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
#include "anims.h"
#include "bullet.h"
#include "sequence.h"
#include "runlist.h"
#include "exhumed.h"
#include "sound.h"
#include "lighting.h"
#include "random.h"
#include "init.h"
#include "player.h"
#include "save.h"
#include "kexbreak.h"
#include <assert.h>

#define kMaxAnims	400

short nMagicSeq = -1;
short nPreMagicSeq  = -1;
short nSavePointSeq = -1;
short nAnimsFree = 0;

short AnimRunRec[kMaxAnims];
short AnimsFree[kMaxAnims];
Anim AnimList[kMaxAnims];
uint8_t AnimFlags[kMaxAnims];

static int s_gibDeathTest = -1;
static int s_gibDeathTestExpected;
static int s_gibDeathTestCallbacks;
static int s_gibDeathTestPassed;
static uint8_t s_gibDeathTestSprite[kMaxSprites];
static int s_gibDeathTestZ[kMaxSprites];
static int s_birdFeatherTest = -1;

// Obj_Bird_Feathers is a no-gravity KEX actor. Its 0.5-unit downward velocity
// is written only when the looping animation enters frames 8 and 14. A KEX
// unit is six Build map units and Build Z has the opposite sign.
static constexpr int kKexBirdFeatherLifetime = 15;
static constexpr int kKexBirdFeatherFallVelocity = (16 * 6) / 2;
static constexpr int kKexBirdFeatherFloorMargin = (1024 * 16 * 6) / 1000;

void KexImpactLight_Tick()
{
    if (!g_kexMode)
        return;

    bool const haveFire = seq_IsLoaded(kSeqKexImpact);
    bool const haveRed = seq_IsLoaded(kSeqKexCobraRed);
    bool const haveGreen = seq_IsLoaded(kSeqKexCobraGreen);
    bool const haveAnubis = seq_IsLoaded(kSeqKexAnuImpact);
    bool const haveExplosion = seq_IsLoaded(kSeqKexExplosion);
    bool const haveWaterExplosion = seq_IsLoaded(kSeqKexWaterExplosion);
    bool const haveVasePuff = seq_IsLoaded(kSeqKexVasePuff);
    if (!haveFire && !haveRed && !haveGreen && !haveAnubis
            && !haveExplosion && !haveWaterExplosion && !haveVasePuff)
        return;

    // Fx_FireballExplode owns A_SpawnLight(512, .75, .375, .25, 15, 2).
    // Red/green cobra puffs use the same fade with radii 384 and 768.
    // KEX multiplies the radius by four in renderDynLight, and its dlight
    // loses .5 of fadeTime per 60-Hz tick. At Exhumed's 30 Hz that is one of
    // the fifteen authored fade steps per tick. Each exact puff SEQ has
    // fourteen visible ticks, ending just before the actor disappears.
    for (int i = 0; i < kMaxAnims; i++)
    {
        short const nSeq = AnimList[i].nSeq;
        int radius = 0;
        int fadeSteps = 15;
        if (haveFire && nSeq == SeqOffsets[kSeqKexImpact])
            radius = 512 * 4 * 6;
        else if (haveRed && nSeq == SeqOffsets[kSeqKexCobraRed])
            radius = 384 * 4 * 6;
        else if (haveGreen && nSeq == SeqOffsets[kSeqKexCobraGreen])
            radius = 768 * 4 * 6;
        else if (haveVasePuff && nSeq == SeqOffsets[kSeqKexVasePuff])
            radius = 384 * 4 * 6;
        else if ((haveExplosion && nSeq == SeqOffsets[kSeqKexExplosion])
                || (haveWaterExplosion
                    && nSeq == SeqOffsets[kSeqKexWaterExplosion]))
        {
            // Both Fx_Explosion01 variants own
            // A_SpawnLight(768, .75, .375, .25, 20, 10).
            radius = 768 * 4 * 6;
            fadeSteps = 20;
        }
        else if (!(haveAnubis && nSeq == SeqOffsets[kSeqKexAnuImpact]))
            continue;
        short const nSprite = AnimList[i].nSprite;
        if ((unsigned)nSprite >= (unsigned)kMaxSprites
                || sprite[nSprite].sectnum < 0)
            continue;
        int const nRun = sprite[nSprite].extra;
        if ((unsigned)nRun >= (unsigned)kMaxRuns
                || RunData[nRun].nRef != kRunAnim
                || RunData[nRun].nVal != i)
            continue;

        // Proj_Anubis keeps its radius-512 blue spawn light while its own
        // thirteen-frame death animation plays. Use the same cast as the
        // in-flight projectile so the light does not blink out on impact.
        if (!radius)
        {
            Kex_SpriteGlow(sprite[nSprite].sectnum, sprite[nSprite].x,
                           sprite[nSprite].y, 32, 6, 0);
            continue;
        }

        int const frame = clamp((int)AnimList[i].nFrame, 0, fadeSteps - 1);
        int const remaining = max(1, fadeSteps - 1 - frame);
        int const peak = (127 * remaining + fadeSteps / 2) / fadeSteps;
        Kex_TransientGlow(sprite[nSprite].sectnum, sprite[nSprite].x,
                          sprite[nSprite].y, peak, radius);
    }
}

