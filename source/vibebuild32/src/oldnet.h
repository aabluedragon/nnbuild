#ifndef OLDNET_H
#define OLDNET_H

#include "build.h"
#include "player.h"
#include "sync.h"

#ifdef OLDNET_CPP_
#define OLDNET_EXTERN
#else
#define OLDNET_EXTERN extern
#endif

#define INPUTFIFO_CURTICK (movefifoplc & (MOVEFIFOSIZ - 1))
#define INPUTFIFO_LASTTICK ((movefifoplc - 1) & (MOVEFIFOSIZ - 1))
#define INPUTFIFO_PREDICTTICK (predictfifoplc & (MOVEFIFOSIZ - 1))

struct votedata_t
{
    int32_t dmflags;
    int8_t starter = -1, level = -1, episode = -1;
    int8_t yes_votes;
    int8_t gametype, skill;
};
OLDNET_EXTERN votedata_t vote;

extern int quittimer;
extern int lastpackettime;
extern int mymaxlag, otherminlag, bufferjitter;

extern int movefifosendplc;
extern int movefifoplc;

OLDNET_EXTERN input_t netInput;

OLDNET_EXTERN bool oldnet_gotinitialsettings; // True if we got PACKET_TYPE_INIT_SETTINGS from the host.

//OLDNET_EXTERN PredictBackup_t predictBackup[MOVEFIFOSIZ];

enum DukePacket_t
{
    PACKET_TYPE_MASTER_TO_SLAVE,
    PACKET_TYPE_SLAVE_TO_MASTER,
    PACKET_TYPE_BROADCAST,
    SERVER_GENERATED_BROADCAST,
    PACKET_TYPE_VERSION,

    /* don't change anything above this line */

    PACKET_TYPE_MESSAGE,

    PACKET_TYPE_NEW_GAME,
    PACKET_TYPE_RTS,
    PACKET_TYPE_MENU_LEVEL_QUIT,
    PACKET_TYPE_WEAPON_CHOICE,
    PACKET_TYPE_PLAYER_OPTIONS,
    PACKET_TYPE_PLAYER_NAME,
    PACKET_TYPE_INIT_SETTINGS,

    PACKET_TYPE_USER_MAP,

    PACKET_TYPE_MAP_VOTE,
    PACKET_TYPE_MAP_VOTE_INITIATE,
    PACKET_TYPE_MAP_VOTE_CANCEL,

    PACKET_TYPE_LOAD_GAME,
    PACKET_TYPE_NULL_PACKET,
    PACKET_TYPE_PLAYER_READY,
    PACKET_TYPE_FRAGLIMIT_CHANGED,
    PACKET_TYPE_EOL,
    PACKET_TYPE_PING,

    PACKET_TYPE_LATEJOIN_ANNOUNCE, // Master -> late joiner: game in progress, snapshot follows.
    PACKET_TYPE_SNAPSHOT_CHUNK,    // Master -> late joiner: piece of the state snapshot.
    PACKET_TYPE_LATEJOIN_GO,       // Master -> everyone: integrate player [1] with roster mask [2..3].

    PACKET_TYPE_LAG_NOTIFY,        // Master -> everyone: laggards mask + seconds left + votes mask.
    PACKET_TYPE_LAG_CLEAR,         // Master -> everyone: stall resolved, resume.
    PACKET_TYPE_LAG_VOTE,          // Slave -> master: I vote to drop the current laggard(s).
    PACKET_TYPE_DROP_PLAYER,       // Master -> everyone: remove player [1], roster mask [2..3].

    PACKET_TYPE_FILE_HEADER,       // Host -> client: begin transfer (name, size, crc).
    PACKET_TYPE_FILE_CHUNK,        // Host -> client: a piece of the file being sent.

    PACKET_TYPE_RESTORE_SCORE,     // Master -> everyone: set slot's score (reconnect by GUID).

    PACKET_TYPE_LATEJOIN_READY,    // Late joiner -> master: I'm in my main loop, start streaming.

    PACKET_TYPE_PLAYER_LEAVING,    // Client -> master: dropping out now (desync reconnect); free my slot.

    PACKET_TYPE_HOST_EVENT,        // Master -> everyone: apply a world change (spawn / monster control) at a scheduled tick.

    PACKET_END, // Should remain last in list.
};

enum NetMode_t
{
    NETMODE_MASTERSLAVE,
    NETMODE_P2P,
    NETMODE_OFFLINE = 255
};

void faketimerhandler(void);
void Net_HandleInput(void);
void Net_GetPackets(void);
void Net_ParsePackets(void);
void Net_SendQuit(void);
void Net_SendWeaponChoice(void);
void Net_SendVersion(void);
void Net_SendLateJoinReady(void);
void Net_SendPlayerOptions(void);
void Net_SendFragLimit(void);
void Net_SendPlayerName(void);
void Net_SendUserMapName(void);
void Net_SendInitialSettings(void);
void Net_SendNewGame(uint32_t flags);
void Net_EndOfLevel(bool secret);
void Net_EnterMessage(void);

void Net_InitiateVote();
void Net_CancelVote();

