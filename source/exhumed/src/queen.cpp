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

#include "queen.h"
#include "exhumed.h"
#include "engine.h"
#include "move.h"
#include "sequence.h"
#include "runlist.h"
#include "bullet.h"
#include "random.h"
#include "wasp.h"
#include "trigdat.h"
#include "anims.h"
#include "player.h"
#include "sound.h"
#include "names.h"
#include "save.h"
#include "kexbreak.h" // VibeBuild32: KEX Queen death sound
#include "kexinv.h"
#include <assert.h>

#define kMaxQueens  1
#define kMaxEggs    10
#define kClassicTails 7
#define kMaxTails    10

// The original save record contains seven tail sprite ids.  Keep that byte
// count forever: the exact KEX controller stores its three extra ids in four
// otherwise-unused MoveQS slots (one marker plus three ids).  Exact tail
// motion does not use the classic 25-sample ring buffer.
#define kKexTailSaveMarkerSlot 21
#define kKexTailSaveFirstSlot  22
static const short kKexTailSaveMarker = -28497;

static actionSeq ActionSeq[] = {
    {0,  0},
    {0,  0},
    {9,  0},
    {36, 0},
    {18, 0},
    {27, 0},
    {45, 0},
    {45, 0},
    {54, 1},
    {53, 1},
    {55, 1}
};

static actionSeq HeadSeq[] = {
    {56, 1},
    {65, 0},
    {65, 0},
    {65, 0},
    {65, 0},
    {65, 0},
    {74, 0},
    {82, 0},
    {90, 0}
};

static actionSeq EggSeq[] = {
    {19, 1},
    {18, 1},
    {0,  0},
    {9,  0},
    {23, 1},
};

// Generated retail layouts. These are selected only when both exact Queen
// files are installed; the stock tables above remain the classic/demo and
// partial-conversion fallback contract.
enum KexQueenAction
{
    kKexQueenIdle = 0,
    kKexQueenBase,
    kKexQueenChase,
    kKexQueenCharge,
    kKexQueenAfterCharge,
    kKexQueenFlap,
    kKexQueenHurl,
    kKexQueenTailMissile,
    kKexQueenPain,
    kKexQueenDeath,
    kKexQueenInert,
};

static actionSeq KexQueenActionSeq[] = {
    {0,   0}, // queen_idle
    {9,   0}, // queen_base
    {18,  0}, // queen_chase
    {27,  0}, // queen_chase_charge
    {36,  0}, // queen_after_charge
    {45,  0}, // queen_flap
    {54,  0}, // queen_hurl
    {63,  0}, // queen_tail_missile
    {72,  1}, // queen_pain
    {73,  1}, // queen_death
    {103, 1}, // safe terminal slot
};

enum KexQueenSnakeAction
{
    kKexQueenSnakeIdle = 0,
    kKexQueenSnakeChase,
    kKexQueenSnakeMelee,
    kKexQueenSnakeExplode,
    kKexQueenSnakeInert,
};

static actionSeq KexQueenSnakeSeq[] = {
    {74,  0}, // queen_snake_idle / setup
    {83,  0}, // queen_snake_chase
    {92,  0}, // queen_snake_melee
    {101, 1}, // 21 four-tick explosions + terminal destroy tick
    {103, 1}, // safe terminal slot
};

struct Queen
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short field_A;
    short field_C;
    short pad;
    short field_10;
    short field_12;
};

struct Egg
{
    short nHealth;
    short field_2;
    short nAction;
    short nSprite;
    short field_8;
    short nTarget;
    short field_C;
    short field_E;
};

struct Head
{
    short nHealth;
    short field_2;
    short nAction;
    short nSprite;
    short field_8;
    short nTarget;
    short field_C;
    short field_E;
};

short QueenCount = 0;
int nQHead = 0;

short nEggsFree;
short nHeadVel;
short nVelShift;

short tailspr[kMaxTails];
short nEggFree[kMaxEggs];

short QueenChan[kMaxQueens];
Egg QueenEgg[kMaxEggs];
Queen QueenList[kMaxQueens];
Head QueenHead;

int MoveQX[25];
int MoveQY[25];
int MoveQZ[25];
short MoveQS[25];
short MoveQA[25];

static int s_queenTest = -1;
static int s_queenAudit = -1;
static int s_queenAuditSeen;
static int s_queenTestPhase;
static int s_queenTestTicks;
static int s_queenTestOkay;
static int s_queenTestCreatureCounted;
static short s_queenTestQueen = -1;
static short s_queenTestPlayer = -1;
static short s_queenTestEgg = -1;
static short s_queenTestInvincibility;
static int s_queenTestBodyMelee;
static int s_queenTestChargeDamage;
static int s_queenTestChargeKnockback;
static int s_queenTestSnakeMelee;
static int s_queenTestProjectile;
static int s_queenTestPodTicks;
static int s_queenTestPodHatches;
static int s_queenTestPodGeometry;
static int s_queenTestPainGibs;
static int s_queenTestBodyGibs;
static int s_queenTestTailDeaths;
static int s_queenTestTailLerp;
static int s_queenTestSnakeExplosions;
static int s_queenTestSnakeTriggers;
static int s_queenTestSaveCompat;
static short s_queenSaveBodyAction;
static short s_queenSaveBodyFrame;
static short s_queenSaveBodyHealth;
static short s_queenSaveHeadSprite;
static short s_queenSaveHeadAction;
static short s_queenSaveHeadHealth;
static short s_queenSaveTail[kMaxTails];

static bool KexQueenSequenceExact()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqQueen)
        || !seq_IsLoaded(kSeqQueenEgg))
        return false;

    int const queen = SeqOffsets[kSeqQueen];
    int const egg = SeqOffsets[kSeqQueenEgg];
    return SeqSize[queen] == 1 && SeqSize[queen + 9] == 10
        && SeqSize[queen + 18] == 10 && SeqSize[queen + 27] == 10
        && SeqSize[queen + 36] == 10 && SeqSize[queen + 45] == 11
        && SeqSize[queen + 54] == 30 && SeqSize[queen + 63] == 16
        && SeqSize[queen + 72] == 13 && SeqSize[queen + 73] == 62
        && SeqSize[queen + 74] == 1 && SeqSize[queen + 83] == 5
        && SeqSize[queen + 92] == 1 && SeqSize[queen + 101] == 85
        && SeqSize[queen + 102] == 1 && SeqSize[queen + 103] == 1
        && SeqSize[egg] == 2 && SeqSize[egg + 1] == 1;
}

static void KexQueenPlaySound(short nSprite, char const *name)
{
    int const sound = KexSound_Get(name);
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static bool KexQueenValidTarget(short nTarget)
{
    return (unsigned)nTarget < (unsigned)kMaxSprites
        && sprite[nTarget].sectnum >= 0
        && sprite[nTarget].sectnum < kMaxSectors
        && sprite[nTarget].statnum == 100
        && (sprite[nTarget].cstat & 0x101)
        && !(sprite[nTarget].cstat & 0x8000);
}

static short KexQueenFindPlayer(short nSprite)
{
    short const nTarget = FindPlayer(nSprite, 1000);
    return KexQueenValidTarget(nTarget) ? nTarget : -1;
}

static bool KexQueenRandomDecision(int nMask)
{
    return (RandomLong() & nMask) == (totalmoves & nMask);
}

static int KexQueenSignedRandom(int nScale)
{
    return ((int16_t)RandomWord() * nScale) / 32768;
}

static void KexQueenTurnToward(short nSprite, short nTarget, int nMaxTurn)
{
    int const desired = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                   sprite[nTarget].y - sprite[nSprite].y);
    int turn = AngleDelta(sprite[nSprite].ang, desired, 1024);
    turn = clamp(turn, -nMaxTurn, nMaxTurn);
    sprite[nSprite].ang = (sprite[nSprite].ang + turn) & kAngleMask;
}

static void KexQueenSetVelocity(short nSprite, int nSpeed, int nAngle)
{
    int const velocity = nSpeed * 6;
    sprite[nSprite].xvel = (Cos(nAngle) * velocity) >> 8;
    sprite[nSprite].yvel = (Sin(nAngle) * velocity) >> 8;
}

static int64_t KexQueenDistanceSq(short nSprite, short nTarget)
{
    int64_t const dx = (int64_t)sprite[nTarget].x - sprite[nSprite].x;
    int64_t const dy = (int64_t)sprite[nTarget].y - sprite[nSprite].y;
    int64_t const dz = ((int64_t)sprite[nTarget].z - sprite[nSprite].z) / 16;
    return dx * dx + dy * dy + dz * dz;
}

static bool KexQueenMeleeRange(short nSprite, short nTarget,
                               int nReach)
{
    if (!KexQueenValidTarget(nTarget))
        return false;
    int64_t const radius = (int64_t)sprite[nSprite].clipdist * 4
        + (int64_t)sprite[nTarget].clipdist * 4 + nReach * 6;
    return KexQueenDistanceSq(nSprite, nTarget) <= radius * radius;
}

// The native body controller has three health-gated phases. KEX exposes one
// 3600-point Queen health pool, so partition it evenly across those phases
// while retaining the native transition choreography. This totals the exact
// 3600 KEX points after conversion to runlist health units.
static int KexQueenPhaseHealth()
{
    return KexHealthUnits(1200);
}

static int KexQueenRemainingHealth(short nQueen)
{
    int const futurePhases = max(0, 2 - (int)QueenList[nQueen].field_A);
    return QueenList[nQueen].nHealth + futurePhases * KexQueenPhaseHealth();
}

static bool KexQueenSpriteValid(short nSprite)
{
    return (unsigned)nSprite < (unsigned)kMaxSprites
        && (unsigned)sprite[nSprite].sectnum < (unsigned)kMaxSectors;
}

static short KexQueenLegacyBodyAction(short nAction)
{
    switch (nAction)
    {
        case 0:  return kKexQueenIdle;
        case 1:  return kKexQueenBase;
        case 2:  return kKexQueenChase;
        case 3:  return kKexQueenFlap;
        case 4:  return kKexQueenTailMissile;
        case 5:
        case 6:  return kKexQueenHurl;
        case 7:  return kKexQueenPain;
        case 8:
        case 9:  return kKexQueenDeath;
        case 10: return kKexQueenInert;
        default: return kKexQueenIdle;
    }
}

static short KexQueenLegacySnakeAction(short nAction)
{
    if (nAction == 0)
        return kKexQueenSnakeIdle;
    if (nAction == 5)
        return kKexQueenSnakeExplode;
    return kKexQueenSnakeChase;
}

static void KexQueenSanitizeBody(short nQueen)
{
    Queen &queen = QueenList[nQueen];
    if ((unsigned)queen.nAction >= ARRAY_SIZE(KexQueenActionSeq))
    {
        queen.nAction = queen.nHealth > 0 ? kKexQueenIdle
                                         : kKexQueenInert;
        queen.nFrame = 0;
    }

    if (queen.nAction == kKexQueenInert)
        return;

    int const nSeq = SeqOffsets[kSeqQueen]
        + KexQueenActionSeq[queen.nAction].a;
    if (queen.nFrame < 0 || queen.nFrame >= SeqSize[nSeq])
        queen.nFrame = 0;
}

static void KexQueenSanitizeSnake(void)
{
    if ((unsigned)QueenHead.nAction >= ARRAY_SIZE(KexQueenSnakeSeq)
        || QueenHead.nAction == kKexQueenSnakeInert)
    {
        QueenHead.nAction = QueenHead.nHealth > 0
            ? kKexQueenSnakeChase : kKexQueenSnakeExplode;
        QueenHead.field_2 = 0;
    }

    int const nSeq = SeqOffsets[kSeqQueen]
        + KexQueenSnakeSeq[QueenHead.nAction].a;
    if (QueenHead.field_2 < 0 || QueenHead.field_2 >= SeqSize[nSeq])
        QueenHead.field_2 = 0;
}

static void KexQueenPackTailSave(short *saveMoveQS)
{
    Bmemcpy(saveMoveQS, MoveQS, sizeof(MoveQS));
    saveMoveQS[kKexTailSaveMarkerSlot] = kKexTailSaveMarker;
    for (int i = kClassicTails; i < kMaxTails; i++)
        saveMoveQS[kKexTailSaveFirstSlot + i - kClassicTails] = tailspr[i];
}

static bool KexQueenRestoreTailSave(void)
{
    for (int i = kClassicTails; i < kMaxTails; i++)
        tailspr[i] = -1;

    if (MoveQS[kKexTailSaveMarkerSlot] != kKexTailSaveMarker)
        return false;

    for (int i = kClassicTails; i < kMaxTails; i++)
        tailspr[i] = MoveQS[kKexTailSaveFirstSlot + i - kClassicTails];
    return true;
}

static void KexQueenMigrateLegacyBody(short nQueen)
{
    Queen &queen = QueenList[nQueen];
    short const oldAction = queen.nAction;
    if (oldAction < 8 && queen.nHealth > 0)
        queen.nHealth = (short)clamp(KexQueenRemainingHealth(nQueen), 1,
                                    KexHealthUnits(3600));
    else
        queen.nHealth = 0;

    queen.nAction = KexQueenLegacyBodyAction(oldAction);
    queen.nFrame = 0;
    queen.field_A = 0;
    queen.field_C = 0;
    queen.field_10 = 5;
    queen.field_12 = 0;

    if (KexQueenSpriteValid(queen.nSprite))
    {
        sprite[queen.nSprite].clipdist = 96;
        sprite[queen.nSprite].xrepeat = 60;
        sprite[queen.nSprite].yrepeat = 60;
        if (queen.nAction == kKexQueenDeath
            || queen.nAction == kKexQueenInert)
            sprite[queen.nSprite].xvel = sprite[queen.nSprite].yvel
                = sprite[queen.nSprite].zvel = 0;
    }
    KexQueenSanitizeBody(nQueen);
}

static void KexQueenNormalizeTailChain(bool legacy)
{
    int count = clamp((int)QueenHead.field_E, 0, kMaxTails);
    for (int i = 0; i < count; i++)
    {
        if (!KexQueenSpriteValid(tailspr[i])
            || sprite[tailspr[i]].statnum != 121)
        {
            count = i;
            break;
        }
    }
    QueenHead.field_E = (short)count;
    for (int i = count; i < kMaxTails; i++)
        tailspr[i] = -1;

    if (!legacy)
        return;

    for (int i = 0; i < count; i++)
    {
        short const nTail = tailspr[i];
        sprite[nTail].clipdist = 96;
        sprite[nTail].xrepeat = 80;
        sprite[nTail].yrepeat = 80;
        sprite[nTail].cstat = i == count - 1 ? 0x100 : 0;
        MoveQA[i] = KexHealthUnits(200);
    }
}

