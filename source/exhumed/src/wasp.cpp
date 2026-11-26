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

#include "wasp.h"
#include "engine.h"
#include "runlist.h"
#include "random.h"
#include "exhumed.h"
#include "sequence.h"
#include "init.h"
#include "player.h"
#include "move.h"
#include "anims.h"
#include "trigdat.h"
#include "sound.h"
#include <assert.h>
#include "save.h"
#include "kexbreak.h"   // VibeBuild32: KEX death sound
#include "kexinv.h"

#define kMaxWasps   100

static short nVelShift = 0;
short nWaspCount;

struct Wasp
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nRun;
    short nTarget;
    short field_C;
    short nAttackTime;
    short dTime;
    short nVelocity;
    short field_14;
    short nDamage;
};

Wasp WaspList[kMaxWasps];

enum KexFlyerKind : uint8_t
{
    kKexFlyerWasp = 0,
    kKexFlyerBird,
    kKexFlyerSeedling,
};

// The three remaster actors share the native Wasp run record, but not their
// authored identity. The marker is mirrored in sprite.hitag so save games can
// reconstruct it without changing the classic Wasp struct or save layout.
static uint8_t s_waspKind[kMaxWasps];
static int s_flyerAudit = -1;
static uint8_t s_flyerAuditSeen;
static int s_flyerTest = -1;
static short s_flyerTestWasp = -1;
static short s_flyerTestPlayer = -1;
static short s_flyerTestInvincibility;
static int s_waspBobTest = -1;
static int s_birdFlightTest = -1;

// kexSlaveActor::GameTicks advances at 60 Hz, while the original Exhumed
// simulation advances at 30 Hz. Keep the source clock and its vertical
// velocity in the two fields that classic Wasps use for their idle bob; this
// is KEX-Wasp-only state, is serialized with the controller, and leaves the
// original game untouched.
static constexpr short kKexWaspBobState = INT16_MIN;
static constexpr int kKexWaspTicksPerCycle = 360 * 32;
static constexpr int kKexZPerUnit = 16 * 6;

static actionSeq ActionSeq[] = {
    {0,  0},
    {0,  0},
    {9,  0},
    {18, 0},
    {27, 1},
    {28, 1},
    {29, 1}
};

// HAWK.SEQ has dedicated KEX action groups: calm, eight-tick attack,
// twelve-tick retreat, and six-frame chase. Death is immediate in KEX mode,
// so the remaining native Wasp action slots only need a safe single view.
static actionSeq BirdActionSeq[] = {
    {0,  0},
    {27, 0},
    {9,  0},
    {18, 0},
    {35, 1},
    {35, 1},
    {35, 1}
};

// The generated KEX WASP.SEQ follows the same action-group ordering as the
// bird sequence, with authored 4/8/1/4 base/attack/backoff/chase cadence.
// Stock WASP.SEQ is detected below and keeps ActionSeq as a safe fallback.
static actionSeq WaspActionSeq[] = {
    {0,  0},
    {27, 0},
    {9,  0},
    {18, 0},
    {35, 1},
    {35, 1},
    {35, 1}
};


void InitWasps()
{
    nWaspCount = 0;
    Bmemset(s_waspKind, 0, sizeof(s_waspKind));
    s_flyerAuditSeen = 0;
    s_flyerTest = -1;
    s_flyerTestWasp = -1;
    s_flyerTestPlayer = -1;
    s_waspBobTest = -1;
    s_birdFlightTest = -1;
}

void SetWaspVel(short nSprite)
{
    if (nVelShift < 0)
    {
        sprite[nSprite].xvel = Cos(sprite[nSprite].ang) << -nVelShift;
        sprite[nSprite].yvel = Sin(sprite[nSprite].ang) << -nVelShift;
    }
    else
    {
        sprite[nSprite].xvel = Cos(sprite[nSprite].ang) >> nVelShift;
        sprite[nSprite].yvel = Sin(sprite[nSprite].ang) >> nVelShift;
    }
}

int GetWaspSprite(int nWasp)
{
    assert(nWasp >= 0 && nWasp < kMaxWasps);
    return WaspList[nWasp].nSprite;
}

int KexSeedlingCount()
{
    int count = 0;
    for (int i = 0; g_kexMode && i < nWaspCount; i++)
    {
        if (s_waspKind[i] != kKexFlyerSeedling || WaspList[i].nHealth <= 0)
            continue;
        int const nSprite = WaspList[i].nSprite;
        if ((unsigned)nSprite < (unsigned)kMaxSprites
            && sprite[nSprite].sectnum >= 0)
            count++;
    }
    return count;
}

static int KexFlyerSequence(int nWasp)
{
    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerBird && seq_IsLoaded(kSeqHawk))
        return kSeqHawk;
    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerSeedling
        && seq_IsLoaded(kSeqSeedling))
        return kSeqSeedling;
    return kSeqWasp;
}

static bool KexWaspSequenceExact()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqWasp))
        return false;
    int const base = SeqOffsets[kSeqWasp];
    // The demo has 13 frames at slot 9 and a single-view death at 27. The
    // generated retail sequence has an eight-frame attack and four-frame
    // directional chase there, which is enough to distinguish them safely.
    return SeqSize[base + 9] == 8 && SeqSize[base + 27] == 4;
}

static bool KexBirdSequenceExact()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqHawk))
        return false;
    int const base = SeqOffsets[kSeqHawk];
    // Generated retail HAWK.SEQ is calm/attack/retreat/chase = 1/8/12/6.
    // The guard keeps the stock controller untouched when that private
    // conversion sequence is unavailable.
    return SeqSize[base] == 1 && SeqSize[base + 9] == 8
        && SeqSize[base + 18] == 12 && SeqSize[base + 27] == 6;
}

static actionSeq const &KexFlyerActionSequence(int nWasp, int nAction)
{
    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerBird)
        return BirdActionSeq[nAction];
    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerWasp
        && KexWaspSequenceExact())
        return WaspActionSeq[nAction];
    return ActionSeq[nAction];
}

static int KexFlyerChaseVelocity(int nWasp)
{
    if (!g_kexMode)
        return 1500;

    // AngleChase receives four times the desired Build displacement. KEX XY
    // is converted at WORLD=6, so AI_Bird's moveSpeed 8 becomes 192 and the
    // AI_Wasp's 16 becomes 384.
    int const speed = s_waspKind[nWasp] == kKexFlyerBird ? 8 : 16;
    return speed * 6 * 4;
}

static char const *KexFlyerName(int nWasp)
{
    switch (s_waspKind[nWasp])
    {
        case kKexFlyerBird: return "bird";
        case kKexFlyerSeedling: return "seedling";
        default: return "wasp";
    }
}

