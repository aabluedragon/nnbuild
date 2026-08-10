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

// Receiver state for one incoming file. The transport (enet for Blood/RR, mmulti
// for SW) only decides how bytes arrive; reassembly, CRC check, safe write and
// group-mount are identical, so they live here.
struct vb_filerecv_t
{
    char    *buf;                                    // reassembly buffer (owned; freed when done/reset)
    int      chunkTotal, chunksGot, lastLen;
    uint32_t crc;
    char     name[64];
};

// NOTE: the three functions below call engine services (kopen4loadfrommod, buildvfs_*,
// initgroupfile, LOG_F, Xmalloc, ...), so include this header AFTER the engine/game
// headers that declare them (it is, in every game's netcode TU).

// Do we already have <name> at this size+crc? Searches mounted groups + the cwd.
static inline int VB_HaveFile(const char *name, int size, uint32_t crc)
{
    int fil = kopen4loadfrommod(name, 0);
    if (fil == -1) return 0;
    int const len = kfilelength(fil);
    if (len != size) { kclose(fil); return 0; }
    char *tmp = (char *)Xmalloc(len + 1);
    kread(fil, tmp, len); kclose(fil);
    uint32_t const have = VB_FileCrc(tmp, len);
    Xfree(tmp);
    return have == crc;
}

// Host->joiner file receiver (transport-independent). <tag> names the game for logs.
static inline void VB_RecvFileHeader(vb_filerecv_t *r, const uint8_t *pkt, const char *tag)
{
    int j = 1;
    int const size = (int)VB_get32(&pkt[j]); j += 4;
    r->crc = VB_get32(&pkt[j]); j += 4;
    r->chunkTotal = VB_get16(&pkt[j]); j += 2;
    int const namelen = pkt[j++];
    Bmemcpy(r->name, &pkt[j], min(namelen, 63)); r->name[min(namelen, 63)] = 0; j += namelen;
    int const fileIndex = pkt[j++], fileTotal = pkt[j++];
    char ofY[24] = ""; if (fileTotal > 1) Bsnprintf(ofY, sizeof(ofY), " (file %d of %d)", fileIndex, fileTotal);
    DO_FREE_AND_NULL(r->buf); r->chunksGot = 0;
    if (VB_HaveFile(r->name, size, r->crc)) { LOG_F(INFO, "%s: already have %s%s, skipping", tag, r->name, ofY); return; }
    if (r->chunkTotal > 0) r->buf = (char *)Xmalloc(r->chunkTotal * VB_FILE_CHUNK_SIZE);
    LOG_F(INFO, "%s: receiving %s (%d bytes) from host%s", tag, r->name, size, ofY);
}

static inline void VB_RecvFileChunk(vb_filerecv_t *r, const uint8_t *pkt, const char *tag)
{
    if (!r->buf) return;
    int const seq = VB_get16(&pkt[1]), total = VB_get16(&pkt[3]), len = VB_get16(&pkt[5]);
    if (total != r->chunkTotal || (unsigned)seq >= (unsigned)total || (unsigned)len > VB_FILE_CHUNK_SIZE) return;
    Bmemcpy(&r->buf[seq * VB_FILE_CHUNK_SIZE], &pkt[7], len);
    r->chunksGot++;
    if (seq == total - 1) r->lastLen = len;
    if (r->chunksGot < r->chunkTotal) return;
    int const filesize = (r->chunkTotal - 1) * VB_FILE_CHUNK_SIZE + r->lastLen;
    if (VB_FileCrc(r->buf, filesize) != r->crc) { LOG_F(ERROR, "%s: %s CRC mismatch, discarding", tag, r->name); DO_FREE_AND_NULL(r->buf); return; }
    const char *fslash = Bstrrchr(r->name, '/'), *bslash = Bstrrchr(r->name, '\\');
    const char *base = max(fslash, bslash); base = base ? base + 1 : r->name;
    if (base[0] == 0 || Bstrstr(base, "..")) { LOG_F(ERROR, "%s: unsafe recv name '%s'", tag, r->name); DO_FREE_AND_NULL(r->buf); return; }
    buildvfs_FILE fp = buildvfs_fopen_write(base);
    if (fp)
    {
        buildvfs_fwrite(r->buf, 1, filesize, fp); buildvfs_fclose(fp);
        LOG_F(INFO, "%s: wrote %s (%d bytes)", tag, base, filesize);
        const char *ext = Bstrrchr(r->name, '.');
        if (ext && (!Bstrcasecmp(ext, ".grp") || !Bstrcasecmp(ext, ".zip") || !Bstrcasecmp(ext, ".pk3")
                 || !Bstrcasecmp(ext, ".pk4") || !Bstrcasecmp(ext, ".rff")))
            if (initgroupfile(base) != -1) LOG_F(INFO, "%s: mounted %s", tag, base);
        // .map user maps are just written to the working dir; the game opens them by name.
    }
    else LOG_F(ERROR, "%s: couldn't write %s", tag, base);
    DO_FREE_AND_NULL(r->buf);
}
