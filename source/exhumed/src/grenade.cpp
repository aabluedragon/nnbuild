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

#include "grenade.h"
#include "engine.h"
#include "player.h"
#include "runlist.h"
#include "exhumed.h"
#include "sound.h"
#include "move.h"
#include "init.h"
#include "bullet.h"
#include "gun.h"
#include "anims.h"
#include "lighting.h"
#include "sequence.h"
#include "random.h"
#include "save.h"
#include "kexbreak.h"
#include <assert.h>

struct Grenade
{
    int16_t field_0;
    int16_t field_2;
    int16_t nSprite;
    int16_t field_6;
    int16_t field_8;
    int16_t field_A;
    int16_t field_C;
    int16_t field_E;
    int field_10;
    int x;
    int y;
};

int nGrenadeCount = 0;
int nGrenadesFree;

int16_t GrenadeFree[kMaxGrenades];
Grenade GrenadeList[kMaxGrenades];

enum
{
    kKexBombMinSpeed = 16,
    kKexBombMaxSpeed = 48,
    kKexBombMaxCharge = kKexBombMaxSpeed - kKexBombMinSpeed,
    kKexBombRadius = 9,
    kKexBombHeight = 16,
    kKexBombGravity = 72,       // KEX default 0.75 in Build's 96x Z metric
    kKexBombBlastRadius = 512,
    kKexBombBlastDamage = 140,
    kKexBombBuildBlastRadius = kKexBombBlastRadius * 6 / 256,
    kKexPlayerRadius = 90,
    kKexBombSpawnHeight = 80,
};

static int s_kexGrenadeTest = -1;

static void KexGrenadePitchDirection(int horizonDelta, int *cosPitch,
                                     int *sinPitch)
{
    // Build's horizon projection is tan(pitch)=(horizon-92)/128. Preserve
    // that exact view direction in Q14 rather than treating horizon as a
    // linear angle. Positive sinPitch is upward in KEX coordinates.
    int const length = max(1, ksqrt(128 * 128 + horizonDelta * horizonDelta));
    *cosPitch = (128 * 16384) / length;
    *sinPitch = (horizonDelta * 16384) / length;
}

static int KexGrenadeSequence()
{
    return g_kexMode && seq_IsLoaded(kSeqAmunBomb)
        ? SeqOffsets[kSeqAmunBomb] : -1;
}


void InitGrenades()
{
    nGrenadeCount = 0;
    s_kexGrenadeTest = -1;

    for (int i = 0; i < kMaxGrenades; i++) {
        GrenadeFree[i] = i;
    }

    nGrenadesFree = kMaxGrenades;
}

int GrabGrenade()
{
    return GrenadeFree[--nGrenadesFree];
}

void DestroyGrenade(int nGrenade)
{
    runlist_DoSubRunRec(GrenadeList[nGrenade].field_6);
    runlist_SubRunRec(GrenadeList[nGrenade].field_8);
    runlist_DoSubRunRec(sprite[GrenadeList[nGrenade].nSprite].lotag - 1);

    mydeletesprite(GrenadeList[nGrenade].nSprite);
    GrenadeFree[nGrenadesFree] = nGrenade;

    nGrenadesFree++;
}

void BounceGrenade(int nGrenade, int nAngle)
{
    GrenadeList[nGrenade].field_10 >>= 1;

    GrenadeList[nGrenade].x = (Cos(nAngle) >> 5) * GrenadeList[nGrenade].field_10;
    GrenadeList[nGrenade].y = (Sin(nAngle) >> 5) * GrenadeList[nGrenade].field_10;

    D3PlayFX(StaticSound[kSoundGrenadeDrop], GrenadeList[nGrenade].nSprite);
}

void KexGrenade_Charge(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return;
    int const nGrenade = nPlayerGrenade[nPlayer];
    if ((unsigned)nGrenade >= (unsigned)kMaxGrenades
        || GrenadeList[nGrenade].field_10 >= 0)
        return;
    // Retail bomb_charge executes A_ChargeBombThrow once per held frame:
    // +1 from the global initial 16, clamped at 48.
    if (GrenadeList[nGrenade].field_E < kKexBombMaxCharge)
        GrenadeList[nGrenade].field_E++;
}

