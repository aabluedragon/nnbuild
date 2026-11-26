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

#ifndef __sequence_h__
#define __sequence_h__

#include "compat.h"

enum {
    kSeqRothands = 0,
    kSeqSword,
    kSeqPistol,
    kSeqM60,
    kSeqFlamer,
    kSeqGrenade,
    kSeqCobra,
    kSeqBoneSaw,
    kSeqScramble,
    kSeqGlove,
    kSeqMummy,
    kSeqSkull,
    kSeqPoof,
    kSeqKapow,
    kSeqFireball,
    kSeqBubble,
    kSeqSpider,
    kSeqAnubis,
    kSeqAnuBall,
    kSeqFish,
    kSeqSnakehed,
    kSeqSnakBody,
    kSeqWasp,
    kSeqCobraPow,
    kSeqScorp,
    kSeqJoe, // player pic
    kSeqStatus,
    kSeqDead,
    kSeqDeadEx,
    kSeqAnuPoof,
    kSeqSkulPoof,
    kSeqBullet,
    kSeqShadow,
    kSeqGrenRoll,
    kSeqGrenBoom,
    kSeqSplash,
    kSeqGrenPow,
    kSeqSkulSrt,
    kSeqFirePoof,
    kSeqBloodHit,
    kSeqLion,
    kSeqItems,
    kSeqLavag,
    kSeqLsplash,
    kSeqLavaShot,
    kSeqSmokeBal,
    kSeqFirePot,
    kSeqRex,
    kSeqSet,
    kSeqQueen,
    kSeqRoach,
    kSeqHawk,
    kSeqSetGhost,
    kSeqSetGBlow,
    kSeqBizzTail,
    kSeqBizzPoof,
    kSeqQueenEgg,
    kSeqRoacShot,
    kSeqBackgrnd,
    kSeqScreens,
    kSeqArrow,
    kSeqFonts,
    kSeqDrips,
    kSeqFireTrap,
    kSeqMagic2,
    kSeqCreepy,
    kSeqSlider,
    kSeqRavolt, // 67
    kSeqEyeHit,
    kSeqFont2,
    kSeqSeeBubbl,
    kSeqBlood,
    kSeqDrum,
    kSeqPoof2,
    kSeqDeadBrn, // 74
    kSeqGrenBubb,
    kSeqRochfire,
    kSeqRat,
    kSeqBoulder,    // NNBuild: the remaster boulder trap; BOULDER.SEQ, optional
    kSeqManacle,    // NNBuild: the remaster's Manacle weapon; MANACLE.SEQ, optional
    kSeqScorpion,   // NNBuild: the remaster's scorpion, the spider's SEQ on its own tiles; optional
    kSeqSeedling,   // NNBuild: Queen hatchling, the flyer SEQ on seedling tiles; optional
    kSeqAmunBomb,   // NNBuild: remaster Amun-bomb world projectile; optional
    kSeqKexMuzzle,      // NNBuild: fireball-spawner Fx_FireballShootPuff; optional
    kSeqKexImpact,      // NNBuild: Fx_FireballExplode impact puff; optional
    kSeqKexCobraRed,    // NNBuild: Fx_RedCobraExplode impact puff; optional
    kSeqKexCobraGreen,  // NNBuild: Fx_GreenCobraExplode impact puff; optional
    kSeqKexCobraCtl,    // NNBuild: timed four-puff cobra death callbacks; optional
    kSeqKexAnuImpact,   // NNBuild: Proj_Anubis death animation; optional
    kSeqKexExplosion,   // NNBuild: Fx_Explosion01 land explosion; optional
    kSeqKexWaterExplosion, // NNBuild: Fx_Explosion01_Water; optional
    kSeqKexVasePuff,       // NNBuild: Fx_VaseExplodePuff; optional
    kSeqKexAmmoSpawn,      // NNBuild: Fx_AmmoSpawn delayed orb; optional
    kSeqKexHealthSpawn,    // NNBuild: Fx_HealthSpawn delayed orb; optional
    kSeqKexGibDeath,       // NNBuild: Fx_Gibs01 + delayed A_RandomItem; optional
    kSeqKexBirdFeather,    // NNBuild: Obj_Bird_Feathers loop/fall actor; optional
    kSeqKexWaterSplash,    // NNBuild: Fx_WaterSplash surface-crossing actor; optional
    kSeqKexFlame           // NNBuild: Proj_Flame flight and sizzle actors; optional
};

struct actionSeq
{
    short a;
    short b;
};

extern short frames;

extern short SeqBase[];
extern short SeqSize[];
extern short SeqOffsets[];
int seq_IsLoaded(int nSeq);      // NNBuild: an optional SEQ that was actually there

extern short FrameFlag[];

extern short nShadowWidth;
extern short nFlameHeight;

extern short nPilotLightFrame;
extern short nPilotLightCount;

extern short laststatustile;

extern int laststatusx;
extern int laststatusy;

void seq_LoadSequences();
int seq_GetFrameSound(int val, int edx);
void seq_MoveSequence(short nSprite, short nSeq, short bx);
int seq_GetSeqPicnum2(short nSeq, short nFrame);
int seq_GetSeqPicnum(short nSeq, short edx, short ebx);
void seq_DrawStatusSequence(short nSequence, uint16_t edx, short ebx);

int seq_DrawGunSequence(int nSeqOffset, short dx, int xOffs, int yOffs, int nShade, int nPal);
short seq_GetFrameFlag(short val, short nFrame);
int seq_PlotSequence(short nSprite, short edx, short nFrame, short ecx);
int seq_PlotArrowSequence(short nSprite, short nSeq, int nVal);
void seq_DrawPilotLightSeq(int xOffset, int yOffset);

#endif
