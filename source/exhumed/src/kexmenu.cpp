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
#include "joystick.h"
#include "kextable.h"

#include "exhumed.h"
#include "engine.h"
#include "menu.h"
#include "config.h"      // gamefunc_* controller action IDs
#include "sound.h"
#include "names.h"
#include "input.h"
#include "light.h"
#include "view.h"
#include "colmatch.h"
#include "movie.h"
#include "cd.h"
#include "gun.h"
#include "status.h"
#include "osdcmds.h"
#include "kexmenu.h"
#include "kexinv.h"
#include "init.h"        // SectFlag, for the air gauge
#include "ramses.h"      // KexLanguage_Index/Code
#include "kextactile.h"

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
    kKexTileTitle     = 7016,   // the console title card, behind PRESS START
    kKexTileEyeBase   = 7020,
    kKexFontBase      = 7100,   // legacy ASCII fallback tile range
    kKexFontAlt       = 7300,
};

#define KEX_MAX_GLYPHS 128

struct kexglyph_t { int32_t codepoint; int16_t tile, alttile, w, h, yoff; };

static kexglyph_t s_glyph[KEX_MAX_GLYPHS];
static int s_glyphCount;
static int  s_state = -1;      // -1 = not probed yet, 0 = unavailable, 1 = ready
static int  s_fontHeight = 76; // tallest glyph in the atlas, for scaling
static char s_chosenMap[32];   // level picked on the overworld, for the game loop
int kex_original_overworld = 0;
static void KexMenu_SaveSlotLabel(int nSlot, char *label, int labelSize);
static char const *KexMenu_InvText(char const *key, char const *fallback);
static int KexMenu_ConfirmLocalized(char const *key, char const *fallback1,
                                    char const *fallback2);

static kexglyph_t const *KexMenu_Glyph(int codepoint)
{
    for (int i = 0; i < s_glyphCount; i++)
        if (s_glyph[i].codepoint == codepoint)
            return &s_glyph[i];
    return NULL;
}

// kexmenu.py writes UTF-8 source strings.  Keep invalid sequences harmless:
// consume one byte and draw the space glyph rather than desynchronizing the
// rest of the menu line.
static int KexMenu_NextCodepoint(char const **pText)
{
    uint8_t const *p = (uint8_t const *)*pText;
    int codepoint = ' ';
    if (p[0] < 0x80)
    {
        codepoint = *p++;
    }
    else if ((p[0] & 0xe0) == 0xc0 && p[1] && (p[1] & 0xc0) == 0x80)
    {
        codepoint = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        p += 2;
    }
    else if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2] && (p[1] & 0xc0) == 0x80
             && (p[2] & 0xc0) == 0x80)
    {
        codepoint = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        p += 3;
    }
    else if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3] && (p[1] & 0xc0) == 0x80
             && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80)
    {
        codepoint = ((p[0] & 7) << 18) | ((p[1] & 0x3f) << 12)
                  | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        p += 4;
    }
    else
    {
        p++;
    }
    *pText = (char const *)p;
    return codepoint;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
static bool KexMenu_ParseGlyph(char *line, int nRec)
{
    UNREFERENCED_PARAMETER(nRec);

    int cp, tile, alttile, w, h, yoff;
    int const fields = Bsscanf(line, "%d %d %d %d %d %d",
                               &cp, &tile, &alttile, &w, &h, &yoff);
    if (fields == 5) // Old conversions: cp tile width height yoffset.
    {
        yoff = h;
        h = w;
        w = alttile;
        alttile = cp >= 32 && cp <= 126 ? kKexFontAlt + cp - 32 : tile;
    }
    else if (fields != 6)
        return false;

    kexglyph_t *g = NULL;
    for (int i = 0; i < s_glyphCount; i++)
        if (s_glyph[i].codepoint == cp)
        {
            g = &s_glyph[i];
            break;
        }
    if (!g)
    {
        if (s_glyphCount >= KEX_MAX_GLYPHS)
            return false;
        g = &s_glyph[s_glyphCount++];
    }
    g->codepoint = cp;
    g->tile = (int16_t)tile; g->alttile = (int16_t)alttile;
    g->w = (int16_t)w; g->h = (int16_t)h; g->yoff = (int16_t)yoff;
    return true;
}

static void KexMenu_LoadFont(void)
{
    Bmemset(s_glyph, 0, sizeof(s_glyph));
    s_glyphCount = 0;

    // The glyphs index themselves by codepoint rather than filling the array in
    // order, so there is no useful cap and the record count says nothing.
    KexTable_Load("kexfont.txt", INT32_MAX, KexMenu_ParseGlyph);

    // The tallest glyph is the line height.
    int most = 0;
    for (int i = 0; i < s_glyphCount; i++)
        if (s_glyph[i].h > most) most = s_glyph[i].h;
    if (most) s_fontHeight = most;
}

static void KexMenu_RunBindingSelftest(void);

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
                   && KexMenu_Glyph('A') && KexMenu_Glyph('A')->tile != 0) ? 1 : 0;
        LOG_F(INFO, "Kex menu: %s", s_state ? "using the remaster's menu art" : "not available, using the classic menu");
    }
    if (s_state)
        KexMenu_RunBindingSelftest();
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
    for (const char *p = str; *p; )
    {
        int cp = KexMenu_NextCodepoint(&p);
        if (cp < 128)
            cp = Btoupper(cp);
        kexglyph_t const *g = KexMenu_Glyph(cp);
        if (!g)
            g = KexMenu_Glyph(' ');
        w += g && g->w ? scale(g->w, height, s_fontHeight) : height / 3;
    }
    return w;
}

