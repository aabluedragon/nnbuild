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
#include "keyboard.h"
#include "control.h"
#include "engine.h"
#include "palette.h"
#include "config.h"
#include "names.h"
#include "view.h"
#include "status.h"
#include "exhumed.h"
#include "ramses.h"
#include "player.h"
#include "snake.h"
#include "gun.h"
#include "light.h"
#include "init.h"
#include "menu.h"
#include "keyboard.h"
#include "cd.h"
#include "typedefs.h"
#include "map.h"
#include "move.h"
#include "sound.h"
#include "engine.h"
#include "save.h"
#include "trigdat.h"
#include "runlist.h"
#include "kexbreak.h"
#include "kexinv.h"

short bSubTitles = kTrue;

int zbob;

fix16_t nDestVertPan[kMaxPlayers] = { 0 };
short dVertPan[kMaxPlayers];
fix16_t nVertPan[kMaxPlayers];
int nCamerax;
int nCameray;
int nCameraz;

short bTouchFloor;

short nQuake[kMaxPlayers] = { 0 };

short nChunkTotal = 0;

short nViewTop;
short nViewBottom;
short nViewRight;
short nViewLeft;
short bClip = kFalse;
short bCamera = kFalse;

int gFov;
short nViewy;

short besttarget;
short enemy;
short nEnemyPal = 0;

#define MAXINTERPOLATIONS MAXSPRITES
int32_t g_interpolationCnt;
int32_t oldipos[MAXINTERPOLATIONS];
int32_t* curipos[MAXINTERPOLATIONS];
int32_t bakipos[MAXINTERPOLATIONS];

struct KexSpritePose
{
    int32_t x, y, z;
    int16_t ang, sectnum;
    uint8_t valid;
};

// KEX renders actor movement smoothly between its 30 Hz game updates. Keep a
// private previous pose per Build slot and apply it only to tsprite copies in
// analyzesprites(), so physics, AI, savegames and net state remain exact.
static KexSpritePose s_kexSpritePose[MAXSPRITES];
static int s_kexSpriteSmoothRatio = 65536;
static constexpr int kKexSpriteInterpolationMaxDelta = 8192;
static int s_kexSpriteInterpolationTest = -1;

int32_t nCameraDist = 0;
int32_t nCameraClock = 0;

void KexView_SpriteInserted(int16_t nSprite)
{
    if ((unsigned)nSprite < MAXSPRITES)
        s_kexSpritePose[nSprite].valid = 0;
}

void KexView_SpriteDeleted(int16_t nSprite)
{
    KexView_SpriteInserted(nSprite);
}

void KexView_SnapshotSprites(void)
{
    if (!g_kexMode)
        return;

    for (int nSprite = 0; nSprite < MAXSPRITES; nSprite++)
    {
        KexSpritePose &pose = s_kexSpritePose[nSprite];
        if (sprite[nSprite].statnum == MAXSTATUS)
        {
            pose.valid = 0;
            continue;
        }
        pose.x = sprite[nSprite].x;
        pose.y = sprite[nSprite].y;
        pose.z = sprite[nSprite].z;
        pose.ang = sprite[nSprite].ang;
        pose.sectnum = sprite[nSprite].sectnum;
        pose.valid = 1;
    }
}


int viewSetInterpolation(int32_t *const posptr)
{
    if (g_interpolationCnt >= MAXINTERPOLATIONS)
        return 1;

    for (bssize_t i = 0; i < g_interpolationCnt; ++i)
        if (curipos[i] == posptr)
            return 0;

    curipos[g_interpolationCnt] = posptr;
    oldipos[g_interpolationCnt] = *posptr;
    g_interpolationCnt++;
    return 0;
}

void viewStopInterpolation(const int32_t * const posptr)
{
    for (bssize_t i = 0; i < g_interpolationCnt; ++i)
        if (curipos[i] == posptr)
        {
            g_interpolationCnt--;
            oldipos[i] = oldipos[g_interpolationCnt];
            bakipos[i] = bakipos[g_interpolationCnt];
            curipos[i] = curipos[g_interpolationCnt];
        }
}

