//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers

This file is part of EDuke32

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------

#ifndef _duke3d_h_
# define _duke3d_h_

#ifndef APPNAME
#define APPNAME "EDuke32-OldMP"
#endif
#ifndef APPBASENAME
#define APPBASENAME "eduke32-oldmp"
#endif
#define VERSION "Release 34"

#define HEAD2 APPNAME " " VERSION

#define MOVEFIFOSIZ 256

#define OSDTEXT_DEFAULT   "^00"
#define OSDTEXT_BLUE      "^01"
#define OSDTEXT_DARKRED   "^10"
#define OSDTEXT_GREEN     "^11"
#define OSDTEXT_RED       "^21"
#define OSDTEXT_YELLOW    "^23"

#define OSDTEXT_BRIGHT    "^S0"

#define OSD_ERROR OSDTEXT_DARKRED OSDTEXT_BRIGHT

#define HORIZ_MIN -99
#define HORIZ_MAX 299

#define GAMEDUKE 0
#define GAMENAM 1
#define GAMEWW2 3

#define VOLUMEALL (g_Shareware==0)
#define PLUTOPAK (g_scriptVersion==14)
#define VOLUMEONE (g_Shareware==1)
#define NAM (g_gameType&1)
#define WW2GI (g_gameType&2)

#define MAXSLEEPDIST  16384
#define SLEEPTIME 24*64

#define BYTEVERSION_13  27
#define BYTEVERSION_14  116
#define BYTEVERSION_15  117
#define BYTEVERSION_JF  189 // increase by 3, because atomic GRP adds 1, and Shareware adds 2

#define BYTEVERSION (BYTEVERSION_JF+(PLUTOPAK?1:(VOLUMEONE<<1)))    // JBF 20040116: different data files give different versions

#define NUMPAGES 1

#define AUTO_AIM_ANGLE          48
#define RECSYNCBUFSIZ 2520   //2520 is the (LCM of 1-8)*3

#define ACTOR_FLOOR_OFFSET  (1<<8)
#define ZOFFSET2            (16<<8)
#define ZOFFSET3            (8<<8)
#define ZOFFSET4            (12<<8)
#define ZOFFSET5            (32<<8)
#define ZOFFSET6            (4<<8)

#define CAMERA(Membname) (ud.camera ## Membname)

#define MAXVOLUMES 14
#define MAXLEVELS 64
#define MAXGAMETYPES 16
#define MAXANIMWALLS 512
#define MAXANIMATES 1024
#define MAXCYCLERS 1024
#define MAXQUOTES 16384
#define FIRST_OBITUARY_QUOTE MAXQUOTES-128
#define FIRST_SUICIDE_QUOTE MAXQUOTES-32

// sector effector lotags
enum
{
    SE_0_ROTATING_SECTOR = 0,
    SE_1_PIVOT = 1,
    SE_2_EARTHQUAKE = 2,
    SE_3_RANDOM_LIGHTS_AFTER_SHOT_OUT = 3,
    SE_4_RANDOM_LIGHTS = 4,
    SE_5 = 5,
    SE_6_SUBWAY = 6,

    SE_7_TELEPORT = 7,
    SE_8_UP_OPEN_DOOR_LIGHTS = 8,
    SE_9_DOWN_OPEN_DOOR_LIGHTS = 9,
    SE_10_DOOR_AUTO_CLOSE = 10,
    SE_11_SWINGING_DOOR = 11,
    SE_12_LIGHT_SWITCH = 12,
    SE_13_EXPLOSIVE = 13,
    SE_14_SUBWAY_CAR = 14,
    SE_15_SLIDING_DOOR = 15,
    SE_16_REACTOR = 16,
    SE_17_WARP_ELEVATOR = 17,
    SE_18_INCREMENTAL_SECTOR_RISE_FALL = 18,
    SE_19_EXPLOSION_LOWERS_CEILING = 19,
    SE_20_STRETCH_BRIDGE = 20,
    SE_21_DROP_FLOOR = 21,
    SE_22_TEETH_DOOR = 22,
    SE_23_ONE_WAY_TELEPORT = 23,
    SE_24_CONVEYOR = 24,
    SE_25_PISTON = 25,
    SE_26 = 26,
    SE_27_DEMO_CAM = 27,
    SE_28_LIGHTNING = 28,
    SE_29_WAVES = 29,
    SE_30_TWO_WAY_TRAIN = 30,
    SE_31_FLOOR_RISE_FALL = 31,
    SE_32_CEILING_RISE_FALL = 32,
    SE_33_QUAKE_DEBRIS = 33,
    SE_34 = 34,  // XXX
    SE_35 = 35,  // XXX
    SE_36_PROJ_SHOOTER = 36,
    SE_40_ROR_UPPER = 40,
    SE_41_ROR_LOWER = 41,
    SE_46_ROR_PROTECTED = 46,
    SE_49_POINT_LIGHT = 49,
    SE_50_SPOT_LIGHT = 50,
    SE_130 = 130,
    SE_131 = 131,
};

