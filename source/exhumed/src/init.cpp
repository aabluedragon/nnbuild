//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2019 EDuke32 developers and contributors
Copyright (C) 2019 sirlemonhead, Nuke.YKT

This file is part of PCExhumed.

PCExhumed is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "compat.h"
#include "build.h"
#include "keyboard.h"
#include "control.h"
#include "init.h"
#include "runlist.h"
#include "kexbreak.h"
#include "kextactile.h"
#include "kexinv.h"
#include "switch.h"
#include "object.h"
#include "aistuff.h"
#include "ramses.h"
#include "player.h"
#include "mummy.h"
#include "move.h"
#include "ra.h"
#include "view.h"
#include "runlist.h"
#include "engine.h"
#include "sound.h"
#include "exhumed.h"
#include "config.h"
#include "items.h"
#include "light.h"
#include "map.h"
#include "menu.h"
#include "lighting.h"
#include "anims.h"
#include "input.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

enum
{
    kTagRamses = 61,
};

ClockTicks ototalclock = 0;

int initx, inity, initz;
short inita, initsect;

short nCurChunkNum = 0;

short nBodyGunSprite[50];
int movefifoend;
int movefifopos;

short nCurBodyGunNum;

short SectSoundSect[kMaxSectors] = { 0 };
short SectSound[kMaxSectors]     = { 0 };
short SectFlag[kMaxSectors]      = { 0 };
int   SectDepth[kMaxSectors]     = { 0 };
int   SectAbove[kMaxSectors]     = { 0 };
short SectDamage[kMaxSectors]    = { 0 };
uint8_t KexHazard[kMaxSectors]   = { 0 };
short SectSpeed[kMaxSectors]     = { 0 };
int   SectBelow[kMaxSectors]     = { 0 };


uint8_t bIsVersion6 = kTrue;

#if 0
// definitions for map version 6 structures
#pragma pack(1)

// 37 bytes
struct Sector_6
{
    uint16_t wallptr, wallnum;
    short ceilingpicnum, floorpicnum;
    short ceilingheinum, floorheinum;
    int ceilingz, floorz;
    int8_t ceilingshade, floorshade;
    uint8_t ceilingxpanning, floorxpanning;
    uint8_t ceilingypanning, floorypanning;
    uint8_t ceilingstat, floorstat;
    uint8_t ceilingpal, floorpal;
    uint8_t visibility;
    short lotag, hitag, extra;
};

struct Wall_6
{
    int x, y;
    short point2, nextsector, nextwall;
    short picnum, overpicnum;
    int8_t shade;
    uint8_t pal;
    short cstat;
    uint8_t xrepeat, yrepeat, xpanning, ypanning;
    short lotag, hitag, extra;
};

// 43 bytes
struct Sprite_6
{
    int x, y, z;
    short cstat;
    int8_t shade;
    uint8_t pal, clipdist;
    uint8_t xrepeat, yrepeat;
    int8_t xoffset, yoffset;
    short picnum, ang, xvel, yvel, zvel, owner;
    short sectnum, statnum;
    short lotag, hitag, extra;
};

#pragma pack()

static Sector_6 sector_6[1024];
static Wall_6   wall_6[8192];
static Sprite_6 sprite_6[4096];
#endif


