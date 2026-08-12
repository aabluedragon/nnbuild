#ifndef M32_EDITOR_PICNUM_MAX
# define M32_EDITOR_PICNUM_MAX MAXUSERTILES
#endif

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(S)) // ' S
    {
        if (ASSERT_AIMING)
        {
            int8_t oshade = AIMED_CF_SEL(shade);
            Bsprintf(tempbuf, "%s shade: ", Typestr[searchstat]);
            getnumberptr256(tempbuf, &AIMED_CF_SEL(shade), sizeof(int8_t), 128, 1, NULL);
            if (AIMED_CF_SEL(shade) != oshade)
                asksave = 1;
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(B)) // ' B
    {
        if (AIMING_AT_SPRITE)
        {
            int8_t oblend = sprite[searchwall].blend;
            Bsprintf(tempbuf, "%s blend: ", Typestr[searchstat]);
            getnumberptr256(tempbuf, &sprite[searchwall].blend, sizeof(int8_t), 255, 0, NULL);
            if (sprite[searchwall].blend != oblend)
                asksave = 1;
        }
    }

    if (PRESSED_KEYSC(F2))  // F2
    {
        if (eitherCTRL || eitherSHIFT)
            infobox ^= (eitherSHIFT | ((eitherCTRL)<<1));
        else
            usedcount = !usedcount;
    }

    if (PRESSED_KEYSC(F1)) // F1
    {
        helpon = !helpon;
//        keystatus[KEYSC_H]=0;  // delete this line?
    }

    if (PRESSED_KEYSC(G)) // G
    {
        if (ASSERT_AIMING)
        {
            int16_t *const picnumptr = AIMING_AT_WALL_OR_MASK ? &AIMED_SELOVR_PICNUM : &AIMED_CF_SEL(picnum);
            const int32_t aiming_at_sprite = AIMING_AT_SPRITE;
            const int32_t opicnum = *picnumptr, osearchwall = searchwall;

            static const char *Typestr_tmp[5] = { "Wall", "Sector ceiling", "Sector floor", "Sprite", "Masked wall" };

            Bsprintf(tempbuf, "%s picnum: ", Typestr_tmp[searchstat]);
            getnumberptr256(tempbuf, picnumptr, sizeof(int16_t), M32_EDITOR_PICNUM_MAX-1, 0+2, NULL);

            Bassert((unsigned)*picnumptr < M32_EDITOR_PICNUM_MAX);
            if (!tileLoad(*picnumptr))
                *picnumptr = opicnum;

            if (*picnumptr != opicnum)
                asksave = 1;

            // need to use the old value because aiming might have changed in getnumberptr256
            if (aiming_at_sprite)
                correct_sprite_yoffset(osearchwall);
        }
    }

    if (PRESSED_KEYSC(B))  // B (clip Blocking xor) (3D)
    {
        if (AIMING_AT_SPRITE)
        {
            sprite[searchwall].cstat ^= 1;
            //                                sprite[searchwall].cstat &= ~256;
            //                                sprite[searchwall].cstat |= ((sprite[searchwall].cstat&1)<<8);
            message("Sprite %d blocking bit %s", searchwall, ONOFF(sprite[searchwall].cstat&1));
            asksave = 1;
        }
        else if (AIMING_AT_WALL_OR_MASK || AIMING_AT_CEILING_OR_FLOOR)
        {
#ifdef YAX_ENABLE
            if (AIMING_AT_CEILING_OR_FLOOR && yax_getbunch(searchsector, AIMING_AT_FLOOR)>=0)
            {
                SECTORFLD(searchsector,stat, AIMING_AT_FLOOR) ^= 512;
                message("Sector %d's %s blocking bit %s", searchsector, typestr[searchstat],
                        ONOFF(SECTORFLD(searchsector,stat, AIMING_AT_FLOOR)&512));
                asksave = 1;
            }
            else
#endif
            {
                wall[searchwall].cstat ^= 1;
                //                                wall[searchwall].cstat &= ~64;
                if ((wall[searchwall].nextwall >= 0) && !eitherSHIFT)
                {
                    NEXTWALL(searchwall).cstat &= ~(1+64);
                    NEXTWALL(searchwall).cstat |= (wall[searchwall].cstat&1);
                }

                message("Wall %d blocking bit %s%s", searchwall, ONOFF(wall[searchwall].cstat&1),
                        eitherSHIFT ? " (one-sided)":"");
                asksave = 1;
            }
        }
    }

    // N (set "spritenoshade" bit)
    if (PRESSED_KEYSC(N) && !eitherCTRL && !keystatus[KEYSC_QUOTE])
    {
        if (AIMING_AT_SPRITE)
        {
            sprite[searchwall].cstat ^= CSTAT_SPRITE_NOSHADE;
            message("Sprite %d spritenoshade bit: %s", searchwall,
                    ONOFF(sprite[searchwall].cstat&CSTAT_SPRITE_NOSHADE));
        }
    }

    if (PRESSED_KEYSC(T))  // T (transluscence for sprites/masked walls)
    {
        if (AIMING_AT_CEILING_OR_FLOOR)   //Set masked/transluscent ceilings/floors
        {
            int32_t nexti[4] = { 128, 256, 384, 0 };
            uint16_t *stat = &AIMED_CEILINGFLOOR(stat);
            const char *statmsg[4] = {"normal", "masked", "translucent", "translucent (2)"};

            i = (*stat&(128+256))>>7;
            i = nexti[i];
            *stat &= ~(128+256);
            *stat |= i;

            message("Sector %d's %s made %s.", searchsector, typestr[searchstat], statmsg[i>>7]);

            asksave = 1;
        }

        if (keystatus[KEYSC_QUOTE])
        {
            if (ASSERT_AIMING)
            {
                int32_t olotag = AIMED(lotag);
                Bsprintf(tempbuf, "%s lotag: ", Typestr_wss[searchstat]);
                AIMED(lotag) = getnumber256(tempbuf, AIMED(lotag), BTAG_MAX, 0);
                if (olotag != AIMED(lotag))
                    asksave = 1;
            }
        }
        else if (eitherCTRL)
        {
            if (AIMING_AT_SPRITE)
                TextEntryMode(searchwall);
        }
        else
        {
            if (AIMING_AT_SPRITE)
            {
                if ((sprite[searchwall].cstat&2) == 0)
                    sprite[searchwall].cstat |= 2;
                else if ((sprite[searchwall].cstat&512) == 0)
                    sprite[searchwall].cstat |= 512;
                else
                    sprite[searchwall].cstat &= ~(2+512);
                asksave = 1;
            }
            else if (AIMING_AT_MASKWALL)
            {
                if ((wall[searchwall].cstat&128) == 0)
                    wall[searchwall].cstat |= 128;
                else if ((wall[searchwall].cstat&512) == 0)
                    wall[searchwall].cstat |= 512;
                else
                    wall[searchwall].cstat &= ~(128+512);

                if (wall[searchwall].nextwall >= 0)
                {
                    NEXTWALL(searchwall).cstat &= ~(128+512);
                    NEXTWALL(searchwall).cstat |= (wall[searchwall].cstat&(128+512));
                }
                asksave = 1;
            }
        }
    }


#undef M32_EDITOR_PICNUM_MAX
