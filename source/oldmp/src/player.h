#ifndef PLAYER_H
#define PLAYER_H

#include "build.h"
#include "inv.h"
#include "sync.h"
#include "macros.h"

// Definitions
#define PHEIGHT (38<<8)

#define PLAYER_ZOFF_NORMAL (40 << 8)
#define PLAYER_ZOFF_WATER (34 << 8)
#define PLAYER_ZOFF_SHRUNK (12 << 8)
#define PLAYER_MINWATERZDIST (16 << 8)
#define PLAYER_STEP_HEIGHT (20 << 8)

#define TURBOTURNTIME (TICRATE/8) // 7
#define NORMALTURN   15
#define PREAMBLETURN 5
#define NORMALKEYMOVE 40
#define MAXVEL       ((NORMALKEYMOVE*2)+10)
#define MAXSVEL      ((NORMALKEYMOVE*2)+10)
#define MAXANGVEL    1024
#define MAXHORIZ     127
#define DIGITALAXISDEADZONE 1024

#define TEST_ALLOWEDCOLOR(pal) (TEST(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))
#define RESET_ALLOWEDCOLOR(pal) (RESET(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))
#define SET_ALLOWEDCOLOR(pal) (SET(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))

// Enums
enum WeaponFlags_t {
    WEAPON_HOLSTER_CLEARS_CLIP = 1,     // 'holstering' clears the current clip
    WEAPON_GLOWS = 2,     // weapon 'glows' (shrinker and grower)
    WEAPON_AUTOMATIC = 4,     // automatic fire (continues while 'fire' is held down
    WEAPON_FIREEVERYOTHER = 8,     // during 'hold time' fire every frame
    WEAPON_FIREEVERYTHIRD = 16,    // during 'hold time' fire every third frame
    WEAPON_RANDOMRESTART = 32,    // restart for automatic is 'randomized' by RND 3
    WEAPON_AMMOPERSHOT = 64,    // uses ammo for each shot (for automatic)
    WEAPON_BOMB_TRIGGER = 128,   // weapon is the 'bomb' trigger
    WEAPON_NOVISIBLE = 256,   // weapon use does not cause user to become 'visible'
    WEAPON_THROWIT = 512,   // weapon 'throws' the 'shoots' item...
    WEAPON_CHECKATRELOAD = 1024,  // check weapon availability at 'reload' time
    WEAPON_STANDSTILL = 2048,  // player stops jumping before actual fire (like tripbomb in duke)
    WEAPON_SPAWNTYPE1 = 0,     // just spawn
    WEAPON_SPAWNTYPE2 = 4096,  // spawn like shotgun shells
    WEAPON_SPAWNTYPE3 = 8192,  // spawn like chaingun shells
    WEAPON_SEMIAUTO = 16384, // cancel button press after each shot
    WEAPON_RELOAD_TIMING = 32768, // special casing for pistol reload sounds
    WEAPON_RESET = 65536  // cycle weapon back to frame 1 if fire is held, 0 if not
};

typedef struct {
    vec3_t pos, opos, vel;

    int zoom, exitx, exity;
    int invdisptime;
    int bobposx, bobposy, pyoff, opyoff;
    int last_pissed_time, truefz, truecz;
    int player_par;
    int bobcounter, weapon_sway;
    int randomflamex, crack_time;

    fix16_t q16horiz, q16horizoff, oq16horiz, oq16horizoff;
    fix16_t q16ang, oq16ang, q16angvel;

    unsigned int interface_toggle_flag;

    palette_t pals;

    int max_secret_rooms, secret_rooms, actors_killed;
    int runspeed, movement_lock, team;
    int max_player_health, max_shield_amount, max_ammo_amount[MAX_WEAPONS];

    int scream_voice;

    int loogiex[64], loogiey[64], numloogs, loogcnt;

    char palette;

    short sbs, sound_pitch;

    short cursectnum, look_ang, last_extra, subweapon;
    short ammo_amount[MAX_WEAPONS], wackedbyactor, frag, fraggedself;

    short curr_weapon, last_weapon, last_used_weapon, wantweaponfire;
    short holoduke_amount, newowner, hurt_delay, hbomb_hold_delay;
    short jumping_counter, airleft;
    short fta, ftq, access_wallnum, access_spritenum;
    short weapon_ang, firstaid_amount;
    short somethingonplayer, on_crane, i, one_parallax_sectnum;
    short over_shoulder_on, random_club_frame;
    short one_eighty_count, cheat_phase;
    short dummyplayersprite, extra_extra8, quick_kick, last_quick_kick;
    short heat_amount, actorsqu, timebeforeexit, customexitsound;

    short weaprecs[16], weapreccnt;


    short orotscrnang, rotscrnang, dead_flag, show_empty_weapon;   // JBF 20031220: added orotscrnang
    short scuba_amount, jetpack_amount, steroids_amount, shield_amount;
    short holoduke_on, pycount, weapon_pos, frag_ps;
    short transporter_hold, last_full_weapon, footprintshade, boot_amount;

    uint8_t toggle_key_flag;
    uint8_t kickback_pic, knuckle_incs, knee_incs, access_incs, fist_incs, tipincs;

    char aim_mode, auto_aim, weaponswitch;

    char gm, on_warping_sector, footprintcount;
    char hbomb_on, jumping_toggle, rapid_fire_hold, on_ground;
    char inven_icon, buttonpalette;

    char jetpack_on, spritebridge, lastrandomspot;
    char scuba_on, footprintpal, heat_on;

    char  holster_weapon, falling_counter;
    char  gotweapon[MAX_WEAPONS], refresh_inventory;

    char walking_snd_toggle, palookup, hard_landing;
    char return_to_center, reloading, name[32];

    int8_t padding_[1];
} DukePlayer_t;

