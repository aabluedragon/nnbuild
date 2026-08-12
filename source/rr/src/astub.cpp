//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "compat.h"
#include "build.h"
#include "editor.h"
#include "pragmas.h"

#include "baselayer.h"
#include "renderlayer.h"

#include "osd.h"
#include "cache1d.h"

#include "osdfuncs.h"
#include "names.h"

#include "grpscan.h"

#include "common.h"
#include "common_game.h"
#include "mapster32.h"
#include "keys.h"

#include "keyboard.h"
#include "scriptfile.h"
#include "xxhash.h"

#include "sounds_mapster32.h"
#include "fx_man.h"

#include "macros.h"
#include "lz4.h"
#include "colmatch.h"
#include "palette.h"

#include "m32script.h"
#include "m32def.h"

#include <signal.h>

#define xdimgame xdim
#define ydimgame ydim
#define ydim2d ydim

// Workaround for namespace pollution in <sys/stat.h> introduced in MinGW 4.8.
#ifdef stat
# undef stat
#endif

const char* AppProperName = "Mapster32";
const char* AppTechnicalName = "mapster32";

#if defined(_WIN32)
#define DEFAULT_GAME_EXEC "eduke32.exe"
#define DEFAULT_GAME_LOCAL_EXEC "eduke32.exe"
#elif defined(__APPLE__)
#define DEFAULT_GAME_EXEC "EDuke32.app/Contents/MacOS/eduke32"
#define DEFAULT_GAME_LOCAL_EXEC "EDuke32.app/Contents/MacOS/eduke32"
#else
#define DEFAULT_GAME_EXEC "eduke32"
#define DEFAULT_GAME_LOCAL_EXEC "./eduke32"
#endif

const char* DefaultGameExec = DEFAULT_GAME_EXEC;
const char* DefaultGameLocalExec = DEFAULT_GAME_LOCAL_EXEC;

static int32_t floor_over_floor;
static int32_t g_fillCurSector = 0;

const char* defaultsetupfilename = SETUPFILENAME;
char setupfilename[BMAX_PATH] = SETUPFILENAME;

static int32_t fixmaponsave_walls = 0;
static int32_t lastsave = -180*60;
static int32_t NoAutoLoad = 0;
static int32_t spnoclip=1;

static char const *default_tiles_cfg = "tiles.cfg";
static int32_t pathsearchmode_oninit;

#pragma pack(push,1)
sound_t g_sounds[MAXSOUNDS];
#pragma pack(pop)

static int16_t g_definedsndnum[MAXSOUNDS];  // maps parse order index to g_sounds index
static int16_t g_sndnum[MAXSOUNDS];  // maps current order index to g_sounds index
int32_t g_numsounds = 0;
static int32_t mousecol, bstatus;

static int32_t corruptchecktimer;
static int32_t curcorruptthing=-1;

static uint32_t templenrepquot=1;

static int32_t duke3d_m32_globalflags;

// KEEPINSYNC global.h (used values only)
enum DUKE3D_GLOBALFLAGS {
    DUKE3D_NO_HARDCODED_FOGPALS = 1<<1,
    DUKE3D_NO_PALETTE_CHANGES = 1<<2,
};

//////////////////// Key stuff ////////////////////

#define eitherALT   (keystatus[KEYSC_LALT] || keystatus[KEYSC_RALT])
#define eitherCTRL  (keystatus[KEYSC_LCTRL] || keystatus[KEYSC_RCTRL])
#define eitherSHIFT (keystatus[KEYSC_LSHIFT] || keystatus[KEYSC_RSHIFT])

#define PRESSED_KEYSC(Key) (keystatus[KEYSC_##Key] && !(keystatus[KEYSC_##Key]=0))


//////////////////// Aiming ////////////////////
static const char *Typestr[] = { "Wall", "Ceiling", "Floor", "Sprite", "Wall" };
static const char *typestr[] = { "wall", "ceiling", "floor", "sprite", "wall" };
static const char *Typestr_wss[] = { "Wall", "Sector", "Sector", "Sprite", "Wall" };
/*static const char *typestr_wss[] = { "wall", "sector", "sector", "sprite", "wall" };*/

/** The following macros multiplex between identically named fields of sector/wall/sprite,
 * based on a macro parameter or the currently aimed at object (AIMED_ versions).
 * They can be used on either side of an assignment. */

// select wall, only makes a difference with walls that have 'swap bottom of walls' bit set
#define SELECT_WALL() (AIMING_AT_WALL ? searchbottomwall : searchwall)

#define SECFLD(i, Field)  (sector[i].Field)
#define WALFLD(i, Field)  (wall[i].Field)
#define SPRFLD(i, Field)  (sprite[i].Field)

// valid fields: z, stat, picnum, heinum, shade, pal, xpanning, ypanning
#define CEILINGFLOOR(iSec, Field) (*(AIMING_AT_CEILING ? &(sector[iSec].ceiling##Field) : &(sector[iSec].floor##Field)))
#define AIMED_CEILINGFLOOR(Field) CEILINGFLOOR(searchsector, Field)

#define AIMED_SEL_WALL(Field) WALFLD(SELECT_WALL(), Field)

// selects from wall proper or its mask
#define OVR_WALL(iWal, Field) (*(AIMING_AT_WALL ? &WALFLD(iWal, Field) : &(wall[iWal].over##Field)))
#define AIMED_SELOVR_WALL(Field) OVR_WALL(SELECT_WALL(), Field)

// the base macro to construct field multiplexing macros: wall and sector cases undetermined
#define MUXBASE(Field, SectorCase, WallCase) (*(AIMING_AT_CEILING_OR_FLOOR ? (SectorCase) : \
                                               (AIMING_AT_WALL_OR_MASK ? (WallCase) : \
                                                &SPRFLD(searchwall, Field))))

#define SFBASE_CF(Field, WallCase) MUXBASE(Field, &AIMED_CEILINGFLOOR(Field), WallCase)

#define SFBASE_(Field, WallCase) MUXBASE(Field, &SECFLD(searchsector,Field), WallCase)

#define AIMED(Field) SFBASE_(Field, &WALFLD(searchwall, Field))
#define AIMED_SEL(Field) SFBASE_(Field, &AIMED_SEL_WALL(Field))
//#define AIMED_CF(Field) SFBASE_CF(Field, &WALFLD(searchwall,Field))
#define AIMED_CF_SEL(Field) SFBASE_CF(Field, &AIMED_SEL_WALL(Field))

// OVR makes sense only with picnum
//#define AIMED_OVR_PICNUM  SFBASE_CF(picnum, &OVR_WALL(searchwall, picnum))
#define AIMED_SELOVR_PICNUM SFBASE_CF(picnum, &AIMED_SELOVR_WALL(picnum))

static const char *ONOFF_[] = {"OFF","ON"};
#define ONOFF(b) (ONOFF_[!!(b)])

static int32_t tsign, mouseax=0, mouseay=0;
static int32_t repeatcountx, repeatcounty;
static int32_t infobox=3; // bit0: current window, bit1: mouse pointer, the variable should be renamed

static char wallshades[MAXWALLS];
static char sectorshades[MAXSECTORS][2];
static char spriteshades[MAXSPRITES];
static char wallpals[MAXWALLS];
static char sectorpals[MAXSECTORS][2];
static char spritepals[MAXSPRITES];
static uint8_t wallflag[MAXWALLS>>3];

#ifdef YAX_ENABLE
static uint8_t havebunch[YAX_MAXBUNCHES];
static int32_t *tempzar[YAX_MAXBUNCHES];

static int32_t yax_invalidop()
{
    silentmessage("Operation forbidden on extended sector.");
    return 0;
}

static int32_t yax_invalidslope()
{
    silentmessage("Firstwalls must coincide for changing slope.");
    return 0;
}

// 1: ok
static int32_t yax_checkslope(int16_t sectnum, int32_t othersectnum)
{
    int16_t w1 = sector[sectnum].wallptr, w2 = wall[w1].point2;
    int16_t nw1 = sector[othersectnum].wallptr, nw2 = wall[nw1].point2;

    if (nw1 < 0)
        return 0;  // error

    nw2 = wall[nw1].point2;
    if (wall[w1].x != wall[nw1].x || wall[w1].y != wall[nw1].y ||
            wall[w2].x != wall[nw2].x || wall[w2].y != wall[nw2].y)
        return 0;

    return 1;
}

# define YAXSLOPECHK(s,os)  (yax_checkslope(s,os) || yax_invalidslope())
# define YAXCHK(p) ((p) || yax_invalidop())
#endif

// tile marking in tile selector for custom creation of tile groups
static uint8_t tilemarked[(MAXTILES+7)>>3];

#ifdef POLYMER
static int16_t spritelightid[MAXSPRITES];
_prlight *spritelightptr[MAXSPRITES];

static int32_t check_prlight_colors(int32_t i)
{
    return (sprite[i].xvel != spritelightptr[i]->color[0]) ||
        (sprite[i].yvel != spritelightptr[i]->color[1]) ||
        (sprite[i].zvel != spritelightptr[i]->color[2]);
}

static void copy_prlight_colors(_prlight *mylightptr, int32_t i)
{
    mylightptr->color[0] = sprite[i].xvel;
    mylightptr->color[1] = sprite[i].yvel;
    mylightptr->color[2] = sprite[i].zvel;
}

#include "../../build/include/m32_addprlight_common_shared.cpp.inc"

static void DeletePolymerLights(void)
{
    int32_t i;
    for (i=0; i<MAXSPRITES; i++)
        if (spritelightptr[i] != NULL)
        {
            polymer_deletelight(spritelightid[i]);
            spritelightid[i] = -1;
            spritelightptr[i] = NULL;
        }
}

void G_Polymer_UnInit(void)
{
    DeletePolymerLights();
}
#endif

extern int32_t mskip;

//extern int32_t fillsector(int16_t sectnum, char fillcolor);

static int32_t osdcmd_quit(osdfuncparm_t const * parm);


#include "../../build/include/duke_rr_astub_tags_shared.cpp.inc"

const char *ExtGetWallCaption(int16_t wallnum)
{
    static char tempbuf[64];

    Bmemset(tempbuf,0,sizeof(tempbuf));

    if (wall[wallnum].cstat & (1<<14))
    {
        Bsprintf(tempbuf,"%d", wallength(wallnum));
        wall[wallnum].cstat &= ~(1<<14);
        return tempbuf;
    }

    if (!(onnames==2 || onnames==4))
    {
        tempbuf[0] = 0;
        return tempbuf;
    }

    // HERE

    if ((wall[wallnum].lotag|wall[wallnum].hitag) == 0)
        tempbuf[0] = 0;
    else
    {
        int32_t lt = taglab_linktags(0, wallnum);
        char histr[TAGLAB_MAX+16], lostr[TAGLAB_MAX+16];

        lt &= ~(int)(wall[wallnum].lotag<=0);
        lt &= ~(int)((wall[wallnum].hitag<=0)<<1);

        taglab_handle1(lt&2, wall[wallnum].hitag, histr);

#ifdef YAX_ENABLE__COMPAT
        if (yax_getnextwall(wallnum, YAX_CEILING) >= 0)  // ceiling nextwall: lotag
        {
            if (wall[wallnum].hitag == 0)
                tempbuf[0] = 0;
            else
                Bsprintf(tempbuf, "%s,*", histr);
        }
        else
#endif
        {
            taglab_handle1(lt&1, wall[wallnum].lotag, lostr);
            Bsnprintf(tempbuf, sizeof(tempbuf), "%s,%s", histr, lostr);
        }
    }

    return tempbuf;
} //end

#include "../../build/include/duke_rr_astub_sector_text_shared.cpp.inc"

#include "../../build/include/duke_rr_ext_get_sprite_caption_shared.cpp.inc"

//printext16 parameters:
//printext16(int32_t xpos, int32_t ypos, int16_t col, int16_t backcol,
//           char name[82], char fontsize)
//  xpos 0-639   (top left)
//  ypos 0-479   (top left)
//  col 0-15
//  backcol 0-15, -1 is transparent background
//  name
//  fontsize 0=8*8, 1=3*5

//drawline16 parameters:
// drawline16(int32_t x1, int32_t y1, int32_t x2, int32_t y2, char col)
//  x1, x2  0-639
//  y1, y2  0-143  (status bar is 144 high, origin is top-left of STATUS BAR)
//  col     0-15

static void PrintStatus(const char *string, int32_t num, int32_t x, int32_t y, int32_t color)
{
    Bsprintf(tempbuf, "%s %d", string, num);
    printext16(x*8, ydim-STATUS2DSIZ+y*8, editorcolors[color], -1, tempbuf, 0);
}

static void PrintNextTag(void)
{
    int32_t obj;
    int32_t nexttag = taglab_getnextfreetag(&obj);

    if (nexttag >= 1)
    {
        if (obj == -1)
            printmessage16("Level %s next tag %d (no tagged objects)", levelname, nexttag);
        else
            printmessage16("Level %s next tag %d (%s %d has greatest)", levelname, nexttag,
                           (obj&32768) ? "sprite" : "wall", obj&32767);
    }
}

void ExtShowSectorData(int16_t sectnum)   //F5
{
    int32_t x,x2,y;
    int32_t i,yi;
    int32_t secrets=0;
    int32_t totalactors1=0,totalactors2=0,totalactors3=0,totalactors4=0;
    int32_t totalrespawn=0;

    UNREFERENCED_PARAMETER(sectnum);
    if (in3dmode())
        return;

    for (i=0; i<numsectors; i++)
        secrets += (sector[i].lotag==32767);

    for (i=headspritestat[0]; i != -1; i=nextspritestat[i])
    {
        // Count all non-player actors.
        if (tileInGroup(tilegroupActors, sprite[i].picnum))
        {
            if (sprite[i].lotag<=1) totalactors1++;
            if (sprite[i].lotag<=2) totalactors2++;
            if (sprite[i].lotag<=3) totalactors3++;
            if (sprite[i].lotag<=4) totalactors4++;
        }

        if (sprite[i].picnum == RESPAWN)
            totalrespawn++;
    }

    Bmemset(numsprite, 0, sizeof(numsprite));
    Bmemset(multisprite, 0, sizeof(numsprite));

    for (i=0; i<MAXSPRITES; i++)
    {
        if (sprite[i].statnum==0 && sprite[i].picnum>=0 && sprite[i].picnum<MAXTILES)
        {
            if (sprite[i].pal!=0)
                multisprite[sprite[i].picnum]++;
            else
                numsprite[sprite[i].picnum]++;
        }
    }

    clearmidstatbar16();             //Clear middle of status bar

    ydim -= 8;
    drawgradient();
    ydim += 8;

    PrintNextTag();

#define PRSTAT(Str, Tiledef) \
    PrintStatus(Str, numsprite[Tiledef], x, y+yi, numsprite[Tiledef]?11:7); \
    PrintStatus("",multisprite[Tiledef], x2,y+yi, multisprite[Tiledef]?9:7); \
    yi++;

    ydim -= 8; // vvvvvv reset at end!!

    videoBeginDrawing();  //{{{

    x=1; x2=14;
    y=4; yi=2;
    printext16(x*8, ydim-STATUS2DSIZ+y*8, editorcolors[11], -1, "Item Count", 0);

    PRSTAT("10%health=", COLA);
    PRSTAT("30%health=", SIXPAK);
    PRSTAT("Med-Kit  =", FIRSTAID);
    PRSTAT("Atom     =", ATOMICHEALTH);
    PRSTAT("Shields  =", SHIELD);

    x=17; x2=30;
    y=4; yi=2;
    printext16(x*8, ydim-STATUS2DSIZ+y*8, editorcolors[11], -1, "Inventory", 0);

    PRSTAT("Steroids =", STEROIDS);
    PRSTAT("Airtank  =", AIRTANK);
    PRSTAT("Jetpack  =", JETPACK);
    PRSTAT("Goggles  =", HEATSENSOR);
    PRSTAT("Boots    =", BOOTS);
    PRSTAT("HoloDuke =", HOLODUKE);
    PRSTAT("Multi D  =", APLAYER);

    x=33; x2=46;
    y=4; yi=2;
    printext16(x*8, ydim-STATUS2DSIZ+y*8, editorcolors[11], -1, "Weapon Count", 0);

    PRSTAT("Pistol   =", FIRSTGUNSPRITE);
    PRSTAT("Shotgun  =", SHOTGUNSPRITE);
    PRSTAT("Chaingun =", CHAINGUNSPRITE);
    PRSTAT("RPG      =", RPGSPRITE);
    PRSTAT("Pipe Bomb=", HEAVYHBOMB);
    PRSTAT("Shrinker =", SHRINKERSPRITE);
    PRSTAT("Devastatr=", DEVISTATORSPRITE);
    PRSTAT("Trip mine=", TRIPBOMBSPRITE);
    PRSTAT("Freezeray=", FREEZESPRITE);

    x=49; x2=62;
    y=4; yi=2;
    printext16(x*8,ydim-STATUS2DSIZ+y*8,editorcolors[11],-1,"Ammo Count",0);

    PRSTAT("Pistol   =", AMMO);
    PRSTAT("Shot     =", SHOTGUNAMMO);
    PRSTAT("Chain    =", BATTERYAMMO);
    PRSTAT("RPG Box  =", RPGAMMO);
    PRSTAT("Pipe Bomb=", HBOMBAMMO);
    PRSTAT("Shrinker =", CRYSTALAMMO);
    PRSTAT("Devastatr=", DEVISTATORAMMO);
    PRSTAT("Expander =", GROWAMMO);
    PRSTAT("Freezeray=", FREEZEAMMO);

    printext16(65*8, ydim-STATUS2DSIZ+4*8, editorcolors[11], -1, "MISC", 0);
    printext16(65*8, ydim-STATUS2DSIZ+8*8, editorcolors[11], -1, "ACTORS", 0);

#undef PRSTAT

    PrintStatus("Secrets =", secrets, 65, 6, 11);
    PrintStatus("Skill 1 =", totalactors1, 65, 10, 11);
    PrintStatus("Skill 2 =", totalactors2, 65, 11, 11);
    PrintStatus("Skill 3 =", totalactors3, 65, 12, 11);
    PrintStatus("Skill 4 =", totalactors4, 65, 13, 11);
    PrintStatus("Respawn =", totalrespawn, 65, 14, 11);

    videoEndDrawing();  //}}}

    ydim += 8; // ^^^^^^ see above!
}

