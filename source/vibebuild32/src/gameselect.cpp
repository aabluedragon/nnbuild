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
    while (*p)
    {
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        char token[1024]; size_t length = 0; char quote = 0; int escaped = 0;
        while (*p)
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
        if (gs_option(argv[i], "--devtools", "-dt", NULL, NULL)) return 1;
    return 0;
}

static void gs_collectDebugParams(void)
{
    DIR *directory = opendir("autojs");
    if (!directory) return;
    dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;
        char path[BMAX_PATH * 2];
        Bsnprintf(path, sizeof(path), "autojs/%s/index.ts", entry->d_name);
        FILE *ts = fopen(path, "r");
        if (ts) { fclose(ts); gs_collectDebugParamsFromFile(path); continue; }
        Bsnprintf(path, sizeof(path), "autojs/%s/index.js", entry->d_name);
        gs_collectDebugParamsFromFile(path);
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
    int bypass = 0;
    for (int i = 1; i < argc; i++)
    {
        const char *inlineValue = NULL;
        if (gs_option(argv[i], "--game", "-gm", "-game", &inlineValue))
        {
            if (inlineValue) want = inlineValue;
            else if (i + 1 < argc) want = argv[++i];
        }
        else if (gs_option(argv[i], "--no-game-menu", "-ngm", "-nogamemenu", NULL))
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
    for (int i = 0; i < argc; i++)
    {
        const char *inlineValue = NULL;
        // Duke's legacy -l/-v parser only understands attached numbers.
        // Present a single canonical form for every public spelling:
        // -l1, -l 1, --level1, --level 1 (and the volume equivalents).
        char const *compactName = NULL;
        if (gs_compactValueOption(argv[i], "--level", "-l", &inlineValue)) compactName = "-l";
        else if (gs_compactValueOption(argv[i], "--volume", "-v", &inlineValue)) compactName = "-v";
        if (compactName)
        {
            if (!inlineValue || !inlineValue[0])
            {
                if (i + 1 >= argc) { out[n++] = compactName; continue; }
                inlineValue = argv[++i];
            }
            size_t const nameLength = Bstrlen(compactName), valueLength = Bstrlen(inlineValue);
            char *combined = (char *)Xmalloc(nameLength + valueLength + 1);
            Bmemcpy(combined, compactName, nameLength);
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
    out[n] = NULL;
    *outArgc = n;
    return out;
}

// --- the engine entry point --------------------------------------------------
int app_main(int32_t argc, char const * const * argv)
{
    VBJS_CaptureLaunchDir();   // VibeBuild32: record the launch dir now (before any chdir) for autojs/
    if (gs_debugModeRequested(argc, argv)) gs_collectDebugParams();
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