static void KexQueenMigrateLegacySnake(void)
{
    if (!KexQueenSpriteValid(QueenHead.nSprite)
        || sprite[QueenHead.nSprite].statnum != 121)
        return;

    short const oldAction = QueenHead.nAction;
    QueenHead.nAction = KexQueenLegacySnakeAction(oldAction);
    QueenHead.field_2 = 0;
    QueenHead.field_C = 0;
    sprite[QueenHead.nSprite].clipdist = 48;
    sprite[QueenHead.nSprite].xrepeat = 80;
    sprite[QueenHead.nSprite].yrepeat = 80;

    if (QueenHead.nAction == kKexQueenSnakeExplode
        || QueenHead.nHealth <= 0)
    {
        QueenHead.nHealth = 0;
        QueenHead.nAction = kKexQueenSnakeExplode;
        QueenHead.field_E = 0;
        for (int i = 0; i < kMaxTails; i++)
            tailspr[i] = -1;
        sprite[QueenHead.nSprite].xvel = sprite[QueenHead.nSprite].yvel
            = sprite[QueenHead.nSprite].zvel = 0;
        sprite[QueenHead.nSprite].cstat = 0x8000;
    }
    else
    {
        QueenHead.nHealth = (short)clamp((int)QueenHead.nHealth, 1,
                                        KexHealthUnits(200));
        KexQueenNormalizeTailChain(true);
        sprite[QueenHead.nSprite].cstat = QueenHead.field_E > 0
            || QueenHead.nAction == kKexQueenSnakeIdle ? 1 : 0x101;
    }
    KexQueenSanitizeSnake();
}

static void KexQueenNormalizeLoadedEgg(short nEgg, bool legacy)
{
    Egg &egg = QueenEgg[nEgg];
    if (egg.field_8 < 0 || !KexQueenSpriteValid(egg.nSprite)
        || sprite[egg.nSprite].statnum != 121)
        return;

    short const nSprite = egg.nSprite;
    if (legacy || egg.field_E != 0 || egg.nAction != 0)
    {
        egg.nHealth = 1;
        egg.field_2 = 0;
        egg.nAction = 0;
        egg.nTarget = -1;
        egg.field_E = 0;
        if (egg.field_C <= 0 || egg.field_C > 60)
            egg.field_C = 60;
        if (sprite[nSprite].xvel == 0 && sprite[nSprite].yvel == 0)
            KexQueenSetVelocity(nSprite, 32, sprite[nSprite].ang);
        sprite[nSprite].zvel = 0;
    }
    else
        egg.field_C = (short)clamp((int)egg.field_C, 1, 60);

    sprite[nSprite].clipdist = 54;
    sprite[nSprite].xrepeat = 40;
    sprite[nSprite].yrepeat = 40;
    sprite[nSprite].cstat = 0;
}

static int KexQueenNormalizeLoadedState(bool exactSave)
{
    int migrated = 0;
    if ((unsigned)QueenCount < (unsigned)kMaxQueens
        && KexQueenSpriteValid(QueenList[0].nSprite))
    {
        if (!exactSave)
        {
            KexQueenMigrateLegacyBody(0);
            migrated++;
        }
        else
            KexQueenSanitizeBody(0);
    }

    if (KexQueenSpriteValid(QueenHead.nSprite)
        && sprite[QueenHead.nSprite].statnum == 121)
    {
        if (!exactSave)
        {
            KexQueenMigrateLegacySnake();
            migrated++;
        }
        else
        {
            KexQueenNormalizeTailChain(false);
            KexQueenSanitizeSnake();
        }
    }

    for (int i = 0; i < kMaxEggs; i++)
    {
        if (QueenEgg[i].field_8 >= 0
            && KexQueenSpriteValid(QueenEgg[i].nSprite)
            && sprite[QueenEgg[i].nSprite].statnum == 121)
        {
            KexQueenNormalizeLoadedEgg((short)i, !exactSave);
            if (!exactSave)
                migrated++;
        }
    }
    return migrated;
}


void InitQueens()
{
    QueenCount = 1;

    for (int i = 0; i < kMaxTails; i++)
        tailspr[i] = -1;
    QueenHead.nSprite = -1;
    QueenHead.field_8 = -1;
    QueenHead.field_E = 0;

    for (int i = 0; i < kMaxEggs; i++)
    {
        nEggFree[i] = i;
        QueenEgg[i].field_8 = -1;
    }

    nEggsFree = kMaxEggs;
    s_queenTest = -1;
    s_queenAudit = -1;
    s_queenAuditSeen = 0;
    s_queenTestQueen = -1;
    s_queenTestPlayer = -1;
    s_queenTestEgg = -1;
}

int GrabEgg()
{
    if (!nEggsFree) {
        return -1;
    }

    nEggsFree--;
    return nEggFree[nEggsFree];
}

void BlowChunks(int nSprite)
{
    for (int i = 0; i < 4; i++)
    {
        BuildCreatureChunk(nSprite, seq_GetSeqPicnum(16, i + 41, 0));
    }
}

