#define OLDNET_CPP_

#include "duke3d.h"
#include "oldnet.h"
#include "net_predict.h"

#define TIMERUPDATESIZ 32

int netQuitSend  = 0;
int quittimer = 0;
int lastpackettime = 0;
int mymaxlag, otherminlag, bufferjitter = 1;

int movefifosendplc;
int movefifoplc;

// Lockstep lag handling (Warcraft 3 style): when the master stalls waiting
// for a slow player, open a countdown; if it expires (or a majority votes to
// drop), the laggard is removed instead of hanging every machine forever.
#define LAGWAIT_COUNTDOWN_SEC 30
static int32_t lagWaitActive;      // Master: a stall dialog is currently open.
static uint32_t lagWaitDeadline;   // timerGetTicks() value when the countdown hits 0.
static uint16_t lagStalledMask;    // Which slots we are stalled on.
static uint16_t lagDropVoteMask;   // Which slots have been voted to drop.

static int32_t lateJoinPending = -1;
static int32_t lateJoinGoSlot = -1;
static uint16_t lateJoinGoMask;
static int32_t lateJoinGoRNG[6];
static void Net_IntegrateLateJoin(int32_t slot);
static void Net_CompleteLateJoin(int32_t slot);
static void Net_DropLaggard(int32_t slot);
static char *snapshotBuf;
static int32_t snapshotChunksGot, snapshotChunkTotal, snapshotLastLen;
#define SNAPSHOT_CHUNK_SIZE 1024

static int g_chatPlayer = -1;
static char recbuf[180];

extern char *rtsptr; // game.c

int pingTime;

static void Net_AddPingTimeToPacket(int* j)
{
    B_BUF32(&packbuf[(*j)], pingTime);
    (*j) += sizeof(int32_t);
}

static void Net_GetPingTimeFromPacket(int* j)
{
    pingTime = (int32_t)B_UNBUF32(&packbuf[(*j)]);
    (*j) += sizeof(int32_t);
}

static void Net_AddPlayerPingToPacket(int* j, int playerNum)
{
    B_BUF32(&packbuf[(*j)], g_player[playerNum].ping);
    (*j) += sizeof(int32_t);
}

static void Net_GetPlayerPingFromPacket(int* j, int playerNum)
{
    g_player[playerNum].ping = (uint32_t)B_UNBUF32(&packbuf[(*j)]);
    (*j) += sizeof(int32_t);
}

static void Net_SendPing(void)
{
    if (myconnectindex != connecthead)
        return;

    static uint32_t lastPingTime;

    uint32_t tics = timerGetTicks();
    if (tics - lastPingTime >= 1000)
    {
        packbuf[0] = PACKET_TYPE_PING;
        int j = 1;

        lastPingTime = pingTime = tics;
        Net_AddPingTimeToPacket(&j);

        int i;
        TRAVERSE_CONNECT(i)
        {
            Net_AddPlayerPingToPacket(&j, i);
        }

        TRAVERSE_CONNECT(i)
        {
            if (i != myconnectindex)
                mmulti_sendpacket(i, (unsigned char*)packbuf, j);
        }
    }
}

void Net_GetPackets(void)
{
#if 1
    if ((totalclock > quittimer) && (g_gameQuit == 1))
    {
        OSD_Printf("Timeout: Didn't get quit packet in time, exiting.\n");
        G_GameExit(" ");
    }
#endif

    ChatPipe_Poll();

    // Joiner: act on the GO once init is done and the engine can load/draw.
    if (lateJoinGoSlot == myconnectindex && in3dmode() && g_netLateJoinerReady)
    {
        int32_t const slot = lateJoinGoSlot;
        lateJoinGoSlot = -1;
        Net_ApplyConnectedMask(lateJoinGoMask);
        Net_CompleteLateJoin(slot);
    }

    if (numplayers < 2 || g_networkBroadcastMode == NETMODE_OFFLINE)
        return;

    Net_SendPing();
    Net_ParsePackets();
}

void Net_AddPlayerInputToPacket(int* j, int playerNum, input_t* osyn, input_t* nsyn)
{
    int inputFlagsPos = (*j)++;
    int extFlagsPos = (*j)++;

    packbuf[inputFlagsPos] = 0;
    packbuf[extFlagsPos] = 0;

    if (nsyn[playerNum].fvel != osyn[playerNum].fvel)
    {
        B_BUF16(&packbuf[(*j)], nsyn[playerNum].fvel);
        (*j) += sizeof(int16_t);
        packbuf[inputFlagsPos] |= 1;
    }
    if (nsyn[playerNum].svel != osyn[playerNum].svel)
    {
        B_BUF16(&packbuf[(*j)], nsyn[playerNum].svel);
        (*j) += sizeof(int16_t);
        packbuf[inputFlagsPos] |= 2;
    }
    if (nsyn[playerNum].q16avel != osyn[playerNum].q16avel)
    {
        B_BUF32(&packbuf[(*j)], nsyn[playerNum].q16avel);
        (*j) += sizeof(fix16_t);
        packbuf[inputFlagsPos] |= 4;
    }
    if (nsyn[playerNum].q16horz != osyn[playerNum].q16horz)
    {
        B_BUF32(&packbuf[(*j)], nsyn[playerNum].q16horz);
        (*j) += sizeof(fix16_t);
        packbuf[inputFlagsPos] |= 8;
    }

    if ((nsyn[playerNum].bits ^ osyn[playerNum].bits) & 0x000000ff) packbuf[(*j)++] = (nsyn[playerNum].bits & 255), packbuf[inputFlagsPos] |= 16;
    if ((nsyn[playerNum].bits ^ osyn[playerNum].bits) & 0x0000ff00) packbuf[(*j)++] = ((nsyn[playerNum].bits >> 8) & 255), packbuf[inputFlagsPos] |= 32;
    if ((nsyn[playerNum].bits ^ osyn[playerNum].bits) & 0x00ff0000) packbuf[(*j)++] = ((nsyn[playerNum].bits >> 16) & 255), packbuf[inputFlagsPos] |= 64;
    if ((nsyn[playerNum].bits ^ osyn[playerNum].bits) & 0xff000000) packbuf[(*j)++] = ((nsyn[playerNum].bits >> 24) & 255), packbuf[inputFlagsPos] |= 128;

    if ((nsyn[playerNum].extbits ^ osyn[playerNum].extbits) & 0x000000ff) packbuf[(*j)++] = (nsyn[playerNum].extbits & 255), packbuf[extFlagsPos] |= 1;
    if ((nsyn[playerNum].extbits ^ osyn[playerNum].extbits) & 0x0000ff00) packbuf[(*j)++] = ((nsyn[playerNum].extbits >> 8) & 255), packbuf[extFlagsPos] |= 2;
    if ((nsyn[playerNum].extbits ^ osyn[playerNum].extbits) & 0x00ff0000) packbuf[(*j)++] = ((nsyn[playerNum].extbits >> 16) & 255), packbuf[extFlagsPos] |= 4;
    if ((nsyn[playerNum].extbits ^ osyn[playerNum].extbits) & 0xff000000) packbuf[(*j)++] = ((nsyn[playerNum].extbits >> 24) & 255), packbuf[extFlagsPos] |= 8;
}

void Net_GetPlayerInputFromPacket(int* j, int playerNum, input_t* osyn, input_t* nsyn)
{
    char inputFlags = packbuf[(*j)++];
    char extFlags = packbuf[(*j)++];

    if (playerNum == myconnectindex)
    {
        if (inputFlags & 1) (*j) += sizeof(int16_t);
        if (inputFlags & 2) (*j) += sizeof(int16_t);
        if (inputFlags & 4) (*j) += sizeof(fix16_t);
        if (inputFlags & 8) (*j) += sizeof(fix16_t);

        if (inputFlags & 16) (*j)++;
        if (inputFlags & 32) (*j)++;
        if (inputFlags & 64) (*j)++;
        if (inputFlags & 128) (*j)++;

        if (extFlags & 1) (*j)++;
        if (extFlags & 2) (*j)++;
        if (extFlags & 4) (*j)++;
        if (extFlags & 8) (*j)++;

        return;
    }

    nsyn[playerNum] = osyn[playerNum];

    if (inputFlags & 1)
    {
        nsyn[playerNum].fvel = (short)B_UNBUF16(&packbuf[(*j)]);
        (*j) += sizeof(int16_t);
    }
    if (inputFlags & 2)
    {
        nsyn[playerNum].svel = (short)B_UNBUF16(&packbuf[(*j)]);
        (*j) += sizeof(int16_t);
    }
    if (inputFlags & 4)
    {
        nsyn[playerNum].q16avel = (fix16_t)B_UNBUF32(&packbuf[(*j)]);
        (*j) += sizeof(fix16_t);
    }
    if (inputFlags & 8)
    {
        nsyn[playerNum].q16horz = (fix16_t)B_UNBUF32(&packbuf[(*j)]);
        (*j) += sizeof(fix16_t);
    }

    if (inputFlags & 16)   nsyn[playerNum].bits = ((nsyn[playerNum].bits & 0xffffff00) | ((int)packbuf[(*j)++]));
    if (inputFlags & 32)   nsyn[playerNum].bits = ((nsyn[playerNum].bits & 0xffff00ff) | ((int)packbuf[(*j)++]) << 8);
    if (inputFlags & 64)   nsyn[playerNum].bits = ((nsyn[playerNum].bits & 0xff00ffff) | ((int)packbuf[(*j)++]) << 16);
    if (inputFlags & 128)  nsyn[playerNum].bits = ((nsyn[playerNum].bits & 0x00ffffff) | ((int)packbuf[(*j)++]) << 24);

    if (extFlags & 1)      nsyn[playerNum].extbits = ((nsyn[playerNum].extbits & 0xffffff00) | ((int)packbuf[(*j)++]));
    if (extFlags & 2)      nsyn[playerNum].extbits = ((nsyn[playerNum].extbits & 0xffff00ff) | ((int)packbuf[(*j)++]) << 8);
    if (extFlags & 4)      nsyn[playerNum].extbits = ((nsyn[playerNum].extbits & 0xff00ffff) | ((int)packbuf[(*j)++]) << 16);
    if (extFlags & 8)      nsyn[playerNum].extbits = ((nsyn[playerNum].extbits & 0x00ffffff) | ((int)packbuf[(*j)++]) << 24);

    g_player[playerNum].movefifoend++;

    if (TEST_SYNC_KEY(nsyn[playerNum].bits, SK_GAMEQUIT) &&
        (playerNum == connecthead) && 
        (playerNum != myconnectindex))
    {
        G_GameExit("Host has terminated the game.");
    }
}

