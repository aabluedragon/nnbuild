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

#include "roach.h"
#include "exhumed.h"
#include "engine.h"
#include "runlist.h"
#include "typedefs.h"
#include "sequence.h"
#include "move.h"
#include "random.h"
#include "trigdat.h"
#include "bullet.h"
#include "items.h"
#include "save.h"
#include "player.h"
#include "sound.h"
#include "anims.h"
#include "kexbreak.h" // VibeBuild32: KEX sentry sounds
#include "kexinv.h"
#include <assert.h>

#define kMaxRoach   100

static actionSeq ActionSeq[] = {
    {24, 0},
    {0,  0},
    {0,  0},
    {16, 0},
    {8,  0},
    {32, 1},
    {42, 1}
};

// Generated KEX ROACH.SEQ action groups: idle, chase, pain and attack retain
// the retail Sentry's 1/8/12/10-frame cadence. Stock/demo data continues to
// use ActionSeq above.
static actionSeq SentryActionSeq[] = {
    {0,  0},
    {0,  0},
    {9,  0},
    {27, 0},
    {18, 0},
    {35, 1},
    {35, 1}
};

struct Roach
{
    short nHealth;
    short field_2;
    short nAction;
    short nSprite;
    short nTarget;
    short field_A;
    short field_C;
    short field_E;
};

int16_t RoachCount = -1;

Roach RoachList[kMaxRoach];

static int s_sentryAudit = -1;
static int s_sentryAuditSeen;
static int s_sentryTest = -1;
static short s_sentryTestRoach = -1;
static short s_sentryTestPlayer = -1;
static short s_sentryTestInvincibility;
static short s_sentryTestFireFrame = -1;
static int s_sentryTestProjectileOkay;


/* Kilmaat Sentry */

void InitRoachs()
{
    RoachCount = kMaxRoach;
    s_sentryAudit = -1;
    s_sentryAuditSeen = 0;
    s_sentryTest = -1;
    s_sentryTestRoach = -1;
    s_sentryTestPlayer = -1;
    s_sentryTestFireFrame = -1;
    s_sentryTestProjectileOkay = 0;
}

static bool KexSentrySequenceExact()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqRoach))
        return false;
    int const base = SeqOffsets[kSeqRoach];
    // Stock ROACH.SEQ has an eight-frame walk at slot zero and a five-frame
    // dormant group at 27. The generated Sentry file has one and ten.
    return SeqSize[base] == 1 && SeqSize[base + 27] == 10;
}

static actionSeq const &RoachActionSequence(int nAction)
{
    return KexSentrySequenceExact() ? SentryActionSeq[nAction]
                                    : ActionSeq[nAction];
}

static void KexSentryAudit(short nRoach)
{
    if (s_sentryAudit < 0)
        s_sentryAudit = g_kexMode && Bgetenv("VB_KEX_SENTRY_AUDIT") ? 1 : 0;
    if (!s_sentryAudit || s_sentryAuditSeen)
        return;
    s_sentryAuditSeen = 1;
    short const nSprite = RoachList[nRoach].nSprite;
    OSD_Printf("Kex Sentry: sprite %d hp %d clip %d repeat %d speed 8 seq %d exact %d\n",
               nSprite, KexHealthPoints(RoachList[nRoach].nHealth), (int)sprite[nSprite].clipdist,
               (int)sprite[nSprite].xrepeat, kSeqRoach,
               KexSentrySequenceExact() ? 1 : 0);
}

