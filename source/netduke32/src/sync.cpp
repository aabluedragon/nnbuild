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

char g_szfirstSyncMsg[MAX_SYNC_TYPES][60];

static int crctable[256];
#define updatecrc(dcrc,xz) (dcrc = (crctable[((dcrc)>>8)^((xz)&255)]^((dcrc)<<8)))

int8_t syncData[MOVEFIFOSIZ][MAX_SYNC_TYPES];
bool syncError[MAX_SYNC_TYPES];
bool g_foundSyncError = false;

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
    unsigned short crc = 0;

    for (int32_t ALL_PLAYERS(i))
    {
        auto pp = g_player[i].ps;
        updatecrc(crc, pp->pos.x);
        updatecrc(crc, pp->pos.y);
        updatecrc(crc, pp->pos.z);
        updatecrc(crc, pp->q16ang);
        updatecrc(crc, pp->q16horiz);
        updatecrc(crc, sprite[pp->i].x);
        updatecrc(crc, sprite[pp->i].y);
        updatecrc(crc, sprite[pp->i].z);
        updatecrc(crc, sprite[pp->i].ang);
    }

    return ((char) crc & 255);
}

static char Sync_PlayerData(void)
{
    unsigned short crc = 0;

    for(int32_t ALL_PLAYERS(i))
    {
        auto pp = g_player[i].ps;
        updatecrc(crc, pp->curr_weapon);
        updatecrc(crc, pp->kickback_pic);
        updatecrc(crc, pp->bobcounter);
        updatecrc(crc, pp->jetpack_on);
        updatecrc(crc, sprite[pp->i].extra);
    }

    return ((char) crc & 255);
}

static char Sync_Actors(void)
{
    unsigned short crc = 0;
    int j;

    for (SPRITES_OF(STAT_ACTOR, j))
    {
        spritetype* spr = &sprite[j];
        updatecrc(crc, spr->x);
        updatecrc(crc, spr->y);
        updatecrc(crc, spr->z);
        updatecrc(crc, spr->lotag);
        updatecrc(crc, spr->hitag);
        updatecrc(crc, spr->ang);
        updatecrc(crc, spr->owner);
    }

    for (SPRITES_OF(STAT_ZOMBIEACTOR, j))
    {
        spritetype* spr = &sprite[j];
        updatecrc(crc, spr->x);
        updatecrc(crc, spr->y);
        updatecrc(crc, spr->z);
        updatecrc(crc, spr->lotag);
        updatecrc(crc, spr->hitag);
        updatecrc(crc, spr->ang);
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
        updatecrc(crc, spr->x);
        updatecrc(crc, spr->y);
        updatecrc(crc, spr->z);
        updatecrc(crc, spr->ang);
    }

    return ((char) crc & 255);
}