int ThrowGrenade(int nPlayer, int UNUSED(edx), int UNUSED(ebx), int ecx, int push1)
{
    if (nPlayerGrenade[nPlayer] < 0)
        return -1;

    int nGrenade = nPlayerGrenade[nPlayer];

    int nGrenadeSprite = GrenadeList[nGrenade].nSprite;
    int nPlayerSprite = PlayerList[nPlayer].nSprite;

    int nAngle = sprite[nPlayerSprite].ang;

    mychangespritesect(nGrenadeSprite, nPlayerViewSect[nPlayer]);

    sprite[nGrenadeSprite].x = sprite[nPlayerSprite].x;
    sprite[nGrenadeSprite].y = sprite[nPlayerSprite].y;
    sprite[nGrenadeSprite].z = sprite[nPlayerSprite].z;

    if (nAngle < 0) {
        nAngle = sprite[nPlayerSprite].ang;
    }

    sprite[nGrenadeSprite].cstat &= 0x7FFF;
    sprite[nGrenadeSprite].ang = nAngle;

    if (g_kexMode)
    {
        // Shipping p_pspr.txt: velocity is dir*charge, except Z is
        // 8+(charge+8)*dir.z. KEX Z is up; Build Z is down and scaled x96.
        int const charge = clamp(kKexBombMinSpeed + GrenadeList[nGrenade].field_E,
                                 kKexBombMinSpeed, kKexBombMaxSpeed);
        GrenadeList[nGrenade].field_10 = charge;
        if (PlayerList[nPlayer].nAmmo[kWeaponGrenade] > 0)
            AddAmmo(nPlayer, kWeaponGrenade, -1);

        if (push1 >= -3000)
        {
            int cosPitch, sinPitch;
            KexGrenadePitchDirection(push1, &cosPitch, &sinPitch);
            int const dirX = mulscale14(Cos(nAngle), cosPitch);
            int const dirY = mulscale14(Sin(nAngle), cosPitch);

            // FireActor traces the authored (radius, z=80) spawn offset from
            // the player before the projectile starts moving.
            movesprite(nGrenadeSprite,
                       dirX * (kKexPlayerRadius * 6),
                       dirY * (kKexPlayerRadius * 6),
                       -(kKexBombSpawnHeight * 96
                         + mulscale14(sinPitch, kKexPlayerRadius * 96)),
                       0, 0, CLIPMASK1);

            GrenadeList[nGrenade].x = dirX * (charge * 6);
            GrenadeList[nGrenade].y = dirY * (charge * 6);
            sprite[nGrenadeSprite].zvel =
                -(kKexBombMinSpeed / 2) * 96
                - mulscale14(sinPitch, (charge + 8) * 96);
        }
        else
        {
            // Native death handling drops a held bomb. It is now a live KEX
            // projectile, but does not inherit horizontal throw thrust.
            GrenadeList[nGrenade].x = 0;
            GrenadeList[nGrenade].y = 0;
            sprite[nGrenadeSprite].zvel = sprite[nPlayerSprite].zvel;
        }

        nPlayerGrenade[nPlayer] = -1;
        return nGrenadeSprite;
    }

    if (push1 >= -3000)
    {
        int nVel = totalvel[nPlayer] << 5;

        GrenadeList[nGrenade].field_10 = ((90 - GrenadeList[nGrenade].field_E) * (90 - GrenadeList[nGrenade].field_E)) + nVel;
        sprite[nGrenadeSprite].zvel = (-64 * push1) - 4352;

        int nMov = movesprite(nGrenadeSprite, Cos(nAngle) * (sprite[nPlayerSprite].clipdist << 3), Sin(nAngle) * (sprite[nPlayerSprite].clipdist << 3), ecx, 0, 0, CLIPMASK1);
        if (nMov & 0x8000)
        {
            nAngle = GetWallNormal(nMov & 0x3FFF);
            BounceGrenade(nGrenade, nAngle);
        }
    }
    else
    {
        GrenadeList[nGrenade].field_10 = 0;
        sprite[nGrenadeSprite].zvel = sprite[nPlayerSprite].zvel;
    }

    GrenadeList[nGrenade].x = Cos(nAngle) >> 4;
    GrenadeList[nGrenade].x *= GrenadeList[nGrenade].field_10;

    GrenadeList[nGrenade].y = Sin(nAngle) >> 4;
    GrenadeList[nGrenade].y *= GrenadeList[nGrenade].field_10;

    nPlayerGrenade[nPlayer] = -1;

    return nGrenadeSprite;
}

