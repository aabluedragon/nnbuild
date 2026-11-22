#include "duke3d.h"
#include "mapeditor.h"
#include "editorcontrol.h"

#if defined(USE_OPENGL) && defined(RENDERTYPESDL) && SDL_TARGET >= 2

#include "editorboard.h"
#include "editorlive.h"
#include "editorprefab.h"
#include "editor.h"
#include "grpscan.h"
#include "imgui.h"
#include "nativeeditor.h"
#include "oldnet.h"
#include "polymost.h"
#include "sdl_inc.h"
#include "vbjs.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace {

using DukeEditor::Board;
using DukeEditor::Activity;
using DukeEditor::GameTarget;
using DukeEditor::LiveMode;
using DukeEditor::ObjectKind;
using DukeEditor::PlayPoint;
using DukeEditor::Presence;

enum class Mode { Menu, Edit, Play };
enum class Tool { Select, Room, Sprite, Start, Pillar, Catwalk, Cut, Prefab };
using Kind = ObjectKind;
enum class Action { Idle, New, Open, Join, Save, SaveAs, Play, BuildTools, Title, Quit };

using Selection = DukeEditor::ObjectRef;

struct Snapshot
{
    Board board;
    uint64_t revision;
    std::vector<DukeEditor::ScriptAttachment> scripts;
    std::string label{};
};

struct Drag
{
    Snapshot before;
    std::vector<int> vertices;
    vec2_t cursor;
    bool changed = false;
};

struct Video
{
    int w, h, depth, full, scale, renderer;
};

struct Thumbnail
{
    GLuint texture = 0;
    float ratio = 1;
};

struct FileEntry
{
    std::string name;
    bool directory;
};

struct PlanProjection
{
    ImVec2 min, max, origin;
    float zoom;

    ImVec2 point(vec2_t position) const
    {
        return {(min.x + max.x) * .5f + (position.x - origin.x) * zoom,
                (min.y + max.y) * .5f + (position.y - origin.y) * zoom};
    }

    bool contains(ImVec2 point, float padding = 0.f) const
    {
        return point.x >= min.x - padding && point.y >= min.y - padding &&
               point.x < max.x + padding && point.y < max.y + padding;
    }
};

constexpr ImU32 ink = IM_COL32(15, 21, 27, 255);
constexpr ImU32 plane = IM_COL32(17, 28, 36, 255);
constexpr ImU32 gridMinor = IM_COL32(29, 44, 54, 255);
constexpr ImU32 gridMajor = IM_COL32(47, 66, 78, 255);
constexpr ImU32 wallColor = IM_COL32(163, 184, 197, 255);
constexpr ImU32 portalColor = IM_COL32(79, 204, 189, 255);
constexpr ImU32 accent = IM_COL32(255, 177, 93, 255);
constexpr ImU32 mute = IM_COL32(151, 172, 185, 255);
constexpr ImVec4 accentText(1.f, .71f, .40f, 1.f);
constexpr size_t historyLimit = 48;

Mode mode;
Tool tool;
Action queued, pendingNavigation, fileAction;
Selection selection;
Video gameVideo;
Board board;
std::vector<Snapshot> undo, redo;
std::deque<DukeEditor::ChangeRecord> localChanges;
uint64_t revision, savedRevision, nextRevision;
std::unique_ptr<Drag> drag;
std::vector<vec2_t> outline;
std::string sourcePath, title, notice, initialMap, playtestPath, browserDirectory;
std::vector<FileEntry> browserEntries;
std::string droppedMap, overwritePath;
char filePath[BMAX_PATH * 2], hierarchySearch[64], tileSearch[64];
bool requested, stopPlay, errorMessage, showFile, showDiscard, showOverwrite, discardConfirmed;
bool view3D, fitPlan, viewDirty, floorSurface = true, cameraLook, sceneHovered;
bool showLivePlan = true;
bool fitSelection, waitForF5Release;
bool sectorTagsOpen, wallMappingOpen, spriteTagsOpen;
int artLimit, brush = 1680, grid = 128, depth = 1536;
std::vector<Thumbnail> thumbnails;
std::vector<char const *> tileNames;
std::vector<int> filteredTiles;
ImGuiContext *styledContext;
ImGuiContext *fontContext;
ImGuiStyle gameStyle;
ImFont *editorFont, *gameFont;
float gameFontScale;
int lastGuiFrame;
void (*gameFileDrop)(char const *);
ImVec2 planOrigin, planMin, planMax, sceneMin, sceneMax;
float planZoom = .1f;
vec3_t camera;
int cameraSector, cameraAngle, cameraHorizon;
bool pick3D, pickSurface3D, openSurfaceMenu;
ImVec2 pickPosition, planPress;
PlayPoint queuedPlayPoint;
PlayPoint surfacePoint;
uint64_t surfaceRevision;
bool batchSelection, sectionView, filterFloor, cutAlongX = true, showLinks, showChecks, showChanges;
int floorLayer, cutCoordinate, pillarSector = -1;
int stairSteps = 4, stairTread = 384, stairRise = 8;
int catwalkTile = 342, catwalkLength = 1024, catwalkWidth = 512, catwalkHeight = 48, catwalkAngle;
std::vector<Selection> group;
bool showPreset, showStairs, showCatwalk;
DukeEditor::MechanismSetup preset;
void switchTool(Tool next);
int selectedSector();
void openPreset(DukeEditor::Mechanism type);

bool groupContains(Selection object)
{
    return std::find(group.begin(), group.end(), object) != group.end();
}

void selectObject(Selection object, bool toggle = false)
{
    selection = object;
    if (!batchSelection || !toggle || (object.kind != Kind::Wall && object.kind != Kind::Sprite))
        return;
    auto const existing = std::find(group.begin(), group.end(), object);
    if (existing == group.end())
        group.push_back(object);
    else
        group.erase(existing);
}
uint32_t startPlayMessage;
int previousSkill;
DukeEditor::LiveSession live;
uint32_t liveRevision, livePresentation;
std::map<uint32_t, std::vector<DukeEditor::ScriptAttachment>> livePrefabBindings;
Presence controlPointer;
intptr_t buildToolsProcess = -1;
std::string buildToolsMap;
std::string buildToolsFolder;
Board buildToolsOriginal;
DukeEditor::ProjectWorkspace projectWorkspace;
bool mapSettingsDirty, showProjectDialog, showMapSettingsDialog;
char projectPath[BMAX_PATH * 2], projectArchive[BMAX_PATH * 2], projectAssetPath[BMAX_PATH * 2];
std::string selectedAsset;
std::string prefabBrushPath;
DukeEditor::Prefab prefabBrush;
DukeEditor::ProjectRequest pendingModernSettings;
bool showPrefabDialog, showComponentDialog;
char prefabPath[BMAX_PATH], componentPath[BMAX_PATH];
DukeEditor::ProjectRequest pendingComponent, pendingPrefab;
uint64_t dialogRevision;
bool projectCommand(DukeEditor::ProjectRequest const &request);
constexpr int projectSearchPath = 1 << 7;
char participantName[37], hostAddress[128] = "127.0.0.1";
int listenPort = DukeEditor::editorPort;
GameTarget importTarget = GameTarget::Inherit;
bool targetPicker(const char *label, GameTarget &target, bool inherit);

bool online()
{
    return live.mode() == LiveMode::Hosting || live.mode() == LiveMode::Joined;
}

bool editable()
{
    return live.mode() != LiveMode::Connecting && !live.pending() && (!online() || live.revision() == liveRevision);
}

uint32_t editionIdentity()
{
    return g_selectedGrp ? uint32_t(g_selectedGrp->type->crcval) : 0;
}

bool isDirty()
{
    return revision != savedRevision || mapSettingsDirty;
}

void status(std::string text, bool warning = false)
{
    notice = std::move(text);
    errorMessage = warning;
    if (warning)
        LOG_F(WARNING, "Map Editor: %s", notice.c_str());
}

std::string basenameOf(std::string const &path)
{
    auto const split = path.find_last_of("/\\");
    return path.substr(split == std::string::npos ? 0 : split + 1);
}

std::string directoryOf(std::string const &path)
{
    auto const split = path.find_last_of("/\\");
    return split == std::string::npos ? "." : path.substr(0, split + 1);
}

bool isMap(std::string const &path)
{
    auto const ext = Bstrrchr(path.c_str(), '.');
    return ext && !Bstrcasecmp(ext, ".map");
}

std::string absolutePath(std::string const &path)
{
    if (path.empty() || path.front() == '/' || path.front() == '\\' || (path.size() >= 2 && path[1] == ':'))
        return path;
    return std::string(Net_LaunchDirectory()) + "/" + path;
}

GameTarget projectTarget()
{
    using namespace DukeEditor;
    return projectWorkspace.root.empty() ? RunningTarget() : projectWorkspace.project.target;
}

void updateMapSettings()
{
    using namespace DukeEditor;
    auto &workspace = projectWorkspace;
    workspace.effectiveTarget = workspace.map.target == GameTarget::Inherit ? projectTarget() : workspace.map.target;
    workspace.modern = workspace.map.modern >= 0 ? workspace.map.modern != 0 :
                       !workspace.root.empty() && workspace.project.modern;
}

void refreshProject()
{
    using namespace DukeEditor;
    auto &workspace = projectWorkspace;
    workspace.assets.clear();
    if (!workspace.root.empty())
        for (auto const &asset : ListProjectAssets(workspace.root))
        {
            GameTarget target = workspace.project.target;
            auto map = asset.path;
            constexpr auto suffixLength = sizeof(mapSettingsSuffix) - 1;
            if (map.size() > suffixLength && map.compare(map.size() - suffixLength, suffixLength, mapSettingsSuffix) == 0)
                map.resize(map.size() - suffixLength);
            if (!asset.directory && IsMapAsset(map))
            {
                MapSettings settings;
                std::string error;
                if (ReadMapSettings(workspace.root + "/" + map, settings, error) && settings.target != GameTarget::Inherit)
                    target = settings.target;
            }
            else if (!asset.directory && IsPrefabAsset(asset.path))
            {
                Prefab prefab;
                std::string error;
                if (ReadPrefab(workspace.root, asset.path, prefab, error))
                    target = prefab.target;
            }
            workspace.assets.push_back({asset, target});
        }
    updateMapSettings();
}

bool loadProject(std::string const &path, std::string &error)
{
    DukeEditor::ProjectSettings options;
    auto folder = absolutePath(path);
    while (folder.size() > 1 && (folder.back() == '/' || folder.back() == '\\'))
        folder.pop_back();
    if (!DukeEditor::ReadProject(folder, options, error))
        return false;
    removesearchpaths_withuser(projectSearchPath);
    addsearchpath_user(folder.c_str(), projectSearchPath);
    if (projectWorkspace.root != folder)
        livePrefabBindings.clear();
    projectWorkspace.root = std::move(folder);
    projectWorkspace.project = std::move(options);
    selectedAsset.clear();
    if (tool == Tool::Prefab) tool = Tool::Select;
    refreshProject();
    return true;
}

void reconcileAttachments(Board const &before)
{
    auto &bindings = projectWorkspace.map.scripts;
    if (bindings.empty() || DukeEditor::SameTopology(before, board))
        return;

    for (auto &binding : bindings)
    {
        if (binding.index < 0)
            continue;
        unsigned const index = binding.index;
        int matching = -1;
        auto findUnique = [&](size_t count, auto matches) {
            for (size_t i = 0; i < count; ++i)
                if (matches(i))
                {
                    if (matching != -1)
                    {
                        matching = -1;
                        return;
                    }
                    matching = i;
                }
        };
        if (binding.kind == "sprite" && index < before.sprites.size())
        {
            auto const &old = before.sprites[index];
            auto matches = [&](size_t i) {
                auto const &item = board.sprites[i];
                return item.x == old.x && item.y == old.y && item.z == old.z &&
                       item.picnum == old.picnum && item.lotag == old.lotag && item.hitag == old.hitag;
            };
            if (board.sprites.size() >= before.sprites.size() && matches(index))
                matching = index;
            else
                findUnique(board.sprites.size(), matches);
        }
        else if (binding.kind == "wall" && index < before.walls.size())
        {
            auto const &old = before.walls[index];
            findUnique(board.walls.size(), [&](size_t i) {
                auto const &item = board.walls[i];
                return item.x == old.x && item.y == old.y &&
                       item.picnum == old.picnum && item.lotag == old.lotag && item.hitag == old.hitag;
            });
        }
        else if (binding.kind == "sector" && index < before.sectors.size())
        {
            auto const &old = before.sectors[index];
            auto const &point = before.walls[old.wallptr];
            findUnique(board.sectors.size(), [&](size_t i) {
                auto const &item = board.sectors[i];
                auto const &first = board.walls[item.wallptr];
                return first.x == point.x && first.y == point.y && item.floorz == old.floorz &&
                       item.ceilingz == old.ceilingz && item.lotag == old.lotag;
            });
        }
        if (binding.index != matching)
        {
            binding.index = matching;
            mapSettingsDirty = true;
        }
    }
}

void pushHistory(std::vector<Snapshot> &history, Snapshot previous)
{
    if (history.size() == historyLimit)
        history.erase(history.begin());
    history.push_back(std::move(previous));
}

void logLocalChange(Board const &before, uint64_t changeRevision, std::string const &label, bool assisted)
{
    if (online())
        return;
    localChanges.push_back(DukeEditor::DescribeChange(before, board, changeRevision,
        assisted ? "Assistant" : participantName, label, -1, assisted));
    if (localChanges.size() > historyLimit)
        localChanges.pop_front();
}

bool submitDraft(std::string const &label, bool strict, bool assisted)
{
    if (!online())
        return true;
    if (!live.submit(liveRevision, board, label, strict, assisted))
    {
        auto notice = live.takeNotice();
        status(notice.empty() ? "Wait for the host to confirm the current edit." : std::move(notice), true);
        return false;
    }
    return true;
}

bool commit(Snapshot previous, char const *label = "Edit properties", bool strict = false, bool assisted = false,
            std::vector<DukeEditor::ScriptAttachment> additions = {})
{
    if (!submitDraft(label, strict, assisted))
    {
        // Keep Inspector references valid when a property edit is rejected.
        auto restore = [](auto &target, auto &source) {
            if (target.size() == source.size())
                std::copy(source.begin(), source.end(), target.begin());
            else
                target = std::move(source);
        };
        restore(board.sectors, previous.board.sectors);
        restore(board.walls, previous.board.walls);
        restore(board.sprites, previous.board.sprites);
        board.start = previous.board.start;
        board.angle = previous.board.angle;
        board.startSector = previous.board.startSector;
        return false;
    }
    if (!DukeEditor::SameTopology(previous.board, board))
    {
        group.clear();
        showPreset = false;
    }
    reconcileAttachments(previous.board);
    if (!additions.empty())
    {
        auto &scripts = projectWorkspace.map.scripts;
        scripts.insert(scripts.end(), additions.begin(), additions.end());
        mapSettingsDirty = true;
        if (live.mode() == LiveMode::Hosting)
        {
            livePrefabBindings[live.revision()] = std::move(additions);
            if (livePrefabBindings.size() > historyLimit)
                livePrefabBindings.erase(livePrefabBindings.begin());
            liveRevision = live.revision();
            livePresentation = live.presentation();
        }
    }
    previous.label = label;
    logLocalChange(previous.board, nextRevision + 1, label, assisted);
    if (!online())
    {
        pushHistory(undo, std::move(previous));
        redo.clear();
    }
    revision = ++nextRevision;
    viewDirty = true;
    return true;
}

template <typename F>
bool change(F edit, char const *label = "Edit properties")
{
    if (!editable())
        return false;
    Snapshot previous{board, revision, projectWorkspace.map.scripts};
    if (!edit())
        return false;
    return commit(std::move(previous), label);
}

bool placePrefab(DukeEditor::Prefab const &prefab, std::string const &path, vec2_t position, int sector,
                 bool useLayer, int layer, std::string &error)
{
    using namespace DukeEditor;
    if (projectWorkspace.root.empty() || !projectWorkspace.modern)
    {
        error = "Enable Modern Mode in a project to place prefabs.";
        return false;
    }
    if (!editable() || drag)
    {
        error = "Wait until the draft is ready for editing.";
        return false;
    }
    for (auto const &item : prefab.sprites)
        for (auto const &script : item.components)
            if (!ProjectFileExists(projectWorkspace.root, script))
            {
                error = "Add the prefab's component to the project before placing it. Missing " + script;
                return false;
            }
    Board next = board;
    std::vector<ScriptAttachment> additions;
    if (!InstantiatePrefab(next, prefab, position, sector, useLayer, layer, additions, error) ||
        !next.validate(Target(projectWorkspace.effectiveTarget).tiles, error, true))
        return false;
    if (projectWorkspace.map.scripts.size() + additions.size() > maxScriptAttachments)
    {
        error = "This placement would exceed the map's script component limit.";
        return false;
    }
    if (!additions.empty() && live.mode() == LiveMode::Joined)
    {
        error = "Place scripted prefabs from the host's project workspace, which owns the component bindings.";
        return false;
    }
    int const first = board.sprites.size();
    Snapshot previous{std::move(board), revision, projectWorkspace.map.scripts};
    board = std::move(next);
    auto const label = "Place prefab " + basenameOf(path);
    if (!commit(std::move(previous), label.c_str(), true, false, std::move(additions)))
    {
        error = notice;
        return false;
    }
    selection = {Kind::Sprite, first};
    status("Placed " + basenameOf(path));
    return true;
}

Kind componentKind(std::string const &kind)
{
    if (kind == "sprite") return Kind::Sprite;
    if (kind == "sector") return Kind::Sector;
    if (kind == "wall") return Kind::Wall;
    return Kind::Nothing;
}

char const *componentKind(Selection object)
{
    switch (object.kind)
    {
        case Kind::Sprite: return "sprite";
        case Kind::Sector: return "sector";
        case Kind::Wall: return "wall";
        default: return nullptr;
    }
}

void setComponents(std::vector<DukeEditor::ScriptAttachment> scripts, Selection object, char const *label)
{
    auto &current = projectWorkspace.map.scripts;
    if (!online())
    {
        pushHistory(undo, {board, revision, current, label});
        redo.clear();
        revision = ++nextRevision;
        logLocalChange(board, nextRevision, label, false);
        localChanges.back().touched.push_back(object);
    }
    current = std::move(scripts);
    mapSettingsDirty = true;
    status(label);
}

void pollLive();

bool canUndo() { return online() ? live.undoable() : !undo.empty(); }
bool canRedo() { return online() ? live.redoable() : !redo.empty(); }

bool useLiveHistory(bool replay)
{
    if (!editable() || !live.useHistory(liveRevision, replay))
    {
        status("The live history changed. Review the latest revision.", true);
        return false;
    }
    pollLive();
    return true;
}

void useHistory(std::vector<Snapshot> &from, std::vector<Snapshot> &to)
{
    if (online())
    {
        useLiveHistory(&from == &redo);
        return;
    }
    if (from.empty() || !editable())
        return;
    Snapshot previous{std::move(board), revision, projectWorkspace.map.scripts, from.back().label};
    board = from.back().board;
    logLocalChange(previous.board, nextRevision + 1, (&from == &redo ? "Redo " : "Undo ") + previous.label, false);
    pushHistory(to, std::move(previous));
    auto const &scripts = from.back().scripts;
    if (scripts != projectWorkspace.map.scripts)
    {
        projectWorkspace.map.scripts = scripts;
        mapSettingsDirty = true;
    }
    revision = from.back().revision;
    ++nextRevision;
    from.pop_back();
    selection = {};
    group.clear();
    outline.clear();
    viewDirty = true;
}

void cameraToStart()
{
    camera = board.start;
    cameraSector = board.startSector;
    cameraAngle = board.angle;
    cameraHorizon = 100;
}

bool setDocument(Board next, std::string name, std::string path)
{
    if (live.mode() == LiveMode::Hosting)
    {
        if (liveRevision != live.revision() || !live.submit(liveRevision, next, "Replace draft", true))
        {
            status("The host could not replace the live draft.", true);
            return false;
        }
        liveRevision = live.revision();
    }
    board = std::move(next);
    title = std::move(name);
    sourcePath = std::move(path);
    livePrefabBindings.clear();
    undo.clear();
    redo.clear();
    localChanges.clear();
    group.clear();
    filterFloor = false;
    sectionView = false;
    showPreset = showStairs = showCatwalk = false;
    outline.clear();
    drag.reset();
    revision = savedRevision = ++nextRevision;
    selection = board.sectors.empty() ? Selection{} : Selection{Kind::Sector, 0};
    controlPointer = {};
    projectWorkspace.map = {};
    mapSettingsDirty = false;
    updateMapSettings();
    tool = Tool::Select;
    fitPlan = viewDirty = true;
    fitSelection = false;
    cameraToStart();
    G_UpdateAppTitle("Map Editor");
    return true;
}

void newMap()
{
    if (setDocument(DukeEditor::NewBoard(), "Untitled.map", {}))
        status("Starter room ready");
}

bool openMap(std::string const &path, GameTarget requestedTarget = GameTarget::Inherit)
{
    auto const local = absolutePath(path);
    std::string error;
    Board next;
    bool const onDisk = buildvfs_exists(local.c_str()) && !buildvfs_isdir(local.c_str());
    DukeEditor::MapSettings settings;
    if (onDisk && !DukeEditor::ReadMapSettings(local, settings, error))
    {
        status(error, true);
        return false;
    }
    bool const targetChanged = requestedTarget != GameTarget::Inherit && requestedTarget != settings.target;
    if (requestedTarget != GameTarget::Inherit)
        settings.target = requestedTarget;
    auto const target = settings.target == GameTarget::Inherit ? projectTarget() : settings.target;
    if (!DukeEditor::ReadBoard(onDisk ? local : path, next, DukeEditor::Target(target).tiles, error))
    {
        status(error, true);
        return false;
    }
    if (!setDocument(std::move(next), basenameOf(path), onDisk ? local : ""))
        return false;
    projectWorkspace.map = std::move(settings);
    mapSettingsDirty = targetChanged;
    updateMapSettings();
    status("Opened " + title);
    LOG_F(INFO, "Map Editor: opened %s", path.c_str());
    return true;
}

