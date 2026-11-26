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

#include "lion.h"
#include "engine.h"
#include "runlist.h"
#include "exhumed.h"
#include "sequence.h"
#include "move.h"
#include "sound.h"
#include "random.h"
#include "trigdat.h"
#include "items.h"
#include "lighting.h"
#include "save.h"
#include "kexbreak.h" // NNBuild: KEX Bastet teleport sound
#include "kexinv.h"
#include "anims.h"
#include <assert.h>

#define kMaxLions   40

static actionSeq ActionSeq[] = {
    {54, 1},
    {18, 0},
    {0,  0},
    {10, 0},
    {44, 0},
    {18, 0},
    {26, 0},
    {34, 0},
    {8,  1},
    {9,  1},
    {52, 1},
    {53, 1}
};

struct Lion
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short _f;
    short _g;
};

short LionCount = 0;
short MoveHook[kMaxLions];
Lion LionList[kMaxLions];


void InitLion()
{
    LionCount = kMaxLions;
}

static bool KexBastetExactSequence()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqLion))
        return false;

    int const nBase = SeqOffsets[kSeqLion];
    return SeqSize[nBase] == 12
        && SeqSize[nBase + 8] == 41
        && SeqSize[nBase + 9] == 23
        && SeqSize[nBase + 10] == 11
        && SeqSize[nBase + 44] == 17
        && SeqSize[nBase + 54] == 1;
}

static bool KexBastetValidTarget(short nTarget)
{
    return (unsigned)nTarget < (unsigned)kMaxSprites
        && sprite[nTarget].sectnum >= 0
        && sprite[nTarget].sectnum < kMaxSectors
        && sprite[nTarget].statnum < kMaxStatus
        && (sprite[nTarget].cstat & 0x101)
        && !(sprite[nTarget].cstat & 0x8000);
}

static bool KexBastetRandomDecision(int nMask)
{
    // scripts/p_enemy.txt: !RandomDecision(actor, mask) means the random
    // masked phase equals this simulation tick's masked phase.
    return (int)(KexRandomLong() & (uint32_t)nMask) == (totalmoves & nMask);
}

static void KexBastetPlaySound(short nSprite, char const *name)
{
    int const sound = KexSound_Get(name);
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexBastetSetChaseVelocity(short nSprite)
{
    int const nWorldSpeed = 24 * 6;
    sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * nWorldSpeed) >> 8;
    sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * nWorldSpeed) >> 8;
}

static void KexBastetStartLight(short nLion, short nSprite)
{
    // A_SpawnLight(..., 20, 2) persists for twenty simulation ticks. Negative
    // _g values are a save-serialized light countdown; positive values retain
    // the native scratch/cooldown uses.
    LionList[nLion]._g = -19;
    AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x, sprite[nSprite].y,
             sprite[nSprite].z, 80);
}

static void KexBastetTickLight(short nLion, short nSprite)
{
    if (LionList[nLion]._g >= 0)
        return;
    AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x, sprite[nSprite].y,
             sprite[nSprite].z, 80);
    LionList[nLion]._g++;
}

static void KexBastetStartTeleportOut(short nLion, short nSprite)
{
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat &= ~0x101;
    LionList[nLion].nAction = 8;
    LionList[nLion].nFrame = 0;
}

static bool KexBastetTryTeleportIn(short nLion, short nSprite)
{
    if (!KexBastetValidTarget(LionList[nLion].nTarget)
        || !KexBastetRandomDecision(30)
        || ((uint8_t)RandomByte() & 7) != 0)
        return false;

    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    LionList[nLion].nAction = 9;
    LionList[nLion].nFrame = 0;
    return true;
}

static void KexBastetTossGibs(short nSprite)
{
    // bastet_death.tsc plays both local sounds, tosses one gib1, two gib2,
    // one gib3 and three gib4, then destroys the host actor.
    static const int gibTiles[] = { 7816, 7817, 7817, 7818, 7819, 7819, 7819 };

    int sound = KexSound_Get("cat_xpld");
    if (sound >= 0)
        PlayLocalSound(sound, 0);
    sound = KexSound_Get("cat_deth");
    if (sound >= 0)
        PlayLocalSound(sound, 0);

    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);
}

static void KexBastetDestroy(short nLion, short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(MoveHook[nLion]);
    sprite[nSprite].cstat = 0x8000;
    mydeletesprite(nSprite);
    LionList[nLion].nSprite = -1;
}

