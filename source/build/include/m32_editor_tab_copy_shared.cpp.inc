    if (PRESSED_KEYSC(TAB))  //TAB
    {
        if (ASSERT_AIMING)
        {
            tempshade = AIMED_CF_SEL(shade);
            temppal = AIMED_CF_SEL(pal);
            templotag = AIMED_SEL(lotag);
            temphitag = AIMED_SEL(hitag);
            tempextra = AIMED_SEL(extra);

            if (AIMING_AT_WALL_OR_MASK)
            {
#ifdef YAX_ENABLE__COMPAT
                if (yax_getnextwall(searchwall, YAX_CEILING) >= 0)
                    templotag = 0;
                if (yax_getnextwall(searchwall, YAX_FLOOR) >= 0)
                    tempextra = -1;
#endif
                temppicnum = AIMED_SELOVR_WALL(picnum);
                tempxrepeat = AIMED_SEL_WALL(xrepeat);
                tempxrepeat = max<int>(1, tempxrepeat);
                tempyrepeat = AIMED_SEL_WALL(yrepeat);
                tempxpanning = AIMED_SEL_WALL(xpanning);
                tempypanning = AIMED_SEL_WALL(ypanning);
#ifdef NEW_MAP_FORMAT
                tempcstat = AIMED_SEL_WALL(cstat);
#else
                tempcstat = AIMED_SEL_WALL(cstat) & ~YAX_NEXTWALLBITS;
#endif
                templenrepquot = getlenbyrep(wallength(searchwall), tempxrepeat);

                tempsectornum = sectorofwall(searchwall);

                tempstatnum = 0;
                tempblend = 0;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
                temppicnum = AIMED_CEILINGFLOOR(picnum);
                tempvis = sector[searchsector].visibility;
                tempxrepeat = AIMED_CEILINGFLOOR(xpanning);
                tempyrepeat = AIMED_CEILINGFLOOR(ypanning);
#ifdef YAX_ENABLE__COMPAT
                if (yax_getbunch(searchsector, AIMING_AT_FLOOR) >= 0)
                    tempxrepeat = 0;
#endif

#ifdef NEW_MAP_FORMAT
                tempcstat = AIMED_CEILINGFLOOR(stat);
#else
                tempcstat = AIMED_CEILINGFLOOR(stat) & ~YAX_BIT;
#endif
                tempsectornum = searchsector;

                tempstatnum = 0;
                tempblend = 0;
            }
            else if (AIMING_AT_SPRITE)
            {
                temppicnum = sprite[searchwall].picnum;
                tempxrepeat = sprite[searchwall].xrepeat;
                tempyrepeat = sprite[searchwall].yrepeat;
                tempcstat = sprite[searchwall].cstat;
                tempxvel = sprite[searchwall].xvel;
                tempyvel = sprite[searchwall].yvel;
                tempzvel = sprite[searchwall].zvel;

                tempstatnum = sprite[searchwall].statnum;
                tempblend = sprite[searchwall].blend;

                tempsectornum = -1;
            }

            somethingintab = searchstat;
        }
    }