int KexExplosion01_Spawn(int x, int y, int z, short nSector,
                         int nKexZOffset, int bWater)
{
    int const nSeq = bWater ? kSeqKexWaterExplosion : kSeqKexExplosion;
    if (!g_kexMode || !seq_IsLoaded(nSeq)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    // KEX positive Z is upward; Build Z grows downward and is 16*WORLD per
    // source unit. Fx_Explosion01 is scale 4, while the water actor is 2.
    int const effectZ = z - nKexZOffset * 16 * 6;
    // The generated KEX explosion art is already RGBA orange/yellow.  Flag 4
    // here selects PCExhumed's legacy palette 4, which remaps that truecolour
    // art into the black cloud seen on explosive barrels.  Fullbright remains
    // on the effect's sequence frames (their FrameFlag is 4), so initialize
    // only the sprite palette normally.
    int const nAnim = BuildAnim(-1, nSeq, 0, x, y, effectZ, nSector,
                                bWater ? 48 : 96, 0);
    if (nAnim < 0)
        return -2;

    if (Bgetenv("VB_KEXEXPLOSION_PALETTE_TEST"))
        OSD_Printf("Kex explosion palette test: %s (%s sprite palette %d)\n",
                   sprite[GetAnimSprite(nAnim)].pal == 0 ? "PASS" : "FAIL",
                   bWater ? "water" : "land",
                   (int)sprite[GetAnimSprite(nAnim)].pal);

    // These are frame-zero callbacks on the spawned effect, before the
    // caller's radial blast callback. Play them only if actor allocation
    // succeeded, exactly as KEX does.
    int const nSound = KexSound_Get(bWater ? "bomb_wat" : "bomb_exp");
    if (nSound >= 0)
        PlayFXAtXYZ(nSound, x, y, effectZ, nSector);
    return nAnim;
}

int KexVasePuff_Spawn(int x, int y, int z, short nSector,
                      int nKexZOffset)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexVasePuff)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    // Fx_VaseExplodePuff already carries its authored olive colour in the
    // generated RGBA art. BuildAnim's flag 4 installs the classic bright
    // palette; applying that second palette transform turns the remaster
    // effect into a near-black cloud. KEXVASEPUFF.SEQ still carries its
    // fullbright frame flag, so only the incompatible palette transform is
    // removed here.
    int const nAnim = BuildAnim(-1, kSeqKexVasePuff, 0, x, y,
        z - nKexZOffset * 16 * 6, nSector, 48, 0);
    return nAnim >= 0 ? nAnim : -2;
}

