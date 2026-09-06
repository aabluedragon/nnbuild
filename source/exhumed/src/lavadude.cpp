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

#include "engine.h"
#include "lavadude.h"
#include "random.h"
#include "runlist.h"
#include "sequence.h"
#include "exhumed.h"
#include "init.h"
#include "move.h"
#include "trigdat.h"
#include "bullet.h"
#include "player.h"
#include "sound.h"
#include "save.h"
#include "kexbreak.h" // VibeBuild32: KEX Mantis death burst
#include "kexinv.h"
#include <assert.h>

#define kMaxLavas   20

struct Lava
{
    short nSprite;
    short nRun;
    short nAction;
    short nTarget;
    short nHealth;
    short nFrame;
    short nChannel;
};

enum
{
    kKexMantisAttackPrefix,
    kKexMantisAttackSaturn,
    kKexMantisAttackPSX,
};

static actionSeq ActionSeq[] = {
    {0,  1},
    {0,  1},
    {1,  0},
    {10, 0},
    {19, 0},
    {28, 1},
    {29, 1},
    {33, 0},
    {42, 1}
};

// KEX needs the two persistent A_MantisThink scratch parameters in addition
// to the attack variant, repeat counter and local timeline. Pack all of them
// into nFrame so the original Lava save-record layout remains unchanged.
// scratch0 and scratch1 are mutually exclusive in the retail script, so their
// reachable states fit in one three-bit code: 0, scratch0 1..5, scratch1 1..2.
enum
{
    kKexMantisFrameMask = 63,
    kKexMantisModeShift = 6,
    kKexMantisModeMask = 3,
    kKexMantisCountShift = 8,
    kKexMantisCountMask = 15,
    kKexMantisScratchShift = 12,
    kKexMantisScratchMask = 7,
};

static int s_kexMantisTest = -1;
static int s_kexMantisTestPhase;
static int s_kexMantisTestTicks;
static short s_kexMantisTestLava = -1;
static short s_kexMantisTestPlayer = -1;
static short s_kexMantisTestInvincibility;
static int s_kexMantisTestOkay;
static unsigned s_kexMantisTestFailures;
static int s_kexMantisTestSpeed;
static int s_kexMantisTestSaturnShots;
static int s_kexMantisTestSaturnFrames;
static int s_kexMantisTestSaturnProjectiles;
static int s_kexMantisTestPSXShots;
static int s_kexMantisTestPSXFrames;
static int s_kexMantisTestPSXProjectiles;
static uint64_t s_kexMantisTestPeakCallbacks;
static uint64_t s_kexMantisTestRiseCallbacks;
static uint64_t s_kexMantisTestSubmergeCallbacks;
static uint64_t s_kexMantisTestSplashCallbacks;
static int s_kexMantisTestDeathBursts;
static int s_kexMantisTestDeathTrigger;
static int s_kexMantisTestDeathDestroyed;

short LavaCount = 0;

Lava LavaList[kMaxLavas];

static int KexMantisAttackFrame(short nLava)
{
    return (uint16_t)LavaList[nLava].nFrame & kKexMantisFrameMask;
}

static int KexMantisAttackMode(short nLava)
{
    return ((uint16_t)LavaList[nLava].nFrame >> kKexMantisModeShift)
        & kKexMantisModeMask;
}

static int KexMantisAttackCount(short nLava)
{
    return ((uint16_t)LavaList[nLava].nFrame >> kKexMantisCountShift)
        & kKexMantisCountMask;
}

static int KexMantisScratchCode(short nLava)
{
    return ((uint16_t)LavaList[nLava].nFrame >> kKexMantisScratchShift)
        & kKexMantisScratchMask;
}

static int KexMantisScratch0(short nLava)
{
    int const code = KexMantisScratchCode(nLava);
    return code >= 1 && code <= 5 ? code : 0;
}

static int KexMantisScratch1(short nLava)
{
    int const code = KexMantisScratchCode(nLava);
    return code >= 6 ? code - 5 : 0;
}

static void KexMantisSetPackedState(short nLava, int nFrame, int nMode,
                                     int nCount, int nScratch)
{
    assert((unsigned)nFrame <= kKexMantisFrameMask);
    assert((unsigned)nMode <= kKexMantisModeMask);
    assert((unsigned)nCount <= kKexMantisCountMask);
    assert((unsigned)nScratch <= kKexMantisScratchMask);
    LavaList[nLava].nFrame = (short)(nFrame
        | (nMode << kKexMantisModeShift)
        | (nCount << kKexMantisCountShift)
        | (nScratch << kKexMantisScratchShift));
}

static void KexMantisSetFrame(short nLava, int nFrame)
{
    KexMantisSetPackedState(nLava, nFrame, KexMantisAttackMode(nLava),
        KexMantisAttackCount(nLava), KexMantisScratchCode(nLava));
}

static void KexMantisSetScratch(short nLava, int scratch0, int scratch1)
{
    assert((unsigned)scratch0 <= 5);
    assert((unsigned)scratch1 <= 2);
    assert(!scratch0 || !scratch1);
    int const code = scratch0 ? scratch0 : (scratch1 ? scratch1 + 5 : 0);
    KexMantisSetPackedState(nLava, KexMantisAttackFrame(nLava),
        KexMantisAttackMode(nLava), KexMantisAttackCount(nLava), code);
}

static void KexMantisSetAttackState(short nLava, int nMode, int nFrame,
                                     int nCount)
{
    KexMantisSetPackedState(nLava, nFrame, nMode, nCount,
                             KexMantisScratchCode(nLava));
}

static void KexMantisSetVelocity(short nSprite)
{
    if (!g_kexMode)
    {
        sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
        sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
        return;
    }

    // AI_Mantis moveSpeed is 64 KEX units/tick. The controller passes this
    // stored vector to movesprite with <<8; converted XY is six times KEX.
    int constexpr nWorldSpeed = 64 * 6;
    sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * nWorldSpeed) >> 8;
    sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * nWorldSpeed) >> 8;
}

