const char *ExtGetSpriteCaption(int16_t spritenum)
{
    static char tempbuf[1024];
    int32_t retfast = 0, lt;

    if (!(onnames>=3 && onnames<=8) || (onnames==7 && sprite[spritenum].picnum!=SECTOREFFECTOR))
        retfast = 1;
    if (onnames==5 && !tileInGroup(tilegroupItems, sprite[spritenum].picnum))
        retfast = 1;
    if (onnames==6 && sprite[spritenum].picnum != sprite[cursprite].picnum)
        retfast = 1;

    tempbuf[0] = 0;

    if (retfast)
        return tempbuf;

    lt = taglab_linktags(1, spritenum);
    lt &= ~(int)(sprite[spritenum].lotag<=0);
    lt &= ~(int)((sprite[spritenum].hitag<=0)<<1);

    if ((sprite[spritenum].lotag|sprite[spritenum].hitag) == 0)
    {
        Bmemset(tempbuf, 0, sizeof(tempbuf));
        SpriteName(spritenum,lo);

        if (lo[0]!=0)
        {
            Bsprintf(tempbuf,"%s",lo);

            if (sprite[spritenum].pal==1)
                Bstrcat(tempbuf," (MULTIPLAYER)");
        }

        return tempbuf;
    }

    char histr[TAGLAB_MAX+16], lostr[TAGLAB_MAX+16];

    taglab_handle1(lt&2, sprite[spritenum].hitag, histr);

    if (sprite[spritenum].picnum==SECTOREFFECTOR)
    {
        if (onnames!=8)
        {
            Bmemset(tempbuf, 0, sizeof(tempbuf));
            Bsprintf(lo,"%s",SectorEffectorText(spritenum));
            Bsprintf(tempbuf,"%s, %s",lo, histr);
        }
    }
    else
    {
        Bmemset(tempbuf, 0, sizeof(tempbuf));
        taglab_handle1(lt&1, sprite[spritenum].lotag, lostr);
        SpriteName(spritenum,lo);

        if (sprite[spritenum].extra != -1)
            Bsprintf(tempbuf,"%s,%s,%d %s", histr, lostr, TrackerCast(sprite[spritenum].extra), lo);
        else
            Bsprintf(tempbuf,"%s,%s %s", histr, lostr, lo);
    }

    return tempbuf;

} //end
