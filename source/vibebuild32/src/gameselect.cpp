// Game selector: one vibebuild32 binary that can launch Duke3D-family games,
// Ion Fury, Shadow Warrior (VoidSW), Blood, Redneck Rampage, and
// PCExhumed/PowerSlave, sharing the engine.
//
// The engine's main() calls app_main(); this file provides that app_main as a
// dispatcher. Each game's real entry and the few engine callbacks that both
// games define (see UNIFY_WITH_VIBEBUILD32.md) are renamed Duke_*/SW_*, and the
// canonical names here route to the active game.
//
// Selection:
//   --game/-gm <duke|worldtour|ionfury|sw|blood|rr|nam|napalm|ww2gi|exhumed>  pick a game explicitly
//   -g/--grp/-gamegrp <data-file>         infer the game from known data files
//   --no-game-menu/-ngm             bypass the picker
// The canonical data file is looked for both in the working directory and in a
// per-game subdir (duke3d/DUKE3D.GRP, sw/Sw.grp, etc.); the chosen game
// switches into that folder.
//
// Extra games are linked and dispatched when built with their WITH_* flags.  The
// default VibeBuild32 make target enables those flags, while explicit minimal
// builds still fall back to Duke for games that are not linked.

#include "compat.h"
#include "baselayer.h"
#include "common_game.h"
#include "crc32.h"
#include "vbjs.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
# include <direct.h>
# define gs_chdir _chdir
#else
# include <unistd.h>
# define gs_chdir chdir
#endif

enum
{
    GS_GAME_DUKE  = 0,
    GS_GAME_WORLDTOUR = 1,
    GS_GAME_SW    = 2,
    GS_GAME_BLOOD = 3,
    GS_GAME_RR    = 4,
    GS_GAME_NAM   = 5,
    GS_GAME_NAPALM = 6,
    GS_GAME_WW2GI = 7,
    GS_GAME_EXHUMED = 8,
    GS_GAME_IONFURY = 9,
    GS_GAME_COUNT = 10,
};

// 0 = Duke3D (vibebuild32), 1 = Duke3D World Tour, 2 = Shadow Warrior,
// 3 = Blood, 4 = Redneck Rampage, 5 = NAM, 6 = Napalm,
// 7 = World War II GI (Duke/NAM-family runtime), 8 = Powerslave/Exhumed,
// 9 = Ion Fury (Duke-family runtime).
// Read by the dispatchers.
int g_activeGame = GS_GAME_DUKE;
static int g_crossplayRequestedDomainGames[ART_CROSSPLAY_DOMAIN_COUNT] =
{
    ART_CROSSPLAY_GAME_UNKNOWN,
    ART_CROSSPLAY_GAME_UNKNOWN,
    ART_CROSSPLAY_GAME_UNKNOWN,
};

// --- each game's real entry + the callbacks both games define (renamed) -------
extern int Duke_app_main(int32_t argc, char const * const * argv);
extern const char *Duke_G_DefaultDefFile(void);
extern void Duke_faketimerhandler(void);
#ifdef WITH_SW
extern int SW_app_main(int32_t argc, char const * const * argv);
extern const char *SW_G_DefaultDefFile(void);
extern void SW_faketimerhandler(void);
#endif
#ifdef WITH_BLOOD
extern int Blood_app_main(int32_t argc, char const * const * argv);
extern const char *Blood_G_DefaultDefFile(void);
extern void Blood_faketimerhandler(void);
#endif
#ifdef WITH_RR
extern int RR_app_main(int32_t argc, char const * const * argv);
extern const char *RR_G_DefaultDefFile(void);
extern void RR_faketimerhandler(void);
#endif
#ifdef WITH_EXHUMED
extern int Exhumed_app_main(int argc, char const * const * argv);
extern const char *Exhumed_G_DefaultDefFile(void);
extern void Exhumed_faketimerhandler(void);
#endif

// --- helpers -----------------------------------------------------------------
static int gs_fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int gs_directoryExists(const char *path)
{
    DIR *directory = opendir(path);
    if (!directory) return 0;
    closedir(directory);
    return 1;
}

static char const * const gs_dukeDataFiles[]  = { "DUKE3D.GRP", "duke3d.grp", NULL };
static char const * const gs_swDataFiles[]    = { "SW.GRP", "Sw.grp", "sw.grp", NULL };
static char const * const gs_bloodDataFiles[] = { "BLOOD.RFF", "blood.rff", NULL };
static char const * const gs_rrDataFiles[]    = { "REDNECK.GRP", "redneck.grp", NULL };
static char const * const gs_namDataFiles[]   = { "NAM.GRP", "nam.grp", NULL };
static char const * const gs_napalmDataFiles[] = { "NAPALM.GRP", "napalm.grp", NULL };
static char const * const gs_ww2giDataFiles[] = { "WW2GI.GRP", "ww2gi.grp", NULL };
static char const * const gs_exhumedDataFiles[] = { "STUFF.DAT", "stuff.dat", NULL };
static char const * const gs_worldTourDataFiles[] = { "DUKE3D.GRP", "duke3d.grp", NULL };
static char const * const gs_ionFuryDataFiles[] = { "fury.grp", "FURY.GRP", NULL };

struct gs_gameSpec_t
{
    int id;
    char const *name;
    char const *subdir;
    char const * const *defaultDataFiles;
};

static gs_gameSpec_t const gs_gameSpecs[GS_GAME_COUNT] =
{
    { GS_GAME_DUKE,  "Duke Nukem 3D",   "duke3d", gs_dukeDataFiles  },
    { GS_GAME_WORLDTOUR, "Duke Nukem 3D: World Tour", "worldtour", gs_worldTourDataFiles },
    { GS_GAME_SW,    "Shadow Warrior",  "sw",     gs_swDataFiles    },
    { GS_GAME_BLOOD, "Blood",           "blood",  gs_bloodDataFiles },
    { GS_GAME_RR,    "Redneck Rampage", "rr",     gs_rrDataFiles    },
    { GS_GAME_NAM,   "NAM",             "nam",    gs_namDataFiles   },
    { GS_GAME_NAPALM, "Napalm",         "napalm", gs_napalmDataFiles },
    { GS_GAME_WW2GI, "World War II GI",  "ww2gi",  gs_ww2giDataFiles },
    { GS_GAME_EXHUMED, "Kexhumed", "exhumed", gs_exhumedDataFiles },
    { GS_GAME_IONFURY, "Ion Fury",       "ionfury", gs_ionFuryDataFiles },
};

static char const *gs_gameName(int game)
{
    return (unsigned)game < GS_GAME_COUNT ? gs_gameSpecs[game].name : "Duke Nukem 3D";
}

static char const *gs_crossplayDomainName(int domain)
{
    switch (domain)
    {
        case ART_CROSSPLAY_DOMAIN_LEVELS: return "levels";
        case ART_CROSSPLAY_DOMAIN_ITEMS: return "items";
        case ART_CROSSPLAY_DOMAIN_WEAPONS: return "weapons";
        default: return "unknown";
    }
}

static int gs_usesDukeRuntime(int game)
{
    return game == GS_GAME_DUKE || game == GS_GAME_WORLDTOUR || game == GS_GAME_NAM ||
           game == GS_GAME_NAPALM || game == GS_GAME_WW2GI || game == GS_GAME_IONFURY;
}

static int32_t gs_dukeRuntimeGameFlags(int game)
{
    switch (game)
    {
        case GS_GAME_NAM:      return GAMEFLAG_NAM;
        case GS_GAME_NAPALM:   return GAMEFLAG_NAM | GAMEFLAG_NAPALM;
        case GS_GAME_WW2GI:    return GAMEFLAG_WW2GI;
        case GS_GAME_IONFURY:  return GAMEFLAG_FURY | GAMEFLAG_NOCLASSIC | GAMEFLAG_NOPOLYMER;
        default:               return GAMEFLAG_DUKE;
    }
}

static int gs_linkedGameOrDuke(int game)
{
    if (gs_usesDukeRuntime(game)) return game;
#ifdef WITH_SW
    if (game == GS_GAME_SW) return GS_GAME_SW;
#endif
#ifdef WITH_BLOOD
    if (game == GS_GAME_BLOOD) return GS_GAME_BLOOD;
#endif
#ifdef WITH_RR
    if (game == GS_GAME_RR) return GS_GAME_RR;
#endif
#ifdef WITH_EXHUMED
    if (game == GS_GAME_EXHUMED) return GS_GAME_EXHUMED;
#endif
    return GS_GAME_DUKE;
}

static gs_gameSpec_t const *gs_gameSpec(int game)
{
    return (unsigned)game < GS_GAME_COUNT ? &gs_gameSpecs[game] : &gs_gameSpecs[GS_GAME_DUKE];
}

static char const *gs_defaultMainGameGrp(int game)
{
    switch (game)
    {
        case GS_GAME_WORLDTOUR: return "DUKE3D.GRP";
        case GS_GAME_RR: return "REDNECK.GRP";
        case GS_GAME_NAM: return "NAM.GRP";
        case GS_GAME_NAPALM: return "NAPALM.GRP";
        case GS_GAME_WW2GI: return "WW2GI.GRP";
        case GS_GAME_EXHUMED: return "STUFF.DAT";
        case GS_GAME_IONFURY: return "fury.grp";
        default: return NULL;
    }
}

static int gs_needsMainGameGrpInjection(int game)
{
    return gs_defaultMainGameGrp(game) != NULL;
}