// TODO - make nType a bool?
int BuildRoach(int nType, int nSprite, int x, int y, int z, short nSector, int angle)
{
    if (RoachCount <= 0)
        return -1;
    short const nRoach = --RoachCount;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 105);
    }
    else
    {
        changespritestat(nSprite, 105);
        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sector[sprite[nSprite].sectnum].floorz;
        angle = sprite[nSprite].ang;
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].shade = -12;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].clipdist = g_kexMode ? 72 : 60;
    sprite[nSprite].ang = angle;
    sprite[nSprite].xrepeat = g_kexMode ? 60 : 40;
    sprite[nSprite].yrepeat = g_kexMode ? 60 : 40;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

    //	GrabTimeSlot(3);

    if (nType)
    {
        RoachList[nRoach].nAction = 0;
    }
    else
    {
        RoachList[nRoach].nAction = 1;
    }

    RoachList[nRoach].nSprite = nSprite;
    RoachList[nRoach].field_2 = 0;
    RoachList[nRoach].field_C = 0;
    RoachList[nRoach].nTarget = -1;
    // AI_Sentry is the KEX skin on this Build actor.  Its remaster definition
    // is 180 health; the classic Kilmaat roach keeps its original 600.
    RoachList[nRoach].nHealth = g_kexMode ? KexHealthUnits(180) : 600;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nRoach, kRunRoach);
    RoachList[nRoach].field_A = runlist_AddRunRec(NewRun, nRoach, kRunRoach);

    nCreaturesLeft++;
    KexSentryAudit(nRoach);

    return nRoach;
}

void GoRoach(short nSprite)
{
    if (g_kexMode)
    {
        // AI_Sentry moveSpeed 8, with converted XY at six Build units per
        // authored unit. Classic GoRoach is exactly twice this speed.
        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * (8 * 6)) >> 8;
        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * (8 * 6)) >> 8;
    }
    else
    {
        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) >> 1) - (Cos(sprite[nSprite].ang) >> 3);
        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) >> 1) - (Sin(sprite[nSprite].ang) >> 3);
    }
}

static void StartRoachRangeAttack(short nRoach, short nSprite, short nTarget)
{
    RoachList[nRoach].field_E = g_kexMode ? 1 : RandomSize(2) + 1;
    RoachList[nRoach].nAction = 3;
    RoachList[nRoach].field_2 = 0;

    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].ang = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                     sprite[nTarget].y - sprite[nSprite].y);

    if (g_kexMode)
        RoachList[nRoach].field_C = KexDifficulty_AICooldown();
}

static void KexSentrySightSound(short nSprite)
{
    if (!g_kexMode)
        return;
    int const sound = KexSound_Get("roch_icu");
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexSentryDelete(short nRoach, short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(RoachList[nRoach].field_A);
    sprite[nSprite].cstat = 0x8000;
    mydeletesprite(nSprite);
}

static void KexSentryDeath(short nRoach, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 64,
        sprite[nSprite].xrepeat, nKiller);

    // sentry_death.tsc tosses nine gibs in this order, then A_Destroy()s the
    // actor.  The classic controller otherwise leaves a roach corpse behind.
    static const int gibTiles[] = { 7800, 7800, 7801, 7802, 7803, 7803, 7804, 7804, 7804 };

    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);

    int const sound = KexSound_Get(RandomBit() ? "roch_die" : "roch_die2");
    if (sound >= 0)
        PlayLocalSound(sound, 0);

    KexSentryDelete(nRoach, nSprite);
}

static void KexSentryTestFinish(bool okay, short nSprite)
{
    if ((unsigned)nSprite < (unsigned)kMaxSprites && sprite[nSprite].statnum == 105)
    {
        KexSentryDelete(s_sentryTestRoach, nSprite);
        nCreaturesLeft--;
    }
    if ((unsigned)s_sentryTestPlayer < (unsigned)nTotalPlayers)
        PlayerList[s_sentryTestPlayer].invincibility = s_sentryTestInvincibility;
    if (!okay)
        OSD_Printf("Kex Sentry test: FAIL (test actor/state disappeared)\n");
    s_sentryTest = -2;
    s_sentryTestRoach = -1;
}

