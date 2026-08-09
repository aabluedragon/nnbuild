// VibeBuild32 masterserver: a public lobby directory and dual-stack (IPv4/IPv6)
// STUN / NAT hole-punch rendezvous. See masterserver.h.

#include "masterserver.h"
#include "compat.h"
#include "baselayer.h"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <winsock2.h>
# include <ws2tcpip.h>
# define ms_close closesocket
# define MS_SOCKET SOCKET
#else
# include <unistd.h>
# include <errno.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <sys/socket.h>
# include <netdb.h>
# include <sys/time.h>
# include <fcntl.h>
# define ms_close close
# define MS_SOCKET int
# define INVALID_SOCKET (-1)
#endif

// Wire protocol. Every packet starts with this 4-byte tag so stray UDP noise is
// ignored, then a 1-byte message type.
static const char MS_MAGIC[4] = { 'V', 'B', 'M', 'S' };
enum
{
    MS_ADVERTISE      = 0xB0,   // server  -> master : register / heartbeat
    MS_ADVERTISE_STOP = 0xB1,   // server  -> master : going away
    MS_LIST_REQ       = 0xB2,   // client  -> master : send me the list
    MS_LIST_RESP      = 0xB3,   // master  -> client : (chunk of) the list
    MS_STUN_REQ       = 0xB4,   // anyone  -> master : what is my public address?
    MS_STUN_RESP      = 0xB5,   // master  -> sender : your public address is ...
};

#define MS_ENTRY_TTL_MS   30000   // drop a server not heard from in this long
#define MS_MAX_ENTRIES    256
#define MS_PKT            1400    // stay under the common path MTU

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

static uint32_t ms_nowms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
#endif
}

static void ms_netinit(void)
{
#ifdef _WIN32
    static int done = 0;
    if (!done) { WSADATA w; WSAStartup(0x0202, &w); done = 1; }
#endif
}

// Open a UDP socket of the given family (AF_INET or AF_INET6). IPv6 sockets are
// bound V6ONLY so IPv4 and IPv6 are handled by separate sockets - macOS won't
// send to IPv4-mapped addresses from a dual-stack socket, so we never rely on
// that. Bind to `port` if >= 0, else an ephemeral port.
static MS_SOCKET ms_opensocket(int family, int32_t port)
{
    ms_netinit();
    MS_SOCKET s = socket(family, SOCK_DGRAM, 0);
    if (s == (MS_SOCKET)INVALID_SOCKET)
        return (MS_SOCKET)INVALID_SOCKET;

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

    int rc;
    if (family == AF_INET6)
    {
        int v6only = 1;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));
        struct sockaddr_in6 sa;
        Bmemset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_addr = in6addr_any;
        sa.sin6_port = htons((uint16_t)(port < 0 ? 0 : port));
        rc = bind(s, (struct sockaddr *)&sa, sizeof(sa));
    }
    else
    {
        struct sockaddr_in sa;
        Bmemset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = INADDR_ANY;
        sa.sin_port = htons((uint16_t)(port < 0 ? 0 : port));
        rc = bind(s, (struct sockaddr *)&sa, sizeof(sa));
    }
    if (rc != 0)
    {
        ms_close(s);
        return (MS_SOCKET)INVALID_SOCKET;
    }
    return s;
}

