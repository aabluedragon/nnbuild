#include "duke3d.h"
//#include "oldnet.h"

#define TIMERUPDATESIZ 32

int quittimer = 0;
int lastpackettime = 0;
int mymaxlag, otherminlag, bufferjitter = 1;
int voting = -1, vote_map = -1, vote_episode = -1;

int movefifosendplc;
int movefifoplc;

// Prediction Vars
int predictfifoplc;

int oldnet_predicting;
DukePlayer_t predictedPlayer;

vec3_t myposbak[MOVEFIFOSIZ];
fix16_t myhorizbak[MOVEFIFOSIZ], myangbak[MOVEFIFOSIZ];

static int g_chatPlayer = -1;
static char recbuf[180];

extern int qe,cp;
extern char *rtsptr; // game.c
extern void computergetinput(int snum, input_t *syn);

int pingTime;

static void Net_AddPingTimeToPacket(int* j)
{
    B_BUF32(&packbuf[(*j)], pingTime);
    (*j) += 4;
}

static void Net_GetPingTimeFromPacket(int* j)
{
    pingTime = (int32_t)B_UNBUF32(&packbuf[(*j)]);
    (*j) += 4;
}

static void Net_AddPlayerPingToPacket(int* j, int playerNum)
{
    B_BUF32(&packbuf[(*j)], g_player[playerNum].ping);
    (*j) += 4;
}

static void Net_GetPlayerPingFromPacket(int* j, int playerNum)
{
    g_player[playerNum].ping = (uint32_t)B_UNBUF32(&packbuf[(*j)]);
    (*j) += 4;
}

static void Net_SendPing(void)
{
    if (g_networkBroadcastMode != NETMODE_MASTERSLAVE || (myconnectindex != connecthead))
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

    Net_SendPing();
    Net_ParsePackets();
}

