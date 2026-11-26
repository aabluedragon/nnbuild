// Shared Mapster32 variable core helpers used by Duke, RR, and Blood editors.
// Include this file with one M32VARS_SHARED_* selector defined.

#if defined(M32VARS_SHARED_CLEAR_AND_ARRAY)
static void Gv_Clear(void)
{
    // only call this function ONCE...
    int32_t i=(MAXGAMEVARS-1);

    //AddLog("Gv_Clear");

    for (; i>=0; i--)
    {
        DO_FREE_AND_NULL(aGameVars[i].szLabel);

        if (aGameVars[i].dwFlags & GAMEVAR_USER_MASK)
            DO_FREE_AND_NULL(aGameVars[i].val.plValues);

        aGameVars[i].val.lValue = 0;
        aGameVars[i].dwFlags |= GAMEVAR_RESET;

        if (i >= MAXGAMEARRAYS)
            continue;

        gamearray_t *const gar = &aGameArrays[i];

        DO_FREE_AND_NULL(gar->szLabel);

        if (gar->dwFlags & GAMEARRAY_NORMAL)
            DO_FREE_AND_NULL(gar->vals);

        gar->dwFlags |= GAMEARRAY_RESET;
    }

    g_gameVarCount = g_gameArrayCount = 0;

    hash_init(&h_gamevars);
    hash_init(&h_arrays);
}

#define ASSERT_IMPLIES(x, y) Bassert(!(x) || (y))

void Gv_NewArray(const char *pszLabel, void *arrayptr, intptr_t asize, uint32_t dwFlags)
{
    ASSERT_IMPLIES(dwFlags&GAMEARRAY_VARSIZE, dwFlags&GAMEARRAY_READONLY);
    ASSERT_IMPLIES(dwFlags&GAMEARRAY_STRIDE2, dwFlags&GAMEARRAY_READONLY);
    ASSERT_IMPLIES(dwFlags&GAMEARRAY_TYPE_MASK,
                   g_gameArrayCount==0 || (dwFlags&(GAMEARRAY_READONLY|GAMEARRAY_WARN)));

    if (g_gameArrayCount >= MAXGAMEARRAYS)
    {
        C_CUSTOMERROR("too many arrays! (max: %d)", MAXGAMEARRAYS);
        return;
    }

    if (Bstrlen(pszLabel) > (MAXARRAYLABEL-1))
    {
        C_CUSTOMERROR("array name `%s' exceeds limit of %d characters.", pszLabel, MAXARRAYLABEL);
        return;
    }

    const int32_t i = hash_find(&h_arrays, pszLabel);

    if (i>=0 && !(aGameArrays[i].dwFlags & GAMEARRAY_RESET))
    {
        // found it it's a duplicate in error

        if (aGameArrays[i].dwFlags&GAMEARRAY_TYPE_MASK)
            C_CUSTOMWARNING("ignored redefining system array `%s'.", pszLabel);

//        C_ReportError(WARNING_DUPLICATEDEFINITION);
        return;
    }

    if (!(dwFlags&GAMEARRAY_VARSIZE) && !(dwFlags&GAMEARRAY_TYPE_MASK) && (asize<=0 || asize>65536))
    {
        // the dummy array with index 0 sets the size to 0 so that accidental accesses as array
        // will complain.
        C_CUSTOMERROR("invalid array size %d. Must be between 1 and 65536", (int)asize);
        return;
    }

    gamearray_t *const gar = &aGameArrays[g_gameArrayCount];

    if (gar->szLabel == NULL)
        gar->szLabel = (char *)Xcalloc(MAXARRAYLABEL, sizeof(char));
    if (gar->szLabel != pszLabel)
        Bstrcpy(gar->szLabel, pszLabel);

    if (!(dwFlags & GAMEARRAY_TYPE_MASK))
        gar->vals = (int32_t *)Xcalloc(asize, sizeof(int32_t));
    else
        gar->vals = arrayptr;

    gar->size = asize;
    gar->dwFlags = dwFlags & ~GAMEARRAY_RESET;

    hash_add(&h_arrays, gar->szLabel, g_gameArrayCount, 1);
    g_gameArrayCount++;
}

#undef ASSERT_IMPLIES
#undef M32VARS_SHARED_CLEAR_AND_ARRAY
#endif

#if defined(M32VARS_SHARED_NEWVAR)
#ifndef M32VARS_REPORT_REDEFINE_INTERNAL
# error "M32VARS_REPORT_REDEFINE_INTERNAL(pszLabel) must be defined before M32VARS_SHARED_NEWVAR"
#endif