static int gs_gameFromName(char const *name)
{
    if (!name) return -1;
    if (!Bstrcasecmp(name, "duke") || !Bstrcasecmp(name, "duke3d") || !Bstrcasecmp(name, "dukenukem3d"))
        return GS_GAME_DUKE;
    if (!Bstrcasecmp(name, "worldtour") || !Bstrcasecmp(name, "world-tour") ||
        !Bstrcasecmp(name, "world tour") || !Bstrcasecmp(name, "wt") ||
        !Bstrcasecmp(name, "duke3dwt") || !Bstrcasecmp(name, "duke-wt") ||
        !Bstrcasecmp(name, "duke20") || !Bstrcasecmp(name, "duke-20") ||
        !Bstrcasecmp(name, "duke3d-world-tour") || !Bstrcasecmp(name, "duke nukem 3d world tour") ||
        !Bstrcasecmp(name, "twentieth") || !Bstrcasecmp(name, "20th"))
        return GS_GAME_WORLDTOUR;
    if (!Bstrcasecmp(name, "sw") || !Bstrcasecmp(name, "shadowwarrior") || !Bstrcasecmp(name, "shadow-warrior"))
        return GS_GAME_SW;
    if (!Bstrcasecmp(name, "blood"))
        return GS_GAME_BLOOD;
    if (!Bstrcasecmp(name, "rr") || !Bstrcasecmp(name, "redneck") || !Bstrcasecmp(name, "redneckrampage") ||
        !Bstrcasecmp(name, "redneck-rampage"))
        return GS_GAME_RR;
    if (!Bstrcasecmp(name, "nam") || !Bstrcasecmp(name, "nam3d") || !Bstrcasecmp(name, "nam-3d"))
        return GS_GAME_NAM;
    if (!Bstrcasecmp(name, "napalm") || !Bstrcasecmp(name, "napalm3d") || !Bstrcasecmp(name, "napalm-3d"))
        return GS_GAME_NAPALM;
    if (!Bstrcasecmp(name, "ww2gi") || !Bstrcasecmp(name, "wwii") || !Bstrcasecmp(name, "wwiigi") ||
        !Bstrcasecmp(name, "wwii-gi") || !Bstrcasecmp(name, "wwii gi") ||
        !Bstrcasecmp(name, "worldwariigi") || !Bstrcasecmp(name, "world-war-ii-gi") ||
        !Bstrcasecmp(name, "world war ii gi") || !Bstrcasecmp(name, "gi"))
        return GS_GAME_WW2GI;
    if (!Bstrcasecmp(name, "kexhumed") || !Bstrcasecmp(name, "exhumed") || !Bstrcasecmp(name, "powerslave") ||
        !Bstrcasecmp(name, "power-slave") || !Bstrcasecmp(name, "power slave") ||
        !Bstrcasecmp(name, "pcexhumed"))
        return GS_GAME_EXHUMED;
    if (!Bstrcasecmp(name, "ionfury") || !Bstrcasecmp(name, "ion-fury") ||
        !Bstrcasecmp(name, "ion fury") || !Bstrcasecmp(name, "fury") ||
        !Bstrcasecmp(name, "aftershock") || !Bstrcasecmp(name, "ionfury-aftershock") ||
        !Bstrcasecmp(name, "ion-fury-aftershock"))
        return GS_GAME_IONFURY;
    return -1;
}

static char const *gs_basename(char const *path)
{
    if (!path) return "";
    char const *slash = strrchr(path, '/');
#ifdef _WIN32
    char const *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    return slash ? slash + 1 : path;
}

static int gs_extEquals(char const *path, char const *extension)
{
    char const *dot = strrchr(gs_basename(path), '.');
    return dot && !Bstrcasecmp(dot, extension);
}

static char const *gs_skipValueWhitespace(char const *value)
{
    while (value && isspace((unsigned char)*value)) ++value;
    return value;
}

static int gs_hasPathSeparator(char const *path)
{
    if (!path) return 0;
    if (strchr(path, '/')) return 1;
#ifdef _WIN32
    if (strchr(path, '\\')) return 1;
#endif
    return 0;
}

static char const *gs_expandHomePath(char const *path, char *buffer, int bufferSize)
{
    if (!path) return "";
    if (path[0] == '~' && path[1] == '/')
    {
        char const *home = getenv("HOME");
        if (home && home[0])
        {
            Bsnprintf(buffer, bufferSize, "%s%s", home, path + 1);
            return buffer;
        }
    }
    return path;
}

static char const *gs_absolutePath(char const *path, char *buffer, int bufferSize)
{
    char expanded[BMAX_PATH];
    char const *const dataPath = gs_expandHomePath(path, expanded, sizeof(expanded));
    if (!dataPath || !dataPath[0])
    {
        buffer[0] = 0;
        return buffer;
    }

#ifdef _WIN32
    if (dataPath[0] == '/' || dataPath[0] == '\\' || (isalpha((unsigned char)dataPath[0]) && dataPath[1] == ':'))
#else
    if (dataPath[0] == '/')
#endif
    {
        Bstrncpyz(buffer, dataPath, bufferSize);
        return buffer;
    }

    char cwd[BMAX_PATH];
    if (Bgetcwd(cwd, sizeof(cwd)))
        Bsnprintf(buffer, bufferSize, "%s/%s", cwd, dataPath);
    else
        Bstrncpyz(buffer, dataPath, bufferSize);

    return buffer;
}

static int gs_splitPathDirectoryAndBase(char const *path, char **directory, char const **base)
{
    if (directory) *directory = NULL;
    if (base) *base = gs_basename(path);
    if (!path || !path[0]) return 0;

    char expanded[BMAX_PATH];
    char const *const fullPath = gs_expandHomePath(path, expanded, sizeof(expanded));
    char const *slash = strrchr(fullPath, '/');
#ifdef _WIN32
    char const *backslash = strrchr(fullPath, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    if (!slash) return 0;

    if (base) *base = gs_basename(path);
    if (directory)
    {
        size_t const length = (size_t)(slash - fullPath);
        char *copy = (char *)Xmalloc(length ? length + 1 : 2);
        if (length)
        {
            Bmemcpy(copy, fullPath, length);
            copy[length] = 0;
        }
        else
        {
            Bstrcpy(copy, "/");
        }
        *directory = copy;
    }
    return 1;
}

static void gs_setInferredDataDir(char const *path);
static int gs_inferGameFromDirectory(char const *path);

static int gs_inferGameFromContainingDirectory(char const *path, int setDirForGame)
{
    char *directory = NULL;
    int game = -1;

    if (gs_splitPathDirectoryAndBase(path, &directory, NULL))
    {
        game = gs_inferGameFromDirectory(directory);
        if (game >= 0 && (setDirForGame < 0 || game == setDirForGame))
            gs_setInferredDataDir(directory);
        Xfree(directory);
    }
    else
    {
        game = gs_inferGameFromDirectory(".");
        if (game >= 0 && (setDirForGame < 0 || game == setDirForGame))
            gs_setInferredDataDir(".");
    }

    return game;
}

// VibeBuild32's public spelling is --long-name or -short-name.  Keep the old
// one-dash words as compatibility aliases so existing launch scripts do not
// break while the help and docs consistently advertise the new form.
static int gs_option(const char *arg, const char *longName, const char *shortName,
                     const char *legacyName, const char **inlineValue)
{
    const char *const names[] = { longName, shortName, legacyName };
    if (inlineValue) *inlineValue = NULL;
    for (auto const name : names)
    {
        if (!name) continue;
        size_t const n = Bstrlen(name);
        if (!Bstrcasecmp(arg, name)) return 1;
        // Compatibility for a double-dash shorthand such as --gm. Public
        // docs prefer -gm / --game, but a generated debug directive should
        // not accidentally feed --gm into Duke's legacy group-file parser.
        if (shortName && arg[0] == '-' && arg[1] == '-' && !Bstrcasecmp(arg + 1, shortName)) return 1;
        if (inlineValue && !Bstrncasecmp(arg, name, n) && arg[n] == '=')
        {
            *inlineValue = gs_skipValueWhitespace(arg + n + 1);
            return 1;
        }
    }
    return 0;
}

static int gs_valueOptionWithAttached(char const *arg, char const *longName, char const *shortName,
                                      char const *legacyName, char const **inlineValue)
{
    if (gs_option(arg, longName, shortName, legacyName, inlineValue)) return 1;

    char const *const names[] = { longName, shortName, legacyName };
    if (inlineValue) *inlineValue = NULL;
    for (auto const name : names)
    {
        if (!name) continue;
        size_t const length = Bstrlen(name);
        if (Bstrncasecmp(arg, name, length)) continue;
        if (!arg[length] || arg[length] == '=') continue;
        if (inlineValue) *inlineValue = gs_skipValueWhitespace(arg + length);
        return 1;
    }
    return 0;
}

static char const *gs_takeOptionValue(int *index, int argc, char const * const *argv, char const *inlineValue)
{
    if (inlineValue) return inlineValue;
    if (*index + 1 >= argc) return NULL;
    return argv[++(*index)];
}

static void gs_emitOptionValue(char const **out, int *count, char const *option, char const *value)
{
    out[(*count)++] = option;
    if (value) out[(*count)++] = value;
}

static int gs_gameOption(char const *arg, char const **inlineValue)
{
    if (!Bstrncasecmp(arg, "--gamegrp", 9) || !Bstrncasecmp(arg, "-gamegrp", 8))
        return 0;
    if (!Bstrncasecmp(arg, "--game-dir", 10) || !Bstrncasecmp(arg, "-game_dir", 9))
        return 0;
    if (!Bstrncasecmp(arg, "--game.", 7) || !Bstrncasecmp(arg, "-gm.", 4))
        return 0;
    return gs_valueOptionWithAttached(arg, "--game", "-gm", "-game", inlineValue);
}

// Normalize a value-taking option whose legacy game parser expects the value
// directly after its short spelling (for example, -l1). This accepts either
// the documented long spelling or the short spelling, with a value attached,
// after '=', or in the following argv token.
static int gs_compactValueOption(char const *arg, char const *longName, char const *shortName, char const **inlineValue)
{
    char const *names[] = { longName, shortName };
    if (inlineValue) *inlineValue = NULL;
    for (auto const name : names)
    {
        size_t const length = Bstrlen(name);
        if (Bstrncasecmp(arg, name, length)) continue;
        char const *value = arg + length;
        if (*value == '=') ++value;
        if (*value || !Bstrcasecmp(arg, name)) { if (inlineValue) *inlineValue = value; return 1; }
    }
    return 0;
}

// Debug-only launch parameters live in an index-file header, for example:
//   /// vbd.params="-l 1 -v 1"
// They are parsed into argv tokens directly: this is not shell input.
static char const *gs_debugParams[64];
static int gs_debugParamCount = 0;

static void gs_addDebugParam(char const *text, size_t length)
{
    if (!length || gs_debugParamCount >= (int)(sizeof(gs_debugParams) / sizeof(gs_debugParams[0]))) return;
    // Debug directives are parsed directly into argv (not by a shell), so
    // explicitly provide the familiar ~/ expansion for file arguments.
    if (length >= 2 && text[0] == '~' && text[1] == '/')
    {
        char const *home = getenv("HOME");
        if (home && home[0])
        {
            size_t const homeLength = Bstrlen(home);
            char *copy = (char *)Xmalloc(homeLength + length);
            Bmemcpy(copy, home, homeLength);
            Bmemcpy(copy + homeLength, text + 1, length);
            copy[homeLength + length - 1] = 0;
            gs_debugParams[gs_debugParamCount++] = copy;
            return;
        }
    }
    char *copy = (char *)Xmalloc(length + 1);
    Bmemcpy(copy, text, length); copy[length] = 0;
    gs_debugParams[gs_debugParamCount++] = copy;
}

static void gs_parseDebugParams(char const *text)
{
    char const *p = text;
    while (*p && isspace((unsigned char)*p)) ++p;
    char const *end = text + Bstrlen(text);
    while (end > p && isspace((unsigned char)end[-1])) --end;
    // The usual directive form quotes the entire argument list:
    //   /// vbd.params="-gm duke -g ~/games/duke3d.grp"
    // Those outer quotes delimit the header value; they must not turn the
    // complete list into one argv token. Inner quotes remain available for a
    // value that itself contains whitespace.
    if (end - p >= 2 && (*p == '\'' || *p == '"') && end[-1] == *p)
    {
        ++p;
        --end;
    }
    while (p < end)
    {
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p >= end) break;
        char token[1024]; size_t length = 0; char quote = 0; int escaped = 0;
        while (p < end)
        {
            char const c = *p++;
            if (escaped) { if (length + 1 < sizeof(token)) token[length++] = c; escaped = 0; continue; }
            if (c == '\\') { escaped = 1; continue; }
            if (quote) { if (c == quote) quote = 0; else if (length + 1 < sizeof(token)) token[length++] = c; continue; }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (isspace((unsigned char)c)) break;
            if (length + 1 < sizeof(token)) token[length++] = c;
        }
        if (escaped && length + 1 < sizeof(token)) token[length++] = '\\';
        gs_addDebugParam(token, length);
    }
}

