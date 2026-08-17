// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.
//
// This file has been modified from Ken Silverman's original release
// by Jonathon Fowler (jf@jonof.id.au)
// by the EDuke32 team (development@voidpoint.com)

#include "baselayer.h"
#include "build.h"
#include "cache1d.h"
#include "colmatch.h"
#include "compat.h"
#include "crc32.h"
#include "engine_priv.h"
#include "lz4.h"
#include "miniz.h"
#include "texcache.h"
#include "vfs.h"

static void *g_vm_data;

// The tile file number (tilesXXX <- this) of each tile:
// 0 <= . < MAXARTFILES_BASE: tile is in a "base" ART file
// MAXARTFILES_BASE <= . < MAXARTFILES_TOTAL: tile is in a map-specific ART file
EDUKE32_STATIC_ASSERT(MAXARTFILES_TOTAL <= 256);

static int32_t tilefileoffs[MAXTILES];
static char tilefilesearchfirst[MAXTILES];
static char g_artBaseSearchFirst;

// Backup tilefilenum[] and tilefileoffs[]. These get allocated only when
// necessary (have per-map ART files).
static uint8_t *g_bakTileFileNum;
static int32_t *g_bakTileFileOffs;
static char *g_bakTileFileSearchFirst;
static vec2_16_t *g_bakTileSiz;
static picanm_t *g_bakPicAnm;
static char * g_bakFakeTile;
static char ** g_bakFakeTileData;
static rottile_t *g_bakRottile;
// NOTE: picsiz[] is not backed up, but recalculated when necessary.

//static int32_t artsize = 0;
static int32_t g_vm_size = 0;

static char artfilename[20];
static char mapartfilename[BMAX_PATH];  // map-specific ART file name
static int32_t mapartfnXXofs;  // byte offset to 'XX' (the number part) in the above
static int32_t artfilnum, artfilplc;
static buildvfs_kfd artfil;

////////// Per-map ART file loading //////////

// Some forward declarations.
static const char *artGetIndexedFileName(int32_t tilefilei);
static int32_t artReadIndexedFile(int32_t tilefilei);

static inline void artClearMapArtFilename(void)
{
    Bmemset(mapartfilename, 0, sizeof(mapartfilename));
    mapartfnXXofs = 0;
}

static inline void artUpdateManifest(void)
{
    for (bssize_t i=0; i<MAXTILES; i++)
        tileUpdatePicSiz(i);
}

template <typename origar_t, typename bakar_t>
static inline void RESTORE_MAPART_ARRAY(origar_t & origar, bakar_t & bakar)
{
    EDUKE32_STATIC_ASSERT(sizeof(origar[0]) == sizeof(bakar[0]));
    for (size_t i=0; i < ARRAY_SIZE(origar); i++)
        if (tilefilenum[i] >= MAXARTFILES_BASE)
            origar[i] = bakar[i];
    DO_FREE_AND_NULL(bakar);
}

template <typename origar_t, typename bakar_t>
static inline void ALLOC_MAPART_ARRAY(origar_t & origar, bakar_t & bakar)
{
    bakar = (bakar_t) Xmalloc(ARRAY_SIZE(origar) * sizeof(origar[0]));
    Bmemcpy(bakar, origar, ARRAY_SIZE(origar) * sizeof(origar[0]));
}

void artClearMapArt(void)
{
    if (g_bakTileFileNum == NULL)
        return;  // per-map ART N/A

    artClearMapArtFilename();

    if (artfilnum >= MAXARTFILES_BASE)
    {
        kclose(artfil);

        artfil = buildvfs_kfd_invalid;
        artfilnum = -1;
        artfilplc = 0L;
    }

    for (bssize_t i=0; i<MAXTILES; i++)
    {
        if (tilefilenum[i] >= MAXARTFILES_BASE)
        {
            // XXX: OK way to free it? Better: cache1d API. CACHE1D_FREE
            walock[i] = CACHE1D_FREE;
            waloff[i] = 0;
        }
    }

    // Restore original per-tile arrays
    RESTORE_MAPART_ARRAY(tilefileoffs, g_bakTileFileOffs);
    RESTORE_MAPART_ARRAY(tilefilesearchfirst, g_bakTileFileSearchFirst);
    RESTORE_MAPART_ARRAY(tilesiz, g_bakTileSiz);
    RESTORE_MAPART_ARRAY(picanm, g_bakPicAnm);
    RESTORE_MAPART_ARRAY(rottile, g_bakRottile);

    // restore entire faketile array as it can cause problems otherwise
    EDUKE32_STATIC_ASSERT(sizeof(faketile[0]) == sizeof(g_bakFakeTile[0]));
    Bmemcpy(faketile, g_bakFakeTile, ARRAY_SIZE(faketile) * sizeof(faketile[0]));
    DO_FREE_AND_NULL(g_bakFakeTile);

    for (size_t i = 0; i < MAXUSERTILES; ++i)
    {
        if (tilefilenum[i] >= MAXARTFILES_BASE && faketiledata[i] != g_bakFakeTileData[i])
        {
            Xfree(faketiledata[i]);
            faketiledata[i] = g_bakFakeTileData[i];
        }
    }
    DO_FREE_AND_NULL(g_bakFakeTileData);

    // must be restored last
    RESTORE_MAPART_ARRAY(tilefilenum, g_bakTileFileNum);

    artUpdateManifest();
#ifdef USE_OPENGL
    //POGOTODO: review this to ensure we're not invalidating more than we have to
    gltexinvalidatetype(INVALIDATE_ART);
# ifdef POLYMER
    if (videoGetRenderMode() == REND_POLYMER)
        polymer_texinvalidate();
# endif
#endif
}

void artSetupMapArt(const char *filename)
{
    artClearMapArt();

    if (Bstrlen(filename) + 7 >= sizeof(mapartfilename))
        return;

    Bstrcpy(mapartfilename, filename);
    append_ext_UNSAFE(mapartfilename, "_XX.art");
    mapartfnXXofs = Bstrlen(mapartfilename) - 6;

    // Check for first per-map ART file: if that one doesn't exist, don't load any.
    buildvfs_kfd fil = kopen4loadfrommod(artGetIndexedFileName(MAXARTFILES_BASE), 0);

    if (fil == buildvfs_kfd_invalid)
    {
        artClearMapArtFilename();
        return;
    }

    kclose(fil);

    // Allocate backup arrays.
    ALLOC_MAPART_ARRAY(tilefilenum, g_bakTileFileNum);
    ALLOC_MAPART_ARRAY(tilefileoffs, g_bakTileFileOffs);
    ALLOC_MAPART_ARRAY(tilefilesearchfirst, g_bakTileFileSearchFirst);
    ALLOC_MAPART_ARRAY(tilesiz, g_bakTileSiz);
    ALLOC_MAPART_ARRAY(picanm, g_bakPicAnm);
    ALLOC_MAPART_ARRAY(faketile, g_bakFakeTile);
    ALLOC_MAPART_ARRAY(faketiledata, g_bakFakeTileData);
    ALLOC_MAPART_ARRAY(rottile, g_bakRottile);

    for (bssize_t i=MAXARTFILES_BASE; i<MAXARTFILES_TOTAL; i++)
    {
        int ret = artReadIndexedFile(i);

        if (ret != 0)
        {
            // NOTE: i == MAXARTFILES_BASE && ret == -1 can only have happened
            // if the file was deleted between the above file existence check
            // and now.  Very cornerly... but I like my code to be prepared to
            // any eventuality.
            if (i == MAXARTFILES_BASE || ret != -1)
                artClearMapArt();
            break;
        }
    }

    artUpdateManifest();
#ifdef USE_OPENGL
    //POGOTODO: review this to ensure we're not invalidating more than we have to
    gltexinvalidatetype(INVALIDATE_ART);
# ifdef POLYMER
    if (videoGetRenderMode() == REND_POLYMER)
        polymer_texinvalidate();
# endif
#endif
}

//
// ART loading
//

void tileSetupDummy(int32_t const tile)
{
    bitmap_set(faketile, tile);
    DO_FREE_AND_NULL(faketiledata[tile]);
}

static void tileSetDataSafe(int32_t const tile, int32_t tsiz, char const * const buffer)
{
    int const compressed_tsiz = LZ4_compressBound(tsiz);
    char * newtile = (char *) Xmalloc(compressed_tsiz);

    if ((tsiz = LZ4_compress_default(buffer, newtile, tsiz, compressed_tsiz)) != -1)
    {
        faketilesize[tile] = tsiz;
        faketiledata[tile] = (char *) Xrealloc(newtile, tsiz);
        bitmap_set(faketile, tile);
        tilefilenum[tile] = MAXARTFILES_TOTAL;
    }
    else
    {
        Xfree(newtile);
    }
}

void tileSetData(int32_t const tile, int32_t tsiz, char const * const buffer)
{
    int const compressed_tsiz = LZ4_compressBound(tsiz);
    faketiledata[tile] = (char *) Xrealloc(faketiledata[tile], compressed_tsiz);

    if ((tsiz = LZ4_compress_default(buffer, faketiledata[tile], tsiz, compressed_tsiz)) != -1)
    {
        faketilesize[tile] = tsiz;
        faketiledata[tile] = (char *) Xrealloc(faketiledata[tile], tsiz);
        bitmap_set(faketile, tile);
        tilefilenum[tile] = MAXARTFILES_TOTAL;
    }
    else
    {
        DO_FREE_AND_NULL(faketiledata[tile]);
        bitmap_clear(faketile, tile);
    }
}

static void tileSoftDelete(int32_t const tile)
{
    tilesiz[tile].x = 0;
    tilesiz[tile].y = 0;
    picsiz[tile] = 0;

    // CACHE1D_FREE
    walock[tile] = CACHE1D_FREE;
    waloff[tile] = 0;

    bitmap_clear(faketile, tile);

    Bmemset(&picanm[tile], 0, sizeof(picanm_t));
}

void tileDelete(int32_t const tile)
{
    tileSoftDelete(tile);

    DO_FREE_AND_NULL(faketiledata[tile]);

    vox_undefine(tile);

#ifdef USE_OPENGL
    for (ssize_t i=MAXPALOOKUPS-1; i>=0; --i)
        hicclearsubst(tile, i);

    md_undefinetile(tile);
#endif
}

void tileUpdatePicSiz(int32_t picnum)
{
    int j = 15;

    while ((j > 1) && (pow2long[j] > tilesiz[picnum].x))
        j--;
    picsiz[picnum] = j;

    j = 15;
    while ((j > 1) && (pow2long[j] > tilesiz[picnum].y))
        j--;
    picsiz[picnum] |= j<<4;
}

void tileSetSize(int32_t picnum, int16_t dasizx, int16_t dasizy)
{
    tilesiz[picnum].x = dasizx;
    tilesiz[picnum].y = dasizy;

    tileUpdatePicSiz(picnum);
}