int KexWaterSplash_Spawn(int x, int y, int z, short nSector)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexWaterSplash)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    // Fx_WaterSplash is scale 1, while the other shared KEX effects use
    // repeats of 24 per source scale unit. Its source actor is placed on the
    // destination water sector's ceiling plane by kexActor::UpdateMovement.
    int const nAnim = BuildAnim(-1, kSeqKexWaterSplash, 0, x, y, z, nSector,
                                24, 4);
    if (nAnim < 0)
        return -2;

    // The crossing actor calls PlaySound("sounds/splash01.wav") before it
    // spawns Fx_WaterSplash. Keep the positional sound at that same water
    // surface, and do not fall back to PCExhumed's unrelated splash audio.
    int const nSound = KexSound_Get("spl_big");
    if (nSound >= 0)
        PlayFXAtXYZ(nSound, x, y, z, nSector);
    return nAnim;
}

void KexWaterSplash_OnTransition(short nOldSector, int oldX, int oldY,
                                 int oldZ, short nNewSector, int newX,
                                 int newY)
{
    if (!g_kexMode || (unsigned)nOldSector >= (unsigned)numsectors
            || (unsigned)nNewSector >= (unsigned)numsectors
            || (SectFlag[nOldSector] & kSectUnderwater)
            || !(SectFlag[nNewSector] & kSectUnderwater))
        return;

    // kexActor::UpdateMovement evaluates this before CModel::MoveActor: an
    // FF_WATER floor whose actor origin is already below its plane is a
    // silent portal transition. Converted water floors name their matching
    // underwater sector through SectBelow, and Build Z grows downward.
    if (SectBelow[nOldSector] == nNewSector
            && oldZ > getflorzofslope(nOldSector, oldX, oldY))
        return;

    KexWaterSplash_Spawn(newX, newY,
        getceilzofslope(nNewSector, newX, newY), nNewSector);
}

int KexGibDeath_Spawn(int x, int y, int z, short nSector,
                      int nKexZOffset, int nRepeat, int nKiller)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexGibDeath)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    // A_GibDeath creates Fx_Gibs01 at actor origin + offset, then copies the
    // dying actor's scale. KEX positive Z is upward; Build Z grows downward.
    int const effectZ = z - nKexZOffset * 16 * 6;
    int const nAnim = BuildAnim(-1, kSeqKexGibDeath, 0, x, y, effectZ,
                                nSector, clamp(nRepeat, 1, 255), 4);
    if (nAnim < 0)
        return -2;

    // Fx_Gibs01 frame 18 calls A_RandomItem on itself. Preserve the player
    // responsible for the death in an otherwise unused animation field.
    sprite[GetAnimSprite(nAnim)].xvel = (short)nKiller;
    return nAnim;
}

static int KexBirdFeatherCenteredOffset(int sourceUnits)
{
    // kexRand::CFloat() is [-1, 1), implemented as
    // (Max(20000)-10000)/10000. Keep the source's even full-range selection:
    // reducing a 15-bit value modulo 20000 favored the negative half.
    int const random = KexRandomMax(20000) - 10000;
    return (random * sourceUnits * 6) / 10000;
}

int KexBirdFeather_Spawn(int x, int y, int z, short nSector)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexBirdFeather)
            || (unsigned)nSector >= (unsigned)numsectors)
        return -1;

    // A_BirdDeath adds RandCFloat()*64/64/16 around the dead bird. KEX is
    // up-positive, so the signed Z offset is inverted for Build.
    x += KexBirdFeatherCenteredOffset(64);
    y += KexBirdFeatherCenteredOffset(64);
    z -= KexBirdFeatherCenteredOffset(16);

    int const nAnim = BuildAnim(-1, kSeqKexBirdFeather, 0, x, y, z,
                                nSector, 64, kAnimLoop);
    if (nAnim >= 0)
    {
        // Obj_Bird_Feathers has randomizeFrames. Spawn changes to frame zero
        // first, then chooses a visual frame without executing its actions.
        AnimList[nAnim].nFrame = KexRandomMax(kKexBirdFeatherLifetime);
    }
    return nAnim;
}

