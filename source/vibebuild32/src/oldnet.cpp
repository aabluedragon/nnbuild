#define OLDNET_CPP_

#include "duke3d.h"
#include "oldnet.h"
#include "net_predict.h"
#include "masterserver.h"

#ifdef _WIN32
# include <process.h>  // _execv, for the desync self-reconnect
#else
# include <unistd.h>   // execv
#endif
#include <dirent.h>    // recursive scan for an already-present mod file
#include <sys/stat.h>

// Masterserver the in-game lobby queries; overridable with the cl_masterserver
// cvar. Defaults to localhost for local play/testing.
char g_masterServerAddr[128] = "127.0.0.1";

// When a client is relaunched to join a specific server (from the lobby or a
// manual address), this holds that server's "host:port" so the connection loop
// can ask the masterserver to punch a NAT hole to it if the direct connect
// stalls. Empty for hosts / non-lobby launches.
char g_punchTargetAddr[128] = "";

// Set via -joinerror when a client relaunches back to the menu after a failed
// join; the JOIN GAME menu shows it once so the user learns why. Cleared after
// being displayed.
char g_joinErrorMsg[128] = "";

// When set (cl_preferipv6), the lobby prefers a server's IPv6 address if it
// published one. Off by default so IPv4-only clients always connect.
int32_t g_netPreferIPv6 = 0;

#ifndef _WIN32
# include <poll.h>
# include <unistd.h>
#endif

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
static uint32_t lateJoinAnnounceStart = 0; // master: when we began announcing the pending join
static uint32_t lateJoinLastAnnounce = 0;  // master: last announce (re)send time
static bool lateJoinJoinerReady = false;   // master: joiner reached its main loop (its READY arrived)
static uint32_t lateJoinClientStartMs = 0; // joiner: when we entered late-join wait (backstop timeout)
static uint32_t lateJoinLastProgressMs = 0; // joiner: last time snapshot/file data arrived (idle timeout)
static int32_t lateJoinGoSlot = -1;

// Abort the late join only after this long with NO progress at all - a genuine
// stall (dead host / lost link), never while a large mod download is streaming.
#define NET_LATEJOIN_IDLE_MS 20000

// Called whenever snapshot or file-transfer data arrives during a late join, so
// the idle timeout above measures a real stall rather than total elapsed time.
static inline void Net_NoteLateJoinProgress(void) { lateJoinLastProgressMs = timerGetTicks(); }

// Async slave: our outgoing input stream, kept independent of the authoritative
// fifo (which is master-driven) so a master substitution can't corrupt the
// delta base. g_asyncSentCount counts inputs sent, for backpressure. All three
// are reset whenever the fifo is cleared (Net_ClearFIFO) so the first post-clear
// delta is encoded against the same zeroed base the master decodes against -
// otherwise a held input would be silently dropped across a level change.
static input_t g_asyncOutPrev, g_asyncOutCur;
static int32_t g_asyncSentCount;

// Per-slot client identity (from PACKET_TYPE_PLAYER_NAME). The master uses these
// plus the slot IP to restore a reconnecting player's score by identity.
static char g_playerGuid[MAXPLAYERS][NET_GUID_LEN];

// Master-side score archive: when a player leaves, we stash their score keyed by
// GUID+IP so we can restore it if the same player rejoins the SAME match. It is
// cleared whenever a new match starts (level change / fraglimit), so a rejoin
// after the match ended starts fresh. Score fields are not part of the sync CRC,
// so restoring them cannot desync the simulation.
#define MAXSCOREARCHIVE 32
struct scoresave_t { bool used; char guid[NET_GUID_LEN]; uint32_t ip; int32_t frag, fraggedself, deaths; };
static scoresave_t g_scoreArchive[MAXSCOREARCHIVE];
// Restore values chosen by the master for a rejoining slot (computed once its
// GUID is known), then broadcast so every scoreboard agrees.
static int32_t g_lateJoinRestoreFrag = 0, g_lateJoinRestoreFraggedself = 0, g_lateJoinRestoreDeaths = 0;
// Master: slots that just late-joined and are waiting for their GUID to arrive
// so a score-restore decision can be made and broadcast.
static bool g_slotAwaitingRestore[MAXPLAYERS];
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

    // Lockstep trusts our own local input and just skips the master's echo of
    // it (they're identical - the master never substitutes). In async the
    // master CAN substitute our input, so we must apply the echoed canonical
    // version like any other player, or we desync. So only skip when NOT async.
    if (playerNum == myconnectindex && !g_netAsyncMode)
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

// Master: schedule a player's removal. Freeze input and fill the target's
// pending ticks; the actual removal happens once every machine has drained to
// the same tick (Net_MasterCheckDrop), matching the late-join discipline so
// the roster change lands on one identical tick everywhere.
static int32_t g_pendingKickSlot = -1;
static char g_pendingKickReason[32];
static int32_t g_pendingDropSlot = -1;   // slave side: removal announced, apply at tick
static uint16_t g_pendingDropMask;
static int32_t g_pendingDropTick;

// Master: real-time (ms) of the last packet seen from each slot, for detecting
// a client that left ungracefully (crash/close/network loss) so its slot can be
// freed for someone to rejoin. 0 means "not heard from this session yet".
static uint32_t g_slotLastPacketMs[MAXPLAYERS];
#define NET_CLIENT_TIMEOUT_MS 5000

// ---- Score persistence across reconnects (master, keyed by client GUID) ----

void Net_ClearScoreArchive(void)
{
    Bmemset(g_scoreArchive, 0, sizeof(g_scoreArchive));
}

// Master: stash a departing player's score so the same identity can reclaim it
// if they rejoin this match.
static void Net_ArchiveScore(int32_t slot)
{
    if (myconnectindex != connecthead || (unsigned)slot >= MAXPLAYERS || !g_player[slot].ps)
        return;
    if (!g_playerGuid[slot][0])
        return;  // no identity to key on; nothing to restore later

    uint32_t const ip = (uint32_t)mmulti_getslotip(slot);

    int32_t idx = -1, freeIdx = -1;
    for (int32_t i = 0; i < MAXSCOREARCHIVE; i++)
    {
        if (!g_scoreArchive[i].used) { if (freeIdx < 0) freeIdx = i; continue; }
        if (g_scoreArchive[i].ip == ip && !Bstrcmp(g_scoreArchive[i].guid, g_playerGuid[slot])) { idx = i; break; }
    }
    if (idx < 0)
    {
        if (freeIdx < 0)
        {
            // Archive full (>32 distinct identities churned this match) and this
            // one isn't already stored: drop it rather than clobbering another
            // player's saved score at a fixed index.
            LOG_F(WARNING, "Score archive full; not saving score for slot %d", slot);
            return;
        }
        idx = freeIdx;
    }

    g_scoreArchive[idx].used = true;
    Bstrncpyz(g_scoreArchive[idx].guid, g_playerGuid[slot], NET_GUID_LEN);
    g_scoreArchive[idx].ip = ip;
    g_scoreArchive[idx].frag = g_player[slot].ps->frag;
    g_scoreArchive[idx].fraggedself = g_player[slot].ps->fraggedself;
    g_scoreArchive[idx].deaths = g_player[slot].ps->deaths;
}

// Master: decide what score a rejoining slot should get, per the identity rules:
// exactly one archived entry for this GUID -> restore it; several entries for the
// GUID -> disambiguate by IP and restore only if exactly one GUID+IP match;
// otherwise (ambiguous, or no match, or the match already ended so the archive
// was cleared) -> start fresh at zero. Result goes into g_lateJoinRestore*.
static bool Net_ComputeRestoreScore(int32_t slot)
{
    g_lateJoinRestoreFrag = g_lateJoinRestoreFraggedself = g_lateJoinRestoreDeaths = 0;

    if (myconnectindex != connecthead || (unsigned)slot >= MAXPLAYERS || !g_playerGuid[slot][0])
        return false;

    uint32_t const ip = (uint32_t)mmulti_getslotip(slot);

    int32_t byGuid = 0, byGuidIp = 0, guidIdx = -1, guidIpIdx = -1;
    for (int32_t i = 0; i < MAXSCOREARCHIVE; i++)
    {
        if (!g_scoreArchive[i].used || Bstrcmp(g_scoreArchive[i].guid, g_playerGuid[slot]))
            continue;
        byGuid++; guidIdx = i;
        if (g_scoreArchive[i].ip == ip) { byGuidIp++; guidIpIdx = i; }
    }

    int32_t pick = -1;
    if (byGuid == 1)
        pick = guidIdx;                 // unique identity -> restore
    else if (byGuid > 1 && byGuidIp == 1)
        pick = guidIpIdx;               // ambiguous GUID, but IP breaks the tie
    // else: ambiguous even with IP, or no entry -> leave at zero (fresh)

    if (pick < 0)
        return false;

    g_lateJoinRestoreFrag        = g_scoreArchive[pick].frag;
    g_lateJoinRestoreFraggedself = g_scoreArchive[pick].fraggedself;
    g_lateJoinRestoreDeaths      = g_scoreArchive[pick].deaths;
    g_scoreArchive[pick].used    = false;  // consumed
    LOG_F(INFO, "Restoring score (%d frags) for rejoining player in slot %d.", g_lateJoinRestoreFrag, slot);
    return true;
}

// Every machine: apply the master's chosen restore score to a just-spawned
// joiner. With no restore this simply confirms the fresh zero the spawn set.
static void Net_ApplyRestoreScore(int32_t slot)
{
    if ((unsigned)slot >= MAXPLAYERS || !g_player[slot].ps)
        return;
    g_player[slot].ps->frag        = g_lateJoinRestoreFrag;
    g_player[slot].ps->fraggedself = g_lateJoinRestoreFraggedself;
    g_player[slot].ps->deaths      = g_lateJoinRestoreDeaths;
}

// Master, each frame: a slot that just late-joined has its GUID a moment later
// (its name packet only flushes once it is fully in the game). Once we have it,
// decide the reconnect score restore and broadcast it so every scoreboard
// matches, then stop waiting on that slot.
void Net_MasterCheckRestore(void)
{
    if (myconnectindex != connecthead)
        return;

    for (int32_t slot = 0; slot < numplayers; slot++)
    {
        if (!g_slotAwaitingRestore[slot])
            continue;
        if (!g_player[slot].connected)      // gave up / left before we could
        {
            g_slotAwaitingRestore[slot] = false;
            continue;
        }
        if (!g_playerGuid[slot][0])
            continue;                        // GUID not here yet; wait

        g_slotAwaitingRestore[slot] = false;

        // Only touch the score when we actually matched an archived identity.
        // Broadcasting a zero "restore" for a fresh joiner would wipe any frags
        // it earned in the brief window between spawning and its GUID arriving.
        if (!Net_ComputeRestoreScore(slot))  // fills g_lateJoinRestore* on a match
            continue;
        Net_ApplyRestoreScore(slot);         // apply locally

        // Tell everyone so their scoreboards agree.
        packbuf[0] = PACKET_TYPE_RESTORE_SCORE;
        packbuf[1] = (char)slot;
        B_BUF32(&packbuf[2],  g_lateJoinRestoreFrag);
        B_BUF32(&packbuf[6],  g_lateJoinRestoreFraggedself);
        B_BUF32(&packbuf[10], g_lateJoinRestoreDeaths);
        for (int32_t i = 0; i < numplayers; i++)
            if (i != myconnectindex && g_player[i].connected)
                mmulti_sendpacket(i, (unsigned char *)packbuf, 14);
    }
}