static void gs_collectDebugParamsFromFile(char const *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) return;
    char line[2048];
    for (int i = 0; i < 32 && fgets(line, sizeof(line), file); ++i)
    {
        char *p = line; while (*p && isspace((unsigned char)*p)) ++p;
        if (!Bstrncasecmp(p, "///", 3))
        {
            p += 3; while (*p && isspace((unsigned char)*p)) ++p;
            if (!Bstrncasecmp(p, "vbd.params", 10))
            {
                p += 10; while (*p && isspace((unsigned char)*p)) ++p;
                if (*p == '=') { ++p; gs_parseDebugParams(p); }
                break;
            }
        }
        else if (*p && Bstrncasecmp(p, "//", 2)) break;
    }
    fclose(file);
}

static int gs_debugModeRequested(int argc, char const * const *argv)
{
    for (int i = 1; i < argc; ++i)
    {
        char const *arg = argv[i];
        if (gs_option(arg, "--devtools", "-dt", NULL, NULL) || gs_option(arg, "--debug", "-dbg", NULL, NULL)) return 1;

        // -dbg8501 and --debug8501 are valid port forms too.
        if (!Bstrncasecmp(arg, "-dbg", 4) || !Bstrncasecmp(arg, "--debug", 7))
        {
            char const *port = (arg[1] == '-') ? arg + 7 : arg + 4;
            if (*port && isdigit((unsigned char)*port)) return 1;
        }
    }
    return 0;
}

// Public colour order is Shadow Warrior's native player-colour order.  Other
// games map it to their closest native player palette, so the same command
// works across the combined binary.  Arbitrary #rrggbb values choose the
// closest one of these eight palette-safe colours.
static int gs_playerColorIndex(char const *value)
{
    static char const *const names[] = { "brown", "gray", "purple", "red", "yellow", "olive", "green", "blue" };
    for (int i = 0; i < 8; ++i)
        if (!Bstrcasecmp(value, names[i])) return i;

    if (!value || value[0] != '#' || Bstrlen(value) != 7) return -1;
    int rgb[3] = {};
    for (int i = 0; i < 6; ++i)
    {
        int const c = value[i + 1];
        int const digit = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (digit < 0) return -1;
        rgb[i >> 1] = (rgb[i >> 1] << 4) | digit;
    }
    static int const palette[8][3] = {
        { 128, 80, 32 }, { 128, 128, 128 }, { 128, 64, 160 }, { 224, 32, 32 },
        { 240, 224, 32 }, { 128, 128, 32 }, { 32, 180, 48 }, { 32, 80, 224 },
    };
    int best = 0, bestDistance = INT_MAX;
    for (int i = 0; i < 8; ++i)
    {
        int const dr = rgb[0] - palette[i][0], dg = rgb[1] - palette[i][1], db = rgb[2] - palette[i][2];
        int const distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) best = i, bestDistance = distance;
    }
    return best;
}

static int gs_nativePlayerColor(int color)
{
    // Duke/RR palette lookups and Blood's colour-preference slots differ from
    // SW's 0..7 selector, but each row preserves the requested hue.
    static int const dukeRR[8] = { 15, 12, 21, 10, 23, 14, 11, 9 };
    static int const blood[8]  = { 6, 4, 3, 2, 5, 6, 3, 1 };
    if (g_activeGame == GS_GAME_SW) return color;
    if (g_activeGame == GS_GAME_BLOOD) return blood[color];
    return dukeRR[color];
}

static int gs_colorOption(char const *arg, char const **inlineValue)
{
    return gs_valueOptionWithAttached(arg, "--color", "-clr", NULL, inlineValue);
}

static int gs_resolutionOption(char const *arg, char const **inlineValue)
{
    return gs_valueOptionWithAttached(arg, "--resolution", "-res", NULL, inlineValue);
}

static int gs_monsterOption(char const *arg, char const **inlineValue)
{
    return gs_valueOptionWithAttached(arg, "--monsters", "-mnst", NULL, inlineValue);
}

static int gs_crossplayDomainOption(char const *arg, int *domain, char const **inlineValue)
{
    if (gs_valueOptionWithAttached(arg, "--game.levels", "-gm.lvls", NULL, inlineValue))
    {
        if (domain) *domain = ART_CROSSPLAY_DOMAIN_LEVELS;
        return 1;
    }
    if (gs_valueOptionWithAttached(arg, "--game.items", "-gm.its", NULL, inlineValue))
    {
        if (domain) *domain = ART_CROSSPLAY_DOMAIN_ITEMS;
        return 1;
    }
    if (gs_valueOptionWithAttached(arg, "--game.weapons", "-gm.wpns", NULL, inlineValue))
    {
        if (domain) *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return 1;
    }
    return 0;
}

static char const *gs_takeDomainOptionValue(int *index, int argc, char const * const *argv, char const *inlineValue)
{
    if (inlineValue && inlineValue[0] == '=')
        inlineValue = gs_skipValueWhitespace(inlineValue + 1);
    if (inlineValue && inlineValue[0])
        return inlineValue;

    if (*index + 1 >= argc)
        return NULL;

    char const *value = argv[++(*index)];
    if (!Bstrcmp(value, "="))
    {
        if (*index + 1 >= argc)
            return NULL;
        value = argv[++(*index)];
    }
    else if (value[0] == '=')
        value = gs_skipValueWhitespace(value + 1);

    return value;
}

static void gs_parseCrossplayDomainArgs(int argc, char const * const *argv)
{
    for (int32_t domain = 0; domain < ART_CROSSPLAY_DOMAIN_COUNT; ++domain)
        g_crossplayRequestedDomainGames[domain] = ART_CROSSPLAY_GAME_UNKNOWN;

    for (int i = 1; i < argc; ++i)
    {
        char const *inlineValue = NULL;
        int domain = -1;
        if (!gs_crossplayDomainOption(argv[i], &domain, &inlineValue))
            continue;

        char const *value = gs_takeDomainOptionValue(&i, argc, argv, inlineValue);
        int const game = gs_gameFromName(value);
        if (game < 0)
        {
            fprintf(stderr, "Invalid --game.%s value '%s'. Use duke, worldtour, ionfury, sw, blood, rr, nam, napalm, ww2gi, or exhumed.\n",
                    gs_crossplayDomainName(domain), value ? value : "");
            continue;
        }

        g_crossplayRequestedDomainGames[domain] = game;
    }
}

static int gs_parseResolution(char const *value, int *width, int *height)
{
    char separator = 0, trailing = 0;
    if (!value || sscanf(value, "%d%c%d%c", width, &separator, height, &trailing) != 3 ||
        (separator != 'x' && separator != 'X') || *width < 320 || *width > 8192 || *height < 200 || *height > 4320)
        return 0;
    return 1;
}

static void gs_collectDebugParams(void)
{
    DIR *directory = opendir("autojs");
    if (!directory) return;
    dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;
        // Match auto-loading: TypeScript wins when both index files exist.
        char path[BMAX_PATH * 2];
        char const *extensions[] = { "ts", "js" };
        for (auto extension : extensions)
        {
            Bsnprintf(path, sizeof(path), "autojs/%s/index.%s", entry->d_name, extension);
            FILE *file = fopen(path, "r");
            if (!file) continue;
            fclose(file);
            gs_collectDebugParamsFromFile(path);
            break;
        }
    }
    closedir(directory);
}

static char const * const *gs_buildArgvWithDebugParams(int argc, char const * const *argv, int *outArgc)
{
    if (!gs_debugParamCount) { *outArgc = argc; return argv; }
    char const **out = (char const **)Xmalloc((size_t)(argc + gs_debugParamCount + 1) * sizeof(char *));
    for (int i = 0; i < argc; ++i) out[i] = argv[i];
    for (int i = 0; i < gs_debugParamCount; ++i) out[argc + i] = gs_debugParams[i];
    *outArgc = argc + gs_debugParamCount;
    out[*outArgc] = NULL;
    return out;
}

// Some selector entries use a linked runtime whose native autodetect needs the
// selected main game data named explicitly. Keep this generic so new entries add
// metadata instead of one-off routing code.
static char gs_mainGameGrpFile[BMAX_PATH] = "";
static char gs_inferredDataDir[BMAX_PATH] = "";

static void gs_joinPath(char *buffer, int bufferSize, char const *dir, char const *file)
{
    if (!dir || !dir[0] || !Bstrcmp(dir, "."))
        Bsnprintf(buffer, bufferSize, "%s", file);
    else
        Bsnprintf(buffer, bufferSize, "%s/%s", dir, file);
}

static void gs_setMainGameGrpFile(char const *path)
{
    if (path && path[0])
        Bstrncpyz(gs_mainGameGrpFile, path, sizeof(gs_mainGameGrpFile));
}

static void gs_setInferredDataDir(char const *path)
{
    if (!path || !path[0]) return;
    char expanded[BMAX_PATH];
    char const *dir = gs_expandHomePath(path, expanded, sizeof(expanded));
    if (gs_directoryExists(dir))
        Bstrncpyz(gs_inferredDataDir, dir, sizeof(gs_inferredDataDir));
}