// sector lotags
enum
{
    ST_0_NO_EFFECT = 0,
    ST_1_ABOVE_WATER = 1,
    ST_2_UNDERWATER = 2,
    ST_3 = 3,
    // ^^^ maybe not complete substitution in code
    ST_9_SLIDING_ST_DOOR = 9,
    ST_15_WARP_ELEVATOR = 15,
    ST_16_PLATFORM_DOWN = 16,
    ST_17_PLATFORM_UP = 17,
    ST_18_ELEVATOR_DOWN = 18,
    ST_19_ELEVATOR_UP = 19,
    ST_20_CEILING_DOOR = 20,
    ST_21_FLOOR_DOOR = 21,
    ST_22_SPLITTING_DOOR = 22,
    ST_23_SWINGING_DOOR = 23,
    ST_25_SLIDING_DOOR = 25,
    ST_26_SPLITTING_ST_DOOR = 26,
    ST_27_STRETCH_BRIDGE = 27,
    ST_28_DROP_FLOOR = 28,
    ST_29_TEETH_DOOR = 29,
    ST_30_ROTATE_RISE_BRIDGE = 30,
    ST_31_TWO_WAY_TRAIN = 31,
    // left: ST 32767, 65534, 65535
};

#include "compat.h"
#include "a.h"
#include "build.h"
#include "cache1d.h"
#include "pragmas.h"

#ifdef RANCID_NETWORKING
#include "mmulti_unstable.h"
#else
#include "mmulti.h"
#endif

#include "sync.h"

#include "baselayer.h"
#include "function.h"
#include "macros.h"
//#include "types.h"
//#include "file_lib.h"
#include "gamedefs.h"
#include "keyboard.h"
//#include "util_lib.h"
//#include "mathutil.h"
#include "common.h"
#include "function.h"
#include "fx_man.h"
#include "config.h"
#include "control.h"
#include "_rts.h"
#include "rts.h"
#include "namesdyn.h"
#include "sounds.h"
#include "soundefs.h"
#include "music.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_scriptVersion, g_Shareware, g_gameType;

////////// TIMING CONSTANTS //////////
// The number of 'totalclock' increments per second:
#define TICRATE             120
// The number of game state updates per second:
#define REALGAMETICSPERSEC  30
// The number of 'totalclock' increments per game state update:
// NOTE: calling a game state update a 'frame' is really weird.
// (This used to be TICRATE/GAMETICSPERSEC, which was 120/26 = 4.615~ truncated
// to 4 by integer division.)
#define TICSPERFRAME        (TICRATE/REALGAMETICSPERSEC)
// Used as a constant to satisfy all of the calculations written with ticrate =
// 26 in mind:
#define GAMETICSPERSEC      26

// #define GC (TICSPERFRAME*44)

#define STAT_DEFAULT        0
#define STAT_ACTOR          1
#define STAT_ZOMBIEACTOR    2
#define STAT_EFFECTOR       3
#define STAT_PROJECTILE     4
#define STAT_MISC           5
#define STAT_STANDABLE      6
#define STAT_LOCATOR        7
#define STAT_ACTIVATOR      8
#define STAT_TRANSPORT      9
#define STAT_PLAYER         10
#define STAT_FX             11
#define STAT_FALLER         12
#define STAT_DUMMYPLAYER    13

#define    ALT_IS_PRESSED ( KB_KeyPressed( sc_RightAlt ) || KB_KeyPressed( sc_LeftAlt ) )
#define    SHIFTS_IS_PRESSED ( KB_KeyPressed( sc_RightShift ) || KB_KeyPressed( sc_LeftShift ) )
#define    RANDOMSCRAP(s, i) A_InsertSprite(s->sectnum,s->x+(krand()&255)-128,s->y+(krand()&255)-128,s->z-(8<<8)-(krand()&8191),SCRAP6+(krand()&15),-8,48,48,krand()&2047,(krand()&63)+64,-512-(krand()&2047),i,5)

#define    PHEIGHT (38<<8)

enum GameMode_t {
    MODE_MENU       = 1,
    MODE_DEMO       = 2,
    MODE_GAME       = 4,
    MODE_EOL        = 8,
    MODE_TYPE       = 16,
    MODE_RESTART    = 32,
    MODE_SENDTOWHOM = 64,
    MODE_END        = 128
};

// Defines the motion characteristics of an actor

enum ActorMoveFlags_t {
    face_player         = 1,
    geth                = 2,
    getv                = 4,
    random_angle        = 8,
    face_player_slow    = 16,
    spin                = 32,
    face_player_smart   = 64,
    fleeenemy           = 128,
    jumptoplayer        = 257,
    seekplayer          = 512,
    furthestdir         = 1024,
    dodgebullet         = 4096
};

// Some misc #defines
#define NO       0
#define YES      1

// Defines for 'useractor' keyword

enum UserActorTypes_t {
    notenemy,
    enemy,
    enemystayput
};

// Player Actions.

enum PlayerActionFlags_t {
    pstanding       = 1,
    pwalking        = 2,
    prunning        = 4,
    pducking        = 8,
    pfalling        = 16,
    pjumping        = 32,
    phigher         = 64,
    pwalkingback    = 128,
    prunningback    = 256,
    pkicking        = 512,
    pshrunk         = 1024,
    pjetpack        = 2048,
    ponsteroids     = 4096,
    ponground       = 8192,
    palive          = 16384,
    pdead           = 32768,
    pfacing         = 65536
};

enum InventoryItem_t {
    GET_STEROIDS,
    GET_SHIELD,
    GET_SCUBA,
    GET_HOLODUKE,
    GET_JETPACK,
    GET_ACCESS   = 6,
    GET_HEATS,
    GET_FIRSTAID = 9,
    GET_BOOTS
};