int BuildGrenade(int nPlayer)
{
    if (nGrenadesFree == 0)
        return -1;

    int nGrenade = GrabGrenade();

    int nSprite = insertsprite(nPlayerViewSect[nPlayer], 201);
    assert(nSprite >= 0 && nSprite < kMaxSprites);

    int nPlayerSprite = PlayerList[nPlayer].nSprite;

    sprite[nSprite].x = sprite[nPlayerSprite].x;
    sprite[nSprite].y = sprite[nPlayerSprite].y;
    sprite[nSprite].z = sprite[nPlayerSprite].z - 3840;
    sprite[nSprite].shade = -64;
    sprite[nSprite].xrepeat = 20;
    sprite[nSprite].yrepeat = 20;
    sprite[nSprite].cstat = 0x8000;
    sprite[nSprite].picnum = g_kexMode ? kKexAmunBombTile : 1;
    sprite[nSprite].pal = 0;
    sprite[nSprite].clipdist = g_kexMode
        ? (kKexBombRadius * 6 + 2) / 4 : 30;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = sprite[nPlayerSprite].ang;
    sprite[nSprite].owner = nPlayerSprite;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].hitag = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;

//	GrabTimeSlot(3);

    GrenadeList[nGrenade].field_E = g_kexMode ? 0 : 90;
    GrenadeList[nGrenade].field_2 = 0;
    GrenadeList[nGrenade].field_0 = 16;
    GrenadeList[nGrenade].field_10 = -1;
    GrenadeList[nGrenade].nSprite = nSprite;
    GrenadeList[nGrenade].field_A = 0;
    GrenadeList[nGrenade].field_C = 0;
    GrenadeList[nGrenade].field_6 = runlist_AddRunRec(sprite[nSprite].lotag - 1, nGrenade, kRunGrenade);
    GrenadeList[nGrenade].field_8 = runlist_AddRunRec(NewRun, nGrenade, kRunGrenade);

    nGrenadePlayer[nGrenade] = nPlayer;
    nPlayerGrenade[nPlayer] = nGrenade;

    return nGrenade;
}

