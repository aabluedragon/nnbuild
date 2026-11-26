//-------------------------------------------------------------------------
/*
Copyright (C) 2016 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "duke3d.h"
//#include "demo.h"
#include "screens.h"
#include "vbjs.h"
#include "renderlayer.h"
#include "cmdline.h"
#include "mapeditor.h"
#include "masterserver.h"
#include "serverlevel.h"
#include "vbjs.h"

#include <string>
#include <vector>

int32_t g_commandSetup = 0;
int32_t g_noSetup = 0;
int32_t g_noAutoLoad = 0;
int32_t g_noSound = 0;
int32_t g_noMusic = 0;
int32_t g_noLogoAnim = 0;
int32_t g_noLogo = 0;

static int32_t g_commandScreenMode = -1;
static int32_t g_commandScreenWidth = 0;
static int32_t g_commandScreenHeight = 0;

int32_t g_keepAddr = 0; // Seemingly unused...

const char* CommandMap = NULL;
const char* CommandName = NULL;
int32_t CommandWeaponChoice = 0;

int32_t netparamcount = 0;
char** netparam = NULL;

char firstdemofile[80] = { '\0' };
int32_t userconfiles = 0;

static std::vector<std::string> startupCommands;
static size_t nextStartupCommand;

std::vector<std::string> G_PendingStartupCommands(void)
{
    return {startupCommands.begin() + nextStartupCommand, startupCommands.end()};
}

void G_RestoreStartupCommands(std::vector<std::string> commands)
{
    startupCommands = std::move(commands);
    nextStartupCommand = 0;
}

bool G_RunStartupCommand(void)
{
    if (nextStartupCommand == startupCommands.size())
        return false;

    OSD_Dispatch(startupCommands[nextStartupCommand++].c_str());
    return true;
}

static void G_AddStartupCommands(const char *text)
{
    while (*text)
    {
        auto const length = strcspn(text, "\r\n");
        auto const leading = strspn(text, " \t");
        if (leading < length && text[leading] != '#')
            startupCommands.emplace_back(text + leading, length - leading);
        text += length + strspn(text + length, "\r\n");
    }
}

static bool G_ReadStartupCommands(const char *path)
{
    buildvfs_FILE const file = buildvfs_fopen_read(path);
    if (!file)
        return false;

    auto const length = buildvfs_flength(file);
    bool ok = false;
    if (length >= 0 && length <= 1024 * 1024)
    {
        std::string content(static_cast<size_t>(length) + 1, '\0');
        ok = buildvfs_fread(&content[0], 1, length, file) == static_cast<size_t>(length);
        if (ok)
        {
            char const *text = content.c_str();
            if (content.compare(0, 3, "\xef\xbb\xbf") == 0)
                text += 3;
            G_AddStartupCommands(text);
        }
    }
    buildvfs_fclose(file);
    return ok;
}

static bool G_IsScriptPath(char const *path)
{
    auto const ext = Bstrrchr(path, '.');
    return ext && (!Bstrcasecmp(ext, ".js") || !Bstrcasecmp(ext, ".ts")
                || !Bstrcasecmp(ext, ".mjs") || !Bstrcasecmp(ext, ".mts")
                || !Bstrcasecmp(ext, ".cjs") || !Bstrcasecmp(ext, ".cts"));
}

static void G_SetCommandLineScreenMode(int32_t mode)
{
    g_commandScreenMode = mode;
    ud.config.ScreenMode = mode;
}

static void G_SetCommandLineResolution(int32_t width, int32_t height)
{
    if (width < 320 || height < 200)
        return;

    g_commandScreenWidth = width;
    g_commandScreenHeight = height;
    ud.config.ScreenWidth = width;
    ud.config.ScreenHeight = height;
}

void G_ApplyCommandLineVideoOverrides(void)
{
    if (g_commandScreenMode >= 0)
        ud.config.ScreenMode = g_commandScreenMode;

    if (g_commandScreenWidth > 0 && g_commandScreenHeight > 0)
    {
        ud.config.ScreenWidth = g_commandScreenWidth;
        ud.config.ScreenHeight = g_commandScreenHeight;
    }
}

static const char *G_CommandLineOptionValue(const char *option, const char *shortName, const char *longName,
                                            int *argIndex, int argc, char const * const *argv)
{
    size_t const shortLen = Bstrlen(shortName);
    size_t const longLen = Bstrlen(longName);

    if (!Bstrncasecmp(option, longName, longLen))
    {
        const char *value = option + longLen;
        if (*value == '=')
            ++value;
        if (*value)
            return isalpha((unsigned char)*value) ? nullptr : value;
        if (*argIndex + 1 < argc)
            return argv[++*argIndex];
        return nullptr;
    }

    if (!Bstrncasecmp(option, shortName, shortLen))
    {
        const char *value = option + shortLen;
        if (*value == '=')
            ++value;
        if (*value)
            return isalpha((unsigned char)*value) ? nullptr : value;
        if (*argIndex + 1 < argc)
            return argv[++*argIndex];
        return nullptr;
    }

    return nullptr;
}

static const char *G_CommandLineOptionTextValue(const char *option, const char *shortName, const char *longName,
                                                int *argIndex, int argc, char const * const *argv)
{
    size_t const shortLen = Bstrlen(shortName);
    size_t const longLen = Bstrlen(longName);

    if (!Bstrncasecmp(option, longName, longLen))
    {
        const char *value = option + longLen;
        if (*value == '=')
            ++value;
        if (*value)
            return value;
        if (*argIndex + 1 < argc)
            return argv[++*argIndex];
        return nullptr;
    }

    if (!Bstrncasecmp(option, shortName, shortLen))
    {
        const char *value = option + shortLen;
        if (*value == '=')
            ++value;
        if (*value)
            return value;
        if (*argIndex + 1 < argc)
            return argv[++*argIndex];
        return nullptr;
    }

    return nullptr;
}

static void G_ShowParameterHelp(void)
{
    const char* s = "Usage: " APPBASENAME " [files] [options]\n"
        "Example: " APPBASENAME " -q4 -a -m -tx -map nukeland.map\n\n"
        "Files can be *.grp/zip/con/def/js/ts\n"
        "\n"
        "-allowvisibility\tAllows visibility adjustment in multiplayer."
        "\n"
        "-cfg [file.cfg]\tUse an alternate configuration file\n"
        "--editor [file.map]\tOpen the integrated Duke3D map editor\n"
        "--build-editor [file.map]\tOpen the native Build editing tools\n"
        "--project <folder|zip>\tPlay the project or open it with --editor\n"
        "--project-map <file.map>\tSelect a map within the project to play or edit\n"
        "--allow-project-scripts\tEnable JavaScript and TypeScript attachments from a project ZIP\n"
        "--editor-mcp [host[:port]]\tJoin a live editor as an MCP participant\n"
        "--mcp-stdio\tControl this game, server or editor through MCP over stdin/stdout\n"
        "--mcp-http <port>\tControl this instance through a loopback MCP endpoint\n"
        "-c#\t\tUse MP mode #, 1 = Dukematch, 2 = Coop, 3 = Team Dukematch\n"
        "-c, --cmd <text>\tRun startup console commands, one per line (repeatable)\n"
        "-cf, --cmd-file <path>\tRead startup console commands, one per line (repeatable)\n"
        "-js, --js <path>\tLoad a JavaScript or TypeScript mod; a bare path also works\n"
        "-d[file.dmo]\tPlay a demo\n"
        "-g, --grp [file.grp]\tUse a group/data file; also infers game if -gm is omitted\n"
        "-h[file.def]\tUse an alternate def\n"
        "-j[dir]\t\tAdds a directory to EDuke32's search list\n"
        "-gm, --game <name>\tForce Duke, World Tour, Ion Fury, SW, Blood, RR, NAM, Napalm, WW2GI, or Exhumed\n"
        "-gm.lvls, --game.levels <name>\tCrossplay: use this game's levels/maps\n"
        "-gm.its, --game.items <name>\tCrossplay: use this game's item rules/data\n"
        "-gm.wpns, --game.weapons <name>\tCrossplay: use this game's weapon rules/data\n"
        "-gjs, --gen-js [name]\tCreate autojs/<name>/ JavaScript mod starter and exit\n"
        "-gts, --gen-ts [name]\tCreate autojs/<name>/ TypeScript mod starter and exit\n"
        "-js, --js [file.js]\tLoad a JavaScript user script\n"
        "-skn, --skin <name|#>\tPlayer multiplayer skin/profile (e.g. Duke, LoWang, Shelly)\n"
        "-sm, --skip-menu\tSkip the game picker and native startup window\n"
        "-m, --menu\t\tForce the native startup window (overrides -sm)\n"
        "-sc, --skip-cinema\tSkip startup cinematics and logo animations\n"
        "-clr, --color <name|#rrggbb>\tSet player color\n"
        "-wnd, --window\tForce windowed video mode\n"
        "-fscr, --fullscreen\tForce fullscreen video mode\n"
        "-res, --resolution <WIDTHxHEIGHT>\tForce video resolution\n"
        "-dbg, --debug\tEnable the JavaScript debugger attach endpoint\n"
        "-dt, --devtools\tOpen JavaScript DevTools (implies -dbg; forces windowed mode)\n"
        "-mnst, --monsters <n>\tSet monsters: 0 disables them; 1+ selects skill\n"
        "-e, --episode <n>\tAlias for -v, --volume <n>\n"
        "-nosound\t\tDisable sound effects\n"
        "-nomusic\t\tDisable music\n"
        "-r, --record\t\tRecord a demo\n"
        "-pb, --playback [file]\tPlay a demo\n"
        "-l#\t\tWarp to level #, see -v\n"
        "--level [#]\tWarp to level #\n"
        "-map [file.map]\tLoads a map\n"
        "-m\t\tDisable monsters\n"
        "-nam/-ww2gi\tRun in NAM or WW2GI-compatible mode\n"
        "-net <command>\tEnable multiplayer (see below)\n"
        "\n"
        "net command examples:\n"
        "-net /n0:3\tHost 3-player match in master/slave mode.\n"
        "-net /n0 192.168.0.1\tJoin specified IP in master/slave mode.\n"
        "cross-game example:\n"
        APPBASENAME " -gm ionfury --game-dir /path/to/Ion\\ Fury -g /path/to/DUKE3D.GRP -map E3L6\n"
        "\t\tRun Duke3D Atomic's Rabid Transit map with Ion Fury as the main game.\n"
        "\n"
        "-as, --async\tHost: server-authoritative netplay - never stall on a slow\n"
        "\t\tclient (their own input gets more latency instead). Default\n"
        "\t\tis lockstep (equal lag for all).\n"
        "-lj, --latejoin\tHost: start with open slots that players may join mid-game.\n"
        "-sn, --startnow\tHost: begin immediately with whoever is present (even alone);\n"
        "\t\tremaining slots stay open to join anytime. Implies --latejoin.\n"
        "-dd, --dedicated\tHost: read admin commands from stdin (kick/ban/etc).\n"
        "\t\tConsole commands include map, mod, mapcycle, nextmap, kick and ban.\n"
        "-hl, --headless\tHost: dedicated server with no window, graphics or sound -\n"
        "\t\tterminal only. Implies --dedicated. (legacy alias: -console)\n"
        "\n"
        "-noffire\t\tDisable friendly fire\n"
        "-rts [file.rts]\tLoad a custom Remote Ridicule sound bank\n"
        "-r\t\tRecord demo\n"
        "-s#\t\tSet skill level (1-4)\n"
#if STARTUP_WINDOW
        "-setup/nosetup\tEnables/disables startup window\n"
#endif
        "-t#\t\tSet respawn mode: 1 = Monsters, 2 = Items, 3 = Inventory, x = All\n"
#if !defined(_WIN32)
        "-usecwd\t\tRead game data and configuration file from working directory\n"
#endif
        "-u#########\tUser's favorite weapon order (default: 3425689071)\n"
        "-v#\t\tWarp to volume #, see -l\n"
        "--volume [#]\tWarp to volume #\n"
        "-wstay\t\tEnable Weapon Stay\n"
        "-x[game.con]\tLoad custom CON script\n"
        "-y##\t\tSet frag limit for DukeMatch\n"
        "-#\t\tLoad and run a game from slot # (0-9)\n"
        //              "\n-?/--help\tDisplay this help message and exit\n"
        "\nSee eduke32 -debughelp for debug parameters"
        ;
#if _WIN32
    Bsprintf(tempbuf, HEAD2 " (%s)", s_buildTimestamp);
    wm_msgbox(tempbuf, s);
#else
    LOG_F(INFO, "%s", s);
#endif
}

static void G_ShowDebugHelp(void)
{
    const char* s = "Usage: " APPBASENAME " [files] [options]\n"
        "\n"
        "-a\t\tUse fake player AI (fake multiplayer only)\n"
        "-cachesize #\tSets cache size, in Kb\n"
        "-fNUM\t\tSend fewer packets in multiplayer (1, 2, 4) (deprecated)\n"
        "-game_dir [dir]\tDuke3d_w32 compatibility option, see -j\n"
        "-gamegrp   \tSelects which file to use as main grp\n"
        "-name [name]\tPlayer name in multiplay\n"
        "-nD\t\tDump default gamevars to gamevars.txt\n"
        "-noautoload\tDisable loading content from autoload dir\n"
        "-nologo\t\tSkip the logo anim\n"
        "-ns/-nm\t\tDisable sound or music\n"
        "-q#\t\tFake multiplayer with # (2-8) players\n"
        "-stun\t\tUse UDP hole punching for multiplayer connections\n"
        "-unstable   \tForce EDuke32 to execute unsafe CON commands (and crash)\n"
        "-w\t\tShow coordinates\n"
        "-z#/-condebug\tEnable line-by-line CON compile debugging at level #\n"
        ;
#ifdef _WIN32
    Bsprintf(tempbuf, HEAD2 " (%s)", s_buildTimestamp);
    wm_msgbox(tempbuf, s);
#else
    LOG_F(INFO, "%s", s);
#endif
}

void G_CheckCommandLine(int argc, char const* const* argv)
{
    int i, j;
    char* c;
    int firstnet = 0;
    char* k;
    bool const resume = G_CheckCmdSwitch(argc, argv, "--server-resume");

    i = 1;

    ud.god = 0;
    ud.warp_on = 0;
    ud.cashman = 0;
    ud.m_player_skill = ud.player_skill = 2;
    ud.m_dmflags = DEFAULT_DMFLAGS;
    g_player[0].wchoice[0] = 3;
    g_player[0].wchoice[1] = 4;
    g_player[0].wchoice[2] = 5;
    g_player[0].wchoice[3] = 7;
    g_player[0].wchoice[4] = 8;
    g_player[0].wchoice[5] = 6;
    g_player[0].wchoice[6] = 0;
    g_player[0].wchoice[7] = 2;
    g_player[0].wchoice[8] = 9;
    g_player[0].wchoice[9] = 1;
    Bsnprintf(ud.wchoice, sizeof(ud.wchoice), "3457860291");

    if (argc > 1)
    {
        LOG_F(INFO, "Application parameters (%d):", argc);
        for (int arg = 0; arg < argc; ++arg)
            LOG_F(INFO, "Command line argument %d: %s", arg, argv[arg]);

        i = 1;
        while (i < argc)
        {
            c = const_cast<char*>(argv[i]);
            if (!Bstrcmp(c, "--editor"))
            {
                const char *path = nullptr;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    auto const extension = Bstrrchr(argv[i + 1], '.');
                    if (extension && !Bstrcasecmp(extension, ".map"))
                        path = argv[++i];
                }
                MapEditor_Request(path);
                ++i;
                continue;
            }
            if (!Bstrcmp(c, "--content-rejoin"))
            {
                ++i;
                continue;
            }
            bool const probe = !Bstrcmp(c, "--content-probe");
            if (probe || !Bstrcmp(c, "--server-resume"))
            {
                if (++i == argc)
                    Bexit(EXIT_FAILURE);
                Net_SetServerLaunchFile(argv[i++], probe);
                continue;
            }
            bool const cmd = !Bstrcmp(c, "-c") || !Bstrcmp(c, "--cmd");
            bool const cmdFile = !Bstrcmp(c, "-cf") || !Bstrcmp(c, "--cmd-file");
            bool const script = !Bstrcmp(c, "-js") || !Bstrcmp(c, "--js");
            if (cmd || cmdFile || script)
            {
                if (++i == argc)
                {
                    LOG_F(ERROR, "Missing argument for %s", c);
                    Bexit(EXIT_FAILURE);
                }
                if (cmd && !resume)
                    G_AddStartupCommands(argv[i]);
                else if (script)
                    VBJS_SetScriptPath(argv[i]);
                else if (cmdFile && !resume && !G_ReadStartupCommands(argv[i]))
                {
                    LOG_F(ERROR, "Could not read startup command file '%s'", argv[i]);
                    Bexit(EXIT_FAILURE);
                }
                ++i;
                continue;
            }
            if (*c != '-' && G_IsScriptPath(c) && (!firstnet || buildvfs_exists(c)))
            {
                VBJS_SetScriptPath(c);
                ++i;
                continue;
            }
            if (((*c == '/') || (*c == '-')) && (!firstnet))
            {
                // VibeBuild32 modern args: accept "--foo" as "-foo" so every Duke option
                // gains a long form, matching RR/SW/Blood (e.g. --cfg, --map, --nosetup, --nologo).
                if (c[0] == '-' && c[1] == '-' && c[2])
                    c++;

                if (!Bstrcasecmp(c + 1, "?") || !Bstrcasecmp(c + 1, "help") || !Bstrcasecmp(c + 1, "-help"))
                {
                    G_ShowParameterHelp();
                    Bexit(EXIT_SUCCESS);
                }
                if (!Bstrcasecmp(c + 1, "?") || !Bstrcasecmp(c + 1, "debughelp") || !Bstrcasecmp(c + 1, "-debughelp"))
                {
                    G_ShowDebugHelp();
                    Bexit(EXIT_SUCCESS);
                }
                if (!Bstrcasecmp(c + 1, "grp") || !Bstrcasecmp(c + 1, "g"))
                {
                    if (argc > i + 1)
                    {
                        G_AddGroup(argv[i + 1]);
                        // A caller that supplied its own game data is explicitly
                        // asking to launch, not to wait in the platform setup UI.
                        // This is especially important on macOS, where that window
                        // may be behind the terminal that launched the game.
                        g_noSetup = 1;
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "game_dir"))
                {
                    if (argc > i + 1)
                    {
                        Bstrcpy(g_modDir, argv[i + 1]);
                        G_AddPath(argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                // VibeBuild32: --js/-js <file.js> loads a QuickJS user script.
                if (!Bstrcasecmp(c + 1, "js"))
                {
                    if (argc > i + 1)
                    {
                        VBJS_SetScriptPath(argv[i + 1]);
                        LOG_F(INFO, "JS script: %s", argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                // Create an editor-ready TypeScript project.  The value is optional:
                // without it VBJS chooses a short unused directory name.
                if (!Bstrcasecmp(c + 1, "gen-js") || !Bstrcasecmp(c + 1, "gjs") ||
                    !Bstrcasecmp(c + 1, "gen-ts") || !Bstrcasecmp(c + 1, "gts"))
                {
                    int const typescript = !Bstrcasecmp(c + 1, "gen-ts") || !Bstrcasecmp(c + 1, "gts");
                    const char *name = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : NULL;
                    if (VBJS_GenerateProject(name, typescript) != 0)
                    {
                        LOG_F(ERROR, "Could not create JavaScript project.");
                        Bexit(EXIT_FAILURE);
                    }
                    Bexit(EXIT_SUCCESS);
                }
                if (!Bstrcasecmp(c + 1, "devtools") || !Bstrcasecmp(c + 1, "dt"))
                {
                    VBJS_SetDevtools(1);
                    G_SetCommandLineScreenMode(0); // Devtools is a side overlay: never start fullscreen.
                    g_noSetup = 1;            // debugger launches cannot wait in the native setup window
                    g_commandSetup = 0;
                    LOG_F(INFO, "JavaScript devtools enabled (windowed mode).");
                    i++;
                    continue;
                }
                if ((!Bstrncasecmp(c + 1, "debug", 5) && (!c[6] || isdigit((unsigned char)c[6]))) ||
                    (!Bstrncasecmp(c + 1, "dbg", 3) && (!c[4] || isdigit((unsigned char)c[4]))))
                {
                    VBJS_SetDebug(1);
                    g_noSetup = 1;            // launch straight into the game for VS Code
                    g_commandSetup = 0;
                    const char *portText = !Bstrncasecmp(c + 1, "debug", 5) ? c + 6 : c + 4;
                    if (*portText) VBJS_SetDebugPort(atoi(portText));
                    else if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) VBJS_SetDebugPort(atoi(argv[++i]));
                    LOG_F(INFO, "JavaScript debugger attach endpoint enabled on port %d.", VBJS_DebugPort());
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "monsters") || !Bstrcasecmp(c + 1, "mnst"))
                {
                    int const n = (argc > i + 1) ? atoi(argv[i + 1]) : 0;   // 1-based
                    if (n <= 0)
                    {
                        M_DMFLAGS_SET(DMFLAG_NOMONSTERS, true);
                        ud.m_player_skill = ud.player_skill = 0;
                        LOG_F(INFO, "No enemies.");
                    }
                    else
                    {
                        M_DMFLAGS_SET(DMFLAG_NOMONSTERS, false);
                        ud.m_player_skill = ud.player_skill = clamp(n - 1, 0, 4);
                    }
                    if (argc > i + 1) i++;
                    i++;
                    continue;
                }
                // VibeBuild32 canonical demo flags: --record/-r and --playback/-pb [file] (cross-game).
                if (!Bstrcasecmp(c + 1, "record") || !Bstrcasecmp(c + 1, "-record"))
                {
                    ud.m_recstat = 1;
                    LOG_F(INFO, "Demo record mode on.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "playback") || !Bstrcasecmp(c + 1, "-playback") || !Bstrcasecmp(c + 1, "pb"))
                {
                    if (argc > i + 1 && argv[i + 1][0] != '-')
                    {
                        Bstrncpyz(firstdemofile, argv[i + 1], sizeof(firstdemofile));
                        if (strchr(firstdemofile, '.') == 0)
                            Bstrcat(firstdemofile, ".dmo");
                        LOG_F(INFO, "Play demo %s.", firstdemofile);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "cfg"))
                {
                    if (argc > i + 1)
                    {
                        Bstrcpy(g_setupFileName, argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "gamegrp"))
                {
                    if (argc > i + 1)
                    {
                        clearGrpNamePtr();
                        g_grpNamePtr = dup_filename(argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "nam"))
                {
                    g_gameType = GAMEFLAG_NAM;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "napalm"))
                {
                    g_gameType = GAMEFLAG_NAM | GAMEFLAG_NAPALM;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "ww2gi"))
                {
                    g_gameType = GAMEFLAG_WW2GI;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "wstay"))
                {
                    //ud.m_weaponstay = 1;
                    M_DMFLAGS_SET(DMFLAG_WEAPONSTAY, true);
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "setup"))
                {
                    g_commandSetup = TRUE;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "nosetup"))
                {
                    g_noSetup = 1;
                    g_commandSetup = 0;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "noautoload"))
                {
                    LOG_F(INFO, "Autoload disabled");
                    g_noAutoLoad = 1;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "keepaddr"))
                {
                    g_keepAddr = 1;
                    i++;
                    continue;
                }
#if 1 // OLDMP MERGE CHECKME
                if (!Bstrcasecmp(c + 1, "stun"))
                {
                    natfree = 1; //Addfaz NatFree
                    i++;
                    continue;
                }
#endif
                if (!Bstrcasecmp(c + 1, "net"))
                {
                    g_noSetup = TRUE;
                    firstnet = i;
                    netparam = (char**)Xcalloc(argc - i, sizeof(char**));
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "name"))
                {
                    if (argc > i + 1)
                    {
                        CommandName = argv[i + 1];
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "map"))
                {
                    if (argc > i + 1)
                    {
                        CommandMap = argv[i + 1];
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "rts"))
                {
                    if (argc > i + 1)
                    {
                        Bfree(g_rtsNamePtr);
                        g_rtsNamePtr = dup_filename(argv[i + 1]);
                        LOG_F(INFO, "Using RTS file \"%s\".", g_rtsNamePtr);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "noffire"))
                {
                    //ud.m_ffire = 0;
                    M_DMFLAGS_SET(DMFLAG_FRIENDLYFIRE, false);
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "allowvisibility"))
                {
                    M_DMFLAGS_SET(DMFLAG_ALLOWVISIBILITYCHANGE, true);
                    LOG_F(INFO, "Visibility adjustment enabled for multiplayer.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "condebug"))
                {
                    g_scriptDebug = 1;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "nologo"))
                {
                    g_noLogo = 1;
                    g_noLogoAnim = 1;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "nosound"))
                {
                    g_noSound = 2;
                    LOG_F(INFO, "Sound off.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "nomusic"))
                {
                    g_noMusic = 1;
                    LOG_F(INFO, "Music off.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "color") && i + 1 < argc)
                {
                    ud.m_color = ud.color = atoi(argv[++i]);
                    LOG_F(INFO, "Player color palette: %d", ud.color);
                    i++;
                    continue;
                }
                if (!Bstrncasecmp(c + 1, "skin", 4) || !Bstrncasecmp(c + 1, "skn", 3))
                {
                    if (const char *skinName = G_CommandLineOptionTextValue(c + 1, "skn", "skin", &i, argc, argv))
                    {
                        int32_t const skin = P_SkinIndexFromName(skinName);
                        if (skin >= 0)
                        {
                            ud.skin = skin;
                            LOG_F(INFO, "Player skin/profile: %s (%d)", g_playerSkinNames[skin], skin);
                        }
                        else
                        {
                            LOG_F(WARNING, "Unknown player skin/profile '%s'.", skinName);
                        }
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "window") || !Bstrcasecmp(c + 1, "wnd"))
                {
                    G_SetCommandLineScreenMode(0);
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "fullscreen") || !Bstrcasecmp(c + 1, "fscr"))
                {
                    G_SetCommandLineScreenMode(1);
                    i++;
                    continue;
                }
                if (!Bstrncasecmp(c + 1, "resolution", 10) || !Bstrncasecmp(c + 1, "res", 3))
                {
                    if (const char *resolution = G_CommandLineOptionValue(c + 1, "res", "resolution", &i, argc, argv))
                    {
                        int width = 0, height = 0;
                        if (sscanf(resolution, "%dx%d", &width, &height) == 2)
                            G_SetCommandLineResolution(width, height);
                    }
                    i++;
                    continue;
                }
                // --zeroping (also accepts -zeroping): client-side shot
                // prediction so hitscan/projectile fire feels instant in
                // multiplayer. Purely local; each player sets it independently.
                if (!Bstrcasecmp(c + 1, "zeroping") || !Bstrcasecmp(c + 1, "-zeroping"))
                {
                    g_netZeroPing = 1;
                    LOG_F(INFO, "ZeroPing shot prediction enabled.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "x") || !Bstrcasecmp(c + 1, "con"))   // VibeBuild32 canonical: --con
                {
                    if (argc > i + 1)
                    {
                        G_AddCon(argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "mx"))
                {
                    if (argc > i + 1)
                    {
                        G_AddConModule(argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
#if !defined(_WIN32)
                if (!Bstrcasecmp(c + 1, "usecwd"))
                {
                    g_useCwd = 1;
                    i++;
                    continue;
                }
#endif
                if (!Bstrcasecmp(c + 1, "unstable"))
                {
                    LOG_F(WARNING, "WARNING WARNING WARNING\n"
                        "EDuke32's runtime script error detection has been disabled via "
                        "the '-unstable' command line parameter.  Bug reports from this "
                        "mode are NOT welcome and you should expect crashes in certain "
                        "mods.  Please run EDuke32 without '-unstable' before sending "
                        "any bug reports.\n");
                    g_scriptSanityChecks = 0;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "cachesize"))
                {
                    if (argc > i + 1)
                    {
                        unsigned int j = atol(argv[i + 1]);
                        MAXCACHE1DSIZE = j << 10;
                        LOG_F(INFO, "Cache size: %dkB", j);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "noinstancechecking"))
                {
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "latejoin") || !Bstrcasecmp(c + 1, "lj"))
                {
                    // Host: start once at least one other player is present
                    // and let the remaining slots join mid-game.
                    g_netLateJoin = TRUE;
                    LOG_F(INFO, "Late joining enabled.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "startnow") || !Bstrcasecmp(c + 1, "sn") || !Bstrcasecmp(c + 1, "nowait"))
                {
                    // Host: begin the match immediately with whoever is present
                    // (even just the host alone) instead of waiting for the
                    // slot count named by /nX:N. The remaining slots stay open
                    // for players to join at any time, so this implies late
                    // joining. Works for both async and lockstep Master/Slave.
                    g_netStartNow = TRUE;
                    g_netLateJoin = TRUE;
                    LOG_F(INFO, "Start-now enabled: match begins immediately, players may join anytime.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "async") || !Bstrcasecmp(c + 1, "as"))
                {
                    // Host: server-authoritative netplay - the master never
                    // blocks on slow clients (vs lockstep's equal-lag model).
                    g_netAsyncMode = TRUE;
                    // Server-authoritative: the master picks each tick's canonical
                    // input (a slow client's is substituted, then applied when it
                    // arrives) and every machine - including the client itself -
                    // simulates from that, so the game never stalls on a laggard
                    // and stays in sync. Trade-off vs lockstep: a client's own
                    // input has a server round-trip of latency (no local
                    // prediction yet) instead of everyone sharing the worst lag.
                    LOG_F(INFO, "Asynchronous (server-authoritative) netplay enabled.");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "dedicated") || !Bstrcasecmp(c + 1, "dd"))
                {
                    // Host: read admin commands from stdin, log to stdout. The
                    // operator has no view and no speakers, so run fully headless
                    // - no window, no OpenGL/graphics context, no audio device.
                    // (The graphics skip itself is done in the startup layer,
                    // which scans argv for -dedicated directly; here we set the
                    // same flag for app_main's checks and mute audio.)
                    g_forceHeadless = TRUE;
                    g_netServerConsole = TRUE;
                    g_noSound = TRUE;
                    g_noMusic = TRUE;
                    LOG_F(INFO, "Dedicated server enabled (headless, reading stdin).");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "advertise") || !Bstrcasecmp(c + 1, "ad"))
                {
                    // Host: publish this server to a masterserver so it shows up
                    // in the in-game lobby. Next arg is the masterserver
                    // host[:port] (IPv4, [IPv6] or a hostname).
                    if (i + 1 < argc && argv[i + 1][0])
                    {
                        Masterserver_SetAdvertise(argv[i + 1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "punchtarget") || !Bstrcasecmp(c + 1, "pt"))
                {
                    // Client: the "host:port" this client is joining, so the
                    // connection loop can request a masterserver NAT hole-punch
                    // to it if the direct connect stalls. Set on lobby/manual
                    // join relaunch.
                    if (i + 1 < argc && argv[i + 1][0])
                    {
                        Bstrncpyz(g_punchTargetAddr, argv[i + 1], sizeof(g_punchTargetAddr));
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "masteraddr") || !Bstrcasecmp(c + 1, "ma"))
                {
                    // Client: the masterserver address the lobby used, carried
                    // across the join relaunch so the connecting client contacts
                    // the same master (for NAT-punch) instead of its own default.
                    if (i + 1 < argc && argv[i + 1][0])
                    {
                        Bstrncpyz(g_masterServerAddr, argv[i + 1], sizeof(g_masterServerAddr));
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "joinerror") || !Bstrcasecmp(c + 1, "je"))
                {
                    // Client: a message describing why the previous join failed,
                    // shown once in the JOIN GAME menu after relaunching there.
                    if (i + 1 < argc && argv[i + 1][0])
                    {
                        Bstrncpyz(g_joinErrorMsg, argv[i + 1], sizeof(g_joinErrorMsg));
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "headless") || !Bstrcasecmp(c + 1, "hl") || !Bstrcasecmp(c + 1, "console"))
                {
                    // Dedicated console server with no graphics at all: no
                    // window, no OpenGL, no sound - just the terminal. Implies
                    // -dedicated. (The bare-video skip itself is handled in the
                    // startup layer, which scans argv for this flag directly;
                    // here we also enable the console and mute audio.)
                    g_forceHeadless = TRUE;
                    g_netServerConsole = TRUE;
                    g_noSound = TRUE;
                    g_noMusic = TRUE;
                    LOG_F(INFO, "Headless dedicated server: no video, no sound (terminal only).");
                    i++;
                    continue;
                }
#if defined(RENDERTYPEWIN) && defined(USE_OPENGL)
                if (!Bstrcasecmp(c + 1, "forcegl"))
                {
                    forcegl = 1;
                    i++;
                    continue;
                }
#endif
            }

            if (firstnet > 0)
            {
                if (*c == '-' || *c == '/')
                {
                    c++;
                    if (((c[0] == 'n') || (c[0] == 'N')) && (c[1] == '0'))
                    {
                        g_networkBroadcastMode = NETMODE_MASTERSLAVE;
                        LOG_F(INFO, "Network mode: master/slave");
                    }
                    else if (((c[0] == 'n') || (c[0] == 'N')) && (c[1] == '1'))
                    {
                        g_networkBroadcastMode = NETMODE_P2P;
                        LOG_F(INFO, "Network mode: peer-to-peer");
                    }

                }
                netparam[netparamcount++] = const_cast<char*>(argv[i]);
                i++;
                continue;
            }

            // VibeBuild32: canonical long aliases for value-taking single-char options.
            // Normalize "--name value", "--name=value", "--namevalue", "-x value",
            // "-x=value", and "-xvalue" into the attached short form handled below.
            static char s_optbuf[BMAX_PATH + 4];
            if (c[0] == '-')
            {
                static const struct { const char *lng; char shrt; } aliases[] = {
                    { "level", 'l' }, { "volume", 'v' }, { "episode", 'v' }, { "skill", 's' }, { "coop", 'c' },
                    { "players", 'q' }, { "respawn", 't' }, { "weapons", 'u' }, { "def", 'h' },
                    { "path", 'j' }, { "demo", 'd' },
                };
                const char *option = c + 1;

                for (auto const &alias : aliases)
                {
                    const char shortName[2] = { alias.shrt, '\0' };
                    const char *value = G_CommandLineOptionValue(option, shortName, alias.lng, &i, argc, argv);

                    if (value == nullptr)
                        continue;

                    Bsnprintf(s_optbuf, sizeof(s_optbuf), "-%c%s", alias.shrt, value);
                    c = s_optbuf;
                    break;
                }
            }

            if ((*c == '/') || (*c == '-'))
            {
                c++;
                switch (*c)
                {
                    case 'a':
                    case 'A':
                        ud.playerai = 1;
                        LOG_F(INFO, "Other player AI.");
                        break;
                    case 'c':
                    case 'C':
                        c++;
                        ud.m_coop = 0;
                        while ((*c >= '0') && (*c <= '9'))
                        {
                            ud.m_coop *= 10;
                            ud.m_coop += *c - '0';
                            c++;
                        }
                        ud.m_coop = clamp<int>(ud.m_coop - 1, 0, MAXGAMETYPES - 1);

                        // Make sure that COOP still defaults to weapon stay ON.
                        if (Gametypes[ud.m_coop].flags & GAMETYPE_WEAPSTAY)
                        {
                            //ud.m_weaponstay = 1;
                            M_DMFLAGS_SET(DMFLAG_WEAPONSTAY, true);
                        }
                        break;
                    case 'd':
                    case 'D':
                        c++;
                        if (strchr(c, '.') == 0)
                            Bstrcat(c, ".dmo");
                        LOG_F(INFO, "Play demo %s.", c);
                        Bstrcpy(firstdemofile, c);
                        break;
                    case 'g':
                    case 'G':
                        c++;
                        if (!*c) break;
                        G_AddGroup(c);
                        break;
                    case 'h':
                    case 'H':
                        c++;
                        if (*c)
                        {
                            G_AddDef(c);
                        }
                        break;
                    case 'j':
                    case 'J':
                        c++;
                        if (!*c) break;
                        G_AddPath(c);
                        break;
                    case 'l':
                    case 'L':
                        ud.warp_on = 1;
                        c++;
                        ud.m_level_number = ud.level_number = (atoi(c) - 1) % MAXLEVELS;
                        break;
                    case 'm':
                    case 'M':
                        if (*(c + 1) != 'a' && *(c + 1) != 'A')
                        {
                            //ud.m_monsters_off = 1;
                            M_DMFLAGS_SET(DMFLAG_NOMONSTERS, true);
                            ud.m_player_skill = ud.player_skill = 0;
                            LOG_F(INFO, "Monsters off.");
                        }
                        break;
                    case 'n':
                    case 'N':
                        c++;
                        if (*c == 's' || *c == 'S')
                        {
                            g_noSound = 2;
                            LOG_F(INFO, "Sound off.");
                        }
                        else if (*c == 'm' || *c == 'M')
                        {
                            g_noMusic = 1;
                            LOG_F(INFO, "Music off.");
                        }
                        else if (*c == 'd' || *c == 'D')
                        {
                            // FILE * fp=fopen("gamevars.txt","w");
                            Gv_Init();
                            Gv_DumpValues();
                            // fclose(fp);
                            LOG_F(INFO, "Game variables saved.");
                        }
                        else
                        {
                            G_ShowParameterHelp();
                            Bexit(-1);
                        }
                        break;
                    case 'q':
                    case 'Q':
                    {
                        LOG_F(INFO, "Fake multiplayer mode.");

                        if (*(++c) == 0) ud.multimode = 1;
                        else ud.multimode = atoi(c) % (MAXPLAYERS + 1);

                        //ud.m_coop = ud.coop = 0;
                        M_DMFLAGS_SET(DMFLAG_MARKERS | DMFLAG_RESPAWNITEMS | DMFLAG_RESPAWNINVENTORY, true);
                        break;
                    }
                    case 'r':
                    case 'R':
                        ud.m_recstat = 1;
                        LOG_F(INFO, "Demo record mode on.");
                        break;
                    case 's':
                    case 'S':
                        c++;
                        ud.m_player_skill = ud.player_skill = (atoi(c) % 5);
                        break;
                    case 't':
                    case 'T':
                        c++;
                        if (Bstrstr(c, "x") != NULL)
                        {
                            LOG_F(INFO, "Respawn All (Items/Inventory/Monsters).");
                            M_DMFLAGS_SET(DMFLAG_RESPAWNMONSTERS | DMFLAG_RESPAWNITEMS | DMFLAG_RESPAWNINVENTORY, true);
                        }
                        else
                        {
                            if (Bstrstr(c, "1") != NULL)
                            {
                                LOG_F(INFO, "Respawn Monsters.");
                                M_DMFLAGS_SET(DMFLAG_RESPAWNMONSTERS, true);
                            }

                            if (Bstrstr(c, "2") != NULL)
                            {
                                LOG_F(INFO, "Respawn Items.");
                                M_DMFLAGS_SET(DMFLAG_RESPAWNITEMS, true);
                            }

                            if (Bstrstr(c, "3") != NULL)
                            {
                                LOG_F(INFO, "Respawn Inventory.");
                                M_DMFLAGS_SET(DMFLAG_RESPAWNINVENTORY, true);
                            }
                        }

                        break;
                    case 'u':
                    case 'U':
                        c++;
                        j = 0;
                        if (*c)
                        {
                            // Only accept numbers from 0 to 9
                            while ((*c >= '0') && (*c <= '9') && j < min<int>(MAX_WEAPONS, 10))
                            {
                                g_player[0].wchoice[j] = *c - '0';
                                ud.wchoice[j] = *c;
                                c++;
                                j++;
                            }

                            // Apply weapon priority if a valid value is given
                            if (j > 0)
                            {
                                CommandWeaponChoice = 1;
                                LOG_F(INFO, "Using favorite weapon order(s).");
                                // Pad value when not all weapons are given
                                while (j < 10)
                                {
                                    if (j == 9)
                                    {
                                        g_player[0].wchoice[9] = 1;
                                        ud.wchoice[9] = '1';
                                    }
                                    else
                                    {
                                        g_player[0].wchoice[j] = 2;
                                        ud.wchoice[j] = '2';
                                    }

                                    j++;
                                }
                            }
                        }

                        if (!CommandWeaponChoice)
                        {
                            LOG_F(INFO, "Using default weapon orders.");
                            g_player[0].wchoice[0] = 3;
                            g_player[0].wchoice[1] = 4;
                            g_player[0].wchoice[2] = 5;
                            g_player[0].wchoice[3] = 7;
                            g_player[0].wchoice[4] = 8;
                            g_player[0].wchoice[5] = 6;
                            g_player[0].wchoice[6] = 0;
                            g_player[0].wchoice[7] = 2;
                            g_player[0].wchoice[8] = 9;
                            g_player[0].wchoice[9] = 1;
                            Bsnprintf(ud.wchoice, sizeof(ud.wchoice), "3457860291");
                        }
                        break;
                    case 'v':
                    case 'V':
                        c++;
                        ud.warp_on = 1;
                        ud.m_volume_number = ud.volume_number = atoi(c) - 1;
                        break;
                    case 'w':
                    case 'W':
                        ud.coords = 1;
                        break;
                    case 'y':
                    case 'Y':
                        c++;
                        ud.fraglimit = atoi(c);
                        break;
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        ud.warp_on = 2 + (*c) - '0';
                        break;
                    case 'z':
                    case 'Z':
                        c++;
                        g_scriptDebug = atoi(c);
                        if (!g_scriptDebug)
                            g_scriptDebug = 1;
                        break;
                }
            }
            else
            {
                k = Bstrchr(c, '.');
                if (k)
                {
                    if (!Bstrcasecmp(k, ".map"))
                    {
                        CommandMap = argv[i++];
                        continue;
                    }
                    if (!Bstrcasecmp(k, ".grp") || !Bstrcasecmp(k, ".zip") || !Bstrcasecmp(k, ".pk3"))
                    {
                        G_AddGroup(argv[i++]);
                        continue;
                    }
                    if (!Bstrcasecmp(k, ".con"))
                    {
                        G_AddCon(const_cast<char*>(argv[i++]));
                        userconfiles = 1;
                        continue;
                    }
                    if (!Bstrcasecmp(k, ".def"))
                    {
                        G_AddDef(argv[i++]);
                        continue;
                    }
                    if (!Bstrcasecmp(k, ".rts"))
                    {
                        Bfree(g_rtsNamePtr);
                        g_rtsNamePtr = dup_filename(argv[i++]);
                        LOG_F(INFO, "Using RTS file \"%s\".", g_rtsNamePtr);
                        continue;
                    }
                }
            }
            i++;
        }
    }
}