static void Net_AddPlayerInputToPacket(int* j, int playerNum, input_t* osyn, input_t* nsyn)
{
    int inputFlagsPos = (*j)++;
    int extFlagsPos = (*j)++;

    packbuf[inputFlagsPos] = 0;
    packbuf[extFlagsPos] = 0;

    if (nsyn[playerNum].fvel != osyn[playerNum].fvel)
    {
        B_BUF16(&packbuf[(*j)], nsyn[playerNum].fvel);
        (*j) += 2;
        packbuf[inputFlagsPos] |= 1;
    }
    if (nsyn[playerNum].svel != osyn[playerNum].svel)
    {
        B_BUF16(&packbuf[(*j)], nsyn[playerNum].svel);
        (*j) += 2;
        packbuf[inputFlagsPos] |= 2;
    }
    if (nsyn[playerNum].q16avel != osyn[playerNum].q16avel)
    {
        B_BUF32(&packbuf[(*j)], nsyn[playerNum].q16avel);
        (*j) += 4;
        packbuf[inputFlagsPos] |= 4;
    }
    if (nsyn[playerNum].q16horz != osyn[playerNum].q16horz)
    {
        B_BUF32(&packbuf[(*j)], nsyn[playerNum].q16horz);
        (*j) += 4;
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

static void Net_GetPlayerInputFromPacket(int* j, int playerNum, input_t* osyn, input_t* nsyn)
{
    char inputFlags = packbuf[(*j)++];
    char extFlags = packbuf[(*j)++];

    if (playerNum == myconnectindex)
    {
        if (inputFlags & 1) (*j) += 2;
        if (inputFlags & 2) (*j) += 2;
        if (inputFlags & 4) (*j) += 4;
        if (inputFlags & 8) (*j) += 4;

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

    copybufbyte(&osyn[playerNum], &nsyn[playerNum], sizeof(input_t));
    if (inputFlags & 1)
    {
        nsyn[playerNum].fvel = (short)B_UNBUF16(&packbuf[(*j)]);
        (*j) += 2;
    }
    if (inputFlags & 2)
    {
        nsyn[playerNum].svel = (short)B_UNBUF16(&packbuf[(*j)]);
        (*j) += 2;
    }
    if (inputFlags & 4)
    {
        nsyn[playerNum].q16avel = (fix16_t)B_UNBUF32(&packbuf[(*j)]);
        (*j) += 4;
    }
    if (inputFlags & 8)
    {
        nsyn[playerNum].q16horz = (fix16_t)B_UNBUF32(&packbuf[(*j)]);
        (*j) += 4;
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
        (g_networkBroadcastMode == NETMODE_MASTERSLAVE) &&
        (playerNum == connecthead) && 
        (playerNum != myconnectindex))
    {
        G_GameExit("Host has terminated the game.");
    }
}

void faketimerhandler(void) { ; };

void Net_HandleInput(void)
{
    int i, j, k;
    //    short who;
    input_t *osyn, *nsyn;

    if (qe == 0 && KB_KeyPressed(sc_LeftControl) && KB_KeyPressed(sc_LeftAlt) && KB_KeyPressed(sc_Delete))
    {
        qe = 1;
        G_GameExit("Quick Exit.");
    }

    //timerUpdateClock();
    //MUSIC_Update();
    Net_GetPackets();

    if (g_player[myconnectindex].movefifoend - movefifoplc >= 100)
        return;

    // Put our local input into the FIFO to be processed by P_ProcessInput and such.
    nsyn = &inputfifo[g_player[myconnectindex].movefifoend&(MOVEFIFOSIZ-1)][myconnectindex];
    *nsyn = localInput;

    g_player[myconnectindex].movefifoend++;

    if (numplayers < 2)
    {
        if (ud.multimode > 1)
        {
            TRAVERSE_CONNECT(i)
            {
                if (i != myconnectindex)
                {
                    if (ud.playerai /*&& g_player[i].isBot*/) // To-do: Add per-player variable.
                    {
                        computergetinput(i, &inputfifo[g_player[i].movefifoend&(MOVEFIFOSIZ - 1)][i]);
                        inputfifo[g_player[i].movefifoend&(MOVEFIFOSIZ - 1)][i].svel++;
                        inputfifo[g_player[i].movefifoend&(MOVEFIFOSIZ - 1)][i].fvel++;
                    }

                    g_player[i].movefifoend++;
                }
            }
        }
        return;
    }

    TRAVERSE_CONNECT(i)
    if (i != myconnectindex)
    {
        k = (g_player[myconnectindex].movefifoend-1)-g_player[i].movefifoend;
        g_player[i].myminlag = min(g_player[i].myminlag,k);
        mymaxlag = max(mymaxlag,k);
    }

    if (((g_player[myconnectindex].movefifoend - 1) & (TIMERUPDATESIZ - 1)) == 0)
    {
        i = mymaxlag - bufferjitter;
        mymaxlag = 0;
        if (i > 0)
            bufferjitter += ((2 + i) >> 2);
        else if (i < 0)
            bufferjitter -= ((2 - i) >> 2);
    }

    // P2P
    if (g_networkBroadcastMode == NETMODE_P2P)
    {
        packbuf[0] = SERVER_GENERATED_BROADCAST;
        if ((g_player[myconnectindex].movefifoend-1) == 0)
            packbuf[0] = PACKET_TYPE_BROADCAST;

        j = 1;

        //Fix timers and buffer/jitter value
        if (((g_player[myconnectindex].movefifoend-1)&(TIMERUPDATESIZ-1)) == 0)
        {
            if (myconnectindex != connecthead)
            {
                i = g_player[connecthead].myminlag-otherminlag;
                if (klabs(i) > 8) i >>= 1;
                else if (klabs(i) > 2) i = ksgn(i);
                else i = 0;

                totalclock -= TICSPERFRAME*i;
                g_player[connecthead].myminlag -= i; otherminlag += i;
            }

            if (myconnectindex == connecthead)
                for(i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                    packbuf[j++] = min(max(g_player[i].myminlag,-128),127);

            for(i=connecthead;i>=0;i=connectpoint2[i])
                g_player[i].myminlag = 0x7fffffff;
        }

        osyn = (input_t *)&inputfifo[(g_player[myconnectindex].movefifoend-2)&(MOVEFIFOSIZ-1)][myconnectindex];
        nsyn = (input_t *)&inputfifo[(g_player[myconnectindex].movefifoend-1)&(MOVEFIFOSIZ-1)][myconnectindex];

        k = j;
        packbuf[j++] = 0;
        packbuf[j++] = 0;

        if (nsyn[0].fvel != osyn[0].fvel)
        {
            packbuf[j++] = (char)nsyn[0].fvel;
            packbuf[j++] = (char)(nsyn[0].fvel>>8);
            packbuf[k] |= 1;
        }
        if (nsyn[0].svel != osyn[0].svel)
        {
            packbuf[j++] = (char)nsyn[0].svel;
            packbuf[j++] = (char)(nsyn[0].svel>>8);
            packbuf[k] |= 2;
        }
        if (nsyn[0].q16avel != osyn[0].q16avel)
        {
            packbuf[j++] = (char)((nsyn[0].q16avel) & 255);
            packbuf[j++] = (char)((nsyn[0].q16avel >> 8) & 255);
            packbuf[j++] = (char)((nsyn[0].q16avel >> 16) & 255);
            packbuf[j++] = (char)((nsyn[0].q16avel >> 24) & 255);
            packbuf[k] |= 4;
        }

        if ((nsyn[0].bits^osyn[0].bits)&0x000000ff) packbuf[j++] = (nsyn[0].bits&255), packbuf[k] |= 8;
        if ((nsyn[0].bits^osyn[0].bits)&0x0000ff00) packbuf[j++] = ((nsyn[0].bits>>8)&255), packbuf[k] |= 16;
        if ((nsyn[0].bits^osyn[0].bits)&0x00ff0000) packbuf[j++] = ((nsyn[0].bits>>16)&255), packbuf[k] |= 32;
        if ((nsyn[0].bits^osyn[0].bits)&0xff000000) packbuf[j++] = ((nsyn[0].bits>>24)&255), packbuf[k] |= 64;

        if (nsyn[0].q16horz != osyn[0].q16horz)
        {
            packbuf[j++] = (char)((nsyn[0].q16horz) & 255);
            packbuf[j++] = (char)((nsyn[0].q16horz >> 8) & 255);
            packbuf[j++] = (char)((nsyn[0].q16horz >> 16) & 255);
            packbuf[j++] = (char)((nsyn[0].q16horz >> 24) & 255);
            packbuf[k] |= 128;
        }
//        k++;
        packbuf[++k] = 0;
        if (nsyn[0].extbits != osyn[0].extbits) packbuf[j++] = nsyn[0].extbits, packbuf[k] |= 1;
        /*        if ((nsyn[0].extbits^osyn[0].extbits)&0x000000ff) packbuf[j++] = (nsyn[0].extbits&255), packbuf[k] |= 1;
                if ((nsyn[0].extbits^osyn[0].extbits)&0x0000ff00) packbuf[j++] = ((nsyn[0].extbits>>8)&255), packbuf[k] |= 2;
                if ((nsyn[0].extbits^osyn[0].extbits)&0x00ff0000) packbuf[j++] = ((nsyn[0].extbits>>16)&255), packbuf[k] |= 4;
                if ((nsyn[0].extbits^osyn[0].extbits)&0xff000000) packbuf[j++] = ((nsyn[0].extbits>>24)&255), packbuf[k] |= 8; */

        Net_AddSyncInfoToPacket(&j);

        TRAVERSE_CONNECT(i)
        if (i != myconnectindex)
            mmulti_sendpacket(i,(unsigned char*)packbuf,j);

        return;
    }

    // Master/Slave
    if (myconnectindex != connecthead)   //Slave
    {
        //Fix timers and buffer/jitter value
        if (((g_player[myconnectindex].movefifoend-1)&(TIMERUPDATESIZ-1)) == 0)
        {
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
        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].movefifoend <= movefifosendplc)
                return;
        }

        osyn = (input_t *)&inputfifo[(movefifosendplc-1)&(MOVEFIFOSIZ-1)][0];
        nsyn = (input_t *)&inputfifo[(movefifosendplc)&(MOVEFIFOSIZ-1)][0];

        //MASTER -> SLAVE packet
        packbuf[0] = PACKET_TYPE_MASTER_TO_SLAVE;
        j = 1;

        TRAVERSE_CONNECT(i)
        {
            packbuf[j++] = i; // Player Index

            //Fix timers and buffer/jitter value 
            packbuf[j++] = min(max(g_player[i].myminlag, -128), 127);
            if ((movefifosendplc & (TIMERUPDATESIZ - 1)) == 0)
            {
                g_player[i].myminlag = 0x7fffffff;
            }

            Net_AddPlayerInputToPacket(&j, i, osyn, nsyn);
        }

        Net_AddSyncInfoToPacket(&j);

        TRAVERSE_CONNECT(i)
            mmulti_sendpacket(i, (unsigned char*)packbuf, j);

        movefifosendplc++;

        // We're the host, terminate the match after sending the packet.
        if (TEST_SYNC_KEY(nsyn[myconnectindex].bits, SK_GAMEQUIT) &&
            (g_networkBroadcastMode == NETMODE_MASTERSLAVE) &&
            (myconnectindex == connecthead))
        {
            G_GameExit(" ");
        }
    }
}

void Net_ParsePackets(void)
{
    int i, j, k;
    int other;
    int packbufleng;

    input_t *osyn, *nsyn;
    
    if (numplayers < 2) return;
    while ((packbufleng = mmulti_getpacket(&other, (unsigned char*)packbuf)) > 0)
    {
        lastpackettime = (int32_t)totalclock;
#if 0
        initprintf("RECEIVED PACKET: type: %d : len %d\n", packbuf[0], packbufleng);
#endif
        switch (packbuf[0])
        {
        case PACKET_TYPE_MASTER_TO_SLAVE:  //[0] (receive master sync buffer)
        {
            j = 1;
            if (myconnectindex == connecthead)
            {
                OSD_Printf("PACKET_TYPE_MASTER_TO_SLAVE: MASTER SHOULDN'T GET THIS PACKET!\n");
            }

            osyn = (input_t*)&inputfifo[(g_player[connecthead].movefifoend - 1) & (MOVEFIFOSIZ - 1)][0];
            nsyn = (input_t*)&inputfifo[(g_player[connecthead].movefifoend) & (MOVEFIFOSIZ - 1)][0];
            TRAVERSE_CONNECT(i)
            {
                int playerNum = packbuf[j++];

                //Fix timers and buffer/jitter value 
                int temp = (int)((signed char)packbuf[j++]);

                if (((g_player[other].movefifoend & (TIMERUPDATESIZ - 1)) == 0) && (playerNum == myconnectindex))
                    otherminlag = temp;

                Net_GetPlayerInputFromPacket(&j, playerNum, osyn, nsyn);
            }

            Net_GetSyncInfoFromPacket(packbuf, packbufleng, &j, other);

            movefifosendplc++;

            break;
        }
        case PACKET_TYPE_SLAVE_TO_MASTER:  //[1] (receive slave sync buffer)
        {
            j = 1;

            osyn = (input_t *)&inputfifo[(g_player[other].movefifoend-1)&(MOVEFIFOSIZ-1)][0];
            nsyn = (input_t *)&inputfifo[(g_player[other].movefifoend)&(MOVEFIFOSIZ-1)][0];

            Net_GetPlayerInputFromPacket(&j, other, osyn, nsyn);
            Net_GetSyncInfoFromPacket(packbuf, packbufleng, &j, other);
            break;
        }
        case PACKET_TYPE_BROADCAST:
            g_player[other].movefifoend = movefifoplc = movefifosendplc = predictfifoplc = 0;
            g_player[other].syncvalhead = syncvaltottail = 0L;
            fallthrough__;
        case SERVER_GENERATED_BROADCAST:
            j = 1;

            if ((g_player[other].movefifoend&(TIMERUPDATESIZ-1)) == 0)
                if (other == connecthead)
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                    {
                        if (i == myconnectindex)
                            otherminlag = (int)((signed char)packbuf[j]);
                        j++;
                    }

            osyn = (input_t *)&inputfifo[(g_player[other].movefifoend-1)&(MOVEFIFOSIZ-1)][0];
            nsyn = (input_t *)&inputfifo[(g_player[other].movefifoend)&(MOVEFIFOSIZ-1)][0];

            copybufbyte(&osyn[other],&nsyn[other],sizeof(input_t));
            k = packbuf[j] + (int)(packbuf[j+1]<<8);
            j += 2;

            if (k&1)   nsyn[other].fvel = packbuf[j]+((short)packbuf[j+1]<<8), j += 2;
            if (k&2)   nsyn[other].svel = packbuf[j]+((short)packbuf[j+1]<<8), j += 2;
            if (k&4)
            {
                nsyn[other].q16avel = (fix16_t)packbuf[j];
                nsyn[other].q16avel += (fix16_t)packbuf[j + 1] << 8;
                nsyn[other].q16avel += (fix16_t)packbuf[j + 2] << 16;
                nsyn[other].q16avel += (fix16_t)packbuf[j + 3] << 24;
                j += 4;
            }
            if (k&8)   nsyn[other].bits = ((nsyn[other].bits&0xffffff00)|((int)packbuf[j++]));
            if (k&16)  nsyn[other].bits = ((nsyn[other].bits&0xffff00ff)|((int)packbuf[j++])<<8);
            if (k&32)  nsyn[other].bits = ((nsyn[other].bits&0xff00ffff)|((int)packbuf[j++])<<16);
            if (k&64)  nsyn[other].bits = ((nsyn[other].bits&0x00ffffff)|((int)packbuf[j++])<<24);
            if (k&128)
            {
                nsyn[other].q16horz = (fix16_t)packbuf[j];
                nsyn[other].q16horz += (fix16_t)packbuf[j + 1] << 8;
                nsyn[other].q16horz += (fix16_t)packbuf[j + 2] << 16;
                nsyn[other].q16horz += (fix16_t)packbuf[j + 3] << 24;
                j += 4;
            }

            if (k&256) nsyn[other].extbits = (unsigned char)packbuf[j++];
            /*            if (k&256)  nsyn[other].extbits = ((nsyn[other].extbits&0xffffff00)|((int)packbuf[j++]));
                        if (k&512)  nsyn[other].extbits = ((nsyn[other].extbits&0xffff00ff)|((int)packbuf[j++])<<8);
                        if (k&1024) nsyn[other].extbits = ((nsyn[other].extbits&0xff00ffff)|((int)packbuf[j++])<<16);
                        if (k&2048) nsyn[other].extbits = ((nsyn[other].extbits&0x00ffffff)|((int)packbuf[j++])<<24); */
            g_player[other].movefifoend++;

            /*
                        while (j < packbufleng)
                        {
                            g_player[other].syncval[g_player[other].syncvalhead&(MOVEFIFOSIZ-1)] = packbuf[j++];
                            g_player[other].syncvalhead++;
                        }
                        */
            Net_GetSyncInfoFromPacket(packbuf, packbufleng, &j, other);

            if (j > packbufleng)
                initprintf("INVALID GAME PACKET!!! (packet %d, %d too many bytes (%d %d))\n",packbuf[0],j-packbufleng,packbufleng,k);

            break;

        case PACKET_TYPE_NULL_PACKET:
            break;

        case PACKET_TYPE_PLAYER_READY:
            if (g_player[other].playerreadyflag == 0)
                initprintf("Player %d is ready\n", other);
            g_player[other].playerreadyflag++;
            return;
        case PACKET_TYPE_QUIT:
            G_GameExit(" ");
            break;
        default:
            switch (packbuf[0])
            {
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
                //slaves in M/S mode only send to master
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                {
                    if (packbuf[1] == 255)
                    {
                        //Master re-transmits message to all others
                        for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                            if (i != other)
                                mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);
                    }
                    else if (((int)packbuf[1]) != myconnectindex)
                    {
                        //Master re-transmits message not intended for master
                        mmulti_sendpacket((int)packbuf[1], (unsigned char*)packbuf,packbufleng);
                        break;
                    }
                }

                Bstrcpy(recbuf,packbuf+2);
                recbuf[packbufleng-2] = 0;

                G_AddUserQuote(recbuf);
                S_PlaySound(EXITMENUSOUND);

                pus = NUMPAGES;
                pub = NUMPAGES;

                break;

            case PACKET_TYPE_NEW_GAME:
                //Slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                if (vote_map != -1 || vote_episode != -1 || voting != -1)
                    G_AddUserQuote("VOTE SUCCEEDED");

                ud.m_level_number = ud.level_number = packbuf[1];
                ud.m_volume_number = ud.volume_number = packbuf[2];
                ud.m_player_skill = ud.player_skill = packbuf[3];

                // Non-menu variables handled by G_EnterLevel
                ud.m_monsters_off = packbuf[4];
                ud.m_respawn_monsters = packbuf[5];
                ud.m_respawn_items = packbuf[6];
                ud.m_respawn_inventory = packbuf[7];
                ud.m_coop = packbuf[8];
                ud.m_marker = packbuf[9];
                ud.m_ffire = packbuf[10];
                ud.m_noexits = packbuf[11];
                ud.m_weaponstay = packbuf[12];

                TRAVERSE_CONNECT(i)
                {
                    P_ResetWeapons(i);
                    P_ResetInventory(i);
                }

                G_NewGame(ud.volume_number,ud.level_number,ud.player_skill);

                if (G_EnterLevel(MODE_GAME)) G_BackToMenu();

                break;

            case PACKET_TYPE_INIT_SETTINGS:
                //Slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i = connectpoint2[connecthead]; i >= 0; i = connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf, packbufleng);

                ud.m_level_number = ud.level_number = packbuf[1];
                ud.m_volume_number = ud.volume_number = packbuf[2];
                ud.m_player_skill = ud.player_skill = packbuf[3];

                // Non-menu variables handled by G_EnterLevel
                ud.m_monsters_off = packbuf[4];
                ud.m_respawn_monsters = packbuf[5];
                ud.m_respawn_items = packbuf[6];
                ud.m_respawn_inventory = packbuf[7];
                ud.m_coop = packbuf[8];
                ud.m_marker = packbuf[9];
                ud.m_ffire = packbuf[10];
                ud.m_noexits = packbuf[11];
                ud.m_weaponstay = packbuf[12];
                ud.warp_on = packbuf[13];
                ud.allow_visibility = packbuf[14];

                if(ud.allow_visibility)
                    initprintf("Visibility adjustment enabled for multiplayer.\n");

                break;

            case PACKET_TYPE_VERSION:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                if (packbuf[2] != (char)atoi(s_buildRev))
                {
                    initprintf("Player has version %d, expecting %d\n",packbuf[2],(char)atoi(s_buildRev));
                    G_GameExit("You cannot play with different versions of EDuke32!");
                }
                if (packbuf[3] != (char)BYTEVERSION)
                {
                    initprintf("Player has version %d, expecting %d (%d, %d, %d)\n",packbuf[3],BYTEVERSION, BYTEVERSION_JF, PLUTOPAK, VOLUMEONE);
                    G_GameExit("You cannot play Duke with different versions!");
                }
                if (packbuf[4] > g_numSyncBytes)
                {
                    initprintf("Sync debugging enabled\n");
                    g_numSyncBytes = packbuf[4];
                }

                break;

            case PACKET_TYPE_PLAYER_OPTIONS:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                other = packbuf[1];
                i = 2;

                g_player[other].ps->aim_mode = packbuf[i++];
                g_player[other].ps->auto_aim = packbuf[i++];
                g_player[other].ps->weaponswitch = packbuf[i++];
                g_player[other].ps->palookup = g_player[other].pcolor = playerColor_getValidPal(packbuf[i++]);
                g_player[other].pteam = packbuf[i++];

                break;

            case PACKET_TYPE_FRAGLIMIT_CHANGED:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i = connectpoint2[connecthead];i >= 0;i = connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf, packbufleng);

                ud.fraglimit = packbuf[2];

                Bsprintf(tempbuf, "FRAGLIMIT CHANGED TO %d", ud.fraglimit);
                G_AddUserQuote(tempbuf);
                break;

            case PACKET_TYPE_EOL:
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                TRAVERSE_CONNECT(i)
                g_player[i].ps->gm = MODE_EOL;

                ud.level_number = packbuf[1];
                ud.m_level_number = ud.level_number;
                ud.from_bonus = packbuf[2];
                break;

            case PACKET_TYPE_PLAYER_NAME:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                other = packbuf[1];

                for (i=2;packbuf[i];i++)
                    g_player[other].user_name[i-2] = packbuf[i];
                g_player[other].user_name[i-2] = 0;
                i++;

                initprintf("Player %d's name is now %s\n", other, g_player[other].user_name);

                break;

            case PACKET_TYPE_WEAPON_CHOICE:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                other = packbuf[1];

                i = 2;

                j = i; //This used to be Duke packet #9... now concatenated with Duke packet #6
                for (;i-j<10;i++) g_player[other].wchoice[i-j] = packbuf[i];

                break;
            case PACKET_TYPE_RTS:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                if (rts_numlumps == 0) break;

                if (ud.config.SoundToggle == 0 || ud.lockout == 1 || ud.config.FXDevice < 0 || !(ud.config.VoiceToggle & 4))
                    break;
                rtsptr = (char *)RTS_GetSound(packbuf[1]-1);
                FX_Play3D(rtsptr, RTS_SoundLength(packbuf[1] - 1), FX_ONESHOT, 0, 0, 1, 255, fix16_one, -packbuf[1]);
                g_RTSPlaying = 7;
                break;

            case PACKET_TYPE_MENU_LEVEL_QUIT:
                //slaves in M/S mode only send to master
                if (myconnectindex == connecthead)
                {
                    //Master re-transmits message to all others
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other)
                            mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);
                }

                G_GameExit("Game aborted from menu; disconnected.");

                break;

            case PACKET_TYPE_USER_MAP:
                //slaves in M/S mode only send to master
                if (myconnectindex == connecthead)
                {
                    //Master re-transmits message to all others
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other)
                            mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);
                }

                Bstrcpy(boardfilename,packbuf+1);
                boardfilename[packbufleng-1] = 0;
                Bcorrectfilename(boardfilename,0);
                if (boardfilename[0] != 0)
                {
                    if ((i = kopen4loadfrommod(boardfilename,0)) < 0)
                    {
                        Bmemset(boardfilename,0,sizeof(boardfilename));
                        Net_SendUserMapName();
                    }
                    else kclose(i);
                }

                if (ud.m_level_number == 7 && ud.m_volume_number == 0 && boardfilename[0] == 0)
                    ud.m_level_number = 0;

                break;

            case PACKET_TYPE_MAP_VOTE:
            case PACKET_TYPE_MAP_VOTE_INITIATE:
            case PACKET_TYPE_MAP_VOTE_CANCEL:

                if (myconnectindex == connecthead)
                {
                    //Master re-transmits message to all others
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other)
                            mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);
                }

                switch (packbuf[0])
                {
                case PACKET_TYPE_MAP_VOTE:
                    if (voting == myconnectindex && g_player[(unsigned char)packbuf[1]].gotvote == 0)
                    {
                        g_player[(unsigned char)packbuf[1]].gotvote = 1;
                        g_player[(unsigned char)packbuf[1]].vote = packbuf[2];
                        Bsprintf(tempbuf,"CONFIRMED VOTE FROM %s",g_player[(unsigned char)packbuf[1]].user_name);
                        G_AddUserQuote(tempbuf);
                    }
                    break;

                case PACKET_TYPE_MAP_VOTE_INITIATE: // call map vote
                    /*                    if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && packbuf[1] == connecthead)
                                            break; // ignore this from master */
                    voting = packbuf[1];
                    vote_episode = packbuf[2];
                    vote_map = packbuf[3];

                    Bsprintf(tempbuf,"%s^00 HAS CALLED A VOTE TO CHANGE MAP TO %s (E%dL%d)",
                             g_player[(unsigned char)packbuf[1]].user_name,
                             MapInfo[(unsigned char)(packbuf[2]*MAXLEVELS + packbuf[3])].name,
                             packbuf[2]+1,packbuf[3]+1);
                    G_AddUserQuote(tempbuf);

                    Bsprintf(tempbuf,"PRESS F1 TO ACCEPT, F2 TO DECLINE");
                    G_AddUserQuote(tempbuf);

                    for (i=MAXPLAYERS-1;i>=0;i--)
                    {
                        g_player[i].vote = 0;
                        g_player[i].gotvote = 0;
                    }
                    g_player[voting].gotvote = g_player[voting].vote = 1;
                    break;

                case PACKET_TYPE_MAP_VOTE_CANCEL: // cancel map vote
                    if (voting == packbuf[1])
                    {
                        voting = -1;
                        i = 0;
                        for (j=MAXPLAYERS-1;j>=0;j--)
                            i += g_player[j].gotvote;

                        if (i != numplayers)
                            Bsprintf(tempbuf,"%s^00 HAS CANCELED THE VOTE",g_player[(unsigned char)packbuf[1]].user_name);
                        else Bsprintf(tempbuf,"VOTE FAILED");
                        for (i=MAXPLAYERS-1;i>=0;i--)
                        {
                            g_player[i].vote = 0;
                            g_player[i].gotvote = 0;
                        }
                        G_AddUserQuote(tempbuf);
                    }
                    break;
                }
                break;

            case PACKET_TYPE_LOAD_GAME:
                //Slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                multiflag = 2;
                multiwhat = 0;
                multiwho = packbuf[2]; //other: need to send in m/s mode because of possible re-transmit
                multipos = packbuf[1];
                G_LoadPlayer(multipos);
                multiflag = 0;
                break;
            }

            break;
        }
    }
}

