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
#include "init.h"
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
#include "save.h"
#include "kexinv.h"
#include "kexbreak.h"

#include "netshared.h"   // VibeBuild32: shared masterserver client + file transfer

#include <stdio.h>

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
    EXNET_PT_ROSTER,      // host-authoritative active slots at session start
    EXNET_PT_LATE_ANNOUNCE, // host -> staged client: this game is already running
    EXNET_PT_LATE_READY,    // staged client -> host: asset/snapshot phase ready
    EXNET_PT_LATE_FREEZE,   // host -> active roster: stop at the named tick
    EXNET_PT_LATE_FROZEN,   // active roster -> host: reached the freeze tick
    EXNET_PT_LATE_SNAPSHOT, // host -> staged client: one EXSN chunk
    EXNET_PT_LATE_GO,       // host -> all: atomically promote staged slot
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

// Mid-level admission deliberately has its own state machine rather than
// pretending a newly connected transport slot was present from tick zero. The
// host first waits for the client to mount any streamed assets, freezes the
// existing deterministic roster at an agreed tick, sends an EXSN world image,
// then all processes create the new player and restart the input pipeline.
#define EXNET_LATE_FREEZE_LEAD 60
#define EXNET_LATE_CHUNK_SIZE  1024
#define EXNET_LATE_MAX_BYTES   (32 * 1024 * 1024)

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
// Level-start barrier. A machine can reach the barrier AFTER a peer's READY has
// already been pumped in (the joiner drains the host's game-file stream first,
// and the host announces READY while the joiner is still loading ART), so a
// plain "have we seen READY" flag that the barrier cleared on entry lost the
// packet and waited there forever. Count level starts instead -- every machine
// runs the same number of them -- and treat a peer as ready for this level once
// the generation it announced has caught up. That also makes a straggler's
// re-announce for the level we just left harmless.
static uint32_t    s_levelGen;
static uint32_t    s_readyGen[kMaxPlayers];
static uint32_t    s_lastInputMs[kMaxPlayers];  // master only: liveness watchdog
// mmulti reserves every slot named by the -net declaration.  A --startnow
// game must distinguish a slot that is merely reserved for a future joiner
// from one whose player is already part of this deterministic simulation.
// Game-side player indices stay compact; packets and transport bookkeeping use
// the stable mmulti slot number.
static uint8_t     s_activeSlot[kMaxPlayers];
static int         s_slotToPlayer[kMaxPlayers];
static int         s_activePlayers;
static int         s_rosterReady;

// ---- late-join staging -----------------------------------------------------
static int         s_lateJoinEnabled;
static int         s_lateJoiner;
static int         s_latePendingSlot = -1; // host-only, transport slot outside the roster
static int         s_latePendingRejoin;
static int         s_lateAssetsReady;
static int         s_lateAssetsSent;
static int         s_lateSnapshotReady;
static uint32_t    s_lateStartMs;
static uint32_t    s_lateLastAnnounceMs;
static uint32_t    s_lateFreezeTick = UINT32_MAX;
static uint8_t     s_lateFrozen[kMaxPlayers];
static uint8_t     s_lateGoSlots[kMaxPlayers];
static uint8_t     s_lateGoGone[kMaxPlayers];
static int         s_lateGoSlot = -1;
static int         s_lateGoRejoin;
static int         s_lateGoReady;
static char        s_lateName[11];
static int         s_lateColor;
static char       *s_lateSnapshot;
static uint8_t    *s_lateSnapshotSeen;
static int         s_lateSnapshotChunks;
static int         s_lateSnapshotGot;
static int         s_lateSnapshotLastLen;

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
void ExNet_SetLateJoin(int on)               { s_lateJoinEnabled = on; }
void ExNet_AddNetFile(const char *path)
{
    if (s_netFileCount < VB_MAX_NETFILES)
        Bstrncpyz(s_netFiles[s_netFileCount++], path, BMAX_PATH);
}

int ExNet_Enabled(void)  { return s_enabled; }
int ExNet_IsServer(void) { return myconnectindex == connecthead; }
int ExNet_IsLateJoiner(void) { return s_lateJoiner; }

static int ExNet_SlotActive(int slot)
{
    return (unsigned)slot < kMaxPlayers && s_activeSlot[slot];
}

static int ExNet_PlayerForSlot(int slot)
{
    return ExNet_SlotActive(slot) ? s_slotToPlayer[slot] : -1;
}

static int ExNet_ApplyRoster(uint8_t const *slots)
{
    Bmemset(s_activeSlot, 0, sizeof(s_activeSlot));
    for (int i = 0; i < kMaxPlayers; i++)
        s_slotToPlayer[i] = -1;

    s_activePlayers = 0;
    for (int slot = 0; slot < numplayers; slot++)
    {
        if (slots[slot] != 1)
            continue;
        s_activeSlot[slot] = 1;
        s_slotToPlayer[slot] = s_activePlayers;
        s_activePlayers++;
    }

    s_rosterReady = s_activePlayers > 1 && ExNet_SlotActive(myconnectindex);
    return s_rosterReady;
}

static void ExNet_ClearLateSnapshot(void)
{
    if (s_lateSnapshot)
        Xfree(s_lateSnapshot);
    if (s_lateSnapshotSeen)
        Xfree(s_lateSnapshotSeen);
    s_lateSnapshot = NULL;
    s_lateSnapshotSeen = NULL;
    s_lateSnapshotChunks = 0;
    s_lateSnapshotGot = 0;
    s_lateSnapshotLastLen = 0;
}

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
        if (i != myconnectindex && ExNet_SlotActive(i) && !s_gone[i])
            mmulti_sendpacket(i, (unsigned char *)buf, len);
}

