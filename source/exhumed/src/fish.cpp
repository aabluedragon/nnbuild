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

#include "fish.h"
#include "anims.h"
#include "engine.h"
#include "sequence.h"
#include "random.h"
#include "runlist.h"
#include "exhumed.h"
#include "move.h"
#include "trigdat.h"
#include "init.h"
#include "sound.h"
#include "save.h"
#include "kexinv.h"
#include "kexbreak.h" // NNBuild: KEX fish attack sound
#include <assert.h>

#define kMaxFishes  128
#define kMaxChunks  128

static actionSeq ActionSeq[] = {
    {8,  0},
    {8,  0},
    {0,  0},
    {24, 0},
    {8,  0},
    {32, 1},
    {33, 1},
    {34, 1},
    {35, 1},
    {39, 1}
};

struct Fish
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short field_A;
    short field_C;
    short nRun;
};

struct Chunk
{
    short nSprite;
    short field_2;
    short field_4;
    short field_6;
};

short FishCount = 0;
short nChunksFree = 0;
int nFreeChunk[kMaxChunks] = {0};
Fish FishList[kMaxFishes];
Chunk FishChunk[kMaxChunks];


void InitFishes()
{
    FishCount = 0;
    nChunksFree = kMaxChunks;

    for (int i = 0; i < kMaxChunks; i++) {
        nFreeChunk[i] = i;
    }
}

int BuildFishLimb(short nFish, short edx)
{
    if (nChunksFree <= 0) {
        return -1;
    }

    short nSprite = FishList[nFish].nSprite;

    nChunksFree--;

    int nFree = nFreeChunk[nChunksFree];

    int nSprite2 = insertsprite(sprite[nSprite].sectnum, 99);
    assert(nSprite2 >= 0 && nSprite2 < kMaxSprites);

    FishChunk[nFree].nSprite = nSprite2;
    FishChunk[nFree].field_4 = edx + 40;
    FishChunk[nFree].field_2 = RandomSize(3) % SeqSize[SeqOffsets[kSeqFish] + edx + 40];

    sprite[nSprite2].x = sprite[nSprite].x;
    sprite[nSprite2].y = sprite[nSprite].y;
    sprite[nSprite2].z = sprite[nSprite].z;
    sprite[nSprite2].cstat = 0;
    sprite[nSprite2].shade = -12;
    sprite[nSprite2].pal = 0;
    sprite[nSprite2].xvel = (RandomSize(5) - 16) << 8;
    sprite[nSprite2].yvel = (RandomSize(5) - 16) << 8;
    sprite[nSprite2].xrepeat = 64;
    sprite[nSprite2].yrepeat = 64;
    sprite[nSprite2].xoffset = 0;
    sprite[nSprite2].yoffset = 0;
    sprite[nSprite2].zvel = (-(RandomByte() + 512)) * 2;

    seq_GetSeqPicnum(kSeqFish, FishChunk[nFree].field_4, 0);

    sprite[nSprite2].picnum = edx;
    sprite[nSprite2].lotag = runlist_HeadRun() + 1;
    sprite[nSprite2].clipdist = 0;

//	GrabTimeSlot(3);

    sprite[nSprite2].extra = -1;
    sprite[nSprite2].owner = runlist_AddRunRec(sprite[nSprite2].lotag - 1, nFree, kRunFishLimb);
    sprite[nSprite2].hitag = runlist_AddRunRec(NewRun, nFree, kRunFishLimb);

    return nFree | 0x200000;
}

void BuildBlood(int x, int y, int z, short nSector)
{
    BuildAnim(-1, kSeqFish, 36, x, y, z, nSector, 75, 128);
}

