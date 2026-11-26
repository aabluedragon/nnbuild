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

#include "exhumed.h"
#include "anubis.h"
#include "aistuff.h"
#include "engine.h"
#include "runlist.h"
#include "sequence.h"
#include "move.h"
#include "bullet.h"
#include "random.h"
#include "items.h"
#include "object.h"
#include "sound.h"
#include "trigdat.h"
#include "save.h"
#include "kexbreak.h"   // NNBuild
#include "kexinv.h"
#include "anims.h"
#include <assert.h>

#define kMaxAnubis  80

struct Anubis
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short f;
    short g;
    short h;
};

Anubis AnubisList[kMaxAnubis];

static actionSeq ActionSeq[] = {
    { 0,  0 },
    { 8,  0 },
    { 16, 0 },
    { 24, 0 },
    { 32, 0 },
    { -1, 0 },
    { 46, 1 },
    { 46, 1 },
    { 47, 1 },
    { 49, 1 },
    { 49, 1 },
    { 40, 1 },
    { 42, 1 },
    { 41, 1 },
    { 43, 1 },
};

// The generated KEX ANUBIS.SEQ keeps the native live-action offsets, replaces
// their compressed range/pain groups with the authored timelines, and adds the
// otherwise-unrepresentable melee-miss group at slot 50.  The classic table
// above remains the fallback for stock data and old conversions.
static actionSeq KexAnubisActionSeq[] = {
    { 0,  0 },
    { 8,  0 },
    { 16, 0 },
    { 24, 0 },
    { 32, 0 },
    { 50, 0 },
    { 46, 1 },
    { 46, 1 },
    { 47, 1 },
    { 49, 1 },
    { 49, 1 },
    { 40, 1 },
    { 42, 1 },
    { 41, 1 },
    { 43, 1 },
};

static bool KexAnubisExactSequence()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqAnubis))
        return false;

    int const nBase = SeqOffsets[kSeqAnubis];
    return SeqSize[nBase] == 1
        && SeqSize[nBase + 8] == 18
        && SeqSize[nBase + 16] == 24
        && SeqSize[nBase + 24] == 25
        && SeqSize[nBase + 32] == 10
        && SeqSize[nBase + 40] == 20
        && SeqSize[nBase + 41] == 1
        && SeqSize[nBase + 42] == 20
        && SeqSize[nBase + 43] == 1
        && SeqSize[nBase + 50] == 9;
}

static actionSeq const &AnubisActionSequence(int nAction, bool bExactAnubis)
{
    return bExactAnubis ? KexAnubisActionSeq[nAction] : ActionSeq[nAction];
}

static bool KexAnubisShouldGib(int nRemainingHealth, int nDamageType)
{
    // kexSlaveActor::InflictDamage selects gibAnim for fire/explosion, or for
    // generic overkill strictly below -2-(maxHealth>>4). AI_Anubis has 100 HP,
    // so exactly -8 survives as a normal death and -9 gibs.
    return nDamageType == kKexDamageExplosion
        || nDamageType == kKexDamageFire
        || nRemainingHealth < KexHealthUnits(-2 - (100 >> 4));
}

int KexAnubisGibSelectorTest()
{
    int const threshold = KexHealthUnits(-2 - (100 >> 4));
    return !KexAnubisShouldGib(threshold, kKexDamageGeneric)
        && KexAnubisShouldGib(threshold - 1, kKexDamageGeneric)
        && KexAnubisShouldGib(0, kKexDamageExplosion)
        && KexAnubisShouldGib(0, kKexDamageFire)
        && !KexAnubisShouldGib(0, kKexDamageAcid);
}

short AnubisCount = 0;
short nAnubisDrum = 0;


void InitAnubis()
{
    AnubisCount = kMaxAnubis;
    nAnubisDrum = 1;
}

int BuildAnubis(int nSprite, int x, int y, int z, int nSector, int nAngle, uint8_t bIsDrummer)
{
    if (AnubisCount <= 0)
        return -1;
    short const nAnubis = --AnubisCount;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 101);
    }
    else
    {
        changespritestat(nSprite, 101);

        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sector[sprite[nSprite].sectnum].floorz;
        nAngle = sprite[nSprite].ang;
    }

    assert(nSprite >=0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].shade = -12;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].clipdist = g_kexMode ? 72 : 60;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].xrepeat = g_kexMode ? 60 : 40;
    sprite[nSprite].yrepeat = g_kexMode ? 60 : 40;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

