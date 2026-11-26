// This object is shared by the editors of *all* Build games!

#include "compat.h"
#include "keys.h"
#include "build.h"
#include "cache1d.h"
#ifdef POLYMER
# include "polymer.h"
#endif
#include "editor.h"
#include "renderlayer.h"

#include "m32script.h"
#include "m32def.h"

#include "lz4.h"
#include "xxhash.h"

// XXX: This breaks editors for games other than Duke. The OSD needs a way to specify colors in abstract instead of concatenating palswap escape sequences.
#include "common_game.h"

#include "vfs.h"

#include "../../build/include/m32common_taglab_core_shared.cpp.inc"

int32_t taglab_load(const char *filename, int32_t flags)
{
    int32_t len, i;
    char buf[BMAX_PATH], *dot, *filebuf;

    taglab_init();

    len = Bstrlen(filename);
    if (len >= BMAX_PATH-1)
        return -1;
    Bmemcpy(buf, filename, len+1);

    //
    dot = Bstrrchr(buf, '.');
    if (!dot)
        dot = &buf[len];

    if (dot-buf+8 >= BMAX_PATH)
        return -1;
    Bmemcpy(dot, ".maptags", 9);
    //

    buildvfs_kfd fil;
    if ((fil = kopen4load(buf,flags)) == buildvfs_kfd_invalid)
        return -1;

    len = kfilelength(fil);

    filebuf = (char *)Xmalloc(len+1);
    if (!filebuf)
    {
        kclose(fil);
        return -1;
    }

    kread(fil, filebuf, len);
    filebuf[len] = 0;
    kclose(fil);

    // ----

    {
        int32_t tag;
        char *cp=filebuf, *bp, *ep;

        while (1)
        {
#define XTAGLAB_STRINGIFY(X) TAGLAB_STRINGIFY(X)
#define TAGLAB_STRINGIFY(X) #X
            i = sscanf(cp, "%d %" XTAGLAB_STRINGIFY(TAGLAB_MAX) "s", &tag, buf);
#undef XTAGLAB_STRINGIFY
#undef TAGLAB_STRINGIFY
            if (i != 2 || !buf[0] || tag<0 || tag>=32768)
                goto nextline;

            buf[TAGLAB_MAX-1] = 0;

            i = Bstrlen(buf);
            bp = buf; while (*bp && isspace(*bp)) bp++;
            ep = &buf[i-1]; while (ep>buf && isspace(*ep)) ep--;
            ep++;

            if (!(ep > bp))
                goto nextline;
            *ep = 0;

            taglab_add(bp, tag);
//initprintf("add tag %s:%d\n", bp, tag);
nextline:
            while (*cp && *cp!='\n')
                cp++;
            while (*cp=='\r' || *cp=='\n')
                cp++;
            if (*cp == 0)
                break;
        }
    }

    // ----
    Xfree(filebuf);

    return 0;
}

int32_t taglab_save(const char *mapname)
{
    int32_t len, i;
    char buf[BMAX_PATH], *dot;
    const char *label;

    if (g_taglab.numlabels==0)
        return 1;

    Bstrncpyz(buf, mapname, BMAX_PATH);

    len = Bstrlen(buf);
    //
    dot = Bstrrchr(buf, '.');
    if (!dot)
        dot = &buf[len];

    if (dot-buf+8 >= BMAX_PATH)
        return -1;
    Bmemcpy(dot, ".maptags", 9);
    //

    buildvfs_fd fil;
    if ((fil = buildvfs_open_write(buf)) == buildvfs_fd_invalid)
    {
        LOG_F(ERROR, "Couldn't open \"%s\" for writing: %s", buf, strerror(errno));
        return -1;
    }

    for (i=0; i<32768; i++)
    {
        label = taglab_getlabel(i);
        if (!label)
            continue;

        len = Bsprintf(buf, "%d %s" OURNEWL, i, label);
        if (buildvfs_write(fil, buf, len)!=len)
            break;
    }

    buildvfs_close(fil);

    return (i!=32768);
}

#include "../../build/include/m32common_taglab_lookup_shared.cpp.inc"

////////// UNDO/REDO SYSTEM //////////
#if M32_UNDO
mapundo_t *mapstate = NULL;

int32_t map_revision = 1;

static int32_t try_match_with_prev(int32_t idx, int32_t numsthgs, XXH64_hash_t crc)
{
    if (mapstate->prev && mapstate->prev->num[idx]==numsthgs && mapstate->prev->crc[idx]==crc)
    {
        // found match!
        mapstate->lz4Blocks[idx] = mapstate->prev->lz4Blocks[idx];
        mapstate->lz4Size[idx] = mapstate->prev->lz4Size[idx];
        (*(int32_t *)mapstate->lz4Blocks[idx])++;  // increase refcount!

        return 1;
    }

    return 0;
}