bool LoadLevel(int nMap)
{
    char fileName[BMAX_PATH];

    artCrossplayResetLevel();
    initspritelists();

    if (bUserMap)
    {
        nMap = 0;
        Bstrncpy(fileName, gUserMapFilename, BMAX_PATH);
    }
    else
    {
        Bsnprintf(fileName, sizeof(fileName), "lev%d.map", nMap);
    }

    // init stuff
    {
        StopAllSounds();
        nCreaturesLeft = 0;
        nFreeze = 0;
        nSpiritSprite = -1;

        InitLion();
        InitRexs();
        InitSets();
        InitQueens();
        InitRoachs();
        InitWasps();
        InitRats();
        InitBullets();
        InitWeapons();
        InitGrenades();
        InitAnims();
        InitSnakes();
        InitFishes();
        InitLights();
        InitMap();
        InitBubbles();
        InitObjects();
        InitLava();
        InitPushBlocks();
        InitAnubis();
        InitSpider();
        InitMummy();
        InitScorp();
        InitPlayer();
        InitItems();
        InitInput();

        if (nMap == kMap20) {
            InitEnergyTile();
        }
    }

    if (nMap > 15)
    {
        nSwitchSound = 35;
        nStoneSound = 23;
        nElevSound = 51;
        nStopSound = 35;
    }
    else
    {
        nSwitchSound = 33;
        nStoneSound = 23;
        nElevSound = 23;
        nStopSound = 66;
    }

    if (nMap < 0) {
        return false;
    }

#if 0
    {
        // going to load the map without loadboard() - to take care of version 6 to 7 map conversion
        //int hFile = kopen4load(fileName_1, 1);
        int hFile = kopen4load(fileName_1, 0);
        //	int hFile = open(fileName_1, O_BINARY | O_RDONLY);
        if (hFile == -1) {
            return false;
        }

        int version;

        kread(hFile, &version, sizeof(version));
        if (version != 6) {
            bIsVersion6 = kFalse;
        }

        initspritelists();

        memset(show2dsector, 0, sizeof(show2dsector));
        memset(show2dsprite, 0, sizeof(show2dsprite));
        memset(show2dwall,   0, sizeof(show2dwall));

        // replacement for build's loadboard()
        kread(hFile, &initx, 4);
        kread(hFile, &inity, 4);
        kread(hFile, &initz, 4);
        kread(hFile, &inita, 2);
        kread(hFile, &initsect, 2);

        // sectors
        short nSectors;
        kread(hFile, &nSectors, sizeof(nSectors));

        if (bIsVersion6) {
            kread(hFile, sector_6, sizeof(Sector_6) * nSectors);
        }
        else {
            kread(hFile, sector, sizeof(SECTOR) * nSectors);
        }

        // walls
        short nWalls;
        kread(hFile, &nWalls, sizeof(nWalls));

        if (bIsVersion6) {
            kread(hFile, wall_6, sizeof(Wall_6) * nWalls);
        }
        else {
            kread(hFile, wall, sizeof(WALL) * nWalls);
        }

        // sprites
        short nSprites;
        kread(hFile, &nSprites, sizeof(nSprites));

        if (bIsVersion6) {
            kread(hFile, sprite_6, sizeof(Sprite_6) * nSprites);
        }
        else {
            kread(hFile, sprite, sizeof(SPRITE) * nSprites);
        }

        // set engine variables
        numsectors = nSectors;
        numwalls = nWalls;

        // load in our version 6 structs to the engines v7 structs if required
        if (bIsVersion6)
        {
            for (int nSector = 0; nSector < nSectors; nSector++)
            {
                sector[nSector].ceilingz = sector_6[nSector].ceilingz;
                sector[nSector].floorz = sector_6[nSector].floorz;
                sector[nSector].wallptr = sector_6[nSector].wallptr;
                sector[nSector].wallnum = sector_6[nSector].wallnum;
                sector[nSector].ceilingpicnum = sector_6[nSector].ceilingpicnum;
                sector[nSector].ceilingheinum = Max(Min(((int)sector_6[nSector].ceilingheinum) << 5, 32767), -32768);

                if ((sector_6[nSector].ceilingstat & 2) == 0) {
                    sector[nSector].ceilingheinum = 0;
                }

                sector[nSector].ceilingshade = sector_6[nSector].ceilingshade;
                sector[nSector].ceilingpal = sector_6[nSector].ceilingpal;
                sector[nSector].ceilingxpanning = sector_6[nSector].ceilingxpanning;
                sector[nSector].ceilingypanning = sector_6[nSector].ceilingypanning;
                sector[nSector].floorpicnum = sector_6[nSector].floorpicnum;
                sector[nSector].floorheinum = Max(Min(((int)sector_6[nSector].floorheinum) << 5, 32767), -32768);

                if ((sector_6[nSector].floorstat & 2) == 0) {
                    sector[nSector].floorheinum = 0;
                }

                sector[nSector].floorshade = sector_6[nSector].floorshade;
                sector[nSector].floorpal = sector_6[nSector].floorpal;
                sector[nSector].floorxpanning = sector_6[nSector].floorxpanning;
                sector[nSector].floorypanning = sector_6[nSector].floorypanning;
                sector[nSector].ceilingstat = sector_6[nSector].ceilingstat;
                sector[nSector].floorstat = sector_6[nSector].floorstat;
                sector[nSector].visibility = sector_6[nSector].visibility;
                sector[nSector].filler = 0;
                sector[nSector].lotag = sector_6[nSector].lotag;
                sector[nSector].hitag = sector_6[nSector].hitag;
                sector[nSector].extra = sector_6[nSector].extra;
            }

            for (int nWall = 0; nWall < nWalls; nWall++)
            {
                wall[nWall].x = wall_6[nWall].x;
                wall[nWall].y = wall_6[nWall].y;
                wall[nWall].point2 = wall_6[nWall].point2;
                wall[nWall].nextwall = wall_6[nWall].nextwall;
                wall[nWall].nextsector = wall_6[nWall].nextsector;
                wall[nWall].cstat = wall_6[nWall].cstat;
                wall[nWall].picnum = wall_6[nWall].picnum;
                wall[nWall].overpicnum = wall_6[nWall].overpicnum;
                wall[nWall].shade = wall_6[nWall].shade;
                wall[nWall].pal = wall_6[nWall].pal;
                wall[nWall].xrepeat = wall_6[nWall].xrepeat;
                wall[nWall].yrepeat = wall_6[nWall].yrepeat;
                wall[nWall].xpanning = wall_6[nWall].xpanning;
                wall[nWall].ypanning = wall_6[nWall].ypanning;
                wall[nWall].lotag = wall_6[nWall].lotag;
                wall[nWall].hitag = wall_6[nWall].hitag;
                wall[nWall].extra = wall_6[nWall].extra;
            }

            for (int nSprite = 0; nSprite < nSprites; nSprite++)
            {
                sprite[nSprite].x = sprite_6[nSprite].x;
                sprite[nSprite].y = sprite_6[nSprite].y;
                sprite[nSprite].z = sprite_6[nSprite].z;
                sprite[nSprite].cstat = sprite_6[nSprite].cstat;
                sprite[nSprite].picnum = sprite_6[nSprite].picnum;
                sprite[nSprite].shade = sprite_6[nSprite].shade;
                sprite[nSprite].pal = sprite_6[nSprite].pal;
                sprite[nSprite].clipdist = sprite_6[nSprite].clipdist;
                sprite[nSprite].filler = 0;
                sprite[nSprite].xrepeat = sprite_6[nSprite].xrepeat;
                sprite[nSprite].yrepeat = sprite_6[nSprite].yrepeat;
                sprite[nSprite].xoffset = sprite_6[nSprite].xoffset;
                sprite[nSprite].yoffset = sprite_6[nSprite].yoffset;
                sprite[nSprite].sectnum = sprite_6[nSprite].sectnum;
                sprite[nSprite].statnum = sprite_6[nSprite].statnum;
                sprite[nSprite].ang = sprite_6[nSprite].ang;
                sprite[nSprite].owner = sprite_6[nSprite].owner;
                sprite[nSprite].xvel = sprite_6[nSprite].xvel;
                sprite[nSprite].yvel = sprite_6[nSprite].yvel;
                sprite[nSprite].zvel = sprite_6[nSprite].zvel;
                sprite[nSprite].lotag = sprite_6[nSprite].lotag;
                sprite[nSprite].hitag = sprite_6[nSprite].hitag;
                sprite[nSprite].extra = sprite_6[nSprite].extra;
            }
        }

        for (int nSprite = 0; nSprite < nSprites; nSprite++) {
            insertsprite(sprite[nSprite].sectnum, sprite[nSprite].statnum);
        }

        updatesector(initx, inity, &initsect);

        kclose(hFile);
        hFile = -1;
    }
    // loadboard has finished
#endif
    vec3_t startPos;
    int status = engineLoadBoard(fileName, 0, &startPos, &inita, &initsect);
    if (status == -2)
        status = engineLoadBoardV5V6(fileName, 0, &startPos, &inita, &initsect);
    if (status < 0)
    {
        OSD_Printf(OSD_ERROR "Map \"%s\" not found or invalid map version!\n", fileName);
        return false;
    }

    if (numyaxbunches > 0)
        OSD_Printf("TROR: %d bunches in \"%s\"\n", numyaxbunches, fileName);

    initx = startPos.x;
    inity = startPos.y;
    initz = startPos.z;
    artCrossplayCaptureMapSprites();
    artCrossplayPrepareForeignMapArt(fileName);
    artCrossplayApplyMapTileRemap();

#ifdef YAX_ENABLE
    // engineLoadBoard has already decoded any converted KEX TROR bunches.
    // Rebuild their runtime lists instead of clearing them; maps without
    // vertical topology retain the original reset behavior.
    yax_update(numyaxbunches > 0 ? 2 : 1);
#endif

    int i;

    for (i = 0; i < kMaxPlayers; i++)
    {
        PlayerList[i].nSprite = -1;
    }

    psky_t* pSky = tileSetupSky(0);

    pSky->tileofs[0] = 0;
    pSky->tileofs[1] = 0;
    pSky->tileofs[2] = 0;
    pSky->tileofs[3] = 0;
    pSky->yoffs = 256;
    pSky->lognumtiles = 2;
    pSky->horizfrac = 65536;
    pSky->yscale = 65536;
    parallaxtype = 2;
    g_visibility = Kex_BaseVisibility();
    flash = 0;
    precache();

    LoadObjects();

    levelnum = nMap;

    return true;
}

