// Shared Blood/Redneck Rampage Mapster32 editor helpers.
// Include with exactly one BLOOD_RR_MAPEDIT_SHARED_* selector defined.

#if defined(BLOOD_RR_MAPEDIT_SHARED_SOUND_DISPLAY)
static void SoundDisplay(void)
{
    if (g_numsounds <= 0) return;

    overridepm16y = ydim;//3*STATUS2DSIZ;

    {
        // cursnd is the first displayed line, cursnd+curofs is where the cursor is
        static int32_t cursnd=0, curofs=0;
        char disptext[80];

        int32_t i, j;
        const int32_t halfpage = (ydim-64)/(2*9);

        while (keystatus[KEYSC_ESC]==0 && keystatus[KEYSC_Q]==0 && keystatus[KEYSC_F2]==0
                && keystatus[buildkeys[BK_MODE2D_3D]]==0)  // quickjump to 3d mode
        {
            videoBeginDrawing();
            CLEARLINES2D(0, ydim16, 0);
            videoEndDrawing();

            idle_waitevent();
            if (handleevents())
                quitevent = 0;

//            drawgradient();

            videoBeginDrawing();
            printext16(9, ydim2d-overridepm16y+9, editorcolors[4], -1, "Sound Index", 0);
            printext16(8, ydim2d-overridepm16y+8, editorcolors[12], -1, "Sound Index", 0);
            printext16(8 + 11*8 + 2*8, ydim2d-overridepm16y+8, editorcolors[15], -1, "(SPACE:play, S:sort)", 0);
            videoEndDrawing();

            if (PRESSED_KEYSC(G))    // goto specified sound#
            {
                _printmessage16("                                                    ");
                j = getnumber16("Goto sound#: ", 0, g_numsounds-1, 0);
                for (i=0; i<g_numsounds; i++)
                    if (g_sndnum[i]==j)
                        break;
                if (i != g_numsounds)
                {
                    if (i<SOUND_NUMDISPLINES)
                        cursnd = 0, curofs = i;
                    else if (i>=g_numsounds-halfpage)
                        cursnd = g_numsounds-halfpage, curofs = i-cursnd;
                    else
                        curofs = halfpage/2, cursnd = i-curofs;
                }
            }
            else if (PRESSED_KEYSC(UP))    // scroll up
            {
                if (curofs>0) curofs--;
                else if (cursnd>0) cursnd--;
            }
            else if (PRESSED_KEYSC(DOWN))    // scroll down
            {
                if (curofs < halfpage-1 && cursnd+curofs<g_numsounds-1)
                    curofs++;
                else if (cursnd + halfpage < g_numsounds)
                    cursnd++;
            }
            else if (PRESSED_KEYSC(PGUP))    // scroll one page up
            {
                i = halfpage/2;

                while (i>0 && curofs>0)
                    i--, curofs--;
                while (i>0 && cursnd>0)
                    i--, cursnd--;
            }
            else if (PRESSED_KEYSC(PGDN))    // scroll one page down
            {
                i = halfpage/2;

                while (i>0 && curofs < halfpage-1 && cursnd+curofs<g_numsounds-1)
                    i--, curofs++;
                while (i>0 && cursnd + halfpage < g_numsounds)
                    i--, cursnd++;
            }
            else if (PRESSED_KEYSC(SPACE) || PRESSED_KEYSC(ENTER))   // play/stop sound
            {
                int32_t j = cursnd+curofs;
                int32_t k = g_sndnum[j];

                if (S_CheckSoundPlaying(0, k) > 0)
                    S_StopSound(k);
                else
                    S_PlaySound(k);
            }
            else if (PRESSED_KEYSC(HOME))    // goto first sound#
            {
                cursnd = curofs = 0;
            }
            else if (PRESSED_KEYSC(END))    // goto last sound#
            {
                if ((cursnd = g_numsounds - halfpage) >= 0)
                    curofs = halfpage-1;
                else
                {
                    cursnd = 0;
                    curofs = g_numsounds-1;
                }
            }

            _printmessage16("                          FILE NAME         PITCH RANGE  PRI FLAGS VOLUME");
            for (i=0; j=cursnd+i, i<SOUND_NUMDISPLINES && j<g_numsounds; i++)
            {
                int32_t l, m, k=g_sndnum[j];
                sound_t *snd=&g_sounds[k];
                char *cp;

                if (ydim-overridepm16y+28+i*9+32 >= ydim) break;

                Bsprintf(disptext,
                         "%4d .................... ................ %6d:%-6d %3d %c%c%c%c%c %6d",
                         //   5678901234567890X23456789012345678901234567
                         k, snd->ps, snd->pe, snd->pr,
                         snd->m&1 ? 'R':'-', snd->m&2 ? 'M':'-', snd->m&4 ? 'D':'-',
                         snd->m&8 ? 'P':'-', snd->m&16 ? 'G':'-', snd->vo);
                for (l = Bsnprintf(disptext+5, 20, "%s", snd->definedname); l<20; l++)
                    disptext[5+l] = ' ';
                if (snd->filename)
                {
                    l = Bstrlen(snd->filename);
                    if (l<=16)
                        cp = snd->filename;
                    else
                        cp = snd->filename + l-15;
                    for (m = Bsnprintf(disptext+26, 16, "%s", cp); m<16; m++)
                        disptext[26+m] = ' ';
                    if (l>16)
                        disptext[26] = disptext[27] = disptext[28] = '.';
                }

                printext16(8, ydim-overridepm16y+28+i*9,
                           keystatus[KEYSC_S]?editorcolors[8] : (S_CheckSoundPlaying(-1, k) ? editorcolors[2] : editorcolors[10]),
                           j==cursnd+curofs ? editorcolors[1] : -1,
                           disptext, 0);
            }

            if (keystatus[KEYSC_S])    // sorting
            {

                char ch, bad=0;

                _printmessage16("Sort by: (S)oundnum (D)ef (F)ile ori(g) or flags (12345)");
                videoShowFrame(1);

                i=0;
                keyFlushChars();
                while (bad == 0)
                {
                    idle_waitevent();
                    if (handleevents())
                        quitevent = 0;

                    ch = keyGetChar();

                    if (keystatus[sc_Escape]) bad = 1;

                    else if (ch == 's' || ch == 'd' || ch == 'f' || ch == 'g' ||
                             ch == '1' || ch == '2' || ch == '3' || ch == '4' || ch == '5')
                    {
                        bad = 2;
                        sort_sounds(ch);
                    }
                }

                clearkeys();
            }
            else
                videoShowFrame(1);
        }

        overridepm16y = -1;

        FX_StopAllSounds();
        S_ClearSoundLocks();

        clearkeys();
    }
}
#undef BLOOD_RR_MAPEDIT_SHARED_SOUND_DISPLAY
#endif

