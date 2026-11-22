#include "editorproject.h"

#include "duke3d.h"
#include "editorboard.h"
#include "grpscan.h"
#include "sjson.h"
#include "vbjs.h"

#include <cstdlib>
#ifdef _WIN32
# include "windows_inc.h"
#endif

namespace DukeEditor {
namespace {

GameTarget launchTarget = GameTarget::Inherit;
std::string launchFolder, launchEntry, temporaryFolder, originalDirectory;
std::vector<std::string> options;
std::vector<const char *> launchArgs;
bool allowScripts;

bool looksLikeZip(const char *path)
{
    auto const ext = Bstrrchr(path, '.');
    return ext && !Bstrcasecmp(ext, ".zip");
}

bool regularAsset(std::string const &path)
{
    return buildvfs_exists(path.c_str()) && !buildvfs_isdir(path.c_str());
}

bool profileMatches(TargetProfile const &profile, grpinfo_t const &group)
{
    if (uint32_t(group.crcval) == profile.crc)
        return true;
    return profile.target == GameTarget::DukeAtomic &&
        (uint32_t(group.crcval) == uint32_t(DUKEWT_CRC) || uint32_t(group.crcval) == uint32_t(DUKEPP_CRC));
}

void cleanup()
{
    if (!originalDirectory.empty())
        buildvfs_chdir(originalDirectory.c_str());
    if (!temporaryFolder.empty())
        RemoveTemporaryProject(temporaryFolder);
}

} // namespace

bool PrepareProjectLaunch(int &argc, const char *const *&argv)
{
    int projectArg = -1, argumentCount = 1, mapArg = -1;
    bool editor = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!Bstrcmp(argv[i], "--allow-project-scripts"))
            allowScripts = true;
        if (!Bstrcmp(argv[i], "--editor"))
            editor = true;
        if (!Bstrcmp(argv[i], "--project-map"))
        {
            if (i + 1 >= argc)
            {
                LOG_F(ERROR, "--project-map needs a project-relative MAP path.");
                return false;
            }
            mapArg = i++;
            continue;
        }
        if (!Bstrcmp(argv[i], "--project"))
        {
            if (i + 1 >= argc)
            {
                LOG_F(ERROR, "--project needs a folder or ZIP.");
                return false;
            }
            projectArg = i;
            argumentCount = 2;
            ++i;
        }
        else if (projectArg < 0 && argv[i][0] != '-' && looksLikeZip(argv[i]) && IsProjectArchive(argv[i]))
            projectArg = i;
    }
    if (projectArg < 0 && mapArg >= 0)
    {
        LOG_F(ERROR, "--project-map requires a project folder or ZIP.");
        return false;
    }
    if (projectArg < 0 && !allowScripts)
        return true;

    auto fail = [](std::string const &error) {
        LOG_F(ERROR, "%s", error.c_str());
        cleanup();
        temporaryFolder.clear();
        return false;
    };
    if (projectArg >= 0)
    {
        std::string error;
        auto const input = argv[projectArg + argumentCount - 1];
        if (looksLikeZip(input))
        {
            temporaryFolder = ImportTemporaryProject(input, error);
            if (temporaryFolder.empty())
                return fail(error);
            atexit(cleanup);
            launchFolder = temporaryFolder;
        }
        else
        {
#ifdef _WIN32
            char path[BMAX_PATH];
            auto const length = GetFullPathNameA(input, sizeof(path), path, nullptr);
            if (!length || length >= sizeof(path))
                return fail("Could not resolve the project folder path.");
            launchFolder = path;
#else
            auto const path = realpath(input, nullptr);
            if (!path)
                return fail("Could not resolve the project folder path.");
            launchFolder = path;
            free(path);
#endif
        }
#ifdef _WIN32
        std::replace(launchFolder.begin(), launchFolder.end(), '\\', '/');
#endif
        while (launchFolder.size() > 1 && launchFolder.back() == '/' &&
               !(launchFolder.size() == 3 && launchFolder[1] == ':'))
            launchFolder.pop_back();

        ProjectSettings project;
        if (!ReadProject(launchFolder, project, error))
            return fail(error);
        if (mapArg >= 0)
        {
            project.entry = argv[mapArg + 1];
            if (!ValidAssetPath(project.entry) || !IsMapAsset(project.entry))
                return fail("Choose a project-relative entry MAP.");
        }
        if (project.entry.empty())
            for (auto const &asset : ListProjectAssets(launchFolder))
                if (!asset.directory && IsMapAsset(asset.path))
                {
                    project.entry = asset.path;
                    break;
                }
        launchTarget = project.target;
        if (!project.entry.empty())
        {
            launchEntry = launchFolder + "/" + project.entry;
            MapSettings map;
            if (!ReadMapSettings(launchEntry, map, error))
                return fail(error);
            if (map.target != GameTarget::Inherit)
                launchTarget = map.target;
            Board check;
            if (!ReadBoard(launchEntry, check, Target(launchTarget).tiles, error))
                return fail(error);
        }
        else if (!editor)
            return fail("The project needs an entry map.");

        options.push_back("-j" + launchFolder);
        options.push_back("-nosetup");
        options.push_back("-nologo");
        if (launchTarget == GameTarget::Standalone)
        {
            auto const script = launchFolder + "/GAME.CON";
            if (!regularAsset(script) || !regularAsset(launchFolder + "/tiles000.art") ||
                !regularAsset(launchFolder + "/palette.dat"))
                return fail("A standalone project needs its own GAME.CON, tiles000.art, and palette.dat.");
            options.push_back("-noautoload");
            options.push_back("-usecwd");
            options.push_back("-x");
            options.push_back(script);
            char cwd[BMAX_PATH];
            if (buildvfs_getcwd(cwd, sizeof(cwd)))
                originalDirectory = cwd;
            if (buildvfs_chdir(launchFolder.c_str()))
                return fail("Could not open the standalone project's asset folder.");
        }
        if (!launchEntry.empty())
        {
            options.push_back("-map");
            options.push_back(launchEntry);
        }
        for (int i = 1; i + 1 < argc; ++i)
            if (!Bstrcasecmp(argv[i], "-gamegrp"))
            {
                std::string const group = argv[i + 1];
                auto const slash = group.find_last_of("/\\");
                if (slash != std::string::npos)
                    options.push_back("-j" + group.substr(0, slash + 1));
            }
    }

    launchArgs.clear();
    launchArgs.push_back(argv[0]);
    for (auto const &option : options)
        launchArgs.push_back(option.c_str());
    for (int i = 1; i < argc; ++i)
    {
        if (i == mapArg)
        {
            ++i;
            continue;
        }
        if (i == projectArg)
        {
            i += argumentCount - 1;
            continue;
        }
        if (Bstrcmp(argv[i], "--allow-project-scripts"))
            launchArgs.push_back(argv[i]);
    }
    argc = launchArgs.size();
    launchArgs.push_back(nullptr);
    argv = launchArgs.data();
    return true;
}