void ExtShowWallData(int16_t wallnum)       //F6
{
    int32_t i, runi, total=0, x, y, yi;

    UNREFERENCED_PARAMETER(wallnum);

    if (in3dmode())
        return;

    clearmidstatbar16();
    drawgradient();

    PrintNextTag();

#define CASES_LIZTROOP \
    LIZTROOP: case LIZTROOPRUNNING : case LIZTROOPSTAYPUT: case LIZTROOPSHOOT: \
              case LIZTROOPJETPACK: case LIZTROOPONTOILET: case LIZTROOPDUCKING
#define CASES_BOSS1  BOSS1: case BOSS1STAYPUT: case BOSS1SHOOT: case BOSS1LOB: case BOSSTOP

    Bmemset(numsprite, 0, sizeof(numsprite));
    Bmemset(multisprite, 0, sizeof(multisprite));

    for (i=0; i<MAXSPRITES; i++)
    {
        if (sprite[i].statnum==0 && sprite[i].pal)
            switch (sprite[i].picnum)
            {
            case CASES_LIZTROOP:
                numsprite[LIZTROOP]++;
                break;
            case CASES_BOSS1:
                multisprite[BOSS1]++;
                break;
            case BOSS2:
                multisprite[BOSS2]++;
                break;
            case BOSS3:
                multisprite[BOSS3]++;
            default:
                break;
            }
    }

    // runi==0: Count Normal Actors
    // runi==1: Count Respawn Actors
    for (runi=0; runi<2; runi++)
    {
        if (runi==1)
        {
            Bmemset(numsprite, 0, sizeof(numsprite));
            Bmemset(multisprite, 0, sizeof(multisprite));
        }

        for (i=0; i<MAXSPRITES; i++)
        {
            int32_t pic;

            if (sprite[i].statnum!=0)
                continue;

            if (runi==0 && sprite[i].pal!=0)
                continue;

            if (runi==1 && sprite[i].picnum!=RESPAWN)
                continue;

            pic = (runi==0) ? (int)sprite[i].picnum : (int)sprite[i].hitag;
            if (pic<0 || pic>=MAXTILES)
                continue;

            switch (pic)
            {
            case CASES_LIZTROOP:
                numsprite[LIZTROOP]++;
                break;
            case PIGCOP: case PIGCOPSTAYPUT: case PIGCOPDIVE:
                numsprite[PIGCOP]++;
                break;
            case LIZMAN: case LIZMANSTAYPUT: case LIZMANSPITTING: case LIZMANFEEDING: case LIZMANJUMP:
                numsprite[LIZMAN]++;
                break;
            case CASES_BOSS1:
                if (runi==0 || sprite[i].pal==0)
                    numsprite[BOSS1]++;
                else
                    multisprite[BOSS1]++;
                break;
            case COMMANDER:
            case COMMANDERSTAYPUT:
                numsprite[COMMANDER]++;
                break;
            case OCTABRAIN:
            case OCTABRAINSTAYPUT:
                numsprite[OCTABRAIN]++;
                break;
            case RECON: case DRONE: case ROTATEGUN: case EGG: case ORGANTIC: case GREENSLIME:
            case BOSS2: case BOSS3: case TANK: case NEWBEAST: case NEWBEASTSTAYPUT: case BOSS4:
                numsprite[pic]++;
                break;
            default:
                break;
            }
        }

#undef CASES_LIZTROOP
#undef CASES_BOSS1

        total=0;
        for (i=0; i<MAXTILES; i++)
            total += numsprite[i];
        for (i=0; i<MAXTILES; i++)
            total += multisprite[i];

        videoBeginDrawing();  //{{{

        x=2+runi*34;
        y=4;
        PrintStatus(runi==0?"Normal actors:":"Respawn actors:", total, x, y, 11);

#define PRSTAT(Str, Tiledef)  PrintStatus(Str, numsprite[Tiledef], x, y+(yi++), numsprite[Tiledef]?11:7);
        yi=1;

        PRSTAT(" Liztroop  =", LIZTROOP);
        PRSTAT(" Lizman    =", LIZMAN);
        PRSTAT(" Commander =", COMMANDER);
        PRSTAT(" Octabrain =", OCTABRAIN);
        PRSTAT(" PigCop    =", PIGCOP);
        PRSTAT(" Recon Car =", RECON);
        PRSTAT(" Drone     =", DRONE);
        PRSTAT(" Turret    =", ROTATEGUN);
        PRSTAT(" Egg       =", EGG);

        x+=17;
        yi=1;
        PRSTAT("Slimer    =", GREENSLIME);
        PRSTAT("Boss1     =", BOSS1);
        PrintStatus("MiniBoss1 =", multisprite[BOSS1], x, y+(yi++), multisprite[BOSS1]?11:7);
        PRSTAT("Boss2     =", BOSS2);
        PRSTAT("Boss3     =", BOSS3);
        PRSTAT("Riot Tank =", TANK);
        PRSTAT("Newbeast  =", NEWBEAST);
        PRSTAT("Boss4     =", BOSS4);
#undef PRSTAT

        videoEndDrawing();  //}}}
    }
}

#include "../../build/include/m32_show_file_text_shared.cpp.inc"

#define M32_IHELP_LOG_LOADING(name) initprintf("Loading \"%s\"\n", (name))
#define M32_IHELP_LOG_MISSING(name) initprintf("Error initializing integrated help: file \"%s\" not found.\n", (name))
#define M32_IHELP_HEADER_YDIM ydim2d
#include "../../build/include/m32common_integrated_help_shared.cpp.inc"

#define SOUND_NUMDISPLINES IHELP_NUMDISPLINES

static int32_t compare_sounds_s(int16_t k1, int16_t k2)
{
    return (int32_t)k1 - (int32_t)k2;
}
static int32_t compare_sounds_d(int16_t k1, int16_t k2)
{
    sound_t *s1 = &g_sounds[k1], *s2 = &g_sounds[k2];
    char *n1 = s1->definedname, *n2 = s2->definedname;

    if (!n1 && !n2) return 0;
    if (!n1) return -1;
    if (!n2) return 1;
    return Bstrcasecmp(n1, n2);
}
static int32_t compare_sounds_f(int16_t k1, int16_t k2)
{
    sound_t *s1 = &g_sounds[k1], *s2 = &g_sounds[k2];
    char *n1 = s1->filename, *n2 = s2->filename;

    if (!n1 && !n2) return 0;
    if (!n1) return -1;
    if (!n2) return 1;
    return Bstrcasecmp(n1, n2);
}
static int32_t compare_sounds_1(int16_t k1, int16_t k2)
{
    return (g_sounds[k2].m&1) - (g_sounds[k1].m&1);
}
static int32_t compare_sounds_2(int16_t k1, int16_t k2)
{
    return (g_sounds[k2].m&2) - (g_sounds[k1].m&2);
}
static int32_t compare_sounds_3(int16_t k1, int16_t k2)
{
    return (g_sounds[k2].m&4) - (g_sounds[k1].m&4);
}
static int32_t compare_sounds_4(int16_t k1, int16_t k2)
{
    return (g_sounds[k2].m&8) - (g_sounds[k1].m&8);
}
static int32_t compare_sounds_5(int16_t k1, int16_t k2)
{
    return (g_sounds[k2].m&16) - (g_sounds[k1].m&16);
}


static int32_t sort_sounds(int32_t how)
{
    int32_t (*compare_sounds)(int16_t, int16_t) = NULL;

    int32_t ms, ofs, l, lb, r, rb, d, n, k1, k2;
    int16_t *src, *dst, *source, *dest, *tmp;

    n = g_numsounds;
    src = source = g_sndnum;
    dest = (int16_t *)Xmalloc(sizeof(int16_t) * n);
    dst = dest;

    switch (how)
    {
    case 'g':  // restore original order
        Bmemcpy(g_sndnum, g_definedsndnum, sizeof(int16_t)*n);
        Bfree(dst);
        return 0;
    case 's':
        compare_sounds = compare_sounds_s;
        break;
    case 'd':
        compare_sounds = compare_sounds_d;
        break;
    case 'f':
        compare_sounds = compare_sounds_f;
        break;
    case '1':
        compare_sounds = compare_sounds_1;
        break;
    case '2':
        compare_sounds = compare_sounds_2;
        break;
    case '3':
        compare_sounds = compare_sounds_3;
        break;
    case '4':
        compare_sounds = compare_sounds_4;
        break;
    case '5':
        compare_sounds = compare_sounds_5;
        break;
    default:
        Bfree(dst);
        return -2;
    }

    for (ms=1; ms<n; ms*=2)
    {
        for (ofs=0; ofs<n; ofs+=2*ms)
        {
            l = ofs;
            r = ofs+ms;
            d = ofs;
            lb = min((l+ms), n);
            rb = min((r+ms), n);
            while (l < lb || r < rb)
            {
                if (l >= lb)
                {
                    dst[d++] = src[r++];
                    continue;
                }
                if (r >= rb)
                {
                    dst[d++] = src[l++];
                    continue;
                }
                k1 = src[l];
                k2 = src[r];
                if (compare_sounds(k1, k2) <= 0)
                {
                    dst[d++] = src[l++];
                    continue;
                }
                dst[d++] = src[r++];
            }
        }
        tmp = src;
        src = dst;
        dst = tmp;
    }
    if (src != source)
        Bmemcpy(source, src, sizeof(int16_t) * n);

    Bfree(dest);
    return 0;
}

#define BLOOD_RR_MAPEDIT_SHARED_SOUND_DISPLAY
#include "../../build/include/blood_rr_mapedit_shared.cpp.inc"
// PK_ ^^^^

int32_t AmbienceToggle = 1;
int32_t ParentalLock = 0;

uint8_t g_ambiencePlaying[MAXSPRITES>>3];

#define testbit(bitarray, i) (bitarray[(i)>>3] & (1<<((i)&7)))
#define setbit(bitarray, i) bitarray[(i)>>3] |= (1<<((i)&7))
#define clearbit(bitarray, i) bitarray[(i)>>3] &= ~(1<<((i)&7))

// adapted from actors.c
static void M32_MoveFX(void)
{
    int32_t i, j;
    int32_t x, ht;
    spritetype *s;

    for (i=headspritestat[0]; i>=0; i=nextspritestat[i])
    {
        s = &sprite[i];

        if (s->picnum != MUSICANDSFX)
        {
            if (testbit(g_ambiencePlaying, i))
            {
                clearbit(g_ambiencePlaying, i);
                S_StopEnvSound(s->lotag, i);
            }
        }
        else if (s->sectnum>=0)
        {
            ht = s->hitag;

            if (s->lotag < 999 && (unsigned)sector[s->sectnum].lotag < 9 &&
                    AmbienceToggle && sector[s->sectnum].floorz != sector[s->sectnum].ceilingz)
            {
                if ((g_sounds[s->lotag].m & SF_MSFX))
                {
                    x = dist((spritetype *)&pos,s);
                    if (x < ht && !testbit(g_ambiencePlaying, i) && FX_VoiceAvailable(g_sounds[s->lotag].pr-1))
                    {
                        char om = g_sounds[s->lotag].m;
                        if (g_numEnvSoundsPlaying == NumVoices)
                        {
                            for (j = headspritestat[0]; j >= 0; j = nextspritestat[j])
                            {
                                if (s->picnum == MUSICANDSFX && j != i && sprite[j].lotag < 999 &&
                                        testbit(g_ambiencePlaying, j) && dist(&sprite[j],(spritetype *)&pos) > x)
                                {
                                    S_StopEnvSound(sprite[j].lotag,j);
                                    break;
                                }

                            }
                            if (j == -1) continue;
                        }
                        g_sounds[s->lotag].m |= SF_LOOP;
                        A_PlaySound(s->lotag,i);
                        g_sounds[s->lotag].m = om;
                        setbit(g_ambiencePlaying, i);
                    }
                    if (x >= ht && testbit(g_ambiencePlaying, i))
                    {
                        clearbit(g_ambiencePlaying, i);
                        S_StopEnvSound(s->lotag,i);
                    }
                }
            }
        }
    }
}


///__ShowHelpText__

void ExtShowSpriteData(int16_t spritenum)   //F6
{
    UNREFERENCED_PARAMETER(spritenum);
    if (!in3dmode())
        ShowFileText("sehelp.hlp");
}

// Floor Over Floor (duke3d)

// If standing in sector with SE42 or SE44
// then draw viewing to SE41 and raise all =hi SE43 cielings.

// If standing in sector with SE43 or SE45
// then draw viewing to SE40 and lower all =hi SE42 floors.

static int32_t fofsizex = -1;
static int32_t fofsizey = -1;
#if 0
static void ResetFOFSize(void)
{
    if (fofsizex != -1) tilesizx[FOF] = fofsizex;
    if (fofsizey != -1) tilesizy[FOF] = fofsizey;
}
#endif
#include "../../build/include/duke_rr_astub_se40_shared.cpp.inc"

void ExtEditSectorData(int16_t sectnum)    //F7
{
    if (in3dmode())
        return;

    if (eitherALT)  //ALT
    {
        keystatus[KEYSC_F7] = 0;
        wallsprite=0;
        curwall = 0;
        curwallnum = 0;
        cursearchspritenum = 0;
        cursectornum=0;
        cursector_lotag = sector[sectnum].lotag;
        cursector_lotag = getnumber16("Enter search sector lotag : ", cursector_lotag, BTAG_MAX,0);
        _printmessage16("Search sector lotag %d",cursector_lotag);
    }
    else EditSectorData(sectnum);
}// end ExtEditSectorData

void ExtEditWallData(int16_t wallnum)       //F8
{
    if (in3dmode())
        return;

    if (eitherALT)  //ALT
    {
        wallsprite=1;
        curwall = wallnum;
        curwallnum = 0;
        cursearchspritenum = 0;
        cursectornum = 0;
        search_lotag = wall[curwall].lotag;
        search_hitag = wall[curwall].hitag;
        search_lotag = getnumber16("Enter wall search lotag : ", search_lotag, BTAG_MAX,0);
        search_hitag = getnumber16("Enter wall search hitag : ", search_hitag, BTAG_MAX,0);
        //    Bsprintf(tempbuf,"Current wall %d lo=%d hi=%d",
        //             curwall,wall[curwall].lotag,wall[curwall].hitag);
        _printmessage16("Search wall lo=%d hi=%d",search_lotag,search_hitag);
    }
    else EditWallData(wallnum);
}

static void GenericSpriteSearch(void);

void ExtEditSpriteData(int16_t spritenum)   //F8
{
    if (in3dmode())
        return;

    if (eitherALT)  //ALT
        GenericSpriteSearch();
    else EditSpriteData(spritenum);
}

static inline void SpriteName(int16_t spritenum, char *lo2)
{
    Bstrcpy(lo2, names[sprite[spritenum].picnum]);
}// end SpriteName


void m32_showmouse(void)
{
    int32_t i, col;

    mousecol = M32_THROB;

    if (whitecol > editorcolors[0])
        col = whitecol - mousecol;
    else col = whitecol + mousecol;

#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        renderDisableFog();
        glDisable(GL_TEXTURE_2D);
        polymost_useColorOnly(true);
    }
#endif

    int const lores = !!(xdim <= 640);

    for (i = (3 - lores); i <= (7 >> lores); i++)
    {
        plotpixel(searchx+i,searchy,col);
        plotpixel(searchx-i,searchy,col);
        plotpixel(searchx,searchy-i,col);
        plotpixel(searchx,searchy+i,col);
    }

    for (i=1; i<=(2 >> lores); i++)
    {
        plotpixel(searchx+i,searchy,whitecol);
        plotpixel(searchx-i,searchy,whitecol);
        plotpixel(searchx,searchy-i,whitecol);
        plotpixel(searchx,searchy+i,whitecol);
    }

    i = (8 >> lores);

    plotpixel(searchx+i,searchy,editorcolors[0]);
    plotpixel(searchx-i,searchy,editorcolors[0]);
    plotpixel(searchx,searchy-i,editorcolors[0]);
    plotpixel(searchx,searchy+i,editorcolors[0]);

    if (!lores)
    {
        for (i=1; i<=4; i++)
        {
            plotpixel(searchx+i,searchy,whitecol);
            plotpixel(searchx-i,searchy,whitecol);
            plotpixel(searchx,searchy-i,whitecol);
            plotpixel(searchx,searchy+i,whitecol);
        }
    }

#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        renderEnableFog();
        polymost_useColorOnly(false);
    }
#endif
}

#include "../../build/include/m32_ask_if_sure_shared.cpp.inc"

static int32_t IsValidTile(int32_t idTile)
{
    return (idTile>=0 && idTile<MAXTILES) && (tilesiz[idTile].x && tilesiz[idTile].y);
}

static int32_t SelectAllTiles(int32_t iCurrentTile)
{
    int32_t i;

    if (iCurrentTile < localartlookupnum)
        iCurrentTile = localartlookup[iCurrentTile];
    else
        iCurrentTile = 0;

    localartlookupnum = MAXTILES;

    for (i = 0; i < MAXTILES; i++)
    {
        localartlookup[i] = i;
        localartfreq[i] = 0;
    }

    return iCurrentTile;
}

static int32_t OnGotoTile(int32_t tileNum);
static int32_t OnSelectTile(int32_t tileNum);
static int32_t OnSaveTileGroup();
static int32_t loadtilegroups(const char *fn);
static int32_t s_Zoom = INITIAL_ZOOM;
static int32_t s_TileZoom = 1;
static char tilesel_errmsg[128], tilesel_showerr=0;

#include "../../build/include/blood_rr_m32_gettile_shared.cpp.inc"

// Dir = 0 (zoom out) or 1 (zoom in)
//void OnZoomInOut( int32_t *pZoom, int32_t Dir /*0*/ )
//{
//}

#define BLOOD_RR_MAPEDIT_SHARED_ON_SAVE_TILE_GROUP
#include "../../build/include/blood_rr_mapedit_shared.cpp.inc"


#ifdef USE_OPENGL
#define M32_TILE_HELPERS_AFTER_POLYMOST_2D_VIEW() do { \
    if (videoGetRenderMode() >= REND_POLYMOST) \
    { \
        glEnable(GL_TEXTURE_2D); \
    } \
} while (0)
#endif
#include "../../build/include/blood_rr_m32_tile_helpers_shared.cpp.inc"
#ifdef USE_OPENGL
#undef M32_TILE_HELPERS_AFTER_POLYMOST_2D_VIEW
#endif

////////// main tile drawing function //////////
static int32_t DrawTiles(int32_t iTopLeft, int32_t iSelected, int32_t nXTiles, int32_t nYTiles,
                         int32_t TileDim, int32_t offset, int32_t showmsg)
{
    int32_t XTile, YTile;
    int32_t tileNum, idTile;
    int32_t i, x, y;
    const char *pRawPixels;
    char szT[128];
#ifdef USE_OPENGL
    int32_t lazyselector = g_lazy_tileselector && usehightile;
    int32_t usehitile;
#else
    int32_t lazyselector = g_lazy_tileselector;
#endif
    int32_t runi=0;
    static uint8_t loadedhitile[(MAXTILES+7)>>3];

#ifdef USE_OPENGL
    polymostSet2dView();

    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        glEnable(GL_TEXTURE_2D);

        if (lazyselector)
            glDrawBuffer(GL_FRONT_AND_BACK);
    }
#endif
    videoClearViewableArea(-1);

    videoBeginDrawing();

restart:
    for (YTile = 0-(offset>0); YTile < nYTiles+(offset<0)+1; YTile++)
    {
        for (XTile = 0; XTile < nXTiles; XTile++)
        {
            tileNum = iTopLeft + XTile + (YTile * nXTiles);

            if (tileNum < 0 || tileNum >= localartlookupnum)
                continue;
#ifdef USE_OPENGL
            usehitile = (runi || !lazyselector);
#endif

            idTile = localartlookup[ tileNum ];
            if (loadedhitile[idTile>>3]&(1<<(idTile&7)))
            {
                if (runi==1)
                    continue;

#ifdef USE_OPENGL
                usehitile = 1;
#endif
            }

            // Get pointer to tile's raw pixel data
            pRawPixels = GetTilePixels(idTile);

            if (pRawPixels != NULL)
            {
                x = XTile * TileDim;
                y = YTile * TileDim+offset;

#ifdef USE_OPENGL
                if (polymost_drawtilescreen(x, y, idTile, TileDim, s_TileZoom,
                                            usehitile, loadedhitile))
#endif
                    classic_drawtilescreen(x, y, idTile, TileDim, pRawPixels);

                if (localartfreq[tileNum] != 0 && y >= 0 && y <= ydim-20)
                {
                    Bsprintf(szT, "%d", localartfreq[tileNum]);
                    printext256(x, y, whitecol, -1, szT, 1);
                }
            }

            tilescreen_drawbox(iTopLeft, iSelected, nXTiles, TileDim, offset, tileNum, idTile);

            if (runi==1 && lazyselector)
            {
                int32_t k;

                if (handleevents())
                    quitevent=0;

                tilescreen_drawrest(iSelected, showmsg);

                k = (g_mousePos.x || g_mousePos.y || g_mouseBits);
                if (!k)
                    for (i=0; i<(signed)ARRAY_SIZE(keystatus); i++)
                        if (keystatus[i])
                        {
                            k = 1;
                            break;
                        }
                if (k)
                {
                    videoEndDrawing();
                    videoShowFrame(1);
#ifdef USE_OPENGL
                    if (videoGetRenderMode() >= REND_POLYMOST && lazyselector)
                        glDrawBuffer(GL_BACK);
#endif
                    return 1;
                }

                videoEndDrawing();
                videoShowFrame(1);
                videoBeginDrawing();
            }
        }
    }

    tilescreen_drawrest(iSelected, showmsg);

    if (videoGetRenderMode() >= REND_POLYMOST && in3dmode() && lazyselector)
    {
        if (runi==0)
        {
            videoEndDrawing();
            videoShowFrame(1);
            videoBeginDrawing();

            runi = 1;
            goto restart;
        }
    }

    videoEndDrawing();
    videoShowFrame(1);

