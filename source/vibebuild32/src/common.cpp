//
// Common non-engine code/data for EDuke32 and Mapster32
//

#include "common.h"

#include "baselayer.h"
#include "build.h"
#include "common_game.h"
#include "compat.h"
#include "grpscan.h"
#include "palette.h"
#include "texcache.h"
#include "vfs.h"

// Defined in oldnet.cpp (multiplayer). Declared here to avoid pulling the full
// multiplayer header into build-layer common code.
#define MAXNETMODFILES 8
extern char g_netModFiles[MAXNETMODFILES][BMAX_PATH];
extern int32_t g_netModFileCount;

#ifdef _WIN32
# include "windows_inc.h"
# include "winbits.h"
#elif defined __APPLE__
# include "osxbits.h"
#endif

#define DUKE_COMMON_DEFAULT_DEF_FILE_SYMBOL Duke_G_DefaultDefFile
#include "../../build/include/duke_common_defaults_shared.cpp.inc"
#undef DUKE_COMMON_DEFAULT_DEF_FILE_SYMBOL

//////////

#include "../../build/include/duke_common_psky_shared.cpp.inc"

//////////

static char g_rootDir[BMAX_PATH];

int g_useCwd;
static void G_LoadAddon(void);
int32_t g_groupFileHandle;

static struct strllist *CommandPaths, *CommandGrps;

void G_ExtPreInit(int32_t argc,char const * const * argv)
{
    g_useCwd = G_CheckCmdSwitch(argc, argv, "-usecwd");

#ifdef _WIN32
    GetModuleFileName(NULL,g_rootDir,BMAX_PATH);
    Bcorrectfilename(g_rootDir,1);
    //buildvfs_chdir(g_rootDir);
#else
    buildvfs_getcwd(g_rootDir,BMAX_PATH);
    strcat(g_rootDir,"/");
#endif
}

void G_ExtInit(void)
{
#ifdef EDUKE32_OSX
    char *appdir = Bgetappdir();
    addsearchpath(appdir);
    Xfree(appdir);
#endif

    char cwd[BMAX_PATH];
#ifdef USE_PHYSFS
    strncpy(cwd, PHYSFS_getBaseDir(), ARRAY_SIZE(cwd));
    cwd[ARRAY_SIZE(cwd)-1] = '\0';
#else
    if (buildvfs_getcwd(cwd, ARRAY_SIZE(cwd)) && Bstrcmp(cwd, "/") != 0)
#endif
        addsearchpath(cwd);

    if (CommandPaths)
    {
        int32_t i;
        struct strllist *s;
        while (CommandPaths)
        {
            s = CommandPaths->next;
            i = addsearchpath(CommandPaths->str);
            if (i < 0)
            {
                LOG_F(ERROR, "Failed adding %s for game data: %s", CommandPaths->str,
                           i==-1 ? "not a directory" : "no such directory");
            }

            Xfree(CommandPaths->str);
            Xfree(CommandPaths);
            CommandPaths = s;
        }
    }

#if defined(_WIN32) && !defined(EDUKE32_STANDALONE)
    if (buildvfs_exists("user_profiles_enabled"))
#else
    if (g_useCwd == 0 && !buildvfs_exists("user_profiles_disabled"))
#endif
    {
        char *homedir;
        int32_t asperr;

        if ((homedir = Bgethomedir()))
        {
            Bsnprintf(cwd, ARRAY_SIZE(cwd), "%s/"
#if defined(_WIN32)
                      APPNAME
#elif defined(GEKKO)
                      "apps/" APPBASENAME
#else
                      ".config/" APPBASENAME
#endif
                      ,homedir);
            asperr = addsearchpath(cwd);
            if (asperr == -2)
            {
                if (buildvfs_mkdir(cwd,S_IRWXU) == 0) asperr = addsearchpath(cwd);
                else asperr = -1;
            }
            if (asperr == 0)
                buildvfs_chdir(cwd);
            Xfree(homedir);
        }
    }

    // JBF 20031220: Because it's annoying renaming GRP files whenever I want to test different game data
#ifndef EDUKE32_STANDALONE
    if (g_grpNamePtr == NULL)
    {
        const char *cp = getenv("DUKE3DGRP");
        if (cp)
        {
            clearGrpNamePtr();
            g_grpNamePtr = dup_filename(cp);
            LOG_F(INFO, "Using %s as main data file (DUKE3DGRP env var override).", g_grpNamePtr);
        }
    }
#endif
}

