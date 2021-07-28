#ifndef SYNC_H
#define SYNC_H

#define MAXSYNCBYTES 16
#define SYNCFIFOSIZ 1024

// TENSW: on really bad network connections, the sync FIFO queue can overflow if it is the
// same size as the move fifo.
#if MOVEFIFOSIZ >= SYNCFIFOSIZ
#error "MOVEFIFOSIZ is greater than or equal to SYNCFIFOSIZ!"
#endif

#define DEFINE_SYNCFUNC(funcname) { #funcname, funcname }
typedef struct
{
    const char* name;
    char (*func)(void);
} SyncType_t;

extern char syncstat[MAXSYNCBYTES];
extern char g_szfirstSyncMsg[MAXSYNCBYTES][60];

extern int g_foundSyncError;
extern int syncvaltail, syncvaltottail;

void initsynccrc(void);
void Net_GetSyncStat(void);
void Net_DisplaySyncMsg(void);
void Net_AddSyncInfoToPacket(int *j);
void Net_GetSyncInfoFromPacket(char *packbuf, int packbufleng, int *j, int otherconnectindex);

#endif