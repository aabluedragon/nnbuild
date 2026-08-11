// Game selector: one vibebuild32 binary that can launch either Duke3D
// (vibebuild32) or Shadow Warrior (the VoidSW port), sharing the engine.
//
// The engine's main() calls app_main(); this file provides that app_main as a
// dispatcher. Each game's real entry and the few engine callbacks that both
// games define (see UNIFY_WITH_VIBEBUILD32.md) are renamed Duke_*/SW_*, and the
// canonical names here route to the active game.
//
// Selection (per spec):
//   --game/-gm <sw|duke>  pick a game explicitly
//   --no-game-menu/-ngm  bypass the picker (use --game, else the single available
//                     game, else Duke)
// The GRP is looked for both in the working directory and in a per-game subdir
// (duke3d/DUKE3D.GRP, sw/Sw.grp); the chosen game switches into that folder.
//
// SW is only linked (and dispatched to) when built with WITH_SW; without it this
// is a transparent pass-through to Duke, so the default build is unchanged.

#include "compat.h"
#include "baselayer.h"
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

// 0 = Duke3D (vibebuild32), 1 = Shadow Warrior, 2 = Blood, 3 = Redneck Rampage.
// Read by the dispatchers.
int g_activeGame = 0;

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

// --- helpers -----------------------------------------------------------------
static int gs_fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
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
            *inlineValue = arg + n + 1;
            return 1;
        }
    }
    return 0;
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
    if (g_activeGame == 1) return color;
    if (g_activeGame == 2) return blood[color];
    return dukeRR[color];
}

static int gs_colorOption(char const *arg, char const **inlineValue)
{
    if (gs_option(arg, "--color", "-clr", NULL, inlineValue)) return 1;
    if (!Bstrncasecmp(arg, "--color", 7) && arg[7])
    {
        *inlineValue = arg + 7;
        return 1;
    }
    if (!Bstrncasecmp(arg, "-clr", 4) && arg[4])
    {
        *inlineValue = arg + 4;
        return 1;
    }
    return 0;
}

static int gs_resolutionOption(char const *arg, char const **inlineValue)
{
    if (gs_option(arg, "--resolution", "-res", NULL, inlineValue)) return 1;
    if (!Bstrncasecmp(arg, "--resolution", 12) && arg[12])
    {
        *inlineValue = arg + 12;
        return 1;
    }
    if (!Bstrncasecmp(arg, "-res", 4) && arg[4])
    {
        *inlineValue = arg + 4;
        return 1;
    }
    return 0;
}