void Net_SendQuit(void)
{
    if (g_gameQuit == 0 && (numplayers > 1))
    {
        if (g_player[myconnectindex].ps->gm&MODE_GAME)
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
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
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
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
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
    buf[4] = g_numSyncBytes;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],5);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
    }
    waitforeverybody();
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

    buf[l++] = g_player[myconnectindex].ps->aim_mode = ud.mouseaiming;
    buf[l++] = g_player[myconnectindex].ps->auto_aim = ud.config.AutoAim;
    buf[l++] = g_player[myconnectindex].ps->weaponswitch = ud.weaponswitch;
    buf[l++] = g_player[myconnectindex].ps->palookup = g_player[myconnectindex].pcolor = playerColor_getValidPal(ud.color);

    buf[l++] = g_player[myconnectindex].pteam = ud.team;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],l);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
    }
}

void Net_SendFragLimit(void)
{
    int i;
    if (myconnectindex == connecthead)
    {
        buf[0] = PACKET_TYPE_FRAGLIMIT_CHANGED;
        buf[1] = myconnectindex;
        buf[2] = ud.fraglimit;

        TRAVERSE_CONNECT(i)
        {
            if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)buf, 3);
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
        }
    }
}

void Net_SendPlayerName(void)
{
    int i,l;

    for (l=0;(unsigned)l<sizeof(szPlayerName)-1;l++)
        g_player[myconnectindex].user_name[l] = Btoupper(szPlayerName[l]);

    if (numplayers < 2) return;

    buf[0] = PACKET_TYPE_PLAYER_NAME;
    buf[1] = myconnectindex;
    l = 2;

    //null terminated player name to send
    for (i=0;szPlayerName[i];i++) buf[l++] = Btoupper(szPlayerName[i]);
    buf[l++] = 0;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)&buf[0],l);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
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
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
        }
    }
}