void ResetEngine()
{
    SetOverscan(BASEPAL);

    EraseScreen(-1);

    resettiming();

    totalclock  = 0;
    ototalclock = totalclock;
    localclock  = (int)totalclock;

    numframes = 0;
}

void InstallEngine()
{
    // initgroupfile("stuff.dat");

    char *cwd;

    // VibeBuild32: pass the literal "tiles000.art" (not a "%03i" format). The eduke32
    // engine's artGetIndexedFileName() substitutes digits positionally into a literal
    // "tilesNNN.art" name; a format string produced "tiles000i.art", so zero ART files
    // loaded -- every tilesiz stayed 0x0, the title screen drew nothing, and PlayMovie()
    // crashed writing through a null waloff[kMovieTile] (same bug that hit Blood and RR).
    artSetBaseSearchFirst(artCrossplayForeignDataCount() > 0 ? 1 : 0);
    if (artCrossplayForeignDataCount() > 0)
        LOG_F(INFO, "Crossplay: base runtime ART pinned to main game data.");

    if (g_modDir[0] != '/' && (cwd = buildvfs_getcwd(NULL, 0)))
    {
        buildvfs_chdir(g_modDir);
        if (artLoadFiles("tiles000.art", MAXCACHE1DSIZE) < 0)
        {
            buildvfs_chdir(cwd);
            if (artLoadFiles("tiles000.art", MAXCACHE1DSIZE) < 0)
                bail2dos("Failed loading art.");
        }
        buildvfs_chdir(cwd);
#ifndef __ANDROID__ //This crashes on *some* Android devices. Small onetime memory leak. TODO fix above function
        Xfree(cwd);
#endif
    }
    else if (artLoadFiles("tiles000.art",MAXCACHE1DSIZE) < 0)
        bail2dos("Failed loading art.");

    // TEMP

    //nScreenWidth *= 2;
    //nScreenHeight *= 2;
    bHiRes = kTrue;
    // TEMP

    if (engineInit())
    {
        wm_msgbox("Fatal Engine Initialization Error",
                  "There was a problem initializing the engine: %s\n\nThe application will now close.", engineerrstr);
        //TODO:
        //G_Cleanup();
        LOG_F(ERROR, "G_Startup: There was a problem initializing the engine: %s", engineerrstr);
        exit(6);
    }
    if (videoSetGameMode(gSetup.fullscreen, gSetup.xdim, gSetup.ydim, gSetup.bpp, 0) < 0)
    {
        LOG_F(ERROR, "Failure setting video mode %dx%dx%d %s! Trying next mode...", gSetup.xdim, gSetup.ydim,
                    gSetup.bpp, gSetup.fullscreen ? "fullscreen" : "windowed");

        int resIdx = 0;

        for (int i=0; i < validmodecnt; i++)
        {
            if (validmode[i].xdim == gSetup.xdim && validmode[i].ydim == gSetup.ydim)
            {
                resIdx = i;
                break;
            }
        }

        int const savedIdx = resIdx;
        int bpp = gSetup.bpp;

        while (videoSetGameMode(0, validmode[resIdx].xdim, validmode[resIdx].ydim, bpp, 0) < 0)
        {
            LOG_F(ERROR, "Failure setting video mode %dx%dx%d windowed! Trying next mode...",
                        validmode[resIdx].xdim, validmode[resIdx].ydim, bpp);

            if (++resIdx == validmodecnt)
            {
                if (bpp == 8)
                    bail2dos("Fatal error: unable to set any video mode!");

                resIdx = savedIdx;
                bpp = 8;
            }
        }

        gSetup.xdim = validmode[resIdx].xdim;
        gSetup.ydim = validmode[resIdx].ydim;
        gSetup.bpp  = bpp;
    }

    LoadPaletteLookups();

    enginecompatibilitymode = ENGINE_19950829;
}

void RemoveEngine()
{
    engineUnInit();
    uninitgroupfile();
}

void SetBelow(short nCurSector, short nBelowSector)
{
    SectBelow[nCurSector] = nBelowSector;
}

void SetAbove(short nCurSector, short nAboveSector)
{
    SectAbove[nCurSector] = nAboveSector;
}

