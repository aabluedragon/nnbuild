//-------------------------------------------------------------------------
//
// VibeBuild32 multiplayer for Powerslave / Exhumed.  See vbnet.h for the
// overview; source/vibebuild32/NETCODE.md for the determinism rules this file
// has to honour.
//
// Why this is written from scratch rather than ported: PCExhumed's network.cpp
// is a set of empty function bodies.  Nothing of the original PowerSlave
// serial/IPX code survived, so there was no netcode to graft the VibeBuild32
// stack onto -- only the *game-side* multiplayer scaffolding, which is intact
// and is what makes this tractable: nNetStartSprite[] (map lotag 62) already
// gives deathmatch starts, RestartPlayer() already spawns at them and paints
// the per-player floor marker, PlayerList[]/nPlayerScore[]/playerNames[] are
// per-player already, and LogoffPlayer() already tears a player down.
//
// The transport is mmulti (source/build/src/mmulti.cpp), the same reliable,
// ordered, dual-stack IPv4+IPv6 UDP channel Duke and SW use, so -net peer
// specs, the masterserver, the NAT hole-punch and the file transfer all behave
// exactly as they do in the other games.
//
//-------------------------------------------------------------------------

#include "build.h"
#include "baselayer.h"
#include "compat.h"
#include "mmulti.h"
#include "osd.h"

#include "exhumed.h"
#include "engine.h"
#include "input.h"
#include "player.h"
#include "network.h"
#include "random.h"
#include "sound.h"
#include "status.h"
#include "keyboard.h"
#include "control.h"
#include "names.h"
#include "vbnet.h"

#include "netshared.h"   // VibeBuild32: shared masterserver client + file transfer

#if defined __APPLE__
# include <mach-o/dyld.h>
#endif
#ifndef _WIN32
# include <unistd.h>
# include <poll.h>
#endif

// ---------------------------------------------------------------------------
// Wire protocol.  Every packet's first byte is one of these.
// ---------------------------------------------------------------------------
enum
{
    EXNET_PT_PROXY = 1,   // master/slave relay wrapper (mmulti MS mode)
    EXNET_PT_INPUT,       // one player's PlayerInput for one tick
    EXNET_PT_READY,       // level-start barrier
    EXNET_PT_SYNC,        // periodic state CRC for desync detection
    EXNET_PT_OPTIONS,     // player name + colour
    EXNET_PT_QUIT,        // clean leave, effective from a named tick
    EXNET_PT_CHAT,        // typed message, HUD only -- never touches the sim
    EXNET_PT_DROP,        // master evicts a silent player, effective from a named tick
    EXNET_PT_FILE_HEADER,
    EXNET_PT_FILE_CHUNK,
    EXNET_PT_FILES_DONE,  // host has finished streaming --netfile/usermap files
};

// How far ahead of the simulated tick a machine publishes its own input.  This
// is the lockstep latency hider: at 30 Hz, 3 ticks buys 100 ms of round trip
// before anybody stalls.  It costs the local player the same 100 ms of input
// lag, which is why it is small.
#define EXNET_INPUT_DELAY   3

// Input ring.  Must comfortably exceed EXNET_INPUT_DELAY plus any jitter; 256
// ticks is ~8.5 s at 30 Hz, far more than the reliable transport will ever be
// behind before the connection is dead anyway.
#define EXNET_RINGSIZ       256
#define EXNET_RINGMASK      (EXNET_RINGSIZ - 1)

// Send a state CRC this often (in ticks).  Every tick would be pure overhead;
// a desync never heals, so catching it within a quarter second is plenty.
#define EXNET_SYNC_INTERVAL 8

// A player whose input has not arrived for this long is gone (crashed, killed,
// unplugged) rather than merely lagging.  Only the master decides this, and it
// announces the eviction so every machine applies it at the same tick -- a
// timeout each machine judged for itself would drop the player on different
// ticks and desync the survivors.
#define EXNET_INPUT_TIMEOUT_MS 12000

static uint8_t s_packbuf[1400];

// ---- lockstep state --------------------------------------------------------
static PlayerInput s_ring[kMaxPlayers][EXNET_RINGSIZ];
static uint8_t     s_ringHave[kMaxPlayers][EXNET_RINGSIZ];
static uint32_t    s_netTick;        // next tick to simulate
static uint32_t    s_sendTick;       // next tick to publish local input for
static uint32_t    s_goneTick[kMaxPlayers];  // tick a quitter stops being required
static uint8_t     s_gone[kMaxPlayers];
static int         s_enabled;
static int         s_broadcastMode;  // mmulti P2P (everyone talks to everyone)
static int         s_syncFailed;
static uint32_t    s_syncCrc[EXNET_RINGSIZ];
static uint8_t     s_syncHave[EXNET_RINGSIZ];
static uint8_t     s_ready[kMaxPlayers];
static uint32_t    s_lastInputMs[kMaxPlayers];  // master only: liveness watchdog