void Net_SendInitialSettings(void)
{
    int i;

    if (myconnectindex != connecthead) return; // First player dictates initial settings.

    packbuf[0] = PACKET_TYPE_INIT_SETTINGS;
    packbuf[1] = ud.m_level_number;
    packbuf[2] = ud.m_volume_number;
    packbuf[3] = ud.m_player_skill;
    packbuf[4] = ud.m_monsters_off;
    packbuf[5] = ud.m_respawn_monsters;
    packbuf[6] = ud.m_respawn_items;
    packbuf[7] = ud.m_respawn_inventory;
    packbuf[8] = ud.m_coop;
    packbuf[9] = ud.m_marker;
    packbuf[10] = ud.m_ffire;
    packbuf[11] = ud.m_noexits;
    packbuf[12] = ud.m_weaponstay;
    packbuf[13] = ud.warp_on;
    packbuf[14] = ud.allow_visibility;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 15);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
    }
}

void Net_NewGame(int volume, int level)
{
    int i;

    packbuf[0] = PACKET_TYPE_NEW_GAME;
    packbuf[1] = ud.m_level_number = level;
    packbuf[2] = ud.m_volume_number = volume;
    packbuf[3] = ud.m_player_skill;
    packbuf[4] = ud.m_monsters_off;
    packbuf[5] = ud.m_respawn_monsters;
    packbuf[6] = ud.m_respawn_items;
    packbuf[7] = ud.m_respawn_inventory;
    packbuf[8] = ud.m_coop;
    packbuf[9] = ud.m_marker;
    packbuf[10] = ud.m_ffire;
    packbuf[11] = ud.m_noexits;
    packbuf[12] = ud.m_weaponstay;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf,13);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
    }
}

