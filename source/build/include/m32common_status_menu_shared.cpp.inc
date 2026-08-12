////////// STATUS BAR MENU "class" //////////

#define MENU_MAX_ENTRIES (8*3)
#define MENU_ENTRY_SIZE 25  // max. length of label (including terminating NUL)

#define MENU_Y_SPACING 8
#define MENU_BASE_Y (ydim-STATUS2DSIZ+32)

#define MENU_FG_COLOR editorcolors[11]
#define MENU_BG_COLOR editorcolors[0]
#define MENU_BG_COLOR_SEL editorcolors[1]

#ifndef MENU_HAVE_DESCRIPTION
# define MENU_HAVE_DESCRIPTION 0
#endif

typedef struct StatusBarMenu_ {
    const char *const menuname;
    const int32_t custom_start_index;
    int32_t numentries;

    void (*process_func)(const struct StatusBarMenu_ *m, int32_t col, int32_t row);

    intptr_t auxdata[MENU_MAX_ENTRIES];
    char *description[MENU_MAX_ENTRIES];  // strdup'd description string, NULL if none
    char name[MENU_MAX_ENTRIES][MENU_ENTRY_SIZE];
} StatusBarMenu;

#define MENU_INITIALIZER_EMPTY(MenuName, ProcessFunc) \
    { MenuName, 0, 0, ProcessFunc, {}, {}, {} }
