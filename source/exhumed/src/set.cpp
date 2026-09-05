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

#include "set.h"
#include "engine.h"
#include "exhumed.h"
#include "runlist.h"
#include "sequence.h"
#include "random.h"
#include "move.h"
#include "trigdat.h"
#include "bullet.h"
#include "set.h"
#include "save.h"
#include "kexbreak.h"   // VibeBuild32
#include "kexinv.h"
#include "sound.h"   // VibeBuild32
#include "player.h"
#include <assert.h>

#define kMaxSets    10

static actionSeq ActionSeq[] = {
    {0,  0},
    {77, 1},
    {78, 1},
    {0,  0},
    {9,  0},
    {63, 0},
    {45, 0},
    {18, 0},
    {27, 0},
    {36, 0},
    {72, 1},
    {74, 1}
};

// Generated KEX SET.SEQ groups. The stock table above remains the fallback
// for classic/demo data; these slots preserve every retail frame count.
static actionSeq KexSetActionSeq[] = {
    {0,  0},   // idle
    {0,  0},   // spawn idle
    {0,  0},   // native wake compatibility (unused by KEX)
    {9,  0},   // chase
    {18, 0},   // melee
    {63, 0},   // pain
    {54, 0},   // fire souls
    {27, 0},   // leap start
    {36, 0},   // leap loop
    {45, 0},   // leap land
    {72, 1},   // death
    {73, 1},   // death loop
};

struct Set
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short field_A;
    short field_C;
    short field_D;
    short field_E;
};

short SetCount = 0;

Set SetList[kMaxSets];
short SetChan[kMaxSets];

static int s_setAudit = -1;
static int s_setAuditSeen;
static int s_setTest = -1;
static int s_setTestPhase;
static int s_setTestTicks;
static short s_setTestSet = -1;
static short s_setTestPlayer = -1;
static short s_setTestInvincibility;
static short s_setTestMeleeMask;
static short s_setTestFireFrame = -1;
static short s_setTestLaunchFrame = -1;
static int s_setTestSpeed;
static int s_setTestOkay;
static int s_setTestProjectileOkay;

static bool KexSetSequenceExact()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqSet))
        return false;
    int const base = SeqOffsets[kSeqSet];
    // The generated file has a one-frame directional idle, 13-frame leap,
    // 15-frame cast and 30-frame death. Demo SET.SEQ has 20/15/18/112.
    return SeqSize[base] == 1 && SeqSize[base + 27] == 13
        && SeqSize[base + 54] == 15 && SeqSize[base + 72] == 30;
}

static actionSeq const &SetActionSequence(int nAction)
{
    return KexSetSequenceExact() ? KexSetActionSeq[nAction] : ActionSeq[nAction];
}

static int SetSoulSequence()
{
    return KexSetSequenceExact() ? 74 : 75;
}

static void KexSetSightSound(short nSprite)
{
    if (!g_kexMode)
        return;
    int const sound = KexSound_Get("set_icu");
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexSetAudit(short nSet)
{
    if (s_setAudit < 0)
        s_setAudit = g_kexMode && Bgetenv("VB_KEX_SET_AUDIT") ? 1 : 0;
    if (!s_setAudit || s_setAuditSeen)
        return;
    s_setAuditSeen = 1;
    short const nSprite = SetList[nSet].nSprite;
    OSD_Printf("Kex Set: sprite %d hp %d clip %d repeat %d speed %d seq %d exact %d\n",
               nSprite, KexHealthPoints(SetList[nSet].nHealth),
               (int)sprite[nSprite].clipdist, (int)sprite[nSprite].xrepeat,
               clamp((80 - KexHealthPoints(SetList[nSet].nHealth) / 75) / 4, 1, 20),
               kSeqSet, KexSetSequenceExact() ? 1 : 0);
}

static void KexSetPursuitVelocity(short nSprite, int nHealth)
{
    // A_SetThink computes the move speed from current health. XY map
    // coordinates are six times the authored KEX units.
    int const nSpeed = clamp((80 - KexHealthPoints(nHealth) / 75) / 4, 1, 20);
    int const nWorldSpeed = nSpeed * 6;
    sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * nWorldSpeed) >> 8;
    sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * nWorldSpeed) >> 8;
}

