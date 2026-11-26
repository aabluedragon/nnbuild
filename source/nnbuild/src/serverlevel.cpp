#include "duke3d.h"
#include "cmdline.h"
#include "netcontent.h"
#include "serverlevel.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#ifdef _WIN32
# include <process.h>
#else
# include <fcntl.h>
# include <signal.h>
# include <sys/wait.h>
# include <unistd.h>
#endif

namespace {

constexpr size_t maxCycleEntries = 64;

struct ServerLevel
{
    std::string pack, level;
};

struct ServerLaunch
{
    ServerLevel target;
    std::vector<ServerLevel> cycle;
    std::vector<std::string> commands;
    int cycleIndex = -1, rotation = 0;
    int coop = 0, skill = 2, fragLimit = 0, dmflags = DEFAULT_DMFLAGS;
};

ServerLaunch launch;
std::string launchFile;
bool probingLaunch;

std::vector<ServerLevel> cycle;
int cycleIndex = -1;
bool cycleLoaded, rotateRequested;
ServerLevel nextLevel;
int nextCycleIndex = -1;
bool nextFromEndOfLevel;

std::string pendingStateFile;

#ifdef _WIN32
intptr_t contentProbe = -1;
#else
pid_t contentProbe = -1;
#endif
uint32_t contentProbeStarted;

bool validLevelName(std::string const &name)
{
    return !name.empty() && name.size() < BMAX_PATH &&
           name.find_first_of("\r\n") == std::string::npos &&
           name.find('\0') == std::string::npos;
}

bool validCycleEntry(ServerLevel const &entry)
{
    return validLevelName(entry.level) &&
           (entry.pack.empty() || (validLevelName(entry.pack) &&
              (!Bstrcasecmp(entry.pack.c_str(), "base") || Net_ValidPackName(entry.pack.c_str()))));
}

bool hostCanSwitch(void)
{
    return myconnectindex == connecthead && numplayers > 1 && g_networkBroadcastMode == NETMODE_MASTERSLAVE;
}

bool parseEpisodeLevel(std::string const &name, int &volume, int &level)
{
    unsigned v, l;
    char extra;
    std::string code(name);
    for (char &c : code)
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    if (code.size() > 4 && code.compare(code.size() - 4, 4, ".MAP") == 0)
        code.resize(code.size() - 4);

    if (code.size() > 8 || sscanf(code.c_str(), "E%uL%u%c", &v, &l, &extra) != 2 ||
        v < 1 || v > MAXVOLUMES || l < 1 || l > MAXLEVELS)
        return false;

    volume = static_cast<int>(v) - 1;
    level = static_cast<int>(l) - 1;
    return true;
}

bool selectLevel(std::string const &name, int &volume, int &level, std::string &userMap)
{
    if (parseEpisodeLevel(name, volume, level))
    {
        auto const &map = g_mapInfo[volume * MAXLEVELS + level];
        if (volume < g_numVolumes && map.filename && map.name)
        {
            auto const file = kopen4loadfrommod(map.filename, 0);
            if (file != buildvfs_kfd_invalid)
            {
                kclose(file);
                return true;
            }
        }
        LOG_F(ERROR, "%s is unavailable in the selected game data", name.c_str());
        return false;
    }

    std::string path = name;
    auto const extension = path.find_last_of('.');
    auto const separator = path.find_last_of("/\\");
    if (extension == std::string::npos || (separator != std::string::npos && extension < separator))
        path += ".map";
    if (!VOLUMEONE && Net_StageUserMap(path.c_str(), userMap))
    {
        volume = 0;
        level = 7;
        return true;
    }

    LOG_F(ERROR, "Could not open user map '%s'", path.c_str());
    return false;
}

void readCycle(void)
{
    if (cycleLoaded)
        return;
    cycleLoaded = true;

    std::string path;
    if (!Net_ResolveContentPath("maplist.txt", path))
        return;
    std::ifstream file(path);
    std::string line;
    int lineNumber = 0;
    while (cycle.size() < maxCycleEntries && std::getline(file, line))
    {
        ++lineNumber;
        std::istringstream input(line);
        input >> std::ws;
        int const first = input.peek();
        if (first == EOF || first == '#' || first == ';')
            continue;
        ServerLevel entry;
        std::string firstWord;
        // Preserve path separators inside quoted Windows paths.
        bool parsed = static_cast<bool>(input >> std::quoted(firstWord, '"', '\0'));
        input >> std::ws;
        if (input.eof() || input.peek() == '#')
            entry.level = std::move(firstWord);
        else
        {
            if (Bstrcasecmp(firstWord.c_str(), "current"))
                entry.pack = std::move(firstWord);
            parsed = parsed && static_cast<bool>(input >> std::quoted(entry.level, '"', '\0'));
            input >> std::ws;
        }
        if (!parsed || (!input.eof() && input.peek() != '#') || !validCycleEntry(entry))
        {
            LOG_F(WARNING, "Ignoring invalid maplist.txt entry on line %d", lineNumber);
            continue;
        }
        cycle.emplace_back(std::move(entry));
    }
    LOG_F(INFO, "Loaded %u map cycle entries from maplist.txt", static_cast<unsigned>(cycle.size()));
}

bool readLaunchFile(ServerLaunch &state)
{
    std::ifstream file(launchFile, std::ios::binary);
    std::string magic;
    size_t count, commandCount;
    if (!(file >> magic) || magic != "VB-SERVER-2" ||
        !(file >> std::quoted(state.target.pack) >> std::quoted(state.target.level) >>
          state.cycleIndex >> state.rotation >> state.coop >> state.skill >>
          state.fragLimit >> state.dmflags >> count >> commandCount) ||
        count > maxCycleEntries || !validLevelName(state.target.level) ||
        (!state.target.pack.empty() && !Net_ValidPackName(state.target.pack.c_str())) ||
        state.rotation < 0 || state.rotation > 2 || state.coop < 0 || state.coop >= MAXGAMETYPES ||
        state.skill < 0 || state.skill > 4 || state.cycleIndex < -1 || state.cycleIndex >= static_cast<int>(count))
        return false;

    for (size_t i = 0; i < count; ++i)
    {
        ServerLevel entry;
        if (!(file >> std::quoted(entry.pack) >> std::quoted(entry.level)) || !validCycleEntry(entry))
            return false;
        state.cycle.emplace_back(std::move(entry));
    }
    for (size_t i = 0; i < commandCount; ++i)
    {
        std::string command;
        if (!(file >> std::quoted(command)))
            return false;
        state.commands.emplace_back(std::move(command));
    }
    return true;
}

bool writeLaunchFile(std::string const &path, ServerLevel const &target, int index)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    auto const commands = G_PendingStartupCommands();
    file << "VB-SERVER-2\n" << std::quoted(target.pack) << ' ' << std::quoted(target.level) << '\n'
         << index << ' ' << g_mapRotationMode << ' ' << ud.m_coop << ' ' << ud.m_player_skill << ' '
         << ud.fraglimit << ' ' << ud.m_dmflags << ' ' << cycle.size() << ' ' << commands.size() << '\n';
    for (auto const &entry : cycle)
        file << std::quoted(entry.pack) << ' ' << std::quoted(entry.level) << '\n';
    for (auto const &command : commands)
        file << std::quoted(command) << '\n';
    file.close();
    return static_cast<bool>(file);
}