// ---- configuration ---------------------------------------------------------
static char s_punchTarget[128];
static char s_masterAddr[128];
static char s_autoJoinMaster[128];
char        ExNet_autoJoinServer[128];
static char s_playerName[11];
static int  s_playerColor;
static int  s_startNowPlayers = 2;
static int  s_startNow;
static int  s_wait;
static char s_netFiles[VB_MAX_NETFILES][BMAX_PATH];
static int  s_filesDone = 0;   // host said it has finished streaming (joiner side)
static int  s_netFileCount;
static vb_filerecv_t s_recv;

// ---- chat ------------------------------------------------------------------
// Chat is deliberately outside the tick stream: it is delivered on the same
// reliable channel but never reaches sPlayerInput or any simulated state, so a
// message can arrive on different ticks on different machines without costing
// a single bit of determinism.
static int  s_chatOpen;
static char s_chatBuf[72];
static int  s_chatLen;

static int ExNet_OSDCmd_Say(osdcmdptr_t parm);   // defined with the rest of the chat code

void ExNet_SetPunchTarget(const char *addr) { Bstrncpyz(s_punchTarget, addr, sizeof(s_punchTarget)); }
void ExNet_SetMasterAddr(const char *addr)  { Bstrncpyz(s_masterAddr,  addr, sizeof(s_masterAddr)); }
void ExNet_SetAutoJoin(const char *addr)    { Bstrncpyz(s_autoJoinMaster, addr, sizeof(s_autoJoinMaster)); }
void ExNet_SetAdvertise(const char *addr)   { VB_SetAdvertise(addr); }
int  ExNet_QueryLobby(const char *addr)     { return VB_QueryLobby(addr, "Exhumed"); }
void ExNet_SetPlayerName(const char *name)  { Bstrncpyz(s_playerName, name, sizeof(s_playerName)); }
void ExNet_SetPlayerColor(int color)        { s_playerColor = clamp(color, 0, 7); }
void ExNet_SetStartNowPlayers(int n)        { s_startNowPlayers = clamp(n, 2, kMaxPlayers); }
void ExNet_SetStartNow(int on)              { s_startNow = on; }
void ExNet_SetWait(int on)                  { s_wait = on; }
void ExNet_AddNetFile(const char *path)
{
    if (s_netFileCount < VB_MAX_NETFILES)
        Bstrncpyz(s_netFiles[s_netFileCount++], path, BMAX_PATH);
}

int ExNet_Enabled(void)  { return s_enabled; }
int ExNet_IsServer(void) { return myconnectindex == connecthead; }

// ---------------------------------------------------------------------------
// Packet routing.
//
// In mmulti's master/slave mode a slave only has a link to the master, so a
// slave-to-slave packet has to be relayed.  Same wrapper SW uses: prefix the
// payload with a proxy header naming the intended recipient (255 = broadcast),
// and let the master fan it out.  In P2P mode every peer is directly connected
// and the wrapper is skipped entirely.
// ---------------------------------------------------------------------------
static void ExNet_Broadcast(void *buf, int len)
{
    if (!s_broadcastMode && myconnectindex != connecthead)
    {
        uint8_t relay[sizeof(s_packbuf) + 2];
        if (len > (int)sizeof(s_packbuf)) len = sizeof(s_packbuf);
        relay[0] = EXNET_PT_PROXY;
        relay[1] = 255;
        Bmemcpy(&relay[2], buf, len);
        mmulti_sendpacket(connecthead, relay, len + 2);
        return;
    }
    for (int i = connecthead; i >= 0; i = connectpoint2[i])
        if (i != myconnectindex)
            mmulti_sendpacket(i, (unsigned char *)buf, len);
}

// Returns the payload length, with *from rewritten to the *originating* player
// when the packet arrived via the master's relay.  Returns 0 for a packet this
// machine only had to forward.
static int ExNet_Recv(int *from, uint8_t *buf)
{
    int len = mmulti_getpacket(from, buf);

    if (len < 2 || buf[0] != EXNET_PT_PROXY)
        return len;

    int const dest = buf[1];

    if (myconnectindex == connecthead)
    {
        buf[1] = (uint8_t)*from;   // rewrite to the sender so the recipient knows who spoke

        if (dest == 255)
        {
            for (int i = connecthead; i >= 0; i = connectpoint2[i])
                if (i != myconnectindex && i != *from)
                    mmulti_sendpacket(i, buf, len);
            Bmemmove(buf, buf + 2, len - 2);
            return len - 2;
        }
        if (dest == myconnectindex)
        {
            Bmemmove(buf, buf + 2, len - 2);
            return len - 2;
        }
        mmulti_sendpacket(dest, buf, len);
        return 0;
    }

    if (*from == connecthead)
    {
        *from = dest;              // the master rewrote this to the true sender
        Bmemmove(buf, buf + 2, len - 2);
        return len - 2;
    }

    LOG_F(ERROR, "Exhumed net: relayed packet from %d, expected master %d", *from, connecthead);
    return 0;
}

// ---------------------------------------------------------------------------
// Input (de)serialisation.  Only the six fields the game loop actually fills
// travel; .h and .i are never written by GetLocalInput() so sending them would
// just be noise on the wire.
// ---------------------------------------------------------------------------
#define EXNET_INPUT_BYTES 21

