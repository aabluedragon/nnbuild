#ifndef masterserver_h_
#define masterserver_h_

#include "compat.h"
#include "../../glue/nnbdefaults.h"   // NNB_DEFAULT_MASTERSERVER, the public lobby

#ifdef __cplusplus
extern "C" {
#endif

#define MASTERSERVER_DEFAULT_PORT 23519

// ---------------------------------------------------------------------------
// Standalone public masterserver: a lobby directory + dual-stack (IPv4/IPv6)
// STUN / NAT hole-punch rendezvous. Game servers advertise themselves to it,
// clients query it for the list and use it to reach servers behind NAT.
// Started with `-masterserver [port]`; blocks until killed.
// ---------------------------------------------------------------------------
void Masterserver_Run(int32_t port);

// ----- Game-server side: advertise this server to a masterserver -------------
// addr is "host[:port]" - IPv4, [IPv6], or a hostname; missing port defaults to
// MASTERSERVER_DEFAULT_PORT. Configure once, then pulse periodically from the
// host loop; the masterserver keys the entry off the packet's source address.
void Masterserver_SetAdvertise(const char *addr);
int32_t Masterserver_IsAdvertising(void);
void Masterserver_AdvertisePulse(const char *name, int32_t gameport, int32_t players, int32_t maxplayers, const char *map);
void Masterserver_AdvertiseStop(void);   // send a one-shot "going away"

// ----- Client side: query a masterserver for the lobby list ------------------
struct msserver_t
{
    char addr[128];     // IPv4 "a.b.c.d:port" - the mandatory address (may be "")
    char addr6[128];    // IPv6 "[ipv6]:port" - optional, "" if the server has none
    char name[32];
    char map[24];
    int32_t players, maxplayers;
};

// Blocking query with a timeout (ms). Fills up to maxout entries; returns the
// count, or -1 on error.
int32_t Masterserver_QueryList(const char *masterAddr, struct msserver_t *out, int32_t maxout, int32_t timeoutMs);

// The same query without blocking the caller: looking up the lobby's name and
// waiting for its answer happen on a worker thread, so a menu keeps drawing.
// Begin starts a query and drops any earlier one that is still running.
// Poll returns 0 while the answer is pending. Once it returns 1, *count is the
// number of servers copied to out, or -1 when the lobby could not be reached.
void    Masterserver_QueryListBegin(const char *masterAddr);
int32_t Masterserver_QueryListPoll(struct msserver_t *out, int32_t maxout, int32_t *count);

// ----- Client side: reach a listed server ------------------------------------
// A listed server may have an IPv4 address, an IPv6 address, or both. Before the
// game commits to one, it asks the server over each of them "are you there?"
// (mmulti answers without using a player slot) and asks the lobby to open a path
// through the server's router. Nothing here blocks: call JoinPoll once a frame.
//
// IPv6 is preferred when preferIPv6 is set: an answer over IPv6 wins at once, and
// after an answer over IPv4 the IPv6 attempt gets MSJOIN_V6_GRACE_MS more to
// answer. Without the preference, the first answer wins.
#define MSJOIN_V6_GRACE_MS 2500
#define MSJOIN_TIMEOUT_MS  25000

enum
{
    MSJOIN_IDLE = 0,
    MSJOIN_TRYING,      // no decision yet
    MSJOIN_CONNECTED,   // msjoin_t::chosen holds the address to join
    MSJOIN_FAILED,      // no answer in MSJOIN_TIMEOUT_MS
    MSJOIN_FULL,        // the server answered, and has no free player slot
};

struct msjoin_t
{
    int32_t state;              // MSJOIN_*
    int32_t have4, have6;       // an address of this kind is being tried
    int32_t ok4, ok6;           // the server answered over it
    int32_t ms4, ms6;           // how long that answer took, in ms
    int32_t elapsedMs;          // since JoinBegin
    int32_t graceLeftMs;        // > 0 while IPv4 answered and IPv6 may still win
    int32_t lobbyRequests;      // times the lobby was asked to open a path
    char    addr4[128], addr6[128];
    char    chosen[128];
};

// localPort is the port the game will use for the match, so that a path opened
// during this attempt stays open for it. server->addr / addr6 must be numeric
// ("a.b.c.d:port", "[v6]:port"). Returns 0 when neither address can be tried.
int32_t Masterserver_JoinBegin(const struct msserver_t *server, const char *masterAddr,
                               int32_t localPort, int32_t preferIPv6);
int32_t Masterserver_JoinPoll(struct msjoin_t *status);   // returns status->state
void    Masterserver_JoinCancel(void);

// ----- NAT hole-punch assist (STUN, IPv4 + IPv6) -----------------------------
// Client: build a punch request to send from the GAME socket (so the master
// learns our real game endpoint). The master is contacted over the same family
// as targetAddr, so v4 targets punch over v4 and v6 targets over v6. Fills
// outMasterEp with the masterserver endpoint string to send to ("a.b.c.d:port"
// or "[ipv6]:port") and buf with the packet. Returns the packet length, or 0.
int32_t Masterserver_MakePunchReq(const char *masterAddr, const char *targetAddr,
                                  char *outMasterEp, int32_t outcap, void *buf, int32_t cap);

// Host: register a callback that opens a NAT hole toward an address string
// ("a.b.c.d:port" or "[ipv6]:port"), then pump the advertise sockets each frame
// so incoming punch requests fire it.
void Masterserver_SetPunchFn(void (*fn)(const char *addr));
void Masterserver_HostPump(void);

#ifdef __cplusplus
}
#endif

#endif
