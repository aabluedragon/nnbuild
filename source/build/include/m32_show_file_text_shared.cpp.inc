// Shared Mapster help-text renderer used by Duke3D, RR, and Blood map editors.
// Callers may override the file-handle type/sentinel when their VFS API differs.
#ifndef M32_SHOW_FILE_TEXT_KFD_TYPE
# define M32_SHOW_FILE_TEXT_KFD_TYPE int32_t
#endif
#ifndef M32_SHOW_FILE_TEXT_KFD_INVALID
# define M32_SHOW_FILE_TEXT_KFD_INVALID -1
#endif

// formerly Show2dText and Show3dText
static void ShowFileText(const char *name)
{
    M32_SHOW_FILE_TEXT_KFD_TYPE fp;
    int32_t t;
    uint8_t x=0,y=4,xmax=0,xx=0,col=0;

    if (!in3dmode())
    {
        clearmidstatbar16();
        drawgradient();
    }

    if ((fp=kopen4load(name,0)) == M32_SHOW_FILE_TEXT_KFD_INVALID)
    {
        Bsprintf(tempbuf, "ERROR: file \"%s\" not found.", name);
        if (in3dmode())
            printext256(1*4,4*8,whitecol,-1,tempbuf,0);
        else
            printext16(1*4,ydim-STATUS2DSIZ+4*8,editorcolors[11],-1,tempbuf,0);
        return;
    }

    t=65;
    videoBeginDrawing();
    while (t!=EOF && col<5)
    {
        t = 0;
        if (kread(fp,&t,1)<=0)
            t = EOF;
        while (t!=EOF && t!='\n' && x<250)
        {
            tempbuf[x]=t;
            t = 0;
            if (kread(fp,&t,1)<=0) t = EOF;
            x++;
            if (x>xmax) xmax=x;
        }
        tempbuf[x]=0;

        if (in3dmode())
            printext256(xx*4,(y*6)+2,whitecol,-1,tempbuf,1);
        else
            printext16(xx*4,ydim-STATUS2DSIZ+(y*6)+2,editorcolors[11],-1,tempbuf,1);

        x=0;
        y++;
        if (y>18)
        {
            col++;
            y=6;
            xx+=xmax;
            xmax=0;
        }
    }
    videoEndDrawing();

    kclose(fp);

}

#undef M32_SHOW_FILE_TEXT_KFD_TYPE
#undef M32_SHOW_FILE_TEXT_KFD_INVALID