int32_t artReadHeader(buildvfs_kfd const fil, char const * const fn, artheader_t * const local)
{
    int32_t artversion;
    kread(fil, &artversion, 4); artversion = B_LITTLE32(artversion);

    if (artversion == B_LITTLE32(0x4c495542))
    {
        kread(fil, &artversion, 4); artversion = B_LITTLE32(artversion);
        if (artversion == B_LITTLE32(0x54524144))
        {
            kread(fil, &artversion, 4); artversion = B_LITTLE32(artversion);
        }
        else
        {
            LOG_F(ERROR, "Unable to load %s: bad version.", fn);
            kclose(fil);
            return 1;
        }
    }

    if (artversion != 1)
    {
        LOG_F(ERROR, "Unable to load %s: bad version.", fn);
        kclose(fil);
        return 1;
    }

    int32_t numtiles_dummy;
    kread(fil, &numtiles_dummy, 4);

    kread(fil, &local->tilestart, 4); local->tilestart = B_LITTLE32(local->tilestart);
    kread(fil, &local->tileend, 4);   local->tileend   = B_LITTLE32(local->tileend);

    if (EDUKE32_PREDICT_FALSE((uint32_t) local->tilestart >= MAXUSERTILES || (uint32_t) local->tileend >= MAXUSERTILES || local->tileend < local->tilestart))
    {
        LOG_F(ERROR, "Unable to load %s: tile range is reversed, negative, or exceeds MAXTILES: file likely corrupt.", fn);
        kclose(fil);
        return 1;
    }

    local->numtiles = (local->tileend-local->tilestart+1);

    return 0;
}

int32_t artReadHeaderFromBuffer(uint8_t const * const buf, artheader_t * const local)
{
    int const artversion = B_LITTLE32(B_UNBUF32(&buf[0]));
    if (EDUKE32_PREDICT_FALSE(artversion != 1))
    {
        LOG_F(ERROR, "Unable to load art: bad version.");
        return 1;
    }

    local->tilestart = B_LITTLE32(B_UNBUF32(&buf[8]));
    local->tileend   = B_LITTLE32(B_UNBUF32(&buf[12]));

    if (EDUKE32_PREDICT_FALSE((unsigned) local->tilestart >= MAXUSERTILES || (unsigned) local->tileend >= MAXUSERTILES || local->tileend < local->tilestart))
    {
        LOG_F(ERROR, "Unable to load art: tile range is reversed, negative, or exceeds MAXTILES: data likely corrupt.");
        return 1;
    }

    local->numtiles = (local->tileend-local->tilestart+1);

    return 0;
}

int32_t artCheckUnitFileHeader(uint8_t const * const buf, int32_t length)
{
    if (EDUKE32_PREDICT_FALSE(length <= ARTv1_UNITOFFSET))
        return -1;

    artheader_t local;
    if (EDUKE32_PREDICT_FALSE(artReadHeaderFromBuffer(buf, &local) != 0))
        return -2;

    if (EDUKE32_PREDICT_FALSE(local.numtiles != 1))
        return -3;

    return 0;
}

void tileConvertAnimFormat(int32_t const picnum, uint32_t const picanmdisk)
{
    EDUKE32_STATIC_ASSERT(PICANM_ANIMTYPE_MASK == 192);

    picanm_t * const thispicanm = &picanm[picnum];
    thispicanm->num = picanmdisk&63;
    thispicanm->xofs = (picanmdisk>>8)&255;
    thispicanm->yofs = (picanmdisk>>16)&255;
    thispicanm->sf = ((picanmdisk>>24)&15) | (picanmdisk&192);
}

void artReadManifest(buildvfs_kfd const fil, artheader_t * const local)
{
    int16_t *tilesizx = (int16_t *) Xmalloc(local->numtiles * sizeof(int16_t));
    int16_t *tilesizy = (int16_t *) Xmalloc(local->numtiles * sizeof(int16_t));
    kread(fil, tilesizx, local->numtiles*sizeof(int16_t));
    kread(fil, tilesizy, local->numtiles*sizeof(int16_t));

    local->tileread = (uint8_t*)Xcalloc(1, bitmap_size(local->numtiles));

    for (bssize_t i=local->tilestart; i<=local->tileend; i++)
    {
        int32_t const localIndex = i - local->tilestart;
        int16_t const tilex = B_LITTLE16(tilesizx[localIndex]);
        int16_t const tiley = B_LITTLE16(tilesizy[localIndex]);
        int32_t const dasiz = tilex * tiley;

        uint32_t picanmdisk;
        kread(fil, &picanmdisk, sizeof(uint32_t));

        if (dasiz == 0)
            continue;

        tilesiz[i].x = tilex;
        tilesiz[i].y = tiley;

        bitmap_set(local->tileread, localIndex);

        picanmdisk = B_LITTLE32(picanmdisk);
        tileConvertAnimFormat(i, picanmdisk);
    }

    DO_FREE_AND_NULL(tilesizx);
    DO_FREE_AND_NULL(tilesizy);
}

void artPreloadFile(buildvfs_kfd const fil, artheader_t * const local)
{
    char *buffer = NULL;
    int32_t buffersize = 0;

    for (bssize_t i=local->tilestart; i<=local->tileend; i++)
    {
        int32_t const localIndex = i - local->tilestart;
        if (!bitmap_test(local->tileread, localIndex))
            continue;

        int const dasiz = tilesiz[i].x * tilesiz[i].y;
        if (dasiz == 0)
        {
            tileDelete(i);
            continue;
        }

        maybe_grow_buffer(&buffer, &buffersize, dasiz);
        kread(fil, buffer, dasiz);
        tileSetData(i, dasiz, buffer);
    }

    DO_FREE_AND_NULL(buffer);
    DO_FREE_AND_NULL(local->tileread);
}

static void artPreloadFileSafe(buildvfs_kfd const fil, artheader_t * const local)
{
    char *buffer = NULL;
    int32_t buffersize = 0;

    for (bssize_t i=local->tilestart; i<=local->tileend; i++)
    {
        int32_t const localIndex = i - local->tilestart;
        if (!bitmap_test(local->tileread, localIndex))
            continue;

        int const dasiz = tilesiz[i].x * tilesiz[i].y;
        if (dasiz == 0)
        {
            tileSoftDelete(i);
            continue;
        }

        maybe_grow_buffer(&buffer, &buffersize, dasiz);
        kread(fil, buffer, dasiz);
        tileSetDataSafe(i, dasiz, buffer);
    }

    DO_FREE_AND_NULL(buffer);
    DO_FREE_AND_NULL(local->tileread);
}

static const char *artGetIndexedFileName(int32_t tilefilei)
{
    if (tilefilei >= MAXARTFILES_BASE)
    {
        int32_t o = mapartfnXXofs;
        tilefilei -= MAXARTFILES_BASE;

        mapartfilename[o+1] = '0' + tilefilei%10;
        mapartfilename[o+0] = '0' + (tilefilei/10)%10;

        return mapartfilename;
    }
    else
    {
        artfilename[7] = '0' + tilefilei%10;
        artfilename[6] = '0' + (tilefilei/10)%10;
        artfilename[5] = '0' + (tilefilei/100)%10;

        return artfilename;
    }
}

// Returns:
//  0: successfully read ART file
// >0: error with the ART file
// -1: ART file does not exist
//<-1: per-map ART issue
static int32_t artReadIndexedFile(int32_t tilefilei)
{
    const char *fn = artGetIndexedFileName(tilefilei);
    const int32_t permap = (tilefilei >= MAXARTFILES_BASE);  // is it a per-map ART file?
    char const searchfirst = permap ? 0 : g_artBaseSearchFirst;
    buildvfs_kfd fil;

    if ((fil = kopen4load(fn, searchfirst)) != buildvfs_kfd_invalid)
    {
        artheader_t local;
        int const headerval = artReadHeader(fil, fn, &local);
        if (headerval != 0)
        {
            kclose(fil);
            return headerval;
        }

        if (permap)
        {
            // Check whether we can evict existing tiles to make place for
            // per-map ART ones.
            for (int i=local.tilestart; i<=local.tileend; i++)
            {
                // Tiles having dummytile replacements or those that are
                // cache1d-locked can't be replaced.
                if (bitmap_test(faketile, i) || walock[i] >= CACHE1D_LOCKED)
                {
                    LOG_F(WARNING, "Per-map .art file could not be loaded %s: tile %d is locked by %s.", fn, i, walock[i] >= CACHE1D_LOCKED ? "cache1d" : "dummytile");
                    kclose(fil);
                    return -3;
                }
            }

            // Free existing tiles from the cache1d. CACHE1D_FREE
            Bmemset(&waloff[local.tilestart], 0, local.numtiles*sizeof(intptr_t));
            Bmemset(&walock[local.tilestart], 1, local.numtiles*sizeof(walock[0]));
        }

        artReadManifest(fil, &local);

#ifndef USE_PHYSFS
        if (cache1d_file_fromzip(fil))
#else
        if (1)
#endif
        {
            if (permap)
                artPreloadFileSafe(fil, &local);
            else
                artPreloadFile(fil, &local);
        }
        else
        {
            int offscount = ktell(fil);

            for (bssize_t i=local.tilestart; i<=local.tileend; ++i)
            {
                int32_t const localIndex = i - local.tilestart;
                if (!bitmap_test(local.tileread, localIndex))
                    continue;

                int const dasiz = tilesiz[i].x * tilesiz[i].y;

                tilefilenum[i] = tilefilei;
                tilefileoffs[i] = offscount;
                tilefilesearchfirst[i] = searchfirst;

                offscount += dasiz;
                // artsize += ((dasiz+15)&0xfffffff0);
            }

            DO_FREE_AND_NULL(local.tileread);
        }

        if (permap)
            LOG_F(INFO, "Per-map .art file %s loaded", fn);

        kclose(fil);
        return 0;
    }

    return -1;
}

//
// loadpics
//
int32_t artLoadFiles(const char *filename, int32_t askedsize)
{
    Bstrncpyz(artfilename, filename, sizeof(artfilename));

    Bmemset(&tilesiz[0], 0, sizeof(vec2_16_t) * MAXTILES);
    Bmemset(picanm, 0, sizeof(picanm));
    Bmemset(tilefilesearchfirst, 0, sizeof(tilefilesearchfirst));

    for (auto &rot : rottile)
        rot = { -1, -1 };

    //    artsize = 0;

    for (int tilefilei=0; tilefilei<MAXARTFILES_BASE; tilefilei++)
        artReadIndexedFile(tilefilei);

    Bmemset(gotpic, 0, sizeof(gotpic));
    //cachesize = min((int32_t)((Bgetsysmemsize()/100)*60),max(artsize,askedsize));
    g_vm_size = (Bgetsysmemsize() <= (uint32_t)askedsize) ? (int32_t)((Bgetsysmemsize() / 100) * 60) : askedsize;
    g_vm_data = Xmalloc(g_vm_size);
    g_cache.initBuffer((intptr_t) g_vm_data, g_vm_size);

    artUpdateManifest();

    artfil = buildvfs_kfd_invalid;
    artfilnum = -1;
    artfilplc = 0L;

    return 0;
}

void artSetBaseSearchFirst(char searchfirst)
{
    g_artBaseSearchFirst = searchfirst;
}

char artGetBaseSearchFirst(void)
{
    return g_artBaseSearchFirst;
}

