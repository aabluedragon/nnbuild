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

#include "sounds_mapster32.h"
#include "fx_man.h"

#include "macros.h"
#include "colmatch.h"
#include "palette.h"

#include "m32script.h"
#include "m32def.h"

#include <signal.h>

// Workaround for namespace pollution in <sys/stat.h> introduced in MinGW 4.8.
#ifdef stat
# undef stat
#endif

const char* AppProperName = "Mapster32";
const char* AppTechnicalName = "mapster32";

#if defined(_WIN32)
#define DEFAULT_GAME_EXEC APPBASENAME ".exe"
#define DEFAULT_GAME_LOCAL_EXEC APPBASENAME ".exe"
#elif defined(__APPLE__)
#define DEFAULT_GAME_EXEC APPNAME ".app/Contents/MacOS/" APPBASENAME
#define DEFAULT_GAME_LOCAL_EXEC APPNAME ".app/Contents/MacOS/" APPBASENAME
#else
#define DEFAULT_GAME_EXEC APPBASENAME
#define DEFAULT_GAME_LOCAL_EXEC "./" APPBASENAME
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

static int32_t bstatus;

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
static char selectpals[MAXSECTORS][2];
static char spritepals[MAXSPRITES];
static uint8_t previewflag[bitmap_size(MAXSECTORS)];
static uint8_t wallflag[bitmap_size(MAXWALLS)];

#ifdef YAX_ENABLE
static uint8_t havebunch[bitmap_size(YAX_MAXBUNCHES)];
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
static uint8_t tilemarked[bitmap_size(MAXTILES)];

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

static int osdcmd_quit(osdcmdptr_t parm);


#include "../../build/include/duke_rr_astub_tags_shared.cpp.inc"

