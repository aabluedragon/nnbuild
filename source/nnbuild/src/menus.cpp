//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers

This file is part of EDuke32

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------

#include "duke3d.h"
#include "masterserver.h"
#include "mapeditor.h"
#include "mouse.h"
#include "osd.h"
#include "osdcmds.h"
#include "config.h"
#include "nnbjs.h"
#include "communityapi.h"
#include <sys/stat.h>

extern char inputloc;
extern int recfilep;
//extern char vgacompatible;
short g_skillSoundID=-1;
int probey=0;
static int musicdevice = 0;
static int lastsavehead=0,last_menu_pos=0,last_menu,sh,onbar,buttonstat;
static int last_zero,last_fifty,last_onehundred,last_twoohtwo,last_threehundred = 0;

enum
{
    FURY_TOP_MENU_X = 40,
    FURY_TOP_MENU_Y = 148,
    FURY_TOP_MENU_STEP = 11,
    FURY_SKILL_MENU_X = 160,
    FURY_CENTER_MENU_X = 160,
};

enum
{
    MENU_NULL = INT32_MIN,
    MENU_CLOSE = -2,
    MENU_PREVIOUS = -1,
    MENU_MAIN = 0,
    MENU_MAIN_INGAME = 50,
    MENU_EPISODE = 100,
    MENU_USERMAP = 101,
    MENU_NEWGAMECUSTOM = 102,
    MENU_NEWGAMECUSTOMSUB = 103,
    MENU_NEWGAMECUSTOML3 = 104,
    MENU_USERMAP_BROWSER = 105,
    MENU_SKILL = 110,
    MENU_GAMESETUP = 200,
    MENU_OPTIONS = 202,
    MENU_VIDEOSETUP = 203,
    MENU_KEYBOARDSETUP = 204,
    MENU_MOUSESETUP = 205,
    MENU_JOYSTICKSETUP = 206,
    MENU_JOYSTICKBTNS = 207,
    MENU_JOYSTICKAXES = 208,
    MENU_KEYBOARDKEYS = 209,
    MENU_MOUSEBTNS = 210,
    MENU_MOUSEADVANCED = 212,
    MENU_JOYSTICKAXIS = 213,
    MENU_CONTROLS = 220,
    MENU_RENDERERSETUP = 230,
    MENU_COLCORR = 231,
    MENU_COLCORR_INGAME = 232,
    MENU_SCREENSETUP = 233,
    MENU_DISPLAYSETUP = 234,
    MENU_LOAD = 300,
    MENU_SAVE = 350,
    MENU_STORY = 400,
    MENU_F1HELP = 401,
    MENU_QUIT = 500,
    MENU_QUITTOTITLE = 501,
    MENU_QUIT_INGAME = 502,
    MENU_NETSETUP = 600,
    MENU_NETWAITMASTER = 601,
    MENU_NETWAITVOTES = 603,
    MENU_SOUND = 700,
    MENU_SOUND_INGAME = 701,
    MENU_ADVSOUND = 702,
    MENU_CREDITS = 990,
    MENU_CREDITS2 = 991,
    MENU_CREDITS3 = 992,
    MENU_CREDITS4 = 993,
    MENU_CREDITS5 = 994,
    MENU_LOADVERIFY = 1000,
    MENU_NEWVERIFY = 1500,
    MENU_SAVEVERIFY = 2000,
    MENU_ADDONS = 3000,
    MENU_ADDONSVERIFY = 3001,
    MENU_ADULTPASSWORD = 10001,
    MENU_RESETPLAYER = 15000,
    MENU_RESETSTATSVERIFY = 15002,
    MENU_BUYDUKE = 20000,
    MENU_NETWORK = 20001,
    MENU_PLAYER = 20002,
    MENU_MACROS = 20004,
    MENU_NETHOST = 20010,
    MENU_NETOPTIONS = 20011,
    MENU_NETUSERMAP = 20012,
    MENU_NETJOIN = 20020,
};

static int last_skill_parent_menu = MENU_EPISODE;
static vec2_t g_menuScriptOrigin;

static char menunamecnt;

static BUILDVFS_FIND_REC *finddirs=NULL, *findfiles=NULL, *finddirshigh=NULL, *findfileshigh=NULL;
static int numdirs=0, numfiles=0;
static int currentlist=0;

static int function, whichkey;
static int changesmade, newvidmode, curvidmode, newfullscreen;
static int vidsets[16] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
static int curvidset, newvidset = 0;
static int soundbits, soundvoices, soundrate;
#undef MAXMOUSEBUTTONS
#define MAXMOUSEBUTTONS 6 // FIXME: menu needs an update for extra buttons
static const char *mousebuttonnames[] = { "Mouse1", "Mouse2", "Mouse3", "Mouse4", "Wheel Up", "Wheel Down", "Mouse5", "Mouse6", "Mouse7", "Mouse8"};

#define USERMAPENTRYLENGTH 25

static inline int32_t Menu_ScriptOriginPixelX(void) { return g_menuScriptOrigin.x >> 16; }
static inline int32_t Menu_ScriptOriginPixelY(void) { return g_menuScriptOrigin.y >> 16; }
static inline int32_t Menu_ApplyScriptOriginX(int32_t const x) { return x + Menu_ScriptOriginPixelX(); }
static inline int32_t Menu_ApplyScriptOriginY(int32_t const y) { return y + Menu_ScriptOriginPixelY(); }
static inline int32_t Menu_ApplyScriptOriginX16(int32_t const x) { return x + g_menuScriptOrigin.x; }
static inline int32_t Menu_ApplyScriptOriginY16(int32_t const y) { return y + g_menuScriptOrigin.y; }

static constexpr int32_t FURY_BLUE_ZOOM = 16384;
static constexpr int32_t FURY_BLUE_SPACE = 5 << 16;
static constexpr int32_t FURY_BLUE_YLINE = 7 << 16;

static int32_t Menu_GameText(int32_t const x, int32_t const y, char const *text,
                             int32_t const shade, int32_t const pal, int32_t const bits)
{
    if (!FURY)
        return G_PrintGameText(2, STARTALPHANUM, Menu_ApplyScriptOriginX(x), Menu_ApplyScriptOriginY(y), text,
                               shade, pal, bits, 0, 0, xdim - 1, ydim - 1, 65536);

    if (text == nullptr)
        return x;

    text = NNBJS_MenuText(text);

    int32_t const zoom = ud.gametext_zoom > 0 ? ud.gametext_zoom : FURY_BLUE_ZOOM;
    int32_t const xspace = ud.gametext_xspace != 0 ? ud.gametext_xspace : FURY_BLUE_SPACE;
    int32_t const xbetween = ud.gametext_tracking;
    int32_t const textFlags = TEXT_VARHEIGHT | ((x == (320 >> 1)) ? TEXT_XCENTER : 0);
    int32_t const orientation = (bits & ~RS_TOPLEFT) | RS_AUTO | RS_NOCLIP | ROTATESPRITE_FULL16;

    vec2_t const dim = G_ScreenText(STARTALPHANUM,
                                    Menu_ApplyScriptOriginX16(x << 16),
                                    Menu_ApplyScriptOriginY16(y << 16),
                                    zoom, 0, 0, text, shade, pal, orientation, 0,
                                    xspace, FURY_BLUE_YLINE, xbetween, 0, textFlags,
                                    0, 0, xdim - 1, ydim - 1);

    return x + (dim.x >> 16);
}

#define mgametext(x,y,t,s,dabits) Menu_GameText((x),(y),(t),(s),0,(dabits))
#define mgametextpal(x,y,t,s,p) Menu_GameText((x),(y),(t),(s),(p),26)

// BUILD SHIT THAT NEEDS TO BE EXTERNED:
extern int32_t r_yshearing;
extern int32_t r_shadeinterpolate;

static int Menu_DefinedSkillCount(void);
static int Menu_FirstDefinedSkill(void);

static char const* MEOSN_SOUND_MIDIDRIVER[] = {
    "OPL3 emu.",
#ifdef _WIN32
    "Windows MME",
#endif
#ifdef __linux__
    "ALSA MIDI",
#endif
    ".sf2 synth",
};

static int32_t MEOSV_SOUND_MIDIDRIVER[] = {
    ASS_OPL3,
#ifdef _WIN32
    ASS_WinMM,
#endif
#ifdef __linux__
    ASS_ALSA,
#endif
    ASS_SF2,
};

static void creditscminitext(int32_t x, int32_t y, const char *t)
{
   minitext((x+1) - (Bstrlen(t) << 1), y+1, t, 4, 10 + 16 + 128);
   minitext(x - (Bstrlen(t) << 1), y, t, 8, 10 + 16 + 128);
}

void ChangeToMenu(int cm)
{
    int const previousMenu = g_currentMenu;

    cm = X_OnEventWithReturn(EVENT_CHANGEMENU, g_player[screenpeek].ps->i, screenpeek, cm);
    if (cm == -1)
        cm = previousMenu;
    else if (cm == -2)
    {
        ud.gm &= ~MODE_MENU;
        cm = 0;
    }

    cm = NNBJS_MenuRoute(g_currentMenu, probey, cm);
    if (cm == -1)
        cm = previousMenu;
    else if (cm == -2)
    {
        ud.gm &= ~MODE_MENU;
        cm = 0;
    }

    g_currentMenu = cm;
    if (previousMenu != cm)
        NNBJS_FireMenu("menuChanged", cm, previousMenu);

    if ((cm >= 1000 && cm <= 1009))
        return;

    if (cm == 0)
        probey = last_zero;
    else if (cm == 50)
        probey = last_fifty;
    else if (cm == 100)
        probey = last_onehundred;
    else if (cm == 202)
        probey = last_twoohtwo;
    else if (cm >= 300 && cm < 400)
        probey = last_threehundred;
    else if (cm == 110)
        probey = FURY ? clamp(ud.m_player_skill - 1, Menu_FirstDefinedSkill(), max<int>(Menu_DefinedSkillCount() - 1, 0)) : 1;
    else probey = 0;
    lastsavehead = -1;
}

#if 0
void savetemp(char *fn,int daptr,int dasiz)
{
    FILE *fp;

    if ((fp = fopen(fn,"wb")) == (FILE *)NULL)
        return;

    fwrite((char *)daptr,dasiz,1,fp);

    fclose(fp);
}
#endif

#define LMB (buttonstat&1)
#define RMB (buttonstat&2)
#define WHEELUP (buttonstat&16)
#define WHEELDOWN (buttonstat&32)

static ControlInfo minfo;

static short mi, mii;

static void Menu_BlackRectangle(int32_t x, int32_t y, int32_t width, int32_t height, int32_t orientation)
{
    const int32_t xscale = divscale16(width, tilesiz[0].x << 16), yscale = divscale16(height, tilesiz[0].y << 16);

    rotatesprite_(x, y, max(xscale, yscale), 0, 0, 127, ud.shadow_pal, (orientation & (1 | 32)) | 2 | 8 | 16, 0, 0, get_xdim_16(x), get_ydim_16(y), get_xdim_16(x + width), get_ydim_16(y + height));
}

static inline int Menu_BlackTranslucentBackgroundOK(int cm)
{
    switch (cm)
    {
    case MENU_COLCORR:
    case MENU_COLCORR_INGAME:
        return 0;
    default:
        return 1;
    }
}

static inline int Menu_IsVerifyPrompt(int cm)
{
    return cm == MENU_NEWVERIFY
        || cm == MENU_RESETSTATSVERIFY
        || (cm >= MENU_LOADVERIFY && cm < MENU_LOADVERIFY + 10)
        || (cm >= MENU_SAVEVERIFY && cm < MENU_SAVEVERIFY + 10);
}

static inline int Menu_UpdateScreenOK(int cm)
{
    if (cm == MENU_NEWVERIFY)
        return 0;
    if (cm == MENU_RESETSTATSVERIFY)
        return 0;
    if (cm == MENU_LOAD || cm == MENU_SAVE)
        return 0;
    if (cm >= MENU_LOADVERIFY && cm < MENU_LOADVERIFY + 10)
        return 0;
    if (cm >= MENU_SAVEVERIFY && cm < MENU_SAVEVERIFY + 10)
        return 0;

    return 1;
}

static void Menu_ReadProbeInput(void)
{
    CONTROL_GetInput(&minfo);
    mi += (minfo.mousey + minfo.dz);
    mii += minfo.mousex;
}

static void Menu_UpdateProbeSelection(int numItems)
{
    if (numItems <= 0)
    {
        probey = 0;
        return;
    }

    if (!buttonstat || buttonstat == 16 || buttonstat == 32)
    {
        if (KB_KeyPressed(sc_UpArrow) || KB_KeyPressed(sc_PgUp) || KB_KeyPressed(sc_kpad_8) || mi < -8192 || WHEELUP)
        {
            mi = mii = 0;
            KB_ClearKeyDown(sc_UpArrow);
            KB_ClearKeyDown(sc_kpad_8);
            KB_ClearKeyDown(sc_PgUp);
            MOUSE_ClearButton(M_WHEELUP);
            S_PlaySound(KICK_HIT);

            probey--;
            if (probey < 0) probey = numItems-1;
        }
        if (KB_KeyPressed(sc_DownArrow) || KB_KeyPressed(sc_PgDn) || KB_KeyPressed(sc_kpad_2) || mi > 8192 || WHEELDOWN)
        {
            mi = mii = 0;
            KB_ClearKeyDown(sc_DownArrow);
            KB_ClearKeyDown(sc_kpad_2);
            KB_ClearKeyDown(sc_PgDn);
            MOUSE_ClearButton(M_WHEELDOWN);
            S_PlaySound(KICK_HIT);
            probey++;
        }
    }

    if (probey >= numItems)
        probey = 0;
}

static int probe_(int type,int x,int y,int i,int numItems, bool processSelection = true)
{
    short centre;

    if (processSelection)
    {
        Menu_ReadProbeInput();
        Menu_UpdateProbeSelection(numItems);
    }

    if (x == (320>>1))
        centre = 320>>2;
    else centre = 0;

    if (FURY)
        goto skipcursor;

    if (centre)
    {
        //        rotatesprite(((320>>1)+(centre)+54)<<16,(y+(probey*i)-4)<<16,65536L,0,SPINNINGNUKEICON+6-((6+((int32_t)totalclock>>3))%7),sh,0,10,0,0,xdim-1,ydim-1);
        //        rotatesprite(((320>>1)-(centre)-54)<<16,(y+(probey*i)-4)<<16,65536L,0,SPINNINGNUKEICON+(((int32_t)totalclock>>3)%7),sh,0,10,0,0,xdim-1,ydim-1);

        rotatesprite(((320>>1)+(centre>>1)+70)<<16,(y+(probey*i)-4)<<16,65536L>>type,0,SPINNINGNUKEICON+6-((6+((int32_t)totalclock>>3))%7),sh,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(((320>>1)-(centre>>1)-70)<<16,(y+(probey*i)-4)<<16,65536L>>type,0,SPINNINGNUKEICON+(((int32_t)totalclock>>3)%7),sh,0,10,0,0,xdim-1,ydim-1);
    }
    else
        rotatesprite((x<<16)-((tilesiz[SPINNINGNUKEICON].x-4)<<(16-type)),(y+(probey*i)-(4>>type))<<16,65536L>>type,0,SPINNINGNUKEICON+((((int32_t)totalclock>>3))%7),sh,0,10,0,0,xdim-1,ydim-1);

skipcursor:
    if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Enter) || (LMB && !onbar))
    {
        if (g_currentMenu != 110)
            S_PlaySound(PISTOL_BODYHIT);
        KB_ClearKeyDown(sc_Enter);
        KB_ClearKeyDown(sc_Space);
        KB_ClearKeyDown(sc_kpad_Enter);
        MOUSE_ClearButton(M_LEFTBUTTON);
        return(probey);
    }
    else if (KB_KeyPressed(sc_Escape) || (RMB))
    {
        onbar = 0;
        KB_ClearKeyDown(sc_Escape);
        S_PlaySound(EXITMENUSOUND);
        MOUSE_ClearButton(M_RIGHTBUTTON);
        return(-1);
    }
    else
    {
        if (onbar == 0) return(-probey-2);
        if (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_kpad_4) || ((buttonstat&1) && (WHEELUP || mii < -256)))
            return(probey);
        else if (KB_KeyPressed(sc_RightArrow) || KB_KeyPressed(sc_kpad_6) || ((buttonstat&1) && (WHEELDOWN || mii > 256)))
            return(probey);
        return(-probey-2);
    }
}
static inline int M_Probe(int x,int y,int i,int numItems)
{
    return probe_(0,x,y,i, numItems);
}

static inline int M_ProbeAlreadyProcessed(int x,int y,int i,int numItems)
{
    return probe_(0,x,y,i, numItems, false);
}

static inline int probesm(int x,int y,int i,int numItems)
{
    return probe_(1,x,y,i, numItems);
}

#define UNSELMENUSHADE (ud.menu_shadedeselected != 0 ? ud.menu_shadedeselected : 10)
#define DISABLEDMENUSHADE (ud.menu_shadedisabled != 0 ? ud.menu_shadedisabled : 20)
#define MENUHIGHLIGHT(x) (probey == (x) ? (sintable[((int32_t)totalclock << 5) & 2047] >> 12) : UNSELMENUSHADE)

int menutext_(int x,int y,int s,int p,const char *t,int bits, bool centerAtMenuMidpoint)
{
    if (t == nullptr)
        return x;

    t = NNBJS_MenuText(t);

    if (p == 0)
    {
        if (s == DISABLEDMENUSHADE)
            p = ud.menutext_paldisabled;
        else if (s == UNSELMENUSHADE)
            p = ud.menutext_paldeselected;
        else
            p = ud.menutext_palselected;
    }

    int32_t const textFlags = TEXT_BIGALPHANUM | TEXT_UPPERCASE | TEXT_LITERALESCAPE
                            | (centerAtMenuMidpoint && x == (320 >> 1) ? TEXT_XCENTER : 0);
    int32_t const orientation = bits | ROTATESPRITE_FULL16;
    int32_t const zoom = ud.menutext_zoom > 0 ? ud.menutext_zoom : 65536;
    int32_t const xspace = ud.menutext_xspace != 0 ? ud.menutext_xspace : (5 << 16);
    int32_t const yline = (FURY ? 10 : 15) << 16;
    int32_t const xbetween = ud.menutext_tracking;
    vec2_t const dim = G_ScreenText(BIGALPHANUM,
                                    Menu_ApplyScriptOriginX16(x << 16),
                                    Menu_ApplyScriptOriginY16((y << 16) - ((FURY ? 0 : 12) << 16)),
                                    zoom, 0, 0, t, s, p, orientation, 0,
                                    xspace, yline, xbetween, 0, textFlags,
                                    0, 0, xdim - 1, ydim - 1);

    return x + (dim.x >> 16);
}

int menutext_(int x,int y,int s,int p,const char *t,int bits)
{
    return menutext_(x, y, s, p, t, bits, true);
}

static int Menu_DrawLeftMenuText(int x,int y,int s,int p,const char *t)
{
    return menutext_(x, y, s, p, OSD_StripColors(menutextbuf, t), 10+16, false);
}

static void Menu_DrawTopBarTitle(int const y, char const * const title, int const titleY = -1)
{
    rotatesprite(320 << 15, y << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
    menutext(320 >> 1, FURY ? max(0, y - 16) : (titleY >= 0 ? titleY : y + 5), 0, 0, title);
}

static inline bool Menu_IsRenderableTile(int const tile)
{
    return (unsigned)tile < MAXTILES && tilesiz[tile].x > 0 && tilesiz[tile].y > 0;
}

static int Menu_FirstRenderableAnimationTile(int const baseTile, int const startFrame, int const frameCount,
                                             int const viewType, int const preferredFrame)
{
    int const frames = max(frameCount, 1);
    int const stride = max(viewType, 1);

    for (int i = 0; i < frames; ++i)
    {
        int const frame = (preferredFrame + i) % frames;
        int const tile = baseTile + startFrame + stride * frame;
        if (Menu_IsRenderableTile(tile))
            return tile;
    }

    if (Menu_IsRenderableTile(baseTile + startFrame))
        return baseTile + startFrame;
    if (Menu_IsRenderableTile(baseTile))
        return baseTile;
    if (Menu_IsRenderableTile(1441))
        return 1441;
    return Menu_IsRenderableTile(APLAYER) ? APLAYER : -1;
}

static int Menu_PlayerPreviewTile(void)
{
    int const previewFrame = ((4 - ((int32_t)totalclock >> 4)) & 3);

    if (P_ValidateSkin(ud.skin) && P_SkinHasEnemySpriteArt(ud.skin))
    {
        int skinStart, skinFrames, skinViewType;
        int const skinBase = P_SkinBaseTile(ud.skin);

        P_GetSkinFrame(ud.skin, 20, 4, &skinStart, &skinFrames, &skinViewType);
        return Menu_FirstRenderableAnimationTile(skinBase, skinStart, skinFrames, skinViewType,
                                                 previewFrame % max(skinFrames, 1));
    }

    int const dukeTile = 1441 - (previewFrame * 5);
    if (Menu_IsRenderableTile(dukeTile))
        return dukeTile;
    if (Menu_IsRenderableTile(1441))
        return 1441;
    return Menu_IsRenderableTile(APLAYER) ? APLAYER : -1;
}

static void Menu_DrawPlayerPreview(int const pal)
{
    int const tile = Menu_PlayerPreviewTile();
    if (tile < 0)
        return;

    int const anchorTile = Menu_IsRenderableTile(APLAYER) ? APLAYER : tile;
    rotatesprite(260 << 16, (24 + (tilesiz[anchorTile].y >> 1)) << 16, 49152L, 0, tile,
                 0, pal, 10, 0, 0, xdim - 1, ydim - 1);
}

static void Menu_FurySmallText(int const x, int const y, char const* const text, int const shade, int const pal, int const flags = 0)
{
    G_ScreenText(STARTALPHANUM, Menu_ApplyScriptOriginX(x), Menu_ApplyScriptOriginY(y), FURY_BLUE_ZOOM, 0, 0, text, shade, pal,
                 RS_AUTO | RS_NOCLIP, 0, FURY_BLUE_SPACE >> 16, FURY_BLUE_YLINE >> 16, 0, 0,
                 TEXT_VARHEIGHT | TEXT_UPPERCASE | flags, 0, 0, xdim, ydim);
}

static void Menu_FuryBackdrop(void)
{
    // Pending 2D menu sprites from the previous frame can otherwise survive
    // over the Fury modal after the rooms are redrawn.
    renderFlushPerms();
    videoClearViewableArea(-1);
    videoClearScreen(0);

    if (ud.gm & MODE_GAME)
    {
        // Verify prompts are displayed by native code after the old menu loop
        // may have just switched state. Make sure the gameplay viewport is not
        // inherited from a previous save/load/options menu before repainting.
        G_UpdateScreenArea();

        // The legacy menu loop can leave the previous native menu frame in the
        // active backbuffer. Redraw the gameplay view before fading so Fury's
        // modal prompts do not inherit save/load rows or other stale menu text.
        G_DrawRooms(screenpeek, 65536);
        if (videoGetRenderMode() >= 3)
            G_DrawBackground();
    }
    else
    {
        videoClearViewableArea(-1);
    }

    videoFadeToBlack(1);
}

static void Menu_FuryPromptText(int32_t const x, int32_t const y, char const * const text, int32_t const flags = TEXT_XCENTER)
{
    int32_t const zoom = ud.gametext_zoom > 0 ? ud.gametext_zoom : FURY_BLUE_ZOOM;
    int32_t const xspace = ud.gametext_xspace != 0 ? ud.gametext_xspace : FURY_BLUE_SPACE;
    int32_t const xbetween = ud.gametext_tracking;
    int32_t const pal = ud.gametext_pal != 0 ? ud.gametext_pal : 11;

    G_ScreenText(STARTALPHANUM, Menu_ApplyScriptOriginX16(x), Menu_ApplyScriptOriginY16(y), zoom,
                 0, 0, text, 0, pal, RS_AUTO | RS_NOCLIP | ROTATESPRITE_FULL16, 0,
                 xspace, 0, xbetween, 0, TEXT_VARHEIGHT | flags, 0, 0, xdim - 1, ydim - 1);
}

static int32_t Menu_FuryPromptLineStep(void)
{
    return FURY_BLUE_YLINE;
}

static void Menu_FuryVerifyPrompt(char const * const line1, char const * const line2 = nullptr, char const * const input = "(Y/N)")
{
    // EVENT_DISPLAYMENU returns a script origin for animated/scripted menus.
    // Native verify prompts are not part of that scripted layout; carrying the
    // previous origin offsets the prompt and makes it line up with stale rows.
    g_menuScriptOrigin = {};

    Menu_FuryBackdrop();

    char const * const lines[] = { line1, line2, input };
    int32_t const numLines = (line1 != nullptr) + (line2 != nullptr) + (input != nullptr);
    int32_t const lineStep = Menu_FuryPromptLineStep();
    int32_t y = 90 << 16;

    int32_t const backingTop = y - (12 << 16);
    int32_t const backingHeight = ((numLines - 1) * lineStep) + (34 << 16);
    Menu_BlackRectangle(24 << 16, backingTop, 272 << 16, backingHeight, RS_TRANS1 | RS_TRANS2);

    for (char const * const line : lines)
    {
        if (line == nullptr)
            continue;

        Menu_FuryPromptText(160 << 16, y, line);
        y += lineStep;
    }
}

static void Menu_FuryLateVerifyPrompt(void)
{
    if (!FURY)
        return;

    if (g_currentMenu == MENU_NEWVERIFY)
    {
        Menu_FuryVerifyPrompt("Abort this game?");
        return;
    }

    if (g_currentMenu >= MENU_LOADVERIFY && g_currentMenu < MENU_LOADVERIFY + 10)
    {
        Bsprintf(tempbuf, "\"%.18s\"", ud.savegame[g_currentMenu - MENU_LOADVERIFY]);
        Menu_FuryVerifyPrompt("Load game:", tempbuf);
        return;
    }

    if (g_currentMenu >= MENU_SAVEVERIFY && g_currentMenu < MENU_SAVEVERIFY + 10)
    {
        Menu_FuryVerifyPrompt("Overwrite saved game?");
        return;
    }

    if (g_currentMenu == MENU_RESETSTATSVERIFY)
        Menu_FuryVerifyPrompt("Reset stats/achievements?");
}

static int Menu_CapirResetAvailable(void)
{
    return communityapiEnabled() || VM_HaveEvent(EVENT_CAPIR);
}

static void Menu_ResetStatsAndDispatchCapir(void)
{
    if (communityapiEnabled())
        communityapiResetStats();

    if (VM_HaveEvent(EVENT_CAPIR))
        X_OnEvent(EVENT_CAPIR, g_player[screenpeek].ps->i, screenpeek, -1);
}

static void Menu_FuryControlText(int const x, int const y, char const* const text, int const shade, int const pal, int const flags = 0)
{
    if (FURY)
        Menu_FurySmallText(x, y, text, shade, pal, flags);
    else
        minitextshade(x, y, text, shade, pal, 10 + 16);
}

static int Menu_KeybindFuncCount(void)
{
    if (keybind_order_custom[0] < 0)
        return NUMGAMEFUNCTIONS;

    int count = 0;

    while (count < NUMGAMEFUNCTIONS && keybind_order_custom[count] >= 0)
        ++count;

    return count;
}

static int Menu_KeybindFuncAt(int const visibleIndex)
{
    if (visibleIndex < 0)
        return -1;

    if (keybind_order_custom[0] >= 0)
        return (visibleIndex < NUMGAMEFUNCTIONS) ? keybind_order_custom[visibleIndex] : -1;

    return (visibleIndex < NUMGAMEFUNCTIONS) ? visibleIndex : -1;
}

static int Menu_VisibleIndexForFunc(int const gameFunc)
{
    if ((unsigned)gameFunc >= NUMGAMEFUNCTIONS)
        return 0;

    if (keybind_order_custom[0] >= 0)
    {
        for (int i = 0; i < NUMGAMEFUNCTIONS && keybind_order_custom[i] >= 0; ++i)
            if (keybind_order_custom[i] == gameFunc)
                return i;

        return 0;
    }

    return gameFunc;
}

static inline int Menu_FurySliderPal(int const damodify, int const pa)
{
    if (pa)
        return ud.slidebar_paldisabled != 0 ? ud.slidebar_paldisabled : pa;

    return damodify && ud.slidebar_palselected != 0 ? ud.slidebar_palselected : 0;
}

static bool Menu_DrawFurySlider(int const type, int const x, int const y, int const value, int const minValue, int const maxValue,
                                int const damodify, int const shade, int const pal, int const reverse)
{
    if (!FURY)
        return false;

    int32_t const slidebarz = ud.menu_slidebarz > 0 ? ud.menu_slidebarz : (65536L >> type);
    int32_t const slidecursorz = ud.menu_slidecursorz > 0 ? ud.menu_slidecursorz : slidebarz;

    int32_t const slidebarwidth = tilesiz[SLIDEBAR].x * slidebarz;
    int32_t const slidebarheight = tilesiz[SLIDEBAR].y * slidebarz;
    int32_t const slidecursorwidth = tilesiz[SLIDEBAR + 1].x * slidecursorz;
    int32_t const slidecursorheight = tilesiz[SLIDEBAR + 1].y * slidecursorz;

    if (slidebarwidth <= 0 || slidebarheight <= 0 || slidecursorwidth <= 0 || slidecursorheight <= 0)
        return false;

    int const range = maxValue - minValue;
    int32_t const slidebarmargin = ud.menu_slidebarmargin > 0 ? ud.menu_slidebarmargin : 0;
    int32_t const sx = x << 16;
    int32_t const sy = (y << 16) - (slidebarheight >> 1);
    int32_t const rawslideregionwidth = slidebarwidth - (slidebarmargin << 1) - slidecursorwidth;
    int32_t const slideregionwidth = rawslideregionwidth > 0 ? rawslideregionwidth : 1;
    int const clampedValue = clamp(value - minValue, 0, range > 0 ? range : 0);
    int32_t const slidepointoffset = scale(slideregionwidth, reverse ? (range - clampedValue) : clampedValue, range > 0 ? range : 1);
    int32_t const slidepointx = sx + slidebarmargin + slidepointoffset;
    int32_t const slidepointy = sy + (slidebarheight >> 1) - (slidecursorheight >> 1);

    rotatesprite(sx, sy, slidebarz, 0, SLIDEBAR, shade, Menu_FurySliderPal(damodify, pal), 2 | 8 | 16, 0, 0, xdim - 1, ydim - 1);
    rotatesprite(slidepointx, slidepointy, slidecursorz, 0, SLIDEBAR + 1, shade, Menu_FurySliderPal(damodify, pal), 2 | 8 | 16, 0, 0, xdim - 1, ydim - 1);

    return true;
}

static void _bar(int type, int x,int y,int *p,int dainc,int damodify,int s, int pa, int min, int max)
{
    int xloc;
    char rev;

    if (dainc < 0)
    {
        dainc = -dainc;
        rev = 1;
    }
    else rev = 0;
    y-=2;

    if (damodify)
    {
        if (*p >= min && *p <= max && (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_kpad_4) || ((buttonstat&1) && (WHEELUP || mii < -256))))         // && onbar) )
        {
            KB_ClearKeyDown(sc_LeftArrow);
            KB_ClearKeyDown(sc_kpad_4);
            MOUSE_ClearButton(M_WHEELUP);
            mii = 0;
            if (!rev)
                *p -= dainc;
            else *p += dainc;
            if (*p < min)
                *p = min;
            if (*p > max)
                *p = max;
            S_PlaySound(KICK_HIT);
        }
        if (*p <= max && *p >= min && (KB_KeyPressed(sc_RightArrow) || KB_KeyPressed(sc_kpad_6) || ((buttonstat&1) && (WHEELDOWN || mii > 256))))        //&& onbar) )
        {
            KB_ClearKeyDown(sc_RightArrow);
            KB_ClearKeyDown(sc_kpad_6);
            MOUSE_ClearButton(M_WHEELDOWN);
            mii = 0;
            if (!rev)
                *p += dainc;
            else *p -= dainc;
            if (*p > max)
                *p = max;
            if (*p < min)
                *p = min;
            S_PlaySound(KICK_HIT);
        }
    }

    xloc = *p;
    if (Menu_DrawFurySlider(type, x, y, xloc, min, max, damodify, s, pa, rev))
        return;

    rotatesprite((x<<16)+(22<<(16-type)),(y<<16)-(3<<(16-type)),65536L>>type,0,SLIDEBAR,s,pa,10,0,0,xdim-1,ydim-1);
    if (rev == 0)
        rotatesprite((x<<16)+((scale(64,xloc-min,max-min)+1)<<(16-type)),(y<<16)+(1<<(16-type)),65536L>>type,0,SLIDEBAR+1,s,pa,10,0,0,xdim-1,ydim-1);
    else
        rotatesprite((x<<16)+((65-scale(64,xloc-min,max-min))<<(16-type)),(y<<16)+(1<<(16-type)),65536L>>type,0,SLIDEBAR+1,s,pa,10,0,0,xdim-1,ydim-1);
}

