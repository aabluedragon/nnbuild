#define M32_EDITOR_PICNUM_MAX MAXTILES
#include "../../build/include/m32_editor_key_actions_shared.cpp.inc"

    // ----------
    i = 512;

    if (keystatus[KEYSC_RSHIFT])
        i = 8;
    else if (keystatus[KEYSC_LSHIFT])
        i = 1;
    else if (keystatus[KEYSC_HOME] && keystatus[KEYSC_END])
        i = 64;
    else if (keystatus[KEYSC_HOME])
        i = 128;
    else if (keystatus[KEYSC_END])
        i = 256;

    mouseaction=0;

    if (eitherCTRL && !eitherSHIFT && (bstatus&1) && AIMING_AT_CEILING_OR_FLOOR)
    {
        g_mousePos.x=0; mskip=1;
        if (g_mousePos.y)
        {
            i=klabs(g_mousePos.y*2);
            mouseaction=1;
        }
    }

    tsign = 0;
    if (ASSERT_AIMING)
    {
        tsign -= (PRESSED_KEYSC(LBRACK) || (mouseaction && g_mousePos.y<0));   // [
        tsign += (PRESSED_KEYSC(RBRACK) || (mouseaction && g_mousePos.y>0));   // ]
    }

    if (tsign)
    {
#ifdef YAX_ENABLE
        int16_t bunchnum, othersidesect=0;
#endif
        if (eitherALT)
        {
            int32_t ns=wall[searchwall].nextsector, sx=wall[searchwall].x, sy=wall[searchwall].y;

            if (ns >= 0 && !mouseaction)
            {
                if (AIMING_AT_CEILING || (tsign < 0 && AIMING_AT_WALL_OR_MASK))
#ifdef YAX_ENABLE
                if (YAXCHK((bunchnum=yax_getbunch(searchsector, YAX_CEILING)) < 0 ||
                           (othersidesect=yax_is121(bunchnum, 1))>=0) &&
                        (bunchnum < 0 || YAXSLOPECHK(searchsector, othersidesect)))
#endif
                {
                    alignceilslope(searchsector, sx, sy, getceilzofslope(ns, sx, sy));
#ifdef YAX_ENABLE
                    if (bunchnum>=0)
                        setslope(othersidesect, 1, sector[searchsector].ceilingheinum);
#endif
                    message("Sector %d align ceiling to wall %d", searchsector, searchwall);
                }

                if (AIMING_AT_FLOOR || (tsign > 0 && AIMING_AT_WALL_OR_MASK))
#ifdef YAX_ENABLE
                if (YAXCHK((bunchnum=yax_getbunch(searchsector, YAX_FLOOR)) < 0 ||
                           (othersidesect=yax_is121(bunchnum, 0))>=0) &&
                        (bunchnum < 0 || YAXSLOPECHK(searchsector, othersidesect)))
#endif
                {
                    alignflorslope(searchsector, sx, sy, getflorzofslope(ns, sx, sy));
#ifdef YAX_ENABLE
                    if (bunchnum>=0)
                        setslope(othersidesect, 0, sector[searchsector].floorheinum);
#endif
                    message("Sector %d align floor to wall %d", searchsector, searchwall);
                }
            }
        }
        else
        {
            if (AIMING_AT_CEILING_OR_FLOOR)
#ifdef YAX_ENABLE
            if (YAXCHK((bunchnum=yax_getbunch(searchsector, AIMING_AT_FLOOR)) < 0 ||
                       (othersidesect=yax_is121(bunchnum, AIMING_AT_CEILING))>=0) &&
                    (bunchnum < 0 || YAXSLOPECHK(searchsector, othersidesect)))
#endif
            {
                int32_t oldslope = (AIMED_CEILINGFLOOR(stat)&2) ? AIMED_CEILINGFLOOR(heinum) : 0;
                int32_t newslope = clamp(oldslope + tsign*i, -BHEINUM_MAX, BHEINUM_MAX);

                setslope(searchsector, AIMING_AT_FLOOR, newslope);
#ifdef YAX_ENABLE
                if (bunchnum >= 0)
                    setslope(othersidesect, !AIMING_AT_FLOOR, newslope);
#endif
                silentmessage("Sector %d %s slope = %d", searchsector,
                              typestr[searchstat], AIMED_CEILINGFLOOR(heinum));
            }
        }

        asksave = 1;
    }


