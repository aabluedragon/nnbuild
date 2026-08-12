////////// UNDO/REDO SYSTEM //////////
#if M32_UNDO
mapundo_t *mapstate = NULL;

int32_t map_revision = 1;

static int32_t try_match_with_prev(int32_t idx, int32_t numsthgs, uintptr_t crc)
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

static void create_compressed_block(int32_t idx, const void *srcdata, uint32_t size, uintptr_t crc)
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

    if (numsectors)
    {
#if !defined UINTPTR_MAX
# error Need UINTPTR_MAX define to select between 32- and 64-bit functions
#endif
#if UINTPTR_MAX == 0xffffffff
        /* 32-bit */
#define XXH__ XXH32
#else
        /* 64-bit */
#define XXH__ XXH64
#endif
        uintptr_t temphash = XXH__((uint8_t *)sector, numsectors*sizeof(sectortype), numsectors*sizeof(sectortype));

        if (!try_match_with_prev(0, numsectors, temphash))
            create_compressed_block(0, sector, numsectors*sizeof(sectortype), temphash);

        if (numwalls)
        {
            temphash = XXH__((uint8_t *)wall, numwalls*sizeof(walltype), numwalls*sizeof(walltype));

            if (!try_match_with_prev(1, numwalls, temphash))
                create_compressed_block(1, wall, numwalls*sizeof(walltype), temphash);
        }

        if (Numsprites)
        {
            temphash = XXH__((uint8_t *)sprite, MAXSPRITES*sizeof(spritetype), MAXSPRITES*sizeof(spritetype));

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
#undef XXH__
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
    if (mapstate == NULL) return 1;

    auto const which = dir ? mapstate->next : mapstate->prev;
    if (which == NULL || !which->num[UNDO_SECTORS]) return 1;

    mapstate = which;

    numsectors   = mapstate->num[UNDO_SECTORS];
    numwalls     = mapstate->num[UNDO_WALLS];
    map_revision = mapstate->revision;

    Bmemset(show2dsector, 0, sizeof(show2dsector));

    reset_highlightsector();
    reset_highlight();

    initspritelists();

    if (mapstate->num[UNDO_SECTORS])
    {
        // restore sector[]
        auto bytes = LZ4_decompress_safe(mapstate->lz4Blocks[UNDO_SECTORS]+4, (char*)sector, mapstate->lz4Size[UNDO_SECTORS], MAXSECTORS*sizeof(sectortype));
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
        if ((sprite[i].cstat & 48) == 48) sprite[i].cstat &= ~48;
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
    CheckMapCorruption(4, 0);

    return 0;
}
#endif