#if defined(BLOOD_RR_MAPEDIT_SHARED_ON_SAVE_TILE_GROUP)
static int32_t OnSaveTileGroup(void)
{
    int32_t i, n=0;
    char hotkey;
    const char *cp, *name;

    if (tile_groups==MAX_TILE_GROUPS)
        TMPERRMSG_RETURN("Cannot save tile group: maximum number of groups (%d) exceeded.", MAX_TILE_GROUPS);

    for (i=0; i<MAXTILES; i++)
        n += !!(tilemarked[i>>3]&(1<<(i&7)));

    if (n==0)
        TMPERRMSG_RETURN("Cannot save tile group: no tiles marked.");
    else if (n > MAX_TILE_GROUP_ENTRIES)
        TMPERRMSG_RETURN("Cannot save tile group: too many tiles in group. Have %d, max is %d.",
                  n, MAX_TILE_GROUP_ENTRIES);

    cp = getstring_simple("Hotkey for new group: ", "", 1, 0);
    if (!cp || !*cp)
        return 1;

    hotkey = Btoupper(cp[0]);
    if (!isalpha(hotkey))
        TMPERRMSG_RETURN("Hotkey must be alphabetic.");

    for (i=0; i<tile_groups; i++)
        if (s_TileGroups[i].key1==hotkey || s_TileGroups[i].key2==Btolower(hotkey))
            TMPERRMSG_RETURN("Hotkey '%c' already in use by tile group `%s'.", hotkey, s_TileGroups[i].szText);

    name = getstring_simple("Name for new tile group: ", "", 63, 0);
    if (!name || !*name)
        return 1;

    for (i=0; name[i]; i++)
        if (!(isalnum(name[i]) || name[i]==' ' || name[i]==','))
            TMPERRMSG_RETURN("Name may only consist of alphabetic, numeric and space characters.");

    {
        int32_t lasti=-1, col=0, j, k, opathsearchmode=pathsearchmode;
        BFILE *fp;

        pathsearchmode = pathsearchmode_oninit;  // use the same pathsearchmode as on init
        fp = fopenfrompath(default_tiles_cfg, "a");
        pathsearchmode = opathsearchmode;
        if (!fp)
            TMPERRMSG_RETURN("Could not open `%s' for writing: %s.", default_tiles_cfg, strerror(errno));
        if (fseek(fp, 0, BSEEK_END))  // seems to be necessary even though we fopen with "a"
            TMPERRMSG_RETURN("Could not seek to end of file `%s'.", default_tiles_cfg);

#define TTAB "\t"
#define TBITCHK(i) ((i)<MAXTILES && (tilemarked[(i)>>3]&(1<<((i)&7))))
        Bfprintf(fp, OURNEWL);
        Bfprintf(fp, "tilegroup \"%s\"" OURNEWL"{" OURNEWL, name);
        Bfprintf(fp, TTAB "hotkey \"%c\"" OURNEWL OURNEWL, hotkey);

        s_TileGroups[tile_groups].pIds = (int32_t *)Xmalloc(n * sizeof(s_TileGroups[tile_groups].pIds[0]));

        j = 0;
        // tileranges for consecutive runs of 3 or more tiles
        for (i=0; i<MAXTILES; i++)
        {
            if (lasti>=0 && !TBITCHK(i))
            {
                if (names[lasti][0] && names[i-1][0])
                    Bfprintf(fp, TTAB "tilerange %s %s" OURNEWL, names[lasti], names[i-1]);
                else
                    Bfprintf(fp, TTAB "tilerange %d %d" OURNEWL, lasti, i-1);

                for (k=lasti; k<i; k++)
                {
                    s_TileGroups[tile_groups].pIds[j++] = k;
                    tilemarked[k>>3] &= ~(1<<(k&7));
                }

                lasti = -1;
            }
            else if (lasti==-1 && TBITCHK(i))
            {
                if (TBITCHK(i+1) && TBITCHK(i+2))
                {
                    lasti = i;
                    i += 2;
                }
            }
        }
        if (lasti>=0 && lasti<=MAXTILES-3)
        {
            for (k=lasti; k<MAXTILES; k++)
            {
                s_TileGroups[tile_groups].pIds[j++] = k;
                tilemarked[k>>3] &= ~(1<<(k&7));
            }
            Bfprintf(fp, TTAB "tilerange %d %d" OURNEWL, lasti, MAXTILES-1);
        }
        Bfprintf(fp, OURNEWL);

        k = 0;
        for (i=0; i<MAXTILES; i++)
            if (tilemarked[i>>3]&(1<<(i&7)))
            {
                k = 1;
                break;
            }

        if (k)
        {
            // throw them all in a tiles{...} group else
            Bfprintf(fp, TTAB "tiles\n" TTAB "{" OURNEWL);
            for (i=0; i<MAXTILES; i++)
            {
                if (TBITCHK(i))
                {
                    s_TileGroups[tile_groups].pIds[j++] = i;

                    if (col==0)
                        Bfprintf(fp, TTAB TTAB), col+=8;

                    if (names[i][0])
                        col+=Bfprintf(fp, "%s ", names[i]);
                    else
                        col+=Bfprintf(fp, "%d ", i);

                    if (col>80)
                    {
                        Bfprintf(fp, OURNEWL);
                        col = 0;
                    }
                }
            }
            if (col>0)
                Bfprintf(fp, OURNEWL);
            Bfprintf(fp, TTAB "}" OURNEWL);
        }
#undef TBITCHK
#undef TTAB
        Bfprintf(fp, "}" OURNEWL);

        Bfclose(fp);

        s_TileGroups[tile_groups].szText = Xstrdup(name);

        s_TileGroups[tile_groups].nIds = n;
        s_TileGroups[tile_groups].key1 = Btoupper(hotkey);
        s_TileGroups[tile_groups].key2 = Btolower(hotkey);
        s_TileGroups[tile_groups].color1 = s_TileGroups[tile_groups].color2 = 0;
        tile_groups++;

        TMPERRMSG_PRINT("Wrote and installed new tile group.");
    }

    return 0;
}
#undef BLOOD_RR_MAPEDIT_SHARED_ON_SAVE_TILE_GROUP
#endif

