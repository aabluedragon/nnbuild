    if (PRESSED_KEYSC(P))  // P (parallaxing sky)
    {
        if (eitherCTRL)
        {
            parallaxtype = (parallaxtype+1)%3;
            message("Parallax type %d", parallaxtype);
        }
        else if (eitherALT)
        {
            if (ASSERT_AIMING)
            {
                Bsprintf(tempbuf, "%s pal: ", Typestr[searchstat]);
                getnumberptr256(tempbuf, &AIMED_CF_SEL(pal), sizeof(uint8_t), M32_MAXPALOOKUPS, 0, NULL);
                asksave = 1;
            }
        }
        else
        {
            if (AIMING_AT_WALL_OR_MASK || AIMING_AT_CEILING)
            {
                sector[searchsector].ceilingstat ^= 1;
                message("Sector %d ceiling parallax bit %s",searchsector, ONOFF(sector[searchsector].ceilingstat&1));
                asksave = 1;
            }
            else if (AIMING_AT_FLOOR)
            {
                sector[searchsector].floorstat ^= 1;
                message("Sector %d floor parallax bit %s",searchsector, ONOFF(sector[searchsector].floorstat&1));
                asksave = 1;
            }
        }
    }

    if (PRESSED_KEYSC(D))   //Alt-D  (adjust sprite[].clipdist)
    {
        if (eitherALT && AIMING_AT_SPRITE)
            sprite[searchwall].clipdist = getnumber256("Sprite clipdist: ", sprite[searchwall].clipdist, 255, 0);
    }

    VM_OnEvent(EVENT_KEYS3D, -1);
}// end 3d

// returns: whether sprite is out of grid
static int32_t jump_to_sprite(int32_t spritenum)
{
    const spritetype *spr = &sprite[spritenum];

    if (pos.x >= -editorgridextent && pos.x <= editorgridextent &&
            pos.y >= -editorgridextent && pos.y <= editorgridextent)
    {
        pos.x = spr->x;
        pos.y = spr->y;

        // BZ_MAX?
        if (pos.z >= -(editorgridextent<<4) && pos.z <= editorgridextent<<4)
            pos.z = spr->z;

        ang = spr->ang;

        if ((unsigned)spr->sectnum < (unsigned)numsectors)
            cursectnum = spr->sectnum;

        return 0;
    }

    return 1;
}

static void DoSpriteSearch(int32_t dir)  // <0: backwards, >=0: forwards
{
    char did_wrap = 0, outofgrid;
    int32_t i, j, k = 0;

    dir = 1-2*(dir<0);

    for (gs_cursprite += dir;; gs_cursprite += dir)
    {
        if (gs_cursprite < 0 || gs_cursprite >= MAXSPRITES)
        {
            if (did_wrap)
                break;

            did_wrap = 1;
            gs_cursprite &= (MAXSPRITES-1);
        }

        if (sprite[gs_cursprite].statnum == MAXSTATUS)
            continue;

        for (i=0; i<3; i++)
            for (j=0; j<7; j++)
            {
                if (!gs_spriteTagInterested[i][j])
                    continue;

                if (i==0)
                {
                    switch (j)
                    {
                    case 0: k = sprite[gs_cursprite].x; break;
                    case 1: k = sprite[gs_cursprite].y; break;
                    case 2: k = sprite[gs_cursprite].z; break;
                    case 3: k = sprite[gs_cursprite].sectnum; break;
                    case 4: k = sprite[gs_cursprite].statnum; break;
                    case 5: k = (uint16_t)sprite[gs_cursprite].hitag; break;
                    case 6: k = (uint16_t)sprite[gs_cursprite].lotag; break;
                    }
                }
                else if (i==1)
                {
                    switch (j)
                    {
                    case 0:
                        k = sprite[gs_cursprite].cstat;
                        k &= gs_spriteTagValue[1][j];
                        break;
                    case 1: k = sprite[gs_cursprite].shade; break;
                    case 2: k = sprite[gs_cursprite].pal; break;
                    case 3: k = sprite[gs_cursprite].blend; break;
                    case 4:
                        k = gs_spriteTagValue[1][j];
                        if (k != sprite[gs_cursprite].xrepeat &&
                                k != sprite[gs_cursprite].yrepeat)
                            goto NEXTSPRITE;
                        break;
                    case 5:
                        k = gs_spriteTagValue[1][j];
                        if (k != sprite[gs_cursprite].xoffset &&
                                k != sprite[gs_cursprite].yoffset)
                            goto NEXTSPRITE;
                        break;
                    case 6: k = sprite[gs_cursprite].picnum; break;
                    }
                }
                else if (i==2)
                {
                    switch (j)
                    {
                    case 0: k = sprite[gs_cursprite].ang; break;
                    case 1: k = (uint16_t)sprite[gs_cursprite].xvel; break;
                    case 2: k = (uint16_t)sprite[gs_cursprite].yvel; break;
                    case 3: k = (uint16_t)sprite[gs_cursprite].zvel; break;
                    case 4: k = (uint16_t)sprite[gs_cursprite].owner; break;
                    case 5: k = sprite[gs_cursprite].clipdist; break;
                    case 6: k = sprite[gs_cursprite].extra; break;
                    }
                }

                if (k != gs_spriteTagValue[i][j])
                    goto NEXTSPRITE;
            }

        // found matching sprite
        outofgrid = jump_to_sprite(gs_cursprite);

        printmessage16("%s Sprite seach%s: found sprite %d%s", dir<0 ? "<" : ">",
                       did_wrap ? " (wrap)" : "", gs_cursprite, outofgrid?"(outside grid)":"");
        return;

NEXTSPRITE:
        ;
    }
    printmessage16("%s Sprite search: none found", dir<0 ? "<" : ">");
}