bool startProbe(ServerLevel const &target, int index)
{
    auto const exe = Net_ProcessExecutable();
    if (!exe)
        return false;

    std::string base;
    if (!Net_ResolveContentPath(G_GrpFile(), base))
        return false;

    buildvfs_mkdir("netcache", S_IRWXU);
    char fileName[64];
    Bsnprintf(fileName, sizeof(fileName), "netcache/server-%d-%u.state", mmulti_getport(), timerGetTicks());
    if (!writeLaunchFile(fileName, target, index) || !Net_ResolveContentPath(fileName, pendingStateFile))
    {
        remove(fileName);
        return false;
    }

    // Use the same base archive, content descriptor and search paths. A failed
    // CON compile or invalid map exits only this headless validation process.
    const std::string searchDir = std::string("-j") + Net_LaunchDirectory();
    const char *args[] = { exe, "-usecwd", "-dedicated", "-nosetup", "-noinstancechecking",
                           "-noautoload", "-gamegrp", base.c_str(), searchDir.c_str(),
                           "--content-probe", pendingStateFile.c_str(), nullptr };
#ifdef _WIN32
    contentProbe = Net_StartContentProcess(args);
#else
    contentProbe = fork();
    if (contentProbe == 0)
    {
        execl(exe, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7],
              args[8], args[9], args[10], static_cast<char const *>(nullptr));
        _exit(EXIT_FAILURE);
    }