// Late joining (see Net_MasterCheckLateJoin in oldnet.cpp).
#define LATEJOIN_SAVE_SPOT 100
OLDNET_EXTERN int32_t g_netAsyncMode;   // Host: server-authoritative, never blocks on slow clients.
OLDNET_EXTERN int32_t g_netZeroPing;    // Client-local shot prediction (make firing feel instant).
OLDNET_EXTERN int32_t g_netLagActive;   // Everyone: a lockstep lag dialog is open right now.
OLDNET_EXTERN uint16_t g_netLagMask;    // Everyone: which slots are lagging.
OLDNET_EXTERN int32_t g_netLagSecsLeft; // Everyone: countdown shown in the dialog.
OLDNET_EXTERN uint16_t g_netLagVoteMask;// Everyone: which slots have been voted to drop.
OLDNET_EXTERN int32_t g_netLateJoin;    // Host: started with -latejoin, open slots may join mid-game.
OLDNET_EXTERN int32_t g_netStartNow;    // Host: begin immediately with whoever is present; rest join anytime.
OLDNET_EXTERN int32_t g_netLateJoiner;  // This client is joining a game already in progress.
OLDNET_EXTERN char g_lateJoinStatus[64]; // Joiner: current phase text for the loading screen.
OLDNET_EXTERN int32_t g_lateJoinPercent; // Joiner: 0..100 progress of the current phase.
OLDNET_EXTERN int32_t g_netLateJoinerReady; // Joiner reached the main loop; safe to load the snapshot.
OLDNET_EXTERN int32_t g_lateJoinFreeze; // Master: input flow paused for a state transfer.
OLDNET_EXTERN int32_t g_netReconnectPending; // Client: desynced, reconnect from a fresh process.
OLDNET_EXTERN char g_netDesyncReason[64];    // Client: which sync stream first diverged.
void Net_StoreProcessArgs(int32_t argc, char const * const *argv);
void Net_ClientReconnect(void);
void Net_LaunchAddress(const char *hostport);   // relaunch as a client joining hostport
void Net_LaunchHost(int32_t advertise);         // relaunch as a listen host (optionally advertised)
void Net_LaunchMenu(const char *errmsg);        // relaunch back to the menu (optional join-error banner)

// Masterserver the in-game lobby queries (settable via cl_masterserver).
extern char g_masterServerAddr[128];
// Client's join target ("host:port") for NAT-punch; empty if not a lobby join.
extern char g_punchTargetAddr[128];
// Reason a previous join failed, shown once in the JOIN GAME menu; empty if none.
extern char g_joinErrorMsg[128];
// cl_preferipv6: lobby prefers a server's IPv6 address when it has one.
extern int32_t g_netPreferIPv6;
uint16_t Net_ConnectedMask(void);
void Net_ApplyConnectedMask(uint16_t mask);
void Net_RebuildConnectList(void);
void Net_MasterCheckLateJoin(void);
void Net_PeerCheckLateJoin(void);
void Net_MasterCheckDrop(void);
void Net_PeerCheckDrop(void);
void Net_MasterCheckTimeouts(void);
void Net_HostAdvertise(void);
void Net_MasterCheckRestore(void);

void Net_LagUpdate(void);
void Net_LagVote(void);
void Net_DisplayLagDialog(void);

// Host administration (kick/ban/banlist).
#define MAXBANS 256
OLDNET_EXTERN int32_t g_netServerConsole; // Read admin commands from stdin (dedicated/headless host).
OLDNET_EXTERN int32_t g_netServerDedicated; // Authoritative (broadcast): slot 0 is a non-playing spectator.
void Net_AdminInit(void);
void Net_PollStdin(void);
void Net_DrawServerScreen(void);
int32_t Net_KickPlayer(const char *ref);
int32_t Net_BanPlayer(const char *ref);
int32_t Net_Unban(const char *ref);
void Net_ListBans(void);
void Net_ListPlayers(void);

// Map rotation (host-authoritative).
OLDNET_EXTERN int32_t g_mapRotationMode; // 0 off, 1 sequential, 2 random
int32_t Net_RotateNextMap(void);
void Net_ListMapRotation(void);
void Net_CheckMapRotation(void);

// Score persistence across reconnects (host, keyed by client GUID). Cleared at
// the start of a new match so a rejoin after the match ended starts fresh.
void Net_ClearScoreArchive(void);

// Host -> client file transfer.
#define MAXNETMODFILES 8
OLDNET_EXTERN char g_netModFiles[MAXNETMODFILES][BMAX_PATH]; // Mods (-g grp/zip) the host loaded.
OLDNET_EXTERN int32_t g_netModFileCount;
void Net_SendFile(int32_t slot, const char *name, int32_t fileIndex = 1, int32_t fileTotal = 1);
void Net_SendGameFiles(int32_t slot); // Push the current map + host mods (slot<0 = everyone).

void Net_ClearFIFO(void);
void Net_CheckPlayerQuit(int i);
void Net_Disconnect(bool showScores);
void Net_WaitForPlayers();

// Host-authoritative, deterministic world changes (spawn actors / monster
// control) driven from the server console. The host schedules the change for a
// near-future sim tick and broadcasts it; every machine applies it when its
// movefifoplc reaches that tick, so the mutation is bit-identical everywhere and
// can't desync. Net_RunHostEvents() is pumped once per simulated tick.
void Net_RunHostEvents(void);
void Net_HostMonsters(int32_t on, int32_t skill);       // skill < 0 = keep current
int32_t Net_HostSpawn(int32_t picnum, int32_t x, int32_t y, int32_t z, int32_t sectnum, int32_t ang);
int32_t Net_ResolvePlayer(const char *ref);             // name/slot -> connected slot, or -1

#endif
