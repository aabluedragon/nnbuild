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
#include "bullet.h"
#include "runlist.h"
#include "anims.h"
#include "sequence.h"
#include "exhumed.h"
#include "sound.h"
#include "init.h"
#include "move.h"
#include "player.h"
#include "trigdat.h"
#include "random.h"
#include "gun.h"
#include "names.h"
#include "lighting.h"
#include "object.h"
#include "save.h"
#include "kexbreak.h"   // NNBuild: KexSound_Get

static int KexBulletTile(int nType)
{
    if (!g_kexMode)
        return -1;
    if (nType == 8)
        return kKexAnubisFireTile;
    if (nType == 10 || nType == 14)
        return kKexFireballTile;
    if (nType == 13)
        return kKexBlasterTile;
    if (nType == 16)
        return kKexSelkisCobraTile;
    return -1;
}

// Several KEX attacks reuse native bullet records. Keep their shipped damage
// values without changing those records for classic gameplay: the player's
// Pistol/M60 hit-scans (types 1/2) are 15/20 rather than the DOS 25/15,
// Proj_Anubis (type 8) is 20, Proj_Fireball (type 10) is 100, and
// Proj_Blaster (type 13) is 30 and the laser-spawner's Proj_Beam (type 15) is
// 100. Both red cobras (types 9/16) have zero direct damage but a separate
// 50-point death blast; impactWallsOnly governs planes, not actor collision.
static int KexBulletDamage(int nType)
{
    if (g_kexMode)
    {
        if (nType == 1)
            return 15;
        if (nType == 2)
            return 20;
        if (nType == 8)
            return 20;
        if (nType == 9)
            return 0;
        if (nType == 10)
            return 100;
        if (nType == 14)
            return 100;
        if (nType == 15)
            return 100;
        if (nType == 11)
            return 40;
        if (nType == 13)
            return 30;
        if (nType == 16)
            return 0;
        if (nType == 19)
            return 5;
    }

    return BulletInfo[nType].nDamage;
}

// A_HitScan's first argument is a finite KEX-world reach. The classic
// instant-hit bullet records use it only as a large-value sentinel and hence
// trace indefinitely; retain that behavior outside KEX mode.
static int KexHitScanRange(int nType)
{
    if (!g_kexMode)
        return 0;
    // pistol_fire: A_HitScan(3000, ...)
    if (nType == 1)
        return 3000;
    // m60_fire: A_HitScan(4000, ...)
    if (nType == 2)
        return 4000;
    return 0;
}

static bool KexHitScanInRange(int nType, int x0, int y0, int z0,
                              int x1, int y1, int z1)
{
    int const range = KexHitScanRange(nType);
    if (!range)
        return true;

    // Converted XY is six Build units per KEX unit while Z is 16*6. Divide
    // the latter by sixteen so all three dimensions share the six-unit scale.
    int64_t const dx = (int64_t)x1 - x0;
    int64_t const dy = (int64_t)y1 - y0;
    int64_t const dz = ((int64_t)z1 - z0) / 16;
    int64_t const limit = (int64_t)range * 6;
    return dx * dx + dy * dy + dz * dz <= limit * limit;
}

static int KexBulletRadius(int nType)
{
    if (!g_kexMode)
        return -1;
    switch (nType)
    {
        case 8:  // Proj_Anubis
        case 11: // Proj_Soul
        case 12: // Proj_Seedling
        case 13: // Proj_Blaster
        case 15: // Proj_Beam (laser spawner)
        case 19: // Proj_Flame (player flamethrower)
            return 36;
        case 9:  // Proj_Cobra_Red
        case 16: // Proj_Cobra_Red_Selkis
            return 16;
        case 10: // Proj_Fireball (Mantis)
        case 14: // Proj_Fireball (wall spawner)
        case 17: // Proj_Boulder
            return 25;
        default:
            return -1;
    }
}

static int KexBulletRepeat(int nType, int kexRadius, int classicRepeat)
{
    if (kexRadius < 0)
        return classicRepeat;
    // Proj_Boulder is authored at scale 2.0; the other bridged projectiles
    // are scale 1.0. Forty is the native projection baseline for that scale.
    if (nType == 17)
        return 80;
    // Proj_Flame starts at the definition's 1/64 scale, then its script
    // grows it as it fizzles. A one-repeat Build sprite is that first pixel.
    return nType == 19 ? 1 : 40;
}

struct KexImpactBlast
{
    int radius;                 // retail KEX units
    int damage;                 // retail logical health points
    bool opensToggleFaces;
};

// Projectile actor damage and death-animation damage are separate in KEX.
// In particular, both red cobras deliberately have direct damage zero but
// their authored death animation still runs A_RadialBlast(224, 50, false).
static KexImpactBlast KexBulletImpactBlast(int nType)
{
    if (g_kexMode)
    {
        switch (nType)
        {
            case 9:             // Proj_Cobra_Red
            case 16:            // Proj_Cobra_Red_Selkis
                return { 224, 50, false };
            case 10:            // Proj_Fireball (Mantis)
            case 14:            // Proj_Fireball (wall spawner)
            case 17:            // Proj_Boulder
                return { 192, 64, true };
        }
    }
    return { 0, 0, false };
}

static int KexBulletBuildBlastRadius(int kexRadius)
{
    // runlist compares (|dx|+|dy|)>>8 with this value. Converted map XY is
    // six times retail KEX, hence the same conversion used by breakables.
    return kexRadius * 6 / 256;
}

static bool KexBulletGlidesOnPlanes(int nType)
{
    // KEX's impactWallsOnly lets these projectiles follow floors/ceilings;
    // they still collide with walls and actors.  Both cobra definitions and
    // Proj_Soul carry the flag in defs/actors/projectiles.txt.
    return g_kexMode && (nType == 9 || nType == 11 || nType == 15 || nType == 16);
}

static bool KexBulletRedCobra(int nType)
{
    return g_kexMode && (nType == 9 || nType == 16);
}

static bool KexBulletFacesTargetWithoutThrust(int nType)
{
    // Both red cobras are homing projectiles with noHomingThrust. Generic
    // homing only updates their orientation; A_CobraSlither then supplies
    // their actual velocity from that orientation.
    return KexBulletRedCobra(nType);
}

static int KexBulletTurnStep(int nType)
{
    // homingTurnAngles from projectiles.txt, converted to Build's 2048-angle
    // circle: Proj_Soul .0218 rad ~= 7, Mummy cobra .03 ~= 10, and Selkis
    // cobra .028 ~= 9.
    if (g_kexMode && nType == 11)
        return 7;
    if (g_kexMode && nType == 9)
        return 10;
    if (g_kexMode && nType == 16)
        return 9;
    return 16;
}
#include <string.h>
#include <assert.h>
#ifndef __WATCOMC__
//#include <cmath>
#else
//#include <math.h>
#include <stdlib.h>
#endif

#define kMaxBullets		500

short BulletFree[kMaxBullets];

// 32 bytes
struct Bullet
{
    short nSeq;
    short nFrame;
    short nSprite;
    short field_6;
    short field_8;
    short nType;
    short field_C;
    short field_E;
    uint16_t field_10;
    uint8_t field_12;
    uint8_t field_13;
    int x;
    int y;
    int z;
};

enum
{
    kKexBoulderType = 17,
    kKexFlameType = 19,
    kKexSpawnerFireballSpeed = 12 * 6,
    kKexSpawnerLaserSpeed = 16 * 6,
    kKexSpawnerFireballHeight = 32 * 16 * 6,
    kKexSpawnerFireballStepHeight = 36 * 16 * 6,
    kKexSpawnerLaserHeight = 8 * 16 * 6,
    kKexBoulderGravity = 72,       // 0.75 KEX units in Build's 16*6 Z scale
    kKexBoulderFloorContacts = 8,
    kKexBoulderSpeed = kKexSpawnerFireballSpeed,
    kKexBoulderHeight = 32 * 16 * 6,
    kKexBoulderStepHeight = 36 * 16 * 6,
    kKexBoulderBounceNumerator = 31, // authored bouncePower 1.9375
    kKexBoulderBounceDenominator = 16,
};

Bullet BulletList[kMaxBullets];
short nBulletEnemy[kMaxBullets];
short s_kexCobraPitch[kMaxBullets]; // KEX actor rotation pitch, not sprite art
short s_kexCobraJoint[kMaxBullets][5];
int nBulletsFree;
int lasthitz, lasthitx, lasthity;
short lasthitsect, lasthitsprite, lasthitwall;

int nBulletCount = 0;
short nRadialBullet = 0;

static void KexRedCobraDestroyJoints(short nBullet);

enum
{
    kKexImpactUnavailable = -1,
    kKexImpactHandledNoAnim = -2,
};

static bool KexBulletExactImpactAvailable(int nType)
{
    if (!g_kexMode)
        return false;
    switch (nType)
    {
        case 8:
            return seq_IsLoaded(kSeqKexAnuImpact);
        case kKexFlameType:
            return seq_IsLoaded(kSeqKexFlame);
        case 9:
        case 11:
        case 16:
            return seq_IsLoaded(kSeqKexCobraRed)
                && seq_IsLoaded(kSeqKexCobraCtl);
        case 10:
        case 12:
        case 14:
        case 17:
            return seq_IsLoaded(kSeqKexImpact);
        case 13: // sentry_projectile_destroy: A_Destroy only
        case 15: // beam_destroy: destroy tails, then A_Destroy
            return true;
        default:
            return false;
    }
}

static bool KexBulletUsesRemasterDeath(int nType)
{
    // Every native slot from Anubis through the remaster-only boulder is
    // bridged to an explicit KEX projectile death recipe. None may inherit a
    // DOS BulletInfo radial blast or actor-hit puff by accident.
    return g_kexMode && ((nType >= 8 && nType <= kKexBoulderType)
                         || nType == kKexFlameType);
}

static bool KexBulletSuppressClassicRadial(int nType)
{
    return KexBulletUsesRemasterDeath(nType);
}

static void KexProjectileImpactSound(int x, int y, int z, int nSector); // below

int KexProjectile_SpawnFireballImpact(int x, int y, int z, short nSector,
                                      int nKexZOffset, int bSoundBefore)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexImpact)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    if (bSoundBefore)
        KexProjectileImpactSound(x, y, z, nSector);
    int const nAnim = BuildAnim(-1, kSeqKexImpact, 0, x, y,
        z - nKexZOffset * 16 * 6, nSector, 48, 4);
    if (!bSoundBefore)
        KexProjectileImpactSound(x, y, z, nSector);
    return nAnim >= 0 ? nAnim : kKexImpactHandledNoAnim;
}

static int KexCobraRandomOffset(int amplitude)
{
    // kexSlaveRandomizer::RFloatVal returns a centred uniform value. Consume
    // one full 16-bit draw per axis and keep this simulation state integral.
    return (int)(((int64_t)RandomSize(16) * amplitude * 2) / 65535)
         - amplitude;
}

static int KexProjectileSpawnCobraPuff(int x, int y, int z, short nSector,
                                      bool red)
{
    int const px = x + KexCobraRandomOffset(32 * 6);
    int const py = y + KexCobraRandomOffset(32 * 6);
    int const pz = z + KexCobraRandomOffset(32 * 16 * 6);
    int const nSeq = red ? kSeqKexCobraRed : kSeqKexCobraGreen;
    int const nAnim = BuildAnim(-1, nSeq, 0, px, py, pz, nSector, 48, 4);
    // Both callbacks attach pbexplode.wav to the puff only when spawning the
    // actor succeeded.
    if (nAnim >= 0)
        KexProjectileImpactSound(px, py, pz, nSector);
    return nAnim;
}

int KexProjectile_StartCobraBurst(int x, int y, int z, short nSector,
                                  int bRed)
{
    int const nPuffSeq = bRed ? kSeqKexCobraRed : kSeqKexCobraGreen;
    if (!g_kexMode || !seq_IsLoaded(nPuffSeq)
            || !seq_IsLoaded(kSeqKexCobraCtl)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    // Frame zero's callback is immediate. A hidden twelve-tick Anim keeps the
    // remaining t=3/6/9 callbacks in the ordinary serialized Anim state and
    // disappears at the same t=12 at which the KEX projectile destroys.
    int const nController = BuildAnim(-1, kSeqKexCobraCtl, 0, x, y, z,
                                      nSector, 1, 0);
    if (nController < 0)
        return kKexImpactHandledNoAnim;
    short const nControllerSprite = GetAnimSprite(nController);
    sprite[nControllerSprite].cstat |= 0x8000;
    sprite[nControllerSprite].xvel = bRed != 0;
    KexProjectileSpawnCobraPuff(x, y, z, nSector, bRed != 0);
    return nController;
}

void KexProjectile_CobraBurstFrame(short nControllerSprite, int nFrame)
{
    if (!g_kexMode || (unsigned)nControllerSprite >= (unsigned)kMaxSprites
            || sprite[nControllerSprite].sectnum < 0
            || (nFrame != 2 && nFrame != 5 && nFrame != 8))
        return;
    KexProjectileSpawnCobraPuff(sprite[nControllerSprite].x,
        sprite[nControllerSprite].y, sprite[nControllerSprite].z,
        sprite[nControllerSprite].sectnum, sprite[nControllerSprite].xvel != 0);
}

static int KexBulletSpawnAnubisImpact(int x, int y, int z, short nSector)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexAnuImpact)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;
    int const nAnim = BuildAnim(-1, kSeqKexAnuImpact, 0, x, y, z,
                                nSector, 40, 4);
    return nAnim >= 0 ? nAnim : kKexImpactHandledNoAnim;
}

static int KexBulletSpawnFlameSizzle(int x, int y, int z, short nSector,
                                     int nRepeat, int nShade,
                                     bool bTranslucent)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexFlame)
            || (unsigned)nSector >= (unsigned)numsectors)
        return kKexImpactUnavailable;

    // flameproj_sizzle inherits the dying Proj_Flame's scale and colour. The
    // generated second sub-sequence is its 60-to-30 Hz visual cadence.
    int const nAnim = BuildAnim(-1, kSeqKexFlame, 1, x, y, z, nSector,
                                clamp(nRepeat, 1, 255),
                                4 | (bTranslucent ? 0x80 : 0));
    if (nAnim < 0)
        return kKexImpactHandledNoAnim;
    sprite[GetAnimSprite(nAnim)].shade = nShade;
    return nAnim;
}