// Count connected players that are NOT in the stalled set - the electorate
// that can vote a laggard out.
static int32_t Net_CountVoters(uint16_t stalledMask)
{
    int32_t n = 0;
    for (int32_t i = 0; i < numplayers; i++)
        if (g_player[i].connected && !((stalledMask >> i) & 1) && i != myconnectindex)
            n++;
    return n;
}

static void Net_BroadcastLagState(void)
{
    packbuf[0] = PACKET_TYPE_LAG_NOTIFY;
    B_BUF16(&packbuf[1], lagStalledMask);
    B_BUF16(&packbuf[3], (uint16_t)g_netLagSecsLeft);
    B_BUF16(&packbuf[5], lagDropVoteMask);
    for (int32_t i = 0; i < numplayers; i++)
        if (i != myconnectindex && g_player[i].connected)
            mmulti_sendpacket(i, (unsigned char *)packbuf, 7);

    g_netLagActive = 1;
    g_netLagMask = lagStalledMask;
    g_netLagVoteMask = lagDropVoteMask;
}

// Master, every frame (from the main loop, independent of the simulation
// clock): detect a lockstep stall, run the WC3-style countdown dialog, and
// drop a laggard on timeout or majority vote so the game never hangs.
void Net_LagUpdate(void)
{
    if (g_netAsyncMode || myconnectindex != connecthead || numplayers < 2)
        return;

    if (!(ud.gm & MODE_GAME) || ud.recstat == 2 || g_lateJoinFreeze)
        return;

    uint32_t const now = timerGetTicks();

    // Track when the broadcast tick last advanced; a stall only counts once
    // the master has been unable to progress for a short grace period, so
    // ordinary network jitter never flickers the dialog.
    static int32_t lastSendPlc = -1;
    static uint32_t lastAdvanceTime;
    if (movefifosendplc != lastSendPlc)
    {
        lastSendPlc = movefifosendplc;
        lastAdvanceTime = now;
    }

    // Are we waiting on anyone for the tick we're trying to broadcast?
    int i;
    uint16_t stalled = 0;
    TRAVERSE_CONNECT(i)
        if (i != myconnectindex && g_player[i].movefifoend <= movefifosendplc)
            stalled |= (1 << i);

    if (stalled && !lagWaitActive && (now - lastAdvanceTime) < 500)
        return;  // Within the grace period; not a real stall yet.

    if (!stalled)
    {
        if (lagWaitActive)
        {
            lagWaitActive = 0;
            lagStalledMask = 0;
            lagDropVoteMask = 0;
            g_netLagActive = 0;
            g_netLagMask = 0;
            packbuf[0] = PACKET_TYPE_LAG_CLEAR;
            for (int32_t i = 0; i < numplayers; i++)
                if (i != myconnectindex && g_player[i].connected)
                    mmulti_sendpacket(i, (unsigned char *)packbuf, 1);
        }
        return;
    }

    if (!lagWaitActive)
    {
        int32_t secs = LAGWAIT_COUNTDOWN_SEC;
        char const *e = getenv("NETDUKE_LAGSECS");  // test override
        if (e) secs = atoi(e);

        lagWaitActive = 1;
        lagDropVoteMask = 0;
        lagWaitDeadline = now + (uint32_t)secs * 1000u;
        LOG_F(INFO, "Lockstep stall: waiting for player(s) mask 0x%x, %d second countdown", stalled, secs);
    }

    lagStalledMask = stalled;
    g_netLagSecsLeft = max(0, (int32_t)(lagWaitDeadline - now) / 1000);

    // Tally votes; drop on timeout or majority of eligible (non-stalled) voters.
    int32_t const voters = Net_CountVoters(lagStalledMask);
    int32_t votes = 0;
    for (int32_t v = 0; v < numplayers; v++)
        if ((lagDropVoteMask >> v) & 1)
            votes++;
    bool const timedOut = (int32_t)(lagWaitDeadline - now) <= 0;

    if (timedOut || (voters > 0 && votes * 2 >= voters))
    {
        for (int32_t i = 0; i < numplayers; i++)
            if ((lagStalledMask >> i) & 1)
            {
                Net_DropLaggard(i);
                return;  // Handle one per frame; a second laggard re-opens next frame.
            }
    }

    // Refresh the shared dialog roughly twice a second.
    static uint32_t lastBroadcast;
    if (now - lastBroadcast >= 400)
    {
        lastBroadcast = now;
        Net_BroadcastLagState();
    }
}

// Master: drop a laggard. Substitute its input for the pending tick so this
// frame completes identically everywhere, mark it disconnected, and broadcast
// the new roster; slaves apply the same removal from the packet.
static void Net_DropLaggard(int32_t slot)
{
    LOG_F(INFO, "Dropping lagging player %d (%s)", slot, g_player[slot].user_name);

    while (g_player[slot].movefifoend <= movefifosendplc)
    {
        int32_t const e = g_player[slot].movefifoend;
        inputfifo[e & (MOVEFIFOSIZ-1)][slot] = inputfifo[(e-1) & (MOVEFIFOSIZ-1)][slot];
        g_player[slot].movefifoend++;
    }

    g_player[slot].connected = 0;
    Net_RebuildConnectList();

    packbuf[0] = PACKET_TYPE_DROP_PLAYER;
    packbuf[1] = (char)slot;
    B_BUF16(&packbuf[2], Net_ConnectedMask());
    for (int32_t i = 0; i < numplayers; i++)
        if (i != myconnectindex && g_player[i].connected)
            mmulti_sendpacket(i, (unsigned char *)packbuf, 4);

    P_QuickKill(g_player[slot].ps);
    Bsprintf(buf, "%s^00 dropped (lag)", g_player[slot].user_name);
    G_AddUserQuote(buf);

    lagWaitActive = 0;
    lagStalledMask = 0;
    lagDropVoteMask = 0;
    g_netLagActive = 0;
    g_netLagMask = 0;

    packbuf[0] = PACKET_TYPE_LAG_CLEAR;
    for (int32_t i = 0; i < numplayers; i++)
        if (i != myconnectindex && g_player[i].connected)
            mmulti_sendpacket(i, (unsigned char *)packbuf, 1);
}

// A non-laggard presses the vote key: tell the master (or, on the master,
// record it directly).
void Net_LagVote(void)
{
    if (!g_netLagActive)
        return;

    if (myconnectindex == connecthead)
    {
        lagDropVoteMask |= (1 << myconnectindex);
    }
    else
    {
        packbuf[0] = PACKET_TYPE_LAG_VOTE;
        packbuf[1] = (char)myconnectindex;
        mmulti_sendpacket(connecthead, (unsigned char *)packbuf, 2);
    }

    g_netLagVoteMask |= (1 << myconnectindex);
}

// Drawn every frame from G_DrawFrame. While a lockstep stall is open this
// shows the Warcraft 3-style countdown and lets other players vote the
// laggard out with F9.
void Net_DisplayLagDialog(void)
{
    if (!g_netLagActive)
        return;

    int const meLagging = (g_netLagMask >> myconnectindex) & 1;

    int y = 60;
    gametext(160, y, "WAITING FOR PLAYERS", 14, 2); y += 12;

    for (int32_t i = 0; i < numplayers; i++)
    {
        if (!((g_netLagMask >> i) & 1))
            continue;
        Bsprintf(tempbuf, "%s is lagging", g_player[i].user_name);
        gametext(160, y, tempbuf, 14, 2);
        y += 8;
    }

    Bsprintf(tempbuf, "Continuing in %d...", g_netLagSecsLeft);
    gametext(160, y, tempbuf, 14, 2); y += 12;

    if (!meLagging)
    {
        int const voted = (g_netLagVoteMask >> myconnectindex) & 1;
        gametext(160, y, voted ? "Vote to drop cast" : "Press F9 to vote to drop", 14, 2);

        if (!voted && KB_UnBoundKeyPressed(sc_F9))
        {
            KB_ClearKeyDown(sc_F9);
            Net_LagVote();
        }
    }
    else
        gametext(160, y, "Reconnect or you will be dropped", 14, 2);
}

void faketimerhandler(void) { };

