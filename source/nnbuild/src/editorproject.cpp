#include "editorproject.h"

#include "duke3d.h"
#include "editorboard.h"
#include "editorprefab.h"
#include "grpscan.h"
#include "miniz.h"
#include "sjson.h"

#include <chrono>
#include <fstream>
#include <limits>
#include <set>

#ifdef _WIN32
# include "windows_inc.h"
#else
# include <unistd.h>
#endif

namespace DukeEditor {

TargetProfile const targetProfiles[7] = {
    {GameTarget::Inherit, "inherit", "Inherit", "", "", 0, 5120},
    {GameTarget::Duke13, "duke13", "Duke Nukem 3D 1.3D", "D3", "DUKE3D.GRP", uint32_t(DUKE13_CRC), 3840},
    {GameTarget::DukeAtomic, "duke_atomic", "Duke Nukem 3D Atomic / Plutonium", "DA", "DUKE3D.GRP", uint32_t(DUKE15_CRC), 5120},
    {GameTarget::Nam, "nam", "NAM", "N", "NAM.GRP", uint32_t(NAM_CRC), 5120},
    {GameTarget::Napalm, "napalm", "NAPALM", "NP", "NAPALM.GRP", uint32_t(NAPALM_CRC), 5120},
    {GameTarget::Ww2gi, "ww2gi", "WWII GI", "GI", "WW2GI.GRP", uint32_t(WW2GI_CRC), 5120},
    {GameTarget::Standalone, "standalone", "Standalone", "S", "", 0, 5120},
};

TargetProfile const &Target(GameTarget target)
{
    return targetProfiles[unsigned(target) < ARRAY_SIZE(targetProfiles) ? unsigned(target) : 0];
}

GameTarget ParseTarget(const char *id)
{
    for (auto const &profile : targetProfiles)
        if (!Bstrcmp(id, profile.id))
            return profile.target;
    return GameTarget::Inherit;
}

GameTarget RunningTarget()
{
    if (ProjectStandalone()) return GameTarget::Standalone;
    if (WW2GI) return GameTarget::Ww2gi;
    if (NAPALM) return GameTarget::Napalm;
    if (NAM) return GameTarget::Nam;
    if (g_selectedGrp && uint32_t(g_selectedGrp->type->crcval) == uint32_t(DUKE13_CRC))
        return GameTarget::Duke13;
    return GameTarget::DukeAtomic;
}

namespace {

constexpr size_t maxMetadata = 256 * 1024;
constexpr size_t maxMapMetadata = maxScriptAttachments * (BMAX_PATH + 128);
constexpr size_t maxPrefabMetadata = 8 * 1024 * 1024;
constexpr unsigned maxAssets = 16384;
constexpr uint32_t maxArchiveBytes = 0x7fffffff;

const char *extension(std::string const &path)
{
    auto const dot = path.find_last_of('.');
    auto const slash = path.find_last_of("/\\");
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? "" : path.c_str() + dot;
}

std::string joined(std::string const &folder, std::string const &child)
{
    if (folder.empty()) return child;
    return folder + (folder.back() == '/' || folder.back() == '\\' ? "" : "/") + child;
}

std::string parent(std::string const &path)
{
    auto const separator = path.find_last_of("/\\");
    return separator == std::string::npos ? "" : path.substr(0, separator);
}

std::string lower(std::string path)
{
    for (auto &ch : path)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return path;
}

enum class FileKind { Missing, File, Directory, Other };

FileKind fileKind(std::string const &path)
{
#ifdef _WIN32
    DWORD const flags = GetFileAttributesA(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES)
        return FileKind::Missing;
    if (flags & FILE_ATTRIBUTE_REPARSE_POINT)
        return FileKind::Other;
    return flags & FILE_ATTRIBUTE_DIRECTORY ? FileKind::Directory : FileKind::File;
#else
    struct stat info;
    if (lstat(path.c_str(), &info) < 0)
        return errno == ENOENT ? FileKind::Missing : FileKind::Other;
    return S_ISREG(info.st_mode) ? FileKind::File : S_ISDIR(info.st_mode) ? FileKind::Directory : FileKind::Other;
#endif
}

bool replaceFile(std::string const &from, std::string const &to, bool overwrite)
{
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(), overwrite ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
#else
    if (overwrite)
        return rename(from.c_str(), to.c_str()) == 0;
    if (link(from.c_str(), to.c_str()) < 0)
        return false;
    unlink(from.c_str());
    return true;
#endif
}

std::string temporaryBeside(std::string const &file)
{
    auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return file + ".nnbuild-" + std::to_string(stamp) + ".tmp";
}

const char *jsonString(sjson_node *obj, const char *key)
{
    auto const node = sjson_find_member(obj, key);
    return node && node->tag == SJSON_STRING ? node->string_ : "";
}

bool safeJson(std::string const &text)
{
    int depth = 0;
    bool quote = false, escape = false;
    for (char ch : text)
    {
        if (!ch) return false;
        if (quote)
        {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') quote = false;
        }
        else if (ch == '"') quote = true;
        else if ((ch == '{' || ch == '[') && ++depth > 32) return false;
        else if ((ch == '}' || ch == ']') && --depth < 0) return false;
    }
    return !quote && depth == 0;
}

sjson_node *readJson(sjson_context *ctx, std::string const &path, size_t limit = maxMetadata)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto const length = file.tellg();
    if (length <= 0 || length > std::streamoff(limit))
        return nullptr;
    std::string text(size_t(length), '\0');
    file.seekg(0);
    if (!file.read(&text[0], length) || !safeJson(text))
        return nullptr;
    auto const obj = sjson_decode(ctx, text.c_str());
    auto const version = sjson_find_member(obj, "version");
    return version && version->tag == SJSON_NUMBER && version->number_ == 1 ? obj : nullptr;
}

bool writeJson(sjson_context *ctx, sjson_node *obj, std::string const &path, bool overwrite = true, size_t limit = maxMetadata)
{
    sjson_put_int(ctx, obj, "version", 1);
    auto const data = sjson_stringify(ctx, obj, "  ");
    auto const length = strlen(data);
    if (length > limit)
    {
        sjson_free_string(ctx, data);
        return false;
    }
    auto const temporary = temporaryBeside(path);
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (stream)
        stream.write(data, length);
    stream.close();
    sjson_free_string(ctx, data);
    if (stream && replaceFile(temporary, path, overwrite))
        return true;
    remove(temporary.c_str());
    return false;
}

std::string assetLocation(std::string const &root, std::string const &path, bool createDirectories)
{
    if (root.empty() || !ValidAssetPath(path))
        return {};
    for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
    {
        auto const dir = joined(root, path.substr(0, slash));
        auto kind = fileKind(dir);
        if (kind == FileKind::Missing && createDirectories)
        {
            Bmkdir(dir.c_str(), S_IRWXU);
            kind = fileKind(dir);
        }
        if (kind != FileKind::Directory)
            return {};
    }
    return joined(root, path);
}

struct PrefabFields
{
    sjson_context *ctx;
    sjson_node *object;
    bool writing;
    bool valid = true;