static void Net_ScheduleRemove(int32_t slot, const char *reasonQuote)
{
    Net_ArchiveScore(slot);

    if (g_pendingKickSlot >= 0)
        return;  // one at a time; the caller can retry after this completes

    while (g_player[slot].movefifoend <= movefifosendplc)
    {
        int32_t const e = g_player[slot].movefifoend;
        inputfifo[e & (MOVEFIFOSIZ-1)][slot] = inputfifo[(e-1) & (MOVEFIFOSIZ-1)][slot];
        g_player[slot].movefifoend++;
    }

    g_pendingKickSlot = slot;
    Bstrncpyz(g_pendingKickReason, reasonQuote, sizeof(g_pendingKickReason));
    g_lateJoinFreeze = 1;  // pause the master's input flow while we drain
}

static void Net_ApplyRemoval(int32_t slot, const char *reasonQuote)
{
    g_player[slot].connected = 0;
    Net_RebuildConnectList();
    P_QuickKill(g_player[slot].ps);
    Bsprintf(buf, "%s^00 %s", g_player[slot].user_name, reasonQuote);
    G_AddUserQuote(buf);
}

// Master, each frame: once frozen for a scheduled removal and every machine
// has drained to the broadcast tick, tell everyone and apply the removal at a
// ready barrier (as late-join does), then resume.
void Net_MasterCheckDrop(void)
{
    if (g_pendingKickSlot < 0 || myconnectindex != connecthead)
        return;
    if (movefifoplc < movefifosendplc)
        return;  // still draining

    int32_t const slot = g_pendingKickSlot;
    // Everyone removes the player right before simulating this tick, so the
    // roster change lands identically without needing a ready barrier.
    int32_t const dropTick = movefifosendplc;

    packbuf[0] = PACKET_TYPE_DROP_PLAYER;
    packbuf[1] = (char)slot;
    B_BUF16(&packbuf[2], Net_ConnectedMask() & ~(1 << slot));
    B_BUF32(&packbuf[4], dropTick);
    for (int32_t i = 0; i < numplayers; i++)
        if (i != myconnectindex && g_player[i].connected)
            mmulti_sendpacket(i, (unsigned char *)packbuf, 8);

    // Also inform the departing player so it can exit cleanly, then sever it
    // at the transport level.
    if (g_player[slot].connected && mmulti_slotconnected(slot))
        mmulti_sendpacket(slot, (unsigned char *)packbuf, 8);

    Net_ApplyRemoval(slot, g_pendingKickReason);
    mmulti_dropslot(slot);

    g_pendingKickSlot = -1;
    g_lateJoinFreeze = 0;
}

// Master, each frame: drop a client we've stopped hearing from entirely
// (ungraceful leave - crash, window closed, network dropped). This uses real
// time, so it fires in async mode (which never stalls and has no lag dialog)
// and also cuts short a lockstep stall once a peer is clearly gone, rather than
// waiting out the full lag vote. Dropping frees the slot so someone can rejoin.
// Host: periodically publish this server's details to the masterserver, so it
// shows up in the in-game lobby. Cheap and rate-limited inside the pulse.
void Net_HostAdvertise(void)
{
    if (myconnectindex != connecthead || !Masterserver_IsAdvertising())
        return;

    // Register (once) the callback that opens a NAT hole toward a waiting client
    // by firing a marker packet from our game socket.
    static int punchFnRegistered = 0;
    if (!punchFnRegistered)
    {
        Masterserver_SetPunchFn(mmulti_punchto_str);
        punchFnRegistered = 1;
    }

    // Service NAT-punch requests the masterserver forwards to us (opens a hole
    // toward each waiting client via mmulti_punchto_str). Do this even between
    // levels, before the MODE_GAME gate below.
    Masterserver_HostPump();

    if (!(ud.gm & MODE_GAME))
        return;

    char map[24];
    if (boardfilename[0])
    {
        char const *slash = Bstrrchr(boardfilename, '/');
        Bstrncpyz(map, slash ? slash + 1 : boardfilename, sizeof(map));
    }
    else
        Bsnprintf(map, sizeof(map), "E%dL%d", ud.volume_number + 1, ud.level_number + 1);

    char const *name = g_player[connecthead].user_name;
    int32_t const dedic = g_netServerDedicated ? 1 : 0;

    Masterserver_AdvertisePulse(name && name[0] ? name : "VibeBuild32 server",
                                mmulti_getport(), numplayers - dedic,
                                playerswhenstarted - dedic, map);
}

void Net_MasterCheckTimeouts(void)
{
    if (myconnectindex != connecthead || numplayers < 2)
        return;
    if (!(ud.gm & MODE_GAME) || ud.recstat == 2)
        return;
    if (g_pendingKickSlot >= 0 || g_lateJoinFreeze || lateJoinPending >= 0)
        return;  // a roster change / state transfer is already in progress

    uint32_t const now = timerGetTicks();
    for (int32_t i = 0; i < numplayers; i++)
    {
        if (i == myconnectindex || !g_player[i].connected)
            continue;
        uint32_t const last = g_slotLastPacketMs[i];
        if (last == 0)
            continue;  // haven't heard from this slot yet this session
        if (now - last > NET_CLIENT_TIMEOUT_MS)
        {
            LOG_F(INFO, "Player %d (%s) timed out (%u ms silent); dropping.",
                  i, g_player[i].user_name, now - last);
            g_slotLastPacketMs[i] = 0;
            Net_ScheduleRemove(i, "timed out");
            return;
        }
    }
}

// Slaves: apply an announced removal right before simulating the tick the
// master tagged it with, matching the master exactly. Called at the top of
// G_MoveLoop, before the tick-drain loop, so the removed player is already
// gone when this tick is simulated.
void Net_PeerCheckDrop(void)
{
    if (g_pendingDropSlot < 0 || myconnectindex == connecthead)
        return;
    if (movefifoplc < g_pendingDropTick)
        return;  // haven't reached the drop tick yet

    int32_t const slot = g_pendingDropSlot;
    g_pendingDropSlot = -1;

    Net_ApplyConnectedMask(g_pendingDropMask);
    g_player[slot].connected = 0;
    Net_RebuildConnectList();
    if ((unsigned)slot < MAXPLAYERS && g_player[slot].ps)
    {
        P_QuickKill(g_player[slot].ps);
        Bsprintf(buf, "%s^00 left", g_player[slot].user_name);
        G_AddUserQuote(buf);
    }
}

static void Net_DropLaggard(int32_t slot)
{
    LOG_F(INFO, "Dropping lagging player %d (%s)", slot, g_player[slot].user_name);
    Net_ScheduleRemove(slot, "dropped (lag)");

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

//////////////////////////////////////////////////////////////////////////////
// Host administration: ban list, kick, ban, and the join/leave notifications.
//////////////////////////////////////////////////////////////////////////////

struct banentry_t { uint32_t ip; char name[32]; };
static banentry_t g_banList[MAXBANS];
static int32_t g_banCount;
static int32_t g_banLoaded;

static void Net_FormatIP(uint32_t ip, char *out, size_t outsize)
{
    Bsnprintf(out, outsize, "%u.%u.%u.%u", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255);
}

static uint32_t Net_ParseIP(const char *s)
{
    // Produce the same little-endian s_addr layout inet_addr() yields (first
    // octet in the low byte), matching how mmulti stores otherip[].
    unsigned a, b, c, d;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256)
        return a | (b << 8) | (c << 16) | (d << 24);
    return 0;
}

// True for the IPv4 loopback range 127.0.0.0/8 (the first octet is the low
// byte in mmulti's layout). The mmulti transport is IPv4-only, so an IPv6
// loopback (::1) or IPv4-mapped loopback would already appear here as a
// 127.x.x.x address once resolved. We never ban these - banning the host's own
// loopback would lock the operator (and any local clients) out of their server.
static bool Net_IsLoopbackIP(uint32_t ip)
{
    return (ip & 0xFF) == 127;
}