////////// Crossplay map ART remapping //////////

static constexpr int ART_CROSSPLAY_MAX_FOREIGN_DATA = 8;
static constexpr int ART_CROSSPLAY_MAX_BASE_ART_FILES = MAXARTFILES_BASE;

static char g_crossplayForeignData[ART_CROSSPLAY_MAX_FOREIGN_DATA][BMAX_PATH];
static int32_t g_crossplayForeignDataGame[ART_CROSSPLAY_MAX_FOREIGN_DATA];
static int32_t g_crossplayForeignDataCount;
static int32_t g_crossplayRuntimeGame = ART_CROSSPLAY_GAME_UNKNOWN;
static int32_t g_crossplayDomainGame[ART_CROSSPLAY_DOMAIN_COUNT] =
{
    ART_CROSSPLAY_GAME_UNKNOWN,
    ART_CROSSPLAY_GAME_UNKNOWN,
    ART_CROSSPLAY_GAME_UNKNOWN,
};
static int16_t g_crossplayTileRemap[MAXTILES];
static int16_t g_crossplayGeneratedTiles[MAXTILES];
static int32_t g_crossplayGeneratedTileCount;
static int16_t g_crossplayOriginalMapSectorCeilingPicnum[MAXSECTORS];
static int16_t g_crossplayOriginalMapSectorFloorPicnum[MAXSECTORS];
static int16_t g_crossplayOriginalMapWallPicnum[MAXWALLS];
static int16_t g_crossplayOriginalMapWallOverpicnum[MAXWALLS];
static char g_crossplayOriginalMapSprite[MAXSPRITES];
static int16_t g_crossplayOriginalMapSpritePicnum[MAXSPRITES];
static int g_crossplayPreparedMapArt;
static int g_crossplayLevelStateInitialized;
static char *g_crossplayArtCacheData[ART_CROSSPLAY_MAX_BASE_ART_FILES];
static int32_t g_crossplayArtCacheSize[ART_CROSSPLAY_MAX_BASE_ART_FILES];
static char g_crossplayArtCacheLoaded[ART_CROSSPLAY_MAX_BASE_ART_FILES];
static char g_crossplayPaletteRemapPath[BMAX_PATH];
static int g_crossplayPaletteRemapLoaded;
static int g_crossplayPaletteRemapAvailable;
static uint8_t g_crossplayPaletteRemapRuntimePalette[768];
static uint8_t g_crossplayPaletteRemap[256];

struct artCrossplayForeignTileRemap
{
    int32_t game;
    int16_t sourceTile;
    int16_t destTile;
};

static artCrossplayForeignTileRemap g_crossplayForeignTileRemap[MAXTILES];
static int32_t g_crossplayForeignTileRemapCount;

static int16_t artCrossplayRemapTile(int16_t tile);

static void artCrossplayEnsureLevelStateInitialized(void)
{
    if (g_crossplayLevelStateInitialized)
        return;

    Bmemset(g_crossplayTileRemap, 0xff, sizeof(g_crossplayTileRemap));
    Bmemset(g_crossplayOriginalMapSectorCeilingPicnum, 0xff, sizeof(g_crossplayOriginalMapSectorCeilingPicnum));
    Bmemset(g_crossplayOriginalMapSectorFloorPicnum, 0xff, sizeof(g_crossplayOriginalMapSectorFloorPicnum));
    Bmemset(g_crossplayOriginalMapWallPicnum, 0xff, sizeof(g_crossplayOriginalMapWallPicnum));
    Bmemset(g_crossplayOriginalMapWallOverpicnum, 0xff, sizeof(g_crossplayOriginalMapWallOverpicnum));
    Bmemset(g_crossplayOriginalMapSpritePicnum, 0xff, sizeof(g_crossplayOriginalMapSpritePicnum));
    g_crossplayLevelStateInitialized = 1;
}

static char const *artCrossplayBaseName(char const *path)
{
    if (!path)
        return "";

    char const *slash = Bstrrchr(path, '/');
#ifdef _WIN32
    char const *backslash = Bstrrchr(path, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif

    return slash ? slash + 1 : path;
}

static void artCrossplayGrpName(char const *path, char *out, size_t outSize)
{
    char const *base = artCrossplayBaseName(path);
    if (*base == '/')
        base++;

    Bstrncpyz(out, base, outSize);
}

static int artCrossplayIsDirectory(char const *path)
{
    if (!path || !path[0])
        return 0;

    struct Bstat st;
    return Bstat(path, &st) == 0 && (st.st_mode & S_IFDIR) == S_IFDIR;
}

static int artCrossplayDirectoryEntryPath(char const *directoryPath, char const *entryName, int basenameOnly, char *out, size_t outSize)
{
    if (!directoryPath || !directoryPath[0] || !entryName || !entryName[0])
        return 0;

    char const *name = basenameOnly ? artCrossplayBaseName(entryName) : entryName;
    while (*name == '/'
#ifdef _WIN32
           || *name == '\\'
#endif
          )
        ++name;
    if (!name[0])
        return 0;

    size_t const directoryLength = Bstrlen(directoryPath);
    char const separator =
#ifdef _WIN32
        (Bstrchr(directoryPath, '\\') && !Bstrchr(directoryPath, '/')) ? '\\' :
#endif
        '/';

    if (directoryLength > 0 && (directoryPath[directoryLength - 1] == '/'
#ifdef _WIN32
        || directoryPath[directoryLength - 1] == '\\'
#endif
        ))
        Bsnprintf(out, outSize, "%s%s", directoryPath, name);
    else
        Bsnprintf(out, outSize, "%s%c%s", directoryPath, separator, name);

    return 1;
}

static int artCrossplayReadPlainFile(char const *path, char **data, int32_t *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return 0;
    }

    long const fileSizeLong = ftell(fp);
    if (fileSizeLong < 0 || fileSizeLong > INT32_MAX || fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return 0;
    }

    int32_t const fileSize = (int32_t)fileSizeLong;
    auto *buffer = (char *)Xmalloc(fileSize ? fileSize : 1);
    int const ok = fread(buffer, 1, fileSize, fp) == (size_t)fileSize;
    fclose(fp);

    if (!ok)
    {
        Xfree(buffer);
        return 0;
    }

    *data = buffer;
    *size = fileSize;
    return 1;
}

static int artCrossplayReadDirectoryEntry(char const *directoryPath, char const *entryName, char **data, int32_t *size)
{
    if (!artCrossplayIsDirectory(directoryPath))
        return 0;

    char path[BMAX_PATH * 2];
    if (artCrossplayDirectoryEntryPath(directoryPath, entryName, 0, path, sizeof(path)) &&
        artCrossplayReadPlainFile(path, data, size))
        return 1;

    if (artCrossplayBaseName(entryName) != entryName &&
        artCrossplayDirectoryEntryPath(directoryPath, entryName, 1, path, sizeof(path)) &&
        artCrossplayReadPlainFile(path, data, size))
        return 1;

    return 0;
}

static int artCrossplayDirectoryHasFile(char const *directoryPath, char const *entryName)
{
    if (!artCrossplayIsDirectory(directoryPath))
        return 0;

    char path[BMAX_PATH * 2];
    struct Bstat st;
    if (artCrossplayDirectoryEntryPath(directoryPath, entryName, 0, path, sizeof(path)) &&
        Bstat(path, &st) == 0 && (st.st_mode & S_IFDIR) != S_IFDIR)
        return 1;

    if (artCrossplayBaseName(entryName) != entryName &&
        artCrossplayDirectoryEntryPath(directoryPath, entryName, 1, path, sizeof(path)) &&
        Bstat(path, &st) == 0 && (st.st_mode & S_IFDIR) != S_IFDIR)
        return 1;

    return 0;
}

