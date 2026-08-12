// Shared Duke/VibeBuild32 psky setup helpers.
//
// Included from source/duke3d/src/common.cpp and
// source/vibebuild32/src/common.cpp. The including translation unit provides
// tileSetupSky(), DEFAULTPSKY, sector/numsectors, getpskyidx(), and g_pskyidx.

// Set up new-style multi-psky handling.
void G_InitMultiPsky(int CLOUDYOCEAN__DYN, int MOONSKY1__DYN, int BIGORBIT1__DYN, int LA__DYN)
{
    // When adding other multi-skies, take care that the tileofs[] values are
    // <= PSKYOFF_MAX. (It can be increased up to MAXPSKYTILES, but should be
    // set as tight as possible.)

    // The default sky properties (all others are implicitly zero):
    psky_t *sky      = tileSetupSky(DEFAULTPSKY);
    sky->lognumtiles = 3;
    sky->horizfrac   = 32768;

    // CLOUDYOCEAN
    // Aligns with the drawn scene horizon because it has one itself.
    sky              = tileSetupSky(CLOUDYOCEAN__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac   = 65536;

    // MOONSKY1
    //        earth          mountain   mountain         sun
    sky              = tileSetupSky(MOONSKY1__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac   = 32768;
    sky->tileofs[6]  = 1;
    sky->tileofs[1]  = 2;
    sky->tileofs[4]  = 2;
    sky->tileofs[2]  = 3;

    // BIGORBIT1   // orbit
    //       earth1         2           3           moon/sun
    sky              = tileSetupSky(BIGORBIT1__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac   = 32768;
    sky->tileofs[5]  = 1;
    sky->tileofs[6]  = 2;
    sky->tileofs[7]  = 3;
    sky->tileofs[2]  = 4;

    // LA // la city
    //       earth1         2           3           moon/sun
    sky              = tileSetupSky(LA__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac   = 16384 + 1024;
    sky->tileofs[0]  = 1;
    sky->tileofs[1]  = 2;
    sky->tileofs[2]  = 1;
    sky->tileofs[3]  = 3;
    sky->tileofs[4]  = 4;
    sky->tileofs[5]  = 0;
    sky->tileofs[6]  = 2;
    sky->tileofs[7]  = 3;

#if 0
    // This assertion should hold. See note above.
    for (bssize_t i=0; i<pskynummultis; ++i)
        for (bssize_t j=0; j<(1<<multipsky[i].lognumtiles); ++j)
            Bassert(multipsky[i].tileofs[j] <= PSKYOFF_MAX);
#endif
}

void G_SetupGlobalPsky(void)
{
    int skyIdx = 0;

    // NOTE: Loop must be running backwards for the same behavior as the game
    // (greatest sector index with matching parallaxed sky takes precedence).
    for (int i = numsectors - 1; i >= 0; i--)
    {
        if (sector[i].ceilingstat & 1)
        {
            skyIdx = getpskyidx(sector[i].ceilingpicnum);
            if (skyIdx > 0)
                break;
        }
    }

    g_pskyidx = skyIdx;
}