void DestroyEgg(short nEgg)
{
    short nSprite = QueenEgg[nEgg].nSprite;

    if (QueenEgg[nEgg].nAction != 4)
    {
        BuildAnim(-1, 34, 0, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum, sprite[nSprite].xrepeat, 4);
    }
    else
    {
        for (int i = 0; i < 4; i++)
        {
            BuildCreatureChunk(nSprite, seq_GetSeqPicnum(kSeqQueenEgg, (i % 2) + 24, 0));
        }
    }

    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_DoSubRunRec(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(QueenEgg[nEgg].field_8);

    QueenEgg[nEgg].field_8 = -1;

    mydeletesprite(nSprite);

    nEggFree[nEggsFree] = nEgg;
    nEggsFree++;
}

void DestroyAllEggs()
{
    for (int i = 0; i < kMaxEggs; i++)
    {
        if (QueenEgg[i].field_8 > -1)
        {
            DestroyEgg(i);
        }
    }
}

void SetHeadVel(short nSprite)
{
    short nAngle = sprite[nSprite].ang;

    if (nVelShift >= 0)
    {
        sprite[nSprite].xvel = Cos(nAngle) >> (int8_t)(nVelShift);
        sprite[nSprite].yvel = Sin(nAngle) >> (int8_t)(nVelShift);
    }
    else
    {
        sprite[nSprite].xvel = Cos(nAngle) << (int8_t)(-nVelShift);
        sprite[nSprite].yvel = Sin(nAngle) << (int8_t)(-nVelShift);
    }
}

int QueenAngleChase(short nSprite, short nSprite2, int val1, int val2)
{
    val1 = KexScaleCreatureMove(nSprite, val1);
    short nAngle;

    spritetype *pSprite = &sprite[nSprite];
    if (nSprite2 < 0)
    {
        pSprite->zvel = 0;
        nAngle = pSprite->ang;
    }
    else
    {
        spritetype *pSprite2 = &sprite[nSprite2];
        int nTileY = (tilesiz[pSprite2->picnum].y * pSprite2->yrepeat) * 2;

        int nMyAngle = GetMyAngle(pSprite2->x - pSprite->x, pSprite2->y - pSprite->y);

        int edx = ((pSprite2->z - nTileY) - pSprite->z) >> 8;

        uint32_t xDiff = klabs(pSprite2->x - pSprite->x);
        uint32_t yDiff = klabs(pSprite2->y - pSprite->y);

        uint32_t sqrtVal = xDiff * xDiff + yDiff * yDiff;

        if (sqrtVal > INT_MAX)
        {
            OSD_Printf("%s %d: overflow\n", EDUKE32_FUNCTION, __LINE__);
            sqrtVal = INT_MAX;
        }

        int nSqrt = ksqrt(sqrtVal);

        int var_14 = GetMyAngle(nSqrt, edx);

        int nAngDelta = AngleDelta(pSprite->ang, nMyAngle, 1024);

        if (klabs(nAngDelta) > 127)
        {
            val1 /= klabs(nAngDelta>>7);
            if (val1 < 256)
                val1 = 256;
        }

        if (klabs(nAngDelta) > val2)
        {
            if (nAngDelta < 0)
                nAngDelta = -val2;
            else
                nAngDelta = val2;
        }

        nAngle = (nAngDelta + sprite[nSprite].ang) & kAngleMask;

        pSprite->zvel = (AngleDelta(pSprite->zvel, var_14, 24) + pSprite->zvel) & kAngleMask;
    }

    pSprite->ang = nAngle;

    int da = pSprite->zvel;
    int x = klabs(Cos(da));

    int v26 = x * ((val1 * Cos(nAngle)) >> 14);
    int v27 = x * ((val1 * Sin(nAngle)) >> 14);

    uint32_t xDiff = klabs((int32_t)(v26 >> 8));
    uint32_t yDiff = klabs((int32_t)(v27 >> 8));

    uint32_t sqrtNum = xDiff * xDiff + yDiff * yDiff;

    if (sqrtNum > INT_MAX)
    {
        OSD_Printf("%s %d: overflow\n", EDUKE32_FUNCTION, __LINE__);
        sqrtNum = INT_MAX;
    }

    int nSqrt = ksqrt(sqrtNum) * Sin(da);

    return movesprite(nSprite, v26 >> 2, v27 >> 2, (Sin(bobangle) >> 5) + (nSqrt >> 13), 0, 0, CLIPMASK1);
}

int DestroyTailPart()
{
    if (!QueenHead.field_E) {
        return 0;
    }

    short nSprite = tailspr[--QueenHead.field_E];

    BlowChunks(nSprite);
    BuildExplosion(nSprite);

    for (int i = 0; i < 5; i++)
    {
        short nHeight = GetSpriteHeight(nSprite);
        BuildLavaLimb(nSprite, i, nHeight);
    }

    mydeletesprite(nSprite);
    return 1;
}

void BuildTail()
{
    short nSprite = QueenHead.nSprite;
    bool const bExactQueen = KexQueenSequenceExact();

    int x = sprite[nSprite].x;
    int y = g_kexMode ? sprite[nSprite].y : sprite[nSprite].x;
    int z = g_kexMode ? sprite[nSprite].z : sprite[nSprite].x;
    short nSector = sprite[nSprite].sectnum;

    int i;

    int const nTails = bExactQueen ? kMaxTails : kClassicTails;
    for (i = 0; i < nTails; i++)
    {
        short nTailSprite = insertsprite(nSector, 121);
        tailspr[i] = nTailSprite;

        if (nTailSprite < 0) {
            bail2dos("Can't create queen's tail!\n");
        }

        sprite[nTailSprite].lotag = runlist_HeadRun() + 1;
        sprite[nTailSprite].owner = runlist_AddRunRec(sprite[nTailSprite].lotag - 1, (i + 1), kRunQueenHead);
        sprite[nTailSprite].shade = -12;
        sprite[nTailSprite].x = x;
        sprite[nTailSprite].y = y;
        sprite[nTailSprite].hitag = 0;
        // Retail tail pieces are non-solid. Only the terminal link is
        // shootable; destroying it exposes the preceding 200-health link.
        sprite[nTailSprite].cstat = bExactQueen && i == nTails - 1
            ? 0x100 : (g_kexMode && i == nTails - 1 ? 0x101 : 0);
        sprite[nTailSprite].clipdist = bExactQueen ? 96 : 100;
        sprite[nTailSprite].xrepeat = 80;
        sprite[nTailSprite].yrepeat = 80;
        sprite[nTailSprite].picnum = 1;
        sprite[nTailSprite].pal = sector[sprite[nTailSprite].sectnum].ceilingpal;
        sprite[nTailSprite].xoffset = 0;
        sprite[nTailSprite].yoffset = 0;
        sprite[nTailSprite].z = z;
        sprite[nTailSprite].extra = -1;
        if (bExactQueen)
            MoveQA[i] = KexHealthUnits(200);
    }

    for (i = 0; i < 24 + 1; i++)
    {
        MoveQX[i] = x;
        MoveQZ[i] = z;
        MoveQY[i] = y;
        assert(nSector >= 0 && nSector < kMaxSectors);
        MoveQS[i] = nSector;
    }

    nQHead = 0;
    QueenHead.field_E = nTails;
}

int BuildQueenEgg(short nQueen, int nVal)
{
    int nEgg = GrabEgg();
    if (nEgg < 0) {
        return -1;
    }

    short nSprite = QueenList[nQueen].nSprite;

    int x = sprite[nSprite].x;
    int y = sprite[nSprite].y;
    short nSector = sprite[nSprite].sectnum;
    int nFloorZ = sector[nSector].floorz;
    short nAngle = sprite[nSprite].ang;

    if (KexQueenSequenceExact() && !nVal)
    {
        int const nSprite2 = insertsprite(nSector, 121);
        assert(nSprite2 >= 0 && nSprite2 < kMaxSprites);

        sprite[nSprite2].x = x;
        sprite[nSprite2].y = y;
        sprite[nSprite2].z = sprite[nSprite].z - 128 * 16 * 6;
        sprite[nSprite2].pal = 0;
        sprite[nSprite2].clipdist = 54; // radius 36
        sprite[nSprite2].xoffset = 0;
        sprite[nSprite2].yoffset = 0;
        sprite[nSprite2].shade = -12;
        sprite[nSprite2].picnum = 1;
        sprite[nSprite2].ang = nAngle;
        sprite[nSprite2].xrepeat = 40; // authored scale 1.0
        sprite[nSprite2].yrepeat = 40;
        KexQueenSetVelocity(nSprite2, 32, nAngle);
        sprite[nSprite2].zvel = 0;
        sprite[nSprite2].cstat = 0;
        sprite[nSprite2].lotag = runlist_HeadRun() + 1;
        sprite[nSprite2].extra = -1;
        sprite[nSprite2].hitag = 0;

        QueenEgg[nEgg].nHealth = 1;
        QueenEgg[nEgg].field_2 = 0;
        QueenEgg[nEgg].nAction = 0;
        QueenEgg[nEgg].nSprite = nSprite2;
        QueenEgg[nEgg].nTarget = -1;
        QueenEgg[nEgg].field_C = 60;
        QueenEgg[nEgg].field_E = 0;

        sprite[nSprite2].owner = runlist_AddRunRec(
            sprite[nSprite2].lotag - 1, nEgg, kRunQueenEgg);
        QueenEgg[nEgg].field_8 = runlist_AddRunRec(
            NewRun, nEgg, kRunQueenEgg);
        if (s_queenTest >= 0 && nQueen == s_queenTestQueen)
        {
            s_queenTestEgg = (short)nEgg;
            int const speed = ksqrt((uint32_t)(
                sprite[nSprite2].xvel * sprite[nSprite2].xvel
                + sprite[nSprite2].yvel * sprite[nSprite2].yvel));
            s_queenTestPodGeometry = sprite[nSprite2].x == sprite[nSprite].x
                && sprite[nSprite2].y == sprite[nSprite].y
                && sprite[nSprite2].z == sprite[nSprite].z - 128 * 16 * 6
                && sprite[nSprite2].ang == sprite[nSprite].ang
                && sprite[nSprite2].clipdist == 54
                && sprite[nSprite2].xrepeat == 40
                && sprite[nSprite2].yrepeat == 40
                && sprite[nSprite2].cstat == 0
                && speed >= 12280 && speed <= 12290
                && QueenEgg[nEgg].field_C == 60;
        }
        return 0;
    }

    int nSprite2 = insertsprite(nSector, 121);
    assert(nSprite2 >= 0 && nSprite2 < kMaxSprites);

    sprite[nSprite2].x = x;
    sprite[nSprite2].y = y;
    sprite[nSprite2].z = nFloorZ;
    sprite[nSprite2].pal = 0;
    sprite[nSprite2].clipdist = 50;
    sprite[nSprite2].xoffset = 0;
    sprite[nSprite2].yoffset = 0;
    sprite[nSprite2].shade = -12;
    sprite[nSprite2].picnum = 1;
    sprite[nSprite2].ang = (RandomSize(9) + (nAngle - 256)) & kAngleMask;

    if (!nVal)
    {
        sprite[nSprite2].xrepeat = 30;
        sprite[nSprite2].yrepeat = 30;
        if (g_kexMode)
        {
            // A_QueenHurlSeedling launches the spawn actor horizontally at
            // 32 KEX units/tick; unlike the classic egg it has no upward
            // impulse. Build stores the converted XY velocity in the same
            // six-times world scale used by the other KEX actors.
            sprite[nSprite2].xvel = (Cos(sprite[nSprite2].ang) * (32 * 6)) >> 8;
            sprite[nSprite2].yvel = (Sin(sprite[nSprite2].ang) * (32 * 6)) >> 8;
            sprite[nSprite2].zvel = 0;
        }
        else
        {
            sprite[nSprite2].xvel = Cos(sprite[nSprite2].ang);
            sprite[nSprite2].yvel = Sin(sprite[nSprite2].ang);
            sprite[nSprite2].zvel = -6000;
        }
        sprite[nSprite2].cstat = 0;
    }
    else
    {
        sprite[nSprite2].xrepeat = 60;
        sprite[nSprite2].yrepeat = 60;
        sprite[nSprite2].xvel = 0;
        sprite[nSprite2].yvel = 0;
        sprite[nSprite2].zvel = -2000;
        sprite[nSprite2].cstat = 0x101;
    }

    sprite[nSprite2].lotag = runlist_HeadRun() + 1;
    sprite[nSprite2].extra = -1;
    sprite[nSprite2].hitag = 0;

    GrabTimeSlot(3);

    QueenEgg[nEgg].nHealth = 200;
    QueenEgg[nEgg].field_2 = 0;
    QueenEgg[nEgg].nSprite = nSprite2;
    QueenEgg[nEgg].field_E = nVal;
    QueenEgg[nEgg].nTarget = QueenList[nQueen].nTarget;

    if (nVal)
    {
        nVal = 4;
        QueenEgg[nEgg].field_C = 200;
    }

    QueenEgg[nEgg].nAction = nVal;

    sprite[nSprite2].owner = runlist_AddRunRec(sprite[nSprite2].lotag - 1, nEgg, kRunQueenEgg);
    QueenEgg[nEgg].field_8 = runlist_AddRunRec(NewRun, nEgg, kRunQueenEgg);

    return 0;
}

static int KexQueenSeedCount()
{
    int count = KexSeedlingCount();
    for (int i = 0; i < kMaxEggs; i++)
    {
        if (QueenEgg[i].field_8 >= 0 && QueenEgg[i].field_E == 0
            && QueenEgg[i].field_C > 0)
            count++;
    }
    return count;
}

static void KexQueenDeleteSeedPod(short nEgg)
{
    short const nSprite = QueenEgg[nEgg].nSprite;
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_DoSubRunRec(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(QueenEgg[nEgg].field_8);
    QueenEgg[nEgg].field_8 = -1;
    mydeletesprite(nSprite);
    nEggFree[nEggsFree++] = nEgg;
}

static void KexQueenHatchSeedPod(short nEgg)
{
    short const nSprite = QueenEgg[nEgg].nSprite;
    sprite[nSprite].cstat = 0x8000;
    if (s_queenTest >= 0 && nEgg == s_queenTestEgg)
    {
        s_queenTestPodHatches++;
        KexQueenDeleteSeedPod(nEgg);
        return;
    }

    // Obj_Seedling_Spawn's terminal callback emits Fx_Explosion01 at z+32.
    if (KexExplosion01_Spawn(sprite[nSprite].x, sprite[nSprite].y,
            sprite[nSprite].z, sprite[nSprite].sectnum, 32, 0) == -1)
    {
        int const oldZ = sprite[nSprite].z;
        sprite[nSprite].z -= 32 * 16 * 6;
        BuildExplosion(nSprite);
        sprite[nSprite].z = oldZ;
    }

    BuildWasp(-2, sprite[nSprite].x, sprite[nSprite].y,
              sprite[nSprite].z, sprite[nSprite].sectnum,
              sprite[nSprite].ang);
    KexQueenDeleteSeedPod(nEgg);
}

static void FuncKexQueenEgg(int a, short nEgg)
{
    Egg *const pEgg = &QueenEgg[nEgg];
    short const nSprite = pEgg->nSprite;
    switch (a & kMessageMask)
    {
        case 0x20000:
        {
            int const nSeq = SeqOffsets[kSeqQueenEgg];
            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, 0);
            seq_MoveSequence(nSprite, nSeq, 0);
            MoveCreatureWithCaution(nSprite);
            if (s_queenTest >= 0 && nEgg == s_queenTestEgg)
                s_queenTestPodTicks++;
            if (--pEgg->field_C <= 0)
                KexQueenHatchSeedPod(nEgg);
            return;
        }

        case 0x90000:
            seq_PlotSequence(a & 0xFFFF, SeqOffsets[kSeqQueenEgg], 0, 1);
            return;

        // The authored pod is neither solid nor shootable.
        case 0x80000:
        case 0xA0000:
            return;

        default:
            DebugOut("unknown msg %d for KEX Queen seed pod\n",
                     a & kMessageMask);
            return;
    }
}

void FuncQueenEgg(int a, int nDamage, int nRun)
{
    short nEgg = RunData[nRun].nVal;

    if (KexQueenSequenceExact() && QueenEgg[nEgg].field_E == 0)
    {
        FuncKexQueenEgg(a, nEgg);
        return;
    }

    Egg *pEgg = &QueenEgg[nEgg];
    short nSprite = pEgg->nSprite;
    short nAction = pEgg->nAction;

    short nTarget;

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Queenhead\n", nMessage);
            break;
        }

        case 0x20000:
        {
            if (pEgg->nHealth <= 0)
            {
                DestroyEgg(nEgg);
                return;
            }

            if (nAction == 0 || nAction == 4) {
                Gravity(nSprite);
            }

            short nSeq = SeqOffsets[kSeqQueenEgg] + EggSeq[nAction].a;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, pEgg->field_2);

            if (nAction != 4)
            {
                seq_MoveSequence(nSprite, nSeq, pEgg->field_2);

                pEgg->field_2++;
                if (pEgg->field_2 >= SeqSize[nSeq])
                {
                    pEgg->field_2 = 0;
                    bVal = true;
                }

                nTarget = UpdateEnemy(&pEgg->nTarget);
                pEgg->nTarget = nTarget;

                if (nTarget >= 0 && (sprite[nTarget].cstat & 0x101) == 0)
                {
                    pEgg->nTarget = -1;
                    pEgg->nAction = 0;
                }
                else
                {
                    nTarget = FindPlayer(-nSprite, 1000);
                    pEgg->nTarget = nTarget;
                }
            }

            switch (nAction)
            {
                case 0:
                {
                    int nMov = MoveCreature(nSprite);
                    if (!nMov) {
                        break;
                    }

                    if (nMov & 0x20000)
                    {
                        if (!RandomSize(1))
                        {
                            pEgg->nAction = 1;
                            pEgg->field_2 = 0;
                        }
                        else
                        {
                            DestroyEgg(nEgg);
                        }
                    }
                    else
                    {
                        short nAngle;

                        switch (nMov & 0xC000)
                        {
                        default:
                            return;
                        case 0x8000:
                            nAngle = GetWallNormal(nMov & 0x3FFF);
                            break;
                        case 0xC000:
                            nAngle = sprite[nMov & 0x3FFF].ang;
                            break;
                        }

                        sprite[nSprite].ang = nAngle;
                        sprite[nSprite].xvel = Cos(nAngle) >> 1;
                        sprite[nSprite].yvel = Sin(nAngle) >> 1;
                    }

                    break;
                }

                case 1:
                {
                    if (bVal)
                    {
                        pEgg->nAction = 3;
                        sprite[nSprite].cstat = 0x101;
                    }
                    break;
                }

                case 2:
                case 3:
                {
                    int nMov = QueenAngleChase(nSprite, nTarget, nHeadVel, 64);

                    switch (nMov & 0xC000)
                    {
                    case 0xC000:
                        if (sprite[nMov & 0x3FFF].statnum != 121)
                        {
                            runlist_DamageEnemy(nMov & 0x3FFF, nSprite, 5);
                        }
                        fallthrough__;
                    case 0x8000:
                        sprite[nSprite].ang += (RandomSize(9) + 768);
                        sprite[nSprite].ang &= kAngleMask;
                        sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 3;
                        sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 3;
                        sprite[nSprite].zvel = -RandomSize(5);
                        break;
                    }

                    return;
                }

                case 4:
                {
                    int nMov = MoveCreature(nSprite);

                    if (nMov & 0x20000)
                    {
                        sprite[nSprite].zvel = -(sprite[nSprite].zvel - 256);
                        if (sprite[nSprite].zvel < -512)
                        {
                            sprite[nSprite].zvel = 0;
                        }
                    }

                    pEgg->field_C--;
                    if (pEgg->field_C <= 0)
                    {
                        int nWasp = BuildWasp(-2, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum, sprite[nSprite].ang);
                        if (nWasp >= 0)
                        {
                            int const nWaspSprite = GetWaspSprite(nWasp);
                            sprite[nSprite].z = sprite[nWaspSprite].z;
                        }

                        DestroyEgg(nEgg);
                    }
                    break;
                }
            }
            break;
        }

        case 0xA0000:
        {
            if (sprite[nRadialSpr].statnum != 121 && (sprite[nSprite].cstat & 0x101) != 0)
            {
                nDamage = runlist_CheckRadialDamage(nSprite);

                pEgg->nHealth -= nDamage;
                if (g_kexMode && nDamage > 0 && pEgg->nHealth > 0)
                    KexDifficulty_PainRoll(0);
            }
            break;
        }

        case 0x80000:
        {
            if (nDamage != 0 && pEgg->nHealth > 0)
            {
                QueenEgg[nEgg].nHealth -= nDamage;

                if (QueenEgg[nEgg].nHealth <= 0)
                    DestroyEgg(nEgg);
                else if (g_kexMode)
                    KexDifficulty_PainRoll(0);
            }
            break;
        }

        case 0x90000:
        {
            seq_PlotSequence(a & 0xFFFF, SeqOffsets[kSeqQueenEgg] + EggSeq[nAction].a, pEgg->field_2, EggSeq[nAction].b);
            break;
        }
    }
}

int BuildQueenHead(short nQueen)
{
    short nSprite = QueenList[nQueen].nSprite;
    bool const bExactQueen = KexQueenSequenceExact();

    int x = sprite[nSprite].x;
    int y = sprite[nSprite].y;
    short nAngle = sprite[nSprite].ang;
    short nSector = sprite[nSprite].sectnum;
    int z = bExactQueen ? sprite[nSprite].z : sector[nSector].floorz;

    int nSprite2 = insertsprite(nSector, 121);
    assert(nSprite2 >= 0 && nSprite2 < kMaxSprites);

    sprite[nSprite2].x = x;
    sprite[nSprite2].y = y;
    sprite[nSprite2].z = z;
    sprite[nSprite2].clipdist = bExactQueen ? 48 : 70;
    sprite[nSprite2].xrepeat = 80;
    sprite[nSprite2].yrepeat = 80;
    sprite[nSprite2].cstat = bExactQueen ? 1 : 0;
    sprite[nSprite2].picnum = 1;
    sprite[nSprite2].shade = -12;
    sprite[nSprite2].pal = 0;
    sprite[nSprite2].xoffset = 0;
    sprite[nSprite2].yoffset = 0;
    sprite[nSprite2].ang = nAngle;

    if (bExactQueen)
    {
        sprite[nSprite2].xvel = 0;
        sprite[nSprite2].yvel = 0;
        sprite[nSprite2].zvel = 0;
    }
    else
    {
        nVelShift = 2;
        SetHeadVel(nSprite2);
        sprite[nSprite2].zvel = -8192;
    }
    sprite[nSprite2].lotag = runlist_HeadRun() + 1;
    sprite[nSprite2].hitag = 0;
    sprite[nSprite2].extra = -1;

    GrabTimeSlot(3);

    // AI_Queen_Snake has 200 health in the KEX definitions; the classic
    // detached head keeps its native 800-health value.
    QueenHead.nHealth = g_kexMode ? KexHealthUnits(200) : 800;
    QueenHead.nAction = 0;
    QueenHead.nTarget = QueenList[nQueen].nTarget;
    QueenHead.field_2 = 0;
    QueenHead.nSprite = nSprite2;
    QueenHead.field_C = 0;

    sprite[nSprite2].owner = runlist_AddRunRec(sprite[nSprite2].lotag - 1, nQueen, kRunQueenHead);

    QueenHead.field_8 = runlist_AddRunRec(NewRun, nQueen, kRunQueenHead);
    QueenHead.field_E = 0;

    return 0;
}

static void KexQueenUpdateTailJoints()
{
    short nParent = QueenHead.nSprite;
    for (int i = 0; i < QueenHead.field_E; i++)
    {
        short const nTail = tailspr[i];
        if ((unsigned)nTail >= (unsigned)kMaxSprites
            || sprite[nTail].sectnum < 0)
            continue;

        sprite[nTail].x += (sprite[nParent].x - sprite[nTail].x) / 5;
        sprite[nTail].y += (sprite[nParent].y - sprite[nTail].y) / 5;
        sprite[nTail].z += (sprite[nParent].z - sprite[nTail].z) / 5;
        sprite[nTail].ang = sprite[nParent].ang;
        if (sprite[nTail].sectnum != sprite[nParent].sectnum)
            mychangespritesect(nTail, sprite[nParent].sectnum);
        nParent = nTail;
    }
}

static void KexQueenDestroyTailLink(int nHead)
{
    int const index = nHead - 1;
    short const nTail = tailspr[index];

    if (s_queenTest >= 0)
        s_queenTestTailDeaths++;
    else
    {
        if (KexExplosion01_Spawn(sprite[nTail].x, sprite[nTail].y,
                sprite[nTail].z, sprite[nTail].sectnum, 8, 0) == -1)
        {
            int const oldZ = sprite[nTail].z;
            sprite[nTail].z -= 8 * 16 * 6;
            BuildExplosion(nTail);
            sprite[nTail].z = oldZ;
        }
    }

    runlist_DoSubRunRec(sprite[nTail].owner);
    runlist_DoSubRunRec(sprite[nTail].lotag - 1);
    mydeletesprite(nTail);
    tailspr[index] = -1;
    QueenHead.field_E--;

    if (QueenHead.field_E > 0)
        sprite[tailspr[QueenHead.field_E - 1]].cstat |= 0x100;
    else
        sprite[QueenHead.nSprite].cstat |= 0x100;
}