    template <class T> void number(char const *key, T &value)
    {
        if (writing)
            sjson_put_int(ctx, object, key, value);
        else
        {
            auto const node = sjson_find_member(object, key);
            if (!node || node->tag != SJSON_NUMBER || node->number_ < std::numeric_limits<T>::min() ||
                node->number_ > std::numeric_limits<T>::max() || node->number_ != double(T(node->number_)))
            {
                valid = false;
                return;
            }
            value = T(node->number_);
        }
    }

    void sprite(uspritetypev7 &item)
    {
        number("x", item.x);
        number("y", item.y);
        number("z", item.z);
        number("tile", item.picnum);
        number("flags", item.cstat);
        number("palette", item.pal);
        number("shade", item.shade);
        number("blend", item.blend);
        number("clip_distance", item.clipdist);
        number("repeat_x", item.xrepeat);
        number("repeat_y", item.yrepeat);
        number("offset_x", item.xoffset);
        number("offset_y", item.yoffset);
        number("angle", item.ang);
        number("status", item.statnum);
        number("owner", item.owner);
        number("velocity_x", item.xvel);
        number("velocity_y", item.yvel);
        number("velocity_z", item.zvel);
        number("lotag", item.lotag);
        number("hitag", item.hitag);
        number("extra", item.extra);
    }
};

bool validPrefab(Prefab const &prefab)
{
    if (prefab.target == GameTarget::Inherit || prefab.sprites.empty() || prefab.sprites.size() > MAXSPRITESV7)
        return false;
    size_t components = 0;
    for (auto const &entry : prefab.sprites)
    {
        auto const &item = entry.sprite;
        components += entry.components.size();
        if (item.x < -2 * BXY_MAX || item.x > 2 * BXY_MAX || item.y < -2 * BXY_MAX || item.y > 2 * BXY_MAX ||
            unsigned(item.picnum) >= unsigned(Target(prefab.target).tiles) || unsigned(item.ang) >= 2048 ||
            unsigned(item.statnum) >= MAXSTATUS || item.owner < -1 || item.owner >= int(prefab.sprites.size()) ||
            (item.cstat & ~0x83ff) || (item.cstat & 48) == 48 || components > maxScriptAttachments)
            return false;
        for (auto const &script : entry.components)
            if (!ValidAssetPath(script) || !IsScriptAsset(script))
                return false;
    }
    return true;
}

bool validAttachment(ScriptAttachment const &attachment)
{
    return attachment.index >= -1 && attachment.index < 8192 &&
           (attachment.kind == "sector" || attachment.kind == "wall" || attachment.kind == "sprite") &&
           ValidAssetPath(attachment.script) && IsScriptAsset(attachment.script);
}

bool collect(std::string const &root, std::string const &dir, std::vector<ProjectAsset> &items, std::set<std::string> &names)
{
    auto const handle = Bopendir(joined(root, dir).c_str());
    if (!handle)
        return false;
    bool valid = true;
    while (auto const entry = Breaddir(handle))
    {
        if (!Bstrcmp(entry->name, ".") || !Bstrcmp(entry->name, ".."))
            continue;
        auto const path = joined(dir, entry->name);
        auto const kind = fileKind(joined(root, path));
        if (kind != FileKind::File && kind != FileKind::Directory)
            continue;
        if (items.size() == maxAssets || !ValidAssetPath(path) || !names.insert(lower(path)).second)
        {
            valid = false;
            break;
        }
        bool const directory = kind == FileKind::Directory;
        items.push_back({path, directory});
        if (directory && !collect(root, path, items, names))
        {
            valid = false;
            break;
        }
    }
    Bclosedir(handle);
    return valid;
}

} // namespace