void G_ScanGroups(void)
{
    ScanGroups();

    g_selectedGrp = NULL;

    char const * const currentGrp = G_GrpFile();

    for (grpfile_t const *fg = foundgrps; fg; fg=fg->next)
    {
        if (!Bstrcasecmp(fg->filename, currentGrp))
        {
            g_selectedGrp = fg;
            break;
        }
    }

    if (g_selectedGrp == NULL)
        g_selectedGrp = foundgrps;
}

#define DUKE_COMMON_AFTER_COMMAND_GRP_LOAD(path) do { \
    if (g_netModFileCount < MAXNETMODFILES) \
        Bstrncpyz(g_netModFiles[g_netModFileCount++], (path), BMAX_PATH); \
} while (0)
#include "../../build/include/duke_common_grpload_shared.cpp.inc"
#undef DUKE_COMMON_AFTER_COMMAND_GRP_LOAD

#ifndef EDUKE32_STANDALONE
#ifndef EDUKE32_TOUCH_DEVICES

#if defined __linux__ || defined EDUKE32_BSD
static void Duke_Add_GOG_Atomic_Linux(const char * path)
{
    char buf[BMAX_PATH];

    Bsnprintf(buf, sizeof(buf), "%s/data", path);
    addsearchpath_user(buf, SEARCHPATH_REMOVE);
}
static void Fury_Add_GOG_Linux(const char * path)
{
    char buf[BMAX_PATH];

    Bsnprintf(buf, sizeof(buf), "%s/game", path);
    addsearchpath(buf);
}
#endif

#if defined EDUKE32_OSX || defined __linux__ || defined EDUKE32_BSD
static void Duke_AddSteamPaths(const char *basepath)
{
    char buf[BMAX_PATH];

    // Duke Nukem 3D: Megaton Edition - Steam
    static char const s_Megaton_Steam[] = "steamapps/common/Duke Nukem 3D/gameroot";
    Bsnprintf(buf, sizeof(buf), "%s/%s", basepath, s_Megaton_Steam);
    addsearchpath(buf);
    Bsnprintf(buf, sizeof(buf), "%s/%s/addons/dc", basepath, s_Megaton_Steam);
    addsearchpath_user(buf, SEARCHPATH_REMOVE);
    Bsnprintf(buf, sizeof(buf), "%s/%s/addons/nw", basepath, s_Megaton_Steam);
    addsearchpath_user(buf, SEARCHPATH_REMOVE);
    Bsnprintf(buf, sizeof(buf), "%s/%s/addons/vacation", basepath, s_Megaton_Steam);
    addsearchpath_user(buf, SEARCHPATH_REMOVE);

    // Duke Nukem 3D - 3D Realms Anthology / Kill-A-Ton Collection 2015 - Steam
#if defined EDUKE32_OSX
    Bsnprintf(buf, sizeof(buf), "%s/steamapps/common/Duke Nukem 3D/Duke Nukem 3D.app/drive_c/Program Files/Duke Nukem 3D", basepath);
    addsearchpath_user(buf, SEARCHPATH_REMOVE);
#endif

    // NAM - Steam
#if defined EDUKE32_OSX
    Bsnprintf(buf, sizeof(buf), "%s/steamapps/common/Nam/Nam.app/Contents/Resources/Nam.boxer/C.harddisk/NAM", basepath);
#else
    Bsnprintf(buf, sizeof(buf), "%s/steamapps/common/Nam/NAM", basepath);
#endif
    addsearchpath_user(buf, SEARCHPATH_NAM);

    // WWII GI - Steam
    Bsnprintf(buf, sizeof(buf), "%s/steamapps/common/World War II GI/WW2GI", basepath);
    addsearchpath_user(buf, SEARCHPATH_WW2GI);

    // Ion Fury - Steam
    Bsnprintf(buf, sizeof(buf), "%s/steamapps/common/Ion Fury", basepath);
    addsearchpath_user(buf, SEARCHPATH_FURY);
}
#endif
#endif
#endif