static int gs_dirHasAllFiles(char const *dir, char const * const *files)
{
    for (char const * const *file = files; file && *file; ++file)
    {
        char path[BMAX_PATH * 2];
        gs_joinPath(path, sizeof(path), dir, *file);
        if (!gs_fileExists(path))
            return 0;
    }
    return 1;
}

static int gs_isWorldTourDataDir(char const *dir)
{
    static char const * const requiredFiles[] =
    {
        "DUKE3D.GRP",
        "GAME.CON",
        "DEFS.CON",
        "USER.CON",
        "FLAMETHROWER.CON",
        "FIREFLYTROOPER.CON",
        "EPISODE5BOSS.CON",
        "TILES020.ART",
        "maps/E5L1.map",
        NULL,
    };

    char expanded[BMAX_PATH];
    char const *const dataDir = gs_expandHomePath(dir, expanded, sizeof(expanded));
    return gs_directoryExists(dataDir) && gs_dirHasAllFiles(dataDir, requiredFiles);
}

static void gs_setWorldTourDataDir(char const *dir)
{
    char expanded[BMAX_PATH];
    char const *const dataDir = gs_expandHomePath(dir, expanded, sizeof(expanded));
    gs_setInferredDataDir(dataDir);
    char groupFile[BMAX_PATH * 2];
    gs_joinPath(groupFile, sizeof(groupFile), dataDir, "DUKE3D.GRP");
    gs_setMainGameGrpFile(groupFile);
}

static int gs_findWorldTourDataFile(char *outdir, int outsz, char *matched, int matchedsz)
{
    static char const * const candidateDirs[] = { ".", "worldtour", "world-tour", "duke3d_wt", NULL };
    for (char const * const *dir = candidateDirs; *dir; ++dir)
    {
        if (!gs_isWorldTourDataDir(*dir))
            continue;
        Bstrncpyz(outdir, *dir, outsz);
        if (matched) Bstrncpyz(matched, "DUKE3D.GRP", matchedsz);
        return 1;
    }
    return 0;
}

// Look for a game's canonical data file in the working dir, then in its
// per-game subdir. On success writes the directory to hold the data (".", or
// the subdir) into outdir and returns 1.
static int gs_findGameDataFile(int game, char *outdir, int outsz, char *matched, int matchedsz)
{
    if (game == GS_GAME_WORLDTOUR)
        return gs_findWorldTourDataFile(outdir, outsz, matched, matchedsz);

    gs_gameSpec_t const *spec = gs_gameSpec(game);
    for (char const * const *file = spec->defaultDataFiles; file && *file; ++file)
    {
        if (gs_fileExists(*file))
        {
            if (game == GS_GAME_DUKE && gs_isWorldTourDataDir("."))
                continue;
            Bstrncpyz(outdir, ".", outsz);
            if (matched) Bstrncpyz(matched, *file, matchedsz);
            return 1;
        }

        char path[BMAX_PATH];
        Bsnprintf(path, sizeof(path), "%s/%s", spec->subdir, *file);
        if (gs_fileExists(path))
        {
            if (game == GS_GAME_DUKE && gs_isWorldTourDataDir(spec->subdir))
                continue;
            Bstrncpyz(outdir, spec->subdir, outsz);
            if (matched) Bstrncpyz(matched, *file, matchedsz);
            return 1;
        }
    }
    return 0;
}

static int gs_gameAvail(int game, char *dir, int sz, char *matched, int matchedsz)
{
    return gs_findGameDataFile(game, dir, sz, matched, matchedsz);
}

struct gs_dataSignature_t
{
    int game;
    int32_t size;
    uint32_t crc;
};

static gs_dataSignature_t const gs_dataSignatures[] =
{
    // Duke Nukem 3D main GRPs and common official/add-on GRPs.
    { GS_GAME_DUKE,  26524524, 0xBBC9CE44u },
    { GS_GAME_DUKE,  26385383, 0xAA4F6A40u },
    { GS_GAME_DUKE,  44356548, 0xFD3DCFF1u },
    { GS_GAME_DUKE,  44356548, 0x982AFE4Au },
    { GS_GAME_DUKE,  44348015, 0xF514A6ACu },
    { GS_GAME_DUKE,   9690241, 0x02F18900u },
    { GS_GAME_DUKE,  10429258, 0xA28AA589u },
    { GS_GAME_DUKE,  10442980, 0x912E1E8Du },
    { GS_GAME_DUKE,  11035779, 0x983AD923u },
    { GS_GAME_DUKE,  10444391, 0xC5F71561u },
    { GS_GAME_DUKE,  10628573, 0x73A15EE7u },
    { GS_GAME_DUKE,   7926624, 0xA9242158u },
    { GS_GAME_DUKE,   8225517, 0xB79D997Fu },
    { GS_GAME_DUKE,   8410183, 0xA8CF80DAu },
    { GS_GAME_DUKE,   8410187, 0x39A692BFu },
    { GS_GAME_DUKE,   8410149, 0xC63B6A8Bu },
    { GS_GAME_DUKE,  23559381, 0x4A2DBB62u },
    { GS_GAME_DUKE,  22551333, 0x2F4FCCEEu },
    { GS_GAME_DUKE,  22521880, 0xB62B42FDu },
    { GS_GAME_DUKE,  22213819, 0x18F01C5Bu },
    { GS_GAME_DUKE,  22397273, 0x65B5F690u },
    { GS_GAME_DUKE,  22213795, 0x64CF2351u },
    { GS_GAME_DUKE,  16169365, 0xF1CAE8E4u },
    { GS_GAME_DUKE,  10965909, 0xC7EFBFA9u },
    { GS_GAME_DUKE,  26135388, 0x82C1B47Fu },
    { GS_GAME_DUKE,  44100411, 0x7FB6117Cu },
    { GS_GAME_DUKE,   3186656, 0x1E9516F1u },
    { GS_GAME_DUKE,   2112419, 0x7CD82A3Bu },
    { GS_GAME_DUKE,   4247491, 0xCF928A58u },

    // NAM-family games use the Duke runtime but need a distinct selector entry
    // so their main data is passed as -gamegrp and sets the native game flags.
    { GS_GAME_NAM,    43448927, 0x75C1F07Bu },
    { GS_GAME_NAPALM, 44365728, 0x3DE1589Au },

    // World War II GI and its official Platoon Leader add-on use the Duke/NAM
    // runtime but must be selected as their own game so -gamegrp sets WW2GI mode.
    { GS_GAME_WW2GI, 77939508, 0x907B82BFu },
    { GS_GAME_WW2GI, 37852572, 0xD1ED8C0Cu },

    // Ion Fury / Aftershock. The install also ships fury.grpinfo, but keeping
    // the known signature here lets the selector infer the runtime before
    // Duke's GRP scanner has parsed grpinfo metadata.
    { GS_GAME_IONFURY, 160826590, 0xE175FB41u },

    // Shadow Warrior main/shareware/add-on GRPs.
    { GS_GAME_SW,    47536148, 0x7545319Fu },
    { GS_GAME_SW,    47536148, 0xD4A1E153u },
    { GS_GAME_SW,    47536148, 0x3EE68767u },
    { GS_GAME_SW,    47537951, 0x1A8776D2u },
    { GS_GAME_SW,    25702245, 0xDAA6BECEu },
    { GS_GAME_SW,    25833456, 0xF21A6B35u },
    { GS_GAME_SW,    26056769, 0x08A7FA1Fu },
    { GS_GAME_SW,    26056769, 0x4227F535u },
    { GS_GAME_SW,    48698128, 0xA9AAA7B7u },
    { GS_GAME_SW,    12499012, 0xA1A65BE8u },
    { GS_GAME_SW,     6235578, 0xACC8DCDEu },
    { GS_GAME_SW,     6236287, 0xB5B71277u },

    // Redneck Rampage / Rides Again.
    { GS_GAME_RR,   141174222, 0x19D9BC79u },
    { GS_GAME_RR,   191798609, 0x958018C6u },

    // PCExhumed / Powerslave full and demo data files.
    { GS_GAME_EXHUMED, 27020745, 0x303CBD89u },
    { GS_GAME_EXHUMED, 27108170, 0xE3B172F1u },
    { GS_GAME_EXHUMED, 15904838, 0x1D8C7645u },
    { GS_GAME_EXHUMED, 16481687, 0x1A6E27FAu },
};

static int gs_fileSignature(char const *path, int32_t *size, uint32_t *crc)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;

    static int crcTableReady = 0;
    if (!crcTableReady)
    {
        initcrc32table();
        crcTableReady = 1;
    }

    unsigned char buffer[65536];
    uint32_t value = 0;
    long total = 0;
    size_t bytes = 0;

    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (total > INT_MAX - (long)bytes)
        {
            fclose(file);
            return 0;
        }
        value = Bcrc32(buffer, (int)bytes, value);
        total += (long)bytes;
    }

    int const ok = !ferror(file);
    fclose(file);
    if (!ok) return 0;

    *size = (int32_t)total;
    *crc = value;
    return 1;
}

static int gs_isBloodRffFile(char const *path)
{
    if (!gs_extEquals(path, ".rff")) return 0;

    FILE *file = fopen(path, "rb");
    if (!file) return 0;

    unsigned char header[6] = {};
    size_t const bytes = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (bytes != sizeof(header) || Bmemcmp(header, "RFF\x1a", 4))
        return 0;

    uint16_t const version = (uint16_t)(header[4] | (header[5] << 8));
    return (version & 0xff00) == 0x0200 || (version & 0xff00) == 0x0300;
}

static int gs_isBloodIniFile(char const *path)
{
    if (!gs_extEquals(path, ".ini")) return 0;

    FILE *file = fopen(path, "r");
    if (!file) return 0;

    int hasEpisode = 0;
    int hasMapEntry = 0;
    int hasInstall = 0;
    char line[1024];

    for (int i = 0; i < 256 && fgets(line, sizeof(line), file); ++i)
    {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) ++p;

        if (!Bstrncasecmp(p, "[Episode", 8))
            hasEpisode = 1;
        else if (!Bstrncasecmp(p, "[Install]", 9))
            hasInstall = 1;
        else if (!Bstrncasecmp(p, "Map", 3) && strchr(p, '='))
            hasMapEntry = 1;

        if (hasEpisode && (hasMapEntry || hasInstall))
            break;
    }

    fclose(file);
    return hasEpisode && (hasMapEntry || hasInstall);
}

static int gs_inferGameFromSignature(int32_t size, uint32_t crc)
{
    for (auto const &signature : gs_dataSignatures)
        if (signature.size == size && signature.crc == crc)
            return signature.game;
    return -1;
}