bool ValidAssetPath(std::string const &path)
{
    if (path.empty() || path.size() >= BMAX_PATH - 1 || path.back() == '/')
        return false;
    for (unsigned char c : path)
        if (c < 32 || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    for (size_t begin = 0, end; begin < path.size(); begin = end + 1)
    {
        end = path.find('/', begin);
        if (end == std::string::npos) end = path.size();
        if (end == begin || path[end - 1] == '.' || path[end - 1] == ' ' || end - begin > 255)
            return false;
        auto component = lower(path.substr(begin, end - begin));
        auto const dot = component.find('.');
        if (dot != std::string::npos) component.resize(dot);
        if (component == "con" || component == "nul" || component == "prn" || component == "aux" ||
            (component.size() == 4 && (component.compare(0, 3, "com") == 0 || component.compare(0, 3, "lpt") == 0) &&
             component[3] >= '1' && component[3] <= '9'))
            return false;
    }
    return true;
}

bool IsMapAsset(std::string const &path) { return !Bstrcasecmp(extension(path), ".map"); }
bool IsPrefabAsset(std::string const &path) { return !Bstrcasecmp(extension(path), prefabSuffix); }
bool IsScriptAsset(std::string const &path)
{
    auto const ext = extension(path);
    return !Bstrcasecmp(ext, ".js") || !Bstrcasecmp(ext, ".ts") || !Bstrcasecmp(ext, ".mjs") || !Bstrcasecmp(ext, ".mts");
}

bool ProjectFileExists(std::string const &root, std::string const &path)
{
    auto const file = assetLocation(root, path, false);
    return !file.empty() && fileKind(file) == FileKind::File;
}

bool ReadPrefab(std::string const &root, std::string const &path, Prefab &prefab, std::string &error)
{
    auto const file = assetLocation(root, path, false);
    if (!IsPrefabAsset(path) || file.empty() || fileKind(file) != FileKind::File)
    {
        error = "Choose an existing prefab asset in the project.";
        return false;
    }
    auto const ctx = sjson_create_context(0, 0, nullptr);
    auto const obj = readJson(ctx, file, maxPrefabMetadata);
    auto const sprites = sjson_find_member(obj, "sprites");
    Prefab result;
    result.target = ParseTarget(jsonString(obj, "target"));
    bool valid = sprites && sprites->tag == SJSON_ARRAY && sjson_child_count(sprites) <= MAXSPRITESV7;
    size_t totalComponents = 0;
    if (valid)
    {
        sjson_node *item;
        sjson_foreach(item, sprites)
        {
            PrefabSprite entry;
            PrefabFields fields{ctx, item, false};
            fields.sprite(entry.sprite);
            entry.sprite.sectnum = -1;
            auto const scripts = sjson_find_member(item, "components");
            if (!fields.valid || !scripts || scripts->tag != SJSON_ARRAY ||
                totalComponents + sjson_child_count(scripts) > maxScriptAttachments)
            {
                valid = false;
                break;
            }
            sjson_node *script;
            sjson_foreach(script, scripts)
            {
                if (script->tag != SJSON_STRING)
                {
                    valid = false;
                    break;
                }
                entry.components.emplace_back(script->string_);
            }
            if (!valid) break;
            totalComponents += entry.components.size();
            result.sprites.push_back(std::move(entry));
        }
    }
    sjson_destroy_context(ctx);
    if (!valid || !validPrefab(result))
    {
        error = "The prefab file has invalid sprite or component data.";
        return false;
    }
    prefab = std::move(result);
    return true;
}

bool WritePrefab(std::string const &root, std::string const &path, Prefab const &prefab, bool overwrite, std::string &error)
{
    if (!IsPrefabAsset(path) || !validPrefab(prefab))
    {
        error = "Choose a .vbprefab path and sprites within the project target's map limits.";
        return false;
    }
    auto const file = assetLocation(root, path, true);
    if (file.empty() || (!overwrite && fileKind(file) != FileKind::Missing))
    {
        error = "Choose a new prefab filename inside the project.";
        return false;
    }
    auto const ctx = sjson_create_context(0, 0, nullptr);
    auto const obj = sjson_mkobject(ctx);
    sjson_put_string(ctx, obj, "target", Target(prefab.target).id);
    auto const sprites = sjson_put_array(ctx, obj, "sprites");
    for (auto const &entry : prefab.sprites)
    {
        auto const item = sjson_mkobject(ctx);
        auto sprite = entry.sprite;
        PrefabFields{ctx, item, true}.sprite(sprite);
        auto const components = sjson_put_array(ctx, item, "components");
        for (auto const &script : entry.components)
            sjson_append_element(components, sjson_mkstring(ctx, script.c_str()));
        sjson_append_element(sprites, item);
    }
    bool const written = writeJson(ctx, obj, file, overwrite, maxPrefabMetadata);
    sjson_destroy_context(ctx);
    if (!written)
        error = "Could not save the prefab. Check the destination and free space.";
    return written;
}

bool CreateComponentAsset(std::string const &root, std::string const &path, std::string &error)
{
    if (!IsScriptAsset(path))
    {
        error = "Choose a JavaScript or TypeScript filename.";
        return false;
    }
    auto const file = assetLocation(root, path, true);
    if (file.empty() || fileKind(file) != FileKind::Missing)
    {
        error = "Choose a new component filename inside the project.";
        return false;
    }
    auto const ext = extension(path);
    bool const ts = !Bstrcasecmp(ext, ".ts") || !Bstrcasecmp(ext, ".mts");
    auto const temporary = temporaryBeside(file);
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (ts)
        out << "type ComponentContext = { kind: string; index: number; map: string; state: Record<string, unknown> };\n\n";
    auto const context = ts ? "component: ComponentContext" : "component";
    auto const delta = ts ? "deltaSeconds: number" : "deltaSeconds";
    out << "export function onAttach(" << context << ") {\n"
           "    // Initialize component.state for this instance.\n}\n\n"
        << "export function onUpdate(" << context << ", " << delta << ") {\n"
           "    // Update visuals each frame.\n}\n\n"
        << "export function onFixedUpdate(" << context << ", " << delta << ") {\n"
           "    // Update gameplay each simulation tick.\n}\n";
    out.close();
    if (out && replaceFile(temporary, file, false))
        return true;
    remove(temporary.c_str());
    error = "Could not create the component asset.";
    return false;
}

bool ReadProject(std::string const &folder, ProjectSettings &settings, std::string &error)
{
    auto const ctx = sjson_create_context(0, 0, nullptr);
    auto const obj = readJson(ctx, joined(folder, projectManifest));
    ProjectSettings next;
    next.name = jsonString(obj, "name");
    next.target = ParseTarget(jsonString(obj, "target"));
    next.entry = jsonString(obj, "entry");
    auto const modern = sjson_find_member(obj, "modern");
    next.modern = modern && modern->tag == SJSON_BOOL && modern->bool_;
    sjson_destroy_context(ctx);
    if (next.name.empty() || next.name.size() > 128 || next.target == GameTarget::Inherit ||
        (!next.entry.empty() && (!ValidAssetPath(next.entry) || !IsMapAsset(next.entry))))
    {
        error = "The folder needs a valid nnbuild.project.json.";
        return false;
    }
    settings = std::move(next);
    return true;
}

bool WriteProject(std::string const &folder, ProjectSettings const &settings, std::string &error)
{
    if (settings.name.empty() || settings.name.size() > 128 || settings.target == GameTarget::Inherit ||
        (!settings.entry.empty() && (!ValidAssetPath(settings.entry) || !IsMapAsset(settings.entry))))
    {
        error = "Choose a project name, game, and optional project-relative entry map.";
        return false;
    }
    auto const ctx = sjson_create_context(0, 0, nullptr);
    auto const obj = sjson_mkobject(ctx);
    sjson_put_string(ctx, obj, "name", settings.name.c_str());
    sjson_put_string(ctx, obj, "target", Target(settings.target).id);
    sjson_put_string(ctx, obj, "entry", settings.entry.c_str());
    sjson_put_bool(ctx, obj, "modern", settings.modern);
    bool const saved = writeJson(ctx, obj, joined(folder, projectManifest));
    sjson_destroy_context(ctx);
    if (!saved)
        error = "Could not write the project settings.";
    return saved;
}

bool CreateProject(std::string const &folder, ProjectSettings const &settings, std::string &error)
{
    auto const kind = fileKind(folder);
    if (kind != FileKind::Directory && Bmkdir(folder.c_str(), S_IRWXU))
    {
        error = "Could not create the project folder.";
        return false;
    }
    if (fileKind(joined(folder, projectManifest)) != FileKind::Missing)
    {
        error = "That folder already has project settings.";
        return false;
    }
    return WriteProject(folder, settings, error);
}

bool ReadMapSettings(std::string const &map, MapSettings &settings, std::string &error)
{
    auto const file = map + mapSettingsSuffix;
    if (fileKind(file) == FileKind::Missing)
    {
        settings = {};
        return true;
    }
    auto const ctx = sjson_create_context(0, 0, nullptr);
    auto const obj = readJson(ctx, file, maxMapMetadata);
    MapSettings next;
    next.target = ParseTarget(jsonString(obj, "target"));
    auto const modern = sjson_find_member(obj, "modern");
    if (modern && modern->tag == SJSON_BOOL) next.modern = modern->bool_;
    auto const scripts = sjson_find_member(obj, "scripts");
    bool valid = obj && (!scripts || (scripts->tag == SJSON_ARRAY && unsigned(sjson_child_count(scripts)) <= maxScriptAttachments));
    if (valid && scripts)
    {
        sjson_node *item;
        sjson_foreach(item, scripts)
        {
            auto const index = sjson_find_member(item, "index");
            ScriptAttachment attachment{jsonString(item, "kind"), jsonString(item, "script"), -2};
            if (index && index->tag == SJSON_NUMBER && index->number_ >= -1 && index->number_ < 8192 &&
                int(index->number_) == index->number_)
                attachment.index = index->number_;
            if (!validAttachment(attachment))
            {
                valid = false;
                break;
            }
            next.scripts.push_back(std::move(attachment));
        }
    }
    sjson_destroy_context(ctx);
    if (!valid)
    {
        error = "The map's settings file is invalid.";
        return false;
    }
    settings = std::move(next);
    return true;
}

bool WriteMapSettings(std::string const &map, MapSettings const &settings, std::string &error)
{
    if (settings.modern < -1 || settings.modern > 1 || settings.scripts.size() > maxScriptAttachments ||
        !std::all_of(settings.scripts.begin(), settings.scripts.end(), validAttachment))
    {
        error = "The map has invalid script attachments or settings.";
        return false;
    }
    auto const ctx = sjson_create_context(0, 0, nullptr);
    auto const obj = sjson_mkobject(ctx);
    sjson_put_string(ctx, obj, "target", Target(settings.target).id);
    if (settings.modern >= 0) sjson_put_bool(ctx, obj, "modern", settings.modern != 0);
    auto const scripts = sjson_put_array(ctx, obj, "scripts");
    for (auto const &attachment : settings.scripts)
    {
        auto const item = sjson_mkobject(ctx);
        sjson_put_string(ctx, item, "kind", attachment.kind.c_str());
        sjson_put_string(ctx, item, "script", attachment.script.c_str());
        sjson_put_int(ctx, item, "index", attachment.index);
        sjson_append_element(scripts, item);
    }
    bool const saved = writeJson(ctx, obj, map + mapSettingsSuffix, true, maxMapMetadata);
    sjson_destroy_context(ctx);
    if (!saved)
        error = "Could not write the map settings.";
    return saved;
}

bool WriteProjectMap(std::string const &map, Board const &board, int tileLimit,
                     MapSettings const &settings, std::string &error)
{
    auto const sidecar = map + mapSettingsSuffix;
    auto const mapKind = fileKind(map), settingsKind = fileKind(sidecar);
    if ((mapKind != FileKind::File && mapKind != FileKind::Missing) ||
        (settingsKind != FileKind::File && settingsKind != FileKind::Missing))
    {
        error = "Choose regular filenames for the map and its settings.";
        return false;
    }

    auto const stagedMap = temporaryBeside(map);
    auto const stagedSettings = stagedMap + mapSettingsSuffix;
    auto const backup = stagedMap + ".bak";
    bool const existing = mapKind == FileKind::File;
    bool saved = false, preserveBackup = false;
    if (WriteMapSettings(stagedMap, settings, error) && WriteBoard(stagedMap, board, tileLimit, error) &&
        (!existing || CopyProjectAsset(map, backup, false, error)))
    {
        if (!replaceFile(stagedMap, map, true))
            error = "Could not replace the map file.";
        else if (replaceFile(stagedSettings, sidecar, true))
            saved = true;
        else
        {
            bool const restored = existing ? replaceFile(backup, map, true) : remove(map.c_str()) == 0;
            preserveBackup = existing && !restored;
            error = "Could not replace the map's settings.";
            if (preserveBackup)
                error += " The previous map is available at " + backup;
            else if (!restored)
                error += " The new map file is available at " + map;
        }
    }
    remove(stagedMap.c_str());
    remove(stagedSettings.c_str());
    if (!preserveBackup)
        remove(backup.c_str());
    return saved;
}

bool CopyProjectAsset(std::string const &source, std::string const &destination, bool overwrite, std::string &error)
{
    if (fileKind(source) != FileKind::File)
    {
        error = "Select a regular file to import.";
        return false;
    }
    auto const temporary = temporaryBeside(destination);
    std::ifstream in(source, std::ios::binary);
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    char data[32768];
    while (in && out)
    {
        in.read(data, sizeof(data));
        out.write(data, in.gcount());
    }
    out.close();
    if (!in.bad() && in.eof() && out && replaceFile(temporary, destination, overwrite))
        return true;
    remove(temporary.c_str());
    error = "Could not copy the asset. Choose a different filename if it already exists.";
    return false;
}

std::vector<ProjectAsset> ListProjectAssets(std::string const &folder)
{
    std::vector<ProjectAsset> items;
    std::set<std::string> names;
    collect(folder, "", items, names);
    std::sort(items.begin(), items.end(), [](ProjectAsset const &a, ProjectAsset const &b) { return a.path < b.path; });
    return items;
}

namespace {

constexpr uint32_t zipLocal = 0x04034b50, zipCentral = 0x02014b50, zipEnd = 0x06054b50;

uint16_t get16(unsigned char const *data)
{
    return uint16_t(data[0]) | uint16_t(data[1]) << 8;
}

uint32_t get32(unsigned char const *data)
{
    return uint32_t(get16(data)) | uint32_t(get16(data + 2)) << 16;
}

void put16(std::ostream &out, uint32_t value)
{
    out.put(char(value));
    out.put(char(value >> 8));
}

void put32(std::ostream &out, uint32_t value)
{
    put16(out, value);
    put16(out, value >> 16);
}

struct ArchiveEntry
{
    std::string name;
    uint32_t offset = 0, compressed = 0, bytes = 0, crc = 0;
    uint16_t method = 0, flags = 0x800;
    bool directory = false;
};

void record(std::ostream &out, ArchiveEntry const &entry, bool central)
{
    put32(out, central ? zipCentral : zipLocal);
    if (central) put16(out, 0x314); // ZIP 2.0 with Unix attributes.
    put16(out, 20);
    put16(out, entry.flags);
    put16(out, entry.method);
    put16(out, 0);
    put16(out, 33); // 1980-01-01
    put32(out, entry.crc);
    put32(out, entry.compressed);
    put32(out, entry.bytes);
    put16(out, entry.name.size() + entry.directory);
    put16(out, 0);
    if (central)
    {
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, entry.directory ? (0040700u << 16) | 16 : 0100600u << 16);
        put32(out, entry.offset);
    }
    out << entry.name;
    if (entry.directory) out.put('/');
}

bool writeArchive(std::string const &root, std::vector<ProjectAsset> const &files, std::fstream &out)
{
    std::vector<ArchiveEntry> entries;
    char buffer[32768];
    for (auto const &asset : files)
    {
        ArchiveEntry entry;
        entry.name = asset.path;
        entry.directory = asset.directory;
        auto const position = out.tellp();
        if (position < 0 || position > maxArchiveBytes)
            return false;
        entry.offset = position;
        record(out, entry, false);
        if (!entry.directory)
        {
            std::ifstream in(joined(root, asset.path), std::ios::binary);
            while (in && out)
            {
                in.read(buffer, sizeof(buffer));
                auto const count = in.gcount();
                if (uint64_t(entry.bytes) + count + entry.offset > maxArchiveBytes)
                    return false;
                out.write(buffer, count);
                entry.crc = mz_crc32(entry.crc, reinterpret_cast<unsigned char const *>(buffer), count);
                entry.bytes += count;
            }
            if (!in.eof() || in.bad() || !out)
                return false;
            entry.compressed = entry.bytes;
            auto const end = out.tellp();
            out.seekp(position);
            record(out, entry, false);
            out.seekp(end);
        }
        entries.push_back(std::move(entry));
    }
    auto const central = out.tellp();
    for (auto const &entry : entries)
        record(out, entry, true);
    auto const end = out.tellp();
    if (central < 0 || end < 0 || end > maxArchiveBytes)
        return false;
    put32(out, zipEnd);
    put16(out, 0);
    put16(out, 0);
    put16(out, entries.size());
    put16(out, entries.size());
    put32(out, end - central);
    put32(out, central);
    put16(out, 0);
    return bool(out);
}

bool readDirectory(std::ifstream &in, std::vector<ArchiveEntry> &files, uint32_t &central)
{
    in.seekg(0, std::ios::end);
    auto const length = in.tellg();
    if (length < 22 || length > maxArchiveBytes)
        return false;
    size_t const size = std::min<std::streamoff>(length, 65557);
    std::vector<unsigned char> tail(size);
    in.seekg(length - std::streamoff(size));
    if (!in.read(reinterpret_cast<char *>(tail.data()), size))
        return false;
    int footer = size - 22;
    while (footer >= 0 && (get32(&tail[footer]) != zipEnd || footer + 22u + get16(&tail[footer + 20]) != size))
        --footer;
    if (footer < 0)
        return false;
    auto const end = &tail[footer];
    uint32_t const directorySize = get32(end + 12);
    unsigned const entries = get16(end + 10);
    central = get32(end + 16);
    if (!entries || entries > maxAssets || get16(end + 4) || get16(end + 6) || get16(end + 8) != entries ||
        uint64_t(central) + directorySize != uint64_t(length) - size + footer)
        return false;

    in.seekg(central);
    std::set<std::string> names;
    uint64_t unpacked = 0;
    for (unsigned i = 0; i < entries; ++i)
    {
        unsigned char header[46];
        if (!in.read(reinterpret_cast<char *>(header), sizeof(header)) || get32(header) != zipCentral)
            return false;
        ArchiveEntry entry;
        entry.flags = get16(header + 8);
        entry.method = get16(header + 10);
        entry.crc = get32(header + 16);
        entry.compressed = get32(header + 20);
        entry.bytes = get32(header + 24);
        entry.offset = get32(header + 42);
        uint16_t const nameSize = get16(header + 28);
        auto const mode = get16(header + 40) & 0170000;
        if (!nameSize || nameSize >= BMAX_PATH || get16(header + 34) ||
            (entry.flags & ~0x80e) || (entry.method != 0 && entry.method != 8) ||
            (mode && mode != 0040000 && mode != 0100000) ||
            entry.offset >= central || uint64_t(entry.compressed) + entry.offset > central)
            return false;
        entry.name.resize(nameSize);
        if (!in.read(&entry.name[0], nameSize))
            return false;
        entry.directory = entry.name.back() == '/';
        if (entry.directory)
            entry.name.pop_back();
        if (!ValidAssetPath(entry.name) || !names.insert(lower(entry.name)).second ||
            (entry.directory && entry.bytes) || (!entry.method && entry.compressed != entry.bytes))
            return false;
        unpacked += entry.bytes;
        if (unpacked > maxArchiveBytes)
            return false;
        in.seekg(get16(header + 30) + get16(header + 32), std::ios::cur);
        files.push_back(std::move(entry));
    }
    return in.tellg() == std::streamoff(uint64_t(central) + directorySize);
}

bool extract(std::ifstream &in, std::ofstream &out, ArchiveEntry const &entry, uint32_t central)
{
    in.seekg(entry.offset);
    unsigned char local[30];
    if (!in.read(reinterpret_cast<char *>(local), sizeof(local)) || get32(local) != zipLocal ||
        get16(local + 6) != entry.flags || get16(local + 8) != entry.method || get16(local + 26) != entry.name.size())
        return false;
    std::string name(entry.name.size(), '\0');
    if (!in.read(&name[0], name.size()) || name != entry.name)
        return false;
    in.seekg(get16(local + 28), std::ios::cur);
    if (!in || uint64_t(in.tellg()) + entry.compressed > central)
        return false;

    unsigned char input[32768], output[32768];
    uint32_t left = entry.compressed, bytes = 0, crc = 0;
    auto write = [&](unsigned char const *data, size_t size) {
        if (size > entry.bytes - bytes)
            return false;
        out.write(reinterpret_cast<const char *>(data), size);
        crc = mz_crc32(crc, data, size);
        bytes += size;
        return bool(out);
    };
    if (entry.method == 0)
    {
        while (left)
        {
            size_t const length = std::min<size_t>(left, sizeof(input));
            if (!in.read(reinterpret_cast<char *>(input), length) || !write(input, length))
                return false;
            left -= length;
        }
    }
    else
    {
        mz_stream stream{};
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
            return false;
        int result = MZ_OK;
        do
        {
            if (!stream.avail_in && left)
            {
                stream.avail_in = std::min<size_t>(left, sizeof(input));
                if (!in.read(reinterpret_cast<char *>(input), stream.avail_in))
                    break;
                left -= stream.avail_in;
                stream.next_in = input;
            }
            stream.next_out = output;
            stream.avail_out = sizeof(output);
            result = mz_inflate(&stream, MZ_NO_FLUSH);
            if (!write(output, sizeof(output) - stream.avail_out))
            {
                result = MZ_DATA_ERROR;
                break;
            }
        } while (result == MZ_OK);
        bool const finished = result == MZ_STREAM_END && !left && !stream.avail_in;
        mz_inflateEnd(&stream);
        if (!finished)
            return false;
    }
    return bytes == entry.bytes && crc == entry.crc;
}

bool makeParents(std::string const &folder, std::string const &relative, std::vector<std::string> &created)
{
    if (relative.empty())
        return true;
    auto const path = joined(folder, relative);
    auto const kind = fileKind(path);
    if (kind == FileKind::Directory)
        return true;
    if (kind != FileKind::Missing || !makeParents(folder, parent(relative), created) || Bmkdir(path.c_str(), S_IRWXU))
        return false;
    created.push_back(path);
    return true;
}

} // namespace

