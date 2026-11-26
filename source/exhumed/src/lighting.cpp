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

#include "typedefs.h"
#include "lighting.h"
#include "player.h"
#include "engine.h"
#include "exhumed.h"
#include "sound.h"
#include "light.h"
#include "random.h"
#include "save.h"
#include "status.h"
#include <string.h>
#include <assert.h>

#define kMaxFlashes			2000
#define kMaxFlickerMask		25
#define kMaxGlows			50
#define kMaxFlickers		100
#define kMaxFlows			375

struct Flash
{
    char field_0;
    short field_1;
    int8_t shade;
};

struct Glow
{
    short field_0;
    short field_2;
    short nSector;
    short field_6;
};

struct Flicker
{
    short field_0;
    short nSector;
    unsigned int field_4;
};

struct Flow
{
    short field_0;
    short field_2;
    int field_4;
    int field_8;
    int field_C;
    int field_10;
    int field_14;
    int field_18;
};

Flash sFlash[kMaxFlashes];
Glow sGlow[kMaxGlows];
short nNextFlash[kMaxFlashes];
Flicker sFlicker[kMaxFlickers];
short nFreeFlash[kMaxFlashes];
Flow sFlowInfo[kMaxFlows];
int flickermask[kMaxFlickerMask];

short bTorch = 0;
short nFirstFlash = -1;
short nLastFlash = -1;
short nFlashDepth = 2;
short nFlashes;
short nFlowCount;
short nFlickerCount;
short nGlowCount;

int bDoFlicks = 0;
int bDoGlows = 0;


int GrabFlash()
{
    if (nFlashes >= kMaxFlashes) {
        return -1;
    }

    short nFlash = nFreeFlash[nFlashes];
    nNextFlash[nFlash] = -1;

    nFlashes++;

    if (nLastFlash <= -1)
    {
        nFirstFlash = nFlash;
    }
    else
    {
        nNextFlash[nLastFlash] = nFlash;
    }

    nLastFlash = nFlash;

    return nLastFlash;
}

// Record one surface's current shade in the flash list, then brighten it.
//
// Every light in this file -- AddFlash's explosions and Kex_SpriteGlow's fires
// alike -- does the same four things to a wall, floor, ceiling or sprite:
// take a flash slot, save the shade into it, mark the surface by pushing its
// pal past the palette so UndoFlashes() knows to restore it, and subtract the
// falloff from the shade. `nType` is the flash record's tag -- the low bits
// name the surface kind (1 floor, 2 wall, 3 ceiling, 4 sprite) and 0x80 asks
// for the shade to be put back -- and `nIndex` is the wall, sector or sprite
// it refers to. `add` is negative to brighten; a positive one still records
// the surface but leaves the shade alone, which is what AddFlash's sprite pass
// wants. Returns false when the flash list is full: there is nothing sensible
// left to do at that point, so callers give up on the rest of the sector.
static bool AddFlashRec(int nType, int nIndex, int8_t *pShade, uint8_t *pPal, int add)
{
    short const nFlash = GrabFlash();
    if (nFlash < 0)
        return false;

    sFlash[nFlash].field_0 = nType;
    sFlash[nFlash].field_1 = nIndex;
    sFlash[nFlash].shade   = *pShade;

    *pPal += 7;

    if (add < 0)
        *pShade = max(-127, *pShade + add);

    return true;
}

void InitLights()
{
    int i;
    nFlickerCount = 0;

    for (i = 0; i < kMaxFlickerMask; i++) {
        flickermask[i] = RandomSize(0x1F) * 2;
    }

    nGlowCount = 0;
    nFlowCount = 0;
    nFlashes  = 0;
    bDoFlicks = kFalse;
    bDoGlows  = kFalse;

    for (i = 0; i < kMaxFlashes; i++) {
        nFreeFlash[i] = i;
    }

    nFirstFlash = -1;
    nLastFlash  = -1;
}

