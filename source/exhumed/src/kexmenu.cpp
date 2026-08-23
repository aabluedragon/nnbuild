//-------------------------------------------------------------------------
//
// VibeBuild32: the Nightdive PowerSlave Exhumed menu, drawn in the Build engine.
//
// The remaster's menu is a tiled stone panel over the twin-Anubis backdrop,
// with a gold/blue display font. All of that art lives in PowerslaveEX.kpf, so
// source/exhumed/tools/kexmenu.py gives each piece a fixed tile number in a DEF
// and this file lays them out. Nothing here is drawn from Build PowerSlave's
// own menu art -- if the Kex data is not mounted, KexMenu_Available() says so
// and menu_Menu() keeps the classic menu.
//
// The font needs the most care: Kex ships one atlas plus a .kfont metrics file,
// and rotatesprite can only draw a whole tile, so the tool slices each glyph
// into its own tile and writes the metrics back out as kexfont.txt. This reads
// that, so neither side hardcodes the other's tile numbering.
//
//-------------------------------------------------------------------------
#include "build.h"
#include "compat.h"
#include "baselayer.h"
#include "keyboard.h"
#include "control.h"
#include "kextable.h"

#include "exhumed.h"
#include "engine.h"
#include "menu.h"
#include "sound.h"
#include "names.h"
#include "input.h"
#include "light.h"
#include "view.h"
#include "colmatch.h"
#include "movie.h"
#include "status.h"
#include "kexmenu.h"

// Mirrors the TILE_* constants in source/exhumed/tools/kexmenu.py.
enum
{
    kKexTileBackdrop  = 7000,
    kKexTilePanel     = 7001,
    kKexTileCursor    = 7002,
    kKexTileRow       = 7003,
    kKexTileRowSel    = 7004,
    kKexTileBtn       = 7005,
    kKexTileBtnSel    = 7006,
    kKexTileCursorR   = 7007,
    kKexTileArrowUp   = 7010,
    kKexTileArrowRight= 7011,
    kKexTileArrowDown = 7012,
    kKexTileArrowLeft = 7013,
    kKexTileKeycap    = 7014,
    kKexTileBlack     = 7015,
    kKexTileEyeBase   = 7020,
    kKexFontBase      = 7100,   // gold body / dark outline: menu entries
    kKexFontAlt       = 7300,   // the atlas as shipped: blue body / gold outline
};

#define KEX_FIRSTCHAR 32
#define KEX_LASTCHAR  126
#define KEX_NUMCHARS  (KEX_LASTCHAR - KEX_FIRSTCHAR + 1)

struct kexglyph_t { int16_t tile, w, h, yoff; };

static kexglyph_t s_glyph[KEX_NUMCHARS];
static int  s_state = -1;      // -1 = not probed yet, 0 = unavailable, 1 = ready
static int  s_fontHeight = 76; // tallest glyph in the atlas, for scaling
static char s_chosenMap[32];   // level picked on the overworld, for the game loop

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
static bool KexMenu_ParseGlyph(char *line, int nRec)
{
    UNREFERENCED_PARAMETER(nRec);

    int cp, tile, w, h, yoff;
    if (Bsscanf(line, "%d %d %d %d %d", &cp, &tile, &w, &h, &yoff) != 5)
        return false;
    if (cp < KEX_FIRSTCHAR || cp > KEX_LASTCHAR)
        return false;

    kexglyph_t &g = s_glyph[cp - KEX_FIRSTCHAR];
    g.tile = (int16_t)tile; g.w = (int16_t)w; g.h = (int16_t)h; g.yoff = (int16_t)yoff;
    return true;
}

static void KexMenu_LoadFont(void)
{
    Bmemset(s_glyph, 0, sizeof(s_glyph));

    // The glyphs index themselves by codepoint rather than filling the array in
    // order, so there is no useful cap and the record count says nothing.
    KexTable_Load("kexfont.txt", INT32_MAX, KexMenu_ParseGlyph);

    // The tallest glyph is the line height.
    int most = 0;
    for (auto const &g : s_glyph)
        if (g.h > most) most = g.h;
    if (most) s_fontHeight = most;
}

int KexMenu_Available(void)
{
    // Only when this session is actually playing the remaster's content. The
    // art can be on the search path while the original game is being played --
    // the converted levels and the base game share a directory in a normal
    // setup -- and the classic menu is the right one then.
    if (!g_kexMode)
        return 0;

    if (s_state < 0)
    {
        KexMenu_LoadFont();
        // The DEF has to have actually produced the art, not just been named:
        // a missing .kpf leaves the tiles at 0x0 and everything would draw blank.
        s_state = (tilesiz[kKexTileBackdrop].x > 0 && tilesiz[kKexTilePanel].x > 0
                   && s_glyph['A' - KEX_FIRSTCHAR].tile != 0) ? 1 : 0;
        LOG_F(INFO, "Kex menu: %s", s_state ? "using the remaster's menu art" : "not available, using the classic menu");
    }
    return s_state;
}

