#define WIND1X   3
#define WIND1Y 150

static char const *tileinfo_colorstr = "";

static void tileinfo_doprint(int32_t x, int32_t y, char *buf, const char *label, int32_t value, int32_t value2, int32_t pos)
{
    int32_t small = (xdim<=640), i = ydim>>6;

    if (value2)
        Bsprintf(buf, "%s:%s%4d, %d", label, tileinfo_colorstr, value, value2);
    else Bsprintf(buf,"%s:%s%4d", label, tileinfo_colorstr, value);

    printext256(x+2, y+2+i*pos, 0, -1, buf, small);
    printext256(x, y+i*pos, whitecol, -1, buf, small);
}

// flags: 1:draw asterisk for lotag
//        2:draw asterisk for extra
//        4:print bottom-swapped wall members colored
static void drawtileinfo(const char *title,int32_t x,int32_t y,int32_t picnum,int32_t shade,int32_t pal,int32_t cstat,
                         int32_t lotag,int32_t hitag,int32_t extra, int32_t blend, int32_t statnum, uint32_t flags)
{
    char buf[64];
    int32_t small = (xdim<=640);

    if (tilesiz[picnum].x>0 && tilesiz[picnum].y>0)
    {
        int32_t x1 = x+80;

        if (small)
            x1 /= 2;

        x1 = (int32_t)(x1 * 320.0/xdim);
        const double scalediv = max(tilesiz[picnum].x,tilesiz[picnum].y)/24.0;
        const int32_t scale = (int32_t)(65536.0/scalediv);

        const int32_t oviewingrange=viewingrange, oyxaspect=yxaspect;
        renderSetAspect(65536, divscale16(ydim*320, xdim*200));
        // +1024: prevents rotatesprite from setting aspect itself
        rotatesprite_fs((x1+13)<<16,(y+11)<<16,scale,0, picnum,shade,pal, 2+1024);
        renderSetAspect(oviewingrange, oyxaspect);
    }

    x = (int32_t)(x * xdim/320.0);
    y = (int32_t)(y * ydim/200.0);

    videoBeginDrawing();
    printext256(x+2,y+2,0,-1,title,small);
    printext256(x,y,editorcolors[14],-1,title,small);

    if (flags&4)
    {
        Bsprintf(tempbuf, "^%d", editorcolors[14]);
        tileinfo_colorstr = tempbuf;
    }

    tileinfo_doprint(x, y, buf, "Pic", picnum, 0, 1);

    if (blend)
    {
        Bsprintf(tempbuf, "^%d", editorcolors[14]);
        tileinfo_colorstr = tempbuf;
    }

    tileinfo_doprint(x, y, buf, "Shd", shade, blend, 2);

    if (!(flags & 4))
        tileinfo_colorstr = "";

    tileinfo_doprint(x, y, buf, "Pal", pal, 0, 3);
    tileinfo_doprint(x, y, buf, "Cst", cstat, 0, 4);
    tileinfo_colorstr = "";
    tileinfo_doprint(x, y, buf, (flags&1)?"Lo*":"Lot", lotag, 0, 5);
    tileinfo_doprint(x, y, buf, "Hit", hitag, 0, 6);
    tileinfo_doprint(x, y, buf, (flags&2)?"Ex*":"Ext", extra, statnum, 7);

    videoEndDrawing();
}
//int32_t snap=0;
//int32_t saveval1,saveval2,saveval3;

static inline void getnumber_dochar(char *ptr, int32_t num)
{
    *ptr = (char) num;
}

static inline void getnumber_doint16_t(int16_t *ptr, int32_t num)
{
    *ptr = (int16_t) num;
}

static inline void getnumber_doint32(int32_t *ptr, int32_t num)
{
    *ptr = (int32_t) num;
}

static inline void getnumber_doint64(int64_t *ptr, int32_t num)
{
    *ptr = (int64_t) num;
}

static void getnumberptr256(const char *namestart, void *num, int32_t bytes, int32_t maxnumber, char sign, void *(func)(int32_t))
{
    char buffer[80], ch;
    int32_t danum = 0, oldnum;
    uint8_t flags = (sign>>1)&3;
    sign &= 1;

    switch (bytes)
    {
    case 1:
        danum = sign ? *(int8_t *)num : *(uint8_t *)num;
        break;
    case 2:
        danum = sign ? *(int16_t *)num : *(uint16_t *)num;
        break;
    case 4:
        danum = *(int32_t *)num;
        break;
    case 8:
        danum = *(int64_t *)num;
        break;
    }

    oldnum = danum;
    keyFlushChars();
    while (keystatus[sc_Escape] == 0)
    {
        if (handleevents())
            quitevent = 0;

        M32_DrawRoomsAndMasks();

        ch = keyGetChar();

        if (keystatus[sc_Escape]) break;

        clearkeys();

        g_mouseBits = 0;
        searchx = osearchx;
        searchy = osearchy;

        inputchecked = 1;

        ExtCheckKeys();

        Bsprintf(buffer,"%s%d",namestart,danum);
        if ((int32_t) totalclock & 32) Bstrcat(buffer,"_ ");
        printmessage256(0, 0, buffer);
        if (func != NULL)
        {
            Bsprintf(buffer,"%s",(char *)func((int32_t)danum));
            printmessage256(0, 9, buffer);
        }
        videoShowFrame(1);

        if (getnumber_internal1(ch, &danum, maxnumber, sign) ||
            getnumber_autocomplete(namestart, ch, &danum, flags))
        {
            if (danum != oldnum)
                asksave = 1;
            oldnum = danum;
            break;
        }

        switch (bytes)
        {
        case 1:
            getnumber_dochar((char *)num, danum);
            break;
        case 2:
            getnumber_doint16_t((int16_t *)num, danum);
            break;
        case 4:
            getnumber_doint32((int32_t *)num, danum);
            break;
        case 8:
            getnumber_doint64((int64_t *)num, danum);
            break;
        }
    }

    clearkeys();

    lockclock = (int32_t) totalclock;  //Reset timing

    switch (bytes)
    {
    case 1:
        getnumber_dochar((char *)num, oldnum);
        break;
    case 2:
        getnumber_doint16_t((int16_t *)num, oldnum);
        break;
    case 4:
        getnumber_doint32((int32_t *)num, oldnum);
        break;
    case 8:
        getnumber_doint64((int64_t *)num, oldnum);
        break;
    }
}