void G_AddSearchPaths(void)
{
#ifndef EDUKE32_STANDALONE
#ifndef EDUKE32_TOUCH_DEVICES
#if defined __linux__ || defined EDUKE32_BSD
    char buf[BMAX_PATH];
    char *homepath = Bgethomedir();

    Bsnprintf(buf, sizeof(buf), "%s/.steam/steam", homepath);
    Duke_AddSteamPaths(buf);

    Bsnprintf(buf, sizeof(buf), "%s/.steam/steam/steamapps/libraryfolders.vdf", homepath);
    Paths_ParseSteamLibraryVDF(buf, Duke_AddSteamPaths);

    // Duke Nukem 3D: Atomic Edition - GOG.com
    Bsnprintf(buf, sizeof(buf), "%s/GOG Games/Duke Nukem 3D Atomic Edition", homepath);
    Duke_Add_GOG_Atomic_Linux(buf);
    Paths_ParseXDGDesktopFilesFromGOG(homepath, "Duke_Nukem_3D_Atomic_Edition", Duke_Add_GOG_Atomic_Linux);

    // Ion Fury - GOG.com
    Bsnprintf(buf, sizeof(buf), "%s/GOG Games/ION Fury", homepath);
    Fury_Add_GOG_Linux(buf);
    Paths_ParseXDGDesktopFilesFromGOG(homepath, "ION_Fury", Fury_Add_GOG_Linux);

    Xfree(homepath);

    addsearchpath("/usr/share/games/jfduke3d");
    addsearchpath("/usr/local/share/games/jfduke3d");
    addsearchpath("/usr/share/games/eduke32");
    addsearchpath("/usr/local/share/games/eduke32");
#elif defined EDUKE32_OSX
    char buf[BMAX_PATH];
    int32_t i;
    char *applications[] = { osx_getapplicationsdir(0), osx_getapplicationsdir(1) };
    char *support[] = { osx_getsupportdir(0), osx_getsupportdir(1) };
    char *documents[] = { osx_getdocumentsdir(0), osx_getdocumentsdir(1) };

    for (i = 0; i < 2; i++)
    {
        Bsnprintf(buf, sizeof(buf), "%s/Steam", support[i]);
        Duke_AddSteamPaths(buf);

        Bsnprintf(buf, sizeof(buf), "%s/Steam/steamapps/libraryfolders.vdf", support[i]);
        Paths_ParseSteamLibraryVDF(buf, Duke_AddSteamPaths);

        // Duke Nukem 3D: Atomic Edition - GOG.com
        Bsnprintf(buf, sizeof(buf), "%s/Duke Nukem 3D.app/Contents/Resources/Duke Nukem 3D.boxer/C.harddisk", applications[i]);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
        Bsnprintf(buf, sizeof(buf), "%s/Duke Nukem 3D.app/Contents/Resources/game/Duke Nukem 3D.app/Contents/Resources/Duke Nukem 3D.boxer/C.harddisk", applications[i]);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);

        // Duke Nukem 3D: Atomic Edition - ZOOM Platform
        Bsnprintf(buf, sizeof(buf), "%s/Duke Nukem 3D - Atomic Edition.app/Contents/MacOS/Duke3D - Atomic Edition", applications[i]);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);

        // NAM - GOG.com
        Bsnprintf(buf, sizeof(buf), "%s/NAM.app/Contents/Resources/game", applications[i]);
        addsearchpath_user(buf, SEARCHPATH_NAM);
        Bsnprintf(buf, sizeof(buf), "%s/NAM.app/Contents/Resources/game/NAM.app/Contents/Resources/game", applications[i]);
        addsearchpath_user(buf, SEARCHPATH_NAM);
        Bsnprintf(buf, sizeof(buf), "%s/NAM.app/Contents/Resources/game", documents[i]);
        addsearchpath_user(buf, SEARCHPATH_NAM);
    }

    for (i = 0; i < 2; i++)
    {
        Bsnprintf(buf, sizeof(buf), "%s/JFDuke3D", support[i]);
        addsearchpath(buf);
        Bsnprintf(buf, sizeof(buf), "%s/EDuke32", support[i]);
        addsearchpath(buf);
    }

    for (i = 0; i < 2; i++)
    {
        Xfree(applications[i]);
        Xfree(support[i]);
        Xfree(documents[i]);
    }
