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
#include "osd.h"

extern int everyothertime;
static int g_lastPlayerPal = 0;
int g_numRealPalettes;
short SpriteCacheList[MAXTILES][3];

static char precachehightile[2][MAXTILES>>3];
static int  g_precacheCount;

//extern const char *duke3dgrpstring;
extern int g_levelTextTime;

void G_SetupGlobalPsky(void);

static void flag_precache(int32_t tile, int32_t type)
{
    if (!(gotpic[tile >> 3] & pow2char[tile & 7]))
        g_precacheCount++;

    gotpic[tile >> 3] |= pow2char[tile & 7];
    precachehightile[type][tile >> 3] |= pow2char[tile & 7];
}

static void tloadtile(int tilenume, int type)
{
    int firstTile, lastTile;

    if ((picanm[tilenume].sf & PICANM_ANIMTYPE_MASK) == PICANM_ANIMTYPE_BACK)
    {
        firstTile = tilenume - picanm[tilenume].num;
        lastTile = tilenume;
    }
    else
    {
        firstTile = tilenume;
        lastTile = tilenume + picanm[tilenume].num;
    }

    for (; firstTile <= lastTile; firstTile++)
        flag_precache(firstTile, type);
}

static void G_CacheSpriteNum(int i)
{
    char maxc;
    int j;

    if (ud.monsters_off && A_CheckEnemySprite(&sprite[i])) return;

    maxc = 1;

    if (SpriteCacheList[PN(i)][0] == PN(i))
        for (j = PN(i); j <= SpriteCacheList[PN(i)][1]; j++)
            tloadtile(j,1);

    switch (DynamicTileMap[PN(i)])
    {
    case HYDRENT__STATIC:
        tloadtile(BROKEFIREHYDRENT,1);
        for (j = TOILETWATER; j < (TOILETWATER+4); j++) tloadtile(j,1);
        break;
    case TOILET__STATIC:
        tloadtile(TOILETBROKE,1);
        for (j = TOILETWATER; j < (TOILETWATER+4); j++) tloadtile(j,1);
        break;
    case STALL__STATIC:
        tloadtile(STALLBROKE,1);
        for (j = TOILETWATER; j < (TOILETWATER+4); j++) tloadtile(j,1);
        break;
    case RUBBERCAN__STATIC:
        maxc = 2;
        break;
    case TOILETWATER__STATIC:
        maxc = 4;
        break;
    case FEMPIC1__STATIC:
        maxc = 44;
        break;
    case LIZTROOP__STATIC:
    case LIZTROOPRUNNING__STATIC:
    case LIZTROOPSHOOT__STATIC:
    case LIZTROOPJETPACK__STATIC:
    case LIZTROOPONTOILET__STATIC:
    case LIZTROOPDUCKING__STATIC:
        for (j = LIZTROOP; j < (LIZTROOP+72); j++) tloadtile(j,1);
        for (j=HEADJIB1;j<LEGJIB1+3;j++) tloadtile(j,1);
        maxc = 0;
        break;
    case WOODENHORSE__STATIC:
        maxc = 5;
        for (j = HORSEONSIDE; j < (HORSEONSIDE+4); j++) tloadtile(j,1);
        break;
    case NEWBEAST__STATIC:
    case NEWBEASTSTAYPUT__STATIC:
        maxc = 90;
        break;
    case BOSS1__STATIC:
    case BOSS2__STATIC:
    case BOSS3__STATIC:
        maxc = 30;
        break;
    case OCTABRAIN__STATIC:
    case OCTABRAINSTAYPUT__STATIC:
    case COMMANDER__STATIC:
    case COMMANDERSTAYPUT__STATIC:
        maxc = 38;
        break;
    case RECON__STATIC:
        maxc = 13;
        break;
    case PIGCOP__STATIC:
    case PIGCOPDIVE__STATIC:
        maxc = 61;
        break;
    case SHARK__STATIC:
        maxc = 30;
        break;
    case LIZMAN__STATIC:
    case LIZMANSPITTING__STATIC:
    case LIZMANFEEDING__STATIC:
    case LIZMANJUMP__STATIC:
        for (j=LIZMANHEAD1;j<LIZMANLEG1+3;j++) tloadtile(j,1);
        maxc = 80;
        break;
    case APLAYER__STATIC:
        maxc = 0;
        if (ud.multimode > 1)
        {
            maxc = 5;
            for (j = 1420;j < 1420+106; j++) tloadtile(j,1);
        }
        break;
    case ATOMICHEALTH__STATIC:
        maxc = 14;
        break;
    case DRONE__STATIC:
        maxc = 10;
        break;
    case EXPLODINGBARREL__STATIC:
    case SEENINE__STATIC:
    case OOZFILTER__STATIC:
        maxc = 3;
        break;
    case NUKEBARREL__STATIC:
    case CAMERA1__STATIC:
        maxc = 5;
        break;
        // caching of HUD sprites for weapons that may be in the level
    case CHAINGUNSPRITE__STATIC:
        for (j=CHAINGUN; j<=CHAINGUN+7; j++) tloadtile(j,1);
        break;
    case RPGSPRITE__STATIC:
        for (j=RPGGUN; j<=RPGGUN+2; j++) tloadtile(j,1);
        break;
    case FREEZESPRITE__STATIC:
        for (j=FREEZE; j<=FREEZE+5; j++) tloadtile(j,1);
        break;
    case GROWSPRITEICON__STATIC:
    case SHRINKERSPRITE__STATIC:
        for (j=SHRINKER-2; j<=SHRINKER+5; j++) tloadtile(j,1);
        break;
    case HBOMBAMMO__STATIC:
    case HEAVYHBOMB__STATIC:
        for (j=HANDREMOTE; j<=HANDREMOTE+5; j++) tloadtile(j,1);
        break;
    case TRIPBOMBSPRITE__STATIC:
        for (j=HANDHOLDINGLASER; j<=HANDHOLDINGLASER+4; j++) tloadtile(j,1);
        break;
    case SHOTGUNSPRITE__STATIC:
        tloadtile(SHOTGUNSHELL,1);
        for (j=SHOTGUN; j<=SHOTGUN+6; j++) tloadtile(j,1);
        break;
    case DEVISTATORSPRITE__STATIC:
        for (j=DEVISTATOR; j<=DEVISTATOR+1; j++) tloadtile(j,1);
        break;

    }

    for (j = PN(i); j < (PN(i)+maxc); j++) tloadtile(j,1);
}

static void G_PrecacheSprites(void)
{
    int i,j;

    for (i=0;i<MAXTILES;i++)
    {
        if (SpriteFlags[i] & SPRITE_PROJECTILE)
            tloadtile(i,1);
        if (SpriteCacheList[i][0] == i && SpriteCacheList[i][2])
            for (j = i; j <= SpriteCacheList[i][1]; j++)
                tloadtile(j,1);
    }
    tloadtile(BOTTOMSTATUSBAR,1);
    if (ud.multimode > 1)
        tloadtile(FRAGBAR,1);

    tloadtile(VIEWSCREEN,1);

    for (i=STARTALPHANUM;i<ENDALPHANUM+1;i++) tloadtile(i,1);
    for (i=BIGALPHANUM; i<BIGALPHANUM+82; i++) tloadtile(i,1);
    for (i=MINIFONT;i<MINIFONT+63;i++) tloadtile(i,1);

    for (i=FOOTPRINTS;i<FOOTPRINTS+3;i++) tloadtile(i,1);

    for (i = BURNING; i < BURNING+14; i++) tloadtile(i,1);
    for (i = BURNING2; i < BURNING2+14; i++) tloadtile(i,1);

    for (i = CRACKKNUCKLES; i < CRACKKNUCKLES+4; i++) tloadtile(i,1);

    for (i = FIRSTGUN; i < FIRSTGUN+3 ; i++) tloadtile(i,1);
    for (i = FIRSTGUNRELOAD; i < FIRSTGUNRELOAD+8 ; i++) tloadtile(i,1);

    for (i = EXPLOSION2; i < EXPLOSION2+21 ; i++) tloadtile(i,1);

    for (i = COOLEXPLOSION1; i < COOLEXPLOSION1+21 ; i++) tloadtile(i,1);

    tloadtile(BULLETHOLE,1);
    tloadtile(BLOODPOOL,1);
    for (i = TRANSPORTERBEAM; i < (TRANSPORTERBEAM+6); i++) tloadtile(i,1);

    for (i = SMALLSMOKE; i < (SMALLSMOKE+4); i++) tloadtile(i,1);
    for (i = SHOTSPARK1; i < (SHOTSPARK1+4); i++) tloadtile(i,1);

    for (i = BLOOD; i < (BLOOD+4); i++) tloadtile(i,1);
    for (i = JIBS1; i < (JIBS5+5); i++) tloadtile(i,1);
    for (i = JIBS6; i < (JIBS6+8); i++) tloadtile(i,1);

    for (i = SCRAP1; i < (SCRAP1+29); i++) tloadtile(i,1);

    tloadtile(FIRELASER,1);
    for (i=FORCERIPPLE; i<(FORCERIPPLE+9); i++) tloadtile(i,1);

    for (i=MENUSCREEN; i<DUKECAR; i++) tloadtile(i,1);

    for (i=RPG; i<RPG+7; i++) tloadtile(i,1);
    for (i=FREEZEBLAST; i<FREEZEBLAST+3; i++) tloadtile(i,1);
    for (i=SHRINKSPARK; i<SHRINKSPARK+4; i++) tloadtile(i,1);
    for (i=GROWSPARK; i<GROWSPARK+4; i++) tloadtile(i,1);
    for (i=SHRINKEREXPLOSION; i<SHRINKEREXPLOSION+4; i++) tloadtile(i,1);
    for (i=MORTER; i<MORTER+4; i++) tloadtile(i,4);
    for (i=0; i<=60; i++) tloadtile(i,1);
}