static void Net_LoadBanList(void)
{
    if (g_banLoaded)
        return;
    g_banLoaded = 1;
    g_banCount = 0;

    int32_t fil = kopen4loadfrommod("banlist.txt", 0);
    if (fil == -1)
        return;

    int32_t len = kfilelength(fil);
    char *text = (char *)Xmalloc(len + 1);
    kread(fil, text, len);
    text[len] = 0;
    kclose(fil);

    char *line = strtok(text, "\r\n");
    while (line && g_banCount < MAXBANS)
    {
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '#')
        {
            char ipstr[64] = {0}, name[32] = {0};
            sscanf(line, "%63s %31[^\r\n]", ipstr, name);
            uint32_t ip = Net_ParseIP(ipstr);
            if (ip)
            {
                g_banList[g_banCount].ip = ip;
                Bstrncpyz(g_banList[g_banCount].name, name, sizeof(g_banList[0].name));
                g_banCount++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    Xfree(text);
    LOG_F(INFO, "Loaded %d ban(s) from banlist.txt", g_banCount);
}

static void Net_SaveBanList(void)
{
    char temp[BMAX_PATH];
    if (g_modDir[0] != '/')
        Bsnprintf(temp, sizeof(temp), "%s/banlist.txt", g_modDir);
    else
        Bstrcpy(temp, "banlist.txt");

    buildvfs_FILE fil = buildvfs_fopen_write(temp);
    if (!fil)
    {
        LOG_F(ERROR, "Couldn't write banlist.txt");
        return;
    }

    char linebuf[128], ipstr[64];
    buildvfs_fputstrptr(fil, "# " APPNAME " ban list - one entry per line: <ip> <name>\n");
    for (int32_t i = 0; i < g_banCount; i++)
    {
        Net_FormatIP(g_banList[i].ip, ipstr, sizeof(ipstr));
        Bsnprintf(linebuf, sizeof(linebuf), "%s %s\n", ipstr, g_banList[i].name);
        buildvfs_fputstrptr(fil, linebuf);
    }
    buildvfs_fclose(fil);
}

// Registered with mmulti; called at the transport handshake.
static int Net_BanCheck(int ip)
{
    if (Net_IsLoopbackIP((uint32_t)ip))
        return 0;  // never lock out localhost, even from a stale ban entry
    Net_LoadBanList();
    for (int32_t i = 0; i < g_banCount; i++)
        if (g_banList[i].ip == (uint32_t)ip)
        {
            char ipstr[64];
            Net_FormatIP((uint32_t)ip, ipstr, sizeof(ipstr));
            LOG_F(INFO, "Refused banned address %s", ipstr);
            Bsprintf(buf, "Refused banned %s", ipstr);
            G_AddUserQuote(buf);
            return 1;
        }
    return 0;
}

// Registered with mmulti; called when a new address begins the handshake.
static void Net_JoinNotify(int ip)
{
    char ipstr[64];
    Net_FormatIP((uint32_t)ip, ipstr, sizeof(ipstr));
    LOG_F(INFO, "%s is attempting to join", ipstr);
    Bsprintf(buf, "%s is joining...", ipstr);
    G_AddUserQuote(buf);
}

// mmulti calls this (master only) when a fresh handshake lands on an existing
// slot. If that slot is a live, fully-integrated in-game player, the only way it
// can be handshaking again is that the client process restarted (left and
// rejoined). Drop the stale connection so the client is re-admitted as a fresh
// late joiner (with its transport sequence reset and its score restored by
// GUID). Returning nonzero tells mmulti to withhold the 0xab, so the client
// keeps retrying the handshake until the drop finishes and it can rejoin clean.
// This makes rejoin work no matter how fast the client left and came back,
// without waiting for the idle timeout to free the slot.
static int Net_ReconnectNotify(int slot)
{
    if (myconnectindex != connecthead || (unsigned)slot >= MAXPLAYERS)
        return 0;
    if (slot == lateJoinPending)
        return 0;  // this slot is mid-join already; a normal handshake retry
    if (!g_player[slot].connected)
        return 0;  // not an in-game player; let the normal join handshake proceed

    if (g_pendingKickSlot < 0 && !g_lateJoinFreeze)
    {
        LOG_F(INFO, "Slot %d re-handshaked while in-game; dropping stale connection to re-admit", slot);
        Net_ScheduleRemove(slot, "reconnecting");
    }
    return 1;  // withhold the handshake reply; the client retries after the drop
}

void Net_AdminInit(void)
{
    Net_LoadBanList();
    mmulti_setcallbacks(Net_BanCheck, Net_JoinNotify, Net_ReconnectNotify);
}

// Resolve a player reference (name or slot number string) to a connected slot,
// or -1. Name match is case-insensitive and may be a prefix.
int32_t Net_ResolvePlayer(const char *ref)
{
    if (ref[0] >= '0' && ref[0] <= '9' && !ref[1 + strspn(ref+1, "0123456789")])
    {
        int32_t s = atoi(ref);
        if ((unsigned)s < (unsigned)numplayers && g_player[s].connected)
            return s;
        return -1;
    }
    size_t const n = strlen(ref);
    for (int32_t i = 0; i < numplayers; i++)
        if (g_player[i].connected && !Bstrncasecmp(g_player[i].user_name, ref, n))
            return i;
    return -1;
}

int32_t Net_KickPlayer(const char *ref)
{
    if (myconnectindex != connecthead || numplayers < 2)
        return -1;
    int32_t slot = Net_ResolvePlayer(ref);
    if (slot < 0 || slot == myconnectindex)
        return -1;

    LOG_F(INFO, "Kicked player %d (%s)", slot, g_player[slot].user_name);
    Net_ScheduleRemove(slot, "was kicked");
    return slot;
}

static void Net_AddBanIP(uint32_t ip, const char *name)
{
    if (Net_IsLoopbackIP(ip))
    {
        LOG_F(WARNING, "Refusing to ban a loopback address (localhost).");
        return;
    }
    Net_LoadBanList();
    for (int32_t i = 0; i < g_banCount; i++)
        if (g_banList[i].ip == ip)
            return;  // already banned
    if (g_banCount >= MAXBANS)
    {
        LOG_F(ERROR, "Ban list is full (%d)", MAXBANS);
        return;
    }
    g_banList[g_banCount].ip = ip;
    Bstrncpyz(g_banList[g_banCount].name, name ? name : "", sizeof(g_banList[0].name));
    g_banCount++;
    Net_SaveBanList();
}

// Ban by connected player (name/slot) or by literal IP string.
int32_t Net_BanPlayer(const char *ref)
{
    if (myconnectindex != connecthead)
        return -1;

    char ipstr[64];
    int32_t slot = Net_ResolvePlayer(ref);

    if (slot >= 0 && slot != myconnectindex)
    {
        uint32_t ip = (uint32_t)mmulti_getslotip(slot);
        if (Net_IsLoopbackIP(ip))
        {
            LOG_F(WARNING, "Refusing to ban %s: they are on localhost.", g_player[slot].user_name);
            return -1;
        }
        Net_FormatIP(ip, ipstr, sizeof(ipstr));
        LOG_F(INFO, "Banned player %d (%s) at %s", slot, g_player[slot].user_name, ipstr);
        Net_AddBanIP(ip, g_player[slot].user_name);
        Net_ScheduleRemove(slot, "was banned");
        return slot;
    }

    // Not a connected player - treat the argument as an IP to blacklist.
    uint32_t ip = Net_ParseIP(ref);
    if (ip)
    {
        if (Net_IsLoopbackIP(ip))
        {
            LOG_F(WARNING, "Refusing to ban a loopback address (localhost).");
            return -1;
        }
        Net_FormatIP(ip, ipstr, sizeof(ipstr));
        Net_AddBanIP(ip, "");
        LOG_F(INFO, "Banned address %s", ipstr);
        return 0;
    }
    return -1;
}

int32_t Net_Unban(const char *ref)
{
    // An empty reference must match nothing: a zero-length name compare returns
    // "equal" against every entry and would wipe the whole ban list at once.
    if (!ref || !ref[0])
        return 0;

    Net_LoadBanList();
    uint32_t ip = Net_ParseIP(ref);
    size_t const n = strlen(ref);
    int32_t removed = 0;

    for (int32_t i = 0; i < g_banCount; )
    {
        bool match = (ip && g_banList[i].ip == ip)
                  || (!ip && g_banList[i].name[0] && !Bstrncasecmp(g_banList[i].name, ref, n));
        if (match)
        {
            g_banList[i] = g_banList[--g_banCount];
            removed++;
        }
        else i++;
    }
    if (removed)
        Net_SaveBanList();
    return removed;
}

void Net_ListBans(void)
{
    Net_LoadBanList();
    OSD_Printf("Ban list (%d):\n", g_banCount);
    char ipstr[64];
    for (int32_t i = 0; i < g_banCount; i++)
    {
        Net_FormatIP(g_banList[i].ip, ipstr, sizeof(ipstr));
        OSD_Printf("  %s %s\n", ipstr, g_banList[i].name);
    }
}

// Non-blocking read of console commands from stdin, for a dedicated/headless
// host driven from a terminal. Dispatches each complete line to the OSD.
void Net_PollStdin(void)
{
#ifndef _WIN32
    static char linebuf[256];
    static int32_t linelen;

    struct pollfd pfd = { 0 /*stdin*/, POLLIN, 0 };
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
    {
        char c;
        if (read(0, &c, 1) != 1)
            break;
        if (c == '\n')
        {
            linebuf[linelen] = 0;
            linelen = 0;
            if (linebuf[0])
                OSD_Dispatch(linebuf);
        }
        else if (linelen < (int32_t)sizeof(linebuf) - 1)
            linebuf[linelen++] = c;
    }
#endif
}

//////////////////////////////////////////////////////////////////////////////
// Map rotation. Host-authoritative: the host picks the next map (sequentially
// or at random) and starts it via the normal NEW_GAME broadcast, so clients
// follow through the proven map-change path.
//////////////////////////////////////////////////////////////////////////////

#define MAXMAPROT 64
struct maprot_t { int16_t vol, lev; char usermap[32]; };
static maprot_t g_mapRot[MAXMAPROT];
static int32_t g_mapRotCount;
static int32_t g_mapRotIndex = -1;
static int32_t g_mapRotLoaded;
static int32_t g_pendingRotate;

// Main-loop context: perform a rotation the sim requested at level end.
void Net_CheckMapRotation(void)
{
    if (!g_pendingRotate)
        return;
    g_pendingRotate = 0;
    if (myconnectindex != connecthead)
        return;

    if (Net_RotateNextMap() < 0)
    {
        // Rotation is enabled but the list is empty or unreadable. Clients have
        // already suppressed their own end-of-level advance and are waiting for
        // our NEW_GAME (they only know rotation is on, not that the list is
        // empty), so we must broadcast one or the match soft-locks. Restart the
        // current map - a sensible "no list -> stay here" that keeps everyone
        // in lockstep on the same level.
        LOG_F(WARNING, "Map rotation enabled but no maps available; restarting the current level");
        G_NewGame(NEWGAME_RESETALL);
    }
}

static void Net_LoadMapList(void)
{
    if (g_mapRotLoaded)
        return;
    g_mapRotLoaded = 1;
    g_mapRotCount = 0;

    int32_t fil = kopen4loadfrommod("maplist.txt", 0);
    if (fil == -1)
        return;

    int32_t len = kfilelength(fil);
    char *text = (char *)Xmalloc(len + 1);
    kread(fil, text, len);
    text[len] = 0;
    kclose(fil);

    char *line = strtok(text, "\r\n");
    while (line && g_mapRotCount < MAXMAPROT)
    {
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '#')
        {
            maprot_t &e = g_mapRot[g_mapRotCount];
            unsigned v, l;
            // "E<vol>L<level>" for built-in maps, otherwise a usermap filename.
            if ((line[0] == 'e' || line[0] == 'E') && sscanf(line + 1, "%uL%u", &v, &l) == 2)
            {
                e.vol = (int16_t)(v - 1);
                e.lev = (int16_t)(l - 1);
                e.usermap[0] = 0;
                g_mapRotCount++;
            }
            else
            {
                char name[32] = {0};
                sscanf(line, "%31s", name);
                if (name[0])
                {
                    e.vol = 0; e.lev = 7;
                    Bstrncpyz(e.usermap, name, sizeof(e.usermap));
                    g_mapRotCount++;
                }
            }
        }
        line = strtok(NULL, "\r\n");
    }
    Xfree(text);
    LOG_F(INFO, "Loaded %d map(s) into the rotation from maplist.txt", g_mapRotCount);
}

// Host: advance to the next map in the rotation and start it for everyone.
int32_t Net_RotateNextMap(void)
{
    if (myconnectindex != connecthead)
        return -1;
    Net_LoadMapList();
    if (g_mapRotCount == 0)
        return -1;

    if (g_mapRotationMode == 2)
        g_mapRotIndex = wrand() % g_mapRotCount;      // host-chosen, then broadcast
    else
        g_mapRotIndex = (g_mapRotIndex + 1) % g_mapRotCount;

    maprot_t const &e = g_mapRot[g_mapRotIndex];

    ud.m_volume_number = ud.volume_number = e.vol;
    ud.m_level_number  = ud.level_number  = e.lev;

    if (e.usermap[0])
    {
        Bstrcpy(boardfilename, e.usermap);
        // Send the map file (and host mods) BEFORE the USER_MAP name: mmulti is
        // reliable+ordered, so a client has the file on disk before the name (and
        // the NEW_GAME that loads it) arrive - no manual pre-sync, no desync onto
        // a fallback level for clients that lack the map.
        Net_SendGameFiles(-1);
        Net_SendUserMapName();
        LOG_F(INFO, "Rotating to usermap %s", e.usermap);
        Bsprintf(buf, "Next map: %s", e.usermap);
    }
    else
    {
        boardfilename[0] = 0;
        LOG_F(INFO, "Rotating to E%dL%d", e.vol + 1, e.lev + 1);
        Bsprintf(buf, "Next map: E%dL%d", e.vol + 1, e.lev + 1);
    }
    G_AddUserQuote(buf);

    G_NewGame(NEWGAME_RESETALL);  // broadcasts NEW_GAME; clients follow
    return g_mapRotIndex;
}

void Net_ListMapRotation(void)
{
    Net_LoadMapList();
    OSD_Printf("Map rotation (%d, mode %s):\n", g_mapRotCount,
               g_mapRotationMode == 2 ? "random" : g_mapRotationMode == 1 ? "sequential" : "off");
    for (int32_t i = 0; i < g_mapRotCount; i++)
    {
        if (g_mapRot[i].usermap[0])
            OSD_Printf("  %d: %s%s\n", i, g_mapRot[i].usermap, i == g_mapRotIndex ? "  <-" : "");
        else
            OSD_Printf("  %d: E%dL%d%s\n", i, g_mapRot[i].vol + 1, g_mapRot[i].lev + 1, i == g_mapRotIndex ? "  <-" : "");
    }
}

//////////////////////////////////////////////////////////////////////////////
// Host -> client file transfer (maps, mods), so clients don't have to sync
// files by hand. Reuses the chunked-stream approach proven for late-join
// snapshots. Files land in the client's mod/working directory.
//////////////////////////////////////////////////////////////////////////////

#define FILE_CHUNK_SIZE 1024
static char *g_recvFileBuf;
static char g_recvFileName[64];
static int32_t g_recvFileChunksGot, g_recvFileChunkTotal, g_recvFileLastLen;
static uint32_t g_recvFileCrc;

static uint32_t Net_FileCrc(const char *data, int32_t len)
{
    // FNV-1a - self-contained integrity check for transferred files.
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < len; i++)
        h = (h ^ (uint8_t)data[i]) * 16777619u;
    return h;
}

