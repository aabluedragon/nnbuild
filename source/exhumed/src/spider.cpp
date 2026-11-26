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

#include "spider.h"
#include "exhumed.h"
#include "engine.h"
#include "runlist.h"
#include "move.h"
#include "sequence.h"
#include "random.h"
#include "sound.h"
#include "kexbreak.h" // NNBuild: KEX death sound
#include "kexinv.h"
#include "anims.h"
#include "trigdat.h"
#include <assert.h>
#include <math.h>

#include "save.h"

short SpiderCount = 0;

#define kMaxSpiders 100

struct Spider
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short nRun;
};

Spider SpiderList[kMaxSpiders];

static actionSeq ActionSeq[] = {
    {16, 0},
    {8,  0},
    {32, 0},
    {24, 0},
    {0,  0},
    {40, 1},
    {41, 1}
};


void InitSpider()
{
    SpiderCount = 0;
}

int GetSpiderSprite(int nSpider)
{
    assert (nSpider >= 0 && nSpider < kMaxSpiders);
    return SpiderList[nSpider].nSprite;
}

// NNBuild: the remaster's scorpion is this AI in a scorpion's shell.
static uint8_t s_spiderScorp[kMaxSpiders];
static int const kKexExactScorpTileBase = 20900;
static int const kKexExactScorpTileCount = 48;
static int s_spiderLeapTest = -1;

// kexSlaveAI is simulated at 60 Hz.  The original Spider record gives us no
// spare saved fields, so hitag carries the two one-bit remainders lost when
// the first on-floor .5-friction step halves its KEX launch velocity.  It is
// otherwise only the KEX attack cooldown and is restored when landing.
static constexpr short kKexSpiderLeapMarker = 0x4000;
static constexpr int kKexSpiderWorld = 6;
static constexpr int kKexSpiderZWorld = 16 * kKexSpiderWorld;
static constexpr int kKexSpiderLeapZ = 15 * kKexSpiderZWorld;
static constexpr int kKexSpiderGravity = (3 * kKexSpiderZWorld) / 4;

static bool KexSpiderExactSequence(short nSpider)
{
    if (!g_kexMode)
        return false;

    bool const scorpion = s_spiderScorp[nSpider] != 0;
    int const nFile = scorpion ? kSeqScorpion : kSeqSpider;
    if (!seq_IsLoaded(nFile))
        return false;

    int const nBase = SeqOffsets[nFile];
    return SeqSize[nBase + 8] == (scorpion ? 12 : 6)
        && SeqSize[nBase + 24] == (scorpion ? 2 : 3)
        && SeqSize[nBase + 32] == (scorpion ? 13 : 14)
        && SeqSize[nBase + 40] == 4
        && SeqSize[nBase + 41] == 1;
}

static void KexSpiderPushAway(short nSprite, short nTarget)
{
    if (!g_kexMode || (unsigned)nTarget >= (unsigned)kMaxSprites
        || sprite[nTarget].sectnum < 0 || !(sprite[nTarget].cstat & 0x101))
        return;

    int64_t const dx = (int)sprite[nTarget].x - sprite[nSprite].x;
    int64_t const dy = (int)sprite[nTarget].y - sprite[nSprite].y;
    // A_SpiderPushAway applies to the live current target, including an
    // in-fighting target. It uses each actor's GetRadius(), not collision
    // bounds. The player puppet's 95.25-unit radius is therefore much larger
    // than its legacy Build clipdist; use the shared doubled-WORLD helper.
    int64_t const radius = (int64_t)KexContactRadius2(nSprite)
        + KexContactRadius2(nTarget) + 4 * kKexSpiderWorld * 2;
    int64_t const dx2 = dx * 2;
    int64_t const dy2 = dy * 2;
    if (dx2 * dx2 + dy2 * dy2 > radius * radius || (dx == 0 && dy == 0))
        return;

    int const nAngle = getangle((int)-dx, (int)-dy);
    // A_SpiderPushAway uses the authored four-unit KEX impulse.
    sprite[nSprite].xvel = (Cos(nAngle) * (4 * kKexSpiderWorld)) >> 8;
    sprite[nSprite].yvel = (Sin(nAngle) * (4 * kKexSpiderWorld)) >> 8;
}

