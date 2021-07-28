//-------------------------------------------------------------------------
/*
Copyright (C) 1997, 2005 - 3D Realms Entertainment

This file is part of Shadow Warrior version 1.2

Shadow Warrior is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

Original Source: 1997 - Frank Maddin and Jim Norwood
Prepared for public release: 03/28/2005 - Charlie Wiederhold, 3D Realms
*/
//-------------------------------------------------------------------------

#include "duke3d.h"

char g_szfirstSyncMsg[MAXSYNCBYTES][60];
int g_foundSyncError = 0;

char syncstat[MAXSYNCBYTES];
int syncvaltail, syncvaltottail;

static int crctable[256];
#define updatecrc(dcrc,xz) (dcrc = (crctable[((dcrc)>>8)^((xz)&255)]^((dcrc)<<8)))

void initsynccrc(void)
{
    int i, j, k, a;

    for (j=0;j<256;j++)     //Calculate CRC table
    {
        k = (j<<8); a = 0;
        for (i=7;i>=0;i--)
        {
            if (((k^a)&0x8000) > 0)
                a = ((a<<1)&65535) ^ 0x1021;   //0x1021 = genpoly
            else
                a = ((a<<1)&65535);
            k = ((k<<1)&65535);
        }
        crctable[j] = (a&65535);
    }
}

static char Sync_PlayerPos(void)
{
    short i;
    unsigned short crc = 0;
    DukePlayer_t *pp;

    TRAVERSE_CONNECT(i)
    {
        pp = g_player[i].ps;
        updatecrc(crc, pp->pos.x & 255);
        updatecrc(crc, pp->pos.y & 255);
        updatecrc(crc, pp->pos.z & 255);
        updatecrc(crc, pp->q16ang & 255);
        updatecrc(crc, pp->q16horiz & 255);
    }

    return ((char) crc & 255);
}

static char Sync_PlayerData(void)
{
    int i;
    int j, nextj;
    unsigned short crc = 0;
    DukePlayer_t *pp;
    spritetype *spr;

    TRAVERSE_CONNECT(i)
    {
        pp = g_player[i].ps;

        updatecrc(crc, pp->curr_weapon & 255);
        updatecrc(crc, pp->kickback_pic & 255);
        updatecrc(crc, sprite[pp->i].extra & 255);
        updatecrc(crc, pp->bobcounter & 255);
    }

    TRAVERSE_SPRITE_STAT(headspritestat[STAT_PLAYER], j, nextj)
    {
        spr = &sprite[j];
        updatecrc(crc, (spr->x) & 255);
        updatecrc(crc, (spr->y) & 255);
        updatecrc(crc, (spr->z) & 255);
        updatecrc(crc, (spr->ang) & 255);
    }

    return ((char) crc & 255);
}

static char Sync_Actors(void)
{
    unsigned short crc = 0;
    int j;
    spritetype *spr;

    for (SPRITES_OF(STAT_ACTOR, j))
    {
        spr = &sprite[j];
        updatecrc(crc, (spr->x) & 255);
        updatecrc(crc, (spr->y) & 255);
        updatecrc(crc, (spr->z) & 255);
        updatecrc(crc, (spr->lotag) & 255);
        updatecrc(crc, (spr->hitag) & 255);
        updatecrc(crc, (spr->ang) & 255);
    }

    for (SPRITES_OF(STAT_ZOMBIEACTOR, j))
    {
        spr = &sprite[j];
        updatecrc(crc, (spr->x) & 255);
        updatecrc(crc, (spr->y) & 255);
        updatecrc(crc, (spr->z) & 255);
        updatecrc(crc, (spr->lotag) & 255);
        updatecrc(crc, (spr->hitag) & 255);
        updatecrc(crc, (spr->ang) & 255);
    }

    return ((char) crc & 255);
}