#define bar(x,y,p,dainc,damodify,s,pa) _bar(0,x,y,p,dainc,damodify,s,pa,0,63);
#define barsm(x,y,p,dainc,damodify,s,pa) _bar(1,x,y,p,dainc,damodify,s,pa,0,63);

static void modval(int min, int max,int *p,int dainc,int damodify)
{
    char rev;

    if (dainc < 0)
    {
        dainc = -dainc;
        rev = 1;
    }
    else rev = 0;

    if (damodify)
    {
        if (rev == 0)
        {
            if (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_kpad_4) || ((buttonstat&1) && minfo.mousex < -256))        // && onbar) )
            {
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_kpad_4);

                *p -= dainc;
                if (*p < min)
                {
                    *p = max;
                    if (damodify == 2)
                        *p = min;
                }
                S_PlaySound(PISTOL_BODYHIT);
            }
            if (KB_KeyPressed(sc_RightArrow) || KB_KeyPressed(sc_kpad_6) || ((buttonstat&1) && minfo.mousex > 256))       //&& onbar) )
            {
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_6);

                *p += dainc;
                if (*p > max)
                {
                    *p = min;
                    if (damodify == 2)
                        *p = max;
                }
                S_PlaySound(PISTOL_BODYHIT);
            }
        }
        else
        {
            if (KB_KeyPressed(sc_RightArrow) || KB_KeyPressed(sc_kpad_6) || ((buttonstat&1) && minfo.mousex > 256))      //&& onbar ))
            {
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_6);

                *p -= dainc;
                if (*p < min)
                {
                    *p = max;
                    if (damodify == 2)
                        *p = min;
                }
                S_PlaySound(PISTOL_BODYHIT);
            }
            if (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_kpad_4) || ((buttonstat&1) && minfo.mousex < -256))      // && onbar) )
            {
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_kpad_4);

                *p += dainc;
                if (*p > max)
                {
                    *p = min;
                    if (damodify == 2)
                        *p = max;
                }
                S_PlaySound(PISTOL_BODYHIT);
            }
        }
    }
}

// In-game lobby (masterserver) state.
static char g_joinAddrBuf[64];
// #define MENUHIGHLIGHT(x) probey==x?-(sintable[((int32_t)totalclock<<4)&2047]>>12):probey-x>=0?(probey-x)<<2:-((probey-x)<<2)

#define SHX(X) 0
// ((x==X)*(-sh))
#define PHX(X) 0
// ((x==X)?1:2)
#define MWIN(X) rotatesprite( 320<<15,200<<15,X,0,MENUSCREEN,-16,0,10+64,0,0,xdim-1,ydim-1)
#define MWINXY(X,OX,OY) rotatesprite( ( 320+(OX) )<<15, ( 200+(OY) )<<15,X,0,MENUSCREEN,-16,0,10+64,0,0,xdim-1,ydim-1)

extern int G_LoadSaveHeader(char spot,struct savehead *saveh);

static struct savehead savehead;
//static int32_t volnum,levnum,plrskl,numplr;
//static char brdfn[BMAX_PATH];
int g_lastSaveSlot = -1;

static int Menu_NgcVisibleEntries(MenuGameplayEntry const *entries, int *visible)
{
    int count = 0;

    if (!entries)
        return 0;

    for (int i = 0; i < MAXMENUGAMEPLAYENTRIES; ++i)
    {
        if (!entries[i].isValid())
            break;

        if (entries[i].flags & MEF_Hidden)
            continue;

        visible[count++] = i;
    }

    return count;
}

static int Menu_DefinedSkillCount(void)
{
    int count = 0;

    for (int i = 0; i < (int)ARRAY_SIZE(g_skillNames); ++i)
    {
        if (g_skillNames[i][0])
            count = i + 1;
    }

    return count;
}

static int Menu_FirstDefinedSkill(void)
{
    for (int i = 0; i < (int)ARRAY_SIZE(g_skillNames); ++i)
    {
        if (g_skillNames[i][0])
            return i;
    }

    return 0;
}

static void Menu_SyncFurySkillSelection(void)
{
    if (!FURY || g_currentMenu != MENU_SKILL)
        return;

    int const skillCount = Menu_DefinedSkillCount();
    if (skillCount <= 0)
        return;

    probey = clamp(probey, 0, skillCount - 1);
    ud.m_player_skill = probey + 1;
}

static int Menu_NgcVisiblePositionForRawIndex(MenuGameplayEntry const *entries, int rawIndex)
{
    if (!entries || (unsigned)rawIndex >= MAXMENUGAMEPLAYENTRIES)
        return 0;

    int position = 0;

    for (int i = 0; i < MAXMENUGAMEPLAYENTRIES; ++i)
    {
        if (!entries[i].isValid())
            break;

        if (entries[i].flags & MEF_Hidden)
            continue;

        if (i == rawIndex)
            return position;

        ++position;
    }

    return 0;
}

static bool Menu_NgcHasVisibleSubentries(MenuGameplayEntry const &entry)
{
    int visible[MAXMENUGAMEPLAYENTRIES];
    return Menu_NgcVisibleEntries(entry.subentries, visible) > 0;
}

static MenuGameplayEntry *Menu_NgcEntriesForCurrentMenu(char const **title)
{
    static char const s_newGameTitle[] = "NEW GAME";

    *title = s_newGameTitle;

    if (g_currentMenu == MENU_NEWGAMECUSTOM)
        return g_MenuGameplayEntries;

    if (g_currentMenu == MENU_NEWGAMECUSTOMSUB)
    {
        int indexes[1] = { ud.m_newgamecustom };
        MenuGameplayEntry *entry = G_GetNewGameCustomEntry(indexes, 1);

        if (!entry)
            return nullptr;

        *title = entry->name;
        return entry->subentries;
    }

    if (g_currentMenu == MENU_NEWGAMECUSTOML3)
    {
        int indexes[2] = { ud.m_newgamecustom, ud.m_newgamecustomsub };
        MenuGameplayEntry *entry = G_GetNewGameCustomEntry(indexes, 2);

        if (!entry)
            return nullptr;

        *title = entry->name;
        return entry->subentries;
    }

    return nullptr;
}

static void Menu_NgcSetCurrentSelection(int rawIndex)
{
    if (g_currentMenu == MENU_NEWGAMECUSTOM)
        ud.m_newgamecustom = rawIndex;
    else if (g_currentMenu == MENU_NEWGAMECUSTOMSUB)
        ud.m_newgamecustomsub = rawIndex;
    else if (g_currentMenu == MENU_NEWGAMECUSTOML3)
        ud.m_newgamecustoml3 = rawIndex;
}

static int Menu_NgcCurrentRawSelection(void)
{
    switch (g_currentMenu)
    {
        case MENU_NEWGAMECUSTOM:   return ud.m_newgamecustom;
        case MENU_NEWGAMECUSTOMSUB: return ud.m_newgamecustomsub;
        case MENU_NEWGAMECUSTOML3:  return ud.m_newgamecustoml3;
        default:                   return probey;
    }
}

static int Menu_ScriptVisibleMenuID(int menuID)
{
    if (menuID >= MENU_LOADVERIFY && menuID < MENU_LOADVERIFY + 10)
        return MENU_LOADVERIFY;
    if (menuID >= MENU_SAVEVERIFY && menuID < MENU_SAVEVERIFY + 10)
        return MENU_SAVEVERIFY;

    return menuID;
}

static void Menu_ApplyFuryTextDefaults(void)
{
    if (!FURY)
        return;

    if (ud.menu_shadedeselected == 16)
        ud.menu_shadedeselected = 10;
    if (ud.menu_shadedisabled == 16)
        ud.menu_shadedisabled = 10;
    if (ud.menutext_zoom == 65536)
        ud.menutext_zoom = 12288;
    if (ud.menutext_xspace == 0)
        ud.menutext_xspace = 5 << 16;
    if (ud.menutext_tracking == 0)
        ud.menutext_tracking = 2 << 16;
    if (ud.menutext_pal == 0)
        ud.menutext_pal = 11;
    if (ud.menutext_palselected == 0)
        ud.menutext_palselected = 10;
    if (ud.menutext_paldeselected == 0)
        ud.menutext_paldeselected = 10;
    if (ud.menutext_paldisabled == 0)
        ud.menutext_paldisabled = 7;
    if (ud.menutext_palselected_right == 0)
        ud.menutext_palselected_right = 11;
    if (ud.menutext_paldeselected_right == 0)
        ud.menutext_paldeselected_right = 11;
    if (ud.menutext_paldisabled_right == 0)
        ud.menutext_paldisabled_right = 7;

    if (ud.gametext_zoom == 65536 || ud.gametext_zoom == 32768)
        ud.gametext_zoom = FURY_BLUE_ZOOM;
    if (ud.gametext_xspace == 0 || ud.gametext_xspace == (8 << 16))
        ud.gametext_xspace = FURY_BLUE_SPACE;
    if (ud.gametext_pal == 0)
        ud.gametext_pal = 11;
    if (ud.gametext_palselected == 0)
        ud.gametext_palselected = 10;
    if (ud.gametext_paldeselected == 0)
        ud.gametext_paldeselected = 10;
    if (ud.gametext_paldisabled == 0)
        ud.gametext_paldisabled = 7;
    if (ud.gametext_palselected_right == 0)
        ud.gametext_palselected_right = 11;
    if (ud.gametext_paldeselected_right == 0)
        ud.gametext_paldeselected_right = 11;
    if (ud.gametext_paldisabled_right == 0)
        ud.gametext_paldisabled_right = 7;
}

static void Menu_SetDisplayEventReturnVars(int32_t originX, int32_t originY)
{
    ud.returnvar[0] = originX;
    ud.returnvar[1] = originY;
    ud.returnvar[2] = Menu_NgcCurrentRawSelection();
    ud.returnvar[3] = -1;

    if (g_currentMenu == MENU_NEWGAMECUSTOMSUB)
        ud.returnvar[3] = ud.m_newgamecustom;
    else if (g_currentMenu == MENU_NEWGAMECUSTOML3)
        ud.returnvar[3] = ud.m_newgamecustomsub;
}

static void Menu_RunDisplayEvent(int eventID)
{
    Menu_SetDisplayEventReturnVars(0, 0);

    if (eventID == EVENT_DISPLAYMENU)
        g_menuScriptOrigin = {};

    int const nativeMenu = g_currentMenu;
    bool const furySkillPreview = FURY && nativeMenu == MENU_SKILL;
    int const savedPlayerSkill = ud.player_skill;

    // Ion Fury confirmation prompts are native modal UI. Do not run scripted
    // menu draw events while they are active; those events repaint save/load
    // rows under the modal in this older menu implementation.
    if (FURY && Menu_IsVerifyPrompt(nativeMenu))
        return;

    int const scriptMenu = Menu_ScriptVisibleMenuID(nativeMenu);

    if (scriptMenu != nativeMenu)
        g_currentMenu = scriptMenu;

    if (furySkillPreview)
        ud.player_skill = ud.m_player_skill;

    X_OnEventWithReturn(eventID, g_player[screenpeek].ps->i, screenpeek, scriptMenu);

    if (furySkillPreview)
        ud.player_skill = savedPlayerSkill;

    if (eventID == EVENT_DISPLAYMENU)
        g_menuScriptOrigin = { ud.returnvar[0], ud.returnvar[1] };

    if (scriptMenu != nativeMenu)
        g_currentMenu = nativeMenu;
}

static void Menu_DisplayScriptCursor(void)
{
    if (!VM_HaveEvent(EVENT_DISPLAYCURSOR))
        return;

    vec2_t mousePos;

    if (!mouseReadAbs(&mousePos, &g_mouseAbs))
        return;

    ud.returnvar[0] = mousePos.x;
    ud.returnvar[1] = mousePos.y;
    ud.returnvar[2] = 255 / 3;

    int32_t const tile = X_OnEventWithReturn(EVENT_DISPLAYCURSOR, g_player[screenpeek].ps->i, screenpeek, CROSSHAIR);

    if ((unsigned)tile < MAXTILES && tilesiz[tile].x > 0 && tilesiz[tile].y > 0)
        rotatesprite_fs_alpha(mousePos.x, mousePos.y, FURY ? 32768 : 65536, 0, tile, 0, 0, 2 | 8 | (FURY ? 1024 : 0), 255 / 3);
}

static void Menu_NgcFireSelectionEvent(void)
{
    ud.returnvar[0] = ud.returnvar[1] = ud.returnvar[2] = -1;

    if (g_currentMenu == MENU_NEWGAMECUSTOMSUB)
        ud.returnvar[0] = ud.m_newgamecustomsub;
    else if (g_currentMenu == MENU_NEWGAMECUSTOML3)
    {
        ud.returnvar[0] = ud.m_newgamecustomsub;
        ud.returnvar[1] = ud.m_newgamecustoml3;
    }

    X_OnEventWithReturn(EVENT_NEWGAMECUSTOM, -1, myconnectindex, ud.m_newgamecustom);
}

static void Menu_NgcBack(void)
{
    switch (g_currentMenu)
    {
        case MENU_NEWGAMECUSTOM:
            ChangeToMenu((ud.gm & MODE_GAME) ? MENU_MAIN_INGAME : MENU_MAIN);
            break;
        case MENU_NEWGAMECUSTOMSUB:
            ChangeToMenu(MENU_NEWGAMECUSTOM);
            break;
        case MENU_NEWGAMECUSTOML3:
            ChangeToMenu(MENU_NEWGAMECUSTOMSUB);
            break;
    }
}

static void Menu_NgcActivate(MenuGameplayEntry const &entry)
{
    if (entry.flags & MEF_Disabled)
        return;

    if (entry.gameFlags & MGE_UserContent)
    {
        Menu_NgcFireSelectionEvent();
        currentlist = 1;
        last_menu_pos = probey;
        ChangeToMenu(MENU_USERMAP);
        return;
    }

    if (Menu_NgcHasVisibleSubentries(entry))
    {
        if (g_currentMenu == MENU_NEWGAMECUSTOM)
            ChangeToMenu(MENU_NEWGAMECUSTOMSUB);
        else if (g_currentMenu == MENU_NEWGAMECUSTOMSUB)
            ChangeToMenu(MENU_NEWGAMECUSTOML3);
        else
        {
            Menu_NgcFireSelectionEvent();
            last_skill_parent_menu = g_currentMenu;
            ChangeToMenu(MENU_SKILL);
        }

        return;
    }

    Menu_NgcFireSelectionEvent();
    last_skill_parent_menu = g_currentMenu;
    ChangeToMenu(MENU_SKILL);
}

static void Menu_DisplayNewGameCustom(void)
{
    char const *title = nullptr;
    MenuGameplayEntry *entries = Menu_NgcEntriesForCurrentMenu(&title);
    int visible[MAXMENUGAMEPLAYENTRIES];
    int const visibleCount = Menu_NgcVisibleEntries(entries, visible);
    int const startY = FURY ? 58 + ((3 - visibleCount) * 6) : 60 - (visibleCount * 2);

    if (!entries || visibleCount <= 0)
    {
        ChangeToMenu(MENU_EPISODE);
        return;
    }

    if (!FURY)
    {
        rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
        menutext(160, 24, 0, 0, title ? title : "NEW GAME");
    }

    int x = M_Probe(160, startY, FURY ? FURY_TOP_MENU_STEP : 20, visibleCount);

    if (probey < 0 || probey >= visibleCount)
    {
        int rawSelection = 0;

        switch (g_currentMenu)
        {
            case MENU_NEWGAMECUSTOM:    rawSelection = ud.m_newgamecustom; break;
            case MENU_NEWGAMECUSTOMSUB: rawSelection = ud.m_newgamecustomsub; break;
            case MENU_NEWGAMECUSTOML3:  rawSelection = ud.m_newgamecustoml3; break;
            default: break;
        }

        probey = Menu_NgcVisiblePositionForRawIndex(entries, rawSelection);
    }

    int const selectedRawIndex = visible[probey];
    Menu_NgcSetCurrentSelection(selectedRawIndex);

    if (x == -1)
    {
        Menu_NgcBack();
        KB_FlushKeyboardQueue();
        return;
    }

    if (x >= 0)
        Menu_NgcActivate(entries[visible[x]]);

    for (int i = 0; i < visibleCount; ++i)
    {
        MenuGameplayEntry const &entry = entries[visible[i]];
        int const disabled = entry.flags & (MEF_Disabled | MEF_LookDisabled);

        menutext(160, startY + ((FURY ? FURY_TOP_MENU_STEP : 20) * i), disabled ? DISABLEDMENUSHADE : MENUHIGHLIGHT(i),
                 (entry.flags & MEF_Disabled) ? 1 : PHX(-2 - i), entry.name);
    }
}

static void M_DisplaySaveGameList(void)
{
    int x, c = 160;

    c += 64;
    for (x = 0;x <= 108;x += 12)
        rotatesprite((c+91-64)<<16,(x+56)<<16,65536L,0,TEXTBOX,24,0,10,0,0,xdim-1,ydim-1);

    rotatesprite(22<<16,97<<16,65536L,0,WINDOWBORDER2,24,0,10,0,0,xdim-1,ydim-1);
    rotatesprite(180<<16,97<<16,65536L,1024,WINDOWBORDER2,24,0,10,0,0,xdim-1,ydim-1);
    rotatesprite(99<<16,50<<16,65536L,512,WINDOWBORDER1,24,0,10,0,0,xdim-1,ydim-1);
    rotatesprite(103<<16,144<<16,65536L,1024+512,WINDOWBORDER1,24,0,10,0,0,xdim-1,ydim-1);

    for (x=0;x<=9;x++)
        minitext(c,48+(12*x),ud.savegame[x],2,10+16);
}

static void M_DisplayFurySaveGameList(void)
{
    for (int i = 0; i < 10; ++i)
    {
        char const *name = ud.savegame[i][0] ? ud.savegame[i] : "EMPTY";
        int const shade = (probey == i) ? (sintable[((int32_t)totalclock << 5) & 2047] >> 12) : 0;
        int const pal = (probey == i) ? ud.menutext_palselected : ud.menutext_paldeselected;

        char clipped[sizeof(ud.savegame[0])];
        Bstrncpyz(clipped, name, sizeof(clipped));
        for (int j = 18; clipped[j]; ++j)
            clipped[j] = '\0';

        Menu_FurySmallText(184, 54 + (i * 9), clipped, shade, pal);
    }
}

static void clearfilenames(void)
{
    klistfree(finddirs);
    klistfree(findfiles);
    finddirs = findfiles = NULL;
    numfiles = numdirs = 0;
}

static int getfilenames(const char *path, const char kind[])
{
    BUILDVFS_FIND_REC *r;

    clearfilenames();
    finddirs = klistpath(path,"*", BUILDVFS_FIND_DIR);
    findfiles = klistpath(path,kind,BUILDVFS_FIND_FILE);
    for (r = finddirs; r; r=r->next) numdirs++;
    for (r = findfiles; r; r=r->next) numfiles++;

    finddirshigh = finddirs;
    findfileshigh = findfiles;
    currentlist = 0;
    if (findfileshigh) currentlist = 1;

    return(0);
}