// ---------------------------------------------------------------------------
// Drawing helpers. Everything is laid out in Build's 320x200 virtual space.
// ---------------------------------------------------------------------------
#define KEXZ(f) ((int)((f) * 65536.f))

static void KexDrawTile(int tile, int x, int y, int w, int h, int shade, int pal, int stat)
{
    if (tilesiz[tile].x <= 0 || tilesiz[tile].y <= 0)
        return;
    // rotatesprite scales uniformly, so a rectangle that is not the tile's own
    // aspect has to be drawn with the two axes reconciled by hand; the panel is
    // the only thing that needs it and it is a noise texture, so scale to fill.
    int const zx = divscale16(w, tilesiz[tile].x);
    int const zy = divscale16(h, tilesiz[tile].y);
    int const z  = min(zx, zy);
    rotatesprite(x << 16, y << 16, z, 0, tile, shade, pal, stat | RS_TOPLEFT | RS_AUTO,
                 0, 0, xdim - 1, ydim - 1);
}

// The stone panel is a 64x64 swatch; tile it so the grain stays the right size
// instead of stretching one copy over the whole panel. Cells are square and
// always full-size -- a partial cell would be drawn square by rotatesprite's
// uniform scale and leave a notch -- so the last row and column are pulled back
// to the panel edge and simply overlap. The swatch is noise; the seam is not
// visible, whereas the notches were.
static void KexDrawTiled(int tile, int x0, int y0, int x1, int y1, int cell, int stat)
{
    if (x1 <= x0 || y1 <= y0)
        return;
    cell = min(cell, min(x1 - x0, y1 - y0));
    if (cell <= 0)
        return;
    for (int y = y0; y < y1; y += cell)
    {
        int const py = min(y, y1 - cell);
        for (int x = x0; x < x1; x += cell)
            KexDrawTile(tile, min(x, x1 - cell), py, cell, cell, 0, 0, stat);
    }
}

static void KexDrawPanel(int x0, int y0, int x1, int y1, int shade)
{
    UNREFERENCED_PARAMETER(shade);
    KexDrawTiled(kKexTilePanel, x0, y0, x1, y1, 32, 0);
}

// The remaster sets every panel in a black surround, which is what lifts it off
// the artwork behind. Drawn as flat tiles rather than by touching the
// framebuffer, so it works under both renderers.
static void KexDrawBorder(int x0, int y0, int x1, int y1, int width)
{
    KexDrawTiled(kKexTileBlack, x0 - width, y0 - width, x1 + width, y1 + width, 24, 0);
}

int KexMenu_TextWidth(const char *str, int height)
{
    int w = 0;
    for (const char *p = str; *p; p++)
    {
        unsigned char c = (unsigned char)Btoupper((unsigned char)*p);
        if (c < KEX_FIRSTCHAR || c > KEX_LASTCHAR) c = ' ';
        kexglyph_t const &g = s_glyph[c - KEX_FIRSTCHAR];
        w += g.w ? scale(g.w, height, s_fontHeight) : height / 3;
    }
    return w;
}

// <x> is the left edge unless centred; <y> is the text's top.
void KexMenu_Text(int x, int y, const char *str, int height, int shade, int pal, int centred, int altFont)
{
    if (centred)
        x -= KexMenu_TextWidth(str, height) / 2;

    int const z = divscale16(height, s_fontHeight);
    for (const char *p = str; *p; p++)
    {
        unsigned char c = (unsigned char)Btoupper((unsigned char)*p);
        if (c < KEX_FIRSTCHAR || c > KEX_LASTCHAR) c = ' ';
        kexglyph_t const &g = s_glyph[c - KEX_FIRSTCHAR];
        if (!g.w) { x += height / 3; continue; }
        int const tile = altFont ? (kKexFontAlt + (c - KEX_FIRSTCHAR)) : g.tile;
        if (tile > 0 && tilesiz[tile].x > 0)
            rotatesprite(x << 16, y << 16, z, 0, tile, shade, pal,
                         RS_TOPLEFT | RS_AUTO, 0, 0, xdim - 1, ydim - 1);
        x += scale(g.w, height, s_fontHeight);
    }
}

void KexMenu_Backdrop(void)
{
    int const tw = tilesiz[kKexTileBackdrop].x, th = tilesiz[kKexTileBackdrop].y;
    if (tw <= 0 || th <= 0)
        return;
    // Cover the 320x200 field rather than fit inside it: the backdrop is 16:9,
    // so fitting would letterbox it. Scale by whichever axis needs more and
    // centre, letting the overflow fall off the sides.
    int const z = max(divscale16(320, tw), divscale16(200, th));
    int const w = mulscale16(tw, z), h = mulscale16(th, z);
    rotatesprite((160 - w / 2) << 16, (100 - h / 2) << 16, z, 0, kKexTileBackdrop, 0, 0,
                 RS_TOPLEFT | RS_AUTO | RS_NOCLIP, 0, 0, xdim - 1, ydim - 1);
}

