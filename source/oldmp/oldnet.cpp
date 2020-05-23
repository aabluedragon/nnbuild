#include "duke3d.h"
//#include "oldnet.h"

#define TIMERUPDATESIZ 32

int quittimer = 0;
int lastpackettime = 0;
int mymaxlag, otherminlag, bufferjitter = 1;
int voting = -1, vote_map = -1, vote_episode = -1;

int avgfvel, avgsvel, avgbits, avgextbits;
fix16_t avghorz;
fix16_t avgavel;

int movefifosendplc;
int movefifoplc;

// Prediction Vars
int predictfifoplc;
int myx, omyx, myxvel, myy, omyy, myyvel, myz, omyz, myzvel;
fix16_t myhoriz, omyhoriz, myhorizoff, omyhorizoff;
fix16_t myang, omyang;
int16_t mycursectnum, myjumpingcounter;
char myjumpingtoggle, myonground, myhardlanding, myreturntocenter;

int myxbak[MOVEFIFOSIZ], myybak[MOVEFIFOSIZ], myzbak[MOVEFIFOSIZ];
fix16_t myhorizbak[MOVEFIFOSIZ], myangbak[MOVEFIFOSIZ];

static int g_chatPlayer = -1;
static char recbuf[180];

extern int qe,cp;
extern char *rtsptr; // game.c
extern void computergetinput(int snum, input_t *syn);

int pingTime;