static void G_DemoLoadScreen(const char* statustext, int const loadScreenTile, int percent)
{
    if (statustext == NULL)
    {
        videoClearScreen(0L);
        // g_player[myconnectindex].ps->palette = palette;
        // G_FadePalette(0,0,0,0);
        P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 0);  // JBF 20040308
    }

    if ((unsigned)loadScreenTile < (MAXTILES << 1))
    {
        rotatesprite_fs(320 << 15, 200 << 15, 65536L, 0, loadScreenTile, 0, 0, 2 + 8 + 64);
    }
    else
    {
        videoNextPage();
        return;
    }

    menutext(160, 105, 0, 0, "LOADING...");

    if (statustext)
        gametext(160, 180, statustext, 0, 2 + 8 + 16);

    Gv_SetVar(g_iReturnVarID, percent , -1, -1);
    X_OnEvent(EVENT_DISPLAYLOADINGSCREEN, g_player[screenpeek].ps->i, screenpeek, -1);
    videoNextPage();
}

static void G_DoLoadScreen(const char* statustext, int percent)
{
    Gv_SetVar(g_iReturnVarID, LOADSCREEN, -1, -1);
    X_OnEvent(EVENT_GETLOADTILE, -1, myconnectindex, -1);
    int const loadScreenTile = Gv_GetVar(g_iReturnVarID, -1, -1);

    if (ud.recstat == 2)
    {
        G_DemoLoadScreen(statustext, loadScreenTile, percent);
        return;
    }

    int const screenSize = ud.screen_size;

    P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 1);

    if (statustext == NULL)
    {
        ud.screen_size = 0;
        G_UpdateScreenArea();
        videoClearScreen(0L);
    }

    if ((unsigned)loadScreenTile < (MAXTILES << 1))
    {
        videoClearScreen(0);
        rotatesprite_fs(320 << 15, 200 << 15, 65536L, 0, loadScreenTile, 0, 0, 2 + 8 + 64);
    }
    else
    {
        videoNextPage();
        return;
    }

    if (boardfilename[0] != 0 && ud.level_number == 7 && ud.volume_number == 0)
    {
        menutext(160, 90, 0, 0, "LOADING USER MAP");
        gametextpal(160, 90 + 10, boardfilename, 14, 2);
    }
    else
    {
        menutext(160, 90, 0, 0, "LOADING");

        if (MapInfo[(ud.volume_number * MAXLEVELS) + ud.level_number].name != NULL)
            menutext(160, 90 + 16 + 8, 0, 0, MapInfo[(ud.volume_number * MAXLEVELS) + ud.level_number].name);
    }

#ifndef EDUKE32_TOUCH_DEVICES
    if (statustext)
        gametext(160, 180, statustext, 0, 2 + 8 + 16);
#endif

    if (percent != -1)
    {
        int const     width = scale(scale(xdim - 1, 288, 320), percent, 100);
        int constexpr tile = 929;
        int constexpr bits = 2 + 8 + 16;

        rotatesprite(31 << 16, 145 << 16, 65536, 0, tile, 15, 0, bits, 0, 0, width, ydim - 1);
        rotatesprite(159 << 16, 145 << 16, 65536, 0, tile, 15, 0, bits, 0, 0, width, ydim - 1);

        rotatesprite(30 << 16, 144 << 16, 65536, 0, tile, 0, 0, bits, 0, 0, width, ydim - 1);
        rotatesprite(158 << 16, 144 << 16, 65536, 0, tile, 0, 0, bits, 0, 0, width, ydim - 1);
    }

    Gv_SetVar(g_iReturnVarID, percent, -1, -1);
    X_OnEvent(EVENT_DISPLAYLOADINGSCREEN, g_player[screenpeek].ps->i, screenpeek, -1);
    videoNextPage();

    if (!statustext)
    {
        KB_FlushKeyboardQueue();
        ud.screen_size = screenSize;
    }
}

void G_CacheMapData(void)
{
    int i,j,pc=0;
    int tc;
    unsigned int starttime, endtime;

    if (ud.recstat == 2)
        return;

    S_PauseMusic(1);
    if (MapInfo[MAXVOLUMES*MAXLEVELS+2].alt_musicfn)
    {
        S_StopMusic();
        S_PlayMusic(&EnvMusicFilename[2][0],MAXVOLUMES*MAXLEVELS+2); // loadmus
    }

    starttime = timerGetTicks();

    G_PrecacheSprites();

    for (i=0;i<numwalls;i++)
    {
        tloadtile(wall[i].picnum, 0);

        if (wall[i].overpicnum >= 0)
        {
            tloadtile(wall[i].overpicnum, 0);
        }
    }

    for (i=0;i<numsectors;i++)
    {
        tloadtile(sector[i].floorpicnum, 0);
        tloadtile(sector[i].ceilingpicnum, 0);
        if (sector[i].ceilingpicnum == LA)  // JBF 20040509: if( waloff[sector[i].ceilingpicnum] == LA) WTF??!??!?!?
        {
            tloadtile(LA+1, 0);
            tloadtile(LA+2, 0);
        }

        j = headspritesect[i];
        while (j >= 0)
        {
            if (sprite[j].xrepeat != 0 && sprite[j].yrepeat != 0 && (sprite[j].cstat&32768) == 0)
                G_CacheSpriteNum(j);
            j = nextspritesect[j];
        }
    }

    tc = (int32_t)totalclock;
    j = 0;

    for (i=0;i<MAXTILES;i++)
    {
        if (!(i&7) && !gotpic[i>>3])
        {
            i+=7;
            continue;
        }
        if (gotpic[i>>3] & pow2char[i&7])
        {
            if (waloff[i] == 0)
                tileLoad((short)i);

#if defined(USE_OPENGL)
            if (ud.config.useprecache)
            {
                int k;

                if (precachehightile[0][i>>3] & pow2char[i&7])
                    for (k=0; k<MAXPALOOKUPS && !KB_KeyPressed(sc_Space); k++)
                        polymost_precache(i,k,0);

                if (precachehightile[1][i>>3] & pow2char[i&7])
                    for (k=0; k<MAXPALOOKUPS && !KB_KeyPressed(sc_Space); k++)
                        polymost_precache(i,k,1);
            }
#endif
            j++;
            pc++;
        }
        else continue;

        MUSIC_Update();

        if ((j&7) == 0)
        {
            handleevents();
            Net_GetPackets();
        }
        if (totalclock - tc > TICRATE/4)
        {
            sprintf(tempbuf,"%d resources remaining\n",g_precacheCount-pc+1);
            G_DoLoadScreen(tempbuf, min(100,100*pc/g_precacheCount));
            tc = (int32_t)totalclock;
        }
    }

    clearbufbyte(gotpic,sizeof(gotpic),0L);

    endtime = timerGetTicks();
    OSD_Printf("Cache time: %dms\n", endtime-starttime);
}

