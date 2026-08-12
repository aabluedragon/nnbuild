static int32_t OnGotoTile(int32_t tileNum)
{
    //Automatically press 'V'
    tileNum = SelectAllTiles(tileNum);

    return getnumber256("Goto tile: ", 0, MAXTILES-1, 0+2+16);
}


static int32_t LoadTileSet(const int32_t idCurrentTile, const int32_t *pIds, const int32_t nIds)
{
    int32_t iNewTile = 0;
    int32_t i;

    localartlookupnum = nIds;

    for (i = 0; i < localartlookupnum; i++)
    {
        localartlookup[i] = pIds[i];
        // REM : Could we still utilise localartfreq[] to mark
        //  which tiles are currently used in the map ? Set to 0xFFFF perhaps ?
        localartfreq[i] = 0;

        if (idCurrentTile == pIds[i])
            iNewTile = i;
    }

    return iNewTile;
}

static int32_t OnSelectTile(int32_t tileNum)
{
    int32_t bDone = 0;
    int32_t i;
    char ch;

    if (tile_groups <= 0)
    {
        TMPERRMSG_PRINT("No tile groups loaded. Check for existence of `%s'.", default_tiles_cfg);
        return tileNum;
    }

    SelectAllTiles(tileNum);

    keyFlushChars();

    polymostSet2dView();
#ifdef M32_TILE_HELPERS_AFTER_POLYMOST_2D_VIEW
    M32_TILE_HELPERS_AFTER_POLYMOST_2D_VIEW();
#endif
    videoClearViewableArea(-1);

    //
    //	Await appropriate selection keypress.
    //

    bDone = 0;

    while (keystatus[KEYSC_ESC] == 0 && !bDone)
    {
        if (handleevents())
            quitevent = 0;

        idle_waitevent();

        //
        // Display the description strings for each available tile group
        //
        int32_t j = 0;

        for (i = 0; i < tile_groups; i++)
        {
            if (s_TileGroups[i].szText != NULL)
            {
                if ((j+2)*16 > ydimgame) break;

                if (s_TileGroups[i].key1)
                {
                    Bsprintf(tempbuf, "(%c) %s", s_TileGroups[i].key1, s_TileGroups[i].szText);
                    printext256(10L, (j+1)*16, whitecol, -1, tempbuf, 0);
                    j++;
                }
            }
        }
        videoShowFrame(1);

        ch = keyGetChar();

        for (i = 0; i < tile_groups; i++)
        {
            if (s_TileGroups[i].pIds != NULL && s_TileGroups[i].key1)
                if (ch == s_TileGroups[i].key1 || ch == s_TileGroups[i].key2)
                {
                    tileNum = LoadTileSet(tileNum, s_TileGroups[i].pIds, s_TileGroups[i].nIds);
                    bDone = 1;
                }
        }
    }

    videoShowFrame(1);

    clearkeys();

    return tileNum;
}

static const char *GetTilePixels(int32_t idTile)
{
    char *pPixelData = 0;

    if (idTile >= 0 && idTile < MAXTILES)
    {
        if (!waloff[idTile])
            tileLoad(idTile);

        if (IsValidTile(idTile))
            pPixelData = (char *)waloff[idTile];
    }

    return pPixelData;
}

static void classic_drawtilescreen(int32_t x, int32_t y, int32_t idTile, int32_t TileDim,
                                   const char *pRawPixels)
{
    int32_t dispxsz = tilesiz[idTile].x, dispysz = tilesiz[idTile].y;
    int32_t divinc = 1, mulinc = 1;

    int32_t xofs, yofs;
    char *pScreen;

    while ((dispxsz/divinc > TileDim) || (dispysz/divinc) > TileDim)
    {
        divinc++;
    }

    if (divinc == 1 && s_TileZoom)
    {
        while ((dispxsz*(mulinc+1)) <= TileDim && (dispysz*(mulinc+1)) <= TileDim)
        {
            mulinc++;
        }
    }

    dispxsz = (dispxsz / divinc) * mulinc;
    dispysz = (dispysz / divinc) * mulinc;

    for (yofs = 0; yofs < dispysz; yofs++)
    {
        y += yofs;
        if (y>=0 && y<ydim)
        {
            pScreen = (char *)ylookup[y]+x+frameplace;
            for (xofs = 0; xofs < dispxsz; xofs++)
            {
                pScreen[xofs] = palookup[globalpal][pRawPixels[((yofs*divinc)/mulinc) + (((xofs*divinc)/mulinc)*tilesiz[idTile].y)]];
            }
        }
        y -= yofs;
    }
}