static void KexBirdFeatherMove(short nSprite)
{
    int const nSector = sprite[nSprite].sectnum;
    if ((unsigned)nSector >= (unsigned)numsectors)
        return;

    // This actor has no XY velocity, gravity, friction or collision response.
    // With no horizontal move it cannot change sector, so the source's floor
    // collision reduces exactly to this clamp while retaining Z velocity.
    sprite[nSprite].z += sprite[nSprite].zvel;
    if (sprite[nSprite].z >= sector[nSector].floorz)
        sprite[nSprite].z = sector[nSector].floorz;
}

static bool KexBirdFeatherDrop(short nSprite)
{
    int const nSector = sprite[nSprite].sectnum;
    if ((unsigned)nSector >= (unsigned)numsectors)
        return true;

    // A_FeatherDrop removes when source origin.z + velocity.z is at or below
    // floor + 1.024. After the KEX-up/Build-down conversion that is this
    // future-position comparison, before the second source physics step.
    return (int64_t)sprite[nSprite].z + sprite[nSprite].zvel
        >= (int64_t)sector[nSector].floorz - kKexBirdFeatherFloorMargin;
}

static bool KexBirdFeather_Tick(short nSprite, short nFrame)
{
    // Each Exhumed tick represents two KEX physics ticks at animSpeed .5.
    // The first uses the current frame; entering the next frame executes the
    // authored f8/f14 callbacks before the second movement.
    KexBirdFeatherMove(nSprite);

    int const nextFrame = (nFrame + 1) % kKexBirdFeatherLifetime;
    if (nextFrame == 8 || nextFrame == 14)
    {
        sprite[nSprite].zvel = kKexBirdFeatherFallVelocity;
        if (KexBirdFeatherDrop(nSprite))
            return true;
    }

    KexBirdFeatherMove(nSprite);
    return false;
}

void KexBirdFeather_TestTick()
{
    if (s_birdFeatherTest < 0)
        s_birdFeatherTest = g_kexMode && Bgetenv("VB_KEX_BIRD_FEATHER_TEST") ? 1 : 0;
    if (!s_birdFeatherTest)
        return;
    s_birdFeatherTest = 0;

    bool const sequence = seq_IsLoaded(kSeqKexBirdFeather)
        && SeqSize[SeqOffsets[kSeqKexBirdFeather]] == kKexBirdFeatherLifetime;
    int const floor = 20000;
    bool const floorGate = !((floor - 147) + kKexBirdFeatherFallVelocity
                             >= floor - kKexBirdFeatherFloorMargin)
        && ((floor - 144) + kKexBirdFeatherFallVelocity
            >= floor - kKexBirdFeatherFloorMargin);
    OSD_Printf("Kex Bird feather test: %s (15-frame randomized loop, f8/f14 "
               "fall, 1.024-unit floor gate)\n",
               sequence && floorGate ? "PASS" : "FAIL");
}