static bool KexBulletHandleDeathVisual(short nBullet, int nType,
                                       int x, int y, int z, short nSector)
{
    if (!KexBulletExactImpactAvailable(nType))
        return false;
    switch (nType)
    {
        case 8:
            return KexBulletSpawnAnubisImpact(x, y, z, nSector)
                != kKexImpactUnavailable;
        case 9:
        case 11:
        case 16:
            return KexProjectile_StartCobraBurst(x, y, z, nSector, 1)
                != kKexImpactUnavailable;
        case 10:
        case 14:
            // fireball_death: sound, spawn, radial blast.
            return KexProjectile_SpawnFireballImpact(
                x, y, z, nSector, 0, 1) != kKexImpactUnavailable;
        case 12:
            // seedling_explode: +2 spawn, sound, then destroy; no radial.
            return KexProjectile_SpawnFireballImpact(
                x, y, z, nSector, 2, 0) != kKexImpactUnavailable;
        case 17:
            // boulder_explode: +2 spawn, sound, radial, four tosses.
            return KexProjectile_SpawnFireballImpact(
                x, y, z, nSector, 2, 0) != kKexImpactUnavailable;
        case 13:
        case 15:
            return true; // their authored death animations render nothing
        case kKexFlameType:
        {
            int nRepeat = 1;
            int nShade = -127;
            bool bTranslucent = false;
            if ((unsigned)nBullet < (unsigned)kMaxBullets)
            {
                short const nSprite = BulletList[nBullet].nSprite;
                if ((unsigned)nSprite < (unsigned)kMaxSprites
                        && sprite[nSprite].sectnum >= 0)
                {
                    nRepeat = sprite[nSprite].xrepeat;
                    nShade = sprite[nSprite].shade;
                    bTranslucent = (sprite[nSprite].cstat & 2) != 0;
                }
            }
            return KexBulletSpawnFlameSizzle(x, y, z, nSector,
                                              nRepeat, nShade, bTranslucent)
                != kKexImpactUnavailable;
        }
        default:
            return false;
    }
}

static bool KexBulletApplyImpactBlast(short nBullet, int x, int y, int z,
                                      short nSector, bool exactImpact)
{
    Bullet *const pBullet = &BulletList[nBullet];
    KexImpactBlast const blast = KexBulletImpactBlast(pBullet->nType);
    if (blast.damage <= 0 || (unsigned)nSector >= (unsigned)numsectors)
        return false;

    short const nSprite = pBullet->nSprite;
    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    if (sprite[nSprite].sectnum != nSector)
        mychangespritesect(nSprite, nSector);

    if (blast.opensToggleFaces)
        KexToggle_Blast(x, y, z, nSector, blast.radius);

    nRadialBullet = pBullet->nType;
    runlist_RadialDamageEnemy(nSprite, blast.damage,
                              KexBulletBuildBlastRadius(blast.radius));
    nRadialBullet = -1;
    // The exact effect owns its fixed-radius fourteen-tick light. Old
    // conversions retain the native long AddFlash fallback.
    if (!exactImpact)
        AddFlash(nSector, x, y, z, 128);

    if (pBullet->nType == 17)
    {
        if (!exactImpact)
            KexProjectileImpactSound(x, y, z, nSector);

        // boulder_explode tosses four Proj_Boulder_Piece actors after its
        // 192/64 blast. They use the same shared A_TossActor recipe as every
        // remaster gib and shard.
        for (int i = 0; i < 4; i++)
        {
            int const nChunk = KexTossBouncyCreatureChunk(nSprite,
                                                           kKexBoulderShardTile,
                                                           kKexTossBounceSoundGlass);
            short const nChunkSprite = nChunk & 0xffff;
            if ((unsigned)nChunkSprite < (unsigned)kMaxSprites)
            {
                sprite[nChunkSprite].xrepeat = 40;
                sprite[nChunkSprite].yrepeat = 40;
            }
        }
    }
    return true;
}

struct KexProjectileLaunch
{
    short nTarget;
    int nCallbackDistance;
    int nHeight;
    int nSpeed;
    bool bHomingThrust;
    bool bSuppressAudit = false;
};

struct KexProjectileExpected
{
    int x, y, z;
    int vx, vy, vz;
};

static int s_kexBulletAudit = -1;
static int s_kexBulletTest = -1;

// Deterministic 64-bit integer square root. Projectile aim is simulation
// state, so do not introduce platform-dependent floating-point normalization.
static uint64_t KexBulletISqrt(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value)
        bit >>= 2;
    while (bit)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
        {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint64_t KexBulletVectorLength(int64_t x, int64_t y, int64_t z)
{
    auto const abs64 = [](int64_t value) -> uint64_t {
        return value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
    };
    uint64_t biggest = max<uint64_t>(abs64(x),
        max<uint64_t>(abs64(y), abs64(z)));
    int shift = 0;
    while (biggest > UINT64_C(0x3fffffff))
    {
        x >>= 1;
        y >>= 1;
        z >>= 1;
        biggest >>= 1;
        shift++;
    }
    uint64_t const squared = (uint64_t)(x * x) + (uint64_t)(y * y)
                           + (uint64_t)(z * z);
    return KexBulletISqrt(squared) << shift;
}

static int KexSpawnerLaunchSpeed(short nSource, int nType)
{
    // kexmap.py marks a converted kexSlaveFireballSpawner with its negative
    // source-sector key in yvel. Do not change ordinary/classic trap bullets
    // merely because the game happens to be running in KEX mode.
    if (!g_kexMode || (unsigned)nSource >= (unsigned)kMaxSprites
            || sprite[nSource].yvel >= 0)
        return 0;
    if (nType == 15)
        return kKexSpawnerLaserSpeed;
    if (nType == 14 || nType == kKexBoulderType)
        return kKexSpawnerFireballSpeed;
    return 0;
}

static bool KexSpawnerProjectile(Bullet const *pBullet)
{
    short const nSprite = pBullet->nSprite;
    return (unsigned)nSprite < (unsigned)kMaxSprites
        && KexSpawnerLaunchSpeed(sprite[nSprite].owner, pBullet->nType) > 0;
}

static int KexBulletTargetCenterZ(short nTarget)
{
    // A_FireProjectile takes the target's logical kexActor::Height(), not
    // its rendered frame extent. In particular the 160-unit player puppet
    // must stay the exact same aim point while its sprites animate at
    // differing repeats.
    return sprite[nTarget].z - KexActorHeightBuild(nTarget) / 2;
}

static void KexBulletAimPitch(int x, int y, int z, short nTarget,
                              int *cosPitch, int *sinPitch)
{
    int64_t const dx = (int64_t)sprite[nTarget].x - x;
    int64_t const dy = (int64_t)sprite[nTarget].y - y;
    uint64_t const horizontal = KexBulletVectorLength(dx, dy, 0);
    int64_t const dzUp = ((int64_t)z - KexBulletTargetCenterZ(nTarget)) / 16;
    uint64_t const length = max<uint64_t>(1,
        KexBulletVectorLength(horizontal, 0, dzUp));
    *cosPitch = clamp<int>((int)((horizontal * 16384) / length), 0, 16384);
    *sinPitch = clamp<int>((int)((dzUp * 16384) / (int64_t)length),
                           -16384, 16384);
}

// Return a 14-bit unit direction. XY is kept on the source actor's authored
// yaw exactly as A_FireProjectile does; only pitch is solved from the target.
// Dividing Build Z by 16 puts it in the same converted metric as Build XY
// (both then carry the converter's common six-times KEX scale).
static void KexBulletAimDirection(int x, int y, int z, short nTarget,
                                  short nYaw, int *fx, int *fy, int *fz)
{
    int cosPitch, sinPitch;
    KexBulletAimPitch(x, y, z, nTarget, &cosPitch, &sinPitch);
    *fx = mulscale14(Cos(nYaw), cosPitch);
    *fy = mulscale14(Sin(nYaw), cosPitch);
    *fz = sinPitch;
}

static int KexRedCobraMaxPitch(int nType)
{
    // homingMaxPitch is .43 radians for Mummy and 1.0 for Selkis.
    return nType == 9 ? 140 : 326;
}

static int KexRedCobraTargetPitch(int x, int y, int z, short nTarget,
                                  int nType)
{
    int cosPitch, sinPitch;
    KexBulletAimPitch(x, y, z, nTarget, &cosPitch, &sinPitch);
    int pitch = GetMyAngle(cosPitch, sinPitch);
    if (pitch >= 1024)
        pitch -= 2048;
    return clamp(pitch, -KexRedCobraMaxPitch(nType),
                 KexRedCobraMaxPitch(nType));
}

static KexProjectileExpected KexBulletExpectedLaunch(short nSource,
    KexProjectileLaunch const &launch)
{
    int fx, fy, fz;
    short const nYaw = sprite[nSource].ang;
    KexBulletAimDirection(sprite[nSource].x, sprite[nSource].y,
                          sprite[nSource].z, launch.nTarget, nYaw,
                          &fx, &fy, &fz);

    // A_FireProjectile adds its fourth argument to actor->Radius(). Build's
    // clipdist is radius/4, while converted KEX XY coordinates are x6.
    int const distance = (sprite[nSource].clipdist << 2)
                       + launch.nCallbackDistance * 6;
    KexProjectileExpected expected;
    expected.x = sprite[nSource].x + (int)(((int64_t)fx * distance) / 16384);
    expected.y = sprite[nSource].y + (int)(((int64_t)fy * distance) / 16384);
    expected.z = sprite[nSource].z - (int)(((int64_t)fz * distance) / 16384)
               - launch.nHeight * 16 * 6;

    // KEX recalculates pitch from the actual projectile origin, then applies
    // the source yaw and the callback's speed.
    KexBulletAimDirection(expected.x, expected.y, expected.z, launch.nTarget,
                          nYaw, &fx, &fy, &fz);
    int const worldSpeed = launch.nSpeed * 6;
    expected.vx = fx * worldSpeed; // clipmove's XY deltas are 14-bit fixed
    expected.vy = fy * worldSpeed;
    expected.vz = -(int)(((int64_t)fz * worldSpeed * 16) / 16384);
    return expected;
}

bulletInfo BulletInfo[] = {
    { 25,   1,    20, -1, -1, 13, 0,  0, -1 },
    { 25,  -1, 65000, -1, 31, 73, 0,  0, -1 },
    { 15,  -1, 60000, -1, 31, 73, 0,  0, -1 },
    { 5,   15,  2000, -1, 14, 38, 4,  5,  3 },
    { 250, 100, 2000, -1, 33, 34, 4, 20, -1 },
    { 200, -1,  2000, -1, 20, 23, 4, 10, -1 },
    { 200, -1, 60000, 68, 68, -1, -1, 0, -1 },
    { 300,  1,     0, -1, -1, -1, 0, 50, -1 },
    { 18,  -1,  2000, -1, 18, 29, 4,  0, -1 },
    { 20,  -1,  2000, 37, 11, 30, 4,  0, -1 },
    { 25,  -1,  3000, -1, 44, 36, 4, 15, 90 },
    { 30,  -1,  1000, -1, 52, 53, 4, 20, 48 },
    { 20,  -1,  3500, -1, 54, 55, 4, 30, -1 },
    { 10,  -1,  5000, -1, 57, 76, 4,  0, -1 },
    { 40,  -1,  1500, -1, 63, 38, 4, 10, 40 },
    { 20,  -1,  2000, -1, 60, 12, 0,  0, -1 },
    { 5,   -1, 60000, -1, 31, 76, 0,  0, -1 },
    // NNBuild: 17, the remaster's boulder (Proj_Boulder, damage 35) as the
    // trap fireball's twin -- same speed, poof and radius -- on BOULDER.SEQ.
    { 35,  -1,  1500, -1, kSeqBoulder, kSeqFirePoof, 4, 10, 40 },
    // 18, the Manacle's bolt: the arrow (whose SEQ the converter draws as the
    // remaster's beam bolt) with Proj_Beam's damage.
    { 100, -1,  2000, -1, kSeqArrow, kSeqPoof, 0, 0, -1 },
    // 19, remaster-only Proj_Flame. The launch bridge supplies its 16-unit
    // velocity; this record owns its fifteen native-tick (30 KEX-tick)
    // fizzle lifetime and KEXFLAME.SEQ flight art. Its .5-speed source
    // animation executes A_UpdateFlame once per native tick, so the
    // 15-unit fizzle (increments of .5) lasts thirty native ticks.
    { 5,   30,  2000, -1, kSeqKexFlame, 0, 4, 0, 0 }
};


void InitBullets()
{
    nBulletCount = 0;
    s_kexBulletAudit = -1;
    s_kexBulletTest = -1;

    for (int i = 0; i < kMaxBullets; i++) {
        BulletFree[i] = i;
    }

    nBulletsFree = kMaxBullets;

    memset(nBulletEnemy, -1, sizeof(nBulletEnemy));
    memset(s_kexCobraPitch, 0, sizeof(s_kexCobraPitch));
    memset(s_kexCobraJoint, -1, sizeof(s_kexCobraJoint));
}

short GrabBullet()
{
    nBulletsFree--;
    return BulletFree[nBulletsFree];
}

void DestroyBullet(short nBullet)
{
    short nSprite = BulletList[nBullet].nSprite;

    KexRedCobraDestroyJoints(nBullet);
    runlist_DoSubRunRec(BulletList[nBullet].field_6);
    runlist_DoSubRunRec(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(BulletList[nBullet].field_8);

    StopSpriteSound(nSprite);

    mydeletesprite(nSprite);

    BulletFree[nBulletsFree] = nBullet;
    nBulletsFree++;
}

void IgniteSprite(int nSprite)
{
    sprite[nSprite].hitag += 2;

    int nAnim = BuildAnim(-1, 38, 0, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum, 40, 20);
    short nAnimSprite = GetAnimSprite(nAnim);

    sprite[nAnimSprite].hitag = nSprite;
    changespritestat(nAnimSprite, kStatIgnited);

    short yRepeat = (tilesiz[sprite[nAnimSprite].picnum].y * 32) / nFlameHeight;
    if (yRepeat < 1)
        yRepeat = 1;

    sprite[nAnimSprite].yrepeat = (uint8_t)yRepeat;
}

void BulletHitsSprite(Bullet *pBullet, short nBulletSprite, short nHitSprite, int x, int y, int z, int nSector)
{
    assert(nSector >= 0 && nSector < kMaxSectors);

    bulletInfo *pBulletInfo = &BulletInfo[pBullet->nType];
    bool const suppressClassicPuff = KexBulletUsesRemasterDeath(pBullet->nType);

    short nStat = sprite[nHitSprite].statnum;

    // Proj_Flame has the normal 5-point projectile hit, unlike the DOS
    // flamethrower's type-3 ignition-only hit. canIgnite still retains the
    // classic delayed burn bridge for actors that support it.
    if (pBullet->nType == kKexFlameType && nStat <= 107
            && nStat != kStatAnubisDrum)
    {
        sprite[nHitSprite].hitag++;
        if (sprite[nHitSprite].hitag == 15)
            IgniteSprite(nHitSprite);
    }

    switch (pBullet->nType)
    {
        case 3:
        {
            if (nStat > 107 || nStat == kStatAnubisDrum) {
                return;
            }

            sprite[nHitSprite].hitag++;

            if (sprite[nHitSprite].hitag == 15) {
                IgniteSprite(nHitSprite);
            }

            if (!RandomSize(2)) {
                BuildAnim(-1, pBulletInfo->field_C, 0, x, y, z, nSector, 40, pBulletInfo->nFlags);
            }

            return;
        }
        case 14:
        {
            if (nStat > 107 || nStat == kStatAnubisDrum) {
                return;
            }
            // else - fall through to below cases
            fallthrough__;
        }
        case 1:
        case 2:
        case 8:
        case 9:
        case 12:
        case 13:
        case 15:
        case 16:
        {
            // loc_29E59
            if (!nStat || nStat > 98) {
                break;
            }

            short nSprite = pBullet->nSprite;
            spritetype *pSprite = &sprite[nSprite];
            spritetype *pHitSprite = &sprite[nHitSprite];

            if (nStat == kStatAnubisDrum)
            {
                short nAngle = (pSprite->ang + 256) - RandomSize(9);

                pHitSprite->xvel = Cos(nAngle) << 1;
                pHitSprite->yvel = Sin(nAngle) << 1;
                pHitSprite->zvel = (-(RandomSize(3) + 1)) << 8;
            }
            else
            {
                int xVel = pHitSprite->xvel;
                int yVel = pHitSprite->yvel;

                pHitSprite->xvel = Cos(pSprite->ang) >> 2;
                pHitSprite->yvel = Sin(pSprite->ang) >> 2;

                MoveCreature(nHitSprite);

                pHitSprite->xvel = xVel;
                pHitSprite->yvel = yVel;
            }

            break;
        }

        default:
            break;
    }

    // BHS_switchBreak:
    short nDamage = KexBulletDamage(pBullet->nType);

    if (pBullet->field_13 > 1) {
        nDamage *= 2;
    }

    int const previousDamageType = nKexDamageType;
    if (g_kexMode && (pBullet->nType == 10 || pBullet->nType == 14
                      || pBullet->nType == kKexFlameType))
        nKexDamageType = kKexDamageFire;
    runlist_DamageEnemy(nHitSprite, nBulletSprite, nDamage);
    nKexDamageType = previousDamageType;

    if (nStat <= 90 || nStat >= 199)
    {
        if (!suppressClassicPuff)
            BuildAnim(-1, pBulletInfo->field_C, 0, x, y, z, nSector, 40, pBulletInfo->nFlags);
        return;
    }

    switch (nStat)
    {
        case kStatDestructibleSprite:
            break;
        case kStatAnubisDrum:
        case 102:
        case kStatExplodeTrigger:
        case kStatExplodeTarget:
            if (!suppressClassicPuff)
                BuildAnim(-1, 12, 0, x, y, z, nSector, 40, 0);
            break;
        default:
            if (!suppressClassicPuff)
            {
                BuildAnim(-1, 39, 0, x, y, z, nSector, 40, 0);
                if (pBullet->nType > 2)
                    BuildAnim(-1, pBulletInfo->field_C, 0, x, y, z,
                              nSector, 40, pBulletInfo->nFlags);
            }
            break;
    }
}


void BackUpBullet(int *x, int *y, short nAngle)
{
    *x -= Cos(nAngle) >> 11;
    *y -= Sin(nAngle) >> 11;
}

static int KexMoveSoulProjectile(short nBullet, short nTarget)
{
    Bullet *pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    int const desiredYaw = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                      sprite[nTarget].y - sprite[nSprite].y);
    int turn = AngleDelta(sprite[nSprite].ang, desiredYaw, 1024);
    turn = clamp(turn, -KexBulletTurnStep(11), KexBulletTurnStep(11));
    sprite[nSprite].ang = (sprite[nSprite].ang + turn) & kAngleMask;

    int fx, fy, fz;
    KexBulletAimDirection(sprite[nSprite].x, sprite[nSprite].y,
                          sprite[nSprite].z, nTarget, sprite[nSprite].ang,
                          &fx, &fy, &fz);
    // Proj_Soul omits homingMaxPitch and therefore uses KEX's 0.25-radian
    // default. Homing pitch snaps each tick; yaw alone uses the turn cap.
    constexpr int kSoulMaxPitchSin = 4053; // sin(0.25) * 16384
    constexpr int kSoulMaxPitchCos = 15875; // cos(0.25) * 16384
    if (klabs(fz) > kSoulMaxPitchSin)
    {
        fz = fz < 0 ? -kSoulMaxPitchSin : kSoulMaxPitchSin;
        fx = mulscale14(Cos(sprite[nSprite].ang), kSoulMaxPitchCos);
        fy = mulscale14(Sin(sprite[nSprite].ang), kSoulMaxPitchCos);
    }

    int const worldSpeed = max(1, pBullet->field_10 / 4);
    pBullet->x = fx * worldSpeed;
    pBullet->y = fy * worldSpeed;
    pBullet->z = -(int)(((int64_t)fz * worldSpeed * 16) / 16384);
    return movesprite(nSprite, pBullet->x, pBullet->y, pBullet->z,
                      sprite[nSprite].clipdist >> 1,
                      sprite[nSprite].clipdist >> 1, CLIPMASK1);
}

static void KexTurnProjectileWithoutThrust(short nBullet, short nTarget)
{
    Bullet *pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    int const desiredYaw = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                      sprite[nTarget].y - sprite[nSprite].y);
    int turn = AngleDelta(sprite[nSprite].ang, desiredYaw, 1024);
    int const turnStep = KexBulletTurnStep(pBullet->nType);
    turn = clamp(turn, -turnStep, turnStep);
    sprite[nSprite].ang = (sprite[nSprite].ang + turn) & kAngleMask;
    // The Cobra action separately derives its velocity from this rotation.
}