bool IsProjectArchive(std::string const &archive)
{
    std::ifstream in(archive, std::ios::binary);
    std::vector<ArchiveEntry> files;
    uint32_t offset;
    return in && readDirectory(in, files, offset) && std::any_of(files.begin(), files.end(), [](ArchiveEntry const &item) {
        return !item.directory && item.name == projectManifest;
    });
}

bool ExportProject(std::string const &folder, std::string const &archive, bool overwrite, std::string &error)
{
    ProjectSettings settings;
    if (!ReadProject(folder, settings, error))
        return false;
    std::vector<ProjectAsset> files;
    std::set<std::string> names;
    if (!collect(folder, "", files, names))
    {
        error = "Some asset paths are invalid, duplicated, or exceed the ZIP limits.";
        return false;
    }
    if (!settings.entry.empty() && fileKind(joined(folder, settings.entry)) != FileKind::File)
    {
        error = "The selected entry map is missing.";
        return false;
    }
    for (auto const &asset : files)
        if (!asset.directory && IsMapAsset(asset.path))
        {
            auto const path = joined(folder, asset.path);
            MapSettings mapSettings;
            if (!ReadMapSettings(path, mapSettings, error))
                return false;
            auto const target = mapSettings.target == GameTarget::Inherit ? settings.target : mapSettings.target;
            Board map;
            if (!ReadBoard(path, map, Target(target).tiles, error))
            {
                error = asset.path + " could not be exported for " + Target(target).label + ". " + error;
                return false;
            }
        }
    files.erase(std::remove_if(files.begin(), files.end(), [&](ProjectAsset const &asset) {
        return !asset.directory && joined(folder, asset.path) == archive;
    }), files.end());
    std::sort(files.begin(), files.end(), [](ProjectAsset const &a, ProjectAsset const &b) { return a.path < b.path; });
    auto const temporary = temporaryBeside(archive);
    std::fstream out(temporary, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    bool const saved = out && writeArchive(folder, files, out);
    out.close();
    if (saved && out && replaceFile(temporary, archive, overwrite))
        return true;
    remove(temporary.c_str());
    error = "Could not export the ZIP. Check the destination, free space, and archive size.";
    return false;
}

bool ImportProject(std::string const &archive, std::string const &folder, std::string &error)
{
    std::ifstream in(archive, std::ios::binary);
    std::vector<ArchiveEntry> entries;
    uint32_t central;
    if (!in || !readDirectory(in, entries, central) ||
        std::none_of(entries.begin(), entries.end(), [](ArchiveEntry const &e) { return !e.directory && e.name == projectManifest; }))
    {
        error = "Choose a valid NNBuild project ZIP.";
        return false;
    }
    if (Bmkdir(folder.c_str(), S_IRWXU))
    {
        error = "Choose a new destination folder whose parent already exists.";
        return false;
    }
    std::vector<std::string> folders{folder}, files;
    bool ok = true;
    for (auto const &entry : entries)
    {
        if (!makeParents(folder, entry.directory ? entry.name : parent(entry.name), folders))
        {
            ok = false;
            break;
        }
        if (entry.directory)
            continue;
        auto const path = joined(folder, entry.name);
        if (fileKind(path) != FileKind::Missing)
        {
            ok = false;
            break;
        }
        std::ofstream out(path, std::ios::binary);
        if (out)
            files.push_back(path);
        bool const extracted = out && extract(in, out, entry, central);
        out.close();
        if (!extracted || !out)
        {
            ok = false;
            break;
        }
    }
    ProjectSettings settings;
    if (ok && ReadProject(folder, settings, error))
        return true;
    for (auto const &file : files)
        remove(file.c_str());
    for (auto it = folders.rbegin(); it != folders.rend(); ++it)
        rmdir(it->c_str());
    if (error.empty())
        error = "Could not extract every asset from the project ZIP.";
    return false;
}

std::string ImportTemporaryProject(std::string const &archive, std::string &error)
{
    auto const path = CreatePlaytestPath();
    if (!path.empty())
    {
        remove(path.c_str());
        if (ImportProject(archive, path, error))
            return path;
    }
    if (error.empty()) error = "Could not create a working folder for the project.";
    return {};
}

void RemoveTemporaryProject(std::string const &folder)
{
    auto const items = ListProjectAssets(folder);
    for (auto it = items.rbegin(); it != items.rend(); ++it)
    {
        auto const path = joined(folder, it->path);
        if (it->directory) rmdir(path.c_str());
        else remove(path.c_str());
    }
    rmdir(folder.c_str());
}

} // namespace DukeEditor