#define MENU_INITIALIZER(MenuName, CustomStartIndex, ProcessFunc, ...) \
    { MenuName, CustomStartIndex, CustomStartIndex, ProcessFunc, {}, {}, ## __VA_ARGS__ }

#if MENU_HAVE_DESCRIPTION
static void M_Clear(StatusBarMenu *m)
{
    int32_t i;

    m->numentries = 0;
    Bmemset(m->auxdata, 0, sizeof(m->auxdata));
    Bmemset(m->name, 0, sizeof(m->name));

    for (i=0; i<MENU_MAX_ENTRIES; i++)
        DO_FREE_AND_NULL(m->description[i]);
}

static int32_t M_HaveDescription(const StatusBarMenu *m)
{
    int32_t i;

    for (i=0; i<MENU_MAX_ENTRIES; i++)
        if (m->description[i] != NULL)
            return 1;

    return 0;
}
#endif

// NOTE: Does not handle description strings when unregistering existing entries.
static void M_UnregisterFunction(StatusBarMenu *m, intptr_t auxdata)
{
    int32_t i, j;

    for (i=m->custom_start_index; i<m->numentries; i++)
        if (m->auxdata[i]==auxdata)
        {
            for (j=i; j<m->numentries-1; j++)
            {
                m->auxdata[j] = m->auxdata[j+1];
                Bmemcpy(m->name[j], m->name[j+1], MENU_ENTRY_SIZE);
            }

            m->auxdata[j] = 0;
            Bmemset(m->name[j], 0, MENU_ENTRY_SIZE);

            m->numentries--;

            break;
        }
}

static void M_RegisterFunction(StatusBarMenu *m, const char *name, intptr_t auxdata, const char *description)
{
    int32_t i;

    for (i=8; i<m->numentries; i++)
    {
        if (m->auxdata[i]==auxdata)
        {
            // same auxdata, different name
            Bstrncpyz(m->name[i], name, MENU_ENTRY_SIZE);
            return;
        }
        else if (!Bstrncmp(m->name[i], name, MENU_ENTRY_SIZE))
        {
            // same name, different auxdata
            m->auxdata[i] = auxdata;
            return;
        }
    }

    if (m->numentries == MENU_MAX_ENTRIES)
        return;  // max reached

    Bstrncpyz(m->name[m->numentries], name, MENU_ENTRY_SIZE);
    m->auxdata[m->numentries] = auxdata;

#if MENU_HAVE_DESCRIPTION
    // NOTE: description only handled here (not above).
    if (description)
        m->description[m->numentries] = Xstrdup(description);
#else
    UNREFERENCED_PARAMETER(description);
#endif

    m->numentries++;
}

static void M_DisplayInitial(const StatusBarMenu *m)
{
    int32_t x = 8, y = MENU_BASE_Y+16;
    int32_t i;

    for (i=0; i<m->numentries; i++)
    {
        if (i==8 || i==16)
        {
            x += 208;
            y = MENU_BASE_Y+16;
        }

        printext16(x,y, MENU_FG_COLOR, MENU_BG_COLOR, m->name[i], 0);
        y += MENU_Y_SPACING;
    }

    printext16(m->numentries>8 ? 216 : 8, MENU_BASE_Y, MENU_FG_COLOR, -1, m->menuname, 0);

    clearkeys();
}

static void M_EnterMainLoop(StatusBarMenu *m)
{
    char disptext[80];
    const int32_t dispwidth = MENU_ENTRY_SIZE-1;

    int32_t i, col=0, row=0;
    int32_t crowmax[3] = {-1, -1, -1};
    int32_t xpos = 8, ypos = MENU_BASE_Y+16;

    if (m->numentries == 0)
    {
        printmessage16("%s menu has no entries", m->menuname);
        return;
    }

    Bmemset(disptext, 0, sizeof(disptext));

    Bassert((unsigned)m->numentries <= MENU_MAX_ENTRIES);
    for (i=0; i<=(m->numentries-1)/8; i++)
        crowmax[i] = (m->numentries >= (i+1)*8) ? 7 : (m->numentries-1)&7;

    drawgradient();
    M_DisplayInitial(m);

    while (keystatus[KEYSC_ESC] == 0)
    {
        idle_waitevent();
        if (handleevents())
            quitevent = 0;

        _printmessage16("Select an option, press <Esc> to exit");

        if (PRESSED_KEYSC(DOWN))
        {
            if (row < crowmax[col])
            {
                printext16(xpos, ypos+row*MENU_Y_SPACING, MENU_FG_COLOR, MENU_BG_COLOR, disptext, 0);
                row++;
            }
        }
        else if (PRESSED_KEYSC(UP))
        {
            if (row > 0)
            {
                printext16(xpos, ypos+row*MENU_Y_SPACING, MENU_FG_COLOR, MENU_BG_COLOR, disptext, 0);
                row--;
            }
        }
        else if (PRESSED_KEYSC(LEFT))
        {
            if (col > 0)
            {
                printext16(xpos, ypos+row*8, MENU_FG_COLOR, 0, disptext, 0);
                col--;
                xpos -= 208;
                disptext[dispwidth] = 0;
                row = min(crowmax[col], row);
            }
        }
        else if (PRESSED_KEYSC(RIGHT))
        {
            if (col < 2 && crowmax[col+1]>=0)
            {
                printext16(xpos, ypos+row*8, MENU_FG_COLOR, 0, disptext, 0);
                col++;
                xpos += 208;
                disptext[dispwidth] = 0;
                row = min(crowmax[col], row);
            }
        }

        for (i=Bsnprintf(disptext, dispwidth, "%s", m->name[col*8 + row]); i < dispwidth; i++)
            disptext[i] = ' ';

        if (PRESSED_KEYSC(ENTER))
        {
            Bassert(m->process_func != NULL);
            m->process_func(m, col, row);
            break;
        }

#if MENU_HAVE_DESCRIPTION
        if (M_HaveDescription(m))
        {
            const int32_t maxrows = 20;
            int32_t r;

            for (r=0; r<maxrows+1; r++)
                printext16(16-4, 16-4 + r*8, 0, MENU_BG_COLOR,  // 71 blanks:
                           "                                                                       ", 0);
            if (m->description[col*8 + row] != NULL)
                printext16(16, 16, MENU_FG_COLOR, MENU_BG_COLOR, m->description[col*8 + row], 0);
        }
#endif
        printext16(xpos, ypos+row*MENU_Y_SPACING, MENU_FG_COLOR, MENU_BG_COLOR_SEL, disptext, 0);
        videoShowFrame(1);
    }

    printext16(xpos, ypos+row*MENU_Y_SPACING, MENU_FG_COLOR, MENU_BG_COLOR, disptext, 0);
    videoShowFrame(1);

    keystatus[KEYSC_ESC] = 0;
}