void FuncFishLimb(int a, int UNUSED(nDamage), int nRun)
{
    short nFish = RunData[nRun].nVal;
    short nSprite = FishChunk[nFish].nSprite;
    assert(nSprite >= 0 && nSprite < kMaxSprites);

    int nSeq = SeqOffsets[kSeqFish] + FishChunk[nFish].field_4;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x20000:
        {
            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, FishChunk[nFish].field_2);

            Gravity(nSprite);

            FishChunk[nFish].field_2++;

            if (FishChunk[nFish].field_2 >= SeqSize[nSeq])
            {
                FishChunk[nFish].field_2 = 0;
                if (RandomBit()) {
                    BuildBlood(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum);
                }
            }

            int FloorZ = sector[sprite[nSprite].sectnum].floorz;

            if (FloorZ <= sprite[nSprite].z)
            {
                sprite[nSprite].z += 256;

                if ((sprite[nSprite].z - FloorZ) > 25600)
                {
                    sprite[nSprite].zvel = 0;
                    runlist_DoSubRunRec(sprite[nSprite].owner);
                    runlist_FreeRun(sprite[nSprite].lotag - 1);
                    runlist_SubRunRec(sprite[nSprite].hitag);
                    mydeletesprite(nSprite);
                }
                else if ((sprite[nSprite].z - FloorZ) > 0)
                {
                    sprite[nSprite].zvel = 1024;
                }

                return;
            }
            else
            {
                if (movesprite(nSprite, sprite[nSprite].xvel << 8, sprite[nSprite].yvel << 8, sprite[nSprite].zvel, 2560, -2560, CLIPMASK1))
                {
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                }
            }

            return;
        }

        case 0x90000:
        {
            seq_PlotSequence(a & 0xFFFF, nSeq, FishChunk[nFish].field_2, 1);
            return;
        }
    }
}

int BuildFish(int nSprite, int x, int y, int z, int nSector, int nAngle)
{
    short nFish = FishCount;
    FishCount++;

    if (nFish >= kMaxFishes) {
        return -1;
    }

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 103);
    }
    else
    {
        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sprite[nSprite].z;
        nAngle = sprite[nSprite].ang;
        changespritestat(nSprite, 103);
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].shade = -12;
    // AI_Fish radius 16 -> Build clipdist 16*WORLD(6)/4 = 24. The original
    // game's much larger collision circle remains untouched outside KEX mode.
    sprite[nSprite].clipdist = g_kexMode ? 24 : 80;
    sprite[nSprite].xrepeat = 40;
    sprite[nSprite].yrepeat = 40;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = seq_GetSeqPicnum(kSeqFish, ActionSeq[0].a, 0);
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].extra = -1;

//	GrabTimeSlot(3);

    FishList[nFish].nAction = 0;
    // AI_Fish has 40 health in KEX; the classic fish remains at 200.
    FishList[nFish].nHealth = g_kexMode ? KexHealthUnits(40) : 200;
    FishList[nFish].nSprite = nSprite;
    FishList[nFish].nTarget = -1;
    FishList[nFish].field_C = 60;
    FishList[nFish].nFrame = 0;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nFish, kRunFish);
    FishList[nFish].nRun = runlist_AddRunRec(NewRun, nFish, kRunFish);

    nCreaturesLeft++;

    return nFish | 0x120000;
}

void IdleFish(short nFish, short edx)
{
    short nSprite = FishList[nFish].nSprite;

    sprite[nSprite].ang += (256 - RandomSize(9)) + 1024;
    sprite[nSprite].ang &= kAngleMask;

    sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 8;
    sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 8;

    FishList[nFish].nAction = 0;
    FishList[nFish].nFrame = 0;

    sprite[nSprite].zvel = RandomSize(9);

    if (!edx)
    {
        if (RandomBit()) {
            sprite[nSprite].zvel = -sprite[nSprite].zvel;
        }
    }
    else if (edx < 0)
    {
        sprite[nSprite].zvel = -sprite[nSprite].zvel;
    }
}

void DestroyFish(short nFish)
{
    short nSprite = FishList[nFish].nSprite;

    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(FishList[nFish].nRun);
    mydeletesprite(nSprite);
}

static bool KexFishExactSequence()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqFish))
        return false;

    int const nBase = SeqOffsets[kSeqFish];
    return SeqSize[nBase] == 8
        && SeqSize[nBase + 8] == 1
        && SeqSize[nBase + 24] == 14
        && SeqSize[nBase + 48] == 1;
}

static void KexFishTossGibs(short nSprite)
{
    // fish_death.tsc tosses three gibs1, two gibs2 and two gibs3, then
    // destroys the host actor.
    static const int gibTiles[] = { 7820, 7820, 7820, 7821, 7821, 7822, 7822 };

    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);

    int const sound = KexSound_Get("fishdies");
    if (sound >= 0)
        PlayLocalSound(sound, 0);
}