void xyzmirror(int i,int wn)
{
    //if (waloff[wn] == 0) tileLoad(wn);
    renderSetTarget(wn,tilesiz[wn].y,tilesiz[wn].x);

    drawrooms(SX(i),SY(i),SZ(i),SA(i),100+sprite[i].shade,SECT(i));
    display_mirror = 1;
    G_DoSpriteAnimations(SX(i),SY(i),SZ(i),SA(i),65536L);
    display_mirror = 0;
    renderDrawMasks();

    initprintf("%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i\n",i,wn,tilesiz[wn].y,tilesiz[wn].x,TrackerCast(SX(i)),TrackerCast(SY(i)),TrackerCast(SZ(i)),TrackerCast(SA(i)),100+TrackerCast(SS(i)),TrackerCast(SECT(i)),TrackerCast(PN(i)));

    renderRestoreTarget();
    squarerotatetile(wn);
    tileInvalidate(wn,-1,255);
}

void G_UpdateScreenArea(void)
{
    int i, j, ss, x1, x2, y1, y2;

    if (ud.screen_size < 0) ud.screen_size = 0;
    if (ud.screen_size > 64) ud.screen_size = 64;
    if (ud.screen_size == 0) renderFlushPerms();

    ss = max(ud.screen_size-8,0);

    x1 = scale(ss,xdim,160);
    x2 = xdim-x1;

    y1 = ss;
    y2 = 200;
    if (ud.screen_size > 0 && (GametypeFlags[ud.coop]&GAMETYPE_FRAGBAR) && ud.multimode > 1)
    {
        j = 0;
        TRAVERSE_CONNECT(i)
        if (i > j) j = i;

        if (j >= 1) y1 += 8;
        if (j >= 4) y1 += 8;
        if (j >= 8) y1 += 8;
        if (j >= 12) y1 += 8;
    }

    if (ud.screen_size >= 8 && !(videoGetRenderMode() >= 3 && ud.screen_size == 8 && ud.statusbarmode))
        y2 -= (ss+scale(tilesiz[BOTTOMSTATUSBAR].y,ud.statusbarscale,100));

    y1 = scale(y1,ydim,200);
    y2 = scale(y2,ydim,200);

    if (videoGetRenderMode() >= 3)
        videoSetViewableArea(x1,y1,x2-1,y2);
    else videoSetViewableArea(x1,y1,x2-1,y2-1);

    G_GetCrosshairColor();
    G_SetCrosshairColor(CrosshairColors.r, CrosshairColors.g, CrosshairColors.b);

    pub = NUMPAGES;
    pus = NUMPAGES;
}

void G_SetupGlobalPsky(void)
{
    int skyIdx = 0;

    // NOTE: Loop must be running backwards for the same behavior as the game
    // (greatest sector index with matching parallaxed sky takes precedence).
    for (int i = numsectors - 1; i >= 0; i--)
    {
        if (sector[i].ceilingstat & 1)
        {
            skyIdx = getpskyidx(sector[i].ceilingpicnum);
            if (skyIdx > 0)
                break;
        }
    }

    g_pskyidx = skyIdx;
}

void P_RandomSpawnPoint(int snum)
{
    DukePlayer_t *p = g_player[snum].ps;
    int i=snum,j,k;
    unsigned int dist,pdist = -1;

    if (ud.multimode > 1 && !(GametypeFlags[ud.coop] & GAMETYPE_FIXEDRESPAWN))
    {
        i = krand()%g_numPlayerSprites;
        if (GametypeFlags[ud.coop] & GAMETYPE_TDMSPAWN)
        {
            for (j=0;j<MAXPLAYERS;j++)
            {
                if (g_player[j].ps == NULL || !g_player[j].connected) continue;

                if (j != snum && g_player[j].ps->team == p->team && sprite[g_player[j].ps->i].extra > 0)
                {
                    for (k=0;k<g_numPlayerSprites;k++)
                    {
                        dist = FindDistance2D(g_player[j].ps->pos.x-g_playerSpawnPoints[k].ox,g_player[j].ps->pos.y-g_playerSpawnPoints[k].oy);
                        if (dist < pdist)
                            i = k, pdist = dist;
                    }
                    break;
                }
            }
        }
    }

    p->bobposx = p->opos.x = p->pos.x = g_playerSpawnPoints[i].ox;
    p->bobposy = p->opos.y = p->pos.y = g_playerSpawnPoints[i].oy;
    p->opos.z = p->pos.z = g_playerSpawnPoints[i].oz;
    p->q16ang = fix16_from_int(g_playerSpawnPoints[i].oa);
    p->cursectnum = g_playerSpawnPoints[i].os;
    // [JM] Need to prevent perpetual gibbing after being killed by a space sector.
    changespritesect(p->i, p->cursectnum);
}

static void P_ResetStatus(int snum)
{
    DukePlayer_t *p = g_player[snum].ps;

    ud.show_help        = 0;
    ud.showallmap       = 0;
    p->dead_flag        = 0;
    p->wackedbyactor    = -1;
    p->falling_counter  = 0;
    p->quick_kick       = 0;
    p->subweapon        = 0;
    p->last_full_weapon = 0;
    p->ftq              = 0;
    p->fta              = 0;
    p->tipincs          = 0;
    p->buttonpalette    = 0;
    p->actorsqu         =-1;
    p->invdisptime      = 0;
    p->refresh_inventory= 0;
    p->last_pissed_time = 0;
    p->holster_weapon   = 0;
    p->pycount          = 0;
    p->pyoff            = 0;
    p->opyoff           = 0;
    p->loogcnt          = 0;
    p->q16angvel           = 0;
    p->weapon_sway      = 0;
    //    p->select_dir       = 0;
    p->extra_extra8     = 0;
    p->show_empty_weapon= 0;
    p->dummyplayersprite=-1;
    p->crack_time       = 0;
    p->hbomb_hold_delay = 0;
    p->transporter_hold = 0;
    p->wantweaponfire  = -1;
    p->hurt_delay       = 0;
    p->footprintcount   = 0;
    p->footprintpal     = 0;
    p->footprintshade   = 0;
    p->jumping_toggle   = 0;
    p->oq16horiz = p->q16horiz = F16(140);
    p->q16horizoff      = 0;
    p->bobcounter       = 0;
    p->on_ground        = 0;
    p->player_par       = 0;
    p->return_to_center = 9;
    p->airleft          = 15*26;
    p->rapid_fire_hold  = 0;
    p->toggle_key_flag  = 0;
    p->access_spritenum = -1;
    if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_ACCESSATSTART))
        ud.got_access = 7;
    else ud.got_access      = 0;
    p->random_club_frame= 0;
    pus = 1;
    p->on_warping_sector = 0;
    p->spritebridge      = 0;
    p->sbs          = 0;
    p->palette = BASEPAL;

    if (p->steroids_amount < 400)
    {
        p->steroids_amount = 0;
        p->inven_icon = 0;
    }
    p->heat_on =            0;
    p->jetpack_on =         0;
    p->holoduke_on =       -1;

    p->look_ang          = 512 - ((ud.level_number&1)<<10);

    p->rotscrnang        = 0;
    p->orotscrnang       = 1;   // JBF 20031220
    p->newowner          =-1;
    p->jumping_counter   = 0;
    p->hard_landing      = 0;
    p->vel.x             = 0;
    p->vel.y             = 0;
    p->vel.z             = 0;
    fricxv            = 0;
    fricyv            = 0;
    p->somethingonplayer =-1;
    p->one_eighty_count  = 0;
    p->cheat_phase       = 0;

    p->on_crane          = -1;

    if ((aplWeaponWorksLike[p->curr_weapon][snum] == PISTOL_WEAPON) &&
            (aplWeaponReload[p->curr_weapon][snum] > aplWeaponTotalTime[p->curr_weapon][snum]))
        p->kickback_pic  = aplWeaponTotalTime[p->curr_weapon][snum]+1;
    else p->kickback_pic = 0;

    p->weapon_pos        = 6;
    p->walking_snd_toggle= 0;
    p->weapon_ang        = 0;

    p->knuckle_incs      = 1;
    p->fist_incs = 0;
    p->knee_incs         = 0;
    p->jetpack_on        = 0;
    p->reloading        = 0;

    p->movement_lock     = 0;

    P_UpdateScreenPal(p);
    X_OnEvent(EVENT_RESETPLAYER, p->i, snum, -1);
}

