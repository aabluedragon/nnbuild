    /* end Mapster32 */

    //  DoWater(horiz);

    if (framerateon)
    {
        static int32_t FrameCount = 0;
        static int32_t LastCount = 0;
        static int32_t LastSec = 0;
        static int32_t LastMS = 0;

        int32_t ms = timerGetTicks();
        int32_t howlong = ms - LastMS;

        if (howlong >= 0)
        {
            int32_t thisSec = ms/1000;
            int32_t x = (xdim <= 640);
            int32_t chars = Bsprintf(tempbuf, "%2u ms (%3u fps)", howlong, LastCount);

            int32_t const dax = m32_is2d3dmode() ? m32_2d3d.x + XSIZE_2D3D - 3: windowxy2.x;
            int32_t const day = m32_is2d3dmode() ? m32_2d3d.y + 4 : windowxy1.y;

            if (!x)
            {
                printext256(dax-(chars<<3)+1, day+2,0,-1,tempbuf,x);
                printext256(dax-(chars<<3), day+1,whitecol,-1,tempbuf,x);
            }
            else
            {
                printext256(dax-(chars<<2)+1, day+2,0,-1,tempbuf,x);
                printext256(dax-(chars<<2), day+1,whitecol,-1,tempbuf,x);
            }

            if (LastSec < thisSec)
            {
                LastCount = FrameCount / (thisSec - LastSec);
                LastSec = thisSec;
                FrameCount = 0;
            }
            FrameCount++;
        }
        LastMS = ms;
    }

    tempbuf[0] = 0;

    if ((bstatus&(4|2|1))==4 && !unrealedlook)  //PK
        Bsprintf(tempbuf,"VIEW");
    else if ((bstatus&(2|1))==2)
        Bsprintf(tempbuf, "Z%s", keystatus[KEYSC_HOME] && keystatus[KEYSC_END]
                                 ? " 128"
                                 : keystatus[KEYSC_HOME]
                                 ? " 256"
                                 : keystatus[KEYSC_END]
                                 ? " 512"
                                 : "");

    if ((bstatus&(2|1))==1 || (keystatus[KEYSC_SPACE]))
        Bsprintf(tempbuf,"LOCK");

    if (bstatus&1)
    {
        switch (searchstat)
        {
        case SEARCH_WALL:
        case SEARCH_MASKWALL:
            if (eitherALT)
                Bsprintf(tempbuf,"CEILING Z %s", eitherCTRL?"512":"");
            else if (eitherSHIFT)
                Bsprintf(tempbuf,"PAN %s", eitherCTRL?"8":"");
            else if (eitherCTRL)
                Bsprintf(tempbuf,"SCALE");
            break;
        case SEARCH_CEILING:
        case SEARCH_FLOOR:
            if (eitherALT)
                Bsprintf(tempbuf,"%s Z %s", AIMING_AT_CEILING?"CEILING":"FLOOR", eitherCTRL?"512":"");
            else if (eitherSHIFT)
                Bsprintf(tempbuf,"PAN");
            else if (eitherCTRL)
                Bsprintf(tempbuf,"SLOPE");
            break;
        case SEARCH_SPRITE:
            if (eitherALT)
                Bsprintf(tempbuf,"MOVE Z %s", eitherCTRL?"1024":"");
            else if (eitherSHIFT)
                Bsprintf(tempbuf,"MOVE XY %s", eitherCTRL?"GRID":"");
            else if (eitherCTRL)
                Bsprintf(tempbuf,"SIZE");
            break;
        }
    }

    if (tempbuf[0] != 0)
    {
        i = (Bstrlen(tempbuf)<<3)+6;
        i = max((searchx+i)-(xdim-1), 0);

        j = max((searchy+16)-(ydim-1), 0);

        printext256(searchx+4+2-i, searchy+4+2-j, 0,-1,tempbuf,!(xdimgame > 640));
        printext256(searchx+4-i,   searchy+4-j,   whitecol,-1,tempbuf,!(xdimgame > 640));
    }

    if (helpon==1)
    {
        int32_t small = !(xdimgame > 640);
        for (i=0; i<MAXHELP3D; i++)
        {
            videoBeginDrawing();
            printext256(2, 8+(i*(8+!small))+2, 0, -1, Help3d[i], small);
            printext256(0, 8+(i*(8+!small)), whitecol, -1, Help3d[i], small);
            videoEndDrawing();

            switch (i)
            {
            case 8:
                Bsprintf(tempbuf,"%d",autosave);
                break;
            case 9:
                Bsprintf(tempbuf,"%s",SKILLMODE[skill]);
                break;
            case 10:
                Bsprintf(tempbuf,"%d",framerateon);
                break;
            case 11:
                Bsprintf(tempbuf,"%s",SPRDSPMODE[nosprites]);
                break;
            case 12:
                Bsprintf(tempbuf,"%d",shadepreview);
                break;
            case 13:
                Bsprintf(tempbuf,"%d",showinvisibility);
                break;
            default :
                Bsprintf(tempbuf," ");
                break;
            }

            videoBeginDrawing();
            if (!Bstrcmp(tempbuf,"0"))
                Bsprintf(tempbuf,"OFF");
            else if (!Bstrcmp(tempbuf,"1"))
                Bsprintf(tempbuf,"ON");
            else if (!Bstrcmp(tempbuf,"2"))
                Bsprintf(tempbuf,"ON (2)");

            printext256((20+(!small * 20))*8+2, 8+(i*(8+!small))+2, 0, -1, tempbuf, small);
            printext256((20+(!small * 20))*8, 8+(i*(8+!small)), whitecol, -1, tempbuf, small);
            videoEndDrawing();
        }
    }
