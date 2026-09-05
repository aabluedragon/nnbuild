//-------------------------------------------------------------------------
//
// VibeBuild32 multiplayer for Powerslave / Exhumed.
//
// PCExhumed shipped with its network layer stubbed out (network.cpp was empty
// bodies), so this is the whole thing: the mmulti transport glue, a
// fixed-delay deterministic lockstep input exchange, desync detection, and the
// shared VibeBuild32 multiplayer feature set (masterserver advertise / lobby /
// NAT punch, host->joiner file transfer, dedicated headless server).
//
// The lockstep contract is the one in source/vibebuild32/NETCODE.md: every
// machine runs the identical sim, tick for tick, from the shared input stream.
// Exhumed's sim is one GameMove() per tick reading sPlayerInput[0..N-1], which
// makes the exchange simple: each machine publishes its own PlayerInput for a
// tick a fixed number of ticks ahead, and no machine runs tick T until it holds
// every live player's input for T.
//
//-------------------------------------------------------------------------
#ifndef __vbnet_h__
#define __vbnet_h__

#include "compat.h"

// ---- command line / setup (called from Exhumed_app_main's arg parser) -------
void ExNet_SetPunchTarget(const char *addr);   // -punchto:   client NAT punch target
void ExNet_SetMasterAddr(const char *addr);    // -masteraddr: masterserver for the punch
void ExNet_SetAutoJoin(const char *addr);      // --joinlobby: query + join first advertised
void ExNet_SetAdvertise(const char *addr);     // --advertise: register with a masterserver
int  ExNet_QueryLobby(const char *addr);       // --lobbylist: log a masterserver's listing
void ExNet_AddNetFile(const char *path);       // --netfile:  offer this file to joiners
void ExNet_SetPlayerName(const char *name);    // -name
void ExNet_SetPlayerColor(int color);          // -color
void ExNet_SetStartNowPlayers(int n);          // --nplayers
void ExNet_SetStartNow(int on);                // --startnow
void ExNet_SetWait(int on);                    // --wait
void ExNet_SetLateJoin(int on);                // --latejoin (host opt-in)

// ---- lifecycle -------------------------------------------------------------
// Runs the mmulti connect cycle from the -net arguments (or from --joinlobby).
// <firstnet> is the argv index of the first mmulti token, 0 for none.
void ExNet_Connect(int argc, char const * const *argv, int firstnet);
int  ExNet_Enabled(void);                      // nonzero once >1 player is connected
int  ExNet_IsServer(void);                     // this machine is the mmulti master
void ExNet_SendGameFiles(int slot);            // host: stream --netfile files + the usermap
void ExNet_ReceiveGameFiles(void);             // joiner: block until the host's files land
// A client which connected after the host entered a level waits here for the
// host's EXSN image. On success the current world is restored and normal
// new-level construction must be skipped.
int  ExNet_LoadLateJoinSnapshot(void);
int  ExNet_IsLateJoiner(void);
void ExNet_LevelStart(void);                   // ready barrier + tick reset, per level
int  ExNet_WantsInput(void);                   // is a new tick's local input due? (gather gate)
void ExNet_Update(void);                       // once per game-loop pass: pump + publish
int  ExNet_MoveFrames(int wallClockFrames);    // how many ticks may run right now
void ExNet_LoadTickInputs(void);               // before each GameMove(): install tick inputs
void ExNet_EndTick(void);                      // after each GameMove(): sync CRC
void ExNet_SendQuit(void);                     // announce a clean local quit
void ExNet_Shutdown(void);

// ---- chat (HUD only, never simulated) --------------------------------------
int  ExNet_ChatActive(void);   // a message is being typed: ignore gameplay input
void ExNet_ChatUpdate(void);   // per frame: open the prompt, collect keystrokes
void ExNet_ChatDraw(void);     // per drawn frame: render the prompt
void ExNet_ChatSendText(const char *text);   // send a line (HUD prompt or the "say" console command)

// ---- masterserver / lobby --------------------------------------------------
void ExNet_HostAdvertise(void);                // host heartbeat + NAT punch service
void ExNet_ClientPunchAttempt(void);           // joiner: ask the master to punch toward the host
int  ExNet_ResolveAutoJoin(void);              // --joinlobby: pick a server; fills the address below
extern char ExNet_autoJoinServer[128];

#endif