#endif
    if (contentProbe == -1)
    {
        remove(pendingStateFile.c_str());
        pendingStateFile.clear();
        return false;
    }
    contentProbeStarted = timerGetTicks();
    return true;
}

int pollProbe(void)
{
    bool const expired = timerGetTicks() - contentProbeStarted > 60000;
#ifdef _WIN32
    HANDLE const process = reinterpret_cast<HANDLE>(contentProbe);
    DWORD status;
    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
    {
        if (expired)
            TerminateProcess(process, EXIT_FAILURE);
        return 0;
    }
    bool const ok = GetExitCodeProcess(process, &status) && status == EXIT_SUCCESS;
    CloseHandle(process);
#else
    int status;
    auto const result = waitpid(contentProbe, &status, WNOHANG);
    if (result == 0)
    {
        if (expired)
            kill(contentProbe, SIGKILL);
        return 0;
    }
    if (result == -1 && errno == EINTR)
        return 0;
    bool const ok = result == contentProbe && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
#endif
    contentProbe = -1;
    return ok ? 1 : -1;
}

void levelChangeFailed(void)
{
    if (nextFromEndOfLevel)
    {
        LOG_F(WARNING, "Map cycle could not advance; restarting the current level");
        G_NewGame(NEWGAME_RESETALL);
    }
    nextFromEndOfLevel = false;
    nextCycleIndex = -1;
}

} // namespace

bool Net_QueueServerLevel(const char *pack, const char *level)
{
    if (!hostCanSwitch())
    {
        OSD_Printf("Only the master/slave host can change the server level.\n");
        return false;
    }
    if (Net_ServerLevelChangePending())
    {
        OSD_Printf("A server level change is already pending.\n");
        return false;
    }

    ServerLevel entry = {pack && Bstrcasecmp(pack, "current") ? pack : "", level};
    if (!validCycleEntry(entry))
    {
        OSD_Printf("Specify a GRP, ZIP, PK3, PK4 or SSI pack, base, or current, and a level or map path.\n");
        return false;
    }
    nextLevel = std::move(entry);
    return true;
}

bool Net_ServerLevelChangePending(void)
{
    return !nextLevel.level.empty() || contentProbe != -1;
}