static void KexFlyerAudit(int nWasp)
{
    if (s_flyerAudit < 0)
        s_flyerAudit = g_kexMode && Bgetenv("VB_KEX_FLYER_AUDIT") ? 1 : 0;
    int const kind = s_waspKind[nWasp];
    if (!s_flyerAudit || (s_flyerAuditSeen & (1u << kind)))
        return;
    s_flyerAuditSeen |= 1u << kind;
    int const nSprite = WaspList[nWasp].nSprite;
    OSD_Printf("Kex flyer: %s sprite %d hp %d damage %d clip %d repeat %d speed %d seq %d\n",
               KexFlyerName(nWasp), nSprite, KexHealthPoints(WaspList[nWasp].nHealth),
               WaspList[nWasp].nDamage, (int)sprite[nSprite].clipdist,
               (int)sprite[nSprite].xrepeat, KexFlyerChaseVelocity(nWasp) / 24,
               KexFlyerSequence(nWasp));
}

static void KexFlyerDelete(short nWasp, short nSprite)
{
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(WaspList[nWasp].nRun);
    sprite[nSprite].cstat = 0x8000;
    mydeletesprite(nSprite);
}

static void KexBirdDeath(short nWasp, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);

    // A_BirdDeath spawns seven Obj_Bird_Feathers around the corpse. The
    // dedicated actor owns source-randomized placement, frame randomization,
    // f8/f14 fall callbacks and floor removal. Keep old converted installs
    // functional with the former chunk-only approximation if its optional
    // sequence is unavailable.
    if (seq_IsLoaded(kSeqKexBirdFeather))
    {
        for (int i = 0; i < 7; i++)
            KexBirdFeather_Spawn(sprite[nSprite].x, sprite[nSprite].y,
                                 sprite[nSprite].z, sprite[nSprite].sectnum);
    }
    else
    {
        for (int i = 0; i < 7; i++)
            BuildCreatureChunk(nSprite | 0x4000, 7632);
    }

    int const sound = KexSound_Get("bird_die");
    if (sound >= 0)
        PlayLocalSound(sound, 0);
    KexFlyerDelete(nWasp, nSprite);
}

static void KexSeedlingDeath(short nWasp, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 32,
        sprite[nSprite].xrepeat, nKiller);

    // seedling_death: Queen gibs 1, 3, 4, 5 and 6, then destroy the host.
    static const int gibTiles[] = { 7841, 7843, 7844, 7845, 7846 };
    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);
    KexFlyerDelete(nWasp, nSprite);
}

static void KexWaspDeath(short nWasp, short nSprite, short nKiller)
{
    KexGibDeath_Spawn(sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z, sprite[nSprite].sectnum, 0,
        sprite[nSprite].xrepeat, nKiller);

    // wasp_death.tsc tosses one each of gib1/2/3, three gib4 and two gib5,
    // then destroys the host actor. Bird-skinned wasps use their separate
    // HAWK sequence and retain the native bird death path.
    static const int gibTiles[] = { 7823, 7824, 7825, 7826, 7826, 7826, 7827, 7827 };

    // A_WaspDeath randomly chooses the two authored death calls.
    int const sound = KexSound_Get(RandomBit() ? "wasp_die" : "wasp_dy1");
    if (sound >= 0)
        PlayLocalSound(sound, 0);

    for (size_t i = 0; i < ARRAY_SIZE(gibTiles); i++)
        KexTossCreatureChunk(nSprite, gibTiles[i]);

    KexFlyerDelete(nWasp, nSprite);
}

static bool KexSeedlingTargetValid(short nTarget)
{
    return nTarget >= 0 && sprite[nTarget].statnum == 100
        && (sprite[nTarget].cstat & 0x101)
        && !(SectFlag[sprite[nTarget].sectnum] & kSectUnderwater);
}

static bool KexSeedlingMeleeRange(short nSprite, short nTarget)
{
    if (!KexSeedlingTargetValid(nTarget))
        return false;

    int64_t const dx = (int64_t)sprite[nTarget].x - sprite[nSprite].x;
    int64_t const dy = (int64_t)sprite[nTarget].y - sprite[nSprite].y;
    // Actor radius 36 + meleeExtraDist/A_MeleeAttack reach 48, plus the
    // target's own Build collision radius. All authored XY units are WORLD=6.
    int64_t const reach = (36 + 48) * 6 + (int)sprite[nTarget].clipdist * 4;
    int64_t const dz = klabs((int64_t)sprite[nTarget].z - sprite[nSprite].z);
    return dx * dx + dy * dy <= reach * reach && dz <= 96 * 16 * 6;
}

static bool KexAirFlyerMeleeRange(short nWasp, short nSprite, short nTarget)
{
    if (nTarget < 0 || !(sprite[nTarget].cstat & 0x101)
        || (SectFlag[sprite[nTarget].sectnum] & kSectUnderwater))
        return false;

    int const bird = s_waspKind[nWasp] == kKexFlyerBird;
    int const radius = bird ? 16 : 18;
    int const attackReach = bird ? 32 : 16;
    int const height = bird ? 128 : 64;
    int64_t const dx = (int64_t)sprite[nTarget].x - sprite[nSprite].x;
    int64_t const dy = (int64_t)sprite[nTarget].y - sprite[nSprite].y;
    int64_t const dz = klabs((int64_t)sprite[nTarget].z - sprite[nSprite].z);
    int64_t const reach = (radius + attackReach) * 6
                        + (int)sprite[nTarget].clipdist * 4;
    return dx * dx + dy * dy <= reach * reach && dz <= (int64_t)height * 16 * 6;
}

static int KexWaspActorHeight(int nSprite)
{
    // A_WaspBob works from actor origins and authored actor heights, not from
    // the variable pixel bounds of the currently displayed Build tile.
    if (sprite[nSprite].statnum == 100)
        return 160 * kKexZPerUnit; // Obj_Player
    int const height = KexDrop_ActorHeight(nSprite);
    return height > 0 ? height * kKexZPerUnit : GetSpriteHeight(nSprite);
}

static bool KexWaspBobWrites(int64_t heightLevel, int64_t dx, int64_t dy,
                              int64_t dz, bool hasTarget)
{
    // scripts/p_enemy.txt:A_WaspBob: an absent target always permits the
    // write; a target suppresses it outside +/-128 height units and at or
    // inside a full three-dimensional 192-unit origin distance.
    if (!hasTarget)
        return true;
    if (heightLevel > 128 * kKexZPerUnit
        || heightLevel < -128 * kKexZPerUnit)
        return false;

    // X/Y are WORLD=6 while Z is SCALE(16)*WORLD.  Use the Z scale as a
    // common denominator so this stays an exact squared-distance test.
    dx *= 16;
    dy *= 16;
    int64_t const reach = 192 * kKexZPerUnit;
    return dx * dx + dy * dy + dz * dz > reach * reach;
}

static bool KexWaspBobWritesForTarget(int nSprite, int nTarget)
{
    if (nTarget < 0)
        return true;

    int const actorHeight = 64 * kKexZPerUnit; // AI_Wasp's authored height
    int const targetHeight = KexWaspActorHeight(nTarget);

    // KEX Z is up and Build Z is down.  This is the source expression
    // ((origin.z + height) - target.origin.z) - target.height, converted to
    // Build coordinates before its signed +/-128 comparison.
    int64_t const heightLevel = (int64_t)sprite[nTarget].z - sprite[nSprite].z
                              + actorHeight - targetHeight;
    return KexWaspBobWrites(heightLevel,
                             (int64_t)sprite[nTarget].x - sprite[nSprite].x,
                             (int64_t)sprite[nTarget].y - sprite[nSprite].y,
                             (int64_t)sprite[nTarget].z - sprite[nSprite].z,
                             true);
}