enum DukeWeapon_t {
    KNEE_WEAPON,
    PISTOL_WEAPON,
    SHOTGUN_WEAPON,
    CHAINGUN_WEAPON,
    RPG_WEAPON,
    HANDBOMB_WEAPON,
    SHRINKER_WEAPON,
    DEVISTATOR_WEAPON,
    TRIPBOMB_WEAPON,
    FREEZE_WEAPON,
    HANDREMOTE_WEAPON,
    GROW_WEAPON,
    MAX_WEAPONS
};

#define deletesprite A_DeleteSprite
void A_DeleteSprite(int s);

typedef struct {
    uint32_t bits;
    short fvel, svel;
    fix16_t q16avel, q16horz;
    unsigned char extbits;
} input_t;

extern input_t recsync[RECSYNCBUFSIZ];

typedef struct {
    int wallnum, tag;
} animwalltype;

extern int probey;

struct savehead {
    char name[19];
    int numplr,volnum,levnum,plrskl;
    char boardfn[BMAX_PATH];
};

typedef struct {
    int UseJoystick;
    int UseMouse;
    int AutoAim;
    int MouseBias;
    //int SmoothInput;

    // JBF 20031211: Store the input settings because
    // (currently) jmact can't regurgitate them
    int MouseFunctions[MAXMOUSEBUTTONS][2];
    int MouseDigitalFunctions[MAXMOUSEAXES][2];
    int MouseAnalogueAxes[MAXMOUSEAXES];
    int JoystickFunctions[MAXJOYBUTTONSANDHATS][2];
    int JoystickDigitalFunctions[MAXJOYAXES][2];
    int JoystickAnalogueAxes[MAXJOYAXES];
    int JoystickAnalogueScale[MAXJOYAXES];
    int JoystickAnalogueInvert[MAXJOYAXES];
    int JoystickAnalogueDead[MAXJOYAXES];
    int JoystickAnalogueSaturate[MAXJOYAXES];
    uint8_t KeyboardKeys[NUMGAMEFUNCTIONS][2];

    //
    // Sound variables
    //
    int FXDevice;
    int MusicDevice;
    int FXVolume;
    int MusicVolume;
    int SoundToggle;
    int MusicToggle;
    int VoiceToggle;
    int AmbienceToggle;

    int NumVoices;
    int NumChannels;
    int NumBits;
    int MixRate;
    
    //int ReverseStereo;

    //
    // Screen variables
    //

    int ScreenMode;

    int ScreenWidth;
    int ScreenHeight;
    int ScreenBPP;

    int ForceSetup;

    int scripthandle;
    int setupread;

    // Misc
    //int CheckForUpdates;
    int LastUpdateCheck;
    int useprecache;
    int PredictionDebug;
} config_t;

#define MAX_RETURN_VALUES 6

typedef struct {
    vec3_t camerapos;
    int const_visibility,uw_framerate;
    int camera_time,folfvel,folavel,folx,foly,fola;
    int reccnt,crosshairscale;

    int runkey_mode,statusbarscale,mouseaiming,weaponswitch,drawweapon;   // JBF 20031125
    int democams,color,m_color,msgdisptime,statusbarmode;
    int m_noexits,noexits,autovote,automsg,idplayers, showweapons;
    int team, viewbob, weaponsway, althud, weaponscale, textscale;

    int entered_name,screen_tilting,shadows,fta_on,executions,auto_run;
    int coords,tickrate,levelstats,m_coop,coop,screen_size,lockout,crosshair;
    int playerai,angleinterpolation,obituaries;

    int respawn_monsters,respawn_items,respawn_inventory,recstat,monsters_off,brightness;
    int m_respawn_items,m_respawn_monsters,m_respawn_inventory,m_recstat,m_monsters_off,detail;
    int m_ffire,ffire,m_player_skill,m_level_number,m_volume_number,multimode;
    int player_skill,level_number,volume_number,m_marker,marker,mouseflip;
    int m_weaponstay;

    int configversion;
    
    int fraglimit;

    int got_access;

    int playerid_time;
    char playerid_num;

    short camerasect;
    fix16_t cameraq16ang, cameraq16horiz;
    short pause_on,from_bonus;
    short camerasprite,last_camsprite;
    short last_level,secretlevel;

    char overhead_on,last_overhead;
    char god,warp_on,cashman,eog,showallmap;
    char show_help,scrollmode,clipping;
    char ridecule[10][40];
    char savegame[10][22];
    char pwlockout[128],rtsname[128];
    char display_bonus_screen;
    char show_level_text;
    char wchoice[MAX_WEAPONS];
    char limit_hit;
    char allow_visibility;

    int32_t returnvar[MAX_RETURN_VALUES - 1];

    config_t config;
} user_defs;

typedef struct {
    int ox,oy,oz;
    short oa,os;
} PlayerSpawn_t;

extern char g_numPlayerSprites;

typedef struct {
    char name[32];
    uint8_t palnum;
    int16_t frags;
    int16_t suicides;
    int8_t blocked;
} Team_t;
extern Team_t teams[MAXPLAYERS];
extern int numTeams;

extern uint8_t numPlayerColors;
extern uint8_t allowedColors[(MAXPALOOKUPS + CHAR_BIT - 1) / CHAR_BIT];
extern char pColorNames[MAXPALOOKUPS][32];

#define TEST_ALLOWEDCOLOR(pal) (TEST(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))
#define RESET_ALLOWEDCOLOR(pal) (RESET(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))
#define SET_ALLOWEDCOLOR(pal) (SET(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))

static inline uint8_t playerColor_getValidPal(int palnum) { return TEST_ALLOWEDCOLOR(palnum) ? palnum : 0; }