// <x> is the left edge unless centred; <y> is the text's top.
void KexMenu_Text(int x, int y, const char *str, int height, int shade, int pal, int centred, int altFont)
{
    if (centred)
        x -= KexMenu_TextWidth(str, height) / 2;

    int const z = divscale16(height, s_fontHeight);
    for (const char *p = str; *p; )
    {
        int cp = KexMenu_NextCodepoint(&p);
        if (cp < 128)
            cp = Btoupper(cp);
        kexglyph_t const *g = KexMenu_Glyph(cp);
        if (!g)
            g = KexMenu_Glyph(' ');
        if (!g || !g->w) { x += height / 3; continue; }
        int const tile = altFont ? g->alttile : g->tile;
        if (tile > 0 && tilesiz[tile].x > 0)
            rotatesprite(x << 16, y << 16, z, 0, tile, shade, pal,
                         RS_TOPLEFT | RS_AUTO, 0, 0, xdim - 1, ydim - 1);
        x += scale(g->w, height, s_fontHeight);
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
    // The Kex translations are substantially longer than the English rows
    // (notably the French checkpoint and title-screen actions).  Size each
    // one to the authored plate instead of letting a centred line spill over
    // adjacent stonework; normal labels retain the reference 12-pixel size.
    int textHeight = KEX_TEXT_H;
    int const textWidth = KEX_ROW_W - (leftAlign ? 12 : 4);
    while (textHeight > 6 && KexMenu_TextWidth(label, textHeight) > textWidth)
        --textHeight;
    int const ty = y + (KEX_ROW_H - textHeight) / 2 + 1;
    if (leftAlign)
        KexMenu_Text(x + 6, ty, label, textHeight, enabled ? 0 : 22, 0, 0, altFont);
    else
        KexMenu_Text(x + KEX_ROW_W / 2, ty, label, textHeight, enabled ? 0 : 22, 0, 1, altFont);
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
    // Keep the remaster's key-cap treatment useful on a controller as well as
    // a keyboard.  The actual actions remain the user's configured bindings.
    if (CONTROL_LastSeenInput == LastSeenInput::Joystick)
    {
        if (!Bstrcasecmp(key1, "ENTER")) key1 = "A";
        else if (!Bstrcasecmp(key1, "ESC")) key1 = "B";
        if (key2)
        {
            if (!Bstrcasecmp(key2, "ENTER")) key2 = "A";
            else if (!Bstrcasecmp(key2, "ESC")) key2 = "B";
        }
    }
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
// Input. KEX menus run outside PlayerInput, so they poll both the classic
// keyboard scancodes and the user's mapped controller actions here.
// ---------------------------------------------------------------------------
enum { kKexKeyNone = 0, kKexKeyUp, kKexKeyDown, kKexKeyLeft, kKexKeyRight, kKexKeyEnter, kKexKeyEsc };

static int KexMenu_PollKey(void)
{
    static int held = 0;
    int key = kKexKeyNone;
    ControlInfo input;
    Bmemset(&input, 0, sizeof(input));
    // The regular game loop calls this through PlayerInput.  KEX menus run
    // outside that path, so poll here to make every existing controller bind
    // work while the game is paused or at the title screen.
    CONTROL_GetInput(&input);

    if (KB_KeyDown[sc_UpArrow] || KB_KeyDown[sc_kpad_8])      key = kKexKeyUp;
    else if (KB_KeyDown[sc_DownArrow] || KB_KeyDown[sc_kpad_2]) key = kKexKeyDown;
    else if (KB_KeyDown[sc_LeftArrow] || KB_KeyDown[sc_kpad_4]) key = kKexKeyLeft;
    else if (KB_KeyDown[sc_RightArrow] || KB_KeyDown[sc_kpad_6]) key = kKexKeyRight;
    else if (KB_KeyDown[sc_Enter] || KB_KeyDown[sc_kpad_Enter] || KB_KeyDown[sc_Space]) key = kKexKeyEnter;
    else if (KB_KeyDown[sc_Escape])                           key = kKexKeyEsc;
    if (key != kKexKeyNone)
        CONTROL_LastSeenInput = LastSeenInput::Keyboard;
    else if (BUTTON(gamefunc_Move_Forward) || BUTTON(gamefunc_Look_Up))
        key = kKexKeyUp;
    else if (BUTTON(gamefunc_Move_Backward) || BUTTON(gamefunc_Look_Down))
        key = kKexKeyDown;
    else if (BUTTON(gamefunc_Strafe_Left) || BUTTON(gamefunc_Turn_Left))
        key = kKexKeyLeft;
    else if (BUTTON(gamefunc_Strafe_Right) || BUTTON(gamefunc_Turn_Right))
        key = kKexKeyRight;
    else if (BUTTON(gamefunc_Open) || BUTTON(gamefunc_Fire) || BUTTON(gamefunc_Jump))
        key = kKexKeyEnter;
    else if (BUTTON(gamefunc_Escape) || BUTTON(gamefunc_Pause))
        key = kKexKeyEsc;

    if (key == kKexKeyNone) { held = 0; return kKexKeyNone; }
    if (key == held) return kKexKeyNone;      // edge-triggered: one step per press
    held = key;
    return key;
}

static void KexMenu_Beep(void) { PlayLocalSound(StaticSound[kSoundSwitchFoot], 0); }

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
static void KexMenu_WrapConfirm(char const *text, char *first, char *second, int size)
{
    Bstrncpyz(first, text, size);
    second[0] = 0;
    if (KexMenu_TextWidth(first, 10) <= 212)
        return;

    char *lastSpace = NULL;
    for (char *p = first; *p; ++p)
    {
        if (*p != ' ')
            continue;
        *p = 0;
        if (KexMenu_TextWidth(first, 10) > 212)
        {
            *p = ' ';
            break;
        }
        *p = ' ';
        lastSpace = p;
    }
    if (lastSpace)
    {
        Bstrncpyz(second, lastSpace + 1, size);
        *lastSpace = 0;
    }
}

static int KexMenu_Confirm(const char *line1, const char *line2)
{
    char wrapped1[256], wrapped2[256];
    if (!line2 || !line2[0])
    {
        KexMenu_WrapConfirm(line1, wrapped1, wrapped2, sizeof(wrapped1));
        line1 = wrapped1;
        line2 = wrapped2;
    }
    int messageSize = 10;
    while (messageSize > 8 && (KexMenu_TextWidth(line1, messageSize) > 212
                               || (line2 && line2[0]
                                   && KexMenu_TextWidth(line2, messageSize) > 212)))
        --messageSize;

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
        KexMenu_Text((x0 + x1) / 2, top + 12, line1, messageSize, 0, 0, 1);
        if (line2 && line2[0])
            KexMenu_Text((x0 + x1) / 2, top + 25, line2, messageSize, 0, 0, 1);

        int const by = top + 52;
        int const lx = x0 + 12, rx = x1 - KEX_BTN_W - 12;
        KexDrawTile(no ? kKexTileBtn : kKexTileBtnSel, lx, by, KEX_BTN_W, KEX_BTN_H, 0, 0, 0);
        KexDrawTile(no ? kKexTileBtnSel : kKexTileBtn, rx, by, KEX_BTN_W, KEX_BTN_H, 0, 0, 0);
        KexMenu_Text(lx + KEX_BTN_W / 2, by + 2,
                     KexMenu_InvText("m_yes", "YES"), 9, 0, 0, 1);
        KexMenu_Text(rx + KEX_BTN_W / 2, by + 2,
                     KexMenu_InvText("m_no", "NO"), 9, 0, 0, 1);

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
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
static int KexMenu_SlotList(const char *title, int loading)
{
    int used[5];
    // Spanish's longest authored map name is 33 bytes in UTF-8; keep the
    // saved-game list lossless rather than inheriting the old English-only cap.
    char slotLabel[5][48];
    for (int i = 0; i < 5; i++)
    {
        used[i] = menu_SaveSlotUsed(i);
        KexMenu_SaveSlotLabel(i, slotLabel[i], sizeof(slotLabel[i]));
    }
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
            int const enabled = !loading || used[i];
            KexMenu_Row(y, slotLabel[i], sel == i, enabled, 1, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }
        KexMenu_Row(y, KexMenu_InvText("m_go_back", "GO BACK"), sel == 5, 1);
        if (sel == 5) KexMenu_Cursor(y);

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
        videoNextPage();
        Exhumed_CaptureFrame();

        switch (KexMenu_PollKey())
        {
            case kKexKeyUp:    sel = (sel + 5) % 6; KexMenu_Beep(); break;
            case kKexKeyDown:  sel = (sel + 1) % 6; KexMenu_Beep(); break;
            case kKexKeyEnter:
                KexMenu_Beep();
                if (sel == 5)
                    return -1;
                if (loading && !used[sel])
                    break;
                if (!loading && used[sel]
                    && !KexMenu_ConfirmLocalized("m_overwrite_game_confirm",
                                                  "ARE YOU SURE YOU WISH TO",
                                                  "OVERWRITE THIS SAVE GAME?"))
                    break;
                return sel;
            case kKexKeyEsc:   KexMenu_Beep(); return -1;
            default: break;
        }
        if (quitevent) return -1;
    }
}

static char const *KexMenu_InvText(char const *key, char const *fallback)
{
    char const *text = KexInv_Text(key);
    return text && text[0] ? text : fallback;
}

static int KexMenu_ConfirmLocalized(char const *key, char const *fallback1,
                                    char const *fallback2)
{
    char const *text = KexInv_Text(key);
    return KexMenu_Confirm(text && text[0] ? text : fallback1,
                           text && text[0] ? NULL : fallback2);
}

// New Game in the remaster proceeds save slot -> difficulty -> overworld.
// These are the native three settings; GO BACK returns to the slot list.
static int KexMenu_Difficulty(void)
{
    static char const * const keys[] =
        { "difficulty0", "difficulty1", "difficulty2" };
    static char const * const fallback[] =
        { "NORMAL", "HARD", "PHARAOH" };
    int sel = 0;

    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();

        int const top = KexMenu_PanelTop(4);
        KexMenu_Frame(top, 4);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("difficultytitle", "SELECT DIFFICULTY"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        for (int i = 0; i < kKexDifficultyCount; i++)
        {
            KexMenu_Row(y, KexMenu_InvText(keys[i], fallback[i]), sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }
        KexMenu_Row(y, KexMenu_InvText("m_go_back", "GO BACK"),
                    sel == kKexDifficultyCount, 1);
        if (sel == kKexDifficultyCount) KexMenu_Cursor(y);

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
        videoNextPage();
        Exhumed_CaptureFrame();

        switch (KexMenu_PollKey())
        {
            case kKexKeyUp:
                sel = (sel + kKexDifficultyCount) % (kKexDifficultyCount + 1);
                KexMenu_Beep();
                break;
            case kKexKeyDown:
                sel = (sel + 1) % (kKexDifficultyCount + 1);
                KexMenu_Beep();
                break;
            case kKexKeyEnter:
                KexMenu_Beep();
                return sel == kKexDifficultyCount ? -1 : sel;
            case kKexKeyEsc:
                KexMenu_Beep();
                return -1;
            default:
                break;
        }
        if (quitevent) return -1;
    }
}

// Kex supports four fully voiced languages.  Keeping their names ASCII here
// makes every choice readable before its own localized font table is loaded.
static void KexMenu_Language(void)
{
    static char const * const languageNames[] =
        { "ENGLISH", "FRANCAIS", "DEUTSCH", "ESPANOL", "ITALIANO" };
    enum { kKexLanguageCount = ARRAY_SIZE(languageNames), kKexLanguageBack,
           kKexLanguageItems };
    int sel = KexLanguage_Index();

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexLanguageItems);
        KexMenu_Frame(top, kKexLanguageItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_language_settings", "LANGUAGE SETTINGS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < kKexLanguageCount; i++)
        {
            KexMenu_Row(y, languageNames[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }
        KexMenu_Row(y, KexMenu_InvText("m_go_back", "GO BACK"),
                    sel == kKexLanguageBack, 1);
        if (sel == kKexLanguageBack) KexMenu_Cursor(y);

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexLanguageItems - 1 : 1))
                  % kKexLanguageItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        else if (key == kKexKeyEnter)
        {
            KexMenu_Beep();
            if (sel == kKexLanguageBack)
                return;
            // The cvar is also the movie and Ramses selector; each text cache
            // sees the new value on its next draw and reloads its own table.
            kex_language = sel;
        }
        if (quitevent) return;
    }
}

// The remaster's Display Settings panel exposes the three renderer preferences
// in its default.cfg. Build applies FOV and anisotropy immediately; its lack of
// a KEX SMAA post-process is made explicit on the retained AA preference.
static void KexMenu_Display(void)
{
    enum { kKexDisplayFov = 0, kKexDisplayAntialias, kKexDisplayAnisotropy,
           kKexDisplayBack, kKexDisplayItems };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexDisplayItems);
        KexMenu_Frame(top, kKexDisplayItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_display_settings", "DISPLAY SETTINGS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        char fov[96], antialias[96], anisotropy[96], aaValue[32];
        Bsnprintf(fov, sizeof(fov), "%s: %.0f",
                  KexMenu_InvText("m_fov", "FOV"), KexRender_Fov());
        int const aa = KexRender_Antialiasing();
        if (aa == 7)
            Bstrcpy(aaValue, "SMAA (COMPAT)");
        else if (aa == 0)
            Bstrcpy(aaValue, "OFF");
        else
            Bsnprintf(aaValue, sizeof(aaValue), "MODE %d (COMPAT)", aa);
        Bsnprintf(antialias, sizeof(antialias), "%s: %s",
                  KexMenu_InvText("m_antialiasing", "ANTIALIASING"),
                  aaValue);
        Bsnprintf(anisotropy, sizeof(anisotropy), "%s: %dx",
                  KexMenu_InvText("m_anisotrophy", "ANISOTROPY"),
                  KexRender_Anisotropy());
        char const * const labels[] =
            { fov, antialias, anisotropy, KexMenu_InvText("m_go_back", "GO BACK") };
        for (int i = 0; i < kKexDisplayItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("LEFT/RIGHT", "ADJUST", "ESC", "GO BACK");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexDisplayItems - 1 : 1))
                  % kKexDisplayItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight)
        {
            int const direction = key == kKexKeyLeft ? -1 : 1;
            if (sel == kKexDisplayFov)
                KexRender_SetFov(KexRender_Fov() + direction);
            else if (sel == kKexDisplayAntialias)
                KexRender_SetAntialiasing(KexRender_Antialiasing() + direction);
            else if (sel == kKexDisplayAnisotropy)
                KexRender_SetAnisotropy(KexRender_Anisotropy() + direction);
            else
                continue;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEnter && sel == kKexDisplayBack)
        {
            KexMenu_Beep();
            return;
        }
        if (quitevent) return;
    }
}

static void KexMenu_Game(void)
{
    enum { kKexGameCheckpoints = 0, kKexGameOverworld,
           kKexGameBobSpeed, kKexGameBobHeight, kKexGameTurnSway,
           kKexGameJumpBob, kKexGameBack, kKexGameItems };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexGameItems);
        KexMenu_Frame(top, kKexGameItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_game_settings", "GAME SETTINGS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        char checkpoints[96], overworld[96], bobSpeed[96], bobHeight[96];
        char turnSway[96], jumpBob[96];
        Bsnprintf(checkpoints, sizeof(checkpoints), "%s: %s",
                  KexMenu_InvText("m_checkpoints", "ALLOW CHECKPOINTS"),
                  KexMenu_InvText(kex_enable_checkpoints ? "m_on" : "m_off",
                                  kex_enable_checkpoints ? "ON" : "OFF"));
        Bsnprintf(overworld, sizeof(overworld), "%s: %s",
                  KexMenu_InvText("m_overworldScreen", "OVERWORLD SCREEN"),
                  KexMenu_InvText(kex_original_overworld ? "m_original" : "m_new",
                                  kex_original_overworld ? "ORIGINAL" : "NEW"));
        Bsnprintf(bobSpeed, sizeof(bobSpeed), "%s: %.1f",
                  KexMenu_InvText("m_viewBobSpeed", "VIEW BOB SPEED"),
                  clamp(kex_view_bob_speed, .5f, 10.f));
        Bsnprintf(bobHeight, sizeof(bobHeight), "%s: %.1f",
                  KexMenu_InvText("m_viewBobHeight", "VIEW BOB HEIGHT"),
                  clamp(kex_view_bob_height, .5f, 10.f));
        Bsnprintf(turnSway, sizeof(turnSway), "%s: %.1f",
                  KexMenu_InvText("m_turnSway", "TURN SWAY"),
                  clamp(kex_turn_sway, .1f, 1.f));
        Bsnprintf(jumpBob, sizeof(jumpBob), "%s: %s",
                  KexMenu_InvText("m_weaponJumpBobbing", "WEAPON JUMP BOBBING"),
                  KexMenu_InvText(kex_weapon_jump_bobbing ? "m_on" : "m_off",
                                  kex_weapon_jump_bobbing ? "ON" : "OFF"));
        char const * const labels[] =
            { checkpoints, overworld, bobSpeed, bobHeight, turnSway, jumpBob,
              KexMenu_InvText("m_go_back", "GO BACK") };
        for (int i = 0; i < kKexGameItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("LEFT/RIGHT", "ADJUST", "ENTER", "TOGGLE");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexGameItems - 1 : 1))
                  % kKexGameItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight || key == kKexKeyEnter)
        {
            int const direction = key == kKexKeyLeft ? -1 : key == kKexKeyRight;
            if (sel == kKexGameCheckpoints)
                kex_enable_checkpoints ^= 1;
            else if (sel == kKexGameOverworld)
                kex_original_overworld ^= 1;
            else if (sel == kKexGameBobSpeed && direction)
            {
                int value = (int)(clamp(kex_view_bob_speed, .5f, 10.f) * 10.f + .5f);
                kex_view_bob_speed = clamp(value + direction, 5, 100) / 10.f;
            }
            else if (sel == kKexGameBobHeight && direction)
            {
                int value = (int)(clamp(kex_view_bob_height, .5f, 10.f) * 10.f + .5f);
                kex_view_bob_height = clamp(value + direction, 5, 100) / 10.f;
            }
            else if (sel == kKexGameTurnSway && direction)
            {
                int value = (int)(clamp(kex_turn_sway, .1f, 1.f) * 10.f + .5f);
                kex_turn_sway = clamp(value + direction, 1, 10) / 10.f;
            }
            else if (sel == kKexGameJumpBob)
                kex_weapon_jump_bobbing ^= 1;
            else if (sel == kKexGameBack && key == kKexKeyEnter)
                return;
            else
                continue;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        if (quitevent) return;
    }
}

