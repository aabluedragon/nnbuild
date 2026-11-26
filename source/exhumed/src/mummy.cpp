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

#include "mummy.h"
#include "sequence.h"
#include "move.h"
#include "map.h"
#include "sound.h"
#include "exhumed.h"
#include "random.h"
#include "trigdat.h"
#include "bullet.h"
#include "items.h"
#include "save.h"
#include <assert.h>
#include "engine.h"
#include "kexbreak.h" // NNBuild: KEX mummy death sounds
#include "kexinv.h"
#include "anims.h"

struct Mummy
{
    short nHealth;
    short B;
    short nAction;
    short nSprite;
    short nTarget;
    short F;
    short G;
    short H;
};

static actionSeq ActionSeq[] = {
    {8,  0},
    {0,  0},
    {16, 0},
    {24, 0},
    {32, 1},
    {40, 1},
    {48, 1},
    {50, 0}
};

short nMummies = -1;
Mummy MummyList[kMaxMummies];

static bool KexMummyExactSequence()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqMummy))
        return false;

    int const nBase = SeqOffsets[kSeqMummy];
    return SeqSize[nBase] == 16
        && SeqSize[nBase + 8] == 1
        && SeqSize[nBase + 16] == 13
        && SeqSize[nBase + 24] == 39
        && SeqSize[nBase + 32] == 35
        && SeqSize[nBase + 40] == 1
        && SeqSize[nBase + 50] == 9;
}

static bool KexMummyShouldGib(int nRemainingHealth, int nDamageType)
{
    // kexSlaveActor::InflictDamage selects gibAnim for fire/explosion, or for
    // generic overkill strictly below -2-(maxHealth>>4). AI_Mummy has 130 HP,
    // so exactly -10 survives as a normal death and -11 gibs.
    return nDamageType == kKexDamageExplosion
        || nDamageType == kKexDamageFire
        || nRemainingHealth < KexHealthUnits(-2 - (130 >> 4));
}

int KexMummyGibSelectorTest()
{
    int const threshold = KexHealthUnits(-2 - (130 >> 4));
    return !KexMummyShouldGib(threshold, kKexDamageGeneric)
        && KexMummyShouldGib(threshold - 1, kKexDamageGeneric)
        && KexMummyShouldGib(0, kKexDamageExplosion)
        && KexMummyShouldGib(0, kKexDamageFire)
        && !KexMummyShouldGib(0, kKexDamageAcid);
}


void InitMummy()
{
    nMummies = 0;
}

int BuildMummy(int nSprite, int x, int y, int z, int nSector, int nAngle)
{
    if (nMummies >= kMaxMummies) {
        return -1;
    }

    short nMummy = nMummies++;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 102);
    }
    else
    {
        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sprite[nSprite].z;
        nAngle = sprite[nSprite].ang;

        changespritestat(nSprite, 102);
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].shade = -12;
    sprite[nSprite].clipdist = g_kexMode ? 72 : 32;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].xrepeat = g_kexMode ? 60 : 42;
    sprite[nSprite].yrepeat = g_kexMode ? 60 : 42;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

//  GrabTimeSlot(3);

    MummyList[nMummy].nAction = 0;
    // AI_Mummy has 130 health in the KEX definitions; classic mummies keep
    // the native 640.
    MummyList[nMummy].nHealth = g_kexMode ? KexHealthUnits(130) : 640;
    MummyList[nMummy].B = 0;
    MummyList[nMummy].nSprite = nSprite;
    MummyList[nMummy].nTarget = -1;
    MummyList[nMummy].F = nMummy;
    MummyList[nMummy].G = 0;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nMummy, kRunMummy);

    MummyList[nMummy].H = runlist_AddRunRec(NewRun, nMummy, kRunMummy);

    nCreaturesLeft++;

    return nMummy;
}