void AddFlash(short nSector, int x, int y, int z, int val)
{
    assert(nSector >= 0 && nSector < kMaxSectors);

    int var_28 = 0;
    unsigned int var_1C = val >> 8;

    if (var_1C >= nFlashDepth) {
        return;
    }

    unsigned int var_20 = val & 0x80;
    unsigned int var_18 = val & 0x40;

    val = ((var_1C + 1) << 8) | (char)val;

    int var_14 = 0;

    short startwall = sector[nSector].wallptr;
    short endwall = sector[nSector].wallptr + sector[nSector].wallnum;

    for (int i = startwall; i < endwall; i++)
    {
        short wall2 = wall[i].point2;

        int xAverage = (wall[i].x + wall[wall2].x) / 2;
        int yAverage = (wall[i].y + wall[wall2].y) / 2;

        sectortype *pNextSector = NULL;
        if (wall[i].nextsector > -1) {
            pNextSector = &sector[wall[i].nextsector];
        }

        int ebx = -255;

        if (!var_18)
        {
            int x2 = x - xAverage;
            if (x2 < 0) {
                x2 = -x2;
            }

            ebx = x2;

            int y2 = y - yAverage;
            if (y2 < 0) {
                y2 = -y2;
            }

            ebx = ((y2 + ebx) >> 4) - 255;
        }

        if (ebx < 0)
        {
            var_14++;
            var_28 += ebx;

            if (wall[i].pal < 5)
            {
                if (!pNextSector || pNextSector->floorz < sector[nSector].floorz)
                {
                    if (!AddFlashRec(var_20 | 2, i, &wall[i].shade, &wall[i].pal, ebx))
                        return;

                    if (!var_1C && !wall[i].overpicnum && pNextSector)
                    {
                        AddFlash(wall[i].nextsector, x, y, z, val);
                    }
                }
            }
        }
    }

    if (var_14 && sector[nSector].floorpal < 4)
    {
        if (!AddFlashRec(var_20 | 1, nSector, &sector[nSector].floorshade,
                         &sector[nSector].floorpal, var_28))
            return;

        if (!(sector[nSector].ceilingstat & 1) && sector[nSector].ceilingpal < 4)
        {
            AddFlashRec(var_20 | 3, nSector, &sector[nSector].ceilingshade,
                        &sector[nSector].ceilingpal, var_28);
        }

        for (short nSprite = headspritesect[nSector]; nSprite >= 0; nSprite = nextspritesect[nSprite])
        {
            if (sprite[nSprite].pal < 4)
            {
                int eax = -255;

                if (!var_18)
                {
                    eax = ((klabs(x - sprite[nSprite].x) + klabs(y - sprite[nSprite].y)) >> 4) - 255;
                }

                AddFlashRec(var_20 | 4, nSprite, &sprite[nSprite].shade,
                            &sprite[nSprite].pal, eax);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The remaster's light sources
//
// Kex groups its fires, torches and lamps as "Light Sources" -- 19 actor types.
// They carry no lightRadius of their own, because in Kex the glow is baked into
// the vertex colours: on the wall behind TOMB's first fire pot the baked
// luminance runs 39 at the top to 255 at the bottom of one face. Build has a
// single shade per wall and cannot reproduce that, so the glow goes back in as
// a real light instead.
//
// This is AddFlash's sweep with the falloff opened up. AddFlash drives the
// shade 255 steps toward full bright at the sprite, which is right for an
// explosion and far too much for a pot of fire standing in a room, so the peak
// and the radius are parameters. The two differ in what reaches the flats as
// well: an explosion sums every wall's contribution, which saturates the floor
// of any sector it goes off in, while a fire hands them the brightest single
// wall so a big room stays dim at the far end. Sprites are left alone -- the
// fire lighting itself looks wrong. Everything touched is recorded in the same
// flash list, so UndoFlashes() puts it back at the top of the next tick and
// nothing accumulates.
// ---------------------------------------------------------------------------
void Kex_SpriteGlow(short nSector, int x, int y, int peak, int shift, int nDepth)
{
    if (nSector < 0 || nSector >= numsectors || nDepth > 1)
        return;

    int const startwall = sector[nSector].wallptr;
    int const endwall   = startwall + sector[nSector].wallnum;
    int lit = 0, best = 0;

    for (int i = startwall; i < endwall; i++)
    {
        int const w2 = wall[i].point2;
        int const mx = (wall[i].x + wall[w2].x) / 2;
        int const my = (wall[i].y + wall[w2].y) / 2;

        int const add = ((klabs(x - mx) + klabs(y - my)) >> shift) - peak;
        if (add >= 0)                          // out of range; negative brightens
            continue;

        lit++;
        if (add < best)
            best = add;

        if (wall[i].pal >= 5)                  // already lit by something else
            continue;

        if (!AddFlashRec(0x80 | 2, i, &wall[i].shade, &wall[i].pal, add))
            return;

        // Carry into the next room the way AddFlash does, so a fire in a
        // doorway lights both sides.
        if (wall[i].nextsector > -1 && !wall[i].overpicnum)
            Kex_SpriteGlow(wall[i].nextsector, x, y, peak, shift, nDepth + 1);
    }

    if (!lit)
        return;

    if (sector[nSector].floorpal < 4)
        AddFlashRec(0x80 | 1, nSector, &sector[nSector].floorshade,
                    &sector[nSector].floorpal, best);

    if (!(sector[nSector].ceilingstat & 1) && sector[nSector].ceilingpal < 4)
        AddFlashRec(0x80 | 3, nSector, &sector[nSector].ceilingshade,
                    &sector[nSector].ceilingpal, best);
}

// One tick of a remaster dynamic light. Unlike Kex_SpriteGlow's persistent
// scenery approximation, this record restores completely at the start of the
// next game tick; the owning animation casts it again with a smaller `peak`.
// Keeping radius separate from peak mirrors KEX's fixed-radius, color-fading
// dlight instead of shrinking the falloff by accident. KEX renders radius*4,
// and converted XY is six times retail units, so callers pass that final Build
// radius directly.
static void Kex_TransientGlowRec(short nSector, int x, int y, int peak,
                                 int radius, int nDepth)
{
    if (nSector < 0 || nSector >= numsectors || nDepth > 1
            || peak <= 0 || radius <= 0)
        return;

    int const startwall = sector[nSector].wallptr;
    int const endwall = startwall + sector[nSector].wallnum;
    int lit = 0, best = 0;

    for (int i = startwall; i < endwall; i++)
    {
        int const w2 = wall[i].point2;
        int const mx = (wall[i].x + wall[w2].x) / 2;
        int const my = (wall[i].y + wall[w2].y) / 2;
        int const distance = klabs(x - mx) + klabs(y - my);
        int const add = (int)(((int64_t)distance * 127) / radius) - peak;
        if (add >= 0)
            continue;

        lit++;
        if (add < best)
            best = add;

        if (wall[i].pal >= 5)
            continue;
        if (!AddFlashRec(2, i, &wall[i].shade, &wall[i].pal, add))
            return;

        if (wall[i].nextsector > -1 && !wall[i].overpicnum)
            Kex_TransientGlowRec(wall[i].nextsector, x, y, peak, radius,
                                 nDepth + 1);
    }

    if (!lit)
        return;
    if (sector[nSector].floorpal < 4)
        AddFlashRec(1, nSector, &sector[nSector].floorshade,
                    &sector[nSector].floorpal, best);
    if (!(sector[nSector].ceilingstat & 1) && sector[nSector].ceilingpal < 4)
        AddFlashRec(3, nSector, &sector[nSector].ceilingshade,
                    &sector[nSector].ceilingpal, best);
}

void Kex_TransientGlow(short nSector, int x, int y, int peak, int radius)
{
    Kex_TransientGlowRec(nSector, x, y, peak, radius, 0);
}

void UndoFlashes()
{
    if (!nFlashes) {
        return;
    }

    int var_24 = 0; // CHECKME - Watcom error "initializer for variable var_24 may not execute

    int edi = -1;

    for (short nFlash = nFirstFlash; nFlash >= 0; nFlash = nNextFlash[nFlash])
    {
        assert(nFlash < 2000 && nFlash >= 0);

        uint8_t var_28 = sFlash[nFlash].field_0 & 0x3F;
        short nIndex = sFlash[nFlash].field_1;

        if (sFlash[nFlash].field_0 & 0x80)
        {
            int var_20 = var_28 - 1;
            assert(var_20 >= 0);

            int8_t *pShade = NULL;

            switch (var_20)
            {
                case 0:
                {
                    assert(nIndex >= 0 && nIndex < kMaxSectors);

                    pShade = &sector[nIndex].floorshade;
                    break;
                }

                case 1:
                {
                    assert(nIndex >= 0 && nIndex < kMaxWalls);

                    pShade = &wall[nIndex].shade;
                    break;
                }

                case 2:
                {
                    assert(nIndex >= 0 && nIndex < kMaxSectors);

                    pShade = &sector[nIndex].ceilingshade;
                    break;
                }

                case 3:
                {
                    assert(nIndex >= 0 && nIndex < kMaxSprites);

                    if (sprite[nIndex].pal >= 7)
                    {
                        pShade = &sprite[nIndex].shade;
                    }
                    else {
                        goto loc_1868A;
                    }

                    break;
                }

                default:
                    break;
            }

            assert(pShade != NULL);

            short var_2C = (*pShade) + 6;
            int var_30 = sFlash[nFlash].shade;

            if (var_2C < var_30)
            {
                *pShade = var_2C;
                edi = nFlash;
                continue;
            }
        }

        // loc_185FE
        var_24 = var_28 - 1; // CHECKME - Watcom error "initializer for variable var_24 may not execute
        assert(var_24 >= 0);

        switch (var_24)
        {
            default:
                break;

            case 0:
            {
                sector[nIndex].floorpal -= 7;
                sector[nIndex].floorshade = sFlash[nFlash].shade;
                break;
            }

            case 1:
            {
                wall[nIndex].pal -= 7;
                wall[nIndex].shade = sFlash[nFlash].shade;
                break;
            }

            case 2:
            {
                sector[nIndex].ceilingpal -= 7;
                sector[nIndex].ceilingshade = sFlash[nFlash].shade;
                break;
            }

            case 3:
            {
                if (sprite[nIndex].pal >= 7)
                {
                    sprite[nIndex].pal -= 7;
                    sprite[nIndex].shade = sFlash[nFlash].shade;
                }

                break;
            }
        }

loc_1868A:

        nFlashes--;
        assert(nFlashes >= 0);

        nFreeFlash[nFlashes] = nFlash;

        if (edi != -1)
        {
            nNextFlash[edi] = nNextFlash[nFlash];
        }

        if (nFlash == nFirstFlash)
        {
            nFirstFlash = nNextFlash[nFirstFlash];
        }

        if (nFlash == nLastFlash)
        {
            nLastFlash = edi;
        }
    }
}

void AddGlow(short nSector, int nVal)
{
    if (nGlowCount >= kMaxGlows) {
        return;
    }

    sGlow[nGlowCount].field_6 = nVal;
    sGlow[nGlowCount].nSector = nSector;
    sGlow[nGlowCount].field_0 = -1;
    sGlow[nGlowCount].field_2 = 0;

    nGlowCount++;
}

void AddFlicker(short nSector, int nVal)
{
    if (nFlickerCount >= kMaxFlickers) {
        return;
    }

    sFlicker[nFlickerCount].field_0 = nVal;
    sFlicker[nFlickerCount].nSector = nSector;

    if (nVal >= 25) {
        nVal = 24;
    }

    sFlicker[nFlickerCount].field_4 = flickermask[nVal];

    nFlickerCount++;
}

void DoGlows()
{
    bDoGlows++;

    if (bDoGlows < 3) {
        return;
    }

    bDoGlows = 0;

    for (int i = 0; i < nGlowCount; i++)
    {
        sGlow[i].field_2++;

        short nSector = sGlow[i].nSector;
        short nShade = sGlow[i].field_0;

        if (sGlow[i].field_2 >= sGlow[i].field_6)
        {
            sGlow[i].field_2 = 0;
            sGlow[i].field_0 = -sGlow[i].field_0;
        }

        sector[nSector].ceilingshade += nShade;
        sector[nSector].floorshade   += nShade;

        int startwall = sector[nSector].wallptr;
        int endwall = startwall + sector[nSector].wallnum - 1;

        for (int nWall = startwall; nWall <= endwall; nWall++)
        {
            wall[nWall].shade += nShade;

            // CHECKME - ASM has edx decreasing here. why?
        }
    }
}

void DoFlickers()
{
    bDoFlicks ^= 1;
    if (!bDoFlicks) {
        return;
    }

    for (int i = 0; i < nFlickerCount; i++)
    {
        short nSector = sFlicker[i].nSector;

        unsigned int eax = (sFlicker[i].field_4 & 1);
        unsigned int edx = (sFlicker[i].field_4 & 1) << 31;
        unsigned int ebp = sFlicker[i].field_4 >> 1;

        ebp |= edx;
        edx = ebp & 1;

        sFlicker[i].field_4 = ebp;

        if (edx ^ eax)
        {
            short shade;

            if (eax)
            {
                shade = sFlicker[i].field_0;
            }
            else
            {
                shade = -sFlicker[i].field_0;
            }

            sector[nSector].ceilingshade += shade;
            sector[nSector].floorshade += shade;

            int startwall = sector[nSector].wallptr;
            int endwall = startwall + sector[nSector].wallnum - 1;

            for (int nWall = endwall; nWall >= startwall; nWall--)
            {
                wall[nWall].shade += shade;

                // CHECKME - ASM has edx decreasing here. why?
            }
        }
    }
}

// nWall can also be passed in here via nSprite parameter - TODO - rename nSprite parameter :)
void AddFlow(int nSprite, int nSpeed, int b)
{
    if (nFlowCount >= kMaxFlows)
        return;

    short nFlow = nFlowCount;
    nFlowCount++;

    short var_18;

    if (b < 2)
    {
        var_18 = sprite[nSprite].sectnum;
        short nPic = sector[var_18].floorpicnum;
        short nAngle = sprite[nSprite].ang;

        sFlowInfo[nFlow].field_14 = (tilesiz[nPic].x << 14) - 1;
        sFlowInfo[nFlow].field_18 = (tilesiz[nPic].y << 14) - 1;
        sFlowInfo[nFlow].field_C  = -Cos(nAngle) * nSpeed;
        sFlowInfo[nFlow].field_10 = Sin(nAngle) * nSpeed;
    }
    else
    {
        short nAngle;

        if (b == 2) {
            nAngle = 512;
        }
        else {
            nAngle = 1536;
        }

        var_18 = nSprite;
        short nPic = wall[var_18].picnum;

        sFlowInfo[nFlow].field_14 = (tilesiz[nPic].x * wall[var_18].xrepeat) << 8;
        sFlowInfo[nFlow].field_18 = (tilesiz[nPic].y * wall[var_18].yrepeat) << 8;
        sFlowInfo[nFlow].field_C = -Cos(nAngle) * nSpeed;
        sFlowInfo[nFlow].field_10 = Sin(nAngle) * nSpeed;
    }

    sFlowInfo[nFlow].field_8 = 0;
    sFlowInfo[nFlow].field_4 = 0;
    sFlowInfo[nFlow].field_0 = var_18;
    sFlowInfo[nFlow].field_2 = b;
}

void DoFlows()
{
    for (int i = 0; i < nFlowCount; i++)
    {
        sFlowInfo[i].field_4 += sFlowInfo[i].field_C;
        sFlowInfo[i].field_8 += sFlowInfo[i].field_10;

        switch (sFlowInfo[i].field_2)
        {
            case 0:
            {
                sFlowInfo[i].field_4 &= sFlowInfo[i].field_14;
                sFlowInfo[i].field_8 &= sFlowInfo[i].field_18;

                short nSector = sFlowInfo[i].field_0;
                sector[nSector].floorxpanning = sFlowInfo[i].field_4 >> 14;
                sector[nSector].floorypanning = sFlowInfo[i].field_8 >> 14;
                break;
            }

            case 1:
            {
                short nSector = sFlowInfo[i].field_0;

                sector[nSector].ceilingxpanning = sFlowInfo[i].field_4 >> 14;
                sector[nSector].ceilingypanning = sFlowInfo[i].field_8 >> 14;

                sFlowInfo[i].field_4 &= sFlowInfo[i].field_14;
                sFlowInfo[i].field_8 &= sFlowInfo[i].field_18;
                break;
            }

            case 2:
            {
                short nWall = sFlowInfo[i].field_0;

                wall[nWall].xpanning = sFlowInfo[i].field_4 >> 14;
                wall[nWall].ypanning = sFlowInfo[i].field_8 >> 14;

                if (sFlowInfo[i].field_4 < 0)
                {
                    sFlowInfo[i].field_4 += sFlowInfo[i].field_14;
                }

                if (sFlowInfo[i].field_8 < 0)
                {
                    sFlowInfo[i].field_8 += sFlowInfo[i].field_18;
                }

                break;
            }

            case 3:
            {
                short nWall = sFlowInfo[i].field_0;

                wall[nWall].xpanning = sFlowInfo[i].field_4 >> 14;
                wall[nWall].ypanning = sFlowInfo[i].field_8 >> 14;

                if (sFlowInfo[i].field_4 >= sFlowInfo[i].field_14)
                {
                    sFlowInfo[i].field_4 -= sFlowInfo[i].field_14;
                }

                if (sFlowInfo[i].field_8 >= sFlowInfo[i].field_18)
                {
                    sFlowInfo[i].field_8 -= sFlowInfo[i].field_18;
                }

                break;
            }
        }
    }
}

void DoLights()
{
    DoFlickers();
    DoGlows();
    DoFlows();
}

void SetTorch(int nPlayer, int bTorchOnOff)
{
    char buf[40];

    if (bTorchOnOff == bTorch) {
        return;
    }

    if (nPlayer != nLocalPlayer) {
        return;
    }

    // char *pTempPal = origpalookup[kPalTorch];
    // palookup[kPalTorch] = palookup[kPalNoTorch];
    // palookup[kPalNoTorch] = pTempPal;
    //
    // pTempPal = origpalookup[kPalTorch];
    // origpalookup[kPalTorch] = origpalookup[kPalNoTorch];
    // origpalookup[kPalNoTorch] = pTempPal;
    //
    // pTempPal = origpalookup[kPalTorch2];
    // origpalookup[kPalTorch2] = origpalookup[kPalNoTorch2];
    // origpalookup[kPalNoTorch2] = pTempPal;
    //
    // pTempPal = palookup[kPalTorch2];
    // palookup[kPalNoTorch2] = palookup[kPalTorch2];
    // palookup[kPalTorch2] = pTempPal;

    if (bTorchOnOff == 2) {
        bTorch = !bTorch;
    }
    else {
        bTorch = bTorchOnOff;
    }

    if (bTorch) {
        PlayLocalSound(kSoundTorchOn, 0);
    }

    strcpy(buf, "TORCH IS ");

    if (bTorch) {
        strcat(buf, "LIT");
    }
    else {
        strcat(buf, "OUT");
    }

    StatusMessage(150, buf);
}

void BuildFlash(short nPlayer, short UNUSED(nSector), int nVal)
{
    if (nPlayer == nLocalPlayer)
    {
        flash = nVal;
        flash = -nVal; // ???
    }
}

class LightingLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void LightingLoadSave::Load()
{
    Read(&bTorch, sizeof(bTorch));
    Read(&nFirstFlash, sizeof(nFirstFlash));
    Read(&nLastFlash, sizeof(nLastFlash));
    Read(&nFlashDepth, sizeof(nFlashDepth));
    Read(&nFlashes, sizeof(nFlashes));
    Read(&nFlowCount, sizeof(nFlowCount));
    Read(&nFlickerCount, sizeof(nFlickerCount));
    Read(&nGlowCount, sizeof(nGlowCount));
    Read(&bDoFlicks, sizeof(bDoFlicks));
    Read(&bDoGlows, sizeof(bDoGlows));

    Read(sFlash, sizeof(sFlash));
    Read(sGlow, sizeof(sGlow));
    Read(nNextFlash, sizeof(nNextFlash));
    Read(sFlicker, sizeof(sFlicker));
    Read(nFreeFlash, sizeof(nFreeFlash));
    Read(sFlowInfo, sizeof(sFlowInfo));
    Read(flickermask, sizeof(flickermask));
}

void LightingLoadSave::Save()
{
    Write(&bTorch, sizeof(bTorch));
    Write(&nFirstFlash, sizeof(nFirstFlash));
    Write(&nLastFlash, sizeof(nLastFlash));
    Write(&nFlashDepth, sizeof(nFlashDepth));
    Write(&nFlashes, sizeof(nFlashes));
    Write(&nFlowCount, sizeof(nFlowCount));
    Write(&nFlickerCount, sizeof(nFlickerCount));
    Write(&nGlowCount, sizeof(nGlowCount));
    Write(&bDoFlicks, sizeof(bDoFlicks));
    Write(&bDoGlows, sizeof(bDoGlows));

    Write(sFlash, sizeof(sFlash));
    Write(sGlow, sizeof(sGlow));
    Write(nNextFlash, sizeof(nNextFlash));
    Write(sFlicker, sizeof(sFlicker));
    Write(nFreeFlash, sizeof(nFreeFlash));
    Write(sFlowInfo, sizeof(sFlowInfo));
    Write(flickermask, sizeof(flickermask));
}

static LightingLoadSave* myLoadSave;

void LightingLoadSaveConstruct()
{
    myLoadSave = new LightingLoadSave();
}