static int artCrossplaySplitPathDirectory(char const *path, char *directory, size_t directorySize)
{
    if (!path || !path[0] || !directory || directorySize == 0)
        return 0;

    char const *slash = Bstrrchr(path, '/');
#ifdef _WIN32
    char const *backslash = Bstrrchr(path, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif

    if (!slash)
        return 0;

    size_t const length = (size_t)(slash - path);
    if (length == 0)
        Bstrncpyz(directory, "/", directorySize);
    else
    {
        size_t const copyLength = min(length, directorySize - 1);
        Bmemcpy(directory, path, copyLength);
        directory[copyLength] = 0;
    }
    return 1;
}

static int artCrossplayReadSiblingDirectoryEntry(char const *containerPath, char const *entryName, char **data, int32_t *size)
{
    char directory[BMAX_PATH * 2];
    return artCrossplaySplitPathDirectory(containerPath, directory, sizeof(directory)) &&
           artCrossplayReadDirectoryEntry(directory, entryName, data, size);
}

static int artCrossplaySiblingDirectoryHasFile(char const *containerPath, char const *entryName)
{
    char directory[BMAX_PATH * 2];
    return artCrossplaySplitPathDirectory(containerPath, directory, sizeof(directory)) &&
           artCrossplayDirectoryHasFile(directory, entryName);
}

#pragma pack(push, 1)
struct artCrossplayRffHeader
{
    char sign[4];
    int16_t version;
    int16_t pad1;
    uint32_t offset;
    uint32_t fileNum;
    int32_t pad2[4];
};

struct artCrossplayRffDictNode
{
    char unused1[16];
    uint32_t offset;
    uint32_t size;
    char unused2[8];
    uint8_t flags;
    char type[3];
    char name[8];
    int32_t id;
};
#pragma pack(pop)

enum
{
    ART_CROSSPLAY_RFF_FLAG_CRYPT = 16,
};

static void artCrossplayRffCrypt(void *data, int32_t length, uint16_t key)
{
    char *bytes = (char *)data;
    for (int32_t i = 0; i < length; ++i, ++key)
        bytes[i] ^= (key >> 1);
}

static void artCrossplayRffEntryName(char const *entryName, char *name, size_t nameSize, char *type, size_t typeSize)
{
    name[0] = 0;
    type[0] = 0;

    char const *base = artCrossplayBaseName(entryName);
    while (*base == '/'
#ifdef _WIN32
           || *base == '\\'
#endif
          )
        ++base;

    char const *dot = Bstrrchr(base, '.');
    size_t const baseLength = dot ? (size_t)(dot - base) : Bstrlen(base);
    size_t const nameLength = min(baseLength, nameSize - 1);
    Bmemcpy(name, base, nameLength);
    name[nameLength] = 0;
    Bstrupr(name);

    if (dot && dot[1])
    {
        size_t const extensionLength = min(Bstrlen(dot + 1), typeSize - 1);
        Bmemcpy(type, dot + 1, extensionLength);
        type[extensionLength] = 0;
    }
    else
    {
        Bstrncpyz(type, "MAP", typeSize);
    }
    Bstrupr(type);
}

static void artCrossplayRffNodeString(char const *source, size_t sourceSize, char *out, size_t outSize)
{
    size_t length = 0;
    while (length < sourceSize && source[length])
        ++length;
    length = min(length, outSize - 1);
    Bmemcpy(out, source, length);
    out[length] = 0;
    Bstrupr(out);
}

static int artCrossplayReadRffEntry(char const *rffPath, char const *entryName, char **data, int32_t *size)
{
    char wantedName[9], wantedType[4];
    artCrossplayRffEntryName(entryName, wantedName, sizeof(wantedName), wantedType, sizeof(wantedType));
    if (!wantedName[0] || !wantedType[0])
        return 0;

    FILE *fp = fopen(rffPath, "rb");
    if (!fp)
        return 0;

    artCrossplayRffHeader header;
    int found = 0;

    if (fread(&header, 1, sizeof(header), fp) != sizeof(header) || Bmemcmp(header.sign, "RFF\x1a", 4))
    {
        fclose(fp);
        return 0;
    }

    int16_t const version = B_LITTLE16(header.version);
    uint32_t const dictOffset = B_LITTLE32(header.offset);
    uint32_t const fileCount = B_LITTLE32(header.fileNum);
    int const encryptedDirectory = (version & 0xff00) == 0x300;
    if ((!encryptedDirectory && (version & 0xff00) != 0x200) || fileCount == 0 || fileCount > 65536)
    {
        fclose(fp);
        return 0;
    }

    uint64_t const dictSize64 = (uint64_t)fileCount * sizeof(artCrossplayRffDictNode);
    if (dictSize64 > INT32_MAX)
    {
        fclose(fp);
        return 0;
    }

    auto *dict = (artCrossplayRffDictNode *)Xmalloc((size_t)dictSize64);
    if (fseek(fp, dictOffset, SEEK_SET) != 0 || fread(dict, 1, (size_t)dictSize64, fp) != (size_t)dictSize64)
    {
        Xfree(dict);
        fclose(fp);
        return 0;
    }

    if (encryptedDirectory)
        artCrossplayRffCrypt(dict, (int32_t)dictSize64, (uint16_t)(dictOffset + (version & 0xff) * dictOffset));

    for (uint32_t i = 0; i < fileCount; ++i)
    {
        char nodeName[9], nodeType[4];
        artCrossplayRffNodeString(dict[i].name, sizeof(dict[i].name), nodeName, sizeof(nodeName));
        artCrossplayRffNodeString(dict[i].type, sizeof(dict[i].type), nodeType, sizeof(nodeType));

        if (Bstrcasecmp(nodeName, wantedName) || Bstrcasecmp(nodeType, wantedType))
            continue;

        uint32_t const entryOffset = B_LITTLE32(dict[i].offset);
        uint32_t const entrySize = B_LITTLE32(dict[i].size);
        if (entrySize > INT32_MAX)
            break;

        auto *buffer = (char *)Xmalloc(entrySize ? entrySize : 1);
        if (fseek(fp, entryOffset, SEEK_SET) == 0 && fread(buffer, 1, entrySize, fp) == entrySize)
        {
            if (dict[i].flags & ART_CROSSPLAY_RFF_FLAG_CRYPT)
                artCrossplayRffCrypt(buffer, min((int32_t)entrySize, 0x100), 0);

            *data = buffer;
            *size = (int32_t)entrySize;
            found = 1;
        }
        else
        {
            Xfree(buffer);
        }
        break;
    }

    Xfree(dict);
    fclose(fp);
    return found;
}

static int artCrossplayRffHasFile(char const *rffPath, char const *entryName)
{
    char *data = NULL;
    int32_t size = 0;
    if (!artCrossplayReadRffEntry(rffPath, entryName, &data, &size))
        return 0;

    Xfree(data);
    return 1;
}

static int artCrossplayFindGrpEntry(FILE *fp, char const *wantedName, int32_t *entryOffset, int32_t *entrySize)
{
    char header[16];
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(header, 1, sizeof(header), fp) != sizeof(header))
        return 0;

    if (Bmemcmp(header, "KenSilverman", 12))
        return 0;

    int32_t const numFiles = B_LITTLE32(B_UNBUF32(&header[12]));
    if (numFiles <= 0)
        return 0;

    int32_t dataOffset = 16 + numFiles * 16;
    for (int32_t i = 0; i < numFiles; ++i)
    {
        char entry[16];
        if (fread(entry, 1, sizeof(entry), fp) != sizeof(entry))
            return 0;

        char name[13];
        Bmemcpy(name, entry, 12);
        name[12] = 0;

        int32_t const size = B_LITTLE32(B_UNBUF32(&entry[12]));
        if (!Bstrcasecmp(name, wantedName))
        {
            *entryOffset = dataOffset;
            *entrySize = size;
            return 1;
        }

        dataOffset += size;
    }

    return 0;
}

static int artCrossplayReadVfsEntry(char const *entryName, char **data, int32_t *size)
{
    buildvfs_kfd kFile = kopen4load(entryName, 0);
    if (kFile == buildvfs_kfd_invalid)
        kFile = kopen4load(artCrossplayBaseName(entryName), 0);

    if (kFile == buildvfs_kfd_invalid)
        return 0;

    int32_t const fileSize = kfilelength(kFile);
    if (fileSize < 0)
    {
        kclose(kFile);
        return 0;
    }

    auto *buffer = (char *)Xmalloc(fileSize ? fileSize : 1);
    int const ok = kread(kFile, buffer, fileSize) == fileSize;
    kclose(kFile);

    if (!ok)
    {
        Xfree(buffer);
        return 0;
    }

    *data = buffer;
    *size = fileSize;
    return 1;
}

static int artCrossplayReadGrpEntry(char const *grpPath, char const *entryName, char **data, int32_t *size)
{
    char wanted[13];
    artCrossplayGrpName(entryName, wanted, sizeof(wanted));

    FILE *fp = fopen(grpPath, "rb");
    if (!fp)
        return 0;

    int32_t offset = 0, entrySize = 0;
    int const found = artCrossplayFindGrpEntry(fp, wanted, &offset, &entrySize);
    if (!found || entrySize < 0)
    {
        fclose(fp);
        return 0;
    }

    auto *buffer = (char *)Xmalloc(entrySize ? entrySize : 1);
    int const ok = fseek(fp, offset, SEEK_SET) == 0 && fread(buffer, 1, entrySize, fp) == (size_t)entrySize;
    fclose(fp);

    if (!ok)
    {
        Xfree(buffer);
        return 0;
    }

    *data = buffer;
    *size = entrySize;
    return 1;
}

static int artCrossplayZipNameMatches(char const *zipName, char const *entryName)
{
    if (!Bstrcasecmp(zipName, entryName))
        return 1;

    if (artCrossplayBaseName(entryName) != entryName)
        return 0;

    char const *zipBase = artCrossplayBaseName(zipName);
    return zipBase != zipName && !Bstrcasecmp(zipBase, entryName);
}

static int artCrossplayReadZipEntry(char const *zipPath, char const *entryName, char **data, int32_t *size)
{
    FILE *fp = fopen(zipPath, "rb");
    if (!fp)
        return 0;

    int found = 0;
    for (;;)
    {
        uint8_t header[30];
        if (fread(header, 1, sizeof(header), fp) != sizeof(header))
            break;

        uint32_t const signature = B_LITTLE32(B_UNBUF32(&header[0]));
        if (signature != 0x04034b50u)
            break;

        uint16_t const flags = B_LITTLE16(B_UNBUF16(&header[6]));
        uint16_t const method = B_LITTLE16(B_UNBUF16(&header[8]));
        uint32_t const compressedSize = B_LITTLE32(B_UNBUF32(&header[18]));
        uint32_t const uncompressedSize = B_LITTLE32(B_UNBUF32(&header[22]));
        uint16_t const nameLength = B_LITTLE16(B_UNBUF16(&header[26]));
        uint16_t const extraLength = B_LITTLE16(B_UNBUF16(&header[28]));

        if (flags & 8)
            break;  // Keep this conservative: data-descriptor entries need central-dir parsing.

        char *zipName = (char *)Xmalloc(nameLength + 1);
        if (fread(zipName, 1, nameLength, fp) != nameLength)
        {
            Xfree(zipName);
            break;
        }
        zipName[nameLength] = 0;

        if (fseek(fp, extraLength, SEEK_CUR))
        {
            Xfree(zipName);
            break;
        }

        int const matches = artCrossplayZipNameMatches(zipName, entryName);
        Xfree(zipName);

        if (!matches)
        {
            if (fseek(fp, compressedSize, SEEK_CUR))
                break;
            continue;
        }

        if (uncompressedSize > INT32_MAX)
            break;

        if (method == 0)
        {
            *data = (char *)Xmalloc(uncompressedSize ? uncompressedSize : 1);
            if (fread(*data, 1, uncompressedSize, fp) != uncompressedSize)
            {
                DO_FREE_AND_NULL(*data);
                break;
            }
            *size = (int32_t)uncompressedSize;
            found = 1;
            break;
        }

        if (method == 8)
        {
            char *compressed = (char *)Xmalloc(compressedSize ? compressedSize : 1);
            if (fread(compressed, 1, compressedSize, fp) != compressedSize)
            {
                Xfree(compressed);
                break;
            }

            size_t inflatedSize = 0;
            void *inflated = tinfl_decompress_mem_to_heap(compressed, compressedSize, &inflatedSize, 0);
            Xfree(compressed);

            if (!inflated || inflatedSize != uncompressedSize)
            {
                if (inflated)
                    mz_free(inflated);
                break;
            }

            *data = (char *)Xmalloc(inflatedSize ? inflatedSize : 1);
            Bmemcpy(*data, inflated, inflatedSize);
            mz_free(inflated);
            *size = (int32_t)inflatedSize;
            found = 1;
            break;
        }

        break;
    }

    fclose(fp);
    return found;
}

static int artCrossplayReadContainerEntry(char const *containerPath, char const *entryName, char **data, int32_t *size)
{
    if (artCrossplayReadDirectoryEntry(containerPath, entryName, data, size))
        return 1;

    if (artCrossplayReadGrpEntry(containerPath, entryName, data, size))
        return 1;

    if (artCrossplayReadZipEntry(containerPath, entryName, data, size))
        return 1;

    if (artCrossplayReadRffEntry(containerPath, entryName, data, size))
        return 1;

    if (artCrossplayReadSiblingDirectoryEntry(containerPath, entryName, data, size))
        return 1;

    return artCrossplayReadVfsEntry(entryName, data, size);
}

static int artCrossplayContainerHasFile(char const *containerPath, char const *entryName)
{
    if (artCrossplayDirectoryHasFile(containerPath, entryName))
        return 1;

    char wanted[13];
    artCrossplayGrpName(entryName, wanted, sizeof(wanted));

    FILE *fp = fopen(containerPath, "rb");
    if (fp)
    {
        int32_t offset = 0, size = 0;
        int const found = artCrossplayFindGrpEntry(fp, wanted, &offset, &size);
        fclose(fp);
        if (found)
            return 1;
    }

    char *zipData = NULL;
    int32_t zipSize = 0;
    if (artCrossplayReadZipEntry(containerPath, entryName, &zipData, &zipSize))
    {
        Xfree(zipData);
        return 1;
    }

    if (artCrossplayRffHasFile(containerPath, entryName))
        return 1;

    if (artCrossplaySiblingDirectoryHasFile(containerPath, entryName))
        return 1;

    buildvfs_kfd kFile = kopen4load(entryName, 0);
    if (kFile == buildvfs_kfd_invalid)
        kFile = kopen4load(artCrossplayBaseName(entryName), 0);
    if (kFile == buildvfs_kfd_invalid)
        return 0;

    kclose(kFile);
    return 1;
}