static char Sync_Map(void)
{
    unsigned short crc = 0;
    int j;

    for (SPRITES_OF(STAT_EFFECTOR, j))
    {
        spritetype* spr = &sprite[j];
        updatecrc(crc, spr->x);
        updatecrc(crc, spr->y);
        updatecrc(crc, spr->z);
        updatecrc(crc, spr->ang);
        updatecrc(crc, spr->lotag);
        updatecrc(crc, spr->hitag);
        updatecrc(crc, spr->owner);
        updatecrc(crc, spr->sectnum);
    }

    for (j = 0; j < numwalls; j++)
    {
        walltype* wal = &wall[j];
        updatecrc(crc, wal->x);
        updatecrc(crc, wal->y);
    }

    for (j = 0; j < numsectors; j++)
    {
        sectortype* sect = &sector[j];
        updatecrc(crc, sect->floorz);
        updatecrc(crc, sect->ceilingz);
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

#if 0
static char Sync_GameSettings(void)
{
    unsigned short crc = 0;

    updatecrc(crc, ud.dmflags);
    updatecrc(crc, (ud.dmflags >> 8));
    updatecrc(crc, ud.limit_hit);

    return ((char)crc & 255);
}
#endif

#if BOT_DEBUG == 1
static char Sync_Bots(void)
{
    unsigned short crc = 0;

    for (int32_t ALL_PLAYERS(i))
    {
        updatecrc(crc, botAI[i].cycleCount);
        updatecrc(crc, botAI[i].moveCount);
        updatecrc(crc, botAI[i].goalPos.x);
        updatecrc(crc, botAI[i].goalPos.y);
        updatecrc(crc, botAI[i].goalPos.z);
        updatecrc(crc, botAI[i].goalSprite);
        updatecrc(crc, botAI[i].goalWall);
        updatecrc(crc, botAI[i].rand);
    }

    return ((char)crc & 255);
}
#endif

// This must not exceed MAX_SYNC_TYPES
static SyncType_t syncType[] = {
    DEFINE_SYNCFUNC(Sync_Engine),
    DEFINE_SYNCFUNC(Sync_Random),
    DEFINE_SYNCFUNC(Sync_PlayerPos),
    DEFINE_SYNCFUNC(Sync_PlayerData),
    DEFINE_SYNCFUNC(Sync_Projectiles),
    DEFINE_SYNCFUNC(Sync_Actors),
    DEFINE_SYNCFUNC(Sync_Map),
#if 0
    DEFINE_SYNCFUNC(Sync_GameSettings),
#endif
#if BOT_DEBUG == 1
    DEFINE_SYNCFUNC(Sync_Bots),
#endif
};
#define NUM_SYNC_TYPES (int32_t)(sizeof(syncType) / sizeof(syncType[0]))

void Net_GetSyncStat(void)
{
    if (numplayers < 2)
        return;

    for (int32_t i = 0; i < NUM_SYNC_TYPES; i++)
        syncData[INPUTFIFO_CURTICK][i] = syncType[i].func();

    // Set NETDUKE_SYNCDUMP to write per-tick state to syncdump.txt for
    // diffing runs against each other when chasing desyncs.
    static FILE *syncDumpFile;
    static bool syncDumpChecked;
    if (!syncDumpChecked)
    {
        syncDumpChecked = true;
        if (getenv("NETDUKE_SYNCDUMP"))
            syncDumpFile = fopen("syncdump.txt", "w");
    }
#if KRANDDEBUG
    // With NETDUKE_KRD set (requires a KRANDDEBUG=1 build), record a backtrace
    // of every krand() call between ticks 8 and 20 and dump them to krd.txt.
    if (getenv("NETDUKE_KRD"))
    {
        extern unsigned int g_moveThingsCount;
        extern void krd_enable(int which);
        extern int32_t krd_print(const char *filename);
        if (g_moveThingsCount == 8)
            krd_enable(1);
        else if (g_moveThingsCount == 20)
        {
            LOG_F(INFO, "krd anchor: Net_GetSyncStat at %p", (void *)&Net_GetSyncStat);
            krd_print("krd.txt");
        }
    }
#endif
    if (syncDumpFile)
    {
        extern unsigned int g_moveThingsCount;
        uint32_t acrc = 2166136261u;
        auto fnv = [&acrc](int32_t v) {
            for (int b = 0; b < 4; b++, v >>= 8)
                acrc = (acrc ^ (uint8_t)v) * 16777619u;
        };
        for (int32_t i = 0; i < MAXSPRITES; i++)
        {
            // STAT_MISC/STAT_FX sprites hold viewer-local state (ambience, eyecandy).
            if (sprite[i].statnum == MAXSTATUS || sprite[i].statnum == STAT_MISC || sprite[i].statnum == STAT_FX)
                continue;
            fnv(i); fnv(sprite[i].x); fnv(sprite[i].y); fnv(sprite[i].z);
            fnv(sprite[i].ang); fnv(sprite[i].extra); fnv(sprite[i].xvel);
            fnv(sprite[i].yvel); fnv(sprite[i].zvel); fnv(sprite[i].lotag);
            fnv(sprite[i].hitag); fnv(sprite[i].owner); fnv(sprite[i].statnum);
            fnv(actor[i].timetosleep);
            for (int t = 0; t < 10; t++)
                if (t != 1 && t != 4 && t != 5)  // T2/T5/T6 hold script pointers
                    fnv(actor[i].t_data[t]);
        }
        extern int mymaxlag, otherminlag, bufferjitter;
        fprintf(syncDumpFile, "%u seed=%08x ws=%08x grand=%08x wg=%04x wp=%04x ns=%d acrc=%08x bj=%d oml=%d",
                g_moveThingsCount, (unsigned)randomseed, (unsigned)wrandomseed,
                (unsigned)g_globalRandom, (unsigned)g_random.global,
                (unsigned)g_random.playerweapon, Numsprites, acrc, bufferjitter, otherminlag);
        for (int32_t ALL_PLAYERS(i))
            fprintf(syncDumpFile, " fd%d=%d", i, g_player[i].movefifoend - movefifoplc);
        for (int32_t ALL_PLAYERS(i))
        {
            auto pp = g_player[i].ps;
            fprintf(syncDumpFile, " p%d=%d,%d,%d,%x,%d,o=%d,%d,%d,cs=%d", i, (int32_t)pp->pos.x,
                    (int32_t)pp->pos.y, (int32_t)pp->pos.z, (unsigned)pp->q16ang,
                    (int32_t)sprite[pp->i].extra, (int32_t)pp->opos.x, (int32_t)pp->opos.y,
                    (int32_t)pp->opos.z, (int32_t)pp->cursectnum);
        }
        fputc('\n', syncDumpFile);
        fflush(syncDumpFile);

        // NETDUKE_STATEDUMP=<tick>: write per-sprite sim state for every tick
        // up to <tick>, one statedump_NNN.txt file each.
        static int32_t stateDumpTick = -2;
        if (stateDumpTick == -2)
        {
            char const *e = getenv("NETDUKE_STATEDUMP");
            stateDumpTick = e ? atoi(e) : -1;
        }
        if (stateDumpTick > 0 && (int32_t)g_moveThingsCount <= stateDumpTick)
        {
            char sfn[32];
            snprintf(sfn, sizeof(sfn), "statedump_%03u.txt", g_moveThingsCount);
            FILE *sf = fopen(sfn, "w");
            if (sf)
            {
                for (int32_t i = 0; i < MAXSPRITES; i++)
                {
                    if (sprite[i].statnum == MAXSTATUS)
                        continue;
                    fprintf(sf, "%d st=%d pic=%d %d,%d,%d a=%d e=%d v=%d,%d,%d lt=%d ht=%d ow=%d tts=%d td=",
                            i, (int32_t)sprite[i].statnum, (int32_t)sprite[i].picnum,
                            (int32_t)sprite[i].x, (int32_t)sprite[i].y, (int32_t)sprite[i].z,
                            (int32_t)sprite[i].ang, (int32_t)sprite[i].extra,
                            (int32_t)sprite[i].xvel, (int32_t)sprite[i].yvel,
                            (int32_t)sprite[i].zvel, (int32_t)sprite[i].lotag,
                            (int32_t)sprite[i].hitag, (int32_t)sprite[i].owner,
                            (int32_t)actor[i].timetosleep);
                    for (int t = 0; t < 10; t++)
                        fprintf(sf, "%d%c", (int32_t)actor[i].t_data[t], t == 9 ? '\n' : ',');
                }
                fclose(sf);
            }
        }
    }
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

    for (int32_t i = 0; i < NUM_SYNC_TYPES; i++)
    {
        // syncError is NON 0 - out of sync
        if (syncError[i])
        {
            sprintf(tempbuf, "Out Of Sync - %s", syncType[i].name);
            printext256(4, 100 + (i * 8), 31, 1, tempbuf, 0);

            if (!g_foundSyncError && g_szfirstSyncMsg[i][0] == '\0')
            {
                S_PlaySound(DUKE_KILLED2);

                // g_foundSyncError one so test all of them and then never test again
                g_foundSyncError = true;

                // save off loop count
                moveCount = g_moveThingsCount;

                for (int32_t j = 0; j < NUM_SYNC_TYPES; j++)
                {
                    if (syncError[j] && g_szfirstSyncMsg[j][0] == '\0')
                    {
                        sprintf(tempbuf, "Out Of Sync - %s", syncType[j].name);
                        strcpy(g_szfirstSyncMsg[j], tempbuf);
                        LOG_F(ERROR, "%s (moveCount %u)", tempbuf, g_moveThingsCount);
                    }
                }
            }
        }
    }

    // print out the g_szfirstSyncMsg message you got
    for (int32_t i = 0; i < NUM_SYNC_TYPES; i++)
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

void Net_AddSyncInfoToPacket(int *j)
{
    auto p = &g_player[myconnectindex];
    int32_t const tickNum = (myconnectindex == connecthead) ? movefifoplc-1 : movefifoplc;
    
    if(tickNum == p->lastSyncTick || g_gameQuit) // Already sent this one, or disconnecting.
    {
        B_BUF32(&packbuf[(*j)], -1);
        (*j) += sizeof(int32_t);
        return;
    }

    p->lastSyncTick = tickNum;
    B_BUF32(&packbuf[(*j)], tickNum);
    (*j) += sizeof(int32_t);

    for (int32_t sb = 0; sb < NUM_SYNC_TYPES; sb++)
    {
        packbuf[(*j)++] = syncData[tickNum & (MOVEFIFOSIZ-1)][sb];
    }
}

void Net_GetSyncInfoFromPacket(char *packbuf, int *j, int otherconnectindex)
{
    // if ready2send is not set, or player is disconnected then don't try to get sync info
#if 1
    if (!ready2send || !g_player[otherconnectindex].connected || g_gameQuit)
        return;
#endif

    int32_t const tickNum = (int32_t)B_UNBUF32(&packbuf[(*j)]);
    (*j) += sizeof(int32_t);

    if (tickNum < 0 || tickNum == g_player[otherconnectindex].lastSyncTick || (tickNum > movefifoplc))
        return;

    g_player[otherconnectindex].lastSyncTick = tickNum;

    // Grab sync info from packet buffer
    for (int32_t sb = 0; sb < NUM_SYNC_TYPES; sb++)
    {
        char head_sync = syncData[tickNum & (MOVEFIFOSIZ - 1)][sb];
        char player_sync = packbuf[(*j)++];

        if (player_sync != head_sync)
        {
            if (!syncError[sb])
            {
                OSD_Printf("Desynchronized! Player: %d, Type: %s, Head CRC: %d, Player CRC: %d, TickNum: %d, MoveFifoPlc: %d\n", otherconnectindex, syncType[sb].name, head_sync, player_sync, tickNum, movefifoplc);
                syncError[sb] = true;
            }

            desynched_players[otherconnectindex] = 1;
        }
    }
}