static void KexMenu_Hud(void)
{
    enum { kKexHudPlayTime = 0, kKexHudMessages, kKexHudCrosshair,
           kKexHudBack, kKexHudItems };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexHudItems);
        KexMenu_Frame(top, kKexHudItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_hudSettings", "HUD SETTINGS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        char playTime[96], messages[96], crosshair[96];
        Bsnprintf(playTime, sizeof(playTime), "%s: %s",
                  KexMenu_InvText("m_showPlayTime", "PLAY TIME"),
                  KexMenu_InvText(kex_show_play_time ? "m_on" : "m_off",
                                  kex_show_play_time ? "ON" : "OFF"));
        Bsnprintf(messages, sizeof(messages), "%s: %s",
                  KexMenu_InvText("m_hudtext", "HUD TEXT"),
                  KexMenu_InvText(kex_show_hud_messages ? "m_on" : "m_off",
                                  kex_show_hud_messages ? "ON" : "OFF"));
        Bsnprintf(crosshair, sizeof(crosshair), "%s: %.2f",
                  KexMenu_InvText("m_crosshairsize", "CROSSHAIR SIZE"),
                  clamp(kex_crosshair_size, .05f, 3.f));
        char const * const labels[] =
            { playTime, messages, crosshair,
              KexMenu_InvText("m_go_back", "GO BACK") };
        for (int i = 0; i < kKexHudItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("LEFT/RIGHT", "ADJUST", "ENTER", "TOGGLE");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexHudItems - 1 : 1))
                  % kKexHudItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight || key == kKexKeyEnter)
        {
            int const direction = key == kKexKeyLeft ? -1 : key == kKexKeyRight;
            if (sel == kKexHudPlayTime)
                kex_show_play_time ^= 1;
            else if (sel == kKexHudMessages)
                kex_show_hud_messages ^= 1;
            else if (sel == kKexHudCrosshair && direction)
            {
                int value = (int)(clamp(kex_crosshair_size, .05f, 3.f) * 100.f + .5f);
                kex_crosshair_size = clamp(value + direction * 5, 5, 300) / 100.f;
            }
            else if (sel == kKexHudBack && key == kKexKeyEnter)
                return;
            else
                continue;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        if (quitevent) return;
    }
}

static float KexMenu_MouseSensitivity(int axis)
{
    return clamp(CONTROL_MouseAxesSensitivity[axis], .1f, 100.f);
}

static void KexMenu_SetMouseSensitivity(int axis, float value)
{
    CONTROL_SetAnalogAxisSensitivity(axis, clamp(value, .1f, 100.f),
                                     controldevice_mouse);
}

static void KexMenu_Mouse(void)
{
    enum { kKexMouseSensitivityX = 0, kKexMouseSensitivityY,
           kKexMouseBack, kKexMouseItems };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexMouseItems);
        KexMenu_Frame(top, kKexMouseItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_mouse_settings", "MOUSE SETTINGS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        char sensitivityX[96], sensitivityY[96];
        Bsnprintf(sensitivityX, sizeof(sensitivityX), "%s: %.1f",
                  KexMenu_InvText("m_msens_x", "MOUSE SENSITIVITY X"),
                  KexMenu_MouseSensitivity(0));
        Bsnprintf(sensitivityY, sizeof(sensitivityY), "%s: %.1f",
                  KexMenu_InvText("m_msens_y", "MOUSE SENSITIVITY Y"),
                  KexMenu_MouseSensitivity(1));
        char const * const labels[] =
            { sensitivityX, sensitivityY, KexMenu_InvText("m_go_back", "GO BACK") };
        for (int i = 0; i < kKexMouseItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("LEFT/RIGHT", "ADJUST", "ENTER", "SELECT");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexMouseItems - 1 : 1)) % kKexMouseItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight)
        {
            int const direction = key == kKexKeyLeft ? -1 : 1;
            if (sel == kKexMouseSensitivityX)
                KexMenu_SetMouseSensitivity(0, KexMenu_MouseSensitivity(0) + direction * .5f);
            else if (sel == kKexMouseSensitivityY)
                KexMenu_SetMouseSensitivity(1, KexMenu_MouseSensitivity(1) + direction * .5f);
            else
                continue;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEnter && sel == kKexMouseBack)
        {
            KexMenu_Beep();
            return;
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        if (quitevent) return;
    }
}

static float KexMenu_ControllerScale(int axis)
{
    return clamp(JoystickAnalogueScale[axis] / 65536.f, .1f, 3.f);
}

static void KexMenu_SetControllerScale(int axis, float value)
{
    int const scale = (int)(clamp(value, .1f, 3.f) * 65536.f + .5f);
    JoystickAnalogueScale[axis] = scale;
    CONTROL_SetAnalogAxisScale(axis, scale, controldevice_joystick);
}

static int KexMenu_ControllerDeadzone(int axis)
{
    return clamp(JoystickAnalogueDead[axis], 0, 9500);
}

static void KexMenu_SetControllerDeadzone(int axis, int value)
{
    int const deadzone = clamp(value, 0, 9500);
    JoystickAnalogueDead[axis] = deadzone;
    JoystickAnalogueSaturate[axis] = max(JoystickAnalogueSaturate[axis], deadzone);
    JOYSTICK_SetDeadZone(axis, deadzone, JoystickAnalogueSaturate[axis]);
}

static void KexMenu_GamepadDeadzones(void)
{
    enum { kKexGamepadLeftDeadzone = 0, kKexGamepadRightDeadzone,
           kKexGamepadDeadzoneBack, kKexGamepadDeadzoneItems };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexGamepadDeadzoneItems);
        KexMenu_Frame(top, kKexGamepadDeadzoneItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_deadzone", "DEADZONE"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        char left[96], right[96];
        Bsnprintf(left, sizeof(left), "%s: %d%%",
                  KexMenu_InvText("m_lstick", "LEFT STICK"),
                  KexMenu_ControllerDeadzone(CONTROLLER_AXIS_LEFTX) / 100);
        Bsnprintf(right, sizeof(right), "%s: %d%%",
                  KexMenu_InvText("m_rstick", "RIGHT STICK"),
                  KexMenu_ControllerDeadzone(CONTROLLER_AXIS_RIGHTX) / 100);
        char const * const labels[] =
            { left, right, KexMenu_InvText("m_go_back", "GO BACK") };
        for (int i = 0; i < kKexGamepadDeadzoneItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("LEFT/RIGHT", "ADJUST", "ENTER", "SELECT");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexGamepadDeadzoneItems - 1 : 1))
                  % kKexGamepadDeadzoneItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight)
        {
            int const direction = key == kKexKeyLeft ? -1 : 1;
            if (sel == kKexGamepadLeftDeadzone)
            {
                int const value = KexMenu_ControllerDeadzone(CONTROLLER_AXIS_LEFTX)
                                  + direction * 500;
                KexMenu_SetControllerDeadzone(CONTROLLER_AXIS_LEFTX, value);
                KexMenu_SetControllerDeadzone(CONTROLLER_AXIS_LEFTY, value);
            }
            else if (sel == kKexGamepadRightDeadzone)
            {
                int const value = KexMenu_ControllerDeadzone(CONTROLLER_AXIS_RIGHTX)
                                  + direction * 500;
                KexMenu_SetControllerDeadzone(CONTROLLER_AXIS_RIGHTX, value);
                KexMenu_SetControllerDeadzone(CONTROLLER_AXIS_RIGHTY, value);
            }
            else
                continue;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEnter && sel == kKexGamepadDeadzoneBack)
        {
            KexMenu_Beep();
            return;
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        if (quitevent) return;
    }
}

static void KexMenu_Gamepad(void)
{
    enum { kKexGamepadVibration = 0, kKexGamepadVolume, kKexGamepadLookX,
           kKexGamepadLookY, kKexGamepadMove, kKexGamepadInvertLook,
           kKexGamepadDeadzone, kKexGamepadItems };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexGamepadItems);
        KexMenu_Frame(top, kKexGamepadItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_gamepad_settings", "GAMEPAD SETTINGS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;

        char vibration[96], volume[96], lookX[96], lookY[96], move[96], invert[96];
        Bsnprintf(vibration, sizeof(vibration), "%s: %s",
                  KexMenu_InvText("m_vibration", "VIBRATION"),
                  KexMenu_InvText(kex_vibration ? "m_on" : "m_off",
                                  kex_vibration ? "ON" : "OFF"));
        Bsnprintf(volume, sizeof(volume), "%s: %d%%",
                  KexMenu_InvText("m_vibration_volume", "VIBRATION INTENSITY"),
                  clamp(kex_vibration_volume, 0, 100));
        Bsnprintf(lookX, sizeof(lookX), "%s: %.2f",
                  KexMenu_InvText("m_joylookspeed_x", "LOOK SPEED X"),
                  KexMenu_ControllerScale(CONTROLLER_AXIS_RIGHTX));
        Bsnprintf(lookY, sizeof(lookY), "%s: %.2f",
                  KexMenu_InvText("m_joylookspeed_y", "LOOK SPEED Y"),
                  KexMenu_ControllerScale(CONTROLLER_AXIS_RIGHTY));
        Bsnprintf(move, sizeof(move), "%s: %.2f",
                  KexMenu_InvText("m_joymovesens", "MOVE SENSITIVITY"),
                  KexMenu_ControllerScale(CONTROLLER_AXIS_LEFTX));
        Bsnprintf(invert, sizeof(invert), "%s: %s",
                  KexMenu_InvText("m_joyinvertlook", "INVERT LOOK"),
                  KexMenu_InvText(JoystickAnalogueInvert[CONTROLLER_AXIS_RIGHTY]
                                      ? "m_on" : "m_off",
                                  JoystickAnalogueInvert[CONTROLLER_AXIS_RIGHTY]
                                      ? "ON" : "OFF"));
        char const * const labels[] =
            { vibration, volume, lookX, lookY, move, invert,
              KexMenu_InvText("m_deadzone", "DEADZONE") };
        for (int i = 0; i < kKexGamepadItems; i++)
        {
            KexMenu_Row(y, labels[i], sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("LEFT/RIGHT", "ADJUST", "ESC", "GO BACK");
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexGamepadItems - 1 : 1))
                  % kKexGamepadItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight)
        {
            int const direction = key == kKexKeyLeft ? -1 : 1;
            if (sel == kKexGamepadVibration)
                kex_vibration ^= 1;
            else if (sel == kKexGamepadVolume)
                kex_vibration_volume = clamp(kex_vibration_volume
                                             + direction * 10, 0, 100);
            else if (sel == kKexGamepadLookX)
                KexMenu_SetControllerScale(CONTROLLER_AXIS_RIGHTX,
                    KexMenu_ControllerScale(CONTROLLER_AXIS_RIGHTX) + direction * .1f);
            else if (sel == kKexGamepadLookY)
                KexMenu_SetControllerScale(CONTROLLER_AXIS_RIGHTY,
                    KexMenu_ControllerScale(CONTROLLER_AXIS_RIGHTY) + direction * .1f);
            else if (sel == kKexGamepadMove)
            {
                float const value = KexMenu_ControllerScale(CONTROLLER_AXIS_LEFTX)
                                    + direction * .1f;
                KexMenu_SetControllerScale(CONTROLLER_AXIS_LEFTX, value);
                KexMenu_SetControllerScale(CONTROLLER_AXIS_LEFTY, value);
            }
            else if (sel == kKexGamepadInvertLook)
            {
                JoystickAnalogueInvert[CONTROLLER_AXIS_RIGHTY] ^= 1;
                CONTROL_SetAnalogAxisInvert(CONTROLLER_AXIS_RIGHTY,
                    JoystickAnalogueInvert[CONTROLLER_AXIS_RIGHTY]);
            }
            else
                continue;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        else if (key == kKexKeyEnter)
        {
            KexMenu_Beep();
            if (sel == kKexGamepadVibration)
                kex_vibration ^= 1;
            else if (sel == kKexGamepadInvertLook)
            {
                JoystickAnalogueInvert[CONTROLLER_AXIS_RIGHTY] ^= 1;
                CONTROL_SetAnalogAxisInvert(CONTROLLER_AXIS_RIGHTY,
                    JoystickAnalogueInvert[CONTROLLER_AXIS_RIGHTY]);
            }
            else if (sel == kKexGamepadDeadzone)
            {
                KexMenu_GamepadDeadzones();
                KB_FlushKeyboardQueue();
                KB_ClearKeysDown();
            }
        }
        if (quitevent) return;
    }
}