// True if the client already has this file with a matching size+crc.
static int Net_HaveFile(const char *name, int32_t size, uint32_t crc)
{
    int32_t fil = kopen4loadfrommod(name, 0);
    if (fil == -1)
        return 0;
    int32_t const len = kfilelength(fil);
    if (len != size)
    {
        kclose(fil);
        return 0;
    }
    char *tmp = (char *)Xmalloc(len);
    kread(fil, tmp, len);
    kclose(fil);
    uint32_t const have = Net_FileCrc(tmp, len);
    Xfree(tmp);
    return have == crc;
}

// Recursively scan `dir` (bounded depth) for a file whose basename matches
// `wantBase` case-insensitively and whose contents match size+crc. Fills `out`
// with the full path and returns 1 on the first match.
static int Net_ScanDirForFile(const char *dir, const char *wantBase, int32_t size, uint32_t crc,
                              int depth, char *out, int32_t outcap)
{
    if (depth > 6)
        return 0;
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d)
        return 0;

    int found = 0;
    struct dirent *de;
    while (!found && (de = readdir(d)) != NULL)
    {
        if (!Bstrcmp(de->d_name, ".") || !Bstrcmp(de->d_name, ".."))
            continue;

        char path[BMAX_PATH];
        Bsnprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
            found = Net_ScanDirForFile(path, wantBase, size, crc, depth + 1, out, outcap);
        else if (S_ISREG(st.st_mode) && (int32_t)st.st_size == size && !Bstrcasecmp(de->d_name, wantBase))
        {
            // name + size match - confirm identical content (same checksum the
            // host advertised) before trusting it.
            FILE *fp = fopen(path, "rb");
            if (fp)
            {
                char *tmp = (char *)Xmalloc(size);
                size_t const rd = fread(tmp, 1, size, fp);
                fclose(fp);
                if ((int32_t)rd == size && Net_FileCrc(tmp, size) == crc)
                {
                    Bstrncpyz(out, path, outcap);
                    found = 1;
                }
                Xfree(tmp);
            }
        }
    }
    closedir(d);
    return found;
}

// Look for an already-present identical copy of an offered file anywhere under
// the working directory (and the mod directory) - matched by basename
// (case-insensitive) plus size + content checksum. Returns 1 and fills `out`.
static int Net_FindExistingFile(const char *name, int32_t size, uint32_t crc, char *out, int32_t outcap)
{
    if (size <= 0)
        return 0;
    const char *fslash = Bstrrchr(name, '/');
    const char *bslash = Bstrrchr(name, '\\');
    const char *base = max(fslash, bslash);
    base = base ? base + 1 : name;
    if (!base[0])
        return 0;

    // Scan the working-directory tree only. Also scan g_modDir when it is a
    // real, scoped subfolder - never a filesystem root like "/" (which would
    // crawl the whole disk and could match an unrelated copy).
    if (Net_ScanDirForFile(".", base, size, crc, 0, out, outcap))
        return 1;
    if (g_modDir[0] && g_modDir[0] != '/' && Bstrcmp(g_modDir, ".") && Bstrcmp(g_modDir, "..")
        && Net_ScanDirForFile(g_modDir, base, size, crc, 0, out, outcap))
        return 1;
    return 0;
}

// Block until the reliable-output backlog for every serviced peer drains below
// a safe bound, so a large streamed payload (snapshot / mod file) can't outrun
// the 512-entry per-peer ring and get silently overwritten. We pump the network
// directly (mmulti_getpacket with a NULL buffer processes acks and retransmits
// without delivering app packets) so the peers' ack counters actually advance;
// gameHandleEvents keeps the machine responsive. A timeout prevents a dead or
// wedged peer from freezing the host here - if it fires, the higher-level
// late-join/idle timeouts abort the join.
// The per-peer reliable ring (opak) holds 512 entries; throttle only as the
// backlog nears that so we never overwrite an unacked slot. A normal snapshot
// (~260 chunks) stays well under this, so it blasts in one pass and trickles
// out non-blocking exactly as before - only an unusually large payload actually
// waits here. (512 = mmulti FIFSIZ; leave a margin for the chunk we're about to
// queue and any concurrent peer traffic counted in the max.)
#define NET_RELIABLE_WINDOW_MAX 480
// Returns false if the window never drained within the timeout - the peer is
// dead or wedged, and the CALLER must abort the transfer (stop queuing chunks).
// If it kept looping instead, every remaining chunk would spin its own full
// timeout while the whole server is frozen, and the reliable ring would be
// overwritten (silent corruption) once the throttle stopped holding.
static bool Net_DrainReliableWindow(void)
{
    if (mmulti_getoutputcirclesize() <= NET_RELIABLE_WINDOW_MAX)
        return true;

    uint32_t const start = timerGetTicks();
    while (mmulti_getoutputcirclesize() > NET_RELIABLE_WINDOW_MAX)
    {
        int32_t o;
        mmulti_getpacket(&o, NULL);
        gameHandleEvents();
        if (timerGetTicks() - start > 10000)
        {
            LOG_F(WARNING, "Reliable output window stalled while streaming; peer not acking - aborting transfer");
            return false;
        }
    }
    return true;
}

// Host: stream a file to one client (or, with slot < 0, every client that
// lacks it). Called before starting a map that references the file.
void Net_SendFile(int32_t slot, const char *name, int32_t fileIndex, int32_t fileTotal)
{
    if (myconnectindex != connecthead)
        return;

    int32_t fil = kopen4loadfrommod(name, 0);
    if (fil == -1)
    {
        LOG_F(WARNING, "Net_SendFile: can't open %s", name);
        return;
    }
    int32_t const size = kfilelength(fil);
    char *data = (char *)Xmalloc(size + 1);
    kread(fil, data, size);
    kclose(fil);
    uint32_t const crc = Net_FileCrc(data, size);

    int32_t const total = (size + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE;
    LOG_F(INFO, "Sending %s (%d bytes, %d chunks)%s", name, size, total,
          slot < 0 ? " to all" : "");

    char hdr[80];
    hdr[0] = PACKET_TYPE_FILE_HEADER;
    int32_t hl = 1;
    B_BUF32(&hdr[hl], size); hl += 4;
    B_BUF32(&hdr[hl], (int32_t)crc); hl += 4;
    B_BUF16(&hdr[hl], (uint16_t)total); hl += 2;
    int32_t const namelen = min((int32_t)strlen(name), 63);
    hdr[hl++] = (char)namelen;
    Bmemcpy(&hdr[hl], name, namelen); hl += namelen;
    // 1-based position of this file within the set, and the set size, so the
    // joiner can show "downloading file X of Y" for multi-file transfers.
    hdr[hl++] = (char)clamp(fileIndex, 1, 255);
    hdr[hl++] = (char)clamp(fileTotal, 1, 255);

    // An explicit slot may be a late joiner not yet marked g_player.connected
    // (we send it the map before it loads the snapshot), so gate on the mmulti
    // link in that case rather than on roster membership.
    #define NET_FILE_WANTS(i) (((i) != myconnectindex) && (slot >= 0 \
        ? ((i) == slot && mmulti_slotconnected(i)) : g_player[i].connected))

    for (int32_t i = 0; i < numplayers; i++)
    {
        if (!NET_FILE_WANTS(i))
            continue;
        mmulti_sendpacket(i, (unsigned char *)hdr, hl);
    }

    char chunk[FILE_CHUNK_SIZE + 8];
    for (int32_t seq = 0; seq < total; seq++)
    {
        int32_t const len = min(FILE_CHUNK_SIZE, size - seq * FILE_CHUNK_SIZE);
        chunk[0] = PACKET_TYPE_FILE_CHUNK;
        B_BUF16(&chunk[1], (uint16_t)seq);
        B_BUF16(&chunk[3], (uint16_t)total);
        B_BUF16(&chunk[5], (uint16_t)len);
        Bmemcpy(&chunk[7], &data[seq * FILE_CHUNK_SIZE], len);

        for (int32_t i = 0; i < numplayers; i++)
        {
            if (!NET_FILE_WANTS(i))
                continue;
            mmulti_sendpacket(i, (unsigned char *)chunk, len + 7);
        }

        if (!Net_DrainReliableWindow())
        {
            LOG_F(WARNING, "File transfer of %s aborted: a receiver stopped acking", name);
            break;  // a peer died mid-transfer; stop rather than freeze the server
        }
    }
    #undef NET_FILE_WANTS
    Xfree(data);
}

