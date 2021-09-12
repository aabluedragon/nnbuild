//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers

This file is part of EDuke32

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------
#define global_c_
#include "duke3d.h"
#include "global.h"

// Pre-initialized globals, the rest is in global.h
int g_spriteGravity = 176; // JBF: g_spriteGravity modified to default to Atomic ed. default when using 1.3d CONs
int g_actorRespawnTime = 768;
int g_itemRespawnTime = 768;
int g_timerTicsPerSecond = TICRATE;
int g_scriptSize = 16384;
int g_playerFriction = 0xcc00;
int g_networkBroadcastMode = NETMODE_OFFLINE;
int g_numFreezeBounces = 3;
short g_spriteDeleteQueueSize=64;

char EpisodeNames[MAXVOLUMES][33] = { "L.A. MELTDOWN", "LUNAR APOCALYPSE", "SHRAPNEL CITY" };
char g_numVolumes = 3;

char SkillNames[5][33] = { "PIECE OF CAKE", "LET'S ROCK", "COME GET SOME", "DAMN I'M GOOD" };

char GametypeNames[MAXGAMETYPES][33] = { "DUKEMATCH","COOPERATIVE PLAY","TEAM DUKEMATCH" };
int GametypeFlags[MAXGAMETYPES] =
{
    // DUKEMATCH (0)
    GAMETYPE_FRAGBAR | GAMETYPE_SCORESHEET | GAMETYPE_DMSWITCHES | GAMETYPE_ALLOWITEMRESPAWN | GAMETYPE_MARKEROPTION | GAMETYPE_ACCESSATSTART,
    // COOPERATIVE PLAY (1)
    GAMETYPE_COOP | GAMETYPE_WEAPSTAY | GAMETYPE_COOPSPAWN | GAMETYPE_ACCESSCARDSPRITES | GAMETYPE_COOPVIEW | GAMETYPE_COOPSOUND | GAMETYPE_OTHERPLAYERSINMAP | GAMETYPE_ALLOWITEMRESPAWN | GAMETYPE_PLAYERSFRIENDLY | GAMETYPE_FIXEDRESPAWN | GAMETYPE_PRESERVEINVENTORYDEATH,
    // TEAM DUKEMATCH (2)
    GAMETYPE_FRAGBAR | GAMETYPE_SCORESHEET | GAMETYPE_DMSWITCHES | GAMETYPE_ALLOWITEMRESPAWN | GAMETYPE_MARKEROPTION | GAMETYPE_ACCESSATSTART | GAMETYPE_TDM | GAMETYPE_TDMSPAWN,
};
char g_numGametypes = 3;

short BlimpSpawnSprites[15] =
{
    RPGSPRITE__STATIC,
    CHAINGUNSPRITE__STATIC,
    DEVISTATORAMMO__STATIC,
    RPGAMMO__STATIC,
    RPGAMMO__STATIC,
    JETPACK__STATIC,
    SHIELD__STATIC,
    FIRSTAID__STATIC,
    STEROIDS__STATIC,
    RPGAMMO__STATIC,
    RPGAMMO__STATIC,
    RPGSPRITE__STATIC,
    RPGAMMO__STATIC,
    FREEZESPRITE__STATIC,
    FREEZEAMMO__STATIC
};

char CheatKeys[2] = { sc_D, sc_N };
char g_setupFileName[BMAX_PATH] = SETUPFILENAME;