void Net_HandleInput(void)
{
    int i, j;
    //    short who;
    input_t *osyn, *nsyn;

    if (quickExit == 0 && KB_KeyPressed(sc_LeftControl) && KB_KeyPressed(sc_LeftAlt) && KB_KeyPressed(sc_Delete))
    {
        quickExit = 1;
        G_GameExit("Quick Exit.");
    }

    Net_GetPackets();

    // A late-join state transfer is in progress: stop generating and
    // aggregating input so every machine settles on the same tick.
    if (g_lateJoinFreeze)
        return;

    if (g_player[myconnectindex].movefifoend - movefifoplc >= 100)
        return;

    // Put our local input into the FIFO to be processed by P_ProcessInput and such.
    nsyn = &inputfifo[g_player[myconnectindex].movefifoend&(MOVEFIFOSIZ-1)][myconnectindex];
    *nsyn = netInput;

    g_player[myconnectindex].movefifoend++;

#if 0
    if (ud.multimode > 1 && (myconnectindex == connecthead))
    {
        for (ALL_PLAYERS(i))
        {
            if (g_player[i].isBot)
            {
                nsyn = &inputfifo[g_player[i].movefifoend & (MOVEFIFOSIZ - 1)][i];
                DukeBot_GetInput(i, nsyn);
                g_player[i].movefifoend++;
            }
        }
    }
#endif

    if (numplayers < 2)
        return;

    TRAVERSE_CONNECT(i)
    if (i != myconnectindex)
    {
        int32_t lag = (g_player[myconnectindex].movefifoend-1)-g_player[i].movefifoend;
        g_player[i].myminlag = min(g_player[i].myminlag, lag);
        mymaxlag = max(mymaxlag, lag);
    }

    if (((g_player[myconnectindex].movefifoend - 1) & (TIMERUPDATESIZ - 1)) == 0)
    {
        i = mymaxlag - bufferjitter;
        mymaxlag = 0;
        if (i > 0)
        {
            // Grow towards the observed jitter, but never leap: a single
            // hitch (level load, window drag) used to add (2+i)>>2 ticks in
            // one step - a 100-tick stall meant a full second of input lag
            // that stuck around near-permanently.
            bufferjitter += min(((2 + i) >> 2), 2);
        }
        else if (i < 0)
        {
            // The old decay ((2 - i) >> 2) rounds to zero for small
            // overshoots, so bufferjitter could never drop the last couple
            // of ticks back to the real jitter floor. Always step down.
            bufferjitter -= max(((2 - i) >> 2), 1);
        }

        bufferjitter = clamp(bufferjitter, 0, 10);
    }

    // Master/Slave
    if (myconnectindex != connecthead)   //Slave
    {
        //Fix timers and buffer/jitter value
        if (((g_player[myconnectindex].movefifoend-1)&(TIMERUPDATESIZ-1)) == 0)
        {
            // [JM] I wish this wasn't so fucking cryptic.
            i = g_player[connecthead].myminlag - otherminlag;
            if (klabs(i) > 2)
            {
                if (klabs(i) > 8)
                {
                    if (i < 0)
                        i++;
                    i >>= 1;
                }
                else
                {
                    if (i < 0)
                        i = -1;
                    if (i > 0)
                        i = 1;
                }
                totalclock -= TICSPERFRAME * i;
                otherminlag += i;
            }

            TRAVERSE_CONNECT(i)
            {
                g_player[i].myminlag = 0x7fffffff;
            }
        }

        packbuf[0] = PACKET_TYPE_SLAVE_TO_MASTER;
        j = 1;

        osyn = (input_t *)&inputfifo[(g_player[myconnectindex].movefifoend-2)&(MOVEFIFOSIZ-1)][myconnectindex];
        nsyn = (input_t *)&inputfifo[(g_player[myconnectindex].movefifoend-1)&(MOVEFIFOSIZ-1)][myconnectindex];

        Net_AddPlayerInputToPacket(&j, 0, osyn, nsyn);
        Net_AddSyncInfoToPacket(&j);

        mmulti_sendpacket(connecthead, (unsigned char*)packbuf,j);
        return;
    }

    while (1)  //Master
    {
        if (g_netAsyncMode)
        {
            // Asynchronous (server-authoritative) mode: never wait for slow
            // clients. Tick at the pace of our own input, substituting each
            // absent player's last known input; their real inputs are applied
            // to later ticks when they arrive.
            if (g_player[myconnectindex].movefifoend <= movefifosendplc)
                return;

            TRAVERSE_CONNECT(i)
            {
                while (g_player[i].movefifoend <= movefifosendplc)
                {
                    int32_t const e = g_player[i].movefifoend;
                    inputfifo[e & (MOVEFIFOSIZ-1)][i] = inputfifo[(e-1) & (MOVEFIFOSIZ-1)][i];
                    g_player[i].movefifoend++;
                }
            }
        }
        else
        {
            // Lockstep: can't advance until every player's input for this tick
            // has arrived. The lag dialog / drop countdown is driven separately
            // from the main loop (Net_LagUpdate) so it keeps running even while
            // the simulation is fully stalled.
            TRAVERSE_CONNECT(i)
            {
                if (g_player[i].movefifoend <= movefifosendplc)
                    return;
            }
        }

        osyn = (input_t *)&inputfifo[(movefifosendplc-1)&(MOVEFIFOSIZ-1)];
        nsyn = (input_t *)&inputfifo[(movefifosendplc)&(MOVEFIFOSIZ-1)];

        //MASTER -> SLAVE packet
        packbuf[0] = PACKET_TYPE_MASTER_TO_SLAVE;

        j = 2;
        char playerCount = 0;
        TRAVERSE_CONNECT(i)
        {
            packbuf[j++] = i; // Player Index

            //Fix timers and buffer/jitter value 
            packbuf[j++] = min(max(g_player[i].myminlag, -128), 127);
            if ((movefifosendplc & (TIMERUPDATESIZ - 1)) == 0)
                g_player[i].myminlag = 0x7fffffff;

            Net_AddPlayerInputToPacket(&j, i, osyn, nsyn);
            playerCount++;
        }
        packbuf[1] = playerCount;

        Net_AddSyncInfoToPacket(&j); // Must always be at the end of the packet.

        TRAVERSE_CONNECT(i)
            mmulti_sendpacket(i, (unsigned char*)packbuf, j);

        movefifosendplc++;

        // We're the host, terminate the match after sending the packet.
        if (TEST_SYNC_KEY(nsyn[myconnectindex].bits, SK_GAMEQUIT) &&
            (myconnectindex == connecthead))
        {
            G_GameExit(" ");
        }
    }
}