//	GrabTimeSlot(3);

    if (bIsDrummer)
    {
        AnubisList[nAnubis].nAction = nAnubisDrum + 6;
        nAnubisDrum++;

        if (nAnubisDrum >= 5) {
            nAnubisDrum = 0;
        }
    }
    else
    {
        AnubisList[nAnubis].nAction = 0;
    }

    // AI_Anubis has 100 health in the KEX definitions; classic Anubis keeps
    // the native 540.
    AnubisList[nAnubis].nHealth = g_kexMode ? KexHealthUnits(100) : 540;
    AnubisList[nAnubis].nFrame  = 0;
    AnubisList[nAnubis].nSprite = nSprite;
    AnubisList[nAnubis].nTarget = -1;
    AnubisList[nAnubis].f = 0;
    AnubisList[nAnubis].g = 0;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nAnubis, kRunAnubis);

    AnubisList[nAnubis].h = runlist_AddRunRec(NewRun, nAnubis, kRunAnubis);
    nCreaturesLeft++;

    return nAnubis | 0x90000;
}

static void KexAnubisPlayDeathSound(short nSprite)
{
    int const sound = KexSound_Get(RandomBit() ? "k_andie2" : "anu_died");
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexAnubisTossGibs(short nSprite)
{
    // anubis_death tosses one gib1, two gib2, two gib3 and four gib4.
    static const int gibTiles[] = { 7808, 7809, 7809, 7810, 7810,
                                    7811, 7811, 7811, 7811 };

    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);
}

static void KexAnubisDestroy(short nAnubis, short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(AnubisList[nAnubis].h);
    sprite[nSprite].cstat = 0x8000;
    mydeletesprite(nSprite);
    AnubisList[nAnubis].nSprite = -1;
}

static void KexAnubisImmediateGib(short nAnubis, short nSprite, short nKiller)
{
    // Safe fallback for a conversion whose ANUBIS.SEQ predates the exact
    // normal-death timeline.
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 64,
        sprite[nSprite].xrepeat, nKiller);
    KexAnubisPlayDeathSound(nSprite);
    KexAnubisTossGibs(nSprite);
    KexAnubisDestroy(nAnubis, nSprite);
}

static void KexAnubisBeginGib(short nAnubis, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 64,
        sprite[nSprite].xrepeat, nKiller);
    KexAnubisPlayDeathSound(nSprite);
    KexAnubisTossGibs(nSprite);

    // A_GibDeath destroys the Anubis immediately but leaves Fx_Gibs01 alive
    // for 19 ticks; that generic effect calls A_RandomItem on tick 18. Keep an
    // invisible, save-serialized host until that callback has run.
    AnubisList[nAnubis].nAction = 15;
    AnubisList[nAnubis].nFrame = 0;
    AnubisList[nAnubis].f = 0;
    AnubisList[nAnubis].g = nKiller;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
}

int KexAnubisOwnsDeathDrop(int nSprite)
{
    if (!g_kexMode)
        return 0;

    for (int nAnubis = AnubisCount; nAnubis < kMaxAnubis; nAnubis++)
        if (AnubisList[nAnubis].nSprite == nSprite
            && (AnubisList[nAnubis].nAction == 11
                || AnubisList[nAnubis].nAction == 12
                || AnubisList[nAnubis].nAction == 15))
            return 1;
    return 0;
}