static int gs_monsterOption(char const *arg, char const **inlineValue)
{
    if (gs_option(arg, "--monsters", "-mnst", NULL, inlineValue)) return 1;
    if (!Bstrncasecmp(arg, "--monsters", 10) && arg[10])
    {
        *inlineValue = arg + 10 + (arg[10] == '=');
        return 1;
    }
    if (!Bstrncasecmp(arg, "-mnst", 5) && arg[5])
    {
        *inlineValue = arg + 5 + (arg[5] == '=');
        return 1;
    }
    return 0;
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

static char const **gs_buildArgvWithDebugParams(int argc, char const * const *argv, int *outArgc)
{
    if (!gs_debugParamCount) { *outArgc = argc; return (char const **)argv; }
    char const **out = (char const **)Xmalloc((size_t)(argc + gs_debugParamCount + 1) * sizeof(char *));
    for (int i = 0; i < argc; ++i) out[i] = argv[i];
    for (int i = 0; i < gs_debugParamCount; ++i) out[argc + i] = gs_debugParams[i];
    *outArgc = argc + gs_debugParamCount;
    out[*outArgc] = NULL;
    return out;
}

// Look for `grp` in the working dir, then in `subdir/`. On success writes the
// directory to hold the data (".", or the subdir) into outdir and returns 1.
static int gs_findGrp(const char *grp, const char *subdir, char *outdir, int outsz)
{
    if (gs_fileExists(grp)) { Bstrncpyz(outdir, ".", outsz); return 1; }
    char path[BMAX_PATH];
    Bsnprintf(path, sizeof(path), "%s/%s", subdir, grp);
    if (gs_fileExists(path)) { Bstrncpyz(outdir, subdir, outsz); return 1; }
    return 1 == 0;  // false
}

// Duke ships DUKE3D.GRP; SW ships Sw.grp (a few case variants seen in the wild).
static int gs_dukeAvail(char *dir, int sz) { return gs_findGrp("DUKE3D.GRP", "duke3d", dir, sz)
                                                  || gs_findGrp("duke3d.grp", "duke3d", dir, sz); }
static int gs_swAvail(char *dir, int sz)   { return gs_findGrp("SW.GRP", "sw", dir, sz)
                                                  || gs_findGrp("Sw.grp", "sw", dir, sz)
                                                  || gs_findGrp("sw.grp", "sw", dir, sz); }
// Blood ships BLOOD.RFF (resource file), in the cwd or a blood/ subdir.
static int gs_bloodAvail(char *dir, int sz){ return gs_findGrp("BLOOD.RFF", "blood", dir, sz)
                                                  || gs_findGrp("blood.rff", "blood", dir, sz); }
// Redneck Rampage ships REDNECK.GRP, in the cwd or an rr/ subdir. Remember which
// spelling matched so we can hand RR an explicit -gamegrp (its CRC autodetect does
// not recognize every REDNECK.GRP revision, and without a mounted grp all art/sound
// is missing).
static char gs_rrGrpFile[64] = "";
static int gs_rrAvail(char *dir, int sz)
{
    if (gs_findGrp("REDNECK.GRP", "rr", dir, sz)) { Bstrncpyz(gs_rrGrpFile, "REDNECK.GRP", sizeof(gs_rrGrpFile)); return 1; }
    if (gs_findGrp("redneck.grp", "rr", dir, sz)) { Bstrncpyz(gs_rrGrpFile, "redneck.grp", sizeof(gs_rrGrpFile)); return 1; }
    return 0;
}

// First-cut picker: a console prompt listing whichever games are present.
// (A graphical picker in the startup window is a follow-up.) Returns the game
// index (0=Duke, 1=SW, 2=Blood, 3=Redneck Rampage).
static int gs_menu(int dukeAvail, int swAvail, int bloodAvail, int rrAvail)
{
    int opt[4], nopt = 0;
    fprintf(stderr, "\nSelect a game:\n");
    if (dukeAvail)  { opt[nopt] = 0; fprintf(stderr, "  %d) Duke Nukem 3D\n",  nopt + 1); nopt++; }
    if (swAvail)    { opt[nopt] = 1; fprintf(stderr, "  %d) Shadow Warrior\n", nopt + 1); nopt++; }
    if (bloodAvail) { opt[nopt] = 2; fprintf(stderr, "  %d) Blood\n",          nopt + 1); nopt++; }
    if (rrAvail)    { opt[nopt] = 3; fprintf(stderr, "  %d) Redneck Rampage\n", nopt + 1); nopt++; }
    if (nopt <= 1)
        return nopt == 1 ? opt[0] : 0;
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
        if (gs_option(argv[i], "--game", "-gm", "-game", &inlineValue))
        {
            if (inlineValue) want = inlineValue;
            else if (i + 1 < argc) want = argv[++i];
        }
        else if (gs_option(argv[i], "--no-game-menu", "-ngm", "-nogamemenu", NULL) ||
                 gs_option(argv[i], "--skip-menu", "-sm", NULL, NULL))
            bypass = 1;
    }

    char dukeDir[BMAX_PATH] = "", swDir[BMAX_PATH] = "", bloodDir[BMAX_PATH] = "", rrDir[BMAX_PATH] = "";
    int const dukeAvail  = gs_dukeAvail(dukeDir, sizeof(dukeDir));
    int const swAvail    = gs_swAvail(swDir, sizeof(swDir));
    int const bloodAvail = gs_bloodAvail(bloodDir, sizeof(bloodDir));
    int const rrAvail    = gs_rrAvail(rrDir, sizeof(rrDir));

    // pick the first available game (Duke > SW > Blood > RR) as the fallback default
    #define GS_FIRST_AVAIL (dukeAvail ? 0 : (swAvail ? 1 : (bloodAvail ? 2 : 3)))

    int sel = -1;
    if (want)
    {
        if (!Bstrcasecmp(want, "sw") || !Bstrcasecmp(want, "shadowwarrior")) sel = 1;
        else if (!Bstrcasecmp(want, "blood")) sel = 2;
        else if (!Bstrcasecmp(want, "rr") || !Bstrcasecmp(want, "redneck") || !Bstrcasecmp(want, "redneckrampage")) sel = 3;
        else if (!Bstrcasecmp(want, "duke") || !Bstrcasecmp(want, "duke3d")) sel = 0;
    }
    if (sel < 0)
    {
        int const nAvail = (dukeAvail ? 1 : 0) + (swAvail ? 1 : 0) + (bloodAvail ? 1 : 0) + (rrAvail ? 1 : 0);
        if (nAvail == 1)
            sel = GS_FIRST_AVAIL;
        else if (nAvail > 1)
            sel = bypass ? GS_FIRST_AVAIL : gs_menu(dukeAvail, swAvail, bloodAvail, rrAvail);
        else
            sel = 0;  // none found; let Duke's normal grp search report it
    }
    #undef GS_FIRST_AVAIL

#ifndef WITH_SW
    if (sel == 1) sel = 0;  // SW not compiled in
#endif
#ifndef WITH_BLOOD
    if (sel == 2) sel = 0;  // Blood not compiled in
#endif
#ifndef WITH_RR
    if (sel == 3) sel = 0;  // Redneck Rampage not compiled in
#endif
    g_activeGame = sel;

    const char *const name = (sel == 3) ? "Redneck Rampage" : (sel == 2) ? "Blood" : (sel == 1) ? "Shadow Warrior" : "Duke Nukem 3D";
    const char *dir = (sel == 3) ? rrDir : (sel == 2) ? bloodDir : (sel == 1) ? swDir : dukeDir;
    if (dir[0] && Bstrcmp(dir, "."))
    {
        if (gs_chdir(dir) == 0)
            LOG_F(INFO, "Game selector: switched into '%s' for %s", dir, name);
    }
    LOG_F(INFO, "Game selector: launching %s", name);
}