void Net_ParsePackets(void)
{
    int i, j;
    int other;
    int packbufleng;

    input_t *osyn, *nsyn;
    
    if (numplayers < 2 || (g_networkBroadcastMode == NETMODE_OFFLINE))
        return;

    while ((packbufleng = mmulti_getpacket(&other, (unsigned char*)packbuf)) > 0)
    {
#if 0
        LOG_F(INFO, "RECEIVED PACKET: type: %d : len %d", packbuf[0], packbufleng);
#endif
        // If we're a client, reject any packets that don't come from the server.
        if ((myconnectindex != connecthead) && other != connecthead)
            continue;

        if (packbuf[0] >= PACKET_END) // Don't accept anything that isn't in our range of defined packets.
            continue;

        lastpackettime = (int32_t)totalclock;

        switch (packbuf[0])
        {
            // ****** Base master/slave packets ******
            case PACKET_TYPE_MASTER_TO_SLAVE:  //[0] (receive master sync buffer)
            {
                if (myconnectindex == connecthead)
                {
                    OSD_Printf("PACKET_TYPE_MASTER_TO_SLAVE: MASTER SHOULDN'T GET THIS PACKET!\n");
                    continue;
                }

                osyn = (input_t*)&inputfifo[(g_player[connecthead].movefifoend - 1) & (MOVEFIFOSIZ - 1)];
                nsyn = (input_t*)&inputfifo[(g_player[connecthead].movefifoend) & (MOVEFIFOSIZ - 1)];

                char playerCount = packbuf[1];
                j = 2;
                for(int32_t i = 0; i < playerCount; i++)
                {
                    int32_t playerNum = packbuf[j++];

                    //Fix timers and buffer/jitter value 
                    int32_t minlag = (int32_t)((signed char)packbuf[j++]);

                    if (((g_player[other].movefifoend & (TIMERUPDATESIZ - 1)) == 0) && (playerNum == myconnectindex))
                        otherminlag = minlag;

                    Net_GetPlayerInputFromPacket(&j, playerNum, osyn, nsyn);
                }

                Net_GetSyncInfoFromPacket(packbuf, &j, other);

                movefifosendplc++;

                break;
            }
            case PACKET_TYPE_SLAVE_TO_MASTER:  //[1] (receive slave sync buffer)
            {
                j = 1;

                osyn = (input_t *)&inputfifo[(g_player[other].movefifoend-1)&(MOVEFIFOSIZ-1)];
                nsyn = (input_t *)&inputfifo[(g_player[other].movefifoend)&(MOVEFIFOSIZ-1)];

                Net_GetPlayerInputFromPacket(&j, other, osyn, nsyn);
                Net_GetSyncInfoFromPacket(packbuf, &j, other);
                break;
            }

            // ****** Misc packets ******
            case PACKET_TYPE_INIT_SETTINGS:
            {
                if (myconnectindex == connecthead)
                {
                    OSD_Printf("PACKET_TYPE_INIT_SETTINGS: MASTER SHOULDN'T GET THIS PACKET!\n");
                    continue;
                }

                j = 1;
                ud.m_level_number = ud.level_number     = packbuf[j++];
                ud.m_volume_number = ud.volume_number   = packbuf[j++];
                ud.m_player_skill = ud.player_skill     = packbuf[j++];

                // Non-menu variables handled by G_EnterLevel
                ud.m_coop               = packbuf[j++];
                ud.warp_on              = packbuf[j++];
                ud.fraglimit            = packbuf[j++];
                ud.multimode            = packbuf[j++];
                ud.playerai             = packbuf[j++];
                ud.m_dmflags            = (int32_t)B_UNBUF32(&packbuf[j]); j += sizeof(int32_t);
                botNameSeed             = (int32_t)B_UNBUF32(&packbuf[j]); j += sizeof(int32_t);

                Net_ApplyConnectedMask(B_UNBUF16(&packbuf[j])); j += sizeof(uint16_t);
                Net_RebuildConnectList();
                g_netAsyncMode = packbuf[j++];

                if (M_DMFLAGS_TEST(DMFLAG_ALLOWVISIBILITYCHANGE))
                    LOG_F(INFO, "Visibility adjustment enabled for multiplayer.");

                oldnet_gotinitialsettings = true;

                break;
            }
            case PACKET_TYPE_LATEJOIN_ANNOUNCE:
            {
                g_netLateJoiner = 1;
                g_noLogo = 1;  // Straight into the game once the snapshot lands.
                LOG_F(INFO, "Joining a game in progress; waiting for the state snapshot...");
                break;
            }
            case PACKET_TYPE_SNAPSHOT_CHUNK:
            {
                int32_t const seq = B_UNBUF16(&packbuf[1]);
                int32_t const total = B_UNBUF16(&packbuf[3]);
                int32_t const len = B_UNBUF16(&packbuf[5]);

                if (total <= 0 || (unsigned)seq >= (unsigned)total || (unsigned)len > SNAPSHOT_CHUNK_SIZE)
                    break;

                if (snapshotBuf == NULL)
                {
                    snapshotBuf = (char *)Xmalloc(total * SNAPSHOT_CHUNK_SIZE);
                    snapshotChunksGot = 0;
                    snapshotChunkTotal = total;
                }

                Bmemcpy(&snapshotBuf[seq * SNAPSHOT_CHUNK_SIZE], &packbuf[7], len);
                snapshotChunksGot++;
                if (seq == total - 1)
                    snapshotLastLen = len;

                break;
            }
            case PACKET_TYPE_LATEJOIN_GO:
            {
                // Never integrate from inside the packet parser: the joiner
                // waits until its init is done (see Net_GetPackets), and
                // existing peers wait until they have simulated every tick
                // the master broadcast (see Net_PeerCheckLateJoin), so that
                // all machines fold the new player in on the identical tick.
                lateJoinGoSlot = (uint8_t)packbuf[1];
                lateJoinGoMask = B_UNBUF16(&packbuf[2]);
                for (i = 0; i < 6; i++)
                    lateJoinGoRNG[i] = (int32_t)B_UNBUF32(&packbuf[4 + i * 4]);
                break;
            }
            case PACKET_TYPE_LAG_NOTIFY:
            {
                g_netLagActive = 1;
                g_netLagMask = B_UNBUF16(&packbuf[1]);
                g_netLagSecsLeft = (int16_t)B_UNBUF16(&packbuf[3]);
                g_netLagVoteMask = B_UNBUF16(&packbuf[5]);
                break;
            }
            case PACKET_TYPE_LAG_CLEAR:
            {
                g_netLagActive = 0;
                g_netLagMask = 0;
                g_netLagVoteMask = 0;
                break;
            }
            case PACKET_TYPE_LAG_VOTE:
            {
                // Master tallies a slave's drop vote.
                if (myconnectindex == connecthead)
                    lagDropVoteMask |= (1 << (uint8_t)packbuf[1]);
                break;
            }
            case PACKET_TYPE_DROP_PLAYER:
            {
                int32_t const slot = (uint8_t)packbuf[1];
                Net_ApplyConnectedMask(B_UNBUF16(&packbuf[2]));
                Net_RebuildConnectList();
                if ((unsigned)slot < MAXPLAYERS && g_player[slot].ps)
                {
                    P_QuickKill(g_player[slot].ps);
                    Bsprintf(buf, "%s^00 dropped (lag)", g_player[slot].user_name);
                    G_AddUserQuote(buf);
                }
                g_netLagActive = 0;
                g_netLagMask = 0;
                break;
            }
            case PACKET_TYPE_NEW_GAME:
            {
                if (myconnectindex == connecthead)
                {
                    OSD_Printf("PACKET_TYPE_NEW_GAME: MASTER SHOULDN'T GET THIS PACKET!\n");
                    continue;
                }

                if (vote.level != -1 || vote.episode != -1 || vote.starter != -1)
                    G_AddUserQuote("VOTE SUCCEEDED");

                j = 1;
                // Non-menu versions of these variables are handled by G_EnterLevel
                ud.m_level_number   = packbuf[j++];
                ud.m_volume_number  = packbuf[j++];
                ud.m_player_skill   = packbuf[j++];
                ud.m_coop           = packbuf[j++];
                ud.m_dmflags        = (int32_t)B_UNBUF32(&packbuf[j]);  j += sizeof(int32_t);
                uint32_t flags      = (uint32_t)B_UNBUF32(&packbuf[j]); j += sizeof(int32_t);
                G_NewGame(flags | NEWGAME_FROMSERVER);
                break;
            }
            case PACKET_TYPE_NULL_PACKET:
            {
                break;
            }
            case PACKET_TYPE_PLAYER_READY:
            {
                if (g_player[other].playerreadyflag == 0)
                    LOG_F(INFO, "Player %d is ready", other);
                g_player[other].playerreadyflag++;
                return;
            }
            case PACKET_TYPE_PING:
            {
                j = 1;
                if (myconnectindex == connecthead) // We're the master, recieving from the client
                {
                    Net_GetPingTimeFromPacket(&j);
                    uint32_t tics = timerGetTicks();
                    g_player[other].ping = tics - pingTime;
                }
                else
                {
                    Net_GetPingTimeFromPacket(&j);
                    TRAVERSE_CONNECT(i)
                    {
                        Net_GetPlayerPingFromPacket(&j, i);
                    }

                    // Send ping time we got from master back to it, to calculate how long it took.
                    j = 1;
                    Net_AddPingTimeToPacket(&j);
                    mmulti_sendpacket(connecthead, (unsigned char*)packbuf, j);
                }
                break;
            }
            case PACKET_TYPE_MESSAGE:
            {
                //slaves in M/S mode only send to master
                if (myconnectindex == connecthead)
                {
                    if (packbuf[1] == 255)
                    {
                        //Master re-transmits message to all others
                        TRAVERSE_CONNECT(i)
                            if (i != other)
                                mmulti_sendpacket(i, (unsigned char*)packbuf, packbufleng);
                    }
                    else if (((int)packbuf[1]) != myconnectindex)
                    {
                        //Master re-transmits message not intended for master
                        mmulti_sendpacket((int)packbuf[1], (unsigned char*)packbuf, packbufleng);
                        break;
                    }
                }

                Bstrcpy(recbuf, packbuf + 2);
                recbuf[packbufleng - 2] = 0;

                G_AddUserQuote(recbuf);
                S_PlaySound(EXITMENUSOUND);

                pus = NUMPAGES;
                pub = NUMPAGES;

                break;
            }
            case PACKET_TYPE_FRAGLIMIT_CHANGED:
            {
                if (myconnectindex == connecthead)
                {
                    OSD_Printf("PACKET_TYPE_FRAGLIMIT_CHANGED: MASTER SHOULDN'T GET THIS PACKET!\n");
                    continue;
                }

                ud.fraglimit = packbuf[1];
                Bsprintf(tempbuf, "FRAGLIMIT CHANGED TO %d", ud.fraglimit);
                G_AddUserQuote(tempbuf);
                break;
            }
            case PACKET_TYPE_EOL:
            {
                ud.gm = MODE_EOL;
                ud.level_number = packbuf[1];
                ud.from_bonus = packbuf[2];
                ud.secretlevel = packbuf[3];
                break;
            }

            // ****** Re-transmittable packets ******
            default:
            {
                switch (packbuf[0])
                {
                    case PACKET_TYPE_VERSION:
                    {
                        if (packbuf[2] != (char)atoi(VERSION))
                        {
                            LOG_F(ERROR, "Player has version %d, expecting %d", packbuf[2], (char)atoi(VERSION));
                            G_GameExit("You cannot play with different versions of NetDuke32!");
                        }
                        if (packbuf[3] != (char)BYTEVERSION)
                        {
                            LOG_F(ERROR, "Player has version %d, expecting %d (%d, %d, %d)", packbuf[3], BYTEVERSION, BYTEVERSION_NETDUKE32, PLUTOPAK, VOLUMEONE);
                            G_GameExit("You cannot play Duke with different versions!");
                        }

                        break;
                    }
                    case PACKET_TYPE_PLAYER_OPTIONS:
                    {
                        int32_t playerNum = packbuf[1];
                        if ((myconnectindex == connecthead) && playerNum != other)
                        {
                            OSD_Printf("PACKET_TYPE_PLAYER_OPTIONS: Player index doesn't match client index!\n");
                            playerNum = packbuf[1] = other; // Sanitize before re-transmit.
                        }

                        j = 2;
                        g_player[playerNum].ps->auto_aim = packbuf[j++];
                        g_player[playerNum].ps->weaponswitch = packbuf[j++];
                        g_player[playerNum].ps->palookup = g_player[playerNum].pcolor = playerColor_getValidPal(packbuf[j++]);
                        g_player[playerNum].pteam = packbuf[j++];
                        g_player[playerNum].pskin = P_ValidateSkin(packbuf[j++]);

                        if (g_player[playerNum].pskin)
                            LOG_F(INFO, "Player %d is using the %s skin", playerNum,
                                  g_playerSkinNames[g_player[playerNum].pskin]);

                        break;
                    }
                    case PACKET_TYPE_PLAYER_NAME:
                    {
                        int32_t playerNum = packbuf[1];
                        if ((myconnectindex == connecthead) && playerNum != other)
                        {
                            OSD_Printf("PACKET_TYPE_PLAYER_NAME: Player index doesn't match client index!\n");
                            playerNum = packbuf[1] = other; // Sanitize before re-transmit.
                        }

                        for (i = 2; packbuf[i] && i < (int32_t)sizeof(g_player[0].user_name); i++)
                        {
                            g_player[playerNum].user_name[i - 2] = packbuf[i];
                        }
                        g_player[playerNum].user_name[i - 2] = 0;
                        i++;

                        LOG_F(INFO, "Player %d's name is now %s", playerNum, g_player[playerNum].user_name);

                        break;
                    }
                    case PACKET_TYPE_WEAPON_CHOICE:
                    {
                        int32_t playerNum = packbuf[1];
                        if ((myconnectindex == connecthead) && playerNum != other)
                        {
                            OSD_Printf("PACKET_TYPE_WEAPON_CHOICE: Player index doesn't match client index!\n");
                            playerNum = packbuf[1] = other; // Sanitize before re-transmit.
                        }

                        i = 2;

                        j = i; //This used to be Duke packet #9... now concatenated with Duke packet #6
                        for (; i - j < 10; i++) g_player[playerNum].wchoice[i - j] = packbuf[i];

                        break;
                    }
                    case PACKET_TYPE_RTS:
                    {
                        if (rts_numlumps == 0)
                            continue;

                        if (ud.config.SoundToggle == 0 || ud.lockout == 1 || ud.config.FXDevice < 0 || !(ud.config.VoiceToggle & 4))
                            break;

                        rtsptr = (char*)RTS_GetSound(packbuf[1] - 1);
                        FX_Play3D(rtsptr, RTS_SoundLength(packbuf[1] - 1), FX_ONESHOT, 0, 0, 1, 255, fix16_one, -packbuf[1]);
                        g_RTSPlaying = 7;
                        break;
                    }
                    case PACKET_TYPE_MENU_LEVEL_QUIT:
                    {
                        G_GameExit("Game aborted from menu; disconnected.");
                        break;
                    }
                    case PACKET_TYPE_USER_MAP:
                    {
                        Bstrcpy(boardfilename, packbuf + 1);
                        boardfilename[packbufleng - 1] = 0;
                        Bcorrectfilename(boardfilename, 0);
                        if (boardfilename[0] != 0)
                        {
                            if ((i = kopen4loadfrommod(boardfilename, 0)) < 0)
                            {
                                Bmemset(boardfilename, 0, sizeof(boardfilename));
                                Net_SendUserMapName();
                            }
                            else kclose(i);
                        }

                        if (ud.m_level_number == 7 && ud.m_volume_number == 0 && boardfilename[0] == 0)
                            ud.m_level_number = 0;

                        break;
                    }
                    case PACKET_TYPE_MAP_VOTE:
                    {
                        int32_t playerNum = packbuf[1];
                        if ((myconnectindex == connecthead) && playerNum != other)
                        {
                            OSD_Printf("PACKET_TYPE_MAP_VOTE: Player index doesn't match client index!\n");
                            playerNum = packbuf[1] = other; // Sanitize before re-transmit.
                        }

                        if ((vote.starter == myconnectindex || myconnectindex == connecthead) && g_player[playerNum].gotvote == 0)
                        {
                            g_player[playerNum].gotvote = 1;
                            if (packbuf[2])
                                vote.yes_votes++;

                            Bsprintf(tempbuf, "CONFIRMED VOTE FROM %s", g_player[playerNum].user_name);
                            G_AddUserQuote(tempbuf);
                        }
                        break;
                    }
                    case PACKET_TYPE_MAP_VOTE_INITIATE: // call map vote
                    {
                        int32_t playerNum = packbuf[1];
                        if ((myconnectindex == connecthead) && playerNum != other)
                        {
                            OSD_Printf("PACKET_TYPE_MAP_VOTE_INITIATE: Player index doesn't match client index!\n");
                            playerNum = packbuf[1] = other; // Sanitize before re-transmit.
                        }

                        j = 2;

                        vote.starter            = playerNum;
                        vote.episode            = packbuf[j++];
                        vote.level              = packbuf[j++];
                        vote.skill              = packbuf[j++];
                        vote.gametype           = packbuf[j++];
                        vote.dmflags            = (int32_t)B_UNBUF32(&packbuf[j]); j += sizeof(int32_t);

                        Bsprintf(tempbuf, "%s^00 HAS CALLED A VOTE TO CHANGE MAP TO %s (E%dL%d)",
                            g_player[playerNum].user_name,
                            g_mapInfo[vote.episode * MAXLEVELS + vote.level].name,
                            vote.episode + 1, vote.level + 1);
                        G_AddUserQuote(tempbuf);

                        Bsprintf(tempbuf, "PRESS F1 TO ACCEPT, F2 TO DECLINE");
                        G_AddUserQuote(tempbuf);

                        for (ALL_PLAYERS(i))
                        {
                            g_player[i].gotvote = 0;
                        }

                        g_player[vote.starter].gotvote = vote.yes_votes = 1;
                        break;
                    }
                    case PACKET_TYPE_MAP_VOTE_CANCEL: // cancel map vote
                    {
                        int32_t playerNum = packbuf[1];
                        if ((myconnectindex == connecthead) && playerNum != other)
                        {
                            OSD_Printf("PACKET_TYPE_MAP_VOTE_CANCEL: Player index doesn't match client index!\n");
                            playerNum = packbuf[1] = other; // Sanitize before re-transmit.
                        }

                        if (vote.starter == playerNum)
                        {
                            vote = votedata_t();
                            int32_t numVotes = 0;

                            for(ALL_PLAYERS(i))
                                numVotes += g_player[i].gotvote;

                            if (numVotes != numplayers)
                                Bsprintf(tempbuf, "%s^00 HAS CANCELED THE VOTE", g_player[playerNum].user_name);
                            else
                                Bsprintf(tempbuf, "VOTE FAILED");

                            G_AddUserQuote(tempbuf);

                            for (ALL_PLAYERS(i))
                            {
                                g_player[i].gotvote = 0;
                            }
                            
                        }
                        break;
                    }
                    case PACKET_TYPE_LOAD_GAME:
                    {
                        multiflag = 2;
                        multiwhat = 0;
                        multiwho = packbuf[2]; //other: need to send in m/s mode because of possible re-transmit
                        multipos = packbuf[1];
                        G_LoadPlayer(multipos);
                        multiflag = 0;
                        break;
                    }
                }

                //Slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if (myconnectindex == connecthead)
                    TRAVERSE_CONNECT(i)
                        if (i != other)
                            mmulti_sendpacket(i, (unsigned char*)packbuf, packbufleng);
                break;
            }
        }
    }
}