void Net_EndOfLevel(void)
{
    int i;

    if (myconnectindex != connecthead)
        return;

    g_player[myconnectindex].ps->gm = MODE_EOL;
    if (ud.from_bonus)
    {
        ud.level_number = ud.from_bonus;
        ud.m_level_number = ud.level_number;
        ud.from_bonus = 0;
    }
    else
    {
        ud.level_number++;
        ud.m_level_number = ud.level_number;
    }

    packbuf[0] = PACKET_TYPE_EOL;
    packbuf[1] = ud.level_number;
    packbuf[2] = ud.from_bonus;

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 3);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
    }
}

void Net_EnterMessage(void)
{
    short ch, hitstate, i, j, l;

    if (g_player[myconnectindex].ps->gm&MODE_SENDTOWHOM)
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
                Bstrcat(recbuf,"* ");
                i = 3, j = Bstrlen(typebuf);
                Bstrcpy(tempbuf,typebuf);
                while (i < j)
                {
                    typebuf[i-3] = tempbuf[i];
                    i++;
                }
                typebuf[i-3] = '\0';
                Bstrcat(recbuf,g_player[myconnectindex].user_name);
            }
            else
            {
                Bstrcat(recbuf,g_player[myconnectindex].user_name);
                Bstrcat(recbuf,": ");
            }

            Bstrcat(recbuf,"^00");
            Bstrcat(recbuf,typebuf);
            j = Bstrlen(recbuf);
            recbuf[j] = 0;
            Bstrcat(tempbuf+2,recbuf);

            if (g_chatPlayer >= ud.multimode)
            {
                tempbuf[1] = 255;
                TRAVERSE_CONNECT(ch)
                {
                    if (ch != myconnectindex) mmulti_sendpacket(ch, (unsigned char*)tempbuf,j+2);
                    if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
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
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead))
                    g_chatPlayer = connecthead;
                mmulti_sendpacket(g_chatPlayer, (unsigned char*)tempbuf,j+2);
            }

            g_chatPlayer = -1;
            g_player[myconnectindex].ps->gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
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
                        g_player[myconnectindex].ps->gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
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
                g_player[myconnectindex].ps->gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
                return;
            }
            if (ud.automsg)
            {
                if (SHIFTS_IS_PRESSED) g_chatPlayer = -1;
                else g_chatPlayer = ud.multimode;
            }
            g_player[myconnectindex].ps->gm |= MODE_SENDTOWHOM;
        }
        else if (hitstate == -1)
            g_player[myconnectindex].ps->gm &= ~(MODE_TYPE|MODE_SENDTOWHOM);
        else pub = NUMPAGES;
    }
}