void viewDoInterpolations(int smoothRatio)
{
    int32_t ndelta = 0;

    for (bssize_t i = 0, j = 0; i < g_interpolationCnt; ++i)
    {
        int32_t const odelta = ndelta;
        bakipos[i] = *curipos[i];
        ndelta = (*curipos[i]) - oldipos[i];
        if (odelta != ndelta)
            j = mulscale16(ndelta, smoothRatio);
        *curipos[i] = oldipos[i] + j;
    }
}

void viewUpdateInterpolations(void)  //Stick at beginning of G_DoMoveThings
{
    for (bssize_t i=g_interpolationCnt-1; i>=0; i--) oldipos[i] = *curipos[i];
}

void viewRestoreInterpolations(void)  //Stick at end of drawscreen
{
    int32_t i=g_interpolationCnt-1;

    for (; i>=0; i--) *curipos[i] = bakipos[i];
}

void InitView()
{
#ifdef USE_OPENGL
    polymostcenterhoriz = 92;
#endif
}

static void analyzesprites();

// yax_drawrooms() wants a (x, y, z, ang, smoothratio) callback; Exhumed's
// sprite pass takes the view from globals, so the arguments go unused.
static void ExhumedYaxSpriteAnim(int32_t, int32_t, int32_t, int32_t, int32_t)
{
    analyzesprites();
}