void SnapSectors(short nSectorA, short nSectorB, short b)
{
    // edx - nSectorA
    // eax - nSectorB

    short nWallA = sector[nSectorA].wallptr;
    short nWallB = sector[nSectorB].wallptr;

    short num1 = sector[nSectorA].wallnum;
    short num2 = sector[nSectorB].wallnum;

    int nCount = 0;

    while (num1 > nCount)
    {
        short dx = nWallB;

        int esi = 0x7FFFFFF;
        int edi = esi;

        int x = wall[nWallA].x;
        int y = wall[nWallA].y;

        int var_14 = 0;

        int nCount2 = 0;

        while (nCount2 < num2)
        {
            int eax = x - wall[dx].x;
            int ebx = y - wall[dx].y;

            if (eax < 0) {
                eax = -eax;
            }

            int var_38 = eax;

            if (ebx < 0) {
                ebx = -ebx;
            }

            int var_3C = ebx;

            var_38 += var_3C;

            eax = esi;
            if (eax < 0) {
                eax = -eax;
            }

            var_3C = eax;

            eax = edi;
//			int var_34 = edi;
            if (eax < 0) {
                eax = -eax;
            }

            int var_34 = eax;

            var_34 += var_3C;

            if (var_38 < var_34)
            {
                esi = x - wall[dx].x;
                edi = y - wall[dx].y;
                var_14 = dx;
            }

            dx++;
            nCount2++;
        }

        dragpoint(var_14, wall[var_14].x + esi, wall[var_14].y + edi, 0);

        nCount++;
        nWallA++;
    }

    if (b) {
        sector[nSectorB].ceilingz = sector[nSectorA].floorz;
    }

    if (SectFlag[nSectorA] & 0x1000) {
        SnapBobs(nSectorA, nSectorB);
    }
}

void InitSectFlag()
{
    KexScripted_Reset();
    KexPlate_Reset();
    KexRemote_Reset();
    KexSwitch_Reset();
    KexSealedDoor_Reset();
    KexBeam_Reset();
    KexTrigger_Reset();
    KexHold_Reset();
    KexScript_Reset();
    KexFloat_Reset();
    KexPad_Reset();
    KexNoStanding_Reset();
    KexWall_Reset();
    KexRamses_Reset();
    KexTactile_Reset();
    for (int i = 0; i < kMaxSectors; i++)
    {
        SectSoundSect[i] = -1;
        SectSound[i] = -1;
        SectAbove[i] = -1;
        SectBelow[i] = -1;
        SectDepth[i] = 0;
        SectFlag[i]  = 0;
        SectSpeed[i] = 0;
        SectDamage[i] = 0;
        KexHazard[i] = kKexHazardNone;
    }
}

