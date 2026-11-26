    smooshyalign = keystatus[KEYSC_gKP5];
    repeatpanalign = eitherSHIFT || eitherALT || (bstatus&2);

    {
        static int32_t omlook;

        if (mlook == 2)
        {
            mlook = omlook;
        }
        else if (!unrealedlook && (bstatus&4))
        {
            omlook = mlook;
            mlook = 2;
        }
    }

    // PK: no btn: wheel changes shade
    if ((bstatus&(16|32) && !(bstatus&(1|2|4))) || keystatus[KEYSC_gMINUS] || keystatus[KEYSC_gPLUS])
    {
        //        if (bstatus&1)
        //            mlook = 2;
        tsign = 0;
        if (bstatus&32 || PRESSED_KEYSC(gMINUS))  // -
            tsign = 1;
        if (bstatus&16 || PRESSED_KEYSC(gPLUS))  // +
            tsign = -1;

        if (tsign)
        {
            g_mouseBits &= ~(16|32);
            bstatus &= ~(16|32);

            if (eitherALT)  //ALT
            {
                if (eitherCTRL)  //CTRL
                {
                    if (tsign==1)
                        g_visibility <<= (int)(g_visibility < 16384);
                    else
                        g_visibility >>= (int)(g_visibility > 32);
                    silentmessage("Global visibility %d", g_visibility);
                }
                else
                {
                    k=eitherSHIFT?1:16;

                    if (highlightsectorcnt > 0 && (hlsectorbitmap[searchsector>>3]&(1<<(searchsector&7))))
                    {
                        while (k-- > 0)
                        {
                            for (i=0; i<highlightsectorcnt; i++)
                            {
                                j = highlightsector[i];
                                sector[j].visibility += tsign;

                                if (tsign==1 && sector[j].visibility == 240)
                                    sector[j].visibility = 239;
                                else if (tsign==-1 && sector[j].visibility == 239)
                                    sector[j].visibility = 240;
                            }
                        }
                    }
                    else
                    {
                        while (k-- > 0)
                        {
                            sector[searchsector].visibility += tsign;

                            if (tsign==1 && sector[searchsector].visibility == 240)
                                sector[searchsector].visibility = 239;
                            else if (tsign==-1 && sector[searchsector].visibility == 239)
                                sector[searchsector].visibility = 240;
                        }
                    }

                    silentmessage("Sector %d visibility %d", searchsector,
                                  TrackerCast(sector[searchsector].visibility));
                    asksave = 1;
                }
            }
            else  // if !eitherALT
            {
                int32_t clamped=0;

                k = (highlightsectorcnt>0 && (hlsectorbitmap[searchsector>>3]&(1<<(searchsector&7))));
                tsign *= (1+3*eitherCTRL);

                if (k == 0)
                {
                    if (ASSERT_AIMING)
                    {
                        if (!eitherSHIFT && AIMING_AT_SPRITE && (show2dsprite[searchwall>>3]&(1<<(searchwall&7))))
                        {
                            for (i=0; i<highlightcnt; i++)
                                if (highlight[i]&16384)
                                    clamped = addtobyte(&sprite[highlight[i]&16383].shade, tsign);
                            (clamped ? message : silentmessage)
                                ("Highlighted sprite shade changed by %d%s",
                                 tsign, clamped?" (some sprites' shade clamped)":"");
                        }
                        else
                        {
                            clamped = addtobyte(&AIMED_CF_SEL(shade), tsign);
                            // TODO: factor formatting stuff out and use elsewhere?
                            (clamped ? message : silentmessage)
                                ("%s %s%d shade %d%s", Typestr[searchstat],
                                 AIMING_AT_CEILING_OR_FLOOR ? "of sector " : "",
                                 AIMING_AT_WALL_OR_MASK ? SELECT_WALL() :
                                     (AIMING_AT_CEILING_OR_FLOOR ? searchsector : searchwall),
                                 AIMED_CF_SEL(shade), clamped ? " (clamped)":"");
                        }
                    }
                }
                else
                {
                    for (i=0; i<highlightsectorcnt; i++)
                    {
                        dasector = highlightsector[i];

                        // sector shade
                        clamped |= addtobyte(&sector[dasector].ceilingshade, tsign);
                        clamped |= addtobyte(&sector[dasector].floorshade, tsign);

                        // wall shade
                        for (WALLS_OF_SECTOR(dasector, j))
                            clamped |= addtobyte(&wall[j].shade, tsign);

                        // sprite shade
                        for (j=headspritesect[dasector]; j!=-1; j=nextspritesect[j])
                            clamped |= addtobyte(&sprite[j].shade, tsign);
                    }
                    (clamped ? message : silentmessage)("Highlighted sector shade changed by %d%s", tsign,
                            clamped?" (some objects' shade clamped)":"");
                }
                asksave = 1;
            }
        }
    }