// ---------------------------------------------------------------------------
// Layout. Proportions follow the remaster's own menu: a centred stone panel
// with a titled header row, evenly spaced entries, and the gold statue cursor
// facing in from both sides of the highlighted row.
// ---------------------------------------------------------------------------
// Must match ROW_W/ROW_H and BTN_W/BTN_H in source/exhumed/tools/kexmenu.py:
// the plates are authored at exactly this size so rotatesprite's uniform scale
// lands 1:1 instead of letterboxing them.
#define KEX_ROW_W      134
#define KEX_ROW_H      18
#define KEX_BTN_W      44
#define KEX_BTN_H      13
#define KEX_PANEL_X0   ((320 - KEX_ROW_W) / 2 - 5)
#define KEX_PANEL_X1   (KEX_PANEL_X0 + KEX_ROW_W + 10)
#define KEX_TEXT_H     12
#define KEX_PAD        6
#define KEX_HEADGAP    12   // the reference leaves a clear break under the title row

// One row of the panel: the remaster's button plate, red when selected, with
// the label centred on it. Returns the row's bottom edge so callers stack rows.
static int KexMenu_Row(int y, const char *label, int selected, int enabled, int altFont = 0, int leftAlign = 0)
{
    int const x = KEX_PANEL_X0 + KEX_PAD;
    KexDrawTile(selected ? kKexTileRowSel : kKexTileRow, x, y, KEX_ROW_W, KEX_ROW_H, 0, 0, 0);
    int const ty = y + (KEX_ROW_H - KEX_TEXT_H) / 2 + 1;
    if (leftAlign)
        KexMenu_Text(x + 6, ty, label, KEX_TEXT_H, enabled ? 0 : 22, 0, 0, altFont);
    else
        KexMenu_Text(x + KEX_ROW_W / 2, ty, label, KEX_TEXT_H, enabled ? 0 : 22, 0, 1, altFont);
    return y + KEX_ROW_H;
}

static void KexMenu_Cursor(int rowY)
{
    if (tilesiz[kKexTileCursor].y <= 0)
        return;
    int const h = 13, w = scale(h, tilesiz[kKexTileCursor].x, tilesiz[kKexTileCursor].y);
    int const y = rowY + (KEX_ROW_H - h) / 2;
    KexDrawTile(kKexTileCursor,  KEX_PANEL_X0 - w - 1, y, w, h, 0, 0, 0);
    KexDrawTile(kKexTileCursorR, KEX_PANEL_X1 + 1,     y, w, h, 0, 0, 0);

}

// The hint bar is a black band with the key drawn in a cap, the way the
// remaster labels its controls.
static void KexMenu_Hint(const char *key1, const char *label1,
                         const char *key2 = NULL, const char *label2 = NULL)
{
    KexDrawTiled(kKexTileBlack, 0, 184, 320, 200, 16, 0);

    int x = 6;
    for (int i = 0; i < 2; i++)
    {
        const char *k = i ? key2 : key1, *l = i ? label2 : label1;
        if (!k || !l)
            break;
        int const kw = max(18, KexMenu_TextWidth(k, 6) + 8);
        KexDrawTile(kKexTileKeycap, x, 187, kw, 11, 0, 0, 0);
        KexMenu_Text(x + kw / 2, 189, k, 6, 0, 0, 1, 1);
        x += kw + 5;
        KexMenu_Text(x, 188, l, 8, 0, 0, 0);
        x += KexMenu_TextWidth(l, 8) + 12;
    }
}

// A framed panel sized to hold <rows> rows plus a header, centred vertically.
static int KexMenu_PanelHeight(int rows)
{
    return KEX_ROW_H * (rows + 1) + KEX_HEADGAP + KEX_PAD * 2;
}

static int KexMenu_PanelTop(int rows)
{
    return (200 - KexMenu_PanelHeight(rows)) / 2;
}

static void KexMenu_Frame(int top, int rows)
{
    int const bottom = top + KexMenu_PanelHeight(rows);
    KexDrawBorder(KEX_PANEL_X0, top, KEX_PANEL_X1, bottom, 5);
    KexDrawPanel(KEX_PANEL_X0, top, KEX_PANEL_X1, bottom, 0);
}

// ---------------------------------------------------------------------------
// Input. The classic menu drives itself straight off KB_KeyDown, so this does
// too rather than introducing a second input path into the same loop.
// ---------------------------------------------------------------------------
enum { kKexKeyNone = 0, kKexKeyUp, kKexKeyDown, kKexKeyLeft, kKexKeyRight, kKexKeyEnter, kKexKeyEsc };