void Net_CorrectPrediction(void)
{
    int i;
    DukePlayer_t *p;

    if (numplayers < 2) return;

    i = ((movefifoplc-1)&(MOVEFIFOSIZ-1));
    p = g_player[myconnectindex].ps;

    // HACK: Don't correct prediction if we're dead. Fixes "wackedbyactor" cam.
    if (p->wackedbyactor >= 0 || sprite[p->i].extra <= 0)
        return;

    if (p->pos.x == myposbak[i].x && p->pos.y == myposbak[i].y && p->pos.z == myposbak[i].z
            && p->q16horiz == myhorizbak[i] && p->q16ang == myangbak[i]) return;

    if (ud.config.PredictionDebug)
    {
        initprintf("Prediction Mismatch - ");
        if (p->pos.x != myposbak[i].x)
        {
            initprintf("^01xpos^12(^02%d^12/^08%d^12) ", myposbak[i].x, p->pos.x);
        }
        if (p->pos.y != myposbak[i].y)
        {
            initprintf("^01ypos^12(^02%d^12/^08%d^12) ", myposbak[i].y, p->pos.y);
        }
        if (p->pos.z != myposbak[i].z)
        {
            initprintf("^01zpos^12(^02%d^12/^08%d^12) ", myposbak[i].z, p->pos.z);
        }
        if (p->q16horiz != myhorizbak[i])
        {
            initprintf("^01q16horiz^12(^02%d^12/^08%d^12) ", myhorizbak[i], p->q16horiz);
        }
        if (p->q16ang != myangbak[i])
        {
            initprintf("^01q16ang^12(^02%d^12/^08%d^12) ", myangbak[i], p->q16ang);
        }
        initprintf("\n");
    }

    predictedPlayer.pos = p->pos;
    predictedPlayer.opos = p->opos;
    predictedPlayer.vel = p->vel;

    predictedPlayer.q16ang = p->q16ang;
    predictedPlayer.oq16ang = p->oq16ang;
    predictedPlayer.cursectnum = p->cursectnum;
    predictedPlayer.q16horiz = p->q16horiz;
    predictedPlayer.oq16horiz = p->oq16horiz;
    predictedPlayer.q16horizoff = p->q16horizoff;
    predictedPlayer.oq16horizoff = p->oq16horizoff;
    predictedPlayer.jumping_counter = p->jumping_counter;
    predictedPlayer.jumping_toggle = p->jumping_toggle;
    predictedPlayer.on_ground = p->on_ground;
    predictedPlayer.hard_landing = p->hard_landing;
    predictedPlayer.return_to_center = p->return_to_center;

    predictfifoplc = movefifoplc;
    while (predictfifoplc < g_player[myconnectindex].movefifoend)
        Net_DoPrediction();
}