static void artCrossplayClearPaletteRemap(void)
{
    g_crossplayPaletteRemapPath[0] = 0;
    g_crossplayPaletteRemapLoaded = 0;
    g_crossplayPaletteRemapAvailable = 0;
    Bmemset(g_crossplayPaletteRemapRuntimePalette, 0, sizeof(g_crossplayPaletteRemapRuntimePalette));
    for (int32_t i = 0; i < 256; ++i)
        g_crossplayPaletteRemap[i] = (uint8_t)i;
}

static uint8_t artCrossplayPaletteByte(uint8_t value)
{
    return value > 63 ? value : (uint8_t)(value << 2);
}

static int artCrossplayReadPalette(char const *containerPath, char **data, int32_t *size)
{
    if (artCrossplayReadContainerEntry(containerPath, "palette.dat", data, size))
        return 1;

    return artCrossplayReadContainerEntry(containerPath, "PALETTE.DAT", data, size);
}

static uint8_t const *artCrossplayPaletteRemapForPath(char const *containerPath)
{
    if (!containerPath || !containerPath[0])
        return NULL;

    if (g_crossplayPaletteRemapLoaded && !Bstrcasecmp(g_crossplayPaletteRemapPath, containerPath) &&
        !Bmemcmp(g_crossplayPaletteRemapRuntimePalette, palette, sizeof(g_crossplayPaletteRemapRuntimePalette)))
        return g_crossplayPaletteRemapAvailable ? g_crossplayPaletteRemap : NULL;

    artCrossplayClearPaletteRemap();
    Bstrncpyz(g_crossplayPaletteRemapPath, containerPath, sizeof(g_crossplayPaletteRemapPath));
    Bmemcpy(g_crossplayPaletteRemapRuntimePalette, palette, sizeof(g_crossplayPaletteRemapRuntimePalette));
    g_crossplayPaletteRemapLoaded = 1;

    char *paletteData = NULL;
    int32_t paletteSize = 0;
    if (!artCrossplayReadPalette(containerPath, &paletteData, &paletteSize) || paletteSize < 768)
    {
        DO_FREE_AND_NULL(paletteData);
        return NULL;
    }

    uint8_t sourcePalette[768];
    for (int32_t i = 0; i < 768; ++i)
        sourcePalette[i] = artCrossplayPaletteByte((uint8_t)paletteData[i]);
    Xfree(paletteData);

    if (!Bmemcmp(sourcePalette, palette, sizeof(sourcePalette)))
        return NULL;

    for (int32_t i = 0; i < 255; ++i)
    {
        uint8_t const r = sourcePalette[i * 3 + 0];
        uint8_t const g = sourcePalette[i * 3 + 1];
        uint8_t const b = sourcePalette[i * 3 + 2];
        g_crossplayPaletteRemap[i] = (uint8_t)paletteGetClosestColorUpToIndexNoCache(r, g, b, 254);
    }

    // Preserve Build's masked-pixel index.  Mapping this through RGB matching
    // can turn transparent texels into visible pixels or visible pixels into
    // holes depending on the destination palette.
    g_crossplayPaletteRemap[255] = 255;
    g_crossplayPaletteRemapAvailable = 1;

    LOG_F(INFO, "Crossplay: remapping foreign ART palette from %s to the active runtime palette.", containerPath);
    return g_crossplayPaletteRemap;
}

static int artCrossplayGeneratedTileAlreadyUsed(int32_t tile)
{
    for (int32_t i = 0; i < g_crossplayGeneratedTileCount; ++i)
        if (g_crossplayGeneratedTiles[i] == tile)
            return 1;

    return 0;
}

static int artCrossplayTileUnavailable(int32_t tile)
{
    if ((unsigned)tile >= MAXUSERTILES)
        return 1;

    return tilesiz[tile].x || tilesiz[tile].y || waloff[tile] || artCrossplayGeneratedTileAlreadyUsed(tile);
}

static int16_t artCrossplayFindFreeTile(void)
{
    for (int32_t tile = MAXUSERTILES - 1; tile >= 0; --tile)
        if (!artCrossplayTileUnavailable(tile))
            return (int16_t)tile;

    return -1;
}

static int16_t artCrossplayFindFreeTileRange(int32_t startOffset, int32_t endOffset)
{
    if (startOffset > endOffset || startOffset < -63 || endOffset > 63)
        return -1;

    for (int32_t destBase = MAXUSERTILES - 1 - endOffset; destBase >= -startOffset; --destBase)
    {
        int valid = 1;

        for (int32_t offset = startOffset; offset <= endOffset; ++offset)
        {
            if (artCrossplayTileUnavailable(destBase + offset))
            {
                valid = 0;
                break;
            }
        }

        if (valid)
            return (int16_t)destBase;
    }

    return -1;
}

static void artCrossplayAnimOffsetsFromDiskPicanm(uint32_t diskPicanm, int32_t *startOffset, int32_t *endOffset)
{
    int32_t const animFrames = diskPicanm & 63;
    int32_t const animType = diskPicanm & PICANM_ANIMTYPE_MASK;

    *startOffset = 0;
    *endOffset = 0;

    if (animFrames <= 0 || animType == PICANM_ANIMTYPE_NONE)
        return;

    if (animType == PICANM_ANIMTYPE_BACK)
        *startOffset = -animFrames;
    else
        *endOffset = animFrames;
}

static int artCrossplayReadDiskPicanmFromArtBuffer(char const *buffer, int32_t bufferSize, int16_t sourceTile, uint32_t *diskPicanm)
{
    if (bufferSize < 16)
        return 0;

    int32_t headerOffset = 0;
    if (bufferSize >= 24 && !Bmemcmp(buffer, "BUILDART", 8))
        headerOffset = 8;

    int32_t const artVersion = B_LITTLE32(B_UNBUF32(&buffer[headerOffset]));
    if (artVersion != 1)
        return 0;

    int32_t const tileStart = B_LITTLE32(B_UNBUF32(&buffer[headerOffset + 8]));
    int32_t const tileEnd = B_LITTLE32(B_UNBUF32(&buffer[headerOffset + 12]));
    if (sourceTile < tileStart || sourceTile > tileEnd || tileStart < 0 || tileEnd < tileStart || tileEnd >= MAXUSERTILES)
        return 0;

    int32_t const numTiles = tileEnd - tileStart + 1;
    int32_t const xofs = headerOffset + 16;
    int32_t const yofs = xofs + numTiles * (int32_t)sizeof(int16_t);
    int32_t const anmofs = yofs + numTiles * (int32_t)sizeof(int16_t);
    int32_t const dataofs = anmofs + numTiles * (int32_t)sizeof(uint32_t);
    if (dataofs > bufferSize)
        return 0;

    int32_t const local = sourceTile - tileStart;
    *diskPicanm = B_LITTLE32(B_UNBUF32(&buffer[anmofs + local * 4]));
    return 1;
}

static int artCrossplayCopyTileFromArtBuffer(char const *buffer, int32_t bufferSize, int16_t sourceTile, int16_t destTile,
                                             uint8_t const *paletteRemap)
{
    if (bufferSize < 16)
        return 0;

    int32_t headerOffset = 0;
    if (bufferSize >= 24 && !Bmemcmp(buffer, "BUILDART", 8))
        headerOffset = 8;

    int32_t const artVersion = B_LITTLE32(B_UNBUF32(&buffer[headerOffset]));
    if (artVersion != 1)
        return 0;

    int32_t const tileStart = B_LITTLE32(B_UNBUF32(&buffer[headerOffset + 8]));
    int32_t const tileEnd = B_LITTLE32(B_UNBUF32(&buffer[headerOffset + 12]));
    if (sourceTile < tileStart || sourceTile > tileEnd || tileStart < 0 || tileEnd < tileStart || tileEnd >= MAXUSERTILES)
        return 0;

    int32_t const numTiles = tileEnd - tileStart + 1;
    int32_t const xofs = headerOffset + 16;
    int32_t const yofs = xofs + numTiles * (int32_t)sizeof(int16_t);
    int32_t const anmofs = yofs + numTiles * (int32_t)sizeof(int16_t);
    int32_t const dataofs = anmofs + numTiles * (int32_t)sizeof(uint32_t);
    if (dataofs > bufferSize)
        return 0;

    int32_t pixelOffset = dataofs;
    for (int32_t tile = tileStart; tile <= tileEnd; ++tile)
    {
        int32_t const local = tile - tileStart;
        int16_t const xsiz = B_LITTLE16(B_UNBUF16(&buffer[xofs + local * 2]));
        int16_t const ysiz = B_LITTLE16(B_UNBUF16(&buffer[yofs + local * 2]));
        int32_t const dasiz = (xsiz > 0 && ysiz > 0) ? xsiz * ysiz : 0;

        if (tile == sourceTile)
        {
            if (xsiz <= 0 || ysiz <= 0 || dasiz <= 0 || pixelOffset + dasiz > bufferSize)
                return 0;

            tileSetSize(destTile, xsiz, ysiz);
            if (paletteRemap)
            {
                char *remappedPixels = (char *)Xmalloc(dasiz);
                for (int32_t pixel = 0; pixel < dasiz; ++pixel)
                    remappedPixels[pixel] = (char)paletteRemap[(uint8_t)buffer[pixelOffset + pixel]];
                tileSetData(destTile, dasiz, remappedPixels);
                Xfree(remappedPixels);
            }
            else
            {
                tileSetData(destTile, dasiz, &buffer[pixelOffset]);
            }
            Bmemset(&picanm[destTile], 0, sizeof(picanm_t));
            tileConvertAnimFormat(destTile, B_LITTLE32(B_UNBUF32(&buffer[anmofs + local * 4])));
            return 1;
        }

        pixelOffset += dasiz;
        if (pixelOffset > bufferSize)
            return 0;
    }

    return 0;
}

static void artCrossplayClearArtCache(void)
{
    for (int32_t i = 0; i < ART_CROSSPLAY_MAX_BASE_ART_FILES; ++i)
        DO_FREE_AND_NULL(g_crossplayArtCacheData[i]);

    Bmemset(g_crossplayArtCacheSize, 0, sizeof(g_crossplayArtCacheSize));
    Bmemset(g_crossplayArtCacheLoaded, 0, sizeof(g_crossplayArtCacheLoaded));
}

static int artCrossplayReadArtFileCached(char const *grpPath, int32_t artIndex, char const **data, int32_t *size)
{
    if ((unsigned)artIndex >= ART_CROSSPLAY_MAX_BASE_ART_FILES)
        return 0;

    if (!g_crossplayArtCacheLoaded[artIndex])
    {
        char artName[13];
        Bsprintf(artName, "TILES%03d.ART", artIndex);

        g_crossplayArtCacheLoaded[artIndex] = 1;
        artCrossplayReadContainerEntry(grpPath, artName, &g_crossplayArtCacheData[artIndex], &g_crossplayArtCacheSize[artIndex]);
    }

    if (!g_crossplayArtCacheData[artIndex])
        return 0;

    *data = g_crossplayArtCacheData[artIndex];
    *size = g_crossplayArtCacheSize[artIndex];
    return 1;
}

