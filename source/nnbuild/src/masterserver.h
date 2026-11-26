#ifndef masterserver_h_
#define masterserver_h_

#include "compat.h"

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