static void ExNet_PackInput(uint8_t *p, const PlayerInput *in)
{
    VB_put32((char *)&p[0],  (uint32_t)in->xVel);
    VB_put32((char *)&p[4],  (uint32_t)in->yVel);
    VB_put32((char *)&p[8],  (uint32_t)in->nAngle);
    VB_put32((char *)&p[12], (uint32_t)in->horizon);
    VB_put16((char *)&p[16], in->buttons);
    VB_put16((char *)&p[18], (uint16_t)in->nTarget);
    p[20] = (uint8_t)in->nItem;
}

static void ExNet_UnpackInput(const uint8_t *p, PlayerInput *in)
{
    Bmemset(in, 0, sizeof(*in));
    in->xVel    = (int32_t)VB_get32(&p[0]);
    in->yVel    = (int32_t)VB_get32(&p[4]);
    in->nAngle  = (fix16_t)VB_get32(&p[8]);
    in->horizon = (fix16_t)VB_get32(&p[12]);
    in->buttons = VB_get16(&p[16]);
    in->nTarget = (int16_t)VB_get16(&p[18]);
    in->nItem   = (int8_t)p[20];
}

// ---------------------------------------------------------------------------
// Desync detection.  A CRC over exactly the state the shared input stream is
// supposed to determine: where every player is, how they are facing, whether
// they are alive, and the RNG cursor.  If two machines disagree on that, the
// sims have already parted company.
// ---------------------------------------------------------------------------
static uint32_t ExNet_StateCrc(void)
{
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) {
        for (int b = 0; b < 4; b++) h = (h ^ ((v >> (b * 8)) & 0xff)) * 16777619u;
    };

    for (int p = 0; p < PlayerCount; p++)
    {
        int const nSprite = PlayerList[p].nSprite;
        if ((unsigned)nSprite >= MAXSPRITES)
            continue;
        mix((uint32_t)sprite[nSprite].x);
        mix((uint32_t)sprite[nSprite].y);
        mix((uint32_t)sprite[nSprite].z);
        mix((uint32_t)sprite[nSprite].ang);
        mix((uint32_t)PlayerList[p].nHealth);
    }
    mix((uint32_t)RandomGetSeed());
    mix((uint32_t)totalmoves);
    return h;
}

static void ExNet_ReportDesync(uint32_t tick, uint32_t mine, uint32_t theirs, int who)
{
    if (s_syncFailed)
        return;
    s_syncFailed = 1;
    LOG_F(ERROR, "Exhumed net: Out Of Sync at tick %u -- local crc %08x, player %d crc %08x",
          tick, mine, who, theirs);
    StatusMessage(300, "OUT OF SYNC");
}

// ---------------------------------------------------------------------------
// Incoming packets.
// ---------------------------------------------------------------------------
static void ExNet_HandlePacket(int from, const uint8_t *pkt, int len)
{
    switch (pkt[0])
    {
        case EXNET_PT_INPUT:
        {
            if (len < 1 + 4 + EXNET_INPUT_BYTES || (unsigned)from >= kMaxPlayers)
                break;
            uint32_t const tick = VB_get32(&pkt[1]);
            // A tick we have already run, or one absurdly far ahead, can only be
            // a duplicate or a corrupt packet; either way it must not scribble
            // over a live ring slot.
            if (tick < s_netTick || tick - s_netTick >= EXNET_RINGSIZ)
                break;
            ExNet_UnpackInput(&pkt[5], &s_ring[from][tick & EXNET_RINGMASK]);
            s_ringHave[from][tick & EXNET_RINGMASK] = 1;
            s_lastInputMs[from] = timerGetTicks();
            break;
        }

        case EXNET_PT_READY:
            if ((unsigned)from < kMaxPlayers)
                s_ready[from] = 1;
            break;

        case EXNET_PT_SYNC:
        {
            if (len < 9)
                break;
            uint32_t const tick = VB_get32(&pkt[1]);
            uint32_t const crc  = VB_get32(&pkt[5]);
            if (tick >= s_netTick || s_netTick - tick >= EXNET_RINGSIZ)
                break;                                   // we have not run it yet, or it aged out
            if (!s_syncHave[tick & EXNET_RINGMASK])
                break;
            if (s_syncCrc[tick & EXNET_RINGMASK] != crc)
                ExNet_ReportDesync(tick, s_syncCrc[tick & EXNET_RINGMASK], crc, from);
            break;
        }

        case EXNET_PT_OPTIONS:
            if (len >= 1 + 1 + 11 && (unsigned)from < kMaxPlayers)
            {
                nPlayerColor[from] = clamp(pkt[1], 0, 7);
                Bmemcpy(playerNames[from], &pkt[2], 10);
                playerNames[from][10] = 0;
            }
            break;

        case EXNET_PT_DROP:
        {
            // Only the master may evict, and only a slave listens to it.
            if (len < 6 || from != connecthead || ExNet_IsServer())
                break;
            int const who = pkt[1];
            if ((unsigned)who >= kMaxPlayers || s_gone[who])
                break;
            s_gone[who] = 1;
            s_goneTick[who] = max(VB_get32(&pkt[2]), s_netTick);
            LOG_F(WARNING, "Exhumed net: master dropped silent player %d at tick %u", who, s_goneTick[who]);
            break;
        }

        case EXNET_PT_QUIT:
        {
            if (len < 5 || (unsigned)from >= kMaxPlayers)
                break;
            uint32_t const tick = VB_get32(&pkt[1]);
            // Everyone drops this player at the same tick, so the sims stay
            // identical across the removal.
            s_gone[from] = 1;
            s_goneTick[from] = max(tick, s_netTick);
            LOG_F(INFO, "Exhumed net: player %d leaves at tick %u", from, s_goneTick[from]);
            break;
        }

        case EXNET_PT_CHAT:
        {
            if (len < 2 || (unsigned)from >= kMaxPlayers)
                break;
            int const n = min((int)pkt[1], len - 2);
            char text[sizeof(s_chatBuf)];
            Bmemcpy(text, &pkt[2], min(n, (int)sizeof(text) - 1));
            text[min(n, (int)sizeof(text) - 1)] = 0;
            StatusMessage(360, "%s: %s", playerNames[from], text);
            LOG_F(INFO, "Exhumed chat: %s: %s", playerNames[from], text);
            break;
        }

        case EXNET_PT_FILE_HEADER: VB_RecvFileHeader(&s_recv, pkt, "Exhumed"); break;
        case EXNET_PT_FILE_CHUNK:  VB_RecvFileChunk(&s_recv, pkt, "Exhumed");  break;
        case EXNET_PT_FILES_DONE:  s_filesDone = 1; break;

        default:
            break;
    }
}