static void KexQueenSpawnSnakeExplosion(short nSprite)
{
    if (s_queenTest >= 0)
    {
        s_queenTestSnakeExplosions++;
        return;
    }
    int const x = sprite[nSprite].x + KexQueenSignedRandom(128 * 6);
    int const y = sprite[nSprite].y + KexQueenSignedRandom(128 * 6);
    int const z = sprite[nSprite].z
        - KexQueenSignedRandom(128 * 16 * 6);
    short nSector = sprite[nSprite].sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0)
        nSector = sprite[nSprite].sectnum;
    if (KexExplosion01_Spawn(x, y, z, nSector, 192, 0) == -1)
        BuildAnim(-1, 36, 0, x, y, z - 192 * 16 * 6, nSector,
                  sprite[nSprite].xrepeat, 4);
}

static void KexQueenBeginSnakeDeath(short nSprite)
{
    // A_QueenDeath fires the arena's tag 1010 with the first snake-death
    // explosion, before the remaining explosion frames consume the actor.
    if (g_kexMode && s_queenTest < 0)
        KexScript_BossEvent(nSprite, "Queen snake death event");
    QueenHead.nAction = kKexQueenSnakeExplode;
    QueenHead.field_2 = 0;
    QueenHead.field_C = 0;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
}

static void KexQueenDeleteSnake(short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_DoSubRunRec(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(QueenHead.field_8);
    mydeletesprite(nSprite);
    QueenHead.nSprite = -1;
    QueenHead.nAction = kKexQueenSnakeInert;
}

static void KexQueenSnakeHop(short nSprite, short nTarget)
{
    if (!KexQueenValidTarget(nTarget) || sprite[nSprite].zvel != 0)
        return;

    if ((totalmoves & 512) == 0)
    {
        int64_t const nearDistance = (int64_t)1024 * 6;
        int const turn = KexQueenDistanceSq(nSprite, nTarget)
            < nearDistance * nearDistance ? 128 : 16;
        KexQueenTurnToward(nSprite, nTarget, turn);
    }

    if (((uint8_t)RandomByte() & 31) != 0)
        return;

    int hop = 16 + ((uint32_t)RandomWord() * 16) / 65535;
    int64_t const halfDistance = (int64_t)512 * 6;
    if (KexQueenDistanceSq(nSprite, nTarget)
        <= halfDistance * halfDistance)
        hop /= 2;
    sprite[nSprite].zvel = -hop * 16 * 6;
}

static void KexQueenSnakeTick(short nSprite)
{
    KexQueenSanitizeSnake();
    int const nAction = QueenHead.nAction;
    int const nFrame = QueenHead.field_2;
    int const nSeq = SeqOffsets[kSeqQueen]
        + KexQueenSnakeSeq[nAction].a;

    if (nAction != kKexQueenSnakeExplode)
    {
        sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nFrame);
        seq_MoveSequence(nSprite, nSeq, nFrame);
    }
    else
    {
        // queen_snake_explode is callback-only and intentionally spriteless.
        seq_MoveSequence(nSprite, nSeq, nFrame);
        if (nFrame == 0)
        {
            if (s_queenTest >= 0)
                s_queenTestSnakeTriggers++;
            else
                runlist_ChangeChannel(QueenChan[0], 1);
        }
        if (nFrame <= 80 && (nFrame & 3) == 0)
            KexQueenSpawnSnakeExplosion(nSprite);
        if (nFrame >= 84)
        {
            KexQueenDeleteSnake(nSprite);
            return;
        }
        QueenHead.field_2++;
        return;
    }

    short nTarget = QueenHead.nTarget;
    if (!KexQueenValidTarget(nTarget))
    {
        nTarget = KexQueenFindPlayer(nSprite);
        QueenHead.nTarget = nTarget;
    }

    switch (nAction)
    {
        case kKexQueenSnakeIdle:
            if (QueenHead.field_E == 0)
                BuildTail();
            QueenHead.nAction = kKexQueenSnakeChase;
            QueenHead.field_2 = 0;
            break;

        case kKexQueenSnakeChase:
            if (nTarget < 0)
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                Gravity(nSprite);
                MoveCreatureWithCaution(nSprite);
                break;
            }

            if (QueenHead.field_C && !KexQueenMeleeRange(nSprite, nTarget, 48))
                QueenHead.field_C = 0;

            if (!QueenHead.field_C && KexQueenMeleeRange(nSprite, nTarget, 48))
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                QueenHead.nAction = kKexQueenSnakeMelee;
                QueenHead.field_2 = 0;
                break;
            }

            KexQueenSnakeHop(nSprite, nTarget);
            if (QueenHead.field_C)
            {
                int const away = (GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                             sprite[nTarget].y - sprite[nSprite].y)
                                  + 1024) & kAngleMask;
                sprite[nSprite].ang = away;
            }
            KexQueenSetVelocity(nSprite, 16, sprite[nSprite].ang);
            Gravity(nSprite);
            MoveCreatureWithCaution(nSprite);
            break;

        case kKexQueenSnakeMelee:
            if (KexQueenMeleeRange(nSprite, nTarget, 48))
            {
                if (s_queenTest >= 0)
                    s_queenTestSnakeMelee = 30;
                runlist_DamageEnemy(nTarget, nSprite, 30);
            }
            QueenHead.field_C = 1; // retreatAfterMelee
            QueenHead.nAction = kKexQueenSnakeChase;
            QueenHead.field_2 = 0;
            break;
    }

    KexQueenUpdateTailJoints();
    if (QueenHead.nAction == nAction && QueenHead.field_2 == nFrame)
    {
        QueenHead.field_2++;
        if (QueenHead.field_2 >= SeqSize[nSeq])
            QueenHead.field_2 = 0;
    }
}

static void FuncKexQueenHead(int a, int nDamage, int nHead)
{
    short const nSprite = QueenHead.nSprite;
    if (!KexQueenSpriteValid(nSprite))
        return;
    KexQueenSanitizeSnake();

    switch (a & 0x7F0000)
    {
        case 0x20000:
            if (nHead == 0)
                KexQueenSnakeTick(nSprite);
            return;

        case 0xA0000:
            if (sprite[nRadialSpr].statnum == 121)
                return;
            if (nHead > 0)
            {
                if (nHead != QueenHead.field_E)
                    return;
                nDamage = runlist_CheckRadialDamage(tailspr[nHead - 1]);
            }
            else
                nDamage = runlist_CheckRadialDamage(nSprite);
            if (!nDamage)
                return;
            fallthrough__;

        case 0x80000:
            if (nDamage <= 0)
                return;
            if (nHead > 0)
            {
                if (nHead != QueenHead.field_E
                    || (sprite[tailspr[nHead - 1]].cstat & 0x100) == 0)
                    return;
                MoveQA[nHead - 1] -= nDamage;
                KexDifficulty_PainRoll(0);
                if (MoveQA[nHead - 1] <= 0)
                    KexQueenDestroyTailLink(nHead);
                return;
            }

            if ((sprite[nSprite].cstat & 0x100) == 0
                || QueenHead.nAction == kKexQueenSnakeExplode)
                return;
            QueenHead.nHealth -= nDamage;
            KexDifficulty_PainRoll(0);
            if (QueenHead.nHealth <= 0)
                KexQueenBeginSnakeDeath(nSprite);
            return;

        case 0x90000:
            if (nHead > 0)
            {
                seq_PlotSequence(a & 0xFFFF, SeqOffsets[kSeqQueen] + 102,
                                 0, 1);
                return;
            }
            if (QueenHead.nAction == kKexQueenSnakeExplode
                || QueenHead.nAction == kKexQueenSnakeInert)
                return;
            seq_PlotSequence(a & 0xFFFF,
                SeqOffsets[kSeqQueen]
                    + KexQueenSnakeSeq[QueenHead.nAction].a,
                QueenHead.field_2,
                KexQueenSnakeSeq[QueenHead.nAction].b);
            return;

        default:
            DebugOut("unknown msg %d for KEX Queen snake\n", a & 0x7F0000);
            return;
    }
}

void FuncQueenHead(int a, int nDamage, int nRun)
{
    short nHead = RunData[nRun].nVal;

    if (KexQueenSequenceExact())
    {
        FuncKexQueenHead(a, nDamage, nHead);
        return;
    }

    short nSprite = QueenHead.nSprite;
    assert(sprite[nSprite].sectnum >= 0
           && sprite[nSprite].sectnum < kMaxSectors);

    short nAction = QueenHead.nAction;

    short nTarget, nHd;

    int var_14 = 0;

    int nMessage = a & 0x7F0000;

    switch (nMessage)
    {
        case 0x20000:
        {
            if (nAction == 0) {
                Gravity(nSprite);
            }

            short nSeq = SeqOffsets[kSeqQueen] + HeadSeq[QueenHead.nAction].a;

            seq_MoveSequence(nSprite, nSeq, QueenHead.field_2);

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, QueenHead.field_2);

            QueenHead.field_2++;
            if (QueenHead.field_2 >= SeqSize[nSeq])
            {
                QueenHead.field_2 = 0;
                var_14 = 1;
            }

            nTarget = QueenHead.nTarget;

            if (nTarget > -1)
            {
                if (!(sprite[nTarget].cstat & 0x101))
                {
                    nTarget = -1;
                    QueenHead.nTarget = nTarget;
                }
            }
            else
            {
                nTarget = FindPlayer(nSprite, 1000);
                QueenHead.nTarget = nTarget;
            }

            switch (nAction)
            {
                case 0:
                    if (QueenHead.field_C > 0)
                    {
                        QueenHead.field_C--;
                        if (QueenHead.field_C == 0)
                        {
                            BuildTail();

                            QueenHead.nAction = 6;
                            nHeadVel = 800;
                            sprite[nSprite].cstat = 0x101;
                        }
                        else if (QueenHead.field_C < 60)
                        {
                            sprite[nSprite].shade--;
                        }
                    }
                    else
                    {
                        int nMov = MoveCreature(nSprite);

                        // original BUG - this line doesn't exist in original code?
                        short nNewAng = sprite[nSprite].ang;

                        switch (nMov & 0xFC000)
                        {
                        default:
                            return;
                        case 0xC000:
                            nNewAng = sprite[nMov & 0x3FFF].ang;
                            break;
                        case 0x8000:
                            nNewAng = GetWallNormal(nMov & 0x3FFF);
                            break;
                        case 0x20000:
                            sprite[nSprite].zvel = -(sprite[nSprite].zvel >> 1);

                            if (sprite[nSprite].zvel > -256)
                            {
                                nVelShift = 100;
                                sprite[nSprite].zvel = 0;
                            }
                            break;
                        }

                        // original BUG - var_18 isn't being set if the check above == 0x20000 ?
                        sprite[nSprite].ang = nNewAng;
                        nVelShift++;

                        if (nVelShift < 5)
                        {
                            SetHeadVel(nSprite);
                        }
                        else
                        {
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;

                            if (sprite[nSprite].zvel == 0)
                            {
                                QueenHead.field_C = 120;
                            }
                        }
                    }

                    break;

                case 6:
                    if (var_14)
                    {
                        QueenHead.nAction = 1;
                        QueenHead.field_2 = 0;
                        break;
                    }
                    fallthrough__;

                case 1:
                    if ((sprite[nTarget].z - 51200) > sprite[nSprite].z)
                    {
                        QueenHead.nAction = 4;
                        QueenHead.field_2 = 0;
                    }
                    else
                    {
                        sprite[nSprite].z -= 2048;
                        goto __MOVEQS;
                    }
                    break;

                case 4:
                case 7:
                case 8:
                    // AI_Queen_Snake's A_QueenSnakeHop gives the detached
                    // head a small random hop whenever it is grounded. The
                    // native controller only supplied the contact bounce;
                    // preserve that classic behavior and add the KEX event.
                    if (g_kexMode && nTarget >= 0 && sprite[nSprite].zvel == 0 && !RandomSize(5))
                    {
                        // A_QueenSnakeHop uses a 16..32-unit impulse, halved
                        // when the head is within 512 authored units of the
                        // player.  Converted XY coordinates are six times
                        // larger, so apply the same threshold in Build space.
                        int const dx = sprite[nTarget].x - sprite[nSprite].x;
                        int const dy = sprite[nTarget].y - sprite[nSprite].y;
                        int nHop = 16 + (RandomSize(8) * 16) / 256;
                        if (ksqrt(dx * dx + dy * dy) <= 512 * 6)
                            nHop /= 2;
                        sprite[nSprite].zvel = -nHop * 16 * 6;
                    }

                    if (var_14)
                    {
                        int nRnd = RandomSize(2);

                        if (nRnd == 0)
                        {
                            QueenHead.nAction = 4;
                        }
                        else if (nRnd == 1)
                        {
                            QueenHead.nAction = 7;
                        }
                        else
                        {
                            QueenHead.nAction = 8;
                        }
                    }

                    if (nTarget > -1)
                    {
                        int nMov = QueenAngleChase(nSprite, nTarget, nHeadVel, 64);

                        switch (nMov & 0xC000)
                        {
                        case 0x8000:
                            break;
                        case 0xC000:
                            if ((nMov & 0x3FFF) == nTarget)
                            {
                                runlist_DamageEnemy(nTarget, nSprite, 10);
                                D3PlayFX(StaticSound[kSoundQTail] | 0x2000, nSprite);

                                sprite[nSprite].ang += RandomSize(9) + 768;
                                sprite[nSprite].ang &= kAngleMask;

                                sprite[nSprite].zvel = (-20) - RandomSize(6);

                                SetHeadVel(nSprite);
                            }
                            break;
                        }
                    }

                    // switch break. MoveQS stuff?
__MOVEQS:
                    MoveQX[nQHead] = sprite[nSprite].x;
                    MoveQY[nQHead] = sprite[nSprite].y;
                    MoveQZ[nQHead] = sprite[nSprite].z;
                    assert(sprite[nSprite].sectnum >= 0 && sprite[nSprite].sectnum < kMaxSectors);
                    MoveQS[nQHead] = sprite[nSprite].sectnum;
                    MoveQA[nQHead] = sprite[nSprite].ang;

                    nHd = nQHead;

                    for (int i = 0; i < QueenHead.field_E; i++)
                    {
                        nHd -= 3;
                        if (nHd < 0) {
                            nHd += (24 + 1); // TODO - enum/define for these
                            //assert(nHd < 24 && nHd >= 0);
                        }

                        int var_20 = MoveQS[nHd];
                        short nTSprite = tailspr[i];

                        if (var_20 != sprite[nTSprite].sectnum)
                        {
                            assert(var_20 >= 0 && var_20 < kMaxSectors);
                            mychangespritesect(nTSprite, var_20);
                        }

                        sprite[nTSprite].x = MoveQX[nHd];
                        sprite[nTSprite].y = MoveQY[nHd];
                        sprite[nTSprite].z = MoveQZ[nHd];
                        sprite[nTSprite].ang = MoveQA[nHd];
                    }

                    nQHead++;
                    if (nQHead >= 25)
                    {
                        nQHead = 0;
                    }

                    break;

                case 5:
                    QueenHead.field_C--;
                    if (QueenHead.field_C <= 0)
                    {
                        QueenHead.field_C = 3;

                        if (QueenHead.field_E--)
                        {
                            if (QueenHead.field_E >= 15 || QueenHead.field_E < 10)
                            {
                                int x = sprite[nSprite].x;
                                int y = sprite[nSprite].y;
                                int z = sprite[nSprite].z;
                                short nSector = sprite[nSprite].sectnum;
                                int nAngle = RandomSize(11) & kAngleMask;

                                sprite[nSprite].xrepeat = 127 - QueenHead.field_E;
                                sprite[nSprite].yrepeat = 127 - QueenHead.field_E;

                                sprite[nSprite].cstat = 0x8000;

                                // DEMO-TODO: in disassembly angle was used without masking and thus causing OOB issue.
                                // This behavior probably would be needed emulated for demo compatibility
                                // int dx = sintable[nAngle + 512] << 10;
                                int dx = Cos(nAngle) << 10;
                                int dy = Sin(nAngle) << 10;
                                int dz = (RandomSize(5) - RandomSize(5)) << 7;

                                movesprite(nSprite, dx, dy, dz, 0, 0, CLIPMASK1);

                                BlowChunks(nSprite);
                                BuildExplosion(nSprite);

                                mychangespritesect(nSprite, nSector);

                                sprite[nSprite].x = x;
                                sprite[nSprite].y = y;
                                sprite[nSprite].z = z;

                                if (QueenHead.field_E < 10)
                                {
                                    for (int i = (10 - QueenHead.field_E) * 2; i > 0; i--)
                                    {
                                        BuildLavaLimb(nSprite, i, GetSpriteHeight(nSprite));
                                    }
                                }
                            }
                        }
                        else
                        {
                            BuildExplosion(nSprite);

                            int i;

                            for (i = 0; i < 10; i++)
                            {
                                BlowChunks(nSprite);
                            }

                            for (i = 0; i < 20; i++)
                            {
                                BuildLavaLimb(nSprite, i, GetSpriteHeight(nSprite));
                            }

                            runlist_SubRunRec(sprite[nSprite].owner);
                            runlist_SubRunRec(QueenHead.field_8);
                            mydeletesprite(nSprite);
                            runlist_ChangeChannel(QueenChan[0], 1);
                        }
                    }
                    break;
            }
            break;
        }

        case 0xA0000:
            if (sprite[nRadialSpr].statnum != 121 && (sprite[nSprite].cstat & 0x101) != 0)
            {
                nDamage = runlist_CheckRadialDamage(nSprite);
                if (!nDamage)
                    break;
            }
            else
                break;
            // fall through to case 0x80000
            fallthrough__;

        case 0x80000:
            // The KEX snake tail is a separate actor, but the native Build
            // handler is shared with the detached head. Only the terminal
            // tail is shootable in the authored actor definition; its death
            // reveals the head instead of consuming head health.
            if (g_kexMode && nHead > 0)
            {
                if (nHead == QueenHead.field_E)
                {
                    DestroyTailPart();
                    QueenHead.nHealth = KexHealthUnits(200);
                    sprite[nSprite].cstat = 0x101;
                    OSD_Printf("Kex Queen snake: terminal tail destroyed, head exposed\n");
                }
                break;
            }
            if (QueenHead.nHealth > 0 && nDamage != 0)
            {
                QueenHead.nHealth -= nDamage;

                if (g_kexMode)
                {
                    if (QueenHead.nHealth > 0)
                        KexDifficulty_PainRoll(0);
                }
                else if (!RandomSize(4))
                {
                    QueenHead.nTarget = a & 0xFFFF;
                    QueenHead.nAction = 7;
                    QueenHead.field_2 = 0;
                }

                if (QueenHead.nHealth <= 0)
                {
                    if (g_kexMode)
                    {
                        // The authored snake's head has no detachable-tail
                        // health ladder: once exposed, it dies as one actor.
                        for (int i = 0; i < QueenHead.field_E; i++)
                            if (tailspr[i] >= 0)
                                mydeletesprite(tailspr[i]);
                        QueenHead.field_E = 0;
                        QueenHead.nAction = 5;
                        QueenHead.field_2 = 0;
                        QueenHead.field_C = 0;
                        QueenHead.field_E = 80;
                        sprite[nSprite].cstat = 0;
                    }
                    else if (DestroyTailPart())
                    {
                        QueenHead.nHealth = 200;
                        nHeadVel += 100;
                    }
                    else
                    {
                        QueenHead.nAction = 5;
                        QueenHead.field_2 = 0;
                        QueenHead.field_C = 0;
                        QueenHead.field_E = 80;
                        sprite[nSprite].cstat = 0;
                    }
                }
            }
            break;

        case 0x90000:
        {
            short nSeq = SeqOffsets[kSeqQueen];

            int edx;

            if (nHead == 0)
            {
                edx = HeadSeq[nAction].b;
                nSeq += HeadSeq[nAction].a;
            }
            else
            {
                edx = 1;
                nSeq += 73;
            }

            seq_PlotSequence(a & 0xFFFF, nSeq, QueenHead.field_2, edx);
            break;
        }

        default:
        {
            DebugOut("unknown msg %d for Queenhead\n", a & 0x7F0000);
            break;
        }
    }
}