static void create_compressed_block(int32_t idx, const void *srcdata, uint32_t size, XXH64_hash_t crc)
{
    // allocate
    int const compressed_size = LZ4_compressBound(size);
    Bassert(compressed_size);
    mapstate->lz4Blocks[idx] = (char *)Xmalloc(4+compressed_size);

    // compress & realloc
    mapstate->lz4Size[idx] = LZ4_compress_default((const char*)srcdata, mapstate->lz4Blocks[idx]+4, size, compressed_size);
    Bassert(mapstate->lz4Size[idx] > 0);
    mapstate->lz4Blocks[idx] = (char *)Xrealloc(mapstate->lz4Blocks[idx], 4+mapstate->lz4Size[idx]);

    // write refcount
    *(int32_t *)mapstate->lz4Blocks[idx] = 1;

    mapstate->crc[idx] = crc;
}

static void free_self_and_successors(mapundo_t *mapst)
{
    mapundo_t *cur = mapst;

    mapst->prev = NULL;  // break the back link

    while (cur->next)
        cur = cur->next;

    while (1)
    {
        int32_t i;
        mapundo_t *const prev = cur->prev;

        for (i=0; i<3; i++)
        {
            int32_t *const refcnt = (int32_t *)cur->lz4Blocks[i];

            if (refcnt)
            {
                (*refcnt)--;
                if (*refcnt == 0)
                    Xfree(refcnt);  // free the block!
            }
        }

        Xfree(cur);

        if (!prev)
            break;

        cur = prev;
    }
}

// NOTE: only _consecutive_ matching (size+crc) sector/wall/sprite blocks are
// shared!
void create_map_snapshot(void)
{
    if (mapstate == NULL)
    {
        // create initial mapstate

        map_revision = 1;

        mapstate = (mapundo_t *)Xcalloc(1, sizeof(mapundo_t));
        mapstate->revision = map_revision;
        mapstate->prev = mapstate->next = NULL;
    }
    else
    {
        if (mapstate->next)
            free_self_and_successors(mapstate->next);
        // now, have no successors

        // calloc because not everything may be set in the following:
        mapstate->next = (mapundo_t *)Xcalloc(1, sizeof(mapundo_t));
        mapstate->next->prev = mapstate;

        mapstate = mapstate->next;

        mapstate->revision = ++map_revision;
    }


    fixspritesectors();

    mapstate->num[UNDO_SECTORS] = numsectors;
    mapstate->num[UNDO_WALLS]   = numwalls;
    mapstate->num[UNDO_SPRITES] = Numsprites;

    mapstate->startpos     = startpos;
    mapstate->startang     = startang;
    mapstate->startsectnum = startsectnum;

    if (numsectors)
    {
        XXH64_hash_t temphash = XXH3_64bits((uint8_t *)sector, numsectors*sizeof(sectortype));

        if (!try_match_with_prev(0, numsectors, temphash))
            create_compressed_block(0, sector, numsectors*sizeof(sectortype), temphash);

        if (numwalls)
        {
            temphash = XXH3_64bits((uint8_t *)wall, numwalls*sizeof(walltype));

            if (!try_match_with_prev(1, numwalls, temphash))
                create_compressed_block(1, wall, numwalls*sizeof(walltype), temphash);
        }

        if (Numsprites)
        {
            temphash = XXH3_64bits((uint8_t *)sprite, MAXSPRITES*sizeof(spritetype));

            if (!try_match_with_prev(2, Numsprites, temphash))
            {
                int32_t i = 0;
                auto const uspri = (uspritetype *)Xmalloc(Numsprites*sizeof(spritetype) + 4);
                auto spri = uspri;

                for (bssize_t j=0; j<MAXSPRITES && i < Numsprites; j++)
                    if (sprite[j].statnum != MAXSTATUS)
                    {
                        Bmemcpy(spri++, &sprite[j], sizeof(spritetype));
                        i++;
                    }

                create_compressed_block(2, uspri, Numsprites*sizeof(spritetype), temphash);
                Xfree(uspri);
            }
        }
    }

    CheckMapCorruption(5, 0);
}

void map_undoredo_free(void)
{
    if (mapstate)
    {
        free_self_and_successors(mapstate);
        mapstate = NULL;
    }

    map_revision = 1;
}