void P_ResetWeapons(int snum)
{
    int weapon;
    DukePlayer_t *p = g_player[snum].ps;

    for (weapon = PISTOL_WEAPON; weapon < MAX_WEAPONS; weapon++)
        p->gotweapon[weapon] = 0;
    for (weapon = PISTOL_WEAPON; weapon < MAX_WEAPONS; weapon++)
        p->ammo_amount[weapon] = 0;

    p->weapon_pos = 6;
    p->kickback_pic = 5;
    p->curr_weapon = PISTOL_WEAPON;
    p->gotweapon[PISTOL_WEAPON] = 1;
    p->gotweapon[KNEE_WEAPON] = 1;
    p->ammo_amount[PISTOL_WEAPON] = 48;
    p->gotweapon[HANDREMOTE_WEAPON] = 1;
    p->last_weapon = -1;
    p->last_used_weapon = -1;

    p->show_empty_weapon= 0;
    p->last_pissed_time = 0;
    p->holster_weapon = 0;

    Gv_SetVar(g_iWeaponVarID, p->curr_weapon, p->i, snum);
    X_OnEvent(EVENT_RESETWEAPONS, p->i, snum, -1);
}

void P_ResetInventory(int snum)
{
    DukePlayer_t *p = g_player[snum].ps;

    p->inven_icon       = 0;
    p->boot_amount = 0;
    p->scuba_on =           0;
    p->scuba_amount =         0;
    p->heat_amount        = 0;
    p->heat_on = 0;
    p->jetpack_on =         0;
    p->jetpack_amount =       0;
    p->shield_amount =      g_startArmorAmount;
    p->holoduke_on = -1;
    p->holoduke_amount =    0;
    p->firstaid_amount = 0;
    p->steroids_amount = 0;
    p->inven_icon = 0;
    X_OnEvent(EVENT_RESETINVENTORY, p->i, snum, -1);
}

static void resetpremapglobals(void)
{
    int i;

    screenpeek = myconnectindex;
    parallaxtype = 0;
    randomseed = 1996;
    tempwallptr = 0;
    camsprite = -1;

    ud.limit_hit = 0;
    ud.pause_on = 0;
    ud.camerasprite = -1;
    ud.eog = 0;
    ud.playerid_time = 0;

    g_numAnimWalls = 0;
    g_numCyclers = 0;
    g_animateCount = 0;
    g_earthquakeTime = 0;
    g_interpolationCnt = 0;
    g_spriteDeleteQueuePos = 0;
    for (i = 0; i < g_spriteDeleteQueueSize; i++) SpriteDeletionQueue[i] = -1;

    startofdynamicinterpolations = 0;
}

static void resetprestat(int snum,int g)
{
    DukePlayer_t *p = g_player[snum].ps;

    p->hbomb_on          = 0;
    p->cheat_phase       = 0;
    p->pals_time         = 0;
    p->toggle_key_flag   = 0;
    p->secret_rooms      = 0;
    p->max_secret_rooms  = 0;
    p->actors_killed     = 0;
    p->max_actors_killed = 0;
    p->lastrandomspot = 0;
    p->weapon_pos = 6;
    p->kickback_pic = 5;
    p->last_weapon = -1;
    p->weapreccnt = 0;
    p->interface_toggle_flag = 0;
    p->show_empty_weapon= 0;
    p->holster_weapon = 0;
    p->last_pissed_time = 0;

    p->one_parallax_sectnum = -1;
    p->visibility = ud.const_visibility;

    if (((g&MODE_EOL) != MODE_EOL && ud.multimode < 2) || (!(GametypeFlags[ud.coop]&GAMETYPE_PRESERVEINVENTORYDEATH) && ud.multimode > 1))
    {
        P_ResetWeapons(snum);
        P_ResetInventory(snum);
    }
    else if (p->curr_weapon == HANDREMOTE_WEAPON)
    {
        p->ammo_amount[HANDBOMB_WEAPON]++;
        p->curr_weapon = HANDBOMB_WEAPON;
        Gv_SetVar(g_iWeaponVarID, p->curr_weapon, p->i, snum);
    }

    p->timebeforeexit   = 0;
    p->customexitsound  = 0;

    g_player[snum].fraggedby_id = g_player[snum].fragged_id = 0;
    g_player[snum].fraggedby_time = g_player[snum].fragged_time = 0;
}