void Gv_NewVar(const char *pszLabel, intptr_t lValue, uint32_t dwFlags)
{
    int32_t i, j;

    //Bsprintf(g_szBuf,"Gv_NewVar(%s, %d, %X)",pszLabel, lValue, dwFlags);
    //AddLog(g_szBuf);

    if (g_gameVarCount >= MAXGAMEVARS)
    {
        C_CUSTOMERROR("too many gamevars! (max: %d)", MAXGAMEVARS);
        return;
    }

    if (Bstrlen(pszLabel) > (MAXVARLABEL-1))
    {
        C_CUSTOMERROR("variable name `%s' exceeds limit of %d characters.", pszLabel, MAXVARLABEL);
        return;
    }

    i = hash_find(&h_gamevars,pszLabel);

    if (i >= 0 && !(aGameVars[i].dwFlags & GAMEVAR_RESET))
    {
        // found it...
        if (aGameVars[i].dwFlags & GAMEVAR_PTR_MASK)
        {
            M32VARS_REPORT_REDEFINE_INTERNAL(pszLabel);
            return;
        }
        else if (!(aGameVars[i].dwFlags & GAMEVAR_SYSTEM))
        {
            // it's a duplicate in error
//            g_numCompilerWarnings++;
//            C_ReportError(WARNING_DUPLICATEDEFINITION);
            return;
        }
    }

    if (i == -1)
        i = g_gameVarCount;

    // Set values
    if ((aGameVars[i].dwFlags & GAMEVAR_SYSTEM) == 0)
    {
        if (aGameVars[i].szLabel == NULL)
            aGameVars[i].szLabel = (char *)Xcalloc(MAXVARLABEL, sizeof(uint8_t));
        if (aGameVars[i].szLabel != pszLabel)
            Bstrcpy(aGameVars[i].szLabel,pszLabel);
        aGameVars[i].dwFlags = dwFlags;

        if (aGameVars[i].dwFlags & GAMEVAR_USER_MASK)
        {
            // only free if not system
            DO_FREE_AND_NULL(aGameVars[i].val.plValues);
        }
    }

    // if existing is system, they only get to change default value....
    aGameVars[i].lDefault = lValue;
    aGameVars[i].dwFlags &= ~GAMEVAR_RESET;

    if (i == g_gameVarCount)
    {
        // we're adding a new one.
        hash_add(&h_gamevars, aGameVars[i].szLabel, g_gameVarCount++, 0);
    }

    if (aGameVars[i].dwFlags & GAMEVAR_PERBLOCK)
    {
        if (!aGameVars[i].val.plValues)
            aGameVars[i].val.plValues = (int32_t *)Xcalloc(1+MAXEVENTS+g_stateCount, sizeof(int32_t));
        for (j=0; j<1+MAXEVENTS+g_stateCount; j++)
            aGameVars[i].val.plValues[j] = lValue;
    }
    else aGameVars[i].val.lValue = lValue;
}

#undef M32VARS_REPORT_REDEFINE_INTERNAL
#undef M32VARS_SHARED_NEWVAR
#endif

#if defined(M32VARS_SHARED_INIT)
void Gv_Init(void)
{
    // only call ONCE

    Gv_Clear();
    Gv_AddSystemVars();
}
#undef M32VARS_SHARED_INIT
#endif

#if defined(M32VARS_SHARED_KEY_ARRAYS)
static uint8_t alphakeys[] =
{
    KEYSC_SPACE,

    KEYSC_A, KEYSC_B, KEYSC_C, KEYSC_D, KEYSC_E, KEYSC_F, KEYSC_G, KEYSC_H,
    KEYSC_I, KEYSC_J, KEYSC_K, KEYSC_L, KEYSC_M, KEYSC_N, KEYSC_O, KEYSC_P,
    KEYSC_Q, KEYSC_R, KEYSC_S, KEYSC_T, KEYSC_U, KEYSC_V, KEYSC_W, KEYSC_X,
    KEYSC_Y, KEYSC_Z,
};

static uint8_t numberkeys[] =
{
    KEYSC_0, KEYSC_1, KEYSC_2, KEYSC_3, KEYSC_4, KEYSC_5, KEYSC_6, KEYSC_7,
    KEYSC_8, KEYSC_9,
};

#undef M32VARS_SHARED_KEY_ARRAYS
#endif