void SelectProjectGame()
{
    if (launchTarget == GameTarget::Inherit || ProjectStandalone())
        return;
    auto const &profile = Target(launchTarget);
    for (auto group = foundgrps; group; group = group->next)
        if (profileMatches(profile, *group->type))
        {
            g_selectedGrp = group;
            return;
        }
    g_selectedGrp = nullptr;
}

bool ProjectStandalone() { return launchTarget == GameTarget::Standalone; }
bool ProjectAllowsAutoScripts() { return temporaryFolder.empty() || allowScripts; }

bool ValidateProjectGame()
{
    if (launchTarget == GameTarget::Inherit || ProjectStandalone())
        return true;
    if (g_selectedGrp && g_groupFileHandle >= 0 && profileMatches(Target(launchTarget), *g_selectedGrp->type))
        return true;
    LOG_F(ERROR, "This project needs %s data. Add the data folder with -j or pass -gamegrp.", Target(launchTarget).label);
    return false;
}

std::string const &LaunchProjectFolder() { return launchFolder; }

void ActivateProjectScripts(std::string const &folder, std::string const &map, MapSettings const &settings)
{
    // Scripts in an exported project use the same APIs as explicitly loaded mods.
    if ((!launchFolder.empty() && folder == launchFolder && !temporaryFolder.empty() && !allowScripts) || folder.empty())
        return;
    ProjectSettings project;
    std::string error;
    bool modern = settings.modern >= 0 ? settings.modern != 0 : ReadProject(folder, project, error) && project.modern;
    if (!modern)
        return;
    for (auto const &attachment : settings.scripts)
    {
        bool const valid = attachment.index >= 0 && (attachment.kind == "sector" ? attachment.index < numsectors :
            attachment.kind == "wall" ? attachment.index < numwalls :
            attachment.index < MAXSPRITES && sprite[attachment.index].statnum < MAXSTATUS);
        if (!valid)
        {
            LOG_F(WARNING, "Project object %s %d is unavailable.", attachment.kind.c_str(), attachment.index);
            continue;
        }
        auto const script = folder + "/" + attachment.script;
        if (regularAsset(script))
            VBJS_ActivateAttachment(script.c_str(), map.c_str(), attachment.kind.c_str(), attachment.index);
        else
            LOG_F(WARNING, "Project script %s could not be found.", attachment.script.c_str());
    }
}

void ActivateLaunchProjectScripts()
{
    if (launchFolder.empty() || !boardfilename[0])
        return;
    std::string path = boardfilename;
#ifdef _WIN32
    std::replace(path.begin(), path.end(), '\\', '/');
#endif
    auto const prefix = launchFolder.back() == '/' ? launchFolder : launchFolder + "/";
#ifdef _WIN32
    if (!Bstrncasecmp(path.c_str(), prefix.c_str(), prefix.size()))
#else
    if (!path.compare(0, prefix.size(), prefix))
#endif
        path.erase(0, prefix.size());
    if (!IsMapAsset(path) || !ValidAssetPath(path) || !regularAsset(prefix + path))
        return;
    MapSettings settings;
    std::string error;
    auto const map = prefix + path;
    if (ReadMapSettings(map, settings, error))
        ActivateProjectScripts(launchFolder, map, settings);
    else
        LOG_F(WARNING, "%s", error.c_str());
}

} // namespace DukeEditor