static void KexGibDeath_RandomItem(short nSprite)
{
    if ((unsigned)nSprite >= (unsigned)kMaxSprites)
        return;

    // A_RandomItem receives Fx_Gibs01, whose own authored height is 64. The
    // death callback offset was already applied when this actor was spawned.
    int const dropZ = sprite[nSprite].z - 64 * 16 * 6;
    KexDrop_RandomItemAt(sprite[nSprite].sectnum, sprite[nSprite].x,
                         sprite[nSprite].y, dropZ, sprite[nSprite].xvel);

    int const kind = s_gibDeathTestSprite[nSprite];
    if (kind)
    {
        bool const okay = dropZ == s_gibDeathTestZ[nSprite]
            && sprite[nSprite].xrepeat == (kind == 1 ? 40 : 80)
            && sprite[nSprite].yrepeat == sprite[nSprite].xrepeat
            && sprite[nSprite].pal == 4 && sprite[nSprite].shade == -64
            && !(sprite[nSprite].cstat & 2);
        s_gibDeathTestPassed += okay;
        s_gibDeathTestCallbacks++;
        s_gibDeathTestSprite[nSprite] = 0;
        if (s_gibDeathTestCallbacks == s_gibDeathTestExpected)
            OSD_Printf("Kex gib-death actor test: %s (%d/%d exact blank+13-art+5-blank callbacks at effect +64)\n",
                       s_gibDeathTestPassed == s_gibDeathTestExpected ? "PASS" : "FAIL",
                       s_gibDeathTestPassed, s_gibDeathTestExpected);
    }
}

void KexGibDeath_TestTick()
{
    if (s_gibDeathTest == -1)
        s_gibDeathTest = g_kexMode && Bgetenv("VB_KEX_GIB_DEATH_TEST") ? 0 : -2;
    if (s_gibDeathTest != 0)
        return;

    int const nPlayer = PlayerList[nLocalPlayer].nSprite;
    if ((unsigned)nPlayer >= (unsigned)kMaxSprites
            || sprite[nPlayer].sectnum < 0)
        return;
    s_gibDeathTest = 1;
    s_gibDeathTestExpected = 2;
    s_gibDeathTestCallbacks = s_gibDeathTestPassed = 0;

    int const offsets[2] = { 0, 64 };
    int const repeats[2] = { 40, 80 };
    for (int i = 0; i < 2; i++)
    {
        int const nAnim = KexGibDeath_Spawn(
            sprite[nPlayer].x + (i ? 96 : -96), sprite[nPlayer].y,
            sprite[nPlayer].z, sprite[nPlayer].sectnum,
            offsets[i], repeats[i], nPlayer);
        if (nAnim < 0)
        {
            OSD_Printf("Kex gib-death actor test: FAIL (exact effect allocation unavailable)\n");
            s_gibDeathTest = -2;
            return;
        }
        int const nSprite = GetAnimSprite(nAnim);
        s_gibDeathTestSprite[nSprite] = (uint8_t)(i + 1);
        s_gibDeathTestZ[nSprite] = sprite[nSprite].z - 64 * 16 * 6;
    }
}


void InitAnims()
{
    for (int i = 0; i < kMaxAnims; i++) {
        AnimsFree[i] = i;
    }

    nAnimsFree = kMaxAnims;

    nMagicSeq     = SeqOffsets[kSeqItems] + 21;
    nPreMagicSeq  = SeqOffsets[kSeqMagic2];
    nSavePointSeq = SeqOffsets[kSeqItems] + 12;
    s_gibDeathTest = -1;
    s_gibDeathTestExpected = s_gibDeathTestCallbacks = s_gibDeathTestPassed = 0;
    Bmemset(s_gibDeathTestSprite, 0, sizeof(s_gibDeathTestSprite));
}

/*
    Use when deleting an ignited sprite to check if any anims reference it.
    Will remove the Anim's loop flag and set the source (the ignited sprite's) sprite reference to -1.
    FuncAnim() will then delete the anim on next call for this anim.

    Without this, the anim will hold reference to a sprite which will eventually be reused, but the anim code
    will continue to manipulate its hitag value. This can break runlist records for things like LavaDude
    limbs that store these in the sprite hitag.

    Specifically needed for IgniteSprite() anims which can become orphaned from the source sprite (e.g a bullet)
    when the bullet sprite is deleted.
*/
void UnlinkIgnitedAnim(int nSprite)
{
    // scan the active anims (that aren't in the 'free' section of AnimsFree[])
    for (int i = kMaxAnims - 1; i >= nAnimsFree; i--)
    {
        int nAnim = AnimsFree[i];

        int nAnimSprite = GetAnimSprite(nAnim);
        if (nAnimSprite < 0)
            continue;

        if (sprite[nAnimSprite].statnum == kStatIgnited)
        {
            // .hitag holds the sprite number of the source 'sprite that's on fire' sprite
            if (nSprite == sprite[nAnimSprite].hitag)
            {
                AnimFlags[nAnim] &= ~kAnimLoop; // clear the animation loop flag
                sprite[nAnimSprite].hitag = -1; // set the sprite reference to -1
            }
        }
    }
}