static void KexTurnRedCobra(short nBullet, short nTarget)
{
    Bullet *const pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    KexTurnProjectileWithoutThrust(nBullet, nTarget);

    int const wanted = KexRedCobraTargetPitch(sprite[nSprite].x,
        sprite[nSprite].y, sprite[nSprite].z, nTarget, pBullet->nType);
    int const delta = AngleDelta(s_kexCobraPitch[nBullet] & kAngleMask,
                                 wanted & kAngleMask, 1024);
    int pitch = s_kexCobraPitch[nBullet]
        + clamp(delta, -KexBulletTurnStep(pBullet->nType),
                KexBulletTurnStep(pBullet->nType));
    if (pitch >= 1024)
        pitch -= 2048;
    else if (pitch < -1024)
        pitch += 2048;
    s_kexCobraPitch[nBullet] = (short)pitch;
}

static int KexRedCobraJointRepeat(int nJoint)
{
    static int const repeat[] = { 40, 35, 30, 25, 20 };
    return (unsigned)nJoint < ARRAY_SIZE(repeat) ? repeat[nJoint] : 20;
}

static void KexRedCobraDestroyJoints(short nBullet)
{
    if ((unsigned)nBullet >= (unsigned)kMaxBullets)
        return;
    for (int i = 0; i < (int)ARRAY_SIZE(s_kexCobraJoint[nBullet]); i++)
    {
        int const nJoint = s_kexCobraJoint[nBullet][i];
        if ((unsigned)nJoint < (unsigned)kMaxSprites
            && sprite[nJoint].sectnum >= 0)
            mydeletesprite(nJoint);
        s_kexCobraJoint[nBullet][i] = -1;
    }
}

static void KexRedCobraCreateJoints(short nBullet)
{
    if ((unsigned)nBullet >= (unsigned)kMaxBullets)
        return;
    KexRedCobraDestroyJoints(nBullet);
    int const tile = KexProjectile_RedCobraJointTile();
    short const nSprite = BulletList[nBullet].nSprite;
    if (tile <= 0 || (unsigned)nSprite >= (unsigned)kMaxSprites
        || sprite[nSprite].sectnum < 0)
        return;
    for (int i = 0; i < (int)ARRAY_SIZE(s_kexCobraJoint[nBullet]); i++)
    {
        int const nJoint = insertsprite(sprite[nSprite].sectnum, 200);
        if (nJoint < 0)
            break;
        s_kexCobraJoint[nBullet][i] = nJoint;
        sprite[nJoint].x = sprite[nSprite].x;
        sprite[nJoint].y = sprite[nSprite].y;
        sprite[nJoint].z = sprite[nSprite].z;
        sprite[nJoint].cstat = 0;
        sprite[nJoint].shade = -64;
        sprite[nJoint].pal = 0;
        sprite[nJoint].xrepeat = sprite[nJoint].yrepeat
            = KexRedCobraJointRepeat(i);
        sprite[nJoint].clipdist = 1;
        sprite[nJoint].picnum = tile;
        sprite[nJoint].owner = -1;
        sprite[nJoint].extra = -1;
    }
}

static void KexRedCobraUpdateJoints(short nBullet)
{
    if ((unsigned)nBullet >= (unsigned)kMaxBullets)
        return;
    int nTarget = BulletList[nBullet].nSprite;
    if ((unsigned)nTarget >= (unsigned)kMaxSprites
        || sprite[nTarget].sectnum < 0)
        return;
    for (int i = 0; i < (int)ARRAY_SIZE(s_kexCobraJoint[nBullet]); i++)
    {
        int const nJoint = s_kexCobraJoint[nBullet][i];
        if ((unsigned)nJoint >= (unsigned)kMaxSprites
            || sprite[nJoint].sectnum < 0)
            continue;
        // A_UpdateCobraJoints lerps halfway toward its previous joint/head,
        // then links to that actor's current sector.
        sprite[nJoint].x += (sprite[nTarget].x - sprite[nJoint].x) / 2;
        sprite[nJoint].y += (sprite[nTarget].y - sprite[nJoint].y) / 2;
        sprite[nJoint].z += (sprite[nTarget].z - sprite[nJoint].z) / 2;
        if (sprite[nJoint].sectnum != sprite[nTarget].sectnum)
            mychangespritesect(nJoint, sprite[nTarget].sectnum);
        nTarget = nJoint;
    }
}

static void KexRedCobraSlither(short nBullet, int nKexTick)
{
    Bullet *const pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    constexpr int kCobraSide = 4 * 6;
    constexpr int kCobraPhaseQ16 = 5340354; // .25 radians in Build angles
    int const speed = pBullet->nType == 9 ? 14 * 6 : 16 * 6;
    int const yaw = sprite[nSprite].ang;
    int const phase = (int)(((int64_t)nKexTick * kCobraPhaseQ16 + 32768)
                            >> 16) & kAngleMask;
    int const side = Cos(phase);
    int const pitch = s_kexCobraPitch[nBullet];
    int const cosPitch = Cos(pitch);
    int const fx = mulscale14(Cos(yaw), cosPitch);
    int const fy = mulscale14(Sin(yaw), cosPitch);
    pBullet->x = fx * speed
        - (int)(((int64_t)Sin(yaw) * side * kCobraSide) / 16384);
    pBullet->y = fy * speed
        + (int)(((int64_t)Cos(yaw) * side * kCobraSide) / 16384);
    pBullet->z = -(int)(((int64_t)Sin(pitch) * speed * 16) / 16384);
}

static int KexMoveRedCobra(short nBullet, short nTarget)
{
    Bullet *const pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    // The animation's cobra_red_loop executes A_CobraSlither at 60 Hz;
    // MoveBullet runs at the native 30-Hz cadence, so integrate both source
    // actions. Mummy and Selkis select 14 and 16 KEX units respectively.
    for (int sub = 0; sub < 2; sub++)
    {
        if ((unsigned)nTarget < (unsigned)kMaxSprites
            && (sprite[nTarget].cstat & 0x101))
            KexTurnRedCobra(nBullet, nTarget);
        KexRedCobraSlither(nBullet, totalmoves * 2 + sub);

        int const nVal = movesprite(nSprite, pBullet->x, pBullet->y,
                                    pBullet->z, sprite[nSprite].clipdist >> 1,
                                    sprite[nSprite].clipdist >> 1, CLIPMASK1);
        if (nVal)
            return nVal;
        KexRedCobraUpdateJoints(nBullet);
    }
    return 0;
}

// KEX velocity uses one metric for all three axes. Build's clipmove XY is
// Q14 in coordinates scaled by six, while its Z delta is scaled by 16*6, so
// { x, y, -z*1024 } is the same fixed-point KEX vector on every axis.
static void KexBoulderReflect(Bullet *pBullet, int nx, int ny, int nz)
{
    int64_t vx = pBullet->x;
    int64_t vy = pBullet->y;
    int64_t vz = -(int64_t)pBullet->z * 1024;
    int64_t const dot = (vx * nx + vy * ny + vz * nz) / 16384;
    int64_t const projection = dot * kKexBoulderBounceNumerator
                             / kKexBoulderBounceDenominator;

    vx -= projection * nx / 16384;
    vy -= projection * ny / 16384;
    vz -= projection * nz / 16384;

    pBullet->x = (int)vx;
    pBullet->y = (int)vy;
    pBullet->z = (int)(-vz / 1024);
}