struct KexBinding
{
    char const *key;
    char const *fallback;
    int gamefunc;
    char const *command;
};

enum { kKexBindingCommand = -1 };

static KexBinding const s_kexBindings[] =
{
    { "m_bind_forward",      "MOVE FORWARD",    gamefunc_Move_Forward, NULL },
    { "m_bind_backward",     "MOVE BACKWARD",   gamefunc_Move_Backward, NULL },
    { "m_bind_strafe_left",  "STRAFE LEFT",     gamefunc_Strafe_Left, NULL },
    { "m_bind_strafe_right", "STRAFE RIGHT",    gamefunc_Strafe_Right, NULL },
    { "m_bind_jump",         "JUMP",            gamefunc_Jump, NULL },
    { "m_bind_use",          "INTERACT",        gamefunc_Open, NULL },
    { "m_bind_attack",       "ATTACK",          gamefunc_Fire, NULL },
    { "m_bind_automap",      "AUTOMAP",         gamefunc_Map, NULL },
    { "m_bind_weaponwheel",  "WEAPON WHEEL",    gamefunc_Weapon_Wheel, NULL },
    { "m_bind_inventory",    "INVENTORY",       gamefunc_Inventory, NULL },
    { "m_bind_mapzoomin",    "ZOOM MAP IN",     gamefunc_Zoom_In, NULL },
    { "m_bind_mapzoomout",   "ZOOM MAP OUT",    gamefunc_Zoom_Out, NULL },
    { "m_bind_screenshot",   "TAKE SCREENSHOT", kKexBindingCommand, "screenshot" },
    { "m_bind_weap_next",    "NEXT WEAPON",     gamefunc_Next_Weapon, NULL },
    { "m_bind_weap_prev",    "PREVIOUS WEAPON", gamefunc_Previous_Weapon, NULL },
    { "m_bind_turn_left",    "TURN LEFT",       gamefunc_Turn_Left, NULL },
    { "m_bind_turn_right",   "TURN RIGHT",      gamefunc_Turn_Right, NULL },
    { "m_bind_weap_0",       "MACHETE",         gamefunc_Weapon_1, NULL },
    { "m_bind_weap_1",       "PISTOL",          gamefunc_Weapon_2, NULL },
    { "m_bind_weap_2",       "M-60",            gamefunc_Weapon_3, NULL },
    { "m_bind_weap_3",       "AMUN BOMB",       gamefunc_Weapon_4, NULL },
    { "m_bind_weap_4",       "FLAME-THROWER",   gamefunc_Weapon_5, NULL },
    { "m_bind_weap_5",       "COBRA STAFF",     gamefunc_Weapon_6, NULL },
    { "m_bind_weap_6",       "RING OF RA",      gamefunc_Weapon_7, NULL },
    { "m_bind_weap_7",       "MANACLE",         gamefunc_Weapon_8, NULL },
};

static bool KexMenu_CommandMatches(char const *entry, int length, char const *command)
{
    int const commandLength = Bstrlen(command);
    while (length > 0 && *entry == ' ')
    {
        ++entry;
        --length;
    }
    while (length > 0 && entry[length - 1] == ' ')
        --length;
    return length == commandLength && Bstrncasecmp(entry, command, length) == 0;
}

static bool KexMenu_BindingHasCommand(char const *commands, char const *command)
{
    if (commands == NULL)
        return false;
    for (char const *entry = commands; *entry; )
    {
        char const * const end = Bstrchr(entry, ';');
        int const length = end ? (int)(end - entry) : Bstrlen(entry);
        if (KexMenu_CommandMatches(entry, length, command))
            return true;
        if (end == NULL)
            break;
        entry = end + 1;
    }
    return false;
}

static void KexMenu_CopyCommandsExcept(char *output, int size, char const *commands,
                                       char const *omit)
{
    output[0] = '\0';
    if (commands == NULL)
        return;
    for (char const *entry = commands; *entry; )
    {
        char const * const end = Bstrchr(entry, ';');
        int const length = end ? (int)(end - entry) : Bstrlen(entry);
        if (length > 0 && !KexMenu_CommandMatches(entry, length, omit))
        {
            int const used = Bstrlen(output);
            Bsnprintf(output + used, size - used, "%s%.*s", used ? "; " : "", length, entry);
        }
        if (end == NULL)
            break;
        entry = end + 1;
    }
}

static kb_scancode KexMenu_CommandBindingScan(char const *command)
{
    for (int scan = 0; scan < MAXBOUNDKEYS; ++scan)
        if (KexMenu_BindingHasCommand(CONTROL_KeyBinds[scan].cmdstr, command))
            return (kb_scancode)scan;
    return sc_Bad;
}

static char const *KexMenu_BindingKeyName(KexBinding const &binding)
{
    kb_scancode const scan = binding.command ? KexMenu_CommandBindingScan(binding.command)
                                             : KeyboardKeys[binding.gamefunc][0];
    char const * const name = scan == sc_Bad ? NULL : KB_ScanCodeToString(scan);
    return name && name[0] ? name : "UNBOUND";
}

static void KexMenu_BindingRow(int y, KexBinding const &binding, int selected)
{
    int const x = KEX_PANEL_X0 + KEX_PAD;
    char const * const label = KexMenu_InvText(binding.key, binding.fallback);
    char const * const key = KexMenu_BindingKeyName(binding);
    int labelHeight = 9, keyHeight = 9;
    while (labelHeight > 6 && KexMenu_TextWidth(label, labelHeight) > 80)
        --labelHeight;
    while (keyHeight > 6 && KexMenu_TextWidth(key, keyHeight) > 38)
        --keyHeight;

    KexMenu_Row(y, "", selected, 1, 1);
    KexMenu_Text(x + 6, y + (KEX_ROW_H - labelHeight) / 2 + 1,
                 label, labelHeight, 0, 0, 0, 1);
    KexMenu_Text(x + KEX_ROW_W - 6, y + (KEX_ROW_H - keyHeight) / 2 + 1,
                 key, keyHeight, 0, 0, 2, 1);
}

static kb_scancode KexMenu_CaptureBinding(KexBinding const &binding)
{
    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    KB_ClearLastScanCode();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(1);
        KexMenu_Frame(top, 1);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_bind_prompt", "BIND ACTION"), 0, 1, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        KexMenu_BindingRow(y, binding, 1);
        KexMenu_Hint("ESC", KexMenu_InvText("m_prompt_cancel", "CANCEL"));
        videoNextPage();
        Exhumed_CaptureFrame();

        kb_scancode const scan = KB_GetLastScanCode();
        if (scan != sc_None)
        {
            KB_ClearLastScanCode();
            if (scan == sc_Escape)
                return sc_Bad;
            return scan;
        }
        if (quitevent)
            return sc_Bad;
    }
}

static void KexMenu_SetBinding(KexBinding const &binding, kb_scancode scan)
{
    if (binding.command)
    {
        if (scan == sc_Bad)
            return;
        for (int oldScan = 0; oldScan < MAXBOUNDKEYS; ++oldScan)
        {
            if (oldScan == scan || !KexMenu_BindingHasCommand(CONTROL_KeyBinds[oldScan].cmdstr,
                                                               binding.command))
                continue;
            char commands[256];
            KexMenu_CopyCommandsExcept(commands, sizeof(commands), CONTROL_KeyBinds[oldScan].cmdstr,
                                       binding.command);
            if (commands[0])
                CONTROL_BindKey((kb_scancode)oldScan, commands,
                                CONTROL_KeyBinds[oldScan].repeat,
                                KB_ScanCodeToString((kb_scancode)oldScan));
            else
                CONTROL_FreeKeyBind((kb_scancode)oldScan);
        }

        char commands[256];
        KexMenu_CopyCommandsExcept(commands, sizeof(commands), CONTROL_KeyBinds[scan].cmdstr,
                                   binding.command);
        int const used = Bstrlen(commands);
        Bsnprintf(commands + used, sizeof(commands) - used, "%s%s", used ? "; " : "",
                  binding.command);
        CONTROL_BindKey(scan, commands, 1, KB_ScanCodeToString(scan));
        return;
    }

    kb_scancode const oldPrimary = KeyboardKeys[binding.gamefunc][0];
    kb_scancode const oldSecondary = KeyboardKeys[binding.gamefunc][1];
    if (scan == sc_Bad || scan == oldPrimary || scan == oldSecondary)
        return;
    KeyboardKeys[binding.gamefunc][0] = scan;
    CONFIG_MapKey(binding.gamefunc, scan, oldPrimary, oldSecondary, oldSecondary);
}

// VB_KEX_BINDINGS_TEST=1 moves the screenshot command between two deliberately
// shared bindings, confirms the unrelated commands remain intact, then restores
// every touched command string. It runs only after the real config is loaded.
static void KexMenu_RunBindingSelftest(void)
{
    static int done = 0;
    if (done || !Bgetenv("VB_KEX_BINDINGS_TEST"))
        return;
    done = 1;

    KexBinding const *screenshot = NULL;
    for (auto const &binding : s_kexBindings)
        if (binding.command && !Bstrcasecmp(binding.command, "screenshot"))
            screenshot = &binding;
    if (screenshot == NULL)
    {
        OSD_Printf("Kex Bindings test: FAIL (screenshot action missing)\n");
        return;
    }

    struct BindingBackup
    {
        kb_scancode scan;
        int bound, repeat;
        char commands[256];
    } backups[MAXBOUNDKEYS];
    int backupCount = 0;
    for (int scan = 0; scan < MAXBOUNDKEYS; ++scan)
    {
        if (scan != sc_F5 && scan != sc_F6
            && !KexMenu_BindingHasCommand(CONTROL_KeyBinds[scan].cmdstr, "screenshot"))
            continue;
        if (CONTROL_KeyIsBound(scan)
            && Bstrlen(CONTROL_KeyBinds[scan].cmdstr) >= (int)sizeof(backups[0].commands))
        {
            OSD_Printf("Kex Bindings test: SKIP (binding command too long)\n");
            return;
        }
        BindingBackup &backup = backups[backupCount++];
        backup.scan = (kb_scancode)scan;
        backup.bound = CONTROL_KeyIsBound(scan);
        backup.repeat = backup.bound ? CONTROL_KeyBinds[scan].repeat : 1;
        Bstrncpyz(backup.commands, backup.bound ? CONTROL_KeyBinds[scan].cmdstr : "",
                  sizeof(backup.commands));
    }

    CONTROL_BindKey(sc_F5, "gamefunc_Open; screenshot", 1, KB_ScanCodeToString(sc_F5));
    CONTROL_BindKey(sc_F6, "gamefunc_Fire; custom_binding_probe", 1, KB_ScanCodeToString(sc_F6));
    KexMenu_SetBinding(*screenshot, sc_F6);

    bool const pass = KexMenu_CommandBindingScan("screenshot") == sc_F6
        && KexMenu_BindingHasCommand(CONTROL_KeyBinds[sc_F5].cmdstr, "gamefunc_Open")
        && !KexMenu_BindingHasCommand(CONTROL_KeyBinds[sc_F5].cmdstr, "screenshot")
        && KexMenu_BindingHasCommand(CONTROL_KeyBinds[sc_F6].cmdstr, "gamefunc_Fire")
        && KexMenu_BindingHasCommand(CONTROL_KeyBinds[sc_F6].cmdstr, "screenshot")
        && Bstrstr(CONTROL_KeyBinds[sc_F6].cmdstr, "custom_binding_probe") != NULL;

    for (int i = 0; i < backupCount; ++i)
    {
        BindingBackup const &backup = backups[i];
        if (backup.bound)
            CONTROL_BindKey(backup.scan, backup.commands, backup.repeat,
                            KB_ScanCodeToString(backup.scan));
        else
            CONTROL_FreeKeyBind(backup.scan);
    }
    OSD_Printf("Kex Bindings test: %s (shared screenshot command)\n", pass ? "PASS" : "FAIL");
}