void CheckMummyRevive(short nMummy)
{
    short nSprite = MummyList[nMummy].nSprite;

    for (int i = 0; i < nMummies; i++)
    {
        if (i != nMummy)
        {
            short nSprite2 = MummyList[i].nSprite;
            if (sprite[nSprite2].statnum != 102) {
                continue;
            }

            if (MummyList[i].nAction != 5) {
                continue;
            }

            int x = klabs(sprite[nSprite2].x - sprite[nSprite].x) >> 8;
            int y = klabs(sprite[nSprite2].y - sprite[nSprite].y) >> 8;

            if (x <= 20 && y <= 20)
            {
                if (cansee(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z - 8192, sprite[nSprite].sectnum,
                          sprite[nSprite2].x, sprite[nSprite2].y, sprite[nSprite2].z - 8192, sprite[nSprite2].sectnum))
                {
                    sprite[nSprite2].cstat = 0;
                    MummyList[i].nAction = 6;
                    MummyList[i].B = 0;
                }
            }
        }
    }
}

static void KexMummyPlayDeathSound(short nSprite)
{
    static char const *const deathSounds[] = {
        "m_deth_kex1", "m_deth_kex2", "m_deth_kex3"
    };
    unsigned const choice = KexRandomMax(ARRAY_SIZE(deathSounds));
    int const sound = KexSound_Get(deathSounds[choice]);
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexMummyTossGibs(short nSprite)
{
    // mummy_death.tsc tosses one gib1, one gib2, two gib3 and two gib4,
    // while A_GibDeath owns the generic effect and its delayed item callback.
    static const int gibTiles[] = { 7812, 7813, 7814, 7814, 7815, 7815 };

    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);
}

static void KexMummyDestroy(short nMummy, short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(MummyList[nMummy].H);
    sprite[nSprite].cstat = 0x8000;
    mydeletesprite(nSprite);
}

static void KexMummyImmediateGib(short nMummy, short nSprite, short nKiller)
{
    // Compatibility fallback for an old conversion whose MUMMY.SEQ does not
    // expose the exact retail cadence.
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexMummyPlayDeathSound(nSprite);
    KexMummyTossGibs(nSprite);
    KexMummyDestroy(nMummy, nSprite);
}

static void KexMummyBeginGib(short nMummy, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexMummyPlayDeathSound(nSprite);
    KexMummyTossGibs(nSprite);

    // The retail host is destroyed immediately, but A_GibDeath leaves a
    // generic effect alive for 19 ticks and calls A_RandomItem on tick 18.
    // Keep this invisible host as that timer so saves preserve the callback.
    MummyList[nMummy].nAction = 8;
    MummyList[nMummy].B = 0;
    MummyList[nMummy].G = nKiller;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
}

int KexMummyOwnsDeathDrop(int nSprite)
{
    if (!g_kexMode)
        return 0;

    for (int nMummy = 0; nMummy < nMummies; nMummy++)
        if (MummyList[nMummy].nSprite == nSprite
            && (MummyList[nMummy].nAction == 4
                || MummyList[nMummy].nAction == 8))
            return 1;
    return 0;
}