void ExplodeGrenade(int nGrenade)
{
    int nSeq, nRepeat;

    int nPlayer = nGrenadePlayer[nGrenade];
    int nGrenadeSprite = GrenadeList[nGrenade].nSprite;
    int nGrenadeSect = sprite[nGrenadeSprite].sectnum;

    GrenadeList[nGrenade].field_C = 1;

    if (SectFlag[nGrenadeSect] & kSectUnderwater)
    {
        nSeq = kSeqGrenBubb;
        nRepeat = 60;
    }
    else
    {
        if (sprite[nGrenadeSprite].z < sector[nGrenadeSect].floorz)
        {
            nSeq = kSeqGrenPow;
            nRepeat = 200;
        }
        else
        {
            nSeq = kSeqGrenBoom;
            nRepeat = 150;
        }
    }

    if (!g_kexMode && GrenadeList[nGrenade].field_10 < 0)
    {
        int nPlayerSprite = PlayerList[nPlayer].nSprite;
        int nAngle = sprite[nPlayerSprite].ang;

        sprite[nGrenadeSprite].z = sprite[nPlayerSprite].z;
        sprite[nGrenadeSprite].x = (Cos(nAngle) >> 5) + sprite[nPlayerSprite].x;
        sprite[nGrenadeSprite].y = (Sin(nAngle) >> 5) + sprite[nPlayerSprite].y;

        changespritesect(nGrenadeSprite, sprite[nPlayerSprite].sectnum);

        if (!PlayerList[nPlayer].invincibility) {
            PlayerList[nPlayer].nHealth = 1;
        }
    }

    int nDamage = g_kexMode ? kKexBombBlastDamage
                            : BulletInfo[kWeaponGrenade].nDamage;

    if (nPlayerDouble[nPlayer] > 0) {
        nDamage *= 2;
    }

    // Proj_AmunBomb's death frame first spawns the matching land/water
    // Fx_Explosion01 actor at +32 KEX Z, then runs its radial blast. Keep the
    // callback order because the spawned actor immediately owns the sound and
    // light. A converted directory predating the optional effect SEQs keeps
    // the classic animation below.
    bool const kexExactExplosion = g_kexMode
        && KexExplosion01_Spawn(sprite[nGrenadeSprite].x,
            sprite[nGrenadeSprite].y, sprite[nGrenadeSprite].z,
            sprite[nGrenadeSprite].sectnum, 32,
            !!(SectFlag[nGrenadeSect] & kSectUnderwater)) != -1;

    runlist_RadialDamageEnemy(nGrenadeSprite, nDamage,
        g_kexMode ? kKexBombBuildBlastRadius
                  : BulletInfo[kWeaponGrenade].nRadius);
    // The remaster's grenade calls A_RadialBlast(512, 140, true): only this
    // KEX path opens authored Toggle faces; classic bomb walls are untouched.
    KexToggle_Blast(sprite[nGrenadeSprite].x, sprite[nGrenadeSprite].y,
                    sprite[nGrenadeSprite].z, sprite[nGrenadeSprite].sectnum,
                    kKexBombBlastRadius);

    if (!kexExactExplosion)
    {
        BuildAnim(-1, nSeq, 0, sprite[nGrenadeSprite].x,
                  sprite[nGrenadeSprite].y, sprite[nGrenadeSprite].z,
                  sprite[nGrenadeSprite].sectnum, nRepeat, 4);
        AddFlash(sprite[nGrenadeSprite].sectnum, sprite[nGrenadeSprite].x,
                 sprite[nGrenadeSprite].y, sprite[nGrenadeSprite].z, 128);
    }

    nGrenadePlayer[nGrenade] = -1;
    DestroyGrenade(nGrenade);
}