static void KexBoulderSlopeNormal(short nSector, int x, int y,
                                  bool ceiling, int *nx, int *ny, int *nz)
{
    // Converted KEX Z is -BuildZ/96 and XY is BuildXY/6. Sampling the
    // linear Build slope over +/-1024 therefore gives an unnormalised KEX
    // upward normal of { dzdx, dzdy, 32*1024 }. Its sign is immaterial to
    // reflection, so the same construction serves ceilings.
    constexpr int sample = 1024;
    auto const surface = [nSector, ceiling](int sx, int sy) {
        return ceiling ? getceilzofslope(nSector, sx, sy)
                       : getflorzofslope(nSector, sx, sy);
    };
    int64_t const rx = (int64_t)surface(x + sample, y)
                     - surface(x - sample, y);
    int64_t const ry = (int64_t)surface(x, y + sample)
                     - surface(x, y - sample);
    int64_t const rz = 32 * sample;
    uint64_t const length = max<uint64_t>(1,
        KexBulletVectorLength(rx, ry, rz));
    *nx = (int)(rx * 16384 / (int64_t)length);
    *ny = (int)(ry * 16384 / (int64_t)length);
    *nz = (int)(rz * 16384 / (int64_t)length);
}

static void KexBoulderReflectSlope(Bullet *pBullet, short nSector,
                                   int x, int y, bool ceiling)
{
    int nx, ny, nz;
    KexBoulderSlopeNormal(nSector, x, y, ceiling, &nx, &ny, &nz);
    KexBoulderReflect(pBullet, nx, ny, nz);
}

static void KexBoulderBounceSound(short nSprite)
{
    int const sound = KexSound_Get("k_bould");
    if (sound >= 0)
        PlayFX2(sound, nSprite);
}

static int KexMoveBoulder(short nBullet)
{
    Bullet *const pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    spritetype *const pSprite = &sprite[nSprite];
    int const floor = getflorzofslope(pSprite->sectnum,
                                      pSprite->x, pSprite->y);

    // kexProjectile adds the actor default gravity only while its origin is
    // above the floor. At exact rest the floor test below still counts as an
    // expiring contact, matching BumpedFloor's on-plane case.
    if (pSprite->z < floor)
        pBullet->z += kKexBoulderGravity;

    // Proj_Boulder is drawn at scale 2, but KEX world collision continues to
    // use its authored height 32 and stepHeight 36. Temporarily decouple the
    // native sprite-height collision from the 80-repeat visual.
    uint8_t const visualRepeat = pSprite->yrepeat;
    int const tileHeight = max(1, (int)tilesiz[pSprite->picnum].y);
    pSprite->yrepeat = (uint8_t)clamp(
        (kKexBoulderHeight + tileHeight * 2) / (tileHeight * 4), 1, 255);
    int nVal = movesprite(nSprite, pBullet->x, pBullet->y, pBullet->z,
                          kKexBoulderHeight, -kKexBoulderStepHeight,
                          CLIPMASK1);
    pSprite->yrepeat = visualRepeat;

    if (!nVal && pBullet->z == 0 && pSprite->z >= floor)
        nVal = 0x20000;
    return nVal;
}

static bool KexBoulderStopped(Bullet const *pBullet,
                              int oldX, int oldY, int oldZ)
{
    // sqrt(0.05) in the common 98304-units-per-KEX-unit metric.
    constexpr uint64_t restLimit = 21981;
    uint64_t const velocity = KexBulletVectorLength(
        pBullet->x, pBullet->y, -(int64_t)pBullet->z * 1024);
    short const nSprite = pBullet->nSprite;
    uint64_t const displacement = KexBulletVectorLength(
        (int64_t)(sprite[nSprite].x - oldX) * 16384,
        (int64_t)(sprite[nSprite].y - oldY) * 16384,
        -(int64_t)(sprite[nSprite].z - oldZ) * 1024);
    return velocity <= restLimit || displacement <= restLimit;
}

static bool KexBoulderHandleBounce(short nBullet, int nVal)
{
    Bullet *const pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    spritetype *const pSprite = &sprite[nSprite];
    int const kind = nVal & 0xC000;

    // A bouncy KEX projectile reflects from faces, but an actor contact runs
    // OnImpact and applies the boulder's 35 direct damage before its blast.
    if (kind == 0xC000)
        return false;

    bool bounced = false;
    if (kind == 0x8000)
    {
        int const normal = GetWallNormal(nVal & 0x3FFF) & kAngleMask;
        KexBoulderReflect(pBullet, Cos(normal), Sin(normal), 0);
        bounced = true;
    }
    if (nVal & 0x10000)
    {
        KexBoulderReflectSlope(pBullet, pSprite->sectnum,
                               pSprite->x, pSprite->y, true);
        bounced = true;
    }
    if (nVal & 0x20000)
    {
        if (--pBullet->field_E <= 0)
            return false;
        KexBoulderReflectSlope(pBullet, pSprite->sectnum,
                               pSprite->x, pSprite->y, false);
        bounced = true;
    }

    if (bounced)
    {
        if (pBullet->x || pBullet->y)
            pSprite->ang = GetMyAngle(pBullet->x, pBullet->y);
        KexBoulderBounceSound(nSprite);
    }
    return bounced;
}

static int KexMoveSpawnerProjectile(short nBullet)
{
    Bullet *const pBullet = &BulletList[nBullet];
    short const nSprite = pBullet->nSprite;
    spritetype *const pSprite = &sprite[nSprite];
    int const height = pBullet->nType == 15
        ? kKexSpawnerLaserHeight : kKexSpawnerFireballHeight;
    int const stepHeight = pBullet->nType == 15
        ? 0 : kKexSpawnerFireballStepHeight;

    // Native Build derives projectile height from its art. KEX keeps visual
    // scale separate: Proj_Fireball is 32/36 high/step and Proj_Beam is 8/0.
    uint8_t const visualRepeat = pSprite->yrepeat;
    int const tileHeight = max(1, (int)tilesiz[pSprite->picnum].y);
    pSprite->yrepeat = (uint8_t)clamp(
        (height + tileHeight * 2) / (tileHeight * 4), 1, 255);
    int const nVal = movesprite(nSprite, pBullet->x, pBullet->y, pBullet->z,
                                height, -stepHeight, CLIPMASK1);
    pSprite->yrepeat = visualRepeat;
    return nVal;
}

static int KexFlameRepeat(int nAge)
{
    // Proj_Flame has the default .5 KEX animation speed, so its loop action
    // runs once per two 60-Hz actor steps -- exactly once per native Exhumed
    // tick. A_UpdateFlame first raises fizzle by .5, then accumulates
    // (5000 - 128*fizzle) / 32768 from the 1/64 starting scale.
    nAge = clamp(nAge, 0, 30);
    int const delta = nAge * 5000 - 32 * nAge * (nAge + 1);
    return clamp(((512 + delta) * 40 + 16384) / 32768, 1, 255);
}

static void KexFlameUpdateVisual(Bullet *pBullet)
{
    int const age = 30 - pBullet->field_E;
    short const nSprite = pBullet->nSprite;
    sprite[nSprite].xrepeat = KexFlameRepeat(age);
    sprite[nSprite].yrepeat = KexFlameRepeat(age);
    // A_UpdateFlame turns white flame toward red and reduces alpha during
    // its fifteen-unit fizzle. The native palette has no per-sprite RGB, so
    // use its fire palette, then introduce translucency for the fading half.
    sprite[nSprite].pal = 4;
    sprite[nSprite].shade = clamp(-127 + age * 227 / 30, -127, 100);
    if (age >= 15)
        sprite[nSprite].cstat |= 2;
}

int MoveBullet(short nBullet)
{
    short hitsect = -1;
    short hitwall = -1;
    short hitsprite = -1;

    Bullet *pBullet = &BulletList[nBullet];
    short nType = pBullet->nType;
    bulletInfo *pBulletInfo = &BulletInfo[nType];

    short nSprite = BulletList[nBullet].nSprite;
    spritetype *pSprite = &sprite[nSprite];

    int x = pSprite->x;
    int y = pSprite->y;
    int z = pSprite->z; // ebx
    short const nStartSector = pSprite->sectnum;
    short nSectFlag = SectFlag[nStartSector];
    unsigned const clipMask = CLIPMASK1;

    int x2, y2, z2;

    int nVal;

    if (pBullet->field_10 < 30000)
    {
        short nEnemySprite = nBulletEnemy[nBullet];
        if (nEnemySprite > -1)
        {
            if (!(sprite[nEnemySprite].cstat & 0x101))
                nBulletEnemy[nBullet] = -1;
            else if (KexBulletFacesTargetWithoutThrust(nType))
            {
                // A_CobraSlither performs two orientation/velocity updates
                // inside KexMoveRedCobra, matching the KEX 60-Hz loop.
                if (!KexBulletRedCobra(nType))
                    KexTurnProjectileWithoutThrust(nBullet, nEnemySprite);
            }
            else
            {
                nVal = g_kexMode && nType == 11
                    ? KexMoveSoulProjectile(nBullet, nEnemySprite)
                    : AngleChase(nSprite, nEnemySprite, pBullet->field_10, 0,
                                 KexBulletTurnStep(nType));
                goto MOVEEND;
            }
        }
        if (nType == 3)
        {
            if (pBullet->field_E < 8)
            {
                pSprite->xrepeat -= 1;
                pSprite->yrepeat += 8;

                pBullet->z -= 200;

                if (pSprite->shade < 90) {
                    pSprite->shade += 35;
                }

                if (pBullet->field_E == 3)
                {
                    pBullet->nSeq = 45;
                    pBullet->nFrame = 0;
                    pSprite->xrepeat = 40;
                    pSprite->yrepeat = 40;
                    pSprite->shade = 0;
                    pSprite->z += 512;
                }
            }
            else
            {
                pSprite->xrepeat += 4;
                pSprite->yrepeat += 4;
            }
        }

        // KEX impactWallsOnly projectiles still use the actor-colliding mask;
        // only their floor/ceiling response differs below.
        if (g_kexMode && nType == kKexBoulderType)
            nVal = KexMoveBoulder(nBullet);
        else if (KexBulletRedCobra(nType))
            nVal = KexMoveRedCobra(nBullet, nBulletEnemy[nBullet]);
        else if (KexSpawnerProjectile(pBullet))
            nVal = KexMoveSpawnerProjectile(nBullet);
        else
            nVal = movesprite(nSprite, pBullet->x, pBullet->y, pBullet->z,
                              pSprite->clipdist >> 1, pSprite->clipdist >> 1,
                              clipMask);

MOVEEND:
        if (g_kexMode && nType == kKexBoulderType)
        {
            if (nVal && KexBoulderHandleBounce(nBullet, nVal))
                return 0;
            if (!nVal && KexBoulderStopped(pBullet, x, y, z))
                nVal = 1;
        }
        if (KexBulletGlidesOnPlanes(nType) && (nVal & 0x30000))
        {
            // impactWallsOnly is frequently misread as "ignore actors". In
            // KEX it instead follows traversable floor/ceiling planes and
            // still impacts vertical walls and actors. Flat planes are the
            // common retail case; retain horizontal thrust after contact.
            if ((nVal & 0x20000) && nType != 15)
                pSprite->z -= 8 * 16 * 6; // authored floorOffset 8
            pBullet->z = 0;
            nVal &= ~0x30000;
        }
        if (nVal)
        {
            x2 = pSprite->x;
            y2 = pSprite->y;
            z2 = pSprite->z;
            hitsect = pSprite->sectnum;

            if (nVal & 0x30000)
            {
                hitwall = nVal & 0x3FFF;
                goto HITWALL;
            }
            else
            {
                switch (nVal & 0xc000)
                {
                case 0x8000:
                    hitwall = nVal & 0x3FFF;
                    goto HITWALL;
                case 0xc000:
                    hitsprite = nVal & 0x3FFF;
                    goto HITSPRITE;
                }
            }
        }

        // kexActor::UpdateMovement lets remaster projectiles cross an
        // FF_WATER portal, emits its Fx_WaterSplash callback, and keeps their
        // velocity. The old PCExhumed loop deleted every bullet at this
        // boundary, which made KEX enemy shots vanish at a water surface.
        // Keep its classic behavior outside the explicit KEX projectile set.
        short nFlagVal = nSectFlag ^ SectFlag[pSprite->sectnum];
        if (KexBulletUsesRemasterDeath(nType))
            KexWaterSplash_OnTransition(nStartSector, x, y, z,
                                        pSprite->sectnum, pSprite->x,
                                        pSprite->y);
        if ((nFlagVal & kSectUnderwater)
            && !KexBulletUsesRemasterDeath(nType))
        {
            DestroyBullet(nBullet);
            nVal = 1;
        }

        if (nVal == 0 && nType != 15 && nType != 3
            && nType != kKexFlameType
            && !(g_kexMode && nType == kKexBoulderType))
        {
            AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, 0);

            if (sprite[nSprite].pal != 5) {
                sprite[nSprite].pal = 1;
            }
        }
    }
    else
    {
        nVal = 1;

        if (nBulletEnemy[nBullet] > -1)
        {
            hitsprite = nBulletEnemy[nBullet];
            x2 = sprite[hitsprite].x;
            y2 = sprite[hitsprite].y;
            z2 = sprite[hitsprite].z - (GetSpriteHeight(hitsprite) >> 1);
            hitsect = sprite[hitsprite].sectnum;
        }
        else
        {
            vec3_t startPos = { x, y, z };
            hitdata_t hitData;
            int dz;
            if (bVanilla)
                dz = -Sin(pBullet->field_C) * 8;
            else
                dz = -pBullet->field_C * 512;
            hitscan(&startPos, pSprite->sectnum, Cos(pSprite->ang), Sin(pSprite->ang), dz, &hitData, CLIPMASK1);
            x2 = hitData.x;
            y2 = hitData.y;
            z2 = hitData.z;
            hitsprite = hitData.sprite;
            hitsect = hitData.sect;
            hitwall = hitData.wall;
        }

        // KEX's player A_HitScan callbacks stop at their authored range.
        // Do this after Build's ray query so portals and slopes still choose
        // the correct first obstruction, but before any damage or puff is
        // committed. An out-of-range contact has no hit feedback at all.
        if (!KexHitScanInRange(nType, x, y, z, x2, y2, z2))
        {
            x2 = x;
            y2 = y;
            z2 = z;
            hitsprite = -1;
            hitwall = -1;
            hitsect = -1;
        }

        lasthitx = x2;
        lasthity = y2;
        lasthitz = z2;
        lasthitsect = hitsect;
        lasthitwall = hitwall;
        lasthitsprite = hitsprite;

        if (hitsprite > -1)
        {
HITSPRITE:
            if (pSprite->pal == 5 && sprite[hitsprite].statnum == 100)
            {
                short nPlayer = GetPlayerFromSprite(hitsprite);
                if (!PlayerList[nPlayer].bIsMummified)
                {
                    PlayerList[nPlayer].bIsMummified = kTrue;
                    SetNewWeapon(nPlayer, kWeaponMummified);
                }
            }
            else
            {
                BulletHitsSprite(pBullet, pSprite->owner, hitsprite, x2, y2, z2, hitsect);
            }
        }
        else if (hitwall > -1)
        {
HITWALL:
            if (wall[hitwall].picnum == kEnergy1)
            {
                short nSector = wall[hitwall].nextsector;
                if (nSector > -1)
                {
                    short nDamage = KexBulletDamage(pBullet->nType);
                    if (pBullet->field_13 > 1) {
                        nDamage *= 2;
                    }

                    short nNormal = GetWallNormal(hitwall) & kAngleMask;

                    runlist_DamageEnemy(sector[nSector].extra, nNormal, nDamage);
                }
            }
        }

        if (hitsect > -1) // NOTE: hitsect can be -1. this check wasn't in original code. TODO: demo compatiblity?
        {
            // KEX death-animation callbacks run on every impact, including a
            // floor or ceiling. Fx_FireballExplode is created at the
            // unmodified contact point (with boulder's authored +2 KEX Z)
            // before the radial callback and before the legacy wall-puff path
            // backs x/y away from the face.
            bool const kexExactImpact = KexBulletHandleDeathVisual(
                nBullet, nType, x2, y2, z2, hitsect);
            bool const kexAuthoredBlast = KexBulletApplyImpactBlast(
                nBullet, x2, y2, z2, hitsect, kexExactImpact);

            if (hitsprite < 0 && hitwall < 0)
            {
                if ((SectBelow[hitsect] >= 0 && (SectFlag[SectBelow[hitsect]] & kSectUnderwater)) || SectDepth[hitsect])
                {
                    pSprite->x = x2;
                    pSprite->y = y2;
                    pSprite->z = z2;
                    BuildSplash(nSprite, hitsect);
                }
                else if (!kexExactImpact)
                {
                    BuildAnim(-1, pBulletInfo->field_C, 0, x2, y2, z2, hitsect, 40, pBulletInfo->nFlags);
                }
            }
            else
            {
                if (hitwall >= 0)
                {
                    if (!kexExactImpact)
                    {
                        BackUpBullet(&x2, &y2, pSprite->ang);

                        if (nType != 3 || RandomSize(2) == 0)
                        {
                            int zOffset = RandomSize(8) << 3;

                            if (!RandomBit()) {
                                zOffset = -zOffset;
                            }

                            // draws bullet puff on walls when they're shot
                            BuildAnim(-1, pBulletInfo->field_C, 0, x2, y2, z2 + zOffset, hitsect, 40, pBulletInfo->nFlags);
                        }
                    }
                }
                else
                {
                    pSprite->x = x2;
                    pSprite->y = y2;
                    pSprite->z = z2;

                    mychangespritesect(nSprite, hitsect);
                }

                if (!kexAuthoredBlast && !KexBulletSuppressClassicRadial(nType)
                        && BulletInfo[nType].nRadius)
                {
                    nRadialBullet = nType;

                    runlist_RadialDamageEnemy(nSprite, KexBulletDamage(nType), pBulletInfo->nRadius);

                    nRadialBullet = -1;

                    AddFlash(pSprite->sectnum, pSprite->x, pSprite->y, pSprite->z, 128);
                }
            }
        }

        DestroyBullet(nBullet);
    }

    return nVal;
}

