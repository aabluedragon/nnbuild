    if (keystatus[KEYSC_HOME] && keystatus[KEYSC_END])
        updownunits = 128;
    else if (keystatus[KEYSC_HOME])
        updownunits = 256;
    else if (keystatus[KEYSC_END])
        updownunits = 512;
    else
        updownunits = 1024;

    mouseaction=0;
    if (eitherALT && (bstatus&1))
    {
        g_mousePos.x=0; mskip=1;
        if (g_mousePos.y!=0)
        {
            updownunits=klabs(g_mousePos.y*128);
            mouseaction=1;
        }
    }


    tsign = 0;
    if (ASSERT_AIMING)
    {
        // PK: PGUP/PGDN, rmb only & mwheel
        tsign -= (PRESSED_KEYSC(PGUP) || (mouseaction && g_mousePos.y<0) || ((bstatus&(16|2|1))==(16|2)));
        tsign += (PRESSED_KEYSC(PGDN) || (mouseaction && g_mousePos.y>0) || ((bstatus&(32|2|1))==(32|2)));
    }

    if (tsign)
    {
        int16_t sect0, sect, havebtm=0, havetop=0, moveCeilings, moveFloors;
        int32_t cz, fz;

        k = 0;
        if (highlightsectorcnt > 0 && searchsector>=0 && searchsector<numsectors)
        {
            if (hlsectorbitmap[searchsector>>3]&(1<<(searchsector&7)))
                k = highlightsectorcnt;
        }

        if (k)
        {
            sect = highlightsector[0];
            havetop = AIMING_AT_WALL_OR_MASK;
        }
        else
        {
            if (AIMING_AT_WALL_OR_MASK && wall[searchwall].nextsector>=0
                    && eitherALT && !(bstatus&1))
            {
                sect = wall[searchwall].nextsector;
                havebtm = !AIMING_AT_MASKWALL && searchisbottom;
                havetop = !havebtm;
            }
            else
            {
                sect = searchsector;
                havetop = AIMING_AT_WALL_OR_MASK;
            }
        }

        sect0 = sect;
        moveCeilings = (AIMING_AT_CEILING || havetop);
        moveFloors = (AIMING_AT_FLOOR || havebtm);

        if (moveCeilings || moveFloors)
        {
            int32_t dz = tsign * (updownunits << (eitherCTRL<<1));   // JBF 20031128
            static const char *cfs[2] = { "ceiling", "floor" };
#ifdef YAX_ENABLE
            int16_t bunchnum=-1, maxbunchnum=-1, cb, fb;
            Bmemset(havebunch, 0, sizeof(havebunch));
#endif
            for (j=0; j<(k?k:1); j++, sect=highlightsector[j])
            {
                // stage one: see if we don't move beyond the other side
                // (ceiling if floor and vice versa)

                if (moveCeilings && (dz > 0) && sector[sect].ceilingz+dz > sector[sect].floorz)
                    dz = (k > 1) ? 0 : min(sector[sect].floorz - sector[sect].ceilingz, dz);
                else if (moveFloors && (dz < 0) && sector[sect].floorz+dz < sector[sect].ceilingz)
                    dz = (k > 1) ? 0 : max(sector[sect].ceilingz - sector[sect].floorz, dz);

                if (dz == 0)
                    break;
            }

            if (dz)
            {
                // now truly move things if we're clear to go!
                sect = sect0;
                for (j=0; j<(k?k:1); j++, sect=highlightsector[j])
                {
                    for (i=headspritesect[sect]; i!=-1; i=nextspritesect[i])
                    {
                        spriteoncfz(i, &cz, &fz);
                        if ((moveCeilings && sprite[i].z == cz) || (moveFloors && sprite[i].z == fz))
                            sprite[i].z += dz;
                    }

                    SECTORFLD(sect,z, moveFloors) += dz;
#ifdef YAX_ENABLE
                    bunchnum = yax_getbunch(sect, moveFloors);
                    if (bunchnum >= 0 && !(havebunch[bunchnum>>3]&(1<<(bunchnum&7))))
                    {
                        maxbunchnum = max(maxbunchnum, bunchnum);
                        havebunch[bunchnum>>3] |= (1<<(bunchnum&7));
                        tempzar[bunchnum] = &SECTORFLD(sect,z, moveFloors);
                    }
#endif
                }
            }

#ifdef YAX_ENABLE
            if (dz)
            {
                // sync z values of extended sectors' ceilings/floors
                for (i=0; i<numsectors; i++)
                {
                    yax_getbunches(i, &cb, &fb);
                    if (cb >= 0 && (havebunch[cb>>3]&(1<<(cb&7))))
                        sector[i].ceilingz = *tempzar[cb];
                    if (fb >= 0 && (havebunch[fb>>3]&(1<<(fb&7))))
                        sector[i].floorz = *tempzar[fb];
                }
            }

            if (!dz)
                silentmessage("Didn't move sector %ss", cfs[moveFloors]);
            else if (k<=1 && bunchnum>=0)
                silentmessage("Bunch %d's ceilings and floors = %d", bunchnum, SECTORFLD(sect0,z, moveFloors));
            else
#endif
            if (k<=1)
                silentmessage("Sector %d %sz = %d", sect0, cfs[moveFloors], SECTORFLD(sect0,z, moveFloors));
            else
                silentmessage("%s %d sector %ss by %d units", tsign<0 ? "Raised" : "Lowered",
                              k, cfs[moveFloors], dz*tsign);
        }

        if (AIMING_AT_SPRITE)
        {
            int32_t cz, fz;

            if (eitherCTRL && !eitherALT)  //CTRL - put sprite on ceiling/floor
            {
                spriteoncfz(searchwall, &cz, &fz);
                sprite[searchwall].z = (tsign==1) ? fz : cz;
            }
            else
            {
                k = !!(show2dsprite[searchwall>>3]&(1<<(searchwall&7)));

                tsign *= (updownunits << ((eitherCTRL && mouseaction)*3));

                for (i=0; i<highlightcnt || k==0; i++)
                {
                    if (k==0 || (highlight[i]&0xc000) == 16384)
                    {
                        int16_t sp = k==0 ? searchwall : highlight[i]&16383;

                        sprite[sp].z += tsign;

                        if (!spnoclip)
                        {
                            spriteoncfz(sp, &cz, &fz);
                            inpclamp(&sprite[sp].z, cz, fz);
                        }
#ifdef YAX_ENABLE
                        else if (sprite[sp].sectnum >= 0)
                        {
                            int16_t cb, fb;
                            yax_getbunches(sprite[sp].sectnum, &cb, &fb);
                            if (cb >= 0 || fb >= 0)
                                setspritez(sp, (vec3_t *)&sprite[sp]);
                        }
#endif
                        if (k==0)
                        {
                            silentmessage("Sprite %d z = %d", searchwall, TrackerCast(sprite[searchwall].z));
                            break;
                        }
                    }

                    if (k==1)
                        silentmessage("Sprites %s by %d units", tsign<0 ? "raised" : "lowered", tsign);
                }
            }
        }

        asksave = 1;
        g_mouseBits &= ~(16|32);
    }