void FuncGrenade(int a, int UNUSED(nDamage), int nRun)
{
    int nGrenade = RunData[nRun].nVal;
    assert(nGrenade >= 0 && nGrenade < kMaxGrenades);

    int nGrenadeSprite = GrenadeList[nGrenade].nSprite;
    int nSeq;
    int const nKexSeq = KexGrenadeSequence();

    if (nKexSeq >= 0 && !GrenadeList[nGrenade].field_C)
    {
        nSeq = nKexSeq;
    }
    else if (GrenadeList[nGrenade].field_C)
    {
        nSeq = SeqOffsets[kSeqGrenBoom];
    }
    else
    {
        nSeq = SeqOffsets[kSeqGrenRoll] + GrenadeList[nGrenade].field_A;
    }

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        case 0x90000:
        {
            seq_PlotSequence(a & 0xFFFF, nSeq, GrenadeList[nGrenade].field_2 >> 8, 1);
            break;
        }

        default:
        {
            DebugOut("unknown msg %d for grenade\n", nMessage);
            return;
        }

        case 0x20000:
        {
            seq_MoveSequence(nGrenadeSprite, nSeq, GrenadeList[nGrenade].field_2 >> 8);
            sprite[nGrenadeSprite].picnum = seq_GetSeqPicnum2(nSeq, GrenadeList[nGrenade].field_2 >> 8);

            if (g_kexMode)
            {
                // Proj_AmunBomb has no expires/bouncy fields. It remains in
                // the player's hand indefinitely, then uses default gravity
                // and runs its death callback on the first impact.
                if (GrenadeList[nGrenade].field_10 < 0)
                    return;

                sprite[nGrenadeSprite].zvel = min(16384,
                    sprite[nGrenadeSprite].zvel + kKexBombGravity);
                int const nMov = movesprite(nGrenadeSprite,
                    GrenadeList[nGrenade].x, GrenadeList[nGrenade].y,
                    sprite[nGrenadeSprite].zvel,
                    (kKexBombHeight * 96) / 2,
                    (kKexBombHeight * 96) / 2, CLIPMASK1);
                if (nMov)
                    ExplodeGrenade(nGrenade);
                return;
            }

            GrenadeList[nGrenade].field_E--;
            if (!GrenadeList[nGrenade].field_E)
            {
                int nPlayer = nGrenadePlayer[nGrenade];

                if (GrenadeList[nGrenade].field_10 < 0)
                {
                    PlayerList[nPlayer].nWeaponState = 0;
                    PlayerList[nPlayer].nWeaponFrame = 0;

                    if (PlayerList[nPlayer].nAmmo[kWeaponGrenade])
                    {
                        PlayerList[nPlayer].bIsFiring = kFalse;
                    }
                    else
                    {
                        SelectNewWeapon(nPlayer);

                        PlayerList[nPlayer].nCurrentWeapon = PlayerList[nPlayer].nNewWeapon;
                        PlayerList[nPlayer].nNewWeapon = -1;
                    }
                }

                ExplodeGrenade(nGrenade);
                return;
            }
            else
            {
                if (GrenadeList[nGrenade].field_10 < 0) {
                    return;
                }

                int nFrame = (GrenadeList[nGrenade].field_2 + GrenadeList[nGrenade].field_0) >> 8;

                GrenadeList[nGrenade].field_2 += GrenadeList[nGrenade].field_0;

                if (nFrame < 0)
                {
                    GrenadeList[nGrenade].field_2 += SeqSize[nSeq] << 8;
                }
                else
                {
                    if (nFrame >= SeqSize[nSeq])
                    {
                        if (GrenadeList[nGrenade].field_C)
                        {
                            DestroyGrenade(nGrenade);
                            return;
                        }
                        else
                        {
                            GrenadeList[nGrenade].field_2 = GrenadeList[nGrenade].field_C;
                        }
                    }
                }

                if (GrenadeList[nGrenade].field_C) {
                    return;
                }

                int zVel = sprite[nGrenadeSprite].zvel;

                Gravity(nGrenadeSprite);
                int nMov = movesprite(nGrenadeSprite, GrenadeList[nGrenade].x, GrenadeList[nGrenade].y, sprite[nGrenadeSprite].zvel, sprite[nGrenadeSprite].clipdist >> 1, sprite[nGrenadeSprite].clipdist >> 1, CLIPMASK1);

                if (!nMov)
                    return;

                if (nMov & 0x20000)
                {
                    if (zVel)
                    {
                        if (SectDamage[sprite[nGrenadeSprite].sectnum] > 0)
                        {
                            ExplodeGrenade(nGrenade);
                            return;
                        }

                        GrenadeList[nGrenade].field_0 = (uint8_t)totalmoves; // limit to 8bits?

                        D3PlayFX(StaticSound[kSoundGrenadeDrop], nGrenadeSprite);

                        sprite[nGrenadeSprite].zvel = -(zVel >> 1);

                        if (sprite[nGrenadeSprite].zvel > -1280)
                        {
                            D3PlayFX(StaticSound[kSoundGrenadeRoll], nGrenadeSprite);
                            GrenadeList[nGrenade].field_0 = 0;
                            GrenadeList[nGrenade].field_2 = 0;
                            sprite[nGrenadeSprite].zvel = 0;
                            GrenadeList[nGrenade].field_A = 1;
                        }
                    }

                    GrenadeList[nGrenade].field_0 = 255 - (RandomByte() * 2);
                    GrenadeList[nGrenade].x -= (GrenadeList[nGrenade].x >> 4);
                    GrenadeList[nGrenade].y -= (GrenadeList[nGrenade].y >> 4);
                }

                // loc_2CF60:
                if ((nMov & 0xC000) >= 0x8000)
                {
                    if ((nMov & 0xC000) <= 0x8000)
                    {
                        BounceGrenade(nGrenade, GetWallNormal(nMov & 0x3FFF));
                    }
                    else if ((nMov & 0xC000) == 0xC000)
                    {
                        BounceGrenade(nGrenade, sprite[nMov & 0x3FFF].ang);
                    }
                }

                GrenadeList[nGrenade].field_2 = 0;
                return;
            }

            break;
        }

        case 0xA0000:
        {
            if (g_kexMode)
                break;
            if (nGrenadeSprite != nRadialSpr && !GrenadeList[nGrenade].field_C)
            {
                if (runlist_CheckRadialDamage(nGrenadeSprite) > 280)
                {
                    GrenadeList[nGrenade].field_E = RandomSize(4) + 1;
                }
            }
            break;
        }
    }
}