static void tilescreen_drawbox(int32_t iTopLeft, int32_t iSelected, int32_t nXTiles,
                               int32_t TileDim, int32_t offset,
                               int32_t tileNum, int32_t idTile)
{
    int32_t marked = (IsValidTile(idTile) && tilemarked[idTile>>3]&(1<<(idTile&7)));

    //
    // Draw white box around currently selected tile or marked tile
    // p1=(x1, y1), p2=(x1+TileDim-1, y1+TileDim-1)
    //
    if (tileNum == iSelected || marked)
    {
        int32_t x1 = ((tileNum-iTopLeft) % nXTiles)*TileDim;
        int32_t y1 = ((tileNum - ((tileNum-iTopLeft) % nXTiles) - iTopLeft)/nXTiles)*TileDim + offset;
        int32_t x2 = x1+TileDim-1;
        int32_t y2 = y1+TileDim-1, oydim16=ydim16;

        char markedcol = editorcolors[14];

        polymostSet2dView();

        y1=max(y1, 0);
        y2=min(y2, ydim-1);

        // plotlines2d uses drawline16, which clips against ydim16...
        ydim16 = ydim;

        {
            // box
            int32_t xx[] = {x1, x1, x2, x2, x1};
            int32_t yy[] = {y1, y2, y2, y1, y1};
            plotlines2d(xx, yy, 5, tileNum==iSelected ? whitecol : markedcol);
        }

        // cross
        if (marked)
        {
            int32_t xx[] = {x1, x2};
            int32_t yy[] = {y1, y2};

            plotlines2d(xx, yy, 2, markedcol);
            swaplong(&yy[0], &yy[1]);
            plotlines2d(xx, yy, 2, markedcol);
        }

        ydim16 = oydim16;
    }
}

static void tilescreen_drawrest(int32_t iSelected, int32_t showmsg)
{
    if (iSelected>=0 && iSelected<MAXTILES)
    {
        int32_t idTile = localartlookup[iSelected];
        int32_t i;
        char szT[128];

        // Draw info bar at bottom.

        // Clear out behind the text for improved visibility.
        //drawline256(0, (ydim-12)<<12, xdim<<12, (ydim-12)<<12, whitecol);
        for (i=ydim-12; i<ydim; i++)
            renderDrawLine(0, i<<12, xdim<<12, i<<12, (ydim-i));

        // Tile number on left.
        Bsprintf(szT, "Tile: %d (p)al: %d" , idTile, globalpal);
        printext256(1, ydim-10, whitecol, -1, szT, 0);

        // Tile name on right.
        printext256(xdim-(Bstrlen(names[idTile])<<3)-1,ydim-10,whitecol,-1,names[idTile],0);

        // Tile dimensions.
        Bsprintf(szT,"dim: %dx%d",tilesiz[idTile].x,tilesiz[idTile].y);
        printext256((xdim>>2)+(4<<3),ydim-10,whitecol,-1,szT,0);

        // EditArt offset flags.
        Bsprintf(szT,"offs: %d, %d", picanm[idTile].xofs, picanm[idTile].yofs);
        printext256((xdim>>2)+(4<<3)+100,ydim-10,whitecol,-1,szT,0);

        // EditArt animation flags.
        if (picanm[idTile].sf&PICANM_ANIMTYPE_MASK)
        {
            static const char *anmtype[] = {"", "Osc", "Fwd", "Bck"};
            int32_t ii = (picanm[idTile].sf&PICANM_ANIMTYPE_MASK)>>PICANM_ANIMTYPE_SHIFT;

            Bsprintf(szT,"%s %d", anmtype[ii], picanm[idTile].num);
            printext256((xdim>>2)+(4<<3)+100+14*8,ydim-10,whitecol,-1,szT,0);
        }
    }

    if (showmsg)
        TMPERRMSG_SHOW(0);

    m32_showmouse();
}
