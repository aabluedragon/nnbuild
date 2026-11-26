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

#ifndef __anims_h__
#define __anims_h__

#include "compat.h"

struct Anim
{
    short nSeq;
    short nFrame;
    short nSprite;
};

enum
{
    kAnimFlag1 = (1 << 2),
    kAnimLoop  = (1 << 4)
};

extern Anim AnimList[];
extern uint8_t AnimFlags[];

void InitAnims();
void KexImpactLight_Tick();
// Returns -1 only when exact converted art is unavailable. A value below -1
// means the exact path was selected but animation allocation was exhausted,
// so callers must not add a second, classic effect.
int KexExplosion01_Spawn(int x, int y, int z, short nSector,
                         int nKexZOffset, int bWater);
int KexVasePuff_Spawn(int x, int y, int z, short nSector,
                      int nKexZOffset);
int KexWaterSplash_Spawn(int x, int y, int z, short nSector);
void KexWaterSplash_OnTransition(short nOldSector, int oldX, int oldY,
                                 int oldZ, short nNewSector, int newX,
                                 int newY);
int KexGibDeath_Spawn(int x, int y, int z, short nSector,
                      int nKexZOffset, int nRepeat, int nKiller);
void KexGibDeath_TestTick();
int KexBirdFeather_Spawn(int x, int y, int z, short nSector);
void KexBirdFeather_TestTick();
void DestroyAnim(int nAnim);
int BuildAnim(int nSprite, int val, int val2, int x, int y, int z, int nSector, int nRepeat, int nFlag);
short GetAnimSprite(short nAnim);
void UnlinkIgnitedAnim(int nSprite);
void FuncAnim(int, int, int);
void BuildExplosion(short nSprite);
int BuildSplash(int nSprite, int nSector);

#endif
