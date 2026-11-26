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
#include "exhumed.h"
#include "move.h"
#include "init.h"
#include "lighting.h"
#include "kexbreak.h"
#include "kextactile.h"
#include "kexinv.h"
#include "ramses.h"
#include "wasp.h"
#include "roach.h"
#include "set.h"
#include "lavadude.h"
#include "scorp.h"
#include "queen.h"
#include "bubbles.h"
#include "object.h"
#include "player.h"
#include "view.h"
#include "status.h"
#include "runlist.h"
#include "items.h"
#include "sound.h"
#include "trigdat.h"
#include "anims.h"
#include "random.h"
#include "bullet.h"
#include "grenade.h"
#include "save.h"
#include "timer.h"
#include <string.h>
#include <assert.h>
#ifndef __WATCOMC__
//#include <cmath>
#else
#include <stdlib.h>
//#include <math.h>
#endif

#define kMaxPushBlocks	100
#define kMaxChunks	75

short NearCount = -1;
short NearSector[kMaxSectors] = { 0 };

short nPushBlocks;
BlockInfo sBlockInfo[kMaxPushBlocks];

// TODO - moveme?
short overridesect;
short nBodySprite[50];
int hihit, sprceiling, sprfloor, lohit;

short nChunkSprite[kMaxChunks];

// A_Quake writes the local KEX player's shake-time directly: it is neither
// spatial nor cumulative. Keep its visual-only random stream private so an
// authored camera shake can never perturb native gameplay RNG decisions.
static int s_kexQuakeTicks;
static int s_kexQuakeYaw;
static uint32_t s_kexQuakeRandom = UINT32_C(0x4b455831);
static int s_kexQuakeTest = -1;
static int s_kexTossTest = -1;
static uint32_t s_kexTossRandom;
// kexPuppet tracks independent lavaTicks and slimeTicks at 60 Hz. The native
// game loop is 30 Hz, so every entry here consumes two source updates.
enum { kKexHazardLavaTick = 0, kKexHazardSlimeTick = 1 };
static uint8_t s_kexHazardTicks[kMaxPlayers][2];

int KexHazard_IsActive(int nPlayer)
{
    return g_kexMode && (unsigned)nPlayer < (unsigned)kMaxPlayers
        && (s_kexHazardTicks[nPlayer][kKexHazardLavaTick]
            || s_kexHazardTicks[nPlayer][kKexHazardSlimeTick]);
}