// Unlike a normal game packet, the staged late-join slot is not in the active
// roster yet. It still needs a direct reliable path to the host for the
// announce, file stream, snapshot and GO packet.
static void ExNet_SendDirect(int slot, void *buf, int len)
{
    if ((unsigned)slot < (unsigned)numplayers && slot != myconnectindex && mmulti_slotconnected(slot))
        mmulti_sendpacket(slot, (unsigned char *)buf, len);
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
                if (i != myconnectindex && i != *from && ExNet_SlotActive(i) && !s_gone[i])
                    mmulti_sendpacket(i, buf, len);
            Bmemmove(buf, buf + 2, len - 2);
            return len - 2;
        }
        if (dest == myconnectindex)
        {
            Bmemmove(buf, buf + 2, len - 2);
            return len - 2;
        }
        if (ExNet_SlotActive(dest) && !s_gone[dest])
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

// A snapshot transfer has to restore the full game-side roster, not just the
// host's player. This opt-in probe runs between two lockstep ticks on the host:
// it captures EXSN, damages every active player record in memory, reloads it,
// and verifies all the player-facing state came back byte-for-byte. The next
// simulation tick therefore starts from exactly the same state the client has.
static uint32_t ExNet_SnapshotPlayerHash(void)
{
    uint32_t h = 2166136261u;
    auto mix = [&h](void const *data, size_t bytes) {
        uint8_t const *p = (uint8_t const *)data;
        while (bytes--)
            h = (h ^ *p++) * 16777619u;
    };
    mix(PlayerList, sizeof(PlayerList[0]) * kMaxPlayers);
    mix(nPlayerScore, sizeof(nPlayerScore[0]) * kMaxPlayers);
    mix(nPlayerColor, sizeof(nPlayerColor[0]) * kMaxPlayers);
    mix(playerNames, sizeof(playerNames));
    mix(nPlayerWeapons, sizeof(nPlayerWeapons[0]) * kMaxPlayers);
    mix(nPistolClip, sizeof(nPistolClip[0]) * kMaxPlayers);
    mix(nPlayerClip, sizeof(nPlayerClip[0]) * kMaxPlayers);
    mix(nPlayerDouble, sizeof(nPlayerDouble[0]) * kMaxPlayers);
    mix(nPlayerFloorSprite, sizeof(nPlayerFloorSprite[0]) * kMaxPlayers);
    mix(nPlayerGrenade, sizeof(nPlayerGrenade));
    mix(KexInvList, sizeof(KexInvList[0]) * kMaxPlayers);
    return h;
}