#include "../../build/include/m32_editor_shift_pan_shared.cpp.inc"

    smooshyalign = keystatus[KEYSC_gKP5];
    repeatpanalign = eitherSHIFT || eitherALT;

    ////////////////////
    updownunits=1;
    mouseaction=0;

    if (!g_mouseBits)
    {
        mouseax=0;
        mouseay=0;
    }

    if ((bstatus&1) && !AIMING_AT_CEILING_OR_FLOOR)
    {
        if (eitherSHIFT)
        {
            mskip=1;
            if (g_mousePos.x)
            {
                mouseaction = 1;
                mouseax += g_mousePos.x;
                updownunits = klabs(mouseax/2);
                if (updownunits)
                    mouseax=0;
            }
        }
        else if (eitherCTRL && !eitherALT)
        {
            mskip=1;
            if (g_mousePos.x)
            {
                mouseaction = 2;
                repeatpanalign = 0;
                updownunits = klabs(mouseax+=g_mousePos.x)/(16 - 12*AIMING_AT_SPRITE);
                if (updownunits)
                    mouseax=0;
            }
        }
    }

    if (keystatus[KEYSC_gLEFT] || keystatus[KEYSC_gRIGHT] || mouseaction) // 4 & 6 (keypad)
    {
        if (repeatcountx == 0 || repeatcountx > 32 || mouseaction)
        {
            changedir = 0;
            if (keystatus[KEYSC_gLEFT]  || g_mousePos.x>0)
                changedir = -1;
            if (keystatus[KEYSC_gRIGHT] || g_mousePos.x<0)
                changedir = 1;

            if (AIMING_AT_WALL_OR_MASK)
            {
                if (repeatpanalign == 0)
                {
                    while (updownunits--)
                        wall[searchwall].xrepeat = changechar(wall[searchwall].xrepeat, changedir, smooshyalign, 1);
                    silentmessage("Wall %d repeat: %d, %d", searchwall,
                                  TrackerCast(wall[searchwall].xrepeat),
                                  TrackerCast(wall[searchwall].yrepeat));
                }
                else
                {
                    int16_t w = SELECT_WALL();

                    if (mouseaction)
                    {
                        i = wall[w].cstat;
                        i &= (8|256);

                        if (i==8 || i==256)
                            changedir*=-1;

                        if (eitherCTRL)
                            updownunits *= 8;
                    }

                    while (updownunits--)
                        wall[w].xpanning = changechar(wall[w].xpanning, changedir, smooshyalign, 0);
                    silentmessage("Wall %d panning: %d, %d", w,
                                  TrackerCast(wall[w].xpanning), TrackerCast(wall[w].ypanning));
                }
                asksave = 1;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
#ifdef YAX_ENABLE__COMPAT
                if (YAXCHK(yax_getbunch(searchsector, AIMING_AT_FLOOR) < 0))
#endif
                {
                    while (updownunits--)
                        AIMED_CEILINGFLOOR(xpanning) = changechar(AIMED_CEILINGFLOOR(xpanning), changedir, smooshyalign, 0);
                    silentmessage("Sector %d %s panning: %d, %d", searchsector, typestr[searchstat],
                                  AIMED_CEILINGFLOOR(xpanning), AIMED_CEILINGFLOOR(ypanning));
                    asksave = 1;
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                static int32_t sumxvect=0, sumyvect=0;

                if (mouseaction==1)
                    mouseaction_movesprites(&sumxvect, &sumyvect, 1536, g_mousePos.x);
                else
                {
                    sumxvect = sumyvect = 0;
                    if (mouseaction==2)
                        changedir *= -1;
                    while (updownunits--)
                        sprite[searchwall].xrepeat = changechar(sprite[searchwall].xrepeat, changedir, smooshyalign, 1);
                    if (sprite[searchwall].xrepeat < 4)
                        sprite[searchwall].xrepeat = 4;
                    silentmessage("Sprite %d repeat: %d, %d", searchwall,
                                  TrackerCast(sprite[searchwall].xrepeat),
                                  TrackerCast(sprite[searchwall].yrepeat));
                }
            }
            asksave = 1;
            repeatcountx = max(1,repeatcountx-2);
        }
        repeatcountx += synctics;
    }
    else
        repeatcountx = 0;


    ////////////////////
    updownunits=1;
    mouseaction=0;

    if ((bstatus&1) && !AIMING_AT_CEILING_OR_FLOOR)
    {
        if (eitherSHIFT)
        {
            mskip=1;
            if (g_mousePos.y)
            {
                mouseaction = 1;
                updownunits = klabs(g_mousePos.y);

                if (!AIMING_AT_SPRITE)
                    updownunits = klabs((int32_t)(g_mousePos.y*128./tilesiz[wall[searchwall].picnum].y));
            }
        }
        else if (eitherCTRL && !eitherALT)
        {
            mskip=1;
            if (g_mousePos.y)
            {
                mouseaction = 2;
                repeatpanalign = 0;
                mouseay += g_mousePos.y;
                updownunits = klabs(mouseay)/(32 - 28*AIMING_AT_SPRITE);
                if (updownunits)
                    mouseay=0;
            }
        }
    }

    if (!g_mouseBits)
    {
        mouseax=0;
        mouseay=0;
    }

    if (keystatus[KEYSC_gUP] || keystatus[KEYSC_gDOWN] || mouseaction)  // 2 & 8 (keypad)
    {
        if (repeatcounty == 0 || repeatcounty > 32 || mouseaction)
        {
            changedir = 0;
            if (keystatus[KEYSC_gUP]   || g_mousePos.y>0)
                changedir = -1;
            if (keystatus[KEYSC_gDOWN] || g_mousePos.y<0)
                changedir = 1;

            if (AIMING_AT_WALL_OR_MASK)
            {
                if (repeatpanalign == 0)
                {
                    while (updownunits--)
                        wall[searchwall].yrepeat = changechar(wall[searchwall].yrepeat, changedir, smooshyalign, 1);
                    silentmessage("Wall %d repeat: %d, %d", searchwall,
                                  TrackerCast(wall[searchwall].xrepeat),
                                  TrackerCast(wall[searchwall].yrepeat));
                }
                else
                {
                    int16_t w = SELECT_WALL();
                    if (mouseaction && eitherCTRL)
                        updownunits *= 8;
                    while (updownunits--)
                        wall[w].ypanning = changechar(wall[w].ypanning, changedir, smooshyalign, 0);
                    silentmessage("Wall %d panning: %d, %d", w,
                                  TrackerCast(wall[w].xpanning), TrackerCast(wall[w].ypanning));
                }
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
                {
                    while (updownunits--)
                        AIMED_CEILINGFLOOR(ypanning) = changechar(AIMED_CEILINGFLOOR(ypanning), changedir, smooshyalign, 0);
                    silentmessage("Sector %d %s panning: %d, %d", searchsector, typestr[searchstat],
                                  AIMED_CEILINGFLOOR(xpanning), AIMED_CEILINGFLOOR(ypanning));
                    asksave = 1;
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                static int32_t sumxvect=0, sumyvect=0;

                if (mouseaction==1)
                    mouseaction_movesprites(&sumxvect, &sumyvect, 2048, g_mousePos.y);
                else
                {
                    sumxvect = sumyvect = 0;
                    while (updownunits--)
                        sprite[searchwall].yrepeat = changechar(sprite[searchwall].yrepeat, changedir, smooshyalign, 1);
                    if (sprite[searchwall].yrepeat < 4)
                        sprite[searchwall].yrepeat = 4;
                    silentmessage("Sprite %d repeat: %d, %d", searchwall,
                                  TrackerCast(sprite[searchwall].xrepeat),
                                  TrackerCast(sprite[searchwall].yrepeat));
                }
            }
            asksave = 1;
            repeatcounty = max(1,repeatcounty-2);
        }
        repeatcounty += synctics;
    }
    else
        repeatcounty = 0;

    ////////////////////

    if (PRESSED_KEYSC(F11))  //F11 - brightness
    {
        static int16_t brightness = -1;

        if (brightness==-1)
            brightness = ((int16_t)((g_videoGamma-1.0)*10.0))&15;

        brightness = brightness + (1-2*eitherSHIFT);
        brightness &= 15;

        g_videoGamma = 1.0 + ((float)brightness / 10.0);
        videoSetPalette(brightness, 0, 0);
        message("Brightness: %d/16", brightness+1);
    }

#include "../../build/include/m32_editor_tab_copy_shared.cpp.inc"

#include "../../build/include/m32_editor_paste_align_shared.cpp.inc"

    if (PRESSED_KEYSC(ENTER))  // ENTER -- paste clipboard contents
    {
        if (eitherSHIFT)
        {
            if (AIMING_AT_WALL_OR_MASK && eitherCTRL)  //Ctrl-shift Enter (auto-shade)
            {
                int16_t daang;
                int8_t dashade[2] = { 127, -128 };

                i = searchwall;
                do
                {
                    dashade[0] = min(dashade[0], TrackerCast(wall[i].shade));
                    dashade[1] = max(dashade[1], TrackerCast(wall[i].shade));

                    i = wall[i].point2;
                }
                while (i != searchwall);

                daang = getangle(POINT2(searchwall).x-wall[searchwall].x, POINT2(searchwall).y-wall[searchwall].y);

                i = searchwall;
                do
                {
                    j = getangle(POINT2(i).x-wall[i].x,POINT2(i).y-wall[i].y);
                    k = (j+2048-daang)&2047;
                    if (k > 1024)
                        k = 2048-k;

                    wall[i].shade = dashade[0]+mulscale10(k, dashade[1]-dashade[0]);

                    i = wall[i].point2;
                }
                while (i != searchwall);

                message("Auto-shaded wall %d's loop", searchwall);
                asksave = 1;
            }
            else if (somethingintab < 255)
            {
                if (ASSERT_AIMING)
                {
                    AIMED_CF_SEL(shade) = tempshade;
                    AIMED_CF_SEL(pal) = temppal;

                    k = 0;
                    if (AIMING_AT_CEILING_OR_FLOOR)
                    {
                        if (somethingintab == SEARCH_CEILING || somethingintab == SEARCH_FLOOR)
                            k=1, sector[searchsector].visibility = tempvis;
                    }
                    else if (AIMING_AT_SPRITE)
                        sprite[searchwall].blend = tempblend;

                    message("Pasted shade+pal%s", k?"+visibility":"");
                    asksave = 1;
                }
            }
        }
        else if (AIMING_AT_WALL_OR_MASK && eitherCTRL && somethingintab < 255)  //Either ctrl key
        {
            int32_t clipboard_has_wall = (somethingintab == SEARCH_WALL || somethingintab == SEARCH_MASKWALL);
            i = searchwall;
            do
            {
                wall[i].picnum = temppicnum;
                wall[i].shade = tempshade;
                wall[i].pal = temppal;

                if (clipboard_has_wall)
                {
                    wall[i].xrepeat = tempxrepeat;
                    wall[i].yrepeat = tempyrepeat;

                    wall[i].xpanning = tempxpanning;
                    wall[i].ypanning = tempypanning;

                    wall[i].cstat &= ~BLOOD_RR_EDITOR_WALL_PASTE_CSTAT_MASK;
                    wall[i].cstat |= (tempcstat & BLOOD_RR_EDITOR_WALL_PASTE_CSTAT_MASK);

                    fixxrepeat(i, templenrepquot);
                }

                i = wall[i].point2;
            }
            while (i != searchwall);

            message("Pasted picnum+shade+pal%s to wall %d's loop",
                    clipboard_has_wall?"+pixelwidth":"", searchwall);
            asksave = 1;
        }
        else if (AIMING_AT_CEILING_OR_FLOOR && eitherCTRL && somethingintab < 255)  //Either ctrl key
        {
            static const char *addnstr[4] = {"", "+stat+panning", "+stat", "+stat + panning (some)"};

            static int16_t sectlist[MAXSECTORS];
            static uint8_t sectbitmap[bitmap_size(MAXSECTORS)];
            int16_t sectcnt, sectnum;

            i = searchsector;
            if (CEILINGFLOOR(i, stat)&1)
            {
                // collect neighboring parallaxed sectors
                bfirst_search_init(sectlist, sectbitmap, &sectnum, MAXSECTORS, i);

                for (sectcnt=0; sectcnt<sectnum; sectcnt++)
                    for (WALLS_OF_SECTOR(sectlist[sectcnt], j))
                    {
                        k = wall[j].nextsector;
                        if (k>=0 && (CEILINGFLOOR(k, stat)&1))
                            bfirst_search_try(sectlist, sectbitmap, &sectnum, wall[j].nextsector);
                    }

                k = 0;
                for (sectcnt=0; sectcnt<sectnum; sectcnt++)
                {
                    i = sectlist[sectcnt];

                    CEILINGFLOOR(i, picnum) = temppicnum;
                    CEILINGFLOOR(i, shade) = tempshade;
                    CEILINGFLOOR(i, pal) = temppal;

                    if (somethingintab == SEARCH_CEILING || somethingintab == SEARCH_FLOOR)
                    {
#ifdef YAX_ENABLE__COMPAT
                        if (yax_getbunch(i, AIMING_AT_FLOOR) >= 0)
                            k |= 2;
                        else
#endif
                        {
                            CEILINGFLOOR(i, xpanning) = tempxrepeat;
                            CEILINGFLOOR(i, ypanning) = tempyrepeat;
                            k |= 1;
                        }

                        SET_PROTECT_BITS(CEILINGFLOOR(i, stat), tempcstat, YAX_BIT_PROTECT);
                    }
                }

                message("Pasted picnum+shade+pal%s to parallaxed sector %ss",
                        addnstr[k], typestr[searchstat]);
                asksave = 1;
            }
#ifdef YAX_ENABLE
            else
            {
                k = yax_getbunch(searchsector, AIMING_AT_FLOOR);
                if (k < 0)
                    goto paste_ceiling_or_floor;

                j = (somethingintab==SEARCH_CEILING || somethingintab==SEARCH_FLOOR);

                for (SECTORS_OF_BUNCH(k,AIMING_AT_FLOOR, i))
                {
                    SECTORFLD(i,picnum, AIMING_AT_FLOOR) = temppicnum;
                    SECTORFLD(i,shade, AIMING_AT_FLOOR) = tempshade;
                    SECTORFLD(i,pal, AIMING_AT_FLOOR) = temppal;
                    if (j)
                        SET_PROTECT_BITS(SECTORFLD(i,stat, AIMING_AT_FLOOR), tempcstat, YAX_BIT_PROTECT);
                }

                message("Pasted picnum+shade+pal%s to sector %ss with bunchnum %d",
                        j?"+stat":"", typestr[searchstat], k);
            }
#endif
        }
        else if (somethingintab < 255)
        {
            // wall/overwall common:
            if (AIMING_AT_WALL_OR_MASK && searchstat==somethingintab &&
                    (!AIMING_AT_WALL || searchwall==searchbottomwall))
            {
                wall[searchwall].xrepeat = tempxrepeat;
                wall[searchwall].yrepeat = tempyrepeat;
                wall[searchwall].xpanning = tempxpanning;
                wall[searchwall].ypanning = tempypanning;

                SET_PROTECT_BITS(wall[searchwall].cstat, tempcstat, ~BLOOD_RR_EDITOR_WALL_PASTE_CSTAT_MASK);

                wall[searchwall].hitag = temphitag;
#ifdef YAX_ENABLE
                if (yax_getnextwall(searchwall, YAX_CEILING) == -1)
#endif
                    wall[searchwall].lotag = templotag;
#ifdef YAX_ENABLE
                if (yax_getnextwall(searchwall, YAX_FLOOR) == -1)
#endif
                    wall[searchwall].extra = tempextra;

                fixxrepeat(searchwall, templenrepquot);
            }


            if (AIMING_AT_WALL)
            {
                if (searchisbottom)
                    SET_PROTECT_BITS(wall[searchbottomwall].cstat, tempcstat, ~256);

                wall[searchbottomwall].picnum = temppicnum;
                wall[searchbottomwall].shade = tempshade;
                wall[searchbottomwall].pal = temppal;
            }
            else if (AIMING_AT_MASKWALL)
            {
                wall[searchwall].overpicnum = temppicnum;
                if (wall[searchwall].nextwall >= 0)
                    NEXTWALL(searchwall).overpicnum = temppicnum;

                wall[searchwall].shade = tempshade;
                wall[searchwall].pal = temppal;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
#ifdef YAX_ENABLE
paste_ceiling_or_floor:
#endif
                AIMED_CEILINGFLOOR(picnum) = temppicnum;
                AIMED_CEILINGFLOOR(shade) = tempshade;
                AIMED_CEILINGFLOOR(pal) = temppal;

                if (somethingintab == SEARCH_CEILING || somethingintab == SEARCH_FLOOR)
                {
#ifdef YAX_ENABLE
                    if (yax_getbunch(searchsector, AIMING_AT_FLOOR) < 0)
#endif
                        AIMED_CEILINGFLOOR(xpanning) = tempxrepeat;
                    AIMED_CEILINGFLOOR(ypanning) = tempyrepeat;

                    SET_PROTECT_BITS(AIMED_CEILINGFLOOR(stat), tempcstat, YAX_BIT_PROTECT|2);

                    sector[searchsector].visibility = tempvis;
                    sector[searchsector].lotag = templotag;
                    sector[searchsector].hitag = temphitag;
                    sector[searchsector].extra = tempextra;
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                sprite[searchwall].picnum = temppicnum;

                if (tilesiz[temppicnum].x <= 0 || tilesiz[temppicnum].y <= 0)
                {
                    j = 0;
                    for (k=0; k<MAXTILES; k++)
                        if (tilesiz[k].x > 0 && tilesiz[k].y > 0)
                        {
                            j = k;
                            break;
                        }
                    sprite[searchwall].picnum = j;
                }

                sprite[searchwall].shade = tempshade;
                sprite[searchwall].pal = temppal;

                if (somethingintab == SEARCH_SPRITE)
                {
                    sprite[searchwall].xrepeat = max(tempxrepeat, 1u);
                    sprite[searchwall].yrepeat = max(tempyrepeat, 1u);
                    sprite[searchwall].cstat = tempcstat;
                    sprite[searchwall].lotag = templotag;
                    sprite[searchwall].hitag = temphitag;
                    sprite[searchwall].extra = tempextra;
                    sprite[searchwall].xvel = tempxvel;
                    sprite[searchwall].yvel = tempyvel;
                    sprite[searchwall].zvel = tempzvel;
                    changespritestat(searchwall, tempstatnum);
                    sprite[searchwall].blend = tempblend;
                }
                else
                    correct_sprite_yoffset(searchwall);
            }

            message("Pasted clipboard");
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(C))
    {
        if (eitherALT)  // Alt-C  picnum replacer
        {
            if (ASSERT_AIMING && somethingintab < 255)
            {
                switch (searchstat)
                {
                case SEARCH_WALL:
                    j = wall[searchbottomwall].picnum;
                    for (i=0; i<numwalls; i++)
                        if (wall[i].picnum == j)
                            wall[i].picnum = temppicnum;
                    break;
                case SEARCH_CEILING:
                case SEARCH_FLOOR:
                    j = AIMED_CEILINGFLOOR(picnum);
                    for (i=0; i<numsectors; i++)
                        if (CEILINGFLOOR(i, picnum) == j)
                            CEILINGFLOOR(i, picnum) = temppicnum;
                    break;
                case SEARCH_SPRITE:
                    j = sprite[searchwall].picnum;
                    for (i=0; i<MAXSPRITES; i++)
                        if (sprite[i].statnum < MAXSTATUS)
                            if (sprite[i].picnum == j)
                            {
                                sprite[i].picnum = temppicnum;
                                correct_sprite_yoffset(i);
                            }
                    break;
                case SEARCH_MASKWALL:
                    j = wall[searchwall].overpicnum;
                    for (i=0; i<numwalls; i++)
                        if (wall[i].overpicnum == j)
                            wall[i].overpicnum = temppicnum;
                    break;
                default:
                    j = -1;
                    break;
                }

                if (j>=0)
                    message("Replaced %ss with picnum %d to picnum %d",
                            typestr[searchstat], j, temppicnum);
                asksave = 1;
            }
        }
        else  //C
        {
            if (AIMING_AT_SPRITE)
            {
                sprite[searchwall].cstat ^= 128;
                message("Sprite %d center bit %s",searchwall, ONOFF((sprite[searchwall].cstat&128)));
                asksave = 1;
            }
        }
    }

    if (PRESSED_KEYSC(SLASH))  // /?     Reset panning&repeat to 0
    {
        if (AIMING_AT_WALL_OR_MASK)
        {
            int16_t w = SELECT_WALL();
            wall[w].xpanning = 0;
            wall[w].ypanning = 0;
            wall[w].xrepeat = 8;
            wall[w].yrepeat = 8;
#ifdef NEW_MAP_FORMAT
            wall[w].cstat = 0;
#else
            wall[w].cstat &= YAX_NEXTWALLBITS;
#endif
            fixrepeats(searchwall);
        }
        else if (AIMING_AT_CEILING_OR_FLOOR)
        {
#ifdef YAX_ENABLE
            int16_t bunchnum = yax_getbunch(searchsector, AIMING_AT_FLOOR);
# if !defined NEW_MAP_FORMAY
            if (bunchnum < 0)
# endif
#endif
                AIMED_CEILINGFLOOR(xpanning) = 0;
            AIMED_CEILINGFLOOR(ypanning) = 0;
            AIMED_CEILINGFLOOR(stat) &= ~2;
            AIMED_CEILINGFLOOR(heinum) = 0;
#ifdef YAX_ENABLE
            if (bunchnum >= 0)
                for (SECTORS_OF_BUNCH(bunchnum,!AIMING_AT_FLOOR, i))
                {
                    SECTORFLD(i,stat, !AIMING_AT_FLOOR) &= ~2;
                    SECTORFLD(i,heinum, !AIMING_AT_FLOOR) = 0;
                }
#endif
        }
        else if (AIMING_AT_SPRITE)
        {
            if (eitherSHIFT)
                sprite[searchwall].xrepeat = sprite[searchwall].yrepeat;
            else
            {
                sprite[searchwall].xrepeat = 64;
                sprite[searchwall].yrepeat = 64;
            }

            correct_sprite_yoffset(searchwall);
        }

        if (ASSERT_AIMING)
        {
            message("%s's size and panning reset", Typestr[searchstat]);
            asksave = 1;
        }
    }

#include "../../build/include/m32_editor_3d_tail_sprite_search_shared.cpp.inc"

////////// KEY PRESS HANDLER IN 2D MODE //////////
static void Keys2d(void)
{
    int32_t i=0, j, k;
    int32_t smooshy, changedir;
    static int32_t repeatcnt[2] = {0,0};  // was repeatcountx, repeatcounty
    int32_t tcursectornum;

//    for(i=0;i<0x50;i++) if(keystatus[i]==1) Bsprintf(tempbuf,"key %d",i); printmessage16(tempbuf);

    tcursectornum = -1;
    for (i=0; i<numsectors; i++)
    {
        YAX_SKIPSECTOR(i);
        if (inside_editor_curpos(i) == 1)
        {
            tcursectornum = i;
            break;
        }
    }
    searchsector = tcursectornum;