bool saveMap(std::string const &path)
{
    std::string error;
    auto const &settings = projectWorkspace.map;
    int const tileLimit = DukeEditor::Target(projectWorkspace.effectiveTarget).tiles;
    bool const hasSettings = mapSettingsDirty || settings.target != DukeEditor::GameTarget::Inherit ||
                             settings.modern >= 0 || !settings.scripts.empty() ||
                             buildvfs_exists((path + DukeEditor::mapSettingsSuffix).c_str());
    if (!(hasSettings ? DukeEditor::WriteProjectMap(path, board, tileLimit, settings, error) :
                        DukeEditor::WriteBoard(path, board, tileLimit, error)))
    {
        status(error, true);
        return false;
    }
    sourcePath = path;
    title = basenameOf(path);
    savedRevision = revision;
    mapSettingsDirty = false;
    refreshProject();
    status("Saved " + title + " as Duke3D v7");
    LOG_F(INFO, "Map Editor: saved %s (Duke3D v7)", path.c_str());
    return true;
}

void mapDropped(char const *path)
{
    if (mode != Mode::Edit || !path)
        return;
    auto const ext = Bstrrchr(path, '.');
    if (ext && !Bstrcasecmp(ext, ".zip") && DukeEditor::IsProjectArchive(path))
    {
        Bstrncpyz(projectArchive, path, sizeof(projectArchive));
        showProjectDialog = true;
    }
    else if (isMap(path) && live.mode() != LiveMode::Joined)
    {
        droppedMap = path;
        queued = Action::Open;
    }
    else if (!projectWorkspace.root.empty())
    {
        Bstrncpyz(projectAssetPath, path, sizeof(projectAssetPath));
        showProjectDialog = true;
    }
}

void rebuildTileFilter()
{
    ImGuiTextFilter filter(tileSearch);
    filteredTiles.clear();
    for (int tile = 0; tile < artLimit; ++tile)
        if (tilesiz[tile].x > 0 && tilesiz[tile].y > 0)
        {
            auto const label = std::to_string(tile) + " " + (tileNames[tile] ? tileNames[tile] : "");
            if (filter.PassFilter(label.c_str()))
                filteredTiles.push_back(tile);
        }
}

void releaseThumbnails(bool deleteGL)
{
    if (deleteGL)
        for (auto const &image : thumbnails)
            if (image.texture)
                glDeleteTextures(1, &image.texture);
    thumbnails.assign(artLimit, {});
}

void styleEditor()
{
    styledContext = ImGui::GetCurrentContext();
    gameStyle = ImGui::GetStyle();
    auto &io = ImGui::GetIO();
    gameFontScale = io.FontGlobalScale;
    gameFont = io.FontDefault;
    if (fontContext != styledContext || (lastGuiFrame && !ImGui::GetFrameCount()))
    {
        fontContext = styledContext;
#ifdef _WIN32
        char directory[MAX_PATH];
        UINT const length = GetWindowsDirectoryA(directory, sizeof(directory));
        std::string const fontPath = length > 0 && length < sizeof(directory)
            ? std::string(directory) + "/Fonts/segoeui.ttf" : "";
#elif defined(__APPLE__)
        std::string const fontPath = "/System/Library/Fonts/Supplemental/Arial.ttf";
#else
        std::string const fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
        if (buildvfs_exists(fontPath.c_str()))
            editorFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.f);
        else
        {
            ImFontConfig config;
            config.SizePixels = 15.f;
            editorFont = io.Fonts->AddFontDefaultVector(&config);
        }
    }
    io.FontDefault = editorFont;
    io.FontGlobalScale = 1.f;
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10, 8);
    style.FramePadding = ImVec2(7, 5);
    style.ItemSpacing = ImVec2(7, 6);
    style.WindowRounding = 7;
    style.ChildRounding = style.FrameRounding = style.PopupRounding = 5;
    style.WindowBorderSize = 1;
    style.ScrollbarSize = 10;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.083f, .119f, .151f, 1);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(.078f, .109f, .143f, 1);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(.093f, .133f, .166f, 1);
    style.Colors[ImGuiCol_Border] = ImVec4(.19f, .25f, .31f, 1);
    style.Colors[ImGuiCol_Text] = ImVec4(.90f, .94f, .97f, 1);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(.55f, .65f, .70f, 1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(.12f, .17f, .22f, 1);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.19f, .26f, .31f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(.15f, .224f, .27f, 1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.22f, .34f, .39f, 1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(.37f, .27f, .18f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(.24f, .23f, .20f, 1);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(.21f, .30f, .36f, 1);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(.36f, .29f, .20f, 1);
    style.Colors[ImGuiCol_Tab] = ImVec4(.14f, .21f, .25f, 1);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(.22f, .34f, .39f, 1);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(.20f, .31f, .34f, 1);
    style.Colors[ImGuiCol_TabSelectedOverline] = ImVec4(.34f, .69f, .61f, 1);
    style.Colors[ImGuiCol_CheckMark] = accentText;
    style.Colors[ImGuiCol_Separator] = ImVec4(.20f, .27f, .32f, 1);
}

void editorInput()
{
    OSD_CaptureInput(0);
    mouseGrabInput(false);
    KB_ClearKeysDown();
    auto &io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    g_ImGuiExclusiveInput = true;
    engineBeginImGuiInput();
}

void gameInput()
{
    g_ImGuiExclusiveInput = false;
    if (auto const context = ImGui::GetCurrentContext())
    {
        engineEndImGuiInput();
        if (styledContext == context)
        {
            ImGui::GetStyle() = gameStyle;
            ImGui::GetIO().FontGlobalScale = gameFontScale;
            ImGui::GetIO().FontDefault = gameFont;
        }
    }
    styledContext = nullptr;
    KB_ClearKeysDown();
}

bool beginEditor()
{
    int const oldRenderer = videoGetRenderMode();
    gameVideo = {xres, yres, bpp, fullscreen, upscalefactor, oldRenderer};
    if ((bpp <= 8 || !ImGui::GetCurrentContext()) &&
        videoSetGameMode(0, std::max(xres, 1024), std::max(yres, 680), 32, 1) < 0)
    {
        videoSetGameMode(gameVideo.full, gameVideo.w, gameVideo.h, gameVideo.depth, gameVideo.scale);
        return false;
    }
    if (bpp <= 8 || !ImGui::GetCurrentContext())
        return false;
    videoSetRenderMode(REND_POLYMOST);
    mode = Mode::Edit;
    artLimit = DukeEditor::Target(DukeEditor::RunningTarget()).tiles;
    if (!DukeEditor::LaunchProjectFolder().empty() && projectWorkspace.root.empty())
    {
        std::string error;
        if (!loadProject(DukeEditor::LaunchProjectFolder(), error))
            status(error, true);
    }
    if (!participantName[0])
        Bstrncpyz(participantName, g_player[0].user_name[0] ? g_player[0].user_name : "Editor", sizeof(participantName));
    tileNames.assign(artLimit, nullptr);
    for (int i = 0; i < g_dynTileListSize; ++i)
    {
        auto const &tile = g_dynTileList[i];
        if (unsigned(tile.staticval) < unsigned(artLimit))
            tileNames[tile.staticval] = tile.str;
    }
    thumbnails.assign(artLimit, {});
    videoSetPalette(ud.brightness >> 2, BASEPAL, 0);
    renderFlushPerms();
    ready2send = 0;
    S_StopMusic();
    FX_StopAllSounds();
    gameFileDrop = g_fileDropCallback;
    g_fileDropCallback = mapDropped;
    styleEditor();
    editorInput();
    rebuildTileFilter();
    view3D = false;
    newMap();
    if (!initialMap.empty())
        openMap(initialMap);
    initialMap.clear();
    LOG_F(INFO, "Map Editor: opened workspace");
    return true;
}

void cleanPlaytest()
{
    if (!playtestPath.empty())
    {
        remove(playtestPath.c_str());
        playtestPath.clear();
    }
}

bool openBuildTools(int mcpPort, std::string &error)
{
    if (buildToolsProcess >= 0)
    {
        error = "The native Build tools are already open.";
        return false;
    }
    buildToolsFolder = DukeEditor::CreatePlaytestPath();
    if (buildToolsFolder.empty() || remove(buildToolsFolder.c_str()) ||
        buildvfs_mkdir(buildToolsFolder.c_str(), S_IRWXU))
    {
        error = "Could not create a working folder for the native Build tools.";
        buildToolsFolder.clear();
        return false;
    }
    buildToolsMap = buildToolsFolder + "/draft.map";
    if (!DukeEditor::WriteBoard(buildToolsMap, board, artLimit, error))
    {
        if (error.empty()) error = "Could not prepare a map for the native Build tools.";
        DukeEditor::RemoveTemporaryProject(buildToolsFolder);
        buildToolsMap.clear();
        buildToolsFolder.clear();
        return false;
    }
    buildToolsProcess = NativeEditor_Launch(buildToolsFolder, buildToolsMap, mcpPort, error);
    if (buildToolsProcess < 0)
    {
        DukeEditor::RemoveTemporaryProject(buildToolsFolder);
        buildToolsMap.clear();
        buildToolsFolder.clear();
        return false;
    }
    buildToolsOriginal = board;
    status("Save the map and exit the native Build tools to apply your edits.");
    return true;
}

void pollBuildTools()
{
    if (!NativeEditor_Finished(buildToolsProcess))
        return;
    auto const path = std::exchange(buildToolsMap, {});
    auto const folder = std::exchange(buildToolsFolder, {});
    auto original = std::move(buildToolsOriginal);
    Board edited;
    std::string error;
    if (DukeEditor::ReadBoard(path, edited, artLimit, error))
    {
        if (DukeEditor::EncodeBoard(edited) == DukeEditor::EncodeBoard(original))
        {
            DukeEditor::RemoveTemporaryProject(folder);
            return;
        }
        Board merged;
        bool const unchanged = DukeEditor::EncodeBoard(board) == DukeEditor::EncodeBoard(original);
        bool const canMerge = unchanged || DukeEditor::MergeBoard(original, edited, board, merged);
        if (mode == Mode::Edit && editable() && !drag && canMerge &&
            change([&] { board = unchanged ? std::move(edited) : std::move(merged); return true; }, "Apply native Build edits"))
        {
            selection = {};
            controlPointer = {};
            fitPlan = true;
            fitSelection = false;
            DukeEditor::RemoveTemporaryProject(folder);
            status("Applied edits from the native Build tools");
            return;
        }
    }
    status("The native draft is available at " + path, true);
}

void returnToEditor();

void enterPlaytest(PlayPoint const &point = {})
{
    std::string error;
    Board test = board;
    if ((point.sector >= 0 || point.spawn >= 0) && !DukeEditor::MakePlayStart(test, point, DukeEditor::RunningTarget(), error))
    {
        status(error, true);
        return;
    }
    auto path = DukeEditor::CreatePlaytestPath();
    if (path.empty() || path.size() >= sizeof(boardfilename) || !DukeEditor::WriteBoard(path, test, artLimit, error))
    {
        if (!path.empty())
            remove(path.c_str());
        status(error.empty() ? "Could not create a temporary map for playtesting." : error, true);
        return;
    }
    playtestPath = std::move(path);
    previousSkill = ud.m_player_skill;
    ud.m_volume_number = 0;
    ud.m_level_number = 7;
    ud.m_player_skill = 2;
    Bstrncpyz(boardfilename, playtestPath.c_str(), sizeof(boardfilename));
    mode = Mode::Play;
    waitForF5Release = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F5];
    renderFlushPerms();
    gameInput();
    mouseGrabInput(true);
    G_NewGame(NEWGAME_NOSEND | NEWGAME_RESETALL);
    if (!(ud.gm & MODE_GAME))
    {
        returnToEditor();
        status("Duke3D could not start this playtest. Check the start and sector geometry.", true);
        return;
    }
    startPlayMessage = timerGetTicks();
    auto const scriptRoot = projectWorkspace.root.empty() && !sourcePath.empty()
        ? directoryOf(sourcePath) : projectWorkspace.root;
    if (!scriptRoot.empty())
        DukeEditor::ActivateProjectScripts(scriptRoot, sourcePath.empty() ? title : sourcePath, projectWorkspace.map);
    LOG_F(INFO, "Map Editor: playtesting %s", title.c_str());
}

void returnToEditor()
{
    VBJS_ClearAttachments();
    cleanPlaytest();
    ud.m_player_skill = previousSkill;
    S_StopMusic();
    FX_StopAllSounds();
    G_BackToMenu();
    ready2send = 0;
    artClearMapArt();
    videoSetPalette(ud.brightness >> 2, BASEPAL, 0);
    renderFlushPerms();
    styleEditor();
    editorInput();
    waitForF5Release = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F5];
    viewDirty = true;
    mode = Mode::Edit;
    G_UpdateAppTitle("Map Editor");
    status("Playtest stopped");
    LOG_F(INFO, "Map Editor: returned to the draft");
}

void leaveEditor()
{
    live.leave();
    releaseThumbnails(styledContext == ImGui::GetCurrentContext());
    gameInput();
    g_fileDropCallback = gameFileDrop;
    mode = Mode::Menu;
    board = {};
    undo.clear();
    redo.clear();
    outline.clear();
    drag.reset();
    videoSetGameMode(gameVideo.full, gameVideo.w, gameVideo.h, gameVideo.depth, gameVideo.scale);
    videoSetRenderMode(gameVideo.renderer);
    G_BackToMenu();
    G_UpdateScreenArea();
    mouseGrabInput(true);
    LOG_F(INFO, "Map Editor: returned to the title menu");
}

void browse(std::string directory)
{
    char canonical[BMAX_PATH];
    if (directory.size() + 2 >= sizeof(canonical) || !buildvfs_isdir(directory.c_str()))
        return;
    if (directory.back() != '/' && directory.back() != '\\')
        directory.push_back('/');
    Bstrncpyz(canonical, directory.c_str(), sizeof(canonical));
    Bcanonicalisefilename(canonical, 0);
    auto const handle = Bopendir(canonical);
    if (!handle)
        return;
    browserDirectory = canonical;
    if (!browserDirectory.empty() && browserDirectory.back() != '/' && browserDirectory.back() != '\\')
        browserDirectory.push_back('/');
    browserEntries.clear();
    while (auto const item = Breaddir(handle))
    {
        if (item->name[0] == '.')
            continue;
        bool const isDirectory = (item->mode & S_IFMT) == S_IFDIR;
        if (isDirectory || isMap(item->name))
            browserEntries.push_back({item->name, isDirectory});
    }
    Bclosedir(handle);
    std::sort(browserEntries.begin(), browserEntries.end(), [](auto const &a, auto const &b) {
        return a.directory != b.directory ? a.directory : Bstrcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

void openFileDialog(Action action)
{
    fileAction = action;
    showFile = true;
    importTarget = GameTarget::Inherit;
    browse(sourcePath.empty() ? (projectWorkspace.root.empty() ? Net_LaunchDirectory() : projectWorkspace.root) :
                               directoryOf(sourcePath));
    Bstrncpyz(filePath, action == Action::SaveAs ? title.c_str() : "", sizeof(filePath));
}

void navigate(Action action)
{
    switch (action)
    {
        case Action::New: newMap(); break;
        case Action::Open:
            if (droppedMap.empty())
                openFileDialog(Action::Open);
            else
                openMap(droppedMap);
            droppedMap.clear();
            break;
        case Action::Join:
            if (live.join(hostAddress, participantName, editionIdentity()))
                status("Connecting to " + std::string(hostAddress));
            else
                status(live.takeNotice(), true);
            break;
        case Action::Title: leaveEditor(); break;
        case Action::Quit: G_GameExit(" "); break;
        case Action::Play: enterPlaytest(std::exchange(queuedPlayPoint, PlayPoint{})); break;
        case Action::BuildTools:
        {
            std::string error;
            if (!openBuildTools(0, error))
                status(std::move(error), true);
            break;
        }
        case Action::Save:
            if (sourcePath.empty()) openFileDialog(Action::SaveAs);
            else saveMap(sourcePath);
            break;
        case Action::SaveAs: openFileDialog(Action::SaveAs); break;
        case Action::Idle: break;
    }
}

void applyAction(Action action)
{
    if (!editable() && (action == Action::Save || action == Action::SaveAs || action == Action::Play || action == Action::BuildTools))
    {
        status("Wait for the host to confirm the current draft first.", true);
        return;
    }
    if ((live.mode() == LiveMode::Joined || live.mode() == LiveMode::Connecting) &&
        (action == Action::New || action == Action::Open))
    {
        status("Leave the live session to open a different map.", true);
        return;
    }
    bool const discard = std::exchange(discardConfirmed, false);
    if (!discard && isDirty() &&
        (action == Action::New || action == Action::Open || action == Action::Join || action == Action::Title || action == Action::Quit))
    {
        pendingNavigation = action;
        showDiscard = true;
    }
    else
        navigate(action);
}

void savedFromDialog()
{
    ImGui::CloseCurrentPopup();
    if (pendingNavigation != Action::Idle)
        queued = std::exchange(pendingNavigation, Action::Idle);
}

void nextPanel(char const *name, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extra = 0)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoCollapse | extra);
}

bool choice(char const *label, bool active)
{
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.32f, .25f, .17f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, accentText);
    }
    bool const pressed = ImGui::Button(label);
    if (active)
        ImGui::PopStyleColor(2);
    return pressed;
}

ImVec4 playerColor(int slot)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(float((slot * 13 + 4) & 31) / 32.f, .53f + .12f * (slot & 1),
                               .91f + .09f * ((slot >> 1) & 1), r, g, b);
    return {r, g, b, 1};
}

int participantCount()
{
    return std::count_if(live.players().begin(), live.players().end(), [](auto const &p) { return !p.name.empty(); });
}

void sessionMenu()
{
    auto label = online() ? "Live " + std::to_string(participantCount()) : "Session";
    label += "###liveSession";
    if (choice(label.c_str(), online()))
        ImGui::OpenPopup("Live editing");
    if (!ImGui::BeginPopup("Live editing"))
        return;
    ImGui::TextUnformatted("Live editing");
    ImGui::Separator();
    if (live.mode() == LiveMode::Connecting)
    {
        ImGui::TextDisabled("Connecting to %s", hostAddress);
        if (ImGui::Button("Cancel connection"))
        {
            live.leave();
            status("Connection cancelled");
            ImGui::CloseCurrentPopup();
        }
    }
    else if (online())
    {
        ImGui::Text("%d / %d participants", participantCount(), DukeEditor::maxEditors);
        if (live.mode() == LiveMode::Hosting)
            ImGui::TextDisabled("Hosting on UDP port %d", listenPort);
        else
            ImGui::TextDisabled("%s", hostAddress);
        if (ImGui::Button(live.mode() == LiveMode::Hosting ? "Stop hosting" : "Leave session"))
        {
            live.leave();
            status("Live session closed");
            ImGui::CloseCurrentPopup();
        }
    }
    else
    {
        ImGui::SetNextItemWidth(205);
        ImGui::InputText("Your name", participantName, sizeof(participantName));
        ImGui::SetNextItemWidth(205);
        ImGui::InputInt("Host port", &listenPort, 0, 0);
        listenPort = clamp(listenPort, 1, 65535);
        if (ImGui::Button("Host this map"))
        {
            if (live.host(listenPort, participantName, editionIdentity(), artLimit, board))
            {
                livePrefabBindings.clear();
                livePresentation = live.presentation();
                liveRevision = live.revision();
                status("Live session listening on UDP port " + std::to_string(listenPort));
                ImGui::CloseCurrentPopup();
            }
            else
                status(live.takeNotice(), true);
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(205);
        ImGui::InputText("Host address", hostAddress, sizeof(hostAddress));
        if (ImGui::Button("Join session"))
        {
            queued = Action::Join;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void drawToolbar(float workspaceWidth)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(accentText, "Map Editor");
    ImGui::SameLine(0, 16);
    if (ImGui::Button("File"))
        ImGui::OpenPopup("Map files");
    if (ImGui::BeginPopup("Map files"))
    {
        if (ImGui::MenuItem("New map", "Ctrl+N")) queued = Action::New;
        if (ImGui::MenuItem("Open map...", "Ctrl+O")) queued = Action::Open;
        if (ImGui::MenuItem("Save", "Ctrl+S")) queued = Action::Save;
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) queued = Action::SaveAs;
        if (NativeEditor_Available() && ImGui::MenuItem("Open Build tools", nullptr, false, buildToolsProcess < 0))
            queued = Action::BuildTools;
        if (ImGui::MenuItem("Project...")) showProjectDialog = true;
        if (ImGui::MenuItem("Map settings...")) showMapSettingsDialog = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Return to title")) queued = Action::Title;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Build"))
        ImGui::OpenPopup("Build tools menu");
    if (ImGui::BeginPopup("Build tools menu"))
    {
        if (ImGui::MenuItem("Draw pillar", nullptr, false, editable() && selectedSector() >= 0))
        {
            pillarSector = selectedSector();
            switchTool(Tool::Pillar);
            status("Click the pillar corners inside the selected sector. Enter closes the outline.");
        }
        if (ImGui::MenuItem("Stairs...", nullptr, false, editable()))
            showStairs = true;
        if (ImGui::MenuItem("Catwalk...", nullptr, false, editable()))
            showCatwalk = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Linked mechanisms", editable() && DukeEditor::DukeMechanisms(projectWorkspace.effectiveTarget)))
        {
            using DukeEditor::Mechanism;
            for (auto type : {Mechanism::Lift, Mechanism::Door, Mechanism::Trap, Mechanism::Light})
                if (ImGui::MenuItem(DukeEditor::MechanismName(type)))
                    openPreset(type);
            ImGui::EndMenu();
        }
        if (!DukeEditor::DukeMechanisms(projectWorkspace.effectiveTarget))
            ImGui::SetItemTooltip("Stock mechanism presets use the Duke3D 1.3D and Atomic game targets.");
        ImGui::MenuItem("Show links", nullptr, &showLinks);
        ImGui::Separator();
        if (ImGui::MenuItem("Check map")) showChecks = true;
        if (ImGui::MenuItem("Change history")) showChanges = true;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    sessionMenu();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canUndo() || !editable());
    if (ImGui::Button("Undo")) useHistory(undo, redo);
    ImGui::SetItemTooltip("Ctrl+Z");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo() || !editable());
    if (ImGui::Button("Redo")) useHistory(redo, undo);
    ImGui::SetItemTooltip("Ctrl+Shift+Z");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("%s%s", title.c_str(), isDirty() ? " *" : "");

    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), workspaceWidth - 248.f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.25f, .48f, .39f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.29f, .61f, .48f, 1));
    if (ImGui::Button("Playtest   F5", ImVec2(138, 0)))
        queued = Action::Play;
    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip("Test the current draft in Duke3D. F5 returns to editing.");
    ImGui::SameLine();
    if (ImGui::Button("Spawn..."))
        ImGui::OpenPopup("Playtest spawn");
    if (ImGui::BeginPopup("Playtest spawn"))
    {
        if (ImGui::MenuItem("Map start")) queued = Action::Play;
        for (int i = 0; i < int(board.sprites.size()); ++i)
        {
            auto const label = DukeEditor::SpawnLabel(board, i);
            if (!label.empty() && ImGui::MenuItem(label.c_str()))
            {
                PlayPoint point;
                point.spawn = i;
                queuedPlayPoint = point;
                queued = Action::Play;
            }
        }
        ImGui::EndPopup();
    }
}

