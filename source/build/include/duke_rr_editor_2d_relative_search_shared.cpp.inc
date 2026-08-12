    if (PRESSED_KEYSC(R))  // R (relative alignment, rotation)
    {
        if (pointhighlight >= 16384)
            toggle_sprite_alignment(cursprite);
        else if (tcursectornum >= 0 && graphicsmode)
        {
            sector[tcursectornum].floorstat ^= 64;
            message("Sector %d floor texture relativity bit %s", searchsector,
                    ONOFF(sector[tcursectornum].floorstat&64));
            asksave = 1;
        }
    }

    if (keystatus[KEYSC_QUOTE] && keystatus[KEYSC_S]) // ' S
    {
        if (pointhighlight >= 16384)
        {
            keystatus[KEYSC_S] = 0;
            Bsprintf(tempbuf, "Sprite %d xrepeat: ", cursprite);
            sprite[cursprite].xrepeat = getnumber16(tempbuf, sprite[cursprite].xrepeat, 255, 0);
            Bsprintf(tempbuf, "Sprite %d yrepeat: ", cursprite);
            sprite[cursprite].yrepeat = getnumber16(tempbuf, sprite[cursprite].yrepeat, 255, 0);
            printmessage16("Sprite %d updated", i);
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(F)) // ' F
    {
        FuncMenu();
    }
    else if (!eitherALT && PRESSED_KEYSC(F))
    {
        if (pointhighlight < 16384 && tcursectornum>=0 && graphicsmode)
            toggle_cf_flipping(tcursectornum, 1);
    }

    tsign = 0;
    if (PRESSED_KEYSC(LBRACK)) // [     search backward
        tsign = -1;
    if (PRESSED_KEYSC(RBRACK)) // ]     search forward
        tsign = +1;

    if (tsign)
    {
        if (eitherALT)
        {
            if (numcorruptthings > 0)
            {
                int32_t wrap=0, x, y, z;

                if (curcorruptthing<0 || curcorruptthing>=numcorruptthings)
                    curcorruptthing = 0;
                else
                {
                    curcorruptthing += tsign;
                    wrap = (curcorruptthing<0 || curcorruptthing>=numcorruptthings);
                    curcorruptthing += numcorruptthings;
                    curcorruptthing %= numcorruptthings;
                }

                k = corruptthings[curcorruptthing];
                j = -1;
                switch (k&CORRUPT_MASK)
                {
                case 0:
                    printmessage16("MAP LIMITS EXCEEDED!");
                    fallthrough__;
                default:
                    k = 0;
                    break;
                case CORRUPT_SECTOR:
                    i = k&(MAXSECTORS-1);
                    j = 0;
                    x = wall[sector[i].wallptr].x;
                    y = wall[sector[i].wallptr].y;
                    z = getflorzofslope(i, x, y);
                    break;
                case CORRUPT_WALL:
                    i = k&(MAXWALLS-1);
                    j = 1;
                    x = wall[i].x + (wall[wall[i].point2].x-wall[i].x)/2;
                    y = wall[i].y + (wall[wall[i].point2].y-wall[i].y)/2;
                    z = getflorzofslope(sectorofwall(i), x, y);
                    break;
                case CORRUPT_SPRITE:
                    i = k&(MAXSPRITES-1);
                    j = 2;
                    x = sprite[i].x;
                    y = sprite[i].y;
                    z = sprite[i].z;
                    break;
                }

                if (k)
                {
                    static const char *secwalspr[3] = {"sector", "wall", "sprite"};
                    if (x>=-editorgridextent && x<=editorgridextent &&
                        y>=-editorgridextent && y<=editorgridextent)
                    {
                        pos.x = x;
                        pos.y = y;
                        pos.z = z;
#ifdef YAX_ENABLE
                        yax_updategrays(pos.z);
#endif
                    }
                    else x=editorgridextent+1;

                    printmessage16("#%d: %s Corrupt %s %d%s", curcorruptthing+1, tsign<0?"<":">", secwalspr[j],
                                   i, (x==editorgridextent+1) ? " (outside grid)" : (wrap ? " (wrap)" : ""));
                }
            }
            else
            {
                printmessage16("Map has no corruptions, cannot cycle them.");
            }
        }
        else if (keystatus[KEYSC_LSHIFT])
        {
            if (pointhighlight&16384)
            {
                const int32_t refspritenum = pointhighlight&16383;
                const int32_t reftag = select_sprite_tag(refspritenum);
                int32_t tmpspritenum = refspritenum;

                while (reftag != INT32_MIN)  // if (reftag != INT32_MIN) while (1)
                {
                    tmpspritenum += tsign;
                    if ((unsigned)tmpspritenum >= MAXSPRITES)
                        tmpspritenum = (tmpspritenum < 0) ? MAXSPRITES-1 : 0;

                    if (tmpspritenum==refspritenum)
                    {
                        silentmessage("No other sprites with tag %d", reftag);
                        break;
                    }

                    if (reftag==select_sprite_tag(tmpspritenum))
                    {
                        int32_t oog = jump_to_sprite(tmpspritenum);

                        if (!oog)
                        {
                            // center cursor so that we can repeat this
                            searchx = halfxdim16;
                            searchy = midydim16;
                        }

                        silentmessage("%s sprite with tag %d: %d%s", tsign>0 ? "Next" : "Previous",
                                      reftag, tmpspritenum, oog ? "(out of grid)" : "");
                        break;
                    }
                }
            }
            else
            {
                printmessage16("No sprite higlighted, cannot cycle linking sprites.");
            }
        }
        else if (wallsprite==0)
        {
            SearchSectors(tsign);
        }
        else if (wallsprite==1)
        {
            if ((tsign<0 && curwallnum>0) || (tsign>0 && curwallnum<numwalls))
                curwallnum += tsign;

            for (i=curwallnum; i>=0 && i<numwalls; i+=tsign)
            {
                if ((wall[i].picnum==wall[curwall].picnum)
                        && (search_lotag==0 || search_lotag==wall[i].lotag)
                        && (search_hitag==0 || search_hitag==wall[i].hitag))
                {
                    pos.x = wall[i].x - (wall[i].x-POINT2(i).x)/2;
                    pos.y = wall[i].y - (wall[i].y-POINT2(i).y)/2;
                    pos.z = getflorzofslope(sectorofwall(i), pos.x, pos.y);
                    printmessage16("%s Wall search: found", tsign<0?"<":">");
                    return;
                }
                curwallnum--;
            }
            printmessage16("%s Wall search: none found", tsign<0?"<":">");
        }
        else if (wallsprite==2)
            DoSpriteSearch(tsign);
    }