void Net_ResetPrediction(void)
{
    if (g_player[myconnectindex].ps == NULL)
        return;

    predictedPlayer = *g_player[myconnectindex].ps;
}

void Net_DoPrediction(void)
{
    DukePlayer_t* p = g_player[myconnectindex].ps;
    spritetype backupSprite = sprite[p->i];
    int32_t backupSeed = randomseed;

    oldnet_predicting = 1;
    sprite[p->i].cstat &= ~257;

    X_OnEvent(EVENT_FAKEDOMOVETHINGS, g_player[myconnectindex].ps->i, myconnectindex, -1);
    P_ProcessInput(myconnectindex);

    sprite[p->i] = backupSprite;
    randomseed = backupSeed;
    oldnet_predicting = 0;

    myposbak[predictfifoplc&(MOVEFIFOSIZ-1)].x = predictedPlayer.pos.x;
    myposbak[predictfifoplc&(MOVEFIFOSIZ-1)].y = predictedPlayer.pos.y;
    myposbak[predictfifoplc&(MOVEFIFOSIZ-1)].z = predictedPlayer.pos.z;
    myangbak[predictfifoplc&(MOVEFIFOSIZ-1)] = predictedPlayer.q16ang;
    myhorizbak[predictfifoplc&(MOVEFIFOSIZ-1)] = predictedPlayer.q16horiz;
    predictfifoplc++;
}