void KexGrenade_TestTick()
{
    if (s_kexGrenadeTest == -1)
        s_kexGrenadeTest = g_kexMode && Bgetenv("VB_KEX_BOMB_TEST") ? 0 : -2;
    if (s_kexGrenadeTest != 0)
        return;

    int const nPlayer = nLocalPlayer;
    short const nPlayerSprite = PlayerList[nPlayer].nSprite;
    if ((unsigned)nPlayerSprite >= (unsigned)kMaxSprites
        || sprite[nPlayerSprite].sectnum < 0 || nPlayerGrenade[nPlayer] >= 0)
        return;

    short const savedAmmo = PlayerList[nPlayer].nAmmo[kWeaponGrenade];
    if (PlayerList[nPlayer].nAmmo[kWeaponGrenade] < 1)
        PlayerList[nPlayer].nAmmo[kWeaponGrenade] = 1;
    int const nGrenade = BuildGrenade(nPlayer);
    if (nGrenade < 0)
    {
        PlayerList[nPlayer].nAmmo[kWeaponGrenade] = savedAmmo;
        OSD_Printf("Kex Amun bomb test: FAIL (no free grenade)\n");
        s_kexGrenadeTest = -2;
        return;
    }

    for (int i = 0; i < 40; i++)
        KexGrenade_Charge(nPlayer);
    int const chargeSteps = GrenadeList[nGrenade].field_E;
    int const nBombSprite = ThrowGrenade(nPlayer, 0, 0, 0, 0);
    Grenade const &bomb = GrenadeList[nGrenade];
    uint64_t const speedSquared = (uint64_t)(int64_t)bomb.x * bomb.x
                                + (uint64_t)(int64_t)bomb.y * bomb.y;
    uint64_t const expectedSpeed = (uint64_t)(kKexBombMaxSpeed * 6) * 16384;
    bool const sequenceOkay = KexGrenadeSequence() >= 0
        && SeqSize[KexGrenadeSequence()] == 1;
    bool explosionEffectsExact = false;
    int landEffect = -1, waterEffect = -1;
    if (nBombSprite >= 0)
    {
        landEffect = KexExplosion01_Spawn(sprite[nBombSprite].x,
            sprite[nBombSprite].y, sprite[nBombSprite].z,
            sprite[nBombSprite].sectnum, 32, 0);
        waterEffect = KexExplosion01_Spawn(sprite[nBombSprite].x,
            sprite[nBombSprite].y, sprite[nBombSprite].z,
            sprite[nBombSprite].sectnum, 32, 1);
    }
    if (landEffect >= 0 && waterEffect >= 0)
    {
        short const nLandSprite = GetAnimSprite(landEffect);
        short const nWaterSprite = GetAnimSprite(waterEffect);
        int landLoop = -1, waterLoop = -1;
        char const *const landAlias = KexSound_Alias("bomb_exp", &landLoop);
        char const *const waterAlias = KexSound_Alias("bomb_wat", &waterLoop);
        explosionEffectsExact = AnimList[landEffect].nSeq
                == SeqOffsets[kSeqKexExplosion]
            && AnimList[waterEffect].nSeq
                == SeqOffsets[kSeqKexWaterExplosion]
            && SeqSize[AnimList[landEffect].nSeq] == 21
            && SeqSize[AnimList[waterEffect].nSeq] == 21
            && sprite[nLandSprite].z == sprite[nBombSprite].z - 32 * 16 * 6
            && sprite[nWaterSprite].z == sprite[nBombSprite].z - 32 * 16 * 6
            && sprite[nLandSprite].xrepeat == 96
            && sprite[nLandSprite].yrepeat == 96
            && sprite[nWaterSprite].xrepeat == 48
            && sprite[nWaterSprite].yrepeat == 48
            && (AnimFlags[landEffect] & 4) && (AnimFlags[waterEffect] & 4)
            && landAlias && !Bstrcmp(landAlias, "sounds/explode03.wav")
            && waterAlias && !Bstrcmp(waterAlias, "sounds/explode02.wav")
            && !landLoop && !waterLoop;
        DestroyAnim(landEffect);
        mydeletesprite(nLandSprite);
        DestroyAnim(waterEffect);
        mydeletesprite(nWaterSprite);
    }
    else
    {
        if (landEffect >= 0)
        {
            short const nEffect = GetAnimSprite(landEffect);
            DestroyAnim(landEffect);
            mydeletesprite(nEffect);
        }
        if (waterEffect >= 0)
        {
            short const nEffect = GetAnimSprite(waterEffect);
            DestroyAnim(waterEffect);
            mydeletesprite(nEffect);
        }
    }

    bool const okay = nBombSprite >= 0 && chargeSteps == kKexBombMaxCharge
        && bomb.field_10 == kKexBombMaxSpeed
        && sprite[nBombSprite].clipdist == (kKexBombRadius * 6 + 2) / 4
        && sprite[nBombSprite].xrepeat == 20 && sprite[nBombSprite].yrepeat == 20
        && sprite[nBombSprite].zvel == -(kKexBombMinSpeed / 2) * 96
        && speedSquared >= (expectedSpeed - 16384) * (expectedSpeed - 16384)
        && speedSquared <= (expectedSpeed + 16384) * (expectedSpeed + 16384)
        && PlayerList[nPlayer].nAmmo[kWeaponGrenade]
             == max(0, (int)savedAmmo - 1)
        && kKexBombBuildBlastRadius == 12 && sequenceOkay
        && explosionEffectsExact;

    DestroyGrenade(nGrenade);
    nGrenadePlayer[nGrenade] = -1;
    nPlayerGrenade[nPlayer] = -1;
    PlayerList[nPlayer].nAmmo[kWeaponGrenade] = savedAmmo;
    OSD_Printf("Kex Amun bomb test: %s (charge 16..48; radius 9/clip 14; velocity 288/-768; gravity .75; blast 512/140; exact 21-tick land/water effects at +32; no fuse/bounce)\n",
               okay ? "PASS" : "FAIL");
    s_kexGrenadeTest = -2;
}

class GrenadeLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void GrenadeLoadSave::Load()
{
    Read(&nGrenadeCount, sizeof(nGrenadeCount));
    Read(&nGrenadesFree, sizeof(nGrenadesFree));

    Read(GrenadeFree, sizeof(GrenadeFree));
    Read(GrenadeList, sizeof(GrenadeList));
}

void GrenadeLoadSave::Save()
{
    Write(&nGrenadeCount, sizeof(nGrenadeCount));
    Write(&nGrenadesFree, sizeof(nGrenadesFree));

    Write(GrenadeFree, sizeof(GrenadeFree));
    Write(GrenadeList, sizeof(GrenadeList));
}

static GrenadeLoadSave* myLoadSave;

void GrenadeLoadSaveConstruct()
{
    myLoadSave = new GrenadeLoadSave();
}