static int KexWaspBobRawVelocity(int gameTicks)
{
    int const degrees = (gameTicks / 32) % 360;
    int const angle = degrees * 2048 / 360;
    // A_WaspBob writes cos(bob)*2 in KEX's up-positive Z.  Build's z grows
    // downward, so the value is deliberately negated here.
    return -mulscale14(Cos(angle), 2 * kKexZPerUnit);
}

static int KexWaspBobTick(short &gameTicks, short &velocity, bool writeBob)
{
    if (writeBob)
        velocity = (short)KexWaspBobRawVelocity(gameTicks);

    // AI_Wasp declares friction .94 and verticalFriction 1.  UpdateVelocity
    // applies it after its animation callback and before this tick's move.
    velocity = (short)((int)velocity * 94 / 100);
    gameTicks++;
    if (gameTicks >= kKexWaspTicksPerCycle)
        gameTicks = 0;
    return velocity;
}

static int KexWaspBobAdvance(int nWasp, int nSprite, int nTarget,
                              bool hasBobCallback)
{
    Wasp &wasp = WaspList[nWasp];
    if (wasp.field_14 != kKexWaspBobState)
    {
        // KEX saves made before this bridge kept the classic randomized
        // fields. Re-enter cleanly at the equivalent global source phase.
        wasp.nAttackTime = (short)((totalmoves * 2) % kKexWaspTicksPerCycle);
        wasp.dTime = 0;
        wasp.field_14 = kKexWaspBobState;
    }

    bool const writeBob = hasBobCallback
                       && KexWaspBobWritesForTarget(nSprite, nTarget);
    // KEX's default animSpeed is .5: a normal frame (including every
    // wasp_chase frame) advances and runs its action once every two 60 Hz
    // actor ticks. The intervening tick keeps the callback's velocity and
    // only applies the actor's vertical friction before it moves.
    int const z0 = KexWaspBobTick(wasp.nAttackTime, wasp.dTime, writeBob);
    int const z1 = KexWaspBobTick(wasp.nAttackTime, wasp.dTime, false);
    // Keep the last KEX velocity observable to normal sprite users, but move
    // by both 60 Hz substeps below so 30 Hz PCExhumed lands at the same time.
    sprite[nSprite].zvel = wasp.dTime;
    return z0 + z1;
}

static int KexWaspMove(int nSprite, int zMove)
{
    return movesprite(nSprite,
                      KexScaleCreatureMove(nSprite, sprite[nSprite].xvel << 8),
                      KexScaleCreatureMove(nSprite, sprite[nSprite].yvel << 8),
                      zMove, 15360, -5120, CLIPMASK0);
}

static int KexWaspAngleChase(int nSprite, int nTarget, int velocity,
                              int turnStep, int zMove)
{
    // The old AngleChase stores target pitch in sprite.zvel and then moves by
    // that pitch. A_WaspBob instead replaces KEX's vertical velocity after
    // the chase think, so reproduce AngleChase's yaw/turn contract while
    // keeping the source's independent X/Y and sampled Z motion.
    velocity = KexScaleCreatureMove((short)nSprite, velocity);
    if (nTarget < 0)
        return KexWaspMove(nSprite, zMove);

    int const targetAngle = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                       sprite[nTarget].y - sprite[nSprite].y);
    int angleDelta = AngleDelta(sprite[nSprite].ang, targetAngle, 1024);
    int const absDelta = klabs(angleDelta);
    if (absDelta > 63)
    {
        int const divisor = max(1, klabs(angleDelta >> 6));
        velocity = max(5, velocity / divisor);
    }

    if (klabs(angleDelta) > turnStep)
        angleDelta = angleDelta < 0 ? -turnStep : turnStep;
    int const angle = (sprite[nSprite].ang + angleDelta) & kAngleMask;
    sprite[nSprite].ang = angle;
    return movesprite(nSprite, (Cos(angle) * velocity) >> 2,
                      (Sin(angle) * velocity) >> 2, zMove,
                      0, 0, CLIPMASK0);
}

static int KexBirdAdvanceGameTicks(short &gameTicks)
{
    int const sourceTicks = gameTicks;
    // A_BirdWander only observes bit 512, so preserving this 2048-tick
    // period is equivalent to the source actor's unbounded GameTicks value.
    gameTicks = (short)((sourceTicks + 2) & 2047);
    return sourceTicks;
}

static int KexBirdTurnStep(short &remainder, int numerator)
{
    // Build angles are 2048/360 turns.  Keep the fractional component of
    // 1.5 degrees (384/45) and 4 degrees (1024/45) so repeated callbacks do
    // not accumulate the old rounded 9-degree-unit drift.
    int const scaled = numerator + remainder % 45;
    int const step = scaled / 45;
    remainder = (short)(scaled - step * 45);
    return step;
}

static void KexBirdSetXYVelocity(int nSprite, int speed)
{
    int const worldSpeed = speed * 6;
    sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * worldSpeed) >> 8;
    sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * worldSpeed) >> 8;
}

static int KexBirdMovePair(int nSprite)
{
    // AI_Bird has friction 0 and no verticalFriction.  Its calm and retreat
    // actions run on every .5-speed animation advance, but their velocity
    // survives through the following 60 Hz physics step as well.
    int const first = MoveCreature(nSprite);
    int const second = MoveCreature(nSprite);
    return first ? first : second;
}

static void KexBirdWander(int nWasp, int nSprite, int gameTicks)
{
    Wasp &wasp = WaspList[nWasp];
    if (!wasp.field_14)
        wasp.field_14 = RandomSize(11);

    int const turn = ((gameTicks + wasp.field_14) & 512) != 0 ? 384 : -384;
    sprite[nSprite].ang = (sprite[nSprite].ang
        + KexBirdTurnStep(wasp.dTime, turn)) & kAngleMask;
    KexBirdSetXYVelocity(nSprite, 2);
}

static bool KexBirdRetreatStops(int z, int zVelocity, int ceiling)
{
    // A_BirdRetreat:
    // velocity.z < 3 || origin.z + velocity.z >= ceilingHeight-height-1.
    // Convert KEX's up-positive coordinates to Build's down-positive Z.
    int64_t const stopZ = (int64_t)ceiling + (128 + 1) * kKexZPerUnit;
    return zVelocity > -(3 * kKexZPerUnit)
        || (int64_t)z + zVelocity <= stopZ;
}

static void KexBirdFinishRetreat(int nWasp, int nSprite)
{
    Wasp &wasp = WaspList[nWasp];
    wasp.nAction = 0;
    wasp.nFrame = 0;
    wasp.nTarget = -1;
    wasp.field_C = RandomSize(6);
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
}

