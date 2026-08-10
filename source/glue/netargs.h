#pragma once
//
// VibeBuild32 unified multiplayer command-line flags.
//
// Every game (Duke/SW/Blood/RR) accepts the SAME networking flags with the SAME
// behavior; this header is the single definition of what each one does. Each game's
// own arg parser (they use different frameworks) just detects a token and forwards
// it here via VB_NetArg(); the action lives in one place.
//
//   -advertise <master>   host a listen server and register it with a masterserver
//   -lobbylist <master>   print the servers a masterserver is advertising
//   -joinlobby <master>   query a masterserver and join the first advertised server
//   -netfile  <path>      (host) offer this file to joiners (mod grp / user map)
//   -nplayers <N>         players to wait for before a dedicated server auto-starts (default 2)
//   -wait                 dedicated server: DON'T auto-start; hold in the lobby for players
//   -startnow             force auto-start even on a windowed -server (dedicated already does)
//   -headless|-dedicated|-console   run a windowless dedicated server (auto-starts by default)
//
// A game passes NULL for any hook/var it does not support; that flag is still
// accepted (and consumed) but does nothing, so command lines stay portable.
//
#include "compat.h"

// One help blurb, printed by every game's -help so the flags are discoverable and
// documented in exactly one place.
#define VB_NETARGS_HELP \
"\nMultiplayer (VibeBuild32):\n" \
"  -advertise <master>       host a server and register it with a masterserver\n" \
"  -lobbylist <master>       list the servers a masterserver is advertising\n" \
"  -joinlobby <master>       join the first server a masterserver is advertising\n" \
"  -netfile <path>           (host) offer a mod/map file to joiners\n" \
"  -headless / -dedicated    run a windowless dedicated server (auto-starts by default)\n" \
"  -nplayers <N>             players to wait for before a dedicated server auto-starts (default 2)\n" \
"  -wait                     dedicated: hold in the lobby instead of auto-starting\n"

struct vb_netarg_ctx
{
    int *networkMode;                 // set to serverMode for host flags, clientMode for -joinlobby
    int  serverMode, clientMode;
    int *noSetup;                     // set to 1 to skip first-run setup (NULL if n/a)
    int *noLogo;                      // set to 1 to skip the logo (NULL if n/a)
    int *startNow;                    // set to 1 for -startnow (NULL if the game has no auto-start)
    int *startNowPlayers;             // set for -nplayers (NULL if unsupported)
    int *wait;                        // set to 1 for -wait (NULL if the game has no auto-start)
    int  maxPlayers;                  // upper clamp for -nplayers (e.g. MAXPLAYERS)
    void (*setAdvertise)(const char *);
    int  (*queryLobby)(const char *);
    int  (*joinLobby)(const char *);
    void (*addNetFile)(const char *);
};

// <flag> is the token WITHOUT its leading '-'. <next> is the following argv token
// (or NULL if there is none). Returns the number of argv tokens consumed:
//   0 = <flag> is not a VibeBuild32 net flag (the caller handles it),
//   1 = a value-less flag was handled,
//   2 = a flag + its value were handled.
static inline int VB_NetArg(const vb_netarg_ctx *cx, const char *flag, const char *next)
{
    if (!Bstrcasecmp(flag, "advertise"))
    {
        if (!next) return 1;
        if (cx->networkMode) *cx->networkMode = cx->serverMode;
        if (cx->noSetup) *cx->noSetup = 1;
        if (cx->noLogo)  *cx->noLogo  = 1;
        if (cx->setAdvertise) cx->setAdvertise(next);
        return 2;
    }
    if (!Bstrcasecmp(flag, "lobbylist"))
    {
        if (!next) return 1;
        if (cx->queryLobby) cx->queryLobby(next);
        return 2;
    }
    if (!Bstrcasecmp(flag, "joinlobby"))
    {
        if (!next) return 1;
        if (cx->noSetup) *cx->noSetup = 1;
        if (cx->noLogo)  *cx->noLogo  = 1;
        if (cx->joinLobby) cx->joinLobby(next);
        return 2;
    }
    if (!Bstrcasecmp(flag, "netfile"))
    {
        if (!next) return 1;
        if (cx->addNetFile) cx->addNetFile(next);
        return 2;
    }
    if (!Bstrcasecmp(flag, "startnow"))
    {
        if (cx->startNow) *cx->startNow = 1;
        return 1;
    }
    if (!Bstrcasecmp(flag, "wait"))   // hold a dedicated server in the lobby instead of auto-starting
    {
        if (cx->wait) *cx->wait = 1;
        return 1;
    }
    if (!Bstrcasecmp(flag, "nplayers"))
    {
        if (!next) return 1;
        if (cx->startNowPlayers)
        {
            int v = atoi(next);
            int const hi = cx->maxPlayers > 2 ? cx->maxPlayers : 2;
            *cx->startNowPlayers = v < 2 ? 2 : (v > hi ? hi : v);
        }
        return 2;
    }
    if (!Bstrcasecmp(flag, "headless") || !Bstrcasecmp(flag, "dedicated") || !Bstrcasecmp(flag, "console"))
    {
        // the engine's early argv scan already set g_forceHeadless; here we just make
        // it a dedicated server and skip the interactive setup/logo.
        if (cx->networkMode) *cx->networkMode = cx->serverMode;
        if (cx->noSetup) *cx->noSetup = 1;
        if (cx->noLogo)  *cx->noLogo  = 1;
        return 1;
    }
    return 0;
}
