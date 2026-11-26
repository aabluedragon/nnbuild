// Shared Mapster32 compiler state used by Duke, RR, and Blood editors.

char        g_szScriptFileName[BMAX_PATH]   = "(none)";  // file we're currently compiling
static char g_szCurrentBlockName[BMAX_PATH] = "(none)";
static char g_szLastBlockName[BMAX_PATH]    = "NULL";

////// compiler state vvv
static char const *textptr;
static char const *start_textptr;
static char const *g_curkwptr;
static int32_t     def_tw;
int32_t            g_totalLines;
int32_t            g_lineNumber;
int32_t            g_numCompilerErrors;
int32_t            g_numCompilerWarnings;
int32_t            g_didDefineSomething;

typedef struct
{
    int32_t currentStateIdx;
    ofstype currentStateOfs;  // the offset to the start of the currently parsed states' code
    char *curStateMenuName;
    int32_t currentEvent;
    ofstype parsingEventOfs;

    int32_t checkingSwitch;
    int32_t numCases;
    instype *caseScriptPtr;  // the pointer to the start of the case table in a switch statement
    // first entry is 'default' code.
    instype *caseCodePtr;  // the pointer to the start of the different cases' code
    int32_t labelsOnly;
    int32_t numBraces;
    int32_t checkingIfElse, ifElseAborted;
} compilerstate_t;

static compilerstate_t cs;
static compilerstate_t cs_default = {-1, -1, NULL, -1, -1, 0, 0, NULL, NULL, 0, 0, 0, 0};
////// -------------------

instype *      apScript = NULL;
instype *      g_scriptPtr;
int32_t        g_scriptSize = 65536;
int32_t *      constants;
int32_t        constants_allocsize = 1024;
int32_t        g_numSavedConstants = 0;
static int32_t g_wasConstant       = 0;
char *         label;
int32_t *      labelval;
uint8_t *      labeltype;
int32_t        g_numLabels             = 0;
int32_t        g_numDefaultLabels      = 0;
static int32_t label_allocsize         = 512;
int32_t        g_stateCount            = 0;
statesinfo_t * statesinfo              = NULL;
static int32_t statesinfo_allocsize    = 512;
static int32_t interactive_compilation = 0;

static char tempbuf[2048];
static char tlabel[MAXLABELLEN];
static char tlabel2[MAXLABELLEN];

int32_t g_iReturnVar = 0;

int32_t m32_sortvar1;
int32_t m32_sortvar2;

char *  apStrings[MAXQUOTES + 1];
char *  apXStrings[MAXQUOTES + 1];
int32_t g_numXStrings = 0;

ofstype     aEventOffsets[MAXEVENTS];
int32_t     aEventSizes[MAXEVENTS];
uint16_t    aEventNumLocals[MAXEVENTS];
gamevar_t   aGameVars[MAXGAMEVARS];
gamearray_t aGameArrays[MAXGAMEARRAYS];
int32_t     g_gameVarCount     = 0;
int32_t     g_systemVarCount   = 0;
int32_t     g_gameArrayCount   = 0;
int32_t     g_systemArrayCount = 0;


// "magic" number for { and }, overrides line number in compiled code for later detection
#define IFELSE_MAGIC 31337


enum ScriptLabel_t
{
    LABEL_ANY    = -1,
    LABEL_DEFINE = 1,
    LABEL_EVENT  = 2
};

static const char *LabelTypeText[] =
{
    "define",
    "event"
};

static char *C_GetLabelType(int32_t type)
{
    uint32_t i;
    char x[64];

    x[0] = 0;
    for (i=0; i<sizeof(LabelTypeText)/sizeof(char *); i++)
    {
        if (!(type & (1<<i))) continue;
        if (x[0]) Bstrcat(x, " or ");
        Bstrcat(x, LabelTypeText[i]);
    }

    return Xstrdup(x);
}

#define NUMKEYWORDS (int32_t)ARRAY_SIZE(keyw)
#define NUMALTKEYWORDS (int32_t)ARRAY_SIZE(altkeyw)