#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST && lazyselector)
        glDrawBuffer(GL_BACK);
#endif

    return 0;

}

#undef TMPERRMSG_SHOW
#undef TMPERRMSG_PRINT
#undef TMPERRMSG_RETURN

#include "../../build/include/duke_rr_astub_tileinfo_shared.cpp.inc"

#if 0
int64_t ldistsqr(spritetype *s1,spritetype *s2)
{
    return (((int64_t)(s2->x - s1->x))*((int64_t)(s2->x - s1->x)) +
            ((int64_t)(s2->y - s1->y))*((int64_t)(s2->y - s1->y)));
}
#endif

#define M32_TEXTENTRY_IS_WALL_ALIGNED(cstatValue) (((cstatValue) & 16) != 0)
#define M32_TEXTENTRY_ARTFREQ_LIMIT MAXTILES
#define M32_TEXTENTRY_FREE(ptr) Bfree(ptr)
#include "../../build/include/m32_text_entry_mode_shared.cpp.inc"

static void mouseaction_movesprites(int32_t *sumxvect, int32_t *sumyvect, int32_t yangofs, int32_t mousexory)
{
    int32_t xvect,yvect, daxvect,dayvect, ii, spi;
    int32_t units, gridlock = (eitherCTRL && grid > 0 && grid < 9);
    spritetype *sp = &sprite[searchwall];
    int16_t tsect = sp->sectnum;
    vec3_t tvec = { sp->x, sp->y, sp->z };

    xvect = -((mousexory*(int32_t)sintable[(ang+yangofs+512)&2047])<<3);
    yvect = -((mousexory*(int32_t)sintable[(ang+yangofs)&2047])<<3);

    if (gridlock)
    {
        units = 1<<(11-grid);

        if ((tvec.x & (units-1)) || (tvec.y & (units-1)))
        {
            daxvect = ((tvec.x & ~(units-1)) - tvec.x)<<14;
            dayvect = ((tvec.y & ~(units-1)) - tvec.y)<<14;
        }
        else
        {
            units <<= 14;

            *sumxvect += xvect;
            *sumyvect += yvect;

            if (klabs(*sumxvect) >= units)
            {
                daxvect = ((*sumxvect)/units)*units;
                *sumxvect %= units;
            }
            else
                daxvect = 0;

            if (klabs(*sumyvect) >= units)
            {
                dayvect = ((*sumyvect)/units)*units;
                *sumyvect %= units;
            }
            else
                dayvect = 0;
        }
    }
    else
    {
        daxvect = xvect;
        dayvect = yvect;
    }

    if (highlightcnt<=0 || (show2dsprite[searchwall>>3] & (1<<(searchwall&7)))==0)
    {
        clipmove(&tvec, &tsect, daxvect,dayvect, sp->clipdist,64<<4,64<<4, spnoclip?1:CLIPMASK0);
        setsprite(searchwall, &tvec);
    }
    else
    {
        xvect = daxvect;
        yvect = dayvect;

        // test run
        for (ii=0; ii<highlightcnt; ii++)
            if (highlight[ii]&16384)
            {
                spi = highlight[ii]&16383;
                Bmemcpy(&tvec, &sprite[spi], sizeof(vec3_t));
                tsect = sprite[spi].sectnum;
                clipmove(&tvec, &tsect, xvect,yvect, 128,64<<4,64<<4, spnoclip?1:CLIPMASK0);

                xvect = (tvec.x - sprite[spi].x)<<14;
                yvect = (tvec.y - sprite[spi].y)<<14;
            }
        // the real thing
        for (ii=0; ii<highlightcnt; ii++)
            if (highlight[ii]&16384)
            {
                spi = highlight[ii]&16383;
                Bmemcpy(&tvec, &sprite[spi], sizeof(vec3_t));
                tsect = sprite[spi].sectnum;
                clipmove(&tvec, &tsect, xvect,yvect, 128,64<<4,64<<4, spnoclip?1:CLIPMASK0);
                setsprite(spi, &tvec);
            }
    }
}

static int32_t addtobyte(int8_t *byte, int32_t num)
{
    int32_t onum = *byte, clamped=0;
    if (onum + num != (int8_t)(onum + num))
        clamped = 1;
    if (!clamped)
        *byte = (onum + num);
    return clamped;
}

static void toggle_sprite_alignment(int32_t spritenum)
{
    static const char *aligntype[4] = { "view", "wall", "floor", "???" };

    int32_t i = sprite[spritenum].cstat;

    if ((i&48) < 32)
        i += 16;
    else
        i &= ~48;
    sprite[spritenum].cstat = i;

    message("Sprite %d now %s aligned", spritenum, aligntype[(i&48)/16]);
    asksave = 1;
}

#include "../../build/include/m32_toggle_cf_flipping_shared.cpp.inc"

// vec2_t initializer from a 1st point of wall
#define WALLVEC_INITIALIZER(w) { POINT2(w).x-wall[w].x, POINT2(w).y-wall[w].y }

static int64_t lldotv2(const vec2_t *v1, const vec2_t *v2)
{
    return (int64_t)v1->x * v2->x + (int64_t)v1->y * v2->y;
}

#ifdef NEW_MAP_FORMAT
# define YAX_BIT_PROTECT 0
#else
# define YAX_BIT_PROTECT YAX_BIT
#endif