void M_DisplayMenus(void)
{
    BUILDVFS_FIND_REC *dir;
    int c,x,i;
    int l,m;
    const char *p = NULL;
    bool furySkillSelectionPreprocessed = false;

    if (buttonstat != 0 && !onbar)
    {
        x = MOUSE_GetButtons()<<3;
        if (x) buttonstat = x<<3;
        else buttonstat = 0;
    }
    else
        buttonstat = MOUSE_GetButtons();

    if ((ud.gm&MODE_MENU) == 0)
    {
        walock[TILE_LOADSHOT] = 1;
        return;
    }

    int const backgroundOK = Menu_BlackTranslucentBackgroundOK(g_currentMenu);
    int const menuOverGame = (ud.gm & MODE_GAME) || ud.recstat == 2;
    int const furyVerifyPrompt = FURY && Menu_IsVerifyPrompt(g_currentMenu);

    Menu_ApplyFuryTextDefaults();

    ud.gm &= (0xff-MODE_TYPE);
    g_player[myconnectindex].ps->fta = 0;

    x = 0;

    sh = 4-(sintable[((int32_t)totalclock<<4)&2047]>>11);

    if (!FURY && menuOverGame && backgroundOK)
        videoFadeToBlack(1);

    if (videoGetRenderMode() >= 3 && !FURY)
    {
        int x,y=0;
        for (;y<ydim;y+=tilesiz[MENUSCREEN].y)
            for (x=0;x<xdim;x+=tilesiz[MENUSCREEN].x)
                rotatesprite(x<<16,y<<16,65536L,0,MENUSCREEN,80,0,1+8+16,0,0,xdim-1,ydim-1);
    }

    if (Menu_UpdateScreenOK(g_currentMenu))
        G_UpdateScreenArea();

    if (FURY && g_currentMenu == MENU_SKILL)
    {
        Menu_ReadProbeInput();
        Menu_UpdateProbeSelection(Menu_DefinedSkillCount());
        furySkillSelectionPreprocessed = true;
    }

    Menu_SyncFurySkillSelection();

    if (FURY && !furyVerifyPrompt)
        Menu_RunDisplayEvent(EVENT_DISPLAYINACTIVEMENU);

    if (!furyVerifyPrompt)
        Menu_RunDisplayEvent(EVENT_DISPLAYMENU);

    if (FURY && (menuOverGame || g_currentMenu == MENU_SKILL) && backgroundOK && !furyVerifyPrompt)
        videoFadeToBlack(1);

    if (KB_KeyPressed(sc_Q))
    {
        switch (g_currentMenu)
        {
        case MENU_USERMAP_BROWSER:
        case MENU_MOUSEBTNS:
        case 360:
        case 361:
        case 362:
        case 363:
        case 364:
        case 365:
        case 366:
        case 367:
        case 368:
        case 369:
        case MENU_QUIT:
        case MENU_QUITTOTITLE:
        case MENU_QUIT_INGAME:
        case MENU_NETWAITVOTES:
        case MENU_ADULTPASSWORD:
        case 20003:
        case 20005:
            break;
        default:
            if (g_currentMenu >= 0)
            {
                last_menu = g_currentMenu;
                last_menu_pos = probey;
                ChangeToMenu(MENU_QUIT_INGAME);
            }
            break;
        }
    }

    switch (g_currentMenu)
    {
        case 25000:
        {
            mgametext(160, 90, "SELECT A SAVE SPOT BEFORE", 0, 2 + 8 + 16);
            mgametext(160, 90 + 9, "YOU QUICK RESTORE.", 0, 2 + 8 + 16);

            x = M_Probe(186, 124, 0, 1);
            if (x >= -1)
            {
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }
                ud.gm &= ~MODE_MENU;
            }
            break;
        }

        case 20000:
        {
            x = M_Probe(326, 190, 0, 1);
            mgametext(160, 41 - 8, "YOU ARE PLAYING THE SHAREWARE", 0, 2 + 8 + 16);
            mgametext(160, 50 - 8, "VERSION OF DUKE NUKEM 3D.  WHILE", 0, 2 + 8 + 16);
            mgametext(160, 59 - 8, "THIS VERSION IS REALLY COOL, YOU", 0, 2 + 8 + 16);
            mgametext(160, 68 - 8, "ARE MISSING OVER 75%% OF THE TOTAL", 0, 2 + 8 + 16);
            mgametext(160, 77 - 8, "GAME, ALONG WITH OTHER GREAT EXTRAS", 0, 2 + 8 + 16);
            mgametext(160, 86 - 8, "AND GAMES, WHICH YOU'LL GET WHEN", 0, 2 + 8 + 16);
            mgametext(160, 95 - 8, "YOU ORDER THE COMPLETE VERSION AND", 0, 2 + 8 + 16);
            mgametext(160, 104 - 8, "GET THE FINAL THREE EPISODES.", 0, 2 + 8 + 16);

            mgametext(160, 104 + 8, "PLEASE READ THE 'HOW TO ORDER' ITEM", 0, 2 + 8 + 16);
            mgametext(160, 113 + 8, "ON THE MAIN MENU OR VISIT", 0, 2 + 8 + 16);
            mgametext(160, 122 + 8, "HTTP://WWW.BUY3DREALMS.COM", 0, 2 + 8 + 16);
            mgametext(160, 131 + 8, "TO UPGRADE TO THE FULL REGISTERED", 0, 2 + 8 + 16);
            mgametext(160, 139 + 8, "VERSION OF DUKE NUKEM 3D.", 0, 2 + 8 + 16);
            mgametext(160, 148 + 16, "PRESS ANY KEY...", 0, 2 + 8 + 16);

            if (x >= -1) ChangeToMenu(100);
            break;
        }

        case 20001:
        {
            rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(160, 24, 0, 0, "NETWORK GAME");

            x = M_Probe(160, 100 - 18, 18, 3);

            if (x == -1) ChangeToMenu(0);
            else if (x == 2) ChangeToMenu(20010);
            else if (x == 1) ChangeToMenu(20020);
            else if (x == 0) ChangeToMenu(20002);

            menutext(160, 100 - 18, MENUHIGHLIGHT(0), 0, "PLAYER SETUP");
            menutext(160, 100, MENUHIGHLIGHT(1), 0, "JOIN GAME");
            menutext(160, 100 + 18, MENUHIGHLIGHT(2), 0, "HOST GAME");
            break;
        }

        case 20010:   // HOST GAME
        {
            rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(160, 24, 0, 0, "HOST GAME");

            x = M_Probe(160, 70, 18, 2);
            if (x == -1) ChangeToMenu(20001);
            else if (x == 0) Net_LaunchHost(1);   // advertise to the masterserver (internet)
            else if (x == 1) Net_LaunchHost(0);   // LAN only

            menutext(160, 70, MENUHIGHLIGHT(0), 0, "HOST INTERNET GAME");
            menutext(160, 88, MENUHIGHLIGHT(1), 0, "HOST LAN GAME");
            mgametext(160, 140, "Starts a 4-player listen server on E1L1.", 0, 2 + 8 + 16);
            mgametext(160, 150, "Others join from their lobby; you can", 0, 2 + 8 + 16);
            mgametext(160, 160, "change map from the console (map exly).", 0, 2 + 8 + 16);
            break;
        }

        case 20020:   // JOIN GAME - masterserver lobby list
        {
            rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(160, 24, 0, 0, "JOIN GAME");

            static struct msserver_t s_list[12];
            static int32_t s_count = -2;   // -2 = need to query, -1 = query failed, >=0 = results
            if (s_count == -2)
                s_count = Masterserver_QueryList(g_masterServerAddr, s_list, 12, 1200);

            int32_t const nServers = (s_count > 0) ? (s_count > 12 ? 12 : s_count) : 0;
            int32_t const nItems = nServers + 2;   // + "enter address" + "refresh"

            int32_t const y0 = 46;
            x = probesm(52, y0, 9, nItems);

            for (int32_t i = 0; i < nServers; i++)
            {
                char line[96];
                // Primary address is the mandatory IPv4; note when IPv6 is also
                // available for the same server.
                char const *shown = s_list[i].addr[0] ? s_list[i].addr : s_list[i].addr6;
                char const *v6tag = (s_list[i].addr[0] && s_list[i].addr6[0]) ? " +v6"
                                  : (!s_list[i].addr[0] && s_list[i].addr6[0]) ? " v6" : "";
                Bsnprintf(line, sizeof(line), "%-14.14s %2d/%-2d %-8.8s %s%s",
                          s_list[i].name, s_list[i].players, s_list[i].maxplayers, s_list[i].map, shown, v6tag);
                minitextshade(52, y0 + i * 9, line, (probey == i) ? 0 : 16, 0, 2 + 8 + 16);
            }
            minitextshade(52, y0 + nServers * 9, "Enter address manually...", (probey == nServers) ? 0 : 16, 0, 2 + 8 + 16);
            minitextshade(52, y0 + (nServers + 1) * 9, "Refresh list", (probey == nServers + 1) ? 0 : 16, 0, 2 + 8 + 16);

            // If we just relaunched here after a failed join, show why (in red).
            if (g_joinErrorMsg[0])
                mgametext(160, 138, g_joinErrorMsg, 0, 2 + 8 + 16);

            if (s_count == -1)
                mgametext(160, 150, "Could not reach the masterserver.", 0, 2 + 8 + 16);
            else if (nServers == 0)
                mgametext(160, 150, "No servers advertised.", 0, 2 + 8 + 16);

            char hdr[96];
            Bsnprintf(hdr, sizeof(hdr), "Masterserver: %s", g_masterServerAddr);
            mgametext(160, 172, hdr, 0, 2 + 8 + 16);

            if (x == -1) { s_count = -2; ChangeToMenu(20001); }
            else if (x >= 0 && x < nServers)
            {
                // Connect over IPv4 by default (always reachable); use IPv6 when
                // the user prefers it and it's offered, or when it's the only
                // address the server published.
                char const *tgt = s_list[x].addr;
                if ((g_netPreferIPv6 && s_list[x].addr6[0]) || !tgt[0])
                    tgt = s_list[x].addr6;
                if (tgt[0]) Net_LaunchAddress(tgt);
            }
            else if (x == nServers) { g_joinAddrBuf[0] = 0; inputloc = 0; KB_FlushKeyboardQueue(); ChangeToMenu(20021); }
            else if (x == nServers + 1) { s_count = -2; }   // refresh
            break;
        }

        case 20021:   // JOIN - manual address entry
        {
            rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(160, 24, 0, 0, "JOIN BY ADDRESS");

            mgametext(160, 64, "Enter the server address and port:", 0, 2 + 8 + 16);
            mgametext(160, 78, "IPv4  1.2.3.4:23513", 0, 2 + 8 + 16);
            mgametext(160, 88, "IPv6  [2001:db8::1]:23513", 0, 2 + 8 + 16);

            char ch = KB_GetCh();
            int32_t L = Bstrlen(g_joinAddrBuf);
            if (ch == asc_BackSpace) { if (L > 0) g_joinAddrBuf[L - 1] = 0; }
            else if (ch == asc_Enter) { if (L > 0) Net_LaunchAddress(g_joinAddrBuf); }
            else if (ch == asc_Escape) { KB_ClearKeyDown(sc_Escape); ChangeToMenu(20020); }
            else if (ch >= 32 && ch < 127 && L < (int32_t)sizeof(g_joinAddrBuf) - 2)
            { g_joinAddrBuf[L] = ch; g_joinAddrBuf[L + 1] = 0; }

            char field[80];
            Bsnprintf(field, sizeof(field), "%s%s", g_joinAddrBuf, ((int32_t)totalclock & 32) ? "_" : "");
            mgametext(160, 112, field[0] ? field : "_", 0, 2 + 8 + 16);
            mgametext(160, 150, "ENTER to join      ESC to go back", 0, 2 + 8 + 16);
            break;
        }

        case 20002:
        case 20003:
        {
            Menu_DrawTopBarTitle(19, "PLAYER SETUP", 24);
            c = (320 >> 1) - 120;
            {
                int io, ii, yy = 37, d = c + 140, enabled;
                const char *opts[] =
                {
                    "Name",
                    "-",
                    "Color",
                    "-",
                    "Team",
                    "-",
                    "Skin",
                    "-",
                    "Auto aim",
                    "Mouse aim",
                    "-",
                    "-",
                    "Switch weapons on pickup",
                    "Switch weapons when empty",
                    "-",
                    "-",
                    "Multiplayer macros",
                    NULL
                };

                x = playerColor_getValidPal(ud.m_color);

                if (probey == 2)
                    x = teams[ud.team].palnum;

                Menu_DrawPlayerPreview(x);

                for (ii = io = 0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        if (io <= probey) yy += 4;
                        continue;
                    }
                    if (io < probey) yy += 8;
                    io++;
                }


                if (g_currentMenu == 20002)
                {
                    x = probesm(c, yy + 5, 0, io);

                    if (x == -1)
                    {
                        ChangeToMenu(202);
                        probey = 3;
                        break;
                    }

                    for (ii = io = 0; opts[ii]; ii++)
                    {
                        if (opts[ii][0] == '-' && !opts[ii][1])
                            continue;
                        enabled = 1;
                        switch (io)
                        {
                        case 0:
                            if (x == io)
                            {
                                strcpy(buf, szPlayerName);
                                inputloc = strlen(buf);
                                g_currentMenu = 20003;

                                KB_ClearKeyDown(sc_Enter);
                                KB_ClearKeyDown(sc_kpad_Enter);
                                KB_FlushKeyboardQueue();
                            }
                            break;

                        case 1:
                            i = ud.m_color;
                            if (x == io)
                            {
                                ud.m_color = playerColor_getNextColor(ud.m_color, 1);
                            }

                            modval(-1, MAXPALOOKUPS+1, (int *)&ud.m_color, 1, probey == 1);

                            if (ud.m_color != i)
                            {
                                if (ud.m_color > i)
                                {
                                    ud.m_color = playerColor_getNextColor(i, 1);
                                }
                                else
                                {
                                    ud.m_color = playerColor_getPreviousColor(i, 1);
                                }

                                ud.color = playerColor_getValidPal(ud.m_color);
                                G_UpdatePlayerFromMenu();
                            }

                            break;

                        case 2:
                            i = ud.team;
                            if (x == io)
                            {
                                ud.team++;
                                if (ud.team >= numTeams)
                                    ud.team = 0;
                            }
                            modval(0, numTeams-1, (int *)&ud.team, 1, probey == 2);
                            if (ud.team != i)
                                G_UpdatePlayerFromMenu();
                            break;

                        case 3:
                            i = ud.skin;
                            if (x == io)
                            {
                                ud.skin++;
                                if (ud.skin >= MAXPLAYERSKINS)
                                    ud.skin = 0;
                            }
                            modval(0, MAXPLAYERSKINS-1, (int *)&ud.skin, 1, probey == 3);
                            if (ud.skin != i)
                            {
                                // Skip version-gated skins (e.g. Alien Beast
                                // in shareware) by continuing in the direction we
                                // just moved, instead of snapping back to Duke -
                                // otherwise cycling backward from Duke lands on a
                                // gated skin and appears stuck. Skin 0 is always
                                // valid, so this terminates.
                                int const dir = (ud.skin == (i + 1) % MAXPLAYERSKINS) ? 1 : -1;
                                for (int guard = 0; ud.skin != 0 && !P_ValidateSkin(ud.skin) && guard < MAXPLAYERSKINS; guard++)
                                {
                                    ud.skin += dir;
                                    if (ud.skin >= MAXPLAYERSKINS) ud.skin = 0;
                                    else if (ud.skin < 0) ud.skin = MAXPLAYERSKINS - 1;
                                }
                                if (ud.skin && !P_ValidateSkin(ud.skin))
                                    ud.skin = 0;
                                G_UpdatePlayerFromMenu();
                            }
                            break;

                        case 4:
                            i = ud.config.AutoAim;
                            if (x == io)
                                ud.config.AutoAim = (ud.config.AutoAim == 2) ? 0 : ud.config.AutoAim + 1;
                            modval(0, 2, (int *)&ud.config.AutoAim, 1, probey == 4);
                            if (ud.config.AutoAim != i)
                                G_UpdatePlayerFromMenu();
                            break;

                        case 5:
                            i = ud.mouseaiming;
                            if (x == io)
                                ud.mouseaiming = !ud.mouseaiming;
                            modval(0, 1, (int *)&ud.mouseaiming, 1, probey == 5);
                            if (ud.mouseaiming != i)
                                G_UpdatePlayerFromMenu();
                            break;

                        case 6:
                            i = 0;
                            if (ud.weaponswitch & 1)
                            {
                                i = 1;
                                if (ud.weaponswitch & 4)
                                    i = 2;
                            }
                            l = i;
                            if (x == io)
                                i = (i == 2) ? 0 : i + 1;
                            modval(0, 2, (int32_t *)&i, 1, probey == 6);
                            if (i != l)
                            {
                                if (i > 0)
                                {
                                    ud.weaponswitch |= 1;
                                    if (i == 2)
                                        ud.weaponswitch |= 4;
                                    else
                                        ud.weaponswitch &= ~4;
                                }
                                else
                                    ud.weaponswitch &= ~(1 | 4);

                                G_UpdatePlayerFromMenu();
                            }
                            break;
                        case 7:
                            i = 0;
                            if (ud.weaponswitch & 2)
                                i = 1;
                            if (x == io)
                                i = 1 - i;
                            modval(0, 1, (int *)&i, 1, probey == 7);
                            if ((ud.weaponswitch & 2 && !i) || (!(ud.weaponswitch & 2) && i))
                            {
                                ud.weaponswitch ^= 2;
                                G_UpdatePlayerFromMenu();
                            }
                            break;

                        case 8:
                            if (x == io)
                            {
                                ChangeToMenu(20004);
                            }
                            break;

                        default:
                            break;
                        }
                        io++;
                    }
                }
                else
                {
                    // because stripcolorcodes needs a valid target and tempbuf is used in G_EnterText()
                    char dummybuf[64];
                    x = G_EnterText(d - 50, 37, buf, 30, 0);

                    while (Bstrlen(OSD_StripColors(dummybuf, buf)) > 10)
                    {
                        buf[Bstrlen(buf) - 1] = '\0';
                        inputloc--;
                    }

                    if (x)
                    {
                        if (x == 1)
                        {
                            if (buf[0] && Bstrcmp(szPlayerName, buf))
                            {
                                Bstrcpy(szPlayerName, buf);
                                Net_SendPlayerName();
                            }
                            // send name update
                        }
                        KB_ClearKeyDown(sc_Enter);
                        KB_ClearKeyDown(sc_kpad_Enter);
                        KB_FlushKeyboardQueue();

                        g_currentMenu = 20002;
                    }
                }

                yy = 37;
                {
                    for (ii = io = 0; opts[ii]; ii++)
                    {
                        if (opts[ii][0] == '-' && !opts[ii][1])
                        {
                            yy += 4;
                            continue;
                        }
                        enabled = 1;
                        switch (io)
                        {
                        case 0:
                            if (g_currentMenu == 20002)
                            {
                                mgametext(d - 50, yy, szPlayerName, MENUHIGHLIGHT(io), 2 + 8 + 16);
                            }
                            break;

                        case 1:
                        {
                            char s[32];
                            if (playerColor_getValidPal(ud.m_color) || ud.m_color == 0)
                            {
                                sprintf(s, "%s", pColorNames[ud.m_color]);
                            }
                            else
                            {
                                sprintf(s, "Unknown: %d", ud.m_color);
                            }

                            mgametext(d - 50, yy, s, MENUHIGHLIGHT(io), 2 + 8 + 16);
                        }
                        break;

                        case 2:
                        {
                            mgametext(d - 50, yy, teams[ud.team].name, MENUHIGHLIGHT(io), 2 + 8 + 16);
                        }
                        break;

                        case 3:
                            mgametext(d - 50, yy, g_playerSkinNames[(unsigned)ud.skin < MAXPLAYERSKINS ? ud.skin : 0], MENUHIGHLIGHT(io), 2 + 8 + 16);
                            break;

                        case 4:
                        {
                            const char *s[] = { "Off", "All weapons", "Bullets only" };
                            mgametext(d - 50, yy, s[ud.config.AutoAim], MENUHIGHLIGHT(io), 2 + 8 + 16);
                        }
                        break;

                        case 5:
                            mgametext(d - 50, yy, ud.mouseaiming ? "Hold button" : "Toggle on/off", MENUHIGHLIGHT(io), 2 + 8 + 16);
                            break;

                        case 6:
                        {
                            const char *s[] = { "Off", "All weapons", "Fav priority" };
                            i = 0;
                            if (ud.weaponswitch & 1)
                            {
                                i = 1;
                                if (ud.weaponswitch & 4)
                                    i = 2;
                            }
                            mgametext(d + 45, yy, s[i], MENUHIGHLIGHT(io), 2 + 8 + 16);
                        }
                        break;

                        case 7:
                            mgametext(d + 45, yy, ud.weaponswitch & 2 ? "On" : "Off", MENUHIGHLIGHT(io), 2 + 8 + 16);
                            break;

                        default:
                            break;
                        }
                        mgametextpal(c, yy, opts[ii], enabled ? MENUHIGHLIGHT(io) : DISABLEDMENUSHADE, 10);
                        io++;
                        yy += 8;
                    }
                }
            }

            break;
        }

        case 20004:
        case 20005:
        {
            rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(160, 24, 0, 0, "MULTIPLAYER MACROS");

            if (g_currentMenu == 20004)
            {
                x = probesm(24, 45, 8, 10);
                if (x == -1)
                {
                    ChangeToMenu(20002);
                    probey = 8;
                }
                else if (x >= 0 && x <= 9)
                {
                    strcpy(buf, ud.ridecule[x]);
                    inputloc = strlen(buf);
                    last_menu_pos = probey;
                    g_currentMenu = 20005;
                    KB_ClearKeyDown(sc_Enter);
                    KB_ClearKeyDown(sc_kpad_Enter);
                    KB_FlushKeyboardQueue();
                }
            }
            else
            {
                x = G_EnterText(26, 40 + (8 * probey), buf, 34, 0);
                if (x)
                {
                    if (x == 1)
                    {
                        Bstrcpy(ud.ridecule[last_menu_pos], buf);
                    }
                    KB_ClearKeyDown(sc_Enter);
                    KB_ClearKeyDown(sc_kpad_Enter);
                    KB_FlushKeyboardQueue();
                    g_currentMenu = 20004;
                }
            }
            for (i = 0; i < 10; i++)
            {
                if (g_currentMenu == 20005 && i == last_menu_pos) continue;
                mgametextpal(26, 40 + (i << 3), ud.ridecule[i], MENUHIGHLIGHT(i), 0);
            }

            mgametext(160, 144, "ACTIVATE IN-GAME WITH SHIFT-F#", 0, 2 + 8 + 16);

            break;
        }

        case 15001:
        case 15000:
        {
            if (FURY)
            {
                Bsprintf(tempbuf, "\"%.18s\"", ud.savegame[g_lastSaveSlot]);
                Menu_FuryVerifyPrompt("Load last game:", tempbuf);
            }
            else
            {
                mgametext(160, 90, "LOAD last game:", 0, 2 + 8 + 16);

                sprintf(tempbuf, "\"%s\"", ud.savegame[g_lastSaveSlot]);
                mgametext(160, 99, tempbuf, 0, 2 + 8 + 16);

                mgametext(160, 99 + 9, "(Y/N)", 0, 2 + 8 + 16);
            }

            if (KB_KeyPressed(sc_Escape) || KB_KeyPressed(sc_N) || RMB)
            {
                if (sprite[g_player[myconnectindex].ps->i].extra <= 0)
                {
                    if (G_EnterLevel(MODE_GAME)) G_BackToMenu();
                    return;
                }

                KB_ClearKeyDown(sc_N);
                KB_ClearKeyDown(sc_Escape);

                ud.gm &= ~MODE_MENU;
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }
            }

            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                KB_FlushKeyboardQueue();
                KB_ClearKeysDown();
                FX_StopAllSounds();

                if (ud.multimode > 1)
                {
                    G_LoadPlayer(-1 - g_lastSaveSlot);
                    ud.gm = MODE_GAME;
                }
                else
                {
                    c = G_LoadPlayer(g_lastSaveSlot);
                    if (c == 0)
                        ud.gm = MODE_GAME;
                }
            }

            M_Probe(186, 124 + 9, 0, 0);

            break;
        }

        case 10000:
        case 10001:
        {
            c = 60;
            rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(160, 24, 0, 0, "ADULT MODE");

            x = M_Probe(60, 50 + 16, 16, 2);
            if (x == -1)
            {
                ChangeToMenu(201);
                probey = 0;
                break;
            }

            menutext(c, 50 + 16, MENUHIGHLIGHT(0), PHX(-2), "ADULT MODE");
            menutext(c, 50 + 16 + 16, MENUHIGHLIGHT(1), PHX(-3), "ENTER PASSWORD");

            menutext(c + 160 + 40, 50 + 16, MENUHIGHLIGHT(0), 0, ud.lockout ? "OFF" : "ON");

            if (g_currentMenu == 10001)
            {
                mgametext(160, 50 + 16 + 16 + 16 + 16 - 12, "ENTER PASSWORD", 0, 2 + 8 + 16);
                x = G_EnterText((320 >> 1), 50 + 16 + 16 + 16 + 16, buf, 19, 998);

                if (x)
                {
                    if (ud.pwlockout[0] == 0 || ud.lockout == 0)
                        strcpy(&ud.pwlockout[0], buf);
                    else if (strcmp(buf, &ud.pwlockout[0]) == 0)
                    {
                        ud.lockout = 0;
                        buf[0] = 0;

                        for (x = 0; x < g_numAnimWalls; x++)
                            if (wall[animwall[x].wallnum].picnum != W_SCREENBREAK &&
                                wall[animwall[x].wallnum].picnum != W_SCREENBREAK + 1 &&
                                wall[animwall[x].wallnum].picnum != W_SCREENBREAK + 2)
                                if (wall[animwall[x].wallnum].extra >= 0)
                                    wall[animwall[x].wallnum].picnum = wall[animwall[x].wallnum].extra;

                    }
                    g_currentMenu = 10000;
                    KB_ClearKeyDown(sc_Enter);
                    KB_ClearKeyDown(sc_kpad_Enter);
                    KB_FlushKeyboardQueue();
                }
            }
            else
            {
                if (x == 0)
                {
                    if (ud.lockout == 1)
                    {
                        if (ud.pwlockout[0] == 0)
                        {
                            ud.lockout = 0;
                            for (x = 0; x < g_numAnimWalls; x++)
                                if (wall[animwall[x].wallnum].picnum != W_SCREENBREAK &&
                                    wall[animwall[x].wallnum].picnum != W_SCREENBREAK + 1 &&
                                    wall[animwall[x].wallnum].picnum != W_SCREENBREAK + 2)
                                    if (wall[animwall[x].wallnum].extra >= 0)
                                        wall[animwall[x].wallnum].picnum = wall[animwall[x].wallnum].extra;
                        }
                        else
                        {
                            buf[0] = 0;
                            g_currentMenu = 10001;
                            inputloc = 0;
                            KB_FlushKeyboardQueue();
                        }
                    }
                    else
                    {
                        ud.lockout = 1;

                        for (x = 0; x < g_numAnimWalls; x++)
                            switch (tileGetMapping(wall[animwall[x].wallnum].picnum))
                            {
                            case FEMPIC1__:
                                wall[animwall[x].wallnum].picnum = BLANKSCREEN;
                                break;
                            case FEMPIC2__:
                            case FEMPIC3__:
                                wall[animwall[x].wallnum].picnum = SCREENBREAK6;
                                break;
                            }
                    }
                }

                else if (x == 1)
                {
                    g_currentMenu = 10001;
                    inputloc = 0;
                    KB_FlushKeyboardQueue();
                }
            }

            break;
        }

        case 1000:
        case 1001:
        case 1002:
        case 1003:
        case 1004:
        case 1005:
        case 1006:
        case 1007:
        case 1008:
        case 1009:
        {
            if (FURY)
            {
                // Drawn once by Menu_FuryLateVerifyPrompt() after scripted
                // menu background events have had a chance to clear the frame.
            }
            else
            {
                rotatesprite(160 << 16, 200 << 15, 65536L, 0, MENUSCREEN, 16, 0, 10 + 64, 0, 0, xdim - 1, ydim - 1);
                rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
                menutext(160, 24, 0, 0, "LOAD GAME");
                rotatesprite(101 << 16, 97 << 16, 65536 >> 1, 512, TILE_LOADSHOT, -32, 0, 4 + 10 + 64, 0, 0, xdim - 1, ydim - 1);

                M_DisplaySaveGameList();

                sprintf(tempbuf, "PLAYERS: %-2d                      ", savehead.numplr);
                mgametext(160, 156, tempbuf, 0, 2 + 8 + 16);

                sprintf(tempbuf, "EPISODE: %-2d / LEVEL: %-2d / SKILL: %-2d", 1 + savehead.volnum, 1 + savehead.levnum, savehead.plrskl);
                mgametext(160, 168, tempbuf, 0, 2 + 8 + 16);

                if (savehead.volnum == 0 && savehead.levnum == 7)
                    mgametext(160, 180, savehead.boardfn, 0, 2 + 8 + 16);

                mgametext(160, 90, "LOAD game:", 0, 2 + 8 + 16);
                sprintf(tempbuf, "\"%s\"", ud.savegame[g_currentMenu - 1000]);
                mgametext(160, 99, tempbuf, 0, 2 + 8 + 16);
                mgametext(160, 99 + 9, "(Y/N)", 0, 2 + 8 + 16);
            }

            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                g_lastSaveSlot = g_currentMenu - 1000;

                KB_FlushKeyboardQueue();
                KB_ClearKeysDown();
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }

                if (ud.multimode > 1)
                {
                    if (ud.gm&MODE_GAME)
                    {
                        G_LoadPlayer(-1 - g_lastSaveSlot);
                        ud.gm = MODE_GAME;
                    }
                    else
                    {
                        tempbuf[0] = PACKET_TYPE_LOAD_GAME;
                        tempbuf[1] = g_lastSaveSlot;
                        tempbuf[2] = myconnectindex;
                        TRAVERSE_CONNECT(x)
                        {
                            if (x != myconnectindex) mmulti_sendpacket(x, (unsigned char*)tempbuf, 3);
                            if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
                        }

                        Net_GetPackets();

                        G_LoadPlayer(g_lastSaveSlot);

                        multiflag = 0;
                    }
                }
                else
                {
                    c = G_LoadPlayer(g_lastSaveSlot);
                    if (c == 0)
                        ud.gm = MODE_GAME;
                }

                break;
            }

            M_Probe(186, 124 + 9, 0, 0);

            if (KB_KeyPressed(sc_N) || KB_KeyPressed(sc_Escape) || RMB)
            {
                KB_ClearKeyDown(sc_N);
                KB_ClearKeyDown(sc_Escape);
                S_PlaySound(EXITMENUSOUND);
                if (ud.gm&MODE_GAME)
                {
                    ud.gm &= ~MODE_MENU;
                    if (ud.multimode < 2 && ud.recstat != 2)
                    {
                        ready2send = 1;
                        totalclock = ototalclock;
                    }
                }
                else
                {
                    ChangeToMenu(300);
                    probey = last_threehundred;
                }
            }

            break;
        }

        case 1500:
        {
            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                KB_FlushKeyboardQueue();
                ChangeToMenu(G_HasNewGameCustomChoices() ? MENU_NEWGAMECUSTOM : MENU_EPISODE);
            }
            if (KB_KeyPressed(sc_N) || KB_KeyPressed(sc_Escape) || RMB)
            {
                KB_ClearKeyDown(sc_N);
                KB_ClearKeyDown(sc_Escape);
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }
                ud.gm &= ~MODE_MENU;
                S_PlaySound(EXITMENUSOUND);
                break;
            }
            M_Probe(186, 124, 0, 0);
            if (FURY)
            {
                // Drawn once by Menu_FuryLateVerifyPrompt() after the frame
                // has been refreshed; drawing here preserves stale menu rows.
            }
            else
            {
                mgametext(160, 90, "ABORT THIS GAME?", 0, 2 + 8 + 16);
                mgametext(160, 90 + 9, "(Y/N)", 0, 2 + 8 + 16);
            }

            break;
        }

        case MENU_RESETSTATSVERIFY:
        {
            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                KB_FlushKeyboardQueue();
                Menu_ResetStatsAndDispatchCapir();
                S_PlaySound(PISTOL_BODYHIT);
                ChangeToMenu(201);
                probey = 11;
                break;
            }

            if (KB_KeyPressed(sc_N) || KB_KeyPressed(sc_Escape) || RMB)
            {
                KB_ClearKeyDown(sc_N);
                KB_ClearKeyDown(sc_Escape);
                S_PlaySound(EXITMENUSOUND);
                ChangeToMenu(201);
                probey = 11;
                break;
            }

            M_Probe(186, 124, 0, 0);
            if (FURY)
            {
                // Drawn once by Menu_FuryLateVerifyPrompt() after the frame
                // has been refreshed; drawing here preserves stale menu rows.
            }
            else
            {
                mgametext(160, 90, "RESET STATS/ACHIEVEMENTS?", 0, 2 + 8 + 16);
                mgametext(160, 90 + 9, "(Y/N)", 0, 2 + 8 + 16);
            }

            break;
        }

        case 2000:
        case 2001:
        case 2002:
        case 2003:
        case 2004:
        case 2005:
        case 2006:
        case 2007:
        case 2008:
        case 2009:
        {

            if (FURY)
            {
                // Drawn once by Menu_FuryLateVerifyPrompt() after scripted
                // menu background events have had a chance to clear the frame.
            }
            else
            {
                rotatesprite(160 << 16, 200 << 15, 65536L, 0, MENUSCREEN, 16, 0, 10 + 64, 0, 0, xdim - 1, ydim - 1);
                rotatesprite(160 << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
                menutext(160, 24, 0, 0, "SAVE GAME");

                rotatesprite(101 << 16, 97 << 16, 65536L >> 1, 512, TILE_LOADSHOT, -32, 0, 4 + 10 + 64, 0, 0, xdim - 1, ydim - 1);
                sprintf(tempbuf, "PLAYERS: %-2d                      ", ud.multimode);
                mgametext(160, 156, tempbuf, 0, 2 + 8 + 16);

                sprintf(tempbuf, "EPISODE: %-2d / LEVEL: %-2d / SKILL: %-2d", 1 + ud.volume_number, 1 + ud.level_number, ud.player_skill);
                mgametext(160, 168, tempbuf, 0, 2 + 8 + 16);

                if (ud.volume_number == 0 && ud.level_number == 7)
                    mgametext(160, 180, boardfilename, 0, 2 + 8 + 16);

                M_DisplaySaveGameList();

                mgametext(160, 90, "OVERWRITE previous SAVED game?", 0, 2 + 8 + 16);
                mgametext(160, 90 + 9, "(Y/N)", 0, 2 + 8 + 16);
            }

            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                inputloc = strlen(&ud.savegame[g_currentMenu - 2000][0]);

                ChangeToMenu(g_currentMenu - 2000 + 360);

                KB_FlushKeyboardQueue();
                break;
            }
            if (KB_KeyPressed(sc_N) || KB_KeyPressed(sc_Escape) || RMB)
            {
                KB_ClearKeyDown(sc_N);
                KB_ClearKeyDown(sc_Escape);
                ChangeToMenu(351);
                S_PlaySound(EXITMENUSOUND);
            }

            M_Probe(186, 124, 0, 0);

            break;
        }

        case 990:
        case 991:
        case 992:
        case 993:
        case 994:
        case 995:
        case 996:
        case 997:
        case 998:
        case 999:
        {
            c = 160;
            if (!VOLUMEALL || !PLUTOPAK)
            {
                //rotatesprite(c<<16,200<<15,65536L,0,MENUSCREEN,16,0,10+64,0,0,xdim-1,ydim-1);
                rotatesprite(c << 16, 19 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
                menutext(c, 24, 0, 0, g_currentMenu >= 998 ? "ABOUT NETDUKE32" : "CREDITS");

                l = 9;
            }
            else
            {
                l = 4;
            }

            if (KB_KeyPressed(sc_LeftArrow) ||
                KB_KeyPressed(sc_kpad_4) ||
                KB_KeyPressed(sc_UpArrow) ||
                KB_KeyPressed(sc_PgUp) ||
                KB_KeyPressed(sc_kpad_8) ||
                WHEELUP)
            {
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_kpad_4);
                KB_ClearKeyDown(sc_UpArrow);
                KB_ClearKeyDown(sc_PgUp);
                KB_ClearKeyDown(sc_kpad_8);

                S_PlaySound(KICK_HIT);
                g_currentMenu--;
                if (g_currentMenu < 990) g_currentMenu = 990 + l;
            }
            else if (
                KB_KeyPressed(sc_PgDn) ||
                KB_KeyPressed(sc_Enter) ||
                KB_KeyPressed(sc_Space) ||
                KB_KeyPressed(sc_kpad_Enter) ||
                KB_KeyPressed(sc_RightArrow) ||
                KB_KeyPressed(sc_DownArrow) ||
                KB_KeyPressed(sc_kpad_2) ||
                KB_KeyPressed(sc_kpad_9) ||
                KB_KeyPressed(sc_kpad_6) ||
                LMB || WHEELDOWN)
            {
                KB_ClearKeyDown(sc_PgDn);
                KB_ClearKeyDown(sc_Enter);
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_Enter);
                KB_ClearKeyDown(sc_kpad_6);
                KB_ClearKeyDown(sc_kpad_9);
                KB_ClearKeyDown(sc_kpad_2);
                KB_ClearKeyDown(sc_DownArrow);
                KB_ClearKeyDown(sc_Space);
                S_PlaySound(KICK_HIT);
                g_currentMenu++;
                if (g_currentMenu > 990 + l) g_currentMenu = 990;
            }

            x = M_Probe(0, 0, 0, 1);

            if (x == -1)
            {
                ChangeToMenu(0);
                break;
            }

            if (!VOLUMEALL || !PLUTOPAK)
            {
                switch (g_currentMenu)
                {
                case 990:
                    mgametext(c, 40, "ORIGINAL CONCEPT", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9, "TODD REPLOGLE", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9, "ALLEN H. BLUM III", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9, "PRODUCED & DIRECTED BY", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9, "GREG MALONE", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "EXECUTIVE PRODUCER", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "GEORGE BROUSSARD", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "BUILD ENGINE", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "KEN SILVERMAN", 0, 2 + 8 + 16);
                    break;
                case 991:
                    mgametext(c, 40, "GAME PROGRAMMING", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9, "TODD REPLOGLE", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9, "3D ENGINE/TOOLS/NET", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9, "KEN SILVERMAN", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9, "NETWORK LAYER/SETUP PROGRAM", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "MARK DOCHTERMANN", 0, 2 + 8 + 16);
                    break;
                case 992:
                    mgametext(c, 40, "MAP DESIGN", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9, "ALLEN H BLUM III", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9, "RICHARD GRAY", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9, "3D MODELING", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9, "CHUCK JONES", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9, "SAPPHIRE CORPORATION", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "ARTWORK", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "DIRK JONES, STEPHEN HORNBACK", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "JAMES STOREY, DAVID DEMARET", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "DOUGLAS R WOOD", 0, 2 + 8 + 16);
                    break;
                case 993:
                    mgametext(c, 40, "SOUND ENGINE", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9, "JIM DOSE", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9, "SOUND & MUSIC DEVELOPMENT", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9, "ROBERT PRINCE", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9, "LEE JACKSON", 0, 2 + 8 + 16);

                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "VOICE TALENT", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "LANI MINELLA - VOICE PRODUCER", 0, 2 + 8 + 16);
                    mgametext(c, 40 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "JON ST. JOHN AS \"DUKE NUKEM\"", 0, 2 + 8 + 16);
                    break;
                case 994:
                    mgametext(c, 60, "GRAPHIC DESIGN", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9, "PACKAGING, MANUAL, ADS", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9 + 9, "ROBERT M. ATKINS", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9 + 9 + 9, "MICHAEL HADWIN", 0, 2 + 8 + 16);

                    mgametext(c, 60 + 9 + 9 + 9 + 9 + 9, "SPECIAL THANKS TO", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9 + 9 + 9 + 9 + 9 + 9, "STEVEN BLACKBURN, TOM HALL", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "SCOTT MILLER, JOE SIEGLER", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "TERRY NAGY, COLLEEN COMPTON", 0, 2 + 8 + 16);
                    mgametext(c, 60 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "HASH INC., FORMGEN, INC.", 0, 2 + 8 + 16);
                    break;
                case 995:
                    mgametext(c, 49, "THE 3D REALMS BETA TESTERS", 0, 2 + 8 + 16);

                    mgametext(c, 49 + 9 + 9, "NATHAN ANDERSON, WAYNE BENNER", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9, "GLENN BRENSINGER, ROB BROWN", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9 + 9, "ERIK HARRIS, KEN HECKBERT", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9 + 9 + 9, "TERRY HERRIN, GREG HIVELY", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9 + 9 + 9 + 9, "HANK LEUKART, ERIC BAKER", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "JEFF RAUSCH, KELLY ROGERS", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "MIKE DUNCAN, DOUG HOWELL", 0, 2 + 8 + 16);
                    mgametext(c, 49 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "BILL BLAIR", 0, 2 + 8 + 16);
                    break;
                case 996:
                    mgametext(c, 32, "COMPANY PRODUCT SUPPORT", 0, 2 + 8 + 16);

                    mgametext(c, 32 + 9 + 9, "THE FOLLOWING COMPANIES WERE COOL", 0, 2 + 8 + 16);
                    mgametext(c, 32 + 9 + 9 + 9, "ENOUGH TO GIVE US LOTS OF STUFF", 0, 2 + 8 + 16);
                    mgametext(c, 32 + 9 + 9 + 9 + 9, "DURING THE MAKING OF DUKE NUKEM 3D.", 0, 2 + 8 + 16);

                    mgametext(c, 32 + 9 + 9 + 9 + 9 + 9 + 9, "ALTEC LANSING MULTIMEDIA", 0, 2 + 8 + 16);
                    mgametext(c, 32 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "FOR TONS OF SPEAKERS AND THE", 0, 2 + 8 + 16);
                    mgametext(c, 32 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "THX-LICENSED SOUND SYSTEM", 0, 2 + 8 + 16);
                    mgametext(c, 32 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "FOR INFO CALL 1-800-548-0620", 0, 2 + 8 + 16);

                    mgametext(c, 32 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "CREATIVE LABS, INC.", 0, 2 + 8 + 16);

                    mgametext(c, 32 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9 + 9, "THANKS FOR THE HARDWARE, GUYS.", 0, 2 + 8 + 16);
                    break;
                case 997:
                    mgametext(c, 50, "DUKE NUKEM IS A TRADEMARK OF", 0, 2 + 8 + 16);
                    mgametext(c, 50 + 9, "3D REALMS ENTERTAINMENT", 0, 2 + 8 + 16);

                    mgametext(c, 50 + 9 + 9 + 9, "DUKE NUKEM", 0, 2 + 8 + 16);
                    mgametext(c, 50 + 9 + 9 + 9 + 9, "(C) 1996 3D REALMS ENTERTAINMENT", 0, 2 + 8 + 16);

                    if (VOLUMEONE)
                    {
                        mgametext(c, 106, "PLEASE READ LICENSE.DOC FOR SHAREWARE", 0, 2 + 8 + 16);
                        mgametext(c, 106 + 9, "DISTRIBUTION GRANTS AND RESTRICTIONS", 0, 2 + 8 + 16);
                    }

                    mgametext(c, VOLUMEONE ? 134 : 115, "MADE IN DALLAS, TEXAS USA", 0, 2 + 8 + 16);
                    break;
                case 998:
                    l = 10;
                    goto cheat_for_port_credits;
                case 999:
                    l = 10;
                    goto cheat_for_port_credits2;
                }
                break;
            }

            // Plutonium pak menus
            int32_t oy;
            switch (g_currentMenu)
            {
            case 990:
            case 991:
            case 992:
                rotatesprite(160 << 16, 200 << 15, 65536L, 0, 2504 + g_currentMenu - 990, 0, 0, 10 + 64, 0, 0, xdim - 1, ydim - 1);
                break;
            case 993:   // JBF 20031220
                rotatesprite(160 << 16, 200 << 15, 65536L, 0, MENUSCREEN, 0, 0, 10 + 64, 0, 0, xdim - 1, ydim - 1);
                rotatesprite(c << 16, 19 << 16, 65536L, 0, MENUBAR, 0, 0, 10, 0, 0, xdim - 1, ydim - 1);
                menutext(160, 24, 0, 0, "ABOUT NETDUKE32");

cheat_for_port_credits:
                if (g_scriptVersion == 13) l = (-2);
                oy = -l;
#define MENU_INCREMENT(x) (oy += x)  // maybe this should have been MENU_EXCREMENT instead

                mgametext(160, MENU_INCREMENT(38), "EDuke32 developers", 0, 2 + 8 + 16);
                creditscminitext(160, MENU_INCREMENT(10), "Richard \"TerminX\" Gobeille");
                creditscminitext(160, MENU_INCREMENT(7), "Evan \"Hendricks266\" Ramos");
                creditscminitext(160, MENU_INCREMENT(7), "Alexey \"Nuke.YKT\" Skrybykin");
                creditscminitext(160, MENU_INCREMENT(7), "Dino \"Doom64hunter\" Bollinger");

                mgametext(160, MENU_INCREMENT(9), "Retired EDuke32 developers", 0, 2 + 8 + 16);
                creditscminitext(160, MENU_INCREMENT(11), "Pierre-Loup \"Plagman\" Griffais");
                creditscminitext(160, MENU_INCREMENT(7), "Philipp \"Helixhorned\" Kutin");
                creditscminitext(160, MENU_INCREMENT(7), "Alex \"pogokeen\" Dawson");

                mgametext(160, MENU_INCREMENT(9), "NNBuild developer", 0, 2 + 8 + 16);
                creditscminitext(160, MENU_INCREMENT(11), "Jordon \"Striker\" Moss");

                mgametext(160, MENU_INCREMENT(9), "Special thanks to", 0, 2 + 8 + 16);
                creditscminitext(160, MENU_INCREMENT(10), "Jonathon \"JonoF\" Fowler");

                mgametext(160, MENU_INCREMENT(9), "Uses BUILD Engine technology by", 0, 2 + 8 + 16);
                creditscminitext(160, MENU_INCREMENT(10), "Ken \"Awesoken\" Silverman");
                break;
            case 994:
                rotatesprite(160 << 16, 200 << 15, 65536L, 0, MENUSCREEN, 0, 0, 10 + 64, 0, 0, xdim - 1, ydim - 1);
                rotatesprite(c << 16, 19 << 16, 65536L, 0, MENUBAR, 0, 0, 10, 0, 0, xdim - 1, ydim - 1);
                menutext(160, 24, 0, 0, "ABOUT NETDUKE32");

cheat_for_port_credits2:
                if (g_scriptVersion == 13) l = (-2);
                oy = -l;
                mgametext(160, MENU_INCREMENT(38), "License and Other Contributors", 0, 2 + 8 + 16);
                {
                    const char *scroller[] =
                    {
                        "This program is distributed under the terms of the",
                        "GNU General Public License version 2 as published by the",
                        "Free Software Foundation. See GNU.TXT for details.",
                        " ",
                        "Thanks to the following people for their contributions:",
                        " ",
                        "Adam Fazakerley",   // netcode stuff
                        "Alexey Lysiuk",     // "_mental_" - bugfixes, shader build step in Xcode project
                        "Barry Duncan",      // "sirlemonhead" - engine version compatibility
                        "Bioman",            // GTK work, APT repository and package upkeep
                        "Brandon Bergren",   // "Bdragon" - tiles.cfg
                        "Charlie Honig",     // "CONAN" - showview command
                        "CL102",             // "CommonLoon102" - mdposoff
                        "Dan Gaskill",       // "Danukem" - testing
                        "David Koenig",      // "Bargle" - Merged a couple of things from duke3d_w32
                        "Ed Coolidge",       // Mapster32 improvements
                        "Emile Belanger",    // "Beloko" - original Android work
                        "Fox Martins",       // "Fox666" - various patches, Polymost shading and aspect fixes
                        "Hunter_rus",        // tons of stuff
                        "James Bentler",     // Mapster32 improvements
                        "Jasper Foreman",    // netcode contributions
                        "Javier Martinez",   // "Malone3D" - EDuke 2.1.1 components
                        "Jeff Hart",         // website graphics
                        "Jonathan Strander", // "Mblackwell" - testing and feature speccing
                        "Jose del Castillo", // "Renegado" - EDuke 2.1.1 components
                        "Lachlan McDonald",  // official EDuke32 icon
                        "LeoD",              // bugfixes
                        "LSDNinja",          // OS X help and testing
                        "Marcus Herbert",    // "rhoenie" - OS X compatibility work
                        "Martijn van Antwerpen", // "LittleTijn" - patches, NetDuke32 fixes
                        "Matthew Palmer",    // "Usurper" - testing and eduke32.com domain
                        "Matt Saettler",     // "Matteus" - NAM, WWII GI, DOS EDuke 2.0 enhancements
                        "Mitchell Richters", // "mjr4077au" - audiolib ALSA improvements
                        "Ozkan Sezer",       // SDL/GTK version checking improvements
                        "Peter Green",       // "Plugwash" - dynamic remapping, custom gametypes
                        "Peter Veenstra",    // "Qbix" - port to 64-bit
                        "Robin Green",       // CON array support
                        "Ryan Gordon",       // "icculus" - icculus.org Duke3D port sound code
                        "Stephen Anthony",   // early 64-bit porting work
                        "tmyqlfpir",         // patches
                        "tueidj",            // Wii port
                        "Yoav N.",           // "NY00123" - Linux / SDL patches, engine version compatibility
                        " ",
                        "--x--",
                        " ",
                        " ",
                        " ",
                        " ",
                        " ",
                        " "
                    };
                    const int numlines = sizeof(scroller) / sizeof(char *);
                    MENU_INCREMENT(10);
                    for (m = 0, i = ((int32_t)totalclock / 104) % numlines; m < 16; m++, i++)
                    {
                        if (i == numlines) i = 0;
                        creditscminitext(160, oy + (m * 7), scroller[i]);
                    }
                }

                creditscminitext(160, MENU_INCREMENT(120), "Visit www.eduke32.com for news and updates");
                break;
#undef MENU_INCREMENT
            }
            break;
        }

        case MENU_MAIN:
        {
            bool const hasEditor = !FURY && MapEditor_CanOpen();
            c = FURY ? FURY_TOP_MENU_X : (320>>1);
            if (!FURY)
            {
                rotatesprite(c<<16,28<<16,65536L,0,INGAMEDUKETHREEDEE,0,0,10,0,0,xdim-1,ydim-1);
                if (PLUTOPAK)   // JBF 20030804
                    rotatesprite((c+100)<<16,36<<16,65536L,0,PLUTOPAKSPRITE+2,(sintable[((int32_t)totalclock<<4)&2047]>>11),0,2+8,0,0,xdim-1,ydim-1);
            }
            int const menuY = FURY ? FURY_TOP_MENU_Y : (hasEditor ? 61 : 67);
            int const menuStep = FURY ? FURY_TOP_MENU_STEP : (hasEditor ? 15 : 16);
            int const menuItems = FURY ? 5 : (hasEditor ? 8 : 7);
            x = M_Probe(c, menuY, menuStep, menuItems);
            if (x >= 0)
            {
                if (ud.multimode > 1 && x == 0 && ud.recstat != 2)
                {
                    last_zero = 0;
                    ChangeToMenu(MENU_NETSETUP);
                }
                else
                {
                    last_zero = x;
                    switch (x)
                    {
                    case 0:
                        ChangeToMenu(G_HasNewGameCustomChoices() ? MENU_NEWGAMECUSTOM : MENU_EPISODE);
                        break;
                    case 1:
                        if (FURY)
                        {
                            last_zero = 1;
                            ChangeToMenu(MENU_LOAD);
                        }
                        else
                        {
                            KB_FlushKeyboardQueue();
                            ChangeToMenu(MENU_NETWORK);   // MULTIPLAYER (host / join)
                        }
                        break;
                    case 2:
                        ChangeToMenu(MENU_OPTIONS);
                        break;   // JBF 20031205: was 200
                    case 3:
                        ChangeToMenu(FURY ? MENU_STORY : MENU_LOAD);
                        break;
                    case 4:
                        if (FURY)
                            ChangeToMenu(MENU_QUIT);
                        else
                        {
                            KB_FlushKeyboardQueue();
                            ChangeToMenu(MENU_STORY);
                        }
                        break;
                    case 5:
                        ChangeToMenu(MENU_CREDITS);
                        break;
                    case 6:
                        if (hasEditor)
                            MapEditor_Request();
                        else
                            ChangeToMenu(MENU_QUIT);
                        break;
                    case 7:
                        ChangeToMenu(MENU_QUIT);
                        break;
                    }
                }
            }

            if (KB_KeyPressed(sc_Q)) ChangeToMenu(MENU_QUIT);

            if (x == -1 && (ud.gm&MODE_GAME || ud.recstat == 2))
            {
                ud.gm &= ~MODE_MENU;
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }
            }

            menutext(c, menuY + menuStep * 0, MENUHIGHLIGHT(0), PHX(-2), "NEW GAME");
            if (FURY)
            {
                menutext(c, menuY + menuStep * 1, MENUHIGHLIGHT(1), 0, "CONTINUE");
                menutext(c, menuY + menuStep * 2, MENUHIGHLIGHT(2), PHX(-3), "OPTIONS");
                menutext(c, menuY + menuStep * 3, MENUHIGHLIGHT(3), PHX(-4), "READ ME!");
                menutext(c, menuY + menuStep * 4, MENUHIGHLIGHT(4), PHX(-5), "QUIT");
            }
            else
            {
                menutext(c,menuY+menuStep*1,MENUHIGHLIGHT(1),0,"MULTIPLAYER");
                menutext(c,menuY+menuStep*2,MENUHIGHLIGHT(2),PHX(-3),"OPTIONS");
                menutext(c,menuY+menuStep*3,MENUHIGHLIGHT(3),PHX(-4),"LOAD GAME");

                if (!VOLUMEALL)
                    menutext(c,menuY+menuStep*4,MENUHIGHLIGHT(4),PHX(-5),"HOW TO ORDER");
                else
                    menutext(c,menuY+menuStep*4,MENUHIGHLIGHT(4),PHX(-5),"HELP");

                menutext(c,menuY+menuStep*5,MENUHIGHLIGHT(5),PHX(-6),"CREDITS");
                if (hasEditor)
                    menutext(c,menuY+menuStep*6,MENUHIGHLIGHT(6),0,"MAP EDITOR");
                int const quitRow = hasEditor ? 7 : 6;
                menutext(c,menuY+menuStep*quitRow,MENUHIGHLIGHT(quitRow),PHX(-7),"QUIT");
            }
            break;
        }

        case MENU_MAIN_INGAME:
        {
            c = FURY ? FURY_TOP_MENU_X : (320>>1);
            if (!FURY)
            {
                rotatesprite(c<<16,32<<16,65536L,0,INGAMEDUKETHREEDEE,0,0,10,0,0,xdim-1,ydim-1);
                if (PLUTOPAK)   // JBF 20030804
                    rotatesprite((c+100)<<16,36<<16,65536L,0,PLUTOPAKSPRITE+2,(sintable[((int32_t)totalclock<<4)&2047]>>11),0,2+8,0,0,xdim-1,ydim-1);
            }
            if (MapEditor_Playing())
            {
                x = M_Probe(c,88,23,2);
                if (x == 0 || x == -1)
                {
                    ud.gm &= ~MODE_MENU;
                    ready2send = 1;
                    totalclock = ototalclock;
                }
                else if (x == 1)
                    MapEditor_StopPlaytest();
                menutext(c,88,MENUHIGHLIGHT(0),0,"RESUME PLAYTEST");
                menutext(c,111,MENUHIGHLIGHT(1),0,"RETURN TO EDITOR");
                break;
            }
            int const menuY = FURY ? FURY_TOP_MENU_Y : 67;
            int const menuStep = FURY ? FURY_TOP_MENU_STEP : 16;
            int const menuItems = FURY ? 5 : 7;
            x = M_Probe(c, menuY, menuStep, menuItems);
            switch (x)
            {
            case 0:
                if (ud.multimode < 2 || ud.recstat == 2)
                    ChangeToMenu(MENU_NEWVERIFY);
                else
                {
                    ChangeToMenu(MENU_NETSETUP);
                    last_fifty = 0;
                }
                break;
            case 1:
                if (ud.recstat != 2)
                {
                    last_fifty = 1;
                    ChangeToMenu(MENU_SAVE);
                    videoSetViewableArea(0,0,xdim-1,ydim-1);
                }
                break;
            case 2:
                last_fifty = 2;
                ChangeToMenu(FURY ? MENU_OPTIONS : MENU_LOAD);
                break;
            case 3:
                last_fifty = 3;
                ChangeToMenu(FURY ? MENU_STORY : MENU_OPTIONS);     // JBF 20031205: was 200
                break;
            case 4:
                last_fifty = 4;
                ChangeToMenu(FURY ? MENU_QUITTOTITLE : MENU_STORY);
                break;
            case 5:
                if (numplayers < 2)
                {
                    last_fifty = 5;
                    ChangeToMenu(MENU_QUITTOTITLE);
                }
                break;
            case 6:
                last_fifty = 6;
                ChangeToMenu(MENU_QUIT);
                break;
            case -1:
                ud.gm &= ~MODE_MENU;
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }
                break;
            }

            if (KB_KeyPressed(sc_Q))
                ChangeToMenu(MENU_QUIT);

            menutext(c, menuY, MENUHIGHLIGHT(0), PHX(-2), "NEW GAME");
            if (FURY)
            {
                menutext(c, menuY + menuStep * 1, MENUHIGHLIGHT(1), PHX(-3), "SAVE GAME");
                menutext(c, menuY + menuStep * 2, MENUHIGHLIGHT(2), PHX(-4), "OPTIONS");
                menutext(c, menuY + menuStep * 3, MENUHIGHLIGHT(3), PHX(-5), "READ ME!");
                menutext(c, menuY + menuStep * 4, MENUHIGHLIGHT(4), PHX(-6), "QUIT");
            }
            else
            {
                menutext(c,menuY+menuStep       ,MENUHIGHLIGHT(1),PHX(-3),"SAVE GAME");
                menutext(c,menuY+menuStep+menuStep,MENUHIGHLIGHT(2),PHX(-4),"LOAD GAME");

                menutext(c,menuY+menuStep+menuStep+menuStep,MENUHIGHLIGHT(3),PHX(-5),"OPTIONS");
                if (!VOLUMEALL)
                {
                    menutext(c,menuY+menuStep+menuStep+menuStep+menuStep,MENUHIGHLIGHT(4),PHX(-6),"HOW TO ORDER");
                }
                else
                {
                    menutext(c,menuY+menuStep+menuStep+menuStep+menuStep,MENUHIGHLIGHT(4),PHX(-6)," HELP");
                }
                if (numplayers > 1)
                    menutext(c,menuY+menuStep+menuStep+menuStep+menuStep+menuStep,MENUHIGHLIGHT(5),1,"QUIT TO TITLE");
                else menutext(c,menuY+menuStep+menuStep+menuStep+menuStep+menuStep,MENUHIGHLIGHT(5),PHX(-7),"QUIT TO TITLE");
                menutext(c,menuY+menuStep+menuStep+menuStep+menuStep+menuStep+menuStep,MENUHIGHLIGHT(6),PHX(-8),"QUIT GAME");
            }
            break;
        }

        case 100:
        {
            rotatesprite(160<<16,19<<16,65536L,0,MENUBAR,16,0,10,0,0,xdim-1,ydim-1);
            menutext(160,24,0,0,"SELECT AN EPISODE");
            x = M_Probe(160,VOLUMEONE?60:60-(g_numVolumes*2),20,VOLUMEONE?3:g_numVolumes+1);
            if (x >= 0)
            {
                if (VOLUMEONE)
                {
                    if (x > 0)
                        ChangeToMenu(20000);
                    else
                    {
                        ud.m_volume_number = x;
                        ud.m_level_number = 0;
                        last_onehundred = x;
                        ChangeToMenu(110);
                    }
                }

                if (!VOLUMEONE)
                {
                    if (x == g_numVolumes /*&& boardfilename[0]*/)
                    {
                        //ud.m_volume_number = 0;
                        //ud.m_level_number = 7;
                        currentlist = 1;
                        last_onehundred = x;
                        ChangeToMenu(101);
                    }
                    else
                    {
                        ud.m_volume_number = x;
                        ud.m_level_number = 0;
                        last_onehundred = x;
                        ChangeToMenu(110);
                    }
                }
            }
            else if (x == -1)
            {
                if (ud.gm&MODE_GAME) ChangeToMenu(50);
                else ChangeToMenu(0);
            }

            c = 80;
            if (VOLUMEONE)
            {
                menutext(160,60,MENUHIGHLIGHT(0),PHX(-2),EpisodeNames[0]);
                menutext(160,60+20,MENUHIGHLIGHT(1),1,EpisodeNames[1]);
                menutext(160,60+20+20,MENUHIGHLIGHT(2),1,EpisodeNames[2]);
                if (PLUTOPAK)
                    menutext(160,60+20+20,MENUHIGHLIGHT(3),1,EpisodeNames[3]);
            }
            else
            {
                for (i=0;i<g_numVolumes;i++)
                    menutext(160,60-(g_numVolumes*2)+(20*i),MENUHIGHLIGHT(i),PHX(-2),EpisodeNames[i]);

                menutext(160,60-(g_numVolumes*2)+(20*i),MENUHIGHLIGHT(i),PHX(-6),"USER MAP");
            }
            break;
        }

        case 101:
        {
            if (boardfilename[0] == 0) strcpy(boardfilename, "./");
            Bcorrectfilename(boardfilename, 1);
            getfilenames(boardfilename, "*.map");
            ChangeToMenu(MENU_USERMAP_BROWSER);
            KB_FlushKeyboardQueue();
            fallthrough__;
        }

        case MENU_NEWGAMECUSTOM:
        case MENU_NEWGAMECUSTOMSUB:
        case MENU_NEWGAMECUSTOML3:
        {
            Menu_DisplayNewGameCustom();
            break;
        }

        case MENU_USERMAP_BROWSER:
        {
            rotatesprite(160<<16,19<<16,65536L,0,MENUBAR,16,0,10,0,0,xdim-1,ydim-1);
            menutext(160,24,0,0,"SELECT A USER MAP");

            // black translucent background underneath file lists
            Menu_BlackRectangle(36 << 16, 42 << 16, 248 << 16, 123 << 16, 1 | 32);

            // path
            minitext(38,45,boardfilename,16,26);

            {
                // JBF 20040208: seek to first name matching pressed character
                BUILDVFS_FIND_REC *seeker = currentlist ? findfiles : finddirs;
                if ((KB_KeyPressed(sc_Home)|KB_KeyPressed(sc_End)) > 0)
                {
                    while (seeker && (KB_KeyPressed(sc_End)?seeker->next:seeker->prev))
                        seeker = KB_KeyPressed(sc_End)?seeker->next:seeker->prev;
                    if (seeker)
                    {
                        if (currentlist) findfileshigh = seeker;
                        else finddirshigh = seeker;
                        S_PlaySound(KICK_HIT);
                    }
                }
                else if ((KB_KeyPressed(sc_PgUp)|KB_KeyPressed(sc_PgDn)) > 0)
                {
                    seeker = currentlist?findfileshigh:finddirshigh;
                    i = 6;
                    while (i>0)
                    {
                        if (seeker && (KB_KeyPressed(sc_PgDn)?seeker->next:seeker->prev))
                            seeker = KB_KeyPressed(sc_PgDn)?seeker->next:seeker->prev;
                        i--;
                    }
                    if (seeker)
                    {
                        if (currentlist) findfileshigh = seeker;
                        else finddirshigh = seeker;
                        S_PlaySound(KICK_HIT);
                    }
                }
                else
                {
                    char ch2, ch;
                    ch = KB_GetCh();
                    if (ch > 0 && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
                    {
                        if (ch >= 'a') ch -= ('a'-'A');
                        while (seeker)
                        {
                            ch2 = seeker->name[0];
                            if (ch2 >= 'a' && ch2 <= 'z') ch2 -= ('a'-'A');
                            if (ch2 == ch) break;
                            seeker = seeker->next;
                        }
                        if (seeker)
                        {
                            if (currentlist) findfileshigh = seeker;
                            else finddirshigh = seeker;
                            S_PlaySound(KICK_HIT);
                        }
                    }
                }
            }
            mgametext(40+4,32,"DIRECTORIES",0,2+8+16);

            if (finddirshigh)
            {
                int len;

                dir = finddirshigh;
                for (i=0; i<5; i++) if (!dir->prev) break;
                    else dir=dir->prev;
                for (i=5; i>-8 && dir; i--, dir=dir->next)
                {
                    if (dir == finddirshigh && currentlist == 0) c=0;
                    else c=16;
                    len = Bstrlen(dir->name);
                    Bstrcpy(tempbuf, dir->name);
                    if (len > USERMAPENTRYLENGTH)
                    {
                        len = USERMAPENTRYLENGTH-3;
                        tempbuf[len] = 0;
                        while (len < USERMAPENTRYLENGTH)
                            tempbuf[len++] = '.';
                    }
                    tempbuf[len] = 0;
                    minitextshade(40,1+12+32+8*(6-i),tempbuf,c,0,26);
                }
            }

            mgametext(180+4,32,"MAP FILES",0,2+8+16);

            if (findfileshigh)
            {
                int len;

                dir = findfileshigh;
                for (i=0; i<6; i++) if (!dir->prev) break;
                    else dir=dir->prev;
                for (i=6; i>-8 && dir; i--, dir=dir->next)
                {
                    if (dir == findfileshigh && currentlist == 1) c=0;
                    else c=16;
                    len = Bstrlen(dir->name);
                    Bstrcpy(tempbuf,dir->name);
                    if (len > USERMAPENTRYLENGTH)
                    {
                        len = USERMAPENTRYLENGTH-3;
                        tempbuf[len] = 0;
                        while (len < USERMAPENTRYLENGTH)
                            tempbuf[len++] = '.';
                    }
                    tempbuf[len] = 0;
                    minitextshade(180,1+12+32+8*(6-i),tempbuf,c,2,26);
                }
            }

            if (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_kpad_4) || ((buttonstat&1) && minfo.mousex < -256) ||
                    KB_KeyPressed(sc_RightArrow) || KB_KeyPressed(sc_kpad_6) || ((buttonstat&1) && minfo.mousex > 256) ||
                    KB_KeyPressed(sc_Tab))
            {
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_kpad_4);
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_6);
                KB_ClearKeyDown(sc_Tab);
                currentlist = 1-currentlist;
                S_PlaySound(KICK_HIT);
            }

            onbar = 0;
            probey = 2;
            if (currentlist == 0) x = probesm(45,32+4+1,0,3);
            else x = probesm(185,32+4+1,0,3);

            if (probey == 1)
            {
                if (currentlist == 0)
                {
                    if (finddirshigh)
                        if (finddirshigh->prev) finddirshigh = finddirshigh->prev;
                }
                else
                {
                    if (findfileshigh)
                        if (findfileshigh->prev) findfileshigh = findfileshigh->prev;
                }
            }
            else if (probey == 0)
            {
                if (currentlist == 0)
                {
                    if (finddirshigh)
                        if (finddirshigh->next) finddirshigh = finddirshigh->next;
                }
                else
                {
                    if (findfileshigh)
                        if (findfileshigh->next) findfileshigh = findfileshigh->next;
                }
            }

            if (x == -1)
            {
                clearfilenames();
                boardfilename[0] = 0;
                if (ud.multimode > 1)
                {
                    Net_SendUserMapName();
                    ChangeToMenu(600);
                    probey = last_menu_pos;
                }
                else ChangeToMenu(100);
            }
            else if (x >= 0)
            {
                if (currentlist == 0)
                {
                    if (!finddirshigh) break;
                    strcat(boardfilename, finddirshigh->name);
                    strcat(boardfilename, "/");
                    Bcorrectfilename(boardfilename, 1);
                    ChangeToMenu(101);
                    KB_FlushKeyboardQueue();
                }
                else
                {
                    if (!findfileshigh) break;
                    strcat(boardfilename, findfileshigh->name);
                    ud.m_volume_number = 0;
                    ud.m_level_number = 7;
                    if (ud.multimode > 1)
                    {
                        Net_SendUserMapName();
                        ChangeToMenu(600);
                        probey = last_menu_pos;
                    }
                    else ChangeToMenu(110);
                }
                clearfilenames();
            }
            break;
        }

        case 110:
        {
            c = FURY ? FURY_SKILL_MENU_X : (320 >> 1);
            int const skillCount = FURY ? Menu_DefinedSkillCount() : 4;
            int const skillY = FURY ? 58 + ((4 - skillCount) * 6) : 70;
            int const skillStep = FURY ? FURY_TOP_MENU_STEP : 19;

            if (!FURY)
            {
                rotatesprite(c<<16,19<<16,65536L,0,MENUBAR,16,0,10,0,0,xdim-1,ydim-1);
                menutext(c,24,0,0,"SELECT SKILL");
            }

            x = furySkillSelectionPreprocessed
                ? M_ProbeAlreadyProcessed(c, skillY, skillStep, skillCount)
                : M_Probe(c, skillY, skillStep, skillCount);
            Menu_SyncFurySkillSelection();
            if (x >= 0)
            {
                switch (x)
                {
                case 0:
                    g_skillSoundID = JIBBED_ACTOR6;
                    break;
                case 1:
                    g_skillSoundID = BONUS_SPEECH1;
                    break;
                case 2:
                    g_skillSoundID = DUKE_GETWEAPON2;
                    break;
                case 3:
                    g_skillSoundID = JIBBED_ACTOR5;
                    break;
                }

                S_PlaySound(g_skillSoundID);

                ud.m_player_skill = x+1;

                M_DMFLAGS_SET(DMFLAG_NOMONSTERS | DMFLAG_RESPAWNITEMS | DMFLAG_RESPAWNINVENTORY, false);

                ud.multimode = 1;

                if (ud.m_volume_number == 3)
                {
                    renderFlushPerms();
                    videoSetViewableArea(0,0,xdim-1,ydim-1);
                    videoClearViewableArea(0L);
                    videoNextPage();
                }

                G_NewGame(NEWGAME_RESETALL);
            }
            else if (x == -1)
            {
                ChangeToMenu(FURY ? last_skill_parent_menu : MENU_EPISODE);
                KB_FlushKeyboardQueue();
            }

            for (i = 0; i < skillCount; ++i)
            {
                if (FURY)
                    Menu_DrawLeftMenuText(c, skillY + skillStep * i, MENUHIGHLIGHT(i), PHX(-2 - i), g_skillNames[i]);
                else
                    menutext(c, skillY + skillStep * i, MENUHIGHLIGHT(i), PHX(-2 - i), g_skillNames[i]);
            }
            break;
        }

        case 230:
        {
#if defined(USE_OPENGL)
            Menu_DrawTopBarTitle(19, "RENDERER SETUP", 24);

            c = (320>>1)-120;

            {
                int io, ii, yy, d=c+160+40, enabled;
                const char *opts[] =
                {
                    "Anisotropic filtering",
                    "Use VSync",
                    "Ambient light level",
                    "-",
                    "Palette Emulation",
                    "Palette Shade Interpolation",
                    "Y-Shearing",
                    "-",
                    "Enable hires textures",
                    "Hires texture quality",
                    "Precache hires textures",
                    "GL texture compression",
                    "Cache textures on disk",
                    "Use detail textures",
                    "Use glow textures",
                    "-",
                    "Use models",
                    "Blend model animations",
                    NULL
                };

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        if (io <= probey) yy += 4;
                        continue;
                    }
                    if (io < probey) yy += 8;
                    io++;
                }

                onbar = (probey==2);
                x = probesm(c,yy+5,0,io);

                if (x == -1)
                {
                    ChangeToMenu(203);
                    probey = 6;
                    break;
                }

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        yy += 4;
                        continue;
                    }
                    enabled = 1;
                    switch (io)
                    {
                    case 0:
                    {
                        int dummy = glanisotropy;
                        modval(0,(int)glinfo.maxanisotropy+1,(int *)&dummy,1,probey==io);
                        if (dummy > glanisotropy) glanisotropy *= 2;
                        else if (dummy < glanisotropy) glanisotropy /= 2;
                        if (x==io)
                            glanisotropy *= 2;
                        if (glanisotropy > glinfo.maxanisotropy) glanisotropy = 1;
                        else if (glanisotropy < 1) glanisotropy = (int)glinfo.maxanisotropy;
                        gltexapplyprops();
                        if (glanisotropy == 1) strcpy(tempbuf,"NONE");
                        else sprintf(tempbuf,"%dx",glanisotropy);
                        mgametextpal(d,yy, tempbuf, MENUHIGHLIGHT(io), 0);
                        break;
                    }
                    case 1:
                    {
                        int ovsync = vsync;
                        if (x==io) vsync = !vsync;
                        modval(0,1,(int *)&vsync,1,probey==io);
                        mgametextpal(d,yy, vsync? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        if (vsync != ovsync)
                            videoSetVsync(vsync);
                        break;
                    }
                    case 2:
                    {
                        int i = (int)(r_ambientlight*1024.f);
                        int j = i;
                        _bar(1,d+8,yy+7, &i,128,x==io,MENUHIGHLIGHT(io),0,128,4096);
                        Bsprintf(tempbuf,"%.2f",r_ambientlight);
                        mgametextpal(d-35,yy, tempbuf, MENUHIGHLIGHT(io), 0);
                        if (i != j)
                        {
                            r_ambientlight = (float)i/1024.f;
                            r_ambientlightrecip = 1.f/r_ambientlight;
                        }
                        break;
                    }
                    case 3:
                    {
                        int i = r_usetileshades;

                        if (x == io)
                            r_usetileshades = 1 - r_usetileshades;

                        modval(0, 1, (int*)&r_usetileshades, 1, probey == io);
                        mgametextpal(d, yy, r_usetileshades ? "Yes" : "No", MENUHIGHLIGHT(io), 0);

                        if (r_usetileshades != i)
                        {
                            r_useindexedcolortextures = r_usetileshades;
                            if (r_usetileshades)
                            {
                                gltexfiltermode = 0; // Force Nearest
                                gltexapplyprops();
                            }
                        }
                        break;
                    }
                    case 4:
                        enabled = r_usetileshades;
                        if (enabled && x==io) r_shadeinterpolate = !r_shadeinterpolate;
                        if (enabled) modval(0,1,(int *)&r_shadeinterpolate,1,probey==io);
                        mgametextpal(d,yy, r_shadeinterpolate ? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 0);
                        break;
                    case 5:
                        if (x == io)
                            r_yshearing = 1 - r_yshearing;

                        modval(0, 1, (int*)&r_yshearing, 1, probey == io);
                        mgametextpal(d, yy, r_yshearing ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 6:
                        if (x==io) usehightile = 1-usehightile;
                        modval(0,1,(int *)&usehightile,1,probey==io);
                        mgametextpal(d,yy, usehightile ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 7: 
                    {
                        i = r_downsize;
                        enabled = usehightile;

                        static char const* qualityopts[] = { "Full", "Half", "Barf", };

                        if (enabled) modval(0, 2, (int*)&r_downsize, 1, probey == io);
                        mgametextpal(d, yy, qualityopts[r_downsize], enabled ? MENUHIGHLIGHT(io) : DISABLEDMENUSHADE, 0);

                        if (r_downsize != i)
                        {
                            texcache_invalidate();
                            r_downsizevar = r_downsize;
                            videoResetMode();
                            if (videoSetGameMode(fullscreen,xdim,ydim,bpp, ud.detail))
                                OSD_Printf("restartvid: Reset failed...\n");
                            return;
                        }

                        break;
                    }
                    case 8:
                        enabled = usehightile;
                        if (enabled && x==io) ud.config.useprecache = !ud.config.useprecache;
                        if (enabled) modval(0,1,(int *)&ud.config.useprecache,1,probey==io);
                        mgametextpal(d,yy, ud.config.useprecache ? "On" : "Off", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 0);
                        break;
                    case 9:
                    {
                        i = glusetexcompr;
                        enabled = usehightile;
                        if (enabled && x == io) glusetexcompr = !glusetexcompr;
                        if (enabled) modval(0, 2, (int*)&glusetexcompr, 1, probey == io);

                        const char* texcompressionopts[] = { "No", "Hi-res Only", "Hi-res & 8-bit" };

                        mgametextpal(d, yy, texcompressionopts[glusetexcompr], enabled ? MENUHIGHLIGHT(io) : DISABLEDMENUSHADE, 0);

                        if (glusetexcompr != i)
                        {
                            texcache_invalidate();
                            videoResetMode();
                            if (videoSetGameMode(fullscreen, xdim, ydim, bpp, ud.detail))
                                OSD_Printf("restartvid: Reset failed...\n");
                            return;
                        }
                        break;
                    }
                    case 10:
                    {
                        i = glusetexcache;
                        enabled = (glusetexcompr && usehightile);
                        if (enabled && x == io) glusetexcache = !glusetexcache;
                        if (enabled) modval(0, 2, (int*)&glusetexcache, 1, probey == io);

                        const char* texcacheopts[] = { "No", "Yes", "Compressed" };

                        mgametextpal(d, yy, texcacheopts[glusetexcache], enabled ? MENUHIGHLIGHT(io) : DISABLEDMENUSHADE, 0);

                        if (glusetexcache != i)
                        {
                            texcache_invalidate();
                            videoResetMode();
                            if (videoSetGameMode(fullscreen, xdim, ydim, bpp, ud.detail))
                                OSD_Printf("restartvid: Reset failed...\n");
                            return;
                        }
                        break;
                    }
                    case 11:
                        enabled = usehightile;
                        if (enabled && x==io) r_detailmapping = !r_detailmapping;
                        if (enabled) modval(0,1,(int *)&r_detailmapping,1,probey==io);
                        mgametextpal(d,yy, r_detailmapping ? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 0);
                        break;
                    case 12:
                        enabled = usehightile;
                        if (enabled && x==io) r_glowmapping = !r_glowmapping;
                        if (enabled) modval(0,1,(int *)&r_glowmapping,1,probey==io);
                        mgametextpal(d,yy, r_glowmapping ? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 0);
                        break;
                    case 13:
                        if (x==io) usemodels = 1-usemodels;
                        modval(0,1,(int *)&usemodels,1,probey==io);
                        mgametextpal(d,yy, usemodels ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 14:
                        enabled = usemodels;
                        if (enabled && x==io) r_animsmoothing = !r_animsmoothing;
                        if (enabled) modval(0,1,(int *)&r_animsmoothing,1,probey==io);
                        mgametextpal(d,yy, r_animsmoothing ? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 0);
                        break;
                    default:
                        break;
                    }
                    mgametextpal(c,yy, opts[ii], enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 10);
                    io++;
                    yy += 8;
                }
            }
#endif
            break;
        }

        case 231:
        case 232:
        {
            Menu_DrawTopBarTitle(19, "COLOR CORRECTION", 24);

            c = (320>>1)-120;

            x = 4;

            onbar = (probey != 3);
            x = M_Probe(c,probey==3?106:98,16,x);

            if (x == -1)
            {
                if (ud.gm&MODE_GAME && g_currentMenu == 232)
                {
                    ud.gm &= ~MODE_MENU;
                    if (ud.multimode < 2  && ud.recstat != 2)
                    {
                        ready2send = 1;
                        totalclock = ototalclock;
                    }
                }
                else
                {
                    ChangeToMenu(203);
                    probey = 4;
                }
                break;
            }

            menutext(c,98,MENUHIGHLIGHT(0),0,"GAMMA");
            menutext(c,98+16,MENUHIGHLIGHT(1),0,"CONTRAST");
            menutext(c,98+16+16,MENUHIGHLIGHT(2),0,"SATURATION");
            menutext(c,98+16+16+16+8,MENUHIGHLIGHT(3),0,"RESET TO DEFAULTS");

            Bsprintf(tempbuf,"%s%.2f",g_videoGamma>=0?" ":"",g_videoGamma);
            mgametext(c+177-56,98-8,tempbuf,MENUHIGHLIGHT(0),2+8+16);
            Bsprintf(tempbuf,"%s%.2f",g_videoContrast>=0?" ":"",g_videoContrast);
            mgametext(c+177-56,98+16-8,tempbuf,MENUHIGHLIGHT(1),2+8+16);
            Bsprintf(tempbuf,"%s%.2f",g_videoSaturation>=0?" ":"", g_videoSaturation);
            mgametext(c+177-56,98+16+16-8,tempbuf,MENUHIGHLIGHT(2),2+8+16);

            rotatesprite(40<<16,24<<16,24576,0,BONUSSCREEN,0,0,2+8+16,0,scale(ydim,35,200),xdim-1,scale(ydim,80,200)-1);
            rotatesprite(160<<16,27<<16,24576,0,3290,0,0,2+8+16,0,scale(ydim,35,200),xdim-1,scale(ydim,80,200)-1);

            {
                int b = (int)(g_videoGamma*40960.f);
                _bar(0,c+177,98,&b,2048,x==0,MENUHIGHLIGHT(0),0, (int)(MIN_GAMMA * 40960.f), (int)(MAX_GAMMA * 40960.f));

                if (b != (double)(g_videoGamma*40960.f))
                {
                    g_videoGamma = (float)b/40960.f;
                    ud.brightness = (int)(min(max((float)((g_videoGamma-1.0)*10.0),0.0f),15.0f));
                    ud.brightness <<= 2;
                    videoSetPalette(ud.brightness>>2, g_player[myconnectindex].ps->palette,0);
                }

                b = (int)(g_videoContrast*40960.f);
                _bar(0,c+177,98+16,&b,2048,x==1,MENUHIGHLIGHT(1),0, (int)(MIN_CONTRAST * 40960.f), (int)(MAX_CONTRAST * 40960.f));

                if (b != (g_videoContrast*40960.f))
                {
                    g_videoContrast = (float)b/40960.f;
                    videoSetPalette(ud.brightness>>2, g_player[myconnectindex].ps->palette,0);
                }

                b = (int)(g_videoSaturation*40960.f);
                _bar(0,c+177,98+16+16,&b,2048,x==2,MENUHIGHLIGHT(2),0, (int)(MIN_SATURATION * 40960.f), (int)(MAX_SATURATION * 40960.f));

                if (b != (g_videoSaturation*40960.f))
                {
                    g_videoSaturation = (float)b/40960.f;
                    videoSetPalette(ud.brightness>>2, g_player[myconnectindex].ps->palette,0);
                }

                if (x == 3)
                {
                    g_videoGamma = DEFAULT_GAMMA;
                    g_videoContrast = DEFAULT_CONTRAST;
                    g_videoSaturation = DEFAULT_SATURATION;
                    ud.brightness = 0;
                    videoSetPalette(ud.brightness>>2, g_player[myconnectindex].ps->palette,0);
                }
            }

            break;
        }

        case 200:
        {
            Menu_DrawTopBarTitle(19, "GAME SETUP", 24);

            c = (320>>1)-120;

            {
                int io, ii, yy, d=c+160+40, enabled;
                const char *opts[] =
                {
                    "Show setup window at start",
                    "Show crosshair",
                    "Crosshair size",
                    "-",
                    "Screen size",
                    "Status bar size",
                    "Stats and chat text size",
                    "Show level stats",
                    "-",
                    "Allow walk with autorun",
                    "-",
                    "Shadows",
                    "Screen tilting",
                    "-",
                    "Show framerate",
                    "Demo recording",
                    "-",
                    "More...",
                    NULL
                };

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        if (io <= probey) yy += 4;
                        continue;
                    }
                    if (io < probey) yy += 8;
                    io++;
                }

                onbar = (probey >= 2 && probey <= 5);
                x = probesm(c,yy+5,0,io);

                if (x == -1)
                {
                    ChangeToMenu(202);
                    break;
                }

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        yy += 4;
                        continue;
                    }
                    enabled = 1;
                    switch (io)
                    {
                    case 0:
                        if (x==io) ud.config.ForceSetup = 1-ud.config.ForceSetup;
                        modval(0,1,(int *)&ud.config.ForceSetup,1,probey==io);
                        mgametextpal(d,yy, ud.config.ForceSetup ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 1:
                        if (x==io) ud.crosshair = !ud.crosshair;
                        modval(0,1,(int *)&ud.crosshair,1,probey==io);
                        {
                            mgametextpal(d,yy,ud.crosshair?"Yes":"No", MENUHIGHLIGHT(io), 0);
                            break;
                        }
                    case 2:
                    {
                        int sbs = ud.crosshairscale;
                        _bar(1,d+8,yy+7, &sbs,5,x==io,MENUHIGHLIGHT(io),0,25,100);
                        ud.crosshairscale = min(100,max(10,sbs));
                    }
                    break;
                    case 3:
                    {
                        int i;
                        i = ud.screen_size;
                        barsm(d+8,yy+7, &ud.screen_size,-4,x==io,MENUHIGHLIGHT(io),PHX(-5));
                        if (videoGetRenderMode() >= 3 && i < ud.screen_size && i == 8 && ud.statusbarmode == 1)
                        {
                            ud.statusbarmode = 0;
                            if (ud.statusbarscale != 100)
                                ud.screen_size = i;
                        }
                        else if (videoGetRenderMode() >= 3 && i > ud.screen_size && i == 8 && ud.statusbarmode == 0)
                        {
                            if (ud.statusbarscale != 100)
                            {
                                ud.statusbarmode = 1;
                                ud.screen_size = i;
                            }
                        }
                    }
                    break;
                    case 4:
                    {
                        int sbs, sbsl;
                        sbs = sbsl = ud.statusbarscale-37;
                        barsm(d+8,yy+7, &sbs,4,x==io,MENUHIGHLIGHT(io),0);
                        if (x == io && sbs != sbsl)
                        {
                            sbs += 37;
                            G_SetStatusBarScale(sbs);
                        }
                    }
                    break;
                    case 5:
                    {
                        _bar(1,d+8,yy+7, &ud.textscale,50,enabled && x==io,MENUHIGHLIGHT(io),0,100,400);
                    }
                    break;
                    case 6:
                        if (x==io) ud.levelstats = 1-ud.levelstats;
                        modval(0,1,(int *)&ud.levelstats,1,probey==io);
                        mgametextpal(d,yy, ud.levelstats ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 7:
                        if (x==io) ud.runkey_mode = 1-ud.runkey_mode;
                        modval(0,1,(int *)&ud.runkey_mode,1,probey==io);
                        mgametextpal(d,yy, ud.runkey_mode ? "No" : "Yes", MENUHIGHLIGHT(io), 0);
                        break;
                    case 8:
                        if (x==io) ud.shadows = 1-ud.shadows;
                        modval(0,1,(int *)&ud.shadows,1,probey==io);
                        mgametextpal(d,yy, ud.shadows ? "On" : "Off", MENUHIGHLIGHT(io), 0);
                        break;
                    case 9:
                        if (x==io) ud.screen_tilting = 1-ud.screen_tilting;
#ifdef USE_OPENGL
                        if (!ud.screen_tilting) renderSetRollAngle(0);
#endif
                        modval(0,1,(int *)&ud.screen_tilting,1,probey==io);
                        mgametextpal(d,yy, ud.screen_tilting ? "On" : "Off", MENUHIGHLIGHT(io), 0);
                        break;  // original had a 'full' option
                    case 10:
                        if (x==io) ud.showfps = 1-ud.showfps;
                        modval(0,1,(int *)&ud.showfps,1,probey==io);
                        mgametextpal(d,yy, ud.showfps ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 11:
                        if (x==io)
                        {
                            enabled = !((ud.gm&MODE_GAME) && ud.m_recstat != 1);
                            if ((ud.gm&MODE_GAME)) G_CloseDemoWrite();
                            else ud.m_recstat = !ud.m_recstat;
                        }
                        if ((ud.gm&MODE_GAME) && ud.m_recstat != 1)
                            enabled = 0;
                        mgametextpal(d,yy,ud.m_recstat?((ud.m_recstat && enabled && ud.gm&MODE_GAME)?"Running":"On"):"Off",enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE,enabled?0:1);
                        break;
                    case 12:
                        if (x==io) ChangeToMenu(201);
                        break;
                    default:
                        break;
                    }
                    mgametextpal(c,yy, opts[ii], enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 10);
                    io++;
                    yy += 8;
                }
            }
            break;
        }

        case 201:
        {
            Menu_DrawTopBarTitle(19, "GAME SETUP", 24);

            c = (320>>1)-120;

            {
                int io, ii, yy, d=c+160+40, enabled;
                const char *opts[] =
                {
                    "Parental lock",
                    "-",
                    "Show inv & pickup messages",
                    "HUD weapon display",
                    "Use alternate HUD",
                    "Use camera view in demos",
                    "-",
                    "DM: Ignore map votes",
                    "DM: Use private messages",
                    "DM: Show player names",
                    "DM: Show player weapons",
                    "DM: Show player obituaries",
                    "-",
                    "Console text style",
                    "-",
                    "Reset stats/achievements",
                    "-",
                    "-",
                    "Previous page",
                    NULL
                };

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        if (io <= probey) yy += 4;
                        continue;
                    }
                    if (io < probey) yy += 8;
                    io++;
                }

                x = probesm(c,yy+5,0,io);

                if (x == -1)
                {
                    ChangeToMenu(200);
                    probey = 12;
                    break;
                }

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        yy += 4;
                        continue;
                    }
                    enabled = 1;
                    switch (io)
                    {
                    case 0:
                        if (!NAM)
                        {
                            if (x==io) ChangeToMenu(10000);
                        }
                        else enabled = 0;
                        break;
                    case 1:
                        if (x==io)
                            ud.fta_on = 1-ud.fta_on;
                        modval(0,1,(int *)&ud.fta_on,1,probey==io);
                        mgametextpal(d,yy, ud.fta_on ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 2:
                        if (x==io)
                        {
                            ud.drawweapon = (ud.drawweapon == 2) ? 0 : ud.drawweapon+1;
                        }
                        modval(0,2,(int *)&ud.drawweapon,1,probey==io);
                        {
                            const char *s[] = { "Off", "Normal", "Icon only" };
                            mgametextpal(d,yy, s[ud.drawweapon], MENUHIGHLIGHT(io), 0);
                            break;
                        }
                    case 3:
                        if (x==io) ud.althud = 1-ud.althud;
                        modval(0,1,(int *)&ud.althud,1,probey==io);
                        mgametextpal(d,yy, ud.althud ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 4:
                        if (x==io) ud.democams = 1-ud.democams;
                        modval(0,1,(int *)&ud.democams,1,probey==io);
                        mgametextpal(d,yy, ud.democams ? "On" : "Off", MENUHIGHLIGHT(io), 0);
                        break;
                    case 5:
                        if (x==io)
                        {
                            ud.autovote = (ud.autovote == 2) ? 0 : ud.autovote+1;
                        }
                        modval(0,2,(int *)&ud.autovote,1,probey==io);
                        {
                            const char *s[] = { "Off", "Vote No", "Vote Yes" };
                            mgametextpal(d,yy, s[ud.autovote], MENUHIGHLIGHT(io), 0);
                            break;
                        }
                    case 6:
                        if (x==io) ud.automsg = 1-ud.automsg;
                        modval(0,1,(int *)&ud.automsg,1,probey==io);
                        mgametextpal(d,yy, ud.automsg ? "Off" : "On", MENUHIGHLIGHT(io), 0);
                        break;
                    case 7:
                        if (x==io) ud.idplayers = 1-ud.idplayers;
                        modval(0,1,(int *)&ud.idplayers,1,probey==io);
                        mgametextpal(d,yy, ud.idplayers ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 8:
                        if (x==io) ud.showweapons = 1-ud.showweapons;
                        modval(0,1,(int *)&ud.showweapons,1,probey==io);
                        mgametextpal(d,yy, ud.showweapons ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 9:
                        if (x==io) ud.obituaries = 1-ud.obituaries;
                        modval(0,1,(int *)&ud.obituaries,1,probey==io);
                        mgametextpal(d,yy, ud.obituaries ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                        break;
                    case 10:
                    {
                        int osdmode = OSD_GetTextMode();
                        if (x==io) osdmode = !osdmode;
                        modval(0,1,(int *)&osdmode,1,probey==io);
                        mgametextpal(d,yy, osdmode? "Plain" : "Sprites", MENUHIGHLIGHT(io), 0);
                        if (OSD_GetTextMode() != osdmode)
                            OSD_SetTextMode(osdmode);
                        break;
                    }

                    case 11:
                        enabled = Menu_CapirResetAvailable();
                        if (enabled && x==io)
                            ChangeToMenu(MENU_RESETSTATSVERIFY);
                        break;

                    case 12:
                        if (x==io) ChangeToMenu(200);
                        break;
                    default:
                        break;
                    }
                    mgametextpal(c,yy, opts[ii], enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, 10);
                    io++;
                    yy += 8;
                }
            }
            break;
        }

        // JBF 20031205: Second level options menu selection
        case 202:
        {
            Menu_DrawTopBarTitle(19, "OPTIONS", 24);

            c = 50;

            onbar = 0;
            x = M_Probe(160,c,18,7);

            last_twoohtwo = probey;

            switch (x)
            {
            case -1:
                if (ud.gm&MODE_GAME) ChangeToMenu(50);
                else ChangeToMenu(0);
                break;

            case 0:
                ChangeToMenu(200);
                break;

            case 1:
                changesmade = 0;
                ChangeToMenu(700);
                break;

            case 2:
            {
                int dax = xdim, day = ydim, daz;
                curvidmode = newvidmode = videoCheckMode(&dax,&day,bpp,fullscreen,0);
                if (newvidmode == 0x7fffffffl) newvidmode = validmodecnt;
                newfullscreen = fullscreen;
                changesmade = 0;

                dax = 0;
                for (day = 0; day < validmodecnt; day++)
                {
                    if (dax == sizeof(vidsets)/sizeof(vidsets[1])) break;
                    for (daz = 0; daz < dax; daz++)
                        if ((validmode[day].bpp|((validmode[day].fs&1)<<16)) == (vidsets[daz]&0x1ffffl)) break;
                    if (vidsets[daz] != -1) continue;
                    if (validmode[day].bpp == 8)
                    {
                        vidsets[dax++] = 8|((validmode[day].fs&1)<<16);
    //                        8-bit Polymost can diaf
    //                        vidsets[dax++] = 0x20000|8|((validmode[day].fs&1)<<16);
                    }
                    else
                        vidsets[dax++] = 0x20000|validmode[day].bpp|((validmode[day].fs&1)<<16);
                }
                for (dax = 0; dax < (int)(sizeof(vidsets)/sizeof(vidsets[1])) && vidsets[dax] != -1; dax++)
                    if (vidsets[dax] == (((videoGetRenderMode()>=2)<<17)|(fullscreen<<16)|bpp)) break;
                if (dax < (int)(sizeof(vidsets)/sizeof(vidsets[1]))) newvidset = dax;
                curvidset = newvidset;

                ChangeToMenu(203);
            }
            break;
            case 3:
                if (ud.recstat != 1)
                    ChangeToMenu(20002);
                break;
            case 4:
                currentlist = 0;
                fallthrough__;
            case 5:
            case 6:
                if (x==6 && !CONTROL_JoyPresent) break;
                ChangeToMenu(204+x-4);
                break;
            }

            menutext(160,c,                  MENUHIGHLIGHT(0),0,"GAME SETUP");
            menutext(160,c+18,               MENUHIGHLIGHT(1),0,"SOUND SETUP");
            menutext(160,c+18+18,            MENUHIGHLIGHT(2),0,"VIDEO SETUP");
            menutext(160,c+18+18+18,         MENUHIGHLIGHT(3),ud.recstat == 1,"PLAYER SETUP");
            menutext(160,c+18+18+18+18,      MENUHIGHLIGHT(4),0,"KEYBOARD SETUP");
            menutext(160,c+18+18+18+18+18,   MENUHIGHLIGHT(5),0,"MOUSE SETUP");
            menutext(160,c+18+18+18+18+18+18,MENUHIGHLIGHT(6),CONTROL_JoyPresent==0,"JOYSTICK SETUP");
            break;
        }

        // JBF 20031206: Video settings menu
        case 203:
        {
            Menu_DrawTopBarTitle(19, "VIDEO SETUP", 24);

            c = (320>>1)-120;

#if defined(USE_OPENGL)
            x = (7/*+(videoGetRenderMode() >= 3)*/);
#else
            x = 7;
#endif
            onbar = (!videoGetRenderMode() && probey == 6); // (probey == 4);
            if (probey == 0 || probey == 1 || probey == 2)
                x = M_Probe(c,50,16,x);
            else if (probey == 3)
                x = M_Probe(c,50+16+16+22,0,x);
            else
                x = M_Probe(c,50+62-16-16-16,16,x);

            if ((probey==0 || probey==1 || probey==2) && (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_RightArrow)))
            {
                S_PlaySound(PISTOL_BODYHIT);
                x=probey;
            }

            switch (x)
            {
            case -1:
                ChangeToMenu(202);
                probey = 2;
                break;

            case 0:
                do
                {
                    if (KB_KeyPressed(sc_LeftArrow))
                    {
                        newvidmode--;
                        if (newvidmode < 0) newvidmode = validmodecnt-1;
                    }
                    else
                    {
                        newvidmode++;
                        if (newvidmode >= validmodecnt) newvidmode = 0;
                    }
                }
                while ((validmode[newvidmode].fs&1) != ((vidsets[newvidset]>>16)&1) || validmode[newvidmode].bpp != (vidsets[newvidset] & 0x0ffff));
                //OSD_Printf("New mode is %dx%dx%d-%d %d\n",validmode[newvidmode].xdim,validmode[newvidmode].ydim,validmode[newvidmode].bpp,validmode[newvidmode].fs,newvidmode);
                if ((curvidmode == 0x7fffffffl && newvidmode == validmodecnt) || curvidmode == newvidmode)
                    changesmade &= ~1;
                else
                    changesmade |= 1;
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_RightArrow);
                break;

            case 1:
            {
                int lastvidset, lastvidmode, safevidmode = -1;
                lastvidset = newvidset;
                lastvidmode = newvidmode;
                // find the next vidset compatible with the current fullscreen setting
                while (vidsets[0] != -1)
                {
                    newvidset++;
                    if (newvidset == sizeof(vidsets)/sizeof(vidsets[0]) || vidsets[newvidset] == -1)
                    {
                        newvidset = -1;
                        continue;
                    }
                    if (((vidsets[newvidset]>>16)&1) != newfullscreen) continue;
                    break;
                }

                if ((vidsets[newvidset] & 0x0ffff) != (vidsets[lastvidset] & 0x0ffff))
                {
                    // adjust the video mode to something legal for the new vidset
                    do
                    {
                        newvidmode++;
                        if (newvidmode == lastvidmode) break;   // end of cycle
                        if (newvidmode >= validmodecnt)
                        {
                            if (safevidmode != -1)
                                break;
                            newvidmode = 0;
                        }
                        if (validmode[newvidmode].bpp == (vidsets[newvidset]&0x0ffff) &&
                                validmode[newvidmode].fs == newfullscreen &&
                                validmode[newvidmode].xdim <= validmode[lastvidmode].xdim &&
                                (safevidmode==-1?1:(validmode[newvidmode].xdim>=validmode[safevidmode].xdim)) &&
                                validmode[newvidmode].ydim <= validmode[lastvidmode].ydim &&
                                (safevidmode==-1?1:(validmode[newvidmode].ydim>=validmode[safevidmode].ydim))
                           )
                            safevidmode = newvidmode;
                    }
                    while (1);
                    if (safevidmode == -1)
                    {
                        //OSD_Printf("No best fit!\n");
                        newvidmode = lastvidmode;
                        newvidset = lastvidset;
                    }
                    else
                    {
                        //OSD_Printf("Best fit is %dx%dx%d-%d %d\n",validmode[safevidmode].xdim,validmode[safevidmode].ydim,validmode[safevidmode].bpp,validmode[safevidmode].fs,safevidmode);
                        newvidmode = safevidmode;
                    }
                }
                if (newvidset != curvidset) changesmade |= 4;
                else changesmade &= ~4;
                if (newvidmode != curvidmode) changesmade |= 1;
                else changesmade &= ~1;
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_RightArrow);
            }
            break;

            case 2:
                newfullscreen = !newfullscreen;
                {
                    int lastvidset, lastvidmode, safevidmode = -1, safevidset = -1;
                    lastvidset = newvidset;
                    lastvidmode = newvidmode;
                    // find the next vidset compatible with the current fullscreen setting
                    while (vidsets[0] != -1)
                    {
                        newvidset++;
                        if (newvidset == lastvidset) break;
                        if (newvidset == sizeof(vidsets)/sizeof(vidsets[0]) || vidsets[newvidset] == -1)
                        {
                            newvidset = -1;
                            continue;
                        }
                        if (((vidsets[newvidset]>>16)&1) != newfullscreen) continue;
                        if ((vidsets[newvidset] & 0x2ffff) != (vidsets[lastvidset] & 0x2ffff))
                        {
                            if ((vidsets[newvidset] & 0x20000) == (vidsets[lastvidset] & 0x20000)) safevidset = newvidset;
                            continue;
                        }
                        break;
                    }
                    if (newvidset == lastvidset)
                    {
                        if (safevidset == -1)
                        {
                            newfullscreen = !newfullscreen;
                            break;
                        }
                        else
                        {
                            newvidset = safevidset;
                        }
                    }

                    // adjust the video mode to something legal for the new vidset
                    do
                    {
                        newvidmode++;

                        if (newvidmode >= validmodecnt) newvidmode = 0;

                        if (validmode[newvidmode].bpp == (vidsets[newvidset]&0x0ffff) &&
                                validmode[newvidmode].fs == newfullscreen &&
                                validmode[newvidmode].xdim <= validmode[lastvidmode].xdim &&
                                (safevidmode==-1?1:(validmode[newvidmode].xdim>=validmode[safevidmode].xdim)) &&
                                validmode[newvidmode].ydim <= validmode[lastvidmode].ydim &&
                                (safevidmode==-1?1:(validmode[newvidmode].ydim>=validmode[safevidmode].ydim))
                           )
                            safevidmode = newvidmode;

                        if (newvidmode == lastvidmode) break;   // end of cycle
                    }
                    while (1);
                    if (safevidmode == -1)
                    {
                        //OSD_Printf("No best fit!\n");
                        newvidmode = lastvidmode;
                        newvidset = lastvidset;
                        newfullscreen = !newfullscreen;
                    }
                    else
                    {
                        //OSD_Printf("Best fit is %dx%dx%d-%d %d\n",validmode[safevidmode].xdim,validmode[safevidmode].ydo,,validmode[safevidmode].bpp,validmode[safevidmode].fs,safevidmode);
                        newvidmode = safevidmode;
                    }
                    if (newvidset != curvidset) changesmade |= 4;
                    else changesmade &= ~4;
                    if (newvidmode != curvidmode) changesmade |= 1;
                    else changesmade &= ~1;
                }
                if (newfullscreen == fullscreen) changesmade &= ~2;
                else changesmade |= 2;
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_RightArrow);
                break;

            case 3:
                if (!changesmade) break;
                {
                    int pxdim, pydim, pfs, pbpp, prend;
                    int nxdim, nydim, nfs, nbpp, nrend;

                    pxdim = xdim;
                    pydim = ydim;
                    pbpp = bpp;
                    pfs = fullscreen;
                    prend = videoGetRenderMode();
                    nxdim = (newvidmode==validmodecnt)?xdim:validmode[newvidmode].xdim;
                    nydim = (newvidmode==validmodecnt)?ydim:validmode[newvidmode].ydim;
                    nfs   = newfullscreen;
                    nbpp  = (newvidmode==validmodecnt)?bpp:validmode[newvidmode].bpp;
                    nrend = (vidsets[newvidset] & 0x20000) ? (nbpp==8?2:3) : 0;

                    if (videoSetGameMode(nfs, nxdim, nydim, nbpp, ud.detail) < 0)
                    {
                        if (videoSetGameMode(pfs, pxdim, pydim, pbpp, ud.detail) < 0)
                        {
                            videoSetRenderMode(prend);
                            G_GameExit("Failed restoring old video mode.");
                        }
                        else onvideomodechange(pbpp > 8);
                    }
                    else onvideomodechange(nbpp > 8);

                    g_restorePalette = 1;
                    G_UpdateScreenArea();
                    videoSetRenderMode(nrend);

                    curvidmode = newvidmode;
                    curvidset = newvidset;
                    changesmade = 0;

                    ud.config.ScreenMode = fullscreen;
                    ud.config.ScreenWidth = xdim;
                    ud.config.ScreenHeight = ydim;
                    ud.config.ScreenBPP = bpp;
                }
                break;

            case 4:
                ChangeToMenu(231);
                break;

            case 5:
                if (!videoGetRenderMode())
                {
                    ud.detail = 1-ud.detail;
                    break;
                }
