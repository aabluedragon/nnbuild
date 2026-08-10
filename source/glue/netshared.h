#pragma once
//
// VibeBuild32 shared netcode glue for the non-Duke games (Shadow Warrior, Blood,
// Redneck Rampage) linked into the unified binary.
//
// The masterserver-client + file-transfer support was originally copy-pasted into
// each game's netcode; this header is the single source of truth for the pieces
// that were identical everywhere:
//   - the weak Masterserver_* client API (Duke's masterserver.cpp provides the
//     strong implementation; declared weak so a standalone game build links with
//     them null and guards every call),
//   - the msserver_t record returned by Masterserver_QueryList (layout must match
//     masterserver.h exactly),
//   - the FNV-1a file CRC and little-endian byte pack/unpack helpers (must match
//     Duke's), and
//   - the host->joiner file-transfer wire constants.
//
// Everything here is header-only (weak decls + static inline), so it adds no new
// linker symbols and each game keeps its own thin transport glue.
//
#include "compat.h"

#ifdef __GNUC__
# define MS_WEAK __attribute__((weak))
#else
# define MS_WEAK
#endif

// Duke's game-agnostic masterserver API (masterserver.cpp). Weak: binds to Duke's
// implementation in the combined binary, stays null in a standalone game build.
extern "C" {
    void    Masterserver_SetAdvertise(const char *addr) MS_WEAK;
    int32_t Masterserver_IsAdvertising(void) MS_WEAK;
    void    Masterserver_AdvertisePulse(const char *name, int32_t gameport, int32_t players, int32_t maxplayers, const char *map) MS_WEAK;
    void    Masterserver_AdvertiseStop(void) MS_WEAK;
    void    Masterserver_SetPunchFn(void (*fn)(const char *)) MS_WEAK;
    void    Masterserver_HostPump(void) MS_WEAK;
    int32_t Masterserver_QueryList(const char *masterAddr, void *out, int32_t maxout, int32_t timeoutMs) MS_WEAK;
    int32_t Masterserver_MakePunchReq(const char *masterAddr, const char *targetAddr,
                                      char *outMasterEp, int32_t outcap, void *buf, int32_t cap) MS_WEAK;
}

// One advertised server from Masterserver_QueryList. MUST match masterserver.h's
// msserver_t layout byte-for-byte (the Duke side memcpys into an array of these).
struct vb_msserver_t
{
    char    addr[128];
    char    addr6[128];
    char    name[32];
    char    map[24];
    int32_t players, maxplayers;
};

// FNV-1a over the file bytes; must match Duke's masterserver/file-transfer CRC.
static inline uint32_t VB_FileCrc(const char *data, int len)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) h = (h ^ (uint8_t)data[i]) * 16777619u;
    return h;
}
static inline void     VB_put32(char *p, uint32_t v) { p[0]=(char)v; p[1]=(char)(v>>8); p[2]=(char)(v>>16); p[3]=(char)(v>>24); }
static inline void     VB_put16(char *p, uint16_t v) { p[0]=(char)v; p[1]=(char)(v>>8); }
static inline uint32_t VB_get32(const uint8_t *p) { return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24); }
static inline uint16_t VB_get16(const uint8_t *p) { return (uint16_t)(p[0]|(p[1]<<8)); }

// Host->joiner file-transfer wire protocol: a HEADER packet (size, crc, chunk
// count, name, file index/total) followed by CHUNK packets of up to CHUNK_SIZE.
#define VB_FILE_HEADER      240
#define VB_FILE_CHUNK       241
#define VB_FILE_CHUNK_SIZE  1024
#define VB_MAX_NETFILES     8