void Net_ClearFIFO(void)
{
    int i = 0;

    syncvaltail = 0L;
    syncvaltottail = 0L;
    memset(&syncstat, 0, sizeof(syncstat));
    memset(&g_szfirstSyncMsg, 0, sizeof(g_szfirstSyncMsg));
    g_foundSyncError = 0;

    bufferjitter = 1;
    mymaxlag = otherminlag = 0;
    movefifoplc = movefifosendplc = predictfifoplc = 0;

    for (; i < MAXPLAYERS; i++)
    {
        Bmemset(&g_player[i].movefifoend, 0, sizeof(g_player[i].movefifoend));
        Bmemset(&g_player[i].syncvalhead, 0, sizeof(g_player[i].syncvalhead));
        Bmemset(&g_player[i].myminlag, 0, sizeof(g_player[i].myminlag));
    }
}

void waitforeverybody(void)
{
    int i;

    if (numplayers < 2)
        return;

    packbuf[0] = PACKET_TYPE_PLAYER_READY;

    g_player[myconnectindex].playerreadyflag++;

    // if we're a peer or slave, not a master
    if ((g_networkBroadcastMode == NETMODE_P2P) || ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)))
    {
        TRAVERSE_CONNECT(i)
        {
            if (i != myconnectindex)
                mmulti_sendpacket(i, (unsigned char*)packbuf, 1);

            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead))
                break; //slaves in M/S mode only send to master
        }
    }

    P_SetGamePalette(g_player[myconnectindex].ps, TITLEPAL, 11);

    while (1)
    {
        //if (quitevent) G_GameExit(""); // This sucks

        gameHandleEvents();
        videoClearScreen(0);

        rotatesprite(0, 0, 65536L, 0, BETASCREEN, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(160 << 16, (104) << 16, 60 << 10, 0, DUKENUKEM, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(160 << 16, (129) << 16, 30 << 11, 0, THREEDEE, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        if (PLUTOPAK)   // JBF 20030804
            rotatesprite(160 << 16, (151) << 16, 30 << 11, 0, PLUTOPAKSPRITE + 1, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);

        gametext(160, 190, "WAITING FOR PLAYERS", 14, 2);

        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
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
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead))
            {
                // we're a slave
                i = -1;
                break;
            }
        }

        // -1 Means we iterated through all players without breaking in above loop. All players ready.
        if (i <= -1)
        {
            // master sends ready packet once it hears from all slaves
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && myconnectindex == connecthead)
            {
                TRAVERSE_CONNECT(i)
                {
                    packbuf[0] = PACKET_TYPE_PLAYER_READY;

                    if (i != myconnectindex)
                        mmulti_sendpacket(i, (unsigned char*)packbuf, 1);
                }
            }

            P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 11);
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
            if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf,1);
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
        }
    }
}