void SetBulletEnemy(short nBullet, short nEnemy)
{
    if (nBullet >= 0) {
        nBulletEnemy[nBullet] = nEnemy;
    }
}

int GetBulletSprite(int nBullet)
{
    assert(nBullet >= 0 && nBullet < kMaxBullets);
    return BulletList[nBullet].nSprite;
}


// NNBuild: Proj_Fireball and Proj_Boulder both play pbexplode.wav from
// their death animation. The exact KEX impact sequence is intentionally
// silent so callback order stays explicit here.
static void KexProjectileImpactSound(int x, int y, int z, int nSector)
{
    int const s = KexSound_Get("k_bldhit");
    if (s >= 0)
        PlayFXAtXYZ(s, x, y, z, nSector);
}

static int BuildBulletInternal(short nSprite, int nType, int UNUSED(ebx),
                               int UNUSED(ecx), int val1, int nAngle,
                               int val2, int val3,
                               KexProjectileLaunch const *kexLaunch)
{
    Bullet sBullet;
    bulletInfo *pBulletInfo = &BulletInfo[nType];

    if (!kexLaunch && pBulletInfo->field_4 > 30000)
    {
        if (val2 >= 10000)
        {
            val2 -= 10000;

            short nTargetSprite = val2;
            spritetype *pTargetSprite = &sprite[nTargetSprite];

//			assert(sprite[nTargetSprite].sectnum <= kMaxSectors);

            if (pTargetSprite->cstat & 0x101)
            {
                sBullet.nType = nType;
                sBullet.field_13 = val3;

                sBullet.nSprite = insertsprite(sprite[nSprite].sectnum, 200);
                sprite[sBullet.nSprite].ang = nAngle;

                int nHeight = GetSpriteHeight(nTargetSprite);

                assert(sprite[nTargetSprite].sectnum >= 0 && sprite[nTargetSprite].sectnum < kMaxSectors);

                BulletHitsSprite(&sBullet, nSprite, nTargetSprite, pTargetSprite->x, pTargetSprite->y, pTargetSprite->z - (nHeight >> 1), pTargetSprite->sectnum);
                mydeletesprite(sBullet.nSprite);
                return -1;
            }
            else
            {
                val2 = 0;
            }
        }
    }

    if (!nBulletsFree) {
        return -1;
    }

    short nSector;

    if (sprite[nSprite].statnum == 100)
    {
        nSector = nPlayerViewSect[GetPlayerFromSprite(nSprite)];
    }
    else
    {
        nSector = sprite[nSprite].sectnum;
    }

    short nBulletSprite = insertsprite(nSector, 200);
    int nHeight = GetSpriteHeight(nSprite);
    nHeight = nHeight - (nHeight >> 2);

    if (val1 == -1) {
        val1 = -nHeight;
    }

    sprite[nBulletSprite].x = sprite[nSprite].x;
    sprite[nBulletSprite].y = sprite[nSprite].y;
    sprite[nBulletSprite].z = sprite[nSprite].z;

    // why is this done here???
    assert(nBulletSprite >= 0 && nBulletSprite < kMaxSprites);

    short nBullet = GrabBullet();
    Bullet *pBullet = &BulletList[nBullet];

    nBulletEnemy[nBullet] = -1;

    sprite[nBulletSprite].cstat = 0;
    int const kexRadius = KexBulletRadius(nType);
    sprite[nBulletSprite].shade = kexRadius >= 0 ? -127 : -64;

    if (pBulletInfo->nFlags & 4) {
        sprite[nBulletSprite].pal = 4;
    }
    else {
        sprite[nBulletSprite].pal = 0;
    }

    sprite[nBulletSprite].clipdist = kexRadius >= 0
        ? (uint8_t)((kexRadius * 6 + 2) / 4) : 25;

    short nRepeat = KexBulletRepeat(nType, kexRadius, pBulletInfo->xyRepeat);
    if (nRepeat < 0) {
        nRepeat = 30;
    }

    sprite[nBulletSprite].xrepeat = nRepeat;
    sprite[nBulletSprite].yrepeat = nRepeat;
    sprite[nBulletSprite].xoffset = 0;
    sprite[nBulletSprite].yoffset = 0;
    sprite[nBulletSprite].ang = nAngle;
    sprite[nBulletSprite].xvel = 0;
    sprite[nBulletSprite].yvel = 0;
    sprite[nBulletSprite].zvel = 0;
    sprite[nBulletSprite].owner = nSprite;
    sprite[nBulletSprite].lotag = runlist_HeadRun() + 1;
    sprite[nBulletSprite].extra = -1;
    sprite[nBulletSprite].hitag = 0;

//	GrabTimeSlot(3);

    pBullet->field_10 = 0;
    pBullet->field_E = g_kexMode && nType == kKexBoulderType
        ? kKexBoulderFloorContacts : pBulletInfo->field_2;
    pBullet->nFrame  = 0;

    short nSeq;

    if (pBulletInfo->field_8 != -1)
    {
        pBullet->field_12 = 0;
        nSeq = pBulletInfo->field_8;
    }
    else
    {
        pBullet->field_12 = 1;
        nSeq = pBulletInfo->nSeq;
    }

    pBullet->nSeq = nSeq;

    int const kexTile = KexBulletTile(nType);
    sprite[nBulletSprite].picnum = kexTile >= 0 ? kexTile : seq_GetSeqPicnum(nSeq, 0, 0);

    if (nSeq == kSeqBullet) {
        sprite[nBulletSprite].cstat |= 0x8000;
    }

    pBullet->field_C = val2;
    pBullet->nType = nType;
    pBullet->nSprite = nBulletSprite;
    pBullet->field_6 = runlist_AddRunRec(sprite[nBulletSprite].lotag - 1, nBullet, kRunBullet);
    pBullet->field_8 = runlist_AddRunRec(NewRun, nBullet, kRunBullet);
    pBullet->field_13 = val3;
    sprite[nBulletSprite].z += val1;

    if (kexLaunch)
    {
        KexProjectileExpected const expected = KexBulletExpectedLaunch(nSprite, *kexLaunch);
        sprite[nBulletSprite].x = expected.x;
        sprite[nBulletSprite].y = expected.y;
        sprite[nBulletSprite].z = expected.z;
        short launchSector = nSector;
        updatesectorz(expected.x, expected.y, expected.z, &launchSector);
        if (launchSector >= 0 && launchSector != nSector)
            mychangespritesect(nBulletSprite, launchSector);

        pBullet->field_10 = kexLaunch->nSpeed * 24;
        pBullet->x = expected.vx;
        pBullet->y = expected.vy;
        pBullet->z = expected.vz;
        if (KexBulletRedCobra(nType))
        {
            s_kexCobraPitch[nBullet] = (short)KexRedCobraTargetPitch(
                expected.x, expected.y, expected.z, kexLaunch->nTarget, nType);
            KexRedCobraCreateJoints(nBullet);
        }
        nBulletEnemy[nBullet] = kexLaunch->bHomingThrust
                || KexBulletFacesTargetWithoutThrust(nType)
            ? kexLaunch->nTarget : -1;

        if (s_kexBulletAudit < 0)
            s_kexBulletAudit = g_kexMode && Bgetenv("VB_KEX_PROJECTILE_AUDIT") ? 1 : 0;
        if (s_kexBulletAudit && !kexLaunch->bSuppressAudit)
        {
            OSD_Printf("Kex projectile: type %d source %d target %d pos %d,%d,%d vel %d,%d,%d callback %d/%d/%d radius %d repeat %d homing %d\n",
                       nType, nSprite, kexLaunch->nTarget,
                       expected.x, expected.y, expected.z,
                       expected.vx, expected.vy, expected.vz,
                       kexLaunch->nCallbackDistance, kexLaunch->nHeight,
                       kexLaunch->nSpeed, kexRadius,
                       (int)sprite[nBulletSprite].xrepeat,
                       kexLaunch->bHomingThrust ? 1 : 0);
        }
        return nBullet;
    }

    int var_18;

    nSector = sprite[nBulletSprite].sectnum;

    while (sprite[nBulletSprite].z < sector[nSector].ceilingz)
    {
        if (SectAbove[nSector] == -1)
        {
            sprite[nBulletSprite].z = sector[nSector].ceilingz;
            break;
        }

        nSector = SectAbove[nSector];
        mychangespritesect(nBulletSprite, nSector);
    }

    if (val2 < 10000)
    {
        var_18 = ((-Sin(val2)) * pBulletInfo->field_4) >> 11;
    }
    else
    {
        val2 -= 10000;

        short nTargetSprite = val2;

        if ((unsigned int)pBulletInfo->field_4 > 30000)
        {
            nBulletEnemy[nBullet] = nTargetSprite;
        }
        else
        {
            nHeight = GetSpriteHeight(nTargetSprite);

            if (sprite[nTargetSprite].statnum == 100)
            {
                nHeight -= nHeight >> 2;
            }
            else
            {
                nHeight -= nHeight >> 1;
            }

            int var_20 = sprite[nTargetSprite].z - nHeight;

            int x, y;

            if (nSprite != -1 && sprite[nSprite].statnum != 100)
            {
                x = sprite[nTargetSprite].x;
                y = sprite[nTargetSprite].y;

                if (sprite[nTargetSprite].statnum != 100)
                {
                    x += (sprite[nTargetSprite].xvel * 20) >> 6;
                    y += (sprite[nTargetSprite].yvel * 20) >> 6;
                }
                else
                {
                    int nPlayer = GetPlayerFromSprite(nTargetSprite);
                    if (nPlayer > -1)
                    {
                        x += nPlayerDX[nPlayer] * 15;
                        y += nPlayerDY[nPlayer] * 15;
                    }
                }

                x -= sprite[nBulletSprite].x;
                y -= sprite[nBulletSprite].y;

                nAngle = GetMyAngle(x, y);
                sprite[nSprite].ang = nAngle;
            }
            else
            {
                // loc_2ABA3:
                x = sprite[nTargetSprite].x - sprite[nBulletSprite].x;
                y = sprite[nTargetSprite].y - sprite[nBulletSprite].y;
            }

            int nSqrt = lsqrt(y*y + x*x);
            if ((unsigned int)nSqrt > 0)
            {
                var_18 = ((var_20 - sprite[nBulletSprite].z) * pBulletInfo->field_4) / nSqrt;
            }
            else
            {
                var_18 = 0;
            }
        }
    }

    pBullet->z = 0;

    int const kexSpawnerSpeed = KexSpawnerLaunchSpeed(nSprite, nType);
    if (kexSpawnerSpeed > 0)
    {
        // Every kexSlaveFireballSpawner launches along its full face normal:
        // fireballs/boulders at speed 12 and laser bolts at 16. The converter
        // has already placed this hidden source at the exact face polygon
        // centroid plus the authored 32-unit normal offset.
        int const normalZ = clamp((int)sprite[nSprite].zvel, -16384, 16384);
        int const horizontal = (int)KexBulletISqrt(
            (uint64_t)16384 * 16384 - (int64_t)normalZ * normalZ);
        int const normalX = mulscale14(Cos(nAngle), horizontal);
        int const normalY = mulscale14(Sin(nAngle), horizontal);
        pBullet->field_10 = (kexSpawnerSpeed / 6) * 24;
        pBullet->x = normalX * kexSpawnerSpeed;
        pBullet->y = normalY * kexSpawnerSpeed;
        pBullet->z = -(int)(((int64_t)normalZ * kexSpawnerSpeed * 16)
                           / 16384);
        nBulletEnemy[nBullet] = -1;
        return nBullet;
    }

    pBullet->x = (sprite[nSprite].clipdist << 2) * Cos(nAngle);
    pBullet->y = (sprite[nSprite].clipdist << 2) * Sin(nAngle);
    nBulletEnemy[nBullet] = -1;

    if (MoveBullet(nBullet))
    {
        nBulletSprite = -1;
    }
    else
    {
        pBullet->field_10 = pBulletInfo->field_4;
        pBullet->x = (Cos(nAngle) >> 3) * pBulletInfo->field_4;
        pBullet->y = (Sin(nAngle) >> 3) * pBulletInfo->field_4;
        pBullet->z = var_18 >> 3;

    }

    return nBullet;
}