void faketimerhandler(void)
{
    int i, j, k;
    //    short who;
    input_t *osyn, *nsyn;

    if (qe == 0 && KB_KeyPressed(sc_LeftControl) && KB_KeyPressed(sc_LeftAlt) && KB_KeyPressed(sc_Delete))
    {
        qe = 1;
        G_GameExit("Quick Exit.");
    }

    timerUpdateClock();
    MUSIC_Update();

    if ((totalclock < ototalclock+TICSPERFRAME) || (ready2send == 0)) return;
    ototalclock += TICSPERFRAME;

    getpackets();

//    if (mmulti_getoutputcirclesize() >= 16) return;

//    for (i=connecthead;i>=0;i=connectpoint2[i])
//        if (i != myconnectindex)
//            if (g_player[i].movefifoend < g_player[myconnectindex].movefifoend-200) return;

    if (g_player[myconnectindex].movefifoend - movefifoplc >= 100)
        return;

    getinput(myconnectindex);

    avgfvel += loc.fvel;
    avgsvel += loc.svel;
    avgavel += loc.q16avel;
    avghorz += loc.q16horz;
    avgbits |= loc.bits;
    avgextbits |= loc.extbits;
    if (g_player[myconnectindex].movefifoend&(g_movesPerPacket-1))
    {
        copybufbyte(&inputfifo[(g_player[myconnectindex].movefifoend-1)&(MOVEFIFOSIZ-1)][myconnectindex],
                    &inputfifo[g_player[myconnectindex].movefifoend&(MOVEFIFOSIZ-1)][myconnectindex],sizeof(input_t));
        g_player[myconnectindex].movefifoend++;
        return;
    }
    nsyn = &inputfifo[g_player[myconnectindex].movefifoend&(MOVEFIFOSIZ-1)][myconnectindex];
    nsyn[0].fvel = avgfvel/g_movesPerPacket;
    nsyn[0].svel = avgsvel/g_movesPerPacket;
    nsyn[0].q16avel = avgavel/g_movesPerPacket;
    nsyn[0].q16horz = avghorz/g_movesPerPacket;
    nsyn[0].bits = avgbits;
    nsyn[0].extbits = avgextbits;
    avgfvel = avgsvel = avgavel = avghorz = avgbits = avgextbits = 0;
    g_player[myconnectindex].movefifoend++;

    if (numplayers < 2)
    {
        if (ud.multimode > 1)
        {
            TRAVERSE_CONNECT(i)
            {
                if (i != myconnectindex)
                {
                    //clearbufbyte(&inputfifo[g_player[i].movefifoend&(MOVEFIFOSIZ-1)][i],sizeof(input_t),0L);
                    if (ud.playerai)
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
#if 0
    if (((g_player[myconnectindex].movefifoend - 1) & (TIMERUPDATESIZ - 1)) == 0)
    {
        i = mymaxlag - bufferjitter;
        mymaxlag = 0;
        if (i > 0)
            bufferjitter += ((2 + i) >> 2);
        else if (i < 0)
            bufferjitter -= ((2 - i) >> 2);
    }
#else
    if (((g_player[myconnectindex].movefifoend-1)&(TIMERUPDATESIZ-1)) == 0)
    {
        i = mymaxlag-bufferjitter; mymaxlag = 0;
        if (i > 0) bufferjitter += ((3+i)>>2);
        else if (i < 0) bufferjitter -= ((1-i)>>2);
    }
#endif

    if (g_networkBroadcastMode == NETMODE_P2P)
    {
        packbuf[0] = SERVER_GENERATED_BROADCAST;
        if ((g_player[myconnectindex].movefifoend-1) == 0) packbuf[0] = PACKET_TYPE_BROADCAST;
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

        /*        while (g_player[myconnectindex].syncvalhead != syncvaltail)
                {
                    packbuf[j++] = g_player[myconnectindex].syncval[syncvaltail&(MOVEFIFOSIZ-1)];
                    syncvaltail++;
                } */

        Net_AddSyncInfoToPacket(&j);

        TRAVERSE_CONNECT(i)
        if (i != myconnectindex)
            mmulti_sendpacket(i,(unsigned char*)packbuf,j);

        return;
    }
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
        packbuf[1] = 0;
        packbuf[2] = 0;
        j = 3;

        // PING
        packbuf[j++] = (char)((pingTime) & 255);
        packbuf[j++] = (char)((pingTime >> 8) & 255);
        packbuf[j++] = (char)((pingTime >> 16) & 255);
        packbuf[j++] = (char)((pingTime >> 24) & 255);

        osyn = (input_t *)&inputfifo[(g_player[myconnectindex].movefifoend-2)&(MOVEFIFOSIZ-1)][myconnectindex];
        nsyn = (input_t *)&inputfifo[(g_player[myconnectindex].movefifoend-1)&(MOVEFIFOSIZ-1)][myconnectindex];

        if (nsyn[0].fvel != osyn[0].fvel)
        {
            packbuf[j++] = (char)nsyn[0].fvel;
            packbuf[j++] = (char)(nsyn[0].fvel>>8);
            packbuf[1] |= 1;
        }
        if (nsyn[0].svel != osyn[0].svel)
        {
            packbuf[j++] = (char)nsyn[0].svel;
            packbuf[j++] = (char)(nsyn[0].svel>>8);
            packbuf[1] |= 2;
        }
        if (nsyn[0].q16avel != osyn[0].q16avel)
        {
            packbuf[j++] = (char)((nsyn[0].q16avel) & 255);
            packbuf[j++] = (char)((nsyn[0].q16avel >> 8) & 255);
            packbuf[j++] = (char)((nsyn[0].q16avel >> 16) & 255);
            packbuf[j++] = (char)((nsyn[0].q16avel >> 24) & 255);
            packbuf[1] |= 4;
        }

        if ((nsyn[0].bits^osyn[0].bits)&0x000000ff) packbuf[j++] = (nsyn[0].bits&255), packbuf[1] |= 8;
        if ((nsyn[0].bits^osyn[0].bits)&0x0000ff00) packbuf[j++] = ((nsyn[0].bits>>8)&255), packbuf[1] |= 16;
        if ((nsyn[0].bits^osyn[0].bits)&0x00ff0000) packbuf[j++] = ((nsyn[0].bits>>16)&255), packbuf[1] |= 32;
        if ((nsyn[0].bits^osyn[0].bits)&0xff000000) packbuf[j++] = ((nsyn[0].bits>>24)&255), packbuf[1] |= 64;

        if (nsyn[0].q16horz != osyn[0].q16horz)
        {
            packbuf[j++] = (char)((nsyn[0].q16horz) & 255);
            packbuf[j++] = (char)((nsyn[0].q16horz >> 8) & 255);
            packbuf[j++] = (char)((nsyn[0].q16horz >> 16) & 255);
            packbuf[j++] = (char)((nsyn[0].q16horz >> 24) & 255);
            packbuf[1] |= 128;
        }
        packbuf[2] = 0;
        if (nsyn[0].extbits != osyn[0].extbits) packbuf[j++] = nsyn[0].extbits, packbuf[2] |= 1;
        /*        if ((nsyn[0].extbits^osyn[0].extbits)&0x000000ff) packbuf[j++] = (nsyn[0].extbits&255), packbuf[2] |= 1;
                if ((nsyn[0].extbits^osyn[0].extbits)&0x0000ff00) packbuf[j++] = ((nsyn[0].extbits>>8)&255), packbuf[2] |= 2;
                if ((nsyn[0].extbits^osyn[0].extbits)&0x00ff0000) packbuf[j++] = ((nsyn[0].extbits>>16)&255), packbuf[2] |= 4;
                if ((nsyn[0].extbits^osyn[0].extbits)&0xff000000) packbuf[j++] = ((nsyn[0].extbits>>24)&255), packbuf[2] |= 8; */

        /*        while (g_player[myconnectindex].syncvalhead != syncvaltail)
                {
                    packbuf[j++] = g_player[myconnectindex].syncval[syncvaltail&(MOVEFIFOSIZ-1)];
                    syncvaltail++;
                } */
        Net_AddSyncInfoToPacket(&j);

        mmulti_sendpacket(connecthead, (unsigned char*)packbuf,j);
        return;
    }

    //This allows allow packet resends
    TRAVERSE_CONNECT(i)
    if (g_player[i].movefifoend <= movefifosendplc)
    {
        packbuf[0] = PACKET_TYPE_NULL_PACKET;
        for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
            mmulti_sendpacket(i, (unsigned char*)packbuf,1);
        return;
    }

    while (1)  //Master
    {
        TRAVERSE_CONNECT(i)
        if (g_player[i].playerquitflag && (g_player[i].movefifoend <= movefifosendplc)) return;

        osyn = (input_t *)&inputfifo[(movefifosendplc-1)&(MOVEFIFOSIZ-1)][0];
        nsyn = (input_t *)&inputfifo[(movefifosendplc)&(MOVEFIFOSIZ-1)][0];

        //MASTER -> SLAVE packet
        packbuf[0] = PACKET_TYPE_MASTER_TO_SLAVE;
        j = 1;

        // PING
        pingTime = timerGetTicks();
        packbuf[j++] = (char)((pingTime) & 255);
        packbuf[j++] = (char)((pingTime >> 8) & 255);
        packbuf[j++] = (char)((pingTime >> 16) & 255);
        packbuf[j++] = (char)((pingTime >> 24) & 255);

        //Fix timers and buffer/jitter value
        if ((movefifosendplc&(TIMERUPDATESIZ-1)) == 0)
        {
            for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                if (g_player[i].playerquitflag)
                    packbuf[j++] = min(max(g_player[i].myminlag,-128),127);

            TRAVERSE_CONNECT(i)
            g_player[i].myminlag = 0x7fffffff;
        }

        k = j;

        TRAVERSE_CONNECT(i)
        {
            j += g_player[i].playerquitflag + g_player[i].playerquitflag;

            packbuf[j++] = (char)((g_player[i].ping) & 255);
            packbuf[j++] = (char)((g_player[i].ping >> 8) & 255);
            packbuf[j++] = (char)((g_player[i].ping >> 16) & 255);
            packbuf[j++] = (char)((g_player[i].ping >> 24) & 255);
        }

        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].playerquitflag == 0) continue;

            packbuf[k] = 0;
            if (nsyn[i].fvel != osyn[i].fvel)
            {
                packbuf[j++] = (char)nsyn[i].fvel;
                packbuf[j++] = (char)(nsyn[i].fvel>>8);
                packbuf[k] |= 1;
            }
            if (nsyn[i].svel != osyn[i].svel)
            {
                packbuf[j++] = (char)nsyn[i].svel;
                packbuf[j++] = (char)(nsyn[i].svel>>8);
                packbuf[k] |= 2;
            }
            if (nsyn[i].q16avel != osyn[i].q16avel)
            {
                packbuf[j++] = (char)((nsyn[i].q16avel) & 255);
                packbuf[j++] = (char)((nsyn[i].q16avel >> 8) & 255);
                packbuf[j++] = (char)((nsyn[i].q16avel >> 16) & 255);
                packbuf[j++] = (char)((nsyn[i].q16avel >> 24) & 255);
                packbuf[k] |= 4;
            }

            if ((nsyn[i].bits^osyn[i].bits)&0x000000ff) packbuf[j++] = (nsyn[i].bits&255), packbuf[k] |= 8;
            if ((nsyn[i].bits^osyn[i].bits)&0x0000ff00) packbuf[j++] = ((nsyn[i].bits>>8)&255), packbuf[k] |= 16;
            if ((nsyn[i].bits^osyn[i].bits)&0x00ff0000) packbuf[j++] = ((nsyn[i].bits>>16)&255), packbuf[k] |= 32;
            if ((nsyn[i].bits^osyn[i].bits)&0xff000000) packbuf[j++] = ((nsyn[i].bits>>24)&255), packbuf[k] |= 64;

            if (nsyn[i].q16horz != osyn[i].q16horz)
            {
                packbuf[j++] = (char)((nsyn[i].q16horz) & 255);
                packbuf[j++] = (char)((nsyn[i].q16horz >> 8) & 255);
                packbuf[j++] = (char)((nsyn[i].q16horz >> 16) & 255);
                packbuf[j++] = (char)((nsyn[i].q16horz >> 24) & 255);
                packbuf[k] |= 128;
            }
            k++;
            packbuf[k] = 0;
            if (nsyn[i].extbits != osyn[i].extbits) packbuf[j++] = nsyn[i].extbits, packbuf[k] |= 1;
            /*
            if ((nsyn[i].extbits^osyn[i].extbits)&0x000000ff) packbuf[j++] = (nsyn[i].extbits&255), packbuf[k] |= 1;
            if ((nsyn[i].extbits^osyn[i].extbits)&0x0000ff00) packbuf[j++] = ((nsyn[i].extbits>>8)&255), packbuf[k] |= 2;
            if ((nsyn[i].extbits^osyn[i].extbits)&0x00ff0000) packbuf[j++] = ((nsyn[i].extbits>>16)&255), packbuf[k] |= 4;
            if ((nsyn[i].extbits^osyn[i].extbits)&0xff000000) packbuf[j++] = ((nsyn[i].extbits>>24)&255), packbuf[k] |= 8; */
            k++;
        }

        /*        while (g_player[myconnectindex].syncvalhead != syncvaltail)
                {
                    packbuf[j++] = g_player[myconnectindex].syncval[syncvaltail&(MOVEFIFOSIZ-1)];
                    syncvaltail++;
                } */
        Net_AddSyncInfoToPacket(&j);

        for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
            if (g_player[i].playerquitflag)
            {
                mmulti_sendpacket(i, (unsigned char*)packbuf,j);
                if (TEST_SYNC_KEY(nsyn[i].bits,SK_GAMEQUIT))
                    g_player[i].playerquitflag = 0;
            }

        movefifosendplc += g_movesPerPacket;
    }
}

