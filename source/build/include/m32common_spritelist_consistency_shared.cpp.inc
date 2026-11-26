#ifndef M32COMMON_DELETED_SPRITE_SECTNUM
# define M32COMMON_DELETED_SPRITE_SECTNUM MAXSECTORS
#endif

#ifndef M32COMMON_LOG_SPRITE_COUNT_MISMATCH
# define M32COMMON_LOG_SPRITE_COUNT_MISMATCH(ournumsprites, numsprites) \
    LOG_F(INFO, "ournumsprites=%d, Numsprites=%d", (ournumsprites), (numsprites))
#endif

static int32_t csc_s, csc_i;
// 1: corrupt, 0: OK
static int32_t check_spritelist_consistency()
{
    int32_t ournumsprites=0;
    static uint8_t havesprite[bitmap_size(MAXSPRITES)];

    csc_s = csc_i = -1;

    if (Numsprites < 0 || Numsprites > MAXSPRITES)
        return 1;

    for (bssize_t i=0; i<MAXSPRITES; i++)
    {
        const int32_t sectnum=sprite[i].sectnum, statnum=sprite[i].statnum;

        csc_i = i;

        if ((statnum==MAXSTATUS) != (sectnum==M32COMMON_DELETED_SPRITE_SECTNUM))
            return 2;  // violation of .statnum==MAXSTATUS iff .sectnum==M32COMMON_DELETED_SPRITE_SECTNUM

        if ((unsigned)statnum > MAXSTATUS || (sectnum!=M32COMMON_DELETED_SPRITE_SECTNUM && (unsigned)sectnum > (unsigned)numsectors))
            return 3;  // oob sectnum or statnum

        if (statnum != MAXSTATUS)
            ournumsprites++;
    }

    if (ournumsprites != Numsprites)
    {
        M32COMMON_LOG_SPRITE_COUNT_MISMATCH(ournumsprites, Numsprites);
        return 4;  // counting sprites by statnum!=MAXSTATUS inconsistent with Numsprites
    }

    // SECTOR LIST

    Bmemset(havesprite, 0, bitmap_size(Numsprites));

    for (bssize_t s=0; s<numsectors; s++)
    {
        int i;
        csc_s = s;

        for (i=headspritesect[s]; i>=0; i=nextspritesect[i])
        {
            csc_i = i;

            if (i >= MAXSPRITES)
                return 5;  // oob sprite index in list, or Numsprites inconsistent

            if (bitmap_test(havesprite, i))
                return 6;  // have a cycle in the list

            bitmap_set(havesprite, i);

            if (sprite[i].sectnum != s)
                return 7;  // .sectnum inconsistent with list
        }

        if (i!=-1)
            return 8;  // various code checks for -1 to break loop
    }

    csc_s = -1;
    for (bssize_t i=0; i<MAXSPRITES; i++)
    {
        csc_i = i;

        if (sprite[i].statnum!=MAXSTATUS && !bitmap_test(havesprite, i))
            return 9;  // have a sprite in the world not in sector list
    }


    // STATUS LIST -- we now clear havesprite[] bits

    for (bssize_t s=0; s<MAXSTATUS; s++)
    {
        int i;
        csc_s = s;

        for (i=headspritestat[s]; i>=0; i=nextspritestat[i])
        {
            csc_i = i;

            if (i >= MAXSPRITES)
                return 10;  // oob sprite index in list, or Numsprites inconsistent

            // have a cycle in the list, or status list inconsistent with
            // sector list (*)
            if (!bitmap_test(havesprite, i))
                return 11;

            bitmap_clear(havesprite, i);

            if (sprite[i].statnum != s)
                return 12;  // .statnum inconsistent with list
        }

        if (i!=-1)
            return 13;  // various code checks for -1 to break loop
    }

    csc_s = -1;
    for (bssize_t i=0; i<Numsprites; i++)
    {
        csc_i = i;

        // Status list contains only a proper subset of the sprites in the
        // sector list.  Reverse case is handled by (*)
        if (bitmap_test(havesprite, i))
            return 14;
    }

    return 0;
}

#undef M32COMMON_LOG_SPRITE_COUNT_MISMATCH
#undef M32COMMON_DELETED_SPRITE_SECTNUM