static int gs_inferGameFromBasename(char const *path)
{
    char const *base = gs_basename(path);
    if (!Bstrcasecmp(base, "DUKE3D.GRP") || !Bstrcasecmp(base, "duke.grp"))
        return GS_GAME_DUKE;
    if (!Bstrcasecmp(base, "SW.GRP") || !Bstrcasecmp(base, "WANTON.GRP") ||
        !Bstrcasecmp(base, "TD.GRP") || !Bstrcasecmp(base, "TWIN DRAGON.GRP") ||
        !Bstrcasecmp(base, "TWIN_DRAGON.GRP"))
        return GS_GAME_SW;
    if (!Bstrcasecmp(base, "BLOOD.RFF") || !Bstrcasecmp(base, "BLOOD.INI"))
        return GS_GAME_BLOOD;
    if (!Bstrcasecmp(base, "REDNECK.GRP") || !Bstrcasecmp(base, "RR.GRP") ||
        !Bstrcasecmp(base, "RRRA.GRP") || !Bstrcasecmp(base, "RIDES.GRP"))
        return GS_GAME_RR;
    if (!Bstrcasecmp(base, "NAM.GRP"))
        return GS_GAME_NAM;
    if (!Bstrcasecmp(base, "NAPALM.GRP"))
        return GS_GAME_NAPALM;
    if (!Bstrcasecmp(base, "WW2GI.GRP") || !Bstrcasecmp(base, "PLATOONL.GRP"))
        return GS_GAME_WW2GI;
    if (!Bstrcasecmp(base, "STUFF.DAT"))
        return GS_GAME_EXHUMED;
    if (!Bstrcasecmp(base, "fury.grp"))
        return GS_GAME_IONFURY;
    return -1;
}

static int gs_inferGameFromDirectory(char const *path)
{
    char expanded[BMAX_PATH];
    char const *dir = gs_expandHomePath(path, expanded, sizeof(expanded));
    if (gs_isWorldTourDataDir(dir))
        return GS_GAME_WORLDTOUR;

    int foundGame = -1;
    for (auto const &spec : gs_gameSpecs)
    {
        if (spec.id == GS_GAME_WORLDTOUR)
            continue;
        for (char const * const *file = spec.defaultDataFiles; file && *file; ++file)
        {
            char candidate[BMAX_PATH * 2];
            Bsnprintf(candidate, sizeof(candidate), "%s/%s", dir, *file);
            if (!gs_fileExists(candidate)) continue;
            if (foundGame >= 0 && foundGame != spec.id) return -1;
            foundGame = spec.id;
            break;
        }
    }
    return foundGame;
}

static int gs_inferGameFromDataPath(char const *path)
{
    if (!path || !path[0]) return -1;

    char expanded[BMAX_PATH];
    char const *dataPath = gs_expandHomePath(path, expanded, sizeof(expanded));

    int32_t size = 0;
    uint32_t crc = 0;
    if (gs_fileSignature(dataPath, &size, &crc))
    {
        int const game = gs_inferGameFromSignature(size, crc);
        if (game >= 0)
        {
            // The World Tour GRP has the same native Duke runtime but needs the
            // loose World Tour payload (GAME.CON, episode 5 maps, new actor
            // scripts, ART, music). Treat the GRP as World Tour only when that
            // payload is present in the containing directory.
            if (game == GS_GAME_DUKE && size == 44356548 && crc == 0x982AFE4Au)
            {
                int const dirGame = gs_inferGameFromContainingDirectory(dataPath, GS_GAME_WORLDTOUR);
                if (dirGame == GS_GAME_WORLDTOUR)
                {
                    gs_setMainGameGrpFile(dataPath);
                    return GS_GAME_WORLDTOUR;
                }
            }
            if (gs_hasPathSeparator(dataPath))
                gs_inferGameFromContainingDirectory(dataPath, game);
            if (gs_needsMainGameGrpInjection(game)) gs_setMainGameGrpFile(dataPath);
            return game;
        }
    }

    if (gs_isBloodRffFile(dataPath) || gs_isBloodIniFile(dataPath))
    {
        gs_setInferredDataDir(dataPath);
        gs_inferGameFromContainingDirectory(dataPath, GS_GAME_BLOOD);
        return GS_GAME_BLOOD;
    }

    int game = gs_inferGameFromBasename(dataPath);
    if (game >= 0)
    {
        if (gs_needsMainGameGrpInjection(game)) gs_setMainGameGrpFile(dataPath);
        gs_inferGameFromContainingDirectory(dataPath, game);
        return game;
    }

    game = gs_inferGameFromDirectory(dataPath);
    if (game >= 0)
    {
        if (game == GS_GAME_WORLDTOUR)
            gs_setWorldTourDataDir(dataPath);
        else
            gs_setInferredDataDir(dataPath);
    }
    if (gs_needsMainGameGrpInjection(game) && game != GS_GAME_WORLDTOUR) gs_setMainGameGrpFile(gs_defaultMainGameGrp(game));
    if (game >= 0) return game;

    if (gs_hasPathSeparator(dataPath))
    {
        game = gs_inferGameFromContainingDirectory(dataPath, -1);
        if (game == GS_GAME_WORLDTOUR)
            gs_setMainGameGrpFile(dataPath);
        else if (gs_needsMainGameGrpInjection(game))
            gs_setMainGameGrpFile(gs_defaultMainGameGrp(game));
        return game;
    }

    return -1;
}

static int gs_configuredCrossplayDomainGame(int domain);

// A file that could plausibly be game content rather than, say, a config or a
// text file.  Deliberately narrow: this decides what gets registered sight
// unseen.
static int gs_isContainerCandidate(char const *path)
{
    return path && path[0] && gs_fileExists(path) &&
           (gs_extEquals(path, ".grp") || gs_extEquals(path, ".pk3") || gs_extEquals(path, ".zip") ||
            gs_extEquals(path, ".pk4") || gs_extEquals(path, ".ssi"));
}

static void gs_trackForeignDataPath(char const *path)
{
    if (!path || !path[0])
        return;

    char absolute[BMAX_PATH];
    char const *dataPath = gs_absolutePath(path, absolute, sizeof(absolute));

    int32_t size = 0;
    uint32_t crc = 0;
    int game = -1;
    if (gs_fileSignature(dataPath, &size, &crc))
        game = gs_inferGameFromSignature(size, crc);
    if (game < 0 && (gs_isBloodRffFile(dataPath) || gs_isBloodIniFile(dataPath)))
        game = GS_GAME_BLOOD;
    if (game < 0)
        game = gs_inferGameFromBasename(dataPath);
    if (game < 0)
        game = gs_inferGameFromDirectory(dataPath);

    // A mod is not any of the games we can recognise.  Every inference above is
    // a test for *retail* data -- a known size and CRC, a known basename, a
    // directory holding one of those -- and a Duke mod GRP or PK3 matches none
    // of them, so it was dropped on the floor: not registered as foreign data,
    // and therefore invisible to every crossplay lookup for art, CON, sounds and
    // levels.  When the player has asked to play another game's levels, an
    // unrecognised container handed over on the command line is content for that
    // game, so register it as such.  It goes in as UNKNOWN rather than as the
    // game the player asked to play the levels of, which is what puts it ahead of
    // the base game in every lookup: the list is searched newest-first and a mod
    // is passed after the game it modifies, so its CONs, art, sounds and maps
    // win -- exactly as a mod does in Duke.  Registering it as "unknown" instead
    // would drop it into the second pass of the two-pass lookups and the base
    // game's stock files would answer first, which is the opposite of a mod.
    int const addOnGame = gs_configuredCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS);

    if (game < 0 && addOnGame >= 0 && addOnGame != g_activeGame && gs_isContainerCandidate(dataPath) &&
        artCrossplayAddForeignGameData(dataPath, addOnGame))
    {
        LOG_F(INFO, "Crossplay: treating %s as add-on data for %s.", dataPath, gs_gameName(addOnGame));
        return;
    }

    if (game >= 0 && game != g_activeGame && artCrossplayAddForeignGameData(dataPath, game))
        LOG_F(INFO, "Crossplay: treating %s as foreign game data (%s).", dataPath, gs_gameName(game));

    if (game == GS_GAME_BLOOD && game != g_activeGame && gs_hasPathSeparator(dataPath))
    {
        char *directory = NULL;
        if (gs_splitPathDirectoryAndBase(dataPath, &directory, NULL))
        {
            if (gs_inferGameFromDirectory(directory) == GS_GAME_BLOOD && artCrossplayAddForeignGameData(directory, GS_GAME_BLOOD))
                LOG_F(INFO, "Crossplay: treating %s as foreign game data (%s art directory).", directory, gs_gameName(game));
            Xfree(directory);
        }
    }
}

static int gs_dataFileOption(char const *arg, char const **inlineValue);
static int gs_gameDirOption(char const *arg, char const **inlineValue);
static int gs_searchPathOption(char const *arg, char const **inlineValue);

static int gs_captureExplicitGameDataPath(char const *path, int targetGame)
{
    if (!path || !path[0] || targetGame < 0)
        return 0;

    char expanded[BMAX_PATH];
    char const *dataPath = gs_expandHomePath(path, expanded, sizeof(expanded));

    int32_t size = 0;
    uint32_t crc = 0;
    if (gs_fileSignature(dataPath, &size, &crc))
    {
        int game = gs_inferGameFromSignature(size, crc);

        if (targetGame == GS_GAME_WORLDTOUR && game == GS_GAME_DUKE && size == 44356548 && crc == 0x982AFE4Au)
            game = gs_inferGameFromContainingDirectory(dataPath, GS_GAME_WORLDTOUR);

        if (game == targetGame)
        {
            if (gs_hasPathSeparator(dataPath))
                gs_inferGameFromContainingDirectory(dataPath, targetGame);
            if (gs_needsMainGameGrpInjection(targetGame))
                gs_setMainGameGrpFile(dataPath);
            return 1;
        }

        return 0;
    }

    int game = -1;
    if (targetGame == GS_GAME_BLOOD && (gs_isBloodRffFile(dataPath) || gs_isBloodIniFile(dataPath)))
        game = GS_GAME_BLOOD;
    else
        game = gs_inferGameFromBasename(dataPath);

    if (game == targetGame)
    {
        if (gs_needsMainGameGrpInjection(targetGame))
            gs_setMainGameGrpFile(dataPath);
        gs_inferGameFromContainingDirectory(dataPath, targetGame);
        return 1;
    }

    game = gs_inferGameFromDirectory(dataPath);
    if (game == targetGame)
    {
        if (targetGame == GS_GAME_WORLDTOUR)
            gs_setWorldTourDataDir(dataPath);
        else
            gs_setInferredDataDir(dataPath);
        if (gs_needsMainGameGrpInjection(targetGame) && targetGame != GS_GAME_WORLDTOUR)
            gs_setMainGameGrpFile(gs_defaultMainGameGrp(targetGame));
        return 1;
    }

    if (gs_hasPathSeparator(dataPath))
    {
        game = gs_inferGameFromContainingDirectory(dataPath, targetGame);
        if (game == targetGame)
        {
            if (gs_needsMainGameGrpInjection(targetGame) && targetGame != GS_GAME_WORLDTOUR)
                gs_setMainGameGrpFile(gs_defaultMainGameGrp(targetGame));
            return 1;
        }
    }

    return 0;
}