static void KexMantisPlaySound(short nSprite, char const *name)
{
    int const sound = KexSound_Get(name);
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexMantisPlayAttackSound(short nSprite)
{
    KexMantisPlaySound(nSprite, "lav_fire");
}

static void KexMantisSpawnDeathBurst(short nLava, short nSprite)
{
    if (s_kexMantisTest >= 0 && nLava == s_kexMantisTestLava)
    {
        s_kexMantisTestDeathBursts++;
        return;
    }
    KexBouncyBalls_Spawn(sprite[nSprite].sectnum, sprite[nSprite].x,
                         sprite[nSprite].y, sprite[nSprite].z, 192);
}

static void KexMantisBeginDeath(short nLava, short nSprite)
{
    LavaList[nLava].nAction = 5;
    LavaList[nLava].nFrame = 0; // death uses an unpacked 0..52 timeline

    // mantis_death frame zero: trigger, sound, unblock, then the first of
    // three authored A_MantisExplode callbacks (frames 0, 5 and 8).
    if (s_kexMantisTest >= 0 && nLava == s_kexMantisTestLava)
        s_kexMantisTestDeathTrigger++;
    else if (LavaList[nLava].nChannel >= 0)
        runlist_ChangeChannel(LavaList[nLava].nChannel, 1);
    KexMantisPlaySound(nSprite, "lav_death");
    sprite[nSprite].cstat &= ~0x101;
    KexMantisSpawnDeathBurst(nLava, nSprite);
}

static void KexMantisDestroy(short nLava, short nSprite)
{
    sprite[nSprite].cstat |= 0x8000;
    if (s_kexMantisTest >= 0 && nLava == s_kexMantisTestLava)
        s_kexMantisTestDeathDestroyed = 1;
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(LavaList[nLava].nRun);
    mydeletesprite(nSprite);
}

static void KexMantisRecordCallback(short nLava, int nAction, int nFrame)
{
    if (s_kexMantisTest < 0 || nLava != s_kexMantisTestLava)
        return;

    uint64_t const bit = UINT64_C(1) << nFrame;
    switch (nAction)
    {
        case 1: s_kexMantisTestPeakCallbacks |= bit; break;
        case 2: s_kexMantisTestRiseCallbacks |= bit; break;
        case 7: s_kexMantisTestSubmergeCallbacks |= bit; break;
        case 8: s_kexMantisTestSplashCallbacks |= bit; break;
    }
}

static void KexMantisEnterFrame(short nLava, short nSprite, short nTarget,
                                 int nAction, int nFrame)
{
    switch (nAction)
    {
        case 1: // mantis_peak
            if (nFrame == 0)
            {
                sprite[nSprite].cstat &= ~0x8000;
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 2)
            {
                KexMantisPlaySound(nSprite, "lav_act1");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 24)
            {
                KexMantisPlaySound(nSprite, "amb_lav2");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 37)
            {
                KexMantisPlaySound(nSprite, "amb_lav1");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 61)
            {
                sprite[nSprite].cstat |= 0x8000;
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            break;

        case 2: // mantis_rise
            if (nFrame == 0)
            {
                if (nTarget >= 0)
                    PlotCourseToSprite(nSprite, nTarget);
                sprite[nSprite].cstat &= ~0x8000;
                if (g_kexMode)
                    KexQuake(60); // exact mantis_rise A_Quake duration
                else
                    SetQuake(nSprite, 60);
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 4)
            {
                KexMantisPlaySound(nSprite, "amb_lav1");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 12)
            {
                KexMantisPlaySound(nSprite, "lav_act4");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 14)
            {
                KexMantisPlaySound(nSprite, "lav_deth");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 31)
            {
                sprite[nSprite].cstat |= 0x101;
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            break;

        case 4: // mantis_pain
            if (nFrame == 0)
            {
                sprite[nSprite].cstat &= ~0x101;
                KexMantisPlaySound(nSprite, "lav_hurt");
            }
            break;

        case 7: // mantis_submerge
            if (nFrame == 3)
            {
                sprite[nSprite].cstat &= ~0x101;
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            break;

        case 8: // mantis_splash
            if (nFrame == 4)
            {
                KexMantisPlaySound(nSprite, "amb_lav2");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 10)
            {
                KexMantisPlaySound(nSprite, "lav_act4");
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            else if (nFrame == 20)
            {
                sprite[nSprite].cstat |= 0x8000;
                KexMantisRecordCallback(nLava, nAction, nFrame);
            }
            break;
    }
}

static void KexMantisBeginAction(short nLava, short nSprite, short nTarget,
                                  int nAction)
{
    LavaList[nLava].nAction = (short)nAction;
    KexMantisSetPackedState(nLava, 0, 0, 0,
                             KexMantisScratchCode(nLava));
    KexMantisEnterFrame(nLava, nSprite, nTarget, nAction, 0);
}

static int KexMantisSourceCenterZ(short nSprite, bool bStepHeight)
{
    // AI_Mantis is 384 KEX units tall with a 0.25-unit step height. KEX Z is
    // converted to Build at 16 * WORLD (six), and Build's axis points down.
    return sprite[nSprite].z - (384 * 16 * 6) / 2
        - (bStepHeight ? 24 : 0);
}

static int KexMantisTargetCenterZ(short nTarget)
{
    // Obj_Player's authored height is 160, hence the familiar 7680 offset.
    if (sprite[nTarget].statnum == 100)
        return sprite[nTarget].z - (160 * 16 * 6) / 2;
    return sprite[nTarget].z - (GetSpriteHeight(nTarget) >> 1);
}

static bool KexMantisCanSeeTarget(short nSprite, short nTarget,
                                  int nMaxDistance)
{
    if ((unsigned)nTarget >= (unsigned)kMaxSprites
        || sprite[nTarget].sectnum < 0 || sprite[nTarget].sectnum >= kMaxSectors)
        return false;

    int const sourceZ = KexMantisSourceCenterZ(nSprite, true);
    int const targetZ = KexMantisTargetCenterZ(nTarget);
    if (nMaxDistance > 0)
    {
        int64_t const dx = (int64_t)sprite[nTarget].x - sprite[nSprite].x;
        int64_t const dy = (int64_t)sprite[nTarget].y - sprite[nSprite].y;
        int64_t const dz = ((int64_t)targetZ - sourceZ) / 16;
        int64_t const distance = (int64_t)nMaxDistance * 6;
        if (dx * dx + dy * dy + dz * dz > distance * distance)
            return false;
    }

    return cansee(sprite[nSprite].x, sprite[nSprite].y, sourceZ,
                  sprite[nSprite].sectnum, sprite[nTarget].x,
                  sprite[nTarget].y, targetZ, sprite[nTarget].sectnum) != 0;
}

static short KexMantisFindPlayer(short nSprite)
{
    int constexpr sightDistance = 3000 * 6;
    int const sourceZ = KexMantisSourceCenterZ(nSprite, false);
    for (int i = 0; i < nTotalPlayers; i++)
    {
        short const nTarget = PlayerList[i].nSprite;
        if ((unsigned)nTarget >= (unsigned)kMaxSprites
            || !(sprite[nTarget].cstat & 0x101)
            || (sprite[nTarget].cstat & 0x8000)
            || sprite[nTarget].sectnum < 0
            || klabs(sprite[nTarget].x - sprite[nSprite].x) >= sightDistance
            || klabs(sprite[nTarget].y - sprite[nSprite].y) >= sightDistance)
            continue;

        if (cansee(sprite[nSprite].x, sprite[nSprite].y, sourceZ,
                   sprite[nSprite].sectnum, sprite[nTarget].x,
                   sprite[nTarget].y, KexMantisTargetCenterZ(nTarget),
                   sprite[nTarget].sectnum))
            return nTarget;
    }
    return -1;
}

static void KexMantisChooseWakeState(short nLava, short nSprite,
                                      short nTarget, int nRandCount,
                                      int nCoin)
{
    int const scratch0 = KexMantisScratch0(nLava);
    int const scratch1 = KexMantisScratch1(nLava);
    nRandCount = min(nRandCount, 2);
    sprite[nSprite].xvel = sprite[nSprite].yvel = 0;

    if (scratch1 >= 2
        || (scratch0 < nRandCount + 3 && (nCoin & 1) != 0))
    {
        KexMantisBeginAction(nLava, nSprite, nTarget, 1);
        KexMantisSetScratch(nLava, scratch0 + 1, 0);
    }
    else
    {
        KexMantisBeginAction(nLava, nSprite, nTarget, 2);
        KexMantisSetScratch(nLava, 0, scratch1 + 1);
    }
}

static bool KexMantisThink(short nLava, short nSprite, short nTarget)
{
    if (nTarget < 0 || (uint8_t)RandomByte() > 32)
        return false;
    if (!KexMantisCanSeeTarget(nSprite, nTarget, 2000))
        return false;

    int const randCount = RandomSize(3);
    int const coin = (uint8_t)RandomByte();
    KexMantisChooseWakeState(nLava, nSprite, nTarget, randCount, coin);
    return true;
}

static void KexMantisTurnAway(short nSprite)
{
    sprite[nSprite].ang = (sprite[nSprite].ang
        + ((RandomWord() & 0x3ff) + 1024)) & kAngleMask;
    KexMantisSetVelocity(nSprite);
}

static int KexMantisMove(short nSprite)
{
    int const oldX = sprite[nSprite].x;
    int const oldY = sprite[nSprite].y;
    int const oldZ = sprite[nSprite].z;
    short const oldSector = sprite[nSprite].sectnum;
    int const oldFloor = getflorzofslope(oldSector, oldX, oldY);
    int const collision = movesprite(nSprite,
        KexScaleCreatureMove(nSprite, sprite[nSprite].xvel << 8),
        KexScaleCreatureMove(nSprite, sprite[nSprite].yvel << 8),
        0, 0, 0, CLIPMASK0);

    short const newSector = sprite[nSprite].sectnum;
    if (newSector != oldSector)
    {
        int const newFloor = getflorzofslope(newSector, sprite[nSprite].x,
                                              sprite[nSprite].y);
        bool const leaveLava = SectDamage[oldSector] && !SectDamage[newSector];
        bool const invalid = (SectFlag[newSector] & kSectUnderwater)
            || leaveLava || newFloor - oldFloor > 512 * 16 * 6;
        if (invalid)
        {
            sprite[nSprite].x = oldX;
            sprite[nSprite].y = oldY;
            sprite[nSprite].z = oldZ;
            mychangespritesect(nSprite, oldSector);
            return 0x8000;
        }
    }
    return collision;
}

static void KexMantisChaseTick(short nLava, short nSprite, short nTarget)
{
    int const timer = KexMantisAttackFrame(nLava) & 3;
    if (nTarget < 0)
    {
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        if (timer & 1)
        {
            nTarget = KexMantisFindPlayer(nSprite);
            if (nTarget >= 0)
            {
                LavaList[nLava].nTarget = nTarget;
                KexMantisSetFrame(nLava, 0);
                // ChangeAnim(chase) executes A_MantisThink immediately.
                KexMantisThink(nLava, nSprite, nTarget);
                return;
            }
        }
        KexMantisSetFrame(nLava, (timer + 1) & 1);
        return;
    }

    if (!(timer & 1))
    {
        PlotCourseToSprite(nSprite, nTarget);
        KexMantisSetVelocity(nSprite);
    }

    int const nextTimer = (timer + 1) & 3;
    KexMantisSetFrame(nLava, nextTimer);
    if (nextTimer == 0 && KexMantisThink(nLava, nSprite, nTarget))
        return;

    int const collision = KexMantisMove(nSprite);
    if (collision & 0xc000)
        KexMantisTurnAway(nSprite);
}

static void KexMantisGoChase(short nLava, short nSprite, short nTarget)
{
    KexMantisBeginAction(nLava, nSprite, nTarget, 0);
    if (nTarget >= 0)
        KexMantisThink(nLava, nSprite, nTarget);
}

static void KexMantisGoSubmerge(short nLava)
{
    short const nSprite = LavaList[nLava].nSprite;
    KexMantisBeginAction(nLava, nSprite, LavaList[nLava].nTarget, 7);
}

static void KexMantisBeginAttack(short nLava, short nSprite, short nTarget)
{
    LavaList[nLava].nAction = 3;
    KexMantisSetAttackState(nLava, kKexMantisAttackPrefix, 0, 0);
    if (nTarget >= 0)
        PlotCourseToSprite(nSprite, nTarget);
}

static int KexMantisAttackRenderFrame(short nLava)
{
    int const nMode = KexMantisAttackMode(nLava);
    int const nFrame = KexMantisAttackFrame(nLava);
    if (nMode == kKexMantisAttackPSX)
        return 4 + clamp(nFrame, 0, 8);
    if (nMode == kKexMantisAttackSaturn)
    {
        // mantis_attack_saturn: frame 4 for one tick, then source frames
        // 5/10/11 for their authored four-tick delays. The following check
        // callback changes animation immediately and is not itself displayed.
        static uint8_t const render[] = {
            4, 5, 5, 5, 5, 10, 10, 10, 10, 11, 11, 11, 11
        };
        return render[clamp(nFrame, 0, (int)ARRAY_SIZE(render) - 1)];
    }
    // mantis_attack's three one-tick lead-in frames precede
    // A_MantisAttackThink; the think frame changes animation immediately.
    return clamp(nFrame, 0, 2);
}

static int KexMantisRenderFrame(short nLava, int nAction)
{
    if (nAction == 3)
        return KexMantisAttackRenderFrame(nLava);
    if (nAction == 5)
    {
        int const frame = LavaList[nLava].nFrame;
        if (frame < 9)
            return frame;       // mantis_pain 0..8
        if (frame < 33)
            return frame - 9;   // mantis_submerge 0..23
        return frame - 33;      // mantis_splash 0..19 (20 destroys)
    }
    if (nAction == 0 || nAction == 1 || nAction == 2 || nAction == 4
        || nAction == 7 || nAction == 8)
        return KexMantisAttackFrame(nLava);
    return LavaList[nLava].nFrame;
}

static int KexMantisSequenceAction(short nLava, int nAction)
{
    if (nAction != 5)
        return nAction;
    int const frame = LavaList[nLava].nFrame;
    return frame < 9 ? 4 : (frame < 33 ? 7 : 8);
}

static bool KexMantisOwnsTimeline(int nAction)
{
    return nAction == 0 || nAction == 1 || nAction == 2 || nAction == 3
        || nAction == 4 || nAction == 5 || nAction == 7 || nAction == 8;
}

static void KexMantisBeginAttackVariant(short nLava, short nSprite,
                                         short nTarget)
{
    if (((uint8_t)RandomByte() & 127) == 0)
    {
        KexMantisSetAttackState(nLava, kKexMantisAttackPSX, 0, 0);
    }
    else
    {
        // kexSlaveRandomizer::Size(3) is a three-bit value. The script's
        // A_MantisCheckAttack fires the first volley before decrementing, so
        // this produces the authored two-to-nine Saturn volleys.
        KexMantisSetAttackState(nLava, kKexMantisAttackSaturn, 0,
                                1 + RandomSize(3));
        // Only the Saturn frame-zero callback faces the target. The rare PSX
        // branch deliberately retains the yaw from common attack frame zero.
        PlotCourseToSprite(nSprite, nTarget);
    }
    KexMantisPlayAttackSound(nSprite);
}

static void KexMantisTestCaptureProjectile(short nLava, short nSprite,
                                            short nTarget, int nBullet,
                                            bool bPSX, int nFrame)
{
    if (s_kexMantisTest < 0 || nLava != s_kexMantisTestLava)
        return;

    if (bPSX)
    {
        s_kexMantisTestPSXShots++;
        if (nFrame >= 1 && nFrame <= 5)
            s_kexMantisTestPSXFrames |= 1 << (nFrame - 1);
        s_kexMantisTestPSXProjectiles +=
            KexBullet_CheckMantisFireProjectile(nBullet, nSprite) != 0;
    }
    else
    {
        s_kexMantisTestSaturnShots++;
        if (nFrame >= 0 && nFrame < 31)
            s_kexMantisTestSaturnFrames |= 1 << nFrame;
        s_kexMantisTestSaturnProjectiles +=
            KexBullet_CheckAIFireProjectile(nBullet, nSprite, nTarget,
                                             8, 192, 16, 0) != 0;
    }

    // The env-only controller probe validates each launch synchronously and
    // removes it before the ordinary bullet run records can affect the map.
    if (nBullet >= 0)
        DestroyBullet((short)nBullet);
}

static void KexMantisAttackTick(short nLava, short nSprite, short nTarget)
{
    int const nMode = KexMantisAttackMode(nLava);
    int const nFrame = KexMantisAttackFrame(nLava);
    int const nCount = KexMantisAttackCount(nLava);

    if (nMode == kKexMantisAttackPrefix)
    {
        if (nFrame + 1 >= 3)
        {
            if (nTarget < 0)
                KexMantisGoSubmerge(nLava);
            else
                KexMantisBeginAttackVariant(nLava, nSprite, nTarget);
        }
        else
            KexMantisSetAttackState(nLava, nMode, nFrame + 1, nCount);
        return;
    }

    if (nMode == kKexMantisAttackPSX)
    {
        // The rare PSX animation calls A_MantisAttack on source frames 5..9.
        if (nTarget >= 0 && nFrame >= 1 && nFrame <= 5)
        {
            int const nBullet = BuildKexMantisFireProjectile(nSprite, nTarget);
            KexMantisTestCaptureProjectile(nLava, nSprite, nTarget, nBullet,
                                            true, nFrame);
        }
        if (nFrame + 1 >= 9)
            KexMantisGoSubmerge(nLava);
        else
            KexMantisSetAttackState(nLava, nMode, nFrame + 1, nCount);
        return;
    }

    // Saturn frame 5 launches one exact A_FireProjectile after frame 4's
    // one-tick face/sound cue. Frames 5/10/11 each hold for four ticks.
    if (nTarget >= 0 && nFrame == 1)
    {
        PlotCourseToSprite(nSprite, nTarget);
        int const nBullet = BuildKexAIFireProjectile(nSprite, 10, nTarget,
                                                      8, 192, 16, 0);
        KexMantisTestCaptureProjectile(nLava, nSprite, nTarget, nBullet,
                                        false, nFrame);
    }
    if (nFrame + 1 < 13)
    {
        KexMantisSetAttackState(nLava, nMode, nFrame + 1, nCount);
        return;
    }

    if (nCount <= 0 || nTarget < 0)
    {
        KexMantisGoSubmerge(nLava);
        return;
    }
    KexMantisSetAttackState(nLava, kKexMantisAttackSaturn, 0, nCount - 1);
    PlotCourseToSprite(nSprite, nTarget);
    KexMantisPlayAttackSound(nSprite);
}

static void KexMantisTimelineTick(short nLava, short nSprite, short nTarget,
                                   int nAction, int nSize)
{
    int const nFrame = KexMantisAttackFrame(nLava);
    if (nFrame + 1 < nSize)
    {
        KexMantisSetFrame(nLava, nFrame + 1);
        KexMantisEnterFrame(nLava, nSprite, nTarget, nAction, nFrame + 1);
        return;
    }

    switch (nAction)
    {
        case 1:
            KexMantisGoChase(nLava, nSprite, nTarget);
            break;
        case 2:
            KexMantisBeginAttack(nLava, nSprite, nTarget);
            break;
        case 4:
        case 3:
            KexMantisGoSubmerge(nLava);
            break;
        case 7:
            KexMantisBeginAction(nLava, nSprite, nTarget, 8);
            break;
        case 8:
            KexMantisGoChase(nLava, nSprite, nTarget);
            break;
    }
}

static bool KexMantisDeathTick(short nLava, short nSprite)
{
    int const nextFrame = LavaList[nLava].nFrame + 1;
    if (nextFrame == 5 || nextFrame == 8)
        KexMantisSpawnDeathBurst(nLava, nSprite);
    else if (nextFrame == 37)
        KexMantisPlaySound(nSprite, "amb_lav2");
    else if (nextFrame == 43)
        KexMantisPlaySound(nSprite, "lav_act4");

    // Frame 53 executes A_Hide and A_Destroy as soon as it is entered.
    if (nextFrame >= 53)
    {
        KexMantisDestroy(nLava, nSprite);
        return true;
    }
    LavaList[nLava].nFrame = (short)nextFrame;
    return false;
}


void InitLava()
{
    LavaCount = 0;
    s_kexMantisTest = -1;
    s_kexMantisTestPhase = 0;
    s_kexMantisTestTicks = 0;
    s_kexMantisTestLava = -1;
    s_kexMantisTestPlayer = -1;
}

int BuildLavaLimb(int nSprite, int edx, int ebx)
{
    short nSector = sprite[nSprite].sectnum;

    int nLimbSprite = insertsprite(nSector, 118);
    assert(nLimbSprite >= 0 && nLimbSprite < kMaxSprites);

    sprite[nLimbSprite].x = sprite[nSprite].x;
    sprite[nLimbSprite].y = sprite[nSprite].y;
    sprite[nLimbSprite].z = sprite[nSprite].z - RandomLong() % ebx;
    sprite[nLimbSprite].cstat = 0;
    sprite[nLimbSprite].shade = -127;
    sprite[nLimbSprite].pal = 1;
    sprite[nLimbSprite].xvel = (RandomSize(5) - 16) << 8;
    sprite[nLimbSprite].yvel = (RandomSize(5) - 16) << 8;
    sprite[nLimbSprite].zvel = 2560 - (RandomSize(5) << 8);
    sprite[nLimbSprite].xoffset = 0;
    sprite[nLimbSprite].yoffset = 0;
    sprite[nLimbSprite].xrepeat = 90;
    sprite[nLimbSprite].yrepeat = 90;
    sprite[nLimbSprite].picnum = (edx & 3) % 3;
    sprite[nLimbSprite].hitag = 0;
    sprite[nLimbSprite].lotag = runlist_HeadRun() + 1;
    sprite[nLimbSprite].clipdist = 0;

//	GrabTimeSlot(3);

    sprite[nLimbSprite].extra = -1;
    sprite[nLimbSprite].owner = runlist_AddRunRec(sprite[nLimbSprite].lotag - 1, nLimbSprite, kRunLavaLimb);
    sprite[nLimbSprite].hitag = runlist_AddRunRec(NewRun, nLimbSprite, kRunLavaLimb);

    return nLimbSprite;
}

void FuncLavaLimb(int a, int UNUSED(nDamage), int nRun)
{
    short nSprite = RunData[nRun].nVal;
    assert(nSprite >= 0 && nSprite < kMaxSprites);

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x20000:
        {
            sprite[nSprite].shade += 3;

            int nRet = movesprite(nSprite, sprite[nSprite].xvel << 12, sprite[nSprite].yvel << 12, sprite[nSprite].zvel, 2560, -2560, CLIPMASK1);

            if (nRet || sprite[nSprite].shade > 100)
            {
                sprite[nSprite].xvel = 0;
                sprite[nSprite].yvel = 0;
                sprite[nSprite].zvel = 0;

                runlist_DoSubRunRec(sprite[nSprite].owner);
                runlist_FreeRun(sprite[nSprite].lotag - 1);
                runlist_SubRunRec(sprite[nSprite].hitag);

                mydeletesprite(nSprite);
            }
            break;
        }

        case 0x90000:
        {
            seq_PlotSequence(a & 0xFFFF, (SeqOffsets[kSeqLavag] + 30) + sprite[nSprite].picnum, 0, 1);
            break;
        }

        default:
            return;
    }
}

int BuildLava(short nSprite, int x, int y, int UNUSED(z), short nSector, short nAngle, int nChannel)
{
    short nLava = LavaCount;
    LavaCount++;

    if (nLava >= kMaxLavas) {
        return -1;
    }

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 118);
    }
    else
    {
        nSector = sprite[nSprite].sectnum;
        nAngle = sprite[nSprite].ang;
        x = sprite[nSprite].x;
        y = sprite[nSprite].y;

        changespritestat(nSprite, 118);
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = sector[nSector].floorz;
    sprite[nSprite].cstat = 0x8000;
    sprite[nSprite].xrepeat = g_kexMode ? 120 : 200;
    sprite[nSprite].yrepeat = g_kexMode ? 120 : 200;
    sprite[nSprite].shade = -12;
    sprite[nSprite].pal = 0;
    sprite[nSprite].clipdist = g_kexMode ? 72 : 127;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = seq_GetSeqPicnum(kSeqLavag, ActionSeq[3].a, 0);
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;

//	GrabTimeSlot(3);

    sprite[nSprite].extra = -1;

    LavaList[nLava].nAction = 0;
    // AI_Mantis has 400 health in KEX; this controller's classic Set-like
    // lava creature remains at 4000.
    LavaList[nLava].nHealth = g_kexMode ? KexHealthUnits(400) : 4000;
    LavaList[nLava].nSprite = nSprite;
    LavaList[nLava].nTarget = -1;
    LavaList[nLava].nChannel = nChannel;
    LavaList[nLava].nFrame = 0;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nLava, kRunLavaDude);
    LavaList[nLava].nRun = runlist_AddRunRec(NewRun, nLava, kRunLavaDude);

    nCreaturesLeft++;

    return nLava;
}

static bool KexMantisTestRenderContract(short nLava)
{
    short const nSavedFrame = LavaList[nLava].nFrame;
    static uint8_t const saturn[] = {
        4, 5, 5, 5, 5, 10, 10, 10, 10, 11, 11, 11, 11
    };
    bool okay = true;

    for (int i = 0; i < 3; i++)
    {
        KexMantisSetAttackState(nLava, kKexMantisAttackPrefix, i, 0);
        okay &= KexMantisAttackRenderFrame(nLava) == i;
    }
    for (int i = 0; i < (int)ARRAY_SIZE(saturn); i++)
    {
        KexMantisSetAttackState(nLava, kKexMantisAttackSaturn, i, 1);
        okay &= KexMantisAttackRenderFrame(nLava) == saturn[i];
    }
    for (int i = 0; i < 9; i++)
    {
        KexMantisSetAttackState(nLava, kKexMantisAttackPSX, i, 0);
        okay &= KexMantisAttackRenderFrame(nLava) == i + 4;
    }
    LavaList[nLava].nFrame = nSavedFrame;
    return okay;
}

static bool KexMantisTestDeathRenderContract(short nLava)
{
    short const savedAction = LavaList[nLava].nAction;
    short const savedFrame = LavaList[nLava].nFrame;
    bool okay = true;
    LavaList[nLava].nAction = 5;
    for (int frame = 0; frame < 53; frame++)
    {
        LavaList[nLava].nFrame = (short)frame;
        int const wantedAction = frame < 9 ? 4 : (frame < 33 ? 7 : 8);
        int const wantedFrame = frame < 9 ? frame
            : (frame < 33 ? frame - 9 : frame - 33);
        okay &= KexMantisSequenceAction(nLava, 5) == wantedAction;
        okay &= KexMantisRenderFrame(nLava, 5) == wantedFrame;
    }
    LavaList[nLava].nAction = savedAction;
    LavaList[nLava].nFrame = savedFrame;
    return okay;
}

static void KexMantisTestFinish(short nSprite, bool bActorValid)
{
    if (bActorValid && (unsigned)s_kexMantisTestLava < (unsigned)LavaCount
        && (unsigned)nSprite < (unsigned)kMaxSprites
        && sprite[nSprite].statnum == 118)
    {
        Lava &lava = LavaList[s_kexMantisTestLava];
        lava.nHealth = 0;
        lava.nAction = 6;
        lava.nFrame = 0;
        lava.nTarget = -1;
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        sprite[nSprite].cstat = 0x8000; // env-only inert hidden run record
        nCreaturesLeft--;
    }
    else
    {
        OSD_Printf("Kex Mantis test: FAIL (test actor/state disappeared)\n");
    }

    if ((unsigned)s_kexMantisTestPlayer < (unsigned)nTotalPlayers)
        PlayerList[s_kexMantisTestPlayer].invincibility =
            s_kexMantisTestInvincibility;
    s_kexMantisTest = -2;
    s_kexMantisTestLava = -1;
}

void KexMantis_TestTick()
{
    if (s_kexMantisTest == -1)
        s_kexMantisTest = g_kexMode && Bgetenv("VB_KEX_MANTIS_TEST")
            ? 0 : -2;
    if (s_kexMantisTest < 0)
        return;

    if (s_kexMantisTest == 0)
    {
        short const nPlayer = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayer >= (unsigned)kMaxSprites
            || sprite[nPlayer].sectnum < 0 || !(sprite[nPlayer].cstat & 0x101))
            return;

        short const nSector = sprite[nPlayer].sectnum;
        // Keep the source and player apart horizontally. A Mantis projectile
        // starts 192 KEX units above the actor origin; spawning both at one
        // XY point makes its 3-D aim nearly vertical and turns a rounding
        // probe into a degenerate collision probe instead.
        int testX = sprite[nPlayer].x
            - mulscale14(Cos(sprite[nPlayer].ang), 600);
        int testY = sprite[nPlayer].y
            - mulscale14(Sin(sprite[nPlayer].ang), 600);
        short testSector = nSector;
        updatesector(testX, testY, &testSector);
        if (testSector < 0)
        {
            testX = sprite[nPlayer].x;
            testY = sprite[nPlayer].y;
            testSector = nSector;
        }
        int const nFloor = getflorzofslope(testSector, testX, testY);
        s_kexMantisTestLava = (short)BuildLava(-1, testX, testY, nFloor,
            testSector, sprite[nPlayer].ang, 0);
        if (s_kexMantisTestLava < 0)
        {
            OSD_Printf("Kex Mantis test: FAIL (no free controller)\n");
            s_kexMantisTest = -2;
            return;
        }

        Lava &lava = LavaList[s_kexMantisTestLava];
        short const nSprite = lava.nSprite;
        lava.nTarget = nPlayer;
        PlotCourseToSprite(nSprite, nPlayer);
        KexMantisSetVelocity(nSprite);
        s_kexMantisTestSpeed = ksqrt((uint32_t)(
            sprite[nSprite].xvel * sprite[nSprite].xvel
            + sprite[nSprite].yvel * sprite[nSprite].yvel));
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;

        s_kexMantisTestPlayer = nLocalPlayer;
        s_kexMantisTestInvincibility =
            PlayerList[nLocalPlayer].invincibility;
        // The full wake/attack/submerge/death contract spans 258 native
        // player ticks.  A short 100-tick grant expires while the disposable
        // Mantis is still attacking the player and restarts the fixture.
        // Match the ordinary Scarab duration, then restore the prior value in
        // KexMantisTestFinish() once this env-only probe completes.
        if (PlayerList[nLocalPlayer].invincibility < 900)
            PlayerList[nLocalPlayer].invincibility = 900;
        s_kexMantisTestOkay = 1;
        s_kexMantisTestSaturnShots = 0;
        s_kexMantisTestSaturnFrames = 0;
        s_kexMantisTestSaturnProjectiles = 0;
        s_kexMantisTestPSXShots = 0;
        s_kexMantisTestPSXFrames = 0;
        s_kexMantisTestPSXProjectiles = 0;
        s_kexMantisTestPeakCallbacks = 0;
        s_kexMantisTestRiseCallbacks = 0;
        s_kexMantisTestSubmergeCallbacks = 0;
        s_kexMantisTestSplashCallbacks = 0;
        s_kexMantisTestDeathBursts = 0;
        s_kexMantisTestDeathTrigger = 0;
        s_kexMantisTestDeathDestroyed = 0;
        s_kexMantisTestFailures = 0;

        lava.nAction = 0;
        KexMantisSetPackedState(s_kexMantisTestLava, 0, 0, 0, 0);
        // Force the retail peak branch without consuming the game's RNG. The
        // ordinary controller uses the same chooser after its gate and LOS.
        KexMantisChooseWakeState(s_kexMantisTestLava, nSprite, nPlayer, 0, 1);
        s_kexMantisTestPhase = 1;
        s_kexMantisTestTicks = 0;
        s_kexMantisTest = 1;
        OSD_Printf("Kex Mantis test: actor %d spawned; exact wake/attack/submerge probe started\n",
                   nSprite);
        return;
    }

    if ((unsigned)s_kexMantisTestLava >= (unsigned)LavaCount)
    {
        KexMantisTestFinish(-1, false);
        return;
    }
    Lava &lava = LavaList[s_kexMantisTestLava];
    short const nSprite = lava.nSprite;
    if (s_kexMantisTestPhase == 7 && s_kexMantisTestDeathDestroyed)
    {
        bool const death = s_kexMantisTestTicks == 52
            && s_kexMantisTestDeathTrigger == 1
            && s_kexMantisTestDeathBursts == 3
            && lava.nAction == 5 && lava.nFrame == 52;
        s_kexMantisTestFailures |= death ? 0 : 1u << 6;
        bool const okay = s_kexMantisTestOkay && death;
        OSD_Printf("Kex Mantis test: %s (peak 62, rise 60, prefix 3, Saturn %d/26 projectile %d, PSX %d/9 projectile %d, sink 24+21, death 3 bursts/53 frames/1 trigger, phase failures %02x)\n",
                   okay ? "PASS" : "FAIL", s_kexMantisTestSaturnShots,
                   s_kexMantisTestSaturnProjectiles,
                   s_kexMantisTestPSXShots,
                   s_kexMantisTestPSXProjectiles, s_kexMantisTestFailures);
        if ((unsigned)s_kexMantisTestPlayer < (unsigned)nTotalPlayers)
            PlayerList[s_kexMantisTestPlayer].invincibility =
                s_kexMantisTestInvincibility;
        s_kexMantisTest = -2;
        s_kexMantisTestLava = -1;
        return;
    }
    if ((unsigned)nSprite >= (unsigned)kMaxSprites
        || sprite[nSprite].statnum != 118)
    {
        KexMantisTestFinish(nSprite, false);
        return;
    }

    s_kexMantisTestTicks++;
    if (s_kexMantisTestPhase == 1 && s_kexMantisTestTicks == 61)
    {
        // Frame 61 is hidden for one tick before its goto enters chase. Keep
        // that immediate chase callback deterministic by removing its target.
        lava.nTarget = -1;
    }

    if (s_kexMantisTestPhase == 1 && s_kexMantisTestTicks == 62)
    {
        uint64_t const peakCallbacks = (UINT64_C(1) << 0)
            | (UINT64_C(1) << 2) | (UINT64_C(1) << 24)
            | (UINT64_C(1) << 37) | (UINT64_C(1) << 61);
        bool const peak = lava.nAction == 0
            && KexMantisAttackFrame(s_kexMantisTestLava) == 0
            && KexMantisScratch0(s_kexMantisTestLava) == 1
            && KexMantisScratch1(s_kexMantisTestLava) == 0
            && (sprite[nSprite].cstat & 0x8101) == 0x8000
            && s_kexMantisTestPeakCallbacks == peakCallbacks;
        s_kexMantisTestOkay &= peak;
        s_kexMantisTestFailures |= peak ? 0 : 1u << 0;

        short const nPlayer = PlayerList[nLocalPlayer].nSprite;
        lava.nTarget = nPlayer;
        // A zero coin forces rise and advances scratch1 exactly once.
        KexMantisChooseWakeState(s_kexMantisTestLava, nSprite, nPlayer, 0, 0);
        s_kexMantisTestPhase = 2;
        s_kexMantisTestTicks = 0;
        return;
    }

    if (s_kexMantisTestPhase == 2 && s_kexMantisTestTicks == 60)
    {
        uint64_t const riseCallbacks = (UINT64_C(1) << 0)
            | (UINT64_C(1) << 4) | (UINT64_C(1) << 12)
            | (UINT64_C(1) << 14) | (UINT64_C(1) << 31);
        bool const rise = lava.nAction == 3
            && KexMantisAttackMode(s_kexMantisTestLava)
                == kKexMantisAttackPrefix
            && KexMantisAttackFrame(s_kexMantisTestLava) == 0
            && KexMantisScratch0(s_kexMantisTestLava) == 0
            && KexMantisScratch1(s_kexMantisTestLava) == 1
            && (sprite[nSprite].cstat & 0x8101) == 0x101
            && s_kexMantisTestRiseCallbacks == riseCallbacks;
        s_kexMantisTestOkay &= rise;
        s_kexMantisTestFailures |= rise ? 0 : 1u << 1;
        if (!rise)
            OSD_Printf("Kex Mantis test: rise state action %d mode %d frame %d scratch %d/%d cstat %x callbacks %llx target %d player %d/%x\n",
                       lava.nAction, KexMantisAttackMode(s_kexMantisTestLava),
                       KexMantisAttackFrame(s_kexMantisTestLava),
                       KexMantisScratch0(s_kexMantisTestLava),
                       KexMantisScratch1(s_kexMantisTestLava),
                       (unsigned) sprite[nSprite].cstat,
                       (unsigned long long)s_kexMantisTestRiseCallbacks, lava.nTarget,
                       PlayerList[nLocalPlayer].nSprite,
                       (unsigned) sprite[PlayerList[nLocalPlayer].nSprite].cstat);
        s_kexMantisTestPhase = 3;
        s_kexMantisTestTicks = 0;
        return;
    }

    if (s_kexMantisTestPhase == 3 && s_kexMantisTestTicks == 3)
    {
        bool const prefix = lava.nAction == 3
            && KexMantisAttackMode(s_kexMantisTestLava)
                != kKexMantisAttackPrefix
            && KexMantisAttackFrame(s_kexMantisTestLava) == 0;
        s_kexMantisTestOkay &= prefix;
        s_kexMantisTestFailures |= prefix ? 0 : 1u << 2;
        if (!prefix)
            OSD_Printf("Kex Mantis test: prefix state action %d mode %d frame %d\n",
                       lava.nAction, KexMantisAttackMode(s_kexMantisTestLava),
                       KexMantisAttackFrame(s_kexMantisTestLava));

        // A real Saturn choice starts at 1..8, so count 1 proves the minimum
        // two-volley path and the A_MantisCheckAttack decrement ordering.
        lava.nAction = 3;
        KexMantisSetAttackState(s_kexMantisTestLava,
                                kKexMantisAttackSaturn, 0, 1);
        PlotCourseToSprite(nSprite, lava.nTarget);
        s_kexMantisTestPhase = 4;
        s_kexMantisTestTicks = 0;
        return;
    }

    if (s_kexMantisTestPhase == 4 && s_kexMantisTestTicks == 26)
    {
        bool const saturn = lava.nAction == 7
            && KexMantisAttackFrame(s_kexMantisTestLava) == 0
            && s_kexMantisTestSaturnShots == 2
            && s_kexMantisTestSaturnFrames == (1 << 1)
            && s_kexMantisTestSaturnProjectiles == 2;
        s_kexMantisTestOkay &= saturn;
        s_kexMantisTestFailures |= saturn ? 0 : 1u << 3;

        lava.nAction = 3;
        lava.nTarget = PlayerList[nLocalPlayer].nSprite;
        KexMantisSetAttackState(s_kexMantisTestLava,
                                kKexMantisAttackPSX, 0, 0);
        s_kexMantisTestPhase = 5;
        s_kexMantisTestTicks = 0;
        return;
    }

    if (s_kexMantisTestPhase == 5 && s_kexMantisTestTicks == 9)
    {
        bool const psx = lava.nAction == 7
            && KexMantisAttackFrame(s_kexMantisTestLava) == 0
            && s_kexMantisTestPSXShots == 5
            && s_kexMantisTestPSXFrames == 0x1f
            && s_kexMantisTestPSXProjectiles == 5;
        s_kexMantisTestOkay &= psx;
        s_kexMantisTestFailures |= psx ? 0 : 1u << 4;
        lava.nTarget = -1;
        s_kexMantisTestPhase = 6;
        s_kexMantisTestTicks = 0;
        return;
    }

    if (s_kexMantisTestPhase == 6 && s_kexMantisTestTicks == 45)
    {
        int const nAttackSeq = SeqOffsets[kSeqLavag] + ActionSeq[3].a;
        bool const stats = lava.nHealth == KexHealthUnits(400)
            && sprite[nSprite].clipdist == 72
            && sprite[nSprite].xrepeat == 120
            && sprite[nSprite].yrepeat == 120
            // Build's integer sintable has the same small directional length
            // variation covered by the Sentry probe, scaled by 64/8.
            && s_kexMantisTestSpeed >= 24480
            && s_kexMantisTestSpeed <= 24640;
        bool const cadence = SeqSize[SeqOffsets[kSeqLavag] + ActionSeq[1].a] == 62
            && SeqSize[SeqOffsets[kSeqLavag] + ActionSeq[2].a] == 60
            && SeqSize[nAttackSeq] == 13
            && SeqSize[SeqOffsets[kSeqLavag] + ActionSeq[7].a] == 24
            && SeqSize[SeqOffsets[kSeqLavag] + ActionSeq[8].a] == 21
            && KexMantisTestRenderContract(s_kexMantisTestLava)
            && KexMantisTestDeathRenderContract(s_kexMantisTestLava)
            && lava.nAction == 0
            && KexMantisAttackFrame(s_kexMantisTestLava) == 0
            && (sprite[nSprite].cstat & 0x8101) == 0x8000
            && s_kexMantisTestSubmergeCallbacks == (UINT64_C(1) << 3)
            && s_kexMantisTestSplashCallbacks == ((UINT64_C(1) << 4)
                | (UINT64_C(1) << 10) | (UINT64_C(1) << 20));
        s_kexMantisTestOkay &= stats && cadence;
        s_kexMantisTestFailures |= stats && cadence ? 0 : 1u << 5;
        if (!stats || !cadence)
            OSD_Printf("Kex Mantis test: sink state stats %d cadence %d action %d frame %d cstat %x speed %d hp %d clip/repeat %d/%d/%d callbacks %llx/%llx\n",
                       stats, cadence, lava.nAction,
                       KexMantisAttackFrame(s_kexMantisTestLava),
                       (unsigned) sprite[nSprite].cstat, s_kexMantisTestSpeed,
                       lava.nHealth, (int) sprite[nSprite].clipdist,
                       (int) sprite[nSprite].xrepeat, (int) sprite[nSprite].yrepeat,
                       (unsigned long long)s_kexMantisTestSubmergeCallbacks,
                       (unsigned long long)s_kexMantisTestSplashCallbacks);
        lava.nHealth = 0;
        KexMantisBeginDeath(s_kexMantisTestLava, nSprite);
        nCreaturesLeft--;
        s_kexMantisTestPhase = 7;
        s_kexMantisTestTicks = 0;
        return;
    }
}

void FuncLava(int a, int nDamage, int nRun)
{
    short nLava = RunData[nRun].nVal;
    assert(nLava >= 0 && nLava < kMaxLavas);

    short nAction = LavaList[nLava].nAction;
    short nSeq = ActionSeq[nAction].a + SeqOffsets[kSeqLavag];
    short nSprite = LavaList[nLava].nSprite;

    if (g_kexMode && nAction == 5)
        nSeq = ActionSeq[KexMantisSequenceAction(nLava, nAction)].a
            + SeqOffsets[kSeqLavag];

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Lava\n", nMessage);
            return;
        }

        case 0x90000:
        {
            int const nRenderFrame = g_kexMode
                ? KexMantisRenderFrame(nLava, nAction)
                : LavaList[nLava].nFrame;
            int const nPlotFlags = g_kexMode && nAction == 5
                ? ActionSeq[KexMantisSequenceAction(nLava, nAction)].b
                : ActionSeq[nAction].b;
            seq_PlotSequence(a & 0xFFFF, nSeq, nRenderFrame,
                             nPlotFlags);
            tsprite[a & 0xFFFF].owner = -1;
            return;
        }

        case 0xA0000:
        {
            return;
        }

        case 0x80000:
        {
            if (!nDamage) {
                return;
            }

            // The deterministic Mantis probe spawns a disposable actor in a
            // real trigger fixture. Its scripted death phase is entered
            // explicitly below, so fixture projectiles must not put it into
            // pain/submerge before the cadence checks complete.
            if (g_kexMode && s_kexMantisTest >= 0
                && nLava == s_kexMantisTestLava)
                return;

            LavaList[nLava].nHealth -= nDamage;

            if (LavaList[nLava].nHealth <= 0)
            {
                LavaList[nLava].nHealth = 0;
                if (g_kexMode)
                    KexMantisBeginDeath(nLava, nSprite);
                else
                {
                    LavaList[nLava].nAction = 5;
                    LavaList[nLava].nFrame  = 0;
                    sprite[nSprite].cstat &= 0xFEFE;
                }

                nCreaturesLeft--;
            }
            else
            {
                short nTarget = a & 0xFFFF;

                if (nTarget >= 0)
                {
                    if (sprite[nTarget].statnum < 199
                        && (!g_kexMode || sprite[nTarget].statnum == 100))
                    {
                        // AI_Mantis has noInFighting: KEX damage from another
                        // creature must not replace its player target.
                        LavaList[nLava].nTarget = nTarget;
                    }
                }

                bool const bKexPain = g_kexMode
                    ? KexDifficulty_PainRoll(16) : false;
                if ((g_kexMode && nAction < 5 && nAction != 4 && bKexPain)
                    || (!g_kexMode && nAction == 3 && !RandomSize(2)))
                {
                    if (g_kexMode)
                    {
                        KexMantisBeginAction(nLava, nSprite,
                                              LavaList[nLava].nTarget, 4);
                    }
                    else
                    {
                        LavaList[nLava].nAction = 4;
                        LavaList[nLava].nFrame  = 0;
                        sprite[nSprite].cstat = 0;
                    }
                }

                BuildLavaLimb(nSprite, totalmoves, 64000);
            }

            return;
        }

        case 0x20000:
        {
            int var_38 = g_kexMode
                ? KexMantisRenderFrame(nLava, nAction)
                : LavaList[nLava].nFrame;
            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, var_38);

            short nFlag = FrameFlag[SeqBase[nSeq] + var_38];

            int var_1C = 0;

            if (nAction)
            {
                // KEX callbacks below play the authored WAVs on their actual
                // retail frames; the stock SEQ cues are differently timed.
                if (!g_kexMode)
                    seq_MoveSequence(nSprite, nSeq, var_38);

                if (!(g_kexMode && KexMantisOwnsTimeline(nAction)))
                {
                    LavaList[nLava].nFrame++;
                    if (LavaList[nLava].nFrame >= SeqSize[nSeq])
                    {
                        var_1C = 1;
                        LavaList[nLava].nFrame = 0;
                    }
                }
            }

            short nTarget = LavaList[nLava].nTarget;

            if (nTarget >= 0 && (g_kexMode || nAction < 4))
            {
                int const nSectorLimit = g_kexMode ? kMaxSectors : 1024;
                if (!(sprite[nTarget].cstat & 0x101)
                    || (unsigned)sprite[nTarget].sectnum
                        >= (unsigned)nSectorLimit)
                {
                    nTarget = -1;
                    LavaList[nLava].nTarget = -1;
                }
            }

            if (g_kexMode && KexMantisOwnsTimeline(nAction))
            {
                switch (nAction)
                {
                    case 0:
                        KexMantisChaseTick(nLava, nSprite, nTarget);
                        break;
                    case 1:
                    case 2:
                    case 4:
                    case 7:
                    case 8:
                        KexMantisTimelineTick(nLava, nSprite, nTarget,
                                               nAction, SeqSize[nSeq]);
                        break;
                    case 3:
                        KexMantisAttackTick(nLava, nSprite, nTarget);
                        break;
                    case 5:
                        if (KexMantisDeathTick(nLava, nSprite))
                            return;
                        break;
                }
                sprite[nSprite].pal = 1;
                return;
            }

            switch (nAction)
            {
                case 0:
                {
                    if ((nLava & 0x1F) == (totalmoves & 0x1F))
                    {
                        if (nTarget < 0)
                        {
                            nTarget = FindPlayer(nSprite, 76800);
                        }

                        PlotCourseToSprite(nSprite, nTarget);

                        KexMantisSetVelocity(nSprite);

                        if (nTarget >= 0 && !RandomSize(1))
                        {
                            LavaList[nLava].nTarget = nTarget;
                            LavaList[nLava].nAction = 2;
                            sprite[nSprite].cstat = 0x101;
                            LavaList[nLava].nFrame = 0;
                            break;
                        }
                    }

                    int x = sprite[nSprite].x;
                    int y = sprite[nSprite].y;
                    int z = sprite[nSprite].z;
                    short nSector = sprite[nSprite].sectnum;

                    int nVal = movesprite(nSprite,
                                          KexScaleCreatureMove(nSprite, sprite[nSprite].xvel << 8),
                                          KexScaleCreatureMove(nSprite, sprite[nSprite].yvel << 8),
                                          0, 0, 0, CLIPMASK0);

                    if (nSector != sprite[nSprite].sectnum)
                    {
                        changespritesect(nSprite, nSector);
                        sprite[nSprite].x = x;
                        sprite[nSprite].y = y;
                        sprite[nSprite].z = z;

                        sprite[nSprite].ang = (sprite[nSprite].ang + ((RandomWord() & 0x3FF) + 1024)) & kAngleMask;
                        KexMantisSetVelocity(nSprite);
                        break;
                    }

                    if (!nVal) {
                        break;
                    }

                    if ((nVal & 0xC000) == 0x8000)
                    {
                        sprite[nSprite].ang = (sprite[nSprite].ang + ((RandomWord() & 0x3FF) + 1024)) & kAngleMask;
                        KexMantisSetVelocity(nSprite);
                        break;
                    }
                    else if ((nVal & 0xC000) == 0xC000)
                    {
                        if ((nVal & 0x3FFF) == nTarget)
                        {
                            int nAng = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                            if (AngleDiff(sprite[nSprite].ang, nAng) < 64)
                            {
                                LavaList[nLava].nAction = 2;
                                LavaList[nLava].nFrame = 0;
                                sprite[nSprite].cstat = 0x101;
                                break;
                            }
                        }
                    }

                    break;
                }

                case 1:
                case 6:
                {
                    break;
                }

                case 2:
                {
                    if (var_1C)
                    {
                        LavaList[nLava].nAction = 3;
                        LavaList[nLava].nFrame = 0;

                        PlotCourseToSprite(nSprite, nTarget);

                        sprite[nSprite].cstat |= 0x101;
                        if (g_kexMode)
                            KexMantisSetAttackState(nLava,
                                kKexMantisAttackPrefix, 0, 0);
                    }

                    break;
                }

                case 3:
                {
                    if ((nFlag & 0x80) && nTarget > -1)
                    {
                        int nHeight = GetSpriteHeight(nSprite);
                        GetUpAngle(nSprite, -64000, nTarget, (-(nHeight >> 1)));
                        BuildBullet(nSprite, 10, Cos(sprite[nSprite].ang) << 8, Sin(sprite[nSprite].ang) << 8, -1, sprite[nSprite].ang, nTarget + 10000, 1);
                    }
                    else if (var_1C)
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        LavaList[nLava].nAction = 7;
                        LavaList[nLava].nFrame = 0;
                    }

                    break;
                }

                case 4:
                {
                    if (var_1C)
                    {
                        LavaList[nLava].nAction = 7;
                        sprite[nSprite].cstat &= 0xFEFE;
                    }

                    break;
                }

                case 5:
                {
                    if (nFlag & 0x40)
                    {
                       int nLimbSprite = BuildLavaLimb(nSprite, LavaList[nLava].nFrame, 64000);
                       D3PlayFX(StaticSound[kSoundSetLand], nLimbSprite);
                    }

                    if (LavaList[nLava].nFrame)
                    {
                        if (nFlag & 0x80)
                        {
                            for (int i = 0; i < 20; i++) {
                                BuildLavaLimb(nSprite, i, 64000);
                            }

                            runlist_ChangeChannel(LavaList[nLava].nChannel, 1);
                        }
                    }
                    else
                    {
                        for (int i = 0; i < 30; i++) {
                            BuildLavaLimb(nSprite, i, 256);
                        }

                        runlist_DoSubRunRec(sprite[nSprite].owner);
                        runlist_FreeRun(sprite[nSprite].lotag - 1);
                        runlist_SubRunRec(LavaList[nLava].nRun);
                        mydeletesprite(nSprite);
                    }

                    break;
                }

                case 7:
                {
                    if (var_1C)
                    {
                        LavaList[nLava].nAction = 8;
                        LavaList[nLava].nFrame = 0;
                    }
                    break;
                }

                case 8:
                {
                    if (var_1C)
                    {
                        LavaList[nLava].nAction = 0;
                        LavaList[nLava].nFrame = 0;
                        sprite[nSprite].cstat = 0x8000;
                    }
                    break;
                }
            }

            // loc_31521:
            sprite[nSprite].pal = 1;
        }
    }
}

class LavaDudeLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void LavaDudeLoadSave::Load()
{
    Read(&LavaCount, sizeof(LavaCount));
    Read(&LavaList, sizeof(LavaList[0]) * LavaCount);
}

void LavaDudeLoadSave::Save()
{
    Write(&LavaCount, sizeof(LavaCount));
    Write(&LavaList, sizeof(LavaList[0]) * LavaCount);
}

static LavaDudeLoadSave* myLoadSave;

void LavaDudeLoadSaveConstruct()
{
    myLoadSave = new LavaDudeLoadSave();
}