static bool KexBirdRetreat(int nWasp, int nSprite)
{
    Wasp &wasp = WaspList[nWasp];
    sprite[nSprite].ang = (sprite[nSprite].ang
        + KexBirdTurnStep(wasp.dTime, 1024)) & kAngleMask;
    KexBirdSetXYVelocity(nSprite, 4);

    int const ceiling = getceilzofslope(sprite[nSprite].sectnum,
                                         sprite[nSprite].x, sprite[nSprite].y);
    if (KexBirdRetreatStops(sprite[nSprite].z, sprite[nSprite].zvel, ceiling))
    {
        KexBirdFinishRetreat(nWasp, nSprite);
        return false;
    }
    return true;
}

// AI_Seedling is a grounded, no-drop-off melee actor. Classic Queen eggs
// hatch a Wasp, which is why the old compatibility path inherited the flying
// controller; the remaster explicitly spawns this different actor instead.
static void FuncKexSeedling(int a, int nDamage, short nWasp)
{
    short const nSprite = WaspList[nWasp].nSprite;
    int const nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x90000:
        {
            actionSeq const &action = KexFlyerActionSequence(
                nWasp, WaspList[nWasp].nAction);
            int const nSeq = SeqOffsets[KexFlyerSequence(nWasp)] + action.a;
            seq_PlotSequence(a & 0xFFFF, nSeq, WaspList[nWasp].nFrame,
                             action.b);
            return;
        }

        case 0xA0000:
            if (!(sprite[nSprite].cstat & 0x101))
                return;
            nDamage = runlist_CheckRadialDamage(nSprite);
            fallthrough__;

        case 0x80000:
            if (nDamage <= 0 || WaspList[nWasp].nHealth <= 0)
                return;
            WaspList[nWasp].nHealth -= nDamage;
            KexDifficulty_PainRoll(0);   // painChance 0 still consumes the byte
            if (WaspList[nWasp].nHealth <= 0)
            {
                WaspList[nWasp].nHealth = 0;
                short const nKiller = nMessage == 0xA0000
                    ? (short)nRadialOwner : (short)(a & 0xFFFF);
                KexSeedlingDeath(nWasp, nSprite, nKiller);
                nCreaturesLeft--;
            }
            return;

        case 0x20000:
        {
            Gravity(nSprite);
            short const nAction = WaspList[nWasp].nAction;
            int const nSeq = SeqOffsets[KexFlyerSequence(nWasp)]
                           + KexFlyerActionSequence(nWasp, nAction).a;
            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, WaspList[nWasp].nFrame);
            seq_MoveSequence(nSprite, nSeq, WaspList[nWasp].nFrame);

            WaspList[nWasp].nFrame++;
            bool const wrapped = WaspList[nWasp].nFrame >= SeqSize[nSeq];
            if (wrapped)
                WaspList[nWasp].nFrame = 0;

            short nTarget = WaspList[nWasp].nTarget;
            if (!KexSeedlingTargetValid(nTarget))
            {
                nTarget = FindPlayer(nSprite, 60);
                WaspList[nWasp].nTarget = nTarget;
            }

            if (nAction == 2)
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                MoveCreatureWithCaution(nSprite);

                // seedling_attack applies A_MeleeAttack(48, 20) on frame 3.
                // nFrame has already advanced, so four means frame 3 ran.
                if (WaspList[nWasp].nFrame == 4 && KexSeedlingMeleeRange(nSprite, nTarget))
                    runlist_DamageEnemy(nTarget, nSprite, 20);

                if (wrapped)
                {
                    WaspList[nWasp].nAction = KexSeedlingMeleeRange(nSprite, nTarget) ? 2 : 0;
                    WaspList[nWasp].nFrame = 0;
                }
                return;
            }

            if (nTarget < 0)
            {
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                MoveCreatureWithCaution(nSprite);
                return;
            }

            PlotCourseToSprite(nSprite, nTarget);
            if (KexSeedlingMeleeRange(nSprite, nTarget))
            {
                WaspList[nWasp].nAction = 2;
                WaspList[nWasp].nFrame = 0;
                sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                MoveCreatureWithCaution(nSprite);
                return;
            }

            int const nWorldSpeed = 16 * 6;
            sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * nWorldSpeed) >> 8;
            sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * nWorldSpeed) >> 8;
            int const nMove = MoveCreatureWithCaution(nSprite);
            if ((nMove & 0xC000) == 0x8000)
                sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
            return;
        }
    }
}

int BuildWasp(short nSprite, int x, int y, int z, short nSector, short nAngle, int bBird)
{
    if (nWaspCount >= kMaxWasps) {
        return -1;
    }

    short nWasp = nWaspCount;
    nWaspCount++;

    uint8_t bEggWasp = kFalse;
    if (nSprite == -2) {
        bEggWasp = kTrue;
    }

    if (nSprite < 0)
    {
        nSprite = insertsprite(nSector, 107);
        assert(nSprite >= 0 && nSprite < kMaxSprites);

        sprite[nSprite].x = x;
        sprite[nSprite].y = y;
        sprite[nSprite].z = z;
    }
    else
    {
        nAngle = sprite[nSprite].ang;
        changespritestat(nSprite, 107);
    }

    int const nKind = g_kexMode
        ? (bBird ? kKexFlyerBird : (bEggWasp ? kKexFlyerSeedling : kKexFlyerWasp))
        : kKexFlyerWasp;
    s_waspKind[nWasp] = (uint8_t)nKind;

    sprite[nSprite].shade = g_kexMode && nKind == kKexFlyerBird ? -128 : -12;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    if (g_kexMode)
    {
        // radius 18/16/36 and scale 1.25/1.5/2.0 from the retail actor defs.
        int const radius = nKind == kKexFlyerBird ? 16
                         : nKind == kKexFlyerSeedling ? 36 : 18;
        int const repeat = nKind == kKexFlyerBird ? 60
                         : nKind == kKexFlyerSeedling ? 80 : 50;
        sprite[nSprite].clipdist = (uint8_t)(radius * 6 / 4);
        sprite[nSprite].xrepeat = (uint8_t)repeat;
        sprite[nSprite].yrepeat = (uint8_t)repeat;
    }
    else
    {
        sprite[nSprite].clipdist = 70;
        sprite[nSprite].xrepeat = bEggWasp ? 20 : 50;
        sprite[nSprite].yrepeat = bEggWasp ? 20 : 50;
    }

    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].hitag = g_kexMode ? (int16_t)nKind : 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