void ProcessSpriteTag(short nSprite, short nLotag, short nHitag)
{
    int nChannel = runlist_AllocChannel(nHitag % 1000);

    int nSpeed = nLotag / 1000;
    if (!nSpeed) {
        nSpeed = 1;
    }

    int nVal = nHitag;

    if (nLotag >= 900 && nLotag <= 949)
    {
        ProcessTrailSprite(nSprite, nLotag, nHitag);
        return;
    }

    // handle tags 6 to 60
    switch (nLotag)
    {
        case 8: // M-60 ammo belt
        {
            nVal = 3 * (nHitag / 3);
            // fall through to 6,7 etc
            fallthrough__;
        }
        case 6:
        case 7:
        case 9:
        case 10:
        case 11:
        case 15:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 26:
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
        case 39:
        case 40:
        case 41:
        case 42:
        case 43:
        case 44:
        case 45:
        case 46:
        case 47:
        case 48:
        case 49:
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 55:
        case 56:
        case 57:
        case 58:
        case 60:
        {
            sprite[nSprite].hitag = nVal;
            changespritestat(nSprite, nLotag + 900);
            sprite[nSprite].cstat &= 0xFEFE;
            BuildItemAnim(nSprite);
            return;
        }
        case 12: // berry twig
        {
            sprite[nSprite].hitag = 40;
            changespritestat(nSprite, nLotag + 900);
            sprite[nSprite].cstat &= 0xFEFE;
            BuildItemAnim(nSprite);
            return;
        }
        case 13: // blood bowl
        {
            sprite[nSprite].hitag = 160;
            changespritestat(nSprite, nLotag + 900);
            sprite[nSprite].cstat &= 0xFEFE;
            BuildItemAnim(nSprite);
            return;
        }
        case 14: // venom bowl
        {
            sprite[nSprite].hitag = -200;
            changespritestat(nSprite, nLotag + 900);
            sprite[nSprite].cstat &= 0xFEFE;
            BuildItemAnim(nSprite);
            return;
        }

        case 16:
            // reserved
            mydeletesprite(nSprite);
            return;

        case 25:
        case 59:
        {
            // extra life or checkpoint scarab. Delete for multiplayer
            if (nNetPlayerCount != 0)
            {
                mydeletesprite(nSprite);
                return;
            }
            else
            {
                sprite[nSprite].hitag = nVal;
                changespritestat(nSprite, nLotag + 900);
                sprite[nSprite].cstat &= 0xFEFE;
                BuildItemAnim(nSprite);
                return;
            }
        }
        case 27:
        {
            sprite[nSprite].hitag = 1;
            changespritestat(nSprite, 9 + 900);
            sprite[nSprite].cstat &= 0xFEFE;
            BuildItemAnim(nSprite);
            return;
        }

        case 38: // raw energy
        {
            nVal++;
            nVal--; // CHECKME ??
            sprite[nSprite].hitag = nVal;
            changespritestat(nSprite, nLotag + 900);
            sprite[nSprite].cstat &= 0xFEFE;
            BuildItemAnim(nSprite);
            return;
        }
    }

    int v6 = nLotag % 1000;

    if (!bNoCreatures || v6 < 100 || v6 > 118)
    {
        if (v6 > 999) {
            mydeletesprite(nSprite);
            return;
        }

        switch (v6)
        {
            case 999:
            {
                AddFlicker(sprite[nSprite].sectnum, nSpeed);
                break;
            }
            case 998:
            {
                AddGlow(sprite[nSprite].sectnum, nSpeed);
                break;
            }
            case 118: // Anubis with drum
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildAnubis(nSprite, 0, 0, 0, 0, 0, 1);
                return;
            }
            case 117:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                // VibeBuild32: hitag 1 is a crow -- the wasp in bird's feathers.
                BuildWasp(nSprite, 0, 0, 0, 0, 0, g_kexMode && nHitag == 1);
                return;
            }
            case 116:
            {
                BuildRat(nSprite, 0, 0, 0, 0, -1);
                return;
            }
            case 115: // Rat (eating)
            {
                BuildRat(nSprite, 0, 0, 0, 0, 0);
                return;
            }
            case 113:
            {
                BuildQueen(nSprite, 0, 0, 0, 0, 0, nChannel);
                return;
            }
            case 112:
            {
                BuildScorp(nSprite, 0, 0, 0, 0, 0, nChannel);
                return;
            }
            case 111:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildSet(nSprite, 0, 0, 0, 0, 0, nChannel);
                return;
            }
            case 108:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildLava(nSprite, 0, 0, 0, 0, 0, nChannel);
                return;
            }
            case 107:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildRex(nSprite, 0, 0, 0, 0, 0, nChannel);
                return;
            }
            case 106:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildFish(nSprite, 0, 0, 0, 0, 0);
                return;
            }
            case 105:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildSpider(nSprite, 0, 0, 0, 0, 0, g_kexMode && nHitag == 1);   // VibeBuild32: hitag 1 = the remaster's scorpion
                return;
            }
            case 104:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildRoach(1, nSprite, 0, 0, 0, 0, 0);
                return;
            }
            case 103:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildRoach(0, nSprite, 0, 0, 0, 0, 0);
                return;
            }
            case 102:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildLion(nSprite, 0, 0, 0, 0, 0);
                return;
            }
            case 101:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildMummy(nSprite, 0, 0, 0, 0, 0);
                return;
            }
            case 100:
            {
                if (bNoCreatures) {
                    mydeletesprite(nSprite);
                    return;
                }

                BuildAnubis(nSprite, 0, 0, 0, 0, 0, 0);
                return;
            }
            case 82:    // VibeBuild32: the remaster's submerged sector with no surface above it
            {
                SectFlag[sprite[nSprite].sectnum] |= kSectUnderwater;
                mydeletesprite(nSprite);
                return;
            }
            case 83:    // VibeBuild32: Kex sector flag 0x20, "No standing on edges"
            {
                KexNoStanding_Add(nSprite);
                return;
            }
            case 99: // underwater type 2
            {
                short nSector = sprite[nSprite].sectnum;
                SetAbove(nSector, nHitag);
                SectFlag[nSector] |= kSectUnderwater;

                mydeletesprite(nSprite);
                return;
            }
            case 98:
            {
                short nSector = sprite[nSprite].sectnum;
                SetBelow(nSector, nHitag);
                SnapSectors(nSector, nHitag, 1);

                mydeletesprite(nSprite);
                return;
            }
            case 97:
            {
                AddSectorBob(sprite[nSprite].sectnum, nHitag, 1);

                mydeletesprite(nSprite);
                return;
            }
            case 96: // Lava sector
            {
                int nDamage = nHitag / 4;
                if (!nDamage) {
                    nDamage = 1;
                }

                short nSector = sprite[nSprite].sectnum;

                SectDamage[nSector] = nDamage;
                SectFlag[nSector] |= kSectLava;
                // kexmap.py reserves the two marker values below for the
                // KEX floor-face flags: lava (0x20) and slime (0x80).  The
                // ordinary damaging-sector behaviour remains intact for all
                // classic maps and any non-KEX marker.
                if (g_kexMode)
                    KexHazard[nSector] |= nHitag == 40 ? kKexHazardLava
                                        : nHitag == 16 ? kKexHazardSlime
                                                       : kKexHazardNone;

                mydeletesprite(nSprite);
                return;
            }
            case 95:
            {
                AddSectorBob(sprite[nSprite].sectnum, nHitag, 0);

                mydeletesprite(nSprite);
                return;
            }
            case 94: // water
            {
                short nSector = sprite[nSprite].sectnum;
                SectDepth[nSector] = nHitag << 8;

                mydeletesprite(nSprite);
                return;
            }
            case 93:
            {
                BuildBubbleMachine(nSprite);
                return;
            }
            case 90:
            {
                BuildObject(nSprite, 3, nHitag);
                return;
            }
            case 91:    // VibeBuild32: Kex DOOR_OPEN_ONLY_MESSAGE direct-use behavior
            {
                KexSealedDoor_Add(sprite[nSprite].sectnum);
                mydeletesprite(nSprite);
                return;
            }
            case 79:
            case 89:
            {
                short nSector = sprite[nSprite].sectnum;

                SectSpeed[nSector] = nSpeed;
                SectFlag[nSector] |= sprite[nSprite].ang;

                mydeletesprite(nSprite);
                return;
            }
            case 88:
            {
                AddFlow(nSprite, nSpeed, 0);

                mydeletesprite(nSprite);
                return;
            }
            case 80: // underwater
            {
                short nSector = sprite[nSprite].sectnum;
                SectFlag[nSector] |= kSectUnderwater;

                mydeletesprite(nSprite);
                return;
            }
            case 78:
            {
                AddFlow(nSprite, nSpeed, 1);

                short nSector = sprite[nSprite].sectnum;
                SectFlag[nSector] |= 0x8000;

                mydeletesprite(nSprite);
                return;
            }
            case 77:
            {
                int nArrow = BuildArrow(nSprite, nSpeed);
                if (!ConfigureKexTrap(nArrow, nChannel))
                    runlist_AddRunRec(sRunChannels[nChannel].a, nArrow, kRunTrap);
                return;
            }
            case 76: // Explosion Trigger (Exploding Fire Cauldron)
            {
                BuildObject(nSprite, 0, nHitag);
                return;
            }
            case 75: // Explosion Target (Cauldrons, fireballs and grenades will destroy nearby 75 sprites)
            {
                BuildObject(nSprite, 1, nHitag);
                return;
            }
            case 69:    // VibeBuild32: the remaster's boulder spawner
            {
                int nTrap = BuildBoulderTrap(nSprite, nSpeed);
                if (!ConfigureKexTrap(nTrap, nChannel))
                    runlist_AddRunRec(sRunChannels[nChannel].a, nTrap, kRunTrap);
                return;
            }
            case 71:
            {
                // VibeBuild32: in Kex mode the hitag is the channel only. The
                // wall-texture flip Build keys off it would otherwise catch a
                // switch on the same channel in the same room.
                int nFireball = BuildFireBall(nSprite, g_kexMode ? -1 : nHitag, nSpeed);
                if (!ConfigureKexTrap(nFireball, nChannel))
                    runlist_AddRunRec(sRunChannels[nChannel].a, nFireball, kRunTrap);
                return;
            }
            case 72:    // VibeBuild32: source-actor-ordered KEX trap stop record
            {
                KexTrapStop_Add(nSprite, nChannel);
                return;
            }
            case 70:
            {
                BuildDrip(nSprite);
                return;
            }
            case 73:    // VibeBuild32: this sector's mover holds where its tag put it
            case 74:    // VibeBuild32: a bomb wall's floor once blown (read at InitSectFlag)
            case 81:    // VibeBuild32: legacy drop-pad top marker (pre-kexpad output)
            {
                KexHold_Add(nSprite);
                return;
            }
            case 66:    // VibeBuild32: ACTOR_TRIGGER, fires when the pen's creatures are dead
            {
                KexTrigger_Add(nSprite, nChannel);
                return;
            }
            case 68:    // VibeBuild32: a KILENTRY shrine pad, hitag = the artifact
            {
                KexInv_AddShrinePad(nSprite, nHitag);
                return;
            }
            case 67:    // VibeBuild32: a SCRIPT sector, hitag = the script's tag
            {
                KexScript_Add(nSprite, nHitag);
                return;
            }
            case 65:    // VibeBuild32: the remaster's laser beam (hitag 1000 = always on)
            {
                KexBeam_Add(nSprite, nHitag >= 1000 ? -1 : nChannel);
                return;
            }
            case 64:    // VibeBuild32: a sector the remaster's level script moves
            {
                // xvel stores partner Build sector + 1; zero is the legacy
                // no-partner marker emitted by old/custom conversions.
                KexScripted_Add(sprite[nSprite].sectnum, nHitag,
                                sprite[nSprite].xvel - 1);
                mydeletesprite(nSprite);
                return;
            }
            case 84:    // VibeBuild32: the remaster's sinking floor trigger plate
            {
                KexPlate_Add(sprite[nSprite].sectnum, nHitag, sprite[nSprite].z);
                mydeletesprite(nSprite);
                return;
            }
            case 85:    // VibeBuild32: Kex switch off art, keyed by its raw event tag
            {
                KexSwitch_Add(sprite[nSprite].sectnum, nHitag, sprite[nSprite].xvel,
                              sprite[nSprite].yvel, sprite[nSprite].x, sprite[nSprite].y);
                mydeletesprite(nSprite);
                return;
            }
            case 86:    // VibeBuild32: Kex ARTIFACT_SPAWN exact floor-triangle destination
            {
                if (g_kexMode)
                    KexInv_AddShrineSpawn(nSprite, nHitag);
                else
                    mydeletesprite(nSprite);
                return;
            }
            case 87:    // VibeBuild32: Kex REMOTE_TRIGGER dispatcher
            {
                // hitag is the allotted Build channel; xvel preserves the
                // signed source tag and yvel says its source event is one-shot.
                if (g_kexMode)
                    KexRemote_Add(sprite[nSprite].sectnum, sprite[nSprite].xvel,
                                  nHitag, sprite[nSprite].yvel);
                mydeletesprite(nSprite);
                return;
            }
            case 63:
            {
                changespritestat(nSprite, 405);
                sprite[nSprite].cstat = 0x8000;
                return;
            }
            case 62:
            {
                nNetStartSprite[nNetStartSprites] = nSprite;
                sprite[nSprite].cstat = 0x8000;

                nNetStartSprites++;
                return;
            }
            case kTagRamses: // Ramses head
            {
                nSpiritSprite = nSprite;
                sprite[nSprite].cstat |= 0x8000;
                return;
            }
            default: // TODO - checkme!
            {
                mydeletesprite(nSprite);
                return;
            }
        }
    }

    mydeletesprite(nSprite);
}

