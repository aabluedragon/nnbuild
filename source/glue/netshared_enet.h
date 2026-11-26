#pragma once
//
// NNBuild shared netcode glue that needs the engine's ENet types. Included only
// by the enet-transport games (Blood, Redneck Rampage) - NOT by Shadow Warrior, which
// uses mmulti. Include AFTER enet.h and netshared.h (both are pulled in by those games'
// engine/netcode headers before this).
//
#include "netshared.h"

// Client NAT hole-punch: ask <masterAddr> to punch a hole toward <target> ("ip:port"),
// then fire the returned request out our own game <socket> so the master learns our real
// endpoint. enet_address_set_host (getaddrinfo) resolves IPv4 to IPv4-mapped; set_host_ip
// only parses IPv6 literals. <tag> names the game for the log line.
static inline void VB_SendPunch(const char *masterAddr, const char *target, ENetSocket socket, const char *tag)
{
    if (!Masterserver_MakePunchReq) return;
    char msEp[128]; unsigned char pbuf[160];
    int32_t const plen = Masterserver_MakePunchReq(masterAddr, target, msEp, sizeof(msEp), pbuf, sizeof(pbuf));
    if (plen <= 0) return;
    char host[64]; Bstrncpyz(host, msEp, sizeof(host));
    char *colon = strrchr(host, ':'); int port = 0;
    if (colon) { *colon = 0; port = atoi(colon + 1); }
    ENetAddress addr; memset(&addr, 0, sizeof(addr));
    if (enet_address_set_host(&addr, host) == 0)
    {
        addr.port = (enet_uint16)port;
        ENetBuffer b; b.data = pbuf; b.dataLength = plen;
        enet_socket_send(socket, &addr, &b, 1);
        LOG_F(INFO, "%s: requested NAT punch to %s via masterserver %s", tag, target, msEp);
    }
}