static int artCrossplayLoadForeignTile(char const *grpPath, int16_t sourceTile)
{
    if ((unsigned)sourceTile >= MAXUSERTILES)
        return 0;

    if (g_crossplayTileRemap[sourceTile] >= 0)
        return 1;

    uint8_t const *paletteRemap = artCrossplayPaletteRemapForPath(grpPath);
    int32_t preferred = sourceTile / 256;
    for (int32_t pass = 0; pass < ART_CROSSPLAY_MAX_BASE_ART_FILES + 1; ++pass)
    {
        int32_t const artIndex = pass == 0 ? preferred : pass - 1;
        if (pass > 0 && artIndex == preferred)
            continue;

        char const *artData = NULL;
        int32_t artSize = 0;
        if (!artCrossplayReadArtFileCached(grpPath, artIndex, &artData, &artSize))
            continue;

        uint32_t diskPicanm = 0;
        if (!artCrossplayReadDiskPicanmFromArtBuffer(artData, artSize, sourceTile, &diskPicanm))
            continue;

        int32_t startOffset = 0, endOffset = 0;
        artCrossplayAnimOffsetsFromDiskPicanm(diskPicanm, &startOffset, &endOffset);

        int32_t const frameCount = endOffset - startOffset + 1;
        if (frameCount <= 0 || g_crossplayGeneratedTileCount + frameCount > (int32_t)ARRAY_SIZE(g_crossplayGeneratedTiles))
            return 0;

        int sourceRangeValid = 1;
        for (int32_t offset = startOffset; offset <= endOffset; ++offset)
        {
            if ((unsigned)(sourceTile + offset) >= MAXUSERTILES)
            {
                sourceRangeValid = 0;
                break;
            }
        }
        if (!sourceRangeValid)
            continue;

        {
            int16_t const destBase = artCrossplayFindFreeTileRange(startOffset, endOffset);
            if (destBase < 0)
                return 0;

            int16_t copiedTiles[128];
            int32_t copiedCount = 0;
            int copiedAll = 1;

            for (int32_t offset = startOffset; offset <= endOffset; ++offset)
            {
                int16_t const srcFrame = (int16_t)(sourceTile + offset);
                int16_t const dstFrame = (int16_t)(destBase + offset);

                if (!artCrossplayCopyTileFromArtBuffer(artData, artSize, srcFrame, dstFrame, paletteRemap))
                {
                    copiedAll = 0;
                    break;
                }

                copiedTiles[copiedCount++] = dstFrame;
            }

            if (!copiedAll)
            {
                while (copiedCount > 0)
                    tileDelete(copiedTiles[--copiedCount]);

                continue;
            }

            for (int32_t offset = startOffset; offset <= endOffset; ++offset)
            {
                int16_t const srcFrame = (int16_t)(sourceTile + offset);
                int16_t const dstFrame = (int16_t)(destBase + offset);
                g_crossplayTileRemap[srcFrame] = dstFrame;
                g_crossplayGeneratedTiles[g_crossplayGeneratedTileCount++] = dstFrame;
            }

            return 1;
        }
    }

    return 0;
}

static int artCrossplayCopyForeignTileFromPath(char const *grpPath, int16_t sourceTile, int16_t destTile)
{
    uint8_t const *paletteRemap = artCrossplayPaletteRemapForPath(grpPath);
    int32_t preferred = sourceTile / 256;
    for (int32_t pass = 0; pass < ART_CROSSPLAY_MAX_BASE_ART_FILES + 1; ++pass)
    {
        int32_t const artIndex = pass == 0 ? preferred : pass - 1;
        if (pass > 0 && artIndex == preferred)
            continue;

        char const *artData = NULL;
        int32_t artSize = 0;
        if (!artCrossplayReadArtFileCached(grpPath, artIndex, &artData, &artSize))
            continue;

        if (artCrossplayCopyTileFromArtBuffer(artData, artSize, sourceTile, destTile, paletteRemap))
            return 1;
    }

    return 0;
}

int16_t artCrossplayLoadForeignGameTile(int32_t game, int16_t sourceTile)
{
    artCrossplayEnsureLevelStateInitialized();

    if ((unsigned)sourceTile >= MAXUSERTILES || game < 0)
        return -1;

    for (int32_t i = 0; i < g_crossplayForeignTileRemapCount; ++i)
        if (g_crossplayForeignTileRemap[i].game == game && g_crossplayForeignTileRemap[i].sourceTile == sourceTile)
            return g_crossplayForeignTileRemap[i].destTile;

    if (g_crossplayForeignTileRemapCount >= (int32_t)ARRAY_SIZE(g_crossplayForeignTileRemap) ||
        g_crossplayGeneratedTileCount >= (int32_t)ARRAY_SIZE(g_crossplayGeneratedTiles))
        return -1;

    int16_t const destTile = artCrossplayFindFreeTile();
    if (destTile < 0)
        return -1;

    for (int32_t pass = 0; pass < 2; ++pass)
    {
        for (int32_t i = 0; i < g_crossplayForeignDataCount; ++i)
        {
            int32_t const dataGame = g_crossplayForeignDataGame[i];
            if (pass == 0)
            {
                if (dataGame != game)
                    continue;
            }
            else if (dataGame >= 0)
                continue;

            artCrossplayClearArtCache();
            if (!artCrossplayCopyForeignTileFromPath(g_crossplayForeignData[i], sourceTile, destTile))
                continue;

            artCrossplayClearArtCache();

            g_crossplayGeneratedTiles[g_crossplayGeneratedTileCount++] = destTile;
            g_crossplayForeignTileRemap[g_crossplayForeignTileRemapCount++] = { game, sourceTile, destTile };
            return destTile;
        }
    }

    artCrossplayClearArtCache();
    return -1;
}

static void artCrossplayMarkTile(char *usedTiles, int32_t tile)
{
    if ((unsigned)tile < MAXUSERTILES)
        usedTiles[tile] = 1;
}

static int artCrossplayIsDukeLikeGame(int32_t game)
{
    switch (game)
    {
    case ART_CROSSPLAY_GAME_DUKE:
    case ART_CROSSPLAY_GAME_WORLDTOUR:
    case ART_CROSSPLAY_GAME_NAM:
    case ART_CROSSPLAY_GAME_NAPALM:
    case ART_CROSSPLAY_GAME_WW2GI:
        return 1;
    default:
        return 0;
    }
}

struct artCrossplayDukeSkyDef
{
    int16_t baseTile;
    int8_t lognumtiles;
    int32_t horizfrac;
    int8_t sourceTileOfs[MAXPSKYTILES];
};

static artCrossplayDukeSkyDef const g_crossplayDukeSkyDefs[] =
{
    { 78, 3, 65536, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 80, 3, 32768, { 0, 2, 3, 0, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 84, 3, 32768, { 0, 0, 4, 0, 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 89, 3, 17408, { 1, 2, 1, 3, 4, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0 } },
};

static int artCrossplayDukeMapUsesSky(char const *usedTiles, int16_t baseTile)
{
    if ((unsigned)baseTile >= MAXUSERTILES || !usedTiles[baseTile])
        return 0;

    for (int32_t i = 0; i < numsectors; ++i)
    {
        if ((sector[i].ceilingstat & 1) && sector[i].ceilingpicnum == baseTile)
            return 1;
        if ((sector[i].floorstat & 1) && sector[i].floorpicnum == baseTile)
            return 1;
    }

    return 0;
}

static int artCrossplayLoadDukeSkyTile(char const *grpPath, int16_t sourceTile)
{
    return (unsigned)sourceTile < MAXUSERTILES && artCrossplayLoadForeignTile(grpPath, sourceTile);
}

static void artCrossplaySetupDukeSky(char const *grpPath, char const *usedTiles)
{
    if (!artCrossplayIsDukeLikeGame(artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS)))
        return;

    psky_t *defaultsky = tileSetupSky(DEFAULTPSKY);
    defaultsky->lognumtiles = 3;
    defaultsky->horizfrac = 32768;
    defaultsky->yscale = 65536;

    for (auto const &skyDef : g_crossplayDukeSkyDefs)
    {
        if (!artCrossplayDukeMapUsesSky(usedTiles, skyDef.baseTile))
            continue;

        int const numTiles = 1 << skyDef.lognumtiles;
        int loaded = artCrossplayLoadDukeSkyTile(grpPath, skyDef.baseTile);

        for (int i = 0; i < numTiles; ++i)
            loaded += artCrossplayLoadDukeSkyTile(grpPath, skyDef.baseTile + skyDef.sourceTileOfs[i]);

        int16_t const remappedBase = artCrossplayRemapTile(skyDef.baseTile);
        if (!loaded || remappedBase == skyDef.baseTile)
            continue;

        psky_t *sky = tileSetupSky(remappedBase);
        Bmemset(sky, 0, sizeof(*sky));
        sky->lognumtiles = skyDef.lognumtiles;
        sky->horizfrac = skyDef.horizfrac;
        sky->yscale = 65536;

        int unsupportedOffsets = 0;
        for (int i = 0; i < numTiles; ++i)
        {
            int16_t const sourceTile = skyDef.baseTile + skyDef.sourceTileOfs[i];
            int16_t const remappedTile = artCrossplayRemapTile(sourceTile);
            int32_t const offset = remappedTile - remappedBase;

            if (offset < -128 || offset > 127)
            {
                unsupportedOffsets = 1;
                continue;
            }

            sky->tileofs[i] = (int8_t)offset;
        }

        if (unsupportedOffsets)
            LOG_F(WARNING, "Crossplay: Duke sky %d uses generated tiles too far apart for Build psky offsets.", skyDef.baseTile);
        else
            LOG_F(INFO, "Crossplay: bridged Duke parallax sky tile %d to generated tile %d.", skyDef.baseTile, remappedBase);
    }
}

static char const *artCrossplayFindForeignDataForMap(char const *mapName)
{
    if (g_crossplayForeignDataCount <= 0 || !mapName || !mapName[0])
        return NULL;

    int32_t const levelsGame = artCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS);
    if (levelsGame < 0 || levelsGame == artCrossplayRuntimeGame())
        return NULL;

    int32_t const passCount = levelsGame >= 0 ? 2 : 1;

    for (int32_t pass = 0; pass < passCount; ++pass)
    {
        for (int32_t i = 0; i < g_crossplayForeignDataCount; ++i)
        {
            if (levelsGame >= 0)
            {
                int32_t const dataGame = g_crossplayForeignDataGame[i];
                if (pass == 0)
                {
                    if (dataGame != levelsGame)
                        continue;
                }
                else if (dataGame >= 0)
                    continue;
            }

            if (artCrossplayContainerHasFile(g_crossplayForeignData[i], mapName))
                return g_crossplayForeignData[i];
        }
    }

    return NULL;
}