void drawDialogs()
{
    if (showFile)
    {
        ImGui::OpenPopup("Map file");
        showFile = false;
    }
    if (showDiscard)
    {
        ImGui::OpenPopup("Unsaved map");
        showDiscard = false;
    }
    if (showOverwrite)
    {
        ImGui::OpenPopup("Replace map?");
        showOverwrite = false;
    }

    ImGui::SetNextWindowSize(ImVec2(530, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Map file", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        bool const saving = fileAction == Action::SaveAs;
        ImGui::TextUnformatted(saving ? "Save Duke3D map" : "Open Duke3D map");
        if (ImGui::Button("Up")) browse(browserDirectory + "..");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", browserDirectory.c_str());

        bool submitted = false;
        if (ImGui::BeginChild("Files", ImVec2(0, 170), ImGuiChildFlags_Borders))
        {
            for (auto const &entry : browserEntries)
            {
                auto const name = entry.directory ? entry.name + "/" : entry.name;
                if (!ImGui::Selectable(name.c_str(), !entry.directory && entry.name == filePath,
                                       ImGuiSelectableFlags_AllowDoubleClick))
                    continue;
                if (entry.directory)
                {
                    browse(browserDirectory + entry.name);
                    break;
                }
                Bstrncpyz(filePath, entry.name.c_str(), sizeof(filePath));
                if (ImGui::IsMouseDoubleClicked(0))
                    submitted = true;
            }
        }
        ImGui::EndChild();
        if (!saving && ImGui::CollapsingHeader("Maps in game data"))
        {
            ImGui::BeginChild("Game maps", ImVec2(0, 115), ImGuiChildFlags_Borders);
            for (int i = 0; i < g_numVolumes * MAXLEVELS; ++i)
            {
                auto const &level = g_mapInfo[i];
                if (!level.filename || !level.name)
                    continue;
                ImGui::PushID(i);
                auto const label = std::string(level.name) + "  (" + level.filename + ")";
                if (ImGui::Selectable(label.c_str()))
                    Bstrncpyz(filePath, level.filename, sizeof(filePath));
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        if (!saving)
            targetPicker("Import as", importTarget, true);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        submitted |= ImGui::InputTextWithHint("##mapPath", "File name or full path", filePath, sizeof(filePath),
                                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        submitted |= ImGui::Button(saving ? "Save map" : "Open map", ImVec2(105, 0));
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
            pendingNavigation = Action::Idle;
            droppedMap.clear();
        }
        else if (submitted && *filePath)
        {
            std::string path = filePath;
            bool const absolute = path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':');
            auto const local = absolute ? path : browserDirectory + path;
            if (saving)
            {
                auto target = local;
                if (!isMap(target)) target += ".map";
                if (buildvfs_exists(target.c_str()) && target != sourcePath)
                {
                    overwritePath = std::move(target);
                    showOverwrite = true;
                    ImGui::CloseCurrentPopup();
                }
                else if (saveMap(target))
                    savedFromDialog();
            }
            else if (openMap(buildvfs_exists(local.c_str()) ? local : path, importTarget))
                ImGui::CloseCurrentPopup();
        }
        if (errorMessage)
            ImGui::TextWrapped("%s", notice.c_str());
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Unsaved map", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s has unsaved changes.", title.c_str());
        if (ImGui::Button("Save and continue"))
        {
            if (sourcePath.empty())
            {
                ImGui::CloseCurrentPopup();
                openFileDialog(Action::SaveAs);
            }
            else if (saveMap(sourcePath))
                savedFromDialog();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes"))
        {
            ImGui::CloseCurrentPopup();
            discardConfirmed = true;
            queued = std::exchange(pendingNavigation, Action::Idle);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
            pendingNavigation = Action::Idle;
            droppedMap.clear();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Replace map?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Replace %s?", basenameOf(overwritePath).c_str());
        if (ImGui::Button("Replace") && saveMap(overwritePath))
            savedFromDialog();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
            showFile = true;
        }
        ImGui::EndPopup();
    }
}

ImVec2 project(vec2_t point)
{
    return PlanProjection{planMin, planMax, planOrigin, planZoom}.point(point);
}

char const *objectName(Kind kind)
{
    switch (kind)
    {
        case Kind::Sector: return "Sector";
        case Kind::Wall: return "Wall";
        case Kind::Vertex: return "Vertex";
        case Kind::Sprite: return "Sprite";
        case Kind::Start: return "Player Start";
        default: return "";
    }
}

std::string playerActivity(Presence const &presence)
{
    switch (presence.activity)
    {
        case Activity::DrawRoom: return "Drawing a room";
        case Activity::PlaceSprite: return "Placing sprites";
        case Activity::PlaceStart: return "Placing the start";
        case Activity::Playtest: return "Playtesting";
        default: break;
    }
    if (presence.kind == Kind::Nothing)
        return presence.perspective ? "3D view" : "Plan view";
    std::string text = presence.activity == Activity::Drag ? "Moving " : "";
    text += objectName(presence.kind);
    if (presence.kind != Kind::Start)
        text += " " + std::to_string(presence.index);
    return text;
}

void drawPeerSelection(ImDrawList *draw, Presence const &peer, ImU32 color, PlanProjection const &plan, bool compact)
{
    unsigned const index = peer.index;
    float const line = compact ? 1.7f : 2.5f;
    auto wallLine = [&](int j) {
        ImVec2 const a = plan.point(board.walls[j].xy), b = plan.point(board.walls[board.walls[j].point2].xy);
        if (std::max(a.x, b.x) >= plan.min.x && std::min(a.x, b.x) <= plan.max.x &&
            std::max(a.y, b.y) >= plan.min.y && std::min(a.y, b.y) <= plan.max.y)
            draw->AddLine(a, b, color, line);
    };
    auto ring = [&](vec2_t position, float radius) {
        auto const point = plan.point(position);
        radius *= compact ? .55f : 1.f;
        if (plan.contains(point, radius))
            draw->AddCircle(point, radius, color, 14, compact ? 1.5f : 2.f);
    };
    if (peer.kind == Kind::Sector && index < board.sectors.size())
    {
        auto const &area = board.sectors[index];
        for (int j = area.wallptr; j < area.wallptr + area.wallnum; ++j)
            wallLine(j);
    }
    else if (peer.kind == Kind::Wall && index < board.walls.size())
        wallLine(index);
    else if (peer.kind == Kind::Vertex && index < board.walls.size())
        ring(board.walls[index].xy, 7.f);
    else if (peer.kind == Kind::Sprite && index < board.sprites.size())
        ring(board.sprites[index].xy, 10.f);
    else if (peer.kind == Kind::Start && board.startSector >= 0)
        ring({board.start.x, board.start.y}, 16.f);
}

void drawPeerCursor(ImDrawList *draw, DukeEditor::Collaborator const &peer, ImU32 color, ImVec2 p,
                    ImVec2 min, ImVec2 max, bool cameraMarker, bool compact)
{
    if (p.x < min.x || p.y < min.y || p.x >= max.x || p.y >= max.y)
        return;
    float const scale = compact ? .7f : 1.f;
    if (cameraMarker)
        draw->AddCircle(p, 7.f * scale, color, 14, 2.f);
    else
        draw->AddTriangleFilled(p, {p.x + 2 * scale, p.y + 15 * scale}, {p.x + 11 * scale, p.y + 9 * scale}, color);
    auto const label = peer.name + (peer.ai ? " (AI)" : "");
    auto const size = ImGui::CalcTextSize(label.c_str());
    float const x = std::max(min.x + 2.f, p.x + size.x + 22.f <= max.x ? p.x + 12.f : p.x - size.x - 14.f);
    float const y = std::max(min.y + 2.f, std::min(p.y + 10.f, max.y - size.y - 7.f));
    draw->AddRectFilled({x, y}, {x + 10.f + size.x, y + 6.f + size.y}, ink, 3);
    draw->AddText({x + 5.f, y + 3.f}, color, label.c_str());
}

void drawPlanParticipants(ImDrawList *draw, PlanProjection const &plan, bool compact)
{
    draw->PushClipRect(plan.min, plan.max, true);
    for (int i = 0; i < DukeEditor::maxEditors; ++i)
    {
        auto const &other = live.players()[i];
        if (i == live.localSlot() || other.name.empty())
            continue;
        auto const color = ImGui::ColorConvertFloat4ToU32(playerColor(i));
        auto const &presence = other.presence;
        if (presence.revision <= liveRevision)
            drawPeerSelection(draw, presence, color, plan, compact);
        if (presence.cursor)
            drawPeerCursor(draw, other, color, plan.point(presence.position), plan.min, plan.max, presence.perspective, compact);
    }
    if (controlPointer.cursor)
    {
        static DukeEditor::Collaborator const assistant{"Assistant", true, 0, {}};
        drawPeerSelection(draw, controlPointer, accent, plan, compact);
        drawPeerCursor(draw, assistant, accent, plan.point(controlPointer.position), plan.min, plan.max, false, compact);
    }
    draw->PopClipRect();
}

void drawPerspectivePointers(ImDrawList *draw)
{
    draw->PushClipRect(sceneMin, sceneMax, true);
    for (int i = 0; i < DukeEditor::maxEditors; ++i)
    {
        auto const &other = live.players()[i];
        auto const &p = other.presence;
        if (i == live.localSlot() || other.name.empty() || !p.cursor || !p.perspective)
            continue;
        ImVec2 const pointer{sceneMin.x + (sceneMax.x - sceneMin.x) * p.viewX / 65535.f,
                             sceneMin.y + (sceneMax.y - sceneMin.y) * p.viewY / 65535.f};
        auto const color = ImGui::ColorConvertFloat4ToU32(playerColor(i));
        drawPeerCursor(draw, other, color, pointer, sceneMin, sceneMax, false, false);
    }
    draw->PopClipRect();
}

void sendLivePointer();

vec2_t mapPoint(ImVec2 point, bool snap)
{
    float x = planOrigin.x + (point.x - (planMin.x + planMax.x) * .5f) / planZoom;
    float y = planOrigin.y + (point.y - (planMin.y + planMax.y) * .5f) / planZoom;
    if (snap && grid)
    {
        x = std::round(x / grid) * grid;
        y = std::round(y / grid) * grid;
    }
    return {int(std::lround(clamp(x, float(-BXY_MAX), float(BXY_MAX)))),
            int(std::lround(clamp(y, float(-BXY_MAX), float(BXY_MAX))))};
}

void sendLivePointer()
{
    if (!online())
        return;
    Presence p;
    if (mode == Mode::Play)
    {
        p.activity = Activity::Playtest;
        live.updatePresence(p);
        return;
    }
    p.kind = selection.kind;
    p.index = selection.index;
    p.revision = liveRevision;
    auto activity = tool == Tool::Pillar ? Activity::DrawRoom :
                    tool == Tool::Catwalk || tool == Tool::Prefab ? Activity::PlaceSprite :
                    tool == Tool::Cut ? Activity::Select : static_cast<Activity>(tool);
    p.activity = controlPointer.cursor ? controlPointer.activity : drag ? Activity::Drag : activity;
    p.perspective = view3D;
    p.cursor = controlPointer.cursor || (sceneHovered && !sectionView);
    if (controlPointer.cursor)
    {
        p.perspective = false;
        p.position = controlPointer.position;
    }
    else if (p.cursor)
    {
        auto const mouse = ImGui::GetIO().MousePos;
        p.position = view3D ? vec2_t{camera.x, camera.y} : mapPoint(mouse, false);
        p.viewX = clamp(int((mouse.x - sceneMin.x) * 65535.f / (sceneMax.x - sceneMin.x)), 0, 65535);
        p.viewY = clamp(int((mouse.y - sceneMin.y) * 65535.f / (sceneMax.y - sceneMin.y)), 0, 65535);
    }
    live.updatePresence(p);
}

void adoptLiveBoard(Board const &incoming, DukeEditor::ChangeRecord const *change = nullptr)
{
    auto &scripts = projectWorkspace.map.scripts;
    auto const found = change ? livePrefabBindings.find(change->restoredEdit) : livePrefabBindings.end();
    if (found != livePrefabBindings.end() && !change->replayed)
        for (auto const &binding : found->second)
        {
            auto const old = std::remove(scripts.begin(), scripts.end(), binding);
            mapSettingsDirty |= old != scripts.end();
            scripts.erase(old, scripts.end());
        }
    auto previous = std::move(board);
    board = incoming;
    reconcileAttachments(previous);
    if (found != livePrefabBindings.end() && change->replayed)
        for (auto const &binding : found->second)
            if (scripts.size() < DukeEditor::maxScriptAttachments &&
                std::find(scripts.begin(), scripts.end(), binding) == scripts.end())
            {
                scripts.push_back(binding);
                mapSettingsDirty = true;
            }
}

void pollLive()
{
    auto const previous = live.mode();
    live.tick();
    auto notice = live.takeNotice();
    if (previous == LiveMode::Connecting && live.mode() == LiveMode::Joined)
    {
        setDocument(live.board(), "Live draft.map", {});
        liveRevision = live.revision();
        livePresentation = live.presentation();
        status("Joined the live editor");
    }
    else if (online() && !drag && !live.pending() && live.presentation() != livePresentation)
    {
        bool const changed = DukeEditor::EncodeBoard(board) != DukeEditor::EncodeBoard(live.board());
        bool const same = DukeEditor::SameTopology(board, live.board());
        if (live.mode() == LiveMode::Hosting && !livePrefabBindings.empty() && live.boardAtRevision(liveRevision))
        {
            for (auto const &event : live.changes())
                if (event.revision > liveRevision)
                    if (auto const next = live.boardAtRevision(event.revision))
                        adoptLiveBoard(*next, &event);
        }
        else if (changed)
            adoptLiveBoard(live.board());
        livePresentation = live.presentation();
        liveRevision = live.revision();
        if (changed)
        {
            if (!same)
            {
                selection = {};
                group.clear();
                showPreset = false;
                controlPointer = {};
                if (cameraSector < 0 || board.sectorAt({camera.x, camera.y}, cameraSector) < 0)
                    cameraToStart();
            }
            undo.clear();
            redo.clear();
            revision = ++nextRevision;
            viewDirty = true;
            if (unsigned(live.author()) < DukeEditor::maxEditors)
            {
                auto const &changes = live.changes();
                if (!changes.empty())
                    status(changes.back().author + " · " + changes.back().label);
            }
        }
    }
    if (previous != LiveMode::Offline && live.mode() == LiveMode::Offline)
    {
        undo.clear();
        redo.clear();
        livePrefabBindings.clear();
    }
    if (!notice.empty())
        status(std::move(notice), true);
}

int selectedSector()
{
    return DukeEditor::ObjectSector(board, selection);
}

PlanProjection fitWalls(ImVec2 min, ImVec2 max, int begin, int end)
{
    vec2_t low{BXY_MAX, BXY_MAX}, high{-BXY_MAX, -BXY_MAX};
    if (begin == end)
        return {min, max, {}, .08f};
    for (int i = begin; i < end; ++i)
    {
        low.x = std::min(low.x, board.walls[i].x);
        low.y = std::min(low.y, board.walls[i].y);
        high.x = std::max(high.x, board.walls[i].x);
        high.y = std::max(high.y, board.walls[i].y);
    }
    float const zoom = std::min((max.x - min.x) / float(std::max(high.x - low.x, 128)),
                                (max.y - min.y) / float(std::max(high.y - low.y, 128))) * .81f;
    return {min, max, {(low.x + high.x) * .5f, (low.y + high.y) * .5f}, zoom};
}

void framePlan()
{
    int begin = 0, end = board.walls.size();
    int const area = fitSelection ? selectedSector() : -1;
    if (area >= 0)
    {
        begin = board.sectors[area].wallptr;
        end = begin + board.sectors[area].wallnum;
    }
    auto const plan = fitWalls(planMin, planMax, begin, end);
    planOrigin = plan.origin;
    planZoom = clamp(plan.zoom, .001f, 2.f);
    fitPlan = false;
}

void frameScene()
{
    if (sectionView)
    {
        auto const p = DukeEditor::ObjectPosition(board, selection);
        cutCoordinate = cutAlongX ? p.y : p.x;
        return;
    }
    if (!view3D)
    {
        fitPlan = fitSelection = true;
        return;
    }
    int const area = selectedSector();
    if (area < 0)
    {
        cameraToStart();
        return;
    }
    auto const &sub = board.sectors[area];
    int64_t x = 0, y = 0;
    for (int i = sub.wallptr; i < sub.wallptr + sub.wallnum; ++i)
    {
        x += board.walls[i].x;
        y += board.walls[i].y;
    }
    vec2_t const center{int(x / sub.wallnum), int(y / sub.wallnum)};
    if (board.sectorAt(center, area) != area)
    {
        cameraToStart();
        return;
    }
    cameraSector = area;
    camera = {center.x, center.y, (board.ceilingAt(area, center) / 2) + (board.floorAt(area, center) / 2)};
    cameraHorizon = 100;
}

float distanceSquared(ImVec2 a, ImVec2 b)
{
    float const x = a.x - b.x, y = a.y - b.y;
    return x*x + y*y;
}

float distanceToWall(ImVec2 cursor, ImVec2 a, ImVec2 b)
{
    auto const d = distanceSquared(a, b);
    if (d == 0)
        return distanceSquared(cursor, a);
    float const along = clamp(((cursor.x - a.x) * (b.x - a.x) + (cursor.y - a.y) * (b.y - a.y)) / d, 0.f, 1.f);
    return distanceSquared(cursor, {a.x + along * (b.x - a.x), a.y + along * (b.y - a.y)});
}

bool onFloorLayer(Selection object)
{
    if (!filterFloor)
        return true;
    int const area = DukeEditor::ObjectSector(board, object);
    if (area < 0)
        return false;
    auto const p = DukeEditor::ObjectPosition(board, object);
    if (board.ceilingAt(area, p) >= floorLayer || board.floorAt(area, p) < floorLayer)
        return false;
    if (object.kind == Kind::Sprite || object.kind == Kind::Start)
    {
        auto const z = object.kind == Kind::Sprite ? board.sprites[object.index].z : board.start.z;
        int const support = DukeEditor::PlatformAt(board, area, p, true, z);
        int const layer = DukeEditor::PlatformAt(board, area, p, true, floorLayer);
        return support == layer && DukeEditor::LayerAt(board, p, area, true, floorLayer) == area;
    }
    return true;
}

Selection pickPlan(ImVec2 mouse)
{
    auto const &io = ImGui::GetIO();
    if (!io.KeyShift)
    {
        bool const wallsOnly = io.KeyCtrl || io.KeySuper;
        Selection picked;
        if (!wallsOnly)
        {
            float best = 81.f;
            if (board.startSector >= 0 && onFloorLayer({Kind::Start, 0}) &&
                distanceSquared(project({board.start.x, board.start.y}), mouse) < best)
            {
                picked = {Kind::Start, 0};
                best = distanceSquared(project({board.start.x, board.start.y}), mouse);
            }
            for (int i = 0; i < int(board.sprites.size()); ++i)
            {
                float const distance = distanceSquared(project(board.sprites[i].xy), mouse);
                bool const preferProp = picked.kind == Kind::Sprite && DukeEditor::IsPlatform(board.sprites[picked.index]) &&
                                        !DukeEditor::IsPlatform(board.sprites[i]);
                if ((distance < best || (distance == best && preferProp)) && onFloorLayer({Kind::Sprite, i}))
                {
                    best = distance;
                    picked = {Kind::Sprite, i};
                }
            }
            if (picked.kind != Kind::Nothing)
                return picked;

            best = 49.f;
            for (int i = 0; i < int(board.walls.size()); ++i)
            {
                float const distance = distanceSquared(project(board.walls[i].xy), mouse);
                if (distance < best && onFloorLayer({Kind::Vertex, i}))
                {
                    best = distance;
                    picked = {Kind::Vertex, i};
                }
            }
            if (picked.kind != Kind::Nothing)
                return picked;
        }

        float best = 36.f;
        for (int i = 0; i < int(board.walls.size()); ++i)
        {
            auto const &edge = board.walls[i];
            float const distance = distanceToWall(mouse, project(edge.xy), project(board.walls[edge.point2].xy));
            if (distance < best && onFloorLayer({Kind::Wall, i}))
            {
                best = distance;
                picked = {Kind::Wall, i};
            }
        }
        if (picked.kind != Kind::Nothing)
        {
            auto const &edge = board.walls[picked.index];
            if (edge.nextwall >= 0)
            {
                auto const a = project(edge.xy);
                auto const b = project(board.walls[edge.point2].xy);
                // Both portal walls project to the same line. Pick the side under the cursor.
                if ((b.x - a.x) * (mouse.y - a.y) < (b.y - a.y) * (mouse.x - a.x) &&
                    onFloorLayer({Kind::Wall, edge.nextwall}))
                    picked.index = edge.nextwall;
            }
            return picked;
        }
        if (wallsOnly)
            return {};
    }
    int const section = DukeEditor::LayerAt(board, mapPoint(mouse, false), selectedSector(), filterFloor, floorLayer);
    return section < 0 ? Selection{} : Selection{Kind::Sector, section};
}

void finishRoom()
{
    bool const pillar = tool == Tool::Pillar;
    if (!change([&] {
        if (!pillar)
            return board.addRoom(outline);
        if (unsigned(pillarSector) >= board.sectors.size())
            return false;
        auto const &room = board.sectors[pillarSector];
        return board.addPillar(pillarSector, outline, board.walls[room.wallptr].picnum);
    }, pillar ? "Draw pillar" : "Draw room"))
    {
        status(pillar ? "Draw the pillar entirely inside an open area of the sector, clear of sprites and the start." :
               "Close a simple outline with at least three corners. Shared edges must have matching endpoints.", true);
        return;
    }
    outline.clear();
    selection = {Kind::Sector, pillar ? pillarSector : int(board.sectors.size()) - 1};
    if (pillar) switchTool(Tool::Select);
    status(pillar ? "Pillar added" : "Sector added");
}

void planClicked(ImVec2 mouse)
{
    auto point = mapPoint(mouse, true);
    if (tool == Tool::Room || tool == Tool::Pillar)
    {
        if (outline.size() >= 3 && distanceSquared(mouse, project(outline.front())) < 100.f)
        {
            finishRoom();
            return;
        }
        if (tool == Tool::Room)
            for (auto const &edge : board.walls)
                if (distanceSquared(mouse, project(edge.xy)) < 49.f)
                {
                    point = edge.xy;
                    break;
                }
        if (outline.empty() || point.x != outline.back().x || point.y != outline.back().y)
            outline.push_back(point);
        return;
    }

    if (tool == Tool::Cut)
    {
        cutCoordinate = cutAlongX ? point.y : point.x;
        sectionView = true;
        switchTool(Tool::Select);
        return;
    }
    int const section = DukeEditor::LayerAt(board, point, selectedSector(), filterFloor, floorLayer);
    if (tool == Tool::Prefab)
    {
        std::string error;
        if (!placePrefab(prefabBrush, prefabBrushPath, point, section, filterFloor, floorLayer, error))
            status(error, true);
        return;
    }
    if (tool == Tool::Catwalk)
    {
        std::string error;
        if (!change([&] {
            return DukeEditor::AddCatwalk(board, section, point, catwalkTile, catwalkLength, catwalkWidth,
                                          catwalkHeight, catwalkAngle, error);
        }, "Add catwalk"))
            status(error, true);
        else
        {
            selection = {Kind::Sprite, int(board.sprites.size()) - 1};
            status("Catwalk added");
            switchTool(Tool::Select);
        }
        return;
    }
    if (tool == Tool::Sprite)
    {
        if (section >= 0 && change([&] {
            if (!board.addSprite(brush, point, section))
                return false;
            int const platform = filterFloor ? DukeEditor::PlatformAt(board, section, point, true, floorLayer) : -1;
            if (platform >= 0)
                board.sprites.back().z = board.sprites[platform].z;
            return true;
        }, "Place sprite"))
        {
            selection = {Kind::Sprite, int(board.sprites.size()) - 1};
            status("Sprite placed");
        }
        else
            status("Choose a point inside a sector with room for a sprite.", true);
        return;
    }

    if (tool == Tool::Start)
    {
        if (section >= 0 && change([&] { return board.setStart(point, section); }, "Move player start"))
        {
            selection = {Kind::Start, 0};
            cameraToStart();
            status("Player start moved");
        }
        else
            status("Choose a sector with at least 48 units of headroom.", true);
        return;
    }

    selectObject(pickPlan(mouse), true);
    planPress = mouse;
}

void startDrag()
{
    drag.reset(new Drag{{board, revision, projectWorkspace.map.scripts}, {}, mapPoint(planPress, false)});
    std::vector<int> vertices;
    if (selection.kind == Kind::Vertex || selection.kind == Kind::Wall)
    {
        vertices.push_back(selection.index);
        if (selection.kind == Kind::Wall)
            vertices.push_back(board.walls[selection.index].point2);
    }
    else if (selection.kind == Kind::Sector)
    {
        auto const &area = board.sectors[selection.index];
        for (int i = area.wallptr; i < area.wallptr + area.wallnum; ++i)
            vertices.push_back(i);
    }
    if (!vertices.empty())
        drag->vertices = board.linkedVertices(std::move(vertices));
}

void moveDrag(ImVec2 mouse)
{
    if (!drag)
        startDrag();
    auto const origin = mapPoint(mouse, false);
    vec2_t delta{origin.x - drag->cursor.x, origin.y - drag->cursor.y};
    if (grid)
    {
        delta.x = int(std::lround(float(delta.x) / grid)) * grid;
        delta.y = int(std::lround(float(delta.y) / grid)) * grid;
    }
    auto shifted = [delta](vec2_t p) { return vec2_t{ p.x + delta.x, p.y + delta.y }; };
    auto const &before = drag->before.board;
    for (int index : drag->vertices)
        board.walls[index].xy = shifted(before.walls[index].xy);
    if (selection.kind == Kind::Sprite)
    {
        auto const &initial = before.sprites[selection.index];
        board.sprites[selection.index] = initial;
        drag->changed = (delta.x || delta.y) && board.moveSprite(selection.index, shifted(initial.xy));
        return;
    }
    else if (selection.kind == Kind::Sector)
    {
        for (int i = 0; i < int(before.sprites.size()); ++i)
            if (before.sprites[i].sectnum == selection.index)
                board.sprites[i].xy = shifted(before.sprites[i].xy);
        if (before.startSector == selection.index)
        {
            auto const target = shifted({before.start.x, before.start.y});
            board.start.x = target.x;
            board.start.y = target.y;
        }
    }
    else if (selection.kind == Kind::Start)
    {
        board.start = before.start;
        board.startSector = before.startSector;
        drag->changed = (delta.x || delta.y) && board.setStart(shifted({before.start.x, before.start.y}), before.startSector);
        return;
    }
    drag->changed = delta.x != 0 || delta.y != 0;
}

void finishDrag()
{
    if (drag->changed)
    {
        std::string error;
        if (board.validate(artLimit, error, true) && board.validateGeometryChange(drag->before.board, error))
            commit(std::move(drag->before), "Move selection");
        else
        {
            board = std::move(drag->before.board);
            status(error, true);
        }
    }
    drag.reset();
}

void drawPlan(ImDrawList *draw, ImVec2 mouse)
{
    draw->AddRectFilled(planMin, planMax, plane, 4);
    draw->PushClipRect(planMin, planMax, true);
    int spacing = grid ? grid : 128;
    while (spacing * planZoom < 13.f)
        spacing *= 2;
    auto const topLeft = mapPoint(planMin, false), bottomRight = mapPoint(planMax, false);
    for (int x = (topLeft.x / spacing - 1) * spacing; x <= bottomRight.x; x += spacing)
    {
        float const sx = project({x, 0}).x;
        draw->AddLine({sx, planMin.y}, {sx, planMax.y}, x % (spacing * 4) ? gridMinor : gridMajor);
    }
    for (int y = (topLeft.y / spacing - 1) * spacing; y <= bottomRight.y; y += spacing)
    {
        float const sy = project({0, y}).y;
        draw->AddLine({planMin.x, sy}, {planMax.x, sy}, y % (spacing * 4) ? gridMinor : gridMajor);
    }

    int const active = selectedSector();
    if (active >= 0)
    {
        auto const &area = board.sectors[active];
        std::vector<ImVec2> polygon;
        for (int w = area.wallptr;;)
        {
            polygon.push_back(project(board.walls[w].xy));
            w = board.walls[w].point2;
            if (w == area.wallptr || polygon.size() == unsigned(area.wallnum))
                break;
        }
        if (polygon.size() >= 3 && polygon.size() == unsigned(area.wallnum))
            draw->AddConcavePolyFilled(polygon.data(), polygon.size(), IM_COL32(255, 177, 93, 24));
    }

    for (int i = 0; i < int(board.walls.size()); ++i)
    {
        auto const &w = board.walls[i];
        auto const a = project(w.xy), b = project(board.walls[w.point2].xy);
        if (std::max(a.x, b.x) < planMin.x - 5 || std::min(a.x, b.x) > planMax.x + 5 ||
            std::max(a.y, b.y) < planMin.y - 5 || std::min(a.y, b.y) > planMax.y + 5)
            continue;
        if (!onFloorLayer({Kind::Wall, i}))
            continue;
        bool const edge = (selection.kind == Kind::Wall && selection.index == i) || groupContains({Kind::Wall, i});
        bool const vertex = selection.kind == Kind::Vertex && selection.index == i;
        draw->AddLine(a, b, edge ? accent : w.nextwall >= 0 ? portalColor : wallColor, edge ? 3.f : 1.5f);
        if (vertex || planZoom >= .024f)
            draw->AddCircleFilled(a, vertex ? 5.f : 2.5f, vertex ? accent : wallColor, 8);
    }
    for (int i = 0; i < int(board.sprites.size()); ++i)
    {
        auto const center = project(board.sprites[i].xy);
        if (center.x < planMin.x - 12 || center.x > planMax.x + 12 ||
            center.y < planMin.y - 12 || center.y > planMax.y + 12)
            continue;
        if (!onFloorLayer({Kind::Sprite, i}))
            continue;
        auto const &sprite = board.sprites[i];
        bool const activeSprite = (selection.kind == Kind::Sprite && selection.index == i) || groupContains({Kind::Sprite, i});
        if (DukeEditor::IsPlatform(sprite))
        {
            ImVec2 footprint[4];
            auto const corners = DukeEditor::PlatformCorners(sprite);
            for (int j = 0; j < 4; ++j) footprint[j] = project(corners[j]);
            draw->AddConvexPolyFilled(footprint, 4, IM_COL32(76, 179, 255, 46));
            draw->AddPolyline(footprint, 4, activeSprite ? accent : IM_COL32(76, 179, 255, 210), ImDrawFlags_Closed, 2);
        }
        draw->AddCircleFilled(center, activeSprite ? 6.f : 4.f, activeSprite ? accent : portalColor, 10);
        float const ang = board.sprites[i].ang * BANG2RAD;
        draw->AddLine(center, {center.x + std::cos(ang)*10, center.y + std::sin(ang)*10},
                      activeSprite ? accent : portalColor, 1.5f);
    }

    if (board.startSector >= 0 && onFloorLayer({Kind::Start, 0}))
    {
        auto const center = project({board.start.x, board.start.y});
        float const angle = board.angle * BANG2RAD;
        ImVec2 const head{center.x + 12 * std::cos(angle), center.y + 12 * std::sin(angle)};
        ImVec2 const left{center.x + 8 * std::cos(angle + 2.5f), center.y + 8 * std::sin(angle + 2.5f)};
        ImVec2 const right{center.x + 8 * std::cos(angle - 2.5f), center.y + 8 * std::sin(angle - 2.5f)};
        draw->AddTriangleFilled(head, left, right, accent);
        if (selection.kind == Kind::Start)
            draw->AddCircle(center, 15, accent);
    }

    if (!outline.empty() && (tool == Tool::Room || tool == Tool::Pillar))
    {
        for (size_t i = 0; i < outline.size(); ++i)
        {
            auto const p = project(outline[i]);
            draw->AddCircleFilled(p, i ? 4.f : 6.f, accent, 12);
            if (i)
                draw->AddLine(project(outline[i-1]), p, accent, 2);
        }
        if (sceneHovered)
            draw->AddLine(project(outline.back()), project(mapPoint(mouse, true)), accent, 1.5f);
    }
    if (sceneHovered && tool == Tool::Prefab)
    {
        auto const anchor = mapPoint(mouse, true);
        for (auto const &item : prefabBrush.sprites)
        {
            auto const at = project({anchor.x + item.sprite.x, anchor.y + item.sprite.y});
            draw->AddCircle(at, 6.f, accent, 12, 2.f);
        }
    }
    draw->PopClipRect();
}

void switchTool(Tool next)
{
    if (tool != next)
        outline.clear();
    tool = next;
    if (tool != Tool::Select)
    {
        view3D = false;
        sectionView = false;
    }
}

bool surfaceClick()
{
    return sceneHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
           distanceSquared(ImGui::GetMouseDragDelta(ImGuiMouseButton_Right), {0, 0}) < 1.f;
}

void drawSurfaceMenu()
{
    if (std::exchange(openSurfaceMenu, false))
        ImGui::OpenPopup("Surface actions");
    if (ImGui::BeginPopup("Surface actions"))
    {
        auto const current = surfaceRevision == nextRevision;
        if (ImGui::MenuItem("Play From Here", nullptr, false, current))
        {
            queuedPlayPoint = surfacePoint;
            queued = Action::Play;
        }
        if (!view3D && !filterFloor && surfacePoint.sector >= 0)
        {
            int const platform = DukeEditor::PlatformAt(board, surfacePoint.sector, surfacePoint.xy, false, 0);
            if (platform >= 0 && ImGui::MenuItem("Play on catwalk", nullptr, false, current))
            {
                queuedPlayPoint = surfacePoint;
                queuedPlayPoint.platform = platform;
                queued = Action::Play;
            }
        }
        ImGui::EndPopup();
    }
}

void drawPlanScene()
{
    planMin = sceneMin;
    planMax = sceneMax;
    if (fitPlan)
        framePlan();
    auto const &io = ImGui::GetIO();
    if (sceneHovered)
    {
        if (io.MouseWheel != 0 && !drag)
        {
            auto const anchor = mapPoint(io.MousePos, false);
            planZoom = clamp(planZoom * std::pow(1.19f, io.MouseWheel), .001f, 3.f);
            auto const moved = project(anchor);
            planOrigin.x += (moved.x - io.MousePos.x) / planZoom;
            planOrigin.y += (moved.y - io.MousePos.y) / planZoom;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (tool == Tool::Select || editable()))
            planClicked(io.MousePos);
    }
    if ((sceneHovered || ImGui::IsItemActive()) && !drag &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
    {
        planOrigin.x -= io.MouseDelta.x / planZoom;
        planOrigin.y -= io.MouseDelta.y / planZoom;
        planOrigin.x = clamp(planOrigin.x, float(-BXY_MAX), float(BXY_MAX));
        planOrigin.y = clamp(planOrigin.y, float(-BXY_MAX), float(BXY_MAX));
    }
    if (editable() && !batchSelection && tool == Tool::Select && selection.kind != Kind::Nothing && (drag || ImGui::IsItemActive()) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        moveDrag(io.MousePos);
    if (drag && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        finishDrag();
    drawPlan(ImGui::GetWindowDrawList(), io.MousePos);
    if (surfaceClick())
    {
        PlayPoint point;
        point.xy = mapPoint(io.MousePos, false);
        point.sector = DukeEditor::LayerAt(board, point.xy, selectedSector(), filterFloor, floorLayer);
        if (point.sector >= 0)
        {
            if (filterFloor)
                point.platform = DukeEditor::PlatformAt(board, point.sector, point.xy, true, floorLayer);
            surfacePoint = point;
            surfaceRevision = nextRevision;
            openSurfaceMenu = true;
        }
    }
}

void animateCamera()
{
    auto const &io = ImGui::GetIO();
    if (sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        cameraLook = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        cameraLook = false;
    if (!cameraLook)
        return;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        cameraAngle = (cameraAngle + int(std::lround(io.MouseDelta.x * 3.f))) & 2047;
        cameraHorizon = clamp(cameraHorizon - int(std::lround(io.MouseDelta.y)), -100, 300);
    }
    int const forward = ImGui::IsKeyDown(ImGuiKey_W) - ImGui::IsKeyDown(ImGuiKey_S);
    int const strafe = ImGui::IsKeyDown(ImGuiKey_D) - ImGui::IsKeyDown(ImGuiKey_A);
    int const rise = ImGui::IsKeyDown(ImGuiKey_Q) - ImGui::IsKeyDown(ImGuiKey_E);
    float const speed = std::min(io.DeltaTime, .1f) * (io.KeyShift ? 8000.f : 2500.f);
    float const angle = cameraAngle * BANG2RAD;
    vec2_t const next{camera.x + int(speed * (std::cos(angle) * forward - std::sin(angle) * strafe)),
                      camera.y + int(speed * (std::sin(angle) * forward + std::cos(angle) * strafe))};
    int const area = board.sectorAt(next, cameraSector);
    if (area < 0)
        return;
    int const top = board.ceilingAt(area, next), bottom = board.floorAt(area, next);
    if (int64_t(bottom) - top < 512 || (area != cameraSector && (camera.z < top || camera.z > bottom)))
        return;
    camera = {next.x, next.y, clamp(camera.z - int(rise * speed * 12), top + 256, bottom - 256)};
    cameraSector = area;
}

void drawMechanismLinks(ImDrawList *draw, PlanProjection const &plan)
{
    if (!showLinks && !showPreset)
        return;
    static uint64_t linkRevision = 0;
    static GameTarget linkTarget = GameTarget::Inherit;
    static std::vector<DukeEditor::MapLink> links;
    if (linkRevision != nextRevision || linkTarget != projectWorkspace.effectiveTarget)
    {
        links = DukeEditor::DukeMechanisms(projectWorkspace.effectiveTarget) ? DukeEditor::MapLinks(board) :
                std::vector<DukeEditor::MapLink>{};
        linkRevision = nextRevision;
        linkTarget = projectWorkspace.effectiveTarget;
    }
    draw->PushClipRect(plan.min, plan.max, true);
    auto drawLink = [&](Selection source, Selection target, ImU32 color, int tag) {
        auto const a = plan.point(DukeEditor::ObjectPosition(board, source));
        auto const b = plan.point(DukeEditor::ObjectPosition(board, target));
        draw->AddLine(a, b, color, 1.7f);
        draw->AddCircle(a, 5.f, color);
        draw->AddCircleFilled(b, 4.f, color);
        if (tag > 0 && !view3D && distanceSquared(a, b) > 625.f)
        {
            ImVec2 const center{(a.x + b.x) * .5f, (a.y + b.y) * .5f};
            auto const text = std::to_string(tag);
            auto const size = ImGui::CalcTextSize(text.c_str());
            draw->AddRectFilled({center.x - 3, center.y - 1}, {center.x + size.x + 3, center.y + size.y + 1}, ink, 3);
            draw->AddText(center, color, text.c_str());
        }
    };
    if (showLinks)
        for (auto const &link : links)
        {
            auto const sector = DukeEditor::ObjectSector(board, link.target);
            Selection const target = sector >= 0 && link.target.kind == Kind::Sprite ?
                Selection{Kind::Sector, sector} : link.target;
            bool const selected = selection == link.source || selection == link.target || selection == target;
            drawLink(link.source, target, selected ? accent : IM_COL32(154, 157, 252, 168), selected ? link.tag : 0);
        }
    if (showPreset && unsigned(preset.section) < board.sectors.size())
    {
        Selection const target{Kind::Sector, preset.section};
        for (int wall : preset.buttons)
            drawLink({Kind::Wall, wall}, target, IM_COL32(222, 165, 255, 225), 0);
        for (int stop : {preset.firstStop, preset.secondStop})
            if (stop >= 0)
                drawLink(target, {Kind::Sector, stop}, portalColor, 0);
    }
    draw->PopClipRect();
}

void sectionCrossing(vec2_t a, vec2_t b, std::vector<float> &cuts)
{
    float const cut = cutCoordinate + .5f;
    auto const aCut = cutAlongX ? a.y : a.x;
    auto const bCut = cutAlongX ? b.y : b.x;
    if ((aCut < cut) != (bCut < cut))
    {
        float const t = (cut - aCut) / (bCut - aCut);
        auto const axisA = cutAlongX ? a.x : a.y, axisB = cutAlongX ? b.x : b.y;
        cuts.push_back(axisA + (axisB - axisA) * t);
    }
}

void drawSection()
{
    struct Span
    {
        int index;
        float a, b;
        int floorA, floorB, ceilingA, ceilingB;
        bool platform;
    };
    std::vector<Span> spans;
    auto const point = [](float x) {
        return cutAlongX ? vec2_t{int(std::lround(x)), cutCoordinate} : vec2_t{cutCoordinate, int(std::lround(x))};
    };
    for (int i = 0; i < int(board.sectors.size()); ++i)
    {
        auto const &s = board.sectors[i];
        std::vector<float> cuts;
        for (int w = s.wallptr; w < s.wallptr + s.wallnum; ++w)
            sectionCrossing(board.walls[w].xy, board.walls[board.walls[w].point2].xy, cuts);
        std::sort(cuts.begin(), cuts.end());
        for (size_t n = 1; n < cuts.size(); n += 2)
        {
            auto const a = point(cuts[n - 1]), b = point(cuts[n]);
            spans.push_back({i, cuts[n - 1], cuts[n], board.floorAt(i, a), board.floorAt(i, b),
                             board.ceilingAt(i, a), board.ceilingAt(i, b), false});
        }
    }
    for (int i = 0; i < int(board.sprites.size()); ++i)
    {
        auto const &s = board.sprites[i];
        if (!DukeEditor::IsPlatform(s))
            continue;
        auto const corners = DukeEditor::PlatformCorners(s);
        std::vector<float> cuts;
        for (int n = 0; n < 4; ++n)
            sectionCrossing(corners[n], corners[(n + 1) % 4], cuts);
        if (cuts.size() == 2)
            spans.push_back({i, std::min(cuts[0], cuts[1]), std::max(cuts[0], cuts[1]), s.z, s.z, s.z, s.z, true});
    }
    auto const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(sceneMin, sceneMax, plane, 4);
    if (spans.empty())
    {
        draw->AddText({sceneMin.x + 14, sceneMin.y + 14}, mute, "Move the cut through the map.");
        return;
    }
    float low = spans.front().a, high = spans.front().b;
    auto top = spans.front().ceilingA, bottom = spans.front().floorA;
    for (auto const &span : spans)
    {
        low = std::min(low, span.a);
        high = std::max(high, span.b);
        top = std::min({top, span.ceilingA, span.ceilingB});
        bottom = std::max({bottom, span.floorA, span.floorB});
        if (!span.platform && (board.sectors[span.index].lotag & 0xff) >= 16 &&
            (board.sectors[span.index].lotag & 0xff) <= 19)
            for (int neighbor : DukeEditor::Neighbors(board, span.index))
            {
                top = std::min(top, board.sectors[neighbor].floorz);
                bottom = std::max(bottom, board.sectors[neighbor].floorz);
            }
    }
    float const scale = std::max(1.f, sceneMax.x - sceneMin.x - 36) / std::max(high - low, 128.f);
    float const scaleZ = std::max(1.f, sceneMax.y - sceneMin.y - 60) / std::max(double(bottom) - top, 8192.);
    auto const projectSide = [&](float x, int z) {
        return ImVec2{sceneMin.x + 18 + (x - low) * scale, sceneMin.y + 24 + float(double(z) - top) * scaleZ};
    };
    auto const clicked = sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    auto const mouse = ImGui::GetIO().MousePos;
    Selection selected;
    int selectedZ = 0;
    std::string summary;
    bool hoveringPlatform = false;
    draw->PushClipRect(sceneMin, sceneMax, true);
    for (auto const &span : spans)
    {
        auto const a = projectSide(span.a, span.floorA), b = projectSide(span.b, span.floorB);
        auto const c = projectSide(span.b, span.ceilingB), d = projectSide(span.a, span.ceilingA);
        Selection const object{span.platform ? Kind::Sprite : Kind::Sector, span.index};
        auto const color = selection == object ? accent : span.platform ? IM_COL32(83, 183, 255, 255) : portalColor;
        if (span.platform)
        {
            draw->AddLine(a, b, color, 3);
            auto const position = DukeEditor::ObjectPosition(board, object);
            int const area = DukeEditor::ObjectSector(board, object);
            auto const gap = (double(span.floorA) - board.ceilingAt(area, position)) / 256.;
            auto const drop = (double(board.floorAt(area, position)) - span.floorA) / 256.;
            char label[80];
            Bsnprintf(label, sizeof(label), "Catwalk  %.0f above · %.0f below", gap, drop);
            if (b.x - a.x > ImGui::CalcTextSize(label).x)
                draw->AddText({a.x + 3.f, a.y - 18.f}, color, label);
        }
        else
        {
            draw->AddQuadFilled(a, b, c, d, selection == object ? IM_COL32(255, 177, 93, 34) : IM_COL32(60, 109, 125, 89));
            draw->AddQuad(a, b, c, d, color, 1.5f);
            auto const headroom = (double(span.floorA) + span.floorB - span.ceilingA - span.ceilingB) / 512.;
            char label[64];
            Bsnprintf(label, sizeof(label), "Sector %d   %.0f clearance", span.index, headroom);
            if (b.x - a.x > ImGui::CalcTextSize(label).x + 8 && a.y - d.y > 22)
                draw->AddText({a.x + 4, (a.y + d.y) * .5f - 7}, wallColor, label);
            auto const lotag = board.sectors[span.index].lotag & 0xff;
            if (lotag >= 16 && lotag <= 19)
            {
                int first = span.floorA, last = span.floorA;
                for (int s : DukeEditor::Neighbors(board, span.index))
                {
                    first = std::min(first, board.sectors[s].floorz);
                    last = std::max(last, board.sectors[s].floorz);
                }
                auto const liftTop = projectSide((span.a + span.b) * .5f, first);
                auto const liftBottom = projectSide((span.a + span.b) * .5f, last);
                draw->AddLine(liftTop, liftBottom, accent, 2.5f);
                draw->AddCircleFilled(liftTop, 4, accent);
                draw->AddCircleFilled(liftBottom, 4, accent);
            }
        }
        if (sceneHovered && mouse.x >= a.x && mouse.x <= b.x)
        {
            float const t = (mouse.x - a.x) / std::max(b.x - a.x, 1.f);
            auto const floor = a.y + (b.y - a.y) * t, ceiling = d.y + (c.y - d.y) * t;
            if (mouse.y >= ceiling - 6 && mouse.y <= floor + 6 && (span.platform || !hoveringPlatform))
            {
                int const floorZ = std::lround(span.floorA + (double(span.floorB) - span.floorA) * t);
                int const ceilingZ = std::lround(span.ceilingA + (double(span.ceilingB) - span.ceilingA) * t);
                char details[160];
                if (span.platform)
                {
                    int const area = board.sprites[span.index].sectnum;
                    auto const at = point(span.a + (span.b - span.a) * t);
                    Bsnprintf(details, sizeof(details), "Catwalk at %.1f · %.1f above · %.1f below",
                              floorZ / 256., (double(floorZ) - board.ceilingAt(area, at)) / 256.,
                              (double(board.floorAt(area, at)) - floorZ) / 256.);
                    hoveringPlatform = true;
                }
                else
                    Bsnprintf(details, sizeof(details), "Sector %d · floor %.1f · ceiling %.1f · %.1f clearance",
                              span.index, floorZ / 256., ceilingZ / 256., (double(floorZ) - ceilingZ) / 256.);
                summary = details;
                if (clicked)
                {
                    selected = object;
                    selectedZ = floorZ;
                }
            }
        }
    }
    if (selected.kind != Kind::Nothing)
    {
        selectObject(selected, true);
        floorLayer = selectedZ;
        filterFloor = true;
    }
    char label[64];
    Bsnprintf(label, sizeof(label), "Cut at %c = %d     Click a floor to select its Plan layer",
              cutAlongX ? 'Y' : 'X', cutCoordinate);
    draw->AddText({sceneMin.x + 12, sceneMax.y - 24}, mute, label);
    if (!summary.empty())
        draw->AddText({sceneMin.x + 12, sceneMin.y + 4}, wallColor, summary.c_str());
    draw->PopClipRect();
}

void floorPicker()
{
    char label[48];
    if (filterFloor)
        Bsnprintf(label, sizeof(label), "Z %.2f", floorLayer / 256.);
    else
        Bstrcpy(label, "All floors");
    ImGui::SetNextItemWidth(100);
    if (!ImGui::BeginCombo("##Floors", label))
        return;
    if (ImGui::Selectable("All floors", !filterFloor))
        filterFloor = false;
    std::set<int> levels;
    for (auto const &area : board.sectors)
        levels.insert(area.floorz);
    for (auto const &s : board.sprites)
        if (DukeEditor::IsPlatform(s))
            levels.insert(s.z);
    for (int z : levels)
    {
        char text[48];
        Bsnprintf(text, sizeof(text), "Floor Z %.2f##%d", z / 256.f, z);
        if (ImGui::Selectable(text, filterFloor && z == floorLayer))
        {
            filterFloor = true;
            floorLayer = z;
        }
    }
    ImGui::EndCombo();
}

void drawLiveOverview(ImDrawList *draw, ImVec2 min, ImVec2 max)
{
    draw->AddRectFilled(min, max, IM_COL32(17, 28, 36, 242), 5);
    draw->AddRect(min, max, gridMajor, 5);
    auto const plan = fitWalls({min.x + 4, min.y + 4}, {max.x - 4, max.y - 4}, 0, board.walls.size());
    for (auto const &edge : board.walls)
        draw->AddLine(plan.point(edge.xy), plan.point(board.walls[edge.point2].xy), edge.nextwall < 0 ? wallColor : gridMajor);

    auto const here = plan.point({camera.x, camera.y});
    if (cameraSector >= 0 && plan.contains(here, -6.f))
    {
        float const angle = cameraAngle * BANG2RAD;
        draw->AddCircleFilled(here, 3.f, accent, 10);
        draw->AddLine(here, {here.x + 8.f * std::cos(angle), here.y + 8.f * std::sin(angle)}, accent, 1.5f);
    }
    drawPlanParticipants(draw, plan, true);
    drawMechanismLinks(draw, plan);
}

void drawScene()
{
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
        ImGui::GetWindowPos().y + 40}, IM_COL32(22, 31, 41, 255), 7);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scene");
    ImGui::SameLine(0, 16);
    if (choice("Plan", !view3D && !sectionView)) view3D = sectionView = false;
    ImGui::SameLine();
    if (choice("3D", view3D))
    {
        view3D = true;
        sectionView = false;
        tool = Tool::Select;
        if (cameraSector < 0) frameScene();
    }
    ImGui::SameLine();
    if (choice("Section", sectionView))
    {
        view3D = false;
        sectionView = true;
        switchTool(Tool::Select);
        frameScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame")) frameScene();
    ImGui::SetItemTooltip("Frame the selection (F)");
    bool const shared = online() && participantCount() > 1;
    bool const showOverlays = shared || controlPointer.cursor;
    if (sectionView)
    {
        ImGui::SameLine();
        if (ImGui::Button(cutAlongX ? "Along X" : "Along Y"))
        {
            cutAlongX = !cutAlongX;
            frameScene();
        }
        ImGui::SameLine();
        if (ImGui::Button("Pick cut")) switchTool(Tool::Cut);
        ImGui::SetItemTooltip("Click in Plan to position this section.");
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) cutCoordinate = std::max(-BXY_MAX, cutCoordinate - (grid ? grid : 128));
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) cutCoordinate = std::min(BXY_MAX, cutCoordinate + (grid ? grid : 128));
    }
    else if (view3D && showOverlays)
    {
        ImGui::SameLine();
        if (choice("Live plan", showLivePlan)) showLivePlan = !showLivePlan;
        ImGui::SetItemTooltip("Show collaborators and their selections on the floor plan.");
    }
    else if (!view3D)
    {
        ImGui::SameLine();
        floorPicker();
    }

    sceneMin = ImGui::GetCursorScreenPos();
    auto const size = ImGui::GetContentRegionAvail();
    sceneMax = {sceneMin.x + std::max(size.x, 1.f), sceneMin.y + std::max(size.y, 1.f)};
    ImVec2 const overviewMax{sceneMax.x - 8.f, sceneMin.y + 8.f + std::min(size.y * .42f, 156.f)};
    ImVec2 const overviewMin{overviewMax.x - std::min(size.x * .44f, 236.f), sceneMin.y + 8.f};
    bool const overview = view3D && ((showOverlays && showLivePlan) || showLinks || showPreset) &&
                          size.x >= 248.f && size.y >= 166.f;
    ImGui::InvisibleButton("Scene canvas", {sceneMax.x - sceneMin.x, sceneMax.y - sceneMin.y},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    sceneHovered = ImGui::IsItemHovered() && !(overview && ImGui::IsMouseHoveringRect(overviewMin, overviewMax));
    auto const mouseDelta = ImGui::GetIO().MouseDelta;
    if ((sceneHovered && (mouseDelta.x != 0.f || mouseDelta.y != 0.f)) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        controlPointer = {};
    pick3D = pickSurface3D = false;
    auto const draw = ImGui::GetWindowDrawList();
    if (view3D)
    {
        animateCamera();
        pickSurface3D = surfaceClick();
        if (pickSurface3D || (sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !cameraLook))
        {
            pick3D = true;
            pickPosition = ImGui::GetIO().MousePos;
        }
        draw->AddRectFilled({sceneMin.x + 8, sceneMax.y - 30}, {sceneMax.x - 8, sceneMax.y - 7}, IM_COL32(15, 21, 27, 220), 4);
        draw->AddText({sceneMin.x + 15, sceneMax.y - 27}, mute, "Right-drag look  |  WASD fly  |  Q/E height");
        if (shared)
            drawPerspectivePointers(draw);
        if (overview)
            drawLiveOverview(draw, overviewMin, overviewMax);
    }
    else if (sectionView)
        drawSection();
    else
    {
        drawPlanScene();
        drawMechanismLinks(draw, {planMin, planMax, planOrigin, planZoom});
        if (showOverlays)
            drawPlanParticipants(draw, {planMin, planMax, planOrigin, planZoom}, false);
    }
    drawSurfaceMenu();
    sendLivePointer();
}

void render3D()
{
    if (viewDirty)
    {
        board.install();
        viewDirty = false;
    }
    cameraSector = board.sectorAt({camera.x, camera.y}, cameraSector);
    if (cameraSector < 0)
        return;

    auto const start = ImGui::GetMainViewport()->Pos;
    int const x1 = std::max(0, int(sceneMin.x - start.x)), y1 = std::max(0, int(sceneMin.y - start.y));
    int const x2 = std::min(xdim - 1, int(sceneMax.x - start.x) - 1), y2 = std::min(ydim - 1, int(sceneMax.y - start.y) - 1);
    if (x2 <= x1 || y2 <= y1)
        return;
    videoSetViewableArea(x1, y1, x2, y2);
    int16_t const oldEditStatus = editstatus;
    editstatus = 1;
    searchit = pick3D ? 2 : 0;
    if (pick3D)
    {
        searchx = int(pickPosition.x - start.x) - x1;
        searchy = int(pickPosition.y - start.y) - y1;
        searchstat = -1;
    }
    drawrooms(camera.x, camera.y, camera.z, cameraAngle, cameraHorizon, cameraSector);
    renderDrawMasks();
    if (pickSurface3D && searchit == 1)
    {
        auto const &hit = polymost_hitdata;
        PlayPoint point;
        point.xy = hit.xy;
        point.sector = hit.sect;
        point.angle = cameraAngle;
        if (hit.wall < 0 && hit.sprite < 0 && searchstat == 2 && hit.sect == searchsector)
        {
            surfacePoint = point;
            openSurfaceMenu = true;
        }
        else if (unsigned(hit.sprite) < board.sprites.size() &&
                 DukeEditor::IsPlatform(board.sprites[hit.sprite]))
        {
            point.platform = hit.sprite;
            surfacePoint = point;
            openSurfaceMenu = true;
        }
        if (openSurfaceMenu)
            surfaceRevision = nextRevision;
    }
    else if (pick3D && searchit == 1)
    {
        if (searchstat == 3 && unsigned(searchwall) < board.sprites.size())
            selectObject({Kind::Sprite, searchwall}, true);
        else if ((searchstat == 0 || searchstat == 4) && unsigned(searchwall) < board.walls.size())
            selectObject({Kind::Wall, searchwall}, true);
        else if ((searchstat == 1 || searchstat == 2) && unsigned(searchsector) < board.sectors.size())
        {
            selection = {Kind::Sector, searchsector};
            floorSurface = searchstat == 2;
        }
    }
    searchit = 0;
    editstatus = oldEditStatus;
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);
}

char const *tileName(int tile)
{
    return unsigned(tile) < tileNames.size() && tileNames[tile] ? tileNames[tile] : "Tile";
}

bool rowSelected(Kind kind, int index)
{
    return selection == Selection{kind, index} || groupContains({kind, index});
}

void hierarchyRow(char const *label, Kind kind, int index, ImGuiTextFilter const &filter)
{
    if (filter.PassFilter(label) &&
        ImGui::Selectable(label, rowSelected(kind, index), ImGuiSelectableFlags_AllowDoubleClick))
    {
        selectObject({kind, index}, true);
        switchTool(Tool::Select);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            frameScene();
    }
}

void drawHierarchy()
{
    ImGui::TextUnformatted("Hierarchy");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##hierarchyFilter", "Search scene", hierarchySearch, sizeof(hierarchySearch));
    ImGuiTextFilter const filter(hierarchySearch);
    ImGui::BeginChild("Hierarchy items");
    if (online())
    {
        auto const heading = "Participants " + std::to_string(participantCount()) + "###Participants";
        if (ImGui::TreeNodeEx(heading.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int i = 0; i < DukeEditor::maxEditors; ++i)
            {
                auto const &peer = live.players()[i];
                if (peer.name.empty())
                    continue;
                ImGui::TextColored(playerColor(i), "%s%s%s", peer.name.c_str(), peer.ai ? " (AI)" : "",
                                   i == live.localSlot() ? " (You)" : "");
                if (i != live.localSlot())
                    ImGui::TextDisabled("%s", playerActivity(peer.presence).c_str());
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
    }
    if (board.startSector >= 0)
        hierarchyRow("Player Start", Kind::Start, 0, filter);
    char label[104];
    Bsnprintf(label, sizeof(label), "Sectors  %u###Sectors", unsigned(board.sectors.size()));
    if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (int i = 0; i < int(board.sectors.size()); ++i)
        {
            auto const &area = board.sectors[i];
            Bsnprintf(label, sizeof(label), "Sector %d%s", i, area.lotag == -1 ? "  (Exit)" : "");
            hierarchyRow(label, Kind::Sector, i, filter);
        }
        ImGui::TreePop();
    }
    Bsnprintf(label, sizeof(label), "Sprites  %u###Sprites", unsigned(board.sprites.size()));
    if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (int i = 0; i < int(board.sprites.size()); ++i)
        {
            auto const &item = board.sprites[i];
            Bsnprintf(label, sizeof(label), "%s  %d##sprite%d", tileName(item.picnum), i, i);
            hierarchyRow(label, Kind::Sprite, i, filter);
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

bool readNumber(char const *label, int &number, int low, int high)
{
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x * .49f, 66.f));
    if (!ImGui::InputInt(label, &number, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
        return false;
    number = clamp(number, low, high);
    return true;
}

template <typename T>
void numberField(char const *label, T &member, int low, int high)
{
    int number = member;
    if (readNumber(label, number, low, high) && number != member)
        change([&] { member = static_cast<T>(number); return true; });
}

template <typename T>
void flagField(char const *label, T &members, int flag)
{
    bool enabled = members & flag;
    if (ImGui::Checkbox(label, &enabled))
        change([&] { members = static_cast<T>(enabled ? members | flag : members & ~flag); return true; });
}

Thumbnail const &thumbnail(int tile)
{
    static Thumbnail const missing;
    if (unsigned(tile) >= thumbnails.size())
        return missing;
    auto &thumb = thumbnails[tile];
    int const sx = tilesiz[tile].x, sy = tilesiz[tile].y;
    if (thumb.texture || sx <= 0 || sy <= 0)
        return thumb;
    if (!waloff[tile])
        tileLoad(tile);
    if (!waloff[tile])
        return thumb;
    auto const pixels = reinterpret_cast<uint8_t const *>(waloff[tile]);
    int const width = std::min(sx, 64), height = std::min(sy, 64);
    std::vector<uint8_t> rgba(4 * width * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            int const index = pixels[(x * sx / width) * sy + (y * sy / height)];
            auto const c = paletteGetColor(index);
            auto const p = &rgba[4 * (y * width + x)];
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = index == 255 ? 0 : 255;
        }

    GLint previous;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glGenTextures(1, &thumb.texture);
    glBindTexture(GL_TEXTURE_2D, thumb.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, previous);
    thumb.ratio = float(sx) / sy;
    return thumb;
}

void drawTile(ImDrawList *draw, int tile, ImVec2 center, ImVec2 area)
{
    auto const &image = thumbnail(tile);
    if (!image.texture)
        return;
    float const width = std::min(area.x, area.y * image.ratio);
    float const height = width / image.ratio;
    draw->AddImage((ImTextureID)(intptr_t)image.texture,
                   {center.x - width * .5f, center.y - height * .5f},
                   {center.x + width * .5f, center.y + height * .5f});
}

void tileField(char const *label, int16_t &tile, int minimum = 0)
{
    numberField(label, tile, minimum, artLimit - 1);
    if (tile < 0)
        return;
    auto const cursor = ImGui::GetCursorScreenPos();
    drawTile(ImGui::GetWindowDrawList(), tile, {cursor.x + 25, cursor.y + 24}, {44, 44});
    ImGui::Dummy({52, 48});
    ImGui::SameLine();
    ImGui::TextWrapped("%s", tileName(tile));
}

bool inspectorSection(char const *label, bool &open)
{
    ImGui::SetNextItemOpen(open, ImGuiCond_Always);
    open = ImGui::CollapsingHeader(label);
    return open;
}

void inspectSector()
{
    auto &s = board.sectors[selection.index];
    ImGui::TextColored(accentText, "Sector %d", selection.index);
    ImGui::TextDisabled("%d walls", s.wallnum);
    ImGui::Separator();
    numberField("Floor Z", s.floorz, s.ceilingz, 1 << 23);
    numberField("Ceiling Z", s.ceilingz, -(1 << 23), s.floorz);
    ImGui::TextDisabled("Lower Z is higher in the map.");
    if (choice("Floor", floorSurface)) floorSurface = true;
    ImGui::SameLine();
    if (choice("Ceiling", !floorSurface)) floorSurface = false;
    auto &stat = floorSurface ? s.floorstat : s.ceilingstat;
    auto &slope = floorSurface ? s.floorheinum : s.ceilingheinum;
    tileField("Tile", floorSurface ? s.floorpicnum : s.ceilingpicnum);
    numberField("Shade", floorSurface ? s.floorshade : s.ceilingshade, -128, 127);
    numberField("Palette", floorSurface ? s.floorpal : s.ceilingpal, 0, 255);
    flagField("Parallax sky", stat, 1);
    flagField("Slope", stat, 2);
    if (stat & 2)
        numberField("Slope amount", slope, INT16_MIN, INT16_MAX);
    if (inspectorSection("Tags and surface flags", sectorTagsOpen))
    {
        numberField("LoTag", s.lotag, INT16_MIN, INT16_MAX);
        numberField("HiTag", s.hitag, INT16_MIN, INT16_MAX);
        numberField("Extra", s.extra, INT16_MIN, INT16_MAX);
        numberField("Flags", stat, 0, 127);
        numberField("Pan X", floorSurface ? s.floorxpanning : s.ceilingxpanning, 0, 255);
        numberField("Pan Y", floorSurface ? s.floorypanning : s.ceilingypanning, 0, 255);
        numberField("Visibility", s.visibility, 0, 255);
        bool levelExit = s.lotag == -1;
        if (ImGui::Checkbox("End level on entry", &levelExit))
            change([&] { s.lotag = levelExit ? -1 : 0; return true; });
    }
}

void inspectWall()
{
    int const index = selection.index;
    auto &w = board.walls[index];
    ImGui::TextColored(accentText, "Wall %d", index);
    if (w.nextsector >= 0)
        ImGui::TextDisabled("Portal to sector %d", w.nextsector);
    else
        ImGui::TextDisabled("Solid boundary");
    ImGui::Separator();
    tileField("Tile", w.picnum);
    numberField("Shade", w.shade, -128, 127);
    numberField("Palette", w.pal, 0, 255);
    flagField("Blocking", w.cstat, 1);
    auto applyWallFlags = [&](int flags) {
        change([&] {
            w.cstat = flags;
            if ((flags & 48) && w.overpicnum < 0)
                w.overpicnum = w.picnum;
            return true;
        });
    };
    bool masked = (w.cstat & 16) != 0;
    if (ImGui::Checkbox("Mask overlay", &masked))
        applyWallFlags(masked ? w.cstat | 16 : w.cstat & ~16);
    if (w.cstat & 16)
        numberField("Overlay tile", w.overpicnum, 0, artLimit - 1);
    if (inspectorSection("Mapping and tags", wallMappingOpen))
    {
        numberField("X repeat", w.xrepeat, 0, 255);
        numberField("Y repeat", w.yrepeat, 0, 255);
        numberField("Pan X", w.xpanning, 0, 255);
        numberField("Pan Y", w.ypanning, 0, 255);
        int flags = w.cstat;
        if (readNumber("Flags", flags, 0, 1023) && flags != w.cstat)
            applyWallFlags(flags);
        numberField("LoTag", w.lotag, INT16_MIN, INT16_MAX);
        numberField("HiTag", w.hitag, INT16_MIN, INT16_MAX);
        numberField("Extra", w.extra, INT16_MIN, INT16_MAX);
    }

    ImGui::Separator();
    if (ImGui::Button("Split at midpoint"))
    {
        int midpoint;
        if (change([&] { return board.splitWall(index, &midpoint); }))
        {
            selection = {Kind::Vertex, midpoint};
            status("Wall split");
            return;
        }
        status("This wall cannot be split at its midpoint.", true);
    }
    bool const closed = board.walls[index].nextwall < 0;
    ImGui::BeginDisabled(!closed);
    readNumber("Room depth", depth, 128, 32768);
    if (ImGui::Button("Extend room"))
    {
        if (change([&] { return board.extendRoom(index, depth); }))
        {
            selection = {Kind::Sector, int(board.sectors.size()) - 1};
            fitPlan = fitSelection = true;
            status("Adjoining sector added");
        }
        else
            status("Could not extend this wall into a room.", true);
    }
    ImGui::EndDisabled();
}

void inspectVertex()
{
    int const index = selection.index;
    auto const &vertex = board.walls[index];
    ImGui::TextColored(accentText, "Vertex %d", index);
    ImGui::Separator();
    auto target = vertex.xy;
    bool edited = readNumber("X", target.x, -BXY_MAX, BXY_MAX);
    edited |= readNumber("Y", target.y, -BXY_MAX, BXY_MAX);
    if (editable() && edited && (target.x != vertex.x || target.y != vertex.y))
    {
        Snapshot before{board, revision, projectWorkspace.map.scripts};
        for (int v : board.linkedVertices({index}))
            board.walls[v].xy = target;
        std::string error;
        if (board.validate(artLimit, error, true) && board.validateGeometryChange(before.board, error))
            commit(std::move(before));
        else
        {
            board = std::move(before.board);
            status(error, true);
        }
    }
    ImGui::TextDisabled("Drag in Plan to move.");
}

void inspectSprite()
{
    auto &s = board.sprites[selection.index];
    ImGui::TextColored(accentText, "Sprite %d", selection.index);
    ImGui::TextDisabled("Sector %d", s.sectnum);
    ImGui::Separator();
    auto target = s.xy;
    bool edited = readNumber("X", target.x, -BXY_MAX, BXY_MAX);
    edited |= readNumber("Y", target.y, -BXY_MAX, BXY_MAX);
    if (edited && (target.x != s.x || target.y != s.y) &&
        !change([&] { return board.moveSprite(selection.index, target); }))
        status("Sprites must stay inside a sector.", true);
    numberField("Z", s.z, -(1 << 23), 1 << 23);
    int const height = (int64_t(DukeEditor::SpriteFloor(board, selection.index)) - s.z) / 256;
    int relativeHeight = height;
    if (readNumber("Above floor", relativeHeight, 0, 1 << 20) && height != relativeHeight)
    {
        std::string error;
        if (!change([&] { return DukeEditor::SetObjectHeight(board, {selection}, relativeHeight, error); },
                    "Set floor-relative height") && !error.empty())
            status(error, true);
    }
    ImGui::SetItemTooltip("Height above the floor or the catwalk that supports this sprite.");
    numberField("Angle", s.ang, 0, 2047);
    tileField("Tile", s.picnum);
    numberField("Shade", s.shade, -128, 127);
    numberField("Palette", s.pal, 0, 255);
    flagField("Blocking", s.cstat, 1);
    if (inspectorSection("Sprite flags and tags", spriteTagsOpen))
    {
        numberField("LoTag", s.lotag, INT16_MIN, INT16_MAX);
        numberField("HiTag", s.hitag, INT16_MIN, INT16_MAX);
        numberField("Extra", s.extra, INT16_MIN, INT16_MAX);
        numberField("X repeat", s.xrepeat, 0, 255);
        numberField("Y repeat", s.yrepeat, 0, 255);
        numberField("Offset X", s.xoffset, -128, 127);
        numberField("Offset Y", s.yoffset, -128, 127);
        numberField("Clip distance", s.clipdist, 0, 255);
        numberField("Status", s.statnum, 0, MAXSTATUS - 1);
        int flags = s.cstat;
        if (readNumber("Flags", flags, 0, 0x83ff) && (flags & ~0x83ff) == 0 && (flags & 48) != 48)
            change([&] { s.cstat = flags; return true; });
    }
}

void inspectStart()
{
    ImGui::TextColored(accentText, "Player Start");
    ImGui::TextDisabled("Sector %d", board.startSector);
    ImGui::Separator();
    vec2_t target{board.start.x, board.start.y};
    bool edited = readNumber("X", target.x, -BXY_MAX, BXY_MAX);
    edited |= readNumber("Y", target.y, -BXY_MAX, BXY_MAX);
    if (edited && !change([&] { return board.setStart(target, board.startSector); }))
        status("Choose a position inside a tall enough sector.", true);
    if (board.startSector >= 0)
    {
        auto const floor = board.floorAt(board.startSector, {board.start.x, board.start.y});
        auto const ceiling = board.ceilingAt(board.startSector, {board.start.x, board.start.y});
        if (floor >= ceiling)
            numberField("Z", board.start.z, ceiling, floor);
    }
    numberField("Angle", board.angle, 0, 2047);
    if (ImGui::Button("Preview from start"))
    {
        cameraToStart();
        view3D = true;
        sectionView = false;
    }
}

void deleteSelection()
{
    bool removed = false;
    if (selection.kind == Kind::Sector)
    {
        removed = change([] { board.deleteSector(selection.index); return true; }, "Delete sector");
        if (removed) status("Sector removed. Undo restores the sector and its sprites.");
    }
    else if (selection.kind == Kind::Sprite)
    {
        removed = change([] { board.deleteSprite(selection.index); return true; }, "Delete sprite");
        if (removed) status("Sprite removed");
    }
    else
        return;
    if (removed)
        selection = {};
}

void setupInteger(char const *label, int &value, int step = 1)
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .48f);
    ImGui::InputInt(label, &value, step, step * 4);
}

template <typename F>
bool toolChange(char const *label, std::string &error, F edit)
{
    bool const changed = change([&] {
        Board next = board;
        if (!edit(next) || !next.validate(DukeEditor::Target(projectWorkspace.effectiveTarget).tiles, error, true))
            return false;
        board = std::move(next);
        return true;
    }, label);
    if (changed)
        status(label);
    else if (!error.empty())
        status(error, true);
    return changed;
}

void focusObject(Selection object)
{
    if (object.kind == Kind::Nothing)
        return;
    int const section = DukeEditor::ObjectSector(board, object);
    if (section < 0)
        return;
    selectObject(object);
    switchTool(Tool::Select);
    view3D = sectionView = false;
    if (filterFloor)
    {
        auto const xy = DukeEditor::ObjectPosition(board, object);
        floorLayer = board.floorAt(section, xy);
        if (object.kind == Kind::Sprite || object.kind == Kind::Start)
        {
            int const z = object.kind == Kind::Sprite ? board.sprites[object.index].z : board.start.z;
            int const platform = DukeEditor::PlatformAt(board, section, xy, true, z);
            if (platform >= 0)
                floorLayer = board.sprites[platform].z;
        }
    }
    frameScene();
}

void drawBatch()
{
    if (!batchSelection)
        return;
    ImGui::Text("%u selected", unsigned(group.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) group.clear();
    if (group.empty())
    {
        ImGui::TextWrapped("Click sprites or Ctrl-click walls to add or remove them.");
        ImGui::Separator();
        return;
    }
    if (ImGui::TreeNode("Selected objects"))
    {
        for (auto const object : group)
            if (ImGui::Selectable(DukeEditor::ObjectLabel(object).c_str(), selection == object))
                focusObject(object);
        ImGui::TreePop();
    }
    static int deltaX, deltaY, deltaZ, shadeDelta, height = 40;
    std::string error;
    if (ImGui::CollapsingHeader("Move", ImGuiTreeNodeFlags_DefaultOpen))
    {
        setupInteger("Offset X", deltaX, 128);
        setupInteger("Offset Y", deltaY, 128);
        setupInteger("Rise (sprites)", deltaZ, 8);
        if (ImGui::Button("Move selection"))
            toolChange("Move selection", error, [&](Board &next) {
                if (deltaZ < -(1 << 20) || deltaZ > (1 << 20))
                {
                    error = "Use a smaller rise.";
                    return false;
                }
                return DukeEditor::TranslateObjects(next, group, {deltaX, deltaY, -deltaZ * 256}, error);
            });
    }
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        setupInteger("Shade offset", shadeDelta);
        if (ImGui::Button("Adjust shade"))
            change([&] {
                for (auto const object : group)
                {
                    auto &shade = object.kind == Kind::Wall ? board.walls[object.index].shade : board.sprites[object.index].shade;
                    shade = clamp(int(shade) + clamp(shadeDelta, -255, 255), -128, 127);
                }
                return true;
            }, "Adjust selection shade");
        if (ImGui::Button("Align wall textures"))
            toolChange("Align wall textures", error, [&](Board &next) { return DukeEditor::AlignWalls(next, group, error); });
        ImGui::SetItemTooltip("Align a connected run of solid walls using the same tile.");
        setupInteger("Height above floor", height, 8);
        if (ImGui::Button("Set sprite height"))
            toolChange("Set floor-relative height", error, [&](Board &next) { return DukeEditor::SetObjectHeight(next, group, height, error); });
        ImGui::SetItemTooltip("Set sprite Z relative to each sector’s floor, in height units.");
    }
    ImGui::Separator();
}

std::vector<int> selectedPrefabSprites()
{
    std::vector<int> sprites;
    for (auto const object : group)
        if (object.kind == Kind::Sprite)
            sprites.push_back(object.index);
    if (sprites.empty() && selection.kind == Kind::Sprite)
        sprites.push_back(selection.index);
    return sprites;
}

void drawComponentInspector()
{
    using namespace DukeEditor;
    if (!projectWorkspace.modern)
        return;
    auto const kind = componentKind(selection);
    if (kind && ImGui::CollapsingHeader("Script components", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto const &scripts = projectWorkspace.map.scripts;
        for (int i = 0; i < int(scripts.size()); ++i)
        {
            auto const &component = scripts[i];
            if (component.kind != kind || component.index != selection.index)
                continue;
            ImGui::PushID(i);
            bool const remove = ImGui::SmallButton("Remove");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", component.script.c_str());
            ImGui::PopID();
            if (remove)
            {
                ProjectRequest request{ProjectAction::Detach};
                request.kind = kind;
                request.index = selection.index;
                request.script = component.script;
                projectCommand(request);
                break;
            }
        }
        if (ImGui::Button("Add Component"))
            ImGui::OpenPopup("Component assets");
        if (ImGui::BeginPopup("Component assets"))
        {
            for (auto const language : {"TypeScript", "JavaScript"})
            {
                auto const label = std::string("New ") + language + " component...";
                if (ImGui::MenuItem(label.c_str(), nullptr, false, !projectWorkspace.root.empty()))
                {
                    pendingComponent = ProjectRequest{ProjectAction::CreateComponent};
                    pendingComponent.kind = kind;
                    pendingComponent.index = selection.index;
                    Bstrncpyz(componentPath, language[0] == 'T' ? "Scripts/Component.ts" : "Scripts/Component.js",
                              sizeof(componentPath));
                    dialogRevision = nextRevision;
                    showComponentDialog = true;
                }
            }
            ImGui::Separator();
            bool found = false;
            for (auto const &item : projectWorkspace.assets)
                if (!item.asset.directory && IsScriptAsset(item.asset.path))
                {
                    found = true;
                    if (ImGui::MenuItem(item.asset.path.c_str()))
                    {
                        ProjectRequest request{ProjectAction::Attach};
                        request.kind = kind;
                        request.index = selection.index;
                        request.script = item.asset.path;
                        projectCommand(request);
                    }
                }
            if (!found)
                ImGui::TextDisabled("Import an existing script in Project settings.");
            ImGui::EndPopup();
        }
    }
    if (!projectWorkspace.root.empty())
    {
        auto sprites = selectedPrefabSprites();
        if (!sprites.empty() && ImGui::Button("Create Prefab..."))
        {
            pendingPrefab = ProjectRequest{ProjectAction::SavePrefab};
            pendingPrefab.sprites = std::move(sprites);
            Bstrncpyz(prefabPath, "Prefabs/Prefab.vbprefab", sizeof(prefabPath));
            dialogRevision = nextRevision;
            showPrefabDialog = true;
        }
    }
    ImGui::Separator();
}

void drawInspector()
{
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();
    if (ImGui::BeginChild("Properties"))
    {
        ImGui::BeginDisabled(!editable());
        drawBatch();
        ImGui::PushID(int(selection.kind));
        ImGui::PushID(selection.index);
        drawComponentInspector();
        switch (selection.kind)
        {
            case Kind::Sector: inspectSector(); break;
            case Kind::Wall: inspectWall(); break;
            case Kind::Vertex: inspectVertex(); break;
            case Kind::Sprite: inspectSprite(); break;
            case Kind::Start: inspectStart(); break;
            case Kind::Nothing:
                ImGui::TextWrapped("Select geometry, a sprite, or the player start.");
                break;
        }
        if (selection.kind == Kind::Sector || selection.kind == Kind::Sprite)
        {
            ImGui::Spacing();
            if (ImGui::Button(selection.kind == Kind::Sector ? "Delete sector" : "Delete sprite"))
                deleteSelection();
        }
        ImGui::PopID();
        ImGui::PopID();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
}

void applyBrush(int tile)
{
    if (batchSelection && !group.empty())
    {
        if (change([&] {
            for (auto const object : group)
                if (object.kind == Kind::Wall)
                    board.walls[object.index].picnum = tile;
                else
                    board.sprites[object.index].picnum = tile;
            return true;
        }, "Apply tile to selection"))
            status("Applied tile " + std::to_string(tile));
        return;
    }
    switch (selection.kind)
    {
        case Kind::Sector:
            change([&] {
                auto &s = board.sectors[selection.index];
                (floorSurface ? s.floorpicnum : s.ceilingpicnum) = tile;
                return true;
            });
            break;
        case Kind::Wall:
            change([&] { board.walls[selection.index].picnum = tile; return true; });
            break;
        case Kind::Sprite:
            change([&] { board.sprites[selection.index].picnum = tile; return true; });
            break;
        default: return;
    }
    status("Applied tile " + std::to_string(tile));
}

void suggestLiftStops()
{
    preset.firstStop = preset.secondStop = -1;
    if (preset.type != DukeEditor::Mechanism::Lift)
        return;
    auto const neighbors = DukeEditor::Neighbors(board, preset.section);
    for (int s : neighbors)
        if (preset.firstStop < 0)
            preset.firstStop = s;
        else if (board.sectors[s].floorz != board.sectors[preset.firstStop].floorz)
        {
            preset.secondStop = s;
            return;
        }
}

void openPreset(DukeEditor::Mechanism type)
{
    preset = {};
    preset.type = type;
    preset.section = selectedSector();
    if (selection.kind == Kind::Wall && board.walls[selection.index].nextwall < 0)
        preset.buttons.push_back(selection.index);
    if (type == DukeEditor::Mechanism::Trap)
        preset.speed = 24576;
    suggestLiftStops();
    showPreset = true;
    showStairs = showCatwalk = false;
    showLinks = true;
}

std::string sectorChoice(int area)
{
    if (unsigned(area) >= board.sectors.size())
        return "Choose a sector";
    char label[72];
    Bsnprintf(label, sizeof(label), "Sector %d · floor %.1f", area, board.sectors[area].floorz / 256.f);
    return label;
}

void sectorPicker(const char *label, int &section, std::vector<int> const *allowed = nullptr)
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .60f);
    if (!ImGui::BeginCombo(label, sectorChoice(section).c_str(), ImGuiComboFlags_HeightLargest))
        return;
    auto row = [&](int s) {
        if (ImGui::Selectable(sectorChoice(s).c_str(), section == s))
            section = s;
    };
    if (allowed)
        for (int s : *allowed) row(s);
    else
    {
        ImGuiListClipper clipper;
        clipper.Begin(board.sectors.size());
        while (clipper.Step())
            for (int s = clipper.DisplayStart; s < clipper.DisplayEnd; ++s)
                row(s);
    }
    ImGui::EndCombo();
}

void positionToolWindow(float width, float height)
{
    auto const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - 16, viewport->WorkPos.y + 98},
                            ImGuiCond_FirstUseEver, {1, 0});
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_FirstUseEver);
}

void drawPreset()
{
    if (!showPreset)
        return;
    positionToolWindow(385, 429);
    auto const label = std::string(DukeEditor::MechanismName(preset.type)) + "###Mechanism preset";
    if (ImGui::Begin(label.c_str(), &showPreset))
    {
        ImGui::BeginDisabled(!editable());
        int const prior = preset.section;
        if (ImGui::SmallButton("Use selected sector"))
            preset.section = selectedSector();
        sectorPicker("Target", preset.section);
        if (prior != preset.section)
            suggestLiftStops();
        using DukeEditor::Mechanism;
        if (preset.type == Mechanism::Lift)
        {
            auto const neighbors = DukeEditor::Neighbors(board, preset.section);
            sectorPicker("First stop", preset.firstStop, &neighbors);
            sectorPicker("Second stop", preset.secondStop, &neighbors);
            ImGui::TextWrapped("Connect the platform to two neighboring floor heights. Set its floor to either stop.");
        }
        if (preset.type == Mechanism::Trap)
        {
            setupInteger("Floor drop", preset.drop, 16);
            ImGui::TextWrapped("Place the trigger in a neighboring room. Use it or shoot it to close and reset the trap.");
        }
        if (preset.type == Mechanism::Light)
        {
            setupInteger("Light shade", preset.lightShade);
            setupInteger("Light palette", preset.lightPalette);
        }
        else
            setupInteger("Speed (Build)", preset.speed, preset.type == Mechanism::Trap ? 1024 : 64);
        ImGui::Separator();
        ImGui::Text("%s", preset.type == Mechanism::Lift ? "Call buttons" : "Switches");
        bool const addable = selection.kind == Kind::Wall && board.walls[selection.index].nextwall < 0 &&
                             std::find(preset.buttons.begin(), preset.buttons.end(), selection.index) == preset.buttons.end();
        ImGui::BeginDisabled(!addable);
        if (ImGui::Button("Add selected wall"))
            preset.buttons.push_back(selection.index);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Ctrl-click a solid wall in Plan or select it in the 3D view.");
        for (size_t i = 0; i < preset.buttons.size();)
        {
            int const wall = preset.buttons[i];
            ImGui::PushID(wall);
            if (ImGui::SmallButton(DukeEditor::ObjectLabel({Kind::Wall, wall}).c_str()))
                focusObject({Kind::Wall, wall});
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                preset.buttons.erase(preset.buttons.begin() + i);
            else
                ++i;
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(preset.section < 0 || preset.buttons.empty());
        if (ImGui::Button("Create linked mechanism"))
        {
            int tag = 0;
            std::string error;
            auto const action = std::string("Create ") + DukeEditor::MechanismName(preset.type);
            if (toolChange(action.c_str(), error, [&](Board &next) {
                return DukeEditor::AddMechanism(next, preset, projectWorkspace.effectiveTarget, tag, error);
            }))
            {
                focusObject({Kind::Sector, preset.section});
                showPreset = false;
                status(action + " · tag " + std::to_string(tag));
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void drawStructureTools()
{
    if (showStairs)
    {
        positionToolWindow(365, 223);
        if (ImGui::Begin("Build stairs", &showStairs))
        {
            ImGui::TextWrapped("Ctrl-click the solid wall at the foot of the flight in Plan.");
            setupInteger("Steps", stairSteps);
            setupInteger("Tread depth", stairTread, 128);
            setupInteger("Rise per step", stairRise, 4);
            ImGui::SetItemTooltip("Positive rise goes up. Negative rise goes down.");
            ImGui::BeginDisabled(!editable() || selection.kind != Kind::Wall);
            if (ImGui::Button("Create stairs"))
            {
                std::string error;
                if (toolChange("Create stairs", error, [&](Board &next) {
                    return next.addStairs(selection.index, stairSteps, stairTread, stairRise, error);
                }))
                {
                    showStairs = false;
                    focusObject({Kind::Sector, int(board.sectors.size()) - 1});
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
    if (showCatwalk)
    {
        positionToolWindow(369, 262);
        if (ImGui::Begin("Build catwalk", &showCatwalk))
        {
            ImGui::TextWrapped("Place a walkable platform in a room with clearance above and below.");
            setupInteger("Tile", catwalkTile);
            setupInteger("Length", catwalkLength, 128);
            setupInteger("Width", catwalkWidth, 128);
            setupInteger("Height above floor", catwalkHeight, 8);
            setupInteger("Angle (0–2047)", catwalkAngle, 512);
            ImGui::BeginDisabled(!editable());
            if (ImGui::Button("Place in Plan"))
            {
                switchTool(Tool::Catwalk);
                showCatwalk = false;
                status("Click the center of the catwalk in Plan.");
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
}

std::vector<DukeEditor::MapIssue> checkDraft()
{
    std::string const root = projectWorkspace.root.empty() && !sourcePath.empty() ? directoryOf(sourcePath) : projectWorkspace.root;
    auto const target = projectWorkspace.effectiveTarget;
    return DukeEditor::CheckMap(board, target, projectWorkspace.modern, projectWorkspace.map, root,
                                target == DukeEditor::RunningTarget());
}

void drawMapCheck()
{
    if (!showChecks)
        return;
    static std::vector<DukeEditor::MapIssue> issues;
    static uint64_t checkedRevision;
    static GameTarget checkedTarget;
    static bool checkedModern, refresh = true;
    static std::string checkedRoot;
    static std::vector<DukeEditor::ScriptAttachment> checkedScripts;
    auto const &workspace = projectWorkspace;
    if (refresh || checkedRevision != nextRevision || checkedTarget != projectWorkspace.effectiveTarget ||
        checkedModern != projectWorkspace.modern || checkedRoot != workspace.root || checkedScripts != workspace.map.scripts)
    {
        issues = checkDraft();
        checkedRevision = nextRevision;
        checkedTarget = projectWorkspace.effectiveTarget;
        checkedModern = projectWorkspace.modern;
        checkedRoot = workspace.root;
        checkedScripts = workspace.map.scripts;
        refresh = false;
    }
    positionToolWindow(399, 330);
    if (ImGui::Begin("Map check", &showChecks))
    {
        ImGui::TextWrapped("%s · %u findings", DukeEditor::Target(checkedTarget).label, unsigned(issues.size()));
        if (ImGui::Button("Refresh")) refresh = true;
        ImGui::Separator();
        if (issues.empty())
            ImGui::TextUnformatted("No issues found.");
        ImGui::BeginChild("Map check results");
        for (int i = 0; i < int(issues.size()); ++i)
        {
            auto const &issue = issues[i];
            ImGui::PushID(i);
            bool const canFocus = DukeEditor::ObjectSector(board, issue.object) >= 0;
            ImGui::BeginDisabled(!canFocus);
            bool const focused = ImGui::Selectable(DukeEditor::ObjectLabel(issue.object).c_str());
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s", issue.message.c_str());
            if (canFocus && (focused || ImGui::IsItemClicked()))
                focusObject(issue.object);
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void drawChangeHistory()
{
    if (!showChanges)
        return;
    positionToolWindow(375, 338);
    if (ImGui::Begin("Change history", &showChanges))
    {
        auto const &changes = online() ? live.changes() : localChanges;
        if (online())
            ImGui::TextWrapped("Undo and Redo apply to the shared draft.");
        if (changes.empty())
            ImGui::TextUnformatted("Edits appear here as they happen.");
        ImGui::BeginChild("Changes");
        bool indexesCurrent = true;
        for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        {
            auto const &entry = *it;
            auto const id = std::to_string(entry.revision);
            ImGui::PushID(id.c_str());
            auto const label = entry.label + "###Details";
            if (ImGui::TreeNodeEx(label.c_str(), it == changes.rbegin() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            {
                ImGui::TextColored(entry.slot < 0 ? accentText : playerColor(entry.slot), "%s%s · revision %s",
                                   entry.author.c_str(), entry.ai ? " (AI)" : "", id.c_str());
                if (entry.structure)
                    ImGui::TextDisabled("Structure updated");
                if (entry.touched.empty() && entry.removed.empty())
                    ImGui::TextDisabled("No map records changed");
                for (auto const object : entry.touched)
                {
                    ImGui::PushID(int(object.kind));
                    ImGui::PushID(object.index);
                    ImGui::BeginDisabled(!indexesCurrent || DukeEditor::ObjectSector(board, object) < 0);
                    if (ImGui::Selectable(DukeEditor::ObjectLabel(object).c_str()))
                        focusObject(object);
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    ImGui::PopID();
                }
                for (auto const object : entry.removed)
                    ImGui::TextDisabled("Previous %s", DukeEditor::ObjectLabel(object).c_str());
                ImGui::TreePop();
            }
            indexesCurrent &= !entry.structure;
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void tileCard(int tile, char const *name, bool entity, ImVec2 size)
{
    ImGui::PushID(tile);
    auto const pos = ImGui::GetCursorScreenPos();
    auto const draw = ImGui::GetWindowDrawList();
    bool const pressed = ImGui::InvisibleButton("tile", size);
    bool const hovered = ImGui::IsItemHovered();
    ImVec2 const end{pos.x + size.x, pos.y + size.y};
    draw->AddRectFilled(pos, end, ink, 5);
    drawTile(draw, tile, {pos.x + size.x * .5f, pos.y + (size.y - 17) * .5f}, {size.x - 10, size.y - 21});
    draw->AddRect(pos, end, brush == tile ? accent : hovered ? wallColor : gridMajor, 5);
    auto const textWidth = ImGui::CalcTextSize(name).x;
    draw->AddText({pos.x + std::max(4.f, (size.x - textWidth) * .5f), end.y - 16}, brush == tile ? accent : mute, name);
    if (pressed)
    {
        brush = tile;
        if (entity)
            switchTool(Tool::Sprite);
        else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            applyBrush(tile);
    }
    if (hovered)
        ImGui::SetTooltip("%s (%d)%s", tileName(tile), tile, entity ? "  |  Click to place" : "  |  Double-click to apply");
    ImGui::PopID();
}

void drawTileShelf()
{
    ImGui::SetNextItemWidth(std::min(226.f, ImGui::GetContentRegionAvail().x * .5f));
    if (ImGui::InputTextWithHint("##tileFilter", "Search name or tile ID", tileSearch, sizeof(tileSearch)))
        rebuildTileFilter();
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.kind != Kind::Sector && selection.kind != Kind::Wall && selection.kind != Kind::Sprite);
    if (ImGui::Button("Apply tile"))
        applyBrush(brush);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%d", brush);
    if (ImGui::BeginChild("Tile browser"))
    {
        ImVec2 const cardSize{59.f, 68.f};
        int const columns = std::max(1, int((ImGui::GetContentRegionAvail().x - 8.f) / (cardSize.x + ImGui::GetStyle().ItemSpacing.x)));
        ImGuiListClipper rows;
        rows.Begin((filteredTiles.size() + columns - 1) / columns, cardSize.y + ImGui::GetStyle().ItemSpacing.y);
        while (rows.Step())
            for (int row = rows.DisplayStart; row < rows.DisplayEnd; ++row)
                for (int col = 0; col < columns && unsigned(row * columns + col) < filteredTiles.size(); ++col)
                {
                    if (col) ImGui::SameLine();
                    int const tile = filteredTiles[row * columns + col];
                    tileCard(tile, std::to_string(tile).c_str(), false, cardSize);
                }
    }
    ImGui::EndChild();
}

void drawEntityShelf()
{
    struct PaletteItem { int tile; char const *name; };
    static PaletteItem const entities[] = {
        {1680, "Trooper"}, {2000, "Pigcop"}, {1820, "Commander"}, {28, "Shotgun"},
        {23, "RPG"}, {49, "Shells"}, {53, "First Aid"}, {60, "Keycard"},
        {1, "Effector"}, {2, "Activator"}, {3, "Touchplate"}, {9, "Respawn"},
        {1405, "Co-op start"}, {142, "Exit switch"}
    };
    if (ImGui::BeginChild("Entity browser"))
    {
        ImVec2 const size{87, 72};
        int const columns = std::max(1, int(ImGui::GetContentRegionAvail().x / (size.x + ImGui::GetStyle().ItemSpacing.x)));
        for (int i = 0; i < ARRAY_SSIZE(entities); ++i)
        {
            if (i % columns) ImGui::SameLine();
            tileCard(entities[i].tile, entities[i].name, true, size);
        }
    }
    ImGui::EndChild();
}

void gameIcon(GameTarget target)
{
    auto const &profile = DukeEditor::Target(target);
    ImU32 const colors[] = {mute, IM_COL32(255, 185, 97, 255), IM_COL32(246, 123, 87, 255),
        IM_COL32(130, 205, 157, 255), IM_COL32(239, 165, 84, 255), IM_COL32(126, 181, 234, 255),
        IM_COL32(192, 147, 241, 255)};
    auto const color = colors[unsigned(profile.target)];
    auto const spot = ImGui::GetCursorScreenPos();
    auto const size = ImGui::CalcTextSize(profile.icon);
    auto const draw = ImGui::GetWindowDrawList();
    float const height = ImGui::GetTextLineHeight();
    draw->AddRectFilled(spot, {spot.x + 24, spot.y + height}, ink, 3);
    draw->AddRect(spot, {spot.x + 24, spot.y + height}, color, 3);
    draw->AddText({spot.x + (24 - size.x) * .5f, spot.y}, color, profile.icon);
    ImGui::Dummy({24, height});
    ImGui::SetItemTooltip("%s", profile.label);
}

bool targetPicker(const char *label, GameTarget &target, bool inherit)
{
    bool changed = false;
    if (!ImGui::BeginCombo(label, DukeEditor::Target(target).label))
        return false;
    for (auto const &profile : DukeEditor::targetProfiles)
    {
        if (!inherit && profile.target == GameTarget::Inherit)
            continue;
        ImGui::PushID(profile.id);
        if (profile.target != GameTarget::Inherit)
        {
            gameIcon(profile.target);
            ImGui::SameLine();
        }
        if (ImGui::Selectable(profile.label, target == profile.target))
        {
            target = profile.target;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return changed;
}

bool projectCommand(DukeEditor::ProjectRequest const &request)
{
    std::string error;
    if (DukeEditor::ControlProject(request, error))
        return true;
    status(std::move(error), true);
    return false;
}

void selectPrefabBrush(std::string const &path)
{
    std::string error;
    if (DukeEditor::ReadPrefab(projectWorkspace.root, path, prefabBrush, error))
    {
        prefabBrushPath = path;
        switchTool(Tool::Prefab);
        status("Click in Plan to place " + basenameOf(path));
    }
    else
        status(error, true);
}

void drawProjectBrowser()
{
    using namespace DukeEditor;
    if (ImGui::Button("Project...")) showProjectDialog = true;
    if (projectWorkspace.root.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Open or create a project to browse its assets.");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Map settings...")) showMapSettingsDialog = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", projectWorkspace.project.name.c_str());
    static ImGuiTextFilter filter;
    ImGui::SetNextItemWidth(190);
    if (ImGui::InputTextWithHint("##assets", "Search assets", filter.InputBuf, sizeof(filter.InputBuf)))
        filter.Build();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshProject();
    if (!selectedAsset.empty() && IsMapAsset(selectedAsset))
    {
        ImGui::SameLine();
        if (ImGui::Button("Set entry map"))
        {
            ProjectRequest request{ProjectAction::Settings};
            request.entry = selectedAsset;
            request.setEntry = true;
            projectCommand(request);
        }
    }
    if (projectWorkspace.modern && IsPrefabAsset(selectedAsset))
    {
        ImGui::SameLine();
        if (ImGui::Button("Place Prefab"))
            selectPrefabBrush(selectedAsset);
    }
    if (ImGui::BeginChild("Project asset browser"))
        for (auto const &item : projectWorkspace.assets)
        {
            auto const &asset = item.asset;
            if (!filter.PassFilter(asset.path.c_str()))
                continue;
            ImGui::PushID(asset.path.c_str());
            auto const target = (!sourcePath.empty() && sourcePath == projectWorkspace.root + "/" + asset.path)
                ? projectWorkspace.effectiveTarget : item.target;
            gameIcon(target);
            ImGui::SameLine();
            auto label = asset.path;
            if (asset.directory) label += "/";
            if (asset.path == projectWorkspace.project.entry) label += "  (Entry)";
            if (!asset.directory && IsPrefabAsset(asset.path)) label += "  (Prefab)";
            if (ImGui::Selectable(label.c_str(), selectedAsset == asset.path, ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedAsset = asset.path;
                if (!asset.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (IsMapAsset(asset.path))
                    {
                        droppedMap = projectWorkspace.root + "/" + asset.path;
                        queued = Action::Open;
                    }
                    else if (projectWorkspace.modern && IsPrefabAsset(asset.path))
                        selectPrefabBrush(asset.path);
                }
            }
            ImGui::PopID();
        }
    ImGui::EndChild();
}

void changeCompatibilitySettings(DukeEditor::ProjectRequest const &request)
{
    using namespace DukeEditor;
    bool const map = request.action == ProjectAction::MapSettings;
    bool const enabled = request.modern == 1 || (map && request.modern == -1 && projectWorkspace.project.modern);
    if (enabled && !(map ? projectWorkspace.modern : projectWorkspace.project.modern))
    {
        pendingModernSettings = request;
        ImGui::OpenPopup("Enable Modern Mode?");
    }
    else
        projectCommand(request);
}

void drawModernCompatibilityWarning()
{
    ImGui::SetNextWindowSize({450, 0}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Enable Modern Mode?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("%s", DukeEditor::modernCompatibilityWarning);
    ImGui::Spacing();
    if (ImGui::Button("Enable Modern Mode"))
    {
        projectCommand(pendingModernSettings);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawComponentDialogs()
{
    if (std::exchange(showComponentDialog, false))
        ImGui::OpenPopup("New Script Component");
    if (std::exchange(showPrefabDialog, false))
        ImGui::OpenPopup("Create Prefab");
    ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Script Component", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Project path", componentPath, sizeof(componentPath));
        bool const sameDraft = dialogRevision == nextRevision;
        ImGui::BeginDisabled(!sameDraft);
        if (ImGui::Button("Create and Attach"))
        {
            pendingComponent.path = componentPath;
            if (projectCommand(pendingComponent))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!sameDraft) ImGui::TextWrapped("The map changed. Select the object again.");
        else if (errorMessage) ImGui::TextWrapped("%s", notice.c_str());
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Create Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        auto const count = unsigned(pendingPrefab.sprites.size());
        ImGui::Text("%u selected sprite%s", count, count == 1 ? "" : "s");
        ImGui::InputText("Project path", prefabPath, sizeof(prefabPath));
        bool const sameDraft = dialogRevision == nextRevision;
        ImGui::BeginDisabled(!sameDraft);
        if (ImGui::Button("Save Prefab"))
        {
            pendingPrefab.path = prefabPath;
            if (projectCommand(pendingPrefab))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!sameDraft) ImGui::TextWrapped("The map changed. Select the sprites again.");
        else if (errorMessage) ImGui::TextWrapped("%s", notice.c_str());
        ImGui::EndPopup();
    }
}

void drawProjectDialogs()
{
    using namespace DukeEditor;
    if (std::exchange(showProjectDialog, false))
        ImGui::OpenPopup("Project settings");
    if (std::exchange(showMapSettingsDialog, false))
        ImGui::OpenPopup("Map settings");
    ImGui::SetNextWindowSize({560, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Project settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputTextWithHint("Folder", "Project folder path", projectPath, sizeof(projectPath));
        if (ImGui::Button("Open folder"))
        {
            ProjectRequest request{ProjectAction::Open};
            request.path = projectPath;
            projectCommand(request);
        }
        ImGui::SameLine();
        if (ImGui::Button("Create project"))
        {
            ProjectRequest request{ProjectAction::Create};
            request.path = projectPath;
            projectCommand(request);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close project")) projectCommand(ProjectRequest{ProjectAction::Close});
        ImGui::InputTextWithHint("ZIP", "Project archive path", projectArchive, sizeof(projectArchive));
        if (ImGui::Button("Import ZIP into folder"))
        {
            ProjectRequest request{ProjectAction::Import};
            request.path = projectArchive;
            request.destination = projectPath;
            projectCommand(request);
        }
        ImGui::SameLine();
        if (ImGui::Button("Export ZIP"))
        {
            ProjectRequest request{ProjectAction::Export};
            request.path = projectArchive;
            projectCommand(request);
        }
        if (!projectWorkspace.root.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", projectWorkspace.root.c_str());
            ProjectRequest request{ProjectAction::Settings};
            auto project = projectWorkspace.project;
            char name[129];
            Bstrncpyz(name, project.name.c_str(), sizeof(name));
            if (ImGui::InputText("Project name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
                request.name = name;
            if (targetPicker("Base game", project.target, false)) request.target = project.target;
            if (ImGui::Checkbox("Enable Modern Mode", &project.modern)) request.modern = project.modern;
            if (!request.name.empty() || request.target != GameTarget::Inherit || request.modern >= 0)
                changeCompatibilitySettings(request);
            drawModernCompatibilityWarning();
            ImGui::TextDisabled("Entry map %s", project.entry.empty() ? "(choose in the asset browser)" : project.entry.c_str());
            ImGui::InputTextWithHint("Asset file", "File to copy into the project", projectAssetPath, sizeof(projectAssetPath));
            if (ImGui::Button("Add asset"))
            {
                ProjectRequest add{ProjectAction::AddAsset};
                add.path = projectAssetPath;
                if (projectCommand(add)) projectAssetPath[0] = 0;
            }
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", notice.c_str());
        if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize({500, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Map settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        auto const &settings = projectWorkspace.map;
        ProjectRequest request{ProjectAction::MapSettings};
        request.target = settings.target;
        request.modern = settings.modern;
        bool changed = targetPicker("Base game", request.target, true);
        request.setTarget = changed;
        char const *const modes[] = {"Inherit", "Off", "On"};
        int modern = settings.modern + 1;
        if (ImGui::Combo("Modern Mode", &modern, modes, ARRAY_SIZE(modes)))
        {
            request.modern = modern - 1;
            changed = true;
        }
        if (changed) changeCompatibilitySettings(request);
        drawModernCompatibilityWarning();
        gameIcon(projectWorkspace.effectiveTarget);
        ImGui::SameLine();
        ImGui::TextUnformatted(Target(projectWorkspace.effectiveTarget).label);
        if (projectWorkspace.modern)
        {
            static char script[BMAX_PATH];
            ImGui::InputTextWithHint("Object script", "Project-relative JS or TS path", script, sizeof(script));
            if (IsScriptAsset(selectedAsset))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Use selected asset")) Bstrncpyz(script, selectedAsset.c_str(), sizeof(script));
            }
            bool const valid = selection.kind == Kind::Sector || selection.kind == Kind::Wall || selection.kind == Kind::Sprite;
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("Attach to selected object"))
            {
                ProjectRequest attach{ProjectAction::Attach};
                attach.kind = selection.kind == Kind::Sector ? "sector" : selection.kind == Kind::Wall ? "wall" : "sprite";
                attach.index = selection.index;
                attach.script = script;
                projectCommand(attach);
            }
            ImGui::EndDisabled();
            for (auto const &binding : settings.scripts)
            {
                ImGui::PushID(&binding);
                if (binding.index >= 0)
                    ImGui::Text("%s %d  %s", binding.kind.c_str(), binding.index, binding.script.c_str());
                else
                    ImGui::Text("%s (unassigned)  %s", binding.kind.c_str(), binding.script.c_str());
                ImGui::SameLine();
                bool const removed = ImGui::SmallButton("Detach");
                ImGui::PopID();
                if (removed)
                {
                    ProjectRequest detach{ProjectAction::Detach};
                    detach.kind = binding.kind;
                    detach.index = binding.index;
                    detach.script = binding.script;
                    projectCommand(detach);
                    break;
                }
            }
        }
        static char exportPath[BMAX_PATH * 2];
        ImGui::InputTextWithHint("MAP", "Destination for a classic map export", exportPath, sizeof(exportPath));
        if (ImGui::Button("Export MAP"))
        {
            ProjectRequest exportMap{ProjectAction::ExportMap};
            exportMap.path = exportPath;
            projectCommand(exportMap);
        }
        ImGui::TextWrapped("%s", notice.c_str());
        if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void drawAssets()
{
    ImGui::TextUnformatted("Assets");
    if (ImGui::BeginTabBar("Asset groups"))
    {
        if (ImGui::BeginTabItem("Tiles"))
        {
            drawTileShelf();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Entities"))
        {
            drawEntityShelf();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Project"))
        {
            drawProjectBrowser();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void drawTools()
{
    if (choice("Select  Q", tool == Tool::Select)) switchTool(Tool::Select);
    ImGui::SetItemTooltip("In Plan, Shift-click selects sectors and Ctrl-click selects walls.");
    ImGui::SameLine();
    if (choice("Room  R", tool == Tool::Room)) switchTool(Tool::Room);
    ImGui::SetItemTooltip("Click corners, then click the first corner or press Enter to close the sector.");
    ImGui::SameLine();
    if (choice("Sprite  E", tool == Tool::Sprite)) switchTool(Tool::Sprite);
    ImGui::SetItemTooltip("Pick an entity or tile, then click in a sector to place it.");
    ImGui::SameLine();
    if (choice("Player Start  T", tool == Tool::Start)) switchTool(Tool::Start);
    ImGui::SetItemTooltip("Click in a sector to place Duke's start.");
    ImGui::SameLine();
    if (choice("Batch Select", batchSelection))
    {
        batchSelection = !batchSelection;
        group.clear();
        switchTool(Tool::Select);
    }
    ImGui::SetItemTooltip("Click sprites or Ctrl-click walls to toggle them in the selection.");
    ImGui::SameLine(0, 14);
    ImGui::SetNextItemWidth(92);
    auto const label = grid ? std::to_string(grid) : "Off";
    if (ImGui::BeginCombo("Snap", label.c_str()))
    {
        for (int scale : {0, 32, 64, 128, 256, 512, 1024})
            if (ImGui::Selectable(scale ? std::to_string(scale).c_str() : "Off", grid == scale))
                grid = scale;
        ImGui::EndCombo();
    }
    if (tool == Tool::Pillar || tool == Tool::Catwalk || tool == Tool::Cut || tool == Tool::Prefab)
    {
        ImGui::SameLine();
        ImGui::TextColored(accentText, "%s", tool == Tool::Pillar ? "Draw pillar" : tool == Tool::Catwalk ?
                           "Place catwalk" : tool == Tool::Prefab ? "Place prefab" : "Pick section cut");
    }
}

void keyboardShortcuts()
{
    auto const &io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || drag)
        return;
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    if (!waitForF5Release && pressed(ImGuiKey_F5))
        queued = Action::Play;
    else if (io.KeyCtrl || io.KeySuper)
    {
        if (pressed(ImGuiKey_O)) queued = Action::Open;
        if (pressed(ImGuiKey_N)) queued = Action::New;
        if (pressed(ImGuiKey_S)) queued = io.KeyShift ? Action::SaveAs : Action::Save;
        if (pressed(ImGuiKey_Z))
        {
            if (io.KeyShift) useHistory(redo, undo);
            else useHistory(undo, redo);
        }
        if (pressed(ImGuiKey_Y)) useHistory(redo, undo);
    }
    else if (!cameraLook)
    {
        if (pressed(ImGuiKey_Q)) switchTool(Tool::Select);
        if (pressed(ImGuiKey_R)) switchTool(Tool::Room);
        if (pressed(ImGuiKey_E)) switchTool(Tool::Sprite);
        if (pressed(ImGuiKey_T)) switchTool(Tool::Start);
        if (pressed(ImGuiKey_F)) frameScene();
        if (pressed(ImGuiKey_Delete)) deleteSelection();
        if (pressed(ImGuiKey_Escape)) outline.clear();
        if ((tool == Tool::Room || tool == Tool::Pillar) && !outline.empty() && pressed(ImGuiKey_Enter)) finishRoom();
    }
}

void drawWorkspace()
{
    auto const context = ImGui::GetCurrentContext();
    if (!context)
        return;
    if (context != styledContext || ImGui::GetFrameCount() < lastGuiFrame)
    {
        releaseThumbnails(false);
        styleEditor();
        editorInput();
    }
    engineBeginImGuiFrame();
    lastGuiFrame = ImGui::GetFrameCount();
    keyboardShortcuts();

    auto const vp = ImGui::GetMainViewport();
    auto const pos = vp->Pos;
    float const gap = 8, width = vp->Size.x - 2*gap, height = vp->Size.y;
    float const left = clamp(width * .189f, 144.f, 224.f);
    float const right = clamp(width * .271f, 199.f, 310.f);
    float const middle = width - left - right - gap * 2;
    float const header = 77, footer = 34, shelf = clamp(height * .25f, 127.f, 210.f);
    float const sceneHeight = std::max(height - header - footer - shelf - gap*5, 94.f);
    float const top = pos.y + header + gap*2;
    float const shelfY = top + sceneHeight + gap;
    float const footerY = shelfY + shelf + gap;

    nextPanel("Toolbar", {pos.x + gap, pos.y + gap}, {width, header}, ImGuiWindowFlags_NoScrollbar);
    drawToolbar(width);
    drawTools();
    ImGui::End();

    nextPanel("Hierarchy", {pos.x + gap, top}, {left, sceneHeight}, ImGuiWindowFlags_NoScrollbar);
    drawHierarchy();
    ImGui::End();

    nextPanel("Scene", {pos.x + left + gap*2, top}, {middle, sceneHeight},
              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawScene();
    ImGui::End();

    nextPanel("Inspector", {pos.x + left + middle + gap*3, top}, {right, sceneHeight + shelf + gap},
              ImGuiWindowFlags_NoScrollbar);
    drawInspector();
    ImGui::End();

    nextPanel("Assets", {pos.x + gap, shelfY}, {left + middle + gap, shelf}, ImGuiWindowFlags_NoScrollbar);
    drawAssets();
    ImGui::End();

    nextPanel("Map status", {pos.x + gap, footerY}, {width, footer}, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
    ImGui::TextColored(errorMessage ? ImVec4(1, .55f, .45f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                       "%s", notice.c_str());
    if (width > 740 && !errorMessage && ImGui::CalcTextSize(notice.c_str()).x < width - 372)
    {
        ImGui::SameLine(width - 348);
        ImGui::TextDisabled("Duke3D v7  |  %u sectors  %u walls  %u sprites",
                            unsigned(board.sectors.size()), unsigned(board.walls.size()), unsigned(board.sprites.size()));
    }
    drawDialogs();
    drawProjectDialogs();
    drawComponentDialogs();
    ImGui::End();
    drawPreset();
    drawStructureTools();
    drawMapCheck();
    drawChangeHistory();

    videoClearScreen(0);
    if (view3D)
        render3D();
    videoNextPage();

    auto const action = std::exchange(queued, Action::Idle);
    if (action != Action::Idle)
        applyAction(action);
}

} // namespace

bool MapEditor_CanOpen()
{
    return !VOLUMEONE && !g_forceHeadless && !nogl && numplayers < 2 && ud.multimode < 2;
}

void MapEditor_Request(char const *path)
{
    if (!requested || initialMap.empty())
        initialMap = path ? path : "";
    requested = true;
}

bool MapEditor_Requested()
{
    return requested;
}

bool MapEditor_Update(bool windowClosing)
{
    if (live.mode() != LiveMode::Offline)
        pollLive();
    pollBuildTools();
    if (waitForF5Release && !SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F5])
    {
        waitForF5Release = false;
        KB_ClearKeyDown(sc_F5);
    }
    if (requested)
    {
        requested = false;
        if (!MapEditor_CanOpen() || !beginEditor())
        {
            LOG_F(ERROR, "Could not open the map editor. It requires a local Duke3D game with OpenGL.");
            G_AddUserQuote("Map Editor could not open");
            return false;
        }
    }
    if (mode == Mode::Play)
    {
        if (stopPlay || windowClosing || (!waitForF5Release && KB_KeyPressed(sc_F5)) ||
            (ud.gm & MODE_EOL) || !(ud.gm & MODE_GAME))
        {
            stopPlay = false;
            returnToEditor();
        }
        else
        {
            sendLivePointer();
            return false;
        }
    }
    if (mode != Mode::Edit)
        return false;
    if (windowClosing)
        queued = Action::Quit;
    if (engineFPSLimit())
        drawWorkspace();
    else
        SDL_Delay(1);
    return true;
}

bool MapEditor_Playing()
{
    return mode == Mode::Play;
}

const char *MapEditor_PlaytestName()
{
    return mode == Mode::Play ? title.c_str() : nullptr;
}

const char *MapEditor_PlaytestSource()
{
    return mode == Mode::Play && !sourcePath.empty() ? sourcePath.c_str() : nullptr;
}

void MapEditor_StopPlaytest()
{
    stopPlay = true;
}

void MapEditor_DrawPlaytestHint()
{
    if (mode == Mode::Play && !(ud.gm & MODE_MENU) && timerGetTicks() - startPlayMessage < 6000)
        gametextpalbits(160, 8, "PLAYTEST   F5 TO EDIT", 0, 0, 2 + 8 + 16);
}

void MapEditor_Shutdown()
{
    live.leave();
    cleanPlaytest();
    if (mode == Mode::Edit && ImGui::GetCurrentContext())
    {
        releaseThumbnails(styledContext == ImGui::GetCurrentContext());
        gameInput();
    }
}

namespace DukeEditor {

WorkspaceState GetWorkspace()
{
    WorkspaceState state;
    if (mode == Mode::Menu)
        return state;
    state.board = &board;
    state.session = &live;
    state.name = title.c_str();
    state.path = sourcePath.c_str();
    state.revision = nextRevision;
    state.tileLimit = Target(projectWorkspace.effectiveTarget).tiles;
    state.playing = mode == Mode::Play;
    state.dirty = isDirty();
    state.busy = !editable() || bool(drag);
    state.undoable = canUndo();
    state.redoable = canRedo();
    state.perspective = view3D;
    state.section = sectionView;
    state.presence = controlPointer;
    state.presence.kind = selection.kind;
    state.presence.index = selection.index;
    state.changes = online() ? &live.changes() : &localChanges;
    return state;
}

std::vector<MapIssue> CheckWorkspaceMap() { return mode == Mode::Edit ? checkDraft() : std::vector<MapIssue>{}; }

bool ControlWorkspace(WorkspaceRequest const &request, std::string &error)
{
    auto const action = request.action;
    if (action == WorkspaceAction::Enter)
    {
        if (mode != Mode::Menu)
            return true;
        if (!MapEditor_CanOpen())
        {
            error = "The map editor requires a local registered Duke3D game with SDL2 and OpenGL.";
            return false;
        }
        if (beginEditor())
            return true;
        error = "Could not open the editor with the current graphics settings.";
        return false;
    }
    if (mode == Mode::Play && action == WorkspaceAction::Stop)
    {
        stopPlay = false;
        returnToEditor();
        return true;
    }
    if (mode != Mode::Edit)
    {
        error = mode == Mode::Play ? "Return to editing first." : "Open the map editor first.";
        return false;
    }
    if (action == WorkspaceAction::Leave)
    {
        live.leave();
        status("Live session closed");
        return true;
    }
    if (!editable() || drag)
    {
        error = "Wait for the current edit or connection to finish.";
        return false;
    }
    if ((action == WorkspaceAction::New || action == WorkspaceAction::Open) && live.mode() == LiveMode::Joined)
    {
        error = "Leave the live session to open another draft.";
        return false;
    }
    if (isDirty() && !request.discard &&
        (action == WorkspaceAction::New || action == WorkspaceAction::Open ||
         action == WorkspaceAction::Join || action == WorkspaceAction::Close))
    {
        error = "The current draft has unsaved changes. Save it first or explicitly discard it.";
        return false;
    }
    switch (action)
    {
        case WorkspaceAction::New:
            if (setDocument(NewBoard(), "Untitled.map", {}))
            {
                status("Starter room ready");
                return true;
            }
            break;
        case WorkspaceAction::Open:
            if (request.path.empty())
            {
                error = "Supply the map path.";
                return false;
            }
            if (openMap(request.path, request.target))
                return true;
            break;
        case WorkspaceAction::Save:
        {
            auto path = request.path.empty() ? sourcePath : absolutePath(request.path);
            if (path.empty())
            {
                error = "Supply a filename for this draft.";
                return false;
            }
            if (!isMap(path)) path += ".map";
            if (path != sourcePath && !request.overwrite && buildvfs_exists(path.c_str()))
            {
                error = "The destination already exists. Choose another path or request overwrite.";
                return false;
            }
            if (saveMap(path))
                return true;
            break;
        }
        case WorkspaceAction::Undo:
        case WorkspaceAction::Redo:
        {
            if (online())
            {
                if (useLiveHistory(action == WorkspaceAction::Redo))
                    return true;
                break;
            }
            auto &history = action == WorkspaceAction::Undo ? undo : redo;
            if (history.empty())
            {
                error = "The requested history is empty.";
                return false;
            }
            auto const previous = nextRevision;
            useHistory(history, action == WorkspaceAction::Undo ? redo : undo);
            controlPointer = {};
            if (nextRevision != previous)
                return true;
            break;
        }
        case WorkspaceAction::Play:
            enterPlaytest(request.playFrom);
            if (mode == Mode::Play)
                return true;
            break;
        case WorkspaceAction::Close:
            leaveEditor();
            return true;
        case WorkspaceAction::Plan:
        case WorkspaceAction::Perspective:
            view3D = action == WorkspaceAction::Perspective;
            sectionView = false;
            if (view3D)
            {
                tool = Tool::Select;
                if (cameraSector < 0) frameScene();
            }
            return true;
        case WorkspaceAction::Section:
            view3D = false;
            sectionView = true;
            switchTool(Tool::Select);
            frameScene();
            return true;
        case WorkspaceAction::Frame:
            frameScene();
            return true;
        case WorkspaceAction::Host:
            if (online())
            {
                error = "Leave the current live session first.";
                return false;
            }
            if (live.host(request.port ? request.port : editorPort, request.name.empty() ? participantName : request.name,
                          editionIdentity(), artLimit, board))
            {
                livePrefabBindings.clear();
                liveRevision = live.revision();
                livePresentation = live.presentation();
                listenPort = request.port ? request.port : editorPort;
                status("Live session listening on UDP port " + std::to_string(listenPort));
                return true;
            }
            error = live.takeNotice();
            return false;
        case WorkspaceAction::Join:
            if (request.address.empty() || online())
            {
                error = online() ? "Leave the current live session first." : "Supply the host address.";
                return false;
            }
            if (live.join(request.address, request.name.empty() ? participantName : request.name, editionIdentity()))
            {
                Bstrncpyz(hostAddress, request.address.c_str(), sizeof(hostAddress));
                status("Connecting to " + request.address);
                return true;
            }
            error = live.takeNotice();
            return false;
        case WorkspaceAction::Stop:
            return true;
        case WorkspaceAction::BuildTools:
            return openBuildTools(request.port, error);
        default:
            error = "Unknown workspace action.";
            return false;
    }
    error = notice;
    return false;
}

bool PointInWorkspace(Presence const &pointer, bool setSelection, std::string &error)
{
    if (mode != Mode::Edit || drag)
    {
        error = "Point to the draft while it is open for editing.";
        return false;
    }
    controlPointer = pointer;
    if (setSelection)
        selection = {pointer.kind, pointer.index};
    return true;
}

bool EditWorkspace(uint64_t expectedRevision, Board next, Presence const &pointer, std::string &error,
                   std::string const &label, bool strict)
{
    if (mode != Mode::Edit || !editable() || drag)
    {
        error = "Wait until the draft is ready for editing.";
        return false;
    }
    if (expectedRevision != nextRevision)
    {
        error = "Inspect the current draft revision before editing.";
        return false;
    }
    Snapshot previous{std::move(board), revision, projectWorkspace.map.scripts};
    board = std::move(next);
    if (!commit(std::move(previous), label.c_str(), strict, true))
    {
        error = notice;
        return false;
    }
    selection = {pointer.kind, pointer.index};
    controlPointer = pointer;
    status("Assistant updated the draft");
    return true;
}

ProjectWorkspace const &GetProjectWorkspace() { return projectWorkspace; }

bool ControlProject(ProjectRequest const &request, std::string &error)
{
    if (mode != Mode::Edit || drag)
    {
        error = "Open the map editor to manage its project.";
        return false;
    }
    auto &workspace = projectWorkspace;
    auto const path = absolutePath(request.path);
    auto const &root = workspace.root;
    switch (request.action)
    {
        case ProjectAction::Create:
        {
            ProjectSettings options;
            options.name = request.name.empty() ? basenameOf(path) : request.name;
            options.target = request.target == GameTarget::Inherit ? RunningTarget() : request.target;
            options.modern = request.modern > 0;
            if (path.empty() || !CreateProject(path, options, error))
                break;
            if (!loadProject(path, error))
                return false;
            status("Created project " + options.name);
            return true;
        }
        case ProjectAction::Open:
            if (path.empty() || !loadProject(path, error))
                break;
            status("Opened project " + workspace.project.name);
            return true;
        case ProjectAction::Close:
            removesearchpaths_withuser(projectSearchPath);
            workspace.root.clear();
            workspace.project = {};
            livePrefabBindings.clear();
            selectedAsset.clear();
            if (tool == Tool::Prefab) switchTool(Tool::Select);
            refreshProject();
            status("Project closed");
            return true;
        case ProjectAction::Import:
        {
            auto const destination = absolutePath(request.destination);
            if (path.empty() || destination.empty() || !ImportProject(path, destination, error) || !loadProject(destination, error))
                break;
            status("Imported project " + workspace.project.name);
            return true;
        }
        case ProjectAction::Export:
            if (root.empty() || path.empty())
                break;
            if (isDirty() && !sourcePath.empty() && sourcePath.compare(0, root.size() + 1, root + "/") == 0)
            {
                error = "Save the current project map before exporting.";
                return false;
            }
            if (!ExportProject(root, path, request.overwrite, error))
                return false;
            status("Exported project " + workspace.project.name);
            return true;
        case ProjectAction::AddAsset:
        {
            if (root.empty() || path.empty())
                break;
            auto const relative = request.destination.empty() ? basenameOf(path) : request.destination;
            if (!ValidAssetPath(relative) || !Bstrcasecmp(relative.c_str(), projectManifest))
            {
                error = "Choose a project-relative asset path.";
                return false;
            }
            if (!CopyProjectAsset(path, root + "/" + relative, request.overwrite, error))
                return false;
            refreshProject();
            status("Imported " + relative);
            return true;
        }
        case ProjectAction::Settings:
        {
            if (root.empty())
                break;
            auto options = workspace.project;
            if (!request.name.empty()) options.name = request.name;
            if (request.target != GameTarget::Inherit) options.target = request.target;
            if (request.modern >= 0) options.modern = request.modern != 0;
            if (request.setEntry)
            {
                if (!request.entry.empty() && (!ValidAssetPath(request.entry) ||
                    !IsMapAsset(request.entry) || !buildvfs_exists((root + "/" + request.entry).c_str())))
                {
                    error = "Choose a map from the project assets.";
                    return false;
                }
                options.entry = request.entry;
            }
            if (!WriteProject(root, options, error))
                return false;
            workspace.project = std::move(options);
            refreshProject();
            status("Project settings saved");
            return true;
        }
        case ProjectAction::MapSettings:
            if (request.setTarget) workspace.map.target = request.target;
            if (request.modern >= -1) workspace.map.modern = request.modern;
            mapSettingsDirty = true;
            updateMapSettings();
            status("Map settings updated");
            return true;
        case ProjectAction::Attach:
        case ProjectAction::Detach:
        case ProjectAction::CreateComponent:
        {
            if (!editable())
            {
                error = "Wait until the draft is ready for editing.";
                return false;
            }
            auto const scriptRoot = root.empty() && !sourcePath.empty() ? directoryOf(sourcePath) : root;
            bool const creating = request.action == ProjectAction::CreateComponent;
            bool const attaching = request.action != ProjectAction::Detach;
            auto const &script = creating ? request.path : request.script;
            Selection const object{componentKind(request.kind), request.index};
            if (attaching)
            {
                auto const assetPath = scriptRoot + "/" + script;
                if (ObjectSector(board, object) < 0 || !workspace.modern || scriptRoot.empty() ||
                    (creating && root.empty()) || !ValidAssetPath(script) || !IsScriptAsset(script) ||
                    (!creating && (!buildvfs_exists(assetPath.c_str()) || buildvfs_isdir(assetPath.c_str()))))
                {
                    error = "Enable Modern Mode and choose a map object and a project-relative JS or TS component.";
                    return false;
                }
            }
            auto scripts = workspace.map.scripts;
            auto const binding = std::find_if(scripts.begin(), scripts.end(), [&](auto const &current) {
                return current.kind == request.kind && current.index == request.index && current.script == script;
            });
            if (attaching)
            {
                if (binding != scripts.end())
                    return true;
                if (scripts.size() >= maxScriptAttachments)
                {
                    error = "This map has reached its script component limit.";
                    return false;
                }
                if (creating && !CreateComponentAsset(root, script, error))
                    return false;
                scripts.push_back({request.kind, script, request.index});
            }
            else if (binding != scripts.end())
                scripts.erase(binding);
            else
                return true;
            setComponents(std::move(scripts), object, attaching ? "Attach script component" : "Remove script component");
            if (creating)
                refreshProject();
            return true;
        }
        case ProjectAction::SavePrefab:
        {
            if (!workspace.modern || root.empty() || !editable())
            {
                error = "Open an editable project with Modern Mode enabled to create a prefab.";
                return false;
            }
            auto const sprites = !request.sprites.empty() ? request.sprites :
                request.index >= 0 ? std::vector<int>{request.index} : selectedPrefabSprites();
            auto relative = request.path;
            if (relative.empty())
            {
                error = "Enter a project-relative filename for the prefab.";
                return false;
            }
            if (!IsPrefabAsset(relative))
                relative += prefabSuffix;
            Prefab prefab;
            if (!CapturePrefab(board, workspace.map, sprites, workspace.effectiveTarget, prefab, error) ||
                !WritePrefab(root, relative, prefab, request.overwrite, error))
                return false;
            refreshProject();
            selectedAsset = relative;
            status("Created " + relative);
            return true;
        }
        case ProjectAction::PlacePrefab:
        {
            Prefab prefab;
            if (!workspace.modern || root.empty())
            {
                error = "Enable Modern Mode in a project to place prefabs.";
                return false;
            }
            if (!ReadPrefab(root, request.path, prefab, error))
                return false;
            return placePrefab(prefab, request.path, request.position, request.sector, request.useFloorLayer,
                               request.floorLayer, error);
        }
        case ProjectAction::ExportMap:
        {
            if (path.empty())
                break;
            auto const target = request.target == GameTarget::Inherit ? workspace.effectiveTarget : request.target;
            auto destination = path;
            if (!isMap(destination)) destination += ".map";
            if (!request.overwrite && buildvfs_exists(destination.c_str()))
            {
                error = "The map export already exists.";
                return false;
            }
            if (!WriteBoard(destination, board, Target(target).tiles, error))
                return false;
            status("Exported a classic MAP for " + std::string(Target(target).label));
            return true;
        }
    }
    if (error.empty())
        error = "Supply the project folder and file paths for this action.";
    return false;
}

} // namespace DukeEditor

#else

static bool requested;

bool MapEditor_CanOpen() { return false; }
void MapEditor_Request(char const *) { requested = true; }
bool MapEditor_Requested() { return requested; }
bool MapEditor_Update(bool) { return false; }
bool MapEditor_Playing() { return false; }
const char *MapEditor_PlaytestSource() { return nullptr; }
void MapEditor_StopPlaytest() {}
void MapEditor_DrawPlaytestHint() {}
void MapEditor_Shutdown() {}

namespace DukeEditor {

WorkspaceState GetWorkspace() { return {}; }
std::vector<MapIssue> CheckWorkspaceMap() { return {}; }
bool ControlWorkspace(WorkspaceRequest const &, std::string &error)
{
    error = "The map editor requires SDL2 and OpenGL.";
    return false;
}
bool PointInWorkspace(Presence const &, bool, std::string &error)
{
    error = "The map editor requires SDL2 and OpenGL.";
    return false;
}
bool EditWorkspace(uint64_t, Board, Presence const &, std::string &error, std::string const &, bool)
{
    error = "The map editor requires SDL2 and OpenGL.";
    return false;
}
ProjectWorkspace const &GetProjectWorkspace()
{
    static const ProjectWorkspace unavailable;
    return unavailable;
}
bool ControlProject(ProjectRequest const &, std::string &error)
{
    error = "The map editor requires SDL2 and OpenGL.";
    return false;
}

} // namespace DukeEditor

#endif