static void gs_captureExplicitGameArgs(int argc, char const * const *argv, int targetGame)
{
    if (targetGame < 0)
        return;

    for (int i = 1; i < argc; ++i)
    {
        char const *inlineValue = NULL;
        char const *value = NULL;
        int crossplayDomain = -1;

        if (gs_crossplayDomainOption(argv[i], &crossplayDomain, &inlineValue))
        {
            (void)gs_takeDomainOptionValue(&i, argc, argv, inlineValue);
            continue;
        }

        if (gs_dataFileOption(argv[i], &inlineValue))
        {
            value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            gs_captureExplicitGameDataPath(value, targetGame);
            continue;
        }

        if (gs_gameDirOption(argv[i], &inlineValue) || gs_searchPathOption(argv[i], &inlineValue))
        {
            value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            int const game = gs_inferGameFromDirectory(value);
            if (game == targetGame)
            {
                if (targetGame == GS_GAME_WORLDTOUR)
                    gs_setWorldTourDataDir(value);
                else
                    gs_setInferredDataDir(value);
                if (gs_needsMainGameGrpInjection(targetGame) && targetGame != GS_GAME_WORLDTOUR)
                    gs_setMainGameGrpFile(gs_defaultMainGameGrp(targetGame));
            }
            continue;
        }

        if (argv[i][0] != '-')
            gs_captureExplicitGameDataPath(argv[i], targetGame);
    }
}

static int gs_gameGrpOption(char const *arg, char const **inlineValue)
{
    return gs_valueOptionWithAttached(arg, "--gamegrp", "-gamegrp", NULL, inlineValue);
}

static int gs_grpOption(char const *arg, char const **inlineValue)
{
    if (gs_valueOptionWithAttached(arg, "--grp", "-grp", NULL, inlineValue)) return 1;
    if (gs_option(arg, NULL, "-g", NULL, inlineValue)) return 1;
    if (!Bstrncasecmp(arg, "-g", 2) && arg[2] && arg[2] != '=' &&
        Bstrncasecmp(arg, "-gm", 3) && Bstrncasecmp(arg, "-game", 5) &&
        Bstrncasecmp(arg, "-gjs", 4) && Bstrncasecmp(arg, "-gts", 4))
    {
        if (inlineValue) *inlineValue = arg + 2;
        return 1;
    }
    return 0;
}

static int gs_dataFileOption(char const *arg, char const **inlineValue)
{
    return gs_grpOption(arg, inlineValue) || gs_gameGrpOption(arg, inlineValue) ||
           gs_valueOptionWithAttached(arg, "--rff", "-rff", NULL, inlineValue) ||
           gs_valueOptionWithAttached(arg, "--ini", "-ini", NULL, inlineValue);
}

static int gs_gameDirOption(char const *arg, char const **inlineValue)
{
    return gs_valueOptionWithAttached(arg, "--game-dir", "-game_dir", "-game_dir", inlineValue);
}

static int gs_conOption(char const *arg, char const **inlineValue)
{
    return gs_valueOptionWithAttached(arg, "--con", "-x", NULL, inlineValue);
}

static int gs_searchPathOption(char const *arg, char const **inlineValue)
{
    // Do not let the `-j` path shorthand consume VibeBuild32's `-js` script option.
    if (!Bstrncasecmp(arg, "-js", 3)) return 0;
    return gs_valueOptionWithAttached(arg, "--search-path", "-j", NULL, inlineValue) ||
           gs_valueOptionWithAttached(arg, "--path", NULL, NULL, inlineValue);
}

static int gs_inferGameFromArgs(int argc, char const * const *argv, char *reason, int reasonSize)
{
    for (int i = 1; i < argc; ++i)
    {
        char const *inlineValue = NULL;
        char const *value = NULL;
        int crossplayDomain = -1;

        if (gs_crossplayDomainOption(argv[i], &crossplayDomain, &inlineValue))
        {
            (void)gs_takeDomainOptionValue(&i, argc, argv, inlineValue);
            continue;
        }

        if (gs_dataFileOption(argv[i], &inlineValue))
        {
            value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            int const game = gs_inferGameFromDataPath(value);
            if (game >= 0)
            {
                if (reason) Bsnprintf(reason, reasonSize, "data file '%s'", value ? value : "");
                return game;
            }
            continue;
        }

        if (gs_gameDirOption(argv[i], &inlineValue))
        {
            value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            int const game = gs_inferGameFromDirectory(value);
            if (game >= 0)
            {
                if (game == GS_GAME_WORLDTOUR)
                    gs_setWorldTourDataDir(value);
                else
                    gs_setInferredDataDir(value);
                if (reason) Bsnprintf(reason, reasonSize, "game directory '%s'", value ? value : "");
                return game;
            }
            continue;
        }

        if (gs_searchPathOption(argv[i], &inlineValue))
        {
            value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            int const game = gs_inferGameFromDirectory(value);
            if (game >= 0)
            {
                if (game == GS_GAME_WORLDTOUR)
                    gs_setWorldTourDataDir(value);
                else
                    gs_setInferredDataDir(value);
                if (reason) Bsnprintf(reason, reasonSize, "search path '%s'", value ? value : "");
                return game;
            }
            continue;
        }

        // A bare data file on the command line is accepted by the existing
        // game parsers; use it for selection too.
        if (argv[i][0] != '-')
        {
            int const game = gs_inferGameFromDataPath(argv[i]);
            if (game >= 0)
            {
                if (reason) Bsnprintf(reason, reasonSize, "data file '%s'", argv[i]);
                return game;
            }
        }
    }
    return -1;
}

// First-cut picker: a console prompt listing whichever games are present.
// (A graphical picker in the startup window is a follow-up.) Returns the game
// index (0=Duke, 1=SW, 2=Blood, 3=Redneck Rampage).
static int gs_menu(int const gameAvail[GS_GAME_COUNT])
{
    int opt[GS_GAME_COUNT], nopt = 0;
    fprintf(stderr, "\nSelect a game:\n");
    for (auto const &spec : gs_gameSpecs)
    {
        if (!gameAvail[spec.id]) continue;
        opt[nopt] = spec.id;
        fprintf(stderr, "  %d) %s\n", nopt + 1, spec.name);
        nopt++;
    }
    if (nopt <= 1)
        return nopt == 1 ? opt[0] : GS_GAME_DUKE;
    fprintf(stderr, "Choice [1]: ");
    fflush(stderr);
    char line[16] = "";
    int pick = 1;
    if (fgets(line, sizeof(line), stdin))
    {
        int const c = line[0] - '0';
        if (c >= 1 && c <= nopt) pick = c;
    }
    return opt[pick - 1];
}

static int gs_firstAvailable(int const gameAvail[GS_GAME_COUNT])
{
    for (auto const &spec : gs_gameSpecs)
        if (gameAvail[spec.id])
            return spec.id;
    return GS_GAME_DUKE;
}

static int gs_availableCount(int const gameAvail[GS_GAME_COUNT])
{
    int count = 0;
    for (auto const &spec : gs_gameSpecs)
        count += gameAvail[spec.id] ? 1 : 0;
    return count;
}

static int gs_effectiveCrossplayDomainGame(int domain)
{
    if ((unsigned)domain >= ART_CROSSPLAY_DOMAIN_COUNT)
        return g_activeGame;

    return g_crossplayRequestedDomainGames[domain] >= 0 ? g_crossplayRequestedDomainGames[domain] : g_activeGame;
}

static int gs_configuredCrossplayDomainGame(int domain)
{
    if ((unsigned)domain >= ART_CROSSPLAY_DOMAIN_COUNT)
        return ART_CROSSPLAY_GAME_UNKNOWN;

    return g_crossplayRequestedDomainGames[domain];
}

static int gs_foreignCrossplayDataGame(char const *path)
{
    if (!path || !path[0])
        return -1;

    char expanded[BMAX_PATH];
    char const *dataPath = gs_expandHomePath(path, expanded, sizeof(expanded));

    int32_t size = 0;
    uint32_t crc = 0;
    int game = -1;

    if (gs_fileSignature(dataPath, &size, &crc))
        game = gs_inferGameFromSignature(size, crc);
    if (game < 0 && (gs_isBloodRffFile(dataPath) || gs_isBloodIniFile(dataPath)))
        game = GS_GAME_BLOOD;
    if (game < 0)
        game = gs_inferGameFromBasename(dataPath);
    if (game < 0)
        game = gs_inferGameFromDirectory(dataPath);

    return (game >= 0 && game != g_activeGame) ? game : -1;
}

static void gs_trackAvailableCrossplayDomainData(int game, char gameDirs[GS_GAME_COUNT][BMAX_PATH],
                                                 char gameMainGrpFiles[GS_GAME_COUNT][BMAX_PATH])
{
    if (game < 0 || game >= GS_GAME_COUNT || game == g_activeGame)
        return;

    if (gameDirs[game][0] && gameMainGrpFiles[game][0])
    {
        char path[BMAX_PATH * 2];
        gs_joinPath(path, sizeof(path), gameDirs[game], gameMainGrpFiles[game]);
        gs_trackForeignDataPath(path);
        return;
    }

    if (gameDirs[game][0])
        gs_trackForeignDataPath(gameDirs[game]);
}