static inline int32_t calc_smoothratio(int32_t totalclk, int32_t ototalclk)
{
    return clamp((totalclk - ototalclk) * (65536 / TICSPERFRAME), 0, 65536);
}

extern int fricxv,fricyv;

// TODO: make a dummy player struct for interpolation and sort the first members
// of this struct into the same order so we can just memcpy it and get rid of the
// mywhatever type globals

typedef struct {
    vec3_t pos, opos, vel;

    int zoom, exitx, exity;
    int invdisptime;
    int bobposx, bobposy, pyoff, opyoff;
    int last_pissed_time, truefz, truecz;
    int player_par, visibility;
    int bobcounter, weapon_sway;
    int pals_time, randomflamex, crack_time;

    fix16_t q16horiz, q16horizoff, oq16horiz, oq16horizoff;
    fix16_t q16ang, oq16ang, q16angvel;

    unsigned int interface_toggle_flag;

    int max_secret_rooms, secret_rooms, max_actors_killed, actors_killed;
    int runspeed, movement_lock, team;
    int max_player_health, max_shield_amount, max_ammo_amount[MAX_WEAPONS];

    int scream_voice;

    int loogiex[64], loogiey[64], numloogs, loogcnt;

    char palette;

    short sbs, sound_pitch;

    short cursectnum, look_ang, last_extra, subweapon;
    short ammo_amount[MAX_WEAPONS], wackedbyactor, frag, fraggedself;

    short curr_weapon, last_weapon, last_used_weapon, tipincs, wantweaponfire;
    short holoduke_amount, newowner, hurt_delay, hbomb_hold_delay;
    short jumping_counter, airleft, knee_incs, access_incs;
    short fta, ftq, access_wallnum, access_spritenum;
    short kickback_pic, weapon_ang, firstaid_amount;
    short somethingonplayer, on_crane, i, one_parallax_sectnum;
    short over_shoulder_on, random_club_frame, fist_incs;
    short one_eighty_count, cheat_phase;
    short dummyplayersprite, extra_extra8, quick_kick, last_quick_kick;
    short heat_amount, actorsqu, timebeforeexit, customexitsound;

    short weaprecs[16], weapreccnt;


    short orotscrnang, rotscrnang, dead_flag, show_empty_weapon;   // JBF 20031220: added orotscrnang
    short scuba_amount, jetpack_amount, steroids_amount, shield_amount;
    short holoduke_on, pycount, weapon_pos, frag_ps;
    short transporter_hold, last_full_weapon, footprintshade, boot_amount;

    char aim_mode, auto_aim, weaponswitch;

    char gm, on_warping_sector, footprintcount;
    char hbomb_on, jumping_toggle, rapid_fire_hold, on_ground;
    char inven_icon, buttonpalette;

    char jetpack_on, spritebridge, lastrandomspot;
    char scuba_on, footprintpal, heat_on;

    char  holster_weapon, falling_counter;
    char  gotweapon[MAX_WEAPONS], refresh_inventory;

    char toggle_key_flag, knuckle_incs; // , select_dir;
    char walking_snd_toggle, palookup, hard_landing;
    char /*fire_flag, */pals[3];
    char return_to_center, reloading, name[32];

    int8_t padding_[1];
} DukePlayer_t;

#define MAXQUOTELEN 128
#define MAXUSERQUOTES 6
#define USERQUOTE_LEFTOFFSET 5
#define USERQUOTE_RIGHTOFFSET 14
extern int quotebot, quotebotgoal; // game.c

void X_ScriptInfo(void);
extern intptr_t *actorLoadEventScrptr[MAXTILES];

typedef struct {
    int workslike, extra, cstat, extra_rand, hitradius, range;
    short spawns, sound, isound, vel, decal, trail, tnum, drop, clipdist, offset, bounces, bsound, toffset;
    signed char sxrepeat, syrepeat, txrepeat, tyrepeat, shade, xrepeat, yrepeat, pal, velmult;
} projectile_t;

typedef struct {
/*    int x;
    int y;
    int z; */
    short ang, oldang, angdir, angdif;
} spriteinterpolate;

typedef struct {
    int floorz,ceilingz,lastvx,lastvy;
    vec3_t bpos;
    int flags;
    intptr_t temp_data[10]; // sometimes used to hold pointers to con code
    short picnum,ang,extra,owner,movflag;
    short tempang,actorstayput,dispicnum;
    short timetosleep;
    char cgg;
    char filler;
    projectile_t projectile;
} ActorData_t;

extern int tempwallptr;
extern intptr_t frameplace;

extern int g_screenCapture;


enum GametypeFlags_t {
    GAMETYPE_COOP                   = 1,
    GAMETYPE_WEAPSTAY               = 2,
    GAMETYPE_FRAGBAR                = 4,
    GAMETYPE_SCORESHEET             = 8,
    GAMETYPE_DMSWITCHES             = 16,
    GAMETYPE_COOPSPAWN              = 32,
    GAMETYPE_ACCESSCARDSPRITES      = 64,
    GAMETYPE_COOPVIEW               = 128,
    GAMETYPE_COOPSOUND              = 256,
    GAMETYPE_OTHERPLAYERSINMAP      = 512,
    GAMETYPE_ALLOWITEMRESPAWN       = 1024,
    GAMETYPE_MARKEROPTION           = 2048,
    GAMETYPE_PLAYERSFRIENDLY        = 4096,
    GAMETYPE_FIXEDRESPAWN           = 8192,
    GAMETYPE_ACCESSATSTART          = 16384,
    GAMETYPE_PRESERVEINVENTORYDEATH = 32768,
    GAMETYPE_TDM                    = 65536,
    GAMETYPE_TDMSPAWN               = 131072
};