static int KexMenu_PollKey(void)
{
    static int held = 0;
    int key = kKexKeyNone;

    if (KB_KeyDown[sc_UpArrow] || KB_KeyDown[sc_kpad_8])      key = kKexKeyUp;
    else if (KB_KeyDown[sc_DownArrow] || KB_KeyDown[sc_kpad_2]) key = kKexKeyDown;
    else if (KB_KeyDown[sc_LeftArrow] || KB_KeyDown[sc_kpad_4]) key = kKexKeyLeft;
    else if (KB_KeyDown[sc_RightArrow] || KB_KeyDown[sc_kpad_6]) key = kKexKeyRight;
    else if (KB_KeyDown[sc_Enter] || KB_KeyDown[sc_kpad_Enter] || KB_KeyDown[sc_Space]) key = kKexKeyEnter;
    else if (KB_KeyDown[sc_Escape])                           key = kKexKeyEsc;

    if (key == kKexKeyNone) { held = 0; return kKexKeyNone; }
    if (key == held) return kKexKeyNone;      // edge-triggered: one step per press
    held = key;
    return key;
}

static void KexMenu_Beep(void) { PlayLocalSound(StaticSound[kSoundSwitchFoot], 0); }

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
static int KexMenu_Confirm(const char *line1, const char *line2)
{
    int no = 1;
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();

        int const x0 = 42, x1 = 278;
        int const top = 68, bottom = 142;
        KexDrawPanel(x0, top, x1, bottom, 0);
        // The message plate spans the panel's inner width so a two-line
        // question sits inside it rather than spilling over the edges.
        KexDrawTile(kKexTileRow, x0 + 8, top + 8, x1 - x0 - 16, 34, 0, 0, 0);
        KexMenu_Text((x0 + x1) / 2, top + 12, line1, 10, 0, 0, 1);
        if (line2 && line2[0])
            KexMenu_Text((x0 + x1) / 2, top + 25, line2, 10, 0, 0, 1);

        int const by = top + 52;
        int const lx = x0 + 12, rx = x1 - KEX_BTN_W - 12;
        KexDrawTile(no ? kKexTileBtn : kKexTileBtnSel, lx, by, KEX_BTN_W, KEX_BTN_H, 0, 0, 0);
        KexDrawTile(no ? kKexTileBtnSel : kKexTileBtn, rx, by, KEX_BTN_W, KEX_BTN_H, 0, 0, 0);
        KexMenu_Text(lx + KEX_BTN_W / 2, by + 2, "YES", 9, 0, 0, 1);
        KexMenu_Text(rx + KEX_BTN_W / 2, by + 2, "NO",  9, 0, 0, 1);

        KexMenu_Hint("ENTER", "SELECT ITEM");
        videoNextPage();
        Exhumed_CaptureFrame();

        switch (KexMenu_PollKey())
        {
            case kKexKeyLeft:  case kKexKeyRight: no ^= 1; KexMenu_Beep(); break;
            case kKexKeyEnter: KexMenu_Beep(); return !no;
            case kKexKeyEsc:   KexMenu_Beep(); return 0;
            default: break;
        }
        if (quitevent) return 1;
    }
}

// The remaster's New Game / Load Game screens are a list of save slots; this is
// the same list the classic menu drives, drawn the remaster's way.
static int KexMenu_SlotList(const char *title)
{
    int sel = 0;
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();

        int const top = KexMenu_PanelTop(6);
        KexMenu_Frame(top, 6);
        int y = top + KEX_PAD;
        KexMenu_Row(y, title, 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        for (int i = 0; i < 5; i++)
        {
            // The remaster leaves the slot labels in the font's shipped colours
            // and left-aligns them, which is what tells them apart from the
            // headers at a glance.
            char label[32];
            Bsnprintf(label, sizeof(label), "EMPTY SLOT %d", i + 1);
            KexMenu_Row(y, label, sel == i, 1, 1, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }
        KexMenu_Row(y, "GO BACK", sel == 5, 1);
        if (sel == 5) KexMenu_Cursor(y);

        KexMenu_Hint("ENTER", "SELECT ITEM");
        videoNextPage();
        Exhumed_CaptureFrame();

        switch (KexMenu_PollKey())
        {
            case kKexKeyUp:    sel = (sel + 5) % 6; KexMenu_Beep(); break;
            case kKexKeyDown:  sel = (sel + 1) % 6; KexMenu_Beep(); break;
            case kKexKeyEnter: KexMenu_Beep(); return sel == 5 ? -1 : sel;
            case kKexKeyEsc:   KexMenu_Beep(); return -1;
            default: break;
        }
        if (quitevent) return -1;
    }
}

static void KexMenu_Credits(void)
{
    static char const * const lines[] = {
        "POWERSLAVE / EXHUMED",
        "LOBOTOMY SOFTWARE 1996",
        "",
        "REMASTER ART AND LEVELS",
        "NIGHTDIVE STUDIOS",
        "",
        "BUILD ENGINE PORT",
        "VIBEBUILD32",
    };
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const rows = (int)ARRAY_SIZE(lines);
        int const top = KexMenu_PanelTop(rows);
        KexMenu_Frame(top, rows);
        int y = top + KEX_PAD;
        KexMenu_Row(y, "CREDITS", 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < rows; i++)
        {
            if (lines[i][0]) KexMenu_Text(160, y + 2, lines[i], 10, 0, 0, 1);
            y += KEX_ROW_H;
        }
        KexMenu_Hint("ESC", "GO BACK");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const k = KexMenu_PollKey();
        if (k == kKexKeyEsc || k == kKexKeyEnter) { KexMenu_Beep(); return; }
        if (quitevent) return;
    }
}