// NOTE - not to be confused with Ken's analyzesprites()
static void analyzesprites()
{
    short nPlayerSprite = PlayerList[nLocalPlayer].nSprite;

    int var_38 = 20;
    int var_2C = 30000;

    spritetype *pPlayerSprite = &sprite[nPlayerSprite];

    besttarget = -1;

    int x = pPlayerSprite->x;
    int y = pPlayerSprite->y;

    int z = pPlayerSprite->z - (GetSpriteHeight(nPlayerSprite) / 2);

    short nSector = pPlayerSprite->sectnum;

    int nAngle = (2048 - pPlayerSprite->ang) & kAngleMask;

    int nTSprite;
    tspritetype *pTSprite;

//	int var_20 = var_24;

    for (nTSprite = spritesortcnt-1, pTSprite = &tsprite[nTSprite]; nTSprite >= 0; nTSprite--, pTSprite--)
    {
        int nSprite = pTSprite->owner;
        spritetype *pSprite = &sprite[nSprite];

        if (pTSprite->sectnum >= 0)
        {
            sectortype *pSector = &sector[pTSprite->sectnum];
            int nSectShade = (pSector->ceilingstat & 1) ? pSector->ceilingshade : pSector->floorshade;
            int nShade = pTSprite->shade + nSectShade + 6;
            pTSprite->shade = clamp(nShade, -128, 127);
        }

        pTSprite->pal = RemapPLU(pTSprite->pal);
        int const breakTile = KexBreak_ViewTile(nSprite);
        if (breakTile >= 0)
            pTSprite->picnum = (int16_t)breakTile;
        else
        {
            int const decorTile = KexDecor_ViewTile(nSprite);
            if (decorTile >= 0)
                pTSprite->picnum = (int16_t)decorTile;
        }
        int const ramsesTile = KexRamses_ViewTile(nSprite);
        if (ramsesTile >= 0)
        {
            // Materialise/vanish advance on the renderer's 120 Hz clock even
            // though gameplay scripts restart at 30 Hz, as in the remaster.
            pTSprite->picnum = (int16_t)ramsesTile;
            pTSprite->shade = -128;
        }
        if (KexFlash_Active(nSprite))       // the remaster's hit flash: white, fullbright
        {
            pTSprite->shade = -128;
            pTSprite->pal = kKexFlashPal;
        }

        // PowerSlaveGDX: Torch bouncing fix
        if ((pTSprite->picnum == kTile338 || pTSprite->picnum == kTile350) && (pTSprite->cstat & 0x80) == 0)
        {
            pTSprite->cstat |= 0x80;
            int nTileY = (tilesiz[pTSprite->picnum].y * pTSprite->yrepeat) * 2;
            pTSprite->z -= nTileY;
        }

        if (pSprite->statnum > 0)
        {
            runlist_SignalRun(pSprite->lotag - 1, nTSprite | 0x90000);

            if ((pSprite->statnum < 150) && (pSprite->cstat & 0x101) && (nSprite != nPlayerSprite))
            {
                int xval = pSprite->x - x;
                int yval = pSprite->y - y;

                int vcos = Cos(nAngle);
                int vsin = Sin(nAngle);

                int edx = ((vcos * yval) + (xval * vsin)) >> 14;
                int ebx = klabs(((vcos * xval) - (yval * vsin)) >> 14);

                if (!ebx)
                    continue;

                edx = (klabs(edx) * 32) / ebx;
                if (ebx < 1000 && ebx < var_2C && edx < 10)
                {
                    besttarget = nSprite;
                    var_38 = edx;
                    var_2C = ebx;
                }
                else if (ebx < 30000)
                {
                    int t = var_38 - edx;
                    if (t > 3 || (ebx < var_2C && klabs(t) < 5))
                    {
                        var_38 = edx;
                        var_2C = ebx;
                        besttarget = nSprite;
                    }
                }
            }
        }

        if (tilehasmodelorvoxel(pTSprite->picnum, pTSprite->pal) && !(spriteext[nSprite].flags & SPREXT_NOTMD))
        {
            int const nRootTile = pTSprite->picnum;
            int const nVoxel = tiletovox[pTSprite->picnum];

            if (nVoxel != -1 && ((voxflags[nVoxel] & VF_ROTATE) || (picanm[nRootTile].extra & 7) == 7))
                pTSprite->ang = (pTSprite->ang + ((int)totalclock << 3)) & kAngleMask;

            if (pTSprite->picnum == 736) // invincibility sprite needs to always face player
                pTSprite->ang = getangle(nCamerax - pTSprite->x, nCameray - pTSprite->y); // TODO - CHECKME: have to set pSprite not PTSprite here or angle won't change??
        }
    }
    if (besttarget != -1)
    {
        spritetype *pTarget = &sprite[besttarget];

        nCreepyTimer = kCreepyCount;

        if (!cansee(x, y, z, nSector, pTarget->x, pTarget->y, pTarget->z - GetSpriteHeight(besttarget), pTarget->sectnum))
        {
            besttarget = -1;
        }
    }

    if (s_kexSpriteInterpolationTest < 0)
        s_kexSpriteInterpolationTest = g_kexMode && Bgetenv("NNB_KEX_SPRITE_INTERP_TEST") ? 0 : -2;

    if (!g_kexMode || s_kexSpriteSmoothRatio >= 65536)
        return;

    for (int i = 0; i < spritesortcnt; i++)
    {
        tspritetype &t = tsprite[i];
        int const nSprite = t.owner;
        if ((unsigned)nSprite >= MAXSPRITES)
            continue;

        KexSpritePose &pose = s_kexSpritePose[nSprite];
        if (!pose.valid || pose.sectnum != t.sectnum)
            continue;

        // The opt-in probe advances a real renderer copy from a private pose.
        // It never writes sprite[] and is reset by the next simulation snapshot.
        if (s_kexSpriteInterpolationTest == 0)
        {
            pose.x = t.x - 256;
            pose.y = t.y;
            pose.z = t.z;
            pose.ang = t.ang;
            pose.sectnum = t.sectnum;
            s_kexSpriteInterpolationTest = 2;
        }

        int const dx = t.x - pose.x;
        int const dy = t.y - pose.y;
        int const dz = t.z - pose.z;
        // Teleports, sector changes and large scripted jumps must present at
        // their destination; blending them can draw through a closed room.
        if (klabs(dx) > kKexSpriteInterpolationMaxDelta
            || klabs(dy) > kKexSpriteInterpolationMaxDelta
            || klabs(dz) > kKexSpriteInterpolationMaxDelta)
            continue;

        t.x = pose.x + mulscale16(dx, s_kexSpriteSmoothRatio);
        t.y = pose.y + mulscale16(dy, s_kexSpriteSmoothRatio);
        t.z = pose.z + mulscale16(dz, s_kexSpriteSmoothRatio);
        t.ang = (int16_t)angle_interpolate16(pose.ang, t.ang, s_kexSpriteSmoothRatio);

        // Prove a real renderer copy, not its authoritative sprite, blended
        // at a fractional frame ratio. The test pose above is private state.
        if (s_kexSpriteInterpolationTest == 2 && (dx || dy || dz))
        {
            OSD_Printf("Kex sprite interpolation test: PASS (sprite %d, ratio %d, delta %d/%d/%d)\n",
                       nSprite, s_kexSpriteSmoothRatio, dx, dy, dz);
            s_kexSpriteInterpolationTest = 1;
        }
    }
}

