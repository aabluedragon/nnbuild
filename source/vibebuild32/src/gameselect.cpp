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

// 0 = Duke3D (vibebuild32), 1 = Shadow Warrior. Read by the dispatchers below.
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

// First-cut picker: a console prompt. (A graphical picker in the startup window
// is a follow-up; this keeps selection working now.)
static int gs_menu(int dukeAvail, int swAvail)
{
    if (!(dukeAvail && swAvail))
        return swAvail ? 1 : 0;
    LOG_F(INFO, "Select a game:  1) Duke Nukem 3D   2) Shadow Warrior  [1]: ");
    fprintf(stderr, "\nSelect a game:\n  1) Duke Nukem 3D\n  2) Shadow Warrior\nChoice [1]: ");
    fflush(stderr);
    char line[16] = "";
    if (fgets(line, sizeof(line), stdin) && (line[0] == '2'))
        return 1;
    return 0;
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

    char dukeDir[BMAX_PATH] = "", swDir[BMAX_PATH] = "";
    int const dukeAvail = gs_dukeAvail(dukeDir, sizeof(dukeDir));
    int const swAvail   = gs_swAvail(swDir, sizeof(swDir));

    int sel = -1;
    if (want)
    {
        if (!Bstrcasecmp(want, "sw") || !Bstrcasecmp(want, "shadowwarrior")) sel = 1;
        else if (!Bstrcasecmp(want, "duke") || !Bstrcasecmp(want, "duke3d")) sel = 0;
    }
    if (sel < 0)
    {
        if (dukeAvail && !swAvail) sel = 0;
        else if (swAvail && !dukeAvail) sel = 1;
        else if (dukeAvail && swAvail) sel = bypass ? 0 : gs_menu(dukeAvail, swAvail);
        else sel = 0;  // neither found; let Duke's normal grp search report it
    }

#ifndef WITH_SW
    sel = 0;  // SW not compiled in
#endif
    g_activeGame = sel;

    const char *dir = (sel == 1) ? swDir : dukeDir;
    if (dir[0] && Bstrcmp(dir, "."))
    {
        if (gs_chdir(dir) == 0)
            LOG_F(INFO, "Game selector: switched into '%s' for %s", dir, sel == 1 ? "Shadow Warrior" : "Duke Nukem 3D");
    }
    LOG_F(INFO, "Game selector: launching %s", sel == 1 ? "Shadow Warrior" : "Duke Nukem 3D");
}

// --- the engine entry point --------------------------------------------------
int app_main(int32_t argc, char const * const * argv)
{
    gs_choose(argc, argv);
#ifdef WITH_SW
    if (g_activeGame == 1)
        return SW_app_main(argc, argv);
#endif
    return Duke_app_main(argc, argv);
}

// --- callbacks both games define: route to the active one --------------------
// These have C linkage in the engine/game headers, so the dispatchers must too.
extern "C" const char *G_DefaultDefFile(void)
{
#ifdef WITH_SW
    if (g_activeGame == 1) return SW_G_DefaultDefFile();
#endif
    return Duke_G_DefaultDefFile();
}

extern "C" void faketimerhandler(void)
{
#ifdef WITH_SW
    if (g_activeGame == 1) { SW_faketimerhandler(); return; }
#endif
    Duke_faketimerhandler();
}