void Net_RunServerLevelChange(void)
{
    if (g_lateJoinFreeze)
        return;

    if (contentProbe != -1)
    {
        auto const result = pollProbe();
        if (!result)
            return;

        std::string const state = std::move(pendingStateFile);
        if (result > 0)
            Net_RestartForContent(state.c_str());

        LOG_F(ERROR, "Could not activate the requested pack and level");
        remove(state.c_str());
        levelChangeFailed();
        return;
    }

    if (nextLevel.level.empty())
        return;

    auto const target = std::move(nextLevel);
    nextLevel = {};
    std::string pack;
    if (!target.pack.empty() && Bstrcasecmp(target.pack.c_str(), "base") &&
        !Net_ResolvePack(target.pack.c_str(), pack))
    {
        LOG_F(ERROR, "Could not open server pack '%s'", target.pack.c_str());
        levelChangeFailed();
        return;
    }

    bool const currentPack = target.pack.empty() ||
        (pack.empty() && g_netModFileCount == 0) ||
        (g_netModFileCount == 1 && !Bstrcmp(g_netModFiles[0], pack.c_str()));

    if (!currentPack)
    {
        readCycle();
        int const index = nextCycleIndex >= 0 ? nextCycleIndex : cycleIndex;
        if (!startProbe({pack, target.level}, index))
        {
            LOG_F(ERROR, "Could not start pack validation");
            levelChangeFailed();
        }
        return;
    }

    int volume, level;
    std::string userMap;
    if (!selectLevel(target.level, volume, level, userMap) || !Net_SendMap(userMap.c_str(), -1))
    {
        levelChangeFailed();
        return;
    }

    if (nextCycleIndex >= 0)
        cycleIndex = nextCycleIndex;
    nextCycleIndex = -1;
    nextFromEndOfLevel = false;
    ud.m_volume_number = volume;
    ud.m_level_number = level;
    Bstrncpyz(boardfilename, userMap.c_str(), sizeof(boardfilename));
    Net_SendUserMapName();
    LOG_F(INFO, "Changing server level to %s", target.level.c_str());
    G_NewGame(NEWGAME_RESETALL);
}

void Net_RequestMapRotation(void)
{
    rotateRequested = true;
}

void Net_CheckMapRotation(void)
{
    if (!rotateRequested || Net_ServerLevelChangePending() || g_lateJoinFreeze)
        return;

    rotateRequested = false;
    nextFromEndOfLevel = true;
    if (Net_RotateNextMap() < 0)
        levelChangeFailed();
}

int32_t Net_RotateNextMap(void)
{
    readCycle();
    if (cycle.empty())
        return -1;
    int const index = g_mapRotationMode == 2
        ? wrand() % cycle.size()
        : (cycleIndex + 1) % cycle.size();

    auto const &target = cycle[index];
    if (!Net_QueueServerLevel(target.pack.c_str(), target.level.c_str()))
        return -1;
    nextCycleIndex = index;
    return index;
}

void Net_ListMapRotation(void)
{
    readCycle();
    OSD_Printf("Map cycle (%u, %s)\n", static_cast<unsigned>(cycle.size()),
               g_mapRotationMode == 2 ? "random" : g_mapRotationMode == 1 ? "sequential" : "off");
    for (size_t i = 0; i < cycle.size(); ++i)
    {
        auto const &entry = cycle[i];
        OSD_Printf("  %u. %s | %s%s\n", static_cast<unsigned>(i + 1),
                   entry.pack.empty() ? "current" : entry.pack.c_str(),
                   entry.level.c_str(), static_cast<int>(i) == cycleIndex ? " (current)" : "");
    }
}

int Net_MapCycleCommand(const osdfuncparm_t *parm)
{
    if (parm->numparms == 0 ||
        (parm->numparms == 1 && !Bstrcasecmp(parm->parms[0], "list")))
    {
        Net_ListMapRotation();
        return OSDCMD_OK;
    }
    if (!hostCanSwitch())
    {
        OSD_Printf("Only the server host can edit the map cycle.\n");
        return OSDCMD_OK;
    }
    if (Net_ServerLevelChangePending())
    {
        OSD_Printf("Wait for the pending level change before editing the map cycle.\n");
        return OSDCMD_OK;
    }
    if (parm->numparms == 1 && !Bstrcasecmp(parm->parms[0], "clear"))
    {
        cycleLoaded = true;
        cycle.clear();
        cycleIndex = -1;
        return OSDCMD_OK;
    }
    if (parm->numparms == 1 && !Bstrcasecmp(parm->parms[0], "reload"))
    {
        cycleLoaded = false;
        cycle.clear();
        cycleIndex = -1;
        Net_ListMapRotation();
        return OSDCMD_OK;
    }
    if ((parm->numparms == 2 || parm->numparms == 3) && !Bstrcasecmp(parm->parms[0], "add"))
    {
        readCycle();
        char const *pack = parm->numparms == 3 ? parm->parms[1] : "current";
        ServerLevel entry = {Bstrcasecmp(pack, "current") ? pack : "", parm->parms[parm->numparms - 1]};
        if (!validCycleEntry(entry))
            return OSDCMD_SHOWHELP;
        if (cycle.size() == maxCycleEntries)
            OSD_Printf("The map cycle already has %u entries.\n", static_cast<unsigned>(maxCycleEntries));
        else
        {
            cycle.emplace_back(std::move(entry));
            OSD_Printf("Added map cycle entry %u.\n", static_cast<unsigned>(cycle.size()));
        }
        return OSDCMD_OK;
    }
    return OSDCMD_SHOWHELP;
}