void ResetView()
{
    videoSetGameMode(gSetup.fullscreen, gSetup.xdim, gSetup.ydim, gSetup.bpp, 0);
    DoOverscanSet(overscanindex);
    EraseScreen(overscanindex);
    memcpy(curpalettefaded, curpalette, sizeof(curpalette));
    videoUpdatePalette(0, 256);
#ifdef USE_OPENGL
    videoTintBlood(0, 0, 0);
#endif

    LoadStatus();
}

void SetView1()
{
}

void FlushMessageLine()
{
    int tileX = tilesiz[nBackgroundPic].x;
    int nTileOffset = 0;

    int xPos = 0;

    while (xPos < xdim)
    {
        overwritesprite(xPos, 0, nBackgroundPic + nTileOffset, -32, 0, kPalNormal);

        nTileOffset = nTileOffset == 0;

        xPos += tileX;
    }
}

void RefreshBackground()
{
    if (screensize <= 0)
        return;
    int nTileOffset = 0;
    int tileX = tilesiz[nBackgroundPic].x;
    int tileY = tilesiz[nBackgroundPic].y;

    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    MaskStatus();

    for (int y = 0; y < nViewTop; y += tileY)
    {
        nTileOffset = (y/tileY)&1;
        for (int x = 0; x < xdim; x += tileX)
        {
            rotatesprite(x<<16, y<<16, 65536L, 0, nBackgroundPic + nTileOffset, -32, kPalNormal, 8 + 16 + 64, 0, 0, xdim-1, nViewTop-1);
            nTileOffset ^= 1;
        }
    }
    for (int y = (nViewTop/tileY)*tileY; y <= nViewBottom; y += tileY)
    {
        nTileOffset = (y/tileY)&1;
        for (int x = 0; x < nViewLeft; x += tileX)
        {
            rotatesprite(x<<16, y<<16, 65536L, 0, nBackgroundPic + nTileOffset, -32, kPalNormal, 8 + 16 + 64, 0, nViewTop, nViewLeft-1, nViewBottom);
            nTileOffset ^= 1;
        }
    }
    for (int y = (nViewTop/tileY)*tileY; y <= nViewBottom; y += tileY)
    {
        nTileOffset = ((y/tileY)^((nViewRight+1)/tileX))&1;
        for (int x = ((nViewRight+1)/tileX)*tileX; x < xdim; x += tileX)
        {
            rotatesprite(x<<16, y<<16, 65536L, 0, nBackgroundPic + nTileOffset, -32, kPalNormal, 8 + 16 + 64, nViewRight+1, nViewTop, xdim-1, nViewBottom);
            nTileOffset ^= 1;
        }
    }
    for (int y = ((nViewBottom+1)/tileY)*tileY; y < ydim; y += tileY)
    {
        nTileOffset = (y/tileY)&1;
        for (int x = 0; x < xdim; x += tileX)
        {
            rotatesprite(x<<16, y<<16, 65536L, 0, nBackgroundPic + nTileOffset, -32, kPalNormal, 8 + 16 + 64, 0, nViewBottom+1, xdim-1, ydim-1);
            nTileOffset ^= 1;
        }
    }

    videoSetViewableArea(nViewLeft, nViewTop, nViewRight, nViewBottom);
}

void MySetView(int x1, int y1, int x2, int y2)
{
    if (!bFullScreen) {
        MaskStatus();
    }

    nViewLeft = x1;
    nViewTop = y1;
    nViewRight = x2;
    nViewBottom = y2;

    videoSetViewableArea(x1, y1, x2, y2);

    nViewy = y1;
}