void FuncAnubis(int a, int nDamage, int nRun)
{
    short nAnubis = RunData[nRun].nVal;
    assert(nAnubis >= 0 && nAnubis < kMaxAnubis);

    short nSprite = AnubisList[nAnubis].nSprite;
    short nAction = AnubisList[nAnubis].nAction;

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Anubis\n", nMessage);
            return;
        }

        case 0x20000:
        {
            bool const bExactAnubis = KexAnubisExactSequence();

            // Invisible stand-in for Fx_Gibs01. Its sole gameplay callback is
            // A_RandomItem on tick 18, after which the effect destroys itself.
            if (g_kexMode && nAction == 15)
            {
                if (AnubisList[nAnubis].f >= 18)
                {
                    if (!seq_IsLoaded(kSeqKexGibDeath))
                        KexDrop_RandomItemAt(sprite[nSprite].sectnum,
                            sprite[nSprite].x, sprite[nSprite].y,
                            sprite[nSprite].z - 128 * 16 * 6,
                            AnubisList[nAnubis].g);
                    KexAnubisDestroy(nAnubis, nSprite);
                }
                else
                    AnubisList[nAnubis].f++;
                return;
            }

            if ((unsigned)nAction >= ARRAY_SIZE(ActionSeq))
                return;

            actionSeq const &action = AnubisActionSequence(nAction,
                                                            bExactAnubis);
            short nSeq = SeqOffsets[kSeqAnubis] + action.a;
            if (AnubisList[nAnubis].nFrame < 0
                || AnubisList[nAnubis].nFrame >= SeqSize[nSeq])
                AnubisList[nAnubis].nFrame = 0;
            short const nRenderedFrame = AnubisList[nAnubis].nFrame;

            bool const bExactDeath = bExactAnubis
                && (nAction == 11 || nAction == 12);
            if (bExactDeath && nRenderedFrame == 2)
            {
                // anubis_death_pc frame 1: A_ThrustZVelocity(6). KEX Z points
                // upward; Build Z points downward and is scaled by 96.
                sprite[nSprite].zvel = -(6 * 16 * 6);
            }

            if (nAction < 11 || bExactDeath) {
                Gravity(nSprite);
            }

            // The stock melee carries DOS sound events. KEX's 24-frame attack
            // has its own callbacks below. The exact range, pain and death
            // groups are callback-free too; their controller owns each event.
            bool const controllerOwnsCallbacks = g_kexMode
                && (nAction == 2
                    || (bExactAnubis
                        && (nAction == 3 || nAction == 4
                            || nAction == 11 || nAction == 12)));
            if (!controllerOwnsCallbacks)
                seq_MoveSequence(nSprite, nSeq, nRenderedFrame);

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nRenderedFrame);

            AnubisList[nAnubis].nFrame++;
            if (AnubisList[nAnubis].nFrame >= SeqSize[nSeq])
            {
                AnubisList[nAnubis].nFrame = 0;
                bVal = true;
            }

            short nTarget = AnubisList[nAnubis].nTarget;

            short nFrame = SeqBase[nSeq] + AnubisList[nAnubis].nFrame;
            short nFlag = FrameFlag[nFrame];

            int nMov = 0;

            if ((nAction > 0 && nAction < 11) || bExactDeath) {
                nMov = MoveCreatureWithCaution(nSprite);
            }

            switch (nAction)
            {
                case 0:
                {
                    if ((nAnubis & 0x1F) == (totalmoves & 0x1F))
                    {
                        if (nTarget < 0) {
                            nTarget = FindPlayer(nSprite, 100);
                        }

                        if (nTarget >= 0)
                        {
                            D3PlayFX(StaticSound[kSoundAnubisICU], nSprite);
                            AnubisList[nAnubis].nAction = 1;
                            AnubisList[nAnubis].nFrame = 0;
                            AnubisList[nAnubis].nTarget = nTarget;

                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        }
                    }
                    return;
                }
                case 1:
                {
                    if (g_kexMode && KexAIEnterMeleeRange(nSprite, nTarget, 0))
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        AnubisList[nAnubis].nAction = 2;
                        AnubisList[nAnubis].nFrame = 0;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        break;
                    }

                    if ((nAnubis & 0x1F) == (totalmoves & 0x1F))
                    {
                        PlotCourseToSprite(nSprite, nTarget);

                        int nAngle = sprite[nSprite].ang & 0xFFF8;
                        sprite[nSprite].xvel = Cos(nAngle) >> 2;
                        sprite[nSprite].yvel = Sin(nAngle) >> 2;
                    }

                    switch (nMov & 0xC000)
                    {
                        case 0xC000:
                        {
                            if ((nMov & 0x3FFF) == nTarget)
                            {
                                int nAng = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                                int nAngDiff = AngleDiff(sprite[nSprite].ang, nAng);

                                if (nAngDiff < 64)
                                {
                                    AnubisList[nAnubis].nAction = 2;
                                    AnubisList[nAnubis].nFrame = 0;
                                }
                                break;
                            }
                            // else we fall through to 0x8000
                            fallthrough__;
                        }
                        case 0x8000:
                        {
                            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                            break;
                        }

                        default:
                        {
                            if (AnubisList[nAnubis].g)
                            {
                                AnubisList[nAnubis].g--;
                            }
                            else
                            {
                                if (!g_kexMode)
                                    AnubisList[nAnubis].g = 60;

                                if (nTarget > -1) // NOTE: nTarget can be -1. this check wasn't in original code. TODO: demo compatiblity?
                                {
                                    if (cansee(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z - GetSpriteHeight(nSprite), sprite[nSprite].sectnum,
                                        sprite[nTarget].x, sprite[nTarget].y, sprite[nTarget].z - GetSpriteHeight(nTarget), sprite[nTarget].sectnum))
                                    {
                                        sprite[nSprite].xvel = 0;
                                        sprite[nSprite].yvel = 0;
                                        sprite[nSprite].ang = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);

                                        AnubisList[nAnubis].nAction = 3;
                                        AnubisList[nAnubis].nFrame = 0;
                                        if (g_kexMode)
                                            AnubisList[nAnubis].g = KexDifficulty_AICooldown();
                                    }
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case 2:
                {
                    if (nTarget == -1)
                    {
                        AnubisList[nAnubis].nAction = 0;
                        AnubisList[nAnubis].g = 50;
                    }
                    else
                    {
                        if (g_kexMode)
                        {
                            PlotCourseToSprite(nSprite, nTarget);

                            // Frames 4/17 run A_CheckMelee(..., 0). A miss
                            // leaves this animation before the following slap.
                            if ((nRenderedFrame == 4 || nRenderedFrame == 17)
                                && !KexMeleeRange(nSprite, nTarget, 0))
                            {
                                AnubisList[nAnubis].nAction = bExactAnubis
                                    ? 5 : 1;
                                AnubisList[nAnubis].nFrame = 0;
                                if (bExactAnubis)
                                {
                                    sprite[nSprite].xvel = 0;
                                    sprite[nSprite].yvel = 0;
                                }
                                else
                                {
                                    sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                                    sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                                }
                                break;
                            }

                            if (nRenderedFrame == 3 || nRenderedFrame == 16)
                            {
                                int const s = KexSound_Get("k_anmele");
                                if (s >= 0) D3PlayFX(s, nSprite);
                            }
                            if (nRenderedFrame == 6 || nRenderedFrame == 19)
                            {
                                int const s = KexSound_Get("k_anslap");
                                if (s >= 0) D3PlayFX(s, nSprite);
                                if (KexMeleeAttackRange(nSprite, nTarget, 48))
                                    runlist_DamageEnemy(nTarget, nSprite, 20);
                            }
                        }
                        else if (PlotCourseToSprite(nSprite, nTarget) >= 768)
                            AnubisList[nAnubis].nAction = 1;
                        else if (nFlag & 0x80)
                            runlist_DamageEnemy(nTarget, nSprite, 7);
                    }

                    break;
                }
                case 3:
                {
                    if (bExactAnubis)
                    {
                        if (nRenderedFrame == 0 || nRenderedFrame == 3
                            || nRenderedFrame == 8)
                            PlotCourseToSprite(nSprite, nTarget);

                        if (nRenderedFrame == 8)
                        {
                            int const sound = KexSound_Get("anu_fire");
                            if (sound >= 0)
                                D3PlayFX(sound, nSprite);
                            BuildKexAIFireProjectile(nSprite, 8, nTarget,
                                                     4, 96, 16, 0);
                        }

                        if (bVal)
                        {
                            AnubisList[nAnubis].nAction = 1;
                            AnubisList[nAnubis].nFrame = 0;
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        }
                        return;
                    }

                    if (bVal)
                    {
                        AnubisList[nAnubis].nAction = 1;

                        sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                        sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        AnubisList[nAnubis].nFrame = 0;
                    }
                    else
                    {
                        // loc_25718:
                        if (nFlag & 0x80)
                        {
                            if (g_kexMode)
                                BuildKexAIFireProjectile(nSprite, 8, nTarget,
                                                         4, 96, 16, 0);
                            else
                                BuildBullet(nSprite, 8, 0, 0, -1,
                                            sprite[nSprite].ang,
                                            nTarget + 10000, 1);
                        }
                    }

                    return;
                }
                case 4:
                case 5:
                {
                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;

                    if (bExactAnubis && nAction == 4
                        && nRenderedFrame == 4)
                    {
                        int const sound = KexSound_Get("anu_hit");
                        if (sound >= 0)
                            D3PlayFX(sound, nSprite);
                    }

                    if (bVal)
                    {
                        AnubisList[nAnubis].nAction = 1;
                        AnubisList[nAnubis].nFrame = 0;
                        if (g_kexMode)
                        {
                            sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> 2;
                            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
                        }
                    }
                    return;
                }
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                {
                    if (bVal)
                    {
                        AnubisList[nAnubis].nAction = (RandomSize(3) % 5) + 6;
                        AnubisList[nAnubis].nFrame = 0;
                    }
                    return;
                }
                case 11:
                case 12:
                {
                    if (bExactAnubis)
                    {
                        if (nRenderedFrame == 0)
                            KexAnubisPlayDeathSound(nSprite);
                        if (nRenderedFrame == 10)
                            sprite[nSprite].cstat &= ~0x1; // A_Die
                        if (nRenderedFrame == 19)
                            KexDrop_RandomItem(nSprite,
                                               AnubisList[nAnubis].g);
                    }

                    if (bVal)
                    {
                        AnubisList[nAnubis].nAction = nAction + 2;
                        AnubisList[nAnubis].nFrame = 0;

                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                    }
                    return;
                }
                case 13:
                case 14:
                {
                    sprite[nSprite].cstat &= 0xFEFE;
                    return;
                }

                default:
                    return;
            }

            // loc_2564C:
            if (nAction && nTarget != -1)
            {
                if (!(sprite[nTarget].cstat & 0x101))
                {
                    AnubisList[nAnubis].nAction = 0;
                    AnubisList[nAnubis].nFrame = 0;
                    AnubisList[nAnubis].g = 100;
                    AnubisList[nAnubis].nTarget = -1;

                    sprite[nSprite].xvel = 0;
                    sprite[nSprite].yvel = 0;
                }
            }

            return;
        }

        case 0x90000:
        {
            if (nAction == 15 || (unsigned)nAction >= ARRAY_SIZE(ActionSeq))
                return;
            bool const bExactAnubis = KexAnubisExactSequence();
            actionSeq const &action = AnubisActionSequence(nAction,
                                                            bExactAnubis);
            int const nSeq = SeqOffsets[kSeqAnubis] + action.a;
            if (AnubisList[nAnubis].nFrame < 0
                || AnubisList[nAnubis].nFrame >= SeqSize[nSeq])
                AnubisList[nAnubis].nFrame = 0;
            seq_PlotSequence(a & 0xFFFF, nSeq,
                             AnubisList[nAnubis].nFrame, action.b);
            break;
        }

        case 0xA0000: // fall through to next case
        {
            if (nAction >= 11) {
                return;
            }

            nDamage = runlist_CheckRadialDamage(nSprite);
            fallthrough__;
        }
        case 0x80000:
        {
            if (nDamage)
            {
                if (AnubisList[nAnubis].nHealth <= 0)
                    return;

                AnubisList[nAnubis].nHealth -= nDamage;

                if (AnubisList[nAnubis].nHealth > 0)
                {
                    short nTarget = a & 0xFFFF;

                    // loc_258D6:
                    if (nTarget < 0 && !g_kexMode) {
                        return;
                    }

                    if (nTarget >= 0
                        && (sprite[nTarget].statnum == 100 || sprite[nTarget].statnum < 199))
                    {
                        if (g_kexMode || !RandomSize(5)) {
                            AnubisList[nAnubis].nTarget = nTarget;
                        }
                    }

                    bool const bPainRoll = g_kexMode
                        ? KexDifficulty_PainRoll(64) : RandomSize(1) != 0;
                    if (bPainRoll && (!g_kexMode || nAction == 1))
                    {
                        if (nAction >= 6 && nAction <= 10)
                        {
                            int nDrumSprite = insertsprite(sprite[nSprite].sectnum, kStatAnubisDrum);

                            sprite[nDrumSprite].x = sprite[nSprite].x;
                            sprite[nDrumSprite].y = sprite[nSprite].y;
                            sprite[nDrumSprite].z = sector[sprite[nDrumSprite].sectnum].floorz;
                            sprite[nDrumSprite].xrepeat = 40;
                            sprite[nDrumSprite].yrepeat = 40;
                            sprite[nDrumSprite].shade = -64;

                            BuildObject(nDrumSprite, 2, 0);
                        }

                        AnubisList[nAnubis].nAction = 4;
                        AnubisList[nAnubis].nFrame = 0;
                    }
                    else if (!g_kexMode)
                    {
                        // loc_259B5:
                        D3PlayFX(StaticSound[kSoundAnubisHit], nSprite);
                    }
                }
                else
                {
                    int const nRemainingHealth = AnubisList[nAnubis].nHealth;
                    short const nKiller = nMessage == 0xA0000
                        ? (short)nRadialOwner : (short)(a & 0xFFFF);
                    int const nDamageType = nMessage == 0xA0000
                        ? kKexDamageExplosion : nKexDamageType;
                    AnubisList[nAnubis].nHealth = 0;
                    nCreaturesLeft--;

                    if (nAction < 11)
                    {
                        if (KexAnubisExactSequence())
                        {
                            // Radial dispatch bypasses runlist_DamageEnemy's
                            // post-death hook; direct hits are reported there
                            // after this synchronous callback returns.
                            if (nMessage == 0xA0000)
                                KexTrigger_Died(nSprite);

                            if (KexAnubisShouldGib(nRemainingHealth,
                                                   nDamageType))
                            {
                                KexAnubisBeginGib(nAnubis, nSprite, nKiller);
                            }
                            else
                            {
                                // InflictDamage clears shootable immediately.
                                // A_Die clears solid on authored death tick 10.
                                sprite[nSprite].cstat &= ~0x100;
                                AnubisList[nAnubis].nAction = 11;
                                AnubisList[nAnubis].nFrame = 0;
                                AnubisList[nAnubis].f = 0;
                                AnubisList[nAnubis].g = nKiller;
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                                sprite[nSprite].zvel = 0;
                            }
                            return;
                        }

                        // Stock/classic cadence and old KEX conversions keep
                        // the pre-existing immediate-death fallback.
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].zvel = 0;
                        sprite[nSprite].z = sector[sprite[nSprite].sectnum].floorz;
                        sprite[nSprite].cstat &= 0xFEFE;
                        DropMagic(nSprite);
                        if (g_kexMode)
                        {
                            if (nMessage == 0xA0000)
                            {
                                KexDrop_RandomItem(nSprite, nKiller);
                                KexTrigger_Died(nSprite);
                            }
                            KexAnubisImmediateGib(nAnubis, nSprite, nKiller);
                            return;
                        }
                        AnubisList[nAnubis].nAction = (nMessage == 0xA0000) + 11;
                        AnubisList[nAnubis].nFrame = 0;
                    }
                }
            }

            return;
        }
    }
}

class AnubisLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void AnubisLoadSave::Load()
{
    Read(&AnubisCount, sizeof(AnubisCount));
    Read(&nAnubisDrum, sizeof(nAnubisDrum));
    Read(&AnubisList[AnubisCount], sizeof(Anubis) * (kMaxAnubis - AnubisCount));
}

void AnubisLoadSave::Save()
{
    Write(&AnubisCount, sizeof(AnubisCount));
    Write(&nAnubisDrum, sizeof(nAnubisDrum));
    Write(&AnubisList[AnubisCount], sizeof(Anubis) * (kMaxAnubis - AnubisCount));
}

static AnubisLoadSave* myLoadSave;

void AnubisLoadSaveConstruct()
{
    myLoadSave = new AnubisLoadSave();
}