typedef struct {
    short got_access, last_extra, shield_amount, curr_weapon, holoduke_on;
    short firstaid_amount, steroids_amount, holoduke_amount, jetpack_amount;
    short heat_amount, scuba_amount, boot_amount;
    short last_weapon, weapon_pos, kickback_pic;
    short ammo_amount[MAX_WEAPONS], frag[MAXPLAYERS];
    char inven_icon, jetpack_on, heat_on, gotweapon[MAX_WEAPONS];
} DukeStatus_t;

typedef struct
{
    int32_t visibility;
} PlayerDisplay_t;

typedef struct {
    DukePlayer_t* ps;
    PlayerDisplay_t display;
    input_t* inputBits;

    int32_t movefifoend, syncvalhead, myminlag;
    int32_t pcolor, pteam, frags[MAXPLAYERS], wchoice[MAX_WEAPONS];
    int32_t fraggedby_time, fraggedby_id, fragged_time, fragged_id;
    uint32_t ping;

    char vote, gotvote, playerreadyflag, connected;
    char user_name[32];
    char syncval[SYNCFIFOSIZ][MAXSYNCBYTES];
} playerdata_t;
extern playerdata_t g_player[MAXPLAYERS];

typedef struct {
    int workslike, extra, cstat, extra_rand, hitradius, range;
    short spawns, sound, isound, vel, decal, trail, tnum, drop, clipdist, offset, bounces, bsound, toffset;
    signed char sxrepeat, syrepeat, txrepeat, tyrepeat, shade, xrepeat, yrepeat, pal, velmult;
} projectile_t;

// Externs
#define PWEAPON(Player, Weapon, Wmember) (aplWeapon ## Wmember [Weapon][Player])
extern intptr_t* aplWeaponClip[MAX_WEAPONS];            // number of items in clip
extern intptr_t* aplWeaponReload[MAX_WEAPONS];          // delay to reload (include fire)
extern intptr_t* aplWeaponFireDelay[MAX_WEAPONS];       // delay to fire
extern intptr_t* aplWeaponHoldDelay[MAX_WEAPONS];       // delay after release fire button to fire (0 for none)
extern intptr_t* aplWeaponTotalTime[MAX_WEAPONS];       // The total time the weapon is cycling before next fire.
extern intptr_t* aplWeaponFlags[MAX_WEAPONS];           // Flags for weapon
extern intptr_t* aplWeaponShoots[MAX_WEAPONS];          // what the weapon shoots
extern intptr_t* aplWeaponSpawnTime[MAX_WEAPONS];       // the frame at which to spawn an item
extern intptr_t* aplWeaponSpawn[MAX_WEAPONS];           // the item to spawn
extern intptr_t* aplWeaponShotsPerBurst[MAX_WEAPONS];   // number of shots per 'burst' (one ammo per 'burst'
extern intptr_t* aplWeaponWorksLike[MAX_WEAPONS];       // What original the weapon works like
extern intptr_t* aplWeaponInitialSound[MAX_WEAPONS];    // Sound made when initialy firing. zero for no sound
extern intptr_t* aplWeaponFireSound[MAX_WEAPONS];       // Sound made when firing (each time for automatic)
extern intptr_t* aplWeaponSound2Time[MAX_WEAPONS];      // Alternate sound time
extern intptr_t* aplWeaponSound2Sound[MAX_WEAPONS];     // Alternate sound sound ID
extern intptr_t* aplWeaponReloadSound1[MAX_WEAPONS];    // Sound of magazine being removed
extern intptr_t* aplWeaponReloadSound2[MAX_WEAPONS];    // Sound of magazine being inserted
extern intptr_t* aplWeaponSelectSound[MAX_WEAPONS];     // Sound for weapon selection

extern int16_t WeaponPickupSprites[MAX_WEAPONS];

extern uint8_t allowedColors[(MAXPALOOKUPS + CHAR_BIT - 1) / CHAR_BIT];
extern char pColorNames[MAXPALOOKUPS][32];

void playerColor_init(void);
void playerColor_add(const char* name, int palnum);
void playerColor_delete(int palnum);
uint8_t playerColor_getNextColor(uint8_t palnum, uint8_t useauto);
uint8_t playerColor_getPreviousColor(uint8_t palnum, uint8_t useauto);

void P_SetWeaponGamevars(int playerNum, const DukePlayer_t* const pPlayer);
void P_UpdatePosWhenViewingCam(DukePlayer_t* pPlayer);

int P_GetHudPal(const DukePlayer_t* pPlayer);
int P_GetKneePal(const DukePlayer_t* pPlayer);
int P_FindOtherPlayer(int playerNum, int32_t* pDist);

void P_DisplayScubaMask(int snum);
void P_DisplayWeapon(int snum);
void P_DropWeapon(DukePlayer_t* p);
void P_UpdateScreenPal(DukePlayer_t* p);
void P_QuickKill(DukePlayer_t* p);
void P_GetInput(int snum, bool uncapped);
void P_ProcessInput(int snum);

int Proj_GetDamage(projectile_t* pProj);

static inline void P_PalFrom(DukePlayer_t* pPlayer, uint8_t f, uint8_t r, uint8_t g, uint8_t b)
{
    pPlayer->pals.f = f;
    pPlayer->pals.r = r;
    pPlayer->pals.g = g;
    pPlayer->pals.b = b;
}

static inline uint8_t playerColor_getValidPal(int palnum) { return TEST_ALLOWEDCOLOR(palnum) ? palnum : 0; }

#endif