static void KexMenu_Bindings(void)
{
    constexpr int kRowsPerPage = 5;
    int const pageCount = (int)ARRAY_SIZE(s_kexBindings) / kRowsPerPage;
    int page = 0, sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kRowsPerPage);
        KexMenu_Frame(top, kRowsPerPage);
        int y = top + KEX_PAD;
        char title[96];
        Bsnprintf(title, sizeof(title), "%s %d/%d",
                  KexMenu_InvText("m_bindings", "BINDINGS"), page + 1, pageCount);
        KexMenu_Row(y, title, 0, 1, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < kRowsPerPage; i++)
        {
            KexMenu_BindingRow(y, s_kexBindings[page * kRowsPerPage + i], sel == i);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }
        KexMenu_Hint("ENTER", KexMenu_InvText("m_bind_prompt", "BIND ACTION"),
                     "LEFT/RIGHT", KexMenu_InvText("m_next_page", "NEXT PAGE"));
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kRowsPerPage - 1 : 1)) % kRowsPerPage;
            KexMenu_Beep();
        }
        else if (key == kKexKeyLeft || key == kKexKeyRight)
        {
            page = (page + (key == kKexKeyLeft ? pageCount - 1 : 1)) % pageCount;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEnter)
        {
            KexBinding const &binding = s_kexBindings[page * kRowsPerPage + sel];
            kb_scancode const scan = KexMenu_CaptureBinding(binding);
            if (scan != sc_Bad)
            {
                KexMenu_SetBinding(binding, scan);
                KexMenu_Beep();
            }
            KB_FlushKeyboardQueue();
            KB_ClearKeysDown();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        if (quitevent) return;
    }
}