#if defined(USE_OPENGL)
                if (!r_usetileshades)
                {
                    gltexfiltermode++;
                    if (gltexfiltermode > 5)
                        gltexfiltermode = 0;
                    gltexapplyprops();
                }
                break;
            case 6:
                if (!videoGetRenderMode()) break;
                ChangeToMenu(230);
                break;
#endif
            }

            menutext(c,50,MENUHIGHLIGHT(0),0,"RESOLUTION");
            sprintf(tempbuf,"%d x %d",
                    (newvidmode==validmodecnt)?xdim:validmode[newvidmode].xdim,
                    (newvidmode==validmodecnt)?ydim:validmode[newvidmode].ydim);
            mgametext(c+168,50-8,tempbuf,MENUHIGHLIGHT(0),2+8+16);

            menutext(c,50+16,MENUHIGHLIGHT(1),0,"RENDERER");
            Bsprintf(tempbuf,(vidsets[newvidset]&0x20000)?"%d-bit OpenGL":"Software", vidsets[newvidset]&0x0ffff);
            mgametext(c+168,50+16-8,tempbuf,MENUHIGHLIGHT(1),2+8+16);

            menutext(c,50+16+16,MENUHIGHLIGHT(2),0,"FULLSCREEN");
            menutext(c+168,50+16+16,MENUHIGHLIGHT(2),0,newfullscreen?"YES":"NO");

            menutext(c+16,50+16+16+22,MENUHIGHLIGHT(3),changesmade==0,"APPLY CHANGES");

            menutext(c,50+62+16,MENUHIGHLIGHT(4),PHX(-6),"COLOR CORRECTION");
            /*        {
                        short ss = ud.brightness;
                        bar(c+171,50+62+16,&ss,8,x==4,MENUHIGHLIGHT(4),PHX(-6));
                        if (x==4)
                        {
                            ud.brightness = ss;
                            videoSetPalette(ud.brightness>>2,&g_player[myconnectindex].ps->palette[0],0);
                        }
                    }
            */
            if (!videoGetRenderMode())
            {
                int i = (int)(r_ambientlight*1024.f);
                int j = i;
                menutext(c,50+62+16+16,MENUHIGHLIGHT(5),0,"PIXEL DOUBLING");
                menutext(c+168,50+62+16+16,MENUHIGHLIGHT(5),0,ud.detail?"OFF":"ON");
                modval(0,1,(int *)&ud.detail,1,probey==5);
                menutext(c,50+62+16+16+16,MENUHIGHLIGHT(6),PHX(-6),"AMBIENT LIGHT");
                _bar(0,c+185,50+62+16+16+16,&i,128,x==6,MENUHIGHLIGHT(6),numplayers>1,128,4096);
                Bsprintf(tempbuf,"%.2f",r_ambientlight);
                mgametextpal(c+185+9,50+62+16+16+16+4, tempbuf, MENUHIGHLIGHT(6), 0);

                if (i != j)
                {
                    r_ambientlight = (float)i/1024.f;
                    r_ambientlightrecip = 1.f/r_ambientlight;
                }
            }