void DestroyAnim(int nAnim)
{
    int nSprite = AnimList[nAnim].nSprite;

    if (nSprite >= 0)
    {
        StopSpriteSound(nSprite);
        runlist_SubRunRec(AnimRunRec[nAnim]);
        runlist_DoSubRunRec(sprite[nSprite].extra);
        runlist_FreeRun(sprite[nSprite].lotag - 1);
    }

    AnimsFree[nAnimsFree] = nAnim;
    nAnimsFree++;
}

int BuildAnim(int nSprite, int val, int val2, int x, int y, int z, int nSector, int nRepeat, int nFlag)
{
	if (!nAnimsFree) {
		return -1;
	}

    nAnimsFree--;

    short nAnim = AnimsFree[nAnimsFree];

    if (nSprite == -1) {
        nSprite = insertsprite(nSector, 500);
    }

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0;

    if (nFlag & 4)
    {
        sprite[nSprite].pal = 4;
        sprite[nSprite].shade = -64;
    }
    else
    {
        sprite[nSprite].pal = 0;
        sprite[nSprite].shade = -12;
    }

    sprite[nSprite].clipdist = 10;
    sprite[nSprite].xrepeat = nRepeat;
    sprite[nSprite].yrepeat = nRepeat;
    //sprite[nSprite].picnum = 1;
    sprite[nSprite].ang = 0;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;

    // CHECKME - where is hitag set otherwise?
    if (sprite[nSprite].statnum < 900) {
        sprite[nSprite].hitag = -1;
    }

    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].owner = -1;
    sprite[nSprite].extra = runlist_AddRunRec(sprite[nSprite].lotag - 1, nAnim, kRunAnim);

    AnimRunRec[nAnim] = runlist_AddRunRec(NewRun, nAnim, kRunAnim);

    AnimFlags[nAnim] = nFlag;
    AnimList[nAnim].nSprite = nSprite;
    AnimList[nAnim].nFrame = 0;
    AnimList[nAnim].nSeq = SeqOffsets[val] + val2;

    if (nFlag & 0x80) {
        sprite[nSprite].cstat |= 0x2; // set transluscence
    }

    return nAnim;
}

short GetAnimSprite(short nAnim)
{
    return AnimList[nAnim].nSprite;
}