static void gs_applyCrossplayDomains(char gameDirs[GS_GAME_COUNT][BMAX_PATH],
                                     char gameMainGrpFiles[GS_GAME_COUNT][BMAX_PATH])
{
    int const levelsGame = gs_configuredCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS);
    int const itemsGame = gs_configuredCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_ITEMS);
    int const weaponsGame = gs_configuredCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_WEAPONS);
    int const effectiveLevelsGame = gs_effectiveCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS);
    int const effectiveItemsGame = gs_effectiveCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_ITEMS);
    int const effectiveWeaponsGame = gs_effectiveCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_WEAPONS);

    artCrossplaySetGameDomains(g_activeGame, levelsGame, itemsGame, weaponsGame);

    if (levelsGame >= 0 || itemsGame >= 0 || weaponsGame >= 0)
    {
        LOG_F(INFO, "Crossplay: runtime=%s, levels=%s, items=%s, weapons=%s.",
              gs_gameName(g_activeGame), gs_gameName(effectiveLevelsGame), gs_gameName(effectiveItemsGame), gs_gameName(effectiveWeaponsGame));
    }

    if (levelsGame >= 0)
        gs_trackAvailableCrossplayDomainData(levelsGame, gameDirs, gameMainGrpFiles);
    if (itemsGame >= 0)
        gs_trackAvailableCrossplayDomainData(itemsGame, gameDirs, gameMainGrpFiles);
    if (weaponsGame >= 0)
        gs_trackAvailableCrossplayDomainData(weaponsGame, gameDirs, gameMainGrpFiles);
}

static void gs_choose(int argc, char const * const * argv)
{
    const char *want = NULL;
    // A debugger launch must never wait for the interactive game picker: VS
    // Code has no way to answer it.  Explicit --game/-gm still wins; without
    // one, retain the normal first-available fallback below.
    int bypass = gs_debugModeRequested(argc, argv);
    for (int i = 1; i < argc; i++)
    {
        const char *inlineValue = NULL;
        if (gs_gameOption(argv[i], &inlineValue))
        {
            want = gs_takeOptionValue(&i, argc, argv, inlineValue);
        }
        else if (gs_option(argv[i], "--no-game-menu", "-ngm", "-nogamemenu", NULL) ||
                 gs_option(argv[i], "--skip-menu", "-sm", NULL, NULL))
            bypass = 1;
    }

    gs_mainGameGrpFile[0] = 0;
    gs_inferredDataDir[0] = 0;
    char gameDirs[GS_GAME_COUNT][BMAX_PATH] = {};
    char gameMainGrpFiles[GS_GAME_COUNT][BMAX_PATH] = {};
    int gameAvail[GS_GAME_COUNT] = {};
    for (auto const &spec : gs_gameSpecs)
        gameAvail[spec.id] = gs_gameAvail(spec.id, gameDirs[spec.id], sizeof(gameDirs[spec.id]),
                                          gameMainGrpFiles[spec.id], sizeof(gameMainGrpFiles[spec.id]));

    int sel = -1;
    if (want)
    {
        sel = gs_gameFromName(want);
        gs_captureExplicitGameArgs(argc, argv, sel);
    }
    else
    {
        char inferenceReason[BMAX_PATH * 2] = "";
        int const inferred = gs_inferGameFromArgs(argc, argv, inferenceReason, sizeof(inferenceReason));
        if (inferred >= 0)
        {
            sel = inferred;
            LOG_F(INFO, "Game selector: inferred %s from %s", gs_gameName(sel), inferenceReason);
        }
    }
    if (sel < 0)
    {
        int const nAvail = gs_availableCount(gameAvail);
        if (nAvail == 1)
            sel = gs_firstAvailable(gameAvail);
        else if (nAvail > 1)
            sel = bypass ? gs_firstAvailable(gameAvail) : gs_menu(gameAvail);
        else
            sel = GS_GAME_DUKE;  // none found; let Duke's normal grp search report it
    }

    sel = gs_linkedGameOrDuke(sel);
    g_activeGame = sel;
    if (gs_usesDukeRuntime(sel))
        g_gameType = gs_dukeRuntimeGameFlags(sel);
    if (gs_needsMainGameGrpInjection(sel) && !gs_mainGameGrpFile[0] && gameMainGrpFiles[sel][0])
        gs_setMainGameGrpFile(gameMainGrpFiles[sel]);

    gs_applyCrossplayDomains(gameDirs, gameMainGrpFiles);

    const char *const name = gs_gameName(sel);
    const char *dir = gs_inferredDataDir[0] ? gs_inferredDataDir : gameDirs[sel];
    if (dir[0] && Bstrcmp(dir, "."))
    {
        if (gs_chdir(dir) == 0)
            LOG_F(INFO, "Game selector: switched into '%s' for %s", dir, name);
    }
    LOG_F(INFO, "Game selector: launching %s", name);
}

static void gs_emitIniOption(char const **out, int *count, char const *value)
{
    char *directory = NULL;
    char const *base = value;
    if (g_activeGame == GS_GAME_BLOOD && value && gs_splitPathDirectoryAndBase(value, &directory, &base))
        gs_emitOptionValue(out, count, "-game_dir", directory);
    gs_emitOptionValue(out, count, "-ini", value ? base : NULL);
}