// Build a copy of argv with the selector's own options (--game/-gm <name>,
// --no-game-menu/-ngm, and --js/-js <file>) removed, so the chosen game's parser
// never sees them. Also splits the
// modern "--name=value" form into two tokens ("--name", "value") so every game's
// option parser (which reads the value as the following token) accepts it uniformly.
static char const **gs_filterArgv(int argc, char const * const * argv, int *outArgc)
{
    // Up to 2 entries per arg (a "-name=value" splits in two), plus injected -gamegrp.
    char const **out = (char const **)Xmalloc((2 * argc + 4) * sizeof(char *));
    int n = 0;
    int haveGameGrp = 0;
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
            char const *const targetName = (g_activeGame == 1 && !Bstrcmp(compactName, "-l")) ? "-level" : compactName;
            size_t const nameLength = Bstrlen(targetName), valueLength = Bstrlen(inlineValue);
            char *combined = (char *)Xmalloc(nameLength + valueLength + 1);
            Bmemcpy(combined, targetName, nameLength);
            Bmemcpy(combined + nameLength, inlineValue, valueLength + 1);
            out[n++] = combined;
            continue;
        }
        if (gs_option(argv[i], "--game", "-gm", "-game", &inlineValue))
        {
            if (!inlineValue) i++;  // skip flag + its separate value
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
            out[n++] = g_activeGame == 3 ? "-enemies" : "-monsters";
            if (inlineValue) out[n++] = inlineValue;
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
            char const *value = inlineValue;
            if (!value && i + 1 < argc) value = argv[++i];
            int width = 0, height = 0;
            if (!gs_parseResolution(value, &width, &height))
            {
                fprintf(stderr, "Invalid resolution '%s' (use WIDTHxHEIGHT, from 320x200 through 8192x4320).\n", value ? value : "");
                continue;
            }
            char *resolution = (char *)Xmalloc(24);
            Bsnprintf(resolution, 24, "%dx%d", width, height);
            out[n++] = "-resolution";
            out[n++] = resolution;
            continue;
        }
        inlineValue = NULL;
        if (gs_colorOption(argv[i], &inlineValue))
        {
            char const *value = inlineValue;
            if (!value && i + 1 < argc) value = argv[++i];
            int const color = value ? gs_playerColorIndex(value) : -1;
            if (color < 0)
            {
                fprintf(stderr, "Invalid player color '%s' (use brown, gray, purple, red, yellow, olive, green, blue, or #rrggbb).\n", value ? value : "");
                continue;
            }
            char *nativeColor = (char *)Xmalloc(4);
            Bsprintf(nativeColor, "%d", gs_nativePlayerColor(color));
            out[n++] = "-color";
            out[n++] = nativeColor;
            continue;
        }
        inlineValue = NULL;
        if (gs_option(argv[i], "--js", "-js", NULL, &inlineValue))
        {
            if (!inlineValue) i++;  // the script path was already captured by app_main
            continue;
        }
        if (!Bstrcasecmp(argv[i], "-gamegrp")) haveGameGrp = 1;

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
    // RR's grp CRC autodetect misses some REDNECK.GRP revisions; since we already
    // located the file, name it explicitly so its art/sound actually mount.
    if (g_activeGame == 3 && gs_rrGrpFile[0] && !haveGameGrp)
    {
        out[n++] = "-gamegrp";
        out[n++] = gs_rrGrpFile;
    }
    if (skipMenu && !forceMenu) out[n++] = "-nosetup";
    out[n] = NULL;
    *outArgc = n;
    return out;
}