// ---------------------------------------------------------------------------
// The main menu. Drop-in for menu_Menu(): same return codes, so exhumed.cpp's
// dispatch does not care which menu produced them.
//   -1 back, 0 quit, 1 new game (SavePosition set), 2 load game, 3 training
// ---------------------------------------------------------------------------
enum { kKexNewGame = 0, kKexLoadGame, kKexOptions, kKexCredits, kKexQuit, kKexItems };

static int KexMenu_Pause(void);   // the in-level menu, defined below

int KexMenu_Main(int bInLevelMenus)
{
    static char const * const labels[kKexItems] =
        { "NEW GAME", "LOAD GAME", "OPTIONS", "CREDITS", "QUIT" };

    GrabPalette();
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);
    KB_KeyDown[sc_Escape] = 0;
    StopAllSounds();
    StopLocalSound();
    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();

    // In a level this is the pause menu, not the front end.
    if (bInLevelMenus)
        return KexMenu_Pause();

    int enabled[kKexItems] = { 1, 1, 1, 1, 1 };
    if (nNetPlayerCount) { enabled[kKexNewGame] = 0; enabled[kKexLoadGame] = 0; }

    int sel = 0;
    while (!enabled[sel]) sel = (sel + 1) % kKexItems;


    while (1)
    {
        HandleAsync();
        OSD_DispatchQueued();

        KexMenu_Backdrop();

        int const top = KexMenu_PanelTop(kKexItems);
        KexMenu_Frame(top, kKexItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, "POWERSLAVE", 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < kKexItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, enabled[i]);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("ENTER", "SELECT ITEM");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            int const step = (key == kKexKeyUp) ? kKexItems - 1 : 1;
            do sel = (sel + step) % kKexItems; while (!enabled[sel]);
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            if (bInLevelMenus) return -1;
        }
        else if (key == kKexKeyEnter)
        {
            KexMenu_Beep();
            switch (sel)
            {
                case kKexNewGame:
                {
                    if (!enabled[sel]) break;
                    SavePosition = (short)KexMenu_SlotList("NEW GAME");
                    if (SavePosition == -1) break;
                    // The remaster picks the level off its overworld map, not
                    // straight into level 1.
                    if (KexMenu_LevelCount() > 0)
                    {
                        int const pick = KexMenu_Overworld(0);
                        if (pick < 0) break;
                        Bstrncpyz(s_chosenMap, KexMenu_LevelMap(pick), sizeof(s_chosenMap));
                    }
                    FadeOut(1);
                    StopAllSounds();
                    MySetView(nViewLeft, nViewTop, nViewRight, nViewBottom);
                    return 1;
                }
                case kKexLoadGame:
                {
                    if (!enabled[sel]) break;
                    SavePosition = (short)KexMenu_SlotList("LOAD GAME");
                    if (SavePosition == -1) break;
                    StopAllSounds();
                    MySetView(nViewLeft, nViewTop, nViewRight, nViewBottom);
                    return 2;
                }
                case kKexOptions:  menu_AdjustVolume(); break;
                case kKexCredits:  KexMenu_Credits(); break;
                case kKexQuit:
                    if (KexMenu_Confirm("ARE YOU SURE YOU WISH TO", "QUIT THE GAME?"))
                    {
                        StopAllSounds();
                        MySetView(nViewLeft, nViewTop, nViewRight, nViewBottom);
                        return 0;
                    }
                    break;
            }
            KB_FlushKeyboardQueue();
            KB_ClearKeysDown();
        }

        if (quitevent) return 0;
    }
}

// ---------------------------------------------------------------------------
// Pause menu. The remaster's in-level menu is the same stone panel over
// whatever is behind it, with its own entries. menu_Menu(1) is what the game
// calls for it, so this is what that returns into.
//   -1 resume, 0 quit, 4 exit to title
// ---------------------------------------------------------------------------
enum { kKexPauseResume = 0, kKexPauseOptions, kKexPauseExit, kKexPauseQuit, kKexPauseItems };