static void KexSpiderLeapVelocityFromDelta(int64_t dx, int64_t dy,
                                             int64_t dz, short &xvel,
                                             short &yvel, short &zvel)
{
    // A_SpiderLeap normalizes the full three-dimensional target vector for
    // its XY component, but always writes a separate +15 KEX-Z launch.
    // Here X/Y already use six Build units per KEX unit; Z is brought to that
    // same basis by dividing its 16x height scale.
    double const length = sqrt((double)dx * dx + (double)dy * dy
                               + (double)dz * dz);
    if (length <= 0)
    {
        xvel = yvel = zvel = 0;
        return;
    }

    xvel = (short)((double)dx * (4 * kKexSpiderWorld * 256) / length);
    yvel = (short)((double)dy * (4 * kKexSpiderWorld * 256) / length);
    zvel = -kKexSpiderLeapZ; // KEX is up-positive, Build is down-positive.
}

static void KexSpiderStoreInitialLeapRemainders(short nSprite)
{
    int const xHalf = sprite[nSprite].xvel / 2;
    int const yHalf = sprite[nSprite].yvel / 2;
    int const xLost = sprite[nSprite].xvel - xHalf * 2;
    int const yLost = sprite[nSprite].yvel - yHalf * 2;
    sprite[nSprite].hitag = kKexSpiderLeapMarker | (xLost + 1)
                         | ((yLost + 1) << 2);
}

static bool KexSpiderHasLeapState(short nSprite)
{
    return (sprite[nSprite].hitag & kKexSpiderLeapMarker) != 0;
}

static int KexSpiderRestoreInitialLaunch(int halfVelocity, int bits)
{
    return halfVelocity * 2 + bits - 1;
}

static int KexSpiderFloorZ(short nSprite)
{
    return getflorzofslope(sprite[nSprite].sectnum,
                            sprite[nSprite].x, sprite[nSprite].y);
}

static bool KexSpiderGroundCallback(short nSprite)
{
    // A_SpiderCheckGround/RiseCheckGround test source origin.z + velocity.z
    // against floor + 1.0. Convert KEX up-positive Z to Build down-positive.
    return (int64_t)sprite[nSprite].z + sprite[nSprite].zvel
        >= (int64_t)KexSpiderFloorZ(nSprite) - kKexSpiderZWorld;
}

static void KexSpiderSourcePhysicsStep(short nSprite)
{
    // kexActor::UpdateVelocity() applies .5 XY friction while grounded,
    // otherwise subtracts the default .75 KEX gravity before moving. This is
    // one of the two source 60-Hz steps represented by an Exhumed update.
    if (sprite[nSprite].z < KexSpiderFloorZ(nSprite))
        sprite[nSprite].zvel += kKexSpiderGravity;
    else
    {
        sprite[nSprite].xvel /= 2;
        sprite[nSprite].yvel /= 2;
    }

    // The bespoke launch velocities are authored action output, not AI
    // moveSpeed, so Pharaoh's kexSlaveAI moveSpeed multiplier must not touch
    // them. The native spider's existing collision envelope remains in use.
    movesprite(nSprite, sprite[nSprite].xvel << 8,
               sprite[nSprite].yvel << 8, sprite[nSprite].zvel,
               1280, -1280, CLIPMASK0);
}

static void KexSpiderFinishLeap(short nSpider, short nSprite)
{
    SpiderList[nSpider].nAction = 1;
    SpiderList[nSpider].nFrame = 0;
    sprite[nSprite].hitag = KexDifficulty_AICooldown();
}

static void KexSpiderLeapTick(short nSpider, short nSprite, short nTarget)
{
    if (!KexSpiderHasLeapState(nSprite))
    {
        // A save made by an older bridge can carry the old DOS launch in
        // action 3. Do not continue that unsafe velocity after upgrading.
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        KexSpiderFinishLeap(nSpider, nSprite);
        return;
    }

    int const phase = SpiderList[nSpider].nFrame; // 0 launch, 1 rise, 2 fall

    // Source leap frame -> first source physics tick. The action's goto then
    // enters *_check_ground, whose callback runs before the second tick.
    KexSpiderSourcePhysicsStep(nSprite);

    int scratchX = sprite[nSprite].xvel;
    int scratchY = sprite[nSprite].yvel;
    if (phase == 0)
    {
        scratchX = KexSpiderRestoreInitialLaunch(scratchX,
                                                  sprite[nSprite].hitag & 3);
        scratchY = KexSpiderRestoreInitialLaunch(scratchY,
                                                  (sprite[nSprite].hitag >> 2) & 3);
    }

    // Both source ground-check callbacks begin with A_SpiderPushAway.
    KexSpiderPushAway(nSprite, nTarget);
    if (KexSpiderGroundCallback(nSprite))
    {
        // ChangeAnim(base) occurs before KEX's second source movement tick.
        // It retains a just-written push-away vector, exactly as the script.
        KexSpiderFinishLeap(nSpider, nSprite);
    }
    else if (!s_spiderScorp[nSpider] && sprite[nSprite].zvel >= 0)
    {
        // Spider (unlike Scorpion) changes to falling_check_ground as soon as
        // the source up-positive velocity reaches zero. Its callback retains
        // the push-away velocity rather than restoring leap scratch values.
        SpiderList[nSpider].nFrame = 2;
    }
    else
    {
        // A_SpiderRiseCheckGround restores its saved launch XY every callback
        // after the push-away test. Scorpion remains in this rising callback
        // while descending as well.
        sprite[nSprite].xvel = scratchX;
        sprite[nSprite].yvel = scratchY;
        SpiderList[nSpider].nFrame = 1;
    }

    KexSpiderSourcePhysicsStep(nSprite);
}