static void prelevel(char g)
{
    int i, nexti, j, startwall, endwall, lotaglist;
    int lotags[MAXSPRITES];
    int switchpicnum;
    extern char ror_protectedsectors[MAXSECTORS];

    clearbufbyte(show2dsector,sizeof(show2dsector),0L);
    clearbufbyte(show2dwall,sizeof(show2dwall),0L);
    clearbufbyte(show2dsprite,sizeof(show2dsprite),0L);

    Bmemset(ror_protectedsectors, 0, MAXSECTORS);

    resetpremapglobals();

    TRAVERSE_CONNECT(j)
    {
        resetprestat(j, g);
    }

    g_numClouds = 0;
    G_SetupGlobalPsky();

    X_OnEvent(EVENT_PRELEVEL, -1, -1, -1);

    for (i=0;i<numsectors;i++)
    {
        sector[i].extra = 256;

        switch (sector[i].lotag)
        {
        case ST_20_CEILING_DOOR:
        case ST_22_SPLITTING_DOOR:
            if (sector[i].floorz > sector[i].ceilingz)
                sector[i].lotag |= 32768;
            continue;
        }

        if (sector[i].ceilingstat&1)
        {
            if (sector[i].ceilingpicnum == CLOUDYSKIES && g_numClouds < 127)
                clouds[g_numClouds++] = i;

            TRAVERSE_CONNECT(j)
            {
                if (g_player[j].ps->one_parallax_sectnum == -1)
                    g_player[j].ps->one_parallax_sectnum = i;
            }
        }

        if (sector[i].lotag == 32767) //Found a secret room
        {
            TRAVERSE_CONNECT(j)
            {
                g_player[j].ps->max_secret_rooms++;
            }
            continue;
        }

        if (sector[i].lotag == -1)
        {
            TRAVERSE_CONNECT(j)
            {
                g_player[j].ps->exitx = wall[sector[i].wallptr].x;
                g_player[j].ps->exity = wall[sector[i].wallptr].y;
            }
            continue;
        }
    }

    i = headspritestat[STAT_DEFAULT];
    while (i >= 0)
    {
        nexti = nextspritestat[i];
        A_ResetVars(i);
        A_LoadActor(i);
        X_OnEvent(EVENT_LOADACTOR, i, -1, -1);
        if (sprite[i].lotag == -1 && (sprite[i].cstat&16))
        {
            TRAVERSE_CONNECT(j)
            {
                g_player[j].ps->exitx = SX(i);
                g_player[j].ps->exity = SY(i);
            }
        }
        else switch (DynamicTileMap[PN(i)])
            {
            case GPSPEED__STATIC:
                sector[SECT(i)].extra = SLT(i);
                deletesprite(i);
                break;

            case CYCLER__STATIC:
                if (g_numCyclers >= MAXCYCLERS)
                {
                    Bsprintf(tempbuf,"\nToo many cycling sectors (%d max).",MAXCYCLERS);
                    G_GameExit(tempbuf);
                }
                cyclers[g_numCyclers][0] = SECT(i);
                cyclers[g_numCyclers][1] = SLT(i);
                cyclers[g_numCyclers][2] = SS(i);
                cyclers[g_numCyclers][3] = sector[SECT(i)].floorshade;
                cyclers[g_numCyclers][4] = SHT(i);
                cyclers[g_numCyclers][5] = (SA(i) == 1536);
                g_numCyclers++;
                deletesprite(i);
                break;

            case SECTOREFFECTOR__STATIC:
            case ACTIVATOR__STATIC:
            case TOUCHPLATE__STATIC:
            case ACTIVATORLOCKED__STATIC:
            case MUSICANDSFX__STATIC:
            case LOCATORS__STATIC:
            case MASTERSWITCH__STATIC:
            case RESPAWN__STATIC:
                sprite[i].cstat = 0;
                break;
            }
        i = nexti;
    }

    for (i=0;i < MAXSPRITES;i++)
    {
        if (sprite[i].statnum < MAXSTATUS)
        {
            if (PN(i) == SECTOREFFECTOR && SLT(i) == 14)
                continue;
            A_Spawn(-1,i);
        }
    }

    for (i=0;i < MAXSPRITES;i++)
        if (sprite[i].statnum < MAXSTATUS)
        {
            if (PN(i) == SECTOREFFECTOR && SLT(i) == 14)
                A_Spawn(-1,i);

            if (sprite[i].cstat & 2048)
                ActorExtra[i].flags |= SPRITE_NOSHADE;
        }

    lotaglist = 0;

    i = headspritestat[STAT_DEFAULT];
    while (i >= 0)
    {
        switch (DynamicTileMap[PN(i)-1])
        {
        case DIPSWITCH__STATIC:
        case DIPSWITCH2__STATIC:
        case PULLSWITCH__STATIC:
        case HANDSWITCH__STATIC:
        case SLOTDOOR__STATIC:
        case LIGHTSWITCH__STATIC:
        case SPACELIGHTSWITCH__STATIC:
        case SPACEDOORSWITCH__STATIC:
        case FRANKENSTINESWITCH__STATIC:
        case LIGHTSWITCH2__STATIC:
        case POWERSWITCH1__STATIC:
        case LOCKSWITCH1__STATIC:
        case POWERSWITCH2__STATIC:
            for (j=0;j<lotaglist;j++)
                if (SLT(i) == lotags[j])
                    break;

            if (j == lotaglist)
            {
                lotags[lotaglist] = SLT(i);
                lotaglist++;
                if (lotaglist > MAXSPRITES-1)
                    G_GameExit("\nToo many switches.");

                j = headspritestat[STAT_EFFECTOR];
                while (j >= 0)
                {
                    if (sprite[j].lotag == SE_12_LIGHT_SWITCH && sprite[j].hitag == SLT(i))
                        ActorExtra[j].temp_data[0] = 1;
                    j = nextspritestat[j];
                }
            }
            break;
        }
        i = nextspritestat[i];
    }

    g_mirrorCount = 0;

    for (i = 0; i < numwalls; i++)
    {
        walltype *wal;
        wal = &wall[i];

        if (wal->overpicnum == MIRROR && (wal->cstat&32) != 0)
        {
            j = wal->nextsector;

            if (g_mirrorCount > 63)
                G_GameExit("\nToo many mirrors (64 max.)");
            if ((j >= 0) && sector[j].ceilingpicnum != MIRROR)
            {
                sector[j].ceilingpicnum = MIRROR;
                sector[j].floorpicnum = MIRROR;
                g_mirrorWall[g_mirrorCount] = i;
                g_mirrorSector[g_mirrorCount] = j;
                g_mirrorCount++;
                continue;
            }
        }

        if (g_numAnimWalls >= MAXANIMWALLS)
        {
            Bsprintf(tempbuf,"\nToo many 'anim' walls (%d max).",MAXANIMWALLS);
            G_GameExit(tempbuf);
        }

        animwall[g_numAnimWalls].tag = 0;
        animwall[g_numAnimWalls].wallnum = 0;
        switchpicnum = wal->overpicnum;
        if ((wal->overpicnum > W_FORCEFIELD)&&(wal->overpicnum <= W_FORCEFIELD+2))
        {
            switchpicnum = W_FORCEFIELD;
        }
        switch (DynamicTileMap[switchpicnum])
        {
        case FANSHADOW__STATIC:
        case FANSPRITE__STATIC:
            wall->cstat |= 65;
            animwall[g_numAnimWalls].wallnum = i;
            g_numAnimWalls++;
            break;

        case W_FORCEFIELD__STATIC:
            if (wal->overpicnum==W_FORCEFIELD__STATIC)
                for (j=0;j<3;j++)
                    tloadtile(W_FORCEFIELD+j, 0);
            if (wal->shade > 31)
                wal->cstat = 0;
            else wal->cstat |= 85+256;


            if (wal->lotag && wal->nextwall >= 0)
                wall[wal->nextwall].lotag =
                    wal->lotag;
            fallthrough__;

        case BIGFORCE__STATIC:

            animwall[g_numAnimWalls].wallnum = i;
            g_numAnimWalls++;

            continue;
        }

        wal->extra = -1;

        switch (DynamicTileMap[wal->picnum])
        {
        case WATERTILE2__STATIC:
            for (j=0;j<3;j++)
                tloadtile(wal->picnum+j, 0);
            break;

        case TECHLIGHT2__STATIC:
        case TECHLIGHT4__STATIC:
            tloadtile(wal->picnum, 0);
            break;
        case W_TECHWALL1__STATIC:
        case W_TECHWALL2__STATIC:
        case W_TECHWALL3__STATIC:
        case W_TECHWALL4__STATIC:
            animwall[g_numAnimWalls].wallnum = i;
            //                animwall[g_numAnimWalls].tag = -1;
            g_numAnimWalls++;
            break;
        case SCREENBREAK6__STATIC:
        case SCREENBREAK7__STATIC:
        case SCREENBREAK8__STATIC:
            for (j=SCREENBREAK6;j<SCREENBREAK9;j++)
                tloadtile(j, 0);
            animwall[g_numAnimWalls].wallnum = i;
            animwall[g_numAnimWalls].tag = -1;
            g_numAnimWalls++;
            break;

        case FEMPIC1__STATIC:
        case FEMPIC2__STATIC:
        case FEMPIC3__STATIC:

            wal->extra = wal->picnum;
            animwall[g_numAnimWalls].tag = -1;
            if (ud.lockout)
            {
                if (wal->picnum == FEMPIC1)
                    wal->picnum = BLANKSCREEN;
                else wal->picnum = SCREENBREAK6;
            }

            animwall[g_numAnimWalls].wallnum = i;
            animwall[g_numAnimWalls].tag = wal->picnum;
            g_numAnimWalls++;
            break;

        case SCREENBREAK1__STATIC:
        case SCREENBREAK2__STATIC:
        case SCREENBREAK3__STATIC:
        case SCREENBREAK4__STATIC:
        case SCREENBREAK5__STATIC:

        case SCREENBREAK9__STATIC:
        case SCREENBREAK10__STATIC:
        case SCREENBREAK11__STATIC:
        case SCREENBREAK12__STATIC:
        case SCREENBREAK13__STATIC:
        case SCREENBREAK14__STATIC:
        case SCREENBREAK15__STATIC:
        case SCREENBREAK16__STATIC:
        case SCREENBREAK17__STATIC:
        case SCREENBREAK18__STATIC:
        case SCREENBREAK19__STATIC:

            animwall[g_numAnimWalls].wallnum = i;
            animwall[g_numAnimWalls].tag = wal->picnum;
            g_numAnimWalls++;
            break;
        }
    }

    //Invalidate textures in sector behind mirror
    for (i=0;i<g_mirrorCount;i++)
    {
        startwall = sector[g_mirrorSector[i]].wallptr;
        endwall = startwall + sector[g_mirrorSector[i]].wallnum;
        for (j=startwall;j<endwall;j++)
        {
            wall[j].picnum = MIRROR;
            wall[j].overpicnum = MIRROR;
            if (wall[g_mirrorWall[i]].pal == 4)
                wall[j].pal = 4;
        }
    }
}