static void ExNet_Pump(void)
{
    int from, len;

    while ((len = ExNet_Recv(&from, s_packbuf)) > 0)
        ExNet_HandlePacket(from, s_packbuf, len);
}

// ---------------------------------------------------------------------------
// Masterserver / lobby / NAT punch -- the same behaviour every other
// VibeBuild32 game has; the shared halves live in netshared.h.
// ---------------------------------------------------------------------------
static const char *ExNet_MasterAddr(void) { return s_masterAddr[0] ? s_masterAddr : "127.0.0.1"; }

void ExNet_HostAdvertise(void)
{
    if (!Masterserver_IsAdvertising || !ExNet_IsServer() || !Masterserver_IsAdvertising())
        return;

    static int punchFnRegistered = 0;
    if (!punchFnRegistered && Masterserver_SetPunchFn)
    {
        Masterserver_SetPunchFn(mmulti_punchto_str);
        punchFnRegistered = 1;
    }
    if (Masterserver_HostPump)
        Masterserver_HostPump();

    // levelnum is -1 until the first level is actually entered, so a server
    // still sitting in the connect cycle would advertise a nonsense "LEV-1".
    char map[24];
    if (levelnum < 0)
        Bstrncpyz(map, "LOBBY", sizeof(map));
    else
        Bsnprintf(map, sizeof(map), "LEV%d", (int)levelnum);

    char const *name = playerNames[connecthead];
    VB_HostAdvertisePulse(1, name && name[0] ? name : "VibeBuild32 Powerslave server",
                          mmulti_getport(), numplayers, kMaxPlayers, map);
}

void ExNet_ClientPunchAttempt(void)
{
    if (!Masterserver_MakePunchReq || !s_punchTarget[0] || !s_masterAddr[0])
        return;

    static int throttle = 0;
    if ((throttle++ % 60) != 0)
        return;

    char msEp[128];
    unsigned char pbuf[160];
    int32_t const plen = Masterserver_MakePunchReq(s_masterAddr, s_punchTarget,
                                                   msEp, sizeof(msEp), pbuf, sizeof(pbuf));
    if (plen > 0)
    {
        mmulti_sendraw_str(msEp, pbuf, plen);
        LOG_F(INFO, "Exhumed: requested NAT punch to %s via masterserver %s", s_punchTarget, msEp);
    }
}

int ExNet_ResolveAutoJoin(void)
{
    if (!s_autoJoinMaster[0])
        return 0;

    char name[32] = "";
    if (!VB_QueryFirstServer(s_autoJoinMaster, ExNet_autoJoinServer, sizeof(ExNet_autoJoinServer),
                             name, sizeof(name)))
        return 0;

    ExNet_SetPunchTarget(ExNet_autoJoinServer);
    ExNet_SetMasterAddr(s_autoJoinMaster);
    LOG_F(INFO, "joinlobby: joining \"%s\" at %s (via masterserver %s)",
          name, ExNet_autoJoinServer, s_autoJoinMaster);
    return 1;
}

// ---------------------------------------------------------------------------
// Host -> joiner file transfer.  Identical wire format to SW's, so a mod GRP or
// a user map named with --netfile reaches every joiner before the level loads.
// ---------------------------------------------------------------------------
// mmulti's per-peer reliable ring (opak) holds FIFSIZ=512 unacked entries. A file
// is streamed as one packet per 1 KB chunk with no natural pacing, so anything
// past ~512 KB would wrap the ring and silently overwrite chunks a peer has not
// acknowledged yet -- the joiner then waits forever for a chunk that will never
// be resent. Hold the sender whenever the backlog nears the ring size, pumping
// mmulti (a NULL buffer processes acks and retransmits without delivering app
// packets) so the peers' ack counters actually advance. Same shape as Duke's
// Net_DrainReliableWindow(). Returns 0 if the window never drained, in which
// case the caller must stop queuing -- otherwise every remaining chunk would
// spin its own full timeout and the ring would be corrupted anyway.
#define EXNET_RELIABLE_WINDOW_MAX 480