int BuildBullet(short nSprite, int nType, int ebx, int ecx, int val1,
                int nAngle, int val2, int val3)
{
    return BuildBulletInternal(nSprite, nType, ebx, ecx, val1, nAngle,
                               val2, val3, nullptr);
}

int BuildKexAIFireProjectile(short nSprite, int nType, short nTarget,
                             int nCallbackDistance, int nHeight,
                             int nSpeed, int bHomingThrust)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites
        || (unsigned)nTarget >= (unsigned)kMaxSprites
        || sprite[nSprite].sectnum < 0 || sprite[nTarget].sectnum < 0
        || !(sprite[nTarget].cstat & 0x101) || nSpeed <= 0)
        return -1;

    KexProjectileLaunch const launch = {
        nTarget, nCallbackDistance, nHeight, nSpeed, bHomingThrust != 0,
        false
    };
    return BuildBulletInternal(nSprite, nType, 0, 0, 0,
                               sprite[nSprite].ang, nTarget + 10000, 1,
                               &launch);
}

static KexProjectileExpected KexPlayerFlameExpected(short nSprite,
                                                     int nPitch)
{
    // kexPlayerPuppet uses radius 95.25, represented exactly as 1143
    // half-Build units. Its native movement sprite has clipdist 58, so the
    // generic actor launcher would put the flame visibly inside the player.
    constexpr int kKexPlayerRadius2 = 1143;
    // The final three A_FireProjectile arguments are distance/height/speed:
    // -48 pulls the flame's muzzle inside the player puppet radius, +80
    // raises it to the puppet centre, and 16 is its world speed.
    constexpr int kKexCallbackDistance = -48;
    constexpr int kKexFlameHeight = 80;
    constexpr int kKexFlameSpeed = 16 * 6;
    int const fx = mulscale14(Cos(sprite[nSprite].ang), Cos(nPitch));
    int const fy = mulscale14(Sin(sprite[nSprite].ang), Cos(nPitch));
    int const fz = Sin(nPitch);
    int const distance2 = kKexPlayerRadius2 + 2 * kKexCallbackDistance * 6;
    KexProjectileExpected expected;
    expected.x = sprite[nSprite].x
        + (int)(((int64_t)fx * distance2) / (2 * 16384));
    expected.y = sprite[nSprite].y
        + (int)(((int64_t)fy * distance2) / (2 * 16384));
    expected.z = sprite[nSprite].z
        - (int)(((int64_t)fz * distance2) / (2 * 16384))
        - kKexFlameHeight * 16 * 6;

    expected.vx = fx * kKexFlameSpeed;
    expected.vy = fy * kKexFlameSpeed;
    expected.vz = -(int)(((int64_t)fz * kKexFlameSpeed * 16) / 16384);
    return expected;
}

int BuildKexPlayerFlameProjectile(short nSprite, int nPitch)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexFlame)
            || (unsigned)nSprite >= (unsigned)kMaxSprites
            || sprite[nSprite].sectnum < 0)
        return -1;

    // BuildBulletInternal's KEX launch branch performs the normal serialized
    // bullet allocation without a legacy immediate hit-scan/move. A player
    // has no target: replace the temporary actor-target launch with the
    // player-puppet direction and authored muzzle below.
    KexProjectileLaunch const launch = {
        nSprite, -48, 80, 16, false, true
    };
    int const nBullet = BuildBulletInternal(nSprite, kKexFlameType,
        0, 0, 0, sprite[nSprite].ang, nSprite + 10000, 1, &launch);
    if (nBullet < 0)
        return -1;

    KexProjectileExpected const expected = KexPlayerFlameExpected(nSprite,
                                                                    nPitch);
    Bullet &bullet = BulletList[nBullet];
    short const nProjectile = bullet.nSprite;
    sprite[nProjectile].x = expected.x;
    sprite[nProjectile].y = expected.y;
    sprite[nProjectile].z = expected.z;
    short nSector = sprite[nProjectile].sectnum;
    updatesectorz(expected.x, expected.y, expected.z, &nSector);
    if (nSector >= 0 && nSector != sprite[nProjectile].sectnum)
        mychangespritesect(nProjectile, nSector);
    bullet.x = expected.vx;
    bullet.y = expected.vy;
    bullet.z = expected.vz;
    bullet.field_10 = 16 * 24;
    int const nFrames = SeqSize[SeqOffsets[kSeqKexFlame]];
    bullet.nFrame = (int)(((uint32_t)RandomSize(16) * nFrames) >> 16);
    sprite[nProjectile].picnum = seq_GetSeqPicnum(kSeqKexFlame,
                                                   bullet.nFrame, 0);
    nBulletEnemy[nBullet] = -1;
    return nBullet;
}

static bool KexBullet_CheckPlayerFlame(int nBullet, short nSource,
                                       int nPitch)
{
    if (!g_kexMode || (unsigned)nBullet >= (unsigned)kMaxBullets
        || (unsigned)nSource >= (unsigned)kMaxSprites)
        return false;
    Bullet const &bullet = BulletList[nBullet];
    if (bullet.nType != kKexFlameType
        || (unsigned)bullet.nSprite >= (unsigned)kMaxSprites
        || sprite[bullet.nSprite].sectnum < 0)
        return false;
    KexProjectileExpected const expected = KexPlayerFlameExpected(nSource,
                                                                    nPitch);
    spritetype const &projectile = sprite[bullet.nSprite];
    int const nFrames = SeqSize[SeqOffsets[kSeqKexFlame]];
    return projectile.x == expected.x && projectile.y == expected.y
        && projectile.z == expected.z && bullet.x == expected.vx
        && bullet.y == expected.vy && bullet.z == expected.vz
        && bullet.field_E == 30 && bullet.field_10 == 16 * 24
        && bullet.nSeq == kSeqKexFlame && bullet.field_12 == 1
        && nBulletEnemy[nBullet] == -1
        && projectile.clipdist == (36 * 6 + 2) / 4
        && projectile.xrepeat == 1 && projectile.yrepeat == 1
        && projectile.pal == 4 && projectile.shade == -127
        && (unsigned)bullet.nFrame < (unsigned)nFrames
        && projectile.picnum == seq_GetSeqPicnum(kSeqKexFlame,
                                                  bullet.nFrame, 0);
}

int KexBullet_CheckAIFireProjectile(int nBullet, short nSource, short nTarget,
                                    int nCallbackDistance, int nHeight,
                                    int nSpeed, int bHomingThrust)
{
    if (!g_kexMode || (unsigned)nBullet >= (unsigned)kMaxBullets
        || (unsigned)nSource >= (unsigned)kMaxSprites
        || (unsigned)nTarget >= (unsigned)kMaxSprites)
        return 0;
    Bullet const &bullet = BulletList[nBullet];
    if ((unsigned)bullet.nSprite >= (unsigned)kMaxSprites
        || sprite[bullet.nSprite].sectnum < 0)
        return 0;
    KexProjectileLaunch const launch = {
        nTarget, nCallbackDistance, nHeight, nSpeed, bHomingThrust != 0,
        false
    };
    KexProjectileExpected const expected = KexBulletExpectedLaunch(nSource, launch);
    int const radius = KexBulletRadius(bullet.nType);
    spritetype const &projectile = sprite[bullet.nSprite];
    return projectile.x == expected.x && projectile.y == expected.y
        && projectile.z == expected.z && bullet.x == expected.vx
        && bullet.y == expected.vy && bullet.z == expected.vz
        && bullet.field_10 == nSpeed * 24
        && nBulletEnemy[nBullet] == (bHomingThrust
                || KexBulletFacesTargetWithoutThrust(bullet.nType)
            ? nTarget : -1)
        && radius >= 0 && projectile.clipdist == (radius * 6 + 2) / 4
        && projectile.xrepeat == KexBulletRepeat(bullet.nType, radius, 0)
        && projectile.yrepeat == KexBulletRepeat(bullet.nType, radius, 0)
        && projectile.shade == -127;
}

int KexBullet_GetRedCobraPitch(int nBullet, int *pPitch)
{
    if (!pPitch || (unsigned)nBullet >= (unsigned)kMaxBullets
        || !KexBulletRedCobra(BulletList[nBullet].nType)
        || (unsigned)BulletList[nBullet].nSprite >= (unsigned)kMaxSprites
        || sprite[BulletList[nBullet].nSprite].sectnum < 0)
        return 0;
    *pPitch = s_kexCobraPitch[nBullet];
    return 1;
}

int KexBullet_GetRedCobraJoint(int nBullet, int nJoint)
{
    if ((unsigned)nBullet >= (unsigned)kMaxBullets
        || (unsigned)nJoint >= ARRAY_SIZE(s_kexCobraJoint[nBullet])
        || !KexBulletRedCobra(BulletList[nBullet].nType))
        return -1;
    return s_kexCobraJoint[nBullet][nJoint];
}

static bool KexRedCobraJointProfile(int nBullet)
{
    if ((unsigned)nBullet >= (unsigned)kMaxBullets)
        return false;
    int const tile = KexProjectile_RedCobraJointTile();
    int joints = 0;
    bool profile = true;
    for (int i = 0; i < (int)ARRAY_SIZE(s_kexCobraJoint[nBullet]); i++)
    {
        int const nJoint = s_kexCobraJoint[nBullet][i];
        if (nJoint < 0)
            continue;
        joints++;
        if ((unsigned)nJoint >= (unsigned)kMaxSprites)
        {
            profile = false;
            continue;
        }
        spritetype const &joint = sprite[nJoint];
        profile &= joint.sectnum >= 0 && joint.picnum == tile
            && joint.xrepeat == KexRedCobraJointRepeat(i)
            && joint.yrepeat == KexRedCobraJointRepeat(i)
            && joint.shade == -64 && joint.pal == 0 && joint.extra == -1;
    }
    return joints == (tile > 0 ? 5 : 0) && profile;
}

static bool KexBullet_CheckRedCobraSlither(int nBullet)
{
    if ((unsigned)nBullet >= (unsigned)kMaxBullets)
        return false;
    Bullet const &before = BulletList[nBullet];
    if (!KexBulletRedCobra(before.nType)
        || (unsigned)before.nSprite >= (unsigned)kMaxSprites)
        return false;
    short const nTarget = nBulletEnemy[nBullet];
    for (int sub = 0; sub < 2; sub++)
    {
        if ((unsigned)nTarget < (unsigned)kMaxSprites
            && (sprite[nTarget].cstat & 0x101))
            KexTurnRedCobra(nBullet, nTarget);
        KexRedCobraSlither(nBullet, totalmoves * 2 + sub);
        KexRedCobraUpdateJoints(nBullet);
    }
    Bullet const &after = BulletList[nBullet];
    int const speed = after.nType == 9 ? 14 * 6 : 16 * 6;
    uint64_t const velocity = KexBulletVectorLength(after.x, after.y,
        -(int64_t)after.z * 1024);
    return velocity >= (uint64_t)speed * 16384
        && velocity <= (uint64_t)(speed + 25) * 16384
        && klabs(s_kexCobraPitch[nBullet])
            <= KexRedCobraMaxPitch(after.nType)
        && KexRedCobraJointProfile(nBullet);
}

static bool KexBulletImpactContractMatches()
{
    KexImpactBlast const mummy = KexBulletImpactBlast(9);
    KexImpactBlast const selkis = KexBulletImpactBlast(16);
    KexImpactBlast const fireball = KexBulletImpactBlast(10);
    KexImpactBlast const trapFireball = KexBulletImpactBlast(14);
    KexImpactBlast const boulder = KexBulletImpactBlast(17);
    KexImpactBlast const soul = KexBulletImpactBlast(11);
    KexImpactBlast const seedling = KexBulletImpactBlast(12);
    return mummy.radius == 224 && mummy.damage == 50
        && !mummy.opensToggleFaces
        && selkis.radius == 224 && selkis.damage == 50
        && !selkis.opensToggleFaces
        && fireball.radius == 192 && fireball.damage == 64
        && fireball.opensToggleFaces
        && trapFireball.radius == 192 && trapFireball.damage == 64
        && trapFireball.opensToggleFaces
        && boulder.radius == 192 && boulder.damage == 64
        && boulder.opensToggleFaces
        && KexBulletBuildBlastRadius(224) == 5
        && KexBulletBuildBlastRadius(192) == 4
        && soul.radius == 0 && soul.damage == 0
        && seedling.radius == 0 && seedling.damage == 0
        && KexBulletSuppressClassicRadial(11)
        && KexBulletSuppressClassicRadial(12)
        && KexBulletDamage(1) == 15
        && KexBulletDamage(2) == 20
        && KexHitScanRange(1) == 3000
        && KexHitScanRange(2) == 4000
        && KexHitScanInRange(1, 0, 0, 0, 3000 * 6, 0, 0)
        && !KexHitScanInRange(1, 0, 0, 0, 3000 * 6 + 1, 0, 0)
        && KexBulletDamage(14) == 100
        && KexBulletRadius(14) == 25 && KexBulletRadius(17) == 25
        && KexBulletRepeat(17, 25, 0) == 80;
}