static void ExNet_NetworkSnapshotSelftest(void)
{
    static int state = -1;
    if (state < 0)
    {
        char const *const path = Bgetenv("VB_EXNET_SNAPSHOT_FILE");
        state = ExNet_IsServer() && path && path[0] ? 1 : 0;
    }
    if (state != 1 || s_netTick < 120)
        return;
    state = 2;

    char const *const path = Bgetenv("VB_EXNET_SNAPSHOT_FILE");
    uint32_t const expected = ExNet_SnapshotPlayerHash();
    LoadSave::SaveNetworkSnapshot(path);

    for (int player = 0; player < nTotalPlayers; player++)
    {
        PlayerList[player].nHealth = (short)(-100 - player);
        nPlayerScore[player] ^= 0x5a5a;
        nPlayerWeapons[player] ^= 0x55;
        KexInvList[player].artifacts ^= UINT32_C(0x55aa55aa);
    }

    LoadSave::LoadNetworkSnapshot(path);
    int const localSpriteOkay = (unsigned)nLocalPlayer < (unsigned)kMaxPlayers
        && nLocalSpr == PlayerList[nLocalPlayer].nSprite;
    int const okay = expected == ExNet_SnapshotPlayerHash() && localSpriteOkay;
    if (okay)
        LOG_F(INFO, "EXNET snapshot test: PASS (all-player EXSN, local sprite OK)");
    else
        LOG_F(ERROR, "EXNET snapshot test: FAIL (all-player EXSN, local sprite %s)",
              localSpriteOkay ? "OK" : "MISMATCH");
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
        case EXNET_PT_ROSTER:
        {
            // Only the master may define the initial session roster. A later
            // roster change uses LATE_GO and an EXSN image, never this startup
            // packet, so it cannot be mistaken for an ordinary connection.
            if (len != 1 + kMaxPlayers || from != connecthead || ExNet_IsServer() || s_rosterReady)
                break;
            for (int slot = 0; slot < kMaxPlayers; slot++)
                if (pkt[1 + slot] > 1)
                    return;
            if (ExNet_ApplyRoster(&pkt[1]))
                LOG_F(INFO, "Exhumed net: received initial roster (%d active player(s))", s_activePlayers);
            break;
        }

        case EXNET_PT_INPUT:
        {
            if (len < 1 + 4 + EXNET_INPUT_BYTES || !ExNet_SlotActive(from))
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
        {
            if (len < 5 || !ExNet_SlotActive(from))
                break;
            uint32_t const gen = VB_get32(&pkt[1]);
            if (gen > s_readyGen[from])
                s_readyGen[from] = gen;
            break;
        }

        case EXNET_PT_SYNC:
        {
            if (len < 9 || !ExNet_SlotActive(from))
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
            if (len >= 1 + 1 + 11 && ExNet_SlotActive(from))
            {
                int const player = ExNet_PlayerForSlot(from);
                nPlayerColor[player] = clamp(pkt[1], 0, 7);
                Bmemcpy(playerNames[player], &pkt[2], 10);
                playerNames[player][10] = 0;
            }
            break;

        case EXNET_PT_DROP:
        {
            // Only the master may evict, and only a slave listens to it.
            if (len < 6 || from != connecthead || ExNet_IsServer())
                break;
            int const who = pkt[1];
            if (!ExNet_SlotActive(who) || s_gone[who])
                break;
            s_gone[who] = 1;
            s_goneTick[who] = max(VB_get32(&pkt[2]), s_netTick);
            LOG_F(WARNING, "Exhumed net: master dropped silent player %d at tick %u", who, s_goneTick[who]);
            break;
        }

        case EXNET_PT_QUIT:
        {
            if (len < 5 || !ExNet_SlotActive(from))
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
            if (len < 2 || !ExNet_SlotActive(from))
                break;
            int const player = ExNet_PlayerForSlot(from);
            int const n = min((int)pkt[1], len - 2);
            char text[sizeof(s_chatBuf)];
            Bmemcpy(text, &pkt[2], min(n, (int)sizeof(text) - 1));
            text[min(n, (int)sizeof(text) - 1)] = 0;
            StatusMessage(360, "%s: %s", playerNames[player], text);
            LOG_F(INFO, "Exhumed chat: %s: %s", playerNames[player], text);
            break;
        }

        case EXNET_PT_FILE_HEADER: VB_RecvFileHeader(&s_recv, pkt, "Exhumed"); break;
        case EXNET_PT_FILE_CHUNK:  VB_RecvFileChunk(&s_recv, pkt, "Exhumed");  break;
        case EXNET_PT_FILES_DONE:  s_filesDone = 1; break;

        case EXNET_PT_LATE_ANNOUNCE:
            if (len == 1 && from == connecthead && !ExNet_IsServer())
            {
                s_lateJoiner = 1;
                LOG_F(INFO, "Exhumed net: joining a game already in progress; waiting for host state");
            }
            break;

        case EXNET_PT_LATE_READY:
            // The staging client sends phase 0 immediately after it has heard
            // the announce, then phase 1 after the ordinary game-file stream
            // has mounted and its engine is ready to load an EXSN image.
            if (ExNet_IsServer() && from == s_latePendingSlot && len == 1 + 1 + 1 + 11)
            {
                int const phase = pkt[1];
                s_lateColor = clamp(pkt[2], 0, 7);
                Bmemcpy(s_lateName, &pkt[3], sizeof(s_lateName));
                s_lateName[sizeof(s_lateName) - 1] = 0;
                if (phase == 0)
                    s_lateAssetsReady = 1;
                else if (phase == 1 && s_lateAssetsSent)
                    s_lateSnapshotReady = 1;
            }
            break;

        case EXNET_PT_LATE_FREEZE:
            if (len == 1 + 4 && from == connecthead && !s_lateJoiner && ExNet_SlotActive(myconnectindex))
            {
                uint32_t const tick = VB_get32(&pkt[1]);
                if (tick == UINT32_MAX)
                {
                    s_lateFreezeTick = UINT32_MAX;
                    Bmemset(s_lateFrozen, 0, sizeof(s_lateFrozen));
                    LOG_F(INFO, "Exhumed net: late-join transfer released without promotion");
                    break;
                }
                // A freeze instruction which arrived after its named tick is
                // unsafe: continuing would create a divergent snapshot. Leave
                // the existing roster alone and let the host's timeout abort
                // the staged connection instead.
                if (tick >= s_netTick)
                {
                    s_lateFreezeTick = tick;
                    Bmemset(s_lateFrozen, 0, sizeof(s_lateFrozen));
                    LOG_F(INFO, "Exhumed net: freezing at tick %u for a late join", tick);
                }
                else
                    LOG_F(WARNING, "Exhumed net: late-join freeze tick %u arrived after local tick %u", tick, s_netTick);
            }
            break;

        case EXNET_PT_LATE_FROZEN:
            if (ExNet_IsServer() && len == 1 + 4 && ExNet_SlotActive(from)
                && s_lateFreezeTick == VB_get32(&pkt[1]))
                s_lateFrozen[from] = 1;
            break;

        case EXNET_PT_LATE_SNAPSHOT:
        {
            if (!s_lateJoiner || from != connecthead || len < 1 + 2 + 2 + 2)
                break;
            int const seq = VB_get16(&pkt[1]);
            int const total = VB_get16(&pkt[3]);
            int const bytes = VB_get16(&pkt[5]);
            if (total <= 0 || total > EXNET_LATE_MAX_BYTES / EXNET_LATE_CHUNK_SIZE
                || seq < 0 || seq >= total || bytes <= 0 || bytes > EXNET_LATE_CHUNK_SIZE
                || len != 1 + 2 + 2 + 2 + bytes)
                break;
            if (!s_lateSnapshot || s_lateSnapshotChunks != total)
            {
                ExNet_ClearLateSnapshot();
                s_lateSnapshot = (char *)Xmalloc((size_t)total * EXNET_LATE_CHUNK_SIZE);
                s_lateSnapshotSeen = (uint8_t *)Xcalloc(total, sizeof(*s_lateSnapshotSeen));
                s_lateSnapshotChunks = total;
            }
            Bmemcpy(&s_lateSnapshot[seq * EXNET_LATE_CHUNK_SIZE], &pkt[7], bytes);
            if (!s_lateSnapshotSeen[seq])
            {
                s_lateSnapshotSeen[seq] = 1;
                s_lateSnapshotGot++;
            }
            if (seq == total - 1)
                s_lateSnapshotLastLen = bytes;
            break;
        }

        case EXNET_PT_LATE_GO:
            if (len == 1 + 1 + 1 + 1 + 11 + kMaxPlayers + kMaxPlayers && from == connecthead)
            {
                int const slot = pkt[1];
                if ((unsigned)slot >= (unsigned)numplayers || pkt[3] > 1)
                    break;
                for (int i = 0; i < kMaxPlayers; i++)
                    if (pkt[15 + i] > 1 || pkt[15 + kMaxPlayers + i] > 1)
                        return;
                s_lateGoSlot = slot;
                s_lateColor = clamp(pkt[2], 0, 7);
                s_lateGoRejoin = pkt[3];
                Bmemcpy(s_lateName, &pkt[4], sizeof(s_lateName));
                s_lateName[sizeof(s_lateName) - 1] = 0;
                Bmemcpy(s_lateGoSlots, &pkt[15], sizeof(s_lateGoSlots));
                Bmemcpy(s_lateGoGone, &pkt[15 + kMaxPlayers], sizeof(s_lateGoGone));
                s_lateGoReady = 1;
            }
            break;

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

static void ExNet_SendRoster(void)
{
    uint8_t pkt[1 + kMaxPlayers];
    pkt[0] = EXNET_PT_ROSTER;
    Bmemcpy(&pkt[1], s_activeSlot, sizeof(s_activeSlot));
    ExNet_Broadcast(pkt, sizeof(pkt));
}

static int ExNet_WaitForRoster(void)
{
    uint32_t const start = timerGetTicks();
    while (!s_rosterReady && !s_lateJoiner)
    {
        handleevents();
        ExNet_Pump();
        ExNet_ClientPunchAttempt();
        if (quitevent || timerGetTicks() - start > 15000)
            return 0;
    }
    return s_rosterReady || s_lateJoiner;
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

    int const hostPlayer = ExNet_PlayerForSlot(connecthead);
    char const *name = hostPlayer >= 0 ? playerNames[hostPlayer] : "";
    int const advertisedPlayers = s_rosterReady ? s_activePlayers : 1;
    VB_HostAdvertisePulse(1, name && name[0] ? name : "VibeBuild32 Powerslave server",
                          mmulti_getport(), advertisedPlayers, kMaxPlayers, map);
}

void ExNet_ClientPunchAttempt(void)
{
    if (!Masterserver_MakePunchReq || !s_punchTarget[0] || !s_masterAddr[0])
        return;

    // Wall-clock throttle, not an iteration counter: the connect cycle this runs
    // from spins far faster than the tic rate, and counting iterations turned
    // "every 60" into a punch request several hundred times a second.
    static int lastPunch = 0;
    int const now = (int)totalclock;
    if (lastPunch && now - lastPunch < 60)
        return;
    lastPunch = now ? now : 1;

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
        if (i != myconnectindex && (slot < 0 ? ExNet_SlotActive(i) : (i == slot && mmulti_slotconnected(i))))
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
    Bmemcpy(&pkt[2], playerNames[nLocalPlayer], 11);
    ExNet_Broadcast(pkt, sizeof(pkt));
}

static void ExNet_SendLateReady(int phase)
{
    if (!s_enabled || !s_lateJoiner || ExNet_IsServer())
        return;

    uint8_t pkt[1 + 1 + 1 + 11];
    pkt[0] = EXNET_PT_LATE_READY;
    pkt[1] = (uint8_t)phase;
    pkt[2] = (uint8_t)s_playerColor;
    Bmemset(&pkt[3], 0, 11);
    Bstrncpyz((char *)&pkt[3], s_playerName, 11);
    ExNet_SendDirect(connecthead, pkt, sizeof(pkt));
}

static int ExNet_ApplyLateRoster(void)
{
    if (!s_lateGoReady || (unsigned)s_lateGoSlot >= (unsigned)numplayers
        || s_lateGoSlots[s_lateGoSlot] != 1 || s_lateGoGone[s_lateGoSlot])
        return 0;

    int const oldPlayers = s_activePlayers;
    int oldMap[kMaxPlayers];
    Bmemcpy(oldMap, s_slotToPlayer, sizeof(oldMap));

    int wanted = 0;
    for (int slot = 0; slot < numplayers; slot++)
        wanted += s_lateGoSlots[slot] != 0;
    if (wanted < 2 || wanted > kMaxPlayers)
        return 0;

    // Compact game indices are a public deterministic contract. Reordering an
    // existing player would remap every PlayerList[] record in the snapshot,
    // so this first implementation only admits a slot that appends to it.
    if (!s_lateJoiner)
        for (int slot = 0; slot < numplayers; slot++)
            if (oldMap[slot] >= 0 && s_lateGoSlots[slot] != 1)
                return 0;

    if (!ExNet_ApplyRoster(s_lateGoSlots))
        return 0;

    for (int slot = 0; slot < numplayers; slot++)
        if (oldMap[slot] >= 0 && ExNet_PlayerForSlot(slot) != oldMap[slot])
            return 0;

    int const newPlayer = ExNet_PlayerForSlot(s_lateGoSlot);
    if (s_lateGoRejoin)
    {
        // A rejoin reactivates a departed record in place. Its PlayerList[]
        // index was part of the frozen EXSN image, so changing it would make
        // every existing peer address a different player after the handoff.
        if ((!s_lateJoiner && (oldPlayers != wanted || oldMap[s_lateGoSlot] != newPlayer
                               || s_gone[s_lateGoSlot] != 2))
            || newPlayer < 0 || newPlayer >= wanted)
            return 0;
    }
    else if (newPlayer != wanted - 1 || (!s_lateJoiner && oldPlayers != wanted - 1))
        return 0;

    nTotalPlayers = (short)wanted;
    nNetPlayerCount = wanted - 1;
    nLocalPlayer = ExNet_PlayerForSlot(myconnectindex);
    wConsoleNode = ExNet_PlayerForSlot(connecthead);
    if (nLocalPlayer < 0 || wConsoleNode < 0)
        return 0;

    for (int slot = 0; slot < numplayers; slot++)
    {
        s_gone[slot] = s_lateGoGone[slot];
        s_goneTick[slot] = 0;
    }
    // A reclaimed slot may still carry the old process's watchdog timestamp.
    // Treat the authoritative GO as current traffic so the master gives the
    // returning client the normal input-delay window after the rebase.
    s_lastInputMs[s_lateGoSlot] = timerGetTicks();

    return newPlayer;
}

static int ExNet_SpawnLatePlayer(int player)
{
    if (player < 0 || player >= nTotalPlayers)
        return 0;
    if (s_lateGoRejoin)
    {
        // LogoffPlayer() deliberately hides but does not destroy the body,
        // floor marker or player record. Restarting that same record preserves
        // the score and compact roster index while producing a fresh spawn.
        if (PlayerCount != nTotalPlayers || PlayerList[player].someNetVal != -1)
            return 0;
    }
    else
    {
        if (PlayerCount != player || GrabPlayer() != player)
            return 0;
        InitPlayerInventory(player);
    }

    PlayerList[player].someNetVal = player == wConsoleNode ? -3 : -4;
    Bstrncpyz(playerNames[player], s_lateName[0] ? s_lateName : "PLAYER", sizeof(playerNames[player]));

    if (!KexCheckpoint_PrepareSpawn(player))
    {
        SetSavePoint(player, initx, inity, initz, initsect, inita);
        KexTravel_PrepareSpawn(player, inita);
    }
    RestartPlayer(player);
    InitPlayerKeys(player);
    KexCampaign_FinishSpawn(player);
    KexCheckpoint_CaptureEntry(player);
    KexCheckpoint_FinishSpawn(player);

    nPlayerColor[player] = (short)s_lateColor;
    return 1;
}

static void ExNet_RebaseLateJoin(void)
{
    Bmemset(s_ring, 0, sizeof(s_ring));
    Bmemset(s_ringHave, 0, sizeof(s_ringHave));
    Bmemset(s_syncHave, 0, sizeof(s_syncHave));
    s_netTick = 0;
    // These initial delayed-input ticks are intentionally idle. Existing peers
    // may resume a few frames before the staging process leaves its loader, so
    // accepting a freshly sampled tick 0 input here would desync that player
    // from the joiner. Real input starts at tick EXNET_INPUT_DELAY on everyone.
    s_sendTick = EXNET_INPUT_DELAY;
    s_syncFailed = 0;

    for (int slot = 0; slot < numplayers; slot++)
        if (ExNet_SlotActive(slot) && !s_gone[slot])
            for (uint32_t t = 0; t < EXNET_INPUT_DELAY; t++)
                s_ringHave[slot][t & EXNET_RINGMASK] = 1;

    s_lateFreezeTick = UINT32_MAX;
    Bmemset(s_lateFrozen, 0, sizeof(s_lateFrozen));
    s_lateGoReady = 0;
    s_lateGoSlot = -1;
    s_lateGoRejoin = 0;
    s_latePendingRejoin = 0;
    s_lateJoiner = 0;
    if (ExNet_IsServer())
        mmulti_setextraslot(-1);
}

static int ExNet_FinishLatePromotion(int player)
{
    int const slot = s_lateGoSlot;
    if (!ExNet_SpawnLatePlayer(player))
        return 0;
    ExNet_RebaseLateJoin();
    LOG_F(INFO, "Exhumed net: promoted transport slot %d as player %d; lockstep rebased",
          slot, player);
    return 1;
}

static void ExNet_LateSnapshotPath(char *path, size_t size)
{
    char const *const tmp = Bgetenv("TMPDIR");
#ifdef _WIN32
    Bsnprintf(path, size, "%s\\exnet-late-%u.exsn", tmp && tmp[0] ? tmp : ".", timerGetTicks());
#else
    Bsnprintf(path, size, "%s/exnet-late-%ld.exsn", tmp && tmp[0] ? tmp : ".", (long)getpid());
#endif
}

static void ExNet_AbortLateJoin(char const *reason)
{
    int const slot = s_latePendingSlot;
    LOG_F(WARNING, "Exhumed net: late join aborted%s%s", reason ? ": " : "", reason ? reason : "");

    // Existing peers may already be parked at the agreed tick. A special
    // all-ones freeze value is a release marker, not a simulation tick.
    if (s_lateFreezeTick != UINT32_MAX)
    {
        uint8_t thaw[1 + 4];
        thaw[0] = EXNET_PT_LATE_FREEZE;
        VB_put32((char *)&thaw[1], UINT32_MAX);
        ExNet_Broadcast(thaw, sizeof(thaw));
    }
    s_lateFreezeTick = UINT32_MAX;
    Bmemset(s_lateFrozen, 0, sizeof(s_lateFrozen));

    if ((unsigned)slot < (unsigned)numplayers)
        mmulti_dropslot(slot);
    mmulti_setextraslot(-1);
    s_latePendingSlot = -1;
    s_latePendingRejoin = 0;
    s_lateAssetsReady = 0;
    s_lateAssetsSent = 0;
    s_lateSnapshotReady = 0;
}

static int ExNet_SendLateSnapshot(void)
{
    char path[BMAX_PATH];
    ExNet_LateSnapshotPath(path, sizeof(path));
    LoadSave::SaveNetworkSnapshot(path);

    FILE *const fp = Bfopen(path, "rb");
    if (!fp)
    {
        ExNet_AbortLateJoin("could not read the EXSN image");
        return 0;
    }
    Bfseek(fp, 0, SEEK_END);
    long const fileBytes = Bftell(fp);
    Bfseek(fp, 0, SEEK_SET);
    if (fileBytes <= 0 || fileBytes > EXNET_LATE_MAX_BYTES)
    {
        Bfclose(fp);
        remove(path);
        ExNet_AbortLateJoin("EXSN image is outside the transfer limit");
        return 0;
    }

    int const chunks = ((int)fileBytes + EXNET_LATE_CHUNK_SIZE - 1) / EXNET_LATE_CHUNK_SIZE;
    if (chunks <= 0 || chunks > 65535)
    {
        Bfclose(fp);
        remove(path);
        ExNet_AbortLateJoin("EXSN image has too many chunks");
        return 0;
    }

    LOG_F(INFO, "Exhumed net: streaming %ld-byte EXSN image to staged slot %d", fileBytes, s_latePendingSlot);
    for (int seq = 0; seq < chunks; seq++)
    {
        int const bytes = min(EXNET_LATE_CHUNK_SIZE, (int)fileBytes - seq * EXNET_LATE_CHUNK_SIZE);
        uint8_t pkt[1 + 2 + 2 + 2 + EXNET_LATE_CHUNK_SIZE];
        pkt[0] = EXNET_PT_LATE_SNAPSHOT;
        VB_put16((char *)&pkt[1], (uint16_t)seq);
        VB_put16((char *)&pkt[3], (uint16_t)chunks);
        VB_put16((char *)&pkt[5], (uint16_t)bytes);
        if (Bfread(&pkt[7], 1, bytes, fp) != (size_t)bytes)
        {
            Bfclose(fp);
            remove(path);
            ExNet_AbortLateJoin("could not read an EXSN chunk");
            return 0;
        }
        ExNet_SendDirect(s_latePendingSlot, pkt, 7 + bytes);
        if (!ExNet_DrainReliableWindow())
        {
            Bfclose(fp);
            remove(path);
            ExNet_AbortLateJoin("staged client stopped acknowledging EXSN data");
            return 0;
        }
    }
    Bfclose(fp);
    remove(path);

    uint8_t roster[kMaxPlayers];
    Bmemcpy(roster, s_activeSlot, sizeof(roster));
    roster[s_latePendingSlot] = 1;

    uint8_t gone[kMaxPlayers];
    for (int slot = 0; slot < kMaxPlayers; slot++)
        gone[slot] = s_gone[slot] != 0;
    gone[s_latePendingSlot] = 0;  // this is the player being admitted/rejoined

    uint8_t go[1 + 1 + 1 + 1 + 11 + kMaxPlayers + kMaxPlayers] = {};
    go[0] = EXNET_PT_LATE_GO;
    go[1] = (uint8_t)s_latePendingSlot;
    go[2] = (uint8_t)s_lateColor;
    go[3] = (uint8_t)s_latePendingRejoin;
    Bstrncpyz((char *)&go[4], s_lateName[0] ? s_lateName : "PLAYER", 11);
    Bmemcpy(&go[15], roster, sizeof(roster));
    Bmemcpy(&go[15 + kMaxPlayers], gone, sizeof(gone));

    // Reliable ordering guarantees the joiner processes every preceding EXSN
    // chunk before this GO command. Existing peers are frozen, so they cannot
    // simulate a changed roster before they receive the same command.
    ExNet_SendDirect(s_latePendingSlot, go, sizeof(go));
    ExNet_Broadcast(go, sizeof(go));
    s_lateGoSlot = s_latePendingSlot;
    s_lateColor = clamp(s_lateColor, 0, 7);
    s_lateGoRejoin = s_latePendingRejoin;
    Bmemcpy(s_lateGoSlots, roster, sizeof(roster));
    Bmemcpy(s_lateGoGone, gone, sizeof(gone));
    s_lateGoReady = 1;

    int const player = ExNet_ApplyLateRoster();
    if (player < 0 || !ExNet_FinishLatePromotion(player))
    {
        ExNet_AbortLateJoin("could not promote the deterministic roster");
        return 0;
    }
    s_latePendingSlot = -1;
    return 1;
}

static void ExNet_HostCheckLateJoin(void)
{
    if (!s_lateJoinEnabled || !ExNet_IsServer() || !s_enabled || numplayers < 2)
        return;

    if (s_latePendingSlot < 0)
    {
        for (int slot = 0; slot < numplayers; slot++)
        {
            if (slot == myconnectindex || !mmulti_slotconnected(slot))
                continue;
            int const rejoin = ExNet_SlotActive(slot) && s_gone[slot] == 2;
            if (ExNet_SlotActive(slot) && !rejoin)
                continue;
            s_latePendingSlot = slot;
            s_latePendingRejoin = rejoin;
            s_lateAssetsReady = 0;
            s_lateAssetsSent = 0;
            s_lateSnapshotReady = 0;
            s_lateStartMs = s_lateLastAnnounceMs = timerGetTicks();
            mmulti_setextraslot(slot);
            uint8_t announce[1] = { EXNET_PT_LATE_ANNOUNCE };
            ExNet_SendDirect(slot, announce, sizeof(announce));
            LOG_F(INFO, "Exhumed net: staged %s in transport slot %d",
                  rejoin ? "rejoiner" : "late joiner", slot);
            return;
        }
        return;
    }

    uint32_t const now = timerGetTicks();
    if (now - s_lateStartMs > 120000)
    {
        ExNet_AbortLateJoin("staged client timed out");
        return;
    }

    if (!s_lateAssetsReady && now - s_lateLastAnnounceMs >= 500)
    {
        s_lateLastAnnounceMs = now;
        uint8_t announce[1] = { EXNET_PT_LATE_ANNOUNCE };
        ExNet_SendDirect(s_latePendingSlot, announce, sizeof(announce));
    }

    if (s_lateAssetsReady && !s_lateAssetsSent)
    {
        ExNet_SendGameFiles(s_latePendingSlot);
        s_lateAssetsSent = 1;
        LOG_F(INFO, "Exhumed net: staged client received game files; waiting for snapshot readiness");
        return;
    }

    if (!s_lateSnapshotReady)
        return;

    if (s_lateFreezeTick == UINT32_MAX)
    {
        s_lateFreezeTick = s_netTick + EXNET_LATE_FREEZE_LEAD;
        Bmemset(s_lateFrozen, 0, sizeof(s_lateFrozen));
        uint8_t freeze[1 + 4];
        freeze[0] = EXNET_PT_LATE_FREEZE;
        VB_put32((char *)&freeze[1], s_lateFreezeTick);
        ExNet_Broadcast(freeze, sizeof(freeze));
        LOG_F(INFO, "Exhumed net: late join will freeze at tick %u", s_lateFreezeTick);
        return;
    }

    if (s_netTick != s_lateFreezeTick)
        return;

    s_lateFrozen[myconnectindex] = 1;
    for (int slot = 0; slot < numplayers; slot++)
        // A rejoiner still has an active compact roster entry, but it was
        // already LogoffPlayer()'d and is deliberately outside normal traffic
        // until GO. It cannot acknowledge this freeze from its loader.
        if (ExNet_SlotActive(slot) && !s_gone[slot] && !s_lateFrozen[slot])
            return;

    ExNet_SendLateSnapshot();
}

int ExNet_LoadLateJoinSnapshot(void)
{
    if (!s_lateJoiner)
        return 0;

    uint32_t const start = timerGetTicks();
    uint32_t lastReady = 0;
    while (!s_lateGoReady || !s_lateSnapshot || s_lateSnapshotGot < s_lateSnapshotChunks
           || s_lateSnapshotLastLen <= 0)
    {
        handleevents();
        ExNet_Pump();
        uint32_t const now = timerGetTicks();
        if (now - lastReady >= 500)
        {
            lastReady = now;
            ExNet_SendLateReady(1);
        }
        if (quitevent || now - start > 120000)
        {
            LOG_F(WARNING, "Exhumed net: timed out waiting for the late-join EXSN image");
            ExNet_ClearLateSnapshot();
            return 0;
        }
    }

    int const player = ExNet_ApplyLateRoster();
    if (player < 0)
    {
        LOG_F(ERROR, "Exhumed net: invalid late-join roster from host");
        ExNet_ClearLateSnapshot();
        return 0;
    }

    char path[BMAX_PATH];
    ExNet_LateSnapshotPath(path, sizeof(path));
    int const bytes = (s_lateSnapshotChunks - 1) * EXNET_LATE_CHUNK_SIZE + s_lateSnapshotLastLen;
    FILE *const fp = Bfopen(path, "wb");
    if (!fp || Bfwrite(s_lateSnapshot, 1, bytes, fp) != (size_t)bytes)
    {
        if (fp) Bfclose(fp);
        remove(path);
        LOG_F(ERROR, "Exhumed net: could not write the staged EXSN image");
        ExNet_ClearLateSnapshot();
        return 0;
    }
    Bfclose(fp);
    ExNet_ClearLateSnapshot();
    LoadSave::LoadNetworkSnapshot(path);
    remove(path);

    if (!ExNet_FinishLatePromotion(player))
    {
        LOG_F(ERROR, "Exhumed net: could not create the staged player from EXSN");
        return 0;
    }
    return 1;
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

    if (numplayers > kMaxPlayers)
        bail2dos("Exhumed supports at most %d network players", kMaxPlayers);

    s_enabled       = 1;
    s_rosterReady   = 0;
    s_activePlayers = 0;
    Bmemset(s_activeSlot, 0, sizeof(s_activeSlot));
    for (int slot = 0; slot < kMaxPlayers; slot++)
        s_slotToPlayer[slot] = -1;
    if (ExNet_IsServer())
    {
        uint8_t slots[kMaxPlayers] = {};
        for (int slot = 0; slot < numplayers; slot++)
            slots[slot] = mmulti_slotconnected(slot) ? 1 : 0;
        if (!ExNet_ApplyRoster(slots))
        {
            s_enabled = 0;
            LOG_F(ERROR, "Exhumed net: host could not form an initial player roster");
            return;
        }
        ExNet_SendRoster();
    }
    else if (!ExNet_WaitForRoster())
    {
        s_enabled = 0;
        LOG_F(ERROR, "Exhumed net: timed out waiting for the host's initial roster");
        return;
    }

    // A staging client has no active simulation roster yet. Give the regular
    // engine startup a harmless one-player identity solely so it can install
    // its assets and reach ExNet_LoadLateJoinSnapshot(); that function replaces
    // the entire world and applies the compact authoritative roster from GO.
    if (s_lateJoiner)
    {
        nTotalPlayers = 1;
        nNetPlayerCount = 0;
        nLocalPlayer = 0;
        wConsoleNode = 0;
        Bstrncpyz(playerNames[0], s_playerName, sizeof(playerNames[0]));
        nPlayerColor[0] = (short)s_playerColor;
        ExNet_SendLateReady(0);
        LOG_F(INFO, "Exhumed: late-join client staged in transport slot %d", myconnectindex);
        OSD_RegisterFunction("say", "say <text>: send a chat message to the other players", ExNet_OSDCmd_Say);
        return;
    }

    nTotalPlayers   = (short)s_activePlayers;
    nNetPlayerCount = s_activePlayers - 1;
    nLocalPlayer    = ExNet_PlayerForSlot(myconnectindex);
    wConsoleNode    = ExNet_PlayerForSlot(connecthead);

    for (int i = 0; i < s_activePlayers; i++)
        if (!playerNames[i][0])
            Bsnprintf(playerNames[i], sizeof(playerNames[i]), "PLAYER %d", i + 1);
    Bstrncpyz(playerNames[nLocalPlayer], s_playerName, sizeof(playerNames[0]));
    nPlayerColor[nLocalPlayer] = (short)s_playerColor;

    LOG_F(INFO, "Exhumed: multiplayer up -- %d active player(s) in %d transport slot(s), I am player %d \"%s\" (%s), transport %s",
          s_activePlayers, numplayers, nLocalPlayer, playerNames[nLocalPlayer],
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
    s_levelGen++;
    s_netTick   = 0;
    s_sendTick  = 0;
    s_syncFailed = 0;

    // Nobody has published input for the first EXNET_INPUT_DELAY ticks yet --
    // by construction, since publishing runs ahead of simulation by exactly
    // that much.  Seed them idle on every machine so the pipeline starts full
    // and identical.
    for (int p = 0; p < numplayers; p++)
        if (ExNet_SlotActive(p))
            for (uint32_t t = 0; t < EXNET_INPUT_DELAY; t++)
                s_ringHave[p][t & EXNET_RINGMASK] = 1;

    s_readyGen[myconnectindex] = s_levelGen;

    uint8_t pkt[5];
    pkt[0] = EXNET_PT_READY;
    VB_put32((char *)&pkt[1], s_levelGen);
    ExNet_Broadcast(pkt, sizeof(pkt));

    LOG_F(INFO, "Exhumed net: waiting for all %d active player(s) at the level-start barrier...", s_activePlayers);

    int const startTic = (int)totalclock;
    int lastAnnounce = startTic;
    for (;;)
    {
        handleevents();
        ExNet_Pump();
        ExNet_HostAdvertise();

        int all = 1;
        for (int p = 0; p < numplayers; p++)
            if (ExNet_SlotActive(p) && s_readyGen[p] < s_levelGen && !s_gone[p]) { all = 0; break; }
        if (all)
            break;

        if (quitevent)
            return;

        // Re-announce periodically: a peer that was still loading when our first
        // READY went out has no other way to learn we are here.
        //
        // Edge-triggered on purpose. This loop spins thousands of times per
        // second while totalclock only advances at 120 Hz, so a plain
        // "elapsed % 120 == 0" test re-broadcast on EVERY iteration that fell
        // inside the matching tic -- hundreds of duplicate READYs, which filled
        // mmulti's 512-entry output ring ("output pakbuf filled for peer 0")
        // and cost us the host's own READY. A joiner that reached the barrier
        // before the host then waited there forever.
        if ((int)totalclock - lastAnnounce >= 120)
        {
            lastAnnounce = (int)totalclock;
            ExNet_Broadcast(pkt, 1);
        }

        if ((int)totalclock - startTic > 120 * 30)
        {
            LOG_F(WARNING, "Exhumed net: level-start barrier timed out, starting anyway");
            break;
        }
    }

    LOG_F(INFO, "Exhumed net: all players ready, starting level %d", (int)levelnum);
}

// Is a new tick's worth of local input about to be published?  The game loop
// asks before gathering, because gathering CONSUMES the accumulators:
// PlayerInterruptKeys() does "nPlayerDAng += q16avel" every 120 Hz tic (and the
// keyboard's contribution only lands on every 4th call), while GetLocalInput()
// zeroes nPlayerDAng after copying it into sPlayerInput.  The game loop spins
// far faster than either rate -- measured at ~10,000 passes per published tick
// -- so gathering unconditionally threw away all but ~0.01% of the turn input
// the player had actually accumulated, and a networked player could walk and
// shoot but not turn.  Single-player has the same structure and solves it the
// same way: it only gathers on a tick boundary.
int ExNet_WantsInput(void)
{
    return s_enabled && s_sendTick < s_netTick + EXNET_INPUT_DELAY;
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
    if (!ExNet_IsServer() || s_activePlayers < 2)
        return;

    uint32_t const now = timerGetTicks();

    for (int p = 0; p < numplayers; p++)
    {
        if (!ExNet_SlotActive(p) || p == myconnectindex || s_gone[p])
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

static void ExNet_CheckLateFreeze(void)
{
    if (s_lateJoiner || s_lateFreezeTick == UINT32_MAX || s_netTick != s_lateFreezeTick
        || s_lateFrozen[myconnectindex])
        return;

    s_lateFrozen[myconnectindex] = 1;
    if (!ExNet_IsServer())
    {
        uint8_t pkt[1 + 4];
        pkt[0] = EXNET_PT_LATE_FROZEN;
        VB_put32((char *)&pkt[1], s_lateFreezeTick);
        ExNet_SendDirect(connecthead, pkt, sizeof(pkt));
    }
    LOG_F(INFO, "Exhumed net: reached late-join freeze tick %u", s_lateFreezeTick);
}

static void ExNet_CheckLatePromotion(void)
{
    if (!s_lateGoReady || s_lateJoiner)
        return;
    if (s_lateFreezeTick == UINT32_MAX || s_netTick != s_lateFreezeTick)
        return;

    int const player = ExNet_ApplyLateRoster();
    if (player < 0 || !ExNet_FinishLatePromotion(player))
    {
        LOG_F(ERROR, "Exhumed net: rejected invalid late-join promotion");
        if (ExNet_IsServer())
            ExNet_AbortLateJoin("one peer rejected roster promotion");
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
    ExNet_CheckLateFreeze();
    ExNet_HostCheckLateJoin();
    ExNet_CheckLatePromotion();

    // Publishing is driven by simulation progress, not by the wall clock: one
    // new input per tick we have run.  That makes the pipeline self-regulating
    // -- if a peer stalls us, we stop producing too, and nothing runs away.
    while (s_sendTick < s_netTick + EXNET_INPUT_DELAY)
    {
        PlayerInput const local = sPlayerInput[nLocalPlayer];

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
        if (!s_lateJoiner && s_lateFreezeTick != UINT32_MAX && t >= s_lateFreezeTick)
            break;
        int have = 1;
        for (int p = 0; p < numplayers; p++)
        {
            if (!ExNet_SlotActive(p))
                continue;
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
        if (!ExNet_SlotActive(p))
            continue;
        int const player = ExNet_PlayerForSlot(p);
        if (s_gone[p] && t >= s_goneTick[p])
        {
            if (s_gone[p] == 1 && t == s_goneTick[p])
            {
                s_gone[p] = 2;          // apply the removal exactly once, on every machine
                LogoffPlayer(player);
            }
            Bmemset(&sPlayerInput[player], 0, sizeof(sPlayerInput[player]));
            continue;
        }
        sPlayerInput[player] = s_ring[p][t & EXNET_RINGMASK];
        s_ringHave[p][t & EXNET_RINGMASK] = 0;
    }
}

void ExNet_EndTick(void)
{
    if (!s_enabled)
        return;

    uint32_t const t = s_netTick++;
    uint32_t const crc = ExNet_StateCrc();

    ExNet_NetworkSnapshotSelftest();

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
            int const nSprite = PlayerList[nLocalPlayer].nSprite;
            LOG_F(INFO, "EXNET tick %u crc %08x send %u  me(%d,%d) hp %d",
                  t, crc, s_sendTick,
                  (unsigned)nSprite < MAXSPRITES ? sprite[nSprite].x : 0,
                  (unsigned)nSprite < MAXSPRITES ? sprite[nSprite].y : 0,
                  (int)PlayerList[nLocalPlayer].nHealth);
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

    StatusMessage(360, "%s: %s", playerNames[nLocalPlayer], text);
    LOG_F(INFO, "Exhumed chat: %s: %s", playerNames[nLocalPlayer], text);
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