void Net_SetServerLaunchFile(const char *path, bool probe)
{
    launchFile = path;
    probingLaunch = probe;
}

bool Net_IsContentLaunch(void) { return !launchFile.empty(); }
bool Net_IsContentProbe(void) { return probingLaunch; }

bool Net_RestoreServerLaunch(void)
{
    if (!Net_IsContentLaunch())
        return true;

    if (!readLaunchFile(launch))
    {
        LOG_F(ERROR, "Could not read server content state from '%s'", launchFile.c_str());
        return false;
    }
    G_ReplaceCommandGroups(launch.target.pack.empty() ? std::vector<std::string>{}
                                                       : std::vector<std::string>{launch.target.pack});
    clearScriptNamePtr();
    g_scriptNamePtr = nullptr;
    clearDefNamePtr();
    g_defNamePtr = nullptr;
    CommandMap = nullptr;
    g_noAutoLoad = 1;
    if (!probingLaunch)
    {
        g_netLateJoin = g_netStartNow = 1;
        cycle = std::move(launch.cycle);
        cycleIndex = launch.cycleIndex;
        cycleLoaded = true;
        G_RestoreStartupCommands(std::move(launch.commands));
        Net_RestoreServerSettings();
    }
    return true;
}

bool Net_ApplyServerLaunchLevel(void)
{
    if (Net_IsContentLaunch())
    {
        int volume, level;
        std::string map;
        int const expectedPacks = !launch.target.pack.empty();
        if (g_netModFileCount != expectedPacks || !selectLevel(launch.target.level, volume, level, map))
            return false;
        ud.m_volume_number = ud.volume_number = volume;
        ud.m_level_number = ud.level_number = level;
        Bstrncpyz(boardfilename, map.c_str(), sizeof(boardfilename));
        ud.warp_on = 1;
        if (!probingLaunch)
            remove(launchFile.c_str());
        return true;
    }

    if (numplayers > 1 && myconnectindex == connecthead && CommandMap)
    {
        int volume, level;
        std::string map;
        if (!selectLevel(CommandMap, volume, level, map))
            return false;
        Bstrncpyz(boardfilename, map.c_str(), sizeof(boardfilename));
        ud.m_volume_number = ud.volume_number = volume;
        ud.m_level_number = ud.level_number = level;
        ud.warp_on = 1;
    }
    if (g_netStartNow && myconnectindex == connecthead)
        ud.warp_on = 1;
    return true;
}

bool Net_CheckLaunchMap(void)
{
    char const *path = boardfilename[0]
        ? boardfilename
        : g_mapInfo[ud.volume_number * MAXLEVELS + ud.level_number].filename;
    vec3_t position;
    int16_t angle, sector;
    return path && engineLoadBoard(path, VOLUMEONE | 8, &position, &angle, &sector) >= 0;
}

void Net_RestoreServerSettings(void)
{
    if (!Net_IsContentLaunch() || probingLaunch)
        return;

    g_mapRotationMode = launch.rotation;
    ud.m_coop = launch.coop;
    ud.m_player_skill = ud.player_skill = launch.skill;
    ud.fraglimit = launch.fragLimit;
    ud.m_dmflags = launch.dmflags;
}