static void KexBastetImmediateDeath(short nLion, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexBastetTossGibs(nSprite);
    KexBastetDestroy(nLion, nSprite);
}

static void KexBastetBeginGibTimer(short nLion, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexBastetTossGibs(nSprite);
    LionList[nLion].nAction = 11;
    LionList[nLion].nFrame = 0;
    LionList[nLion].nTarget = nKiller;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
}

int KexBastetOwnsDeathDrop(int nSprite)
{
    if (!g_kexMode || !KexBastetExactSequence())
        return 0;

    for (int nLion = LionCount; nLion < kMaxLions; nLion++)
        if (LionList[nLion].nSprite == nSprite && LionList[nLion].nAction == 11)
            return 1;
    return 0;
}

int BuildLion(short nSprite, int x, int y, int z, short nSector, short nAngle)
{
    if (LionCount <= 0)
        return -1;
    short const nLion = --LionCount;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 104);
    }
    else
    {
        changespritestat(nSprite, 104);
        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sector[sprite[nSprite].sectnum].floorz;
        nAngle = sprite[nSprite].ang;
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    // AI_Bastet radius 48 and scale 1.5. Build's clipdist is radius*WORLD/4
    // and one KEX scale unit is 40 repeat; retain the classic lion values
    // outside the remaster compatibility path.
    sprite[nSprite].clipdist = g_kexMode ? 72 : 60;
    sprite[nSprite].shade = -12;
    sprite[nSprite].xrepeat = g_kexMode ? 60 : 40;
    sprite[nSprite].yrepeat = g_kexMode ? 60 : 40;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].extra = -1;

//	GrabTimeSlot(3);

    LionList[nLion].nAction = 0;
    // AI_Bastet has 150 health in the KEX definitions; classic lions keep
    // their native 500.
    LionList[nLion].nHealth = g_kexMode ? KexHealthUnits(150) : 500;
    LionList[nLion].nFrame = 0;
    LionList[nLion].nSprite = nSprite;
    LionList[nLion].nTarget = -1;
    LionList[nLion]._g = 0;
    LionList[nLion]._f = nLion;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nLion, kRunLion);

    MoveHook[nLion] = runlist_AddRunRec(NewRun, nLion, kRunLion);

    nCreaturesLeft++;

    return nLion;
}