static char Sync_Projectiles(void)
{
    unsigned short crc = 0;
    int j;
    spritetype *spr;

    for (SPRITES_OF(STAT_PROJECTILE, j))
    {
        spr = &sprite[j];
        updatecrc(crc, (spr->x) & 255);
        updatecrc(crc, (spr->y) & 255);
        updatecrc(crc, (spr->z) & 255);
        updatecrc(crc, (spr->ang) & 255);
    }

    return ((char) crc & 255);
}

static char Sync_Map(void)
{
    unsigned short crc = 0;
    int j;
    spritetype *spr;
    walltype *wal;
    sectortype *sect;

    for (SPRITES_OF(STAT_EFFECTOR, j))
    {
        spr = &sprite[j];
        updatecrc(crc, (spr->x) & 255);
        updatecrc(crc, (spr->y) & 255);
        updatecrc(crc, (spr->z) & 255);
        updatecrc(crc, (spr->ang) & 255);
        updatecrc(crc, (spr->lotag) & 255);
        updatecrc(crc, (spr->hitag) & 255);
    }

    for (j=numwalls;j>=0;j--)
    {
        wal = &wall[j];
        updatecrc(crc, (wal->x) & 255);
        updatecrc(crc, (wal->y) & 255);
    }

    for (j=numsectors;j>=0;j--)
    {
        sect = &sector[j];
        updatecrc(crc, (sect->floorz) & 255);
        updatecrc(crc, (sect->ceilingz) & 255);
    }

    return ((char) crc & 255);
}

static char Sync_Random(void)
{
    unsigned short crc = 0;

    updatecrc(crc, randomseed);
    updatecrc(crc, (randomseed >> 8));

    updatecrc(crc, g_globalRandom);
    updatecrc(crc, (g_globalRandom >> 8));

    updatecrc(crc, g_random.global);
    updatecrc(crc, (g_random.global >> 8));

    updatecrc(crc, g_random.playerweapon);
    updatecrc(crc, (g_random.playerweapon >> 8));

    return ((char) crc & 255);
}

static char Sync_Engine(void)
{
    unsigned short crc = 0;

    updatecrc(crc, Numsprites);
    updatecrc(crc, (Numsprites >> 8));

    updatecrc(crc, tailspritefree);
    updatecrc(crc, (tailspritefree >> 8));

    updatecrc(crc, numwalls);
    updatecrc(crc, (numwalls >> 8));

    updatecrc(crc, numsectors);
    updatecrc(crc, (numsectors >> 8));

    return ((char)crc & 255);
}

// This must not exceed MAXSYNCBYTES
static SyncType_t syncType[] = {
    DEFINE_SYNCFUNC(Sync_Engine),
    DEFINE_SYNCFUNC(Sync_Random),
    DEFINE_SYNCFUNC(Sync_PlayerPos),
    DEFINE_SYNCFUNC(Sync_PlayerData),
    DEFINE_SYNCFUNC(Sync_Projectiles),
    DEFINE_SYNCFUNC(Sync_Actors),
    DEFINE_SYNCFUNC(Sync_Map),
};
#define NUM_SYNC_TYPES (sizeof(syncType) / sizeof(syncType[0]))

void Net_GetSyncStat(void)
{
    playerdata_t *pp = &g_player[myconnectindex];

    if (numplayers < 2)
        return;

    for (size_t i = 0; i < NUM_SYNC_TYPES; i++)
    {
        pp->syncval[pp->syncvalhead & (SYNCFIFOSIZ - 1)][i] = syncType[i].func();
    }

    pp->syncvalhead++;
}

////////////////////////////////////////////////////////////////////////
//
// Sync Message print
//
////////////////////////////////////////////////////////////////////////

int desynched_players[MAXPLAYERS];