static int KexMenu_Pause(void)
{
    static char const * const labels[kKexPauseItems] =
        { "RESUME", "OPTIONS", "EXIT TO TITLE SCREEN", "QUIT" };

    int sel = 0;
    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();

    while (1)
    {
        HandleAsync();
        OSD_DispatchQueued();

        int const top = KexMenu_PanelTop(kKexPauseItems);
        KexMenu_Frame(top, kKexPauseItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, "GAME PAUSED", 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < kKexPauseItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("ENTER", "SELECT ITEM");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexPauseItems - 1 : 1)) % kKexPauseItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return -1;
        }
        else if (key == kKexKeyEnter)
        {
            KexMenu_Beep();
            switch (sel)
            {
                case kKexPauseResume: return -1;
                case kKexPauseOptions: menu_AdjustVolume(); break;
                case kKexPauseExit:
                    if (KexMenu_Confirm("ARE YOU SURE YOU WISH TO", "EXIT TO THE TITLE SCREEN?"))
                        return 4;
                    break;
                case kKexPauseQuit:
                    if (KexMenu_Confirm("ARE YOU SURE YOU WISH TO", "QUIT THE GAME?"))
                        return 0;
                    break;
            }
            KB_FlushKeyboardQueue();
            KB_ClearKeysDown();
        }
        if (quitevent) return 0;
    }
}

// ---------------------------------------------------------------------------
// Title screen. The remaster opens on the book cinematic -- the same one this
// port already ships as book.mov -- and then holds on the backdrop waiting for
// a key, rather than the original's publisher logos and skull animation.
// ---------------------------------------------------------------------------
void KexMenu_TitleHold(void)
{
    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    GrabPalette();
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    int const start = (int)totalclock;
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();

        // A slow pulse, so the prompt reads as an invitation rather than a
        // static label. Sin() is the engine's own fixed-point table.
        int const shade = (Sin(((int)totalclock - start) << 5) >> 11) - 4;
        KexMenu_Text(160, 150, "PRESS START", 16, (int8_t)shade, 0, 1);

        KexMenu_Text(160, 182, "POWERSLAVE AND POWERSLAVE EXHUMED ARE TRADEMARKS", 5, 0, 0, 1);
        KexMenu_Text(160, 189, "OF THROWBACK ENTERTAINMENT INC", 5, 0, 0, 1);

        videoNextPage();
        Exhumed_CaptureFrame();

        if (KB_KeyWaiting() || KB_KeyDown[sc_Enter] || KB_KeyDown[sc_Space] || KB_KeyDown[sc_Escape])
        {
            KexMenu_Beep();
            KB_FlushKeyboardQueue();
            KB_ClearKeysDown();
            return;
        }
        if (quitevent)
            return;
    }
}

// ---------------------------------------------------------------------------
// Overworld level select.
//
// The remaster picks levels off a hand-painted map rather than from a list:
// each level sits at a point on it, joined to its neighbours by compass
// directions, and the arrow keys walk that graph. kexmenu.py writes the map's
// positions, links and display names out of the remaster's own mapInfo.txt and
// localization table, so nothing about the campaign is hardcoded here.
// ---------------------------------------------------------------------------
enum { kKexTileOverworld = 7008, kKexTileMarker = 7009, kKexTileArrow = 7010 };

#define KEX_MAX_LEVELS 64

struct kexlevel_t
{
    char map[32];
    char title[48];
    int16_t x, y;
    int16_t link[4];      // north, east, south, west
};

static kexlevel_t s_level[KEX_MAX_LEVELS];
static int s_levelCount = -1;
static int s_eyeFrames = 0;

static bool KexMenu_ParseLevel(char *line, int nRec)
{
    kexlevel_t &L = s_level[nRec];
    int x, y, n, e, so, w, used = 0;
    if (Bsscanf(line, "%31s %d %d %d %d %d %d %n", L.map, &x, &y, &n, &e, &so, &w, &used) < 7)
        return false;

    L.x = (int16_t)x; L.y = (int16_t)y;
    L.link[0] = (int16_t)n; L.link[1] = (int16_t)e;
    L.link[2] = (int16_t)so; L.link[3] = (int16_t)w;
    Bstrncpyz(L.title, used > 0 ? line + used : "", sizeof(L.title));
    return true;
}

static void KexMenu_LoadWorld(void)
{
    s_levelCount = KexTable_Load("kexworld.txt", KEX_MAX_LEVELS, KexMenu_ParseLevel);

    // How many frames the eye's spin has; the tool writes it out beside the art.
    s_eyeFrames = 0;
    for (int i = 0; i < 64; i++)
    {
        if (tilesiz[kKexTileEyeBase + i].x <= 0)
            break;
        s_eyeFrames = i + 1;
    }
    LOG_F(INFO, "Kex overworld: %d levels, %d eye frames", s_levelCount, s_eyeFrames);
}