static bool KexSetThink(short nSet, short nSprite)
{
    KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);

    // scripts/p_enemy.txt RandomDecision(ai, 30): only one of the sixteen
    // masked RNG phases matching this game tick evaluates the decisions.
    if ((RandomLong() & 30) != (totalmoves & 30))
        return false;

    if ((uint8_t)RandomByte() <= 16)
    {
        int const sound = KexSound_Get("set_gr1");
        if (sound >= 0)
            D3PlayFX(sound, nSprite);
    }

    int const health = KexHealthPoints(SetList[nSet].nHealth);
    if (health < 1500 && (((uint8_t)RandomByte() & 1) == 0))
    {
        SetList[nSet].nAction = 6;
        SetList[nSet].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        return true;
    }
    if (health < 2000 && (((uint8_t)RandomByte() & 3) == 0))
    {
        SetList[nSet].nAction = 7;
        SetList[nSet].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        return true;
    }
    return false;
}


void InitSets()
{
    SetCount = kMaxSets;
    s_setAudit = -1;
    s_setAuditSeen = 0;
    s_setTest = -1;
    s_setTestSet = -1;
    s_setTestProjectileOkay = 0;
}

int BuildSet(short nSprite, int x, int y, int z, short nSector, short nAngle, int nChannel)
{
    if (SetCount <= 0)
        return -1;
    short const nSet = --SetCount;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 120);
    }
    else
    {
        changespritestat(nSprite, 120);
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
    sprite[nSprite].shade = -12;
    sprite[nSprite].clipdist = g_kexMode ? 144 : 110;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].xrepeat = g_kexMode ? 80 : 87;
    sprite[nSprite].yrepeat = g_kexMode ? 80 : 96;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

    //	GrabTimeSlot(3);

    SetList[nSet].nAction = 1;
    // AI_Set has 3000 health in the KEX scripts; the original Build Set
    // encounter remains at 8000.
    SetList[nSet].nHealth = g_kexMode ? KexHealthUnits(3000) : 8000;
    SetList[nSet].nSprite = nSprite;
    SetList[nSet].nFrame  = 0;
    SetList[nSet].nTarget = -1;
    SetList[nSet].field_A = 90;
    SetList[nSet].field_C = 0;
    SetList[nSet].field_D = 0;

    SetChan[nSet] = nChannel;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nSet, kRunSet);

    // this isn't stored anywhere.
    runlist_AddRunRec(NewRun, nSet, kRunSet);

    nCreaturesLeft++;
    KexSetAudit(nSet);

    return nSet;
}

static void KexSetTestFinish(short nSprite)
{
    if ((unsigned)s_setTestSet < (unsigned)kMaxSets && nSprite >= 0)
    {
        SetList[s_setTestSet].nHealth = 0;
        SetList[s_setTestSet].nAction = 11;
        SetList[s_setTestSet].nFrame = 0;
        SetList[s_setTestSet].nTarget = -1;
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        sprite[nSprite].cstat = 0x8000; // env-only inert hidden corpse/run record
        nCreaturesLeft--;
    }
    if ((unsigned)s_setTestPlayer < (unsigned)nTotalPlayers)
        PlayerList[s_setTestPlayer].invincibility = s_setTestInvincibility;
    s_setTest = -2;
    s_setTestSet = -1;
}