#define GTFLAGS(x) (GametypeFlags[ud.coop] & x)
#define FFIRE_CHECK(x,y) (!ud.ffire && (GTFLAGS(GAMETYPE_PLAYERSFRIENDLY) || (GTFLAGS(GAMETYPE_TDM) && (g_player[x].ps->team == g_player[y].ps->team))))
#define HEAD_ABOVE_TROR_WATER(x) ((yax_getbunch(g_player[x].ps->cursectnum, YAX_FLOOR) != -1) && (sector[g_player[x].ps->cursectnum].lotag & 0x7ff) == 1)

extern int g_lastSaveSlot;
extern int g_restorePalette;

typedef enum basepal_ {
    BASEPAL = 0,
    WATERPAL,
    SLIMEPAL,
    DREALMSPAL,
    TITLEPAL,
    ENDINGPAL,  // 5
    ANIMPAL,
    BASEPALCOUNT
} basepal_t;

extern int cachecount;
extern char boardfilename[BMAX_PATH];
extern char currentboardfilename[BMAX_PATH];

//DUKE3D.H:
typedef struct {
    short got_access, last_extra, shield_amount, curr_weapon, holoduke_on;
    short firstaid_amount, steroids_amount, holoduke_amount, jetpack_amount;
    short heat_amount, scuba_amount, boot_amount;
    short last_weapon, weapon_pos, kickback_pic;
    short ammo_amount[MAX_WEAPONS], frag[MAXPLAYERS];
    char inven_icon, jetpack_on, heat_on, gotweapon[MAX_WEAPONS];
} STATUSBARTYPE;

extern int g_cameraDistance, g_cameraClock;

extern int stereomode, stereowidth, stereopixelwidth;

extern int g_myAimMode, g_myAimStat, g_oldAimStat;

#define IFISGLMODE if (videoGetRenderMode() >= 3)
#define IFISSOFTMODE if (videoGetRenderMode() < 3)

#define TILE_SAVESHOT (MAXTILES-1)
#define TILE_LOADSHOT (MAXTILES-3)
#define TILE_TILT     (MAXTILES-2)
#define TILE_ANIM     (MAXTILES-4)
#define TILE_VIEWSCR  (MAXTILES-5)

enum GameEvent_t {
    EVENT_INIT,
    EVENT_ENTERLEVEL,
    EVENT_RESETWEAPONS,
    EVENT_RESETINVENTORY,
    EVENT_HOLSTER,
    EVENT_LOOKLEFT,
    EVENT_LOOKRIGHT,
    EVENT_SOARUP,
    EVENT_SOARDOWN,
    EVENT_CROUCH,
    EVENT_JUMP,
    EVENT_RETURNTOCENTER,
    EVENT_LOOKUP,
    EVENT_LOOKDOWN,
    EVENT_AIMUP,
    EVENT_FIRE,
    EVENT_CHANGEWEAPON,
    EVENT_GETSHOTRANGE,
    EVENT_GETAUTOAIMANGLE,
    EVENT_GETLOADTILE,
    EVENT_CHEATGETSTEROIDS,
    EVENT_CHEATGETHEAT,
    EVENT_CHEATGETBOOT,
    EVENT_CHEATGETSHIELD,
    EVENT_CHEATGETSCUBA,
    EVENT_CHEATGETHOLODUKE,
    EVENT_CHEATGETJETPACK,
    EVENT_CHEATGETFIRSTAID,
    EVENT_QUICKKICK,
    EVENT_INVENTORY,
    EVENT_USENIGHTVISION,
    EVENT_USESTEROIDS,
    EVENT_INVENTORYLEFT,
    EVENT_INVENTORYRIGHT,
    EVENT_HOLODUKEON,
    EVENT_HOLODUKEOFF,
    EVENT_USEMEDKIT,
    EVENT_USEJETPACK,
    EVENT_TURNAROUND,
    EVENT_DISPLAYWEAPON,
    EVENT_FIREWEAPON,
    EVENT_SELECTWEAPON,
    EVENT_MOVEFORWARD,
    EVENT_MOVEBACKWARD,
    EVENT_TURNLEFT,
    EVENT_TURNRIGHT,
    EVENT_STRAFELEFT,
    EVENT_STRAFERIGHT,
    EVENT_WEAPKEY1,
    EVENT_WEAPKEY2,
    EVENT_WEAPKEY3,
    EVENT_WEAPKEY4,
    EVENT_WEAPKEY5,
    EVENT_WEAPKEY6,
    EVENT_WEAPKEY7,
    EVENT_WEAPKEY8,
    EVENT_WEAPKEY9,
    EVENT_WEAPKEY10,
    EVENT_DRAWWEAPON,
    EVENT_DISPLAYCROSSHAIR,
    EVENT_DISPLAYREST,
    EVENT_DISPLAYSBAR,
    EVENT_RESETPLAYER,
    EVENT_INCURDAMAGE,
    EVENT_AIMDOWN,
    EVENT_GAME,
    EVENT_PREVIOUSWEAPON,
    EVENT_NEXTWEAPON,
    EVENT_SWIMUP,
    EVENT_SWIMDOWN,
    EVENT_GETMENUTILE,
    EVENT_SPAWN,
    EVENT_LOGO,
    EVENT_EGS,
    EVENT_DOFIRE,
    EVENT_PRESSEDFIRE,
    EVENT_USE,
    EVENT_PROCESSINPUT,
    EVENT_FAKEDOMOVETHINGS,
    EVENT_DISPLAYSTART,
    EVENT_DISPLAYROOMS,
    EVENT_KILLIT,
    EVENT_LOADACTOR,
    EVENT_DISPLAYBONUSSCREEN,
    EVENT_DISPLAYMENU,
    EVENT_DISPLAYMENUREST,
    EVENT_DISPLAYLOADINGSCREEN,
    EVENT_DISPLAYTIP,
    EVENT_ANIMATESPRITES,
    EVENT_NEWGAME,
    EVENT_PREGAME,
    EVENT_SOUND,
    EVENT_PRELEVEL,
    EVENT_DAMAGESPRITE,
    EVENT_POSTDAMAGESPRITE,
    EVENT_ALTWEAPON,
    EVENT_LASTWEAPON,
    EVENT_ALTFIRE,
    MAXEVENTS
};