int32_t map_undoredo(int dir)
{
    auto bstatus = mouseReadButtons();
    if (mapstate == NULL || EDITING_MAP_P()) return 1;

    auto const which = dir ? mapstate->next : mapstate->prev;
    if (which == NULL || !which->num[UNDO_SECTORS]) return 1;

    mapstate = which;

    numsectors   = mapstate->num[UNDO_SECTORS];
    numwalls     = mapstate->num[UNDO_WALLS];
    map_revision = mapstate->revision;

    startpos     = mapstate->startpos;
    startang     = mapstate->startang;
    startsectnum = mapstate->startsectnum;

    Bmemset(show2dsector, 0, sizeof(show2dsector));

    reset_highlightsector();
    reset_highlight();

    initspritelists();

    if (mapstate->num[UNDO_SECTORS])
    {
        // restore sector[]
        auto bytes = LZ4_decompress_safe(mapstate->lz4Blocks[UNDO_SECTORS]+4, (char*)sector, mapstate->lz4Size[UNDO_SECTORS], MAXSECTORS*sizeof(sectortype));
        UNREFERENCED_PARAMETER(bytes);
        Bassert(bytes > 0);

        if (mapstate->num[UNDO_WALLS])  // restore wall[]
        {
            bytes = LZ4_decompress_safe(mapstate->lz4Blocks[UNDO_WALLS]+4, (char*)wall, mapstate->lz4Size[UNDO_WALLS], MAXWALLS*sizeof(walltype));
            Bassert(bytes > 0);
        }

        if (mapstate->num[UNDO_SPRITES])  // restore sprite[]
        {
            bytes = LZ4_decompress_safe(mapstate->lz4Blocks[UNDO_SPRITES]+4, (char*)sprite, mapstate->lz4Size[UNDO_SPRITES], MAXSPRITES*sizeof(spritetype));
            Bassert(bytes > 0);
        }
    }

    // insert sprites
    for (int i=0; i<mapstate->num[UNDO_SPRITES]; i++)
    {
        if ((sprite[i].cstat & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_SLOPE && (sprite[i].xoffset|sprite[i].yoffset) == 0)
            sprite[i].cstat &= ~CSTAT_SPRITE_ALIGNMENT_SLOPE;
        Bassert((unsigned)sprite[i].sectnum < (unsigned)numsectors
                   && (unsigned)sprite[i].statnum < MAXSTATUS);
        insertsprite(sprite[i].sectnum, sprite[i].statnum);
    }

    Bassert(Numsprites == mapstate->num[UNDO_SPRITES]);

#ifdef POLYMER
    if (in3dmode() && videoGetRenderMode() == REND_POLYMER)
        polymer_loadboard();
#endif
#ifdef YAX_ENABLE
    yax_update(0);
    yax_updategrays(pos.z);
#endif
    calc_sector_reachability();
    CheckMapCorruption(4, 0);

    return 0;
}
#endif

////

#include "../../build/include/m32common_corruptchk_prefix_shared.cpp.inc"


#include "../../build/include/m32common_spritelist_consistency_shared.cpp.inc"

#if CCHK_LOOP_CHECKS
// Return the least wall index of the outer loop of sector <sectnum>, or
//  -1 if there is none,
//  -2 if there is more than one.
static int32_t determine_outer_loop(int32_t sectnum)
{
    int32_t j, outerloopstart = -1;

    const int32_t startwall = sector[sectnum].wallptr;
    const int32_t endwall = startwall + sector[sectnum].wallnum - 1;

    for (j=startwall; j<=endwall; j=get_nextloopstart(j))
    {
        if (clockdir(j) == CLOCKDIR_CW)
        {
            if (outerloopstart == -1)
                outerloopstart = j;
            else if (outerloopstart >= 0)
                return -2;
        }
    }

    return outerloopstart;
}
#endif

#define TRYFIX_NONE() (tryfixing == 0ull)
#define TRYFIX_CNUM(onumct) (onumct < MAXCORRUPTTHINGS && (tryfixing & (1ull<<onumct)))

#include "../../build/include/m32common_check_corruption_shared.cpp.inc"
////


#include "../../build/include/m32common_status_menu_shared.cpp.inc"

#include "../../build/include/m32common_special_functions_menu_shared.cpp.inc"

void m32_showmouse()
{
#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        renderDisableFog();
        polymost_useColorOnly(true);
    }
#endif

    int const lores = !!(xdim <= 640);
    int col = batmanandthrobbin();
    int i;

    for (i = 1; i <= (7 >> lores); i++)
    {
        plotpixel(searchx+i,searchy,col);
        plotpixel(searchx-i,searchy,col);
        plotpixel(searchx,searchy-i,col);
        plotpixel(searchx,searchy+i,col);
    }

    col = throbandbig();
    i = (8 >> lores);

    plotpixel(searchx+i,searchy,col);
    plotpixel(searchx-i,searchy,col);
    plotpixel(searchx,searchy-i,col);
    plotpixel(searchx,searchy+i,col);
    plotpixel(searchx, searchy, col);

#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        renderEnableFog();
        polymost_useColorOnly(false);
    }
#endif
}