int BuildQueen(int nSprite, int x, int y, int z, int nSector, int nAngle, int nChannel)
{
    bool const bExactQueen = KexQueenSequenceExact();
    // QueenCount is the number of free slots, not a signed allocation
    // cursor.  A malformed map or a late script request must leave the
    // already-live retail Queen intact when the one-slot pool is full.
    if (QueenCount <= 0)
        return -1;
    short const nQueen = --QueenCount;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 121);
    }
    else
    {
        changespritestat(nSprite, 121);
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
    sprite[nSprite].pal = 0;
    sprite[nSprite].shade = -12;
    sprite[nSprite].clipdist = bExactQueen ? 96 : 100;
    sprite[nSprite].xrepeat = bExactQueen ? 60 : 80;
    sprite[nSprite].yrepeat = bExactQueen ? 60 : 80;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;
    sprite[nSprite].hitag = 0;

    GrabTimeSlot(3);

    QueenList[nQueen].nAction = 0;
    // The exact retail path keeps AI_Queen's single authored 3600-point pool.
    // Partial conversions retain the older three-phase compatibility path;
    // classic encounters keep their native 4000 points per phase.
    QueenList[nQueen].nHealth = bExactQueen ? KexHealthUnits(3600)
        : g_kexMode ? KexQueenPhaseHealth() : 4000;
    QueenList[nQueen].nFrame = 0;
    QueenList[nQueen].nSprite = nSprite;
    QueenList[nQueen].nTarget = -1;
    QueenList[nQueen].field_A = 0;
    QueenList[nQueen].field_10 = 5;
    QueenList[nQueen].field_C = 0;
    QueenList[nQueen].field_12 = 0;

    QueenChan[nQueen] = nChannel;

    nHeadVel = 800;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nQueen, kRunQueen);

    runlist_AddRunRec(NewRun, nQueen, kRunQueen);

    nCreaturesLeft++;

    if (s_queenAudit < 0)
        s_queenAudit = g_kexMode && Bgetenv("VB_KEX_QUEEN_AUDIT") ? 1 : 0;
    if (s_queenAudit && !s_queenAuditSeen)
    {
        s_queenAuditSeen = 1;
        OSD_Printf("Kex Queen: sprite %d hp %d clip %d repeat %d exact %d\n",
                   nSprite, KexHealthPoints(QueenList[nQueen].nHealth),
                   (int)sprite[nSprite].clipdist,
                   (int)sprite[nSprite].xrepeat,
                   bExactQueen ? 1 : 0);
    }

    return nQueen;
}

void SetQueenSpeed(short nSprite, int nSpeed)
{
    sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> (2 - nSpeed);
    sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> (2 - nSpeed);
}

// AI_QueenThink moves laterally around its target at four KEX units per tick
// while the chase animation is playing.  Keep the native phase/attack state
// machine, but replace its direct pursuit velocity in KEX mode.
static void KexQueenStrafe(short nSprite, short nTarget)
{
    int const nTargetAngle = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                        sprite[nTarget].y - sprite[nSprite].y);
    int const nStrafeAngle = (nTargetAngle + (RandomBit() ? RandomSize(9) : -RandomSize(9))) & kAngleMask;

    sprite[nSprite].ang = nTargetAngle;
    sprite[nSprite].xvel = (Cos(nStrafeAngle) * (4 * 6)) >> 8;
    sprite[nSprite].yvel = (Sin(nStrafeAngle) * (4 * 6)) >> 8;
}

static void KexQueenBeginAction(short nQueen, int nAction)
{
    QueenList[nQueen].nAction = (short)nAction;
    QueenList[nQueen].nFrame = 0;
}

static void KexQueenThinkHigh(short nQueen, short nSprite, short nTarget)
{
    if (!KexQueenValidTarget(nTarget))
        return;

    if (KexHealthPoints(QueenList[nQueen].nHealth) <= 2400)
    {
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        KexQueenBeginAction(nQueen, kKexQueenPain);
        return;
    }

    KexQueenTurnToward(nSprite, nTarget, 8); // 0.025 radians
    if (!KexQueenRandomDecision(14)
        && (((uint8_t)RandomByte() & 7) == 0))
        KexQueenBeginAction(nQueen, kKexQueenTailMissile);

    if (KexQueenRandomDecision(62))
        return;

    if ((uint8_t)RandomByte() <= 16)
        KexQueenPlaySound(nSprite, "q_gurgle");

    int const targetAngle = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                       sprite[nTarget].y - sprite[nSprite].y);
    int strafeAngle = (targetAngle + KexQueenSignedRandom(512)) & kAngleMask;
    if (((uint8_t)RandomByte() & 1) != 0)
        strafeAngle = (-strafeAngle) & kAngleMask;
    KexQueenSetVelocity(nSprite, 4, strafeAngle);
}

static void KexQueenThinkStumpy(short nQueen, short nSprite)
{
    if (KexQueenRandomDecision(14))
        return;

    if ((uint8_t)RandomByte() <= 16)
        KexQueenPlaySound(nSprite, "q_stumpy");

    if (KexHealthPoints(QueenList[nQueen].nHealth) <= 1200
        && (((uint8_t)RandomByte() & 1) != 0))
    {
        if (KexQueenSeedCount() < 8)
            KexQueenBeginAction(nQueen, kKexQueenHurl);
    }
    else if (((uint8_t)RandomByte() & 7) == 0)
        KexQueenBeginAction(nQueen, kKexQueenCharge);
}

static void KexQueenPursue(short nSprite, short nTarget)
{
    PlotCourseToSprite(nSprite, nTarget);
    KexQueenSetVelocity(nSprite, 8, sprite[nSprite].ang);
}

static void KexQueenPainGibs(short nSprite)
{
    if (s_queenTest >= 0)
    {
        s_queenTestPainGibs += 5;
        return;
    }
    static int const gibTiles[] = { 7841, 7843, 7844, 7845, 7846 };
    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);
}

static void KexQueenBodyDeath(short nQueen, short nSprite)
{
    // queen_death calls A_GibDeath(self, 64) on frame 61. The focused Queen
    // lifecycle test counts callbacks without creating world-side actors;
    // Fx_Gibs01 has its own independent exact actor probe.
    if (s_queenTest < 0)
        KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
            sprite[nSprite].z, sprite[nSprite].sectnum, 64,
            sprite[nSprite].xrepeat, QueenList[nQueen].nTarget);

    if (s_queenTest >= 0)
        s_queenTestBodyGibs += 6;
    else
    {
        static int const gibTiles[] = { 7841, 7842, 7843, 7844, 7845, 7846 };
        for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
            KexTossCreatureChunk(nSprite, gibTiles[i]);
    }

    BuildQueenHead(nQueen);
    sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
    changespritestat(nSprite, 0);
    KexQueenBeginAction(nQueen, kKexQueenInert);
}

static void KexQueenChargeTick(short nQueen, short nSprite, short nTarget)
{
    if (!KexQueenValidTarget(nTarget))
    {
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        KexQueenBeginAction(nQueen, kKexQueenChase);
        return;
    }

    int64_t const farDistance = (int64_t)192 * 6;
    int const turn = KexQueenDistanceSq(nSprite, nTarget)
        >= farDistance * farDistance ? 64 : 32;
    int const velocityRadius = ksqrt((uint32_t)(
        sprite[nSprite].xvel * sprite[nSprite].xvel
        + sprite[nSprite].yvel * sprite[nSprite].yvel)) >> 6;
    KexQueenTurnToward(nSprite, nTarget, turn);
    KexQueenSetVelocity(nSprite, 16, sprite[nSprite].ang);

    int64_t const radius = (int64_t)sprite[nSprite].clipdist * 4
        + (int64_t)sprite[nTarget].clipdist * 4 + velocityRadius;
    if (KexQueenDistanceSq(nSprite, nTarget) <= radius * radius)
    {
        int const chargeXVel = sprite[nSprite].xvel;
        int const chargeYVel = sprite[nSprite].yvel;
        runlist_DamageEnemy(nTarget, nSprite, 40);
        sprite[nTarget].xvel = chargeXVel * 2;
        sprite[nTarget].yvel = chargeYVel * 2;
        if (s_queenTest >= 0)
        {
            s_queenTestChargeDamage = 40;
            s_queenTestChargeKnockback
                = sprite[nTarget].xvel == chargeXVel * 2
                && sprite[nTarget].yvel == chargeYVel * 2;
        }
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        KexQueenBeginAction(nQueen, kKexQueenAfterCharge);
        return;
    }
    MoveCreatureWithCaution(nSprite);
}

static void KexQueenFireTailMissile(short nSprite, short nTarget)
{
    if (KexQueenValidTarget(nTarget))
    {
        int const nBullet = BuildKexAIFireProjectile(
            nSprite, 12, nTarget, -64, 128, 32, 0);
        if (s_queenTest >= 0)
        {
            s_queenTestProjectile = KexBullet_CheckAIFireProjectile(
                nBullet, nSprite, nTarget, -64, 128, 32, 0);
            if (nBullet >= 0)
                DestroyBullet(nBullet);
        }
    }
}