#define CON_ERRPRINTF(Text, ...) do { \
        OSD_Printf("Line %d, %s: " Text, g_errorLineNum, keyw[g_tw], ## __VA_ARGS__); \
    } while (0)

enum SystemString_t {
    STR_MAPNAME,
    STR_MAPFILENAME,
    STR_PLAYERNAME,
    STR_VERSION,
    STR_GAMETYPE
};

// store global game definitions



enum GamevarFlags_t {
    MAXGAMEVARS        = 2048,       // must be a power of two
    MAXVARLABEL        = 26,
    GAMEVAR_PERPLAYER  = 0x00000001, // per-player variable
    GAMEVAR_PERACTOR   = 0x00000002, // per-actor variable
    GAMEVAR_USER_MASK  = (0x00000001|0x00000002),
    GAMEVAR_RESET      = 0x00000008, // marks var for to default
    GAMEVAR_DEFAULT    = 0x00000100, // allow override
    GAMEVAR_SECRET     = 0x00000200, // don't dump...
    GAMEVAR_NODEFAULT  = 0x00000400, // don't reset on actor spawn
    GAMEVAR_SYSTEM     = 0x00000800, // cannot change mode flags...(only default value)
    GAMEVAR_READONLY   = 0x00001000, // values are read-only (no setvar allowed)
    GAMEVAR_INTPTR     = 0x00002000, // plValues is a pointer to an int
    GAMEVAR_SYNCCHECK  = 0x00004000, // throw warnings during compile if used in local event
    GAMEVAR_SHORTPTR   = 0x00008000, // plValues is a pointer to a short
    GAMEVAR_CHARPTR    = 0x00010000, // plValues is a pointer to a char
    GAMEVAR_NORESET    = 0x00020000, // var values are not reset when restoring map state
    GAMEVAR_SPECIAL    = 0x00040000, // flag for structure member shortcut vars
    GAMEVAR_Q16PTR     = 0x00100000,

    GAMEVAR_RAWQ16PTR = GAMEVAR_Q16PTR | GAMEVAR_SPECIAL,
    GAMEVAR_PTR_MASK = GAMEVAR_INTPTR | GAMEVAR_SHORTPTR | GAMEVAR_CHARPTR | GAMEVAR_Q16PTR | GAMEVAR_RAWQ16PTR,
};

enum GamearrayFlags_t {
    MAXGAMEARRAYS      = (MAXGAMEVARS>>2), // must be lower than MAXGAMEVARS
    MAXARRAYLABEL      = MAXVARLABEL,
    GAMEARRAY_NORMAL   = 0,
    GAMEARRAY_NORESET  = 0x00000001,
};

typedef struct {
    union {
        intptr_t lValue;
        intptr_t *plValues;     // array of values when 'per-player', or 'per-actor'
    } val;
    intptr_t lDefault;
    unsigned int dwFlags;
    char *szLabel;
} gamevar_t;

typedef struct {
    char *szLabel;
    intptr_t *plValues;     // array of values
    int size;
    char bReset;
} gamearray_t;

extern gamevar_t aGameVars[MAXGAMEVARS];
extern gamearray_t aGameArrays[MAXGAMEARRAYS];
extern int g_gameVarCount;
extern int g_gameArrayCount;

extern int SpriteFlags[MAXTILES];

enum SpriteFlags_t {
    SPRITE_SHADOW       = 0x00000001,
    SPRITE_NVG          = 0x00000002,
    SPRITE_NOSHADE      = 0x00000004,
    SPRITE_PROJECTILE   = 0x00000008,
    SPRITE_DECAL        = 0x00000010,
    SPRITE_BADGUY       = 0x00000020,
    SPRITE_NOPAL        = 0x00000040,
    SPRITE_NOEVENTCODE  = 0x00000080,
    SPRITE_SMOOTHMOVE   = 0x00002000,
    SPRITE_DAMAGEEVENT  = 0x04000000,
};

extern short SpriteCacheList[MAXTILES][3];

extern int g_iReturnVarID;
extern int g_iWeaponVarID;
extern int g_iWorksLikeVarID;
extern int g_iZRangeVarID;
extern int g_iAngRangeVarID;
extern int g_iAimAngleVarID;
extern int g_iLoTagID;          // var ID of "LOTAG"
extern int g_iHiTagID;          // ver ID of "HITAG"
extern int g_iTextureID;        // ver ID of "TEXTURE"