// Host: push everything a client needs to play the current game - the usermap
// (if any) and the mod group files the host loaded on its command line. mmulti
// is reliable+ordered, so callers send this BEFORE the USER_MAP name packet:
// that guarantees the file lands on disk before the client tries to open it.
void Net_SendGameFiles(int32_t slot)
{
    if (myconnectindex != connecthead)
        return;

    int32_t const total = g_netModFileCount + (boardfilename[0] ? 1 : 0);
    int32_t idx = 0;
    for (int32_t i = 0; i < g_netModFileCount; i++)
        Net_SendFile(slot, g_netModFiles[i], ++idx, total);

    if (boardfilename[0])
        Net_SendFile(slot, boardfilename, ++idx, total);
}

static void Net_ReceiveFileHeader(void)
{
    int32_t j = 1;
    int32_t const size = (int32_t)B_UNBUF32(&packbuf[j]); j += 4;
    g_recvFileCrc = (uint32_t)B_UNBUF32(&packbuf[j]); j += 4;
    g_recvFileChunkTotal = B_UNBUF16(&packbuf[j]); j += 2;
    int32_t const namelen = (uint8_t)packbuf[j++];
    Bmemcpy(g_recvFileName, &packbuf[j], min(namelen, 63));
    g_recvFileName[min(namelen, 63)] = 0;
    j += namelen;

    // Position of this file within the set (1-based) and the set size, so we can
    // show "file X of Y" when the host is sending more than one file.
    int32_t const fileIndex = (uint8_t)packbuf[j++];
    int32_t const fileTotal = (uint8_t)packbuf[j++];
    char ofY[24];
    ofY[0] = 0;
    if (fileTotal > 1)
        Bsnprintf(ofY, sizeof(ofY), " (file %d of %d)", fileIndex, fileTotal);

    DO_FREE_AND_NULL(g_recvFileBuf);
    g_recvFileChunksGot = 0;
    Net_NoteLateJoinProgress();

    // Only download a file we don't already have. A built-in map (E1L3 etc.
    // ships inside the GRP) or any other file we share byte-for-byte with the
    // host needs no transfer - a matching size+crc means our copy is identical,
    // so we use it directly. Leaving g_recvFileBuf NULL turns the streamed
    // chunks into no-ops, so we neither rewrite the file nor show a misleading
    // "Downloading" for something already in the game. A genuinely missing or
    // different-version file (crc mismatch) still downloads as before.
    if (Net_HaveFile(g_recvFileName, size, g_recvFileCrc))
    {
        LOG_F(INFO, "Host offered %s (%d bytes); already have it, skipping download", g_recvFileName, size);
        if (fileTotal > 1)   // keep the multi-file counter moving over skipped files
        {
            Bsnprintf(g_lateJoinStatus, sizeof(g_lateJoinStatus), "Have %s%s", g_recvFileName, ofY);
            g_lateJoinPercent = 100;
        }
        return;
    }

    // Also look through our directory tree: the user may already have this mod
    // sitting in a subfolder (a different name-case, or just not on the search
    // path). If a file whose basename matches case-insensitively also matches
    // byte-for-byte (same size + content checksum the host sent), reuse it
    // instead of downloading - mounting a group from where it already lives.
    char foundPath[BMAX_PATH];
    if (Net_FindExistingFile(g_recvFileName, size, g_recvFileCrc, foundPath, sizeof(foundPath)))
    {
        LOG_F(INFO, "Host offered %s (%d bytes); found an identical copy at %s, using it instead of downloading",
              g_recvFileName, size, foundPath);
        const char *ext = Bstrrchr(g_recvFileName, '.');
        if (ext && (!Bstrcasecmp(ext, ".grp") || !Bstrcasecmp(ext, ".zip")
                 || !Bstrcasecmp(ext, ".pk3") || !Bstrcasecmp(ext, ".pk4")))
        {
            if (initgroupfile(foundPath) != -1)
                LOG_F(INFO, "Mounted existing group file %s", foundPath);
        }
        Bsprintf(buf, "Using local %s", g_recvFileName);
        G_AddUserQuote(buf);
        if (fileTotal > 1)
        {
            Bsnprintf(g_lateJoinStatus, sizeof(g_lateJoinStatus), "Have %s%s", g_recvFileName, ofY);
            g_lateJoinPercent = 100;
        }
        return;
    }

    if (g_recvFileChunkTotal > 0)
        g_recvFileBuf = (char *)Xmalloc(g_recvFileChunkTotal * FILE_CHUNK_SIZE);

    LOG_F(INFO, "Receiving %s (%d bytes) from host%s", g_recvFileName, size, ofY);
    Bsprintf(buf, "Downloading %s...", g_recvFileName);
    G_AddUserQuote(buf);

    Bsnprintf(g_lateJoinStatus, sizeof(g_lateJoinStatus), "Downloading %s%s", g_recvFileName, ofY);
    g_lateJoinPercent = 0;
}

static void Net_ReceiveFileChunk(void)
{
    Net_NoteLateJoinProgress();
    if (g_recvFileBuf == NULL)
        return;
    int32_t const seq = B_UNBUF16(&packbuf[1]);
    int32_t const total = B_UNBUF16(&packbuf[3]);
    int32_t const len = B_UNBUF16(&packbuf[5]);
    if (total != g_recvFileChunkTotal || (unsigned)seq >= (unsigned)total || (unsigned)len > FILE_CHUNK_SIZE)
        return;

    Bmemcpy(&g_recvFileBuf[seq * FILE_CHUNK_SIZE], &packbuf[7], len);
    g_recvFileChunksGot++;
    if (seq == total - 1)
        g_recvFileLastLen = len;

    if (g_recvFileChunkTotal > 0)
        g_lateJoinPercent = 100 * g_recvFileChunksGot / g_recvFileChunkTotal;

    if (g_recvFileChunksGot < g_recvFileChunkTotal)
        return;

    // Complete: write the file into the mod/working directory.
    int32_t const filesize = (g_recvFileChunkTotal - 1) * FILE_CHUNK_SIZE + g_recvFileLastLen;
    uint32_t const crc = Net_FileCrc(g_recvFileBuf, filesize);
    if (crc != g_recvFileCrc)
    {
        // A corrupt transfer must not be written or mounted: a bad map/GRP on
        // disk can crash the loader or desync us. Drop it; the host's reliable
        // delivery makes a clean mismatch rare, and a failed asset surfaces as a
        // normal "file not found" rather than silent corruption.
        LOG_F(ERROR, "Received %s but CRC mismatch (%08x != %08x); discarding", g_recvFileName, crc, g_recvFileCrc);
        DO_FREE_AND_NULL(g_recvFileBuf);
        return;
    }

    // Write under the basename only - never honour an absolute path or any
    // directory components the sender's name may carry (e.g. a leading '/' or,
    // on Windows, a '\'), which would otherwise escape the mod directory. Strip
    // to the last separator of either kind, and refuse anything still containing
    // a parent-directory reference. Land it in the mod directory when one is
    // configured, otherwise the working directory.
    const char *fslash = Bstrrchr(g_recvFileName, '/');
    const char *bslash = Bstrrchr(g_recvFileName, '\\');
    const char *base = max(fslash, bslash);
    base = base ? base + 1 : g_recvFileName;
    if (base[0] == 0 || Bstrstr(base, ".."))
    {
        LOG_F(ERROR, "Refusing received file with unsafe name '%s'", g_recvFileName);
        DO_FREE_AND_NULL(g_recvFileBuf);
        return;
    }

    char temp[BMAX_PATH];
    if (g_modDir[0] && g_modDir[0] != '/')
        Bsnprintf(temp, sizeof(temp), "%s/%s", g_modDir, base);
    else
        Bstrcpy(temp, base);

    buildvfs_FILE fp = buildvfs_fopen_write(temp);
    if (fp)
    {
        buildvfs_fwrite(g_recvFileBuf, 1, filesize, fp);
        buildvfs_fclose(fp);
        LOG_F(INFO, "Wrote %s (%d bytes)", g_recvFileName, filesize);
        Bsprintf(buf, "Received %s", g_recvFileName);
        G_AddUserQuote(buf);

        // Group files (custom map/art/sound packs) must be mounted so their
        // contents become visible to the asset loader; plain files (.map, etc.)
        // are already reachable now that they sit in the mod directory. Note CON
        // gameplay code inside a mod is compiled at startup and won't hot-load.
        const char *ext = Bstrrchr(g_recvFileName, '.');
        if (ext && (!Bstrcasecmp(ext, ".grp") || !Bstrcasecmp(ext, ".zip")
                 || !Bstrcasecmp(ext, ".pk3") || !Bstrcasecmp(ext, ".pk4")))
        {
            if (initgroupfile(temp) != -1)
            {
                LOG_F(INFO, "Mounted received group file %s", g_recvFileName);
                Bsprintf(buf, "Mounted %s", g_recvFileName);
                G_AddUserQuote(buf);
            }
        }
    }
    else
        LOG_F(ERROR, "Couldn't write received file %s", g_recvFileName);

    DO_FREE_AND_NULL(g_recvFileBuf);
}

// Dedicated-server status screen, drawn in place of the 3D view. Local only.
void Net_DrawServerScreen(void)
{
    videoClearScreen(0L);

    int y = 20;
    gametext(160, y, "NETDUKE32 DEDICATED SERVER", 14, 2); y += 12;

    if (boardfilename[0])
        Bsprintf(tempbuf, "Map: %s", boardfilename);
    else
        Bsprintf(tempbuf, "Map: E%dL%d", ud.volume_number + 1, ud.level_number + 1);
    gametext(160, y, tempbuf, 0, 2); y += 12;

    Bsprintf(tempbuf, "Players: %d", numplayers - 1);  // minus the server itself
    gametext(160, y, tempbuf, 0, 2); y += 12;

    for (int32_t i = 0; i < numplayers; i++)
    {
        if (!g_player[i].connected || i == myconnectindex)
            continue;
        Bsprintf(tempbuf, "%d: %s", i, g_player[i].user_name);
        gametext(160, y, tempbuf, 0, 2); y += 8;
    }

    y += 8;
    gametext(160, y, "console: kick ban listplayers nextmap", 14, 2);

    videoNextPage();
}