void FuncAnim(int a, int UNUSED(b), int nRun)
{
    short nAnim = RunData[nRun].nVal;
    assert(nAnim >= 0 && nAnim < kMaxAnims);

    short nSprite = AnimList[nAnim].nSprite;
    short nSeq = AnimList[nAnim].nSeq;

    assert(nSprite != -1);

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x20000:
        {
            short nFrame = AnimList[nAnim].nFrame;

            if (!(sprite[nSprite].cstat & 0x8000))
            {
                seq_MoveSequence(nSprite, nSeq, nFrame);
            }

            // cobra_{red,green}_death calls its puff callback immediately,
            // then again after 3, 6 and 9 Exhumed ticks. The hidden generated
            // controller starts at t=0, so pre-increment frames 2/5/8 are the
            // three delayed callback points.
            if (g_kexMode && seq_IsLoaded(kSeqKexCobraCtl)
                    && nSeq == SeqOffsets[kSeqKexCobraCtl])
                KexProjectile_CobraBurstFrame(nSprite, nFrame);

            // effects/orb_spawn holds a blank frame for 20 ticks, draws 23
            // visible ticks, and runs A_SpawnOrbPickup on the final visible
            // frame before its destroy frame. The generated sequences omit
            // only that already-invisible destroy frame.
            if (g_kexMode && nFrame == 42
                    && ((seq_IsLoaded(kSeqKexAmmoSpawn)
                            && nSeq == SeqOffsets[kSeqKexAmmoSpawn])
                        || (seq_IsLoaded(kSeqKexHealthSpawn)
                            && nSeq == SeqOffsets[kSeqKexHealthSpawn])))
                KexDrop_OrbSpawnPickup(nSprite);

            // Fx_Gibs01's final blank frame owns the delayed A_RandomItem
            // callback. Its already-invisible frame is still part of the
            // exact 19-tick actor and is destroyed immediately afterwards.
            if (g_kexMode && nFrame == 18
                    && seq_IsLoaded(kSeqKexGibDeath)
                    && nSeq == SeqOffsets[kSeqKexGibDeath])
                KexGibDeath_RandomItem(nSprite);

            if (g_kexMode && seq_IsLoaded(kSeqKexBirdFeather)
                    && nSeq == SeqOffsets[kSeqKexBirdFeather]
                    && KexBirdFeather_Tick(nSprite, nFrame))
            {
                DestroyAnim(nAnim);
                mydeletesprite(nSprite);
                return;
            }

            if (sprite[nSprite].statnum == kStatIgnited)
            {
                int nIgnitedSprite = sprite[nSprite].hitag;
                if (nIgnitedSprite > -1)
                {
                    sprite[nSprite].x = sprite[nIgnitedSprite].x;
                    sprite[nSprite].y = sprite[nIgnitedSprite].y;
                    sprite[nSprite].z = sprite[nIgnitedSprite].z;

                    if (sprite[nIgnitedSprite].sectnum != sprite[nSprite].sectnum)
                    {
                        if (sprite[nIgnitedSprite].sectnum < 0 || sprite[nIgnitedSprite].sectnum >= kMaxSectors)
                        {
                            DestroyAnim(nAnim);
                            mydeletesprite(nSprite);
                            return;
                        }
                        else
                        {
                            mychangespritesect(nSprite, sprite[nIgnitedSprite].sectnum);
                        }
                    }

                    if (!nFrame)
                    {
                        if (sprite[nIgnitedSprite].cstat != 0x8000)
                        {
                            int hitag2 = sprite[nIgnitedSprite].hitag;
                            sprite[nIgnitedSprite].hitag--;

                            if (hitag2 >= 15)
                            {
                                int const previousDamageType = nKexDamageType;
                                if (g_kexMode)
                                    nKexDamageType = kKexDamageFire;
                                runlist_DamageEnemy(nIgnitedSprite, -1, (sprite[nIgnitedSprite].hitag - 14) * 2);
                                nKexDamageType = previousDamageType;

                                if (sprite[nIgnitedSprite].shade < 100)
                                {
                                    sprite[nIgnitedSprite].pal = 0;
                                    sprite[nIgnitedSprite].shade++;
                                }

                                if (!(sprite[nIgnitedSprite].cstat & 0x101))
                                {
                                    DestroyAnim(nAnim);
                                    mydeletesprite(nSprite);
                                    return;
                                }
                            }
                            else
                            {
                                sprite[nIgnitedSprite].hitag = 1;
                                DestroyAnim(nAnim);
                                mydeletesprite(nSprite);
                            }
                        }
                        else
                        {
                            sprite[nIgnitedSprite].hitag = 1;
                            DestroyAnim(nAnim);
                            mydeletesprite(nSprite);
                        }
                    }
                }
            }

            AnimList[nAnim].nFrame++;
            if (AnimList[nAnim].nFrame >= SeqSize[nSeq])
            {
                if (AnimFlags[nAnim] & 0x10)
                {
                    AnimList[nAnim].nFrame = 0;
                }
                else if (nSeq == nPreMagicSeq)
                {
                    AnimList[nAnim].nFrame = 0;
                    AnimList[nAnim].nSeq = nMagicSeq;
                    short nAnimSprite = AnimList[nAnim].nSprite;
                    AnimFlags[nAnim] |= 0x10;
                    sprite[nAnimSprite].cstat |= 2;
                }
                else if (nSeq == nSavePointSeq)
                {
                    AnimList[nAnim].nFrame = 0;
                    AnimList[nAnim].nSeq++;
                    AnimFlags[nAnim] |= 0x10;
                }
                else
                {
                    DestroyAnim(nAnim);
                    mydeletesprite(nSprite);
                }
                return;
            }

            return;
        }

        case 0x90000:
        {
            seq_PlotSequence(a & 0xFFFF, nSeq, AnimList[nAnim].nFrame, 0x101);
            tsprite[a & 0xFFFF].owner = -1;
            return;
        }

        case 0xA0000:
        {
            return;
        }

        default:
        {
            DebugOut("unknown msg %x for anim\n", nMessage);
            return;
        }
    }
}