#if defined(USE_OPENGL)
            else
            {
                int filter = gltexfiltermode;
                int enabled = (!r_usetileshades);
                menutext(c,50+62+16+16, enabled ? MENUHIGHLIGHT(5) : DISABLEDMENUSHADE, enabled ? 0 : 1,"TEXTURE FILTER");
                
                switch (gltexfiltermode)
                {
                case 0:
                    strcpy(tempbuf,"NEAREST");
                    break;
                case 1:
                    strcpy(tempbuf,"LINEAR");
                    break;
                case 2:
                    strcpy(tempbuf,"NEAR_MM_NEAR");
                    break;
                case 3:
                    strcpy(tempbuf,"BILINEAR");
                    break;
                case 4:
                    strcpy(tempbuf,"NEAR_MM_LIN");
                    break;
                case 5:
                    strcpy(tempbuf,"TRILINEAR");
                    break;
                default:
                    strcpy(tempbuf,"OTHER");
                    break;
                }

                if (enabled)
                {
                    modval(0, 5, (int*)&gltexfiltermode, 1, probey == 5);
                    if (gltexfiltermode != filter)
                        gltexapplyprops();
                }

                mgametextpal(c+168,50+62+16+16-8,tempbuf, enabled ? MENUHIGHLIGHT(5) : DISABLEDMENUSHADE,!videoGetRenderMode());

                menutext(c,50+62+16+16+16,MENUHIGHLIGHT(6),bpp==8,"RENDERER SETUP");
            }
    #endif
            break;
        }

        case 204:
        {
            int const keybindFuncCount = Menu_KeybindFuncCount();
            int const resetDefaultsIndex = keybindFuncCount;
            int const resetClassicIndex = keybindFuncCount + 1;

            if (probey > resetClassicIndex)
                probey = resetClassicIndex;

            Menu_DrawTopBarTitle(19, "KEYBOARD SETUP", 24);

            c = (320>>1)-120;

            onbar = 0;

            if (probey == resetDefaultsIndex)
                x = probesm(60,145,0,keybindFuncCount+2);
            else if (probey == resetClassicIndex)
                x = probesm(60,152,0,keybindFuncCount+2);
            else x = probesm(0,0,0,keybindFuncCount+2);

            if (x==-1)
            {
                ChangeToMenu(202);
                probey = 4;
            }
            else if (x == resetDefaultsIndex)
            {
                CONFIG_SetDefaultKeys(keydefaults);
                CONFIG_RepairFuryDefaults();
                break;
            }
            else if (x == resetClassicIndex)
            {
                CONFIG_SetDefaultKeys(oldkeydefaults);
                CONFIG_RepairFuryDefaults();
                break;
            }
            else if (x>=0)
            {
                function = Menu_KeybindFuncAt(probey);
                if ((unsigned)function >= NUMGAMEFUNCTIONS)
                    break;

                whichkey = currentlist;
                ChangeToMenu(210);
                KB_FlushKeyboardQueue();
                KB_ClearLastScanCode();
                break;
            }

            // the top of our list
            m = probey - 6;
            if (m < 0) m = 0;
            else if (m + 13 >= keybindFuncCount) m = keybindFuncCount-13;
            if (m < 0) m = 0;

            int const selectedFunc = Menu_KeybindFuncAt(probey);

            if (selectedFunc == gamefunc_Show_Console) currentlist = 0;
            else if (KB_KeyPressed(sc_LeftArrow) || KB_KeyPressed(sc_kpad_4) ||
                     KB_KeyPressed(sc_RightArrow) || KB_KeyPressed(sc_kpad_6) ||
                     KB_KeyPressed(sc_Tab))
            {
                currentlist ^= 1;
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_4);
                KB_ClearKeyDown(sc_kpad_6);
                KB_ClearKeyDown(sc_Tab);
                S_PlaySound(KICK_HIT);
            }
            else if (KB_KeyPressed(sc_Delete))
            {
                if ((unsigned)selectedFunc < NUMGAMEFUNCTIONS)
                {
                    char key[2];
                    key[0] = ud.config.KeyboardKeys[selectedFunc][0];
                    key[1] = ud.config.KeyboardKeys[selectedFunc][1];
                    ud.config.KeyboardKeys[selectedFunc][currentlist] = 0xff;
                    CONFIG_MapKey(selectedFunc, ud.config.KeyboardKeys[selectedFunc][0], key[0], ud.config.KeyboardKeys[selectedFunc][1], key[1]);
                }
                S_PlaySound(KICK_HIT);
                KB_ClearKeyDown(sc_Delete);
            }

            for (l=min(13,keybindFuncCount)-1; l >= 0 ; l--)
            {
                int const drawFunc = Menu_KeybindFuncAt(m+l);
                p = CONFIG_FunctionNumToName(drawFunc);
                if (!p) continue;

                strcpy(tempbuf, p);
                for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
                Menu_FuryControlText(FURY ? 58 : 70, 34+l*8,tempbuf,(m+l == probey)?0:16,FURY ? 7 : 1);

                //strcpy(tempbuf, KB_ScanCodeToString(ud.config.KeyboardKeys[m+l][0]));
                strcpy(tempbuf, keyGetName(ud.config.KeyboardKeys[drawFunc][0]));
                if (!tempbuf[0]) strcpy(tempbuf, "  -");
                Menu_FuryControlText(FURY ? 166 : 70+100, 34+l*8, tempbuf,
                                     (m+l == probey && !currentlist?0:16), FURY ? 10 : 2);

                //strcpy(tempbuf, KB_ScanCodeToString(ud.config.KeyboardKeys[m+l][1]));
                strcpy(tempbuf, keyGetName(ud.config.KeyboardKeys[drawFunc][1]));
                if (!tempbuf[0]) strcpy(tempbuf, "  -");
                Menu_FuryControlText(FURY ? 226 : 70+120+34, 34+l*8, tempbuf,
                                     (m+l == probey && currentlist?0:16), FURY ? 10 : 2);
            }

            if (FURY)
            {
                Menu_FurySmallText(160, 140, "RESET KEYS TO DEFAULTS", MENUHIGHLIGHT(resetDefaultsIndex), 10, TEXT_XCENTER);
                Menu_FurySmallText(160, 147, "SET CLASSIC KEY LAYOUT", MENUHIGHLIGHT(resetClassicIndex), 10, TEXT_XCENTER);
                Menu_FurySmallText(160, 158, "UP/DOWN = SELECT ACTION", 0, 11, TEXT_XCENTER);
                Menu_FurySmallText(160, 165, "LEFT/RIGHT = SELECT LIST", 0, 11, TEXT_XCENTER);
                Menu_FurySmallText(160, 172, "ENTER = MODIFY   DELETE = CLEAR", 0, 11, TEXT_XCENTER);
            }
            else
            {
                mgametextpal(160,140,  "RESET KEYS TO DEFAULTS",MENUHIGHLIGHT(resetDefaultsIndex),10);
                mgametextpal(160,140+7,"SET CLASSIC KEY LAYOUT",MENUHIGHLIGHT(resetClassicIndex),10);
                mgametext(160,144+9+3,"UP/DOWN = SELECT ACTION",0,2+8+16);
                mgametext(160,144+9+9+3,"LEFT/RIGHT = SELECT LIST",0,2+8+16);
                mgametext(160,144+9+9+9+3,"ENTER = MODIFY   DELETE = CLEAR",0,2+8+16);
            }

            break;
        }

        case 210:
        {
            int32_t sc;
            if (FURY)
            {
                Menu_FurySmallText(160, 90, "PRESS THE KEY TO ASSIGN AS", 0, 10, TEXT_XCENTER);
                sprintf(tempbuf,"%s FOR \"%s\"", whichkey?"SECONDARY":"PRIMARY", CONFIG_FunctionNumToName(function));
                Menu_FurySmallText(160, 98, tempbuf, 0, 10, TEXT_XCENTER);
                Menu_FurySmallText(160, 116, "PRESS \"ESCAPE\" TO CANCEL", 0, 11, TEXT_XCENTER);
            }
            else
            {
                Menu_DrawTopBarTitle(19, "KEYBOARD SETUP", 24);

                mgametext(320>>1,90,"PRESS THE KEY TO ASSIGN AS",0,2+8+16);
                sprintf(tempbuf,"%s FOR \"%s\"", whichkey?"SECONDARY":"PRIMARY", CONFIG_FunctionNumToName(function));
                mgametext(320>>1,90+9,tempbuf,0,2+8+16);
                mgametext(320>>1,90+9+9+9,"PRESS \"ESCAPE\" TO CANCEL",0,2+8+16);
            }

            sc = KB_GetLastScanCode();
            if (sc != sc_None || RMB)
            {
                if (sc == sc_Escape || RMB)
                {
                    S_PlaySound(EXITMENUSOUND);
                }
                else
                {
                    char key[2];
                    key[0] = ud.config.KeyboardKeys[function][0];
                    key[1] = ud.config.KeyboardKeys[function][1];

                    S_PlaySound(PISTOL_BODYHIT);

                    ud.config.KeyboardKeys[function][whichkey] = KB_GetLastScanCode();
                    if (function == gamefunc_Show_Console)
                        OSD_CaptureKey(KB_GetLastScanCode());
                    else
                        CONFIG_MapKey(function, ud.config.KeyboardKeys[function][0], key[0], ud.config.KeyboardKeys[function][1], key[1]);
                }

                ChangeToMenu(204);

                currentlist = whichkey;
                probey = Menu_VisibleIndexForFunc(function);

                KB_ClearKeyDown(sc);
            }

            break;
        }

        case 205:
        {
            Menu_DrawTopBarTitle(19, "MOUSE SETUP", 24);

            c = 60-4;

            onbar = (probey == (MAXMOUSEBUTTONS-2)*2+2);
            if (probey < (MAXMOUSEBUTTONS-2)*2+2)
                x = probesm(73,38,8,(MAXMOUSEBUTTONS-2)*2+2+2+2);
            else
                x = probesm(40,123-((MAXMOUSEBUTTONS-2)*2+2)*9,9,(MAXMOUSEBUTTONS-2)*2+2+2+2);

            if (x==-1)
            {
                ChangeToMenu(202);
                probey = 5;
                break;
            }
            else if (x == (MAXMOUSEBUTTONS-2)*2+2)
            {
                // sensitivity
            }
            else if (x == (MAXMOUSEBUTTONS-2)*2+2+1)
            {
                // mouse aiming toggle
                if (!ud.mouseaiming) g_myAimMode = 1-g_myAimMode;
            }
            else if (x == (MAXMOUSEBUTTONS-2)*2+2+2)
            {
                // invert mouse aim
                ud.mouseflip = 1-ud.mouseflip;
            }
            else if (x == (MAXMOUSEBUTTONS-2)*2+2+2+1)
            {
                //advanced
                ChangeToMenu(212);
                break;
            }
            else if (x >= 0)
            {
                //set an option
                ChangeToMenu(211);
                function = 0;
                whichkey = x;
                if (x < (MAXMOUSEBUTTONS-2)*2)
                    probey = ud.config.MouseFunctions[x>>1][x&1];
                else
                    probey = ud.config.MouseFunctions[x-4][0];
                if (probey < 0) probey = NUMGAMEFUNCTIONS-1;
                break;
            }

            for (l=0; l < (MAXMOUSEBUTTONS-2)*2+2; l++)
            {
                tempbuf[0] = 0;
                if (l < (MAXMOUSEBUTTONS-2)*2)
                {
                    if (l&1)
                    {
                        Bstrcpy(tempbuf, "Double ");
                        m = ud.config.MouseFunctions[l>>1][1];
                    }
                    else
                        m = ud.config.MouseFunctions[l>>1][0];
                    Bstrcat(tempbuf, mousebuttonnames[l>>1]);
                }
                else
                {
                    Bstrcpy(tempbuf, mousebuttonnames[l-(MAXMOUSEBUTTONS-2)]);
                    m = ud.config.MouseFunctions[l-(MAXMOUSEBUTTONS-2)][0];
                }

                minitextshade(c+20,34+l*8,tempbuf,(l==probey)?0:16,1,10+16);

                if (m == -1)
                    minitextshade(c+100+20,34+l*8,"  -NONE-",(l==probey)?0:16,2,10+16);
                else
                {
                    strcpy(tempbuf, CONFIG_FunctionNumToName(m));
                    for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
                    minitextshade(c+100+20,34+l*8,tempbuf,(l==probey)?0:16,2,10+16);
                }
            }

            mgametextpal(40,118,"Base mouse sensitivity",MENUHIGHLIGHT((MAXMOUSEBUTTONS-2)*2+2),10);
            mgametextpal(40,118+9,"Use mouse aiming",!ud.mouseaiming?MENUHIGHLIGHT((MAXMOUSEBUTTONS-2)*2+2+1):DISABLEDMENUSHADE,10);
            mgametextpal(40,118+9+9,"Invert mouse",MENUHIGHLIGHT((MAXMOUSEBUTTONS-2)*2+2+2),10);
            mgametextpal(40,118+9+9+9,"Advanced mouse setup",MENUHIGHLIGHT((MAXMOUSEBUTTONS-2)*2+2+2+1),10);

            {
                int sense;
                sense = (int)CONTROL_MouseSensitivity-1;
                _bar(1, 248, 126, &sense, 1, x == (MAXMOUSEBUTTONS - 2) * 2 + 2, MENUHIGHLIGHT((MAXMOUSEBUTTONS - 2) * 2 + 2), PHX(-7), 0, 100);
                CONTROL_MouseSensitivity=sense+1;
            }

            if (!ud.mouseaiming) modval(0,1,(int *)&g_myAimMode,1,probey == (MAXMOUSEBUTTONS-2)*2+2+1);
            else if (probey == (MAXMOUSEBUTTONS-2)*2+2+1)
            {
                mgametext(160,140+9+9+9,"SET MOUSE AIM TYPE TO TOGGLE ON/OFF",0,2+8+16);
                mgametext(160,140+9+9+9+9,"IN THE PLAYER SETUP MENU TO ENABLE",0,2+8+16);
            }

            modval(0,1,(int *)&ud.mouseflip,1,probey == (MAXMOUSEBUTTONS-2)*2+2+2);

            mgametextpal(240,118+9, g_myAimMode && !ud.mouseaiming ? "Yes" : "No", !ud.mouseaiming?MENUHIGHLIGHT((MAXMOUSEBUTTONS-2)*2+2+1):DISABLEDMENUSHADE, 0);
            mgametextpal(240,118+9+9, !ud.mouseflip ? "Yes" : "No", MENUHIGHLIGHT((MAXMOUSEBUTTONS-2)*2+2+2), 0);

            if (probey < (MAXMOUSEBUTTONS-2)*2+2)
            {
                mgametext(160,160+9,"UP/DOWN = SELECT BUTTON",0,2+8+16);
                mgametext(160,160+9+9,"ENTER = MODIFY",0,2+8+16);
            }
            break;
        }

        case 211:
        {
            int const keybindFuncCount = Menu_KeybindFuncCount();
            int const noneFuncIndex = keybindFuncCount;

            if (probey > noneFuncIndex)
                probey = noneFuncIndex;

            if (function == 0) Menu_DrawTopBarTitle(19, "MOUSE SETUP", 24);
            else if (function == 1) Menu_DrawTopBarTitle(19, "ADVANCED MOUSE", 24);
            else if (function == 2) Menu_DrawTopBarTitle(19, "JOYSTICK BUTTONS", 24);
            else if (function == 3) Menu_DrawTopBarTitle(19, "JOYSTICK AXES", 24);

            x = M_Probe(0,0,0,keybindFuncCount + 1);

            if (x==-1)
            {
                if (function == 0)
                {
                    // mouse button
                    ChangeToMenu(205);
                    probey = whichkey;
                }
                else if (function == 1)
                {
                    // mouse digital axis
                    ChangeToMenu(212);
                    probey = 2+(whichkey^2); // Keep in sync with advanced mouse menu axis start index
                }
                else if (function == 2)
                {
                    // joystick button/hat
                    ChangeToMenu(207);
                    probey = whichkey;
                }
                else if (function == 3)
                {
                    // joystick digital axis
                    ChangeToMenu((whichkey>>2)+208);
                    probey = 1+((whichkey>>1)&1)*4+(whichkey&1);
                }
                break;
            }
            else if (x >= 0)
            {
                x = (x == noneFuncIndex) ? -1 : Menu_KeybindFuncAt(x);

                if (function == 0)
                {
                    if (whichkey < (MAXMOUSEBUTTONS-2)*2)
                    {
                        ud.config.MouseFunctions[whichkey>>1][whichkey&1] = x;
                        CONTROL_MapButton(x, whichkey>>1, whichkey&1, controldevice_mouse);
                        //mousebind(whichkey>>1).cmdstr[0] = 0; // kill the bind when changing the button in the menu
                        CONTROL_FreeMouseBind(whichkey >> 1);
                    }
                    else
                    {
                        ud.config.MouseFunctions[whichkey-(MAXMOUSEBUTTONS-2)][0] = x;
                        CONTROL_MapButton(x, whichkey-(MAXMOUSEBUTTONS-2), 0, controldevice_mouse);
                        //mousebind(whichkey - (MAXMOUSEBUTTONS - 2)).cmdstr[0] = 0;
                        CONTROL_FreeMouseBind(whichkey - (MAXMOUSEBUTTONS - 2));
                    }
                    ChangeToMenu(205);
                    probey = whichkey;
                }
                else if (function == 1)
                {
                    ud.config.MouseDigitalFunctions[whichkey>>1][whichkey&1] = x;
                    //CONTROL_MapDigitalAxis(whichkey>>1, x, whichkey&1, controldevice_mouse);
                    ChangeToMenu(212);
                    probey = 2+(whichkey^2); // Keep in sync with advanced mouse menu axis start index
                }
                else if (function == 2)
                {
                    if (whichkey < 2*joystick.numButtons)
                    {
                        ud.config.JoystickFunctions[whichkey>>1][whichkey&1] = x;
                        CONTROL_MapButton(x, whichkey>>1, whichkey&1, controldevice_joystick);
                    }
                    else
                    {
                        ud.config.JoystickFunctions[joystick.numButtons + (whichkey-2*joystick.numButtons)][0] = x;
                        CONTROL_MapButton(x, joystick.numButtons + (whichkey-2*joystick.numButtons), 0, controldevice_joystick);
                    }
                    ChangeToMenu(207);
                    probey = whichkey;
                }
                else if (function == 3)
                {
                    ud.config.JoystickDigitalFunctions[whichkey>>1][whichkey&1] = x;
                    CONTROL_MapDigitalAxis(whichkey>>1, x, whichkey&1);
                    ChangeToMenu((whichkey>>2)+208);
                    probey = 1+((whichkey>>1)&1)*4+(whichkey&1);
                }
                break;
            }

            mgametext(320>>1,31,"SELECT A FUNCTION TO ASSIGN",0,2+8+16);

            if (function == 0)
            {
                if (whichkey < (MAXMOUSEBUTTONS-2)*2)
                    sprintf(tempbuf,"TO %s%s", (whichkey&1)?"DOUBLE-CLICKED ":"", mousebuttonnames[whichkey>>1]);
                else
                    Bstrcpy(tempbuf, mousebuttonnames[whichkey-(MAXMOUSEBUTTONS-2)]);
            }
            else if (function == 1)
            {
                Bstrcpy(tempbuf,"TO DIGITAL ");
                switch (whichkey)
                {
                case 0:
                    Bstrcat(tempbuf, "LEFT");
                    break;
                case 1:
                    Bstrcat(tempbuf, "RIGHT");
                    break;
                case 2:
                    Bstrcat(tempbuf, "UP");
                    break;
                case 3:
                    Bstrcat(tempbuf, "DOWN");
                    break;
                }
            }
            else if (function == 2)
            {
                static const char *directions[] =
                    { "UP", "RIGHT", "DOWN", "LEFT"
                    };
                if (whichkey < 2*joystick.numButtons)
                    Bsprintf(tempbuf,"TO %s%s", (whichkey&1)?"DOUBLE-CLICKED ":"", joyGetName(1,whichkey>>1));
                else
                    Bsprintf(tempbuf,"TO HAT %s", directions[whichkey-2*joystick.numButtons]);
            }
            else if (function == 3)
            {
                Bsprintf(tempbuf,"TO DIGITAL %s %s",joyGetName(0,whichkey>>1),(whichkey&1)?"POSITIVE":"NEGATIVE");
            }

            mgametext(320>>1,31+9,tempbuf,0,2+8+16);

            if (KB_KeyPressed(sc_End))
            {
                KB_ClearKeyDown(sc_End);
                probey = noneFuncIndex;
                S_PlaySound(KICK_HIT);
            }
            else if (KB_KeyPressed(sc_Home))
            {
                KB_ClearKeyDown(sc_Home);
                probey = 0;
                S_PlaySound(KICK_HIT);
            }

            m = probey - 6;
            if (m < 0) m = 0;
            else if (m + 13 >= keybindFuncCount + 1) m = keybindFuncCount + 1 - 13;
            if (m < 0) m = 0;

            for (l=0; l < min(13,keybindFuncCount + 1); l++)
            {
                if (l+m == noneFuncIndex)
                    strcpy(tempbuf, "  -NONE-");
                else
                    strcpy(tempbuf, CONFIG_FunctionNumToName(Menu_KeybindFuncAt(m+l)));

                for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
                minitext(100,51+l*8,tempbuf,(m+l == probey)?0:16,10+16);
            }

            mgametext(320>>1,161,"PRESS \"ESCAPE\" TO CANCEL",0,2+8+16);

            break;
        }

        case 212:
        {
            Menu_DrawTopBarTitle(19, "ADVANCED MOUSE", 24);

            c = (320>>1)-120;
            static const int axes_start_menu_idx = 2;
            static const int axes_start_ypos = 92;

            onbar = (probey == 0 || probey == 1);
            if (probey < 2)
                x = M_Probe(c,46,16,6);
            else if (probey < 6)
            {
                m=50;
                x = probesm(c+10, axes_start_ypos+3,9,6);
            }
            else
            {
                x = M_Probe(c,146+16-(16 * 6),16,6);
            }

            switch (x)
            {
            case -1:
                ChangeToMenu(205);
                probey = (MAXMOUSEBUTTONS-2)*2+2+2+2;
                break;

            case 0:
                // x-axis scale
            case 1:
                // y-axis scale
                break;

            case 2:
                // digital up
            case 3:
                // digital down
            case 4:
                // digital left
            case 5:
                // digital right
                function = 1;
                whichkey = (x-axes_start_menu_idx)^2; // flip the actual axis number
                ChangeToMenu(211);
                probey = ud.config.MouseDigitalFunctions[whichkey>>1][whichkey&1];
                if (probey < 0) probey = NUMGAMEFUNCTIONS-1;
                break;
            }

            switch (probey)
            {
            case 2:
            case 3:
            case 4:
            case 5:
                mgametext(160,144+9+9,"DIGITAL AXES ARE NOT FOR MOUSE LOOK",0,2+8+16);
                mgametext(160,144+9+9+9,"OR FOR AIMING UP AND DOWN",0,2+8+16);
                break;
            }

            menutext(c,46,MENUHIGHLIGHT(0),0,"X-AXIS SCALE");

            l = fix16_from_float(CONTROL_MouseAxesSensitivity[0]);
            _bar(0, c + 160 + 40, 46, &l, fix16_from_float(0.1f), x == 0, MENUHIGHLIGHT(0), 0, 0, fix16_from_float(100.0));

            if (fix16_to_float(l) != CONTROL_MouseAxesSensitivity[0])
            {
                CONTROL_MouseAxesSensitivity[0] = fix16_to_float(l);
                //CONTROL_SetAnalogAxisScale(0, fix16_to_float(l), controldevice_mouse);
            }

            Bsprintf(tempbuf,"%s%.2f",l>=0?" ":"", fix16_to_float(l));
            mgametext(c+160-16,46-8,tempbuf,MENUHIGHLIGHT(0),2+8+16);

            menutext(c,46+16,MENUHIGHLIGHT(1),0,"Y-AXIS SCALE");

            l = fix16_from_float(CONTROL_MouseAxesSensitivity[1]);
            _bar(0, c + 160 + 40, 46 + 16, &l, fix16_from_float(0.1f), x == 1, MENUHIGHLIGHT(1), 0, 0, fix16_from_float(100.0));

            if (fix16_to_float(l) != CONTROL_MouseAxesSensitivity[1])
            {
                CONTROL_MouseAxesSensitivity[1] = fix16_to_float(l);
                //CONTROL_SetAnalogAxisScale(1, fix16_to_float(l), controldevice_mouse);
            }

            Bsprintf(tempbuf,"%s%.2f",l>=0?" ":"", fix16_to_float(l));
            mgametext(c+160-16,46+16-8,tempbuf,MENUHIGHLIGHT(1),2+8+16);

            // Digital Axes
            Menu_DrawTopBarTitle(94, "DIGITAL AXES SETUP", 43 + 16 + 16 + 16 + 8);

            static const char* directions[] = { "UP:", "DOWN:", "LEFT:", "RIGHT:" };

            for (int index = 0; index < 4; index++)
            {
                mgametextpal(c + 10, axes_start_ypos + 16 + (9 * index), directions[index], MENUHIGHLIGHT(axes_start_menu_idx + index), 10);

                int axis = 0; // Left/Right
                int direction = index % 2;
                if (index < 2)
                    axis = 1; // Up/Down

                if (ud.config.MouseDigitalFunctions[axis][direction] < 0)
                    strcpy(tempbuf, "  -NONE-");
                else
                    strcpy(tempbuf, CONFIG_FunctionNumToName(ud.config.MouseDigitalFunctions[axis][direction]));

                for (i = 0; tempbuf[i]; i++) if (tempbuf[i] == '_') tempbuf[i] = ' ';
                minitextshade(c + 10 + 60, (axes_start_ypos+1) + 16 + (9 * index), tempbuf, MENUHIGHLIGHT(axes_start_menu_idx + index), 0, 10 + 16);
            }

            break;
        }

        case 206:
        {
            Menu_DrawTopBarTitle(19, "JOYSTICK SETUP", 24);

            x = M_Probe(160,100-18,18,3);

            switch (x)
            {
            case -1:
                ChangeToMenu(202);
                probey = 6;
                break;
            case 0:
            case 1:
                ChangeToMenu(207+x);
                break;
            case 2:
                ChangeToMenu(213);
                break;
            }

            menutext(160,100-18,0,0,"EDIT BUTTONS");
            menutext(160,100,0,0,"EDIT AXES");
            menutext(160,100+18,0,0,"DEAD ZONES");

            break;
        }

        case 207:
        {
            Menu_DrawTopBarTitle(10, "JOYSTICK BUTTONS", 15);

            c = 2*joystick.numButtons + 4*(joystick.numHats>0);

            x = M_Probe(0,0,0,c);

            if (x == -1)
            {
                ChangeToMenu(206);
                probey = 0;
                break;
            }
            else if (x >= 0)
            {
                function = 2;
                whichkey = x;
                ChangeToMenu(211);
                if (x < 2*joystick.numButtons)
                {
                    probey = ud.config.JoystickFunctions[x>>1][x&1];
                }
                else
                {
                    probey = ud.config.JoystickFunctions[joystick.numButtons + (x-2*joystick.numButtons)][0];
                }
                if (probey < 0) probey = NUMGAMEFUNCTIONS-1;
                break;
            }

            // the top of our list
            if (c < 13) m = 0;
            else
            {
                m = probey - 6;
                if (m < 0) m = 0;
                else if (m + 13 >= c) m = c-13;
            }

            for (l=0; l<min(13,c); l++)
            {
                if (m+l < 2*joystick.numButtons)
                {
                    sprintf(tempbuf, "%s%s", ((l+m)&1)?"Double ":"", joyGetName(1,(l+m)>>1));
                    x = ud.config.JoystickFunctions[(l+m)>>1][(l+m)&1];
                }
                else
                {
                    static const char *directions[] =
                        { "Up", "Right", "Down", "Left"
                        };
                    sprintf(tempbuf, "Hat %s", directions[(l+m)-2*joystick.numButtons]);
                    x = ud.config.JoystickFunctions[joystick.numButtons + ((l+m)-2*joystick.numButtons)][0];
                }
                minitextshade(80-4,33+l*8,tempbuf,(m+l == probey)?0:16,1,10+16);

                if (x == -1)
                    minitextshade(176,33+l*8,"  -NONE-",(m+l==probey)?0:16,2,10+16);
                else
                {
                    strcpy(tempbuf, CONFIG_FunctionNumToName(x));
                    for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
                    minitextshade(176,33+l*8,tempbuf,(m+l==probey)?0:16,2,10+16);
                }
            }

            mgametext(160,149,"UP/DOWN = SELECT BUTTON",0,2+8+16);
            mgametext(160,149+9,"ENTER = MODIFY",0,2+8+16);
            break;
        }

        case 208:
        case 209:
        case 217:
        case 218:
        case 219:
        case 220:
        case 221:
        case 222:
        {
            int thispage, twothispage;

            Menu_DrawTopBarTitle(10, "JOYSTICK AXES", 15);

            thispage = (g_currentMenu < 217) ? (g_currentMenu-208) : (g_currentMenu-217)+2;
            twothispage = (thispage*2+1 < joystick.numAxes);

            onbar = 0;
            switch (probey)
            {
            case 0:
            case 4:
                onbar = 1;
                x = M_Probe(88,45+(probey==4)*64,0,1+(4<<twothispage));
                break;
            case 1:
            case 2:
            case 5:
            case 6:
                x = M_Probe(172+(probey==2||probey==6)*72,45+15+(probey==5||probey==6)*64,0,1+(4<<twothispage));
                break;
            case 3:
            case 7:
                x = M_Probe(88,45+15+15+(probey==7)*64,0,1+(4<<twothispage));
                break;
            default:
                x = M_Probe(60,79+79*twothispage,0,1+(4<<twothispage));
                break;
            }

            switch (x)
            {
            case -1:
                ChangeToMenu(206);
                probey = 1;
                break;
            case 8:
                if (joystick.numAxes > 2)
                {
                    if (thispage == ((joystick.numAxes+1)/2)-1) ChangeToMenu(208);
                    else
                    {
                        if (g_currentMenu == 209) ChangeToMenu(217);
                        else ChangeToMenu(g_currentMenu+1);
                    }
                }
                break;

            case 4: // bar
                if (!twothispage && joystick.numAxes > 2)
                    ChangeToMenu(208);
            case 0:
                break;

            case 1: // digitals
            case 2:
            case 5:
            case 6:
                function = 3;
                whichkey = ((thispage*2+(x==5||x==6)) << 1) + (x==2||x==6);
                ChangeToMenu(211);
                probey = ud.config.JoystickDigitalFunctions[whichkey>>1][whichkey&1];
                if (probey < 0) probey = NUMGAMEFUNCTIONS-1;
                break;

            case 3: // analogues
            case 7:
                l = ud.config.JoystickAnalogueAxes[thispage*2+(x==7)];
                if (l == analog_turning) l = analog_strafing;
                else if (l == analog_strafing) l = analog_lookingupanddown;
                else if (l == analog_lookingupanddown) l = analog_moving;
                else if (l == analog_moving) l = -1;
                else l = analog_turning;
                ud.config.JoystickAnalogueAxes[thispage*2+(x==7)] = l;
                CONTROL_MapAnalogAxis(thispage*2+(x==7),l);
                {
                    extern int32_t mouseyaxismode;	// player.c
                    mouseyaxismode = -1;
                }
                break;
            default:
                break;
            }

            Bstrcpy(tempbuf,joyGetName(0,thispage*2));
            menutext(42,32,0,0,tempbuf);
            if (twothispage)
            {
                Bstrcpy(tempbuf,joyGetName(0,thispage*2+1));
                menutext(42,32+64,0,0,tempbuf);
            }
            mgametext(76,38,"SCALE",0,2+8+16);
            l = (ud.config.JoystickAnalogueScale[thispage*2]+262144) >> 13;
            bar(140+56,38+8,&l,1,x==0,0,0);
            l = (l<<13)-262144;
            if (l != ud.config.JoystickAnalogueScale[thispage*2])
            {
                CONTROL_SetAnalogAxisScale(thispage*2, l, controldevice_joystick);
                ud.config.JoystickAnalogueScale[thispage*2] = l;
            }
            Bsprintf(tempbuf,"%s%.2f",l>=0?" ":"",(float)l/65536.0);
            mgametext(140,38,tempbuf,0,2+8+16);

            mgametext(76,38+15,"DIGITAL",0,2+8+16);
            if (ud.config.JoystickDigitalFunctions[thispage*2][0] < 0)
                strcpy(tempbuf, "  -NONE-");
            else
                strcpy(tempbuf, CONFIG_FunctionNumToName(ud.config.JoystickDigitalFunctions[thispage*2][0]));

            for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
            minitext(140+12,38+15,tempbuf,0,10+16);

            if (ud.config.JoystickDigitalFunctions[thispage*2][1] < 0)
                strcpy(tempbuf, "  -NONE-");
            else
                strcpy(tempbuf, CONFIG_FunctionNumToName(ud.config.JoystickDigitalFunctions[thispage*2][1]));

            for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
            minitext(140+12+72,38+15,tempbuf,0,10+16);

            mgametext(76,38+15+15,"ANALOG",0,2+8+16);
            if (CONFIG_AnalogNumToName(ud.config.JoystickAnalogueAxes[thispage*2]))
            {
                p = CONFIG_AnalogNumToName(ud.config.JoystickAnalogueAxes[thispage*2]);
                if (p)
                {
                    mgametext(140+12,38+15+15, strchr(p,'_')+1, 0, 2+8+16);
                }
            }

            if (twothispage)
            {
                mgametext(76,38+64,"SCALE",0,2+8+16);
                l = (ud.config.JoystickAnalogueScale[thispage*2+1]+262144) >> 13;
                bar(140+56,38+8+64,&l,1,x==4,0,0);
                l = (l<<13)-262144;
                if (l != ud.config.JoystickAnalogueScale[thispage*2+1])
                {
                    CONTROL_SetAnalogAxisScale(thispage*2+1, l, controldevice_joystick);
                    ud.config.JoystickAnalogueScale[thispage*2+1] = l;
                }
                Bsprintf(tempbuf,"%s%.2f",l>=0?" ":"",(float)l/65536.0);
                mgametext(140,38+64,tempbuf,0,2+8+16);

                mgametext(76,38+64+15,"DIGITAL",0,2+8+16);
                if (ud.config.JoystickDigitalFunctions[thispage*2+1][0] < 0)
                    strcpy(tempbuf, "  -NONE-");
                else
                    strcpy(tempbuf, CONFIG_FunctionNumToName(ud.config.JoystickDigitalFunctions[thispage*2+1][0]));

                for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
                minitext(140+12,38+15+64,tempbuf,0,10+16);

                if (ud.config.JoystickDigitalFunctions[thispage*2+1][1] < 0)
                    strcpy(tempbuf, "  -NONE-");
                else
                    strcpy(tempbuf, CONFIG_FunctionNumToName(ud.config.JoystickDigitalFunctions[thispage*2+1][1]));

                for (i=0;tempbuf[i];i++) if (tempbuf[i]=='_') tempbuf[i] = ' ';
                minitext(140+12+72,38+15+64,tempbuf,0,10+16);

                mgametext(76,38+64+15+15,"ANALOG",0,2+8+16);
                if (CONFIG_AnalogNumToName(ud.config.JoystickAnalogueAxes[thispage*2+1]))
                {
                    p = CONFIG_AnalogNumToName(ud.config.JoystickAnalogueAxes[thispage*2+1]);
                    if (p)
                    {
                        mgametext(140+12,38+64+15+15, strchr(p,'_')+1, 0, 2+8+16);
                    }
                }
            }

            if (joystick.numAxes > 2)
            {
                menutext(320>>1,twothispage?158:108,SHX(-10),(joystick.numAxes<=2),"NEXT...");
                sprintf(tempbuf,"Page %d of %d",thispage+1,(joystick.numAxes+1)/2);
                mgametext(320-100,158,tempbuf,0,2+8+16);
            }
            break;
        }

        case 213:
        case 214:
        case 215:
        case 216:
        {
            // Pray this is enough pages for now :-|
            int first,last;
            Menu_DrawTopBarTitle(19, "JOY DEAD ZONES", 24);

            first = 4*(g_currentMenu-213);
            last  = min(4*(g_currentMenu-213)+4,joystick.numAxes);

            onbar = 1;
            x = M_Probe(320,48,15,2*(last-first)+(joystick.numAxes >4));

            if (x==-1)
            {
                ChangeToMenu(206);
                probey = 2;
                break;
            }
            else if (x==2*(last-first) && joystick.numAxes > 4)
            {
                ChangeToMenu((g_currentMenu-213) == (joystick.numAxes /4) ? 213 : (g_currentMenu+1));
                probey = 0;
                break;
            }

            for (m=first;m<last-1;m++)
            {
                int odx,dx,ody,dy;
                Bstrcpy(tempbuf,joyGetName(0,m));
                menutext(32,48+30*(m-first),0,0,tempbuf);

                mgametext(128,48+30*(m-first)-8,"DEAD",0,2+8+16);
                mgametext(128,48+30*(m-first)-8+15,"SATU",0,2+8+16);

                dx = odx = min(64,64*ud.config.JoystickAnalogueDead[m]/10000);
                dy = ody = min(64,64*ud.config.JoystickAnalogueSaturate[m]/10000);

                bar(217,48+30*(m-first),&dx,4,x==((m-first)*2),0,0);
                bar(217,48+30*(m-first)+15,&dy,4,x==((m-first)*2+1),0,0);

                Bsprintf(tempbuf,"%3d%%",100*dx/64);
                mgametext(217-49,48+30*(m-first)-8,tempbuf,0,2+8+16);
                Bsprintf(tempbuf,"%3d%%",100*dy/64);
                mgametext(217-49,48+30*(m-first)-8+15,tempbuf,0,2+8+16);

                if (dx != odx) ud.config.JoystickAnalogueDead[m]     = 10000l*dx/64l;
                if (dy != ody) ud.config.JoystickAnalogueSaturate[m] = 10000l*dy/64l;
                if (dx != odx || dy != ody)
                    JOYSTICK_SetDeadZone(m,ud.config.JoystickAnalogueDead[m],ud.config.JoystickAnalogueSaturate[m]);
            }
            //mgametext(160,158,"DEAD = DEAD ZONE, SAT. = SATURATION",0,2+8+16);
            if (joystick.numAxes>4)
            {
                menutext(32,48+30*(last-first),0,0,"NEXT...");
                sprintf(tempbuf,"Page %d of %d", 1+(g_currentMenu-213), (joystick.numAxes+3)/4);
                mgametext(320-100,158,tempbuf,0,2+8+16);
            }
            break;
        }

        case 700:
        case 701:
        {
            c = (320>>1)-120;
            Menu_DrawTopBarTitle(19, "SOUND SETUP", 24);

            if (!(changesmade & 8))
            {
                soundrate = ud.config.MixRate;
                soundvoices = ud.config.NumVoices;
                soundbits = ud.config.NumBits;
                for (i = 0; i < (int32_t)ARRAY_SIZE(MEOSV_SOUND_MIDIDRIVER); i++)
                {
                    if (MEOSV_SOUND_MIDIDRIVER[i] == ud.config.MusicDevice)
                    {
                        musicdevice = i;
                        break;
                    }
                }
            }

            {
                int io, ii, yy, d=c+160+40, enabled, j;
                const char *opts[] =
                {
                    "Sound",
                    "Sound volume",
                    "-",
                    "Music",
                    "Music volume",
                    "-",
                    "Sample rate",
                    "Sample size",
                    "Number of voices",
                    "-",
                    "Midi System",
                    "-",
                    "Restart sound system",
                    "-",
                    WW2GI ? "WW2GI talk" : NAM ? "NAM talk" : "Duke talk",
                    WW2GI ? "GI Match player sounds" : NAM ? "GruntMatch player sounds" : "DukeMatch player sounds",
                    "Ambient sounds",
                    WORLDTOUR ? "Developer commentary" : NULL,
                    NULL
                };

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        if (io <= probey) yy += 4;
                        continue;
                    }
                    if (io < probey) yy += 8;
                    io++;
                }

                onbar = (probey == 1 || probey == 3);
                x = probesm(c,yy+5,0,io);

                if (x == -1)
                {
                    ud.config.MixRate = soundrate;
                    ud.config.NumVoices = soundvoices;
                    ud.config.NumBits = soundbits;

                    if (ud.gm&MODE_GAME && g_currentMenu == 701)
                    {
                        ud.gm &= ~MODE_MENU;
                        if (ud.multimode < 2  && ud.recstat != 2)
                        {
                            ready2send = 1;
                            totalclock = ototalclock;
                        }
                    }

                    else ChangeToMenu(202);
                    probey = 1;
                }

                yy = 37;
                for (ii=io=0; opts[ii]; ii++)
                {
                    if (opts[ii][0] == '-' && !opts[ii][1])
                    {
                        yy += 4;
                        continue;
                    }
                    enabled = 1;

                    switch (io)
                    {
                    case 0:
                        if (ud.config.FXDevice >= 0)
                        {
                            i = ud.config.SoundToggle;
                            modval(0,1,(int *)&ud.config.SoundToggle,1,probey==io);
                            if (x==io)
                                ud.config.SoundToggle = 1-ud.config.SoundToggle;
                            if (i != ud.config.SoundToggle)
                            {
                                if (ud.config.SoundToggle == 0)
                                {
                                    FX_StopAllSounds();
                                }
                            }
                        }
                        mgametextpal(d,yy, ud.config.SoundToggle ? "On" : "Off", MENUHIGHLIGHT(io), 0);
                        break;
                    case 1:
                    {
                        enabled = (ud.config.SoundToggle && ud.config.FXDevice >= 0);
                        l = ud.config.FXVolume;
                        ud.config.FXVolume >>= 2;
                        _bar(1,d+8,yy+7, &ud.config.FXVolume,4,probey==io,enabled?MENUHIGHLIGHT(io):UNSELMENUSHADE,!enabled,0,64);
                        ud.config.FXVolume <<= 2;
                        if (l != ud.config.FXVolume)
                            FX_SetVolume((short) ud.config.FXVolume);
                    }
                    break;
                    case 2:
                        if (ud.config.MusicDevice >= 0)
                        {
                            i = ud.config.MusicToggle;
                            modval(0,1,(int *)&ud.config.MusicToggle,1,probey==io);
                            if (x==io)
                                ud.config.MusicToggle = 1-ud.config.MusicToggle;
                            if (i != ud.config.MusicToggle)
                            {
                                if (ud.config.MusicToggle == 0) S_PauseMusic(1);
                                else
                                {
                                    S_RestartMusic();

                                    S_PauseMusic(0);
                                }
                            }
                        }
                        mgametextpal(d,yy, ud.config.MusicToggle ? "On" : "Off", MENUHIGHLIGHT(io), 0);
                        break;
                    case 3:
                    {
                        enabled = (ud.config.MusicToggle && ud.config.MusicDevice >= 0);
                        l = ud.config.MusicVolume;
                        ud.config.MusicVolume >>= 2;
                        _bar(1,d+8,yy+7, &ud.config.MusicVolume,4,probey==io,enabled?MENUHIGHLIGHT(io):UNSELMENUSHADE,!enabled,0,64);
                        ud.config.MusicVolume <<= 2;
                        if (l != ud.config.MusicVolume)
                            S_MusicVolume(ud.config.MusicVolume);
                    }
                    break;
                    case 4:
                    {
                        int rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000 };
                        int j = (sizeof(rates)/sizeof(rates[0]));

                        for (i = 0;i<j;i++)
                            if (rates[i] == ud.config.MixRate)
                                break;

                        modval(0,j-1,(int *)&i,1,enabled && probey==io);
                        if (x == io)
                        {
                            i++;
                            if (i >= j)
                                i = 0;
                        }
                        if (i == j)
                            Bsprintf(tempbuf,"Other");
                        else
                        {
                            Bsprintf(tempbuf,"%d Hz",rates[i]);
                            if (rates[i] != ud.config.MixRate)
                            {
                                ud.config.MixRate = rates[i];
                                changesmade |= 8;
                            }
                        }
                        mgametextpal(d,yy,tempbuf, MENUHIGHLIGHT(io), 0);
                    }
                    break;
                    case 5:
                        i = ud.config.NumBits;
                        if (x==io)
                        {
                            if (ud.config.NumBits == 8)
                                ud.config.NumBits = 16;
                            else if (ud.config.NumBits == 16)
                                ud.config.NumBits = 8;
                        }
                        modval(8,16,(int *)&ud.config.NumBits,8,probey==io);
                        if (ud.config.NumBits != i)
                            changesmade |= 8;
                        Bsprintf(tempbuf,"%d-bit",ud.config.NumBits);
                        mgametextpal(d,yy, tempbuf, MENUHIGHLIGHT(io), 0);
                        break;
                    case 6:
                        i = ud.config.NumVoices;
                        if (x==io)
                        {
                            ud.config.NumVoices++;
                            if (ud.config.NumVoices > 96)
                                ud.config.NumVoices = 4;
                        }
                        modval(4,96,(int *)&ud.config.NumVoices,1,probey==io);
                        if (ud.config.NumVoices != i)
                            changesmade |= 8;
                        Bsprintf(tempbuf,"%d",ud.config.NumVoices);
                        mgametextpal(d,yy, tempbuf, MENUHIGHLIGHT(io), 0);
                        break;
                    case 7:
                        i = musicdevice;
                        if (x == io)
                        {
                            musicdevice++;
                            if (musicdevice >= (int32_t)ARRAY_SIZE(MEOSV_SOUND_MIDIDRIVER))
                                musicdevice = 0;
                        }
                        modval(0, 2, (int*)&musicdevice, 1, probey == io);

                        if (musicdevice != i)
                            changesmade |= 8;

                        ud.config.MusicDevice = MEOSV_SOUND_MIDIDRIVER[musicdevice];

                        Bsprintf(tempbuf, "%s", MEOSN_SOUND_MIDIDRIVER[musicdevice]);
                        mgametextpal(d, yy, tempbuf, MENUHIGHLIGHT(io), 0);
                        break;
                    case 8:
                        enabled = (changesmade&8);
                        if (!enabled) break;
                        if (x == io)
                        {
                            S_SoundShutdown();
                            S_MusicShutdown();

                            LOG_F(INFO, "Initializing music...");
                            S_MusicStartup();
                            LOG_F(INFO, "Initializing sound...");
                            S_SoundStartup();

                            FX_StopAllSounds();

                            S_RestartMusic();

                            changesmade &= ~8;
                        }
                        break;
                    case 9:
                        enabled = (ud.config.SoundToggle && ud.config.FXDevice >= 0);
                        i = j = (ud.config.VoiceToggle&1);
                        modval(0,1,(int *)&i,1,enabled && probey==io);
                        if (x == io || j != i)
                            ud.config.VoiceToggle ^= 1;
                        mgametextpal(d,yy, ud.config.VoiceToggle&1? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, enabled?0:1);
                        break;
                    case 10:
                        enabled = (ud.config.SoundToggle && ud.config.FXDevice >= 0);
                        i = j = (ud.config.VoiceToggle&4);
                        modval(0,1,(int *)&i,1,enabled && probey==io);
                        if (x == io || j != i)
                            ud.config.VoiceToggle ^= 4;
                        mgametextpal(d,yy, ud.config.VoiceToggle&4? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, enabled?0:1);
                        break;
                    case 11:
                        enabled = (ud.config.SoundToggle && ud.config.FXDevice >= 0);
                        modval(0,1,(int *)&ud.config.AmbienceToggle,1,enabled && probey==io);
                        if (enabled && x == io)
                            ud.config.AmbienceToggle = 1-ud.config.AmbienceToggle;
                        mgametextpal(d,yy, ud.config.AmbienceToggle? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, enabled?0:1);
                        break;
                    case 12:
                        enabled = (WORLDTOUR && ud.config.SoundToggle && ud.config.FXDevice >= 0);
                        modval(0,1,(int *)&g_worldTourCommentary,1,enabled && probey==io);
                        if (enabled && x == io)
                            g_worldTourCommentary = 1-g_worldTourCommentary;
                        mgametextpal(d,yy, g_worldTourCommentary? "Yes" : "No", enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, enabled?0:1);
                        break;

                    default:
                        break;
                    }
                    mgametextpal(c,yy, opts[ii], enabled?MENUHIGHLIGHT(io):DISABLEDMENUSHADE, enabled?10:1);
                    io++;
                    yy += 8;

                }
            }
            break;
        }

        case MENU_SAVE:
        {
            ChangeToMenu(351);
            g_screenCapture = 1;
            G_DrawRooms(myconnectindex, 65536);
            //savetemp("duke3d.tmp",waloff[TILE_SAVESHOT],160*100);
            g_screenCapture = 0;
            break;
        }

        case 360:
        case 361:
        case 362:
        case 363:
        case 364:
        case 365:
        case 366:
        case 367:
        case 368:
        case 369:
        case 351:
        case MENU_LOAD:
        {
            c = 320>>1;
            if (!FURY)
            {
                rotatesprite(c<<16,200<<15,65536L,0,MENUSCREEN,16,0,10+64,0,0,xdim-1,ydim-1);
                rotatesprite(c<<16,19<<16,65536L,0,MENUBAR,16,0,10,0,0,xdim-1,ydim-1);

                if (g_currentMenu == MENU_LOAD) menutext(c,24,0,0,"LOAD GAME");
                else menutext(c,24,0,0,"SAVE GAME");
            }

            if (g_currentMenu >= 360 && g_currentMenu <= 369)
            {
                if (!FURY)
                {
                    sprintf(tempbuf,"PLAYERS: %-2d                      ",ud.multimode);
                    mgametext(160,156,tempbuf,0,2+8+16);
                    sprintf(tempbuf,"EPISODE: %-2d / LEVEL: %-2d / SKILL: %-2d",1+ud.volume_number,1+ud.level_number,ud.player_skill);
                    mgametext(160,168,tempbuf,0,2+8+16);
                    if (ud.volume_number == 0 && ud.level_number == 7)
                        mgametext(160,180,currentboardfilename,0,2+8+16);
                }

                x = G_EnterText((320>>1),184,&ud.savegame[g_currentMenu-360][0],19, 999);

                if (x == -1)
                {
                    //        ReadSaveGameHeaders();
                    ud.gm = MODE_GAME;
                    if (ud.multimode < 2  && ud.recstat != 2)
                    {
                        ready2send = 1;
                        totalclock = ototalclock;
                    }
                    goto DISPLAYNAMES;
                }

                if (x == 1)
                {
                    if (ud.savegame[g_currentMenu-360][0] == 0)
                    {
                        KB_FlushKeyboardQueue();
                        ChangeToMenu(351);
                    }
                    else
                    {
                        if (ud.multimode > 1)
                            G_SavePlayer(-1-(g_currentMenu-360));
                        else G_SavePlayer(g_currentMenu-360);
                        g_lastSaveSlot = g_currentMenu-360;
                        ud.gm = MODE_GAME;

                        if (ud.multimode < 2  && ud.recstat != 2)
                        {
                            ready2send = 1;
                            totalclock = ototalclock;
                        }
                        KB_ClearKeyDown(sc_Escape);
                        S_PlaySound(EXITMENUSOUND);
                    }
                }

                if (!FURY)
                {
                    rotatesprite(101<<16,97<<16,65536>>1,512,TILE_SAVESHOT,-32,0,2+4+8+64,0,0,xdim-1,ydim-1);
                    M_DisplaySaveGameList();
                    rotatesprite((c+67+strlen(&ud.savegame[g_currentMenu-360][0])*4)<<16,(50+12*probey)<<16,32768L-10240,0,SPINNINGNUKEICON+((((int32_t)totalclock)>>3)%7),0,0,10,0,0,xdim-1,ydim-1);
                }
                else
                    M_DisplayFurySaveGameList();
                break;
            }

            last_threehundred = probey;

            x = M_Probe(c+68,54,12,10);

            if (g_currentMenu == MENU_LOAD)
            {
                if (ud.savegame[probey][0])
                {
                    if (lastsavehead != probey)
                    {
                        G_LoadSaveHeader(probey,&savehead);
                        lastsavehead = probey;
                    }

                    if (!FURY)
                    {
                        rotatesprite(101<<16,97<<16,65536L>>1,512,TILE_LOADSHOT,-32,0,4+10+64,0,0,xdim-1,ydim-1);
                        sprintf(tempbuf,"PLAYERS: %-2d                      ",savehead.numplr);
                        mgametext(160,156,tempbuf,0,2+8+16);
                        sprintf(tempbuf,"EPISODE: %-2d / LEVEL: %-2d / SKILL: %-2d",1+savehead.volnum,1+savehead.levnum,savehead.plrskl);
                        mgametext(160,168,tempbuf,0,2+8+16);
                        if (savehead.volnum == 0 && savehead.levnum == 7)
                            mgametext(160,180,savehead.boardfn,0,2+8+16);
                    }
                }
                else if (!FURY) menutext(69,70,0,0,"EMPTY");
            }
            else
            {
                if (ud.savegame[probey][0])
                {
                    if (lastsavehead != probey)
                        G_LoadSaveHeader(probey,&savehead);
                    lastsavehead = probey;
                    if (!FURY)
                        rotatesprite(101<<16,97<<16,65536L>>1,512,TILE_LOADSHOT,-32,0,4+10+64,0,0,xdim-1,ydim-1);
                }
                else if (!FURY) menutext(69,70,0,0,"EMPTY");
                if (!FURY)
                {
                    sprintf(tempbuf,"PLAYERS: %-2d                      ",ud.multimode);
                    mgametext(160,156,tempbuf,0,2+8+16);
                    sprintf(tempbuf,"EPISODE: %-2d / LEVEL: %-2d / SKILL: %-2d",1+ud.volume_number,1+ud.level_number,ud.player_skill);
                    mgametext(160,168,tempbuf,0,2+8+16);
                    if (ud.volume_number == 0 && ud.level_number == 7)
                        mgametext(160,180,currentboardfilename,0,2+8+16);
                }
            }

            switch (x)
            {
            case -1:
                if (g_currentMenu == MENU_LOAD)
                {
                    if ((ud.gm&MODE_GAME) != MODE_GAME)
                    {
                        ChangeToMenu(0);
                        break;
                    }
                    else
                        ud.gm &= ~MODE_MENU;
                }
                else
                    ud.gm = MODE_GAME;

                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }

                break;
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
                if (g_currentMenu == MENU_LOAD)
                {
                    if (ud.savegame[x][0])
                        g_currentMenu = (1000+x);
                }
                else
                {
                    if (ud.savegame[x][0] != 0)
                        g_currentMenu = 2000+x;
                    else
                    {
                        KB_FlushKeyboardQueue();
                        g_currentMenu = (360+x);
                        ud.savegame[x][0] = 0;
                        inputloc = 0;
                    }
                }
                break;
            }