static bool KexBoulderPhysicsContractMatches()
{
    Bullet wall = {};
    wall.x = 72 * 16384;
    KexBoulderReflect(&wall, 16384, 0, 0);

    Bullet floor = {};
    floor.z = 64;
    KexBoulderReflect(&floor, 0, 0, 16384);

    return kKexBoulderGravity == 72
        && kKexBoulderFloorContacts == 8
        && kKexBoulderHeight == 32 * 16 * 6
        && kKexBoulderStepHeight == 36 * 16 * 6
        && kKexBoulderBounceNumerator == 31
        && kKexBoulderBounceDenominator == 16
        && wall.x == -(72 * 16384 * 15 / 16)
        && wall.y == 0 && wall.z == 0
        && floor.x == 0 && floor.y == 0 && floor.z == -60;
}

static int KexMantisSpreadAngle()
{
    // A_MantisAttack adds RFloat() * .0625 radians. Convert a signed 16-bit
    // random fraction to Build's 2048-angle circle without floating point;
    // .0625 rad is 20.37 Build angle units.
    int const signedRandom = RandomSize(16) - 32768;
    return (int)(((int64_t)signedRandom * 41) / 65536);
}

int BuildKexMantisFireProjectile(short nSprite, short nTarget)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites
        || (unsigned)nTarget >= (unsigned)kMaxSprites
        || sprite[nSprite].sectnum < 0 || sprite[nTarget].sectnum < 0
        || !(sprite[nTarget].cstat & 0x101))
        return -1;

    // FireActor("Proj_Fireball", 0, 0, 192) starts at the actor origin,
    // unlike A_FireProjectile's radius-plus-distance muzzle. Mantis has the
    // authored 48-unit radius (clipdist 72), so -48 cancels that generic
    // launch distance while retaining the shared allocation/appearance path.
    KexProjectileLaunch const launch = {
        nTarget, -48, 192, 8, false, true
    };
    int const nBullet = BuildBulletInternal(nSprite, 10, 0, 0, 0,
        sprite[nSprite].ang, nTarget + 10000, 1, &launch);
    if (nBullet < 0)
        return -1;

    Bullet &bullet = BulletList[nBullet];
    short const nProjectile = bullet.nSprite;
    int const yawSpread = KexMantisSpreadAngle();
    int const pitchSpread = KexMantisSpreadAngle();
    short const yaw = (sprite[nSprite].ang + yawSpread) & kAngleMask;

    int cosPitch, sinPitch;
    KexBulletAimPitch(sprite[nProjectile].x, sprite[nProjectile].y,
                      sprite[nProjectile].z, nTarget,
                      &cosPitch, &sinPitch);
    int const spreadCos = Cos(pitchSpread);
    int const spreadSin = Sin(pitchSpread);
    int const adjustedCos = mulscale14(cosPitch, spreadCos)
                          - mulscale14(sinPitch, spreadSin);
    int const adjustedSin = mulscale14(sinPitch, spreadCos)
                          + mulscale14(cosPitch, spreadSin);
    int const fx = mulscale14(Cos(yaw), adjustedCos);
    int const fy = mulscale14(Sin(yaw), adjustedCos);

    // Math::RandFloat() supplies the continuous 8..16 KEX speed. Keep it in
    // Q16 through conversion so the native projectile does not collapse the
    // range to eight integer choices.
    int const speedQ16 = (8 << 16)
        + (int)(((int64_t)RandomSize(16) * (8 << 16)) / 65535);
    int64_t const worldSpeedQ16 = (int64_t)speedQ16 * 6;
    bullet.x = (int)(((int64_t)fx * worldSpeedQ16) / 65536);
    bullet.y = (int)(((int64_t)fy * worldSpeedQ16) / 65536);
    bullet.z = -(int)(((int64_t)adjustedSin * worldSpeedQ16 * 16)
                       / (16384 * INT64_C(65536)));
    bullet.field_10 = (uint16_t)clamp<int>(
        (int)(((int64_t)speedQ16 * 24 + 32768) / 65536), 1, 29999);
    nBulletEnemy[nBullet] = -1;
    sprite[nProjectile].ang = yaw;

    if (s_kexBulletAudit)
    {
        int const speedMilli = (int)(((int64_t)speedQ16 * 1000 + 32768) / 65536);
        OSD_Printf("Kex Mantis PSX projectile: source %d target %d pos %d,%d,%d vel %d,%d,%d speed %d.%03d spread %d/%d radius %d repeat %d\n",
                   nSprite, nTarget, (int)sprite[nProjectile].x,
                   (int)sprite[nProjectile].y, (int)sprite[nProjectile].z,
                   bullet.x, bullet.y, bullet.z, speedMilli / 1000,
                   speedMilli % 1000, yawSpread, pitchSpread,
                   KexBulletRadius(10), (int)sprite[nProjectile].xrepeat);
    }
    return nBullet;
}

int KexBullet_CheckMantisFireProjectile(int nBullet, short nSource)
{
    if (!g_kexMode || (unsigned)nBullet >= (unsigned)kMaxBullets
        || (unsigned)nSource >= (unsigned)kMaxSprites)
        return 0;
    Bullet const &bullet = BulletList[nBullet];
    if (bullet.nType != 10
        || (unsigned)bullet.nSprite >= (unsigned)kMaxSprites
        || sprite[bullet.nSprite].sectnum < 0)
        return 0;
    spritetype const &projectile = sprite[bullet.nSprite];
    uint64_t const fixedWorldSpeed = KexBulletVectorLength(
        bullet.x, bullet.y, (int64_t)bullet.z * 1024);
    return projectile.x == sprite[nSource].x
        && projectile.y == sprite[nSource].y
        && projectile.z == sprite[nSource].z - 192 * 16 * 6
        && klabs(AngleDelta(sprite[nSource].ang, projectile.ang, 1024)) <= 20
        && fixedWorldSpeed >= UINT64_C(48) * 16384
        && fixedWorldSpeed <= UINT64_C(96) * 16384
        && bullet.field_10 >= 192 && bullet.field_10 <= 384
        && nBulletEnemy[nBullet] == -1
        && projectile.clipdist == (25 * 6 + 2) / 4
        && projectile.xrepeat == 40 && projectile.yrepeat == 40
        && projectile.shade == -127;
}