static void ms_setnonblock(MS_SOCKET s)
{
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// Is a sockaddr_in6 actually an IPv4-mapped address (::ffff:a.b.c.d)?
static int ms_is_v4mapped(const struct sockaddr_in6 *a)
{
    return IN6_IS_ADDR_V4MAPPED(&a->sin6_addr);
}

// Format an address as a connect string usable by the game: "a.b.c.d:port" for
// IPv4 (and IPv4-mapped), "[v6]:port" for real IPv6. `overridePort`, if > 0,
// replaces the address's port (used to advertise the game port rather than the
// ephemeral source port).
static void ms_addr_to_str(const struct sockaddr_storage *ss, int32_t overridePort, char *out, int32_t outlen)
{
    char host[128];
    host[0] = 0;
    int port = 0;
    int isV6 = 0;

    if (ss->ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)ss;
        port = ntohs(a->sin6_port);
        if (ms_is_v4mapped(a))
        {
            // pull the trailing 4 bytes out as a v4 dotted quad
            const uint8_t *b = a->sin6_addr.s6_addr + 12;
            Bsnprintf(host, sizeof(host), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        }
        else
        {
            isV6 = 1;
            inet_ntop(AF_INET6, (void *)&a->sin6_addr, host, sizeof(host));
        }
    }
    else if (ss->ss_family == AF_INET)
    {
        const struct sockaddr_in *a = (const struct sockaddr_in *)ss;
        port = ntohs(a->sin_port);
        inet_ntop(AF_INET, (void *)&a->sin_addr, host, sizeof(host));
    }

    if (overridePort > 0)
        port = overridePort;

    if (isV6)
        Bsnprintf(out, outlen, "[%s]:%d", host, port);
    else
        Bsnprintf(out, outlen, "%s:%d", host, port);
}

// Resolve "host[:port]" (IPv4, [IPv6], or hostname) to a sockaddr usable from a
// dual-stack IPv6 socket (IPv4 results come back as ::ffff:a.b.c.d). Returns 1
// on success. defPort is used when the string carries no port.
static int ms_resolve(const char *addr, int32_t defPort, struct sockaddr_storage *out)
{
    char host[256];
    Bstrncpyz(host, addr, sizeof(host));
    char portbuf[16];
    Bsnprintf(portbuf, sizeof(portbuf), "%d", defPort);
    const char *port = portbuf;

    // [ipv6]:port  |  ipv6-with-colons  |  host:port  |  host
    if (host[0] == '[')
    {
        char *close = Bstrchr(host, ']');
        if (close)
        {
            *close = 0;
            memmove(host, host + 1, Bstrlen(host + 1) + 1);
            if (close[1] == ':') port = close + 2;
        }
    }
    else
    {
        char *colon = Bstrchr(host, ':');
        // a lone colon means host:port; multiple colons means a bare IPv6 literal
        if (colon && !Bstrchr(colon + 1, ':'))
        {
            *colon = 0;
            port = colon + 1;
        }
    }

    struct addrinfo hints, *res = NULL;
    Bmemset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // native IPv4 or IPv6 (no IPv4-mapped; macOS won't send to those)
    hints.ai_socktype = SOCK_DGRAM;
    ms_netinit();
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return 0;
    Bmemcpy(out, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return 1;
}

// ---------------------------------------------------------------------------
// masterserver: the standalone directory / STUN process
// ---------------------------------------------------------------------------

struct ms_entry_t
{
    struct sockaddr_storage src;   // where the advertise came from (public endpoint)
    uint16_t gameport;             // the port the game actually listens on
    uint8_t players, maxplayers;
    char name[32];
    char map[24];
    uint32_t lastseen;
    int used;
};

static ms_entry_t g_ent[MS_MAX_ENTRIES];

// The exact sockaddr length for a family - macOS rejects sends with the padded
// sockaddr_storage length for an IPv4 address.
static socklen_t ms_sslen(const struct sockaddr_storage *ss)
{
    return ss->ss_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
}

static int ms_sameaddr(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a, *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port && !Bmemcmp(&x->sin6_addr, &y->sin6_addr, sizeof(x->sin6_addr));
    }
    const struct sockaddr_in *x = (const struct sockaddr_in *)a, *y = (const struct sockaddr_in *)b;
    return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
}

static void ms_expire(uint32_t now)
{
    for (int i = 0; i < MS_MAX_ENTRIES; i++)
        if (g_ent[i].used && (now - g_ent[i].lastseen) > MS_ENTRY_TTL_MS)
            g_ent[i].used = 0;
}

// read a length-prefixed (1 byte) string into dst
static int ms_getstr(const uint8_t *buf, int len, int *pos, char *dst, int dstsz)
{
    if (*pos >= len) return 0;
    int n = buf[(*pos)++];
    if (*pos + n > len) return 0;
    int c = n < dstsz - 1 ? n : dstsz - 1;
    Bmemcpy(dst, buf + *pos, c);
    dst[c] = 0;
    *pos += n;
    return 1;
}

static int ms_putstr(uint8_t *buf, int *pos, int cap, const char *s)
{
    int n = (int)Bstrlen(s);
    if (n > 31) n = 31;
    if (*pos + 1 + n > cap) return 0;
    buf[(*pos)++] = (uint8_t)n;
    Bmemcpy(buf + *pos, s, n);
    *pos += n;
    return 1;
}

// Handle one validated (magic-checked) packet. Replies go out the socket the
// request arrived on, so v4 stays v4 and v6 stays v6.
static void ms_process(MS_SOCKET s, const uint8_t *buf, int n, const struct sockaddr_storage *from, socklen_t fromlen)
{
    uint32_t now = ms_nowms();
    const uint8_t type = buf[4];
    int pos = 5;

    if (type == MS_ADVERTISE)
    {
        if (pos + 4 > n) return;
        uint16_t gameport = (uint16_t)(buf[pos] | (buf[pos + 1] << 8)); pos += 2;
        uint8_t players = buf[pos++];
        uint8_t maxp = buf[pos++];
        char name[32] = "", map[24] = "";
        ms_getstr(buf, n, &pos, name, sizeof(name));
        ms_getstr(buf, n, &pos, map, sizeof(map));

        int slot = -1, freeSlot = -1;
        for (int i = 0; i < MS_MAX_ENTRIES; i++)
        {
            if (g_ent[i].used) { if (ms_sameaddr(&g_ent[i].src, from)) { slot = i; break; } }
            else if (freeSlot < 0) freeSlot = i;
        }
        if (slot < 0) slot = freeSlot;
        if (slot >= 0)
        {
            int fresh = !g_ent[slot].used;
            g_ent[slot].used = 1;
            g_ent[slot].src = *from;
            g_ent[slot].gameport = gameport;
            g_ent[slot].players = players;
            g_ent[slot].maxplayers = maxp;
            Bstrncpyz(g_ent[slot].name, name, sizeof(g_ent[slot].name));
            Bstrncpyz(g_ent[slot].map, map, sizeof(g_ent[slot].map));
            g_ent[slot].lastseen = now;
            if (fresh)
            {
                char a[128]; ms_addr_to_str(from, gameport, a, sizeof(a));
                LOG_F(INFO, "masterserver: + %s \"%s\" (%s)", a, name, map);
            }
        }
    }
    else if (type == MS_ADVERTISE_STOP)
    {
        for (int i = 0; i < MS_MAX_ENTRIES; i++)
            if (g_ent[i].used && ms_sameaddr(&g_ent[i].src, from)) g_ent[i].used = 0;
    }
    else if (type == MS_LIST_REQ)
    {
        uint8_t out[MS_PKT];
        Bmemcpy(out, MS_MAGIC, 4);
        out[4] = MS_LIST_RESP;
        int op = 6;   // out[5] = count (filled below)
        int count = 0;
        for (int i = 0; i < MS_MAX_ENTRIES && op < (int)sizeof(out) - 80; i++)
        {
            if (!g_ent[i].used) continue;
            char a[128]; ms_addr_to_str(&g_ent[i].src, g_ent[i].gameport, a, sizeof(a));
            int save = op;
            if (!ms_putstr(out, &op, sizeof(out), a)) { op = save; break; }
            if (op + 2 > (int)sizeof(out)) { op = save; break; }
            out[op++] = g_ent[i].players;
            out[op++] = g_ent[i].maxplayers;
            if (!ms_putstr(out, &op, sizeof(out), g_ent[i].name)) { op = save; break; }
            if (!ms_putstr(out, &op, sizeof(out), g_ent[i].map)) { op = save; break; }
            count++;
        }
        out[5] = (uint8_t)count;
        sendto(s, (char *)out, op, 0, (struct sockaddr *)from, fromlen);
    }
    else if (type == MS_STUN_REQ)
    {
        uint8_t out[64];
        Bmemcpy(out, MS_MAGIC, 4);
        out[4] = MS_STUN_RESP;
        int op = 5;
        char a[128]; ms_addr_to_str(from, 0, a, sizeof(a));
        ms_putstr(out, &op, sizeof(out), a);
        sendto(s, (char *)out, op, 0, (struct sockaddr *)from, fromlen);
    }
}

void Masterserver_Run(int32_t port)
{
    if (port <= 0) port = MASTERSERVER_DEFAULT_PORT;

    // Separate IPv4 and IPv6 sockets (a dual-stack socket can't reliably reply to
    // IPv4-mapped peers on macOS).
    MS_SOCKET s4 = ms_opensocket(AF_INET, port);
    MS_SOCKET s6 = ms_opensocket(AF_INET6, port);
    if (s4 == (MS_SOCKET)INVALID_SOCKET && s6 == (MS_SOCKET)INVALID_SOCKET)
    {
        LOG_F(ERROR, "masterserver: could not bind UDP port %d", port);
        return;
    }
    if (s4 != (MS_SOCKET)INVALID_SOCKET) ms_setnonblock(s4);
    if (s6 != (MS_SOCKET)INVALID_SOCKET) ms_setnonblock(s6);
    LOG_F(INFO, "VibeBuild32 masterserver listening on UDP %d (IPv4:%s IPv6:%s)", port,
          s4 != (MS_SOCKET)INVALID_SOCKET ? "on" : "off", s6 != (MS_SOCKET)INVALID_SOCKET ? "on" : "off");

    uint32_t lastExpire = ms_nowms();
    MS_SOCKET socks[2] = { s4, s6 };

    for (;;)
    {
        fd_set rf;
        FD_ZERO(&rf);
        int maxfd = 0;
        for (int k = 0; k < 2; k++)
            if (socks[k] != (MS_SOCKET)INVALID_SOCKET)
            {
                FD_SET(socks[k], &rf);
                if ((int)socks[k] > maxfd) maxfd = (int)socks[k];
            }

        struct timeval tv = { 1, 0 };
        select(maxfd + 1, &rf, NULL, NULL, &tv);

        uint32_t now = ms_nowms();
        if (now - lastExpire > 2000) { ms_expire(now); lastExpire = now; }

        for (int k = 0; k < 2; k++)
        {
            MS_SOCKET s = socks[k];
            if (s == (MS_SOCKET)INVALID_SOCKET || !FD_ISSET(s, &rf)) continue;
            uint8_t buf[MS_PKT];
            struct sockaddr_storage from;
            socklen_t fromlen = sizeof(from);
            int n = (int)recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
            if (n >= 5 && !Bmemcmp(buf, MS_MAGIC, 4))
                ms_process(s, buf, n, &from, fromlen);
        }
    }
}

// ---------------------------------------------------------------------------
// game-server side: advertise ourselves
// ---------------------------------------------------------------------------

static MS_SOCKET g_advSock = (MS_SOCKET)INVALID_SOCKET;
static struct sockaddr_storage g_advDst;
static int g_advOn = 0;
static uint32_t g_advLast = 0;

void Masterserver_SetAdvertise(const char *addr)
{
    g_advOn = 0;
    if (!addr || !addr[0]) return;
    if (!ms_resolve(addr, MASTERSERVER_DEFAULT_PORT, &g_advDst))
    {
        LOG_F(WARNING, "advertise: could not resolve masterserver '%s'", addr);
        return;
    }
    if (g_advSock != (MS_SOCKET)INVALID_SOCKET) { ms_close(g_advSock); g_advSock = (MS_SOCKET)INVALID_SOCKET; }
    g_advSock = ms_opensocket(g_advDst.ss_family, -1);
    if (g_advSock == (MS_SOCKET)INVALID_SOCKET)
    {
        LOG_F(WARNING, "advertise: could not open a socket");
        return;
    }
    ms_setnonblock(g_advSock);
    g_advOn = 1;
    g_advLast = 0;
    LOG_F(INFO, "Advertising this server to masterserver '%s'", addr);
}

int32_t Masterserver_IsAdvertising(void) { return g_advOn; }

void Masterserver_AdvertisePulse(const char *name, int32_t gameport, int32_t players, int32_t maxplayers, const char *map)
{
    if (!g_advOn) return;
    uint32_t now = ms_nowms();
    if (g_advLast && (now - g_advLast) < 5000) return;   // ~every 5s
    g_advLast = now;

    uint8_t out[MS_PKT];
    Bmemcpy(out, MS_MAGIC, 4);
    out[4] = MS_ADVERTISE;
    int op = 5;
    out[op++] = (uint8_t)(gameport & 0xff);
    out[op++] = (uint8_t)((gameport >> 8) & 0xff);
    out[op++] = (uint8_t)players;
    out[op++] = (uint8_t)maxplayers;
    ms_putstr(out, &op, sizeof(out), name ? name : "server");
    ms_putstr(out, &op, sizeof(out), map ? map : "");
    sendto(g_advSock, (char *)out, op, 0, (struct sockaddr *)&g_advDst, ms_sslen(&g_advDst));
}

void Masterserver_AdvertiseStop(void)
{
    if (!g_advOn) return;
    uint8_t out[8];
    Bmemcpy(out, MS_MAGIC, 4);
    out[4] = MS_ADVERTISE_STOP;
    sendto(g_advSock, (char *)out, 5, 0, (struct sockaddr *)&g_advDst, ms_sslen(&g_advDst));
    g_advOn = 0;
}

// ---------------------------------------------------------------------------
// client side: query the list
// ---------------------------------------------------------------------------

int32_t Masterserver_QueryList(const char *masterAddr, struct msserver_t *out, int32_t maxout, int32_t timeoutMs)
{
    struct sockaddr_storage dst;
    if (!ms_resolve(masterAddr, MASTERSERVER_DEFAULT_PORT, &dst))
        return -1;

    MS_SOCKET s = ms_opensocket(dst.ss_family, -1);
    if (s == (MS_SOCKET)INVALID_SOCKET)
        return -1;
    ms_setnonblock(s);

    uint8_t req[8];
    Bmemcpy(req, MS_MAGIC, 4);
    req[4] = MS_LIST_REQ;
    sendto(s, (char *)req, 5, 0, (struct sockaddr *)&dst, ms_sslen(&dst));

    int32_t got = 0;
    uint32_t start = ms_nowms();
    while ((int32_t)(ms_nowms() - start) < timeoutMs && got < maxout)
    {
        uint8_t buf[MS_PKT];
        struct sockaddr_storage from;
        socklen_t fl = sizeof(from);
        int n = (int)recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 6 || Bmemcmp(buf, MS_MAGIC, 4) || buf[4] != MS_LIST_RESP)
        {
#ifdef _WIN32
            Sleep(5);
#else
            struct timeval tv = { 0, 5000 };
            select(0, NULL, NULL, NULL, &tv);
#endif
            continue;
        }
        int count = buf[5];
        int pos = 6;
        for (int i = 0; i < count && got < maxout; i++)
        {
            char a[128] = "", name[32] = "", map[24] = "";
            if (!ms_getstr(buf, n, &pos, a, sizeof(a))) break;
            if (pos + 2 > n) break;
            int players = buf[pos++];
            int maxp = buf[pos++];
            if (!ms_getstr(buf, n, &pos, name, sizeof(name))) break;
            if (!ms_getstr(buf, n, &pos, map, sizeof(map))) break;
            Bstrncpyz(out[got].addr, a, sizeof(out[got].addr));
            Bstrncpyz(out[got].name, name, sizeof(out[got].name));
            Bstrncpyz(out[got].map, map, sizeof(out[got].map));
            out[got].players = players;
            out[got].maxplayers = maxp;
            got++;
        }
        break;   // one response packet is enough for now
    }

    ms_close(s);
    return got;
}