void Net_SendQuit(void)
{
    if (netQuitSend)
    {
        //game.cpp@app_crashhandler() calls Net_SendQuit(). It has been called already when the crash has happen within Net_SendQuit().
        //Abort the second call
        return;
    }
    netQuitSend = 1;
    if (g_gameQuit == 0 && (numplayers > 1))
    {
        if (ud.gm & MODE_GAME)
        {
            g_gameQuit = 1;
            quittimer = (int32_t)totalclock+(CLOCKTICKSPERSECOND*2);
        }
        else
        {
            int i;

            tempbuf[0] = PACKET_TYPE_MENU_LEVEL_QUIT;
            tempbuf[1] = myconnectindex;

            TRAVERSE_CONNECT(i)
            {
                if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)tempbuf,2);
                if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
            }
            G_GameExit(" ");
        }
    }
    else if (numplayers < 2)
        G_GameExit(" ");

    if ((totalclock > quittimer) && (g_gameQuit == 1))
        G_GameExit("Timed out.");
}

void Net_SendWeaponChoice(void)
{
    int i,l;

    buf[0] = PACKET_TYPE_WEAPON_CHOICE;
    buf[1] = myconnectindex;
    l = 2;

    for (i=0;i<10;i++)
    {
        g_player[myconnectindex].wchoice[i] = g_player[0].wchoice[i];
        buf[l++] = (char)g_player[0].wchoice[i];
    }

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],l);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

void Net_SendVersion(void)
{
    int i;

    if (numplayers < 2) return;

    buf[0] = PACKET_TYPE_VERSION;
    buf[1] = myconnectindex;
    buf[2] = (char)atoi(s_buildRev);
    buf[3] = BYTEVERSION;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],4);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }

    // A late joiner must stay out of the ready-flag protocol until the
    // final integration barrier, or its counters contaminate the running
    // game's barrier bookkeeping.
    if (!g_netLateJoiner)
        Net_WaitForPlayers();
}