void FuncMummy(int a, int nDamage, int nRun)
{
    short nMummy = RunData[nRun].nVal;
    assert(nMummy >= 0 && nMummy < kMaxMummies);

    short nTarget = UpdateEnemy(&MummyList[nMummy].nTarget);

    short nSprite = MummyList[nMummy].nSprite;
    short nAction = MummyList[nMummy].nAction;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Mummy\n", nMessage);
            break;
        }

        case 0x20000:
        {
            bool const bExactMummy = KexMummyExactSequence();

            // Invisible stand-in for Fx_Gibs01. Its sole gameplay callback is
            // A_RandomItem on tick 18, after which the effect destroys itself.
            if (g_kexMode && nAction == 8)
            {
                if (MummyList[nMummy].B >= 18)
                {
                    if (!seq_IsLoaded(kSeqKexGibDeath))
                        KexDrop_RandomItemAt(sprite[nSprite].sectnum,
                            sprite[nSprite].x, sprite[nSprite].y,
                            sprite[nSprite].z - 64 * 16 * 6,
                            MummyList[nMummy].G);
                    KexMummyDestroy(nMummy, nSprite);
                }
                else
                    MummyList[nMummy].B++;
                return;
            }

            if ((unsigned)nAction >= ARRAY_SIZE(ActionSeq))
                return;

            Gravity(nSprite);

            int nSeq = SeqOffsets[kSeqMummy] + ActionSeq[nAction].a;
            // Exact KEX groups are longer than the stock sequence. A save made
            // with either data set remains safe if the loose file is removed or
            // regenerated with the other cadence.
            if (MummyList[nMummy].B < 0
                || MummyList[nMummy].B >= SeqSize[nSeq])
                MummyList[nMummy].B = 0;
            short const nRenderedFrame = MummyList[nMummy].B;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nRenderedFrame);

            short nFrame = SeqBase[nSeq] + nRenderedFrame;
            short nFrameFlag = FrameFlag[nFrame];

            // The retail melee is thirteen frames, with its sound on frame 3
            // and one 20-point hit on frame 11. Suppress the DOS sequence's
            // frame-2 sound while this exact KEX timeline owns the callbacks.
            bool const controllerOwnsCallbacks = g_kexMode
                && (nAction == 2
                    || (bExactMummy
                        && (nAction == 3 || nAction == 4 || nAction == 7)));
            if (!controllerOwnsCallbacks)
                seq_MoveSequence(nSprite, nSeq, nRenderedFrame);

            bool bVal = false;

            MummyList[nMummy].B++;
            if (MummyList[nMummy].B >= SeqSize[nSeq])
            {
                MummyList[nMummy].B = 0;

                bVal = true;
            }

            if (nTarget != -1 && nAction < 4)
            {
                if ((!sprite[nTarget].cstat) && nAction)
                {
                    MummyList[nMummy].nAction = 0;
                    MummyList[nMummy].B = 0;
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                }
            }

            int nMov = MoveCreatureWithCaution(nSprite);

            if (nAction > 7)
                return;

            switch (nAction)
            {
                case 0:
                {
                    if (g_kexMode || (MummyList[nMummy].F & 0x1F) == (totalmoves & 0x1F))
                    {
                        sprite[nSprite].cstat = 0x101;

                        if (nTarget < 0)
                        {
                            int nTarget = FindPlayer(nSprite, 100);
                            if (nTarget >= 0)
                            {
                                D3PlayFX(StaticSound[kSoundMummyChant0], nSprite);
                                MummyList[nMummy].B = 0;
                                MummyList[nMummy].nTarget = nTarget;
                                MummyList[nMummy].nAction = 1;
                                MummyList[nMummy].G = g_kexMode ? 0 : 90;

                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                                sprite[nSprite].yvel = sintable[sprite[nSprite].ang] >> 2; // NOTE no angle masking in original code
                            }
                        }
                    }
                    return;
                }

                case 1:
                {
                    if (g_kexMode && KexAIEnterMeleeRange(nSprite, nTarget, 48))
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        MummyList[nMummy].nAction = 2;
                        MummyList[nMummy].B = 0;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        return;
                    }

                    if (MummyList[nMummy].G > 0)
                    {
                        MummyList[nMummy].G--;
                    }

                    if (g_kexMode
                        || (MummyList[nMummy].F & 0x1F) == (totalmoves & 0x1F))
                    {
                        sprite[nSprite].cstat = 0x101;

                        PlotCourseToSprite(nSprite, nTarget);

                        if (MummyList[nMummy].nAction == 1)
                        {
                            if ((g_kexMode && MummyList[nMummy].G <= 0)
                                || (!g_kexMode && RandomBit()))
                            {
                                if (cansee(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z - GetSpriteHeight(nSprite), sprite[nSprite].sectnum,
                                    sprite[nTarget].x, sprite[nTarget].y, sprite[nTarget].z - GetSpriteHeight(nTarget), sprite[nTarget].sectnum))
                                {
                                    MummyList[nMummy].nAction = 3;
                                    MummyList[nMummy].B = 0;
                                    if (g_kexMode)
                                        MummyList[nMummy].G = KexDifficulty_AICooldown();

                                    sprite[nSprite].xvel = 0;
                                    sprite[nSprite].yvel = 0;
                                    return;
                                }
                            }
                        }
                    }

                    // loc_2B5A8
                    if (!MummyList[nMummy].B)
                    {
                        sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                        sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                    }

                    if (sprite[nSprite].xvel || sprite[nSprite].yvel)
                    {
                        if (sprite[nSprite].xvel > 0)
                        {
                            sprite[nSprite].xvel -= 1024;
                            if (sprite[nSprite].xvel < 0) {
                                sprite[nSprite].xvel = 0;
                            }
                        }
                        else if (sprite[nSprite].xvel < 0)
                        {
                            sprite[nSprite].xvel += 1024;
                            if (sprite[nSprite].xvel > 0) {
                                sprite[nSprite].xvel = 0;
                            }
                        }

                        if (sprite[nSprite].yvel > 0)
                        {
                            sprite[nSprite].yvel -= 1024;
                            if (sprite[nSprite].yvel < 0) {
                                sprite[nSprite].yvel = 0;
                            }
                        }
                        else if (sprite[nSprite].yvel < 0)
                        {
                            sprite[nSprite].yvel += 1024;
                            if (sprite[nSprite].yvel > 0) {
                                sprite[nSprite].yvel = 0;
                            }
                        }
                    }

                    if (nMov)
                    {
                        switch (nMov & 0xC000)
                        {
                            case 0x8000:
                            {
                                sprite[nSprite].ang = (sprite[nSprite].ang + ((RandomWord() & 0x3FF) + 1024)) & kAngleMask;
                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                                sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                                return;
                            }

                            case 0xC000:
                            {
                                if ((nMov & 0x3FFF) == nTarget)
                                {
                                    int nAngle = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                                    if (AngleDiff(sprite[nSprite].ang, nAngle) < 64)
                                    {
                                        MummyList[nMummy].nAction = 2;
                                        MummyList[nMummy].B = 0;

                                        sprite[nSprite].xvel = 0;
                                        sprite[nSprite].yvel = 0;
                                    }
                                }
                                return;
                            }
                        }
                    }

                    break;
                }

                case 2:
                {
                    if (nTarget == -1)
                    {
                        MummyList[nMummy].nAction = 0;
                        MummyList[nMummy].B = 0;
                    }
                    else
                    {
                        if (g_kexMode)
                        {
                            PlotCourseToSprite(nSprite, nTarget);
                            if (nRenderedFrame == 3)
                            {
                                int const s = KexSound_Get("m_nstaff");
                                if (s >= 0) D3PlayFX(s, nSprite);
                            }
                            if (nRenderedFrame == 11
                                && KexMeleeAttackRange(nSprite, nTarget, 48))
                                runlist_DamageEnemy(nTarget, nSprite, 20);
                            if (nRenderedFrame == 12)
                            {
                                // A_CheckMelee(..., 48) only leaves on a miss;
                                // otherwise the thirteen-frame attack loops.
                                if (KexMeleeRange(nSprite, nTarget, 48))
                                    MummyList[nMummy].B = 0;
                                else
                                {
                                    MummyList[nMummy].nAction = 1;
                                    MummyList[nMummy].B = 0;
                                    sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                                    sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                                }
                            }
                        }
                        else if (PlotCourseToSprite(nSprite, nTarget) >= 1024)
                        {
                            MummyList[nMummy].nAction = 1;
                            MummyList[nMummy].B = 0;
                        }
                        else if (nFrameFlag & 0x80)
                            runlist_DamageEnemy(nTarget, nSprite, 5);
                    }
                    return;
                }

                case 3:
                {
                    if (bExactMummy)
                    {
                        if (nRenderedFrame == 0)
                        {
                            int const sound = KexSound_Get("m_watha");
                            if (sound >= 0)
                                D3PlayFX(sound, nSprite);
                        }
                        else if (nRenderedFrame == 6)
                        {
                            int const sound = KexSound_Get("m_chant0");
                            if (sound >= 0)
                                D3PlayFX(sound, nSprite);
                            BuildKexAIFireProjectile(nSprite, 9, nTarget,
                                                     2, 96, 12, 0);
                        }

                        if (bVal)
                        {
                            MummyList[nMummy].B = 0;
                            MummyList[nMummy].nAction = 1;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        }
                        return;
                    }

                    if (bVal)
                    {
                        MummyList[nMummy].B = 0;
                        if (g_kexMode)
                        {
                            MummyList[nMummy].nAction = 1;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        }
                        else
                        {
                            MummyList[nMummy].nAction = 0;
                            MummyList[nMummy].G = 100;
                            MummyList[nMummy].nTarget = -1;
                        }
                        return;
                    }
                    else if (nFrameFlag & 0x80)
                    {
                        // mummy_range_attack has no A_Quake callback in KEX;
                        // retain the native impact shake only for classic play.
                        if (!g_kexMode)
                            SetQuake(nSprite, 100);

                        int const nBullet = g_kexMode
                            ? BuildKexAIFireProjectile(nSprite, 9, nTarget,
                                                      2, 96, 12, 0)
                            : BuildBullet(nSprite, 9, 0, 0, -15360,
                                          sprite[nSprite].ang,
                                          nTarget + 10000, 1);
                        // Resurrection is a DOS-only mechanic. No remaster
                        // Mummy animation or script invokes this callback.
                        if (!g_kexMode)
                            CheckMummyRevive(nMummy);

                        if (!g_kexMode && nBullet > -1)
                        {
                            if (!RandomSize(3))
                            {
                                // FIXME CHECKME - nBullet & 0xFFFF can be -1. Original code doesn't handle this??

                                SetBulletEnemy(nBullet, nTarget);
                                int nBulletSprite = GetBulletSprite(nBullet);
                                sprite[nBulletSprite].pal = 5;
                            }
                        }
                    }
                    return;
                }

                case 4:
                {
                    if (bExactMummy)
                    {
                        if (nRenderedFrame == 0)
                        {
                            KexMummyPlayDeathSound(nSprite);
                            // A_MummyDeathEffect changes actor scale 1.5 -> 1.25.
                            sprite[nSprite].xrepeat = 50;
                            sprite[nSprite].yrepeat = 50;
                        }
                        if (nRenderedFrame == 24)
                            sprite[nSprite].cstat &= ~0x101; // A_Die
                        if (nRenderedFrame == 32)
                            KexDrop_RandomItem(nSprite, MummyList[nMummy].G);
                    }

                    if (bVal)
                    {
                        MummyList[nMummy].B = 0;
                        MummyList[nMummy].nAction = 5;
                    }
                    return;
                }

                case 5:
                {
                    MummyList[nMummy].B = 0;
                    return;
                }

                case 6:
                {
                    if (bExactMummy)
                    {
                        // A legacy save may contain the native resurrection
                        // state, but retail KEX has no path back from a corpse.
                        MummyList[nMummy].nAction = 5;
                        MummyList[nMummy].B = 0;
                        return;
                    }

                    if (bVal)
                    {
                        sprite[nSprite].cstat = 0x101;

                        MummyList[nMummy].nAction = 0;
                        MummyList[nMummy].nHealth = 300;
                        MummyList[nMummy].nTarget = -1;

                        nCreaturesLeft++;
                    }
                    return;
                }

                case 7:
                {
                    if (nMov & 0x20000)
                    {
                        sprite[nSprite].xvel >>= 1;
                        sprite[nSprite].yvel >>= 1;
                    }

                    if (bVal)
                    {
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].cstat = 0x101;

                        MummyList[nMummy].B = 0;
                        if (g_kexMode)
                        {
                            MummyList[nMummy].nAction = 1;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        }
                        else
                        {
                            MummyList[nMummy].nAction = 0;
                            MummyList[nMummy].nTarget = -1;
                        }
                    }

                    return;
                }
            }

            return;
        }

        case 0x90000:
        {
            if (nAction == 8 || (unsigned)nAction >= ARRAY_SIZE(ActionSeq))
                return;
            int const nSeq = SeqOffsets[kSeqMummy] + ActionSeq[nAction].a;
            if (MummyList[nMummy].B < 0
                || MummyList[nMummy].B >= SeqSize[nSeq])
                MummyList[nMummy].B = 0;
            seq_PlotSequence(a & 0xFFFF, nSeq, MummyList[nMummy].B,
                             ActionSeq[nAction].b);
            return;
        }

        case 0xA0000:
        {
            if (MummyList[nMummy].nHealth <= 0)
                return;

            nDamage = runlist_CheckRadialDamage(nSprite);
            // fall through to 0x80000
            fallthrough__;
        }
        case 0x80000:
        {
            if (nDamage <= 0)
                return;

            if (MummyList[nMummy].nHealth <= 0) {
                return;
            }

            MummyList[nMummy].nHealth -= nDamage;

            if (MummyList[nMummy].nHealth <= 0)
            {
                int const nRemainingHealth = MummyList[nMummy].nHealth;
                short const nKiller = nMessage == 0xA0000
                    ? (short)nRadialOwner : (short)(a & 0xFFFF);
                int const nDamageType = nMessage == 0xA0000
                    ? kKexDamageExplosion : nKexDamageType;
                MummyList[nMummy].nHealth = 0;
                nCreaturesLeft--;

                // Radial dispatch bypasses runlist_DamageEnemy's post-death
                // hook, so report this kill here. Direct hits are reported by
                // that hook after this synchronous actor callback returns.
                if (g_kexMode && nMessage == 0xA0000)
                    KexTrigger_Died(nSprite);

                if (KexMummyExactSequence())
                {
                    if (KexMummyShouldGib(nRemainingHealth, nDamageType))
                    {
                        KexMummyBeginGib(nMummy, nSprite, nKiller);
                    }
                    else
                    {
                        // InflictDamage clears shootable immediately. A_Die
                        // clears solid later, on expanded death tick 24.
                        sprite[nSprite].cstat &= ~0x100;
                        MummyList[nMummy].B = 0;
                        MummyList[nMummy].nAction = 4;
                        MummyList[nMummy].G = nKiller;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].zvel = 0;
                        sprite[nSprite].z = sector[sprite[nSprite].sectnum].floorz;
                    }
                    return;
                }

                // Stock/classic cadence and the old KEX conversion retain the
                // pre-existing immediate-death fallback.
                sprite[nSprite].cstat &= 0xFEFE;
                DropMagic(nSprite);
                if (g_kexMode)
                {
                    if (nMessage == 0xA0000)
                        KexDrop_RandomItem(nSprite, nKiller);
                    KexMummyImmediateGib(nMummy, nSprite, nKiller);
                    return;
                }

                MummyList[nMummy].B = 0;
                MummyList[nMummy].nAction = 4;

                sprite[nSprite].xvel = 0;
                sprite[nSprite].yvel = 0;
                sprite[nSprite].zvel = 0;
                sprite[nSprite].z = sector[sprite[nSprite].sectnum].floorz;
            }
            else
            {
                short const nSource = nMessage == 0xA0000
                    ? (short)nRadialOwner : (short)(a & 0xFFFF);
                if (g_kexMode && nSource >= 0 && sprite[nSource].statnum < 199)
                    MummyList[nMummy].nTarget = nSource;

                bool const bPainRoll = g_kexMode
                    ? KexDifficulty_PainRoll(64) : !RandomSize(2);
                if (bPainRoll && (!g_kexMode || MummyList[nMummy].nAction == 1))
                {
                    MummyList[nMummy].nAction = 7;
                    MummyList[nMummy].B = 0;

                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                }
            }

            return;
        }
    }
}

class MummyLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void MummyLoadSave::Load()
{
    Read(&nMummies, sizeof(nMummies));
    Read(&MummyList, sizeof(MummyList[0]) * nMummies);
}

void MummyLoadSave::Save()
{
    Write(&nMummies, sizeof(nMummies));
    Write(&MummyList, sizeof(MummyList[0]) * nMummies);
}

static MummyLoadSave* myLoadSave;

void MummyLoadSaveConstruct()
{
    myLoadSave = new MummyLoadSave();
}