void ExamineSprites()
{
    nNetStartSprites = 0;
    nCurStartSprite = 0;

    for (int nSprite = 0; nSprite < kMaxSprites; nSprite++)
    {
        int nStatus = sprite[nSprite].statnum;
        if (!nStatus)
        {
            if (!KexCheckpoint_RegisterSprite(nSprite))
            {
                mydeletesprite(nSprite);
                continue;
            }
            short lotag = sprite[nSprite].lotag;
            short hitag = sprite[nSprite].hitag;

            if ((nStatus < kMaxStatus) && lotag)
            {
                sprite[nSprite].lotag = 0;
                sprite[nSprite].hitag = 0;

                ProcessSpriteTag(nSprite, lotag, hitag);
            }
            else
            {
                changespritestat(nSprite, 0);
            }
        }
    }

    if (nNetPlayerCount)
    {
        int nSprite = insertsprite(initsect, 0);
        sprite[nSprite].x = initx;
        sprite[nSprite].y = inity;
        sprite[nSprite].z = initz;
        sprite[nSprite].cstat = 0x8000;
        nNetStartSprite[nNetStartSprites] = nSprite;
        nNetStartSprites++;
    }
}

void LoadObjects()
{
    runlist_InitRun();
    runlist_InitChan();
    InitLink();
    InitPoint();
    InitSlide();
    InitSwitch();
    InitElev();
    InitWallFace();
    InitTimeSlot();
    InitSectFlag();

    for (int nSector = 0; nSector < numsectors; nSector++)
    {
        short hitag = sector[nSector].hitag;
        short lotag = sector[nSector].lotag;

        sector[nSector].hitag = 0;
        sector[nSector].lotag = 0;
        sector[nSector].extra = -1;

        if (hitag || lotag)
        {
            sector[nSector].lotag = runlist_HeadRun() + 1;
            sector[nSector].hitag = lotag;

            runlist_ProcessSectorTag(nSector, lotag, hitag);
        }
    }

    for (int nWall = 0; nWall < numwalls; nWall++)
    {
        wall[nWall].extra = -1;

        short lotag = wall[nWall].lotag;
        short hitag = wall[nWall].hitag;

        wall[nWall].lotag = 0;

        if (hitag || lotag)
        {
            wall[nWall].lotag = runlist_HeadRun() + 1;
            runlist_ProcessWallTag(nWall, lotag, hitag);
        }
    }

    // Clear per-map KEX construction state before ExamineSprites() feeds it
    // shrine pads and checkpoint actors. This also distinguishes an ordinary
    // map transition from a pending checkpoint reload.
    KexInv_BeginLevel();
    ExamineSprites();
    PostProcess();

    // Constructor-level regression probe for the ordinary KEX enemies whose
    // native Build counterparts use different collision/renderer dimensions.
    // Run after every authored sprite has been constructed so it validates the
    // live level state, not merely constants in source.
    if (g_kexMode && Bgetenv("VB_KEX_COMMON_ENEMY_TEST"))
    {
        struct KexEnemyDimensions
        {
            int stat, clipdist, repeat;
            char const *name;
        };
        static KexEnemyDimensions const expected[] = {
            { 101, 72, 60, "Anubis" },
            { 102, 72, 60, "Mummy" },
            { 104, 72, 60, "Bastet" },
            { 103, 24, 40, "Fish" },
            {  99, 24, 40, "Spider/Scorpion" },
        };
        int count[ARRAY_SIZE(expected)] = {};
        int bad[ARRAY_SIZE(expected)] = {};
        for (unsigned kind = 0; kind < ARRAY_SIZE(expected); kind++)
            for (int nSprite = headspritestat[expected[kind].stat]; nSprite >= 0;
                 nSprite = nextspritestat[nSprite])
            {
                count[kind]++;
                bad[kind] += sprite[nSprite].clipdist != expected[kind].clipdist
                          || sprite[nSprite].xrepeat != expected[kind].repeat
                          || sprite[nSprite].yrepeat != expected[kind].repeat;
            }
        int total = 0, totalBad = 0;
        for (unsigned kind = 0; kind < ARRAY_SIZE(expected); kind++)
        {
            total += count[kind];
            totalBad += bad[kind];
        }
        bool const okay = total > 0 && totalBad == 0;
        OSD_Printf("Kex common enemy dimensions test: %s "
                   "(Anubis %d/%d, Mummy %d/%d, Bastet %d/%d, Fish %d/%d, "
                   "Spider/Scorpion %d/%d)\n",
                   okay ? "PASS" : "FAIL", count[0] - bad[0], count[0],
                   count[1] - bad[1], count[1], count[2] - bad[2], count[2],
                   count[3] - bad[3], count[3], count[4] - bad[4], count[4]);

        bool bastetCadence = seq_IsLoaded(kSeqLion);
        int const bastetSeq = SeqOffsets[kSeqLion];
        if (bastetCadence)
            for (int direction = 0; direction < 8; direction++)
                bastetCadence = bastetCadence
                    && SeqSize[bastetSeq + direction] == 12
                    && SeqSize[bastetSeq + 10 + direction] == 11
                    && SeqSize[bastetSeq + 44 + direction] == 17;
        bastetCadence = bastetCadence
            && SeqSize[bastetSeq + 8] == 41
            && SeqSize[bastetSeq + 9] == 23
            && SeqSize[bastetSeq + 54] == 1;

        bool fishCadence = seq_IsLoaded(kSeqFish);
        int const fishSeq = SeqOffsets[kSeqFish];
        if (fishCadence)
            for (int direction = 0; direction < 8; direction++)
                fishCadence = fishCadence
                    && SeqSize[fishSeq + direction] == 8
                    && SeqSize[fishSeq + 8 + direction] == 1
                    && SeqSize[fishSeq + 24 + direction] == 14;
        fishCadence = fishCadence && SeqSize[fishSeq + 48] == 1;
        OSD_Printf("Kex Bastet/Fish sequence lifecycle test: %s "
                   "(Bastet 12/11/17/41/23, Fish 8/1/14, delayed drops)\n",
                   bastetCadence && fishCadence ? "PASS" : "FAIL");

        // The converter replaces both Spider files with exact retail action
        // groups.  Check every directional slot, not only the callback-facing
        // first one, so a stale compressed SCORPION.SEQ cannot pass unnoticed.
        bool spiderCadence = seq_IsLoaded(kSeqSpider);
        bool scorpCadence = seq_IsLoaded(kSeqScorpion);
        int const spiderSeq = SeqOffsets[kSeqSpider];
        int const scorpSeq = SeqOffsets[kSeqScorpion];
        if (spiderCadence)
            for (int direction = 0; direction < 8; direction++)
                spiderCadence = spiderCadence
                    && SeqSize[spiderSeq + direction] == 6
                    && SeqSize[spiderSeq + 8 + direction] == 6
                    && SeqSize[spiderSeq + 16 + direction] == 6
                    && SeqSize[spiderSeq + 24 + direction] == 3
                    && SeqSize[spiderSeq + 32 + direction] == 14
                    && (direction == 0
                        ? SeqSize[spiderSeq + 40] == 4
                            && SeqSize[spiderSeq + 41] == 1
                        : SeqSize[spiderSeq + 40 + direction] == 1);
        if (scorpCadence)
            for (int direction = 0; direction < 8; direction++)
                scorpCadence = scorpCadence
                    && SeqSize[scorpSeq + direction] == 12
                    && SeqSize[scorpSeq + 8 + direction] == 12
                    && SeqSize[scorpSeq + 16 + direction] == 12
                    && SeqSize[scorpSeq + 24 + direction] == 2
                    && SeqSize[scorpSeq + 32 + direction] == 13
                    && (direction == 0
                        ? SeqSize[scorpSeq + 40] == 4
                            && SeqSize[scorpSeq + 41] == 1
                        : SeqSize[scorpSeq + 40 + direction] == 1);
        OSD_Printf("Kex Spider/Scorpion sequence cadence test: %s "
                   "(Spider 6/14/3/4, Scorpion 12/13/2/4, save identity)\n",
                   spiderCadence && scorpCadence && KexSpiderIdentityTest()
                       ? "PASS" : "FAIL");

        bool anubisCadence = seq_IsLoaded(kSeqAnubis);
        int const anubisSeq = SeqOffsets[kSeqAnubis];
        if (anubisCadence)
            for (int direction = 0; direction < 8; direction++)
                anubisCadence = anubisCadence
                    && SeqSize[anubisSeq + direction] == 1
                    && SeqSize[anubisSeq + 8 + direction] == 18
                    && SeqSize[anubisSeq + 16 + direction] == 24
                    && SeqSize[anubisSeq + 24 + direction] == 25
                    && SeqSize[anubisSeq + 32 + direction] == 10
                    && SeqSize[anubisSeq + 50 + direction] == 9;
        anubisCadence = anubisCadence
            && SeqSize[anubisSeq + 40] == 20
            && SeqSize[anubisSeq + 41] == 1
            && SeqSize[anubisSeq + 42] == 20
            && SeqSize[anubisSeq + 43] == 1;
        OSD_Printf("Kex Anubis sequence/death selector test: %s "
                   "(1/18/24/25/10/9/20/1, generic -8/-9 split)\n",
                   anubisCadence && KexAnubisGibSelectorTest()
                       ? "PASS" : "FAIL");

        bool mummyCadence = seq_IsLoaded(kSeqMummy);
        int const mummySeq = SeqOffsets[kSeqMummy];
        if (mummyCadence)
            for (int direction = 0; direction < 8; direction++)
                mummyCadence = mummyCadence
                    && SeqSize[mummySeq + direction] == 16
                    && SeqSize[mummySeq + 8 + direction] == 1
                    && SeqSize[mummySeq + 16 + direction] == 13
                    && SeqSize[mummySeq + 24 + direction] == 39
                    && SeqSize[mummySeq + 50 + direction] == 9;
        mummyCadence = mummyCadence
            && SeqSize[mummySeq + 32] == 35
            && SeqSize[mummySeq + 40] == 1;
        OSD_Printf("Kex Mummy sequence/death selector test: %s "
                   "(16/1/13/39/35/1/9, generic -10/-11 split)\n",
                   mummyCadence && KexMummyGibSelectorTest()
                       ? "PASS" : "FAIL");

        // Exercise the shared A_CheckMelee/A_MeleeAttack conversion at both
        // sides of its exact 3-D boundary. These temporary sprites never join
        // a controller or survive level construction.
        int const nSource = insertsprite(initsect, 0);
        int const nTarget = insertsprite(initsect, 0);
        bool rangeOkay = nSource >= 0 && nTarget >= 0;
        bool meleeKindsOkay = rangeOkay;
        if (rangeOkay)
        {
            sprite[nSource].x = sprite[nSource].y = sprite[nSource].z = 0;
            sprite[nSource].clipdist = 72; // authored radius 48
            sprite[nSource].cstat = 0x101;
            // AI source radius 48 + the player puppet's hardcoded 95.25 +
            // A_MeleeAttack reach 48, all at WORLD=6: 1147.5 Build units.
            sprite[nTarget].x = 1147;
            sprite[nTarget].y = sprite[nTarget].z = 0;
            sprite[nTarget].clipdist = 1;  // prove KEX uses authored player radius
            changespritestat(nTarget, 100);
            sprite[nTarget].cstat = 0x101;
            rangeOkay = KexMeleeRange(nSource, nTarget, 48);
            sprite[nTarget].x++;
            rangeOkay = rangeOkay && !KexMeleeRange(nSource, nTarget, 48);
            sprite[nTarget].x = 0;
            sprite[nTarget].z = 1147 * 16;
            rangeOkay = rangeOkay && KexMeleeRange(nSource, nTarget, 48);
            sprite[nTarget].z += 16;
            rangeOkay = rangeOkay && !KexMeleeRange(nSource, nTarget, 48);
            sprite[nTarget].z = 0;
            sprite[nTarget].cstat = 0;
            rangeOkay = rangeOkay && !KexMeleeRange(nSource, nTarget, 48);

            // The source has three deliberately different checks: generic
            // AI entry is sight/FOV aware, A_CheckMelee compares raw origins,
            // and flying A_MeleeAttack lifts the target by half its height.
            sprite[nTarget].cstat = 0x101;
            changespritestat(nSource, 101); // Anubis: forward-facing AI.
            sprite[nSource].x = initx;
            sprite[nSource].y = inity;
            sprite[nSource].z = initz;
            sprite[nTarget].x = initx + 128;
            sprite[nTarget].y = inity;
            sprite[nTarget].z = initz;
            sprite[nSource].ang = getangle(128, 0);
            meleeKindsOkay = KexAIEnterMeleeRange(nSource, nTarget, 0);
            sprite[nSource].ang = (sprite[nSource].ang + 339) & kAngleMask;
            meleeKindsOkay = meleeKindsOkay
                && !KexAIEnterMeleeRange(nSource, nTarget, 0);

            changespritestat(nSource, 103); // AI_Fish: flying.
            sprite[nSource].x = sprite[nSource].y = sprite[nSource].z = 0;
            sprite[nSource].clipdist = 24;
            sprite[nTarget].x = sprite[nTarget].y = 0;
            sprite[nTarget].z = 13000;
            meleeKindsOkay = meleeKindsOkay
                && !KexMeleeRange(nSource, nTarget, 16)
                && KexMeleeAttackRange(nSource, nTarget, 16);
        }
        if (nSource >= 0)
            mydeletesprite(nSource);
        if (nTarget >= 0)
            mydeletesprite(nTarget);
        OSD_Printf("Kex common enemy melee range test: %s "
                   "(XY/Z boundary, invalid target, AI entry and flying hit)\n",
                   rangeOkay && meleeKindsOkay ? "PASS" : "FAIL");

        // kexAI's inherited sightDistance is 3000 source units. Its strict
        // per-axis comparison is shared by every converted ordinary monster,
        // replacing the native controllers' inconsistent acquisition ranges.
        bool const sightOkay = KexAIWithinSightDistance(17999, 0)
            && !KexAIWithinSightDistance(18000, 0)
            && KexAIWithinSightDistance(0, -17999)
            && !KexAIWithinSightDistance(0, -18000);
        OSD_Printf("Kex common enemy sight range test: %s "
                   "(3000 KEX units, strict XY boundary)\n",
                   sightOkay ? "PASS" : "FAIL");

    }

    // The remaster's vases and barrels are shootable props; arm them once the
    // level's own objects are in place. No-op outside Kex mode.
    KexBreak_Init();
    KexDecor_Init();
    KexLight_Init();
    KexTele_Init();
    KexScripted_Init();
    KexFloat_Init();
    KexPad_Init();
    KexBeam_Init();
    KexTrigger_Init();
    KexScript_Init();
    KexNoStanding_Test();
    KexInv_LevelStart();
    InitRa();
    InitChunks();

    for (int nChannel = 0; nChannel < kMaxChannels; nChannel++)
    {
        runlist_ChangeChannel(nChannel, 0);
        runlist_ReadyChannel(nChannel);
    }
    KexCheckpoint_LevelReady();

    nCamerax = initx;
    nCameray = inity;
    nCameraz = initz;
}
