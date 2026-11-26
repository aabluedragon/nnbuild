    if (PRESSED_KEYSC(T))  // T (tag)
    {
        char buffer[80];

        if (eitherCTRL)  //Ctrl-T
        {
            if (eitherSHIFT)
                showtags--;
            else
                showtags++;
            showtags += 3;
            showtags %= 3;
            printmessage16("Show tags %s", showtags<2?ONOFF(showtags):"LABELED");
        }
        else if (eitherALT)  //ALT
        {
            if (pointhighlight >= 16384)
            {
                i = pointhighlight-16384;
                j = taglab_linktags(1, i);
                j = 4*(j&1);
                Bsprintf(buffer,"Sprite (%d) Lo-tag: ", i);
                sprite[i].lotag = _getnumber16(buffer, sprite[i].lotag, BTAG_MAX, 0+j, sprite[i].picnum==SECTOREFFECTOR ?
                                               &SectorEffectorTagText : NULL);
            }
            else if (linehighlight >= 0)
            {
#ifdef YAX_ENABLE__COMPAT
                if (yax_getnextwall(linehighlight, YAX_CEILING)>=0)
                    message("Can't change lotag in protected wall");
                else
#endif
                {
                    i = linehighlight;
                    j = taglab_linktags(1, i);
                    j = 4*(j&1);
                    Bsprintf(buffer,"Wall (%d) Lo-tag: ", i);
                    wall[i].lotag = getnumber16(buffer, wall[i].lotag, BTAG_MAX, 0+j);
                }
            }
        }
        else
        {
            if (tcursectornum >= 0)
            {
                Bsprintf(buffer,"Sector (%d) Lo-tag: ", tcursectornum);
                sector[tcursectornum].lotag =
                    _getnumber16(buffer, sector[tcursectornum].lotag, BTAG_MAX, 0, &ExtGetSectorType);
            }
        }
    }

    if (PRESSED_KEYSC(F1) || (keystatus[KEYSC_QUOTE] && keystatus[KEYSC_TILDE])) //F1 or ' ~
    {
        // PK_
        if (numhelppages>0)
            IntegratedHelp();
        else
            printmessage16("m32help.hlp invalid or not found!");
    }

    if (PRESSED_KEYSC(F2))
        if (g_numsounds > 0)
        {
            SoundDisplay();
        }

    // F3: side view toggle (handled in build.c)

    getpoint(searchx,searchy, &mousxplc,&mousyplc);
    ppointhighlight = getpointhighlight(mousxplc,mousyplc, ppointhighlight);

    if ((ppointhighlight&0xc000) == 16384)
    {
        //   sprite[ppointhighlight&16383].cstat ^= 1;
        cursprite = ppointhighlight&16383;
    }

    if (keystatus[KEYSC_F9]) // F9 f1=3b
        ShowFileText("sthelp.hlp");

    /* start Mapster32 */

    if (PRESSED_KEYSC(F4))
    {
        showfirstwall = !showfirstwall;
        message("Sector firstwall highlight %s", showfirstwall?"enabled":"disabled");
    }

    if (PRESSED_KEYSC(M))  // M (tag)
    {
        if (eitherALT)  //ALT
        {
            if (pointhighlight >= 16384)
            {
                i = pointhighlight-16384;
                Bsprintf(tempbuf, "Sprite %d Extra: ", i);
                sprite[i].extra = getnumber16(tempbuf, sprite[i].extra, BTAG_MAX, 1);
            }
            else if (linehighlight >= 0)
            {
#ifdef YAX_ENABLE__COMPAT
                if (yax_getnextwall(linehighlight, YAX_FLOOR)>=0)
                    message("Can't change extra in protected wall");
                else
#endif
                {
                    i = linehighlight;
                    Bsprintf(tempbuf,"Wall %d Extra: ",i);
                    wall[i].extra = getnumber16(tempbuf,wall[i].extra,BTAG_MAX,1);
                }
            }
        }
        else
        {
            if (tcursectornum >= 0)
            {
                Bsprintf(tempbuf,"Sector %d Extra: ",tcursectornum);
                sector[tcursectornum].extra = getnumber16(tempbuf,sector[tcursectornum].extra,BTAG_MAX,1);
            }
        }
    }

    if (!eitherCTRL && PRESSED_KEYSC(E))  // E (expand)
    {
        if (tcursectornum >= 0)
        {
            sector[tcursectornum].floorstat ^= 8;
            message("Sector %d floor texture expansion bit %s", tcursectornum,
                    ONOFF(sector[tcursectornum].floorstat&8));
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(SLASH))  // /     Reset panning&repeat to 0
    {
        if ((ppointhighlight&0xc000) == 16384)
        {
            if (eitherSHIFT)
                sprite[cursprite].xrepeat = sprite[cursprite].yrepeat;
            else
                sprite[cursprite].xrepeat = sprite[cursprite].yrepeat = 64;
            asksave = 1;
        }
        else if (graphicsmode != 0)
        {
            i = tcursectornum;

            if (i >= 0)
            {
#ifdef YAX_ENABLE
                if (yax_getbunch(i, YAX_FLOOR) < 0)
#endif
                    sector[i].floorxpanning = 0;
                sector[i].floorypanning = 0;
                message("Sector %d floor panning reset", i);
                asksave = 1;
            }
        }
    }
