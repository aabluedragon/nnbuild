#ifndef GAMESTRUCTURES_H
#define GAMESTRUCTURES_H

#include "build.h"

typedef struct
{
    const char* name;
    int         lId;
    int         flags;
    int         maxParm2;
    int16_t     offset;
} memberlabel_t;

extern const memberlabel_t SectorLabels[];
extern const memberlabel_t WallLabels[];
extern const memberlabel_t ActorLabels[];
extern const memberlabel_t PlayerLabels[];
extern const memberlabel_t ProjectileLabels[];
extern const memberlabel_t UserdefsLabels[];
extern const memberlabel_t InputLabels[];
extern const memberlabel_t TsprLabels[];
extern const memberlabel_t TileDataLabels[];

extern hashtable_t h_actor;
extern hashtable_t h_input;
extern hashtable_t h_paldata;
extern hashtable_t h_player;
extern hashtable_t h_projectile;
extern hashtable_t h_sector;
extern hashtable_t h_tiledata;
extern hashtable_t h_tsprite;
extern hashtable_t h_userdef;
extern hashtable_t h_wall;

static hashtable_t* const vmStructHashTablePtrs[] = {
    &h_actor, &h_input, /*&h_paldata,*/ &h_player, &h_projectile, &h_sector, &h_tiledata, &h_tsprite, &h_userdef, &h_wall,
};

void VM_InitHashTables(void);

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


#endif