void KexSet_TestTick()
{
    if (s_setTest == -1)
        s_setTest = g_kexMode && Bgetenv("VB_KEX_SET_TEST") ? 0 : -2;
    if (s_setTest < 0)
        return;

    if (s_setTest == 0)
    {
        short const nPlayer = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayer >= (unsigned)kMaxSprites || sprite[nPlayer].sectnum < 0)
            return;
        short const nSector = sprite[nPlayer].sectnum;
        s_setTestSet = (short)BuildSet(-1, sprite[nPlayer].x + 700,
            sprite[nPlayer].y, sector[nSector].floorz, nSector,
            sprite[nPlayer].ang, 0);
        if (s_setTestSet < 0)
        {
            OSD_Printf("Kex Set test: FAIL (no free controller)\n");
            s_setTest = -2;
            return;
        }
        short const nSprite = SetList[s_setTestSet].nSprite;
        SetList[s_setTestSet].nTarget = nPlayer;
        SetList[s_setTestSet].nAction = 4;
        SetList[s_setTestSet].nFrame = 0;
        KexSetPursuitVelocity(nSprite, SetList[s_setTestSet].nHealth);
        s_setTestSpeed = ksqrt((uint32_t)(sprite[nSprite].xvel * sprite[nSprite].xvel
                                         + sprite[nSprite].yvel * sprite[nSprite].yvel));
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_setTestPlayer = nLocalPlayer;
        s_setTestInvincibility = PlayerList[nLocalPlayer].invincibility;
        PlayerList[nLocalPlayer].invincibility = 1;
        s_setTestMeleeMask = 0;
        s_setTestFireFrame = -1;
        s_setTestLaunchFrame = -1;
        s_setTestOkay = 1;
        s_setTestProjectileOkay = 0;
        s_setTestPhase = 1;
        s_setTestTicks = 0;
        s_setTest = 1;
        OSD_Printf("Kex Set test: actor %d spawned; exact melee/cast/pain/leap probe started\n",
                   nSprite);
        return;
    }

    if (s_setTestSet < SetCount || s_setTestSet >= kMaxSets)
    {
        OSD_Printf("Kex Set test: FAIL (controller disappeared)\n");
        KexSetTestFinish(-1);
        return;
    }
    short const nSprite = SetList[s_setTestSet].nSprite;
    if ((unsigned)nSprite >= (unsigned)kMaxSprites || sprite[nSprite].sectnum < 0)
    {
        OSD_Printf("Kex Set test: FAIL (actor disappeared)\n");
        KexSetTestFinish(-1);
        return;
    }

    s_setTestTicks++;
    if (s_setTestPhase == 1 && s_setTestTicks == 18)
    {
        s_setTestOkay &= SetList[s_setTestSet].nAction == 4
            && SetList[s_setTestSet].nFrame == 0 && s_setTestMeleeMask == 3;
        SetList[s_setTestSet].nAction = 6;
        SetList[s_setTestSet].nFrame = 0;
        s_setTestPhase = 2; s_setTestTicks = 0;
    }
    else if (s_setTestPhase == 2 && s_setTestTicks == 15)
    {
        s_setTestOkay &= SetList[s_setTestSet].nAction == 3
            && SetList[s_setTestSet].nFrame == 0 && s_setTestFireFrame == 7;
        SetList[s_setTestSet].nAction = 5;
        SetList[s_setTestSet].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_setTestPhase = 3; s_setTestTicks = 0;
    }
    else if (s_setTestPhase == 3 && s_setTestTicks == 8)
    {
        s_setTestOkay &= SetList[s_setTestSet].nAction == 3
            && SetList[s_setTestSet].nFrame == 0;
        SetList[s_setTestSet].nAction = 7;
        SetList[s_setTestSet].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        s_setTestPhase = 4; s_setTestTicks = 0;
    }
    else if (s_setTestPhase == 4 && s_setTestTicks == 13)
    {
        int const base = SeqOffsets[kSeqSet];
        bool const cadence = KexSetSequenceExact()
            && SeqSize[base] == 1 && SeqSize[base + 9] == 20
            && SeqSize[base + 18] == 18 && SeqSize[base + 27] == 13
            && SeqSize[base + 36] == 1 && SeqSize[base + 45] == 12
            && SeqSize[base + 54] == 15 && SeqSize[base + 63] == 8
            && SeqSize[base + 72] == 30 && SeqSize[base + 73] == 1;
        bool const stats = SetList[s_setTestSet].nHealth == KexHealthUnits(3000)
            && sprite[nSprite].clipdist == 144
            && sprite[nSprite].xrepeat == 80 && sprite[nSprite].yrepeat == 80
            && s_setTestSpeed >= 3830 && s_setTestSpeed <= 3850;
        bool const callbacks = s_setTestMeleeMask == 3 && s_setTestFireFrame == 7
            && s_setTestLaunchFrame == 12
            && s_setTestProjectileOkay
            && (FrameFlag[SeqBase[base + 54] + 9] & 4)
            && SetList[s_setTestSet].nAction == 8
            && SetList[s_setTestSet].nFrame == 0 && sprite[nSprite].zvel < 0;
        bool const okay = s_setTestOkay && cadence && stats && callbacks;
        OSD_Printf("Kex Set test: %s (hp %d, clip %d, repeat %d, speed %d, seq %d/%d/%d/%d/%d/%d/%d/%d/%d/%d, melee %d, fire %d, projectile %d, leap %d, action/frame %d/%d)\n",
                   okay ? "PASS" : "FAIL", KexHealthPoints(SetList[s_setTestSet].nHealth),
                   (int)sprite[nSprite].clipdist, (int)sprite[nSprite].xrepeat,
                   s_setTestSpeed, SeqSize[base], SeqSize[base + 9],
                   SeqSize[base + 18], SeqSize[base + 27], SeqSize[base + 36],
                   SeqSize[base + 45], SeqSize[base + 54], SeqSize[base + 63],
                   SeqSize[base + 72], SeqSize[base + 73], s_setTestMeleeMask,
                   s_setTestFireFrame, s_setTestProjectileOkay,
                   s_setTestLaunchFrame,
                   SetList[s_setTestSet].nAction, SetList[s_setTestSet].nFrame);
        KexSetTestFinish(nSprite);
    }
}