//	GrabTimeSlot(3);

    WaspList[nWasp].nAction = 0;
    WaspList[nWasp].nFrame  = 0;
    WaspList[nWasp].nSprite = nSprite;
    WaspList[nWasp].nTarget = -1;
    // Authored actor health: Wasp 60, Bird 10, Queen Seedling 100. Both
    // attacks deal 20; classic keeps its 800/10 balance.
    WaspList[nWasp].nHealth = !g_kexMode ? 800
        : nKind == kKexFlyerBird ? KexHealthUnits(10)
        : nKind == kKexFlyerSeedling ? KexHealthUnits(100) : KexHealthUnits(60);
    WaspList[nWasp].nDamage = g_kexMode ? 20 : 10;

    if (bEggWasp && !g_kexMode)
    {
        WaspList[nWasp].field_C = 60;
        WaspList[nWasp].nDamage /= 2;
    }
    else
    {
        WaspList[nWasp].field_C = RandomSize(5);
    }

    WaspList[nWasp].nAttackTime = 0;
    WaspList[nWasp].field_14 = g_kexMode && nKind == kKexFlyerWasp
                              && KexWaspSequenceExact() ? kKexWaspBobState : 0;
    WaspList[nWasp].nVelocity = 0;
    WaspList[nWasp].dTime = g_kexMode
        && ((nKind == kKexFlyerWasp && KexWaspSequenceExact())
            || (nKind == kKexFlyerBird && KexBirdSequenceExact()))
        ? 0 : RandomSize(7) + 127;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nWasp, kRunWasp);

    WaspList[nWasp].nRun = runlist_AddRunRec(NewRun, nWasp, kRunWasp);

    nCreaturesLeft++;
    KexFlyerAudit(nWasp);
    // BuildQueenEgg needs the controller index for GetWaspSprite(); all other
    // callers ignore the return value. Returning the sprite here was a latent
    // out-of-range assertion as soon as a Queen egg actually hatched.
    return nWasp;
}

void KexFlyer_TestTick()
{
    if (s_waspBobTest == -1)
        s_waspBobTest = g_kexMode && Bgetenv("VB_KEX_WASP_BOB_TEST") ? 0 : -2;
    if (s_waspBobTest == 0)
    {
        short gameTicks = 0, velocity = 0;
        int const first = KexWaspBobTick(gameTicks, velocity, true);
        int const second = KexWaspBobTick(gameTicks, velocity, false);
        int const held0 = KexWaspBobTick(gameTicks, velocity, false);
        int const held1 = KexWaspBobTick(gameTicks, velocity, false);
        short wrapTicks = kKexWaspTicksPerCycle - 1, wrapVelocity = 0;
        KexWaspBobTick(wrapTicks, wrapVelocity, false);

        int64_t const z128 = 128 * kKexZPerUnit;
        int64_t const reach = 192 * kKexZPerUnit;
        bool const okay = KexWaspBobRawVelocity(0) == -2 * kKexZPerUnit
            && first == -180 && second == -169
            && held0 == -158 && held1 == -148 && gameTicks == 4
            && wrapTicks == 0
            && KexWaspBobWrites(0, 0, 0, 0, false)
            && KexWaspBobWrites(z128, reach / 16 + 1, 0, 0, true)
            && !KexWaspBobWrites(z128 + 1, reach / 16 + 1, 0, 0, true)
            && !KexWaspBobWrites(0, reach / 16, 0, 0, true)
            && !KexWaspBobWrites(0, 0, 0, reach, true);
        OSD_Printf("Kex Wasp bob test: %s (30 Hz chase callback over two 60 Hz cos/friction steps, 3D target gate, Build-Z inversion)\n",
                   okay ? "PASS" : "FAIL");
        s_waspBobTest = -2;
    }

    if (s_birdFlightTest == -1)
        s_birdFlightTest = g_kexMode && Bgetenv("VB_KEX_BIRD_FLIGHT_TEST") ? 0 : -2;
    if (s_birdFlightTest == 0)
    {
        short calmRemainder = 0, retreatRemainder = 0, gameTicks = 2046;
        int calmTurn = 0, retreatTurn = 0;
        for (int i = 0; i < 15; i++)
            calmTurn += KexBirdTurnStep(calmRemainder, 384);
        for (int i = 0; i < 45; i++)
            retreatTurn += KexBirdTurnStep(retreatRemainder, 1024);
        int const firstTick = KexBirdAdvanceGameTicks(gameTicks);
        int const secondTick = KexBirdAdvanceGameTicks(gameTicks);
        int const ceiling = 10000;
        bool const okay = calmTurn == 128 && calmRemainder == 0
            && retreatTurn == 1024 && retreatRemainder == 0
            && firstTick == 2046 && secondTick == 0 && gameTicks == 2
            && KexBirdRetreatStops(ceiling + (128 + 1) * kKexZPerUnit + 288,
                                   -288, ceiling)
            && !KexBirdRetreatStops(ceiling + (128 + 1) * kKexZPerUnit + 289,
                                    -288, ceiling)
            && KexBirdRetreatStops(ceiling + 999999, -287, ceiling);
        OSD_Printf("Kex Bird flight test: %s (60 Hz velocity persistence, exact turn fractions, authored retreat ceiling gate)\n",
                   okay ? "PASS" : "FAIL");
        s_birdFlightTest = -2;
    }

    if (s_flyerTest == -1)
        s_flyerTest = g_kexMode && Bgetenv("VB_KEX_FLYER_TEST") ? 0 : -2;
    if (s_flyerTest < 0)
        return;

    if (s_flyerTest == 0)
    {
        short const nPlayer = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayer >= (unsigned)kMaxSprites
            || sprite[nPlayer].sectnum < 0 || !(sprite[nPlayer].cstat & 0x101))
            return;

        short const nSector = sprite[nPlayer].sectnum;
        int const nFloor = getflorzofslope(nSector, sprite[nPlayer].x, sprite[nPlayer].y);
        s_flyerTestWasp = (short)BuildWasp(-2, sprite[nPlayer].x,
                                                sprite[nPlayer].y, nFloor,
                                                nSector, sprite[nPlayer].ang);
        if (s_flyerTestWasp < 0)
        {
            OSD_Printf("Kex flyer test: FAIL (no free controller)\n");
            s_flyerTest = -2;
            return;
        }
        WaspList[s_flyerTestWasp].nTarget = nPlayer;
        s_flyerTestPlayer = nLocalPlayer;
        s_flyerTestInvincibility = PlayerList[nLocalPlayer].invincibility;
        if (PlayerList[nLocalPlayer].invincibility < 100)
            PlayerList[nLocalPlayer].invincibility = 100;
        OSD_Printf("Kex flyer test: seedling %d spawned on floor %d; attack sequence %d frames\n",
                   WaspList[s_flyerTestWasp].nSprite, nFloor,
                   SeqSize[SeqOffsets[kSeqSeedling] + ActionSeq[2].a]);
        s_flyerTest = 1;
        return;
    }

    if ((unsigned)s_flyerTestWasp >= (unsigned)nWaspCount)
    {
        if ((unsigned)s_flyerTestPlayer < (unsigned)nTotalPlayers)
            PlayerList[s_flyerTestPlayer].invincibility = s_flyerTestInvincibility;
        OSD_Printf("Kex flyer test: FAIL (controller disappeared)\n");
        s_flyerTest = -2;
        return;
    }

    short const nSprite = WaspList[s_flyerTestWasp].nSprite;
    if ((unsigned)nSprite >= (unsigned)kMaxSprites || sprite[nSprite].statnum != 107)
    {
        if ((unsigned)s_flyerTestPlayer < (unsigned)nTotalPlayers)
            PlayerList[s_flyerTestPlayer].invincibility = s_flyerTestInvincibility;
        OSD_Printf("Kex flyer test: FAIL (seedling disappeared)\n");
        s_flyerTest = -2;
        return;
    }

    if (s_flyerTest++ == 7)
    {
        int const nFloor = getflorzofslope(sprite[nSprite].sectnum,
                                           sprite[nSprite].x, sprite[nSprite].y);
        int const chaseFrames = SeqSize[SeqOffsets[kSeqSeedling] + ActionSeq[0].a];
        int const attackFrames = SeqSize[SeqOffsets[kSeqSeedling] + ActionSeq[2].a];
        int const birdCalmFrames = SeqSize[SeqOffsets[kSeqHawk] + BirdActionSeq[0].a];
        int const birdAttackFrames = SeqSize[SeqOffsets[kSeqHawk] + BirdActionSeq[2].a];
        int const birdRetreatFrames = SeqSize[SeqOffsets[kSeqHawk] + BirdActionSeq[3].a];
        int const birdChaseFrames = SeqSize[SeqOffsets[kSeqHawk] + BirdActionSeq[1].a];
        int const waspBaseFrames = SeqSize[SeqOffsets[kSeqWasp] + WaspActionSeq[0].a];
        int const waspAttackFrames = SeqSize[SeqOffsets[kSeqWasp] + WaspActionSeq[2].a];
        int const waspBackoffFrames = SeqSize[SeqOffsets[kSeqWasp] + WaspActionSeq[3].a];
        int const waspChaseFrames = SeqSize[SeqOffsets[kSeqWasp] + WaspActionSeq[1].a];
        bool const okay = s_waspKind[s_flyerTestWasp] == kKexFlyerSeedling
            && WaspList[s_flyerTestWasp].nHealth == KexHealthUnits(100)
            && WaspList[s_flyerTestWasp].nDamage == 20
            && sprite[nSprite].clipdist == 54
            && sprite[nSprite].xrepeat == 80 && sprite[nSprite].yrepeat == 80
            && sprite[nSprite].z == nFloor && sprite[nSprite].zvel == 0
            && chaseFrames == 1 && attackFrames == 6
            && birdCalmFrames == 1 && birdAttackFrames == 8
            && birdRetreatFrames == 12 && birdChaseFrames == 6
            && KexWaspSequenceExact() && waspBaseFrames == 4
            && waspAttackFrames == 8 && waspBackoffFrames == 1
            && waspChaseFrames == 4
            && WaspList[s_flyerTestWasp].nAction == 2
            && WaspList[s_flyerTestWasp].nFrame == 0;
        OSD_Printf("Kex flyer test: %s (ground %d/%d, hp %d, clip %d, repeat %d, seed %d/%d, bird %d/%d/%d/%d, wasp %d/%d/%d/%d, action/frame %d/%d)\n",
                   okay ? "PASS" : "FAIL", (int)sprite[nSprite].z, nFloor,
                   KexHealthPoints(WaspList[s_flyerTestWasp].nHealth), (int)sprite[nSprite].clipdist,
                   (int)sprite[nSprite].xrepeat, chaseFrames, attackFrames,
                   birdCalmFrames, birdAttackFrames, birdRetreatFrames, birdChaseFrames,
                   waspBaseFrames, waspAttackFrames, waspBackoffFrames, waspChaseFrames,
                   WaspList[s_flyerTestWasp].nAction, WaspList[s_flyerTestWasp].nFrame);
        KexFlyerDelete(s_flyerTestWasp, nSprite);
        nCreaturesLeft--;
        if ((unsigned)s_flyerTestPlayer < (unsigned)nTotalPlayers)
            PlayerList[s_flyerTestPlayer].invincibility = s_flyerTestInvincibility;
        s_flyerTest = -2;
    }
}