void Net_ListPlayers(void)
{
    char ipstr[64];
    OSD_Printf("Players:\n");
    for (int32_t i = 0; i < numplayers; i++)
    {
        if (!g_player[i].connected)
            continue;
        if (i == myconnectindex)
            Bstrcpy(ipstr, "(this machine)");
        else
        {
            // Full address so IPv6 peers show correctly (getslotip is IPv4-only).
            mmulti_getslotaddrstr(i, ipstr, sizeof(ipstr));
            if (!ipstr[0])
                Net_FormatIP((uint32_t)mmulti_getslotip(i), ipstr, sizeof(ipstr));
        }
        OSD_Printf("  %d: %s  %s%s\n", i, g_player[i].user_name, ipstr,
                   i == connecthead ? "  [host]" : "");
    }
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

    // Async slave: our own player is authoritative-driven by the master's
    // echoed canonical input (which the master may substitute when ours arrives
    // late), so we must NOT feed local input straight into the authoritative
    // sim the way lockstep does - that races the master and desyncs the instant
    // we move. Just stage our input and send it; MASTER_TO_SLAVE applies the
    // canonical version to our fifo (see Net_GetPlayerInputFromPacket), exactly
    // like every other player.
    if (g_netAsyncMode && numplayers >= 2 && myconnectindex != connecthead)
    {
        // Backpressure: g_asyncSentCount counts inputs we've sent; our own
        // movefifoend counts the ones the master has echoed back. Their gap is
        // the master's still-unconfirmed queue of our input. Cap it so long-match
        // clock skew can't grow it until it wraps the 256-entry ring and corrupts
        // input. While throttled, the master just repeats our last input.
        if (g_asyncSentCount - g_player[myconnectindex].movefifoend < 128)
        {
            g_asyncOutPrev = g_asyncOutCur;
            g_asyncOutCur = netInput;
            g_asyncSentCount++;

            int aj = 1;
            packbuf[0] = PACKET_TYPE_SLAVE_TO_MASTER;
            Net_AddPlayerInputToPacket(&aj, 0, &g_asyncOutPrev, &g_asyncOutCur);
            Net_AddSyncInfoToPacket(&aj);
            mmulti_sendpacket(connecthead, (unsigned char*)packbuf, aj);
        }
        return;
    }

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

// ---------------------------------------------------------------------------
// Deterministic host events (server-console spawn / monster control)
//
// Any mid-game world change must happen identically on every machine at the
// same sim tick or the lockstep simulation desyncs (see NETCODE.md). So the
// host doesn't mutate the world directly: it schedules the change for a
// near-future tick, applies it locally at that tick, AND broadcasts it so every
// peer applies the same change at the same tick. Because all machines run the
// same ticks in order with the same sprite pool and RNG state, the result is
// bit-identical.
// ---------------------------------------------------------------------------

// How many ticks ahead of the host's latest generated input to schedule an
// event. Peers can never simulate past the host's movefifoend, so any tick
// beyond it is guaranteed to be in every machine's future; this margin only
// needs to cover the reliable packet reaching the peers, which it does many
// times over. Small so the command feels responsive.
#define NET_HOSTEVENT_MARGIN 12
#define NET_MAX_HOSTEVENTS 32

enum { HOSTEV_MONSTERS, HOSTEV_SPAWN };

struct hostevent_t { int32_t tick, kind, a, b, c, d, e, f; };
static hostevent_t g_hostEvents[NET_MAX_HOSTEVENTS];
static int32_t g_hostEventCount;

static void Net_QueueHostEvent(hostevent_t const &ev)
{
    if (g_hostEventCount < NET_MAX_HOSTEVENTS)
        g_hostEvents[g_hostEventCount++] = ev;
}

static void Net_ApplyHostEvent(hostevent_t const &ev)
{
    switch (ev.kind)
    {
        case HOSTEV_MONSTERS:
        {
            int32_t const on = ev.a, skill = ev.b;
            DMFLAGS_SET(DMFLAG_NOMONSTERS, !on);
            M_DMFLAGS_SET(DMFLAG_NOMONSTERS, !on);
            if (!on)
            {
                // Kill-and-remove every enemy actor. Iterating the sprite pool
                // by index and deleting in place is deterministic: the pool is
                // identical on every machine, so the same slots are freed in the
                // same order and stay in lockstep.
                for (int32_t i = 0; i < MAXSPRITES; i++)
                    if (sprite[i].statnum != MAXSTATUS && A_CheckEnemySprite(&sprite[i]))
                        A_DeleteSprite(i);
            }
            else if (skill >= 0)
                ud.player_skill = ud.m_player_skill = skill;
            break;
        }
        case HOSTEV_SPAWN:
        {
            int32_t const picnum = ev.a, x = ev.b, y = ev.c, z = ev.d, sectnum = ev.e, ang = ev.f;
            if ((unsigned)sectnum >= (unsigned)numsectors || (unsigned)picnum >= MAXTILES)
                break;
            int32_t const s = A_InsertSprite(sectnum, x, y, z, picnum, -16, 48, 48, ang, 0, 0, -1, 0);
            if (s >= 0)
                A_Spawn(-1, s);  // run the actor's spawn init (same code, same RNG, on every machine)
            break;
        }
    }
}

// Pumped once per simulated tick from G_DoMoveThings, right after movefifoplc++.
// Fires any scheduled event whose tick has arrived. Every machine reaches each
// tick in order, so they all fire at the identical tick.
void Net_RunHostEvents(void)
{
    for (int32_t i = 0; i < g_hostEventCount; )
    {
        if (movefifoplc >= g_hostEvents[i].tick)
        {
            hostevent_t const ev = g_hostEvents[i];
            g_hostEvents[i] = g_hostEvents[--g_hostEventCount];  // compact
            Net_ApplyHostEvent(ev);
        }
        else
            ++i;
    }
}

// Host side: schedule an event for a future tick, apply it locally at that tick,
// and broadcast it to every connected peer to apply at the same tick.
static void Net_ScheduleHostEvent(int32_t kind, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f)
{
    if (myconnectindex != connecthead)
        return;

    hostevent_t ev;
    ev.tick = g_player[connecthead].movefifoend + NET_HOSTEVENT_MARGIN;
    ev.kind = kind; ev.a = a; ev.b = b; ev.c = c; ev.d = d; ev.e = e; ev.f = f;

    Net_QueueHostEvent(ev);

    int32_t p = 0;
    packbuf[p++] = PACKET_TYPE_HOST_EVENT;
    B_BUF32(&packbuf[p], ev.tick); p += 4;
    packbuf[p++] = (char)ev.kind;
    B_BUF32(&packbuf[p], ev.a); p += 4;
    B_BUF32(&packbuf[p], ev.b); p += 4;
    B_BUF32(&packbuf[p], ev.c); p += 4;
    B_BUF32(&packbuf[p], ev.d); p += 4;
    B_BUF32(&packbuf[p], ev.e); p += 4;
    B_BUF32(&packbuf[p], ev.f); p += 4;

    int32_t i;
    TRAVERSE_CONNECT(i)
        if (i != myconnectindex)
            mmulti_sendpacket(i, (unsigned char *)packbuf, p);
}

void Net_HostMonsters(int32_t on, int32_t skill)
{
    Net_ScheduleHostEvent(HOSTEV_MONSTERS, on, skill, 0, 0, 0, 0);
}

int32_t Net_HostSpawn(int32_t picnum, int32_t x, int32_t y, int32_t z, int32_t sectnum, int32_t ang)
{
    if ((unsigned)sectnum >= (unsigned)numsectors || (unsigned)picnum >= MAXTILES)
        return -1;
    Net_ScheduleHostEvent(HOSTEV_SPAWN, picnum, x, y, z, sectnum, ang);
    return 0;
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
        if ((unsigned)other < MAXPLAYERS)
            g_slotLastPacketMs[other] = timerGetTicks();

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
                g_netServerDedicated = packbuf[j++];
                // Mirror the host's rotation state so our level-end handling
                // matches the host's (see Net_EndOfLevel / Net_BuildInitialSettings).
                g_mapRotationMode = packbuf[j++];

                if (M_DMFLAGS_TEST(DMFLAG_ALLOWVISIBILITYCHANGE))
                    LOG_F(INFO, "Visibility adjustment enabled for multiplayer.");

                oldnet_gotinitialsettings = true;

                break;
            }
            case PACKET_TYPE_LATEJOIN_ANNOUNCE:
            {
                if (!g_netLateJoiner)
                {
                    LOG_F(INFO, "Joining a game in progress; waiting for the state snapshot...");
                    Bstrncpyz(g_lateJoinStatus, "Connecting to game in progress", sizeof(g_lateJoinStatus));
                    g_lateJoinPercent = 0;
                    lateJoinClientStartMs = timerGetTicks();  // start the client backstop timeout
                    Net_NoteLateJoinProgress();
                }
                g_netLateJoiner = 1;
                g_noLogo = 1;  // Straight into the game once the snapshot lands.
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
                    Bstrncpyz(g_lateJoinStatus, "Receiving game state", sizeof(g_lateJoinStatus));
                    g_lateJoinPercent = 0;
                }

                Bmemcpy(&snapshotBuf[seq * SNAPSHOT_CHUNK_SIZE], &packbuf[7], len);
                snapshotChunksGot++;
                Net_NoteLateJoinProgress();
                if (seq == total - 1)
                    snapshotLastLen = len;

                if (snapshotChunkTotal > 0)
                    g_lateJoinPercent = 100 * snapshotChunksGot / snapshotChunkTotal;

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
            case PACKET_TYPE_RESTORE_SCORE:
            {
                int32_t const slot = (uint8_t)packbuf[1];
                if ((unsigned)slot < MAXPLAYERS && g_player[slot].ps)
                {
                    g_player[slot].ps->frag        = (int32_t)B_UNBUF32(&packbuf[2]);
                    g_player[slot].ps->fraggedself = (int32_t)B_UNBUF32(&packbuf[6]);
                    g_player[slot].ps->deaths      = (int32_t)B_UNBUF32(&packbuf[10]);
                }
                break;
            }
            case PACKET_TYPE_LATEJOIN_READY:
            {
                // The late joiner has reached its main loop and is now actively
                // reading packets, so it can safely receive the snapshot stream.
                // Only then do we start streaming - sending earlier races the
                // joiner's init and drops chunks it isn't yet listening for.
                if (myconnectindex == connecthead && other == lateJoinPending)
                    lateJoinJoinerReady = true;
                break;
            }
            case PACKET_TYPE_PLAYER_LEAVING:
            {
                // A client is dropping out on purpose (reconnecting after a
                // desync). Drop it cleanly, right now, so its slot and transport
                // sequence reset before its relaunched self re-handshakes.
                if (myconnectindex == connecthead && (unsigned)other < MAXPLAYERS
                    && g_player[other].connected)
                    Net_ScheduleRemove(other, "left to reconnect");
                break;
            }
            case PACKET_TYPE_HOST_EVENT:
            {
                // Host scheduled a world change (spawn / monster control) for a
                // future tick. Queue it with the same tick the host chose; we
                // apply it when our movefifoplc reaches it, in lockstep.
                if (other == connecthead && myconnectindex != connecthead)
                {
                    hostevent_t ev;
                    int32_t q = 1;
                    ev.tick = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    ev.kind = (uint8_t)packbuf[q++];
                    ev.a = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    ev.b = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    ev.c = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    ev.d = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    ev.e = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    ev.f = (int32_t)B_UNBUF32(&packbuf[q]); q += 4;
                    Net_QueueHostEvent(ev);
                }
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

                if (slot == myconnectindex)
                {
                    // We were removed by the host.
                    G_GameExit("You were removed from the game.");
                    break;
                }

                // Apply at the master-tagged tick (Net_PeerCheckDrop) so every
                // machine removes the player on the identical tick.
                g_pendingDropSlot = slot;
                g_pendingDropMask = B_UNBUF16(&packbuf[2]);
                g_pendingDropTick = (int32_t)B_UNBUF32(&packbuf[4]);
                g_netLagActive = 0;
                g_netLagMask = 0;
                break;
            }
            case PACKET_TYPE_FILE_HEADER:
                Net_ReceiveFileHeader();
                break;
            case PACKET_TYPE_FILE_CHUNK:
                Net_ReceiveFileChunk();
                break;
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
                            G_GameExit("You cannot play with different versions of " APPNAME "!");
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
                        while (packbuf[i] && i < packbufleng) i++; // skip any remaining name bytes
                        i++;                                       // and its null terminator

                        // Read the appended GUID (see Net_SendPlayerName).
                        if ((unsigned)playerNum < MAXPLAYERS)
                        {
                            int32_t g = 0;
                            while (i < packbufleng && packbuf[i] && g < NET_GUID_LEN - 1)
                                g_playerGuid[playerNum][g++] = packbuf[i++];
                            g_playerGuid[playerNum][g] = 0;
                        }

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

void Net_SendLateJoinReady(void)
{
    // Heartbeat from a late joiner to the master: "I've reached my main loop
    // and I'm reading packets now - it's safe to stream the snapshot." Sent
    // repeatedly (throttled) until we start receiving the snapshot, because a
    // single ready packet can be lost while the master is still announcing.
    static uint32_t lastReadyTic = 0;

    if (!g_netLateJoiner || numplayers < 2 || myconnectindex == connecthead)
        return;

    uint32_t const now = timerGetTicks();

    // Backstop: abort only on a genuine stall - no snapshot/file data at all for
    // NET_LATEJOIN_IDLE_MS. A large mod download keeps resetting the progress
    // timer (Net_NoteLateJoinProgress), so a slow-but-advancing transfer never
    // trips this; only a dead host or lost link does.
    if (lateJoinClientStartMs && (now - lateJoinLastProgressMs > NET_LATEJOIN_IDLE_MS))
        G_GameExit("Timed out joining the game in progress (no data from host).");

    if (now - lastReadyTic < 150)
        return;
    lastReadyTic = now;

    buf[0] = PACKET_TYPE_LATEJOIN_READY;
    buf[1] = myconnectindex;
    mmulti_sendpacket(connecthead, (unsigned char *)&buf[0], 2);
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

    // Append our stable GUID (also null-terminated) so the host can key score
    // restoration on identity rather than nickname. Remember our own too.
    Bstrncpyz(g_playerGuid[myconnectindex], g_netClientGuid, NET_GUID_LEN);
    for (i=0;g_netClientGuid[i];i++) buf[l++] = g_netClientGuid[i];
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

    // Give the freshly-joined slot a current heartbeat so the idle-timeout
    // drop doesn't fire before its first input packet arrives.
    if ((unsigned)slot < MAXPLAYERS)
        g_slotLastPacketMs[slot] = timerGetTicks();

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
    // The joiner is now in the connect list; mmulti services it normally.
    mmulti_setextraslot(-1);

    LOG_F(INFO, "Player %d (%s) joined the game in progress", slot, g_player[slot].user_name);
    Bsprintf(buf, "%s^00 joined", g_player[slot].user_name);
    G_AddUserQuote(buf);
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

    Bstrncpyz(g_lateJoinStatus, "Entering the level", sizeof(g_lateJoinStatus));
    g_lateJoinPercent = 100;

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

    // The snapshot is a pre-spawn world (our slot was never present, so it is
    // dead at the origin). Spawn ourselves now with the exact same code every
    // other machine runs against the same pre-spawn state, so the result is
    // bit-identical everywhere without depending on the savegame capturing
    // every side effect of the spawn.
    P_SpawnLateJoiner(myconnectindex);

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
    // Existing peers spawn the joiner's player locally, deterministically and at
    // this same (master-locked) tick, matching the host's pre-snapshot spawn.
    // The joiner itself gets the spawned player from the snapshot, so it does
    // not run this.
    Net_IntegrateLateJoin(slot);
    P_SpawnLateJoiner(slot);
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
                lateJoinJoinerReady = false;
                lateJoinAnnounceStart = lateJoinLastAnnounce = timerGetTicks();
                // Service this slot's reliable channel even though it isn't in
                // the connect list yet, so announce/files/snapshot deliver (with
                // retransmits) and we can hear the joiner's readiness signal.
                mmulti_setextraslot(i);
                // Forget the previous occupant's identity so score restoration
                // keys only on the GUID the NEW joiner sends (arriving during its
                // init, before we build the GO packet). A joiner that never sends
                // one simply can't reclaim a score.
                g_playerGuid[i][0] = 0;
                LOG_F(INFO, "Late joiner in slot %d; pausing for state transfer", i);

                // Tell the joiner what's going on, so it knows to stay out of
                // the startup barriers and wait for the snapshot.
                packbuf[0] = PACKET_TYPE_LATEJOIN_ANNOUNCE;
                mmulti_sendpacket(i, (unsigned char *)packbuf, 1);
                Net_SendInitialSettingsTo(i);
                break;
            }
        }
        if (lateJoinPending < 0)
            return;
    }

    // A reconnecting client can take a variable amount of time to finish its
    // own init before it starts reading packets, so a fixed announce window is
    // a race: announce too early and every copy is lost before the joiner is
    // listening. Instead, keep re-announcing until the joiner proves it is up
    // and reading packets by sending us a READY packet (lateJoinJoinerReady),
    // and only then proceed to stream the snapshot. A generous timeout guards
    // against a joiner that dies mid-init so we don't freeze the game forever.
    {
        uint32_t const now = timerGetTicks();
        if (now - lateJoinLastAnnounce > 200)
        {
            lateJoinLastAnnounce = now;
            packbuf[0] = PACKET_TYPE_LATEJOIN_ANNOUNCE;
            mmulti_sendpacket(lateJoinPending, (unsigned char *)packbuf, 1);
            Net_SendInitialSettingsTo(lateJoinPending);
        }
        if (!lateJoinJoinerReady)
        {
            if (now - lateJoinAnnounceStart > 25000)
            {
                LOG_F(WARNING, "Late joiner in slot %d never became ready; aborting join", lateJoinPending);
                mmulti_dropslot(lateJoinPending);
                lateJoinPending = -1;
                g_lateJoinFreeze = 0;
                mmulti_setextraslot(-1);
            }
            return;  // keep announcing until the joiner is listening
        }
    }

    // Wait until we have simulated every tick we broadcast, so that all
    // peers can settle on the identical state.
    if (movefifoplc < movefifosendplc)
        return;

    int32_t const slot = lateJoinPending;

    // The joiner needs the map (and any host mods) on disk before it can load
    // the state snapshot that references them. Reliable+ordered delivery means
    // these all arrive before the snapshot chunks we send next.
    Net_SendGameFiles(slot);

    if (G_SavePlayer(LATEJOIN_SAVE_SPOT))
    {
        LOG_F(ERROR, "Late join: failed to write the state snapshot");
        g_lateJoinFreeze = 0;
        lateJoinPending = -1;
        mmulti_setextraslot(-1);
        return;
    }

    int32_t const fil = kopen4loadfrommod("egamlj.sav", 0);
    if (fil == -1)
    {
        LOG_F(ERROR, "Late join: failed to read back the state snapshot");
        g_lateJoinFreeze = 0;
        lateJoinPending = -1;
        mmulti_setextraslot(-1);
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
        if (!Net_DrainReliableWindow())
        {
            // The joiner died mid-stream. Abort instead of spinning a full
            // timeout per remaining chunk (which would freeze every other
            // player and corrupt the reliable ring). The slot frees up to rejoin.
            LOG_F(WARNING, "Late join aborted: slot %d stopped acking during the snapshot", slot);
            kclose(fil);
            mmulti_dropslot(slot);
            lateJoinPending = -1;
            g_lateJoinFreeze = 0;
            mmulti_setextraslot(-1);
            g_player[slot].connected = 0;
            return;
        }
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
    // Snapshot was pre-spawn; spawn the joiner into our live world with the
    // same code the joiner and peers run, keeping every machine identical.
    P_SpawnLateJoiner(slot);
    // The joiner's GUID arrives a little later (its name packet only flushes
    // once it is fully in the game), so score restoration is deferred until
    // then - see Net_MasterCheckRestore.
    g_slotAwaitingRestore[slot] = true;
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

    // A dedicated host announces that slot 0 is a non-participating spectator so
    // every machine deterministically applies the same spectator state to it.
    g_netServerDedicated = g_netServerConsole ? 1 : 0;
    packbuf[j++] = (char)g_netServerDedicated;

    // Whether the host rotates maps. Clients must know this so that at level
    // end they suppress their own advance and wait for the host's NEW_GAME
    // (Net_EndOfLevel); otherwise the host defers while clients advance locally
    // and the machines desync onto different levels.
    packbuf[j++] = (char)g_mapRotationMode;

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

    // Map rotation is host-authoritative: on level end the host starts the
    // next rotation map for everyone via the NEW_GAME broadcast (the proven
    // path 'nextmap' uses). Clients suppress their own end-of-level advance
    // and wait for that broadcast, so nobody double-advances. The actual
    // rotation is deferred to main-loop context (Net_CheckMapRotation) since
    // we're deep in the movement code here.
    if (g_mapRotationMode && numplayers > 1)
    {
        if (myconnectindex == connecthead)
            g_pendingRotate = 1;
        return;
    }

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

// The process's own argv, stashed at startup so a desynced client can relaunch
// itself with the identical command line to reconnect (see Net_ClientReconnect).
static int32_t g_procArgc;
static char const * const *g_procArgv;
static char g_procExePath[BMAX_PATH];

void Net_StoreProcessArgs(int32_t argc, char const * const *argv)
{
    // Keep this trivial: it runs at the very top of app_main, before the engine
    // (and, on macOS, the Cocoa/CoreFoundation app) is initialized. Resolving the
    // executable path here via Bgetappdir() would spin up NSBundle/CoreFoundation
    // too early and corrupt the later SDL/Cocoa init (a headless server crashed
    // in CoreFoundation). The path is resolved lazily instead, at reconnect time.
    g_procArgc = argc;
    g_procArgv = argv;
}

// Resolve an ABSOLUTE path to our own binary, lazily and only when a reconnect
// is actually needed (long after startup, so the Cocoa app is fully up). execv()
// does not search PATH and resolves a relative argv[0] against the CURRENT
// directory - which the game may have chdir'd away from - so a bare/relative
// argv[0] would fail to relaunch. Bgetappdir() gives the real executable
// directory; join it with argv[0]'s basename.
static char const *Net_ResolveExePath(void)
{
    if (g_procExePath[0])
        return g_procExePath;

    char *dir = Bgetappdir();
    if (dir && dir[0] && g_procArgv && g_procArgv[0])
    {
        char const *base = Bstrrchr(g_procArgv[0], '/');
#ifdef _WIN32
        char const *bslash = Bstrrchr(g_procArgv[0], '\\');
        if (bslash > base) base = bslash;
#endif
        base = base ? base + 1 : g_procArgv[0];
        Bsnprintf(g_procExePath, sizeof(g_procExePath), "%s/%s", dir, base);
    }
    else if (g_procArgv && g_procArgv[0])
        Bstrncpyz(g_procExePath, g_procArgv[0], sizeof(g_procExePath));
    if (dir)
        Xfree(dir);

    return g_procExePath[0] ? g_procExePath : (g_procArgv ? g_procArgv[0] : NULL);
}

// A desynced client can't safely keep simulating against the host, and there is
// no in-process rewind, so recover by relaunching ourselves with the same
// command line. That re-runs the whole proven connect+late-join path (the host
// times out our old slot and re-admits us, restoring our score by GUID), which
// is far more robust than trying to surgically re-sync a diverged live state.
void Net_ClientReconnect(void)
{
    g_netReconnectPending = 0;

    LOG_F(WARNING, "Desynchronized from host (%s); reconnecting.",
          g_netDesyncReason[0] ? g_netDesyncReason : "unknown");

    // Tell the host we're leaving so it drops our slot immediately (freeing it
    // and resetting the transport sequence) instead of waiting out the idle
    // timeout - otherwise our relaunched self would race the host and re-handshake
    // into a still-occupied slot. Pump the network a few times so the packet is
    // actually transmitted before we tear the socket down.
    if (numplayers > 1 && myconnectindex != connecthead)
    {
        packbuf[0] = PACKET_TYPE_PLAYER_LEAVING;
        packbuf[1] = (char)myconnectindex;
        for (int32_t rep = 0; rep < 8; rep++)
        {
            mmulti_sendpacket(connecthead, (unsigned char *)packbuf, 2);
            int32_t o;
            mmulti_getpacket(&o, NULL);  // drive dosendpackets so it goes out now
        }
    }

    // Release our UDP socket first so the fresh image can rebind the same port.
    mmulti_uninitmultiplayers();

    char const *exe = Net_ResolveExePath();
    if (exe && g_procArgv)
    {
#ifdef _WIN32
        _execv(exe, (char * const *)g_procArgv);
#else
        execv(exe, (char * const *)g_procArgv);
#endif
    }

    // exec() only returns if it failed; fall back to a clean exit so a launcher
    // (or the player) can restart us.
    G_GameExit("Reconnect after desync failed; please rejoin.");
}

// Join a server chosen from the in-game lobby by relaunching ourselves as a
// client connecting to `hostport` ("a.b.c.d:port" or "[v6]:port"). Reuses the
// proven startup connection path rather than re-initialising mmulti mid-run.
// Preserves the original args (minus any prior -net block) so data dirs / mods
// stay configured.
void Net_LaunchAddress(const char *hostport)
{
    if (!hostport || !hostport[0])
        return;

    char const *exe = Net_ResolveExePath();
    if (!exe || !g_procArgv)
    {
        G_GameExit("Cannot relaunch to join a server.");
        return;
    }

    static char const *newargv[80];
    int32_t n = 0;
    for (int32_t i = 0; g_procArgv[i] && n < 70; i++)
    {
        if (!Bstrcasecmp(g_procArgv[i], "-net"))   // drop any existing net block (it runs to end of line)
            break;
        newargv[n++] = g_procArgv[i];
    }
    // Remember the target so, if the direct connect stalls (server behind NAT),
    // the connection loop can request a masterserver hole-punch to it.
    newargv[n++] = "-punchtarget";
    newargv[n++] = hostport;
    // Carry the masterserver the lobby used so the connecting client punches via
    // the same one (its own cvar may be unset on a fresh launch).
    if (g_masterServerAddr[0])
    {
        newargv[n++] = "-masteraddr";
        newargv[n++] = g_masterServerAddr;
    }
    newargv[n++] = "-net";
    newargv[n++] = "/p23530";       // a client source port distinct from the usual host port
    newargv[n++] = "/n0";
    newargv[n++] = hostport;
    newargv[n]   = NULL;

    LOG_F(INFO, "Joining %s (relaunching as client)", hostport);
    mmulti_uninitmultiplayers();
#ifdef _WIN32
    _execv(exe, (char * const *)newargv);
#else
    execv(exe, (char * const *)newargv);
#endif
    G_GameExit("Join relaunch failed; please restart to join.");
}

// Relaunch back to a plain single-player session (main menu) after a join was
// aborted or failed, dropping any -net/-punchtarget block. If `errmsg` is set,
// it is forwarded via -joinerror so the JOIN GAME menu can show why.
void Net_LaunchMenu(const char *errmsg)
{
    char const *exe = Net_ResolveExePath();
    if (!exe || !g_procArgv)
    {
        G_GameExit("Could not return to the menu.");
        return;
    }

    static char const *mv[80];
    int32_t n = 0;
    for (int32_t i = 0; g_procArgv[i] && n < 74; i++)
    {
        // Drop the net block (runs to end of line) and any prior -punchtarget /
        // -joinerror so they don't accumulate across relaunches.
        if (!Bstrcasecmp(g_procArgv[i], "-net"))
            break;
        if (!Bstrcasecmp(g_procArgv[i], "-punchtarget") || !Bstrcasecmp(g_procArgv[i], "-joinerror"))
        {
            if (g_procArgv[i + 1])
                i++;   // also skip its value
            continue;
        }
        mv[n++] = g_procArgv[i];
    }
    if (errmsg && errmsg[0])
    {
        mv[n++] = "-joinerror";
        mv[n++] = errmsg;
    }
    mv[n] = NULL;

    LOG_F(INFO, "Returning to the menu%s%s", errmsg && errmsg[0] ? ": " : "", errmsg ? errmsg : "");
    mmulti_uninitmultiplayers();
#ifdef _WIN32
    _execv(exe, (char * const *)mv);
#else
    execv(exe, (char * const *)mv);
#endif
    G_GameExit("Could not return to the menu; please restart.");
}

// Start hosting a listen server by relaunching with host net args (4 slots,
// begins immediately; others late-join). If `advertise`, also publish to the
// masterserver so it appears in the internet lobby.
void Net_LaunchHost(int32_t advertise)
{
    char const *exe = Net_ResolveExePath();
    if (!exe || !g_procArgv)
    {
        G_GameExit("Cannot relaunch to host.");
        return;
    }

    static char const *nv[80];
    int32_t n = 0;
    for (int32_t i = 0; g_procArgv[i] && n < 66; i++)
    {
        if (!Bstrcasecmp(g_procArgv[i], "-net"))
            break;
        nv[n++] = g_procArgv[i];
    }
    nv[n++] = "-startnow";
    nv[n++] = "-v1";
    nv[n++] = "-l1";
    if (advertise && g_masterServerAddr[0])
    {
        nv[n++] = "-advertise";
        nv[n++] = g_masterServerAddr;
    }
    nv[n++] = "-net";
    nv[n++] = "/p23513";
    nv[n++] = "/n0:4";
    nv[n]   = NULL;

    LOG_F(INFO, "Hosting a listen server (relaunching)%s", advertise ? " + advertising" : "");
    mmulti_uninitmultiplayers();
#ifdef _WIN32
    _execv(exe, (char * const *)nv);
#else
    execv(exe, (char * const *)nv);
#endif
    G_GameExit("Host relaunch failed; please restart to host.");
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

    // The tick counter just reset, so drop any host events still scheduled
    // against the old numbering - they'd otherwise fire at the wrong tick (or
    // never) after the restart.
    g_hostEventCount = 0;

    // Async slave outgoing stream: reset the delta base and send counter in
    // lockstep with the fifo so the first input after this clear is encoded
    // against the same zeroed base the master decodes it against.
    g_asyncOutPrev = g_asyncOutCur = {};
    g_asyncSentCount = 0;

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

    // On the host, a clean quit must reset the player's transport slot, not just
    // their game presence. Otherwise mmulti still considers the slot connected
    // (otherip stays set) and its reliable sequence counters keep the departing
    // client's values: the host would immediately treat the stale connection as
    // a fresh late joiner, and a client relaunching on the same slot would
    // deadlock against the leftover sequence state (its ocnt-0 packets look
    // "already seen"). Dropping the slot clears otherip and zeroes the counters
    // so a rejoin - however quick - starts from a clean transport.
    if (myconnectindex == connecthead && i != connecthead)
        mmulti_dropslot(i);

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

    // On a late-join host, a departing player's slot stays reserved so they (or
    // anyone) can rejoin it mid-game. Shrinking numplayers here would be
    // permanent - nothing grows it back on a late-join - so after a few
    // leave/rejoin cycles it would fall below 2, at which point the host stops
    // running the ready handshake (Net_WaitForPlayers early-returns) and the
    // next joiner hangs on "WAITING FOR PLAYERS". Keep the reserved slot count
    // stable on the host; the connect list above already dropped the slot from
    // active traversal, mirroring the empty-but-reserved state used at startup.
    if (!(myconnectindex == connecthead && g_netLateJoin))
    {
        numplayers--;
        ud.multimode--;
    }

    if (numplayers < 2)
        S_PlaySound(GENERIC_AMBIENCE17);

    pub = NUMPAGES;
    pus = NUMPAGES;
    G_UpdateScreenArea();

    P_QuickKill(g_player[i].ps);
    A_DeleteSprite(g_player[i].ps->i);

    LOG_F(INFO, "Player %d (%s) left the game", i, g_player[i].user_name);
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

    uint32_t const waitStart = timerGetTicks();

    while (1)
    {
        //if (quitevent) G_GameExit(""); // This sucks
        gameHandleEvents();

        // Master resilience: never hang here forever. If a slot never becomes
        // ready within a grace period (e.g. a client that dropped or stalled
        // during a mid-game join), disconnect the stragglers and carry on so
        // the whole server can't be frozen by one bad peer. They can reconnect.
        if (myconnectindex == connecthead && timerGetTicks() - waitStart > 10000)
        {
            int32_t dropped = 0;
            TRAVERSE_CONNECT(i)
            {
                if (i != myconnectindex
                    && g_player[i].playerreadyflag < g_player[myconnectindex].playerreadyflag)
                {
                    LOG_F(WARNING, "Player %d (%s) never became ready; disconnecting to keep the server alive.",
                          i, g_player[i].user_name);
                    g_player[i].connected = 0;
                    mmulti_dropslot(i);
                    if ((unsigned)i < MAXPLAYERS)
                        g_slotLastPacketMs[i] = 0;
                    dropped = 1;
                }
            }
            if (dropped)
            {
                Net_RebuildConnectList();
                P_SetGamePalette(g_player[myconnectindex].ps, oldPal, 11);
                return;
            }
        }

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
            // A slot scheduled for removal (e.g. a client that left and is
            // reconnecting on the same slot) will never signal ready on this
            // dead connection. Don't block the barrier on it - otherwise the
            // master sits here until the 10s timeout before it can return to the
            // main loop and process the drop + clean re-admit, which the joiner
            // experiences as being stuck on "waiting for the state snapshot".
            if (i == g_pendingKickSlot)
                continue;

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