void G_NewGame(int vn,int ln,int sk)
{
    int i,j;

    handleevents();
    Net_GetPackets();

    if (g_skillSoundID >= 0 && ud.config.FXDevice >= 0 && ud.config.SoundToggle)
    {
        while (S_CheckSoundPlaying(g_skillSoundID))
        {
            handleevents();
            Net_GetPackets();
            S_Cleanup();
        }
    }

    g_skillSoundID = -1;

    waitforeverybody();
    ready2send = 0;

    if (ud.m_recstat != 2 && ud.last_level >= 0 && ud.multimode > 1 && (ud.coop&GAMETYPE_SCORESHEET))
        G_BonusScreen(1);

    if (ln == 0 && vn == 3 && ud.multimode < 2 && ud.lockout == 0)
    {
        S_PlayMusic(&EnvMusicFilename[1][0],MAXVOLUMES*MAXLEVELS+1);

        renderFlushPerms();
        videoSetViewableArea(0,0,xdim-1,ydim-1);
        videoClearViewableArea(0L);
        videoNextPage();

        G_PlayAnim("vol41a.anm",6);
        videoClearViewableArea(0L);
        videoNextPage();

        G_PlayAnim("vol42a.anm",7);
        G_PlayAnim("vol43a.anm",9);
        videoClearViewableArea(0L);
        videoNextPage();

        FX_StopAllSounds();
    }

    g_showShareware = 26*34;

    ud.level_number =   ln;
    ud.volume_number =  vn;
    ud.player_skill =   sk;
    ud.secretlevel =    0;
    ud.from_bonus = 0;
    ud.limit_hit = 0;

    ud.last_level = -1;
    g_lastSaveSlot = -1;

    //AddLog("Newgame");
    Gv_ResetVars();

    Gv_InitWeaponPointers();

    Gv_RefreshPointers();

    Gv_ResetSystemDefaults();

    for (i=0;i<(MAXVOLUMES*MAXLEVELS);i++)
        if (MapInfo[i].savedstate)
        {
            Bfree(MapInfo[i].savedstate);
            MapInfo[i].savedstate = NULL;
        }

    // Set up some player-specific info
    TRAVERSE_CONNECT(j)
    {
        DukePlayer_t *p = g_player[j].ps;
        p->zoom = 768;
        p->gm = 0;
        if (!(GametypeFlags[ud.m_coop] & GAMETYPE_COOP)) // If gametype isn't coop
        {
            // Give starting weapons.
            for (i = 0; i < MAX_WEAPONS; i++)
            {
                if (aplWeaponWorksLike[i][0] == PISTOL_WEAPON)
                {
                    p->curr_weapon = i;
                    p->gotweapon[i] = 1;
                    p->ammo_amount[i] = 48;
                    Gv_SetVar(g_iWeaponVarID, p->curr_weapon, p->i, 0);
                }
                else if (aplWeaponWorksLike[i][0] == KNEE_WEAPON)
                    p->gotweapon[i] = 1;
                else if (aplWeaponWorksLike[i][0] == HANDREMOTE_WEAPON)
                    p->gotweapon[i] = 1;
            }
            p->last_weapon = -1;
        }
    }

    display_mirror =        0;

    if (ud.multimode > 1)
    {
        if (numplayers < 2)
        {
            connecthead = 0;
            for (i=0;i<MAXPLAYERS;i++) connectpoint2[i] = i+1;
            connectpoint2[ud.multimode-1] = -1;
        }
    }
    else
    {
        connecthead = 0;
        connectpoint2[0] = -1;
    }

    X_OnEvent(EVENT_NEWGAME, g_player[screenpeek].ps->i, screenpeek, -1);
}

static void prepare_spawns(void)
{
    int i, nexti;
    spritetype *s;

    // Insert brown arrow player. Gets deleted by the next loop.
    A_InsertSprite(g_player[0].ps->cursectnum, g_player[0].ps->pos.x, g_player[0].ps->pos.y, g_player[0].ps->pos.z,
        APLAYER, 0, 0, 0, fix16_to_int(g_player[0].ps->q16ang), 0, 0, 0, 10);

    g_numPlayerSprites = 0;
    i = headspritestat[STAT_PLAYER];
    while (i >= 0)
    {
        nexti = nextspritestat[i];
        s = &sprite[i];

        if (g_numPlayerSprites == MAXSPAWNPOINTS)
        {
            initprintf("WARNING: Too many player sprites! (max %d.) Ignoring the rest.", MAXSPAWNPOINTS);
            break;
        }

        // Get position
        g_playerSpawnPoints[(unsigned char)g_numPlayerSprites].ox = s->x;
        g_playerSpawnPoints[(unsigned char)g_numPlayerSprites].oy = s->y;
        g_playerSpawnPoints[(unsigned char)g_numPlayerSprites].oz = s->z;
        g_playerSpawnPoints[(unsigned char)g_numPlayerSprites].oa = s->ang;
        g_playerSpawnPoints[(unsigned char)g_numPlayerSprites].os = s->sectnum;
        g_numPlayerSprites++;

        deletesprite(i); // Purge with great prejudice
        i = nexti;
    }
}

static void insert_player_sprite(int p)
{
    int spriteIndex;
    int sp; // Spawn point number
    spritetype *s;

    sp = p % g_numPlayerSprites; // Wrap spawn point index if more players than spawns.
    spriteIndex = A_InsertSprite(g_playerSpawnPoints[sp].os, g_playerSpawnPoints[sp].ox, g_playerSpawnPoints[sp].oy, g_playerSpawnPoints[sp].oz,
        APLAYER, 0, 0, 0, g_playerSpawnPoints[sp].oa, 0, 0, 0, 10);
    s = &sprite[spriteIndex];

    s->owner = spriteIndex;
    s->shade = 0;
    s->xrepeat = 42;
    s->yrepeat = 36;
    s->cstat = 1 + 256;
    s->xoffset = 0;
    s->clipdist = 64;

    // Either reset health to max if starting an episode, inventory is to be preserved, or was dead in the last level.
    if ((ud.last_level == -1) || (!(GametypeFlags[ud.coop] & GAMETYPE_PRESERVEINVENTORYDEATH) && ud.multimode > 1) || g_player[p].ps->last_extra <= 0)
    {
        g_player[p].ps->last_extra = g_player[p].ps->max_player_health;
        s->extra = g_player[p].ps->max_player_health;
        g_player[p].ps->runspeed = g_playerFriction;
    }
    else s->extra = g_player[p].ps->last_extra; // Use health from previous level.

    // Set sprite yvel to player index
    s->yvel = p;

    // Find a palette that doesn't match another player if color set to auto and not in TDM.
    if (!g_player[p].pcolor && ud.multimode > 1 && !(GametypeFlags[ud.coop] & GAMETYPE_TDM))
    {
        if (s->pal == 0) // Pal is 0, meaning "auto".
        {
            int colorfound = 0;
            int currentpal = playerColor_getNextColor(0, 1);

            // Iterate through all colors
            while (!colorfound)
            {
                for (int k = 0; k <= MAXPLAYERS; k++)
                {
                    // Iterated through all players and hit nothing, our color is found.
                    if (k == MAXPLAYERS)
                    {
                        colorfound = 1;
                        g_lastPlayerPal = currentpal;
                        break;
                    }

                    // Disregard non-existent or disconnected players.
                    if (g_player[k].ps == NULL || !g_player[k].connected) continue;

                    if (currentpal == g_player[k].ps->palookup)
                    {
                        // Color used, advance to the next color.
                        currentpal = playerColor_getNextColor(currentpal, 1);
                        break;
                    }
                }
                
                if (currentpal == 0) break;
            }

            if (!colorfound) // We hit our limit, let's just cycle through all the colors, starting from the last used.
            {
                currentpal = playerColor_getNextColor(g_lastPlayerPal, 0);
                g_lastPlayerPal = currentpal;
            }
            
            g_player[p].pcolor = s->pal = g_player[p].ps->palookup = currentpal;
        }
        else
        {
            g_player[p].pcolor = g_player[p].ps->palookup = s->pal;
        }
    }
    else
    {
        int k = g_player[p].pcolor;

        if (GametypeFlags[ud.coop] & GAMETYPE_TDM)
        {
            k = teams[g_player[p].pteam].palnum;
            g_player[p].ps->team = g_player[p].pteam;
        }
        s->pal = g_player[p].ps->palookup = k;
    }

    g_player[p].ps->i = spriteIndex;
    g_player[p].ps->frag_ps = p;
    ActorExtra[spriteIndex].owner = spriteIndex;

    ActorExtra[spriteIndex].bpos.x = g_player[p].ps->bobposx = g_player[p].ps->opos.x = g_player[p].ps->pos.x = s->x;
    ActorExtra[spriteIndex].bpos.y = g_player[p].ps->bobposy = g_player[p].ps->opos.y = g_player[p].ps->pos.y = s->y;
    ActorExtra[spriteIndex].bpos.z = g_player[p].ps->opos.z = g_player[p].ps->pos.z = s->z;
    g_player[p].ps->oq16ang = g_player[p].ps->q16ang = fix16_from_int(s->ang);

    g_player[p].ps->cursectnum = s->sectnum;
}

