    if ((bstatus&1) && eitherSHIFT)
        mskip=1;

    if ((bstatus&1) && eitherSHIFT && AIMING_AT_CEILING_OR_FLOOR && (g_mousePos.x|g_mousePos.y))
    {
        int32_t fw,x1,y1,x2,y2,stat,ma,a=0;

        stat = SECTORFLD(searchsector,stat, AIMING_AT_FLOOR);
        if (stat&64) // align to first wall
        {
            fw=sector[searchsector].wallptr;
            x1=wall[fw].x,y1=wall[fw].y;
            x2=POINT2(fw).x,y2=POINT2(fw).y;
            a=getangle(x1-x2,y1-y2);
        }
        mouseax+=g_mousePos.x; mouseay+=g_mousePos.y;
        ma = getangle(mouseax,mouseay);
        ma += ang-a;

        i = stat;
        i = (i&0x4)+((i>>4)&3);
        if (stat&64) // align to first wall
            switch (i)
            {
            case 0:break;
            case 1:ma=-ma; break;
            case 2:ma=1024-ma; break;
            case 3:ma+=1024; break;
            case 4:ma=-512-ma; break;
            case 5:ma+=512; break;
            case 6:ma-=512; break;
            case 7:ma=512-ma; break;
            }
        else
            switch (i)
            {
            case 0:ma=-ma; break;
            case 1:break;
            case 2:ma+=1024; break;
            case 3:ma=1024-ma; break;
            case 4:ma-=512; break;
            case 5:ma=512-ma; break;
            case 6:ma=-512-ma; break;
            case 7:ma+=512; break;
            }

        a = ksqrt(uhypsq(mouseax,mouseay));
        if (a)
        {
            int32_t mult = (stat&8) ? 8192 : 8192*2;
            x1 = -a*sintable[(ma+2048)&2047]/mult;
            y1 = -a*sintable[(ma+1536)&2047]/mult;

            if (x1||y1)
            {
                mouseax=0;
                mouseay=0;

                if (AIMING_AT_CEILING_OR_FLOOR)
                {
                    changedir = 1-2*(x1<0);
                    x1 = klabs(x1);
#ifdef YAX_ENABLE__COMPAT
                    if (yax_getbunch(searchsector, AIMING_AT_FLOOR) < 0)
#endif
                    while (x1--)
                        AIMED_CEILINGFLOOR(xpanning) = changechar(AIMED_CEILINGFLOOR(xpanning),changedir,0,0);

                    changedir = 1-2*(y1<0);
                    y1 = klabs(y1);

                    while (y1--)
                        AIMED_CEILINGFLOOR(ypanning) = changechar(AIMED_CEILINGFLOOR(ypanning),changedir,0,0);

                    silentmessage("Sector %d %s panning: %d, %d", searchsector, typestr[searchstat],
                                  AIMED_CEILINGFLOOR(xpanning), AIMED_CEILINGFLOOR(ypanning));
                    asksave=1;
                }
            }
        }
        g_mousePos.x=0;
        g_mousePos.y=0;
    }