void KexSpiderLeap_TestTick()
{
    if (s_spiderLeapTest < 0)
        s_spiderLeapTest = g_kexMode && Bgetenv("NNB_KEX_SPIDER_LEAP_TEST") ? 1 : 0;
    if (!s_spiderLeapTest)
        return;
    s_spiderLeapTest = 0;

    short xvel, yvel, zvel;
    // Source vector (3,4,12) has length 13; X/Y get the normalized *4
    // impulse while Z remains the authored constant 15-unit jump.
    KexSpiderLeapVelocityFromDelta(18, 24, 72, xvel, yvel, zvel);
    bool const launch = xvel == 1417 && yvel == 1890 && zvel == -kKexSpiderLeapZ;
    bool const firstFriction = KexSpiderRestoreInitialLaunch(xvel / 2,
        (xvel - (xvel / 2) * 2) + 1) == xvel;
    int const floor = 10000;
    bool const floorGate = !((floor - 200) + 48 >= floor - kKexSpiderZWorld)
        && ((floor - 100) + 48 >= floor - kKexSpiderZWorld);

    // Probe the script's GetRadius()-based push-away without allocating a
    // controller. The exact player boundary is 16 + 95.25 + 4 KEX units,
    // or 691.5 Build units; the same callback must also retain in-fighting
    // targets instead of silently limiting itself to players.
    short const nSource = insertsprite(0, 0);
    short const nTarget = insertsprite(0, 0);
    bool pushAway = nSource >= 0 && nTarget >= 0;
    if (pushAway)
    {
        sprite[nSource].x = sprite[nSource].y = 0;
        sprite[nSource].clipdist = 24;
        sprite[nSource].cstat = 0x101;
        sprite[nTarget].y = 0;
        sprite[nTarget].clipdist = 1; // player radius comes from KEX, not clipdist
        sprite[nTarget].cstat = 0x101;
        changespritestat(nTarget, 100);
        sprite[nTarget].x = 691;
        KexSpiderPushAway(nSource, nTarget);
        pushAway = sprite[nSource].xvel != 0 || sprite[nSource].yvel != 0;
        sprite[nSource].xvel = sprite[nSource].yvel = 0;
        sprite[nTarget].x = 692;
        KexSpiderPushAway(nSource, nTarget);
        pushAway = pushAway && sprite[nSource].xvel == 0 && sprite[nSource].yvel == 0;

        changespritestat(nTarget, 99);
        sprite[nTarget].clipdist = 24;
        sprite[nTarget].x = 216; // 16 + 16 + 4 source units at WORLD=6
        KexSpiderPushAway(nSource, nTarget);
        pushAway = pushAway && (sprite[nSource].xvel != 0 || sprite[nSource].yvel != 0);
    }
    if (nSource >= 0)
        mydeletesprite(nSource);
    if (nTarget >= 0)
        mydeletesprite(nTarget);

    OSD_Printf("Kex Spider leap test: %s (3D XY launch, +15 Z, 60 Hz .5/.75 "
               "physics, check-ground, authored target push-away)\n",
               launch && firstFriction && floorGate && pushAway ? "PASS" : "FAIL");
}