static int16_t artCrossplayRemapTile(int16_t tile)
{
    artCrossplayEnsureLevelStateInitialized();

    if ((unsigned)tile < MAXTILES && g_crossplayTileRemap[tile] >= 0)
        return g_crossplayTileRemap[tile];

    return tile;
}

void artCrossplaySetGameDomains(int32_t runtimeGame, int32_t levelsGame, int32_t itemsGame, int32_t weaponsGame)
{
    g_crossplayRuntimeGame = runtimeGame;
    g_crossplayDomainGame[ART_CROSSPLAY_DOMAIN_LEVELS] = levelsGame;
    g_crossplayDomainGame[ART_CROSSPLAY_DOMAIN_ITEMS] = itemsGame;
    g_crossplayDomainGame[ART_CROSSPLAY_DOMAIN_WEAPONS] = weaponsGame;
}

int32_t artCrossplayRuntimeGame(void)
{
    return g_crossplayRuntimeGame;
}

int32_t artCrossplayDomainGame(int32_t domain)
{
    if ((unsigned)domain >= ART_CROSSPLAY_DOMAIN_COUNT)
        return ART_CROSSPLAY_GAME_UNKNOWN;

    return g_crossplayDomainGame[domain];
}

int32_t artCrossplayEffectiveDomainGame(int32_t domain)
{
    int32_t const configured = artCrossplayDomainGame(domain);
    return configured >= 0 ? configured : artCrossplayRuntimeGame();
}

void artCrossplayClearForeignData(void)
{
    g_crossplayForeignDataCount = 0;
    Bmemset(g_crossplayForeignData, 0, sizeof(g_crossplayForeignData));
    for (int32_t i = 0; i < ART_CROSSPLAY_MAX_FOREIGN_DATA; ++i)
        g_crossplayForeignDataGame[i] = ART_CROSSPLAY_GAME_UNKNOWN;
    artCrossplayClearPaletteRemap();
}

int32_t artCrossplayAddForeignData(char const *path)
{
    return artCrossplayAddForeignGameData(path, ART_CROSSPLAY_GAME_UNKNOWN);
}

int32_t artCrossplayAddForeignGameData(char const *path, int32_t game)
{
    if (!path || !path[0] || g_crossplayForeignDataCount >= ART_CROSSPLAY_MAX_FOREIGN_DATA)
        return 0;

    for (int32_t i = 0; i < g_crossplayForeignDataCount; ++i)
        if (!Bstrcasecmp(g_crossplayForeignData[i], path))
        {
            if (g_crossplayForeignDataGame[i] < 0 && game >= 0)
                g_crossplayForeignDataGame[i] = game;
            return 0;
        }

    if (g_crossplayForeignDataCount > 0)
    {
        Bmemmove(&g_crossplayForeignData[1], &g_crossplayForeignData[0],
                 (size_t)g_crossplayForeignDataCount * sizeof(g_crossplayForeignData[0]));
        Bmemmove(&g_crossplayForeignDataGame[1], &g_crossplayForeignDataGame[0],
                 (size_t)g_crossplayForeignDataCount * sizeof(g_crossplayForeignDataGame[0]));
    }

    Bstrncpyz(g_crossplayForeignData[0], path, BMAX_PATH);
    g_crossplayForeignDataGame[0] = game;
    ++g_crossplayForeignDataCount;
    return 1;
}

int32_t artCrossplayForeignDataCount(void)
{
    return g_crossplayForeignDataCount;
}

int32_t artCrossplayHasForeignGameData(int32_t game)
{
    for (int32_t i = 0; i < g_crossplayForeignDataCount; ++i)
        if (game < 0 || g_crossplayForeignDataGame[i] == game)
            return 1;

    return 0;
}

static int32_t artCrossplayReadForeignDataEntryForGame(int32_t game, char const *entryName, char **data, int32_t *size,
                                                       char *sourcePath, size_t sourcePathSize)
{
    if (!entryName || !entryName[0] || !data || !size)
        return 0;

    int32_t const passCount = game >= 0 ? 2 : 1;

    for (int32_t pass = 0; pass < passCount; ++pass)
    {
        for (int32_t i = 0; i < g_crossplayForeignDataCount; ++i)
        {
            int32_t const dataGame = g_crossplayForeignDataGame[i];

            if (game >= 0)
            {
                if (pass == 0)
                {
                    if (dataGame != game)
                        continue;
                }
                else if (dataGame >= 0)
                    continue;
            }

            if (!artCrossplayReadContainerEntry(g_crossplayForeignData[i], entryName, data, size))
                continue;

            if (sourcePath && sourcePathSize > 0)
                Bstrncpyz(sourcePath, g_crossplayForeignData[i], sourcePathSize);
            return 1;
        }
    }

    return 0;
}

int32_t artCrossplayReadForeignDataEntry(char const *entryName, char **data, int32_t *size, char *sourcePath, size_t sourcePathSize)
{
    int32_t const levelsGame = artCrossplayDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS);
    return artCrossplayReadForeignDataEntryForGame(levelsGame, entryName, data, size, sourcePath, sourcePathSize);
}

int32_t artCrossplayReadForeignGameDataEntry(int32_t game, char const *entryName, char **data, int32_t *size,
                                             char *sourcePath, size_t sourcePathSize)
{
    return artCrossplayReadForeignDataEntryForGame(game, entryName, data, size, sourcePath, sourcePathSize);
}

void artCrossplayResetLevel(void)
{
    artCrossplayEnsureLevelStateInitialized();

    for (int32_t i = 0; i < g_crossplayGeneratedTileCount; ++i)
        tileDelete(g_crossplayGeneratedTiles[i]);

    g_crossplayGeneratedTileCount = 0;
    g_crossplayForeignTileRemapCount = 0;
    g_crossplayPreparedMapArt = 0;
    Bmemset(g_crossplayTileRemap, 0xff, sizeof(g_crossplayTileRemap));
    Bmemset(g_crossplayOriginalMapSectorCeilingPicnum, 0xff, sizeof(g_crossplayOriginalMapSectorCeilingPicnum));
    Bmemset(g_crossplayOriginalMapSectorFloorPicnum, 0xff, sizeof(g_crossplayOriginalMapSectorFloorPicnum));
    Bmemset(g_crossplayOriginalMapWallPicnum, 0xff, sizeof(g_crossplayOriginalMapWallPicnum));
    Bmemset(g_crossplayOriginalMapWallOverpicnum, 0xff, sizeof(g_crossplayOriginalMapWallOverpicnum));
    Bmemset(g_crossplayOriginalMapSprite, 0, sizeof(g_crossplayOriginalMapSprite));
    Bmemset(g_crossplayOriginalMapSpritePicnum, 0xff, sizeof(g_crossplayOriginalMapSpritePicnum));
    artCrossplayClearArtCache();
    artCrossplayClearPaletteRemap();
}

void artCrossplayCaptureMapSprites(void)
{
    artCrossplayEnsureLevelStateInitialized();

    Bmemset(g_crossplayOriginalMapSectorCeilingPicnum, 0xff, sizeof(g_crossplayOriginalMapSectorCeilingPicnum));
    Bmemset(g_crossplayOriginalMapSectorFloorPicnum, 0xff, sizeof(g_crossplayOriginalMapSectorFloorPicnum));
    Bmemset(g_crossplayOriginalMapWallPicnum, 0xff, sizeof(g_crossplayOriginalMapWallPicnum));
    Bmemset(g_crossplayOriginalMapWallOverpicnum, 0xff, sizeof(g_crossplayOriginalMapWallOverpicnum));
    Bmemset(g_crossplayOriginalMapSprite, 0, sizeof(g_crossplayOriginalMapSprite));
    Bmemset(g_crossplayOriginalMapSpritePicnum, 0xff, sizeof(g_crossplayOriginalMapSpritePicnum));

    for (int32_t i = 0; i < numsectors; ++i)
    {
        g_crossplayOriginalMapSectorCeilingPicnum[i] = sector[i].ceilingpicnum;
        g_crossplayOriginalMapSectorFloorPicnum[i] = sector[i].floorpicnum;
    }

    for (int32_t i = 0; i < numwalls; ++i)
    {
        g_crossplayOriginalMapWallPicnum[i] = wall[i].picnum;
        g_crossplayOriginalMapWallOverpicnum[i] = wall[i].overpicnum;
    }

    for (int32_t i = 0; i < MAXSPRITES; ++i)
        if (sprite[i].statnum != MAXSTATUS)
        {
            g_crossplayOriginalMapSprite[i] = 1;
            g_crossplayOriginalMapSpritePicnum[i] = sprite[i].picnum;
        }
}

int32_t artCrossplayIsOriginalMapSprite(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return 0;

    return g_crossplayOriginalMapSprite[spriteNum] != 0;
}

int16_t artCrossplayOriginalMapSpritePicnum(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || !g_crossplayOriginalMapSprite[spriteNum])
        return -1;

    return g_crossplayOriginalMapSpritePicnum[spriteNum];
}

int16_t artCrossplayOriginalMapSectorCeilingPicnum(int32_t sectorNum)
{
    if ((unsigned)sectorNum >= MAXSECTORS)
        return -1;

    return g_crossplayOriginalMapSectorCeilingPicnum[sectorNum];
}

int16_t artCrossplayOriginalMapSectorFloorPicnum(int32_t sectorNum)
{
    if ((unsigned)sectorNum >= MAXSECTORS)
        return -1;

    return g_crossplayOriginalMapSectorFloorPicnum[sectorNum];
}

int16_t artCrossplayOriginalMapWallPicnum(int32_t wallNum)
{
    if ((unsigned)wallNum >= MAXWALLS)
        return -1;

    return g_crossplayOriginalMapWallPicnum[wallNum];
}

int16_t artCrossplayOriginalMapWallOverpicnum(int32_t wallNum)
{
    if ((unsigned)wallNum >= MAXWALLS)
        return -1;

    return g_crossplayOriginalMapWallOverpicnum[wallNum];
}

int16_t artCrossplayRemapMapTile(int16_t tile)
{
    return artCrossplayRemapTile(tile);
}

