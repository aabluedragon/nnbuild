// NNBuild masterserver: a public lobby directory and dual-stack (IPv4/IPv6)
// STUN / NAT hole-punch rendezvous. See masterserver.h.

#include <chrono>
#include <mutex>
#include <thread>

#include "masterserver.h"
#include "compat.h"
#include "baselayer.h"
#include "mmulti.h"   // MMULTI_PROBE_QUERY / MMULTI_PROBE_REPLY

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
static const char MS_MAGIC[4] = { 'N', 'B', 'M', 'S' };
enum
{
    MS_ADVERTISE      = 0xB0,   // server  -> master : register / heartbeat
    MS_ADVERTISE_STOP = 0xB1,   // server  -> master : going away
    MS_LIST_REQ       = 0xB2,   // client  -> master : send me the list
    MS_LIST_RESP      = 0xB3,   // master  -> client : (chunk of) the list
    MS_STUN_REQ       = 0xB4,   // anyone  -> master : what is my public address?
    MS_STUN_RESP      = 0xB5,   // master  -> sender : your public address is ...
    MS_PUNCH_REQ      = 0xB6,   // client  -> master : arrange a hole-punch to <server>
    MS_PUNCH_NOTIFY   = 0xB7,   // master  -> server : client <addr> wants in, punch to it
};

#define MS_ENTRY_TTL_MS   30000   // drop a server not heard from in this long
#define MS_MAX_ENTRIES    256
#define MS_PKT            1400    // stay under the common path MTU

// A lobby on the open internet gets abused. Two limits keep it harmless:
//  - Each source address may send MS_RATE_MAX packets per MS_RATE_WINDOW_MS; the
//    rest are dropped unanswered. A list answer is far larger than the request,
//    so without this a forged source address would turn the lobby into a tool
//    for flooding that address. Normal use is a few packets per ten seconds.
//  - One address may hold MS_MAX_PER_ADDR entries, so one machine cannot fill
//    the list.
#define MS_RATE_WINDOW_MS 10000
#define MS_RATE_MAX       60
#define MS_RATE_SLOTS     2048
#define MS_MAX_PER_ADDR   8

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
// on success. defPort is used when the string carries no port. aiFlags goes to
// getaddrinfo: AI_NUMERICHOST accepts only a written-out address and so never
// waits for a name server.
static int ms_resolve_ex(const char *addr, int32_t defPort, struct sockaddr_storage *out, int aiFlags)
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
    hints.ai_flags = aiFlags;
    ms_netinit();
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return 0;
    Bmemcpy(out, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return 1;
}

static int ms_resolve(const char *addr, int32_t defPort, struct sockaddr_storage *out)
{
    return ms_resolve_ex(addr, defPort, out, 0);
}

// Resolve "host[:port]" to its IPv4 and/or IPv6 endpoints (both, when the name
// has them). Fills out4/out6 and sets *have4/*have6. Returns 1 if at least one
// family resolved. Used to advertise a server over both families.
static int ms_resolve_both(const char *addr, int32_t defPort,
                           struct sockaddr_storage *out4, int *have4,
                           struct sockaddr_storage *out6, int *have6)
{
    *have4 = *have6 = 0;

    char host[256];
    Bstrncpyz(host, addr, sizeof(host));
    char portbuf[16];
    Bsnprintf(portbuf, sizeof(portbuf), "%d", defPort);
    const char *port = portbuf;

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
        if (colon && !Bstrchr(colon + 1, ':'))
        {
            *colon = 0;
            port = colon + 1;
        }
    }

    struct addrinfo hints, *res = NULL;
    Bmemset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    ms_netinit();
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return 0;

    for (struct addrinfo *ai = res; ai; ai = ai->ai_next)
    {
        if (ai->ai_family == AF_INET && !*have4)
        {
            Bmemcpy(out4, ai->ai_addr, ai->ai_addrlen);
            *have4 = 1;
        }
        else if (ai->ai_family == AF_INET6 && !*have6)
        {
            Bmemcpy(out6, ai->ai_addr, ai->ai_addrlen);
            *have6 = 1;
        }
    }
    freeaddrinfo(res);
    return (*have4 || *have6);
}

// ---------------------------------------------------------------------------
// masterserver: the standalone directory / STUN process
// ---------------------------------------------------------------------------

struct ms_entry_t
{
    uint32_t serverId;             // random per-server-instance id (merges v4+v6)
    struct sockaddr_storage src4;  // public IPv4 advertise endpoint (family 0 = none)
    struct sockaddr_storage src6;  // public IPv6 advertise endpoint (family 0 = none)
    uint16_t gameport;             // the port the game actually listens on
    uint8_t players, maxplayers;
    char name[32];
    char map[24];
    uint32_t lastseen;
    int used;
};

static ms_entry_t g_ent[MS_MAX_ENTRIES];

