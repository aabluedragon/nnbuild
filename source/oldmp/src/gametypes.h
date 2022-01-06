#ifndef GAMETYPES_H
#define GAMETYPES_H

#include "player.h"

#define MAXGAMETYPES 16
#define GTFLAGS(x) (Gametypes[ud.coop].flags & (x))

enum GametypeFlags_t {
    GAMETYPE_COOP = 1,
    GAMETYPE_WEAPSTAY = 2,
    GAMETYPE_FRAGBAR = 4,
    GAMETYPE_SCORESHEET = 8,
    GAMETYPE_DMSWITCHES = 16,
    GAMETYPE_COOPSPAWN = 32,
    GAMETYPE_ACCESSCARDSPRITES = 64,
    GAMETYPE_COOPVIEW = 128,
    GAMETYPE_COOPSOUND = 256,
    GAMETYPE_OTHERPLAYERSINMAP = 512,
    GAMETYPE_ALLOWITEMRESPAWN = 1024,
    GAMETYPE_MARKEROPTION = 2048,
    GAMETYPE_PLAYERSFRIENDLY = 4096,
    GAMETYPE_FIXEDRESPAWN = 8192,
    GAMETYPE_ACCESSATSTART = 16384,
    GAMETYPE_PRESERVEINVENTORYDEATH = 32768,
    GAMETYPE_TDM = 65536,
    GAMETYPE_TDMSPAWN = 131072
};

typedef struct {
    char name[33];
    int32_t flags;
} Gametype_t;
extern Gametype_t Gametypes[MAXGAMETYPES];
extern uint8_t g_numGametypes;

typedef struct {
    char name[32];
    uint8_t palnum;
    int16_t frags;
    int16_t suicides;
    int8_t blocked;
} Team_t;
extern Team_t teams[MAXPLAYERS];
extern uint8_t numTeams;

// Inline Functions

// Returns true if attacking a friendly while friendly fire is disabled.
static inline bool G_BlockFriendlyFire(int32_t p1, int32_t p2)
{
    if (ud.ffire)
        return false;

    if (GTFLAGS(GAMETYPE_PLAYERSFRIENDLY))
        return true;

    if (GTFLAGS(GAMETYPE_TDM) && (g_player[p1].ps->team == g_player[p2].ps->team))
        return true;

    return false;
}

// Formerly clearfrags(), clears gametype stats such as frags, suicides, etc.
static inline void G_ClearGametypeStats(void)
{
    int32_t i;
    playerdata_t* p;

    for (i = 0; i < MAXPLAYERS; i++)
    {
        p = &g_player[i];

        if (p->ps != NULL)
        {
            p->ps->frag = p->ps->fraggedself = 0;
            Bmemset(p->frags, 0, sizeof(p->frags));
        }
    }

    for (i = 0; i < numTeams; i++)
    {
        teams[i].frags = 0;
        teams[i].suicides = 0;
        teams[i].blocked = 0;
    }
}

#endif