#if defined(BLOOD_RR_MAPEDIT_SHARED_EDIT_SECTOR_DATA)
static void EditSectorData(int16_t sectnum)
{
    int32_t col=1, row=0, rowmax = 6;
    int32_t i = -1;
    int32_t xpos = 208, ypos = ydim-STATUS2DSIZ+48;

    med_editval = 0;

    med_dispwidth = 24;
    med_disptext[med_dispwidth] = 0;

    med_typename = "Sector";
    med_thenum = sectnum;

    drawgradient();

    showsectordata(sectnum, 0);

    while (keystatus[KEYSC_ESC] == 0)
    {
        med_handlecommon(xpos, ypos, &row, rowmax);

        if (PRESSED_KEYSC(LEFT))
        {
            if (col == 2)
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 1;
                xpos = 208;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
        }
        if (PRESSED_KEYSC(RIGHT))
        {
            if (col == 1)
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 2;
                xpos = 408;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
        }

#ifdef YAX_ENABLE
        if (med_editval)
        {
            if ((row==1 || row==3 || row==5) && yax_getbunch(sectnum, (col==2)) >= 0)
                med_editval = 0;
        }
#endif

        if (col == 1)
        {
            switch (row)
            {
            case 0:
#ifdef YAX_ENABLE__COMPAT
                i = sector[sectnum].ceilingstat&YAX_BIT;
#endif
                handlemed(1, "Flags (hex)", "Ceiling Flags", &sector[sectnum].ceilingstat,
                          sizeof(sector[sectnum].ceilingstat), 65535, 0);
#ifdef YAX_ENABLE__COMPAT
                sector[sectnum].ceilingstat &= ~YAX_BIT;
                sector[sectnum].ceilingstat |= i;
#endif
                break;
            case 1:
                i = Bsprintf(med_disptext,"(X,Y)pan: %d, %d",
                             TrackerCast(sector[sectnum].ceilingxpanning),
                             TrackerCast(sector[sectnum].ceilingypanning));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sector %d Ceiling X Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].ceilingxpanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].ceilingxpanning,255,0);
                    Bsprintf(med_edittext,"Sector %d Ceiling Y Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].ceilingypanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].ceilingypanning,255,0);
                }
                break;
            case 2:
                handlemed(0, "Shade byte", "Ceiling Shade", &sector[sectnum].ceilingshade,sizeof(sector[sectnum].ceilingshade), 128, 1);
                break;
            case 3:
                handlemed(0, "Z-coordinate", "Ceiling Z-coordinate", &sector[sectnum].ceilingz,
                          sizeof(sector[sectnum].ceilingz), BZ_MAX, 1);
                break;
            case 4:
                handlemed(0, "Tile number", "Ceiling Tile Number", &sector[sectnum].ceilingpicnum,
                          sizeof(sector[sectnum].ceilingpicnum), MAXTILES, 0);
                break;
            case 5:
                handlemed(0, "Ceiling heinum", "Ceiling Heinum", &sector[sectnum].ceilingheinum,
                          sizeof(sector[sectnum].ceilingheinum), BHEINUM_MAX, 1);
                setslope(sectnum, YAX_CEILING, sector[sectnum].ceilingheinum);
                break;
            case 6:
                handlemed(0, "Palookup number", "Ceiling Palookup Number", &sector[sectnum].ceilingpal,
                          sizeof(sector[sectnum].ceilingpal), M32_MAXPALOOKUPS, 0);
                break;
            }
        }
        else if (col == 2)
        {
            switch (row)
            {
            case 0:
#ifdef YAX_ENABLE__COMPAT
                i = sector[sectnum].ceilingstat&YAX_BIT;
#endif
                handlemed(1, "Flags (hex)", "Floor Flags", &sector[sectnum].floorstat,
                          sizeof(sector[sectnum].floorstat), 65535, 0);
#ifdef YAX_ENABLE__COMPAT
                sector[sectnum].ceilingstat &= ~YAX_BIT;
                sector[sectnum].ceilingstat |= i;
#endif
                break;

            case 1:
                i = Bsprintf(med_disptext,"(X,Y)pan: %d, %d",
                             TrackerCast(sector[sectnum].floorxpanning),
                             TrackerCast(sector[sectnum].floorypanning));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sector %d Floor X Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].floorxpanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].floorxpanning,255,0);
                    Bsprintf(med_edittext,"Sector %d Floor Y Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].floorypanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].floorypanning,255,0);
                }
                break;

            case 2:
                handlemed(0, "Shade byte", "Floor Shade", &sector[sectnum].floorshade,
                          sizeof(sector[sectnum].floorshade), 128, 1);
                break;
            case 3:
                handlemed(0, "Z-coordinate", "Floor Z-coordinate", &sector[sectnum].floorz,
                          sizeof(sector[sectnum].floorz), BZ_MAX, 1); //2147483647L,-2147483648L
                break;
            case 4:
                handlemed(0, "Tile number", "Floor Tile Number", &sector[sectnum].floorpicnum,
                          sizeof(sector[sectnum].floorpicnum), MAXTILES, 0);
                break;
            case 5:
                handlemed(0, "Floor heinum", "Floor Heinum", &sector[sectnum].floorheinum,
                          sizeof(sector[sectnum].floorheinum), BHEINUM_MAX, 1);
                setslope(sectnum, YAX_FLOOR, sector[sectnum].floorheinum);
                break;
            case 6:
                handlemed(0, "Palookup number", "Floor Palookup Number", &sector[sectnum].floorpal,
                          sizeof(sector[sectnum].floorpal), M32_MAXPALOOKUPS, 0);
                break;
            }
        }

        med_printcurline(xpos, ypos, row, 1);
        if (med_editval)
            med_editval = 0;

        videoShowFrame(1);
    }

    med_printcurline(xpos, ypos, row, 0);
    // printmessage16("");

    videoShowFrame(1);
    keystatus[KEYSC_ESC] = 0;
}
#undef BLOOD_RR_MAPEDIT_SHARED_EDIT_SECTOR_DATA
#endif