// Build a copy of argv with the selector's own options (--game/-gm <name>,
// --no-game-menu/-ngm, and --js/-js <file>) removed, so the chosen game's parser
// never sees them. Also splits the
// modern "--name=value" form into two tokens ("--name", "value") so every game's
// option parser (which reads the value as the following token) accepts it uniformly.
static char const **gs_filterArgv(int argc, char const * const * argv, int *outArgc)
{
    // Up to 2 entries per arg (a "-name=value" splits in two), plus injected -gamegrp.
    char const **out = (char const **)Xmalloc((3 * argc + 12) * sizeof(char *));
    int n = 0;
    int haveGameGrp = 0;
    int haveCon = 0;
    int netArgStart = -1;
    char const *netInlineValue = NULL;
    // All games already understand -nosetup.  Translate the VibeBuild32
    // launcher spelling once, after the game selector has consumed it.
    int skipMenu = gs_debugModeRequested(argc, argv);
    int forceMenu = 0;
    for (int i = 0; i < argc; i++)
    {
        const char *inlineValue = NULL;
        // Duke's legacy -l/-v parser only understands attached numbers.
        // Present a single canonical form for every public spelling:
        // -l1, -l 1, --level1, --level 1 (and the volume equivalents).
        char const *compactName = NULL;
        if (gs_compactValueOption(argv[i], "--level", "-l", &inlineValue)) compactName = "-l";
        else if (gs_compactValueOption(argv[i], "--volume", "-v", &inlineValue)) compactName = "-v";
        else if (gs_compactValueOption(argv[i], "--episode", "-e", &inlineValue)) compactName = "-v";
        if (compactName)
        {
            if (!inlineValue || !inlineValue[0])
            {
                if (i + 1 >= argc) { out[n++] = compactName; continue; }
                inlineValue = argv[++i];
            }
            // SW's original command line uses an attached `-level#` rather
            // than Duke's `-l#`.  Keep the public VibeBuild32 spelling the
            // same for every game while translating it at the final dispatch.
            char const *const targetName = (g_activeGame == GS_GAME_SW && !Bstrcmp(compactName, "-l")) ? "-level" : compactName;
            size_t const nameLength = Bstrlen(targetName), valueLength = Bstrlen(inlineValue);
            char *combined = (char *)Xmalloc(nameLength + valueLength + 1);
            Bmemcpy(combined, targetName, nameLength);
            Bmemcpy(combined + nameLength, inlineValue, valueLength + 1);
            out[n++] = combined;
            continue;
        }
        inlineValue = NULL;
        if (gs_option(argv[i], "--net", "-net", NULL, &inlineValue))
        {
            netArgStart = i;
            netInlineValue = inlineValue;
            break;
        }
        inlineValue = NULL;
        int crossplayDomain = -1;
        if (gs_crossplayDomainOption(argv[i], &crossplayDomain, &inlineValue))
        {
            (void)gs_takeDomainOptionValue(&i, argc, argv, inlineValue);
            continue;
        }
        inlineValue = NULL;
        if (gs_gameOption(argv[i], &inlineValue))
        {
            (void)gs_takeOptionValue(&i, argc, argv, inlineValue);  // skip flag + its separate value
            continue;
        }
        if (gs_option(argv[i], "--no-game-menu", "-ngm", "-nogamemenu", NULL)) continue;
        if (gs_option(argv[i], "--skip-menu", "-sm", NULL, NULL)) { skipMenu = 1; continue; }
        if (gs_option(argv[i], "--menu", "-m", NULL, NULL))
        {
            // All game frontends understand their longstanding -setup switch.
            // Keep this explicit override even when -dbg implied --skip-menu.
            forceMenu = 1;
            out[n++] = "-setup";
            continue;
        }
        if (gs_option(argv[i], "--skip-cinema", "-sc", NULL, NULL))
        {
            // -nologo is the shared engine-level suppression used by Duke,
            // Shadow Warrior, Blood, and Redneck for their startup movies.
            out[n++] = "-nologo";
            continue;
        }
        inlineValue = NULL;
        if (gs_monsterOption(argv[i], &inlineValue))
        {
            // RR's legacy parser calls this option `-enemies`; the other
            // games receive the public spelling directly.
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            gs_emitOptionValue(out, &n, g_activeGame == GS_GAME_RR ? "-enemies" : "-monsters", value);
            continue;
        }
        if (gs_option(argv[i], "--window", "-wnd", NULL, NULL))
        {
            out[n++] = "-window";
            continue;
        }
        if (gs_option(argv[i], "--fullscreen", "-fscr", NULL, NULL))
        {
            out[n++] = "-fullscreen";
            continue;
        }
        inlineValue = NULL;
        if (gs_resolutionOption(argv[i], &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            int width = 0, height = 0;
            if (!gs_parseResolution(value, &width, &height))
            {
                fprintf(stderr, "Invalid resolution '%s' (use WIDTHxHEIGHT, from 320x200 through 8192x4320).\n", value ? value : "");
                continue;
            }
            char *resolution = (char *)Xmalloc(24);
            Bsnprintf(resolution, 24, "%dx%d", width, height);
            gs_emitOptionValue(out, &n, "-resolution", resolution);
            continue;
        }
        inlineValue = NULL;
        if (gs_colorOption(argv[i], &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            int const color = value ? gs_playerColorIndex(value) : -1;
            if (color < 0)
            {
                fprintf(stderr, "Invalid player color '%s' (use brown, gray, purple, red, yellow, olive, green, blue, or #rrggbb).\n", value ? value : "");
                continue;
            }
            char *nativeColor = (char *)Xmalloc(4);
            Bsnprintf(nativeColor, 4, "%d", gs_nativePlayerColor(color));
            gs_emitOptionValue(out, &n, "-color", nativeColor);
            continue;
        }
        inlineValue = NULL;
        if (gs_gameDirOption(argv[i], &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            gs_trackForeignDataPath(value);
            int const foreignGame = gs_foreignCrossplayDataGame(value);
            if (foreignGame >= 0)
            {
                LOG_F(INFO, "Crossplay: not mounting %s as runtime game data; using it only as %s foreign data.",
                      value ? value : "", gs_gameName(foreignGame));
                continue;
            }
            gs_emitOptionValue(out, &n, "-game_dir", value);
            continue;
        }
        inlineValue = NULL;
        if (gs_searchPathOption(argv[i], &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            gs_trackForeignDataPath(value);
            int const foreignGame = gs_foreignCrossplayDataGame(value);
            if (foreignGame >= 0)
            {
                LOG_F(INFO, "Crossplay: not mounting %s as runtime search path; using it only as %s foreign data.",
                      value ? value : "", gs_gameName(foreignGame));
                continue;
            }
            gs_emitOptionValue(out, &n, "-j", value);
            continue;
        }
        inlineValue = NULL;
        int const isGameGrp = gs_gameGrpOption(argv[i], &inlineValue);
        if (isGameGrp || gs_grpOption(argv[i], &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);

            if (isGameGrp)
            {
                haveGameGrp = 1;
                gs_emitOptionValue(out, &n, "-gamegrp", value);
                continue;
            }

            // Public -g/--grp is commonly used as "the game data file". For
            // Duke-runtime derivatives that need a specific main GRP, translate
            // a recognized data file into -gamegrp so the native parser sets
            // the correct g_gameType instead of mounting it as an add-on.
            if (gs_needsMainGameGrpInjection(g_activeGame) &&
                (gs_inferGameFromBasename(value) == g_activeGame ||
                 (g_activeGame == GS_GAME_WORLDTOUR && value && !Bstrcasecmp(gs_basename(value), "DUKE3D.GRP"))))
            {
                haveGameGrp = 1;
                gs_emitOptionValue(out, &n, "-gamegrp", value);
                continue;
            }

            if (g_activeGame == GS_GAME_BLOOD && value && gs_extEquals(value, ".rff"))
            {
                gs_emitOptionValue(out, &n, "-rff", value);
                continue;
            }
            if (g_activeGame == GS_GAME_BLOOD && value && gs_extEquals(value, ".ini"))
            {
                gs_emitIniOption(out, &n, value);
                continue;
            }

            gs_trackForeignDataPath(value);

            if (value && (gs_extEquals(value, ".rff") || gs_extEquals(value, ".ini")))
                continue;

            int const foreignGame = gs_foreignCrossplayDataGame(value);
            if (foreignGame >= 0)
            {
                LOG_F(INFO, "Crossplay: not mounting %s as runtime GRP; using it only as %s foreign data.",
                      value ? value : "", gs_gameName(foreignGame));
                continue;
            }

            gs_emitOptionValue(out, &n, "-g", value);
            continue;
        }
        inlineValue = NULL;
        if (gs_valueOptionWithAttached(argv[i], "--rff", "-rff", NULL, &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            gs_emitOptionValue(out, &n, "-rff", value);
            continue;
        }
        inlineValue = NULL;
        if (gs_valueOptionWithAttached(argv[i], "--ini", "-ini", NULL, &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            gs_emitIniOption(out, &n, value);
            continue;
        }
        inlineValue = NULL;
        if (gs_option(argv[i], "--js", "-js", NULL, &inlineValue))
        {
            (void)gs_takeOptionValue(&i, argc, argv, inlineValue);  // the script path was already captured by app_main
            continue;
        }
        inlineValue = NULL;
        if (gs_conOption(argv[i], &inlineValue))
        {
            char const *value = gs_takeOptionValue(&i, argc, argv, inlineValue);
            haveCon = 1;
            gs_emitOptionValue(out, &n, "-x", value);
            continue;
        }

        // Modern args: split an option token's "=value" off into its own token.
        const char *a = argv[i], *eq;
        if (a[0] == '-' && (eq = strchr(a, '=')) != NULL && eq > a + 1)
        {
            size_t const nameLen = (size_t)(eq - a);
            char *name = (char *)Xmalloc(nameLen + 1);
            Bmemcpy(name, a, nameLen); name[nameLen] = 0;
            out[n++] = name;
            out[n++] = eq + 1;   // value (stable: points into the original argv string)
            continue;
        }
        out[n++] = a;
    }
    // If the selector chose a game whose runtime depends on a main GRP, name it
    // explicitly so the native parser switches into that game's data/type.
    if (gs_needsMainGameGrpInjection(g_activeGame) && gs_mainGameGrpFile[0] && !haveGameGrp)
    {
        gs_emitOptionValue(out, &n, "-gamegrp", gs_mainGameGrpFile);
    }
    else if (gs_needsMainGameGrpInjection(g_activeGame) && !haveGameGrp)
    {
        gs_emitOptionValue(out, &n, "-gamegrp", gs_defaultMainGameGrp(g_activeGame));
    }
    if (g_activeGame == GS_GAME_WORLDTOUR && !haveCon)
        gs_emitOptionValue(out, &n, "-x", "GAME.CON");
    if (skipMenu && !forceMenu) out[n++] = "-nosetup";
    if (netArgStart >= 0)
    {
        out[n++] = "-net";
        if (netInlineValue && netInlineValue[0])
            out[n++] = netInlineValue;
        for (int i = netArgStart + 1; i < argc; ++i)
            out[n++] = argv[i];
    }
    out[n] = NULL;
    *outArgc = n;
    return out;
}

// --- the engine entry point --------------------------------------------------
int app_main(int32_t argc, char const * const * argv)
{
    artCrossplayClearForeignData();
    VBJS_CaptureLaunchDir();   // VibeBuild32: record the launch dir now (before any chdir) for autojs/
    VBJS_SetExecutablePath(argc > 0 ? argv[0] : NULL);
    if (gs_debugModeRequested(argc, argv))
    {
        // Enable the shared debugger before dispatch. Shadow Warrior has its
        // own command-line parser and therefore never reaches Duke's -dbg
        // branch, but VS Code still needs the same localhost bridge.
        VBJS_SetDebug(1);
        for (int i = 1; i < argc; ++i)
            if (gs_option(argv[i], "--devtools", "-dt", NULL, NULL)) { VBJS_SetDevtools(1); break; }
        gs_collectDebugParams();
    }
    int launchArgc = argc;
    char const * const *launchArgv = gs_buildArgvWithDebugParams(argc, argv, &launchArgc);
    gs_parseCrossplayDomainArgs(launchArgc, launchArgv);

    // Parse launcher-owned script arguments before Duke's legacy parser. In
    // particular, the legacy -net option treats later tokens as network data;
    // a VibeBuild option must work in any position instead of being swallowed.
    for (int i = 1; i < launchArgc; ++i)
    {
        const char *inlineValue = NULL;
        if (!gs_option(launchArgv[i], "--js", "-js", NULL, &inlineValue)) continue;
        const char *path = gs_takeOptionValue(&i, launchArgc, launchArgv, inlineValue);
        if (path && path[0]) VBJS_SetScriptPath(path);
    }

    // This is a launcher command, not a Duke-only gameplay option: generate a
    // project before choosing a game, then exit without opening a game window.
    for (int i = 1; i < launchArgc; ++i)
    {
        const char *arg = launchArgv[i];
        const char *name = nullptr;
        int typescript = 0;
        if (!Bstrncasecmp(arg, "--gen-js=", 9) || !Bstrncasecmp(arg, "-gjs=", 5))
            name = strchr(arg, '=') + 1;
        else if (!Bstrcasecmp(arg, "--gen-js") || !Bstrcasecmp(arg, "-gjs"))
            name = (i + 1 < launchArgc && launchArgv[i + 1][0] != '-') ? launchArgv[++i] : nullptr;
        else if (!Bstrncasecmp(arg, "--gen-ts=", 9) || !Bstrncasecmp(arg, "-gts=", 5))
            typescript = 1, name = strchr(arg, '=') + 1;
        else if (!Bstrcasecmp(arg, "--gen-ts") || !Bstrcasecmp(arg, "-gts"))
            typescript = 1, name = (i + 1 < launchArgc && launchArgv[i + 1][0] != '-') ? launchArgv[++i] : nullptr;
        else continue;

        if (name && !name[0]) name = nullptr;
        if (VBJS_GenerateProject(name, typescript) != 0)
        {
            fprintf(stderr, "Could not create JavaScript project.\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    gs_choose(launchArgc, launchArgv);

    if (getenv("VIBEBUILD32_SELECTOR_DRY_RUN"))
    {
        LOG_F(INFO, "Game selector: selected %s", gs_gameName(g_activeGame));
        return EXIT_SUCCESS;
    }

    int fargc = 0;
    char const **fargv = gs_filterArgv(launchArgc, launchArgv, &fargc);

#ifdef WITH_SW
    if (g_activeGame == GS_GAME_SW)
        return SW_app_main(fargc, fargv);
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == GS_GAME_BLOOD)
        return Blood_app_main(fargc, fargv);
#endif
#ifdef WITH_RR
    if (g_activeGame == GS_GAME_RR)
        return RR_app_main(fargc, fargv);
#endif
#ifdef WITH_EXHUMED
    if (g_activeGame == GS_GAME_EXHUMED)
        return Exhumed_app_main(fargc, fargv);
#endif
    return Duke_app_main(fargc, fargv);
}

// --- callbacks both games define: route to the active one --------------------
// These have C linkage in the engine/game headers, so the dispatchers must too.
extern "C" const char *G_DefaultDefFile(void)
{
#ifdef WITH_SW
    if (g_activeGame == GS_GAME_SW) return SW_G_DefaultDefFile();
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == GS_GAME_BLOOD) return Blood_G_DefaultDefFile();
#endif
#ifdef WITH_RR
    if (g_activeGame == GS_GAME_RR) return RR_G_DefaultDefFile();
#endif
#ifdef WITH_EXHUMED
    if (g_activeGame == GS_GAME_EXHUMED) return Exhumed_G_DefaultDefFile();
#endif
    if (g_activeGame == GS_GAME_IONFURY) return "ashock.def";
    return Duke_G_DefaultDefFile();
}

extern "C" void faketimerhandler(void)
{
#ifdef WITH_SW
    if (g_activeGame == GS_GAME_SW) { SW_faketimerhandler(); return; }
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == GS_GAME_BLOOD) { Blood_faketimerhandler(); return; }
#endif
#ifdef WITH_RR
    if (g_activeGame == GS_GAME_RR) { RR_faketimerhandler(); return; }
#endif
#ifdef WITH_EXHUMED
    if (g_activeGame == GS_GAME_EXHUMED) { Exhumed_faketimerhandler(); return; }
#endif
    Duke_faketimerhandler();
}
