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

#ifndef __move_h__
#define __move_h__

// 16 bytes
struct BlockInfo
{
    int x;
    int y;
    int walldist;
    short nSprite;
};
extern BlockInfo sBlockInfo[];

extern int hihit;
extern short nChunkSprite[];
extern short nBodySprite[];

signed int lsqrt(int a1);
void MoveThings();
void ResetMoveFifo();
void InitChunks();
void InitPushBlocks();
void Gravity(short nSprite);
short UpdateEnemy(short *nEnemy);
int MoveCreature(short nSprite);
int MoveCreatureWithCaution(int nSprite);
int KexScaleCreatureMove(short nSprite, int nMove); // Pharaoh: kexSlaveAI moveSpeed * 1.1
// kexPuppet::OnDamage selects its sizzle feedback from its live lava/slime
// timers, not just the sector underfoot.
int KexHazard_IsActive(int nPlayer);
void KexNoStanding_Test(void);            // VB_KEX_NOEDGE_TEST runtime regression probe
void WheresMyMouth(int nPlayer, int *x, int *y, int *z, short *sectnum);

int GetSpriteHeight(int nSprite);

int GrabBody();

int GrabBodyGunSprite();
void CreatePushBlock(int nSector);

void FuncCreatureChunk(int a, int, int nRun);

int FindPlayer(int nSprite, int nDistance);

int BuildCreatureChunk(int nVal, int nPic);
// KEX A_TossActor's shared 0,0,+32 / +/-8 / +4..16 gib recipe.  This is
// deliberately separate from the original game's much faster chunk throw.
int KexTossCreatureChunk(int nVal, int nPic);
enum KexTossBounceSound
{
    kKexTossBounceSoundGlass,
    kKexTossBounceSoundWood,
    kKexTossBounceSoundFlesh,
};
// The remaster's vase, barrel, exploding-vase, and boulder pieces carry
// bouncy=1 plus A_DestroyAtRest; creature gibs instead use expires=1. Each
// shard also retains its three authored floor-bounce WAV variants.
int KexTossBouncyCreatureChunk(int nVal, int nPic, KexTossBounceSound sound);
// Live disposable KEX shards beyond the classic 75-slot ring. Used only by
// the hidden save/reload regression probe.
int KexTossOverflowCount(void);

void BuildNear(int x, int y, int walldist, int nSector);
int BelowNear(short nSprite);

int PlotCourseToSprite(int nSprite1, int nSprite2);
// KEX contact radii in twice-Build-XY units. The doubled representation keeps
// the remaster player puppet's authored 95.25-unit radius exact at WORLD=6.
int KexContactRadius2(short nSprite);
// Logical KEX actor height in Build Z units. This is shared by sight, melee,
// and projectile callbacks so their target centres never depend on sprite art.
int KexActorHeightBuild(short nSprite);
// KEX A_CheckMelee/A_MeleeAttack range: both collision radii plus the
// callback's authored reach, measured in the remaster's WORLD=6 units.
int KexMeleeRange(short nSprite, short nTarget, int nReach);
bool KexAIWithinSightDistance(int xDistance, int yDistance);
bool KexAIEnterMeleeRange(short nSprite, short nTarget, int nReach);
bool KexMeleeAttackRange(short nSprite, short nTarget, int nReach);
// KEX A_Quake is a fixed local camera-shake duration, unlike native SetQuake.
void KexQuake(int nTicks);
int KexQuakeYaw();

void CheckSectorFloor(short nSector, int z, int *x, int *y);

int GetAngleToSprite(int nSprite1, int nSprite2);

int GetWallNormal(short nWall);

int GetUpAngle(short nSprite1, int nVal, short nSprite2, int ecx);
void MoveSector(short nSector, int nAngle, int *nXVel, int *nYVel);

int AngleChase(int nSprite, int nSprite2, int ebx, int ecx, int push1);

void SetQuake(short nSprite, int nVal);

#endif