static void KexQueenBodyTick(short nQueen, short nSprite)
{
    KexQueenSanitizeBody(nQueen);
    int const nAction = QueenList[nQueen].nAction;
    int const nFrame = QueenList[nQueen].nFrame;
    int const nSeq = SeqOffsets[kSeqQueen] + KexQueenActionSeq[nAction].a;

    // queen_death frame 61 is callback-only and has no authored sprite.
    if (!(nAction == kKexQueenDeath && nFrame == 61))
    {
        sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nFrame);
        seq_MoveSequence(nSprite, nSeq, nFrame);
    }

    short nTarget = QueenList[nQueen].nTarget;
    if (!KexQueenValidTarget(nTarget))
    {
        nTarget = -1;
        QueenList[nQueen].nTarget = -1;
    }

    switch (nAction)
    {
        case kKexQueenIdle:
            nTarget = KexQueenFindPlayer(nSprite);
            if (nTarget >= 0)
            {
                QueenList[nQueen].nTarget = nTarget;
                KexQueenPlaySound(nSprite, "q_sight");
                KexQueenBeginAction(nQueen, kKexQueenChase);
            }
            break;

        case kKexQueenBase:
            KexQueenThinkHigh(nQueen, nSprite, nTarget);
            if (QueenList[nQueen].nAction == kKexQueenBase
                && KexQueenMeleeRange(nSprite, nTarget, 48))
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                KexQueenBeginAction(nQueen, kKexQueenFlap);
            }
            else if (QueenList[nQueen].nAction != kKexQueenPain)
                MoveCreatureWithCaution(nSprite);
            break;

        case kKexQueenChase:
            // queen_chase frame zero executes CheckState before ThinkStumpy.
            if (nFrame == 0
                && KexHealthPoints(QueenList[nQueen].nHealth) > 2400)
                KexQueenBeginAction(nQueen, kKexQueenBase);
            if (nTarget >= 0)
                KexQueenPursue(nSprite, nTarget);
            KexQueenThinkStumpy(nQueen, nSprite);
            if (QueenList[nQueen].nAction == kKexQueenChase
                && KexQueenMeleeRange(nSprite, nTarget, 48))
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                KexQueenBeginAction(nQueen, kKexQueenFlap);
            }
            else
                MoveCreatureWithCaution(nSprite);
            break;

        case kKexQueenCharge:
            KexQueenChargeTick(nQueen, nSprite, nTarget);
            break;

        case kKexQueenAfterCharge:
            if (nFrame == 9)
                KexQueenBeginAction(nQueen, kKexQueenChase);
            break;

        case kKexQueenFlap:
            if (nFrame == 1 && KexQueenValidTarget(nTarget))
            {
                PlotCourseToSprite(nSprite, nTarget);
                if (KexQueenMeleeRange(nSprite, nTarget, 48))
                {
                    if (s_queenTest >= 0)
                        s_queenTestBodyMelee = 50;
                    runlist_DamageEnemy(nTarget, nSprite, 50);
                }
            }
            if (nFrame == 10 && !KexQueenMeleeRange(nSprite, nTarget, 48))
                KexQueenBeginAction(nQueen, kKexQueenChase);
            break;

        case kKexQueenHurl:
            if (nFrame == 12 && KexQueenValidTarget(nTarget))
                PlotCourseToSprite(nSprite, nTarget);
            if (nFrame == 16)
            {
                BuildQueenEgg(nQueen, 0);
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
            }
            if (nFrame == 29 && (((uint8_t)RandomByte() & 3) == 0))
                KexQueenBeginAction(nQueen, kKexQueenChase);
            else if (sprite[nSprite].xvel || sprite[nSprite].yvel)
                MoveCreatureWithCaution(nSprite);
            break;

        case kKexQueenTailMissile:
            if (nFrame <= 14
                && !(s_queenTest >= 0 && nQueen == s_queenTestQueen))
                KexQueenThinkHigh(nQueen, nSprite, nTarget);
            if (nFrame == 7)
                KexQueenFireTailMissile(nSprite, nTarget);
            if (nFrame == 15 && (((uint8_t)RandomByte() & 3) == 0))
                KexQueenBeginAction(nQueen, kKexQueenChase);
            if (QueenList[nQueen].nAction != kKexQueenPain)
                MoveCreatureWithCaution(nSprite);
            break;

        case kKexQueenPain:
            if (nFrame == 0)
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                KexQueenPainGibs(nSprite);
            }
            if (nFrame == 12)
                KexQueenBeginAction(nQueen, kKexQueenChase);
            break;

        case kKexQueenDeath:
            if (nFrame == 61)
            {
                KexQueenBodyDeath(nQueen, nSprite);
                return;
            }
            break;

        case kKexQueenInert:
            return;
    }

    if (QueenList[nQueen].nAction == nAction
        && QueenList[nQueen].nFrame == nFrame)
    {
        QueenList[nQueen].nFrame++;
        if (QueenList[nQueen].nFrame >= SeqSize[nSeq])
            QueenList[nQueen].nFrame = 0;
    }
}

static void FuncKexQueen(int a, int nDamage, short nQueen)
{
    short const nSprite = QueenList[nQueen].nSprite;
    if (!KexQueenSpriteValid(nSprite))
        return;
    KexQueenSanitizeBody(nQueen);
    switch (a & kMessageMask)
    {
        case 0x20000:
            if (QueenList[nQueen].nAction != kKexQueenInert)
                KexQueenBodyTick(nQueen, nSprite);
            return;

        case 0xA0000:
            if (sprite[nRadialSpr].statnum == 121
                || (sprite[nSprite].cstat & 0x101) == 0)
                return;
            nDamage = runlist_CheckRadialDamage(nSprite);
            if (!nDamage)
                return;
            fallthrough__;

        case 0x80000:
            if (nDamage <= 0 || QueenList[nQueen].nHealth <= 0)
                return;
            QueenList[nQueen].nHealth -= nDamage;
            KexDifficulty_PainRoll(0);
            if (QueenList[nQueen].nHealth <= 0)
            {
                QueenList[nQueen].nHealth = 0;
                sprite[nSprite].xvel = sprite[nSprite].yvel
                    = sprite[nSprite].zvel = 0;
                KexQueenBeginAction(nQueen, kKexQueenDeath);
                nCreaturesLeft--;
            }
            return;

        case 0x90000:
        {
            int const nAction = QueenList[nQueen].nAction;
            int const nFrame = QueenList[nQueen].nFrame;
            if (nAction == kKexQueenInert
                || (nAction == kKexQueenDeath && nFrame == 61))
                return;
            seq_PlotSequence(a & 0xFFFF,
                SeqOffsets[kSeqQueen] + KexQueenActionSeq[nAction].a,
                nFrame, KexQueenActionSeq[nAction].b);
            return;
        }

        default:
            DebugOut("unknown msg %d for KEX Queen\n", a & kMessageMask);
            return;
    }
}

void FuncQueen(int a, int nDamage, int nRun)
{
    short nQueen = RunData[nRun].nVal;
    assert(nQueen >= 0 && nQueen < kMaxQueens);

    if (KexQueenSequenceExact())
    {
        FuncKexQueen(a, nDamage, nQueen);
        return;
    }

    short nSprite = QueenList[nQueen].nSprite;
    short nAction = QueenList[nQueen].nAction;
    short si = QueenList[nQueen].field_A;
    short nTarget = QueenList[nQueen].nTarget;

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {

        default:
        {
            DebugOut("unknown msg %d for Queen\n", nMessage);
            break;
        }

        case 0x20000:
        {
            if (si < 3) {
                Gravity(nSprite);
            }

            short nSeq = SeqOffsets[kSeqQueen] + ActionSeq[nAction].a;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, QueenList[nQueen].nFrame);

            seq_MoveSequence(nSprite, nSeq, QueenList[nQueen].nFrame);

            QueenList[nQueen].nFrame++;
            if (QueenList[nQueen].nFrame >= SeqSize[nSeq])
            {
                QueenList[nQueen].nFrame = 0;
                bVal = true;
            }

            short nFlag = FrameFlag[SeqBase[nSeq] + QueenList[nQueen].nFrame];

            if (nTarget > -1)
            {
                if (nAction < 7)
                {
                    if (!(sprite[nSprite].cstat & 0x101))
                    {
                        nTarget = -1;
                        QueenList[nQueen].nTarget = -1;
                        QueenList[nQueen].nAction = 0;
                    }
                }
            }
            switch (nAction)
            {
                case 0:
                {
                    if (nTarget < 0)
                    {
                        nTarget = FindPlayer(nSprite, 60);
                    }

                    if (nTarget >= 0)
                    {
                        QueenList[nQueen].nAction = QueenList[nQueen].field_A + 1;
                        QueenList[nQueen].nFrame  = 0;
                        QueenList[nQueen].nTarget = nTarget;
                        QueenList[nQueen].field_C = RandomSize(7);

                        if (g_kexMode && nTarget >= 0)
                            KexQueenStrafe(nSprite, nTarget);
                        else
                            SetQueenSpeed(nSprite, si);
                    }
                    break;
                }

                case 6:
                {
                    if (bVal)
                    {
                        BuildQueenEgg(nQueen, 1);
                        QueenList[nQueen].nAction = 3;
                        QueenList[nQueen].field_C = RandomSize(6) + 60;
                    }

                    break;
                }

                case 1:
                case 2:
                case 3:
                {
                    // AI_Queen's low-health stumpy state occasionally enters
                    // queen_chase_charge. The stock SEQ has no separate
                    // charge action, so keep the authored chase pose while
                    // reproducing the movement, hit, and knockback here.
                    if (g_kexMode && QueenList[nQueen].field_12 > 0)
                    {
                        int nMov = MoveCreatureWithCaution(nSprite);

                        if ((nMov & 0xC000) == 0xC000 && (nMov & 0x3FFF) == nTarget)
                        {
                            runlist_DamageEnemy(nTarget, nSprite, 40);
                            sprite[nTarget].xvel = sprite[nSprite].xvel * 2;
                            sprite[nTarget].yvel = sprite[nSprite].yvel * 2;
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                            QueenList[nQueen].field_12 = 0;
                        }
                        else if (--QueenList[nQueen].field_12 <= 0)
                        {
                            QueenList[nQueen].field_12 = 0;
                            if (g_kexMode && nTarget >= 0)
                                KexQueenStrafe(nSprite, nTarget);
                            else
                                SetQueenSpeed(nSprite, si);
                        }

                        break;
                    }

                    if (g_kexMode && nTarget >= 0
                        && KexQueenRemainingHealth(nQueen) <= KexHealthUnits(1200)
                        && RandomSize(3) == 0)
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        // KEX velocity is 16 world units/tick; Exhumed stores
                        // sprite velocity at 1/64th of that movement scale.
                        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * (16 * 6)) >> 8;
                        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * (16 * 6)) >> 8;
                        QueenList[nQueen].field_12 = 30;
                        QueenList[nQueen].nFrame = 0;
                        break;
                    }

                    QueenList[nQueen].field_C--;

                    if ((nQueen & 0x1F) == (totalmoves & 0x1F))
                    {
                        if (si < 2)
                        {
                            if (QueenList[nQueen].field_C <= 0)
                            {
                                QueenList[nQueen].nFrame = 0;
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                                QueenList[nQueen].nAction = si + 4;
                                QueenList[nQueen].field_C = RandomSize(6) + 30;
                                break;
                            }
                            else
                            {
                                if (QueenList[nQueen].field_10 < 5)
                                {
                                    QueenList[nQueen].field_10++;
                                }

                                // then to PLOTSPRITE
                            }
                        }
                        else
                        {
                            if (QueenList[nQueen].field_C <= 0)
                            {
                                if (nWaspCount < 100)
                                {
                                    QueenList[nQueen].nAction = 6;
                                    QueenList[nQueen].nFrame  = 0;
                                    break;
                                }
                                else
                                {
                                    QueenList[nQueen].field_C = 30000;
                                    // then to PLOTSPRITE
                                }
                            }
                        }

                        // loc_35B4B
                        PlotCourseToSprite(nSprite, nTarget);
                        if (g_kexMode && nTarget >= 0)
                            KexQueenStrafe(nSprite, nTarget);
                        else
                            SetQueenSpeed(nSprite, si);
                    }

                    int nMov = MoveCreatureWithCaution(nSprite);

                    switch (nMov & 0xC000)
                    {
                        case 0xC000:
                            if ((si == 2) && ((nMov & 0x3FFF) == nTarget))
                            {
                                runlist_DamageEnemy(nTarget, nSprite, 5);
                                if (g_kexMode)
                                {
                                    int const sound = KexSound_Get("rex-bite");
                                    if (sound >= 0)
                                        D3PlayFX(sound, nSprite);
                                }
                                break;
                            }
                            fallthrough__;
                        case 0x8000:
                            sprite[nSprite].ang += 256;
                            sprite[nSprite].ang &= kAngleMask;

                            if (g_kexMode && nTarget >= 0)
                                KexQueenStrafe(nSprite, nTarget);
                            else
                                SetQueenSpeed(nSprite, si);
                            break;
                    }

                    // loc_35BD2
                    if (nAction && nTarget != -1)
                    {
                        if (!(sprite[nTarget].cstat & 0x101))
                        {
                            QueenList[nQueen].nAction = 0;
                            QueenList[nQueen].nFrame  = 0;
                            QueenList[nQueen].field_C = 100;
                            QueenList[nQueen].nTarget = -1;

                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                        }
                    }

                    break;
                }

                case 4:
                case 5:
                {
                    if (bVal && QueenList[nQueen].field_10 <= 0)
                    {
                        QueenList[nQueen].nAction = 0;
                        QueenList[nQueen].field_C = 15;
                    }
                    else
                    {
                        if (nFlag & 0x80)
                        {
                            QueenList[nQueen].field_10--;

                            PlotCourseToSprite(nSprite, nTarget);

                            if (!si)
                            {
                                BuildBullet(nSprite, 12, 0, 0, -1, sprite[nSprite].ang, nTarget + 10000, 1);
                            }
                            else
                            {
                                if (g_kexMode && nAction == 5)
                                {
                                    int const sound = KexSound_Get("q_stumpy");
                                    if (sound >= 0)
                                        D3PlayFX(sound, nSprite);
                                }
                                BuildQueenEgg(nQueen, 0);
                            }
                        }
                    }

                    break;
                }

                case 7:
                {
                    if (bVal)
                    {
                        QueenList[nQueen].nAction = 0;
                        QueenList[nQueen].nFrame  = 0;
                    }

                    break;
                }

                case 8:
                case 9:
                {
                    if (bVal)
                    {
                        if (nAction == 9)
                        {
                            QueenList[nQueen].field_C--;
                            if (QueenList[nQueen].field_C <= 0)
                            {
                                sprite[nSprite].cstat = 0;

                                if (g_kexMode)
                                {
                                    static const int gibTiles[] = { 7841, 7842, 7843, 7844, 7845, 7846 };
                                    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
                                        KexTossCreatureChunk(nSprite, gibTiles[i]);

                                    int const sound = KexSound_Get("q_deth");
                                    if (sound >= 0)
                                        PlayLocalSound(sound, 0);
                                }
                                else
                                    for (int i = 0; i < 20; i++)
                                    {
                                        short nChunkSprite = BuildCreatureChunk(nSprite, seq_GetSeqPicnum(kSeqQueen, 57, 0)) & 0xFFFF;

                                        sprite[nChunkSprite].picnum = kTile3117 + (i % 3);
                                        sprite[nChunkSprite].xrepeat = 100;
                                        sprite[nChunkSprite].yrepeat = 100;
                                    }

                                if (!g_kexMode)
                                {
                                    short nChunkSprite = BuildCreatureChunk(nSprite, seq_GetSeqPicnum(kSeqQueen, 57, 0));

                                    sprite[nChunkSprite].picnum = kTile3126;
                                    sprite[nChunkSprite].yrepeat = 100;
                                    sprite[nChunkSprite].xrepeat = 100;
                                }

                                if (!g_kexMode)
                                    PlayFXAtXYZ(
                                        StaticSound[kSoundFishDies],
                                        sprite[nSprite].x,
                                        sprite[nSprite].y,
                                        sprite[nSprite].z,
                                        sprite[nSprite].sectnum);

                                BuildQueenHead(nQueen);

                                QueenList[nQueen].nAction++;
                            }
                        }
                        else
                            QueenList[nQueen].nAction++;
                    }

                    break;
                }

                case 10:
                {
                    sprite[nSprite].cstat &= 0xFEFE;
                    break;
                }
            }
            break;
        }

        case 0xA0000:
        {
            if (sprite[nRadialSpr].statnum != 121 && (sprite[nSprite].cstat & 0x101) != 0)
            {
                nDamage = runlist_CheckRadialDamage(nSprite);

                if (!nDamage) {
                    break;
                }
            }
            else
                break;

            fallthrough__;
        } // fall through to case 0x80000

        case 0x80000:
        {
            if (QueenList[nQueen].nHealth > 0)
            {
                QueenList[nQueen].nHealth -= nDamage;

                if (QueenList[nQueen].nHealth <= 0)
                {
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                    sprite[nSprite].zvel = 0;

                    QueenList[nQueen].field_A++;

                    switch (QueenList[nQueen].field_A)
                    {
                    case 1:
                        QueenList[nQueen].nHealth = g_kexMode ? KexQueenPhaseHealth() : 4000;
                        QueenList[nQueen].nAction = 7;

                        BuildAnim(-1, 36, 0, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z - 7680, sprite[nSprite].sectnum, sprite[nSprite].xrepeat, 4);
                        break;
                    case 2:
                        QueenList[nQueen].nHealth = g_kexMode ? KexQueenPhaseHealth() : 4000;
                        QueenList[nQueen].nAction = 7;

                        DestroyAllEggs();
                        break;
                    case 3:
                        QueenList[nQueen].nAction = 8;
                        QueenList[nQueen].nHealth = 0;
                        QueenList[nQueen].field_C = 5;

                        nCreaturesLeft--;
                        break;
                    }

                    QueenList[nQueen].nFrame = 0;
                }
                else
                {
                    if (g_kexMode)
                    {
                        // AI_Queen has painChance 0 and no pain animation.
                        KexDifficulty_PainRoll(0);
                    }
                    else if (si > 0 && !RandomSize(4))
                    {
                        QueenList[nQueen].nAction = 7;
                        QueenList[nQueen].nFrame  = 0;
                    }
                }
            }
            break;
        }

        case 0x90000:
        {
            seq_PlotSequence(a & 0xFFFF, SeqOffsets[kSeqQueen] + ActionSeq[nAction].a, QueenList[nQueen].nFrame, ActionSeq[nAction].b);
            break;
        }
    }
}

