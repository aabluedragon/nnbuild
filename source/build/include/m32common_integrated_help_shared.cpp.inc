#ifndef M32_IHELP_LOG_LOADING
# define M32_IHELP_LOG_LOADING(name) LOG_F(INFO, "Loading %s", (name))
#endif

#ifndef M32_IHELP_LOG_MISSING
# define M32_IHELP_LOG_MISSING(name) ((void)0)
#endif

#ifndef M32_IHELP_HEADER_YDIM
# define M32_IHELP_HEADER_YDIM ydim
#endif

// PK_ vvvv
typedef struct helppage_
{
    int32_t numlines;
    char line[1][80];  // C99 flexible array member
} helppage_t;

static helppage_t **helppage=NULL;
static int32_t numhelppages=0;

static int32_t emptyline(const char *start)
{
    int32_t i;
    for (i=0; i<80; i++)
    {
        if (start[i]=='\n' || !start[i]) break;
        if (start[i]!=' ' && start[i]!='\t' && start[i]!='\r')
            return 0;
    }
    return 1;
}

static int32_t newpage(const char *start)
{
    int32_t i;
    for (i=80-1; i>=0; i--)
    {
        if (start[i] == '^' && start[i+1] == 'P')
            return 1;
    }
    return 0;
}

#define IHELP_INITPAGES 32
#define IHELP_INITLINES 16

static void ReadHelpFile(const char *name)
{
    BFILE *fp;
    int32_t i, j, k, numallocpages;
    int32_t pos, charsread=0;
    helppage_t *hp;
    char skip=0;

    M32_IHELP_LOG_LOADING(name);

    if ((fp=fopenfrompath(name,"rb")) == NULL)
    {
        M32_IHELP_LOG_MISSING(name);
        return;
    }

    helppage=(helppage_t **)Xmalloc(IHELP_INITPAGES * sizeof(helppage_t *));
    numallocpages=IHELP_INITPAGES;

    i=0;
    while (!Bfeof(fp) && !ferror(fp))
    {
        while (!Bfeof(fp))    // skip empty lines
        {
            pos = ftell(fp);
            if (Bfgets(tempbuf, 80, fp) == NULL) break;
            charsread = ftell(fp)-pos;
            if (!newpage(tempbuf))
            {
                break;
            }
        }

        if (Bfeof(fp) || charsread<=0) break;

        hp=(helppage_t *)Xcalloc(1,sizeof(helppage_t) + IHELP_INITLINES*80);

        hp->numlines = IHELP_INITLINES;

        if (charsread == 79 && tempbuf[78]!='\n') skip=1;
        j=0;

        do
        {
            if (j >= hp->numlines)
            {
                hp=(helppage_t *)Xrealloc(hp, sizeof(helppage_t) + 2*hp->numlines*80);
                hp->numlines *= 2;
            }

            // limit the line length to 78 chars and probably get rid of the CR
            if (charsread>0)
            {
                tempbuf[charsread-1]=0;
                if (charsread>=2 && tempbuf[charsread-2]==0x0d)
                    tempbuf[charsread-2]=0;
            }

            Bmemcpy(hp->line[j], tempbuf, 80);

            for (k=charsread; k<80; k++) hp->line[j][k]=0;

            if (skip)
            {
                while (fgetc(fp)!='\n' && !Bfeof(fp)) /*skip rest of line*/;
                skip=0;
            }

            pos = ftell(fp);
            if (Bfgets(tempbuf, 80, fp) == NULL) break;
            charsread = ftell(fp)-pos;
            if (charsread == 79 && tempbuf[78]!='\n') skip=1;

            j++;

        }
        while (!newpage(tempbuf) && !Bfeof(fp) && charsread>0);

        hp=(helppage_t *)Xrealloc(hp, sizeof(helppage_t) + j*80);
        hp->numlines=j;

        if (i >= numallocpages)
        {
            helppage = (helppage_t **)Xrealloc(helppage, 2*numallocpages*sizeof(helppage_t *));
            numallocpages *= 2;
        }
        helppage[i] = hp;
        i++;
    }

    helppage =(helppage_t **)Xrealloc(helppage, i*sizeof(helppage_t *));
    numhelppages = i;

    Bfclose(fp);
}