static void resetpspritevars(char g)
{
    short i;
    STATUSBARTYPE tsbar[MAXPLAYERS];

    UNREFERENCED_PARAMETER(g);

    if (ud.recstat != 2)
    {
        for (i = 0; i < MAXPLAYERS; i++)
        {
            if (g_player[i].ps == NULL || !g_player[i].connected) continue;

            // Kept to keep old behavior. Rest of the block that was here was useless.
            if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_PRESERVEINVENTORYDEATH) && ud.last_level >= 0)
            {
                tsbar[i].inven_icon = g_player[i].ps->inven_icon;
                tsbar[i].steroids_amount = g_player[i].ps->steroids_amount;
            }

            // Reset status for player
            P_ResetStatus(i);

            // Kept to keep old behavior. Rest of the block that was here was useless.
            if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_PRESERVEINVENTORYDEATH) && ud.last_level >= 0)
            {
                g_player[i].ps->inven_icon = tsbar[i].inven_icon;
                g_player[i].ps->steroids_amount = tsbar[i].steroids_amount;
            }
        }
    }

    // Prepare spawn points
    prepare_spawns();
    
    g_lastPlayerPal = 0;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (g_player[i].ps == NULL || !g_player[i].connected) continue;

        insert_player_sprite(i);
    }
}

static inline void clearfrags(void)
{
    int32_t i;
    playerdata_t *p;

    for (i = 0; i < MAXPLAYERS; i++)
    {
        p = &g_player[i];

        if (p->ps != NULL)
        {
            p->ps->frag = p->ps->fraggedself = 0;
            Bmemset(p->frags, 0, sizeof(p->frags));
        }
    }
}

static inline void clearteams(void)
{
    int8_t i;
    for (i = 0; i < numTeams; i++)
    {
        teams[i].frags = 0;
        teams[i].suicides = 0;
        teams[i].blocked = 0;
    }
}

void G_ResetTimers(void)
{
    totalclock = cloudtotalclock = ototalclock = lockclock = 0L;
    ready2send = 1;
    g_levelTextTime = 85;
    g_moveThingsCount = 0;
}

extern int jump_input;

void clearfifo(void)
{
    int i = 0;

    Net_ClearFIFO();

    jump_input = 0;

    clearbufbyte(&loc,sizeof(input_t),0L);
    clearbufbyte(&inputfifo,sizeof(input_t)*MOVEFIFOSIZ*MAXPLAYERS,0L);

    for (;i<MAXPLAYERS;i++)
    {
        if (g_player[i].inputBits != NULL)
            Bmemset(g_player[i].inputBits,0,sizeof(input_t));
        g_player[i].vote = 0;
        g_player[i].gotvote = 0;
    }
}

int32_t G_FindLevelByFile(const char *fn)
{
    int32_t volume, level;

    for (volume=0; volume<MAXVOLUMES; volume++)
    {
        for (level=0; level<MAXLEVELS; level++)
        {
            if (MapInfo[(volume*MAXLEVELS)+level].filename != NULL)
                if (!Bstrcasecmp(fn, MapInfo[(volume*MAXLEVELS)+level].filename))
                    return ((volume * MAXLEVELS) + level);
        }
    }
    return MAXLEVELS*MAXVOLUMES;
}