void Net_ParsePackets(void)
{
    int i, j, k, l;
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
            j = 1;

            // PING
            pingTime = (int32_t)packbuf[j];
            pingTime += (int32_t)packbuf[j + 1] << 8;
            pingTime += (int32_t)packbuf[j + 2] << 16;
            pingTime += (int32_t)packbuf[j + 3] << 24;
            j += 4;

            if ((g_player[other].movefifoend&(TIMERUPDATESIZ-1)) == 0)
                for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                {
                    if (g_player[i].playerquitflag == 0) continue;
                    if (i == myconnectindex)
                        otherminlag = (int)((signed char)packbuf[j]);
                    j++;
                }

            osyn = (input_t *)&inputfifo[(g_player[connecthead].movefifoend-1)&(MOVEFIFOSIZ-1)][0];
            nsyn = (input_t *)&inputfifo[(g_player[connecthead].movefifoend)&(MOVEFIFOSIZ-1)][0];

            k = j;
            TRAVERSE_CONNECT(i)
            {
                j += g_player[i].playerquitflag + g_player[i].playerquitflag;

                g_player[i].ping = (int32_t)packbuf[j];
                g_player[i].ping += (int32_t)packbuf[j + 1] << 8;
                g_player[i].ping += (int32_t)packbuf[j + 2] << 16;
                g_player[i].ping += (int32_t)packbuf[j + 3] << 24;
                j += 4;
            }

            TRAVERSE_CONNECT(i)
            {
                if (g_player[i].playerquitflag == 0) continue;

                l = packbuf[k]+(int)(packbuf[k+1]<<8);
                k += 2;

                if (i == myconnectindex)
                {
                    //j += ((l&1)<<1)+(l&2)+((l&4)>>2)+((l&8)>>3)+((l&16)>>4)+((l&32)>>5)+((l&64)>>6)+((l&128)>>7)+((l&256)>>8)/*+((l&512)>>9)+((l&1024)>>10)+((l&2048)>>11)*/;

                    if (l & 1) j += 2;
                    if (l & 2) j += 2;
                    if (l & 4) j += 4;
                    if (l & 8) j++;
                    if (l & 16) j++;
                    if (l & 32) j++;
                    if (l & 64) j++;
                    if (l & 128) j += 4;
                    if (l & 256) j++;

                    continue;
                }

                copybufbyte(&osyn[i],&nsyn[i],sizeof(input_t));
                if (l&1)   nsyn[i].fvel = packbuf[j]+((short)packbuf[j+1]<<8), j += 2;
                if (l&2)   nsyn[i].svel = packbuf[j]+((short)packbuf[j+1]<<8), j += 2;
                if (l&4)
                {
                    nsyn[i].q16avel = (fix16_t)packbuf[j];
                    nsyn[i].q16avel += (fix16_t)packbuf[j + 1] << 8;
                    nsyn[i].q16avel += (fix16_t)packbuf[j + 2] << 16;
                    nsyn[i].q16avel += (fix16_t)packbuf[j + 3] << 24;
                    j += 4;
                }
                if (l&8)   nsyn[i].bits = ((nsyn[i].bits&0xffffff00)|((int)packbuf[j++]));
                if (l&16)  nsyn[i].bits = ((nsyn[i].bits&0xffff00ff)|((int)packbuf[j++])<<8);
                if (l&32)  nsyn[i].bits = ((nsyn[i].bits&0xff00ffff)|((int)packbuf[j++])<<16);
                if (l&64)  nsyn[i].bits = ((nsyn[i].bits&0x00ffffff)|((int)packbuf[j++])<<24);
                if (l&128)
                {
                    nsyn[i].q16horz = (fix16_t)packbuf[j];
                    nsyn[i].q16horz += (fix16_t)packbuf[j + 1] << 8;
                    nsyn[i].q16horz += (fix16_t)packbuf[j + 2] << 16;
                    nsyn[i].q16horz += (fix16_t)packbuf[j + 3] << 24;
                    j += 4;
                }
                if (l&256)  nsyn[i].extbits = (unsigned char)packbuf[j++];
                /*                if (l&256)  nsyn[i].extbits = ((nsyn[i].extbits&0xffffff00)|((int)packbuf[j++]));
                                if (l&512)  nsyn[i].extbits = ((nsyn[i].extbits&0xffff00ff)|((int)packbuf[j++])<<8);
                                if (l&1024) nsyn[i].extbits = ((nsyn[i].extbits&0xff00ffff)|((int)packbuf[j++])<<16);
                                if (l&2048) nsyn[i].extbits = ((nsyn[i].extbits&0x00ffffff)|((int)packbuf[j++])<<24); */

                if (TEST_SYNC_KEY(nsyn[i].bits,SK_GAMEQUIT)) g_player[i].playerquitflag = 0;
                g_player[i].movefifoend++;
            }

            Net_GetSyncInfoFromPacket(packbuf, packbufleng, &j, other);

            TRAVERSE_CONNECT(i)
            if (i != myconnectindex)
                for (j=g_movesPerPacket-1;j>=1;j--)
                {
                    copybufbyte(&nsyn[i],&inputfifo[g_player[i].movefifoend&(MOVEFIFOSIZ-1)][i],sizeof(input_t));
                    g_player[i].movefifoend++;
                }

            movefifosendplc += g_movesPerPacket;

            break;
        case PACKET_TYPE_SLAVE_TO_MASTER:  //[1] (receive slave sync buffer)
            j = 3;
            k = packbuf[1] + (int)(packbuf[2]<<8);

            // PING
            pingTime = (int32_t)packbuf[j];
            pingTime += (int32_t)packbuf[j + 1] << 8;
            pingTime += (int32_t)packbuf[j + 2] << 16;
            pingTime += (int32_t)packbuf[j + 3] << 24;
            j += 4;

            g_player[other].ping = timerGetTicks() - pingTime;

            osyn = (input_t *)&inputfifo[(g_player[other].movefifoend-1)&(MOVEFIFOSIZ-1)][0];
            nsyn = (input_t *)&inputfifo[(g_player[other].movefifoend)&(MOVEFIFOSIZ-1)][0];

            copybufbyte(&osyn[other],&nsyn[other],sizeof(input_t));
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

            /*            while (j != packbufleng)
                        {
                            g_player[other].syncval[g_player[other].syncvalhead&(MOVEFIFOSIZ-1)] = packbuf[j++];
                            g_player[other].syncvalhead++;
                        } */
            Net_GetSyncInfoFromPacket(packbuf, packbufleng, &j, other);

            for (i=g_movesPerPacket-1;i>=1;i--)
            {
                copybufbyte(&nsyn[other],&inputfifo[g_player[other].movefifoend&(MOVEFIFOSIZ-1)][other],sizeof(input_t));
                g_player[other].movefifoend++;
            }

            break;

        case PACKET_TYPE_BROADCAST:
            g_player[other].movefifoend = movefifoplc = movefifosendplc = predictfifoplc = 0;
            g_player[other].syncvalhead = syncvaltottail = 0L;
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

            for (i=g_movesPerPacket-1;i>=1;i--)
            {
                copybufbyte(&nsyn[other],&inputfifo[g_player[other].movefifoend&(MOVEFIFOSIZ-1)][other],sizeof(input_t));
                g_player[other].movefifoend++;
            }

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
                break;

            case PACKET_TYPE_VERSION:
                //slaves in M/S mode only send to master
                //Master re-transmits message to all others
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex == connecthead))
                    for (i=connectpoint2[connecthead];i>=0;i=connectpoint2[i])
                        if (i != other) mmulti_sendpacket(i, (unsigned char*)packbuf,packbufleng);

                if (packbuf[2] != (char)atoi(s_buildDate))
                {
                    initprintf("Player has version %d, expecting %d\n",packbuf[2],(char)atoi(s_buildDate));
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
            quittimer = (int32_t)totalclock+120;
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
    buf[2] = (char)atoi(s_buildDate);
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

    TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 13);
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
        if (g_chatPlayer != -1 || ud.multimode < 3 || g_movesPerPacket == 4)
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

            if (g_chatPlayer >= ud.multimode || g_movesPerPacket == 4)
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

    if (p->pos.x == myxbak[i] && p->pos.y == myybak[i] && p->pos.z == myzbak[i]
            && p->q16horiz == myhorizbak[i] && p->q16ang == myangbak[i]) return;

    if (ud.config.PredictionDebug)
    {
        initprintf("Prediction Mismatch - ");
        if (p->pos.x != myxbak[i])
        {
            initprintf("^01xpos^12(^02%d^12/^08%d^12) ", myxbak[i], p->pos.x);
        }
        if (p->pos.y != myybak[i])
        {
            initprintf("^01ypos^12(^02%d^12/^08%d^12) ", myybak[i], p->pos.y);
        }
        if (p->pos.z != myzbak[i])
        {
            initprintf("^01zpos^12(^02%d^12/^08%d^12) ", myzbak[i], p->pos.z);
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

    myx = p->pos.x;
    omyx = p->opos.x;
    myxvel = p->vel.x;
    myy = p->pos.y;
    omyy = p->opos.y;
    myyvel = p->vel.y;
    myz = p->pos.z;
    omyz = p->opos.z;
    myzvel = p->vel.z;
    myang = p->q16ang;
    omyang = p->oq16ang;
    mycursectnum = p->cursectnum;
    myhoriz = p->q16horiz;
    omyhoriz = p->oq16horiz;
    myhorizoff = p->q16horizoff;
    omyhorizoff = p->oq16horizoff;
    myjumpingcounter = p->jumping_counter;
    myjumpingtoggle = p->jumping_toggle;
    myonground = p->on_ground;
    myhardlanding = p->hard_landing;
    myreturntocenter = p->return_to_center;

    predictfifoplc = movefifoplc;
    while (predictfifoplc < g_player[myconnectindex].movefifoend)
        Net_DoPrediction();
}

void Net_ResetPrediction(void)
{
    myx = omyx = g_player[myconnectindex].ps->pos.x;
    myy = omyy = g_player[myconnectindex].ps->pos.y;
    myz = omyz = g_player[myconnectindex].ps->pos.z;
    myxvel = myyvel = myzvel = 0;
    myang = omyang = g_player[myconnectindex].ps->q16ang;
    myhoriz = omyhoriz = g_player[myconnectindex].ps->q16horiz;
    myhorizoff = omyhorizoff = g_player[myconnectindex].ps->q16horizoff;
    mycursectnum = g_player[myconnectindex].ps->cursectnum;
    myjumpingcounter = g_player[myconnectindex].ps->jumping_counter;
    myjumpingtoggle = g_player[myconnectindex].ps->jumping_toggle;
    myonground = g_player[myconnectindex].ps->on_ground;
    myhardlanding = g_player[myconnectindex].ps->hard_landing;
    myreturntocenter = g_player[myconnectindex].ps->return_to_center;
}

void Net_DoPrediction(void)
{
    input_t *syn;
    DukePlayer_t *p;
    int i, j, k, doubvel, fz, cz, hz, lz, x, y;
    unsigned int sb_snum;
    short psect, psectlotag, tempsect, backcstat;
    char shrunk, spritebridge;

    syn = (input_t *)&inputfifo[predictfifoplc&(MOVEFIFOSIZ-1)][myconnectindex];

    p = g_player[myconnectindex].ps;

    backcstat = sprite[p->i].cstat;
    sprite[p->i].cstat &= ~257;

    sb_snum = syn->bits;

    psect = mycursectnum;
    psectlotag = sector[psect].lotag;
    spritebridge = 0;

    shrunk = (sprite[p->i].yrepeat < 32);

    if (ud.clipping == 0 && (sector[psect].floorpicnum == MIRROR || psect < 0 || psect >= MAXSECTORS))
    {
        myx = omyx;
        myy = omyy;
    }
    else
    {
        omyx = myx;
        omyy = myy;
    }

    omyhoriz = myhoriz;
    omyhorizoff = myhorizoff;
    omyz = myz;
    omyang = myang;

    getzrange_old(myx,myy,myz,psect,&cz,&hz,&fz,&lz,163L,CLIPMASK0);

    j = getflorzofslope(psect,myx,myy);

    if ((lz&49152) == 16384 && psectlotag == 1 && klabs(myz-j) > PHEIGHT+(16<<8))
        psectlotag = 0;

    if (p->aim_mode == 0 && myonground && psectlotag != 2 && (sector[psect].floorstat&2))
    {
        x = myx+(sintable[(fix16_to_int(myang)+512)&2047]>>5);
        y = myy+(sintable[fix16_to_int(myang)&2047]>>5);
        tempsect = psect;
        updatesector(x,y,&tempsect);
        if (tempsect >= 0)
        {
            k = getflorzofslope(psect,x,y);
            if (psect == tempsect)
                myhorizoff += fix16_from_int(mulscale16(j-k,160));
            else if (klabs(getflorzofslope(tempsect,x,y)-k) <= (4<<8))
                myhorizoff += fix16_from_int(mulscale16(j-k,160));
        }
    }

    if (myhorizoff > 0)
    {
        myhorizoff -= ((myhorizoff >> 3) + fix16_one);
        myhorizoff = max(myhorizoff, 0);
    }
    else if (p->q16horizoff < 0)
    {
        myhorizoff += (((-myhorizoff) >> 3) + fix16_one);
        myhorizoff = min(myhorizoff, 0);
    }

    if (hz >= 0 && (hz&49152) == 49152)
    {
        hz &= (MAXSPRITES-1);
        if (sprite[hz].statnum == 1 && sprite[hz].extra >= 0)
        {
            hz = 0;
            cz = getceilzofslope(psect,myx,myy);
        }
    }

    if (lz >= 0 && (lz&49152) == 49152)
    {
        j = lz&(MAXSPRITES-1);
        if ((sprite[j].cstat&33) == 33)
        {
            psectlotag = 0;
            spritebridge = 1;
        }
        if (A_CheckEnemySprite(&sprite[j]) && sprite[j].xrepeat > 24 && klabs(sprite[p->i].z-sprite[j].z) < (84<<8))
        {
            j = getangle(sprite[j].x-myx,sprite[j].y-myy);
            myxvel -= sintable[(j+512)&2047]<<4;
            myyvel -= sintable[j&2047]<<4;
        }
    }

    if (sprite[p->i].extra <= 0)
    {
        if (psectlotag == 2)
        {
            if (p->on_warping_sector == 0)
            {
                if (klabs(myz-fz) > (PHEIGHT>>1))
                    myz += 348;
            }
            clipmove_old(&myx,&myy,&myz,&mycursectnum,0,0,164L,(4L<<8),(4L<<8),CLIPMASK0);
        }

        updatesector(myx,myy,&mycursectnum);
        pushmove_old(&myx,&myy,&myz,&mycursectnum,128L,(4L<<8),(20L<<8),CLIPMASK0);

        myhoriz = F16(100);
        myhorizoff = 0;

        goto ENDFAKEPROCESSINPUT;
    }

    doubvel = TICSPERFRAME;

    if (p->on_crane >= 0) goto FAKEHORIZONLY;

    if (p->one_eighty_count < 0) myang += F16(128);

    i = 40;

    if (psectlotag == 2)
    {
        myjumpingcounter = 0;

        if (TEST_SYNC_KEY(sb_snum, SK_JUMP))
        {
            if (myzvel > 0) myzvel = 0;
            myzvel -= 348;
            if (myzvel < -(256*6)) myzvel = -(256*6);
        }
        else if (TEST_SYNC_KEY(sb_snum, SK_CROUCH))
        {
            if (myzvel < 0) myzvel = 0;
            myzvel += 348;
            if (myzvel > (256*6)) myzvel = (256*6);
        }
        else
        {
            if (myzvel < 0)
            {
                myzvel += 256;
                if (myzvel > 0)
                    myzvel = 0;
            }
            if (myzvel > 0)
            {
                myzvel -= 256;
                if (myzvel < 0)
                    myzvel = 0;
            }
        }

        if (myzvel > 2048) myzvel >>= 1;

        myz += myzvel;

        if (myz > (fz-(15<<8)))
            myz += ((fz-(15<<8))-myz)>>1;

        if (myz < (cz+(4<<8)))
        {
            myz = cz+(4<<8);
            myzvel = 0;
        }
    }

    else if (p->jetpack_on)
    {
        myonground = 0;
        myjumpingcounter = 0;
        myhardlanding = 0;

        if (p->jetpack_on < 11)
            myz -= (p->jetpack_on<<7); //Goin up

        if (shrunk) j = 512;
        else j = 2048;

        if (TEST_SYNC_KEY(sb_snum, SK_JUMP))                            //A
            myz -= j;
        if (TEST_SYNC_KEY(sb_snum, SK_CROUCH))                       //Z
            myz += j;

        if (shrunk == 0 && (psectlotag == 0 || psectlotag == 2)) k = 32;
        else k = 16;

        if (myz > (fz-(k<<8)))
            myz += ((fz-(k<<8))-myz)>>1;
        if (myz < (cz+(18<<8)))
            myz = cz+(18<<8);
    }
    else if (psectlotag != 2)
    {
        if (psectlotag == 1 && p->spritebridge == 0)
        {
            if (shrunk == 0) i = 34;
            else i = 12;
        }

        if (myz < (fz-(i<<8))) //falling
        {
            if (!TEST_SYNC_KEY(sb_snum, SK_JUMP) && !TEST_SYNC_KEY(sb_snum, SK_CROUCH) && myonground && (sector[psect].floorstat&2) && myz >= (fz-(i<<8)-(16<<8)))
                myz = fz-(i<<8);
            else
            {
                myonground = 0;

                myzvel += (g_spriteGravity+80);

                if (myzvel >= (4096+2048)) myzvel = (4096+2048);
            }
        }
        else
        {
            if (psectlotag != 1 && psectlotag != 2 && myonground == 0 && myzvel > (6144>>1))
                myhardlanding = myzvel>>10;
            myonground = 1;

            if (i==40)
            {
                //Smooth on the ground

                k = ((fz-(i<<8))-myz)>>1;
                if (klabs(k) < 256) k = 0;
                myz += k; // ((fz-(i<<8))-myz)>>1;
                myzvel -= 768; // 412;
                if (myzvel < 0) myzvel = 0;
            }
            else if (myjumpingcounter == 0)
            {
                myz += ((fz-(i<<7))-myz)>>1; //Smooth on the water
                if (p->on_warping_sector == 0 && myz > fz-(16<<8))
                {
                    myz = fz-(16<<8);
                    myzvel >>= 1;
                }
            }

            if (TEST_SYNC_KEY(sb_snum, SK_CROUCH))
                myz += (2048+768);

            // jumping
            if (TEST_SYNC_KEY(sb_snum, SK_JUMP) == 0 && myjumpingtoggle == 1)
                myjumpingtoggle = 0;

            else if (TEST_SYNC_KEY(sb_snum, SK_JUMP) && myjumpingtoggle == 0)
            {
                if (myjumpingcounter == 0)
                    if ((fz-cz) > (56<<8))
                    {
                        myjumpingcounter = 1;
                        myjumpingtoggle = 1;
                    }
            }
            if (myjumpingcounter && TEST_SYNC_KEY(sb_snum, SK_JUMP) == 0)
                myjumpingcounter = 0;
        }

        if (myjumpingcounter)
        {
            if (TEST_SYNC_KEY(sb_snum, SK_JUMP) == 0 && myjumpingtoggle == 1)
                myjumpingtoggle = 0;

            if (myjumpingcounter < (1024+256))
            {
                if (psectlotag == 1 && myjumpingcounter > 768)
                {
                    myjumpingcounter = 0;
                    myzvel = -512;
                }
                else
                {
                    myzvel -= (sintable[(2048-128+myjumpingcounter)&2047])/12;
                    myjumpingcounter += 180;

                    myonground = 0;
                }
            }
            else
            {
                myjumpingcounter = 0;
                myzvel = 0;
            }
        }

        myz += myzvel;

        if (myz < (cz+(4<<8)))
        {
            myjumpingcounter = 0;
            if (myzvel < 0) myxvel = myyvel = 0;
            myzvel = 128;
            myz = cz+(4<<8);
        }
    }

    if (p->fist_incs ||
            p->transporter_hold > 2 ||
            myhardlanding ||
            p->access_incs > 0 ||
            p->knee_incs > 0 ||
            (p->curr_weapon == TRIPBOMB_WEAPON &&
             p->kickback_pic > 1 &&
             p->kickback_pic < 4))
    {
        doubvel = 0;
        myxvel = 0;
        myyvel = 0;
    }
    else if (syn->q16avel)            //p->ang += syncangvel * constant
    {
        //ENGINE calculates angvel for you
        fix16_t inputAng = syn->q16avel;

        myang += (psectlotag == 2) ? fix16_mul(inputAng - (inputAng >> 3), fix16_from_int(ksgn(doubvel)))
            : fix16_mul(inputAng, fix16_from_int(ksgn(doubvel)));
        myang &= 0x7FFFFFF;
    }

    if (myxvel || myyvel || syn->fvel || syn->svel)
    {
        if (p->jetpack_on == 0 && p->steroids_amount > 0 && p->steroids_amount < 400)
            doubvel <<= 1;

        myxvel += ((syn->fvel*doubvel)<<6);
        myyvel += ((syn->svel*doubvel)<<6);

        // There really ought to be a predicted version of kickback_pic for this, to stop stuttering while kicking.
        if ((aplWeaponWorksLike[p->curr_weapon][myconnectindex] == KNEE_WEAPON && p->kickback_pic > 10 && myonground) || (myonground && TEST_SYNC_KEY(sb_snum, SK_CROUCH)))
        {
            myxvel = mulscale16(myxvel,p->runspeed-0x2000);
            myyvel = mulscale16(myyvel,p->runspeed-0x2000);
        }
        else
        {
            if (psectlotag == 2)
            {
                myxvel = mulscale16(myxvel,p->runspeed-0x1400);
                myyvel = mulscale16(myyvel,p->runspeed-0x1400);
            }
            else
            {
                myxvel = mulscale16(myxvel,p->runspeed);
                myyvel = mulscale16(myyvel,p->runspeed);
            }
        }

        if (klabs(myxvel) < 2048 && klabs(myyvel) < 2048)
            myxvel = myyvel = 0;

        if (shrunk)
        {
            myxvel =
                mulscale16(myxvel,(p->runspeed)-(p->runspeed>>1)+(p->runspeed>>2));
            myyvel =
                mulscale16(myyvel,(p->runspeed)-(p->runspeed>>1)+(p->runspeed>>2));
        }
    }

FAKEHORIZONLY:
    if (psectlotag == 1 || spritebridge == 1) i = (4L<<8);
    else i = (20L<<8);

    clipmove_old(&myx,&myy,&myz,&mycursectnum,myxvel,myyvel,164L,4L<<8,i,CLIPMASK0);
    pushmove_old(&myx,&myy,&myz,&mycursectnum,164L,4L<<8,4L<<8,CLIPMASK0);

    if (p->jetpack_on == 0 && psectlotag != 1 && psectlotag != 2 && shrunk)
    {
        myz += 32 << 8;
    }

    // center_view
    i = 0;
    if (TEST_SYNC_KEY(sb_snum, SK_CENTER_VIEW) || myhardlanding)
    {
        myreturntocenter = 9;
    }

    if (TEST_SYNC_KEY(sb_snum, SK_LOOK_UP))
    {
        myreturntocenter = 9;
        if (TEST_SYNC_KEY(sb_snum, SK_RUN)) myhoriz += fix16_from_int(12);     // running
        myhoriz += fix16_from_int(12);
        i++;
    }
    else if (TEST_SYNC_KEY(sb_snum, SK_LOOK_DOWN))
    {
        myreturntocenter = 9;
        if (TEST_SYNC_KEY(sb_snum, SK_RUN)) myhoriz -= fix16_from_int(12);
        myhoriz -= fix16_from_int(12);
        i++;
    }
    else if (TEST_SYNC_KEY(sb_snum, SK_AIM_UP))
    {
        if (TEST_SYNC_KEY(sb_snum, SK_RUN)) myhoriz += fix16_from_int(6);
        myhoriz += fix16_from_int(6);
        i++;
    }
    else if (TEST_SYNC_KEY(sb_snum, SK_AIM_DOWN))
    {
        // running
        if (TEST_SYNC_KEY(sb_snum, SK_RUN)) myhoriz -= fix16_from_int(6);
        myhoriz -= fix16_from_int(6);
        i++;
    }

    if (myreturntocenter > 0)
    {
        if (TEST_SYNC_KEY(sb_snum, SK_LOOK_UP) == 0 && TEST_SYNC_KEY(sb_snum, SK_LOOK_DOWN) == 0)
        {
            myreturntocenter--;
            myhoriz += F16(33) - fix16_div(myhoriz, F16(3));
            i++;
        }
    }

    if (myhardlanding > 0)
    {
        myhardlanding--;
        myhoriz -= fix16_from_int(myhardlanding << 4);
    }

    if (i)
    {
        if (myhoriz > F16(95) && myhoriz < F16(105)) myhoriz = F16(100);
        if (myhorizoff > F16(-5) && myhorizoff < F16(5)) myhorizoff = 0;
    }

    myhoriz += syn->q16horz;
    myhoriz = fix16_clamp(myhoriz, F16(HORIZ_MIN), F16(HORIZ_MAX));

    if (p->knee_incs > 0)
    {
        myhoriz -= F16(48);
        myreturntocenter = 9;
    }

ENDFAKEPROCESSINPUT:

    X_OnEvent(EVENT_FAKEDOMOVETHINGS, g_player[myconnectindex].ps->i, myconnectindex, -1);

    myxbak[predictfifoplc&(MOVEFIFOSIZ-1)] = myx;
    myybak[predictfifoplc&(MOVEFIFOSIZ-1)] = myy;
    myzbak[predictfifoplc&(MOVEFIFOSIZ-1)] = myz;
    myangbak[predictfifoplc&(MOVEFIFOSIZ-1)] = myang;
    myhorizbak[predictfifoplc&(MOVEFIFOSIZ-1)] = myhoriz;
    predictfifoplc++;

    sprite[p->i].cstat = backcstat;
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
    avgfvel = avgsvel = avgavel = avghorz = avgbits = avgextbits = 0;

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

    if (numplayers < 2) return;
    packbuf[0] = PACKET_TYPE_PLAYER_READY;

    g_player[myconnectindex].playerreadyflag++;

    // if we're a peer or slave, not a master
    if ((g_networkBroadcastMode == NETMODE_P2P) || ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)))
        TRAVERSE_CONNECT(i)
    {
        if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 1);
        if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
    }

    if (ud.multimode > 1)
    {
        P_SetGamePalette(g_player[myconnectindex].ps, TITLEPAL, 11);
        rotatesprite(0, 0, 65536L, 0, BETASCREEN, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);

        rotatesprite(160 << 16, (104) << 16, 60 << 10, 0, DUKENUKEM, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(160 << 16, (129) << 16, 30 << 11, 0, THREEDEE, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        if (PLUTOPAK)   // JBF 20030804
            rotatesprite(160 << 16, (151) << 16, 30 << 11, 0, PLUTOPAKSPRITE + 1, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);

        gametext(160, 190, "WAITING FOR PLAYERS", 14, 2);
        videoNextPage();
    }

    while (1)
    {
        //if (quitevent) G_GameExit("");

        handleevents();
        getpackets();

        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].playerreadyflag < g_player[myconnectindex].playerreadyflag) break;
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead))
            {
                // we're a slave
                i = -1; break;
            }
            //slaves in M/S mode only wait for master
        }
        if (i < 0)
        {
            // master sends ready packet once it hears from all slaves
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && myconnectindex == connecthead)
                TRAVERSE_CONNECT(i)
            {
                packbuf[0] = PACKET_TYPE_PLAYER_READY;
                if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf, 1);
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
        {
            handleevents();
            getpackets();
        }
        if (KB_KeyPressed(sc_Escape)) return;

        packbuf[0] = PACKET_TYPE_NULL_PACKET;
        TRAVERSE_CONNECT(i)
        {
            if (i != myconnectindex) mmulti_sendpacket(i, (unsigned char*)packbuf,1);
            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
        }
    }
}