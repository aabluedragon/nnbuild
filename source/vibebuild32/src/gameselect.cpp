// Game selector: one vibebuild32 binary that can launch either Duke3D
// (vibebuild32) or Shadow Warrior (the VoidSW port), sharing the engine.
//
// The engine's main() calls app_main(); this file provides that app_main as a
// dispatcher. Each game's real entry and the few engine callbacks that both
// games define (see UNIFY_WITH_VIBEBUILD32.md) are renamed Duke_*/SW_*, and the
// canonical names here route to the active game.
//
// Selection (per spec):
//   -game <sw|duke>   pick a game explicitly
//   -nogamemenu       bypass the picker (use -game, else the single available
//                     game, else Duke)
// The GRP is looked for both in the working directory and in a per-game subdir
// (duke3d/DUKE3D.GRP, sw/Sw.grp); the chosen game switches into that folder.
//
// SW is only linked (and dispatched to) when built with WITH_SW; without it this
// is a transparent pass-through to Duke, so the default build is unchanged.

#include "compat.h"
#include "baselayer.h"
#include <stdio.h>

#ifdef _WIN32
# include <direct.h>
# define gs_chdir _chdir
#else
# include <unistd.h>
# define gs_chdir chdir
#endif

// 0 = Duke3D (vibebuild32), 1 = Shadow Warrior, 2 = Blood. Read by the dispatchers.
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

// --- helpers -----------------------------------------------------------------
static int gs_fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
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

// First-cut picker: a console prompt listing whichever games are present.
// (A graphical picker in the startup window is a follow-up.) Returns the game
// index (0=Duke, 1=SW, 2=Blood).
static int gs_menu(int dukeAvail, int swAvail, int bloodAvail)
{
    int opt[3], nopt = 0;
    fprintf(stderr, "\nSelect a game:\n");
    if (dukeAvail)  { opt[nopt] = 0; fprintf(stderr, "  %d) Duke Nukem 3D\n",  nopt + 1); nopt++; }
    if (swAvail)    { opt[nopt] = 1; fprintf(stderr, "  %d) Shadow Warrior\n", nopt + 1); nopt++; }
    if (bloodAvail) { opt[nopt] = 2; fprintf(stderr, "  %d) Blood\n",          nopt + 1); nopt++; }
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
        if (!Bstrcasecmp(argv[i], "-game") && i + 1 < argc) want = argv[i + 1];
        else if (!Bstrcasecmp(argv[i], "-nogamemenu")) bypass = 1;
    }

    char dukeDir[BMAX_PATH] = "", swDir[BMAX_PATH] = "", bloodDir[BMAX_PATH] = "";
    int const dukeAvail  = gs_dukeAvail(dukeDir, sizeof(dukeDir));
    int const swAvail    = gs_swAvail(swDir, sizeof(swDir));
    int const bloodAvail = gs_bloodAvail(bloodDir, sizeof(bloodDir));

    int sel = -1;
    if (want)
    {
        if (!Bstrcasecmp(want, "sw") || !Bstrcasecmp(want, "shadowwarrior")) sel = 1;
        else if (!Bstrcasecmp(want, "blood")) sel = 2;
        else if (!Bstrcasecmp(want, "duke") || !Bstrcasecmp(want, "duke3d")) sel = 0;
    }
    if (sel < 0)
    {
        int const nAvail = (dukeAvail ? 1 : 0) + (swAvail ? 1 : 0) + (bloodAvail ? 1 : 0);
        if (nAvail == 1)
            sel = dukeAvail ? 0 : (swAvail ? 1 : 2);
        else if (nAvail > 1)
            sel = bypass ? (dukeAvail ? 0 : (swAvail ? 1 : 2)) : gs_menu(dukeAvail, swAvail, bloodAvail);
        else
            sel = 0;  // none found; let Duke's normal grp search report it
    }

#ifndef WITH_SW
    if (sel == 1) sel = 0;  // SW not compiled in
#endif
#ifndef WITH_BLOOD
    if (sel == 2) sel = 0;  // Blood not compiled in
#endif
    g_activeGame = sel;

    const char *const name = (sel == 2) ? "Blood" : (sel == 1) ? "Shadow Warrior" : "Duke Nukem 3D";
    const char *dir = (sel == 2) ? bloodDir : (sel == 1) ? swDir : dukeDir;
    if (dir[0] && Bstrcmp(dir, "."))
    {
        if (gs_chdir(dir) == 0)
            LOG_F(INFO, "Game selector: switched into '%s' for %s", dir, name);
    }
    LOG_F(INFO, "Game selector: launching %s", name);
}

// Build a copy of argv with the selector's own options (-game <name>,
// -nogamemenu) removed, so the chosen game's parser never sees them.
static char const **gs_filterArgv(int argc, char const * const * argv, int *outArgc)
{
    char const **out = (char const **)Xmalloc((argc + 1) * sizeof(char *));
    int n = 0;
    for (int i = 0; i < argc; i++)
    {
        if (!Bstrcasecmp(argv[i], "-game")) { i++; continue; }   // skip flag + its value
        if (!Bstrcasecmp(argv[i], "-nogamemenu")) continue;
        out[n++] = argv[i];
    }
    out[n] = NULL;
    *outArgc = n;
    return out;
}

// --- the engine entry point --------------------------------------------------
int app_main(int32_t argc, char const * const * argv)
{
    gs_choose(argc, argv);

    int fargc = 0;
    char const **fargv = gs_filterArgv(argc, argv, &fargc);

#ifdef WITH_SW
    if (g_activeGame == 1)
        return SW_app_main(fargc, fargv);
#endif
#ifdef WITH_BLOOD
    if (g_activeGame == 2)
        return Blood_app_main(fargc, fargv);
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
    Duke_faketimerhandler();
}