int BuildSoul(int nSet)
{
    int nSetSprite = SetList[nSet].nSprite;
    int nSprite = insertsprite(sprite[nSetSprite].sectnum, 0);

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].cstat = 0x8000;
    sprite[nSprite].shade = -127;
    sprite[nSprite].xrepeat = 1;
    sprite[nSprite].yrepeat = 1;
    sprite[nSprite].pal = 0;
    sprite[nSprite].clipdist = 5;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = seq_GetSeqPicnum(kSeqSet, SetSoulSequence(), 0);
    sprite[nSprite].ang = RandomSize(11);
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = (-256) - RandomSize(10);
    sprite[nSprite].x = sprite[nSetSprite].x;
    sprite[nSprite].y = sprite[nSetSprite].y;

    short nSector = sprite[nSprite].sectnum;
    sprite[nSprite].z = (RandomSize(8) << 8) + 8192 + sector[nSector].ceilingz - GetSpriteHeight(nSprite);

    sprite[nSprite].hitag = nSet;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = 0;

//	GrabTimeSlot(3);

    sprite[nSprite].owner = runlist_AddRunRec(NewRun, nSprite, kRunSoul);

    return nSprite | 0x230000; // TODO: What should this return, ideally/usefully?
}

void FuncSoul(int a, int UNUSED(b), int nRun)
{
    short nSprite = RunData[nRun].nVal;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x20000:
        {
            seq_MoveSequence(nSprite, SeqOffsets[kSeqSet] + SetSoulSequence(), 0);

            if (sprite[nSprite].xrepeat < 32)
            {
                sprite[nSprite].xrepeat++;
                sprite[nSprite].yrepeat++;
            }

            sprite[nSprite].extra += (nSprite & 0x0F) + 5;
            sprite[nSprite].extra &= kAngleMask;

            int nVel = (Cos(sprite[nSprite].extra) >> 7);

            if (movesprite(nSprite, Cos(sprite[nSprite].ang) * nVel, Sin(sprite[nSprite].ang) * nVel, sprite[nSprite].zvel, 5120, 0, CLIPMASK0) & 0x10000)
            {
                int nSet = sprite[nSprite].hitag;
                int nSetSprite = SetList[nSet].nSprite;

                sprite[nSprite].cstat = 0;
                sprite[nSprite].yrepeat = 1;
                sprite[nSprite].xrepeat = 1;
                sprite[nSprite].x = sprite[nSetSprite].x;
                sprite[nSprite].y = sprite[nSetSprite].y;
                sprite[nSprite].z = sprite[nSetSprite].z - (GetSpriteHeight(nSetSprite) >> 1);
                mychangespritesect(nSprite, sprite[nSetSprite].sectnum);
                return;
            }
        }

        case 0x80000:
        case 0xA0000:
        case 0x90000:
            return;

        default:
            DebugOut("unknown msg %d for Soul\n", nMessage);
    }
}

