#ifndef PLAYER_H
#define PLAYER_H

#include "build.h"
#include "inv.h"

#define PHEIGHT (38<<8)

#define PLAYER_ZOFF_NORMAL (40 << 8)
#define PLAYER_ZOFF_WATER (34 << 8)
#define PLAYER_ZOFF_SHRUNK (12 << 8)
#define PLAYER_MINWATERZDIST (16 << 8)
#define PLAYER_STEP_HEIGHT (20 << 8)

#define TEST_ALLOWEDCOLOR(pal) (TEST(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))
#define RESET_ALLOWEDCOLOR(pal) (RESET(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))
#define SET_ALLOWEDCOLOR(pal) (SET(allowedColors[(pal) >> 3], 1 << ((pal) & 7)))

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
    int workslike, extra, cstat, extra_rand, hitradius, range;
    short spawns, sound, isound, vel, decal, trail, tnum, drop, clipdist, offset, bounces, bsound, toffset;
    signed char sxrepeat, syrepeat, txrepeat, tyrepeat, shade, xrepeat, yrepeat, pal, velmult;
} projectile_t;

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

void P_DisplayScubaMask(int snum);
void P_DisplayWeapon(int snum);
void P_GetInput(int snum, bool uncapped);
void P_DropWeapon(DukePlayer_t* p);
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