const char *ExtGetWallCaption(int16_t wallnum)
{
    static char tempbuf[64];

    Bmemset(tempbuf,0,sizeof(tempbuf));

    if (bitmap_test(editwall, wallnum))
    {
        Bsprintf(tempbuf,"%d", wallength(wallnum));
        bitmap_clear(editwall, wallnum);
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
        if (sprite[i].statnum==0 && (unsigned)sprite[i].picnum<MAXUSERTILES)
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
            if ((unsigned)pic>=MAXUSERTILES)
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
        for (i=0; i<MAXUSERTILES; i++)
            total += numsprite[i];
        for (i=0; i<MAXUSERTILES; i++)
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

#define M32_SHOW_FILE_TEXT_KFD_TYPE buildvfs_kfd
#define M32_SHOW_FILE_TEXT_KFD_INVALID buildvfs_kfd_invalid
#include "../../build/include/m32_show_file_text_shared.cpp.inc"

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
        Xfree(dst);
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
        Xfree(dst);
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

    Xfree(dest);
    return 0;
}

static void SoundDisplay(void)
{
    if (g_numsounds <= 0) return;

    overridepm16y = ydim;//3*STATUS2DSIZ;

    {
        // cursnd is the first displayed line, cursnd+curofs is where the cursor is
        static int32_t cursnd=0, curofs=0;
        char disptext[80];

        int32_t i, j;
        const int32_t halfpage = (ydim-64)/(2*9);

        while (keystatus[KEYSC_ESC]==0 && keystatus[KEYSC_Q]==0 && keystatus[KEYSC_F2]==0
                && keystatus[buildkeys[BK_MODE2D_3D]]==0)  // quickjump to 3d mode
        {
            videoBeginDrawing();
            CLEARLINES2D(0, ydim16, 0);
            videoEndDrawing();

            idle_waitevent();
            if (handleevents())
                quitevent = 0;

//            drawgradient();

            videoBeginDrawing();
            printext16(9, ydim-overridepm16y+9, editorcolors[4], -1, "Sound Index", 0);
            printext16(8, ydim-overridepm16y+8, editorcolors[12], -1, "Sound Index", 0);
            printext16(8 + 11*8 + 2*8, ydim-overridepm16y+8, editorcolors[15], -1, "(SPACE:play, S:sort)", 0);
            videoEndDrawing();

            if (PRESSED_KEYSC(G))    // goto specified sound#
            {
                _printmessage16("                                                    ");
                j = getnumber16("Goto sound#: ", 0, MAXSOUNDS-1, 0);
                for (i=0; i<g_numsounds; i++)
                    if (g_sndnum[i]==j)
                        break;
                if (i != g_numsounds)
                {
                    if (i<SOUND_NUMDISPLINES)
                        cursnd = 0, curofs = i;
                    else if (i>=g_numsounds-halfpage)
                        cursnd = g_numsounds-halfpage, curofs = i-cursnd;
                    else
                        curofs = halfpage/2, cursnd = i-curofs;
                }
            }
            else if (PRESSED_KEYSC(UP))    // scroll up
            {
                if (curofs>0) curofs--;
                else if (cursnd>0) cursnd--;
            }
            else if (PRESSED_KEYSC(DOWN))    // scroll down
            {
                if (curofs < halfpage-1 && cursnd+curofs<g_numsounds-1)
                    curofs++;
                else if (cursnd + halfpage < g_numsounds)
                    cursnd++;
            }
            else if (PRESSED_KEYSC(PGUP))    // scroll one page up
            {
                i = halfpage/2;

                while (i>0 && curofs>0)
                    i--, curofs--;
                while (i>0 && cursnd>0)
                    i--, cursnd--;
            }
            else if (PRESSED_KEYSC(PGDN))    // scroll one page down
            {
                i = halfpage/2;

                while (i>0 && curofs < halfpage-1 && cursnd+curofs<g_numsounds-1)
                    i--, curofs++;
                while (i>0 && cursnd + halfpage < g_numsounds)
                    i--, cursnd++;
            }
            else if (PRESSED_KEYSC(SPACE) || PRESSED_KEYSC(ENTER))   // play/stop sound
            {
                int32_t j = cursnd+curofs;
                int32_t k = g_sndnum[j];

                if (S_CheckSoundPlaying(0, k) > 0)
                    S_StopSound(k);
                else
                    S_PlaySound(k);
            }
            else if (PRESSED_KEYSC(HOME))    // goto first sound#
            {
                cursnd = curofs = 0;
            }
            else if (PRESSED_KEYSC(END))    // goto last sound#
            {
                if ((cursnd = g_numsounds - halfpage) >= 0)
                    curofs = halfpage-1;
                else
                {
                    cursnd = 0;
                    curofs = g_numsounds-1;
                }
            }

            _printmessage16("                          FILE NAME         PITCH RANGE  PRI FLAGS VOLUME");
            for (i=0; j=cursnd+i, i<SOUND_NUMDISPLINES && j<g_numsounds; i++)
            {
                int32_t l, m, k=g_sndnum[j];
                sound_t *snd=&g_sounds[k];
                char *cp;

                if (ydim-overridepm16y+28+i*9+32 >= ydim) break;

                Bsprintf(disptext,
                         "%4d .................... ................ %6d:%-6d %3d %c%c%c%c%c %6d",
                         //   5678901234567890X23456789012345678901234567
                         k, snd->ps, snd->pe, snd->pr,
                         snd->m&1 ? 'R':'-', snd->m&2 ? 'M':'-', snd->m&4 ? 'D':'-',
                         snd->m&8 ? 'P':'-', snd->m&16 ? 'G':'-', snd->vo);
                for (l = Bsnprintf(disptext+5, 20, "%s", snd->definedname); l<20; l++)
                    disptext[5+l] = ' ';
                if (snd->filename)
                {
                    l = Bstrlen(snd->filename);
                    if (l<=16)
                        cp = snd->filename;
                    else
                        cp = snd->filename + l-15;
                    for (m = Bsnprintf(disptext+26, 16, "%s", cp); m<16; m++)
                        disptext[26+m] = ' ';
                    if (l>16)
                        disptext[26] = disptext[27] = disptext[28] = '.';
                }

                printext16(8, ydim-overridepm16y+28+i*9,
                           keystatus[KEYSC_S]?editorcolors[8] : (S_CheckSoundPlaying(-1, k) ? editorcolors[2] : editorcolors[10]),
                           j==cursnd+curofs ? editorcolors[1] : -1,
                           disptext, 0);
            }

            if (keystatus[KEYSC_S])    // sorting
            {

                char ch, bad=0;

                _printmessage16("Sort by: (S)oundnum (D)ef (F)ile ori(g) or flags (12345)");
                videoShowFrame(1);

                i=0;
                keyFlushChars();
                while (bad == 0)
                {
                    idle_waitevent();
                    if (handleevents())
                        quitevent = 0;

                    ch = keyGetChar();

                    if (keystatus[sc_Escape]) bad = 1;

                    else if (ch == 's' || ch == 'd' || ch == 'f' || ch == 'g' ||
                             ch == '1' || ch == '2' || ch == '3' || ch == '4' || ch == '5')
                    {
                        bad = 2;
                        sort_sounds(ch);
                    }
                }

                clearkeys();
            }
            else
                videoShowFrame(1);
        }

        overridepm16y = -1;

        FX_StopAllSounds();
        S_ClearSoundLocks();

        clearkeys();
    }
}
// PK_ ^^^^

int32_t AmbienceToggle = 1;
int32_t ParentalLock = 0;

uint8_t g_ambiencePlaying[bitmap_size(MAXSPRITES)];

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
            if (bitmap_test(g_ambiencePlaying, i))
            {
                bitmap_clear(g_ambiencePlaying, i);
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
                    x = dist(&pos,s);
                    if (x < ht && !bitmap_test(g_ambiencePlaying, i) && FX_VoiceAvailable(g_sounds[s->lotag].pr-1))
                    {
                        char om = g_sounds[s->lotag].m;
                        if (g_numEnvSoundsPlaying == NumVoices)
                        {
                            for (j = headspritestat[0]; j >= 0; j = nextspritestat[j])
                            {
                                if (s->picnum == MUSICANDSFX && j != i && sprite[j].lotag < 999 &&
                                    bitmap_test(g_ambiencePlaying, j) && dist(&sprite[j],&pos) > x)
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
                        bitmap_set(g_ambiencePlaying, i);
                    }
                    if (x >= ht && bitmap_test(g_ambiencePlaying, i))
                    {
                        bitmap_clear(g_ambiencePlaying, i);
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


#include "../../build/include/m32_ask_if_sure_shared.cpp.inc"

static FORCE_INLINE bool IsValidTile(int const idTile)
{
    return (unsigned)idTile < MAXUSERTILES && tilesiz[idTile].x && tilesiz[idTile].y && rottile[idTile].owner == -1;
}

static int32_t SelectAllTiles(int32_t iCurrentTile)
{
    if (iCurrentTile < localartlookupnum)
        iCurrentTile = localartlookup[iCurrentTile];
    else
        iCurrentTile = 0;

    localartlookupnum = MAXUSERTILES;

    for (int i = 0; i < MAXUSERTILES; i++)
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

static int32_t DrawTiles(int32_t iTopLeft, int32_t iSelected, int32_t nXTiles, int32_t nYTiles,
                         int32_t TileDim, int32_t offset, int32_t showmsg);

#define TMPERRMSG_SHOW(alsoOSD) do { \
    printext256(0, 0, whitecol, 0, tilesel_errmsg, 0); \
    if (alsoOSD) OSD_Printf("%s\n", tilesel_errmsg); \
} while (0)

#define TMPERRMSG_PRINT(Msg, ...) do {  \
    Bsprintf(tilesel_errmsg, Msg, ## __VA_ARGS__); \
    TMPERRMSG_SHOW(1); \
    videoShowFrame(1); \
    tilesel_showerr = 1; \
} while (0)

#define TMPERRMSG_RETURN(Msg, ...) do   \
{ \
    TMPERRMSG_PRINT(Msg, ## __VA_ARGS__);  \
    return 1; \
} while (0)


static inline void pushDisableFog(void)
{
#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        polymost_setFogEnabled(false);
    }
#endif
}

static inline void popDisableFog(void)
{
#ifdef USE_OPENGL
    if (videoGetRenderMode() >= REND_POLYMOST)
    {
    }
#endif
}


static void editorCalculateTileViewerDimensions(int32_t *nXTiles, int32_t *nYTiles, int32_t *nDisplayedTiles, int32_t *nzoom)
{
    int nx, ny, ndisplayed;

    do
    {
        nx = xdim / ZOOMSIZ(*nzoom);
        ny = (ydim / ZOOMSIZ(*nzoom)) + 1;
        // Refuse to draw less than half of a row.
        if ((ZOOMSIZ(*nzoom) >> 1) < 12)
            ny--;

        ndisplayed = nx * ny;

        if (!ndisplayed)
        {
            // Eh-up, resolution changed since we were last displaying tiles.
            (*nzoom)++;
        }
    } while (!ndisplayed);

    *nXTiles = nx;
    *nYTiles = ny;
    *nDisplayedTiles = ndisplayed;
}


static int32_t editorTilePickerBasicKeys(int32_t tileNum, int32_t nXTiles, int32_t iTopLeftTile, int32_t nDisplayedTiles)
{
    if (PRESSED_KEYSC(LEFT))
    {
        if (eitherCTRL)  // same as HOME, for consistency with CTRL-UP/DOWN
            tileNum = (tileNum / nXTiles) * nXTiles;
        else
            tileNum--;
    }

    if (PRESSED_KEYSC(RIGHT))
    {
        if (eitherCTRL)  // same as END, for consistency with CTRL-UP/DOWN
            tileNum = ((tileNum + nXTiles) / nXTiles) * nXTiles - 1;
        else
            tileNum++;
    }

    if (PRESSED_KEYSC(UP))
    {
        if (eitherCTRL)
            while (tileNum - nXTiles >= iTopLeftTile)
                tileNum -= nXTiles;
        else
            tileNum -= nXTiles;
    }

    if (PRESSED_KEYSC(DOWN))
    {
        if (eitherCTRL)
            while (tileNum + nXTiles < iTopLeftTile + nDisplayedTiles)
                tileNum += nXTiles;
        else
            tileNum += nXTiles;
    }

    if (PRESSED_KEYSC(PGUP))
    {
        if (eitherCTRL)
            tileNum = 0;
        else
            tileNum -= nDisplayedTiles;
    }

    if (PRESSED_KEYSC(PGDN))
    {
        if (eitherCTRL)
            tileNum = localartlookupnum - 1;
        else
            tileNum += nDisplayedTiles;
    }

    if (PRESSED_KEYSC(HOME))
    {
        if (eitherCTRL)
            tileNum = iTopLeftTile;
        else
            tileNum = (tileNum / nXTiles) * nXTiles;
    }

    if (PRESSED_KEYSC(END))
    {
        if (eitherCTRL)
            tileNum = iTopLeftTile + nDisplayedTiles - 1;
        else
            tileNum = ((tileNum + nXTiles) / nXTiles) * nXTiles - 1;
    }

    if (PRESSED_KEYSC(DASH))
    {
        if (globalpal > 0)
            globalpal--;
        else globalpal = M32_MAXPALOOKUPS;

        while (globalpal > 0 && (!palookup[globalpal] || palookup[globalpal] == palookup[0]))
            globalpal--;
    }

    if (PRESSED_KEYSC(EQUAL))
    {
        if (globalpal < M32_MAXPALOOKUPS)
        {
            globalpal++;

            while (globalpal < M32_MAXPALOOKUPS && (!palookup[globalpal] || palookup[globalpal] == palookup[0]))
                globalpal++;

            if (globalpal == M32_MAXPALOOKUPS)
                globalpal = 0;
        }
        else globalpal = 0;
    }

    if (PRESSED_KEYSC(BS))
        globalpal = 0;

    // 'U'  KEYPRESS : go straight to user defined art
    if (PRESSED_KEYSC(U))
    {
        SelectAllTiles(tileNum);
        tileNum = FIRST_USER_ART_TILE;
    }

    // 'A'  KEYPRESS : Go straight to start of Atomic edition's art
    if (PRESSED_KEYSC(A))
    {
        SelectAllTiles(tileNum);
        tileNum = FIRST_ATOMIC_TILE;
    }

    // 'E'  KEYPRESS : Go straight to start of extended art
    if (PRESSED_KEYSC(E))
    {
        SelectAllTiles(tileNum);

        if (tileNum == FIRST_EXTENDED_TILE)
            tileNum = SECOND_EXTENDED_TILE;
        else tileNum = FIRST_EXTENDED_TILE;
    }

    // 'T' KEYPRESS = Select from pre-defined tileset
    if (PRESSED_KEYSC(T))
        tileNum = OnSelectTile(tileNum);

    // change palette
    if (PRESSED_KEYSC(P))
    {
        Bsprintf(tempbuf, "%s pal: ", Typestr[searchstat]);

        int pal;

        do
        {
            pal = getnumber256(tempbuf, globalpal, M32_MAXPALOOKUPS, 0+2+16);
        } while (pal != 0 && (palookup[pal] == palookup[0] || palookup[pal] == NULL));

        globalpal = pal;
    }

    if (PRESSED_KEYSC(Z))
        s_TileZoom = !s_TileZoom;

    return tileNum;
}

static int32_t editorCalculateTileUsage(int32_t idInitialTile)
{
    idInitialTile = clamp(idInitialTile, 0, MAXUSERTILES - 1);

    keystatus[KEYSC_V] = 0;

    for (int32_t i = 0; i < MAXUSERTILES; i++)
    {
        localartfreq[i] = 0;
        localartlookup[i] = i;
    }

    switch (searchstat)
    {
    case SEARCH_WALL:
        for (int32_t i = 0; i < numwalls; i++)
            localartfreq[wall[i].picnum]++;
        break;

    case SEARCH_CEILING:
    case SEARCH_FLOOR:
        for (int32_t i = 0; i < numsectors; i++)
        {
            localartfreq[sector[i].ceilingpicnum]++;
            localartfreq[sector[i].floorpicnum]++;
        }
        break;

    case SEARCH_SPRITE:
        for (int32_t i = 0; i < MAXSPRITES; i++)
            localartfreq[sprite[i].picnum] += (sprite[i].statnum < MAXSTATUS);
        break;

    case SEARCH_MASKWALL:
        for (int32_t i = 0; i < numwalls; i++)
            localartfreq[wall[i].overpicnum]++;
        break;

    default:
        break;
    }

    //
    //	Sort tiles into frequency order
    //

    int32_t gap = MAXUSERTILES / 2;

    do
    {
        for (int32_t i = 0; i < MAXUSERTILES-gap; i++)
        {
            int32_t temp = i;

            while (temp >= 0 && localartfreq[temp]<localartfreq[temp+gap])
            {
                int32_t tempint;

                tempint = localartfreq[temp];
                localartfreq[temp] = localartfreq[temp+gap];
                localartfreq[temp+gap] = tempint;

                tempint = localartlookup[temp];
                localartlookup[temp] = localartlookup[temp+gap];
                localartlookup[temp+gap] = tempint;

                if (idInitialTile == temp)
                    idInitialTile = temp + gap;
                else if (idInitialTile == temp + gap)
                    idInitialTile = temp;

                temp -= gap;
            }
        }

        gap >>= 1;
    } while (gap > 0);

    //
    // Set up count of number of used tiles
    //
    localartlookupnum = 0;
    while (localartfreq[localartlookupnum] > 0)
        localartlookupnum++;

    //
    // Check : If no tiles used at all then switch to displaying all tiles
    //
    if (!localartfreq[0])
    {
        localartlookupnum = MAXUSERTILES;

        for (int32_t i = 0; i < MAXUSERTILES; i++)
        {
            localartlookup[i] = i;
            localartfreq[i] = 0; // Terrible bodge : zero tilefreq's not displayed in tile view. Still, when in Rome ... :-)
        }
    }

    return idInitialTile;
}

static int32_t editorGetTile(int32_t idInitialTile)
{
    int32_t mtile = 0, omousex = searchx, omousey = searchy, moffset = 0;
    int32_t nmoffset = 0;

    int32_t noTilesMarked = 1;
    int32_t mark_lastk    = -1;

    FX_StopAllSounds();
    S_ClearSoundLocks();

    pushDisableFog();

    int32_t const idInitialPal = globalpal;

    int32_t tileNum   = editorCalculateTileUsage(idInitialTile);
    int32_t iLastTile = tileNum, idSelectedTile = tileNum;

    int32_t nXTiles = 0, nYTiles = 0, nDisplayedTiles = 0;
    editorCalculateTileViewerDimensions(&nXTiles, &nYTiles, &nDisplayedTiles, &s_Zoom);

    int32_t iTopLeftTile = tileNum - (tileNum % nXTiles);
    iTopLeftTile = clamp(iTopLeftTile, 0, MAXUSERTILES - nDisplayedTiles);

    int32_t zoomsz = ZOOMSIZ(s_Zoom);

    searchx = ((tileNum-iTopLeftTile)%nXTiles)*zoomsz + (zoomsz>>1);
    searchy = ((tileNum-iTopLeftTile)/nXTiles)*zoomsz + (zoomsz>>1);

    editorMaybeWarpMouse(searchx, searchy);

    ////////////////////////////////
    // Start of key handling code //
    ////////////////////////////////

    ClockTicks lastclock = 0;
    int32_t omoffset = 0;
    int32_t ozoom = s_Zoom;
    int32_t nzoom = s_Zoom;
    int32_t olocked;

    auto doSmoothScrolling = [&](int const amt)
    {
        nmoffset += amt;

        //if ((nmoffset < 0 && iTopLeftTile > localartlookupnum - 1) || (nmoffset > 0 && iTopLeftTile == 0))
        //{
        //    nmoffset=0;
        //    omoffset=0;
        //    return true;
        //}

        while (nmoffset >= ZOOMSIZ(nzoom))
        {
            iTopLeftTile -= nXTiles;
            nmoffset -= ZOOMSIZ(nzoom);
            omoffset -= ZOOMSIZ(nzoom);
        }

        while (nmoffset <= -ZOOMSIZ(nzoom))
        {
            iTopLeftTile += nXTiles;
            nmoffset += ZOOMSIZ(nzoom);
            omoffset += ZOOMSIZ(nzoom);
        }

        return false;
    };

    while ((keystatus[KEYSC_ENTER]|keystatus[KEYSC_ESC]|(bstatus&1)) == 0)
    {
        editorCalculateTileViewerDimensions(&nXTiles, &nYTiles, &nDisplayedTiles, &nzoom);

        int32_t const clock = (int32_t)(totalclock - lastclock);

        if (clock > 3)
        {
            lastclock = lastclock + 4;
            omoffset = nmoffset;
            ozoom = nzoom;
            s_Zoom = nzoom;
        }

        int32_t const smoothratio = calc_smoothratio(totalclock, lastclock, 30);

        moffset = nmoffset - mulscale16(65536 - smoothratio, nmoffset - omoffset);
        //OSD_Printf("moffset %d for smoothratio %d\n", moffset, smoothratio);
        zoomsz = ZOOMSIZ(nzoom) - mulscale16(65536 - smoothratio, ZOOMSIZ(nzoom) - ZOOMSIZ(ozoom));
        //moffset = nmoffset;
        //zoomsz = ZOOMSIZ(nzoom);
        DrawTiles(iTopLeftTile, (tileNum >= localartlookupnum) ? localartlookupnum-1 : tileNum,
                  nXTiles, nYTiles, zoomsz, moffset,
                  (tilesel_showerr && (tileNum==iLastTile || (tilesel_showerr=0))));

        OSD_DispatchQueued();

        if (handleevents())
            quitevent = 0;

        iLastTile = tileNum;

        bstatus = mouseReadButtons();

        olocked = g_mouseLockedToWindow;
        editorMaybeLockMouse(!!(bstatus & ~(16|32)));

        if (osdvisible) continue;

        int32_t mousedx, mousedy;

        if (g_mouseLockedToWindow)
        {
            mouseReadPos(&mousedx, &mousedy);
            searchx += mousedx;
            searchy += mousedy;
        }
        else
        {
            // we need to warp to the last position for the RMB panning thing to work right
            if (olocked)
                editorMaybeWarpMouse(searchx, searchy);

            mousedx = g_mousePos.x;
            mousedy = g_mousePos.y;

            g_mousePos = {};

            searchx = g_mouseAbs.x / upscalefactor;
            searchy = g_mouseAbs.y / upscalefactor;
        }

        if (bstatus & 2)
        {
            searchx -= mousedx;
            searchy += mousedy;

            if (doSmoothScrolling(mousedy << 1))
                searchy -= mousedy << 1;
        }

        // Keep the pointer visible at all times.
        int32_t temp = min(zoomsz >> 1, 12);
        inpclamp(&searchx, temp, nXTiles * zoomsz - 1);
        inpclamp(&searchy, temp, ydim - temp);

        bool const scrollmode = !(eitherCTRL ^ revertCTRL);

        if (bstatus&16 && scrollmode && iTopLeftTile > 0)
        {
            g_mouseBits &= ~16;
            doSmoothScrolling(nzoom << scrollamount << 4);
        }
        else if (bstatus&32 && scrollmode && iTopLeftTile < localartlookupnum-nDisplayedTiles-1)
        {
            g_mouseBits &= ~32;
            doSmoothScrolling(-(nzoom << scrollamount << 4));
        }
        else if (bstatus&48 && scrollmode)
            g_mouseBits &= ~48;

        //
        //	Adjust top-left to ensure tilenum is within displayed range of tiles
        //

        //while (tileNum < iTopLeftTile - (nmoffset<0?nXTiles:0))
        //    iTopLeftTile -= nXTiles;

        //while (tileNum >= iTopLeftTile + nDisplayedTiles - (mtile != tileNum ? nXTiles : 0))
        //    iTopLeftTile += nXTiles;

        if ((nmoffset < 0 && iTopLeftTile > localartlookupnum - nDisplayedTiles - 1) || (nmoffset > 0 && iTopLeftTile == 0))
        {
            debug_break();
            nmoffset = 0;
            omoffset = 0;
        }

        if (mtile != tileNum)  // if changed by keyboard, update mouse cursor
        {
            tileNum = clamp(tileNum, 0, min(MAXUSERTILES - 1, localartlookupnum - 1));
            searchx = ((tileNum - iTopLeftTile) % nXTiles) * zoomsz + (zoomsz >> 1);
            searchy = ((tileNum - iTopLeftTile) / nXTiles) * zoomsz + (zoomsz >> 1) + nmoffset;

            temp = min(zoomsz >> 1, 12);
            inpclamp(&searchx, temp, xdim - temp);
            inpclamp(&searchy, temp, ydim - temp);

            // HACK? FIXME? XXX? who knows!
            editorMaybeWarpMouse(searchx, searchy);
        }

        iTopLeftTile = clamp(iTopLeftTile, 0, MAXUSERTILES - nDisplayedTiles);

        mtile = tileNum = searchx / zoomsz + ((searchy - nmoffset) / zoomsz) * nXTiles + iTopLeftTile;
        //while (tileNum >= iTopLeftTile + nDisplayedTiles)
        //{
        //    tileNum -= nXTiles;
        //    mtile = tileNum;
        //}

        synctics = (int32_t)totalclock - lockclock;
        lockclock += synctics;

        // Zoom in / out using numeric key pad's / and * keys
        if (((keystatus[KEYSC_gSLASH] || (!scrollmode && bstatus&16)) && nzoom < (NUM_ZOOMS-1))
                || ((keystatus[KEYSC_gSTAR]  || (!scrollmode && bstatus&32)) && nzoom > 0))
        {
            if (PRESSED_KEYSC(gSLASH) || (!scrollmode && bstatus&16))
            {
                g_mouseBits &= ~16;
                bstatus &= ~16;
                inputchecked = 1;

                if (ZOOMSIZ(nzoom + 1) <= xdim && ZOOMSIZ(nzoom + 1) <= ydim)
                    nzoom++;
            }
            else
            {
                keystatus[KEYSC_gSTAR] = 0;
                g_mouseBits &= ~32;
                bstatus &= ~32;
                inputchecked = 1;
                nzoom--;
            }

            if (tileNum >= localartlookupnum)
                tileNum = localartlookupnum - 1;

            editorCalculateTileViewerDimensions(&nXTiles, &nYTiles, &nDisplayedTiles, &nzoom);

            // Determine if the top-left displayed tile needs to change in order to display selected tile.
            iTopLeftTile = tileNum - (tileNum % nXTiles);
            iTopLeftTile = clamp(iTopLeftTile, 0, MAXUSERTILES - nDisplayedTiles);

            // Scroll window so mouse points to the same tile as it was before zooming.
            iTopLeftTile -= searchx/ZOOMSIZ(nzoom) + ((searchy-nmoffset)/ZOOMSIZ(nzoom))*nXTiles + iTopLeftTile-tileNum;
        }

        int32_t const oldtile = tileNum;
        tileNum = editorTilePickerBasicKeys(tileNum, nXTiles, iTopLeftTile, nDisplayedTiles);

        // 'V'  KEYPRESS
        if (PRESSED_KEYSC(V))
            tileNum = SelectAllTiles(tileNum);

        // 'G'  KEYPRESS - Goto frame
        if (PRESSED_KEYSC(G))
        {
            if (eitherCTRL)
            {
                if (OnSaveTileGroup() == 0)
                {
//                    tileNum = SelectAllTiles(tileNum);
                    Bmemset(tilemarked, 0, sizeof(tilemarked));
                    mark_lastk = -1;
                    noTilesMarked = 1;
                }
            }
            else
                tileNum = OnGotoTile(tileNum);
        }

        // 'S' KEYPRESS: search for named tile
        if (PRESSED_KEYSC(S))
        {
            static char laststr[25] = "";
            static char buf[2][25];

            const char *searchstr = getstring_simple("Search for tile name: ", laststr, sizeof(names[0]) - 1, 1);

            if (searchstr && searchstr[0])
            {
                int32_t const slen   = Bstrlen(searchstr) - 1;
                int32_t const i0     = localartlookup[tileNum];
                bool const    anchor = (searchstr[0] == '^');

                Bstrncpyz(laststr, searchstr, 25);
                Bmemcpy(buf[0], laststr, 25);
                Bstrupr(buf[0]);

                for (int32_t i = (i0 + 1) % MAXUSERTILES; i != i0; i = (i + 1) % MAXUSERTILES)
                {
                    Bmemcpy(buf[1], names[i], 25);
                    buf[1][24] = 0;
                    Bstrupr(buf[1]);

                    if ((anchor && !Bstrncmp(buf[1], buf[0] + 1, slen)) ||
                        (!anchor && Bstrstr(buf[1], buf[0])))
                    {
                        SelectAllTiles(i);
                        tileNum = i;
                        break;
                    }
                }
            }

            g_mousePos.x = g_mousePos.y = g_mouseBits = 0;
        }

        // See if we need to adjust for a change of tile selection.
        if (oldtile != tileNum && (unsigned)tileNum > (unsigned)nXTiles)
        {
            while (iTopLeftTile + nDisplayedTiles - tileNum - 1 < nXTiles)
                iTopLeftTile += nXTiles;

            while (tileNum < iTopLeftTile + nXTiles)
                iTopLeftTile -= nXTiles;
        }

        if (tileNum < nXTiles)
            iTopLeftTile = 0;
        else if (tileNum >= MAXUSERTILES - nXTiles)
            iTopLeftTile = MAXUSERTILES - nDisplayedTiles;

        // SPACE keypress: mark/unmark selected tile
        if (PRESSED_KEYSC(SPACE))
        {
            if (tileNum < localartlookupnum && IsValidTile(localartlookup[tileNum]))
            {
                if (keystatus[KEYSC_LCTRL] && keystatus[KEYSC_RSHIFT])
                {
                    Bmemset(tilemarked, 0, sizeof(tilemarked));
                    mark_lastk = -1;
                    noTilesMarked = 1;
                }
                else
                {
                    int32_t k = tileNum, kend, dir;

                    if (noTilesMarked)
                    {
                        noTilesMarked = 0;
                        TMPERRMSG_PRINT("Beginning marking tiles. To group, press Ctrl-G."
                                        " To reset, press LCtrl-RShift-SPACE.");
                    }

                    if (mark_lastk >= 0 && eitherCTRL)
                    {
                        kend = mark_lastk;
                        dir  = ksgn(mark_lastk - k);
                    }
                    else
                    {
                        kend = k;
                        dir = 0;
                    }

                    mark_lastk = k;

                    for (; dir == 0 || dir * (kend - k) >= 1; k += dir)
                    {
                        bitmap_flip(tilemarked, localartlookup[k]);
                        if (dir == 0)
                            break;
                    }
                }
            }
        }
    }

    auto resetToInitial = [&]
    {
        idSelectedTile = idInitialTile;
        globalpal      = idInitialPal;
    };

    if ((keystatus[KEYSC_ENTER] || (bstatus&1)) == 0)
        resetToInitial();
    else
    {
        if (tileNum < localartlookupnum)
        {
            // Convert tile num from index to actual tile num
            idSelectedTile = localartlookup[tileNum];

            // Check : if invalid tile selected, return original tile num
            if (!IsValidTile(idSelectedTile))
                resetToInitial();
        }
        else
            resetToInitial();
    }

    searchx = omousex;
    searchy = omousey;
    editorMaybeWarpMouse(searchx, searchy);
    keystatus[KEYSC_ESC] = 0;
    keystatus[KEYSC_ENTER] = 0;

    popDisableFog();

    return idSelectedTile;
}

static int32_t OnSaveTileGroup(void)
{
    int32_t i, n=0;
    char hotkey;
    const char *cp, *name;

    if (tile_groups==MAX_TILE_GROUPS)
        TMPERRMSG_RETURN("Cannot save tile group: maximum number of groups (%d) exceeded.", MAX_TILE_GROUPS);

    for (i=0; i<MAXUSERTILES; i++)
        n += !!bitmap_test(tilemarked, i);

    if (n==0)
        TMPERRMSG_RETURN("Cannot save tile group: no tiles marked.");
    else if (n > MAX_TILE_GROUP_ENTRIES)
        TMPERRMSG_RETURN("Cannot save tile group: too many tiles in group. Have %d, max is %d.",
                  n, MAX_TILE_GROUP_ENTRIES);

    cp = getstring_simple("Hotkey for new group: ", "", 1, 0);
    if (!cp || !*cp)
        return 1;

    hotkey = Btoupper(cp[0]);
    if (!isalpha(hotkey))
        TMPERRMSG_RETURN("Hotkey must be alphabetic.");

    for (i=0; i<tile_groups; i++)
        if (s_TileGroups[i].key1==hotkey || s_TileGroups[i].key2==Btolower(hotkey))
            TMPERRMSG_RETURN("Hotkey '%c' already in use by tile group `%s'.", hotkey, s_TileGroups[i].szText);

    name = getstring_simple("Name for new tile group: ", "", 63, 0);
    if (!name || !*name)
        return 1;

    for (i=0; name[i]; i++)
        if (!(isalnum(name[i]) || name[i]==' ' || name[i]==','))
            TMPERRMSG_RETURN("Name may only consist of alphabetic, numeric and space characters.");

    {
        int32_t lasti=-1, col=0, j, k, opathsearchmode=pathsearchmode;
        BFILE *fp;

        pathsearchmode = pathsearchmode_oninit;  // use the same pathsearchmode as on init
        fp = fopenfrompath(default_tiles_cfg, "a");
        pathsearchmode = opathsearchmode;
        if (!fp)
            TMPERRMSG_RETURN("Could not open `%s' for writing: %s.", default_tiles_cfg, strerror(errno));
        if (fseek(fp, 0, BSEEK_END))  // seems to be necessary even though we fopen with "a"
            TMPERRMSG_RETURN("Could not seek to end of file `%s'.", default_tiles_cfg);

#define TTAB "\t"
#define TBITCHK(i) ((i) < MAXUSERTILES && bitmap_test(tilemarked, (i)))
        Bfprintf(fp, OURNEWL);
        Bfprintf(fp, "tilegroup \"%s\"" OURNEWL"{" OURNEWL, name);
        Bfprintf(fp, TTAB "hotkey \"%c\"" OURNEWL OURNEWL, hotkey);

        s_TileGroups[tile_groups].pIds = (int32_t *)Xmalloc(n * sizeof(s_TileGroups[tile_groups].pIds[0]));

        j = 0;
        // tileranges for consecutive runs of 3 or more tiles
        for (i=0; i<MAXUSERTILES; i++)
        {
            if (lasti>=0 && !TBITCHK(i))
            {
                if (names[lasti][0] && names[i-1][0])
                    Bfprintf(fp, TTAB "tilerange %s %s" OURNEWL, names[lasti], names[i-1]);
                else
                    Bfprintf(fp, TTAB "tilerange %d %d" OURNEWL, lasti, i-1);

                for (k=lasti; k<i; k++)
                {
                    s_TileGroups[tile_groups].pIds[j++] = k;
                    bitmap_clear(tilemarked, k);
                }

                lasti = -1;
            }
            else if (lasti==-1 && TBITCHK(i))
            {
                if (TBITCHK(i+1) && TBITCHK(i+2))
                {
                    lasti = i;
                    i += 2;
                }
            }
        }
        if (lasti>=0 && lasti<=MAXUSERTILES-3)
        {
            for (k=lasti; k<MAXUSERTILES; k++)
            {
                s_TileGroups[tile_groups].pIds[j++] = k;
                bitmap_clear(tilemarked, k);
            }
            Bfprintf(fp, TTAB "tilerange %d %d" OURNEWL, lasti, MAXUSERTILES-1);
        }
        Bfprintf(fp, OURNEWL);

        k = 0;
        for (i=0; i<MAXUSERTILES; i++)
            if (bitmap_test(tilemarked, i))
            {
                k = 1;
                break;
            }

        if (k)
        {
            // throw them all in a tiles{...} group else
            Bfprintf(fp, TTAB "tiles\n" TTAB "{" OURNEWL);
            for (i=0; i<MAXUSERTILES; i++)
            {
                if (TBITCHK(i))
                {
                    s_TileGroups[tile_groups].pIds[j++] = i;

                    if (col==0)
                        Bfprintf(fp, TTAB TTAB), col+=8;

                    if (names[i][0])
                        col+=Bfprintf(fp, "%s ", names[i]);
                    else
                        col+=Bfprintf(fp, "%d ", i);

                    if (col>80)
                    {
                        Bfprintf(fp, OURNEWL);
                        col = 0;
                    }
                }
            }
            if (col>0)
                Bfprintf(fp, OURNEWL);
            Bfprintf(fp, TTAB "}" OURNEWL);
        }
#undef TBITCHK
#undef TTAB
        Bfprintf(fp, "}" OURNEWL);

        Bfclose(fp);

        s_TileGroups[tile_groups].szText = Xstrdup(name);

        s_TileGroups[tile_groups].nIds = n;
        s_TileGroups[tile_groups].key1 = Btoupper(hotkey);
        s_TileGroups[tile_groups].key2 = Btolower(hotkey);
        s_TileGroups[tile_groups].color1 = s_TileGroups[tile_groups].color2 = 0;
        tile_groups++;

        TMPERRMSG_PRINT("Wrote and installed new tile group.");
    }

    return 0;
}

static int32_t OnGotoTile(int32_t tileNum)
{
    //Automatically press 'V'
    SelectAllTiles(tileNum);

    return getnumber256("Goto tile: ", 0, MAXUSERTILES-1, 0+2+16);
}

static int32_t LoadTileSet(const int32_t idCurrentTile, const int32_t *pIds, const int32_t nIds)
{
    int32_t iNewTile = 0;

    localartlookupnum = nIds;

    for (int32_t i = 0; i < localartlookupnum; i++)
    {
        localartlookup[i] = pIds[i];
        // REM : Could we still utilise localartfreq[] to mark
        //  which tiles are currently used in the map ? Set to 0xFFFF perhaps ?
        localartfreq[i] = 0;

        if (idCurrentTile == pIds[i])
            iNewTile = i;
    }

    return iNewTile;
}

static int32_t OnSelectTile(int32_t tileNum)
{
    if (tile_groups <= 0)
    {
        TMPERRMSG_PRINT("No tile groups loaded. Check for existence of `%s'.", default_tiles_cfg);
        return tileNum;
    }

    SelectAllTiles(tileNum);

    keyFlushChars();

    polymostSet2dView();

    videoClearViewableArea(-1);

    //
    //	Await appropriate selection keypress.
    //

    bool bDone = false;

    while (keystatus[KEYSC_ESC] == 0 && !bDone)
    {
        if (handleevents())
            quitevent = 0;

        idle_waitevent();

        //
        // Display the description strings for each available tile group
        //
        int32_t j = 0;

        for (int32_t i = 0; i < tile_groups; i++)
        {
            if (s_TileGroups[i].szText != NULL)
            {
                if ((j+2)*16 > ydim) break;

                if (s_TileGroups[i].key1)
                {
                    Bsprintf(tempbuf, "(%c) %s", s_TileGroups[i].key1, s_TileGroups[i].szText);
                    printext256(10L, (j+1)*16, whitecol, -1, tempbuf, 0);
                    j++;
                }
            }
        }
        videoShowFrame(1);

        char const ch = keyGetChar();

        for (int32_t i = 0; i < tile_groups; i++)
        {
            if (s_TileGroups[i].pIds != NULL && s_TileGroups[i].key1)
                if (ch == s_TileGroups[i].key1 || ch == s_TileGroups[i].key2)
                {
                    tileNum = LoadTileSet(tileNum, s_TileGroups[i].pIds, s_TileGroups[i].nIds);
                    bDone = true;
                }
        }
    }

    videoShowFrame(1);

    clearkeys();

    return tileNum;
}

static const char *GetTilePixels(int32_t idTile)
{
    char *pPixelData = 0;

    if ((unsigned)idTile < MAXUSERTILES)
    {
        if (!waloff[idTile])
            tileLoad(idTile);

        if (IsValidTile(idTile))
            pPixelData = (char *)waloff[idTile];
    }

    return pPixelData;
}

static void classic_drawtilescreen(int32_t x, int32_t y, int32_t idTile, int32_t TileDim,
                                   const char *pRawPixels)
{
    int32_t dispxsz = tilesiz[idTile].x, dispysz = tilesiz[idTile].y;
    int32_t divinc = 1, mulinc = 1;

    int32_t xofs, yofs;
    char *pScreen;

    while ((dispxsz/divinc > TileDim) || (dispysz/divinc) > TileDim)
    {
        divinc++;
    }

    if (divinc == 1 && s_TileZoom)
    {
        while ((dispxsz*(mulinc+1)) <= TileDim && (dispysz*(mulinc+1)) <= TileDim)
        {
            mulinc++;
        }
    }

    dispxsz = (dispxsz / divinc) * mulinc;
    dispysz = (dispysz / divinc) * mulinc;

    for (yofs = 0; yofs < dispysz; yofs++)
    {
        y += yofs;
        if (y>=0 && y<ydim)
        {
            pScreen = (char *)ylookup[y]+x+frameplace;
            for (xofs = 0; xofs < dispxsz; xofs++)
            {
                pScreen[xofs] = palookup[globalpal][pRawPixels[((yofs*divinc)/mulinc) + (((xofs*divinc)/mulinc)*tilesiz[idTile].y)]];
            }
        }
        y -= yofs;
    }
}

static void tilescreen_drawbox(int32_t iTopLeft, int32_t iSelected, int32_t nXTiles,
                               int32_t TileDim, int32_t offset,
                               int32_t tileNum, int32_t idTile)
{
    int32_t marked = (IsValidTile(idTile) && bitmap_test(tilemarked, idTile));

    //
    // Draw white box around currently selected tile or marked tile
    // p1=(x1, y1), p2=(x1+TileDim-1, y1+TileDim-1)
    //
    if (tileNum == iSelected || marked)
    {
        int32_t x1 = ((tileNum-iTopLeft) % nXTiles)*TileDim;
        int32_t y1 = ((tileNum - ((tileNum-iTopLeft) % nXTiles) - iTopLeft)/nXTiles)*TileDim + offset;
        int32_t x2 = x1+TileDim-1;
        int32_t y2 = y1+TileDim-1, oydim16=ydim16;

        char markedcol = editorcolors[14] - (M32_THROB>>1);

        polymostSet2dView();

        y1=max(y1, 0);
        y2=min(y2, ydim-1);

        // plotlines2d uses drawline16, which clips against ydim16...
        ydim16 = ydim;

        {
            // box
            int32_t xx[] = {x1+1, x2-1, x2-1, x1+1, x1+1, x1, x2, x2, x1, x1, };
            int32_t yy[] = { y1+1, y1+1, y2-1, y2-1, y1+1, y1, y1, y2, y2, y1, };
            plotlines2d(xx, yy, 10, tileNum==iSelected ? batmanandthrobbin() : markedcol);
        }

        // cross
        if (marked)
        {
            int32_t xx[] = {x1, x2};
            int32_t yy[] = {y1, y2};

            plotlines2d(xx, yy, 2, markedcol);
            swaplong(&yy[0], &yy[1]);
            plotlines2d(xx, yy, 2, markedcol);
        }

        ydim16 = oydim16;
    }
}

static void tilescreen_drawrest(int32_t iSelected, int32_t showmsg)
{
    if ((unsigned)iSelected < MAXUSERTILES)
    {
        int32_t idTile = localartlookup[iSelected];
        int32_t i;
        char szT[128];

        // Draw info bar at bottom.

        // Clear out behind the text for improved visibility.
        //drawline256(0, (ydim-12)<<12, xdim<<12, (ydim-12)<<12, whitecol);
        for (i=ydim-12; i<ydim; i++)
            renderDrawLine(0, i<<12, xdim<<12, i<<12, (ydim-i));

        // Tile number on left.
        Bsprintf(szT, "Tile: %d (p)al: %d" , idTile, globalpal);
        printext256(1, ydim-10, whitecol, -1, szT, 0);

        // Tile name on right.
        printext256(xdim-(Bstrlen(names[idTile])<<3)-1,ydim-10,whitecol,-1,names[idTile],0);

        // Tile dimensions.
        Bsprintf(szT,"dim: %dx%d",tilesiz[idTile].x,tilesiz[idTile].y);
        printext256((xdim>>2)+(4<<3),ydim-10,whitecol,-1,szT,0);

        // EditArt offset flags.
        Bsprintf(szT,"offs: %d, %d", picanm[idTile].xofs, picanm[idTile].yofs);
        printext256((xdim>>2)+(4<<3)+100,ydim-10,whitecol,-1,szT,0);

        // EditArt animation flags.
        if (picanm[idTile].sf&PICANM_ANIMTYPE_MASK)
        {
            static const char *anmtype[] = {"", "Osc", "Fwd", "Bck"};
            int32_t ii = (picanm[idTile].sf&PICANM_ANIMTYPE_MASK)>>PICANM_ANIMTYPE_SHIFT;

            Bsprintf(szT,"%s %d", anmtype[ii], picanm[idTile].num);
            printext256((xdim>>2)+(4<<3)+100+14*8,ydim-10,whitecol,-1,szT,0);
        }
    }

    if (showmsg)
        TMPERRMSG_SHOW(0);

    m32_showmouse();
}

////////// main tile drawing function //////////
static int32_t DrawTiles(int32_t iTopLeft, int32_t iSelected, int32_t nXTiles, int32_t nYTiles,
                         int32_t TileDim, int32_t offset, int32_t showmsg)
{
    static uint8_t loadedhitile[bitmap_size(MAXTILES)];

#ifdef USE_OPENGL
    int32_t const lazyselector = !osdvisible && g_lazy_tileselector && usehightile;
#else
    int32_t const lazyselector = g_lazy_tileselector;
#endif
    int32_t x = 0, y = 0;
    int32_t runi = 0;

#ifdef USE_OPENGL
    polymostSet2dView();

    if (videoGetRenderMode() >= REND_POLYMOST)
    {
        if (lazyselector)
            glDrawBuffer(GL_FRONT_AND_BACK);
    }
#endif
    videoClearViewableArea(-1);

    videoBeginDrawing();

restart:
    for (int32_t YTile = 0 - (offset > 0); YTile < nYTiles + (offset < 0) + 1; YTile++)
    {
        for (int32_t XTile = 0; XTile < nXTiles; XTile++)
        {
            int32_t const tileNum = iTopLeft + XTile + (YTile * nXTiles);

            if (tileNum < 0 || tileNum >= localartlookupnum)
                continue;
#ifdef USE_OPENGL
            bool usehitile = (runi || !lazyselector);
#endif
            int32_t const idTile = localartlookup[tileNum];

            if (bitmap_test(loadedhitile, idTile))
            {
                if (runi==1)
                    continue;
#ifdef USE_OPENGL
                usehitile = true;
#endif
            }

            // Get pointer to tile's raw pixel data
            const char *pRawPixels = GetTilePixels(idTile);

            // don't draw rotated tiles generated near MAXTILES
            if (EDUKE32_PREDICT_FALSE(rottile[idTile].owner != -1))
                pRawPixels = NULL;

            if (pRawPixels != NULL)
            {
                x = XTile * TileDim;
                y = YTile * TileDim + offset;

#ifdef USE_OPENGL
                if (polymost_drawtilescreen(x, y, idTile, TileDim, s_TileZoom, usehitile, loadedhitile))
#endif
                    classic_drawtilescreen(x, y, idTile, TileDim, pRawPixels);
            }

            tilescreen_drawbox(iTopLeft, iSelected, nXTiles, TileDim, offset, tileNum, idTile);

            if (localartfreq[tileNum] != 0 && y >= 0 && y <= ydim-20)
            {
                char szT[128];
                Bsprintf(szT, "%d", localartfreq[tileNum]);
                printext256(x, y, whitecol, -1, szT, 1);
            }

            if (runi == 1 && lazyselector)
            {
                if (handleevents())
                    quitevent=0;

                tilescreen_drawrest(iSelected, showmsg);

                bool k = (g_mousePos.x || g_mousePos.y || g_mouseBits);

                if (!k)
                    for (int32_t i = 0; i < (signed)ARRAY_SIZE(keystatus); i++)
                        if (keystatus[i])
                        {
                            k = true;
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
        if (runi == 0)
        {
            videoEndDrawing();
            videoShowFrame(1);
            videoBeginDrawing();

            runi = 1;
            goto restart;
        }
    }

    videoEndDrawing();
    OSD_Draw();
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

#include "../../build/include/m32_text_entry_mode_shared.cpp.inc"

static void mouseaction_movesprites(int32_t *sumxvect, int32_t *sumyvect, int32_t yangofs, int32_t mousexory)
{
    int32_t xvect,yvect, daxvect,dayvect, ii, spi;
    int32_t units, gridlock = (eitherCTRL && grid > 0 && grid < 9);
    auto const sp = &sprite[searchwall];
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

    if (highlightcnt <= 0 || !bitmap_test(show2dsprite, searchwall))
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
    static const char *aligntype[4] = { "view", "wall", "floor", "sloped" };

    int32_t i = sprite[spritenum].cstat;

    if ((i & CSTAT_SPRITE_ALIGNMENT) < CSTAT_SPRITE_ALIGNMENT_FLOOR)
        i += CSTAT_SPRITE_ALIGNMENT_WALL;
    else
        i &= ~CSTAT_SPRITE_ALIGNMENT;
    sprite[spritenum].cstat = i;

    message("Sprite %d now %s aligned", spritenum, aligntype[(i & CSTAT_SPRITE_ALIGNMENT) / CSTAT_SPRITE_ALIGNMENT_WALL]);
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
    int32_t i = 0, changedir,tsign;
    int32_t j, k, hiz, loz;
    int32_t hihit, lohit;
    char smooshyalign=0, repeatpanalign=0;
    int16_t startwall, endwall, dasector;
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
                    Bsprintf(lines[num++],"^%dWall%d ->^%d %d", editorcolors[10], searchwall, editorcolors[14], searchbottomwall);
                else
                    Bsprintf(lines[num++],"^%dWall^%d %d", editorcolors[10], (wall[searchwall].cstat & CSTAT_WALL_ROTATE_90) ? editorcolors[11] : editorcolors[10], searchwall);

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

                {
                    int16_t const heinum = spriteGetSlope(searchwall);
                    int32_t const notextended = heinum == 0;
                    Bsprintf(lines[num++], "Repeat:  %d,%d",
                        TrackerCast(sprite[searchwall].xrepeat), TrackerCast(sprite[searchwall].yrepeat));
                    Bsprintf(lines[num++], "PosXY:   %d,%d%s",
                        TrackerCast(sprite[searchwall].x), TrackerCast(sprite[searchwall].y),
                             (sprite[searchwall].xoffset|sprite[searchwall].yoffset) && notextended ? " ^251*":"");
                    Bsprintf(lines[num++], "PosZ: ""   %d", TrackerCast(sprite[searchwall].z));// prevents tab character
                    if (!notextended)
                        Bsprintf(lines[num++], "Slope:    %d", heinum);
                    lines[num++][0]=0;
                }

                if (getmessageleng)
                    break;

                if ((unsigned)sprite[searchwall].picnum>=MAXUSERTILES)
                    break;

                if (names[sprite[searchwall].picnum][0])
                {
                    if (sprite[searchwall].picnum==SECTOREFFECTOR)
                        Bsprintf(lines[num++],"^%dSprite %d^%d %s", editorcolors[10], searchwall, whitecol, SectorEffectorText(searchwall));
                    else
                        Bsprintf(lines[num++],"^%dSprite %d^%d %s", editorcolors[10], searchwall, whitecol, names[sprite[searchwall].picnum]);
                }
                else Bsprintf(lines[num++],"^%dSprite %d^%d", editorcolors[10], searchwall, whitecol);

                Bsprintf(lines[num++], "Elevation:%d",
                         getflorzofslope(searchsector, sprite[searchwall].x, sprite[searchwall].y) - sprite[searchwall].z);
                break;
            }
        }

        x = (int32_t)(WIND1X*(xdim/320.));
        y = (int32_t)(WIND1Y*(ydim/200.));
        y += (ydim>>6)*8;

        if (getmessageleng && !m32_is2d3dmode())
        {
            while (num < 4)
                lines[num++][0] = 0;
            Bsprintf(lines[num++], "^%d%s", editorcolors[10], getmessage);
        }

        videoBeginDrawing();
        for (i=0; i<num; i++)
        {
            printext256(x+2, y+2, 0, -1, lines[i], xdim<=640);
            printext256(x, y, whitecol, -1, lines[i], xdim<=640);
            y += ydim>>6;
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

    if (eitherCTRL && eitherALT && PRESSED_KEYSC(W))
    {
        spriteinsertmode = !spriteinsertmode;
        message("Sprite insertion clips non-hitscan sprites: %s", ONOFF(spriteinsertmode));
    }

    if (PRESSED_KEYSC(V))  //V
    {
        if (ASSERT_AIMING)
        {
            int32_t const oldtile = AIMED_SELOVR_PICNUM;

            globalpal = AIMED_CF_SEL(pal);

            int32_t const tempint = editorGetTile(oldtile);

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
        int16_t pal[4];

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
                bitmap_clear(g_ambiencePlaying, searchwall);
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
        asksave = 1;
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
            asksave = 1;
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
            if ((i & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_FLOOR)
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

    smooshyalign = keystatus[KEYSC_gKP5];
    repeatpanalign = eitherSHIFT || eitherALT || (bstatus&2);

    {
        static int32_t omlook;

        if (mlook == 2)
        {
            mlook = omlook;
        }
        else if (!unrealedlook && (bstatus&4))
        {
            omlook = mlook;
            mlook = 2;
        }
    }

    // PK: no btn: wheel changes shade
    if ((bstatus&(16|32) && !(bstatus&(1|2|4))) || keystatus[KEYSC_gMINUS] || keystatus[KEYSC_gPLUS])
    {
        //        if (bstatus&1)
        //            mlook = 2;
        tsign = 0;
        if (bstatus&32 || PRESSED_KEYSC(gMINUS))  // -
            tsign = 1;
        if (bstatus&16 || PRESSED_KEYSC(gPLUS))  // +
            tsign = -1;

        if (tsign)
        {
            g_mouseBits &= ~(16|32);
            bstatus &= ~(16|32);

            if (eitherALT)  //ALT
            {
                if (eitherCTRL)  //CTRL
                {
                    if (tsign==1)
                        g_visibility <<= (int)(g_visibility < 16384);
                    else
                        g_visibility >>= (int)(g_visibility > 32);
                    silentmessage("Global visibility %d", g_visibility);
                }
                else
                {
                    k=eitherSHIFT?1:16;

                    if (highlightsectorcnt > 0 && bitmap_test(hlsectorbitmap, searchsector))
                    {
                        while (k-- > 0)
                        {
                            for (i=0; i<highlightsectorcnt; i++)
                            {
                                j = highlightsector[i];
                                sector[j].visibility += tsign;

                                if (tsign==1 && sector[j].visibility == 240)
                                    sector[j].visibility = 239;
                                else if (tsign==-1 && sector[j].visibility == 239)
                                    sector[j].visibility = 240;
                            }
                        }
                    }
                    else
                    {
                        while (k-- > 0)
                        {
                            sector[searchsector].visibility += tsign;

                            if (tsign==1 && sector[searchsector].visibility == 240)
                                sector[searchsector].visibility = 239;
                            else if (tsign==-1 && sector[searchsector].visibility == 239)
                                sector[searchsector].visibility = 240;
                        }
                    }

                    if (sector[searchsector].visibility >= 240)
                        silentmessage("Sector %d visibility %d (%d)", searchsector, TrackerCast(sector[searchsector].visibility) - 256,
                                      TrackerCast(sector[searchsector].visibility));
                    else
                        silentmessage("Sector %d visibility %d", searchsector, TrackerCast(sector[searchsector].visibility));

                    asksave = 1;
                }
            }
            else  // if !eitherALT
            {
                int32_t clamped=0;

                k = (highlightsectorcnt>0 && bitmap_test(hlsectorbitmap, searchsector));
                tsign *= (1+3*eitherCTRL);

                if (k == 0)
                {
                    if (ASSERT_AIMING)
                    {
                        if (!eitherSHIFT && AIMING_AT_SPRITE && bitmap_test(show2dsprite, searchwall))
                        {
                            for (i=0; i<highlightcnt; i++)
                                if (highlight[i]&16384)
                                    clamped = addtobyte(&sprite[highlight[i]&16383].shade, tsign);
                            (clamped ? message : silentmessage)
                                ("Highlighted sprite shade changed by %d%s",
                                 tsign, clamped?" (some sprites' shade clamped)":"");
                        }
                        else
                        {
                            clamped = addtobyte(&AIMED_CF_SEL(shade), tsign);
                            // TODO: factor formatting stuff out and use elsewhere?
                            (clamped ? message : silentmessage)
                                ("%s %s%d shade %d%s", Typestr[searchstat],
                                 AIMING_AT_CEILING_OR_FLOOR ? "of sector " : "",
                                 AIMING_AT_WALL_OR_MASK ? SELECT_WALL() :
                                     (AIMING_AT_CEILING_OR_FLOOR ? searchsector : searchwall),
                                 AIMED_CF_SEL(shade), clamped ? " (clamped)":"");
                        }
                    }
                }
                else
                {
                    for (i=0; i<highlightsectorcnt; i++)
                    {
                        dasector = highlightsector[i];

                        // sector shade
                        clamped |= addtobyte(&sector[dasector].ceilingshade, tsign);
                        clamped |= addtobyte(&sector[dasector].floorshade, tsign);

                        // wall shade
                        for (WALLS_OF_SECTOR(dasector, j))
                            clamped |= addtobyte(&wall[j].shade, tsign);

                        // sprite shade
                        for (j=headspritesect[dasector]; j!=-1; j=nextspritesect[j])
                            clamped |= addtobyte(&sprite[j].shade, tsign);
                    }
                    (clamped ? message : silentmessage)("Highlighted sector shade changed by %d%s", tsign,
                            clamped?" (some objects' shade clamped)":"");
                }
                asksave = 1;
            }
        }
    }

    // PK: lmb only & mousewheel, -, and +, cycle picnum
    if (keystatus[KEYSC_DASH] || keystatus[KEYSC_EQUAL] || (bstatus&(16|32) && (bstatus&1) && !(bstatus&2)))
    {
        if (ASSERT_AIMING)
        {
            int32_t pic = AIMED_SELOVR_PICNUM;
            int32_t dir = (keystatus[KEYSC_EQUAL] || (bstatus&16)) ? 1 : -1;

            do
            {
                pic += dir + MAXUSERTILES;
                pic %= MAXUSERTILES;
            }
            while (!IsValidTile(pic));
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
            if (AIMING_AT_WALL_OR_MASK)
            {
                AIMED_SEL_WALL(cstat) ^= CSTAT_WALL_ROTATE_90;

                message("Wall %d texture rotation bit %s", SELECT_WALL(),
                        ONOFF(AIMED_SEL_WALL(cstat)&CSTAT_WALL_ROTATE_90));
                asksave = 1;
            }
            else if (AIMING_AT_SPRITE)
                toggle_sprite_alignment(searchwall);
        }
    }

    if (PRESSED_KEYSC(F))  //F (Flip)
    {
        if (eitherALT)  //ALT-F (relative alignmment flip)
        {
            if (!AIMING_AT_SPRITE && ASSERT_AIMING)
                SetFirstWall(searchsector, searchwall, eitherSHIFT);
        }
        else
        {
            static const int8_t next2[4] = { 1, 3, 0, 2 };  // 0->1->3->2->0
            static const int8_t prev2[4] = { 2, 0, 3, 1 };  // 0<-1<-3<-2<-0

            static const char *flip2label[4] = { "none", "X", "Y", "X and Y" };

            if (AIMING_AT_WALL_OR_MASK)
            {
                i = wall[searchbottomwall].cstat;
                i = ((i>>3)&1)+((i>>7)&2);    //3-x,8-y

                i = eitherSHIFT ? prev2[i] : next2[i];
                message("Wall %d flip %s", searchwall, flip2label[i]);

                i = ((i&1)<<3)+((i&2)<<7);
                wall[searchbottomwall].cstat &= ~0x0108;
                wall[searchbottomwall].cstat |= i;
                asksave = 1;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)  //8-way ceiling/floor flipping (bits 2,4,5)
                toggle_cf_flipping(searchsector, AIMING_AT_FLOOR);
            else if (AIMING_AT_SPRITE)
            {
                i = sprite[searchwall].cstat;
                if (((i & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_FLOOR) && ((i&64) == 0))
                {
                    sprite[searchwall].cstat &= ~0xc;
                    sprite[searchwall].cstat |= ((i&4)^4);
                    message("Sprite %d flip bit %s",searchwall, ONOFF(sprite[searchwall].cstat&4));
                }
                else
                {
                    i = ((i>>2)&3);

                    i = eitherSHIFT ? prev2[i] : next2[i];
                    message("Sprite %d flip %s", searchwall, flip2label[i]);

                    sprite[searchwall].cstat &= ~0xc;
                    sprite[searchwall].cstat |= (i<<2);
                }
                asksave = 1;
            }
        }
    }


    if (keystatus[KEYSC_HOME] && keystatus[KEYSC_END])
        updownunits = 128;
    else if (keystatus[KEYSC_HOME])
        updownunits = 256;
    else if (keystatus[KEYSC_END])
        updownunits = 512;
    else
        updownunits = 1024;

    mouseaction=0;
    if (eitherALT && (bstatus&1))
    {
        g_mousePos.x=0; mskip=1;
        if (g_mousePos.y!=0)
        {
            updownunits=klabs(g_mousePos.y*128);
            mouseaction=1;
        }
    }


    tsign = 0;
    if (ASSERT_AIMING)
    {
        // PK: PGUP/PGDN, rmb only & mwheel
        tsign -= (PRESSED_KEYSC(PGUP) || (mouseaction && g_mousePos.y<0) || ((bstatus&(16|2|1))==(16|2)));
        tsign += (PRESSED_KEYSC(PGDN) || (mouseaction && g_mousePos.y>0) || ((bstatus&(32|2|1))==(32|2)));
    }

    if (tsign)
    {
        int16_t sect0, sect, havebtm=0, havetop=0, moveCeilings, moveFloors;
        int32_t cz, fz;

        k = 0;
        if (highlightsectorcnt > 0 && searchsector>=0 && searchsector<numsectors)
        {
            if (bitmap_test(hlsectorbitmap, searchsector))
                k = highlightsectorcnt;
        }

        if (k)
        {
            sect = highlightsector[0];
            havetop = AIMING_AT_WALL_OR_MASK;
        }
        else
        {
            if (AIMING_AT_WALL_OR_MASK && wall[searchwall].nextsector>=0
                    && eitherALT && !(bstatus&1))
            {
                sect = wall[searchwall].nextsector;
                havebtm = !AIMING_AT_MASKWALL && searchisbottom;
                havetop = !havebtm;
            }
            else
            {
                sect = searchsector;
                havetop = AIMING_AT_WALL_OR_MASK;
            }
        }

        sect0 = sect;
        moveCeilings = (AIMING_AT_CEILING || havetop);
        moveFloors = (AIMING_AT_FLOOR || havebtm);

        if (moveCeilings || moveFloors)
        {
            int32_t dz = tsign * (updownunits << (eitherCTRL<<1));   // JBF 20031128
            static const char *cfs[2] = { "ceiling", "floor" };
#ifdef YAX_ENABLE
            int16_t bunchnum=-1, maxbunchnum=-1, cb, fb;
            Bmemset(havebunch, 0, sizeof(havebunch));
#endif
            for (j=0; j<(k?k:1); j++, sect=highlightsector[j])
            {
                // stage one: see if we don't move beyond the other side
                // (ceiling if floor and vice versa)

                if (moveCeilings && (dz > 0) && sector[sect].ceilingz+dz > sector[sect].floorz)
                    dz = (k > 1) ? 0 : min(sector[sect].floorz - sector[sect].ceilingz, dz);
                else if (moveFloors && (dz < 0) && sector[sect].floorz+dz < sector[sect].ceilingz)
                    dz = (k > 1) ? 0 : max(sector[sect].ceilingz - sector[sect].floorz, dz);

                if (dz == 0)
                    break;
            }

            if (dz)
            {
                // now truly move things if we're clear to go!
                sect = sect0;
                for (j=0; j<(k?k:1); j++, sect=highlightsector[j])
                {
                    for (i=headspritesect[sect]; i!=-1; i=nextspritesect[i])
                    {
                        spriteoncfz(i, &cz, &fz);
                        if ((moveCeilings && sprite[i].z == cz) || (moveFloors && sprite[i].z == fz))
                            sprite[i].z += dz;
                    }

                    SECTORFLD(sect,z, moveFloors) += dz;
#ifdef YAX_ENABLE
                    bunchnum = yax_getbunch(sect, moveFloors);
                    if (bunchnum >= 0 && !bitmap_test(havebunch, bunchnum))
                    {
                        maxbunchnum = max(maxbunchnum, bunchnum);
                        bitmap_set(havebunch, bunchnum);
                        tempzar[bunchnum] = &SECTORFLD(sect,z, moveFloors);
                    }
#endif
                }
            }

#ifdef YAX_ENABLE
            if (dz)
            {
                // sync z values of extended sectors' ceilings/floors
                for (i=0; i<numsectors; i++)
                {
                    yax_getbunches(i, &cb, &fb);
                    if (cb >= 0 && bitmap_test(havebunch, cb))
                        sector[i].ceilingz = *tempzar[cb];
                    if (fb >= 0 && bitmap_test(havebunch, fb))
                        sector[i].floorz = *tempzar[fb];
                }
            }

            if (!dz)
                silentmessage("Didn't move sector %ss", cfs[moveFloors]);
            else if (k<=1 && bunchnum>=0)
                silentmessage("Bunch %d's ceilings and floors = %d", bunchnum, SECTORFLD(sect0,z, moveFloors));
            else
#endif
            if (k<=1)
                silentmessage("Sector %d %sz = %d", sect0, cfs[moveFloors], SECTORFLD(sect0,z, moveFloors));
            else
                silentmessage("%s %d sector %ss by %d units", tsign<0 ? "Raised" : "Lowered",
                              k, cfs[moveFloors], dz*tsign);
        }

        if (AIMING_AT_SPRITE)
        {
            int32_t cz, fz;

            if (eitherCTRL && !eitherALT)  //CTRL - put sprite on ceiling/floor
            {
                spriteoncfz(searchwall, &cz, &fz);
                sprite[searchwall].z = (tsign==1) ? fz : cz;
            }
            else
            {
                k = !!bitmap_test(show2dsprite, searchwall);

                tsign *= (updownunits << ((eitherCTRL && mouseaction)*3));

                for (i=0; i<highlightcnt || k==0; i++)
                {
                    if (k==0 || (highlight[i]&0xc000) == 16384)
                    {
                        int16_t sp = k==0 ? searchwall : highlight[i]&16383;

                        sprite[sp].z += tsign;

                        if (!spnoclip)
                        {
                            spriteoncfz(sp, &cz, &fz);
                            inpclamp(&sprite[sp].z, cz, fz);
                        }
#ifdef YAX_ENABLE
                        else if (sprite[sp].sectnum >= 0)
                        {
                            int16_t cb, fb;
                            yax_getbunches(sprite[sp].sectnum, &cb, &fb);
                            if (cb >= 0 || fb >= 0)
                                setspritez(sp, &sprite[sp].xyz);
                        }
#endif
                        if (k==0)
                        {
                            silentmessage("Sprite %d z = %d", searchwall, TrackerCast(sprite[searchwall].z));
                            break;
                        }
                    }

                    if (k==1)
                        silentmessage("Sprites %s by %d units", tsign<0 ? "raised" : "lowered", tsign);
                }
            }
        }

        asksave = 1;
        g_mouseBits &= ~(16|32);
    }

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
            else if (keystatus[KEYSC_LCTRL])
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

        printext256(searchx+4+2-i, searchy+4+2-j, 0,-1,tempbuf,!(xdim > 640));
        printext256(searchx+4-i,   searchy+4-j,   whitecol,-1,tempbuf,!(xdim > 640));
    }

    if (helpon==1)
    {
        int32_t small = !(xdim > 640);
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

    if (keystatus[buildkeys[BK_MODE2D_3D]])  // Enter
    {
        SetGamePalette(BASEPAL);
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

                if (bitmap_test(g_ambiencePlaying, searchwall) && sprite[searchwall].lotag != oldtag)
                {
                    bitmap_clear(g_ambiencePlaying, searchwall);
                    S_StopEnvSound(oldtag, searchwall);
                }
            }
            else
                sprite[searchwall].lotag = getnumber256("Sprite lotag: ", sprite[searchwall].lotag, BTAG_MAX, 0+j);
        }
    }

#include "../../build/include/m32_editor_key_actions_shared.cpp.inc"

    // ----------
    i = 512;

    if (keystatus[KEYSC_RSHIFT])
        i = 8;
    else if (keystatus[KEYSC_LSHIFT])
        i = 1;
    else if (keystatus[KEYSC_HOME] && keystatus[KEYSC_END])
        i = 64;
    else if (keystatus[KEYSC_HOME])
        i = 128;
    else if (keystatus[KEYSC_END])
        i = 256;

    mouseaction=0;

    if (keystatus[KEYSC_LCTRL] && !eitherSHIFT && (bstatus&1) && AIMING_AT_CEILING_OR_FLOOR)
    {
        g_mousePos.x=0; mskip=1;
        if (g_mousePos.y)
        {
            i=klabs(g_mousePos.y*2);
            mouseaction=1;
        }
    }

    tsign = 0;
    if (ASSERT_AIMING)
    {
        tsign -= (PRESSED_KEYSC(LBRACK) || (mouseaction && g_mousePos.y<0));   // [
        tsign += (PRESSED_KEYSC(RBRACK) || (mouseaction && g_mousePos.y>0));   // ]
    }

    if (tsign)
    {
#ifdef YAX_ENABLE
        int16_t bunchnum, othersidesect=0;
#endif
        if (eitherALT)
        {
            int32_t ns=wall[searchwall].nextsector, sx=wall[searchwall].x, sy=wall[searchwall].y;

            if (ns >= 0 && !mouseaction)
            {
                if (AIMING_AT_CEILING || (tsign < 0 && AIMING_AT_WALL_OR_MASK))
#ifdef YAX_ENABLE
                if (YAXCHK((bunchnum=yax_getbunch(searchsector, YAX_CEILING)) < 0 ||
                           (othersidesect=yax_is121(bunchnum, 1))>=0) &&
                        (bunchnum < 0 || YAXSLOPECHK(searchsector, othersidesect)))
#endif
                {
                    alignceilslope(searchsector, sx, sy, getceilzofslope(ns, sx, sy));
#ifdef YAX_ENABLE
                    if (bunchnum>=0)
                        setslope(othersidesect, 1, sector[searchsector].ceilingheinum);
#endif
                    message("Sector %d align ceiling to wall %d", searchsector, searchwall);
                }

                if (AIMING_AT_FLOOR || (tsign > 0 && AIMING_AT_WALL_OR_MASK))
#ifdef YAX_ENABLE
                if (YAXCHK((bunchnum=yax_getbunch(searchsector, YAX_FLOOR)) < 0 ||
                           (othersidesect=yax_is121(bunchnum, 0))>=0) &&
                        (bunchnum < 0 || YAXSLOPECHK(searchsector, othersidesect)))
#endif
                {
                    alignflorslope(searchsector, sx, sy, getflorzofslope(ns, sx, sy));
#ifdef YAX_ENABLE
                    if (bunchnum>=0)
                        setslope(othersidesect, 0, sector[searchsector].floorheinum);
#endif
                    message("Sector %d align floor to wall %d", searchsector, searchwall);
                }
            }
        }
        else
        {
            if (AIMING_AT_CEILING_OR_FLOOR)
            {
#ifdef YAX_ENABLE
                if (YAXCHK((bunchnum=yax_getbunch(searchsector, AIMING_AT_FLOOR)) < 0 ||
                           (othersidesect=yax_is121(bunchnum, AIMING_AT_CEILING))>=0) &&
                        (bunchnum < 0 || YAXSLOPECHK(searchsector, othersidesect)))
#endif
                {
                    int32_t oldslope = (AIMED_CEILINGFLOOR(stat)&2) ? AIMED_CEILINGFLOOR(heinum) : 0;
                    int32_t newslope = clamp(oldslope + tsign*i, -BHEINUM_MAX, BHEINUM_MAX);

                    setslope(searchsector, AIMING_AT_FLOOR, newslope);
#ifdef YAX_ENABLE
                    if (bunchnum >= 0)
                        setslope(othersidesect, !AIMING_AT_FLOOR, newslope);
#endif
                    silentmessage("Sector %d %s slope = %d", searchsector,
                                  typestr[searchstat], AIMED_CEILINGFLOOR(heinum));
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                auto const cstat = sprite[searchwall].cstat;
                if (cstat & CSTAT_SPRITE_ALIGNMENT_FLOOR)
                {
                    int32_t oldslope = spriteGetSlope(searchwall);
                    int32_t newslope = clamp(oldslope + tsign*i, -BHEINUM_MAX, BHEINUM_MAX);
                    spriteSetSlope(searchwall, newslope);
                    silentmessage("Sprite %d slope = %d", searchwall, newslope);
                }
            }
        }

        asksave = 1;
    }


#include "../../build/include/m32_editor_shift_pan_shared.cpp.inc"

    smooshyalign = keystatus[KEYSC_gKP5];
    repeatpanalign = eitherSHIFT || eitherALT;

    ////////////////////
    updownunits=1;
    mouseaction=0;

    if (!g_mouseBits)
    {
        mouseax=0;
        mouseay=0;
    }

    if ((bstatus&1) && !AIMING_AT_CEILING_OR_FLOOR)
    {
        if (eitherSHIFT)
        {
            mskip=1;
            if (g_mousePos.x)
            {
                mouseaction = 1;
                mouseax += g_mousePos.x;
                updownunits = klabs(mouseax/2);
                if (updownunits)
                    mouseax=0;
            }
        }
        else if (eitherCTRL && !eitherALT)
        {
            mskip=1;
            if (g_mousePos.x)
            {
                mouseaction = 2;
                repeatpanalign = 0;
                updownunits = klabs(mouseax+=g_mousePos.x)/(16 - 12*AIMING_AT_SPRITE);
                if (updownunits)
                    mouseax=0;
            }
        }
    }

    if (keystatus[KEYSC_gLEFT] || keystatus[KEYSC_gRIGHT] || mouseaction) // 4 & 6 (keypad)
    {
        if (repeatcountx == 0 || repeatcountx > 32 || mouseaction)
        {
            changedir = 0;
            if (keystatus[KEYSC_gLEFT]  || g_mousePos.x>0)
                changedir = -1;
            if (keystatus[KEYSC_gRIGHT] || g_mousePos.x<0)
                changedir = 1;

            if (AIMING_AT_WALL_OR_MASK)
            {
                if (repeatpanalign == 0)
                {
                    while (updownunits--)
                        wall[searchwall].xrepeat = changechar(wall[searchwall].xrepeat, changedir, smooshyalign, 1);
                    silentmessage("Wall %d repeat: %d, %d", searchwall,
                                  TrackerCast(wall[searchwall].xrepeat),
                                  TrackerCast(wall[searchwall].yrepeat));
                }
                else
                {
                    int16_t w = SELECT_WALL();

                    if (mouseaction)
                    {
                        i = wall[w].cstat;
                        i &= (8|256);

                        if (i==8 || i==256)
                            changedir*=-1;

                        if (eitherCTRL)
                            updownunits *= 8;
                    }

                    while (updownunits--)
                        wall[w].xpanning = changechar(wall[w].xpanning, changedir, smooshyalign, 0);
                    silentmessage("Wall %d panning: %d, %d", w,
                                  TrackerCast(wall[w].xpanning), TrackerCast(wall[w].ypanning));
                }
                asksave = 1;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
#ifdef YAX_ENABLE__COMPAT
                if (YAXCHK(yax_getbunch(searchsector, AIMING_AT_FLOOR) < 0))
#endif
                {
                    while (updownunits--)
                        AIMED_CEILINGFLOOR(xpanning) = changechar(AIMED_CEILINGFLOOR(xpanning), changedir, smooshyalign, 0);
                    silentmessage("Sector %d %s panning: %d, %d", searchsector, typestr[searchstat],
                                  AIMED_CEILINGFLOOR(xpanning), AIMED_CEILINGFLOOR(ypanning));
                    asksave = 1;
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                static int32_t sumxvect=0, sumyvect=0;

                if (mouseaction==1)
                    mouseaction_movesprites(&sumxvect, &sumyvect, 1536, g_mousePos.x);
                else
                {
                    sumxvect = sumyvect = 0;
                    if (mouseaction==2)
                        changedir *= -1;
                    while (updownunits--)
                        sprite[searchwall].xrepeat = changechar(sprite[searchwall].xrepeat, changedir, smooshyalign, 1);
                    if (sprite[searchwall].xrepeat < 1)
                        sprite[searchwall].xrepeat = 1;
                    silentmessage("Sprite %d repeat: %d, %d", searchwall,
                                  TrackerCast(sprite[searchwall].xrepeat),
                                  TrackerCast(sprite[searchwall].yrepeat));
                }
            }
            asksave = 1;
            repeatcountx = max(1,repeatcountx-2);
        }
        repeatcountx += synctics;
    }
    else
        repeatcountx = 0;


    ////////////////////
    updownunits=1;
    mouseaction=0;

    if ((bstatus&1) && !AIMING_AT_CEILING_OR_FLOOR)
    {
        if (eitherSHIFT)
        {
            mskip=1;
            if (g_mousePos.y)
            {
                mouseaction = 1;
                updownunits = klabs(g_mousePos.y);

                if (!AIMING_AT_SPRITE)
                    updownunits = klabs((int32_t)(g_mousePos.y*128./tilesiz[wall[searchwall].picnum].y));
            }
        }
        else if (eitherCTRL && !eitherALT)
        {
            mskip=1;
            if (g_mousePos.y)
            {
                mouseaction = 2;
                repeatpanalign = 0;
                mouseay += g_mousePos.y;
                updownunits = klabs(mouseay)/(32 - 28*AIMING_AT_SPRITE);
                if (updownunits)
                    mouseay=0;
            }
        }
    }

    if (!g_mouseBits)
    {
        mouseax=0;
        mouseay=0;
    }

    if (keystatus[KEYSC_gUP] || keystatus[KEYSC_gDOWN] || mouseaction)  // 2 & 8 (keypad)
    {
        if (repeatcounty == 0 || repeatcounty > 32 || mouseaction)
        {
            changedir = 0;
            if (keystatus[KEYSC_gUP]   || g_mousePos.y>0)
                changedir = -1;
            if (keystatus[KEYSC_gDOWN] || g_mousePos.y<0)
                changedir = 1;

            if (AIMING_AT_WALL_OR_MASK)
            {
                if (repeatpanalign == 0)
                {
                    while (updownunits--)
                        wall[searchwall].yrepeat = changechar(wall[searchwall].yrepeat, changedir, smooshyalign, 1);
                    silentmessage("Wall %d repeat: %d, %d", searchwall,
                                  TrackerCast(wall[searchwall].xrepeat),
                                  TrackerCast(wall[searchwall].yrepeat));
                }
                else
                {
                    int16_t w = SELECT_WALL();
                    if (mouseaction && eitherCTRL)
                        updownunits *= 8;
                    while (updownunits--)
                        wall[w].ypanning = changechar(wall[w].ypanning, changedir, smooshyalign, 0);
                    silentmessage("Wall %d panning: %d, %d", w,
                                  TrackerCast(wall[w].xpanning), TrackerCast(wall[w].ypanning));
                }
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
                {
                    while (updownunits--)
                        AIMED_CEILINGFLOOR(ypanning) = changechar(AIMED_CEILINGFLOOR(ypanning), changedir, smooshyalign, 0);
                    silentmessage("Sector %d %s panning: %d, %d", searchsector, typestr[searchstat],
                                  AIMED_CEILINGFLOOR(xpanning), AIMED_CEILINGFLOOR(ypanning));
                    asksave = 1;
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                static int32_t sumxvect=0, sumyvect=0;

                if (mouseaction==1)
                    mouseaction_movesprites(&sumxvect, &sumyvect, 2048, g_mousePos.y);
                else
                {
                    sumxvect = sumyvect = 0;
                    while (updownunits--)
                        sprite[searchwall].yrepeat = changechar(sprite[searchwall].yrepeat, changedir, smooshyalign, 1);
                    if (sprite[searchwall].yrepeat < 1)
                        sprite[searchwall].yrepeat = 1;
                    silentmessage("Sprite %d repeat: %d, %d", searchwall,
                                  TrackerCast(sprite[searchwall].xrepeat),
                                  TrackerCast(sprite[searchwall].yrepeat));
                }
            }
            asksave = 1;
            repeatcounty = max(1,repeatcounty-2);
        }
        repeatcounty += synctics;
    }
    else
        repeatcounty = 0;

    ////////////////////

#include "../../build/include/m32_editor_tab_copy_shared.cpp.inc"

#include "../../build/include/m32_editor_paste_align_shared.cpp.inc"

    if (PRESSED_KEYSC(ENTER))  // ENTER -- paste clipboard contents
    {
        if (eitherSHIFT)
        {
            if (AIMING_AT_WALL_OR_MASK && eitherCTRL)  //Ctrl-shift Enter (auto-shade)
            {
                int16_t daang;
                int8_t dashade[2] = { 127, -128 };

                i = searchwall;
                do
                {
                    dashade[0] = min(dashade[0], TrackerCast(wall[i].shade));
                    dashade[1] = max(dashade[1], TrackerCast(wall[i].shade));

                    i = wall[i].point2;
                }
                while (i != searchwall);

                daang = getangle(POINT2(searchwall).x-wall[searchwall].x, POINT2(searchwall).y-wall[searchwall].y);

                i = searchwall;
                do
                {
                    j = getangle(POINT2(i).x-wall[i].x,POINT2(i).y-wall[i].y);
                    k = (j+2048-daang)&2047;
                    if (k > 1024)
                        k = 2048-k;

                    wall[i].shade = dashade[0]+mulscale10(k, dashade[1]-dashade[0]);

                    i = wall[i].point2;
                }
                while (i != searchwall);

                message("Auto-shaded wall %d's loop", searchwall);
                asksave = 1;
            }
            else if (somethingintab < 255)
            {
                if (ASSERT_AIMING)
                {
                    AIMED_CF_SEL(shade) = tempshade;
                    AIMED_CF_SEL(pal) = temppal;

                    k = 0;
                    if (AIMING_AT_CEILING_OR_FLOOR)
                    {
                        if (somethingintab == SEARCH_CEILING || somethingintab == SEARCH_FLOOR)
                            k=1, sector[searchsector].visibility = tempvis;
                    }
                    else if (AIMING_AT_SPRITE)
                        sprite[searchwall].blend = tempblend;

                    message("Pasted shade+pal%s", k?"+visibility":"");
                    asksave = 1;
                }
            }
        }
        else if (AIMING_AT_WALL_OR_MASK && eitherCTRL && somethingintab < 255)  //Either ctrl key
        {
            int32_t clipboard_has_wall = (somethingintab == SEARCH_WALL || somethingintab == SEARCH_MASKWALL);
            i = searchwall;
            do
            {
                wall[i].picnum = temppicnum;
                wall[i].shade = tempshade;
                wall[i].pal = temppal;

                if (clipboard_has_wall)
                {
                    wall[i].xrepeat = tempxrepeat;
                    wall[i].yrepeat = tempyrepeat;

                    wall[i].xpanning = tempxpanning;
                    wall[i].ypanning = tempypanning;

                    wall[i].cstat &= ~(4 + 1+64 + 8+256 + 4096);
                    wall[i].cstat |= (tempcstat & (4 + 1+64 + 8+256 + 4096));

                    fixxrepeat(i, templenrepquot);
                }

                i = wall[i].point2;
            }
            while (i != searchwall);

            message("Pasted picnum+shade+pal%s to wall %d's loop",
                    clipboard_has_wall?"+pixelwidth":"", searchwall);
            asksave = 1;
        }
        else if (AIMING_AT_CEILING_OR_FLOOR && eitherCTRL && somethingintab < 255)  //Either ctrl key
        {
            static const char *addnstr[4] = {"", "+stat+panning", "+stat", "+stat + panning (some)"};

            static int16_t sectlist[MAXSECTORS];
            static uint8_t sectbitmap[bitmap_size(MAXSECTORS)];
            int16_t sectcnt, sectnum;

            i = searchsector;
            if (CEILINGFLOOR(i, stat)&1)
            {
                // collect neighboring parallaxed sectors
                bfirst_search_init(sectlist, sectbitmap, &sectnum, MAXSECTORS, i);

                for (sectcnt=0; sectcnt<sectnum; sectcnt++)
                    for (WALLS_OF_SECTOR(sectlist[sectcnt], j))
                    {
                        k = wall[j].nextsector;
                        if (k>=0 && (CEILINGFLOOR(k, stat)&1))
                            bfirst_search_try(sectlist, sectbitmap, &sectnum, wall[j].nextsector);
                    }

                k = 0;
                for (sectcnt=0; sectcnt<sectnum; sectcnt++)
                {
                    i = sectlist[sectcnt];

                    CEILINGFLOOR(i, picnum) = temppicnum;
                    CEILINGFLOOR(i, shade) = tempshade;
                    CEILINGFLOOR(i, pal) = temppal;

                    if (somethingintab == SEARCH_CEILING || somethingintab == SEARCH_FLOOR)
                    {
#ifdef YAX_ENABLE__COMPAT
                        if (yax_getbunch(i, AIMING_AT_FLOOR) >= 0)
                            k |= 2;
                        else
#endif
                        {
                            CEILINGFLOOR(i, xpanning) = tempxrepeat;
                            CEILINGFLOOR(i, ypanning) = tempyrepeat;
                            k |= 1;
                        }

                        SET_PROTECT_BITS(CEILINGFLOOR(i, stat), tempcstat, YAX_BIT_PROTECT);
                    }
                }

                message("Pasted picnum+shade+pal%s to parallaxed sector %ss",
                        addnstr[k], typestr[searchstat]);
                asksave = 1;
            }
#ifdef YAX_ENABLE
            else
            {
                k = yax_getbunch(searchsector, AIMING_AT_FLOOR);
                if (k < 0)
                    goto paste_ceiling_or_floor;

                j = (somethingintab==SEARCH_CEILING || somethingintab==SEARCH_FLOOR);

                for (SECTORS_OF_BUNCH(k,AIMING_AT_FLOOR, i))
                {
                    SECTORFLD(i,picnum, AIMING_AT_FLOOR) = temppicnum;
                    SECTORFLD(i,shade, AIMING_AT_FLOOR) = tempshade;
                    SECTORFLD(i,pal, AIMING_AT_FLOOR) = temppal;
                    if (j)
                        SET_PROTECT_BITS(SECTORFLD(i,stat, AIMING_AT_FLOOR), tempcstat, YAX_BIT_PROTECT);
                }

                message("Pasted picnum+shade+pal%s to sector %ss with bunchnum %d",
                        j?"+stat":"", typestr[searchstat], k);
            }
#endif
        }
        else if (somethingintab < 255)
        {
            // wall/overwall common:
            if (AIMING_AT_WALL_OR_MASK && searchstat==somethingintab &&
                    (!AIMING_AT_WALL || searchwall==searchbottomwall))
            {
                wall[searchwall].xrepeat = tempxrepeat;
                wall[searchwall].yrepeat = tempyrepeat;
                wall[searchwall].xpanning = tempxpanning;
                wall[searchwall].ypanning = tempypanning;

                SET_PROTECT_BITS(wall[searchwall].cstat, tempcstat, ~(4 + 1+64 + 8+256 + 4096));

                wall[searchwall].hitag = temphitag;
#ifdef YAX_ENABLE
                if (yax_getnextwall(searchwall, YAX_CEILING) == -1)
#endif
                    wall[searchwall].lotag = templotag;
#ifdef YAX_ENABLE
                if (yax_getnextwall(searchwall, YAX_FLOOR) == -1)
#endif
                    wall[searchwall].extra = tempextra;

                fixxrepeat(searchwall, templenrepquot);
            }


            if (AIMING_AT_WALL)
            {
                if (searchisbottom)
                    SET_PROTECT_BITS(wall[searchbottomwall].cstat, tempcstat, ~(256 + 4096));

                wall[searchbottomwall].picnum = temppicnum;
                wall[searchbottomwall].shade = tempshade;
                wall[searchbottomwall].pal = temppal;
            }
            else if (AIMING_AT_MASKWALL)
            {
                wall[searchwall].overpicnum = temppicnum;
                if (wall[searchwall].nextwall >= 0)
                    NEXTWALL(searchwall).overpicnum = temppicnum;

                wall[searchwall].shade = tempshade;
                wall[searchwall].pal = temppal;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)
            {
#ifdef YAX_ENABLE
paste_ceiling_or_floor:
#endif
                AIMED_CEILINGFLOOR(picnum) = temppicnum;
                AIMED_CEILINGFLOOR(shade) = tempshade;
                AIMED_CEILINGFLOOR(pal) = temppal;

                if (somethingintab == SEARCH_CEILING || somethingintab == SEARCH_FLOOR)
                {
#ifdef YAX_ENABLE
                    if (yax_getbunch(searchsector, AIMING_AT_FLOOR) < 0)
#endif
                        AIMED_CEILINGFLOOR(xpanning) = tempxrepeat;
                    AIMED_CEILINGFLOOR(ypanning) = tempyrepeat;

                    SET_PROTECT_BITS(AIMED_CEILINGFLOOR(stat), tempcstat, YAX_BIT_PROTECT|2);

                    sector[searchsector].visibility = tempvis;
                    sector[searchsector].lotag = templotag;
                    sector[searchsector].hitag = temphitag;
                    sector[searchsector].extra = tempextra;
                }
            }
            else if (AIMING_AT_SPRITE)
            {
                sprite[searchwall].picnum = temppicnum;

                if (tilesiz[temppicnum].x <= 0 || tilesiz[temppicnum].y <= 0)
                {
                    j = 0;
                    for (k=0; k<MAXUSERTILES; k++)
                        if (tilesiz[k].x > 0 && tilesiz[k].y > 0)
                        {
                            j = k;
                            break;
                        }
                    sprite[searchwall].picnum = j;
                }

                sprite[searchwall].shade = tempshade;
                sprite[searchwall].pal = temppal;

                if (somethingintab == SEARCH_SPRITE)
                {
                    sprite[searchwall].xrepeat = max(tempxrepeat, 1u);
                    sprite[searchwall].yrepeat = max(tempyrepeat, 1u);
                    sprite[searchwall].cstat = tempcstat;
                    sprite[searchwall].lotag = templotag;
                    sprite[searchwall].hitag = temphitag;
                    sprite[searchwall].extra = tempextra;
                    sprite[searchwall].xvel = tempxvel;
                    sprite[searchwall].yvel = tempyvel;
                    sprite[searchwall].zvel = tempzvel;
                    changespritestat(searchwall, tempstatnum);
                    sprite[searchwall].blend = tempblend;
                }
                else
                    correct_sprite_yoffset(searchwall);
            }

            message("Pasted clipboard");
            asksave = 1;
        }
    }

    if (PRESSED_KEYSC(C))
    {
        if (eitherALT)  // Alt-C  picnum replacer
        {
            if (ASSERT_AIMING && somethingintab < 255)
            {
                switch (searchstat)
                {
                case SEARCH_WALL:
                    j = wall[searchbottomwall].picnum;
                    for (i=0; i<numwalls; i++)
                        if (wall[i].picnum == j)
                            wall[i].picnum = temppicnum;
                    break;
                case SEARCH_CEILING:
                case SEARCH_FLOOR:
                    j = AIMED_CEILINGFLOOR(picnum);
                    for (i=0; i<numsectors; i++)
                        if (CEILINGFLOOR(i, picnum) == j)
                            CEILINGFLOOR(i, picnum) = temppicnum;
                    break;
                case SEARCH_SPRITE:
                    j = sprite[searchwall].picnum;
                    for (i=0; i<MAXSPRITES; i++)
                        if (sprite[i].statnum < MAXSTATUS)
                            if (sprite[i].picnum == j)
                            {
                                sprite[i].picnum = temppicnum;
                                correct_sprite_yoffset(i);
                            }
                    break;
                case SEARCH_MASKWALL:
                    j = wall[searchwall].overpicnum;
                    for (i=0; i<numwalls; i++)
                        if (wall[i].overpicnum == j)
                            wall[i].overpicnum = temppicnum;
                    break;
                default:
                    j = -1;
                    break;
                }

                if (j>=0)
                    message("Replaced %ss with picnum %d to picnum %d",
                            typestr[searchstat], j, temppicnum);
                asksave = 1;
            }
        }
        else  //C
        {
            if (AIMING_AT_SPRITE)
            {
                sprite[searchwall].cstat ^= 128;
                message("Sprite %d center bit %s",searchwall, ONOFF((sprite[searchwall].cstat&128)));
                asksave = 1;
            }
        }
    }

    if (PRESSED_KEYSC(SLASH))  // /?     Reset panning&repeat to 0
    {
        if (AIMING_AT_WALL_OR_MASK)
        {
            int16_t w = SELECT_WALL();
            wall[w].xpanning = 0;
            wall[w].ypanning = 0;
            wall[w].xrepeat = 8;
            wall[w].yrepeat = 8;
#ifdef NEW_MAP_FORMAT
            wall[w].cstat = 0;
#else
            wall[w].cstat &= YAX_NEXTWALLBITS;
#endif
            fixrepeats(searchwall);
        }
        else if (AIMING_AT_CEILING_OR_FLOOR)
        {
#ifdef YAX_ENABLE
            int16_t bunchnum = yax_getbunch(searchsector, AIMING_AT_FLOOR);
# if !defined NEW_MAP_FORMAY
            if (bunchnum < 0)
# endif
#endif
                AIMED_CEILINGFLOOR(xpanning) = 0;
            AIMED_CEILINGFLOOR(ypanning) = 0;
            AIMED_CEILINGFLOOR(stat) &= ~2;
            AIMED_CEILINGFLOOR(heinum) = 0;
#ifdef YAX_ENABLE
            if (bunchnum >= 0)
                for (SECTORS_OF_BUNCH(bunchnum,!AIMING_AT_FLOOR, i))
                {
                    SECTORFLD(i,stat, !AIMING_AT_FLOOR) &= ~2;
                    SECTORFLD(i,heinum, !AIMING_AT_FLOOR) = 0;
                }
#endif
        }
        else if (AIMING_AT_SPRITE)
        {
            if (!eitherCTRL)
            {
                if (eitherSHIFT)
                    sprite[searchwall].xrepeat = sprite[searchwall].yrepeat;
                else
                {
                    sprite[searchwall].xrepeat = 64;
                    sprite[searchwall].yrepeat = 64;
                }
            }

            if (!eitherALT)
            {
                sprite[searchwall].xoffset = 0;
                sprite[searchwall].yoffset = 0;
            }

            correct_sprite_yoffset(searchwall);
        }

        if (ASSERT_AIMING)
        {
            message("%s's size, panning and slope reset", Typestr[searchstat]);
            asksave = 1;
        }
    }

#include "../../build/include/m32_editor_3d_tail_sprite_search_shared.cpp.inc"

////////// KEY PRESS HANDLER IN 2D MODE //////////
static void Keys2d(void)
{
    int32_t i=0, j, k;
    int32_t smooshy, changedir;
    static int32_t repeatcnt[2] = {0,0};  // was repeatcountx, repeatcounty
    int32_t tcursectornum;

//    for(i=0;i<0x50;i++) if(keystatus[i]==1) Bsprintf(tempbuf,"key %d",i); printmessage16(tempbuf);

    tcursectornum = -1;
    for (i=0; i<numsectors; i++)
    {
        YAX_SKIPSECTOR(i);
        if (inside_editor_curpos(i) == 1)
        {
            tcursectornum = i;
            break;
        }
    }
    searchsector = tcursectornum;

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

    if ((i=tcursectornum)>=0 && g_fillCurSector && !bitmap_test(hlsectorbitmap, i))
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
                calc_sector_reachability();
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
            printmessage16("Display grayed-out walls: %s", ONOFF(showinnergray));
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
        if (eitherALT)
        {
            showgraysectors = !showgraysectors;
            printmessage16("Display grayed-out sectors: %s", ONOFF(showgraysectors));
        }
        else
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
    }

#include "../../build/include/duke_rr_editor_2d_tag_shared.cpp.inc"
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

                if ((ppointhighlight&0xc000) == 16384 && (sprite[cursprite].cstat & CSTAT_SPRITE_ALIGNMENT))
                {
                    uint8_t *repeat = (k==0) ? &sprite[cursprite].xrepeat : &sprite[cursprite].yrepeat;
                    *repeat = max<uint8_t>(1, changechar(*repeat, changedir, smooshy, 1));
                    silentmessage("Sprite %d repeat: %d, %d", cursprite,
                        TrackerCast(sprite[cursprite].xrepeat),
                        TrackerCast(sprite[cursprite].yrepeat));
                }
                else if (graphicsmode)
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
                repeatcnt[k] = max(1, repeatcnt[k]-2);
            }
            repeatcnt[k] += synctics;
        }
        else
            repeatcnt[k] = 0;
    }

#include "../../build/include/duke_rr_editor_2d_relative_search_shared.cpp.inc"

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
                LOG_F(WARNING, "Set sector %d's wallptr to %d (was %d)", i,
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
    else
    {
        size_t constexpr size = ARRAY_SIZE(tempbuf);
        size_t bytesWritten = Bsnprintf(tempbuf, size, "Application parameters:");
        for (i = 1; i < argc; ++i)
            bytesWritten += Bsnprintf(tempbuf + bytesWritten, size - bytesWritten, " %s", argv[i]);
        LOG_F(INFO, "%s", tempbuf);
    }

    lengths = (int32_t *)Xmalloc(argc*sizeof(int32_t));
    for (j=1; j<argc; j++)
    {
        lengths[j] = Bstrlen(argv[j]);
        maxlen += lengths[j];
    }

    testplay_addparam = (char *)Xmalloc(maxlen+argc);
    testplay_addparam[0] = 0;

    j = 0;
    i = 1;

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
                Bexit(EXIT_SUCCESS);
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
                        LOG_F(INFO, "Cache size: %dkB",sz);

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
            if (!Bstrcasecmp(c+1,"nam"))
            {
                g_gameType = GAMEFLAG_NAM;
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"napalm"))
            {
                g_gameType = GAMEFLAG_NAM|GAMEFLAG_NAPALM;
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"ww2gi"))
            {
                g_gameType = GAMEFLAG_WW2GI;
                COPYARG(i);
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"check"))
            {
                LOG_F(INFO, "Map wall checking on save enabled");
                fixmaponsave_walls = 1;
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"nocheck"))
            {
                LOG_F(INFO, "Map wall checking on save disabled");
                fixmaponsave_walls = 0;
                i++;
                continue;
            }
            if (!Bstrcasecmp(c+1,"noautoload"))
            {
                LOG_F(INFO, "Autoload disabled");
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

    Xfree(lengths);

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
    if (buildvfs_exists("m32_usecwd"))
        g_useCwd = 1;

    OSD_SetVersion(AppProperName,0,2);
    LOG_F(INFO, "Mapster32 %s", s_buildRev);
    PrintBuildInfo();

    G_CheckCommandLine(argc,argv);

    if (Bstrcmp(setupfilename, SETUPFILENAME))
        LOG_F(INFO, "Using config file %s",setupfilename);

    int dosetup = 0;

    if (loadsetup(setupfilename) < 0)
        LOG_F(INFO, "Configuration file not found, using defaults."), dosetup = 1;

    bpp = bppgame;

    return dosetup;
}

static int osdcmd_quit(osdcmdptr_t UNUSED(parm))
{
    UNREFERENCED_CONST_PARAMETER(parm);

    OSD_ShowDisplay(0);

    ExtUnInit();
    engineUnInit();

    Bfflush(NULL);

    Bexit(EXIT_SUCCESS);
}

static int osdcmd_artdump(osdcmdptr_t parm)
{
    UNREFERENCED_CONST_PARAMETER(parm);

    BFILE *f = Bfopen("tilesxxx.art", "wb");
    uint32_t first = 0;
    uint32_t last = MAXUSERTILES-1;
    uint32_t numtiles = 0;

    if (parm->numparms >= 1)
        first = Batol(parm->parms[0]);

    if (parm->numparms >= 2)
        last = Batol(parm->parms[1]);

    if (first >= MAXUSERTILES || last >= MAXUSERTILES)
    {
        Bfclose(f);
        return OSDCMD_SHOWHELP;
    }

    for (uint32_t i = last; i > first; --i)
        if (tileLoad(i))
        {
            numtiles = i + 1;
            break;
        }

    int32_t s32;

    // header
    s32 = B_LITTLE32(0x4c495542);
    Bfwrite(&s32, sizeof(int32_t), 1, f);
    s32 = B_LITTLE32(0x54524144);
    Bfwrite(&s32, sizeof(int32_t), 1, f);

    // artversion
    s32 = 1;
    Bfwrite(&s32, sizeof(int32_t), 1, f);

    // numtiles
    Bfwrite(&numtiles, sizeof(int32_t), 1, f);

    // localtilestart
    s32 = first;
    Bfwrite(&s32, sizeof(int32_t), 1, f);

    // localtileend
    s32 = numtiles - 1;
    Bfwrite(&s32, sizeof(int32_t), 1, f);  // tileend

    // tilesizx
    for (uint32_t i = first; i < numtiles; ++i)
        Bfwrite(&tilesiz[i].x, sizeof(int16_t), 1, f);

    // tilesizy
    for (uint32_t i = first; i < numtiles; ++i)
        Bfwrite(&tilesiz[i].y, sizeof(int16_t), 1, f);

    // picanm
    for (uint32_t i = first; i < numtiles; ++i)
    {
        picanm_t p = picanm[i];

        // undo picanm reorder...
        p.num &= ~192;
        p.num |= p.sf & 192;
        p.sf &= 0x0F;

        // do not write the tileflags member
        Bfwrite(&p.num, sizeof(uint8_t), 1, f);
        Bfwrite(&p.xofs, sizeof(int8_t), 1, f);
        Bfwrite(&p.yofs, sizeof(int8_t), 1, f);
        Bfwrite(&p.sf, sizeof(uint8_t), 1, f);
    }

    for (uint32_t i = first; i < numtiles; ++i)
    {
        tileLoad(i);
        Bfwrite((void *)waloff[i], tilesiz[i].x * tilesiz[i].y, 1, f);
    }

    Bfclose(f);

    return OSDCMD_OK;
}

static int osdcmd_editorgridextent(osdcmdptr_t parm)
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

static int osdcmd_upscalefactor(osdcmdptr_t parm)
{
    int32_t i;

    if (parm->numparms == 0)
    {
        OSD_Printf("\"upscalefactor\" is \"%d\"\n", upscalefactor);
        return OSDCMD_SHOWHELP;
    }
    else if (parm->numparms != 1)
        return OSDCMD_SHOWHELP;

    i = Batol(parm->parms[0]);

    if (i >= 1 && i <= 3)
    {
        upscalefactor = i;
        OSD_Printf("upscalefactor %d\n", upscalefactor);

        if (!in3dmode())
        {
            videoSet2dMode(fullscreen, xres, yres, upscalefactor);
            return OSDCMD_OK;
        }

    }
    else
        OSD_Printf("upscalefactor: specify a value 1-3\n");

    return OSDCMD_OK;
}

#include "../../build/include/mapster_osd_command_bodies_shared.cpp.inc"

#ifdef USE_OPENGL
static int osdcmd_tint(osdcmdptr_t parm)
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
        Xfree(scripthist[scripthistend]);
        scripthist[scripthistend] = Xstrdup(commandstr);
        scripthistend++;
        scripthistend %= SCRIPTHISTSIZ;
    }
}

#include "../../build/include/mapster_m32_script_osd_handlers_shared.cpp.inc"

static int32_t registerosdcommands(void)
{
#define MAPSTER_REGISTER_ARTDUMP_COMMAND
#define MAPSTER_REGISTER_UPSCALEFACTOR_COMMAND
#define MAPSTER_SHARED_REGISTER_OSD_COMMANDS
#include "../../build/include/mapster_osd_commands_shared.cpp.inc"

    static osdcvardata_t cvars_editor[] =
    {
        { "cameraheight", "adjust editor camera height", (void*)&kensplayerheight, CVAR_INT, 1, 255 },
    };

    for (auto & i : cvars_editor)
        OSD_RegisterCvar(&i, osdcmd_cvar_set);

    return 0;
}


enum
{
    T_INCLUDE = 0,
    T_DEFINE = 1,
    T_LOADGRP,
    T_CACHESIZE,
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
    T_INCLUDEOPTIONAL,

    T_GLOBALGAMEFLAGS,

    T_GAMESTARTUP,

    T_SOUND,
    T_FILE,
    T_ID,
    T_MINPITCH,
    T_MAXPITCH,
    T_PRIORITY,
    T_TYPE,
    T_DISTANCE,
    T_VOLUME,

    T_DUMMY,
};

static int32_t parsegroupfiles(scriptfile *script);

static void parsegroupfiles_include(const char *fn, scriptfile *script, const char *cmdtokptr)
{
    scriptfile *included = scriptfile_fromfile(fn);

    if (!included)
    {
        if (!Bstrcasecmp(cmdtokptr,"null"))
            LOG_F(WARNING, "warning: failed including %s as module", fn);
        else
            LOG_F(WARNING, "%s:%d: warning: failed including %s",
                           script->filename, scriptfile_getlinum(script, cmdtokptr),
                           fn);
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
        { "define",          T_DEFINE },
        { "#define",         T_DEFINE },
        { "loadgrp",         T_LOADGRP },
        { "cachesize",       T_CACHESIZE },
        { "noautoload",      T_NOAUTOLOAD },
        { "sound",           T_SOUND },
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
        case T_CACHESIZE:
        {
            int32_t cacheSize;

            if (scriptfile_getnumber(script, &cacheSize))
                break;

            if (cacheSize > 0)
                g_maxCacheSize = cacheSize << 10;
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
        case T_DEFINE:
        {
            char *name;
            int32_t number;

            if (scriptfile_getstring(script, &name)) break;
            if (scriptfile_getsymbol(script, &number)) break;

            if (EDUKE32_PREDICT_FALSE(scriptfile_addsymbolvalue(name, number) < 0))
                LOG_F(WARNING, "%s:%d: warning: symbol %s was NOT redefined to %d",
                               script->filename, scriptfile_getlinum(script, cmdtokptr),
                               name, number);
            break;
        }
        case T_NOAUTOLOAD:
            NoAutoLoad = 1;
            break;
        case T_SOUND:
        {
            char *tokenPtr = script->ltextptr;
            char *fileName = NULL;
            char *soundEnd;

            double volume = 1.0;

            int32_t soundNum = -1;
            int32_t maxpitch = 0;
            int32_t minpitch = 0;
            int32_t priority = 0;
            int32_t type     = 0;
            int32_t distance = 0;

            if (scriptfile_getbraces(script, &soundEnd))
                break;

            char * definedName = nullptr;

            static const tokenlist soundTokens[] =
            {
                { "id",       T_ID },
                { "file",     T_FILE },
                { "minpitch", T_MINPITCH },
                { "maxpitch", T_MAXPITCH },
                { "priority", T_PRIORITY },
                { "type",     T_TYPE },
                { "distance", T_DISTANCE },
                { "volume",   T_VOLUME },
            };

            while (script->textptr < soundEnd)
            {
                switch (getatoken(script, soundTokens, ARRAY_SIZE(soundTokens)))
                {
                    case T_ID:
                        definedName = script->ltextptr;
                        scriptfile_getsymbol(script, &soundNum);
                        break;
                    case T_FILE:     scriptfile_getstring(script, &fileName); break;
                    case T_MINPITCH: scriptfile_getsymbol(script, &minpitch); break;
                    case T_MAXPITCH: scriptfile_getsymbol(script, &maxpitch); break;
                    case T_PRIORITY: scriptfile_getsymbol(script, &priority); break;
                    case T_TYPE:     scriptfile_getsymbol(script, &type);     break;
                    case T_DISTANCE: scriptfile_getsymbol(script, &distance); break;
                    case T_VOLUME:   scriptfile_getdouble(script, &volume);   break;
                }
            }

            if (soundNum==-1)
            {
                LOG_F(ERROR, "%s:%d: error: missing ID for sound definition",
                             script->filename, scriptfile_getlinum(script, tokenPtr));
                break;
            }

            if (fileName == NULL || check_file_exist(fileName))
                break;

            if (S_DefineSound(soundNum, fileName, definedName, minpitch, maxpitch, priority, type, distance, volume) == -1)
                LOG_F(ERROR, "%s:%d: error: invalid sound ID",
                             script->filename, scriptfile_getlinum(script, tokenPtr));
        }
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

int32_t parsetilegroups(scriptfile *script)
{
    int32_t tokn;
    char *cmdtokptr;

    tokenlist tgtokens[] =
    {
        { "include",         T_INCLUDE          },
        { "#include",        T_INCLUDE          },
        { "define",          T_DEFINE           },
        { "#define",         T_DEFINE           },

        { "dynamicremap",    T_DUMMY            },

        { "tilegroup",       T_TILEGROUP        },
        { "spritehotkey",    T_HOTKEY           },
        { "alphabet",        T_ALPHABET         },
    };

    while (1)
    {
        tokn = getatoken(script,tgtokens,ARRAY_SIZE(tgtokens));
        cmdtokptr = script->ltextptr;
        switch (tokn)
        {
        case T_HOTKEY:
        {
            int32_t i, j;
            if (scriptfile_getsymbol(script,&i)) break;
            if (scriptfile_getsymbol(script,&j)) break;
            if (i < 0 || i > 9 || (unsigned)j >= MAXUSERTILES) break;
            prefixtiles[i] = j;
            break;
        }
        case T_INCLUDE:
        {
            char *fn;
            if (!scriptfile_getstring(script,&fn))
            {
                scriptfile *included;

                included = scriptfile_fromfile(fn);
                if (!included)
                {
                    LOG_F(WARNING, "%s:%d: warning: failed including %s",
                                   script->filename, scriptfile_getlinum(script,cmdtokptr),
                                   fn);
                }
                else
                {
                    parsetilegroups(included);
                    scriptfile_close(included);
                }
            }
            break;
        }
        case T_DEFINE:
        {
            char *name;
            int32_t number;

            if (scriptfile_getstring(script,&name)) break;
            if (scriptfile_getsymbol(script,&number)) break;
            if (scriptfile_addsymbolvalue(name,number) < 0)
                LOG_F(WARNING, "%s:%d: warning: symbol %s was NOT redefined to %d",
                               script->filename, scriptfile_getlinum(script,cmdtokptr),
                               name, number);
            break;
        }
        case T_TILEGROUP:
        {
            char *end, *name;
            int32_t i;

            if (tile_groups >= MAX_TILE_GROUPS) break;
            if (scriptfile_getstring(script,&name)) break;
            if (scriptfile_getbraces(script,&end)) break;

            TileGroup *const tileGrp = &s_TileGroups[tile_groups];

            tileGrp->pIds = (int32_t *)Xcalloc(MAX_TILE_GROUP_ENTRIES, sizeof(int32_t));
            tileGrp->szText = Xstrdup(name);

            while (script->textptr < end)
            {
                static const tokenlist tgtokens2[] =
                {
                    { "tilegroup",  T_TILEGROUP   },
                    { "tile",       T_TILE        },
                    { "tilerange",  T_TILERANGE   },
                    { "hotkey",     T_HOTKEY      },
                    { "tiles",      T_TILES       },
                    { "colors",     T_COLORS      },
                };

                const int32_t token = getatoken(script,tgtokens2,ARRAY_SIZE(tgtokens2));

                switch (token)
                {
                case T_TILE:
                {
                    if (scriptfile_getsymbol(script,&i)) break;
                    if (i >= 0 && i < MAXUSERTILES && tileGrp->nIds < MAX_TILE_GROUP_ENTRIES)
                        tileGrp->pIds[tileGrp->nIds++] = i;
//                    OSD_Printf("added tile %d to group %d\n",i,g);
                    break;
                }
                case T_TILERANGE:
                {
                    int32_t j;
                    if (scriptfile_getsymbol(script,&i)) break;
                    if (scriptfile_getsymbol(script,&j)) break;
                    if (i < 0 || i >= MAXTILES || (unsigned)j >= MAXUSERTILES) break;
                    while (tileGrp->nIds < MAX_TILE_GROUP_ENTRIES && i <= j)
                    {
                        tileGrp->pIds[tileGrp->nIds++] = i++;
//                        OSD_Printf("added tile %d to group %d\n",i,g);
                    }
                    break;
                }
                case T_COLORS:
                {
                    int32_t j;
                    if (scriptfile_getsymbol(script, &i)) break;
                    if (scriptfile_getsymbol(script, &j)) break;
                    if (i < 0 || i >= 256 || j < 0 || j >= 256) break;
                    tileGrp->color1 = i;
                    tileGrp->color2 = j;
                    break;
                }
                case T_HOTKEY:
                {
                    char *c;
                    if (scriptfile_getstring(script,&c)) break;
                    tileGrp->key1 = Btoupper(c[0]);
                    tileGrp->key2 = Btolower(c[0]);
                    break;
                }
                case T_TILES:
                {
                    char *end2;
                    if (scriptfile_getbraces(script,&end2)) break;
                    while (script->textptr < end2-1)
                    {
                        if (!scriptfile_getsymbol(script,&i))
                        {
                            if ((unsigned)i < MAXUSERTILES && tileGrp->nIds < MAX_TILE_GROUP_ENTRIES)
                                tileGrp->pIds[tileGrp->nIds++] = i;
//                            OSD_Printf("added tile %d to group %d\n",i,g);
                        }
                    }
                    break;
                }
                }
            }

            tileGrp->pIds = (int32_t *)Xrealloc(tileGrp->pIds, tileGrp->nIds*sizeof(int32_t));
            tile_groups++;
            break;
        }
        case T_ALPHABET:
        {
            char *end;
            int32_t i, j, k;

            if (numalphabets >= MAX_ALPHABETS)
            {
                OSD_Printf("Too many alphabet definitions (max: %d).\n", MAX_ALPHABETS);
                break;
            }

            if (scriptfile_getbraces(script,&end)) break;

            for (i=0; i<NUMPRINTABLES; i++)
            {
                alphabets[numalphabets].pic[i] = -1;
                alphabets[numalphabets].xofs[i] = 0;
                alphabets[numalphabets].yofs[i] = 0;
            }

            while (script->textptr < end)
            {
                tokenlist alphtokens2[] =
                {
                    { "map",        T_MAP         },
                    { "mapa",       T_MAPA        },
                    { "maprange",   T_MAPRANGE    },
                    { "maprangea",  T_MAPRANGEA   },
                    { "offset",     T_OFFSET      },
                    { "offseta",    T_OFFSETA     },
                };

                int32_t token = getatoken(script,alphtokens2,ARRAY_SIZE(alphtokens2));
                switch (token)
                {
                case T_MAP:  // map <ascii num> <start tilenum>, e.g. map 46 3002
                {
                    if (scriptfile_getnumber(script,&i)) break;
                    if (scriptfile_getsymbol(script,&j)) break;

                    if (i>=33 && i<=126 && (unsigned)j<MAXUSERTILES)
                        alphabets[numalphabets].pic[i-33] = j;

                    break;
                }
                case T_MAPA:  // mapa <ascii string> <start tilenum>, e.g. map ".,!?" 3002
                {
                    char *s;
                    if (scriptfile_getstring(script,&s)) break;
                    if (scriptfile_getsymbol(script,&i)) break;

                    for (; *s; s++, i++)
                    {
                        if (*s>=33 && *s<=126 && (unsigned)i<MAXUSERTILES)
                            alphabets[numalphabets].pic[(*s)-33] = i;
                    }
                    break;
                }
                // maprange <start ascii num> <end ascii num> <start tilenum>, e.g. map 33 126 STARTALPHANUM
                // maprangea <start char> <end char> <start tilenum>, e.g. map "!" "~" STARTALPHANUM
                case T_MAPRANGE:
                case T_MAPRANGEA:
                {
                    if (token==T_MAPRANGE)
                    {
                        if (scriptfile_getnumber(script,&i)) break;
                        if (scriptfile_getnumber(script,&j)) break;
                    }
                    else
                    {
                        char *c1, *c2;
                        if (scriptfile_getstring(script,&c1)) break;
                        if (scriptfile_getstring(script,&c2)) break;
                        i=*c1;
                        j=*c2;
                    }
                    if (scriptfile_getsymbol(script,&k)) break;

                    if (i>126 || j<33) break;
                    for (; i<=j && k<MAXUSERTILES; i++, k++)
                    {
                        if (i>=33 && i<=126)
                            alphabets[numalphabets].pic[i-33] = k;
                    }
                    break;
                }
                case T_OFFSET:  // offset <ascii num> <xoffset> <yoffset>
                {
                    if (scriptfile_getnumber(script, &i)) break;
                    if (scriptfile_getnumber(script, &j)) break;
                    if (scriptfile_getnumber(script, &k)) break;

                    if (i >= 33 && i <= 126)
                    {
                        alphabets[numalphabets].xofs[i-33] = j;
                        alphabets[numalphabets].yofs[i-33] = k;
                    }
                    break;
                }
                case T_OFFSETA:  // offseta <ascii string> <xoffset> <yoffset>
                {
                    char *s;
                    if (scriptfile_getstring(script, &s)) break;
                    if (scriptfile_getnumber(script, &i)) break;
                    if (scriptfile_getnumber(script, &j)) break;

                    for (; *s; s++)
                        if (*s >= 33 && *s <= 126)
                        {
                            alphabets[numalphabets].xofs[(*s)-33] = i;
                            alphabets[numalphabets].yofs[(*s)-33] = j;
                        }
                    break;
                }
                }
            }
            numalphabets++;
            break;
        }
        case T_EOF:
            return 0;
        default:
            break;
        }
    }
    return 0;
}

static int32_t loadtilegroups(const char *fn)
{
    int32_t i, j;
    scriptfile *script;
    TileGroup blank = { NULL, 0, NULL, 0, 0, 0, 0};

    script = scriptfile_fromfile(fn);
    if (!script) return -1;

    for (i=0; i<tile_groups; i++)
    {
        Xfree(s_TileGroups[i].pIds);
        Xfree(s_TileGroups[i].szText);
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

static void parseconsounds_include(const char *fn, scriptfile *script, const char *cmdtokptr, bool optional)
{
    scriptfile *included;

    included = scriptfile_fromfile(fn);
    if (!included)
    {
        if (optional)
        {
            LOG_F(INFO, "Optional module %s absent, skipping.", fn);
            return;
        }

        if (!Bstrcasecmp(cmdtokptr,"null"))
            LOG_F(WARNING, "warning: failed including %s as module", fn);
        else
            LOG_F(WARNING, "%s:%d: warning: failed including %s",
                           script->filename,scriptfile_getlinum(script,cmdtokptr),
                           fn);
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

    tokenlist cstokens[] =
    {
        { "include",         T_INCLUDE          },
        { "#include",        T_INCLUDE          },
        { "includedefault",  T_INCLUDEDEFAULT   },
        { "#includedefault", T_INCLUDEDEFAULT   },
        { "includeoptional", T_INCLUDEOPTIONAL  },
        { "#includeoptional",T_INCLUDEOPTIONAL  },
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
        case T_INCLUDEOPTIONAL:
        case T_INCLUDE:
        {
            char *fn;
            if (!scriptfile_getstring(script,&fn))
                parseconsounds_include(fn, script, cmdtokptr, (tokn == T_INCLUDEOPTIONAL));
            break;
        }
        case T_INCLUDEDEFAULT:
        {
            parseconsounds_include(G_DefaultConFile(), script, cmdtokptr, false);
            break;
        }
        case T_DEFINE:
        {
            char *name;
            int32_t number;

            if (scriptfile_getstring(script,&name)) break;
            if (scriptfile_getsymbol(script,&number)) break;
            if (scriptfile_addsymbolvalue(name,number) < 0)
                LOG_F(WARNING, "%s:%d: warning: symbol %s was NOT redefined to %d",
                                script->filename, scriptfile_getlinum(script,cmdtokptr),
                                name, number);
            break;
        }
        case T_GAMESTARTUP:
        {
            if (scriptfile_getsymbol(script, &g_visibility)) break;
            break;
        }
        case T_DEFINESOUND:
        {
            char * filename;
            int32_t sndnum, ps, pe, pr, m, vo;
            int32_t slen;

            if (scriptfile_getsymbol(script, &sndnum)) break;

            char * definedname = script->ltextptr;
            float volume = 1.0;

            if (sndnum < 0 || sndnum >= MAXSOUNDS)
            {
                LOG_F(WARNING, "%s:%d: warning: invalid sound definition %s (sound number < 0 or >= MAXSOUNDS)",
                               script->filename,scriptfile_getlinum(script,cmdtokptr),
                               definedname);
                break;
            }

            if (scriptfile_getstring(script, &filename))
                break;

            slen = Bstrlen(filename);
            if (slen >= BMAX_PATH)
            {
                LOG_F(WARNING, "%s:%d: warning: invalid sound definition %s (filename too long)",
                               script->filename,scriptfile_getlinum(script,cmdtokptr),
                               definedname);
                break;
            }

            if (scriptfile_getnumber(script, &ps)) goto BAD;
            if (scriptfile_getnumber(script, &pe)) goto BAD;
            if (scriptfile_getnumber(script, &pr)) goto BAD;
            if (scriptfile_getnumber(script, &m)) goto BAD;
            if (ParentalLock && (m&8)) goto BAD;
            if (scriptfile_getnumber(script, &vo)) goto BAD;
            if (0)
            {
BAD:
                break;
            }

            if (S_DefineSound(sndnum, filename, definedname, ps, pe, pr, m, vo, volume) == -1)
                LOG_F(ERROR, "%s:%d: error: invalid sound ID",
                             script->filename, scriptfile_getlinum(script, cmdtokptr));

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

    LOG_F(INFO, "Loading sounds from \"%s\"",fn);

    script = scriptfile_fromfile(fn);
    if (!script)
    {
        LOG_F(ERROR, "Error loading sounds: file \"%s\" not found.", fn);
        return -1;
    }
    ret = parseconsounds(script);

    for (char * m : g_scriptModules)
    {
        parseconsounds_include(m, NULL, "null", false);
        Xfree(m);
    }
    g_scriptModules.clear();

    if (ret < 0)
        LOG_F(ERROR, "There was an error parsing \"%s\".", fn);
    else if (ret == 0)
        LOG_F(INFO, "\"%s\" doesn't contain sound definitions. No sounds loaded.", fn);
    else
        LOG_F(INFO, "Loaded %d sound definitions.", ret);

    if (g_visibility != 512)
        LOG_F(INFO, "Global visibility: %d", g_visibility);

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
        char *dummy = NULL;
        char *ptr = Xstrdup(setupfilename), *p = Bstrtoken(ptr, ".", &dummy, 1);
        if (!Bstrcmp(setupfilename, SETUPFILENAME))
            Bsprintf(tempbuf, "m32_settings.cfg");
        else Bsprintf(tempbuf,"%s_m32_settings.cfg",p);
        OSD_Exec(tempbuf);
        Xfree(ptr);
    }

    // backup pathsearchmode so that a later open
    // will hopefully be the same file
    pathsearchmode_oninit = pathsearchmode = 0;

    G_ScanGroups();

    // reset search mode to full file system
    pathsearchmode = 1;

    signal(SIGINT, m32script_interrupt_handler);

    return rv;
}

int32_t ExtPostStartupWindow(void)
{
    if (!g_useCwd)
        G_CleanupSearchPaths();

    G_LoadGroups(!NoAutoLoad);

    if (engineInit())
    {
        LOG_F(ERROR, "There was a problem initializing the engine: %s", engineerrstr);
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
        Xfree(s_TileGroups[i].pIds);
        Xfree(s_TileGroups[i].szText);
    }
    for (i = numhelppages-1; i >= 0; i--) Xfree(helppage[i]);
    Xfree(helppage);
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

                    if (!bitmap_test(previewflag, isec))
                    {
                        start_wall = sector[isec].wallptr;
                        end_wall = start_wall + sector[isec].wallnum;
    
                        for (w = start_wall; w < end_wall; w++)
                        {
                            if (!bitmap_test(wallflag, w))
                            {
                                wallshades[w] = wall[w].shade;
                                wallpals[w] = wall[w].pal;
    
                                wall[w].shade = sprite[i].shade;
                                wall[w].pal = sprite[i].pal;
    
                                bitmap_set(wallflag, w);
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
                        bitmap_set(previewflag, isec);
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
            for (i=0; i<highlightsectorcnt; i++)
            {
                ii = highlightsector[i];
                selectpals[ii][0] = sector[ii].floorpal;
                selectpals[ii][1] = sector[ii].ceilingpal;

                sector[ii].floorpal = sector[ii].ceilingpal = 6;
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
            auto pSprite = (uspriteptr_t)&sprite[i];

            if ((pSprite->cstat & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_FACING || pSprite->statnum == MAXSTATUS || (unsigned)pSprite->sectnum >= MAXSECTORS) continue;
            if (bitmap_test(graysectbitmap, pSprite->sectnum)) continue;

            int daang = 0, flags = 0, shade = 0, frames = 0;
            int picnum = pSprite->picnum;
            int32_t xp1, yp1;

            if ((sprite[i].cstat & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_FACING || sprite[i].statnum == MAXSTATUS) continue;

            ii++;

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
        {
            YAX_SKIPSECTOR(ii);

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
        Bassert((unsigned)tspr->owner < MAXSPRITES);
        Duke_ApplySpritePropertiesToTSprite(tspr, (uspriteptr_t)&sprite[tspr->owner]);

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

            const int32_t wallaligned = (tspr->cstat & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_WALL;
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

            if (!tilehasmodelorvoxel(tspr->picnum,tspr->pal))
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
#define MESSAGEY 3 // ((i/charsperline)<<3)+(ydim-(ydim>>3))-(((getmessageleng-1)/charsperline)<<3)

static void Keys2d3d(void)
{
    int32_t i;

#if M32_UNDO
    if (mapstate == NULL)
    {
        //        map_revision = 0;
        create_map_snapshot(); // initial map state
        //        Xfree(mapstate->next);
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

    if (PRESSED_KEYSC(F11))  //F11 - brightness
    {
        static int16_t brightness = -1;

        if (brightness==-1)
            brightness = ((int16_t)((g_videoGamma-1.0)*16.0))&15;

        brightness = brightness + (1-2*eitherSHIFT);
        brightness &= 15;

        g_videoGamma = 1.0 + ((float)brightness / 16.0);
        videoSetPalette(brightness, BASEPAL, 0);
        message("Brightness: %d/16", brightness+1);
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

        if (PRESSED_KEYSC(Z)) // CTRL+Z
        {
            if (EDITING_MAP_P())
                message("Can't undo or redo while editing!");
            else if (!in3dmode() || m32_3dundo)
            {
                if (eitherSHIFT)
                {
                    if (map_undoredo(1))
                        message("Nothing to redo!");
                    else
                        message("Redo: restored revision %d", map_revision - 1);
                }
                else
                {
                    if (map_undoredo(0))
                        message("Nothing to undo!");
                    else
                        message("Undo: restored revision %d", map_revision - 1);
                }

                updatesectorz(pos.x, pos.y, pos.z, &cursectnum);

                // kick the user back to 2d mode if the sector they were in was deleted by the undo/redo operation
                if (cursectnum == -1 && (in3dmode() && !m32_is2d3dmode()))
                    keystatus[buildkeys[BK_MODE2D_3D]] = 1;
            }
        }

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
            sector[ii].floorpal = selectpals[ii][0];
            sector[ii].ceilingpal = selectpals[ii][1];
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
            
            if (bitmap_test(previewflag, isec))
            {
                start_wall = sector[isec].wallptr;
                end_wall = start_wall + sector[isec].wallnum;
    
                for (w = start_wall; w < end_wall; w++)
                {
                    if (bitmap_test(wallflag, w))
                    {
                        wall[w].shade = wallshades[w];
                        wall[w].pal = wallpals[w];
                        bitmap_clear(wallflag, w);
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
            bitmap_clear(previewflag, isec);
            }
        }
    }

    lastbstatus = bstatus;
    bstatus = mouseReadButtons();

    //if (mlook && in3dmode() && m32_is2d3dmode())
    //    mskip=1;

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

        FX_StopAllSounds();
        S_ClearSoundLocks();
    }

#include "../../build/include/m32_editor_save_screenshot_tail_shared.cpp.inc"

////

void faketimerhandler(void) { ; }

#include "../../build/include/m32_editor_palette_sector_search_shared.cpp.inc"

//////////////////// manual editing ////////////////////
// Build edit originally by Ed Coolidge <semicharm@earthlink.net>

#include "../../build/include/m32_editor_manual_edit_core_shared.cpp.inc"

#include "../../build/include/m32_manual_edit_handle_common_shared.cpp.inc"

static void EditSectorData(int16_t sectnum)
{
    int32_t col=1, row=0, rowmax = 6;
    int32_t i = -1;
    int32_t xpos = 208, ypos = ydim-STATUS2DSIZ+48;

    med_editval = 0;

    med_dispwidth = 24;
    med_disptext[med_dispwidth] = 0;

    med_typename = "Sector";
    med_thenum = sectnum;

    drawgradient();

    showsectordata(sectnum, 0);

    while (keystatus[KEYSC_ESC] == 0)
    {
        med_handlecommon(xpos, ypos, &row, rowmax);

        if (PRESSED_KEYSC(LEFT))
        {
            if (col == 2)
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 1;
                xpos = 208;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
        }
        if (PRESSED_KEYSC(RIGHT))
        {
            if (col == 1)
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 2;
                xpos = 408;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
        }

#ifdef YAX_ENABLE
        if (med_editval)
        {
            if ((row==1 || row==3 || row==5) && yax_getbunch(sectnum, (col==2)) >= 0)
                med_editval = 0;
        }
#endif

        if (col == 1)
        {
            switch (row)
            {
            case 0:
#ifdef YAX_ENABLE__COMPAT
                i = sector[sectnum].ceilingstat&YAX_BIT;
#endif
                handlemed(1, "Flags (hex)", "Ceiling Flags", &sector[sectnum].ceilingstat,
                          sizeof(sector[sectnum].ceilingstat), 65535, 0);
#ifdef YAX_ENABLE__COMPAT
                sector[sectnum].ceilingstat &= ~YAX_BIT;
                sector[sectnum].ceilingstat |= i;
#endif
                break;
            case 1:
                i = Bsprintf(med_disptext,"(X,Y)pan: %d, %d",
                             TrackerCast(sector[sectnum].ceilingxpanning),
                             TrackerCast(sector[sectnum].ceilingypanning));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sector %d Ceiling X Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].ceilingxpanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].ceilingxpanning,255,0);
                    Bsprintf(med_edittext,"Sector %d Ceiling Y Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].ceilingypanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].ceilingypanning,255,0);
                }
                break;
            case 2:
                handlemed(0, "Shade byte", "Ceiling Shade", &sector[sectnum].ceilingshade,sizeof(sector[sectnum].ceilingshade), 128, 1);
                break;
            case 3:
                handlemed(0, "Z-coordinate", "Ceiling Z-coordinate", &sector[sectnum].ceilingz,
                          sizeof(sector[sectnum].ceilingz), BZ_MAX, 1);
                break;
            case 4:
                handlemed(0, "Tile number", "Ceiling Tile Number", &sector[sectnum].ceilingpicnum,
                          sizeof(sector[sectnum].ceilingpicnum), MAXUSERTILES-1, 0);
                break;
            case 5:
                handlemed(0, "Ceiling heinum", "Ceiling Heinum", &sector[sectnum].ceilingheinum,
                          sizeof(sector[sectnum].ceilingheinum), BHEINUM_MAX, 1);
                setslope(sectnum, YAX_CEILING, sector[sectnum].ceilingheinum);
                break;
            case 6:
                handlemed(0, "Palookup number", "Ceiling Palookup Number", &sector[sectnum].ceilingpal,
                          sizeof(sector[sectnum].ceilingpal), M32_MAXPALOOKUPS, 0);
                break;
            }
        }
        else if (col == 2)
        {
            switch (row)
            {
            case 0:
#ifdef YAX_ENABLE__COMPAT
                i = sector[sectnum].ceilingstat&YAX_BIT;
#endif
                handlemed(1, "Flags (hex)", "Floor Flags", &sector[sectnum].floorstat,
                          sizeof(sector[sectnum].floorstat), 65535, 0);
#ifdef YAX_ENABLE__COMPAT
                sector[sectnum].ceilingstat &= ~YAX_BIT;
                sector[sectnum].ceilingstat |= i;
#endif
                break;

            case 1:
                i = Bsprintf(med_disptext,"(X,Y)pan: %d, %d",
                             TrackerCast(sector[sectnum].floorxpanning),
                             TrackerCast(sector[sectnum].floorypanning));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sector %d Floor X Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].floorxpanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].floorxpanning,255,0);
                    Bsprintf(med_edittext,"Sector %d Floor Y Pan: ",sectnum);
                    printmessage16("%s", med_edittext);
                    sector[sectnum].floorypanning = (char)getnumber16(med_edittext,(int32_t)sector[sectnum].floorypanning,255,0);
                }
                break;

            case 2:
                handlemed(0, "Shade byte", "Floor Shade", &sector[sectnum].floorshade,
                          sizeof(sector[sectnum].floorshade), 128, 1);
                break;
            case 3:
                handlemed(0, "Z-coordinate", "Floor Z-coordinate", &sector[sectnum].floorz,
                          sizeof(sector[sectnum].floorz), BZ_MAX, 1); //2147483647L,-2147483648L
                break;
            case 4:
                handlemed(0, "Tile number", "Floor Tile Number", &sector[sectnum].floorpicnum,
                          sizeof(sector[sectnum].floorpicnum), MAXUSERTILES-1, 0);
                break;
            case 5:
                handlemed(0, "Floor heinum", "Floor Heinum", &sector[sectnum].floorheinum,
                          sizeof(sector[sectnum].floorheinum), BHEINUM_MAX, 1);
                setslope(sectnum, YAX_FLOOR, sector[sectnum].floorheinum);
                break;
            case 6:
                handlemed(0, "Palookup number", "Floor Palookup Number", &sector[sectnum].floorpal,
                          sizeof(sector[sectnum].floorpal), M32_MAXPALOOKUPS, 0);
                break;
            }
        }

        med_printcurline(xpos, ypos, row, 1);
        if (med_editval)
            med_editval = 0;

        videoShowFrame(1);
    }

    med_printcurline(xpos, ypos, row, 0);
    // printmessage16("");

    videoShowFrame(1);
    keystatus[KEYSC_ESC] = 0;
}

static void EditWallData(int16_t wallnum)
{
    int32_t row=0;
    int32_t i = -1;
    int32_t xpos = 208, ypos = ydim-STATUS2DSIZ+48;

    med_editval = 0;

    med_dispwidth = 24;
    med_disptext[med_dispwidth] = 0;

    med_typename = "Wall";
    med_thenum = wallnum;

    drawgradient();

    showwalldata(wallnum, 0);

    while (keystatus[KEYSC_ESC] == 0)
    {
        med_handlecommon(xpos, ypos, &row, 6);

        switch (row)
        {
        case 0:
#if !defined NEW_MAP_FORMAT
            i = wall[wallnum].cstat&YAX_NEXTWALLBITS;
#endif
            handlemed(1, "Flags (hex)", "Flags", &wall[wallnum].cstat,
                      sizeof(wall[wallnum].cstat), 65535, 0);
#if !defined NEW_MAP_FORMAT
            wall[wallnum].cstat &= ~YAX_NEXTWALLBITS;
            wall[wallnum].cstat |= i;
#endif
            break;
        case 1:
            handlemed(0, "Shade", "Shade", &wall[wallnum].shade,
                      sizeof(wall[wallnum].shade), 128, 1);
            break;
        case 2:
            handlemed(0, "Pal", "Pal", &wall[wallnum].pal,
                      sizeof(wall[wallnum].pal), M32_MAXPALOOKUPS, 0);
            break;
        case 3:
            i = Bsprintf(med_disptext,"(X,Y)repeat: %d, %d",
                         TrackerCast(wall[wallnum].xrepeat),
                         TrackerCast(wall[wallnum].yrepeat));
            for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
            if (med_editval)
            {
                Bsprintf(med_edittext,"Wall %d X Repeat: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].xrepeat = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].xrepeat,255,0);
                Bsprintf(med_edittext,"Wall %d Y Repeat: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].yrepeat = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].yrepeat,255,0);
            }
            break;
        case 4:
            i = Bsprintf(med_disptext,"(X,Y)pan: %d, %d",
                         TrackerCast(wall[wallnum].xpanning),
                         TrackerCast(wall[wallnum].ypanning));
            for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
            if (med_editval)
            {
                Bsprintf(med_edittext,"Wall %d X Pan: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].xpanning = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].xpanning,255,0);
                Bsprintf(med_edittext,"Wall %d Y Pan: ",wallnum);
                printmessage16("%s", med_edittext);
                wall[wallnum].ypanning = (char)getnumber16(med_edittext,(int32_t)wall[wallnum].ypanning,255,0);
            }
            break;
        case 5:
            handlemed(0, "Tile number", "Tile number", &wall[wallnum].picnum,
                      sizeof(wall[wallnum].picnum), MAXUSERTILES-1, 0);
            break;

        case 6:
            handlemed(0, "OverTile number", "OverTile number", &wall[wallnum].overpicnum,
                      sizeof(wall[wallnum].overpicnum), MAXUSERTILES-1, 0);
            break;
        }

        med_printcurline(xpos, ypos, row, 1);
        if (med_editval)
        {
            med_editval = 0;
            //showwalldata(wallnum, 0);
            //// printmessage16("");
        }

        videoShowFrame(1);
    }

    med_printcurline(xpos, ypos, row, 0);
    // printmessage16("");

    videoShowFrame(1);
    keystatus[KEYSC_ESC] = 0;
}

static void EditSpriteData(int16_t spritenum)
{
    int32_t col=0, row=0, rowmax=4;
    int32_t xpos = 8, ypos = ydim-STATUS2DSIZ+48;

    med_editval = 0;

    med_dispwidth = 24;
    med_disptext[med_dispwidth] = 0;

    med_typename = "Sprite";
    med_thenum = spritenum;

    drawgradient();

    //    clearmidstatbar16();

    while (keystatus[KEYSC_ESC] == 0)
    {
        showspritedata(spritenum, 0);

        med_handlecommon(xpos, ypos, &row, rowmax);

        if (PRESSED_KEYSC(LEFT))
        {
            switch (col)
            {
            case 1:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 0;
                xpos = 8;
                rowmax = 4;
                med_dispwidth = 23;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            case 2:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 1;
                xpos = 208;
                rowmax = 6;
                med_dispwidth = 24;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            }
        }
        if (PRESSED_KEYSC(RIGHT))
        {
            switch (col)
            {
            case 0:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 1;
                xpos = 208;
                rowmax = 6;
                med_dispwidth = 24;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            case 1:
            {
                med_printcurline(xpos, ypos, row, 0);
                col = 2;
                xpos = 408;
                rowmax = 6;
                med_dispwidth = 26;
                med_disptext[med_dispwidth] = 0;
                if (row > rowmax) row = rowmax;
            }
            break;
            }
        }

        switch (col)
        {
        case 0:
        {
            switch (row)
            {
            case 0:
                handlemed(0, "X-coordinate", "X-coordinate", &sprite[spritenum].x,
                          sizeof(sprite[spritenum].x), 131072, 1);
                break;
            case 1:
                handlemed(0, "Y-coordinate", "Y-coordinate", &sprite[spritenum].y,
                          sizeof(sprite[spritenum].y), 131072, 1);
                break;
            case 2:
                handlemed(0, "Z-coordinate", "Z-coordinate", &sprite[spritenum].z,
                          sizeof(sprite[spritenum].z), BZ_MAX, 1); //2147483647L,-2147483648L
                break;
            case 3:
            {
                int16_t i = sprite[spritenum].sectnum;
                handlemed(0, "Sectnum", "Sectnum", &sprite[spritenum].sectnum,
                          sizeof(sprite[spritenum].sectnum), numsectors-1, 0);
                if (i != sprite[spritenum].sectnum)
                {
                    swapshort(&i, &sprite[spritenum].sectnum);
                    changespritesect(spritenum,i);
                }
                break;
            }
            case 4:
            {
                int16_t i = sprite[spritenum].statnum;
                handlemed(0, "Statnum", "Statnum", &sprite[spritenum].statnum,
                          sizeof(sprite[spritenum].statnum), MAXSTATUS-1, 0);
                if (i != sprite[spritenum].statnum)
                {
                    swapshort(&i, &sprite[spritenum].statnum);
                    changespritestat(spritenum,i);
                }
                break;
            }
            }
        }
        break;
        case 1:
        {
            switch (row)
            {
            case 0:
                handlemed(1, "Flags (hex)", "Flags", &sprite[spritenum].cstat,
                          sizeof(sprite[spritenum].cstat), 65535, 0);
                break;
            case 1:
                handlemed(0, "Shade", "Shade", &sprite[spritenum].shade,
                          sizeof(sprite[spritenum].shade), 128, 1);
                break;
            case 2:
                handlemed(0, "Pal", "Pal", &sprite[spritenum].pal,
                          sizeof(sprite[spritenum].pal), M32_MAXPALOOKUPS, 0);
                break;
            case 3:
                handlemed(0, "Blend", "Blend", &sprite[spritenum].blend,
                          sizeof(sprite[spritenum].blend), MAXBLENDTABS, 0);
                break;
            case 4:
            {
                int32_t i = Bsprintf(med_disptext,"(X,Y)repeat: %d, %d",
                             TrackerCast(sprite[spritenum].xrepeat),
                             TrackerCast(sprite[spritenum].yrepeat));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sprite %d X Repeat: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].xrepeat = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].xrepeat,255,0);
                    Bsprintf(med_edittext,"Sprite %d Y Repeat: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].yrepeat = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].yrepeat,255,0);
                }
            }
            break;
            case 5:
            {
                int32_t i = Bsprintf(med_disptext,"(X,Y)offset: %d, %d",
                             TrackerCast(sprite[spritenum].xoffset),
                             TrackerCast(sprite[spritenum].yoffset));
                for (; i < med_dispwidth; i++) med_disptext[i] = ' ';
                if (med_editval)
                {
                    Bsprintf(med_edittext,"Sprite %d X Offset: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].xoffset = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].xoffset,128,1);
                    Bsprintf(med_edittext,"Sprite %d Y Offset: ",spritenum);
                    printmessage16("%s", med_edittext);
                    sprite[spritenum].yoffset = (char)getnumber16(med_edittext,(int32_t)sprite[spritenum].yoffset,128,1);
                }
            }
            break;
            case 6:
                handlemed(0, "Tile number", "Tile number", &sprite[spritenum].picnum,
                          sizeof(sprite[spritenum].picnum), MAXUSERTILES-1, 0+2);
                break;
            }
        }
        break;
        case 2:
        {
            switch (row)
            {
            case 0:
                handlemed(0, "Angle (2048 degrees)", "Angle", &sprite[spritenum].ang,
                          sizeof(sprite[spritenum].ang), 2048, 1);
                if (med_editval)
                    sprite[spritenum].ang &= 2047;
                break;
            case 1:
                handlemed(0, "X-Velocity", "X-Velocity", &sprite[spritenum].xvel,
                          sizeof(sprite[spritenum].xvel), 65535, 1);
                break;
            case 2:
                handlemed(0, "Y-Velocity", "Y-Velocity", &sprite[spritenum].yvel,
                          sizeof(sprite[spritenum].yvel), 65535, 1);
                break;
            case 3:
                handlemed(0, "Z-Velocity", "Z-Velocity", &sprite[spritenum].zvel,
                          sizeof(sprite[spritenum].zvel), 65535, 1);
                break;
            case 4:
                handlemed(0, "Owner", "Owner", &sprite[spritenum].owner,
                          sizeof(sprite[spritenum].owner), MAXSPRITES-1, 1);
                break;
            case 5:
                handlemed(0, "Clipdist", "Clipdist", &sprite[spritenum].clipdist,
                          sizeof(sprite[spritenum].clipdist), 255, 0);
                break;
            case 6:
                handlemed(0, "Extra", "Extra", &sprite[spritenum].extra,
                          sizeof(sprite[spritenum].extra), BTAG_MAX, 1);
                break;
            }
        }
        break;
        }

        med_printcurline(xpos, ypos, row, 1);
        if (med_editval)
            med_editval = 0;

        videoShowFrame(1);
    }

    med_printcurline(xpos, ypos, row, 0);
    // printmessage16("");
    videoShowFrame(1);

    keystatus[KEYSC_ESC] = 0;
}

#define TWENTYFIVE_BLANKS "                         "

static void GenericSpriteSearch(void)
{
    char disptext[80];
    char edittext[80];
    static int32_t col=0, row=0;
    int32_t i, j, k;
    int32_t rowmax[3]= {6,6,6}, dispwidth[3] = {24,24,28};
    int32_t xpos[3] = {8,200,400}, ypos = ydim-STATUS2DSIZ+48;

    static const char *labels[7][3] =
    {
        {"X-coordinate", "Flags (hex)", "Angle (2048 degrees)"},
        {"Y-coordinate", "Shade",       "X-Velocity"},
        {"Z-coordinate", "Pal",         "Y-Velocity"},
        {"Sectnum",      "Blend",       "Z-Velocity"},
        {"Statnum",      "(X/Y)repeat", "Owner"},
        {"Hitag",        "(X/Y)offset", "Clipdist"},
        {"Lotag",        "Tile number", "Extra"}
    };

    static int32_t maxval[7][3] =
    {
        { BXY_MAX     , 65535         , 2048 },
        { BXY_MAX     , 128           , 65535 },
        { BZ_MAX      , M32_MAXPALOOKUPS, 65535 },
        { MAXSECTORS-1, MAXBLENDTABS-1, 65535 },
        { MAXSTATUS-1 , 128           , MAXSPRITES-1 },
        { BTAG_MAX    , 128           , 256 },
        { BTAG_MAX    , MAXUSERTILES-1    , BTAG_MAX }
    };

    static char sign[7][3] =
    {
        {1,   0,   1},
        {1,   1,   1},
        {1,   0,   1},
        {0,   0,   1},
        {0,   0,   0},
        {0+4, 1,   0},
        {0+4, 0+2, 1}
    };

    clearmidstatbar16();

    drawgradient();

    printext16(xpos[0], ypos-2*8, editorcolors[10], editorcolors[0], "Sprite search", 0);

    for (i=0; i<3; i++)
        for (j=0; j<=rowmax[i]; j++)
        {
            if (gs_spriteTagInterested[i][j])
                k=Bsprintf(disptext, "%s: %d", labels[j][i], gs_spriteTagValue[i][j]);
            else
                k=Bsprintf(disptext, "%s: ^7any", labels[j][i]);
//            for (; k<dispwidth[i]; k++) disptext[k] = 0;

            printext16(xpos[i], ypos+j*8, editorcolors[11], editorcolors[0], disptext, 0);
        }
    Bmemset(disptext, 0, sizeof(disptext));

    //    disptext[dispwidth[col]] = 0;
    //    showspritedata(spritenum, 0);
    wallsprite = 2;

    while (keystatus[KEYSC_ESC] == 0)
    {
        idle_waitevent();
        if (handleevents())
        {
            if (quitevent) quitevent = 0;
        }

        printmessage16("Sprite search, press <Esc> to exit");

        if (PRESSED_KEYSC(DOWN))
        {
            if (row < rowmax[col])
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                row++;
            }
        }
        if (PRESSED_KEYSC(UP))
        {
            if (row > 0)
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                row--;
            }
        }
        if (PRESSED_KEYSC(LEFT))
        {
            if (col > 0)
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                col--;
                disptext[dispwidth[col]] = 0;
                if (row > rowmax[col]) row = rowmax[col];
            }
        }
        if (PRESSED_KEYSC(RIGHT))
        {
            if (col < 2)
            {
                printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
                col++;
                disptext[dispwidth[col]] = 0;
                if (row > rowmax[col]) row = rowmax[col];
            }
        }
        if (PRESSED_KEYSC(ENTER))
        {
            Bsprintf(edittext, "%s: ", labels[row][col]);
            printmessage16("%s", edittext);
            i = getnumber16(edittext, gs_spriteTagInterested[col][row] ? gs_spriteTagValue[col][row] : 0,
                            maxval[row][col], sign[row][col]);
            if (col == 2 && row == 0) i = (i+2048)&2047;  // angle
            gs_spriteTagValue[col][row] = i;
            gs_spriteTagInterested[col][row] = 1;

            if (col == 1 && row == 6)  // picnum
            {
                printext16(xpos[1], ypos-2*8, editorcolors[14], editorcolors[0], TWENTYFIVE_BLANKS, 0);
                if (names[i][0])
                    printext16(xpos[1], ypos-2*8, editorcolors[14], editorcolors[0], names[i], 0);
            }
        }
        if (PRESSED_KEYSC(BS) || PRESSED_KEYSC(DELETE))
        {
            gs_spriteTagInterested[col][row] = 0;

            if (col == 1 && row == 6)  // picnum
                printext16(xpos[1], ypos-2*8, editorcolors[14], editorcolors[0], TWENTYFIVE_BLANKS, 0);
        }

        i = gs_spriteTagInterested[col][row];
        if (i)
        {
            if (col == 1 && row == 0)  // flags
                k = Bsprintf(disptext, "%s: %x", labels[row][col], gs_spriteTagValue[col][row]);
            else
                k = Bsprintf(disptext, "%s: %d", labels[row][col], gs_spriteTagValue[col][row]);
        }
        else
            k = Bsprintf(disptext, "%s: ^7any", labels[row][col]);
        //                      v-------^^
        for (; k<dispwidth[col]-2*i; k++) disptext[k] = ' ';
        disptext[k] = 0;

        printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[1],disptext,0);

        videoShowFrame(1);
    }

    printext16(xpos[col],ypos+row*8,editorcolors[11],editorcolors[0],disptext,0);
    printmessage16("Search sprite");
    videoShowFrame(1);

    keystatus[KEYSC_ESC] = 0;
}
