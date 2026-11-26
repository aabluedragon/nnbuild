#pragma once
//
// NNBuild unified multiplayer command-line flags.
//
// Every game (Duke/SW/Blood/RR) accepts the SAME networking flags with the SAME
// behavior; this header is the single definition of what each one does. Each game's
// own arg parser (they use different frameworks) just detects a token and forwards
// it here via VB_NetArg(); the action lives in one place.
//
//   --advertise/-ad <master>  host a listen server and register it with a masterserver
//   --lobbylist/-ll <master>  print the servers a masterserver is advertising
//   --joinlobby/-jl <master>  query a masterserver and join the first advertised server
//   --netfile/-nf <path>      (host) offer this file to joiners (mod grp / user map)
//   --nplayers/-np <N>        players to wait for before a dedicated server auto-starts (default 2)
//   --wait/-w                 dedicated server: DON'T auto-start; hold in the lobby for players
//   --startnow/-sn            force auto-start even on a windowed -server (dedicated already does)
//   --headless/-hl or --dedicated/-dd   run a windowless dedicated server (auto-starts by default)
//
// A game passes NULL for any hook/var it does not support; that flag is still
// accepted (and consumed) but does nothing, so command lines stay portable.
//
#include "compat.h"

// One help blurb, printed by every game's -help so the flags are discoverable and
// documented in exactly one place.
#define VB_NETARGS_HELP \
"\nMultiplayer (NNBuild):\n" \
"  --advertise / -ad <master>    host a server and register it with a masterserver\n" \
"  --lobbylist / -ll <master>    list the servers a masterserver is advertising\n" \
"  --joinlobby / -jl <master>    join the first server a masterserver is advertising\n" \
"  --netfile / -nf <path>        (host) offer a mod/map file to joiners\n" \
"  --headless / -hl              run a windowless dedicated server (auto-starts by default)\n" \
"  --dedicated / -dd             run a windowless dedicated server (auto-starts by default)\n" \
"  --nplayers / -np <N>          players to wait for before a dedicated server auto-starts (default 2)\n" \
"  --startnow / -sn              auto-start with present players\n" \
"  --wait / -w                   dedicated: hold in the lobby instead of auto-starting\n" \
"  (options also accept --flag=value; old -long-name spellings remain compatible)\n"

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

// <arg> is an argv token that may carry its leading dashes and a "=value" suffix
// (canonical forms: --long-name, --long-name=value, -short-name; old -long-name
// spellings remain accepted). <next> is the
// following argv token (or NULL). Returns argv tokens consumed:
//   0 = <arg> is not an NNBuild net flag (the caller handles it),
//   1 = handled with no separate value (value-less flag, or an inline --flag=value),
//   2 = handled and the separate <next> token was its value.
static inline int VB_NetArg(const vb_netarg_ctx *cx, const char *arg, const char *next)
{
    while (*arg == '-') arg++;                         // accept -flag and --flag alike

    char flag[24];                                     // the name, split off any "=value"
    const char *inlineVal = strchr(arg, '=');
    if (inlineVal)
    {
        int n = (int)(inlineVal - arg);
        if (n > (int)sizeof(flag) - 1) n = sizeof(flag) - 1;
        Bmemcpy(flag, arg, n); flag[n] = 0;
        inlineVal++;                                   // point past '='
    }
    else Bstrncpyz(flag, arg, sizeof(flag));

    // A value comes from --flag=value (inline) or the next token; the consumed count
    // reflects which so the caller advances argv correctly.
    const char *const val = inlineVal ? inlineVal : next;
    int const valConsumed = inlineVal ? 1 : 2;

    if (!Bstrcasecmp(flag, "advertise") || !Bstrcasecmp(flag, "ad"))
    {
        if (!val) return 1;
        if (cx->networkMode) *cx->networkMode = cx->serverMode;
        if (cx->noSetup) *cx->noSetup = 1;
        if (cx->noLogo)  *cx->noLogo  = 1;
        if (cx->setAdvertise) cx->setAdvertise(val);
        return valConsumed;
    }
    if (!Bstrcasecmp(flag, "lobbylist") || !Bstrcasecmp(flag, "ll"))
    {
        if (!val) return 1;
        if (cx->queryLobby) cx->queryLobby(val);
        return valConsumed;
    }
    if (!Bstrcasecmp(flag, "joinlobby") || !Bstrcasecmp(flag, "jl"))
    {
        if (!val) return 1;
        if (cx->noSetup) *cx->noSetup = 1;
        if (cx->noLogo)  *cx->noLogo  = 1;
        if (cx->joinLobby) cx->joinLobby(val);
        return valConsumed;
    }
    if (!Bstrcasecmp(flag, "netfile") || !Bstrcasecmp(flag, "nf"))
    {
        if (!val) return 1;
        if (cx->addNetFile) cx->addNetFile(val);
        return valConsumed;
    }
    if (!Bstrcasecmp(flag, "nplayers") || !Bstrcasecmp(flag, "np"))
    {
        if (!val) return 1;
        if (cx->startNowPlayers)
        {
            int v = atoi(val);
            int const hi = cx->maxPlayers > 2 ? cx->maxPlayers : 2;
            *cx->startNowPlayers = v < 2 ? 2 : (v > hi ? hi : v);
        }
        return valConsumed;
    }
    if (!Bstrcasecmp(flag, "startnow") || !Bstrcasecmp(flag, "sn"))
    {
        if (cx->startNow) *cx->startNow = 1;
        return 1;
    }
    if (!Bstrcasecmp(flag, "wait") || !Bstrcasecmp(flag, "w"))   // hold a dedicated server in the lobby instead of auto-starting
    {
        if (cx->wait) *cx->wait = 1;
        return 1;
    }
    if (!Bstrcasecmp(flag, "headless") || !Bstrcasecmp(flag, "hl") ||
        !Bstrcasecmp(flag, "dedicated") || !Bstrcasecmp(flag, "dd") || !Bstrcasecmp(flag, "console"))
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