#elif defined (_WIN32)
    char buf[BMAX_PATH] = {0};
    DWORD bufsize;

    // Duke Nukem 3D: 20th Anniversary World Tour - Steam
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 434050)", "InstallLocation", buf, &bufsize))
    {
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // Duke Nukem 3D: Megaton Edition - Steam
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 225140)", "InstallLocation", buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/gameroot", remaining);
        addsearchpath(buf);
        Bstrncpy(suffix, "/gameroot/addons/dc", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
        Bstrncpy(suffix, "/gameroot/addons/nw", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
        Bstrncpy(suffix, "/gameroot/addons/vacation", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // Duke Nukem 3D - 3D Realms Anthology / Kill-A-Ton Collection 2015 - Steam
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 359850)", "InstallLocation", buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/Duke Nukem 3D", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // Duke Nukem 3D: Atomic Edition - GOG.com
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\GOG.com\Games\1207658730)", "path", buf, &bufsize))
    {
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue("SOFTWARE\\GOG.com\\GOGDUKE3D", "PATH", buf, &bufsize))
    {
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // Duke Nukem 3D: Atomic Edition - ZOOM Platform
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\ZOOM PLATFORM\Duke Nukem 3D - Atomic Edition)", "InstallPath", buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        addsearchpath_user(buf, SEARCHPATH_REMOVE);

        Bstrncpy(suffix, "/AddOns", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // Duke Nukem 3D - 3D Realms Anthology
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue("SOFTWARE\\3DRealms\\Duke Nukem 3D", NULL, buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/Duke Nukem 3D", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // 3D Realms Anthology
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue("SOFTWARE\\3DRealms\\Anthology", NULL, buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/Duke Nukem 3D", remaining);
        addsearchpath_user(buf, SEARCHPATH_REMOVE);
    }

    // NAM - Steam
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 329650)", "InstallLocation", buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/NAM", remaining);
        addsearchpath_user(buf, SEARCHPATH_NAM);
    }

    // NAM - GOG.com
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\GOG.com\Games\1575726518)", "path", buf, &bufsize))
    {
        addsearchpath_user(buf, SEARCHPATH_NAM);
    }

    // WWII GI - Steam
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 376750)", "InstallLocation", buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/WW2GI", remaining);
        addsearchpath_user(buf, SEARCHPATH_WW2GI);
    }

    // WWII GI - GOG.com
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\GOG.com\Games\1625778777)", "path", buf, &bufsize))
    {
        char * const suffix = buf + bufsize - 1;
        DWORD const remaining = sizeof(buf) - bufsize;

        Bstrncpy(suffix, "/WW2GI", remaining);
        addsearchpath_user(buf, SEARCHPATH_WW2GI);
    }

    // Ion Fury - Steam
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 562860)", "InstallLocation", buf, &bufsize))
    {
        addsearchpath_user(buf, SEARCHPATH_FURY);
    }

    // Ion Fury - GOG.com
    bufsize = sizeof(buf);
    if (Paths_ReadRegistryValue(R"(SOFTWARE\GOG.com\Games\1740836875)", "path", buf, &bufsize))
    {
        addsearchpath_user(buf, SEARCHPATH_FURY);
    }
#endif
#endif
#endif
}

#include "../../build/include/duke_common_modules_shared.cpp.inc"

//////////

#include "../../build/include/duke_common_loading_shared.cpp.inc"