// Returns the chosen level's index, or -1 to go back to the menu.
int KexMenu_Overworld(int start)
{
    if (s_levelCount < 0)
        KexMenu_LoadWorld();
    if (s_levelCount <= 0 || tilesiz[kKexTileOverworld].x <= 0)
        return -1;

    int cur = clamp(start, 0, s_levelCount - 1);

    int const mw = tilesiz[kKexTileOverworld].x, mh = tilesiz[kKexTileOverworld].y;
    // The remaster frames the map close in -- roughly a fifth of it across the
    // screen -- rather than fitting the whole thing, so this is a fixed zoom on
    // the art's own scale rather than a fit-to-screen.
    int const z = divscale16(320, mw * 10 / 27);

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();

    while (1)
    {
        HandleAsync();

        kexlevel_t const &L = s_level[cur];
        int const px = mulscale16(L.x, z), py = mulscale16(L.y, z);
        int ox = clamp(px - 160, 0, max(0, mulscale16(mw, z) - 320));
        int oy = clamp(py - 100, 0, max(0, mulscale16(mh, z) - 200));

        rotatesprite((-ox) << 16, (-oy) << 16, z, 0, kKexTileOverworld, 0, 0,
                     RS_TOPLEFT | RS_AUTO | RS_NOCLIP, 0, 0, xdim - 1, ydim - 1);

        // Neighbours first, so the marker sits on top of its own arrows.
        static int const adx[4] = { 0, 1, 0, -1 }, ady[4] = { -1, 0, 1, 0 };
        static int const atile[4] = { kKexTileArrowUp, kKexTileArrowRight,
                                      kKexTileArrowDown, kKexTileArrowLeft };
        for (int d = 0; d < 4; d++)
        {
            if (L.link[d] < 0 || L.link[d] >= s_levelCount)
                continue;
            int const t = atile[d];
            if (tilesiz[t].x <= 0)
                continue;
            int const aw = tilesiz[t].x / 5, ah = tilesiz[t].y / 5;
            KexDrawTile(t, px - ox + adx[d] * 21 - aw / 2, py - oy + ady[d] * 18 - ah / 2,
                        aw, ah, 0, 0, 0);
        }

        // The marker is the Eye of Ra, spinning: one tile per frame of the
        // remaster's own 31-frame cycle, stepped off the game clock.
        int const frame = s_eyeFrames > 0 ? (((int)totalclock / 3) % s_eyeFrames) : -1;
        int const eye = frame >= 0 ? kKexTileEyeBase + frame : kKexTileMarker;
        if (tilesiz[eye].x > 0)
        {
            int const ew = tilesiz[eye].x / 5, eh = tilesiz[eye].y / 5;
            KexDrawTile(eye, px - ox - ew / 2, py - oy - eh / 2, ew, eh, 0, 0, 0);
        }

        // Title banner: a dark band across the top with the name in the font's
        // shipped blue-bodied colours, not the menu's gold.
        int const bw = KexMenu_TextWidth(L.title, 18) + 40;
        KexDrawTiled(kKexTileBlack, 160 - bw / 2, 16, 160 + bw / 2, 44, 28, RS_TRANS1);
        KexMenu_Text(160, 21, L.title, 18, 0, 0, 1, 1);

        KexMenu_Hint("ENTER", "SELECT ITEM", "ESC", "OPEN MENU");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        int dir = -1;
        switch (key)
        {
            case kKexKeyUp:    dir = 0; break;
            case kKexKeyRight: dir = 1; break;
            case kKexKeyDown:  dir = 2; break;
            case kKexKeyLeft:  dir = 3; break;
            case kKexKeyEnter: KexMenu_Beep(); return cur;
            case kKexKeyEsc:   KexMenu_Beep(); return -1;
            default: break;
        }
        if (dir >= 0 && L.link[dir] >= 0 && L.link[dir] < s_levelCount)
        {
            cur = L.link[dir];
            KexMenu_Beep();
        }
        if (quitevent)
            return -1;
    }
}

int KexMenu_LevelCount(void)
{
    if (s_levelCount < 0)
        KexMenu_LoadWorld();
    return s_levelCount;
}

const char *KexMenu_LevelMap(int i)
{
    return (unsigned)i < (unsigned)s_levelCount ? s_level[i].map : NULL;
}

// The level the player picked on the overworld, or NULL if they did not get
// that far. The game loop reads this instead of starting at level 1.
const char *KexMenu_ChosenMap(void)
{
    return s_chosenMap[0] ? s_chosenMap : NULL;
}