static int ExNet_DrainReliableWindow(void)
{
    if (mmulti_getoutputcirclesize() <= EXNET_RELIABLE_WINDOW_MAX)
        return 1;

    uint32_t const start = timerGetTicks();
    while (mmulti_getoutputcirclesize() > EXNET_RELIABLE_WINDOW_MAX)
    {
        int32_t o;
        mmulti_getpacket(&o, NULL);
        handleevents();
        if (timerGetTicks() - start > 10000)
        {
            LOG_F(WARNING, "Exhumed: reliable output window stalled while streaming; peer not acking -- aborting transfer");
            return 0;
        }
    }
    return 1;
}

static int s_sendAbort = 0;

static void ExNet_SendCb(void *ctx, const void *buf, int len)
{
    if (s_sendAbort)
        return;
    if (!ExNet_DrainReliableWindow())
    {
        s_sendAbort = 1;
        return;
    }

    int const slot = (int)(intptr_t)ctx;
    for (int i = 0; i < numplayers; i++)
        if (i != myconnectindex && (slot < 0 ? mmulti_slotconnected(i) : (i == slot && mmulti_slotconnected(i))))
            mmulti_sendpacket(i, (unsigned char *)const_cast<void *>(buf), len);
}

static void ExNet_SendFile(int slot, const char *name, int fileIndex, int fileTotal)
{
    if (!ExNet_IsServer())
        return;
    VB_SendFile(ExNet_SendCb, (void *)(intptr_t)slot, name, fileIndex, fileTotal,
                EXNET_PT_FILE_HEADER, EXNET_PT_FILE_CHUNK, "Exhumed");
}

void ExNet_SendGameFiles(int slot)
{
    if (!ExNet_IsServer())
        return;

    int const total = s_netFileCount + (gUserMapFilename[0] ? 1 : 0);

    s_sendAbort = 0;

    int idx = 0;
    for (int i = 0; i < s_netFileCount; i++)
        ExNet_SendFile(slot, s_netFiles[i], ++idx, total);
    if (gUserMapFilename[0])
        ExNet_SendFile(slot, gUserMapFilename, ++idx, total);

    // Always terminate the stream, even when there was nothing to send or the
    // send aborted: joiners block on this marker, so withholding it would turn
    // "no mod files" into a stall.
    s_sendAbort = 0;
    uint8_t done[1] = { EXNET_PT_FILES_DONE };
    ExNet_SendCb((void *)(intptr_t)slot, done, sizeof(done));
}