#if defined(M32VARS_SHARED_SYSTEM_ARRAYS)
#ifndef M32VARS_BITMAP_SIZE
# define M32VARS_BITMAP_SIZE(value) bitmap_size(value)
# define M32VARS_UNDEF_BITMAP_SIZE
#endif

    g_systemVarCount = g_gameVarCount;

    // must be first!
    Gv_NewArray(".LOCALS_BASE", NULL, 0, GAMEARRAY_INT32);

    Gv_NewArray("highlight", (void *)highlight, hlcnt_id,
                GAMEARRAY_READONLY|GAMEARRAY_INT16|GAMEARRAY_VARSIZE);
    Gv_NewArray("highlightsector", (void *)highlightsector, hlscnt_id,
                GAMEARRAY_READONLY|GAMEARRAY_INT16|GAMEARRAY_VARSIZE);

    Gv_NewArray("hsect", (void *)headspritesect, MAXSECTORS+1, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("psect", (void *)prevspritesect, MAXSPRITES, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("nsect", (void *)nextspritesect, MAXSPRITES, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("hstat", (void *)headspritestat, MAXSTATUS+1, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("pstat", (void *)prevspritestat, MAXSPRITES, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("nstat", (void *)nextspritestat, MAXSPRITES, GAMEARRAY_READONLY|GAMEARRAY_INT16);
#ifdef YAX_ENABLE
    Gv_NewArray("headsectbunchc", (void *)headsectbunch[0], YAX_MAXBUNCHES, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("nextsectbunchc", (void *)nextsectbunch[0], MAXSECTORS, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("headsectbunchf", (void *)headsectbunch[1], YAX_MAXBUNCHES, GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("nextsectbunchf", (void *)nextsectbunch[1], MAXSECTORS, GAMEARRAY_READONLY|GAMEARRAY_INT16);
#endif
    Gv_NewArray("editorcolors", (void *)editorcolors, 256, GAMEARRAY_READONLY|GAMEARRAY_UINT8);
    Gv_NewArray("tilesizx", (void *)&tilesiz[0].x, MAXTILES, GAMEARRAY_STRIDE2|GAMEARRAY_READONLY|GAMEARRAY_INT16);
    Gv_NewArray("tilesizy", (void *)&tilesiz[0].y, MAXTILES, GAMEARRAY_STRIDE2|GAMEARRAY_READONLY|GAMEARRAY_INT16);
//    Gv_NewArray("picsiz", (void *)picsiz, MAXTILES, GAMEARRAY_READONLY|GAMEARRAY_OFCHAR);
    Gv_NewArray("picanm", (void *)picanm, MAXTILES, GAMEARRAY_READONLY|GAMEARRAY_INT32);

    Gv_NewArray("show2dsector", (void *)show2dsector, M32VARS_BITMAP_SIZE(MAXSECTORS), GAMEARRAY_READONLY|GAMEARRAY_UINT8);
    Gv_NewArray("show2dwall", (void *)show2dwall, M32VARS_BITMAP_SIZE(MAXWALLS), GAMEARRAY_READONLY|GAMEARRAY_UINT8);
    Gv_NewArray("show2dsprite", (void *)show2dsprite, M32VARS_BITMAP_SIZE(MAXSPRITES), GAMEARRAY_READONLY|GAMEARRAY_UINT8);

    Gv_NewArray("keystatus", (void *)keystatus, 256, GAMEARRAY_WARN|GAMEARRAY_UINT8);
    Gv_NewArray("alphakeys", (void *)alphakeys, sizeof(alphakeys), GAMEARRAY_READONLY|GAMEARRAY_UINT8);
    Gv_NewArray("numberkeys", (void *)numberkeys, sizeof(numberkeys), GAMEARRAY_READONLY|GAMEARRAY_UINT8);

    g_systemArrayCount = g_gameArrayCount;

#ifdef M32VARS_UNDEF_BITMAP_SIZE
# undef M32VARS_BITMAP_SIZE
# undef M32VARS_UNDEF_BITMAP_SIZE
#endif
#undef M32VARS_SHARED_SYSTEM_ARRAYS
#endif

#if defined(M32VARS_SHARED_POLYMER_VARS)
#ifdef POLYMER
    Gv_NewVar("pr_overrideparallax",(intptr_t)&pr_overrideparallax, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("pr_parallaxscale",(intptr_t)&pr_parallaxscale, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("pr_parallaxbias",(intptr_t)&pr_parallaxbias, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("pr_overridespecular",(intptr_t)&pr_overridespecular, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("pr_specularpower",(intptr_t)&pr_specularpower, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("pr_specularfactor",(intptr_t)&pr_specularfactor, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
#else
    {
        // dummy Polymer variables for non-Polymer builds
        static int32_t pr_overrideparallax = 0;
        static float pr_parallaxscale = 0.1f;
        static float pr_parallaxbias = 0.0f;
        static int32_t pr_overridespecular = 0;
        static float pr_specularpower = 15.0f;
        static float pr_specularfactor = 1.0f;

        Gv_NewVar("pr_overrideparallax",(intptr_t)&pr_overrideparallax, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
        Gv_NewVar("pr_parallaxscale",(intptr_t)&pr_parallaxscale, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
        Gv_NewVar("pr_parallaxbias",(intptr_t)&pr_parallaxbias, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
        Gv_NewVar("pr_overridespecular",(intptr_t)&pr_overridespecular, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
        Gv_NewVar("pr_specularpower",(intptr_t)&pr_specularpower, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
        Gv_NewVar("pr_specularfactor",(intptr_t)&pr_specularfactor, GAMEVAR_FLOATPTR | GAMEVAR_SYSTEM);
    }
#endif

#undef M32VARS_SHARED_POLYMER_VARS
#endif

#if defined(M32VARS_SHARED_COMMON_SYSTEM_VARS)
    // special vars for struct access
    // MUST be at top and in this order!!!
    Gv_NewVar("sprite", -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("sector", -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("wall", -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("tsprite", -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("light", -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);

    // these too have to be in here and in order!
    // keep in sync with m32script.h: IDs of special vars

    Gv_NewVar("I", 0, GAMEVAR_READONLY | GAMEVAR_SYSTEM);  // THISACTOR
    Gv_NewVar("RETURN", (intptr_t)&g_iReturnVar, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("LOTAG", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("HITAG", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("TEXTURE", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("DOSCRSHOT", (intptr_t)&g_doScreenShot, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);

    Gv_NewVar("xdim",(intptr_t)&xdim, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("ydim",(intptr_t)&ydim, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("windowx1",(intptr_t)&windowxy1.x, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("windowx2",(intptr_t)&windowxy2.x, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("windowy1",(intptr_t)&windowxy1.y, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("windowy2",(intptr_t)&windowxy2.y, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("totalclock",(intptr_t)&totalclock, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);

    Gv_NewVar("viewingrange",(intptr_t)&viewingrange, GAMEVAR_SYSTEM | GAMEVAR_INTPTR | GAMEVAR_READONLY);
    Gv_NewVar("yxaspect",(intptr_t)&yxaspect, GAMEVAR_SYSTEM | GAMEVAR_INTPTR | GAMEVAR_READONLY);

///    Gv_NewVar("framerate",(intptr_t)&g_frameRate, GAMEVAR_SYSTEM | GAMEVAR_INTPTR | GAMEVAR_READONLY);
///    Gv_NewVar("display_mirror",(intptr_t)&display_mirror, GAMEVAR_SYSTEM | GAMEVAR_CHARPTR);

    Gv_NewVar("randomseed",(intptr_t)&randomseed, GAMEVAR_SYSTEM | GAMEVAR_INTPTR);

    Gv_NewVar("numwalls",(intptr_t)&numwalls, GAMEVAR_SYSTEM | GAMEVAR_SHORTPTR | GAMEVAR_READONLY);
    Gv_NewVar("numsectors",(intptr_t)&numsectors, GAMEVAR_SYSTEM | GAMEVAR_SHORTPTR | GAMEVAR_READONLY);
    Gv_NewVar("numsprites",(intptr_t)&Numsprites, GAMEVAR_SYSTEM | GAMEVAR_INTPTR | GAMEVAR_READONLY);
    {
        static int32_t numtiles;
        Gv_NewVar("numtiles",(intptr_t)&numtiles, GAMEVAR_SYSTEM | GAMEVAR_INTPTR | GAMEVAR_READONLY);
    }
#ifdef YAX_ENABLE
    Gv_NewVar("numbunches",(intptr_t)&numyaxbunches, GAMEVAR_SYSTEM | GAMEVAR_INTPTR | GAMEVAR_READONLY);
#endif

#ifdef USE_OPENGL
    Gv_NewVar("rendmode",(intptr_t)&rendmode, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
#endif

    // current position
    Gv_NewVar("posx",(intptr_t)&pos.x, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("posy",(intptr_t)&pos.y, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("posz",(intptr_t)&pos.z, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("ang",(intptr_t)&ang, GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("horiz",(intptr_t)&horiz, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("cursectnum",(intptr_t)&cursectnum, GAMEVAR_READONLY | GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("hardcoded_movement",(intptr_t)&g_doHardcodedMovement, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);

    Gv_NewVar("searchx",(intptr_t)&searchx, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("searchy",(intptr_t)&searchy, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("searchstat",(intptr_t)&searchstat, GAMEVAR_READONLY | GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("searchwall",(intptr_t)&searchwall, GAMEVAR_READONLY | GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("searchsector",(intptr_t)&searchsector, GAMEVAR_READONLY | GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("searchbottomwall",(intptr_t)&searchbottomwall, GAMEVAR_READONLY | GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM);

    Gv_NewVar("pointhighlight",(intptr_t)&pointhighlight, GAMEVAR_SHORTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("linehighlight",(intptr_t)&linehighlight, GAMEVAR_SHORTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);

    hlcnt_id = g_gameVarCount;
    Gv_NewVar("highlightcnt",(intptr_t)&highlightcnt, GAMEVAR_SHORTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    hlscnt_id = g_gameVarCount;
    Gv_NewVar("highlightsectorcnt",(intptr_t)&highlightsectorcnt, GAMEVAR_SHORTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);

#undef M32VARS_SHARED_COMMON_SYSTEM_VARS
#endif

#if defined(M32VARS_SHARED_EDITOR_VIEW_VARS)
    Gv_NewVar("zoom",(intptr_t)&zoom, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("drawlinepat",(intptr_t)&m32_drawlinepat, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("halfxdim16", (intptr_t)&halfxdim16, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("midydim16", (intptr_t)&midydim16, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("ydim16",(intptr_t)&ydim16, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("m32_sideview",(intptr_t)&m32_sideview, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);

    Gv_NewVar("SV1",(intptr_t)&m32_sortvar1, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("SV2",(intptr_t)&m32_sortvar2, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("spritesortcnt",(intptr_t)&spritesortcnt, GAMEVAR_INTPTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY);

#undef M32VARS_SHARED_EDITOR_VIEW_VARS
#endif

#if defined(M32VARS_SHARED_CLIPBOARD_VARS)
    // clipboard contents
    Gv_NewVar("temppicnum",(intptr_t)&temppicnum, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempcstat",(intptr_t)&tempcstat, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("templotag",(intptr_t)&templotag, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("temphitag",(intptr_t)&temphitag, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempextra",(intptr_t)&tempextra, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempshade",(intptr_t)&tempshade, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("temppal",(intptr_t)&temppal, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempvis",(intptr_t)&tempvis, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempxrepeat",(intptr_t)&tempxrepeat, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempyrepeat",(intptr_t)&tempyrepeat, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);

#undef M32VARS_SHARED_CLIPBOARD_VARS
#endif

#if defined(M32VARS_SHARED_START_MOUSE_VARS)
#ifndef M32VARS_STARTPOS_INTPTR_FLAGS
# define M32VARS_STARTPOS_INTPTR_FLAGS (GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM)
#endif
#ifndef M32VARS_STARTPOS_SHORTPTR_FLAGS
# define M32VARS_STARTPOS_SHORTPTR_FLAGS (GAMEVAR_READONLY | GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM)
#endif
    // starting position
    Gv_NewVar("startposx",(intptr_t)&startpos.x, M32VARS_STARTPOS_INTPTR_FLAGS);
    Gv_NewVar("startposy",(intptr_t)&startpos.y, M32VARS_STARTPOS_INTPTR_FLAGS);
    Gv_NewVar("startposz",(intptr_t)&startpos.z, M32VARS_STARTPOS_INTPTR_FLAGS);
    Gv_NewVar("startang",(intptr_t)&startang, M32VARS_STARTPOS_SHORTPTR_FLAGS);
    Gv_NewVar("startsectnum",(intptr_t)&startsectnum, M32VARS_STARTPOS_SHORTPTR_FLAGS);

    Gv_NewVar("mousxplc",(intptr_t)&mousxplc, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
    Gv_NewVar("mousyplc",(intptr_t)&mousyplc, GAMEVAR_READONLY | GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
#ifdef M32VARS_HAS_MOUSEBITS
    Gv_NewVar("mousebits",(intptr_t)&g_mouseBits, GAMEVAR_INTPTR | GAMEVAR_SYSTEM);
#endif

#undef M32VARS_HAS_MOUSEBITS
#undef M32VARS_STARTPOS_SHORTPTR_FLAGS
#undef M32VARS_STARTPOS_INTPTR_FLAGS
#undef M32VARS_SHARED_START_MOUSE_VARS
#endif