void FuncSet(int a, int nDamage, int nRun)
{
    short nSet = RunData[nRun].nVal;
    assert(nSet >= 0 && nSet < kMaxSets);

    short nSprite = SetList[nSet].nSprite;
    short nAction = SetList[nSet].nAction;

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Set\n", nMessage);
            return;
        }

        case 0xA0000:
        {
            if (g_kexMode || nAction == 5)
            {
                nDamage = runlist_CheckRadialDamage(nSprite);
                // fall through to case 0x80000
            }
            fallthrough__;
        }

        case 0x80000:
        {
            if (nDamage && SetList[nSet].nHealth > 0)
            {
                if (nAction != 1)
                {
                    SetList[nSet].nHealth -= nDamage;
                }

                if (SetList[nSet].nHealth <= 0)
                {
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                    sprite[nSprite].zvel = 0;
                    sprite[nSprite].cstat &= 0xFEFE;

                    SetList[nSet].nHealth = 0;

                    nCreaturesLeft--;

                    if (nAction < 10)
                    {
                        SetList[nSet].nFrame  = 0;
                        SetList[nSet].nAction = 10;
                        if (g_kexMode && !KexSetSequenceExact())
                        {
                            int const sound = KexSound_Get("set_deth");
                            if (sound >= 0)
                                PlayLocalSound(sound, 0);
                        }
                    }
                }
                else if (g_kexMode)
                {
                    short const nSource = a & 0xFFFF;
                    // AI_Set has noInFighting: only a player can replace its
                    // target in KEX mode.
                    if (nSource >= 0 && sprite[nSource].statnum == 100)
                        SetList[nSet].nTarget = nSource;

                    bool const bPain = KexDifficulty_PainRoll(24);
                    if (nAction == 1)
                    {
                        SetList[nSet].nAction = 2;
                        SetList[nSet].nFrame = 0;
                    }
                    else if (bPain && nAction == 3)
                    {
                        SetList[nSet].nAction = 5;
                        SetList[nSet].nFrame = 0;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                    }
                }
                else if (nAction == 1)
                {
                    SetList[nSet].nAction = 2;
                    SetList[nSet].nFrame  = 0;
                }
            }
            return;
        }

        case 0x90000:
        {
            actionSeq const &action = SetActionSequence(nAction);
            seq_PlotSequence(a, SeqOffsets[kSeqSet] + action.a,
                             SetList[nSet].nFrame, action.b);
            return;
        }

        case 0x20000:
        {
            Gravity(nSprite);

            actionSeq const &action = SetActionSequence(SetList[nSet].nAction);
            short nSeq = SeqOffsets[kSeqSet] + action.a;
            short const nRenderedFrame = SetList[nSet].nFrame;
            bool const bExactSet = KexSetSequenceExact();
            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, SetList[nSet].nFrame);
            seq_MoveSequence(nSprite, nSeq, SetList[nSet].nFrame);

            if (!g_kexMode && nAction == 3)
            {
                if (SetList[nSet].field_D) {
                    SetList[nSet].nFrame++;
                }
            }

            SetList[nSet].nFrame++;
            if (SetList[nSet].nFrame >= SeqSize[nSeq])
            {
                SetList[nSet].nFrame = 0;
                bVal = true;
            }

            short nFlag = FrameFlag[SeqBase[nSeq] + SetList[nSet].nFrame];
            short nTarget = SetList[nSet].nTarget;

            if (nTarget > -1 && nAction < 10)
            {
                if (!(sprite[nTarget].cstat & 0x101))
                {
                    SetList[nSet].nTarget = -1;
                    SetList[nSet].nAction = 0;
                    SetList[nSet].nFrame  = 0;
                    nTarget = -1;
                }
            }

            int nMov = MoveCreature(nSprite);

            pushmove_old(&sprite[nSprite].x, &sprite[nSprite].y, &sprite[nSprite].z, &sprite[nSprite].sectnum, sprite[nSprite].clipdist << 2, 5120, -5120, CLIPMASK0);

            if (sprite[nSprite].zvel > 4000)
            {
                if (nMov & 0x20000)
                {
                    if (!g_kexMode)
                        SetQuake(nSprite, 100);
                }
            }

            switch (nAction)
            {
                default:
                    return;

                case 0:
                {
                    if ((nSet & 0x1F) == (totalmoves & 0x1F))
                    {
                        if (nTarget < 0)
                        {
                            nTarget = FindPlayer(nSprite, 1000);
                        }

                        if (nTarget >= 0)
                        {
                            SetList[nSet].nAction = 3;
                            SetList[nSet].nFrame  = 0;
                            SetList[nSet].nTarget = nTarget;

                            if (g_kexMode)
                            {
                                PlotCourseToSprite(nSprite, nTarget);
                                KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                                KexSetSightSound(nSprite);
                            }
                            else
                            {
                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                                sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                            }
                        }
                    }

                    return;
                }

                case 1:
                {
                    short const nFound = FindPlayer(nSprite, 1000);
                    if (g_kexMode && nFound >= 0)
                    {
                        SetList[nSet].nTarget = nFound;
                        SetList[nSet].nAction = 3;
                        SetList[nSet].nFrame = 0;
                        PlotCourseToSprite(nSprite, nFound);
                        KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                        KexSetSightSound(nSprite);
                    }
                    else if (!g_kexMode && nFound >= 0)
                    {
                        SetList[nSet].field_A--;
                        if (SetList[nSet].field_A <= 0)
                        {
                            SetList[nSet].nAction = 2;
                            SetList[nSet].nFrame  = 0;
                        }
                    }

                    return;
                }

                case 2:
                {
                    if (bVal)
                    {
                        SetList[nSet].nAction = g_kexMode ? 3 : 7;
                        SetList[nSet].field_C = 0;
                        SetList[nSet].nFrame  = 0;

                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;

                        SetList[nSet].nTarget = FindPlayer(nSprite, 1000);
                        if (g_kexMode && SetList[nSet].nTarget >= 0)
                        {
                            PlotCourseToSprite(nSprite, SetList[nSet].nTarget);
                            KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                        }
                    }
                    return;
                }

                case 3:
                {
                    if (nTarget != -1)
                    {
                        if ((nFlag & 0x10) && (nMov & 0x20000))
                        {
                            if (!g_kexMode)
                                SetQuake(nSprite, 100);
                        }

                        int nCourse = PlotCourseToSprite(nSprite, nTarget);

                        if (g_kexMode)
                        {
                            if (KexSetThink(nSet, nSprite))
                                return;
                        }
                        else if ((nSet & 0x1F) == (totalmoves & 0x1F))
                        {
                            int nRand = RandomSize(3);

                            switch (nRand)
                            {
                                case 0:
                                case 2:
                                {
                                    SetList[nSet].field_C = 0;
                                    SetList[nSet].nAction = 7;
                                    SetList[nSet].nFrame  = 0;
                                    sprite[nSprite].xvel = 0;
                                    sprite[nSprite].yvel = 0;
                                    return;
                                }
                                case 1:
                                {
                                    PlotCourseToSprite(nSprite, nTarget);

                                    SetList[nSet].nAction = 6;
                                    SetList[nSet].nFrame  = 0;
                                    SetList[nSet].field_E = 5;
                                    sprite[nSprite].xvel = 0;
                                    sprite[nSprite].yvel = 0;
                                    return;
                                }
                                default:
                                {
                                    if (nCourse <= 100)
                                    {
                                        SetList[nSet].field_D = 0;
                                    }
                                    else
                                    {
                                        SetList[nSet].field_D = 1;
                                    }
                                    break;
                                }
                            }
                        }

                        // loc_338E2
                        int nAngle = sprite[nSprite].ang & 0xFFF8;
                        if (g_kexMode)
                        {
                            sprite[nSprite].ang = nAngle;
                            KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                        }
                        else
                        {
                            sprite[nSprite].xvel = Cos(nAngle) >> 1;
                            sprite[nSprite].yvel = Sin(nAngle) >> 1;
                        }

                        if (!g_kexMode && SetList[nSet].field_D)
                        {
                            sprite[nSprite].xvel *= 2;
                            sprite[nSprite].yvel *= 2;
                        }

                        if ((nMov & 0xC000) == 0x8000)
                        {
                            short nWall = nMov & 0x3FFF;
                            short nSector = wall[nWall].nextsector;

                            if (nSector >= 0)
                            {
                                if (!g_kexMode && (sprite[nSprite].z - sector[nSector].floorz) < 55000)
                                {
                                    if (sprite[nSprite].z > sector[nSector].ceilingz)
                                    {
                                        SetList[nSet].field_C = 1;
                                        SetList[nSet].nAction = 7;
                                        SetList[nSet].nFrame  = 0;
                                        sprite[nSprite].xvel = 0;
                                        sprite[nSprite].yvel = 0;
                                        return;
                                    }
                                }
                            }

                            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                            if (g_kexMode)
                                KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                            else
                            {
                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 1;
                                sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 1;
                            }
                            break;
                        }
                        else if ((nMov & 0xC000) == 0xC000)
                        {
                            if (nTarget == (nMov & 0x3FFF))
                            {
                                int nAng = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                                if (AngleDiff(sprite[nSprite].ang, nAng) < 64)
                                {
                                    SetList[nSet].nAction = 4;
                                    SetList[nSet].nFrame  = 0;
                                }
                                break;
                            }
                            else if (!g_kexMode)
                            {
                                SetList[nSet].field_C = 1;
                                SetList[nSet].nAction = 7;
                                SetList[nSet].nFrame  = 0;
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                                return;
                            }
                            else
                            {
                                sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                                KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                            }
                        }

                        break;
                    }
                    else
                    {
                        SetList[nSet].nAction = 0;
                        SetList[nSet].nFrame  = 0;
                        return;
                    }
                }

                case 4:
                {
                    if (bExactSet)
                    {
                        if (nTarget == -1)
                        {
                            SetList[nSet].nAction = 0;
                            SetList[nSet].nFrame = 0;
                        }
                        else if (nRenderedFrame == 8 || nRenderedFrame == 17)
                        {
                            if (nSet == s_setTestSet)
                                s_setTestMeleeMask |= nRenderedFrame == 8 ? 1 : 2;
                            // A_CheckMelee(..., 0) precedes A_MeleeAttack(48,40)
                            // on both authored strike frames.
                            if (PlotCourseToSprite(nSprite, nTarget) >= 768)
                            {
                                SetList[nSet].nAction = 3;
                                SetList[nSet].nFrame = 0;
                                KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                            }
                            else
                                runlist_DamageEnemy(nTarget, nSprite, 40);
                        }
                        return;
                    }
                    if (nTarget == -1)
                    {
                        SetList[nSet].nAction = 0;
                        SetList[nSet].field_A = 50;
                    }
                    else
                    {
                        if (PlotCourseToSprite(nSprite, nTarget) >= 768)
                        {
                            SetList[nSet].nAction = 3;
                        }
                        else if (nFlag & 0x80)
                        {
                            { int const s = KexSound_Get("k_setstp"); if (s >= 0) D3PlayFX(s, nSprite); }
                            runlist_DamageEnemy(nTarget, nSprite, g_kexMode ? 40 : 5);
                        }
                    }

                    break;
                }

                case 5:
                {
                    if (bVal)
                    {
                        if (g_kexMode)
                        {
                            SetList[nSet].nAction = 3;
                            SetList[nSet].nFrame = 0;
                            KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                        }
                        else
                        {
                            SetList[nSet].nAction = 0;
                            SetList[nSet].field_A = 15;
                        }
                    }
                    return;
                }

                case 6:
                {
                    if (bExactSet)
                    {
                        if (nTarget >= 0 && (nRenderedFrame == 0 || nRenderedFrame == 1
                            || nRenderedFrame == 2 || nRenderedFrame == 5
                            || nRenderedFrame == 7 || nRenderedFrame == 9))
                            PlotCourseToSprite(nSprite, nTarget);

                        if (nRenderedFrame == 7 && nTarget >= 0)
                        {
                            if (nSet == s_setTestSet)
                                s_setTestFireFrame = nRenderedFrame;
                            int const nBullet = BuildKexAIFireProjectile(
                                nSprite, 11, nTarget, 50, 192, 16, 1);
                            if (nSet == s_setTestSet)
                                s_setTestProjectileOkay =
                                    KexBullet_CheckAIFireProjectile(nBullet,
                                        nSprite, nTarget, 50, 192, 16, 1);
                        }
                        if (bVal)
                        {
                            SetList[nSet].nAction = 3;
                            SetList[nSet].nFrame = 0;
                            KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                        }
                        return;
                    }
                    if (nFlag & 0x80)
                    {
                        if (g_kexMode)
                        {
                            int const sound = KexSound_Get("ghost109");
                            if (sound >= 0)
                                D3PlayFX(sound, nSprite);
                        }

                        int const nBullet = g_kexMode
                            ? BuildKexAIFireProjectile(nSprite, 11, nTarget,
                                                      50, 192, 16, 1)
                            : BuildBullet(nSprite, 11, 0, 0, -1,
                                          sprite[nSprite].ang,
                                          nTarget + 10000, 1);
                        if (!g_kexMode && nBullet >= 0)
                            SetBulletEnemy(nBullet, nTarget);

                        SetList[nSet].field_E--;
                        if (SetList[nSet].field_E <= 0 || !RandomBit())
                        {
                            SetList[nSet].nAction = 0;
                            SetList[nSet].nFrame  = 0;
                        }
                    }
                    return;
                }

                case 7:
                {
                    if (g_kexMode && nRenderedFrame == 0 && nTarget >= 0)
                        PlotCourseToSprite(nSprite, nTarget);
                    if (bVal)
                    {
                        if (g_kexMode)
                        {
                            if (nSet == s_setTestSet)
                                s_setTestLaunchFrame = nRenderedFrame;
                            // A_SetLeap uses (moveSpeed * .5) + 5 KEX
                            // units/tick horizontally and a fixed 30-unit
                            // upward impulse. Convert both to this port's
                            // six-times XY / sixteen-times Z world scale.
                            int const nMoveSpeed = clamp((80 - (KexHealthPoints(SetList[nSet].nHealth) / 75)) / 4, 1, 20);
                            int const nWorldSpeed = nMoveSpeed * 3 + 30;
                            sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * nWorldSpeed) >> 8;
                            sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * nWorldSpeed) >> 8;
                            sprite[nSprite].zvel = -(30 * 16 * 6);
                        }
                        else if (SetList[nSet].field_C)
                        {
                            sprite[nSprite].zvel = -10000;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
                        }
                        else
                        {
                            sprite[nSprite].zvel = -(PlotCourseToSprite(nSprite, nTarget));
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
                        }

                        SetList[nSet].nAction = 8;
                        SetList[nSet].nFrame  = 0;
                    }
                    return;
                }

                case 8:
                {
                    if (bVal)
                    {
                        SetList[nSet].nFrame = SeqSize[nSeq] - 1;
                    }

                    if (nMov & 0x20000)
                    {
                        if (!g_kexMode)
                            SetQuake(nSprite, 200);
                        // A_SetCheckGround in the remaster's p_enemy script
                        // punishes a grounded player caught by Set's landing.
                        // The native leap already supplies the landing event;
                        // keep this extra hit KEX-only so classic Set remains
                        // unchanged.
                        if (g_kexMode && nTarget >= 0
                            && sprite[nTarget].statnum == 100
                            && sprite[nTarget].z <= sector[sprite[nTarget].sectnum].floorz + 1)
                            runlist_DamageEnemy(nTarget, nSprite, 40);
                        SetList[nSet].nAction = 9;
                        SetList[nSet].nFrame  = 0;
                    }
                    return;
                }

                case 9:
                {
                    sprite[nSprite].xvel >>= 1;
                    sprite[nSprite].yvel >>= 1;

                    if (g_kexMode && nRenderedFrame == 6)
                        KexQuake(8); // exact A_Quake duration, aligned to retail frame 6

                    if (bVal)
                    {
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;

                        if (nTarget >= 0)
                            PlotCourseToSprite(nSprite, nTarget);

                        SetList[nSet].nAction = g_kexMode ? 3 : 6;
                        SetList[nSet].nFrame  = 0;
                        if (!g_kexMode)
                            SetList[nSet].field_E = 5;

                        if (g_kexMode)
                            KexSetPursuitVelocity(nSprite, SetList[nSet].nHealth);
                        else
                        {
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                        }
                    }
                    return;
                }

                case 10:
                {
                    if (!g_kexMode && (nFlag & 0x80))
                    {
                        sprite[nSprite].z -= GetSpriteHeight(nSprite);
                        BuildCreatureChunk(nSprite, seq_GetSeqPicnum(kSeqSet, 76, 0));
                        sprite[nSprite].z += GetSpriteHeight(nSprite);
                    }

                    if (bVal)
                    {
                        SetList[nSet].nAction = 11;
                        SetList[nSet].nFrame  = 0;

                        runlist_ChangeChannel(SetChan[nSet], 1);

                        // A_SetDeath only fires remote event tag 1010. The
                        // twenty rising souls belong to the DOS death path.
                        if (!g_kexMode)
                            for (int i = 0; i < 20; i++)
                                BuildSoul(nSet);
                    }
                    return;
                }

                case 11:
                {
                    sprite[nSprite].cstat &= 0xFEFE;
                    return;
                }
            }

            // loc_33AE3: ?
            if (nAction)
            {
                if (nTarget != -1)
                {
                    if (!(sprite[nTarget].cstat & 0x101))
                    {
                        SetList[nSet].nAction = 0;
                        SetList[nSet].nFrame  = 0;
                        SetList[nSet].field_A = 100;
                        SetList[nSet].nTarget = -1;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                    }
                }
            }

            return;
        }
    }
}

class SetLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void SetLoadSave::Load()
{
    Read(&SetCount, sizeof(SetCount));
    Read(&SetList[SetCount], sizeof(Set) * (kMaxSets - SetCount));
    Read(SetChan, sizeof(SetChan));
}

void SetLoadSave::Save()
{
    Write(&SetCount, sizeof(SetCount));
    Write(&SetList[SetCount], sizeof(Set) * (kMaxSets - SetCount));
    Write(SetChan, sizeof(SetChan));
}

static SetLoadSave* myLoadSave;

void SetLoadSaveConstruct()
{
    myLoadSave = new SetLoadSave();
}
