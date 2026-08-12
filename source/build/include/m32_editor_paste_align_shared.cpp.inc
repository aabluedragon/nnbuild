    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(ENTER)) // ' ENTER
    {
        if (AIMED_SELOVR_PICNUM != temppicnum)
        {
            AIMED_SELOVR_PICNUM = temppicnum;
            asksave = 1;
        }

        if (AIMING_AT_SPRITE)
            correct_sprite_yoffset(searchwall);

        message("Pasted picnum only");
    }
    else if (keystatus[KEYSC_SEMI] && PRESSED_KEYSC(ENTER) && AIMING_AT_CEILING_OR_FLOOR)  // ; ENTER
    {
        if ((unsigned)tempsectornum >= (unsigned)numsectors)
        {
            message("Can't align sector %d's %s, have no reference sector",
                    searchsector, typestr[searchstat]);
        }
#ifdef YAX_ENABLE__COMPAT
        else if (yax_getbunch(searchsector, AIMING_AT_FLOOR) >= 0)
        {
            yax_invalidop();
        }
#endif
        else if (tempsectornum == searchsector)
        {
            message("Didn't align sector %d with itself as reference", tempsectornum);
        }
        else
        {
            // auto-align ceiling/floor
            const int32_t refwall = sector[tempsectornum].wallptr;
            const int32_t ourwall = sector[searchsector].wallptr;

            const vec2_t vecw1 = WALLVEC_INITIALIZER(refwall);
            const vec2_t vecw2 = WALLVEC_INITIALIZER(ourwall);
            const vec2_t vecw2r90 = { -vecw2.y, vecw2.x };  // v, rotated 90 deg CW

            const int32_t bits = CEILINGFLOOR(tempsectornum, stat)&(64+32+16+8+4);
            const int32_t tile = CEILINGFLOOR(tempsectornum, picnum);

            if ((CEILINGFLOOR(tempsectornum, stat)&64) == 0)
            {
                // world-aligned texture

                if (tilesiz[tile].x<=0 || tilesiz[tile].y<=0)
                {
                    message("Can't align sector %d's %s, reference sector %d's has void tile",
                            searchsector, typestr[searchstat], tempsectornum);
                }
                else
                {
                    // non-smooshed: 1 texel corresponds to 16 BUILD units
                    int32_t dx = (wall[ourwall].x-wall[refwall].x)<<4;
                    int32_t dy = (wall[ourwall].y-wall[refwall].y)<<4;

                    dx >>= ((picsiz[tile]&15) - !!(bits&8));
                    dy >>= ((picsiz[tile]>>4) - !!(bits&8));

                    CEILINGFLOOR(searchsector, xpanning) = CEILINGFLOOR(tempsectornum, xpanning) + dy;
                    CEILINGFLOOR(searchsector, ypanning) = CEILINGFLOOR(tempsectornum, ypanning) + dx;

                    CEILINGFLOOR(searchsector, stat) &= ~(64+32+16+8+4);
                    CEILINGFLOOR(searchsector, stat) |= bits;

                    message("Aligned sector %d's %s with sector %d's",
                            searchsector, typestr[searchstat], tempsectornum);
                }
            }
            else
            {
                // firstwall-aligned texture

                const int64_t a = lldotv2(&vecw1, &vecw2);
                const int64_t b = lldotv2(&vecw1, &vecw2r90);

                if (a!=0 && b!=0)
                {
                    message("Walls %d and %d are nether parallel nor perpendicular",
                            refwall, ourwall);
                }
                else
                {
                    // 0<-6<-3<-5<-0, 1<-7<-2<-4<-1 (mirrored/not mirrored separately)
                    static const int8_t prev3each[8] = { 5, 4, 7, 6, 2, 3, 0, 1 };

                    const int32_t degang = 90*(b<0) + 180*(a<0) + 270*(b>0);
                    int32_t i, tempbits = (bits&4) + ((bits>>4)&3);

//message("Wall %d is rotated %d degrees CW wrt wall %d", ourwall, degang, refwall);
                    CEILINGFLOOR(searchsector, stat) &= ~(64+32+16+8+4);
                    for (i=0; i<degang/90; i++)
                        tempbits = prev3each[tempbits];
                    tempbits = (tempbits&4) + ((tempbits&3)<<4);
                    CEILINGFLOOR(searchsector, stat) |= 64 + (bits&8) + tempbits;

                    {
                        const walltype *rw = &wall[refwall], *rw2 = &POINT2(refwall);
                        const walltype *ow = &wall[ourwall], *ow2 = &POINT2(ourwall);
                        int32_t intx, inty, sign12, sign34;

                        if (b != 0)  // perpendicular
                            inflineintersect(rw->x,rw->y, rw2->x,rw2->y, ow->x,ow->y, ow2->x,ow2->y,
                                             &intx,&inty, &sign12,&sign34);
                        else  // parallel
                            inflineintersect(rw->x,rw->y, rw2->x,rw2->y,
                                             ow->x,ow->y, ow->x+vecw2r90.x,ow->y+vecw2r90.y,
                                             &intx,&inty, &sign12,&sign34);
                        if (sign12 == 0)
                        {
                            message("INTERNAL ERROR: Couldn't get intersection"
                                    " of reference and alignee walls");
                        }
                        else
                        {
                            double dx = (double)(rw->x-intx)*(rw->x-intx) + (double)(rw->y-inty)*(rw->y-inty);
                            double dy = (double)(ow->x-intx)*(ow->x-intx) + (double)(ow->y-inty)*(ow->y-inty);

                            dx = -sign12 * sqrt(dx) * 16;
                            dy = sign34 * sqrt(dy) * 16;
                            if (a < 0 || b > 0)
                                dx = -dx;
                            dx /= 1<<((picsiz[tile]&15) - !!(bits&8));
                            dy /= 1<<((picsiz[tile]>>4) - !!(bits&8));
//initprintf("int=(%d,%d), dx=%.03f dy=%.03f\n", intx,inty, dx, dy);
                            CEILINGFLOOR(searchsector, xpanning) =
                                CEILINGFLOOR(tempsectornum, xpanning) + (int32_t)dx;
                            CEILINGFLOOR(searchsector, ypanning) =
                                CEILINGFLOOR(tempsectornum, ypanning) + (int32_t)dy;

                            message("%sAligned sector %d %s (firstwall-relative) with sector %d's",
                                    (CEILINGFLOOR(tempsectornum, stat)&(32+16+4)) ? "(TODO) ":"",
                                    searchsector, typestr[searchstat], tempsectornum);
                        }
                    }
                }
            }
        }
    }