//////////////////// Key stuff ////////////////////

#define eitherALT   (keystatus[KEYSC_LALT] || keystatus[KEYSC_RALT])
#define eitherCTRL  (keystatus[KEYSC_LCTRL] || keystatus[KEYSC_RCTRL])
#define eitherSHIFT (keystatus[KEYSC_LSHIFT] || keystatus[KEYSC_RSHIFT])

#define PRESSED_KEYSC(Key) (keystatus[KEYSC_##Key] && !(keystatus[KEYSC_##Key]=0))

////

// All these variables need verification that all relevant editor stubs are actually implementing them correctly.

char getmessage[162], getmessageleng;
int32_t getmessagetimeoff; //, charsperline;

int32_t mousxplc, mousyplc;
int32_t mouseaction;

char *scripthist[SCRIPTHISTSIZ];
int32_t scripthistend;

int32_t g_lazy_tileselector;
int32_t fixmaponsave_sprites = 1;

int32_t showambiencesounds=2;

int32_t autosave=180;

int32_t autocorruptcheck;
int32_t corruptcheck_noalreadyrefd, corruptcheck_heinum=1;
int32_t corruptcheck_game_duke3d=1;  // TODO: at startup, make conditional on which game we are editing for?
int32_t corrupt_tryfix_alt;
int32_t corruptlevel, numcorruptthings, corruptthings[MAXCORRUPTTHINGS];

////

#ifdef YAX_ENABLE
const char *yupdownwall[2] = {"upwall","downwall"};
const char *YUPDOWNWALL[2] = {"UPWALL","DOWNWALL"};
#endif

////

void drawgradient(void)
{
    int32_t i, col = editorcolors[25];
    videoBeginDrawing();
    for (i=ydim-STATUS2DSIZ+16; i<ydim && col>0; i++,col--)
        CLEARLINES2D(i, 1, (col<<24)|(col<<16)|(col<<8)|col);
    CLEARLINES2D(i, ydim-i, 0);
    videoEndDrawing();
}

static void message_common1(const char *tmpstr)
{
    Bstrncpyz(getmessage, tmpstr, sizeof(getmessage));

    getmessageleng = Bstrlen(getmessage);
    getmessagetimeoff = (int32_t) totalclock + 120*2 + getmessageleng*(120/30);
//    lastmessagetime = totalclock;
}

void message(const char *fmt, ...)
{
    char tmpstr[256];
    va_list va;

    va_start(va, fmt);
    Bvsnprintf(tmpstr, 256, fmt, va);
    va_end(va);

    message_common1(tmpstr);

    if (!mouseaction)
        OSD_Printf("%s\n", tmpstr);
}

void silentmessage(const char *fmt, ...)
{
    char tmpstr[256];
    va_list va;

    va_start(va, fmt);
    Bvsnprintf(tmpstr, 256, fmt, va);
    va_end(va);

    message_common1(tmpstr);
}

////////// tag labeling system //////////

typedef struct
{
    hashtable_t hashtab;
    char *label[32768];
    int32_t numlabels;
} taglab_t;

static taglab_t g_taglab;

static void tstrtoupper(char *s)
{
    int32_t i;
    for (i=0; s[i]; i++)
        s[i] = Btoupper(s[i]);
}

void taglab_init()
{
    int32_t i;

    g_taglab.numlabels = 0;
    g_taglab.hashtab.size = 16384;
    hash_init(&g_taglab.hashtab);

    for (i=0; i<32768; i++)
        DO_FREE_AND_NULL(g_taglab.label[i]);
}