static void KexMenu_Options(void)
{
    enum { kKexAudio = 0, kKexDisplay, kKexHud, kKexGame, kKexMouse, kKexGamepad,
           kKexBindings, kKexLanguage, kKexOptionsItems };
    static char const * const keys[] =
        { "m_audio_settings", "m_display_settings", "m_hudSettings", "m_game_settings",
          "m_mouse_settings", "m_gamepad_settings", "m_bindings", "m_language_settings" };
    static char const * const fallback[] =
        { "AUDIO SETTINGS", "DISPLAY SETTINGS", "HUD SETTINGS", "GAME SETTINGS",
          "MOUSE SETTINGS", "CONTROLLER SETTINGS", "BINDINGS", "LANGUAGE SETTINGS" };
    int sel = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        int const top = KexMenu_PanelTop(kKexOptionsItems);
        KexMenu_Frame(top, kKexOptionsItems);
        int y = top + KEX_PAD;
        KexMenu_Row(y, KexMenu_InvText("m_options", "OPTIONS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < kKexOptionsItems; i++)
        {
            KexMenu_Row(y, KexMenu_InvText(keys[i], fallback[i]), sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (key == kKexKeyUp || key == kKexKeyDown)
        {
            sel = (sel + (key == kKexKeyUp ? kKexOptionsItems - 1 : 1))
                  % kKexOptionsItems;
            KexMenu_Beep();
        }
        else if (key == kKexKeyEsc)
        {
            KexMenu_Beep();
            return;
        }
        else if (key == kKexKeyEnter)
        {
            KexMenu_Beep();
            if (sel == kKexAudio)
                menu_AdjustVolume();
            else if (sel == kKexDisplay)
                KexMenu_Display();
            else if (sel == kKexHud)
                KexMenu_Hud();
            else if (sel == kKexGame)
                KexMenu_Game();
            else if (sel == kKexMouse)
                KexMenu_Mouse();
            else if (sel == kKexGamepad)
                KexMenu_Gamepad();
            else if (sel == kKexBindings)
                KexMenu_Bindings();
            else if (sel == kKexLanguage)
                KexMenu_Language();
            KB_FlushKeyboardQueue();
            KB_ClearKeysDown();
        }
        if (quitevent) return;
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
        KexMenu_Row(y, KexMenu_InvText("m_about", "CREDITS"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < rows; i++)
        {
            if (lines[i][0]) KexMenu_Text(160, y + 2, lines[i], 10, 0, 0, 1);
            y += KEX_ROW_H;
        }
        KexMenu_Hint("ESC", KexMenu_InvText("m_prompt_back", "GO BACK"));
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
    static char const * const keys[kKexItems] =
        { "m_new_game", "m_load_game", "m_options", "m_about", "m_quit" };
    static char const * const fallback[kKexItems] =
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
            KexMenu_Row(y, KexMenu_InvText(keys[i], fallback[i]), sel == i, enabled[i]);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
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
                    int difficulty = -1;
                    do
                    {
                        SavePosition = (short)KexMenu_SlotList(
                            KexMenu_InvText("m_new_game", "NEW GAME"), 0);
                        if (SavePosition == -1) break;
                        difficulty = KexMenu_Difficulty();
                    }
                    while (difficulty < 0);
                    if (SavePosition == -1) break;
                    KexDifficulty_Set(difficulty);
                    KexCampaign_NewGame();
                    s_chosenMap[0] = 0;
                    // The remaster picks the level off its overworld map, not
                    // straight into level 1.
                    if (KexMenu_LevelCount() > 0)
                    {
                        playMusicFile("music/overworld.ogg", true);
                        int const pick = KexMenu_Overworld(0);
                        if (pick < 0) { playMusicFile("music/title.ogg", true); break; }
                        KexCampaign_UnlockMap(pick);
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
                    SavePosition = (short)KexMenu_SlotList(
                        KexMenu_InvText("m_load_game", "LOAD GAME"), 1);
                    if (SavePosition == -1) break;
                    StopAllSounds();
                    MySetView(nViewLeft, nViewTop, nViewRight, nViewBottom);
                    return 2;
                }
                case kKexOptions:  KexMenu_Options(); break;
                case kKexCredits:
                    playMusicFile("music/credu1.ogg", true);
                    KexMenu_Credits();
                    playMusicFile("music/title.ogg", true);
                    break;
                case kKexQuit:
                    if (KexMenu_ConfirmLocalized("m_quit_game_confirm",
                                                 "ARE YOU SURE YOU WISH TO", "QUIT THE GAME?"))
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
//   -1 resume, 0 quit, 4 exit to title, 5 reload checkpoint, 6 restart level
// ---------------------------------------------------------------------------
enum { kKexPauseResume = 0, kKexPauseOptions, kKexPauseReload,
       kKexPauseRestart, kKexPauseExit, kKexPauseQuit, kKexPauseItems };

static int KexMenu_Pause(void)
{
    static char const * const keys[kKexPauseItems] =
        { "m_resume", "m_options", "m_reload_last_checkpoint", "m_restart_level",
          "m_exit_title_screen", "m_quit" };
    static char const * const fallback[kKexPauseItems] =
        { "RESUME", "OPTIONS", "RELOAD LAST CHECKPOINT", "RESTART LEVEL",
          "EXIT TO TITLE SCREEN", "QUIT" };

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
        KexMenu_Row(y, KexMenu_InvText("m_game_paused", "GAME PAUSED"), 0, 1);
        y += KEX_ROW_H + KEX_HEADGAP;
        for (int i = 0; i < kKexPauseItems; i++)
        {
            KexMenu_Row(y, KexMenu_InvText(keys[i], fallback[i]), sel == i, 1);
            if (sel == i) KexMenu_Cursor(y);
            y += KEX_ROW_H;
        }

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"));
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
                case kKexPauseOptions: KexMenu_Options(); break;
                case kKexPauseReload:
                    if (KexCheckpoint_HasCheckpoint()
                        && KexMenu_ConfirmLocalized("m_reload_checkpoint_confirm",
                                                    "RELOAD LAST CHECKPOINT?",
                                                    "UNSAVED PROGRESS WILL BE LOST."))
                        return 5;
                    break;
                case kKexPauseRestart:
                    if (KexMenu_ConfirmLocalized("m_reload_level_confirm",
                                                 "RESTART THE LEVEL?",
                                                 "UNSAVED PROGRESS WILL BE LOST."))
                        return 6;
                    break;
                case kKexPauseExit:
                    if (KexMenu_ConfirmLocalized("m_exit_game_confirm",
                                                 "ARE YOU SURE YOU WISH TO",
                                                 "EXIT TO THE TITLE SCREEN?"))
                        return 4;
                    break;
                case kKexPauseQuit:
                    if (KexMenu_ConfirmLocalized("m_quit_game_confirm",
                                                 "ARE YOU SURE YOU WISH TO", "QUIT THE GAME?"))
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
    // The title track carries the menu behind this screen too; starting a
    // level replaces it, playMusicFile stopping the old track first.
    playMusicFile("music/title.ogg", true);
    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    GrabPalette();
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    int const start = (int)totalclock;
    while (1)
    {
        HandleAsync();
        KexMenu_Backdrop();
        // The remaster holds on the console game's title card -- the logo, the
        // doorway, Lobotomy's 1996 copyright line -- with the prompt over it.
        // The backdrop under it fills the widescreen margins.
        int const card = tilesiz[kKexTileTitle].x > 0;
        if (card)
            KexDrawTile(kKexTileTitle, 0, 0, 320, 200, 0, 0, 0);

        // A slow pulse, so the prompt reads as an invitation rather than a
        // static label. Sin() is the engine's own fixed-point table.
        int const shade = (Sin(((int)totalclock - start) << 5) >> 11) - 4;
        KexMenu_Text(160, card ? 148 : 150,
                     KexMenu_InvText("m_press_start", "PRESS START"),
                     16, (int8_t)shade, 0, 1);

        if (!card)
        {
            KexMenu_Text(160, 182, "POWERSLAVE AND POWERSLAVE EXHUMED ARE TRADEMARKS", 5, 0, 0, 1);
            KexMenu_Text(160, 189, "OF THROWBACK ENTERTAINMENT INC", 5, 0, 0, 1);
        }

        videoNextPage();
        Exhumed_CaptureFrame();

        int const key = KexMenu_PollKey();
        if (KB_KeyWaiting() || key == kKexKeyEnter || key == kKexKeyEsc)
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
enum
{
    kKexTileOverworld = 7008,
    kKexTileMarker = 7009,
    kKexTileArrow = 7010,
    kKexTileOverworldOriginal = 7017,
    kKexTileEyeOriginalBase = 7550,
    kKexTileArrowOriginalUp = 7581,
    kKexTileArrowOriginalRight = 7582,
    kKexTileArrowOriginalDown = 7583,
    kKexTileArrowOriginalLeft = 7584,
};

struct kexlevel_t
{
    char map[32];
    char title[48];
    int16_t x, y;
    int16_t originalX, originalY;
    int16_t link[4];      // north, east, south, west
};

static kexlevel_t s_level[KEX_CAMPAIGN_MAX_MAPS];
static int s_levelCount = -1;
static int s_worldLanguage = -1;
static int s_eyeFrames = 0;
static int s_originalEyeFrames = 0;
static int s_worldSelftestDone;

static bool KexMenu_ParseLevel(char *line, int nRec)
{
    kexlevel_t &L = s_level[nRec];
    int x, y, originalX, originalY, n, e, so, w, used = 0;
    int fields = Bsscanf(line, "%31s %d %d %d %d %d %d %d %d %n",
                         L.map, &x, &y, &originalX, &originalY,
                         &n, &e, &so, &w, &used);
    if (fields < 9)
    {
        // Generated output before the original-overworld option had only the
        // repainted coordinates. Preserve its normal map behaviour and derive
        // a safe original fallback for a user who adds the new art later.
        used = 0;
        fields = Bsscanf(line, "%31s %d %d %d %d %d %d %n",
                         L.map, &x, &y, &n, &e, &so, &w, &used);
        if (fields < 7)
            return false;
        originalX = clamp(Blrintf((((float)x / 1200.f) - .180f) / .645f * 640.f),
                          0, 640);
        originalY = clamp(Blrintf((((float)y / 614.f) + .005f) / 1.010f * 512.f),
                          0, 512);
    }

    L.x = (int16_t)x; L.y = (int16_t)y;
    L.originalX = (int16_t)originalX; L.originalY = (int16_t)originalY;
    L.link[0] = (int16_t)n; L.link[1] = (int16_t)e;
    L.link[2] = (int16_t)so; L.link[3] = (int16_t)w;
    Bstrncpyz(L.title, used > 0 ? line + used : "", sizeof(L.title));
    return true;
}

static void KexMenu_UnlockCompletedLevel(int nLevel)
{
    if (nLevel < 0 || nLevel >= s_levelCount)
        return;

    KexCampaign_UnlockMap(nLevel);
    for (int d = 0; d < 4; d++)
        KexCampaign_UnlockMap(s_level[nLevel].link[d]);
}

static void KexMenu_RunWorldSelftest(void)
{
    if (s_worldSelftestDone || !Bgetenv("VB_KEXOVERWORLD_TEST"))
        return;
    s_worldSelftestDone = 1;

    KexCampaign_NewGame();
    int freshLocked = 1;
    for (int i = 0; i < s_levelCount; i++)
        freshLocked &= !KexCampaign_MapUnlocked(i);

    int start = 0;
    KexMenu_UnlockCompletedLevel(start);
    int adjacencyExact = s_levelCount > 0;
    for (int i = 0; i < s_levelCount; i++)
    {
        int expected = i == start;
        for (int d = 0; d < 4; d++)
            expected |= s_level[start].link[d] == i;
        adjacencyExact &= KexCampaign_MapUnlocked(i) == !!expected;
    }

    OSD_Printf("Kex overworld unlock test: %s (%d maps, fresh %s, start links %d/%d/%d/%d)\n",
               freshLocked && adjacencyExact ? "PASS" : "FAIL", s_levelCount,
               freshLocked ? "locked" : "FAIL", s_levelCount > 0 ? s_level[0].link[0] : -1,
               s_levelCount > 0 ? s_level[0].link[1] : -1,
               s_levelCount > 0 ? s_level[0].link[2] : -1,
               s_levelCount > 0 ? s_level[0].link[3] : -1);
    KexCampaign_NewGame();
}

static void KexMenu_LoadWorld(void)
{
    int const language = KexLanguage_Index();
    char filename[32];
    if (language == 0)
        Bstrcpy(filename, "kexworld.txt");
    else
        Bsnprintf(filename, sizeof(filename), "kexworld_%s.txt", KexLanguage_Code());
    s_levelCount = KexTable_Load(filename, KEX_CAMPAIGN_MAX_MAPS, KexMenu_ParseLevel);
    if (s_levelCount == 0 && language != 0)
        s_levelCount = KexTable_Load("kexworld.txt", KEX_CAMPAIGN_MAX_MAPS,
                                     KexMenu_ParseLevel);
    s_worldLanguage = language;

    // The tool writes both cursor ranges beside the art. Count their present
    // tiles so a partial optional conversion safely falls back to the HD set.
    s_eyeFrames = 0;
    for (int i = 0; i < 64; i++)
    {
        if (tilesiz[kKexTileEyeBase + i].x <= 0)
            break;
        s_eyeFrames = i + 1;
    }
    s_originalEyeFrames = 0;
    for (int i = 0; i < 64; i++)
    {
        if (tilesiz[kKexTileEyeOriginalBase + i].x <= 0)
            break;
        s_originalEyeFrames = i + 1;
    }
    KexMenu_RunWorldSelftest();
    LOG_F(INFO, "Kex overworld: %d levels, %d/%d HD/original eye frames",
          s_levelCount, s_eyeFrames, s_originalEyeFrames);
}

static void KexMenu_EnsureWorld(void)
{
    if (s_levelCount < 0 || s_worldLanguage != KexLanguage_Index())
        KexMenu_LoadWorld();
}

static void KexMenu_SaveSlotLabel(int nSlot, char *label, int labelSize)
{
    if (!menu_SaveSlotUsed(nSlot))
    {
        Bsnprintf(label, labelSize, "%s %d",
                  KexMenu_InvText("m_empty_slot", "EMPTY SLOT"), nSlot + 1);
        return;
    }

    char map[BMAX_PATH] = "";
    int complete = 0;
    if (KexInv_SaveSlotInfo(nSlot, map, sizeof(map), &complete))
    {
        if (complete)
        {
            Bstrncpyz(label, "CAMPAIGN COMPLETE", labelSize);
            return;
        }

        char *leaf = map;
        for (char *p = map; *p; p++)
            if (*p == '/' || *p == '\\')
                leaf = p + 1;

        KexMenu_EnsureWorld();
        for (int i = 0; i < s_levelCount; i++)
            if (!Bstrcasecmp(leaf, s_level[i].map))
            {
                Bstrncpyz(label, s_level[i].title, labelSize);
                if (KexLanguage_Index() == 0)
                    Bstrupr(label);
                return;
            }

        char fallback[32];
        Bstrncpyz(fallback, !Bstrncasecmp(leaf, "KEX_", 4) ? leaf + 4 : leaf,
                  sizeof(fallback));
        char *dot = Bstrrchr(fallback, '.');
        if (dot && !Bstrcasecmp(dot, ".MAP"))
            *dot = 0;
        for (char *p = fallback; *p; p++)
            if (*p == '_')
                *p = ' ';
        Bstrupr(fallback);
        Bstrncpyz(label, fallback[0] ? fallback : "SAVED GAME", labelSize);
        return;
    }

    Bsnprintf(label, labelSize, "SAVED GAME %d", nSlot + 1);
}

// Returns the chosen level's index, or -1 to go back to the menu.
int KexMenu_Overworld(int start)
{
    KexMenu_EnsureWorld();
    if (s_levelCount <= 0 || tilesiz[kKexTileOverworld].x <= 0)
        return -1;

    int const original = kex_original_overworld
                      && tilesiz[kKexTileOverworldOriginal].x > 0
                      && s_originalEyeFrames > 0;
    int const mapTile = original ? kKexTileOverworldOriginal : kKexTileOverworld;
    int const eyeBase = original ? kKexTileEyeOriginalBase : kKexTileEyeBase;
    int const eyeFrames = original ? s_originalEyeFrames : s_eyeFrames;

    int cur = clamp(start, 0, s_levelCount - 1);

    int const mw = tilesiz[mapTile].x, mh = tilesiz[mapTile].y;
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
        int const mapX = original ? L.originalX : L.x;
        int const mapY = original ? L.originalY : L.y;
        int const px = mulscale16(mapX, z), py = mulscale16(mapY, z);
        int ox = clamp(px - 160, 0, max(0, mulscale16(mw, z) - 320));
        int oy = clamp(py - 100, 0, max(0, mulscale16(mh, z) - 200));

        rotatesprite((-ox) << 16, (-oy) << 16, z, 0, mapTile, 0, 0,
                     RS_TOPLEFT | RS_AUTO | RS_NOCLIP, 0, 0, xdim - 1, ydim - 1);

        // Neighbours first, so the marker sits on top of its own arrows.
        static int const adx[4] = { 0, 1, 0, -1 }, ady[4] = { -1, 0, 1, 0 };
        static int const hdArrow[4] = { kKexTileArrowUp, kKexTileArrowRight,
                                        kKexTileArrowDown, kKexTileArrowLeft };
        static int const originalArrow[4] = { kKexTileArrowOriginalUp,
                                              kKexTileArrowOriginalRight,
                                              kKexTileArrowOriginalDown,
                                              kKexTileArrowOriginalLeft };
        for (int d = 0; d < 4; d++)
        {
            if (L.link[d] < 0 || L.link[d] >= s_levelCount)
                continue;
            int const t = original ? originalArrow[d] : hdArrow[d];
            if (tilesiz[t].x <= 0)
                continue;
            int const aw = tilesiz[t].x / 5, ah = tilesiz[t].y / 5;
            KexDrawTile(t, px - ox + adx[d] * 21 - aw / 2, py - oy + ady[d] * 18 - ah / 2,
                        aw, ah, 0, 0, 0);
        }

        // The marker is the Eye of Ra, spinning: one tile per frame of the
        // remaster's own 31-frame cycle, stepped off the game clock.
        int const frame = eyeFrames > 0 ? (((int)totalclock / 3) % eyeFrames) : -1;
        int const eye = frame >= 0 ? eyeBase + frame : kKexTileMarker;
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

        KexMenu_Hint("ENTER", KexMenu_InvText("m_prompt_select", "SELECT ITEM"),
                     "ESC", KexMenu_InvText("m_prompt_menu", "OPEN MENU"));
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
        if (dir >= 0 && L.link[dir] >= 0 && L.link[dir] < s_levelCount
            && KexCampaign_MapUnlocked(L.link[dir]))
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
    KexMenu_EnsureWorld();
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

static int KexMenu_FindLevel(char const *map)
{
    char base[BMAX_PATH];
    Bstrncpyz(base, map ? map : "", sizeof(base));
    char *last = base;
    for (char *p = base; *p; p++)
        if (*p == '/' || *p == '\\')
            last = p + 1;
    if (last != base)
    {
        char leaf[BMAX_PATH];
        Bstrncpyz(leaf, last, sizeof(leaf));
        Bstrncpyz(base, leaf, sizeof(base));
    }

    for (int i = 0; i < s_levelCount; i++)
        if (!Bstrcasecmp(base, s_level[i].map))
            return i;
    return -1;
}

const char *KexMenu_ChooseNextMap(const char *current)
{
    KexMenu_EnsureWorld();
    if (s_levelCount <= 0)
        return NULL;

    int start = KexMenu_FindLevel(current);
    if (start < 0)
        start = 0;

    // kexSlaveGame::UnlockAdjacentMaps marks all four authored neighbours
    // when a level is completed. The overworld accepts movement only when the
    // destination byte is set; selecting the current map remains legal.
    KexMenu_UnlockCompletedLevel(start);

    playMusicFile("music/overworld.ogg", true);
    int const pick = KexMenu_Overworld(start);
    if (pick < 0)
    {
        char const *const track = Kex_MusicForMap(current);
        if (track)
            playMusicFile(track, true);
        return NULL;
    }
    KexCampaign_UnlockMap(pick);
    Bstrncpyz(s_chosenMap, s_level[pick].map, sizeof(s_chosenMap));
    return s_chosenMap;
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
    kKexTileHudAirBack = 7068, kKexTileHudAirFront = 7069,   // the underwater gauge
    kKexTileHudDolphin = 7070, kKexTileHudVulture  = 7071,   // the team-doll badges
    kKexTileHudSpark   = 7072,                               // the jewel's heading spark
    kKexTileHudWings   = 7073,                               // widescreen scarab replacement
    kKexTileHudWingsR  = 7074,
};

// kexSlaveHudLocal draws these exact rectangles in its 320x240 source canvas:
//
//   head_left (0,192,40,48)       head_right (280,192,40,48)
//   main_left (24,216,136,24)     main_right (160,216,136,24)
//   blue gauge (54,222,86,8)      red gauge (196,222,86,8)
//
// The original Build HUD coordinate system is 320x200, whose pixels are 6/5
// taller.  Preserve every source x coordinate and compress only y by 5/6;
// kexmenu.py pre-compresses the art by the same amount so rotatesprite does
// not letterbox it.  The previous code incorrectly treated the individual
// source files as a 416-unit strip, shrinking the two main frames and moving
// both gauges inward.
#define KEX_HUD_Y(y)      (((y) * 5 + 3) / 6)
#define KEX_HUD_MAIN_L_X  24
#define KEX_HUD_MAIN_R_X  160
#define KEX_HUD_MAIN_Y    KEX_HUD_Y(216)
#define KEX_HUD_MAIN_W    136
#define KEX_HUD_MAIN_H    KEX_HUD_Y(24)
#define KEX_HUD_HEAD_Y    KEX_HUD_Y(192)
#define KEX_HUD_HEAD_W    40
#define KEX_HUD_HEAD_H    KEX_HUD_Y(48)
#define KEX_HUD_SLOT_W    86
#define KEX_HUD_SLOT_H    KEX_HUD_Y(8)
#define KEX_HUD_SLOT_Y    KEX_HUD_Y(222)
#define KEX_HUD_BLUE_X    54
#define KEX_HUD_RED_X     196

// Flat fills have to be tiled rather than stretched: KexDrawTile reconciles the
// two axes by taking the smaller scale, so a square swatch asked for an 86x7
// rectangle would come out seven wide. Square cells the height of the slot give
// the run its real length, and the overlap of the pulled-back last cell is
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

// The remaster's underwater air gauge (gfx/airsupply_back, _front): a red disc
// with a blue sphere over it that shrinks with the air left. It slides down
// from above the screen to rest centred at (272, 52) of the remaster's
// 320x240 canvas -- a quarter of the remaining way per frame -- and back up
// at 8% per frame when the player surfaces; the sphere is the disc's 64
// units scaled by air / 64, the remaster's full lungful. Here the canvas is
// 320x200, so the y's are 5/6 of those, and Player::nAir is the source's
// native 0..64 supply while KEX mode is active.
static void KexHud_DrawAir(void)
{
    static float s_airY = -300.f;
    Player const &pl = PlayerList[nLocalPlayer];
    int under = KexInv_TestAir();
    if (!under && pl.nSprite >= 0 && sprite[pl.nSprite].sectnum >= 0)
        under = (SectFlag[sprite[pl.nSprite].sectnum] & kSectUnderwater) != 0;
    s_airY += ((under ? 116.f : -300.f) - s_airY) * (under ? 0.25f : 0.08f);
    if (s_airY < -60.f || tilesiz[kKexTileHudAirBack].x <= 0)
        return;
    int const cx = 272, cy = (int)((s_airY - 64.f) * (200.f / 240.f));
    int const bw = 64, bh = 53;    // 64x64, squashed to 320x200 by kexmenu.py
    KexDrawTile(kKexTileHudAirBack, cx - bw / 2, cy - bh / 2, bw, bh, 0, 0, 0);
    int const air = KexInv_TestAir() ? 60 : clamp((int)pl.nAir, 0, 64);
    int const fw = bw * air / 64, fh = bh * air / 64;
    if (fw > 0 && fh > 0)
        KexDrawTile(kKexTileHudAirFront, cx - fw / 2, cy - fh / 2, fw, fh, 0, 0, 0);
}

// The centre jewel's spark: the remaster draws one of three jewel frames
// from hud.png, mirrored as needed, so a blue spark sits at the point of
// the jewel's rim the player is heading toward, north up -- a compass in
// the bar. Kex yaw 0 faces +Y (north; x = sin yaw), clockwise; Build's
// angle 0 faces east with y down, so north is 1536 and yaw = angle + 512.
// The spark is one tile here, at eight points around the jewel's centre.
static void KexHud_DrawSpark(void)
{
    if (tilesiz[kKexTileHudSpark].x <= 0)
        return;
    int const yaw = (fix16_to_int(PlayerList[nLocalPlayer].q16angle) + 512) & kAngleMask;
    int const sector = ((yaw + 128) >> 8) & 7;          // 0 north, clockwise
    static int8_t const dx[8] = { 0, 6, 8, 6, 0, -6, -8, -6 };
    static int8_t const dy[8] = { -7, -5, 0, 5, 7, 5, 0, -5 };
    int const cx = 160, cy = KEX_HUD_MAIN_Y + KEX_HUD_MAIN_H / 2;
    int const w = tilesiz[kKexTileHudSpark].x, h = tilesiz[kKexTileHudSpark].y;
    KexDrawTile(kKexTileHudSpark, cx + dx[sector] - w / 2, cy + dy[sector] - h / 2, w, h, 0, 0, 0);
}

// The team-doll rewards' badges at the top left, where the remaster puts
// them: the Dolphin at ten dolls (2x swim speed), the Vulture at fourteen
// (hold jump to fly). The remaster pulses each with a slow
// sine; Build cannot overbright, so this one dims and returns instead.
static void KexHud_DrawBadges(void)
{
    int const dolls = KexInv_DollCount(nLocalPlayer);
    if (dolls < KEX_DOLLS_DOLPHIN)
        return;
    double const t = (double)(int)totalclock * (1000.0 / 120.0) * 0.0035;
    int const shade = 6 - (int)(6.0 * sin(t));
    if (tilesiz[kKexTileHudDolphin].x > 0)
        KexDrawTile(kKexTileHudDolphin, 25, 10, 16, 27, shade, 0, 0);
    if (dolls >= KEX_DOLLS_VULTURE && tilesiz[kKexTileHudVulture].x > 0)
        KexDrawTile(kKexTileHudVulture, 52, 10, 28, 27, shade, 0, 0);
}

int KexHud_Available(void)
{
    return KexMenu_Available() && tilesiz[kKexTileHudMainL].x > 0;
}

void KexHud_Draw(void)
{
    if (!KexHud_Available())
        return;

    // Keep the source's 320-wide HUD canvas centred when the game has room
    // beyond the 4:3-safe area.  All of the measurements below remain the
    // remaster's native ones rather than being fitted to that wider viewport.
    int const canvasW = max(320, (xdim * 200) / max(1, ydim));
    int const canvasX = (canvasW - 320) / 2;
    int const lmain = canvasX + KEX_HUD_MAIN_L_X;
    int const rmain = canvasX + KEX_HUD_MAIN_R_X;

    int const air   = clamp((int)airframe * 100 / 16, 0, 100);
    int const hp    = PlayerList[nLocalPlayer].nHealth;
    int const hpPct = clamp((hp * 100) / KexInv_MaxHealth(nLocalPlayer), 0, 100);

    // Fills first: the slot is a hole in the frame, so the frame laid over them
    // clips each bar to the slot's slanted end for free. Air on the left,
    // health on the right, the way the remaster reads them.
    KexHud_Bar(canvasX + KEX_HUD_BLUE_X, kKexTileHudBlue, air);
    KexHud_Bar(canvasX + KEX_HUD_RED_X,  kKexTileHudRed,  hpPct);

    KexDrawTile(kKexTileHudMainL, lmain, KEX_HUD_MAIN_Y,
                KEX_HUD_MAIN_W, KEX_HUD_MAIN_H, 0, 0, 0);
    KexDrawTile(kKexTileHudMainR, rmain, KEX_HUD_MAIN_Y,
                KEX_HUD_MAIN_W, KEX_HUD_MAIN_H, 0, 0, 0);

    // Heads last: as in kexSlaveHudLocal, they overlap the outer 16 pixels of
    // the main frames and overhang them by 20 Build lines.
    KexDrawTile(kKexTileHudHeadL, canvasX, KEX_HUD_HEAD_Y,
                KEX_HUD_HEAD_W, KEX_HUD_HEAD_H, 0, 0, 0);
    KexDrawTile(kKexTileHudHeadR, canvasX + 280, KEX_HUD_HEAD_Y,
                KEX_HUD_HEAD_W, KEX_HUD_HEAD_H, 0, 0, 0);

    extern char message_text[80];
    extern short message_timer;

    // The rest of DrawStatus() never ran in this mode, which silently dropped
    // the message line -- the pickup captions kexpickup.txt exists to show --
    // and the ammo count the remaster prints beside the health bar.
    int const nWeapon = PlayerList[nLocalPlayer].nCurrentWeapon;
    if (nWeapon > 0 && nWeapon < kWeaponMummified && WeaponInfo[nWeapon].nAmmoType >= 0)
    {
        char ammoBuf[16];
        Bsprintf(ammoBuf, "%d", PlayerList[nLocalPlayer].nAmmo[nWeapon]);
        KexMenu_Text(rmain + 24, KEX_HUD_SLOT_Y + 1, ammoBuf, 7, 0, 0, 1);
    }
    KexHud_DrawSpark();
    KexHud_DrawAir();
    KexHud_DrawBadges();
    // Pickup captions: the remaster stacks them at the top of the screen,
    // centred, newest at the bottom, in the gold menu font, and pulses each
    // slowly while it lasts. Build cannot overbright, so the pulse dims and
    // returns, about once a second.
    {
        double const t = (double)(int)totalclock * (1000.0 / 120.0) * 0.006;
        int const shade = 6 - (int)(6.0 * sin(t));
        char const *text; int ticks;
        for (int i = 0; KexInv_CaptionLine(i, &text, &ticks); i++)
            KexMenu_Text(160, 12 + i * 13, text, 10, shade, 0, 1, 0);
    }
    if (kex_show_hud_messages && message_timer && message_text[0])
        printext(0, 0, message_text, kTileFont);

    if (kex_show_play_time)
    {
        uint64_t const seconds = KexPlayTime_Seconds();
        uint64_t const hours = seconds / 3600;
        unsigned const minutes = (unsigned)((seconds / 60) % 60);
        unsigned const secs = (unsigned)(seconds % 60);
        char playTime[32];
        Bsnprintf(playTime, sizeof(playTime), "%02llu:%02u:%02u",
                  (unsigned long long)hours, minutes, secs);
        KexMenu_Text(8, KEX_HUD_MAIN_Y - 11, playTime, 6, 10, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// The status screen: the console game's inventory, which the remaster kept.
//
// Four pages -- Map, Arsenal, Artifacts, Transmitter -- walked with the left
// and right arrows, the up and down arrows picking an item on the page so its
// description (the remaster's own wording, kexinv.txt) shows in the plate at
// the bottom. Every icon is the remaster's, at its own aspect; the plates are
// the bar_left/bar_right pair the remaster draws its menu titles on.
// ---------------------------------------------------------------------------
enum
{
    kKexTileInvWeapon    = 7400,
    kKexTileInvKey       = 7410,   // + 2 * key + lit
    kKexTileInvArtifact  = 7420,
    kKexTileInvTrans     = 7430,
    kKexTileInvTransOn   = 7438,
    kKexTileInvDoll      = 7440,
    kKexTileInvMapClosed = 7441,
    kKexTileInvMapOpen   = 7442,
    kKexTileInvPlateL    = 7443,
    kKexTileInvPlateR    = 7444,
    kKexTileInvArrowL    = 7445,
    kKexTileInvArrowR    = 7446,
    kKexTileInvSlot      = 7447,
};

enum { kKexPageMap = 0, kKexPageArsenal, kKexPageArtifacts, kKexPageTransmitter, kKexPageCount };

// A tile centred in a box at its own aspect, never larger than the box.
static void KexDrawFit(int tile, int cx, int cy, int boxw, int boxh, int shade, int stat = 0)
{
    if (tilesiz[tile].x <= 0 || tilesiz[tile].y <= 0)
        return;
    int const z = min(divscale16(boxw, tilesiz[tile].x), divscale16(boxh, tilesiz[tile].y));
    int const w = mulscale16(tilesiz[tile].x, z), h = mulscale16(tilesiz[tile].y, z);
    KexDrawTile(tile, cx - w / 2, cy - h / 2, w, h, shade, 0, stat);
}

// Up to two lines, split at the newline KexInv_Format produced.
static void KexMenu_Desc(int y, char const *text)
{
    char buf[256];
    Bstrncpyz(buf, text, sizeof(buf));
    char *nl = Bstrchr(buf, '\n');
    if (nl) *nl++ = 0;
    KexMenu_Text(160, y, buf, 8, 0, 0, 1);
    if (nl && *nl)
        KexMenu_Text(160, y + 11, nl, 7, 0, 0, 1, 1);
}

// The remaster's weapon order is the console game's; Build's differs in two
// places (flamer and grenade swap), and the eighth, the Manacle, has no Build
// weapon at all.
static int const s_kexWeaponOfIcon[8] = { kWeaponSword, kWeaponPistol, kWeaponM60, kWeaponGrenade,
                                          kWeaponFlamer, kWeaponStaff, kWeaponRing, kWeaponManacle };

// Where each transmitter piece sits in the assembled device, in the pieces'
// own pixels: the monitor (7) with its screen (6, lit when complete), the
// keyboard base (1) under it, headphones (3) and microphone (2) to the right,
// the power pack (4), module (0) and chip (5) on the left.
static int const s_kexTransPos[KEX_TRANS_PIECES][2] = {
    { 4, 52 }, { 20, 46 }, { 78, 40 }, { 76, 6 }, { 0, 30 }, { 2, 64 }, { 36, 8 }, { 30, 0 } };
#define KEX_TRANS_W 100
#define KEX_TRANS_H 78

static void KexMenu_StatusPage(int page, int sel)
{
    Player const &P = PlayerList[nLocalPlayer];
    KexInv const &inv = KexInvList[nLocalPlayer];
    char buf[256];
    char key[32];
    char const *desc = NULL;

    switch (page)
    {
        case kKexPageMap:
        {
            // The scroll, open once this level's map is in hand; the keys in
            // the remaster's order; the team doll and its tally.
            KexDrawFit(inv.map ? kKexTileInvMapOpen : kKexTileInvMapClosed, 70, 92, 64, 52, 0);
            for (int k = 0; k < 4; k++)
            {
                int const lit = (P.keys & (0x1000 << k)) != 0;
                KexDrawFit(kKexTileInvKey + 2 * k + lit, 124 + k * 26, 76, 22, 21, 0);
            }
            KexDrawFit(kKexTileInvDoll, 240, 92, 40, 54, KexInv_DollCount(nLocalPlayer) ? 0 : 48,
                       KexInv_DollCount(nLocalPlayer) ? 0 : RS_TRANS1);
            KexInv_Format(buf, sizeof(buf), "doll", KexInv_DollCount(nLocalPlayer));
            desc = buf;
            break;
        }
        case kKexPageArsenal:
        {
            for (int i = 0; i < 8; i++)
            {
                int const cx = 52 + (i % 4) * 72, cy = 64 + (i / 4) * 50;
                int const w = s_kexWeaponOfIcon[i];
                int const owned = w >= 0 && (nPlayerWeapons[nLocalPlayer] & (1 << w));
                KexDrawFit(kKexTileInvSlot, cx, cy, 68, 46, sel == i ? -12 : 8, 0);
                KexDrawFit(kKexTileInvWeapon + i, cx, cy - 3, 56, 30, owned ? 0 : 48, owned ? 0 : RS_TRANS1);
                if (owned && WeaponInfo[w].nAmmoType > 0)
                {
                    Bsprintf(buf, "%d", P.nAmmo[WeaponInfo[w].nAmmoType]);
                    KexMenu_Text(cx + 30, cy + 12, buf, 6, 0, 0, 1, 1);
                }
                if (sel == i)
                {
                    Bsprintf(key, "weap%d", i);
                    KexInv_Format(buf, sizeof(buf), key, 0);
                    desc = buf;
                }
            }
            break;
        }
        case kKexPageArtifacts:
        {
            for (int i = 0; i < kKexArtiCount; i++)
            {
                int const cx = 64 + (i % 3) * 96, cy = 64 + (i / 3) * 52;
                int const owned = KexInv_HasArtifact(nLocalPlayer, i);
                KexDrawFit(kKexTileInvSlot, cx, cy, 88, 48, sel == i ? -12 : 8, 0);
                KexDrawFit(kKexTileInvArtifact + i, cx, cy, 72, 40, owned ? 0 : 48, owned ? 0 : RS_TRANS1);
                if (sel == i)
                {
                    Bsprintf(key, owned ? "arti%d" : "artiname%d", i);
                    KexInv_Format(buf, sizeof(buf), key, 0);
                    desc = buf;
                }
            }
            if (!inv.artifacts)
            {
                KexInv_Format(buf, sizeof(buf), "noarti", 0);
                desc = buf;
            }
            break;
        }
        case kKexPageTransmitter:
        {
            int const n = KexInv_TransCount(nLocalPlayer);
            int const complete = n >= KEX_TRANS_PIECES;
            // Native pixels 1:1 (100x78), the whole device centred on the page.
            int const ox = 160 - KEX_TRANS_W / 2, oy = 96 - KEX_TRANS_H / 2;
            for (int i = 0; i < KEX_TRANS_PIECES; i++)
            {
                int const have = (inv.transmitter >> i) & 1;
                int const tile = (i == 6 && complete) ? kKexTileInvTransOn : kKexTileInvTrans + i;
                if (tilesiz[tile].x <= 0)
                    continue;
                KexDrawTile(tile, ox + s_kexTransPos[i][0], oy + s_kexTransPos[i][1],
                            tilesiz[tile].x, tilesiz[tile].y, have ? 0 : 30, 0, have ? 0 : RS_TRANS1);
            }
            KexInv_Format(buf, sizeof(buf), complete ? "transfull" : n ? "transn" : "trans0", n);
            desc = buf;
            break;
        }
    }

    if (desc)
        KexMenu_Desc(146, desc);
}

void KexMenu_Status(bool weaponWheel)
{
    if (!KexMenu_Available() || tilesiz[kKexTileInvSlot].x <= 0)
        return;

    static int const s_items[kKexPageCount] = { 0, 8, kKexArtiCount, 0 };
    static int s_inventoryPage = kKexPageArsenal; // the inventory page you left is the page you return to
    int page = weaponWheel ? kKexPageArsenal : s_inventoryPage;
    int sel = 0;
    if (weaponWheel)
    {
        // Start on the equipped weapon.  The wheel intentionally exposes the
        // arsenal only; Inventory keeps the complete status interface below.
        for (int i = 0; i < 8; i++)
            if (s_kexWeaponOfIcon[i] == PlayerList[nLocalPlayer].nCurrentWeapon)
                sel = i;
    }

    // Unattended (VB_KEXINV_TEST): hold each page for a moment, screenshot it,
    // move on, and leave after the last.
    int const autoFrames = KexInv_TestActive() ? 30 : 0;
    int frame = 0, shown = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);   // the level's view window clips the top otherwise

    while (1)
    {
        HandleAsync();

        // The menu's backdrop over the whole view, and the stone panel framed
        // on it like the menu's own.
        KexMenu_Backdrop();
        KexDrawBorder(8, 8, 312, 192, 4);
        KexDrawPanel(8, 8, 312, 192, 0);

        // Title plates with the page's name between the arrows, and STATUS
        // above it in the font's shipped colours.
        KexDrawTile(kKexTileInvPlateL, 0, 16, 160, 22, 0, 0, 0);
        KexDrawTile(kKexTileInvPlateR, 160, 16, 160, 22, 0, 0, 0);
        char title[64], pageKey[16];
        Bsprintf(pageKey, "page%d", page);
        Bstrncpyz(title, KexInv_Text(pageKey) ? KexInv_Text(pageKey) : pageKey, sizeof(title));
        Bstrupr(title);
        KexMenu_Text(160, 3, KexInv_Text("status") ? KexInv_Text("status") : "STATUS", 10, 0, 0, 1, 1);
        KexMenu_Text(160, 20, title, 12, 0, 0, 1);
        int const tw = KexMenu_TextWidth(title, 12);
        KexDrawFit(kKexTileInvArrowL, 160 - tw / 2 - 14, 27, 12, 10, 0);
        KexDrawFit(kKexTileInvArrowR, 160 + tw / 2 + 14, 27, 12, 10, 0);

        // The description plate.
        KexDrawTile(kKexTileRow, 30, 142, 260, 28, 0, 0, 0);

        KexMenu_StatusPage(page, sel);

        KexMenu_Hint(weaponWheel ? "ENTER" : "ESC",
                     weaponWheel ? KexMenu_InvText("m_prompt_select", "SELECT")
                                 : KexMenu_InvText("m_prompt_back", "GO BACK"));
        videoNextPage();
        Exhumed_CaptureFrame();

        if (autoFrames && ++frame % autoFrames == 0)
        {
            videoCaptureScreen("cpshot0000.png", 0);
            if (++shown >= kKexPageCount) { s_inventoryPage = kKexPageArsenal; return; }
            page = (page + 1) % kKexPageCount;
            sel = 1;
        }

        int const n = s_items[page];
        switch (KexMenu_PollKey())
        {
            case kKexKeyLeft:
                if (weaponWheel) sel = (sel & ~3) | ((sel + 3) & 3);
                else { page = (page + kKexPageCount - 1) % kKexPageCount; sel = 0; }
                KexMenu_Beep();
                break;
            case kKexKeyRight:
                if (weaponWheel) sel = (sel & ~3) | ((sel + 1) & 3);
                else { page = (page + 1) % kKexPageCount; sel = 0; }
                KexMenu_Beep();
                break;
            case kKexKeyUp:
                if (weaponWheel) sel ^= 4;
                else if (n) sel = (sel + n - 1) % n;
                KexMenu_Beep();
                break;
            case kKexKeyDown:
                if (weaponWheel) sel ^= 4;
                else if (n) sel = (sel + 1) % n;
                KexMenu_Beep();
                break;
            case kKexKeyEnter:
                if (weaponWheel)
                {
                    int const weapon = s_kexWeaponOfIcon[sel];
                    if (nPlayerWeapons[nLocalPlayer] & (1 << weapon))
                    {
                        StopFiringWeapon(nLocalPlayer);
                        SetNewWeapon(nLocalPlayer, weapon);
                        KexMenu_Beep();
                        return;
                    }
                    KexMenu_Beep();
                    break;
                }
                s_inventoryPage = page;
                KexMenu_Beep();
                return;
            case kKexKeyEsc:
                if (!weaponWheel) s_inventoryPage = page;
                KexMenu_Beep();
                return;
            default: break;
        }
        if (quitevent)
        {
            if (!weaponWheel) s_inventoryPage = page;
            return;
        }
    }
}
