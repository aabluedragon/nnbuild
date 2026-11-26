static int32_t DrawTiles(int32_t iTopLeft, int32_t iSelected, int32_t nXTiles, int32_t nYTiles,
                         int32_t TileDim, int32_t offset, int32_t showmsg);

#define TMPERRMSG_SHOW(alsoOSD) do { \
    printext256(0, 0, whitecol, 0, tilesel_errmsg, 0); \
    if (alsoOSD) OSD_Printf("%s\n", tilesel_errmsg); \
} while (0)

#define TMPERRMSG_PRINT(Msg, ...) do {  \
    Bsprintf(tilesel_errmsg, Msg, ## __VA_ARGS__); \
    TMPERRMSG_SHOW(1); \
    videoShowFrame(1); \
    tilesel_showerr = 1; \
} while (0)

#define TMPERRMSG_RETURN(Msg, ...) do   \
{ \
    TMPERRMSG_PRINT(Msg, ## __VA_ARGS__);  \
    return 1; \
} while (0)


static inline void pushDisableFog(void)
{
#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        glPushAttrib(GL_ENABLE_BIT);
        polymost_setFogEnabled(false);
    }
#endif
}

static inline void popDisableFog(void)
{
#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        glPopAttrib();
    }
#endif
}

static int32_t m32gettile(int32_t idInitialTile)
{
    int32_t gap, temp, zoomsz;
    int32_t nXTiles, nYTiles, nDisplayedTiles;
    int32_t i;
    int32_t tileNum, iTopLeftTile, iLastTile;
    int32_t idSelectedTile, idInitialPal;
    int32_t scrollmode;
    int32_t mousedx, mousedy, mtile, omousex=searchx, omousey=searchy, moffset=0;

    int32_t noTilesMarked=1;
    int32_t mark_lastk = -1;

#ifdef M32_GETTILE_BEFORE_FOG
    M32_GETTILE_BEFORE_FOG();
#endif

    pushDisableFog();

    // Enable following line for testing. I couldn't work out how to change vidmode on the fly
    // s_Zoom = NUM_ZOOMS - 1;

    idInitialTile = clamp(idInitialTile, 0, MAXTILES-1);

    // Ensure zoom not to big (which can happen if display size
    //   changes whilst Mapster is running)
    do
    {
        nXTiles = xdim / ZoomToThumbSize[s_Zoom];
        nYTiles = ydim / ZoomToThumbSize[s_Zoom];
        // Refuse to draw less than half of a row.
        if (ZoomToThumbSize[s_Zoom]/2 < 12) nYTiles--;
        nDisplayedTiles  = nXTiles * nYTiles;

        if (!nDisplayedTiles)
        {
            // Eh-up, resolution changed since we were last displaying tiles.
            s_Zoom--;
        }
    }
    while (!nDisplayedTiles);

    keystatus[KEYSC_V] = 0;

    for (i = 0; i < MAXTILES; i++)
    {
        localartfreq[i] = 0;
        localartlookup[i] = i;
    }

    iLastTile = tileNum = idSelectedTile = idInitialTile;
    idInitialPal = globalpal;

    switch (searchstat)
    {
    case SEARCH_WALL:
        for (i = 0; i < numwalls; i++)
            localartfreq[ wall[i].picnum ]++;
        break;

    case SEARCH_CEILING:
    case SEARCH_FLOOR:
        for (i = 0; i < numsectors; i++)
        {
            localartfreq[ sector[i].ceilingpicnum ]++;
            localartfreq[ sector[i].floorpicnum ]++;
        }
        break;

    case SEARCH_SPRITE:
        for (i=0; i<MAXSPRITES; i++)
            localartfreq[ sprite[i].picnum ] += (sprite[i].statnum < MAXSTATUS);
        break;

    case SEARCH_MASKWALL:
        for (i = 0; i < numwalls; i++)
            localartfreq[ wall[i].overpicnum ]++;
        break;

    default:
        break;
    }


    //
    //	Sort tiles into frequency order
    //

    gap = MAXTILES / 2;

    do
    {
        for (i = 0; i < MAXTILES-gap; i++)
        {
            temp = i;

            while (temp >= 0 && localartfreq[temp]<localartfreq[temp+gap])
            {
                int32_t tempint;

                tempint = localartfreq[temp];
                localartfreq[temp] = localartfreq[temp+gap];
                localartfreq[temp+gap] = tempint;

                tempint = localartlookup[temp];
                localartlookup[temp] = localartlookup[temp+gap];
                localartlookup[temp+gap] = tempint;

                if (tileNum == temp)
                    tileNum = temp + gap;
                else if (tileNum == temp + gap)
                    tileNum = temp;

                temp -= gap;
            }
        }
        gap >>= 1;
    }
    while (gap > 0);

    //
    // Set up count of number of used tiles
    //

    localartlookupnum = 0;
    while (localartfreq[localartlookupnum] > 0)
        localartlookupnum++;

    //
    // Check : If no tiles used at all then switch to displaying all tiles
    //

    if (!localartfreq[0])
    {
        localartlookupnum = MAXTILES;

        for (i = 0; i < MAXTILES; i++)
        {
            localartlookup[i] = i;
            localartfreq[i] = 0; // Terrible bodge : zero tilefreq's not displayed in tile view. Still, when in Rome ... :-)
        }

        tileNum = idInitialTile;
    }

    //
    //
    //

    iTopLeftTile = tileNum - (tileNum % nXTiles);
    iTopLeftTile = clamp(iTopLeftTile, 0, MAXTILES-nDisplayedTiles);

    zoomsz = ZoomToThumbSize[s_Zoom];

    searchx = ((tileNum-iTopLeftTile)%nXTiles)*zoomsz + zoomsz/2;
    searchy = ((tileNum-iTopLeftTile)/nXTiles)*zoomsz + zoomsz/2;

    ////////////////////////////////
    // Start of key handling code //
    ////////////////////////////////

    while ((keystatus[KEYSC_ENTER]|keystatus[KEYSC_ESC]|(bstatus&1)) == 0)
    {
        int32_t ret;
        zoomsz = ZoomToThumbSize[s_Zoom];

        ret = DrawTiles(iTopLeftTile, (tileNum >= localartlookupnum) ? localartlookupnum-1 : tileNum,
                        nXTiles, nYTiles, zoomsz, moffset,
                        (tilesel_showerr && (tileNum==iLastTile || (tilesel_showerr=0))));

        if (ret==0)
        {
            idle_waitevent_timeout(500);
            // SDL seems to miss mousewheel events when rotated slowly.

            if (handleevents())
                quitevent = 0;
        }
        mouseGetValues(&mousedx,&mousedy,&bstatus);

        iLastTile = tileNum;

        searchx += mousedx;
        searchy += mousedy;

        if (bstatus&2)
        {
            moffset += mousedy*2;
            searchy += mousedy;
            searchx -= mousedx;

            if ((moffset < 0 && iTopLeftTile > localartlookupnum-nDisplayedTiles-1)
                    || (moffset > 0 && iTopLeftTile==0))
            {
                moffset=0;
                searchy -= mousedy*2;
            }

            while (moffset > zoomsz)
            {
                iTopLeftTile -= nXTiles;
                moffset -= zoomsz;
            }
            while (moffset < -zoomsz)
            {
                iTopLeftTile += nXTiles;
                moffset += zoomsz;
            }
        }

        // Keep the pointer visible at all times.
        temp = min(zoomsz/2, 12);
        inpclamp(&searchx, temp, xdim-temp);
        inpclamp(&searchy, temp, ydim-temp);

        scrollmode = !(eitherCTRL^revertCTRL);
        if (bstatus&16 && scrollmode && iTopLeftTile > 0)
        {
            g_mouseBits &= ~16;
            iTopLeftTile -= (nXTiles*scrollamount);
        }
        else if (bstatus&32 && scrollmode && iTopLeftTile < localartlookupnum-nDisplayedTiles-1)
        {
            g_mouseBits &= ~32;
            iTopLeftTile += (nXTiles*scrollamount);
        }

        mtile = tileNum = searchx/zoomsz + ((searchy-moffset)/zoomsz)*nXTiles + iTopLeftTile;
        while (tileNum >= iTopLeftTile + nDisplayedTiles)
        {
            tileNum -= nXTiles;
            mtile = tileNum;
        }

        // These two lines are so obvious I don't need to comment them ...;-)
        synctics = (int32_t) totalclock-lockclock;
        lockclock += synctics;

        // Zoom in / out using numeric key pad's / and * keys
        if (((keystatus[KEYSC_gSLASH] || (!scrollmode && bstatus&16)) && s_Zoom<(signed)(NUM_ZOOMS-1))
                || ((keystatus[KEYSC_gSTAR]  || (!scrollmode && bstatus&32)) && s_Zoom>0))
        {
            if (PRESSED_KEYSC(gSLASH) || (!scrollmode && bstatus&16))
            {
                g_mouseBits &= ~16;
                bstatus &= ~16;

                // Watch out : If editor window is small, then the next zoom level
                //  might get so large that even one tile might not fit !
                if (ZoomToThumbSize[s_Zoom+1]<=xdim && ZoomToThumbSize[s_Zoom+1]<=ydim)
                {
                    // Phew, plenty of room.
                    s_Zoom++;
                }
            }
            else
            {
                keystatus[KEYSC_gSTAR] = 0;
                g_mouseBits &= ~32;
                bstatus &= ~32;
                s_Zoom--;
            }

            zoomsz = ZoomToThumbSize[s_Zoom];

            if (tileNum >= localartlookupnum)
                tileNum = localartlookupnum-1;

            // Calculate new num of tiles to display
            nXTiles = xdim / zoomsz;
            nYTiles = ydim / zoomsz;
            // Refuse to draw less than half of a row.
            if (zoomsz/2 < 12)
                nYTiles--;
            nDisplayedTiles  = nXTiles * nYTiles;

            // Determine if the top-left displayed tile needs to
            //   alter in order to display selected tile
            iTopLeftTile = tileNum - (tileNum % nXTiles);
            iTopLeftTile = clamp(iTopLeftTile, 0, MAXTILES - nDisplayedTiles);

            // scroll window so mouse points the same tile as it was before zooming
            iTopLeftTile -= searchx/zoomsz + ((searchy-moffset)/zoomsz)*nXTiles + iTopLeftTile-tileNum;
        }

        if (PRESSED_KEYSC(LEFT))
        {
            if (eitherCTRL)  // same as HOME, for consistency with CTRL-UP/DOWN
                tileNum = (tileNum/nXTiles)*nXTiles;
            else
                tileNum--;
        }

        if (PRESSED_KEYSC(RIGHT))
        {
            if (eitherCTRL)  // same as END, for consistency with CTRL-UP/DOWN
                tileNum = ((tileNum+nXTiles)/nXTiles)*nXTiles - 1;
            else
                tileNum++;
        }

        if (PRESSED_KEYSC(UP))
        {
            if (eitherCTRL)
                while (tileNum-nXTiles >= iTopLeftTile)
                    tileNum -= nXTiles;
            else
                tileNum -= nXTiles;
        }

        if (PRESSED_KEYSC(DOWN))
        {
            if (eitherCTRL)
                while (tileNum+nXTiles < iTopLeftTile + nDisplayedTiles)
                    tileNum += nXTiles;
            else
                tileNum += nXTiles;
        }

        if (PRESSED_KEYSC(PGUP))
        {
            if (eitherCTRL)
                tileNum = 0;
            else
                tileNum -= nDisplayedTiles;
        }

        if (PRESSED_KEYSC(PGDN))
        {
            if (eitherCTRL)
                tileNum = localartlookupnum-1;
            else
                tileNum += nDisplayedTiles;
        }

        if (PRESSED_KEYSC(HOME))
        {
            if (eitherCTRL)
                tileNum = iTopLeftTile;
            else
                tileNum = (tileNum/nXTiles)*nXTiles;
        }

        if (PRESSED_KEYSC(END))
        {
            if (eitherCTRL)
                tileNum = iTopLeftTile + nDisplayedTiles - 1;
            else
                tileNum = ((tileNum+nXTiles)/nXTiles)*nXTiles - 1;
        }

        if (PRESSED_KEYSC(DASH))
        {
            if (globalpal > 0)
                globalpal--;
            else globalpal = M32_MAXPALOOKUPS;

            while (globalpal > 0 && (!palookup[globalpal] || palookup[globalpal] == palookup[0]))
                globalpal--;
        }

        if (PRESSED_KEYSC(EQUAL))
        {
            if (globalpal < M32_MAXPALOOKUPS)
            {
                globalpal++;

                while (globalpal < M32_MAXPALOOKUPS && (!palookup[globalpal] || palookup[globalpal] == palookup[0]))
                    globalpal++;

                if (globalpal == M32_MAXPALOOKUPS)
                    globalpal = 0;
            }
            else globalpal = 0;
        }

        if (PRESSED_KEYSC(BS))
            globalpal = 0;

        // 'V'  KEYPRESS
        if (PRESSED_KEYSC(V))
            tileNum = SelectAllTiles(tileNum);

        // 'G'  KEYPRESS - Goto frame
        if (PRESSED_KEYSC(G))
        {
            if (eitherCTRL)
            {
                if (OnSaveTileGroup() == 0)
                {
//                    tileNum = SelectAllTiles(tileNum);
                    Bmemset(tilemarked, 0, sizeof(tilemarked));
                    mark_lastk = -1;
                    noTilesMarked = 1;
                }
            }
            else
                tileNum = OnGotoTile(tileNum);
        }

        // change palette
        if (PRESSED_KEYSC(P))
        {
            Bsprintf(tempbuf, "%s pal: ", Typestr[searchstat]);

            int pal;

            do
            {
                pal = getnumber256(tempbuf, globalpal, M32_MAXPALOOKUPS, 0+2+16);
            } while (pal != 0 && (palookup[pal] == palookup[0] || palookup[pal] == NULL));

            globalpal = pal;
        }

        // 'U'  KEYPRESS : go straight to user defined art
        if (PRESSED_KEYSC(U))
        {
            SelectAllTiles(tileNum);
            tileNum = FIRST_USER_ART_TILE;
        }

        // 'A'  KEYPRESS : Go straight to start of Atomic edition's art
        if (PRESSED_KEYSC(A))
        {
            SelectAllTiles(tileNum);
            tileNum = FIRST_ATOMIC_TILE;
        }

        // 'E'  KEYPRESS : Go straight to start of extended art
        if (PRESSED_KEYSC(E))
        {
            SelectAllTiles(tileNum);

            if (tileNum == FIRST_EXTENDED_TILE)
                tileNum = SECOND_EXTENDED_TILE;
            else tileNum = FIRST_EXTENDED_TILE;
        }

        // 'T' KEYPRESS = Select from pre-defined tileset
        if (PRESSED_KEYSC(T))
            tileNum = OnSelectTile(tileNum);

        if (PRESSED_KEYSC(Z))
            s_TileZoom = !s_TileZoom;

        //
        // Ensure tilenum is within valid range
        //
        tileNum = clamp(tileNum, 0, min(MAXTILES-1, localartlookupnum+nDisplayedTiles-1));

        // 'S' KEYPRESS: search for named tile
        if (PRESSED_KEYSC(S))
        {
            static char laststr[25] = "";
            const char *searchstr = getstring_simple("Search for tile name: ", laststr, sizeof(names[0])-1, 1);
            static char buf[2][25];

            if (searchstr && searchstr[0])
            {
                int32_t i, i0, slen=Bstrlen(searchstr)-1;

                Bstrncpyz(laststr, searchstr, 25);
                i0 = localartlookup[tileNum];

                Bmemcpy(buf[0], laststr, 25);
                Bstrupr(buf[0]);

                for (i=(i0+1)%MAXTILES; i!=i0; i=(i+1)%MAXTILES)
                {
                    Bmemcpy(buf[1], names[i], 25);
                    buf[1][24]=0;
                    Bstrupr(buf[1]);

                    if ((searchstr[0]=='^' && !Bstrncmp(buf[1], buf[0]+1, slen)) ||
                        (searchstr[0]!='^' && Bstrstr(buf[1], buf[0])))
                    {
                        SelectAllTiles(i);
                        tileNum = i;
                        break;
                    }
                }
            }

            g_mousePos.x = g_mousePos.y = g_mouseBits = 0;
        }

        //
        //	Adjust top-left to ensure tilenum is within displayed range of tiles
        //

        while (tileNum < iTopLeftTile - (moffset<0)?nXTiles:0)
            iTopLeftTile -= nXTiles;

        while (tileNum >= iTopLeftTile + nDisplayedTiles)
            iTopLeftTile += nXTiles;

        iTopLeftTile = clamp(iTopLeftTile, 0, MAXTILES - nDisplayedTiles);


        // SPACE keypress: mark/unmark selected tile
        if (PRESSED_KEYSC(SPACE))
        {
            if (tileNum < localartlookupnum && IsValidTile(localartlookup[tileNum]))
            {
                if (keystatus[KEYSC_LCTRL] && keystatus[KEYSC_RSHIFT])
                {
                    Bmemset(tilemarked, 0, sizeof(tilemarked));
                    mark_lastk = -1;
                    noTilesMarked = 1;
                }
                else
                {
                    int32_t k=tileNum, kend, dir;

                    if (noTilesMarked)
                    {
                        noTilesMarked = 0;
                        TMPERRMSG_PRINT("Beginning marking tiles. To group, press Ctrl-G."
                                        " To reset, press LCtrl-RShift-SPACE.");
                    }

                    if (mark_lastk>=0 && eitherCTRL)
                    {
                        kend = mark_lastk;
                        dir = ksgn(mark_lastk-k);
                    }
                    else
                    {
                        kend = k;
                        dir = 0;
                    }

                    mark_lastk = k;

                    for (; dir==0 || dir*(kend-k)>=1; k+=dir)
                    {
                        tilemarked[localartlookup[k]>>3] ^= (1<<(localartlookup[k]&7));
                        if (dir==0)
                            break;
                    }
                }
            }
        }

        if (mtile!=tileNum) // if changed by keyboard, update mouse cursor
        {
            searchx = ((tileNum-iTopLeftTile)%nXTiles) * zoomsz + zoomsz/2;
            searchy = ((tileNum-iTopLeftTile)/nXTiles) * zoomsz + zoomsz/2 + moffset;
        }
    }

    if ((keystatus[KEYSC_ENTER] || (bstatus&1)) == 0)
    {
        idSelectedTile = idInitialTile;
        globalpal      = idInitialPal;
    }
    else
    {
        if (tileNum < localartlookupnum)
        {
            // Convert tile num from index to actual tile num
            idSelectedTile = localartlookup[tileNum];

            // Check : if invalid tile selected, return original tile num
            if (!IsValidTile(idSelectedTile))
            {
                idSelectedTile = idInitialTile;
                globalpal      = idInitialPal;
            }
        }
        else
        {
            idSelectedTile = idInitialTile;
            globalpal      = idInitialPal;
        }
    }

    searchx=omousex;
    searchy=omousey;

    keystatus[KEYSC_ESC] = 0;
    keystatus[KEYSC_ENTER] = 0;

    popDisableFog();

    return idSelectedTile;
}