static void StartSpiderLeap(short nSpider, short nSprite)
{
    if (g_kexMode && KexSpiderExactSequence(nSpider))
    {
        short const nTarget = SpiderList[nSpider].nTarget;
        SpiderList[nSpider].nAction = 3;
        SpiderList[nSpider].nFrame = 0;

        if (nTarget >= 0 && nTarget < kMaxSprites
            && sprite[nTarget].sectnum >= 0)
        {
            int64_t const dx = (int)sprite[nTarget].x - sprite[nSprite].x;
            int64_t const dy = (int)sprite[nTarget].y - sprite[nSprite].y;
            int64_t const dz = ((int)sprite[nSprite].z - sprite[nTarget].z) / 16;
            short xvel, yvel, zvel;
            KexSpiderLeapVelocityFromDelta(dx, dy, dz, xvel, yvel, zvel);
            sprite[nSprite].xvel = xvel;
            sprite[nSprite].yvel = yvel;
            sprite[nSprite].zvel = zvel;
            KexSpiderStoreInitialLeapRemainders(nSprite);
            int const sound = KexSound_Get("spi_jump");
            if (sound >= 0)
                D3PlayFX(sound, nSprite);
        }
        else
        {
            sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
            KexSpiderStoreInitialLeapRemainders(nSprite);
        }
        return;
    }

    short const nSector = sprite[nSprite].sectnum;
    if (sprite[nSprite].cstat & 8)
    {
        sprite[nSprite].cstat ^= 8;
        sprite[nSprite].zvel = 1;
        sprite[nSprite].z = sector[nSector].ceilingz + GetSpriteHeight(nSprite);
    }
    else
    {
        sprite[nSprite].zvel = -5120;
    }

    SpiderList[nSpider].nAction = 3;
    SpiderList[nSpider].nFrame = 0;

    if (g_kexMode)
        sprite[nSprite].hitag = KexDifficulty_AICooldown();
    else if (!RandomSize(3))
        D3PlayFX(StaticSound[kSoundSpiderJump], nSprite);
}

static void KexSpiderTossGibs(short nSpider, short nSprite)
{
    static const int spiderGibs[] = { 7828, 7829, 7830, 7831, 7832, 7833, 7834, 7835, 7836 };
    static const int scorpGibs[] = { 7837, 7838, 7838, 7838, 7839, 7839, 7839, 7840, 7840 };
    int const *gibTiles = s_spiderScorp[nSpider] ? scorpGibs : spiderGibs;
    size_t const nGibs = s_spiderScorp[nSpider] ? ARRAY_SIZE(scorpGibs) : ARRAY_SIZE(spiderGibs);

    int const sound = KexSound_Get("spi_newd");
    if (sound >= 0)
        PlayLocalSound(sound, 0);

    for (size_t i = 0; i < nGibs; i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);
}

static void KexSpiderDestroy(short nSpider, short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(SpiderList[nSpider].nRun);
    sprite[nSprite].cstat = 0x8000;
    mydeletesprite(nSprite);
    SpiderList[nSpider].nSprite = -1;
}

static void KexSpiderImmediateDeath(short nSpider, short nSprite, short nKiller)
{
    // Compatibility fallback for an old conversion whose death slot is still
    // the one-frame safe pose rather than the authored four-tick timeline.
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);
    KexSpiderTossGibs(nSpider, nSprite);
    KexSpiderDestroy(nSpider, nSprite);
}

static void KexSpiderBeginGibTimer(short nSpider, short nSprite)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, SpiderList[nSpider].nTarget);
    KexSpiderTossGibs(nSpider, nSprite);

    // A_GibDeath destroys the host on death tick 3, but Fx_Gibs01 survives for
    // 19 more ticks and runs A_RandomItem on tick 18. Action 6 is an invisible,
    // save-serialized stand-in for that generic effect.
    SpiderList[nSpider].nAction = 6;
    SpiderList[nSpider].nFrame = 0;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].cstat = 0x8000;
}

int KexSpiderOwnsDeathDrop(int nSprite)
{
    if (!g_kexMode)
        return 0;

    for (int nSpider = 0; nSpider < SpiderCount; nSpider++)
        if (SpiderList[nSpider].nSprite == nSprite
            && (SpiderList[nSpider].nAction == 5
                || SpiderList[nSpider].nAction == 6))
            return 1;
    return 0;
}

int KexSpiderIdentityTest()
{
    if (!g_kexMode)
        return 1;

    for (int nSpider = 0; nSpider < SpiderCount; nSpider++)
    {
        int const nSprite = SpiderList[nSpider].nSprite;
        if ((unsigned)nSprite >= (unsigned)kMaxSprites
            || sprite[nSprite].statnum != 99)
            continue;
        if ((sprite[nSprite].extra == 1) != (s_spiderScorp[nSpider] != 0))
            return 0;
    }
    return 1;
}

