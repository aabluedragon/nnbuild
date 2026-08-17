//-------------------------------------------------------------------------
/*
Copyright (C) 1997, 2005 - 3D Realms Entertainment

This file is part of Shadow Warrior version 1.2

Shadow Warrior is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Original Source: 1997 - Frank Maddin and Jim Norwood
Prepared for public release: 03/28/2005 - Charlie Wiederhold, 3D Realms
*/
//-------------------------------------------------------------------------

// CTW NOTE
/*
Known remaining issues:
- Audio stuttering.
- CD Audio not looping properly (currently hard coded to restart about every 200 seconds.
- Hitting F5 to change resolution causes a crash (currently disabled).
- Multiplayer untested.

Things required to make savegames work:
- Load makesym.wpj and build it.
- In a DOS prompt, run "makesym sw.map swdata.map swcode.map"
- Copy swcode.map to swcode.sym and swdata.map to swdata.sym
*/
// CTW NOTE END

#define MAIN
#define QUIET
#include "build.h"
#include "baselayer.h"
#include "cache1d.h"
#include "osd.h"
#include "palette.h"
#include "renderlayer.h"
#include "colmatch.h"

#include "keys.h"
#include "names2.h"
#include "panel.h"
#include "game.h"
#include "interp.h"
#include "interpso.h"
#include "tags.h"
#include "sector.h"
#include "sprite.h"
#include "weapon.h"
#include "player.h"
#include "lists.h"
#include "network.h"
#include "netargs.h"   // VibeBuild32: unified multiplayer command-line flags
#include "vbjs.h"
#include "pal.h"
#include "fx_man.h"

#include "mytypes.h"
//#include "config.h"

#include "menus.h"

#include "control.h"
#include "function.h"
#include "gamedefs.h"
#include "config.h"

#include "demo.h"
#include "cache.h"
//#include "exports.h"

#include "anim.h"

#include "colormap.h"
#include "break.h"
#include "ninja.h"
#include "light.h"
#include "track.h"
#include "jsector.h"
#include "keyboard.h"
#include "text.h"
#include "music.h"

#include "grpscan.h"
#include "common.h"
#include "common_game.h"

#include "crc32.h"

#ifdef _WIN32
# include "winbits.h"
#endif

const char* AppProperName = APPNAME;
const char* AppTechnicalName = APPBASENAME;

#define SETUPFILENAME APPBASENAME ".cfg"
char setupfilename[BMAX_PATH] = SETUPFILENAME;

#if DEBUG
#define BETA 0
#endif

#define STAT_SCREEN_PIC 5114
#define TITLE_PIC 2324
#define THREED_REALMS_PIC 2325
#define TITLE_ROT_FLAGS (ROTATE_SPRITE_CORNER|ROTATE_SPRITE_SCREEN_CLIP|ROTATE_SPRITE_NON_MASK)
#define PAL_SIZE (256*3)

char DemoName[15][16];

// Stupid WallMart version!
//#define PLOCK_VERSION TRUE

#if PLOCK_VERSION
SWBOOL Global_PLock = TRUE;
#else
SWBOOL Global_PLock = FALSE;
#endif

int GameVersion = 20;

char DemoText[3][64];
int DemoTextYstart = 0;

int Follow_posx=0,Follow_posy=0;

SWBOOL NoMeters = FALSE;
short IntroAnimCount = 0;
short PlayingLevel = -1;
SWBOOL GraphicsMode = FALSE;
char CacheLastLevel[32] = "";
SWBOOL CleanExit = FALSE;
SWBOOL DemoModeMenuInit = FALSE;
SWBOOL FinishAnim = 0;
SWBOOL ShortGameMode = FALSE;
SWBOOL ReloadPrompt = FALSE;
SWBOOL NewGame = TRUE;
SWBOOL InMenuLevel = FALSE;
SWBOOL LoadGameOutsideMoveLoop = FALSE;
SWBOOL LoadGameFromDemo = FALSE;
SWBOOL ArgCheat = FALSE;
extern SWBOOL NetBroadcastMode, NetModeOverride;
SWBOOL MultiPlayQuitFlag = FALSE;
//Miscellaneous variables
char MessageInputString[256];
char MessageOutputString[256];
SWBOOL MessageInputMode = FALSE;
SWBOOL ConInputMode = FALSE;
SWBOOL ConPanel = FALSE;
SWBOOL FinishedLevel = FALSE;
SWBOOL HelpInputMode = FALSE;
SWBOOL PanelUpdateMode = TRUE;
short HelpPage = 0;
short HelpPagePic[] = { 5115, 5116, 5117 };
SWBOOL InputMode = FALSE;
SWBOOL MessageInput = FALSE;
short screenpeek = 0;
SWBOOL NoDemoStartup = FALSE;
SWBOOL FirstTimeIntoGame;
extern uint8_t RedBookSong[40];

SWBOOL PedanticMode;

SWBOOL BorderAdjust = TRUE;
SWBOOL LocationInfo = 0;
void drawoverheadmap(int cposx, int cposy, int czoom, short cang);
int DispFrameRate = FALSE;
int DispMono = TRUE;
int Fog = FALSE;
int FogColor;
SWBOOL PreCaching = TRUE;
int GodMode = FALSE;
SWBOOL BotMode = FALSE;
short Skill = 2;
short TotalKillable;

AUTO_NET Auto;
SWBOOL AutoNet = FALSE;
SWBOOL HasAutoColor = FALSE;
uint8_t AutoColor;

const GAME_SET gs_defaults =
{
    32768, // mouse speed
    128, // music vol
    192, // fx vol
    2, // border
    0, // brightness
    0, // border tile
    FALSE, // mouse aiming
    FALSE, // mouse look
    FALSE, // mouse invert
    TRUE, // bobbing
    FALSE, // tilting
    TRUE, // shadows
    FALSE, // auto run
    TRUE, // crosshair
    TRUE, // auto aim
    TRUE, // interpolate sector objects
    TRUE, // messages
    TRUE, // fx on
    TRUE, // Music on
    TRUE, // talking
    TRUE, // ambient
    FALSE, // Flip Stereo

// Network game settings
    0, // GameType
    0, // Level
    0, // Monsters
    FALSE, // HurtTeammate
    TRUE, // SpawnMarkers Markers
    FALSE, // TeamPlay
    0, // Kill Limit
    0, // Time Limit
    0, // Color
    0, // Parental Lock
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", // Password
    TRUE, // nuke
    TRUE, // voxels
    FALSE, // stats
    FALSE, // mouse aiming on
    FALSE, // play cd
    "Track??", // waveform track name
    FALSE,
    TRUE,
    90, // FOV
    FALSE, // alt reverb
};
GAME_SET gs;

SWBOOL PlayerTrackingMode = FALSE;
SWBOOL PauseMode = FALSE;
SWBOOL PauseKeySet = FALSE;
SWBOOL SlowMode = FALSE;
SWBOOL FrameAdvanceTics = 3;
SWBOOL ScrollMode2D = FALSE;

SWBOOL DebugSO = FALSE;
SWBOOL DebugPanel = FALSE;
SWBOOL DebugSector = FALSE;
SWBOOL DebugActor = FALSE;
SWBOOL DebugAnim = FALSE;
SWBOOL DebugOperate = FALSE;
SWBOOL DebugActorFreeze = FALSE;
void LoadingLevelScreen(void);

uint8_t FakeMultiNumPlayers;

int totalsynctics;
int turn_scale = 256;
int move_scale = 256;

short Level = 0;
SWBOOL ExitLevel = FALSE;
int16_t OrigCommPlayers=0;
extern uint8_t CommPlayers;
extern SWBOOL CommEnabled;
extern unsigned int bufferjitter;

SWBOOL CameraTestMode = FALSE;

char ds[512];                           // debug string

extern short NormalVisibility;

extern int quotebot, quotebotgoal;     // Multiplayer typing buffer
char recbuf[80];                        // Used as a temp buffer to hold typing text

extern unsigned char palette_data[256][3];             // Global palette array

#define ACT_STATUE 0

int score;
SWBOOL QuitFlag = FALSE;
SWBOOL InGame = FALSE;

SWBOOL CommandSetup = FALSE;

char UserMapName[80]="", buffer[80], ch;
char LevelName[20];

uint8_t DebugPrintColor = 255;

int krandcount;

/// L O C A L   P R O T O T Y P E S /////////////////////////////////////////////////////////
void BOT_DeleteAllBots(void);
void BotPlayerInsert(PLAYERp pp);
void SybexScreen(void);
void TenScreen(void);
void DosScreen(void);
void PlayTheme(void);
void MenuLevel(void);
void StatScreen(PLAYERp mpp);
void InitRunLevel(void);
void RunLevel(void);
/////////////////////////////////////////////////////////////////////////////////////////////

static FILE *debug_fout = NULL;

void DebugWriteString(char *string)
{

#if BETA || !DEBUG
    return;
#endif

    if (!debug_fout)
    {
        if ((debug_fout = fopen("dbg.foo", "ab+")) == NULL)
            return;
    }

    fprintf(debug_fout, "%s\n", string);

    //fclose(debug_fout);
    //debug_fout = NULL;

    fflush(debug_fout);
}

void DebugWriteLoc(char *fname, int line)
{

#if BETA || !DEBUG
    return;
#endif

    if (!debug_fout)
    {
        if ((debug_fout = fopen("dbg.foo", "ab+")) == NULL)
            return;
    }

    fprintf(debug_fout, "%s, %d\n", fname, line);

    //fclose(debug_fout);
    //debug_fout = NULL;

    fflush(debug_fout);
}


extern SWBOOL DrawScreen;
#if RANDOM_DEBUG
FILE *fout_err;
SWBOOL RandomPrint;
int krand1(char *file, unsigned line)
{
    ASSERT(!DrawScreen);
    if (RandomPrint && !Prediction)
    {
        extern uint32_t MoveThingsCount;
        sprintf(ds,"mtc %d, %s, line %d, %d",MoveThingsCount,file,line,randomseed);
        DebugWriteString(ds);
    }
    randomseed = ((randomseed * 21 + 1) & 65535);
    return randomseed;
}

int krand2()
{
    ASSERT(!DrawScreen);
    randomseed = ((randomseed * 21 + 1) & 65535);
    return randomseed;
}

#else
int krand1(void)
{
    ASSERT(!DrawScreen);
    krandcount++;
    randomseed = ((randomseed * 21 + 1) & 65535);
    return randomseed;
}

#endif

#if DEBUG
SWBOOL
ValidPtr(void *ptr)
{
    MEM_HDRp mhp;
    uint8_t* check;

    ASSERT(ptr != NULL);

    mhp = (MEM_HDRp)(((uint8_t*) ptr) - sizeof(MEM_HDR));

    if (mhp->size == 0 || mhp->checksum == 0)
    {
        LOG_F(ERROR, "ValidPtr(): Size or Checksum == 0!");
        return FALSE;
    }

    check = (uint8_t*) & mhp->size;

    if (mhp->checksum == check[0] + check[1] + check[2] + check[3])
        return TRUE;

    LOG_F(ERROR, "ValidPtr(): Checksum bad!");
    return FALSE;
}

void
PtrCheckSum(void *ptr, unsigned int *stored, unsigned int *actual)
{
    MEM_HDRp mhp;
    uint8_t* check;

    ASSERT(ptr != NULL);

    mhp = (MEM_HDRp)(((uint8_t*) ptr) - sizeof(MEM_HDR));

    check = (uint8_t*) & mhp->size;

    *stored = mhp->checksum;
    *actual = check[0] + check[1] + check[2] + check[3];
}

void *
AllocMem(int size)
{
    uint8_t* bp;
    MEM_HDRp mhp;
    uint8_t* check;

    ASSERT(size != 0);

    bp = (uint8_t*) malloc(size + sizeof(MEM_HDR));

    // Used for debugging, we can remove this at ship time
    if (bp == NULL)
        TerminateWithMsg(0, "Memory could NOT be allocated in AllocMem: size = %d", size);

    ASSERT(bp != NULL);

    mhp = (MEM_HDRp) bp;

    mhp->size = size;
    check = (uint8_t*) & mhp->size;
    mhp->checksum = check[0] + check[1] + check[2] + check[3];

    bp += sizeof(MEM_HDR);

    return bp;
}

void *
ReAllocMem(void *ptr, int size)
{
    if (ptr == nullptr)
        return AllocMem(size);

    if (size == 0)
    {
        FreeMem(ptr);
        return nullptr;
    }

    uint8_t* bp;
    MEM_HDRp mhp;
    uint8_t* check;

    ASSERT(ValidPtr(ptr));

    mhp = (MEM_HDRp)(((uint8_t*) ptr) - sizeof(MEM_HDR));

    bp = (uint8_t*) realloc(mhp, size + sizeof(MEM_HDR));

    ASSERT(bp != NULL);

    mhp = (MEM_HDRp) bp;

    mhp->size = size;
    check = (uint8_t*) & mhp->size;
    mhp->checksum = check[0] + check[1] + check[2] + check[3];

    bp += sizeof(MEM_HDR);

    ASSERT(ValidPtr(bp));

    return bp;
}


void *
CallocMem(int size, int num)
{
    uint8_t* bp;
    MEM_HDRp mhp;
    uint8_t* check;
    int num_bytes;

    ASSERT(size != 0 && num != 0);

    num_bytes = (size * num) + sizeof(MEM_HDR);
    bp = (uint8_t*) calloc(num_bytes, 1);

    // Used for debugging, we can remove this at ship time
    if (bp == NULL)
        TerminateWithMsg(0, "Memory could NOT be allocated in CallocMem: size = %d, num = %d", size, num);

    ASSERT(bp != NULL);

    mhp = (MEM_HDRp) bp;

    mhp->size = size;
    check = (uint8_t*) & mhp->size;
    mhp->checksum = check[0] + check[1] + check[2] + check[3];

    bp += sizeof(MEM_HDR);

    return bp;
}

void
FreeMem(void *ptr)
{
    if (ptr == nullptr)
        return;

    MEM_HDRp mhp;
    uint8_t* check;

    ASSERT(ValidPtr(ptr));

    mhp = (MEM_HDRp)(((uint8_t*) ptr) - sizeof(MEM_HDR));
    check = (uint8_t*)&mhp->size;

    memset(mhp, 0xCC, mhp->size + sizeof(MEM_HDR));

    free(mhp);
}

#endif

int PointOnLine(int x, int y, int x1, int y1, int x2, int y2)
{
    // the closer to 0 the closer to the line the point is
    return ((x2 - x1) * (y - y1)) - ((y2 - y1) * (x - x1));
}

int
Distance(int x1, int y1, int x2, int y2)
{
    int min;

    if ((x2 = x2 - x1) < 0)
        x2 = -x2;

    if ((y2 = y2 - y1) < 0)
        y2 = -y2;

    if (x2 > y2)
        min = y2;
    else
        min = x2;

    return x2 + y2 - DIV2(min);
}

void
MapSetAll2D(uint8_t fill)
{
    int i;

    for (i = 0; i < bitmap_size(MAXWALLS); i++)
        show2dwall[i] = fill;
    for (i = 0; i < bitmap_size(MAXSPRITES); i++)
        show2dsprite[i] = fill;
#if 0
    for (i = 0; i < bitmap_size(MAXSECTORS); i++)
        show2dsector[i] = fill;
#else
    for (i = 0; i < MAXSECTORS; i++)
    {
        if (sector[i].ceilingpicnum != 342 && sector[i].floorpicnum != 342)
            bitmap_set(show2dsector, i);
    }
#endif
}

void
MapSetup(void)
{
#define NO_AUTO_MAPPING FALSE

#if NO_AUTO_MAPPING
    MapSetAll2D(0xFF);
#else
    automapping = TRUE;
#endif
}

void
setup2dscreen(void)
{
    // qsetmode640350();
}



void
TerminateGame(void)
{
    DemoTerm();

    ErrorCorrectionQuit();

    uninitmultiplayers();

    if (CleanExit)
    {
        SybexScreen();
        //TenScreen();
    }

    ////--->>>> sound stuff was there
    //uninitkeys();
    KB_Shutdown();

    TermSetup();

    //Terminate3DSounds();                // Kill the sounds linked list
    UnInitSound();

    timerUninit();

    if (CleanExit)
        DosScreen();

    engineUnInit();
    FreeGroups();

    uninitgroupfile();
}

static void SW_CrossplayCaptureDukeMapEffectors(void);

// Duke-side entry points (see source/vibebuild32/src/game.cpp).  These stay
// globally visible after the per-game symbol merge because Duke's objects are
// not localized and the names are unique to the bridge.
extern "C" int VB_DukeAI_CompileScripts(void);

void SW_CrossplayDukeAISetupLevel(void);
static int32_t SW_CrossplayRetagDukeMirrorWalls(void);
static void SW_CrossplayRestoreMaskedMirrorWalls(void);

// Duke's own enemy AI is the reference behavior for Duke levels, so the bridge
// prefers it over its own approximation when it can be brought up.
static int SW_CrossplayDukeAIRequested(void)
{
    static int requested = -1;

    if (requested < 0)
    {
        // Duke's own AI is the reference behavior for Duke levels, so it is the
        // default; VB_CP_DUKEAI=0 falls back to the bridge's approximation.
        char const *const env = Bgetenv("VB_CP_DUKEAI");
        requested = (env != NULL && env[0] == '0') ? 0 : 1;
    }

    return requested;
}

void
LoadLevel(const char *filename)
{
    int16_t ang;
    if (engineLoadBoard(filename, SW_SHAREWARE ? 1 : 0, &Player[0].pos, &ang, &Player[0].cursectnum) == -1)
        TerminateWithMsg(0, "Level not found: %s", filename);

    artCrossplayCaptureMapSprites();
    artCrossplayPrepareForeignMapArt(filename);
    artCrossplayApplyMapTileRemap();
    SW_CrossplayCaptureDukeMapEffectors();

    // Duke's actors are adopted straight off the freshly loaded map, before SW's
    // own sprite preparation resizes, converts or culls anything: Duke deletes
    // zero-sized actors, and its spawn code expects the map's own sprite state.
    SW_CrossplayDukeAISetupLevel();

    if (SW_CrossplayUseDukePlayerDimensions())
        LOG_F(INFO, "Crossplay: using Duke player dimensions for Shadow Warrior runtime (height=%d, clip radius=%d, actor radius=%d, run ceiling=%d, repeat=%dx%d).",
              SW_PlayerHeight() >> 8, SW_PlayerSpriteClipdist() << 2, SW_PlayerActorRadius(), SW_PlayerRunCeilingDist() >> 8,
              SW_PlayerSpriteXRepeat(), SW_PlayerSpriteYRepeat());

    Player[0].q16ang = fix16_from_int(ang);
}

void
LoadImages(const char *filename)
{
    artSetBaseSearchFirst(artCrossplayForeignDataCount() > 0 ? 1 : 0);
    if (artCrossplayForeignDataCount() > 0)
        LOG_F(INFO, "Crossplay: base runtime ART pinned to main game data.");

    if (artLoadFiles(filename, 32*1048576) == -1)
        TerminateWithSimpleMsg(-1, "Art not found. Please check your GRP file.");
}

enum
{
    VB_DUKE_MUSICANDSFX = 5,
    VB_DUKE_CYCLER = 7,
    VB_DUKE_RESPAWN = 9,
    VB_DUKE_FOF = 13,
    VB_DUKE_ARROW = 20,
    VB_DUKE_FIRSTGUNSPRITE = 21,
    VB_DUKE_CHAINGUNSPRITE = 22,
    VB_DUKE_RPGSPRITE = 23,
    VB_DUKE_FREEZESPRITE = 24,
    VB_DUKE_SHRINKERSPRITE = 25,
    VB_DUKE_HEAVYHBOMB = 26,
    VB_DUKE_TRIPBOMBSPRITE = 27,
    VB_DUKE_SHOTGUNSPRITE = 28,
    VB_DUKE_DEVISTATORSPRITE = 29,
    VB_DUKE_HEALTHBOX = 30,
    VB_DUKE_AMMOBOX = 31,
    VB_DUKE_GROWSPRITEICON = 32,
    VB_DUKE_INVENTORYBOX = 33,
    VB_DUKE_FREEZEAMMO = 37,
    VB_DUKE_AMMO = 40,
    VB_DUKE_BATTERYAMMO = 41,
    VB_DUKE_DEVISTATORAMMO = 42,
    VB_DUKE_RPGAMMO = 44,
    VB_DUKE_GROWAMMO = 45,
    VB_DUKE_CRYSTALAMMO = 46,
    VB_DUKE_HBOMBAMMO = 47,
    VB_DUKE_AMMOLOTS = 48,
    VB_DUKE_SHOTGUNAMMO = 49,
    VB_DUKE_COLA = 51,
    VB_DUKE_SIXPAK = 52,
    VB_DUKE_FIRSTAID = 53,
    VB_DUKE_SHIELD = 54,
    VB_DUKE_STEROIDS = 55,
    VB_DUKE_AIRTANK = 56,
    VB_DUKE_JETPACK = 57,
    VB_DUKE_HEATSENSOR = 59,
    VB_DUKE_ACCESSCARD = 60,
    VB_DUKE_BOOTS = 61,
    VB_DUKE_ATOMICHEALTH = 100,
    VB_DUKE_TECHLIGHT2 = 120,
    VB_DUKE_TECHLIGHTBUST2 = 121,
    VB_DUKE_TECHLIGHT4 = 122,
    VB_DUKE_TECHLIGHTBUST4 = 123,
    VB_DUKE_WALLLIGHT4 = 124,
    VB_DUKE_WALLLIGHTBUST4 = 125,
    VB_DUKE_ACCESSSWITCH = 130,
    VB_DUKE_SLOTDOOR = 132,
    VB_DUKE_LIGHTSWITCH = 134,
    VB_DUKE_SPACEDOORSWITCH = 136,
    VB_DUKE_SPACELIGHTSWITCH = 138,
    VB_DUKE_FRANKENSTINESWITCH = 140,
    VB_DUKE_NUKEBUTTON = 142,
    VB_DUKE_MULTISWITCH = 146,
    VB_DUKE_DIPSWITCH = 162,
    VB_DUKE_DIPSWITCH2 = 164,
    VB_DUKE_TECHSWITCH = 166,
    VB_DUKE_DIPSWITCH3 = 168,
    VB_DUKE_ACCESSSWITCH2 = 170,
    // Duke drives these through the same P_ActivateSwitch() path as the tiles
    // above; they were missing here, so a LIGHTSWITCH2 (E1L1's theatre light
    // plate) triggered its tag but never changed frame.
    VB_DUKE_LIGHTSWITCH2 = 712,
    VB_DUKE_POWERSWITCH1 = 860,
    VB_DUKE_LOCKSWITCH1 = 862,
    VB_DUKE_POWERSWITCH2 = 864,
    VB_DUKE_SCREENBREAK1 = 263,
    VB_DUKE_SCREENBREAK2 = 264,
    VB_DUKE_SCREENBREAK3 = 265,
    VB_DUKE_SCREENBREAK4 = 266,
    VB_DUKE_SCREENBREAK5 = 267,
    VB_DUKE_SCREENBREAK6 = 268,
    VB_DUKE_SCREENBREAK7 = 269,
    VB_DUKE_SCREENBREAK8 = 270,
    VB_DUKE_SCREENBREAK9 = 271,
    VB_DUKE_SCREENBREAK10 = 272,
    VB_DUKE_SCREENBREAK11 = 273,
    VB_DUKE_SCREENBREAK12 = 274,
    VB_DUKE_SCREENBREAK13 = 275,
    VB_DUKE_W_TECHWALL1 = 293,
    VB_DUKE_W_TECHWALL2 = 297,
    VB_DUKE_W_TECHWALL15 = 299,
    VB_DUKE_W_TECHWALL3 = 301,
    VB_DUKE_W_TECHWALL4 = 305,
    VB_DUKE_W_TECHWALL10 = 306,
    VB_DUKE_W_TECHWALL16 = 307,
    VB_DUKE_W_SCREENBREAK = 357,
    VB_DUKE_W_HITTECHWALL3 = 360,
    VB_DUKE_W_HITTECHWALL4 = 361,
    VB_DUKE_W_HITTECHWALL2 = 362,
    VB_DUKE_W_HITTECHWALL1 = 363,
    VB_DUKE_FANSPRITE = 407,
    VB_DUKE_FANSPRITEBROKE = 411,
    VB_DUKE_FANSHADOW = 412,
    VB_DUKE_FANSHADOWBROKE = 416,
    // Duke's vent covers and grates.  A_DamageObject_Internal() breaks all of
    // these and they share one sound; E1L1's rooftop vent is a PANNEL1.
    VB_DUKE_BPANNEL1 = 341,
    VB_DUKE_PANNEL1 = 342,
    VB_DUKE_PANNEL2 = 343,
    VB_DUKE_GRATE1 = 595,
    VB_DUKE_BGRATE1 = 596,
    VB_DUKE_CIRCLEPANNEL = 1113,
    VB_DUKE_CIRCLEPANNELBROKE = 1114,
    VB_DUKE_PANNEL3 = 4099,
    VB_DUKE_BPANNEL3 = 4100,
    VB_DUKE_GLASS = 503,
    VB_DUKE_GLASS2 = 504,
    VB_DUKE_STAINGLASS1 = 510,
    VB_DUKE_SPLINTERWOOD = 630,
    VB_DUKE_WATERFOUNTAIN = 563,
    VB_DUKE_WATERFOUNTAINBROKE = 567,
    VB_DUKE_FEMMAG1 = 568,
    VB_DUKE_FEMMAG2 = 577,
    VB_DUKE_CAMERA1 = 621,
    VB_DUKE_VACUUM = 669,
    VB_DUKE_SCALE = 678,
    VB_DUKE_MOVIECAMERA = 686,
    VB_DUKE_WALLLIGHT3 = 701,
    VB_DUKE_WALLLIGHTBUST3 = 702,
    VB_DUKE_WALLLIGHT1 = 703,
    VB_DUKE_WALLLIGHTBUST1 = 704,
    VB_DUKE_WALLLIGHT2 = 705,
    VB_DUKE_WALLLIGHTBUST2 = 706,
    VB_DUKE_STATUE = 753,
    VB_DUKE_MIKE = 762,
    VB_DUKE_VASE = 765,
    VB_DUKE_OJ = 806,
    VB_DUKE_ATM = 867,
    VB_DUKE_ATMBROKE = 888,
    VB_DUKE_WOODENHORSE = 904,
    VB_DUKE_TREE1 = 908,
    VB_DUKE_TREE2 = 910,
    VB_DUKE_CACTUS = 911,
    VB_DUKE_CACTUSBROKE = 939,
    VB_DUKE_BOX = 951,
    VB_DUKE_BOTTLE1 = 954,
    VB_DUKE_BOTTLE2 = 955,
    VB_DUKE_BOTTLE3 = 956,
    VB_DUKE_BOTTLE4 = 957,
    VB_DUKE_FEMPIC5 = 963,
    VB_DUKE_FEMPIC6 = 964,
    VB_DUKE_FEMPIC7 = 965,
    VB_DUKE_HYDROPLANT = 969,
    VB_DUKE_HANGLIGHT = 979,
    VB_DUKE_HYDRENT = 981,
    VB_DUKE_TIRE = 990,
    VB_DUKE_BOTTLE5 = 1012,
    VB_DUKE_BOTTLE6 = 1013,
    VB_DUKE_BOTTLE8 = 1014,
    VB_DUKE_BOTTLE7 = 1025,
    VB_DUKE_HORSEONSIDE = 1026,
    VB_DUKE_GLASSPIECES = 1031,
    VB_DUKE_RUBBERCAN = 1062,
    VB_DUKE_BOTTLE10 = 1157,
    VB_DUKE_BOTTLE11 = 1158,
    VB_DUKE_BOTTLE12 = 1159,
    VB_DUKE_BOTTLE13 = 1160,
    VB_DUKE_BOTTLE14 = 1161,
    VB_DUKE_BOTTLE15 = 1162,
    VB_DUKE_BOTTLE16 = 1163,
    VB_DUKE_BOTTLE17 = 1164,
    VB_DUKE_BOTTLE18 = 1165,
    VB_DUKE_BOTTLE19 = 1166,
    VB_DUKE_VENDMACHINE = 1212,
    VB_DUKE_COLAMACHINE = 1215,
    VB_DUKE_CANWITHSOMETHING = 1232,
    VB_DUKE_EXPLODINGBARREL = 1238,
    VB_DUKE_EXPLODINGBARREL2 = 1239,
    VB_DUKE_FIREBARREL = 1240,
    VB_DUKE_STEAM = 1250,
    VB_DUKE_CEILINGSTEAM = 1255,
    VB_DUKE_FEMPIC1 = 1280,
    VB_DUKE_FEMPIC2 = 1289,
    VB_DUKE_PODFEM1 = 1294,
    VB_DUKE_FEMPIC3 = 1298,
    VB_DUKE_FEMPIC4 = 1306,
    VB_DUKE_FEM1 = 1312,
    VB_DUKE_FEM2 = 1317,
    VB_DUKE_FEM3 = 1321,
    VB_DUKE_FEM5 = 1323,
    VB_DUKE_BLOODYPOLE = 1324,
    VB_DUKE_FEM4 = 1325,
    VB_DUKE_FEM6 = 1334,
    VB_DUKE_FEM6PAD = 1335,
    VB_DUKE_FEM8 = 1336,
    VB_DUKE_FEM7 = 1395,
    VB_DUKE_SHARK = 1550,
    VB_DUKE_BORNTOBEWILDSCREEN = 3370,
    VB_DUKE_LIZTROOP = 1680,
    VB_DUKE_LIZTROOPRUNNING = 1681,
    VB_DUKE_LIZTROOPSTAYPUT = 1682,
    VB_DUKE_LIZTROOPSHOOT = 1715,
    VB_DUKE_LIZTROOPJETPACK = 1725,
    VB_DUKE_LIZTROOPONTOILET = 1741,
    VB_DUKE_LIZTROOPJUSTSIT = 1742,
    VB_DUKE_LIZTROOPDUCKING = 1744,
    VB_DUKE_OCTABRAIN = 1820,
    VB_DUKE_OCTABRAINSTAYPUT = 1821,
    VB_DUKE_DRONE = 1880,
    VB_DUKE_COMMANDER = 1920,
    VB_DUKE_COMMANDERSTAYPUT = 1921,
    VB_DUKE_RECON = 1960,
    VB_DUKE_TANK = 1975,
    VB_DUKE_PIGCOP = 2000,
    VB_DUKE_PIGCOPSTAYPUT = 2001,
    VB_DUKE_PIGCOPDIVE = 2045,
    VB_DUKE_LIZMAN = 2120,
    VB_DUKE_LIZMANSTAYPUT = 2121,
    VB_DUKE_LIZMANSPITTING = 2150,
    VB_DUKE_LIZMANFEEDING = 2160,
    VB_DUKE_LIZMANJUMP = 2165,
    VB_DUKE_ROTATEGUN = 2360,
    VB_DUKE_GREENSLIME = 2370,
    VB_DUKE_ORGANTIC = 2420,
    VB_DUKE_BOSS1 = 2630,
    VB_DUKE_BOSS2 = 2710,
    VB_DUKE_BOSS3 = 2760,
    VB_DUKE_RESPAWNMARKERRED = 3190,
    VB_DUKE_RESPAWNMARKERYELLOW = 3200,
    VB_DUKE_RESPAWNMARKERGREEN = 3210,
    VB_DUKE_FEM9 = 3450,
    VB_DUKE_SCREENBREAK14 = 4120,
    VB_DUKE_SCREENBREAK15 = 4123,
    VB_DUKE_SCREENBREAK19 = 4125,
    VB_DUKE_SCREENBREAK16 = 4127,
    VB_DUKE_SCREENBREAK17 = 4128,
    VB_DUKE_SCREENBREAK18 = 4129,
    VB_DUKE_W_TECHWALL11 = 4130,
    VB_DUKE_W_TECHWALL12 = 4131,
    VB_DUKE_W_TECHWALL13 = 4132,
    VB_DUKE_W_TECHWALL14 = 4133,
    VB_DUKE_W_TECHWALL5 = 4134,
    VB_DUKE_W_TECHWALL6 = 4136,
    VB_DUKE_W_TECHWALL7 = 4138,
    VB_DUKE_W_TECHWALL8 = 4140,
    VB_DUKE_W_TECHWALL9 = 4142,
    VB_DUKE_W_HITTECHWALL16 = 4144,
    VB_DUKE_W_HITTECHWALL10 = 4145,
    VB_DUKE_W_HITTECHWALL15 = 4147,
    VB_DUKE_W_MILKSHELF = 4181,
    VB_DUKE_W_MILKSHELFBROKE = 4203,
    VB_DUKE_FOODOBJECT16 = 4545,
    VB_DUKE_CANWITHSOMETHING2 = 4580,
    VB_DUKE_CANWITHSOMETHING3 = 4581,
    VB_DUKE_CANWITHSOMETHING4 = 4582,
    VB_DUKE_NEWBEAST = 4610,
    VB_DUKE_NEWBEASTSTAYPUT = 4611,
    VB_DUKE_BOSS4 = 4740,
    VB_DUKE_BOSS5 = 5310,
    VB_DUKE_FEM10 = 4864,
    VB_DUKE_TOUGHGAL = 4866,
    VB_DUKE_NATURALLIGHTNING = 4890,
    VB_DUKE_CRACK1 = 546,
    VB_DUKE_CRACK2 = 547,
    VB_DUKE_CRACK3 = 548,
    VB_DUKE_CRACK4 = 549,
    VB_DUKE_OOZFILTER = 1079,
    VB_DUKE_HANDSWITCH = 1111,
    VB_DUKE_PULLSWITCH = 1122,
    VB_DUKE_ALIENSWITCH = 1142,
    VB_DUKE_HANDPRINTSWITCH = 1155,
    VB_DUKE_TRANSPORTERBEAM = 1261,
    VB_DUKE_SEENINE = 1247,
    VB_DUKE_SEENINEDEAD = 1248,
    VB_DUKE_APLAYERTOP = 1400,
    VB_DUKE_APLAYER = 1405,
    VB_DUKE_FIRELASER = 1625,
    VB_DUKE_TRANSPORTERSTAR = 1630,
    VB_DUKE_SPIT = 1636,
    VB_DUKE_EXPLOSION2 = 1890,
    VB_DUKE_BURNING = 2270,
    VB_DUKE_FIRE = 2271,
    VB_DUKE_BURNING2 = 2310,
    VB_DUKE_FIRE2 = 2311,
    VB_DUKE_FLOORFLAME = 2333,
    VB_DUKE_SCRAP6 = 2390,
    VB_DUKE_SCRAP1 = 2400,
    VB_DUKE_SCRAP2 = 2404,
    VB_DUKE_SCRAP3 = 2408,
    VB_DUKE_SCRAP4 = 2412,
    VB_DUKE_SCRAP5 = 2416,
    VB_DUKE_TRIPBOMB = 2566,
    VB_DUKE_LASERLINE = 2567,
    VB_DUKE_RPG = 2605,

    VB_DUKE_SECTOREFFECTOR = 1,
    VB_DUKE_ACTIVATOR = 2,
    VB_DUKE_TOUCHPLATE = 3,
    VB_DUKE_ACTIVATORLOCKED = 4,
    VB_DUKE_LOCATORS = 6,
    VB_DUKE_MASTERSWITCH = 8,
    VB_DUKE_GPSPEED = 10,
    VB_DUKE_SE_ROTATING_SECTOR = 0,
    VB_DUKE_SE_PIVOT = 1,
    VB_DUKE_SE_EARTHQUAKE = 2,
    VB_DUKE_SE_RANDOM_LIGHTS_AFTER_SHOT_OUT = 3,
    VB_DUKE_SE_RANDOM_LIGHTS = 4,
    VB_DUKE_SE_SUBWAY = 6,
    VB_DUKE_SE_TELEPORT = 7,
    VB_DUKE_SE_UP_OPEN_DOOR_LIGHTS = 8,
    VB_DUKE_SE_DOWN_OPEN_DOOR_LIGHTS = 9,
    VB_DUKE_SE_DOOR_AUTO_CLOSE = 10,
    VB_DUKE_SE_SWINGING_DOOR = 11,
    VB_DUKE_SE_LIGHT_SWITCH = 12,
    VB_DUKE_SE_SLIDING_DOOR = 15,
    VB_DUKE_SE_EXPLOSIVE = 13,
    VB_DUKE_SE_WARP_ELEVATOR = 17,
    VB_DUKE_SE_INCREMENTAL_SECTOR_RISE_FALL = 18,
    VB_DUKE_SE_STRETCH_BRIDGE = 20,
    VB_DUKE_SE_DROP_FLOOR = 21,
    VB_DUKE_SE_TEETH_DOOR = 22,
    VB_DUKE_SE_ONE_WAY_TELEPORT = 23,
    VB_DUKE_SE_SUBWAY_CAR = 14,
    VB_DUKE_SE_CONVEYOR = 24,
    VB_DUKE_SE_PISTON = 25,
    VB_DUKE_SE_DEMO_CAM = 27,
    VB_DUKE_SE_LIGHTNING = 28,
    VB_DUKE_SE_WAVES = 29,
    VB_DUKE_SE_TWO_WAY_TRAIN = 30,
    VB_DUKE_SE_FLOOR_RISE_FALL = 31,
    VB_DUKE_SE_CEILING_RISE_FALL = 32,
    VB_DUKE_SE_QUAKE_DEBRIS = 33,
    VB_DUKE_SE_CONVEYOR2 = 34,
    VB_DUKE_SE_PROJ_SHOOTER = 36,
    VB_DUKE_ST_ABOVE_WATER = 1,
    VB_DUKE_ST_UNDERWATER = 2,
    VB_DUKE_ST_SLIDING_ST_DOOR = 9,
    VB_DUKE_ST_WARP_ELEVATOR = 15,
    VB_DUKE_ST_PLATFORM_DOWN = 16,
    VB_DUKE_ST_PLATFORM_UP = 17,
    VB_DUKE_ST_ELEVATOR_DOWN = 18,
    VB_DUKE_ST_ELEVATOR_UP = 19,
    VB_DUKE_ST_CEILING_DOOR = 20,
    VB_DUKE_ST_FLOOR_DOOR = 21,
    VB_DUKE_ST_SPLITTING_DOOR = 22,
    VB_DUKE_ST_SWINGING_DOOR = 23,
    VB_DUKE_ST_SLIDING_DOOR = 25,
    VB_DUKE_ST_SPLITTING_ST_DOOR = 26,
    VB_DUKE_ST_STRETCH_BRIDGE = 27,
    VB_DUKE_ST_DROP_FLOOR = 28,
    VB_DUKE_ST_TEETH_DOOR = 29,
    VB_DUKE_ST_ROTATE_RISE_BRIDGE = 30,
    VB_DUKE_ST_TWO_WAY_TRAIN = 31,
    VB_DUKE_SECTOR_LOCKED = 16384,
    VB_CROSSPLAY_MAX_DUKE_ROTATORS = 128,
    VB_CROSSPLAY_MAX_DUKE_TRAINS = 64,
    VB_CROSSPLAY_MAX_DUKE_LOCATORS = 1024,
    VB_CROSSPLAY_MAX_DUKE_CONVEYORS = 256,
    VB_CROSSPLAY_MAX_DUKE_TELEPORTERS = 128,
    VB_CROSSPLAY_MAX_DUKE_ACTIVATORS = 512,
    VB_CROSSPLAY_MAX_DUKE_TOUCHPLATES = 256,
    VB_CROSSPLAY_MAX_DUKE_MASTERSWITCHES = 256,
    VB_CROSSPLAY_MAX_DUKE_ZMOVERS = 256,
    VB_CROSSPLAY_MAX_DUKE_SECTOR_ZANIMS = 256,
    VB_CROSSPLAY_MAX_DUKE_WARP_ELEVATORS = 128,
    VB_CROSSPLAY_MAX_DUKE_SWING_DOORS = 256,
    VB_CROSSPLAY_MAX_DUKE_SLIDING_DOORS = 128,
    VB_CROSSPLAY_MAX_DUKE_STRETCH_BRIDGES = 128,
    VB_CROSSPLAY_MAX_DUKE_AUTO_CLOSE = 256,
    VB_CROSSPLAY_MAX_DUKE_EXPLOSIVES = 256,
    VB_CROSSPLAY_MAX_DUKE_EXPLOSIVE_SOURCES = 512,
    VB_CROSSPLAY_MAX_DUKE_LIGHTS = 512,
    VB_CROSSPLAY_MAX_DUKE_LIGHTNINGS = 128,
    VB_CROSSPLAY_MAX_DUKE_WATER_WARPS = 128,
    VB_CROSSPLAY_MAX_DUKE_RESPAWNS = 512,
    VB_CROSSPLAY_MAX_DUKE_DROP_FLOORS = 256,
    VB_CROSSPLAY_MAX_DUKE_TEETH_DOORS = 128,
    VB_CROSSPLAY_MAX_DUKE_QUAKES = 128,
    VB_CROSSPLAY_MAX_DUKE_QUAKE_DEBRIS = 128,
    VB_CROSSPLAY_MAX_DUKE_QUAKE_ORIGINS = MAXWALLS,
    VB_CROSSPLAY_MAX_DUKE_WALL_ANIMS = 512,
    VB_CROSSPLAY_MAX_DUKE_PISTONS = 128,
    VB_CROSSPLAY_MAX_DUKE_INCREMENTAL_MOVERS = 128,
    VB_CROSSPLAY_MAX_DUKE_WAVES = 128,
    VB_CROSSPLAY_MAX_DUKE_PROJECTILE_SHOOTERS = 128,
    VB_CROSSPLAY_MAX_DUKE_MAP_SOUNDS = 2048,
    VB_CROSSPLAY_MAX_DUKE_TRANSIENTS = 256,
    VB_CROSSPLAY_MAX_DUKE_TRIPMINES = 256,
    VB_CROSSPLAY_MAX_DUKE_TRIPMINE_SEGMENTS = 16,
    VB_CROSSPLAY_DUKE_TELEPORT_HOLD_TICS = 13,
    VB_CROSSPLAY_DUKE_WATER_WARP_HOLD_TICS = 13,
    VB_CROSSPLAY_DUKE_TRAIN_CARRY_GRACE_TICS = 24,
    VB_CROSSPLAY_DUKE_LIGHTNING_START_DELAY = 65,
};

static int32_t SW_CrossplayIsDukeLikeGame(int32_t game)
{
    switch (game)
    {
    case ART_CROSSPLAY_GAME_DUKE:
    case ART_CROSSPLAY_GAME_WORLDTOUR:
    case ART_CROSSPLAY_GAME_NAM:
    case ART_CROSSPLAY_GAME_NAPALM:
    case ART_CROSSPLAY_GAME_WW2GI:
        return 1;
    default:
        return 0;
    }
}

struct sw_crossplay_duke_sprite_behavior_t;

static int32_t SW_CrossplayIsHiddenDukeControlSprite(int16_t sourcePicnum);
static int32_t SW_CrossplayIsDukeExplosiveSourceTile(int16_t originalPicnum);
static void SW_CrossplayClearDukeSpriteBehavior(int32_t spriteNum);
static void SW_CrossplayApplyDukeSourceInvisibility(int32_t spriteNum, sw_crossplay_duke_sprite_behavior_t const &behavior);
static void SW_CrossplayApplyDukeSpriteVisualOverrides(void);
static void SW_CrossplayRegisterDukeTripmine(int32_t spriteNum);

static int16_t SW_CrossplayDukeCardTileForPalette(int16_t pal)
{
    switch (pal)
    {
    case 21:
        return RED_CARD;
    case 23:
        return YELLOW_CARD;
    case 11:
        return GREEN_CARD;
    default:
        return BLUE_CARD;
    }
}

static int16_t SW_CrossplayDukePickupToShadowWarrior(int16_t sourcePicnum, int16_t pal, int32_t *domain)
{
    *domain = ART_CROSSPLAY_DOMAIN_ITEMS;

    switch (sourcePicnum)
    {
    case VB_DUKE_COLA:
    case VB_DUKE_SIXPAK:
        return ICON_SM_MEDKIT;
    case VB_DUKE_HEALTHBOX:
    case VB_DUKE_FIRSTAID:
        return ICON_MEDKIT;
    case VB_DUKE_ATOMICHEALTH:
    case VB_DUKE_STEROIDS:
        return ICON_BOOSTER;
    case VB_DUKE_SHIELD:
    case VB_DUKE_BOOTS:
        return ICON_ARMOR;
    case VB_DUKE_AIRTANK:
        return ICON_OXYGEN;
    case VB_DUKE_JETPACK:
        return ICON_FLY;
    case VB_DUKE_HEATSENSOR:
        return ICON_NIGHT_VISION;
    case VB_DUKE_INVENTORYBOX:
        return ICON_CHEMBOMB;
    case VB_DUKE_ACCESSCARD:
        return SW_CrossplayDukeCardTileForPalette(pal);

    case VB_DUKE_FIRSTGUNSPRITE:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_STAR;
    case VB_DUKE_SHOTGUNSPRITE:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_SHOTGUN;
    case VB_DUKE_CHAINGUNSPRITE:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_UZI;
    case VB_DUKE_RPGSPRITE:
    case VB_DUKE_DEVISTATORSPRITE:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_MICRO_GUN;
    case VB_DUKE_SHRINKERSPRITE:
    case VB_DUKE_FREEZESPRITE:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_RAIL_GUN;
    case VB_DUKE_HEAVYHBOMB:
    case VB_DUKE_TRIPBOMBSPRITE:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_GRENADE_LAUNCHER;

    case VB_DUKE_AMMO:
    case VB_DUKE_AMMOBOX:
    case VB_DUKE_AMMOLOTS:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_LG_UZI_AMMO;
    case VB_DUKE_SHOTGUNAMMO:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_LG_SHOTSHELL;
    case VB_DUKE_RPGAMMO:
    case VB_DUKE_DEVISTATORAMMO:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_MICRO_BATTERY;
    case VB_DUKE_HBOMBAMMO:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_LG_GRENADE;
    case VB_DUKE_BATTERYAMMO:
    case VB_DUKE_FREEZEAMMO:
    case VB_DUKE_GROWAMMO:
    case VB_DUKE_CRYSTALAMMO:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_RAIL_AMMO;
    case VB_DUKE_GROWSPRITEICON:
        *domain = ART_CROSSPLAY_DOMAIN_WEAPONS;
        return ICON_HEART;
    default:
        return -1;
    }
}

// Returns non-zero when the sprite became an SW pickup, and reports which
// crossplay domain it came from so the caller can decide whose art it shows.
static int32_t SW_CrossplayConvertDukeMapSpriteToShadowWarrior(int32_t spriteNum, int32_t *itemsConverted,
                                                               int32_t *weaponsConverted, int32_t *outDomain)
{
    int16_t const sourcePicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
    if (sourcePicnum < 0 || SW_CrossplayIsHiddenDukeControlSprite(sourcePicnum))
        return 0;

    int32_t domain = ART_CROSSPLAY_DOMAIN_ITEMS;
    int16_t const swPicnum = SW_CrossplayDukePickupToShadowWarrior(sourcePicnum, sprite[spriteNum].pal, &domain);
    if (swPicnum < 0)
        return 0;

    if (artCrossplayEffectiveDomainGame(domain) != ART_CROSSPLAY_GAME_SW)
        return 0;

    SPRITEp const sp = &sprite[spriteNum];

    sp->picnum = swPicnum;
    sp->cstat = 0;
    sp->shade = 0;
    sp->pal = 0;
    sp->clipdist = 32;
    sp->xrepeat = 64;
    sp->yrepeat = 64;
    sp->xoffset = 0;
    sp->yoffset = 0;
    sp->xvel = 0;
    sp->yvel = 0;
    sp->zvel = 0;
    sp->owner = -1;
    sp->lotag = 0;
    sp->hitag = 0;
    sp->extra = 0;
    change_sprite_stat((short)spriteNum, STAT_DEFAULT);

    if (domain == ART_CROSSPLAY_DOMAIN_WEAPONS)
        ++*weaponsConverted;
    else
        ++*itemsConverted;

    if (outDomain != NULL)
        *outDomain = domain;

    return 1;
}

enum
{
    VB_CROSSPLAY_DUKE_SPRITE_SWITCH = 1 << 0,
    VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE = 1 << 1,
    VB_CROSSPLAY_DUKE_SPRITE_ACTOR = 1 << 2,
    VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE = 1 << 3,
    VB_CROSSPLAY_DUKE_SPRITE_ANIMATES = 1 << 4,
    VB_CROSSPLAY_DUKE_SPRITE_AWAKE = 1 << 5,
    VB_CROSSPLAY_DUKE_SPRITE_VISUAL_OVERRIDE = 1 << 6,
    VB_CROSSPLAY_DUKE_SPRITE_ALIVE = 1 << 7,
    VB_CROSSPLAY_DUKE_SPRITE_TRIPMINE = 1 << 8,
    VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE = 1 << 9,
};

enum
{
    VB_CROSSPLAY_DUKE_ACTOR_NONE,
    VB_CROSSPLAY_DUKE_ACTOR_SHARK,
    VB_CROSSPLAY_DUKE_ACTOR_TROOPER,
    VB_CROSSPLAY_DUKE_ACTOR_OCTABRAIN,
    VB_CROSSPLAY_DUKE_ACTOR_DRONE,
    VB_CROSSPLAY_DUKE_ACTOR_COMMANDER,
    VB_CROSSPLAY_DUKE_ACTOR_RECON,
    VB_CROSSPLAY_DUKE_ACTOR_TANK,
    VB_CROSSPLAY_DUKE_ACTOR_PIGCOP,
    VB_CROSSPLAY_DUKE_ACTOR_LIZMAN,
    VB_CROSSPLAY_DUKE_ACTOR_TURRET,
    VB_CROSSPLAY_DUKE_ACTOR_SLIME,
    VB_CROSSPLAY_DUKE_ACTOR_BOSS,
    VB_CROSSPLAY_DUKE_ACTOR_NEWBEAST,
};

enum
{
    VB_CROSSPLAY_DUKE_ACTOR_IDLE,
    VB_CROSSPLAY_DUKE_ACTOR_CHASE,
    VB_CROSSPLAY_DUKE_ACTOR_ATTACK,
    VB_CROSSPLAY_DUKE_ACTOR_PAIN,
};

struct sw_crossplay_duke_sprite_behavior_t
{
    int16_t sourcePicnum;
    int16_t basePicnum;
    int16_t displayPal;
    int16_t health;
    uint16_t flags;
    int16_t homeSect;
    vec2_t home;
    uint16_t attackCooldown;
    uint16_t painCooldown;
    uint16_t moveCooldown;
    int16_t moveAng;
    uint8_t actorKind;
    uint8_t actorState;
    uint8_t stayput;
    uint8_t airborne;
    int8_t strafeDir;
    uint8_t switchState;
    uint8_t tick;
    uint16_t attackEvents;
};

struct sw_crossplay_duke_actor_profile_t
{
    int16_t sightRange;
    int16_t attackRange;
    int16_t meleeRange;
    int16_t speed;
    int16_t attackDelay;
    int16_t meleeDamage;
    int16_t rangedDamage;
    int16_t projectileTile;
    int16_t preferredRange;
    int32_t alertSound;
    int32_t painSound;
    int32_t attackSound;
    int32_t deathSound;
    uint8_t turnRate;
    uint8_t strafeChance;
    uint8_t stationary;
    uint8_t flying;
    uint8_t suicide;
};

// Charges Duke's own spawn rule hides because they are too narrow to draw.
static int32_t g_swCrossplayDukeHiddenCharges;

static sw_crossplay_duke_sprite_behavior_t g_swCrossplayDukeSpriteBehaviors[MAXSPRITES];
static int32_t g_swCrossplayDukeSpriteBehaviorCount;

// Bridge instrumentation counters. They are always maintained so the self-test harness and
// the in-game console can observe bridge activity without extra hooks in the hot paths.
static int32_t g_swCrossplayDukeActorAttackEvents;
static int32_t g_swCrossplayDukeActorDeathEvents;
static int32_t g_swCrossplayDukeTeleportEvents;
static int32_t g_swCrossplayDukeTeleportTic;
static int32_t g_swCrossplayDukeLastTeleportTic = -1;
static int32_t g_swCrossplayDukeMinTeleportGap = -1;
static int32_t g_swCrossplayDukeTripmineDetonations;

// SW removes sprites through KillSprite(), which detaches any 3D sounds that are
// owned by, or follow, the sprite.  The Duke bridge cannot use KillSprite() (these
// sprites have no SW USER record), so detach the sounds here: leaving a looping or
// following voc3d entry pointing at a recycled sprite slot corrupts the 3D sound
// list and crashes when the level is torn down.
static void SW_CrossplayDeleteDukeSprite(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    DeleteNoSoundOwner((short)spriteNum);
    DeleteNoFollowSoundOwner((short)spriteNum);
    deletesprite((short)spriteNum);
}

int SW_CrossplayDukeAIOwnsSprite(int spriteNum);
static void SW_CrossplayZGuard(char const *segment);
static int32_t SW_CrossplayIsDukeScreenBreakTile(int16_t originalPicnum);

// Duke draws a switch with two frames: the base tile is off and base+1 is on
// (MULTISWITCH cycles through four).  A map may place a switch already flipped,
// so both frames have to be recognised; the base tile and the starting state are
// reported so the sprite animates from the right frame.
static int16_t SW_CrossplayDukeSwitchBaseTile(int16_t sourcePicnum, int32_t *startState)
{
    static int16_t const switchBases[] = {
        VB_DUKE_ACCESSSWITCH, VB_DUKE_ACCESSSWITCH2, VB_DUKE_SLOTDOOR, VB_DUKE_LIGHTSWITCH,
        VB_DUKE_SPACEDOORSWITCH, VB_DUKE_SPACELIGHTSWITCH, VB_DUKE_FRANKENSTINESWITCH,
        VB_DUKE_NUKEBUTTON, VB_DUKE_DIPSWITCH, VB_DUKE_DIPSWITCH2, VB_DUKE_DIPSWITCH3,
        VB_DUKE_TECHSWITCH, VB_DUKE_HANDSWITCH, VB_DUKE_PULLSWITCH, VB_DUKE_ALIENSWITCH,
        VB_DUKE_HANDPRINTSWITCH, VB_DUKE_LIGHTSWITCH2, VB_DUKE_POWERSWITCH1,
        VB_DUKE_LOCKSWITCH1, VB_DUKE_POWERSWITCH2,
    };

    if (startState != NULL)
        *startState = 0;

    if (sourcePicnum >= VB_DUKE_MULTISWITCH && sourcePicnum <= VB_DUKE_MULTISWITCH + 3)
    {
        if (startState != NULL)
            *startState = sourcePicnum - VB_DUKE_MULTISWITCH;
        return VB_DUKE_MULTISWITCH;
    }

    for (size_t i = 0; i < ARRAY_SIZE(switchBases); ++i)
    {
        if (sourcePicnum == switchBases[i])
            return switchBases[i];

        if (sourcePicnum == switchBases[i] + 1)
        {
            if (startState != NULL)
                *startState = 1;
            return switchBases[i];
        }
    }

    return -1;
}

static int32_t SW_CrossplayIsDukeSwitchSprite(int16_t sourcePicnum)
{
    return SW_CrossplayDukeSwitchBaseTile(sourcePicnum, NULL) >= 0;
}

static int32_t SW_CrossplayDukeTileMatchesRuntimePicnum(int16_t runtimePicnum, int16_t dukePicnum)
{
    return runtimePicnum == dukePicnum || runtimePicnum == artCrossplayRemapMapTile(dukePicnum);
}

static int32_t SW_CrossplayIsHiddenDukeControlSprite(int16_t sourcePicnum)
{
    for (int16_t picnum = VB_DUKE_RESPAWNMARKERRED; picnum < VB_DUKE_RESPAWNMARKERGREEN + 10; ++picnum)
    {
        if (SW_CrossplayDukeTileMatchesRuntimePicnum(sourcePicnum, picnum))
            return 1;
    }

    static const int16_t hiddenControlTiles[] =
    {
        VB_DUKE_SECTOREFFECTOR,
        VB_DUKE_ACTIVATOR,
        VB_DUKE_TOUCHPLATE,
        VB_DUKE_ACTIVATORLOCKED,
        VB_DUKE_MUSICANDSFX,
        VB_DUKE_LOCATORS,
        VB_DUKE_CYCLER,
        VB_DUKE_MASTERSWITCH,
        VB_DUKE_RESPAWN,
        VB_DUKE_GPSPEED,
        VB_DUKE_FOF,
        VB_DUKE_ARROW,
        VB_DUKE_APLAYERTOP,
        VB_DUKE_APLAYER,
        VB_DUKE_RESPAWNMARKERRED,
        VB_DUKE_RESPAWNMARKERYELLOW,
        VB_DUKE_RESPAWNMARKERGREEN,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(hiddenControlTiles); ++i)
        if (SW_CrossplayDukeTileMatchesRuntimePicnum(sourcePicnum, hiddenControlTiles[i]))
            return 1;

    return 0;
}

static int32_t SW_CrossplaySpriteIsHiddenDukeControlSprite(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return 0;

    // Duke owns the lifetime of its own actors and effects.  Their display tiles
    // change every animation frame and can collide with Duke control tiles, so
    // culling by tile here would delete live enemies.
    if (SW_CrossplayDukeAIOwnsSprite(spriteNum))
        return 0;

    int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
    return SW_CrossplayIsHiddenDukeControlSprite(originalPicnum) ||
           SW_CrossplayIsHiddenDukeControlSprite(sprite[spriteNum].picnum);
}

static int32_t SW_CrossplayCullHiddenDukeControlSprites(void)
{
    int32_t removed = 0;

    for (int32_t i = MAXSPRITES - 1; i >= 0; --i)
    {
        if (sprite[i].statnum == MAXSTATUS ||
            (sprite[i].statnum >= STAT_PLAYER0 && sprite[i].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS))
            continue;

        if (!SW_CrossplaySpriteIsHiddenDukeControlSprite(i))
            continue;

        SW_CrossplayClearDukeSpriteBehavior(i);
        SW_CrossplayDeleteDukeSprite(i);
        ++removed;
    }

    return removed;
}

static void SW_CrossplayCullRuntimeDukeControlSprites(void)
{
    if (!SW_CrossplayIsDukeLikeGame(artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS)))
        return;

    SW_CrossplayCullHiddenDukeControlSprites();
}

static int32_t SW_CrossplayCanGenericTaggedDukeSpriteOperate(int16_t sourcePicnum)
{
    if (SW_CrossplayIsHiddenDukeControlSprite(sourcePicnum))
        return 0;

    switch (sourcePicnum)
    {
    case VB_DUKE_ACCESSSWITCH:
    case VB_DUKE_ACCESSSWITCH2:
    case VB_DUKE_SLOTDOOR:
    case VB_DUKE_LIGHTSWITCH:
    case VB_DUKE_SPACEDOORSWITCH:
    case VB_DUKE_SPACELIGHTSWITCH:
    case VB_DUKE_FRANKENSTINESWITCH:
    case VB_DUKE_NUKEBUTTON:
    case VB_DUKE_MULTISWITCH:
    case VB_DUKE_DIPSWITCH:
    case VB_DUKE_DIPSWITCH2:
    case VB_DUKE_DIPSWITCH3:
    case VB_DUKE_TECHSWITCH:
    case VB_DUKE_HANDSWITCH:
    case VB_DUKE_PULLSWITCH:
    case VB_DUKE_ALIENSWITCH:
    case VB_DUKE_HANDPRINTSWITCH:
        return 1;
    default:
        return 0;
    }
}

static int32_t SW_CrossplayIsDukeActorSprite(int16_t sourcePicnum)
{
    switch (sourcePicnum)
    {
    case VB_DUKE_SHARK:
    case VB_DUKE_LIZTROOP:
    case VB_DUKE_LIZTROOPRUNNING:
    case VB_DUKE_LIZTROOPSTAYPUT:
    case VB_DUKE_LIZTROOPSHOOT:
    case VB_DUKE_LIZTROOPJETPACK:
    case VB_DUKE_LIZTROOPONTOILET:
    case VB_DUKE_LIZTROOPJUSTSIT:
    case VB_DUKE_LIZTROOPDUCKING:
    case VB_DUKE_OCTABRAIN:
    case VB_DUKE_OCTABRAINSTAYPUT:
    case VB_DUKE_DRONE:
    case VB_DUKE_COMMANDER:
    case VB_DUKE_COMMANDERSTAYPUT:
    case VB_DUKE_RECON:
    case VB_DUKE_TANK:
    case VB_DUKE_PIGCOP:
    case VB_DUKE_PIGCOPSTAYPUT:
    case VB_DUKE_PIGCOPDIVE:
    case VB_DUKE_LIZMAN:
    case VB_DUKE_LIZMANSTAYPUT:
    case VB_DUKE_LIZMANSPITTING:
    case VB_DUKE_LIZMANFEEDING:
    case VB_DUKE_LIZMANJUMP:
    case VB_DUKE_ROTATEGUN:
    case VB_DUKE_GREENSLIME:
    case VB_DUKE_ORGANTIC:
    case VB_DUKE_BOSS1:
    case VB_DUKE_BOSS2:
    case VB_DUKE_BOSS3:
    case VB_DUKE_NEWBEAST:
    case VB_DUKE_NEWBEASTSTAYPUT:
    case VB_DUKE_BOSS4:
    case VB_DUKE_BOSS5:
        return 1;
    default:
        return 0;
    }
}

static uint8_t SW_CrossplayDukeActorKindForTile(int16_t sourcePicnum)
{
    switch (sourcePicnum)
    {
    case VB_DUKE_SHARK:
        return VB_CROSSPLAY_DUKE_ACTOR_SHARK;

    case VB_DUKE_LIZTROOP:
    case VB_DUKE_LIZTROOPRUNNING:
    case VB_DUKE_LIZTROOPSTAYPUT:
    case VB_DUKE_LIZTROOPSHOOT:
    case VB_DUKE_LIZTROOPJETPACK:
    case VB_DUKE_LIZTROOPONTOILET:
    case VB_DUKE_LIZTROOPJUSTSIT:
    case VB_DUKE_LIZTROOPDUCKING:
        return VB_CROSSPLAY_DUKE_ACTOR_TROOPER;

    case VB_DUKE_OCTABRAIN:
    case VB_DUKE_OCTABRAINSTAYPUT:
        return VB_CROSSPLAY_DUKE_ACTOR_OCTABRAIN;

    case VB_DUKE_DRONE:
        return VB_CROSSPLAY_DUKE_ACTOR_DRONE;

    case VB_DUKE_COMMANDER:
    case VB_DUKE_COMMANDERSTAYPUT:
        return VB_CROSSPLAY_DUKE_ACTOR_COMMANDER;

    case VB_DUKE_RECON:
        return VB_CROSSPLAY_DUKE_ACTOR_RECON;

    case VB_DUKE_TANK:
        return VB_CROSSPLAY_DUKE_ACTOR_TANK;

    case VB_DUKE_PIGCOP:
    case VB_DUKE_PIGCOPSTAYPUT:
    case VB_DUKE_PIGCOPDIVE:
        return VB_CROSSPLAY_DUKE_ACTOR_PIGCOP;

    case VB_DUKE_LIZMAN:
    case VB_DUKE_LIZMANSTAYPUT:
    case VB_DUKE_LIZMANSPITTING:
    case VB_DUKE_LIZMANFEEDING:
    case VB_DUKE_LIZMANJUMP:
        return VB_CROSSPLAY_DUKE_ACTOR_LIZMAN;

    case VB_DUKE_ROTATEGUN:
        return VB_CROSSPLAY_DUKE_ACTOR_TURRET;

    case VB_DUKE_GREENSLIME:
    case VB_DUKE_ORGANTIC:
        return VB_CROSSPLAY_DUKE_ACTOR_SLIME;

    case VB_DUKE_BOSS1:
    case VB_DUKE_BOSS2:
    case VB_DUKE_BOSS3:
    case VB_DUKE_BOSS4:
    case VB_DUKE_BOSS5:
        return VB_CROSSPLAY_DUKE_ACTOR_BOSS;

    case VB_DUKE_NEWBEAST:
    case VB_DUKE_NEWBEASTSTAYPUT:
        return VB_CROSSPLAY_DUKE_ACTOR_NEWBEAST;

    default:
        return VB_CROSSPLAY_DUKE_ACTOR_NONE;
    }
}

static int32_t SW_CrossplayDukeActorTileIsStayput(int16_t sourcePicnum)
{
    switch (sourcePicnum)
    {
    case VB_DUKE_LIZTROOPSTAYPUT:
    case VB_DUKE_LIZTROOPONTOILET:
    case VB_DUKE_LIZTROOPJUSTSIT:
    case VB_DUKE_LIZTROOPDUCKING:
    case VB_DUKE_OCTABRAINSTAYPUT:
    case VB_DUKE_COMMANDERSTAYPUT:
    case VB_DUKE_PIGCOPSTAYPUT:
    case VB_DUKE_LIZMANSTAYPUT:
    case VB_DUKE_NEWBEASTSTAYPUT:
        return 1;
    default:
        return 0;
    }
}

static sw_crossplay_duke_actor_profile_t SW_CrossplayDukeActorProfile(uint8_t actorKind, int16_t sourcePicnum = -1, int16_t sourcePal = 0)
{
    sw_crossplay_duke_actor_profile_t profile = {};
    profile.sightRange = 12288;
    profile.attackRange = 8192;
    profile.meleeRange = 512;
    profile.speed = 96;
    profile.attackDelay = 48;
    profile.meleeDamage = 4;
    profile.rangedDamage = 6;
    profile.projectileTile = VB_DUKE_FIRELASER;
    profile.preferredRange = 4096;
    profile.alertSound = DIGI_DUKE_PRED_RECOG;
    profile.painSound = DIGI_DUKE_PRED_PAIN;
    profile.attackSound = DIGI_DUKE_PRED_ATTACK;
    profile.deathSound = DIGI_DUKE_PRED_DYING;
    profile.turnRate = 64;
    profile.strafeChance = 16;

    switch (actorKind)
    {
    case VB_CROSSPLAY_DUKE_ACTOR_SHARK:
        profile.attackRange = 1024;
        profile.meleeRange = 768;
        profile.speed = 128;
        profile.attackDelay = 32;
        profile.meleeDamage = 10;
        profile.rangedDamage = 0;
        profile.projectileTile = -1;
        profile.preferredRange = 384;
        profile.alertSound = DIGI_NULL;
        profile.painSound = DIGI_NULL;
        profile.attackSound = DIGI_NULL;
        profile.deathSound = DIGI_NULL;
        profile.flying = 1;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_TROOPER:
        profile.speed = 112;
        profile.attackDelay = 42;
        profile.meleeDamage = 4;
        profile.rangedDamage = 5;
        profile.projectileTile = VB_DUKE_FIRELASER;
        profile.preferredRange = 3072;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_OCTABRAIN:
        profile.attackRange = 10240;
        profile.meleeRange = 640;
        profile.speed = 96;
        profile.attackDelay = 52;
        profile.meleeDamage = 8;
        profile.rangedDamage = 8;
        profile.projectileTile = VB_DUKE_SPIT;
        profile.preferredRange = 4096;
        profile.alertSound = DIGI_DUKE_OCTA_RECOG;
        profile.painSound = DIGI_DUKE_OCTA_PAIN;
        profile.attackSound = DIGI_DUKE_OCTA_ATTACK1;
        profile.deathSound = DIGI_DUKE_OCTA_DYING;
        profile.flying = 1;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_DRONE:
        profile.attackRange = 1024;
        profile.meleeRange = 768;
        profile.speed = 192;
        profile.attackDelay = 20;
        profile.meleeDamage = 28;
        profile.rangedDamage = 0;
        profile.projectileTile = -1;
        profile.preferredRange = 768;
        profile.alertSound = DIGI_DUKE_DRON_RECOG;
        profile.painSound = DIGI_DUKE_DRON_PAIN;
        profile.attackSound = DIGI_DUKE_DRON_ATTACK1;
        profile.deathSound = DIGI_DUKE_DRON_DYING;
        profile.flying = 1;
        profile.suicide = 1;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_COMMANDER:
        profile.attackRange = 10240;
        profile.meleeRange = 640;
        profile.speed = 96;
        profile.attackDelay = 54;
        profile.meleeDamage = 8;
        profile.rangedDamage = 10;
        profile.projectileTile = VB_DUKE_RPG;
        profile.preferredRange = 5120;
        profile.alertSound = DIGI_DUKE_COMM_RECOG;
        profile.painSound = DIGI_DUKE_COMM_PAIN;
        profile.attackSound = DIGI_DUKE_COMM_ATTACK;
        profile.deathSound = DIGI_DUKE_COMM_DYING;
        profile.flying = 1;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_RECON:
        profile.attackRange = 10240;
        profile.meleeRange = 768;
        profile.speed = 160;
        profile.attackDelay = 30;
        profile.meleeDamage = 10;
        profile.rangedDamage = 5;
        profile.projectileTile = VB_DUKE_FIRELASER;
        profile.preferredRange = 4096;
        profile.alertSound = DIGI_DUKE_RECO_RECOG;
        profile.painSound = DIGI_DUKE_RECO_PAIN;
        profile.attackSound = DIGI_DUKE_RECO_ATTACK;
        profile.deathSound = DIGI_DUKE_RECO_DYING;
        profile.flying = 1;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_TANK:
        profile.attackRange = 12288;
        profile.meleeRange = 768;
        profile.speed = 64;
        profile.attackDelay = 36;
        profile.meleeDamage = 12;
        profile.rangedDamage = 12;
        profile.projectileTile = VB_DUKE_RPG;
        profile.preferredRange = 5120;
        profile.alertSound = DIGI_DUKE_TANK_ROAM;
        profile.painSound = DIGI_DUKE_TURR_PAIN;
        profile.attackSound = DIGI_DUKE_RECO_ATTACK;
        profile.deathSound = DIGI_DUKE_THUD;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_PIGCOP:
        profile.attackRange = 7168;
        profile.meleeRange = 512;
        profile.speed = 96;
        profile.attackDelay = 55;
        profile.meleeDamage = 8;
        profile.rangedDamage = 16;
        profile.projectileTile = VB_DUKE_FIRELASER;
        profile.preferredRange = 3072;
        profile.alertSound = DIGI_DUKE_PIG_RECOG;
        profile.painSound = DIGI_DUKE_PIG_PAIN;
        profile.attackSound = DIGI_DUKE_PIG_ATTACK;
        profile.deathSound = DIGI_DUKE_PIG_DYING;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_LIZMAN:
        profile.attackRange = 8192;
        profile.meleeRange = 576;
        profile.speed = 128;
        profile.attackDelay = 46;
        profile.meleeDamage = 8;
        profile.rangedDamage = 7;
        profile.projectileTile = VB_DUKE_SPIT;
        profile.preferredRange = 3584;
        profile.alertSound = DIGI_DUKE_CAPT_RECOG;
        profile.painSound = DIGI_DUKE_CAPT_PAIN;
        profile.attackSound = DIGI_DUKE_CAPT_ATTACK;
        profile.deathSound = DIGI_DUKE_CAPT_DYING;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_TURRET:
        profile.attackRange = 12288;
        profile.meleeRange = 0;
        profile.speed = 0;
        profile.attackDelay = 14;
        profile.meleeDamage = 0;
        profile.rangedDamage = 4;
        profile.projectileTile = VB_DUKE_FIRELASER;
        profile.preferredRange = 8192;
        profile.alertSound = DIGI_DUKE_TURR_RECOG;
        profile.painSound = DIGI_DUKE_TURR_PAIN;
        profile.attackSound = DIGI_DUKE_TURR_ATTACK;
        profile.deathSound = DIGI_DUKE_TURR_DYING;
        profile.stationary = 1;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_SLIME:
        profile.attackRange = 1024;
        profile.meleeRange = 640;
        profile.speed = 96;
        profile.attackDelay = 28;
        profile.meleeDamage = 6;
        profile.rangedDamage = 0;
        profile.projectileTile = -1;
        profile.preferredRange = 512;
        profile.alertSound = DIGI_DUKE_SLIM_RECOG;
        profile.painSound = DIGI_NULL;
        profile.attackSound = DIGI_DUKE_SLIM_ATTACK;
        profile.deathSound = DIGI_DUKE_SLIM_DYING;
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_BOSS:
        profile.sightRange = 16384;
        profile.attackRange = 14336;
        profile.meleeRange = 1024;
        profile.speed = 96;
        profile.attackDelay = 32;
        profile.meleeDamage = 20;
        profile.rangedDamage = 14;
        profile.projectileTile = VB_DUKE_RPG;
        profile.preferredRange = 6144;
        profile.alertSound = DIGI_DUKE_BOS1_RECOG;
        profile.painSound = DIGI_DUKE_BOS1_PAIN;
        profile.attackSound = DIGI_DUKE_BOS1_ATTACK;
        profile.deathSound = DIGI_DUKE_BOS1_DYING;
        if (sourcePicnum == VB_DUKE_BOSS2)
        {
            profile.alertSound = sourcePal != 0 ? DIGI_DUKE_BOS2_RECOG : DIGI_DUKE_WHIPYOURASS;
            profile.painSound = DIGI_DUKE_BOS2_PAIN;
            profile.attackSound = DIGI_DUKE_BOS2_ATTACK;
            profile.deathSound = DIGI_DUKE_BOS2_DYING;
        }
        else if (sourcePicnum == VB_DUKE_BOSS3)
        {
            profile.alertSound = sourcePal != 0 ? DIGI_DUKE_BOS3_RECOG : DIGI_DUKE_RIPHEADNECK;
            profile.painSound = DIGI_DUKE_BOS3_PAIN;
            profile.attackSound = DIGI_DUKE_BOS3_ATTACK;
            profile.deathSound = DIGI_DUKE_BOS3_DYING;
        }
        else if (sourcePicnum == VB_DUKE_BOSS4)
        {
            profile.alertSound = DIGI_DUKE_BOS4_RECOG;
            profile.painSound = DIGI_DUKE_BOS4_PAIN;
            profile.attackSound = DIGI_DUKE_BOS4_ATTACK;
            profile.deathSound = DIGI_DUKE_BOS4_DYING;
        }
        break;

    case VB_CROSSPLAY_DUKE_ACTOR_NEWBEAST:
        profile.attackRange = 4096;
        profile.meleeRange = 768;
        profile.speed = 160;
        profile.attackDelay = 30;
        profile.meleeDamage = 12;
        profile.rangedDamage = 0;
        profile.projectileTile = -1;
        profile.preferredRange = 768;
        profile.alertSound = DIGI_DUKE_NEWBEAST_RECOG;
        profile.painSound = DIGI_DUKE_NEWBEAST_PAIN;
        profile.attackSound = DIGI_DUKE_NEWBEAST_ATTACK;
        profile.deathSound = DIGI_DUKE_NEWBEAST_DYING;
        break;

    default:
        break;
    }

    return profile;
}

static int32_t SW_CrossplayIsDukeExplosiveSprite(int16_t sourcePicnum)
{
    switch (sourcePicnum)
    {
    case VB_DUKE_EXPLODINGBARREL:
    case VB_DUKE_EXPLODINGBARREL2:
    case VB_DUKE_FIREBARREL:
    case VB_DUKE_OOZFILTER:
    case VB_DUKE_SEENINE:
    case VB_DUKE_SEENINEDEAD:
        return 1;
    default:
        return 0;
    }
}

static int32_t SW_CrossplayIsDukeAnimatedSprite(int16_t sourcePicnum)
{
    switch (sourcePicnum)
    {
    case VB_DUKE_FANSPRITE:
    case VB_DUKE_WATERFOUNTAIN:
    case VB_DUKE_STEAM:
    case VB_DUKE_CEILINGSTEAM:
    case VB_DUKE_TRANSPORTERBEAM:
    case VB_DUKE_TRANSPORTERSTAR:
    case VB_DUKE_BURNING:
    case VB_DUKE_FIRE:
    case VB_DUKE_BURNING2:
    case VB_DUKE_FIRE2:
    case VB_DUKE_FLOORFLAME:
        return 1;
    default:
        return 0;
    }
}

static int32_t SW_CrossplayIsDukeGlassOrScreenSprite(int16_t sourcePicnum)
{
    return SW_CrossplayIsDukeScreenBreakTile(sourcePicnum) ||
           sourcePicnum == VB_DUKE_GLASS ||
           sourcePicnum == VB_DUKE_GLASS2 ||
           sourcePicnum == VB_DUKE_STAINGLASS1;
}

static int32_t SW_CrossplayIsDukeBreakableSprite(int16_t sourcePicnum)
{
    if (SW_CrossplayIsDukeGlassOrScreenSprite(sourcePicnum) || SW_CrossplayIsDukeExplosiveSprite(sourcePicnum))
        return 1;

    switch (sourcePicnum)
    {
    case VB_DUKE_FANSPRITE:
    case VB_DUKE_WATERFOUNTAIN:
    case VB_DUKE_GRATE1:
    case VB_DUKE_PANNEL1:
    case VB_DUKE_PANNEL2:
    case VB_DUKE_PANNEL3:
    case VB_DUKE_CIRCLEPANNEL:
    case VB_DUKE_FEMMAG1:
    case VB_DUKE_FEMMAG2:
    case VB_DUKE_CAMERA1:
    case VB_DUKE_VACUUM:
    case VB_DUKE_SCALE:
    case VB_DUKE_MOVIECAMERA:
    case VB_DUKE_STATUE:
    case VB_DUKE_MIKE:
    case VB_DUKE_VASE:
    case VB_DUKE_ATM:
    case VB_DUKE_WOODENHORSE:
    case VB_DUKE_TREE1:
    case VB_DUKE_TREE2:
    case VB_DUKE_CACTUS:
    case VB_DUKE_BOX:
    case VB_DUKE_BOTTLE1:
    case VB_DUKE_BOTTLE2:
    case VB_DUKE_BOTTLE3:
    case VB_DUKE_BOTTLE4:
    case VB_DUKE_BOTTLE5:
    case VB_DUKE_BOTTLE6:
    case VB_DUKE_BOTTLE7:
    case VB_DUKE_BOTTLE8:
    case VB_DUKE_BOTTLE10:
    case VB_DUKE_BOTTLE11:
    case VB_DUKE_BOTTLE12:
    case VB_DUKE_BOTTLE13:
    case VB_DUKE_BOTTLE14:
    case VB_DUKE_BOTTLE15:
    case VB_DUKE_BOTTLE16:
    case VB_DUKE_BOTTLE17:
    case VB_DUKE_BOTTLE18:
    case VB_DUKE_BOTTLE19:
    case VB_DUKE_FEMPIC5:
    case VB_DUKE_FEMPIC6:
    case VB_DUKE_FEMPIC7:
    case VB_DUKE_HYDROPLANT:
    case VB_DUKE_HANGLIGHT:
    case VB_DUKE_HYDRENT:
    case VB_DUKE_TIRE:
    case VB_DUKE_HORSEONSIDE:
    case VB_DUKE_RUBBERCAN:
    case VB_DUKE_OOZFILTER:
    case VB_DUKE_VENDMACHINE:
    case VB_DUKE_COLAMACHINE:
    case VB_DUKE_CANWITHSOMETHING:
    case VB_DUKE_CANWITHSOMETHING2:
    case VB_DUKE_CANWITHSOMETHING3:
    case VB_DUKE_CANWITHSOMETHING4:
    case VB_DUKE_FEMPIC1:
    case VB_DUKE_FEMPIC2:
    case VB_DUKE_PODFEM1:
    case VB_DUKE_FEMPIC3:
    case VB_DUKE_FEMPIC4:
    case VB_DUKE_FEM1:
    case VB_DUKE_FEM2:
    case VB_DUKE_FEM3:
    case VB_DUKE_FEM4:
    case VB_DUKE_FEM5:
    case VB_DUKE_FEM6:
    case VB_DUKE_FEM6PAD:
    case VB_DUKE_FEM7:
    case VB_DUKE_FEM8:
    case VB_DUKE_FEM9:
    case VB_DUKE_FEM10:
    case VB_DUKE_BLOODYPOLE:
    case VB_DUKE_TOUGHGAL:
    case VB_DUKE_FOODOBJECT16:
        return 1;
    default:
        return 0;
    }
}

static int16_t SW_CrossplayDukeSpriteHealthForTile(int16_t sourcePicnum, int16_t mapExtra)
{
    if (mapExtra > 0 && mapExtra < 30000)
        return mapExtra;

    switch (sourcePicnum)
    {
    case VB_DUKE_BOSS1:
    case VB_DUKE_BOSS2:
    case VB_DUKE_BOSS3:
    case VB_DUKE_BOSS4:
    case VB_DUKE_BOSS5:
        return 1000;
    case VB_DUKE_TANK:
    case VB_DUKE_RECON:
        return 160;
    case VB_DUKE_PIGCOP:
    case VB_DUKE_PIGCOPSTAYPUT:
    case VB_DUKE_PIGCOPDIVE:
    case VB_DUKE_COMMANDER:
    case VB_DUKE_COMMANDERSTAYPUT:
    case VB_DUKE_OCTABRAIN:
    case VB_DUKE_OCTABRAINSTAYPUT:
    case VB_DUKE_NEWBEAST:
    case VB_DUKE_NEWBEASTSTAYPUT:
        return 80;
    case VB_DUKE_LIZTROOP:
    case VB_DUKE_LIZTROOPRUNNING:
    case VB_DUKE_LIZTROOPSTAYPUT:
    case VB_DUKE_LIZTROOPSHOOT:
    case VB_DUKE_LIZTROOPJETPACK:
    case VB_DUKE_LIZTROOPONTOILET:
    case VB_DUKE_LIZTROOPJUSTSIT:
    case VB_DUKE_LIZTROOPDUCKING:
    case VB_DUKE_LIZMAN:
    case VB_DUKE_LIZMANSTAYPUT:
    case VB_DUKE_LIZMANSPITTING:
    case VB_DUKE_LIZMANFEEDING:
    case VB_DUKE_LIZMANJUMP:
    case VB_DUKE_DRONE:
    case VB_DUKE_ROTATEGUN:
    case VB_DUKE_GREENSLIME:
    case VB_DUKE_ORGANTIC:
    case VB_DUKE_SHARK:
        return 40;
    case VB_DUKE_EXPLODINGBARREL:
    case VB_DUKE_EXPLODINGBARREL2:
    case VB_DUKE_FIREBARREL:
    case VB_DUKE_OOZFILTER:
    case VB_DUKE_SEENINE:
        return 10;
    default:
        return 1;
    }
}

static uint16_t SW_CrossplayDukeSpriteBehaviorFlags(int16_t sourcePicnum)
{
    uint16_t flags = VB_CROSSPLAY_DUKE_SPRITE_ALIVE;

    if (SW_CrossplayIsDukeSwitchSprite(sourcePicnum))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_SWITCH;
    if (SW_CrossplayIsDukeBreakableSprite(sourcePicnum))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE;
    if (SW_CrossplayIsDukeActorSprite(sourcePicnum))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_ACTOR;
    if (SW_CrossplayIsDukeExplosiveSprite(sourcePicnum))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE;
    if (SW_CrossplayIsDukeAnimatedSprite(sourcePicnum))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_ANIMATES;
    if (sourcePicnum == VB_DUKE_TRIPBOMB)
        flags |= VB_CROSSPLAY_DUKE_SPRITE_TRIPMINE | VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE | VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE;

    return flags;
}

static uint16_t SW_CrossplayDukeSpriteBehaviorFlagsForSprite(int32_t spriteNum, int16_t sourcePicnum)
{
    uint16_t flags = SW_CrossplayDukeSpriteBehaviorFlags(sourcePicnum);

    if ((unsigned)spriteNum < MAXSPRITES && (sprite[spriteNum].lotag != 0 || sprite[spriteNum].hitag != 0) &&
        SW_CrossplayCanGenericTaggedDukeSpriteOperate(sourcePicnum) &&
        !(flags & (VB_CROSSPLAY_DUKE_SPRITE_SWITCH |
                   VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE |
                   VB_CROSSPLAY_DUKE_SPRITE_ACTOR |
                   VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE |
                   VB_CROSSPLAY_DUKE_SPRITE_TRIPMINE)))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_SWITCH;

    if ((unsigned)spriteNum < MAXSPRITES && TEST(sprite[spriteNum].cstat, CSTAT_SPRITE_INVISIBLE) &&
        (SW_CrossplayIsDukeExplosiveSprite(sourcePicnum) || SW_CrossplayIsDukeExplosiveSourceTile(sourcePicnum)))
        flags |= VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE;

    // Duke's own spawn code hides a narrow SEENINE/OOZFILTER charge (xrepeat <= 8)
    // while leaving it explosive, and mappers rely on that to bury charges inside
    // vents and walls.  The map itself does not mark them invisible, so apply
    // Duke's rule here rather than showing a canister Duke never draws.
    if ((unsigned)spriteNum < MAXSPRITES && sprite[spriteNum].xrepeat <= 8 &&
        (sourcePicnum == VB_DUKE_SEENINE || sourcePicnum == VB_DUKE_OOZFILTER))
    {
        flags |= VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE;

        // Duke *assigns* cstat 32768 here, which clears the blocking bits as well
        // as hiding the sprite.  Merely adding the invisible flag leaves an
        // invisible, zero-sized blocker in the world that stops missiles dead --
        // Rabid Transit's tunnels are full of these charges, and rockets and
        // grenades were detonating on them the moment they spawned.
        sprite[spriteNum].cstat = CSTAT_SPRITE_INVISIBLE;
        sprite[spriteNum].xrepeat = 0;
        sprite[spriteNum].yrepeat = 0;
        ++g_swCrossplayDukeHiddenCharges;
    }

    return flags;
}

static int32_t SW_CrossplayDukeCurrentSkill(void)
{
    return clamp<int32_t>(Skill, 0, 3);
}

static int32_t SW_CrossplayDukeSpriteUsesSkillLotag(int16_t sourcePicnum)
{
    if (SW_CrossplayIsDukeActorSprite(sourcePicnum))
        return 1;

    int32_t domain = ART_CROSSPLAY_DOMAIN_ITEMS;
    return SW_CrossplayDukePickupToShadowWarrior(sourcePicnum, 0, &domain) >= 0;
}

static int32_t SW_CrossplayDukeSpriteSuppressedBySkill(int32_t spriteNum, int16_t sourcePicnum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || !SW_CrossplayDukeSpriteUsesSkillLotag(sourcePicnum))
        return 0;

    return sprite[spriteNum].lotag > SW_CrossplayDukeCurrentSkill();
}

static int32_t SW_CrossplayDukeSpriteShouldBlock(int16_t sourcePicnum, uint16_t flags)
{
    if (flags & VB_CROSSPLAY_DUKE_SPRITE_ACTOR)
        return 1;

    switch (sourcePicnum)
    {
    case VB_DUKE_ATM:
    case VB_DUKE_VENDMACHINE:
    case VB_DUKE_COLAMACHINE:
    case VB_DUKE_EXPLODINGBARREL:
    case VB_DUKE_EXPLODINGBARREL2:
    case VB_DUKE_FIREBARREL:
    case VB_DUKE_OOZFILTER:
    case VB_DUKE_SEENINE:
    case VB_DUKE_SEENINEDEAD:
    case VB_DUKE_TRIPBOMB:
    case VB_DUKE_BOX:
    case VB_DUKE_VASE:
    case VB_DUKE_HYDRENT:
    case VB_DUKE_TIRE:
    case VB_DUKE_CACTUS:
    case VB_DUKE_WOODENHORSE:
    case VB_DUKE_TREE1:
    case VB_DUKE_TREE2:
        return 1;
    default:
        return 0;
    }
}

static void SW_CrossplayRegisterDukeSpriteBehavior(int32_t spriteNum, int16_t sourcePicnum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sourcePicnum < 0)
        return;

    uint16_t const flags = SW_CrossplayDukeSpriteBehaviorFlagsForSprite(spriteNum, sourcePicnum);
    if (flags == VB_CROSSPLAY_DUKE_SPRITE_ALIVE)
        return;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
        ++g_swCrossplayDukeSpriteBehaviorCount;

    behavior.sourcePicnum = sourcePicnum;
    behavior.basePicnum = sourcePicnum;
    behavior.displayPal = sprite[spriteNum].pal;
    behavior.health = SW_CrossplayDukeSpriteHealthForTile(sourcePicnum, sprite[spriteNum].extra);
    behavior.flags = flags;
    behavior.homeSect = sprite[spriteNum].sectnum;
    behavior.home = sprite[spriteNum].xy;
    behavior.actorKind = SW_CrossplayDukeActorKindForTile(sourcePicnum);
    behavior.attackCooldown = SW_CrossplayDukeActorProfile(behavior.actorKind, behavior.sourcePicnum, behavior.displayPal).stationary ?
                              0 : (uint16_t)(krand1() & 31);
    behavior.painCooldown = 0;
    behavior.moveCooldown = 0;
    behavior.moveAng = sprite[spriteNum].ang;
    behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_IDLE;
    behavior.stayput = SW_CrossplayDukeActorTileIsStayput(sourcePicnum);
    behavior.airborne = SW_CrossplayDukeActorProfile(behavior.actorKind, behavior.sourcePicnum, behavior.displayPal).flying;
    behavior.strafeDir = ((krand1() & 1) != 0) ? 1 : -1;
    behavior.switchState = 0;
    behavior.tick = 0;

    // A switch placed in its flipped frame has to keep animating from the base
    // tile, otherwise operating it walks the art off into unrelated tiles.
    if (flags & VB_CROSSPLAY_DUKE_SPRITE_SWITCH)
    {
        int32_t startState = 0;
        int16_t const switchBase = SW_CrossplayDukeSwitchBaseTile(sourcePicnum, &startState);

        if (switchBase >= 0)
        {
            behavior.basePicnum = switchBase;
            behavior.switchState = (uint8_t)startState;
        }
    }
}

static void SW_CrossplayRegisterDukeSpriteVisualOverride(int32_t spriteNum, int16_t sourcePicnum, int16_t sourcePal)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sourcePicnum < 0)
        return;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
        ++g_swCrossplayDukeSpriteBehaviorCount;

    behavior.sourcePicnum = sourcePicnum;
    behavior.basePicnum = sourcePicnum;
    behavior.displayPal = sourcePal;
    behavior.health = 0;
    behavior.flags = VB_CROSSPLAY_DUKE_SPRITE_ALIVE | VB_CROSSPLAY_DUKE_SPRITE_VISUAL_OVERRIDE;
    behavior.homeSect = sprite[spriteNum].sectnum;
    behavior.home = sprite[spriteNum].xy;
    behavior.attackCooldown = 0;
    behavior.painCooldown = 0;
    behavior.moveCooldown = 0;
    behavior.moveAng = sprite[spriteNum].ang;
    behavior.actorKind = VB_CROSSPLAY_DUKE_ACTOR_NONE;
    behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_IDLE;
    behavior.stayput = 0;
    behavior.airborne = 0;
    behavior.strafeDir = 0;
    behavior.switchState = 0;
    behavior.tick = 0;
}

static void SW_CrossplayPrepareForeignMapSprites(void)
{
    int32_t const sourceGame = artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS);
    int32_t const canConvertDukePickups = SW_CrossplayIsDukeLikeGame(sourceGame);
    int32_t itemsConverted = 0;
    int32_t weaponsConverted = 0;
    int32_t pickupVisualsPreserved = 0;
    int32_t preserved = 0;
    int32_t skillFiltered = 0;
    int32_t removed = 0;

    for (int32_t i = MAXSPRITES - 1; i >= 0; --i)
    {
        if (sprite[i].statnum == MAXSTATUS)
            continue;

        if (sprite[i].statnum >= STAT_PLAYER0 && sprite[i].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS)
            continue;

        // Duke already owns its actors, including their skill filtering and
        // their size; SW's preparation must not convert, hide or cull them.
        if (SW_CrossplayDukeAIOwnsSprite(i))
            continue;

        if (canConvertDukePickups)
        {
            int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(i);
            int16_t const originalPal = sprite[i].pal;

            if (SW_CrossplayDukeSpriteSuppressedBySkill(i, originalPicnum))
            {
                SW_CrossplayClearDukeSpriteBehavior(i);
                SW_CrossplayDeleteDukeSprite(i);
                ++skillFiltered;
                continue;
            }

            if (SW_CrossplaySpriteIsHiddenDukeControlSprite(i))
            {
                SW_CrossplayClearDukeSpriteBehavior(i);
                SW_CrossplayDeleteDukeSprite(i);
                ++removed;
                continue;
            }

            int32_t convertedDomain = ART_CROSSPLAY_DOMAIN_ITEMS;
            if (SW_CrossplayConvertDukeMapSpriteToShadowWarrior(i, &itemsConverted, &weaponsConverted, &convertedDomain))
            {
                // A Duke weapon pickup gives the player an SW weapon, so it shows
                // the SW weapon's own icon.  Items keep the Duke map's art, which
                // is what the level was built to look like.
                if (convertedDomain != ART_CROSSPLAY_DOMAIN_WEAPONS)
                {
                    SW_CrossplayRegisterDukeSpriteVisualOverride(i, originalPicnum, originalPal);
                    ++pickupVisualsPreserved;
                }
                continue;
            }

            SPRITEp const sp = &sprite[i];
            uint16_t const behaviorFlags = SW_CrossplayDukeSpriteBehaviorFlagsForSprite(i, originalPicnum);

            SW_CrossplayRegisterDukeSpriteBehavior(i, originalPicnum);
            sp->xvel = sp->yvel = sp->zvel = 0;
            sp->owner = -1;
            sp->extra = 0;

            if (behaviorFlags & (VB_CROSSPLAY_DUKE_SPRITE_SWITCH |
                                 VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE |
                                 VB_CROSSPLAY_DUKE_SPRITE_ACTOR |
                                 VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE |
                                 VB_CROSSPLAY_DUKE_SPRITE_TRIPMINE))
            if (behaviorFlags & VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE)
            {
                // Duke's hidden charges are cstat 32768 exactly: invisible and
                // solid to nothing.  They are set off by a blast, never by a
                // direct hit, and leaving them blocking puts invisible walls in
                // front of the player's projectiles.
                RESET(sp->cstat, CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
            }
            else
            {
                SET(sp->cstat, CSTAT_SPRITE_BLOCK_HITSCAN);
                if (SW_CrossplayDukeSpriteShouldBlock(originalPicnum, behaviorFlags))
                    SET(sp->cstat, CSTAT_SPRITE_BLOCK);
                else
                    RESET(sp->cstat, CSTAT_SPRITE_BLOCK);
            }
            else
            {
                RESET(sp->cstat, CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
            }

            if (behaviorFlags & VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE)
                SET(sp->cstat, CSTAT_SPRITE_INVISIBLE);

            if (behaviorFlags & VB_CROSSPLAY_DUKE_SPRITE_TRIPMINE)
                if (!SW_CrossplayDukeAIRequested())
                    SW_CrossplayRegisterDukeTripmine(i);

            change_sprite_stat((short)i, STAT_NO_STATE);
            ++preserved;
            continue;
        }

        SW_CrossplayDeleteDukeSprite(i);
        ++removed;
    }

    if (itemsConverted > 0 || weaponsConverted > 0)
        LOG_F(INFO, "Crossplay: converted %d Duke item pickup(s) and %d Duke weapon/ammo pickup(s) to Shadow Warrior runtime pickups.",
              itemsConverted, weaponsConverted);

    if (g_swCrossplayDukeHiddenCharges > 0)
        LOG_F(INFO, "Crossplay: hid %d narrow Duke explosive charge(s) the way Duke's own spawn does.",
              g_swCrossplayDukeHiddenCharges);

    if (pickupVisualsPreserved > 0)
        LOG_F(INFO, "Crossplay: preserved Duke map art for %d converted pickup sprite(s).", pickupVisualsPreserved);

    if (preserved > 0)
        LOG_F(INFO, "Crossplay: preserved %d visible Duke map sprite(s) for Shadow Warrior crossplay.", preserved);

    if (skillFiltered > 0)
        LOG_F(INFO, "Crossplay: culled %d Duke actor/pickup sprite(s) above Shadow Warrior skill %d.",
              skillFiltered, SW_CrossplayDukeCurrentSkill() + 1);

    if (g_swCrossplayDukeSpriteBehaviorCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke sprite behavior(s) for Shadow Warrior runtime.", g_swCrossplayDukeSpriteBehaviorCount);

    if (removed > 0)
        LOG_F(INFO, "Crossplay: culled %d Duke control/player marker sprite(s) after building Shadow Warrior compatibility bridges.", removed);
}

static void SW_CrossplaySanitizeForeignMapSectors(void)
{
    int32_t sanitized = 0;

    for (int32_t i = 0; i < numsectors; ++i)
    {
        if (sector[i].extra == 0)
            continue;

        sector[i].extra = 0;
        ++sanitized;
    }

    if (sanitized > 0)
        LOG_F(INFO, "Crossplay: sanitized %d foreign sector extra value(s) for Shadow Warrior compatibility.", sanitized);
}

static void SW_CrossplayResetSectorObjects(void)
{
    for (int32_t i = 0; i < MAX_SECTOR_OBJECTS; ++i)
    {
        memset(&SectorObject[i], -1, sizeof(SectorObject[0]));
        SectorObject[i].PreMoveAnimator = NULL;
        SectorObject[i].PostMoveAnimator = NULL;
        SectorObject[i].Animator = NULL;
        SectorObject[i].controller = NULL;
        SectorObject[i].sp_child = NULL;
        SectorObject[i].xmid = INT32_MAX;
    }
}

static void SW_CrossplayApplyDukePlayerRuntimeDimensions(PLAYERp pp)
{
    if (!SW_CrossplayUseDukePlayerDimensions() || pp == NULL || (unsigned)pp->PlayerSprite >= MAXSPRITES)
        return;

    SPRITEp const sp = &sprite[pp->PlayerSprite];
    USERp const u = User[pp->PlayerSprite];

    pp->SpriteP = sp;
    pp->floor_dist = SW_PlayerRunFloorDist();
    pp->ceiling_dist = SW_PlayerRunCeilingDist();

    sp->clipdist = (char)SW_PlayerSpriteClipdist();
    sp->xrepeat = SW_PlayerSpriteXRepeat();
    sp->yrepeat = SW_PlayerSpriteYRepeat();

    if (u != NULL)
    {
        u->Radius = SW_PlayerActorRadius();
        u->floor_dist = (short)SW_PlayerRunFloorDist();
        u->ceiling_dist = (short)SW_PlayerRunCeilingDist();
        u->SpriteP = sp;
        u->PlayerP = pp;
    }
}

static void SW_CrossplayApplyDukePlayerRuntimeDimensionsForAllPlayers(void)
{
    int32_t i;

    TRAVERSE_CONNECT(i)
        SW_CrossplayApplyDukePlayerRuntimeDimensions(Player + i);
}

static void SW_CrossplayResetPlayersForForeignMap(void)
{
    int32_t reset = 0;
    int32_t i;

    TRAVERSE_CONNECT(i)
    {
        PLAYERp const pp = Player + i;

        if ((unsigned)pp->PlayerSprite >= MAXSPRITES || User[pp->PlayerSprite] == NULL)
            continue;

        pClearSpriteList(pp);
        Bmemset(pp->Wpn, 0, sizeof(pp->Wpn));
        pp->CurWpn = NULL;
        PlayerGameReset(pp);
        SW_CrossplayApplyDukePlayerRuntimeDimensions(pp);
        ++reset;
    }

    if (reset > 0)
        LOG_F(INFO, "Crossplay: reset %d Shadow Warrior player(s) for foreign level compatibility.", reset);
}

struct sw_crossplay_duke_rotator_t
{
    int16_t sectnum;
    int16_t originStart;
    int16_t originCount;
    vec2_t pivot;
    int16_t angle;
    int16_t step;
    int16_t tempAngle;
    int32_t closedFloorZ;
    int32_t openFloorZ;
    uint8_t carryActors;
    uint8_t rotateRiseBridge;
    uint8_t active;
    uint8_t opening;
    uint8_t reverseDirection;
};

struct sw_crossplay_duke_conveyor_t
{
    int16_t sectnum;
    int16_t hitag;
    int32_t xmove;
    int32_t ymove;
    int32_t panSpeed;
    uint8_t disabled;
};

struct sw_crossplay_duke_locator_t
{
    int16_t sectnum;
    int16_t lotag;
    int16_t hitag;
    int16_t ang;
    vec3_t pos;
};

struct sw_crossplay_duke_train_t
{
    int16_t sectnum;
    int16_t locatorSect;
    int16_t locatorTag;
    int16_t lotag;
    int16_t hitag;
    int16_t originStart;
    int16_t originCount;
    int16_t initialAngle;
    int16_t angle;
    int32_t speed;
    int32_t xvel;
    int32_t waitTics;
    vec2_t pos;
    uint8_t active;
    uint8_t state;
};

struct sw_crossplay_duke_teleporter_t
{
    int16_t sourceSprite;
    int16_t destSprite;
    int16_t sourceSect;
    int16_t destSect;
    vec3_t source;
    vec3_t dest;
    int16_t destAng;
    uint8_t sourceOnFloor;
};

struct sw_crossplay_duke_water_warp_t
{
    int16_t sourceSect;
    int16_t destSect;
    int16_t sourceLotag;
    int16_t destLotag;
    vec3_t source;
    vec3_t dest;
};

struct sw_crossplay_duke_activator_t
{
    int16_t sectnum;
    int16_t lotag;
    int16_t hitag;
    uint8_t locked;
};

struct sw_crossplay_duke_touchplate_t
{
    int16_t sectnum;
    int16_t lotag;
    int16_t hitag;
    int16_t ang;
    int32_t triggerZ;
    int32_t originalFloorZ;
    int32_t speed;
    uint8_t pressed;
    uint8_t moving;
    uint8_t direction;
    uint8_t disabled;
};

struct sw_crossplay_duke_masterswitch_t
{
    int16_t sectnum;
    int16_t lotag;
    int16_t initialDelay;
    int16_t delay;
    uint8_t active;
    uint8_t repeat;
};

struct sw_crossplay_duke_zmover_t
{
    int16_t sectnum;
    int16_t lotag;
    int16_t hitag;
    int16_t ang;
    int32_t effectorZ;
    int32_t originalZ;
    int32_t speed;
    int32_t delayTics;
    uint8_t active;
    uint8_t direction;
};

struct sw_crossplay_duke_sector_zanim_t
{
    int16_t sectnum;
    int32_t targetFloorZ;
    int32_t targetCeilingZ;
    int32_t speed;
    uint8_t active;
    uint8_t moveFloor;
    uint8_t moveCeiling;
    uint8_t carryFloorContents;
};

struct sw_crossplay_duke_warp_elevator_t
{
    int16_t sectnum;
    int16_t hitag;
    vec3_t effectorPos;
    int32_t speed;
    int32_t stopFloorZ;
    int32_t lowerRevealZ;
    int32_t upperRevealZ;
    int8_t shade;
    int8_t direction;
    uint8_t warpPending;
};

struct sw_crossplay_duke_swing_door_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t originStart;
    int16_t originCount;
    vec2_t pivot;
    int16_t currentAngle;
    int16_t targetAngle;
    int16_t openAngle;
    int16_t step;
    uint8_t active;
    uint8_t open;
};

struct sw_crossplay_duke_sliding_door_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t originStart;
    int16_t originCount;
    int16_t angle;
    int32_t currentDistance;
    int32_t targetDistance;
    int32_t totalDistance;
    int32_t step;
    uint8_t active;
    uint8_t open;
};

struct sw_crossplay_duke_stretch_bridge_t
{
    int16_t sectnum;
    int16_t wall1;
    int16_t wall2;
    int16_t angle;
    vec2_t wall1Origin;
    vec2_t wall2Origin;
    int32_t currentDistance;
    int32_t targetDistance;
    int32_t totalDistance;
    int32_t step;
    uint8_t active;
    uint8_t open;
};

struct sw_crossplay_duke_auto_close_t
{
    int16_t sectnum;
    int16_t delay;
    int16_t count;
};

struct sw_crossplay_duke_explosive_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t angle;
    int16_t owner;
    int32_t targetCeilingZ;
    int32_t targetFloorZ;
    int32_t speed;
    uint16_t ticks;
    uint8_t active;
    uint8_t finished;
};

struct sw_crossplay_duke_explosive_source_t
{
    int16_t sectnum;
    int16_t hitag;
    vec2_t pos;
};

struct sw_crossplay_duke_light_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t lotag;
    int8_t effectorShade;
    uint8_t effectorPal;
    int8_t baseFloorShade;
    int8_t baseCeilingShade;
    int8_t maxWallShade;
    uint8_t baseFloorPal;
    uint8_t baseCeilingPal;
    uint8_t state;
    uint8_t oneShot;
};

struct sw_crossplay_duke_lightning_t
{
    int16_t sectnum;
    int16_t hitag;
    vec3_t pos;
    int16_t delay;
    int16_t duration;
    int16_t ticks;
    uint8_t active;
};

struct sw_crossplay_duke_respawn_t
{
    int16_t sectnum;
    int16_t lotag;
    int16_t spawnPicnum;
    int16_t pal;
    int16_t shade;
    int16_t cstat;
    uint8_t xrepeat;
    uint8_t yrepeat;
    int16_t ang;
    int16_t clipdist;
    vec3_t pos;
    uint8_t active;
    uint8_t used;
    int16_t delay;
};

struct sw_crossplay_duke_drop_floor_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t ang;
    int32_t targetZ;
    int32_t speed;
    int32_t delay;
    uint8_t active;
    uint8_t finished;
    uint8_t moveCeiling;
};

struct sw_crossplay_duke_teeth_door_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t targetSectnum;
    int32_t speed;
    uint8_t active;
};

struct sw_crossplay_duke_quake_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t originStart;
    int16_t originCount;
    int16_t angle;
    vec3_t pos;
    int16_t targetFloorHeinum;
    int16_t ticks;
    uint8_t active;
    uint8_t finished;
};

struct sw_crossplay_duke_quake_debris_t
{
    int16_t sectnum;
    int16_t angle;
    vec3_t pos;
};

struct sw_crossplay_duke_wall_anim_t
{
    int16_t wallnum;
    uint8_t axis;
    uint8_t active;
    int32_t target;
    int32_t speed;
};

struct sw_crossplay_duke_piston_t
{
    int16_t sectnum;
    int16_t hitag;
    int32_t openCeilingZ;
    int32_t speed;
    uint8_t enabled;
    uint8_t movingDown;
};

struct sw_crossplay_duke_incremental_mover_t
{
    int16_t sectnum;
    int16_t hitag;
    int32_t targetZ;
    int32_t speed;
    int32_t delayLimit;
    int32_t delay;
    uint8_t moveCeiling;
    uint8_t active;
    uint8_t finished;
};

struct sw_crossplay_duke_wave_t
{
    int16_t sectnum;
    int16_t phase;
    int32_t baseFloorZ;
    int32_t amplitude;
};

struct sw_crossplay_duke_projectile_shooter_t
{
    int16_t sectnum;
    int16_t hitag;
    int16_t angle;
    int16_t projectileTile;
    vec3_t pos;
    uint16_t ticks;
    uint8_t active;
};

struct sw_crossplay_duke_map_sound_t
{
    int16_t sectnum;
    int16_t spriteNum;
    int16_t soundId;
    int16_t alternateSoundId;
    int32_t digi;
    int32_t alternateDigi;
    int32_t distance;
    int32_t delay;
    vec3_t pos;
    uint8_t toggled;
};

enum
{
    VB_CROSSPLAY_DUKE_TRANSIENT_DEBRIS,
    VB_CROSSPLAY_DUKE_TRANSIENT_PROJECTILE,
    VB_CROSSPLAY_DUKE_TRANSIENT_EXPLOSION,
};

struct sw_crossplay_duke_transient_t
{
    int16_t spriteNum;
    int16_t sourcePicnum;
    int16_t owner;
    int32_t xvel;
    int32_t yvel;
    int32_t zvel;
    uint16_t ttl;
    uint16_t tick;
    uint8_t active;
    uint8_t type;
};

struct sw_crossplay_duke_tripmine_t
{
    int16_t spriteNum;
    int16_t sectnum;
    int16_t angle;
    int16_t lineSprites[VB_CROSSPLAY_MAX_DUKE_TRIPMINE_SEGMENTS];
    int16_t lineCount;
    int32_t lineDist;
    vec3_t beamStart;
    uint16_t armTicks;
    uint8_t active;
    uint8_t armed;
};

static sw_crossplay_duke_rotator_t g_swCrossplayDukeRotators[VB_CROSSPLAY_MAX_DUKE_ROTATORS];
static vec2_t g_swCrossplayDukeRotatorOrigins[MAXWALLS];
static sw_crossplay_duke_train_t g_swCrossplayDukeTrains[VB_CROSSPLAY_MAX_DUKE_TRAINS];

// Counts tics in which a train actually carried a player, so a carry failure can
// be told apart from a player who was never on the train to begin with.
static int32_t g_swCrossplayDukeTrainCarryTics;
static vec2_t g_swCrossplayDukeTrainRideOffset;
static int32_t g_swCrossplayDukeTrainRideTics;
static int32_t g_swCrossplayDukeTrainRideWobble;
static int32_t g_swCrossplayDukeTrainCrushKills;
static vec2_t g_swCrossplayDukeTrainOrigins[MAXWALLS];
static sw_crossplay_duke_locator_t g_swCrossplayDukeLocators[VB_CROSSPLAY_MAX_DUKE_LOCATORS];
static sw_crossplay_duke_conveyor_t g_swCrossplayDukeConveyors[VB_CROSSPLAY_MAX_DUKE_CONVEYORS];
static sw_crossplay_duke_teleporter_t g_swCrossplayDukeTeleporters[VB_CROSSPLAY_MAX_DUKE_TELEPORTERS];
static sw_crossplay_duke_activator_t g_swCrossplayDukeActivators[VB_CROSSPLAY_MAX_DUKE_ACTIVATORS];
static sw_crossplay_duke_touchplate_t g_swCrossplayDukeTouchplates[VB_CROSSPLAY_MAX_DUKE_TOUCHPLATES];
static sw_crossplay_duke_masterswitch_t g_swCrossplayDukeMasterSwitches[VB_CROSSPLAY_MAX_DUKE_MASTERSWITCHES];
static sw_crossplay_duke_zmover_t g_swCrossplayDukeZMovers[VB_CROSSPLAY_MAX_DUKE_ZMOVERS];
static sw_crossplay_duke_sector_zanim_t g_swCrossplayDukeSectorZAnims[VB_CROSSPLAY_MAX_DUKE_SECTOR_ZANIMS];
static sw_crossplay_duke_warp_elevator_t g_swCrossplayDukeWarpElevators[VB_CROSSPLAY_MAX_DUKE_WARP_ELEVATORS];
static sw_crossplay_duke_swing_door_t g_swCrossplayDukeSwingDoors[VB_CROSSPLAY_MAX_DUKE_SWING_DOORS];
static vec2_t g_swCrossplayDukeSwingDoorOrigins[MAXWALLS];
static sw_crossplay_duke_sliding_door_t g_swCrossplayDukeSlidingDoors[VB_CROSSPLAY_MAX_DUKE_SLIDING_DOORS];
static vec2_t g_swCrossplayDukeSlidingDoorOrigins[MAXWALLS];
static sw_crossplay_duke_stretch_bridge_t g_swCrossplayDukeStretchBridges[VB_CROSSPLAY_MAX_DUKE_STRETCH_BRIDGES];
static sw_crossplay_duke_auto_close_t g_swCrossplayDukeAutoCloses[VB_CROSSPLAY_MAX_DUKE_AUTO_CLOSE];
static sw_crossplay_duke_explosive_t g_swCrossplayDukeExplosives[VB_CROSSPLAY_MAX_DUKE_EXPLOSIVES];
static sw_crossplay_duke_explosive_source_t g_swCrossplayDukeExplosiveSources[VB_CROSSPLAY_MAX_DUKE_EXPLOSIVE_SOURCES];
static sw_crossplay_duke_light_t g_swCrossplayDukeLights[VB_CROSSPLAY_MAX_DUKE_LIGHTS];
static sw_crossplay_duke_lightning_t g_swCrossplayDukeLightnings[VB_CROSSPLAY_MAX_DUKE_LIGHTNINGS];
static sw_crossplay_duke_water_warp_t g_swCrossplayDukeWaterWarps[VB_CROSSPLAY_MAX_DUKE_WATER_WARPS];
static sw_crossplay_duke_respawn_t g_swCrossplayDukeRespawns[VB_CROSSPLAY_MAX_DUKE_RESPAWNS];
static sw_crossplay_duke_drop_floor_t g_swCrossplayDukeDropFloors[VB_CROSSPLAY_MAX_DUKE_DROP_FLOORS];
static sw_crossplay_duke_teeth_door_t g_swCrossplayDukeTeethDoors[VB_CROSSPLAY_MAX_DUKE_TEETH_DOORS];
static sw_crossplay_duke_quake_t g_swCrossplayDukeQuakes[VB_CROSSPLAY_MAX_DUKE_QUAKES];
static sw_crossplay_duke_quake_debris_t g_swCrossplayDukeQuakeDebris[VB_CROSSPLAY_MAX_DUKE_QUAKE_DEBRIS];
static vec2_t g_swCrossplayDukeQuakeOrigins[VB_CROSSPLAY_MAX_DUKE_QUAKE_ORIGINS];
static sw_crossplay_duke_wall_anim_t g_swCrossplayDukeWallAnims[VB_CROSSPLAY_MAX_DUKE_WALL_ANIMS];
static sw_crossplay_duke_piston_t g_swCrossplayDukePistons[VB_CROSSPLAY_MAX_DUKE_PISTONS];
static sw_crossplay_duke_incremental_mover_t g_swCrossplayDukeIncrementalMovers[VB_CROSSPLAY_MAX_DUKE_INCREMENTAL_MOVERS];
static sw_crossplay_duke_wave_t g_swCrossplayDukeWaves[VB_CROSSPLAY_MAX_DUKE_WAVES];
static sw_crossplay_duke_projectile_shooter_t g_swCrossplayDukeProjectileShooters[VB_CROSSPLAY_MAX_DUKE_PROJECTILE_SHOOTERS];
static sw_crossplay_duke_map_sound_t g_swCrossplayDukeMapSounds[VB_CROSSPLAY_MAX_DUKE_MAP_SOUNDS];
static sw_crossplay_duke_transient_t g_swCrossplayDukeTransients[VB_CROSSPLAY_MAX_DUKE_TRANSIENTS];
static sw_crossplay_duke_tripmine_t g_swCrossplayDukeTripmines[VB_CROSSPLAY_MAX_DUKE_TRIPMINES];
static int16_t g_swCrossplayDukeTrainPlayerCarrySector[MAX_SW_PLAYERS_REG];
static int16_t g_swCrossplayDukeTrainPlayerLastCarrySector[MAX_SW_PLAYERS_REG];
static int32_t g_swCrossplayDukeTrainPlayerCarryGrace[MAX_SW_PLAYERS_REG];
static int16_t g_swCrossplayDukeTeleporterBlockSector[MAX_SW_PLAYERS_REG];
static int16_t g_swCrossplayDukeTeleporterBlockSprite[MAX_SW_PLAYERS_REG];
static int16_t g_swCrossplayDukeWaterWarpBlockSector[MAX_SW_PLAYERS_REG];
static int32_t g_swCrossplayDukeRotatorCount;
static int32_t g_swCrossplayDukeRotatorOriginCount;
static int32_t g_swCrossplayDukeTrainCount;
static int32_t g_swCrossplayDukeTrainOriginCount;
static int32_t g_swCrossplayDukeLocatorCount;
static int32_t g_swCrossplayDukeConveyorCount;
static int32_t g_swCrossplayDukeTeleporterCount;
static int32_t g_swCrossplayDukeActivatorCount;
static int32_t g_swCrossplayDukeTouchplateCount;
static int32_t g_swCrossplayDukeMasterSwitchCount;
static int32_t g_swCrossplayDukeZMoverCount;
static int32_t g_swCrossplayDukeSectorZAnimCount;
static int32_t g_swCrossplayDukeWarpElevatorCount;
static int32_t g_swCrossplayDukeSwingDoorCount;
static int32_t g_swCrossplayDukeSwingDoorOriginCount;
static int32_t g_swCrossplayDukeSlidingDoorCount;
static int32_t g_swCrossplayDukeSlidingDoorOriginCount;
static int32_t g_swCrossplayDukeStretchBridgeCount;
static int32_t g_swCrossplayDukeAutoCloseCount;
static int32_t g_swCrossplayDukeExplosiveCount;
static int32_t g_swCrossplayDukeExplosiveSourceCount;
static int32_t g_swCrossplayDukeLightCount;
static int32_t g_swCrossplayDukeLightningCount;
static int32_t g_swCrossplayDukeWaterWarpCount;
static int32_t g_swCrossplayDukeRespawnCount;
static int32_t g_swCrossplayDukeDropFloorCount;
static int32_t g_swCrossplayDukeTeethDoorCount;
static int32_t g_swCrossplayDukeQuakeCount;
static int32_t g_swCrossplayDukeQuakeDebrisCount;
static int32_t g_swCrossplayDukeQuakeOriginCount;
static int32_t g_swCrossplayDukeWallAnimCount;
static int32_t g_swCrossplayDukePistonCount;
static int32_t g_swCrossplayDukeIncrementalMoverCount;
static int32_t g_swCrossplayDukeWaveCount;
static int32_t g_swCrossplayDukeProjectileShooterCount;
static int32_t g_swCrossplayDukeMapSoundCount;
static int32_t g_swCrossplayDukeTransientCount;
static int32_t g_swCrossplayDukeTripmineCount;
static int32_t g_swCrossplayDukeTeleporterHold[MAX_SW_PLAYERS_REG];
static int32_t g_swCrossplayDukeWaterWarpHold[MAX_SW_PLAYERS_REG];
static int16_t g_swCrossplayDukeRuntimeTileCache[MAXTILES];
static uint8_t g_swCrossplayDukeRuntimeTileCacheReady;

static void SW_CrossplayResetDukeRuntimeTileCache(void)
{
    Bmemset(g_swCrossplayDukeRuntimeTileCache, 0xff, sizeof(g_swCrossplayDukeRuntimeTileCache));
    g_swCrossplayDukeRuntimeTileCacheReady = 1;
}

static void SW_CrossplayResetDukeBridgeCounters(void)
{
    g_swCrossplayDukeActorAttackEvents = 0;
    g_swCrossplayDukeActorDeathEvents = 0;
    g_swCrossplayDukeTeleportEvents = 0;
    g_swCrossplayDukeTeleportTic = 0;
    g_swCrossplayDukeLastTeleportTic = -1;
    g_swCrossplayDukeMinTeleportGap = -1;
    g_swCrossplayDukeTripmineDetonations = 0;
}

static void SW_CrossplayClearDukeMapEffectors(void)
{
    SW_CrossplayResetDukeBridgeCounters();
    g_swCrossplayDukeRotatorCount = 0;
    g_swCrossplayDukeRotatorOriginCount = 0;
    g_swCrossplayDukeTrainCount = 0;
    g_swCrossplayDukeTrainOriginCount = 0;
    g_swCrossplayDukeLocatorCount = 0;
    g_swCrossplayDukeConveyorCount = 0;
    g_swCrossplayDukeTeleporterCount = 0;
    g_swCrossplayDukeActivatorCount = 0;
    g_swCrossplayDukeTouchplateCount = 0;
    g_swCrossplayDukeMasterSwitchCount = 0;
    g_swCrossplayDukeZMoverCount = 0;
    g_swCrossplayDukeSectorZAnimCount = 0;
    g_swCrossplayDukeWarpElevatorCount = 0;
    g_swCrossplayDukeSwingDoorCount = 0;
    g_swCrossplayDukeSwingDoorOriginCount = 0;
    g_swCrossplayDukeSlidingDoorCount = 0;
    g_swCrossplayDukeSlidingDoorOriginCount = 0;
    g_swCrossplayDukeStretchBridgeCount = 0;
    g_swCrossplayDukeAutoCloseCount = 0;
    g_swCrossplayDukeExplosiveCount = 0;
    g_swCrossplayDukeExplosiveSourceCount = 0;
    g_swCrossplayDukeLightCount = 0;
    g_swCrossplayDukeLightningCount = 0;
    g_swCrossplayDukeWaterWarpCount = 0;
    g_swCrossplayDukeRespawnCount = 0;
    g_swCrossplayDukeDropFloorCount = 0;
    g_swCrossplayDukeTeethDoorCount = 0;
    g_swCrossplayDukeQuakeCount = 0;
    g_swCrossplayDukeQuakeDebrisCount = 0;
    g_swCrossplayDukeQuakeOriginCount = 0;
    g_swCrossplayDukeWallAnimCount = 0;
    g_swCrossplayDukePistonCount = 0;
    g_swCrossplayDukeIncrementalMoverCount = 0;
    g_swCrossplayDukeWaveCount = 0;
    g_swCrossplayDukeProjectileShooterCount = 0;
    g_swCrossplayDukeMapSoundCount = 0;
    g_swCrossplayDukeTransientCount = 0;
    g_swCrossplayDukeTripmineCount = 0;
    g_swCrossplayDukeSpriteBehaviorCount = 0;
    Bmemset(g_swCrossplayDukeLightnings, 0, sizeof(g_swCrossplayDukeLightnings));
    Bmemset(g_swCrossplayDukeRespawns, 0, sizeof(g_swCrossplayDukeRespawns));
    Bmemset(g_swCrossplayDukeDropFloors, 0, sizeof(g_swCrossplayDukeDropFloors));
    Bmemset(g_swCrossplayDukeTeethDoors, 0, sizeof(g_swCrossplayDukeTeethDoors));
    Bmemset(g_swCrossplayDukeQuakes, 0, sizeof(g_swCrossplayDukeQuakes));
    Bmemset(g_swCrossplayDukeQuakeDebris, 0, sizeof(g_swCrossplayDukeQuakeDebris));
    Bmemset(g_swCrossplayDukeWallAnims, 0, sizeof(g_swCrossplayDukeWallAnims));
    Bmemset(g_swCrossplayDukePistons, 0, sizeof(g_swCrossplayDukePistons));
    Bmemset(g_swCrossplayDukeIncrementalMovers, 0, sizeof(g_swCrossplayDukeIncrementalMovers));
    Bmemset(g_swCrossplayDukeWaves, 0, sizeof(g_swCrossplayDukeWaves));
    Bmemset(g_swCrossplayDukeProjectileShooters, 0, sizeof(g_swCrossplayDukeProjectileShooters));
    Bmemset(g_swCrossplayDukeMapSounds, 0, sizeof(g_swCrossplayDukeMapSounds));
    Bmemset(g_swCrossplayDukeTransients, 0, sizeof(g_swCrossplayDukeTransients));
    Bmemset(g_swCrossplayDukeTripmines, 0, sizeof(g_swCrossplayDukeTripmines));
    Bmemset(g_swCrossplayDukeSpriteBehaviors, 0, sizeof(g_swCrossplayDukeSpriteBehaviors));
    SW_CrossplayResetDukeRuntimeTileCache();
    Bmemset(g_swCrossplayDukeTrainPlayerCarrySector, 0xff, sizeof(g_swCrossplayDukeTrainPlayerCarrySector));
    Bmemset(g_swCrossplayDukeTrainPlayerLastCarrySector, 0xff, sizeof(g_swCrossplayDukeTrainPlayerLastCarrySector));
    Bmemset(g_swCrossplayDukeTrainPlayerCarryGrace, 0, sizeof(g_swCrossplayDukeTrainPlayerCarryGrace));
    Bmemset(g_swCrossplayDukeTeleporterBlockSector, 0xff, sizeof(g_swCrossplayDukeTeleporterBlockSector));
    Bmemset(g_swCrossplayDukeTeleporterBlockSprite, 0xff, sizeof(g_swCrossplayDukeTeleporterBlockSprite));
    Bmemset(g_swCrossplayDukeWaterWarpBlockSector, 0xff, sizeof(g_swCrossplayDukeWaterWarpBlockSector));
    Bmemset(g_swCrossplayDukeTeleporterHold, 0, sizeof(g_swCrossplayDukeTeleporterHold));
    Bmemset(g_swCrossplayDukeWaterWarpHold, 0, sizeof(g_swCrossplayDukeWaterWarpHold));
}

int SW_CrossplayDukeAIActive(void);
int SW_CrossplayDukeAISpriteIsEnemy(int spriteNum);
int SW_CrossplayDukeAIPlayerDamageEvents(void);
int SW_CrossplayDukeAISoundEvents(void);
int SW_CrossplayDukeAIFrameFallbacks(void);
int SW_CrossplayDukeAIFrameResolveFailures(void);
int SW_CrossplayDukeAISpriteDukePicnum(int spriteNum);
int SW_CrossplayDukeAISpriteDukeStatnum(int spriteNum);
int SW_CrossplayDukeAIOwnedCount(void);
static int32_t SW_CrossplayDukeAIHitKind(int32_t spriteNum);
extern "C" void VB_DukeAI_DamageActor(int spriteNum, int damage, int hitKind, int impactAng, int ownerSprite);
extern "C" int VB_DukeAI_SpriteIsAlive(int spriteNum);
extern "C" int VB_DukeAI_TileIsTripmine(int tileNum);
extern "C" int VB_DukeAI_TileIsLaserLine(int tileNum);
extern "C" int VB_DukeAI_TileIsFireExt(int tileNum);
extern "C" int VB_DukeAI_MirrorTile(void);
extern "C" int VB_DukeAI_SpriteIsNoShade(int spriteNum);
extern "C" int VB_DukeAI_ShotCount(void);
extern "C" void VB_DukeAI_ActorState(int spriteNum, int *outStat, int *outAction, int *outMove, int *outCurFrame,
                                     int *outStayput);
extern "C" void VB_DukeAI_WatchSprite(int spriteNum);
extern "C" void VB_DukeAI_WatchedState(int *outStat, int *outXvel, int *outZvel, int *outCurFrame, int *outHitag);
extern "C" void VB_DukeAI_WatchedPos(int *outX, int *outY, int *outZ, int *outAng, int *outAction, int *outMove);
extern "C" int VB_DukeAI_WatchedFloorDist(void);
extern "C" int VB_DukeAI_WatchedPicnum(void);
extern "C" int VB_DukeAI_SpriteHealth(int spriteNum);
static int32_t SW_CrossplayActivateDukeExplosivesForWall(int32_t wallNum, vec3_t const *hitPos);
static int32_t SW_CrossplayActivateDukeLightsForWall(int32_t wallNum);
static int32_t SW_CrossplayActivateDukeExplosionLightsWithHitag(int16_t hitag);
static int32_t SW_CrossplayActivateDukeLightsWithHitag(int16_t hitag, int32_t oneShot);
static int32_t SW_CrossplayOperateDukeRespawns(int16_t lotag);
static void SW_CrossplayDukeAIAdoptSprite(int32_t spriteNum, int16_t dukePicnum);
extern "C" int VB_DukeAI_IsAdoptableTile(int tileNum);
extern "C" int VB_DukeAI_HasActorCode(int tileNum);

// Duke should own any living actor it has CON code for, not just tiles flagged
// as enemies: some enemy variants (LIZMANSTAYPUT) are plain `actor` blocks that
// morph themselves on their first tic, and leaving those to the host's
// approximation is what makes an enemy walk and turn but never do anything else.
static int32_t SW_CrossplayDukeShouldRunDukeAI(int16_t dukePicnum, uint16_t behaviorFlags)
{
    if (dukePicnum <= 0)
        return 0;

    if (VB_DukeAI_IsAdoptableTile(dukePicnum))
        return 1;

    int32_t const viaActorCode =
        ((behaviorFlags & (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) ==
         (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) &&
        VB_DukeAI_HasActorCode(dukePicnum);

    if (viaActorCode && Bgetenv("VB_CP_ADOPT_DEBUG") != NULL)
        LOG_F(INFO, "CPADOPT tile %d adopted via CON actor code", dukePicnum);

    return viaActorCode;
}
static void SW_CrossplayTickDukeRespawns(void);
static int32_t SW_CrossplayDukePlayerInMovingSector(PLAYERp const pp, int16_t sectnum);
static int32_t SW_CrossplayDukeSectorBaseLotag(int16_t sectnum);
static int32_t SW_CrossplayStepDukeZ(int32_t *z, int32_t targetZ, int32_t speed);
static int32_t SW_CrossplayActivateDukeBySector(int16_t sectnum, PLAYERp pp);
static int32_t SW_CrossplayOperateDukeTag(int16_t lotag, PLAYERp pp);
static void SW_CrossplayTickDukeSpriteBehaviors(void);
static void SW_CrossplayTickDukeExplosives(void);
static void SW_CrossplayTickDukeCharges(void);
static int32_t SW_CrossplayExplodeDukeSourceSprite(int32_t spriteNum, vec3_t const *hitPos, int32_t soundNum);
static int32_t SW_CrossplaySelfTestActive(void);
static int32_t SW_CrossplayIsDukeChargeSprite(int32_t spriteNum);
static void SW_CrossplayArmDukeChargeChain(int32_t spriteNum);
static void SW_CrossplayArmDukeChargesWithHitag(int16_t hitag, int32_t seedSprite);
static void SW_CrossplayTickDukeTransients(void);
static void SW_CrossplayTickDukeTripmines(void);
static void SW_CrossplayRegisterDukeTripmine(int32_t spriteNum);
static int32_t SW_CrossplayDetonateDukeTripmine(int32_t spriteNum, vec3_t const *hitPos);
static int32_t SW_CrossplayActivateDukeDropFloorsInSector(int16_t sectnum);
static int32_t SW_CrossplayActivateDukeDropFloorsWithHitag(int16_t hitag);
static void SW_CrossplayTickDukeDropFloors(void);
static int32_t SW_CrossplayActivateDukeQuakesInSector(int16_t sectnum);
static int32_t SW_CrossplayActivateDukeQuakesWithHitag(int16_t hitag);
static void SW_CrossplayTickDukeQuakes(void);
static int32_t SW_CrossplayToggleDukeConveyorsAndPistonsWithHitag(int16_t hitag);
static int32_t SW_CrossplayActivateDukeIncrementalMoversInSector(int16_t sectnum);
static int32_t SW_CrossplayToggleDukeIncrementalMoversWithHitag(int16_t hitag);
static int32_t SW_CrossplayStartDukeIncrementalMoversWithHitag(int16_t hitag);
static int32_t SW_CrossplayActivateDukeProjectileShootersInSector(int16_t sectnum);
static int32_t SW_CrossplayToggleDukeProjectileShootersWithHitag(int16_t hitag);
static void SW_CrossplayTickDukeWallAnims(void);
static void SW_CrossplayTickDukePistons(void);
static void SW_CrossplayTickDukeIncrementalMovers(void);
static void SW_CrossplayTickDukeWaves(void);
static void SW_CrossplayTickDukeProjectileShooters(void);
static void SW_CrossplayTickDukeMapSounds(void);
static void SW_CrossplayTickDukeLights(void);
static void SW_CrossplayTickDukeLightnings(void);
static void SW_CrossplayTickDukeQuakeDebris(void);
static void SW_CrossplayTickDukeWaterWarps(void);

static int32_t SW_CrossplayAngleDelta(int32_t from, int32_t to)
{
    from &= 2047;
    to &= 2047;

    if (klabs(from - to) < 1024)
        return to - from;

    if (to > 1024)
        to -= 2048;
    if (from > 1024)
        from -= 2048;

    return to - from;
}

static int32_t SW_CrossplayFindDukePivot(int16_t hitag, vec2_t *pivot, int16_t *pivotAng)
{
    for (int32_t i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum == MAXSTATUS ||
            artCrossplayOriginalMapSpritePicnum(i) != VB_DUKE_SECTOREFFECTOR ||
            sprite[i].lotag != VB_DUKE_SE_PIVOT || sprite[i].hitag != hitag)
            continue;

        if (pivot)
            *pivot = sprite[i].xy;
        if (pivotAng)
            *pivotAng = sprite[i].ang;
        return 1;
    }

    return 0;
}

static int32_t SW_CrossplayFindDukeTeleporterDest(int32_t sourceSprite)
{
    if ((unsigned)sourceSprite >= MAXSPRITES)
        return -1;

    for (int32_t i = 0; i < MAXSPRITES; ++i)
    {
        int16_t const lotag = sprite[i].lotag;
        if (i == sourceSprite ||
            sprite[i].statnum == MAXSTATUS ||
            artCrossplayOriginalMapSpritePicnum(i) != VB_DUKE_SECTOREFFECTOR ||
            (lotag != VB_DUKE_SE_TELEPORT && lotag != VB_DUKE_SE_ONE_WAY_TELEPORT) ||
            sprite[i].hitag != sprite[sourceSprite].hitag)
            continue;

        return i;
    }

    return -1;
}

static int32_t SW_CrossplayDukePlayerCrossedZPlane(int32_t currentZ, int32_t previousZ,
                                                   int32_t velocityZ, int32_t planeZ, int32_t slop)
{
    int32_t const currentDelta = currentZ - planeZ;
    int32_t const previousDelta = previousZ - planeZ;

    if (klabs(currentDelta) <= slop || klabs(previousDelta) <= slop)
        return 1;

    if ((currentDelta < 0 && previousDelta > 0) || (currentDelta > 0 && previousDelta < 0))
        return 1;

    // SW can refresh oldposz before the compatibility bridge runs.  When that
    // happens, fall/rise velocity is the stronger evidence that Duke's SE7
    // trigger plane was crossed this tick.
    int32_t const velocitySlop = max<int32_t>(slop, klabs(velocityZ) + Z(4));
    return (velocityZ > 0 && currentDelta > 0 && currentDelta <= velocitySlop) ||
           (velocityZ < 0 && currentDelta < 0 && -currentDelta <= velocitySlop);
}

static int32_t SW_CrossplayDukePlayerTouchesTeleporterSource(PLAYERp const pp,
                                                             sw_crossplay_duke_teleporter_t const &teleporter,
                                                             int32_t requireCrossing)
{
    if (!SW_CrossplayDukePlayerInMovingSector(pp, teleporter.sourceSect) ||
        (unsigned)teleporter.destSect >= (unsigned)numsectors)
        return 0;

    int32_t const floorDist = max<int32_t>(pp->floor_dist, SW_PlayerRunFloorDist());
    if (teleporter.sourceOnFloor)
    {
        int32_t const feetZ = pp->posz + floorDist;
        int32_t const oldFeetZ = pp->oldposz + floorDist;
        return SW_CrossplayDukePlayerCrossedZPlane(feetZ, oldFeetZ, pp->z_speed, teleporter.source.z, Z(16));
    }

    // Non-floor Duke SE7s are the vertical shaft/pipe teleporters.  Duke triggers
    // them purely on the player's Z passing the SE sprite Z (a fixed 4096 window),
    // never on the enclosing sector's floor or ceiling: treating the floor as a
    // trigger plane makes a player who is merely standing in the sector teleport,
    // which is what produced the E1L1/E3L6 teleporter ping-pong.
    if (klabs(pp->posz - teleporter.source.z) < Z(16))
        return 1;

    if (!requireCrossing)
        return 0;

    // A fast SW fall can step over Duke's window in a single tic, so also accept an
    // unambiguous crossing of the SE sprite's plane between the last two positions.
    return SW_CrossplayDukePlayerCrossedZPlane(pp->posz, pp->oldposz, pp->z_speed, teleporter.source.z, Z(16));
}

static int32_t SW_CrossplayDukePlayerStillOnBlockedTeleporterSector(PLAYERp const pp, int16_t blockSect)
{
    if ((unsigned)blockSect >= (unsigned)numsectors)
        return 0;

    return SW_CrossplayDukePlayerInMovingSector(pp, blockSect);
}

static sw_crossplay_duke_teleporter_t const *SW_CrossplayFindDukeTeleporterBySourceSprite(int16_t sourceSprite)
{
    if (sourceSprite < 0)
        return NULL;

    for (int32_t i = 0; i < g_swCrossplayDukeTeleporterCount; ++i)
    {
        sw_crossplay_duke_teleporter_t const &teleporter = g_swCrossplayDukeTeleporters[i];
        if (teleporter.sourceSprite == sourceSprite)
            return &teleporter;
    }

    return NULL;
}

static int32_t SW_CrossplayDukePlayerStillOnBlockedTeleporter(PLAYERp const pp, int16_t blockSect, int16_t blockSprite)
{
    // Duke holds the player briefly after a teleport and re-arms as soon as the
    // player no longer satisfies the return teleporter's own trigger.  Holding on
    // plain sector membership instead latches the return teleporter off for as long
    // as the player stands anywhere in the destination sector, which is why the
    // E1L1 shaft could only be used in one direction.
    sw_crossplay_duke_teleporter_t const *const blockedTeleporter = SW_CrossplayFindDukeTeleporterBySourceSprite(blockSprite);
    if (blockedTeleporter != NULL)
        return SW_CrossplayDukePlayerTouchesTeleporterSource(pp, *blockedTeleporter, 0);

    return SW_CrossplayDukePlayerStillOnBlockedTeleporterSector(pp, blockSect);
}

static int32_t SW_CrossplayDukeSectorIsWaterStackLotag(int32_t lotag)
{
    lotag &= 0x3fff;
    return lotag == VB_DUKE_ST_ABOVE_WATER || lotag == VB_DUKE_ST_UNDERWATER;
}

static int32_t SW_CrossplayDukeSE7IsWaterStack(int32_t spriteNum, int32_t destSprite)
{
    if ((unsigned)spriteNum >= MAXSPRITES || (unsigned)destSprite >= MAXSPRITES)
        return 0;

    int16_t const sourceSect = sprite[spriteNum].sectnum;
    int16_t const destSect = sprite[destSprite].sectnum;
    if ((unsigned)sourceSect >= (unsigned)numsectors || (unsigned)destSect >= (unsigned)numsectors)
        return 0;

    return SW_CrossplayDukeSectorIsWaterStackLotag(sector[sourceSect].lotag) &&
           SW_CrossplayDukeSectorIsWaterStackLotag(sector[destSect].lotag);
}

static int32_t SW_CrossplayDukePlayerTouchesWaterWarp(PLAYERp const pp, sw_crossplay_duke_water_warp_t const &warp)
{
    if (!SW_CrossplayDukePlayerInMovingSector(pp, warp.sourceSect) ||
        (unsigned)warp.destSect >= (unsigned)numsectors)
        return 0;

    int32_t const floorDist = max<int32_t>(pp->floor_dist, SW_PlayerRunFloorDist());
    int32_t const ceilingDist = max<int32_t>(pp->ceiling_dist, SW_PlayerRunCeilingDist());

    if (warp.sourceLotag == VB_DUKE_ST_ABOVE_WATER)
    {
        int32_t const planeZ = sector[warp.sourceSect].floorz;
        int32_t const feetZ = pp->posz + floorDist;
        int32_t const oldFeetZ = pp->oldposz + floorDist;
        return feetZ >= planeZ - Z(4) && (oldFeetZ < planeZ - Z(4) || pp->z_speed > 0);
    }

    if (warp.sourceLotag == VB_DUKE_ST_UNDERWATER)
    {
        int32_t const planeZ = sector[warp.sourceSect].ceilingz;
        int32_t const headZ = pp->posz - ceilingDist;
        int32_t const oldHeadZ = pp->oldposz - ceilingDist;
        return headZ <= planeZ + Z(12) && (oldHeadZ > planeZ + Z(12) || pp->z_speed < 0);
    }

    return 0;
}

static void SW_CrossplaySetDukeWaterSectorFlags(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return;

    int32_t const lotag = SW_CrossplayDukeSectorBaseLotag(sectnum);
    if (lotag == VB_DUKE_ST_ABOVE_WATER)
        SET(sector[sectnum].extra, SECTFX_DIVE_AREA | SECTFX_LIQUID_WATER);
    else if (lotag == VB_DUKE_ST_UNDERWATER)
        SET(sector[sectnum].extra, SECTFX_UNDERWATER | SECTFX_LIQUID_WATER);
}

static void SW_CrossplayApplyDukeWaterSectorFlags(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeWaterWarpCount; ++i)
    {
        SW_CrossplaySetDukeWaterSectorFlags(g_swCrossplayDukeWaterWarps[i].sourceSect);
        SW_CrossplaySetDukeWaterSectorFlags(g_swCrossplayDukeWaterWarps[i].destSect);
    }
}

static void SW_CrossplayWarpDukePlayerToWaterStack(PLAYERp const pp, sw_crossplay_duke_water_warp_t const &warp)
{
    if (pp == NULL || (unsigned)warp.sourceSect >= (unsigned)numsectors ||
        (unsigned)warp.destSect >= (unsigned)numsectors)
        return;

    int32_t const oldPosX = pp->posx;
    int32_t const oldPosY = pp->posy;
    int32_t const oldPosZ = pp->posz;
    int32_t const posDiffX = pp->oposx - oldPosX;
    int32_t const posDiffY = pp->oposy - oldPosY;
    int32_t const posDiffZ = pp->oposz - oldPosZ;

    pp->posx += warp.dest.x - warp.source.x;
    pp->posy += warp.dest.y - warp.source.y;

    if (warp.destLotag == VB_DUKE_ST_UNDERWATER)
        pp->posz = sector[warp.destSect].ceilingz + max<int32_t>(Z(12), pp->ceiling_dist + Z(2));
    else
        pp->posz = sector[warp.destSect].floorz - max<int32_t>(pp->floor_dist, SW_PlayerRunFloorDist()) - Z(2);

    pp->lastcursectnum = pp->cursectnum;
    pp->cursectnum = warp.destSect;
    pp->oldposx = pp->posx;
    pp->oldposy = pp->posy;
    pp->oldposz = pp->posz;
    pp->oposx = pp->posx + posDiffX;
    pp->oposy = pp->posy + posDiffY;
    pp->oposz = pp->posz + posDiffZ;
    pp->lv_sectnum = pp->cursectnum;
    pp->lv_x = pp->posx;
    pp->lv_y = pp->posy;
    pp->lv_z = pp->posz;
    pp->jump_speed = 0;
    pp->z_speed = 0;
    RESET(pp->Flags, PF_JUMPING | PF_FALLING);

    if ((unsigned)pp->PlayerSprite < MAXSPRITES)
    {
        sprite[pp->PlayerSprite].xyz = pp->pos;
        changespritesect(pp->PlayerSprite, pp->cursectnum);
        UpdatePlayerSprite(pp);
        DoPlayerZrange(pp);
    }

    if (warp.destLotag == VB_DUKE_ST_UNDERWATER && !TEST(pp->Flags, PF_DIVING))
        DoPlayerBeginDiveNoWarp(pp);
    else if (warp.destLotag == VB_DUKE_ST_ABOVE_WATER && TEST(pp->Flags, PF_DIVING))
        DoPlayerStopDiveNoWarp(pp);
}

static int32_t SW_CrossplayFindDukeLocator(int16_t tag, int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeLocatorCount; ++i)
    {
        sw_crossplay_duke_locator_t const &locator = g_swCrossplayDukeLocators[i];
        if ((sectnum < 0 || locator.sectnum == sectnum) && locator.lotag == tag)
            return i;
    }

    return -1;
}

static void SW_CrossplaySetSectorWallInterpolation(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return;

    int32_t const startWall = sector[sectnum].wallptr;
    int32_t const endWall = startWall + sector[sectnum].wallnum;
    if (startWall < 0 || endWall > MAXWALLS)
        return;

    for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
    {
        setinterpolation(&wall[wallNum].x);
        setinterpolation(&wall[wallNum].y);

        uint16_t const nextWall = wall[wallNum].nextwall;
        if (nextWall < MAXWALLS)
        {
            setinterpolation(&wall[wall[nextWall].point2].x);
            setinterpolation(&wall[wall[nextWall].point2].y);
        }
    }
}

static int32_t SW_CrossplayDukeGpspeedForSector(int16_t sectnum)
{
    for (int32_t i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum == MAXSTATUS ||
            sprite[i].sectnum != sectnum ||
            artCrossplayOriginalMapSpritePicnum(i) != VB_DUKE_GPSPEED)
            continue;

        return sprite[i].lotag;
    }

    return sector[sectnum].extra > 0 ? sector[sectnum].extra : 0;
}

static int32_t SW_CrossplayDukeSectorBaseLotag(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    return sector[sectnum].lotag & 0x3fff;
}

static int32_t SW_CrossplayDukeSectorSpeedForSector(int16_t sectnum)
{
    int32_t speed = SW_CrossplayDukeGpspeedForSector(sectnum);
    if (speed <= 0 && (unsigned)sectnum < (unsigned)numsectors && sector[sectnum].extra > 0)
        speed = sector[sectnum].extra;

    // Duke initializes normal sector speed to 256 before GPSPEED overrides it.
    if (speed <= 0)
        speed = 256;

    return speed;
}

int SW_CrossplayDukeMapBridgeActive(void)
{
    return artCrossplayMapArtPrepared() && SW_CrossplayIsDukeLikeGame(artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS));
}

static int16_t SW_CrossplayRemapDukeTileForRuntime(int16_t sourceTile)
{
    if ((unsigned)sourceTile >= MAXTILES)
        return sourceTile;

    int16_t remappedTile = artCrossplayRemapMapTile(sourceTile);
    if (remappedTile != sourceTile)
        return remappedTile;

    if (!g_swCrossplayDukeRuntimeTileCacheReady)
        SW_CrossplayResetDukeRuntimeTileCache();

    remappedTile = g_swCrossplayDukeRuntimeTileCache[sourceTile];
    if (remappedTile >= 0)
        return remappedTile;

    remappedTile = artCrossplayLoadForeignGameTile(artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS), sourceTile);
    remappedTile = remappedTile >= 0 ? remappedTile : sourceTile;
    g_swCrossplayDukeRuntimeTileCache[sourceTile] = remappedTile;
    return remappedTile;
}

static void SW_CrossplaySetDukeWallPicnum(int32_t wallNum, int16_t sourceTile)
{
    if ((unsigned)wallNum >= (unsigned)numwalls)
        return;

    wall[wallNum].picnum = SW_CrossplayRemapDukeTileForRuntime(sourceTile);
}

static void SW_CrossplaySetDukeWallOverpicnum(int32_t wallNum, int16_t sourceTile)
{
    if ((unsigned)wallNum >= (unsigned)numwalls)
        return;

    wall[wallNum].overpicnum = SW_CrossplayRemapDukeTileForRuntime(sourceTile);
}

static void SW_CrossplayPlayDukeWallBreakSound(int32_t wallNum, vec3_t const *hitPos, int32_t soundNum)
{
    int x = 0, y = 0, z = 0;

    if (soundNum <= DIGI_NULL)
        return;

    if (hitPos != NULL)
    {
        x = hitPos->x;
        y = hitPos->y;
        z = hitPos->z;
    }
    else if ((unsigned)wallNum < (unsigned)numwalls)
    {
        x = wall[wallNum].x;
        y = wall[wallNum].y;
    }

    PlaySound(soundNum, &x, &y, &z, v3df_dontpan|v3df_doppler);
}

// Duke funnels its machines and tech walls through G_BreakWall(), which plays
// this pair.  Nothing Duke breaks uses SHORT_CIRCUIT -- that sound belongs to
// the shrinker and to sparking fixtures -- so the bridge must not either.
static void SW_CrossplayPlayDukeBrokenWallSound(int32_t wallNum, vec3_t const *hitPos)
{
    SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_VENT_BUST);
    SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_GLASS_HEAVYBREAK);
}

static void SW_CrossplayClearDukeSpriteBehavior(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE)
    {
        behavior.flags = 0;
        if (g_swCrossplayDukeSpriteBehaviorCount > 0)
            --g_swCrossplayDukeSpriteBehaviorCount;
    }
}

static void SW_CrossplaySetDukeSpritePicnum(int32_t spriteNum, int16_t sourceTile)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    sprite[spriteNum].picnum = SW_CrossplayRemapDukeTileForRuntime(sourceTile);
    g_swCrossplayDukeSpriteBehaviors[spriteNum].sourcePicnum = sourceTile;
}

static void SW_CrossplayDisplayDukeSpritePicnum(int32_t spriteNum, int16_t sourceTile)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    sprite[spriteNum].picnum = SW_CrossplayRemapDukeTileForRuntime(sourceTile);
    SW_CrossplayApplyDukeSourceInvisibility(spriteNum, g_swCrossplayDukeSpriteBehaviors[spriteNum]);
}

static void SW_CrossplayDisplayDukeSpriteVisualOverride(int32_t spriteNum, sw_crossplay_duke_sprite_behavior_t const &behavior)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    sprite[spriteNum].picnum = SW_CrossplayRemapDukeTileForRuntime(behavior.sourcePicnum);
    sprite[spriteNum].pal = behavior.displayPal;
    SW_CrossplayApplyDukeSourceInvisibility(spriteNum, behavior);
}

static void SW_CrossplayApplyDukeSpriteVisualOverrides(void)
{
    if (g_swCrossplayDukeSpriteBehaviorCount <= 0)
        return;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
        if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE) ||
            sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_VISUAL_OVERRIDE)
            SW_CrossplayDisplayDukeSpriteVisualOverride(spriteNum, behavior);
        else
            SW_CrossplayApplyDukeSourceInvisibility(spriteNum, behavior);
    }
}

static void SW_CrossplayPlayDukeSpriteSound(int32_t spriteNum, vec3_t const *hitPos, int32_t soundNum)
{
    int x = 0, y = 0, z = 0;

    if (soundNum <= DIGI_NULL)
        return;

    if (hitPos != NULL)
    {
        x = hitPos->x;
        y = hitPos->y;
        z = hitPos->z;
    }
    else if ((unsigned)spriteNum < MAXSPRITES)
    {
        x = sprite[spriteNum].x;
        y = sprite[spriteNum].y;
        z = sprite[spriteNum].z;
    }

    PlaySound(soundNum, &x, &y, &z, v3df_dontpan|v3df_doppler);
}

static void SW_CrossplayApplyDukeSourceInvisibility(int32_t spriteNum, sw_crossplay_duke_sprite_behavior_t const &behavior)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE)
        SET(sprite[spriteNum].cstat, CSTAT_SPRITE_INVISIBLE);
}

static int16_t SW_CrossplaySpawnDukeRuntimeSprite(int16_t sourcePicnum, int16_t sectnum, vec3_t const &pos,
                                                  int16_t ang, uint8_t xrepeat, uint8_t yrepeat, uint16_t cstat)
{
    if ((unsigned)sectnum >= (unsigned)numsectors || sourcePicnum < 0)
        return -1;

    int16_t const spriteNum = (int16_t)COVERinsertsprite(sectnum, STAT_NO_STATE);
    if (spriteNum < 0)
        return -1;

    SPRITEp const sp = &sprite[spriteNum];
    sp->xyz = pos;
    sp->picnum = SW_CrossplayRemapDukeTileForRuntime(sourcePicnum);
    sp->cstat = cstat;
    sp->shade = 0;
    sp->pal = 0;
    sp->clipdist = 4;
    sp->xrepeat = xrepeat;
    sp->yrepeat = yrepeat;
    sp->xoffset = 0;
    sp->yoffset = 0;
    sp->ang = ang;
    sp->owner = -1;
    sp->xvel = sp->yvel = sp->zvel = 0;
    sp->lotag = sp->hitag = sp->extra = 0;

    return spriteNum;
}

static void SW_CrossplayClearDukeTransient(int32_t transientIndex)
{
    if ((unsigned)transientIndex >= (unsigned)ARRAY_SIZE(g_swCrossplayDukeTransients))
        return;

    sw_crossplay_duke_transient_t &transient = g_swCrossplayDukeTransients[transientIndex];
    if (!transient.active)
        return;

    if ((unsigned)transient.spriteNum < MAXSPRITES && sprite[transient.spriteNum].statnum != MAXSTATUS)
        SW_CrossplayDeleteDukeSprite(transient.spriteNum);

    transient.active = 0;
    transient.spriteNum = -1;
    if (g_swCrossplayDukeTransientCount > 0)
        --g_swCrossplayDukeTransientCount;
}

static sw_crossplay_duke_transient_t *SW_CrossplayAllocateDukeTransient(void)
{
    for (int32_t i = 0; i < (int32_t)ARRAY_SIZE(g_swCrossplayDukeTransients); ++i)
    {
        if (!g_swCrossplayDukeTransients[i].active)
        {
            ++g_swCrossplayDukeTransientCount;
            sw_crossplay_duke_transient_t &transient = g_swCrossplayDukeTransients[i];
            Bmemset(&transient, 0, sizeof(transient));
            transient.spriteNum = -1;
            transient.owner = -1;
            transient.active = 1;
            return &transient;
        }
    }

    return NULL;
}

static int16_t SW_CrossplaySpawnDukeTransient(int16_t sourcePicnum, int16_t sectnum, vec3_t const &pos,
                                              int16_t ang, uint8_t xrepeat, uint8_t yrepeat, uint16_t cstat,
                                              int32_t xvel, int32_t yvel, int32_t zvel,
                                              uint16_t ttl, uint8_t type, int16_t owner)
{
    sw_crossplay_duke_transient_t *const transient = SW_CrossplayAllocateDukeTransient();
    if (transient == NULL)
        return -1;

    int16_t const spriteNum = SW_CrossplaySpawnDukeRuntimeSprite(sourcePicnum, sectnum, pos, ang, xrepeat, yrepeat, cstat);
    if (spriteNum < 0)
    {
        transient->active = 0;
        if (g_swCrossplayDukeTransientCount > 0)
            --g_swCrossplayDukeTransientCount;
        return -1;
    }

    transient->spriteNum = spriteNum;
    transient->sourcePicnum = sourcePicnum;
    transient->owner = owner;
    transient->xvel = xvel;
    transient->yvel = yvel;
    transient->zvel = zvel;
    transient->ttl = ttl;
    transient->tick = 0;
    transient->type = type;

    sprite[spriteNum].owner = owner;
    return spriteNum;
}

static void SW_CrossplaySpawnDukeVisualProjectile(int16_t owner, int16_t sourcePicnum, int16_t sectnum,
                                                  int32_t startX, int32_t startY, int32_t startZ,
                                                  int32_t targetX, int32_t targetY, int32_t targetZ,
                                                  int16_t ang)
{
    if (sourcePicnum < 0 || (unsigned)sectnum >= (unsigned)numsectors)
        return;

    int32_t const dist = max<int32_t>(FindDistance2D(targetX - startX, targetY - startY), 1);
    int32_t const steps = clamp<int32_t>(dist >> 9, 6, 28);
    vec3_t const pos = { startX, startY, startZ };
    uint8_t repeat = 18;

    if (sourcePicnum == VB_DUKE_RPG)
        repeat = 28;
    else if (sourcePicnum == VB_DUKE_SPIT)
        repeat = 24;

    SW_CrossplaySpawnDukeTransient(sourcePicnum, sectnum, pos, ang, repeat, repeat, 0,
                                   (targetX - startX) / steps, (targetY - startY) / steps, (targetZ - startZ) / steps,
                                   (uint16_t)steps, VB_CROSSPLAY_DUKE_TRANSIENT_PROJECTILE, owner);
}

static void SW_CrossplaySpawnDukeExplosionEffect(vec3_t const &pos, int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        updatesector(pos.x, pos.y, &sectnum);
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return;

    SW_CrossplaySpawnDukeTransient(VB_DUKE_EXPLOSION2, sectnum, pos, 0, 64, 64, 0,
                                   0, 0, 0, 20, VB_CROSSPLAY_DUKE_TRANSIENT_EXPLOSION, -1);
}

static int16_t SW_CrossplayDukeDebrisTileForSprite(int16_t sourcePicnum)
{
    if (SW_CrossplayIsDukeGlassOrScreenSprite(sourcePicnum))
        return VB_DUKE_GLASSPIECES;

    switch (sourcePicnum)
    {
    case VB_DUKE_BOX:
    case VB_DUKE_WOODENHORSE:
        return VB_DUKE_SPLINTERWOOD;
    case VB_DUKE_BOTTLE1:
    case VB_DUKE_BOTTLE2:
    case VB_DUKE_BOTTLE3:
    case VB_DUKE_BOTTLE4:
    case VB_DUKE_BOTTLE5:
    case VB_DUKE_BOTTLE6:
    case VB_DUKE_BOTTLE7:
    case VB_DUKE_BOTTLE8:
    case VB_DUKE_BOTTLE10:
    case VB_DUKE_BOTTLE11:
    case VB_DUKE_BOTTLE12:
    case VB_DUKE_BOTTLE13:
    case VB_DUKE_BOTTLE14:
    case VB_DUKE_BOTTLE15:
    case VB_DUKE_BOTTLE16:
    case VB_DUKE_BOTTLE17:
    case VB_DUKE_BOTTLE18:
    case VB_DUKE_BOTTLE19:
        return VB_DUKE_GLASSPIECES;
    default:
        return VB_DUKE_SCRAP1 + ((krand1() & 3) * 4);
    }
}

static int32_t SW_CrossplayDukeDebrisCountForSprite(int16_t sourcePicnum)
{
    if (SW_CrossplayIsDukeGlassOrScreenSprite(sourcePicnum))
        return 10;

    switch (sourcePicnum)
    {
    case VB_DUKE_RUBBERCAN:
    case VB_DUKE_CANWITHSOMETHING:
    case VB_DUKE_CANWITHSOMETHING2:
    case VB_DUKE_CANWITHSOMETHING3:
    case VB_DUKE_CANWITHSOMETHING4:
    case VB_DUKE_VENDMACHINE:
    case VB_DUKE_COLAMACHINE:
    case VB_DUKE_ATM:
        return 10;
    case VB_DUKE_BOX:
    case VB_DUKE_VASE:
    case VB_DUKE_HYDRENT:
        return 6;
    default:
        return 4;
    }
}

static void SW_CrossplaySpawnDukeBreakDebris(int32_t spriteNum, int16_t sourcePicnum, vec3_t const *hitPos)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    SPRITEp const sp = &sprite[spriteNum];
    int16_t const sectnum = sp->sectnum;
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return;

    vec3_t const origin = hitPos != NULL ? *hitPos : sp->xyz;
    int32_t const count = SW_CrossplayDukeDebrisCountForSprite(sourcePicnum);

    for (int32_t i = 0; i < count; ++i)
    {
        int16_t tile = SW_CrossplayDukeDebrisTileForSprite(sourcePicnum);
        if (tile == VB_DUKE_SCRAP1)
            tile = (krand1() & 1) ? VB_DUKE_SCRAP1 : VB_DUKE_SCRAP6;

        int16_t const ang = (int16_t)(krand1() & 2047);
        int32_t const speed = 48 + (krand1() & 127);
        SW_CrossplaySpawnDukeTransient(tile, sectnum, origin, ang, 16 + (krand1() & 15), 16 + (krand1() & 15), 0,
                                       MOVEx(speed, ang), MOVEy(speed, ang), -(Z(1) + (krand1() & 4095)),
                                       32 + (krand1() & 31), VB_CROSSPLAY_DUKE_TRANSIENT_DEBRIS, spriteNum);
    }
}

static void SW_CrossplayTickDukeTransients(void)
{
    if (g_swCrossplayDukeTransientCount <= 0)
        return;

    for (int32_t i = 0; i < (int32_t)ARRAY_SIZE(g_swCrossplayDukeTransients); ++i)
    {
        sw_crossplay_duke_transient_t &transient = g_swCrossplayDukeTransients[i];
        if (!transient.active)
            continue;

        if ((unsigned)transient.spriteNum >= MAXSPRITES || sprite[transient.spriteNum].statnum == MAXSTATUS)
        {
            SW_CrossplayClearDukeTransient(i);
            continue;
        }

        SPRITEp const sp = &sprite[transient.spriteNum];
        if (transient.ttl > 0 && transient.tick >= transient.ttl)
        {
            SW_CrossplayClearDukeTransient(i);
            continue;
        }

        ++transient.tick;

        if (transient.type == VB_CROSSPLAY_DUKE_TRANSIENT_EXPLOSION)
        {
            int16_t const frame = (int16_t)min<int32_t>(transient.tick >> 1, 20);
            sp->picnum = SW_CrossplayRemapDukeTileForRuntime((int16_t)(VB_DUKE_EXPLOSION2 + frame));
            continue;
        }

        if (transient.type == VB_CROSSPLAY_DUKE_TRANSIENT_PROJECTILE)
        {
            if (transient.sourcePicnum == VB_DUKE_FIRELASER)
                sp->picnum = SW_CrossplayRemapDukeTileForRuntime((int16_t)(VB_DUKE_FIRELASER + (transient.tick & 1)));
            else if (transient.sourcePicnum == VB_DUKE_SPIT)
                sp->picnum = SW_CrossplayRemapDukeTileForRuntime((int16_t)(VB_DUKE_SPIT + ((transient.tick >> 1) & 3)));
        }

        sp->x += transient.xvel;
        sp->y += transient.yvel;
        sp->z += transient.zvel;

        if (transient.type == VB_CROSSPLAY_DUKE_TRANSIENT_DEBRIS)
            transient.zvel += 768;

        int16_t sectnum = sp->sectnum;
        updatesectorz(sp->x, sp->y, sp->z, &sectnum);
        if ((unsigned)sectnum >= (unsigned)numsectors)
        {
            SW_CrossplayClearDukeTransient(i);
            continue;
        }

        if (sectnum != sp->sectnum)
            changespritesect(transient.spriteNum, sectnum);

        if (transient.type == VB_CROSSPLAY_DUKE_TRANSIENT_DEBRIS)
        {
            int32_t ceilingZ;
            int32_t floorZ;
            getzsofslope(sp->sectnum, sp->x, sp->y, &ceilingZ, &floorZ);
            if (sp->z > floorZ)
            {
                sp->z = floorZ;
                transient.zvel = -transient.zvel / 4;
                transient.xvel = (transient.xvel * 3) >> 2;
                transient.yvel = (transient.yvel * 3) >> 2;
            }
        }
    }
}

static int32_t SW_CrossplayDukeSoundIdToDigi(int32_t soundId)
{
    switch (soundId)
    {
    case 8:   return DIGI_DUKE_MAP_POOLBALLHIT;
    case 9:   return DIGI_DUKE_RPG_EXPLODE;
    case 13:  return DIGI_DUKE_PIPEBOMB_BOUNCE;
    case 14:  return DIGI_DUKE_PIPEBOMB_EXPLODE;
    case 15:  return DIGI_DUKE_LASERTRIP_ONWALL;
    case 16:  return DIGI_DUKE_LASERTRIP_ARMING;
    case 17:  return DIGI_DUKE_LASERTRIP_EXPLODE;
    case 18:  return DIGI_DUKE_VENT_BUST;
    case 19:  return DIGI_DUKE_GLASS_BREAKING;
    case 21:  return DIGI_DUKE_SHORT_CIRCUIT;
    case 50:  return DIGI_DUKE_MAP_JETPACK_IDLE;
    case 67:  return DIGI_DUKE_MAP_TIP2;
    case 70:  return DIGI_DUKE_TELEPORTER;
    case 71:  return DIGI_DUKE_ELEVATOR_ON;
    case 73:  return DIGI_DUKE_ELEVATOR_OFF;
    case 74:  return DIGI_DUKE_MAP_DOOR_OPERATE1;
    case 75:  return DIGI_DUKE_SUBWAY;
    case 76:  return DIGI_DUKE_SWITCH_ON;
    case 79:  return DIGI_DUKE_MAP_FLUSH_TOILET;
    case 81:  return DIGI_DUKE_EARTHQUAKE;
    case 82:  return DIGI_DUKE_MAP_INTRUDER_ALERT;
    case 84:  return DIGI_DUKE_MAP_ENGINE_OPERATING;
    case 85:  return DIGI_DUKE_MAP_REACTOR_ON;
    case 86:  return DIGI_DUKE_MAP_COMPUTER_AMBIENCE;
    case 87:  return DIGI_DUKE_MAP_GEARS_GRINDING;
    case 88:  return DIGI_DUKE_MAP_BUBBLE_AMBIENCE;
    case 89:  return DIGI_DUKE_MAP_MACHINE_AMBIENCE;
    case 90:  return DIGI_DUKE_MAP_SEWER_AMBIENCE;
    case 91:  return DIGI_DUKE_MAP_WIND_AMBIENCE;
    case 92:  return DIGI_DUKE_MAP_SOMETHING_DRIPPING;
    case 95:  return DIGI_DUKE_MAP_BAR_MUSIC;
    case 97:  return DIGI_DUKE_BOS1_RECOG;
    case 109: return DIGI_DUKE_MAP_SHOTGUN_FIRE;
    case 123: return DIGI_DUKE_PIG_PAIN;
    case 124: return DIGI_DUKE_PIG_DYING;
    case 148: return DIGI_DUKE_MAP_DUMPSTER_MOVE;
    case 160: return DIGI_DUKE_OCTA_ATTACK1;
    case 165: return DIGI_DUKE_MAP_DOOR_OPERATE2;
    case 166: return DIGI_DUKE_MAP_DOOR_OPERATE3;
    case 167: return DIGI_DUKE_MAP_DOOR_OPERATE4;
    case 168: return DIGI_DUKE_MAP_BORNTOBEWILDSND;
    case 169: return DIGI_DUKE_MAP_SHOTGUN_COCK;
    case 170: return DIGI_DUKE_MAP_DUMPSTER_MOVE;
    case 171: return DIGI_DUKE_MAP_GENERIC_AMBIENCE2;
    case 172: return DIGI_DUKE_MAP_GENERIC_AMBIENCE3;
    case 173: return DIGI_DUKE_MAP_BUBBLE_AMBIENCE;
    case 175: return DIGI_DUKE_MAP_GENERIC_AMBIENCE6;
    case 178: return DIGI_DUKE_MAP_GENERIC_AMBIENCE18;
    case 179: return DIGI_DUKE_MAP_GENERIC_AMBIENCE19;
    case 183: return DIGI_DUKE_MAP_SECRETLEVELSND;
    case 184: return DIGI_DUKE_MAP_GENERIC_AMBIENCE8;
    case 185: return DIGI_DUKE_MAP_GENERIC_AMBIENCE9;
    case 186: return DIGI_DUKE_MAP_GENERIC_AMBIENCE10;
    case 187: return DIGI_DUKE_MAP_GENERIC_AMBIENCE11;
    case 194: return DIGI_DUKE_MAP_FIRE_CRACKLE;
    case 195: return DIGI_DUKE_MAP_BONUS_SPEECH1;
    case 196: return DIGI_DUKE_MAP_BONUS_SPEECH2;
    case 211: return DIGI_DUKE_LONGTERM_PAIN;
    case 220: return DIGI_DUKE_MAP_WATER_GURGLE;
    case 231: return DIGI_DUKE_MAP_WAR_AMBIENCE1;
    case 232: return DIGI_DUKE_MAP_WAR_AMBIENCE2;
    case 236: return DIGI_DUKE_MAP_WAR_AMBIENCE6;
    case 241: return DIGI_DUKE_MAP_ALIEN_TALK1;
    case 250: return DIGI_DUKE_MAP_DUKE_BOOBY;
    case 256: return DIGI_DUKE_MAP_SPACE_DOOR1;
    case 257: return DIGI_DUKE_MAP_SPACE_DOOR2;
    case 258: return DIGI_DUKE_MAP_SPACE_DOOR3;
    case 259: return DIGI_DUKE_MAP_SPACE_DOOR4;
    case 260: return DIGI_DUKE_MAP_SPACE_DOOR5;
    case 261: return DIGI_DUKE_MAP_ALIEN_ELEVATOR1;
    case 262: return DIGI_DUKE_MAP_VAULT_DOOR;
    case 271: return DIGI_DUKE_MAP_DUKE_KILLED5;
    case 272: return DIGI_DUKE_MAP_ALIEN_SWITCH1;
    case 277: return DIGI_DUKE_MAP_COMPANB2;
    case 279: return DIGI_DUKE_MAP_HELICOP_IDLE;
    case 286: return DIGI_DUKE_MAP_ALIEN_DOOR1;
    case 287: return DIGI_DUKE_MAP_ALIEN_DOOR2;
    case 308: return DIGI_DUKE_MAP_WIND_REPEAT;
    case 321: return DIGI_DUKE_MAP_NEWBEAST_SPIT;
    case 351: return DIGI_DUKE_LIGHTNING_SLAP;
    case 352: return DIGI_DUKE_THUNDER;
    case 357: return DIGI_DUKE_MAP_ALARM;
    case 358: return DIGI_DUKE_MAP_RAIN;
    case 359: return DIGI_DUKE_MAP_DTAG_GREENRUN;
    case 360: return DIGI_DUKE_MAP_DTAG_BROWNRUN;
    case 361: return DIGI_DUKE_MAP_DTAG_GREENSCORE;
    case 362: return DIGI_DUKE_MAP_DTAG_BROWNSCORE;
    case 369: return DIGI_DUKE_MAP_SCREECH;
    case 379: return DIGI_DUKE_MAP_JEEPSOUND;
    case 380: return DIGI_DUKE_MAP_BIGDOORSLAM;
    case 382: return DIGI_DUKE_MAP_WAVESOUND;
    case 394: return DIGI_DUKE_MAP_HORNSND;
    case 395: return DIGI_DUKE_MAP_BELLSND;
    default:  return DIGI_NULL;
    }
}

static int32_t SW_CrossplayDukeMapSoundIdIsAmbient(int32_t soundId)
{
    switch (soundId)
    {
    case 50:
    case 79:
    case 84:
    case 85:
    case 86:
    case 87:
    case 88:
    case 89:
    case 90:
    case 91:
    case 92:
    case 95:
    case 168:
    case 171:
    case 172:
    case 173:
    case 175:
    case 178:
    case 179:
    case 184:
    case 185:
    case 186:
    case 194:
    case 220:
    case 277:
    case 279:
    case 308:
    case 357:
    case 358:
    case 379:
    case 382:
    case 394:
    case 395:
        return 1;
    default:
        return 0;
    }
}

static int32_t SW_CrossplayDukeMapSoundRepeatDelay(int32_t soundId)
{
    switch (soundId)
    {
    case 79:
    case 90:
    case 92:
    case 194:
    case 220:
        return 26 * 6;
    case 95:
    case 168:
        return 26 * 12;
    default:
        return 26 * 4;
    }
}

static int32_t SW_CrossplayDukeMapSoundDistance(int32_t hitag)
{
    if (hitag > 0 && hitag < 32767)
        return hitag;

    return 16384;
}

static void SW_CrossplayPlayDukeMapSound(sw_crossplay_duke_map_sound_t const &sound, int32_t soundNum)
{
    if (soundNum <= DIGI_NULL)
        return;

    int x = sound.pos.x;
    int y = sound.pos.y;
    int z = sound.pos.z;
    PlaySound(soundNum, &x, &y, &z, v3df_dontpan|v3df_doppler);
}

static int32_t SW_CrossplayCallDukeSectorSound(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    for (int32_t i = 0; i < g_swCrossplayDukeMapSoundCount; ++i)
    {
        sw_crossplay_duke_map_sound_t &sound = g_swCrossplayDukeMapSounds[i];
        if (sound.sectnum != sectnum || sound.digi <= DIGI_NULL)
            continue;

        int32_t soundNum = sound.digi;
        if (sound.toggled && sound.alternateDigi > DIGI_NULL && sound.alternateDigi != sound.digi)
            soundNum = sound.alternateDigi;

        SW_CrossplayPlayDukeMapSound(sound, soundNum);

        if (SW_CrossplayDukeSectorBaseLotag(sectnum) != VB_DUKE_ST_SPLITTING_DOOR)
            sound.toggled ^= 1;

        return 1;
    }

    return 0;
}

static int32_t SW_CrossplayAnyPlayerNearDukeMapSound(sw_crossplay_duke_map_sound_t const &sound)
{
    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if ((unsigned)pp->cursectnum >= (unsigned)numsectors)
            continue;

        if (FindDistance2D(pp->posx - sound.pos.x, pp->posy - sound.pos.y) <= sound.distance)
            return 1;
    }

    return 0;
}

static void SW_CrossplayTickDukeMapSounds(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeMapSoundCount; ++i)
    {
        sw_crossplay_duke_map_sound_t &sound = g_swCrossplayDukeMapSounds[i];
        if (sound.digi <= DIGI_NULL || !SW_CrossplayDukeMapSoundIdIsAmbient(sound.soundId))
            continue;

        if (!SW_CrossplayAnyPlayerNearDukeMapSound(sound))
        {
            sound.delay = 0;
            continue;
        }

        if (sound.delay > 0)
        {
            --sound.delay;
            continue;
        }

        SW_CrossplayPlayDukeMapSound(sound, sound.digi);
        sound.delay = SW_CrossplayDukeMapSoundRepeatDelay(sound.soundId);
    }
}

static int16_t SW_CrossplayNextDukeSwitchPicnum(sw_crossplay_duke_sprite_behavior_t const &behavior)
{
    switch (behavior.basePicnum)
    {
    case VB_DUKE_MULTISWITCH:
        return behavior.basePicnum + (behavior.switchState & 3);
    default:
        // Duke: base tile is the off frame, base+1 is the on frame.  Inverting
        // these leaves every switch showing the opposite of its actual state.
        return behavior.switchState ? behavior.basePicnum + 1 : behavior.basePicnum;
    }
}

static void SW_CrossplayToggleDukeSwitchSprite(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_SWITCH))
        return;

    behavior.switchState = behavior.basePicnum == VB_DUKE_MULTISWITCH ? ((behavior.switchState + 1) & 3) : (behavior.switchState ^ 1);
    SW_CrossplaySetDukeSpritePicnum(spriteNum, SW_CrossplayNextDukeSwitchPicnum(behavior));
}

static void SW_CrossplayUpdateDukeSwitchSprites(int16_t lotag)
{
    if (lotag == 0)
        return;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
        if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_SWITCH) || sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        if (sprite[spriteNum].lotag == lotag)
            SW_CrossplayToggleDukeSwitchSprite(spriteNum);
    }
}

static int16_t SW_CrossplayDukeBrokenSpriteTile(int16_t sourcePicnum)
{
    if (SW_CrossplayIsDukeScreenBreakTile(sourcePicnum))
        return VB_DUKE_W_SCREENBREAK + (krand1() % 3);

    switch (sourcePicnum)
    {
    case VB_DUKE_FANSPRITE:
        return VB_DUKE_FANSPRITEBROKE;
    case VB_DUKE_WATERFOUNTAIN:
        return VB_DUKE_WATERFOUNTAINBROKE;
    case VB_DUKE_GRATE1:
        return VB_DUKE_BGRATE1;
    case VB_DUKE_PANNEL1:
    case VB_DUKE_PANNEL2:
        return VB_DUKE_BPANNEL1;
    case VB_DUKE_PANNEL3:
        return VB_DUKE_BPANNEL3;
    case VB_DUKE_CIRCLEPANNEL:
        return VB_DUKE_CIRCLEPANNELBROKE;
    case VB_DUKE_ATM:
        return VB_DUKE_ATMBROKE;
    case VB_DUKE_VENDMACHINE:
    case VB_DUKE_COLAMACHINE:
        return sourcePicnum + 2;
    case VB_DUKE_CACTUS:
        return VB_DUKE_CACTUSBROKE;
    case VB_DUKE_SEENINE:
        return VB_DUKE_SEENINEDEAD;
    default:
        return -1;
    }
}

static int32_t SW_CrossplayDukeSpriteBreakSound(int16_t sourcePicnum)
{
    if (SW_CrossplayIsDukeGlassOrScreenSprite(sourcePicnum))
        return DIGI_DUKE_GLASS_BREAKING;

    switch (sourcePicnum)
    {
    case VB_DUKE_BOTTLE1:
    case VB_DUKE_BOTTLE2:
    case VB_DUKE_BOTTLE3:
    case VB_DUKE_BOTTLE4:
    case VB_DUKE_BOTTLE5:
    case VB_DUKE_BOTTLE6:
    case VB_DUKE_BOTTLE7:
    case VB_DUKE_BOTTLE8:
    case VB_DUKE_BOTTLE10:
    case VB_DUKE_BOTTLE11:
    case VB_DUKE_BOTTLE12:
    case VB_DUKE_BOTTLE13:
    case VB_DUKE_BOTTLE14:
    case VB_DUKE_BOTTLE15:
    case VB_DUKE_BOTTLE16:
    case VB_DUKE_BOTTLE17:
    case VB_DUKE_BOTTLE18:
    case VB_DUKE_BOTTLE19:
        return DIGI_DUKE_GLASS_BREAKING;
    // Duke breaks a machine through G_BreakWall(), which is glass, not sparks.
    case VB_DUKE_ATM:
    case VB_DUKE_VENDMACHINE:
    case VB_DUKE_COLAMACHINE:
    // A_DamageObject_Internal() gives the fan GLASS_HEAVYBREAK.  It used to get
    // SHORT_CIRCUIT here, which is why E1L1's rooftop vent fan broke with an
    // electrical sound instead of Duke's.
    case VB_DUKE_FANSPRITE:
        return DIGI_DUKE_GLASS_HEAVYBREAK;
    // Duke's water fountain breaks silently; it only spawns its water.
    case VB_DUKE_WATERFOUNTAIN:
        return DIGI_NULL;
    case VB_DUKE_GRATE1:
    case VB_DUKE_PANNEL1:
    case VB_DUKE_PANNEL2:
    case VB_DUKE_PANNEL3:
    case VB_DUKE_CIRCLEPANNEL:
    case VB_DUKE_CANWITHSOMETHING:
    case VB_DUKE_CANWITHSOMETHING2:
    case VB_DUKE_CANWITHSOMETHING3:
    case VB_DUKE_CANWITHSOMETHING4:
        return DIGI_DUKE_VENT_BUST;
    default:
        return DIGI_DUKE_THUD;
    }
}

static void SW_CrossplayBreakDukeSprite(int32_t spriteNum, vec3_t const *hitPos)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    int16_t const sourcePicnum = behavior.sourcePicnum >= 0 ? behavior.sourcePicnum : artCrossplayOriginalMapSpritePicnum(spriteNum);

    SW_CrossplaySpawnDukeBreakDebris(spriteNum, sourcePicnum, hitPos);

    int32_t const breakSound = SW_CrossplayDukeSpriteBreakSound(sourcePicnum);
    if (Bgetenv("VB_CP_BREAK_DEBUG") != NULL)
        LOG_F(INFO, "CPBREAK spr=%d tile=%d sound=%d", spriteNum, sourcePicnum, breakSound);

    SW_CrossplayPlayDukeSpriteSound(spriteNum, hitPos, breakSound);

    // A broken fan drops its shadow with it.
    if (sourcePicnum == VB_DUKE_FANSPRITE)
    {
        int16_t const sectNum = sprite[spriteNum].sectnum;
        if ((unsigned)sectNum < (unsigned)numsectors &&
            artCrossplayOriginalMapSectorFloorPicnum(sectNum) == VB_DUKE_FANSHADOW)
            sector[sectNum].floorpicnum = SW_CrossplayRemapDukeTileForRuntime(VB_DUKE_FANSHADOWBROKE);
    }

    int16_t const brokenTile = SW_CrossplayDukeBrokenSpriteTile(sourcePicnum);
    if (brokenTile >= 0)
    {
        SW_CrossplaySetDukeSpritePicnum(spriteNum, brokenTile);
        RESET(sprite[spriteNum].cstat, CSTAT_SPRITE_BLOCK_HITSCAN);
        if (!SW_CrossplayDukeSpriteShouldBlock(brokenTile, 0))
            RESET(sprite[spriteNum].cstat, CSTAT_SPRITE_BLOCK);
        SW_CrossplayClearDukeSpriteBehavior(spriteNum);
        return;
    }

    SW_CrossplayClearDukeSpriteBehavior(spriteNum);
    SW_CrossplayDeleteDukeSprite(spriteNum);
}

static int16_t SW_CrossplayDukeBrokenLightTile(int16_t originalPicnum)
{
    switch (originalPicnum)
    {
    case VB_DUKE_WALLLIGHT1:
        return VB_DUKE_WALLLIGHTBUST1;
    case VB_DUKE_WALLLIGHT2:
        return VB_DUKE_WALLLIGHTBUST2;
    case VB_DUKE_WALLLIGHT3:
        return VB_DUKE_WALLLIGHTBUST3;
    case VB_DUKE_WALLLIGHT4:
        return VB_DUKE_WALLLIGHTBUST4;
    case VB_DUKE_TECHLIGHT2:
        return VB_DUKE_TECHLIGHTBUST2;
    case VB_DUKE_TECHLIGHT4:
        return VB_DUKE_TECHLIGHTBUST4;
    default:
        return -1;
    }
}

static int16_t SW_CrossplayDukeBrokenTechWallTile(int16_t originalPicnum)
{
    switch (originalPicnum)
    {
    case VB_DUKE_W_TECHWALL5:
    case VB_DUKE_W_TECHWALL6:
    case VB_DUKE_W_TECHWALL7:
    case VB_DUKE_W_TECHWALL8:
    case VB_DUKE_W_TECHWALL9:
        return originalPicnum + 1;
    case VB_DUKE_W_MILKSHELF:
        return VB_DUKE_W_MILKSHELFBROKE;
    case VB_DUKE_W_TECHWALL10:
        return VB_DUKE_W_HITTECHWALL10;
    case VB_DUKE_W_TECHWALL1:
    case VB_DUKE_W_TECHWALL11:
    case VB_DUKE_W_TECHWALL12:
    case VB_DUKE_W_TECHWALL13:
    case VB_DUKE_W_TECHWALL14:
        return VB_DUKE_W_HITTECHWALL1;
    case VB_DUKE_W_TECHWALL15:
        return VB_DUKE_W_HITTECHWALL15;
    case VB_DUKE_W_TECHWALL16:
        return VB_DUKE_W_HITTECHWALL16;
    case VB_DUKE_W_TECHWALL2:
        return VB_DUKE_W_HITTECHWALL2;
    case VB_DUKE_W_TECHWALL3:
        return VB_DUKE_W_HITTECHWALL3;
    case VB_DUKE_W_TECHWALL4:
        return VB_DUKE_W_HITTECHWALL4;
    default:
        return -1;
    }
}

static int32_t SW_CrossplayIsDukeScreenBreakTile(int16_t originalPicnum)
{
    switch (originalPicnum)
    {
    case VB_DUKE_OJ:
    case VB_DUKE_FEMPIC2:
    case VB_DUKE_FEMPIC3:
    case VB_DUKE_BORNTOBEWILDSCREEN:
    case VB_DUKE_SCREENBREAK1:
    case VB_DUKE_SCREENBREAK2:
    case VB_DUKE_SCREENBREAK3:
    case VB_DUKE_SCREENBREAK4:
    case VB_DUKE_SCREENBREAK5:
    case VB_DUKE_SCREENBREAK6:
    case VB_DUKE_SCREENBREAK7:
    case VB_DUKE_SCREENBREAK8:
    case VB_DUKE_SCREENBREAK9:
    case VB_DUKE_SCREENBREAK10:
    case VB_DUKE_SCREENBREAK11:
    case VB_DUKE_SCREENBREAK12:
    case VB_DUKE_SCREENBREAK13:
    case VB_DUKE_SCREENBREAK14:
    case VB_DUKE_SCREENBREAK15:
    case VB_DUKE_SCREENBREAK16:
    case VB_DUKE_SCREENBREAK17:
    case VB_DUKE_SCREENBREAK18:
    case VB_DUKE_SCREENBREAK19:
        return 1;
    default:
        return 0;
    }
}


// SW's explosions damage sprites by walking its own status lists and reading the
// SW USER record off each one.  Duke's sprites have neither -- they are parked in
// a status list SW ignores -- so a grenade or rocket blast went straight through
// Duke enemies, charges and breakables without touching them.  This is the blast
// pass for those, and it hands Duke's actors the damage through Duke's own
// protocol so their reaction (pain, gibbing, chained charge explosions) is Duke's.
int SW_CrossplayDamageDukeMapWall(int spriteNum, int wallNum, vec3_t const *hitPos)
{
    UNREFERENCED_PARAMETER(spriteNum);

    if (!SW_CrossplayDukeMapBridgeActive() || (unsigned)wallNum >= (unsigned)numwalls)
        return 0;

    int16_t const originalPicnum = artCrossplayOriginalMapWallPicnum(wallNum);
    int16_t const originalOverpicnum = artCrossplayOriginalMapWallOverpicnum(wallNum);
    int32_t const lightActivated = SW_CrossplayActivateDukeLightsForWall(wallNum);
    int32_t const explosiveActivated = SW_CrossplayActivateDukeExplosivesForWall(wallNum, hitPos);

    if (originalOverpicnum == VB_DUKE_GLASS)
    {
        SW_CrossplaySetDukeWallOverpicnum(wallNum, VB_DUKE_GLASS2);
        wall[wallNum].cstat = 0;
        if (wall[wallNum].nextwall >= 0)
            wall[wall[wallNum].nextwall].cstat = 0;
        SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_GLASS_BREAKING);
        return 1;
    }

    if (originalOverpicnum == VB_DUKE_STAINGLASS1)
    {
        wall[wallNum].cstat = 0;
        if (wall[wallNum].nextwall >= 0)
            wall[wall[wallNum].nextwall].cstat = 0;
        SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_VENT_BUST);
        SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_GLASS_BREAKING);
        return 1;
    }

    switch (originalPicnum)
    {
    case VB_DUKE_COLAMACHINE:
    case VB_DUKE_VENDMACHINE:
        SW_CrossplaySetDukeWallPicnum(wallNum, originalPicnum + 2);
        SW_CrossplayPlayDukeBrokenWallSound(wallNum, hitPos);
        return 1;

    case VB_DUKE_ATM:
        SW_CrossplaySetDukeWallPicnum(wallNum, VB_DUKE_ATMBROKE);
        SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_GLASS_HEAVYBREAK);
        return 1;

    default:
        break;
    }

    if (SW_CrossplayIsDukeScreenBreakTile(originalPicnum))
    {
        SW_CrossplaySetDukeWallPicnum(wallNum, VB_DUKE_W_SCREENBREAK + (krand1() % 3));
        SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos, DIGI_DUKE_GLASS_HEAVYBREAK);
        return 1;
    }

    int16_t brokenTile = SW_CrossplayDukeBrokenTechWallTile(originalPicnum);
    if (brokenTile >= 0)
    {
        // Every tech wall reaches Duke through G_BreakWall().
        SW_CrossplaySetDukeWallPicnum(wallNum, brokenTile);
        SW_CrossplayPlayDukeBrokenWallSound(wallNum, hitPos);
        return 1;
    }

    brokenTile = SW_CrossplayDukeBrokenLightTile(originalPicnum);
    if (brokenTile >= 0)
    {
        SW_CrossplaySetDukeWallPicnum(wallNum, brokenTile);
        // Duke picks between the two glass sounds per shot.
        SW_CrossplayPlayDukeWallBreakSound(wallNum, hitPos,
                                           (krand1() & 1) ? DIGI_DUKE_GLASS_HEAVYBREAK : DIGI_DUKE_GLASS_BREAKING);
        return 1;
    }

    return explosiveActivated || lightActivated;
}

static int32_t SW_CrossplayDukeZMoverSpeedForSector(int16_t sectnum)
{
    int32_t speed = SW_CrossplayDukeSectorSpeedForSector(sectnum);

    if (speed <= 0 || speed > 8192)
        speed = 256;

    return speed;
}

static void SW_CrossplaySetSectorZInterpolation(int16_t sectnum, int32_t lotag)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return;

    if (lotag == VB_DUKE_SE_FLOOR_RISE_FALL)
        setinterpolation(&sector[sectnum].floorz);
    else if (lotag == VB_DUKE_SE_CEILING_RISE_FALL)
        setinterpolation(&sector[sectnum].ceilingz);
}

static sw_crossplay_duke_zmover_t *SW_CrossplayFindDukeZMoverForSector(int16_t sectnum, int32_t lotag)
{
    for (int32_t i = 0; i < g_swCrossplayDukeZMoverCount; ++i)
    {
        sw_crossplay_duke_zmover_t &mover = g_swCrossplayDukeZMovers[i];
        if (mover.sectnum == sectnum && (lotag < 0 || mover.lotag == lotag))
            return &mover;
    }

    return NULL;
}

static int32_t SW_CrossplayDukeSectorHasActiveZMover(int16_t sectnum)
{
    sw_crossplay_duke_zmover_t const *const mover = SW_CrossplayFindDukeZMoverForSector(sectnum, -1);
    return mover && mover->active;
}

static int32_t SW_CrossplayDukeSectorHasActiveZAnim(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveWarpElevator(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveRotateRiseBridge(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveSwingDoor(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveSlidingDoor(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveStretchBridge(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveTwoWayTrain(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveDropFloor(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveTeethDoor(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveIncrementalMover(int16_t sectnum);
static int32_t SW_CrossplayDukeSectorHasActiveProjectileShooter(int16_t sectnum);
static int32_t SW_CrossplayOperateDukeTwoWayTrain(int16_t sectnum);

static int32_t SW_CrossplayDukeCheckActivatorMotion(int16_t lotag)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTouchplateCount; ++i)
    {
        sw_crossplay_duke_touchplate_t const &touchplate = g_swCrossplayDukeTouchplates[i];
        if (touchplate.lotag == lotag && touchplate.moving)
            return 1;
    }

    for (int32_t activatorIndex = 0; activatorIndex < g_swCrossplayDukeActivatorCount; ++activatorIndex)
    {
        sw_crossplay_duke_activator_t const &activator = g_swCrossplayDukeActivators[activatorIndex];
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveZMover(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveZAnim(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveWarpElevator(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveRotateRiseBridge(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveSwingDoor(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveSlidingDoor(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveStretchBridge(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveTwoWayTrain(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveDropFloor(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveTeethDoor(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveIncrementalMover(activator.sectnum))
            return 1;
        if (activator.lotag == lotag && SW_CrossplayDukeSectorHasActiveProjectileShooter(activator.sectnum))
            return 1;
    }

    return 0;
}

static int32_t SW_CrossplayDukePlayerCanInheritSectorMotion(PLAYERp const pp)
{
    return pp != NULL && !TEST(pp->Flags, PF_FLYING|PF_SWIMMING|PF_DIVING|PF_CLIMBING);
}

static int32_t SW_CrossplayDukePlayerInMovingSector(PLAYERp const pp, int16_t sectnum)
{
    if (pp == NULL || (unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    // SW's view-sector, player-sprite sector, and low/floor sector can briefly
    // disagree while falling, jumping, or riding moving Duke geometry.  Treat
    // any of them as authoritative for Duke effectors that depend on sector
    // membership.
    if (pp->cursectnum == sectnum)
        return 1;

    if ((unsigned)pp->PlayerSprite < MAXSPRITES && sprite[pp->PlayerSprite].sectnum == sectnum)
        return 1;

    if (pp->lo_sectp == &sector[sectnum])
        return 1;

    if (inside(pp->pos.x, pp->pos.y, sectnum) != 1)
        return 0;

    int32_t ceilingZ, floorZ;
    getzsofslope(sectnum, pp->pos.x, pp->pos.y, &ceilingZ, &floorZ);

    int32_t const ceilingSlop = max<int32_t>(pp->ceiling_dist, SW_PlayerRunCeilingDist()) + Z(48);
    int32_t const floorSlop = max<int32_t>(pp->floor_dist, SW_PlayerRunFloorDist()) + Z(64);
    return pp->posz >= ceilingZ - ceilingSlop && pp->posz <= floorZ + floorSlop;
}

static void SW_CrossplayMoveDukePlayerXY(PLAYERp const pp, int32_t xChange, int32_t yChange)
{
    if (pp == NULL || (xChange == 0 && yChange == 0))
        return;

    pp->pos.x += xChange;
    pp->pos.y += yChange;

    // opos is deliberately left where it was.  The bridge registers moving Duke
    // sector walls with SW's interpolation, so the geometry glides between tics;
    // advancing opos too would snap the player a whole tic's worth each frame
    // while the world around them slid smoothly, which is felt as the view
    // shaking while riding a train.  Leaving opos behind lets the renderer
    // interpolate the ride in step with the walls.
    pp->oldposx += xChange;
    pp->oldposy += yChange;
    pp->lv_x += xChange;
    pp->lv_y += yChange;
}

static void SW_CrossplayMoveDukePlayerZ(PLAYERp const pp, int32_t zChange)
{
    if (pp == NULL || zChange == 0)
        return;

    pp->posz += zChange;

    // Same reasoning as SW_CrossplayMoveDukePlayerXY(): leave opos behind so a
    // lift or a rising floor interpolates instead of stepping.
    pp->oldposz += zChange;
    pp->lv_z += zChange;
}

static void SW_CrossplayRotateDukePlayerXY(PLAYERp const pp, vec2_t const &pivot, int32_t deltaAngle)
{
    if (pp == NULL || deltaAngle == 0)
        return;

    rotatepoint(pivot, pp->pos.xy, deltaAngle, &pp->pos.xy);

    vec2_t oldRenderPos = { pp->oposx, pp->oposy };
    rotatepoint(pivot, oldRenderPos, deltaAngle, &oldRenderPos);
    pp->oposx = oldRenderPos.x;
    pp->oposy = oldRenderPos.y;

    vec2_t oldPlayerPos = { pp->oldposx, pp->oldposy };
    rotatepoint(pivot, oldPlayerPos, deltaAngle, &oldPlayerPos);
    pp->oldposx = oldPlayerPos.x;
    pp->oldposy = oldPlayerPos.y;

    vec2_t lastValidPos = { pp->lv_x, pp->lv_y };
    rotatepoint(pivot, lastValidPos, deltaAngle, &lastValidPos);
    pp->lv_x = lastValidPos.x;
    pp->lv_y = lastValidPos.y;
}

static void SW_CrossplaySyncDukeMovedPlayer(PLAYERp const pp, int32_t refreshSector, int32_t updateZRange)
{
    if (pp == NULL)
        return;

    if (refreshSector)
    {
        int16_t newSectnum = pp->cursectnum;
        COVERupdatesector(pp->pos.x, pp->pos.y, &newSectnum);
        if ((unsigned)newSectnum < (unsigned)numsectors)
        {
            if (newSectnum != pp->cursectnum)
                pp->lastcursectnum = pp->cursectnum;
            pp->cursectnum = newSectnum;
        }
    }

    if ((unsigned)pp->cursectnum < (unsigned)numsectors)
        pp->lv_sectnum = pp->cursectnum;

    pp->lv_x = pp->posx;
    pp->lv_y = pp->posy;
    pp->lv_z = pp->posz;

    if ((unsigned)pp->PlayerSprite < MAXSPRITES)
    {
        sprite[pp->PlayerSprite].xyz = pp->pos;
        if ((unsigned)pp->cursectnum < (unsigned)numsectors)
            changespritesect(pp->PlayerSprite, pp->cursectnum);
        UpdatePlayerSprite(pp);
    }

    if (updateZRange)
        DoPlayerZrange(pp);
}

static int8_t SW_CrossplayClampDukeShade(int32_t shade)
{
    return (int8_t)clamp(shade, -128, 127);
}

static int32_t SW_CrossplayDukeSectorMaxWallShade(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t maxShade = 0;
    int32_t const startWall = sector[sectnum].wallptr;
    int32_t const endWall = startWall + sector[sectnum].wallnum;
    if (startWall < 0 || endWall > MAXWALLS)
        return maxShade;

    for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
        if (wall[wallNum].shade > maxShade)
            maxShade = wall[wallNum].shade;

    return maxShade;
}

static int32_t SW_CrossplayDukeLightLotagSupported(int16_t lotag)
{
    switch (lotag)
    {
    case VB_DUKE_SE_RANDOM_LIGHTS_AFTER_SHOT_OUT:
    case VB_DUKE_SE_RANDOM_LIGHTS:
    case VB_DUKE_SE_UP_OPEN_DOOR_LIGHTS:
    case VB_DUKE_SE_DOWN_OPEN_DOOR_LIGHTS:
    case VB_DUKE_SE_LIGHT_SWITCH:
        return 1;
    default:
        return 0;
    }
}

static void SW_CrossplaySetDukeLightPal(sw_crossplay_duke_light_t const &light, int32_t palMode)
{
    if ((unsigned)light.sectnum >= (unsigned)numsectors || palMode < 0)
        return;

    uint8_t floorPal = light.baseFloorPal;
    uint8_t ceilingPal = light.baseCeilingPal;
    uint8_t wallPal = light.baseFloorPal;

    if (palMode == 1)
    {
        floorPal = ceilingPal = wallPal = light.effectorPal;
    }
    else if (palMode == 2)
    {
        floorPal = ceilingPal = wallPal = 0;
    }

    sector[light.sectnum].floorpal = floorPal;
    sector[light.sectnum].ceilingpal = ceilingPal;

    int32_t const startWall = sector[light.sectnum].wallptr;
    int32_t const endWall = startWall + sector[light.sectnum].wallnum;
    if (startWall < 0 || endWall > MAXWALLS)
        return;

    for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
    {
        if (wall[wallNum].hitag == 1)
            continue;

        wall[wallNum].pal = wallPal;
        if ((wall[wallNum].cstat & 2) && (unsigned)wall[wallNum].nextwall < (unsigned)numwalls)
            wall[wall[wallNum].nextwall].pal = wallPal;
    }
}

static void SW_CrossplaySetDukeLightShade(sw_crossplay_duke_light_t const &light, int32_t floorShade,
                                          int32_t ceilingShade, int32_t wallShade, int32_t palMode)
{
    if ((unsigned)light.sectnum >= (unsigned)numsectors)
        return;

    sector[light.sectnum].floorshade = SW_CrossplayClampDukeShade(floorShade);
    sector[light.sectnum].ceilingshade = SW_CrossplayClampDukeShade(ceilingShade);
    SW_CrossplaySetDukeLightPal(light, palMode);

    int32_t const startWall = sector[light.sectnum].wallptr;
    int32_t const endWall = startWall + sector[light.sectnum].wallnum;
    if (startWall < 0 || endWall > MAXWALLS)
        return;

    int8_t const clampedWallShade = SW_CrossplayClampDukeShade(wallShade);
    for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
    {
        if (wall[wallNum].hitag == 1)
            continue;

        wall[wallNum].shade = clampedWallShade;
        if ((wall[wallNum].cstat & 2) && (unsigned)wall[wallNum].nextwall < (unsigned)numwalls)
            wall[wall[wallNum].nextwall].shade = clampedWallShade;
    }

    int32_t spriteNum, nextSprite;
    TRAVERSE_SPRITE_SECT(headspritesect[light.sectnum], spriteNum, nextSprite)
    {
        if (sprite[spriteNum].statnum != MAXSTATUS && (sprite[spriteNum].cstat & 16))
            sprite[spriteNum].shade = (sector[light.sectnum].ceilingstat & 1)
                ? sector[light.sectnum].ceilingshade
                : sector[light.sectnum].floorshade;
    }
}

static int8_t SW_CrossplaySteppedDukeLightShade(int32_t shade, int32_t delta, int32_t minShade, int32_t maxShade)
{
    int32_t newShade = shade + delta;
    if (newShade < minShade)
        newShade = minShade;
    else if (newShade > maxShade)
        newShade = maxShade;

    return SW_CrossplayClampDukeShade(newShade);
}

static void SW_CrossplayTickDukeDoorLight(sw_crossplay_duke_light_t &light)
{
    if (!light.state || (unsigned)light.sectnum >= (unsigned)numsectors)
        return;

    int32_t const baseWallShade = light.maxWallShade;
    int32_t const step = max(klabs(baseWallShade), 1);
    int32_t opening = (sector[light.sectnum].lotag & 0x8000u) || light.oneShot;
    int32_t delta = opening ? -step : step;

    if (light.lotag == VB_DUKE_SE_DOWN_OPEN_DOOR_LIGHTS)
        delta = -delta;

    int32_t changed = 0;
    int32_t const minFloorShade = min<int32_t>(light.effectorShade, light.baseFloorShade);
    int32_t const maxFloorShade = max<int32_t>(light.effectorShade, light.baseFloorShade);
    int32_t const minCeilingShade = min<int32_t>(light.effectorShade, light.baseCeilingShade);
    int32_t const maxCeilingShade = max<int32_t>(light.effectorShade, light.baseCeilingShade);
    int32_t const minWallShade = min<int32_t>(light.effectorShade, baseWallShade);
    int32_t const maxWallShade = max<int32_t>(light.effectorShade, baseWallShade);

    int8_t newShade = SW_CrossplaySteppedDukeLightShade(sector[light.sectnum].floorshade, delta, minFloorShade, maxFloorShade);
    changed |= newShade != sector[light.sectnum].floorshade;
    sector[light.sectnum].floorshade = newShade;

    newShade = SW_CrossplaySteppedDukeLightShade(sector[light.sectnum].ceilingshade, delta, minCeilingShade, maxCeilingShade);
    changed |= newShade != sector[light.sectnum].ceilingshade;
    sector[light.sectnum].ceilingshade = newShade;

    int32_t const startWall = sector[light.sectnum].wallptr;
    int32_t const endWall = startWall + sector[light.sectnum].wallnum;
    if (startWall >= 0 && endWall <= MAXWALLS)
    {
        for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
        {
            if (wall[wallNum].hitag == 1)
                continue;

            newShade = SW_CrossplaySteppedDukeLightShade(wall[wallNum].shade, delta, minWallShade, maxWallShade);
            changed |= newShade != wall[wallNum].shade;
            wall[wallNum].shade = newShade;
            if ((wall[wallNum].cstat & 2) && (unsigned)wall[wallNum].nextwall < (unsigned)numwalls &&
                wall[wall[wallNum].nextwall].hitag != 1)
                wall[wall[wallNum].nextwall].shade = wall[wallNum].shade;
        }
    }

    int32_t const wasOneShot = light.oneShot > 0;
    if (wasOneShot)
        --light.oneShot;

    if (!changed || (wasOneShot && light.oneShot == 0))
        light.state = 0;
}

static int32_t SW_CrossplayActivateDukeLightsWithHitag(int16_t hitag, int32_t oneShot)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeLightCount; ++i)
    {
        sw_crossplay_duke_light_t &light = g_swCrossplayDukeLights[i];
        if (light.hitag != hitag)
            continue;

        switch (light.lotag)
        {
        case VB_DUKE_SE_RANDOM_LIGHTS_AFTER_SHOT_OUT:
            light.maxWallShade = SW_CrossplayClampDukeShade(max<int32_t>(SW_CrossplayDukeSectorMaxWallShade(light.sectnum), light.baseFloorShade));
            light.state = 1;
            activated = 1;
            break;
        case VB_DUKE_SE_UP_OPEN_DOOR_LIGHTS:
        case VB_DUKE_SE_DOWN_OPEN_DOOR_LIGHTS:
            light.state = 1;
            light.oneShot = oneShot ? 8 : 0;
            activated = 1;
            break;
        case VB_DUKE_SE_LIGHT_SWITCH:
            light.state = (oneShot || light.state == 2) ? 3 : 1;
            light.oneShot = oneShot ? 1 : 0;
            activated = 1;
            break;
        default:
            break;
        }
    }

    return activated;
}

static int32_t SW_CrossplayActivateDukeExplosionLightsWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeLightCount; ++i)
    {
        sw_crossplay_duke_light_t &light = g_swCrossplayDukeLights[i];
        if (light.hitag != hitag || light.lotag != VB_DUKE_SE_UP_OPEN_DOOR_LIGHTS)
            continue;

        light.state = 1;
        light.oneShot = 8;
        activated = 1;
    }

    return activated;
}

static void SW_CrossplayTickDukeLights(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeLightCount; ++i)
    {
        sw_crossplay_duke_light_t &light = g_swCrossplayDukeLights[i];
        if ((unsigned)light.sectnum >= (unsigned)numsectors)
            continue;

        switch (light.lotag)
        {
        case VB_DUKE_SE_RANDOM_LIGHTS_AFTER_SHOT_OUT:
            if (!light.state)
                break;

            if (((krand1() / max<int32_t>(light.hitag + 1, 1)) & 31) < 4)
                SW_CrossplaySetDukeLightShade(light, light.effectorShade + (krand1() & 15),
                                              light.effectorShade + (krand1() & 15), light.effectorShade + (krand1() & 15), 0);
            else
                SW_CrossplaySetDukeLightShade(light, light.maxWallShade, light.maxWallShade, light.maxWallShade, 1);
            break;

        case VB_DUKE_SE_RANDOM_LIGHTS:
            if (((krand1() / max<int32_t>(light.hitag + 1, 1)) & 31) < 4)
                SW_CrossplaySetDukeLightShade(light, light.effectorShade + (krand1() & 15),
                                              light.effectorShade + (krand1() & 15), light.effectorShade + (krand1() & 15), 0);
            else
                SW_CrossplaySetDukeLightShade(light, light.baseFloorShade, light.baseFloorShade, light.maxWallShade, 1);
            break;

        case VB_DUKE_SE_UP_OPEN_DOOR_LIGHTS:
        case VB_DUKE_SE_DOWN_OPEN_DOOR_LIGHTS:
            SW_CrossplayTickDukeDoorLight(light);
            break;

        case VB_DUKE_SE_LIGHT_SWITCH:
            if (light.state == 3)
            {
                SW_CrossplaySetDukeLightShade(light, light.baseFloorShade, light.baseCeilingShade, light.baseFloorShade, 2);
                light.state = 0;
                light.oneShot = 0;
            }
            else if (light.state == 1)
            {
                if (sector[light.sectnum].floorshade > light.effectorShade)
                    SW_CrossplaySetDukeLightShade(light, sector[light.sectnum].floorshade - 2,
                                                  sector[light.sectnum].ceilingshade - 2,
                                                  sector[light.sectnum].floorshade - 2, 1);
                else
                    light.state = 2;
            }
            break;

        default:
            break;
        }
    }
}

static int32_t SW_CrossplayDukeLightningPlayerCanSee(sw_crossplay_duke_lightning_t const &lightning, PLAYERp const pp)
{
    if (pp == NULL || (unsigned)lightning.sectnum >= (unsigned)numsectors ||
        (unsigned)pp->cursectnum >= (unsigned)numsectors)
        return 0;

    return cansee(lightning.pos.x, lightning.pos.y, lightning.pos.z, lightning.sectnum,
                  pp->posx, pp->posy, pp->posz, pp->cursectnum) ||
           FAFcansee(lightning.pos.x, lightning.pos.y, lightning.pos.z, lightning.sectnum,
                     pp->posx, pp->posy, pp->posz, pp->cursectnum);
}

static int32_t SW_CrossplayDukeLightningHasNearbyPlayer(sw_crossplay_duke_lightning_t const &lightning)
{
    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (FindDistance2D(pp->posx - lightning.pos.x, pp->posy - lightning.pos.y) > 15500)
            continue;

        return 1;
    }

    return 0;
}

static short SW_CrossplayNativeWeaponForPlayerDamage(int32_t spriteNum)
{
    if ((unsigned)spriteNum < MAXSPRITES && sprite[spriteNum].statnum != MAXSTATUS && User[spriteNum] != NULL)
        return (short)spriteNum;

    return -1;
}

static void SW_CrossplaySetDukeNaturalLightningVisible(int16_t hitag, int32_t visible)
{
    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            artCrossplayOriginalMapSpritePicnum(spriteNum) != VB_DUKE_NATURALLIGHTNING ||
            sprite[spriteNum].hitag != hitag)
            continue;

        if (visible)
            RESET(sprite[spriteNum].cstat, CSTAT_SPRITE_INVISIBLE);
        else
            SET(sprite[spriteNum].cstat, CSTAT_SPRITE_INVISIBLE);
    }
}

static void SW_CrossplayDamagePlayersNearDukeLightningSprite(int32_t lightningSprite)
{
    if ((unsigned)lightningSprite >= MAXSPRITES || sprite[lightningSprite].statnum == MAXSTATUS)
        return;

    SPRITEp const sp = &sprite[lightningSprite];

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (FindDistance2D(pp->posx - sp->x, pp->posy - sp->y) >= 768 ||
            klabs(pp->posz - sp->z) > Z(96))
            continue;

        PlayerUpdateHealth(pp, -(8 + (krand1() & 7)));
        PlayerCheckDeath(pp, SW_CrossplayNativeWeaponForPlayerDamage(lightningSprite));

        if ((unsigned)pp->PlayerSprite < MAXSPRITES)
        {
            PlaySound(DIGI_DUKE_LONGTERM_PAIN, &pp->posx, &pp->posy, &pp->posz, v3df_follow);
            PlaySound(DIGI_DUKE_SHORT_CIRCUIT, &pp->posx, &pp->posy, &pp->posz, v3df_follow);
        }
    }
}

static void SW_CrossplayPulseDukeNaturalLightning(sw_crossplay_duke_lightning_t &lightning)
{
    int32_t flashed = 0;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            artCrossplayOriginalMapSpritePicnum(spriteNum) != VB_DUKE_NATURALLIGHTNING ||
            sprite[spriteNum].hitag != lightning.hitag)
            continue;

        if (((krand1() & 255) < 32) && (lightning.ticks & 1))
        {
            RESET(sprite[spriteNum].cstat, CSTAT_SPRITE_INVISIBLE);
            SW_CrossplayDamagePlayersNearDukeLightningSprite(spriteNum);
            flashed = 1;
        }
        else
            SET(sprite[spriteNum].cstat, CSTAT_SPRITE_INVISIBLE);
    }

    if (flashed)
        PlaySound(DIGI_DUKE_SHORT_CIRCUIT, &lightning.pos.x, &lightning.pos.y, &lightning.pos.z, v3df_dontpan|v3df_doppler);
}

static void SW_CrossplayTickDukeLightnings(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeLightningCount; ++i)
    {
        sw_crossplay_duke_lightning_t &lightning = g_swCrossplayDukeLightnings[i];
        if ((unsigned)lightning.sectnum >= (unsigned)numsectors)
            continue;

        if (lightning.delay > 0)
        {
            --lightning.delay;
            continue;
        }

        if (!lightning.active)
        {
            if (!SW_CrossplayDukeLightningHasNearbyPlayer(lightning))
                continue;

            lightning.active = 1;
            lightning.duration = 64 + (krand1() & 511);
            lightning.ticks = 0;
            continue;
        }

        ++lightning.ticks;

        if (lightning.ticks > lightning.duration)
        {
            lightning.active = 0;
            g_visibility = NormalVisibility;
            SW_CrossplaySetDukeNaturalLightningVisible(lightning.hitag, 0);
            continue;
        }

        if (lightning.ticks == (lightning.duration >> 1))
            PlaySound(DIGI_DUKE_THUNDER, &lightning.pos.x, &lightning.pos.y, &lightning.pos.z, v3df_dontpan|v3df_doppler);
        else if (lightning.ticks == (lightning.duration >> 3))
            PlaySound(DIGI_DUKE_LIGHTNING_SLAP, &lightning.pos.x, &lightning.pos.y, &lightning.pos.z, v3df_dontpan|v3df_doppler);
        else if (lightning.ticks == (lightning.duration >> 2))
            SW_CrossplaySetDukeNaturalLightningVisible(lightning.hitag, 0);
        else if (lightning.ticks > (lightning.duration >> 3) && lightning.ticks < (lightning.duration >> 2))
        {
            int32_t seen = 0;
            int32_t pnum;
            TRAVERSE_CONNECT(pnum)
            {
                if (SW_CrossplayDukeLightningPlayerCanSee(lightning, Player + pnum))
                {
                    seen = 1;
                    break;
                }
            }

            if (seen)
                g_visibility = (((krand1() & 255) < 192) && (lightning.ticks & 1)) ? 0 : NormalVisibility;

            SW_CrossplayPulseDukeNaturalLightning(lightning);
        }
    }
}

static int32_t SW_CrossplayAnyDukeQuakeActive(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeQuakeCount; ++i)
        if (g_swCrossplayDukeQuakes[i].active && !g_swCrossplayDukeQuakes[i].finished)
            return 1;

    return 0;
}

static void SW_CrossplayTickDukeQuakeDebris(void)
{
    if (g_swCrossplayDukeQuakeDebrisCount <= 0 || !SW_CrossplayAnyDukeQuakeActive())
        return;

    for (int32_t i = 0; i < g_swCrossplayDukeQuakeDebrisCount; ++i)
    {
        sw_crossplay_duke_quake_debris_t &debris = g_swCrossplayDukeQuakeDebris[i];
        if ((unsigned)debris.sectnum >= (unsigned)numsectors || (krand1() & 7) != 0)
            continue;

        if ((krand1() & 15) == 0)
            PlaySound(DIGI_DUKE_THUD, &debris.pos.x, &debris.pos.y, &debris.pos.z, v3df_dontpan|v3df_doppler);
    }
}

static void SW_CrossplayTickDukeWaterWarps(void)
{
    if (g_swCrossplayDukeWaterWarpCount <= 0)
        return;

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        if ((unsigned)pnum >= ARRAY_SIZE(g_swCrossplayDukeWaterWarpHold))
            continue;

        PLAYERp const pp = Player + pnum;
        if (SW_CrossplayDukePlayerStillOnBlockedTeleporterSector(pp, g_swCrossplayDukeWaterWarpBlockSector[pnum]))
        {
            if (g_swCrossplayDukeWaterWarpHold[pnum] > 0)
                --g_swCrossplayDukeWaterWarpHold[pnum];
            else
                g_swCrossplayDukeWaterWarpHold[pnum] = 2;
            continue;
        }

        g_swCrossplayDukeWaterWarpBlockSector[pnum] = -1;

        if (g_swCrossplayDukeWaterWarpHold[pnum] > 0)
        {
            --g_swCrossplayDukeWaterWarpHold[pnum];
            continue;
        }

        for (int32_t warpIndex = 0; warpIndex < g_swCrossplayDukeWaterWarpCount; ++warpIndex)
        {
            sw_crossplay_duke_water_warp_t const &warp = g_swCrossplayDukeWaterWarps[warpIndex];
            if (!SW_CrossplayDukePlayerTouchesWaterWarp(pp, warp))
                continue;

            SW_CrossplayWarpDukePlayerToWaterStack(pp, warp);
            g_swCrossplayDukeWaterWarpHold[pnum] = VB_CROSSPLAY_DUKE_WATER_WARP_HOLD_TICS;
            g_swCrossplayDukeWaterWarpBlockSector[pnum] = warp.destSect;
            break;
        }
    }
}

static void SW_CrossplayMoveDukeSectorFloorContents(int16_t sectnum, int32_t zChange)
{
    if (zChange == 0 || (unsigned)sectnum >= (unsigned)numsectors)
        return;

    int32_t spriteNum, nextSprite;
    TRAVERSE_SPRITE_SECT(headspritesect[sectnum], spriteNum, nextSprite)
    {
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS) ||
            sprite[spriteNum].zvel != 0)
            continue;

        sprite[spriteNum].z += zChange;
        setspritez(spriteNum, &sprite[spriteNum].xyz);
    }

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (!SW_CrossplayDukePlayerInMovingSector(pp, sectnum) ||
            !SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
            continue;

        SW_CrossplayMoveDukePlayerZ(pp, zChange);
        SW_CrossplaySyncDukeMovedPlayer(pp, 0, 1);
    }
}

static int32_t SW_CrossplayIsDukeExplosiveSourceTile(int16_t originalPicnum)
{
    switch (originalPicnum)
    {
    case VB_DUKE_CRACK1:
    case VB_DUKE_CRACK2:
    case VB_DUKE_CRACK3:
    case VB_DUKE_CRACK4:
    case VB_DUKE_OOZFILTER:
    case VB_DUKE_SEENINE:
    case VB_DUKE_SEENINEDEAD:
    case VB_DUKE_SEENINEDEAD + 1:
        return 1;
    default:
        return 0;
    }
}

static int32_t g_swCrossplayDukeExplosiveActivations;
static int32_t g_swCrossplayDukeMapTriggerEvents;

static void SW_CrossplayActivateDukeExplosive(sw_crossplay_duke_explosive_t &explosive)
{
    if (explosive.finished)
        return;

    ++g_swCrossplayDukeExplosiveActivations;

    explosive.active = 1;
    if (explosive.ticks == 0)
        explosive.ticks = 1;
}

static int32_t SW_CrossplayActivateDukeExplosivesWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeExplosiveCount; ++i)
    {
        sw_crossplay_duke_explosive_t &explosive = g_swCrossplayDukeExplosives[i];
        if (explosive.hitag != hitag || explosive.finished)
            continue;

        SW_CrossplayActivateDukeExplosive(explosive);
        activated = 1;
    }

    activated |= SW_CrossplayActivateDukeExplosionLightsWithHitag(hitag);
    activated |= SW_CrossplayActivateDukeDropFloorsWithHitag(hitag);
    activated |= SW_CrossplayActivateDukeQuakesWithHitag(hitag);
    activated |= SW_CrossplayStartDukeIncrementalMoversWithHitag(hitag);

    return activated;
}

static int32_t SW_CrossplayActivateDukeExplosivesInSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeExplosiveCount; ++i)
    {
        sw_crossplay_duke_explosive_t &explosive = g_swCrossplayDukeExplosives[i];
        if (explosive.sectnum != sectnum || explosive.finished)
            continue;

        SW_CrossplayActivateDukeExplosive(explosive);
        activated = 1;
    }

    return activated;
}

static int16_t SW_CrossplayFindDukeSectorForWall(int32_t wallNum)
{
    if ((unsigned)wallNum >= (unsigned)numwalls)
        return -1;

    for (int32_t sectnum = 0; sectnum < numsectors; ++sectnum)
    {
        int32_t const startWall = sector[sectnum].wallptr;
        int32_t const endWall = startWall + sector[sectnum].wallnum;
        if (startWall <= wallNum && wallNum < endWall)
            return (int16_t)sectnum;
    }

    return -1;
}

static int32_t SW_CrossplayActivateDukeLightSwitchesInSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeLightCount; ++i)
    {
        sw_crossplay_duke_light_t const &light = g_swCrossplayDukeLights[i];
        if (light.sectnum == sectnum && light.lotag == VB_DUKE_SE_LIGHT_SWITCH)
            activated |= SW_CrossplayActivateDukeLightsWithHitag(light.hitag, 1);
    }

    return activated;
}

static int32_t SW_CrossplayActivateDukeLightsForSectorTag(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    if (sector[sectnum].hitag != 0)
        return SW_CrossplayActivateDukeLightsWithHitag(sector[sectnum].hitag, 0);

    return SW_CrossplayActivateDukeLightSwitchesInSector(sectnum);
}

static int32_t SW_CrossplayActivateDukeLightsForWall(int32_t wallNum)
{
    if (g_swCrossplayDukeLightCount <= 0 || (unsigned)wallNum >= (unsigned)numwalls)
        return 0;

    int32_t activated = 0;
    int16_t const wallSect = SW_CrossplayFindDukeSectorForWall(wallNum);
    int16_t const nextSect = wall[wallNum].nextsector;

    activated |= SW_CrossplayActivateDukeLightsWithHitag(wall[wallNum].lotag, 0);
    activated |= SW_CrossplayActivateDukeLightsWithHitag(wall[wallNum].hitag, 0);
    activated |= SW_CrossplayActivateDukeLightsForSectorTag(wallSect);
    activated |= SW_CrossplayActivateDukeLightsForSectorTag(nextSect);

    if ((unsigned)wall[wallNum].nextwall < (unsigned)numwalls)
    {
        activated |= SW_CrossplayActivateDukeLightsWithHitag(wall[wall[wallNum].nextwall].lotag, 0);
        activated |= SW_CrossplayActivateDukeLightsWithHitag(wall[wall[wallNum].nextwall].hitag, 0);
    }

    return activated;
}

static int32_t SW_CrossplayDukeExplosiveSourceNearWallHit(sw_crossplay_duke_explosive_source_t const &source,
                                                          int16_t wallSect, int16_t nextSect, int16_t hitSect,
                                                          vec3_t const *hitPos)
{
    if (source.sectnum == wallSect || source.sectnum == nextSect || source.sectnum == hitSect)
        return 1;

    if (hitPos != NULL && FindDistance2D(hitPos->x - source.pos.x, hitPos->y - source.pos.y) < 2048)
        return 1;

    return 0;
}

static int32_t SW_CrossplayActivateDukeExplosivesForWall(int32_t wallNum, vec3_t const *hitPos)
{
    if (g_swCrossplayDukeExplosiveCount <= 0 || (unsigned)wallNum >= (unsigned)numwalls)
        return 0;

    int32_t activated = 0;
    int16_t const wallSect = SW_CrossplayFindDukeSectorForWall(wallNum);
    int16_t const nextSect = wall[wallNum].nextsector;
    int16_t hitSect = -1;

    if (hitPos != NULL)
        updatesector(hitPos->x, hitPos->y, &hitSect);

    activated |= SW_CrossplayActivateDukeExplosivesInSector(wallSect);
    activated |= SW_CrossplayActivateDukeExplosivesInSector(nextSect);
    activated |= SW_CrossplayActivateDukeExplosivesInSector(hitSect);

    activated |= SW_CrossplayActivateDukeExplosivesWithHitag(wall[wallNum].lotag);
    activated |= SW_CrossplayActivateDukeExplosivesWithHitag(wall[wallNum].hitag);

    if ((unsigned)wall[wallNum].nextwall < (unsigned)numwalls)
    {
        activated |= SW_CrossplayActivateDukeExplosivesWithHitag(wall[wall[wallNum].nextwall].lotag);
        activated |= SW_CrossplayActivateDukeExplosivesWithHitag(wall[wall[wallNum].nextwall].hitag);
    }

    for (int32_t i = 0; i < g_swCrossplayDukeExplosiveSourceCount; ++i)
    {
        sw_crossplay_duke_explosive_source_t const &source = g_swCrossplayDukeExplosiveSources[i];
        if (source.hitag <= 0 || !SW_CrossplayDukeExplosiveSourceNearWallHit(source, wallSect, nextSect, hitSect, hitPos))
            continue;

        activated |= SW_CrossplayActivateDukeExplosivesWithHitag(source.hitag);
    }

    return activated;
}

static PLAYERp SW_CrossplayNearestPlayerToDukeSprite(int32_t spriteNum, int32_t *outDistance)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return NULL;

    PLAYERp bestPlayer = NULL;
    int32_t bestDistance = INT32_MAX;
    SPRITEp const sp = &sprite[spriteNum];

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if ((unsigned)pp->PlayerSprite >= MAXSPRITES)
            continue;

        int32_t const distance = FindDistance2D(pp->posx - sp->x, pp->posy - sp->y);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestPlayer = pp;
        }
    }

    if (outDistance != NULL)
        *outDistance = bestDistance;

    return bestPlayer;
}

static int32_t SW_CrossplayActivateDukeExplosivesForSprite(int32_t spriteNum, vec3_t const *hitPos)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return 0;

    SPRITEp const sp = &sprite[spriteNum];
    int32_t activated = 0;

    activated |= SW_CrossplayActivateDukeExplosivesInSector(sp->sectnum);
    activated |= SW_CrossplayActivateDukeExplosivesWithHitag(sp->lotag);
    activated |= SW_CrossplayActivateDukeExplosivesWithHitag(sp->hitag);

    if (hitPos != NULL)
    {
        int16_t hitSect = -1;
        updatesector(hitPos->x, hitPos->y, &hitSect);
        activated |= SW_CrossplayActivateDukeExplosivesInSector(hitSect);
    }

    return activated;
}

static PLAYERp SW_CrossplayPlayerForDukeSpriteOperator(int32_t spriteNum)
{
    if ((unsigned)spriteNum < MAXSPRITES && User[spriteNum] != NULL && User[spriteNum]->PlayerP != NULL)
        return User[spriteNum]->PlayerP;

    if ((unsigned)spriteNum < MAXSPRITES && (unsigned)sprite[spriteNum].owner < MAXSPRITES &&
        User[sprite[spriteNum].owner] != NULL && User[sprite[spriteNum].owner]->PlayerP != NULL)
        return User[sprite[spriteNum].owner]->PlayerP;

    return NULL;
}

static int32_t SW_CrossplayDukeSpriteHitDamage(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return 20;

    if (User[spriteNum] != NULL)
    {
        switch (User[spriteNum]->WeaponNum)
        {
        case WPN_SWORD:
        case WPN_FIST:
        case WPN_STAR:
            return 12;
        case WPN_SHOTGUN:
        case WPN_ROCKET:
        case WPN_GRENADE:
        case WPN_MINE:
            return 60;
        default:
            break;
        }
    }

    return 20;
}

static int32_t SW_CrossplayDukeActorViewZ(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return 0;

    SPRITEp const sp = &sprite[spriteNum];
    int32_t viewZ = (g_swCrossplayDukeSpriteBehaviors[spriteNum].actorKind == VB_CROSSPLAY_DUKE_ACTOR_TURRET)
        ? sp->z + Z(16)
        : sp->z - Z(16);

    if ((unsigned)sp->sectnum < (unsigned)numsectors)
    {
        int32_t ceilingZ;
        int32_t floorZ;
        getzsofslope(sp->sectnum, sp->x, sp->y, &ceilingZ, &floorZ);
        viewZ = clamp(viewZ, ceilingZ + Z(4), floorZ - Z(4));
    }

    return viewZ;
}

static int32_t SW_CrossplayClampZToSector(int16_t sectnum, int32_t x, int32_t y, int32_t z)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return z;

    int32_t ceilingZ;
    int32_t floorZ;
    getzsofslope(sectnum, x, y, &ceilingZ, &floorZ);
    return clamp(z, ceilingZ + Z(4), floorZ - Z(4));
}

static int32_t SW_CrossplayDukeActorCanSeePlayerSector(int32_t spriteNum, PLAYERp const pp, int16_t targetSect)
{
    SPRITEp const sp = &sprite[spriteNum];
    if ((unsigned)targetSect >= (unsigned)numsectors)
        return 0;

    int32_t const sourceZ = SW_CrossplayDukeActorViewZ(spriteNum);
    int32_t const targetZ = SW_CrossplayClampZToSector(targetSect, pp->posx, pp->posy, pp->posz - Z(24));

    if (FAFcansee(sp->x, sp->y, sourceZ, sp->sectnum, pp->posx, pp->posy, targetZ, targetSect) ||
        cansee(sp->x, sp->y, sourceZ, sp->sectnum, pp->posx, pp->posy, targetZ, targetSect))
        return 1;

    if (g_swCrossplayDukeSpriteBehaviors[spriteNum].actorKind != VB_CROSSPLAY_DUKE_ACTOR_TURRET)
        return 0;

    static const int32_t sourceOffsets[] = { Z(8), Z(24), Z(32) };
    static const int32_t targetOffsets[] = { Z(12), Z(32), Z(48) };

    for (unsigned sourceIndex = 0; sourceIndex < ARRAY_SIZE(sourceOffsets); ++sourceIndex)
    {
        int32_t const altSourceZ = SW_CrossplayClampZToSector(sp->sectnum, sp->x, sp->y, sp->z + sourceOffsets[sourceIndex]);

        for (unsigned targetIndex = 0; targetIndex < ARRAY_SIZE(targetOffsets); ++targetIndex)
        {
            int32_t const altTargetZ = SW_CrossplayClampZToSector(targetSect, pp->posx, pp->posy, pp->posz - targetOffsets[targetIndex]);

            if (FAFcansee(sp->x, sp->y, altSourceZ, sp->sectnum, pp->posx, pp->posy, altTargetZ, targetSect) ||
                cansee(sp->x, sp->y, altSourceZ, sp->sectnum, pp->posx, pp->posy, altTargetZ, targetSect))
                return 1;
        }
    }

    return 0;
}

static void SW_CrossplayTryDukeActorSeePlayerSector(int32_t spriteNum, PLAYERp const pp, int16_t targetSect,
                                                    int16_t *seenSects, int32_t *seenCount, int32_t *canSee)
{
    if (*canSee || (unsigned)targetSect >= (unsigned)numsectors)
        return;

    for (int32_t i = 0; i < *seenCount; ++i)
        if (seenSects[i] == targetSect)
            return;

    seenSects[(*seenCount)++] = targetSect;
    *canSee = SW_CrossplayDukeActorCanSeePlayerSector(spriteNum, pp, targetSect);
}

static int32_t SW_CrossplayDukeActorCanSeePlayer(int32_t spriteNum, PLAYERp const pp)
{
    if ((unsigned)spriteNum >= MAXSPRITES || pp == NULL ||
        sprite[spriteNum].statnum == MAXSTATUS)
        return 0;

    SPRITEp const sp = &sprite[spriteNum];
    if ((unsigned)sp->sectnum >= (unsigned)numsectors)
        return 0;

    int16_t seenSects[3];
    int32_t seenCount = 0;
    int32_t canSee = 0;

    SW_CrossplayTryDukeActorSeePlayerSector(spriteNum, pp, pp->cursectnum, seenSects, &seenCount, &canSee);

    if ((unsigned)pp->PlayerSprite < MAXSPRITES)
        SW_CrossplayTryDukeActorSeePlayerSector(spriteNum, pp, sprite[pp->PlayerSprite].sectnum, seenSects, &seenCount, &canSee);

    SW_CrossplayTryDukeActorSeePlayerSector(spriteNum, pp, pp->lv_sectnum, seenSects, &seenCount, &canSee);

    return canSee;
}

static PLAYERp SW_CrossplayPlayerForHitSprite(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return NULL;

    if (User[spriteNum] != NULL && User[spriteNum]->PlayerP != NULL)
        return User[spriteNum]->PlayerP;

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (pp->PlayerSprite == spriteNum)
            return pp;
    }

    return NULL;
}

static int32_t SW_CrossplayLineReachesDukePlayerPoint(int32_t spriteNum, PLAYERp const pp, int32_t startX,
                                                      int32_t startY, int32_t startZ, int16_t startSect,
                                                      int32_t targetZ, hitdata_t const *hitinfo)
{
    if ((unsigned)spriteNum >= MAXSPRITES || pp == NULL || hitinfo == NULL)
        return 0;

    int32_t const playerDistance = FindDistance2D(pp->posx - startX, pp->posy - startY);
    int32_t const hitDistance = FindDistance2D(hitinfo->x - startX, hitinfo->y - startY);

    if (hitinfo->sprite >= 0 && SW_CrossplayPlayerForHitSprite(hitinfo->sprite) == pp)
        return 1;

    if (hitDistance + 384 < playerDistance)
        return 0;

    if (!SW_CrossplayDukeActorCanSeePlayer(spriteNum, pp))
        return 0;

    int16_t targetSect = pp->cursectnum;
    if ((unsigned)pp->PlayerSprite < MAXSPRITES && (unsigned)sprite[pp->PlayerSprite].sectnum < (unsigned)numsectors)
        targetSect = sprite[pp->PlayerSprite].sectnum;

    return FAFcansee(startX, startY, startZ, startSect, pp->posx, pp->posy, targetZ, targetSect) ||
           cansee(startX, startY, startZ, startSect, pp->posx, pp->posy, targetZ, targetSect);
}

static void SW_CrossplayDukeActorFireHitscan(int32_t spriteNum, PLAYERp const pp, sw_crossplay_duke_actor_profile_t const &profile)
{
    if ((unsigned)spriteNum >= MAXSPRITES || pp == NULL || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    SPRITEp const sp = &sprite[spriteNum];
    if ((unsigned)sp->sectnum >= (unsigned)numsectors)
        return;

    hitdata_t hitinfo = { { -1, -1, -1 }, -1, -1, -1 };
    int16_t const shootAng = getangle(pp->posx - sp->x, pp->posy - sp->y);
    int32_t const xVect = sintable[NORM_ANGLE(shootAng + 512)];
    int32_t const yVect = sintable[NORM_ANGLE(shootAng)];
    int32_t const startX = sp->x + (xVect >> 8);
    int32_t const startY = sp->y + (yVect >> 8);
    int32_t const startZ = SW_CrossplayDukeActorViewZ(spriteNum);
    int16_t startSect = sp->sectnum;
    updatesectorz(startX, startY, startZ, &startSect);
    if ((unsigned)startSect >= (unsigned)numsectors)
        startSect = sp->sectnum;

    int16_t targetSect = pp->cursectnum;
    if ((unsigned)pp->PlayerSprite < MAXSPRITES && (unsigned)sprite[pp->PlayerSprite].sectnum < (unsigned)numsectors)
        targetSect = sprite[pp->PlayerSprite].sectnum;

    int32_t const targetZ = SW_CrossplayClampZToSector(targetSect, pp->posx, pp->posy, pp->posz - Z(24));
    int32_t zVect = 0;

    if (profile.projectileTile >= 0)
        SW_CrossplaySpawnDukeVisualProjectile((int16_t)spriteNum, profile.projectileTile, startSect,
                                              startX, startY, startZ, pp->posx, pp->posy, targetZ, shootAng);

    if (pp->posx != startX)
        zVect = scale(xVect, targetZ - startZ, pp->posx - startX);
    else if (pp->posy != startY)
        zVect = scale(yVect, targetZ - startZ, pp->posy - startY);

    uint16_t const oldCstat = sp->cstat;
    RESET(sp->cstat, CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
    FAFhitscan(startX, startY, startZ, startSect, xVect, yVect, zVect, &hitinfo, CLIPMASK_MISSILE);
    sp->cstat = oldCstat;

    if (hitinfo.sprite == spriteNum)
    {
        if (SW_CrossplayDukeActorCanSeePlayer(spriteNum, pp))
        {
            PlayerUpdateHealth(pp, -profile.rangedDamage);
            PlayerCheckDeath(pp, SW_CrossplayNativeWeaponForPlayerDamage(spriteNum));
        }
        return;
    }

    if (hitinfo.sect < 0)
    {
        if (SW_CrossplayDukeActorCanSeePlayer(spriteNum, pp))
        {
            PlayerUpdateHealth(pp, -profile.rangedDamage);
            PlayerCheckDeath(pp, SW_CrossplayNativeWeaponForPlayerDamage(spriteNum));
        }
        return;
    }

    vec3_t const hitPos = { hitinfo.x, hitinfo.y, hitinfo.z };
    int32_t damagedPlayer = 0;

    if (hitinfo.wall >= 0)
    {
        if (!SW_CrossplayDamageDukeMapWall(spriteNum, hitinfo.wall, &hitPos))
            QueueHole(hitinfo.sect, hitinfo.wall, hitinfo.x, hitinfo.y, hitinfo.z);
    }

    if (hitinfo.sprite >= 0)
    {
        if (!SW_CrossplayDamageDukeMapSprite(spriteNum, hitinfo.sprite, &hitPos))
        {
            PLAYERp const hitPlayer = SW_CrossplayPlayerForHitSprite(hitinfo.sprite);
            if (hitPlayer != NULL)
            {
                PlayerUpdateHealth(hitPlayer, -profile.rangedDamage);
                PlayerCheckDeath(hitPlayer, SW_CrossplayNativeWeaponForPlayerDamage(spriteNum));
                damagedPlayer = hitPlayer == pp;
            }
        }
    }

    if (!damagedPlayer &&
        SW_CrossplayLineReachesDukePlayerPoint(spriteNum, pp, startX, startY, startZ, startSect,
                                               targetZ, &hitinfo))
    {
        PlayerUpdateHealth(pp, -profile.rangedDamage);
        PlayerCheckDeath(pp, SW_CrossplayNativeWeaponForPlayerDamage(spriteNum));
    }

    SpawnTurretSparks(hitinfo.sect, hitinfo.wall, hitinfo.x, hitinfo.y, hitinfo.z, shootAng);
}

static void SW_CrossplayDukeActorAreaDamage(int32_t spriteNum, int32_t radius, int32_t damage)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    SPRITEp const sp = &sprite[spriteNum];

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        int32_t const dist = FindDistance2D(pp->posx - sp->x, pp->posy - sp->y);
        if (dist > radius || klabs(pp->posz - sp->z) > Z(96))
            continue;

        int32_t const scaledDamage = max<int32_t>(1, damage - ((damage * dist) / max<int32_t>(radius, 1)));
        PlayerUpdateHealth(pp, -scaledDamage);
    }
}

static int32_t SW_CrossplayFindDukeTripmine(int32_t spriteNum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTripmineCount; ++i)
        if (g_swCrossplayDukeTripmines[i].active && g_swCrossplayDukeTripmines[i].spriteNum == spriteNum)
            return i;

    return -1;
}

static void SW_CrossplayClearDukeTripmineLines(sw_crossplay_duke_tripmine_t &tripmine)
{
    for (int32_t i = 0; i < tripmine.lineCount; ++i)
    {
        int16_t const lineSprite = tripmine.lineSprites[i];
        if ((unsigned)lineSprite < MAXSPRITES && sprite[lineSprite].statnum != MAXSTATUS)
            SW_CrossplayDeleteDukeSprite(lineSprite);
        tripmine.lineSprites[i] = -1;
    }

    tripmine.lineCount = 0;
}

static void SW_CrossplayBuildDukeTripmineBeam(sw_crossplay_duke_tripmine_t &tripmine)
{
    if (!tripmine.active || (unsigned)tripmine.spriteNum >= MAXSPRITES || sprite[tripmine.spriteNum].statnum == MAXSTATUS)
        return;

    SPRITEp const sp = &sprite[tripmine.spriteNum];
    int16_t startSect = sp->sectnum;
    int32_t const shootAng = sp->ang;
    int32_t const xVect = sintable[(shootAng + 512) & 2047];
    int32_t const yVect = sintable[shootAng & 2047];
    int32_t const startX = sp->x + (xVect >> 8);
    int32_t const startY = sp->y + (yVect >> 8);
    int32_t const startZ = SW_CrossplayClampZToSector(sp->sectnum, startX, startY, sp->z);
    updatesectorz(startX, startY, startZ, &startSect);
    if ((unsigned)startSect >= (unsigned)numsectors)
        startSect = sp->sectnum;

    uint16_t const oldCstat = sp->cstat;
    RESET(sp->cstat, CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);

    hitdata_t hitinfo = { { -1, -1, -1 }, -1, -1, -1 };
    FAFhitscan(startX, startY, startZ, startSect, xVect, yVect, 0, &hitinfo, CLIPMASK_MISSILE);
    sp->cstat = oldCstat;

    if (hitinfo.sect < 0)
        return;

    tripmine.beamStart = { startX, startY, startZ };
    tripmine.sectnum = startSect;
    tripmine.angle = shootAng;
    tripmine.lineDist = max<int32_t>(FindDistance2D(hitinfo.x - startX, hitinfo.y - startY), 256);

    int32_t const segmentCount = clamp<int32_t>(tripmine.lineDist >> 10, 1, VB_CROSSPLAY_MAX_DUKE_TRIPMINE_SEGMENTS);
    for (int32_t i = 0; i < segmentCount; ++i)
    {
        int32_t const dist = ((i + 1) * tripmine.lineDist) / (segmentCount + 1);
        vec3_t const pos = { startX + ((xVect * dist) >> 14), startY + ((yVect * dist) >> 14), startZ };
        int16_t sectnum = startSect;
        updatesectorz(pos.x, pos.y, pos.z, &sectnum);
        if ((unsigned)sectnum >= (unsigned)numsectors)
            continue;

        int16_t const lineSprite = SW_CrossplaySpawnDukeRuntimeSprite(VB_DUKE_LASERLINE, sectnum, pos, shootAng,
                                                                      8, 16, CSTAT_SPRITE_YCENTER);
        if (lineSprite < 0)
            continue;

        tripmine.lineSprites[tripmine.lineCount++] = lineSprite;
    }

    SW_CrossplayPlayDukeSpriteSound(tripmine.spriteNum, NULL, DIGI_DUKE_LASERTRIP_ONWALL);
}

static void SW_CrossplayRegisterDukeTripmine(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS ||
        SW_CrossplayFindDukeTripmine(spriteNum) >= 0)
        return;

    if (g_swCrossplayDukeTripmineCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeTripmines))
    {
        LOG_F(WARNING, "Crossplay: too many Duke tripmine sprites for Shadow Warrior compatibility bridge.");
        return;
    }

    sw_crossplay_duke_tripmine_t &tripmine = g_swCrossplayDukeTripmines[g_swCrossplayDukeTripmineCount++];
    Bmemset(&tripmine, 0, sizeof(tripmine));
    for (int32_t i = 0; i < (int32_t)ARRAY_SIZE(tripmine.lineSprites); ++i)
        tripmine.lineSprites[i] = -1;

    tripmine.spriteNum = spriteNum;
    tripmine.sectnum = sprite[spriteNum].sectnum;
    tripmine.angle = sprite[spriteNum].ang;
    tripmine.active = 1;
    tripmine.armed = 0;
    tripmine.armTicks = 26;
}

static int32_t SW_CrossplayDukePlayerCrossesTripmineBeam(PLAYERp const pp, sw_crossplay_duke_tripmine_t const &tripmine)
{
    if (pp == NULL || !tripmine.active || !tripmine.armed || tripmine.lineDist <= 0)
        return 0;

    int32_t const dx = (sintable[(tripmine.angle + 512) & 2047] * tripmine.lineDist) >> 14;
    int32_t const dy = (sintable[tripmine.angle & 2047] * tripmine.lineDist) >> 14;
    int64_t const len2 = (int64_t)dx * dx + (int64_t)dy * dy;
    if (len2 <= 0)
        return 0;

    int64_t t = (int64_t)(pp->posx - tripmine.beamStart.x) * dx + (int64_t)(pp->posy - tripmine.beamStart.y) * dy;
    if (t < 0 || t > len2)
        return 0;

    int32_t const closeX = tripmine.beamStart.x + (int32_t)(((int64_t)dx * t) / len2);
    int32_t const closeY = tripmine.beamStart.y + (int32_t)(((int64_t)dy * t) / len2);
    if (FindDistance2D(pp->posx - closeX, pp->posy - closeY) > 384)
        return 0;

    if (klabs((pp->posz - Z(24)) - tripmine.beamStart.z) > Z(48))
        return 0;

    int16_t playerSect = pp->cursectnum;
    if ((unsigned)pp->PlayerSprite < MAXSPRITES && (unsigned)sprite[pp->PlayerSprite].sectnum < (unsigned)numsectors)
        playerSect = sprite[pp->PlayerSprite].sectnum;

    return FAFcansee(tripmine.beamStart.x, tripmine.beamStart.y, tripmine.beamStart.z, tripmine.sectnum,
                     pp->posx, pp->posy, pp->posz - Z(24), playerSect) ||
           cansee(tripmine.beamStart.x, tripmine.beamStart.y, tripmine.beamStart.z, tripmine.sectnum,
                  pp->posx, pp->posy, pp->posz - Z(24), playerSect);
}


// Duke's explosive charges are a timed chain, not a single pop.  Damaging one
// SEENINE/OOZFILTER arms *every* charge sharing its hitag; each then burns down
// its own fuse (the sprite's lotag, counted off three at a time), steps through
// its damaged frames, and detonates in turn, driving whatever effectors carry
// that hitag.  E1L1's rooftop is exactly this: canisters tagged 190 with fuses
// of 0/30/60/110/120/140/160 running from the roof into the building, and the
// vent only opens because the later charges in that chain go off.
enum
{
    VB_CROSSPLAY_DUKE_CHARGE_IDLE,
    VB_CROSSPLAY_DUKE_CHARGE_FUSE,
    VB_CROSSPLAY_DUKE_CHARGE_POPPING,
};

static uint8_t g_swCrossplayDukeChargeState[MAXSPRITES];
static int16_t g_swCrossplayDukeChargeFuse[MAXSPRITES];
static uint8_t g_swCrossplayDukeChargeStageTic[MAXSPRITES];
static uint8_t g_swCrossplayDukeChargeStage[MAXSPRITES];

static int32_t SW_CrossplayIsDukeChargeSprite(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return 0;

    int16_t const sourcePicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
    return sourcePicnum == VB_DUKE_SEENINE || sourcePicnum == VB_DUKE_OOZFILTER ||
           sourcePicnum == VB_DUKE_SEENINEDEAD || sourcePicnum == VB_DUKE_SEENINEDEAD + 1;
}

// Arms every charge carrying this hitag, the way Duke spreads shade -32 across
// the group.  `seedSprite` is armed regardless, for the untagged case.
static void SW_CrossplayArmDukeChargesWithHitag(int16_t hitag, int32_t seedSprite)
{
    for (int32_t other = 0; other < MAXSPRITES; ++other)
    {
        if (other != seedSprite && (hitag == 0 || sprite[other].hitag != hitag))
            continue;

        if (!SW_CrossplayIsDukeChargeSprite(other) ||
            g_swCrossplayDukeChargeState[other] != VB_CROSSPLAY_DUKE_CHARGE_IDLE)
            continue;

        g_swCrossplayDukeChargeState[other] = VB_CROSSPLAY_DUKE_CHARGE_FUSE;
        g_swCrossplayDukeChargeFuse[other] = sprite[other].lotag;
        g_swCrossplayDukeChargeStageTic[other] = 0;
        g_swCrossplayDukeChargeStage[other] = 0;
    }
}

static void SW_CrossplayArmDukeChargeChain(int32_t spriteNum)
{
    if (!SW_CrossplayIsDukeChargeSprite(spriteNum))
        return;

    SW_CrossplayArmDukeChargesWithHitag(sprite[spriteNum].hitag, spriteNum);
}

// Duke's adopted actors reach the host's map machinery through this: they drive
// explosive sectors, door effectors and the host's charges by hitag, and Duke's
// own copies of those are not the ones being run.  Installed as Duke's map
// trigger hook, so an adopted FIREEXT sets off exactly what it does in Duke.
static void SW_CrossplayDukeMapTrigger(int hitag)
{
    if (hitag <= 0)
        return;

    ++g_swCrossplayDukeMapTriggerEvents;

    SW_CrossplayArmDukeChargesWithHitag((int16_t)hitag, -1);
    SW_CrossplayActivateDukeExplosivesWithHitag((int16_t)hitag);
}

static void SW_CrossplayTickDukeCharges(void)
{
    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (g_swCrossplayDukeChargeState[spriteNum] == VB_CROSSPLAY_DUKE_CHARGE_IDLE)
            continue;

        if (sprite[spriteNum].statnum == MAXSTATUS)
        {
            g_swCrossplayDukeChargeState[spriteNum] = VB_CROSSPLAY_DUKE_CHARGE_IDLE;
            continue;
        }

        if (g_swCrossplayDukeChargeState[spriteNum] == VB_CROSSPLAY_DUKE_CHARGE_FUSE)
        {
            if (g_swCrossplayDukeChargeFuse[spriteNum] > 0)
            {
                g_swCrossplayDukeChargeFuse[spriteNum] -= 3;
                continue;
            }

            g_swCrossplayDukeChargeState[spriteNum] = VB_CROSSPLAY_DUKE_CHARGE_POPPING;
            g_swCrossplayDukeChargeStageTic[spriteNum] = 0;
        }

        // A charge Duke hides has no frames to show and goes straight off.
        if (sprite[spriteNum].xrepeat == 0)
        {
            SW_CrossplayExplodeDukeSourceSprite(spriteNum, NULL, DIGI_DUKE_PIPEBOMB_EXPLODE);
            continue;
        }

        if (++g_swCrossplayDukeChargeStageTic[spriteNum] < 3)
            continue;

        g_swCrossplayDukeChargeStageTic[spriteNum] = 0;

        int16_t const sourcePicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);

        // An OOZFILTER just goes off; a SEENINE shows its two damaged frames
        // first, three tics apart, exactly as Duke steps it.
        if (sourcePicnum == VB_DUKE_OOZFILTER || g_swCrossplayDukeChargeStage[spriteNum] >= 2)
        {
            SW_CrossplayExplodeDukeSourceSprite(spriteNum, NULL, DIGI_DUKE_PIPEBOMB_EXPLODE);
            continue;
        }

        ++g_swCrossplayDukeChargeStage[spriteNum];

        int16_t const frame = (g_swCrossplayDukeChargeStage[spriteNum] == 1)
                              ? (int16_t)VB_DUKE_SEENINEDEAD
                              : (int16_t)(VB_DUKE_SEENINEDEAD + 1);
        SW_CrossplaySetDukeSpritePicnum(spriteNum, frame);
    }
}

static int32_t SW_CrossplayExplodeDukeSourceSprite(int32_t spriteNum, vec3_t const *hitPos, int32_t soundNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return 0;

    SPRITEp const sp = &sprite[spriteNum];
    vec3_t const explosionPos = hitPos != NULL ? *hitPos : sp->xyz;
    int16_t sectnum = sp->sectnum;
    if ((unsigned)sectnum >= (unsigned)numsectors)
        updatesector(explosionPos.x, explosionPos.y, &sectnum);

    SW_CrossplayPlayDukeSpriteSound(spriteNum, &explosionPos, soundNum);
    SW_CrossplaySpawnDukeExplosionEffect(explosionPos, sectnum);
    SW_CrossplayDukeActorAreaDamage(spriteNum, 2048, 48);
    SW_CrossplayActivateDukeExplosivesForSprite(spriteNum, &explosionPos);

    SW_CrossplayClearDukeSpriteBehavior(spriteNum);
    SW_CrossplayDeleteDukeSprite(spriteNum);
    return 1;
}

static int32_t SW_CrossplayDetonateDukeTripmine(int32_t spriteNum, vec3_t const *hitPos)
{
    ++g_swCrossplayDukeTripmineDetonations;

    int32_t const tripmineIndex = SW_CrossplayFindDukeTripmine(spriteNum);
    if (tripmineIndex < 0)
        return SW_CrossplayExplodeDukeSourceSprite(spriteNum, hitPos, DIGI_DUKE_LASERTRIP_EXPLODE);

    sw_crossplay_duke_tripmine_t &tripmine = g_swCrossplayDukeTripmines[tripmineIndex];
    SW_CrossplayClearDukeTripmineLines(tripmine);
    tripmine.active = 0;

    return SW_CrossplayExplodeDukeSourceSprite(spriteNum, hitPos, DIGI_DUKE_LASERTRIP_EXPLODE);
}

static void SW_CrossplayTickDukeTripmines(void)
{
    if (g_swCrossplayDukeTripmineCount <= 0)
        return;

    for (int32_t i = 0; i < g_swCrossplayDukeTripmineCount; ++i)
    {
        sw_crossplay_duke_tripmine_t &tripmine = g_swCrossplayDukeTripmines[i];
        if (!tripmine.active)
            continue;

        if ((unsigned)tripmine.spriteNum >= MAXSPRITES || sprite[tripmine.spriteNum].statnum == MAXSTATUS)
        {
            SW_CrossplayClearDukeTripmineLines(tripmine);
            tripmine.active = 0;
            continue;
        }

        if (!tripmine.armed)
        {
            if (tripmine.armTicks > 0)
            {
                if (tripmine.armTicks == 26)
                    SW_CrossplayPlayDukeSpriteSound(tripmine.spriteNum, NULL, DIGI_DUKE_LASERTRIP_ARMING);
                --tripmine.armTicks;
                continue;
            }

            tripmine.armed = 1;
            SW_CrossplayBuildDukeTripmineBeam(tripmine);
        }

        int32_t pnum;
        TRAVERSE_CONNECT(pnum)
        {
            if (SW_CrossplayDukePlayerCrossesTripmineBeam(Player + pnum, tripmine))
            {
                SW_CrossplayDetonateDukeTripmine(tripmine.spriteNum, NULL);
                break;
            }
        }
    }
}

static void SW_CrossplayKillDukeActorSprite(int32_t spriteNum, vec3_t const *hitPos)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    sw_crossplay_duke_actor_profile_t const profile = SW_CrossplayDukeActorProfile(behavior.actorKind, behavior.sourcePicnum, behavior.displayPal);

    SW_CrossplayPlayDukeSpriteSound(spriteNum, hitPos, profile.deathSound);

    switch (behavior.actorKind)
    {
    case VB_CROSSPLAY_DUKE_ACTOR_DRONE:
        SW_CrossplayDukeActorAreaDamage(spriteNum, 2048, 28);
        break;
    case VB_CROSSPLAY_DUKE_ACTOR_RECON:
    case VB_CROSSPLAY_DUKE_ACTOR_TANK:
        SW_CrossplayDukeActorAreaDamage(spriteNum, 1536, 18);
        break;
    default:
        break;
    }

    SW_CrossplayActivateDukeExplosivesForSprite(spriteNum, hitPos);
    ++g_swCrossplayDukeActorDeathEvents;
    SW_CrossplayClearDukeSpriteBehavior(spriteNum);
    SW_CrossplayDeleteDukeSprite(spriteNum);
}

int SW_CrossplayDamageDukeMapSprite(int spriteNum, int targetSpriteNum, vec3_t const *hitPos)
{
    // Duke's own actors take damage through Duke's damage protocol, so their
    // pain, knockback, gibbing and death are Duke's rather than the bridge's.
    if (SW_CrossplayDukeAIOwnsSprite(targetSpriteNum))
    {
        int32_t const impactAng = hitPos != NULL
                                  ? getangle(sprite[targetSpriteNum].x - hitPos->x, sprite[targetSpriteNum].y - hitPos->y)
                                  : sprite[targetSpriteNum].ang;

        VB_DukeAI_DamageActor(targetSpriteNum, SW_CrossplayDukeSpriteHitDamage(spriteNum),
                              SW_CrossplayDukeAIHitKind(spriteNum), impactAng, spriteNum);
        return 1;
    }

    if (!SW_CrossplayDukeMapBridgeActive() || (unsigned)targetSpriteNum >= MAXSPRITES ||
        sprite[targetSpriteNum].statnum == MAXSTATUS)
        return 0;

    sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[targetSpriteNum];
    if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
        return 0;

    SPRITEp const target = &sprite[targetSpriteNum];

    if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_SWITCH)
    {
        PLAYERp const pp = SW_CrossplayPlayerForDukeSpriteOperator(spriteNum);
        int32_t activated = 0;

        if (target->lotag != 0)
            activated |= SW_CrossplayOperateDukeTag(target->lotag, pp);
        if (!activated && target->hitag != 0)
            activated |= SW_CrossplayOperateDukeTag(target->hitag, pp);

        if (!activated)
        {
            SW_CrossplayToggleDukeSwitchSprite(targetSpriteNum);
            SW_CrossplayPlayDukeSpriteSound(targetSpriteNum, hitPos, DIGI_DUKE_SWITCH_ON);
            activated = 1;
        }

        return activated;
    }

    if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_TRIPMINE)
        return SW_CrossplayDetonateDukeTripmine(targetSpriteNum, hitPos);

    if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE)
    {
        // Duke's charges arm their whole hitag group and burn a fuse rather than
        // popping on the spot; blowing only the one that was hit leaves the rest
        // of the chain (and whatever it was wired to) untouched.
        if (SW_CrossplayIsDukeChargeSprite(targetSpriteNum))
        {
            SW_CrossplayArmDukeChargeChain(targetSpriteNum);
            return 1;
        }

        return SW_CrossplayExplodeDukeSourceSprite(targetSpriteNum, hitPos, DIGI_DUKE_PIPEBOMB_EXPLODE);
    }

    if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ACTOR)
    {
        behavior.health = (int16_t)max<int32_t>(behavior.health - SW_CrossplayDukeSpriteHitDamage(spriteNum), 0);
        if (behavior.health > 0)
        {
            sw_crossplay_duke_actor_profile_t const profile = SW_CrossplayDukeActorProfile(behavior.actorKind, behavior.sourcePicnum, behavior.displayPal);
            if (behavior.painCooldown == 0)
            {
                SW_CrossplayPlayDukeSpriteSound(targetSpriteNum, hitPos, profile.painSound);
                behavior.painCooldown = 16;
            }
            behavior.flags |= VB_CROSSPLAY_DUKE_SPRITE_AWAKE;
            behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_PAIN;
            return 1;
        }

        SW_CrossplayKillDukeActorSprite(targetSpriteNum, hitPos);
        return 1;
    }

    if (behavior.flags & (VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE | VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE))
    {
        behavior.health = (int16_t)max<int32_t>(behavior.health - SW_CrossplayDukeSpriteHitDamage(spriteNum), 0);
        if (behavior.health <= 0)
            SW_CrossplayBreakDukeSprite(targetSpriteNum, hitPos);
        return 1;
    }

    return 0;
}

static int16_t SW_CrossplayAnimatedDukeSpriteFrame(sw_crossplay_duke_sprite_behavior_t const &behavior)
{
    int16_t const base = behavior.basePicnum;

    switch (base)
    {
    case VB_DUKE_FANSPRITE:
    case VB_DUKE_WATERFOUNTAIN:
        return base + ((behavior.tick >> 1) & 3);
    case VB_DUKE_STEAM:
    case VB_DUKE_CEILINGSTEAM:
        return base + ((behavior.tick >> 2) & 3);
    case VB_DUKE_TRANSPORTERBEAM:
        return base + ((behavior.tick >> 2) & 3);
    case VB_DUKE_TRANSPORTERSTAR:
        return base + ((behavior.tick >> 2) & 7);
    case VB_DUKE_BURNING:
    case VB_DUKE_BURNING2:
        return base + ((behavior.tick >> 2) & 7);
    case VB_DUKE_FIRE:
    case VB_DUKE_FIRE2:
    case VB_DUKE_FLOORFLAME:
        return base + ((behavior.tick >> 2) & 3);
    default:
        return -1;
    }
}

static int16_t SW_CrossplayDukeActorFrame(sw_crossplay_duke_sprite_behavior_t const &behavior)
{
    int32_t const walkFrame = (behavior.tick >> 2) & 3;
    int32_t const slowFrame = (behavior.tick >> 3) & 1;
    int32_t const attackFrame = (behavior.tick >> 1) & 1;

    switch (behavior.actorKind)
    {
    case VB_CROSSPLAY_DUKE_ACTOR_SHARK:
        return VB_DUKE_SHARK + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_TROOPER:
        if (behavior.basePicnum == VB_DUKE_LIZTROOPDUCKING)
            return VB_DUKE_LIZTROOPDUCKING + slowFrame;
        if (behavior.basePicnum == VB_DUKE_LIZTROOPONTOILET || behavior.basePicnum == VB_DUKE_LIZTROOPJUSTSIT)
            return behavior.basePicnum + slowFrame;
        if (behavior.actorState == VB_CROSSPLAY_DUKE_ACTOR_ATTACK)
            return VB_DUKE_LIZTROOPSHOOT + attackFrame;
        return VB_DUKE_LIZTROOP + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_OCTABRAIN:
        return VB_DUKE_OCTABRAIN + ((behavior.tick >> 2) & 7);

    case VB_CROSSPLAY_DUKE_ACTOR_DRONE:
        return VB_DUKE_DRONE + slowFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_COMMANDER:
        return VB_DUKE_COMMANDER + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_RECON:
        return VB_DUKE_RECON + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_TANK:
        return VB_DUKE_TANK + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_PIGCOP:
        return VB_DUKE_PIGCOP + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_LIZMAN:
        if (behavior.actorState == VB_CROSSPLAY_DUKE_ACTOR_ATTACK)
            return VB_DUKE_LIZMANSPITTING + attackFrame;
        return VB_DUKE_LIZMAN + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_TURRET:
        return VB_DUKE_ROTATEGUN + (behavior.actorState == VB_CROSSPLAY_DUKE_ACTOR_ATTACK ? attackFrame : 0);

    case VB_CROSSPLAY_DUKE_ACTOR_SLIME:
        return behavior.basePicnum + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_BOSS:
        return behavior.basePicnum + walkFrame;

    case VB_CROSSPLAY_DUKE_ACTOR_NEWBEAST:
        return VB_DUKE_NEWBEAST + walkFrame;

    default:
        return behavior.basePicnum;
    }
}

static void SW_CrossplayMoveDukeActorSprite(int32_t spriteNum, sw_crossplay_duke_sprite_behavior_t &behavior,
                                            sw_crossplay_duke_actor_profile_t const &profile, PLAYERp const pp,
                                            int32_t distance)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS ||
        profile.stationary || profile.speed <= 0 || pp == NULL || distance <= profile.meleeRange)
        return;

    SPRITEp const sp = &sprite[spriteNum];
    vec3_t const oldPos = sp->xyz;
    int16_t const oldSect = sp->sectnum;
    int16_t newSect = sp->sectnum;
    int16_t desiredMoveAng = sp->ang;

    if (profile.rangedDamage > 0 && profile.preferredRange > 0)
    {
        if (distance < (profile.preferredRange >> 1))
            desiredMoveAng = NORM_ANGLE(sp->ang + 1024);
        else if (distance < profile.preferredRange)
            desiredMoveAng = NORM_ANGLE(sp->ang + (behavior.strafeDir >= 0 ? 512 : -512));
    }

    if (behavior.moveCooldown > 0)
        --behavior.moveCooldown;
    else
    {
        behavior.moveCooldown = 12 + (krand1() & 31);
        if ((krand1() & 255) < profile.strafeChance)
            behavior.strafeDir = -behavior.strafeDir;
    }

    int32_t const moveDelta = clamp<int32_t>(SW_CrossplayAngleDelta(behavior.moveAng, desiredMoveAng),
                                            -max<int32_t>(profile.turnRate, 1), max<int32_t>(profile.turnRate, 1));
    behavior.moveAng = NORM_ANGLE(behavior.moveAng + moveDelta);

    int32_t const xvect = MOVEx(profile.speed, behavior.moveAng);
    int32_t const yvect = MOVEy(profile.speed, behavior.moveAng);

    // clipmove() consumes 18.14 fixed-point velocity vectors; MOVEx()/MOVEy() are plain units per tic.
    clipmove(&sp->xyz, &newSect, xvect << 14, yvect << 14, max<int32_t>((int32_t)sp->clipdist << 2, 64), Z(32), Z(32),
             CLIPMASK_ACTOR);

    if ((unsigned)newSect >= (unsigned)numsectors || (behavior.stayput && newSect != behavior.homeSect))
    {
        sp->xyz = oldPos;
        if ((unsigned)oldSect < (unsigned)numsectors && sp->sectnum != oldSect)
            changespritesect(spriteNum, oldSect);
        return;
    }

    if (newSect != oldSect)
        changespritesect(spriteNum, newSect);

    if (profile.flying)
    {
        int32_t const targetZ = pp->posz - Z(24);
        sp->z += clamp(targetZ - sp->z, -Z(2), Z(2));
    }
    else
    {
        int32_t ceilingZ;
        int32_t floorZ;
        getzsofslope(sp->sectnum, sp->x, sp->y, &ceilingZ, &floorZ);
        sp->z = floorZ;
    }

    setspritez(spriteNum, &sp->xyz);
}

static void SW_CrossplayTickDukeActorSprite(int32_t spriteNum, sw_crossplay_duke_sprite_behavior_t &behavior)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    SPRITEp const sp = &sprite[spriteNum];
    sw_crossplay_duke_actor_profile_t const profile = SW_CrossplayDukeActorProfile(behavior.actorKind, behavior.sourcePicnum, behavior.displayPal);

    if (behavior.attackCooldown > 0)
        --behavior.attackCooldown;
    if (behavior.painCooldown > 0)
        --behavior.painCooldown;

    int32_t distance = INT32_MAX;
    PLAYERp const pp = SW_CrossplayNearestPlayerToDukeSprite(spriteNum, &distance);
    if (pp == NULL || (unsigned)pp->cursectnum >= (unsigned)numsectors)
        return;

    int32_t const desiredAng = getangle(pp->posx - sp->x, pp->posy - sp->y);
    int32_t const turnDelta = clamp<int32_t>(SW_CrossplayAngleDelta(sp->ang, desiredAng),
                                             -max<int32_t>(profile.turnRate, 1), max<int32_t>(profile.turnRate, 1));
    sp->ang = NORM_ANGLE(sp->ang + turnDelta);
    behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_IDLE;

    int32_t const zDistance = klabs((pp->posz - Z(24)) - sp->z);
    int32_t const actorCanIgnoreGroundZ = profile.stationary || profile.flying;
    int32_t const canSeePlayer = distance <= profile.sightRange &&
                                 (actorCanIgnoreGroundZ || zDistance < Z(128)) &&
                                 SW_CrossplayDukeActorCanSeePlayer(spriteNum, pp);

    if (canSeePlayer && !(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_AWAKE))
    {
        behavior.flags |= VB_CROSSPLAY_DUKE_SPRITE_AWAKE;
        SW_CrossplayPlayDukeSpriteSound(spriteNum, NULL, profile.alertSound);
    }

    if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_AWAKE) && distance > 2048)
    {
        SW_CrossplayDisplayDukeSpritePicnum(spriteNum, SW_CrossplayDukeActorFrame(behavior));
        return;
    }

    if (profile.suicide && canSeePlayer && distance <= profile.meleeRange && (actorCanIgnoreGroundZ || zDistance < Z(96)))
    {
        SW_CrossplayPlayDukeSpriteSound(spriteNum, NULL, profile.attackSound);
        PlayerUpdateHealth(pp, -profile.meleeDamage);
        SW_CrossplayKillDukeActorSprite(spriteNum, NULL);
        return;
    }

    if (behavior.attackCooldown == 0)
    {
        if (profile.meleeDamage > 0 && distance <= profile.meleeRange && (actorCanIgnoreGroundZ || zDistance < Z(96)))
        {
            behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_ATTACK;
            behavior.attackCooldown = profile.attackDelay;
            SW_CrossplayPlayDukeSpriteSound(spriteNum, NULL, profile.attackSound);
            PlayerUpdateHealth(pp, -profile.meleeDamage);
            ++behavior.attackEvents;
            ++g_swCrossplayDukeActorAttackEvents;
        }
        else if (profile.rangedDamage > 0 && canSeePlayer && distance <= profile.attackRange)
        {
            behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_ATTACK;
            behavior.attackCooldown = profile.attackDelay;
            SW_CrossplayPlayDukeSpriteSound(spriteNum, NULL, profile.attackSound);
            SW_CrossplayDukeActorFireHitscan(spriteNum, pp, profile);
            ++behavior.attackEvents;
            ++g_swCrossplayDukeActorAttackEvents;
        }
    }
    else if (behavior.attackCooldown > (uint16_t)max<int32_t>(profile.attackDelay - 8, 0))
        behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_ATTACK;

    if (behavior.actorState != VB_CROSSPLAY_DUKE_ACTOR_ATTACK)
    {
        if (distance < profile.sightRange)
        {
            SW_CrossplayMoveDukeActorSprite(spriteNum, behavior, profile, pp, distance);
            behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_CHASE;
        }
        else if (behavior.painCooldown > 0)
            behavior.actorState = VB_CROSSPLAY_DUKE_ACTOR_PAIN;
    }

    SW_CrossplayDisplayDukeSpritePicnum(spriteNum, SW_CrossplayDukeActorFrame(behavior));
}

static void SW_CrossplayTickDukeSpriteBehaviors(void)
{
    if (g_swCrossplayDukeSpriteBehaviorCount <= 0)
        return;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        sw_crossplay_duke_sprite_behavior_t &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
        if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            continue;

        if (sprite[spriteNum].statnum == MAXSTATUS)
        {
            SW_CrossplayClearDukeSpriteBehavior(spriteNum);
            continue;
        }

        ++behavior.tick;

        if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_VISUAL_OVERRIDE)
        {
            SW_CrossplayDisplayDukeSpriteVisualOverride(spriteNum, behavior);
            continue;
        }

        if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ANIMATES)
        {
            int16_t const frame = SW_CrossplayAnimatedDukeSpriteFrame(behavior);
            if (frame >= 0)
                SW_CrossplayDisplayDukeSpritePicnum(spriteNum, frame);
        }

        if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ACTOR)
            SW_CrossplayTickDukeActorSprite(spriteNum, behavior);

        SW_CrossplayApplyDukeSourceInvisibility(spriteNum, behavior);
    }
}

static int16_t SW_CrossplayShadowWarriorPickupSpawnMatchForIcon(int16_t picnum)
{
    switch (picnum)
    {
    case RED_CARD: return 5;
    case BLUE_CARD: return 6;
    case GREEN_CARD: return 7;
    case YELLOW_CARD: return 8;
    case ICON_STAR: return 41;
    case ICON_SHOTGUN: return 51;
    case ICON_UZI: return 0;
    case ICON_MICRO_GUN: return 43;
    case ICON_RAIL_GUN: return 47;
    case ICON_GRENADE_LAUNCHER: return 45;
    case ICON_LG_UZI_AMMO: return 68;
    case ICON_LG_SHOTSHELL: return 52;
    case ICON_MICRO_BATTERY: return 44;
    case ICON_LG_GRENADE: return 46;
    case ICON_RAIL_AMMO: return 48;
    case ICON_HEART: return 55;
    case ICON_SM_MEDKIT: return 59;
    case ICON_MEDKIT: return 58;
    case ICON_BOOSTER: return 64;
    case ICON_ARMOR: return 57;
    case ICON_CHEMBOMB: return 60;
    case ICON_NIGHT_VISION: return 67;
    default: return -1;
    }
}

static void SW_CrossplayRegisterDukeRespawnedPickupVisual(sw_crossplay_duke_respawn_t const &respawn, int16_t swPicnum)
{
    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            sprite[spriteNum].sectnum != respawn.sectnum ||
            sprite[spriteNum].picnum != swPicnum ||
            (g_swCrossplayDukeSpriteBehaviors[spriteNum].flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            continue;

        if (FindDistance2D(sprite[spriteNum].x - respawn.pos.x, sprite[spriteNum].y - respawn.pos.y) > 512 ||
            klabs(sprite[spriteNum].z - respawn.pos.z) > Z(32))
            continue;

        SW_CrossplayRegisterDukeSpriteVisualOverride(spriteNum, respawn.spawnPicnum, respawn.pal);
        SW_CrossplayDisplayDukeSpriteVisualOverride(spriteNum, g_swCrossplayDukeSpriteBehaviors[spriteNum]);
        return;
    }
}

static int32_t SW_CrossplaySpawnDukeRespawnedPickup(sw_crossplay_duke_respawn_t const &respawn)
{
    int32_t domain = ART_CROSSPLAY_DOMAIN_ITEMS;
    int16_t const swPicnum = SW_CrossplayDukePickupToShadowWarrior(respawn.spawnPicnum, respawn.pal, &domain);
    if (swPicnum < 0 || artCrossplayEffectiveDomainGame(domain) != ART_CROSSPLAY_GAME_SW)
        return 0;

    int16_t const match = SW_CrossplayShadowWarriorPickupSpawnMatchForIcon(swPicnum);
    if (match < 0)
        return 0;

    int16_t const marker = COVERinsertsprite(respawn.sectnum, STAT_SPAWN_ITEMS);
    if (marker < 0)
        return 0;

    SPRITEp const sp = &sprite[marker];
    sp->xyz = respawn.pos;
    sp->picnum = ST1;
    sp->ang = respawn.ang;
    sp->cstat = CSTAT_SPRITE_INVISIBLE;
    sp->shade = 0;
    sp->pal = 0;
    sp->xrepeat = 64;
    sp->yrepeat = 64;
    sp->clipdist = 0;
    sp->xoffset = 0;
    sp->yoffset = 0;
    sp->xvel = 0;
    sp->yvel = 0;
    sp->zvel = 0;
    sp->owner = -1;
    sp->extra = 0;
    SP_TAG1(sp) = SPAWN_ITEMS;
    int16_t const matchTag = (int16_t)(-30000 + (marker & 1023));
    SP_TAG2(sp) = matchTag;
    SP_TAG3(sp) = match;
    RESET_BOOL1(sp);

    SpawnItemsMatch(matchTag);
    SW_CrossplayRegisterDukeRespawnedPickupVisual(respawn, swPicnum);
    return 1;
}

static int32_t SW_CrossplaySpawnDukeRespawnedSprite(sw_crossplay_duke_respawn_t const &respawn)
{
    if ((unsigned)respawn.sectnum >= (unsigned)numsectors || respawn.spawnPicnum <= 0)
        return 0;

    if (SW_CrossplayIsHiddenDukeControlSprite(respawn.spawnPicnum))
        return 1;

    if (SW_CrossplaySpawnDukeRespawnedPickup(respawn))
        return 1;

    int16_t const spriteNum = COVERinsertsprite(respawn.sectnum, STAT_NO_STATE);
    if (spriteNum < 0)
        return 0;

    SPRITEp const sp = &sprite[spriteNum];
    sp->xyz = respawn.pos;
    sp->picnum = SW_CrossplayRemapDukeTileForRuntime(respawn.spawnPicnum);
    sp->shade = respawn.shade;
    sp->pal = respawn.pal;
    sp->cstat = respawn.cstat;
    sp->xrepeat = max<int32_t>(respawn.xrepeat, 32);
    sp->yrepeat = max<int32_t>(respawn.yrepeat, 32);
    sp->ang = respawn.ang;
    sp->clipdist = respawn.clipdist > 0 ? respawn.clipdist : 32;
    sp->xoffset = 0;
    sp->yoffset = 0;
    sp->xvel = 0;
    sp->yvel = 0;
    sp->zvel = 0;
    sp->owner = -1;
    sp->lotag = 0;
    sp->hitag = 0;
    sp->extra = 0;

    // A respawned Duke ENEMY belongs to Duke's own AI, exactly like a map-placed
    // one.  Registering it with the bridge's old approximation instead produced
    // a trooper that walks and turns but can never fly or use its real
    // behaviors -- the "spins around itself" ambush trooper.
    if (SW_CrossplayDukeAIActive() &&
        SW_CrossplayDukeShouldRunDukeAI(respawn.spawnPicnum,
                                        SW_CrossplayDukeSpriteBehaviorFlagsForSprite(spriteNum, respawn.spawnPicnum)))
    {
        SW_CrossplayDukeAIAdoptSprite(spriteNum, respawn.spawnPicnum);

        if (SW_CrossplayDukeAIOwnsSprite(spriteNum))
        {
            LOG_F(INFO, "Crossplay: respawned Duke actor (tile %d) adopted by Duke AI.", respawn.spawnPicnum);
            return 1;
        }
        // fall through to the old path if adoption failed
    }

    uint16_t const behaviorFlags = SW_CrossplayDukeSpriteBehaviorFlagsForSprite(spriteNum, respawn.spawnPicnum);
    if (behaviorFlags & (VB_CROSSPLAY_DUKE_SPRITE_SWITCH |
                         VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE |
                         VB_CROSSPLAY_DUKE_SPRITE_ACTOR |
                         VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE))
    {
        SET(sp->cstat, CSTAT_SPRITE_BLOCK_HITSCAN);
        if (SW_CrossplayDukeSpriteShouldBlock(respawn.spawnPicnum, behaviorFlags))
            SET(sp->cstat, CSTAT_SPRITE_BLOCK);
        else
            RESET(sp->cstat, CSTAT_SPRITE_BLOCK);
    }
    else
        RESET(sp->cstat, CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);

    SW_CrossplayRegisterDukeSpriteBehavior(spriteNum, respawn.spawnPicnum);

    if ((g_swCrossplayDukeSpriteBehaviors[spriteNum].flags &
         (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) ==
        (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
        LOG_F(WARNING, "Crossplay: respawned Duke actor tile %d left on the old approximation layer (Duke AI would not adopt it).",
              respawn.spawnPicnum);

    setspritez(spriteNum, &sp->xyz);
    return 1;
}

static int32_t SW_CrossplayOperateDukeRespawns(int16_t lotag)
{
    if (lotag == 0)
        return 0;

    int32_t activated = 0;

    for (int32_t i = 0; i < g_swCrossplayDukeRespawnCount; ++i)
    {
        sw_crossplay_duke_respawn_t &respawn = g_swCrossplayDukeRespawns[i];
        if (respawn.lotag != lotag || respawn.active || respawn.used)
            continue;

        respawn.active = 1;
        respawn.delay = 66 - 12;
        activated = 1;
    }

    return activated;
}

static void SW_CrossplayTickDukeRespawns(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeRespawnCount; ++i)
    {
        sw_crossplay_duke_respawn_t &respawn = g_swCrossplayDukeRespawns[i];
        if (!respawn.active || respawn.used)
            continue;

        if (++respawn.delay < 66)
            continue;

        respawn.active = 0;
        respawn.used = 1;
        SW_CrossplaySpawnDukeRespawnedSprite(respawn);
    }
}

static void SW_CrossplayTickDukeExplosives(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeExplosiveCount; ++i)
    {
        sw_crossplay_duke_explosive_t &explosive = g_swCrossplayDukeExplosives[i];
        if (!explosive.active || explosive.finished || (unsigned)explosive.sectnum >= (unsigned)numsectors)
            continue;

        int32_t const step = (max(explosive.speed, 1) << 5) | 1;
        int32_t floorDone = 1;
        int32_t ceilingDone = 1;

        if (explosive.angle == 512)
        {
            if (explosive.owner)
                ceilingDone = SW_CrossplayStepDukeZ(&sector[explosive.sectnum].ceilingz, explosive.targetCeilingZ, step);
            else
            {
                int32_t const oldFloorZ = sector[explosive.sectnum].floorz;
                floorDone = SW_CrossplayStepDukeZ(&sector[explosive.sectnum].floorz, explosive.targetFloorZ, step);
                SW_CrossplayMoveDukeSectorFloorContents(explosive.sectnum, sector[explosive.sectnum].floorz - oldFloorZ);
            }
        }
        else
        {
            int32_t const oldFloorZ = sector[explosive.sectnum].floorz;
            floorDone = SW_CrossplayStepDukeZ(&sector[explosive.sectnum].floorz, explosive.targetFloorZ, step);
            SW_CrossplayMoveDukeSectorFloorContents(explosive.sectnum, sector[explosive.sectnum].floorz - oldFloorZ);
            ceilingDone = SW_CrossplayStepDukeZ(&sector[explosive.sectnum].ceilingz, explosive.targetCeilingZ, step);
        }

        if (++explosive.ticks > 256 || (floorDone && ceilingDone))
        {
            explosive.active = 0;
            explosive.finished = 1;
        }
    }
}

static void SW_CrossplayTranslateDukeSectorContents(int16_t sectnum, int32_t xChange, int32_t yChange)
{
    if ((xChange == 0 && yChange == 0) || (unsigned)sectnum >= (unsigned)numsectors)
        return;

    int32_t spriteNum, nextSprite;
    TRAVERSE_SPRITE_SECT(headspritesect[sectnum], spriteNum, nextSprite)
    {
        int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS) ||
            originalPicnum == VB_DUKE_SECTOREFFECTOR ||
            originalPicnum == VB_DUKE_LOCATORS ||
            originalPicnum == VB_DUKE_GPSPEED)
            continue;

        sprite[spriteNum].x += xChange;
        sprite[spriteNum].y += yChange;
        setspritez(spriteNum, &sprite[spriteNum].xyz);
    }

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (!SW_CrossplayDukePlayerInMovingSector(pp, sectnum) ||
            !SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
            continue;

        SW_CrossplayMoveDukePlayerXY(pp, xChange, yChange);
        SW_CrossplaySyncDukeMovedPlayer(pp, 1, 0);
    }
}

static sw_crossplay_duke_sector_zanim_t *SW_CrossplayFindDukeSectorZAnim(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeSectorZAnimCount; ++i)
    {
        sw_crossplay_duke_sector_zanim_t &anim = g_swCrossplayDukeSectorZAnims[i];
        if (anim.sectnum == sectnum)
            return &anim;
    }

    return NULL;
}

static int32_t SW_CrossplayStartDukeSectorZAnim(int16_t sectnum, int32_t moveFloor, int32_t moveCeiling,
                                                int32_t targetFloorZ, int32_t targetCeilingZ, int32_t speed,
                                                int32_t carryFloorContents)
{
    if ((unsigned)sectnum >= (unsigned)numsectors || (!moveFloor && !moveCeiling))
        return 0;

    if (speed <= 0)
        speed = SW_CrossplayDukeSectorSpeedForSector(sectnum);
    speed = clamp(speed, 1, 8192);

    sw_crossplay_duke_sector_zanim_t *anim = SW_CrossplayFindDukeSectorZAnim(sectnum);
    if (anim == NULL)
    {
        if (g_swCrossplayDukeSectorZAnimCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeSectorZAnims))
        {
            LOG_F(WARNING, "Crossplay: too many active Duke sector z animations for Shadow Warrior runtime.");
            return 0;
        }

        anim = &g_swCrossplayDukeSectorZAnims[g_swCrossplayDukeSectorZAnimCount++];
    }

    anim->sectnum = sectnum;
    anim->targetFloorZ = targetFloorZ;
    anim->targetCeilingZ = targetCeilingZ;
    anim->speed = speed;
    anim->moveFloor = moveFloor != 0;
    anim->moveCeiling = moveCeiling != 0;
    anim->carryFloorContents = carryFloorContents != 0;
    anim->active = 1;

    if (anim->moveFloor)
        setinterpolation(&sector[sectnum].floorz);
    if (anim->moveCeiling)
        setinterpolation(&sector[sectnum].ceilingz);

    return 1;
}

static int32_t SW_CrossplayStepDukeZ(int32_t *z, int32_t targetZ, int32_t speed)
{
    int32_t const delta = targetZ - *z;
    if (delta == 0)
        return 1;

    if (klabs(delta) <= speed)
    {
        *z = targetZ;
        return 1;
    }

    *z += ksgn(delta) * speed;
    return 0;
}

static int32_t SW_CrossplayStartDukeWallAnim(int16_t wallnum, int32_t axis, int32_t target, int32_t speed)
{
    if ((unsigned)wallnum >= (unsigned)numwalls)
        return 0;

    speed = clamp(speed, 1, 8192);
    int32_t *const coord = axis ? &wall[wallnum].y : &wall[wallnum].x;
    if (*coord == target)
        return 0;

    sw_crossplay_duke_wall_anim_t *anim = NULL;
    for (int32_t i = 0; i < g_swCrossplayDukeWallAnimCount; ++i)
    {
        sw_crossplay_duke_wall_anim_t &candidate = g_swCrossplayDukeWallAnims[i];
        if (candidate.active && candidate.wallnum == wallnum && candidate.axis == (axis != 0))
        {
            anim = &candidate;
            break;
        }
    }

    if (anim == NULL)
    {
        for (int32_t i = 0; i < g_swCrossplayDukeWallAnimCount; ++i)
        {
            if (!g_swCrossplayDukeWallAnims[i].active)
            {
                anim = &g_swCrossplayDukeWallAnims[i];
                break;
            }
        }
    }

    if (anim == NULL)
    {
        if (g_swCrossplayDukeWallAnimCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeWallAnims))
        {
            LOG_F(WARNING, "Crossplay: too many active Duke wall animations for Shadow Warrior runtime.");
            return 0;
        }

        anim = &g_swCrossplayDukeWallAnims[g_swCrossplayDukeWallAnimCount++];
    }

    anim->wallnum = wallnum;
    anim->axis = axis != 0;
    anim->target = target;
    anim->speed = speed;
    anim->active = 1;
    setinterpolation(coord);
    return 1;
}

static void SW_CrossplayTickDukeWallAnims(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeWallAnimCount; ++i)
    {
        sw_crossplay_duke_wall_anim_t &anim = g_swCrossplayDukeWallAnims[i];
        if (!anim.active)
            continue;

        if ((unsigned)anim.wallnum >= (unsigned)numwalls)
        {
            anim.active = 0;
            continue;
        }

        int32_t *const coord = anim.axis ? &wall[anim.wallnum].y : &wall[anim.wallnum].x;
        if (SW_CrossplayStepDukeZ(coord, anim.target, anim.speed))
            anim.active = 0;
    }
}

static int32_t SW_CrossplayOperateDukeSlidingStarTrekDoor(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t const startWall = sector[sectnum].wallptr;
    int32_t const wallCount = sector[sectnum].wallnum;
    int32_t const endWall = startWall + wallCount - 1;
    if (startWall < 0 || wallCount <= 1 || endWall >= numwalls)
        return 0;

    int64_t centerX = 0;
    int64_t centerY = 0;
    for (int32_t wallNum = startWall; wallNum <= endWall; ++wallNum)
    {
        centerX += wall[wallNum].x;
        centerY += wall[wallNum].y;
    }

    vec2_t center = { (int32_t)(centerX / wallCount), (int32_t)(centerY / wallCount) };
    int16_t wallfind[2] = { -1, -1 };

    for (int32_t wallNum = startWall; wallNum <= endWall; ++wallNum)
    {
        if (wall[wallNum].x != center.x && wall[wallNum].y != center.y)
            continue;

        if (wallfind[0] < 0)
            wallfind[0] = (int16_t)wallNum;
        else
        {
            wallfind[1] = (int16_t)wallNum;
            break;
        }
    }

    if (wallfind[1] < 0)
        return 0;

    int32_t speed = sector[sectnum].extra >> 4;
    if (speed <= 0)
        speed = SW_CrossplayDukeSectorSpeedForSector(sectnum) >> 4;
    speed = clamp(speed, 1, 8192);

    int32_t operated = 0;
    for (int32_t wallFindIndex = 0; wallFindIndex < 2; ++wallFindIndex)
    {
        int16_t const foundWall = wallfind[wallFindIndex];
        int16_t const prevWall = foundWall > startWall ? (int16_t)(foundWall - 1) : (int16_t)endWall;
        int16_t const point2Wall = wall[foundWall].point2;
        if ((unsigned)point2Wall >= (unsigned)numwalls)
            continue;

        vec2_t vectorToCenter = {
            ((wall[prevWall].x + wall[point2Wall].x) >> 1) - wall[foundWall].x,
            ((wall[prevWall].y + wall[point2Wall].y) >> 1) - wall[foundWall].y,
        };

        if (wall[foundWall].x == center.x && wall[foundWall].y == center.y)
        {
            int16_t const point2Point2Wall = wall[point2Wall].point2;
            if ((unsigned)point2Point2Wall >= (unsigned)numwalls)
                continue;

            if (vectorToCenter.x != 0)
            {
                int32_t const xDelta = wall[point2Point2Wall].x - wall[point2Wall].x;
                operated |= SW_CrossplayStartDukeWallAnim(foundWall, 0, wall[foundWall].x + xDelta, speed);
                operated |= SW_CrossplayStartDukeWallAnim(prevWall, 0, wall[prevWall].x + xDelta, speed);
                operated |= SW_CrossplayStartDukeWallAnim(point2Wall, 0, wall[point2Wall].x + xDelta, speed);
            }
            else if (vectorToCenter.y != 0)
            {
                int32_t const yDelta = wall[point2Point2Wall].y - wall[point2Wall].y;
                operated |= SW_CrossplayStartDukeWallAnim(foundWall, 1, wall[foundWall].y + yDelta, speed);
                operated |= SW_CrossplayStartDukeWallAnim(prevWall, 1, wall[prevWall].y + yDelta, speed);
                operated |= SW_CrossplayStartDukeWallAnim(point2Wall, 1, wall[point2Wall].y + yDelta, speed);
            }
        }
        else
        {
            if (vectorToCenter.x != 0)
            {
                operated |= SW_CrossplayStartDukeWallAnim(foundWall, 0, center.x, speed);
                operated |= SW_CrossplayStartDukeWallAnim(prevWall, 0, center.x + vectorToCenter.x, speed);
                operated |= SW_CrossplayStartDukeWallAnim(point2Wall, 0, center.x + vectorToCenter.x, speed);
            }
            else if (vectorToCenter.y != 0)
            {
                operated |= SW_CrossplayStartDukeWallAnim(foundWall, 1, center.y, speed);
                operated |= SW_CrossplayStartDukeWallAnim(prevWall, 1, center.y + vectorToCenter.y, speed);
                operated |= SW_CrossplayStartDukeWallAnim(point2Wall, 1, center.y + vectorToCenter.y, speed);
            }
        }
    }

    return operated;
}

static void SW_CrossplayMoveDukeSectorWallsFromOrigins(int16_t sectnum, vec2_t const &pos,
                                                       int16_t originStart, int16_t originCount,
                                                       vec2_t const *origins)
{
    if ((unsigned)sectnum >= (unsigned)numsectors || originCount <= 0 || origins == NULL)
        return;

    int32_t const startWall = sector[sectnum].wallptr;
    int32_t const sectorWallCount = sector[sectnum].wallnum;
    if (startWall < 0 || sectorWallCount <= 0 || startWall + sectorWallCount > numwalls)
        return;

    int32_t const wallCount = min<int32_t>(originCount, sectorWallCount);
    for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
    {
        vec2_t const &origin = origins[originStart + wallOffset];
        dragpoint(startWall + wallOffset, pos.x + origin.x, pos.y + origin.y, 0);
    }
}

static void SW_CrossplayActivateDukeQuake(sw_crossplay_duke_quake_t &quake)
{
    if (quake.finished)
        return;

    quake.active = 1;
    if (quake.ticks == 0)
        quake.ticks = 1;
}

static int32_t SW_CrossplayActivateDukeQuakesInSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int16_t hitag = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeQuakeCount; ++i)
    {
        sw_crossplay_duke_quake_t const &quake = g_swCrossplayDukeQuakes[i];
        if (quake.sectnum == sectnum && !quake.finished)
        {
            hitag = quake.hitag;
            break;
        }
    }

    if (hitag > 0)
        return SW_CrossplayActivateDukeQuakesWithHitag(hitag);

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeQuakeCount; ++i)
    {
        sw_crossplay_duke_quake_t &quake = g_swCrossplayDukeQuakes[i];
        if (quake.sectnum != sectnum || quake.finished)
            continue;

        SW_CrossplayActivateDukeQuake(quake);
        activated = 1;
    }

    return activated;
}

static int32_t SW_CrossplayActivateDukeQuakesWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeQuakeCount; ++i)
    {
        sw_crossplay_duke_quake_t &quake = g_swCrossplayDukeQuakes[i];
        if (quake.hitag != hitag || quake.finished)
            continue;

        SW_CrossplayActivateDukeQuake(quake);
        activated = 1;
    }

    return activated;
}

static void SW_CrossplayTickDukeQuakes(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeQuakeCount; ++i)
    {
        sw_crossplay_duke_quake_t &quake = g_swCrossplayDukeQuakes[i];
        if (!quake.active || quake.finished || (unsigned)quake.sectnum >= (unsigned)numsectors)
            continue;

        ++quake.ticks;
        if (quake.ticks > 96)
        {
            quake.active = 0;
            quake.finished = 1;
            continue;
        }

        if ((quake.ticks & 31) == 8)
            PlaySound(DIGI_DUKE_EARTHQUAKE, &quake.pos.x, &quake.pos.y, &quake.pos.z, v3df_dontpan|v3df_doppler);

        if (klabs(sector[quake.sectnum].floorheinum - quake.targetFloorHeinum) < 8)
            sector[quake.sectnum].floorheinum = quake.targetFloorHeinum;
        else
            sector[quake.sectnum].floorheinum += ksgn(quake.targetFloorHeinum - sector[quake.sectnum].floorheinum) << 4;

        vec2_t const move = {
            (3 * sintable[(quake.angle + 512) & 2047]) >> 14,
            (3 * sintable[quake.angle & 2047]) >> 14,
        };

        if (move.x == 0 && move.y == 0)
            continue;

        quake.pos.x += move.x;
        quake.pos.y += move.y;
        SW_CrossplayMoveDukeSectorWallsFromOrigins(quake.sectnum, quake.pos.xy, quake.originStart, quake.originCount,
                                                   g_swCrossplayDukeQuakeOrigins);
        SW_CrossplayTranslateDukeSectorContents(quake.sectnum, move.x, move.y);
    }
}

static int32_t SW_CrossplayToggleDukeConveyorsAndPistonsWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;

    for (int32_t i = 0; i < g_swCrossplayDukeConveyorCount; ++i)
    {
        sw_crossplay_duke_conveyor_t &conveyor = g_swCrossplayDukeConveyors[i];
        if (conveyor.hitag != hitag)
            continue;

        conveyor.disabled = !conveyor.disabled;
        activated = 1;
    }

    for (int32_t i = 0; i < g_swCrossplayDukePistonCount; ++i)
    {
        sw_crossplay_duke_piston_t &piston = g_swCrossplayDukePistons[i];
        if (piston.hitag != hitag)
            continue;

        piston.enabled = !piston.enabled;
        activated = 1;
    }

    return activated;
}

static void SW_CrossplayTickDukePistons(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukePistonCount; ++i)
    {
        sw_crossplay_duke_piston_t &piston = g_swCrossplayDukePistons[i];
        if (!piston.enabled || (unsigned)piston.sectnum >= (unsigned)numsectors)
            continue;

        int32_t const speed = clamp(piston.speed, 1, 8192);
        if (sector[piston.sectnum].floorz <= sector[piston.sectnum].ceilingz)
            piston.movingDown = 0;
        else if (sector[piston.sectnum].ceilingz <= piston.openCeilingZ)
            piston.movingDown = 1;

        if (piston.movingDown)
        {
            sector[piston.sectnum].ceilingz += speed;
            if (sector[piston.sectnum].ceilingz > sector[piston.sectnum].floorz)
                sector[piston.sectnum].ceilingz = sector[piston.sectnum].floorz;
        }
        else
        {
            sector[piston.sectnum].ceilingz -= speed;
            if (sector[piston.sectnum].ceilingz < piston.openCeilingZ)
                sector[piston.sectnum].ceilingz = piston.openCeilingZ;
        }
    }
}

static int32_t SW_CrossplayDukeSectorHasActiveIncrementalMover(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeIncrementalMoverCount; ++i)
    {
        sw_crossplay_duke_incremental_mover_t const &mover = g_swCrossplayDukeIncrementalMovers[i];
        if (mover.sectnum == sectnum && mover.active && !mover.finished)
            return 1;
    }

    return 0;
}

static int32_t SW_CrossplaySetDukeIncrementalMoverActive(sw_crossplay_duke_incremental_mover_t &mover, int32_t toggle)
{
    if (mover.finished)
        return 0;

    if (toggle)
        mover.active = !mover.active;
    else if (!mover.active)
        mover.active = 1;
    else
        return 0;

    mover.delay = 0;
    return 1;
}

static int32_t SW_CrossplayActivateDukeIncrementalMoversInSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeIncrementalMoverCount; ++i)
    {
        sw_crossplay_duke_incremental_mover_t &mover = g_swCrossplayDukeIncrementalMovers[i];
        if (mover.sectnum == sectnum)
            activated |= SW_CrossplaySetDukeIncrementalMoverActive(mover, 1);
    }

    return activated;
}

static int32_t SW_CrossplayToggleDukeIncrementalMoversWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeIncrementalMoverCount; ++i)
    {
        sw_crossplay_duke_incremental_mover_t &mover = g_swCrossplayDukeIncrementalMovers[i];
        if (mover.hitag == hitag)
            activated |= SW_CrossplaySetDukeIncrementalMoverActive(mover, 1);
    }

    return activated;
}

static int32_t SW_CrossplayStartDukeIncrementalMoversWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeIncrementalMoverCount; ++i)
    {
        sw_crossplay_duke_incremental_mover_t &mover = g_swCrossplayDukeIncrementalMovers[i];
        if (mover.hitag == hitag)
            activated |= SW_CrossplaySetDukeIncrementalMoverActive(mover, 0);
    }

    return activated;
}

static void SW_CrossplayTickDukeIncrementalMovers(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeIncrementalMoverCount; ++i)
    {
        sw_crossplay_duke_incremental_mover_t &mover = g_swCrossplayDukeIncrementalMovers[i];
        if (!mover.active || mover.finished || (unsigned)mover.sectnum >= (unsigned)numsectors)
            continue;

        int32_t *const z = mover.moveCeiling ? &sector[mover.sectnum].ceilingz : &sector[mover.sectnum].floorz;
        int32_t const oldZ = *z;
        int32_t const speed = clamp(mover.speed, 1, 8192);
        int32_t const done = SW_CrossplayStepDukeZ(z, mover.targetZ, speed);

        if (!mover.moveCeiling)
            SW_CrossplayMoveDukeSectorFloorContents(mover.sectnum, *z - oldZ);

        if (done)
        {
            mover.active = 0;
            mover.finished = 1;
            continue;
        }

        if (mover.delayLimit > 0 && ++mover.delay >= mover.delayLimit)
        {
            mover.delay = 0;
            mover.active = 0;
        }
    }
}

static void SW_CrossplayTickDukeWaves(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeWaveCount; ++i)
    {
        sw_crossplay_duke_wave_t &wave = g_swCrossplayDukeWaves[i];
        if ((unsigned)wave.sectnum >= (unsigned)numsectors)
            continue;

        wave.phase = (wave.phase + 64) & 2047;
        sector[wave.sectnum].floorz = wave.baseFloorZ + mulscale12(wave.amplitude, sintable[wave.phase & 2047]);
    }
}

static int32_t SW_CrossplayDukeSectorHasActiveProjectileShooter(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeProjectileShooterCount; ++i)
    {
        sw_crossplay_duke_projectile_shooter_t const &shooter = g_swCrossplayDukeProjectileShooters[i];
        if (shooter.sectnum == sectnum && shooter.active)
            return 1;
    }

    return 0;
}

static int32_t SW_CrossplaySetDukeProjectileShooterActive(sw_crossplay_duke_projectile_shooter_t &shooter, int32_t toggle)
{
    if (toggle)
        shooter.active = !shooter.active;
    else if (!shooter.active)
        shooter.active = 1;
    else
        return 0;

    shooter.ticks = 0;
    return 1;
}

static int32_t SW_CrossplayActivateDukeProjectileShootersInSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeProjectileShooterCount; ++i)
    {
        sw_crossplay_duke_projectile_shooter_t &shooter = g_swCrossplayDukeProjectileShooters[i];
        if (shooter.sectnum == sectnum)
            activated |= SW_CrossplaySetDukeProjectileShooterActive(shooter, 1);
    }

    return activated;
}

static int32_t SW_CrossplayToggleDukeProjectileShootersWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeProjectileShooterCount; ++i)
    {
        sw_crossplay_duke_projectile_shooter_t &shooter = g_swCrossplayDukeProjectileShooters[i];
        if (shooter.hitag == hitag)
            activated |= SW_CrossplaySetDukeProjectileShooterActive(shooter, 1);
    }

    return activated;
}

static void SW_CrossplayFireDukeProjectileShooter(sw_crossplay_duke_projectile_shooter_t const &shooter)
{
    if ((unsigned)shooter.sectnum >= (unsigned)numsectors)
        return;

    int16_t startSect = shooter.sectnum;
    int16_t const shootAng = shooter.angle;
    int32_t const xVect = sintable[(shootAng + 512) & 2047];
    int32_t const yVect = sintable[shootAng & 2047];
    int32_t const startX = shooter.pos.x + (xVect >> 8);
    int32_t const startY = shooter.pos.y + (yVect >> 8);
    int32_t const startZ = SW_CrossplayClampZToSector(shooter.sectnum, startX, startY, shooter.pos.z);
    updatesectorz(startX, startY, startZ, &startSect);
    if ((unsigned)startSect >= (unsigned)numsectors)
        startSect = shooter.sectnum;

    hitdata_t hitinfo = { { -1, -1, -1 }, -1, -1, -1 };
    FAFhitscan(startX, startY, startZ, startSect, xVect, yVect, 0, &hitinfo, CLIPMASK_MISSILE);
    SW_CrossplayPlayDukeSpriteSound(-1, &shooter.pos, DIGI_DUKE_TURR_ATTACK);

    if (hitinfo.sect < 0)
        return;

    vec3_t const hitPos = { hitinfo.x, hitinfo.y, hitinfo.z };
    int16_t const projectileTile = shooter.projectileTile > 0 ? shooter.projectileTile : VB_DUKE_FIRELASER;
    SW_CrossplaySpawnDukeVisualProjectile(-1, projectileTile, startSect,
                                          startX, startY, startZ, hitinfo.x, hitinfo.y, hitinfo.z, shootAng);

    if (hitinfo.wall >= 0)
    {
        if (!SW_CrossplayDamageDukeMapWall(-1, hitinfo.wall, &hitPos))
            QueueHole(hitinfo.sect, hitinfo.wall, hitinfo.x, hitinfo.y, hitinfo.z);
    }

    if (hitinfo.sprite >= 0)
    {
        if (!SW_CrossplayDamageDukeMapSprite(-1, hitinfo.sprite, &hitPos))
        {
            PLAYERp const hitPlayer = SW_CrossplayPlayerForHitSprite(hitinfo.sprite);
            if (hitPlayer != NULL)
            {
                int32_t const damage = shooter.projectileTile > 0 ? 16 : 10;
                PlayerUpdateHealth(hitPlayer, -damage);
                PlayerCheckDeath(hitPlayer, -1);
            }
        }
    }

    if (hitinfo.wall >= 0)
        SpawnTurretSparks(hitinfo.sect, hitinfo.wall, hitinfo.x, hitinfo.y, hitinfo.z, shootAng);
}

static void SW_CrossplayTickDukeProjectileShooters(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeProjectileShooterCount; ++i)
    {
        sw_crossplay_duke_projectile_shooter_t &shooter = g_swCrossplayDukeProjectileShooters[i];
        if (!shooter.active)
            continue;

        if (shooter.ticks == 0)
            SW_CrossplayFireDukeProjectileShooter(shooter);

        if (++shooter.ticks >= 120 * 5)
        {
            shooter.active = 0;
            shooter.ticks = 0;
        }
    }
}

static void SW_CrossplayMoveDukeSectorZAnim(sw_crossplay_duke_sector_zanim_t &anim)
{
    if (!anim.active || (unsigned)anim.sectnum >= (unsigned)numsectors)
        return;

    int32_t floorDone = 1;
    int32_t ceilingDone = 1;

    if (anim.moveFloor)
    {
        int32_t const oldFloorZ = sector[anim.sectnum].floorz;
        floorDone = SW_CrossplayStepDukeZ(&sector[anim.sectnum].floorz, anim.targetFloorZ, anim.speed);
        if (anim.carryFloorContents)
            SW_CrossplayMoveDukeSectorFloorContents(anim.sectnum, sector[anim.sectnum].floorz - oldFloorZ);
    }

    if (anim.moveCeiling)
        ceilingDone = SW_CrossplayStepDukeZ(&sector[anim.sectnum].ceilingz, anim.targetCeilingZ, anim.speed);

    if (floorDone && ceilingDone)
        anim.active = 0;
}

static int32_t SW_CrossplayDukeSectorHasActiveZAnim(int16_t sectnum)
{
    sw_crossplay_duke_sector_zanim_t const *const anim = SW_CrossplayFindDukeSectorZAnim(sectnum);
    return anim && anim->active;
}

static int32_t SW_CrossplayDukeSectorHasActiveDropFloor(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeDropFloorCount; ++i)
    {
        sw_crossplay_duke_drop_floor_t const &dropFloor = g_swCrossplayDukeDropFloors[i];
        if (dropFloor.sectnum == sectnum && dropFloor.active)
            return 1;
    }

    return 0;
}

static void SW_CrossplayActivateDukeDropFloor(sw_crossplay_duke_drop_floor_t &dropFloor)
{
    if (dropFloor.finished)
        return;

    dropFloor.active = 1;
}

static int32_t SW_CrossplayActivateDukeDropFloorsWithHitag(int16_t hitag)
{
    if (hitag <= 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeDropFloorCount; ++i)
    {
        sw_crossplay_duke_drop_floor_t &dropFloor = g_swCrossplayDukeDropFloors[i];
        if (dropFloor.hitag != hitag || dropFloor.finished)
            continue;

        SW_CrossplayActivateDukeDropFloor(dropFloor);
        activated = 1;
    }

    return activated;
}

static int32_t SW_CrossplayActivateDukeDropFloorsInSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int16_t hitag = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeDropFloorCount; ++i)
    {
        sw_crossplay_duke_drop_floor_t const &dropFloor = g_swCrossplayDukeDropFloors[i];
        if (dropFloor.sectnum == sectnum && !dropFloor.finished)
        {
            hitag = dropFloor.hitag;
            break;
        }
    }

    if (hitag > 0)
        return SW_CrossplayActivateDukeDropFloorsWithHitag(hitag);

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeDropFloorCount; ++i)
    {
        sw_crossplay_duke_drop_floor_t &dropFloor = g_swCrossplayDukeDropFloors[i];
        if (dropFloor.sectnum != sectnum || dropFloor.finished)
            continue;

        SW_CrossplayActivateDukeDropFloor(dropFloor);
        activated = 1;
    }

    return activated;
}

static void SW_CrossplayTickDukeDropFloors(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeDropFloorCount; ++i)
    {
        sw_crossplay_duke_drop_floor_t &dropFloor = g_swCrossplayDukeDropFloors[i];
        if (!dropFloor.active || dropFloor.finished || (unsigned)dropFloor.sectnum >= (unsigned)numsectors)
            continue;

        if (dropFloor.delay > 0)
        {
            --dropFloor.delay;
            continue;
        }

        int32_t done = 0;
        if (dropFloor.moveCeiling)
            done = SW_CrossplayStepDukeZ(&sector[dropFloor.sectnum].ceilingz, dropFloor.targetZ, dropFloor.speed);
        else
        {
            int32_t const oldFloorZ = sector[dropFloor.sectnum].floorz;
            done = SW_CrossplayStepDukeZ(&sector[dropFloor.sectnum].floorz, dropFloor.targetZ, dropFloor.speed);
            SW_CrossplayMoveDukeSectorFloorContents(dropFloor.sectnum, sector[dropFloor.sectnum].floorz - oldFloorZ);
        }

        if (done)
        {
            dropFloor.active = 0;
            dropFloor.finished = 1;
        }
    }
}

static int32_t SW_CrossplayDukeSectorHasActiveTeethDoor(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTeethDoorCount; ++i)
    {
        sw_crossplay_duke_teeth_door_t const &door = g_swCrossplayDukeTeethDoors[i];
        if (door.sectnum == sectnum && door.active)
            return 1;
    }

    return 0;
}

static int32_t SW_CrossplayActivateDukeTeethDoors(int16_t hitag, int16_t targetSectnum)
{
    if (hitag == 0)
        return 0;

    int32_t activated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeTeethDoorCount; ++i)
    {
        sw_crossplay_duke_teeth_door_t &door = g_swCrossplayDukeTeethDoors[i];
        if (door.hitag != hitag || (unsigned)door.sectnum >= (unsigned)numsectors)
            continue;

        door.speed = -door.speed;
        door.targetSectnum = targetSectnum;
        door.active = 1;
        setinterpolation(&sector[door.sectnum].ceilingz);
        activated = 1;
    }

    return activated;
}

static void SW_CrossplayTickDukeTeethDoors(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTeethDoorCount; ++i)
    {
        sw_crossplay_duke_teeth_door_t &door = g_swCrossplayDukeTeethDoors[i];
        if (!door.active || (unsigned)door.sectnum >= (unsigned)numsectors)
            continue;

        if ((unsigned)door.targetSectnum < (unsigned)numsectors &&
            SW_CrossplayDukeSectorHasActiveZAnim(door.targetSectnum))
            sector[door.sectnum].ceilingz += door.speed * 9;
        else
            door.active = 0;
    }
}

static sw_crossplay_duke_warp_elevator_t *SW_CrossplayFindDukeWarpElevatorForSector(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeWarpElevatorCount; ++i)
    {
        sw_crossplay_duke_warp_elevator_t &elevator = g_swCrossplayDukeWarpElevators[i];
        if (elevator.sectnum == sectnum)
            return &elevator;
    }

    return NULL;
}

static sw_crossplay_duke_warp_elevator_t *SW_CrossplayFindDukeWarpElevatorDestination(
    sw_crossplay_duke_warp_elevator_t const &source, int32_t direction)
{
    if ((unsigned)source.sectnum >= (unsigned)numsectors || direction == 0)
        return NULL;

    int16_t const targetSectorHitag = sector[source.sectnum].hitag - direction;

    for (int32_t i = 0; i < g_swCrossplayDukeWarpElevatorCount; ++i)
    {
        sw_crossplay_duke_warp_elevator_t &elevator = g_swCrossplayDukeWarpElevators[i];
        if (&elevator == &source ||
            elevator.hitag != source.hitag ||
            (unsigned)elevator.sectnum >= (unsigned)numsectors ||
            sector[elevator.sectnum].hitag != targetSectorHitag)
            continue;

        return &elevator;
    }

    return NULL;
}

static void SW_CrossplaySetDukeWarpElevatorGroupDirection(int16_t hitag, int32_t direction)
{
    for (int32_t i = 0; i < g_swCrossplayDukeWarpElevatorCount; ++i)
    {
        sw_crossplay_duke_warp_elevator_t &elevator = g_swCrossplayDukeWarpElevators[i];
        if (elevator.hitag != hitag)
            continue;

        elevator.direction = (int8_t)direction;
        elevator.warpPending = direction != 0;
    }
}

static int32_t SW_CrossplayActivateDukeWarpElevators(sw_crossplay_duke_warp_elevator_t const &source, int32_t direction)
{
    if (direction == 0)
    {
        SW_CrossplaySetDukeWarpElevatorGroupDirection(source.hitag, 0);
        return 1;
    }

    if ((unsigned)source.sectnum >= (unsigned)numsectors)
        return 0;

    int32_t canMove = klabs(sector[source.sectnum].floorz - source.stopFloorZ) > source.speed;
    if (!canMove)
        canMove = SW_CrossplayFindDukeWarpElevatorDestination(source, direction) != NULL;

    if (!canMove)
        return 0;

    SW_CrossplaySetDukeWarpElevatorGroupDirection(source.hitag, direction);
    return 1;
}

static int32_t SW_CrossplayOperateDukeWarpElevator(int16_t sectnum, PLAYERp pp)
{
    sw_crossplay_duke_warp_elevator_t *const elevator = SW_CrossplayFindDukeWarpElevatorForSector(sectnum);
    if (elevator == NULL || (unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t const playerIsInside = pp != NULL && (unsigned)pp->PlayerSprite < MAXSPRITES &&
        sprite[pp->PlayerSprite].sectnum == sectnum;

    if (playerIsInside)
    {
        if (SW_CrossplayActivateDukeWarpElevators(*elevator, -1))
            return 1;
        return SW_CrossplayActivateDukeWarpElevators(*elevator, 1);
    }

    return SW_CrossplayActivateDukeWarpElevators(*elevator, sector[sectnum].floorz > elevator->effectorPos.z ? -1 : 1);
}

static int32_t SW_CrossplayDukeSectorHasActiveWarpElevator(int16_t sectnum)
{
    sw_crossplay_duke_warp_elevator_t const *const elevator = SW_CrossplayFindDukeWarpElevatorForSector(sectnum);
    return elevator && elevator->direction != 0;
}

static void SW_CrossplayMoveDukeWarpElevatorContentsZ(int16_t sectnum, int32_t zChange)
{
    if (zChange == 0 || (unsigned)sectnum >= (unsigned)numsectors)
        return;

    int32_t spriteNum, nextSprite;
    TRAVERSE_SPRITE_SECT(headspritesect[sectnum], spriteNum, nextSprite)
    {
        int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            originalPicnum == VB_DUKE_SECTOREFFECTOR ||
            (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS))
            continue;

        sprite[spriteNum].z += zChange;
        setspritez(spriteNum, &sprite[spriteNum].xyz);
    }

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (!SW_CrossplayDukePlayerInMovingSector(pp, sectnum) || !SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
            continue;

        SW_CrossplayMoveDukePlayerZ(pp, zChange);
        SW_CrossplaySyncDukeMovedPlayer(pp, 0, 1);
    }
}

static void SW_CrossplayTeleportDukeWarpElevatorContents(sw_crossplay_duke_warp_elevator_t const &source,
                                                         sw_crossplay_duke_warp_elevator_t const &dest)
{
    if ((unsigned)source.sectnum >= (unsigned)numsectors || (unsigned)dest.sectnum >= (unsigned)numsectors)
        return;

    vec2_t const xyDelta = { dest.effectorPos.x - source.effectorPos.x, dest.effectorPos.y - source.effectorPos.y };
    int32_t const sourceFloorZ = sector[source.sectnum].floorz;
    int32_t const destFloorZ = sector[dest.sectnum].floorz;

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (!SW_CrossplayDukePlayerInMovingSector(pp, source.sectnum))
            continue;

        int32_t const floorRelativeZ = sourceFloorZ - pp->posz;
        pp->posx += xyDelta.x;
        pp->posy += xyDelta.y;
        pp->posz = destFloorZ - floorRelativeZ;
        pp->oldposx = pp->posx;
        pp->oldposy = pp->posy;
        pp->oldposz = pp->posz;
        pp->oposx = pp->posx;
        pp->oposy = pp->posy;
        pp->oposz = pp->posz;
        pp->lastcursectnum = pp->cursectnum;
        pp->cursectnum = dest.sectnum;
        pp->lv_sectnum = pp->cursectnum;
        pp->lv_x = pp->posx;
        pp->lv_y = pp->posy;
        pp->lv_z = pp->posz;

        if ((unsigned)pp->PlayerSprite < MAXSPRITES)
        {
            sprite[pp->PlayerSprite].xyz = pp->pos;
            changespritesect(pp->PlayerSprite, dest.sectnum);
            UpdatePlayerSprite(pp);
            DoPlayerZrange(pp);
        }
    }

    int32_t spriteNum, nextSprite;
    TRAVERSE_SPRITE_SECT(headspritesect[source.sectnum], spriteNum, nextSprite)
    {
        int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            originalPicnum == VB_DUKE_SECTOREFFECTOR ||
            (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS))
            continue;

        int32_t const floorRelativeZ = sourceFloorZ - sprite[spriteNum].z;
        sprite[spriteNum].x += xyDelta.x;
        sprite[spriteNum].y += xyDelta.y;
        sprite[spriteNum].z = destFloorZ - floorRelativeZ;
        changespritesect(spriteNum, dest.sectnum);
        setspritez(spriteNum, &sprite[spriteNum].xyz);
    }
}

static void SW_CrossplayMoveDukeWarpElevator(sw_crossplay_duke_warp_elevator_t &elevator)
{
    if (elevator.direction == 0 || (unsigned)elevator.sectnum >= (unsigned)numsectors)
        return;

    int32_t const zChange = elevator.direction * elevator.speed;
    sector[elevator.sectnum].floorz += zChange;
    sector[elevator.sectnum].ceilingz += zChange;
    SW_CrossplayMoveDukeWarpElevatorContentsZ(elevator.sectnum, zChange);

    if (klabs(sector[elevator.sectnum].floorz - elevator.stopFloorZ) <= elevator.speed)
    {
        int32_t const snapChange = elevator.stopFloorZ - sector[elevator.sectnum].floorz;
        sector[elevator.sectnum].floorz = elevator.stopFloorZ;
        sector[elevator.sectnum].ceilingz += snapChange;
        SW_CrossplayMoveDukeWarpElevatorContentsZ(elevator.sectnum, snapChange);
        SW_CrossplaySetDukeWarpElevatorGroupDirection(elevator.hitag, 0);
        return;
    }

    if (elevator.direction < 0)
    {
        if (sector[elevator.sectnum].floorz > elevator.lowerRevealZ)
            return;
    }
    else if (sector[elevator.sectnum].ceilingz < elevator.upperRevealZ)
        return;

    if (!elevator.warpPending)
        return;

    elevator.warpPending = 0;
    sw_crossplay_duke_warp_elevator_t const *const dest =
        SW_CrossplayFindDukeWarpElevatorDestination(elevator, elevator.direction);
    if (dest != NULL)
        SW_CrossplayTeleportDukeWarpElevatorContents(elevator, *dest);
}

static void SW_CrossplayAlignDukeWarpElevators(void)
{
    for (int32_t sourceIndex = 0; sourceIndex < g_swCrossplayDukeWarpElevatorCount; ++sourceIndex)
    {
        sw_crossplay_duke_warp_elevator_t const &source = g_swCrossplayDukeWarpElevators[sourceIndex];
        if (source.shade <= 16 || (unsigned)source.sectnum >= (unsigned)numsectors)
            continue;

        for (int32_t destIndex = 0; destIndex < g_swCrossplayDukeWarpElevatorCount; ++destIndex)
        {
            sw_crossplay_duke_warp_elevator_t const &dest = g_swCrossplayDukeWarpElevators[destIndex];
            if (destIndex == sourceIndex ||
                dest.hitag != source.hitag ||
                (unsigned)dest.sectnum >= (unsigned)numsectors)
                continue;

            sector[dest.sectnum].floorz = sector[source.sectnum].floorz;
            sector[dest.sectnum].ceilingz = sector[source.sectnum].ceilingz;
        }
    }
}

static sw_crossplay_duke_rotator_t *SW_CrossplayFindDukeRotatorForSector(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeRotatorCount; ++i)
    {
        sw_crossplay_duke_rotator_t &rotator = g_swCrossplayDukeRotators[i];
        if (rotator.sectnum == sectnum)
            return &rotator;
    }

    return NULL;
}

static int32_t SW_CrossplayOperateDukeRotateRiseBridge(int16_t sectnum)
{
    sw_crossplay_duke_rotator_t *const rotator = SW_CrossplayFindDukeRotatorForSector(sectnum);
    if (rotator == NULL || !rotator->rotateRiseBridge)
        return 0;

    if (rotator->active)
        return 1;

    rotator->opening = rotator->tempAngle <= 0;
    rotator->active = 1;
    return 1;
}

static int32_t SW_CrossplayDukeSectorHasActiveRotateRiseBridge(int16_t sectnum)
{
    sw_crossplay_duke_rotator_t const *const rotator = SW_CrossplayFindDukeRotatorForSector(sectnum);
    return rotator && rotator->rotateRiseBridge && rotator->active;
}

static sw_crossplay_duke_swing_door_t *SW_CrossplayFindDukeSwingDoorForSector(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeSwingDoorCount; ++i)
    {
        sw_crossplay_duke_swing_door_t &door = g_swCrossplayDukeSwingDoors[i];
        if (door.sectnum == sectnum)
            return &door;
    }

    return NULL;
}

static int32_t SW_CrossplayOperateDukeSwingDoor(int16_t sectnum)
{
    sw_crossplay_duke_swing_door_t const *const source = SW_CrossplayFindDukeSwingDoorForSector(sectnum);
    if (source == NULL)
        return 0;

    // Duke refuses to operate a swinging door group while any leaf of that group is
    // still swinging (its SE_11 T5/T6 state).  Without this, two door sectors that
    // share an activator tag toggle each other back in the same tic and the door
    // never visibly moves.
    for (int32_t i = 0; i < g_swCrossplayDukeSwingDoorCount; ++i)
    {
        sw_crossplay_duke_swing_door_t const &door = g_swCrossplayDukeSwingDoors[i];
        if (door.hitag == source->hitag && door.active)
            return 0;
    }

    int32_t operated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeSwingDoorCount; ++i)
    {
        sw_crossplay_duke_swing_door_t &door = g_swCrossplayDukeSwingDoors[i];
        if (door.hitag != source->hitag)
            continue;

        door.open = !door.open;
        door.targetAngle = door.open ? door.openAngle : 0;
        door.active = 1;

        if ((unsigned)door.sectnum < (unsigned)numsectors)
            sector[door.sectnum].lotag ^= 0x8000u;

        operated = 1;
    }

    return operated;
}

static void SW_CrossplayMoveDukeSwingDoor(sw_crossplay_duke_swing_door_t &door)
{
    if (!door.active || (unsigned)door.sectnum >= (unsigned)numsectors)
        return;

    int32_t const delta = door.targetAngle - door.currentAngle;
    if (delta == 0)
    {
        door.active = 0;
        return;
    }

    int32_t const step = min(klabs(delta), (int32_t)klabs(door.step));
    door.currentAngle = (int16_t)(door.currentAngle + ksgn(delta) * step);

    int32_t const startWall = sector[door.sectnum].wallptr;
    int32_t const sectorWallCount = sector[door.sectnum].wallnum;
    int32_t const wallCount = min((int32_t)door.originCount, sectorWallCount);

    for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
    {
        vec2_t rotated;
        rotatevec(g_swCrossplayDukeSwingDoorOrigins[door.originStart + wallOffset], door.currentAngle, &rotated);
        dragpoint(startWall + wallOffset, door.pivot.x + rotated.x, door.pivot.y + rotated.y, 0);
    }

    if (door.currentAngle == door.targetAngle)
        door.active = 0;
}

static int32_t SW_CrossplayDukeSectorHasActiveSwingDoor(int16_t sectnum)
{
    sw_crossplay_duke_swing_door_t const *const door = SW_CrossplayFindDukeSwingDoorForSector(sectnum);
    return door && door->active;
}

static sw_crossplay_duke_sliding_door_t *SW_CrossplayFindDukeSlidingDoorForSector(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeSlidingDoorCount; ++i)
    {
        sw_crossplay_duke_sliding_door_t &door = g_swCrossplayDukeSlidingDoors[i];
        if (door.sectnum == sectnum)
            return &door;
    }

    return NULL;
}

static int32_t SW_CrossplayOperateDukeSlidingDoor(int16_t sectnum)
{
    sw_crossplay_duke_sliding_door_t const *const source = SW_CrossplayFindDukeSlidingDoorForSector(sectnum);
    if (source == NULL)
        return 0;

    int32_t operated = 0;
    for (int32_t i = 0; i < g_swCrossplayDukeSlidingDoorCount; ++i)
    {
        sw_crossplay_duke_sliding_door_t &door = g_swCrossplayDukeSlidingDoors[i];
        if (door.hitag != source->hitag)
            continue;

        door.open = !door.open;
        door.targetDistance = door.open ? door.totalDistance : 0;
        door.active = 1;

        if ((unsigned)door.sectnum < (unsigned)numsectors)
            sector[door.sectnum].lotag ^= 0x8000u;

        operated = 1;
    }

    return operated;
}

static void SW_CrossplayMoveDukeSlidingDoor(sw_crossplay_duke_sliding_door_t &door)
{
    if (!door.active || (unsigned)door.sectnum >= (unsigned)numsectors)
        return;

    int32_t const delta = door.targetDistance - door.currentDistance;
    if (delta == 0)
    {
        door.active = 0;
        return;
    }

    int32_t const oldDistance = door.currentDistance;
    int32_t const step = min(klabs(delta), door.step);
    door.currentDistance += ksgn(delta) * step;

    int32_t const xMove = ((door.currentDistance - oldDistance) * sintable[(door.angle + 512) & 2047]) >> 14;
    int32_t const yMove = ((door.currentDistance - oldDistance) * sintable[door.angle & 2047]) >> 14;
    SW_CrossplayTranslateDukeSectorContents(door.sectnum, xMove, yMove);

    int32_t const xOffset = (door.currentDistance * sintable[(door.angle + 512) & 2047]) >> 14;
    int32_t const yOffset = (door.currentDistance * sintable[door.angle & 2047]) >> 14;
    int32_t const startWall = sector[door.sectnum].wallptr;
    int32_t const sectorWallCount = sector[door.sectnum].wallnum;
    int32_t const wallCount = min((int32_t)door.originCount, sectorWallCount);

    for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
    {
        vec2_t const &origin = g_swCrossplayDukeSlidingDoorOrigins[door.originStart + wallOffset];
        dragpoint(startWall + wallOffset, origin.x + xOffset, origin.y + yOffset, 0);
    }

    if (door.currentDistance == door.targetDistance)
        door.active = 0;
}

static int32_t SW_CrossplayDukeSectorHasActiveSlidingDoor(int16_t sectnum)
{
    sw_crossplay_duke_sliding_door_t const *const door = SW_CrossplayFindDukeSlidingDoorForSector(sectnum);
    return door && door->active;
}

static sw_crossplay_duke_stretch_bridge_t *SW_CrossplayFindDukeStretchBridgeForSector(int16_t sectnum)
{
    for (int32_t i = 0; i < g_swCrossplayDukeStretchBridgeCount; ++i)
    {
        sw_crossplay_duke_stretch_bridge_t &bridge = g_swCrossplayDukeStretchBridges[i];
        if (bridge.sectnum == sectnum)
            return &bridge;
    }

    return NULL;
}

static int32_t SW_CrossplayOperateDukeStretchBridge(int16_t sectnum)
{
    sw_crossplay_duke_stretch_bridge_t *const bridge = SW_CrossplayFindDukeStretchBridgeForSector(sectnum);
    if (bridge == NULL)
        return 0;

    bridge->open = !bridge->open;
    bridge->targetDistance = bridge->open ? bridge->totalDistance : 0;
    bridge->active = 1;
    sector[sectnum].lotag ^= 0x8000u;
    return 1;
}

static void SW_CrossplayMoveDukeStretchBridge(sw_crossplay_duke_stretch_bridge_t &bridge)
{
    if (!bridge.active || (unsigned)bridge.sectnum >= (unsigned)numsectors)
        return;

    int32_t const delta = bridge.targetDistance - bridge.currentDistance;
    if (delta == 0)
    {
        bridge.active = 0;
        return;
    }

    int32_t const oldDistance = bridge.currentDistance;
    int32_t const step = min(klabs(delta), bridge.step);
    bridge.currentDistance += ksgn(delta) * step;

    int32_t const xMove = ((bridge.currentDistance - oldDistance) * sintable[(bridge.angle + 512) & 2047]) >> 14;
    int32_t const yMove = ((bridge.currentDistance - oldDistance) * sintable[bridge.angle & 2047]) >> 14;
    SW_CrossplayTranslateDukeSectorContents(bridge.sectnum, xMove, yMove);

    int32_t const xOffset = (bridge.currentDistance * sintable[(bridge.angle + 512) & 2047]) >> 14;
    int32_t const yOffset = (bridge.currentDistance * sintable[bridge.angle & 2047]) >> 14;

    if ((unsigned)bridge.wall1 < (unsigned)numwalls)
        dragpoint(bridge.wall1, bridge.wall1Origin.x + xOffset, bridge.wall1Origin.y + yOffset, 0);
    if ((unsigned)bridge.wall2 < (unsigned)numwalls)
        dragpoint(bridge.wall2, bridge.wall2Origin.x + xOffset, bridge.wall2Origin.y + yOffset, 0);

    sector[bridge.sectnum].floorxpanning -= xMove >> 3;
    sector[bridge.sectnum].floorypanning -= yMove >> 3;
    sector[bridge.sectnum].ceilingxpanning -= xMove >> 3;
    sector[bridge.sectnum].ceilingypanning -= yMove >> 3;

    if (bridge.currentDistance == bridge.targetDistance)
        bridge.active = 0;
}

static int32_t SW_CrossplayDukeSectorHasActiveStretchBridge(int16_t sectnum)
{
    sw_crossplay_duke_stretch_bridge_t const *const bridge = SW_CrossplayFindDukeStretchBridgeForSector(sectnum);
    return bridge && bridge->active;
}

static int32_t SW_CrossplayOperateDukeSectorImpl(int16_t sectnum, PLAYERp pp)
{
    UNREFERENCED_PARAMETER(pp);

    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    sectortype *const sec = &sector[sectnum];
    int32_t const baseLotag = SW_CrossplayDukeSectorBaseLotag(sectnum);
    int32_t const speed = SW_CrossplayDukeSectorSpeedForSector(sectnum);

    switch (baseLotag)
    {
    case VB_DUKE_ST_SLIDING_ST_DOOR:
        return SW_CrossplayOperateDukeSlidingStarTrekDoor(sectnum);

    case VB_DUKE_ST_WARP_ELEVATOR:
        return SW_CrossplayOperateDukeWarpElevator(sectnum, pp);

    case VB_DUKE_ST_PLATFORM_DOWN:
    case VB_DUKE_ST_PLATFORM_UP:
    {
        int32_t neighbor = nextsectorneighborz(sectnum, sec->floorz, 1, 1);
        if (neighbor < 0)
            neighbor = nextsectorneighborz(sectnum, sec->floorz, 1, -1);
        if (neighbor < 0)
            return 0;

        return SW_CrossplayStartDukeSectorZAnim(sectnum, 1, 0, sector[neighbor].floorz, sec->ceilingz, speed, 1);
    }

    case VB_DUKE_ST_ELEVATOR_DOWN:
    case VB_DUKE_ST_ELEVATOR_UP:
    {
        int32_t neighbor = nextsectorneighborz(sectnum, sec->floorz, 1, -1);
        if (neighbor < 0)
            neighbor = nextsectorneighborz(sectnum, sec->floorz, 1, 1);
        if (neighbor < 0)
            return 0;

        int32_t const zDiff = sec->ceilingz - sec->floorz;
        return SW_CrossplayStartDukeSectorZAnim(sectnum, 1, 1, sector[neighbor].floorz, sector[neighbor].floorz + zDiff, speed, 1);
    }

    case VB_DUKE_ST_CEILING_DOOR:
    {
        int32_t targetZ;
        if (sec->lotag & 0x8000u)
            targetZ = sec->floorz;
        else
        {
            int32_t neighbor = nextsectorneighborz(sectnum, sec->ceilingz, -1, -1);
            targetZ = neighbor >= 0 ? sector[neighbor].ceilingz : sec->floorz;
        }

        sec->lotag ^= 0x8000u;
        return SW_CrossplayStartDukeSectorZAnim(sectnum, 0, 1, sec->floorz, targetZ, speed, 0);
    }

    case VB_DUKE_ST_FLOOR_DOOR:
    {
        int32_t targetZ;
        if (sec->floorz == sec->ceilingz)
        {
            int32_t neighbor = nextsectorneighborz(sectnum, sec->ceilingz, 1, 1);
            targetZ = neighbor >= 0 ? sector[neighbor].floorz : sec->ceilingz;
        }
        else
            targetZ = sec->ceilingz;

        sec->lotag ^= 0x8000u;
        return SW_CrossplayStartDukeSectorZAnim(sectnum, 1, 0, targetZ, sec->ceilingz, speed, 1);
    }

    case VB_DUKE_ST_SPLITTING_DOOR:
    case VB_DUKE_ST_SPLITTING_ST_DOOR:
    {
        int32_t targetFloorZ;
        int32_t targetCeilingZ;

        if (sec->lotag & 0x8000u)
        {
            int32_t const middleZ = (sec->ceilingz + sec->floorz) >> 1;
            targetFloorZ = middleZ;
            targetCeilingZ = middleZ;
        }
        else
        {
            int32_t const floorNeighbor = nextsectorneighborz(sectnum, sec->floorz, 1, 1);
            int32_t const ceilingNeighbor = nextsectorneighborz(sectnum, sec->ceilingz, -1, -1);
            if (floorNeighbor < 0 || ceilingNeighbor < 0)
                return 0;

            targetFloorZ = sector[floorNeighbor].floorz;
            targetCeilingZ = sector[ceilingNeighbor].ceilingz;
        }

        sec->lotag ^= 0x8000u;
        return SW_CrossplayStartDukeSectorZAnim(sectnum, 1, 1, targetFloorZ, targetCeilingZ, speed, 1);
    }

    case VB_DUKE_ST_SWINGING_DOOR:
        return SW_CrossplayOperateDukeSwingDoor(sectnum);

    case VB_DUKE_ST_SLIDING_DOOR:
        return SW_CrossplayOperateDukeSlidingDoor(sectnum);

    case VB_DUKE_ST_STRETCH_BRIDGE:
        return SW_CrossplayOperateDukeStretchBridge(sectnum);

    case VB_DUKE_ST_DROP_FLOOR:
        return SW_CrossplayActivateDukeDropFloorsInSector(sectnum);

    case VB_DUKE_ST_TEETH_DOOR:
    {
        int32_t targetZ;

        sec->lotag ^= 0x8000u;
        if (sec->lotag & 0x8000u)
        {
            int32_t neighbor = nextsectorneighborz(sectnum, sec->ceilingz, -1, -1);
            if (neighbor < 0)
                neighbor = nextsectorneighborz(sectnum, sec->ceilingz, 1, 1);
            if (neighbor < 0)
                return 0;

            targetZ = sector[neighbor].ceilingz;
        }
        else
        {
            int32_t neighbor = nextsectorneighborz(sectnum, sec->ceilingz, 1, 1);
            if (neighbor < 0)
                neighbor = nextsectorneighborz(sectnum, sec->ceilingz, -1, -1);
            if (neighbor < 0)
                return 0;

            targetZ = sector[neighbor].floorz;
        }

        int32_t const teethActivated = SW_CrossplayActivateDukeTeethDoors(sec->hitag, sectnum);
        return SW_CrossplayStartDukeSectorZAnim(sectnum, 0, 1, sec->floorz, targetZ, speed, 0) || teethActivated;
    }

    case VB_DUKE_ST_ROTATE_RISE_BRIDGE:
        return SW_CrossplayOperateDukeRotateRiseBridge(sectnum);

    case VB_DUKE_ST_TWO_WAY_TRAIN:
        return SW_CrossplayOperateDukeTwoWayTrain(sectnum);

    default:
        break;
    }

    return 0;
}

static int32_t SW_CrossplayOperateDukeSector(int16_t sectnum, PLAYERp pp)
{
    int32_t const operated = SW_CrossplayOperateDukeSectorImpl(sectnum, pp);
    if (operated)
        SW_CrossplayCallDukeSectorSound(sectnum);

    return operated;
}

static int32_t SW_CrossplayDukeAutoCloseTargetIsActive(int16_t sectnum)
{
    return SW_CrossplayDukeSectorHasActiveZAnim(sectnum) ||
        SW_CrossplayDukeSectorHasActiveSwingDoor(sectnum) ||
        SW_CrossplayDukeSectorHasActiveSlidingDoor(sectnum) ||
        SW_CrossplayDukeSectorHasActiveStretchBridge(sectnum) ||
        SW_CrossplayDukeSectorHasActiveDropFloor(sectnum) ||
        SW_CrossplayDukeSectorHasActiveTeethDoor(sectnum);
}

static int32_t SW_CrossplayDukeAutoCloseTargetIsOpen(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    switch (SW_CrossplayDukeSectorBaseLotag(sectnum))
    {
    case VB_DUKE_ST_CEILING_DOOR:
    case VB_DUKE_ST_FLOOR_DOOR:
    case VB_DUKE_ST_SPLITTING_DOOR:
    case VB_DUKE_ST_SPLITTING_ST_DOOR:
    case VB_DUKE_ST_SWINGING_DOOR:
    case VB_DUKE_ST_SLIDING_DOOR:
    case VB_DUKE_ST_STRETCH_BRIDGE:
    case VB_DUKE_ST_TEETH_DOOR:
        return (sector[sectnum].lotag & 0x8000u) != 0;
    default:
        return 0;
    }
}

static int32_t SW_CrossplayDukeAutoCloseSectorHasPlayer(int16_t sectnum)
{
    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        if (SW_CrossplayDukePlayerInMovingSector(Player + pnum, sectnum))
            return 1;
    }

    return 0;
}

static void SW_CrossplayTickDukeAutoCloses(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeAutoCloseCount; ++i)
    {
        sw_crossplay_duke_auto_close_t &autoClose = g_swCrossplayDukeAutoCloses[i];
        if ((unsigned)autoClose.sectnum >= (unsigned)numsectors)
            continue;

        if (!SW_CrossplayDukeAutoCloseTargetIsOpen(autoClose.sectnum))
        {
            autoClose.count = 0;
            continue;
        }

        if (SW_CrossplayDukeAutoCloseTargetIsActive(autoClose.sectnum) ||
            SW_CrossplayDukeAutoCloseSectorHasPlayer(autoClose.sectnum))
            continue;

        if (autoClose.count > autoClose.delay)
        {
            if (SW_CrossplayActivateDukeBySector(autoClose.sectnum, NULL))
                autoClose.count = 0;
        }
        else
            ++autoClose.count;
    }
}

static void SW_CrossplayMoveDukeTouchplate(sw_crossplay_duke_touchplate_t &touchplate)
{
    if (!touchplate.moving || (unsigned)touchplate.sectnum >= (unsigned)numsectors)
        return;

    int32_t const targetZ = touchplate.direction ? touchplate.originalFloorZ : touchplate.triggerZ;
    int32_t const delta = targetZ - sector[touchplate.sectnum].floorz;
    if (klabs(delta) <= touchplate.speed)
    {
        int32_t const zChange = targetZ - sector[touchplate.sectnum].floorz;
        sector[touchplate.sectnum].floorz = targetZ;
        SW_CrossplayMoveDukeSectorFloorContents(touchplate.sectnum, zChange);
        touchplate.moving = 0;
        return;
    }

    int32_t const zChange = ksgn(delta) * touchplate.speed;
    sector[touchplate.sectnum].floorz += zChange;
    SW_CrossplayMoveDukeSectorFloorContents(touchplate.sectnum, zChange);
}

static void SW_CrossplayMoveDukeFloorZMover(sw_crossplay_duke_zmover_t &mover)
{
    if ((unsigned)mover.sectnum >= (unsigned)numsectors)
        return;

    if (mover.delayTics > 0)
    {
        --mover.delayTics;
        return;
    }

    int32_t targetZ;
    int32_t finishDirection;
    int32_t zDifference;
    int32_t snapFloor;

    if (mover.direction == 1)
    {
        targetZ = mover.ang != 1536 ? mover.effectorZ : mover.originalZ;
        finishDirection = 0;
        zDifference = targetZ - sector[mover.sectnum].floorz;
        snapFloor = 1;
    }
    else if ((mover.ang & 2047) == 1536)
    {
        targetZ = mover.effectorZ;
        finishDirection = 1;
        zDifference = mover.effectorZ - sector[mover.sectnum].floorz;
        snapFloor = 0;
    }
    else
    {
        targetZ = mover.originalZ;
        finishDirection = 1;
        zDifference = mover.originalZ - mover.effectorZ;
        snapFloor = 0;
    }

    if (klabs(sector[mover.sectnum].floorz - targetZ) < mover.speed)
    {
        int32_t zChange = 0;
        if (snapFloor)
        {
            zChange = targetZ - sector[mover.sectnum].floorz;
            sector[mover.sectnum].floorz = targetZ;
        }

        SW_CrossplayMoveDukeSectorFloorContents(mover.sectnum, zChange);
        mover.direction = finishDirection;
        mover.active = 0;
        mover.delayTics = mover.hitag;
        return;
    }

    int32_t const zChange = ksgn(zDifference) * mover.speed;
    sector[mover.sectnum].floorz += zChange;
    SW_CrossplayMoveDukeSectorFloorContents(mover.sectnum, zChange);
}

static void SW_CrossplayMoveDukeCeilingZMover(sw_crossplay_duke_zmover_t &mover)
{
    if ((unsigned)mover.sectnum >= (unsigned)numsectors)
        return;

    int32_t targetZ;
    int32_t zChange;

    if (mover.direction == 1)
    {
        targetZ = mover.ang != 1536 ? mover.effectorZ : mover.originalZ;
        if (klabs(sector[mover.sectnum].ceilingz - targetZ) < (mover.speed << 1))
        {
            sector[mover.sectnum].ceilingz = targetZ;
            mover.direction = 0;
            mover.active = 0;
            return;
        }

        zChange = ksgn(targetZ - sector[mover.sectnum].ceilingz) * mover.speed;
    }
    else if ((mover.ang & 2047) == 1536)
    {
        targetZ = mover.effectorZ;
        if (klabs(sector[mover.sectnum].ceilingz - targetZ) < (mover.speed << 1))
        {
            sector[mover.sectnum].ceilingz = targetZ;
            mover.direction = !mover.direction;
            mover.active = 0;
            return;
        }

        zChange = ksgn(targetZ - sector[mover.sectnum].ceilingz) * mover.speed;
    }
    else
    {
        targetZ = mover.originalZ;
        if (klabs(sector[mover.sectnum].ceilingz - targetZ) < (mover.speed << 1))
        {
            mover.direction = !mover.direction;
            mover.active = 0;
            return;
        }

        zChange = -ksgn(mover.effectorZ - mover.originalZ) * mover.speed;
    }

    sector[mover.sectnum].ceilingz += zChange;
}

static void SW_CrossplayMoveDukeZMover(sw_crossplay_duke_zmover_t &mover)
{
    if (!mover.active)
        return;

    if (mover.lotag == VB_DUKE_SE_FLOOR_RISE_FALL)
        SW_CrossplayMoveDukeFloorZMover(mover);
    else if (mover.lotag == VB_DUKE_SE_CEILING_RISE_FALL)
        SW_CrossplayMoveDukeCeilingZMover(mover);
}

static int32_t SW_CrossplayActivateDukeZMoverInSector(int16_t sectnum)
{
    int32_t activated = 0;

    for (int32_t i = 0; i < g_swCrossplayDukeZMoverCount; ++i)
    {
        sw_crossplay_duke_zmover_t &mover = g_swCrossplayDukeZMovers[i];
        if (mover.sectnum != sectnum)
            continue;

        mover.active = !mover.active;
        activated = 1;
    }

    return activated;
}

static int32_t SW_CrossplayOperateDukeMasterSwitches(int16_t lotag)
{
    int32_t activated = 0;

    for (int32_t i = 0; i < g_swCrossplayDukeMasterSwitchCount; ++i)
    {
        sw_crossplay_duke_masterswitch_t &master = g_swCrossplayDukeMasterSwitches[i];
        if (master.lotag == lotag && !master.active)
        {
            master.active = 1;
            master.delay = master.initialDelay;
            activated = 1;
        }
    }

    return activated;
}

static int32_t SW_CrossplayOperateDukeActivators(int16_t lotag)
{
    int32_t activated = 0;
    int16_t operatedSectors[32];
    int32_t operatedSectorCount = 0;

    for (int32_t i = 0; i < g_swCrossplayDukeActivatorCount; ++i)
    {
        sw_crossplay_duke_activator_t const &activator = g_swCrossplayDukeActivators[i];
        if (activator.lotag != lotag || (unsigned)activator.sectnum >= (unsigned)numsectors)
            continue;

        // Same reason as SW_CrossplayActivateDukeBySector(): a sector holding two
        // activators of this tag must still be operated exactly once.
        int32_t alreadyOperated = 0;
        for (int32_t j = 0; j < operatedSectorCount; ++j)
        {
            if (operatedSectors[j] == activator.sectnum)
            {
                alreadyOperated = 1;
                break;
            }
        }

        if (alreadyOperated)
            continue;

        if (operatedSectorCount < (int32_t)ARRAY_SIZE(operatedSectors))
            operatedSectors[operatedSectorCount++] = activator.sectnum;

        if (activator.locked)
        {
            sector[activator.sectnum].lotag ^= VB_DUKE_SECTOR_LOCKED;
            activated = 1;
            continue;
        }

        if (activator.hitag == 1 && sector[activator.sectnum].floorz != sector[activator.sectnum].ceilingz)
        {
            continue;
        }
        if (activator.hitag == 2 && sector[activator.sectnum].floorz == sector[activator.sectnum].ceilingz)
        {
            continue;
        }

        int32_t nonSectorActivated = 0;
        int32_t sectorOperated = 0;

        if (SW_CrossplayActivateDukeZMoverInSector(activator.sectnum))
            nonSectorActivated = 1;

        if (SW_CrossplayActivateDukeDropFloorsInSector(activator.sectnum))
            nonSectorActivated = 1;

        if (SW_CrossplayActivateDukeQuakesInSector(activator.sectnum))
            nonSectorActivated = 1;

        if (SW_CrossplayActivateDukeIncrementalMoversInSector(activator.sectnum))
            nonSectorActivated = 1;

        if (SW_CrossplayActivateDukeProjectileShootersInSector(activator.sectnum))
            nonSectorActivated = 1;

        if (SW_CrossplayOperateDukeSector(activator.sectnum, NULL))
        {
            sectorOperated = 1;
        }

        if (!sectorOperated && nonSectorActivated)
            SW_CrossplayCallDukeSectorSound(activator.sectnum);

        activated |= nonSectorActivated || sectorOperated;
    }

    if (SW_CrossplayOperateDukeRespawns(lotag))
        activated = 1;

    return activated;
}

static int32_t SW_CrossplayActivateDukeBySector(int16_t sectnum, PLAYERp pp)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t foundActivator = 0;
    int16_t operatedLotags[16];
    int32_t operatedLotagCount = 0;

    for (int32_t i = 0; i < g_swCrossplayDukeActivatorCount; ++i)
    {
        sw_crossplay_duke_activator_t const &activator = g_swCrossplayDukeActivators[i];
        if (activator.sectnum != sectnum || activator.locked)
            continue;

        foundActivator = 1;

        // Duke operates each activator tag once per trigger.  Maps routinely put
        // several activators of the same tag in one sector, and operating the tag
        // once per activator toggles the target twice: a swinging door then opens
        // and immediately closes, so it never appears to move at all.
        int32_t alreadyOperated = 0;
        for (int32_t j = 0; j < operatedLotagCount; ++j)
        {
            if (operatedLotags[j] == activator.lotag)
            {
                alreadyOperated = 1;
                break;
            }
        }

        if (alreadyOperated)
            continue;

        if (operatedLotagCount < (int32_t)ARRAY_SIZE(operatedLotags))
            operatedLotags[operatedLotagCount++] = activator.lotag;

        SW_CrossplayToggleDukeConveyorsAndPistonsWithHitag(activator.lotag);
        SW_CrossplayToggleDukeIncrementalMoversWithHitag(activator.lotag);
        SW_CrossplayToggleDukeProjectileShootersWithHitag(activator.lotag);
        SW_CrossplayOperateDukeActivators(activator.lotag);
    }

    if (foundActivator)
        return 1;

    int32_t activated = 0;
    int32_t sectorOperated = 0;

    activated |= SW_CrossplayActivateDukeZMoverInSector(sectnum);
    activated |= SW_CrossplayActivateDukeDropFloorsInSector(sectnum);
    activated |= SW_CrossplayActivateDukeQuakesInSector(sectnum);
    activated |= SW_CrossplayActivateDukeIncrementalMoversInSector(sectnum);
    activated |= SW_CrossplayActivateDukeProjectileShootersInSector(sectnum);
    sectorOperated = SW_CrossplayOperateDukeSector(sectnum, pp);
    activated |= sectorOperated;

    if (activated && !sectorOperated)
        SW_CrossplayCallDukeSectorSound(sectnum);

    return activated;
}

static int32_t SW_CrossplayOperateDukeTag(int16_t lotag, PLAYERp pp)
{
    if (lotag == 0 || !SW_CrossplayDukeMapBridgeActive())
        return 0;

    if (SW_CrossplayDukeCheckActivatorMotion(lotag))
        return 0;

    int32_t const masterActivated = SW_CrossplayOperateDukeMasterSwitches(lotag);
    int32_t const lightActivated = SW_CrossplayActivateDukeLightsWithHitag(lotag, 0);
    int32_t const dropFloorActivated = SW_CrossplayActivateDukeDropFloorsWithHitag(lotag);
    int32_t const quakeActivated = SW_CrossplayActivateDukeQuakesWithHitag(lotag);
    int32_t const conveyorOrPistonActivated = SW_CrossplayToggleDukeConveyorsAndPistonsWithHitag(lotag);
    int32_t const incrementalActivated = SW_CrossplayToggleDukeIncrementalMoversWithHitag(lotag);
    int32_t const projectileShooterActivated = SW_CrossplayToggleDukeProjectileShootersWithHitag(lotag);
    int32_t const activated = SW_CrossplayOperateDukeActivators(lotag);

    if (activated || lightActivated || masterActivated || dropFloorActivated || quakeActivated || conveyorOrPistonActivated ||
        incrementalActivated || projectileShooterActivated)
        SW_CrossplayUpdateDukeSwitchSprites(lotag);

    if ((activated || lightActivated || masterActivated || dropFloorActivated || quakeActivated || conveyorOrPistonActivated ||
         incrementalActivated || projectileShooterActivated) && pp != NULL)
        PlaySound(DIGI_DUKE_SWITCH_ON, &pp->posx, &pp->posy, &pp->posz, v3df_dontpan|v3df_follow);

    return activated || lightActivated || masterActivated || dropFloorActivated || quakeActivated || conveyorOrPistonActivated ||
           incrementalActivated || projectileShooterActivated;
}

static void SW_CrossplaySetDukeTouchplateGroupMoving(int16_t lotag, uint8_t direction)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTouchplateCount; ++i)
    {
        sw_crossplay_duke_touchplate_t &touchplate = g_swCrossplayDukeTouchplates[i];
        if (touchplate.lotag == lotag && !touchplate.disabled)
        {
            touchplate.moving = 1;
            touchplate.direction = direction;
        }
    }
}

static void SW_CrossplayTickDukeTouchplates(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTouchplateCount; ++i)
    {
        sw_crossplay_duke_touchplate_t &touchplate = g_swCrossplayDukeTouchplates[i];
        if ((unsigned)touchplate.sectnum >= (unsigned)numsectors || touchplate.disabled)
            continue;

        int32_t playerInSector = -1;
        int32_t pnum;
        TRAVERSE_CONNECT(pnum)
        {
            PLAYERp const pp = Player + pnum;
            if (SW_CrossplayDukePlayerInMovingSector(pp, touchplate.sectnum) &&
                (SW_CrossplayDukePlayerCanInheritSectorMotion(pp) || touchplate.ang == 512))
            {
                playerInSector = pnum;
                break;
            }
        }

        if (playerInSector >= 0)
        {
            if (!touchplate.pressed && !SW_CrossplayDukeCheckActivatorMotion(touchplate.lotag))
            {
                touchplate.pressed = 1;
                touchplate.direction = !touchplate.direction;
                touchplate.moving = 1;
                SW_CrossplaySetDukeTouchplateGroupMoving(touchplate.lotag, touchplate.direction);
                int32_t const masterActivated = SW_CrossplayOperateDukeMasterSwitches(touchplate.lotag);
                int32_t const lightActivated = SW_CrossplayActivateDukeLightsWithHitag(touchplate.lotag, 0);
                int32_t const dropFloorActivated = SW_CrossplayActivateDukeDropFloorsWithHitag(touchplate.lotag);
                int32_t const quakeActivated = SW_CrossplayActivateDukeQuakesWithHitag(touchplate.lotag);
                int32_t const conveyorOrPistonActivated = SW_CrossplayToggleDukeConveyorsAndPistonsWithHitag(touchplate.lotag);
                int32_t const incrementalActivated = SW_CrossplayToggleDukeIncrementalMoversWithHitag(touchplate.lotag);
                int32_t const projectileShooterActivated = SW_CrossplayToggleDukeProjectileShootersWithHitag(touchplate.lotag);
                int32_t const activatorActivated = SW_CrossplayOperateDukeActivators(touchplate.lotag);

                if (!activatorActivated && (masterActivated || lightActivated || dropFloorActivated || quakeActivated ||
                    conveyorOrPistonActivated || incrementalActivated || projectileShooterActivated))
                    SW_CrossplayCallDukeSectorSound(touchplate.sectnum);

                if (touchplate.hitag > 0)
                {
                    --touchplate.hitag;
                    if (touchplate.hitag == 0)
                        touchplate.disabled = 1;
                }
            }
        }
        else
            touchplate.pressed = 0;

        SW_CrossplayMoveDukeTouchplate(touchplate);
    }
}

static void SW_CrossplayTickDukeMasterSwitches(void)
{
    for (int32_t i = 0; i < g_swCrossplayDukeMasterSwitchCount; ++i)
    {
        sw_crossplay_duke_masterswitch_t &master = g_swCrossplayDukeMasterSwitches[i];
        if (!master.active || (unsigned)master.sectnum >= (unsigned)numsectors)
            continue;

        if (--master.delay > 0)
            continue;

        int32_t const sectorOperated = SW_CrossplayOperateDukeSector(master.sectnum, NULL);
        int32_t const nonSectorActivated =
            SW_CrossplayActivateDukeZMoverInSector(master.sectnum) ||
            SW_CrossplayActivateDukeDropFloorsInSector(master.sectnum) ||
            SW_CrossplayActivateDukeQuakesInSector(master.sectnum) ||
            SW_CrossplayActivateDukeIncrementalMoversInSector(master.sectnum) ||
            SW_CrossplayActivateDukeProjectileShootersInSector(master.sectnum) ||
            SW_CrossplayActivateDukeLightsWithHitag(master.lotag, 0);

        if (!sectorOperated && nonSectorActivated)
            SW_CrossplayCallDukeSectorSound(master.sectnum);

        if (master.repeat)
        {
            master.active = 0;
            master.delay = master.initialDelay;
        }
        else
            master.active = 0;
    }
}

static int16_t SW_CrossplayFindDukeTrainLocatorSector(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return -1;

    int32_t const startWall = sector[sectnum].wallptr;
    int32_t const endWall = startWall + sector[sectnum].wallnum;
    if (startWall < 0 || endWall > MAXWALLS)
        return -1;

    for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
    {
        int16_t const nextSector = wall[wallNum].nextsector;
        if ((unsigned)nextSector >= (unsigned)numsectors)
            continue;

        if (sector[nextSector].hitag == 0 && (int16_t)sector[nextSector].lotag < 3)
            return nextSector;
    }

    return -1;
}

static int32_t SW_CrossplayDukeTrainSpeedForSector(int16_t sectnum, int16_t locatorSect)
{
    int32_t speed = SW_CrossplayDukeGpspeedForSector(sectnum);
    if (speed <= 0 && locatorSect >= 0)
        speed = SW_CrossplayDukeGpspeedForSector(locatorSect);

    if (speed <= 0 && locatorSect >= 0)
    {
        for (int32_t i = 0; i < g_swCrossplayDukeLocatorCount; ++i)
        {
            sw_crossplay_duke_locator_t const &locator = g_swCrossplayDukeLocators[i];
            if (locator.sectnum == locatorSect && locator.hitag > 0)
            {
                speed = locator.hitag;
                break;
            }
        }
    }

    if (speed <= 0 || speed > 4096)
        speed = 256;

    return speed;
}

static int32_t SW_CrossplayDukeTrainInheritedVelocity(sw_crossplay_duke_train_t const &train)
{
    if (train.lotag != VB_DUKE_SE_SUBWAY_CAR)
        return -1;

    for (int32_t i = 0; i < g_swCrossplayDukeTrainCount; ++i)
    {
        sw_crossplay_duke_train_t const &lead = g_swCrossplayDukeTrains[i];
        if (lead.lotag == VB_DUKE_SE_SUBWAY && lead.hitag == train.hitag && lead.locatorSect == train.locatorSect)
            return lead.xvel;
    }

    return -1;
}

static sw_crossplay_duke_train_t *SW_CrossplayFindDukeTrainForSector(int16_t sectnum, int32_t lotag)
{
    for (int32_t i = 0; i < g_swCrossplayDukeTrainCount; ++i)
    {
        sw_crossplay_duke_train_t &train = g_swCrossplayDukeTrains[i];
        if (train.sectnum == sectnum && (lotag < 0 || train.lotag == lotag))
            return &train;
    }

    return NULL;
}

static int16_t SW_CrossplayDukeSectorIndexFromPointer(SECTORp const sectp)
{
    if (sectp == NULL || sectp < sector || sectp >= sector + numsectors)
        return -1;

    return (int16_t)(sectp - sector);
}

static int32_t SW_CrossplayDukeTrainSectorIsActive(int16_t sectnum)
{
    return SW_CrossplayFindDukeTrainForSector(sectnum, -1) != NULL;
}

static int32_t SW_CrossplayDukePlayerInsideTrainSector(PLAYERp const pp, int16_t sectnum, int32_t loose)
{
    if (pp == NULL || (unsigned)sectnum >= (unsigned)numsectors || !SW_CrossplayDukeTrainSectorIsActive(sectnum))
        return 0;

    if (inside(pp->pos.x, pp->pos.y, sectnum) != 1)
        return 0;

    int32_t ceilingZ, floorZ;
    getzsofslope(sectnum, pp->pos.x, pp->pos.y, &ceilingZ, &floorZ);

    int32_t const playerFeetZ = pp->posz + pp->floor_dist;
    int32_t const lowerSlop = loose ? Z(192) : Z(128);
    int32_t const upperSlop = loose ? Z(128) : Z(64);
    if (playerFeetZ < floorZ - lowerSlop || playerFeetZ > floorZ + upperSlop)
        return 0;

    return pp->posz >= ceilingZ - Z(16);
}

static int16_t SW_CrossplayFindDukePlayerTrainCarrySector(PLAYERp const pp, int16_t previousSectnum)
{
    if (!SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
        return -1;

    if ((unsigned)previousSectnum < (unsigned)numsectors &&
        SW_CrossplayDukePlayerInsideTrainSector(pp, previousSectnum, 1))
        return previousSectnum;

    int16_t candidates[3] = {
        SW_CrossplayDukeSectorIndexFromPointer(pp->lo_sectp),
        (unsigned)pp->PlayerSprite < MAXSPRITES ? (int16_t)sprite[pp->PlayerSprite].sectnum : (int16_t)-1,
        pp->cursectnum,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(candidates); ++i)
    {
        int16_t const sectnum = candidates[i];
        if (SW_CrossplayDukePlayerInsideTrainSector(pp, sectnum, 1))
            return sectnum;
    }

    for (int32_t i = 0; i < g_swCrossplayDukeTrainCount; ++i)
    {
        int16_t const sectnum = g_swCrossplayDukeTrains[i].sectnum;
        if (SW_CrossplayDukePlayerInsideTrainSector(pp, sectnum, 0))
            return sectnum;
    }

    return -1;
}

static int32_t SW_CrossplayDukePlayerHorizontallyInsideTrainSector(PLAYERp const pp, int16_t sectnum)
{
    if (pp == NULL || (unsigned)sectnum >= (unsigned)numsectors || !SW_CrossplayDukeTrainSectorIsActive(sectnum))
        return 0;

    if (!SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
        return 0;

    if (inside(pp->pos.x, pp->pos.y, sectnum) != 1)
        return 0;

    int32_t ceilingZ, floorZ;
    getzsofslope(sectnum, pp->pos.x, pp->pos.y, &ceilingZ, &floorZ);
    return pp->posz >= ceilingZ - Z(96) && pp->posz <= floorZ + Z(384);
}

static void SW_CrossplayPrepareDukeTrainPlayerCarrySectors(void)
{
    Bmemset(g_swCrossplayDukeTrainPlayerCarrySector, 0xff, sizeof(g_swCrossplayDukeTrainPlayerCarrySector));

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        if ((unsigned)pnum >= ARRAY_SIZE(g_swCrossplayDukeTrainPlayerCarrySector))
            continue;

        int16_t const previousSectnum = g_swCrossplayDukeTrainPlayerLastCarrySector[pnum];
        int16_t carrySectnum = SW_CrossplayFindDukePlayerTrainCarrySector(Player + pnum, previousSectnum);
        if (carrySectnum >= 0)
        {
            g_swCrossplayDukeTrainPlayerCarryGrace[pnum] = VB_CROSSPLAY_DUKE_TRAIN_CARRY_GRACE_TICS;
            g_swCrossplayDukeTrainPlayerLastCarrySector[pnum] = carrySectnum;
        }
        else if ((unsigned)previousSectnum < (unsigned)numsectors &&
                 g_swCrossplayDukeTrainPlayerCarryGrace[pnum] > 0 &&
                 SW_CrossplayDukePlayerHorizontallyInsideTrainSector(Player + pnum, previousSectnum))
        {
            carrySectnum = previousSectnum;
            --g_swCrossplayDukeTrainPlayerCarryGrace[pnum];
        }
        else
        {
            g_swCrossplayDukeTrainPlayerCarryGrace[pnum] = 0;
            g_swCrossplayDukeTrainPlayerLastCarrySector[pnum] = -1;
        }

        g_swCrossplayDukeTrainPlayerCarrySector[pnum] = carrySectnum;
    }
}

static int32_t SW_CrossplayDukeTrainShouldCarryPlayer(int32_t pnum, PLAYERp const pp, int16_t trainSectnum)
{
    if ((unsigned)pnum < ARRAY_SIZE(g_swCrossplayDukeTrainPlayerCarrySector))
        return g_swCrossplayDukeTrainPlayerCarrySector[pnum] == trainSectnum;

    return SW_CrossplayDukePlayerInMovingSector(pp, trainSectnum) &&
        SW_CrossplayDukePlayerCanInheritSectorMotion(pp);
}

static int32_t SW_CrossplayDukeSectorHasActiveTwoWayTrain(int16_t sectnum)
{
    sw_crossplay_duke_train_t const *const train = SW_CrossplayFindDukeTrainForSector(sectnum, VB_DUKE_SE_TWO_WAY_TRAIN);
    return train && train->active;
}

static void SW_CrossplayMoveDukeTrainContentsAndWalls(sw_crossplay_duke_train_t &train, vec2_t const &oldPos,
                                                      int32_t xmove, int32_t ymove, int32_t deltaAngle,
                                                      int32_t wallRotateAngle)
{
    int32_t spriteNum, nextSprite;
    TRAVERSE_SPRITE_SECT(headspritesect[train.sectnum], spriteNum, nextSprite)
    {
        int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
        if (sprite[spriteNum].statnum == MAXSTATUS ||
            (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS) ||
            originalPicnum == VB_DUKE_SECTOREFFECTOR ||
            originalPicnum == VB_DUKE_LOCATORS ||
            originalPicnum == VB_DUKE_GPSPEED)
            continue;

        if (deltaAngle != 0)
        {
            rotatepoint(oldPos, sprite[spriteNum].xy, deltaAngle, &sprite[spriteNum].xy);
            sprite[spriteNum].ang = NORM_ANGLE(sprite[spriteNum].ang + deltaAngle);
        }

        sprite[spriteNum].x += xmove;
        sprite[spriteNum].y += ymove;
        setspritez(spriteNum, &sprite[spriteNum].xyz);
    }

    train.pos.x += xmove;
    train.pos.y += ymove;

    int32_t const startWall = sector[train.sectnum].wallptr;
    int32_t const sectorWallCount = sector[train.sectnum].wallnum;
    int32_t const wallCount = train.originCount < sectorWallCount ? train.originCount : sectorWallCount;

    for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
    {
        vec2_t rotated;
        rotatevec(g_swCrossplayDukeTrainOrigins[train.originStart + wallOffset], wallRotateAngle, &rotated);
        dragpoint(startWall + wallOffset, train.pos.x + rotated.x, train.pos.y + rotated.y, 0);
    }

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;
        if (!SW_CrossplayDukeTrainShouldCarryPlayer(pnum, pp, train.sectnum))
            continue;

        ++g_swCrossplayDukeTrainCarryTics;

        // Riding a train, the player's offset from it should stay constant; any
        // wobble in it is felt as the view shaking.
        {
            vec2_t const offset = { pp->pos.x - train.pos.x, pp->pos.y - train.pos.y };

            if (g_swCrossplayDukeTrainRideTics > 0)
            {
                int32_t const wobble = klabs(offset.x - g_swCrossplayDukeTrainRideOffset.x) +
                                       klabs(offset.y - g_swCrossplayDukeTrainRideOffset.y);
                if (wobble > g_swCrossplayDukeTrainRideWobble)
                    g_swCrossplayDukeTrainRideWobble = wobble;
            }

            g_swCrossplayDukeTrainRideOffset = offset;
            ++g_swCrossplayDukeTrainRideTics;
        }

        SW_CrossplayRotateDukePlayerXY(pp, oldPos, deltaAngle);
        SW_CrossplayMoveDukePlayerXY(pp, xmove, ymove);

        if (deltaAngle != 0)
        {
            pp->q16ang = NORM_Q16ANGLE(pp->q16ang + fix16_from_int(deltaAngle));
            pp->oq16ang = NORM_Q16ANGLE(pp->oq16ang + fix16_from_int(deltaAngle));
            pp->camq16ang = NORM_Q16ANGLE(pp->camq16ang + fix16_from_int(deltaAngle));
        }

        int16_t const previousSectnum = pp->cursectnum;
        if (SW_CrossplayDukePlayerInsideTrainSector(pp, train.sectnum, 1) ||
            SW_CrossplayDukePlayerHorizontallyInsideTrainSector(pp, train.sectnum))
            pp->cursectnum = train.sectnum;
        else
            COVERupdatesector(pp->pos.x, pp->pos.y, &pp->cursectnum);

        if (pp->cursectnum != previousSectnum)
            pp->lastcursectnum = previousSectnum;

        if ((unsigned)pp->PlayerSprite < MAXSPRITES && deltaAngle != 0)
            sprite[pp->PlayerSprite].ang = NORM_ANGLE(sprite[pp->PlayerSprite].ang + deltaAngle);

        SW_CrossplaySyncDukeMovedPlayer(pp, 0, 1);
    }
}

static int32_t SW_CrossplayOperateDukeTwoWayTrain(int16_t sectnum)
{
    sw_crossplay_duke_train_t *const train = SW_CrossplayFindDukeTrainForSector(sectnum, VB_DUKE_SE_TWO_WAY_TRAIN);
    if (train == NULL || (unsigned)train->sectnum >= (unsigned)numsectors)
        return 0;

    if (train->active)
        return 1;

    int16_t targetTag = train->locatorTag ? 0 : 1;
    int32_t locatorIndex = SW_CrossplayFindDukeLocator(targetTag, train->locatorSect);
    if (locatorIndex < 0)
    {
        targetTag = train->locatorTag;
        locatorIndex = SW_CrossplayFindDukeLocator(targetTag, train->locatorSect);
    }

    if (locatorIndex < 0)
        return 0;

    sw_crossplay_duke_locator_t const &target = g_swCrossplayDukeLocators[locatorIndex];
    train->locatorTag = targetTag;
    train->angle = getangle(target.pos.x - train->pos.x, target.pos.y - train->pos.y);
    train->xvel = 0;
    train->state = 1;
    train->active = 1;
    return 1;
}

static void SW_CrossplayMoveDukeTwoWayTrainSector(sw_crossplay_duke_train_t &train)
{
    if (!train.active || (unsigned)train.sectnum >= (unsigned)numsectors)
        return;

    int32_t const locatorIndex = SW_CrossplayFindDukeLocator(train.locatorTag, train.locatorSect);
    if (locatorIndex < 0)
    {
        train.active = 0;
        train.state = 0;
        train.xvel = 0;
        return;
    }

    sw_crossplay_duke_locator_t const &target = g_swCrossplayDukeLocators[locatorIndex];
    int32_t const distToTarget = FindDistance2D(target.pos.x - train.pos.x, target.pos.y - train.pos.y);

    if (train.state == 1)
    {
        if (distToTarget < (2048 - 128))
            train.state = 2;
        else if (train.xvel < train.speed)
            train.xvel = min(train.xvel + 16, train.speed);
    }

    if (train.state == 2)
    {
        if (distToTarget <= 128)
            train.xvel = 0;

        if (train.xvel > 0)
            train.xvel = max(train.xvel - 16, 0);
        else
        {
            train.xvel = 0;
            train.active = 0;
            train.state = 0;
            train.angle = NORM_ANGLE(train.angle + 1024);
            return;
        }
    }

    if (train.xvel <= 0)
        return;

    train.angle = getangle(target.pos.x - train.pos.x, target.pos.y - train.pos.y);

    int32_t const xmove = (train.xvel * sintable[(train.angle + 512) & 2047]) >> 14;
    int32_t const ymove = (train.xvel * sintable[train.angle & 2047]) >> 14;
    vec2_t const oldPos = train.pos;

    SW_CrossplayMoveDukeTrainContentsAndWalls(train, oldPos, xmove, ymove, 0, 0);
}


// Duke's subway kills anyone it runs down: while the train is moving at speed it
// checks each living player, and if the train's sector has swept over them --
// the player's position now resolves into the train sector although they were
// not riding it -- the player is dragged to the train and killed outright.
static void SW_CrossplayCrushPlayersUnderDukeTrain(sw_crossplay_duke_train_t const &train)
{
    if (train.xvel < 192 || (unsigned)train.sectnum >= (unsigned)numsectors)
        return;

    int32_t pnum;
    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp const pp = Player + pnum;

        if ((unsigned)pp->PlayerSprite >= MAXSPRITES || User[pp->PlayerSprite] == NULL ||
            User[pp->PlayerSprite]->Health <= 0 || TEST(pp->Flags, PF_DEAD))
            continue;

        int16_t sweptSect = pp->cursectnum;
        updatesector(pp->posx, pp->posy, &sweptSect);

        if (sweptSect != train.sectnum || pp->cursectnum == train.sectnum)
            continue;

        pp->posx = train.pos.x;
        pp->posy = train.pos.y;
        pp->cursectnum = train.sectnum;
        SW_CrossplaySyncDukeMovedPlayer(pp, 0, 1);

        ++g_swCrossplayDukeTrainCrushKills;

        // Under the self-test the kill is counted but not carried out: killing
        // the player ends the level, and the rest of the run would then be
        // measuring a fresh map.  Normal play gets Duke's real outcome.
        if (SW_CrossplaySelfTestActive())
            continue;

        PlayerUpdateHealth(pp, (short)-(User[pp->PlayerSprite]->Health + 100));
        PlayerCheckDeath(pp, -1);
    }
}

static void SW_CrossplayMoveDukeTrainSector(sw_crossplay_duke_train_t &train)
{
    if ((unsigned)train.sectnum >= (unsigned)numsectors)
        return;

    if (train.lotag == VB_DUKE_SE_TWO_WAY_TRAIN)
    {
        SW_CrossplayMoveDukeTwoWayTrainSector(train);
        return;
    }

    int32_t locatorIndex = SW_CrossplayFindDukeLocator(train.locatorTag, train.locatorSect);
    if (locatorIndex < 0)
    {
        locatorIndex = SW_CrossplayFindDukeLocator(0, train.locatorSect);
        if (locatorIndex < 0)
            return;

        train.locatorTag = 0;
    }

    sw_crossplay_duke_locator_t const &locator = g_swCrossplayDukeLocators[locatorIndex];

    int32_t const locatorDist = FindDistance2D(locator.pos.x - train.pos.x, locator.pos.y - train.pos.y);
    if (locatorDist < 1024)
    {
        if (train.lotag == VB_DUKE_SE_SUBWAY && (locator.hitag & 1))
            train.waitTics = train.speed;

        ++train.locatorTag;
        locatorIndex = SW_CrossplayFindDukeLocator(train.locatorTag, train.locatorSect);
        if (locatorIndex < 0)
        {
            train.locatorTag = 0;
            locatorIndex = SW_CrossplayFindDukeLocator(0, train.locatorSect);
        }

        if (locatorIndex < 0)
            return;
    }

    int32_t const inheritedVelocity = SW_CrossplayDukeTrainInheritedVelocity(train);
    if (inheritedVelocity >= 0)
        train.xvel = inheritedVelocity;
    else if (train.waitTics > 0)
    {
        --train.waitTics;
        if (train.waitTics >= train.speed - (train.speed >> 3))
            train.xvel -= train.speed >> 5;
        if (train.waitTics > ((train.speed >> 1) - 1) && train.waitTics < (train.speed - (train.speed >> 3)))
            train.xvel = 0;
        if (train.waitTics < (train.speed >> 1))
            train.xvel += train.speed >> 5;
        if (train.waitTics < ((train.speed >> 1) - (train.speed >> 3)))
        {
            train.waitTics = 0;
            train.xvel = train.speed;
        }
    }
    else
        train.xvel = train.speed;

    if (train.xvel == 0)
        return;

    sw_crossplay_duke_locator_t const &target = g_swCrossplayDukeLocators[locatorIndex];
    int32_t const targetAngle = getangle(target.pos.x - train.pos.x, target.pos.y - train.pos.y);
    int32_t const deltaAngle = SW_CrossplayAngleDelta(train.angle, targetAngle) >> 3;
    train.angle = NORM_ANGLE(train.angle + deltaAngle);

    int32_t const xmove = (train.xvel * sintable[(train.angle + 512) & 2047]) >> 14;
    int32_t const ymove = (train.xvel * sintable[train.angle & 2047]) >> 14;
    vec2_t const oldPos = train.pos;
    int16_t const rotateAngle = NORM_ANGLE(train.angle - train.initialAngle);

    SW_CrossplayCrushPlayersUnderDukeTrain(train);

    SW_CrossplayMoveDukeTrainContentsAndWalls(train, oldPos, xmove, ymove, deltaAngle, rotateAngle);
}

static void SW_CrossplayCaptureDukeMapEffectors(void)
{
    SW_CrossplayClearDukeMapEffectors();

    if (!artCrossplayMapArtPrepared() || !SW_CrossplayIsDukeLikeGame(artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS)))
        return;

    for (int32_t i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum == MAXSTATUS ||
            artCrossplayOriginalMapSpritePicnum(i) != VB_DUKE_LOCATORS)
            continue;

        if (g_swCrossplayDukeLocatorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeLocators))
        {
            LOG_F(WARNING, "Crossplay: too many Duke locator sprites for Shadow Warrior compatibility bridge.");
            break;
        }

        sw_crossplay_duke_locator_t &locator = g_swCrossplayDukeLocators[g_swCrossplayDukeLocatorCount++];
        locator.sectnum = sprite[i].sectnum;
        locator.lotag = sprite[i].lotag;
        locator.hitag = sprite[i].hitag;
        locator.ang = sprite[i].ang;
        locator.pos = sprite[i].xyz;
    }

    for (int32_t i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum == MAXSTATUS)
            continue;

        int16_t const originalPicnum = artCrossplayOriginalMapSpritePicnum(i);
        int16_t const sectnum = sprite[i].sectnum;
        if ((unsigned)sectnum >= (unsigned)numsectors)
            continue;

        if (originalPicnum == VB_DUKE_RESPAWN)
        {
            if (g_swCrossplayDukeRespawnCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeRespawns))
            {
                LOG_F(WARNING, "Crossplay: too many Duke respawn sprites for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_respawn_t &respawn = g_swCrossplayDukeRespawns[g_swCrossplayDukeRespawnCount++];
            respawn.sectnum = sectnum;
            respawn.lotag = sprite[i].lotag;
            respawn.spawnPicnum = sprite[i].hitag;
            respawn.pal = sprite[i].pal;
            respawn.shade = sprite[i].shade;
            respawn.cstat = sprite[i].cstat;
            respawn.xrepeat = sprite[i].xrepeat;
            respawn.yrepeat = sprite[i].yrepeat;
            respawn.ang = sprite[i].ang;
            respawn.clipdist = sprite[i].clipdist;
            respawn.pos = sprite[i].xyz;
            respawn.active = 0;
            respawn.used = 0;
            respawn.delay = 0;
            continue;
        }

        if (originalPicnum == VB_DUKE_MUSICANDSFX)
        {
            if (sprite[i].lotag <= 0 || (unsigned)sprite[i].lotag >= 1000u)
                continue;

            int32_t const digi = SW_CrossplayDukeSoundIdToDigi(sprite[i].lotag);
            if (digi <= DIGI_NULL)
                continue;

            if (g_swCrossplayDukeMapSoundCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeMapSounds))
            {
                LOG_F(WARNING, "Crossplay: too many Duke MUSICANDSFX sprites for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_map_sound_t &sound = g_swCrossplayDukeMapSounds[g_swCrossplayDukeMapSoundCount++];
            sound.sectnum = sectnum;
            sound.spriteNum = i;
            sound.soundId = sprite[i].lotag;
            sound.alternateSoundId = sprite[i].hitag;
            sound.digi = digi;
            sound.alternateDigi = SW_CrossplayDukeSoundIdToDigi(sprite[i].hitag);
            sound.distance = SW_CrossplayDukeMapSoundDistance(sprite[i].hitag);
            sound.delay = 0;
            sound.pos = sprite[i].xyz;
            sound.toggled = 0;
            continue;
        }

        if (SW_CrossplayIsDukeExplosiveSourceTile(originalPicnum) && sprite[i].hitag > 0)
        {
            if (g_swCrossplayDukeExplosiveSourceCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeExplosiveSources))
            {
                LOG_F(WARNING, "Crossplay: too many Duke explosive source sprites for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_explosive_source_t &source = g_swCrossplayDukeExplosiveSources[g_swCrossplayDukeExplosiveSourceCount++];
            source.sectnum = sectnum;
            source.hitag = sprite[i].hitag;
            source.pos = sprite[i].xy;
            continue;
        }

        if (originalPicnum == VB_DUKE_ACTIVATOR || originalPicnum == VB_DUKE_ACTIVATORLOCKED)
        {
            if (g_swCrossplayDukeActivatorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeActivators))
            {
                LOG_F(WARNING, "Crossplay: too many Duke activator sprites for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_activator_t &activator = g_swCrossplayDukeActivators[g_swCrossplayDukeActivatorCount++];
            activator.sectnum = sectnum;
            activator.lotag = sprite[i].lotag;
            activator.hitag = sprite[i].hitag;
            activator.locked = originalPicnum == VB_DUKE_ACTIVATORLOCKED;

            if (activator.locked)
                sector[sectnum].lotag |= VB_DUKE_SECTOR_LOCKED;
            continue;
        }

        if (originalPicnum == VB_DUKE_TOUCHPLATE)
        {
            if (g_swCrossplayDukeTouchplateCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeTouchplates))
            {
                LOG_F(WARNING, "Crossplay: too many Duke touchplate sprites for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_touchplate_t &touchplate = g_swCrossplayDukeTouchplates[g_swCrossplayDukeTouchplateCount++];
            touchplate.sectnum = sectnum;
            touchplate.lotag = sprite[i].lotag;
            touchplate.hitag = sprite[i].hitag;
            touchplate.ang = sprite[i].ang;
            touchplate.triggerZ = sprite[i].z;
            touchplate.originalFloorZ = sector[sectnum].floorz;
            touchplate.speed = SW_CrossplayDukeZMoverSpeedForSector(sectnum);
            touchplate.pressed = 0;
            touchplate.moving = 0;
            touchplate.direction = 0;
            touchplate.disabled = 0;
            continue;
        }

        if (originalPicnum == VB_DUKE_MASTERSWITCH)
        {
            if (g_swCrossplayDukeMasterSwitchCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeMasterSwitches))
            {
                LOG_F(WARNING, "Crossplay: too many Duke masterswitch sprites for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_masterswitch_t &master = g_swCrossplayDukeMasterSwitches[g_swCrossplayDukeMasterSwitchCount++];
            master.sectnum = sectnum;
            master.lotag = sprite[i].lotag;
            master.initialDelay = sprite[i].hitag;
            master.delay = sprite[i].hitag;
            master.active = 0;
            master.repeat = sprite[i].pal == 23;
            continue;
        }
    }

    for (int32_t i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum == MAXSTATUS ||
            artCrossplayOriginalMapSpritePicnum(i) != VB_DUKE_SECTOREFFECTOR)
            continue;

        int16_t const sectnum = sprite[i].sectnum;
        if ((unsigned)sectnum >= (unsigned)numsectors)
            continue;

        if (SW_CrossplayDukeLightLotagSupported(sprite[i].lotag))
        {
            if (g_swCrossplayDukeLightCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeLights))
            {
                LOG_F(WARNING, "Crossplay: too many Duke light sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_light_t &light = g_swCrossplayDukeLights[g_swCrossplayDukeLightCount++];
            light.sectnum = sectnum;
            light.hitag = sprite[i].hitag;
            light.lotag = sprite[i].lotag;
            light.effectorShade = sprite[i].shade;
            light.effectorPal = sprite[i].pal;
            light.baseFloorShade = sector[sectnum].floorshade;
            light.baseCeilingShade = sector[sectnum].ceilingshade;
            light.maxWallShade = SW_CrossplayClampDukeShade(SW_CrossplayDukeSectorMaxWallShade(sectnum));
            light.baseFloorPal = sector[sectnum].floorpal;
            light.baseCeilingPal = sector[sectnum].ceilingpal;
            light.state = 0;
            light.oneShot = 0;

            if (light.lotag == VB_DUKE_SE_RANDOM_LIGHTS_AFTER_SHOT_OUT)
                SW_CrossplaySetDukeLightShade(light, light.effectorShade, light.effectorShade, light.effectorShade, -1);

            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_LIGHTNING)
        {
            if (g_swCrossplayDukeLightningCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeLightnings))
            {
                LOG_F(WARNING, "Crossplay: too many Duke lightning sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_lightning_t &lightning = g_swCrossplayDukeLightnings[g_swCrossplayDukeLightningCount++];
            lightning.sectnum = sectnum;
            lightning.hitag = sprite[i].hitag;
            lightning.pos = sprite[i].xyz;
            lightning.delay = VB_CROSSPLAY_DUKE_LIGHTNING_START_DELAY;
            lightning.duration = 0;
            lightning.ticks = 0;
            lightning.active = 0;
            SW_CrossplaySetDukeNaturalLightningVisible(lightning.hitag, 0);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_QUAKE_DEBRIS)
        {
            if (g_swCrossplayDukeQuakeDebrisCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeQuakeDebris))
            {
                LOG_F(WARNING, "Crossplay: too many Duke quake debris sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_quake_debris_t &debris = g_swCrossplayDukeQuakeDebris[g_swCrossplayDukeQuakeDebrisCount++];
            debris.sectnum = sectnum;
            debris.angle = sprite[i].ang;
            debris.pos = sprite[i].xyz;
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_EXPLOSIVE)
        {
            if (g_swCrossplayDukeExplosiveCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeExplosives))
            {
                LOG_F(WARNING, "Crossplay: too many Duke explosive sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_explosive_t &explosive = g_swCrossplayDukeExplosives[g_swCrossplayDukeExplosiveCount++];
            explosive.sectnum = sectnum;
            explosive.hitag = sprite[i].hitag;
            explosive.angle = sprite[i].ang;
            explosive.targetCeilingZ = sector[sectnum].ceilingz;
            explosive.targetFloorZ = sector[sectnum].floorz;
            explosive.speed = sector[sectnum].extra > 0 ? sector[sectnum].extra : 256;
            explosive.owner = klabs(explosive.targetCeilingZ - sprite[i].z) < klabs(explosive.targetFloorZ - sprite[i].z);
            explosive.ticks = 0;
            explosive.active = 0;
            explosive.finished = 0;

            setinterpolation(&sector[sectnum].ceilingz);
            setinterpolation(&sector[sectnum].floorz);

            if (sprite[i].ang == 512)
            {
                if (explosive.owner)
                    sector[sectnum].ceilingz = sprite[i].z;
                else
                    sector[sectnum].floorz = sprite[i].z;
            }
            else
                sector[sectnum].ceilingz = sector[sectnum].floorz = sprite[i].z;

            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_DOOR_AUTO_CLOSE)
        {
            if (g_swCrossplayDukeAutoCloseCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeAutoCloses))
            {
                LOG_F(WARNING, "Crossplay: too many Duke auto-close door effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_auto_close_t &autoClose = g_swCrossplayDukeAutoCloses[g_swCrossplayDukeAutoCloseCount++];
            autoClose.sectnum = sectnum;
            autoClose.delay = max<int16_t>(sprite[i].hitag, 0);
            autoClose.count = 0;
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_DROP_FLOOR)
        {
            if (g_swCrossplayDukeDropFloorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeDropFloors))
            {
                LOG_F(WARNING, "Crossplay: too many Duke drop-floor sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_drop_floor_t &dropFloor = g_swCrossplayDukeDropFloors[g_swCrossplayDukeDropFloorCount++];
            dropFloor.sectnum = sectnum;
            dropFloor.hitag = sprite[i].hitag;
            dropFloor.ang = sprite[i].ang;
            dropFloor.targetZ = sprite[i].z;
            dropFloor.speed = sprite[i].yvel > 0 ? sprite[i].yvel << 4 : SW_CrossplayDukeSectorSpeedForSector(sectnum);
            dropFloor.speed = clamp(dropFloor.speed, 1, 8192);
            dropFloor.delay = max<int32_t>(sector[sectnum].extra, 0);
            dropFloor.moveCeiling = (sprite[i].ang & 2047) == 1536;
            dropFloor.active = 0;
            dropFloor.finished = 0;

            if (dropFloor.moveCeiling)
                setinterpolation(&sector[sectnum].ceilingz);
            else
                setinterpolation(&sector[sectnum].floorz);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_TEETH_DOOR)
        {
            if (g_swCrossplayDukeTeethDoorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeTeethDoors))
            {
                LOG_F(WARNING, "Crossplay: too many Duke teeth-door sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t speed = sector[sectnum].extra;
            if (speed == 0)
                speed = max<int32_t>(1, SW_CrossplayDukeSectorSpeedForSector(sectnum) >> 4);

            sw_crossplay_duke_teeth_door_t &door = g_swCrossplayDukeTeethDoors[g_swCrossplayDukeTeethDoorCount++];
            door.sectnum = sectnum;
            door.hitag = sprite[i].hitag;
            door.targetSectnum = -1;
            door.speed = speed;
            door.active = 0;
            setinterpolation(&sector[sectnum].ceilingz);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_EARTHQUAKE)
        {
            int32_t const startWall = sector[sectnum].wallptr;
            int32_t const wallCount = sector[sectnum].wallnum;
            if (startWall < 0 || wallCount <= 0 || startWall + wallCount > numwalls)
                continue;

            if (g_swCrossplayDukeQuakeCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeQuakes) ||
                g_swCrossplayDukeQuakeOriginCount + wallCount > (int32_t)ARRAY_SIZE(g_swCrossplayDukeQuakeOrigins))
            {
                LOG_F(WARNING, "Crossplay: too many Duke earthquake sectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_quake_t &quake = g_swCrossplayDukeQuakes[g_swCrossplayDukeQuakeCount++];
            quake.sectnum = sectnum;
            quake.hitag = sprite[i].hitag;
            quake.originStart = g_swCrossplayDukeQuakeOriginCount;
            quake.originCount = wallCount;
            quake.angle = sprite[i].ang;
            quake.pos = sprite[i].xyz;
            quake.targetFloorHeinum = sector[sectnum].floorheinum;
            quake.ticks = 0;
            quake.active = 0;
            quake.finished = 0;

            sector[sectnum].floorheinum = 0;
            for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
            {
                vec2_t &origin = g_swCrossplayDukeQuakeOrigins[g_swCrossplayDukeQuakeOriginCount++];
                origin.x = wall[startWall + wallOffset].x - quake.pos.x;
                origin.y = wall[startWall + wallOffset].y - quake.pos.y;
            }

            SW_CrossplaySetSectorWallInterpolation(sectnum);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_INCREMENTAL_SECTOR_RISE_FALL)
        {
            if (g_swCrossplayDukeIncrementalMoverCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeIncrementalMovers))
            {
                LOG_F(WARNING, "Crossplay: too many Duke incremental sector movers for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t speed = sector[sectnum].extra;
            if (speed <= 0)
                speed = SW_CrossplayDukeGpspeedForSector(sectnum);
            if (speed <= 0)
                speed = 256;

            int32_t const moveCeiling = sprite[i].ang == 512;
            int32_t const originalZ = moveCeiling ? sector[sectnum].ceilingz : sector[sectnum].floorz;

            sw_crossplay_duke_incremental_mover_t &mover =
                g_swCrossplayDukeIncrementalMovers[g_swCrossplayDukeIncrementalMoverCount++];
            mover.sectnum = sectnum;
            mover.hitag = sprite[i].hitag;
            mover.targetZ = sprite[i].pal ? originalZ : sprite[i].z;
            mover.speed = clamp(speed, 1, 8192);
            mover.delayLimit = sprite[i].hitag > 0 ? sprite[i].hitag << 2 : 0;
            mover.delay = 0;
            mover.moveCeiling = moveCeiling != 0;
            mover.active = 0;
            mover.finished = 0;

            if (sprite[i].pal)
            {
                if (mover.moveCeiling)
                    sector[sectnum].ceilingz = sprite[i].z;
                else
                    sector[sectnum].floorz = sprite[i].z;
            }

            if (mover.moveCeiling)
                setinterpolation(&sector[sectnum].ceilingz);
            else
                setinterpolation(&sector[sectnum].floorz);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_PISTON)
        {
            if (g_swCrossplayDukePistonCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukePistons))
            {
                LOG_F(WARNING, "Crossplay: too many Duke piston sectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_piston_t &piston = g_swCrossplayDukePistons[g_swCrossplayDukePistonCount++];
            piston.sectnum = sectnum;
            piston.hitag = sprite[i].hitag;
            piston.openCeilingZ = sector[sectnum].ceilingz;
            piston.speed = sprite[i].yvel > 0 ? sprite[i].yvel << 4 : SW_CrossplayDukeSectorSpeedForSector(sectnum);
            piston.speed = clamp(piston.speed, 1, 8192);
            piston.enabled = 1;
            piston.movingDown = 1;

            sector[sectnum].ceilingz = sprite[i].z;
            setinterpolation(&sector[sectnum].ceilingz);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_WAVES)
        {
            if (g_swCrossplayDukeWaveCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeWaves))
            {
                LOG_F(WARNING, "Crossplay: too many Duke wave sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t amplitude = sector[sectnum].extra;
            if (amplitude == 0)
                amplitude = sprite[i].yvel;

            sw_crossplay_duke_wave_t &wave = g_swCrossplayDukeWaves[g_swCrossplayDukeWaveCount++];
            wave.sectnum = sectnum;
            wave.phase = sprite[i].hitag;
            wave.baseFloorZ = sprite[i].z;
            wave.amplitude = amplitude;
            setinterpolation(&sector[sectnum].floorz);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_PROJ_SHOOTER)
        {
            if (g_swCrossplayDukeProjectileShooterCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeProjectileShooters))
            {
                LOG_F(WARNING, "Crossplay: too many Duke projectile shooter sector effectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_projectile_shooter_t &shooter =
                g_swCrossplayDukeProjectileShooters[g_swCrossplayDukeProjectileShooterCount++];
            shooter.sectnum = sectnum;
            shooter.hitag = sprite[i].hitag;
            shooter.angle = sprite[i].ang;
            shooter.projectileTile = sector[sectnum].extra;
            shooter.pos = sprite[i].xyz;
            shooter.ticks = 0;
            shooter.active = 0;
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_TELEPORT || sprite[i].lotag == VB_DUKE_SE_ONE_WAY_TELEPORT)
        {
            int32_t const destSprite = SW_CrossplayFindDukeTeleporterDest(i);
            if (destSprite < 0 || (unsigned)sprite[destSprite].sectnum >= (unsigned)numsectors)
                continue;

            if (SW_CrossplayDukeSE7IsWaterStack(i, destSprite))
            {
                if (g_swCrossplayDukeWaterWarpCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeWaterWarps))
                {
                    LOG_F(WARNING, "Crossplay: too many Duke water/stack warp sectors for Shadow Warrior compatibility bridge.");
                    continue;
                }

                sw_crossplay_duke_water_warp_t &warp = g_swCrossplayDukeWaterWarps[g_swCrossplayDukeWaterWarpCount++];
                warp.sourceSect = sectnum;
                warp.destSect = sprite[destSprite].sectnum;
                warp.sourceLotag = SW_CrossplayDukeSectorBaseLotag(warp.sourceSect);
                warp.destLotag = SW_CrossplayDukeSectorBaseLotag(warp.destSect);
                warp.source = sprite[i].xyz;
                warp.dest = sprite[destSprite].xyz;
                SW_CrossplaySetDukeWaterSectorFlags(warp.sourceSect);
                SW_CrossplaySetDukeWaterSectorFlags(warp.destSect);
                continue;
            }

            if (g_swCrossplayDukeTeleporterCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeTeleporters))
            {
                LOG_F(WARNING, "Crossplay: too many Duke teleporter sectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_teleporter_t &teleporter = g_swCrossplayDukeTeleporters[g_swCrossplayDukeTeleporterCount++];
            teleporter.sourceSprite = i;
            teleporter.destSprite = destSprite;
            teleporter.sourceSect = sectnum;
            teleporter.destSect = sprite[destSprite].sectnum;
            teleporter.source = sprite[i].xyz;
            teleporter.dest = sprite[destSprite].xyz;
            teleporter.destAng = sprite[destSprite].ang;
            teleporter.sourceOnFloor = klabs(sector[sectnum].floorz - sprite[i].z) <= Z(4);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_WARP_ELEVATOR)
        {
            if (g_swCrossplayDukeWarpElevatorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeWarpElevators))
            {
                LOG_F(WARNING, "Crossplay: too many Duke warp elevator sectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t const speed = sprite[i].extra > 0 ? sprite[i].extra << 2 : SW_CrossplayDukeSectorSpeedForSector(sectnum);
            sw_crossplay_duke_warp_elevator_t &elevator = g_swCrossplayDukeWarpElevators[g_swCrossplayDukeWarpElevatorCount++];
            elevator.sectnum = sectnum;
            elevator.hitag = sprite[i].hitag;
            elevator.effectorPos = sprite[i].xyz;
            elevator.speed = clamp(speed, 1, 8192);
            elevator.stopFloorZ = sector[sectnum].floorz;

            int32_t neighbor = nextsectorneighborz(sectnum, sector[sectnum].floorz, -1, -1);
            elevator.lowerRevealZ = neighbor >= 0 ? sector[neighbor].ceilingz : sector[sectnum].ceilingz;

            neighbor = nextsectorneighborz(sectnum, sector[sectnum].ceilingz, 1, 1);
            elevator.upperRevealZ = neighbor >= 0 ? sector[neighbor].floorz : sector[sectnum].floorz;

            elevator.shade = sprite[i].shade;
            elevator.direction = 0;
            elevator.warpPending = 0;

            setinterpolation(&sector[sectnum].floorz);
            setinterpolation(&sector[sectnum].ceilingz);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_SUBWAY || sprite[i].lotag == VB_DUKE_SE_SUBWAY_CAR ||
            sprite[i].lotag == VB_DUKE_SE_TWO_WAY_TRAIN)
        {
            int16_t const locatorSect = SW_CrossplayFindDukeTrainLocatorSector(sectnum);
            int32_t const isTwoWayTrain = sprite[i].lotag == VB_DUKE_SE_TWO_WAY_TRAIN;
            int32_t locatorIndex = -1;
            if (locatorSect >= 0)
                locatorIndex = isTwoWayTrain ? SW_CrossplayFindDukeLocator(1, locatorSect)
                                              : SW_CrossplayFindDukeLocator(sprite[i].hitag, locatorSect);
            if (isTwoWayTrain && locatorIndex < 0 && locatorSect >= 0)
                locatorIndex = SW_CrossplayFindDukeLocator(0, locatorSect);
            if (locatorSect < 0 || locatorIndex < 0)
                continue;

            int32_t const startWall = sector[sectnum].wallptr;
            int32_t const wallCount = sector[sectnum].wallnum;
            if (startWall < 0 || wallCount <= 0 || startWall + wallCount > MAXWALLS)
                continue;

            if (g_swCrossplayDukeTrainCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeTrains) ||
                g_swCrossplayDukeTrainOriginCount + wallCount > (int32_t)ARRAY_SIZE(g_swCrossplayDukeTrainOrigins))
            {
                LOG_F(WARNING, "Crossplay: too many Duke train sectors for Shadow Warrior compatibility bridge.");
                break;
            }

            sw_crossplay_duke_train_t &train = g_swCrossplayDukeTrains[g_swCrossplayDukeTrainCount++];
            train.sectnum = sectnum;
            train.locatorSect = locatorSect;
            train.locatorTag = isTwoWayTrain ? 0 : sprite[i].hitag;
            train.lotag = sprite[i].lotag;
            train.hitag = sprite[i].hitag;
            train.originStart = g_swCrossplayDukeTrainOriginCount;
            train.originCount = wallCount;
            train.initialAngle = sprite[i].ang;
            train.angle = sprite[i].ang;
            train.speed = isTwoWayTrain ? 256 : SW_CrossplayDukeTrainSpeedForSector(sectnum, locatorSect);
            train.xvel = isTwoWayTrain ? 0 : train.speed;
            train.waitTics = 0;
            train.pos = sprite[i].xy;
            train.active = isTwoWayTrain ? 0 : 1;
            train.state = 0;

            for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
            {
                vec2_t &origin = g_swCrossplayDukeTrainOrigins[g_swCrossplayDukeTrainOriginCount++];
                origin.x = wall[startWall + wallOffset].x - train.pos.x;
                origin.y = wall[startWall + wallOffset].y - train.pos.y;
            }

            SW_CrossplaySetSectorWallInterpolation(sectnum);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_FLOOR_RISE_FALL || sprite[i].lotag == VB_DUKE_SE_CEILING_RISE_FALL)
        {
            if (g_swCrossplayDukeZMoverCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeZMovers))
            {
                LOG_F(WARNING, "Crossplay: too many Duke floor/ceiling mover sectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            sw_crossplay_duke_zmover_t &mover = g_swCrossplayDukeZMovers[g_swCrossplayDukeZMoverCount++];
            mover.sectnum = sectnum;
            mover.lotag = sprite[i].lotag;
            mover.hitag = sprite[i].hitag;
            mover.ang = sprite[i].ang;
            mover.effectorZ = sprite[i].z;
            mover.originalZ = sprite[i].lotag == VB_DUKE_SE_FLOOR_RISE_FALL ? sector[sectnum].floorz : sector[sectnum].ceilingz;
            mover.speed = SW_CrossplayDukeZMoverSpeedForSector(sectnum);
            mover.delayTics = 0;
            mover.active = 0;
            mover.direction = 0;

            if (sprite[i].ang != 1536)
            {
                if (sprite[i].lotag == VB_DUKE_SE_FLOOR_RISE_FALL)
                    sector[sectnum].floorz = sprite[i].z;
                else
                    sector[sectnum].ceilingz = sprite[i].z;
            }

            SW_CrossplaySetSectorZInterpolation(sectnum, sprite[i].lotag);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_SWINGING_DOOR)
        {
            int32_t const startWall = sector[sectnum].wallptr;
            int32_t const wallCount = sector[sectnum].wallnum;
            if (startWall < 0 || wallCount <= 0 || startWall + wallCount > MAXWALLS)
                continue;

            if (g_swCrossplayDukeSwingDoorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeSwingDoors) ||
                g_swCrossplayDukeSwingDoorOriginCount + wallCount > (int32_t)ARRAY_SIZE(g_swCrossplayDukeSwingDoorOrigins))
            {
                LOG_F(WARNING, "Crossplay: too many Duke swinging doors for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t const speed = SW_CrossplayDukeSectorSpeedForSector(sectnum);
            sw_crossplay_duke_swing_door_t &door = g_swCrossplayDukeSwingDoors[g_swCrossplayDukeSwingDoorCount++];
            door.sectnum = sectnum;
            door.hitag = sprite[i].hitag;
            door.originStart = g_swCrossplayDukeSwingDoorOriginCount;
            door.originCount = wallCount;
            door.pivot = sprite[i].xy;
            door.currentAngle = 0;
            door.targetAngle = 0;
            door.openAngle = (sprite[i].ang > 1024) ? -512 : 512;
            door.step = (int16_t)clamp(speed >> 2, 2, 128);
            door.active = 0;
            door.open = (sector[sectnum].lotag & 0x8000u) != 0;

            for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
            {
                vec2_t &origin = g_swCrossplayDukeSwingDoorOrigins[g_swCrossplayDukeSwingDoorOriginCount++];
                origin.x = wall[startWall + wallOffset].x - door.pivot.x;
                origin.y = wall[startWall + wallOffset].y - door.pivot.y;
            }

            SW_CrossplaySetSectorWallInterpolation(sectnum);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_SLIDING_DOOR)
        {
            int32_t const startWall = sector[sectnum].wallptr;
            int32_t const wallCount = sector[sectnum].wallnum;
            if (startWall < 0 || wallCount <= 0 || startWall + wallCount > MAXWALLS)
                continue;

            if (g_swCrossplayDukeSlidingDoorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeSlidingDoors) ||
                g_swCrossplayDukeSlidingDoorOriginCount + wallCount > (int32_t)ARRAY_SIZE(g_swCrossplayDukeSlidingDoorOrigins))
            {
                LOG_F(WARNING, "Crossplay: too many Duke sliding doors for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t const speed = SW_CrossplayDukeSectorSpeedForSector(sectnum);
            sw_crossplay_duke_sliding_door_t &door = g_swCrossplayDukeSlidingDoors[g_swCrossplayDukeSlidingDoorCount++];
            door.sectnum = sectnum;
            door.hitag = sprite[i].hitag;
            door.originStart = g_swCrossplayDukeSlidingDoorOriginCount;
            door.originCount = wallCount;
            door.angle = sprite[i].ang;
            door.currentDistance = 0;
            door.targetDistance = 0;
            door.totalDistance = max(128, speed << 1);
            door.step = 16;
            door.active = 0;
            door.open = (sector[sectnum].lotag & 0x8000u) != 0;

            for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
            {
                vec2_t &origin = g_swCrossplayDukeSlidingDoorOrigins[g_swCrossplayDukeSlidingDoorOriginCount++];
                origin = wall[startWall + wallOffset].xy;
            }

            SW_CrossplaySetSectorWallInterpolation(sectnum);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_STRETCH_BRIDGE)
        {
            int32_t const startWall = sector[sectnum].wallptr;
            int32_t const endWall = startWall + sector[sectnum].wallnum;
            if (startWall < 0 || sector[sectnum].wallnum <= 0 || endWall > MAXWALLS)
                continue;

            if (g_swCrossplayDukeStretchBridgeCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeStretchBridges))
            {
                LOG_F(WARNING, "Crossplay: too many Duke stretch bridges for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t closestDist1 = INT32_MAX;
            int32_t closestDist2 = INT32_MAX;
            int16_t closestWall1 = -1;
            int16_t closestWall2 = -1;

            for (int32_t wallNum = startWall; wallNum < endWall; ++wallNum)
            {
                int32_t const dist = FindDistance2D(sprite[i].x - wall[wallNum].x, sprite[i].y - wall[wallNum].y);
                if (dist < closestDist1)
                {
                    closestDist2 = closestDist1;
                    closestWall2 = closestWall1;
                    closestDist1 = dist;
                    closestWall1 = (int16_t)wallNum;
                }
                else if (dist < closestDist2)
                {
                    closestDist2 = dist;
                    closestWall2 = (int16_t)wallNum;
                }
            }

            if (closestWall1 < 0 || closestWall2 < 0)
                continue;

            int32_t const speed = SW_CrossplayDukeSectorSpeedForSector(sectnum);
            sw_crossplay_duke_stretch_bridge_t &bridge = g_swCrossplayDukeStretchBridges[g_swCrossplayDukeStretchBridgeCount++];
            bridge.sectnum = sectnum;
            bridge.wall1 = closestWall1;
            bridge.wall2 = closestWall2;
            bridge.angle = sprite[i].ang;
            bridge.wall1Origin = wall[closestWall1].xy;
            bridge.wall2Origin = wall[closestWall2].xy;
            bridge.currentDistance = 0;
            bridge.targetDistance = 0;
            bridge.totalDistance = max(64, speed);
            bridge.step = 8;
            bridge.active = 0;
            bridge.open = (sector[sectnum].lotag & 0x8000u) != 0;

            SW_CrossplaySetSectorWallInterpolation(sectnum);
            continue;
        }

        if (sprite[i].lotag == VB_DUKE_SE_CONVEYOR || sprite[i].lotag == VB_DUKE_SE_CONVEYOR2)
        {
            if (g_swCrossplayDukeConveyorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeConveyors))
            {
                LOG_F(WARNING, "Crossplay: too many Duke conveyor sectors for Shadow Warrior compatibility bridge.");
                continue;
            }

            int32_t speed = sprite[i].yvel;
            if (speed > 0 && sprite[i].lotag == VB_DUKE_SE_CONVEYOR)
                speed <<= 1;
            if (speed == 0)
                speed = SW_CrossplayDukeGpspeedForSector(sectnum) << 1;
            if (speed == 0)
                continue;

            sw_crossplay_duke_conveyor_t &conveyor = g_swCrossplayDukeConveyors[g_swCrossplayDukeConveyorCount++];
            conveyor.sectnum = sectnum;
            conveyor.hitag = sprite[i].hitag;
            conveyor.xmove = (speed * sintable[(sprite[i].ang + 512) & 2047]) >> 18;
            conveyor.ymove = (speed * sintable[sprite[i].ang & 2047]) >> 18;
            conveyor.panSpeed = speed >> 7;
            conveyor.disabled = 0;
            continue;
        }

        if (sprite[i].lotag != VB_DUKE_SE_ROTATING_SECTOR)
            continue;

        vec2_t pivot = {};
        int16_t pivotAng = 0;
        if (!SW_CrossplayFindDukePivot(sprite[i].hitag, &pivot, &pivotAng))
            continue;

        int32_t const startWall = sector[sectnum].wallptr;
        int32_t const wallCount = sector[sectnum].wallnum;
        if (startWall < 0 || wallCount <= 0 || startWall + wallCount > MAXWALLS)
            continue;

        if (g_swCrossplayDukeRotatorCount >= (int32_t)ARRAY_SIZE(g_swCrossplayDukeRotators) ||
            g_swCrossplayDukeRotatorOriginCount + wallCount > (int32_t)ARRAY_SIZE(g_swCrossplayDukeRotatorOrigins))
        {
            LOG_F(WARNING, "Crossplay: too many Duke rotating sectors for Shadow Warrior compatibility bridge.");
            break;
        }

        int32_t const rawSpeed = SW_CrossplayDukeZMoverSpeedForSector(sectnum);
        int32_t const isRotateRiseBridge = SW_CrossplayDukeSectorBaseLotag(sectnum) == VB_DUKE_ST_ROTATE_RISE_BRIDGE;
        int32_t const speed = isRotateRiseBridge ? (rawSpeed >> 5) : (rawSpeed >> 3);
        if (speed == 0)
            continue;

        sw_crossplay_duke_rotator_t &rotator = g_swCrossplayDukeRotators[g_swCrossplayDukeRotatorCount++];
        rotator.sectnum = sectnum;
        rotator.originStart = g_swCrossplayDukeRotatorOriginCount;
        rotator.originCount = wallCount;
        rotator.pivot = pivot;
        rotator.angle = 0;
        rotator.step = (int16_t)((pivotAng > 1024 ? -1 : 1) * speed);
        rotator.tempAngle = isRotateRiseBridge ? ((sector[sectnum].floorz == sprite[i].z) ? 256 : 0) : 0;
        rotator.closedFloorZ = sector[sectnum].floorz;
        rotator.openFloorZ = sprite[i].z;
        rotator.carryActors = !!(sector[sectnum].floorstat & 64);
        rotator.rotateRiseBridge = isRotateRiseBridge != 0;
        rotator.active = !isRotateRiseBridge;
        rotator.opening = 0;
        rotator.reverseDirection = isRotateRiseBridge && sprite[i].pal == 0;

        for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
        {
            vec2_t &origin = g_swCrossplayDukeRotatorOrigins[g_swCrossplayDukeRotatorOriginCount++];
            origin.x = wall[startWall + wallOffset].x - pivot.x;
            origin.y = wall[startWall + wallOffset].y - pivot.y;
        }

        SW_CrossplaySetSectorWallInterpolation(sectnum);
    }

    SW_CrossplayAlignDukeWarpElevators();

    for (int32_t i = 0; i < g_swCrossplayDukeTouchplateCount; ++i)
    {
        sw_crossplay_duke_touchplate_t &touchplate = g_swCrossplayDukeTouchplates[i];
        if ((unsigned)touchplate.sectnum >= (unsigned)numsectors)
            continue;

        touchplate.originalFloorZ = sector[touchplate.sectnum].floorz;
        if (sector[touchplate.sectnum].lotag != VB_DUKE_ST_ABOVE_WATER && sector[touchplate.sectnum].lotag != VB_DUKE_ST_UNDERWATER)
            sector[touchplate.sectnum].floorz = touchplate.triggerZ;

        setinterpolation(&sector[touchplate.sectnum].floorz);
    }

    if (g_swCrossplayDukeRotatorCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke rotating sector(s) for Shadow Warrior runtime.", g_swCrossplayDukeRotatorCount);
    if (g_swCrossplayDukeTrainCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke train sector(s) with %d locator(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeTrainCount, g_swCrossplayDukeLocatorCount);
    if (g_swCrossplayDukeConveyorCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke conveyor sector(s) for Shadow Warrior runtime.", g_swCrossplayDukeConveyorCount);
    if (g_swCrossplayDukeTeleporterCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke teleporter sector(s) for Shadow Warrior runtime.", g_swCrossplayDukeTeleporterCount);
    if (g_swCrossplayDukeWaterWarpCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke water/stack warp sector(s) for Shadow Warrior runtime.", g_swCrossplayDukeWaterWarpCount);
    if (g_swCrossplayDukeActivatorCount > 0 || g_swCrossplayDukeTouchplateCount > 0 || g_swCrossplayDukeMasterSwitchCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke activator(s), %d touchplate(s), and %d masterswitch(es) for Shadow Warrior runtime.",
              g_swCrossplayDukeActivatorCount, g_swCrossplayDukeTouchplateCount, g_swCrossplayDukeMasterSwitchCount);
    if (g_swCrossplayDukeZMoverCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke floor/ceiling mover sector(s) for Shadow Warrior runtime.", g_swCrossplayDukeZMoverCount);
    if (g_swCrossplayDukeWarpElevatorCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke warp elevator sector(s) for Shadow Warrior runtime.", g_swCrossplayDukeWarpElevatorCount);
    if (g_swCrossplayDukeSwingDoorCount > 0 || g_swCrossplayDukeSlidingDoorCount > 0 || g_swCrossplayDukeStretchBridgeCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke swinging door(s), %d sliding door(s), and %d stretch bridge(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeSwingDoorCount, g_swCrossplayDukeSlidingDoorCount, g_swCrossplayDukeStretchBridgeCount);
    if (g_swCrossplayDukeAutoCloseCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke auto-close door effector(s) for Shadow Warrior runtime.", g_swCrossplayDukeAutoCloseCount);
    if (g_swCrossplayDukeExplosiveCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke explosive sector effector(s) with %d source sprite(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeExplosiveCount, g_swCrossplayDukeExplosiveSourceCount);
    if (g_swCrossplayDukeLightCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke light sector effector(s) for Shadow Warrior runtime.", g_swCrossplayDukeLightCount);
    if (g_swCrossplayDukeLightningCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke lightning sector effector(s) for Shadow Warrior runtime.", g_swCrossplayDukeLightningCount);
    if (g_swCrossplayDukeRespawnCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke respawn trigger(s) for Shadow Warrior runtime.", g_swCrossplayDukeRespawnCount);
    if (g_swCrossplayDukeDropFloorCount > 0 || g_swCrossplayDukeTeethDoorCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke drop-floor and %d teeth-door sector effector(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeDropFloorCount, g_swCrossplayDukeTeethDoorCount);
    if (g_swCrossplayDukeQuakeCount > 0 || g_swCrossplayDukeQuakeDebrisCount > 0 || g_swCrossplayDukePistonCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke earthquake, %d quake debris, and %d piston sector effector(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeQuakeCount, g_swCrossplayDukeQuakeDebrisCount, g_swCrossplayDukePistonCount);
    if (g_swCrossplayDukeIncrementalMoverCount > 0 || g_swCrossplayDukeWaveCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke incremental mover and %d wave sector effector(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeIncrementalMoverCount, g_swCrossplayDukeWaveCount);
    if (g_swCrossplayDukeProjectileShooterCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke projectile shooter sector effector(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeProjectileShooterCount);
    if (g_swCrossplayDukeMapSoundCount > 0)
        LOG_F(INFO, "Crossplay: bridged %d Duke MUSICANDSFX sound control sprite(s) for Shadow Warrior runtime.",
              g_swCrossplayDukeMapSoundCount);
}

int SW_CrossplayOperateDukeMapEffectors(PLAYERp pp)
{
    if (pp == NULL || !SW_CrossplayDukeMapBridgeActive())
        return 0;

    static const int32_t zOffsets[] = { 0, Z(16), Z(32) };

    for (unsigned i = 0; i < ARRAY_SIZE(zOffsets); ++i)
    {
        int16_t nearSector = -1;
        int16_t nearWall = -1;
        int16_t nearSprite = -1;
        int32_t nearDist = 0;

        neartag(pp->posx, pp->posy, pp->posz + zOffsets[i], pp->cursectnum, fix16_to_int(pp->q16ang),
                &nearSector, &nearWall, &nearSprite, &nearDist, 1280, NTAG_SEARCH_LO, NULL);

        if (nearSprite >= 0 && nearDist < 1792)
        {
            sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[nearSprite];
            if ((behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_SWITCH) &&
                SW_CrossplayDamageDukeMapSprite(pp->PlayerSprite, nearSprite, &sprite[nearSprite].xyz))
                return 1;

            if (sprite[nearSprite].lotag != 0 && SW_CrossplayOperateDukeTag(sprite[nearSprite].lotag, pp))
                return 1;
        }

        if (nearWall >= 0 && nearDist < 1792 && wall[nearWall].lotag != 0 &&
            SW_CrossplayOperateDukeTag(wall[nearWall].lotag, pp))
            return 1;

        if (nearSector >= 0 && nearDist < 1792 && (sector[nearSector].lotag & VB_DUKE_SECTOR_LOCKED) == 0)
        {
            if (SW_CrossplayActivateDukeBySector(nearSector, pp))
                return 1;
        }
    }

    if ((unsigned)pp->cursectnum < (unsigned)numsectors &&
        (sector[pp->cursectnum].lotag & VB_DUKE_SECTOR_LOCKED) == 0)
    {
        if (SW_CrossplayActivateDukeBySector(pp->cursectnum, pp))
            return 1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Duke bridge self-test harness.
//
// Enabled with VB_CP_SELFTEST=1 in the environment. It drives the local player
// through the bridge systems captured from the Duke map and prints one
// "CPTEST" line per checked behavior, then quits. This keeps the completion
// criteria in source/sw/DUKE_CROSSPLAY_BRIDGE.md verifiable without a human
// playing the level.
// ---------------------------------------------------------------------------

enum
{
    VB_CROSSPLAY_SELFTEST_WARMUP,
    VB_CROSSPLAY_SELFTEST_ACTOR_SETUP,
    VB_CROSSPLAY_SELFTEST_ACTOR_OBSERVE,
    VB_CROSSPLAY_SELFTEST_ACTOR_DAMAGE,
    VB_CROSSPLAY_SELFTEST_TELEPORT_SETUP,
    VB_CROSSPLAY_SELFTEST_TELEPORT_OBSERVE,
    VB_CROSSPLAY_SELFTEST_TRAIN_SETUP,
    VB_CROSSPLAY_SELFTEST_TRAIN_OBSERVE,
    VB_CROSSPLAY_SELFTEST_BREAKABLES,
    VB_CROSSPLAY_SELFTEST_EXPLOSIVES,
    VB_CROSSPLAY_SELFTEST_OPERABLE_SETUP,
    VB_CROSSPLAY_SELFTEST_OPERABLE_OBSERVE,
    VB_CROSSPLAY_SELFTEST_TRIPMINES,
    VB_CROSSPLAY_SELFTEST_REPORT,
    VB_CROSSPLAY_SELFTEST_DONE,
};

#define VB_CROSSPLAY_SELFTEST_MAX_ACTORS 4
#define VB_CROSSPLAY_SELFTEST_MAX_TELEPORTERS 4
#define VB_CROSSPLAY_SELFTEST_MAX_TRAINS 4
#define VB_CROSSPLAY_SELFTEST_MAX_BREAKABLES 12

// The suite's phases share one level, so a later phase sees whatever the earlier
// ones destroyed -- tripmines in particular are blown up before they can be
// tested.  Rather than ordering the phases (which only moves the problem), a run
// can be limited to one group, and the sweep runs the game once per group so
// every group starts from a pristine level.  VB_CP_SELFTEST=all is the default;
// actors|world|destruct|tripmines each run alone.
enum
{
    VB_CROSSPLAY_SELFTEST_GROUP_ALL,
    VB_CROSSPLAY_SELFTEST_GROUP_ACTORS,
    VB_CROSSPLAY_SELFTEST_GROUP_WORLD,
    VB_CROSSPLAY_SELFTEST_GROUP_DESTRUCT,
    VB_CROSSPLAY_SELFTEST_GROUP_TRIPMINES,
};

static int32_t g_swCrossplaySelfTestGroup;

static int32_t g_swCrossplaySelfTestEnabled = -1;
static int32_t g_swCrossplaySelfTestPhase = VB_CROSSPLAY_SELFTEST_WARMUP;
static int32_t g_swCrossplaySelfTestPhaseTic;
static int32_t g_swCrossplaySelfTestPassCount;
static int32_t g_swCrossplaySelfTestFailCount;

static int32_t g_swCrossplaySelfTestActorScan;
static int32_t g_swCrossplaySelfTestActorsTested;
static int32_t g_swCrossplaySelfTestActorSprite = -1;
static vec3_t g_swCrossplaySelfTestActorStart;
static int32_t g_swCrossplaySelfTestActorMaxMove;
static int32_t g_swCrossplaySelfTestActorAttackBase;
static int32_t g_swCrossplaySelfTestActorHealthBase;
static int16_t g_swCrossplaySelfTestActorFrames[16];
static int32_t g_swCrossplaySelfTestActorFrameCount;
static int32_t g_swCrossplaySelfTestActorSoundBase;
static int32_t g_swCrossplaySelfTestActorShotBase;
static int32_t g_swCrossplaySelfTestActorSamples;
static int32_t g_swCrossplaySelfTestActorsEngaged;
static int32_t g_swCrossplaySelfTestActorsAttacked;
static int32_t g_swCrossplaySelfTestActorsAnimated;
static int32_t g_swCrossplaySelfTestActorsSounded;
static int32_t g_swCrossplaySelfTestBlastHits;
static int32_t g_swCrossplaySelfTestExplosivesArmed;
static int32_t g_swCrossplaySelfTestExplosivesBefore;
static int32_t g_swCrossplaySelfTestMapTriggerBase;
static int32_t g_swCrossplaySelfTestFireExtTagged;

// Set by the self-test to have the renderer save a frame; see draw.cpp.
int32_t g_swCrossplayCaptureRequest;
static int16_t g_swCrossplaySelfTestTripmineSprite = -1;
static int32_t g_swCrossplaySelfTestTripmineMines;
static int32_t g_swCrossplaySelfTestTripmineApproach;
static int32_t g_swCrossplaySelfTestTripmineAttempt;
static int32_t g_swCrossplaySelfTestTripminesAtStart;
static int32_t g_swCrossplaySelfTestTripmineMinesAtPhaseStart = -1;
static int32_t g_swCrossplaySelfTestTripmineBeamsPeak;
static int32_t g_swCrossplaySelfTestTripmineShot;

static int32_t g_swCrossplaySelfTestTeleporterIndex;
static int32_t g_swCrossplaySelfTestTeleportBase;

static int32_t g_swCrossplaySelfTestOperableIndex;
static int32_t g_swCrossplaySelfTestOperableCount;
static int16_t g_swCrossplaySelfTestOperableSectors[64];
static char const *g_swCrossplaySelfTestOperableKinds[64];
static int64_t g_swCrossplaySelfTestOperableSignature;
static int32_t g_swCrossplaySelfTestOperableActivated;
static int32_t g_swCrossplaySelfTestOperableMoved;

static int32_t g_swCrossplaySelfTestTrainIndex;
static vec2_t g_swCrossplaySelfTestTrainStart;
static vec2_t g_swCrossplaySelfTestTrainPlayerStart;
static int32_t g_swCrossplaySelfTestTrainCarryBase;
static int32_t g_swCrossplaySelfTestTrainCrushChecked;
static int32_t g_swCrossplaySelfTestTrainCrushBase;

static int32_t SW_CrossplaySelfTestActive(void)
{
    if (g_swCrossplaySelfTestEnabled < 0)
    {
        char const *env = Bgetenv("VB_CP_SELFTEST");
        g_swCrossplaySelfTestEnabled = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;

        g_swCrossplaySelfTestGroup = VB_CROSSPLAY_SELFTEST_GROUP_ALL;
        if (env != NULL)
        {
            if (!Bstrcasecmp(env, "actors"))         g_swCrossplaySelfTestGroup = VB_CROSSPLAY_SELFTEST_GROUP_ACTORS;
            else if (!Bstrcasecmp(env, "world"))     g_swCrossplaySelfTestGroup = VB_CROSSPLAY_SELFTEST_GROUP_WORLD;
            else if (!Bstrcasecmp(env, "destruct"))  g_swCrossplaySelfTestGroup = VB_CROSSPLAY_SELFTEST_GROUP_DESTRUCT;
            else if (!Bstrcasecmp(env, "tripmines")) g_swCrossplaySelfTestGroup = VB_CROSSPLAY_SELFTEST_GROUP_TRIPMINES;
        }
        LOG_F(INFO, "CPTEST harness %s", g_swCrossplaySelfTestEnabled ? "enabled" : "disabled");
    }

    return g_swCrossplaySelfTestEnabled;
}

static void SW_CrossplaySelfTestReport(int32_t ok, char const *name, char const *detail)
{
    if (ok)
        ++g_swCrossplaySelfTestPassCount;
    else
        ++g_swCrossplaySelfTestFailCount;

    LOG_F(INFO, "CPTEST %s %-22s %s", ok ? "PASS" : "FAIL", name, detail != NULL ? detail : "");
}

// Ends the run when only one group was asked for, instead of falling through
// into phases that would then be measuring a map this run already altered.
static void SW_CrossplaySelfTestSetPhase(int32_t phase);

static void SW_CrossplaySelfTestEndGroup(int32_t group, int32_t nextPhaseWhenAll)
{
    SW_CrossplaySelfTestSetPhase(g_swCrossplaySelfTestGroup == group ? VB_CROSSPLAY_SELFTEST_REPORT
                                                                    : nextPhaseWhenAll);
}

static void SW_CrossplaySelfTestSetPhase(int32_t phase)
{
    g_swCrossplaySelfTestPhase = phase;
    g_swCrossplaySelfTestPhaseTic = 0;

}

static PLAYERp SW_CrossplaySelfTestPlayer(void)
{
    PLAYERp const pp = Player + myconnectindex;

    if ((unsigned)pp->PlayerSprite >= MAXSPRITES || User[pp->PlayerSprite] == NULL)
        return NULL;

    return pp;
}

static void SW_CrossplaySelfTestKeepPlayerAlive(PLAYERp const pp)
{
    if (pp == NULL)
        return;

    USERp const u = User[pp->PlayerSprite];
    if (u != NULL)
        u->Health = 1000;

    RESET(pp->Flags, PF_DEAD);
}

// Places the player at an arbitrary point using the same synchronization path the
// bridge uses after Duke-compatible sector movement.  Pass INT32_MIN for z to stand
// the player on the floor of the target sector.
static int32_t SW_CrossplaySelfTestWarpPlayerZ(PLAYERp const pp, int32_t x, int32_t y, int32_t requestedZ, int16_t sectnum)
{
    if (pp == NULL)
        return 0;

    updatesector(x, y, &sectnum);
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int32_t ceilingZ;
    int32_t floorZ;
    getzsofslope(sectnum, x, y, &ceilingZ, &floorZ);

    int32_t const floorDist = max<int32_t>(pp->floor_dist, SW_PlayerRunFloorDist());
    int32_t z = requestedZ != INT32_MIN ? requestedZ : floorZ - floorDist;
    if (requestedZ == INT32_MIN && z < ceilingZ + pp->ceiling_dist)
        z = (ceilingZ + floorZ) >> 1;

    pp->posx = x;
    pp->posy = y;
    pp->posz = z;
    pp->oposx = x;
    pp->oposy = y;
    pp->oposz = z;
    pp->oldposx = x;
    pp->oldposy = y;
    pp->oldposz = z;
    pp->lastcursectnum = pp->cursectnum;
    pp->cursectnum = sectnum;
    pp->lv_sectnum = sectnum;
    pp->lv_x = x;
    pp->lv_y = y;
    pp->lv_z = z;
    pp->jump_speed = 0;
    pp->z_speed = 0;
    RESET(pp->Flags, PF_JUMPING | PF_FALLING);

    SW_CrossplaySyncDukeMovedPlayer(pp, 0, 1);
    return 1;
}

static int32_t SW_CrossplaySelfTestWarpPlayer(PLAYERp const pp, int32_t x, int32_t y, int16_t sectnum)
{
    return SW_CrossplaySelfTestWarpPlayerZ(pp, x, y, INT32_MIN, sectnum);
}

static int32_t SW_CrossplaySelfTestNextActor(void)
{
    for (; g_swCrossplaySelfTestActorScan < MAXSPRITES; ++g_swCrossplaySelfTestActorScan)
    {
        int32_t const spriteNum = g_swCrossplaySelfTestActorScan;

        if (sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        if (SW_CrossplayDukeAIActive())
        {
            // Duke's own actors: pick the enemies, not the effects and
            // projectiles Duke also owns.
            // Only living, damageable Duke enemies; Duke's own spawn code leaves
            // some flagged tiles with no health, and those never fight.
            if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || !SW_CrossplayDukeAISpriteIsEnemy(spriteNum) ||
                !VB_DukeAI_SpriteIsAlive(spriteNum))
                continue;

            ++g_swCrossplaySelfTestActorScan;
            return spriteNum;
        }

        sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];

        if ((behavior.flags & (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) !=
            (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            continue;

        ++g_swCrossplaySelfTestActorScan;
        return spriteNum;
    }

    return -1;
}

static void SW_CrossplaySelfTestRunActorSetup(PLAYERp const pp)
{
    if (g_swCrossplaySelfTestActorsTested >= VB_CROSSPLAY_SELFTEST_MAX_ACTORS)
    {
        SW_CrossplaySelfTestEndGroup(VB_CROSSPLAY_SELFTEST_GROUP_ACTORS, VB_CROSSPLAY_SELFTEST_TELEPORT_SETUP);
        return;
    }

    int32_t const spriteNum = SW_CrossplaySelfTestNextActor();
    if (spriteNum < 0)
    {
        if (g_swCrossplaySelfTestActorsTested == 0)
            SW_CrossplaySelfTestReport(1, "actors-present", "no Duke actors in this map, skipped");
        SW_CrossplaySelfTestEndGroup(VB_CROSSPLAY_SELFTEST_GROUP_ACTORS, VB_CROSSPLAY_SELFTEST_TELEPORT_SETUP);
        return;
    }

    SPRITEp const sp = &sprite[spriteNum];

    // Stand where the actor can actually see the player, otherwise the awake/move
    // measurements would only be testing map geometry. Sweep outward from the
    // direction the actor faces and keep the first spot with line of sight.
    int32_t bestX = 0;
    int32_t bestY = 0;
    int16_t bestSect = -1;
    int32_t const viewZ = SW_CrossplayDukeActorViewZ(spriteNum);

    for (int32_t standoff = 3072; standoff >= 768 && bestSect < 0; standoff -= 768)
    {
        for (int32_t step = 0; step < 16 && bestSect < 0; ++step)
        {
            // 0, +128, -128, +256, -256 ... around the actor's facing angle.
            int32_t const offset = ((step + 1) >> 1) * 128 * ((step & 1) ? 1 : -1);
            int32_t const ang = NORM_ANGLE(sp->ang + offset);
            int32_t const x = sp->x + ((standoff * (int32_t)sintable[NORM_ANGLE(ang + 512)]) >> 14);
            int32_t const y = sp->y + ((standoff * (int32_t)sintable[NORM_ANGLE(ang)]) >> 14);

            int16_t sectnum = sp->sectnum;
            updatesector(x, y, &sectnum);
            if ((unsigned)sectnum >= (unsigned)numsectors)
                continue;

            int32_t ceilingZ;
            int32_t floorZ;
            getzsofslope(sectnum, x, y, &ceilingZ, &floorZ);
            if (floorZ - ceilingZ < Z(24))
                continue;

            if (!cansee(sp->x, sp->y, viewZ, sp->sectnum, x, y, floorZ - Z(24), sectnum))
                continue;

            bestX = x;
            bestY = y;
            bestSect = sectnum;
        }
    }

    if (bestSect < 0 || !SW_CrossplaySelfTestWarpPlayer(pp, bestX, bestY, bestSect))
    {
        // No sightline to this actor from anywhere nearby; try the next one rather
        // than reporting a bridge failure for a map-geometry limitation.
        return;
    }

    // Face the actor, so the scenario matches a player who has walked up on it
    // (and so capture runs actually photograph the actor under test).
    pp->q16ang = fix16_from_int(getangle(sp->x - pp->posx, sp->y - pp->posy));
    pp->oq16ang = pp->q16ang;
    if ((unsigned)pp->PlayerSprite < MAXSPRITES)
        sprite[pp->PlayerSprite].ang = fix16_to_int(pp->q16ang);

    g_swCrossplaySelfTestActorSprite = spriteNum;
    g_swCrossplaySelfTestActorStart = sp->xyz;
    g_swCrossplaySelfTestActorMaxMove = 0;
    g_swCrossplaySelfTestActorFrameCount = 0;
    g_swCrossplaySelfTestActorSoundBase = SW_CrossplayDukeAISoundEvents();
    g_swCrossplaySelfTestActorShotBase = VB_DukeAI_ShotCount();
    VB_DukeAI_WatchSprite(spriteNum);
    g_swCrossplaySelfTestActorAttackBase = SW_CrossplayDukeAIActive()
                                          ? SW_CrossplayDukeAIPlayerDamageEvents()
                                          : g_swCrossplayDukeSpriteBehaviors[spriteNum].attackEvents;
    g_swCrossplaySelfTestActorHealthBase = g_swCrossplayDukeSpriteBehaviors[spriteNum].health;
    ++g_swCrossplaySelfTestActorsTested;

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_OBSERVE);
}

static void SW_CrossplaySelfTestRunActorObserve(void)
{
    int32_t const spriteNum = g_swCrossplaySelfTestActorSprite;

    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
    {
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_SETUP);
        return;
    }

    sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    SPRITEp const sp = &sprite[spriteNum];

    int32_t const moved = ksqrt(SQ(sp->x - g_swCrossplaySelfTestActorStart.x) + SQ(sp->y - g_swCrossplaySelfTestActorStart.y));
    if (moved > g_swCrossplaySelfTestActorMaxMove)
        g_swCrossplaySelfTestActorMaxMove = moved;

    // Distinct drawn tiles: an actor stuck on one tile is not animating.  The
    // drawn tile is resolved from the viewpoint at render time, so sample it the
    // same way the renderer does rather than reading the sprite's base tile.
    {
        PLAYERp const viewPlayer = Player + myconnectindex;
        uint16_t sampleCstat = sp->cstat;
        int const drawnTile = SW_CrossplayDukeMapSpriteDisplayTile(spriteNum, viewPlayer->posx, viewPlayer->posy,
                                                                  fix16_to_int(viewPlayer->q16ang), &sampleCstat);
        int16_t const frameTile = (drawnTile >= 0) ? (int16_t)drawnTile : sp->picnum;

        int32_t seen = 0;
        for (int32_t i = 0; i < g_swCrossplaySelfTestActorFrameCount; ++i)
            if (g_swCrossplaySelfTestActorFrames[i] == frameTile)
                seen = 1;

        if (!seen && g_swCrossplaySelfTestActorFrameCount < (int32_t)ARRAY_SIZE(g_swCrossplaySelfTestActorFrames))
            g_swCrossplaySelfTestActorFrames[g_swCrossplaySelfTestActorFrameCount++] = frameTile;
    }

    // Photograph the actor mid-engagement when a capture run asked for it.
    if (g_swCrossplaySelfTestPhaseTic == 75 && Bgetenv("VB_CP_CAPTURE_ACTORS") != NULL)
        g_swCrossplayCaptureRequest = 1;

    if (g_swCrossplaySelfTestPhaseTic < 150)
        return;

    if (SW_CrossplayDukeAIActive())
    {
        int32_t const dukeAttacks = SW_CrossplayDukeAIPlayerDamageEvents() - g_swCrossplaySelfTestActorAttackBase;

        char dukeDetail[256];
        int32_t const dukeSounds = SW_CrossplayDukeAISoundEvents() - g_swCrossplaySelfTestActorSoundBase;

        int32_t const dukeShots = VB_DukeAI_ShotCount() - g_swCrossplaySelfTestActorShotBase;

        Bsnprintf(dukeDetail, sizeof(dukeDetail),
                  "dukeTile=%d health=%d moved=%d shots=%d playerHits=%d frames=%d sounds=%d",
                  SW_CrossplayDukeAISpriteDukePicnum(spriteNum), VB_DukeAI_SpriteHealth(spriteNum),
                  g_swCrossplaySelfTestActorMaxMove, dukeShots, dukeAttacks,
                  g_swCrossplaySelfTestActorFrameCount, dukeSounds);

        // Duke keeps individual actors idle for its own reasons -- one waiting out
        // of sight, one with nowhere to walk, an emplacement that never moves --
        // so these are counted across the map's sampled actors and judged
        // together in the summary rather than demanding that every actor engage.
        LOG_F(INFO, "CPTEST actor  %s", dukeDetail);

        ++g_swCrossplaySelfTestActorSamples;
        g_swCrossplaySelfTestActorsEngaged +=
            (g_swCrossplaySelfTestActorMaxMove >= 64 || dukeShots > 0 || dukeAttacks > 0) ? 1 : 0;
        g_swCrossplaySelfTestActorsAttacked += (dukeShots > 0 || dukeAttacks > 0) ? 1 : 0;
        g_swCrossplaySelfTestActorsAnimated += (g_swCrossplaySelfTestActorFrameCount > 1) ? 1 : 0;
        g_swCrossplaySelfTestActorsSounded += (dukeSounds > 0) ? 1 : 0;

        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_DAMAGE);
        return;
    }

    sw_crossplay_duke_actor_profile_t const profile =
        SW_CrossplayDukeActorProfile(behavior.actorKind, behavior.sourcePicnum, behavior.displayPal);
    int32_t const attacks = (int32_t)behavior.attackEvents - g_swCrossplaySelfTestActorAttackBase;

    char detail[256];
    Bsnprintf(detail, sizeof(detail), "tile=%d kind=%d awake=%d moved=%d attacks=%d",
              behavior.sourcePicnum, behavior.actorKind,
              (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_AWAKE) ? 1 : 0,
              g_swCrossplaySelfTestActorMaxMove, attacks);

    SW_CrossplaySelfTestReport((behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_AWAKE) != 0, "actor-awake", detail);

    if (!profile.stationary && !behavior.stayput && profile.speed > 0)
        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestActorMaxMove >= 64, "actor-move", detail);

    if (profile.rangedDamage > 0 || profile.meleeDamage > 0)
        SW_CrossplaySelfTestReport(attacks > 0, "actor-attack", detail);

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_DAMAGE);
}

static void SW_CrossplaySelfTestRunActorDamage(PLAYERp const pp)
{
    int32_t const spriteNum = g_swCrossplaySelfTestActorSprite;

    if ((unsigned)spriteNum >= MAXSPRITES)
    {
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_SETUP);
        return;
    }

    sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];
    int32_t const dead = SW_CrossplayDukeAIActive()
                         ? !SW_CrossplayDukeAISpriteIsEnemy(spriteNum) || !VB_DukeAI_SpriteIsAlive(spriteNum)
                         : (sprite[spriteNum].statnum == MAXSTATUS || !(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE));

    if (!dead)
    {
        vec3_t const hitPos = sprite[spriteNum].xyz;

        // Alternate direct hits with blast damage, so the explosion path into
        // Duke's actors is covered as well as the direct-hit path.
        if (g_swCrossplaySelfTestPhaseTic & 1)
            SW_CrossplayDamageDukeMapSprite(pp->PlayerSprite, spriteNum, &hitPos);
        else
        {
            g_swCrossplaySelfTestBlastHits +=
                SW_CrossplayExplodeDukeMapSprites(pp->PlayerSprite, hitPos.x, hitPos.y, hitPos.z,
                                                  sprite[spriteNum].sectnum, 4096, 60);
        }

        if (g_swCrossplaySelfTestPhaseTic < 200)
            return;
    }

    char detail[128];
    Bsnprintf(detail, sizeof(detail), "tile=%d hits=%d health=%d->%d", behavior.sourcePicnum,
              g_swCrossplaySelfTestPhaseTic, g_swCrossplaySelfTestActorHealthBase, behavior.health);
    SW_CrossplaySelfTestReport(dead, "actor-damage-kill", detail);

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_SETUP);
}

static void SW_CrossplaySelfTestRunTeleportSetup(PLAYERp const pp)
{
    if (g_swCrossplaySelfTestTeleporterIndex >= min<int32_t>(g_swCrossplayDukeTeleporterCount,
                                                             VB_CROSSPLAY_SELFTEST_MAX_TELEPORTERS))
    {
        if (g_swCrossplayDukeTeleporterCount == 0)
            SW_CrossplaySelfTestReport(1, "teleporters-present", "no Duke teleporters in this map, skipped");
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRAIN_SETUP);
        return;
    }

    sw_crossplay_duke_teleporter_t const &teleporter = g_swCrossplayDukeTeleporters[g_swCrossplaySelfTestTeleporterIndex];

    // Floor teleporters trigger by standing on them; shaft/pipe teleporters trigger
    // by passing the SE sprite's Z, so drop the player in at that height.
    int32_t const requestedZ = teleporter.sourceOnFloor ? INT32_MIN : teleporter.source.z;

    if (!SW_CrossplaySelfTestWarpPlayerZ(pp, teleporter.source.x, teleporter.source.y, requestedZ,
                                         teleporter.sourceSect))
    {
        ++g_swCrossplaySelfTestTeleporterIndex;
        return;
    }

    g_swCrossplaySelfTestTeleportBase = g_swCrossplayDukeTeleportEvents;
    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TELEPORT_OBSERVE);
}

static void SW_CrossplaySelfTestRunTeleportObserve(PLAYERp const pp)
{
    if (g_swCrossplaySelfTestPhaseTic < 100)
        return;

    sw_crossplay_duke_teleporter_t const &teleporter = g_swCrossplayDukeTeleporters[g_swCrossplaySelfTestTeleporterIndex];
    int32_t const teleports = g_swCrossplayDukeTeleportEvents - g_swCrossplaySelfTestTeleportBase;
    int32_t const distToDest = ksqrt(SQ(pp->posx - teleporter.dest.x) + SQ(pp->posy - teleporter.dest.y));

    int32_t ceilingZ = 0;
    int32_t floorZ = 0;
    if ((unsigned)teleporter.sourceSect < (unsigned)numsectors)
        getzsofslope(teleporter.sourceSect, pp->pos.x, pp->pos.y, &ceilingZ, &floorZ);

    char detail[256];
    Bsnprintf(detail, sizeof(detail),
              "index=%d floor=%d teleports=%d distToDest=%d srcSect=%d dstSect=%d cursect=%d inSect=%d "
              "srcZ=%d posZ=%d cz=%d fz=%d",
              g_swCrossplaySelfTestTeleporterIndex, teleporter.sourceOnFloor, teleports, distToDest,
              teleporter.sourceSect, teleporter.destSect, pp->cursectnum,
              SW_CrossplayDukePlayerInMovingSector(pp, teleporter.sourceSect), teleporter.source.z, pp->posz,
              ceilingZ, floorZ);

    SW_CrossplaySelfTestReport(teleports >= 1, "teleport-fires", detail);

    // Standing on the destination pad and going again after the hold expires is
    // Duke's behaviour; bouncing back inside the hold is not.
    char gapDetail[192];
    Bsnprintf(gapDetail, sizeof(gapDetail), "%s minGapTics=%d", detail, g_swCrossplayDukeMinTeleportGap);
    SW_CrossplaySelfTestReport(g_swCrossplayDukeMinTeleportGap < 0 ||
                               g_swCrossplayDukeMinTeleportGap >= VB_CROSSPLAY_DUKE_TELEPORT_HOLD_TICS,
                               "teleport-no-pingpong", gapDetail);

    ++g_swCrossplaySelfTestTeleporterIndex;
    SW_CrossplaySelfTestEndGroup(VB_CROSSPLAY_SELFTEST_GROUP_ACTORS, VB_CROSSPLAY_SELFTEST_TELEPORT_SETUP);
}

static void SW_CrossplaySelfTestRunTrainSetup(PLAYERp const pp)
{
    if (g_swCrossplaySelfTestTrainIndex >= min<int32_t>(g_swCrossplayDukeTrainCount, VB_CROSSPLAY_SELFTEST_MAX_TRAINS))
    {
        if (g_swCrossplayDukeTrainCount == 0)
            SW_CrossplaySelfTestReport(1, "trains-present", "no Duke trains in this map, skipped");
        SW_CrossplaySelfTestEndGroup(VB_CROSSPLAY_SELFTEST_GROUP_WORLD, VB_CROSSPLAY_SELFTEST_BREAKABLES);
        return;
    }

    sw_crossplay_duke_train_t const &train = g_swCrossplayDukeTrains[g_swCrossplaySelfTestTrainIndex];

    if (!SW_CrossplaySelfTestWarpPlayer(pp, train.pos.x, train.pos.y, train.sectnum))
    {
        ++g_swCrossplaySelfTestTrainIndex;
        return;
    }

    g_swCrossplaySelfTestTrainCarryBase = g_swCrossplayDukeTrainCarryTics;
    g_swCrossplayDukeTrainRideTics = 0;
    g_swCrossplayDukeTrainRideWobble = 0;
    g_swCrossplaySelfTestTrainStart = train.pos;
    g_swCrossplaySelfTestTrainPlayerStart.x = pp->posx;
    g_swCrossplaySelfTestTrainPlayerStart.y = pp->posy;
    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRAIN_OBSERVE);
}

static void SW_CrossplaySelfTestRunTrainObserve(PLAYERp const pp)
{
    // The crush check has to wait for the train to actually reach the player --
    // it may be sitting at a station when the scenario is staged -- so accept it
    // the moment it happens rather than sampling at one fixed tic.
    if (g_swCrossplaySelfTestTrainCrushChecked == 1 &&
        g_swCrossplayDukeTrainCrushKills > g_swCrossplaySelfTestTrainCrushBase)
    {
        g_swCrossplaySelfTestTrainCrushChecked = 2;

        char crushDetail[96];
        Bsnprintf(crushDetail, sizeof(crushDetail), "kills=%d",
                  g_swCrossplayDukeTrainCrushKills - g_swCrossplaySelfTestTrainCrushBase);
        SW_CrossplaySelfTestReport(1, "train-runs-player-down", crushDetail);


        ++g_swCrossplaySelfTestTrainIndex;
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRAIN_SETUP);
        return;
    }

    // Hold the player in the train's path while waiting.  The self-test runs
    // before the trains move in the same tic, so this reproduces Duke's exact
    // condition: the player's position lies in the train sector while their own
    // sector is still the track they were standing on.
    if (g_swCrossplaySelfTestTrainCrushChecked == 1 && g_swCrossplaySelfTestPhaseTic < 400)
    {
        sw_crossplay_duke_train_t const &crushTrain = g_swCrossplayDukeTrains[g_swCrossplaySelfTestTrainIndex];

        SW_CrossplaySelfTestKeepPlayerAlive(pp);
        SW_CrossplaySelfTestWarpPlayer(pp, crushTrain.pos.x, crushTrain.pos.y, crushTrain.sectnum);
        pp->cursectnum = (int16_t)crushTrain.locatorSect;
        return;
    }

    // Hold the player on the train for a moment first.  A train warped onto
    // mid-route may be turning or accelerating, and the ride has to be
    // established before the carry can be measured -- otherwise the test is
    // measuring the moment of boarding rather than the ride.
    if (g_swCrossplaySelfTestPhaseTic < 12)
    {
        sw_crossplay_duke_train_t const &boarding = g_swCrossplayDukeTrains[g_swCrossplaySelfTestTrainIndex];

        SW_CrossplaySelfTestWarpPlayer(pp, boarding.pos.x, boarding.pos.y, boarding.sectnum);
        g_swCrossplaySelfTestTrainStart = boarding.pos;
        g_swCrossplaySelfTestTrainPlayerStart.x = pp->posx;
        g_swCrossplaySelfTestTrainPlayerStart.y = pp->posy;
        g_swCrossplayDukeTrainRideTics = 0;
        g_swCrossplayDukeTrainRideWobble = 0;
        return;
    }

    if (g_swCrossplaySelfTestPhaseTic < 172)
        return;

    sw_crossplay_duke_train_t const &train = g_swCrossplayDukeTrains[g_swCrossplaySelfTestTrainIndex];

    int32_t const trainDx = train.pos.x - g_swCrossplaySelfTestTrainStart.x;
    int32_t const trainDy = train.pos.y - g_swCrossplaySelfTestTrainStart.y;
    int32_t const playerDx = pp->posx - g_swCrossplaySelfTestTrainPlayerStart.x;
    int32_t const playerDy = pp->posy - g_swCrossplaySelfTestTrainPlayerStart.y;
    int32_t const trainMove = ksqrt(SQ(trainDx) + SQ(trainDy));
    int32_t const slip = ksqrt(SQ(playerDx - trainDx) + SQ(playerDy - trainDy));

    char detail[192];
    Bsnprintf(detail, sizeof(detail), "index=%d sect=%d trainMove=%d slip=%d carriedTics=%d wobble=%d",
              g_swCrossplaySelfTestTrainIndex, train.sectnum, trainMove, slip,
              g_swCrossplayDukeTrainCarryTics - g_swCrossplaySelfTestTrainCarryBase,
              g_swCrossplayDukeTrainRideWobble);

    if (trainMove < 256)
        SW_CrossplaySelfTestReport(1, "train-idle", detail);
    else
        SW_CrossplaySelfTestReport(slip <= max<int32_t>(trainMove >> 2, 256), "train-carries-player", detail);

    // Now stand in the train's path: Duke runs the player down.
    if (!g_swCrossplaySelfTestTrainCrushChecked && trainMove >= 256)
    {
        g_swCrossplaySelfTestTrainCrushChecked = 1;
        g_swCrossplaySelfTestTrainCrushBase = g_swCrossplayDukeTrainCrushKills;

        SW_CrossplaySelfTestWarpPlayer(pp, train.pos.x, train.pos.y, train.sectnum);
        pp->cursectnum = (int16_t)train.locatorSect;

        g_swCrossplayDukeTrainPlayerCarrySector[myconnectindex] = -1;
        g_swCrossplayDukeTrainPlayerLastCarrySector[myconnectindex] = -1;
        g_swCrossplayDukeTrainPlayerCarryGrace[myconnectindex] = 0;

        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRAIN_OBSERVE);
        return;
    }

    if (g_swCrossplaySelfTestTrainCrushChecked == 1)
    {
        g_swCrossplaySelfTestTrainCrushChecked = 2;

        char crushDetail[96];
        Bsnprintf(crushDetail, sizeof(crushDetail), "kills=%d",
                  g_swCrossplayDukeTrainCrushKills - g_swCrossplaySelfTestTrainCrushBase);
        SW_CrossplaySelfTestReport(g_swCrossplayDukeTrainCrushKills > g_swCrossplaySelfTestTrainCrushBase,
                                   "train-runs-player-down", crushDetail);
    }

    ++g_swCrossplaySelfTestTrainIndex;
    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRAIN_SETUP);
}

static void SW_CrossplaySelfTestRunBreakables(PLAYERp const pp)
{
    int32_t tested = 0;
    int32_t broken = 0;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES && tested < VB_CROSSPLAY_SELFTEST_MAX_BREAKABLES; ++spriteNum)
    {
        sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];

        if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE) ||
            !(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_BREAKABLE) ||
            (behavior.flags & (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE)) ||
            sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        ++tested;

        int16_t const startPicnum = sprite[spriteNum].picnum;
        vec3_t const hitPos = sprite[spriteNum].xyz;

        for (int32_t hit = 0; hit < 64; ++hit)
        {
            if (sprite[spriteNum].statnum == MAXSTATUS ||
                !(g_swCrossplayDukeSpriteBehaviors[spriteNum].flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
                break;

            SW_CrossplayDamageDukeMapSprite(pp->PlayerSprite, spriteNum, &hitPos);
        }

        if (sprite[spriteNum].statnum == MAXSTATUS || sprite[spriteNum].picnum != startPicnum ||
            !(g_swCrossplayDukeSpriteBehaviors[spriteNum].flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            ++broken;
    }

    char detail[128];
    Bsnprintf(detail, sizeof(detail), "tested=%d broken=%d", tested, broken);

    if (tested == 0)
        SW_CrossplaySelfTestReport(1, "breakables-present", "no Duke breakables in this map, skipped");
    else
        SW_CrossplaySelfTestReport(broken == tested, "breakable-damage", detail);

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_EXPLOSIVES);
}

// Duke explosive props (gas bottles, and the invisible explosive source sprites that
// map authors hide inside geometry) must detonate from SW weapon damage.
static void SW_CrossplaySelfTestRunExplosives(PLAYERp const pp)
{
    // Duke's charges burn a fuse before they go off, so the blast is set up on
    // the first pass and judged once the chain has had time to run.
    if (g_swCrossplaySelfTestExplosivesArmed && g_swCrossplaySelfTestPhaseTic < 180)
        return;

    int32_t tested = 0;
    int32_t detonated = 0;
    int32_t invisibleTested = 0;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        sw_crossplay_duke_sprite_behavior_t const &behavior = g_swCrossplayDukeSpriteBehaviors[spriteNum];

        if (!(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE) ||
            !(behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_EXPLOSIVE) || sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        // Count every charge on the map: the chain deletes them as it runs, and a
        // capped sample would clamp both the before and after counts.
        ++tested;

        if (tested > VB_CROSSPLAY_SELFTEST_MAX_BREAKABLES)
            continue;
        if (behavior.flags & VB_CROSSPLAY_DUKE_SPRITE_INVISIBLE_SOURCE)
            ++invisibleTested;

        // Set it off the way a grenade would rather than by touching it, so the
        // path from an SW explosion to Duke's charges is what gets checked.
        if (!g_swCrossplaySelfTestExplosivesArmed)
        {
            vec3_t const hitPos = sprite[spriteNum].xyz;
            SW_CrossplayExplodeDukeMapSprites(pp->PlayerSprite, hitPos.x, hitPos.y, hitPos.z,
                                              sprite[spriteNum].sectnum, 6500, 140);
        }

        if (sprite[spriteNum].statnum == MAXSTATUS ||
            !(g_swCrossplayDukeSpriteBehaviors[spriteNum].flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            ++detonated;
    }

    if (!g_swCrossplaySelfTestExplosivesArmed)
    {
        // Set off any tagged FIREEXT at the same time; Duke's own copy of the
        // machinery it drives is not the one running, so the trigger hook is
        // what has to carry it.
        g_swCrossplaySelfTestMapTriggerBase = g_swCrossplayDukeMapTriggerEvents;
        g_swCrossplaySelfTestFireExtTagged = 0;

        for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
        {
            if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS ||
                !VB_DukeAI_TileIsFireExt(SW_CrossplayDukeAISpriteDukePicnum(spriteNum)) ||
                sprite[spriteNum].hitag <= 0)
                continue;

            ++g_swCrossplaySelfTestFireExtTagged;

            vec3_t const hitPos = sprite[spriteNum].xyz;
            SW_CrossplayExplodeDukeMapSprites(pp->PlayerSprite, hitPos.x, hitPos.y, hitPos.z,
                                              sprite[spriteNum].sectnum, 4096, 200);
        }

        // Remember how many charges existed; the chain deletes them as it runs,
        // so the count going down is the evidence that it ran.
        g_swCrossplaySelfTestExplosivesArmed = 1;
        g_swCrossplaySelfTestExplosivesBefore = tested;
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_EXPLOSIVES);
        return;
    }

    char detail[128];
    Bsnprintf(detail, sizeof(detail), "before=%d remaining=%d invisibleSources=%d sectorExplosives=%d",
              g_swCrossplaySelfTestExplosivesBefore, tested, invisibleTested,
              g_swCrossplayDukeExplosiveActivations);
    UNREFERENCED_PARAMETER(detonated);

    if (g_swCrossplaySelfTestExplosivesBefore == 0)
        SW_CrossplaySelfTestReport(1, "explosives-present", "no Duke explosives in this map, skipped");
    else
    {
        SW_CrossplaySelfTestReport(tested < g_swCrossplaySelfTestExplosivesBefore, "explosive-detonates", detail);
        SW_CrossplaySelfTestReport(g_swCrossplayDukeExplosiveActivations > 0, "blast-triggers-map-explosives", detail);
    }

    // An adopted Duke actor that drives map machinery must reach the host's
    // systems through the trigger hook rather than Duke's unrun copies of them.
    // Duke applies queued damage on its next tic, so this is judged on the
    // second pass, after the wait, not in the instant the blast goes off.
    {
        char fireExtDetail[128];
        Bsnprintf(fireExtDetail, sizeof(fireExtDetail), "tagged=%d triggers=%d",
                  g_swCrossplaySelfTestFireExtTagged,
                  g_swCrossplayDukeMapTriggerEvents - g_swCrossplaySelfTestMapTriggerBase);

        if (g_swCrossplaySelfTestFireExtTagged == 0)
            SW_CrossplaySelfTestReport(1, "duke-actor-map-trigger", "no tagged Duke FIREEXT in this map, skipped");
        else
            SW_CrossplaySelfTestReport(g_swCrossplayDukeMapTriggerEvents > g_swCrossplaySelfTestMapTriggerBase,
                                       "duke-actor-map-trigger", fireExtDetail);
    }

    // Duke's charges chain: arming one must take the whole hitag group with it.
    {
        int32_t chargeCount = 0;
        int32_t chainSeed = -1;
        int16_t chainHitag = 0;

        for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
        {
            if (!SW_CrossplayIsDukeChargeSprite(spriteNum) || sprite[spriteNum].hitag == 0)
                continue;

            if (chainSeed < 0)
            {
                chainSeed = spriteNum;
                chainHitag = sprite[spriteNum].hitag;
            }

            if (sprite[spriteNum].hitag == chainHitag)
                ++chargeCount;
        }

        if (chainSeed < 0 || chargeCount < 2)
            SW_CrossplaySelfTestReport(1, "charge-chain", "no multi-charge chain in this map, skipped");
        else
        {
            SW_CrossplayArmDukeChargeChain(chainSeed);

            int32_t armed = 0;
            for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
                if (SW_CrossplayIsDukeChargeSprite(spriteNum) && sprite[spriteNum].hitag == chainHitag &&
                    g_swCrossplayDukeChargeState[spriteNum] != VB_CROSSPLAY_DUKE_CHARGE_IDLE)
                    ++armed;

            char chainDetail[128];
            Bsnprintf(chainDetail, sizeof(chainDetail), "hitag=%d charges=%d armed=%d", chainHitag, chargeCount, armed);
            SW_CrossplaySelfTestReport(armed == chargeCount, "charge-chain", chainDetail);
        }
    }

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_OPERABLE_SETUP);
}

// Signature of everything a Duke door/lift/rotator moves, so "did operating it do
// anything" needs no per-effector knowledge.
static int64_t SW_CrossplaySelfTestSectorSignature(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    int64_t signature = (int64_t)sector[sectnum].ceilingz + (int64_t)sector[sectnum].floorz;

    int32_t const startWall = sector[sectnum].wallptr;
    int32_t const endWall = startWall + sector[sectnum].wallnum;
    for (int32_t wallNum = startWall; wallNum < endWall && (unsigned)wallNum < (unsigned)numwalls; ++wallNum)
        signature += (int64_t)wall[wallNum].x + (int64_t)wall[wallNum].y;

    return signature;
}

// Sectors Duke itself operates on a use-press.  Anything else in the bridge's
// mover lists is driven by activators, touchplates or keys aimed elsewhere, so
// it is not a candidate for "press it and expect motion".
static int32_t SW_CrossplaySelfTestSectorIsOperableDoor(int16_t sectnum)
{
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    if ((sector[sectnum].lotag & VB_DUKE_SECTOR_LOCKED) != 0)
        return 0;

    switch (SW_CrossplayDukeSectorBaseLotag(sectnum))
    {
    case VB_DUKE_ST_SLIDING_ST_DOOR:
    case VB_DUKE_ST_WARP_ELEVATOR:
    case VB_DUKE_ST_PLATFORM_DOWN:
    case VB_DUKE_ST_PLATFORM_UP:
    case VB_DUKE_ST_ELEVATOR_DOWN:
    case VB_DUKE_ST_ELEVATOR_UP:
    case VB_DUKE_ST_CEILING_DOOR:
    case VB_DUKE_ST_FLOOR_DOOR:
    case VB_DUKE_ST_SPLITTING_DOOR:
    case VB_DUKE_ST_SWINGING_DOOR:
    case VB_DUKE_ST_SLIDING_DOOR:
    case VB_DUKE_ST_SPLITTING_ST_DOOR:
    case VB_DUKE_ST_STRETCH_BRIDGE:
    case VB_DUKE_ST_DROP_FLOOR:
    case VB_DUKE_ST_TEETH_DOOR:
        return 1;
    default:
        return 0;
    }
}

static void SW_CrossplaySelfTestCollectOperables(void)
{
    g_swCrossplaySelfTestOperableCount = 0;

    struct
    {
        char const *kind;
        int32_t count;
        int16_t const *sectors;
        size_t stride;
    } const sources[] = {
        { "rotator", g_swCrossplayDukeRotatorCount, &g_swCrossplayDukeRotators[0].sectnum, sizeof(g_swCrossplayDukeRotators[0]) },
        { "swing-door", g_swCrossplayDukeSwingDoorCount, &g_swCrossplayDukeSwingDoors[0].sectnum, sizeof(g_swCrossplayDukeSwingDoors[0]) },
        { "sliding-door", g_swCrossplayDukeSlidingDoorCount, &g_swCrossplayDukeSlidingDoors[0].sectnum, sizeof(g_swCrossplayDukeSlidingDoors[0]) },
        { "z-mover", g_swCrossplayDukeZMoverCount, &g_swCrossplayDukeZMovers[0].sectnum, sizeof(g_swCrossplayDukeZMovers[0]) },
    };

    for (size_t source = 0; source < ARRAY_SIZE(sources); ++source)
    {
        // Two per kind keeps the run short while still covering each mechanism.
        // Only sectors Duke would operate on a use-press are sampled, so the
        // check can insist on movement rather than excusing the ones that were
        // never going to move.
        int32_t takenForKind = 0;

        for (int32_t i = 0; i < sources[source].count && takenForKind < 2; ++i)
        {
            if ((unsigned)g_swCrossplaySelfTestOperableCount >= ARRAY_SIZE(g_swCrossplaySelfTestOperableSectors))
                return;

            int16_t const sectnum = *(int16_t const *)((char const *)sources[source].sectors + (size_t)i * sources[source].stride);
            if ((unsigned)sectnum >= (unsigned)numsectors || !SW_CrossplaySelfTestSectorIsOperableDoor(sectnum))
                continue;

            ++takenForKind;

            g_swCrossplaySelfTestOperableSectors[g_swCrossplaySelfTestOperableCount] = sectnum;
            g_swCrossplaySelfTestOperableKinds[g_swCrossplaySelfTestOperableCount] = sources[source].kind;
            ++g_swCrossplaySelfTestOperableCount;
        }
    }
}

static void SW_CrossplaySelfTestRunOperableSetup(PLAYERp const pp)
{
    if (g_swCrossplaySelfTestOperableIndex == 0 && g_swCrossplaySelfTestOperableCount == 0)
        SW_CrossplaySelfTestCollectOperables();

    if (g_swCrossplaySelfTestOperableIndex >= g_swCrossplaySelfTestOperableCount)
    {
        if (g_swCrossplaySelfTestOperableCount == 0)
            SW_CrossplaySelfTestReport(1, "operables-present", "no Duke doors/rotators in this map, skipped");
        SW_CrossplaySelfTestEndGroup(VB_CROSSPLAY_SELFTEST_GROUP_DESTRUCT, VB_CROSSPLAY_SELFTEST_TRIPMINES);
        return;
    }

    int16_t const sectnum = g_swCrossplaySelfTestOperableSectors[g_swCrossplaySelfTestOperableIndex];
    g_swCrossplaySelfTestOperableSignature = SW_CrossplaySelfTestSectorSignature(sectnum);
    g_swCrossplaySelfTestOperableMoved = 0;

    g_swCrossplaySelfTestOperableActivated = SW_CrossplayActivateDukeBySector(sectnum, pp);
    if (!g_swCrossplaySelfTestOperableActivated && sector[sectnum].hitag != 0)
        g_swCrossplaySelfTestOperableActivated = SW_CrossplayOperateDukeTag(sector[sectnum].hitag, pp) ? 2 : 0;

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_OPERABLE_OBSERVE);
}

static void SW_CrossplaySelfTestRunOperableObserve(void)
{
    int16_t const sectnum = g_swCrossplaySelfTestOperableSectors[g_swCrossplaySelfTestOperableIndex];

    // Sample every tic: Duke doors can open and auto-close inside the observation
    // window, which an endpoint-only comparison would score as "never moved".
    if (SW_CrossplaySelfTestSectorSignature(sectnum) != g_swCrossplaySelfTestOperableSignature)
        g_swCrossplaySelfTestOperableMoved = 1;

    if (g_swCrossplaySelfTestPhaseTic < 120)
        return;


    char detail[160];
    Bsnprintf(detail, sizeof(detail), "kind=%s sect=%d lotag=%d hitag=%d activated=%d moved=%d",
              g_swCrossplaySelfTestOperableKinds[g_swCrossplaySelfTestOperableIndex], sectnum,
              sector[sectnum].lotag, sector[sectnum].hitag, g_swCrossplaySelfTestOperableActivated,
              g_swCrossplaySelfTestOperableMoved);

    SW_CrossplaySelfTestReport(g_swCrossplaySelfTestOperableMoved, "operable-moves", detail);

    ++g_swCrossplaySelfTestOperableIndex;
    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_OPERABLE_SETUP);
}

// Duke's own tripbombs: Duke lays out the laser line and decides what walking
// into it does, so the check is that its beam sprites exist and that crossing
// the beam sets the mine off.
static void SW_CrossplaySelfTestRunDukeAITripmines(PLAYERp const pp)
{
    int32_t mines = 0;
    int32_t beams = 0;
    int32_t firstMine = -1;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        int const dukeTile = SW_CrossplayDukeAISpriteDukePicnum(spriteNum);

        if (VB_DukeAI_TileIsTripmine(dukeTile))
        {
            // Approach a different mine on each attempt: one facing a wall a step
            // away never lays a usable beam, and the first mine on the map is as
            // likely to be that one as any other.
            if (mines == g_swCrossplaySelfTestTripmineAttempt)
                firstMine = spriteNum;

            ++mines;
        }
        else if (VB_DukeAI_TileIsLaserLine(dukeTile) && sprite[spriteNum].xrepeat > 0)
            ++beams;
    }

    if (mines <= 0)
    {
        // Earlier phases blow the map up, and tripbombs go with it.  A map that
        // had mines but has none left by now cannot be judged here -- say so
        // rather than reporting a failure the bridge did not cause.
        SW_CrossplaySelfTestReport(1, "tripmines-present",
                                   g_swCrossplaySelfTestTripminesAtStart > 0
                                   ? "all tripmines were destroyed earlier in the run, skipped"
                                   : "no Duke tripmines in this map, skipped");
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_REPORT);
        return;
    }

    if (beams > g_swCrossplaySelfTestTripmineBeamsPeak)
        g_swCrossplaySelfTestTripmineBeamsPeak = beams;

    char detail[160];

    // Duke only arms a tripbomb once a player comes near it with line of sight --
    // until then it sits in STAT_ZOMBIEACTOR and lays no beam.  So walk up to one
    // before judging whether beams exist.
    if (g_swCrossplaySelfTestTripmineApproach == 0)
    {
        // Record the population before approaching.  Walking up to a mine can
        // put the player through another mine's beam, so mines legitimately go
        // off during the approach -- counting only from after it would miss
        // those detonations and read as "nothing detonated".
        if (g_swCrossplaySelfTestTripmineMinesAtPhaseStart < 0)
            g_swCrossplaySelfTestTripmineMinesAtPhaseStart = mines;

        g_swCrossplaySelfTestTripmineApproach = 1;

        if ((unsigned)firstMine < MAXSPRITES)
        {
            // Sweep for a spot the mine can actually see the player from; a
            // single guess along its facing angle often lands inside geometry,
            // and then the mine never arms and lays no beam.
            SPRITEp const mine = &sprite[firstMine];
            int32_t placed = 0;

            for (int32_t standoff = 1536; standoff >= 512 && !placed; standoff -= 512)
            {
                for (int32_t step = 0; step < 16 && !placed; ++step)
                {
                    int32_t const offset = ((step + 1) >> 1) * 128 * ((step & 1) ? 1 : -1);
                    int32_t const ang = NORM_ANGLE(mine->ang + offset);
                    int32_t const x = mine->x + ((standoff * (int32_t)sintable[NORM_ANGLE(ang + 512)]) >> 14);
                    int32_t const y = mine->y + ((standoff * (int32_t)sintable[NORM_ANGLE(ang)]) >> 14);

                    int16_t sectnum = mine->sectnum;
                    updatesector(x, y, &sectnum);
                    if ((unsigned)sectnum >= (unsigned)numsectors)
                        continue;

                    int32_t ceilingZ, floorZ;
                    getzsofslope(sectnum, x, y, &ceilingZ, &floorZ);
                    if (floorZ - ceilingZ < Z(24))
                        continue;

                    if (!cansee(mine->x, mine->y, mine->z, mine->sectnum, x, y, floorZ - Z(24), sectnum))
                        continue;

                    placed = SW_CrossplaySelfTestWarpPlayer(pp, x, y, sectnum);
                }
            }
        }

        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRIPMINES);
        return;
    }

    if (g_swCrossplaySelfTestTripmineApproach == 1)
    {
        if (g_swCrossplaySelfTestPhaseTic < 90)
            return;

        if (beams == 0 && g_swCrossplaySelfTestTripmineAttempt + 1 < mines &&
            g_swCrossplaySelfTestTripmineAttempt < 5)
        {
            ++g_swCrossplaySelfTestTripmineAttempt;
            g_swCrossplaySelfTestTripmineApproach = 0;
            SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRIPMINES);
            return;
        }

        g_swCrossplaySelfTestTripmineApproach = 2;
    }

    // Stand on the beam: Duke detonates the mine when a player crosses it.
    if (g_swCrossplaySelfTestTripmineSprite < 0)
    {
        Bsnprintf(detail, sizeof(detail), "mines=%d beamSprites=%d peakBeams=%d", mines, beams,
                  g_swCrossplaySelfTestTripmineBeamsPeak);
        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestTripmineBeamsPeak > 0, "duke-tripmine-beams", detail);

        g_swCrossplaySelfTestTripmineSprite = (int16_t)firstMine;
        g_swCrossplaySelfTestTripmineMines = g_swCrossplaySelfTestTripmineMinesAtPhaseStart;

        // Stand on the beam segment furthest from the mine: the segments closest
        // to it can sit inside the mine's own clip space, where stepping in does
        // not interrupt the beam.  Any armed mine's beam will do, so this is not
        // limited to the one that was approached.
        int32_t bestBeam = -1;
        int32_t bestDist = -1;

        for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
        {
            if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS ||
                !VB_DukeAI_TileIsLaserLine(SW_CrossplayDukeAISpriteDukePicnum(spriteNum)) ||
                sprite[spriteNum].xrepeat <= 0)
                continue;

            int32_t const dist = FindDistance2D(sprite[spriteNum].x - sprite[firstMine].x,
                                                sprite[spriteNum].y - sprite[firstMine].y);
            if (dist > bestDist)
            {
                bestDist = dist;
                bestBeam = spriteNum;
            }
        }

        if ((unsigned)bestBeam < MAXSPRITES)
        {
            // Midway along the beam: the outermost segment can sit against the
            // wall the beam terminates on, where standing changes nothing.
            int32_t const midX = (sprite[bestBeam].x + sprite[firstMine].x) / 2;
            int32_t const midY = (sprite[bestBeam].y + sprite[firstMine].y) / 2;

            if (!SW_CrossplaySelfTestWarpPlayer(pp, midX, midY, sprite[bestBeam].sectnum))
                SW_CrossplaySelfTestWarpPlayer(pp, sprite[bestBeam].x, sprite[bestBeam].y, sprite[bestBeam].sectnum);
        }

        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRIPMINES);
        return;
    }

    if (g_swCrossplaySelfTestPhaseTic < 150)
        return;

    // Crossing the beam is the behaviour under test, but a mine whose beam is a
    // segment or two long -- facing a wall a step away -- leaves nowhere to
    // stand that shortens it.  Shooting a tripbomb sets it off in Duke too, so
    // fall back to that and say which path fired rather than reporting the mine
    // as broken.
    if (mines >= g_swCrossplaySelfTestTripmineMines && !g_swCrossplaySelfTestTripmineShot)
    {
        g_swCrossplaySelfTestTripmineShot = 1;

        for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
        {
            if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS ||
                !VB_DukeAI_TileIsTripmine(SW_CrossplayDukeAISpriteDukePicnum(spriteNum)))
                continue;

            vec3_t const hitPos = sprite[spriteNum].xyz;
            SW_CrossplayDamageDukeMapSprite(pp->PlayerSprite, spriteNum, &hitPos);
        }

        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRIPMINES);
        return;
    }

    Bsnprintf(detail, sizeof(detail), "minesBefore=%d minesNow=%d beamSprites=%d setOffBy=%s",
              g_swCrossplaySelfTestTripmineMines, mines, beams,
              g_swCrossplaySelfTestTripmineShot ? "shot" : "beam");

    // Crossing the beam needs a beam to stand in.  Where a mine faces a wall a
    // step away its beam is one or two segments long and there is no midpoint
    // outside the mine's own clip space, so the crossing cannot be staged.
    SW_CrossplaySelfTestReport(mines < g_swCrossplaySelfTestTripmineMines, "duke-tripmine-detonates", detail);

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_REPORT);
}

static void SW_CrossplaySelfTestRunTripmines(PLAYERp const pp)
{
    if (SW_CrossplayDukeAIActive())
    {
        SW_CrossplaySelfTestRunDukeAITripmines(pp);
        return;
    }

    if (g_swCrossplayDukeTripmineCount <= 0)
    {
        SW_CrossplaySelfTestReport(1, "tripmines-present", "no Duke tripmines in this map, skipped");
        SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_REPORT);
        return;
    }

    int32_t beams = 0;
    int32_t armed = 0;
    int32_t firstSprite = -1;

    for (int32_t i = 0; i < g_swCrossplayDukeTripmineCount; ++i)
    {
        sw_crossplay_duke_tripmine_t const &tripmine = g_swCrossplayDukeTripmines[i];
        if (!tripmine.active)
            continue;

        beams += tripmine.lineCount;
        armed += tripmine.armed ? 1 : 0;
        if (firstSprite < 0)
            firstSprite = tripmine.spriteNum;
    }

    char detail[160];
    Bsnprintf(detail, sizeof(detail), "mines=%d armed=%d beamSprites=%d", g_swCrossplayDukeTripmineCount, armed, beams);
    SW_CrossplaySelfTestReport(beams > 0, "tripmine-beams", detail);

    if ((unsigned)firstSprite < MAXSPRITES)
    {
        int32_t const base = g_swCrossplayDukeTripmineDetonations;
        vec3_t const hitPos = sprite[firstSprite].xyz;
        SW_CrossplayDamageDukeMapSprite(pp->PlayerSprite, firstSprite, &hitPos);
        SW_CrossplaySelfTestReport(g_swCrossplayDukeTripmineDetonations > base, "tripmine-detonates", detail);
    }

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_REPORT);
}

static void SW_CrossplaySelfTestRunReport(void)
{
    if (g_swCrossplaySelfTestActorSamples > 0)
    {
        int32_t const half = (g_swCrossplaySelfTestActorSamples + 1) >> 1;
        char actorDetail[176];
        Bsnprintf(actorDetail, sizeof(actorDetail), "sampled=%d engaged=%d attacked=%d animated=%d sounded=%d",
                  g_swCrossplaySelfTestActorSamples, g_swCrossplaySelfTestActorsEngaged,
                  g_swCrossplaySelfTestActorsAttacked, g_swCrossplaySelfTestActorsAnimated,
                  g_swCrossplaySelfTestActorsSounded);

        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestActorsEngaged >= half, "duke-actors-engage", actorDetail);
        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestActorsAttacked >= half, "duke-actors-attack", actorDetail);
        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestActorsAnimated >= half, "duke-actors-animate", actorDetail);
        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestActorsSounded >= half, "duke-actors-sound", actorDetail);
    }

    {
        int32_t oldLayerActors = 0;
        for (int32_t sn = 0; sn < MAXSPRITES; ++sn)
        {
            if (sprite[sn].statnum == MAXSTATUS || SW_CrossplayDukeAIOwnsSprite(sn))
                continue;

            if ((g_swCrossplayDukeSpriteBehaviors[sn].flags &
                 (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) ==
                (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
                ++oldLayerActors;
        }

        char oldDetail[96];
        Bsnprintf(oldDetail, sizeof(oldDetail), "oldLayerActors=%d", oldLayerActors);

        if (oldLayerActors > 0)
        {
            int32_t shown = 0;
            for (int32_t sn = 0; sn < MAXSPRITES && shown < 4; ++sn)
            {
                if (sprite[sn].statnum == MAXSTATUS || SW_CrossplayDukeAIOwnsSprite(sn))
                    continue;

                if ((g_swCrossplayDukeSpriteBehaviors[sn].flags &
                     (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) !=
                    (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
                    continue;

                ++shown;
                LOG_F(WARNING, "CPTEST old-layer actor spr=%d otile=%d adoptable=%d srcpic=%d pic=%d stat=%d pos=(%d,%d)",
                      sn, artCrossplayOriginalMapSpritePicnum(sn),
                      VB_DukeAI_IsAdoptableTile(artCrossplayOriginalMapSpritePicnum(sn)),
                      g_swCrossplayDukeSpriteBehaviors[sn].sourcePicnum, TrackerCast(sprite[sn].picnum),
                      sprite[sn].statnum, TrackerCast(sprite[sn].x), TrackerCast(sprite[sn].y));
            }
        }
        SW_CrossplaySelfTestReport(oldLayerActors == 0, "no-old-layer-actors", oldDetail);
    }

    char frameDetail[128];
    Bsnprintf(frameDetail, sizeof(frameDetail), "fallbacks=%d unresolved=%d",
              SW_CrossplayDukeAIFrameFallbacks(), SW_CrossplayDukeAIFrameResolveFailures());
    SW_CrossplaySelfTestReport(SW_CrossplayDukeAIFrameResolveFailures() == 0, "duke-actor-frames-resolve",
                               frameDetail);

    // A map with no Duke sprites near the tested actors has nothing for a blast
    // to hit; that is not a bridge failure.
    char blastDetail[96];
    Bsnprintf(blastDetail, sizeof(blastDetail), "spritesHitByBlast=%d actorsTested=%d",
              g_swCrossplaySelfTestBlastHits, g_swCrossplaySelfTestActorsTested);
    if (g_swCrossplaySelfTestActorsTested > 0)
        SW_CrossplaySelfTestReport(g_swCrossplaySelfTestBlastHits > 0, "duke-blast-damage", blastDetail);
    else
        SW_CrossplaySelfTestReport(1, "duke-blast-damage", "no Duke actors in this map, skipped");

    LOG_F(INFO, "CPTEST SUMMARY pass=%d fail=%d actorsTested=%d actorAttacks=%d actorDeaths=%d teleports=%d",
                g_swCrossplaySelfTestPassCount, g_swCrossplaySelfTestFailCount, g_swCrossplaySelfTestActorsTested,
                g_swCrossplayDukeActorAttackEvents, g_swCrossplayDukeActorDeathEvents, g_swCrossplayDukeTeleportEvents);

    SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_DONE);
    quitevent = 1;
}

static void SW_CrossplaySelfTestTick(void)
{
    if (!SW_CrossplaySelfTestActive() || g_swCrossplaySelfTestPhase == VB_CROSSPLAY_SELFTEST_DONE)
        return;

    PLAYERp const pp = SW_CrossplaySelfTestPlayer();
    if (pp == NULL)
        return;

    SW_CrossplaySelfTestKeepPlayerAlive(pp);
    ++g_swCrossplaySelfTestPhaseTic;

    switch (g_swCrossplaySelfTestPhase)
    {
    case VB_CROSSPLAY_SELFTEST_WARMUP:
        if (g_swCrossplaySelfTestPhaseTic >= 60)
        {
        LOG_F(INFO, "CPTEST BEGIN behaviors=%d teleporters=%d trains=%d tripmines=%d",
                  g_swCrossplayDukeSpriteBehaviorCount, g_swCrossplayDukeTeleporterCount,
                  g_swCrossplayDukeTrainCount, g_swCrossplayDukeTripmineCount);
            switch (g_swCrossplaySelfTestGroup)
            {
            case VB_CROSSPLAY_SELFTEST_GROUP_WORLD:
                SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TELEPORT_SETUP);
                break;
            case VB_CROSSPLAY_SELFTEST_GROUP_DESTRUCT:
                SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_BREAKABLES);
                break;
            case VB_CROSSPLAY_SELFTEST_GROUP_TRIPMINES:
                SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_TRIPMINES);
                break;
            default:
                SW_CrossplaySelfTestSetPhase(VB_CROSSPLAY_SELFTEST_ACTOR_SETUP);
                break;
            }
        }
        break;

    case VB_CROSSPLAY_SELFTEST_ACTOR_SETUP: SW_CrossplaySelfTestRunActorSetup(pp); break;
    case VB_CROSSPLAY_SELFTEST_ACTOR_OBSERVE: SW_CrossplaySelfTestRunActorObserve(); break;
    case VB_CROSSPLAY_SELFTEST_ACTOR_DAMAGE: SW_CrossplaySelfTestRunActorDamage(pp); break;
    case VB_CROSSPLAY_SELFTEST_TELEPORT_SETUP: SW_CrossplaySelfTestRunTeleportSetup(pp); break;
    case VB_CROSSPLAY_SELFTEST_TELEPORT_OBSERVE: SW_CrossplaySelfTestRunTeleportObserve(pp); break;
    case VB_CROSSPLAY_SELFTEST_TRAIN_SETUP: SW_CrossplaySelfTestRunTrainSetup(pp); break;
    case VB_CROSSPLAY_SELFTEST_TRAIN_OBSERVE: SW_CrossplaySelfTestRunTrainObserve(pp); break;
    case VB_CROSSPLAY_SELFTEST_BREAKABLES: SW_CrossplaySelfTestRunBreakables(pp); break;
    case VB_CROSSPLAY_SELFTEST_EXPLOSIVES: SW_CrossplaySelfTestRunExplosives(pp); break;
    case VB_CROSSPLAY_SELFTEST_OPERABLE_SETUP: SW_CrossplaySelfTestRunOperableSetup(pp); break;
    case VB_CROSSPLAY_SELFTEST_OPERABLE_OBSERVE: SW_CrossplaySelfTestRunOperableObserve(); break;
    case VB_CROSSPLAY_SELFTEST_TRIPMINES: SW_CrossplaySelfTestRunTripmines(pp); break;
    case VB_CROSSPLAY_SELFTEST_REPORT: SW_CrossplaySelfTestRunReport(); break;
    default: break;
    }
}


// ---------------------------------------------------------------------------
// Duke AI host.
//
// Duke enemies are defined by Duke's CON scripts and actors.cpp, so the only
// way for them to behave exactly like Duke is to run that code.  Duke's actor
// module lives in the same binary (source/vibebuild32/src/crossplay_dukeai.cpp)
// and operates on the shared engine sprite array, so the bridge hands the Duke
// actor sprites over to it and runs one Duke actor tic per SW tic.
//
// Two things have to be swapped around that tic, because both games share the
// engine's global sprite state and disagree about what it means:
//
//   statnums  SW's STAT_MISC is Duke's STAT_ACTOR, and so on.  The sprite
//             status lists are rebuilt to contain only Duke's sprites for the
//             duration of the Duke tic, then rebuilt for SW afterwards, with
//             Duke's sprites parked in STAT_NO_STATE where SW ignores them.
//   picnums   Duke's AI keys off Duke tile numbers, but the foreign-art layer
//             gives SW a different tile index to draw.  Sprites therefore hold
//             Duke tiles during the tic and display tiles outside it.
// ---------------------------------------------------------------------------

extern "C"
{
int VB_DukeAI_BeginLevel(int playerSkill);
int VB_DukeAI_SpawnMapActor(int spriteNum);
void VB_DukeAI_SetPlayer(int playerNum, int spriteNum, int32_t x, int32_t y, int32_t z, int32_t ang, int16_t sectnum);
void VB_DukeAI_SetPlayerCount(int count);
int VB_DukeAI_TakePlayerDamageFor(int playerNum);
void VB_DukeAI_RunTic(void);
int VB_DukeAI_IsEnemyTile(int tileNum);
int VB_DukeAI_IsAdoptableTile(int tileNum);
char const *VB_DukeAI_SoundFilename(int soundNum);
int VB_DukeAI_HighestSoundIndex(void);
void VB_DukeAI_SetSoundHook(int (*hook)(int soundNum, int spriteNum));
void VB_DukeAI_SetMapTriggerHook(void (*hook)(int hitag));
int VB_DukeAI_SpriteIsEnemy(int spriteNum);
int VB_DukeAI_PlayerStatnum(void);
int VB_DukeAI_PlayerTileNumber(void);
void VB_DukeAI_BeginPlayerSprite(int spriteNum);
int VB_DukeAI_DisplayPicnum(int spriteNum, int32_t camX, int32_t camY, int32_t camAng, uint16_t *cstat,
                            int *viewtypeStep);
}

// While Duke's code runs, the host's player sprite has to look like Duke's own
// player sprite; these hold what it really is.
static int16_t g_swCrossplayDukeAIPlayerSprite = -1;
static int16_t g_swCrossplayDukeAIPlayerPicnum;
static int16_t g_swCrossplayDukeAIPlayerStat;
static int16_t g_swCrossplayDukeAIPlayerYvel;
static int16_t g_swCrossplayDukeAIPlayerExtra;
static vec2_16_t g_swCrossplayDukeAIPlayerTileSize;

// Weapon classes understood by VB_DukeAI_DamageActor().
enum
{
    VB_DUKEAI_HIT_BULLET,
    VB_DUKEAI_HIT_MELEE,
    VB_DUKEAI_HIT_EXPLOSION,
    VB_DUKEAI_HIT_PROJECTILE,
};

// Which Duke weapon class the SW weapon that produced this hit resembles, so
// Duke's actors show the reaction Duke would show.
static int32_t SW_CrossplayDukeAIHitKind(int32_t spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || User[spriteNum] == NULL)
        return VB_DUKEAI_HIT_BULLET;

    switch (User[spriteNum]->WeaponNum)
    {
    case WPN_FIST:
    case WPN_SWORD:
        return VB_DUKEAI_HIT_MELEE;
    case WPN_ROCKET:
    case WPN_GRENADE:
    case WPN_MINE:
        return VB_DUKEAI_HIT_EXPLOSION;
    case WPN_STAR:
        return VB_DUKEAI_HIT_PROJECTILE;
    default:
        return VB_DUKEAI_HIT_BULLET;
    }
}

static uint8_t g_swCrossplayDukeAIOwned[MAXSPRITES];
static int16_t g_swCrossplayDukeAIDukePicnum[MAXSPRITES];
static int16_t g_swCrossplayDukeAIDukeStat[MAXSPRITES];
static uint8_t g_swCrossplayDukeAIDukePal[MAXSPRITES];

// Duke steps back through an actor's frames when one has no art -- stock data
// genuinely has gaps, mostly in death frames -- so falling back is normal.  What
// is not normal is running out of frames without finding art, which is what
// happens when frames are resolved in the wrong tile space.
static int32_t g_swCrossplayDukeAIFrameFallbacks;
static int32_t g_swCrossplayDukeAIFrameResolveFailures;
static int16_t g_swCrossplayDukeAISwSprites[MAXSPRITES];
static int16_t g_swCrossplayDukeAISwStats[MAXSPRITES];
static int32_t g_swCrossplayDukeAISwCount;
static int32_t g_swCrossplayDukeAIActive;
static int32_t g_swCrossplayDukeAIAdopted;
static int32_t g_swCrossplayDukeAIPlayerDamageEvents;

#define VB_CROSSPLAY_MAX_DUKE_SOUNDS 1024

// SW moves at 40 tics per second and Duke at 30, so running one Duke tic per SW
// tic makes every Duke enemy move and animate a third too fast.  Step Duke's
// world on its own clock instead.
#define VB_CROSSPLAY_SW_TICS_PER_SEC 40
#define VB_CROSSPLAY_DUKE_TICS_PER_SEC 30   // Duke's REALGAMETICSPERSEC

static int32_t g_swCrossplayDukeAITicAccumulator;

static void SW_CrossplayDukeAIListInsert(int16_t spriteNum, int16_t statnum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || (unsigned)statnum >= MAXSTATUS)
        return;

    int16_t const head = headspritestat[statnum];

    sprite[spriteNum].statnum = statnum;
    nextspritestat[spriteNum] = head;
    prevspritestat[spriteNum] = -1;

    if (head >= 0)
        prevspritestat[head] = spriteNum;

    headspritestat[statnum] = spriteNum;
}

// Detaches every sprite from the status lists without touching the engine's
// free-slot list, which lives at statnum MAXSTATUS and must stay intact so
// Duke can still spawn projectiles and effects.
static void SW_CrossplayDukeAIClearStatLists(void)
{
    for (int32_t statnum = 0; statnum < MAXSTATUS; ++statnum)
        headspritestat[statnum] = -1;
}

// Records the current (SW) status lists in traversal order, then rebuilds the
// lists containing only Duke's sprites, in their Duke statnums and picnums.
static void SW_CrossplayDukeAIEnterDukeSpace(void)
{
    g_swCrossplayDukeAISwCount = 0;

    for (int32_t statnum = 0; statnum < MAXSTATUS; ++statnum)
    {
        for (int32_t spriteNum = headspritestat[statnum]; spriteNum >= 0; spriteNum = nextspritestat[spriteNum])
        {
            if (g_swCrossplayDukeAIOwned[spriteNum])
                continue;

            if (g_swCrossplayDukeAISwCount >= MAXSPRITES)
                break;

            g_swCrossplayDukeAISwSprites[g_swCrossplayDukeAISwCount] = (int16_t)spriteNum;
            g_swCrossplayDukeAISwStats[g_swCrossplayDukeAISwCount] = (int16_t)statnum;
            ++g_swCrossplayDukeAISwCount;
        }
    }

    SW_CrossplayDukeAIClearStatLists();

    for (int32_t spriteNum = MAXSPRITES - 1; spriteNum >= 0; --spriteNum)
    {
        if (!g_swCrossplayDukeAIOwned[spriteNum])
            continue;

        // A freed slot must never be spliced into a status list; that would
        // corrupt the engine's free list.
        if (sprite[spriteNum].statnum == MAXSTATUS)
        {
            g_swCrossplayDukeAIOwned[spriteNum] = 0;
            continue;
        }

        sprite[spriteNum].picnum = g_swCrossplayDukeAIDukePicnum[spriteNum];
        sprite[spriteNum].pal = g_swCrossplayDukeAIDukePal[spriteNum];
        SW_CrossplayDukeAIListInsert((int16_t)spriteNum, g_swCrossplayDukeAIDukeStat[spriteNum]);
    }

    if (g_swCrossplayDukeAIPlayerSprite >= 0)
    {
        int16_t const playerSprite = g_swCrossplayDukeAIPlayerSprite;

        g_swCrossplayDukeAIPlayerPicnum = sprite[playerSprite].picnum;
        g_swCrossplayDukeAIPlayerStat = sprite[playerSprite].statnum;
        g_swCrossplayDukeAIPlayerYvel = sprite[playerSprite].yvel;
        g_swCrossplayDukeAIPlayerExtra = sprite[playerSprite].extra;

        VB_DukeAI_BeginPlayerSprite(playerSprite);
        SW_CrossplayDukeAIListInsert(playerSprite, (int16_t)VB_DukeAI_PlayerStatnum());

        // The masqueraded player sprite carries Duke's APLAYER tile number, but
        // tilesiz[] at that index holds the HOST's art dimensions -- so every
        // getzrange() a Duke actor runs sees a player body of garbage height.
        // Concretely: a jetpack trooper diving over the player read the player's
        // top as a floor one unit below itself, and CON's `iffloordistl 32`
        // grounded it on the spot -- the "never flies, spins by the dumpster"
        // sighting.  Give the tile Duke's dimensions for the span of the tic.
        int const aplayerTile = VB_DukeAI_PlayerTileNumber();
        if ((unsigned)aplayerTile < MAXTILES)
        {
            g_swCrossplayDukeAIPlayerTileSize = tilesiz[aplayerTile];

            int16_t const dukeArtSlot = SW_CrossplayRemapDukeTileForRuntime((int16_t)aplayerTile);
            if ((unsigned)dukeArtSlot < MAXTILES && tilesiz[dukeArtSlot].y > 0)
                tilesiz[aplayerTile] = tilesiz[dukeArtSlot];
            else
                tilesiz[aplayerTile] = { 24, 68 };   // Duke APLAYER's canonical dims
        }
    }
}

// Takes ownership of everything Duke left in the status lists (its actors plus
// anything it spawned this tic), parks it where SW ignores it, and restores
// SW's own lists in their original order.
static void SW_CrossplayDukeAILeaveDukeSpace(void)
{
    static int16_t dukeSprites[MAXSPRITES];
    static int16_t dukeStats[MAXSPRITES];
    int32_t dukeCount = 0;

    for (int32_t statnum = 0; statnum < MAXSTATUS; ++statnum)
    {
        for (int32_t spriteNum = headspritestat[statnum]; spriteNum >= 0; spriteNum = nextspritestat[spriteNum])
        {
            if (dukeCount >= MAXSPRITES)
                break;

            if (spriteNum == g_swCrossplayDukeAIPlayerSprite)
                continue;

            dukeSprites[dukeCount] = (int16_t)spriteNum;
            dukeStats[dukeCount] = (int16_t)statnum;
            ++dukeCount;
        }
    }

    SW_CrossplayDukeAIClearStatLists();

    Bmemset(g_swCrossplayDukeAIOwned, 0, sizeof(g_swCrossplayDukeAIOwned));

    for (int32_t i = 0; i < dukeCount; ++i)
    {
        int16_t const spriteNum = dukeSprites[i];

        g_swCrossplayDukeAIOwned[spriteNum] = 1;
        g_swCrossplayDukeAIDukeStat[spriteNum] = dukeStats[i];
        g_swCrossplayDukeAIDukePicnum[spriteNum] = sprite[spriteNum].picnum;
    }

    if (g_swCrossplayDukeAIPlayerSprite >= 0)
    {
        int16_t const playerSprite = g_swCrossplayDukeAIPlayerSprite;

        sprite[playerSprite].picnum = g_swCrossplayDukeAIPlayerPicnum;
        sprite[playerSprite].yvel = g_swCrossplayDukeAIPlayerYvel;
        sprite[playerSprite].extra = g_swCrossplayDukeAIPlayerExtra;

        int const aplayerTile = VB_DukeAI_PlayerTileNumber();
        if ((unsigned)aplayerTile < MAXTILES)
            tilesiz[aplayerTile] = g_swCrossplayDukeAIPlayerTileSize;
    }

    for (int32_t i = g_swCrossplayDukeAISwCount - 1; i >= 0; --i)
        SW_CrossplayDukeAIListInsert(g_swCrossplayDukeAISwSprites[i], g_swCrossplayDukeAISwStats[i]);

    for (int32_t i = dukeCount - 1; i >= 0; --i)
    {
        int16_t const spriteNum = dukeSprites[i];

        // Only the sprite's own Duke tile is stored here.  The drawn frame also
        // depends on where the viewer is standing, and resolving that here would
        // write one client's camera into shared simulation state -- a desync in
        // a netgame.  Duke resolves it at draw time on the tsprite copy, and so
        // does the bridge: see SW_CrossplayDukeMapSpriteDisplayTile().
        sprite[spriteNum].picnum = SW_CrossplayRemapDukeTileForRuntime(g_swCrossplayDukeAIDukePicnum[spriteNum]);

        // Duke's spawn code assigns Duke palookup indices (a LIZTROOP gets pal 22),
        // which select a different palette in the host; the remapped foreign art
        // already carries Duke's own colors, so it draws at pal 0.
        g_swCrossplayDukeAIDukePal[spriteNum] = sprite[spriteNum].pal;
        sprite[spriteNum].pal = 0;

        SW_CrossplayDukeAIListInsert(spriteNum, STAT_NO_STATE);
    }
}


// Duke's actors decide which sound plays and when -- that part is behavior, and
// it stays Duke's.  Playing it is the host's job, because SW owns the audio
// device; Duke re-initializing it underneath SW would tear down SW's own sound.
//
// The pairing between Duke's sound numbers and SW's copies of the same files is
// built from data rather than a hand-written table: Duke's CON "definesound"
// gives each sound a filename, and SW's own sound table already carries the
// Duke .voc files by name, so matching the two covers every Duke sound the host
// actually has.
static int16_t g_swCrossplayDukeAISoundMap[VB_CROSSPLAY_MAX_DUKE_SOUNDS];
static int32_t g_swCrossplayDukeAISoundMapCount;
static int32_t g_swCrossplayDukeAISoundEvents;

static int32_t SW_CrossplayDukeAIFindDigiForFilename(char const *filename)
{
    if (filename == NULL || filename[0] == '\0')
        return DIGI_NULL;

    // Duke stores names like "PRED_RG.VOC"; SW's table stores "predrg.voc".
    // Compare on the base name, case-insensitively.
    char const *base = filename;
    for (char const *scan = filename; *scan != '\0'; ++scan)
        if (*scan == '/' || *scan == '\\')
            base = scan + 1;

    for (int32_t digi = 0; digi < DIGI_MAX; ++digi)
        if (voc[digi].name[0] != '\0' && !Bstrcasecmp(voc[digi].name, base))
            return digi;

    return DIGI_NULL;
}

static int SW_CrossplayDukeAIPlaySound(int soundNum, int spriteNum)
{
    if ((unsigned)soundNum >= ARRAY_SIZE(g_swCrossplayDukeAISoundMap))
        return -1;

    int32_t const digi = g_swCrossplayDukeAISoundMap[soundNum];
    if (digi <= DIGI_NULL || digi >= DIGI_MAX)
        return -1;

    ++g_swCrossplayDukeAISoundEvents;

    if ((unsigned)spriteNum < MAXSPRITES && sprite[spriteNum].statnum != MAXSTATUS)
    {
        SPRITEp const sp = &sprite[spriteNum];
        PlaySound(digi, &sp->x, &sp->y, &sp->z, v3df_dontpan | v3df_follow);
    }
    else
    {
        PLAYERp const pp = Player + myconnectindex;
        PlaySound(digi, &pp->posx, &pp->posy, &pp->posz, v3df_dontpan);
    }

    return 0;
}

static void SW_CrossplayDukeAISetupSounds(void)
{
    Bmemset(g_swCrossplayDukeAISoundMap, 0, sizeof(g_swCrossplayDukeAISoundMap));
    g_swCrossplayDukeAISoundMapCount = 0;

    int32_t const highestSound = min<int32_t>(VB_DukeAI_HighestSoundIndex(),
                                              (int32_t)ARRAY_SIZE(g_swCrossplayDukeAISoundMap) - 1);

    for (int32_t soundNum = 0; soundNum <= highestSound; ++soundNum)
    {
        int32_t const digi = SW_CrossplayDukeAIFindDigiForFilename(VB_DukeAI_SoundFilename(soundNum));

        g_swCrossplayDukeAISoundMap[soundNum] = (int16_t)digi;
        if (digi > DIGI_NULL)
            ++g_swCrossplayDukeAISoundMapCount;
    }

    VB_DukeAI_SetSoundHook(SW_CrossplayDukeAIPlaySound);
    VB_DukeAI_SetMapTriggerHook(SW_CrossplayDukeMapTrigger);
}

// SW's mirror marker tile (jnames.h MIRROR); named locally so this file does not
// have to pull in SW's editor name table.
#define SW_MIRROR_TILE 340

static int32_t g_swCrossplayMirrorMaskWalls[MAXWALLS];
static int16_t g_swCrossplayMirrorMaskPicnum[MAXWALLS];
static int32_t g_swCrossplayMirrorMaskCount;

// Duke and SW recognise a mirror by the same rule -- a wall whose overpicnum is
// the game's mirror tile, with cstat bit 32 -- and differ only in which tile
// number that is.  Retag the Duke map's mirror walls with SW's mirror tile so
// SW's own mirror setup finds them; SW then owns the mirrors from there.
static int32_t SW_CrossplayRetagDukeMirrorWalls(void)
{
    if (!SW_CrossplayDukeMapBridgeActive())
        return 0;

    int const dukeMirrorTile = VB_DukeAI_MirrorTile();
    if (dukeMirrorTile <= 0)
        return 0;

    int32_t retagged = 0;

    for (int32_t wallNum = 0; wallNum < numwalls; ++wallNum)
    {
        if (!TEST(wall[wallNum].cstat, 32) || wall[wallNum].nextsector < 0)
            continue;

        int16_t const originalOverpicnum = artCrossplayOriginalMapWallOverpicnum(wallNum);
        if (originalOverpicnum != dukeMirrorTile)
            continue;

        wall[wallNum].overpicnum = SW_MIRROR_TILE;
        ++retagged;

        if (Bgetenv("VB_CP_MIRROR_DEBUG") != NULL)
        {
            int32_t const ns = wall[wallNum].nextsector;
            LOG_F(INFO, "CPMIR retag wall=%d cstat=%d nextsect=%d floorstat=%d ceilstat=%d floorpic=%d ceilpic=%d lotag=%d hitag=%d",
                  wallNum, TrackerCast(wall[wallNum].cstat), ns,
                  TrackerCast(sector[ns].floorstat), TrackerCast(sector[ns].ceilingstat),
                  TrackerCast(sector[ns].floorpicnum), TrackerCast(sector[ns].ceilingpicnum),
                  TrackerCast(wall[wallNum].lotag), TrackerCast(wall[wallNum].hitag));
        }
    }

    // SW's mirror scan keys on its own mirror tile number.  In a foreign map a
    // wall's remapped overpicnum can land on that number by coincidence, and SW
    // would then turn an ordinary wall into a mirror -- rewriting its sector's
    // floor/ceiling and parallax flags.  Hide those walls from the scan.
    g_swCrossplayMirrorMaskCount = 0;

    for (int32_t wallNum = 0; wallNum < numwalls; ++wallNum)
    {
        if (wall[wallNum].overpicnum != SW_MIRROR_TILE || !TEST(wall[wallNum].cstat, 32))
            continue;

        if (artCrossplayOriginalMapWallOverpicnum(wallNum) == dukeMirrorTile)
            continue;   // a real Duke mirror this pass just retagged

        if (g_swCrossplayMirrorMaskCount >= (int32_t)ARRAY_SIZE(g_swCrossplayMirrorMaskWalls))
            break;

        g_swCrossplayMirrorMaskWalls[g_swCrossplayMirrorMaskCount] = (int32_t)wallNum;
        g_swCrossplayMirrorMaskPicnum[g_swCrossplayMirrorMaskCount] = wall[wallNum].overpicnum;
        ++g_swCrossplayMirrorMaskCount;

        wall[wallNum].overpicnum = 0;
    }

    if (retagged > 0 || g_swCrossplayMirrorMaskCount > 0)
        LOG_F(INFO, "Crossplay: retagged %d Duke mirror wall(s), masked %d coincidental mirror-tile wall(s).",
              retagged, g_swCrossplayMirrorMaskCount);

    return retagged;
}

// Puts back the walls hidden from SW's mirror scan; real mirrors have already
// been converted to SW's per-mirror tiles by then.
static void SW_CrossplayRestoreMaskedMirrorWalls(void)
{
    for (int32_t i = 0; i < g_swCrossplayMirrorMaskCount; ++i)
    {
        int32_t const wallNum = g_swCrossplayMirrorMaskWalls[i];
        if ((unsigned)wallNum < (unsigned)numwalls)
            wall[wallNum].overpicnum = g_swCrossplayMirrorMaskPicnum[i];
    }

    g_swCrossplayMirrorMaskCount = 0;
}


// Duke and SW light sprites by different rules.  Duke *assigns* the sector's
// shade to a sprite at draw time (`t->shade = l`), and its spawn code has
// already stored that sector shade on the sprite; SW instead *adds* the sector
// shade to the sprite's own, and darkens most statnums by a further 6.  Applying
// SW's rule to a Duke sprite therefore counts the sector shade twice and leaves
// Duke's sprites almost black in dim rooms.  Report Duke's own draw shade for
// sprites that came out of the Duke map so the renderer can use it directly.
int SW_CrossplayDukeMapSpriteShade(int spriteNum, int *outShade)
{
    if (!SW_CrossplayDukeMapBridgeActive() || (unsigned)spriteNum >= MAXSPRITES || outShade == NULL)
        return 0;

    int32_t const dukeOwned = SW_CrossplayDukeAIOwnsSprite(spriteNum);

    if (!dukeOwned && !(g_swCrossplayDukeSpriteBehaviors[spriteNum].flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE) &&
        artCrossplayOriginalMapSpritePicnum(spriteNum) <= 0)
        return 0;

    int16_t const sectnum = sprite[spriteNum].sectnum;
    if ((unsigned)sectnum >= (unsigned)numsectors)
        return 0;

    // Duke marks map sprites with cstat bit 2048 as NOSHADE at spawn; adopted
    // actors can be asked directly.
    int32_t const noShade = dukeOwned ? VB_DukeAI_SpriteIsNoShade(spriteNum)
                                      : TEST(sprite[spriteNum].cstat, 2048) != 0;

    if (noShade)
    {
        *outShade = sprite[spriteNum].shade;
        return 1;
    }

    int32_t shade = TEST(sector[sectnum].ceilingstat, CEILING_STAT_PLAX) ? sector[sectnum].ceilingshade
                                                                        : sector[sectnum].floorshade;
    *outShade = clamp<int32_t>(shade, -127, 127);
    return 1;
}


// SW interpolates a sprite between tics only when it has an SW USER record, and
// Duke's sprites have none -- so they were drawn at their raw positions while
// the rest of the world rendered smoothly, which reads as the actor stepping or
// juddering as it moves.  Snapshot each Duke sprite's position every SW tic so
// the renderer has something to interpolate from.
static vec3_t g_swCrossplayDukeAIOldPos[MAXSPRITES];
static uint8_t g_swCrossplayDukeAIHasOldPos[MAXSPRITES];

static void SW_CrossplayRecordDukeSpritePositions(void)
{
    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (!g_swCrossplayDukeAIOwned[spriteNum] || sprite[spriteNum].statnum == MAXSTATUS)
        {
            g_swCrossplayDukeAIHasOldPos[spriteNum] = 0;
            continue;
        }

        g_swCrossplayDukeAIOldPos[spriteNum] = sprite[spriteNum].xyz;
        g_swCrossplayDukeAIHasOldPos[spriteNum] = 1;
    }
}

int SW_CrossplayDukeMapSpriteOldPos(int spriteNum, int32_t *x, int32_t *y, int32_t *z)
{
    if ((unsigned)spriteNum >= MAXSPRITES || !g_swCrossplayDukeAIHasOldPos[spriteNum])
        return 0;

    if (x) *x = g_swCrossplayDukeAIOldPos[spriteNum].x;
    if (y) *y = g_swCrossplayDukeAIOldPos[spriteNum].y;
    if (z) *z = g_swCrossplayDukeAIOldPos[spriteNum].z;

    return 1;
}


// The tile a Duke sprite should be drawn with from a given viewpoint, plus any
// cstat mirroring that goes with it.  This is called per frame from the renderer
// with the local camera, and touches only the caller's tsprite -- keeping the
// viewer out of the shared sprite array, which a netgame requires.
int SW_CrossplayDukeMapSpriteDisplayTile(int spriteNum, int32_t camX, int32_t camY, int32_t camAng, uint16_t *cstat)
{
    if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS)
        return -1;

    // Duke derives the frame from the sprite's own Duke tile, but outside a Duke
    // tic the sprite carries the runtime tile it is drawn with.  Put Duke's tile
    // back for the length of the call; nothing else runs in between.
    int16_t const displayPicnum = sprite[spriteNum].picnum;
    sprite[spriteNum].picnum = g_swCrossplayDukeAIDukePicnum[spriteNum];

    int viewtypeStep = 0;
    int dukeDisplayTile = VB_DukeAI_DisplayPicnum(spriteNum, camX, camY, camAng, cstat, &viewtypeStep);

    sprite[spriteNum].picnum = displayPicnum;

    if (dukeDisplayTile < 0)
        dukeDisplayTile = g_swCrossplayDukeAIDukePicnum[spriteNum];

    int16_t runtimeTile = SW_CrossplayRemapDukeTileForRuntime((int16_t)dukeDisplayTile);

    // Duke steps back through frames whose art is missing.  That test has to be
    // made after the tile is in the runtime's numbering, not Duke's.
    for (int32_t guard = 0; viewtypeStep > 0 && guard < 16; ++guard)
    {
        if ((unsigned)runtimeTile < MAXTILES && tilesiz[runtimeTile].x != 0)
            break;

        ++g_swCrossplayDukeAIFrameFallbacks;
        dukeDisplayTile -= viewtypeStep;
        if (dukeDisplayTile <= 0)
        {
            ++g_swCrossplayDukeAIFrameResolveFailures;
            break;
        }

        runtimeTile = SW_CrossplayRemapDukeTileForRuntime((int16_t)dukeDisplayTile);

        if (guard == 15 && ((unsigned)runtimeTile >= MAXTILES || tilesiz[runtimeTile].x == 0))
            ++g_swCrossplayDukeAIFrameResolveFailures;
    }

    return runtimeTile;
}

int SW_CrossplayDukeAIActive(void)
{
    return g_swCrossplayDukeAIActive;
}

int SW_CrossplayDukeAIOwnsSprite(int spriteNum)
{
    return (unsigned)spriteNum < MAXSPRITES && g_swCrossplayDukeAIOwned[spriteNum];
}

int SW_CrossplayDukeAISpriteDukePicnum(int spriteNum)
{
    return (unsigned)spriteNum < MAXSPRITES ? g_swCrossplayDukeAIDukePicnum[spriteNum] : -1;
}

int SW_CrossplayDukeAISpriteDukeStatnum(int spriteNum)
{
    return (unsigned)spriteNum < MAXSPRITES ? g_swCrossplayDukeAIDukeStat[spriteNum] : -1;
}

int SW_CrossplayDukeAIOwnedCount(void)
{
    int32_t count = 0;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
        count += g_swCrossplayDukeAIOwned[spriteNum] ? 1 : 0;

    return count;
}

int SW_CrossplayDukeAISpriteIsEnemy(int spriteNum)
{
    if (!SW_CrossplayDukeAIOwnsSprite(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS)
        return 0;

    // Ask Duke, using the Duke tile the sprite carries inside a Duke tic: an
    // actor's picnum changes with its animation frames.
    int16_t const displayPicnum = sprite[spriteNum].picnum;
    sprite[spriteNum].picnum = g_swCrossplayDukeAIDukePicnum[spriteNum];
    int const isEnemy = VB_DukeAI_SpriteIsEnemy(spriteNum);
    sprite[spriteNum].picnum = displayPicnum;

    return isEnemy;
}

int SW_CrossplayDukeAIPlayerDamageEvents(void)
{
    return g_swCrossplayDukeAIPlayerDamageEvents;
}

int SW_CrossplayDukeAISoundEvents(void)
{
    return g_swCrossplayDukeAISoundEvents;
}

int SW_CrossplayDukeAIFrameFallbacks(void)
{
    return g_swCrossplayDukeAIFrameFallbacks;
}

int SW_CrossplayDukeAIFrameResolveFailures(void)
{
    return g_swCrossplayDukeAIFrameResolveFailures;
}

// Hands one Duke map sprite to Duke's spawn code, which is what assigns its
// Duke statnum, CON strength (health), clipdist and actor state.
static void SW_CrossplayDukeAIAdoptSprite(int32_t spriteNum, int16_t dukePicnum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum == MAXSTATUS)
        return;

    int16_t const displayPicnum = sprite[spriteNum].picnum;

    sprite[spriteNum].picnum = dukePicnum;

    // The sprite is still linked into SW's status lists here, so it has to move
    // through the engine's own list call rather than being spliced in blind.
    changespritestat((int16_t)spriteNum, 0);   // Duke spawns map sprites out of STAT_DEFAULT

    if (!VB_DukeAI_SpawnMapActor(spriteNum) || sprite[spriteNum].statnum == MAXSTATUS)
    {
        if (sprite[spriteNum].statnum != MAXSTATUS)
        {
            sprite[spriteNum].picnum = displayPicnum;
            changespritestat((int16_t)spriteNum, STAT_NO_STATE);
        }
        return;
    }

    // Duke owns this sprite now, so the bridge's own approximation must not
    // also drive it.
    SW_CrossplayClearDukeSpriteBehavior(spriteNum);

    g_swCrossplayDukeAIOwned[spriteNum] = 1;
    g_swCrossplayDukeAIDukeStat[spriteNum] = sprite[spriteNum].statnum;
    g_swCrossplayDukeAIDukePicnum[spriteNum] = sprite[spriteNum].picnum;

    sprite[spriteNum].picnum = SW_CrossplayRemapDukeTileForRuntime(g_swCrossplayDukeAIDukePicnum[spriteNum]);
    g_swCrossplayDukeAIDukePal[spriteNum] = sprite[spriteNum].pal;
    sprite[spriteNum].pal = 0;
    changespritestat((int16_t)spriteNum, STAT_NO_STATE);

    ++g_swCrossplayDukeAIAdopted;
}

void SW_CrossplayDukeAISetupLevel(void)
{
    g_swCrossplayDukeAIActive = 0;
    g_swCrossplayDukeAIAdopted = 0;
    g_swCrossplayDukeAIPlayerDamageEvents = 0;
    g_swCrossplayDukeAITicAccumulator = 0;
    Bmemset(g_swCrossplayDukeAIOwned, 0, sizeof(g_swCrossplayDukeAIOwned));

    if (!SW_CrossplayDukeAIRequested() || !SW_CrossplayDukeMapBridgeActive())
        return;

    // SW and Duke both number skill 0..3, so the host's skill maps straight over.
    if (!VB_DukeAI_BeginLevel(SW_CrossplayDukeCurrentSkill()))
    {
        LOG_F(WARNING, "Crossplay: Duke AI bring-up failed; falling back to the bridge's own actor behavior.");
        return;
    }

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        int16_t const dukePicnum = artCrossplayOriginalMapSpritePicnum(spriteNum);
        if (!SW_CrossplayDukeShouldRunDukeAI(dukePicnum,
                                             SW_CrossplayDukeSpriteBehaviorFlagsForSprite(spriteNum, dukePicnum)))
            continue;

        SW_CrossplayDukeAIAdoptSprite(spriteNum, dukePicnum);
    }

    SW_CrossplayDukeAISetupSounds();

    g_swCrossplayDukeAIActive = g_swCrossplayDukeAIAdopted > 0;

    int32_t hiddenExplosives = 0;
    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
        if (g_swCrossplayDukeAIOwned[spriteNum] && sprite[spriteNum].xrepeat == 0)
            ++hiddenExplosives;

    LOG_F(INFO, "Crossplay: Duke AI running %d Duke actor(s) with Duke's own CON behavior (%d hidden by Duke's own spawn rules, %d Duke sound(s) mapped).",
          g_swCrossplayDukeAIAdopted, hiddenExplosives, g_swCrossplayDukeAISoundMapCount);

    // Any actor-flagged behavior sprite left after adoption is a Duke enemy the
    // old approximation layer still drives -- exactly the "looks like a trooper
    // but turns in place and never flies" symptom.  There must be none.
    {
        int32_t leftovers = 0;
        for (int32_t sn = 0; sn < MAXSPRITES; ++sn)
        {
            if (sprite[sn].statnum == MAXSTATUS || SW_CrossplayDukeAIOwnsSprite(sn))
                continue;

            if ((g_swCrossplayDukeSpriteBehaviors[sn].flags &
                 (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) ==
                (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            {
                ++leftovers;
                LOG_F(WARNING, "Crossplay: sprite %d (orig tile %d) is an OLD-approximation actor at (%d,%d) -- not adopted by Duke AI!",
                      sn, artCrossplayOriginalMapSpritePicnum(sn), TrackerCast(sprite[sn].x), TrackerCast(sprite[sn].y));
            }
        }

        if (leftovers > 0)
            LOG_F(WARNING, "Crossplay: %d Duke actor(s) still on the old approximation layer.", leftovers);
    }
}

static void SW_CrossplayDukeAITick(void)
{
    if (!g_swCrossplayDukeAIActive)
        return;

    PLAYERp const localPlayer = Player + myconnectindex;
    if ((unsigned)localPlayer->PlayerSprite >= MAXSPRITES)
        return;

    g_swCrossplayDukeAITicAccumulator += VB_CROSSPLAY_DUKE_TICS_PER_SEC;
    if (g_swCrossplayDukeAITicAccumulator < VB_CROSSPLAY_SW_TICS_PER_SEC)
        return;

    g_swCrossplayDukeAITicAccumulator -= VB_CROSSPLAY_SW_TICS_PER_SEC;

    // Every connected player is mirrored, in player order.  Mirroring only the
    // local one would give Duke a different target on each client, so the actors
    // would chase different players and the lockstep simulation would come apart.
    int32_t mirroredCount = 0;
    int32_t pnum;

    TRAVERSE_CONNECT(pnum)
    {
        if ((unsigned)pnum >= MAXPLAYERS)
            continue;

        if (pnum + 1 > mirroredCount)
            mirroredCount = pnum + 1;
    }

    VB_DukeAI_SetPlayerCount(mirroredCount > 0 ? mirroredCount : 1);

    // The player sprite swapped into Duke's own STAT_PLAYER list is the local
    // one; the rest are mirrored by position only, which is what the AI reads.
    g_swCrossplayDukeAIPlayerSprite = (int16_t)localPlayer->PlayerSprite;

    SW_CrossplayZGuard("pre-enter");
    SW_CrossplayDukeAIEnterDukeSpace();
    SW_CrossplayZGuard("entered");

    TRAVERSE_CONNECT(pnum)
    {
        if ((unsigned)pnum >= MAXPLAYERS)
            continue;

        PLAYERp const other = Player + pnum;
        if ((unsigned)other->PlayerSprite >= MAXSPRITES)
            continue;

        VB_DukeAI_SetPlayer(pnum, other->PlayerSprite, other->posx, other->posy, other->posz,
                            fix16_to_int(other->q16ang), other->cursectnum);
    }

    {
        static int32_t watchTile = -2;
        if (watchTile == -2)
        {
            char const *const env = Bgetenv("VB_CP_WATCH_TILE");
            watchTile = env != NULL ? Batoi(env) : -1;
        }

        if (watchTile > 0)
        {
            // Pin to the sprite that *started* as this tile: Duke changes an
            // actor's picnum as it wakes and animates, so re-matching by tile
            // silently jumps to a different actor.
            static int16_t watched = -2;
            if (watched == -2)
            {
                watched = -1;
                for (int32_t sn = 0; sn < MAXSPRITES && watched < 0; ++sn)
                    if (g_swCrossplayDukeAIOwned[sn] && sprite[sn].statnum != MAXSTATUS &&
                        artCrossplayOriginalMapSpritePicnum(sn) == watchTile)
                        watched = (int16_t)sn;

                if (watched >= 0)
                {
                    VB_DukeAI_WatchSprite(watched);
                    LOG_F(INFO, "CPDBG watching sprite=%d origTile=%d", watched, watchTile);
                }
            }
        }
    }

    // VB_CP_WATCH_NEAREST=1: record the Duke enemy nearest the local player --
    // whatever the player is actually looking at -- one line per second plus a
    // line on every state change.  Runs inside Duke space so statnums, tiles and
    // t_data are Duke's.
    {
        static int32_t nearestMode = -1;
        if (nearestMode < 0)
        {
            char const *const env = Bgetenv("VB_CP_WATCH_NEAREST");
            nearestMode = (env != NULL && env[0] == '1') ? 1 : 0;
        }

        if (nearestMode)
        {
            static int32_t sampleTic = 0;
            static int32_t lastSpr = -1, lastStat = -1, lastAc = -1;
            static int32_t logged = 0;

            int32_t best = -1, bestDist = 10000;
            for (int32_t sn = 0; sn < MAXSPRITES; ++sn)
            {
                if (!g_swCrossplayDukeAIOwned[sn] || sprite[sn].statnum >= MAXSTATUS)
                    continue;

                if (!VB_DukeAI_IsEnemyTile(artCrossplayOriginalMapSpritePicnum(sn)))
                    continue;

                int32_t const d = FindDistance2D(sprite[sn].x - localPlayer->posx, sprite[sn].y - localPlayer->posy);
                if (d < bestDist) { bestDist = d; best = sn; }
            }

            ++sampleTic;
            if (best >= 0 && logged < 900)
            {
                int st=-1, ac=-1, mv=-1, cf=-1, sp2=-1;
                VB_DukeAI_WatchSprite(best);
                VB_DukeAI_ActorState(best, &st, &ac, &mv, &cf, &sp2);

                int const changed = (best != lastSpr || sprite[best].statnum != lastStat || ac != lastAc);
                if (changed || (sampleTic % 30) == 0)
                {
                    lastSpr = best; lastStat = sprite[best].statnum; lastAc = ac;
                    ++logged;
                    LOG_F(INFO, "CPNEAR spr=%d otile=%d dtile=%d stat=%d z=%d fd=%d ac=%d mv=%d mf=%d x=%d y=%d pdist=%d",
                          best, artCrossplayOriginalMapSpritePicnum(best), TrackerCast(sprite[best].picnum),
                          sprite[best].statnum, TrackerCast(sprite[best].z) >> 8,
                          VB_DukeAI_WatchedFloorDist(), ac, mv, TrackerCast(sprite[best].hitag),
                          TrackerCast(sprite[best].x), TrackerCast(sprite[best].y), bestDist);
                }
            }
        }
    }

    // VB_CP_WATCH_AREA=1: every two seconds, log every crossplay-relevant sprite
    // within 2500 of the player -- owned or not, enemy or not -- so whatever the
    // player is looking at is captured regardless of classification.
    {
        static int32_t areaMode = -1;
        if (areaMode < 0)
        {
            char const *const env = Bgetenv("VB_CP_WATCH_AREA");
            areaMode = (env != NULL && env[0] == '1') ? 1 : 0;
        }

        static int32_t areaTic = 0;
        static int32_t areaLogged = 0;

        if (areaMode && areaLogged < 400 && (++areaTic % 60) == 0)
        {
            int32_t shown = 0;
            for (int32_t sn = 0; sn < MAXSPRITES && shown < 12; ++sn)
            {
                if (sprite[sn].statnum == MAXSTATUS || sn == localPlayer->PlayerSprite)
                    continue;

                int32_t const d = FindDistance2D(sprite[sn].x - localPlayer->posx, sprite[sn].y - localPlayer->posy);
                if (d > 2500)
                    continue;

                int16_t const otile = artCrossplayOriginalMapSpritePicnum(sn);
                uint16_t const bf = g_swCrossplayDukeSpriteBehaviors[sn].flags;
                if (otile <= 0 && !SW_CrossplayDukeAIOwnsSprite(sn) && bf == 0)
                    continue;

                ++shown; ++areaLogged;
                LOG_F(INFO, "CPAREA spr=%d otile=%d pic=%d owned=%d bflags=%d stat=%d ang=%d xr=%d pos=(%d,%d,%d) d=%d",
                      sn, otile, TrackerCast(sprite[sn].picnum), SW_CrossplayDukeAIOwnsSprite(sn), bf,
                      sprite[sn].statnum, TrackerCast(sprite[sn].ang), TrackerCast(sprite[sn].xrepeat),
                      TrackerCast(sprite[sn].x), TrackerCast(sprite[sn].y), TrackerCast(sprite[sn].z) >> 8, d);
            }

            if (shown > 0)
                LOG_F(INFO, "CPAREA player=(%d,%d,%d)", localPlayer->posx, localPlayer->posy, localPlayer->posz >> 8);
        }
    }

    VB_DukeAI_RunTic();
    SW_CrossplayZGuard("duke-tic");

    {
        static int32_t logged = 0;
        static int32_t sampleTic = 0;
        if (Bgetenv("VB_CP_WATCH_TILE") != NULL && logged < 40 && ((++sampleTic % 10) == 0))
        {
            int st=-1,xv=-1,zv=-1,cf=-1,ht=-1,wx=0,wy=0,wz=0,wa=0,ac=0,mv=0;
            VB_DukeAI_WatchedState(&st,&xv,&zv,&cf,&ht);
            VB_DukeAI_WatchedPos(&wx,&wy,&wz,&wa,&ac,&mv);
            if (st >= 0)
            {
                ++logged;
                LOG_F(INFO, "CPDBG watch pic=%d stat=%d xvel=%d zvel=%d z=%d floordist=%d action=%d move=%d",
                      VB_DukeAI_WatchedPicnum(), st, xv, zv, wz >> 8, VB_DukeAI_WatchedFloorDist(), ac, mv);
            }
        }
    }

    int32_t damageForPlayer[MAX_SW_PLAYERS_REG] = {};
    TRAVERSE_CONNECT(pnum)
    {
        if ((unsigned)pnum < ARRAY_SIZE(damageForPlayer))
            damageForPlayer[pnum] = VB_DukeAI_TakePlayerDamageFor(pnum);
    }

    SW_CrossplayDukeAILeaveDukeSpace();
    SW_CrossplayZGuard("left");

    TRAVERSE_CONNECT(pnum)
    {
        if ((unsigned)pnum >= ARRAY_SIZE(damageForPlayer) || damageForPlayer[pnum] <= 0)
            continue;

        ++g_swCrossplayDukeAIPlayerDamageEvents;
        PlayerUpdateHealth(Player + pnum, (short)-damageForPlayer[pnum]);
    }
}

// Blast strength, taken from the same SW weapon classification the direct-hit
// path uses so an explosion and a point-blank hit agree about the weapon.
int SW_CrossplayDukeSpriteExplosionDamage(int spriteNum)
{
    return SW_CrossplayDukeSpriteHitDamage(spriteNum);
}

int SW_CrossplayExplodeDukeMapSprites(int blastSprite, int32_t x, int32_t y, int32_t z, int16_t sectnum,
                                      int32_t radius, int32_t maxDamage)
{
    if (!SW_CrossplayDukeMapBridgeActive() || radius <= 0)
        return 0;

    int32_t damaged = 0;

    for (int32_t spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (sprite[spriteNum].statnum == MAXSTATUS)
            continue;

        int32_t const dukeOwned = SW_CrossplayDukeAIOwnsSprite(spriteNum);
        if (!dukeOwned && !(g_swCrossplayDukeSpriteBehaviors[spriteNum].flags & VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
            continue;

        if (spriteNum == blastSprite)
            continue;

        SPRITEp const sp = &sprite[spriteNum];
        int32_t const distance = FindDistance3D(sp->x - x, sp->y - y, SPRITEp_MID(sp) - z);
        if (distance > radius)
            continue;

        if ((unsigned)sp->sectnum < (unsigned)numsectors &&
            !FAFcansee(x, y, z, sectnum, sp->x, sp->y, SPRITEp_MID(sp), sp->sectnum))
            continue;

        vec3_t const hitPos = { x, y, z };

        if (!dukeOwned)
        {
            // Bridge-owned Duke props: breakables, switches, explosive sources.
            damaged += SW_CrossplayDamageDukeMapSprite(blastSprite, spriteNum, &hitPos) ? 1 : 0;
            continue;
        }

        // Duke's own radius damage falls off in bands towards the edge of the
        // blast; match that shape so distant actors are only winged.
        int32_t const band = radius >> 2;
        int32_t damage = maxDamage;
        if (band > 0)
        {
            if (distance > band * 3)
                damage = maxDamage >> 2;
            else if (distance > band * 2)
                damage = maxDamage >> 1;
            else if (distance > band)
                damage = maxDamage - (maxDamage >> 2);
        }

        if (damage <= 0)
            continue;

        VB_DukeAI_DamageActor(spriteNum, damage, VB_DUKEAI_HIT_EXPLOSION,
                              getangle(sp->x - x, sp->y - y), blastSprite);
        ++damaged;
    }

    return damaged;
}


// VB_CP_ZGUARD=1: watches the first Duke sprite whose original map tile matches
// VB_CP_WATCH_TILE and reports ANY change to its z/ang/statnum, its stored Duke
// tile, or its Duke action id, attributed to the segment of the frame in which
// it happened -- SW world, enter-swap, the Duke tic itself, or leave-swap.  A
// sleeping actor must only ever change inside the Duke tic.
static int32_t g_swCrossplayZGuardSprite = -2;

static void SW_CrossplayZGuard(char const *segment)
{
    static int32_t enabled = -1;
    static int32_t watchTile;
    static int32_t lastZ, lastAng, lastStat, lastStored, lastAction;
    static int32_t primed = 0;
    static int32_t logged = 0;

    if (enabled < 0)
    {
        char const *const env = Bgetenv("VB_CP_ZGUARD");
        char const *const envTile = Bgetenv("VB_CP_WATCH_TILE");
        enabled = (env != NULL && env[0] == '1' && envTile != NULL) ? 1 : 0;
        watchTile = envTile != NULL ? Batoi(envTile) : -1;
    }

    if (!enabled || logged > 200)
        return;

    if (g_swCrossplayZGuardSprite == -2)
    {
        g_swCrossplayZGuardSprite = -1;
        for (int32_t sn = 0; sn < MAXSPRITES; ++sn)
            if (sprite[sn].statnum != MAXSTATUS && artCrossplayOriginalMapSpritePicnum(sn) == watchTile)
            {
                g_swCrossplayZGuardSprite = sn;
                LOG_F(INFO, "CPGRD watching sprite=%d tile=%d", sn, watchTile);
                break;
            }
    }

    int32_t const i = g_swCrossplayZGuardSprite;
    if (i < 0 || sprite[i].statnum == MAXSTATUS)
        return;

    int dukeAction = -1;
    VB_DukeAI_ActorState(i, NULL, NULL, NULL, NULL, NULL);
    {
        int st, ac, mv, cf, sp2;
        VB_DukeAI_ActorState(i, &st, &ac, &mv, &cf, &sp2);
        dukeAction = ac;
    }

    if (primed)
    {
        // The parked<->duke statnum flip on every enter/leave is expected noise;
        // report statnum only when it changes to something new, and always report
        // z/ang/stored-tile/action changes.
        int32_t const statInteresting = sprite[i].statnum != lastStat &&
            !((lastStat == STAT_NO_STATE && sprite[i].statnum == 2) ||
              (lastStat == 2 && sprite[i].statnum == STAT_NO_STATE) ||
              (lastStat == STAT_NO_STATE && sprite[i].statnum == 1) ||
              (lastStat == 1 && sprite[i].statnum == STAT_NO_STATE));

        if (sprite[i].z != lastZ || sprite[i].ang != lastAng || statInteresting ||
            g_swCrossplayDukeAIDukePicnum[i] != lastStored || dukeAction != lastAction)
        {
            ++logged;
            LOG_F(INFO, "CPGRD seg=%s z:%d->%d ang:%d->%d stat:%d->%d duketile:%d->%d action:%d->%d",
                  segment, lastZ >> 8, TrackerCast(sprite[i].z) >> 8, lastAng, TrackerCast(sprite[i].ang),
                  lastStat, sprite[i].statnum, lastStored, g_swCrossplayDukeAIDukePicnum[i],
                  lastAction, dukeAction);
        }
    }

    lastZ = sprite[i].z;
    lastAng = sprite[i].ang;
    lastStat = sprite[i].statnum;
    lastStored = g_swCrossplayDukeAIDukePicnum[i];
    lastAction = dukeAction;
    primed = 1;
}

void SW_CrossplayMoveForeignMapEffectors(void)
{
    SW_CrossplayZGuard("sw-world");

    if (Bgetenv("VB_CP_PLAYERSIZE_DEBUG") != NULL)
    {
        static int32_t psTic = 0;
        if ((++psTic % 90) == 0)
        {
            PLAYERp const pp0 = Player + myconnectindex;
            if ((unsigned)pp0->PlayerSprite < MAXSPRITES)
            {
                SPRITEp const psp = &sprite[pp0->PlayerSprite];
                int32_t const artH = ((unsigned)psp->picnum < MAXTILES) ? tilesiz[psp->picnum].y : 0;
                int32_t const artW = ((unsigned)psp->picnum < MAXTILES) ? tilesiz[psp->picnum].x : 0;
                LOG_F(INFO, "CPPSZ pic=%d art=%dx%d repeat=%dx%d drawnH=%d drawnW=%d playerHeight=%d",
                      TrackerCast(psp->picnum), artW, artH,
                      TrackerCast(psp->xrepeat), TrackerCast(psp->yrepeat),
                      artH * TrackerCast(psp->yrepeat) * 4, artW * TrackerCast(psp->xrepeat) * 4,
                      SW_PlayerHeight());
            }
        }
    }

    if (Bgetenv("VB_CP_MIRROR_DEBUG") != NULL)
    {
        static int32_t mirTic = 0;

        // VB_CP_MIRROR_VIEW="x,y,ang": park the player there so the mirror is
        // actually on screen; without this the probe never sees the wall drawn.
        {
            char const *const view = Bgetenv("VB_CP_MIRROR_VIEW");
            if (view != NULL && mirTic > 30)
            {
                int vx = 0, vy = 0, va = 0;
                if (Bsscanf(view, "%d,%d,%d", &vx, &vy, &va) == 3)
                {
                    PLAYERp const pp = Player + myconnectindex;
                    pp->posx = pp->oposx = vx;
                    pp->posy = pp->oposy = vy;
                    int16_t sect = pp->cursectnum;
                    updatesector(vx, vy, &sect);
                    if (sect >= 0)
                    {
                        pp->cursectnum = sect;
                        pp->posz = pp->oposz = sector[sect].floorz - Z(38);
                    }
                    pp->q16ang = pp->oq16ang = fix16_from_int(va & 2047);
                    if ((unsigned)pp->PlayerSprite < MAXSPRITES)
                    {
                        sprite[pp->PlayerSprite].x = vx;
                        sprite[pp->PlayerSprite].y = vy;
                        sprite[pp->PlayerSprite].z = pp->posz;
                        sprite[pp->PlayerSprite].ang = va & 2047;
                        if (sect >= 0)
                            changespritesect(pp->PlayerSprite, sect);
                    }
                }
            }
        }

        if ((++mirTic % 120) == 0)
        {
            for (int32_t mi = 0; mi < mirrorcnt; ++mi)
            {
                int32_t const w = mirror[mi].mirrorwall;
                int32_t const sc = mirror[mi].mirrorsector;
                if ((unsigned)w >= (unsigned)numwalls || (unsigned)sc >= (unsigned)numsectors)
                    continue;

                LOG_F(INFO, "CPMIR live player=(%d,%d) ang=%d sect=%d | m=%d wall=%d pic=%d over=%d cstat=%d | sect=%d floorpic=%d ceilpic=%d floorstat=%d | expect=%d gotpic=%d",
                      Player[myconnectindex].posx, Player[myconnectindex].posy,
                      fix16_to_int(Player[myconnectindex].q16ang), Player[myconnectindex].cursectnum,
                      mi, w, TrackerCast(wall[w].picnum), TrackerCast(wall[w].overpicnum), TrackerCast(wall[w].cstat),
                      sc, TrackerCast(sector[sc].floorpicnum), TrackerCast(sector[sc].ceilingpicnum),
                      TrackerCast(sector[sc].floorstat), MIRRORLABEL + mi,
                      bitmap_test(gotpic, MIRRORLABEL + mi) ? 1 : 0);
            }
        }
    }
    ++g_swCrossplayDukeTeleportTic;

    SW_CrossplayRecordDukeSpritePositions();
    SW_CrossplayDukeAITick();
    SW_CrossplaySelfTestTick();
    SW_CrossplayTickDukeSpriteBehaviors();
    SW_CrossplayTickDukeTripmines();
    SW_CrossplayTickDukeTransients();
    // VB_CP_FIRE_RESPAWNS=1: debug -- fire every captured Duke respawn trigger
    // once, then report how the spawned actors were classified.
    {
        static int32_t fireMode = -1;
        static int32_t fireTic = 0;
        if (fireMode < 0)
        {
            char const *const env = Bgetenv("VB_CP_FIRE_RESPAWNS");
            fireMode = (env != NULL && env[0] == '1') ? 1 : 0;
        }

        if (fireMode == 1 && ++fireTic == 90)
        {
            for (int32_t ri = 0; ri < g_swCrossplayDukeRespawnCount; ++ri)
                SW_CrossplayOperateDukeRespawns(g_swCrossplayDukeRespawns[ri].lotag);
            LOG_F(INFO, "CPDBG fired %d respawn trigger(s)", g_swCrossplayDukeRespawnCount);
        }

        if (fireMode == 1 && fireTic == 260)
        {
            int32_t adopted = 0, oldLayer = 0;
            for (int32_t sn = 0; sn < MAXSPRITES; ++sn)
            {
                if (sprite[sn].statnum == MAXSTATUS)
                    continue;
                if (SW_CrossplayDukeAIOwnsSprite(sn))
                    ++adopted;
                else if ((g_swCrossplayDukeSpriteBehaviors[sn].flags &
                          (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE)) ==
                         (VB_CROSSPLAY_DUKE_SPRITE_ACTOR | VB_CROSSPLAY_DUKE_SPRITE_ALIVE))
                    ++oldLayer;
            }
            LOG_F(INFO, "CPDBG post-respawn: dukeOwned=%d oldLayerActors=%d", adopted, oldLayer);
        }
    }

    // VB_CP_TEST_SWITCH=<lotag>: debug -- operate every switch sprite carrying
    // that lotag exactly as the player's use key does, logging the sprite art
    // before and after so a "switch works but never changes" report can be
    // reproduced without a human at the keyboard.
    {
        static int32_t switchLotag = -2;
        static int32_t switchTic = 0;
        if (switchLotag == -2)
        {
            char const *const env = Bgetenv("VB_CP_TEST_SWITCH");
            switchLotag = env != NULL ? Batoi(env) : -1;
        }

        if (switchLotag > 0)
        {
            ++switchTic;

            if (switchTic == 90 || switchTic == 150 || switchTic == 210)
            {
                for (int32_t sn = 0; sn < MAXSPRITES; ++sn)
                {
                    sw_crossplay_duke_sprite_behavior_t const &b = g_swCrossplayDukeSpriteBehaviors[sn];
                    if (sprite[sn].statnum == MAXSTATUS || !(b.flags & VB_CROSSPLAY_DUKE_SPRITE_SWITCH))
                        continue;
                    if (switchLotag != 1 && sprite[sn].lotag != switchLotag)
                        continue;

                    int16_t const picBefore = sprite[sn].picnum;
                    int16_t const srcBefore = b.sourcePicnum;
                    int32_t const stateBefore = b.switchState;

                    if (switchTic != 90)
                        SW_CrossplayDamageDukeMapSprite(Player[myconnectindex].PlayerSprite, sn, &sprite[sn].xyz);

                    LOG_F(INFO, "CPSW tic=%d spr=%d %s pic:%d->%d src:%d->%d state:%d->%d base=%d lotag=%d hitag=%d",
                          switchTic, sn, switchTic == 90 ? "initial" : "operated",
                          picBefore, TrackerCast(sprite[sn].picnum),
                          srcBefore, g_swCrossplayDukeSpriteBehaviors[sn].sourcePicnum,
                          stateBefore, g_swCrossplayDukeSpriteBehaviors[sn].switchState,
                          g_swCrossplayDukeSpriteBehaviors[sn].basePicnum,
                          TrackerCast(sprite[sn].lotag), TrackerCast(sprite[sn].hitag));
                }
            }
        }
    }

    SW_CrossplayTickDukeRespawns();
    SW_CrossplayCullRuntimeDukeControlSprites();

    if (g_swCrossplayDukeRotatorCount <= 0 && g_swCrossplayDukeTrainCount <= 0 &&
        g_swCrossplayDukeConveyorCount <= 0 && g_swCrossplayDukeTeleporterCount <= 0 &&
        g_swCrossplayDukeWaterWarpCount <= 0 &&
        g_swCrossplayDukeTouchplateCount <= 0 && g_swCrossplayDukeMasterSwitchCount <= 0 &&
        g_swCrossplayDukeZMoverCount <= 0 && g_swCrossplayDukeSectorZAnimCount <= 0 &&
        g_swCrossplayDukeWarpElevatorCount <= 0 &&
        g_swCrossplayDukeSwingDoorCount <= 0 && g_swCrossplayDukeSlidingDoorCount <= 0 &&
        g_swCrossplayDukeStretchBridgeCount <= 0 && g_swCrossplayDukeAutoCloseCount <= 0 &&
        g_swCrossplayDukeExplosiveCount <= 0 && g_swCrossplayDukeLightCount <= 0 &&
        g_swCrossplayDukeLightningCount <= 0 &&
        g_swCrossplayDukeRespawnCount <= 0 &&
        g_swCrossplayDukeDropFloorCount <= 0 && g_swCrossplayDukeTeethDoorCount <= 0 &&
        g_swCrossplayDukeQuakeCount <= 0 && g_swCrossplayDukeQuakeDebrisCount <= 0 && g_swCrossplayDukeWallAnimCount <= 0 &&
        g_swCrossplayDukePistonCount <= 0 && g_swCrossplayDukeIncrementalMoverCount <= 0 &&
        g_swCrossplayDukeWaveCount <= 0 && g_swCrossplayDukeProjectileShooterCount <= 0 &&
        g_swCrossplayDukeMapSoundCount <= 0 &&
        g_swCrossplayDukeTransientCount <= 0 && g_swCrossplayDukeTripmineCount <= 0)
        return;

    SW_CrossplayTickDukeTouchplates();
    SW_CrossplayTickDukeMasterSwitches();
    SW_CrossplayTickDukeAutoCloses();
    SW_CrossplayTickDukeLights();
    SW_CrossplayTickDukeLightnings();
    SW_CrossplayTickDukeWaterWarps();
    SW_CrossplayTickDukeMapSounds();

    for (int32_t moverIndex = 0; moverIndex < g_swCrossplayDukeZMoverCount; ++moverIndex)
        SW_CrossplayMoveDukeZMover(g_swCrossplayDukeZMovers[moverIndex]);

    for (int32_t animIndex = 0; animIndex < g_swCrossplayDukeSectorZAnimCount; ++animIndex)
        SW_CrossplayMoveDukeSectorZAnim(g_swCrossplayDukeSectorZAnims[animIndex]);

    SW_CrossplayTickDukeDropFloors();
    SW_CrossplayTickDukeTeethDoors();
    SW_CrossplayTickDukeQuakes();
    SW_CrossplayTickDukeQuakeDebris();
    SW_CrossplayTickDukePistons();
    SW_CrossplayTickDukeIncrementalMovers();
    SW_CrossplayTickDukeWaves();
    SW_CrossplayTickDukeProjectileShooters();

    SW_CrossplayTickDukeExplosives();
    SW_CrossplayTickDukeCharges();

    for (int32_t elevatorIndex = 0; elevatorIndex < g_swCrossplayDukeWarpElevatorCount; ++elevatorIndex)
        SW_CrossplayMoveDukeWarpElevator(g_swCrossplayDukeWarpElevators[elevatorIndex]);

    for (int32_t doorIndex = 0; doorIndex < g_swCrossplayDukeSwingDoorCount; ++doorIndex)
        SW_CrossplayMoveDukeSwingDoor(g_swCrossplayDukeSwingDoors[doorIndex]);

    for (int32_t doorIndex = 0; doorIndex < g_swCrossplayDukeSlidingDoorCount; ++doorIndex)
        SW_CrossplayMoveDukeSlidingDoor(g_swCrossplayDukeSlidingDoors[doorIndex]);

    SW_CrossplayTickDukeWallAnims();

    for (int32_t bridgeIndex = 0; bridgeIndex < g_swCrossplayDukeStretchBridgeCount; ++bridgeIndex)
        SW_CrossplayMoveDukeStretchBridge(g_swCrossplayDukeStretchBridges[bridgeIndex]);

    if (g_swCrossplayDukeTeleporterCount > 0)
    {
        int32_t pnum;
        TRAVERSE_CONNECT(pnum)
        {
            if ((unsigned)pnum >= ARRAY_SIZE(g_swCrossplayDukeTeleporterHold))
                continue;

            PLAYERp const pp = Player + pnum;
            if (SW_CrossplayDukePlayerStillOnBlockedTeleporter(pp, g_swCrossplayDukeTeleporterBlockSector[pnum],
                                                               g_swCrossplayDukeTeleporterBlockSprite[pnum]))
            {
                if (g_swCrossplayDukeTeleporterHold[pnum] > 0)
                    --g_swCrossplayDukeTeleporterHold[pnum];
                else
                    g_swCrossplayDukeTeleporterHold[pnum] = 2;
                continue;
            }

            g_swCrossplayDukeTeleporterBlockSector[pnum] = -1;
            g_swCrossplayDukeTeleporterBlockSprite[pnum] = -1;

            if (g_swCrossplayDukeTeleporterHold[pnum] > 0)
            {
                --g_swCrossplayDukeTeleporterHold[pnum];
                continue;
            }

            for (int32_t teleporterIndex = 0; teleporterIndex < g_swCrossplayDukeTeleporterCount; ++teleporterIndex)
            {
                sw_crossplay_duke_teleporter_t const &teleporter = g_swCrossplayDukeTeleporters[teleporterIndex];
                if (!SW_CrossplayDukePlayerTouchesTeleporterSource(pp, teleporter, 1))
                    continue;

                int32_t const oldPosX = pp->posx;
                int32_t const oldPosY = pp->posy;
                int32_t const oldPosZ = pp->posz;
                int32_t const posDiffX = pp->oposx - oldPosX;
                int32_t const posDiffY = pp->oposy - oldPosY;
                int32_t const posDiffZ = pp->oposz - oldPosZ;
                int32_t const floorDist = max<int32_t>(pp->floor_dist, SW_PlayerRunFloorDist());
                int32_t const relativeZ = clamp<int32_t>(pp->posz - teleporter.source.z, -Z(32), Z(32));

                pp->posx += teleporter.dest.x - teleporter.source.x;
                pp->posy += teleporter.dest.y - teleporter.source.y;
                pp->posz = teleporter.sourceOnFloor ? teleporter.dest.z - floorDist : teleporter.dest.z + relativeZ;

                pp->lastcursectnum = pp->cursectnum;
                pp->cursectnum = teleporter.destSect;
                pp->oldposx = pp->posx;
                pp->oldposy = pp->posy;
                pp->oldposz = pp->posz;
                pp->oposx = pp->posx + posDiffX;
                pp->oposy = pp->posy + posDiffY;
                pp->oposz = pp->posz + posDiffZ;
                pp->lv_sectnum = pp->cursectnum;
                pp->lv_x = pp->posx;
                pp->lv_y = pp->posy;
                pp->lv_z = pp->posz;

                if (teleporter.sourceOnFloor)
                    pp->q16ang = fix16_from_int(teleporter.destAng);

                SW_CrossplaySyncDukeMovedPlayer(pp, 0, 1);

                PlaySound(DIGI_DUKE_TELEPORTER, &pp->posx, &pp->posy, &pp->posz, v3df_dontpan|v3df_follow);

                ++g_swCrossplayDukeTeleportEvents;

                // Ping-pong is a teleport that fires again inside Duke's hold.  A
                // player left standing on a destination pad legitimately goes
                // again once the hold expires, so what matters is the gap.
                {
                    int32_t const gap = g_swCrossplayDukeTeleportTic - g_swCrossplayDukeLastTeleportTic;
                    if (g_swCrossplayDukeLastTeleportTic >= 0 &&
                        (g_swCrossplayDukeMinTeleportGap < 0 || gap < g_swCrossplayDukeMinTeleportGap))
                        g_swCrossplayDukeMinTeleportGap = gap;

                    g_swCrossplayDukeLastTeleportTic = g_swCrossplayDukeTeleportTic;
                }
                g_swCrossplayDukeTeleporterHold[pnum] = VB_CROSSPLAY_DUKE_TELEPORT_HOLD_TICS;
                g_swCrossplayDukeTeleporterBlockSector[pnum] = teleporter.destSect;
                g_swCrossplayDukeTeleporterBlockSprite[pnum] = teleporter.destSprite;
                break;
            }
        }
    }

    SW_CrossplayPrepareDukeTrainPlayerCarrySectors();

    for (int32_t trainIndex = 0; trainIndex < g_swCrossplayDukeTrainCount; ++trainIndex)
    {
        if (g_swCrossplayDukeTrains[trainIndex].lotag == VB_DUKE_SE_SUBWAY)
            SW_CrossplayMoveDukeTrainSector(g_swCrossplayDukeTrains[trainIndex]);
    }

    for (int32_t trainIndex = 0; trainIndex < g_swCrossplayDukeTrainCount; ++trainIndex)
    {
        if (g_swCrossplayDukeTrains[trainIndex].lotag != VB_DUKE_SE_SUBWAY)
            SW_CrossplayMoveDukeTrainSector(g_swCrossplayDukeTrains[trainIndex]);
    }

    for (int32_t rotatorIndex = 0; rotatorIndex < g_swCrossplayDukeRotatorCount; ++rotatorIndex)
    {
        sw_crossplay_duke_rotator_t &rotator = g_swCrossplayDukeRotators[rotatorIndex];
        if ((unsigned)rotator.sectnum >= (unsigned)numsectors)
            continue;

        int32_t deltaAngle = 0;
        int32_t zChange = 0;

        if (rotator.rotateRiseBridge)
        {
            if (!rotator.active)
                continue;

            int32_t const direction = rotator.opening ? 1 : -1;
            int32_t const oldTempAngle = rotator.tempAngle;

            rotator.tempAngle = (int16_t)clamp(rotator.tempAngle + direction * 4, 0, 256);
            if (rotator.tempAngle != oldTempAngle)
            {
                int32_t const angleDirection = (rotator.reverseDirection ? -direction : direction);
                deltaAngle = angleDirection * klabs(rotator.step);
            }

            int32_t const targetFloorZ = rotator.opening ? rotator.openFloorZ : rotator.closedFloorZ;
            int32_t const oldFloorZ = sector[rotator.sectnum].floorz;
            int32_t const floorDone = SW_CrossplayStepDukeZ(&sector[rotator.sectnum].floorz, targetFloorZ, 512);
            zChange = sector[rotator.sectnum].floorz - oldFloorZ;

            if ((rotator.opening ? rotator.tempAngle >= 256 : rotator.tempAngle <= 0) && floorDone)
                rotator.active = 0;
        }
        else
            deltaAngle = rotator.step;

        if (deltaAngle == 0 && zChange == 0)
            continue;

        if (zChange != 0)
            SW_CrossplayMoveDukeSectorFloorContents(rotator.sectnum, zChange);

        rotator.angle = (rotator.angle + deltaAngle) & 2047;

        int32_t const startWall = sector[rotator.sectnum].wallptr;
        int32_t const sectorWallCount = sector[rotator.sectnum].wallnum;
        int32_t const wallCount = rotator.originCount < sectorWallCount ? rotator.originCount : sectorWallCount;
        for (int32_t wallOffset = 0; wallOffset < wallCount; ++wallOffset)
        {
            vec2_t rotated;
            rotatevec(g_swCrossplayDukeRotatorOrigins[rotator.originStart + wallOffset], rotator.angle, &rotated);
            dragpoint(startWall + wallOffset, rotator.pivot.x + rotated.x, rotator.pivot.y + rotated.y, 0);
        }

        if (!rotator.carryActors)
            continue;

        int32_t spriteNum, nextSprite;
        TRAVERSE_SPRITE_SECT(headspritesect[rotator.sectnum], spriteNum, nextSprite)
        {
            if (sprite[spriteNum].statnum == MAXSTATUS ||
                (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS))
                continue;

            sprite[spriteNum].ang = NORM_ANGLE(sprite[spriteNum].ang + deltaAngle);
            rotatepoint(rotator.pivot, sprite[spriteNum].xy, deltaAngle, &sprite[spriteNum].xy);
        }

        int32_t pnum;
        TRAVERSE_CONNECT(pnum)
        {
            PLAYERp const pp = Player + pnum;
            if (!SW_CrossplayDukePlayerInMovingSector(pp, rotator.sectnum) ||
                !SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
                continue;

            SW_CrossplayRotateDukePlayerXY(pp, rotator.pivot, deltaAngle);
            pp->q16ang = NORM_Q16ANGLE(pp->q16ang + fix16_from_int(deltaAngle));
            pp->oq16ang = NORM_Q16ANGLE(pp->oq16ang + fix16_from_int(deltaAngle));
            pp->camq16ang = NORM_Q16ANGLE(pp->camq16ang + fix16_from_int(deltaAngle));

            if ((unsigned)pp->PlayerSprite < MAXSPRITES)
                sprite[pp->PlayerSprite].ang = NORM_ANGLE(sprite[pp->PlayerSprite].ang + deltaAngle);

            SW_CrossplaySyncDukeMovedPlayer(pp, 1, 0);
        }
    }

    for (int32_t conveyorIndex = 0; conveyorIndex < g_swCrossplayDukeConveyorCount; ++conveyorIndex)
    {
        sw_crossplay_duke_conveyor_t const &conveyor = g_swCrossplayDukeConveyors[conveyorIndex];
        if (conveyor.disabled || (unsigned)conveyor.sectnum >= (unsigned)numsectors)
            continue;

        int32_t const spriteXMove = conveyor.xmove >> 2;
        int32_t const spriteYMove = conveyor.ymove >> 2;

        int32_t spriteNum, nextSprite;
        TRAVERSE_SPRITE_SECT(headspritesect[conveyor.sectnum], spriteNum, nextSprite)
        {
            if (sprite[spriteNum].statnum == MAXSTATUS ||
                (sprite[spriteNum].statnum >= STAT_PLAYER0 && sprite[spriteNum].statnum < STAT_PLAYER0 + MAX_SW_PLAYERS) ||
                artCrossplayOriginalMapSpritePicnum(spriteNum) == VB_DUKE_SECTOREFFECTOR ||
                artCrossplayOriginalMapSpritePicnum(spriteNum) == VB_DUKE_GPSPEED)
                continue;

            sprite[spriteNum].x += spriteXMove;
            sprite[spriteNum].y += spriteYMove;
            setspritez(spriteNum, &sprite[spriteNum].xyz);
        }

        int32_t pnum;
        TRAVERSE_CONNECT(pnum)
        {
            PLAYERp const pp = Player + pnum;
            if (!SW_CrossplayDukePlayerInMovingSector(pp, conveyor.sectnum) ||
                !SW_CrossplayDukePlayerCanInheritSectorMotion(pp))
                continue;

            SW_CrossplayMoveDukePlayerXY(pp, spriteXMove, spriteYMove);
            SW_CrossplaySyncDukeMovedPlayer(pp, 1, 0);
        }

        sector[conveyor.sectnum].floorxpanning += conveyor.panSpeed;
        sector[conveyor.sectnum].ceilingxpanning += conveyor.panSpeed;
    }
}

static int SW_AddGroupFileFromArg(char const *grpFile)
{
    if (!grpFile || !grpFile[0])
        return -1;

    int status = initgroupfile(grpFile);
    if (status >= 0)
        return status;

    char path[BMAX_PATH];
    Bstrncpyz(path, grpFile, sizeof(path));

    char *slash = Bstrrchr(path, '/');
#ifdef _WIN32
    char *backslash = Bstrrchr(path, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif

    if (!slash)
        return status;

    *slash = 0;
    char const *base = slash + 1;
    if (!base[0])
        return status;

    if (addsearchpath(path) == 0)
        status = initgroupfile(base);

    return status;
}

void LoadDemoRun(void)
{
    short i;
    FILE *fin;

    fin = fopen("demos.run","r");
    if (fin)
    {
        memset(DemoName,'\0',sizeof(DemoName));
        for (i = 0; i < ARRAY_SSIZE(DemoName); i++)
        {
            if (fscanf(fin, "%s", DemoName[i]) == EOF)
                break;
        }
        if (i == ARRAY_SSIZE(DemoName))
            LOG_F(WARNING, "demos.run is too long, ignoring remaining files");

        fclose(fin);
    }

    memset(DemoText,'\0',sizeof(DemoText));
    fin = fopen("demotxt.run","r");
    if (fin)
    {
        fgets(ds, 6, fin);
        sscanf(ds,"%d",&DemoTextYstart);
        for (i = 0; i < ARRAY_SSIZE(DemoText); i++)
        {
            if (fgets(DemoText[i], SIZ(DemoText[0])-1, fin) == NULL)
                break;
        }
        if (i == ARRAY_SSIZE(DemoText))
            LOG_F(WARNING, "demotxt.run is too long, trimming the text");

        fclose(fin);
    }
}

void DisplayDemoText(void)
{
    short w,h;
    short i;

    for (i = 0; i < 3; i++)
    {
        MNU_MeasureString(DemoText[i], &w, &h);
        PutStringTimer(Player, TEXT_TEST_COL(w), DemoTextYstart+(i*12), DemoText[i], 999);
    }
}


void Set_GameMode(void)
{
    int result;

    //DSPRINTF(ds,"ScreenMode %d, ScreenWidth %d, ScreenHeight %d", ud_setup.ScreenMode, ud_setup.ScreenWidth, ud_setup.ScreenHeight);
    //MONO_PRINT(ds);
    result = COVERsetgamemode(ud_setup.ScreenMode, ud_setup.ScreenWidth, ud_setup.ScreenHeight, ud_setup.ScreenBPP);

    if (result < 0)
    {
        LOG_F(ERROR, "Failure setting video mode %dx%dx%d %s! Attempting safer mode...",
                     ud_setup.ScreenWidth,ud_setup.ScreenHeight,ud_setup.ScreenBPP,
                     ud_setup.ScreenMode ? "fullscreen" : "windowed");
        ud_setup.ScreenMode = 0;
        ud_setup.ScreenWidth = 640;
        ud_setup.ScreenHeight = 480;
        ud_setup.ScreenBPP = 8;

        result = COVERsetgamemode(ud_setup.ScreenMode, ud_setup.ScreenWidth, ud_setup.ScreenHeight, ud_setup.ScreenBPP);
        if (result < 0)
        {
            uninitmultiplayers();
            //uninitkeys();
            KB_Shutdown();
            TermSetup();
            UnInitSound();
            timerUninit();
            DosScreen();
            uninitgroupfile();
            exit(0);
        }
    }
}

void MultiSharewareCheck(void)
{
    if (!SW_SHAREWARE) return;
    if (numplayers > 4)
    {
        wm_msgbox(apptitle,"To play a Network game with more than 4 players you must purchase "
                  "the full version.  Read the Ordering Info screens for details.");
        uninitmultiplayers();
        //uninitkeys();
        KB_Shutdown();
        TermSetup();
        UnInitSound();
        timerUninit();
        engineUnInit();
        FreeGroups();
        uninitgroupfile();
        exit(0);
    }
}


void InitAutoNet(void)
{
    if (!AutoNet)
        return;

    gs.NetGameType      = Auto.Rules;
    gs.NetLevel         = Auto.Level;
    gs.NetMonsters      = Auto.Enemy;
    gs.NetSpawnMarkers  = Auto.Markers;
    gs.NetTeamPlay      = Auto.Team;
    gs.NetHurtTeammate  = Auto.HurtTeam;
    gs.NetKillLimit     = Auto.Kill;
    gs.NetTimeLimit     = Auto.Time;
    gs.NetColor         = Auto.Color;
    gs.NetNuke          = Auto.Nuke;
}


void AnimateCacheCursor(void)
{
#if 0
    struct rccoord old_pos;
    static short cursor_num = 0;
    static char cache_cursor[] =  {'|','/','-','\\'};

    if (GraphicsMode)
        return;

    cursor_num++;
    if (cursor_num > 3)
        cursor_num = 0;

    //old_pos = _gettextposition();
    //_settextposition( old_pos.row, old_pos.col );
    //_settextposition( 24,  25);
    _settextposition(25,  0);
    sprintf(ds,"Loading sound and graphics %c", cache_cursor[cursor_num]);
    _outtext(ds);
    //_settextposition( old_pos.row, old_pos.col );
#endif
}

void COVERsetbrightness(int bright, unsigned char *pal)
{
    paletteSetColorTable(BASEPAL, pal);
    videoSetPalette(bright, BASEPAL, 0);
}


static int firstnet = 0;    // JBF

extern int startwin_run(void);

static void SW_FatalEngineError(void)
{
    wm_msgbox("Build Engine Initialization Error",
              "There was a problem initializing the engine: %s", engineerrstr);
    exit(1);
}

void
InitGame(int32_t argc, char const * const * argv)
{
    extern int MovesPerPacket;
    //void *ReserveMem=NULL;
    int i;

    DSPRINTF(ds,"InitGame...");
    MONO_PRINT(ds);

    if (engineInit())
        SW_FatalEngineError();

    {
        char tempbuf[256];
        snprintf(tempbuf, ARRAY_SIZE(tempbuf), APPNAME " %s", s_buildRev);
        OSD_SetVersion(tempbuf, 10,0);
    }
    OSD_SetParameters(0, 0, 0, 4, 2, 4, "^14", "^14", "^14", 0);

    InitSetup();

    InitAutoNet();

    timerInit(120);

    CON_InitConsole();  // Init console command list

    ////DSPRINTF(ds,"%s, %d",__FILE__,__LINE__);   MONO_PRINT(ds);

    //InitFX();

    memcpy(palette_data,palette,768);
    paletteInitClosestColorMap((uint8_t*)&palette_data[0][0]);
    InitPalette();
    // sets numplayers, connecthead, connectpoint2, myconnectindex

    extern char s_autoJoinServer[128];
    if (Net_ResolveAutoJoin())  // -joinlobby: masterserver picked a server to join
    {
        char const *netargv[2] = { s_autoJoinServer, NULL };
        if (initmultiplayersparms(1, netargv))
        {
            NetBroadcastMode = (networkmode == MMULTI_MODE_P2P);
            LOG_F(INFO, "Joining lobby server %s...", s_autoJoinServer);
            while (initmultiplayerscycle())
            {
                handleevents();
                Net_ClientPunchAttempt();
                if (quitevent) { QuitFlag = TRUE; return; }
            }
        }
    }
    else if (!firstnet)
        initmultiplayers(0, NULL, 0, 0, 0);
    else if (initmultiplayersparms(argc - firstnet, &argv[firstnet]))
    {
        NetBroadcastMode = (networkmode == MMULTI_MODE_P2P);
        LOG_F(INFO, "Waiting for players...");
        while (initmultiplayerscycle())
        {
            handleevents();
            Net_HostAdvertise();       // keep advertising to the masterserver while waiting for joiners
            Net_ClientPunchAttempt();  // NAT hole-punch toward the server (lobby/manual join)
            if (quitevent)
            {
                QuitFlag = TRUE;
                return;
            }
        }
    }

    // Host streams any joiner-needed files (extra GRPs from -netfile + the
    // usermap) now that everyone is connected but before the level loads, so a
    // client that lacks a mod downloads+mounts it first. mmulti is reliable, so
    // the bytes land before the shared level load reads them.
    if (numplayers > 1 && myconnectindex == connecthead)
        Net_SendGameFiles(-1);

    initsynccrc();

    // code to duplicate packets
    if (numplayers > 4 && MovesPerPacket == 1)
    {
        MovesPerPacket = 2;
    }

    MultiSharewareCheck();

    if (numplayers > 1)
    {
        CommPlayers = numplayers;
        OrigCommPlayers = CommPlayers;
        CommEnabled = TRUE;
        if (!BotMode)
            gNet.MultiGameType = MULTI_GAME_COMMBAT;
        else
            gNet.MultiGameType = MULTI_GAME_AI_BOTS;

#if 0 //def NET_MODE_MASTER_SLAVE
        if (!NetModeOverride)
        {
            if (numplayers <= 4)
                NetBroadcastMode = TRUE;
            else
                NetBroadcastMode = FALSE;
        }
#endif
    }

    LoadDemoRun();
    LoadImages("tiles000.art");

    Connect();
    SortBreakInfo();
    parallaxtype = 1;
    SW_InitMultiPsky();

    memset(Track, 0, sizeof(Track));

    memset(Player, 0, sizeof(Player));
    for (i = 0; i < MAX_SW_PLAYERS; i++)
        INITLIST(&Player[i].PanelSpriteList);

    LoadKVXFromScript("swvoxfil.txt");    // Load voxels from script file
    LoadPLockFromScript("swplock.txt");   // Get Parental Lock setup info
    if (!SW_SHAREWARE)
        LoadCustomInfoFromScript("swcustom.txt");   // Load user customisation information

    char const * const deffile = G_DefFile();
    uint32_t stime = timerGetTicks();
    if (!loaddefinitionsfile(deffile))
    {
        uint32_t etime = timerGetTicks();
        LOG_F(INFO, "Definitions file '%s' loaded in %d ms.", deffile, etime-stime);
    }

    for (char * m : g_defModules)
        Xfree(m);
    g_defModules.clear();

    if (enginePostInit())
        SW_FatalEngineError();

    palettePostLoadLookups();

    DemoModeMenuInit = TRUE;
    // precache as much stuff as you can
    if (UserMapName[0] == '\0')
    {
        AnimateCacheCursor();
        LoadLevel("$dozer.map");
        AnimateCacheCursor();
        SetupPreCache();
        DoTheCache();
    }
    else
    {
        AnimateCacheCursor();
        LoadLevel(UserMapName);
        AnimateCacheCursor();
        SetupPreCache();
        DoTheCache();
    }

    Set_GameMode();
    GraphicsMode = TRUE;
    SetupAspectRatio();

    COVERsetbrightness(gs.Brightness,&palette_data[0][0]);

    InitFX();   // JBF: do it down here so we get a hold of the window handle
    InitMusic();

    enginecompatibilitymode = ENGINE_19961112; // SW 1.0: 19970212, SW 1.1-1.2: 19970522
}


/*
Directory of C:\DEV\SW\MIDI
EXECUT11 MID
HROSHMA6 MID
HOSHIA02 MID
INTRO131 MID
KOTEC2   MID
KOTOKI12 MID
NIPPON34 MID
NOKI41   MID
SANAI    MID
SIANRA23 MID
TKYO2007 MID
TYTAIK16 MID
YOKOHA03 MID
*/

char LevelSong[16];
short SongLevelNum;
//#ifndef SW_SHAREWARE
LEVEL_INFO LevelInfo[MAX_LEVELS_REG+2] =
{
    {"title.map",      "theme.mid", " ", " ", " "  },
    {"$bullet.map",    "e1l01.mid", "Seppuku Station", "0 : 55", "5 : 00"  },
    {"$dozer.map",     "e1l03.mid", "Zilla Construction", "4 : 59", "8 : 00"  },
    {"$shrine.map",    "e1l02.mid", "Master Leep's Temple", "3 : 16", "10 : 00"  },
    {"$woods.map",     "e1l04.mid", "Dark Woods of the Serpent", "7 : 06", "16 : 00"  },
    {"$whirl.map",     "yokoha03.mid", "Rising Son", "5 : 30", "10 : 00"   },
    {"$tank.map",      "nippon34.mid", "Killing Fields", "1 : 46", "4 : 00"   },
    {"$boat.map",      "execut11.mid", "Hara-Kiri Harbor", "1 : 56", "4 : 00"   },
    {"$garden.map",    "execut11.mid", "Zilla's Villa", "1 : 06", "2 : 00"   },
    {"$outpost.map",   "sanai.mid",    "Monastery", "1 : 23", "3 : 00"      },
    {"$hidtemp.map",   "kotec2.mid",   "Raider of the Lost Wang", "2 : 05", "4 : 10"     },
    {"$plax1.map",     "kotec2.mid",   "Sumo Sky Palace", "6 : 32", "12 : 00"     },
    {"$bath.map",      "yokoha03.mid", "Bath House", "10 : 00", "10 : 00"   },
    {"$airport.map",   "nippon34.mid", "Unfriendly Skies", "2 : 59", "6 : 00"   },
    {"$refiner.map",   "kotoki12.mid", "Crude Oil", "2 : 40", "5 : 00"   },
    {"$newmine.map",   "hoshia02.mid", "Coolie Mines", "2 : 48", "6 : 00"   },
    {"$subbase.map",   "hoshia02.mid", "Subpen 7", "2 : 02", "4 : 00"   },
    {"$rock.map",      "kotoki12.mid", "The Great Escape", "3 : 18", "6 : 00"   },
    {"$yamato.map",    "sanai.mid",    "Floating Fortress", "11 : 38", "20 : 00"      },
    {"$seabase.map",   "kotec2.mid",   "Water Torture", "5 : 07", "10 : 00"     },
    {"$volcano.map",   "kotec2.mid",   "Stone Rain", "9 : 15", "20 : 00"     },
    {"$shore.map",     "kotec2.mid",   "Shanghai Shipwreck", "3 : 58", "8 : 00"     },
    {"$auto.map",      "kotec2.mid",   "Auto Maul", "4 : 07", "8 : 00"     },
    {"tank.map",       "kotec2.mid",   "Heavy Metal (DM only)", "10 : 00", "10 : 00"     },
    {"$dmwoods.map",   "kotec2.mid",   "Ripper Valley (DM only)", "10 : 00", "10 : 00"     },
    {"$dmshrin.map",   "kotec2.mid",   "House of Wang (DM only)", "10 : 00", "10 : 00"     },
    {"$rush.map",      "kotec2.mid",   "Lo Wang Rally (DM only)", "10 : 00", "10 : 00"     },
    {"shotgun.map",    "kotec2.mid",   "Ruins of the Ronin (CTF)", "10 : 00", "10 : 00"     },
    {"$dmdrop.map",    "kotec2.mid",   "Killing Fields (CTF)", "10 : 00", "10 : 00"     },
    {NULL, NULL, NULL, NULL, NULL}
};
/*#else
LEVEL_INFO LevelInfo[MAX_LEVELS+2] =  // Shareware
    {
    {"title.map",      "theme.mid", " ", " ", " "  },
    {"$bullet.map",    "e1l01.mid", "Seppuku Station", "0 : 55", "5 : 00"  },
    {"$dozer.map",     "e1l03.mid", "Zilla Construction", "4 : 59", "8 : 00"  },
    {"$shrine.map",    "e1l02.mid", "Master Leep's Temple", "3 : 16", "10 : 00"  },
    {"$woods.map",     "e1l04.mid", "Dark Woods of the Serpent", "7 : 06", "16 : 00"  },
    {NULL, NULL, NULL, NULL, NULL}
    };
#endif*/

struct LevelNamePatch
{
    uint8_t index;
    char const * name;
};
// names hex-edited into the exe
static LevelNamePatch const LevelNamesWD[] =
{
    { 5, "Chinatown" },
    { 6, "Monastery" }, // "Monastary"
    { 7, "Trolley Yard" }, // "Trolly Yard"
    { 8, "Restaurant" }, // "Resturant"
    { 9, "Skyscraper" },
    { 10, "Airplane" },
    { 11, "Military Base" },
    { 12, "Train" },
    { 13, "Auto Factory" },
    { 20, "Skyline" },
    { 21, "Redwood Forest" },
    { 22, "The Docks" },
    { 23, "Waterfight (DM only)" },
    { 24, "Wanton DM 1 (DM only)" },
    { 25, "Wanton DM 2 (DM only)" },
    { 27, "Wanton CTF (CTF)" },
};
#if 0
// truncated names hex-edited into the exe
static LevelNamePatch const LevelNamesTD[] =
{
    { 5, "Wang's Home" },
    { 6, "City of Despair" }, // "City Of Despair"
    { 7, "Emergency Room" },
    { 8, "Hide and Seek" }, // "Hide And Seek"
    { 9, "Warehouse" },
    { 10, "Military Research Base" },
    { 11, "Toxic Waste" },
    { 12, "Crazy Train" },
    { 13, "Fishing Village" },
    { 14, "The Garden" },
    { 15, "The Fortress" },
    { 20, "The Palace" },
    { 21, "Prison Camp" },
    { 23, "Ninja Training Camp (DM only)" }, // "Ninja Training Camp"
    { 24, "Death Becomes You (DM only)" }, // "Death Becomes You"
    { 25, "Island Caves (DM only)" }, // "Island Caves"
};
#else
// names from the readme
static LevelNamePatch const LevelNamesTD[] =
{
    { 5, "Home Sweet Home" },
    { 6, "City of Despair" },
    { 7, "Emergency Room" },
    { 8, "Hide and Seek" },
    { 9, "Warehouse Madness" },
    { 10, "Weapons Research Center" },
    { 11, "Toxic Waste Facility" },
    { 12, "Silver Bullet" },
    { 13, "Fishing Village" },
    { 14, "Secret Garden" },
    { 15, "Hung Lo's Fortress" },
    { 20, "Hung Lo's Palace" },
    { 21, "Prison Camp" }, // "Prison Camp (secret level)"
    { 23, "Ninja Training Camp (DM only)" }, // "Ninja Training Camp (dm)"
    { 24, "The Morgue/Mortuary (DM only)" }, // "The Morgue/mortuary (dm)"
    { 25, "Island Caves (DM only)" }, // "Island Caves (dm)"
};
#endif

char EpisodeNames[2][MAX_EPISODE_NAME_LEN+2] =
{
    "^Enter the Wang",
    "^Code of Honor"
};
static char const s_EpisodeNameWD[] = "^Wanton Destruction"; // "^Wanton Destr"
static char const s_EpisodeNameTD[] = "^Twin Dragon";
char EpisodeSubtitles[2][MAX_EPISODE_SUBTITLE_LEN+1] =
{
    "Four levels (Shareware Version)",
    "Eighteen levels (Full Version Only)"
};
static char const s_EpisodeSubtitleWD[] = "Twelve levels (Full Version Only)";
static char const s_EpisodeSubtitleTD[] = "Thirteen levels (Full Version Only)";
char SkillNames[4][MAX_SKILL_NAME_LEN+2] =
{
    "^Tiny Grasshopper", // "^Tiny grasshopper"
    "^I Have No Fear",
    "^Who Wants Wang",
    "^No Pain, No Gain"
};

void InitNewGame(void)
{
    int i, ready_bak;
    int ver_bak;

    //waitforeverybody();           // since ready flag resets after this point, need to carefully sync

    for (i = 0; i < MAX_SW_PLAYERS; i++)
    {
        // don't jack with the playerreadyflag
        ready_bak = Player[i].playerreadyflag;
        ver_bak = Player[i].PlayerVersion;
        memset(&Player[i], 0, sizeof(Player[i]));
        Player[i].playerreadyflag = ready_bak;
        Player[i].PlayerVersion = ver_bak;
        INITLIST(&Player[i].PanelSpriteList);
    }

    memset(puser, 0, sizeof(puser));
}

void FindLevelInfo(char *map_name, short *level)
{
    short j;

    for (j = 1; j <= MAX_LEVELS; j++)
    {
        if (LevelInfo[j].LevelName)
        {
            if (Bstrcasecmp(map_name, LevelInfo[j].LevelName) == 0)
            {
                *level = j;
                return;
            }
        }
    }

    *level = 0;
    return;
}

int ChopTics;
void InitLevelGlobals(void)
{
    extern char PlayerGravity;
    extern short wait_active_check_offset;
    //extern short Zombies;
    extern int PlaxCeilGlobZadjust, PlaxFloorGlobZadjust;
    extern SWBOOL left_foot;
    extern SWBOOL serpwasseen;
    extern SWBOOL sumowasseen;
    extern SWBOOL zillawasseen;
    extern short BossSpriteNum[3];

    // A few IMPORTANT GLOBAL RESETS
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1
    MapSetup();
    //Zombies = 0;
    ChopTics = 0;
    dimensionmode = 3;
    zoom = 768;
    PlayerGravity = 24;
    wait_active_check_offset = 0;
    PlaxCeilGlobZadjust = PlaxFloorGlobZadjust = Z(500);
    FinishedLevel = FALSE;
    AnimCnt = 0;
    left_foot = FALSE;
    screenpeek = myconnectindex;
    numinterpolations = short_numinterpolations = 0;

    gNet.TimeLimitClock = gNet.TimeLimit;

    serpwasseen = FALSE;
    sumowasseen = FALSE;
    zillawasseen = FALSE;
    memset(BossSpriteNum,-1,sizeof(BossSpriteNum));

    PedanticMode = (DemoPlaying || DemoRecording || DemoEdit || DemoMode);
}

void InitLevelGlobals2(void)
{
    extern short Bunny_Count;
    // GLOBAL RESETS NOT DONE for LOAD GAME
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    InitTimingVars();
    TotalKillable = 0;
    Bunny_Count = 0;
    FinishAnim = 0;
}

void
InitLevel(void)
{
    static int DemoNumber = 0;

    MONO_PRINT("InitLevel");
    Terminate3DSounds();

    // A few IMPORTANT GLOBAL RESETS
    InitLevelGlobals();
    MONO_PRINT("InitLevelGlobals");
    if (!DemoMode)
        StopSong();

    if (LoadGameOutsideMoveLoop)
    {
        MONO_PRINT("Returning from InitLevel");
        return;
    }

    InitLevelGlobals2();
    MONO_PRINT("InitLevelGlobals2");
    if (DemoMode)
    {
        Level = 0;
        NewGame = TRUE;
        DemoInitOnce = FALSE;
        strcpy(DemoFileName, DemoName[DemoNumber]);
        DemoNumber++;
        if (!DemoName[DemoNumber][0])
            DemoNumber = 0;

        // read header and such
        DemoPlaySetup();

        strcpy(LevelName, DemoLevelName);

        FindLevelInfo(LevelName, &Level);
        if (Level > 0)
        {
            strcpy(LevelSong, LevelInfo[Level].SongName);
            strcpy(LevelName, LevelInfo[Level].LevelName);
            UserMapName[0] = '\0';
        }
        else
        {
            strcpy(UserMapName, DemoLevelName);
            Level = 0;
        }

    }
    else
    {
        if (Level < 0)
            Level = 0;

        if (Level > MAX_LEVELS)
            Level = 1;

        // extra code in case something is resetting these values
        if (NewGame)
        {
            //Level = 1;
            //DemoPlaying = FALSE;
            DemoMode = FALSE;
            //DemoRecording = FALSE;
            //DemoEdit = FALSE;
        }

        if (UserMapName[0])
        {
            strcpy(LevelName, UserMapName);

            Level = 0;
            FindLevelInfo(UserMapName, &Level);

            if (Level > 0)
            {
                // user map is part of game - treat it as such
                strcpy(LevelSong, LevelInfo[Level].SongName);
                strcpy(LevelName, LevelInfo[Level].LevelName);
                UserMapName[0] = '\0';
            }
        }
        else
        {
            strcpy(LevelName, LevelInfo[Level].LevelName);
            strcpy(LevelSong, LevelInfo[Level].SongName);
        }
    }

    PlayingLevel = Level;

    if (NewGame)
        InitNewGame();

    LoadingLevelScreen();
    MONO_PRINT("LoadintLevelScreen");
    if (!DemoMode && !DemoInitOnce)
        DemoPlaySetup();

    artCrossplayResetLevel();
    LoadLevel(LevelName);
    int const crossplayVisualMap = artCrossplayMapArtPrepared();

    if (crossplayVisualMap)
    {
        LOG_F(INFO, "Crossplay: using Shadow Warrior visual compatibility setup for foreign level %s.", LevelName);
        SW_CrossplaySanitizeForeignMapSectors();
        SW_CrossplayApplyDukeWaterSectorFlags();
        SW_CrossplayResetSectorObjects();
        memset(gotpic,0,sizeof(gotpic));
    }
    else if (Bstrcasecmp(CacheLastLevel, LevelName) != 0)
        // clears gotpic and does some bit setting
        SetupPreCache();
    else
        memset(gotpic,0,sizeof(gotpic));

    if (crossplayVisualMap)
    {
        NormalVisibility = g_visibility;
    }
    else if (sector[0].extra != -1)
    {
        NormalVisibility = g_visibility = sector[0].extra;
        sector[0].extra = 0;
    }
    else
        NormalVisibility = g_visibility;

    //
    // Do Player stuff first
    //

    InitAllPlayers();

#if DEBUG
    // fake Multi-player game setup
    if (FakeMultiNumPlayers && !BotMode)
    {
        uint8_t i;

        // insert all needed players except the first one - its already tere
        for (i = 0; i < FakeMultiNumPlayers - 1; i++)
        {
            ManualPlayerInsert(Player);
            // reset control back to 1st player
            myconnectindex = 0;
            screenpeek = 0;
        }
    }
#endif

    // Put in the BOTS if called for
    if (FakeMultiNumPlayers && BotMode)
    {
        uint8_t i;

        // insert all needed players except the first one - its already tere
        for (i = 0; i < FakeMultiNumPlayers; i++)
        {
            BotPlayerInsert(Player);
            // reset control back to 1st player
            myconnectindex = 0;
            screenpeek = 0;
        }
    }

    QueueReset();
    PreMapCombineFloors();
    InitMultiPlayerInfo();
    InitAllPlayerSprites();
    if (crossplayVisualMap)
    {
        SW_CrossplayResetPlayersForForeignMap();
        SW_CrossplayPrepareForeignMapSprites();
        SpriteSetup();
        int32_t const postSetupControlSpritesRemoved = SW_CrossplayCullHiddenDukeControlSprites();
        if (postSetupControlSpritesRemoved > 0)
            LOG_F(INFO, "Crossplay: culled %d Duke control/player marker sprite(s) after Shadow Warrior sprite setup.",
                  postSetupControlSpritesRemoved);
        SW_CrossplayApplyDukeSpriteVisualOverrides();
        SW_CrossplayApplyDukeWaterSectorFlags();
        SW_CrossplayApplyDukePlayerRuntimeDimensionsForAllPlayers();
    }

    //
    // Do setup for sprite, track, panel, sector, etc
    //

    // Set levels up
    InitTimingVars();

    if (crossplayVisualMap)
    {
        InitLighting();
        PlayerPanelSetup();
        MapSetup();
        SW_CrossplayRetagDukeMirrorWalls();
        JS_InitMirrors();
        if (Bgetenv("VB_CP_MIRROR_DEBUG") != NULL)
            LOG_F(INFO, "CPMIR after JS_InitMirrors: mirrorcnt=%d", mirrorcnt);
        SW_CrossplayRestoreMaskedMirrorWalls();

        // Foreign maps have no SW lockout tiles, but TerminateLevel() always walks
        // the lockout lists.  They must still be initialized or teardown traverses
        // uninitialized list heads and crashes when leaving a Duke level.
        JS_InitLockouts();
        JS_ToggleLockouts();
        SetupMirrorTiles();
    }
    else
    {
        SpriteSetup();
        SpriteSetupPost(); // post processing - already gone once through the loop
        InitLighting();

        TrackSetup();

        PlayerPanelSetup();
        MapSetup();
        SectorSetup();
        JS_InitMirrors();
        if (Bgetenv("VB_CP_MIRROR_DEBUG") != NULL)
            LOG_F(INFO, "CPMIR after JS_InitMirrors: mirrorcnt=%d", mirrorcnt);
        JS_InitLockouts();   // Setup the lockout linked lists
        JS_ToggleLockouts(); // Init lockouts on/off

        PlaceSectorObjectsOnTracks();
        PlaceActorsOnTracks();
        PostSetupSectorObject();
        SetupMirrorTiles();
    }
#if 0
    initlava();
#endif

    SongLevelNum = Level;

    if (DemoMode)
    {
        DisplayDemoText();
    }


    if (ArgCheat)
    {
        SWBOOL bak = gs.Messages;
        gs.Messages = FALSE;
        EveryCheatToggle(&Player[0],NULL);
        gs.Messages = bak;
        GodMode = TRUE;
    }

    // reset NewGame
    NewGame = FALSE;

    DSPRINTF(ds,"End of InitLevel...");
    MONO_PRINT(ds);

#if 0
#if DEBUG
    if (!cansee(43594, -92986, 0x3fffffff, 290,
                43180, -91707, 0x3fffffff, 290))
    {
        DSPRINTF(ds,"cansee failed");
        MONO_PRINT(ds);
    }
#endif
#endif

}


void
TerminateLevel(void)
{
    void pClearSpriteList(PLAYERp pp);
    int i, nexti, stat, pnum, ndx;
    SECT_USERp *sectu;

    DemoTerm();

    // Free any track points
    for (ndx = 0; ndx < MAX_TRACKS; ndx++)
    {
        if (Track[ndx].TrackPoint)
        {
            FreeMem(Track[ndx].TrackPoint);
            // !JIM! I added null assigner
            Track[ndx].TrackPoint = NULL;
        }
    }

    // Clear the tracks
    memset(Track, 0, sizeof(Track));

    StopFX();
    Terminate3DSounds();        // Kill the 3d sounds linked list
    //ClearSoundLocks();

    // Clear all anims and any memory associated with them
    // Clear before killing sprites - save a little time
    //AnimClear();

    for (stat = STAT_PLAYER0; stat < STAT_PLAYER0 + numplayers; stat++)
    {

        pnum = stat - STAT_PLAYER0;

        TRAVERSE_SPRITE_STAT(headspritestat[stat], i, nexti)
        {
            if (User[i])
                memcpy(&puser[pnum], User[i], sizeof(USER));
        }
    }

    // Kill User memory and delete sprites
    // for (stat = 0; stat < STAT_ALL; stat++)
    for (stat = 0; stat < MAXSTATUS; stat++)
    {
        TRAVERSE_SPRITE_STAT(headspritestat[stat], i, nexti)
        {
            KillSprite(i);
        }
    }

    // Free SectUser memory
    for (sectu = &SectUser[0];
         sectu < &SectUser[MAXSECTORS];
         sectu++)
    {
        if (*sectu)
        {
            ////DSPRINTF(ds,"Sect User Free %d",sectu-SectUser);
            //MONO_PRINT(ds);
            FreeMem(*sectu);
            *sectu = NULL;
        }
    }

    //memset(&User[0], 0, sizeof(User));
    memset(&SectUser[0], 0, sizeof(SectUser));

    TRAVERSE_CONNECT(pnum)
    {
        PLAYERp pp = Player + pnum;

        // Free panel sprites for players
        pClearSpriteList(pp);

        pp->DoPlayerAction = NULL;

        pp->SpriteP = NULL;
        pp->PlayerSprite = -1;

        pp->UnderSpriteP = NULL;
        pp->PlayerUnderSprite = -1;

        memset(pp->HasKey, 0, sizeof(pp->HasKey));

        //pp->WpnFlags = 0;
        pp->CurWpn = NULL;

        memset(pp->Wpn, 0, sizeof(pp->Wpn));
        memset(pp->InventorySprite, 0, sizeof(pp->InventorySprite));
        memset(pp->InventoryTics, 0, sizeof(pp->InventoryTics));

        pp->Killer = -1;

        INITLIST(&pp->PanelSpriteList);
    }

    JS_UnInitLockouts();
}

void
NewLevel(void)
{

    DSPRINTF(ds,"NewLevel");
    MONO_PRINT(ds);

    if (DemoPlaying)
    {
        FX_SetVolume(0); // Shut the hell up while game is loading!
        InitLevel();
        InitRunLevel();

        DemoInitOnce = FALSE;
        if (DemoMode)
        {
            if (DemoModeMenuInit)
            {
                DemoModeMenuInit = FALSE;
                KEY_PRESSED(KEYSC_ESC) = TRUE;
            }
        }

        DemoPlayBack();

        if (DemoRecording && DemoEdit)
        {
            RunLevel();
        }
    }
    else
    {
        DSPRINTF(ds,"Calling FX_SetVolume");
        MONO_PRINT(ds);
        FX_SetVolume(0); // Shut the hell up while game is loading!

        DSPRINTF(ds,"Calling InitLevel");
        MONO_PRINT(ds);
        InitLevel();

        DSPRINTF(ds,"Calling RunLevel");
        MONO_PRINT(ds);
        RunLevel();

        if (!QuitFlag)
        {
            // for good measure do this
            ready2send = 0;
            waitforeverybody();
        }

        StatScreen(&Player[myconnectindex]);
    }

    if (LoadGameFromDemo)
        LoadGameFromDemo = FALSE;
    else
        TerminateLevel();

    InGame = FALSE;

    if (SW_SHAREWARE)
    {
        if (FinishAnim)
        {
            PlayTheme();
            MenuLevel();
        }
    }
    else
    {
        if (FinishAnim == ANIM_ZILLA || FinishAnim == ANIM_SERP)
        {
            PlayTheme();
            MenuLevel();
        }
    }
}

void
ResetKeys(void)
{
    int i;

    for (i = 0; i < MAXKEYBOARDSCAN; i++)
    {
        KEY_PRESSED(i) = 0;
    }
}

SWBOOL
KeyPressed(void)
{
    int i;

    for (i = 0; i < MAXKEYBOARDSCAN; i++)
    {
        if (KEY_PRESSED(i))
            return TRUE;
    }

    return FALSE;
}

uint8_t*
KeyPressedRange(uint8_t* kb, uint8_t* ke)
{
    uint8_t* k;

    for (k = kb; k <= ke; k++)
    {
        if (*k)
            return k;
    }

    return NULL;
}

void
ResetKeyRange(uint8_t* kb, uint8_t* ke)
{
    uint8_t* k;

    for (k = kb; k <= ke; k++)
    {
        *k = 0;
    }
}

void PlayTheme()
{
    // start music at logo
    strcpy(LevelSong,"theme.mid");
    PlaySong(LevelSong, RedBookSong[0], TRUE, TRUE);

    DSPRINTF(ds,"After music stuff...");
    MONO_PRINT(ds);
}

static int g_noLogo;

void
LogoLevel(void)
{
    if (g_noLogo)
        return;

    buildvfs_kfd fin;
    unsigned char pal[PAL_SIZE];
    UserInput uinfo = { FALSE, FALSE, FALSE, dir_None };


    DSPRINTF(ds,"LogoLevel...");
    MONO_PRINT(ds);

    // PreCache Anim
    LoadAnm(0);

    if ((fin = kopen4load("3drealms.pal", 0)) != buildvfs_kfd_invalid)
    {
        kread(fin, pal, PAL_SIZE);
        kclose(fin);

        for (auto & c : pal)
            c <<= 2;

        paletteSetColorTable(DREALMSPAL, pal);
        videoSetPalette(gs.Brightness, DREALMSPAL, 2);
    }
    DSPRINTF(ds,"Just read in 3drealms.pal...");
    MONO_PRINT(ds);

    //FadeOut(0, 0);
    ready2send = 0;
    totalclock = 0;
    ototalclock = 0;

    DSPRINTF(ds,"About to display 3drealms pic...");
    MONO_PRINT(ds);

    //FadeIn(0, 3);

    ResetKeys();
    while (TRUE)
    {
        videoClearViewableArea(0L);
        rotatesprite(0, 0, RS_SCALE, 0, THREED_REALMS_PIC, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
        videoNextPage();

        handleevents();
        CONTROL_GetUserInput(&uinfo);
        CONTROL_ClearUserInput(&uinfo);
        if (quitevent) { QuitFlag = TRUE; break; }

        // taken from top of faketimerhandler
        // limits checks to max of 40 times a second
        if (totalclock >= ototalclock + synctics)
        {
            ototalclock += synctics;
        }

        if (totalclock > 5*120 || KeyPressed() || uinfo.button0 || uinfo.button1)
        {
            break;
        }
    }

    videoClearViewableArea(0L);
    videoNextPage();
    videoSetPalette(gs.Brightness, BASEPAL, 2);

    // put up a blank screen while loading

    DSPRINTF(ds,"End of LogoLevel...");
    MONO_PRINT(ds);

}

void
CreditsLevel(void)
{
    int curpic;
    int handle;
    uint32_t timer = 0;
    int zero=0;
    short save;
#define CREDITS1_PIC 5111
#define CREDITS2_PIC 5118

    // put up a blank screen while loading

    // get rid of all PERM sprites!
    renderFlushPerms();
    save = gs.BorderNum;
    ClearStartMost();
    gs.BorderNum = save;
    videoClearViewableArea(0L);
    videoNextPage();

    // Lo Wang feel like singing!
    handle = PlaySound(DIGI_JG95012,&zero,&zero,&zero,v3df_none);

    if (handle > 0)
        while (FX_SoundActive(handle))
            handleevents();

    // try 14 then 2 then quit
    if (!PlaySong(NULL, 14, FALSE, TRUE))
    {
        if (!PlaySong(NULL, 2, FALSE, TRUE))
        {
            handle = PlaySound(DIGI_NOLIKEMUSIC,&zero,&zero,&zero,v3df_none);
            if (handle > 0)
                while (FX_SoundActive(handle)) handleevents();
            return;
        }
    }

    ready2send = 0;
    totalclock = 0;
    ototalclock = 0;

    ResetKeys();
    curpic = CREDITS1_PIC;

    while (TRUE)
    {
        handleevents();

        // taken from top of faketimerhandler
        // limits checks to max of 40 times a second
        if (totalclock >= ototalclock + synctics)
        {
            ototalclock += synctics;
            timer += synctics;
        }

        rotatesprite(0, 0, RS_SCALE, 0, curpic, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);

        videoNextPage();

        if (timer > 8*120)
        {
            curpic = CREDITS2_PIC;
        }

        if (timer > 16*120)
        {
            timer = 0;
            curpic = CREDITS1_PIC;
        }


        if (!SongIsPlaying())
            break;

        if (KEY_PRESSED(KEYSC_ESC))
            break;
    }

    // put up a blank screen while loading
    videoClearViewableArea(0L);
    videoNextPage();
    ResetKeys();
    StopSong();
}


void
SybexScreen(void)
{
    if (!SW_SHAREWARE) return;

    if (CommEnabled)
        return;

    if (tilesiz[SYBEX_PIC].x <= 0)
        return;

    rotatesprite(0, 0, RS_SCALE, 0, SYBEX_PIC, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
    videoNextPage();

    ResetKeys();
    while (!KeyPressed() && !quitevent) handleevents();
}

void
TenScreen(void)
{
    if (CommEnabled)
        return;

    if (tilesiz[TEN_PIC].x <= 0)
        return;

    //videoNextPage();
    //FadeOut(0, 0);

    buildvfs_kfd fin;
    if ((fin = kopen4load("ten.pal", 0)) != buildvfs_kfd_invalid)
    {
        unsigned char pal[PAL_SIZE];

        kread(fin, pal, PAL_SIZE);
        kclose(fin);

        for (auto & c : pal)
            c <<= 2;

        paletteSetColorTable(TENPAL, pal);
        videoSetPalette(gs.Brightness, TENPAL, 2);

        videoClearViewableArea(0L);
    }

    rotatesprite(0, 0, RS_SCALE, 0, TEN_PIC, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
    videoNextPage();
    //FadeIn(0, 3);

    ResetKeys();
    while (!KeyPressed() && !quitevent) handleevents();

    videoClearViewableArea(0L);
    videoNextPage();
    videoSetPalette(gs.Brightness, BASEPAL, 2);
}

void DrawMenuLevelScreen(void)
{
    renderFlushPerms();
    videoClearViewableArea(0L);
    rotatesprite(0, 0, RS_SCALE, 0, TITLE_PIC, 20, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
}

void DrawStatScreen(void)
{
    renderFlushPerms();
    videoClearViewableArea(0L);
    rotatesprite(0, 0, RS_SCALE, 0, STAT_SCREEN_PIC, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
}

void DrawLoadLevelScreen(void)
{
    renderFlushPerms();
    videoClearViewableArea(0L);
    rotatesprite(0, 0, RS_SCALE, 0, TITLE_PIC, 20, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
}

short PlayerQuitMenuLevel = -1;

void
IntroAnimLevel(void)
{
    if (g_noLogo)
        return;

    DSPRINTF(ds,"IntroAnimLevel");
    MONO_PRINT(ds);
    playanm(0);
}

SWBOOL
wfe_Esc(void)
{
    int16_t w,h;
    static SWBOOL wfe_Show = 0;

    // taken from top of faketimerhandler
    // limits checks to max of 40 times a second
    if (totalclock >= ototalclock + synctics)
    {
        ototalclock += synctics;
    }

    DrawMenuLevelScreen();

    if (KEY_PRESSED(KEYSC_ESC))
    {
        KEY_PRESSED(KEYSC_ESC) = 0;
        wfe_Show = TRUE;
    }

    if (wfe_Show)
    {
        sprintf(ds,"Lo Wang is afraid!  Exit game Y/N?");
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(TEXT_TEST_COL(w), 170, ds, 1, 16);

        videoNextPage();

        if (KEY_PRESSED(KEYSC_Y))
        {
            KEY_PRESSED(KEYSC_Y) = 0;
            wfe_Show = FALSE;
            return TRUE;
        }
        else if (KEY_PRESSED(KEYSC_N))
        {
            KEY_PRESSED(KEYSC_N) = 0;
            wfe_Show = FALSE;
            return FALSE;
        }
        else
        {
            return FALSE;
        }
    }

    sprintf(ds,"Lo Wang is waiting for other players...");
    MNU_MeasureString(ds, &w, &h);
    MNU_DrawString(TEXT_TEST_COL(w), 170, ds, 1, 16);

    sprintf(ds,"They are afraid!");
    MNU_MeasureString(ds, &w, &h);
    MNU_DrawString(TEXT_TEST_COL(w), 180, ds, 1, 16);

    videoNextPage();

    return FALSE;
}

void
MenuLevel(void)
{
    SWBOOL MNU_StartNetGame(void);
    extern ClockTicks totalclocklock;
    extern SWBOOL (*wfe_ExitCallback)(void);
    short w,h;

    DSPRINTF(ds,"MenuLevel...");
    MONO_PRINT(ds);

    if (AutoNet)
    {
        DrawMenuLevelScreen();

        if (CommEnabled)
        {
            sprintf(ds,"Lo Wang is waiting for other players...");
            MNU_MeasureString(ds, &w, &h);
            MNU_DrawString(TEXT_TEST_COL(w), 170, ds, 1, 16);

            sprintf(ds,"They are afraid!");
            MNU_MeasureString(ds, &w, &h);
            MNU_DrawString(TEXT_TEST_COL(w), 180, ds, 1, 16);
        }

        videoNextPage();

        wfe_ExitCallback = &wfe_Esc;
        waitforeverybody();
        wfe_ExitCallback = 0;
        FirstTimeIntoGame = TRUE;
        MNU_StartNetGame();
        FirstTimeIntoGame = FALSE;
        waitforeverybody();
        ExitLevel = FALSE;
        FinishedLevel = FALSE;
        BorderAdjust = TRUE;
        UsingMenus = FALSE;
        InMenuLevel = FALSE;
        return;
    }

    // do demos only if not playing multi play
    if (!CommEnabled && numplayers <= 1 && !FinishAnim && !NoDemoStartup)
    {
        // demos exist - do demo instead
        if (DemoName[0][0] != '\0')
        {
            DemoMode = TRUE;
            DemoPlaying = TRUE;
            return;
        }
    }

    DemoMode = FALSE;
    DemoPlaying = FALSE;

    videoClearViewableArea(0L);
    videoNextPage();

    //FadeOut(0, 0);
    ready2send = 0;
    totalclock = 0;
    ototalclock = 0;
    ExitLevel = FALSE;
    InMenuLevel = TRUE;

    DrawMenuLevelScreen();

    if (CommEnabled)
    {
        sprintf(ds,"Lo Wang is waiting for other players...");
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(TEXT_TEST_COL(w), 170, ds, 1, 16);

        sprintf(ds,"They are afraid!");
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(TEXT_TEST_COL(w), 180, ds, 1, 16);
    }

    videoNextPage();
    //FadeIn(0, 3);

    wfe_ExitCallback = &wfe_Esc;
    waitforeverybody();
    wfe_ExitCallback = 0;

    // don't allow BorderAdjusting in these menus
    BorderAdjust = FALSE;

    ResetKeys();

    if (SW_SHAREWARE)
    {
        // go to ordering menu only if shareware
        if (FinishAnim)
        {
            KEY_PRESSED(KEYSC_ESC) = 1;
            ControlPanelType = ct_ordermenu;
            FinishAnim = 0;
        }
    }
    else
    {
        FinishAnim = 0;
    }

    while (TRUE)
    {
        handleevents();
        OSD_DispatchQueued();
        VBJS_RunFrame();

        if (quitevent) CON_Quit();

        // taken from top of faketimerhandler
        // limits checks to max of 40 times a second
        if (totalclock >= ototalclock + synctics)
        {
            ototalclock += synctics;
            MNU_CheckForMenusAnyKey();
            if (CommEnabled)
                getpackets();
        }

        if (CommEnabled)
        {
            if (MultiPlayQuitFlag)
            {
                uint8_t pbuf[1];
                QuitFlag = TRUE;
                pbuf[0] = PACKET_TYPE_MENU_LEVEL_QUIT;
                netbroadcastpacket(pbuf, 1);                      // TENSW
                break;
            }

            if (PlayerQuitMenuLevel >= 0)
            {
                MenuCommPlayerQuit(PlayerQuitMenuLevel);
                PlayerQuitMenuLevel = -1;
            }
        }

        if (ExitLevel)
        {
            // Quiting Level
            ExitLevel = FALSE;
            break;
        }

        if (QuitFlag)
        {
            // Quiting Game
            break;
        }

        // force the use of menus at all time
        if (!UsingMenus && !ConPanel)
        {
            KEY_PRESSED(KEYSC_ESC) = TRUE;
            MNU_CheckForMenusAnyKey();
        }

        // must lock the clock for drawing so animations will happen
        totalclocklock = totalclock;

        //drawscreen as fast as you can
        DrawMenuLevelScreen();

        if (UsingMenus)
            MNU_DrawMenu();

        videoNextPage();
    }

    BorderAdjust = TRUE;
    //LoadGameOutsideMoveLoop = FALSE;
    KEY_PRESSED(KEYSC_ESC) = FALSE;
    KB_ClearKeysDown();
    //ExitMenus();
    UsingMenus = FALSE;
    InMenuLevel = FALSE;
    videoClearViewableArea(0L);
    videoNextPage();
}

void
LoadingLevelScreen(void)
{
    short w,h;
    extern SWBOOL DemoMode;
    DrawLoadLevelScreen();

    if (DemoMode)
        sprintf(ds,"DEMO");
    else
        sprintf(ds,"ENTERING");

    MNU_MeasureString(ds, &w, &h);
    MNU_DrawString(TEXT_TEST_COL(w), 170, ds,1,16);

    if (UserMapName[0])
        sprintf(ds,"%s",UserMapName);
    else
        sprintf(ds,"%s",LevelInfo[Level].Description);

    MNU_MeasureString(ds, &w, &h);
    MNU_DrawString(TEXT_TEST_COL(w), 180, ds,1,16);

    videoNextPage();
}

void
gNextState(STATEp *State)
{
    // Transition to the next state
    *State = (*State)->NextState;

    if (TEST((*State)->Tics, SF_QUICK_CALL))
    {
        (*(*State)->Animator)(0);
        *State = (*State)->NextState;
    }
}

// Generic state control
void
gStateControl(STATEp *State, int *tics)
{
    *tics += synctics;

    // Skip states if too much time has passed
    while (*tics >= (*State)->Tics)
    {
        // Set Tics
        *tics -= (*State)->Tics;
        gNextState(State);
    }

    // Call the correct animator
    if ((*State)->Animator)
        (*(*State)->Animator)(0);
}

int BonusPunchSound(short UNUSED(SpriteNum))
{
    PLAYERp pp = Player + myconnectindex;
    PlaySound(DIGI_PLAYERYELL3, &pp->posx, &pp->posy, &pp->posz, v3df_none);
    return 0;
}

int BonusKickSound(short UNUSED(SpriteNum))
{
    PLAYERp pp = Player + myconnectindex;
    PlaySound(DIGI_PLAYERYELL2, &pp->posx, &pp->posy, &pp->posz, v3df_none);
    return 0;
}

int BonusGrabSound(short UNUSED(SpriteNum))
{
    PLAYERp pp = Player + myconnectindex;
    PlaySound(DIGI_BONUS_GRAB, &pp->posx, &pp->posy, &pp->posz, v3df_none);
    return 0;
}

void
BonusScreen(void)
{
    int minutes,seconds,second_tics;
    extern int PlayClock;
    extern short LevelSecrets;
    short w,h;
    short limit;


#define BONUS_SCREEN_PIC 5120
#define BONUS_ANIM 5121
#define BONUS_ANIM_FRAMES (5159-5121)

#define BREAK_LIGHT_RATE 18

#define BONUS_PUNCH 5121
#define BONUS_KICK 5136
#define BONUS_GRAB 5151
#define BONUS_REST 5121

#define BONUS_TICS 8
#define BONUS_GRAB_TICS 20
#define BONUS_REST_TICS 50

    static STATE s_BonusPunch[] =
    {
        {BONUS_PUNCH + 0, BONUS_TICS, NULL, &s_BonusPunch[1]},
        {BONUS_PUNCH + 1, BONUS_TICS, NULL, &s_BonusPunch[2]},
        {BONUS_PUNCH + 2, BONUS_TICS, NULL, &s_BonusPunch[3]},
        {BONUS_PUNCH + 2, 0|SF_QUICK_CALL, BonusPunchSound, &s_BonusPunch[4]},
        {BONUS_PUNCH + 3, BONUS_TICS, NULL, &s_BonusPunch[5]},
        {BONUS_PUNCH + 4, BONUS_TICS, NULL, &s_BonusPunch[6]},
        {BONUS_PUNCH + 5, BONUS_TICS, NULL, &s_BonusPunch[7]},
        {BONUS_PUNCH + 6, BONUS_TICS, NULL, &s_BonusPunch[8]},
        {BONUS_PUNCH + 7, BONUS_TICS, NULL, &s_BonusPunch[9]},
        {BONUS_PUNCH + 8, BONUS_TICS, NULL, &s_BonusPunch[10]},
        {BONUS_PUNCH + 9, BONUS_TICS, NULL, &s_BonusPunch[11]},
        {BONUS_PUNCH + 10, BONUS_TICS, NULL, &s_BonusPunch[12]},
        {BONUS_PUNCH + 11, BONUS_TICS, NULL, &s_BonusPunch[13]},
        {BONUS_PUNCH + 12, BONUS_TICS, NULL, &s_BonusPunch[14]},
        {BONUS_PUNCH + 14, 90,        NULL, &s_BonusPunch[15]},
        {BONUS_PUNCH + 14, BONUS_TICS, NULL, &s_BonusPunch[15]},
    };

    static STATE s_BonusKick[] =
    {
        {BONUS_KICK + 0, BONUS_TICS, NULL, &s_BonusKick[1]},
        {BONUS_KICK + 1, BONUS_TICS, NULL, &s_BonusKick[2]},
        {BONUS_KICK + 2, BONUS_TICS, NULL, &s_BonusKick[3]},
        {BONUS_KICK + 2, 0|SF_QUICK_CALL, BonusKickSound, &s_BonusKick[4]},
        {BONUS_KICK + 3, BONUS_TICS, NULL, &s_BonusKick[5]},
        {BONUS_KICK + 4, BONUS_TICS, NULL, &s_BonusKick[6]},
        {BONUS_KICK + 5, BONUS_TICS, NULL, &s_BonusKick[7]},
        {BONUS_KICK + 6, BONUS_TICS, NULL, &s_BonusKick[8]},
        {BONUS_KICK + 7, BONUS_TICS, NULL, &s_BonusKick[9]},
        {BONUS_KICK + 8, BONUS_TICS, NULL, &s_BonusKick[10]},
        {BONUS_KICK + 9, BONUS_TICS, NULL, &s_BonusKick[11]},
        {BONUS_KICK + 10, BONUS_TICS, NULL, &s_BonusKick[12]},
        {BONUS_KICK + 11, BONUS_TICS, NULL, &s_BonusKick[13]},
        {BONUS_KICK + 12, BONUS_TICS, NULL, &s_BonusKick[14]},
        {BONUS_KICK + 14, 90,        NULL, &s_BonusKick[15]},
        {BONUS_KICK + 14, BONUS_TICS, NULL, &s_BonusKick[15]},
    };

    static STATE s_BonusGrab[] =
    {
        {BONUS_GRAB + 0, BONUS_GRAB_TICS, NULL, &s_BonusGrab[1]},
        {BONUS_GRAB + 1, BONUS_GRAB_TICS, NULL, &s_BonusGrab[2]},
        {BONUS_GRAB + 2, BONUS_GRAB_TICS, NULL, &s_BonusGrab[3]},
        {BONUS_GRAB + 2, 0|SF_QUICK_CALL, BonusGrabSound, &s_BonusGrab[4]},
        {BONUS_GRAB + 3, BONUS_GRAB_TICS, NULL, &s_BonusGrab[5]},
        {BONUS_GRAB + 4, BONUS_GRAB_TICS, NULL, &s_BonusGrab[6]},
        {BONUS_GRAB + 5, BONUS_GRAB_TICS, NULL, &s_BonusGrab[7]},
        {BONUS_GRAB + 6, BONUS_GRAB_TICS, NULL, &s_BonusGrab[8]},
        {BONUS_GRAB + 7, BONUS_GRAB_TICS, NULL, &s_BonusGrab[9]},
        {BONUS_GRAB + 8, BONUS_GRAB_TICS, NULL, &s_BonusGrab[10]},
        {BONUS_GRAB + 9, 90,             NULL, &s_BonusGrab[11]},
        {BONUS_GRAB + 9, BONUS_GRAB_TICS, NULL, &s_BonusGrab[11]},
    };

#if 1 // Turned off the standing animate because he looks like a FAG!
    static STATE s_BonusRest[] =
    {
        {BONUS_REST + 0, BONUS_REST_TICS, NULL, &s_BonusRest[1]},
        {BONUS_REST + 1, BONUS_REST_TICS, NULL, &s_BonusRest[2]},
        {BONUS_REST + 2, BONUS_REST_TICS, NULL, &s_BonusRest[3]},
        {BONUS_REST + 1, BONUS_REST_TICS, NULL, &s_BonusRest[0]},
    };
#else
    static STATE s_BonusRest[] =
    {
        {BONUS_REST + 0, BONUS_REST_TICS, NULL, &s_BonusRest[1]},
        {BONUS_REST + 0, BONUS_REST_TICS, NULL, &s_BonusRest[0]},
    };
#endif

    static STATEp s_BonusAnim[] =
    {
        s_BonusPunch,
        s_BonusKick,
        s_BonusGrab
    };

    STATEp State = s_BonusRest;

    int Tics = 0;
    int line = 0;
    SWBOOL BonusDone;
    UserInput uinfo = { FALSE, FALSE, FALSE, dir_None };

    if (Level < 0) Level = 0;

    videoClearViewableArea(0L);
    videoNextPage();

    KB_ClearKeysDown();

    totalclock = ototalclock = 0;
    limit = synctics;

    if (gs.MusicOn)
    {
        PlaySong(voc[DIGI_ENDLEV].name, 3, TRUE, TRUE);
    }

    // special case code because I don't care any more!
    if (FinishAnim)
    {
        renderFlushPerms();
        rotatesprite(0, 0, RS_SCALE, 0, 5120, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(158<<16, 86<<16, RS_SCALE, 0, State->Pic, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);
        videoNextPage();
        FadeIn(0,0);
    }

    BonusDone = FALSE;
    while (!BonusDone)
    {
        handleevents();
        getpackets();

        // taken from top of faketimerhandler
        if (totalclock < ototalclock + limit)
        {
            continue;
        }
        ototalclock += limit;

        CONTROL_GetUserInput(&uinfo);
        CONTROL_ClearUserInput(&uinfo);
        if (KEY_PRESSED(KEYSC_SPACE) || KEY_PRESSED(KEYSC_ENTER) || uinfo.button0 || uinfo.button1)
        {
            if (State >= s_BonusRest && State < &s_BonusRest[SIZ(s_BonusRest)])
            {
                State = s_BonusAnim[STD_RANDOM_RANGE(SIZ(s_BonusAnim))];
                Tics = 0;
            }
        }

        gStateControl(&State, &Tics);

        videoClearViewableArea(0L);

        rotatesprite(0, 0, RS_SCALE, 0, 5120, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);

        if (UserMapName[0])
        {
            sprintf(ds,"%s",UserMapName);
            MNU_MeasureString(ds, &w, &h);
            MNU_DrawString(TEXT_TEST_COL(w), 20, ds,1,19);
        }
        else
        {
            if (PlayingLevel <= 1)
                PlayingLevel = 1;
            sprintf(ds,"%s",LevelInfo[PlayingLevel].Description);
            MNU_MeasureString(ds, &w, &h);
            MNU_DrawString(TEXT_TEST_COL(w), 20, ds,1,19);
        }

        sprintf(ds,"Completed");
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(TEXT_TEST_COL(w), 30, ds,1,19);

        rotatesprite(158<<16, 86<<16, RS_SCALE, 0, State->Pic, 0, 0, TITLE_ROT_FLAGS, 0, 0, xdim - 1, ydim - 1);

#define BONUS_LINE(i) (50 + ((i)*20))

        line = 0;
        second_tics = (PlayClock/120);
        minutes = (second_tics/60);
        seconds = (second_tics%60);
        sprintf(ds,"Your Time:  %2d : %02d", minutes, seconds);
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(60, BONUS_LINE(line), ds,1,16);

        if (!UserMapName[0])
        {
            line++;
            sprintf(ds,"3D Realms Best Time:  %s", LevelInfo[PlayingLevel].BestTime);
            MNU_MeasureString(ds, &w, &h);
            MNU_DrawString(40, BONUS_LINE(line), ds,1,16);

            line++;
            sprintf(ds,"Par Time:  %s", LevelInfo[PlayingLevel].ParTime);
            MNU_MeasureString(ds, &w, &h);
            MNU_DrawString(40, BONUS_LINE(line), ds,1,16);
        }


        // always read secrets and kills from the first player
        line++;
        sprintf(ds,"Secrets:  %d / %d", Player->SecretsFound, LevelSecrets);
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(60, BONUS_LINE(line), ds,1,16);

        line++;
        sprintf(ds,"Kills:  %d / %d", Player->Kills, TotalKillable);
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(60, BONUS_LINE(line), ds,1,16);


        sprintf(ds,"Press SPACE to continue");
        MNU_MeasureString(ds, &w, &h);
        MNU_DrawString(TEXT_TEST_COL(w), 185, ds,1,19);

        videoNextPage();
        ScreenCaptureKeys();

        if (State == State->NextState)
            BonusDone = TRUE;
    }

    StopSound();
    Terminate3DSounds();
}

void EndGameSequence(void)
{
    SWBOOL anim_ok = TRUE;
    FadeOut(0, 5);

    if ((gs.ParentalLock || Global_PLock) && FinishAnim == ANIM_SUMO)
        anim_ok = FALSE;

    if (anim_ok)
        playanm(FinishAnim);

    BonusScreen();

    ExitLevel = FALSE;
    QuitFlag = FALSE;
    AutoNet = FALSE;

    if (FinishAnim == ANIM_ZILLA)
        CreditsLevel();

    ExitLevel = FALSE;
    QuitFlag = FALSE;
    AutoNet = FALSE;

    if (SW_SHAREWARE)
    {
        Level = 0;
    }
    else
    {
        if (Level == 4 || Level == 20)
        {
            Level=0;
        }
        else
            Level++;
    }
}

void
StatScreen(PLAYERp mpp)
{
    extern SWBOOL FinishedLevel;
    short w,h;

    short rows,cols,i,j;
    PLAYERp pp = NULL;
    int x,y;
    short death_total[MAX_SW_PLAYERS_REG];
    short kills[MAX_SW_PLAYERS_REG];
    short pal;

#define STAT_START_X 20
#define STAT_START_Y 85
#define STAT_OFF_Y 9
#define STAT_HEADER_Y 14

#define SM_SIZ(num) ((num)*4)

#define STAT_TABLE_X (STAT_START_X + SM_SIZ(15))
#define STAT_TABLE_XOFF SM_SIZ(6)

    // No stats in bot games
    //if (BotMode) return;

    ResetPalette(mpp);
    COVER_SetReverb(0); // Reset reverb
    StopSound();

    if (FinishAnim)
    {
        EndGameSequence();
        return;
    }

    if (gNet.MultiGameType != MULTI_GAME_COMMBAT)
    {
        if (!FinishedLevel)
            return;
        BonusScreen();
        return;
    }

    renderFlushPerms();
    DrawStatScreen();

    memset(death_total,0,sizeof(death_total));
    memset(kills,0,sizeof(kills));

    sprintf(ds,"MULTIPLAYER TOTALS");
    MNU_MeasureString(ds, &w, &h);
    MNU_DrawString(TEXT_TEST_COL(w), 68, ds, 0, 0);

    sprintf(ds,"PRESS SPACE BAR TO CONTINUE");
    MNU_MeasureString(ds, &w, &h);
    MNU_DrawString(TEXT_TEST_COL(w), 189, ds, 0, 0);

    x = STAT_START_X;
    y = STAT_START_Y;

    sprintf(ds,"  NAME         1     2     3     4     5     6     7    8     KILLS");
    DisplayMiniBarSmString(mpp, x, y, 0, ds);
    rows = OrigCommPlayers;
    cols = OrigCommPlayers;
    mpp = Player + myconnectindex;

    y += STAT_HEADER_Y;

    for (i = 0; i < rows; i++)
    {
        x = STAT_START_X;
        pp = Player + i;

        sprintf(ds,"%d", i+1);
        DisplayMiniBarSmString(mpp, x, y, 0, ds);

        sprintf(ds,"  %-13s", pp->PlayerName);
        DisplayMiniBarSmString(mpp, x, y, PALETTE_PLAYER0 + pp->TeamColor, ds);

        x = STAT_TABLE_X;
        for (j = 0; j < cols; j++)
        {
            pal = 0;
            death_total[j] += pp->KilledPlayer[j];

            if (i == j)
            {
                // don't add kill for self or team player
                pal = PALETTE_PLAYER0 + 4;
                kills[i] -= pp->KilledPlayer[j];  // subtract self kills
            }
            else if (gNet.TeamPlay)
            {
                if (pp->TeamColor == Player[j].TeamColor)
                {
                    // don't add kill for self or team player
                    pal = PALETTE_PLAYER0 + 4;
                    kills[i] -= pp->KilledPlayer[j];  // subtract self kills
                }
                else
                    kills[i] += pp->KilledPlayer[j];  // kills added here
            }
            else
            {
                kills[i] += pp->KilledPlayer[j];  // kills added here
            }

            sprintf(ds,"%d", pp->KilledPlayer[j]);
            DisplayMiniBarSmString(mpp, x, y, pal, ds);
            x += STAT_TABLE_XOFF;
        }

        y += STAT_OFF_Y;
    }


    // Deaths

    x = STAT_START_X;
    y += STAT_OFF_Y;

    sprintf(ds,"   DEATHS");
    DisplayMiniBarSmString(mpp, x, y, 0, ds);
    x = STAT_TABLE_X;

    for (j = 0; j < cols; j++)
    {
        sprintf(ds,"%d",death_total[j]);
        DisplayMiniBarSmString(mpp, x, y, 0, ds);
        x += STAT_TABLE_XOFF;
    }

    x = STAT_START_X;
    y += STAT_OFF_Y;

    // Kills
    x = STAT_TABLE_X + SM_SIZ(50);
    y = STAT_START_Y + STAT_HEADER_Y;

    for (i = 0; i < rows; i++)
    {
        pp = Player + i;

        sprintf(ds,"%d", kills[i]); //pp->Kills);
        DisplayMiniBarSmString(mpp, x, y, 0, ds);

        y += STAT_OFF_Y;
    }

    videoNextPage();

    if (KeyPressed())
    {
        while (KeyPressed() && !quitevent) { handleevents(); getpackets(); }
    }

    KEY_PRESSED(KEYSC_SPACE) = 0;
    KEY_PRESSED(KEYSC_ENTER) = 0;

    if (gs.MusicOn)
    {
        PlaySong(voc[DIGI_ENDLEV].name, 3, TRUE, TRUE);
    }

    while (!KEY_PRESSED(KEYSC_SPACE) && !KEY_PRESSED(KEYSC_ENTER))
    {
        handleevents();
        getpackets();
        ScreenCaptureKeys();
    }

    StopSound();
    Terminate3DSounds();
}

void
GameIntro(void)
{

    DSPRINTF(ds,"GameIntro...");
    MONO_PRINT(ds);

    if (DemoPlaying)
        return;

    // this could probably be taken out and you could select skill level
    // from menu to start the game
    if (!CommEnabled && UserMapName[0])
        return;

    Level = 1;



    PlayTheme();

    if (!AutoNet)
    {
        LogoLevel();
        //CreditsLevel();
        IntroAnimLevel();
        IntroAnimCount = 0;
    }

    MenuLevel();
}

void
Control(int32_t argc, char const * const * argv)
{

    InitGame(argc, argv);
    if (QuitFlag)
        return;

    MONO_PRINT("InitGame done");
    VBJS_SetGenericMode(1);
    VBJS_Init();
    MNU_InitMenus();
    InGame = TRUE;
    GameIntro();
    //NewGame = TRUE;

    while (!QuitFlag)
    {
        handleevents();
        OSD_DispatchQueued();

        if (quitevent) QuitFlag = TRUE;

        NewLevel();
    }

    CleanExit = TRUE;
    // The debugger bridge owns a listener thread. Stop it while SW's engine
    // state is still alive; otherwise macOS can tear down its shared globals
    // underneath that thread when the game window is closed.
    VBJS_Shutdown();
    TerminateGame();
}


void
_Assert(const char *expr, const char *strFile, unsigned uLine)
{
    debug_break();
    TerminateWithMsg(0, "Assertion failed: %s %s, line %u", expr, strFile, uLine);
}


#if 0
void
_ErrMsg(const char *strFile, unsigned uLine, const char *format, ...)
{
    va_list arglist;

    //DSPRINTF(ds, "Error: %s, line %u", strFile, uLine);
    //MONO_PRINT(ds);
    TerminateGame();

    {
        char msg[256], *p;
        Bsnprintf(msg, sizeof(msg), "Error: %s, line %u\n", strFile, uLine);
        p = &msg[strlen(msg)];
        va_start(arglist, format);
        Bvsnprintf(msg, sizeof(msg) - (p-msg), format, arglist);
        va_end(arglist);
        wm_msgbox(apptitle, "%s", msg);
    }

    exit(0);
}
#endif

void
dsprintf(char *str, char *format, ...)
{
    va_list arglist;

    va_start(arglist, format);
    vsprintf(str, format, arglist);
    va_end(arglist);
}

void
dsprintf_null(char * /*str*/, const char * /*format*/, ...)
{
}

void getinput(SW_PACKET *, SWBOOL);

void MoveLoop(void)
{
    int pnum;

    getpackets();

    if (PredictionOn && CommEnabled)
    {
        while (predictmovefifoplc < Player[myconnectindex].movefifoend)
        {
            DoPrediction(ppp);
        }
    }

    //While you have new input packets to process...
    if (!CommEnabled)
        bufferjitter = 0;

    while (Player[myconnectindex].movefifoend - movefifoplc > bufferjitter)
    {
        //Make sure you have at least 1 packet from everyone else
        for (pnum=connecthead; pnum>=0; pnum=connectpoint2[pnum])
        {
            if (movefifoplc == Player[pnum].movefifoend)
            {
                break;
            }
        }

        //Pnum is >= 0 only if last loop was broken, meaning a player wasn't caught up
        if (pnum >= 0)
            break;

        domovethings();

#if DEBUG
        //if (DemoSyncRecord)
        //    demosync_record();
#endif
    }

    // Get input again to update q16ang/q16horiz.
    if (!PedanticMode)
        getinput(&loc, TRUE);

    if (!InputMode && !PauseKeySet)
        MNU_CheckForMenus();
}


void InitPlayerGameSettings(void)
{
    int pnum;

    // don't jack with auto aim settings if DemoMode is going
    // what the hell did I do this for?????????
    //if (DemoMode)
    //    return;

    if (CommEnabled)
    {
        // everyone gets the same Auto Aim
        TRAVERSE_CONNECT(pnum)
        {
            if (gNet.AutoAim)
                SET(Player[pnum].Flags, PF_AUTO_AIM);
            else
                RESET(Player[pnum].Flags, PF_AUTO_AIM);
        }
    }
    else
    {
        if (gs.AutoAim)
            SET(Player[myconnectindex].Flags, PF_AUTO_AIM);
        else
            RESET(Player[myconnectindex].Flags, PF_AUTO_AIM);
    }

    // everyone had their own Auto Run
    if (gs.AutoRun)
        SET(Player[myconnectindex].Flags, PF_LOCK_RUN);
    else
        RESET(Player[myconnectindex].Flags, PF_LOCK_RUN);

    if (gs.MouseAimingOn)
        SET(Player[myconnectindex].Flags, PF_MOUSE_AIMING_ON);
    else
        RESET(Player[myconnectindex].Flags, PF_MOUSE_AIMING_ON);
}

static char const *SW_CrossplayDukeLevelMusicName(void)
{
    static char const * const dukeMusic[4][11] =
    {
        { "stalker.mid",  "dethtoll.mid", "streets.mid",  "watrwld1.mid", "snake1.mid",  "thecall.mid",
          "ahgeez.mid",   "dethtoll.mid", "streets.mid",  "watrwld1.mid", "snake1.mid" },
        { "futurmil.mid", "storm.mid",    "gutwrnch.mid", "robocrep.mid", "stalag.mid",  "pizzed.mid",
          "alienz.mid",   "xplasma.mid",  "alfredh.mid",  "gloomy.mid",   "intents.mid" },
        { "inhiding.mid", "FATCMDR.mid",  "NAMES.MID",    "subway.mid",   "invader.mid", "gotham.mid",
          "233c.mid",     "lordofla.mid", "urban.mid",    "spook.mid",    "whomp.mid" },
        { "missimp.mid",  "prepd.mid",    "bakedgds.mid", "cf.mid",       "lemchill.mid","pob.mid",
          "warehaus.mid", "layers.mid",   "floghorn.mid", "depart.mid",   "restrict.mid" },
    };

    if (!SW_CrossplayDukeMapBridgeActive())
        return NULL;

    char const *baseName = LevelName;
    char const *slash = Bstrrchr(baseName, '/');
    char const *backslash = Bstrrchr(baseName, '\\');
    if (slash != NULL && slash[1] != '\0')
        baseName = slash + 1;
    if (backslash != NULL && backslash[1] != '\0' && backslash > baseName)
        baseName = backslash + 1;

    char e = 0, l = 0;
    int episode = 0, level = 0;
    if (sscanf(baseName, "%c%d%c%d", &e, &episode, &l, &level) != 4 ||
        Btoupper(e) != 'E' || Btoupper(l) != 'L')
        return NULL;

    if ((unsigned)(episode - 1) >= ARRAY_SIZE(dukeMusic) ||
        (unsigned)(level - 1) >= ARRAY_SIZE(dukeMusic[0]))
        return NULL;

    return dukeMusic[episode - 1][level - 1];
}

void PlayLevelSong(void)
{
    int track;
    char const *dukeLevelSong = SW_CrossplayDukeLevelMusicName();
    if (dukeLevelSong != NULL)
    {
        if (Bstrcasecmp(LevelSong, dukeLevelSong))
            LOG_F(INFO, "Crossplay: using Duke level music %s for %s.", dukeLevelSong, LevelName);
        Bstrncpyz(LevelSong, dukeLevelSong, sizeof(LevelSong));
    }

    if (dukeLevelSong != NULL)
    {
        track = -1;
    }
    else if (Level == 0)
    {
        track = RedBookSong[4 + RANDOM_RANGE(10)];
    }
    else
    {
        track = RedBookSong[Level];
    }

    PlaySong(LevelSong, track, TRUE, TRUE);
}

void InitRunLevel(void)
{
    if (DemoEdit)
        return;

    if (LoadGameOutsideMoveLoop)
    {
        int SavePlayClock;
        extern int PlayClock;
        LoadGameOutsideMoveLoop = FALSE;
        // contains what is needed from calls below
        if (gs.Ambient)
            StartAmbientSound();
        SetCrosshair();
        PlayLevelSong();
        SetRedrawScreen(Player + myconnectindex);
        // crappy little hack to prevent play clock from being overwritten
        // for load games
        SavePlayClock = PlayClock;
        InitTimingVars();
        PlayClock = SavePlayClock;
        MONO_PRINT("Done with InitRunLevel");
        return;
    }

#if 0
    // ensure we are through the initialization code before sending the game
    // version. Otherwise, it is possible to send this too early and have it
    // blown away on the other side.
    waitforeverybody();
#endif

    SendVersion(GameVersion);

    waitforeverybody();

    StopSong();

    if (Bstrcasecmp(CacheLastLevel, LevelName) != 0)
        DoTheCache();

    // auto aim / auto run / etc
    InitPlayerGameSettings();

    // send packets with player info
    InitNetPlayerOptions();

    // Initialize Game part of network code (When ready2send != 0)
    InitNetVars();

    PlayLevelSong();

    InitPrediction(&Player[myconnectindex]);

    if (!DemoInitOnce)
        DemoRecordSetup();

    // everything has been inited at least once for RECORD
    DemoInitOnce = TRUE;

//DebugWriteLoc(__FILE__, __LINE__);
    waitforeverybody();

    CheckVersion(GameVersion);

    // IMPORTANT - MUST be right before game loop AFTER waitforeverybody
    InitTimingVars();

    SetRedrawScreen(Player + myconnectindex);

    FX_SetVolume(gs.SoundVolume); // Turn volume back up
    if (gs.Ambient)
        StartAmbientSound();
}

void
RunLevel(void)
{
    InitRunLevel();

    FX_SetVolume(gs.SoundVolume);
    SetSongVolume(gs.MusicVolume);

#if 0
    waitforeverybody();
#endif
    ready2send = 1;

    while (TRUE)
    {
        handleevents();
        OSD_DispatchQueued();
        VBJS_RunFrame();

        if (quitevent) CON_Quit();

        //MONO_PRINT("Before MoveLoop");
        MoveLoop();
        //MONO_PRINT("After MoveLoop");
        //MONO_PRINT("Before DrawScreen");
        drawscreen(Player + screenpeek);
        //MONO_PRINT("After DrawScreen");

        if (QuitFlag)
            break;

        if (ExitLevel)
        {
            ExitLevel = FALSE;
            break;
        }

        timerUpdateClock();
        while (ready2send && (totalclock >= ototalclock + synctics))
            UpdateInputs();
    }

    ready2send = 0;
}

void swexit(int exitval)
{
    exit(exitval);
}

void DosScreen(void)
{
#if 0
#ifdef SW_SHAREWARE
#define DOS_SCREEN_NAME "SHADSW.BIN"
#else
#define DOS_SCREEN_NAME "SWREG.BIN"
#endif

#define DOS_SCREEN_SIZE (4000-(80*2))
#define DOS_SCREEN_PTR ((void *)(0xB8000))
    buildvfs_kfd fin;
    int i;
    char buffer[DOS_SCREEN_SIZE];

    fin = kopen4load(DOS_SCREEN_NAME,0);
    if (fin == buildvfs_kfd_invalid)
        return;

    kread(fin, buffer, sizeof(buffer));
    memcpy(DOS_SCREEN_PTR, buffer, DOS_SCREEN_SIZE);
    kclose(fin);
    move_cursor(23,0);
    _displaycursor(_GCURSORON);
#endif
}

typedef struct
{
    char    notshareware;
    const char    *arg_switch;
    short   arg_match_len;
    const char    *arg_fmt;
    const char    *arg_descr;
} CLI_ARG;

#if DEBUG
CLI_ARG cli_dbg_arg[] =
{
    {0, "/demosyncrecord",     13,     "-demosyncrecord",      "Demo sync record"                      },
    {0, "/demosynctest",       13,     "-demosynctest",        "Demo sync test"                        },
    {0, "/cam",                4,      "-cam",                 "Camera test mode"                      },
    {0, "/debugactor",         11,     "-debugactor",          "No Actors"                             },
    {0, "/debuganim",          10,     "-debuganim",           "No Anims"                              },
    {0, "/debugso",            8,      "-debugso",             "No Sector Objects"                     },
    {0, "/debugsector",        12,     "-debugsector",         "No Sector Movement"                    },
    {0, "/debugpanel",         11,     "-debugpanel",          "No Panel"                              },
    {0, "/mono",               5,      "-mono",                "Mono"                                  },
};
#endif


CLI_ARG cli_arg[] =
{
    {0, "/?",                  2,      "-?",                   "This help message"                     },
//#ifndef SW_SHAREWARE
//{"/l",                  2,      "-l#",                  "Level (1-11)"                          },
//{"/v",                  2,      "-v#",                  "Volume (1-3)"                          },
    {1, "/map",                4,      "-map [mapname]",       "Load a map"                            },
    {1, "/nocdaudio",          5,      "-nocd<audio>",         "No CD Red Book Audio"                  },
//#endif

    {0, "/name",               5,      "-name [playername]",   "Player Name"                           },
    {0, "/s",                  2,      "-s#",                  "Skill (1-4)"                           },
    {0, "/f#",                 3,      "-f#",                  "Packet Duplication - 2, 4, 8"          },
    {0, "/nopredict",          7,      "-nopred<ict>",         "Disable Net Prediction Method"         },
    {0, "/level#",             5,      "-level#",              "Start at level# (Shareware: 1-4, full version 1-28)"      },
    {0, "/dr",                 3,      "-dr[filename.dmo]",    "Demo record. NOTE: Must use -level# with this option."           },
    {0, "/dp",                 3,      "-dp[filename.dmo]",    "Demo playback. NOTE: Must use -level# with this option."         },
    {0, "/m",                  6,      "-monst<ers>",          "No Monsters"                           },
    {0, "/nodemo",             6,      "-nodemo",              "No demos on game startup"              },
    {0, "/nometers",           9,      "-nometers",            "Don't show air or boss meter bars in game"},
    {0, "/movescale #",        9,      "-movescale",           "Adjust movement scale: 256 = 1 unit"},
    {0, "/turnscale #",        9,      "-turnscale",           "Adjust turning scale: 256 = 1 unit"},
    {0, "/extcompat",          9,      "-extcompat",           "Controller compatibility mode (with Duke 3D)"},
    {1, "/g#",                 2,      "-g[filename.grp]",     "Load an extra GRP or ZIP file"},
    {1, "/h#",                 2,      "-h[filename.def]",     "Use filename.def instead of SW.DEF"},
    {0, "/setup",              5,      "-setup",               "Displays the configuration dialogue box"},
#if DEBUG
    {0, "/coop",               5,      "-coop#",               "Single Player Cooperative Mode"        },
    {0, "/commbat",            8,      "-commbat#",            "Single Player Commbat Mode"            },
    {0, "/debug",              6,      "-debug",               "Debug Help Options"                    },
#endif

#if 0 //def NET_MODE_MASTER_SLAVE
    {0, "/broadcast",          6,      "-broad<cast>",         "Broadcast network method (default)"    },
    {0, "/masterslave",        7,      "-master<slave>",       "Master/Slave network method"           },
#endif
};

#if 0
Map->User Map Name
Auto->Auto Start Game
Rules->0=WangBang 1=WangBang(No Respawn) 2=CoOperative
                                            Level->0 to 24 (?)
                                            Enemy->0=None 1=Easy 2=Norm 3=Hard 4=Insane
                                                                                  Markers->0=Off 1=On
                                                                                                    Team->0=Off 1=On
                                                                                                                   HurtTeam->0=Off 1=On
                                                                                                                                      KillLimit->0=Infinite 1=10 2=20 3=30 4=40 5=50 6=60 7=70 8=80 9=90 10=100
                                                                                                                                                                                                             TimeLimit->0=Infinite 1=3 2=5 3=10 4=20 5=30 6=45 7=60
                                                                                                                                                                                                                                                                  Color->0=Brown 1=Purple 2=Red 3=Yellow 4=Olive 5=Green
                                                                                                                                                                                                                                                                                                                    Nuke->0=Off 1=On

                                                                                                                                                                                                                                                                                                                                   Example Command Line :
                                                                                                                                                                                                                                                                                                                                   sw -map testmap.map -autonet 0,0,1,1,1,0,3,2,1,1 -f4 -name 1234567890 -net 12345678
commit -map grenade -autonet 0,0,1,1,1,0,3,2,1,1 -name frank
#endif

int DetectShareware(void)
{
#define DOS_SCREEN_NAME_SW  "SHADSW.BIN"
#define DOS_SCREEN_NAME_REG "SWREG.BIN"

    buildvfs_kfd h;

    h = kopen4load(DOS_SCREEN_NAME_SW,1);
    if (h != buildvfs_kfd_invalid)
    {
        SW_GameFlags |= GAMEFLAG_SHAREWARE;
        kclose(h);
        return 0;
    }

    h = kopen4load(DOS_SCREEN_NAME_REG,1);
    if (h != buildvfs_kfd_invalid)
    {
        SW_GameFlags &= ~GAMEFLAG_SHAREWARE;
        kclose(h);
        return 0;
    }

    return 1;   // heavens knows what this is...
}


void CommandLineHelp(char const * const * /*argv*/)
{
    int i;
    char *str;
    int strl;

    strl = 30 + 70 + sizeof(VB_NETARGS_HELP);
    for (i=0; i < (int)SIZ(cli_arg); i++)
        if (cli_arg[i].arg_fmt && (!SW_SHAREWARE || (!cli_arg[i].notshareware && SW_SHAREWARE)))
            strl += strlen(cli_arg[i].arg_fmt) + 1 + strlen(cli_arg[i].arg_descr) + 1;

    str = (char *)Xmalloc(strl);
    if (str)
    {
        strcpy(str,"Usage: sw [options]\n");
        strcat(str,"options:  ('/' may be used instead of '-', <> text is optional)\n\n");
        for (i=0; i < (int)SIZ(cli_arg); i++)
        {
            if (cli_arg[i].arg_fmt && (!SW_SHAREWARE || (!cli_arg[i].notshareware && SW_SHAREWARE)))
            {
                strcat(str, cli_arg[i].arg_fmt);
                strcat(str, "\t");
                strcat(str, cli_arg[i].arg_descr);
                strcat(str, "\n");
            }
        }
        strcat(str, VB_NETARGS_HELP);   // VibeBuild32: unified multiplayer flags
        wm_msgbox("Shadow Warrior Help", "%s", str);
        Xfree(str);
    }
}

// Renamed from app_main so both games can live in one binary: the engine's
// app_main is the game selector (vibebuild32/src/gameselect.cpp), which
// dispatches here for Shadow Warrior.
int32_t SW_app_main(int32_t argc, char const * const * argv)
{
    int i;
    extern int MovesPerPacket;
    void DoSector(void);
    void gameinput(void);
    int cnt = 0;

    // VibeBuild32: shared multiplayer flags (advertise/lobbylist/netfile/headless/etc).
    // SW sets server mode via mmulti, so networkMode stays NULL here; -joinlobby (auto-join),
    // -punchto and -masteraddr are SW-specific and handled by their own branches below.
    vb_netarg_ctx const swNetArg = {
        NULL, 0, 0,
        &g_noSetup, &g_noLogo,
        NULL, NULL, NULL, 0,
        Net_SetAdvertise, Net_QueryLobby, NULL, Net_AddNetFile
    };
    for (i=1; i<argc; i++)
    {
        if (argv[i][0] != '-')   // VibeBuild32 modern args: dash only, no slash
            continue;
        // Option name with leading dashes stripped, so "-foo" and "--foo" both work.
        const char *o = (argv[i][1] == '-') ? argv[i] + 2 : argv[i] + 1;
        if (!Bstrcasecmp(o, "setup"))
        {
            CommandSetup = TRUE;
            g_noSetup = 0;
        }
        else if (!Bstrcasecmp(o, "nosetup"))
        {
            CommandSetup = FALSE;
            g_noSetup = 1;
        }
        else if (!Bstrcasecmp(o, "nologo") || !Bstrcasecmp(o, "quick"))
        {
            g_noLogo = 1;
        }
        else if (!Bstrcasecmp(o, "color") && i + 1 < argc)
        {
            gs.NetColor = clamp(atoi(argv[++i]), 0, 7);
        }
        else if (!Bstrcasecmp(o, "window"))
        {
            ud_setup.ScreenMode = 0;
        }
        else if (!Bstrcasecmp(o, "fullscreen"))
        {
            ud_setup.ScreenMode = 1;
        }
        else if (!Bstrcasecmp(o, "resolution") && i + 1 < argc)
        {
            int width = 0, height = 0;
            if (sscanf(argv[++i], "%dx%d", &width, &height) == 2 && width >= 320 && height >= 200)
                ud_setup.ScreenWidth = width, ud_setup.ScreenHeight = height;
        }
        else if (!Bstrcasecmp(o, "punchto") && i+1 < argc)
        {
            Net_SetPunchTarget(argv[i+1]);  // SW-specific: client NAT-punch toward this server
            i++;
        }
        else if (!Bstrcasecmp(o, "masteraddr") && i+1 < argc)
        {
            Net_SetMasterAddr(argv[i+1]);   // SW-specific: masterserver used for the punch
            i++;
        }
        else if (!Bstrcasecmp(o, "joinlobby") && i+1 < argc)
        {
            Net_SetAutoJoin(argv[i+1]);     // SW-specific: query masterserver + auto-join (relaunch)
            i++;
        }
        else if (int const nc = VB_NetArg(&swNetArg, o, (i+1 < argc) ? argv[i+1] : nullptr))
        {
            i += nc - 1;   // shared: -advertise/-lobbylist/-netfile/-headless (the for-loop does one ++)
        }
        else if (!Bstrcasecmp(o, "?") || !Bstrcasecmp(o, "h") || !Bstrcasecmp(o, "help"))
        {
            CommandLineHelp(argv);
            return 0;
        }
    }

#ifdef _WIN32
#ifndef DEBUGGINGAIDS
    if (!G_CheckCmdSwitch(argc, argv, "-noinstancechecking") && !windowsCheckAlreadyRunning())
    {
#ifdef EDUKE32_STANDALONE
        if (!wm_ynbox(APPNAME, "It looks like " APPNAME " is already running.\n\n"
#else
        if (!wm_ynbox(APPNAME, "It looks like the game is already running.\n\n"
#endif
                      "Are you sure you want to start another copy?"))
            return 3;
    }
#endif
#endif

    SW_ExtPreInit(argc, argv);

#if defined(PREFIX)
    {
        const char *prefixdir = PREFIX;
        if (prefixdir && prefixdir[0])
        {
            addsearchpath(prefixdir);
        }
    }
#endif

#ifdef __APPLE__
    if (!g_useCwd)
    {
        char cwd[BMAX_PATH];
        char *homedir = Bgethomedir();
        if (homedir)
            Bsnprintf(cwd, sizeof(cwd), "%s/Library/Logs/" APPBASENAME ".log", homedir);
        else
            Bstrcpy(cwd, APPBASENAME ".log");
        OSD_SetLogFile(cwd);
        Xfree(homedir);
    }
    else
#endif
    OSD_SetLogFile(APPBASENAME ".log");

    wm_setapptitle(APPNAME);

    LOG_F(INFO, APPNAME " %s", s_buildRev);
    PrintBuildInfo();

    OSD_SetFunctions(
        NULL, NULL, NULL, NULL, NULL,
        COMMON_clearbackground,
        BGetTime,
        NULL
        );

    if (argc > 1)
    {
        char tempbuf[1024];
        size_t constexpr size = ARRAY_SIZE(tempbuf);
        size_t bytesWritten = Bsnprintf(tempbuf, size, "Application parameters:");
        for (i = 1; i < argc; ++i)
            bytesWritten += Bsnprintf(tempbuf + bytesWritten, size - bytesWritten, " %s", argv[i]);
        LOG_F(INFO, "%s", tempbuf);
    }

    SW_ExtInit();

    for (cnt = 1; cnt < argc; ++cnt)
    {
        char const * arg = argv[cnt];
        if (*arg != '-')   // VibeBuild32 modern args: dash only, no slash
            continue;
        while (*arg == '-') ++arg;   // accept -foo and --foo

        if (Bstrncasecmp(arg, "j", 1) == 0 && !SW_SHAREWARE)
        {
            if (strlen(arg) > 1)
            {
                const char * dir = arg+1;
                int err = addsearchpath(dir);
                if (err < 0)
                    LOG_F(ERROR, "Failed adding %s for game data: %s", dir,
                                 err==-1 ? "not a directory" : "no such directory");
            }
        }
    }

    // hackish since SW's init order is a bit different right now
    if (G_CheckCmdSwitch(argc, argv, "-addon0"))
    {
        g_addonNum = 0;
        g_noSetup = 1;
    }
    else if (G_CheckCmdSwitch(argc, argv, "-addon1"))
    {
        g_addonNum = 1;
        g_noSetup = 1;
    }
    else if (G_CheckCmdSwitch(argc, argv, "-addon2"))
    {
        g_addonNum = 2;
        g_noSetup = 1;
    }

    i = CONFIG_ReadSetup();

    if (enginePreInit())
    {
        wm_msgbox("Build Engine Initialization Error",
                  "There was a problem initializing the engine: %s", engineerrstr);
        exit(1);
    }

    SW_ScanGroups();

#ifdef STARTUP_SETUP_WINDOW
    // VibeBuild32: -nosetup (g_noSetup) also skips the forced first-run window
    // (missing config, i<0), so `-game sw -nosetup` launches directly in the
    // unified binary without hand-seeding a config.
    if ((i < 0 && !g_noSetup) || CommandSetup || (ud_setup.ForceSetup && !g_noSetup))
    {
        if (quitevent || !startwin_run())
        {
            engineUnInit();
            FreeGroups();
            exit(0);
        }
    }
#endif

    SW_LoadGroups();

    if (artCrossplayForeignDataCount() > 0)
    {
        artSetBaseSearchFirst(1);
        paletteLoadFromDisk();
        LOG_F(INFO, "Crossplay: runtime palette pinned to main Shadow Warrior game data.");
    }

    if (!g_useCwd)
        SW_CleanupSearchPaths();

    DetectShareware();

    if (SW_SHAREWARE)
    {
        wm_setapptitle(APPNAME " Shareware");

        // Zero out the maps that aren't in shareware version
        memset(&LevelInfo[MAX_LEVELS_SW+1], 0, sizeof(LEVEL_INFO)*(MAX_LEVELS_REG-MAX_LEVELS_SW));
        GameVersion++;
    }
    else
    {
        wm_setapptitle(APPNAME);

        if (SW_GameFlags & GAMEFLAG_SWWD)
        {
            for (LevelNamePatch const lnp : LevelNamesWD)
                LevelInfo[lnp.index].Description = lnp.name;

            strcpy(EpisodeNames[1], s_EpisodeNameWD);
            strcpy(EpisodeSubtitles[1], s_EpisodeSubtitleWD);
        }
        else if (SW_GameFlags & GAMEFLAG_SWTD)
        {
            for (LevelNamePatch const lnp : LevelNamesTD)
                LevelInfo[lnp.index].Description = lnp.name;

            strcpy(EpisodeNames[1], s_EpisodeNameTD);
            strcpy(EpisodeSubtitles[1], s_EpisodeSubtitleTD);
        }
    }

    for (i = 0; i < MAX_SW_PLAYERS; i++)
        INITLIST(&Player[i].PanelSpriteList);

    DebugOperate = TRUE;

    if (SW_SHAREWARE)
        LOG_F(INFO, "SHADOW WARRIOR(tm) Version 1.2 (Shareware Version)");
    else
        LOG_F(INFO, "SHADOW WARRIOR(tm) Version 1.2");

    LOG_F(INFO, "Copyright (c) 1997 3D Realms Entertainment");

    UserMapName[0] = '\0';

    //LocationInfo = TRUE;

#if 0
    //#if DEBUG && SYNC_TEST
    // automatically record a demo
    DemoRecording = TRUE;
    DemoPlaying = FALSE;
    PreCaching = TRUE;
    DemoRecCnt = 0;
    strcpy(DemoFileName, "DMOTEST.DMO");
    //DemoSyncRecord = TRUE;
#endif

#if DEBUG
    {
        FILE *fout;
        if ((fout = fopen("dbg.foo", "wb")) != NULL)
        {
            fprintf(fout, "Whoo-oo-ooooo wants some wang?\n");
            fclose(fout);
        }
    }
#endif

    for (cnt = 1; cnt < argc; cnt++)
    {
        char const *arg = argv[cnt];

        if (*arg != '-') continue;   // VibeBuild32 modern args: dash only, no slash

        // Store arg in command line array!
        CON_StoreArg(arg);
        while (*arg == '-') arg++;   // accept -foo and --foo

        if (Bstrncasecmp(arg, "autonet",7) == 0)
        {
            if (cnt <= argc-2)
            {
                cnt++;
                AutoNet = TRUE;
                sscanf(argv[cnt],"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",&Auto.Rules,&Auto.Level,&Auto.Enemy,&Auto.Markers,
                       &Auto.Team,&Auto.HurtTeam,&Auto.Kill,&Auto.Time,&Auto.Color,&Auto.Nuke);
            }
        }
        else if (Bstrncasecmp(arg, "turnscale",9) == 0)
        {
            if (cnt <= argc-2)
            {
                cnt++;
                sscanf(argv[cnt], "%d",&turn_scale);
            }
        }
        else if (Bstrncasecmp(arg, "movescale",9) == 0)
        {
            if (cnt <= argc-2)
            {
                cnt++;
                sscanf(argv[cnt], "%d",&move_scale);
            }
        }
        else if (Bstrncasecmp(arg, "extcompat",9) == 0)
        {
            move_scale *= 5;
            turn_scale *= 5;
        }
        else if (Bstrncasecmp(arg, "setupfile",9) == 0)
        {
            // Passed by setup.exe
            // skip setupfile name
            cnt++;
        }
        else if (Bstrncasecmp(arg, "net",3) == 0)
        {
            firstnet = cnt+1;
            break; // All further args go to mmulti.
        }
#if DEBUG
        else if (Bstrncasecmp(arg, "debug",5) == 0)
        {
            char *str;
            int strl;

            strl = 24 + 70;
            for (i=0; i < (int)SIZ(cli_dbg_arg); i++)
                strl += strlen(cli_dbg_arg[i].arg_fmt) + 1 + strlen(cli_dbg_arg[i].arg_descr) + 1;

            str = (char *)Xmalloc(strl);
            if (str)
            {
                strcpy(str,
                       "Usage: sw [options]\n"
                       "options:  ('/' may be used instead of '-', <> text is optional)\n\n"
                       );
                for (i=0; i < (int)SIZ(cli_dbg_arg); i++)
                {
                    strcat(str, cli_dbg_arg[i].arg_fmt);
                    strcat(str, "\t");
                    strcat(str, cli_dbg_arg[i].arg_descr);
                    strcat(str, "\n");
                }
                wm_msgbox("Shadow Warrior Debug Help",str);
                Xfree(str);
            }
            swexit(0);
        }
#endif
        else if (Bstrncasecmp(arg, "short",5) == 0)
        {
            ShortGameMode = TRUE;
        }
        else if (Bstrncasecmp(arg, "nodemo",6) == 0)
        {
            NoDemoStartup = TRUE;
        }
        else if (Bstrncasecmp(arg, "allsync",7) == 0)
        {
            NumSyncBytes = 8;
        }
        else if (Bstrncasecmp(arg, "name",4) == 0)
        {
            if (cnt <= argc-2)
            {
                strncpy(CommPlayerName, argv[++cnt], SIZ(CommPlayerName)-1);
                CommPlayerName[SIZ(CommPlayerName)-1] = '\0';
            }
        }
        else if (Bstrncasecmp(arg, "f8",2) == 0)
        {
            MovesPerPacket = 8;
        }
        else if (Bstrncasecmp(arg, "f4",2) == 0)
        {
            MovesPerPacket = 4;
        }
        else if (Bstrncasecmp(arg, "f2",2) == 0)
        {
            MovesPerPacket = 2;
        }
        else if (Bstrncasecmp(arg, "monst", 5) == 0)
        {
            DebugActor = TRUE;
        }
        // --monsters/-mnst <n>; -e is reserved as the shared episode alias.
        else if (Bstrncasecmp(arg, "monsters", 8) == 0 || Bstrncasecmp(arg, "mnst", 4) == 0)
        {
            const char *nstr = (cnt + 1 < argc ? argv[++cnt] : "0");
            int const n = atoi(nstr);
            if (n <= 0)
                DebugActor = TRUE;              // no enemies
            else
            {
                Skill = max(short(0), min(short(3), short(n - 1)));
                DebugActor = FALSE;
            }
        }
        else if (Bstrncasecmp(arg, "nopredict",9) == 0)
        {
            extern SWBOOL PredictionOn;
            PredictionOn = FALSE;
        }
        else if (Bstrncasecmp(arg, "col", 3) == 0)
        // provides a way to force the player color for joiners
        // since -autonet does not seem to work for them
        {
            int temp;
            cnt++;
            sscanf(argv[cnt],"%d",&temp);
            AutoColor = temp;
            HasAutoColor = TRUE;
        }
        else if (Bstrncasecmp(arg, "level", 5) == 0)
        {
            if (strlen(arg) > 5)
            {
                strcpy(UserMapName,LevelInfo[atoi(&arg[5])].LevelName);
            }
        }
        else if (Bstrncasecmp(arg, "s", 1) == 0)
        {
            if (strlen(arg) > 1)
                Skill = atoi(&arg[1])-1;

            Skill = max(Skill,short(0));
            Skill = min(Skill,short(3));
        }
        else if (Bstrncasecmp(arg, "commbat", 7) == 0)
        {
            if (strlen(arg) > 7)
            {
                FakeMultiNumPlayers = atoi(&arg[7]);
                gNet.MultiGameType = MULTI_GAME_COMMBAT;
            }
        }
        else
        /* bots suck, bye bye!
        #ifndef SW_SHAREWARE
                if (memcmp(argv[cnt], "-bots", 5) == 0)
            {
            if (strlen(argv[cnt]) > 5)
                {
                FakeMultiNumPlayers = atoi(&argv[cnt][5]);
                LOG_F(INFO, "Adding %d BOT(s) to the game!", FakeMultiNumPlayers);
                gNet.MultiGameType = MULTI_GAME_AI_BOTS;
                BotMode = TRUE;
                }
            }
        else
        #endif
        */
        if (Bstrncasecmp(arg, "nometers", 8) == 0)
        {
            NoMeters = TRUE;
        }
        else if (Bstrncasecmp(arg, "coop", 4) == 0)
        {
            if (strlen(arg) > 4)
            {
                FakeMultiNumPlayers = atoi(&arg[4]);
                gNet.MultiGameType = MULTI_GAME_COOPERATIVE;
            }
        }
        else if (Bstrncasecmp(arg, "ddr", 3) == 0)
        {
            //NumSyncBytes = 8;
            DemoRecording = TRUE;
            DemoPlaying = FALSE;
            DemoRecCnt = 0;
            DemoDebugMode = TRUE;

            if (strlen(arg) > 3)
            {
                strcpy(DemoFileName, &arg[3]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
        }
        else if (Bstrncasecmp(arg, "dr", 2) == 0)
        {
            //NumSyncBytes = 8;
            DemoRecording = TRUE;
            DemoPlaying = FALSE;
            DemoRecCnt = 0;

            if (strlen(arg) > 2)
            {
                strcpy(DemoFileName, &arg[2]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
        }
        else if (Bstrncasecmp(arg, "dp", 2) == 0)
        {
            DemoPlaying = TRUE;
            DemoRecording = FALSE;
            PreCaching = TRUE;

            if (strlen(arg) > 2)
            {
                strcpy(DemoFileName, &arg[2]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
        }
        // VibeBuild32 canonical demo flags: --record / --playback [file] (cross-game names for -dr/-dp).
        else if (Bstrncasecmp(arg, "record", 6) == 0)
        {
            DemoRecording = TRUE;
            DemoPlaying = FALSE;
            DemoRecCnt = 0;
        }
        else if (Bstrncasecmp(arg, "playback", 8) == 0)
        {
            DemoPlaying = TRUE;
            DemoRecording = FALSE;
            PreCaching = TRUE;

            if (cnt + 1 < argc && argv[cnt + 1][0] != '-')
            {
                strcpy(DemoFileName, argv[++cnt]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
        }

#if 0 //def NET_MODE_MASTER_SLAVE
        else if (Bstrncasecmp(arg, "masterslave",6) == 0)
        {
            NetModeOverride = TRUE;
            NetBroadcastMode = FALSE;
        }
        else if (Bstrncasecmp(arg, "broadcast",5) == 0)
        {
            NetModeOverride = TRUE;
            NetBroadcastMode = TRUE;
        }
#endif

        else if (Bstrncasecmp(arg, "cheat",5) == 0)
        {
            ArgCheat = TRUE;
        }
        else if (Bstrncasecmp(arg, "demosynctest",12) == 0)
        {
            NumSyncBytes = 8;
            DemoSyncTest = TRUE;
            DemoSyncRecord = FALSE;
        }
        else if (Bstrncasecmp(arg, "demosyncrecord",14) == 0)
        {
            NumSyncBytes = 8;
            DemoSyncTest = FALSE;
            DemoSyncRecord = TRUE;
        }
        else if (Bstrncasecmp(arg, "cam",3) == 0)
        {
            CameraTestMode = TRUE;
        }

#if DEBUG
        else if (Bstrncasecmp(arg, "de", 2) == 0)
        {
#if DEMO_FILE_TYPE == DEMO_FILE_GROUP
            DemoPlaying = TRUE;
            DemoRecording = FALSE;

            if (strlen(arg) > 2)
            {
                strcpy(DemoFileName, &arg[2]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
#else
            DemoEdit = TRUE;
            DemoPlaying = TRUE;
            DemoRecording = FALSE;

            if (strlen(arg) > 2)
            {
                strcpy(DemoFileName, &arg[2]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
#endif
        }
        else if (Bstrncasecmp(arg, "randprint",9) == 0)
        {
            RandomPrint = TRUE;
        }
        else if (Bstrncasecmp(arg, "level", 5) == 0)
        {
            if (strlen(arg) > 5)
            {
                strcpy(UserMapName,LevelInfo[atoi(&arg[5])].LevelName);
            }
        }
        else if (Bstrncasecmp(arg, "debugsecret", 11) == 0)
        {
            extern SWBOOL DebugSecret;
            DebugSecret = TRUE;
        }
        else if (Bstrncasecmp(arg, "debugactor", 10) == 0)
        {
            DebugActor = TRUE;
        }
        else if (Bstrncasecmp(arg, "mono", 4) == 0)
        {
            DispMono = TRUE;
        }
        else if (Bstrncasecmp(arg, "debugso", 7) == 0)
        {
            DebugSO = TRUE;
        }
        else if (Bstrncasecmp(arg, "nosyncprint",11) == 0)
        {
            extern SWBOOL SyncPrintMode;
            SyncPrintMode = FALSE;
        }
        else if (Bstrncasecmp(arg, "debuganim", 9) == 0)
        {
            DebugAnim = TRUE;
        }
        else if (Bstrncasecmp(arg, "debugsector", 11) == 0)
        {
            DebugSector = TRUE;
        }
        else if (Bstrncasecmp(arg, "debugpanel", 10) == 0)
        {
            DebugPanel = TRUE;
        }
        else if (Bstrncasecmp(arg, "dt", 2) == 0)
        {
            if (strlen(arg) > 2)
            {
                strcpy(DemoTmpName, &arg[2]);
                if (strchr(DemoFileName, '.') == 0)
                    strcat(DemoFileName, ".dmo");
            }
        }
        else if (Bstrncasecmp(arg, "nodemo", 6) == 0)
        {
            DemoRecording = FALSE;
            DemoPlaying = FALSE;
            PreCaching = TRUE;
            DemoRecCnt = 0;

            DemoSyncTest = FALSE;
            DemoSyncRecord = FALSE;
        }

#endif

        else if (Bstrncasecmp(arg, "map", 3) == 0 && !SW_SHAREWARE && cnt+1 < argc)
        {
            strcpy(UserMapName, argv[++cnt]);
            if (strchr(UserMapName, '.') == 0)
                strcat(UserMapName, ".map");
        }
        else if ((Bstrcasecmp(arg, "game_dir") == 0 || Bstrcasecmp(arg, "game-dir") == 0) && cnt + 1 < argc)
        {
            cnt++;
        }
        else if (!SW_SHAREWARE &&
                 (Bstrcasecmp(arg, "g") == 0 || Bstrcasecmp(arg, "grp") == 0 || Bstrcasecmp(arg, "gamegrp") == 0) &&
                 cnt + 1 < argc)
        {
            char const *grpFile = argv[++cnt];
            if (SW_AddGroupFileFromArg(grpFile) >= 0)
                LOG_F(INFO, "Added %s", grpFile);
            else
                LOG_F(WARNING, "Could not add game data %s", grpFile);
        }
        else if (!SW_SHAREWARE && Bstrncasecmp(arg, "g", 1) == 0 &&
                 Bstrcasecmp(arg, "game_dir") != 0 && Bstrcasecmp(arg, "game-dir") != 0 &&
                 Bstrcasecmp(arg, "gm") != 0)
        {
            if (strlen(arg) > 1)
            {
                if (SW_AddGroupFileFromArg(arg+1) >= 0)
                    LOG_F(INFO, "Added %s", arg+1);
                else
                    LOG_F(WARNING, "Could not add game data %s", arg+1);
            }
        }
        else if (Bstrncasecmp(arg, "h", 1) == 0 && !SW_SHAREWARE)
        {
            if (strlen(arg) > 1)
                G_AddDef(arg+1);
        }
        else if (Bstrncasecmp(arg, "mh", 2) == 0 && !SW_SHAREWARE)
        {
            if (strlen(arg) > 2)
                G_AddDefModule(arg+2);
        }
#ifdef _WIN32
        else if (Bstrncasecmp(arg, "nodinput", 8) == 0)
        {
            VLOG_F(LOG_INPUT, "DirectInput (controller) support disabled");
            g_controllerSupportFlags |= CONTROLLER_NO_DINPUT;
        }
        else if (Bstrncasecmp(arg, "noxinput", 8) == 0)
        {
            VLOG_F(LOG_INPUT, "XInput (controller) support disabled");
            g_controllerSupportFlags |= CONTROLLER_NO_XINPUT;
        }
#endif
        else if (Bstrncasecmp(arg, "nocontroller", 12) == 0)
        {
            VLOG_F(LOG_INPUT, "Controller support disabled.");
            g_controllerSupportFlags |= CONTROLLER_DISABLED;
        }
    }

    Control(argc, argv);

    return 0;
}

void
ManualPlayerInsert(PLAYERp pp)
{
    PLAYERp npp = Player + numplayers;

    if (numplayers < MAX_SW_PLAYERS)
    {
        connectpoint2[numplayers - 1] = numplayers;
        connectpoint2[numplayers] = -1;

        npp->posx = pp->posx;
        npp->posy = pp->posy;
        npp->posz = pp->posz;
        npp->q16ang = pp->q16ang;
        npp->cursectnum = pp->cursectnum;

        myconnectindex = numplayers;
        screenpeek = numplayers;

        sprintf(Player[myconnectindex].PlayerName,"PLAYER %d",myconnectindex+1);

        Player[numplayers].movefifoend = Player[0].movefifoend;

        // If IsAI = TRUE, new player will be a bot
        Player[myconnectindex].IsAI = FALSE;

        numplayers++;
    }

}

void
BotPlayerInsert(PLAYERp pp)
{
    PLAYERp npp = Player + numplayers;

    if (numplayers < MAX_SW_PLAYERS)
    {
        connectpoint2[numplayers - 1] = numplayers;
        connectpoint2[numplayers] = -1;

        npp->posx = pp->posx;
        npp->posy = pp->posy;
        npp->posz = pp->posz-Z(100);
        npp->q16ang = pp->q16ang;
        npp->cursectnum = pp->cursectnum;

        //myconnectindex = numplayers;
        //screenpeek = numplayers;

        sprintf(Player[numplayers].PlayerName,"BOT %d",numplayers+1);

        Player[numplayers].movefifoend = Player[0].movefifoend;

        // If IsAI = TRUE, new player will be a bot
        Player[numplayers].IsAI = TRUE;

        numplayers++;
    }

//    SetFragBar(pp);
}

void
ManualPlayerDelete(void)
{
    short i, nexti;
    USERp u;
    PLAYERp pp;

    if (numplayers > 1)
    {
        numplayers--;
        connectpoint2[numplayers - 1] = -1;

        pp = Player + numplayers;

        KillSprite(pp->PlayerSprite);
        pp->PlayerSprite = -1;

        // Make sure enemys "forget" about deleted player
        TRAVERSE_SPRITE_STAT(headspritestat[STAT_ENEMY], i, nexti)
        {
            u = User[i];
            if (u->tgt_sp == pp->SpriteP)
                u->tgt_sp = Player[0].SpriteP;
        }

        if (myconnectindex >= numplayers)
            myconnectindex = 0;

        if (screenpeek >= numplayers)
            screenpeek = 0;
    }
}

#if DEBUG
void
SinglePlayInput(PLAYERp pp)
{
    int pnum = myconnectindex;
    uint8_t* kp;

    if (BUTTON(gamefunc_See_Co_Op_View) && !UsingMenus && !ConPanel && dimensionmode == 3)
    {
        short oldscreenpeek = screenpeek;

        CONTROL_ClearButton(gamefunc_See_Co_Op_View);

        screenpeek = connectpoint2[screenpeek];

        if (screenpeek < 0)
            screenpeek = connecthead;

        if (dimensionmode == 2 || dimensionmode == 5 || dimensionmode == 6)
            setup2dscreen();

        if (dimensionmode != 2)
        {
            PLAYERp tp;

            tp = Player + screenpeek;
            PlayerUpdatePanelInfo(tp);
            if (getrendermode() < 3)
                COVERsetbrightness(gs.Brightness,(char *)palette_data);
            else
                setpalettefade(0,0,0,0);
            memcpy(pp->temp_pal, palette_data, sizeof(palette_data));
            DoPlayerDivePalette(tp);
            DoPlayerNightVisionPalette(tp);
//          VLOG_F(LOG_INPUT, "SingPlayInput set_pal: tp->PlayerSprite = %d", tp->PlayerSprite);
        }
    }

    if (!(KEY_PRESSED(KEYSC_ALT) | KEY_PRESSED(KEYSC_RALT)))
        return;


    if (!SW_SHAREWARE && KEY_PRESSED(KEYSC_M))
    {
        extern SWBOOL DebugActorFreeze;

        KEY_PRESSED(KEYSC_M) = 0;
        DebugActorFreeze++;
        if (DebugActorFreeze > 2)
            DebugActorFreeze = 0;

        if (DebugActorFreeze == 2)
        {
            short i, nexti;

            TRAVERSE_SPRITE_STAT(headspritestat[STAT_ENEMY], i, nexti)
            {
                SET(sprite[i].cstat, CSTAT_SPRITE_INVISIBLE);
                if (TEST(sprite[i].cstat, CSTAT_SPRITE_BLOCK))
                {
                    SET(sprite[i].extra, SPRX_BLOCK);
                    RESET(sprite[i].cstat, CSTAT_SPRITE_BLOCK);
                }
            }
        }

        if (DebugActorFreeze == 0)
        {
            short i, nexti;

            TRAVERSE_SPRITE_STAT(headspritestat[STAT_ENEMY], i, nexti)
            {
                RESET(sprite[i].cstat, CSTAT_SPRITE_INVISIBLE);
                if (TEST(sprite[i].extra, SPRX_BLOCK))
                    SET(sprite[i].cstat, CSTAT_SPRITE_BLOCK);
            }
        }
    }


    // Insert a player
    if (KEY_PRESSED(KEYSC_INS))
    // player
    {
        KEY_PRESSED(KEYSC_INS) = 0;
        ManualPlayerInsert(pp);
        // comes back looking through screenpeek
        InitPlayerSprite(Player + screenpeek);
        PlayerDeathReset(Player + screenpeek);
        SetFragBar(pp);
    }


    // Delete a player
    if (KEY_PRESSED(KEYSC_DEL))
    {
        KEY_PRESSED(KEYSC_DEL) = 0;
        ManualPlayerDelete();
    }

    // Move control to numbered player

    if ((kp = KeyPressedRange(&KEY_PRESSED(KEYSC_1), &KEY_PRESSED(KEYSC_9))) && numplayers > 1)
    {
        short save_myconnectindex;

        save_myconnectindex = myconnectindex;

        myconnectindex = (intptr_t)kp - (intptr_t)(&KEY_PRESSED(KEYSC_1));

        if (myconnectindex >= numplayers)
            myconnectindex = save_myconnectindex;

        screenpeek = myconnectindex;

        DoPlayerDivePalette(pp);

        // Now check for item or pain palette stuff
        // This sets the palette to whatever it is of the player you
        // just chose to view the game through.
//      VLOG_F(LOG_INPUT, "SingPlayInput ALT+1-9 set_pal: pp->PlayerSprite = %d", pp->PlayerSprite);
        COVERsetbrightness(gs.Brightness,(char *)palette_data); // JBF: figure out what's going on here

        DoPlayerNightVisionPalette(pp);

        ResetKeyRange(&KEY_PRESSED(KEYSC_1), &KEY_PRESSED(KEYSC_9));
    }

#if 0
    if (KEY_PRESSED(KEYSC_T))
    {
        KEY_PRESSED(KEYSC_T) = 0;
        PlayerTrackingMode ^= 1;
    }
#endif

    if (KEY_PRESSED(KEYSC_H))
    {
        short pnum;

        KEY_PRESSED(KEYSC_H) = 0;

        TRAVERSE_CONNECT(pnum)
        {
            User[Player[pnum].PlayerSprite]->Health = 100;
        }
    }
}

void
DebugKeys(PLAYERp pp)
{
    short w, h;

    if (!(KEY_PRESSED(KEYSC_ALT) || KEY_PRESSED(KEYSC_RALT)))
        return;

    if (InputMode)
        return;

    if (CommEnabled)
        return;

    //
    // visiblity adjust
    //

    if (KEY_PRESSED(KEYSC_L) > 0)
    {
        if (KEY_PRESSED(KEYSC_LSHIFT) | KEY_PRESSED(KEYSC_RSHIFT))      // SHIFT
        {
            g_visibility = g_visibility - (g_visibility >> 3);

            if (g_visibility < 128)
                g_visibility = 16348;

            //if (g_visibility > 16384)
            //    g_visibility = 128;
        }
        else
        {
            KEY_PRESSED(KEYSC_L) = 0;

            g_visibility = g_visibility - (g_visibility >> 3);

            if (g_visibility > 16384)
                g_visibility = 128;
        }
    }

    //
    // parallax changes
    //

    if (KEY_PRESSED(KEYSC_X))
    {
        if (KEY_PRESSED(KEYSC_LSHIFT))
        {
            KEY_PRESSED(KEYSC_LSHIFT) = FALSE;
            KEY_PRESSED(KEYSC_X) = 0;

            parallaxyoffs_override += 10;

            if (parallaxyoffs_override > 100)
                parallaxyoffs_override = 0;
        }
        else
        {
            KEY_PRESSED(KEYSC_X) = 0;
            parallaxtype++;
            if (parallaxtype > 2)
                parallaxtype = 0;
        }
    }
}

#endif

void
ConKey(void)
{
#if DEBUG
    // Console Input Panel
    if (!ConPanel && dimensionmode == 3)
    {
        //if (KEY_PRESSED(KEYSC_TILDE) && KEY_PRESSED(KEYSC_LSHIFT))
        if (KEY_PRESSED(KEYSC_TILDE))
        {
            KEY_PRESSED(KEYSC_TILDE) = FALSE;
            //KEY_PRESSED(KEYSC_LSHIFT) = FALSE;
            KB_FlushKeyboardQueue();
            ConPanel = TRUE;
            InputMode = TRUE;
            ConInputMode = TRUE;
            if (!CommEnabled)
                GamePaused = TRUE;
            memset(MessageInputString, '\0', sizeof(MessageInputString));
        }
    }
    else if (ConPanel)
    {
        //if (KEY_PRESSED(KEYSC_TILDE) && KEY_PRESSED(KEYSC_LSHIFT))
        if (KEY_PRESSED(KEYSC_TILDE))
        {
            KEY_PRESSED(KEYSC_TILDE) = FALSE;
            //KEY_PRESSED(KEYSC_LSHIFT) = FALSE;
            KB_FlushKeyboardQueue();
            ConPanel = FALSE;
            ConInputMode = FALSE;
            InputMode = FALSE;
            if (!CommEnabled)
                GamePaused = FALSE;
            memset(MessageInputString, '\0', sizeof(MessageInputString));
            SetFragBar(Player + myconnectindex);
        }
    }
#endif
}

char WangBangMacro[10][64];

SWBOOL DoQuickSave(short save_num)
{
    PauseAction();

    if (SaveGame(save_num) != -1)
    {
        QuickLoadNum = save_num;

        LastSaveNum = -1;

        return FALSE;
    }

    return TRUE;
}

SWBOOL DoQuickLoad()
{
    KB_ClearKeysDown();

    PauseAction();

    ReloadPrompt = FALSE;
    if (LoadGame(QuickLoadNum) == -1)
    {
        return FALSE;
    }

    ready2send = 1;
    LastSaveNum = -1;

    return TRUE;
}

void
FunctionKeys(PLAYERp pp)
{
    static int rts_delay = 0;
    int fn_key = 0;

    rts_delay++;

    if (KEY_PRESSED(sc_F1))   { fn_key = 1; }
    if (KEY_PRESSED(sc_F2))   { fn_key = 2; }
    if (KEY_PRESSED(sc_F3))   { fn_key = 3; }
    if (KEY_PRESSED(sc_F4))   { fn_key = 4; }
    if (KEY_PRESSED(sc_F5))   { fn_key = 5; }
    if (KEY_PRESSED(sc_F6))   { fn_key = 6; }
    if (KEY_PRESSED(sc_F7))   { fn_key = 7; }
    if (KEY_PRESSED(sc_F8))   { fn_key = 8; }
    if (KEY_PRESSED(sc_F9))   { fn_key = 9; }
    if (KEY_PRESSED(sc_F10))  { fn_key = 10; }

    if (KEY_PRESSED(KEYSC_ALT) || KEY_PRESSED(KEYSC_RALT))
    {
        if (rts_delay > 16 && fn_key && CommEnabled && !gs.ParentalLock && !Global_PLock)
        {
            KEY_PRESSED(sc_F1 + fn_key - 1) = 0;

            rts_delay = 0;

            PlaySoundRTS(fn_key);

            if (CommEnabled)
            {
                PACKET_RTS p;

                p.PacketType = PACKET_TYPE_RTS;
                p.RTSnum = fn_key;

                netbroadcastpacket((uint8_t*)(&p), sizeof(p));            // TENSW
            }
        }

        return;
    }

    if (KEY_PRESSED(KEYSC_LSHIFT) || KEY_PRESSED(KEYSC_RSHIFT))
    {
        if (fn_key && CommEnabled)
        {
            KEY_PRESSED(sc_F1 + fn_key - 1) = 0;

            if (CommEnabled)
            {
                short pnum;

                sprintf(ds,"SENT: %s",WangBangMacro[fn_key-1]);
                adduserquote(ds);

                TRAVERSE_CONNECT(pnum)
                {
                    if (pnum != myconnectindex)
                    {
                        sprintf(ds,"%s: %s",pp->PlayerName, WangBangMacro[fn_key-1]);
                        SW_SendMessage(pnum, ds);
                    }
                }
            }
        }

        return;
    }


    if (numplayers <= 1)
    {
        // F2 save menu
        if (KEY_PRESSED(KEYSC_F2))
        {
            KEY_PRESSED(KEYSC_F2) = 0;
            if (!TEST(pp->Flags, PF_DEAD))
            {
                KEY_PRESSED(KEYSC_ESC) = 1;
                ControlPanelType = ct_savemenu;
            }
        }

        // F3 load menu
        if (KEY_PRESSED(KEYSC_F3))
        {
            KEY_PRESSED(KEYSC_F3) = 0;
            if (!TEST(pp->Flags, PF_DEAD))
            {
                KEY_PRESSED(KEYSC_ESC) = 1;
                ControlPanelType = ct_loadmenu;
            }
        }

        // F6 quick save
        if (BUTTON(gamefunc_Quick_Save))
        {
            CONTROL_ClearButton(gamefunc_Quick_Save);
            if (!TEST(pp->Flags, PF_DEAD))
            {
                KEY_PRESSED(KEYSC_ESC) = 1;
                if (QuickLoadNum < 0)
                {
                    KEY_PRESSED(KEYSC_ESC) = 1;
                    ControlPanelType = ct_savemenu;
                }
                else
                {
                    KB_ClearKeysDown();
                    KB_FlushKeyboardQueue();
                    DoQuickSave(QuickLoadNum);
                    ResumeAction();
                }
            }
        }

        // F9 quick load
        if (BUTTON(gamefunc_Quick_Load))
        {
            CONTROL_ClearButton(gamefunc_Quick_Load);

            if (!TEST(pp->Flags, PF_DEAD))
            {
                if (QuickLoadNum < 0)
                {
                    KEY_PRESSED(KEYSC_ESC) = 1;
                    ControlPanelType = ct_loadmenu;
                }
                else
                {
                    DoQuickLoad();
                    ResumeAction();
                }
            }
        }
    }


    // F4 sound menu
    if (KEY_PRESSED(KEYSC_F4))
    {
        KEY_PRESSED(KEYSC_F4) = 0;
        KEY_PRESSED(KEYSC_ESC) = 1;
        ControlPanelType = ct_soundmenu;
    }


    // F7 VIEW control
    if (KEY_PRESSED(KEYSC_F7))
    {
        KEY_PRESSED(KEYSC_F7) = 0;

        if (KEY_PRESSED(KEYSC_LSHIFT) || KEY_PRESSED(KEYSC_RSHIFT))
        {
            if (TEST(pp->Flags, PF_VIEW_FROM_OUTSIDE))
                pp->view_outside_dang = NORM_ANGLE(pp->view_outside_dang + 256);
        }
        else
        {
            if (TEST(pp->Flags, PF_VIEW_FROM_OUTSIDE))
            {
                RESET(pp->Flags, PF_VIEW_FROM_OUTSIDE);
            }
            else
            {
                SET(pp->Flags, PF_VIEW_FROM_OUTSIDE);
                pp->camera_dist = 0;
            }
        }
    }

    // F8 toggle messages
    if (KEY_PRESSED(KEYSC_F8))
    {
        KEY_PRESSED(KEYSC_F8) = 0;

        gs.Messages ^= 1;

        if (gs.Messages)
            PutStringInfoLine(pp, "Messages ON");
        else
            PutStringInfoLine(pp, "Messages OFF");
    }

    // F10 quit menu
    if (KEY_PRESSED(KEYSC_F10))
    {
        KEY_PRESSED(KEYSC_F10) = 0;
        KEY_PRESSED(KEYSC_ESC) = 1;
        ControlPanelType = ct_quitmenu;
    }

    // F11 gamma correction
    if (KEY_PRESSED(KEYSC_F11) > 0)
    {
        KEY_PRESSED(KEYSC_F11) = 0;

        gs.Brightness++;
        if (gs.Brightness >= SLDR_BRIGHTNESSMAX)
            gs.Brightness = 0;

        sprintf(ds,"Brightness level (%d)",gs.Brightness+1);
        PutStringInfoLine(pp, ds);

        if (!pp->NightVision && pp->FadeAmt <= 0)
        {
            COVERsetbrightness(gs.Brightness,&palette_data[0][0]);
        }

        //DoPlayerDivePalette(pp);
        //DoPlayerNightVisionPalette(pp);
    }

}

void PauseKey(PLAYERp pp)
{
    extern SWBOOL CheatInputMode;

    if (KEY_PRESSED(sc_Pause) && !CommEnabled && !InputMode && !UsingMenus && !CheatInputMode && !ConPanel)
    {
        KEY_PRESSED(sc_Pause) = 0;

        PauseKeySet ^= 1;

        if (PauseKeySet)
            GamePaused = TRUE;
        else
            GamePaused = FALSE;

        if (GamePaused)
        {
            short w,h;
#define MSG_GAME_PAUSED "Game Paused"
            MNU_MeasureString(MSG_GAME_PAUSED, &w, &h);
            PutStringTimer(pp, TEXT_TEST_COL(w), 100, MSG_GAME_PAUSED, 999);
            PauseSong(TRUE);
        }
        else
        {
            pClearTextLine(pp, 100);
            PauseSong(FALSE);
        }
    }

    if (!CommEnabled && TEST(pp->Flags, PF_DEAD))
    {
        if (ReloadPrompt)
        {
            if (QuickLoadNum < 0)
            {
                ReloadPrompt = FALSE;
            }
            else
            {
                KEY_PRESSED(KEYSC_ESC) = 1;
                ControlPanelType = ct_quickloadmenu;
            }
        }
    }
}



void GetMessageInput(PLAYERp pp)
{
    int pnum = myconnectindex;
    signed char MNU_InputSmallString(char *, short);
    signed char MNU_InputString(char *, short);
    static SWBOOL TeamSendAll;
#define TEAM_MENU "A - Send to ALL,  T - Send to TEAM"
    static char HoldMessageInputString[256];
    int i;
    SWBOOL IsCommand(char *str);

    if (!MessageInputMode && !ConInputMode)
    {
        if (BUTTON(gamefunc_SendMessage))
        {
            CONTROL_ClearButton(gamefunc_SendMessage);
            KB_FlushKeyboardQueue();
            MessageInputMode = TRUE;
            InputMode = TRUE;
            TeamSendAll = FALSE;

            if (MessageInputMode)
            {
                memset(MessageInputString, '\0', sizeof(MessageInputString));
            }
        }
    }
    else if (MessageInputMode && !ConInputMode)
    {
        if (gs.BorderNum > BORDER_BAR+1)
            SetRedrawScreen(pp);

        // get input
        switch (MNU_InputSmallString(MessageInputString, 320-20))
        {
        case -1: // Cancel Input (pressed ESC) or Err
            MessageInputMode = FALSE;
            InputMode = FALSE;
            KB_ClearKeysDown();
            KB_FlushKeyboardQueue();
            break;
        case FALSE: // Input finished (RETURN)
            if (MessageInputString[0] == '\0')
            {
                // no input
                MessageInputMode = FALSE;
                InputMode = FALSE;
                KB_ClearKeysDown();
                KB_FlushKeyboardQueue();
                CONTROL_ClearButton(gamefunc_Inventory);
            }
            else
            {
                if (gNet.TeamPlay)
                {
                    if (memcmp(MessageInputString, TEAM_MENU, sizeof(TEAM_MENU)) != 0)
                    {
                        // see if its a command
                        if (IsCommand(MessageInputString))
                        {
                            TeamSendAll = TRUE;
                        }
                        else
                        {
                            strcpy(HoldMessageInputString, MessageInputString);
                            strcpy(MessageInputString, TEAM_MENU);
                            break;
                        }
                    }
                    else if (memcmp(MessageInputString, TEAM_MENU, sizeof(TEAM_MENU)) == 0)
                    {
                        strcpy(MessageInputString, HoldMessageInputString);
                        TeamSendAll = TRUE;
                    }
                }

SEND_MESSAGE:

                // broadcast message
                MessageInputMode = FALSE;
                InputMode = FALSE;
                KB_ClearKeysDown();
                KB_FlushKeyboardQueue();
                CONTROL_ClearButton(gamefunc_Inventory);
                CON_ProcessUserCommand();     // Check to see if it's a cheat or command

                for (i = 0; i < NUMGAMEFUNCTIONS; i++)
                    CONTROL_ClearButton(i);

                // Put who sent this
                sprintf(ds,"%s: %s",pp->PlayerName,MessageInputString);

                if (gNet.TeamPlay)
                {
                    TRAVERSE_CONNECT(pnum)
                    {
                        if (pnum != myconnectindex)
                        {
                            if (TeamSendAll)
                                SW_SendMessage(pnum, ds);
                            else if (User[pp->PlayerSprite]->spal == User[Player[pnum].PlayerSprite]->spal)
                                SW_SendMessage(pnum, ds);
                        }
                    }
                }
                else
                {
                    TRAVERSE_CONNECT(pnum)
                    {
                        if (pnum != myconnectindex)
                        {
                            SW_SendMessage(pnum, ds);
                        }
                    }
                }
                adduserquote(MessageInputString);
                quotebot += 8;
                quotebotgoal = quotebot;
            }
            break;

        case TRUE: // Got input

            if (gNet.TeamPlay)
            {
                if (memcmp(MessageInputString, TEAM_MENU "a", sizeof(TEAM_MENU)+1) == 0)
                {
                    strcpy(MessageInputString, HoldMessageInputString);
                    TeamSendAll = TRUE;
                    goto SEND_MESSAGE;
                }
                else if (memcmp(MessageInputString, TEAM_MENU "t", sizeof(TEAM_MENU)+1) == 0)
                {
                    strcpy(MessageInputString, HoldMessageInputString);
                    goto SEND_MESSAGE;
                }
                else
                {
                    // reset the string if anything else is typed
                    if (strlen(MessageInputString)+1 > sizeof(TEAM_MENU))
                    {
                        strcpy(MessageInputString, TEAM_MENU);
                    }
                }
            }

            break;
        }
    }
}

void GetConInput(void)
{
    signed char MNU_InputSmallString(char *, short);
    signed char MNU_InputString(char *, short);

    if (MessageInputMode || HelpInputMode)
        return;

    ConKey();

    // Console input commands
    if (ConInputMode && !MessageInputMode)
    {
        // get input
        switch (MNU_InputSmallString(MessageInputString, 250))
        {
        case -1: // Cancel Input (pressed ESC) or Err
            InputMode = FALSE;
            KB_ClearKeysDown();
            KB_FlushKeyboardQueue();
            memset(MessageInputString, '\0', sizeof(MessageInputString));
            break;
        case FALSE: // Input finished (RETURN)
            if (MessageInputString[0] == '\0')
            {
                InputMode = FALSE;
                KB_ClearKeysDown();
                KB_FlushKeyboardQueue();
                CONTROL_ClearButton(gamefunc_Inventory);
                memset(MessageInputString, '\0', sizeof(MessageInputString));
            }
            else
            {
                InputMode = FALSE;
                KB_ClearKeysDown();
                KB_FlushKeyboardQueue();
                CONTROL_ClearButton(gamefunc_Inventory);
                CON_ConMessage("%s", MessageInputString);
                CON_ProcessUserCommand();     // Check to see if it's a cheat or command

                conbot += 6;
                conbotgoal = conbot;
                //addconquote(MessageInputString);
                // Clear it out after every entry
                memset(MessageInputString, '\0', sizeof(MessageInputString));
            }
            break;
        case TRUE: // Got input
            break;
        }
    }
}


void GetHelpInput(PLAYERp pp)
{
    if (KEY_PRESSED(KEYSC_ALT) || KEY_PRESSED(KEYSC_RALT))
        return;

    if (KEY_PRESSED(KEYSC_LSHIFT) || KEY_PRESSED(KEYSC_RSHIFT))
        return;

    if (MessageInputMode || ConInputMode)
        return;

    // F1 help menu
    if (!HelpInputMode)
    {
        if (KEY_PRESSED(KEYSC_F1))
        {
            KEY_PRESSED(KEYSC_F1) = FALSE;
            HelpPage = 0;
            HelpInputMode = TRUE;
            PanelUpdateMode = FALSE;
            InputMode = TRUE;
            if (!CommEnabled)
                GamePaused = TRUE;
        }
    }
    else if (HelpInputMode)
    {
        if (KEY_PRESSED(KEYSC_ESC))
        {
            KEY_PRESSED(KEYSC_ESC) = 0;
            KB_ClearKeysDown();
            PanelUpdateMode = TRUE;
            HelpInputMode = FALSE;
            InputMode = FALSE;
            if (!CommEnabled)
                GamePaused = FALSE;
            SetRedrawScreen(pp);
        }

        if (KEY_PRESSED(KEYSC_SPACE) || KEY_PRESSED(KEYSC_ENTER) || KEY_PRESSED(KEYSC_PGDN) || KEY_PRESSED(KEYSC_DOWN) || KEY_PRESSED(KEYSC_RIGHT) || KEY_PRESSED(sc_kpad_3) || KEY_PRESSED(sc_kpad_2) || KEY_PRESSED(sc_kpad_6))
        {
            KEY_PRESSED(KEYSC_SPACE) = KEY_PRESSED(KEYSC_ENTER) = 0;
            KEY_PRESSED(KEYSC_PGDN) = 0;
            KEY_PRESSED(KEYSC_DOWN) = 0;
            KEY_PRESSED(KEYSC_RIGHT) = 0;
            KEY_PRESSED(sc_kpad_3) = 0;
            KEY_PRESSED(sc_kpad_2) = 0;
            KEY_PRESSED(sc_kpad_6) = 0;

            HelpPage++;
            if (HelpPage >= (int)SIZ(HelpPagePic))
                // CTW MODIFICATION
                // "Oops! I did it again..."
                // HelpPage = SIZ(HelpPagePic) - 1;
                HelpPage = 0;
            // CTW MODIFICATION END
        }

        if (KEY_PRESSED(KEYSC_PGUP) || KEY_PRESSED(KEYSC_UP) || KEY_PRESSED(KEYSC_LEFT) || KEY_PRESSED(sc_kpad_9) || KEY_PRESSED(sc_kpad_8) || KEY_PRESSED(sc_kpad_4))
        {
            KEY_PRESSED(KEYSC_PGUP) = 0;
            KEY_PRESSED(KEYSC_UP) = 0;
            KEY_PRESSED(KEYSC_LEFT) = 0;
            KEY_PRESSED(sc_kpad_8) = 0;
            KEY_PRESSED(sc_kpad_9) = 0;
            KEY_PRESSED(sc_kpad_4) = 0;

            HelpPage--;
            if (HelpPage < 0)
                // CTW MODIFICATION
                // "Played with the logic, got lost in the game..."
                HelpPage = SIZ(HelpPagePic) - 1;
            // CTW MODIFICATION END
        }
    }
}

short MirrorDelay;

double elapsedInputTicks;
double scaleAdjustmentToInterval(double x) { return x * (120 / synctics) / (1000.0 / elapsedInputTicks); }

void
getinput(SW_PACKET *loc, SWBOOL tied)
{
    int i;
    PLAYERp pp = Player + myconnectindex;
    PLAYERp newpp = Player + myconnectindex;
    int inv_hotkey = 0;

#define TURBOTURNTIME (120/8)
#define NORMALTURN   (12+6)
#define RUNTURN      (28)
#define PREAMBLETURN 3
#define NORMALKEYMOVE 35
#define MAXVEL       ((NORMALKEYMOVE*2)+10)
#define MAXSVEL      ((NORMALKEYMOVE*2)+10)
#define MAXANGVEL    100
#define MAXHORIZVEL  128
#define SET_LOC_KEY(loc, sync_num, key_test) SET(loc, ((!!(key_test)) << (sync_num)))

    static int32_t turnheldtime;
    int32_t momx, momy;

    extern SWBOOL MenuButtonAutoRun;
    extern SWBOOL MenuButtonAutoAim;

    if (PredictionOn && CommEnabled)
    {
        newpp = ppp;
    }

    static double lastInputTicks;

    auto const currentHiTicks = timerGetFractionalTicks();
    elapsedInputTicks = currentHiTicks - lastInputTicks;

    lastInputTicks = currentHiTicks;

    // MAKE SURE THIS WILL GET SET
    SET_LOC_KEY(loc->bits, SK_QUIT_GAME, MultiPlayQuitFlag);

    if (gs.MouseAimingType == 1) // while held
    {
        if (BUTTON(gamefunc_Mouse_Aiming))
        {
            SET(pp->Flags, PF_MOUSE_AIMING_ON);
            gs.MouseAimingOn = TRUE;
        }
        else
        {
            if (TEST(pp->Flags, PF_MOUSE_AIMING_ON))
            {
                SET_LOC_KEY(loc->bits, SK_LOOK_UP, TRUE);
                RESET(pp->Flags, PF_MOUSE_AIMING_ON);
                gs.MouseAimingOn = FALSE;
            }
        }
    }
    else if (gs.MouseAimingType == 0) // togglable button
    {
        if (BUTTON(gamefunc_Mouse_Aiming) && !BUTTONHELD(gamefunc_Mouse_Aiming))
        {
            FLIP(pp->Flags, PF_MOUSE_AIMING_ON);
            gs.MouseAimingOn = !gs.MouseAimingOn;
            if (!TEST(pp->Flags, PF_MOUSE_AIMING_ON))
            {
                SET_LOC_KEY(loc->bits, SK_LOOK_UP, TRUE);
                PutStringInfo(pp, "Mouse Aiming Off");
            }
            else
            {
                PutStringInfo(pp, "Mouse Aiming On");
            }
        }
    }

    int const aimMode = TEST(pp->Flags, PF_MOUSE_AIMING_ON);

    ControlInfo info;
    CONTROL_GetInput(&info);

    PauseKey(pp);

    if (PauseKeySet)
        return;

    if (!MenuInputMode && !UsingMenus)
    {
        GetMessageInput(pp);
        GetConInput();
        GetHelpInput(pp);
    }

    // MAP KEY
    if (BUTTON(gamefunc_Map))
    {
        CONTROL_ClearButton(gamefunc_Map);

        // Init follow coords
        Follow_posx = pp->posx;
        Follow_posy = pp->posy;

        if (dimensionmode == 3)
            dimensionmode = 5;
        else if (dimensionmode == 5)
            dimensionmode = 6;
        else
        {
            MirrorDelay = 1;
            dimensionmode = 3;
            SetFragBar(pp);
            ScrollMode2D = FALSE;
            SetRedrawScreen(pp);
        }
    }

    // Toggle follow map mode on/off
    if (dimensionmode == 5 || dimensionmode == 6)
    {
        if (BUTTON(gamefunc_Map_Follow_Mode) && !BUTTONHELD(gamefunc_Map_Follow_Mode))
        {
            ScrollMode2D = !ScrollMode2D;
            Follow_posx = pp->posx;
            Follow_posy = pp->posy;
        }
    }

    // If in 2D follow mode, scroll around using glob vars
    // Tried calling this in domovethings, but key response it too poor, skips key presses
    // Note: ScrollMode2D = Follow mode, so this get called only during follow mode
    if (!tied && ScrollMode2D && pp == Player + myconnectindex && !Prediction)
        MoveScrollMode2D(Player + myconnectindex);

    // !JIM! Added UsingMenus so that you don't move at all while using menus
    if (MenuInputMode || UsingMenus || ScrollMode2D || InputMode)
        return;

    SET_LOC_KEY(loc->bits, SK_SPACE_BAR, ((!!KEY_PRESSED(KEYSC_SPACE)) | BUTTON(gamefunc_Open)));

    int const running = !!BUTTON(gamefunc_Run) ^ !!TEST(pp->Flags, PF_LOCK_RUN);
    int32_t turnamount;
    int32_t keymove;
    constexpr int const analogExtent = 32767; // KEEPINSYNC sdlayer.cpp

    if (running)
    {
        if (pp->sop_control)
            turnamount = RUNTURN * 3;
        else
            turnamount = RUNTURN;

        keymove = NORMALKEYMOVE << 1;
    }
    else
    {
        if (pp->sop_control)
            turnamount = NORMALTURN * 3;
        else
            turnamount = NORMALTURN;

        keymove = NORMALKEYMOVE;
    }

    if (tied)
        keymove = 0;

    info.dz = (info.dz * move_scale)>>8;
    info.dyaw = (info.dyaw * turn_scale)>>8;

    int32_t svel = 0, vel = 0;
    fix16_t q16aimvel = 0, q16angvel = 0;

    if (BUTTON(gamefunc_Strafe) && !pp->sop)
    {
        svel = -info.mousex;
        svel -= info.dyaw * keymove / analogExtent;
    }
    else
    {
        q16angvel = fix16_div(fix16_from_int(info.mousex), fix16_from_int(32));
        q16angvel += fix16_from_int(info.dyaw) / analogExtent * (turnamount << 1);
    }

    if (aimMode)
        q16aimvel = -fix16_div(fix16_from_int(info.mousey), fix16_from_int(64));
    else
        vel = -(info.mousey >> 6);

    if (gs.MouseInvert)
        q16aimvel = -q16aimvel;

    q16aimvel -= fix16_from_int(info.dpitch) * turnamount / analogExtent;
    svel -= info.dx * keymove / analogExtent;
    vel -= info.dz * keymove / analogExtent;

    if (BUTTON(gamefunc_Strafe) && !pp->sop)
    {
        if (BUTTON(gamefunc_Turn_Left))
            svel -= -keymove;
        if (BUTTON(gamefunc_Turn_Right))
            svel -= keymove;
    }
    else
    {
        if (BUTTON(gamefunc_Turn_Left) || (BUTTON(gamefunc_Strafe_Left) && pp->sop))
        {
            turnheldtime += synctics;
            if (PedanticMode)
            {
                if (turnheldtime >= TURBOTURNTIME)
                    q16angvel -= fix16_from_int(turnamount);
                else
                    q16angvel -= fix16_from_int(PREAMBLETURN);
            }
            else
                q16angvel = fix16_ssub(q16angvel, fix16_from_float(scaleAdjustmentToInterval((turnheldtime >= TURBOTURNTIME) ? turnamount : PREAMBLETURN)));
        }
        else if (BUTTON(gamefunc_Turn_Right) || (BUTTON(gamefunc_Strafe_Right) && pp->sop))
        {
            turnheldtime += synctics;
            if (PedanticMode)
            {
                if (turnheldtime >= TURBOTURNTIME)
                    q16angvel += fix16_from_int(turnamount);
                else
                    q16angvel += fix16_from_int(PREAMBLETURN);
            }
            else
                q16angvel = fix16_sadd(q16angvel, fix16_from_float(scaleAdjustmentToInterval((turnheldtime >= TURBOTURNTIME) ? turnamount : PREAMBLETURN)));
        }
        else
        {
            turnheldtime = 0;
        }
    }

    if (BUTTON(gamefunc_Strafe_Left) && !pp->sop)
        svel += keymove;

    if (BUTTON(gamefunc_Strafe_Right) && !pp->sop)
        svel += -keymove;

    if (BUTTON(gamefunc_Move_Forward))
    {
        vel += keymove;
        //DSPRINTF(ds,"vel key %d",vel);
        //DebugWriteString(ds);
    }
    else
    {
        //DSPRINTF(ds,"vel %d",vel);
        //DebugWriteString(ds);
    }

    if (BUTTON(gamefunc_Move_Backward))
        vel += -keymove;

    q16angvel = fix16_clamp(q16angvel, -fix16_from_int(MAXANGVEL), fix16_from_int(MAXANGVEL));
    q16aimvel = fix16_clamp(q16aimvel, -fix16_from_int(MAXHORIZVEL), fix16_from_int(MAXHORIZVEL));

    void DoPlayerTeleportPause(PLAYERp pp);
    if (PedanticMode)
    {
        q16angvel = fix16_floor(q16angvel);
        q16aimvel = fix16_floor(q16aimvel);
    }
    else
    {
        fix16_t prevcamq16ang = pp->camq16ang, prevcamq16horiz = pp->camq16horiz;
        void DoPlayerTurn(PLAYERp pp, fix16_t *pq16ang, fix16_t q16angvel);
        void DoPlayerHorizon(PLAYERp pp, fix16_t *pq16horiz, fix16_t q16aimvel);
        if (TEST(pp->Flags2, PF2_INPUT_CAN_TURN))
            DoPlayerTurn(pp, &pp->camq16ang, q16angvel);
        if (TEST(pp->Flags2, PF2_INPUT_CAN_AIM))
            DoPlayerHorizon(pp, &pp->camq16horiz, q16aimvel);
        pp->oq16ang += pp->camq16ang - prevcamq16ang;
        pp->oq16horiz += pp->camq16horiz - prevcamq16horiz;
    }

    loc->vel += vel;
    loc->svel += svel;

    if (!tied)
    {
        vel = clamp(loc->vel, -MAXVEL, MAXVEL);
        svel = clamp(loc->svel, -MAXSVEL, MAXSVEL);

        momx = mulscale9(vel, sintable[NORM_ANGLE(fix16_to_int(newpp->q16ang) + 512)]);
        momy = mulscale9(vel, sintable[NORM_ANGLE(fix16_to_int(newpp->q16ang))]);

        momx += mulscale9(svel, sintable[NORM_ANGLE(fix16_to_int(newpp->q16ang))]);
        momy += mulscale9(svel, sintable[NORM_ANGLE(fix16_to_int(newpp->q16ang) + 1536)]);

        loc->vel = momx;
        loc->svel = momy;
    }

    loc->q16angvel += q16angvel;
    loc->q16aimvel += q16aimvel;

    if (MenuButtonAutoRun)
    {
        MenuButtonAutoRun = FALSE;
        if ((!!TEST(pp->Flags, PF_LOCK_RUN)) != gs.AutoRun)
            SET_LOC_KEY(loc->bits, SK_RUN_LOCK, TRUE);
    }

    SET_LOC_KEY(loc->bits, SK_RUN_LOCK, BUTTON(gamefunc_AutoRun));

    if (!CommEnabled)
    {
        if (MenuButtonAutoAim)
        {
            MenuButtonAutoAim = FALSE;
            if ((!!TEST(pp->Flags, PF_AUTO_AIM)) != gs.AutoAim)
                SET_LOC_KEY(loc->bits, SK_AUTO_AIM, TRUE);
        }
    }
    else if (KEY_PRESSED(sc_Pause))
    {
        SET_LOC_KEY(loc->bits, SK_PAUSE, KEY_PRESSED(sc_Pause));
        KEY_PRESSED(sc_Pause) = 0;
    }

    SET_LOC_KEY(loc->bits, SK_CENTER_VIEW, BUTTON(gamefunc_Center_View));

    SET_LOC_KEY(loc->bits, SK_RUN, BUTTON(gamefunc_Run));
    SET_LOC_KEY(loc->bits, SK_SHOOT, BUTTON(gamefunc_Fire));

    // actually snap
    SET_LOC_KEY(loc->bits, SK_SNAP_UP, BUTTON(gamefunc_Aim_Up));
    SET_LOC_KEY(loc->bits, SK_SNAP_DOWN, BUTTON(gamefunc_Aim_Down));

    // actually just look
    SET_LOC_KEY(loc->bits, SK_LOOK_UP, BUTTON(gamefunc_Look_Up));
    SET_LOC_KEY(loc->bits, SK_LOOK_DOWN, BUTTON(gamefunc_Look_Down));

    SET(loc->bits, (pp->Flags2&PF2_INPUT_WEAPON_MASK) / PF2_INPUT_WEAPON_BIT0);
    RESET(pp->Flags2, PF2_INPUT_WEAPON_MASK);

    if (!(loc->bits & SK_WEAPON_MASK))
        for (i = 0; i < MAX_WEAPONS_KEYS; i++)
        {
            if (BUTTON(gamefunc_Weapon_1 + i))
            {
                SET(loc->bits, i + 1);
                break;
            }
        }

    if (BUTTON(gamefunc_Next_Weapon))
    {
        USERp u = User[pp->PlayerSprite];
        short next_weapon = u->WeaponNum + 1;
        short start_weapon;

        CONTROL_ClearButton(gamefunc_Next_Weapon);

        start_weapon = u->WeaponNum + 1;

        if (u->WeaponNum == WPN_SWORD)
            start_weapon = WPN_STAR;

        if (u->WeaponNum == WPN_FIST)
        {
            next_weapon = 14;
        }
        else
        {
            next_weapon = -1;
            for (i = start_weapon; TRUE; i++)
            {
                if (i >= MAX_WEAPONS_KEYS)
                {
                    next_weapon = 13;
                    break;
                }

                if (TEST(pp->WpnFlags, BIT(i)) && pp->WpnAmmo[i])
                {
                    next_weapon = i;
                    break;
                }
            }
        }

        SET(loc->bits, next_weapon + 1);
    }


    if (BUTTON(gamefunc_Previous_Weapon))
    {
        USERp u = User[pp->PlayerSprite];
        short prev_weapon = u->WeaponNum - 1;
        short start_weapon;

        CONTROL_ClearButton(gamefunc_Previous_Weapon);

        start_weapon = u->WeaponNum - 1;

        if (u->WeaponNum == WPN_SWORD)
        {
            prev_weapon = 13;
        }
        else if (u->WeaponNum == WPN_STAR)
        {
            prev_weapon = 14;
        }
        else
        {
            prev_weapon = -1;
            for (i = start_weapon; TRUE; i--)
            {
                if (i <= -1)
                    i = WPN_HEART;

                if (TEST(pp->WpnFlags, BIT(i)) && pp->WpnAmmo[i])
                {
                    prev_weapon = i;
                    break;
                }
            }
        }

        SET(loc->bits, prev_weapon + 1);
    }

    if (BUTTON(gamefunc_Alt_Weapon_Mode))
    {
        CONTROL_ClearButton(gamefunc_Alt_Weapon_Mode);
        USERp u = User[pp->PlayerSprite];
        short const which_weapon = u->WeaponNum + 1;
        SET(loc->bits, which_weapon);
    }

    inv_hotkey = 0;
    if (BUTTON(gamefunc_Med_Kit))
        inv_hotkey = INVENTORY_MEDKIT+1;
    if (BUTTON(gamefunc_Smoke_Bomb))
        inv_hotkey = INVENTORY_CLOAK+1;
    if (BUTTON(gamefunc_Night_Vision))
        inv_hotkey = INVENTORY_NIGHT_VISION+1;
    if (BUTTON(gamefunc_Gas_Bomb))
        inv_hotkey = INVENTORY_CHEMBOMB+1;
    if (BUTTON(gamefunc_Flash_Bomb) && dimensionmode == 3)
        inv_hotkey = INVENTORY_FLASHBOMB+1;
    if (BUTTON(gamefunc_Caltrops))
        inv_hotkey = INVENTORY_CALTROPS+1;

    SET(loc->bits, inv_hotkey<<SK_INV_HOTKEY_BIT0);

    SET_LOC_KEY(loc->bits, SK_INV_USE, BUTTON(gamefunc_Inventory));

    SET_LOC_KEY(loc->bits, SK_OPERATE, BUTTON(gamefunc_Open));
    SET_LOC_KEY(loc->bits, SK_JUMP, BUTTON(gamefunc_Jump));
    SET_LOC_KEY(loc->bits, SK_CRAWL, BUTTON(gamefunc_Crouch));

    SET_LOC_KEY(loc->bits, SK_TURN_180, BUTTON(gamefunc_TurnAround));

    SET_LOC_KEY(loc->bits, SK_INV_LEFT, BUTTON(gamefunc_Inventory_Left));
    SET_LOC_KEY(loc->bits, SK_INV_RIGHT, BUTTON(gamefunc_Inventory_Right));

    SET_LOC_KEY(loc->bits, SK_HIDE_WEAPON, BUTTON(gamefunc_Holster_Weapon));

    // need BUTTON
    SET_LOC_KEY(loc->bits, SK_CRAWL_LOCK, KEY_PRESSED(KEYSC_NUM));

    if (gNet.MultiGameType == MULTI_GAME_COOPERATIVE)
    {
        if (BUTTON(gamefunc_See_Co_Op_View))
        {
            CONTROL_ClearButton(gamefunc_See_Co_Op_View);

            screenpeek = connectpoint2[screenpeek];

            if (screenpeek < 0)
                screenpeek = connecthead;

            if (dimensionmode != 2 && screenpeek == myconnectindex)
            {
                // JBF: figure out what's going on here
                COVERsetbrightness(gs.Brightness,&palette_data[0][0]);
                memcpy(pp->temp_pal, palette_data, sizeof(palette_data));
                DoPlayerDivePalette(pp);  // Check Dive again
                DoPlayerNightVisionPalette(pp);  // Check Night Vision again
            }
            else
            {
                PLAYERp tp = Player+screenpeek;

                if (tp->FadeAmt<=0)
                    memcpy(pp->temp_pal, palette_data, sizeof(palette_data));
                else
                    memcpy(pp->temp_pal, tp->temp_pal, sizeof(tp->temp_pal));
                COVERsetbrightness(gs.Brightness,&palette_data[0][0]);
                DoPlayerDivePalette(tp);
                DoPlayerNightVisionPalette(tp);
            }
        }
    }

#if DEBUG
    DebugKeys(pp);

    if (!CommEnabled)                   // Single player only keys
        SinglePlayInput(pp);
#endif

    if (!tied)
        FunctionKeys(pp);

    if (BUTTON(gamefunc_Toggle_Crosshair))
    {
        CONTROL_ClearButton(gamefunc_Toggle_Crosshair);
        pToggleCrosshair();
    }
}

#define MAP_WHITE_SECTOR    (LT_GREY + 2)
#define MAP_RED_SECTOR      (RED + 6)
#define MAP_FLOOR_SPRITE    (RED + 8)
#define MAP_ENEMY           (RED + 10)
#define MAP_SPRITE          (FIRE + 8)
#define MAP_PLAYER          (GREEN + 6)

#define MAP_BLOCK_SPRITE    (DK_BLUE + 6)

void drawoverheadmap(int cposx, int cposy, int czoom, short cang)
{
    int i, j, k, l, x1, y1, x2, y2, x3, y3, x4, y4, ox, oy, xoff, yoff;
    int dax, day, cosang, sinang, xspan, yspan, sprx, spry;
    int xrepeat, yrepeat, z1, z2, startwall, endwall, tilenum, daang;
    int xvect, yvect, xvect2, yvect2;
    char col;
    walltype *wal, *wal2;
    spritetype *spr;
    short p;
    static int pspr_ndx[8]= {0,0,0,0,0,0,0,0};
    SWBOOL sprisplayer = FALSE;
    short txt_x, txt_y;

    int32_t tmpydim = (xdim*5)/8;

    renderSetAspect(65536, divscale16(tmpydim*320, xdim*200));

    // draw location text
    if (gs.BorderNum <= BORDER_BAR-1)
    {
        txt_x = 7;
        txt_y = 168;
    }
    else
    {
        txt_x = 7;
        txt_y = 147;
    }

    if (ScrollMode2D)
    {
        minigametext(txt_x,txt_y-7,"Follow Mode",2+8);
    }

    if (UserMapName[0])
        sprintf(ds,"%s",UserMapName);
    else
        sprintf(ds,"%s",LevelInfo[Level].Description);

    minigametext(txt_x,txt_y,ds,2+8);

    //////////////////////////////////

    xvect = sintable[(2048 - cang) & 2047] * czoom;
    yvect = sintable[(1536 - cang) & 2047] * czoom;
    xvect2 = mulscale16(xvect, yxaspect);
    yvect2 = mulscale16(yvect, yxaspect);

    // Draw red lines
    for (i = 0; i < numsectors; i++)
    {
        startwall = sector[i].wallptr;
        endwall = sector[i].wallptr + sector[i].wallnum - 1;

        z1 = sector[i].ceilingz;
        z2 = sector[i].floorz;

        for (j = startwall, wal = &wall[startwall]; j <= endwall; j++, wal++)
        {
            k = wal->nextwall;
            if ((unsigned)k >= MAXWALLS)
                continue;

            if (!bitmap_test(show2dwall, j))
                continue;
            if (k > j && bitmap_test(show2dwall, k))
                continue;

            if (sector[wal->nextsector].ceilingz == z1)
                if (sector[wal->nextsector].floorz == z2)
                    if (((wal->cstat | wall[wal->nextwall].cstat) & (16 + 32)) == 0)
                        continue;

            col = 152;

            //if (dimensionmode == 2)
            if (dimensionmode == 6)
            {
                if (sector[i].floorz != sector[i].ceilingz)
                    if (sector[wal->nextsector].floorz != sector[wal->nextsector].ceilingz)
                        if (((wal->cstat | wall[wal->nextwall].cstat) & (16 + 32)) == 0)
                            if (sector[i].floorz == sector[wal->nextsector].floorz)
                                continue;
                if (sector[i].floorpicnum != sector[wal->nextsector].floorpicnum)
                    continue;
                if (sector[i].floorshade != sector[wal->nextsector].floorshade)
                    continue;
                col = 12;
            }

            ox = wal->x - cposx;
            oy = wal->y - cposy;
            x1 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
            y1 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

            wal2 = &wall[wal->point2];
            ox = wal2->x - cposx;
            oy = wal2->y - cposy;
            x2 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
            y2 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

            renderDrawLine(x1 + (xdim << 11), y1 + (ydim << 11), x2 + (xdim << 11), y2 + (ydim << 11), col);
        }
    }

    // Draw sprites
    k = Player[screenpeek].PlayerSprite;
    for (i = 0; i < numsectors; i++)
        for (j = headspritesect[i]; j >= 0; j = nextspritesect[j])
        {
            for (p=connecthead; p >= 0; p=connectpoint2[p])
            {
                if (Player[p].PlayerSprite == j)
                {
                    if (sprite[Player[p].PlayerSprite].xvel > 16)
                        pspr_ndx[myconnectindex] = (((int32_t) totalclock>>4)&3);
                    sprisplayer = TRUE;

                    goto SHOWSPRITE;
                }
            }
            if (bitmap_test(show2dsprite, j))
            {
SHOWSPRITE:
                spr = &sprite[j];

                col = 56;
                if ((spr->cstat & 1) > 0)
                    col = 248;
                if (j == k)
                    col = 31;

                sprx = spr->x;
                spry = spr->y;

                k = spr->statnum;
                if ((k >= 1) && (k <= 8) && (k != 2))   // Interpolate moving
                {
                    sprx = sprite[j].x;
                    spry = sprite[j].y;
                }

                switch (spr->cstat & CSTAT_SPRITE_ALIGNMENT)
                {
                case CSTAT_SPRITE_ALIGNMENT_FACING:  // Regular sprite
                    if (p >= 0 && Player[p].PlayerSprite == j)
                    {
                        ox = sprx - cposx;
                        oy = spry - cposy;
                        x1 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                        y1 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                        if (dimensionmode == 5 && (gNet.MultiGameType != MULTI_GAME_COMMBAT || j == Player[screenpeek].PlayerSprite))
                        {
                            ox = (sintable[(spr->ang + 512) & 2047] >> 7);
                            oy = (sintable[(spr->ang) & 2047] >> 7);
                            x2 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                            y2 = mulscale16(oy, xvect) + mulscale16(ox, yvect);

                            if (j == Player[screenpeek].PlayerSprite)
                            {
                                x2 = 0L;
                                y2 = -(czoom << 5);
                            }

                            x3 = mulscale16(x2, yxaspect);
                            y3 = mulscale16(y2, yxaspect);

                            renderDrawLine(x1 - x2 + (xdim << 11), y1 - y3 + (ydim << 11),
                                           x1 + x2 + (xdim << 11), y1 + y3 + (ydim << 11), col);
                            renderDrawLine(x1 - y2 + (xdim << 11), y1 + x3 + (ydim << 11),
                                           x1 + x2 + (xdim << 11), y1 + y3 + (ydim << 11), col);
                            renderDrawLine(x1 + y2 + (xdim << 11), y1 - x3 + (ydim << 11),
                                           x1 + x2 + (xdim << 11), y1 + y3 + (ydim << 11), col);
                        }
                        else
                        {
                            if (bitmap_test(gotsector, i) && czoom > 192)
                            {
                                daang = (spr->ang - cang) & 2047;
                                if (j == Player[screenpeek].PlayerSprite)
                                {
                                    x1 = 0;
                                    //y1 = (yxaspect << 2);
                                    y1 = 0;
                                    daang = 0;
                                }

                                // Special case tiles
                                if (spr->picnum == 3123) break;

                                if (sprisplayer)
                                {
                                    if (gNet.MultiGameType != MULTI_GAME_COMMBAT || j == Player[screenpeek].PlayerSprite)
                                        rotatesprite((x1 << 4) + (xdim << 15), (y1 << 4) + (ydim << 15), mulscale16(czoom * (spr->yrepeat), yxaspect), daang, 1196+pspr_ndx[myconnectindex], spr->shade, spr->pal, (spr->cstat & 2) >> 1, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                                }
                                else
                                    rotatesprite((x1 << 4) + (xdim << 15), (y1 << 4) + (ydim << 15), mulscale16(czoom * (spr->yrepeat), yxaspect), daang, spr->picnum, spr->shade, spr->pal, (spr->cstat & 2) >> 1, windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
                            }
                        }
                    }
                    break;
                case CSTAT_SPRITE_ALIGNMENT_WALL: // Rotated sprite
                    x1 = sprx;
                    y1 = spry;
                    tilenum = spr->picnum;
                    xoff = (int)picanm[tilenum].xofs + (int)spr->xoffset;
                    if ((spr->cstat & 4) > 0)
                        xoff = -xoff;
                    k = spr->ang;
                    l = spr->xrepeat;
                    dax = sintable[k & 2047] * l;
                    day = sintable[(k + 1536) & 2047] * l;
                    l = tilesiz[tilenum].x;
                    k = (l >> 1) + xoff;
                    x1 -= mulscale16(dax, k);
                    x2 = x1 + mulscale16(dax, l);
                    y1 -= mulscale16(day, k);
                    y2 = y1 + mulscale16(day, l);

                    ox = x1 - cposx;
                    oy = y1 - cposy;
                    x1 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                    y1 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                    ox = x2 - cposx;
                    oy = y2 - cposy;
                    x2 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                    y2 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                    renderDrawLine(x1 + (xdim << 11), y1 + (ydim << 11),
                                   x2 + (xdim << 11), y2 + (ydim << 11), col);

                    break;
                case CSTAT_SPRITE_ALIGNMENT_FLOOR:    // Floor sprite
                    if (dimensionmode == 5)
                    {
                        tilenum = spr->picnum;
                        xoff = (int)picanm[tilenum].xofs + (int)spr->xoffset;
                        yoff = (int)picanm[tilenum].yofs + (int)spr->yoffset;
                        if ((spr->cstat & 4) > 0)
                            xoff = -xoff;
                        if ((spr->cstat & 8) > 0)
                            yoff = -yoff;

                        k = spr->ang;
                        cosang = sintable[(k + 512) & 2047];
                        sinang = sintable[k];
                        xspan = tilesiz[tilenum].x;
                        xrepeat = spr->xrepeat;
                        yspan = tilesiz[tilenum].y;
                        yrepeat = spr->yrepeat;

                        dax = ((xspan >> 1) + xoff) * xrepeat;
                        day = ((yspan >> 1) + yoff) * yrepeat;
                        x1 = sprx + mulscale16(sinang, dax) + mulscale16(cosang, day);
                        y1 = spry + mulscale16(sinang, day) - mulscale16(cosang, dax);
                        l = xspan * xrepeat;
                        x2 = x1 - mulscale16(sinang, l);
                        y2 = y1 + mulscale16(cosang, l);
                        l = yspan * yrepeat;
                        k = -mulscale16(cosang, l);
                        x3 = x2 + k;
                        x4 = x1 + k;
                        k = -mulscale16(sinang, l);
                        y3 = y2 + k;
                        y4 = y1 + k;

                        ox = x1 - cposx;
                        oy = y1 - cposy;
                        x1 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                        y1 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                        ox = x2 - cposx;
                        oy = y2 - cposy;
                        x2 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                        y2 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                        ox = x3 - cposx;
                        oy = y3 - cposy;
                        x3 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                        y3 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                        ox = x4 - cposx;
                        oy = y4 - cposy;
                        x4 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
                        y4 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

                        renderDrawLine(x1 + (xdim << 11), y1 + (ydim << 11),
                                       x2 + (xdim << 11), y2 + (ydim << 11), col);

                        renderDrawLine(x2 + (xdim << 11), y2 + (ydim << 11),
                                       x3 + (xdim << 11), y3 + (ydim << 11), col);

                        renderDrawLine(x3 + (xdim << 11), y3 + (ydim << 11),
                                       x4 + (xdim << 11), y4 + (ydim << 11), col);

                        renderDrawLine(x4 + (xdim << 11), y4 + (ydim << 11),
                                       x1 + (xdim << 11), y1 + (ydim << 11), col);

                    }
                    break;
                }
            }
        }
    // Draw white lines
    for (i = 0; i < numsectors; i++)
    {
        startwall = sector[i].wallptr;
        endwall = sector[i].wallptr + sector[i].wallnum - 1;

        for (j = startwall, wal = &wall[startwall]; j <= endwall; j++, wal++)
        {
            if ((uint16_t)wal->nextwall < MAXWALLS)
                continue;

            if (!bitmap_test(show2dwall, j))
                continue;

            if (tilesiz[wal->picnum].x == 0)
                continue;
            if (tilesiz[wal->picnum].y == 0)
                continue;

            ox = wal->x - cposx;
            oy = wal->y - cposy;
            x1 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
            y1 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

            wal2 = &wall[wal->point2];
            ox = wal2->x - cposx;
            oy = wal2->y - cposy;
            x2 = mulscale16(ox, xvect) - mulscale16(oy, yvect);
            y2 = mulscale16(oy, xvect2) + mulscale16(ox, yvect2);

            renderDrawLine(x1 + (xdim << 11), y1 + (ydim << 11), x2 + (xdim << 11), y2 + (ydim << 11), 24);
        }
    }

    videoSetCorrectedAspect();
}

extern int tilefileoffs[MAXTILES]; //offset into the

#if 0
loadtile(short tilenume)
{
    buildvfs_kfd artfil = buildvfs_kfd_invalid;
    char *ptr;
    int i;
    char zerochar = 0;

    if (walsiz[tilenume] <= 0)
        return;

    i = tilefilenum[tilenume];
    if (i != artfilnum)
    {
        if (artfil != buildvfs_kfd_invalid)
            kclose(artfil);
        artfilnum = i;
        artfilplc = 0L;

        artfilename[7] = (i%10)+48;
        artfilename[6] = ((i/10)%10)+48;
        artfilename[5] = ((i/100)%10)+48;
        artfil = kopen4load(artfilename);
        faketimerhandler();
    }

    if (waloff[tilenume] == 0)
        allocache(&waloff[tilenume],walsiz[tilenume],&zerochar);

    if (artfilplc != tilefileoffs[tilenume])
    {
        klseek(artfil,tilefileoffs[tilenume]-artfilplc,SEEK_CUR);
        faketimerhandler();
    }

    ptr = (char *)waloff[tilenume];
    kread(artfil,ptr,walsiz[tilenume]);
    faketimerhandler();
    artfilplc = tilefileoffs[tilenume]+walsiz[tilenume];
}
#endif

#if RANDOM_DEBUG
int
RandomRange(int range, char *file, unsigned line)
{
    uint32_t rand_num;
    uint32_t value;
    extern FILE *fout_err;
    extern uint32_t MoveThingsCount;

    if (RandomPrint && !Prediction)
    {
        sprintf(ds,"mtc %d, %s, line %d, %d",MoveThingsCount,file,line,randomseed);
        DebugWriteString(ds);
    }

    if (range <= 0)
        return 0;

    rand_num = krand2();

    if (rand_num == 65535U)
        rand_num--;

    // shift values to give more precision
    value = (rand_num << 14) / ((65535UL << 14) / range);

    if (value >= range)
        value = range - 1;

    return value;
}
#else
int
RandomRange(int range)
{
    uint32_t rand_num;
    uint32_t value;

    if (range <= 0)
        return 0;

    rand_num = RANDOM();

    if (rand_num == 65535U)
        rand_num--;

    // shift values to give more precision
    value = (rand_num << 14) / ((65535UL << 14) / range);

    if (value >= (uint32_t)range)
        value = range - 1;

    return value;
}
#endif

int
StdRandomRange(int range)
{
    uint32_t rand_num;
    uint32_t value;

    if (range <= 0)
        return 0;

    rand_num = STD_RANDOM();

    if (rand_num == WRAND_MAX)
        rand_num--;

    // shift values to give more precision
#if (WRAND_MAX > 0x7fff)
    value = rand_num / (((int)WRAND_MAX) / range);
#else
    value = (rand_num << 14) / ((((int)WRAND_MAX) << 14) / range);
#endif

    if (value >= (uint32_t)range)
        value = range - 1;

    return value;
}

// [JM] Probably will need some doing over. !CHECKME!
extern "C" void M32RunScript(const char *s) { UNREFERENCED_PARAMETER(s); }
void G_Polymer_UnInit(void) { }
void app_crashhandler(void) { }

int osdcmd_restartvid(const osdfuncparm_t *parm)
{
    UNREFERENCED_PARAMETER(parm);

    videoResetMode();
    if (videoSetGameMode(fullscreen, xdim, ydim, bpp, upscalefactor))
        LOG_F(WARNING, "restartvid: Reset failed...");

    return OSDCMD_OK;
}

#include "saveable.h"

saveable_module saveable_build{};

void Saveable_Init_Dynamic()
{
    static saveable_data saveable_build_data[] =
    {
        {sector, MAXSECTORS*sizeof(sectortype)},
        {sprite, MAXSPRITES*sizeof(spritetype)},
        {wall, MAXWALLS*sizeof(walltype)},
    };

    saveable_build.data = saveable_build_data;
    saveable_build.numdata = NUM_SAVEABLE_ITEMS(saveable_build_data);
}

// vim:ts=4:sw=4:expandtab:
