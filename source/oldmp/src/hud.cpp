#include "hud.h"
#include "colmatch.h"

int althud_numbertile = 2930;
int althud_numberpal = 0;
int althud_shadows = 1;
int althud_flashing = 1;

int hud_glowingquotes = 1;
int hud_showmapname = 1;

palette_t CrosshairColors = { 255, 255, 0, 0 };
palette_t DefaultCrosshairColors = { 0, 0, 0, 0 };
int32_t g_crosshairSum = 0;

int32_t g_levelTextTime = 0;

static void G_PatchStatusBar(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t const scl = sbarsc(65536);
    int32_t const tx = sbarx16((160 << 16) - (tilesiz[BOTTOMSTATUSBAR].x << 15)); // centered
    int32_t const ty = sbary(200 - tilesiz[BOTTOMSTATUSBAR].y);

    int32_t const clx1 = sbarsc(scale(x1, xdim, 320)), cly1 = sbarsc(scale(y1, ydim, 200));
    int32_t const clx2 = sbarsc(scale(x2, xdim, 320)), cly2 = sbarsc(scale(y2, ydim, 200));
    int32_t const clofx = (xdim - sbarsc(xdim)) >> 1, clofy = (ydim - sbarsc(ydim));

    rotatesprite(tx, ty, scl, 0, BOTTOMSTATUSBAR, 4, 0, 10 + 16 + 64, clx1 + clofx, cly1 + clofy, clx2 + clofx - 1, cly2 + clofy - 1);
}

void G_SetStatusBarScale(int sc)
{
    ud.statusbarscale = min(100, max(10, sc));
    G_UpdateScreenArea();
}

void G_DrawFrags(void)
{
    int32_t const xpos = (320 << 15) - (tilesiz[FRAGBAR].x << 15);
    int32_t const numFragbarRows = (playerswhenstarted-1) >> 2;
    int32_t pNum = 0;

    for (int32_t row = 0; row <= numFragbarRows; row++)
    {
        rotatesprite_fs(xpos, (8 * row) << 16, 65600, 0, FRAGBAR, 0, 0, RS_AUTO | RS_NOCLIP | RS_TOPLEFT | RS_NOMASK);

        for (int32_t column = 0; column < 4; column++, pNum++)
        {
            auto const plr = &g_player[pNum];

            if (pNum >= MAXPLAYERS) break;
            if (!plr->connected) continue;
            
            // Name
            minitext(21 + (73 * column), 2 + (8 * row), &plr->user_name[0], plr->ps->palookup, RS_AUTO | RS_NOCLIP | RS_TOPLEFT);

            // Frag Count
            Bsprintf(tempbuf, "%d", plr->ps->frag - plr->ps->fraggedself);
            minitext(67 + (73 * column), 2 + (8 * row), tempbuf, plr->ps->palookup, RS_AUTO | RS_NOCLIP | RS_TOPLEFT);
        }
    }
}

void G_DrawInventory(DukePlayer_t* p)
{
    int n, j = 0, xoff = 0, y;

    n = (p->jetpack_amount > 0) << 3;
    if (n & 8) j++;
    n |= (p->scuba_amount > 0) << 5;
    if (n & 32) j++;
    n |= (p->steroids_amount > 0) << 1;
    if (n & 2) j++;
    n |= (p->holoduke_amount > 0) << 2;
    if (n & 4) j++;
    n |= (p->firstaid_amount > 0);
    if (n & 1) j++;
    n |= (p->heat_amount > 0) << 4;
    if (n & 16) j++;
    n |= (p->boot_amount > 0) << 6;
    if (n & 64) j++;

    xoff = 160 - (j * 11);

    j = 0;

    //    if (ud.screen_size > 4)
    y = 154;
    //    else y = (ud.drawweapon == 2?150:172);

        /*    if (ud.screen_size == 4 && ud.drawweapon != 2)
            {
                xoff += 65;
                if (ud.multimode > 1)
                    xoff -= 9;
            }
        */
    while (j <= 9)
    {
        if (n & (1 << j))
        {
            switch (n & (1 << j))
            {
            case   1:
                rotatesprite(xoff << 16, y << 16, 65536L, 0, FIRSTAID_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            case   2:
                rotatesprite((xoff + 1) << 16, y << 16, 65536L, 0, STEROIDS_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            case   4:
                rotatesprite((xoff + 2) << 16, y << 16, 65536L, 0, HOLODUKE_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            case   8:
                rotatesprite(xoff << 16, y << 16, 65536L, 0, JETPACK_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            case  16:
                rotatesprite(xoff << 16, y << 16, 65536L, 0, HEAT_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            case  32:
                rotatesprite(xoff << 16, y << 16, 65536L, 0, AIRTANK_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            case 64:
                rotatesprite(xoff << 16, (y - 1) << 16, 65536L, 0, BOOT_ICON, 0, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                break;
            }

            xoff += 22;

            if (p->inven_icon == j + 1)
                rotatesprite((xoff - 2) << 16, (y + 19) << 16, 65536L, 1024, ARROW, -32, 0, 2 + 16, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
        }

        j++;
    }
}

// draws inventory numbers in the HUD for both the full and mini status bars
static void G_DrawInvNum(int x, int y, char num1, char ha, char sbits)
{
    char dabuf[80] = { 0 };
    int shd = (x < 0);

    if (shd) x = -x;

    Bsprintf(dabuf, "%d", num1);
    if (num1 > 99)
    {
        if (shd && ud.screen_size == 4 && videoGetRenderMode() >= 3 && althud_shadows)
        {
            rotatesprite(sbarx(x - 4 + 1), sbary(y + 1), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 4, POLYMOSTTRANS | sbits, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarx(x + 1), sbary(y + 1), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 4, POLYMOSTTRANS | sbits, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarx(x + 4 + 1), sbary(y + 1), sbarsc(65536L), 0, THREEBYFIVE + dabuf[2] - '0', ha, 4, POLYMOSTTRANS | sbits, 0, 0, xdim - 1, ydim - 1);
        }
        rotatesprite(sbarx(x - 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, sbits, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, sbits, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[2] - '0', ha, 0, sbits, 0, 0, xdim - 1, ydim - 1);
        return;
    }
    if (num1 > 9)
    {
        if (shd && ud.screen_size == 4 && videoGetRenderMode() >= 3 && althud_shadows)
        {
            rotatesprite(sbarx(x + 1), sbary(y + 1), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 4, POLYMOSTTRANS | sbits, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarx(x + 4 + 1), sbary(y + 1), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 4, POLYMOSTTRANS | sbits, 0, 0, xdim - 1, ydim - 1);
        }

        rotatesprite(sbarx(x), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, sbits, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, sbits, 0, 0, xdim - 1, ydim - 1);
        return;
    }
    rotatesprite(sbarx(x + 4 + 1), sbary(y + 1), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 4, sbits, 0, 0, xdim - 1, ydim - 1);
    rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, sbits, 0, 0, xdim - 1, ydim - 1);
}

static void G_DrawDigiNum(int x, int y, int n, char s, char cs)
{
    int i, j = 0, k, p, c;
    char b[10];

    Bsnprintf(b, 10, "%d", n);
    i = Bstrlen(b);

    for (k = i - 1; k >= 0; k--)
    {
        p = DIGITALNUM + *(b + k) - '0';
        j += tilesiz[p].x + 1;
    }
    c = x - (j >> 1);

    j = 0;
    for (k = 0; k < i; k++)
    {
        p = DIGITALNUM + *(b + k) - '0';
        rotatesprite(sbarx(c + j), sbary(y), sbarsc(65536L), 0, p, s, 0, cs, 0, 0, xdim - 1, ydim - 1);
        j += tilesiz[p].x + 1;
    }
}

static void G_DrawAltDigiNum(int x, int y, int n, char s, char cs)
{
    int i, j = 0, k, p, c;
    char b[10];
    int rev = (x < 0);
    int shd = (y < 0);

    if (rev) x = -x;
    if (shd) y = -y;

    Bsnprintf(b, 10, "%d", n);
    i = Bstrlen(b);

    for (k = i - 1; k >= 0; k--)
    {
        p = althud_numbertile + *(b + k) - '0';
        j += tilesiz[p].x + 1;
    }
    c = x - (j >> 1);

    if (rev)
    {
        //        j = 0;
        for (k = 0; k < i; k++)
        {
            p = althud_numbertile + *(b + k) - '0';
            if (shd && videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarxr(c + j - 1), sbary(y + 1), sbarsc(65536L), 0, p, s, 4, cs | POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarxr(c + j), sbary(y), sbarsc(65536L), 0, p, s, althud_numberpal, cs, 0, 0, xdim - 1, ydim - 1);
            j -= tilesiz[p].x + 1;
        }
        return;
    }
    j = 0;
    for (k = 0; k < i; k++)
    {
        p = althud_numbertile + *(b + k) - '0';
        if (shd && videoGetRenderMode() >= 3 && althud_shadows)
            rotatesprite(sbarx(c + j + 1), sbary(y + 1), sbarsc(65536L), 0, p, s, 4, cs | POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(c + j), sbary(y), sbarsc(65536L), 0, p, s, althud_numberpal, cs, 0, 0, xdim - 1, ydim - 1);
        j += tilesiz[p].x + 1;
    }
}

static void G_DrawWeapNum(short ind, int x, int y, int num1, int num2, char ha)
{
    char dabuf[80] = { 0 };

    rotatesprite(sbarx(x - 7), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + ind + 1, ha - 10, 7, 10, 0, 0, xdim - 1, ydim - 1);
    rotatesprite(sbarx(x - 3), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + 10, ha, 0, 10, 0, 0, xdim - 1, ydim - 1);

    if (VOLUMEONE && (ind > HANDBOMB_WEAPON || ind < 0))
    {
        minitextshade(x + 1, y - 4, "ORDER", 20, 11, 2 + 8 + 16 + 256);
        return;
    }

    rotatesprite(sbarx(x + 9), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + 11, ha, 0, 10, 0, 0, xdim - 1, ydim - 1);

    if (num1 > 99) num1 = 99;
    if (num2 > 99) num2 = 99;

    Bsprintf(dabuf, "%d", num1);
    if (num1 > 9)
    {
        rotatesprite(sbarx(x), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
    }
    else rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);

    Bsprintf(dabuf, "%d", num2);
    if (num2 > 9)
    {
        rotatesprite(sbarx(x + 13), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 17), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        return;
    }
    rotatesprite(sbarx(x + 13), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
}

static void G_DrawWeapNum2(char ind, int x, int y, int num1, int num2, char ha)
{
    char dabuf[80] = { 0 };

    rotatesprite(sbarx(x - 7), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + ind + 1, ha - 10, 7, 10, 0, 0, xdim - 1, ydim - 1);
    rotatesprite(sbarx(x - 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + 10, ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
    rotatesprite(sbarx(x + 13), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + 11, ha, 0, 10, 0, 0, xdim - 1, ydim - 1);

    Bsprintf(dabuf, "%d", num1);
    if (num1 > 99)
    {
        rotatesprite(sbarx(x), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 8), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[2] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
    }
    else if (num1 > 9)
    {
        rotatesprite(sbarx(x + 4), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 8), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
    }
    else rotatesprite(sbarx(x + 8), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);

    Bsprintf(dabuf, "%d", num2);
    if (num2 > 99)
    {
        rotatesprite(sbarx(x + 17), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 21), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 25), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[2] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
    }
    else if (num2 > 9)
    {
        rotatesprite(sbarx(x + 17), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(sbarx(x + 21), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[1] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
        return;
    }
    else
        rotatesprite(sbarx(x + 25), sbary(y), sbarsc(65536L), 0, THREEBYFIVE + dabuf[0] - '0', ha, 0, 10, 0, 0, xdim - 1, ydim - 1);
}

static void G_DrawWeapAmounts(DukePlayer_t* p, int x, int y, int u)
{
    int cw = p->curr_weapon;

    if (u & 4)
    {
        if (u != -1) G_PatchStatusBar(88, 178, 88 + 37, 178 + 6); //original code: (96,178,96+12,178+6);
        G_DrawWeapNum2(PISTOL_WEAPON, x, y,
            p->ammo_amount[PISTOL_WEAPON], p->max_ammo_amount[PISTOL_WEAPON],
            12 - 20 * (cw == PISTOL_WEAPON));
    }
    if (u & 8)
    {
        if (u != -1) G_PatchStatusBar(88, 184, 88 + 37, 184 + 6); //original code: (96,184,96+12,184+6);
        G_DrawWeapNum2(SHOTGUN_WEAPON, x, y + 6,
            p->ammo_amount[SHOTGUN_WEAPON], p->max_ammo_amount[SHOTGUN_WEAPON],
            (!p->gotweapon[SHOTGUN_WEAPON] * 9) + 12 - 18 *
            (cw == SHOTGUN_WEAPON));
    }
    if (u & 16)
    {
        if (u != -1) G_PatchStatusBar(88, 190, 88 + 37, 190 + 6); //original code: (96,190,96+12,190+6);
        G_DrawWeapNum2(CHAINGUN_WEAPON, x, y + 12,
            p->ammo_amount[CHAINGUN_WEAPON], p->max_ammo_amount[CHAINGUN_WEAPON],
            (!p->gotweapon[CHAINGUN_WEAPON] * 9) + 12 - 18 *
            (cw == CHAINGUN_WEAPON));
    }
    if (u & 32)
    {
        if (u != -1) G_PatchStatusBar(127, 178, 127 + 29, 178 + 6); //original code: (135,178,135+8,178+6);
        G_DrawWeapNum(RPG_WEAPON, x + 39, y,
            p->ammo_amount[RPG_WEAPON], p->max_ammo_amount[RPG_WEAPON],
            (!p->gotweapon[RPG_WEAPON] * 9) + 12 - 19 *
            (cw == RPG_WEAPON));
    }
    if (u & 64)
    {
        if (u != -1) G_PatchStatusBar(127, 184, 127 + 29, 184 + 6); //original code: (135,184,135+8,184+6);
        G_DrawWeapNum(HANDBOMB_WEAPON, x + 39, y + 6,
            p->ammo_amount[HANDBOMB_WEAPON], p->max_ammo_amount[HANDBOMB_WEAPON],
            (((!p->ammo_amount[HANDBOMB_WEAPON]) | (!p->gotweapon[HANDBOMB_WEAPON])) * 9) + 12 - 19 *
            ((cw == HANDBOMB_WEAPON) || (cw == HANDREMOTE_WEAPON)));
    }
    if (u & 128)
    {
        if (u != -1) G_PatchStatusBar(127, 190, 127 + 29, 190 + 6); //original code: (135,190,135+8,190+6);

        if (p->subweapon & (1 << GROW_WEAPON))
            G_DrawWeapNum(SHRINKER_WEAPON, x + 39, y + 12,
                p->ammo_amount[GROW_WEAPON], p->max_ammo_amount[GROW_WEAPON],
                (!p->gotweapon[GROW_WEAPON] * 9) + 12 - 18 *
                (cw == GROW_WEAPON));
        else
            G_DrawWeapNum(SHRINKER_WEAPON, x + 39, y + 12,
                p->ammo_amount[SHRINKER_WEAPON], p->max_ammo_amount[SHRINKER_WEAPON],
                (!p->gotweapon[SHRINKER_WEAPON] * 9) + 12 - 18 *
                (cw == SHRINKER_WEAPON));
    }
    if (u & 256)
    {
        if (u != -1) G_PatchStatusBar(158, 178, 162 + 29, 178 + 6); //original code: (166,178,166+8,178+6);

        G_DrawWeapNum(DEVISTATOR_WEAPON, x + 70, y,
            p->ammo_amount[DEVISTATOR_WEAPON], p->max_ammo_amount[DEVISTATOR_WEAPON],
            (!p->gotweapon[DEVISTATOR_WEAPON] * 9) + 12 - 18 *
            (cw == DEVISTATOR_WEAPON));
    }
    if (u & 512)
    {
        if (u != -1) G_PatchStatusBar(158, 184, 162 + 29, 184 + 6); //original code: (166,184,166+8,184+6);

        G_DrawWeapNum(TRIPBOMB_WEAPON, x + 70, y + 6,
            p->ammo_amount[TRIPBOMB_WEAPON], p->max_ammo_amount[TRIPBOMB_WEAPON],
            (!p->gotweapon[TRIPBOMB_WEAPON] * 9) + 12 - 18 *
            (cw == TRIPBOMB_WEAPON));
    }

    if (u & 65536L)
    {
        if (u != -1) G_PatchStatusBar(158, 190, 162 + 29, 190 + 6); //original code: (166,190,166+8,190+6);

        G_DrawWeapNum(-1, x + 70, y + 12,
            p->ammo_amount[FREEZE_WEAPON], p->max_ammo_amount[FREEZE_WEAPON],
            (!p->gotweapon[FREEZE_WEAPON] * 9) + 12 - 18 *
            (cw == FREEZE_WEAPON));
    }
}

void G_DrawStatusBar(int snum)
{
    DukePlayer_t* p = g_player[snum].ps;
    int i, j, o, ss = ud.screen_size, u;
    int permbit = 0;

    if (ss < 4) return;

    if (g_player[snum].ps->gm & MODE_MENU)
        if ((g_currentMenu >= 400 && g_currentMenu <= 405))
            return;

    if (videoGetRenderMode() >= 3) pus = NUMPAGES;   // JBF 20040101: always redraw in GL

    if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
    {
        if (pus)
        {
            G_DrawFrags();
        }
        else
        {
            TRAVERSE_CONNECT(i)
                if (g_player[i].ps->frag != sbar.frag[i])
                {
                    G_DrawFrags();
                    break;
                }

        }
        TRAVERSE_CONNECT(i)
            if (i != myconnectindex)
                sbar.frag[i] = g_player[i].ps->frag;
    }

    if (ss == 4)   //DRAW MINI STATUS BAR:
    {
        if (ud.althud) // althud
        {
            static int ammo_sprites[MAX_WEAPONS];

            if (!ammo_sprites[0])
            {
                /* this looks stupid but it lets us initialize static memory to dynamic values
                   these values can be changed from the CONs with dynamic tile remapping
                   but we don't want to have to recreate the values in memory every time
                   the HUD is drawn */

                int asprites[MAX_WEAPONS] = { BOOTS, AMMO, SHOTGUNAMMO,
                                              BATTERYAMMO, RPGAMMO, HBOMBAMMO, CRYSTALAMMO, DEVISTATORAMMO,
                                              TRIPBOMBSPRITE, FREEZEAMMO + 1, HBOMBAMMO, GROWAMMO
                };
                Bmemcpy(ammo_sprites, asprites, sizeof(ammo_sprites));
            }

            //            rotatesprite(sbarx(5+1),sbary(200-25+1),sbarsc(49152L),0,SIXPAK,0,4,10+16+1+32,0,0,xdim-1,ydim-1);
            //            rotatesprite(sbarx(5),sbary(200-25),sbarsc(49152L),0,SIXPAK,0,0,10+16,0,0,xdim-1,ydim-1);
            if (videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarx(2 + 1), sbary(200 - 21 + 1), sbarsc(49152L), 0, COLA, 0, 4, 10 + 16 + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarx(2), sbary(200 - 21), sbarsc(49152L), 0, COLA, 0, 0, 10 + 16, 0, 0, xdim - 1, ydim - 1);

            if (sprite[p->i].pal == 1 && p->last_extra < 2)
                G_DrawAltDigiNum(40, -(200 - 22), 1, -16, 10 + 16);
            else if (!althud_flashing || p->last_extra > (p->max_player_health >> 2) || (int32_t)totalclock & 32)
            {
                int s = -8;
                if (althud_flashing && p->last_extra > p->max_player_health)
                    s += (sintable[((int32_t)totalclock << 5) & 2047] >> 10);
                G_DrawAltDigiNum(40, -(200 - 22), p->last_extra, s, 10 + 16);
            }

            if (videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarx(62 + 1), sbary(200 - 25 + 1), sbarsc(49152L), 0, SHIELD, 0, 4, 10 + 16 + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarx(62), sbary(200 - 25), sbarsc(49152L), 0, SHIELD, 0, 0, 10 + 16, 0, 0, xdim - 1, ydim - 1);

            G_DrawAltDigiNum(105, -(200 - 22), p->shield_amount, -16, 10 + 16);

            if (videoGetRenderMode() >= 3 && althud_shadows)
            {
                if (ud.got_access & 1) rotatesprite(sbarxr(39 - 1), sbary(200 - 43 + 1), sbarsc(32768), 0, ACCESSCARD, 0, 4, 10 + 16 + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
                if (ud.got_access & 4) rotatesprite(sbarxr(34 - 1), sbary(200 - 41 + 1), sbarsc(32768), 0, ACCESSCARD, 0, 4, 10 + 16 + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
                if (ud.got_access & 2) rotatesprite(sbarxr(29 - 1), sbary(200 - 39 + 1), sbarsc(32768), 0, ACCESSCARD, 0, 4, 10 + 16 + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
            }

            if (ud.got_access & 1) rotatesprite(sbarxr(39), sbary(200 - 43), sbarsc(32768), 0, ACCESSCARD, 0, 0, 10 + 16, 0, 0, xdim - 1, ydim - 1);
            if (ud.got_access & 4) rotatesprite(sbarxr(34), sbary(200 - 41), sbarsc(32768), 0, ACCESSCARD, 0, 23, 10 + 16, 0, 0, xdim - 1, ydim - 1);
            if (ud.got_access & 2) rotatesprite(sbarxr(29), sbary(200 - 39), sbarsc(32768), 0, ACCESSCARD, 0, 21, 10 + 16, 0, 0, xdim - 1, ydim - 1);

            i = 32768;
            if (p->curr_weapon == PISTOL_WEAPON) i = 16384;
            if (videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarxr(57 - 1), sbary(200 - 15 + 1), sbarsc(i), 0, ammo_sprites[p->curr_weapon], 0, 4, 2 + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
            rotatesprite(sbarxr(57), sbary(200 - 15), sbarsc(i), 0, ammo_sprites[p->curr_weapon], 0, 0, 2, 0, 0, xdim - 1, ydim - 1);

            if (p->curr_weapon == HANDREMOTE_WEAPON) i = HANDBOMB_WEAPON;
            else i = p->curr_weapon;
            if (p->curr_weapon != KNEE_WEAPON &&
                (!althud_flashing || (int32_t)totalclock & 32 || p->ammo_amount[i] > (p->max_ammo_amount[i] / 10)))
                G_DrawAltDigiNum(-20, -(200 - 22), p->ammo_amount[i], -16, 10 + 16);

            o = 102;
            permbit = 0;

            if (p->inven_icon)
            {
                switch (p->inven_icon)
                {
                case 1:
                    i = FIRSTAID_ICON;
                    break;
                case 2:
                    i = STEROIDS_ICON;
                    break;
                case 3:
                    i = HOLODUKE_ICON;
                    break;
                case 4:
                    i = JETPACK_ICON;
                    break;
                case 5:
                    i = HEAT_ICON;
                    break;
                case 6:
                    i = AIRTANK_ICON;
                    break;
                case 7:
                    i = BOOT_ICON;
                    break;
                default:
                    i = -1;
                }
                if (i >= 0)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        rotatesprite(sbarx(231 - o + 1), sbary(200 - 21 - 2 + 1), sbarsc(65536L), 0, i, 0, 4, 10 + 16 + permbit + POLYMOSTTRANS2, 0, 0, xdim - 1, ydim - 1);
                    rotatesprite(sbarx(231 - o), sbary(200 - 21 - 2), sbarsc(65536L), 0, i, 0, 0, 10 + 16 + permbit, 0, 0, xdim - 1, ydim - 1);
                }

                if (videoGetRenderMode() >= 3 && althud_shadows)
                    minitext(292 - 30 - o + 1, 190 - 3 + 1, "%", 4, POLYMOSTTRANS + 10 + 16 + permbit + 256);
                minitext(292 - 30 - o, 190 - 3, "%", 6, 10 + 16 + permbit + 256);

                j = 0x80000000;
                switch (p->inven_icon)
                {
                case 1:
                    i = p->firstaid_amount;
                    break;
                case 2:
                    i = ((p->steroids_amount + 3) >> 2);
                    break;
                case 3:
                    i = ((p->holoduke_amount + 15) / 24);
                    j = p->holoduke_on;
                    break;
                case 4:
                    i = ((p->jetpack_amount + 15) >> 4);
                    j = p->jetpack_on;
                    break;
                case 5:
                    i = p->heat_amount / 12;
                    j = p->heat_on;
                    break;
                case 6:
                    i = ((p->scuba_amount + 63) >> 6);
                    break;
                case 7:
                    i = (p->boot_amount >> 1);
                    break;
                }
                G_DrawInvNum(-(284 - 30 - o), 200 - 6 - 3, (char)i, 0, 10 + permbit);
                if (j > 0)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        minitext(288 - 30 - o + 1, 180 - 3 + 1, "ON", 4, POLYMOSTTRANS + 10 + 16 + permbit + 256);
                    minitext(288 - 30 - o, 180 - 3, "ON", 0, 10 + 16 + permbit + 256);
                }
                else if ((unsigned int)j != 0x80000000)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        minitext(284 - 30 - o + 1, 180 - 3 + 1, "OFF", 4, POLYMOSTTRANS + 10 + 16 + permbit + 256);
                    minitext(284 - 30 - o, 180 - 3, "OFF", 2, 10 + 16 + permbit + 256);
                }
                if (p->inven_icon >= 6)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        minitext(284 - 35 - o + 1, 180 - 3 + 1, "AUTO", 4, POLYMOSTTRANS + 10 + 16 + permbit + 256);
                    minitext(284 - 35 - o, 180 - 3, "AUTO", 2, 10 + 16 + permbit + 256);
                }

            }
            return;
        }
        rotatesprite(sbarx(5), sbary(200 - 28), sbarsc(65536L), 0, HEALTHBOX, 0, 21, 10 + 16, 0, 0, xdim - 1, ydim - 1);
        if (p->inven_icon)
            rotatesprite(sbarx(69), sbary(200 - 30), sbarsc(65536L), 0, INVENTORYBOX, 0, 21, 10 + 16, 0, 0, xdim - 1, ydim - 1);

        if (sprite[p->i].pal == 1 && p->last_extra < 2) // frozen
            G_DrawDigiNum(20, 200 - 17, 1, -16, 10 + 16);
        else G_DrawDigiNum(20, 200 - 17, p->last_extra, -16, 10 + 16);

        rotatesprite(sbarx(37), sbary(200 - 28), sbarsc(65536L), 0, AMMOBOX, 0, 21, 10 + 16, 0, 0, xdim - 1, ydim - 1);

        if (p->curr_weapon == HANDREMOTE_WEAPON) i = HANDBOMB_WEAPON;
        else i = p->curr_weapon;
        G_DrawDigiNum(53, 200 - 17, p->ammo_amount[i], -16, 10 + 16);

        o = 158;
        permbit = 0;
        if (p->inven_icon)
        {
            switch (p->inven_icon)
            {
            case 1:
                i = FIRSTAID_ICON;
                break;
            case 2:
                i = STEROIDS_ICON;
                break;
            case 3:
                i = HOLODUKE_ICON;
                break;
            case 4:
                i = JETPACK_ICON;
                break;
            case 5:
                i = HEAT_ICON;
                break;
            case 6:
                i = AIRTANK_ICON;
                break;
            case 7:
                i = BOOT_ICON;
                break;
            default:
                i = -1;
            }
            if (i >= 0) rotatesprite(sbarx(231 - o), sbary(200 - 21), sbarsc(65536L), 0, i, 0, 0, 10 + 16 + permbit, 0, 0, xdim - 1, ydim - 1);

            minitext(292 - 30 - o, 190, "%", 6, 10 + 16 + permbit + 256);

            j = 0x80000000;
            switch (p->inven_icon)
            {
            case 1:
                i = p->firstaid_amount;
                break;
            case 2:
                i = ((p->steroids_amount + 3) >> 2);
                break;
            case 3:
                i = ((p->holoduke_amount + 15) / 24);
                j = p->holoduke_on;
                break;
            case 4:
                i = ((p->jetpack_amount + 15) >> 4);
                j = p->jetpack_on;
                break;
            case 5:
                i = p->heat_amount / 12;
                j = p->heat_on;
                break;
            case 6:
                i = ((p->scuba_amount + 63) >> 6);
                break;
            case 7:
                i = (p->boot_amount >> 1);
                break;
            }
            G_DrawInvNum(284 - 30 - o, 200 - 6, (char)i, 0, 10 + permbit);
            if (j > 0) minitext(288 - 30 - o, 180, "ON", 0, 10 + 16 + permbit + 256);
            else if ((unsigned int)j != 0x80000000) minitext(284 - 30 - o, 180, "OFF", 2, 10 + 16 + permbit + 256);
            if (p->inven_icon >= 6) minitext(284 - 35 - o, 180, "AUTO", 2, 10 + 16 + permbit + 256);
        }
        return;
    }

    //DRAW/UPDATE FULL STATUS BAR:

    if (pus)
    {
        pus = 0;
        u = -1;
    }
    else u = 0;

    if (sbar.frag[myconnectindex] != p->frag)
    {
        sbar.frag[myconnectindex] = p->frag;
        u |= 32768;
    }
    if (sbar.got_access != ud.got_access)
    {
        sbar.got_access = ud.got_access;
        u |= 16384;
    }

    if (sbar.last_extra != p->last_extra)
    {
        sbar.last_extra = p->last_extra;
        u |= 1;
    }

    {
        int lAmount = Gv_GetVarByLabel("PLR_MORALE", -1, p->i, snum);
        if (lAmount == -1)
        {
            if (sbar.shield_amount != p->shield_amount)
            {
                sbar.shield_amount = p->shield_amount;
                u |= 2;
            }

        }
        else
        {
            if (sbar.shield_amount != lAmount)
            {
                sbar.shield_amount = lAmount;
                u |= 2;
            }

        }
    }

    if (sbar.curr_weapon != p->curr_weapon)
    {
        sbar.curr_weapon = p->curr_weapon;
        u |= (4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 1024 + 65536L);
    }

    for (i = 1; i < MAX_WEAPONS; i++)
    {
        if (sbar.ammo_amount[i] != p->ammo_amount[i])
        {
            sbar.ammo_amount[i] = p->ammo_amount[i];
            if (i < 9)
                u |= ((2 << i) + 1024);
            else u |= 65536L + 1024;
        }

        if (sbar.gotweapon[i] != p->gotweapon[i])
        {
            sbar.gotweapon[i] = p->gotweapon[i];
            if (i < 9)
                u |= ((2 << i) + 1024);
            else u |= 65536L + 1024;
        }
    }
    if (sbar.inven_icon != p->inven_icon)
    {
        sbar.inven_icon = p->inven_icon;
        u |= (2048 + 4096 + 8192);
    }
    if (sbar.holoduke_on != p->holoduke_on)
    {
        sbar.holoduke_on = p->holoduke_on;
        u |= (4096 + 8192);
    }
    if (sbar.jetpack_on != p->jetpack_on)
    {
        sbar.jetpack_on = p->jetpack_on;
        u |= (4096 + 8192);
    }
    if (sbar.heat_on != p->heat_on)
    {
        sbar.heat_on = p->heat_on;
        u |= (4096 + 8192);
    }
    if (sbar.firstaid_amount != p->firstaid_amount)
    {
        sbar.firstaid_amount = p->firstaid_amount;
        u |= 8192;
    }
    if (sbar.steroids_amount != p->steroids_amount)
    {
        sbar.steroids_amount = p->steroids_amount;
        u |= 8192;
    }
    if (sbar.holoduke_amount != p->holoduke_amount)
    {
        sbar.holoduke_amount = p->holoduke_amount;
        u |= 8192;
    }
    if (sbar.jetpack_amount != p->jetpack_amount)
    {
        sbar.jetpack_amount = p->jetpack_amount;
        u |= 8192;
    }
    if (sbar.heat_amount != p->heat_amount)
    {
        sbar.heat_amount = p->heat_amount;
        u |= 8192;
    }
    if (sbar.scuba_amount != p->scuba_amount)
    {
        sbar.scuba_amount = p->scuba_amount;
        u |= 8192;
    }
    if (sbar.boot_amount != p->boot_amount)
    {
        sbar.boot_amount = p->boot_amount;
        u |= 8192;
    }
    if (u == 0) return;

    //0 - update health
    //1 - update armor
    //2 - update PISTOL_WEAPON ammo
    //3 - update SHOTGUN_WEAPON ammo
    //4 - update CHAINGUN_WEAPON ammo
    //5 - update RPG_WEAPON ammo
    //6 - update HANDBOMB_WEAPON ammo
    //7 - update SHRINKER_WEAPON ammo
    //8 - update DEVISTATOR_WEAPON ammo
    //9 - update TRIPBOMB_WEAPON ammo
    //10 - update ammo display
    //11 - update inventory icon
    //12 - update inventory on/off
    //13 - update inventory %
    //14 - update keys
    //15 - update kills
    //16 - update FREEZE_WEAPON ammo

    if (u == -1)
    {
        G_PatchStatusBar(0, 0, 320, 200);

        if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
            rotatesprite(sbarx(277 + 1), sbary(SBY + 7 - 1), sbarsc(65536L), 0, KILLSICON, 0, 0, 10 + 16, 0, 0, xdim - 1, ydim - 1);
    }

    if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
    {
        if (u & 32768)
        {
            if (u != -1)
                G_PatchStatusBar(276, SBY + 17, 299, SBY + 17 + 10);

            G_DrawDigiNum(287, SBY + 17, max(p->frag - p->fraggedself, 0), -16, 10 + 16);
        }
    }
    else
    {
        if (u & 16384)
        {
            if (u != -1) G_PatchStatusBar(275, SBY + 18, 299, SBY + 18 + 12);
            if (ud.got_access & 4) rotatesprite(sbarx(275), sbary(SBY + 16), sbarsc(65536L), 0, ACCESS_ICON, 0, 23, 10 + 16, 0, 0, xdim - 1, ydim - 1);
            if (ud.got_access & 2) rotatesprite(sbarx(288), sbary(SBY + 16), sbarsc(65536L), 0, ACCESS_ICON, 0, 21, 10 + 16, 0, 0, xdim - 1, ydim - 1);
            if (ud.got_access & 1) rotatesprite(sbarx(281), sbary(SBY + 23), sbarsc(65536L), 0, ACCESS_ICON, 0, 0, 10 + 16, 0, 0, xdim - 1, ydim - 1);
        }
    }
    if (u & (4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 65536L)) G_DrawWeapAmounts(p, 96, SBY + 16, u);

    if (u & 1)
    {
        if (u != -1) G_PatchStatusBar(20, SBY + 17, 43, SBY + 17 + 11);
        if (sprite[p->i].pal == 1 && p->last_extra < 2)
            G_DrawDigiNum(32, SBY + 17, 1, -16, 10 + 16);
        else G_DrawDigiNum(32, SBY + 17, p->last_extra, -16, 10 + 16);
    }
    if (u & 2)
    {
        int lAmount = Gv_GetVarByLabel("PLR_MORALE", -1, p->i, snum);
        if (u != -1) G_PatchStatusBar(52, SBY + 17, 75, SBY + 17 + 11);
        if (lAmount == -1)
            G_DrawDigiNum(64, SBY + 17, p->shield_amount, -16, 10 + 16);
        else
            G_DrawDigiNum(64, SBY + 17, lAmount, -16, 10 + 16);
    }

    if (u & 1024)
    {
        if (u != -1) G_PatchStatusBar(196, SBY + 17, 219, SBY + 17 + 11);
        if (p->curr_weapon != KNEE_WEAPON)
        {
            if (p->curr_weapon == HANDREMOTE_WEAPON) i = HANDBOMB_WEAPON;
            else i = p->curr_weapon;
            G_DrawDigiNum(230 - 22, SBY + 17, p->ammo_amount[i], -16, 10 + 16);
        }
    }

    if (u & (2048 + 4096 + 8192))
    {
        if (u != -1)
        {
            if (u & (2048 + 4096))
            {
                G_PatchStatusBar(231, SBY + 13, 265, SBY + 13 + 18);
            }
            else
            {
                G_PatchStatusBar(250, SBY + 24, 261, SBY + 24 + 6);
            }

        }
        if (p->inven_icon)
        {
            o = 0;
            //            permbit = 128;

            if (u & (2048 + 4096))
            {
                switch (p->inven_icon)
                {
                case 1:
                    i = FIRSTAID_ICON;
                    break;
                case 2:
                    i = STEROIDS_ICON;
                    break;
                case 3:
                    i = HOLODUKE_ICON;
                    break;
                case 4:
                    i = JETPACK_ICON;
                    break;
                case 5:
                    i = HEAT_ICON;
                    break;
                case 6:
                    i = AIRTANK_ICON;
                    break;
                case 7:
                    i = BOOT_ICON;
                    break;
                }
                rotatesprite(sbarx(231 - o), sbary(SBY + 13), sbarsc(65536L), 0, i, 0, 0, 10 + 16 + permbit, 0, 0, xdim - 1, ydim - 1);
                minitext(292 - 30 - o, SBY + 24, "%", 6, 10 + 16 + permbit + 256);
                if (p->inven_icon >= 6) minitext(284 - 35 - o, SBY + 14, "AUTO", 2, 10 + 16 + permbit + 256);
            }
            if (u & (2048 + 4096))
            {
                switch (p->inven_icon)
                {
                case 3:
                    j = p->holoduke_on;
                    break;
                case 4:
                    j = p->jetpack_on;
                    break;
                case 5:
                    j = p->heat_on;
                    break;
                default:
                    j = 0x80000000;
                }
                if (j > 0) minitext(288 - 30 - o, SBY + 14, "ON", 0, 10 + 16 + permbit + 256);
                else if ((unsigned int)j != 0x80000000) minitext(284 - 30 - o, SBY + 14, "OFF", 2, 10 + 16 + permbit + 256);
            }
            if (u & 8192)
            {
                switch (p->inven_icon)
                {
                case 1:
                    i = p->firstaid_amount;
                    break;
                case 2:
                    i = ((p->steroids_amount + 3) >> 2);
                    break;
                case 3:
                    i = ((p->holoduke_amount + 15) / 24);
                    break;
                case 4:
                    i = ((p->jetpack_amount + 15) >> 4);
                    break;
                case 5:
                    i = p->heat_amount / 12;
                    break;
                case 6:
                    i = ((p->scuba_amount + 63) >> 6);
                    break;
                case 7:
                    i = (p->boot_amount >> 1);
                    break;
                }
                G_DrawInvNum(284 - 30 - o, SBY + 28, (char)i, 0, 10 + permbit);
            }
        }
    }
}

// Crosshair
void G_GetCrosshairColor(void)
{
    if (DefaultCrosshairColors.f)
        return;

    tileLoad(CROSSHAIR);

    if (!waloff[CROSSHAIR])
        return;

    char const* ptr = (char const*)waloff[CROSSHAIR];

    // find the brightest color in the original 8-bit tile
    int32_t ii = tilesiz[CROSSHAIR].x * tilesiz[CROSSHAIR].y;
    int32_t bri = 0, j = 0, i;

    Bassert(ii > 0);

    do
    {
        if (*ptr != 255)
        {
            i = curpalette[(int32_t)*ptr].r + curpalette[(int32_t)*ptr].g + curpalette[(int32_t)*ptr].b;
            if (i > j) { j = i; bri = *ptr; }
        }
        ptr++;
    } while (--ii);

    Bmemcpy(&CrosshairColors, &curpalette[bri], sizeof(palette_t));
    Bmemcpy(&DefaultCrosshairColors, &curpalette[bri], sizeof(palette_t));
    DefaultCrosshairColors.f = 1; // this flag signifies that the color has been detected
}

void G_SetCrosshairColor(int32_t r, int32_t g, int32_t b)
{
    if (g_crosshairSum == r + (g << 8) + (b << 16))
        return;

    tileLoad(CROSSHAIR);

    if (!waloff[CROSSHAIR])
        return;

    if (!DefaultCrosshairColors.f)
        G_GetCrosshairColor();

    g_crosshairSum = r + (g << 8) + (b << 16);
    CrosshairColors.r = r;
    CrosshairColors.g = g;
    CrosshairColors.b = b;

    char* ptr = (char*)waloff[CROSSHAIR];

    int32_t ii = tilesiz[CROSSHAIR].x * tilesiz[CROSSHAIR].y;

    Bassert(ii > 0);

    int32_t i = (videoGetRenderMode() == REND_CLASSIC)
        ? paletteGetClosestColor(CrosshairColors.r, CrosshairColors.g, CrosshairColors.b)
        : paletteGetClosestColor(255, 255, 255);  // use white in GL so we can tint it to the right color

    do
    {
        if (*ptr != 255)
            *ptr = i;
        ptr++;
    } while (--ii);

    paletteMakeLookupTable(CROSSHAIR_PAL, NULL, CrosshairColors.r, CrosshairColors.g, CrosshairColors.b, 1);

#ifdef USE_OPENGL
    // XXX: this makes us also load all hightile textures tinted with the crosshair color!
    polytint_t& crosshairtint = hictinting[CROSSHAIR_PAL];
    crosshairtint.r = CrosshairColors.r;
    crosshairtint.g = CrosshairColors.g;
    crosshairtint.b = CrosshairColors.b;
    crosshairtint.f = HICTINT_USEONART | HICTINT_GRAYSCALE;
#endif
    tileInvalidate(CROSSHAIR, -1, -1);
}

void G_DrawCrosshair(void)
{
    auto pPlayer = g_player[screenpeek].ps;
    if (pPlayer->newowner != -1 || ud.overhead_on || !ud.crosshair || ud.camerasprite != -1)
        return;

    Gv_SetVar(g_iReturnVarID, 0, pPlayer->i, screenpeek);
    X_OnEvent(EVENT_DISPLAYCROSSHAIR, g_player[screenpeek].ps->i, screenpeek, -1);

    if (Gv_GetVar(g_iReturnVarID, pPlayer->i, screenpeek) == 0)
        rotatesprite((160L - (pPlayer->look_ang >> 1)) << 16, 100L << 16, scale(65536, ud.crosshairscale, 100), 0, CROSSHAIR, 0, CROSSHAIR_PAL, 2 + 1, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
}

// Player IDs
#define ID_ALPHA 64                     // Starting Alpha
#define ID_ALPHA_RANGE (255-ID_ALPHA)   // Alpha Remainder
#define ID_YPOS 145                     // Y position for id text
#define ID_TIME (TICRATE*2)             // Time for id text
#define ID_FADETIME (TICRATE*1)         // Remaining time to start fading id text
#define KILLID_TIME (TICRATE*3)
#define KILLID_FADETIME (TICRATE*2)

#define WITHIN_TIME(starttime, totaltime) ((starttime - (int32_t)totalclock) > 0 && (starttime - (int32_t)totalclock) <= totaltime)

static int32_t GetDistance2D(int x1, int y1, int x2, int y2)
{
    int32_t x = klabs(x1 - x2);
    int32_t y = klabs(y1 - y2);

    if (x < y) swaplong(&x, &y);

    {
        int32_t t = y + (y >> 1);
        return (x - (x >> 5) - (x >> 7) + (t >> 2) + (t >> 6));
    }
}

void G_DisplayPlayerId(void)
{
    int idalpha;

    if (g_player[myconnectindex].ps->gm & MODE_MENU)
        return;

    if (ud.idplayers && (ud.multimode > 1))
    {
        // TODO: (Optimization) Consider moving this segment to a point in the code that operates at 120hz.
        int i;

        hitdata_t hit;

        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].ps->holoduke_on != -1)
                sprite[g_player[i].ps->holoduke_on].cstat ^= 256;
        }

        hitscan(&ud.camerapos, ud.camerasect,
            sintable[(fix16_to_int(ud.cameraq16ang) + 512) & 2047],
            sintable[fix16_to_int(ud.cameraq16ang) & 2047],
            fix16_to_int(F16(100) - ud.cameraq16horiz) << 11, &hit, 0xffff0030);

        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].ps->holoduke_on != -1)
                sprite[g_player[i].ps->holoduke_on].cstat ^= 256;
        }

        if ((hit.sprite >= 0) && sprite[hit.sprite].picnum == APLAYER && sprite[hit.sprite].yvel != screenpeek && g_player[sprite[hit.sprite].yvel].ps->dead_flag == 0)
        {
            if (GetDistance2D(ud.camerapos.x, ud.camerapos.y, sprite[hit.sprite].x, sprite[hit.sprite].y) < 9216)
            {
                ud.playerid_num = sprite[hit.sprite].yvel;
                ud.playerid_time = (int32_t)totalclock + ID_TIME;
            }
        }
        // End segment

        if (WITHIN_TIME(ud.playerid_time, ID_TIME))
        {
            idalpha = ID_ALPHA;
            if (WITHIN_TIME(ud.playerid_time, ID_FADETIME))
            {
                idalpha += ID_ALPHA_RANGE - scale(ud.playerid_time - (int32_t)totalclock, ID_ALPHA_RANGE, ID_FADETIME);
            }

            G_ScreenText(STARTALPHANUM, 160, ID_YPOS, 65536, 0, 0, g_player[ud.playerid_num].user_name, -127, g_player[ud.playerid_num].ps->palookup, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);

            if ((!GTFLAGS(GAMETYPE_COOP) && !GTFLAGS(GAMETYPE_PLAYERSFRIENDLY)) && ((GTFLAGS(GAMETYPE_TDM) && (g_player[ud.playerid_num].pteam != g_player[screenpeek].pteam)) || !GTFLAGS(GAMETYPE_TDM)))
            {
                G_ScreenText(MINIFONT, 160, ID_YPOS + 8, 65536, 0, 0, "Enemy", -127, 2, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
            }
            else if ((GTFLAGS(GAMETYPE_COOP) || GTFLAGS(GAMETYPE_PLAYERSFRIENDLY)) || (GTFLAGS(GAMETYPE_TDM) && (g_player[ud.playerid_num].pteam == g_player[screenpeek].pteam)))
            {
                G_ScreenText(MINIFONT, 160, ID_YPOS + 8, 65536, 0, 0, "Friendly", -127, 6, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);

                Bsprintf(tempbuf, "^12Health: ^02%d ^07/ ^12Armor: ^06%d", TrackerCast(sprite[g_player[ud.playerid_num].ps->i].extra), g_player[ud.playerid_num].ps->shield_amount);
                G_ScreenText(MINIFONT, 160, ID_YPOS + 14, 65536, 0, 0, tempbuf, -127, 12, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
            }
        }
    }

    if (ud.multimode > 1)
    {
        if (WITHIN_TIME(g_player[screenpeek].fragged_time, KILLID_TIME))
        {
            idalpha = ID_ALPHA;
            if (WITHIN_TIME(g_player[screenpeek].fragged_time, KILLID_FADETIME))
            {
                idalpha += ID_ALPHA_RANGE - scale(g_player[screenpeek].fragged_time - (int32_t)totalclock, ID_ALPHA_RANGE, KILLID_FADETIME);
            }

            // HACK: Modulo palookup to keep under 3 digits. ScreenText really needs a 3-digit form of color code.
            Bsprintf(tempbuf, "KILLED ^%02d%s", g_player[g_player[screenpeek].fragged_id].ps->palookup % 100, g_player[g_player[screenpeek].fragged_id].user_name);
            G_ScreenText(STARTALPHANUM, 160, ID_YPOS - 9, 65536, 0, 0, tempbuf, -127, 0, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }

        if (WITHIN_TIME(g_player[screenpeek].fraggedby_time, KILLID_TIME))
        {
            idalpha = ID_ALPHA;
            if (WITHIN_TIME(g_player[screenpeek].fraggedby_time, KILLID_FADETIME))
            {
                idalpha += ID_ALPHA_RANGE - scale(g_player[screenpeek].fraggedby_time - (int32_t)totalclock, ID_ALPHA_RANGE, KILLID_FADETIME);
            }

            // HACK: Modulo palookup to keep under 3 digits. ScreenText really needs a 3-digit form of color code.
            Bsprintf(tempbuf, "KILLED BY ^%02d%s", g_player[g_player[screenpeek].fraggedby_id].ps->palookup % 100, g_player[g_player[screenpeek].fraggedby_id].user_name);
            G_ScreenText(STARTALPHANUM, 160, ID_YPOS - 18, 65536, 0, 0, tempbuf, -127, 0, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }

        if (ud.limit_hit == 1) // Someone won!
        {
            int pal = 0;
            rotatesprite_(0, 0, 65536L, 0, BETASCREEN, 0, 4, RS_AUTO | RS_TOPLEFT, 128, 0, get_xdim(160 - 120), get_ydim(ID_YPOS - 37), get_xdim(160 + 120), get_ydim(ID_YPOS - 28));

            if (GTFLAGS(GAMETYPE_TDM))
            {
                pal = teams[ud.winner].palnum;
                Bsprintf(tempbuf, "%s Team ^8Wins!", teams[ud.winner].name);
            }
            else
            {
                pal = g_player[ud.winner].ps->palookup;
                Bsprintf(tempbuf, "%s ^8Wins!", g_player[ud.winner].user_name);
            }

            G_ScreenText(STARTALPHANUM, 160, ID_YPOS - 36, 65536, 0, 0, tempbuf, -127, pal, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, 0, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }

        if (screenpeek != myconnectindex)
        {
            G_ScreenText(MINIFONT, 160, 16, 65536, 0, 0, "Following", -127, 6, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, 64, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
            G_ScreenText(STARTALPHANUM, 160, 16 + 7, 65536, 0, 0, g_player[screenpeek].user_name, -127, g_player[screenpeek].ps->palookup, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, 64, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }
    }
}

void G_AddKillToFeed(int victim, int attacker)
{
    g_player[victim].fraggedby_id = attacker;
    g_player[victim].fraggedby_time = g_player[attacker].fragged_time = (int32_t)totalclock + KILLID_TIME;
    g_player[attacker].fragged_id = victim;
}

// Level Name
void G_PrintLevelName(void)
{
    if (!ud.show_level_text || !hud_showmapname || !g_levelTextTime)
        return;

    int bits = 10 + 16;

    if (g_levelTextTime > 4)
        bits = bits;
    else if (g_levelTextTime > 2)
        bits |= 1;
    else bits |= 1 + 32;

    if (g_mapInfo[(ud.volume_number * MAXLEVELS) + ud.level_number].name != NULL)
    {
        if (currentboardfilename[0] != 0 && ud.volume_number == 0 && ud.level_number == 7)
            menutext_(160, 75, -g_levelTextTime + 22, 0, currentboardfilename, bits);
        else
            menutext_(160, 75, -g_levelTextTime + 22, 0, g_mapInfo[(ud.volume_number * MAXLEVELS) + ud.level_number].name, bits);
    }
}