void KexBullet_TestTick()
{
    if (s_kexBulletTest == -1)
        s_kexBulletTest = g_kexMode && Bgetenv("VB_KEX_PROJECTILE_TEST")
            ? 0 : -2;
    if (s_kexBulletTest != 0)
        return;

    short const nTarget = PlayerList[nLocalPlayer].nSprite;
    if ((unsigned)nTarget >= (unsigned)kMaxSprites
        || sprite[nTarget].sectnum < 0 || !(sprite[nTarget].cstat & 0x101))
        return;

    struct TestRecipe
    {
        int nType, nSourceClip, nDistance, nHeight, nSpeed, bHomingThrust;
    };
    // Current retail projectile callbacks for Anubis, Sentry, Set, Selkis,
    // Mummy, Queen and Saturn Mantis respectively. Queen's FireActor starts at
    // the actor origin, hence the -64 radius cancellation. This also catches
    // the type-16 hitscan shortcut without requiring a live boss encounter.
    static TestRecipe const recipes[] = {
        { 8,  72,  4,  96, 16, 0 },
        { 13, 72,  4,  96, 24, 0 },
        { 11, 144, 50, 192, 16, 1 },
        { 16, 120, 2, 144, 64, 0 },
        { 9,  72,  2,  96, 12, 0 },
        { 12, 96, -64, 128, 32, 0 },
        { 10, 72,  8, 192, 16, 0 },
    };

    short const nSource = insertsprite(sprite[nTarget].sectnum, 0);
    if (nSource < 0)
    {
        OSD_Printf("Kex projectile test: FAIL (no free source sprite)\n");
        s_kexBulletTest = -2;
        return;
    }
    // Put the synthetic launcher behind the player and aim it forward so the
    // check exercises horizontal and vertical normalization, not a degenerate
    // same-origin vector.
    sprite[nSource].x = sprite[nTarget].x
        - mulscale14(Cos(sprite[nTarget].ang), 600);
    sprite[nSource].y = sprite[nTarget].y
        - mulscale14(Sin(sprite[nTarget].ang), 600);
    sprite[nSource].z = sprite[nTarget].z;
    sprite[nSource].ang = sprite[nTarget].ang;
    sprite[nSource].cstat = 0;
    sprite[nSource].xvel = 0;  // source actor-array index (synthetic actor 0)
    sprite[nSource].yvel = -1; // converted spawner marker: source sector 0
    sprite[nSource].zvel = 0;

    // Keep the native A_FireProjectile target point independent of animated
    // Build sprite repeat. The retail kexPuppet height is exactly 160 units.
    bool const logicalTargetCenterExact = sprite[nTarget].statnum == 100
        && KexBulletTargetCenterZ(nTarget)
            == sprite[nTarget].z - (160 * 16 * 6) / 2;

    unsigned passed = 0;
    bool redCobraSlitherExact = true;
    for (unsigned i = 0; i < ARRAY_SIZE(recipes); i++)
    {
        TestRecipe const &r = recipes[i];
        sprite[nSource].clipdist = r.nSourceClip;
        int const nBullet = BuildKexAIFireProjectile(nSource, r.nType,
            nTarget, r.nDistance, r.nHeight, r.nSpeed, r.bHomingThrust);
        bool const launchExact = KexBullet_CheckAIFireProjectile(
            nBullet, nSource, nTarget, r.nDistance, r.nHeight, r.nSpeed,
            r.bHomingThrust);
        if (launchExact)
            passed |= 1u << i;
        if (r.nType == 9 || r.nType == 16)
            redCobraSlitherExact &= launchExact
                && KexBullet_CheckRedCobraSlither(nBullet);
        if (nBullet >= 0)
            DestroyBullet(nBullet);
    }
    sprite[nSource].clipdist = 72;
    int const nMantisBullet = BuildKexMantisFireProjectile(nSource, nTarget);
    if (KexBullet_CheckMantisFireProjectile(nMantisBullet, nSource))
        passed |= 1u << ARRAY_SIZE(recipes);
    if (nMantisBullet >= 0)
        DestroyBullet(nMantisBullet);

    bool boulderLaunchExact = false;
    bool boulderMotionExact = false;
    int const nBoulder = BuildBullet(nSource, kKexBoulderType, 0, 0, 0,
                                     sprite[nSource].ang, 0, 1);
    if (nBoulder >= 0)
    {
        Bullet const &boulder = BulletList[nBoulder];
        bool const horizontalExact = boulder.field_10 == 12 * 24
            && boulder.field_E == kKexBoulderFloorContacts
            && boulder.x == Cos(sprite[nSource].ang) * kKexBoulderSpeed
            && boulder.y == Sin(sprite[nSource].ang) * kKexBoulderSpeed
            && boulder.z == 0
            && sprite[boulder.nSprite].x == sprite[nSource].x
            && sprite[boulder.nSprite].y == sprite[nSource].y
            && sprite[boulder.nSprite].z == sprite[nSource].z
            && sprite[boulder.nSprite].clipdist == (25 * 6 + 2) / 4
            && sprite[boulder.nSprite].xrepeat == 80
            && sprite[boulder.nSprite].yrepeat == 80;

        // Exercise the real movesprite path on the loaded map: one free-fall
        // tick must add 0.75 gravity, then a flat-floor contact must reflect
        // upward and consume exactly one of the eight authored contacts.
        Bullet &moving = BulletList[nBoulder];
        short const movingSprite = moving.nSprite;
        int const floorZ = getflorzofslope(sprite[movingSprite].sectnum,
                                           sprite[movingSprite].x,
                                           sprite[movingSprite].y);
        sprite[movingSprite].z = floorZ - 1024;
        moving.x = moving.y = moving.z = 0;
        bool const fell = MoveBullet(nBoulder) == 0
            && moving.z == kKexBoulderGravity
            && sprite[movingSprite].z == floorZ - 1024 + kKexBoulderGravity
            && moving.field_E == kKexBoulderFloorContacts;
        sprite[movingSprite].z = floorZ - 16;
        moving.z = 64;
        bool const bounced = MoveBullet(nBoulder) == 0
            && moving.z < 0
            && moving.field_E == kKexBoulderFloorContacts - 1
            && sprite[movingSprite].z == floorZ;
        boulderMotionExact = fell && bounced;
        DestroyBullet(nBoulder);

        sprite[nSource].zvel = 16384;
        int const nVertical = BuildBullet(nSource, kKexBoulderType, 0, 0, 0,
                                          sprite[nSource].ang, 0, 1);
        if (nVertical >= 0)
        {
            Bullet const &vertical = BulletList[nVertical];
            boulderLaunchExact = horizontalExact
                && vertical.x == 0 && vertical.y == 0
                && vertical.z == -kKexBoulderSpeed * 16
                && vertical.field_E == kKexBoulderFloorContacts;
            DestroyBullet(nVertical);
        }
    }
    if (boulderLaunchExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 1);

    bool sharedSpawnerLaunchExact = false;
    sprite[nSource].zvel = 16384;
    int const nFireball = BuildBullet(nSource, 14, 0, 0, 0,
                                      sprite[nSource].ang, 0, 1);
    if (nFireball >= 0)
    {
        Bullet const &fireball = BulletList[nFireball];
        bool const fireballExact = fireball.field_10 == 12 * 24
            && fireball.x == 0 && fireball.y == 0
            && fireball.z == -kKexSpawnerFireballSpeed * 16
            && KexBulletDamage(14) == 100 && KexBulletRadius(14) == 25
            && sprite[fireball.nSprite].clipdist == (25 * 6 + 2) / 4
            && sprite[fireball.nSprite].xrepeat == 40
            && sprite[fireball.nSprite].yrepeat == 40;
        DestroyBullet(nFireball);

        sprite[nSource].zvel = 0;
        int const nLaser = BuildBullet(nSource, 15, 0, 0, 0,
                                       sprite[nSource].ang, 0, 1);
        if (nLaser >= 0)
        {
            Bullet const &laser = BulletList[nLaser];
            sharedSpawnerLaunchExact = fireballExact
                && laser.field_10 == 16 * 24
                && laser.x == Cos(sprite[nSource].ang) * kKexSpawnerLaserSpeed
                && laser.y == Sin(sprite[nSource].ang) * kKexSpawnerLaserSpeed
                && laser.z == 0
                && KexBulletDamage(15) == 100 && KexBulletRadius(15) == 36
                && KexBulletGlidesOnPlanes(15)
                && sprite[laser.nSprite].clipdist == (36 * 6 + 2) / 4
                && sprite[laser.nSprite].xrepeat == 40
                && sprite[laser.nSprite].yrepeat == 40;
            DestroyBullet(nLaser);
        }
    }
    if (sharedSpawnerLaunchExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 2);

    bool impactEffectExact = false;
    if (KexBulletExactImpactAvailable(10))
    {
        int const x = sprite[nSource].x;
        int const y = sprite[nSource].y;
        int const z = sprite[nSource].z;
        short const nSector = sprite[nSource].sectnum;
        int const nFireImpact = KexProjectile_SpawnFireballImpact(
            x, y, z, nSector, 0, 1);
        bool fireExact = false;
        if (nFireImpact >= 0)
        {
            short const nEffect = GetAnimSprite(nFireImpact);
            fireExact = AnimList[nFireImpact].nSeq == SeqOffsets[kSeqKexImpact]
                && SeqSize[AnimList[nFireImpact].nSeq] == 14
                && sprite[nEffect].x == x && sprite[nEffect].y == y
                && sprite[nEffect].z == z
                && sprite[nEffect].xrepeat == 48
                && sprite[nEffect].yrepeat == 48
                && (AnimFlags[nFireImpact] & 4);
            DestroyAnim(nFireImpact);
            mydeletesprite(nEffect);
        }

        int const nBoulderImpact = KexProjectile_SpawnFireballImpact(
            x, y, z, nSector, 2, 0);
        bool boulderExact = false;
        if (nBoulderImpact >= 0)
        {
            short const nEffect = GetAnimSprite(nBoulderImpact);
            boulderExact = AnimList[nBoulderImpact].nSeq
                    == SeqOffsets[kSeqKexImpact]
                && sprite[nEffect].x == x && sprite[nEffect].y == y
                && sprite[nEffect].z == z - 2 * 16 * 6
                && sprite[nEffect].xrepeat == 48
                && sprite[nEffect].yrepeat == 48
                && (AnimFlags[nBoulderImpact] & 4);
            DestroyAnim(nBoulderImpact);
            mydeletesprite(nEffect);
        }
        impactEffectExact = fireExact && boulderExact;
    }

    bool deathFamilyExact = false;
    if (KexBulletExactImpactAvailable(8)
            && KexBulletExactImpactAvailable(9)
            && KexBulletExactImpactAvailable(11)
            && KexBulletExactImpactAvailable(12)
            && KexBulletExactImpactAvailable(13)
            && KexBulletExactImpactAvailable(15)
            && seq_IsLoaded(kSeqKexCobraGreen))
    {
        int const x = sprite[nSource].x;
        int const y = sprite[nSource].y;
        int const z = sprite[nSource].z;
        short const nSector = sprite[nSource].sectnum;

        int const nAnubisImpact = KexBulletSpawnAnubisImpact(
            x, y, z, nSector);
        bool anubisExact = false;
        if (nAnubisImpact >= 0)
        {
            short const nEffect = GetAnimSprite(nAnubisImpact);
            anubisExact = AnimList[nAnubisImpact].nSeq
                    == SeqOffsets[kSeqKexAnuImpact]
                && SeqSize[AnimList[nAnubisImpact].nSeq] == 13
                && sprite[nEffect].x == x && sprite[nEffect].y == y
                && sprite[nEffect].z == z
                && sprite[nEffect].xrepeat == 40
                && sprite[nEffect].yrepeat == 40
                && (AnimFlags[nAnubisImpact] & 4);
            DestroyAnim(nAnubisImpact);
            mydeletesprite(nEffect);
        }

        int const nCobraController = KexProjectile_StartCobraBurst(
            x, y, z, nSector, 1);
        bool cobraExact = false;
        if (nCobraController >= 0)
        {
            short const nController = GetAnimSprite(nCobraController);
            cobraExact = AnimList[nCobraController].nSeq
                    == SeqOffsets[kSeqKexCobraCtl]
                && SeqSize[AnimList[nCobraController].nSeq] == 12
                && (sprite[nController].cstat & 0x8000)
                && sprite[nController].xvel == 1
                && SeqSize[SeqOffsets[kSeqKexCobraRed]] == 14
                && SeqSize[SeqOffsets[kSeqKexCobraGreen]] == 14;
            DestroyAnim(nCobraController);
            mydeletesprite(nController);
        }

        deathFamilyExact = anubisExact && cobraExact
            && KexBulletHandleDeathVisual(-1, 13, x, y, z, nSector)
            && KexBulletHandleDeathVisual(-1, 15, x, y, z, nSector)
            && KexBulletSuppressClassicRadial(11)
            && KexBulletSuppressClassicRadial(12);
    }

    // Player flames use a different authored radius than creatures and do
    // not target an actor, so cover their muzzle math, visual growth and
    // inherited impact sizzle independently of the AI callback recipes.
    bool playerFlameExact = false;
    if (KexBulletExactImpactAvailable(kKexFlameType))
    {
        int const nFlame = BuildKexPlayerFlameProjectile(nTarget, 0);
        if (KexBullet_CheckPlayerFlame(nFlame, nTarget, 0))
        {
            Bullet &flame = BulletList[nFlame];
            short const nFlameSprite = flame.nSprite;
            flame.field_E = 12; // age eighteen: late-fizzle translucency
            KexFlameUpdateVisual(&flame);
            bool const visualExact = KexFlameRepeat(18) == 97
                && KexFlameRepeat(30) == 147
                && sprite[nFlameSprite].xrepeat == KexFlameRepeat(18)
                && sprite[nFlameSprite].yrepeat == KexFlameRepeat(18)
                && sprite[nFlameSprite].pal == 4
                && sprite[nFlameSprite].shade == 9
                && (sprite[nFlameSprite].cstat & 2);

            int const nSizzle = KexBulletSpawnFlameSizzle(
                sprite[nFlameSprite].x, sprite[nFlameSprite].y,
                sprite[nFlameSprite].z, sprite[nFlameSprite].sectnum,
                sprite[nFlameSprite].xrepeat, sprite[nFlameSprite].shade,
                (sprite[nFlameSprite].cstat & 2) != 0);
            bool sizzleExact = false;
            if (nSizzle >= 0)
            {
                short const nSizzleSprite = GetAnimSprite(nSizzle);
                sizzleExact = AnimList[nSizzle].nSeq
                        == SeqOffsets[kSeqKexFlame] + 1
                    && SeqSize[AnimList[nSizzle].nSeq] == 16
                    && sprite[nSizzleSprite].xrepeat
                        == sprite[nFlameSprite].xrepeat
                    && sprite[nSizzleSprite].yrepeat
                        == sprite[nFlameSprite].yrepeat
                    && sprite[nSizzleSprite].pal == 4
                    && sprite[nSizzleSprite].shade == sprite[nFlameSprite].shade
                    && (AnimFlags[nSizzle] & 4)
                    && (sprite[nSizzleSprite].cstat & 2);
                DestroyAnim(nSizzle);
                mydeletesprite(nSizzleSprite);
            }
            playerFlameExact = visualExact && sizzleExact;
        }
        if (nFlame >= 0)
            DestroyBullet(nFlame);
    }

    // A_FireRa is a separate player callback, not an AI BulletList recipe:
    // it starts from alternating side muzzles and uses the complete eight
    // GameTicks aim wave. Exercise the actual Kex-break projectile path,
    // including its authored actor profile and source ownership.
    bool const playerRaExact = KexRaFire_PlayerTest(nTarget, 256) != 0;
    mydeletesprite(nSource);

    if (KexBulletImpactContractMatches())
        passed |= 1u << (ARRAY_SIZE(recipes) + 3);
    if (KexBoulderPhysicsContractMatches() && boulderMotionExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 4);
    if (impactEffectExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 5);
    if (deathFamilyExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 6);
    if (logicalTargetCenterExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 7);
    if (playerFlameExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 8);
    if (playerRaExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 9);
    if (redCobraSlitherExact)
        passed |= 1u << (ARRAY_SIZE(recipes) + 10);

    unsigned const wanted = (1u << (ARRAY_SIZE(recipes) + 11)) - 1;
    OSD_Printf("Kex projectile test: %s (recipes+spawners+impact+physics+effect+logical-target-centre+player-flame+player-ra+red-cobra-slither %x/%x; Anubis/Sentry/Set/Selkis/Mummy/Queen/Mantis-Saturn/Mantis-PSX; exact 3-D fireball/laser/boulder launch; logical 160-unit player target centre; authored death blasts; exact orange/cobra/Anubis impact art, cadence and light; player flame muzzle/lifetime/fizzle/sizzle; player Ra alternating muzzle, aim wave, owner and actor profile; Mummy/Selkis 60-Hz slither, pitch and speed; no false Soul/Seedling radial or Blaster/Beam puff; boulder gravity/reflection/expiry)\n",
               passed == wanted ? "PASS" : "FAIL", passed, wanted);
    s_kexBulletTest = -2;
}

void FuncBullet(int a, int UNUSED(b), int nRun)
{
    short nBullet = RunData[nRun].nVal;
    assert(nBullet >= 0 && nBullet < kMaxBullets);

    short nSeq = SeqOffsets[BulletList[nBullet].nSeq];
    short nSprite = BulletList[nBullet].nSprite;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x20000:
        {
            short nFlag = FrameFlag[SeqBase[nSeq] + BulletList[nBullet].nFrame];

            seq_MoveSequence(nSprite, nSeq, BulletList[nBullet].nFrame);

            int const kexTile = KexBulletTile(BulletList[nBullet].nType);
            if (kexTile >= 0)
                sprite[nSprite].picnum = kexTile;
            if (BulletList[nBullet].nType == kKexFlameType)
                KexFlameUpdateVisual(&BulletList[nBullet]);

            if (nFlag & 0x80)
            {
                BuildAnim(-1, 45, 0, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum, sprite[nSprite].xrepeat, 0);
            }

            BulletList[nBullet].nFrame++;
            if (BulletList[nBullet].nFrame >= SeqSize[nSeq])
            {
                if (!BulletList[nBullet].field_12)
                {
                    BulletList[nBullet].nSeq = BulletInfo[BulletList[nBullet].nType].nSeq;
                    BulletList[nBullet].field_12++;
                }

                BulletList[nBullet].nFrame = 0;
            }

            if (!(g_kexMode && BulletList[nBullet].nType == kKexBoulderType)
                && BulletList[nBullet].field_E != -1
                && --BulletList[nBullet].field_E == 0)
            {
                DestroyBullet(nBullet);
            }
            else
            {
                MoveBullet(nBullet);
            }
            break;
        }

        case 0x90000:
        {
            short nSprite2 = a & 0xFFFF;
            tsprite[nSprite2].statnum = 1000;

            if (BulletList[nBullet].nType == 15)
            {
                seq_PlotArrowSequence(nSprite2, nSeq, BulletList[nBullet].nFrame);
            }
            else
            {
                seq_PlotSequence(nSprite2, nSeq, BulletList[nBullet].nFrame, 0);
                int const kexTile = KexBulletTile(BulletList[nBullet].nType);
                if (kexTile >= 0)
                    tsprite[nSprite2].picnum = kexTile;
                tsprite[nSprite2].owner = -1;
            }
            break;
        }

        case 0xA0000:
            break;

        default:
        {
            DebugOut("unknown msg %d for bullet\n", nMessage);
            return;
        }
    }
}

class BulletLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void BulletLoadSave::Load()
{
    Read(BulletFree, sizeof(BulletFree));
    Read(BulletList, sizeof(BulletList));
    Read(nBulletEnemy, sizeof(nBulletEnemy));
    Read(s_kexCobraPitch, sizeof(s_kexCobraPitch));
    Read(s_kexCobraJoint, sizeof(s_kexCobraJoint));

    Read(&nBulletsFree, sizeof(nBulletsFree));
    Read(&lasthitz, sizeof(lasthitz));
    Read(&lasthitx, sizeof(lasthitx));
    Read(&lasthity, sizeof(lasthity));
    Read(&lasthitsect, sizeof(lasthitsect));
    Read(&lasthitsprite, sizeof(lasthitsprite));
    Read(&lasthitwall, sizeof(lasthitwall));
    Read(&nBulletCount, sizeof(nBulletCount));
    Read(&nRadialBullet, sizeof(nRadialBullet));
}

void BulletLoadSave::Save()
{
    Write(BulletFree, sizeof(BulletFree));
    Write(BulletList, sizeof(BulletList));
    Write(nBulletEnemy, sizeof(nBulletEnemy));
    Write(s_kexCobraPitch, sizeof(s_kexCobraPitch));
    Write(s_kexCobraJoint, sizeof(s_kexCobraJoint));

    Write(&nBulletsFree, sizeof(nBulletsFree));
    Write(&lasthitz, sizeof(lasthitz));
    Write(&lasthitx, sizeof(lasthitx));
    Write(&lasthity, sizeof(lasthity));
    Write(&lasthitsect, sizeof(lasthitsect));
    Write(&lasthitsprite, sizeof(lasthitsprite));
    Write(&lasthitwall, sizeof(lasthitwall));
    Write(&nBulletCount, sizeof(nBulletCount));
    Write(&nRadialBullet, sizeof(nRadialBullet));
}

static BulletLoadSave* myLoadSave;

void BulletLoadSaveConstruct()
{
    myLoadSave = new BulletLoadSave();
}
