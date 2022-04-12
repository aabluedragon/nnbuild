#ifndef GAMETYPES_H
#define GAMETYPES_H

#include "player.h"

#define MAXGAMETYPES 16
#define GTFLAGS(x) (Gametypes[ud.coop].flags & (x))

enum GametypeFlags_t {
    GAMETYPE_COOP                   = (1 << 0),
    GAMETYPE_WEAPSTAY               = (1 << 1),
    GAMETYPE_FRAGBAR                = (1 << 2),
    GAMETYPE_SCORESHEET             = (1 << 3),
    GAMETYPE_DMSWITCHES             = (1 << 4),
    GAMETYPE_COOPSPAWN              = (1 << 5),
    GAMETYPE_ACCESSCARDSPRITES      = (1 << 6),
    GAMETYPE_COOPVIEW               = (1 << 7),
    GAMETYPE_COOPSOUND              = (1 << 8),
    GAMETYPE_OTHERPLAYERSINMAP      = (1 << 9),
    GAMETYPE_ALLOWITEMRESPAWN       = (1 << 10),
    GAMETYPE_MARKEROPTION           = (1 << 11),
    GAMETYPE_PLAYERSFRIENDLY        = (1 << 12),
    GAMETYPE_FIXEDRESPAWN           = (1 << 13),
    GAMETYPE_ACCESSATSTART          = (1 << 14),
    GAMETYPE_PRESERVEINVENTORYDEATH = (1 << 15),
    GAMETYPE_TDM                    = (1 << 16),
    GAMETYPE_TDMSPAWN               = (1 << 17),
};

enum DMFlags_t {
    DMFLAG_FRIENDLYFIRE             = (1 << 0),
    DMFLAG_NOMONSTERS               = (1 << 1),
    DMFLAG_RESPAWNMONSTERS          = (1 << 2),
    DMFLAG_RESPAWNITEMS             = (1 << 3),
    DMFLAG_RESPAWNINVENTORY         = (1 << 4),
    DMFLAG_MARKERS                  = (1 << 5),
    DMFLAG_NOEXITS                  = (1 << 6),
    DMFLAG_WEAPONSTAY               = (1 << 7),
    DMFLAG_NOPLAYERID               = (1 << 8),
    DMFLAG_NOFOVCHANGE              = (1 << 9),
    DMFLAG_ALLOWVISIBILITYCHANGE    = (1 << 10),
};

#define DEFAULT_DMFLAGS (DMFLAG_FRIENDLYFIRE | DMFLAG_MARKERS)

static inline void DMFLAGS_SET(int32_t flags, bool state)
{
    if (state)
        ud.dmflags |= (flags);
    else
        ud.dmflags &= ~(flags);
}

static inline void M_DMFLAGS_SET(int32_t flags, bool state)
{
    if (state)
        ud.m_dmflags |= (flags);
    else
        ud.m_dmflags &= ~(flags);
}

static inline bool DMFLAGS_TEST(int32_t flags)
{
    return (ud.dmflags & (flags));
}

static inline bool M_DMFLAGS_TEST(int32_t flags)
{
    return (ud.m_dmflags & (flags));
}

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
    if (DMFLAGS_TEST(DMFLAG_FRIENDLYFIRE))
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