void FuncWasp(int a, int nDamage, int nRun)
{
    short nWasp = RunData[nRun].nVal;
    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerSeedling)
    {
        FuncKexSeedling(a, nDamage, nWasp);
        return;
    }
    short nSprite = WaspList[nWasp].nSprite;
    short nAction = WaspList[nWasp].nAction;

    short nTarget = -1;

    bool bVal = false;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x90000:
        {
            actionSeq const &action = KexFlyerActionSequence(nWasp, nAction);
            seq_PlotSequence(a & 0xFFFF, SeqOffsets[KexFlyerSequence(nWasp)]
                             + action.a, WaspList[nWasp].nFrame, action.b);
            return;
        }

        case 0xA0000:
        {
            if (!(sprite[nSprite].cstat & 0x101))
                return;

            nDamage = runlist_CheckRadialDamage(nSprite);
            // fall through to case 0x80000
            fallthrough__;
        }

        case 0x80000:
        {
            if (!nDamage) {
                return;
            }

            if (WaspList[nWasp].nHealth > 0)
            {
                WaspList[nWasp].nHealth -= nDamage;

                if (WaspList[nWasp].nHealth > 0)
                {
                    if (g_kexMode)
                    {
                        short const nSource = a & 0xFFFF;
                        if (nSource >= 0 && sprite[nSource].statnum < 199
                            && (s_waspKind[nWasp] != kKexFlyerBird
                                || sprite[nSource].statnum == 100))
                            WaspList[nWasp].nTarget = nSource;

                        // AI_Wasp and AI_Bird both have painChance 0. The
                        // random byte is still consumed, but taking damage
                        // cannot initiate backoff or bird retreat; those are
                        // authored attack-completion events.
                        KexDifficulty_PainRoll(0);
                    }
                    else
                    {
                        if (!RandomSize(4))
                        {
                            WaspList[nWasp].nAction = 3;
                            WaspList[nWasp].nFrame  = 0;
                        }

                        WaspList[nWasp].nAction = 1;
                        sprite[nSprite].ang += RandomSize(9) + 768;
                        sprite[nSprite].ang &= kAngleMask;

                        WaspList[nWasp].nVelocity = 3000;

                        sprite[nSprite].zvel = (-20) - RandomSize(6);
                    }
                }
                else
                {
                    // Wasp is dead
                    short const nKiller = nMessage == 0xA0000
                        ? (short)nRadialOwner : (short)(a & 0xFFFF);
                    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerBird)
                    {
                        KexBirdDeath(nWasp, nSprite, nKiller);
                        nCreaturesLeft--;
                        return;
                    }

                    if (g_kexMode)
                    {
                        KexWaspDeath(nWasp, nSprite, nKiller);
                        nCreaturesLeft--;
                        return;
                    }

                    nVelShift = 0;

                    WaspList[nWasp].nAction = 4;
                    WaspList[nWasp].nFrame  = 0;

                    sprite[nSprite].cstat = 0;
                    sprite[nSprite].ang = (sprite[nSprite].ang + 1024) & kAngleMask;

                    SetWaspVel(nSprite);

                    sprite[nSprite].zvel = 512;

                    nCreaturesLeft--;
                }
            }
            return;
        }

        case 0x20000:
        {
            short nSeq = SeqOffsets[KexFlyerSequence(nWasp)]
                       + KexFlyerActionSequence(nWasp, nAction).a;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, WaspList[nWasp].nFrame);

            seq_MoveSequence(nSprite, nSeq, WaspList[nWasp].nFrame);

            WaspList[nWasp].nFrame++;
            if (WaspList[nWasp].nFrame >= SeqSize[nSeq])
            {
                WaspList[nWasp].nFrame = 0;
                bVal = true;
            }

            bool const bExactKexWasp = g_kexMode
                                    && s_waspKind[nWasp] == kKexFlyerWasp
                                    && KexWaspSequenceExact();
            bool const bExactKexBird = g_kexMode
                                    && s_waspKind[nWasp] == kKexFlyerBird
                                    && KexBirdSequenceExact();
            int const kexBirdGameTicks = bExactKexBird
                ? KexBirdAdvanceGameTicks(WaspList[nWasp].nAttackTime) : 0;
            // wasp_attack frame seven calls A_WaspBackoff before movement.
            // Its authored Z write is zero, so discard the last chase bob
            // before advancing these two KEX actor ticks.
            if (bExactKexWasp && nAction == 2 && bVal)
                WaspList[nWasp].dTime = 0;
            int const kexWaspZMove = bExactKexWasp
                ? KexWaspBobAdvance(nWasp, nSprite, WaspList[nWasp].nTarget,
                                     nAction == 1)
                : 0;

            if (WaspList[nWasp].nHealth > 0)
            {
                nTarget = WaspList[nWasp].nTarget;

                if (nTarget > -1 && (!(sprite[nTarget].cstat & 0x101) || (SectFlag[sprite[nTarget].sectnum] & kSectUnderwater)))
                {
                    // goto pink
                    WaspList[nWasp].nTarget = -1;
                    WaspList[nWasp].nAction = 0;
                    WaspList[nWasp].field_C = RandomSize(6);
                    return;
                }
            }

            // Both authored aerial attacks apply their melee callback on
            // attack frame zero. nFrame has advanced by this point, so one
            // identifies that frame and keeps damage synchronized with the
            // generated bird/wasp attack sound instead of the prior chase hit.
            if (g_kexMode && nAction == 2 && WaspList[nWasp].nFrame == 1
                && KexAirFlyerMeleeRange(nWasp, nSprite, nTarget))
                runlist_DamageEnemy(nTarget, nSprite, WaspList[nWasp].nDamage);

            switch (nAction)
            {
                default:
                    return;

                case 0:
                {
                    if (!bExactKexWasp && !bExactKexBird)
                    {
                        // Original EXE behavior: all legacy Wasps use this
                        // randomized idle bob. The remaster's Wasp takes the
                        // KEX-only chase callback path below instead.
                        bool const bKexBob = !(g_kexMode
                                              && s_waspKind[nWasp] == kKexFlyerBird);
                        if (bKexBob)
                            sprite[nSprite].zvel = (Sin(WaspList[nWasp].nAttackTime)
                                                  * (2 * kKexZPerUnit)) >> 14;
                        else
                            sprite[nSprite].zvel = 0;

                        WaspList[nWasp].nAttackTime += WaspList[nWasp].dTime;
                        WaspList[nWasp].nAttackTime &= kAngleMask;
                    }

                    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerBird
                        && nTarget < 0)
                    {
                        if (bExactKexBird)
                        {
                            // bird_fly_calm's lone A_BirdWander callback
                            // writes velocity on its 30 Hz animation edge;
                            // kexSlaveAI then moves it on both 60 Hz source
                            // physics ticks before the next edge.
                            KexBirdWander(nWasp, nSprite, kexBirdGameTicks);
                        }
                        else
                        {
                        // A_BirdWander turns the calm crow by 1.5 degrees
                        // each tick and flies at 2 KEX units/tick. Wasp's
                        // idle path otherwise has no velocity until it sees
                        // a target, leaving converted crows frozen in place.
                        if (!WaspList[nWasp].field_14)
                            WaspList[nWasp].field_14 = RandomSize(11);

                        if (((totalmoves + WaspList[nWasp].field_14) & 512) != 0)
                            sprite[nSprite].ang = (sprite[nSprite].ang + 9) & kAngleMask;
                        else
                            sprite[nSprite].ang = (sprite[nSprite].ang - 9) & kAngleMask;

                        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * (2 * 6)) >> 8;
                        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * (2 * 6)) >> 8;
                        }
                    }

                    if (bExactKexWasp)
                        KexWaspMove(nSprite, kexWaspZMove);
                    else if (bExactKexBird && nTarget < 0)
                        KexBirdMovePair(nSprite);
                    else
                        MoveCreature(nSprite);

                    if (nTarget >= 0)
                    {
                        WaspList[nWasp].field_C--;
                        if (WaspList[nWasp].field_C > 0)
                        {
                            PlotCourseToSprite(nSprite, nTarget);
                        }
                        else
                        {
                            if (!bExactKexWasp)
                                sprite[nSprite].zvel = 0;
                            if (bExactKexBird)
                            {
                                // kexSlaveAI::StartChasing clears the calm
                                // callback velocity before its first chase
                                // think, rather than carrying the wander
                                // vector into pursuit.
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                            }
                            WaspList[nWasp].nAction = 1;
                            WaspList[nWasp].nFrame  = 0;
                            WaspList[nWasp].nVelocity = KexFlyerChaseVelocity(nWasp);
                            WaspList[nWasp].field_C = RandomSize(5) + 60;
                        }
                    }
                    else
                    {
                        if ((nWasp & 0x1F) == (totalmoves & 0x1F))
                        {
                            short const found = FindPlayer(nSprite, 60);
                            WaspList[nWasp].nTarget = found;
                            if (found >= 0 && g_kexMode
                                && s_waspKind[nWasp] == kKexFlyerBird)
                            {
                                int const sound = KexSound_Get("bird_act");
                                if (sound >= 0)
                                    D3PlayFX(sound, nSprite);
                            }
                        }
                    }

                    return;
                }

                case 1:
                {
                    WaspList[nWasp].field_C--;

                    if (g_kexMode && KexAirFlyerMeleeRange(nWasp, nSprite, nTarget))
                    {
                        PlotCourseToSprite(nSprite, nTarget);
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        WaspList[nWasp].nAction = 2;
                        WaspList[nWasp].nFrame = 0;
                        return;
                    }

                    if (WaspList[nWasp].field_C <= 0)
                    {
                        WaspList[nWasp].nAction = 0;
                        WaspList[nWasp].field_C = RandomSize(6);
                        return;
                    }

                    int const nChaseVal = bExactKexWasp
                        ? KexWaspAngleChase(nSprite, nTarget,
                                             WaspList[nWasp].nVelocity, 16,
                                             kexWaspZMove)
                        : AngleChase(nSprite, nTarget,
                                     WaspList[nWasp].nVelocity, 0, 16);

                    switch (nChaseVal & 0xC000)
                    {
                        default:
                            return;

                        case 0x8000:
                        {
                            return;
                        }

                        case 0xC000:
                        {
                            short nSprite2 = (nChaseVal & 0x3FFF);
                            if (nSprite2 == nTarget)
                            {
                                sprite[nSprite].xvel = 0;
                                sprite[nSprite].yvel = 0;
                                if (!g_kexMode)
                                    runlist_DamageEnemy(nSprite2, nSprite, WaspList[nWasp].nDamage);
                                WaspList[nWasp].nAction = 2;
                                WaspList[nWasp].nFrame = 0;
                            }
                            return;
                        }
                    }

                    return;
                }

                case 2:
                case 3:
                {
                    // kexSlaveAI remains moveable while its melee animation
                    // plays. Its only carried velocity here is the decaying
                    // vertical bob, sampled by KexWaspBobAdvance above.
                    if (bExactKexWasp && nAction == 2)
                        KexWaspMove(nSprite, kexWaspZMove);

                    if (g_kexMode && nAction == 2 && bVal)
                    {
                        WaspList[nWasp].nAction = 3;
                        WaspList[nWasp].nFrame = 0;
                        if (s_waspKind[nWasp] == kKexFlyerBird)
                        {
                            if (bExactKexBird)
                            {
                                // bird_attack's delay-8 goto enters
                                // bird_fly_retreat, whose frame zero writes
                                // +3 KEX Z before frame one immediately runs
                                // A_BirdRetreat.  That velocity then persists
                                // through both 60 Hz source physics steps.
                                sprite[nSprite].zvel = -(3 * kKexZPerUnit);
                                if (KexBirdRetreat(nWasp, nSprite))
                                    KexBirdMovePair(nSprite);
                                return;
                            }
                            // bird_fly_retreat starts with
                            // A_ThrustZVelocity(3). KEX Z points upward;
                            // Build Z points downward and is scaled by 96.
                            WaspList[nWasp].field_14 = 12;
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                            sprite[nSprite].zvel = -(3 * 16 * 6);
                        }
                        else
                        {
                            // wasp_attack frame 7 calls A_WaspBackoff at 20
                            // KEX units/tick before returning to chase.
                            WaspList[nWasp].field_14 = bExactKexWasp
                                                       ? kKexWaspBobState : 0;
                            sprite[nSprite].xvel = (Cos(sprite[nSprite].ang + 1024) * (20 * 6)) >> 8;
                            sprite[nSprite].yvel = (Sin(sprite[nSprite].ang + 1024) * (20 * 6)) >> 8;
                            sprite[nSprite].zvel = 0;
                        }
                        return;
                    }

                    if (g_kexMode && s_waspKind[nWasp] == kKexFlyerBird
                        && nAction == 3)
                    {
                        if (bExactKexBird)
                        {
                            if (KexBirdRetreat(nWasp, nSprite))
                                KexBirdMovePair(nSprite);
                            return;
                        }
                        sprite[nSprite].ang = (sprite[nSprite].ang + 4 * 2048 / 360) & kAngleMask;
                        sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * (4 * 6)) >> 8;
                        sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * (4 * 6)) >> 8;

                        int const nMove = MoveCreature(nSprite);
                        if ((nMove & 0x10000) || --WaspList[nWasp].field_14 <= 0)
                        {
                            WaspList[nWasp].field_14 = 0;
                            WaspList[nWasp].nAction = 0;
                            WaspList[nWasp].nFrame = 0;
                            WaspList[nWasp].nTarget = -1;
                            WaspList[nWasp].field_C = RandomSize(6);
                            sprite[nSprite].xvel = 0;
                            sprite[nSprite].yvel = 0;
                            sprite[nSprite].zvel = 0;
                        }
                        return;
                    }

                    if (g_kexMode && nAction == 3)
                    {
                        if (bExactKexWasp)
                            KexWaspMove(nSprite, kexWaspZMove);
                        else
                            MoveCreature(nSprite);

                        if (bVal)
                        {
                            WaspList[nWasp].nAction = 1;
                            WaspList[nWasp].nFrame = 0;
                            WaspList[nWasp].nVelocity = KexFlyerChaseVelocity(nWasp);
                            sprite[nSprite].zvel = 0;
                            if (bExactKexWasp)
                                WaspList[nWasp].dTime = 0;
                        }
                        return;
                    }

                    if (bVal)
                    {
                        sprite[nSprite].ang += RandomSize(9) + 768;
                        sprite[nSprite].ang &= kAngleMask;
                        sprite[nSprite].zvel = (-20) - RandomSize(6);

                        WaspList[nWasp].nAction = 1;
                        WaspList[nWasp].nVelocity = 3000;
                    }
                    return;
                }
                case 4:
                {
                    int nMove = MoveCreature(nSprite) & 0x8000;
                    nMove |= 0xC000;

                    if (nMove)
                    {
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].zvel = 1024;
                        WaspList[nWasp].nAction = 5;
                        WaspList[nWasp].nFrame = 0;
                    }

                    return;
                }
                case 5:
                {
                    short nSector = sprite[nSprite].sectnum;

                    sprite[nSprite].z += sprite[nSprite].zvel;

                    if (sprite[nSprite].z >= sector[nSector].floorz)
                    {
                        if (SectBelow[nSector] > -1)
                        {
                            BuildSplash(nSprite, nSector);
                            sprite[nSprite].cstat |= 0x8000;
                        }

                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                        sprite[nSprite].zvel = 0;
                        WaspList[nWasp].nAction = 6;
                        WaspList[nWasp].nFrame = 0;
                        runlist_SubRunRec(WaspList[nWasp].nRun);
                    }

                    return;
                }
            }

            break;
        }
    }
}

class WaspLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void WaspLoadSave::Load()
{
    Read(&nVelShift, sizeof(nVelShift));
    Read(&nWaspCount, sizeof(nWaspCount));
    Read(WaspList, sizeof(WaspList[0]) * nWaspCount);
    Bmemset(s_waspKind, 0, sizeof(s_waspKind));
    for (int i = 0; g_kexMode && i < nWaspCount; i++)
    {
        int const nSprite = WaspList[i].nSprite;
        if ((unsigned)nSprite >= (unsigned)kMaxSprites)
            continue;

        int kind = sprite[nSprite].hitag;
        if (kind != kKexFlyerBird && kind != kKexFlyerSeedling)
        {
            // Compatibility with saves written before the persistent marker:
            // birds are on the private 7600 tile run; a hatched Queen actor
            // used the old repeat-20 Wasp skin.
            if (sprite[nSprite].picnum >= 7600 && sprite[nSprite].picnum <= 7635)
                kind = kKexFlyerBird;
            else if ((sprite[nSprite].picnum >= 7850 && sprite[nSprite].picnum <= 7864)
                     || sprite[nSprite].xrepeat == 20)
                kind = kKexFlyerSeedling;
            else
                kind = kKexFlyerWasp;
            sprite[nSprite].hitag = (int16_t)kind;
        }
        s_waspKind[i] = (uint8_t)kind;
        if (kind == kKexFlyerBird)
            sprite[nSprite].shade = -128;
        if (WaspList[i].nAction == 1)
            WaspList[i].nVelocity = KexFlyerChaseVelocity(i);
        KexFlyerAudit(i);
    }
}

void WaspLoadSave::Save()
{
    Write(&nVelShift, sizeof(nVelShift));
    Write(&nWaspCount, sizeof(nWaspCount));
    Write(WaspList, sizeof(WaspList[0]) * nWaspCount);
}

static WaspLoadSave* myLoadSave;

void WaspLoadSaveConstruct()
{
    myLoadSave = new WaspLoadSave();
}