// The masterserver's two listening sockets, so a reply can always leave on the
// socket matching the destination family (a v4 socket can't send to a v6 peer,
// and vice-versa) - needed when a punch request arrives on one family but must
// notify a server registered on the other.
static MS_SOCKET g_msSock4 = (MS_SOCKET)INVALID_SOCKET;
static MS_SOCKET g_msSock6 = (MS_SOCKET)INVALID_SOCKET;

// The exact sockaddr length for a family - macOS rejects sends with the padded
// sockaddr_storage length for an IPv4 address.
static socklen_t ms_sslen(const struct sockaddr_storage *ss)
{
    return ss->ss_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
}

// Send to dst on whichever listening socket matches its family.
static void ms_send_family(const struct sockaddr_storage *dst, const void *buf, int len)
{
    MS_SOCKET s = (dst->ss_family == AF_INET6) ? g_msSock6 : g_msSock4;
    if (s != (MS_SOCKET)INVALID_SOCKET)
        sendto(s, (const char *)buf, len, 0, (struct sockaddr *)dst, ms_sslen(dst));
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
        {
            // A server that stopped pinging (crashed / lost link) without a clean
            // stop - drop it once it goes stale so the list only shows live hosts.
            const struct sockaddr_storage *ss = g_ent[i].src4.ss_family ? &g_ent[i].src4 : &g_ent[i].src6;
            char a[128]; ms_addr_to_str(ss, g_ent[i].gameport, a, sizeof(a));
            LOG_F(INFO, "masterserver: - %s \"%s\" (timed out)", a, g_ent[i].name);
            g_ent[i].used = 0;
        }
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
    if (n > 100) n = 100;   // room for long IPv6 [addr]:port strings
    if (*pos + 1 + n > cap) return 0;
    buf[(*pos)++] = (uint8_t)n;
    Bmemcpy(buf + *pos, s, n);
    *pos += n;
    return 1;
}

// Same machine, whatever the port? IPv6 compares the first 64 bits, the part a
// single site is given.
static int ms_sameip(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a, *y = (const struct sockaddr_in6 *)b;
        return !Bmemcmp(&x->sin6_addr, &y->sin6_addr, 8);
    }
    const struct sockaddr_in *x = (const struct sockaddr_in *)a, *y = (const struct sockaddr_in *)b;
    return x->sin_addr.s_addr == y->sin_addr.s_addr;
}

// The per-address packet budget (see MS_RATE_MAX). The table is indexed by a hash
// of the address; two addresses landing on one slot simply share a window.
struct ms_rate_t
{
    uint32_t key, windowStart, count;
};
static ms_rate_t g_rate[MS_RATE_SLOTS];

static int ms_rate_ok(const struct sockaddr_storage *from, uint32_t now)
{
    uint32_t key = 2166136261u;
    const uint8_t *bytes;
    int len;
    if (from->ss_family == AF_INET6)
    {
        bytes = ((const struct sockaddr_in6 *)from)->sin6_addr.s6_addr;
        len = ms_is_v4mapped((const struct sockaddr_in6 *)from) ? 16 : 8;
    }
    else
    {
        bytes = (const uint8_t *)&((const struct sockaddr_in *)from)->sin_addr;
        len = 4;
    }
    for (int i = 0; i < len; i++) key = (key ^ bytes[i]) * 16777619u;
    if (!key) key = 1;

    ms_rate_t *r = &g_rate[key % MS_RATE_SLOTS];
    if (r->key != key || (now - r->windowStart) > MS_RATE_WINDOW_MS)
    {
        r->key = key;
        r->windowStart = now;
        r->count = 0;
    }
    return ++r->count <= MS_RATE_MAX;
}