extern char g_bEnhanced;    // are we 'enhanced' (more minerals, etc)

extern char g_szBuf[1024];

#define NAM_GRENADE_LIFETIME       120
#define NAM_GRENADE_LIFETIME_VAR    30

#define PWEAPON(Player, Weapon, Wmember) (aplWeapon ## Wmember [Weapon][Player])
extern intptr_t *aplWeaponClip[MAX_WEAPONS];            // number of items in clip
extern intptr_t *aplWeaponReload[MAX_WEAPONS];          // delay to reload (include fire)
extern intptr_t *aplWeaponFireDelay[MAX_WEAPONS];       // delay to fire
extern intptr_t *aplWeaponHoldDelay[MAX_WEAPONS];       // delay after release fire button to fire (0 for none)
extern intptr_t *aplWeaponTotalTime[MAX_WEAPONS];       // The total time the weapon is cycling before next fire.
extern intptr_t *aplWeaponFlags[MAX_WEAPONS];           // Flags for weapon
extern intptr_t *aplWeaponShoots[MAX_WEAPONS];          // what the weapon shoots
extern intptr_t *aplWeaponSpawnTime[MAX_WEAPONS];       // the frame at which to spawn an item
extern intptr_t *aplWeaponSpawn[MAX_WEAPONS];           // the item to spawn
extern intptr_t *aplWeaponShotsPerBurst[MAX_WEAPONS];   // number of shots per 'burst' (one ammo per 'burst'
extern intptr_t *aplWeaponWorksLike[MAX_WEAPONS];       // What original the weapon works like
extern intptr_t *aplWeaponInitialSound[MAX_WEAPONS];    // Sound made when initialy firing. zero for no sound
extern intptr_t *aplWeaponFireSound[MAX_WEAPONS];       // Sound made when firing (each time for automatic)
extern intptr_t *aplWeaponSound2Time[MAX_WEAPONS];      // Alternate sound time
extern intptr_t *aplWeaponSound2Sound[MAX_WEAPONS];     // Alternate sound sound ID
extern intptr_t *aplWeaponReloadSound1[MAX_WEAPONS];    // Sound of magazine being removed
extern intptr_t *aplWeaponReloadSound2[MAX_WEAPONS];    // Sound of magazine being inserted
extern intptr_t *aplWeaponSelectSound[MAX_WEAPONS];     // Sound for weapon selection

extern int g_timerTicsPerSecond;

enum WeaponFlags_t {
    WEAPON_HOLSTER_CLEARS_CLIP = 1,     // 'holstering' clears the current clip
    WEAPON_GLOWS               = 2,     // weapon 'glows' (shrinker and grower)
    WEAPON_AUTOMATIC           = 4,     // automatic fire (continues while 'fire' is held down
    WEAPON_FIREEVERYOTHER      = 8,     // during 'hold time' fire every frame
    WEAPON_FIREEVERYTHIRD      = 16,    // during 'hold time' fire every third frame
    WEAPON_RANDOMRESTART       = 32,    // restart for automatic is 'randomized' by RND 3
    WEAPON_AMMOPERSHOT         = 64,    // uses ammo for each shot (for automatic)
    WEAPON_BOMB_TRIGGER        = 128,   // weapon is the 'bomb' trigger
    WEAPON_NOVISIBLE           = 256,   // weapon use does not cause user to become 'visible'
    WEAPON_THROWIT             = 512,   // weapon 'throws' the 'shoots' item...
    WEAPON_CHECKATRELOAD       = 1024,  // check weapon availability at 'reload' time
    WEAPON_STANDSTILL          = 2048,  // player stops jumping before actual fire (like tripbomb in duke)
    WEAPON_SPAWNTYPE1          = 0,     // just spawn
    WEAPON_SPAWNTYPE2          = 4096,  // spawn like shotgun shells
    WEAPON_SPAWNTYPE3          = 8192,  // spawn like chaingun shells
    WEAPON_SEMIAUTO            = 16384, // cancel button press after each shot
    WEAPON_RELOAD_TIMING       = 32768, // special casing for pistol reload sounds
    WEAPON_RESET               = 65536  // cycle weapon back to frame 1 if fire is held, 0 if not
};

#define TRIPBOMB_TRIPWIRE           1
#define TRIPBOMB_TIMER              2

#define PIPEBOMB_REMOTE             1
#define PIPEBOMB_TIMER              2

// custom projectiles

enum ProjectileFlags_t {
    PROJECTILE_HITSCAN             = 1,
    PROJECTILE_RPG                 = 2,
    PROJECTILE_BOUNCESOFFWALLS     = 4,
    PROJECTILE_BOUNCESOFFMIRRORS   = 8,
    PROJECTILE_KNEE                = 16,
    PROJECTILE_WATERBUBBLES        = 32,
    PROJECTILE_TIMED               = 64,
    PROJECTILE_BOUNCESOFFSPRITES   = 128,
    PROJECTILE_SPIT                = 256,
    PROJECTILE_COOLEXPLOSION1      = 512,
    PROJECTILE_BLOOD               = 1024,
    PROJECTILE_LOSESVELOCITY       = 2048,
    PROJECTILE_NOAIM               = 4096,
    PROJECTILE_RANDDECALSIZE       = 8192,
    PROJECTILE_EXPLODEONTIMER      = 16384,
    PROJECTILE_RPG_IMPACT          = 32768,
    PROJECTILE_RADIUS_PICNUM       = 65536,
    PROJECTILE_ACCURATE_AUTOAIM    = 131072,
    PROJECTILE_FORCEIMPACT         = 262144
};