int BuildSpider(int nSprite, int x, int y, int z, short nSector, int nAngle, int bScorp)
{
    int nSpider = SpiderCount++;
    if (nSpider >= kMaxSpiders) {
        return -1;
    }

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 99);
    }
    else
    {
        changespritestat(nSprite, 99);
        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sprite[nSprite].z;
        nAngle = sprite[nSprite].ang;
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].shade = -12;
    // Both retail AI_Spider and AI_Scorp use radius 16. They share this
    // controller and therefore the same Build-space clipdist in KEX mode.
    sprite[nSprite].clipdist = g_kexMode ? 24 : 15;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].xrepeat = 40;
    sprite[nSprite].yrepeat = 40;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

    //	GrabTimeSlot(3);

    SpiderList[nSpider].nAction = 0;
    SpiderList[nSpider].nFrame = 0;
    SpiderList[nSpider].nSprite = nSprite;
    SpiderList[nSpider].nTarget = -1;
    // Both KEX AI_Spider and AI_Scorp use 20 health.  The native Build spider
    // remains at 160 outside KEX mode.
    SpiderList[nSpider].nHealth = g_kexMode ? KexHealthUnits(20) : 160;
    s_spiderScorp[nSpider] = (uint8_t)(bScorp != 0 && seq_IsLoaded(kSeqScorpion));
    // Sprite state is serialized independently of this controller struct.
    // Persist the skin identity there so a cold save load can reconstruct the
    // side array without changing the classic Spider save-record size.
    if (g_kexMode)
        sprite[nSprite].extra = s_spiderScorp[nSpider] ? 1 : 0;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nSpider, kRunSpider);

    SpiderList[nSpider].nRun = runlist_AddRunRec(NewRun, nSpider, kRunSpider);

    nCreaturesLeft++;

    return nSpider;
}