// unused function
void TestLava()
{
}

static inline int interpolate16(int a, int b, int smooth)
{
    return a + mulscale16(b - a, smooth);
}

static inline fix16_t q16angle_interpolate16(fix16_t a, fix16_t b, int smooth)
{
    return a + mulscale16(((b+F16(1024)-a)&0x7FFFFFF)-F16(1024), smooth);
}

void viewDo3rdPerson(int nSprite, vec3_t* v, short* nSector, short nAngle, int horiz)
{
    spritetype* sp;
    int i, nx, ny, nz, hx, hy /*, hz*/;
    short bakcstat, daang;
    hitdata_t hitinfo;

    nx = Cos(nAngle + 1024) >> 4;
    ny = Sin(nAngle + 1024) >> 4;
    nz = (horiz - 100) * 128;

    sp = &sprite[nSprite];

    bakcstat = sp->cstat;
    sp->cstat &= (short)~0x101;

    updatesectorz(v->x, v->y, v->z, nSector);
    hitscan(v, *nSector, nx, ny, nz, &hitinfo, CLIPMASK1);

    hx = hitinfo.x - v->x;
    hy = hitinfo.y - v->y;

    if (klabs(nx) + klabs(ny) > klabs(hx) + klabs(hy))
    {
        *nSector = hitinfo.sect;
        if (hitinfo.wall >= 0)
        {
            daang = getangle(wall[wall[hitinfo.wall].point2].x - wall[hitinfo.wall].x,
                wall[wall[hitinfo.wall].point2].y - wall[hitinfo.wall].y);

            i = nx * sintable[daang] + ny * sintable[(daang + 1536) & 2047];
            if (klabs(nx) > klabs(ny)) {
                hx -= mulscale28(nx, i);
            }
            else {
                hy -= mulscale28(ny, i);
            }
        }
        else if (hitinfo.sprite < 0)
        {
            if (klabs(nx) > klabs(ny)) {
                hx -= (nx >> 5);
            }
            else {
                hy -= (ny >> 5);
            }
        }
        if (klabs(nx) > klabs(ny)) {
            i = divscale16(hx, nx);
        }
        else {
            i = divscale16(hy, ny);
        }
        if (i < nCameraDist) {
            nCameraDist = i;
        }
    }

    v->x = v->x + mulscale16(nx, nCameraDist);
    v->y = v->y + mulscale16(ny, nCameraDist);
    v->z = v->z + mulscale16(nz, nCameraDist);

    updatesectorz(v->x, v->y, v->z, nSector);

    sp->cstat = bakcstat;
}