DISPLAYNAMES:
            if (FURY)
                M_DisplayFurySaveGameList();
            else
                M_DisplaySaveGameList();
            break;
        }

        case MENU_STORY:
        case MENU_F1HELP:
            if (FURY)
            {
                int const goPrevious =
                    KB_KeyPressed(sc_LeftArrow) ||
                    KB_KeyPressed(sc_kpad_4) ||
                    KB_KeyPressed(sc_UpArrow) ||
                    KB_KeyPressed(sc_PgUp) ||
                    KB_KeyPressed(sc_kpad_8) ||
                    WHEELDOWN;

                int const goNext =
                    KB_KeyPressed(sc_PgDn) ||
                    KB_KeyPressed(sc_Enter) ||
                    KB_KeyPressed(sc_kpad_Enter) ||
                    KB_KeyPressed(sc_RightArrow) ||
                    KB_KeyPressed(sc_DownArrow) ||
                    KB_KeyPressed(sc_kpad_2) ||
                    KB_KeyPressed(sc_kpad_9) ||
                    KB_KeyPressed(sc_Space) ||
                    KB_KeyPressed(sc_kpad_6) ||
                    LMB ||
                    WHEELUP;

                x = M_Probe(0, 0, 0, 1);

                if (x == -1)
                {
                    ChangeToMenu((ud.gm & MODE_GAME) ? MENU_MAIN_INGAME : MENU_MAIN);
                    return;
                }

                if (goPrevious || goNext || x >= 0)
                {
                    KB_ClearKeyDown(sc_LeftArrow);
                    KB_ClearKeyDown(sc_kpad_4);
                    KB_ClearKeyDown(sc_UpArrow);
                    KB_ClearKeyDown(sc_PgUp);
                    KB_ClearKeyDown(sc_kpad_8);
                    KB_ClearKeyDown(sc_PgDn);
                    KB_ClearKeyDown(sc_Enter);
                    KB_ClearKeyDown(sc_RightArrow);
                    KB_ClearKeyDown(sc_kpad_Enter);
                    KB_ClearKeyDown(sc_kpad_6);
                    KB_ClearKeyDown(sc_kpad_9);
                    KB_ClearKeyDown(sc_kpad_2);
                    KB_ClearKeyDown(sc_DownArrow);
                    KB_ClearKeyDown(sc_Space);
                    MOUSE_ClearButton(M_LEFTBUTTON);
                    MOUSE_ClearButton(M_WHEELUP);
                    MOUSE_ClearButton(M_WHEELDOWN);
                    S_PlaySound(KICK_HIT);

                    ChangeToMenu(goPrevious ? MENU_STORY : MENU_F1HELP);
                }
                break;
            }
            if (VOLUMEALL) goto VOLUME_ALL_40x;
            fallthrough__;
        case 402:
        case 403:
        {
            c = 320>>1;

            if (KB_KeyPressed(sc_LeftArrow) ||
                    KB_KeyPressed(sc_kpad_4) ||
                    KB_KeyPressed(sc_UpArrow) ||
                    KB_KeyPressed(sc_PgUp) ||
                    KB_KeyPressed(sc_kpad_8))
            {
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_kpad_4);
                KB_ClearKeyDown(sc_UpArrow);
                KB_ClearKeyDown(sc_PgUp);
                KB_ClearKeyDown(sc_kpad_8);

                S_PlaySound(KICK_HIT);
                g_currentMenu--;
                if (g_currentMenu < 400) g_currentMenu = 403;
            }
            else if (
                KB_KeyPressed(sc_PgDn) ||
                KB_KeyPressed(sc_Enter) ||
                KB_KeyPressed(sc_kpad_Enter) ||
                KB_KeyPressed(sc_RightArrow) ||
                KB_KeyPressed(sc_DownArrow) ||
                KB_KeyPressed(sc_kpad_2) ||
                KB_KeyPressed(sc_kpad_9) ||
                KB_KeyPressed(sc_Space) ||
                KB_KeyPressed(sc_kpad_6))
            {
                KB_ClearKeyDown(sc_PgDn);
                KB_ClearKeyDown(sc_Enter);
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_Enter);
                KB_ClearKeyDown(sc_kpad_6);
                KB_ClearKeyDown(sc_kpad_9);
                KB_ClearKeyDown(sc_kpad_2);
                KB_ClearKeyDown(sc_DownArrow);
                KB_ClearKeyDown(sc_Space);
                S_PlaySound(KICK_HIT);
                g_currentMenu++;
                if (g_currentMenu > 403) g_currentMenu = 400;
            }

            if (KB_KeyPressed(sc_Escape))
            {
                if (ud.gm&MODE_GAME)
                    ChangeToMenu(50);
                else ChangeToMenu(0);
                return;
            }

            renderFlushPerms();
            rotatesprite(0,0,65536L,0,ORDERING+g_currentMenu-400,0,0,10+16+64,0,0,xdim-1,ydim-1);

            break;