#if defined(BLOOD_RR_MAPEDIT_SHARED_EDIT_WALL_DATA)
static void EditWallData(int16_t wallnum)
{
    int32_t row=0;
    int32_t i = -1;
    int32_t xpos = 208, ypos = ydim-STATUS2DSIZ+48;

    med_editval = 0;

    med_dispwidth = 24;
    med_disptext[med_dispwidth] = 0;

    med_typename = "Wall";
    med_thenum = wallnum;

    drawgradient();

    showwalldata(wallnum, 0);

    while (keystatus[KEYSC_ESC] == 0)
    {
        med_handlecommon(xpos, ypos, &row, 6);

        switch (row)
        {
        case 0:
#if !defined NEW_MAP_FORMAT
            i = wall[wallnum].cstat&YAX_NEXTWALLBITS;
#endif
            handlemed(1, "Flags (hex)", "Flags", &wall[wallnum].cstat,
                      sizeof(wall[wallnum].cstat), 65535, 0);
#if !defined NEW_MAP_FORMAT
            wall[wallnum].cstat &= ~YAX_NEXTWALLBITS;
            wall[wallnum].cstat |= i;
#endif
            break;
        case 1:
            handlemed(0, "Shade", "Shade", &wall[wallnum].shade,
                      sizeof(wall[wallnum].shade), 128, 1);
            break;
        case 2:
            handlemed(0, "Pal", "Pal", &wall[wallnum].pal,
                      sizeof(wall[wallnum].pal), M32_MAXPALOOKUPS, 0);
            break;
        case 3:
            i = Bsprintf(med_disptext,"(X,Y)repeat: %d, %d",
                         TrackerCast(wall[wallnum].xrepeat),
                         TrackerCast(wall[wallnum].yrepeat));
            for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
            if (med_editval)
            {
                Bsprintf(med_edittext,"Wall %d X Repeat: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].xrepeat = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].xrepeat,255,0);
                Bsprintf(med_edittext,"Wall %d Y Repeat: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].yrepeat = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].yrepeat,255,0);
            }
            break;
        case 4:
            i = Bsprintf(med_disptext,"(X,Y)pan: %d, %d",
                         TrackerCast(wall[wallnum].xpanning),
                         TrackerCast(wall[wallnum].ypanning));
            for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
            if (med_editval)
            {
                Bsprintf(med_edittext,"Wall %d X Pan: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].xpanning = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].xpanning,255,0);
                Bsprintf(med_edittext,"Wall %d Y Pan: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].ypanning = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].ypanning,255,0);
            }
            break;
        case 5:
            handlemed(0, "Tile number", "Tile number", &wall[wallnum].picnum,
                      sizeof(wall[wallnum].picnum), MAXTILES, 0);
            break;

        case 6:
            handlemed(0, "OverTile number", "OverTile number", &wall[wallnum].overpicnum,
                      sizeof(wall[wallnum].overpicnum), MAXTILES, 0);
            break;
        }

        med_printcurline(xpos, ypos, row, 1);
        if (med_editval)
        {
            med_editval = 0;
            //showwalldata(wallnum, 0);
            //// printmessage16("");
        }

        videoShowFrame(1);
    }

    med_printcurline(xpos, ypos, row, 0);
    // printmessage16("");

    videoShowFrame(1);
    keystatus[KEYSC_ESC] = 0;
}
#undef BLOOD_RR_MAPEDIT_SHARED_EDIT_WALL_DATA
#endif

#if defined(BLOOD_RR_MAPEDIT_SHARED_EDIT_SPRITE_DATA)
static void EditSpriteData(int16_t spritenum)
{
    int32_t col=0, row=0, rowmax=4;
    int32_t xpos = 8, ypos = ydim-STATUS2DSIZ+48;

    med_editval = 0;

    med_dispwidth = 24;
    med_disptext[med_dispwidth] = 0;

    med_typename = "Sprite";
    med_thenum = spritenum;

    drawgradient();

    //    clearmidstatbar16();

    while (keystatus[KEYSC_ESC] == 0)
    {
        showspritedata(spritenum, 0);

        med_handlecommon(xpos, ypos, &row, rowmax);

        if (PRESSED_KEYSC(LEFT))
        {
            switch (col)
            {
            case 1:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 0;
                xpos = 8;
                rowmax = 4;
                med_dispwidth = 23;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            case 2:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 1;
                xpos = 208;
                rowmax = 6;
                med_dispwidth = 24;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            }
        }
        if (PRESSED_KEYSC(RIGHT))
        {
            switch (col)
            {
            case 0:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 1;
                xpos = 208;
                rowmax = 6;
                med_dispwidth = 24;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            case 1:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 2;
                xpos = 408;
                rowmax = 6;
                med_dispwidth = 26;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            }
        }

        switch (col)
        {
        case 0:
        {
            switch (row)
            {
            case 0:
                handlemed(0, "X-coordinate", "X-coordinate", &sprite[spritenum].x,
                          sizeof(sprite[spritenum].x), 131072, 1);
                break;
            case 1:
                handlemed(0, "Y-coordinate", "Y-coordinate", &sprite[spritenum].y,
                          sizeof(sprite[spritenum].y), 131072, 1);
                break;
            case 2:
                handlemed(0, "Z-coordinate", "Z-coordinate", &sprite[spritenum].z,
                          sizeof(sprite[spritenum].z), BZ_MAX, 1); //2147483647L,-2147483648L
                break;
            case 3:
            {
                int16_t i = sprite[spritenum].sectnum;
                handlemed(0, "Sectnum", "Sectnum", &sprite[spritenum].sectnum,
                          sizeof(sprite[spritenum].sectnum), numsectors-1, 0);
                if (i != sprite[spritenum].sectnum)
                {
                    swapshort(&i, &sprite[spritenum].sectnum);
                    changespritesect(spritenum,i);
                }
                break;
            }
            case 4:
            {
                int16_t i = sprite[spritenum].statnum;
                handlemed(0, "Statnum", "Statnum", &sprite[spritenum].statnum,
                          sizeof(sprite[spritenum].statnum), MAXSTATUS-1, 0);
                if (i != sprite[spritenum].statnum)
                {
                    swapshort(&i, &sprite[spritenum].statnum);
                    changespritestat(spritenum,i);
                }
                break;
            }
            }
        }
        break;
        case 1:
        {
            switch (row)
            {
            case 0:
                handlemed(1, "Flags (hex)", "Flags", &sprite[spritenum].cstat,
                          sizeof(sprite[spritenum].cstat), 65535, 0);
                break;
            case 1:
                handlemed(0, "Shade", "Shade", &sprite[spritenum].shade,
                          sizeof(sprite[spritenum].shade), 128, 1);
                break;
            case 2:
                handlemed(0, "Pal", "Pal", &sprite[spritenum].pal,
                          sizeof(sprite[spritenum].pal), M32_MAXPALOOKUPS, 0);
                break;
            case 3:
                handlemed(0, "Blend", "Blend", &sprite[spritenum].blend,
                          sizeof(sprite[spritenum].blend), MAXBLENDTABS, 0);
                break;
            case 4:
            {
                int32_t i = Bsprintf(med_disptext,"(X,Y)repeat: %d, %d",
                             TrackerCast(sprite[spritenum].xrepeat),
                             TrackerCast(sprite[spritenum].yrepeat));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sprite %d X Repeat: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].xrepeat = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].xrepeat,255,0);
                    Bsprintf(med_edittext,"Sprite %d Y Repeat: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].yrepeat = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].yrepeat,255,0);
                }
            }
            break;
            case 5:
            {
                int32_t i = Bsprintf(med_disptext,"(X,Y)offset: %d, %d",
                             TrackerCast(sprite[spritenum].xoffset),
                             TrackerCast(sprite[spritenum].yoffset));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sprite %d X Offset: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].xoffset = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].xoffset,128,1);
                    Bsprintf(med_edittext,"Sprite %d Y Offset: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].yoffset = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].yoffset,128,1);
                }
            }
            break;
            case 6:
                handlemed(0, "Tile number", "Tile number", &sprite[spritenum].picnum,
                          sizeof(sprite[spritenum].picnum), MAXTILES-1, 0+2);
                break;
            }
        }
        break;
        case 2:
        {
            switch (row)
            {
            case 0:
                handlemed(0, "Angle (2048 degrees)", "Angle", &sprite[spritenum].ang,
                          sizeof(sprite[spritenum].ang), 2048, 1);
                if (med_editval)
                    sprite[spritenum].ang &= 2047;
                break;
            case 1:
                handlemed(0, "X-Velocity", "X-Velocity", &sprite[spritenum].xvel,
                          sizeof(sprite[spritenum].xvel), 65535, 1);
                break;
            case 2:
                handlemed(0, "Y-Velocity", "Y-Velocity", &sprite[spritenum].yvel,
                          sizeof(sprite[spritenum].yvel), 65535, 1);
                break;
            case 3:
                handlemed(0, "Z-Velocity", "Z-Velocity", &sprite[spritenum].zvel,
                          sizeof(sprite[spritenum].zvel), 65535, 1);
                break;
            case 4:
                handlemed(0, "Owner", "Owner", &sprite[spritenum].owner,
                          sizeof(sprite[spritenum].owner), MAXSPRITES-1, 1);
                break;
            case 5:
                handlemed(0, "Clipdist", "Clipdist", &sprite[spritenum].clipdist,
                          sizeof(sprite[spritenum].clipdist), 255, 0);
                break;
            case 6:
                handlemed(0, "Extra", "Extra", &sprite[spritenum].extra,
                          sizeof(sprite[spritenum].extra), BTAG_MAX, 1);
                break;
            }
        }
        break;
        }

        med_printcurline(xpos, ypos, row, 1);
        if (med_editval)
            med_editval = 0;

        videoShowFrame(1);
    }

    med_printcurline(xpos, ypos, row, 0);
    // printmessage16("");
    videoShowFrame(1);

    keystatus[KEYSC_ESC] = 0;
}
#undef BLOOD_RR_MAPEDIT_SHARED_EDIT_SPRITE_DATA
#endif