static void KexHazard_ResetPlayer(short nSprite, uint8_t nHazards)
{
    int const nPlayer = GetPlayerFromSprite(nSprite);
    if ((unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return;
    if (nHazards & kKexHazardLava)
        s_kexHazardTicks[nPlayer][kKexHazardLavaTick] = 0;
    if (nHazards & kKexHazardSlime)
        s_kexHazardTicks[nPlayer][kKexHazardSlimeTick] = 0;
}

static void KexHazard_DamagePlayer(short nSprite, uint8_t nHazard, int reset)
{
    int const nPlayer = GetPlayerFromSprite(nSprite);
    if ((unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return;

    int const tick = nHazard == kKexHazardLava
        ? kKexHazardLavaTick : kKexHazardSlimeTick;
    // LavaDamage and SlimeDamage reset their own timer on a moving floor
    // contact. The caller distinguishes a landing/ground face from a
    // vertical lava wall, whose plane does not trigger LavaDamage's reset.
    if (reset)
    {
        s_kexHazardTicks[nPlayer][tick] = 0;
        return;
    }

    bool const anklets = KexInv_HasArtifact(nPlayer, kKexArtiAnklets);
    int const period = nHazard == kKexHazardSlime && anklets ? 64 : 32;
    int const previous = s_kexHazardTicks[nPlayer][tick];
    s_kexHazardTicks[nPlayer][tick] = (uint8_t)(previous + 2);

    // Source checks (++ticks & (period - 1)) == 1.  Advancing two source
    // updates per native tick can cross that phase on either update.
    if ((previous & (period - 1)) != 0 && (previous & (period - 1)) != period - 1)
        return;

    int nDamage;
    if (nHazard == kKexHazardLava)
    {
        // Lava kills outright without the Anklets; with them it deals 50
        // source health points each 32 source updates.
        nDamage = anklets ? 50 : max(1, ((int)PlayerList[nPlayer].nHealth + 3) / 4);
    }
    else
    {
        // Slime is 375 source points/32 ticks normally, but five points/64
        // ticks with the Anklets. runlist_DamageEnemy multiplies by four.
        nDamage = anklets ? 5 : 375;
    }
    runlist_DamageEnemy(nSprite, -1, (short)nDamage);

    // OnDamage handles a surviving lava/slime hit's sizzle. A fatal hit
    // returns from OnDamage first, then the caller adds this exact pair.
    if (PlayerList[nPlayer].nHealth <= 0)
    {
        int const sizzle = KexSound_Get("jon_pois");
        if (sizzle >= 0)
            D3PlayFX(sizzle, nSprite);
        int const death = KexSound_Get("k_jdie2");
        if (death >= 0)
            D3PlayFX(death, nSprite);
    }
}

static int KexQuakeRandomInt()
{
    s_kexQuakeRandom = s_kexQuakeRandom * UINT32_C(1664525)
        + UINT32_C(221297);
    int const r = (int)((s_kexQuakeRandom & 0x7fff)
        + ((s_kexQuakeRandom >> 15) & 0x7fff)
        + (s_kexQuakeRandom >> 31));
    return ((r & 0x7fff) + (r >> 15)) & 0x7fff;
}

static int KexQuakeRandomCenti()
{
    // kexRand::CFloat(): Max(20000) - 10000, expressed without float state.
    return KexQuakeRandomInt() % 20000 - 10000;
}

void KexQuake(int nTicks)
{
    if (!g_kexMode)
        return;
    // The KEX action is an assignment, so a short event can deliberately
    // replace a longer current shake instead of extending it.
    s_kexQuakeTicks = max(0, nTicks);
    KexTactile_Quake();
}

int KexQuakeYaw()
{
    return g_kexMode ? s_kexQuakeYaw : 0;
}

static void KexQuake_Tick()
{
    if (!g_kexMode || s_kexQuakeTicks <= 0)
    {
        s_kexQuakeYaw = 0;
        return;
    }

    // player.cpp writes shakeVector then decrements shakeTime; render uses it
    // only while the remaining time is nonzero. Match that final blank tick.
    if (--s_kexQuakeTicks <= 0)
    {
        s_kexQuakeYaw = 0;
        if ((unsigned)nLocalPlayer < (unsigned)kMaxPlayers)
            nQuake[nLocalPlayer] = 0;
        return;
    }

    // KEX applies CFloat()*32*0.125 degrees of yaw and CFloat()*32*0.5
    // vertical units. Build yaw is 2048 units/circle and a KEX Z unit is 96.
    s_kexQuakeYaw = (KexQuakeRandomCenti() * 2048) / (10000 * 90);
    int const vertical = (KexQuakeRandomCenti() * 1536) / 10000;
    if ((unsigned)nLocalPlayer < (unsigned)kMaxPlayers)
        nQuake[nLocalPlayer] = (short)vertical;
}

static void KexQuake_TestTick()
{
    if (s_kexQuakeTest == -1)
        s_kexQuakeTest = g_kexMode && Bgetenv("VB_KEX_QUAKE_TEST") ? 0 : -2;
    if (s_kexQuakeTest != 0 || (unsigned)nLocalPlayer >= (unsigned)kMaxPlayers)
        return;

    int const savedTicks = s_kexQuakeTicks;
    int const savedYaw = s_kexQuakeYaw;
    uint32_t const savedRandom = s_kexQuakeRandom;
    short const savedQuake = nQuake[nLocalPlayer];

    KexQuake(60);
    KexQuake(8); // source assignment, not max/accumulate behavior
    bool okay = s_kexQuakeTicks == 8;
    for (int i = 0; i < 7; i++)
    {
        KexQuake_Tick();
        okay &= s_kexQuakeTicks == 7 - i;
    }
    KexQuake_Tick();
    okay &= s_kexQuakeTicks == 0 && s_kexQuakeYaw == 0
        && nQuake[nLocalPlayer] == 0;

    s_kexQuakeTicks = savedTicks;
    s_kexQuakeYaw = savedYaw;
    s_kexQuakeRandom = savedRandom;
    nQuake[nLocalPlayer] = savedQuake;
    OSD_Printf("Kex quake test: %s (local fixed-duration assignment and final blank tick)\n",
               okay ? "PASS" : "FAIL");
    s_kexQuakeTest = -2;
}

// Every shipped A_TossActor call has this exact recipe: origin + (0,0,32),
// horizontal Range(-8,8), vertical Range(4,16).  KEX coordinates use WORLD
// = 6 in X/Y and SCALE(16)*WORLD in Z; its actor clock is 60 Hz while
// PCExhumed's is 30 Hz.  Sprite x/y velocity is kept in the same <<8 form as
// MoveCreature(), so one source step moves xvel/64 Build units.
static constexpr int kKexTossWorld = 6;
static constexpr int kKexTossZUnit = 16 * kKexTossWorld;
static constexpr int kKexTossXYVelocityUnit = kKexTossWorld * 64;
static constexpr int kKexTossGravity = (3 * kKexTossZUnit) / 4;
static constexpr int kKexTossMinXYVelocity = kKexTossXYVelocityUnit / 8;
static constexpr int16_t kKexTossChunkMarker = -2;
static constexpr int16_t kKexTossBouncyChunkMarker = -3;
// The DOS ring holds 75 reusable chunk sprites. KEX can legitimately toss
// more than that in a same-tick chain of explosive vases, so overflow pieces
// use disposable sprites and a saved marker distinct from the classic ring.
static constexpr int16_t kKexTossOverflowChunkMarker = -7;
static constexpr int16_t kKexTossOverflowBouncyChunkMarker = -8;
static constexpr int16_t kKexTossGibExpireBase = -1000;

static bool KexTossIsBouncyMarker(int marker)
{
    return (marker <= kKexTossBouncyChunkMarker
            && marker >= kKexTossBouncyChunkMarker - kKexTossBounceSoundFlesh)
        || (marker <= kKexTossOverflowBouncyChunkMarker
            && marker >= kKexTossOverflowBouncyChunkMarker - kKexTossBounceSoundFlesh);
}

static KexTossBounceSound KexTossBouncySoundForMarker(int marker)
{
    int const base = marker <= kKexTossOverflowBouncyChunkMarker
        ? kKexTossOverflowBouncyChunkMarker : kKexTossBouncyChunkMarker;
    return (KexTossBounceSound)(base - marker);
}

static bool KexTossIsOverflowMarker(int marker)
{
    return marker == kKexTossOverflowChunkMarker
        || (marker <= kKexTossOverflowBouncyChunkMarker
            && marker >= kKexTossOverflowBouncyChunkMarker - kKexTossBounceSoundFlesh);
}

int KexTossOverflowCount(void)
{
    int count = 0;
    for (int nSprite = headspritestat[899]; nSprite >= 0;
         nSprite = nextspritestat[nSprite])
        count += KexTossIsOverflowMarker(sprite[nSprite].extra);
    return count;
}

static int KexTossRandomInt()
{
    // math/random.cpp:kexRand::Int(), including its folded 15-bit output.
    s_kexTossRandom = s_kexTossRandom * UINT32_C(1664525) + UINT32_C(221297);
    uint32_t const r = (s_kexTossRandom & 0x7fff)
        + ((s_kexTossRandom >> 15) & 0x7fff) + (s_kexTossRandom >> 31);
    return ((r & 0x7fff) + (r >> 15)) & 0x7fff;
}

static int KexTossRangeForRoll(int roll, int minimum, int maximum)
{
    // kexRand::Range(min,max) orders its 0..9999 roll from max toward min.
    int64_t const numerator = (int64_t)maximum * 10000
        + (int64_t)(minimum - maximum) * roll;
    return numerator >= 0 ? (int)((numerator + 5000) / 10000)
        : -(int)((-numerator + 5000) / 10000);
}

static int KexTossRange(int minimum, int maximum)
{
    return KexTossRangeForRoll(KexTossRandomInt() % 10000, minimum, maximum);
}

static void KexTossPreserveClassicRandomCursor()
{
    // BuildCreatureChunk used these 18 DOS-RNG bits before this KEX bridge.
    // Preserve that synced cursor while A_TossActor uses the independent
    // remaster kexRand stream.
    (void)RandomSize(5);
    (void)RandomSize(5);
    (void)RandomSize(8);
}

static int KexTossClipdist(int nPic, bool bouncy)
{
    // kexActor collision uses its authored radius, unscaled: every vase,
    // barrel and boulder shard is radius 32; creature gibs are radius 28
    // except the three fish-gib actors, which are radius 32. Build's signed
    // clipdist is measured in quarters of a world unit.
    int const radius = bouncy || (nPic >= 7820 && nPic <= 7822) ? 32 : 28;
    return radius * kKexTossWorld / 4;
}

static int KexTossActorHeight(int nPic, bool bouncy)
{
    // These are the actual kexSlaveActor heights on the toss targets. The
    // regular creature gibs share 56, fish gibs use 64, and every bouncy prop
    // or boulder shard uses 32. Build sprite.z is the KEX actor origin, so a
    // ceiling contact must include that authored height.
    int const height = bouncy ? 32 : (nPic >= 7820 && nPic <= 7822 ? 64 : 56);
    return height * kKexTossZUnit;
}

static int KexTossGroundFriction(int velocity)
{
    // kexActor::UpdateVelocity() removes components below AMOVE_MIN (.125).
    // x/y are stored at six Build units times 64 fixed-point units per KEX
    // world unit, so that source threshold is exactly 48 here.
    velocity /= 2;
    return klabs(velocity) < kKexTossMinXYVelocity ? 0 : velocity;
}

static bool KexTossAtRest(short nSprite)
{
    int const nSector = sprite[nSprite].sectnum;
    if ((unsigned)nSector >= (unsigned)numsectors)
        return false;

    // A_DestroyAtRest(0.01) accepts a point on its KEX floor with a zero
    // post-friction velocity. movesprite() keeps the Build proxy exactly on
    // that floor after a downward contact.
    return sprite[nSprite].z >= getflorzofslope(nSector, sprite[nSprite].x,
                                                  sprite[nSprite].y)
        && sprite[nSprite].xvel == 0 && sprite[nSprite].yvel == 0
        && sprite[nSprite].zvel == 0;
}

static int KexTossBounceVelocity(int velocity)
{
    // CheckFloorAndCeilings snaps source origin to its floor, then stores
    // -(old velocity) * (1.2 - defaultGravity). With .75 gravity that is a
    // .45 reflection; Build Z has the opposite sign but the same equation.
    return -velocity * 45 / 100;
}

static char const *KexTossBounceSoundName(KexTossBounceSound sound, int variant)
{
    // The values are the exact bounceSound_1..3 strings on the KEX shard
    // actors. flesh01..03 already have the enemy blood aliases; glass and
    // wood have no DOS counterpart and are emitted as KEX-only sound names.
    static char const *const soundNames[][3] = {
        { "k_gls1", "k_gls2", "k_gls3" },
        { "k_wood1", "k_wood2", "k_wood3" },
        { "blood_1", "blood_2", "blood_3" },
    };
    if ((unsigned)sound >= ARRAY_SSIZE(soundNames))
        return NULL;
    return soundNames[sound][variant % 3];
}

static void KexTossPlayBounceSound(short nSprite, KexTossBounceSound sound,
                                   int sourceStep)
{
    // kexActor chooses bounceSounds[session.GetTime() % 3]. Session time is
    // milliseconds and its fixed actor clock is 60 Hz, so derive the two
    // source instants represented by this 30-Hz native update without using
    // or perturbing the gameplay random stream.
    int const milliseconds = (int)(((int64_t)(int32_t)totalclock * 1000) / TICRATE)
        + sourceStep * (1000 / 60);
    char const *const name = KexTossBounceSoundName(sound, milliseconds % 3);
    int const nSound = name ? KexSound_Get(name) : -1;
    if (nSound >= 0)
        D3PlayFX(nSound, nSprite);
}

static bool KexTossGibExpireTick(short nSprite)
{
    int ticks = sprite[nSprite].extra <= kKexTossGibExpireBase
        ? -sprite[nSprite].extra - (-kKexTossGibExpireBase) : 0;

    // kexActor::UpdateMovement decrements expires once per source tick only
    // when movement is zero. Health 100 reaches zero on tick 100, then the
    // original scale loses 1/32 per tick and vanishes on tick 131.
    ++ticks;
    if (ticks >= 131)
        return true;
    if (ticks >= 100)
    {
        int const repeat = 64 * (131 - ticks) / 32;
        sprite[nSprite].xrepeat = max(1, repeat);
        sprite[nSprite].yrepeat = max(1, repeat);
    }
    sprite[nSprite].extra = kKexTossGibExpireBase - ticks;
    return false;
}

static void KexTossSourcePhysicsStep(short nSprite, int bouncySound,
                                     int sourceStep)
{
    int const nSector = sprite[nSprite].sectnum;
    if ((unsigned)nSector >= (unsigned)numsectors)
        return;
    bool const bouncy = bouncySound >= 0;

    int const floor = getflorzofslope(nSector, sprite[nSprite].x,
                                      sprite[nSprite].y);
    bool const airborne = sprite[nSprite].z < floor;
    if (airborne)
        sprite[nSprite].zvel += kKexTossGravity;
    else
    {
        sprite[nSprite].xvel = KexTossGroundFriction(sprite[nSprite].xvel);
        sprite[nSprite].yvel = KexTossGroundFriction(sprite[nSprite].yvel);
    }

    // kexActor::CheckFloorAndCeilings tests the ceiling before the floor,
    // and its normal actor path reverses Z velocity without snapping to the
    // plane. The old bridge omitted this entirely, letting an upward shard
    // pass through a low ceiling until the next floor contact.
    int const ceiling = getceilzofslope(nSector, sprite[nSprite].x,
                                        sprite[nSprite].y);
    if (sprite[nSprite].z - KexTossActorHeight(sprite[nSprite].picnum, bouncy)
        + sprite[nSprite].zvel <= ceiling)
        sprite[nSprite].zvel = -sprite[nSprite].zvel;

    // The bouncy shard actors perform their floor reflection before moving,
    // which makes the rebound leave from the floor in this same source tick.
    if (bouncy && airborne && sprite[nSprite].zvel > 4 * kKexTossGravity
        && sprite[nSprite].z + sprite[nSprite].zvel >= floor)
    {
        sprite[nSprite].z = floor;
        sprite[nSprite].zvel = KexTossBounceVelocity(sprite[nSprite].zvel);
        sprite[nSprite].xvel = KexTossGroundFriction(sprite[nSprite].xvel);
        sprite[nSprite].yvel = KexTossGroundFriction(sprite[nSprite].yvel);
        KexTossPlayBounceSound(nSprite, (KexTossBounceSound)bouncySound,
                               sourceStep);
    }

    // kexActor::UpdateMovement takes this pre-move sector snapshot before
    // CModel::MoveActor. It owns the air-to-water splash callback after a
    // successful sector transition, not the old PCExhumed chunk loop.
    short const oldSector = sprite[nSprite].sectnum;
    int const oldX = sprite[nSprite].x;
    int const oldY = sprite[nSprite].y;
    int const oldZ = sprite[nSprite].z;
    int const nMove = movesprite(nSprite, sprite[nSprite].xvel << 8,
                                 sprite[nSprite].yvel << 8,
                                 sprite[nSprite].zvel, 0, 0, CLIPMASK1);
    short const newSector = sprite[nSprite].sectnum;
    KexWaterSplash_OnTransition(oldSector, oldX, oldY, oldZ, newSector,
                                sprite[nSprite].x, sprite[nSprite].y);
    int const landedFloor = getflorzofslope(sprite[nSprite].sectnum,
                                            sprite[nSprite].x, sprite[nSprite].y);
    if (sprite[nSprite].z >= landedFloor)
    {
        sprite[nSprite].z = landedFloor;
        if (bouncy && sprite[nSprite].zvel > 4 * kKexTossGravity)
        {
            sprite[nSprite].zvel = KexTossBounceVelocity(sprite[nSprite].zvel);
            sprite[nSprite].xvel = KexTossGroundFriction(sprite[nSprite].xvel);
            sprite[nSprite].yvel = KexTossGroundFriction(sprite[nSprite].yvel);
            KexTossPlayBounceSound(nSprite, (KexTossBounceSound)bouncySound,
                                   sourceStep);
        }
        else
            sprite[nSprite].zvel = 0;
    }

    // kexCModel::MoveActor() reports a failed horizontal collision by clearing
    // movement and velocity. A floor contact is handled above and preserves
    // horizontal velocity so the next source tick applies its .5 friction.
    if ((nMove & 0xC000) == 0x8000 || (nMove & 0xC000) == 0xC000)
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
}

static void KexToss_TestTick()
{
    if (s_kexTossTest == -1)
        s_kexTossTest = g_kexMode && Bgetenv("VB_KEX_TOSS_TEST") ? 0 : -2;
    if (s_kexTossTest != 0)
        return;

    int airX = 3072, airY = -1536, airZ = -960;
    airZ += kKexTossGravity;
    int const groundX = KexTossGroundFriction(95);
    int const groundY = KexTossGroundFriction(-95);
    bool const okay = KexTossRangeForRoll(0, -8 * kKexTossXYVelocityUnit,
                                           8 * kKexTossXYVelocityUnit) == 3072
        && KexTossRangeForRoll(9999, -8 * kKexTossXYVelocityUnit,
                                8 * kKexTossXYVelocityUnit) == -3071
        && KexTossRangeForRoll(0, -16 * kKexTossZUnit,
                                -4 * kKexTossZUnit) == -384
        && KexTossRangeForRoll(9999, -16 * kKexTossZUnit,
                                -4 * kKexTossZUnit) == -1536
        && KexTossClipdist(7808, false) == 42
        && KexTossClipdist(7820, false) == 48
        && KexTossClipdist(7849, true) == 48
        && KexTossActorHeight(7808, false) == 56 * kKexTossZUnit
        && KexTossActorHeight(7820, false) == 64 * kKexTossZUnit
        && KexTossActorHeight(7849, true) == 32 * kKexTossZUnit
        && !Bstrcmp(KexTossBounceSoundName(kKexTossBounceSoundGlass, 0), "k_gls1")
        && !Bstrcmp(KexTossBounceSoundName(kKexTossBounceSoundWood, 2), "k_wood3")
        && !Bstrcmp(KexTossBounceSoundName(kKexTossBounceSoundFlesh, 1), "blood_2")
        && airX == 3072 && airY == -1536 && airZ == -888
        && groundX == 0 && groundY == 0
        && KexTossBounceVelocity(960) == -432;
    OSD_Printf("Kex toss test: %s (A_TossActor +32, +/-8, +4..16; gib expiry, bouncy 60 Hz .5/.75/sounds)\n",
               okay ? "PASS" : "FAIL");
    s_kexTossTest = -2;
}


signed int lsqrt(int a1)
{
    int v1;
    int v2;
    signed int result;

    v1 = a1;
    v2 = a1 - 0x40000000;

    result = 0;

    if (v2 >= 0)
    {
        result = 32768;
        v1 = v2;
    }
    if (v1 - ((result << 15) + 0x10000000) >= 0)
    {
        v1 -= (result << 15) + 0x10000000;
        result += 16384;
    }
    if (v1 - ((result << 14) + 0x4000000) >= 0)
    {
        v1 -= (result << 14) + 0x4000000;
        result += 8192;
    }
    if (v1 - ((result << 13) + 0x1000000) >= 0)
    {
        v1 -= (result << 13) + 0x1000000;
        result += 4096;
    }
    if (v1 - ((result << 12) + 0x400000) >= 0)
    {
        v1 -= (result << 12) + 0x400000;
        result += 2048;
    }
    if (v1 - ((result << 11) + 0x100000) >= 0)
    {
        v1 -= (result << 11) + 0x100000;
        result += 1024;
    }
    if (v1 - ((result << 10) + 0x40000) >= 0)
    {
        v1 -= (result << 10) + 0x40000;
        result += 512;
    }
    if (v1 - ((result << 9) + 0x10000) >= 0)
    {
        v1 -= (result << 9) + 0x10000;
        result += 256;
    }
    if (v1 - ((result << 8) + 0x4000) >= 0)
    {
        v1 -= (result << 8) + 0x4000;
        result += 128;
    }
    if (v1 - ((result << 7) + 4096) >= 0)
    {
        v1 -= (result << 7) + 4096;
        result += 64;
    }
    if (v1 - ((result << 6) + 1024) >= 0)
    {
        v1 -= (result << 6) + 1024;
        result += 32;
    }
    if (v1 - (32 * result + 256) >= 0)
    {
        v1 -= 32 * result + 256;
        result += 16;
    }
    if (v1 - (16 * result + 64) >= 0)
    {
        v1 -= 16 * result + 64;
        result += 8;
    }
    if (v1 - (8 * result + 16) >= 0)
    {
        v1 -= 8 * result + 16;
        result += 4;
    }
    if (v1 - (4 * result + 4) >= 0)
    {
        v1 -= 4 * result + 4;
        result += 2;
    }
    if (v1 - (2 * result + 1) >= 0)
        result += 1;

    return result;
}

static void KexAcquire_TestTick();

void MoveThings()
{
    UndoFlashes();
    DoLights();
    KexImpactLight_Tick(); // exact short Fx_FireballExplode dlight; no-op elsewhere
    KexLight_Tick();      // the remaster's fires and torches; no-op elsewhere
    KexFlash_Tick();      // and its hit flash
    KexInv_CaptionTick(); // and its pickup captions
    KexTele_Tick();       // its teleport pads; no-op elsewhere
    KexForce_Tick();      // its force-field electrocution feedback
    KexHudFlash_Tick();   // source red damage and blue pickup feedback
    KexInv_Tick();        // the collectibles self-test; no-op elsewhere
    KexScripted_Tick();   // the casket lids; no-op elsewhere
    KexPlate_Tick();      // the remaster's floor-trigger plates; no-op elsewhere
    KexRemote_Tick();     // its REMOTE_TRIGGER dispatcher; no-op elsewhere
    KexBeam_Tick();       // the laser beams; no-op elsewhere
    KexScript_Tick();     // the level scripts; no-op elsewhere
    int const kexRamses = KexRamses_Tick(); // authored KEX spirit cinematic
    KexFlyer_TestTick();  // deterministic crow/wasp/seedling probe; env-only
    KexAcquire_TestTick(); // shared KEX sight/FOV acquisition probe; env-only
    KexSpiderLeap_TestTick(); // remaster Spider/Scorpion leap/landing probe; env-only
    KexSentry_TestTick(); // exact Sentry cadence/callback probe; env-only
    KexSet_TestTick();     // exact Set cadence/callback/health probe; env-only
    KexSelkis_TestTick();  // exact Selkis lifecycle/death probe; env-only
    KexQueen_TestTick();   // exact Queen body/pod/snake/tail probe; env-only
    KexBullet_TestTick();  // exact remaster enemy projectile recipes; env-only
    KexGrenade_TestTick(); // exact Amun-bomb charge/throw/lifecycle probe; env-only
    KexMantis_TestTick();  // exact Mantis attack variants/cadence probe; env-only
    KexTrap_TestTick();    // exact shared spawner-factory cadence/stagger probe; env-only
    KexGibDeath_TestTick();// exact shared Fx_Gibs01 visual/drop callback probe; env-only
    KexBirdFeather_TestTick(); // exact Obj_Bird_Feathers loop/fall probe; env-only
    KexQuake_TestTick();       // exact A_Quake assignment/lifetime probe; env-only
    KexToss_TestTick();        // exact shared A_TossActor launch/physics probe; env-only

    if (nFreeze)
    {
        if ((nFreeze == 1 || nFreeze == 2) && !kexRamses) {
            DoSpiritHead();
        }
    }
    else
    {
        runlist_ExecObjects();
        runlist_CleanRunRecs();
    }

    KexQuake_Tick(); // local-only A_Quake shake after all current callbacks

    MoveStatus();
    DoBubbleMachines();
    DoDrips();
    KexFloat_Tick();      // paired floating slabs from the remaster
    KexPad_Tick();        // paired KEX drop pads
    DoMovingSects();
    DoRegenerates();
    KexTactile_Tick(); // authored 200 Hz KEX controller curves, resampled at 30 Hz

    if (nCameraDist >= 0)
    {
        nCameraDist = min(nCameraDist + (((int32_t)totalclock - nCameraClock) << 10), 65536);
        nCameraClock = (int32_t)totalclock;
    }

    if (levelnum == kMap20)
    {
        DoFinale();
        if (lCountDown < 1800 && nDronePitch < 2400 && !lFinaleStart)
        {
            nDronePitch += 64;
            BendAmbientSound();
        }
    }
}

void ResetMoveFifo()
{
    localclock = (int)totalclock;
    movefifoend = 0;
    movefifopos = 0;
    s_kexQuakeTicks = 0;
    s_kexQuakeYaw = 0;
    s_kexQuakeRandom = UINT32_C(0x4b455831);
    // game.cpp seeds kexRand from the timer when a game state begins.
    s_kexTossRandom = ((uint32_t)(timerGetTicks() >> 4) * 2 + 1) * UINT32_C(69069);
    s_kexQuakeTest = -1;
    s_kexTossTest = -1;
    KexTactile_Reset();
    memset(s_kexHazardTicks, 0, sizeof(s_kexHazardTicks));
}

// not used
void clipwall()
{

}

void BuildNear(int x, int y, int walldist, int nSector)
{
    NearSector[0] = nSector;
    NearCount = 1;

    int i = 0;

    while (i < NearCount)
    {
        short nSector = NearSector[i];

        short nWall = sector[nSector].wallptr;
        short nWallCount = sector[nSector].wallnum;

        while (1)
        {
            nWallCount--;
            if (nWallCount < 0)
            {
                i++;
                break;
            }

            short nNextSector = wall[nWall].nextsector;

            if (nNextSector >= 0)
            {
                int j = 0;
                for (; j < NearCount; j++)
                {
                    // loc_14F4D:
                    if (nNextSector == NearSector[j])
                        break;
                }

                if (j >= NearCount)
                {
                    vec2_t pos = { x, y };
                    if (clipinsidebox(pos, nWall, walldist))
                    {
                        NearSector[NearCount] = wall[nWall].nextsector;
                        NearCount++;
                    }
                }
            }

            nWall++;
        }
    }
}

int BelowNear(short nSprite)
{
    short nSector = sprite[nSprite].sectnum;
    int z = sprite[nSprite].z;

    int var_24, z2;

    if ((lohit & 0xC000) == 0xC000)
    {
        var_24 = lohit & 0xC000;
        z2 = sprite[lohit & 0x3FFF].z;
    }
    else
    {
        var_24 = 0x20000;
        z2 = sector[nSector].floorz + SectDepth[nSector];

        if (NearCount > 0)
        {
            short edx;

            for (int i = 0; i < NearCount; i++)
            {
                int nSect2 = NearSector[i];

                while (nSect2 >= 0)
                {
                    edx = nSect2;
                    nSect2 = SectBelow[nSect2];
                }

                int ecx = sector[edx].floorz + SectDepth[edx];
                int eax = ecx - z;

                if (eax < 0 && eax >= -5120)
                {
                    z2 = ecx;
                    nSector = edx;
                }
            }
        }
    }

    if (z2 < sprite[nSprite].z)
    {
        sprite[nSprite].z = z2;
        overridesect = nSector;
        sprite[nSprite].zvel = 0;

        bTouchFloor = kTrue;

        return var_24;
    }
    else
    {
        return 0;
    }
}

static int32_t KexNoStanding_Filter(int16_t nSector, void *userdata)
{
    vec3_t const *const pos = (vec3_t const *)userdata;
    return !KexNoStanding_Sector(nSector) || inside(pos->x, pos->y, nSector) == 1;
}

static void KexGetZRange(vec3_t *pos, int16_t nSector,
                         int32_t *ceilingz, int32_t *ceilinghit,
                         int32_t *floorz, int32_t *floorhit,
                         int32_t wallDist, uint32_t clipMask)
{
    if (KexNoStanding_Active())
        getzrange_filtered(pos, nSector, ceilingz, ceilinghit, floorz, floorhit,
                           wallDist, clipMask, KexNoStanding_Filter, pos);
    else
        getzrange(pos, nSector, ceilingz, ceilinghit, floorz, floorhit, wallDist, clipMask);
}

static int KexSectorHit(int hit)
{
    return (hit & 0xC000) == 0x4000 ? (hit & 0x3FFF) : -1;
}

// A read-only runtime probe for the retail sector-flag regression. For every
// flagged sector it samples points just across a portal, where the actor's
// origin is in the neighbour but its radius still overlaps the flagged
// polygon. At least some authored ledges should differ between ordinary
// Build edge support and the filtered Kex result.
void KexNoStanding_Test(void)
{
    if (!g_kexMode || !Bgetenv("VB_KEX_NOEDGE_TEST"))
        return;

    int marked = 0, portals = 0, samples = 0, suppressed = 0, shown = 0;
    for (int s = 0; s < numsectors; s++)
    {
        if (!KexNoStanding_Sector(s))
            continue;
        marked++;

        int const first = sector[s].wallptr;
        int const end = first + sector[s].wallnum;
        for (int w = first; w < end; w++)
        {
            int const ns = wall[w].nextsector;
            if ((unsigned)ns >= (unsigned)numsectors)
                continue;
            portals++;

            int const w2 = wall[w].point2;
            int64_t const dx = (int64_t)wall[w2].x - wall[w].x;
            int64_t const dy = (int64_t)wall[w2].y - wall[w].y;
            int64_t const adx = dx < 0 ? -dx : dx;
            int64_t const ady = dy < 0 ? -dy : dy;
            int64_t const den = adx > ady ? adx : ady;
            if (!den)
                continue;

            bool foundPoint = false;
            vec3_t pos = {};
            static int const distance[] = { 4, 8, 16, 32, 64, 96, 127 };
            for (int q = 1; q <= 3 && !foundPoint; q++)
            {
                int const mx = wall[w].x + (int)(dx * q / 4);
                int const my = wall[w].y + (int)(dy * q / 4);
                for (int d : distance)
                {
                    int const ox = (int)(-dy * d / den);
                    int const oy = (int)( dx * d / den);
                    for (int sign = -1; sign <= 1; sign += 2)
                    {
                        int const x = mx + sign * ox;
                        int const y = my + sign * oy;
                        if (inside(x, y, ns) == 1 && inside(x, y, s) != 1)
                        {
                            pos.x = x;
                            pos.y = y;
                            foundPoint = true;
                            break;
                        }
                    }
                    if (foundPoint)
                        break;
                }
            }
            if (!foundPoint)
                continue;

            int sc, sf, nc, nf;
            getzsofslope((int16_t)s, pos.x, pos.y, &sc, &sf);
            getzsofslope((int16_t)ns, pos.x, pos.y, &nc, &nf);
            int const top = sc > nc ? sc : nc;
            int const bottom = sf < nf ? sf : nf;
            if (bottom - top <= 2048)
                continue;
            pos.z = top + (bottom - top) / 2;

            int c1, ch1, f1, fh1, c2, ch2, f2, fh2;
            getzrange(&pos, (int16_t)ns, &c1, &ch1, &f1, &fh1, 128, CLIPMASK0);
            getzrange_filtered(&pos, (int16_t)ns, &c2, &ch2, &f2, &fh2, 128, CLIPMASK0,
                               KexNoStanding_Filter, &pos);
            samples++;

            int const floorSector = KexSectorHit(fh1);
            int const ceilingSector = KexSectorHit(ch1);
            bool const removedFloor = floorSector >= 0 && KexNoStanding_Sector(floorSector)
                && inside(pos.x, pos.y, (int16_t)floorSector) != 1 && (f1 != f2 || fh1 != fh2);
            bool const removedCeiling = ceilingSector >= 0 && KexNoStanding_Sector(ceilingSector)
                && inside(pos.x, pos.y, (int16_t)ceilingSector) != 1 && (c1 != c2 || ch1 != ch2);
            if (!removedFloor && !removedCeiling)
                continue;

            suppressed += removedFloor + removedCeiling;
            if (shown++ < 4)
                OSD_Printf("Kex no-edge test: sector %d from %d at (%d,%d): floor %d/%d -> %d/%d, ceiling %d/%d -> %d/%d\n",
                           s, ns, pos.x, pos.y, f1, fh1, f2, fh2, c1, ch1, c2, ch2);
        }
    }
    OSD_Printf("Kex no-edge test: %d marked sectors, %d portals, %d samples, %d edge surfaces suppressed\n",
               marked, portals, samples, suppressed);
}

int movespritez(short nSprite, int z, int height, int UNUSED(flordist), int clipdist,
                int kexWasAirborne)
{
    spritetype* pSprite = &sprite[nSprite];
    short nSector = pSprite->sectnum;
    assert(nSector >= 0 && nSector < kMaxSectors);

    overridesect = nSector;
    short edi = nSector;

    // backup cstat
    uint16_t cstat = pSprite->cstat;

    pSprite->cstat &= ~CSTAT_SPRITE_BLOCK;

    int nRet = 0;

    short nSectFlags = SectFlag[nSector];

    if (nSectFlags & kSectUnderwater) {
        z >>= 1;
    }

    int spriteZ = pSprite->z;
    int floorZ = sector[nSector].floorz;

    int ebp = spriteZ + z;
    int eax = sector[nSector].ceilingz + (height >> 1);

    if ((nSectFlags & kSectUnderwater) && ebp < eax) {
        ebp = eax;
    }

    // loc_151E7: converted KEX maps use YAX for authored vertical links,
    // including floor/ceiling Toggle faces activated at runtime. Prefer the
    // polygon-selected YAX neighbour at the sprite's XY, then retain the
    // original game's SectBelow/SectAbove path as a fallback.
    int lower = -1;
#ifdef YAX_ENABLE
    if (g_kexMode && numyaxbunches > 0)
        lower = yax_getneighborsect(pSprite->x, pSprite->y, pSprite->sectnum, YAX_FLOOR);
#endif
    if (lower < 0)
        lower = SectBelow[pSprite->sectnum];
    int floorSurface = g_kexMode
        ? getflorzofslope(pSprite->sectnum, pSprite->x, pSprite->y)
        : sector[pSprite->sectnum].floorz;
    while (ebp > floorSurface && lower >= 0)
    {
        edi = lower;

        mychangespritesect(nSprite, edi);
        floorSurface = g_kexMode
            ? getflorzofslope(pSprite->sectnum, pSprite->x, pSprite->y)
            : sector[pSprite->sectnum].floorz;
#ifdef YAX_ENABLE
        lower = g_kexMode && numyaxbunches > 0
            ? yax_getneighborsect(pSprite->x, pSprite->y, pSprite->sectnum, YAX_FLOOR) : -1;
#else
        lower = -1;
#endif
        if (lower < 0)
            lower = SectBelow[pSprite->sectnum];
    }

    if (edi != nSector)
    {
        pSprite->z = ebp;

        if (SectFlag[edi] & kSectUnderwater)
        {
            if (nSprite == PlayerList[nLocalPlayer].nSprite) {
                D3PlayFX(StaticSound[kSoundBubbleLow], nSprite);
            }

            if (pSprite->statnum <= 107) {
                pSprite->hitag = 0;
            }
        }
    }
    else
    {
        int upper = -1;
#ifdef YAX_ENABLE
        if (g_kexMode && numyaxbunches > 0)
            upper = yax_getneighborsect(pSprite->x, pSprite->y, pSprite->sectnum, YAX_CEILING);
#endif
        if (upper < 0)
            upper = SectAbove[pSprite->sectnum];
        int ceilingSurface = g_kexMode
            ? getceilzofslope(pSprite->sectnum, pSprite->x, pSprite->y)
            : sector[pSprite->sectnum].ceilingz;
        while (ebp < ceilingSurface && upper >= 0)
        {
            edi = upper;

            mychangespritesect(nSprite, edi);
            ceilingSurface = g_kexMode
                ? getceilzofslope(pSprite->sectnum, pSprite->x, pSprite->y)
                : sector[pSprite->sectnum].ceilingz;
#ifdef YAX_ENABLE
            upper = g_kexMode && numyaxbunches > 0
                ? yax_getneighborsect(pSprite->x, pSprite->y, pSprite->sectnum, YAX_CEILING) : -1;
#else
            upper = -1;
#endif
            if (upper < 0)
                upper = SectAbove[pSprite->sectnum];
        }
    }

    // This function will keep the player from falling off cliffs when you're too close to the edge.
    // This function finds the highest and lowest z coordinates that your clipping BOX can get to.
    vec3_t zrangePos = { pSprite->x, pSprite->y, pSprite->z - 256 };
    KexGetZRange(&zrangePos, pSprite->sectnum,
                 &sprceiling, &hihit, &sprfloor, &lohit, 128, CLIPMASK0);

    int mySprfloor = sprfloor;

    if ((lohit & 0xC000) != 0xC000) {
        mySprfloor += SectDepth[pSprite->sectnum];
    }

    if (ebp > mySprfloor)
    {
        if (z > 0)
        {
            bTouchFloor = kTrue;

            if ((lohit & 0xC000) == 0xC000)
            {
                // Path A
                short nFloorSprite = lohit & 0x3FFF;

                if (pSprite->statnum == 100 && sprite[nFloorSprite].statnum != 0 && sprite[nFloorSprite].statnum < 100)
                {
                    short nDamage = (z >> 9);
                    if (nDamage)
                    {
                        runlist_DamageEnemy(nFloorSprite, nSprite, nDamage << 1);
                    }

                    pSprite->zvel = -z;
                }
                else
                {
                    if (sprite[nFloorSprite].statnum == 0 || sprite[nFloorSprite].statnum > 199)
                    {
                        nRet |= 0x20000;
                    }
                    else
                    {
                        nRet |= lohit;
                    }

                    pSprite->zvel = 0;
                }
            }
            else
            {
                // Path B
                if (SectBelow[pSprite->sectnum] == -1)
                {
                    nRet |= 0x20000;

                    uint8_t const kexHazards = KexHazard[pSprite->sectnum];
                    short nSectDamage = SectDamage[pSprite->sectnum];
                    if (g_kexMode && pSprite->statnum == 100)
                    {
                        // GroundMove resets each timer when its floor lacks
                        // that flag. A first floor contact after falling
                        // resets instead of dealing an immediate hazard tick.
                        KexHazard_ResetPlayer(nSprite,
                            (kKexHazardLava | kKexHazardSlime) & ~kexHazards);
                        if (kexHazards & kKexHazardLava)
                            KexHazard_DamagePlayer(nSprite, kKexHazardLava,
                                                   kexWasAirborne);
                        if (kexHazards & kKexHazardSlime)
                            KexHazard_DamagePlayer(nSprite, kKexHazardSlime,
                                                   kexWasAirborne);
                        // The tag-96 marker may still be an ordinary native
                        // damaging sector in a converted map. Only the two
                        // reserved KEX face markers bypass its classic path.
                        if (kexHazards != kKexHazardNone)
                            nSectDamage = 0;
                    }
                    if (nSectDamage != 0)
                    {
                        if (pSprite->hitag < 15)
                        {
                            IgniteSprite(nSprite);
                            pSprite->hitag = 20;
                        }
                        nSectDamage >>= 2;
                        nSectDamage = nSectDamage - (nSectDamage>>2);
                        if (nSectDamage) {
                            runlist_DamageEnemy(nSprite, -1, nSectDamage);
                        }
                    }

                    pSprite->zvel = 0;
                }
            }
        }

        // loc_1543B:
        ebp = mySprfloor;
        pSprite->z = mySprfloor;
    }
    else
    {
        if ((ebp - height) < sprceiling && ((hihit & 0xC000) == 0xC000 || SectAbove[pSprite->sectnum] == -1))
        {
            ebp = sprceiling + height;
            nRet |= 0x10000;
        }
    }

    if (spriteZ <= floorZ && ebp > floorZ)
    {
        if ((SectDepth[nSector] != 0) || (edi != nSector && (SectFlag[edi] & kSectUnderwater)))
        {
            assert(nSector >= 0 && nSector < kMaxSectors);
            BuildSplash(nSprite, nSector);
        }
    }

    pSprite->cstat = cstat; // restore cstat
    pSprite->z = ebp;

    if (pSprite->statnum == 100)
    {
        BuildNear(pSprite->x, pSprite->y, clipdist + (clipdist / 2), pSprite->sectnum);
        nRet |= BelowNear(nSprite);
    }

    return nRet;
}

int GetSpriteHeight(int nSprite)
{
    return tilesiz[sprite[nSprite].picnum].y * sprite[nSprite].yrepeat * 4;
}

int movesprite(short nSprite, int dx, int dy, int dz, int UNUSED(ceildist), int flordist, unsigned int clipmask)
{
    spritetype *pSprite = &sprite[nSprite];
    bTouchFloor = kFalse;

    if (g_kexMode && pSprite->statnum == 100 && KexForce_StunMove(nSprite))
        return 0;

    int x = pSprite->x;
    int y = pSprite->y;
    int z = pSprite->z;

    int nSpriteHeight = GetSpriteHeight(nSprite);

    int nClipDist = (int8_t)pSprite->clipdist << 2;

    short nSector = pSprite->sectnum;
    assert(nSector >= 0 && nSector < kMaxSectors);

    int floorZ = sector[nSector].floorz;
    int const kexWasAirborne = g_kexMode && pSprite->statnum == 100
        && pSprite->z < getflorzofslope(nSector, pSprite->x, pSprite->y);

    int nRet = 0;

    if ((SectFlag[nSector] & kSectUnderwater) || (floorZ < z))
    {
        dx >>= 1;
        dy >>= 1;
    }

    nRet |= movespritez(nSprite, dz, nSpriteHeight, flordist, nClipDist, kexWasAirborne);

    nSector = pSprite->sectnum; // modified in movespritez so re-grab this variable

    if (pSprite->statnum == 100)
    {
        short nPlayer = GetPlayerFromSprite(nSprite);

        int varA = 0;
        int varB = 0;

        CheckSectorFloor(overridesect, pSprite->z, &varB, &varA);

        if (varB || varA)
        {
            nXDamage[nPlayer] = varB;
            nYDamage[nPlayer] = varA;
        }

        dx += nXDamage[nPlayer];
        dy += nYDamage[nPlayer];
    }
    else
    {
        CheckSectorFloor(overridesect, pSprite->z, &dx, &dy);
    }

    KexWall_ClipBegin(nSprite, clipmask);
    nRet |= (uint16_t)clipmove_old(&pSprite->x, &pSprite->y, &pSprite->z, &nSector, dx, dy, nClipDist, nSpriteHeight, flordist, clipmask);
    KexWall_ClipEnd();

    // kexPuppet::OnCollide runs the same hazard callbacks for a solid wall
    // face as for a floor face.  The converter writes its source face flag to
    // the private wall.extra values below; ordinary Build wall collisions are
    // intentionally unchanged.
    if (g_kexMode && pSprite->statnum == 100 && (nRet & 0xC000) == 0x8000)
    {
        int const nWall = nRet & 0x3FFF;
        if ((unsigned)nWall < (unsigned)numwalls)
        {
            // kexPuppet::OnCollide handles a force field before evaluating
            // its lava/slime flags. A Scepter collision permanently removes
            // only the linked field group; otherwise it electrocutes the
            // player at the remaster's contact cadence.
            KexForce_OnWallHit(nSprite, nWall);
            uint8_t const hazard = wall[nWall].extra == kKexHazardWallLava
                ? kKexHazardLava : wall[nWall].extra == kKexHazardWallSlime
                ? kKexHazardSlime : kKexHazardNone;
            if (hazard != kKexHazardNone)
                KexHazard_DamagePlayer(nSprite, hazard,
                    hazard == kKexHazardSlime && kexWasAirborne);
        }
    }

    if ((nSector != pSprite->sectnum) && nSector >= 0)
    {
        if (nRet & 0x20000) {
            dz = 0;
        }

        if ((sector[nSector].floorz - z) < (dz + flordist))
        {
            pSprite->x = x;
            pSprite->y = y;
        }
        else
        {
            mychangespritesect(nSprite, nSector);

            if (pSprite->pal < 5 && !pSprite->hitag)
            {
                pSprite->pal = sector[pSprite->sectnum].ceilingpal;
            }
        }
    }

    return nRet;
}

void Gravity(short nSprite)
{
    short nSector = sprite[nSprite].sectnum;

    if (SectFlag[nSector] & kSectUnderwater)
    {
        if (sprite[nSprite].statnum != 100)
        {
            if (sprite[nSprite].zvel <= 1024)
            {
                if (sprite[nSprite].zvel < 2048) {
                    sprite[nSprite].zvel += 512;
                }
            }
            else
            {
                sprite[nSprite].zvel -= 64;
            }
        }
        else
        {
            if (sprite[nSprite].zvel > 0)
            {
                sprite[nSprite].zvel -= 64;
                if (sprite[nSprite].zvel < 0) {
                    sprite[nSprite].zvel = 0;
                }
            }
            else if (sprite[nSprite].zvel < 0)
            {
                sprite[nSprite].zvel += 64;
                if (sprite[nSprite].zvel > 0) {
                    sprite[nSprite].zvel = 0;
                }
            }
        }
    }
    else
    {
        sprite[nSprite].zvel += 512;
        if (sprite[nSprite].zvel > 16384) {
            sprite[nSprite].zvel = 16384;
        }
    }
}

int KexScaleCreatureMove(short nSprite, int nMove)
{
    if (!g_kexMode || KexDifficulty_Get() != kKexDifficultyPharaoh
        || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return nMove;

    // Converted KEX AIs occupy the original creature statnums. Player 100 is
    // deliberately excluded, as are bullets, chunks, props, and map movers.
    int const stat = sprite[nSprite].statnum;
    if (stat != 99 && (stat < 101 || stat > 122))
        return nMove;

    // kexSlaveAI's constructor multiplies moveSpeed by 1.1 on Pharaoh.
    return (int)((int64_t)nMove * 11 / 10);
}

int MoveCreature(short nSprite)
{
    return movesprite(nSprite,
                      KexScaleCreatureMove(nSprite, sprite[nSprite].xvel << 8),
                      KexScaleCreatureMove(nSprite, sprite[nSprite].yvel << 8),
                      sprite[nSprite].zvel, 15360, -5120, CLIPMASK0);
}

int MoveCreatureWithCaution(int nSprite)
{
    int x = sprite[nSprite].x;
    int y = sprite[nSprite].y;
    int z = sprite[nSprite].z;
    short nSectorPre = sprite[nSprite].sectnum;

    int ecx = MoveCreature(nSprite);

    short nSector = sprite[nSprite].sectnum;

    if (nSector != nSectorPre)
    {
        int zDiff = sector[nSectorPre].floorz - sector[nSector].floorz;
        if (zDiff < 0) {
            zDiff = -zDiff;
        }

        if (zDiff > 15360 || (SectFlag[nSector] & kSectUnderwater) || (SectBelow[nSector] > -1 && SectFlag[SectBelow[nSector]]) || SectDamage[nSector])
        {
            sprite[nSprite].x = x;
            sprite[nSprite].y = y;
            sprite[nSprite].z = z;

            mychangespritesect(nSprite, nSectorPre);

            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
            sprite[nSprite].xvel = Sin(sprite[nSprite].ang + 512) >> 2;
            sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> 2;
            return 0;
        }
    }

    return ecx;
}

int GetAngleToSprite(int nSprite1, int nSprite2)
{
    if (nSprite1 < 0 || nSprite2 < 0)
        return -1;

    return GetMyAngle(sprite[nSprite2].x - sprite[nSprite1].x, sprite[nSprite2].y - sprite[nSprite1].y);
}

int PlotCourseToSprite(int nSprite1, int nSprite2)
{
    if (nSprite1 < 0 || nSprite2 < 0)
        return -1;

    int x = sprite[nSprite2].x - sprite[nSprite1].x;
    int y = sprite[nSprite2].y - sprite[nSprite1].y;

    sprite[nSprite1].ang = GetMyAngle(x, y);

    uint32_t x2 = klabs(x);
    uint32_t y2 = klabs(y);

    uint32_t diff = x2 * x2 + y2 * y2;

    if (diff > INT_MAX)
    {
        OSD_Printf("%s %d: overflow\n", EDUKE32_FUNCTION, __LINE__);
        diff = INT_MAX;
    }

    return ksqrt(diff);
}

int KexContactRadius2(short nSprite)
{
    // kexPlayerPuppet hardcodes radius 95.25 while the classic Build player
    // needs clipdist 58 for its own movement. Do not use that smaller native
    // collision proxy for remaster scripts such as A_CheckMelee and
    // A_SpiderPushAway. In twice-Build units, 95.25 * WORLD(6) is 1143.
    static constexpr int kKexPlayerContactRadius2 = 1143;
    if ((unsigned)nSprite < (unsigned)MAXSPRITES && sprite[nSprite].statnum == 100)
        return kKexPlayerContactRadius2;

    // Every converted non-player creature is assigned its authored radius as
    // Build clipdist * 4. Double it here to retain one half Build unit.
    return (unsigned)nSprite < (unsigned)MAXSPRITES ? sprite[nSprite].clipdist * 8 : 0;
}

static bool KexMeleeRangeAtTargetZ(short nSprite, short nTarget,
                                   int nReach, int64_t targetZ);

int KexMeleeRange(short nSprite, short nTarget, int nReach)
{
    // A_CheckMelee compares the two raw origins, even for a flying actor.
    int64_t const targetZ = (unsigned)nTarget < (unsigned)MAXSPRITES
        ? sprite[nTarget].z : 0;
    return KexMeleeRangeAtTargetZ(nSprite, nTarget, nReach, targetZ);
}

static bool KexMeleeRangeAtTargetZ(short nSprite, short nTarget,
                                   int nReach, int64_t targetZ)
{
    if ((unsigned)nSprite >= (unsigned)MAXSPRITES
        || (unsigned)nTarget >= (unsigned)MAXSPRITES
        || !(sprite[nTarget].cstat & 0x101) || nReach < 0)
        return 0;

    // Script distances use WORLD=6; Build z uses 16 units per XY unit. Keep
    // all components in twice-Build-XY units so the player puppet's authored
    // 95.25 KEX radius remains exact. Check each axis first so even corrupt
    // coordinates cannot overflow the squared-distance calculation.
    int64_t const radius = (int64_t)KexContactRadius2(nSprite)
        + KexContactRadius2(nTarget) + (int64_t)nReach * 12;
    int64_t const dx = ((int64_t)sprite[nTarget].x - sprite[nSprite].x) * 2;
    int64_t const dy = ((int64_t)sprite[nTarget].y - sprite[nSprite].y) * 2;
    int64_t const dz = (targetZ - sprite[nSprite].z) / 8;

    if (dx < -radius || dx > radius || dy < -radius || dy > radius
        || dz < -radius || dz > radius)
        return 0;

    return dx * dx + dy * dy + dz * dz <= radius * radius;
}

static bool KexAIFlying(short nSprite)
{
    // Every KEX caller of the common melee bridge currently has a dedicated
    // status list. AI_Fish is the only one whose KEX definition has flying.
    return (unsigned)nSprite < (unsigned)MAXSPRITES
        && sprite[nSprite].statnum == 103;
}

int KexActorHeightBuild(short nSprite)
{
    // KexDrop_ActorHeight is the central source-definition table for every
    // native remaster controller (including the Wasp/Bird/Seedling identity
    // packed into hitag). Do not derive this from rendered repeat: KEX traces
    // and flying melee callbacks operate on logical actor dimensions.
    if ((unsigned)nSprite >= (unsigned)MAXSPRITES)
        return 0;
    if (sprite[nSprite].statnum == 100) // kexPuppet, not an enemy drop actor
        return 160 * 16 * 6;
    if (sprite[nSprite].statnum == 121)
    {
        // The exact Queen body, snake head and seed pod deliberately share
        // one native status list but retain KEX heights 256, 48 and 32.
        if (sprite[nSprite].clipdist == 48)
            return 48 * 16 * 6;
        if (sprite[nSprite].clipdist == 54)
            return 32 * 16 * 6;
    }
    int const height = KexDrop_ActorHeight(nSprite);
    return height > 0 ? height * 16 * 6 : GetSpriteHeight(nSprite);
}

bool KexMeleeAttackRange(short nSprite, short nTarget, int nReach)
{
    int64_t targetZ = (unsigned)nTarget < (unsigned)MAXSPRITES
        ? sprite[nTarget].z : 0;
    // A_MeleeAttack lifts the target by half *the target's* logical height
    // for flying attackers. AI_Fish is the only current bridge caller.
    if (KexAIFlying(nSprite))
        targetZ -= KexActorHeightBuild(nTarget) / 2;
    return KexMeleeRangeAtTargetZ(nSprite, nTarget, nReach, targetZ);
}

bool KexAIWithinSightDistance(int xDistance, int yDistance)
{
    // kexAI's constructor and definition fallback use sightDistance = 3000.
    // KEX maps use six Build X/Y units per authored world unit. Check the
    // strict source boundary directly instead of inheriting the native
    // controller's various 40/50/60/100/1000-unit search radii.
    static constexpr int kKexAISightDistance = 3000 * 6;
    return xDistance > -kKexAISightDistance && xDistance < kKexAISightDistance
        && yDistance > -kKexAISightDistance && yDistance < kKexAISightDistance;
}

static bool KexAILookAllAround(short nSprite)
{
    if ((unsigned)nSprite >= (unsigned)MAXSPRITES)
        return false;

    // Exact KEX definition flags for every native-controller actor that can
    // use FindPlayer. Anubis, Mummy, Bird, Set and Selkis retain kexAI's
    // 1.04-radian forward cone; the rest wake from every direction.
    switch (sprite[nSprite].statnum)
    {
        case  99: // AI_Spider / AI_Scorp
        case 103: // AI_Fish
        case 104: // AI_Bastet
        case 105: // AI_Sentry
        case 118: // AI_Mantis
        case 121: // AI_Queen / snake host
            return true;
        case 107: // AI_Wasp / AI_Bird / AI_Seedling
            return sprite[nSprite].hitag != 1; // only AI_Bird lacks the flag
        default:
            return false;
    }
}

static int s_kexAcquireTest = -1;

static void KexAcquire_TestTick()
{
    if (s_kexAcquireTest < 0)
        s_kexAcquireTest = g_kexMode && Bgetenv("VB_KEX_COMMON_ENEMY_TEST") ? 1 : 0;
    if (!s_kexAcquireTest)
        return;
    s_kexAcquireTest = 0;

    // InitPlayer has completed by this point. Move only the puppet's
    // coordinates inside its current sector and restore its whole sprite
    // before the simulation advances.
    short const nPlayer = nTotalPlayers > 0 ? PlayerList[nLocalPlayer].nSprite : -1;
    bool acquireOkay = (unsigned)nPlayer < (unsigned)MAXSPRITES
        && sprite[nPlayer].sectnum >= 0;
    short nProbe = -1;
    if (acquireOkay)
    {
        SPRITE const savedPlayer = sprite[nPlayer];
        nProbe = insertsprite(savedPlayer.sectnum, 101); // AI_Anubis
        acquireOkay = nProbe >= 0;
        if (acquireOkay)
        {
            sprite[nPlayer].x = savedPlayer.x + 16;
            sprite[nPlayer].y = savedPlayer.y;
            sprite[nPlayer].z = savedPlayer.z;
            sprite[nPlayer].cstat = (savedPlayer.cstat | 0x101) & ~0x8000;
            sprite[nProbe].x = savedPlayer.x;
            sprite[nProbe].y = savedPlayer.y;
            sprite[nProbe].z = savedPlayer.z;
            sprite[nProbe].cstat = 0x101;
            sprite[nProbe].ang = getangle(16, 0);
            acquireOkay = FindPlayer(nProbe, 0) == nPlayer;
            sprite[nProbe].ang = (sprite[nProbe].ang + 339) & kAngleMask;
            acquireOkay = acquireOkay && FindPlayer(nProbe, 0) == -1;
            sprite[nPlayer] = savedPlayer;
        }
    }
    if (nProbe >= 0)
        mydeletesprite(nProbe);
    OSD_Printf("Kex common enemy acquisition test: %s "
               "(logical-centre LOS and 1.04-radian forward cone)\n",
               acquireOkay ? "PASS" : "FAIL");
}

bool KexAIEnterMeleeRange(short nSprite, short nTarget, int nReach)
{
    if ((unsigned)nSprite >= (unsigned)MAXSPRITES
        || (unsigned)nTarget >= (unsigned)MAXSPRITES
        || sprite[nSprite].sectnum < 0 || sprite[nTarget].sectnum < 0
        || !(sprite[nTarget].cstat & 0x101))
        return false;

    int64_t targetZ = sprite[nTarget].z;
    if (KexAIFlying(nSprite))
    {
        // kexAI::CheckMeleeRange lifts the target by the *source* stepHeight.
        // AI_Fish defines 28 source units, which maps to 2688 Build Z units.
        targetZ -= 28 * 16 * 6;
    }
    if (!KexMeleeRangeAtTargetZ(nSprite, nTarget, nReach, targetZ))
        return false;

    int const dx = sprite[nTarget].x - sprite[nSprite].x;
    int const dy = sprite[nTarget].y - sprite[nSprite].y;
    if (!KexAIWithinSightDistance(dx, dy))
        return false;
    if (!KexAILookAllAround(nSprite)
        && AngleDiff(sprite[nSprite].ang, getangle(dx, dy)) > 338)
        return false;

    // CheckTargetSight traces from the centres of the logical actors, not
    // from their render extents. Build Z is down-positive.
    int const sourceZ = sprite[nSprite].z - KexActorHeightBuild(nSprite) / 2;
    int const visibleTargetZ = sprite[nTarget].z
        - KexActorHeightBuild(nTarget) / 2;
    return cansee(sprite[nSprite].x, sprite[nSprite].y, sourceZ,
                  sprite[nSprite].sectnum, sprite[nTarget].x,
                  sprite[nTarget].y, visibleTargetZ,
                  sprite[nTarget].sectnum) != 0;
}

int FindPlayer(int nSprite, int nDistance)
{
    int var_18 = 0;
    if (nSprite >= 0)
        var_18 = 1;

    if (nSprite < 0)
        nSprite = -nSprite;

    if (nDistance < 0)
        nDistance = 100;

    int x = sprite[nSprite].x;
    int y = sprite[nSprite].y;
    short nSector = sprite[nSprite].sectnum;

    int z = g_kexMode
        ? sprite[nSprite].z - KexActorHeightBuild(nSprite) / 2
        : sprite[nSprite].z - GetSpriteHeight(nSprite);

    nDistance <<= 8;

    short nPlayerSprite;
    int i = 0;

    while (1)
    {
        if (i >= nTotalPlayers)
            return -1;

        nPlayerSprite = PlayerList[i].nSprite;

        if ((sprite[nPlayerSprite].cstat & 0x101) && (!(sprite[nPlayerSprite].cstat & 0x8000)))
        {
            int const dx = sprite[nPlayerSprite].x - x;
            int const dy = sprite[nPlayerSprite].y - y;
            bool const withinSight = g_kexMode
                ? KexAIWithinSightDistance(dx, dy)
                : klabs(dx) < nDistance && klabs(dy) < nDistance;

            if (withinSight && (!g_kexMode || KexAILookAllAround(nSprite)
                || AngleDiff(sprite[nSprite].ang, getangle(dx, dy)) <= 338))
            {
                int const targetZ = g_kexMode
                    ? sprite[nPlayerSprite].z - KexActorHeightBuild(nPlayerSprite) / 2
                    : sprite[nPlayerSprite].z - 7680;
                if (cansee(sprite[nPlayerSprite].x, sprite[nPlayerSprite].y,
                            targetZ, sprite[nPlayerSprite].sectnum,
                            x, y, z, nSector))
                {
                    break;
                }
            }
        }

        i++;
    }

    if (var_18) {
        PlotCourseToSprite(nSprite, nPlayerSprite);
    }

    return nPlayerSprite;
}

void CheckSectorFloor(short nSector, int z, int *x, int *y)
{
    short nSpeed = SectSpeed[nSector];

    if (!nSpeed) {
        return;
    }

    short nFlag = SectFlag[nSector];
    short nAng = nFlag & kAngleMask;

    if (z >= sector[nSector].floorz)
    {
        *x += (Cos(nAng) << 3) * nSpeed;
        *y += (sintable[nAng] << 3) * nSpeed; // no anglemask in original code
    }
    else if (nFlag & 0x800)
    {
        *x += (Cos(nAng) << 4) * nSpeed;
        *y += (sintable[nAng] << 4) * nSpeed; // no anglemask in original code
    }
}

int GetUpAngle(short nSprite1, int nVal, short nSprite2, int ecx)
{
    int x = sprite[nSprite2].x - sprite[nSprite1].x;
    int y = sprite[nSprite2].y - sprite[nSprite1].y;

    int ebx = (sprite[nSprite2].z + ecx) - (sprite[nSprite1].z + nVal);
    int edx = (sprite[nSprite2].z + ecx) - (sprite[nSprite1].z + nVal);

    ebx >>= 4;
    edx >>= 8;

    ebx = -ebx;

    ebx -= edx;

    int nSqrt = lsqrt(x * x + y * y);

    return GetMyAngle(nSqrt, ebx);
}

void InitPushBlocks()
{
    nPushBlocks = 0;
}

int GrabPushBlock()
{
    if (nPushBlocks >= kMaxPushBlocks) {
        return -1;
    }

    return nPushBlocks++;
}

void CreatePushBlock(int nSector)
{
    int nBlock = GrabPushBlock();
    int i;

    int startwall = sector[nSector].wallptr;
    int nWalls = sector[nSector].wallnum;

    int xSum = 0;
    int ySum = 0;

    for (i = 0; i < nWalls; i++)
    {
        xSum += wall[startwall + i].x;
        ySum += wall[startwall + i].y;
    }

    int xAvg = xSum / nWalls;
    int yAvg = ySum / nWalls;

    sBlockInfo[nBlock].x = xAvg;
    sBlockInfo[nBlock].y = yAvg;

    int nSprite = insertsprite(nSector, 0);

    sBlockInfo[nBlock].nSprite = nSprite;

    sprite[nSprite].x = xAvg;
    sprite[nSprite].y = yAvg;
    sprite[nSprite].z = sector[nSector].floorz - 256;
    sprite[nSprite].cstat = 0x8000;

    int var_28 = 0;

    for (i = 0; i < nWalls; i++)
    {
        uint32_t xDiff = klabs(xAvg - wall[startwall + i].x);
        uint32_t yDiff = klabs(yAvg - wall[startwall + i].y);

        uint32_t sqrtNum = xDiff * xDiff + yDiff * yDiff;

        if (sqrtNum > INT_MAX)
        {
            OSD_Printf("%s %d: overflow\n", EDUKE32_FUNCTION, __LINE__);
            sqrtNum = INT_MAX;
        }

        int nSqrt = ksqrt(sqrtNum);
        if (nSqrt > var_28) {
            var_28 = nSqrt;
        }
    }

    sBlockInfo[nBlock].walldist = var_28;

    sprite[nSprite].clipdist = (var_28 & 0xFF) << 2;
    sector[nSector].extra = nBlock;
}

void MoveSector(short nSector, int nAngle, int *nXVel, int *nYVel)
{
    int i;

    if (nSector == -1) {
        return;
    }

    int nXVect, nYVect;

    if (nAngle < 0)
    {
        nXVect = *nXVel;
        nYVect = *nYVel;
        nAngle = GetMyAngle(nXVect, nYVect);
    }
    else
    {
        nXVect = Sin(nAngle + 512) << 6;
        nYVect = Sin(nAngle) << 6;
    }

    short nBlock = sector[nSector].extra;
    short nSectFlag = SectFlag[nSector];

    sectortype *pSector = &sector[nSector];
    int nFloorZ = sector[nSector].floorz;
    int startwall = sector[nSector].wallptr;
    int nWalls = sector[nSector].wallnum;

    walltype *pStartWall = &wall[startwall];
    short nNextSector = wall[startwall].nextsector;

    BlockInfo *pBlockInfo = &sBlockInfo[nBlock];

    int x = sBlockInfo[nBlock].x;
    int x_b = sBlockInfo[nBlock].x;

    int y = sBlockInfo[nBlock].y;
    int y_b = sBlockInfo[nBlock].y;

    short nSectorB = nSector;

    int nZVal;
    int z;

    int bUnderwater = nSectFlag & kSectUnderwater;

    if (nSectFlag & kSectUnderwater)
    {
        nZVal = sector[nSector].ceilingz;
        z = sector[nNextSector].ceilingz + 256;

        sector[nSector].ceilingz = sector[nNextSector].ceilingz;
    }
    else
    {
        nZVal = sector[nSector].floorz;
        z = sector[nNextSector].floorz - 256;

        sector[nSector].floorz = sector[nNextSector].floorz;
    }

    clipmove_old((int32_t*)&x, (int32_t*)&y, (int32_t*)&z, &nSectorB, nXVect, nYVect, pBlockInfo->walldist, 0, 0, CLIPMASK1);

    int yvect = y - y_b;
    int xvect = x - x_b;

    if (nSectorB != nNextSector && nSectorB != nSector)
    {
        yvect = 0;
        xvect = 0;
    }
    else
    {
        if (!bUnderwater)
        {
            z = nZVal;
            x = x_b;
            y = y_b;

            clipmove_old((int32_t*)&x, (int32_t*)&y, (int32_t*)&z, &nSectorB, nXVect, nYVect, pBlockInfo->walldist, 0, 0, CLIPMASK1);

            int ebx = x;
            int ecx = x_b;
            int edx = y;
            int eax = xvect;
            int esi = y_b;

            if (eax < 0) {
                eax = -eax;
            }

            ebx -= ecx;
            ecx = eax;
            eax = ebx;
            edx -= esi;

            if (eax < 0) {
                eax = -eax;
            }

            if (ecx > eax)
            {
                xvect = ebx;
            }

            eax = yvect;
            if (eax < 0) {
                eax = -eax;
            }

            ebx = eax;
            eax = edx;

            if (eax < 0) {
                eax = -eax;
            }

            if (ebx > eax) {
                yvect = edx;
            }
        }
    }

    // GREEN
    if (yvect || xvect)
    {
        for (i = headspritesect[nSector]; i != -1; i = nextspritesect[i])
        {
            if (sprite[i].statnum < 99)
            {
                sprite[i].x += xvect;
                sprite[i].y += yvect;
            }
            else
            {
                z = sprite[i].z;

                if ((nSectFlag & kSectUnderwater) || z != nZVal || sprite[i].cstat & 0x8000)
                {
                    x = sprite[i].x;
                    y = sprite[i].y;
                    nSectorB = nSector;

                    clipmove_old((int32_t*)&x, (int32_t*)&y, (int32_t*)&z, &nSectorB, -xvect, -yvect, 4 * sprite[i].clipdist, 0, 0, CLIPMASK0);

                    if (nSectorB >= 0 && nSectorB < kMaxSectors && nSectorB != nSector) {
                        mychangespritesect(i, nSectorB);
                    }
                }
            }
        }

        for (i = headspritesect[nNextSector]; i != -1; i = nextspritesect[i])
        {
            if (sprite[i].statnum >= 99)
            {
                x = sprite[i].x;
                y = sprite[i].y;
                z = sprite[i].z;
                nSectorB = nNextSector;

                clipmove_old((int32_t*)&x, (int32_t*)&y, (int32_t*)&z, &nSectorB,
                    -xvect - (Sin(nAngle + 512) * (4 * sprite[i].clipdist)),
                    -yvect - (Sin(nAngle) * (4 * sprite[i].clipdist)),
                    4 * sprite[i].clipdist, 0, 0, CLIPMASK0);


                if (nSectorB != nNextSector && (nSectorB == nSector || nNextSector == nSector))
                {
                    if (nSectorB != nSector || nFloorZ >= sprite[i].z)
                    {
                        if (nSectorB >= 0 && nSectorB < kMaxSectors) {
                            mychangespritesect(i, nSectorB);
                        }
                    }
                    else
                    {
                        movesprite(i,
                            (xvect << 14) + Sin(nAngle + 512) * sprite[i].clipdist,
                            (yvect << 14) + Sin(nAngle) * sprite[i].clipdist,
                            0, 0, 0, CLIPMASK0);
                    }
                }
            }
        }

        for (int i = 0; i < nWalls; i++)
        {
            dragpoint(startwall, xvect + pStartWall->x, yvect + pStartWall->y, 0);
            pStartWall++;
            startwall++;
        }

        pBlockInfo->x += xvect;
        pBlockInfo->y += yvect;
    }

    // loc_163DD
    xvect <<= 14;
    yvect <<= 14;

    if (!(nSectFlag & kSectUnderwater))
    {
        for (i = headspritesect[nSector]; i != -1; i = nextspritesect[i])
        {
            if (sprite[i].statnum >= 99 && nZVal == sprite[i].z && !(sprite[i].cstat & 0x8000))
            {
                nSectorB = nSector;
                clipmove_old(&sprite[i].x, &sprite[i].y, &sprite[i].z, &nSectorB, xvect, yvect, 4 * sprite[i].clipdist, 5120, -5120, CLIPMASK0);
            }
        }
    }

    if (nSectFlag & kSectUnderwater) {
        pSector->ceilingz = nZVal;
    }
    else {
        pSector->floorz = nZVal;
    }

    *nXVel = xvect;
    *nYVel = yvect;

    /*
        Update player position variables, in case the player sprite was moved by a sector.
        Otherwise these can be out of sync when used in sound code (before being updated in PlayerFunc()).
        Can cause local player sounds to play off-centre.
        TODO: Might need to be done elsewhere too?
    */
    int nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
    initx = sprite[nPlayerSprite].x;
    inity = sprite[nPlayerSprite].y;
    initz = sprite[nPlayerSprite].z;
    inita = sprite[nPlayerSprite].ang;
    initsect = sprite[nPlayerSprite].sectnum;
}

void SetQuake(short nSprite, int nVal)
{
    int x = sprite[nSprite].x;
    int y = sprite[nSprite].y;

    nVal *= 256;

    for (int i = 0; i < nTotalPlayers; i++)
    {
        int nPlayerSprite = PlayerList[i].nSprite;

        uint32_t xDiff = klabs((int32_t)((sprite[nPlayerSprite].x - x) >> 8));
        uint32_t yDiff = klabs((int32_t)((sprite[nPlayerSprite].y - y) >> 8));

        uint32_t sqrtNum = xDiff * xDiff + yDiff * yDiff;

        if (sqrtNum > INT_MAX)
        {
            OSD_Printf("%s %d: overflow\n", EDUKE32_FUNCTION, __LINE__);
            sqrtNum = INT_MAX;
        }

        int nSqrt = ksqrt(sqrtNum);

        int eax = nVal;

        if (nSqrt)
        {
            eax = eax / nSqrt;

            if (eax >= 256)
            {
                if (eax > 3840) {
                    eax = 3840;
                }
            }
            else
            {
                eax = 0;
            }
        }

        if (eax > nQuake[i]) {
            nQuake[i] = eax;
        }
    }
}

int AngleChase(int nSprite, int nSprite2, int ebx, int ecx, int push1)
{
    ebx = KexScaleCreatureMove((short)nSprite, ebx);
    int nClipType = sprite[nSprite].statnum != 107;

    /* bjd - need to handle cliptype to clipmask change that occured in later build engine version */
    if (nClipType == 1) {
        nClipType = CLIPMASK1;
    }
    else {
        nClipType = CLIPMASK0;
    }

    short nAngle;

    if (nSprite2 < 0)
    {
        sprite[nSprite].zvel = 0;
        nAngle = sprite[nSprite].ang;
    }
    else
    {
        int nHeight = tilesiz[sprite[nSprite2].picnum].y * sprite[nSprite2].yrepeat * 2;

        int nMyAngle = GetMyAngle(sprite[nSprite2].x - sprite[nSprite].x, sprite[nSprite2].y - sprite[nSprite].y);

        uint32_t xDiff = klabs(sprite[nSprite2].x - sprite[nSprite].x);
        uint32_t yDiff = klabs(sprite[nSprite2].y - sprite[nSprite].y);

        uint32_t sqrtNum = xDiff * xDiff + yDiff * yDiff;

        if (sqrtNum > INT_MAX)
        {
            OSD_Printf("%s %d: overflow\n", EDUKE32_FUNCTION, __LINE__);
            sqrtNum = INT_MAX;
        }

        int nSqrt = ksqrt(sqrtNum);

        int var_18 = GetMyAngle(nSqrt, ((sprite[nSprite2].z - nHeight) - sprite[nSprite].z) >> 8);

        int nAngDelta = AngleDelta(sprite[nSprite].ang, nMyAngle, 1024);
        int nAngDelta2 = klabs(nAngDelta);

        if (nAngDelta2 > 63)
        {
            nAngDelta2 = klabs(nAngDelta >> 6);

            ebx /= nAngDelta2;

            if (ebx < 5) {
                ebx = 5;
            }
        }

        int nAngDeltaC = klabs(nAngDelta);

        if (nAngDeltaC > push1)
        {
            if (nAngDelta >= 0)
                nAngDelta = push1;
            else
                nAngDelta = -push1;
        }

        nAngle = (nAngDelta + sprite[nSprite].ang) & kAngleMask;
        int nAngDeltaD = AngleDelta(sprite[nSprite].zvel, var_18, 24);

        sprite[nSprite].zvel = (sprite[nSprite].zvel + nAngDeltaD) & kAngleMask;
    }

    sprite[nSprite].ang = nAngle;

    int eax = klabs(Cos(sprite[nSprite].zvel));

    int x = ((Cos(nAngle) * ebx) >> 14) * eax;
    int y = ((Sin(nAngle) * ebx) >> 14) * eax;

    int xshift = x >> 8;
    int yshift = y >> 8;

    // VibeBuild32: the squares overflow for a fast flyer (the crows past
    // ~512 units a move logged "overflow" some 1700 times a level in CAVERN
    // and had their length clamped, flattening the dive). The length has a
    // closed form -- x and y are (cos, sin) * ebx * eax, so their norm is
    // |ebx| * eax, shifted as they were -- which is used when the squares
    // would not fit; the exact path is kept where it always worked.
    int64_t const sq = (int64_t)xshift * xshift + (int64_t)yshift * yshift;
    int const len = (sq <= INT_MAX) ? (int)ksqrt((uint32_t)sq)
                                    : (int)(((int64_t)klabs(ebx) * eax) >> 22);

    int z = Sin(sprite[nSprite].zvel) * len;

    return movesprite(nSprite, x >> 2, y >> 2, (z >> 13) + (Sin(ecx) >> 5), 0, 0, nClipType);
}

int GetWallNormal(short nWall)
{
    nWall &= kMaxWalls-1;

    int nWall2 = wall[nWall].point2;

    int nAngle = GetMyAngle(wall[nWall2].x - wall[nWall].x, wall[nWall2].y - wall[nWall].y);
    return (nAngle + 512) & kAngleMask;
}

void WheresMyMouth(int nPlayer, int *x, int *y, int *z, short *sectnum)
{
    int nSprite = PlayerList[nPlayer].nSprite;

    *x = sprite[nSprite].x;
    *y = sprite[nSprite].y;

    int height = GetSpriteHeight(nSprite) / 2;

    *z = sprite[nSprite].z - height;
    *sectnum = sprite[nSprite].sectnum;

    clipmove_old((int32_t*)x, (int32_t*)y, (int32_t*)z, sectnum,
        Cos(sprite[nSprite].ang) << 7,
        Sin(sprite[nSprite].ang) << 7,
        5120, 1280, 1280, CLIPMASK1);
}

void InitChunks()
{
    nCurChunkNum = 0;
    memset(nChunkSprite,   -1, sizeof(nChunkSprite));
    memset(nBodyGunSprite, -1, sizeof(nBodyGunSprite));
    memset(nBodySprite,    -1, sizeof(nBodySprite));
    nCurBodyNum    = 0;
    nCurBodyGunNum = 0;
    nBodyTotal  = 0;
    nChunkTotal = 0;
}

int GrabBodyGunSprite()
{
    int nSprite = nBodyGunSprite[nCurBodyGunNum];

    if (nSprite == -1)
    {
        nSprite = insertsprite(0, 899);
        nBodyGunSprite[nCurBodyGunNum] = nSprite;

        sprite[nSprite].lotag = -1;
        sprite[nSprite].owner = -1;
    }
    else
    {
        int nAnim = sprite[nSprite].owner;

        if (nAnim != -1) {
            DestroyAnim(nAnim);
        }

        sprite[nSprite].lotag = -1;
        sprite[nSprite].owner = -1;
    }

    nCurBodyGunNum++;
    if (nCurBodyGunNum >= 50) { // TODO - enum/define
        nCurBodyGunNum = 0;
    }

    sprite[nSprite].cstat = 0;

    return nSprite;
}

int GrabBody()
{
    int nSprite;

    do
    {
        nSprite = nBodySprite[nCurBodyNum];

        if (nSprite == -1)
        {
            nSprite = insertsprite(0, 899);
            nBodySprite[nCurBodyNum] = nSprite;
            sprite[nSprite].cstat = 0x8000;
        }

        nCurBodyNum++;
        if (nCurBodyNum >= 50) {
            nCurBodyNum = 0;
        }
    } while (sprite[nSprite].cstat & 0x101);

    if (nBodyTotal < 50) {
        nBodyTotal++;
    }

    sprite[nSprite].cstat = 0;
    return nSprite;
}

int GrabChunkSprite()
{
    int nSprite = nChunkSprite[nCurChunkNum];

    if (nSprite == -1)
    {
        nSprite = insertsprite(0, 899);
        nChunkSprite[nCurChunkNum] = nSprite;
    }
    else if (sprite[nSprite].statnum)
    {
// TODO	MonoOut("too many chunks being used at once!\n");
        return -1;
    }

    changespritestat(nSprite, 899);

    nCurChunkNum++;
    if (nCurChunkNum >= kMaxChunks)
        nCurChunkNum = 0;

    if (nChunkTotal < kMaxChunks)
        nChunkTotal++;

    sprite[nSprite].cstat = 0x80;

    return nSprite;
}

static int GrabKexOverflowChunkSprite()
{
    // Do not expand or retarget the classic ring: its cursor and allocation
    // state are serialized for original maps. These KEX-only pieces carry a
    // self-identifying marker and delete themselves when their toss ends.
    int const nSprite = insertsprite(0, 899);
    if (nSprite < 0)
        return -1;
    sprite[nSprite].cstat = 0x80;
    return nSprite;
}

static int BuildCreatureChunkImpl(int nVal, int nPic, int kexTossMode,
                                  KexTossBounceSound bouncySound = kKexTossBounceSoundGlass)
{
    int var_14;

    int nSprite = GrabChunkSprite();
    bool bKexOverflow = false;

    if (nSprite < 0 && g_kexMode && kexTossMode)
    {
        nSprite = GrabKexOverflowChunkSprite();
        bKexOverflow = nSprite >= 0;
    }

    if (nSprite == -1) {
        return -1;
    }

    if (nVal & 0x4000)
    {
        nVal &= 0x3FFF;
        var_14 = 1;
    }
    else
    {
        var_14 = 0;
    }

    nVal &= 0xFFFF;

    sprite[nSprite].x = sprite[nVal].x;
    sprite[nSprite].y = sprite[nVal].y;
    sprite[nSprite].z = sprite[nVal].z;

    mychangespritesect(nSprite, sprite[nVal].sectnum);

    sprite[nSprite].cstat = 0x80;
    sprite[nSprite].shade = -12;
    sprite[nSprite].pal = 0;

    if (kexTossMode)
    {
        // actionDef.cpp:kexActionTossActor spawns at origin+(0,0,32), then
        // adds Range(-8,8), Range(-8,8), Range(4,16).  Build Z increases
        // downward, hence the negative origin/launch conversion.
        sprite[nSprite].z -= 32 * kKexTossZUnit;
        KexTossPreserveClassicRandomCursor();
        sprite[nSprite].xvel = KexTossRange(-8 * kKexTossXYVelocityUnit,
                                             8 * kKexTossXYVelocityUnit);
        sprite[nSprite].yvel = KexTossRange(-8 * kKexTossXYVelocityUnit,
                                             8 * kKexTossXYVelocityUnit);
        sprite[nSprite].zvel = KexTossRange(-16 * kKexTossZUnit,
                                             -4 * kKexTossZUnit);
    }
    else
    {
        sprite[nSprite].xvel = (RandomSize(5) - 16) << 7;
        sprite[nSprite].yvel = (RandomSize(5) - 16) << 7;
        sprite[nSprite].zvel = (-(RandomSize(8) + 512)) << 3;

        if (var_14)
        {
            sprite[nSprite].xvel *= 4;
            sprite[nSprite].yvel *= 4;
            sprite[nSprite].zvel *= 2;
        }
    }

    sprite[nSprite].xrepeat = 64;
    sprite[nSprite].yrepeat = 64;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = nPic;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].clipdist = kexTossMode
        ? KexTossClipdist(nPic, kexTossMode == 2) : 40;

//	GrabTimeSlot(3);

    sprite[nSprite].extra = kexTossMode == 2
        ? (bKexOverflow ? kKexTossOverflowBouncyChunkMarker
                        : kKexTossBouncyChunkMarker) - bouncySound
        : kexTossMode ? (bKexOverflow ? kKexTossOverflowChunkMarker
                                      : kKexTossChunkMarker) : -1;
    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nSprite, kRunCreatureChunk);
    sprite[nSprite].hitag = runlist_AddRunRec(NewRun, nSprite, kRunCreatureChunk);

    return nSprite | 0xD0000; // TODO: FIXME
}

int BuildCreatureChunk(int nVal, int nPic)
{
    return BuildCreatureChunkImpl(nVal, nPic, 0);
}

int KexTossCreatureChunk(int nVal, int nPic)
{
    // The KEX path is opt-in at the exact action call sites.  Keep this guard
    // so tools or older maps that invoke it outside KEX mode remain classic.
    return g_kexMode ? BuildCreatureChunkImpl(nVal, nPic, 1)
                     : BuildCreatureChunk(nVal, nPic);
}

int KexTossBouncyCreatureChunk(int nVal, int nPic, KexTossBounceSound sound)
{
    return g_kexMode ? BuildCreatureChunkImpl(nVal, nPic, 2, sound)
                     : BuildCreatureChunk(nVal, nPic);
}

static void DestroyCreatureChunk(short nSprite)
{
    bool const bKexOverflow = KexTossIsOverflowMarker(sprite[nSprite].extra);
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(sprite[nSprite].hitag);

    if (bKexOverflow)
    {
        mydeletesprite(nSprite);
        return;
    }
    changespritestat(nSprite, 0);
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = 0;
}

void FuncCreatureChunk(int a, int UNUSED(b), int nRun)
{
    int nSprite = RunData[nRun].nVal;
    assert(nSprite >= 0 && nSprite < kMaxSprites);

    int nMessage = a & 0x7F0000;

    if (nMessage != 0x20000)
        return;

    bool const bouncyToss = KexTossIsBouncyMarker(sprite[nSprite].extra);
    bool const gibToss = sprite[nSprite].extra == kKexTossChunkMarker
        || sprite[nSprite].extra <= kKexTossGibExpireBase;
    if (g_kexMode && (bouncyToss || gibToss))
    {
        int const bouncySound = bouncyToss
            ? KexTossBouncySoundForMarker(sprite[nSprite].extra) : -1;
        for (int sourceStep = 0; sourceStep < 2; sourceStep++)
        {
            // Tick() runs UpdateSprite (and therefore A_DestroyAtRest) before
            // UpdateMovement. Creature gibs have no such action: expires=1
            // decrements at the end of precisely the source tick that stops.
            if (bouncyToss && KexTossAtRest(nSprite))
            {
                DestroyCreatureChunk(nSprite);
                return;
            }
            KexTossSourcePhysicsStep(nSprite, bouncySound, sourceStep);
            if (!bouncyToss && KexTossAtRest(nSprite)
                && KexTossGibExpireTick(nSprite))
            {
                DestroyCreatureChunk(nSprite);
                return;
            }
        }
        int const nSector = sprite[nSprite].sectnum;
        if ((unsigned)nSector < (unsigned)numsectors)
            sprite[nSprite].pal = sector[nSector].ceilingpal;
        return;
    }

    Gravity(nSprite);

    int nSector = sprite[nSprite].sectnum;
    sprite[nSprite].pal = sector[nSector].ceilingpal;

    int nVal = movesprite(nSprite, sprite[nSprite].xvel << 10, sprite[nSprite].yvel << 10, sprite[nSprite].zvel, 2560, -2560, CLIPMASK1);

    if (sprite[nSprite].z >= sector[nSector].floorz)
    {
        // re-grab this variable as it may have changed in movesprite(). Note the check above is against the value *before* movesprite so don't change it.
        nSector = sprite[nSprite].sectnum;

        sprite[nSprite].xvel = 0;
        sprite[nSprite].yvel = 0;
        sprite[nSprite].zvel = 0;
        sprite[nSprite].z = sector[nSector].floorz;
    }
    else
    {
        if (!nVal)
            return;

        short nAngle;

        if (nVal & 0x20000)
        {
            sprite[nSprite].cstat = 0x8000;
        }
        else
        {
            if ((nVal & 0x3C000) == 0x10000)
            {
                sprite[nSprite].xvel >>= 1;
                sprite[nSprite].yvel >>= 1;
                sprite[nSprite].zvel = -sprite[nSprite].zvel;
                return;
            }
            else if ((nVal & 0x3C000) == 0xC000)
            {
                nAngle = sprite[nVal & 0x3FFF].ang;
            }
            else if ((nVal & 0x3C000) == 0x8000)
            {
                nAngle = GetWallNormal(nVal & 0x3FFF);
            }
            else
            {
                return;
            }

            // loc_16E0C
            int nSqrt = lsqrt(((sprite[nSprite].yvel >> 10) * (sprite[nSprite].yvel >> 10)
                + (sprite[nSprite].xvel >> 10) * (sprite[nSprite].xvel >> 10)) >> 8);

            sprite[nSprite].xvel = Cos(nAngle) * (nSqrt >> 1);
            sprite[nSprite].yvel = Sin(nAngle) * (nSqrt >> 1);
            return;
        }
    }

    DestroyCreatureChunk(nSprite);
}

short UpdateEnemy(short *nEnemy)
{
    if (*nEnemy >= 0)
    {
        if (!(sprite[*nEnemy].cstat & 0x101)) {
            *nEnemy = -1;
        }
    }

    return *nEnemy;
}

class MoveLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void MoveLoadSave::Load()
{
    Read(&NearCount, sizeof(NearCount));
    Read(NearSector, sizeof(NearSector[0]) * NearCount);
    Read(&nPushBlocks, sizeof(nPushBlocks));
    Read(sBlockInfo, sizeof(sBlockInfo[0]) * nPushBlocks);
    Read(&overridesect, sizeof(overridesect));

    Read(&nCurBodyNum, sizeof(nCurBodyNum));
    // An ordinary EXSV only restores the prefix constructed by the legacy
    // single-player loader.  EXSN instead resumes a live multiplayer world:
    // the cursor can point at any reusable body/chunk/gun slot, so leaving the
    // tail from the staging level would make a later allocation diverge.
    Read(nBodySprite, LoadSave::IsNetworkSnapshot()
        ? sizeof(nBodySprite) : sizeof(nBodySprite[0]) * nCurBodyNum);
    Read(&nBodyTotal, sizeof(nBodyTotal));

    if (LoadSave::IsNetworkSnapshot())
    {
        Read(&nCurBodyGunNum, sizeof(nCurBodyGunNum));
        Read(nBodyGunSprite, sizeof(nBodyGunSprite));
    }

    Read(&hihit, sizeof(hihit));
    Read(&sprceiling, sizeof(sprceiling));
    Read(&sprfloor, sizeof(sprfloor));
    Read(&lohit, sizeof(lohit));

    Read(&nCurChunkNum, sizeof(nCurChunkNum));
    Read(nChunkSprite, LoadSave::IsNetworkSnapshot()
        ? sizeof(nChunkSprite) : sizeof(nChunkSprite[0]) * nCurChunkNum);
    if (LoadSave::IsNetworkSnapshot())
        Read(&nChunkTotal, sizeof(nChunkTotal));
}

void MoveLoadSave::Save()
{
    Write(&NearCount, sizeof(NearCount));
    Write(NearSector, sizeof(NearSector[0]) * NearCount);
    Write(&nPushBlocks, sizeof(nPushBlocks));
    Write(sBlockInfo, sizeof(sBlockInfo[0]) * nPushBlocks);
    Write(&overridesect, sizeof(overridesect));

    Write(&nCurBodyNum, sizeof(nCurBodyNum));
    Write(nBodySprite, LoadSave::IsNetworkSnapshot()
        ? sizeof(nBodySprite) : sizeof(nBodySprite[0]) * nCurBodyNum);
    Write(&nBodyTotal, sizeof(nBodyTotal));

    if (LoadSave::IsNetworkSnapshot())
    {
        Write(&nCurBodyGunNum, sizeof(nCurBodyGunNum));
        Write(nBodyGunSprite, sizeof(nBodyGunSprite));
    }

    Write(&hihit, sizeof(hihit));
    Write(&sprceiling, sizeof(sprceiling));
    Write(&sprfloor, sizeof(sprfloor));
    Write(&lohit, sizeof(lohit));

    Write(&nCurChunkNum, sizeof(nCurChunkNum));
    Write(nChunkSprite, LoadSave::IsNetworkSnapshot()
        ? sizeof(nChunkSprite) : sizeof(nChunkSprite[0]) * nCurChunkNum);
    if (LoadSave::IsNetworkSnapshot())
        Write(&nChunkTotal, sizeof(nChunkTotal));
}

static MoveLoadSave* myLoadSave;

void MoveLoadSaveConstruct()
{
    myLoadSave = new MoveLoadSave();
}