void FuncSpider(int a, int nDamage, int nRun)
{
    short nSpider = RunData[nRun].nVal;
    assert(nSpider >= 0 && nSpider < kMaxSpiders);

    int nVel = 0;

    short nSprite = SpiderList[nSpider].nSprite;
    short nAction = SpiderList[nSpider].nAction;
    if ((unsigned)nAction >= ARRAY_SIZE(ActionSeq))
    {
        nAction = SpiderList[nSpider].nAction = 0;
        SpiderList[nSpider].nFrame = 0;
    }

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Spider\n", nMessage);
            break;
        }

        case 0x20000:
        {
            bool const bExactSpider = KexSpiderExactSequence(nSpider);

            if (g_kexMode && bExactSpider && nAction == 6)
            {
                if (SpiderList[nSpider].nFrame >= 18)
                {
                    if (!seq_IsLoaded(kSeqKexGibDeath))
                        KexDrop_RandomItemAt(sprite[nSprite].sectnum,
                            sprite[nSprite].x, sprite[nSprite].y,
                            sprite[nSprite].z - 64 * 16 * 6,
                            SpiderList[nSpider].nTarget);
                    KexSpiderDestroy(nSpider, nSprite);
                }
                else
                    SpiderList[nSpider].nFrame++;
                return;
            }

            nVel = 6;

            bool const bExactLeap = g_kexMode && bExactSpider && nAction == 3;
            if (SpiderList[nSpider].nHealth && !bExactLeap)
            {
                if (sprite[nSprite].cstat & 8)
                {
                    sprite[nSprite].z = sector[sprite[nSprite].sectnum].ceilingz + GetSpriteHeight(nSprite);
                }
                else
                {
                    Gravity(nSprite);
                }
            }

            int nSeq = SeqOffsets[s_spiderScorp[nSpider] ? kSeqScorpion : kSeqSpider] + ActionSeq[nAction].a;
            // A save made with the exact loose KEX sequence may carry a frame
            // beyond the stock/fallback group's shorter cadence.  Clamp before
            // touching sequence arrays so removing the converted data remains
            // safe and classic saves retain their existing values.
            if (SpiderList[nSpider].nFrame < 0
                || SpiderList[nSpider].nFrame >= SeqSize[nSeq])
                SpiderList[nSpider].nFrame = 0;
            short const nRenderedFrame = SpiderList[nSpider].nFrame;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, nRenderedFrame);

            // KEX has one bite callback, not the DOS sequence's three flags.
            // Converted data uses all 14/13 authored frames; stock-layout data
            // remains a safe compressed fallback for an old conversion.
            if (!(g_kexMode && nAction == 2))
                seq_MoveSequence(nSprite, nSeq, nRenderedFrame);

            int nFrameFlag = FrameFlag[SeqBase[nSeq] + nRenderedFrame];

            short nTarget = SpiderList[nSpider].nTarget;

            if (bExactLeap)
            {
                KexSpiderLeapTick(nSpider, nSprite, nTarget);
                return;
            }

            SpiderList[nSpider].nFrame++;
            if (SpiderList[nSpider].nFrame >= SeqSize[nSeq]) {
                SpiderList[nSpider].nFrame = 0;
            }

            if (g_kexMode && bExactSpider && nAction == 5)
            {
                // spider_death/scorp_death face the killer on tick 0, hold the
                // middle pose for ticks 1/2, then gib and destroy on tick 3.
                if (nRenderedFrame == 0
                    && (unsigned)nTarget < (unsigned)kMaxSprites
                    && sprite[nTarget].statnum < kMaxStatus)
                    PlotCourseToSprite(nSprite, nTarget);
                if (nRenderedFrame == 3)
                    KexSpiderBeginGibTimer(nSpider, nSprite);
                return;
            }

            if (nTarget <= -1 || sprite[nTarget].cstat & 0x101)
            {
                switch (nAction)
                {
                    default:
                        return;

                    case 0:
                    {
                        if ((nSpider & 0x1F) == (totalmoves & 0x1F))
                        {
                            if (nTarget < 0) {
                                nTarget = FindPlayer(nSprite, 100);
                            }

                            if (nTarget >= 0)
                            {
                                SpiderList[nSpider].nAction = 1;
                                SpiderList[nSpider].nFrame = 0;
                                SpiderList[nSpider].nTarget = nTarget;

                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
                                sprite[nSprite].yvel = sintable[sprite[nSprite].ang]; // NOTE - not angle masking here in original code
                                return;
                            }
                        }

                        break;
                    }
                    case 1:
                    {
                        if (g_kexMode && KexAIEnterMeleeRange(
                                nSprite, nTarget, s_spiderScorp[nSpider] ? 0 : 20))
                        {
                            PlotCourseToSprite(nSprite, nTarget);
                            SpiderList[nSpider].nAction = 2;
                            SpiderList[nSpider].nFrame = 0;
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                            return;
                        }

                        if (nTarget >= 0) {
                            nVel++;
                        }
        goto case_3;
                        break;
                    }
                    case 4:
                    {
                        if (!SpiderList[nSpider].nFrame)
                        {
                            SpiderList[nSpider].nFrame  = 0;
                            SpiderList[nSpider].nAction = 1;
                        }
                        fallthrough__;
                    }
                    case 3:
                    {
        case_3:
                        short nSector = sprite[nSprite].sectnum;

                        if (g_kexMode && SpiderList[nSpider].nAction == 1
                            && sprite[nSprite].hitag > 0)
                            sprite[nSprite].hitag--;

                        if (sprite[nSprite].cstat & 8)
                        {
                            sprite[nSprite].zvel = 0;
                            sprite[nSprite].z = sector[nSector].ceilingz + (tilesiz[sprite[nSprite].picnum].y << 5);

                            if (sector[nSector].ceilingstat & 1)
                            {
                                sprite[nSprite].cstat ^= 8;
                                sprite[nSprite].zvel = 1;

                                SpiderList[nSpider].nAction = 3;
                                SpiderList[nSpider].nFrame  = 0;
                            }
                        }

                        if ((totalmoves & 0x1F) == (nSpider & 0x1F)
                            || (g_kexMode && SpiderList[nSpider].nAction == 1
                                && sprite[nSprite].hitag <= 0))
                        {
                            PlotCourseToSprite(nSprite, nTarget);

                            if (RandomSize(3))
                            {
                                sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
                                sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
                            }
                            else
                            {
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                            }

                            if (SpiderList[nSpider].nAction == 1
                                && (g_kexMode || RandomBit()))
                            {
                                StartSpiderLeap(nSpider, nSprite);
                            }
                        }
                        break;
                    }
                    case 5:
                    {
                        if (!SpiderList[nSpider].nFrame)
                        {
                            runlist_DoSubRunRec(sprite[nSprite].owner);
                            runlist_FreeRun(sprite[nSprite].lotag - 1);
                            runlist_SubRunRec(SpiderList[nSpider].nRun);
                            sprite[nSprite].cstat = 0x8000;
                            mydeletesprite(nSprite);
                        }
                        return;
                    }
                    case 2:
                    {
                        if (nTarget != -1)
                        {
                            if (g_kexMode)
                            {
                                bool const exactSequence = KexSpiderExactSequence(nSpider);
                                int const checkFrame = s_spiderScorp[nSpider]
                                    ? (exactSequence ? 11 : 5) : 0;
                                int const checkReach = s_spiderScorp[nSpider] ? 0 : 20;
                                if (nRenderedFrame == checkFrame
                                    && !KexMeleeRange(nSprite, nTarget, checkReach))
                                {
                                    SpiderList[nSpider].nAction = 1;
                                    SpiderList[nSpider].nFrame = 0;
                                    PlotCourseToSprite(nSprite, nTarget);
                                    sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
                                    sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
                                    return;
                                }

                                int const hitFrame = s_spiderScorp[nSpider]
                                    ? (exactSequence ? 12 : 6)
                                    : (exactSequence ? 6 : 3);
                                int const hitReach = s_spiderScorp[nSpider] ? 16 : 20;
                                if (nRenderedFrame == hitFrame)
                                {
                                    int const s = KexSound_Get("spi_atak");
                                    if (s >= 0) D3PlayFX(s, nSprite);
                                    if (KexMeleeAttackRange(nSprite, nTarget, hitReach))
                                        runlist_DamageEnemy(nTarget, nSprite, 10);
                                }
                                return;
                            }

                            if (nFrameFlag & 0x80)
                            {
                                runlist_DamageEnemy(nTarget, nSprite, 3);
                                D3PlayFX(StaticSound[kSoundSpiderAttack], nSprite);
                            }

                            if (PlotCourseToSprite(nSprite, nTarget) < 1024) {
                                return;
                            }

                            SpiderList[nSpider].nAction = 1;
                        }
                        else
                        {
                            SpiderList[nSpider].nAction = 0;
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                        }

                        SpiderList[nSpider].nFrame = 0;
                        break;
                    }
                }
            }
            else
            {
                SpiderList[nSpider].nTarget = -1;
                SpiderList[nSpider].nAction = 0;
                SpiderList[nSpider].nFrame = 0;

                sprite[nSprite].xvel = 0;
                sprite[nSprite].yvel = 0;
            }

            if (g_kexMode && KexSpiderExactSequence(nSpider)
                && SpiderList[nSpider].nAction == 3)
            {
                KexSpiderLeapTick(nSpider, nSprite, nTarget);
                return;
            }

            int nMov = movesprite(nSprite,
                                  KexScaleCreatureMove(nSprite, sprite[nSprite].xvel << nVel),
                                  KexScaleCreatureMove(nSprite, sprite[nSprite].yvel << nVel),
                                  sprite[nSprite].zvel, 1280, -1280, CLIPMASK0);

            if (!nMov)
                return;

            if (nMov & 0x10000
                && sprite[nSprite].zvel < 0
                && (hihit & 0xC000) != 0xC000
                && !((sector[sprite[nSprite].sectnum].ceilingstat) & 1))
            {
                sprite[nSprite].cstat |= 8;
                sprite[nSprite].z = GetSpriteHeight(nSprite) + sector[sprite[nSprite].sectnum].ceilingz;
                sprite[nSprite].zvel = 0;

                SpiderList[nSpider].nAction = 1;
                SpiderList[nSpider].nFrame = 0;
                return;
            }
            else
            {
                switch (nMov & 0xC000)
                {
                    case 0x8000:
                    {
                        sprite[nSprite].ang = (sprite[nSprite].ang + 256) & 0x7EF;
                        sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
                        sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
                        return;
                    }
                    case 0xC000:
                    {
                        if ((nMov & 0x3FFF) == nTarget)
                        {
                            int nAng = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                            if (AngleDiff(sprite[nSprite].ang, nAng) < 64)
                            {
                                SpiderList[nSpider].nAction = 2;
                                SpiderList[nSpider].nFrame = 0;
                            }
                        }
                        return;
                    }
                    default:
                        break;
                }

                if (SpiderList[nSpider].nAction == 3)
                {
                    SpiderList[nSpider].nAction = 1;
                    SpiderList[nSpider].nFrame = 0;
                }
                return;
            }

            return;
        }

        case 0x90000:
        {
            if (KexSpiderExactSequence(nSpider) && nAction == 6)
                return;
            int const nSeq = SeqOffsets[s_spiderScorp[nSpider] ? kSeqScorpion : kSeqSpider]
                           + ActionSeq[nAction].a;
            if (SpiderList[nSpider].nFrame < 0
                || SpiderList[nSpider].nFrame >= SeqSize[nSeq])
                SpiderList[nSpider].nFrame = 0;
            seq_PlotSequence(a & 0xFFFF, nSeq, SpiderList[nSpider].nFrame,
                             ActionSeq[nAction].b);
            break;
        }

        case 0xA0000:
        {
            if (SpiderList[nSpider].nHealth <= 0)
                return;

            nDamage = runlist_CheckRadialDamage(nSprite);
            // fall through
            fallthrough__;
        }

        case 0x80000:
        {
            if (!nDamage)
                return;

            short nTarget = a & 0xFFFF;

            SpiderList[nSpider].nHealth -= nDamage;
            if (SpiderList[nSpider].nHealth > 0)
            {
                /*
                NOTE:
                    nTarget check was added, but should we return if it's invalid instead
                    or should code below (action set, b set) happen?
                    Other AI doesn't show consistency in this regard (see Scorpion code)
                */
                if (nTarget > -1
                    && ((g_kexMode && sprite[nTarget].statnum < 199)
                        || (!g_kexMode && sprite[nTarget].statnum == 100)))
                {
                    SpiderList[nSpider].nTarget = nTarget;
                }

                if (g_kexMode)
                {
                    // AI_Spider and AI_Scorp deliberately have no painAnim.
                    // Their 255/256 roll only plays the shared pain sound;
                    // it must never switch to the Build flinch action.
                    if (KexDifficulty_PainRoll(255))
                    {
                        int const sound = KexSound_Get("spi-hurt");
                        if (sound >= 0)
                            D3PlayFX(sound, nSprite);
                    }
                    return;
                }

                SpiderList[nSpider].nAction = 4;
                SpiderList[nSpider].nFrame  = 0;
            }
            else
            {
                SpiderList[nSpider].nHealth = 0;
                nCreaturesLeft--;

                if (g_kexMode)
                {
                    short const nKiller = nMessage == 0xA0000
                        ? (short)nRadialOwner : nTarget;
                    if (KexSpiderExactSequence(nSpider))
                    {
                        // InflictDamage removes shootable immediately. The
                        // host stays solid through the four authored death
                        // ticks and disappears with A_Destroy on tick 3.
                        sprite[nSprite].cstat &= ~0x100;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].zvel = 0;
                        SpiderList[nSpider].nAction = 5;
                        SpiderList[nSpider].nFrame = 0;
                        SpiderList[nSpider].nTarget = nKiller;

                        // Radial dispatch bypasses runlist_DamageEnemy's
                        // direct-hit post hook.
                        if (nMessage == 0xA0000)
                            KexTrigger_Died(nSprite);
                        return;
                    }

                    // Old conversion fallback: retain the pre-existing
                    // immediate destruction and immediate drop timing.
                    sprite[nSprite].cstat &= 0xFEFE;
                    if (nMessage == 0xA0000)
                    {
                        KexDrop_RandomItem(nSprite, nKiller);
                        KexTrigger_Died(nSprite);
                    }
                    KexSpiderImmediateDeath(nSpider, nSprite, nKiller);
                }
                else
                {
                    // Classic keeps its original seven native chunks and
                    // delayed death sequence.
                    sprite[nSprite].cstat &= 0xFEFE;
                    SpiderList[nSpider].nAction = 5;
                    SpiderList[nSpider].nFrame  = 0;
                    for (int i = 0; i < 7; i++)
                        BuildCreatureChunk(nSprite, seq_GetSeqPicnum(kSeqSpider, i + 41, 0));
                }
            }

            return;
        }
    }
}

class SpiderLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void SpiderLoadSave::Load()
{
    Read(&SpiderCount, sizeof(SpiderCount));
    Read(SpiderList, sizeof(Spider) * SpiderCount);

    // s_spiderScorp is runtime-only, while sprites and SpiderList are saved.
    // Rebuild it from the explicit marker. The private exact tile range also
    // upgrades saves written before that marker existed.
    for (int nSpider = 0; nSpider < SpiderCount; nSpider++)
    {
        int const nSprite = SpiderList[nSpider].nSprite;
        s_spiderScorp[nSpider] = 0;
        if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites)
            continue;
        int const nPic = sprite[nSprite].picnum;
        bool const scorpion = sprite[nSprite].extra == 1
            || (sprite[nSprite].extra < 0
                && nPic >= kKexExactScorpTileBase
                && nPic < kKexExactScorpTileBase + kKexExactScorpTileCount);
        s_spiderScorp[nSpider] = scorpion;
        sprite[nSprite].extra = scorpion ? 1 : 0;
    }
}

void SpiderLoadSave::Save()
{
    Write(&SpiderCount, sizeof(SpiderCount));
    Write(SpiderList, sizeof(Spider) * SpiderCount);
}

static SpiderLoadSave* myLoadSave;

void SpiderLoadSaveConstruct()
{
    myLoadSave = new SpiderLoadSave();
}