// Names and map names come from strangers and end up in the log and on players'
// screens: keep printable ASCII only.
static void ms_clean(char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 32 || (unsigned char)*s > 126) *s = '?';
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
        if (pos + 8 > n) return;
        uint16_t gameport = (uint16_t)(buf[pos] | (buf[pos + 1] << 8)); pos += 2;
        uint8_t players = buf[pos++];
        uint8_t maxp = buf[pos++];
        uint32_t serverId = (uint32_t)(buf[pos] | (buf[pos+1] << 8) | (buf[pos+2] << 16) | ((uint32_t)buf[pos+3] << 24));
        pos += 4;
        char name[32] = "", map[24] = "";
        ms_getstr(buf, n, &pos, name, sizeof(name));
        ms_getstr(buf, n, &pos, map, sizeof(map));
        ms_clean(name);
        ms_clean(map);

        // One logical server may advertise from both its IPv4 and IPv6 endpoint;
        // merge those by serverId into a single entry that carries both. Match an
        // existing entry by serverId, else claim a free slot.
        int slot = -1, freeSlot = -1, fromThisAddr = 0;
        for (int i = 0; i < MS_MAX_ENTRIES; i++)
        {
            if (g_ent[i].used)
            {
                if (g_ent[i].serverId == serverId) { slot = i; break; }
                if (ms_sameip(&g_ent[i].src4, from) || ms_sameip(&g_ent[i].src6, from)) fromThisAddr++;
            }
            else if (freeSlot < 0) freeSlot = i;
        }
        if (slot < 0 && fromThisAddr >= MS_MAX_PER_ADDR)
            return;   // this address already lists as many servers as one address may
        if (slot < 0) slot = freeSlot;
        if (slot >= 0)
        {
            int fresh = !g_ent[slot].used;
            if (fresh)
            {
                Bmemset(&g_ent[slot], 0, sizeof(g_ent[slot]));
                g_ent[slot].serverId = serverId;
            }
            g_ent[slot].used = 1;
            // Record this advertise's endpoint in the matching family slot.
            int hadFamily;
            if (from->ss_family == AF_INET6)
            {
                hadFamily = (g_ent[slot].src6.ss_family != 0);
                g_ent[slot].src6 = *from;
            }
            else
            {
                hadFamily = (g_ent[slot].src4.ss_family != 0);
                g_ent[slot].src4 = *from;
            }
            g_ent[slot].gameport = gameport;
            g_ent[slot].players = players;
            g_ent[slot].maxplayers = maxp;
            Bstrncpyz(g_ent[slot].name, name, sizeof(g_ent[slot].name));
            Bstrncpyz(g_ent[slot].map, map, sizeof(g_ent[slot].map));
            g_ent[slot].lastseen = now;
            if (fresh || !hadFamily)
            {
                char a[128]; ms_addr_to_str(from, gameport, a, sizeof(a));
                LOG_F(INFO, "masterserver: + %s \"%s\" (%s)", a, name, map);
            }
        }
    }
    else if (type == MS_ADVERTISE_STOP)
    {
        // Match by serverId when present (clears both families at once); fall
        // back to source-address match for older stop packets.
        uint32_t serverId = 0;
        int haveId = (pos + 4 <= n);
        if (haveId)
            serverId = (uint32_t)(buf[pos] | (buf[pos+1] << 8) | (buf[pos+2] << 16) | ((uint32_t)buf[pos+3] << 24));
        for (int i = 0; i < MS_MAX_ENTRIES; i++)
        {
            if (!g_ent[i].used) continue;
            if (haveId ? (g_ent[i].serverId == serverId)
                       : (ms_sameaddr(&g_ent[i].src4, from) || ms_sameaddr(&g_ent[i].src6, from)))
            {
                const struct sockaddr_storage *ss = g_ent[i].src4.ss_family ? &g_ent[i].src4 : &g_ent[i].src6;
                char a[128]; ms_addr_to_str(ss, g_ent[i].gameport, a, sizeof(a));
                LOG_F(INFO, "masterserver: - %s \"%s\" (going away)", a, g_ent[i].name);
                g_ent[i].used = 0;
            }
        }
    }
    else if (type == MS_LIST_REQ)
    {
        uint8_t out[MS_PKT];
        Bmemcpy(out, MS_MAGIC, 4);
        out[4] = MS_LIST_RESP;
        int op = 6;   // out[5] = count (filled below)
        int count = 0;
        for (int i = 0; i < MS_MAX_ENTRIES && op < (int)sizeof(out) - 160; i++)
        {
            if (!g_ent[i].used) continue;
            char a4[128] = "", a6[128] = "";
            if (g_ent[i].src4.ss_family) ms_addr_to_str(&g_ent[i].src4, g_ent[i].gameport, a4, sizeof(a4));
            if (g_ent[i].src6.ss_family) ms_addr_to_str(&g_ent[i].src6, g_ent[i].gameport, a6, sizeof(a6));
            int save = op;
            if (!ms_putstr(out, &op, sizeof(out), a4)) { op = save; break; }   // IPv4 (may be "")
            if (!ms_putstr(out, &op, sizeof(out), a6)) { op = save; break; }   // IPv6 (may be "")
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
        uint8_t out[160];
        Bmemcpy(out, MS_MAGIC, 4);
        out[4] = MS_STUN_RESP;
        int op = 5;
        char a[128]; ms_addr_to_str(from, 0, a, sizeof(a));
        ms_putstr(out, &op, sizeof(out), a);
        sendto(s, (char *)out, op, 0, (struct sockaddr *)from, fromlen);
    }
    else if (type == MS_PUNCH_REQ)
    {
        // A client wants to reach a server it saw in the list. Find that server
        // and tell it to punch a hole toward this client; also tell the client
        // the server's address so it keeps handshaking there.
        char target[128] = "";
        ms_getstr(buf, n, &pos, target, sizeof(target));
        for (int i = 0; i < MS_MAX_ENTRIES; i++)
        {
            if (!g_ent[i].used) continue;
            // The client may be trying either the v4 or v6 address; punch out the
            // matching family's endpoint.
            char a4[128] = "", a6[128] = "";
            if (g_ent[i].src4.ss_family) ms_addr_to_str(&g_ent[i].src4, g_ent[i].gameport, a4, sizeof(a4));
            if (g_ent[i].src6.ss_family) ms_addr_to_str(&g_ent[i].src6, g_ent[i].gameport, a6, sizeof(a6));
            const struct sockaddr_storage *srv;
            const char *a;
            if (a4[0] && !Bstrcmp(a4, target)) { srv = &g_ent[i].src4; a = a4; }
            else if (a6[0] && !Bstrcmp(a6, target)) { srv = &g_ent[i].src6; a = a6; }
            else continue;

            char cliAddr[128]; ms_addr_to_str(from, 0, cliAddr, sizeof(cliAddr));
            uint8_t out[192];
            Bmemcpy(out, MS_MAGIC, 4); out[4] = MS_PUNCH_NOTIFY;
            int op = 5; ms_putstr(out, &op, sizeof(out), cliAddr);
            // The server may be registered on the other family than the request
            // arrived on, so send the notify out the matching-family socket.
            ms_send_family(srv, out, op);

            uint8_t r[192];
            Bmemcpy(r, MS_MAGIC, 4); r[4] = MS_STUN_RESP;
            int rp = 5; ms_putstr(r, &rp, sizeof(r), a);
            sendto(s, (char *)r, rp, 0, (struct sockaddr *)from, fromlen);

            LOG_F(INFO, "masterserver: punch %s <-> %s", cliAddr, a);
            break;
        }
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
    g_msSock4 = s4;
    g_msSock6 = s6;
    LOG_F(INFO, "NNBuild masterserver listening on UDP %d (IPv4:%s IPv6:%s)", port,
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
            if (n >= 5 && !Bmemcmp(buf, MS_MAGIC, 4) && ms_rate_ok(&from, now))
                ms_process(s, buf, n, &from, fromlen);
        }
    }
}