////////// KEY PRESS HANDLER IN 3D MODE //////////
static void Keys3d(void)
{
    int32_t i = 0, changedir,tsign; // ,count,nexti
    int32_t j, k, tempint = 0, hiz, loz;
    int32_t hihit, lohit;
    char smooshyalign=0, repeatpanalign=0; //, buffer[80];
    int16_t startwall, endwall, dasector; //, statnum=0;
    char tempbuf[128];

    /* start Mapster32 */

    if (g_numsounds > 0 && AmbienceToggle)
    {
        M32_MoveFX();
        S_Update();
    }

    if (usedcount && !helpon)
    {
#if 0
        if (!AIMING_AT_SPRITE)
        {
            count=0;
            for (i=0; i<numwalls; i++)
            {
                if (wall[i].picnum == temppicnum) count++;
                if (wall[i].overpicnum == temppicnum) count++;
            }
            for (i=0; i<numsectors; i++)
            {
                if (sector[i].ceilingpicnum == temppicnum) count++;
                if (sector[i].floorpicnum == temppicnum) count++;
            }
        }
        else
        {
            count=0;
            statnum=0;

            i = headspritestat[statnum];
            while (i != -1)
            {
                nexti = nextspritestat[i];
                if (sprite[i].picnum == temppicnum) count++;
                i = nexti;
            }
        }
#endif
        drawtileinfo("Clipboard",3,124,temppicnum,tempshade,temppal,tempcstat,templotag,temphitag,tempextra,tempblend,tempstatnum,0);
    }// end if usedcount

    if (searchsector > -1 && searchsector < numsectors)
    {
        char lines[8][128];
        int32_t num=0;
        int32_t x,y,flags=0;

        if (infobox&1)
        {
            int32_t height2 = sector[searchsector].floorz - sector[searchsector].ceilingz;

            switch (searchstat)
            {
            case SEARCH_WALL:
            case SEARCH_MASKWALL:
            {
                int32_t dist, height1=0, height3=0;
                const int32_t w = SELECT_WALL();
                const int32_t swappedbot = (int32_t)(w != searchwall);
                flags |= (swappedbot<<2);
#ifdef YAX_ENABLE__COMPAT
                flags |= (yax_getnextwall(searchwall, YAX_CEILING)>=0) + 2*(yax_getnextwall(searchwall, YAX_FLOOR)>=0);
#endif
                drawtileinfo("Selected", WIND1X,WIND1Y,
                             AIMING_AT_WALL ? wall[w].picnum : wall[w].overpicnum,
                             wall[w].shade, wall[w].pal, wall[w].cstat,
                             wall[searchwall].lotag, wall[searchwall].hitag, wall[searchwall].extra, 0, 0, flags);

                dist = wallength(searchwall);

                if (wall[searchwall].nextsector >= 0 && wall[searchwall].nextsector < numsectors)
                {
                    int32_t nextsect = wall[searchwall].nextsector;
                    height1 = sector[searchsector].floorz - sector[nextsect].floorz;
                    height2 = sector[nextsect].floorz - sector[nextsect].ceilingz;
                    height3 = sector[nextsect].ceilingz - sector[searchsector].ceilingz;
                }

                Bsprintf(lines[num++],"Panning:^%d %d, %d", swappedbot?editorcolors[14]:whitecol,
                                TrackerCast(wall[w].xpanning), TrackerCast(wall[w].ypanning));
                Bsprintf(lines[num++],"Repeat:  %d, %d", TrackerCast(wall[searchwall].xrepeat), TrackerCast(wall[searchwall].yrepeat));
                Bsprintf(lines[num++],"Overpic: %d", TrackerCast(wall[searchwall].overpicnum));
                lines[num++][0]=0;

                if (getmessageleng)
                    break;

                if (searchwall != searchbottomwall)
                    Bsprintf(lines[num++],"^%dWall %d ->^%d %d", editorcolors[10], searchwall, editorcolors[14], searchbottomwall);
                else
                    Bsprintf(lines[num++],"^%dWall %d", editorcolors[10], searchwall);

                if (wall[searchwall].nextsector!=-1)
                    Bsprintf(lines[num++],"LoHeight:%d, HiHeight:%d, Length:%d",height1,height3,dist);
                else
                    Bsprintf(lines[num++],"Height:%d, Length:%d",height2,dist);
                break;
            }

            case SEARCH_CEILING:
            case SEARCH_FLOOR:
                drawtileinfo("Selected", WIND1X, WIND1Y, AIMED_CEILINGFLOOR(picnum), AIMED_CEILINGFLOOR(shade),
                             AIMED_CEILINGFLOOR(pal), AIMED_CEILINGFLOOR(stat),
                             sector[searchsector].lotag, sector[searchsector].hitag, sector[searchsector].extra,0,0,0);

                {
                    int32_t xp=AIMED_CEILINGFLOOR(xpanning), yp=AIMED_CEILINGFLOOR(ypanning);
#ifdef YAX_ENABLE__COMPAT
                    int32_t notextended = 1;
                    if (yax_getbunch(searchsector, AIMING_AT_FLOOR) >= 0)
                        notextended = 0;
                    Bsprintf(lines[num++],"Panning:  %d%s, %d", xp, notextended?"":"*", yp);
#else
                    Bsprintf(lines[num++],"Panning:  %d, %d", xp, yp);
#endif
                }
                Bsprintf(lines[num++],"%sZ: %d", Typestr[searchstat], AIMED_CEILINGFLOOR(z));
                Bsprintf(lines[num++],"Slope:    %d", AIMED_CEILINGFLOOR(heinum));
                lines[num++][0]=0;

                if (getmessageleng)
                    break;

                Bsprintf(lines[num++],"^%dSector %d^%d %s, Lotag:%s", editorcolors[10], searchsector, whitecol, typestr[searchstat], ExtGetSectorCaption(searchsector));
                Bsprintf(lines[num++],"Height: %d, Visibility:%d", height2, TrackerCast(sector[searchsector].visibility));
                break;

            case SEARCH_SPRITE:
                drawtileinfo("Selected", WIND1X, WIND1Y, sprite[searchwall].picnum, sprite[searchwall].shade,
                             sprite[searchwall].pal, sprite[searchwall].cstat, sprite[searchwall].lotag,
                             sprite[searchwall].hitag, sprite[searchwall].extra,sprite[searchwall].blend, sprite[searchwall].statnum, 0);

                Bsprintf(lines[num++], "Repeat:  %d,%d",
                    TrackerCast(sprite[searchwall].xrepeat), TrackerCast(sprite[searchwall].yrepeat));
                Bsprintf(lines[num++], "PosXY:   %d,%d%s",
                    TrackerCast(sprite[searchwall].x), TrackerCast(sprite[searchwall].y),
                         sprite[searchwall].xoffset|sprite[searchwall].yoffset ? " ^251*":"");
                Bsprintf(lines[num++], "PosZ: ""   %d", TrackerCast(sprite[searchwall].z));// prevents tab character
                lines[num++][0]=0;

                if (getmessageleng)
                    break;

                if (sprite[searchwall].picnum<0 || sprite[searchwall].picnum>=MAXTILES)
                    break;

                if (names[sprite[searchwall].picnum][0])
                {
                    if (sprite[searchwall].picnum==SECTOREFFECTOR)
                        Bsprintf(lines[num++],"^%dSprite %d^%d %s", editorcolors[10], searchwall, whitecol, SectorEffectorText(searchwall));
                    else
                        Bsprintf(lines[num++],"^%dSprite %d^%d %s", editorcolors[10], searchwall, whitecol, names[sprite[searchwall].picnum]);
                }
                else Bsprintf(lines[num++],"^%dSprite %d^%d, picnum %d", editorcolors[10], searchwall, whitecol, TrackerCast(sprite[searchwall].picnum));

                Bsprintf(lines[num++], "Elevation:%d",
                         getflorzofslope(searchsector, sprite[searchwall].x, sprite[searchwall].y) - sprite[searchwall].z);
                break;
            }
        }

        x = (int32_t)(WIND1X*(xdimgame/320.));
        y = (int32_t)(WIND1Y*(ydimgame/200.));
        y += (ydimgame>>6)*8;

        if (getmessageleng && !m32_is2d3dmode())
        {
            while (num < 4)
                lines[num++][0] = 0;
            Bsprintf(lines[num++], "^%d%s", editorcolors[10], getmessage);
        }

        videoBeginDrawing();
        for (i=0; i<num; i++)
        {
            printext256(x+2, y+2, 0, -1, lines[i], xdimgame<=640);
            printext256(x, y, whitecol, -1, lines[i], xdimgame<=640);
            y += ydimgame>>6;
        }
        videoEndDrawing();
    }

    VM_OnEvent(EVENT_PREKEYS3D, -1);

    if (keystatus[KEYSC_RALT] && keystatus[KEYSC_RCTRL] && AIMING_AT_CEILING_OR_FLOOR && searchsector >= 0)
    {
        keystatus[KEYSC_RALT] = 0;

        if (highlightcnt >= 0)
        {
            message("WARNING: Cannot select sectors while sprites/walls selected");
        }
        else
        {
            const int32_t sub = keystatus[KEYSC_SEMI];
            const int32_t add = keystatus[KEYSC_QUOTE];

            if (!add && !sub)
                reset_highlightsector();

            handlesecthighlight1(searchsector, sub, 1);
            update_highlightsector();

            message("%s sector %d", sub ? "De-selected" : "Selected", searchsector);
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(V)) // ' V
    {
        if (AIMING_AT_CEILING_OR_FLOOR)
            getnumberptr256("Sector visibility: ", &sector[searchsector].visibility, sizeof(sector[0].visibility), 255, 0, NULL);
    }

    if (keystatus[KEYSC_SEMI] && PRESSED_KEYSC(V))   // ; V
    {
        int16_t currsector;
        uint8_t visval;

        if (highlightsectorcnt == -1)
        {
            message("You didn't select any sectors!");
            return;
        }

        visval = getnumber256("Visibility of selected sectors: ", sector[searchsector].visibility, 255, 0);

        if (AskIfSure("Are you sure to change the visibility of all selected sectors?"))
            return;

        for (i=0; i<highlightsectorcnt; i++)
        {
            currsector = highlightsector[i];
            sector[currsector].visibility = visval;
        }

        message("Visibility changed on all selected sectors");
    }

    if (!keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(L))  // L (grid lock)
    {
        gridlock = !gridlock;
        message("Grid locking %s", gridlock ? "on" : "off");
    }

    if (PRESSED_KEYSC(V))  //V
    {
        if (ASSERT_AIMING)
        {
            int32_t oldtile = AIMED_SELOVR_PICNUM;

            globalpal = AIMED_CF_SEL(pal);
            tempint = m32gettile(oldtile);
            AIMED_SELOVR_PICNUM = tempint;
            AIMED_CF_SEL(pal) = globalpal;

            if (AIMING_AT_MASKWALL && wall[searchwall].nextwall >= 0)
                NEXTWALL(searchwall).overpicnum = tempint;

            if (AIMING_AT_SPRITE)
                correct_sprite_yoffset(searchwall);

            if (oldtile != tempint)
                asksave = 1;
        }
    }

    if (PRESSED_KEYSC(3))  /* 3 (toggle floor-over-floor (cduke3d only) */
    {
        floor_over_floor = !floor_over_floor;
        //        if (!floor_over_floor) ResetFOFSize();
        message("Floor-over-floor display %s",floor_over_floor?"enabled":"disabled");
    }

    if (PRESSED_KEYSC(F3) && !m32_is2d3dmode())
    {
        mlook = !mlook;
        message("Mouselook: %s",mlook?"enabled":"disabled");
    }

    if (PRESSED_KEYSC(F4))
    {
        AmbienceToggle = !AmbienceToggle;
        message("Ambience sounds: %s",AmbienceToggle?"enabled":"disabled");
        if (!AmbienceToggle)
        {
            FX_StopAllSounds();
            S_ClearSoundLocks();
        }
    }

    // PK
    if (PRESSED_KEYSC(F5))
    {
        unrealedlook = !unrealedlook;
        message("UnrealEd mouse navigation: %s",unrealedlook?"enabled":"disabled");
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(DELETE)) // ' del
    {
        if (AIMING_AT_WALL_OR_MASK)
        {
#ifdef NEW_MAP_FORMAT
            wall[searchwall].cstat = 0;
#else
            wall[searchwall].cstat &= YAX_NEXTWALLBITS;
#endif
            message("Wall %d cstat = %d", searchwall, TrackerCast(wall[searchwall].cstat));
        }
        else if (AIMING_AT_SPRITE)
        {
            sprite[searchwall].cstat = 0;
            message("Sprite %d cstat = 0", searchwall);
        }
    }

    // 'P and ;P - Will copy palette to all sectors/walls/sprites highlighted with R-Alt key
    if ((keystatus[KEYSC_QUOTE] || keystatus[KEYSC_SEMI]) && PRESSED_KEYSC(P))   // ' P  ; P
    {
        int16_t w, start_wall, end_wall, currsector;
        int8_t pal[4];

        if (highlightsectorcnt == -1)
        {
            message("You didn't select any sectors!");
            return;
        }

        if (keystatus[KEYSC_QUOTE])
        {
            pal[0] = getnumber256("Ceiling palette: ", -1, M32_MAXPALOOKUPS, 1);
            pal[1] = getnumber256("Floor palette: ", -1, M32_MAXPALOOKUPS, 1);
            pal[2] = getnumber256("Wall palette: ", -1, M32_MAXPALOOKUPS, 1);
            pal[3] = getnumber256("Sprite palette: ", -1, M32_MAXPALOOKUPS, 1);
        }
        else
        {
            pal[0] = getnumber256("Global palette: ", 0, M32_MAXPALOOKUPS, 0);
            pal[1] = pal[2] = pal[3] = pal[0];
        }

        if (AskIfSure(0)) return;

        for (i = 0; i < highlightsectorcnt; i++)
        {
            currsector = highlightsector[i];

            if (pal[0] > -1)
                sector[currsector].ceilingpal = pal[0];
            if (pal[1] > -1)
                sector[currsector].floorpal = pal[1];

            // Do all the walls in the sector
            start_wall = sector[currsector].wallptr;
            end_wall = start_wall + sector[currsector].wallnum;

            if (pal[2] > -1)
                for (w = start_wall; w < end_wall; w++)
                    wall[w].pal = pal[2];

            if (pal[3] > -1)
            {
                for (k=0; k<highlightsectorcnt; k++)
                    for (w=headspritesect[highlightsector[k]]; w >= 0; w=nextspritesect[w])
                        sprite[w].pal = pal[3];
            }
        }

        message("Palettes changed");
    }

    if (PRESSED_KEYSC(DELETE))
    {
        if (AIMING_AT_SPRITE)
        {
            deletesprite(searchwall);

            message("Sprite %d deleted",searchwall);
            if (AmbienceToggle)
            {
                clearbit(g_ambiencePlaying, searchwall);
                S_StopEnvSound(sprite[searchwall].lotag, searchwall);
            }
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(BS))  // backspace
    {
        if (AIMING_AT_WALL_OR_MASK)
        {
#ifdef YAX_ENABLE
            int16_t ynw, cf=-1;

            if (m32_script_expertmode)
            {
                if (eitherSHIFT && !eitherCTRL)
                    cf = 0;
                else if (!eitherSHIFT && eitherCTRL)
                    cf = 1;
            }

            if (cf >= 0)
            {
                // clear TROR uplink/downlink
                ynw = yax_getnextwall(searchwall, cf);
                if (ynw >= 0)
                {
                    yax_setnextwall(ynw, !cf, -1);
                    yax_setnextwall(searchwall, cf, -1);
                }

                message("Cleared wall %d's %s link to wall %d", searchwall, yupdownwall[cf], ynw);
            }
            else
#endif
            if (!eitherSHIFT && !eitherCTRL)
            {
                int32_t nw = wall[searchwall].nextwall;

                if ((unsigned)nw < (unsigned)numwalls)
                {
                    wall[nw].nextsector = wall[nw].nextwall = -1;
                    wall[searchwall].nextsector = wall[searchwall].nextwall = -1;

                    message("Cleared connection between wall %d and %d", searchwall, nw);
                }
            }
        }
    }

    if (PRESSED_KEYSC(F6))  //F6
    {
        autospritehelp = !autospritehelp;
        message("Automatic SECTOREFFECTOR help %s", autospritehelp?"enabled":"disabled");
    }
    if (PRESSED_KEYSC(F7))  //F7
    {
        autosecthelp = !autosecthelp;
        message("Automatic sector tag help %s", autosecthelp?"enabled":"disabled");
    }

    if (autospritehelp && helpon==0)
    {
        if (AIMING_AT_SPRITE && sprite[searchwall].picnum==SECTOREFFECTOR)
            ShowFileText("sehelp.hlp");
        else if (AIMING_AT_CEILING_OR_FLOOR)
            ShowFileText("sthelp.hlp");
    }

    // [.] or [,]: Search & fix panning to the right or left (3D)
    if (AIMING_AT_WALL_OR_MASK && ((tsign=PRESSED_KEYSC(PERIOD)) || PRESSED_KEYSC(COMMA)))
    {
        uint32_t flags = (!eitherSHIFT) | (tsign?0:16) |
            ((!eitherALT)<<2) | ((!!keystatus[KEYSC_QUOTE])<<3) |
            32*(searchwall != searchbottomwall);

        int32_t naligned=AutoAlignWalls(searchwall, flags, 0);
        // Do it a second time because the first one is wrong. FIXME!!!
        AutoAlignWalls(searchwall, flags, 0);

        message("Aligned %d wall%s based on wall %d%s%s%s%s", naligned,
                naligned==1 ? "" : "s", searchwall,
                !eitherSHIFT ? " iteratively" : "",
                !eitherALT ? ", aligning xrepeats" : "",
                keystatus[KEYSC_QUOTE] ? ", aligning TROR-nextwalls" : "",
                (wall[searchbottomwall].cstat&4) ? "" : ". WARNING: top-aligned");
    }

    tsign = 0;
    tsign -= PRESSED_KEYSC(COMMA);
    tsign += PRESSED_KEYSC(PERIOD);

    if (tsign)
    {
        if (AIMING_AT_SPRITE)
        {
            sprite[searchwall].ang += tsign<<(!eitherSHIFT*7);
            sprite[searchwall].ang &= 2047;
            message("Sprite %d angle: %d", searchwall, TrackerCast(sprite[searchwall].ang));
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(L)) // ' L
#ifdef YAX_ENABLE
    if (YAXCHK(!AIMING_AT_CEILING_OR_FLOOR || yax_getbunch(searchsector, AIMING_AT_FLOOR) < 0))
#endif
    {
        i = m32_clipping;
        m32_clipping = 0;

        switch (searchstat)
        {
        case SEARCH_CEILING:
            getnumberptr256("Sector ceilingz: ", &sector[searchsector].ceilingz,
                            sizeof(sector[0].ceilingz), BZ_MAX, 1, NULL);
            if (!(sector[searchsector].ceilingstat&2))
            {
                sector[searchsector].ceilingstat |= 2;
                sector[searchsector].ceilingheinum = 0;
            }
            getnumberptr256("Sector ceiling slope: ", &sector[searchsector].ceilingheinum,
                            sizeof(sector[0].ceilingheinum), BHEINUM_MAX, 1, NULL);
            break;
        case SEARCH_FLOOR:
            getnumberptr256("Sector floorz: ", &sector[searchsector].floorz,
                            sizeof(sector[0].floorz), BZ_MAX, 1, NULL);
            if (!(sector[searchsector].floorstat&2))
            {
                sector[searchsector].floorheinum = 0;
                sector[searchsector].floorstat |= 2;
            }
            getnumberptr256("Sector floor slope: ", &sector[searchsector].floorheinum,
                            sizeof(sector[0].floorheinum), BHEINUM_MAX, 1, NULL);
            break;

        case SEARCH_SPRITE:
            getnumberptr256("Sprite x: ", &sprite[searchwall].x, sizeof(sprite[0].x), editorgridextent-1, 1, NULL);
            getnumberptr256("Sprite y: ", &sprite[searchwall].y, sizeof(sprite[0].y), editorgridextent-1, 1, NULL);
            getnumberptr256("Sprite z: ", &sprite[searchwall].z, sizeof(sprite[0].z), BZ_MAX, 1, NULL);
            getnumberptr256("Sprite angle: ", &sprite[searchwall].ang, sizeof(sprite[0].ang), 2047, 1, NULL);
            sprite[searchwall].ang &= 2047;
            break;
        }

        setslope(searchsector, YAX_CEILING, sector[searchsector].ceilingheinum);
        setslope(searchsector, YAX_FLOOR, sector[searchsector].floorheinum);

        asksave = 1;
        m32_clipping = i;
    }


    getzrange(&pos, cursectnum, &hiz, &hihit, &loz, &lohit, 128, CLIPMASK0);

    if (PRESSED_KEYSC(CAPS) || (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(Z)))  // CAPS LOCK
    {
        zmode = (zmode+1)%3;
        if (zmode == 1)
            zlock = (loz-pos.z)&0xfffffc00;
        switch (zmode)
        {
        case 0: message("Zmode = Gravity"); break;
        case 1: message("Zmode = Locked/Sector"); break;
        case 2: message("Zmode = Locked/Free"); break;
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(M))  // 'M
    {
        if (ASSERT_AIMING)
        {
#ifdef YAX_ENABLE__COMPAT
            if (AIMING_AT_WALL_OR_MASK && yax_getnextwall(searchwall, YAX_FLOOR)>=0)
                message("Can't change extra in protected wall");
            else
#endif
            {
                Bsprintf(tempbuf, "%s extra: ", Typestr_wss[searchstat]);
                getnumberptr256(tempbuf, &AIMED(extra), sizeof(int16_t), BTAG_MAX, 1, NULL);
                asksave = 1;
            }
        }
    }

    if (PRESSED_KEYSC(1) && ASSERT_AIMING)  // 1 (make 1-way wall)
    {
        if (!AIMING_AT_SPRITE)
        {
            wall[searchwall].cstat ^= 32;
            message("Wall %d one side masking bit %s", searchwall, ONOFF(wall[searchwall].cstat&32));
        }
        else
        {
            i = sprite[searchwall].cstat;
            i ^= 64;
            if ((i&48) == 32)
            {
                i &= ~8;
                if ((i&64) && pos.z>sprite[searchwall].z)
                    i |= 8;
            }
            message("Sprite %d one sided bit %s", searchwall, ONOFF(i&64));
            sprite[searchwall].cstat = i;
        }
        asksave = 1;
    }

    if (PRESSED_KEYSC(2))  // 2 (bottom wall swapping)
    {
        if (!AIMING_AT_SPRITE && searchwall>=0)
        {
            wall[searchwall].cstat ^= 2;
            message("Wall %d bottom texture swap bit %s", searchwall, ONOFF(wall[searchwall].cstat&2));
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(O))  // O (top/bottom orientation - for doors)
    {
        if (AIMING_AT_WALL_OR_MASK)
        {
            int16_t w = SELECT_WALL();
            wall[w].cstat ^= 4;
            message("Wall %d %s orientation", w, wall[w].cstat&4?"bottom":"top");
            asksave = 1;
        }
        else if (AIMING_AT_SPRITE)   // O (ornament onto wall) (2D)
        {
            DoSpriteOrnament(searchwall);
            message("Sprite %d ornament onto wall", searchwall);
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(M))  // M (masking walls)
    {
        if (!AIMING_AT_SPRITE && ASSERT_AIMING)
        {
            int16_t next = wall[searchwall].nextwall;

            if (next >= 0)
            {
                wall[searchwall].cstat ^= 16;
                message("Wall %d masking bit %s", searchwall, ONOFF(wall[searchwall].cstat&16));

                wall[searchwall].cstat &= ~8;

                if (wall[searchwall].cstat&16)
                {
                    if (!eitherSHIFT)
                    {
                        wall[next].cstat |= 8;           //auto other-side flip
                        wall[next].cstat |= 16;
                        wall[next].overpicnum = wall[searchwall].overpicnum;
                    }
                }
                else
                {
                    if (!eitherSHIFT)
                    {
                        wall[next].cstat &= ~8;         //auto other-side unflip
                        wall[next].cstat &= ~16;
                    }
                }

                wall[searchwall].cstat &= ~32;
                if (!eitherSHIFT)
                    wall[next].cstat &= ~32;

                asksave = 1;
            }
        }
    }

    if (PRESSED_KEYSC(H))  // H (hitscan sensitivity)
    {
        if (keystatus[KEYSC_QUOTE])
        {
            if (ASSERT_AIMING)
            {
                j = 0;
                if (AIMING_AT_WALL || AIMING_AT_SPRITE)
                {
                    j = taglab_linktags(AIMING_AT_SPRITE, searchwall);
                    j = 2*(j&2);
                }

                Bsprintf(tempbuf, "%s hitag: ", Typestr_wss[searchstat]);
                getnumberptr256(tempbuf, &AIMED(hitag), sizeof(int16_t), BTAG_MAX, 0+j, NULL);
            }
        }
        else
        {
            if (AIMING_AT_SPRITE)
            {
                sprite[searchwall].cstat ^= 256;
                message("Sprite %d hitscan sensitivity bit %s", searchwall, ONOFF(sprite[searchwall].cstat&256));
                asksave = 1;
            }
            else if (AIMING_AT_WALL_OR_MASK || AIMING_AT_CEILING_OR_FLOOR)
            {
#ifdef YAX_ENABLE
                if (AIMING_AT_CEILING_OR_FLOOR && yax_getbunch(searchsector, AIMING_AT_FLOOR)>=0)
                {
                    SECTORFLD(searchsector,stat, AIMING_AT_FLOOR) ^= 2048;
                    message("Sector %d's %s hitscan sensitivity bit %s", searchsector, typestr[searchstat],
                            ONOFF(SECTORFLD(searchsector,stat, AIMING_AT_FLOOR)&2048));
                    asksave = 1;
                }
                else
#endif
                {
                    wall[searchwall].cstat ^= 64;

                    if (wall[searchwall].nextwall >= 0 && !eitherSHIFT)
                    {
                        NEXTWALL(searchwall).cstat &= ~64;
                        NEXTWALL(searchwall).cstat |= (wall[searchwall].cstat&64);
                    }

                    message("Wall %d hitscan sensitivity bit %s%s", searchwall,
                            ONOFF(wall[searchwall].cstat&64), eitherSHIFT?" (one-sided)":"");
                    asksave = 1;
                }
            }
        }
    }

#include "../../build/include/blood_rr_editor_mlook_shade_shared.cpp.inc"

    // PK: lmb only & mousewheel, -, and +, cycle picnum
    if (keystatus[KEYSC_DASH] || keystatus[KEYSC_EQUAL] || (bstatus&(16|32) && (bstatus&1) && !(bstatus&2)))
    {
        if (ASSERT_AIMING)
        {
            int32_t pic = AIMED_SELOVR_PICNUM;
            int32_t dir = (keystatus[KEYSC_EQUAL] || (bstatus&16)) ? 1 : -1;

            do
            {
                pic += dir + MAXTILES;
                pic %= MAXTILES;
            }
            while (tilesiz[pic].x<=0 || tilesiz[pic].y<=0);
            AIMED_SELOVR_PICNUM = pic;

            if (AIMING_AT_SPRITE)
                correct_sprite_yoffset(searchwall);

            asksave = 1;
        }
        keystatus[KEYSC_DASH] = keystatus[KEYSC_EQUAL] = 0;
        g_mouseBits &= ~(16|32);
    }

    if (PRESSED_KEYSC(E))  // E (expand)
    {
        if (AIMING_AT_CEILING_OR_FLOOR)
        {
            AIMED_CEILINGFLOOR(stat) ^= 8;
            message("Sector %d %s texture expansion bit %s", searchsector, typestr[searchstat],
                    ONOFF(AIMED_CEILINGFLOOR(stat)&8));
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(R))  // R (relative alignment, rotation)
    {
        if (keystatus[KEYSC_QUOTE]) // FRAMERATE TOGGLE
        {
            framerateon = !framerateon;
            message("Show framerate %s", ONOFF(framerateon));
        }
        else
        {
            if (AIMING_AT_CEILING_OR_FLOOR)
            {
                AIMED_CEILINGFLOOR(stat) ^= 64;
                message("Sector %d %s texture relativity bit %s", searchsector, typestr[searchstat],
                        ONOFF(AIMED_CEILINGFLOOR(stat)&64));
                asksave = 1;
            }
            else if (AIMING_AT_SPRITE)
                toggle_sprite_alignment(searchwall);
        }
    }

#include "../../build/include/blood_rr_editor_flip_shared.cpp.inc"


#include "../../build/include/blood_rr_editor_z_movement_shared.cpp.inc"


#include "../../build/include/blood_rr_editor_post_mapster_overlay_shared.cpp.inc"

    if (keystatus[buildkeys[BK_MODE2D_3D]])  // Enter
    {
        SetGamePalette(BASEPAL);
        FX_StopAllSounds();
        S_ClearSoundLocks();

#ifdef POLYMER
        DeletePolymerLights();
#endif
    }

    //Stick this in 3D part of ExtCheckKeys
    //Also choose your own key scan codes

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(D)) // ' d
    {
        skill = (skill+1)%MAXSKILL;
        message("%s", SKILLMODE[skill]);
    }

    if (keystatus[KEYSC_I])
    {
        keystatus[KEYSC_I] = 0;

        if (keystatus[KEYSC_QUOTE])  // ' i
        {
            if (AIMING_AT_SPRITE)
            {
                sprite[searchwall].cstat ^= 32768;
                message("Sprite %d made %svisible", searchwall, (sprite[searchwall].cstat&32768) ? "in":"");
            }
        }
        else
        {
            showinvisibility = !showinvisibility;
#if !defined YAX_ENABLE
            message("Show invisible sprites %s", showinvisibility?"enabled":"disabled");
#else
            message("Show invisible objects %s", showinvisibility?"enabled":"disabled");
#endif
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(X)) // ' x
    {
        shadepreview = !shadepreview;
        message("Map shade preview %s", shadepreview?"enabled":"disabled");

#ifdef POLYMER
        DeletePolymerLights();
#endif
    }
///___unused_keys___
    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(W)) // ' w
    {
        nosprites = (nosprites+1)%MAXNOSPRITES;
        message("%s", SPRDSPMODE[nosprites]);
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(Y)) // ' y
    {
        purpleon = !purpleon;
        if (nosprites>3) nosprites=0;
        message("Purple %s", ONOFF(purpleon));
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(C)) // ' C
    {
        if (AIMING_AT_WALL_OR_MASK)
        {
            for (i=0; i<MAXWALLS; i++)
                if (wall[i].picnum==temppicnum)
                    wall[i].shade=tempshade;
        }
        else if (AIMING_AT_CEILING_OR_FLOOR)
        {
            for (i=0; i<MAXSECTORS; i++)
                if (CEILINGFLOOR(i, picnum)==temppicnum)
                    CEILINGFLOOR(i, shade)=tempshade;
        }
        else if (AIMING_AT_SPRITE)
        {
            for (i=0; i<MAXSPRITES; i++)
                if (sprite[i].picnum==temppicnum)
                    sprite[i].shade=tempshade;
        }

        if (ASSERT_AIMING)
        {
            message("%ss with picnum %d now have shade of %d", Typestr[searchstat], temppicnum, tempshade);
            asksave = 1;
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(T)) // ' T
    {
        j = 0;
        if (AIMING_AT_WALL || AIMING_AT_SPRITE)
        {
            j = taglab_linktags(AIMING_AT_SPRITE, searchwall);
            j = 4*(j&1);
        }

        if (AIMING_AT_WALL_OR_MASK)
        {
#ifdef YAX_ENABLE__COMPAT
            if (yax_getnextwall(searchwall, YAX_CEILING)>=0)
                message("Can't change lotag in protected wall");
            else
#endif
            wall[searchwall].lotag = getnumber256("Wall lotag: ", wall[searchwall].lotag, BTAG_MAX, 0+j);
        }
        else if (AIMING_AT_CEILING_OR_FLOOR)
        {
            sector[searchsector].lotag =
                _getnumber256("Sector lotag: ", sector[searchsector].lotag, BTAG_MAX, 0, &ExtGetSectorType);
        }
        else if (AIMING_AT_SPRITE)
        {
            if (sprite[searchwall].picnum == SECTOREFFECTOR)
            {
                sprite[searchwall].lotag =
                    _getnumber256("Sprite lotag: ", sprite[searchwall].lotag, BTAG_MAX, 0+j, &SectorEffectorTagText);
            }
            else if (sprite[searchwall].picnum == MUSICANDSFX)
            {
                int16_t oldtag = sprite[searchwall].lotag;

                sprite[searchwall].lotag =
                    _getnumber256("Sprite lotag: ", sprite[searchwall].lotag, BTAG_MAX, 0+j, &MusicAndSFXTagText);

                if (testbit(g_ambiencePlaying, searchwall) && sprite[searchwall].lotag != oldtag)
                {
                    clearbit(g_ambiencePlaying, searchwall);
                    S_StopEnvSound(oldtag, searchwall);
                }
            }
            else
                sprite[searchwall].lotag = getnumber256("Sprite lotag: ", sprite[searchwall].lotag, BTAG_MAX, 0+j);
        }
    }

#define BLOOD_RR_EDITOR_WALL_PASTE_CSTAT_MASK (4 + 1+64 + 8+256)
#include "../../build/include/blood_rr_editor_key_actions_pan_tab_shared.cpp.inc"
#undef BLOOD_RR_EDITOR_WALL_PASTE_CSTAT_MASK
#if M32_UNDO
    if (eitherCTRL && PRESSED_KEYSC(Z)) // CTRL+Z
    {
        if (eitherSHIFT)
        {
            if (map_undoredo(1)) message("Nothing to redo!");
            else message("Redo: restored revision %d", map_revision-1);
        }
        else
        {
            if (map_undoredo(0)) message("Nothing to undo!");
            else message("Undo: restored revision %d", map_revision-1);
        }
    }
#endif
    if (keystatus[KEYSC_TAB])  //TAB
    {
        if (eitherCTRL)
        {
            g_fillCurSector = !g_fillCurSector;
            silentmessage("Fill currently pointed-at sector: %s", ONOFF(g_fillCurSector));
            keystatus[KEYSC_TAB] = 0;
        }
        else if (eitherSHIFT)
        {
            if (pointhighlight >= 16384)
            {
                drawgradient();
                showspritedata(pointhighlight&16383, 0);
            }
            else if (linehighlight >= 0 /* && ((bstatus&1) || sectorofwall(linehighlight)==tcursectornum)*/)
            {
                drawgradient();
                showwalldata(linehighlight, 0);
            }
        }
        else if (tcursectornum >= 0)
        {
            drawgradient();
            showsectordata(tcursectornum, 0);
        }
    }
    else if (!(keystatus[KEYSC_F5]|keystatus[KEYSC_F6]|keystatus[KEYSC_F7]|keystatus[KEYSC_F8]) && !eitherSHIFT)
    {
        static int32_t counter = 0;
        static int32_t omx = 0, omy = 0;
        /*
          static int32_t opointhighlight, olinehighlight, ocursectornum;
          if (pointhighlight == opointhighlight && linehighlight == olinehighlight && tcursectornum == ocursectornum)
        */
        if (omx == mousxplc && omy == mousyplc)
        {
            if (counter < 6)
                counter++;
        }
        else if (counter > 0)
            counter--;

        omx = mousxplc;
        omy = mousyplc;

        /*
          opointhighlight = pointhighlight;
          olinehighlight = linehighlight;
          ocursectornum = tcursectornum;
        */

        if (totalclock < lastpm16time + 120*2)
            _printmessage16("%s", lastpm16buf);
        else if (counter >= 2 && totalclock >= 120*6)
        {
            if (pointhighlight >= 16384)
            {
                i = pointhighlight-16384;
                showspritedata(i, 1);

                if (sprite[i].picnum==SECTOREFFECTOR)
                    _printmessage16("^10%s", SectorEffectorText(i));
            }
            else if (linehighlight >= 0 && ((bstatus&1) || sectorofwall(linehighlight)==tcursectornum))
                showwalldata(linehighlight, 1);
            else if (tcursectornum >= 0)
                showsectordata(tcursectornum, 1);
        }
    }

///__bigcomment__

    if ((i=tcursectornum)>=0 && g_fillCurSector && (hlsectorbitmap[i>>3]&(1<<(i&7)))==0)
    {
        int32_t col = editorcolors[4];
#ifdef YAX_ENABLE
        if (yax_getbunch(tcursectornum, YAX_FLOOR)>=0)
            col = editorcolors[12];
#endif
        fillsector(tcursectornum, col);
    }

#ifdef YAX_ENABLE
    if (eitherCTRL && PRESSED_KEYSC(U) && tcursectornum>=0)  // Ctrl-U: unlink bunch sectors
    {
        int16_t cf, fb = yax_getbunch(tcursectornum, YAX_FLOOR);
        if (fb >= 0)
        {
            for (SECTORS_OF_BUNCH(fb,YAX_FLOOR, i))
                fillsector_notrans(i, editorcolors[11]);
            fade_editor_screen(editorcolors[11]);

            if (ask_if_sure("Clear all TROR extensions from marked sectors?", 0))
            {
                for (cf=0; cf<2; cf++)
                    for (SECTORS_OF_BUNCH(fb,cf, i))
                        yax_setbunch(i, cf, -1);

                yax_update(0);
                yax_updategrays(pos.z);

                message("Cleared TROR bunch %d", fb);
                asksave = 1;
            }
        }
    }

    if (/*!m32_sideview &&*/ numyaxbunches>0)
    {
        int32_t zsign=0;

        if (PRESSED_KEYSC(PGDN) || (eitherCTRL && PRESSED_KEYSC(DOWN)))
            zsign = 1;
        else if (PRESSED_KEYSC(PGUP) || (eitherCTRL && PRESSED_KEYSC(UP)))
            zsign = -1;

        if (zsign)
        {
            int32_t bestzdiff=INT32_MAX, hiz=0, loz=0, bottomp=0;

            for (i=0; i<numsectors; i++)
            {
                if (yax_getbunch(i, YAX_FLOOR) < 0 /*&& yax_getbunch(i, YAX_CEILING) < 0*/)
                    continue;

                loz = min(loz, TrackerCast(sector[i].floorz));
                hiz = max(hiz, TrackerCast(sector[i].floorz));

                // TODO: see if at least one sector point inside sceeen
                j = (sector[i].floorz-pos.z)*zsign;
                if (j>0 && j < bestzdiff)
                    bestzdiff = j;
            }

            if (bestzdiff==INT32_MAX)
            {
                if (zsign == 1)
                    bottomp=1, bestzdiff = (hiz+(1024<<4) - pos.z);
                else
                    bestzdiff = pos.z - loz;
            }

            pos.z += zsign*bestzdiff;
            yax_updategrays(pos.z);

            printmessage16("Z position: %d%s", pos.z,
                           bottomp ? " (bottom)":(pos.z==loz ? " (top)":""));
            updatesectorz(pos.x, pos.y, pos.z, &cursectnum);
        }
    }

    if (eitherCTRL && PRESSED_KEYSC(A))
    {
        if (eitherALT)
        {
            showinnergray = !showinnergray;
            printmessage16("Display grayed out walls: %s", ONOFF(showinnergray));
        }
        else
        {
            autogray = !autogray;
            printmessage16("Automatic grayout of plain sectors %s", ONOFF(autogray));
            yax_updategrays(pos.z);
        }
    }
#endif

    // Ctrl-R set editor z range to hightlightsectors' c/f bounds
    if (eitherCTRL && PRESSED_KEYSC(R))
    {
        if (highlightsectorcnt <= 0)
        {
            editorzrange[0] = INT32_MIN;
            editorzrange[1] = INT32_MAX;
            printmessage16("Reset Z range");
        }
        else
        {
            int32_t damin=INT32_MAX, damax=INT32_MIN;

            for (i=0; i<highlightsectorcnt; i++)
            {
                damin = min(damin, TrackerCast(sector[highlightsector[i]].ceilingz));
                damax = max(damax, TrackerCast(sector[highlightsector[i]].floorz));
            }

            if (damin < damax)
            {
                editorzrange[0] = damin;
                editorzrange[1] = damax;
                printmessage16("Set Z range to highlighted sector bounds (%d..%d)",
                               editorzrange[0], editorzrange[1]);
            }
        }
        yax_updategrays(pos.z);
    }

#include "../../build/include/duke_rr_editor_2d_tag_shared.cpp.inc"
    if (graphicsmode)
    {
        for (k=0; k<2; k++)    // panning/repeat
        {
            if (k==0)
                j = (keystatus[KEYSC_gLEFT]<<1)|keystatus[KEYSC_gRIGHT];  // 4 & 6 (keypad 2D)
            else
                j = (keystatus[KEYSC_gUP]<<1)|keystatus[KEYSC_gDOWN];  // 2 & 8 (keypad 2D)

            if (j)
            {
                smooshy = keystatus[KEYSC_gKP5];

                if (repeatcnt[k] == 0 || repeatcnt[k] > 32)
                {
                    changedir = 1-(j&2);

                    if ((ppointhighlight&0xc000) == 16384 && (sprite[cursprite].cstat & 48))
                    {
                        uint8_t *repeat = (k==0) ? &sprite[cursprite].xrepeat : &sprite[cursprite].yrepeat;
                        *repeat = max<uint8_t>(4, changechar(*repeat, changedir, smooshy, 1));
                        silentmessage("Sprite %d repeat: %d, %d", cursprite,
                                      TrackerCast(sprite[cursprite].xrepeat),
                                      TrackerCast(sprite[cursprite].yrepeat));
                    }
                    else
                    {
                        i = tcursectornum;

                        if (i >= 0)
#ifdef YAX_ENABLE
                            if (k==1 || yax_getbunch(i, YAX_FLOOR) < 0)
#endif
                            {
                                uint8_t *panning = (k==0) ? &sector[i].floorxpanning : &sector[i].floorypanning;
                                *panning = changechar(*panning, changedir, smooshy, 0);
                                silentmessage("Sector %d floor panning: %d, %d", searchsector,
                                              TrackerCast(sector[i].floorxpanning),
                                              TrackerCast(sector[i].floorypanning));
                            }
                    }

                    asksave = 1;
                    repeatcnt[k] = max(1,repeatcnt[k]-2);
                }
                repeatcnt[k] += synctics;
            }
            else
                repeatcnt[k] = 0;
        }
    }

#include "../../build/include/duke_rr_editor_2d_relative_search_shared.cpp.inc"
    if (PRESSED_KEYSC(G))  // G (grid on/off)
    {
        if (autogrid)
        {
            grid = 8*eitherSHIFT;

            autogrid = 0;
        }
        else
        {
            grid += (1-2*eitherSHIFT);
            if (grid == -1 || grid == 9)
            {
                autogrid = 1;
                grid = 0;
            }
        }

        if (autogrid)
            printmessage16("Grid size: 9 (autosize)");
        else if (!grid)
            printmessage16("Grid off");
        else
            printmessage16("Grid size: %d (%d units)", grid, 2048>>grid);
    }

    if (autogrid)
    {
        grid = -1;

        while (grid++ < 7)
        {
            if (mulscale14((2048>>grid), zoom) <= 16)
                break;
        }
    }


    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(L)) // ' L  (set sprite/wall coordinates)
    {
        if (pointhighlight >= 16384)
        {
            i = pointhighlight - 16384;
            Bsprintf(tempbuf, "Sprite %d x: ", i);
            sprite[i].x = getnumber16(tempbuf, sprite[i].x, editorgridextent-1, 1);
            Bsprintf(tempbuf, "Sprite %d y: ", i);
            sprite[i].y = getnumber16(tempbuf, sprite[i].y, editorgridextent-1, 1);
            Bsprintf(tempbuf, "Sprite %d z: ", i);
            sprite[i].z = getnumber16(tempbuf, sprite[i].z, BZ_MAX, 1);
            Bsprintf(tempbuf, "Sprite %d angle: ", i);
            sprite[i].ang = getnumber16(tempbuf, sprite[i].ang, 2048, 1);
            sprite[i].ang &= 2047;
            printmessage16("Sprite %d updated", i);
        }
        else if (pointhighlight >= 0)
        {
            i = linehighlight;
            j = wall[i].x;
            k = wall[i].y;

            Bsprintf(tempbuf, "Wall %d x: ", i);
            j = getnumber16(tempbuf, j, editorgridextent, 1);
            Bsprintf(tempbuf, "Wall %d y: ", i);
            k = getnumber16(tempbuf, k, editorgridextent, 1);
            dragpoint(i, j, k, 0);
            printmessage16("Wall %d updated", i);
        }
    }


    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(3)) // ' 3
    {
        onnames = (onnames+1)%M32_NUM_SPRITE_MODES;
        printmessage16("Mode %d %s", onnames, SpriteMode[onnames]);
    }
    //   Ver();

///__motorcycle___

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(7)) // ' 7 : swap hilo
    {

        if (pointhighlight >= 16384)
        {
            swapshort(&sprite[cursprite].lotag, &sprite[cursprite].hitag);
            printmessage16("Sprite %d tags swapped", cursprite);
        }
        else if (linehighlight >= 0)
        {
#ifdef YAX_ENABLE
            if (yax_getnextwall(linehighlight, YAX_CEILING)>=0 || yax_getnextwall(searchwall, YAX_FLOOR)>=0)
                message("Can't swap tags in protected wall");
            else
#endif
            {
                swapshort(&wall[linehighlight].lotag, &wall[linehighlight].hitag);
                printmessage16("Wall %d tags swapped", linehighlight);
            }
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(J)) // ' J
    {
        char dachars[4] = {'s', 'w', 'i', 'c'};

        fade_editor_screen(-1);
        i = editor_ask_function("Jump to (s)ector, (w)all, spr(i)te, or (c)oordinates?", dachars, 4);

        switch (i)
        {
        case 0:
            if (numsectors > 0)
            {
                j = getnumber16("Sector: ", 0, numsectors-1, 0+8);
                if (j < 0)
                    break;
                pos.x = wall[sector[j].wallptr].x;
                pos.y = wall[sector[j].wallptr].y;
                pos.z = sector[j].floorz;
                printmessage16("Current pos now on sector %d's first wall-point", j);
            }
            break;
        case 1:
            if (numwalls > 0)
            {
                j = getnumber16("Wall: ", 0, numwalls-1, 0+8);
                if (j < 0)
                    break;
                pos.x = wall[j].x + (wall[wall[j].point2].x-wall[j].x)/4;
                pos.y = wall[j].y + (wall[wall[j].point2].y-wall[j].y)/4;
                pos.z = getflorzofslope(sectorofwall(j), pos.x, pos.y);
                printmessage16("Current pos now on wall %d's midpoint", j);
            }
            break;
        case 2:
            j = getnumber16("Sprite: ", 0, MAXSPRITES-1, 0+8);
            if (j < 0 || sprite[j].statnum==MAXSTATUS)
                break;
            pos.x = sprite[j].x;
            pos.y = sprite[j].y;
            pos.z = sprite[j].z;
            printmessage16("Current pos now on sprite %d", j);
            break;

        case 3:
            pos.x = getnumber16("X-coordinate:    ", pos.x, editorgridextent, 1);
            pos.y = getnumber16("Y-coordinate:    ", pos.y, editorgridextent, 1);
            printmessage16("Current pos now (%d, %d)", pos.x, pos.y);
            break;
        }
#ifdef YAX_ENABLE
        yax_updategrays(pos.z);
#endif
    }
}// end key2d

static void InitCustomColors(void)
{
    int32_t i;
    palette_t *edcol;

    /* blue */
    vgapal16[9*4+0] = 252;
    vgapal16[9*4+1] = 124;
    vgapal16[9*4+2] = 28;

    /* orange */
    vgapal16[31*4+0] = 80; // blue
    vgapal16[31*4+1] = 180; // green
    vgapal16[31*4+2] = 240; // red

    // UNUSED?
    vgapal16[39*4+0] = 144;
    vgapal16[39*4+1] = 212;
    vgapal16[39*4+2] = 252;


    /* light yellow */
    vgapal16[22*4+0] = 204;
    vgapal16[22*4+1] = 252;
    vgapal16[22*4+2] = 252;

    /* grey */
    vgapal16[23*4+0] = 180;
    vgapal16[23*4+1] = 180;
    vgapal16[23*4+2] = 180;

    /* blue */
    vgapal16[24*4+0] = 204;
    vgapal16[24*4+1] = 164;
    vgapal16[24*4+2] = 48;

    vgapal16[32*4+0] = 240;
    vgapal16[32*4+1] = 200;
    vgapal16[32*4+2] = 84;

    // grid color
    vgapal16[25*4+0] = 64;
    vgapal16[25*4+1] = 56;
    vgapal16[25*4+2] = 56;

    vgapal16[26*4+0] = 96;
    vgapal16[26*4+1] = 96;
    vgapal16[26*4+2] = 96;

    // UNUSED?
    vgapal16[33*4+0] = 0; //60; // blue
    vgapal16[33*4+1] = 0; //120; // green
    vgapal16[33*4+2] = 192; //180; // red

    // UNUSED?
    vgapal16[41*4+0] = 0; //96;
    vgapal16[41*4+1] = 0; //160;
    vgapal16[41*4+2] = 252; //192;

    for (i = 0; i<256; i++)
    {
        if (editorcolors[i] == 0)
        {
            edcol = (palette_t *)&vgapal16[4*i];
            editorcolors[i] = paletteGetClosestColorUpToIndex(edcol->b, edcol->g, edcol->r, 239);
        }
    }
}

int32_t ExtPreSaveMap(void)
{
    int32_t numfixedsprites;

    numfixedsprites = fixspritesectors();   //Do this before saving!
    updatesectorz(startpos.x,startpos.y,startpos.z,&startsectnum);
    if (startsectnum < 0)
        updatesector(startpos.x,startpos.y,&startsectnum);

    if (fixmaponsave_walls)
    {
        int32_t i, startwall, j, endwall;

        for (i=0; i<numsectors; i++)
        {
            startwall = sector[i].wallptr;
            for (j=startwall; j<numwalls; j++)
                if (wall[j].point2 < startwall)
                    startwall = wall[j].point2;
            if (sector[i].wallptr != startwall)
                initprintf("Warning: set sector %d's wallptr to %d (was %d)\n", i,
                           TrackerCast(sector[i].wallptr), startwall);
            sector[i].wallptr = startwall;
        }

        for (i=numsectors-2; i>=0; i--)
            sector[i].wallnum = sector[i+1].wallptr-sector[i].wallptr;
        sector[numsectors-1].wallnum = numwalls - sector[numsectors-1].wallptr;

#ifdef YAX_ENABLE
        // setting redwalls from scratch would very likely wreak havoc with TROR maps
        if (numyaxbunches > 0)
            return numfixedsprites;
#endif
        for (i=0; i<numwalls; i++)
        {
            wall[i].nextsector = -1;
            wall[i].nextwall = -1;
        }
        for (i=0; i<numsectors; i++)
        {
            startwall = sector[i].wallptr;
            endwall = startwall + sector[i].wallnum;
            for (j=startwall; j<endwall; j++)
                checksectorpointer(j, i);
        }
    }

    return numfixedsprites;
}

#include "../../build/include/m32_show_parameter_help_shared.cpp.inc"


#define COPYARG(i) \
    Bmemcpy(&testplay_addparam[j], argv[i], lengths[i]); \
    j += lengths[i]; \
    testplay_addparam[j++] = ' ';

#if defined(RENDERTYPEWIN) && defined(USE_OPENGL)
extern char forcegl;
#endif

static void G_CheckCommandLine(int32_t argc, char const * const * argv)
{
    int32_t i = 1, j, maxlen=0, *lengths;
    const char *c, *k;

    mapster32_fullpath = argv[0];

#ifdef HAVE_CLIPSHAPE_FEATURE
    // pre-form the default 10 clipmaps
    for (j = '0'; j<='9'; ++j)
    {
        char clipshape[16] = "_clipshape0.map";

        clipshape[10] = j;
        g_clipMapFiles.append(Xstrdup(clipshape));
    }
#endif

    if (argc <= 1)
        return;

    lengths = (int32_t *)Xmalloc(argc*sizeof(int32_t));
    for (j=1; j<argc; j++)
    {
        lengths[j] = Bstrlen(argv[j]);
        maxlen += lengths[j];
    }

    testplay_addparam = (char *)Xmalloc(maxlen+argc);
    testplay_addparam[0] = 0;

    j = 0;

    while (i < argc)
    {
        c = argv[i];

        if ((*c == '-')
#ifdef _WIN32
            || (*c == '/')
#endif
)
        {
            if (!Bstrcasecmp(c+1,"?") || !Bstrcasecmp(c+1,"help") || !Bstrcasecmp(c+1,"-help"))
            {
                G_ShowParameterHelp();
                Bexit(0);
            }

            if (!Bstrcasecmp(c+1,"addon"))
            {
                if (argc > i+1)
                {
                    g_addonNum = Batoi(argv[i+1]);

                    if (!(g_addonNum > ADDON_NONE && g_addonNum < NUMADDONS))
                        g_addonNum = ADDON_NONE;

                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }

            if (!Bstrcasecmp(c+1, "g") || !Bstrcasecmp(c+1, "grp"))
            {
                if (argc > i+1)
                {
                    G_AddGroup(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }

            if (!Bstrcasecmp(c+1,"game_dir"))
            {
                if (argc > i+1)
                {
                    Bstrncpyz(g_modDir, argv[i+1], sizeof(g_modDir));

                    G_AddPath(argv[i+1]);

                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"cachesize"))
            {
                if (argc > i+1)
                {
                    int32_t sz = atoi_safe(argv[i+1]);
                    if (sz >= 16<<10 && sz <= 1024<<10)
                    {
                        g_maxCacheSize = sz<<10;
                        initprintf("Cache size: %dkB\n",sz);

                        COPYARG(i);
                        COPYARG(i+1);
                    }
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"cfg"))
            {
                if (argc > i+1)
                {
                    Bstrncpyz(setupfilename, argv[i+1], sizeof(setupfilename));
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"gamegrp"))
            {
                if (argc > i+1)
                {
                    clearGrpNamePtr();
                    g_grpNamePtr = dup_filename(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"namesfile"))
            {
                g_namesFileName = argv[i+1];
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"x"))
            {
                if (argc > i+1)
                {
                    G_AddCon(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"mx"))
            {
                if (argc > i+1)
                {
                    G_AddConModule(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"h"))
            {
                if (argc > i+1)
                {
                    G_AddDef(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"mh"))
            {
                if (argc > i+1)
                {
                    G_AddDefModule(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"j"))
            {
                if (argc > i+1)
                {
                    G_AddPath(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
#ifdef HAVE_CLIPSHAPE_FEATURE
            if (!Bstrcasecmp(c+1,"clipmap"))
            {
                if (argc > i+1)
                {
                    G_AddClipMap(argv[i+1]);
                    COPYARG(i);
                    COPYARG(i+1);
                    i++;
                }
                i++;
                continue;
            }
#endif
            if (!Bstrcasecmp(c+1,"nm") || !Bstrcasecmp(c+1,"ns"))
            {
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"rr"))
            {
                g_gameType = GAMEFLAG_RR;
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"rrra"))
            {
                g_gameType = GAMEFLAG_RR|GAMEFLAG_RRRA;
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"check"))
            {
                initprintf("Map wall checking on save enabled\n");
                fixmaponsave_walls = 1;
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"nocheck"))
            {
                initprintf("Map wall checking on save disabled\n");
                fixmaponsave_walls = 0;
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"noautoload"))
            {
                initprintf("Autoload disabled\n");
                NoAutoLoad = 1;
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"usecwd"))
            {
                g_useCwd = 1;
                COPYARG(i);
                i++;
                continue;
            }
#if defined(RENDERTYPEWIN) && defined(USE_OPENGL)
            if (!Bstrcasecmp(c+1,"forcegl"))
            {
                forcegl = 1;
                i++;
                continue;
            }
#endif
        }


        if ((*c == '-')
#ifdef _WIN32
            || (*c == '/')
#endif
)
        {
            c++;
            switch (*c)
            {
            case 'h':
            case 'H':
                c++;
                if (*c)
                {
                    G_AddDef(c);
                    COPYARG(i);
                }
                break;
            case 'j':
            case 'J':
                c++;
                if (!*c) break;
                G_AddPath(c);
                COPYARG(i);
                break;
            case 'g':
            case 'G':
                c++;
                if (!*c) break;
                G_AddGroup(c);
                COPYARG(i);
                break;
            case 'x':
            case 'X':
                c++;
                if (*c)
                {
                    G_AddCon(c);
                    COPYARG(i);
                }
                break;
            }
        }
        else
        {
            k = Bstrrchr(c,'.');
            if (k)
            {
                if (!Bstrcasecmp(k,".map"))
                {
                    B_SetBoardFileName(argv[i++]);
                    continue;
                }
                else if (!Bstrcasecmp(k,".grp") || !Bstrcasecmp(k,".zip"))
                {
                    COPYARG(i);
                    G_AddGroup(argv[i++]);
                    continue;
                }
                else if (!Bstrcasecmp(k,".def"))
                {
                    COPYARG(i);
                    G_AddDef(argv[i++]);
                    continue;
                }
                else if (!Bstrcasecmp(k,".con"))
                {
                    COPYARG(i);
                    G_AddCon(argv[i++]);
                    continue;
                }
            }
        }
        i++;
    }

    Bfree(lengths);

    if (j > 0)
    {
        testplay_addparam[j-1] = 0;
        testplay_addparam = (char *)Xrealloc(testplay_addparam, j*sizeof(char));
    }
    else
    {
        DO_FREE_AND_NULL(testplay_addparam);
    }

}
#undef COPYARG

#if defined(_WIN32) && defined(DEBUGGINGAIDS)
// See FILENAME_CASE_CHECK in cache1d.c
static int32_t check_filename_casing(void)
{
    return 1;
}
#endif

int32_t ExtPreInit(int32_t argc,char const * const * argv)
{
#if defined(_WIN32) && defined(DEBUGGINGAIDS)
    {
        extern int32_t (*check_filename_casing_fn)(void);
        check_filename_casing_fn = check_filename_casing;
    }
#endif

    G_ExtPreInit(argc, argv);

    OSD_SetLogFile("mapster32.log");
    OSD_SetVersion("Mapster32",0,2);
    initprintf("Mapster32 %s\n", s_buildRev);
    PrintBuildInfo();

    G_CheckCommandLine(argc,argv);

    return 0;
}

static int32_t osdcmd_quit(osdfuncparm_t const * const UNUSED(parm))
{
    UNREFERENCED_CONST_PARAMETER(parm);

    OSD_ShowDisplay(0);

    ExtUnInit();
    engineUnInit();

    Bfflush(NULL);

    Bexit(0);
}

static int32_t osdcmd_editorgridextent(osdfuncparm_t const * const parm)
{
    int32_t i;

    if (parm->numparms == 0)
    {
        OSD_Printf("\"editorgridextent\" is \"%d\"\n", editorgridextent);
        return OSDCMD_SHOWHELP;
    }
    else if (parm->numparms != 1)
        return OSDCMD_SHOWHELP;

    i = Batol(parm->parms[0]);

    if (i >= 65536 && i <= BXY_MAX)
    {
        editorgridextent = i;
        OSD_Printf("editorgridextent %d\n", editorgridextent);
    }
    else
        OSD_Printf("editorgridextent: value out of range (65536 to %d)\n", BXY_MAX);

    return OSDCMD_OK;
}

#define M32_OSD_COMMAND_RETURN_TYPE int32_t
#define M32_OSD_COMMAND_PARAM osdfuncparm_t const * const
#include "../../build/include/mapster_osd_command_bodies_shared.cpp.inc"

#ifdef USE_OPENGL
static int32_t osdcmd_tint(osdfuncparm_t const * const parm)
{
    int32_t i;
    polytint_t *p;

    if (parm->numparms==1)
    {
        i = atoi_safe(parm->parms[0]);
        if (i>=0 && i<=M32_MAXPALOOKUPS)
        {
            p = &hictinting[i];
            OSD_Printf("pal %d: r=%d g=%d b=%d f=%d\n", i, p->r, p->g, p->b, p->f);
        }
    }
    else if (parm->numparms==0)
    {
        OSD_Printf("Hightile tintings:\n");
        for (i=0,p=&hictinting[0]; i<=M32_MAXPALOOKUPS; i++,p++)
            if (p->r != 255 || p->g != 255 || p->b != 255 || p->f != 0)
                OSD_Printf("pal %d: rgb %3d %3d %3d  f %d\n", i, p->r, p->g, p->b, p->f);
    }
    else if (parm->numparms>=2)
    {
        i = atoi_safe(parm->parms[0]);
        if (i<0 || i>M32_MAXPALOOKUPS)
            return OSDCMD_SHOWHELP;

        p = &hictinting[i];
        p->r = atoi_safe(parm->parms[1]);
        p->g = (parm->numparms>=3) ? atoi_safe(parm->parms[2]) : 255;
        p->b = (parm->numparms>=4) ? atoi_safe(parm->parms[3]) : 255;
        p->f = (parm->numparms>=5) ? atoi_safe(parm->parms[4]) : 0;
    }
    return OSDCMD_OK;
}
#endif

static void SaveInHistory(const char *commandstr)
{
    int32_t i, idx, dosave=1;

    // If running with osdtryscript=1, we could receive e.g.
    //  "// this file is automatically generated by Mapster32"
    // from m32_config.cfg here.
    if (!Bstrncmp(commandstr, "//", 2))
        return;

    for (i=1; i<=4; i++)
    {
        idx = (scripthistend-i)&(SCRIPTHISTSIZ-1);
        if (!scripthist[idx])
            break;
        else if (!Bstrcmp(scripthist[idx], commandstr))
        {
            dosave = 0;
            break;
        }
    }

    if (dosave)
    {
        Bfree(scripthist[scripthistend]);
        scripthist[scripthistend] = Xstrdup(commandstr);
        scripthistend++;
        scripthistend %= SCRIPTHISTSIZ;
    }
}

// M32 script vvv
static int32_t osdcmd_include(osdfuncparm_t const * const parm)
{
    if (parm->numparms != 1)
        return OSDCMD_SHOWHELP;
    C_Compile(parm->parms[0], 1);
    return OSDCMD_OK;
}

static int32_t osdcmd_scriptinfo(osdfuncparm_t const * const UNUSED(parm))
{
    UNREFERENCED_CONST_PARAMETER(parm);
    C_CompilationInfo();
    return OSDCMD_OK;
}

#ifdef DEBUGGINGAIDS
extern void X_Disasm(ofstype beg, int32_t size);

static int32_t osdcmd_disasm(osdfuncparm_t const * const parm)
{
    int32_t i;

    if (parm->numparms != 2)
        return OSDCMD_SHOWHELP;

    if (!isdigit(parm->parms[1][0]))
        return OSDCMD_SHOWHELP;

    i=atoi_safe(parm->parms[1]);

    if (parm->parms[0][0]=='s')
    {
        if (i>=0 && i<g_stateCount)
            X_Disasm(statesinfo[i].ofs, statesinfo[i].codesize);
    }
    else
    {
        if (i>=0 && i<MAXEVENTS && aEventOffsets[i]>=0)
            X_Disasm(aEventOffsets[i], aEventSizes[i]);
    }
    return OSDCMD_OK;
}
#endif

static int32_t osdcmd_do(osdfuncparm_t const * const parm)
{
    if (parm->numparms==0)
        return OSDCMD_SHOWHELP;

    int32_t onumconstants = g_numSavedConstants;

    intptr_t oscrofs = (g_scriptPtr-apScript);
    int32_t  ofs     = 2 * (parm->numparms > 0);  // true if "do" command
    int32_t  slen    = Bstrlen(parm->raw + ofs);
    char *   tp      = (char *)Xmalloc(slen+2);

    Bmemcpy(tp, parm->raw+ofs, slen);

    // M32script call from 'special functions' menu
    int32_t dontsavehist = (slen == 0 || tp[0] == ' ');

    // needed so that subsequent commands won't execute old stuff.
    tp[slen]   = '\n';
    tp[slen+1] = '\0';

    g_didDefineSomething = 0;

    C_Compile(tp, 0);

    if (parm->numparms >= 0)
        DO_FREE_AND_NULL(tp);

    if (g_numCompilerErrors)
    {
//        g_scriptPtr = script + oscrofs;  // handled in C_Compile()
        Bfree(tp);
        return OSDCMD_OK;
    }

    if (g_didDefineSomething == 0)
    {
        g_numSavedConstants = onumconstants;

        *g_scriptPtr = CON_RETURN + (g_lineNumber<<12);
        g_scriptPtr  = apScript + oscrofs;

        insptr = apScript + oscrofs;
        Bmemcpy(&vm, &vm_default, sizeof(vmstate_t));

        if (in3dmode() && AIMING_AT_SPRITE)
        {
            vm.spriteNum = searchwall;
            vm.pSprite   = &sprite[vm.spriteNum];
        }

        // If OSD is down, that would interfere with user input, so don't consider
        // m32script executed from the console as 'interactive'. Which leaves only
        // that from the 'special functions' menu
        if (OSD_GetRowsCur() < 0)
            vm.miscflags |= VMFLAG_MISC_INTERACTIVE;

        VM_Execute(0);

        M32_PostScriptExec();

        if (!(vm.flags&VMFLAG_ERROR) && !dontsavehist)
            SaveInHistory(parm->raw);

        OSD_Printf(OSDTEXT_GREEN "%s\n", parm->raw);
//        asksave = 1; // handled in Access(Sprite|Sector|Wall)
    }

    return OSDCMD_OK;
}

void M32RunScript(const char *s)
{
    osdfuncparm_t parm;

    parm.numparms = -1;
    parm.raw = s;

    osdcmd_do(&parm);
}

static int32_t osdcmd_endisableevent(osdfuncparm_t const * const parm)
{
    int32_t i, j, enable;

    if (!label) return OSDCMD_OK;

    if (parm->numparms < 1)
    {
        OSD_Printf("--- Defined events:\n");
        for (i=0; i<MAXEVENTS; i++)
            if (aEventOffsets[i] >= 0)
                OSD_Printf("%s (%d): %s\n", label+(i*MAXLABELLEN), i, aEventEnabled[i]?"on":"off");
        return OSDCMD_OK;
    }

    enable = !Bstrcasecmp(parm->name, "enableevent");

    if (parm->numparms == 1)
    {
        if (!Bstrcasecmp(parm->parms[0], "all") || !Bstrcasecmp(parm->parms[0], "a"))
        {
            for (i=0; i<MAXEVENTS; i++)
                aEventEnabled[i] = enable?1:0;
            OSD_Printf("%sabled all events.\n", enable?"En":"Dis");
            return OSDCMD_OK;
        }
    }

    for (i=0; i<parm->numparms; i++)
    {
        char buf[64] = "EVENT_", buf2[64];

        if (isdigit(parm->parms[i][0]))
        {
            j = atoi_safe(parm->parms[i]);
            Bsprintf(buf2, "event %d", j);
        }
        else if (!Bstrncmp(parm->parms[i], "EVENT_", 6))
        {
            j = hash_find(&h_labels, parm->parms[i]);
            Bstrncpyz(buf2, parm->parms[i], sizeof(buf2));
        }
        else
        {
            Bstrncat(buf, parm->parms[i], sizeof(buf)-6-1);
            j = hash_find(&h_labels, buf);
            Bmemcpy(buf2, buf, sizeof(buf2));
        }

        if (j>=0 && j<MAXEVENTS)
        {
            aEventEnabled[j] = enable?1:0;
            OSD_Printf("%sabled %s.\n", enable?"En":"Dis", buf2);
        }
        else
            OSD_Printf("Invalid event %s.\n", buf2);
    }
    return OSDCMD_OK;
}

static int32_t registerosdcommands(void)
{
#define MAPSTER_SHARED_REGISTER_OSD_COMMANDS
#include "../../build/include/mapster_osd_commands_shared.cpp.inc"
    return 0;
}


enum
{
    T_INCLUDE = 0,
    T_DEFINE = 1,
    T_LOADGRP,
    T_TILEGROUP,
    T_TILE,
    T_TILERANGE,
    T_HOTKEY,
    T_TILES,
    T_NOAUTOLOAD,
    T_COLORS,

    T_ALPHABET,
    T_MAP,
    T_MAPA,
    T_MAPRANGE,
    T_MAPRANGEA,
    T_OFFSET,
    T_OFFSETA,

    T_DEFINESOUND,
    T_INCLUDEDEFAULT,

    T_GLOBALGAMEFLAGS,

    T_GAMESTARTUP,

    T_DUMMY,
};

static int32_t parsegroupfiles(scriptfile *script);

static void parsegroupfiles_include(const char *fn, scriptfile *script, const char *cmdtokptr)
{
    scriptfile *included = scriptfile_fromfile(fn);

    if (!included)
    {
        if (!Bstrcasecmp(cmdtokptr,"null"))
            initprintf("Warning: Failed including %s as module\n", fn);
        else
            initprintf("Warning: Failed including %s on line %s:%d\n",
                       fn, script->filename,scriptfile_getlinum(script,cmdtokptr));
    }
    else
    {
        parsegroupfiles(included);
        scriptfile_close(included);
    }
}

static int32_t parsegroupfiles(scriptfile *script)
{
    int32_t tokn;
    char *cmdtokptr;

    tokenlist grptokens[] =
    {
        { "include",         T_INCLUDE },
        { "#include",        T_INCLUDE },
        { "includedefault",  T_INCLUDEDEFAULT },
        { "#includedefault", T_INCLUDEDEFAULT },
        { "loadgrp",         T_LOADGRP },
        { "noautoload",      T_NOAUTOLOAD },
        { "globalgameflags", T_GLOBALGAMEFLAGS },
    };

    while (1)
    {
        tokn = getatoken(script,grptokens,ARRAY_SIZE(grptokens));
        cmdtokptr = script->ltextptr;
        switch (tokn)
        {
        case T_LOADGRP:
        {
            char *fn;
            int opathsearchmode = pathsearchmode;

            pathsearchmode = 1;
            if (!scriptfile_getstring(script,&fn))
            {
                int32_t j = initgroupfile(fn);

                if (j == -1)
                    initprintf("Could not find group file \"%s\".\n",fn);
                else
                {
                    initprintf("Using group file \"%s\".\n",fn);
                    if (!NoAutoLoad)
                        G_DoAutoload(fn);
                }

            }
            pathsearchmode = opathsearchmode;
        }
        break;
        case T_INCLUDE:
        {
            char *fn;
            if (!scriptfile_getstring(script,&fn))
                parsegroupfiles_include(fn, script, cmdtokptr);
            break;
        }
        break;
        case T_INCLUDEDEFAULT:
        {
            parsegroupfiles_include(G_DefaultDefFile(), script, cmdtokptr);
            break;
        }
        break;
        case T_NOAUTOLOAD:
            NoAutoLoad = 1;
            break;
        case T_GLOBALGAMEFLAGS:
        {
            if (scriptfile_getnumber(script,&duke3d_m32_globalflags)) break;
        }
        break;
        case T_EOF:
            return 0;
        default:
            break;
        }
    }
    return 0;
}

#include "../../build/include/m32_loaddefinitions_game_shared.cpp.inc"

#define M32_TILEGROUP_WARN_INCLUDE(fn, script, cmdtokptr) \
    initprintf("Warning: Failed including %s on line %s:%d\n", (fn), (script)->filename, scriptfile_getlinum((script), (cmdtokptr)))
#define M32_TILEGROUP_WARN_DEFINE(name, number, script, cmdtokptr) \
    initprintf("Warning: Symbol %s was NOT redefined to %d on line %s:%d\n", (name), (number), (script)->filename, scriptfile_getlinum((script), (cmdtokptr)))
#include "../../build/include/blood_rr_parsetilegroups_shared.cpp.inc"
#undef M32_TILEGROUP_WARN_INCLUDE
#undef M32_TILEGROUP_WARN_DEFINE

static int32_t loadtilegroups(const char *fn)
{
    int32_t i, j;
    scriptfile *script;
    TileGroup blank = { NULL, 0, NULL, 0, 0, 0, 0};

    script = scriptfile_fromfile(fn);
    if (!script) return -1;

    for (i=0; i<tile_groups; i++)
    {
        Bfree(s_TileGroups[i].pIds);
        Bfree(s_TileGroups[i].szText);
        Bmemcpy(&s_TileGroups[i], &blank, sizeof(blank));
    }
    tile_groups = 0;
#if 0
    // ---------- Init hardcoded tile group consisting of all named tiles
    s_TileGroups[0].szText = Xstrdup("All named");
    s_TileGroups[0].pIds = (int32_t *)Xmalloc(MAXTILES * sizeof(s_TileGroups[0].pIds[0]));

    j = 0;
    for (i=0; i<MAXTILES; i++)
        if (names[i][0])
            s_TileGroups[0].pIds[j++] = i;
    if (j)
    {
        s_TileGroups[0].nIds = j;
        s_TileGroups[0].key1 = 'Y';
        s_TileGroups[0].key2 = 'y';
        tile_groups++;
    }
    // --------------------
#endif
    parsetilegroups(script);

    scriptfile_close(script);
    scriptfile_clearsymbols();

    tilegroupItems = getTileGroup("Items");
    tilegroupActors = getTileGroup("Actors");

    // Apply 2d sprite colors as specified in tiles.cfg.
    for (i=0; i<tile_groups; i++)
    {
        // If the colors were specified...
        if (s_TileGroups[i].color1 && s_TileGroups[i].color2)
        {
            // Apply the colors to all tiles in the group...
            for (j = s_TileGroups[i].nIds-1; j >= 0 ; j--)
            {
                int const tilenum = s_TileGroups[i].pIds[j];

                // ... but for each tile, only if no color has been specified
                // for it previously.
                if (spritecol2d[tilenum][0] == 0)
                {
                    spritecol2d[tilenum][0] = s_TileGroups[i].color1;
                    spritecol2d[tilenum][1] = s_TileGroups[i].color2;
                }
            }
        }
    }

    return 0;
}

/// vvv Parse CON files partially to get sound definitions
static int32_t parseconsounds(scriptfile *script);

static void parseconsounds_include(const char *fn, scriptfile *script, const char *cmdtokptr)
{
    scriptfile *included;

    included = scriptfile_fromfile(fn);
    if (!included)
    {
        if (!Bstrcasecmp(cmdtokptr,"null"))
            initprintf("Warning: Failed including %s as module\n", fn);
        else
            initprintf("Warning: Failed including %s on line %s:%d\n",
                       fn, script->filename,scriptfile_getlinum(script,cmdtokptr));
    }
    else
    {
        parseconsounds(included);
        scriptfile_close(included);
/*
        // why?
        int32_t tmp = parseconsounds(included);
        scriptfile_close(included);
        if (tmp < 0) return tmp;
*/
    }
}

static int32_t parseconsounds(scriptfile *script)
{
    int32_t tokn;
    char *cmdtokptr;
    int32_t num_invalidsounds=0;

    tokenlist cstokens[] =
    {
        { "include",         T_INCLUDE          },
        { "#include",        T_INCLUDE          },
        { "includedefault",  T_INCLUDEDEFAULT   },
        { "#includedefault", T_INCLUDEDEFAULT   },
        { "define",          T_DEFINE           },
        { "#define",         T_DEFINE           },
        { "definesound",     T_DEFINESOUND      },
        { "gamestartup",     T_GAMESTARTUP      },
    };

    while (1)
    {
        tokn = getatoken(script,cstokens,ARRAY_SIZE(cstokens));
        cmdtokptr = script->ltextptr;
        switch (tokn)
        {
        case T_INCLUDE:
        {
            char *fn;
            if (!scriptfile_getstring(script,&fn))
                parseconsounds_include(fn, script, cmdtokptr);
            break;
        }
        case T_INCLUDEDEFAULT:
        {
            parseconsounds_include(G_DefaultConFile(), script, cmdtokptr);
            break;
        }
        case T_DEFINE:
        {
            char *name;
            int32_t number;

            if (scriptfile_getstring(script,&name)) break;
            if (scriptfile_getsymbol(script,&number)) break;
            if (scriptfile_addsymbolvalue(name,number) < 0)
                initprintf("Warning: Symbol %s was NOT redefined to %d on line %s:%d\n",
                           name,number,script->filename,scriptfile_getlinum(script,cmdtokptr));
            break;
        }
        case T_GAMESTARTUP:
        {
            if (scriptfile_getsymbol(script, &g_visibility)) break;
            break;
        }
        case T_DEFINESOUND:
        {
            char *definedname, *filename;
            int32_t sndnum, ps, pe, pr, m, vo;
            int32_t slen, duplicate=0;

            if (scriptfile_getsymbol(script, &sndnum)) break;

            definedname = Xstrdup(script->ltextptr);

            if (sndnum < 0 || sndnum >= MAXSOUNDS)
            {
                initprintf("Warning: invalid sound definition %s (sound number < 0 or >= MAXSOUNDS) on line %s:%d\n",
                           definedname, script->filename,scriptfile_getlinum(script,cmdtokptr));
                Bfree(definedname);
                num_invalidsounds++;
                break;
            }

            if (scriptfile_getstring(script, &filename))
            {
                Bfree(definedname);
                num_invalidsounds++;
                break;
            }

            slen = Bstrlen(filename);
            if (slen >= BMAX_PATH)
            {
                initprintf("Warning: invalid sound definition %s (filename too long) on line %s:%d\n",
                           definedname, script->filename,scriptfile_getlinum(script,cmdtokptr));
                Bfree(definedname);
                num_invalidsounds++;
                break;
            }

            if (g_sounds[sndnum].filename)
            {
                duplicate = 1;
                Bfree(g_sounds[sndnum].filename);
            }
            g_sounds[sndnum].filename = (char *)Xcalloc(slen+1,sizeof(uint8_t));
            // Hopefully noone does memcpy(..., g_sounds[].filename, BMAX_PATH)

            Bmemcpy(g_sounds[sndnum].filename, filename, slen+1);

            if (scriptfile_getnumber(script, &ps)) goto BAD;
            if (scriptfile_getnumber(script, &pe)) goto BAD;
            if (scriptfile_getnumber(script, &pr)) goto BAD;
            if (scriptfile_getnumber(script, &m)) goto BAD;
            if (ParentalLock && (m&8)) goto BAD;
            if (scriptfile_getnumber(script, &vo)) goto BAD;
            if (0)
            {
BAD:
                Bfree(definedname);
                DO_FREE_AND_NULL(g_sounds[sndnum].filename);
                num_invalidsounds++;
                break;
            }

            if (g_sounds[sndnum].definedname)
            {
                duplicate = 1;
                Bfree(g_sounds[sndnum].definedname);
            }
            if (duplicate)
                initprintf("warning: duplicate sound #%d, overwriting\n", sndnum);

            g_sounds[sndnum].definedname = definedname;  // we want to keep it for display purposes
            g_sounds[sndnum].ps = ps;
            g_sounds[sndnum].pe = pe;
            g_sounds[sndnum].pr = pr;
            g_sounds[sndnum].m = m;
            g_sounds[sndnum].vo = vo;
            if (!duplicate)
            {
                g_sndnum[g_numsounds] = g_definedsndnum[g_numsounds] = sndnum;
                g_numsounds++;
                if (g_numsounds == MAXSOUNDS)
                    goto END;
            }
            break;
        }
        case T_EOF:
            goto END;
        default:
            break;
        }
    }
END:
    return g_numsounds;
}

static int32_t loadconsounds(const char *fn)
{
    scriptfile *script;
    int32_t ret;

    initprintf("Loading sounds from \"%s\"\n",fn);

    script = scriptfile_fromfile(fn);
    if (!script)
    {
        initprintf("Error loading sounds: file \"%s\" not found.\n", fn);
        return -1;
    }
    ret = parseconsounds(script);

    for (char * m : g_scriptModules)
    {
        parseconsounds_include(m, NULL, "null");
        free(m);
    }
    g_scriptModules.clear();

    if (ret < 0)
        initprintf("There was an error parsing \"%s\".\n", fn);
    else if (ret == 0)
        initprintf("\"%s\" doesn't contain sound definitions. No sounds loaded.\n", fn);
    else
        initprintf("Loaded %d sound definitions.\n", ret);

    if (g_visibility != 512)
        initprintf("Global visibility: %d\n", g_visibility);

    scriptfile_close(script);
    scriptfile_clearsymbols();
    return ret;
}

void ExtPreLoadMap(void)
{
}

/// ^^^

static void m32script_interrupt_handler(int signo)
{
    if (signo==SIGINT)
    {
        vm.flags |= VMFLAG_ERROR;
        OSD_Printf("M32 script execution interrupted.\n");
        Bmemset(aEventEnabled, 0, sizeof(aEventEnabled));
    }
}

static void M32_HandleMemErr(int32_t bytes, int32_t lineNum, const char *fileName, const char *funcName)
{
#ifdef DEBUGGINGAIDS
    debug_break();
    Bsprintf(tempbuf, "Out of memory: failed allocating %d bytes at %s:%d (%s)!", bytes, fileName, lineNum, funcName);
#else
    UNREFERENCED_PARAMETER(lineNum);
    UNREFERENCED_PARAMETER(fileName);
    UNREFERENCED_PARAMETER(funcName);
    Bsprintf(tempbuf, "Out of memory: failed allocating %d bytes!", bytes);
#endif
    fatal_exit(tempbuf);
}


int32_t ExtInit(void)
{
    int32_t rv = 0;

    OSD_SetParameters(0, 2, 0, 0, 4, 0, OSD_ERROR, OSDTEXT_YELLOW, OSDTEXT_RED, 0);

    set_memerr_handler(&M32_HandleMemErr);

    if (!g_useCwd)
        G_AddSearchPaths();

    G_ExtInit();

    bpp = 32;

//#ifdef USE_OPENGL
    if (Bstrcmp(setupfilename, SETUPFILENAME))
        initprintf("Using config file \"%s\".\n",setupfilename);

    if (loadsetup(setupfilename) < 0)
        initprintf("Configuration file not found, using defaults.\n"), rv = 1;
//#endif
    Bmemcpy(buildkeys, default_buildkeys, NUMBUILDKEYS);   //Trick to make build use setup.dat keys

    kensplayerheight = 40; //32
    zmode = 2;
    zlock = kensplayerheight<<8;

    showinvisibility = 1;

    getmessageleng = 0;
    getmessagetimeoff = 0;

    Bsprintf(apptitle, "Mapster32 %s", s_buildRev);
    autosavetimer = (int32_t) totalclock+120*autosave;

    registerosdcommands();

    {
        char *ptr = Xstrdup(setupfilename), *p = strtok(ptr,".");
        if (!Bstrcmp(setupfilename, SETUPFILENAME))
            Bsprintf(tempbuf, "m32_settings.cfg");
        else Bsprintf(tempbuf,"%s_m32_settings.cfg",p);
        OSD_Exec(tempbuf);
        Bfree(ptr);
    }

    // backup pathsearchmode so that a later open
    // will hopefully be the same file
    pathsearchmode_oninit = pathsearchmode;

    G_ScanGroups();

    signal(SIGINT, m32script_interrupt_handler);

    return rv;
}

int32_t ExtPostStartupWindow(void)
{
    G_LoadGroups(!NoAutoLoad);

    if (!g_useCwd)
        G_CleanupSearchPaths();

    if (engineInit())
    {
        initprintf("There was a problem initializing the engine.\n");
        return -1;
    }

    G_LoadLookups();

    loadtilegroups(default_tiles_cfg);

    ReadHelpFile("m32help.hlp");

    G_InitMultiPsky(CLOUDYOCEAN, MOONSKY1, BIGORBIT1, LA);

    return 0;
}

void ExtPostInit(void)
{
    InitCustomColors();

    if (!(duke3d_m32_globalflags & DUKE3D_NO_PALETTE_CHANGES))
    {
        // Make base shade table at shade 0 into the identity map.
        // (In the shade table of Duke3D's PALETTE.DAT, palookup[0][239]==143.)
        // This makes it possible to sensibly use Lunatic's engine.saveLookupDat().
        palookup[0][239] = 239;
    }

    if (!(duke3d_m32_globalflags & DUKE3D_NO_HARDCODED_FOGPALS))
        paletteSetupDefaultFog();

    palettePostLoadLookups();
}

void ExtUnInit(void)
{
//    int32_t i;
    // setvmode(0x03);
    writesetup(setupfilename);

    S_SoundShutdown();
    uninitgroupfile();
#if 0
    for (i = MAX_TILE_GROUPS-1; i >= 0; i--)
    {
        Bfree(s_TileGroups[i].pIds);
        Bfree(s_TileGroups[i].szText);
    }
    for (i = numhelppages-1; i >= 0; i--) Bfree(helppage[i]);
    Bfree(helppage);
#endif

    Duke_CommonCleanup();
}

void ExtPreCheckKeys(void) // just before drawrooms
{
    int32_t i = 0, ii;

    if (in3dmode())
    {
        if (shadepreview)
        {
            for (i=0; i<highlightsectorcnt; i++)
            {
                ii = highlightsector[i];
                sectorpals[ii][0] = sector[ii].floorpal;
                sectorpals[ii][1] = sector[ii].ceilingpal;

                sector[ii].floorpal = sector[ii].ceilingpal = 6;
            }

//            int32_t i = 0;
            for (i=0; i<MAXSPRITES; i++)
            {
                if (sprite[i].statnum==MAXSTATUS)
                    continue;

                if (sprite[i].picnum == SECTOREFFECTOR &&
                        (sprite[i].lotag == 12 || sprite[i].lotag == 3 || sprite[i].lotag == 4))
                {
                    int32_t w, isec=sprite[i].sectnum;
                    int32_t start_wall;
                    int32_t end_wall;

                    if (isec<0)
                        continue;

                    start_wall = sector[isec].wallptr;
                    end_wall = start_wall + sector[isec].wallnum;

                    for (w = start_wall; w < end_wall; w++)
                    {
                        if (!(wallflag[w>>3]&(1<<(w&7))))
                        {
                            wallshades[w] = wall[w].shade;
                            wallpals[w] = wall[w].pal;

                            wall[w].shade = sprite[i].shade;
                            wall[w].pal = sprite[i].pal;

                            wallflag[w>>3] |= (1<<(w&7));
                        }
                        // removed: same thing with nextwalls
                    }
                    sectorshades[isec][0] = sector[isec].floorshade;
                    sectorshades[isec][1] = sector[isec].ceilingshade;
                    sector[isec].floorshade = sprite[i].shade;
                    sector[isec].ceilingshade = sprite[i].shade;

                    sectorpals[isec][0] = sector[isec].floorpal;
                    sectorpals[isec][1] = sector[isec].ceilingpal;
                    sector[isec].floorpal = sprite[i].pal;
                    sector[isec].ceilingpal = sprite[i].pal;

                    for (w = headspritesect[isec]; w >= 0; w = nextspritesect[w])
                    {
                        if (w == i)
                            continue;

                        spriteshades[w] = sprite[w].shade;
                        spritepals[w] = sprite[w].pal;

                        sprite[w].shade = sprite[i].shade;
                        sprite[w].pal = sprite[i].pal;
                    }
                }
                else if (sprite[i].picnum == SECTOREFFECTOR && (sprite[i].lotag == 49 || sprite[i].lotag == 50))
                {
#ifdef POLYMER
                    if (sprite[i].lotag == 49)
                    {
                        if (videoGetRenderMode() == REND_POLYMER)
                        {
                            if (spritelightptr[i] == NULL)
                            {
#pragma pack(push,1)
                                _prlight mylight;
#pragma pack(pop)
                                addprlight_common1(&mylight, i);
                            }
                            else
                            {
                                if (Bmemcmp(&sprite[i], spritelightptr[i], sizeof(vec3_t)))
                                {
                                    Bmemcpy(spritelightptr[i], &sprite[i], sizeof(vec3_t));
                                    spritelightptr[i]->sector = sprite[i].sectnum;
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if (SHT(i) != spritelightptr[i]->range)
                                {
                                    spritelightptr[i]->range = SHT(i);
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if (check_prlight_colors(i))
                                    copy_prlight_colors(spritelightptr[i], i);
                                if ((int)!!(CS(i) & 128) != spritelightptr[i]->publicflags.negative)
                                {
                                    spritelightptr[i]->publicflags.negative = !!(CS(i) & 128);
                                }
                            }
                        }
                    }
                    if (sprite[i].lotag == 50)
                    {
                        if (videoGetRenderMode() == REND_POLYMER)
                        {
                            if (spritelightptr[i] == NULL)
                            {
#pragma pack(push,1)
                                _prlight mylight;
#pragma pack(pop)
                                mylight.radius = (256-(SS(i)+128))<<1;
                                mylight.faderadius = (int16_t)(mylight.radius * 0.75f);
                                mylight.tilenum = OW(i);
                                mylight.publicflags.emitshadow = !(CS(i) & 64);

                                addprlight_common1(&mylight, i);
                            }
                            else
                            {
                                if (Bmemcmp(&sprite[i], spritelightptr[i], sizeof(vec3_t)))
                                {
                                    Bmemcpy(spritelightptr[i], &sprite[i], sizeof(vec3_t));
                                    spritelightptr[i]->sector = sprite[i].sectnum;
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if (SHT(i) != spritelightptr[i]->range)
                                {
                                    spritelightptr[i]->range = SHT(i);
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if (check_prlight_colors(i))
                                    copy_prlight_colors(spritelightptr[i], i);
                                if (((256-(SS(i)+128))<<1) != spritelightptr[i]->radius)
                                {
                                    spritelightptr[i]->radius = (256-(SS(i)+128))<<1;
                                    spritelightptr[i]->faderadius = (int16_t)(spritelightptr[i]->radius * 0.75f);
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if (SA(i) != spritelightptr[i]->angle)
                                {
                                    spritelightptr[i]->angle = SA(i);
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if (SH(i) != spritelightptr[i]->horiz)
                                {
                                    spritelightptr[i]->horiz = SH(i);
                                    spritelightptr[i]->flags.invalidate = 1;
                                }
                                if ((int)!(CS(i) & 64) != spritelightptr[i]->publicflags.emitshadow)
                                {
                                    spritelightptr[i]->publicflags.emitshadow = !(CS(i) & 64);
                                }
                                if ((int)!!(CS(i) & 128) != spritelightptr[i]->publicflags.negative)
                                {
                                    spritelightptr[i]->publicflags.negative = !!(CS(i) & 128);
                                }
                                spritelightptr[i]->tilenum = OW(i);
                            }
                        }
                    }
#endif // POLYMER
                }
            }
        }

        if (floor_over_floor) SE40Code(pos.x,pos.y,pos.z,ang,horiz);
        if (purpleon) videoClearViewableArea(255);

        return;
    }


    videoBeginDrawing();  //{{{

    //    if (cursectornum >= 0)
    //        fillsector(cursectornum, 31);

    if (graphicsmode && (!m32_sideview || m32_sideelev == 512) && zoom >= 256)
    {
        for (i=ii=0; i<MAXSPRITES && ii < Numsprites; i++)
        {
            int32_t daang = 0, flags, shade;
            int32_t picnum, frames;
            int32_t xp1, yp1;

            if ((sprite[i].cstat & 48) != 0 || sprite[i].statnum == MAXSTATUS) continue;
            ii++;
            picnum = sprite[i].picnum;
            daang = flags = frames = shade = 0;

            switch (picnum)
            {
                // 5-frame walk
            case 1550 :             // Shark
                frames=5;
                fallthrough__;
                // 2-frame walk
            case 1445 :             // duke kick
            case LIZTROOPDUCKING :
            case 2030 :            // pig shot
            case OCTABRAIN :
            case PIGCOPDIVE :
            case 2190 :            // liz capt shot
            case BOSS1SHOOT :
            case BOSS1LOB :
            case LIZTROOPSHOOT :
                if (frames==0) frames=2;
                fallthrough__;
                // 4-frame walk
            case 1491 :             // duke crawl
            case LIZTROOP :
            case LIZTROOPRUNNING :
            case PIGCOP :
            case LIZMAN :
            case BOSS1 :
            case BOSS2 :
            case BOSS3 :
            case BOSS4 :
            case NEWBEAST:
                if (frames==0) frames=4;
                fallthrough__;
            case LIZTROOPJETPACK :
            case DRONE :
            case COMMANDER :
            case TANK :
            case RECON :
                if (frames==0) frames = 10;
                fallthrough__;
            case CAMERA1:
            case APLAYER :
                if (frames==0) frames=1;
                fallthrough__;
            case GREENSLIME :
            case EGG :
            case PIGCOPSTAYPUT :
            case LIZMANSTAYPUT:
            case LIZTROOPSTAYPUT :
            case LIZMANSPITTING :
            case LIZMANFEEDING :
            case LIZMANJUMP :
            case NEWBEASTSTAYPUT :
            case BOSS1STAYPUT :
            {
                int32_t k;
                if (frames!=0)
                {
                    if (frames==10) frames=0;
                    k = 1536;//getangle(tspr->x-pos.x,tspr->y-pos.y);
                    k = (((sprite[i].ang+3072+128-k+(m32_sideview ? (2048 - m32_sideang) : 0))&2047)>>8)&7;
                    //This guy has only 5 pictures for 8 angles (3 are x-flipped)
                    if (k <= 4)
                    {
                        picnum += k;
                        daang = 0;
                        flags &= ~4;
                    }
                    else
                    {
                        picnum += 8-k;
                        daang = 1024;
                        flags |= 4;
                    }
                }

                if (graphicsmode == 2)
                {
                    if (frames==2) picnum+=((((4-((int32_t) totalclock>>5)))&1)*5);
                    if (frames==4) picnum+=((((4-((int32_t) totalclock>>5)))&3)*5);
                    if (frames==5) picnum+=((((int32_t) totalclock>>5)%5))*5;
                }

                if (tilesiz[picnum].x == 0)
                    picnum -= 5;       //Hack, for actors
            }
            break;
            default:
                break;
            }

            shade = (i+16384 == pointhighlight) ? 7 - (M32_THROB>>1) : sprite[i].shade;

            if (m32_sideview)
            {
                editorGet2dScreenCoordinates(&xp1, &yp1, sprite[i].x-pos.x, sprite[i].y-pos.y, zoom);
                yp1 += midydim16 + getscreenvdisp(sprite[i].z-pos.z, zoom);
                yp1 -= mulscale14(tilesiz[picnum].y<<2, zoom);
                xp1 += halfxdim16;
            }
            else
                ovhscrcoords(sprite[i].x, sprite[i].y-(tilesiz[picnum].y<<2), &xp1, &yp1);

            ydim16 = ydim - STATUS2DSIZ2;  // XXX?

            int f = mulscale12(128, zoom);

            if (xp1 < -f || xp1 > xdim+f || yp1 < -f || yp1 > ydim16+f)
                continue;

            const int32_t oviewingrange=viewingrange, oyxaspect=yxaspect;
            renderSetAspect(yxaspect, divscale16(sprite[i].yrepeat, sprite[i].xrepeat));
            rotatesprite(xp1<<16,yp1<<16,zoom<<5,daang,picnum,
                         shade,sprite[i].pal,flags|1024,0,0,xdim-1,ydim16-1);
            renderSetAspect(oviewingrange, oyxaspect);
        }
    }

    if (showambiencesounds)
    {
        for (ii=0; ii<numsectors; ii++)
            for (i=headspritesect[ii]; i>=0; i=nextspritesect[i])
            {
                int32_t radius, col;
                int32_t xp1, yp1;

                if (sprite[i].picnum != MUSICANDSFX /*|| zoom < 256*/ || sprite[i].hitag < 1000)
                    continue;

                if (showambiencesounds==1 && sprite[i].sectnum!=cursectnum)
                    continue;

                editorGet2dScreenCoordinates(&xp1,&yp1, sprite[i].x-pos.x,sprite[i].y-pos.y, zoom);
                if (m32_sideview)
                    yp1 += getscreenvdisp(sprite[i].z-pos.z, zoom);

                drawlinepat = 0xf0f0f0f0;

                col = editorcolors[6];
                if (i+16384 == pointhighlight)
                {
                    radius = mulscale14(sprite[i].hitag - (M32_THROB<<2), zoom);
                    col += M32_THROB>>1;
                }
                else radius = mulscale14(sprite[i].hitag, zoom);

                editorDraw2dCircle(halfxdim16+xp1, midydim16+yp1, radius, scalescreeny(16384), col);
                drawlinepat = 0xffffffff;
            }
    }

    videoEndDrawing();  //}}}
}

void ExtAnalyzeSprites(int32_t ourx, int32_t oury, int32_t ourz, int32_t oura, int32_t smoothr)
{
    int32_t i, k;
    tspriteptr_t tspr;
    int32_t frames=0, sh;

    UNREFERENCED_PARAMETER(ourx);
    UNREFERENCED_PARAMETER(oury);
    UNREFERENCED_PARAMETER(ourz);
    UNREFERENCED_PARAMETER(oura);
    UNREFERENCED_PARAMETER(smoothr);

    for (i=0,tspr=&tsprite[0]; i<spritesortcnt; i++,tspr++)
    {
        frames=0;

        if ((nosprites==1||nosprites==3) && tspr->picnum<11)
            tspr->xrepeat=0;

        if (nosprites==1||nosprites==3)
            switch (tspr->picnum)
            {
            case SEENINE :
                tspr->xrepeat=0;
            }

        if (showinvisibility && (tspr->cstat&32768))
        {
            tspr->pal = 6;
            tspr->cstat &= (uint16_t)~32768;
            tspr->cstat |= 2+512;
        }

        /* Shade preview rules (thanks to Gambini)
         *
         * 1st rule: Any pal value not equal to 0 in the floor of a sector will
         *           turn all the sprites within this sector to that pal value.
         *
         * 2nd rule: The shade of a sprite will be taken from the floor unless the
         *           ceiling is parallaxed, in which case will be taken from the
         *           ceiling. But not the pal which always follow the 1st rule.
         *
         * 3rd rule: relative to wall sprites will keep their own shade unless
         *           they're actors, but they will still retain the floor pal.
         */
        if (shadepreview)
        {
            if (tspr->sectnum<0)
                continue;

            const int32_t wallaligned = (tspr->cstat & 16);
            const int32_t fpal = sector[tspr->sectnum].floorpal;

            // 1st rule
            // Compare with game.c:G_MaybeTakeOnFloorPal()
            if (fpal && !g_noFloorPal[fpal])
                tspr->pal = fpal;

            // 2nd and 3rd rule minus "actor condition"
            if (!wallaligned && (tspr->cstat&CSTAT_SPRITE_NOSHADE)==0)
            {
                if (sector[tspr->sectnum].ceilingstat&1)
                    sh = sector[tspr->sectnum].ceilingshade;
                else
                    sh = sector[tspr->sectnum].floorshade;

                inpclamp(&sh, -127, 127);
                tspr->shade = sh;
            }
        }

        switch (tspr->picnum)
        {
            // 5-frame walk
        case 1550 :             // Shark
            frames=5;
            fallthrough__;
            // 2-frame walk
        case 1445 :             // duke kick
        case LIZTROOPDUCKING :
        case 2030 :            // pig shot
        case OCTABRAIN :
        case PIGCOPDIVE :
        case 2190 :            // liz capt shot
        case BOSS1SHOOT :
        case BOSS1LOB :
        case LIZTROOPSHOOT :
            if (frames==0) frames=2;
            fallthrough__;
            // 4-frame walk
        case 1491 :             // duke crawl
        case LIZTROOP :
        case LIZTROOPRUNNING :
        case PIGCOP :
        case LIZMAN :
        case BOSS1 :
        case BOSS2 :
        case BOSS3 :
        case BOSS4 :
        case NEWBEAST:
            if (frames==0) frames=4;
            fallthrough__;
        case LIZTROOPJETPACK :
        case DRONE :
        case COMMANDER :
        case TANK :
        case RECON :
            if (frames==0) frames = 10;
            fallthrough__;
        case ROTATEGUN :
        case CAMERA1:
        case APLAYER :
            if (frames==0) frames=1;
            fallthrough__;
        case GREENSLIME :
        case PIGCOPSTAYPUT :
        case LIZMANSTAYPUT:
        case LIZTROOPSTAYPUT :
        case LIZMANSPITTING :
        case LIZMANFEEDING :
        case LIZMANJUMP :
        case NEWBEASTSTAYPUT :
        case BOSS1STAYPUT :
            if (skill!=4)
            {
                if (tspr->lotag>skill+1)
                {
                    tspr->xrepeat=0;
                    tspr->cstat=32768;
                    break;
                }
            }
            if (nosprites==2||nosprites==3)
            {
                tspr->xrepeat=0;
                tspr->cstat=32768;
            }
            //                else tspr->cstat&=32767;

#ifdef USE_OPENGL
            if (!usemodels || md_tilehasmodel(tspr->picnum,tspr->pal) < 0)
#endif
            {
                if (frames!=0)
                {
                    if (frames==10) frames=0;
                    k = getangle(tspr->x-pos.x,tspr->y-pos.y);
                    k = (((tspr->ang+3072+128-k)&2047)>>8)&7;
                    //This guy has only 5 pictures for 8 angles (3 are x-flipped)
                    if (k <= 4)
                    {
                        tspr->picnum += k;
                        tspr->cstat &= ~4;   //clear x-flipping bit
                    }
                    else
                    {
                        tspr->picnum += 8-k;
                        tspr->cstat |= 4;    //set x-flipping bit
                    }
                }

                if (frames==2) tspr->picnum += (((4-((int32_t) totalclock>>5)))&1)*5;
                if (frames==4) tspr->picnum += (((4-((int32_t) totalclock>>5)))&3)*5;
                if (frames==5) tspr->picnum += (((int32_t) totalclock>>5)%5)*5;

                if (tilesiz[tspr->picnum].x == 0)
                    tspr->picnum -= 5;       //Hack, for actors
            }
            break;

        default:
            break;
        }
    }
}

#define MESSAGEX 3 // (xdimgame>>1)
#define MESSAGEY 3 // ((i/charsperline)<<3)+(ydimgame-(ydimgame>>3))-(((getmessageleng-1)/charsperline)<<3)

static void Keys2d3d(void)
{
    int32_t i;
#if M32_UNDO
    if (mapstate == NULL)
    {
        //        map_revision = 0;
        create_map_snapshot(); // initial map state
        //        Bfree(mapstate->next);
        //        mapstate = mapstate->prev;
    }
#endif

    if (cursectnum>=0 && sector[cursectnum].lotag==2)
    {
        if (sector[cursectnum].ceilingpicnum==FLOORSLIME)
            SetGamePalette(SLIMEPAL);
        else
            SetGamePalette(WATERPAL);
    }
    else SetGamePalette(BASEPAL);

    if (keystatus[KEYSC_F10])
    {
        keystatus[KEYSC_F10]=0;

        if (!in3dmode())
        {
            if (eitherSHIFT)
            {
                // shrinking the size while in a corner pulls the PIP into that corner
                if (m32_2d3d.x + XSIZE_2D3D >= xdim - 5)
                    m32_2d3d.x = 0xffff;

                if (m32_2d3d.y + YSIZE_2D3D >= ydim - 5 - STATUS2DSIZ2)
                    m32_2d3d.y = 0xffff;

                if (++m32_2d3dsize > 5)
                    m32_2d3dsize = 3;

                message("2d3d size %d", m32_2d3dsize);
            }
            else
            {
                m32_2d3dmode = !m32_2d3dmode;
                message("2d3d mode %s", m32_2d3dmode ? "enabled" : "disabled");
            }
        }
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(A)) // 'A
    {
        if (in3dmode())
            autosave = autosave?0:getnumber256("Autosave interval, in seconds: ",180,3600,0);
        else
            autosave = autosave?0:getnumber16("Autosave interval, in seconds: ",180,3600,0);

        if (autosave) message("Autosave enabled, interval: %d seconds",autosave);
        else message("Autosave disabled");
    }

    if (keystatus[KEYSC_QUOTE] && PRESSED_KEYSC(N)) // 'N
    {
        m32_clipping--;
        if (m32_clipping < 0)
            m32_clipping = 2;
        message("Clipping %s", m32_clipping==0 ? "disabled" :
                (m32_clipping==1 ? "non-masks only" : "enabled"));
    }

    if (eitherCTRL && PRESSED_KEYSC(N)) // CTRL+N
    {
        spnoclip = !spnoclip;
        message("Sprite clipping %s", spnoclip?"disabled":"enabled");
    }

    if (eitherCTRL)  //CTRL
    {
        if (!in3dmode())
            if (PRESSED_KEYSC(P)) // Ctrl-P: Map playtesting
                test_map(eitherALT);

        if (keystatus[KEYSC_S]) // S
        {
            if (levelname[0])
            {
                keystatus[KEYSC_S] = 0;

                i = CheckMapCorruption(4, 0);
                if (i<4)
                {
                    Bsprintf(tempbuf, "Save to %s?", levelname);
                    if (!AskIfSure(tempbuf))
                    {
                        if (SaveBoard(levelname, M32_SB_ASKOV) != NULL)
                        {
                            message("Board saved to %s", levelname);
                            asksave = 0;
                            lastsave=(int32_t) totalclock;
                        }
                    }
                }
                else
                    message("Map is heavily corrupted, not saving. See OSD for details.");
            }
        }
        if (keystatus[KEYSC_L]) // L
        {
            if (totalclock < (lastsave + 120*10) || !AskIfSure("Are you sure you want to load the last saved map?"))
            {
                int32_t sposx=pos.x,sposy=pos.y,sposz=pos.z,sang=ang;
                const char *f = GetSaveBoardFilename(levelname);

                lastsave=(int32_t) totalclock;
                //  			  sectorhighlightstat = -1;
                //  			  newnumwalls = -1;
                //  			  joinsector[0] = -1;
                //  			  circlewall = -1;
                //  			  circlepoints = 7;

                if (LoadBoard(f, 0))
                    message("Invalid map format.");

                pos.x=sposx; pos.y=sposy; pos.z=sposz; ang=sang;
                updatesectorz(pos.x, pos.y, pos.z, &cursectnum);

                keystatus[KEYSC_L] = 0;
            }
        }
    }

    if (keystatus[buildkeys[BK_MODE2D_3D]])  // Enter
    {
        getmessageleng = 0;
        getmessagetimeoff = 0;
    }

    if (getmessageleng > 0)
    {
        if (!in3dmode())
            printmessage16("%s", getmessage);
        if (totalclock > getmessagetimeoff)
            getmessageleng = 0;
    }

}

void ExtCheckKeys(void)
{
    static int32_t soundinit = 0;
    static int32_t lastbstatus = 0;

    if (!soundinit)
    {
        g_numsounds = 0;

        loadconsounds(G_ConFile());

        if (g_numsounds > 0)
        {
            if (S_SoundStartup() != 0)
                S_SoundShutdown();
        }
        soundinit = 1;
    }

    if (in3dmode() && shadepreview)
    {
        int32_t i = 0, ii;
        int32_t w, isec, start_wall, end_wall;

        for (i=0; i<highlightsectorcnt; i++)
        {
            ii = highlightsector[i];
            sector[ii].floorpal = sectorpals[ii][0];
            sector[ii].ceilingpal = sectorpals[ii][1];
        }

        for (i=0; i<MAXSPRITES; i++)
        {
            if (sprite[i].statnum==MAXSTATUS || sprite[i].picnum != SECTOREFFECTOR)
                continue;

            if (sprite[i].lotag != 12 && sprite[i].lotag != 3 && sprite[i].lotag != 4)
                continue;

            isec = sprite[i].sectnum;
            if (isec<0)
                continue;

            start_wall = sector[isec].wallptr;
            end_wall = start_wall + sector[isec].wallnum;

            for (w = start_wall; w < end_wall; w++)
            {
                if (wallflag[w>>3]&(1<<(w&7)))
                {
                    wall[w].shade = wallshades[w];
                    wall[w].pal = wallpals[w];
                    wallflag[w>>3] &= ~(1<<(w&7));
                }
                // removed: same thing with nextwalls
            }
            sector[isec].floorshade = sectorshades[isec][0];
            sector[isec].ceilingshade = sectorshades[isec][1];
            sector[isec].floorpal = sectorpals[isec][0];
            sector[isec].ceilingpal = sectorpals[isec][1];

            for (w=headspritesect[isec]; w>=0; w=nextspritesect[w])
            {
                if (w == i)
                    continue;
                sprite[w].shade = spriteshades[w];
                sprite[w].pal = spritepals[w];
            }

        }
    }

    lastbstatus = bstatus;
    bstatus = mouseReadButtons();

    Keys2d3d();

    // 2d3d mode
    if ((in3dmode() && !m32_is2d3dmode()) || m32_is2d3dmode())
    {
#ifdef USE_OPENGL
        int bakrendmode = rendmode;

        if (m32_is2d3dmode())
            rendmode = REND_CLASSIC;
#endif

        Keys3d();
        editinput();
        if (infobox&2)
            m32_showmouse();

#ifdef USE_OPENGL
        rendmode = bakrendmode;
#endif
    }
    else if (!in3dmode())
    {
        Keys2d();

        if (autocorruptcheck>0 && totalclock > corruptchecktimer)
        {
            if (CheckMapCorruption(3, 0)>=3)
                printmessage16("Corruption detected. See OSD for details.");
            corruptchecktimer = (int32_t) totalclock + 120*autocorruptcheck;
        }
    }

#include "../../build/include/m32_editor_save_screenshot_tail_shared.cpp.inc"

////

void faketimerhandler(void)
{
    timerUpdateClock();
}

#include "../../build/include/m32_editor_palette_sector_search_shared.cpp.inc"

//////////////////// manual editing ////////////////////
// Build edit originally by Ed Coolidge <semicharm@earthlink.net>

#include "../../build/include/m32_editor_manual_edit_core_shared.cpp.inc"

#include "../../build/include/m32_manual_edit_handle_common_shared.cpp.inc"

#define BLOOD_RR_MAPEDIT_SHARED_EDIT_SECTOR_DATA
#include "../../build/include/blood_rr_mapedit_shared.cpp.inc"

#define BLOOD_RR_MAPEDIT_SHARED_EDIT_WALL_DATA
#include "../../build/include/blood_rr_mapedit_shared.cpp.inc"

#define BLOOD_RR_MAPEDIT_SHARED_EDIT_SPRITE_DATA
#include "../../build/include/blood_rr_mapedit_shared.cpp.inc"

#define TWENTYFIVE_BLANKS "                         "

#define BLOOD_RR_MAPEDIT_SHARED_GENERIC_SPRITE_SEARCH
#include "../../build/include/blood_rr_mapedit_shared.cpp.inc"