void FuncLion(int a, int nDamage, int nRun)
{
    short nLion = RunData[nRun].nVal;
    assert(nLion >= 0 && nLion < kMaxLions);

    short nSprite = LionList[nLion].nSprite;
    short nAction = LionList[nLion].nAction;
    if ((unsigned)nAction >= ARRAY_SIZE(ActionSeq))
    {
        nAction = LionList[nLion].nAction = 0;
        LionList[nLion].nFrame = 0;
    }

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Lion\n", nMessage);
            return;
        }

        case 0x90000:
        {
            if (KexBastetExactSequence() && nAction == 11)
                return;
            int const nSeq = SeqOffsets[kSeqLion] + ActionSeq[nAction].a;
            if (LionList[nLion].nFrame < 0
                || LionList[nLion].nFrame >= SeqSize[nSeq])
                LionList[nLion].nFrame = 0;
            seq_PlotSequence(a, SeqOffsets[kSeqLion] + ActionSeq[nAction].a, LionList[nLion].nFrame, ActionSeq[nAction].b);
            return;
        }

        case 0xA0000:
        {
            nDamage = runlist_CheckRadialDamage(nSprite);
            // now fall through to 0x80000
            fallthrough__;
        }
        case 0x80000:
        {
            if (nDamage && LionList[nLion].nHealth > 0)
            {
                LionList[nLion].nHealth -= nDamage;
                if (LionList[nLion].nHealth <= 0)
                {
                    // R.I.P.
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                    sprite[nSprite].zvel = 0;
                    sprite[nSprite].cstat &= 0xFEFE;

                    LionList[nLion].nHealth = 0;

                    nCreaturesLeft--;

                    if (nAction < 10)
                    {
                        if (g_kexMode)
                        {
                            short const nKiller = nMessage == 0xA0000
                                ? (short)nRadialOwner : (short)(a & 0xFFFF);
                            if (KexBastetExactSequence())
                            {
                                // bastet_death is a spriteless one-frame gib
                                // callback. Keep only an invisible controller
                                // until generic Fx_Gibs01 runs A_RandomItem on
                                // timer tick 18.
                                KexBastetBeginGibTimer(nLion, nSprite, nKiller);
                                if (nMessage == 0xA0000)
                                    KexTrigger_Died(nSprite);
                                return;
                            }

                            // Old converted data keeps its prior immediate
                            // visual death, but radial dispatch still needs
                            // the drop/trigger hooks that direct damage owns.
                            if (nMessage == 0xA0000)
                            {
                                KexDrop_RandomItem(nSprite, nKiller);
                                KexTrigger_Died(nSprite);
                            }
                            KexBastetImmediateDeath(nLion, nSprite, nKiller);
                            return;
                        }

                        DropMagic(nSprite);

                        if (nMessage == 0xA0000) {
                            LionList[nLion].nAction = 11;
                        }
                        else
                        {
                            LionList[nLion].nAction = 10;
                        }

                        LionList[nLion].nFrame = 0;
                        return;
                    }
                }
                else
                {
                    short nTarget = a & 0xFFFF;

                    if (g_kexMode)
                    {
                        if ((unsigned)nTarget < (unsigned)kMaxSprites
                            && sprite[nTarget].statnum < 199)
                            LionList[nLion].nTarget = nTarget;

                        // kexSlaveAI only enters pain from its chase state.
                        // Bastet's Build action 2 is that state; its jump,
                        // hide and damage-dependent reactions are DOS-only.
                        if (KexDifficulty_PainRoll(24) && nAction == 2)
                        {
                            LionList[nLion].nAction = 4;
                            LionList[nLion].nFrame = 0;
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;

                            // Exact bastet_pain calls A_BastetScream on frame
                            // 6. The old loose-data fallback retains its
                            // immediate cue so its timing does not regress.
                            if (!KexBastetExactSequence())
                            {
                                int const sound = KexSound_Get(
                                    RandomBit() ? "cat_pain1" : "cat_pain2");
                                if (sound >= 0)
                                    PlayLocalSound(sound, 0);
                            }
                        }
                        return;
                    }

                    if (nTarget > -1)
                    {
                        if (sprite[nTarget].statnum < 199) {
                            LionList[nLion].nTarget = nTarget;
                        }

                        if (nAction != 6)
                        {
                            if (RandomSize(8) <= (LionList[nLion].nHealth >> 2))
                            {
                                LionList[nLion].nAction = 4;
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;

                            }
                            else if (RandomSize(1))
                            {
                                PlotCourseToSprite(nSprite, nTarget);
                                LionList[nLion].nAction = 5;
                                LionList[nLion]._g = RandomSize(3);
                                sprite[nSprite].ang = (sprite[nSprite].ang - (RandomSize(1) << 8)) + (RandomSize(1) << 8); // NOTE: no angle mask in original code
                            }
                            else
                            {
                                LionList[nLion].nAction = 8;
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                                sprite[nSprite].cstat &= 0xFEFE;
                            }

                            LionList[nLion].nFrame = 0;
                        }
                    }
                }
            }
            return;
        }

        case 0x20000:
        {
            bool const bExactBastet = KexBastetExactSequence();
            if (bExactBastet && nAction == 11)
            {
                if (LionList[nLion].nFrame >= 18)
                {
                    if (!seq_IsLoaded(kSeqKexGibDeath))
                        KexDrop_RandomItemAt(sprite[nSprite].sectnum,
                            sprite[nSprite].x, sprite[nSprite].y,
                            sprite[nSprite].z - 64 * 16 * 6,
                            LionList[nLion].nTarget);
                    KexBastetDestroy(nLion, nSprite);
                }
                else
                    LionList[nLion].nFrame++;
                return;
            }

            if (bExactBastet)
                KexBastetTickLight(nLion, nSprite);

            if (nAction != 7) {
                Gravity(nSprite);
            }

            short nSeq = SeqOffsets[kSeqLion] + ActionSeq[nAction].a;
            if (LionList[nLion].nFrame < 0
                || LionList[nLion].nFrame >= SeqSize[nSeq])
                LionList[nLion].nFrame = 0;
            short const nRenderedFrame = LionList[nLion].nFrame;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nRenderedFrame);

            // Bastet's attack callbacks do not match the DOS lion sequence.
            // The exact eleven-frame KEX timeline below owns its sounds/hits.
            if (!(g_kexMode && nAction == 3))
                seq_MoveSequence(nSprite, nSeq, nRenderedFrame);

            LionList[nLion].nFrame++;
            if (LionList[nLion].nFrame >= SeqSize[nSeq])
            {
                LionList[nLion].nFrame = 0;
                bVal = true;
            }

            short nFlag = FrameFlag[SeqBase[nSeq] + LionList[nLion].nFrame];
            short nTarget = LionList[nLion].nTarget;

            int nMov = MoveCreatureWithCaution(nSprite);

            switch (nAction)
            {
                default:
                    return;

                case 0:
                case 1:
                {
                    if ((LionList[nLion]._f & 0x1F) == (totalmoves & 0x1F))
                    {
                        if (nTarget < 0)
                        {
                            nTarget = FindPlayer(nSprite, 40);
                            if (nTarget >= 0)
                            {
                                if (bExactBastet)
                                    KexBastetPlaySound(nSprite, "cat_icu");
                                else
                                    D3PlayFX(StaticSound[kSoundCatICU], nSprite);
                                LionList[nLion].nAction = 2;
                                LionList[nLion].nFrame = 0;

                                if (bExactBastet)
                                    KexBastetSetChaseVelocity(nSprite);
                                else
                                {
                                    sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                                    sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                                }
                                LionList[nLion].nTarget = nTarget;
                                return;
                            }
                        }
                    }

                    if (nAction)
                    {
                        LionList[nLion]._g--;
                        if (LionList[nLion]._g <= 0)
                        {
                            if (RandomBit())
                            {
                                sprite[nSprite].ang = RandomWord() & kAngleMask;
                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                                sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                            }
                            else
                            {
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                            }

                            LionList[nLion]._g = 100;
                        }
                    }

                    return;
                }

                case 2:
                {
                    if (bExactBastet)
                    {
                        // A_BastetTryTeleport runs on every one of the twelve
                        // chase frames. Hidden actors use the separate
                        // 1-in-16 phase/1-in-8 return test; visible actors use
                        // the 1-in-8 phase/1-in-64 departure test.
                        if (sprite[nSprite].cstat & 0x8000)
                        {
                            KexBastetTryTeleportIn(nLion, nSprite);
                            return;
                        }
                        if (nRenderedFrame == 0 && KexBastetValidTarget(nTarget))
                            PlotCourseToSprite(nSprite, nTarget);
                        if (KexBastetValidTarget(nTarget)
                            && KexBastetRandomDecision(14)
                            && (((uint8_t)RandomByte() & 63) == 0))
                        {
                            KexBastetStartTeleportOut(nLion, nSprite);
                            return;
                        }
                        KexBastetSetChaseVelocity(nSprite);
                    }

                    // Safe fallback for conversions with the stock teleport
                    // cadence. Exact loose data uses the authored callbacks
                    // above and has no forced timeout.
                    if (g_kexMode && !bExactBastet
                        && (sprite[nSprite].cstat & 0x8000))
                    {
                        // A_BastetTryTeleportIn retries on a random tick
                        // (RandomDecision(..., 30) plus an 1-in-8 roll). The
                        // counter is only a guard against a pathological
                        // random stream leaving the actor hidden forever.
                        if (--LionList[nLion]._g <= 0
                            || (RandomSize(5) == 0 && RandomSize(2) == 0))
                        {
                            LionList[nLion].nAction = 9;
                            LionList[nLion].nFrame = 0;
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;

                            // bastet_teleport_in cues its authored sound on
                            // the first visible frame.
                            int const sound = KexSound_Get("tele_3");
                            if (sound >= 0)
                                D3PlayFX(sound, nSprite);
                        }

                        return;
                    }

                    // AI_Bastet's meleeExtraDist is 16. Enter the authored
                    // attack as soon as the two collision radii plus that
                    // reach overlap, rather than waiting for movesprite to
                    // report a literal collision.
                    if (g_kexMode && KexBastetValidTarget(nTarget)
                        && KexAIEnterMeleeRange(nSprite, nTarget, 16))
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        LionList[nLion].nAction = 3;
                        LionList[nLion].nFrame = 0;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        return;
                    }

                    if (g_kexMode && !bExactBastet && nTarget >= 0
                        && !(sprite[nSprite].cstat & 0x8000)
                        && (LionList[nLion]._f & 0x1F) == (totalmoves & 0x1F)
                        && RandomSize(8) == 0)
                    {
                        LionList[nLion].nAction = 8;
                        LionList[nLion].nFrame = 0;
                        LionList[nLion]._g = 300;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;

                        int const sound = KexSound_Get("tele_2");
                        if (sound >= 0)
                            D3PlayFX(sound, nSprite);

                        return;
                    }

                    if (!bExactBastet
                        && (totalmoves & 0x1F) == (LionList[nLion]._f & 0x1F))
                    {
                        PlotCourseToSprite(nSprite, nTarget);

                        int nAng = sprite[nSprite].ang & 0xFFF8;

                        if (sprite[nSprite].cstat & 0x8000)
                        {
                            sprite[nSprite].xvel = Cos(nAng) * 2;
                            sprite[nSprite].yvel = Sin(nAng) * 2;
                        }
                        else
                        {
                            sprite[nSprite].xvel = Cos(nAng) >> 1;
                            sprite[nSprite].yvel = Sin(nAng) >> 1;
                        }
                    }

                    if ((nMov & 0xC000) < 0x8000)
                    {
                        break;
                    }
                    else if ((nMov & 0xC000) == 0x8000)
                    {
                        // loc_378FA:
                        sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                        if (bExactBastet)
                            KexBastetSetChaseVelocity(nSprite);
                        else
                        {
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                        }
                        break;
                    }
                    else if ((nMov & 0xC000) == 0xC000)
                    {
                        if ((nMov & 0x3FFF) == nTarget)
                        {
                            if (sprite[nSprite].cstat & 0x8000)
                            {
                                LionList[nLion].nAction = 9;
                                sprite[nSprite].cstat &= 0x7FFF;
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                            }
                            else
                            {
                                int nAng = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);

                                if (AngleDiff(sprite[nSprite].ang, nAng) < 64)
                                {
                                    LionList[nLion].nAction = 3;
                                }
                            }

                            LionList[nLion].nFrame = 0;
                            break;
                        }
                        else
                        {
                            // loc_378FA:
                            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                            if (bExactBastet)
                                KexBastetSetChaseVelocity(nSprite);
                            else
                            {
                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                                sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                            }
                            break;
                        }
                    }

                    break;
                }

                case 3:
                {
                    if (nTarget == -1)
                    {
                        LionList[nLion].nAction = 1;
                        LionList[nLion]._g = 50;
                    }
                    else
                    {
                        if (g_kexMode)
                        {
                            if (bExactBastet && nRenderedFrame == 0)
                            {
                                // A_BastetCheckTeleport either returns a
                                // hidden attacker to chase or occasionally
                                // selects the authored teleport-in animation
                                // even while the actor is already visible.
                                if (sprite[nSprite].cstat & 0x8000)
                                {
                                    LionList[nLion].nAction = 2;
                                    LionList[nLion].nFrame = 0;
                                    return;
                                }
                                if (KexBastetRandomDecision(30)
                                    && (((uint8_t)RandomByte() & 7) == 0))
                                {
                                    LionList[nLion].nAction = 9;
                                    LionList[nLion].nFrame = 0;
                                    return;
                                }
                            }
                            PlotCourseToSprite(nSprite, nTarget);
                            if (nRenderedFrame == 1 || nRenderedFrame == 6)
                            {
                                int const s = KexSound_Get("cat_warp");
                                if (s >= 0) D3PlayFX(s, nSprite);
                            }
                            if (nRenderedFrame == 2 || nRenderedFrame == 7)
                            {
                                int const s = KexSound_Get("cat_xpld");
                                if (s >= 0) D3PlayFX(s, nSprite);
                            }
                            if ((nRenderedFrame == 1 || nRenderedFrame == 7)
                                && KexMeleeAttackRange(nSprite, nTarget, 48))
                                runlist_DamageEnemy(nTarget, nSprite, 20);
                            if (nRenderedFrame == 10)
                            {
                                // A_CheckMelee(..., 48) loops the eleven-frame
                                // attack while its expanded contact still holds.
                                if (KexMeleeRange(nSprite, nTarget, 48))
                                    LionList[nLion].nFrame = 0;
                                else
                                {
                                    LionList[nLion].nAction = 2;
                                    LionList[nLion].nFrame = 0;
                                }
                            }
                        }
                        else if (PlotCourseToSprite(nSprite, nTarget) >= 768)
                            LionList[nLion].nAction = 2;
                        else if (nFlag & 0x80)
                            runlist_DamageEnemy(nTarget, nSprite, 10);
                    }

                    break;
                }

                case 4:
                {
                    if (bExactBastet)
                    {
                        if (nRenderedFrame == 6)
                            KexBastetPlaySound(nSprite,
                                ((uint8_t)RandomByte() & 1)
                                    ? "cat_pain2" : "cat_pain1");
                        if (nRenderedFrame == 15
                            && KexBastetValidTarget(nTarget))
                        {
                            // A_BastetPainTeleport retries the normal return
                            // callback while hidden; otherwise a one-in-four
                            // byte roll starts teleport-out immediately.
                            if (sprite[nSprite].cstat & 0x8000)
                            {
                                if (KexBastetTryTeleportIn(nLion, nSprite))
                                    return;
                            }
                            else if ((((uint8_t)RandomByte() & 3) == 0))
                            {
                                KexBastetStartTeleportOut(nLion, nSprite);
                                return;
                            }
                        }
                    }

                    if (bVal)
                    {
                        LionList[nLion].nAction = 2;
                        LionList[nLion].nFrame = 0;
                        if (bExactBastet)
                            KexBastetSetChaseVelocity(nSprite);
                    }

                    if (nMov & 0x20000)
                    {
                        sprite[nSprite].xvel >>= 1;
                        sprite[nSprite].yvel >>= 1;
                    }

                    return;
                }

                case 5: // Jump away when damaged
                {
                    LionList[nLion]._g--;
                    if (LionList[nLion]._g <= 0)
                    {
                        sprite[nSprite].zvel = -4000;
                        LionList[nLion]._g = 0;

                        int x = sprite[nSprite].x;
                        int y = sprite[nSprite].y;
                        int z = sprite[nSprite].z - (GetSpriteHeight(nSprite) >> 1);

                        int nCheckDist = 0x7FFFFFFF;

                        short nAngle = sprite[nSprite].ang;
                        short nScanAngle = (sprite[nSprite].ang - 512) & kAngleMask;

                        for (int i = 0; i < 5; i++)
                        {
                            short hitwall;
                            int hitx, hity;
                            vec3_t startPos = { x, y, z };
                            hitdata_t hitData;

                            hitscan(&startPos, sprite[nSprite].sectnum, Cos(nScanAngle), Sin(nScanAngle), 0, &hitData, CLIPMASK1);

                            hitx = hitData.x;
                            hity = hitData.y;
                            hitwall = hitData.wall;

                            if (hitwall > -1)
                            {
                                int theX = klabs(hitx - x);
                                int theY = klabs(hity - y);

                                if ((theX + theY) < nCheckDist)
                                {
                                    nCheckDist = theX;
                                    nAngle = nScanAngle;
                                }
                            }

                            nScanAngle += 256;
                            nScanAngle &= kAngleMask;
                        }

                        sprite[nSprite].ang = nAngle;

                        LionList[nLion].nAction = 6;
                        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang)) - (Cos(sprite[nSprite].ang) >> 3);
                        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang)) - (Sin(sprite[nSprite].ang) >> 3);
                        D3PlayFX(StaticSound[kSoundCatICU], nSprite);
                    }

                    return;
                }

                case 6:
                {
                    if (nMov & 0x30000)
                    {
                        LionList[nLion].nAction = 2;
                        LionList[nLion].nFrame = 0;
                        return;
                    }

                    if ((nMov & 0xC000) == 0x8000)
                    {
                        LionList[nLion].nAction = 7;
                        sprite[nSprite].ang = (GetWallNormal(nMov & 0x3FFF) + 1024) & kAngleMask;
                        LionList[nLion]._g = RandomSize(4);
                        return;
                    }
                    else if ((nMov & 0xC000) == 0xC000)
                    {
                        if ((nMov & 0x3FFF) == nTarget)
                        {
                            int nAng = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                            if (AngleDiff(sprite[nSprite].ang, nAng) < 64)
                            {
                                LionList[nLion].nAction = 3;
                                LionList[nLion].nFrame = 0;
                            }
                        }
                        else
                        {
                            // loc_378FA:
                            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                            break;
                        }
                    }

                    return;
                }

                case 7:
                {
                    LionList[nLion]._g--;

                    if (LionList[nLion]._g <= 0)
                    {
                        LionList[nLion]._g = 0;
                        if (nTarget > -1)
                        {
                            PlotCourseToSprite(nSprite, nTarget);
                        }
                        else
                        {
                            sprite[nSprite].ang = (RandomSize(9) + (sprite[nSprite].ang + 768)) & kAngleMask;
                        }

                        sprite[nSprite].zvel = -1000;

                        LionList[nLion].nAction = 6;
                        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang)) - (Cos(sprite[nSprite].ang) >> 3);
                        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang)) - (Sin(sprite[nSprite].ang) >> 3);
                        D3PlayFX(StaticSound[kSoundCatICU], nSprite);
                    }

                    return;
                }

                case 8:
                {
                    if (bExactBastet)
                    {
                        if (nRenderedFrame == 10)
                        {
                            KexBastetPlaySound(nSprite, "tele_2");
                            KexBastetStartLight(nLion, nSprite);
                        }
                        // Expanded positions 37..40 hold the authored final
                        // frame for its delay 4. A_Hide runs on the first one.
                        if (nRenderedFrame == 37)
                            sprite[nSprite].cstat |= 0x8000;
                        if (bVal)
                        {
                            LionList[nLion].nAction = 2;
                            LionList[nLion].nFrame = 0;
                        }
                        return;
                    }

                    if (bVal)
                    {
                        LionList[nLion].nAction = 2;
                        if (g_kexMode)
                            LionList[nLion]._g = 300;
                        LionList[nLion].nFrame  = 0;
                        sprite[nSprite].cstat |= 0x8000;
                    }
                    return;
                }

                case 9:
                {
                    if (bExactBastet)
                    {
                        if (nRenderedFrame == 0)
                            sprite[nSprite].cstat &= ~0x8000;
                        if (nRenderedFrame == 1)
                        {
                            KexBastetPlaySound(nSprite, "tele_3");
                            KexBastetStartLight(nLion, nSprite);
                        }
                        if (nRenderedFrame == 16)
                            KexBastetPlaySound(nSprite, "cat_warp");
                        if (nRenderedFrame == 22)
                        {
                            // A_BastetTeleportIn restores blocking and writes
                            // the retail scratch value before returning chase.
                            sprite[nSprite].cstat &= ~0x8000;
                            sprite[nSprite].cstat |= 0x101;
                            LionList[nLion]._g = 30;
                            LionList[nLion].nFrame = 0;
                            LionList[nLion].nAction = 2;
                            KexBastetSetChaseVelocity(nSprite);
                        }
                        return;
                    }

                    if (bVal)
                    {
                        LionList[nLion].nFrame  = 0;
                        LionList[nLion].nAction = 2;
                        sprite[nSprite].cstat |= 0x101;
                    }
                    return;
                }

                case 10:
                case 11:
                {
                    if (bVal)
                    {
                        runlist_SubRunRec(sprite[nSprite].owner);
                        runlist_SubRunRec(MoveHook[nLion]);
                        sprite[nSprite].cstat = 0x8000;
                    }
                    return;
                }
            }

            // loc_379AD: ?
            if (nAction != 1 && nTarget != -1)
            {
                if (!(sprite[nTarget].cstat & 0x101))
                {
                    LionList[nLion].nAction = 1;
                    LionList[nLion].nFrame = 0;
                    LionList[nLion]._g = 100;
                    LionList[nLion].nTarget = -1;
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                }
            }

            return;
        }
    }
}

class LionLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void LionLoadSave::Load()
{
    Read(&LionCount, sizeof(LionCount));
    Read(&MoveHook, sizeof(MoveHook));
    Read(&LionList[LionCount], sizeof(Lion) * (kMaxLions - LionCount));
}

void LionLoadSave::Save()
{
    Write(&LionCount, sizeof(LionCount));
    Write(&MoveHook, sizeof(MoveHook));
    Write(&LionList[LionCount], sizeof(Lion) * (kMaxLions - LionCount));
}

static LionLoadSave* myLoadSave;

void LionLoadSaveConstruct()
{
    myLoadSave = new LionLoadSave();
}