// why can't MSVC allocate an array of variable size?!
#define IHELP_NUMDISPLINES 110 // ((overridepm16y>>4)+(overridepm16y>>5)+(overridepm16y>>7)-2)
#define IHELP_PATLEN 45
extern int32_t overridepm16y;  // influences clearmidstatbar16()

static void IntegratedHelp(void)
{
    if (!helppage) return;

    overridepm16y = ydim;//3*STATUS2DSIZ;

    {
        int32_t i, j;
        static int32_t curhp=0, curline=0;
        int32_t highlighthp=-1, highlightline=-1, lasthighlighttime=0;
        char disptext[IHELP_NUMDISPLINES][80];
        char oldpattern[IHELP_PATLEN+1];

        Bmemset(oldpattern, 0, sizeof(char));
        //    clearmidstatbar16();

        videoBeginDrawing();
        CLEARLINES2D(0, ydim, 0);
        videoEndDrawing();

        while (keystatus[KEYSC_ESC]==0 && keystatus[KEYSC_Q]==0 && keystatus[KEYSC_F1]==0)
        {
            idle_waitevent();
            if (handleevents())
                quitevent = 0;

            //        printmessage16("Help mode, press <Esc> to exit");

            if (keystatus[KEYSC_S])
            {
                fade_editor_screen(-1);
            }
            else
            {
                videoBeginDrawing();
                CLEARLINES2D(0, ydim, 0);
                videoEndDrawing();
            }

            // based on 'save as' dialog in overheadeditor()
            if (keystatus[KEYSC_S])    // text search
            {
                char ch, bad=0, pattern[IHELP_PATLEN+1];

                for (i=0; i<IHELP_PATLEN+1; i++) pattern[i]=0;

                i=0;
                keyFlushChars();
                while (bad == 0)
                {
                    _printmessage16("Search: %s_", pattern);
                    videoShowFrame(1);

                    idle_waitevent();

                    if (handleevents())
                        quitevent = 0;

                    ch = keyGetChar();

                    if (keystatus[sc_Escape]) bad = 1;
                    else if (ch == 13) bad = 2;
                    else if (ch > 0)
                    {
                        if (i > 0 && (ch == 8 || ch == 127))
                        {
                            i--;
                            pattern[i] = 0;
                        }
                        else if (i < IHELP_PATLEN && ch >= 32 && ch < 128)
                        {
                            pattern[i++] = ch;
                            pattern[i] = 0;
                        }
                    }
                }

                if (bad==1)
                {
                    keystatus[KEYSC_ESC] = keystatus[KEYSC_Q] = keystatus[KEYSC_F1] = 0;
                }

                if (bad==2)
                {
                    keystatus[KEYSC_ENTER] = 0;

                    for (i=curhp; i<numhelppages; i++)
                    {
                        for (j = (i==curhp)?(curline+1):0; j<helppage[i]->numlines; j++)
                        {
                            // entering an empty pattern will search with the last used pattern
                            if (Bstrstr(helppage[i]->line[j], pattern[0]?pattern:oldpattern))
                            {
                                curhp = i;

                                if ((curline=j) <= helppage[i]->numlines - 32 /*-IHELP_NUMDISPLINES*/) /**/;
                                else if ((curline=helppage[i]->numlines- 32 /*-IHELP_NUMDISPLINES*/) >= 0) /**/;
                                else curline=0;

                                highlighthp = i;
                                highlightline = j;
                                lasthighlighttime = (int32_t) totalclock;
                                goto ENDFOR1;
                            }
                        }
                    }
ENDFOR1:
                    if (pattern[0])
                        Bmemcpy(oldpattern, pattern, IHELP_PATLEN+1);
                }
            }
            else if (PRESSED_KEYSC(T))    // goto table of contents
            {
                curhp=0;
                curline=0;
            }
            else if (PRESSED_KEYSC(G))    // goto arbitrary page
            {
                curhp=getnumber16("Goto page: ", 0, numhelppages-1, 0);
                curline=0;
            }
            else if (PRESSED_KEYSC(UP))    // scroll up
            {
                if (curline>0) curline--;
            }
            else if (PRESSED_KEYSC(DOWN))    // scroll down
            {
                if (curline + 32/*+IHELP_NUMDISPLINES*/ < helppage[curhp]->numlines) curline++;
            }
            else if (PRESSED_KEYSC(PGUP))    // scroll one page up
            {
                i=IHELP_NUMDISPLINES;
                while (i>0 && curline>0) i--, curline--;
            }
            else if (PRESSED_KEYSC(PGDN))    // scroll one page down
            {
                i=IHELP_NUMDISPLINES;
                while (i>0 && curline + 32/*+IHELP_NUMDISPLINES*/ < helppage[curhp]->numlines) i--, curline++;
            }
            else if (PRESSED_KEYSC(SPACE))   // goto next paragraph
            {
                for (i=curline, j=0; i < helppage[curhp]->numlines; i++)
                {
                    if (emptyline(helppage[curhp]->line[i])) { j=1; continue; }
                    if (j==1 && !emptyline(helppage[curhp]->line[i])) { j=2; break; }
                }
                if (j==2)
                {
                    if (i + 32 /*+IHELP_NUMDISPLINES*/ < helppage[curhp]->numlines)
                        curline=i;
                    else if (helppage[curhp]->numlines - 32/*-IHELP_NUMDISPLINES*/ > curline)
                        curline = helppage[curhp]->numlines - 32/*-IHELP_NUMDISPLINES*/;
                }
            }
            else if (PRESSED_KEYSC(BS))   // goto prev paragraph
            {
                for (i=curline-1, j=0; i>=0; i--)
                {
                    if (!emptyline(helppage[curhp]->line[i])) { j=1; continue; }
                    if (j==1 && emptyline(helppage[curhp]->line[i])) { j=2; break; }
                }
                if (j==2 || i==-1) curline=i+1;
            }
            else if (PRESSED_KEYSC(HOME))    // goto beginning of page
            {
                curline=0;
            }
            else if (PRESSED_KEYSC(END))    // goto end of page
            {
                if ((curline=helppage[curhp]->numlines - 32/*-IHELP_NUMDISPLINES*/) >= 0) /**/;
                else curline=0;
            }
            else if (PRESSED_KEYSC(LEFT) || PRESSED_KEYSC(LBRACK))    // prev page
            {
                if (curhp>0)
                {
                    curhp--;
                    curline=0;
                }
            }
            else if (PRESSED_KEYSC(RIGHT) || PRESSED_KEYSC(RBRACK))    // next page
            {
                if (curhp<numhelppages-1)
                {
                    curhp++;
                    curline=0;
                }
            }
            else    // '1'-'0' on the upper row
            {
                for (i=2; i<=11; i++)
                    if (keystatus[i]) break;
                if (i--<12 && i<numhelppages)
                {
                    curhp=i;
                    curline=0;
                }
            }

//            drawgradient();

            videoBeginDrawing();
            printext16(9, M32_IHELP_HEADER_YDIM-overridepm16y+9, editorcolors[4], -1, "Help Mode", 0);
            printext16(8, M32_IHELP_HEADER_YDIM-overridepm16y+8, editorcolors[12], -1, "Help Mode", 0);
            printext16(8 + 9*8 + 2*8, M32_IHELP_HEADER_YDIM-overridepm16y+8, editorcolors[15], -1, "(S:search)", 0);
            videoEndDrawing();

            if (curhp < helppage[0]->numlines)
                _printmessage16("%s", helppage[0]->line[curhp]);
            else
                _printmessage16("%d. (Untitled page)", curhp);

            for (i=0; j=(curhp==0)?(i+curline+1):(i+curline),
                    i<IHELP_NUMDISPLINES && j<helppage[curhp]->numlines; i++)
            {
                if (ydim-overridepm16y+28+i*9+32 >= ydim)
                    break;
                Bmemcpy(disptext[i], helppage[curhp]->line[j], 80);
                printext16(8, ydim-overridepm16y+28+i*9, editorcolors[10],
                           (j==highlightline && curhp==highlighthp
                            && totalclock-lasthighlighttime<120*5) ?
                           editorcolors[1] : -1,
                           disptext[i], 0);
            }

            videoShowFrame(1);
        }

        clearkeys();
    }

    overridepm16y = -1;
}

#undef M32_IHELP_LOG_LOADING
#undef M32_IHELP_LOG_MISSING
#undef M32_IHELP_HEADER_YDIM