// ---------------------------------------------------------------------------
// The remaster's status bar: two Anubis heads at the ends, an air bar and a
// health bar between them, in place of Build PowerSlave's ornate panel. The
// pieces ship separately in the .kpf, so they are laid out here rather than
// blitted as one strip.
// ---------------------------------------------------------------------------
enum
{
    kKexTileHudHeadL = 7060, kKexTileHudHeadR = 7061,
    kKexTileHudMainL = 7062, kKexTileHudMainR = 7063,
    kKexTileHudRed   = 7064, kKexTileHudBlue  = 7065,
    kKexTileHudScarab= 7066, kKexTileHudTrough= 7067,
};

// The remaster's bar is a 416-unit-wide assembly stretched to the screen:
//   head(40) scarab(32) main_left(136) main_right(136) scarab(32) head(40)
// At 320 units wide that is 30 / 25 / 105 / 105 / 25 / 30, which puts the jewel
// where the two frames meet on the exact horizontal centre -- as it is in the
// remaster. The band is 16 tall and the heads stand 32, overhanging it.
#define KEX_HUD_BAND_H  16
#define KEX_HUD_HEAD_H  32
#define KEX_HUD_HEAD_W  30
#define KEX_HUD_SCAR_W  25
#define KEX_HUD_MAIN_W  105

// The bar slot is cut out of the frame art: alpha 0 at x 31..117 of 136, rows
// 6..9 of 24. Scaled into the drawn 105x16 frame that is x+24 wide 66, four
// rows down, three tall.
#define KEX_HUD_SLOT_W  66
#define KEX_HUD_SLOT_H  3
#define KEX_HUD_SLOT_Y  (200 - KEX_HUD_BAND_H + 4)

// Flat fills have to be tiled rather than stretched: KexDrawTile reconciles the
// two axes by taking the smaller scale, so a square swatch asked for a 66x3
// rectangle would come out 3 wide. Square cells the height of the slot give the
// run its real length, and the overlap of the pulled-back last cell is
// invisible in a flat colour.
static void KexHud_Bar(int x, int tile, int pct)
{
    KexDrawTiled(kKexTileHudTrough, x, KEX_HUD_SLOT_Y, x + KEX_HUD_SLOT_W,
                 KEX_HUD_SLOT_Y + KEX_HUD_SLOT_H, KEX_HUD_SLOT_H, 0);
    int const w = (KEX_HUD_SLOT_W * pct) / 100;
    if (w > 0)
        KexDrawTiled(tile, x, KEX_HUD_SLOT_Y, x + w,
                     KEX_HUD_SLOT_Y + KEX_HUD_SLOT_H, KEX_HUD_SLOT_H, 0);
}

extern short airframe;      // status.cpp: current air-meter frame

int KexHud_Available(void)
{
    return KexMenu_Available() && tilesiz[kKexTileHudMainL].x > 0;
}

void KexHud_Draw(void)
{
    if (!KexHud_Available())
        return;

    int const bandY = 200 - KEX_HUD_BAND_H;
    int const headY = 200 - KEX_HUD_HEAD_H;
    int const lmain = KEX_HUD_HEAD_W + KEX_HUD_SCAR_W;   //  55
    int const rmain = lmain + KEX_HUD_MAIN_W;            // 160, the centre
    int const rscar = rmain + KEX_HUD_MAIN_W;            // 265

    int const air   = clamp((int)airframe * 100 / 16, 0, 100);
    int const hp    = PlayerList[nLocalPlayer].nHealth;
    int const hpPct = clamp((hp * 100) / kMaxHealth, 0, 100);

    // Fills first: the slot is a hole in the frame, so the frame laid over them
    // clips each bar to the slot's slanted end for free. Air on the left,
    // health on the right, the way the remaster reads them.
    KexHud_Bar(lmain + 24, kKexTileHudBlue, air);
    KexHud_Bar(rmain + 28, kKexTileHudRed,  hpPct);

    KexDrawTile(kKexTileHudMainL, lmain, bandY, KEX_HUD_MAIN_W, KEX_HUD_BAND_H, 0, 0, 0);
    KexDrawTile(kKexTileHudMainR, rmain, bandY, KEX_HUD_MAIN_W, KEX_HUD_BAND_H, 0, 0, 0);

    KexDrawTile(kKexTileHudScarab, KEX_HUD_HEAD_W, bandY, KEX_HUD_SCAR_W, KEX_HUD_BAND_H, 0, 0, 0);
    KexDrawTile(kKexTileHudScarab, rscar,          bandY, KEX_HUD_SCAR_W, KEX_HUD_BAND_H, 0, 0, 0);

    // Heads last: they overhang the band, and their surround is transparent so
    // the view shows through beside them, as it does in the remaster.
    KexDrawTile(kKexTileHudHeadL, 0, headY, KEX_HUD_HEAD_W, KEX_HUD_HEAD_H, 0, 0, 0);
    KexDrawTile(kKexTileHudHeadR, rscar + KEX_HUD_SCAR_W, headY,
                KEX_HUD_HEAD_W, KEX_HUD_HEAD_H, 0, 0, 0);
}
