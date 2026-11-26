static void ExtSE40Draw(int32_t spnum,int32_t x,int32_t y,int32_t z,int16_t a,int16_t h)
{
    static int32_t tempsectorz[MAXSECTORS];
    static int32_t tempsectorpicnum[MAXSECTORS];

    int32_t j=0,k=0;
    int32_t floor1 = 0, floor2 = 0, fofmode = 0, draw_both = 0;
    int32_t offx,offy,offz;

    if (sprite[spnum].ang!=512) return;

    // Things are a little different now, as we allow for masked transparent
    // floors and ceilings. So the FOF textures is no longer required
    //	if (!bitmap_test(gotpic, FOF))
    //		return;
    //	bitmap_clear(gotpic, FOF);

    if (tilesiz[562].x)
    {
        fofsizex = tilesiz[562].x;
        tilesiz[562].x = 0;
    }
    if (tilesiz[562].y)
    {
        fofsizey = tilesiz[562].y;
        tilesiz[562].y = 0;
    }

    floor1=spnum;

    if (sprite[spnum].lotag==42) fofmode=40;
    if (sprite[spnum].lotag==43) fofmode=41;
    if (sprite[spnum].lotag==44) fofmode=40;
    if (sprite[spnum].lotag==45) fofmode=41;

    // fofmode=sprite[spnum].lotag-2;

    // sectnum=sprite[j].sectnum;
    // sectnum=cursectnum;

    /*  recursive?
    for(j=0;j<MAXSPRITES;j++)
    {
    if(
    sprite[j].sectnum==sectnum &&
    sprite[j].picnum==1 &&
    sprite[j].lotag==110
    ) { DrawFloorOverFloor(j); break;}
    }
    */

    for (j=0; j<MAXSPRITES; j++)
    {
        if (sprite[j].picnum==1 && sprite[j].lotag==fofmode && sprite[j].hitag==sprite[floor1].hitag)
        {
            floor1=j;
            fofmode=sprite[j].lotag;
            break;
        }
    }

    if (fofmode==40) k=41;
    else k=40;

    for (j=0; j<MAXSPRITES; j++)
    {
        if (sprite[j].picnum==1 && sprite[j].lotag==k && sprite[j].hitag==sprite[floor1].hitag)
        {
            floor2=j;
            break;
        }
    }

//    i=floor1;
    offx=sprite[floor2].x-sprite[floor1].x;
    offy=sprite[floor2].y-sprite[floor1].y;
    offz=0;

    if (sprite[floor2].ang >= 1024)
        offz = sprite[floor2].z;
    else if (fofmode==41)
        offz = SPRITESEC(floor2).floorz;
    else
        offz = SPRITESEC(floor2).ceilingz;

    if (sprite[floor1].ang >= 1024)
        offz -= sprite[floor1].z;
    else if (fofmode==40)
        offz -= SPRITESEC(floor1).floorz;
    else
        offz -= SPRITESEC(floor1).ceilingz;

    for (j=0; j<MAXSPRITES; j++) // raise ceiling or floor
    {
        if (sprite[j].picnum==1 && sprite[j].lotag==k+2 && sprite[j].hitag==sprite[floor1].hitag)
        {
            if (k==40)
            {
                tempsectorz[sprite[j].sectnum] = SPRITESEC(j).floorz;
                SPRITESEC(j).floorz += (((z-SPRITESEC(j).floorz)/32768)+1)*32768;
                tempsectorpicnum[sprite[j].sectnum] = SPRITESEC(j).floorpicnum;
                SPRITESEC(j).floorpicnum = 562;
            }
            else
            {
                tempsectorz[sprite[j].sectnum] = SPRITESEC(j).ceilingz;
                SPRITESEC(j).ceilingz += (((z-SPRITESEC(j).ceilingz)/32768)-1)*32768;
                tempsectorpicnum[sprite[j].sectnum] = SPRITESEC(j).ceilingpicnum;
                SPRITESEC(j).ceilingpicnum = 562;
            }
            draw_both = 1;
        }
    }

    drawrooms(x+offx,y+offy,z+offz,a,h,sprite[floor2].sectnum);
    ExtAnalyzeSprites(0,0,0,0,0);
    renderDrawMasks();
    M32_ResetFakeRORTiles();

    if (draw_both)
    {
        for (j=0; j<MAXSPRITES; j++) // restore ceiling or floor for the draw both sectors
        {
            if (sprite[j].picnum==SECTOREFFECTOR &&
                    sprite[j].lotag==k+2 && sprite[j].hitag==sprite[floor1].hitag)
            {
                if (k==40)
                {
                    SPRITESEC(j).floorz = tempsectorz[sprite[j].sectnum];
                    SPRITESEC(j).floorpicnum = tempsectorpicnum[sprite[j].sectnum];
                }
                else
                {
                    SPRITESEC(j).ceilingz = tempsectorz[sprite[j].sectnum];
                    SPRITESEC(j).ceilingpicnum = tempsectorpicnum[sprite[j].sectnum];
                }
            }// end if
        }// end for

        // Now re-draw
        drawrooms(x+offx,y+offy,z+offz,a,h,sprite[floor2].sectnum);
        ExtAnalyzeSprites(0,0,0,0,0);
        renderDrawMasks();
        M32_ResetFakeRORTiles();
    }

} // end SE40

static void SE40Code(int32_t x,int32_t y,int32_t z,int32_t a,int32_t h)
{
    int32_t i;

    i = 0;
    while (i<MAXSPRITES)
    {
        int32_t t = sprite[i].lotag;
        switch (t)
        {
            //            case 40:
            //            case 41:
            //                ExtSE40Draw(i,x,y,z,a,h);
            //                break;
        case 42:
        case 43:
        case 44:
        case 45:
            if (cursectnum == sprite[i].sectnum)
                ExtSE40Draw(i,x,y,z,a,h);
            break;
        }
        i++;
    }
}