static void KexFishDestroy(short nFish, short nSprite)
{
    sprite[nSprite].cstat = 0x8000;
    DestroyFish(nFish);
    FishList[nFish].nSprite = -1;
}

static void KexFishImmediateDeath(short nFish, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexFishTossGibs(nSprite);
    KexFishDestroy(nFish, nSprite);
}

static void KexFishBeginGibTimer(short nFish, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexFishTossGibs(nSprite);
    FishList[nFish].nAction = 9;
    FishList[nFish].nFrame = 0;
    FishList[nFish].nTarget = nKiller;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
}

int KexFishOwnsDeathDrop(int nSprite)
{
    if (!g_kexMode || !KexFishExactSequence())
        return 0;

    for (int nFish = 0; nFish < FishCount; nFish++)
        if (FishList[nFish].nSprite == nSprite && FishList[nFish].nAction == 9)
            return 1;
    return 0;
}

void FuncFish(int a, int nDamage, int nRun)
{
    short nFish = RunData[nRun].nVal;
    assert(nFish >= 0 && nFish < kMaxFishes);

    short nSprite = FishList[nFish].nSprite;
    short nAction = FishList[nFish].nAction;
    if ((unsigned)nAction >= ARRAY_SIZE(ActionSeq))
    {
        nAction = FishList[nFish].nAction = 0;
        FishList[nFish].nFrame = 0;
    }

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Fish\n", nMessage);
            return;
        }

        case 0x90000:
        {
            if (KexFishExactSequence() && nAction == 9)
                return;
            int const nSeq = SeqOffsets[kSeqFish] + ActionSeq[nAction].a;
            if (FishList[nFish].nFrame < 0
                || FishList[nFish].nFrame >= SeqSize[nSeq])
                FishList[nFish].nFrame = 0;
            seq_PlotSequence(a & 0xFFFF, SeqOffsets[kSeqFish] + ActionSeq[nAction].a, FishList[nFish].nFrame, ActionSeq[nAction].b);
            tsprite[a & 0xFFFF].owner = -1;
            return;
        }

        case 0xA0000:
        {
            if (FishList[nFish].nHealth <= 0) {
                return;
            }
            else
            {
                nDamage = runlist_CheckRadialDamage(nSprite);
                if (!nDamage) {
                    return;
                }

                if (!g_kexMode)
                    FishList[nFish].field_C = 10;
            }
            // fall through
            fallthrough__;
        }
        case 0x80000:
        {
            if (!nDamage) {
                return;
            }

            FishList[nFish].nHealth -= nDamage;
            if (FishList[nFish].nHealth <= 0)
            {
                FishList[nFish].nHealth = 0;
                nCreaturesLeft--;

                sprite[nSprite].cstat &= 0xFEFE;

                if (g_kexMode)
                {
                    short const nKiller = nMessage == 0xA0000
                        ? (short)nRadialOwner : (short)(a & 0xFFFF);
                    if (KexFishExactSequence())
                    {
                        KexFishBeginGibTimer(nFish, nSprite, nKiller);
                        if (nMessage == 0xA0000)
                            KexTrigger_Died(nSprite);
                    }
                    else
                    {
                        if (nMessage == 0xA0000)
                        {
                            KexDrop_RandomItem(nSprite, nKiller);
                            KexTrigger_Died(nSprite);
                        }
                        KexFishImmediateDeath(nFish, nSprite, nKiller);
                    }
                }
                else if (nMessage == 0x80000)
                {
                    for (int i = 0; i < 3; i++)
                    {
                        BuildFishLimb(nFish, i);
                    }

                    PlayFXAtXYZ(StaticSound[kSoundFishDies], sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum);
                    DestroyFish(nFish);
                }
                else
                {
                    FishList[nFish].nAction = 9;
                    FishList[nFish].nFrame = 0;
                }

                return;
            }
            else
            {
                short nTarget = a & 0xFFFF;
                if ((unsigned)nTarget < (unsigned)kMaxSprites
                    && ((!g_kexMode && sprite[nTarget].statnum < 199)
                        || (g_kexMode && sprite[nTarget].statnum == 100)))
                {
                    FishList[nFish].nTarget = nTarget;
                }

                if (g_kexMode)
                {
                    // AI_Fish has painChance 0 and no pain animation or
                    // pain sound. KEX still consumes the random byte in
                    // kexSlaveAI::InflictDamage, but damage never interrupts
                    // its current movement or attack.
                    KexDifficulty_PainRoll(0);
                    return;
                }

                FishList[nFish].nAction = 4;
                FishList[nFish].nFrame = 0;
                FishList[nFish].field_C += 10;
            }

            return;
        }

        case 0x20000:
        {
            bool const bExactFish = KexFishExactSequence();
            if (bExactFish && nAction == 9)
            {
                if (FishList[nFish].nFrame >= 18)
                {
                    if (!seq_IsLoaded(kSeqKexGibDeath))
                        KexDrop_RandomItemAt(sprite[nSprite].sectnum,
                            sprite[nSprite].x, sprite[nSprite].y,
                            sprite[nSprite].z - 64 * 16 * 6,
                            FishList[nFish].nTarget);
                    KexFishDestroy(nFish, nSprite);
                }
                else
                    FishList[nFish].nFrame++;
                return;
            }

            if (!(SectFlag[sprite[nSprite].sectnum] & kSectUnderwater))
            {
                Gravity(nSprite);
            }

            short nSeq = SeqOffsets[kSeqFish] + ActionSeq[nAction].a;
            if (FishList[nFish].nFrame < 0
                || FishList[nFish].nFrame >= SeqSize[nSeq])
                FishList[nFish].nFrame = 0;
            short const nRenderedFrame = FishList[nFish].nFrame;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nRenderedFrame);

            // The KEX bite's sounds are on frames 2/8 and its sole hit is on
            // frame 12. The DOS FISH.SEQ has a different two-hit sound recipe.
            if (!(g_kexMode && nAction == 3))
                seq_MoveSequence(nSprite, nSeq, nRenderedFrame);

            FishList[nFish].nFrame++;
            if (FishList[nFish].nFrame >= SeqSize[nSeq]) {
                FishList[nFish].nFrame = 0;
            }

            short nTarget = FishList[nFish].nTarget;

            switch (nAction)
            {
                default:
                    return;

                case 0:
                {
                    FishList[nFish].field_C--;
                    if (FishList[nFish].field_C <= 0)
                    {
                        nTarget = FindPlayer(nSprite, 60);
                        if (nTarget >= 0)
                        {
                            if (g_kexMode)
                            {
                                int const sound = KexSound_Get("fish_act");
                                if (sound >= 0)
                                    D3PlayFX(sound, nSprite);
                            }
                            FishList[nFish].nTarget = nTarget;
                            FishList[nFish].nAction = 2;
                            FishList[nFish].nFrame = 0;

                            int nAngle = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].z - sprite[nSprite].z);
                            sprite[nSprite].zvel = Sin(nAngle) >> 5;

                            FishList[nFish].field_C = RandomSize(6) + 90;
                        }
                        else
                        {
                            IdleFish(nFish, 0);
                        }
                    }

                    break;
                }

                case 1:
                    return;

                case 2:
                case 3:
                {
                    if (g_kexMode && nAction == 3)
                    {
                        if (nRenderedFrame == 2 || nRenderedFrame == 8)
                        {
                            int const s = KexSound_Get("fishbit2");
                            if (s >= 0) D3PlayFX(s, nSprite);
                        }
                        if (nRenderedFrame == 12
                            && KexMeleeAttackRange(nSprite, nTarget, 48))
                            runlist_DamageEnemy(nTarget, nSprite, 20);
                        if (nRenderedFrame == 13)
                        {
                            // A_CheckMelee(..., 16) keeps biting while close.
                            FishList[nFish].nAction = KexMeleeRange(nSprite, nTarget, 16)
                                ? 3 : 2;
                            FishList[nFish].nFrame = 0;
                        }
                    }

                    if (g_kexMode && nAction == 2
                        && KexAIEnterMeleeRange(nSprite, nTarget, 16))
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        FishList[nFish].nAction = 3;
                        FishList[nFish].nFrame = 0;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].zvel = 0;
                        return;
                    }

                    FishList[nFish].field_C--;
                    if (FishList[nFish].field_C <= 0)
                    {
                        IdleFish(nFish, 0);
                        return;
                    }
                    else
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        int nHeight = GetSpriteHeight(nSprite) >> 1;

                        int z = klabs(sprite[nTarget].z - sprite[nSprite].z);

                        if (z <= nHeight)
                        {
                            sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) >> 5) - (Cos(sprite[nSprite].ang) >> 7);
                            sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) >> 5) - (Sin(sprite[nSprite].ang) >> 7);
                        }
                        else
                        {
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                        }

                        sprite[nSprite].zvel = (sprite[nTarget].z - sprite[nSprite].z) >> 3;
                    }
                    break;
                }

                case 4:
                {
                    if (FishList[nFish].nFrame == 0)
                    {
                        IdleFish(nFish, 0);
                    }
                    return;
                }

                case 8:
                {
                    return;
                }

                case 9:
                {
                    if (FishList[nFish].nFrame == 0)
                    {
                        DestroyFish(nFish);
                    }
                    return;
                }
            }

            int x = sprite[nSprite].x;
            int y = sprite[nSprite].y;
            int z = sprite[nSprite].z;
            short nSector = sprite[nSprite].sectnum;

            // loc_2EF54
            int nMov = movesprite(nSprite,
                                  KexScaleCreatureMove(nSprite, sprite[nSprite].xvel << 13),
                                  KexScaleCreatureMove(nSprite, sprite[nSprite].yvel << 13),
                                  KexScaleCreatureMove(nSprite, sprite[nSprite].zvel << 2),
                                  0, 0, CLIPMASK0);

            if (!(SectFlag[sprite[nSprite].sectnum] & kSectUnderwater))
            {
                mychangespritesect(nSprite, nSector);
                sprite[nSprite].x = x;
                sprite[nSprite].y = y;
                sprite[nSprite].z = z;

                IdleFish(nFish, 0);
                return;
            }
            else
            {
                if (nAction >= 5) {
                    return;
                }

                if (!nMov)
                {
                    if (nAction == 3 && !g_kexMode)
                    {
                        FishList[nFish].nAction = 2;
                        FishList[nFish].nFrame = 0;
                    }
                    return;
                }

                if ((nMov & 0x30000) == 0)
                {
                    if ((nMov & 0xC000) == 0x8000)
                    {
                        IdleFish(nFish, 0);
                    }
                    else if ((nMov & 0xC000) == 0xC000)
                    {
                        if (sprite[nMov & 0x3FFF].statnum == 100)
                        {
                            FishList[nFish].nTarget = nMov & 0x3FFF;
                            sprite[nSprite].ang = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);

                            if (nAction != 3)
                            {
                                FishList[nFish].nAction = 3;
                                FishList[nFish].nFrame = 0;
                            }
                            else if (!g_kexMode && !FishList[nFish].nFrame)
                                runlist_DamageEnemy(nTarget, nSprite, 2);
                        }
                    }
                }
                else if (nMov & 0x20000)
                {
                    IdleFish(nFish, -1);
                }
                else
                {
                    IdleFish(nFish, 1);
                }
            }

            return;
        }
    }
}

class FishLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void FishLoadSave::Load()
{
    Read(&FishCount, sizeof(FishCount));
    Read(&nChunksFree, sizeof(nChunksFree));
    Read(nFreeChunk, sizeof(nFreeChunk[0]) * kMaxChunks);
    Read(FishList, sizeof(FishList[0]) * kMaxFishes);
    Read(FishChunk, sizeof(FishChunk[0]) * kMaxChunks);
}

void FishLoadSave::Save()
{
    Write(&FishCount, sizeof(FishCount));
    Write(&nChunksFree, sizeof(nChunksFree));
    Write(nFreeChunk, sizeof(nFreeChunk[0]) * kMaxChunks);
    Write(FishList, sizeof(FishList[0]) * kMaxFishes);
    Write(FishChunk, sizeof(FishChunk[0]) * kMaxChunks);
}

static FishLoadSave* myLoadSave;

void FishLoadSaveConstruct()
{
    myLoadSave = new FishLoadSave();
}