void KexSentry_TestTick()
{
    if (s_sentryTest == -1)
        s_sentryTest = g_kexMode && Bgetenv("VB_KEX_SENTRY_TEST") ? 0 : -2;
    if (s_sentryTest < 0)
        return;

    if (s_sentryTest == 0)
    {
        short const nPlayer = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayer >= (unsigned)kMaxSprites
            || sprite[nPlayer].sectnum < 0 || !(sprite[nPlayer].cstat & 0x101))
            return;
        short const nSector = sprite[nPlayer].sectnum;
        int const nFloor = getflorzofslope(nSector, sprite[nPlayer].x, sprite[nPlayer].y);
        s_sentryTestRoach = (short)BuildRoach(0, -1, sprite[nPlayer].x,
                                              sprite[nPlayer].y, nFloor,
                                              nSector, sprite[nPlayer].ang);
        if (s_sentryTestRoach < 0)
        {
            OSD_Printf("Kex Sentry test: FAIL (no free controller)\n");
            s_sentryTest = -2;
            return;
        }
        RoachList[s_sentryTestRoach].nTarget = nPlayer;
        RoachList[s_sentryTestRoach].nAction = 3;
        RoachList[s_sentryTestRoach].field_2 = 0;
        RoachList[s_sentryTestRoach].field_E = 1;
        s_sentryTestPlayer = nLocalPlayer;
        s_sentryTestInvincibility = PlayerList[nLocalPlayer].invincibility;
        if (PlayerList[nLocalPlayer].invincibility < 100)
            PlayerList[nLocalPlayer].invincibility = 100;
        s_sentryTestFireFrame = -1;
        s_sentryTestProjectileOkay = 0;
        s_sentryTest = 1;
        OSD_Printf("Kex Sentry test: actor %d spawned; exact attack probe started\n",
                   RoachList[s_sentryTestRoach].nSprite);
        return;
    }

    if (s_sentryTestRoach < RoachCount || s_sentryTestRoach >= kMaxRoach)
    {
        KexSentryTestFinish(false, -1);
        return;
    }
    short const nSprite = RoachList[s_sentryTestRoach].nSprite;
    if ((unsigned)nSprite >= (unsigned)kMaxSprites || sprite[nSprite].statnum != 105)
    {
        KexSentryTestFinish(false, nSprite);
        return;
    }

    if (s_sentryTest == 10)
    {
        bool const attackOkay = RoachList[s_sentryTestRoach].nAction == 2
                              && RoachList[s_sentryTestRoach].field_2 == 0
                              && s_sentryTestFireFrame == 6;
        if (!attackOkay)
        {
            OSD_Printf("Kex Sentry test: FAIL (attack action/frame/fire %d/%d/%d)\n",
                       RoachList[s_sentryTestRoach].nAction,
                       RoachList[s_sentryTestRoach].field_2,
                       s_sentryTestFireFrame);
            KexSentryTestFinish(true, nSprite);
            return;
        }
        RoachList[s_sentryTestRoach].nAction = 4;
        RoachList[s_sentryTestRoach].field_2 = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_sentryTest = 11;
        return;
    }

    if (s_sentryTest == 21)
    {
        int const base = SeqOffsets[kSeqRoach];
        int const idleFrames = SeqSize[base + SentryActionSeq[0].a];
        int const chaseFrames = SeqSize[base + SentryActionSeq[2].a];
        int const painFrames = SeqSize[base + SentryActionSeq[4].a];
        int const attackFrames = SeqSize[base + SentryActionSeq[3].a];
        int const attackSeq = base + SentryActionSeq[3].a;
        int const speed = ksqrt((uint32_t)(sprite[nSprite].xvel * sprite[nSprite].xvel
                                         + sprite[nSprite].yvel * sprite[nSprite].yvel));
        bool const okay = KexSentrySequenceExact()
            && RoachList[s_sentryTestRoach].nAction == 2
            && RoachList[s_sentryTestRoach].field_2 == 0
            && RoachList[s_sentryTestRoach].nHealth == KexHealthUnits(180)
            && sprite[nSprite].clipdist == 72
            && sprite[nSprite].xrepeat == 60 && sprite[nSprite].yrepeat == 60
            && idleFrames == 1 && chaseFrames == 8
            && painFrames == 12 && attackFrames == 10
            && (FrameFlag[SeqBase[attackSeq] + 6] & 4)
            && s_sentryTestFireFrame == 6 && s_sentryTestProjectileOkay
            && speed >= 3060 && speed <= 3080;
        OSD_Printf("Kex Sentry test: %s (hp %d, clip %d, repeat %d, speed %d, seq %d/%d/%d/%d, fire %d, projectile %d, action/frame %d/%d)\n",
                   okay ? "PASS" : "FAIL", KexHealthPoints(RoachList[s_sentryTestRoach].nHealth),
                   (int)sprite[nSprite].clipdist, (int)sprite[nSprite].xrepeat,
                   speed, idleFrames, chaseFrames, painFrames, attackFrames,
                   s_sentryTestFireFrame, s_sentryTestProjectileOkay,
                   RoachList[s_sentryTestRoach].nAction,
                   RoachList[s_sentryTestRoach].field_2);
        KexSentryTestFinish(true, nSprite);
        return;
    }

    s_sentryTest++;
}