extern projectile_t ProjectileData[MAXTILES], DefaultProjectileData[MAXTILES];

// logo control

enum LogoFlags_t {
    LOGO_ENABLED           = 1,
    LOGO_PLAYANIM          = 2,
    LOGO_PLAYMUSIC         = 4,
    LOGO_3DRSCREEN         = 8,
    LOGO_TITLESCREEN       = 16,
    LOGO_DUKENUKEM         = 32,
    LOGO_THREEDEE          = 64,
    LOGO_PLUTOPAKSPRITE    = 128,
    LOGO_SHAREWARESCREENS  = 256,
    LOGO_TENSCREEN         = 512
};

enum ScreenTextFlags_t {
    TEXT_XRIGHT = 0x00000001,
    TEXT_XCENTER = 0x00000002,
    TEXT_YBOTTOM = 0x00000004,
    TEXT_YCENTER = 0x00000008,
    TEXT_INTERNALSPACE = 0x00000010,
    TEXT_TILESPACE = 0x00000020,
    TEXT_INTERNALLINE = 0x00000040,
    TEXT_TILELINE = 0x00000080,
    TEXT_XOFFSETZERO = 0x00000100,
    TEXT_XJUSTIFY = 0x00000200,
    TEXT_YOFFSETZERO = 0x00000400,
    TEXT_YJUSTIFY = 0x00000800,
    TEXT_LINEWRAP = 0x00001000,
    TEXT_UPPERCASE = 0x00002000,
    TEXT_INVERTCASE = 0x00004000,
    TEXT_IGNOREESCAPE = 0x00008000,
    TEXT_LITERALESCAPE = 0x00010000,
    TEXT_BACKWARDS = 0x00020000,
    TEXT_GAMETEXTNUMHACK = 0x00040000,
    TEXT_DIGITALNUMBER = 0x00080000,
    TEXT_BIGALPHANUM = 0x00100000,
    TEXT_GRAYFONT = 0x00200000,
};

extern int g_numRealPalettes;

#define MAXCHEATLEN 20
#define NUMCHEATCODES (signed int)(sizeof(CheatStrings)/sizeof(CheatStrings[0]))

extern char CheatStrings[][MAXCHEATLEN];

extern short WeaponPickupSprites[MAX_WEAPONS];

extern int g_numQuoteRedefinitions;

typedef struct {
// this needs to have a copy of everything related to the map/actor state
// see savegame.c
    short numwalls;
    walltype wall[MAXWALLS];
    short numsectors;
    sectortype sector[MAXSECTORS];
    spritetype sprite[MAXSPRITES];
    spriteext_t spriteext[MAXSPRITES];
    short tailspritefree;
    short headspritesect[MAXSECTORS+1];
    short prevspritesect[MAXSPRITES];
    short nextspritesect[MAXSPRITES];
    short headspritestat[MAXSTATUS+1];
    short prevspritestat[MAXSPRITES];
    short nextspritestat[MAXSPRITES];
    short g_numCyclers;
    short cyclers[MAXCYCLERS][6];
    PlayerSpawn_t g_playerSpawnPoints[MAXSPAWNPOINTS];
    short g_numAnimWalls;
    short SpriteDeletionQueue[1024],g_spriteDeleteQueuePos;
    animwalltype animwall[MAXANIMWALLS];
    vec2_t origins[2048];
    short g_mirrorWall[64], g_mirrorSector[64], g_mirrorCount;
    char show2dsector[(MAXSECTORS+7)>>3];
    short g_numClouds,clouds[128],cloudx[128],cloudy[128];
    int g_pskyidx;
    ActorData_t ActorExtra[MAXSPRITES];
    short pskyoff[MAXPSKYTILES], pskybits;

    int animategoal[MAXANIMATES], animatevel[MAXANIMATES], g_animateCount;
    short animatesect[MAXANIMATES];
    int animateptr[MAXANIMATES];
    char g_numPlayerSprites;
    char g_earthquakeTime;
    int lockclock;
    int randomseed, g_globalRandom;
    char scriptptrs[MAXSPRITES];
    intptr_t *vars[MAXGAMEVARS];
} mapstate_t;

typedef struct {
    int partime, designertime;
    char *name, *filename, *musicfn, *alt_musicfn;
    mapstate_t *savedstate;
} map_t;

typedef struct {
    DukePlayer_t *ps;
    input_t *inputBits;

    int32_t movefifoend, syncvalhead, myminlag;
    int32_t pcolor, pteam, frags[MAXPLAYERS], wchoice[MAX_WEAPONS];
    int32_t fraggedby_time, fraggedby_id, fragged_time, fragged_id;
    uint32_t ping;

    char vote, gotvote, playerreadyflag, connected;
    char user_name[32];
    char syncval[SYNCFIFOSIZ][MAXSYNCBYTES];
} playerdata_t;

extern char *team_names[];

// key bindings stuff
typedef struct {
    const char *name;
    int id;
} keydef_t;

extern keydef_t keynames[];
extern const char *mousenames[];

extern char* duke3dgrp;
extern const char* duke3dgrpstring;
extern char mod_dir[BMAX_PATH];

extern hashtable_t gamevarH;
extern hashtable_t arrayH;
extern hashtable_t keywH;
extern hashtable_t gamefuncH;

#include "funct.h"

#ifdef __cplusplus
}
#endif

#include "global.h"
#include "screens.h"
#include "oldnet.h"

#endif