// Joiner side of the same exchange. The received files are mounted the moment
// they land (VB_RecvFileChunk calls initgroupfile), so this has to complete
// BEFORE InstallEngine() reads the ART -- otherwise a mod GRP would be mounted
// after the tiles were already loaded from the base game and have no effect.
void ExNet_ReceiveGameFiles(void)
{
    if (!s_enabled || ExNet_IsServer())
        return;

    int const startTic = (int)totalclock;
    int lastLog = 0;

    while (!s_filesDone)
    {
        handleevents();
        ExNet_Pump();

        if (quitevent)
            return;

        int const elapsed = (int)totalclock - startTic;
        if (elapsed / 600 > lastLog)
        {
            lastLog = elapsed / 600;
            LOG_F(INFO, "Exhumed: waiting for the host's game files (%d s)...", lastLog * 5);
        }
        if (elapsed > 120 * 120)
        {
            LOG_F(WARNING, "Exhumed: timed out waiting for the host's game files; continuing without them");
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Connect.
// ---------------------------------------------------------------------------
static void ExNet_SendOptions(void)
{
    if (!s_enabled)
        return;

    uint8_t pkt[1 + 1 + 11];
    pkt[0] = EXNET_PT_OPTIONS;
    pkt[1] = (uint8_t)s_playerColor;
    Bmemcpy(&pkt[2], playerNames[myconnectindex], 11);
    ExNet_Broadcast(pkt, sizeof(pkt));
}

void ExNet_Connect(int argc, char const * const *argv, int firstnet)
{
    if (!s_playerName[0])
        Bsnprintf(s_playerName, sizeof(s_playerName), "PLAYER");

    if (ExNet_ResolveAutoJoin())
    {
        char const *netargv[2] = { ExNet_autoJoinServer, NULL };
        if (initmultiplayersparms(1, netargv))
        {
            s_broadcastMode = (networkmode == MMULTI_MODE_P2P);
            LOG_F(INFO, "Exhumed: joining lobby server %s...", ExNet_autoJoinServer);
            while (initmultiplayerscycle())
            {
                handleevents();
                ExNet_ClientPunchAttempt();
                if (quitevent) return;
            }
        }
    }
    else if (!firstnet)
    {
        mmulti_initmultiplayers(0, NULL, 0, 0, 0);
    }
    else if (initmultiplayersparms(argc - firstnet, &argv[firstnet]))
    {
        s_broadcastMode = (networkmode == MMULTI_MODE_P2P);

        // --startnow / --nplayers / a dedicated server: come out of the connect
        // cycle as soon as that many players are here, instead of holding for
        // the full count the -net spec declared. --wait is the opposite and is
        // simply the absence of this call.
        if (s_startNow && !s_wait)
        {
            mmulti_allowlatejoin(s_startNowPlayers);
            LOG_F(INFO, "Exhumed: starting as soon as %d player(s) are connected", s_startNowPlayers);
        }

        LOG_F(INFO, "Exhumed: waiting for players...");
        while (initmultiplayerscycle())
        {
            handleevents();
            ExNet_HostAdvertise();
            ExNet_ClientPunchAttempt();
            if (quitevent) return;
        }
    }

    if (numplayers <= 1)
        return;

    s_enabled       = 1;
    nTotalPlayers   = (short)numplayers;
    nNetPlayerCount = numplayers - 1;
    nLocalPlayer    = myconnectindex;
    wConsoleNode    = myconnectindex;

    for (int i = 0; i < numplayers; i++)
        if (!playerNames[i][0])
            Bsnprintf(playerNames[i], sizeof(playerNames[i]), "PLAYER %d", i + 1);
    Bstrncpyz(playerNames[myconnectindex], s_playerName, sizeof(playerNames[0]));
    nPlayerColor[myconnectindex] = (short)s_playerColor;

    LOG_F(INFO, "Exhumed: multiplayer up -- %d players, I am player %d \"%s\" (%s), transport %s",
          numplayers, myconnectindex, playerNames[myconnectindex],
          ExNet_IsServer() ? "master" : "slave",
          s_broadcastMode ? "peer-to-peer" : "master/slave");

    ExNet_SendOptions();

    // A headless dedicated server has no HUD chat prompt, so give every machine
    // the same line over the console. OSD_Init() has already run by now (it is
    // part of the pre-InstallEngine startup).
    OSD_RegisterFunction("say", "say <text>: send a chat message to the other players", ExNet_OSDCmd_Say);
}

// ---------------------------------------------------------------------------
// Level start barrier.  Every machine resets its tick cursors to zero and then
// waits until all of them have said they are there, so tick 0 is the same
// moment everywhere.
// ---------------------------------------------------------------------------
void ExNet_LevelStart(void)
{
    if (!s_enabled)
        return;

    Bmemset(s_ring,     0, sizeof(s_ring));
    Bmemset(s_ringHave, 0, sizeof(s_ringHave));
    Bmemset(s_syncHave, 0, sizeof(s_syncHave));
    Bmemset(s_ready,    0, sizeof(s_ready));
    s_netTick   = 0;
    s_sendTick  = 0;
    s_syncFailed = 0;

    // Nobody has published input for the first EXNET_INPUT_DELAY ticks yet --
    // by construction, since publishing runs ahead of simulation by exactly
    // that much.  Seed them idle on every machine so the pipeline starts full
    // and identical.
    for (int p = 0; p < numplayers; p++)
        for (uint32_t t = 0; t < EXNET_INPUT_DELAY; t++)
            s_ringHave[p][t & EXNET_RINGMASK] = 1;

    s_ready[myconnectindex] = 1;

    uint8_t pkt[1] = { EXNET_PT_READY };
    ExNet_Broadcast(pkt, 1);

    LOG_F(INFO, "Exhumed net: waiting for all %d players at the level-start barrier...", numplayers);

    int const startTic = (int)totalclock;
    for (;;)
    {
        handleevents();
        ExNet_Pump();
        ExNet_HostAdvertise();

        int all = 1;
        for (int p = 0; p < numplayers; p++)
            if (!s_ready[p] && !s_gone[p]) { all = 0; break; }
        if (all)
            break;

        if (quitevent)
            return;

        // Re-announce periodically: a peer that was still loading when our first
        // READY went out has no other way to learn we are here.
        if ((((int)totalclock - startTic) % 120) == 0)
            ExNet_Broadcast(pkt, 1);

        if ((int)totalclock - startTic > 120 * 30)
        {
            LOG_F(WARNING, "Exhumed net: level-start barrier timed out, starting anyway");
            break;
        }
    }

    LOG_F(INFO, "Exhumed net: all players ready, starting level %d", (int)levelnum);
}

// ---------------------------------------------------------------------------
// Per game-loop pass: read what arrived, then publish local input far enough
// ahead that the simulation never has to wait on our own machine.
// ---------------------------------------------------------------------------
// Dedicated-server console: read stdin a line at a time and hand it to the OSD,
// so a headless Powerslave server is administered exactly like Duke's.
static void ExNet_PollStdin(void)
{
#ifndef _WIN32
    if (!g_forceHeadless)
        return;

    static char linebuf[256];
    static int  linelen;

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
        else if (linelen < (int)sizeof(linebuf) - 1)
            linebuf[linelen++] = c;
    }
#endif
}

// Master-only liveness watchdog.  A client that crashed or was killed never
// publishes another input, and lockstep would otherwise wait on it forever --
// freezing everybody.  The master announces the eviction with the tick it takes
// effect on, so all the survivors drop the player at the same instant.
static void ExNet_CheckTimeouts(void)
{
    if (!ExNet_IsServer() || numplayers < 2)
        return;

    uint32_t const now = timerGetTicks();

    for (int p = 0; p < numplayers; p++)
    {
        if (p == myconnectindex || s_gone[p])
            continue;
        if (!s_lastInputMs[p])                    // never heard from yet: start the clock
        {
            s_lastInputMs[p] = now;
            continue;
        }
        if (now - s_lastInputMs[p] < EXNET_INPUT_TIMEOUT_MS)
            continue;

        // Everyone is stalled on the same first missing tick, so naming the
        // master's current tick is a point every machine can honour.
        s_gone[p] = 1;
        s_goneTick[p] = s_netTick;

        uint8_t pkt[6];
        pkt[0] = EXNET_PT_DROP;
        pkt[1] = (uint8_t)p;
        VB_put32((char *)&pkt[2], s_goneTick[p]);
        ExNet_Broadcast(pkt, sizeof(pkt));

        LOG_F(WARNING, "Exhumed net: player %d timed out, dropping at tick %u", p, s_goneTick[p]);
        mmulti_dropslot(p);
    }
}

void ExNet_Update(void)
{
    if (!s_enabled)
        return;

    ExNet_Pump();
    ExNet_HostAdvertise();
    ExNet_CheckTimeouts();
    ExNet_PollStdin();

    // Publishing is driven by simulation progress, not by the wall clock: one
    // new input per tick we have run.  That makes the pipeline self-regulating
    // -- if a peer stalls us, we stop producing too, and nothing runs away.
    while (s_sendTick < s_netTick + EXNET_INPUT_DELAY)
    {
        PlayerInput const local = sPlayerInput[myconnectindex];

        s_ring[myconnectindex][s_sendTick & EXNET_RINGMASK] = local;
        s_ringHave[myconnectindex][s_sendTick & EXNET_RINGMASK] = 1;

        uint8_t pkt[1 + 4 + EXNET_INPUT_BYTES];
        pkt[0] = EXNET_PT_INPUT;
        VB_put32((char *)&pkt[1], s_sendTick);
        ExNet_PackInput(&pkt[5], &local);
        ExNet_Broadcast(pkt, sizeof(pkt));

        s_sendTick++;
    }
}

// How many ticks may run right now: however many consecutive ticks from
// s_netTick every live player has published, capped by the wall clock so a
// machine that fell behind catches up smoothly instead of in one lurch.
int ExNet_MoveFrames(int wallClockFrames)
{
    if (!s_enabled)
        return wallClockFrames;

    int ready = 0;
    while (ready < wallClockFrames)
    {
        uint32_t const t = s_netTick + ready;
        int have = 1;
        for (int p = 0; p < numplayers; p++)
        {
            if (s_gone[p] && t >= s_goneTick[p])
                continue;
            if (!s_ringHave[p][t & EXNET_RINGMASK]) { have = 0; break; }
        }
        if (!have)
            break;
        ready++;
    }

    return ready;
}

void ExNet_LoadTickInputs(void)
{
    if (!s_enabled)
        return;

    uint32_t const t = s_netTick;

    for (int p = 0; p < numplayers; p++)
    {
        if (s_gone[p] && t >= s_goneTick[p])
        {
            if (s_gone[p] == 1 && t == s_goneTick[p])
            {
                s_gone[p] = 2;          // apply the removal exactly once, on every machine
                LogoffPlayer(p);
            }
            Bmemset(&sPlayerInput[p], 0, sizeof(sPlayerInput[p]));
            continue;
        }
        sPlayerInput[p] = s_ring[p][t & EXNET_RINGMASK];
        s_ringHave[p][t & EXNET_RINGMASK] = 0;
    }
}

void ExNet_EndTick(void)
{
    if (!s_enabled)
        return;

    uint32_t const t = s_netTick++;
    uint32_t const crc = ExNet_StateCrc();

    s_syncCrc[t & EXNET_RINGMASK]  = crc;
    s_syncHave[t & EXNET_RINGMASK] = 1;

    if ((t % EXNET_SYNC_INTERVAL) == 0)
    {
        uint8_t pkt[9];
        pkt[0] = EXNET_PT_SYNC;
        VB_put32((char *)&pkt[1], t);
        VB_put32((char *)&pkt[5], crc);
        ExNet_Broadcast(pkt, sizeof(pkt));
    }

    // VB_EXNET_DEBUG=<ticks> traces the lockstep cursor. Without it a clean
    // "no desyncs" result is not worth much -- a sim that never advanced also
    // never disagrees -- so this is what makes the two-instance test mean
    // something.
    {
        static int32_t traceEvery = -1;
        if (traceEvery < 0)
        {
            char const *const env = Bgetenv("VB_EXNET_DEBUG");
            traceEvery = env != NULL ? Batoi(env) : 0;
        }
        if (traceEvery > 0 && (t % (uint32_t)traceEvery) == 0)
        {
            int const nSprite = PlayerList[myconnectindex].nSprite;
            LOG_F(INFO, "EXNET tick %u crc %08x send %u  me(%d,%d) hp %d",
                  t, crc, s_sendTick,
                  (unsigned)nSprite < MAXSPRITES ? sprite[nSprite].x : 0,
                  (unsigned)nSprite < MAXSPRITES ? sprite[nSprite].y : 0,
                  (int)PlayerList[myconnectindex].nHealth);
        }
    }
}

// ---------------------------------------------------------------------------
// Chat.  A blocking text prompt (the one the save-game menu uses) would stall
// this machine's tick stream and leave every peer waiting on input, so the
// prompt is a per-frame state machine instead: the sim keeps running at full
// rate underneath while a message is being typed.
// ---------------------------------------------------------------------------
int ExNet_ChatActive(void) { return s_chatOpen; }

static void ExNet_ChatClose(void)
{
    s_chatOpen = 0;
    s_chatLen  = 0;
    s_chatBuf[0] = 0;
    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    CONTROL_BindsEnabled = 1;
}

// One place that puts a line on the wire, whether it came from the HUD prompt
// or from the console's "say" (which is how a headless dedicated server talks).
void ExNet_ChatSendText(const char *text)
{
    if (!s_enabled || !text || !text[0])
        return;

    int const n = min((int)Bstrlen(text), (int)sizeof(s_chatBuf) - 1);

    uint8_t pkt[2 + sizeof(s_chatBuf)];
    pkt[0] = EXNET_PT_CHAT;
    pkt[1] = (uint8_t)n;
    Bmemcpy(&pkt[2], text, n);
    ExNet_Broadcast(pkt, 2 + n);

    StatusMessage(360, "%s: %s", playerNames[myconnectindex], text);
    LOG_F(INFO, "Exhumed chat: %s: %s", playerNames[myconnectindex], text);
}

static void ExNet_ChatSend(void)
{
    if (s_chatLen > 0)
        ExNet_ChatSendText(s_chatBuf);
    ExNet_ChatClose();
}

static int ExNet_OSDCmd_Say(osdcmdptr_t parm)
{
    if (parm->numparms < 1)
        return OSDCMD_SHOWHELP;
    if (!s_enabled)
    {
        OSD_Printf("say: not in a multiplayer game\n");
        return OSDCMD_OK;
    }

    char text[sizeof(s_chatBuf)];
    text[0] = 0;
    for (int i = 0; i < parm->numparms; i++)
    {
        if (i)
            Bstrncat(text, " ", sizeof(text) - Bstrlen(text) - 1);
        Bstrncat(text, parm->parms[i], sizeof(text) - Bstrlen(text) - 1);
    }

    ExNet_ChatSendText(text);
    return OSDCMD_OK;
}

void ExNet_ChatUpdate(void)
{
    if (!s_enabled)
        return;

    if (!s_chatOpen)
    {
        if (KB_UnBoundKeyPressed(sc_T))
        {
            KB_ClearKeyDown(sc_T);
            KB_FlushKeyboardQueue();
            s_chatOpen = 1;
            s_chatLen  = 0;
            s_chatBuf[0] = 0;
            // Suppress the gameplay binds while typing, so "walk forward" and
            // "open the menu" don't fire on the letters being entered.
            CONTROL_BindsEnabled = 0;
        }
        return;
    }

    while (KB_KeyWaiting())
    {
        char const ch = KB_GetCh();

        if (ch == 0)                    // extended key: swallow the scancode too
        {
            KB_GetCh();
            continue;
        }
        if (ch == asc_Enter)     { ExNet_ChatSend();  return; }
        if (ch == asc_Escape)    { ExNet_ChatClose(); return; }
        if (ch == asc_BackSpace)
        {
            if (s_chatLen > 0) s_chatBuf[--s_chatLen] = 0;
            continue;
        }
        if (ch >= 32 && ch < 127 && s_chatLen < (int)sizeof(s_chatBuf) - 1)
        {
            s_chatBuf[s_chatLen++] = ch;
            s_chatBuf[s_chatLen] = 0;
        }
    }
}

void ExNet_ChatDraw(void)
{
    if (!s_enabled || !s_chatOpen)
        return;

    char line[sizeof(s_chatBuf) + 8];
    Bsnprintf(line, sizeof(line), "SAY: %s_", s_chatBuf);
    printext(0, 10, line, kTileFont);
}

void ExNet_SendQuit(void)
{
    if (!s_enabled)
        return;

    uint8_t pkt[5];
    pkt[0] = EXNET_PT_QUIT;
    VB_put32((char *)&pkt[1], s_netTick + EXNET_INPUT_DELAY);
    ExNet_Broadcast(pkt, sizeof(pkt));

    // The reliable channel needs a moment to hand the message over before we
    // tear the socket down, or the peers sit waiting on input from a player
    // that is already gone.
    for (int i = 0; i < 30; i++)
    {
        handleevents();
        ExNet_Pump();
    }
}

void ExNet_Shutdown(void)
{
    if (!s_enabled)
        return;
    s_enabled = 0;
    mmulti_uninitmultiplayers();
}