// ---------------------------------------------------------------------------
// game-server side: advertise ourselves
// ---------------------------------------------------------------------------

// Advertise over both families when the masterserver has both A and AAAA
// records and this machine has connectivity: g_advSock4 carries the mandatory
// IPv4 registration, g_advSock6 the optional IPv6 one. A random serverId lets
// the masterserver merge the two into one listing.
static MS_SOCKET g_advSock4 = (MS_SOCKET)INVALID_SOCKET;
static MS_SOCKET g_advSock6 = (MS_SOCKET)INVALID_SOCKET;
static struct sockaddr_storage g_advDst4, g_advDst6;
static int g_advHave4 = 0, g_advHave6 = 0;
static int g_advOn = 0;
static uint32_t g_advLast = 0;
static uint32_t g_serverId = 0;

void Masterserver_SetAdvertise(const char *addr)
{
    g_advOn = 0;
    if (g_advSock4 != (MS_SOCKET)INVALID_SOCKET) { ms_close(g_advSock4); g_advSock4 = (MS_SOCKET)INVALID_SOCKET; }
    if (g_advSock6 != (MS_SOCKET)INVALID_SOCKET) { ms_close(g_advSock6); g_advSock6 = (MS_SOCKET)INVALID_SOCKET; }
    if (!addr || !addr[0]) return;

    if (!ms_resolve_both(addr, MASTERSERVER_DEFAULT_PORT, &g_advDst4, &g_advHave4, &g_advDst6, &g_advHave6))
    {
        LOG_F(WARNING, "advertise: could not resolve masterserver '%s'", addr);
        return;
    }

    if (g_advHave4)
    {
        g_advSock4 = ms_opensocket(AF_INET, -1);
        if (g_advSock4 != (MS_SOCKET)INVALID_SOCKET) ms_setnonblock(g_advSock4);
        else g_advHave4 = 0;
    }
    if (g_advHave6)
    {
        g_advSock6 = ms_opensocket(AF_INET6, -1);
        if (g_advSock6 != (MS_SOCKET)INVALID_SOCKET) ms_setnonblock(g_advSock6);
        else g_advHave6 = 0;
    }
    if (!g_advHave4 && !g_advHave6)
    {
        LOG_F(WARNING, "advertise: could not open a socket");
        return;
    }

    // A per-instance id so the masterserver folds our IPv4 and IPv6 advertises
    // into a single listing.
    g_serverId = ms_nowms() ^ ((uint32_t)(uintptr_t)&g_serverId) ^ (g_serverId * 2654435761u + 1u);
    if (!g_serverId) g_serverId = 1;

    g_advOn = 1;
    g_advLast = 0;
    LOG_F(INFO, "Advertising this server to masterserver '%s' (IPv4:%s IPv6:%s)",
          addr, g_advHave4 ? "on" : "off", g_advHave6 ? "on" : "off");
}

int32_t Masterserver_IsAdvertising(void) { return g_advOn; }

