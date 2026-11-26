#ifndef M32_TEXTENTRY_IS_WALL_ALIGNED
# define M32_TEXTENTRY_IS_WALL_ALIGNED(cstatValue) (((cstatValue) & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_WALL)
#endif

#ifndef M32_TEXTENTRY_CURSOR_TILE
# define M32_TEXTENTRY_CURSOR_TILE SMALLFNTCURSOR
#endif

#ifndef M32_TEXTENTRY_ARTFREQ_LIMIT
# define M32_TEXTENTRY_ARTFREQ_LIMIT MAXUSERTILES
#endif

#ifndef M32_TEXTENTRY_FREE
# define M32_TEXTENTRY_FREE(ptr) Xfree(ptr)
#endif

static void TextEntryMode(int16_t startspr)
{
    char ch, buffer[80], doingspace=0;
    int16_t daang = 0, t, alphidx, linebegspr, curspr, cursor;
    int32_t i, j, k, dax = 0, day = 0;
    static uint8_t hgap=0, vgap=4;
    static uint8_t spcgap[MAX_ALPHABETS], firstrun=1;
    spritetype *sp;

    int16_t *spritenums;
    int32_t stackallocsize=32, numletters=0;

    if (firstrun)
    {
        firstrun=0;
        for (i=0; i<numalphabets; i++)
            spcgap[i] = 0;
    }

    if (startspr<0 || startspr>=MAXSPRITES ||
            sprite[startspr].statnum == MAXSTATUS)
        return;

    if (numalphabets == 0)
    {
        message("Alphabet configuration not read.");
        return;
    }

    if (!M32_TEXTENTRY_IS_WALL_ALIGNED(sprite[startspr].cstat))
    {
        message("Must point at a wall-aligned text sprite.");
        return;
    }

    int16_t basetile = t = sprite[startspr].picnum;
    alphidx = -1;
    for (i=0; i<numalphabets; i++)
    {
        for (j=0; j<NUMPRINTABLES; j++)
            if (alphabets[i].pic[j] == t)
            {
                alphidx = i;
                basetile = t;
                if (spcgap[i] == 0)
                    spcgap[i] = 3*tilesiz[t].x/2;
                goto ENDFOR1;
            }
    }
ENDFOR1:
    if (alphidx==-1)
    {
        message("Must point at a text sprite.");
        return;
    }

    curspr = linebegspr = startspr;

    t = sprite[startspr].picnum;
    sprite[startspr].xoffset = -picanm[t].xofs;
    sprite[startspr].yoffset = -picanm[t].yofs;

    spritenums = (int16_t *)Xmalloc(stackallocsize * sizeof(int16_t));

    cursor = insertsprite(sprite[startspr].sectnum,0);
    if (cursor < 0) goto ERROR_TOOMANYSPRITES;

    sp = &sprite[cursor];
    Bmemcpy(sp, &sprite[startspr], sizeof(spritetype));
    sp->yoffset = 0;
    sp->picnum = M32_TEXTENTRY_CURSOR_TILE;
    sp->xrepeat = clamp(sp->xrepeat/tilesiz[sp->picnum].x, 2, 255);
    sp->yrepeat = clamp((sp->yrepeat*tilesiz[sprite[startspr].picnum].y)/tilesiz[sp->picnum].y, 4, 255);
    sp->pal = 0;
    sp->cstat = 18;

    keyFlushChars();
    while (keystatus[sc_Escape] == 0)
    {
        if (handleevents())
            quitevent = 0;


        if (PRESSED_KEYSC(UP))  // vertical gap in pixels (32 x-units)
            vgap += (vgap<255);

        if (PRESSED_KEYSC(DOWN))
            vgap -= (vgap>0);

        if (PRESSED_KEYSC(RIGHT))  // horizontal gap in half pixels
            hgap += (hgap<255);

        if (PRESSED_KEYSC(LEFT))
            hgap -= (hgap>0);

        if (PRESSED_KEYSC(INSERT))  // space gap in half pixels
            spcgap[alphidx] += (spcgap[alphidx]<255);

        if (PRESSED_KEYSC(DELETE))
            spcgap[alphidx] -= (spcgap[alphidx]>1);

        if (PRESSED_KEYSC(HOME))  // shade
            sprite[linebegspr].shade += (sprite[linebegspr].shade<127);

        if (PRESSED_KEYSC(END))
            sprite[linebegspr].shade -= (sprite[linebegspr].shade>-128);

        if (PRESSED_KEYSC(PGUP))  // pal
            sprite[linebegspr].pal += (sprite[linebegspr].pal<255);

        if (PRESSED_KEYSC(PGDN))
            sprite[linebegspr].pal -= (sprite[linebegspr].pal>0);

        M32_DrawRoomsAndMasks();

        ch = keyGetChar();

        if (keystatus[sc_Escape]) break;

        clearkeys();

        g_mouseBits = 0;
        searchx = osearchx;
        searchy = osearchy;

        inputchecked = 1;

        ExtCheckKeys();

        Bsprintf(tempbuf, "^%dText entry mode.^%d Navigation keys change vars.", editorcolors[10], whitecol);
        printmessage256(0,0,tempbuf);
        Bsprintf(buffer, "Hgap=%d, Vgap=%d, SPCgap=%d, Shd=%d, Pal=%d",
                 hgap, vgap, spcgap[alphidx], TrackerCast(sprite[linebegspr].shade), TrackerCast(sprite[linebegspr].pal));
        printmessage256(0, 9, buffer);
        videoShowFrame(1);

        // ---
        sp = &sprite[curspr];
        if (!doingspace)
        {
            dax = sp->x; day = sp->y;
            daang = sp->ang;
        }

        j = sp->xrepeat*(hgap+tilesiz[sp->picnum].x+2);
        {
            vec3_t vect;
            vect.x = dax + ((j*sintable[daang])>>17);
            vect.y = day - ((j*sintable[(daang+512)&2047])>>17);
            vect.z = sp->z;
            setsprite(cursor,&vect);
        }

        if (ch>=33 && ch<=126 && alphabets[alphidx].pic[ch-33] >= 0)
        {
            int16_t sect;

            // mapping char->tilenum
            t = alphabets[alphidx].pic[ch-33];
            j = sp->xrepeat*(hgap+tilesiz[sp->picnum].x+tilesiz[t].x);

            dax += (j*sintable[daang])>>17;
            day -= (j*sintable[(daang+512)&2047])>>17;
            dax += (j*sintable[(sprite[curspr].ang+2560)&2047])>>17;
            day += (j*sintable[(sprite[curspr].ang+2048)&2047])>>17;

            sect = sprite[curspr].sectnum;
            updatesector(dax,day,&sect);
            if (Numsprites < MAXSPRITES && sect >= 0)
            {
                i = insertsprite(sect,0);
                Bmemcpy(&sprite[i], &sprite[linebegspr], sizeof(spritetype));
                sprite[i].sectnum = sect;

                sprite[i].x = dax, sprite[i].y = day;
                sprite[i].picnum = t;
                sprite[i].ang = daang;

                sprite[i].xoffset = -picanm[t].xofs;
                sprite[i].yoffset = -picanm[t].yofs;
                sprite[i].xoffset += alphabets[alphidx].xofs[(int32_t)ch-33];
                sprite[i].yoffset += alphabets[alphidx].yofs[(int32_t)ch-33];

                DoSpriteOrnament(i);

                for (k=0; k<M32_TEXTENTRY_ARTFREQ_LIMIT; k++)
                    localartfreq[k] = 0;
                for (k=0; k<MAXSPRITES; k++)
                    if (sprite[k].statnum < MAXSTATUS)
                        localartfreq[sprite[k].picnum]++;

                curspr = i;
                doingspace = 0;

                asksave = 1;

                if (numletters >= stackallocsize)
                {
                    stackallocsize *= 2;
                    spritenums = (int16_t *)Xrealloc(spritenums, stackallocsize*sizeof(int16_t));
                }
                spritenums[numletters++] = i;
            }
        }
        else if (ch == 32)
        {
            dax += ((sp->xrepeat*spcgap[alphidx]*sintable[daang])>>17);
            day -= ((sp->xrepeat*spcgap[alphidx]*sintable[(daang+512)&2047])>>17);
            doingspace = 1;
        }
        else if (ch == 8)  // backspace
        {
            if (doingspace)
                doingspace = 0;
            else if (numletters > 0)
            {
                int16_t last = spritenums[numletters-1];

                if (sprite[last].z != sprite[linebegspr].z)  // only "delete" line break
                {
                    sprite[linebegspr].z = sprite[last].z;
                    curspr = last;
                }
                else if (numletters > 1)
                {
                    int16_t sectolast = spritenums[numletters-2];

                    if (sprite[last].z == sprite[sectolast].z)
                        curspr = sectolast;
                    else  // if we delete the first letter on the line
                        curspr = linebegspr;

                    numletters--;
                    deletesprite(last);

                    asksave = 1;
                }
                else
                {
                    numletters--;
                    deletesprite(last);

                    curspr = linebegspr;
                    asksave = 1;
                }
            }
            else
            {
                sprite[linebegspr].z -= ((sprite[linebegspr].yrepeat*(vgap+tilesiz[basetile].y))<<2);
                asksave = 1;
            }
        }
        else if (ch == 13)  // enter
        {
            sprite[linebegspr].z += ((sprite[linebegspr].yrepeat*(vgap+tilesiz[basetile].y))<<2);
            curspr = linebegspr;
            doingspace = 0;
            asksave = 1;
        }
    }

ERROR_TOOMANYSPRITES:
    if (cursor < 0) message("Too many sprites in map!");
    else deletesprite(cursor);

    M32_TEXTENTRY_FREE(spritenums);

    clearkeys();

    lockclock = (int32_t) totalclock;  //Reset timing
}

#undef M32_TEXTENTRY_IS_WALL_ALIGNED
#undef M32_TEXTENTRY_CURSOR_TILE
#undef M32_TEXTENTRY_ARTFREQ_LIMIT
#undef M32_TEXTENTRY_FREE