void Net_DisplaySyncMsg(void)
{
    static unsigned int moveCount = 0;
    extern unsigned int g_moveThingsCount;

    if (numplayers < 2)
        return;

    for (size_t i = 0; i < NUM_SYNC_TYPES; i++)
    {
        // syncstat is NON 0 - out of sync
        if (syncstat[i] != 0)
        {
            sprintf(tempbuf, "Out Of Sync - %s", syncType[i].name);
            printext256(4, 100 + (i * 8), 31, 1, tempbuf, 0);

            if (!g_foundSyncError && g_szfirstSyncMsg[i][0] == '\0')
            {
                // g_foundSyncError one so test all of them and then never test again
                g_foundSyncError = TRUE;

                // save off loop count
                moveCount = g_moveThingsCount;

                for (size_t j = 0; j < NUM_SYNC_TYPES; j++)
                {
                    if (syncstat[j] != 0 && g_szfirstSyncMsg[j][0] == '\0')
                    {
                        sprintf(tempbuf, "Out Of Sync - %s", syncType[j].name);
                        strcpy(g_szfirstSyncMsg[j], tempbuf);
                    }
                }
            }
        }
    }

    // print out the g_szfirstSyncMsg message you got
    for (size_t i = 0; i < NUM_SYNC_TYPES; i++)
    {
        if (g_szfirstSyncMsg[i][0] != '\0')
        {
            sprintf(tempbuf, "FIRST %s", g_szfirstSyncMsg[i]);
            printext256(4, 44 + (i * 8), 31, 1, tempbuf, 0);
            sprintf(tempbuf, "moveCount %d",moveCount);
            printext256(4, 52 + (i * 8), 31, 1, tempbuf, 0);

            int ypos = 180;
            for (i = 0; i < MAXPLAYERS; i++)
            {
                if (desynched_players[i] == 1)
                {
                    Bsprintf(tempbuf, "DESYNCHED: %s (IDX: %d)", g_player[i].user_name, i);
                    printext256(4, ypos, 31, 0, tempbuf, 0);
                    ypos += 8;
                }
            }
        }
    }
}

void  Net_AddSyncInfoToPacket(int *j)
{
    int count = 0;

    // sync testing
    while (g_player[myconnectindex].syncvalhead != syncvaltail && count++ < 4)
    {
        for (size_t sb = 0; sb < NUM_SYNC_TYPES; sb++)
            packbuf[(*j)++] = g_player[myconnectindex].syncval[syncvaltail & (SYNCFIFOSIZ - 1)][sb];

        syncvaltail++;
    }
}

void Net_GetSyncInfoFromPacket(char *packbuf, int packbufleng, int *j, int otherconnectindex)
{
    int i;
    playerdata_t *ppo = &g_player[otherconnectindex];

    // if ready2send is not set, or player is disconnected then don't try to get sync info
#if 1
    if (!ready2send || !g_player[otherconnectindex].connected)
        return;
#endif

    // Grab sync info from packet buffer
    while ((*j) < packbufleng)
    {
        for (size_t sb = 0; sb < NUM_SYNC_TYPES; sb++)
        {
            ppo->syncval[ppo->syncvalhead & (SYNCFIFOSIZ - 1)][sb] = packbuf[(*j)++];
        }
        ppo->syncvalhead++;
    }

    TRAVERSE_CONNECT(i)
    {
        if (g_player[i].syncvalhead == syncvaltottail)
            return;
    }

    // update syncstat
    // if any of the syncstat vars is non-0 then there is a problem
    while (1)
    {
        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].syncvalhead == syncvaltottail)
                return;

            for (size_t sb = 0; sb < NUM_SYNC_TYPES; sb++)
            {
                char head_sync = g_player[connecthead].syncval[syncvaltottail & (SYNCFIFOSIZ - 1)][sb];
                char player_sync = g_player[i].syncval[syncvaltottail & (SYNCFIFOSIZ - 1)][sb];
                if (player_sync != head_sync)
                {
                    if (!syncstat[sb])
                    {
                        OSD_Printf("Desynchronized! Player: %d, Type: %s, Head CRC: %d (syncvalhead: %d), Player CRC: %d (syncvalhead: %d). syncvaltottail: %d\n", i, syncType[sb].name, head_sync, g_player[connecthead].syncvalhead, player_sync, g_player[i].syncvalhead, syncvaltottail);
                        syncstat[sb] = 1;
                    }

                    desynched_players[i] = 1;
                }
            }
        }

        syncvaltottail++;
    }
}