void Net_SendPlayerOptions(void)
{
    int i,l;

    buf[0] = PACKET_TYPE_PLAYER_OPTIONS;
    buf[1] = myconnectindex;
    l = 2;

    //null terminated player name to send
//    for (i=0;szPlayerName[i];i++) buf[l++] = Btoupper(szPlayerName[i]);
//    buf[l++] = 0;

    buf[l++] = g_player[myconnectindex].ps->auto_aim = ud.config.AutoAim;
    buf[l++] = g_player[myconnectindex].ps->weaponswitch = ud.weaponswitch;
    buf[l++] = g_player[myconnectindex].ps->palookup = g_player[myconnectindex].pcolor = playerColor_getValidPal(ud.color);
    buf[l++] = g_player[myconnectindex].pteam = ud.team;
    buf[l++] = g_player[myconnectindex].pskin = P_ValidateSkin(ud.skin);

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],l);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

void Net_SendFragLimit(void)
{
    if (myconnectindex != connecthead)
        return;

    packbuf[0] = PACKET_TYPE_FRAGLIMIT_CHANGED;
    packbuf[1] = ud.fraglimit;

    int32_t i;
    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 2);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

// TODO: Give player number parameter. (Useful for sending bot names)
void Net_SendPlayerName(void)
{
    int i,l;

    for (l=0;(unsigned)l<sizeof(szPlayerName)-1;l++)
        g_player[myconnectindex].user_name[l] = Btoupper(szPlayerName[l]);

    if (numplayers < 2)
        return;

    buf[0] = PACKET_TYPE_PLAYER_NAME;
    buf[1] = myconnectindex;
    l = 2;

    //null terminated player name to send
    for (i=0;szPlayerName[i];i++) buf[l++] = Btoupper(szPlayerName[i]);
    buf[l++] = 0;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],l);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

void Net_SendUserMapName(void)
{
    if (numplayers > 1)
    {
        int j;
        int ch;

        packbuf[0] = PACKET_TYPE_USER_MAP;
        packbuf[1] = 0;

        Bcorrectfilename(boardfilename,0);

        j = Bstrlen(boardfilename);
        boardfilename[j++] = 0;
        Bstrcat(packbuf+1,boardfilename);

        TRAVERSE_CONNECT(ch)
        {
            if (ch != myconnectindex) mmulti_sendpacket(ch, (unsigned char*)packbuf,j);
            if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
        }
    }
}

uint16_t Net_ConnectedMask(void)
{
    uint16_t mask = 0;
    for (int32_t i = 0; i < numplayers && i < 16; i++)
        if (g_player[i].connected)
            mask |= (1 << i);
    return mask;
}

void Net_ApplyConnectedMask(uint16_t mask)
{
    for (int32_t i = 0; i < numplayers && i < 16; i++)
    {
        g_player[i].connected = (mask >> i) & 1;
        if (g_player[i].connected)
        {
            G_MaybeAllocPlayer(i);
            g_player[i].isBot = 0;
        }
    }
}

// Rebuild connecthead/connectpoint2 to span exactly the connected players,
// in ascending slot order so every machine derives the identical list.
void Net_RebuildConnectList(void)
{
    int32_t last = -1;
    connecthead = -1;
    for (int32_t i = 0; i < numplayers; i++)
    {
        if (!g_player[i].connected)
            continue;
        if (connecthead < 0)
            connecthead = i;
        else
            connectpoint2[last] = i;
        last = i;
    }
    if (last >= 0)
        connectpoint2[last] = -1;
}

static void Net_SendInitialSettingsTo(int32_t playerNum);

// Shared by every machine when a late joiner is admitted: flip the slot on,
// rebuild the rotation, and rewind the input pipeline to tick zero. The
// world state itself is untouched on existing peers - the joiner arrives at
// the identical state by loading the master's snapshot.
static void Net_IntegrateLateJoin(int32_t slot)
{
    G_MaybeAllocPlayer(slot);
    g_player[slot].connected = 1;
    g_player[slot].isBot = 0;

    Net_RebuildConnectList();

    for (int32_t i = 0; i < MAXPLAYERS; i++)
        g_player[i].playerreadyflag = 0;

    // Full input-fifo wipe (not just the counters): the wire format encodes
    // inputs as deltas against the previous fifo slot, so its contents must
    // be identical on every machine when the tick counter restarts.
    clearfifo();

    // Packets stamped with the old tick numbering may still be in flight;
    // don't let them false-positive the desync detector.
    g_syncCheckStartTick = MOVEFIFOSIZ;

    g_lateJoinFreeze = 0;
    lateJoinPending = -1;

    LOG_F(INFO, "Player %d joined the game in progress", slot);
}

// Joiner side of PACKET_TYPE_LATEJOIN_GO: persist the streamed snapshot and
// load it; G_LoadPlayer ends in the ready barrier the others are waiting at.
static void Net_CompleteLateJoin(int32_t slot)
{
    if (snapshotBuf == NULL || snapshotChunksGot < snapshotChunkTotal)
    {
        LOG_F(ERROR, "Late join: snapshot incomplete (%d/%d chunks)",
              snapshotChunksGot, snapshotChunkTotal);
        G_GameExit("Late join failed: incomplete state snapshot.");
    }

    char temp[BMAX_PATH];
    if (g_modDir[0] != '/')
        Bsnprintf(temp, sizeof(temp), "%s/egamlj.sav", g_modDir);
    else
        Bstrcpy(temp, "egamlj.sav");

    buildvfs_FILE fil = buildvfs_fopen_write(temp);
    if (fil == NULL)
        G_GameExit("Late join failed: can't write state snapshot.");

    buildvfs_fwrite(snapshotBuf, 1,
                    (snapshotChunkTotal - 1) * SNAPSHOT_CHUNK_SIZE + snapshotLastLen, fil);
    buildvfs_fclose(fil);
    DO_FREE_AND_NULL(snapshotBuf);

    Net_IntegrateLateJoin(slot);

    multiflag = 0;
    if (G_LoadPlayer(LATEJOIN_SAVE_SPOT))
        G_GameExit("Late join failed: couldn't load state snapshot.");

    // Auxiliary RNG streams live outside the savegame; adopt the master's.
    g_globalRandom        = (short)lateJoinGoRNG[0];
    g_random.global       = lateJoinGoRNG[1];
    g_random.animwall     = lateJoinGoRNG[2];
    g_random.player       = lateJoinGoRNG[3];
    g_random.playerweapon = lateJoinGoRNG[4];
    wrandomseed           = (uint32_t)lateJoinGoRNG[5];

    g_netLateJoiner = 2;
}

// Existing peers: integrate a pending late join once every broadcast tick
// has been simulated, which matches the tick the master snapshotted.
void Net_PeerCheckLateJoin(void)
{
    if (lateJoinGoSlot < 0 || lateJoinGoSlot == myconnectindex || myconnectindex == connecthead)
        return;

    if (g_player[connecthead].movefifoend > movefifoplc)
        return;  // Still have master ticks to simulate.

    int32_t const slot = lateJoinGoSlot;
    lateJoinGoSlot = -1;
    Net_ApplyConnectedMask(lateJoinGoMask);
    Net_IntegrateLateJoin(slot);
    Net_WaitForPlayers();
    G_ResetTimers();
}