void FuncRoach(int a, int nDamage, int nRun)
{
    short nRoach = RunData[nRun].nVal;
    assert(nRoach >= 0 && nRoach < kMaxRoach);

    short nSprite = RoachList[nRoach].nSprite;
    short nAction = RoachList[nRoach].nAction;

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Roach\n", nMessage);
            return;
        }

        case 0x90000:
        {
            actionSeq const &action = RoachActionSequence(nAction);
            seq_PlotSequence(a & 0xFFFF, action.a + SeqOffsets[kSeqRoach],
                             RoachList[nRoach].field_2, action.b);
            return;
        }

        case 0xA0000: // fall through to next case
        {
            nDamage = runlist_CheckRadialDamage(nSprite);
            fallthrough__;
        }
        case 0x80000:
        {
            if (nDamage)
            {
                if (RoachList[nRoach].nHealth <= 0) {
                    return;
                }

                RoachList[nRoach].nHealth -= nDamage;
                if (RoachList[nRoach].nHealth <= 0)
                {
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                    sprite[nSprite].zvel = 0;
                    sprite[nSprite].cstat &= 0xFEFE;

                    RoachList[nRoach].nHealth = 0;

                    if (nAction < 5)
                    {
                        DropMagic(nSprite);
                        if (g_kexMode)
                        {
                            short const nKiller = nMessage == 0xA0000
                                ? (short)nRadialOwner
                                : (short)(a & 0xFFFF);
                            KexSentryDeath(nRoach, nSprite, nKiller);
                            nCreaturesLeft--; // NOTE: This was incrementing in original code. Bug?
                            return;
                        }
                        RoachList[nRoach].nAction = 5;
                        RoachList[nRoach].field_2 = 0;
                    }

                    nCreaturesLeft--; // NOTE: This was incrementing in original code. Bug?
                }
                else
                {
                    short nSprite2 = a & 0xFFFF;
                    bool const bKexPain = g_kexMode ? KexDifficulty_PainRoll(24) : false;
                    if (nSprite2 >= 0)
                    {
                        if (sprite[nSprite2].statnum < 199) {
                            RoachList[nRoach].nTarget = nSprite2;
                        }

                        if (nAction == 0)
                        {
                            RoachList[nRoach].nAction = 2;
                            GoRoach(nSprite);
                            RoachList[nRoach].field_2 = 0;
                        }
                        else
                        {
                            if ((g_kexMode && bKexPain && nAction == 2)
                                || (!g_kexMode && !RandomSize(4)))
                            {
                                RoachList[nRoach].nAction = 4;
                                RoachList[nRoach].field_2 = 0;
                                if (g_kexMode && !KexSentrySequenceExact())
                                {
                                    int const sound = KexSound_Get("roch_wld");
                                    if (sound >= 0)
                                        D3PlayFX(sound, nSprite);
                                }
                            }
                        }
                    }
                }
            }

            return;
        }

        case 0x20000:
        {
            Gravity(nSprite);

            actionSeq const &action = RoachActionSequence(RoachList[nRoach].nAction);
            int nSeq = SeqOffsets[kSeqRoach] + action.a;
            short const nRenderedFrame = RoachList[nRoach].field_2;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, RoachList[nRoach].field_2);
            seq_MoveSequence(nSprite, nSeq, RoachList[nRoach].field_2);

            RoachList[nRoach].field_2++;
            if (RoachList[nRoach].field_2 >= SeqSize[nSeq])
            {
                bVal = true;
                RoachList[nRoach].field_2 = 0;
            }

            int nFlag = FrameFlag[SeqBase[nSeq] + RoachList[nRoach].field_2];
            short nTarget = RoachList[nRoach].nTarget;

            if (nAction > 5) {
                return;
            }

            switch (nAction)
            {
                case 0:
                {
                    if (RoachList[nRoach].field_2 == 1)
                    {
                        RoachList[nRoach].field_C--;
                        if (RoachList[nRoach].field_C <= 0)
                        {
                            RoachList[nRoach].field_C = RandomSize(6);
                        }
                        else
                        {
                            RoachList[nRoach].field_2 = 0;
                        }
                    }

                    if (((nRoach & 0xF) == (totalmoves & 0xF)) && nTarget < 0)
                    {
                        short nTarget = FindPlayer(nSprite, 50);
                        if (nTarget >= 0)
                        {
                            RoachList[nRoach].nAction = 2;
                            RoachList[nRoach].field_2 = 0;
                            RoachList[nRoach].nTarget = nTarget;
                            GoRoach(nSprite);
                            KexSentrySightSound(nSprite);
                        }
                    }

                    return;
                }

                case 1:
                {
                    // partly the same as case 0.
                    if (((nRoach & 0xF) == (totalmoves & 0xF)) && nTarget < 0)
                    {
                        short nTarget = FindPlayer(nSprite, 100);
                        if (nTarget >= 0)
                        {
                            RoachList[nRoach].nAction = 2;
                            RoachList[nRoach].field_2 = 0;
                            RoachList[nRoach].nTarget = nTarget;
                            GoRoach(nSprite);
                            KexSentrySightSound(nSprite);
                        }
                    }

                    return;
                }

                case 2:
                {
                    if (g_kexMode && RoachList[nRoach].field_C > 0)
                        RoachList[nRoach].field_C--;

                    if (g_kexMode && RoachList[nRoach].field_2 == 1 && RandomSize(8) >= 240)
                    {
                        static char const * const chatter[] = { "roch_act2", "roch_act3", "roch_act4" };
                        int const sound = KexSound_Get(chatter[KexRandomMax(ARRAY_SIZE(chatter))]);
                        if (sound >= 0)
                            D3PlayFX(sound, nSprite);
                    }

                    if (g_kexMode && nTarget >= 0)
                    {
                        // AI_Sentry turnSpeed 2 degrees/tick. The classic
                        // controller snaps to the target every sixteen ticks.
                        int const wanted = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                                      sprite[nTarget].y - sprite[nSprite].y);
                        int delta = AngleDelta(sprite[nSprite].ang, wanted, 1024);
                        delta = clamp(delta, -11, 11);
                        sprite[nSprite].ang = (sprite[nSprite].ang + delta) & kAngleMask;
                        GoRoach(nSprite);
                    }
                    else if ((totalmoves & 0xF) == (nRoach & 0xF))
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        GoRoach(nSprite);
                    }

                    int nMov = MoveCreatureWithCaution(nSprite);

                    if ((nMov & 0xC000) == 0xC000)
                    {
                        if ((nMov & 0x3FFF) == nTarget)
                        {
                            if (!g_kexMode || RoachList[nRoach].field_C <= 0)
                                StartRoachRangeAttack(nRoach, nSprite, nTarget);
                        }
                        else
                        {
                            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                            GoRoach(nSprite);
                        }
                    }
                    else if ((nMov & 0xC000) == 0x8000)
                    {
                        sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                        GoRoach(nSprite);
                    }
                    else
                    {
                        if (!g_kexMode && RoachList[nRoach].field_C != 0)
                        {
                            RoachList[nRoach].field_C--;
                        }
                        else if (!g_kexMode)
                        {
                            StartRoachRangeAttack(nRoach, nSprite, nTarget);
                        }
                        else if (RoachList[nRoach].field_C <= 0
                                 && cansee(sprite[nSprite].x, sprite[nSprite].y,
                                           sprite[nSprite].z - GetSpriteHeight(nSprite), sprite[nSprite].sectnum,
                                           sprite[nTarget].x, sprite[nTarget].y,
                                           sprite[nTarget].z - GetSpriteHeight(nTarget), sprite[nTarget].sectnum))
                        {
                            StartRoachRangeAttack(nRoach, nSprite, nTarget);
                        }
                    }

                    if (nTarget != -1 && !(sprite[nTarget].cstat & 0x101))
                    {
                        RoachList[nRoach].nAction = 1;
                        RoachList[nRoach].field_2 = 0;
                        RoachList[nRoach].field_C = 100;
                        RoachList[nRoach].nTarget = -1;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                    }

                    return;
                }

                case 3:
                {
                    if (bVal)
                    {
                        RoachList[nRoach].field_E--;
                        if (RoachList[nRoach].field_E <= 0)
                        {
                            RoachList[nRoach].nAction = 2;
                            GoRoach(nSprite);
                            RoachList[nRoach].field_2 = 0;
                            if (!g_kexMode)
                                RoachList[nRoach].field_C = RandomSize(7);
                        }
                    }
                    else
                    {
                        bool const bExactFire = KexSentrySequenceExact()
                                             && nRenderedFrame == 6;
                        if (bExactFire || (!KexSentrySequenceExact() && (nFlag & 0x80)))
                        {
                            if (g_kexMode && !KexSentrySequenceExact())
                            {
                                int const sound = KexSound_Get("roch_gun");
                                if (sound >= 0)
                                    D3PlayFX(sound, nSprite);
                            }
                            if (nRoach == s_sentryTestRoach)
                                s_sentryTestFireFrame = nRenderedFrame;
                            int nBullet;
                            if (g_kexMode)
                                nBullet = BuildKexAIFireProjectile(nSprite, 13,
                                    nTarget, 4, 96, 24, 0);
                            else
                                nBullet = BuildBullet(nSprite, 13, 0, 0, -1,
                                    sprite[nSprite].ang, nTarget + 10000, 1);
                            if (nRoach == s_sentryTestRoach)
                                s_sentryTestProjectileOkay =
                                    KexBullet_CheckAIFireProjectile(nBullet,
                                        nSprite, nTarget, 4, 96, 24, 0);
                        }
                    }

                    return;
                }

                case 4:
                {
                    // Retail sentry_pain has twelve source frames but its
                    // goto chase callback is on frame ten; frame eleven is
                    // unreachable. Stock Roach still waits for its wrap.
                    if ((KexSentrySequenceExact() && nRenderedFrame == 10) || bVal)
                    {
                        RoachList[nRoach].nAction = 2;
                        RoachList[nRoach].field_2 = 0;
                        GoRoach(nSprite);
                    }

                    return;
                }

                case 5:
                {
                    if (bVal)
                    {
                        sprite[nSprite].cstat = 0;
                        RoachList[nRoach].nAction = 6;
                        RoachList[nRoach].field_2 = 0;
                    }

                    return;
                }
            }
        }
    }
}

class RoachLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void RoachLoadSave::Load()
{
    Read(&RoachCount, sizeof(RoachCount));
    Read(&RoachList[RoachCount], sizeof(Roach) * (kMaxRoach - RoachCount));
}

void RoachLoadSave::Save()
{
    Write(&RoachCount, sizeof(RoachCount));
    Write(&RoachList[RoachCount], sizeof(Roach) * (kMaxRoach - RoachCount));
}

static RoachLoadSave* myLoadSave;

void RoachLoadSaveConstruct()
{
    myLoadSave = new RoachLoadSave();
}