//////////

#ifdef FORMAT_UPGRADE_ELIGIBLE
int g_maybeUpgradeSoundFormats = 1;

static buildvfs_kfd S_TryFormats(char * const testfn, char * const fn_suffix, char const searchfirst)
{
    if (!g_maybeUpgradeSoundFormats)
        return buildvfs_kfd_invalid;

    static char const * extensions[] =
    {
#ifdef HAVE_FLAC
        ".flac",
#endif
#ifdef HAVE_VORBIS
        ".ogg",
#endif
    };

    for (char const * ext : extensions)
    {
        Bstrcpy(fn_suffix, ext);
        buildvfs_kfd const fp = kopen4loadfrommod(testfn, searchfirst);
        if (fp != buildvfs_kfd_invalid)
            return fp;
    }

    return buildvfs_kfd_invalid;
}

static buildvfs_kfd S_TryExtensionReplacements(char * const testfn, char const searchfirst, uint8_t const ismusic)
{
    char * extension = Bstrrchr(testfn, '.');
    char * const fn_end = Bstrchr(testfn, '\0');

    // ex: grabbag.voc --> grabbag_voc.*
    if (extension != NULL)
    {
        *extension = '_';

        buildvfs_kfd const fp = S_TryFormats(testfn, fn_end, searchfirst);
        if (fp != buildvfs_kfd_invalid)
            return fp;
    }
    else
    {
        extension = fn_end;
    }

    // ex: grabbag.mid --> grabbag.*
    if (ismusic)
    {
        buildvfs_kfd const fp = S_TryFormats(testfn, extension, searchfirst);
        if (fp != buildvfs_kfd_invalid)
            return fp;
    }

    return buildvfs_kfd_invalid;
}

buildvfs_kfd S_OpenAudio(const char *fn, char searchfirst, uint8_t const ismusic)
{
    buildvfs_kfd const origfp      = kopen4loadfrommod(fn, searchfirst);
#ifndef USE_PHYSFS
    char const * const origparent  = origfp != buildvfs_kfd_invalid ? kfileparent(origfp) : NULL;
    uint32_t const    parentlength = origparent != NULL ? Bstrlen(origparent) : 0;

    auto testfn = (char *)Xmalloc(Bstrlen(fn) + 12 + parentlength); // "music/" + overestimation of parent minus extension + ".flac" + '\0'
#else
    auto testfn = (char *)Xmalloc(Bstrlen(fn) + 12);
#endif

    // look in ./
    // ex: ./grabbag.mid
    Bstrcpy(testfn, fn);
    buildvfs_kfd fp = S_TryExtensionReplacements(testfn, searchfirst, ismusic);
    if (fp != buildvfs_kfd_invalid)
        goto success;

#ifndef USE_PHYSFS
    // look in ./music/<file's parent GRP name>/
    // ex: ./music/duke3d/grabbag.mid
    // ex: ./music/nwinter/grabbag.mid
    if (origparent != NULL)
    {
        char const * const parentextension = Bstrrchr(origparent, '.');
        uint32_t const namelength = parentextension != NULL ? (unsigned)(parentextension - origparent) : parentlength;

        Bsprintf(testfn, "music/%.*s/%s", namelength, origparent, fn);
        fp = S_TryExtensionReplacements(testfn, searchfirst, ismusic);
        if (fp != buildvfs_kfd_invalid)
            goto success;
    }

    // look in ./music/
    // ex: ./music/grabbag.mid
    Bsprintf(testfn, "music/%s", fn);
    fp = S_TryExtensionReplacements(testfn, searchfirst, ismusic);
    if (fp != buildvfs_kfd_invalid)
        goto success;
#endif

    Xfree(testfn);
    return origfp;

success:
    Xfree(testfn);
    kclose(origfp);
    return fp;
}

#endif

void Duke_CommonCleanup(void)
{
    DO_FREE_AND_NULL(g_grpNamePtr);
    DO_FREE_AND_NULL(g_scriptNamePtr);
    DO_FREE_AND_NULL(g_rtsNamePtr);
}