int32_t artCrossplayPrepareForeignMapArt(char const *mapName)
{
    artCrossplayEnsureLevelStateInitialized();

    char const *grpPath = artCrossplayFindForeignDataForMap(mapName);
    if (!grpPath)
        return 0;

    artCrossplayClearArtCache();

    char *usedTiles = (char *)Xcalloc(MAXUSERTILES, sizeof(char));

    for (int32_t i = 0; i < numsectors; ++i)
    {
        artCrossplayMarkTile(usedTiles, sector[i].ceilingpicnum);
        artCrossplayMarkTile(usedTiles, sector[i].floorpicnum);
    }

    for (int32_t i = 0; i < numwalls; ++i)
    {
        artCrossplayMarkTile(usedTiles, wall[i].picnum);
        artCrossplayMarkTile(usedTiles, wall[i].overpicnum);
    }

    for (int32_t i = 0; i < MAXSPRITES; ++i)
        if (g_crossplayOriginalMapSprite[i] && sprite[i].statnum != MAXSTATUS)
            artCrossplayMarkTile(usedTiles, sprite[i].picnum);

    artCrossplaySetupDukeSky(grpPath, usedTiles);

    int32_t copied = 0, missing = 0;
    for (int32_t tile = 0; tile < MAXUSERTILES; ++tile)
    {
        if (!usedTiles[tile])
            continue;

        if (artCrossplayLoadForeignTile(grpPath, (int16_t)tile))
            copied++;
        else
            missing++;
    }

    Xfree(usedTiles);
    artCrossplayClearArtCache();

    if (copied > 0)
    {
        char missingText[64] = "";
        if (missing)
            Bsprintf(missingText, "; missing %d", missing);

        g_crossplayPreparedMapArt = 1;
        LOG_F(INFO, "Crossplay: remapped %d foreign map tile(s) from %s%s.",
              copied, grpPath, missingText);
    }
    else if (missing > 0)
        LOG_F(WARNING, "Crossplay: no foreign map ART tiles could be loaded from %s.", grpPath);

    return copied;
}

int32_t artCrossplayMapArtPrepared(void)
{
    return g_crossplayPreparedMapArt;
}

void artCrossplayApplyMapTileRemap(void)
{
    if (!g_crossplayPreparedMapArt)
        return;

    for (int32_t i = 0; i < numsectors; ++i)
    {
        sector[i].ceilingpicnum = artCrossplayRemapTile(sector[i].ceilingpicnum);
        sector[i].floorpicnum = artCrossplayRemapTile(sector[i].floorpicnum);
    }

    for (int32_t i = 0; i < numwalls; ++i)
    {
        wall[i].picnum = artCrossplayRemapTile(wall[i].picnum);
        wall[i].overpicnum = artCrossplayRemapTile(wall[i].overpicnum);
    }

    for (int32_t i = 0; i < MAXSPRITES; ++i)
        if (g_crossplayOriginalMapSprite[i] && sprite[i].statnum != MAXSTATUS)
            sprite[i].picnum = artCrossplayRemapTile(sprite[i].picnum);
}


//
// loadtile
//
static void tilePostLoad(int16_t tilenume);

// Duke Nukem 64 (Reality) supplies its own tile decompressor via this callback;
// nullptr and duke64==0 for every other game, so the normal path runs unchanged.
bool (*rt_tileload_callback)(int16_t tileNum) = nullptr;

bool tileLoad(int16_t tileNum)
{
    if ((unsigned) tileNum >= (unsigned) MAXTILES) return 0;
    int const dasiz = tilesiz[tileNum].x*tilesiz[tileNum].y;
    if (dasiz <= 0) return 0;

    // Allocate storage if necessary.
    if (waloff[tileNum] == 0)
    {
        walock[tileNum] = CACHE1D_UNLOCKED;
        g_cache.allocateBlock(&waloff[tileNum], dasiz, &walock[tileNum]);
    }

    if (!duke64 || !rt_tileload_callback || !rt_tileload_callback(tileNum))
    tileLoadData(tileNum, dasiz, (char *) waloff[tileNum]);

#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST &&
        in3dmode())
    {
        //POGOTODO: this type stuff won't be necessary down the line -- review this
        int type;
        for (type = 0; type <= 1; ++type)
        {
            gltexinvalidate(tileNum, 0, (type ? DAMETH_CLAMPED : DAMETH_MASK) | DAMETH_INDEXED);
            texcache_fetch(tileNum, 0, 0, (type ? DAMETH_CLAMPED : DAMETH_MASK) | DAMETH_INDEXED);
        }
    }
#endif

    tilePostLoad(tileNum);

    return (waloff[tileNum] != 0 && tilesiz[tileNum].x > 0 && tilesiz[tileNum].y > 0);
}

void tileMaybeRotate(int16_t tilenume)
{
    auto &rot = rottile[tilenume];
    auto &siz = tilesiz[rot.owner];

    auto src = (char *)waloff[rot.owner];
    auto dst = (char *)waloff[tilenume];

    // the engine has a squarerotatetile() we could call, but it mirrors at the same time
    for (int x = 0; x < siz.x; ++x)
    {
        int const xofs = siz.x - x - 1;
        int const yofs = siz.y * x;

        for (int y = 0; y < siz.y; ++y)
            *(dst + y * siz.x + xofs) = *(src + y + yofs);
    }

    tileSetSize(tilenume, siz.y, siz.x);
}

void tileLoadData(int16_t tilenume, int32_t dasiz, char *buffer)
{
    int const owner = rottile[tilenume].owner;

    if (owner != -1)
    {
        if (!waloff[owner])
            tileLoad(owner);

        if (waloff[owner] && waloff[tilenume])
            tileMaybeRotate(tilenume);

        return;
    }

    int const tfn = tilefilenum[tilenume];

    // dummy tiles for highres replacements and tilefromtexture definitions
    if (bitmap_test(faketile, tilenume))
    {
        if (faketiledata[tilenume] != NULL)
            LZ4_decompress_safe(faketiledata[tilenume], buffer, faketilesize[tilenume], dasiz);

        faketimerhandler();
        return;
    }

    // Potentially switch open ART file.
    if (tfn != artfilnum)
    {
        if (artfil != buildvfs_kfd_invalid)
            kclose(artfil);

        char const *fn = artGetIndexedFileName(tfn);

        artfil = kopen4loadfrommod(fn, tilefilesearchfirst[tilenume]);

        if (artfil == buildvfs_kfd_invalid)
        {
            LOG_F(ERROR, "Failed opening .art file %s!", fn);
            engineUnInit();
            Bexit(EXIT_FAILURE);
        }

        artfilnum = tfn;
        artfilplc = 0L;

        faketimerhandler();
    }

    // Seek to the right position.
    if (artfilplc != tilefileoffs[tilenume])
    {
        klseek(artfil, tilefileoffs[tilenume], BSEEK_SET);
        faketimerhandler();
    }

    kread(artfil, buffer, dasiz);
    faketimerhandler();
    artfilplc = tilefileoffs[tilenume]+dasiz;
}

static void tilePostLoad(int16_t tilenume)
{
#if !defined DEBUG_TILESIZY_512 && !defined DEBUG_TILEOFFSETS
    UNREFERENCED_PARAMETER(tilenume);
#endif
#ifdef DEBUG_TILESIZY_512
    if (tilesizy[tilenume] >= 512)
    {
        int32_t i;
        char *p = (char *) waloff[tilenume];
        for (i=0; i<tilesizx[tilenume]*tilesizy[tilenume]; i++)
            p[i] = i;
    }
#endif
#ifdef DEBUG_TILEOFFSETS
    // Add some dark blue marker lines to STEAM and CEILINGSTEAM.
    // See test_tileoffsets.map.
    if (tilenume==1250 || tilenume==1255)
    {
        char *p = (char *) waloff[tilenume];
        p[0] = p[1] = p[2] = p[3] = 254;
    }

    // Add some offset to the cocktail glass neon sign. It's more asymmetric
    // than the steam, and thus more suited to debugging the spatial
    // orientation of drawn sprites.
    if (tilenume==1008)
    {
        picanm[tilenume].xofs = 8;
        picanm[tilenume].yofs = 12;
    }
#endif
}

int32_t tileGetCRC32(int16_t tileNum)
{
    if ((unsigned)tileNum >= (unsigned)MAXTILES)
        return 0;
    int const dasiz = tilesiz[tileNum].x * tilesiz[tileNum].y;
    if (dasiz <= 0)
        return 0;

    auto data = (char *)Xmalloc(dasiz);
    tileLoadData(tileNum, dasiz, data);

    int32_t crc = Bcrc32((unsigned char *)data, (unsigned int)dasiz, 0);

    Xfree(data);

    return crc;
}

vec2_16_t tileGetSize(int16_t tileNum)
{
    if ((unsigned)tileNum >= (unsigned)MAXTILES)
        return vec2_16_t{};

    return tilesiz[tileNum];
}

// Assumes pic has been initialized to zero.
void artConvertRGB(palette_t * const pic, uint8_t const * const buf, int32_t const bufsizx, int32_t const sizx, int32_t const sizy)
{
    for (bssize_t y = 0; y < sizy; ++y)
    {
        palette_t * const picrow = &pic[bufsizx * y];

        for (bssize_t x = 0; x < sizx; ++x)
        {
            uint8_t index = buf[sizy * x + y];

            if (index == 255)
                continue;

            index *= 3;

            // pic is BGRA
            picrow[x].r = palette[index+2];
            picrow[x].g = palette[index+1];
            picrow[x].b = palette[index];
            picrow[x].f = 255;
        }
    }
}

//
// allocatepermanenttile
//
intptr_t tileCreate(int16_t tilenume, int32_t xsiz, int32_t ysiz)
{
    if (xsiz <= 0 || ysiz <= 0 || (unsigned) tilenume >= MAXTILES)
        return 0;

    int const dasiz = xsiz*ysiz;

    walock[tilenume] = CACHE1D_PERMANENT;
    g_cache.allocateBlock(&waloff[tilenume], dasiz, &walock[tilenume]);

    tileSetSize(tilenume, xsiz, ysiz);
    Bmemset(&picanm[tilenume], 0, sizeof(picanm_t));

    return waloff[tilenume];
}

//
// copytilepiece
//
void tileCopySection(int32_t tilenume1, int32_t sx1, int32_t sy1, int32_t xsiz, int32_t ysiz,
    int32_t tilenume2, int32_t sx2, int32_t sy2)
{
    char *ptr1, *ptr2, dat;
    int32_t xsiz1, ysiz1, xsiz2, ysiz2, i, j, x1, y1, x2, y2;

    xsiz1 = tilesiz[tilenume1].x; ysiz1 = tilesiz[tilenume1].y;
    xsiz2 = tilesiz[tilenume2].x; ysiz2 = tilesiz[tilenume2].y;
    if ((xsiz1 > 0) && (ysiz1 > 0) && (xsiz2 > 0) && (ysiz2 > 0))
    {
        if (waloff[tilenume1] == 0) tileLoad(tilenume1);
        if (waloff[tilenume2] == 0) tileLoad(tilenume2);

        x1 = sx1;
        for (i=0; i<xsiz; i++)
        {
            y1 = sy1;
            for (j=0; j<ysiz; j++)
            {
                x2 = sx2+i;
                y2 = sy2+j;
                if ((x2 >= 0) && (y2 >= 0) && (x2 < xsiz2) && (y2 < ysiz2))
                {
                    ptr1 = (char *) (waloff[tilenume1] + x1*ysiz1 + y1);
                    ptr2 = (char *) (waloff[tilenume2] + x2*ysiz2 + y2);
                    dat = *ptr1;
                    if (dat != 255)
                        *ptr2 = *ptr1;
                }

                y1++; if (y1 >= ysiz1) y1 = 0;
            }
            x1++; if (x1 >= xsiz1) x1 = 0;
        }
    }
}

void Buninitart(void)
{
    if (artfil != buildvfs_kfd_invalid)
        kclose(artfil);

    DO_FREE_AND_NULL(g_vm_data);
}
