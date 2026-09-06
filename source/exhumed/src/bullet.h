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

#ifndef __bullet_h__
#define __bullet_h__

#include "aistuff.h"

// 32 bytes
struct bulletInfo
{
    short nDamage; // 0
    short field_2; // 2
    int field_4;   // 4
    short field_8; // 8
    short nSeq; // 10
    short field_C; // 12
    short nFlags;
    short nRadius; // damage radius
    short xyRepeat;
};

extern bulletInfo BulletInfo[];

extern short nRadialBullet;
extern short lasthitsect;
extern int lasthitz;
extern int lasthitx;
extern int lasthity;

void InitBullets();
short GrabBullet();
void DestroyBullet(short nRun);
int MoveBullet(short nBullet);
void SetBulletEnemy(short nBullet, short nEnemy);
int BuildBullet(short nSprite, int nType, int ebx, int ecx, int val1, int nAngle, int val2, int val3);
// KEX A_FireProjectile: callback distance is added to the source actor's
// authored radius; height and speed are in retail KEX units.  Homing thrust
// means the projectile curves its velocity rather than merely facing a target.
int BuildKexAIFireProjectile(short nSprite, int nType, short nTarget,
                             int nCallbackDistance, int nHeight,
                             int nSpeed, int bHomingThrust);
// The remaster's player flamethrower calls A_FireProjectile("Proj_Flame",
// 0, 0, -48, 80, 16). Its player puppet has a different authored radius
// from the legacy movement sprite, so it has a separate launch bridge.
int BuildKexPlayerFlameProjectile(short nSprite, int nPitch);
int KexBullet_CheckAIFireProjectile(int nBullet, short nSource, short nTarget,
                                    int nCallbackDistance, int nHeight,
                                    int nSpeed, int bHomingThrust);
// Mantis's rare PSX callback uses FireActor at z+192, then adds independent
// signed yaw/pitch spread and a floating 8..16-unit speed.
int BuildKexMantisFireProjectile(short nSprite, short nTarget);
int KexBullet_CheckMantisFireProjectile(int nBullet, short nSource);
// Exact remaster death-animation helpers shared by native enemy bullets and
// the vase-spawned green-cobra/Ra projectiles.
int KexProjectile_SpawnFireballImpact(int x, int y, int z, short nSector,
                                      int nKexZOffset, int bSoundBefore);
int KexProjectile_StartCobraBurst(int x, int y, int z, short nSector,
                                  int bRed);
void KexProjectile_CobraBurstFrame(short nControllerSprite, int nFrame);
void KexBullet_TestTick();
int GetBulletSprite(int nBullet);
void IgniteSprite(int nSprite);
void FuncBullet(int, int, int);
void BackUpBullet(int *x, int *y, short nAngle);

#endif