void DrawView(int smoothRatio)
{
    s_kexSpriteSmoothRatio = g_kexMode ? clamp(smoothRatio, 0, 65536) : 65536;
    int playerX;
    int playerY;
    int playerZ;
    short nSector;
    fix16_t nAngle;
    fix16_t pan;

    fix16_t nCameraa;
    fix16_t nCamerapan;

    int viewz;

#if 0
    if (bgpages <= 0)
    {
        if (textpages > 0)
        {
            textpages--;
            FlushMessageLine();
        }
    }
    else
    {
        RefreshBackground();
        bgpages--;
    }
#else
    FlushMessageLine();
    RefreshBackground();
#endif

    if (!bFullScreen) {
        MaskStatus();
    }

    zbob = Sin(2 * bobangle) >> 3;

    int nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
    int nPlayerOldCstat = sprite[nPlayerSprite].cstat;
    int nDoppleOldCstat = sprite[nDoppleSprite[nLocalPlayer]].cstat;

    if (nSnakeCam >= 0)
    {
        int nSprite = SnakeList[nSnakeCam].nSprites[0];

        playerX = sprite[nSprite].x;
        playerY = sprite[nSprite].y;
        playerZ = sprite[nSprite].z;
        nSector = sprite[nSprite].sectnum;
        nAngle = fix16_from_int(sprite[nSprite].ang);

        SetGreenPal();
        UnMaskStatus();

        enemy = SnakeList[nSnakeCam].nEnemy;

        if (enemy <= -1 || totalmoves & 1)
        {
            nEnemyPal = -1;
        }
        else
        {
            nEnemyPal = sprite[enemy].pal;
            sprite[enemy].pal = 5;
        }
    }
    else
    {
        playerX = interpolate16(PlayerList[nLocalPlayer].opos.x, sprite[nPlayerSprite].x, smoothRatio);
        playerY = interpolate16(PlayerList[nLocalPlayer].opos.y, sprite[nPlayerSprite].y, smoothRatio);
        playerZ = interpolate16(PlayerList[nLocalPlayer].opos.z, sprite[nPlayerSprite].z, smoothRatio)
                + interpolate16(oeyelevel[nLocalPlayer], eyelevel[nLocalPlayer], smoothRatio);
        nSector = nPlayerViewSect[nLocalPlayer];
        nAngle = q16angle_interpolate16(PlayerList[nLocalPlayer].q16oangle, PlayerList[nLocalPlayer].q16angle, smoothRatio);

        if (!bCamera)
        {
            sprite[nPlayerSprite].cstat |= CSTAT_SPRITE_INVISIBLE;
            sprite[nDoppleSprite[nLocalPlayer]].cstat |= CSTAT_SPRITE_INVISIBLE;
        }
        else
        {
            sprite[nPlayerSprite].cstat |= CSTAT_SPRITE_TRANSLUCENT;
            sprite[nDoppleSprite[nLocalPlayer]].cstat |= CSTAT_SPRITE_INVISIBLE;
        }
    }

    nCameraa = nAngle;

    if (!bCamera || nFreeze)
    {
        if (nSnakeCam >= 0)
        {
            pan = F16(92);
            viewz = playerZ;
        }
        else
        {
            viewz = playerZ + nQuake[nLocalPlayer];
            int floorZ = sector[sprite[nPlayerSprite].sectnum].floorz;

            // pan = nVertPan[nLocalPlayer];
            pan = interpolate16(PlayerList[nLocalPlayer].q16ohoriz, PlayerList[nLocalPlayer].q16horiz, smoothRatio);

            if (viewz > floorZ)
                viewz = floorZ;

            nCameraa += fix16_from_int((nQuake[nLocalPlayer] >> 7) % 31);
            nCameraa += fix16_from_int(KexQuakeYaw());
            nCameraa &= 0x7FFFFFF;
        }
    }
    else
    {
        // code from above
        viewz = playerZ + nQuake[nLocalPlayer];
        int floorZ = sector[sprite[nPlayerSprite].sectnum].floorz;

        pan = interpolate16(PlayerList[nLocalPlayer].q16ohoriz, PlayerList[nLocalPlayer].q16horiz, smoothRatio);

        if (viewz > floorZ)
            viewz = floorZ;

        nCameraa += fix16_from_int((nQuake[nLocalPlayer] >> 7) % 31);
        nCameraa += fix16_from_int(KexQuakeYaw());
        nCameraa &= 0x7FFFFFF;

        vec3_t cpos;
        cpos.x = playerX;
        cpos.y = playerY;
        cpos.z = playerZ;

        viewDo3rdPerson(PlayerList[nLocalPlayer].nSprite, &cpos, &nSector, inita, fix16_to_int(pan));

        playerX = cpos.x;
        playerY = cpos.y;
        playerZ = cpos.z;
    }

    nCamerax = playerX;
    nCameray = playerY;
    nCameraz = playerZ;

    int Z = sector[nSector].ceilingz + 256;
    if (Z <= viewz)
    {
        Z = sector[nSector].floorz - 256;

        if (Z < viewz)
            viewz = Z;
    }
    else {
        viewz = Z;
    }

    nCamerapan = pan;

    if (nFreeze == 2 || nFreeze == 1)
    {
        nSnakeCam = -1;
        videoSetViewableArea(0, 0, xdim - 1, ydim - 1);
        UnMaskStatus();
    }

    UpdateMap();

    if (nFreeze != 3)
    {
        static uint8_t sectorFloorPal[MAXSECTORS];
        static uint8_t sectorCeilingPal[MAXSECTORS];
        static uint8_t wallPal[MAXWALLS];
        int const viewingRange = viewingrange;
        int const vr = Blrintf(65536.f * tanf(gFov * (fPI / 360.f)));

        if (r_usenewaspect)
        {
            newaspect_enable = 1;
            videoSetCorrectedAspect();
            renderSetAspect(mulscale16(vr, viewingrange), yxaspect);
        }
        else
            renderSetAspect(vr, yxaspect);

        if (HavePLURemap())
        {
            for (int i = 0; i < numsectors; i++)
            {
                sectorFloorPal[i] = sector[i].floorpal;
                sectorCeilingPal[i] = sector[i].ceilingpal;
                sector[i].floorpal = RemapPLU(sectorFloorPal[i]);
                sector[i].ceilingpal = RemapPLU(sectorCeilingPal[i]);
            }
            for (int i = 0; i < numwalls; i++)
            {
                wallPal[i] = wall[i].pal;
                wall[i].pal = RemapPLU(wallPal[i]);
            }
        }

        // True Room over Room. The remaster's maps stack rooms -- 300 floor and
        // ceiling faces across the 25 levels carry a vertical link, the same
        // way a wall face carries a horizontal one -- and the converter turns
        // those into TROR bunches. Both calls return immediately when the map
        // has no bunches, so the original PowerSlave levels are unaffected.
        //
        // The per-layer sprite pass is analyzesprites(), which walks tsprite[]
        // for whatever renderDrawRooms left in spritesortcnt -- the same shape
        // Duke's G_DoSpriteAnimations has. The main layer is done last, so
        // besttarget still ends up computed from the sectors the player is
        // actually standing in.
        KexWall_ViewBegin(nCamerax, nCameray, viewz);
        yax_preparedrawrooms();
        renderDrawRoomsQ16(nCamerax, nCameray, viewz, nCameraa, nCamerapan, nSector);
        yax_drawrooms(ExhumedYaxSpriteAnim, nSector, 0, smoothRatio);
        analyzesprites();
        renderDrawMasks();
        KexWall_ViewEnd();

        if (HavePLURemap())
        {
            for (int i = 0; i < numsectors; i++)
            {
                sector[i].floorpal = sectorFloorPal[i];
                sector[i].ceilingpal = sectorCeilingPal[i];
            }
            for (int i = 0; i < numwalls; i++)
            {
                wall[i].pal = wallPal[i];
            }
        }

        if (r_usenewaspect)
        {
            newaspect_enable = 0;
            renderSetAspect(viewingRange, tabledivide32_noinline(65536 * ydim * 8, xdim * 5));
        }

        if (nFreeze)
        {
            nSnakeCam = -1;

            if (nFreeze == 2)
            {
                if (nHeadStage == 4)
                {
                    nHeadStage = 5;

                    sprite[nPlayerSprite].cstat |= 0x8000;

                    int ang2 = fix16_to_int(nCameraa) - sprite[nPlayerSprite].ang;
                    if (ang2 < 0)
                        ang2 = -ang2;

                    if (ang2 > 10)
                    {
                        inita -= (ang2 >> 3);
                        return;
                    }

                    if (bSubTitles)
                    {
                        if (levelnum == 1)
                            ReadyCinemaText(1);
                        else
                            ReadyCinemaText(5);
                    }
                }
                else
                {
                    if ((bSubTitles && !AdvanceCinemaText()) || KB_KeyDown[sc_Escape] || KB_KeyDown[sc_Return] || KB_KeyDown[sc_Space])
                    {
                        levelnew = levelnum + 1;

                        if (CDplaying()) {
                            fadecdaudio();
                        }
                    }

                    videoSetViewableArea(nViewLeft, nViewTop, nViewRight, nViewBottom);
                }
            }
        }
        else
        {
            if (nSnakeCam < 0)
            {
                DrawWeapons(smoothRatio);
                float const crosshairSize = g_kexMode
                    ? clamp(kex_crosshair_size, 0.f, 3.f)
                    : (gShowCrosshair ? 1.f : 0.f);
                if (crosshairSize > 0.f) {
                    if (!waloff[kTile1579]) tileLoad(kTile1579);
                    int const scale = g_kexMode
                        ? max(1, (int)(65536.f * crosshairSize + .5f)) : 65536;
                    rotatesprite(160<<16, 92<<16, scale, 0, kTile1579, 0, 0, 1+2, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                }
                DrawMap();
                DrawStatus();
            }
            else
            {
                RestoreGreenPal();
                if (nEnemyPal > -1) {
                    sprite[enemy].pal = nEnemyPal;
                }

                DrawMap();

                if (!bFullScreen) {
                    MaskStatus();
                }

                DrawSnakeCamStatus();
            }
        }
    }
    else
    {
        videoClearScreen(overscanindex);
        DrawStatus();
    }

    sprite[nPlayerSprite].cstat = nPlayerOldCstat;
    sprite[nDoppleSprite[nLocalPlayer]].cstat = nDoppleOldCstat;

#ifdef USE_OPENGL
    // kexHud::Display adds these after every HUD element. The Kex bridge keeps
    // their source alpha independently of PCExhumed's damage/item palette
    // fades. Each native tick represents two source updates.
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        int const damageAlpha = KexHud_DamageFlashAlpha();
        if (damageAlpha > 0)
            fullscreen_tint_gl(255, 32, 32, (uint8_t)damageAlpha);
        int const pickupAlpha = KexHud_PickupFlashAlpha();
        if (pickupAlpha > 0)
            fullscreen_tint_gl(32, 32, 255, (uint8_t)pickupAlpha);
        int const shockAlpha = KexForce_ElectrocuteAlpha();
        if (shockAlpha > 0)
            fullscreen_tint_gl(32, 255, 32, (uint8_t)shockAlpha);
        int const teleAlpha = KexTele_FlashAlpha();
        if (teleAlpha > 0)
            fullscreen_tint_gl(0, 255, 8, (uint8_t)teleAlpha);
    }
#endif

    flash = 0;
}

