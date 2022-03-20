#ifndef ACTORS_H
#define ACTORS_H

#include "player.h"
#include "namesdyn.h"

typedef struct {
    int floorz, ceilingz, lastvx, lastvy;
    vec3_t bpos;
    int flags;
    intptr_t temp_data[10]; // sometimes used to hold pointers to con code
    short htpicnum, htang, htextra, htowner, movflag;
    short tempang, actorstayput, dispicnum;
    short timetosleep;
    char cgg;
    char filler;
    projectile_t projectile;
} ActorData_t;
extern ActorData_t actor[MAXSPRITES];

extern intptr_t* actorscrptr[MAXTILES];

void G_UpdateInterpolations(void);
int G_SetInterpolation(int32_t* const posptr);
void G_StopInterpolation(const int32_t* const posptr);
void G_DoInterpolations(int smoothratio);
void G_RestoreInterpolations(void);

void G_DoConveyorInterp(int32_t smoothratio);
void G_ResetConveyorInterp(void);

void G_ClearCameraView(DukePlayer_t* ps);

int G_CheckForSpaceCeiling(int sectnum);
int G_CheckForSpaceFloor(int sectnum);

void G_AddMonsterKills(int32_t spriteNum, int32_t amount);

// I think these should be moved to player.cpp/h
void P_AddAmmo(int weapon, DukePlayer_t* p, int amount);
void P_AddWeaponNoSwitch(DukePlayer_t* p, int weapon);
void P_AddWeapon(DukePlayer_t* pPlayer, int weaponNum, int switchWeapon);
void P_SelectNextInvItem(DukePlayer_t* p);
void P_CheckWeapon(DukePlayer_t* p);

void A_RadiusDamage(int i, int r, int hp1, int hp2, int hp3, int hp4);

int A_MoveSprite(int spritenum, int xchange, int ychange, int zchange, unsigned int cliptype);
int A_SetSprite(int i, unsigned int cliptype);

void A_DeleteSprite(int s);
void A_AddToDeleteQueue(int i);
void A_SpawnMultiple(int sp, int pic, int n);
void A_DoGuts(int sp, int gtype, int n);

int A_IncurDamage(int sn);

// I don't like the fact these are in actors.cpp. Should be sector.cpp, IMHO.
void Sect_SetInterpolation(int i);
void Sect_ClearInterpolation(int i);

void A_MoveCyclers(void);
void A_MoveDummyPlayers(void);

void A_PlayAlertSound(int i);

int A_CheckSpriteFlags(int iActor, int iType);
int A_CheckSpriteTileFlags(int iPicnum, int iType);
int A_CheckInventorySprite(spritetype* s);
int A_CheckEnemyTile(int pn);
int A_CheckEnemySprite(void const* s);
int A_CheckSwitchTile(int i);
int A_CheckNoSE7Water(uspriteptr_t const pSprite, int sectNum, int sectLotag, int32_t* pOther);

void G_MovePlayerSprite(int playerNum);
void G_MoveWorld(void);

enum
{
    PIPEBOMB_PICKUPTIMER = 0,
    PIPEBOMB_ORIGINALOWNER,
    PIPEBOMB_RESPAWNTIME,
    PIPEBOMB_WASHIT,
    PIPEBOMB_EXPLOSIONTIMER,
    PIPEBOMB_INWATER,
    PIPEBOMB_FUSETYPE,
    PIPEBOMB_FUSETIME,
    PIPEBOMB_INITIALZ,
    PIPEBOMB_INITIALSECT,
};

enum
{
    FUSE_TIMED = 1,
    FUSE_REMOTE,
    FUSE_EXPLODE,
};

enum
{
    GREENSLIME_FROZEN = -5,
    GREENSLIME_ONPLAYER,
    GREENSLIME_DEAD,  // set but not checked anywhere...
    GREENSLIME_EATINGACTOR,
    GREENSLIME_DONEEATING,
    GREENSLIME_ONFLOOR,
    GREENSLIME_TOCEILING,
    GREENSLIME_ONCEILING,
    GREENSLIME_TOFLOOR,
};

#endif // ACTORS_H