VOLUME_ALL_40x:

            c = 320>>1;

            if (KB_KeyPressed(sc_LeftArrow) ||
                    KB_KeyPressed(sc_kpad_4) ||
                    KB_KeyPressed(sc_UpArrow) ||
                    KB_KeyPressed(sc_PgUp) ||
                    KB_KeyPressed(sc_kpad_8) ||
                    WHEELDOWN)
            {
                KB_ClearKeyDown(sc_LeftArrow);
                KB_ClearKeyDown(sc_kpad_4);
                KB_ClearKeyDown(sc_UpArrow);
                KB_ClearKeyDown(sc_PgUp);
                KB_ClearKeyDown(sc_kpad_8);
                S_PlaySound(KICK_HIT);
                g_currentMenu--;
                if (g_currentMenu < 400) g_currentMenu = 401;
            }
            else if (
                KB_KeyPressed(sc_PgDn) ||
                KB_KeyPressed(sc_Enter) ||
                KB_KeyPressed(sc_kpad_Enter) ||
                KB_KeyPressed(sc_RightArrow) ||
                KB_KeyPressed(sc_DownArrow) ||
                KB_KeyPressed(sc_kpad_2) ||
                KB_KeyPressed(sc_kpad_9) ||
                KB_KeyPressed(sc_Space) ||
                KB_KeyPressed(sc_kpad_6) ||
                LMB || WHEELUP)
            {
                KB_ClearKeyDown(sc_PgDn);
                KB_ClearKeyDown(sc_Enter);
                KB_ClearKeyDown(sc_RightArrow);
                KB_ClearKeyDown(sc_kpad_Enter);
                KB_ClearKeyDown(sc_kpad_6);
                KB_ClearKeyDown(sc_kpad_9);
                KB_ClearKeyDown(sc_kpad_2);
                KB_ClearKeyDown(sc_DownArrow);
                KB_ClearKeyDown(sc_Space);
                S_PlaySound(KICK_HIT);
                g_currentMenu++;
                if (g_currentMenu > 401) g_currentMenu = 400;
            }

            x = M_Probe(0,0,0,1);

            if (x == -1)
            {
                if (ud.gm&MODE_GAME)
                    ChangeToMenu(50);
                else ChangeToMenu(0);
                return;
            }

            renderFlushPerms();
            switch (g_currentMenu)
            {
            case 400:
                rotatesprite(0,0,65536L,0,TEXTSTORY,0,0,10+16+64, 0,0,xdim-1,ydim-1);
                break;
            case 401:
                rotatesprite(0,0,65536L,0,F1HELP,0,0,10+16+64, 0,0,xdim-1,ydim-1);
                break;
            }

            break;
        }

        case MENU_QUIT:
        case MENU_QUIT_INGAME:
        {
            c = 320>>1;

            if (FURY)
            {
                // Ion Fury draws its themed quit modal from EVENT_DISPLAYMENUREST.
            }
            else
            {
                mgametext(c,90,"Are you sure you want to quit?",0,2+8+16);
                mgametext(c,99,"(Y/N)",0,2+8+16);
            }

            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                KB_FlushKeyboardQueue();

                Net_SendQuit();
            }

            x = M_Probe(186,124,0,1);
            if (x == -1 || KB_KeyPressed(sc_N) || RMB)
            {
                KB_ClearKeyDown(sc_N);
                quittimer = 0;
                if (ud.gm&MODE_DEMO && ud.recstat == 2)
                    ud.gm = MODE_DEMO;
                else
                {
                    if (g_currentMenu == MENU_QUIT_INGAME)
                    {
                        ChangeToMenu(last_menu);
                        probey = last_menu_pos;
                    }
                    else if (!(ud.gm & MODE_GAME || ud.recstat == 2))
                        ChangeToMenu(0);
                    else ud.gm &= ~MODE_MENU;
                    if (ud.multimode < 2  && ud.recstat != 2)
                    {
                        ready2send = 1;
                        totalclock = ototalclock;
                    }
                }
            }
            break;
        }

        case MENU_QUITTOTITLE:
        {
            c = 320>>1;
            if (FURY)
            {
                // Ion Fury draws its themed quit modal from EVENT_DISPLAYMENUREST.
            }
            else
            {
                mgametext(c,90,"Quit to Title?",0,2+8+16);
                mgametext(c,99,"(Y/N)",0,2+8+16);
            }

            if (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || KB_KeyPressed(sc_Y) || LMB)
            {
                KB_FlushKeyboardQueue();
                ud.gm = MODE_DEMO;
                if (ud.recstat == 1)
                    G_CloseDemoWrite();
                ChangeToMenu(0);
            }

            x = M_Probe(186,124,0,0);

            if (x == -1 || KB_KeyPressed(sc_N) || RMB)
            {
                ud.gm &= ~MODE_MENU;
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 1;
                    totalclock = ototalclock;
                }
            }
            break;
        }

        case 601:
        {
            G_DrawFrags();
            rotatesprite(160 << 16, 29 << 16, 65536L, 0, MENUBAR, 16, 0, 10, 0, 0, xdim - 1, ydim - 1);
            menutext(320 >> 1, 34, 0, 0, &g_player[myconnectindex].user_name[0]);

            sprintf(tempbuf, "Waiting for master");
            mgametext(160, 50, tempbuf, 0, 2 + 8 + 16);
            mgametext(160, 59, "to select level", 0, 2 + 8 + 16);

            if (KB_KeyPressed(sc_Escape))
            {
                KB_ClearKeyDown(sc_Escape);
                S_PlaySound(EXITMENUSOUND);
                ChangeToMenu(0);
            }
            break;
        }

        case 602:
        {
            if (menunamecnt == 0)
            {
                //        getfilenames("SUBD");
                getfilenames(".", "*.MAP");
                if (menunamecnt == 0)
                    ChangeToMenu(600);
            }
            fallthrough__;
        }

        case 603:
        {
            x = M_Probe(186,124,0,0);

            if (x == -1)
            {
                if (vote.starter == myconnectindex)
                    Net_CancelVote();

                ChangeToMenu(0);
            }

            mgametext(160,90,"WAITING FOR VOTES",0,2);
            break;
        }

        case 600:
        {
            c = (320>>1) - 120;
            if ((ud.gm&MODE_GAME) != MODE_GAME)
                G_DrawFrags();
            rotatesprite(160<<16,26<<16,65536L,0,MENUBAR,16,0,10,0,0,xdim-1,ydim-1);
            menutext(160,31,0,0,&g_player[myconnectindex].user_name[0]);

            x = M_Probe(c,57-8,16,7);

            modval(0,g_numGametypes-1,(int *)&ud.m_coop,1,probey==0);
            if (!VOLUMEONE)
                modval(0,g_numVolumes-1,(int *)&ud.m_volume_number,1,probey==1);

            i = ud.m_level_number;

            modval(0,ud.m_volume_number == 0?6+(boardfilename[0]!=0):MAXLEVELS-1,(int *)&ud.m_level_number,1,probey==2);

            if (probey == 1)
                if (ud.m_volume_number == 0 && ud.m_level_number > 6+(boardfilename[0]!=0))
                    ud.m_level_number = 0;

            while (g_mapInfo[(ud.m_volume_number*MAXLEVELS)+ud.m_level_number].name == NULL)
            {
                if (ud.m_level_number < i || i == 0)
                    ud.m_level_number--;
                else ud.m_level_number++;

                if (ud.m_level_number > MAXLEVELS-1 || ud.m_level_number < 0)
                {
                    ud.m_level_number = 0;
                    break;
                }
            }

            switch (x)
            {
            case -1:
                ud.m_recstat = 0;
                if (ud.gm&MODE_GAME) ChangeToMenu(50);
                else ChangeToMenu(0);
                break;
            case 0:
                ud.m_coop++;
                if (ud.m_coop == g_numGametypes) ud.m_coop = 0;
                break;
            case 1:
                if (!VOLUMEONE)
                {
                    ud.m_volume_number++;
                    if (ud.m_volume_number == g_numVolumes) ud.m_volume_number = 0;
                    if (ud.m_volume_number == 0 && ud.m_level_number > 6+(boardfilename[0]!=0))
                        ud.m_level_number = 0;
                    if (ud.m_level_number > MAXLEVELS-1) ud.m_level_number = 0;
                }
                break;
            case 2:
                ud.m_level_number++;
                if (!VOLUMEONE)
                {
                    if (ud.m_volume_number == 0 && ud.m_level_number > 6+(boardfilename[0]!=0))
                        ud.m_level_number = 0;
                }
                else
                {
                    if (ud.m_volume_number == 0 && ud.m_level_number > 5)
                        ud.m_level_number = 0;
                }
                if (ud.m_level_number > MAXLEVELS-1) ud.m_level_number = 0;
                break;
            case 3:
                ud.m_player_skill++;

                if (ud.m_player_skill > 4)
                {
                    ud.m_player_skill = 0;
                }

                M_DMFLAGS_SET(DMFLAG_NOMONSTERS, (ud.m_player_skill == 0) ? 1 : 0);
                break;

            case 4:
                ChangeToMenu(604);
                break;

            case 5:
                if (VOLUMEALL)
                {
                    currentlist = 1;
                    last_menu_pos = probey;
                    ChangeToMenu(101);
                }
                break;
            case 6:
                // master does whatever it wants
                if (myconnectindex == connecthead)
                {
                    G_NewGame(NEWGAME_RESETALL);
                    break;
                }
                else if (vote.starter == -1)
                {
                    if (myconnectindex != connecthead)
                        Net_InitiateVote();

                    ChangeToMenu(603);
                }
                break;
            }

            c += 40;

            //if(ud.m_coop==1) mgametext(c+70,57-7-9,"COOPERATIVE PLAY",0,2+8+16);
            //else if(ud.m_coop==2) mgametext(c+70,57-7-9,"DUKEMATCH (NO SPAWN)",0,2+8+16);
            //else mgametext(c+70,57-7-9,"DUKEMATCH (SPAWN)",0,2+8+16);
            mgametext(c+70,57-7-9,Gametypes[ud.m_coop].name,MENUHIGHLIGHT(0),2+8+16);
            if (VOLUMEONE)
            {
                mgametext(c+70,57+16-7-9,EpisodeNames[ud.m_volume_number],MENUHIGHLIGHT(1),2+8+16);
            }
            else
            {
                mgametext(c+70,57+16-7-9,EpisodeNames[ud.m_volume_number],MENUHIGHLIGHT(1),2+8+16);
            }

            mgametext(c+70,57+16+16-7-9,&g_mapInfo[MAXLEVELS*ud.m_volume_number+ud.m_level_number].name[0],MENUHIGHLIGHT(2),2+8+16);

            mgametext(c+70,57+16+16+16-7-9, ud.m_player_skill > 0 ? g_skillNames[ud.m_player_skill-1] : "NONE",MENUHIGHLIGHT(3),2+8+16);

            c -= 44;

            menutext(c,57-9,MENUHIGHLIGHT(0),PHX(-2),"GAME TYPE");

            if (VOLUMEONE)
            {
                sprintf(tempbuf,"EPISODE %d",ud.m_volume_number+1);
                menutext(c,57+16-9,MENUHIGHLIGHT(1),1,tempbuf);
            }
            else
            {
                sprintf(tempbuf,"EPISODE %d",ud.m_volume_number+1);
                menutext(c,57+16-9,MENUHIGHLIGHT(1),PHX(-3),tempbuf);
            }

            sprintf(tempbuf,"LEVEL %d",ud.m_level_number+1);
            menutext(c,57+16+16-9,MENUHIGHLIGHT(2),PHX(-4),tempbuf);

            if (NAM_WW2GI) {
                menutext(c,57+16+16+16-9,MENUHIGHLIGHT(3),PHX(-5),"ENEMIES");
            } else {
                menutext(c,57+16+16+16-9,MENUHIGHLIGHT(3),PHX(-5),"MONSTERS");
            }

            menutext(c,57+16+16+16+16-9,MENUHIGHLIGHT(4),0,"GAME FLAGS");

            if (VOLUMEALL)
            {
                menutext(c,57+16+16+16+16+16-9,MENUHIGHLIGHT(5),0,"USER MAP");
                if (boardfilename[0] != 0)
                    mgametext(c+70+44,57+16+16+16+16,boardfilename,MENUHIGHLIGHT(6),2+8+16);
            }
            else
            {
                menutext(c,57+16+16+16+16+16-9,MENUHIGHLIGHT(5),1,"USER MAP");
            }

            menutext(c,57+16+16+16+16+16+16-9,MENUHIGHLIGHT(6),vote.starter!=-1,"START GAME");

            break;
        }

        case 604:
        {
            Menu_DrawTopBarTitle(19, "GAME FLAGS", 24);

            c = (320 >> 1) - 120;

            {
                int io, ii, yy, d = c + 160 + 40;

                struct
                {
                    const char* name;
                    int32_t dmflag;
                } opts[] = {
                    { "Friendly Fire",              DMFLAG_FRIENDLYFIRE },
                    { "Disable Exits",              DMFLAG_NOEXITS },
                    { "Enable Markers",             DMFLAG_MARKERS },
                    { "Weapons Stay",               DMFLAG_WEAPONSTAY },
                    { "-",                          0 },
                    { NAM_WW2GI ? "Respawn Enemies" : "Respawn Monsters", DMFLAG_RESPAWNMONSTERS },
                    { "Respawn Inventory",          DMFLAG_RESPAWNINVENTORY },
                    { "Respawn Items",              DMFLAG_RESPAWNITEMS },
                    { "-",                          0 },
                    { "Enable Visibility Changes",  DMFLAG_ALLOWVISIBILITYCHANGE },
                    { "Disable FOV Changes",        DMFLAG_NOFOVCHANGE },
                    { "Disable Player IDs",         DMFLAG_NOPLAYERIDS },
                    { "Disable Weapon Icons",       DMFLAG_NOWEAPONICONS },
                    { "Disable DM Weapon Spawns",   DMFLAG_NODMWEAPONSPAWNS },
                    { "Enable V1.3D Double Kick",   DMFLAG_ALLOWDOUBLEKICK },
                    { "Keep Items Across Episodes", DMFLAG_KEEPITEMSACROSSEPISODES },
                    { "-",                          0 },
                    { "Accept",                     0 },
                };

                yy = 37;
                for (ii = io = 0; ii < (int32_t)ARRAY_SIZE(opts); ii++)
                {
                    if (opts[ii].name[0] == '-' && !opts[ii].dmflag)
                    {
                        if (io <= probey)
                            yy += 4;

                        continue;
                    }
                    if (io < probey)
                        yy += 8;

                    io++;
                }

                //onbar = (probey >= 2 && probey <= 5);
                x = probesm(c, yy + 5, 0, io);

                if (x == -1)
                {
                    ChangeToMenu(600);
                    break;
                }

                yy = 37;
                for (ii = io = 0; ii < (int32_t)ARRAY_SIZE(opts); ii++)
                {
                    if (opts[ii].name[0] == '-' && !opts[ii].dmflag)
                    {
                        yy += 4;
                        continue;
                    }

                    bool enabled = true;
                    switch (io)
                    {
                        case 0:
                        {
                            if (!(Gametypes[ud.m_coop].flags & GAMETYPE_PLAYERSFRIENDLY) && !(Gametypes[ud.m_coop].flags & GAMETYPE_TDM))
                                enabled = false;
                            break;
                        }
                        case 1:
                        {
                            if ((Gametypes[ud.m_coop].flags & GAMETYPE_COOP))
                                enabled = false;
                            break;
                        }
                        case 2:
                        {
                            if (!(Gametypes[ud.m_coop].flags & GAMETYPE_MARKEROPTION))
                                enabled = false;
                            break;
                        }
                        case 5:
                        {
                            if (!(Gametypes[ud.m_coop].flags & GAMETYPE_ALLOWITEMRESPAWN))
                                enabled = false;
                            break;
                        }
                        case 6:
                        {
                            if (!(Gametypes[ud.m_coop].flags & GAMETYPE_ALLOWITEMRESPAWN))
                                enabled = false;
                            break;
                        }
                        case 11:
                        {
                            if (!(Gametypes[ud.m_coop].flags & GAMETYPE_COOP))
                                enabled = false;
                            break;
                        }
                        case 13:
                        {
                            if (!(Gametypes[ud.m_coop].flags & GAMETYPE_COOP))
                                enabled = false;
                            break;
                        }
                    }

                    if (ii == (int32_t)ARRAY_SIZE(opts) - 1)
                    {
                        if (x == io)
                            ChangeToMenu(600);
                    }

                    if (enabled && opts[ii].dmflag)
                    {
                        int32_t state = M_DMFLAGS_TEST(opts[ii].dmflag);
                        int32_t old_state = state;

                        if (x == io)
                            state = (bool)!state;

                        modval(0, 1, (int*)&state, 1, probey == io);

                        if(state != old_state)
                            M_DMFLAGS_SET(opts[ii].dmflag, state);

                        mgametextpal(d, yy, state ? "Yes" : "No", MENUHIGHLIGHT(io), 0);
                    }

                    mgametextpal(c, yy, opts[ii].name, enabled ? MENUHIGHLIGHT(io) : DISABLEDMENUSHADE, 10);

                    io++;
                    yy += 8;
                }
            }
            break;
        }
    }

    if (!furyVerifyPrompt)
        Menu_RunDisplayEvent(EVENT_DISPLAYMENUREST);
    Menu_FuryLateVerifyPrompt();
    if (!(FURY && Menu_IsVerifyPrompt(g_currentMenu)))
        Menu_DisplayScriptCursor();

    // The normal HUD path draws DevTools during gameplay. Draw it here too at
    // title/menu screens, where there is no HUD pass yet.
    if ((ud.gm & MODE_GAME) == 0)
        NNBJS_DrawHUD();

    if ((ud.gm&MODE_MENU) != MODE_MENU)
    {
        G_UpdateScreenArea();
        g_cameraClock = (int32_t)totalclock;
        g_cameraDistance = 65536L;
    }
}