// --- the engine entry point --------------------------------------------------
int app_main(int32_t argc, char const * const * argv)
{
    VBJS_CaptureLaunchDir();   // VibeBuild32: record the launch dir now (before any chdir) for autojs/
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
    char const **launchArgv = gs_buildArgvWithDebugParams(argc, argv, &launchArgc);

    // Parse launcher-owned script arguments before Duke's legacy parser. In
    // particular, the legacy -net option treats later tokens as network data;
    // a VibeBuild option must work in any position instead of being swallowed.
    for (int i = 1; i < launchArgc; ++i)
    {
        const char *inlineValue = NULL;
        if (!gs_option(launchArgv[i], "--js", "-js", NULL, &inlineValue)) continue;
        const char *path = inlineValue;
        if (!path && i + 1 < launchArgc) path = launchArgv[++i];
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

    int fargc = 0;
    char const **fargv = gs_filterArgv(launchArgc, launchArgv, &fargc);

#ifdef WITH_SW
    if (g_activeGame == 1)
        return SW_app_main(fargc, fargv);
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == 2)
        return Blood_app_main(fargc, fargv);
#endif
#ifdef WITH_RR
    if (g_activeGame == 3)
        return RR_app_main(fargc, fargv);
#endif
    return Duke_app_main(fargc, fargv);
}

// --- callbacks both games define: route to the active one --------------------
// These have C linkage in the engine/game headers, so the dispatchers must too.
extern "C" const char *G_DefaultDefFile(void)
{
#ifdef WITH_SW
    if (g_activeGame == 1) return SW_G_DefaultDefFile();
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == 2) return Blood_G_DefaultDefFile();
#endif
#ifdef WITH_RR
    if (g_activeGame == 3) return RR_G_DefaultDefFile();
#endif
    return Duke_G_DefaultDefFile();
}

extern "C" void faketimerhandler(void)
{
#ifdef WITH_SW
    if (g_activeGame == 1) { SW_faketimerhandler(); return; }
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == 2) { Blood_faketimerhandler(); return; }
#endif
#ifdef WITH_RR
    if (g_activeGame == 3) { RR_faketimerhandler(); return; }
#endif
    Duke_faketimerhandler();
}
