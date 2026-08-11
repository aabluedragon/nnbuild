// VibeBuild32 JS scripting — Duke3D game bindings.
//
// This TU includes the Duke game headers (which clash with quickjs.h, so the
// QuickJS side lives in vbjs.cpp). It implements the vbjs_game::* seam the
// script API calls into, plus the HUD text draw.
#include "duke3d.h"
#include "funct.h"
#include "gamevars.h"
#include "osd.h"
#include "vbjs.h"

extern hashtable_t h_gamevars;   // gamevars.cpp

static int vbjs_playerSprite(void)
{
    if ((unsigned)myconnectindex >= MAXPLAYERS || !g_player[myconnectindex].ps)
        return -1;
    return g_player[myconnectindex].ps->i;
}

namespace vbjs_game {

void log(const char *s)
{
    LOG_F(INFO, "[js] %s", s ? s : "");
    OSD_Printf("[js] %s\n", s ? s : "");
}

int spawn(int tile, int x, int y, int z, int owner)
{
    if ((unsigned)tile >= MAXTILES) return -1;
    int const psp   = vbjs_playerSprite();
    int const psect = (psp >= 0) ? sprite[psp].sectnum : -1;

    int sect = (owner >= 0 && owner < MAXSPRITES && sprite[owner].sectnum >= 0) ? sprite[owner].sectnum : psect;

    if (x == 0 && y == 0 && z == 0 && psp >= 0)          // no location -> at the player
    {
        x = sprite[psp].x; y = sprite[psp].y; z = sprite[psp].z;
    }
    else if (sect >= 0)                                   // find the sector the point lives in
    {
        int16_t s2 = (int16_t)sect;
        updatesector(x, y, &s2);
        if (s2 >= 0) sect = s2;
    }
    if (sect < 0) return -1;

    return A_InsertSprite(sect, x, y, z, tile, 0, 48, 48, 0, 0, 0, (owner >= 0 ? owner : psp), 1);
}

void sendChat(const char *text)
{
    // v1: show the message locally (user-quote line). Networked broadcast is a
    // later addition once a clean single-call send path is exposed.
    if (text) G_AddUserQuote(text);
}

int getGamevar(const char *name, int spriteNum, int playerNum, int *ok)
{
    int const id = hash_find(&h_gamevars, name);
    if (id < 0) { if (ok) *ok = 0; return 0; }
    if (ok) *ok = 1;
    return Gv_GetVarByLabel(name, 0, spriteNum, playerNum);
}

void setGamevar(const char *name, int value, int spriteNum, int playerNum)
{
    int const id = hash_find(&h_gamevars, name);
    if (id >= 0) Gv_SetVar(id, value, spriteNum, playerNum);
}

int playerSprite(void) { return vbjs_playerSprite(); }

void runCommand(const char *cmd)
{
    if (!cmd) return;
    while (*cmd == '/' || *cmd == ' ') cmd++;   // tolerate a leading console-style '/'
    if (*cmd) OSD_Dispatch(cmd);
}

void spriteXYZ(int spriteNum, int *x, int *y, int *z, int *ok)
{
    if ((unsigned)spriteNum >= MAXSPRITES) { if (ok) *ok = 0; return; }
    if (x) *x = sprite[spriteNum].x;
    if (y) *y = sprite[spriteNum].y;
    if (z) *z = sprite[spriteNum].z;
    if (ok) *ok = 1;
}

} // namespace vbjs_game

// HUD draw: render every script-registered text line (called from the overlay pass).
extern "C" void VBJS_DrawHUD(void)
{
    int n = 0;
    vbjs_text_t *t = VBJS_TextSlots(&n);
    for (int i = 0; i < n; i++)
        if (t[i].used)
            G_PrintGameText(0, STARTALPHANUM, t[i].x, t[i].y, t[i].str, t[i].shade, t[i].pal,
                            8 + 16, 0, 0, xdim - 1, ydim - 1, 65536);
}