void NoClip()
{
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    bClip = kFalse;
}

void Clip()
{
    videoSetViewableArea(nViewLeft, nViewTop, nViewRight, nViewBottom);
    if (!bFullScreen) {
        MaskStatus();
    }

    bClip = kTrue;
}

class ViewLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void ViewLoadSave::Load()
{
    Read(nDestVertPan, sizeof(nDestVertPan));
    Read(dVertPan, sizeof(dVertPan));
    Read(nVertPan, sizeof(nVertPan));
    Read(&nCamerax, sizeof(nCamerax));
    Read(&nCameray, sizeof(nCameray));
    Read(&nCameraz, sizeof(nCameraz));
    Read(&bTouchFloor, sizeof(bTouchFloor));
    Read(nQuake, sizeof(nQuake));
    Read(&nChunkTotal, sizeof(nChunkTotal));
    Read(&besttarget, sizeof(besttarget));

    // TODO - view vars?

    Read(&bCamera, sizeof(bCamera));
    Read(&enemy, sizeof(enemy));
    Read(&nEnemyPal, sizeof(nEnemyPal));
}

void ViewLoadSave::Save()
{
    Write(nDestVertPan, sizeof(nDestVertPan));
    Write(dVertPan, sizeof(dVertPan));
    Write(nVertPan, sizeof(nVertPan));
    Write(&nCamerax, sizeof(nCamerax));
    Write(&nCameray, sizeof(nCameray));
    Write(&nCameraz, sizeof(nCameraz));
    Write(&bTouchFloor, sizeof(bTouchFloor));
    Write(nQuake, sizeof(nQuake));
    Write(&nChunkTotal, sizeof(nChunkTotal));
    Write(&besttarget, sizeof(besttarget));

    // TODO - view vars?

    Write(&bCamera, sizeof(bCamera));
    Write(&enemy, sizeof(enemy));
    Write(&nEnemyPal, sizeof(nEnemyPal));
}

static ViewLoadSave* myLoadSave;

void ViewLoadSaveConstruct()
{
    myLoadSave = new ViewLoadSave();
}