#if defined(BLOOD_RR_MAPEDIT_SHARED_GENERIC_SPRITE_SEARCH)
static void GenericSpriteSearch(void)
{
    char disptext[80];
    char edittext[80];
    static int32_t col=0, row=0;
    int32_t i, j, k;
    int32_t rowmax[3]= {6,6,6}, dispwidth[3] = {24,24,28};
    int32_t xpos[3] = {8,200,400}, ypos = ydim-STATUS2DSIZ+48;

    static const char *labels[7][3] =
    {
        {"X-coordinate", "Flags (hex)", "Angle (2048 degrees)"},
        {"Y-coordinate", "Shade",       "X-Velocity"},
        {"Z-coordinate", "Pal",         "Y-Velocity"},
        {"Sectnum",      "Blend",       "Z-Velocity"},
        {"Statnum",      "(X/Y)repeat", "Owner"},
        {"Hitag",        "(X/Y)offset", "Clipdist"},
        {"Lotag",        "Tile number", "Extra"}
    };

    static int32_t maxval[7][3] =
    {
        { BXY_MAX     , 65535         , 2048 },
        { BXY_MAX     , 128           , 65535 },
        { BZ_MAX      , M32_MAXPALOOKUPS, 65535 },
        { MAXSECTORS-1, MAXBLENDTABS-1, 65535 },
        { MAXSTATUS-1 , 128           , MAXSPRITES-1 },
        { BTAG_MAX    , 128           , 256 },
        { BTAG_MAX    , MAXTILES-1    , BTAG_MAX }
    };

    static char sign[7][3] =
    {
        {1,   0,   1},
        {1,   1,   1},
        {1,   0,   1},
        {0,   0,   1},
        {0,   0,   0},
        {0+4, 1,   0},
        {0+4, 0+2, 1}
    };

    clearmidstatbar16();

    drawgradient();

    printext16(xpos[0], ypos-2*8, editorcolors[10], editorcolors[0], "Sprite search", 0);

    for (i=0; i<3; i++)
        for (j=0; j<=rowmax[i]; j++)
        {
            if (gs_spriteTagInterested[i][j])
                k=Bsprintf(disptext, "%s: %d", labels[j][i], gs_spriteTagValue[i][j]);
            else
                k=Bsprintf(disptext, "%s: ^7any", labels[j][i]);
//            for (; k<dispwidth[i]; k++) disptext[k] = 0;

            printext16(xpos[i], ypos+j*8, editorcolors[11], editorcolors[0], disptext, 0);
        }
    Bmemset(disptext, 0, sizeof(disptext));

    //    disptext[dispwidth[col]] = 0;
    //    showspritedata(spritenum, 0);
    wallsprite = 2;

    while (keystatus[KEYSC_ESC] == 0)
    {
        idle_waitevent();
        if (handleevents())
        {
            if (quitevent) quitevent = 0;
        }

        printmessage16("Sprite search, press <Esc> to exit");

        if (PRESSED_KEYSC(DOWN))
        {
            if (row < rowmax[col])
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                row++;
            }
        }
        if (PRESSED_KEYSC(UP))
        {
            if (row > 0)
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                row--;
            }
        }
        if (PRESSED_KEYSC(LEFT))
        {
            if (col > 0)
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                col--;
                disptext[dispwidth[col]] = 0;
                if (row > rowmax[col]) row = rowmax[col];
            }
        }
        if (PRESSED_KEYSC(RIGHT))
        {
            if (col < 2)
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                col++;
                disptext[dispwidth[col]] = 0;
                if (row > rowmax[col]) row = rowmax[col];
            }
        }
        if (PRESSED_KEYSC(ENTER))
        {
            Bsprintf(edittext, "%s: ", labels[row][col]);
            printmessage16("%s", edittext);
            i = getnumber16(edittext, gs_spriteTagInterested[col][row] ? gs_spriteTagValue[col][row] : 0,
                            maxval[row][col], sign[row][col]);
            if (col == 2 && row == 0) i = (i+2048)&2047;  // angle
            gs_spriteTagValue[col][row] = i;
            gs_spriteTagInterested[col][row] = 1;

            if (col == 1 && row == 6)  // picnum
            {
                printext16(xpos[1], ypos-2*8, editorcolors[14], editorcolors[0], TWENTYFIVE_BLANKS, 0);
                if (names[i][0])
                    printext16(xpos[1], ypos-2*8, editorcolors[14], editorcolors[0], names[i], 0);
            }
        }
        if (PRESSED_KEYSC(BS) || PRESSED_KEYSC(DELETE))
        {
            gs_spriteTagInterested[col][row] = 0;

            if (col == 1 && row == 6)  // picnum
                printext16(xpos[1], ypos-2*8, editorcolors[14], editorcolors[0], TWENTYFIVE_BLANKS, 0);
        }

        i = gs_spriteTagInterested[col][row];
        if (i)
        {
            if (col == 1 && row == 0)  // flags
                k = Bsprintf(disptext, "%s: %x", labels[row][col], gs_spriteTagValue[col][row]);
            else
                k = Bsprintf(disptext, "%s: %d", labels[row][col], gs_spriteTagValue[col][row]);
        }
        else
            k = Bsprintf(disptext, "%s: ^7any", labels[row][col]);
        //                      v-------^^
        for (; k<dispwidth[col]-2*i; k++) disptext[k] = ' ';
        disptext[k] = 0;

        printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[1],disptext,0);

        videoShowFrame(1);
    }

    printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
    printmessage16("Search sprite");
    videoShowFrame(1);

    keystatus[KEYSC_ESC] = 0;
}
#undef BLOOD_RR_MAPEDIT_SHARED_GENERIC_SPRITE_SEARCH
#endif