static int ms_build_advertise(uint8_t *out, int cap, const char *name, int32_t gameport,
                              int32_t players, int32_t maxplayers, const char *map)
{
    Bmemcpy(out, MS_MAGIC, 4);
    out[4] = MS_ADVERTISE;
    int op = 5;
    out[op++] = (uint8_t)(gameport & 0xff);
    out[op++] = (uint8_t)((gameport >> 8) & 0xff);
    out[op++] = (uint8_t)players;
    out[op++] = (uint8_t)maxplayers;
    out[op++] = (uint8_t)(g_serverId & 0xff);
    out[op++] = (uint8_t)((g_serverId >> 8) & 0xff);
    out[op++] = (uint8_t)((g_serverId >> 16) & 0xff);
    out[op++] = (uint8_t)((g_serverId >> 24) & 0xff);
    ms_putstr(out, &op, cap, name ? name : "server");
    ms_putstr(out, &op, cap, map ? map : "");
    return op;
}

void Masterserver_AdvertisePulse(const char *name, int32_t gameport, int32_t players, int32_t maxplayers, const char *map)
{
    if (!g_advOn) return;
    uint32_t now = ms_nowms();
    if (g_advLast && (now - g_advLast) < 5000) return;   // ~every 5s
    g_advLast = now;

    uint8_t out[MS_PKT];
    int op = ms_build_advertise(out, sizeof(out), name, gameport, players, maxplayers, map);

    if (g_advHave4 && g_advSock4 != (MS_SOCKET)INVALID_SOCKET)
        sendto(g_advSock4, (char *)out, op, 0, (struct sockaddr *)&g_advDst4, ms_sslen(&g_advDst4));
    if (g_advHave6 && g_advSock6 != (MS_SOCKET)INVALID_SOCKET)
        sendto(g_advSock6, (char *)out, op, 0, (struct sockaddr *)&g_advDst6, ms_sslen(&g_advDst6));
}

void Masterserver_AdvertiseStop(void)
{
    if (!g_advOn) return;
    uint8_t out[16];
    Bmemcpy(out, MS_MAGIC, 4);
    out[4] = MS_ADVERTISE_STOP;
    out[5] = (uint8_t)(g_serverId & 0xff);
    out[6] = (uint8_t)((g_serverId >> 8) & 0xff);
    out[7] = (uint8_t)((g_serverId >> 16) & 0xff);
    out[8] = (uint8_t)((g_serverId >> 24) & 0xff);
    if (g_advHave4 && g_advSock4 != (MS_SOCKET)INVALID_SOCKET)
        sendto(g_advSock4, (char *)out, 9, 0, (struct sockaddr *)&g_advDst4, ms_sslen(&g_advDst4));
    if (g_advHave6 && g_advSock6 != (MS_SOCKET)INVALID_SOCKET)
        sendto(g_advSock6, (char *)out, 9, 0, (struct sockaddr *)&g_advDst6, ms_sslen(&g_advDst6));
    g_advOn = 0;
}

// ---------------------------------------------------------------------------
// NAT hole-punch assist
// ---------------------------------------------------------------------------

static void (*g_punchFn)(const char *) = NULL;

void Masterserver_SetPunchFn(void (*fn)(const char *))
{
    g_punchFn = fn;
}

// Host: drain any punch requests the masterserver forwarded to our advertise
// socket, opening a NAT hole to each waiting client via the registered callback.
static void ms_pump_sock(MS_SOCKET s)
{
    if (s == (MS_SOCKET)INVALID_SOCKET) return;
    for (int guard = 0; guard < 16; guard++)
    {
        uint8_t buf[MS_PKT];
        struct sockaddr_storage from;
        socklen_t fl = sizeof(from);
        int n = (int)recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 5 || Bmemcmp(buf, MS_MAGIC, 4))
            break;
        if (buf[4] == MS_PUNCH_NOTIFY)
        {
            char cli[128] = "";
            int pos = 5;
            ms_getstr(buf, n, &pos, cli, sizeof(cli));
            // Open a hole toward the waiting client (IPv4 or IPv6) from the game
            // socket. The callback resolves the address string itself.
            if (cli[0] && g_punchFn)
            {
                g_punchFn(cli);
                LOG_F(INFO, "punch: opening hole to %s for an incoming client", cli);
            }
        }
    }
}

// Host: drain punch requests the masterserver forwarded to either advertise
// socket, opening a NAT hole to each waiting client via the registered callback.
void Masterserver_HostPump(void)
{
    if (!g_advOn) return;
    ms_pump_sock(g_advSock4);
    ms_pump_sock(g_advSock6);
}

