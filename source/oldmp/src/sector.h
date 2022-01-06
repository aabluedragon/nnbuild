#ifndef SECTOR_H
#define SECTOR_H

#include "actors.h"  // ActorData_t
#include "macros.h"
#include "player.h"  // PlayerSpawn_t

typedef struct {
// this needs to have a copy of everything related to the map/actor state
// see savegame.c
    short numwalls;
    walltype wall[MAXWALLS];
    short numsectors;
    sectortype sector[MAXSECTORS];
    spritetype sprite[MAXSPRITES];
    spriteext_t spriteext[MAXSPRITES];
    short tailspritefree;
    short headspritesect[MAXSECTORS+1];
    short prevspritesect[MAXSPRITES];
    short nextspritesect[MAXSPRITES];
    short headspritestat[MAXSTATUS+1];
    short prevspritestat[MAXSPRITES];
    short nextspritestat[MAXSPRITES];
    short g_numCyclers;
    short cyclers[MAXCYCLERS][6];
    PlayerSpawn_t g_playerSpawnPoints[MAXSPAWNPOINTS];
    short g_numAnimWalls;
    short SpriteDeletionQueue[1024],g_spriteDeleteQueuePos;
    animwalltype animwall[MAXANIMWALLS];
    vec2_t origins[2048];
    short g_mirrorWall[64], g_mirrorSector[64], g_mirrorCount;
    char show2dsector[(MAXSECTORS+7)>>3];
    short g_numClouds,clouds[128],cloudx[128],cloudy[128];
    int g_pskyidx;
    ActorData_t actor[MAXSPRITES];
    short pskyoff[MAXPSKYTILES], pskybits;

    int g_animateCount;
    sectanim_t sectorAnims[MAXANIMATES];

    char g_numPlayerSprites;
    char g_earthquakeTime;
    ClockTicks lockclock;
    int randomseed, g_globalRandom;
    char scriptptrs[MAXSPRITES];
    intptr_t *vars[MAXGAMEVARS];
} mapstate_t;

typedef struct {
    int partime, designertime;
    char *name, *filename, *musicfn, *alt_musicfn;
    mapstate_t *savedstate;
} map_t;

#endif // SECTOR_H