int KexQueen_SaveTestPrepare(void)
{
    if (!KexQueenSequenceExact()
        || (unsigned)QueenCount >= (unsigned)kMaxQueens
        || !KexQueenSpriteValid(QueenList[0].nSprite))
        return 0;

    if (!KexQueenSpriteValid(QueenHead.nSprite)
        || sprite[QueenHead.nSprite].statnum != 121)
        BuildQueenHead(0);
    if (!KexQueenSpriteValid(QueenHead.nSprite)
        || sprite[QueenHead.nSprite].statnum != 121)
        return 0;

    if (QueenHead.field_E == 0)
        BuildTail();
    if (QueenHead.field_E != kMaxTails)
        return 0;

    QueenHead.nAction = kKexQueenSnakeChase;
    QueenHead.field_2 = 0;
    QueenHead.field_C = 0;
    sprite[QueenHead.nSprite].cstat = 1;

    s_queenSaveBodyAction = QueenList[0].nAction;
    s_queenSaveBodyFrame = QueenList[0].nFrame;
    s_queenSaveBodyHealth = QueenList[0].nHealth;
    s_queenSaveHeadSprite = QueenHead.nSprite;
    s_queenSaveHeadAction = QueenHead.nAction;
    s_queenSaveHeadHealth = QueenHead.nHealth;
    for (int i = 0; i < kMaxTails; i++)
        s_queenSaveTail[i] = tailspr[i];
    return 1;
}

int KexQueen_SaveTestValidate(void)
{
    bool okay = KexQueenSequenceExact()
        && (unsigned)QueenCount < (unsigned)kMaxQueens
        && QueenList[0].nAction == s_queenSaveBodyAction
        && QueenList[0].nFrame == s_queenSaveBodyFrame
        && QueenList[0].nHealth == s_queenSaveBodyHealth
        && QueenHead.nSprite == s_queenSaveHeadSprite
        && QueenHead.nAction == s_queenSaveHeadAction
        && QueenHead.nHealth == s_queenSaveHeadHealth
        && QueenHead.field_E == kMaxTails
        && MoveQS[kKexTailSaveMarkerSlot] == kKexTailSaveMarker;
    for (int i = 0; i < kMaxTails; i++)
    {
        okay &= tailspr[i] == s_queenSaveTail[i]
            && KexQueenSpriteValid(tailspr[i])
            && sprite[tailspr[i]].statnum == 121
            && sprite[tailspr[i]].cstat == (i == kMaxTails - 1 ? 0x100 : 0)
            && MoveQA[i] == KexHealthUnits(200);
    }
    OSD_Printf("Kex Queen save test: %s (stable record %d tails + %d packed, body %d/%d, head %d/%d, chain %d)\n",
               okay ? "PASS" : "FAIL", kClassicTails,
               kMaxTails - kClassicTails,
               QueenList[0].nAction, QueenList[0].nHealth,
               QueenHead.nAction, QueenHead.nHealth, QueenHead.field_E);
    return okay ? 1 : 0;
}

static bool KexQueenTestBodySaveMigration(short nQueen)
{
    Queen const saved = QueenList[nQueen];
    short const nSprite = saved.nSprite;
    int const savedClip = sprite[nSprite].clipdist;
    int const savedXRepeat = sprite[nSprite].xrepeat;
    int const savedYRepeat = sprite[nSprite].yrepeat;

    QueenList[nQueen].nAction = 4; // legacy tail-missile action
    QueenList[nQueen].nFrame = 99;
    QueenList[nQueen].nHealth = KexHealthUnits(600);
    QueenList[nQueen].field_A = 1;
    sprite[nSprite].clipdist = 100;
    sprite[nSprite].xrepeat = sprite[nSprite].yrepeat = 80;
    KexQueenMigrateLegacyBody(nQueen);

    bool const okay
        = QueenList[nQueen].nAction == kKexQueenTailMissile
        && QueenList[nQueen].nFrame == 0
        && QueenList[nQueen].nHealth == KexHealthUnits(1800)
        && QueenList[nQueen].field_A == 0
        && sprite[nSprite].clipdist == 96
        && sprite[nSprite].xrepeat == 60
        && sprite[nSprite].yrepeat == 60
        && KexQueenLegacyBodyAction(3) == kKexQueenFlap
        && KexQueenLegacyBodyAction(6) == kKexQueenHurl
        && KexQueenLegacyBodyAction(9) == kKexQueenDeath
        && KexQueenLegacySnakeAction(8) == kKexQueenSnakeChase
        && KexQueenLegacySnakeAction(5) == kKexQueenSnakeExplode;

    QueenList[nQueen] = saved;
    sprite[nSprite].clipdist = savedClip;
    sprite[nSprite].xrepeat = savedXRepeat;
    sprite[nSprite].yrepeat = savedYRepeat;
    return okay;
}

static bool KexQueenTestEggSaveMigration(short nEgg)
{
    Egg const saved = QueenEgg[nEgg];
    short const nSprite = saved.nSprite;
    int const savedClip = sprite[nSprite].clipdist;
    int const savedXRepeat = sprite[nSprite].xrepeat;
    int const savedYRepeat = sprite[nSprite].yrepeat;
    int const savedCstat = sprite[nSprite].cstat;
    int const savedXVel = sprite[nSprite].xvel;
    int const savedYVel = sprite[nSprite].yvel;
    int const savedZVel = sprite[nSprite].zvel;

    QueenEgg[nEgg].nHealth = 200;
    QueenEgg[nEgg].field_2 = 17;
    QueenEgg[nEgg].nAction = 4;
    QueenEgg[nEgg].field_C = 200;
    QueenEgg[nEgg].field_E = 1;
    sprite[nSprite].clipdist = 50;
    sprite[nSprite].xrepeat = sprite[nSprite].yrepeat = 60;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = -2000;
    KexQueenNormalizeLoadedEgg(nEgg, true);

    bool const okay = QueenEgg[nEgg].nHealth == 1
        && QueenEgg[nEgg].field_2 == 0
        && QueenEgg[nEgg].nAction == 0
        && QueenEgg[nEgg].field_C == 60
        && QueenEgg[nEgg].field_E == 0
        && sprite[nSprite].clipdist == 54
        && sprite[nSprite].xrepeat == 40
        && sprite[nSprite].yrepeat == 40
        && sprite[nSprite].cstat == 0
        && (sprite[nSprite].xvel || sprite[nSprite].yvel)
        && sprite[nSprite].zvel == 0;

    QueenEgg[nEgg] = saved;
    sprite[nSprite].clipdist = savedClip;
    sprite[nSprite].xrepeat = savedXRepeat;
    sprite[nSprite].yrepeat = savedYRepeat;
    sprite[nSprite].cstat = savedCstat;
    sprite[nSprite].xvel = savedXVel;
    sprite[nSprite].yvel = savedYVel;
    sprite[nSprite].zvel = savedZVel;
    return okay;
}

static bool KexQueenTestTailSaveMigration(void)
{
    short saveMoveQS[ARRAY_SIZE(MoveQS)];
    KexQueenPackTailSave(saveMoveQS);
    bool okay = saveMoveQS[kKexTailSaveMarkerSlot] == kKexTailSaveMarker;
    for (int i = kClassicTails; i < kMaxTails; i++)
        okay &= saveMoveQS[kKexTailSaveFirstSlot + i - kClassicTails]
            == tailspr[i];

    short const savedAction = QueenHead.nAction;
    short const savedFrame = QueenHead.field_2;
    QueenHead.nAction = 8; // legacy controller action; invalid in exact table
    QueenHead.field_2 = 99;
    KexQueenSanitizeSnake();
    okay &= QueenHead.nAction == kKexQueenSnakeChase
        && QueenHead.field_2 == 0;
    QueenHead.nAction = savedAction;
    QueenHead.field_2 = savedFrame;
    return okay;
}

static void KexQueenTestFinish(bool bOkay)
{
    if ((unsigned)s_queenTestPlayer < (unsigned)nTotalPlayers)
        PlayerList[s_queenTestPlayer].invincibility
            = s_queenTestInvincibility;

    if ((unsigned)s_queenTestQueen < (unsigned)kMaxQueens)
    {
        short const nSprite = QueenList[s_queenTestQueen].nSprite;
        if ((unsigned)nSprite < (unsigned)kMaxSprites
            && sprite[nSprite].sectnum >= 0)
        {
            if (s_queenTestCreatureCounted)
                nCreaturesLeft--;
            sprite[nSprite].xvel = sprite[nSprite].yvel
                = sprite[nSprite].zvel = 0;
            sprite[nSprite].cstat = 0x8000;
            if (sprite[nSprite].statnum != 0)
                changespritestat(nSprite, 0);
            QueenList[s_queenTestQueen].nAction = kKexQueenInert;
            QueenList[s_queenTestQueen].nFrame = 0;
        }
    }

    if (!bOkay)
        OSD_Printf("Kex Queen test: FAIL (phase %d tick %d)\n",
                   s_queenTestPhase, s_queenTestTicks);
    s_queenTest = -2;
    s_queenTestQueen = -1;
    s_queenTestCreatureCounted = 0;
}