// Client: resolve the masterserver to an IPv4 endpoint (for sending a punch
// request from the game socket) and build the request packet. Returns the
// packet length, or 0 on failure; fills *ip/*port (network order).
int32_t Masterserver_MakePunchReq(const char *masterAddr, const char *targetAddr,
                                  char *outMasterEp, int32_t outcap, void *buf, int32_t cap)
{
    // Contact the masterserver over the same family as the join target, so the
    // master learns our game endpoint in that family and can rendezvous it with
    // the server's matching (v4 or v6) advertise endpoint.
    struct sockaddr_storage tss;
    if (!ms_resolve(targetAddr, 0, &tss))
        return 0;
    int want6 = (tss.ss_family == AF_INET6 && !ms_is_v4mapped((const struct sockaddr_in6 *)&tss));

    struct sockaddr_storage m4, m6;
    int h4 = 0, h6 = 0;
    if (!ms_resolve_both(masterAddr, MASTERSERVER_DEFAULT_PORT, &m4, &h4, &m6, &h6))
        return 0;

    const struct sockaddr_storage *m;
    if (want6 && h6) m = &m6;
    else if (!want6 && h4) m = &m4;
    else if (h4) m = &m4;         // target v6 but master has no v6: fall back to v4
    else if (h6) m = &m6;
    else return 0;

    ms_addr_to_str(m, 0, outMasterEp, outcap);   // keep the master's own port

    uint8_t *out = (uint8_t *)buf;
    if (cap < 6) return 0;
    Bmemcpy(out, MS_MAGIC, 4);
    out[4] = MS_PUNCH_REQ;
    int op = 5;
    ms_putstr(out, &op, cap, targetAddr);
    return op;
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
            char a4[128] = "", a6[128] = "", name[32] = "", map[24] = "";
            if (!ms_getstr(buf, n, &pos, a4, sizeof(a4))) break;
            if (!ms_getstr(buf, n, &pos, a6, sizeof(a6))) break;
            if (pos + 2 > n) break;
            int players = buf[pos++];
            int maxp = buf[pos++];
            if (!ms_getstr(buf, n, &pos, name, sizeof(name))) break;
            if (!ms_getstr(buf, n, &pos, map, sizeof(map))) break;
            Bstrncpyz(out[got].addr, a4, sizeof(out[got].addr));
            Bstrncpyz(out[got].addr6, a6, sizeof(out[got].addr6));
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

// ---------------------------------------------------------------------------
// client side: the list, without blocking the caller
// ---------------------------------------------------------------------------

static std::mutex s_queryMutex;
static int32_t s_queryGen = 0;    // identifies the newest query
static int32_t s_queryDone = 0;   // the newest query has its answer
static int32_t s_queryCount = 0;
static struct msserver_t s_queryList[64];

void Masterserver_QueryListBegin(const char *masterAddr)
{
    struct { char addr[256]; int32_t gen; } job;
    Bstrncpyz(job.addr, masterAddr ? masterAddr : "", sizeof(job.addr));
    {
        std::lock_guard<std::mutex> lock(s_queryMutex);
        job.gen = ++s_queryGen;
        s_queryDone = 0;
    }

    std::thread([job]
    {
        struct msserver_t list[64];
        int32_t const n = Masterserver_QueryList(job.addr, list, 64, 1500);

        std::lock_guard<std::mutex> lock(s_queryMutex);
        if (job.gen != s_queryGen)
            return;   // a newer query replaced this one
        if (n > 0) Bmemcpy(s_queryList, list, n * sizeof(list[0]));
        s_queryCount = n;
        s_queryDone = 1;
    }).detach();
}

int32_t Masterserver_QueryListPoll(struct msserver_t *out, int32_t maxout, int32_t *count)
{
    std::lock_guard<std::mutex> lock(s_queryMutex);
    if (!s_queryDone)
        return 0;

    int32_t n = s_queryCount;
    if (n > maxout) n = maxout;
    if (n > 0) Bmemcpy(out, s_queryList, n * sizeof(out[0]));
    *count = n;
    return 1;
}

// ---------------------------------------------------------------------------
// client side: reach a listed server over IPv4 and IPv6 at once
// ---------------------------------------------------------------------------

#define MSJOIN_GAME_PORT      23513  // a server address written without a port (mmulti's default)
#define MSJOIN_PROBE_EVERY_MS 400    // repeat the question this often
#define MSJOIN_HELP_FIRST_MS  1200   // then ask the lobby to open a path ...
#define MSJOIN_HELP_EVERY_MS  3000   // ... and keep asking

// One kind of address (IPv4 or IPv6) of the server being joined.
struct ms_jointry_t
{
    MS_SOCKET sock;
    int have;                       // there is an address of this kind to try
    int ok;                         // the server answered over it
    int full;                       // ... and said it has no free slot
    uint32_t answeredAt;            // ms after the start
    struct sockaddr_storage dst;    // the server
    char str[128];                  // the same, as the list wrote it
    struct sockaddr_storage lobby;  // the lobby's address of this kind
    int haveLobby;
};

static struct
{
    int32_t state, prefer6, gen;
    uint32_t start, lastProbe, lastHelp, endElapsed;
    int32_t helpCount;
    uint8_t nonce[4];
    ms_jointry_t v4, v6;
    char chosen[128];
} s_join;

// The lobby's addresses, written by the thread that looks its name up.
static std::mutex s_joinMutex;
static int32_t s_joinGen = 0;
static struct sockaddr_storage s_joinLobby4, s_joinLobby6;
static int s_joinLobbyHave4 = 0, s_joinLobbyHave6 = 0, s_joinLobbyReady = 0;

static void ms_join_close(void)
{
    if (s_join.v4.have && s_join.v4.sock != (MS_SOCKET)INVALID_SOCKET) ms_close(s_join.v4.sock);
    if (s_join.v6.have && s_join.v6.sock != (MS_SOCKET)INVALID_SOCKET) ms_close(s_join.v6.sock);
    s_join.v4.sock = s_join.v6.sock = (MS_SOCKET)INVALID_SOCKET;
}

static void ms_join_open(ms_jointry_t *t, const char *addr, int family, int32_t localPort)
{
    t->sock = (MS_SOCKET)INVALID_SOCKET;
    if (!addr[0] || !ms_resolve_ex(addr, MSJOIN_GAME_PORT, &t->dst, AI_NUMERICHOST) || t->dst.ss_family != family)
        return;

    // The match will use localPort. Asking from the same port means a path
    // opened through a router now is still open then.
    t->sock = ms_opensocket(family, localPort);
    if (t->sock == (MS_SOCKET)INVALID_SOCKET)
        t->sock = ms_opensocket(family, -1);
    if (t->sock == (MS_SOCKET)INVALID_SOCKET)
        return;

    ms_setnonblock(t->sock);
    Bstrncpyz(t->str, addr, sizeof(t->str));
    t->have = 1;
}

void Masterserver_JoinCancel(void)
{
    if (s_join.state != MSJOIN_IDLE)
        ms_join_close();
    s_join.state = MSJOIN_IDLE;

    std::lock_guard<std::mutex> lock(s_joinMutex);
    ++s_joinGen;   // an unfinished lobby lookup now belongs to nobody
}

int32_t Masterserver_JoinBegin(const struct msserver_t *server, const char *masterAddr,
                               int32_t localPort, int32_t preferIPv6)
{
    Masterserver_JoinCancel();
    Bmemset(&s_join, 0, sizeof(s_join));
    s_join.prefer6 = preferIPv6;

    ms_join_open(&s_join.v4, server->addr, AF_INET, localPort);
    ms_join_open(&s_join.v6, server->addr6, AF_INET6, localPort);
    if (!s_join.v4.have && !s_join.v6.have)
        return 0;

    // The server repeats these four bytes in its answer, so an answer to an
    // earlier attempt, or one somebody made up, is not taken for this one.
    static uint32_t counter;
    uint32_t const r = (ms_nowms() * 2654435761u) ^ ((uint32_t)(uintptr_t)&s_join >> 4) ^ (++counter * 40503u)
                       ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
    s_join.nonce[0] = (uint8_t)r; s_join.nonce[1] = (uint8_t)(r >> 8);
    s_join.nonce[2] = (uint8_t)(r >> 16); s_join.nonce[3] = (uint8_t)(r >> 24);

    s_join.start = ms_nowms();
    s_join.state = MSJOIN_TRYING;

    // Looking the lobby's name up can take a while, so it happens on the side.
    // Until it is done, the direct questions go out without the lobby's help.
    struct { char addr[256]; int32_t gen; } job;
    Bstrncpyz(job.addr, masterAddr ? masterAddr : "", sizeof(job.addr));
    {
        std::lock_guard<std::mutex> lock(s_joinMutex);
        job.gen = s_join.gen = ++s_joinGen;
        s_joinLobbyReady = 0;
    }
    if (job.addr[0])
        std::thread([job]
        {
            struct sockaddr_storage m4, m6;
            int h4 = 0, h6 = 0;
            ms_resolve_both(job.addr, MASTERSERVER_DEFAULT_PORT, &m4, &h4, &m6, &h6);

            std::lock_guard<std::mutex> lock(s_joinMutex);
            if (job.gen != s_joinGen)
                return;
            s_joinLobby4 = m4; s_joinLobbyHave4 = h4;
            s_joinLobby6 = m6; s_joinLobbyHave6 = h6;
            s_joinLobbyReady = 1;
        }).detach();

    return 1;
}

static void ms_join_read(ms_jointry_t *t, uint32_t elapsed)
{
    if (!t->have) return;
    for (int guard = 0; guard < 16; guard++)
    {
        uint8_t buf[64];
        struct sockaddr_storage from;
        socklen_t fl = sizeof(from);
        int const n = (int)recvfrom(t->sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0)
            break;
        if (n == 9 && !Bmemcmp(buf, MMULTI_PROBE_REPLY, 4) && !Bmemcmp(buf + 4, s_join.nonce, 4) && !t->ok)
        {
            t->ok = 1;
            t->full = (buf[8] == 0);
            t->answeredAt = elapsed;
        }
    }
}

static void ms_join_ask(ms_jointry_t *t)
{
    if (!t->have || t->ok) return;
    uint8_t q[8];
    Bmemcpy(q, MMULTI_PROBE_QUERY, 4);
    Bmemcpy(q + 4, s_join.nonce, 4);
    sendto(t->sock, (const char *)q, sizeof(q), 0, (struct sockaddr *)&t->dst, ms_sslen(&t->dst));
}

// Ask the lobby to have the server send something our way first, which opens
// the server's router for us. Sent from the socket that asks the server, so the
// lobby sees the address the server has to open for.
static int ms_join_help(ms_jointry_t *t)
{
    if (!t->have || t->ok || !t->haveLobby) return 0;
    uint8_t out[160];
    Bmemcpy(out, MS_MAGIC, 4);
    out[4] = MS_PUNCH_REQ;
    int op = 5;
    ms_putstr(out, &op, sizeof(out), t->str);
    sendto(t->sock, (const char *)out, op, 0, (struct sockaddr *)&t->lobby, ms_sslen(&t->lobby));
    return 1;
}

int32_t Masterserver_JoinPoll(struct msjoin_t *status)
{
    if (s_join.state == MSJOIN_TRYING)
    {
        uint32_t const now = ms_nowms();
        uint32_t const elapsed = now - s_join.start;

        if (!s_join.v4.haveLobby && !s_join.v6.haveLobby)
        {
            std::lock_guard<std::mutex> lock(s_joinMutex);
            if (s_joinLobbyReady && s_join.gen == s_joinGen)
            {
                s_join.v4.lobby = s_joinLobby4; s_join.v4.haveLobby = s_joinLobbyHave4;
                s_join.v6.lobby = s_joinLobby6; s_join.v6.haveLobby = s_joinLobbyHave6;
                s_joinLobbyReady = 0;
            }
        }

        ms_join_read(&s_join.v4, elapsed);
        ms_join_read(&s_join.v6, elapsed);

        if (!s_join.lastProbe || (now - s_join.lastProbe) >= MSJOIN_PROBE_EVERY_MS)
        {
            s_join.lastProbe = now | 1;
            ms_join_ask(&s_join.v4);
            ms_join_ask(&s_join.v6);
        }

        if (elapsed >= MSJOIN_HELP_FIRST_MS && (!s_join.lastHelp || (now - s_join.lastHelp) >= MSJOIN_HELP_EVERY_MS))
        {
            int const sent = ms_join_help(&s_join.v4) + ms_join_help(&s_join.v6);
            if (sent)
            {
                s_join.lastHelp = now | 1;
                s_join.helpCount++;
            }
        }

        const ms_jointry_t *win = NULL;
        if (s_join.prefer6)
        {
            // IPv6 wins as soon as it answers. IPv4 wins once IPv6 has had its
            // extra time, or at once when there is no IPv6 address to wait for.
            if (s_join.v6.ok) win = &s_join.v6;
            else if (s_join.v4.ok && (!s_join.v6.have || (elapsed - s_join.v4.answeredAt) >= MSJOIN_V6_GRACE_MS))
                win = &s_join.v4;
        }
        else if (s_join.v4.ok) win = &s_join.v4;
        else if (s_join.v6.ok) win = &s_join.v6;

        if (win)
        {
            Bstrncpyz(s_join.chosen, win->str, sizeof(s_join.chosen));
            s_join.state = win->full ? MSJOIN_FULL : MSJOIN_CONNECTED;
        }
        else if (elapsed >= MSJOIN_TIMEOUT_MS)
            s_join.state = MSJOIN_FAILED;

        if (s_join.state != MSJOIN_TRYING)
        {
            s_join.endElapsed = elapsed;
            ms_join_close();   // the match needs this port next
        }
    }

    if (status)
    {
        uint32_t const elapsed = (s_join.state == MSJOIN_TRYING) ? (ms_nowms() - s_join.start) : s_join.endElapsed;
        Bmemset(status, 0, sizeof(*status));
        status->state = s_join.state;
        status->have4 = s_join.v4.have;  status->have6 = s_join.v6.have;
        status->ok4 = s_join.v4.ok;      status->ok6 = s_join.v6.ok;
        status->ms4 = (int32_t)s_join.v4.answeredAt;
        status->ms6 = (int32_t)s_join.v6.answeredAt;
        status->elapsedMs = (int32_t)elapsed;
        status->lobbyRequests = s_join.helpCount;
        if (s_join.state == MSJOIN_TRYING && s_join.prefer6 && s_join.v4.ok && s_join.v6.have && !s_join.v6.ok)
            status->graceLeftMs = MSJOIN_V6_GRACE_MS - (int32_t)(elapsed - s_join.v4.answeredAt);
        Bstrncpyz(status->addr4, s_join.v4.str, sizeof(status->addr4));
        Bstrncpyz(status->addr6, s_join.v6.str, sizeof(status->addr6));
        Bstrncpyz(status->chosen, s_join.chosen, sizeof(status->chosen));
    }
    return s_join.state;
}
