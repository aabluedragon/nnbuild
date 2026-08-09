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
    char addr[128];     // "a.b.c.d:port" or "[ipv6]:port" - directly usable to connect
    char name[32];
    char map[24];
    int32_t players, maxplayers;
};

// Blocking query with a timeout (ms). Fills up to maxout entries; returns the
// count, or -1 on error.
int32_t Masterserver_QueryList(const char *masterAddr, struct msserver_t *out, int32_t maxout, int32_t timeoutMs);

// ----- NAT hole-punch assist (STUN) ------------------------------------------
// Client: build a punch request to send from the GAME socket (so the master
// learns our real game endpoint) - resolves the master to an IPv4 ip/port
// (network order) and fills buf. Returns the packet length, or 0 on failure.
int32_t Masterserver_MakePunchReq(const char *masterAddr, const char *targetAddr,
                                  unsigned int *ip, unsigned short *port, void *buf, int32_t cap);

// Host: register a callback that opens a NAT hole to (ip,port) (network order),
// then pump the advertise socket each frame so incoming punch requests fire it.
void Masterserver_SetPunchFn(void (*fn)(unsigned int ip, unsigned short port));
void Masterserver_HostPump(void);

#ifdef __cplusplus
}
#endif

#endif