void KexQueen_TestTick()
{
    if (s_queenTest == -1)
        s_queenTest = g_kexMode && Bgetenv("VB_KEX_QUEEN_TEST") ? 0 : -2;
    if (s_queenTest < 0)
        return;

    if (s_queenTest == 0)
    {
        short const nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayerSprite >= (unsigned)kMaxSprites
            || sprite[nPlayerSprite].sectnum < 0)
            return;

        short const nSector = sprite[nPlayerSprite].sectnum;
        // The retail campaign has one Queen controller.  KILARENA already
        // owns it, while a purpose-built test map leaves it free.  Reuse the
        // authored controller in the former case so this self-test exercises
        // the real boss map instead of trying to allocate a second Queen.
        bool const reuseAuthored = QueenCount == 0
            && KexQueenSpriteValid(QueenList[0].nSprite)
            && sprite[QueenList[0].nSprite].statnum == 121;
        if (reuseAuthored)
        {
            s_queenTestQueen = 0;
            short const nSprite = QueenList[0].nSprite;
            if (sprite[nSprite].sectnum != nSector)
                mychangespritesect(nSprite, nSector);
            sprite[nSprite].x = sprite[nPlayerSprite].x + 700;
            sprite[nSprite].y = sprite[nPlayerSprite].y;
            sprite[nSprite].z = sector[nSector].floorz;
            sprite[nSprite].ang = sprite[nPlayerSprite].ang;
            sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
            sprite[nSprite].cstat = 0x101;
            sprite[nSprite].clipdist = 96;
            sprite[nSprite].xrepeat = sprite[nSprite].yrepeat = 60;
            QueenList[0].nAction = kKexQueenIdle;
            QueenList[0].nHealth = KexHealthUnits(3600);
            QueenList[0].nFrame = 0;
            QueenList[0].nTarget = -1;
            QueenList[0].field_A = 0;
            QueenList[0].field_10 = 5;
            QueenList[0].field_C = 0;
            QueenList[0].field_12 = 0;
            QueenChan[0] = 0;
        }
        else
        {
            s_queenTestQueen = (short)BuildQueen(
                -1, sprite[nPlayerSprite].x + 700, sprite[nPlayerSprite].y,
                sector[nSector].floorz, nSector, sprite[nPlayerSprite].ang, 0);
        }
        if (s_queenTestQueen < 0)
        {
            OSD_Printf("Kex Queen test: FAIL (no usable controller)\n");
            s_queenTest = -2;
            return;
        }

        short const nSprite = QueenList[s_queenTestQueen].nSprite;
        QueenList[s_queenTestQueen].nTarget = nPlayerSprite;
        PlotCourseToSprite(nSprite, nPlayerSprite);
        KexQueenBeginAction(s_queenTestQueen, kKexQueenFlap);
        s_queenTestPlayer = nLocalPlayer;
        s_queenTestInvincibility
            = PlayerList[nLocalPlayer].invincibility;
        // The full body-to-snake probe deliberately receives the body flap,
        // charge and head-melee hits. Shield this env-only lifecycle through
        // its roughly 270 native ticks, then restore the original timer in
        // KexQueenTestFinish().
        if (PlayerList[nLocalPlayer].invincibility < 900)
            PlayerList[nLocalPlayer].invincibility = 900;

        int const base = SeqOffsets[kSeqQueen];
        bool flags = (FrameFlag[SeqBase[base + 45] + 1] & 0x80)
            && (FrameFlag[SeqBase[base + 54] + 12] & 0x04)
            && (FrameFlag[SeqBase[base + 54] + 16] & 0x80)
            && (FrameFlag[SeqBase[base + 63] + 7] & 0x84) == 0x84
            && (FrameFlag[SeqBase[base + 63] + 8] & 0x04)
            && (FrameFlag[SeqBase[base + 92]] & 0x80);
        for (int i = 0; i < 21; i++)
            flags &= (FrameFlag[SeqBase[base + 101] + i * 4] & 0x80) != 0;

        // The controller is now allocated, whether it was spawned above or
        // borrowed from KILARENA.  A second request must fail transactionally:
        // no sprite allocation and, crucially, no negative free-slot count.
        short const freeBeforeOverflow = QueenCount;
        bool const overflowRejected = BuildQueen(
            -1, sprite[nPlayerSprite].x, sprite[nPlayerSprite].y,
            sector[nSector].floorz, nSector, sprite[nPlayerSprite].ang, 0) == -1
            && QueenCount == freeBeforeOverflow;
        s_queenTestOkay = KexQueenSequenceExact() && flags
            && overflowRejected
            && QueenList[s_queenTestQueen].nHealth == KexHealthUnits(3600)
            && sprite[nSprite].clipdist == 96
            && sprite[nSprite].xrepeat == 60
            && sprite[nSprite].yrepeat == 60;
        s_queenTestSaveCompat
            = KexQueenTestBodySaveMigration(s_queenTestQueen);
        s_queenTestOkay &= s_queenTestSaveCompat;
        s_queenTestCreatureCounted = 1;
        s_queenTestBodyMelee = 0;
        s_queenTestChargeDamage = 0;
        s_queenTestChargeKnockback = 0;
        s_queenTestSnakeMelee = 0;
        s_queenTestProjectile = 0;
        s_queenTestPodTicks = 0;
        s_queenTestPodHatches = 0;
        s_queenTestPodGeometry = 0;
        s_queenTestPainGibs = 0;
        s_queenTestBodyGibs = 0;
        s_queenTestTailDeaths = 0;
        s_queenTestTailLerp = 0;
        s_queenTestSnakeExplosions = 0;
        s_queenTestSnakeTriggers = 0;
        s_queenTestPhase = 1;
        s_queenTestTicks = 0;
        s_queenTest = 1;
        OSD_Printf("Kex Queen test: actor %d %s; exact lifecycle probe started\n",
                   nSprite, reuseAuthored ? "reused" : "spawned");
        return;
    }

    if ((unsigned)s_queenTestQueen >= (unsigned)kMaxQueens)
    {
        KexQueenTestFinish(false);
        return;
    }
    short const nBody = QueenList[s_queenTestQueen].nSprite;
    if ((unsigned)nBody >= (unsigned)kMaxSprites
        || sprite[nBody].sectnum < 0)
    {
        KexQueenTestFinish(false);
        return;
    }

    s_queenTestTicks++;
    if (s_queenTestPhase == 1 && s_queenTestTicks == 11)
    {
        s_queenTestOkay &= s_queenTestBodyMelee == 50
            && QueenList[s_queenTestQueen].nAction == kKexQueenFlap
            && QueenList[s_queenTestQueen].nFrame == 0;
        short const nPlayer = PlayerList[s_queenTestPlayer].nSprite;
        sprite[nBody].x = sprite[nPlayer].x + 500;
        sprite[nBody].y = sprite[nPlayer].y;
        PlotCourseToSprite(nBody, nPlayer);
        KexQueenBeginAction(s_queenTestQueen, kKexQueenCharge);
        KexQueenChargeTick(s_queenTestQueen, nBody, nPlayer);
        s_queenTestOkay &= s_queenTestChargeDamage == 40
            && s_queenTestChargeKnockback
            && QueenList[s_queenTestQueen].nAction == kKexQueenAfterCharge;
        sprite[nPlayer].xvel = sprite[nPlayer].yvel = 0;
        sprite[nBody].x = sprite[nPlayer].x + 700;
        sprite[nBody].y = sprite[nPlayer].y;
        KexQueenBeginAction(s_queenTestQueen, kKexQueenHurl);
        s_queenTestPhase = 2; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 2 && s_queenTestTicks == 17)
    {
        s_queenTestOkay &= s_queenTestEgg >= 0
            && s_queenTestPodGeometry;
        s_queenTestSaveCompat &= s_queenTestEgg >= 0
            && KexQueenTestEggSaveMigration(s_queenTestEgg);
        s_queenTestOkay &= s_queenTestSaveCompat;
        KexQueenBeginAction(s_queenTestQueen, kKexQueenInert);
        s_queenTestPhase = 3; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 3 && s_queenTestPodHatches)
    {
        s_queenTestOkay &= s_queenTestPodHatches == 1
            && s_queenTestPodTicks == 60;
        QueenList[s_queenTestQueen].nTarget
            = PlayerList[s_queenTestPlayer].nSprite;
        KexQueenBeginAction(s_queenTestQueen, kKexQueenTailMissile);
        s_queenTestPhase = 4; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 3 && s_queenTestTicks > 65)
    {
        KexQueenTestFinish(false);
        return;
    }
    else if (s_queenTestPhase == 4 && s_queenTestTicks == 8)
    {
        s_queenTestOkay &= s_queenTestProjectile == 1;
        QueenList[s_queenTestQueen].nHealth = KexHealthUnits(2000);
        KexQueenBeginAction(s_queenTestQueen, kKexQueenPain);
        s_queenTestPhase = 5; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 5 && s_queenTestTicks == 13)
    {
        s_queenTestOkay &= s_queenTestPainGibs == 5
            && QueenList[s_queenTestQueen].nAction == kKexQueenChase
            && QueenList[s_queenTestQueen].nFrame == 0;
        QueenList[s_queenTestQueen].nHealth = 0;
        KexQueenBeginAction(s_queenTestQueen, kKexQueenDeath);
        nCreaturesLeft--;
        s_queenTestCreatureCounted = 0;
        s_queenTestPhase = 6; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 6 && s_queenTestTicks == 62)
    {
        s_queenTestOkay &= s_queenTestBodyGibs == 6
            && QueenList[s_queenTestQueen].nAction == kKexQueenInert
            && (unsigned)QueenHead.nSprite < (unsigned)kMaxSprites;
        s_queenTestPhase = 7; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 7 && QueenHead.field_E == 10)
    {
        short const nHead = QueenHead.nSprite;
        bool tailsOkay = sprite[nHead].clipdist == 48
            && sprite[nHead].xrepeat == 80
            && sprite[nHead].cstat == 1
            && QueenHead.nHealth == KexHealthUnits(200);
        for (int i = 0; i < 10; i++)
        {
            tailsOkay &= (unsigned)tailspr[i] < (unsigned)kMaxSprites
                && sprite[tailspr[i]].clipdist == 96
                && sprite[tailspr[i]].xrepeat == 80
                && MoveQA[i] == KexHealthUnits(200)
                && sprite[tailspr[i]].cstat == (i == 9 ? 0x100 : 0);
            sprite[tailspr[i]].x = sprite[nHead].x;
            sprite[tailspr[i]].y = sprite[nHead].y;
            sprite[tailspr[i]].z = sprite[nHead].z;
        }
        int const oldX = sprite[nHead].x;
        sprite[nHead].x += 500;
        KexQueenUpdateTailJoints();
        s_queenTestTailLerp = sprite[tailspr[0]].x == oldX + 100
            && sprite[tailspr[1]].x == oldX + 20;
        s_queenTestSaveCompat &= KexQueenTestTailSaveMigration();
        s_queenTestOkay &= tailsOkay && s_queenTestTailLerp
            && s_queenTestSaveCompat;
        s_queenTestPhase = 8; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 7 && s_queenTestTicks > 3)
    {
        KexQueenTestFinish(false);
        return;
    }
    else if (s_queenTestPhase == 8)
    {
        if (QueenHead.field_E > 0)
            FuncKexQueenHead(0x80000, KexHealthUnits(200),
                             QueenHead.field_E);
        if (QueenHead.field_E == 0)
        {
            short const nHead = QueenHead.nSprite;
            s_queenTestOkay &= s_queenTestTailDeaths == 10
                && (sprite[nHead].cstat & 0x101) == 0x101;
            short const nPlayer = PlayerList[s_queenTestPlayer].nSprite;
            if (sprite[nHead].sectnum != sprite[nPlayer].sectnum)
                mychangespritesect(nHead, sprite[nPlayer].sectnum);
            sprite[nHead].x = sprite[nPlayer].x + 500;
            sprite[nHead].y = sprite[nPlayer].y;
            sprite[nHead].z = sector[sprite[nHead].sectnum].floorz;
            sprite[nHead].ang = sprite[nPlayer].ang;
            sprite[nHead].xvel = sprite[nHead].yvel = sprite[nHead].zvel = 0;
            QueenHead.nTarget = nPlayer;
            QueenHead.nAction = kKexQueenSnakeMelee;
            QueenHead.field_2 = 0;
            s_queenTestPhase = 9; s_queenTestTicks = 0;
        }
    }
    else if (s_queenTestPhase == 9 && s_queenTestTicks == 1)
    {
        s_queenTestOkay &= s_queenTestSnakeMelee == 30
            && QueenHead.nAction == kKexQueenSnakeChase;
        FuncKexQueenHead(0x80000, KexHealthUnits(200), 0);
        s_queenTestPhase = 10; s_queenTestTicks = 0;
    }
    else if (s_queenTestPhase == 10 && QueenHead.nSprite < 0)
    {
        s_queenTestOkay &= s_queenTestSnakeExplosions == 21
            && s_queenTestSnakeTriggers == 1
            && s_queenTestTicks == 85;
        OSD_Printf("Kex Queen test: %s (seq 1/10/10/10/10/11/30/16/13/62 + snake 1/5/1/85, pool %d, melee/charge %d/%d + knockback %d, pod %d ticks, projectile %d, pain/body gibs %d/%d, tails %d x 200 + lerp %d, save 7+3/migrate %d, snake melee %d, death %d explosions/%d ticks/%d trigger)\n",
                   s_queenTestOkay ? "PASS" : "FAIL",
                   QueenCount == 0,
                   s_queenTestBodyMelee, s_queenTestChargeDamage,
                   s_queenTestChargeKnockback, s_queenTestPodTicks,
                   s_queenTestProjectile, s_queenTestPainGibs,
                   s_queenTestBodyGibs, s_queenTestTailDeaths,
                   s_queenTestTailLerp, s_queenTestSaveCompat,
                   s_queenTestSnakeMelee,
                   s_queenTestSnakeExplosions, s_queenTestTicks,
                   s_queenTestSnakeTriggers);
        KexQueenTestFinish(s_queenTestOkay != 0);
    }
    else if (s_queenTestPhase == 10 && s_queenTestTicks > 86)
    {
        KexQueenTestFinish(false);
    }
}

class QueenLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void QueenLoadSave::Load()
{
    Read(&QueenCount, sizeof(QueenCount));
    Read(&nQHead, sizeof(nQHead));
    Read(&nEggsFree, sizeof(nEggsFree));
    Read(&nHeadVel, sizeof(nHeadVel));
    Read(&nVelShift, sizeof(nVelShift));
    // This field has contained seven shorts since the original save version.
    // The exact controller's extra three ids live in marked spare MoveQS
    // slots, so old classic and old approximate-KEX saves stay aligned.
    Read(tailspr, sizeof(short) * kClassicTails);
    for (int i = kClassicTails; i < kMaxTails; i++)
        tailspr[i] = -1;
    Read(nEggFree, sizeof(nEggFree));
    Read(QueenChan, sizeof(QueenChan));
    Read(QueenEgg, sizeof(QueenEgg));
    Read(QueenList, sizeof(QueenList));
    Read(&QueenHead, sizeof(QueenHead));
    Read(MoveQX, sizeof(MoveQX));
    Read(MoveQY, sizeof(MoveQY));
    Read(MoveQZ, sizeof(MoveQZ));
    Read(MoveQS, sizeof(MoveQS));
    Read(MoveQA, sizeof(MoveQA));

    if (KexQueenSequenceExact())
    {
        bool const exactSave = KexQueenRestoreTailSave();
        int const migrated = KexQueenNormalizeLoadedState(exactSave);
        if (migrated)
            OSD_Printf("Kex Queen save: migrated %d legacy controller record%s\n",
                       migrated, migrated == 1 ? "" : "s");
    }
}

void QueenLoadSave::Save()
{
    Write(&QueenCount, sizeof(QueenCount));
    Write(&nQHead, sizeof(nQHead));
    Write(&nEggsFree, sizeof(nEggsFree));
    Write(&nHeadVel,  sizeof(nHeadVel));
    Write(&nVelShift, sizeof(nVelShift));
    Write(tailspr, sizeof(short) * kClassicTails);
    Write(nEggFree,  sizeof(nEggFree));
    Write(QueenChan, sizeof(QueenChan));
    Write(QueenEgg, sizeof(QueenEgg));
    Write(QueenList,  sizeof(QueenList));
    Write(&QueenHead, sizeof(QueenHead));
    Write(MoveQX, sizeof(MoveQX));
    Write(MoveQY, sizeof(MoveQY));
    Write(MoveQZ, sizeof(MoveQZ));
    short saveMoveQS[ARRAY_SIZE(MoveQS)];
    if (KexQueenSequenceExact())
        KexQueenPackTailSave(saveMoveQS);
    else
        Bmemcpy(saveMoveQS, MoveQS, sizeof(MoveQS));
    Write(saveMoveQS, sizeof(saveMoveQS));
    Write(MoveQA, sizeof(MoveQA));
}

static QueenLoadSave* myLoadSave;

void QueenLoadSaveConstruct()
{
    myLoadSave = new QueenLoadSave();
}