// Master only: called once per frame from the move loop. When an empty slot
// completes the mmulti handshake mid-game, pause the input flow (the
// lockstep peers stall on their own), snapshot the paused state, stream it
// to the joiner, then tell everyone to fold the new player in.
void Net_MasterCheckLateJoin(void)
{
    if (!g_netLateJoin || myconnectindex != connecthead || numplayers < 2)
        return;

    if (!(ud.gm & MODE_GAME) || ud.recstat == 2)
        return;

    if (lateJoinPending < 0)
    {
        for (int32_t i = 1; i < numplayers; i++)
        {
            if (!g_player[i].connected && mmulti_slotconnected(i))
            {
                lateJoinPending = i;
                g_lateJoinFreeze = 1;
                LOG_F(INFO, "Late joiner in slot %d; pausing for state transfer", i);

                // Tell the joiner right away what's going on, so it knows to
                // stay out of the startup barriers and wait for the snapshot.
                packbuf[0] = PACKET_TYPE_LATEJOIN_ANNOUNCE;
                mmulti_sendpacket(i, (unsigned char *)packbuf, 1);
                Net_SendInitialSettingsTo(i);
                break;
            }
        }
        if (lateJoinPending < 0)
            return;
    }

    // Wait until we have simulated every tick we broadcast, so that all
    // peers can settle on the identical state.
    if (movefifoplc < movefifosendplc)
        return;

    int32_t const slot = lateJoinPending;

    if (G_SavePlayer(LATEJOIN_SAVE_SPOT))
    {
        LOG_F(ERROR, "Late join: failed to write the state snapshot");
        g_lateJoinFreeze = 0;
        lateJoinPending = -1;
        return;
    }

    int32_t const fil = kopen4loadfrommod("egamlj.sav", 0);
    if (fil == -1)
    {
        LOG_F(ERROR, "Late join: failed to read back the state snapshot");
        g_lateJoinFreeze = 0;
        lateJoinPending = -1;
        return;
    }

    int32_t const snapLen = kfilelength(fil);
    int32_t const total = (snapLen + SNAPSHOT_CHUNK_SIZE - 1) / SNAPSHOT_CHUNK_SIZE;
    char chunk[SNAPSHOT_CHUNK_SIZE + 8];

    LOG_F(INFO, "Late join: sending %d byte snapshot in %d chunks", snapLen, total);

    for (int32_t seq = 0; seq < total; seq++)
    {
        int32_t const len = min(SNAPSHOT_CHUNK_SIZE, snapLen - seq * SNAPSHOT_CHUNK_SIZE);

        chunk[0] = PACKET_TYPE_SNAPSHOT_CHUNK;
        B_BUF16(&chunk[1], (uint16_t)seq);
        B_BUF16(&chunk[3], (uint16_t)total);
        B_BUF16(&chunk[5], (uint16_t)len);
        kread(fil, &chunk[7], len);

        mmulti_sendpacket(slot, (unsigned char *)chunk, len + 7);

        // Keep the reliable-delivery window from overflowing; this also
        // keeps acks and peer traffic flowing while we blast the file out.
        while (mmulti_getoutputcirclesize() > 128)
            gameHandleEvents();
    }
    kclose(fil);

    // Everyone integrates the same roster; the GO carries it so the joiner
    // learns who is actually present.
    g_player[slot].connected = 1;

    packbuf[0] = PACKET_TYPE_LATEJOIN_GO;
    packbuf[1] = (char)slot;
    B_BUF16(&packbuf[2], Net_ConnectedMask());
    // The auxiliary RNG streams aren't part of the savegame snapshot; ship
    // them along so the joiner can seed them after loading.
    B_BUF32(&packbuf[4], (int32_t)g_globalRandom);
    B_BUF32(&packbuf[8], g_random.global);
    B_BUF32(&packbuf[12], g_random.animwall);
    B_BUF32(&packbuf[16], g_random.player);
    B_BUF32(&packbuf[20], g_random.playerweapon);
    B_BUF32(&packbuf[24], (int32_t)wrandomseed);

    for (int32_t i = 0; i < numplayers; i++)
        if (i != myconnectindex && g_player[i].connected)
            mmulti_sendpacket(i, (unsigned char *)packbuf, 28);

    Net_IntegrateLateJoin(slot);
    Net_WaitForPlayers();  // The joiner reaches this barrier from inside G_LoadPlayer.
    G_ResetTimers();
}

static int32_t Net_BuildInitialSettings(void)
{
    packbuf[0] = PACKET_TYPE_INIT_SETTINGS;

    int32_t j = 1;
    packbuf[j++] = ud.m_level_number;
    packbuf[j++] = ud.m_volume_number;
    packbuf[j++] = ud.m_player_skill;
    packbuf[j++] = ud.m_coop;
    packbuf[j++] = ud.warp_on;
    packbuf[j++] = ud.fraglimit;
    packbuf[j++] = ud.multimode;
    packbuf[j++] = ud.playerai;
    B_BUF32(&packbuf[j], ud.m_dmflags); j += sizeof(int32_t);
    B_BUF32(&packbuf[j], botNameSeed); j += sizeof(int32_t);
    B_BUF16(&packbuf[j], Net_ConnectedMask()); j += sizeof(uint16_t);
    packbuf[j++] = (char)g_netAsyncMode;

    return j;
}

void Net_SendInitialSettings(void)
{
    if (myconnectindex != connecthead) // First player dictates initial settings.
        return;

    int32_t j = Net_BuildInitialSettings();

    int i;
    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex)
            mmulti_sendpacket(i, (unsigned char*)packbuf, j);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

static void Net_SendInitialSettingsTo(int32_t playerNum)
{
    int32_t const j = Net_BuildInitialSettings();
    mmulti_sendpacket(playerNum, (unsigned char*)packbuf, j);
}

void Net_SendNewGame(uint32_t flags)
{
    if (numplayers < 2 || myconnectindex != connecthead) // Only hosts should be allowed to do this.
        return;

    packbuf[0] = PACKET_TYPE_NEW_GAME;
    
    int32_t j = 1;
    packbuf[j++] = ud.m_level_number;
    packbuf[j++] = ud.m_volume_number;
    packbuf[j++] = ud.m_player_skill;
    packbuf[j++] = ud.m_coop;
    B_BUF32(&packbuf[j], ud.m_dmflags); j += sizeof(int32_t);
    B_BUF32(&packbuf[j], flags);        j += sizeof(int32_t);

    int i;
    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf,j);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

void Net_EndOfLevel(bool secret)
{
    if (/*(myconnectindex != connecthead) ||*/ oldnet_predicting)
        return;

    ud.gm = MODE_EOL;

    if (secret)
    {
        ud.from_bonus = ud.level_number + 1;

        if (ud.secretlevel > 0 && ud.secretlevel < MAXLEVELS)
            ud.level_number = ud.secretlevel - 1;
    }
    else
    {
        if (ud.from_bonus)
        {
            ud.level_number = ud.from_bonus;
            ud.from_bonus = 0;
        }
        else
        {
            ud.level_number++;

            if (ud.level_number > MAXLEVELS - 1)
                ud.level_number = 0;
        }
    }

#if 0 // [JM] Disabled for Duke64 TC compat. Might break other things, but here's hoping it does not.
    packbuf[0] = PACKET_TYPE_EOL;
    packbuf[1] = ud.level_number;
    packbuf[2] = ud.from_bonus;
    packbuf[3] = ud.secretlevel;

    int i;
    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 4);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
#endif
}

void Net_EnterMessage(void)
{
    short ch, hitstate, i, j, l;

    if (ud.gm & MODE_SENDTOWHOM)
    {
        if (g_chatPlayer != -1 || ud.multimode < 3)
        {
            tempbuf[0] = PACKET_TYPE_MESSAGE;
            tempbuf[2] = 0;
            recbuf[0]  = 0;

            if (ud.multimode < 3)
                g_chatPlayer = 2;

            if (typebuf[0] == '/' && Btoupper(typebuf[1]) == 'M' && Btoupper(typebuf[2]) == 'E')
            {
                i = 3, j = Bstrlen(typebuf);
                Bstrcpy(tempbuf,typebuf);
                while (i < j)
                {
                    typebuf[i-3] = tempbuf[i];
                    i++;
                }
                typebuf[i-3] = '\0';
                Bsprintf(recbuf, "^07* %s^07", g_player[myconnectindex].user_name);
            }
            else
            {
                Bsprintf(recbuf, "%s^07: ", g_player[myconnectindex].user_name);
            }

            Bstrcat(recbuf,typebuf);
            j = Bstrlen(recbuf);
            recbuf[j] = 0;
            Bstrcat(tempbuf+2,recbuf);

            if (g_chatPlayer >= ud.multimode)
            {
                ChatPipe_SendMessage(typebuf);
                tempbuf[1] = 255;

                TRAVERSE_CONNECT(ch)
                {
                    if (ch != myconnectindex) mmulti_sendpacket(ch, (unsigned char*)tempbuf,j+2);
                    if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
                }

                G_AddUserQuote(recbuf);
                quotebot += 8;
                l = G_GameTextLen(USERQUOTE_LEFTOFFSET,OSD_StripColors(tempbuf,recbuf));
                while (l > (ud.config.ScreenWidth - USERQUOTE_RIGHTOFFSET))
                {
                    l -= (ud.config.ScreenWidth - USERQUOTE_RIGHTOFFSET);
                    quotebot += 8;
                }
                quotebotgoal = quotebot;
            }
            else if (g_chatPlayer >= 0)
            {
                tempbuf[1] = (char)g_chatPlayer;
                if (myconnectindex != connecthead)
                    g_chatPlayer = connecthead;
                mmulti_sendpacket(g_chatPlayer, (unsigned char*)tempbuf,j+2);
            }

            g_chatPlayer = -1;
            ud.gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
        }
        else if (g_chatPlayer == -1)
        {
            j = 50;
            gametext(320>>1,j,"SEND MESSAGE TO...",0,2+8+16);
            j += 8;
            TRAVERSE_CONNECT(i)
            {
                if (i == myconnectindex)
                {
                    minitextshade((320>>1)-40+1,j+1,"A/ENTER - ALL",26,0,2+8+16);
                    minitext((320>>1)-40,j,"A/ENTER - ALL",0,2+8+16);
                    j += 7;
                }
                else
                {
                    Bsprintf(buf,"      %d - %s",i+1,g_player[i].user_name);
                    minitextshade((320>>1)-40-6+1,j+1,buf,26,0,2+8+16);
                    minitext((320>>1)-40-6,j,buf,0,2+8+16);
                    j += 7;
                }
            }
            minitextshade((320>>1)-40-4+1,j+1,"    ESC - Abort",26,0,2+8+16);
            minitext((320>>1)-40-4,j,"    ESC - Abort",0,2+8+16);
            j += 7;

            if (ud.screen_size > 0) j = 200-45;
            else j = 200-8;
            mpgametext(j,typebuf,0,2+8+16);

            if (KB_KeyWaiting())
            {
                i = KB_GetCh();

                if (i == 'A' || i == 'a' || i == 13)
                    g_chatPlayer = ud.multimode;
                else if (i >= '1' || i <= (ud.multimode + '1'))
                    g_chatPlayer = i - '1';
                else
                {
                    g_chatPlayer = ud.multimode;
                    if (i == 27)
                    {
                        ud.gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
                        g_chatPlayer = -1;
                    }
                    else
                        typebuf[0] = 0;
                }

                KB_ClearKeyDown(sc_1);
                KB_ClearKeyDown(sc_2);
                KB_ClearKeyDown(sc_3);
                KB_ClearKeyDown(sc_4);
                KB_ClearKeyDown(sc_5);
                KB_ClearKeyDown(sc_6);
                KB_ClearKeyDown(sc_7);
                KB_ClearKeyDown(sc_8);
                KB_ClearKeyDown(sc_A);
                KB_ClearKeyDown(sc_Escape);
                KB_ClearKeyDown(sc_Enter);
            }
        }
    }
    else
    {
        if (ud.screen_size > 1) j = 200-45;
        else j = 200-8;
        if (xdim >= 640 && ydim >= 480)
            j = scale(j,ydim,200);
        hitstate = Net_EnterText(320>>1,j,typebuf,120,1);

        if (hitstate == 1)
        {
            KB_ClearKeyDown(sc_Enter);
            if (Bstrlen(typebuf) == 0)
            {
                ud.gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
                return;
            }
            if (ud.automsg)
            {
                if (SHIFTS_IS_PRESSED) g_chatPlayer = -1;
                else g_chatPlayer = ud.multimode;
            }
            ud.gm |= MODE_SENDTOWHOM;
        }
        else if (hitstate == -1)
            ud.gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
        else pub = NUMPAGES;
    }
}