int G_EnterLevel(int g)
{
    int i, mii;
    char levname[BMAX_PATH];
    int16_t lbang;

//    mmulti_flushpackets();
//    waitforeverybody();

    vote_map = vote_episode = voting = -1;

    if ((g&MODE_DEMO) != MODE_DEMO) ud.recstat = ud.m_recstat;

    ud.monsters_off = ud.m_monsters_off;
    ud.coop = ud.m_coop;

    ud.ffire = ud.m_ffire;
    ud.respawn_monsters = (ud.player_skill > 3) ? 1 : ud.m_respawn_monsters;
    ud.noexits = (GTFLAGS(GAMETYPE_COOP)) ? 0 : ud.m_noexits;
    ud.marker = (GTFLAGS(GAMETYPE_MARKEROPTION)) ? ud.m_marker : 0;

    if (GTFLAGS(GAMETYPE_ALLOWITEMRESPAWN))
    {
        ud.respawn_items = ud.m_respawn_items;
        ud.respawn_inventory = ud.m_respawn_inventory;
    }
    else
    {
        ud.respawn_items = ud.respawn_inventory = 0;
    }

    if (ud.m_weaponstay)
    {
        GametypeFlags[ud.coop] |= GAMETYPE_WEAPSTAY;
    }
    else
    {
        GametypeFlags[ud.coop] &= ~(GAMETYPE_WEAPSTAY);
    }

    if ((g&MODE_DEMO) == 0 && ud.recstat == 2)
        ud.recstat = 0;

    Gv_RefreshPointers();

    FX_StopAllSounds();
    S_ClearSoundLocks();
    FX_SetReverb(0);

    if (boardfilename[0] != 0 && ud.m_level_number == 7 && ud.m_volume_number == 0)
    {
        int32_t volume, level;

        Bcorrectfilename(boardfilename,0);

        volume = level = G_FindLevelByFile(boardfilename);

        if (level != MAXLEVELS*MAXVOLUMES)
        {
            level &= MAXLEVELS-1;
            volume = (volume - level) / MAXLEVELS;

            ud.level_number = ud.m_level_number = level;
            ud.volume_number = ud.m_volume_number = volume;
            boardfilename[0] = 0;
        }
    }

    mii = (ud.volume_number*MAXLEVELS)+ud.level_number;

    if (MapInfo[mii].name == NULL || MapInfo[mii].filename == NULL)
    {
        if (boardfilename[0] != 0 && ud.m_level_number == 7 && ud.m_volume_number == 0)
        {
            if (MapInfo[mii].filename == NULL)
                MapInfo[mii].filename = (char*)Bcalloc(BMAX_PATH, sizeof(uint8_t));
            if (MapInfo[mii].name == NULL)
                MapInfo[mii].name = Bstrdup("User Map");
        }
        else
        {
            OSD_Printf(OSDTEXT_RED "Map E%dL%d not defined!\n", ud.volume_number+1, ud.level_number+1);
            return 1;
        }
    }

    i = ud.screen_size;
    ud.screen_size = 0;
    G_DoLoadScreen(NULL, -1);
    G_UpdateScreenArea();
    ud.screen_size = i;

    if (boardfilename[0] != 0 && ud.m_level_number == 7 && ud.m_volume_number == 0)
    {
        Bstrcpy(levname, boardfilename);
        Bsprintf(apptitle,"%s - %s - " APPNAME,levname,duke3dgrpstring);
    }
    else Bsprintf(apptitle,"%s - %s - " APPNAME,MapInfo[mii].name,duke3dgrpstring);

    Bstrcpy(tempbuf,apptitle);
    wm_setapptitle(tempbuf);

    if (!VOLUMEONE)
    {
        if (boardfilename[0] != 0 && ud.m_level_number == 7 && ud.m_volume_number == 0)
        {
            if (engineLoadBoard(boardfilename,0,&g_player[0].ps->pos, &lbang ,&g_player[0].ps->cursectnum) == -1)
            {
                OSD_Printf(OSD_ERROR "Map '%s' not found!\n",boardfilename);
                //G_GameExit(tempbuf);
                return 1;
            }

            {
                char *p;

                strcpy(levname, boardfilename);
                p = Bstrrchr(levname,'.');
                if (!p) strcat(levname,".mhk");
                else
                {
                    p[1]='m';
                    p[2]='h';
                    p[3]='k';
                    p[4]=0;
                }
                if (!engineLoadMHK(levname)) initprintf("Loaded map hack file '%s'\n",levname);

                // usermap music based on map filename
                Bcorrectfilename(levname,0);
                p = Bstrrchr(levname,'.');
                if (p)
                {
                    int fil;

                    p[1]='o';
                    p[2]='g';
                    p[3]='g';
                    p[4]=0;

                    fil = kopen4loadfrommod(levname,0);

                    if (fil > -1)
                    {
                        kclose(fil);
                        if (MapInfo[ud.m_level_number].alt_musicfn == NULL)
                            MapInfo[ud.m_level_number].alt_musicfn = (char*)Bcalloc(Bstrlen(levname)+1,sizeof(char));
                        else if ((Bstrlen(levname)+1) > sizeof(MapInfo[ud.m_level_number].alt_musicfn))
                            MapInfo[ud.m_level_number].alt_musicfn = (char*)Brealloc(MapInfo[ud.m_level_number].alt_musicfn,(Bstrlen(levname)+1));
                        Bstrcpy(MapInfo[ud.m_level_number].alt_musicfn,levname);
                    }
                    else if (MapInfo[ud.m_level_number].alt_musicfn != NULL)
                    {
                        Bfree(MapInfo[ud.m_level_number].alt_musicfn);
                        MapInfo[ud.m_level_number].alt_musicfn = NULL;
                    }

                    p[1]='m';
                    p[2]='i';
                    p[3]='d';
                    p[4]=0;

                    fil = kopen4loadfrommod(levname,0);

                    if (fil == -1)
                        Bsprintf(levname,"dethtoll.mid");
                    else kclose(fil);

                    if (MapInfo[ud.m_level_number].musicfn == NULL)
                        MapInfo[ud.m_level_number].musicfn = (char*)Bcalloc(Bstrlen(levname)+1,sizeof(char));
                    else if ((Bstrlen(levname)+1) > sizeof(MapInfo[ud.m_level_number].musicfn))
                        MapInfo[ud.m_level_number].musicfn = (char*)Brealloc(MapInfo[ud.m_level_number].musicfn,(Bstrlen(levname)+1));
                    Bstrcpy(MapInfo[ud.m_level_number].musicfn,levname);
                }
            }
        }
        else if (engineLoadBoard(MapInfo[mii].filename,0,&g_player[0].ps->pos, &lbang, &g_player[0].ps->cursectnum) == -1)
        {
            OSD_Printf(OSD_ERROR "Map %s not found!\n",MapInfo[mii].filename);
            //G_GameExit(tempbuf);
            return 1;
        }
        else
        {
            char *p;
            strcpy(levname, MapInfo[mii].filename);
            p = Bstrrchr(levname,'.');
            if (!p) strcat(levname,".mhk");
            else
            {
                p[1]='m';
                p[2]='h';
                p[3]='k';
                p[4]=0;
            }
            if (!engineLoadMHK(levname)) initprintf("Loaded map hack file '%s'\n",levname);
        }
    }
    else
    {

        i = strlen(MapInfo[mii].filename);
        copybufbyte(MapInfo[mii].filename,&levname[0],i);
        levname[i] = 255;
        levname[i+1] = 0;

        if (engineLoadBoard(levname,1,&g_player[0].ps->pos, &lbang, &g_player[0].ps->cursectnum) == -1)
        {
            OSD_Printf(OSD_ERROR "Map '%s' not found!\n",MapInfo[mii].filename);
            //G_GameExit(tempbuf);
            return 1;
        }
        else
        {
            char *p;
            p = Bstrrchr(levname,'.');
            if (!p) strcat(levname,".mhk");
            else
            {
                p[1]='m';
                p[2]='h';
                p[3]='k';
                p[4]=0;
            }
            if (!engineLoadMHK(levname)) initprintf("Loaded map hack file '%s'\n",levname);
        }
    }

    g_player[0].ps->q16ang = fix16_from_int(lbang);

    g_precacheCount = 0;
    clearbufbyte(gotpic,sizeof(gotpic),0L);
    clearbufbyte(precachehightile, sizeof(precachehightile), 0l);
    //clearbufbyte(ActorExtra,sizeof(ActorExtra),0l); // JBF 20040531: yes? no?

    prelevel(g);

    allignwarpelevators();
    resetpspritevars(g);

    //cachedebug = 0;
    automapping = 0;

    G_CacheMapData();

    if (ud.recstat != 2)
    {
        g_musicIndex = (ud.volume_number*MAXLEVELS) + ud.level_number;
        if (MapInfo[g_musicIndex].musicfn != NULL)
            S_PlayMusic(&MapInfo[g_musicIndex].musicfn[0],g_musicIndex);
    }

    if ((g&MODE_GAME) || (g&MODE_EOL))
        g_player[myconnectindex].ps->gm = MODE_GAME;
    else if (g&MODE_RESTART)
    {
        if (ud.recstat == 2)
            g_player[myconnectindex].ps->gm = MODE_DEMO;
        else g_player[myconnectindex].ps->gm = MODE_GAME;
    }

    if ((ud.recstat == 1) && (g&MODE_RESTART) != MODE_RESTART)
        G_OpenDemoWrite();

    if (VOLUMEONE)
    {
        if (ud.level_number == 0 && ud.recstat != 2) P_DoQuote(40,g_player[myconnectindex].ps);
    }

    TRAVERSE_CONNECT(i)
    switch (DynamicTileMap[sector[sprite[g_player[i].ps->i].sectnum].floorpicnum])
    {
    case HURTRAIL__STATIC:
    case FLOORSLIME__STATIC:
    case FLOORPLASMA__STATIC:
        P_ResetWeapons(i);
        P_ResetInventory(i);
        g_player[i].ps->gotweapon[PISTOL_WEAPON] = 0;
        g_player[i].ps->ammo_amount[PISTOL_WEAPON] = 0;
        g_player[i].ps->curr_weapon = KNEE_WEAPON;
        g_player[i].ps->kickback_pic = 0;
        Gv_SetVar(g_iWeaponVarID, g_player[i].ps->curr_weapon, g_player[i].ps->i, i);
        break;
    }

    //PREMAP.C - replace near the my's at the end of the file

    Net_ResetPrediction();

    //g_player[myconnectindex].ps->palette = palette;
    //G_FadePalette(0,0,0,0);
    P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 0);    // JBF 20040308

    P_UpdateScreenPal(g_player[myconnectindex].ps);
    renderFlushPerms();

    everyothertime = 0;
    g_globalRandom = 0;

    ud.last_level = ud.level_number+1;

    clearfifo();

    for (i=g_interpolationCnt-1;i>=0;i--) bakipos[i] = *curipos[i];

    g_restorePalette = 1;

    waitforeverybody();
    mmulti_flushpackets();

    G_FadePalette(0,0,0,0);
    G_UpdateScreenArea();
    videoClearViewableArea(0L);
    G_DrawBackground();
    G_DrawRooms(myconnectindex,65536);

    g_player[myconnectindex].ps->over_shoulder_on = 0;

    clearfrags();
    clearteams();

    G_ResetTimers();  // Here we go

    //Bsprintf(g_szBuf,"G_EnterLevel L=%d V=%d",ud.level_number, ud.volume_number);
    //AddLog(g_szBuf);
    // variables are set by pointer...

    Bmemcpy(&currentboardfilename[0],&boardfilename[0],BMAX_PATH);

    X_OnEvent(EVENT_ENTERLEVEL, -1, -1, -1);
    OSD_Printf(OSDTEXT_YELLOW "E%dL%d: %s\n",ud.volume_number+1,ud.level_number+1,MapInfo[mii].name);
    return 0;
}

void G_FreeMapState(int mapnum)
{
    int j;

    for (j=0;j<g_gameVarCount;j++)
    {
        if (aGameVars[j].dwFlags & GAMEVAR_NORESET) continue;
        if (aGameVars[j].dwFlags & GAMEVAR_PERPLAYER)
        {
            if (MapInfo[mapnum].savedstate->vars[j])
                Bfree(MapInfo[mapnum].savedstate->vars[j]);
        }
        else if (aGameVars[j].dwFlags & GAMEVAR_PERACTOR)
        {
            if (MapInfo[mapnum].savedstate->vars[j])
                Bfree(MapInfo[mapnum].savedstate->vars[j]);
        }
    }
    Bfree(MapInfo[mapnum].savedstate);
    MapInfo[mapnum].savedstate = NULL;
}