void BuildExplosion(short nSprite)
{
    short nSector = sprite[nSprite].sectnum;

    int edx = 36;

    if (SectFlag[nSector] & kSectUnderwater)
    {
        edx = 75;
    }
    else if (sprite[nSprite].z == sector[nSector].floorz)
    {
        edx = 34;
    }

    BuildAnim(-1, edx, 0, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum, sprite[nSprite].xrepeat, 4);
}

int BuildSplash(int nSprite, int nSector)
{
    int nRepeat, nSound;

    if (sprite[nSprite].statnum != 200)
    {
        nRepeat = sprite[nSprite].xrepeat + (RandomWord() % sprite[nSprite].xrepeat);
        nSound = kSoundSplashBig;
    }
    else
    {
        nRepeat = 20;
        nSound = kSoundSplashSmall;
    }

    int bIsLava = SectFlag[nSector] & kSectLava;

    int edx, nFlag;

    if (bIsLava)
    {
        edx = 43;
        nFlag = 4;
    }
    else
    {
        edx = 35;
        nFlag = 0;
    }

    int nAnim = BuildAnim(-1, edx, 0, sprite[nSprite].x, sprite[nSprite].y, sector[nSector].floorz, nSector, nRepeat, nFlag);

    if (!bIsLava)
    {
        D3PlayFX(StaticSound[nSound] | 0xa00, AnimList[nAnim].nSprite);
    }

    return AnimList[nAnim].nSprite;
}

class AnimsLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void AnimsLoadSave::Load()
{
    Read(&nMagicSeq, sizeof(nMagicSeq));
    Read(&nPreMagicSeq, sizeof(nPreMagicSeq));
    Read(&nSavePointSeq, sizeof(nSavePointSeq));
    Read(&nAnimsFree, sizeof(nAnimsFree));

    Read(AnimRunRec, sizeof(AnimRunRec));
    Read(AnimsFree, sizeof(AnimsFree));
    Read(AnimList, sizeof(AnimList));
    Read(AnimFlags, sizeof(AnimFlags));
}

void AnimsLoadSave::Save()
{
    Write(&nMagicSeq, sizeof(nMagicSeq));
    Write(&nPreMagicSeq, sizeof(nPreMagicSeq));
    Write(&nSavePointSeq, sizeof(nSavePointSeq));
    Write(&nAnimsFree, sizeof(nAnimsFree));

    Write(AnimRunRec, sizeof(AnimRunRec));
    Write(AnimsFree, sizeof(AnimsFree));
    Write(AnimList, sizeof(AnimList));
    Write(AnimFlags, sizeof(AnimFlags));
}

static AnimsLoadSave* myLoadSave;

void AnimsLoadSaveConstruct()
{
    myLoadSave = new AnimsLoadSave();
}