void Net_ClearFIFO(void)
{
    netInput = {}; // Clear all networked input.

    memset(&syncData, 0, sizeof(syncData));
    memset(&syncError, 0, sizeof(syncError));
    memset(&g_szfirstSyncMsg, 0, sizeof(g_szfirstSyncMsg));
    g_foundSyncError = false;

    bufferjitter = 1;
    mymaxlag = otherminlag = 0;
    movefifoplc = movefifosendplc = predictfifoplc = 0;

    Net_InitializePrediction();

    for (int32_t i = 0; i < MAXPLAYERS; i++)
    {
        g_player[i].movefifoend = 0;
        g_player[i].myminlag = 0;
        g_player[i].lastSyncTick = -1;
    }
}

void Net_CheckPlayerQuit(int i)
{
    if (TEST_SYNC_KEY(g_player[i].inputBits->bits, SK_GAMEQUIT) == 0)
        return;

    g_player[i].connected = 0;

    G_CloseDemoWrite();

    // If we're the master, keep executing until all slaves can get the packet.
    if (i == connecthead)
        return;

    if (i == myconnectindex)
        G_GameExit(" ");

    if (screenpeek == i)
    {
        screenpeek = G_GetNextPlayer(i);
        if (screenpeek < 0)
            screenpeek = connecthead;
    }

    if (i == connecthead)
        connecthead = connectpoint2[connecthead];
    else
    {
        int j;
        TRAVERSE_CONNECT(j)
        {
            if (connectpoint2[j] == i)
                connectpoint2[j] = connectpoint2[i];
        }
    }

    numplayers--;
    ud.multimode--;

    if (numplayers < 2)
        S_PlaySound(GENERIC_AMBIENCE17);

    pub = NUMPAGES;
    pus = NUMPAGES;
    G_UpdateScreenArea();

    P_QuickKill(g_player[i].ps);
    A_DeleteSprite(g_player[i].ps->i);

    Bsprintf(buf, "%s^00 is history!", g_player[i].user_name);
    G_AddUserQuote(buf);
    Bstrcpy(apStrings[QUOTE_RESERVED2], buf);

    if (vote.starter == i)
    {
        for (ALL_PLAYERS(i))
            g_player[i].gotvote = 0;

        vote = votedata_t();
    }

    g_player[myconnectindex].ps->ftq = QUOTE_RESERVED2, g_player[myconnectindex].ps->fta = 180;
}

void Net_WaitForPlayers()
{
    int i;

    if (numplayers < 2)
        return;

    g_player[myconnectindex].playerreadyflag++;
    packbuf[0] = PACKET_TYPE_PLAYER_READY;
    if (myconnectindex != connecthead)
        mmulti_sendpacket(connecthead, (unsigned char*)packbuf, 1);

    auto oldPal = g_player[myconnectindex].ps->palette;
    P_SetGamePalette(g_player[myconnectindex].ps, TITLEPAL, 11);

    while (1)
    {
        //if (quitevent) G_GameExit(""); // This sucks
        gameHandleEvents();

        if (!engineFPSLimit())
            continue;

        videoClearScreen(0);

        rotatesprite(0, 0, 65536L, 0, BETASCREEN, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(160 << 16, (104) << 16, 60 << 10, 0, DUKENUKEM, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(160 << 16, (129) << 16, 30 << 11, 0, THREEDEE, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        if (PLUTOPAK)   // JBF 20030804
            rotatesprite(160 << 16, (151) << 16, 30 << 11, 0, PLUTOPAKSPRITE + 1, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);

        gametext(160, 190, "WAITING FOR PLAYERS", 14, 2);

        if (myconnectindex == connecthead)
        {
            int ypos = 8;
            gametext(8, ypos, "^12Player Status:", -127, 2);
            TRAVERSE_CONNECT(i)
            {
                ypos += 8;
                gametext(8, ypos, g_player[i].user_name, -127, 2);

                if (g_player[i].playerreadyflag >= g_player[myconnectindex].playerreadyflag)
                    gametext(107, ypos, "^7- ^8Ready!", -127, 2);
                else
                    gametext(107, ypos, "^7- ^10Loading", -127, 2);
            }
        }

        videoNextPage();

        // Check if ready, if not, break from loop.
        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].playerreadyflag < g_player[myconnectindex].playerreadyflag)
                break;

            //slaves in M/S mode only wait for master
            if (myconnectindex != connecthead)
            {
                i = -1; // we're a slave
                break;
            }
        }

        // -1 Means we iterated through all players without breaking in above loop. All players ready.
        if (i <= -1)
        {
            // master sends ready packet once it hears from all slaves
            if (myconnectindex == connecthead)
            {
                TRAVERSE_CONNECT(i)
                {
                    packbuf[0] = PACKET_TYPE_PLAYER_READY;

                    if (i != myconnectindex)
                        mmulti_sendpacket(i, (unsigned char*)packbuf, 1);
                }
            }

            P_SetGamePalette(g_player[myconnectindex].ps, oldPal, 11);
            return;
        }
    }
}

void allowtimetocorrecterrorswhenquitting(void)
{
    int i, j, oldtotalclock;

    ready2send = 0;

    for (j=MAXPLAYERS-1;j>=0;j--)
    {
        oldtotalclock = (int32_t)totalclock;

        while (totalclock < oldtotalclock+TICSPERFRAME)
            gameHandleEvents();

        if (KB_KeyPressed(sc_Escape))
            return;

        packbuf[0] = PACKET_TYPE_NULL_PACKET;
        TRAVERSE_CONNECT(i)
        {
            if (i != myconnectindex)
                mmulti_sendpacket(i, (unsigned char*)packbuf,1);
            if (myconnectindex != connecthead)
                break; //slaves in M/S mode only send to master
        }
    }
}

void Net_Disconnect(bool showScores)
{
    allowtimetocorrecterrorswhenquitting();
    mmulti_uninitmultiplayers();

    g_networkBroadcastMode = NETMODE_OFFLINE;

    if (!quickExit && showScores)
    {
        if (playerswhenstarted > 1 && ud.gm & MODE_GAME && GTFLAGS(GAMETYPE_SCORESHEET))
        {
            G_BonusScreen(1);
            videoSetGameMode(ud.config.ScreenMode, ud.config.ScreenWidth, ud.config.ScreenHeight, ud.config.ScreenBPP, ud.detail);
        }
    }

    numplayers = ud.multimode = playerswhenstarted = 1;
    myconnectindex = screenpeek = 0;
    oldnet_gotinitialsettings = 0;

    G_BackToMenu();
}

void Net_InitiateVote()
{
    for (int32_t ALL_PLAYERS(i))
        g_player[i].gotvote = 0;

    g_player[myconnectindex].gotvote = vote.yes_votes = 1;
    
    packbuf[0] = PACKET_TYPE_MAP_VOTE_INITIATE;
    int32_t j = 1;
    packbuf[j++] = vote.starter           = myconnectindex;
    packbuf[j++] = vote.episode           = ud.m_volume_number;
    packbuf[j++] = vote.level             = ud.m_level_number;
    packbuf[j++] = vote.skill             = ud.m_player_skill;
    packbuf[j++] = vote.gametype          = ud.m_coop;
    B_BUF32(&packbuf[j], ud.m_dmflags); j += sizeof(int32_t);

    int32_t c = 0;
    TRAVERSE_CONNECT(c)
    {
        if (c != myconnectindex) mmulti_sendpacket(c, (unsigned char*)packbuf, j);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}

void Net_CancelVote()
{
    OSD_Printf("Canceling vote.\n");

    vote = votedata_t();
    for (int32_t ALL_PLAYERS(i))
    {
        g_player[i].gotvote = 0;
    }

    packbuf[0] = PACKET_TYPE_MAP_VOTE_CANCEL;
    packbuf[1] = myconnectindex;

    int32_t c = 0;
    TRAVERSE_CONNECT(c)
    {
        if (c != myconnectindex) mmulti_sendpacket(c, (unsigned char*)packbuf, 2);
        if (myconnectindex != connecthead) break; //slaves in M/S mode only send to master
    }
}