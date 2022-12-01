#ifndef GAMESTRUCTURES_H
#define GAMESTRUCTURES_H

#include "build.h"

int32_t __fastcall VM_GetPlayer(int const playerNum, int32_t labelNum, int const lParm2);
void    __fastcall VM_SetPlayer(int const playerNum, int const labelNum, int const lParm2, int32_t const newValue);

void __fastcall X_AccessTileData(int iSet, int lVar1, int lLabelID, int lVar2);
void __fastcall X_AccessUserdef(int iSet, int labelNum, int lVar2, int lParm2);
void __fastcall X_AccessActiveProjectile(int iSet, int lVar1, int lLabelID, int lVar2);
void __fastcall X_AccessPlayerInput(int iSet, int lVar1, int lLabelID, int lVar2);
void __fastcall X_AccessWall(int iSet, int lVar1, int lLabelID, int lVar2);
void __fastcall X_AccessSector(int iSet, int iSector, int labelNum, int lVar2);
void __fastcall X_AccessSprite(int iSet, int iActor, int labelNum, int lVar2, int lParm2);
void __fastcall X_AccessTsprite(int iSet, int iActor, int labelNum, int lVar2);
void __fastcall X_AccessProjectile(int iSet, int tileNum, int labelNum, int lVar2);

/*
extern void X_AccessUserdef(int iSet, int lLabelID, int lVar2, int lParm2);
extern void X_AccessActiveProjectile(int iSet, int lVar1, int lLabelID, int lVar2);
extern void X_AccessPlayer(int iSet, int lVar1, int lLabelID, int lVar2, int lParm2);
extern void X_AccessPlayerInput(int iSet, int lVar1, int lLabelID, int lVar2);
extern void X_AccessWall(int iSet, int lVar1, int lLabelID, int lVar2);
extern void X_AccessSector(int iSet, int lVar1, int lLabelID, int lVar2);
extern void X_AccessSprite(int iSet, int lVar1, int lLabelID, int lVar2, int lParm2);
extern void X_AccessTsprite(int iSet, int lVar1, int lLabelID, int lVar2);
extern void X_AccessProjectile(int iSet, int lVar1, int lLabelID, int lVar2);
*/

#endif