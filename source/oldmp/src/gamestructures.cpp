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

#include "duke3d.h"
#include "gamedef.h"
#include "osd.h"
#include "gamestructures.h"

const memberlabel_t SectorLabels[] =
{
    { "wallptr",            SECTOR_WALLPTR, 0, 0, -1 },
    { "wallnum",            SECTOR_WALLNUM, 0, 0, -1 },
    { "ceilingz",           SECTOR_CEILINGZ, 0, 0, -1 },
    { "ceilingzgoal",       SECTOR_CEILINGZGOAL, 0, 0, -1 },
    { "ceilingzvel",        SECTOR_CEILINGZVEL, 0, 0, -1 },
    { "floorz",             SECTOR_FLOORZ, 0, 0, -1 },
    { "floorzgoal",         SECTOR_FLOORZGOAL, 0, 0, -1 },
    { "floorzvel",          SECTOR_FLOORZVEL, 0, 0, -1 },
    { "ceilingstat",        SECTOR_CEILINGSTAT, 0, 0, -1 },
    { "floorstat",          SECTOR_FLOORSTAT, 0, 0, -1 },
    { "ceilingpicnum",      SECTOR_CEILINGPICNUM, 0, 0, -1 },
    { "ceilingslope",       SECTOR_CEILINGSLOPE, 0, 0, -1 },
    { "ceilingshade",       SECTOR_CEILINGSHADE, 0, 0, -1 },
    { "ceilingpal",         SECTOR_CEILINGPAL, 0, 0, -1 },
    { "ceilingxpanning",    SECTOR_CEILINGXPANNING, 0, 0, -1 },
    { "ceilingypanning",    SECTOR_CEILINGYPANNING, 0, 0, -1 },
    { "floorpicnum",        SECTOR_FLOORPICNUM, 0, 0, -1 },
    { "floorslope",         SECTOR_FLOORSLOPE, 0, 0, -1 },
    { "floorshade",         SECTOR_FLOORSHADE, 0, 0, -1 },
    { "floorpal",           SECTOR_FLOORPAL, 0, 0, -1 },
    { "floorxpanning",      SECTOR_FLOORXPANNING, 0, 0, -1 },
    { "floorypanning",      SECTOR_FLOORYPANNING, 0, 0, -1 },
    { "visibility",         SECTOR_VISIBILITY, 0, 0, -1 },
    // { "filler",           SECTOR_ALIGNTO, 0, 0, -1 },
    { "alignto",            SECTOR_ALIGNTO, 0, 0, -1 }, // aka filler, not used
    { "lotag",              SECTOR_LOTAG, 0, 0, -1 },
    { "hitag",              SECTOR_HITAG, 0, 0, -1 },
    { "extra",              SECTOR_EXTRA, 0, 0, -1 },
    { "",                   -1, 0, 0, -1  }     // END OF LIST
};

const memberlabel_t WallLabels[] =
{
    { "x",          WALL_X, 0, 0, -1 },
    { "y",          WALL_Y, 0, 0, -1 },
    { "point2",     WALL_POINT2, 0, 0, -1 },
    { "nextwall",   WALL_NEXTWALL, 0, 0, -1 },
    { "nextsector", WALL_NEXTSECTOR, 0, 0, -1 },
    { "cstat",      WALL_CSTAT, 0, 0, -1 },
    { "picnum",     WALL_PICNUM, 0, 0, -1 },
    { "overpicnum", WALL_OVERPICNUM, 0, 0, -1 },
    { "shade",      WALL_SHADE, 0, 0, -1 },
    { "pal",        WALL_PAL, 0, 0, -1 },
    { "xrepeat",    WALL_XREPEAT, 0, 0, -1 },
    { "yrepeat",    WALL_YREPEAT, 0, 0, -1 },
    { "xpanning",   WALL_XPANNING, 0, 0, -1 },
    { "ypanning",   WALL_YPANNING, 0, 0, -1 },
    { "lotag",      WALL_LOTAG, 0, 0, -1 },
    { "hitag",      WALL_HITAG, 0, 0, -1 },
    { "extra",      WALL_EXTRA, 0, 0, -1 },
    { "",           -1, 0, 0, -1  }     // END OF LIST
};

const memberlabel_t ActorLabels[] =
{
    { "x",              ACTOR_X, 0, 0, -1 },
    { "y",              ACTOR_Y, 0, 0, -1 },
    { "z",              ACTOR_Z, 0, 0, -1 },
    { "cstat",          ACTOR_CSTAT, 0, 0, -1 },
    { "picnum",         ACTOR_PICNUM, 0, 0, -1 },
    { "shade",          ACTOR_SHADE, 0, 0, -1 },
    { "pal",            ACTOR_PAL, 0, 0, -1 },
    { "clipdist",       ACTOR_CLIPDIST, 0, 0, -1 },
    //{ "detail",         ACTOR_DETAIL, 0, 0, -1 }, // aka filler, not used
    { "blend",          ACTOR_BLEND, 0, 0, -1 },
    { "xrepeat",        ACTOR_XREPEAT, 0, 0, -1 },
    { "yrepeat",        ACTOR_YREPEAT, 0, 0, -1 },
    { "xoffset",        ACTOR_XOFFSET, 0, 0, -1 },
    { "yoffset",        ACTOR_YOFFSET, 0, 0, -1 },
    { "sectnum",        ACTOR_SECTNUM, 0, 0, -1 },
    { "statnum",        ACTOR_STATNUM, 0, 0, -1 },
    { "ang",            ACTOR_ANG, 0, 0, -1 },
    { "owner",          ACTOR_OWNER, 0, 0, -1 },
    { "xvel",           ACTOR_XVEL, 0, 0, -1 },
    { "yvel",           ACTOR_YVEL, 0, 0, -1 },
    { "zvel",           ACTOR_ZVEL, 0, 0, -1 },
    { "lotag",          ACTOR_LOTAG, 0, 0, -1 },
    { "hitag",          ACTOR_HITAG, 0, 0, -1 },
    { "extra",          ACTOR_EXTRA, 0, 0, -1 },

    // actor labels...
    { "htcgg",          ACTOR_HTCGG, 0, 0, -1 },
    { "htpicnum",       ACTOR_HTPICNUM, 0, 0, -1 },
    { "htang",          ACTOR_HTANG, 0, 0, -1 },
    { "htextra",        ACTOR_HTEXTRA, 0, 0, -1 },
    { "htowner",        ACTOR_HTOWNER, 0, 0, -1 },
    { "htmovflag",      ACTOR_HTMOVFLAG, 0, 0, -1 },
    { "httempang",      ACTOR_HTTEMPANG, 0, 0, -1 },
    { "htactorstayput", ACTOR_HTSTAYPUT, 0, 0, -1 },
    { "htdispicnum",    ACTOR_HTDISPICNUM, 0, 0, -1 },
    { "httimetosleep",  ACTOR_HTTIMETOSLEEP, 0, 0, -1 },
    { "htfloorz",       ACTOR_HTFLOORZ, 0, 0, -1 },
    { "htceilingz",     ACTOR_HTCEILINGZ, 0, 0, -1 },
    { "htlastvx",       ACTOR_HTLASTVX, 0, 0, -1 },
    { "htlastvy",       ACTOR_HTLASTVY, 0, 0, -1 },
    { "htbposx",        ACTOR_HTBPOSX, 0, 0, -1 },
    { "htbposy",        ACTOR_HTBPOSY, 0, 0, -1 },
    { "htbposz",        ACTOR_HTBPOSZ, 0, 0, -1 },
    { "htg_t",          ACTOR_HTG_T, LABEL_HASPARM2, 10, -1 },
    { "htflags",        ACTOR_HTFLAGS, 0, 0, -1 },

    // model flags

    { "angoff",         ACTOR_ANGOFF, 0, 0, -1 },
    { "pitch",          ACTOR_PITCH, 0, 0, -1 },
    { "roll",           ACTOR_ROLL, 0, 0, -1 },
    { "mdxoff",         ACTOR_MDXOFF, 0, 0, -1 },
    { "mdyoff",         ACTOR_MDYOFF, 0, 0, -1 },
    { "mdzoff",         ACTOR_MDZOFF, 0, 0, -1 },
    { "mdflags",        ACTOR_MDFLAGS, 0, 0, -1 },
    { "xpanning",       ACTOR_XPANNING, 0, 0, -1 },
    { "ypanning",       ACTOR_YPANNING, 0, 0, -1 },

    { "alpha",          ACTOR_ALPHA, 0, 0, -1 },

    { "",               -1, 0, 0, -1  }     // END OF LIST
};

const memberlabel_t TsprLabels[] =
{
    // tsprite access

    { "tsprx",          ACTOR_X, 0, 0, -1 },
    { "tspry",          ACTOR_Y, 0, 0, -1 },
    { "tsprz",          ACTOR_Z, 0, 0, -1 },
    { "tsprcstat",      ACTOR_CSTAT, 0, 0, -1 },
    { "tsprpicnum",     ACTOR_PICNUM, 0, 0, -1 },
    { "tsprshade",      ACTOR_SHADE, 0, 0, -1 },
    { "tsprpal",        ACTOR_PAL, 0, 0, -1 },
    { "tsprclipdist",   ACTOR_CLIPDIST, 0, 0, -1 },
    //{ "tsprfiller",   ACTOR_DETAIL, 0, 0, -1 },
    //{ "tsprdetail",     ACTOR_DETAIL, 0, 0, -1 }, // aka filler, not used
    { "tsprblend",      ACTOR_BLEND, 0, 0, -1 },
    { "tsprxrepeat",    ACTOR_XREPEAT, 0, 0, -1 },
    { "tspryrepeat",    ACTOR_YREPEAT, 0, 0, -1 },
    { "tsprxoffset",    ACTOR_XOFFSET, 0, 0, -1 },
    { "tspryoffset",    ACTOR_YOFFSET, 0, 0, -1 },
    { "tsprsectnum",    ACTOR_SECTNUM, 0, 0, -1 },
    { "tsprstatnum",    ACTOR_STATNUM, 0, 0, -1 },
    { "tsprang",        ACTOR_ANG, 0, 0, -1 },
    { "tsprowner",      ACTOR_OWNER, 0, 0, -1 },
#if 1
    { "tsprxvel",       ACTOR_XVEL, 0, 0, -1 },
    { "tspryvel",       ACTOR_YVEL, 0, 0, -1 },
    { "tsprzvel",       ACTOR_ZVEL, 0, 0, -1 },
    { "tsprlotag",      ACTOR_LOTAG, 0, 0, -1 },
    { "tsprhitag",      ACTOR_HITAG, 0, 0, -1 },
    { "tsprextra",      ACTOR_EXTRA, 0, 0, -1 },
#endif
    { "",               -1, 0, 0, -1 } // END OF LIST
};

const memberlabel_t PlayerLabels[] =
{
    { "zoom",                   PLAYER_ZOOM, 0, 0, -1 },
    { "exitx",                  PLAYER_EXITX, 0, 0, -1 },
    { "exity",                  PLAYER_EXITY, 0, 0, -1 },
    { "loogiex",                PLAYER_LOOGIEX, LABEL_HASPARM2, 64, -1 },
    { "loogiey",                PLAYER_LOOGIEY, LABEL_HASPARM2, 64, -1 },
    { "numloogs",               PLAYER_NUMLOOGS, 0, 0, -1 },
    { "loogcnt",                PLAYER_LOOGCNT, 0, 0, -1 },
    { "posx",                   PLAYER_POSX, 0, 0, -1 },
    { "posy",                   PLAYER_POSY, 0, 0, -1 },
    { "posz",                   PLAYER_POSZ, 0, 0, -1 },
    { "horiz",                  PLAYER_HORIZ, 0, 0, -1 },
    { "ohoriz",                 PLAYER_OHORIZ, 0, 0, -1 },
    { "ohorizoff",              PLAYER_OHORIZOFF, 0, 0, -1 },
    { "invdisptime",            PLAYER_INVDISPTIME, 0, 0, -1 },
    { "bobposx",                PLAYER_BOBPOSX, 0, 0, -1 },
    { "bobposy",                PLAYER_BOBPOSY, 0, 0, -1 },
    { "oposx",                  PLAYER_OPOSX, 0, 0, -1 },
    { "oposy",                  PLAYER_OPOSY, 0, 0, -1 },
    { "oposz",                  PLAYER_OPOSZ, 0, 0, -1 },
    { "pyoff",                  PLAYER_PYOFF, 0, 0, -1 },
    { "opyoff",                 PLAYER_OPYOFF, 0, 0, -1 },
    { "posxv",                  PLAYER_POSXV, 0, 0, -1 },
    { "posyv",                  PLAYER_POSYV, 0, 0, -1 },
    { "poszv",                  PLAYER_POSZV, 0, 0, -1 },
    { "last_pissed_time",       PLAYER_LAST_PISSED_TIME, 0, 0, -1 },
    { "truefz",                 PLAYER_TRUEFZ, 0, 0, -1 },
    { "truecz",                 PLAYER_TRUECZ, 0, 0, -1 },
    { "player_par",             PLAYER_PLAYER_PAR, 0, 0, -1 },
    { "visibility",             PLAYER_VISIBILITY, 0, 0, -1 },
    { "bobcounter",             PLAYER_BOBCOUNTER, 0, 0, -1 },
    { "weapon_sway",            PLAYER_WEAPON_SWAY, 0, 0, -1 },
    { "pals_time",              PLAYER_PALS_TIME, 0, 0, -1 },
    { "randomflamex",           PLAYER_RANDOMFLAMEX, 0, 0, -1 },
    { "crack_time",             PLAYER_CRACK_TIME, 0, 0, -1 },
    { "aim_mode",               PLAYER_AIM_MODE, 0, 0, -1 },
    { "ang",                    PLAYER_ANG, 0, 0, -1 },
    { "oang",                   PLAYER_OANG, 0, 0, -1 },
    { "angvel",                 PLAYER_ANGVEL, 0, 0, -1 },
    { "cursectnum",             PLAYER_CURSECTNUM, 0, 0, -1 },
    { "look_ang",               PLAYER_LOOK_ANG, 0, 0, -1 },
    { "last_extra",             PLAYER_LAST_EXTRA, 0, 0, -1 },
    { "subweapon",              PLAYER_SUBWEAPON, 0, 0, -1 },
    { "ammo_amount",            PLAYER_AMMO_AMOUNT, LABEL_HASPARM2, MAX_WEAPONS, -1 },
    { "wackedbyactor",          PLAYER_WACKEDBYACTOR, 0, 0, -1 },
    { "frag",                   PLAYER_FRAG, 0, 0, -1 },
    { "fraggedself",            PLAYER_FRAGGEDSELF, 0, 0, -1 },
    { "curr_weapon",            PLAYER_CURR_WEAPON, 0, 0, -1 },
    { "last_weapon",            PLAYER_LAST_WEAPON, 0, 0, -1 },
    { "tipincs",                PLAYER_TIPINCS, 0, 0, -1 },
    { "horizoff",               PLAYER_HORIZOFF, 0, 0, -1 },
    { "wantweaponfire",         PLAYER_WANTWEAPONFIRE, 0, 0, -1 },
    { "holoduke_amount",        PLAYER_HOLODUKE_AMOUNT, 0, 0, -1 },
    { "newowner",               PLAYER_NEWOWNER, 0, 0, -1 },
    { "hurt_delay",             PLAYER_HURT_DELAY, 0, 0, -1 },
    { "hbomb_hold_delay",       PLAYER_HBOMB_HOLD_DELAY, 0, 0, -1 },
    { "jumping_counter",        PLAYER_JUMPING_COUNTER, 0, 0, -1 },
    { "airleft",                PLAYER_AIRLEFT, 0, 0, -1 },
    { "knee_incs",              PLAYER_KNEE_INCS, 0, 0, -1 },
    { "access_incs",            PLAYER_ACCESS_INCS, 0, 0, -1 },
    { "fta",                    PLAYER_FTA, 0, 0, -1 },
    { "ftq",                    PLAYER_FTQ, 0, 0, -1 },
    { "access_wallnum",         PLAYER_ACCESS_WALLNUM, 0, 0, -1 },
    { "access_spritenum",       PLAYER_ACCESS_SPRITENUM, 0, 0, -1 },
    { "kickback_pic",           PLAYER_KICKBACK_PIC, 0, 0, -1 },
    { "got_access",             PLAYER_GOT_ACCESS, 0, 0, -1 },
    { "weapon_ang",             PLAYER_WEAPON_ANG, 0, 0, -1 },
    { "firstaid_amount",        PLAYER_FIRSTAID_AMOUNT, 0, 0, -1 },
    { "somethingonplayer",      PLAYER_SOMETHINGONPLAYER, 0, 0, -1 },
    { "on_crane",               PLAYER_ON_CRANE, 0, 0, -1 },
    { "i",                      PLAYER_I, 0, 0, -1 },
    { "one_parallax_sectnum",   PLAYER_ONE_PARALLAX_SECTNUM, 0, 0, -1 },
    { "over_shoulder_on",       PLAYER_OVER_SHOULDER_ON, 0, 0, -1 },
    { "random_club_frame",      PLAYER_RANDOM_CLUB_FRAME, 0, 0, -1 },
    { "fist_incs",              PLAYER_FIST_INCS, 0, 0, -1 },
    { "one_eighty_count",       PLAYER_ONE_EIGHTY_COUNT, 0, 0, -1 },
    { "cheat_phase",            PLAYER_CHEAT_PHASE, 0, 0, -1 },
    { "dummyplayersprite",      PLAYER_DUMMYPLAYERSPRITE, 0, 0, -1 },
    { "extra_extra8",           PLAYER_EXTRA_EXTRA8, 0, 0, -1 },
    { "quick_kick",             PLAYER_QUICK_KICK, 0, 0, -1 },
    { "heat_amount",            PLAYER_HEAT_AMOUNT, 0, 0, -1 },
    { "actorsqu",               PLAYER_ACTORSQU, 0, 0, -1 },
    { "timebeforeexit",         PLAYER_TIMEBEFOREEXIT, 0, 0, -1 },
    { "customexitsound",        PLAYER_CUSTOMEXITSOUND, 0, 0, -1 },
    { "weaprecs",               PLAYER_WEAPRECS, LABEL_HASPARM2, MAX_WEAPONS, -1 },
    { "weapreccnt",             PLAYER_WEAPRECCNT, 0, 0, -1 },
    { "interface_toggle_flag",  PLAYER_INTERFACE_TOGGLE_FLAG, 0, 0, -1 },
    { "rotscrnang",             PLAYER_ROTSCRNANG, 0, 0, -1 },
    { "orotscrnang",            PLAYER_OROTSCRNANG, 0, 0, -1 },
    { "dead_flag",              PLAYER_DEAD_FLAG, 0, 0, -1 },
    { "show_empty_weapon",      PLAYER_SHOW_EMPTY_WEAPON, 0, 0, -1 },
    { "scuba_amount",           PLAYER_SCUBA_AMOUNT, 0, 0, -1 },
    { "jetpack_amount",         PLAYER_JETPACK_AMOUNT, 0, 0, -1 },
    { "steroids_amount",        PLAYER_STEROIDS_AMOUNT, 0, 0, -1 },
    { "shield_amount",          PLAYER_SHIELD_AMOUNT, 0, 0, -1 },
    { "holoduke_on",            PLAYER_HOLODUKE_ON, 0, 0, -1 },
    { "pycount",                PLAYER_PYCOUNT, 0, 0, -1 },
    { "weapon_pos",             PLAYER_WEAPON_POS, 0, 0, -1 },
    { "frag_ps",                PLAYER_FRAG_PS, 0, 0, -1 },
    { "transporter_hold",       PLAYER_TRANSPORTER_HOLD, 0, 0, -1 },
    { "last_full_weapon",       PLAYER_LAST_FULL_WEAPON, 0, 0, -1 },
    { "footprintshade",         PLAYER_FOOTPRINTSHADE, 0, 0, -1 },
    { "boot_amount",            PLAYER_BOOT_AMOUNT, 0, 0, -1 },
    { "scream_voice",           PLAYER_SCREAM_VOICE, 0, 0, -1 },
    { "gm",                     PLAYER_GM, 0, 0, -1 },
    { "on_warping_sector",      PLAYER_ON_WARPING_SECTOR, 0, 0, -1 },
    { "footprintcount",         PLAYER_FOOTPRINTCOUNT, 0, 0, -1 },
    { "hbomb_on",               PLAYER_HBOMB_ON, 0, 0, -1 },
    { "jumping_toggle",         PLAYER_JUMPING_TOGGLE, 0, 0, -1 },
    { "rapid_fire_hold",        PLAYER_RAPID_FIRE_HOLD, 0, 0, -1 },
    { "on_ground",              PLAYER_ON_GROUND, 0, 0, -1 },
    { "name",                   PLAYER_NAME,  LABEL_ISSTRING, 32, -1 },
    { "inven_icon",             PLAYER_INVEN_ICON, 0, 0, -1 },
    { "buttonpalette",          PLAYER_BUTTONPALETTE, 0, 0, -1 },
    { "jetpack_on",             PLAYER_JETPACK_ON, 0, 0, -1 },
    { "spritebridge",           PLAYER_SPRITEBRIDGE, 0, 0, -1 },
    { "lastrandomspot",         PLAYER_LASTRANDOMSPOT, 0, 0, -1 },
    { "scuba_on",               PLAYER_SCUBA_ON, 0, 0, -1 },
    { "footprintpal",           PLAYER_FOOTPRINTPAL, 0, 0, -1 },
    { "heat_on",                PLAYER_HEAT_ON, 0, 0, -1 },
    { "holster_weapon",         PLAYER_HOLSTER_WEAPON, 0, 0, -1 },
    { "falling_counter",        PLAYER_FALLING_COUNTER, 0, 0, -1 },
    { "gotweapon",              PLAYER_GOTWEAPON, LABEL_HASPARM2, MAX_WEAPONS, -1 },
    { "palette",                PLAYER_PALETTE, 0, 0, -1 },
    { "toggle_key_flag",        PLAYER_TOGGLE_KEY_FLAG, 0, 0, -1 },
    { "knuckle_incs",           PLAYER_KNUCKLE_INCS, 0, 0, -1 },
    { "walking_snd_toggle",     PLAYER_WALKING_SND_TOGGLE, 0, 0, -1 },
    { "palookup",               PLAYER_PALOOKUP, 0, 0, -1 },
    { "hard_landing",           PLAYER_HARD_LANDING, 0, 0, -1 },
    { "max_secret_rooms",       PLAYER_MAX_SECRET_ROOMS, 0, 0, -1 },
    { "secret_rooms",           PLAYER_SECRET_ROOMS, 0, 0, -1 },
    { "pals",                   PLAYER_PALS, LABEL_HASPARM2, 3, -1 },
    { "max_actors_killed",      PLAYER_MAX_ACTORS_KILLED, 0, 0, -1 },
    { "actors_killed",          PLAYER_ACTORS_KILLED, 0, 0, -1 },
    { "return_to_center",       PLAYER_RETURN_TO_CENTER, 0, 0, -1 },
    { "runspeed",               PLAYER_RUNSPEED, 0, 0, -1 },
    { "sbs",                    PLAYER_SBS, 0, 0, -1 },
    { "reloading",              PLAYER_RELOADING, 0, 0, -1 },
    { "auto_aim",               PLAYER_AUTO_AIM, 0, 0, -1 },
    { "movement_lock",          PLAYER_MOVEMENT_LOCK, 0, 0, -1 },
    { "sound_pitch",            PLAYER_SOUND_PITCH, 0, 0, -1 },
    { "weaponswitch",           PLAYER_WEAPONSWITCH, 0, 0, -1 },
    { "team",                   PLAYER_TEAM, 0, 0, -1 },
    { "max_player_health",      PLAYER_MAX_PLAYER_HEALTH, 0, 0, -1 },
    { "max_shield_amount",      PLAYER_MAX_SHIELD_AMOUNT, 0, 0, -1 },
    { "max_ammo_amount",        PLAYER_MAX_AMMO_AMOUNT, LABEL_HASPARM2, MAX_WEAPONS, -1 },
    { "last_quick_kick",        PLAYER_LAST_QUICK_KICK, 0, 0, -1 },
    { "connected",              PLAYER_CONNECTED, 0, 0, -1 },
    { "lowsprite",              PLAYER_LOWSPRITE, 0, 0, -1 },
    { "highsprite",             PLAYER_HIGHSPRITE, 0, 0, -1 },
    { "",                       -1, 0, 0, -1 } // END OF LIST
};

const memberlabel_t ProjectileLabels[] =
{
    { "workslike",  PROJ_WORKSLIKE, 0, 0, -1 },
    { "spawns",     PROJ_SPAWNS, 0, 0, -1 },
    { "sxrepeat",   PROJ_SXREPEAT, 0, 0, -1 },
    { "syrepeat",   PROJ_SYREPEAT, 0, 0, -1 },
    { "sound",      PROJ_SOUND, 0, 0, -1 },
    { "isound",     PROJ_ISOUND, 0, 0, -1 },
    { "vel",        PROJ_VEL, 0, 0, -1 },
    { "extra",      PROJ_EXTRA, 0, 0, -1 },
    { "decal",      PROJ_DECAL, 0, 0, -1 },
    { "trail",      PROJ_TRAIL, 0, 0, -1 },
    { "txrepeat",   PROJ_TXREPEAT, 0, 0, -1 },
    { "tyrepeat",   PROJ_TYREPEAT, 0, 0, -1 },
    { "toffset",    PROJ_TOFFSET, 0, 0, -1 },
    { "tnum",       PROJ_TNUM, 0, 0, -1 },
    { "drop",       PROJ_DROP, 0, 0, -1 },
    { "cstat",      PROJ_CSTAT, 0, 0, -1 },
    { "clipdist",   PROJ_CLIPDIST, 0, 0, -1 },
    { "shade",      PROJ_SHADE, 0, 0, -1 },
    { "xrepeat",    PROJ_XREPEAT, 0, 0, -1 },
    { "yrepeat",    PROJ_YREPEAT, 0, 0, -1 },
    { "pal",        PROJ_PAL, 0, 0, -1 },
    { "extra_rand", PROJ_EXTRA_RAND, 0, 0, -1 },
    { "hitradius",  PROJ_HITRADIUS, 0, 0, -1 },
    { "velmult",    PROJ_MOVECNT, 0, 0, -1 },
    { "offset",     PROJ_OFFSET, 0, 0, -1 },
    { "bounces",    PROJ_BOUNCES, 0, 0, -1 },
    { "bsound",     PROJ_BSOUND, 0, 0, -1 },
    { "range",      PROJ_RANGE, 0, 0, -1 },
    { "",           -1, 0, 0, -1 } // END OF LIST
};

const memberlabel_t UserdefsLabels[] =
{
    { "god",                        USERDEFS_GOD,                   0, 0, -1 },
    { "warp_on",                    USERDEFS_WARP_ON,               0, 0, -1 },
    { "cashman",                    USERDEFS_CASHMAN,               0, 0, -1 },
    { "eog",                        USERDEFS_EOG,                   0, 0, -1 },
    { "showallmap",                 USERDEFS_SHOWALLMAP,            0, 0, -1 },
    { "show_help",                  USERDEFS_SHOW_HELP,             0, 0, -1 },
    { "scrollmode",                 USERDEFS_SCROLLMODE,            0, 0, -1 },
    { "clipping",                   USERDEFS_CLIPPING,              0, 0, -1 },
    { "user_name",                  USERDEFS_USER_NAME,             LABEL_HASPARM2, MAXPLAYERS, -1 },
    { "ridecule",                   USERDEFS_RIDECULE,              LABEL_HASPARM2 | LABEL_ISSTRING, 10, -1 },
    { "savegame",                   USERDEFS_SAVEGAME,              LABEL_HASPARM2 | LABEL_ISSTRING, 10, -1 },
    { "pwlockout",                  USERDEFS_PWLOCKOUT,             LABEL_ISSTRING, 128, -1 },
    { "rtsname;",                   USERDEFS_RTSNAME,               LABEL_ISSTRING, 128, -1 },
    { "overhead_on",                USERDEFS_OVERHEAD_ON,           0, 0, -1 },
    { "last_overhead",              USERDEFS_LAST_OVERHEAD,         0, 0, -1 },
    { "showweapons",                USERDEFS_SHOWWEAPONS,           0, 0, -1 },

    { "pause_on",                   USERDEFS_PAUSE_ON,              0, 0, -1 },
    { "from_bonus",                 USERDEFS_FROM_BONUS,            0, 0, -1 },
    { "camerasprite",               USERDEFS_CAMERASPRITE,          0, 0, -1 },
    { "last_camsprite",             USERDEFS_LAST_CAMSPRITE,        0, 0, -1 },
    { "last_level",                 USERDEFS_LAST_LEVEL,            0, 0, -1 },
    { "secretlevel",                USERDEFS_SECRETLEVEL,           0, 0, -1 },

    { "const_visibility",           USERDEFS_CONST_VISIBILITY,      0, 0, -1 },
    { "uw_framerate",               USERDEFS_UW_FRAMERATE,          0, 0, -1 },
    { "camera_time",                USERDEFS_CAMERA_TIME,           0, 0, -1 },
    { "folfvel",                    USERDEFS_FOLFVEL,               0, 0, -1 },
    { "folavel",                    USERDEFS_FOLAVEL,               0, 0, -1 },
    { "folx",                       USERDEFS_FOLX,                  0, 0, -1 },
    { "foly",                       USERDEFS_FOLY,                  0, 0, -1 },
    { "fola",                       USERDEFS_FOLA,                  0, 0, -1 },
    { "reccnt",                     USERDEFS_RECCNT,                0, 0, -1 },

    { "entered_name",               USERDEFS_ENTERED_NAME,          0, 0, -1 },
    { "screen_tilting",             USERDEFS_SCREEN_TILTING,        0, 0, -1 },
    { "shadows",                    USERDEFS_SHADOWS,               0, 0, -1 },
    { "fta_on",                     USERDEFS_FTA_ON,                0, 0, -1 },
    { "executions",                 USERDEFS_EXECUTIONS,            0, 0, -1 },
    { "auto_run",                   USERDEFS_AUTO_RUN,              0, 0, -1 },
    { "coords",                     USERDEFS_COORDS,                0, 0, -1 },
    { "tickrate",                   USERDEFS_TICKRATE,              0, 0, -1 },
    { "m_coop",                     USERDEFS_M_COOP,                0, 0, -1 },
    { "coop",                       USERDEFS_COOP,                  0, 0, -1 },
    { "screen_size",                USERDEFS_SCREEN_SIZE,           0, 0, -1 },
    { "lockout",                    USERDEFS_LOCKOUT,               0, 0, -1 },
    { "crosshair",                  USERDEFS_CROSSHAIR,             0, 0, -1 },

    { "wchoice[MAXPLAYERS][MAX_WEAPONS]",   USERDEFS_WCHOICE, 0, 0, -1 },

    { "playerai",                   USERDEFS_PLAYERAI,              0, 0, -1 },
    { "respawn_monsters",           USERDEFS_RESPAWN_MONSTERS,      0, 0, -1 },
    { "respawn_items",              USERDEFS_RESPAWN_ITEMS,         0, 0, -1 },
    { "respawn_inventory",          USERDEFS_RESPAWN_INVENTORY,     0, 0, -1 },
    { "recstat",                    USERDEFS_RECSTAT,               0, 0, -1 },
    { "monsters_off",               USERDEFS_MONSTERS_OFF,          0, 0, -1 },
    { "brightness",                 USERDEFS_BRIGHTNESS,            0, 0, -1 },
    { "m_respawn_items",            USERDEFS_M_RESPAWN_ITEMS,       0, 0, -1 },
    { "m_respawn_monsters",         USERDEFS_M_RESPAWN_MONSTERS,    0, 0, -1 },
    { "m_respawn_inventory",        USERDEFS_M_RESPAWN_INVENTORY,   0, 0, -1 },
    { "m_recstat",                  USERDEFS_M_RECSTAT,             0, 0, -1 },
    { "m_monsters_off",             USERDEFS_M_MONSTERS_OFF,        0, 0, -1 },
    { "detail",                     USERDEFS_DETAIL,                0, 0, -1 },
    { "m_ffire",                    USERDEFS_M_FFIRE,               0, 0, -1 },
    { "ffire",                      USERDEFS_FFIRE,                 0, 0, -1 },
    { "m_player_skill",             USERDEFS_M_PLAYER_SKILL,        0, 0, -1 },
    { "m_level_number",             USERDEFS_M_LEVEL_NUMBER,        0, 0, -1 },
    { "m_volume_number",            USERDEFS_M_VOLUME_NUMBER,       0, 0, -1 },
    { "multimode",                  USERDEFS_MULTIMODE,             0, 0, -1 },
    { "player_skill",               USERDEFS_PLAYER_SKILL,          0, 0, -1 },
    { "level_number",               USERDEFS_LEVEL_NUMBER,          0, 0, -1 },
    { "volume_number",              USERDEFS_VOLUME_NUMBER,         0, 0, -1 },
    { "m_marker",                   USERDEFS_M_MARKER,              0, 0, -1 },
    { "marker",                     USERDEFS_MARKER,                0, 0, -1 },
    { "mouseflip",                  USERDEFS_MOUSEFLIP,             0, 0, -1 },
    { "statusbarscale",             USERDEFS_STATUSBARSCALE,        0, 0, -1 },
    { "drawweapon",                 USERDEFS_DRAWWEAPON,            0, 0, -1 },
    { "mouseaiming",                USERDEFS_MOUSEAIMING,           0, 0, -1 },
    { "weaponswitch",               USERDEFS_WEAPONSWITCH,          0, 0, -1 },
    { "democams",                   USERDEFS_DEMOCAMS,              0, 0, -1 },
    { "color",                      USERDEFS_COLOR,                 0, 0, -1 },
    { "msgdisptime",                USERDEFS_MSGDISPTIME,           0, 0, -1 },
    { "statusbarmode",              USERDEFS_STATUSBARMODE,         0, 0, -1 },
    { "m_noexits",                  USERDEFS_M_NOEXITS,             0, 0, -1 },
    { "noexits",                    USERDEFS_NOEXITS,               0, 0, -1 },
    { "autovote",                   USERDEFS_AUTOVOTE,              0, 0, -1 },
    { "automsg",                    USERDEFS_AUTOMSG,               0, 0, -1 },
    { "idplayers",                  USERDEFS_IDPLAYERS,             0, 0, -1 },
    { "team",                       USERDEFS_TEAM,                  0, 0, -1 },
    { "viewbob",                    USERDEFS_VIEWBOB,               0, 0, -1 },
    { "weaponsway",                 USERDEFS_WEAPONSWAY,            0, 0, -1 },
    { "angleinterpolation",         USERDEFS_ANGLEINTERPOLATION,    0, 0, -1 },
    { "obituaries",                 USERDEFS_OBITUARIES,            0, 0, -1 },
    { "levelstats",                 USERDEFS_LEVELSTATS,            0, 0, -1 },
    { "crosshairscale",             USERDEFS_CROSSHAIRSCALE,        0, 0, -1 },
    { "althud",                     USERDEFS_ALTHUD,                0, 0, -1 },
    { "display_bonus_screen",       USERDEFS_DISPLAY_BONUS_SCREEN,  0, 0, -1 },
    { "show_level_text",            USERDEFS_SHOW_LEVEL_TEXT,       0, 0, -1 },
    { "weaponscale",                USERDEFS_WEAPONSCALE,           0, 0, -1 },
    { "textscale",                  USERDEFS_TEXTSCALE,             0, 0, -1 },
    { "return",                     USERDEFS_RETURN,                LABEL_HASPARM2, MAX_RETURN_VALUES, -1 },
    { "teampal",                    USERDEFS_TEAMPAL,               LABEL_HASPARM2, MAXPLAYERS, -1 },
    { "teamfrags",                  USERDEFS_TEAMFRAGS,             LABEL_HASPARM2, MAXPLAYERS, -1 },
    { "teamsuicides",               USERDEFS_TEAMSUICIDES,          LABEL_HASPARM2, MAXPLAYERS, -1 },
    { "dmflags",                    USERDEFS_DMFLAGS,               0, 0, -1 },
    { "m_dmflags",                  USERDEFS_M_DMFLAGS,             0, 0, -1 },
    { "draw_yxaspect",              USERDEFS_DRAW_YXASPECT,         0, 0, -1 },
    { "monsters_killed",            USERDEFS_MONSTERS_KILLED,       0, 0, -1 },
    { "",                           -1,                             0, 0, -1 } // END OF LIST
};

const memberlabel_t InputLabels[] =
{
    { "avel",       INPUT_AVEL,     0, 0, -1 },
    { "horz",       INPUT_HORZ,     0, 0, -1 },
    { "fvel",       INPUT_FVEL,     0, 0, -1 },
    { "svel",       INPUT_SVEL,     0, 0, -1 },
    { "bits",       INPUT_BITS,     0, 0, -1 },
    { "extbits",    INPUT_EXTBITS,  0, 0, -1 },
    { "",           -1,             0, 0, -1 } // END OF LIST
};

const memberlabel_t TileDataLabels[] =
{
    // tilesiz[]
    { "xsize",      TILEDATA_XSIZE,      0, 0, -1 },
    { "ysize",      TILEDATA_YSIZE,      0, 0, -1 },

    // picanm[]
    { "animframes", TILEDATA_ANIMFRAMES, 0, 0, -1 },
    { "xoffset",    TILEDATA_XOFFSET,    0, 0, -1 },
    { "yoffset",    TILEDATA_YOFFSET,    0, 0, -1 },
    { "animspeed",  TILEDATA_ANIMSPEED,  0, 0, -1 },
    { "animtype",   TILEDATA_ANIMTYPE,   0, 0, -1 },

    // g_tile[]
    { "gameflags",  TILEDATA_GAMEFLAGS,  0, 0, -1 },
    { "",           -1,                  0, 0, -1 } // END OF LIST
};

hashtable_t h_actor         = { ACTOR_END >> 1, NULL };
hashtable_t h_input         = { INPUT_END >> 1, NULL };
//hashtable_t h_paldata    = { PALDATA_END>>1, NULL };
hashtable_t h_player        = { PLAYER_END >> 1, NULL };
hashtable_t h_projectile    = { PROJ_END >> 1, NULL };
hashtable_t h_sector        = { SECTOR_END >> 1, NULL };
hashtable_t h_tiledata      = { TILEDATA_END >> 1, NULL };
hashtable_t h_tsprite       = { ACTOR_END >> 1, NULL };
hashtable_t h_userdef       = { USERDEFS_END >> 1, NULL };
hashtable_t h_wall          = { WALL_END >> 1, NULL };

#define STRUCT_HASH_SETUP(table, labels)                 \
    do                                                   \
    {                                                    \
        for (int i = 0; i < ARRAY_SSIZE(labels); i++)    \
            hash_add(&table, labels[i].name, i, 0);      \
        EDUKE32_STATIC_ASSERT(ARRAY_SSIZE(labels) != 0); \
    } while (0)

void VM_InitHashTables(void)
{
    for (auto table : vmStructHashTablePtrs)
        hash_init(table);

    inithashnames();

    STRUCT_HASH_SETUP(h_actor, ActorLabels);
    STRUCT_HASH_SETUP(h_input, InputLabels);
    //STRUCT_HASH_SETUP(h_paldata, PalDataLabels);
    STRUCT_HASH_SETUP(h_player, PlayerLabels);
    STRUCT_HASH_SETUP(h_projectile, ProjectileLabels);
    STRUCT_HASH_SETUP(h_sector, SectorLabels);
    STRUCT_HASH_SETUP(h_tiledata, TileDataLabels);
    STRUCT_HASH_SETUP(h_tsprite, TsprLabels);
    STRUCT_HASH_SETUP(h_userdef, UserdefsLabels);
    STRUCT_HASH_SETUP(h_wall, WallLabels);
}
#undef STRUCT_HASH_SETUP

#define LABEL(struct, memb, name, idx)                                                              \
    {                                                                                                               \
        name, idx, sizeof(struct[0].memb) | (is_unsigned<decltype(struct[0].memb)>::value ? LABEL_UNSIGNED : 0), 0, \
        offsetof(remove_pointer_t<decltype(&struct[0])>, memb)                                                      \
    }

#define MEMBER(struct, memb, idx) LABEL(struct, memb, #memb, idx)

// this is all the crap for accessing the game's structs through the CON VM
// I got a 3-4 fps gain by inlining these...

void __fastcall X_AccessTileData(int iSet, int lVar1, int lLabelID, int lVar2)
{
    int lValue = 0, tile = g_i;

    if (lVar1 != sysVarIDs.THISACTOR)
        tile = Gv_GetVar(lVar1, g_i, g_p);

    if ((tile < 0 || tile >= MAXTILES) && g_scriptSanityChecks)
    {
        OSD_Printf(CON_ERROR "tried to %s %s on invalid tile number (%d) from sprite id: %d, picnum: %d.\n", g_errorLineNum, VM_GetKeywordForID(g_tw),
            iSet ? "set" : "get", TileDataLabels[lLabelID].name, tile, g_i, TrackerCast(g_sp->picnum));
        insptr += (lVar2 == MAXGAMEVARS);
        return;
    }

    if (iSet)
        lValue = Gv_GetVar(lVar2, g_i, g_p);

    switch (lLabelID)
    {
    case TILEDATA_XSIZE:
        if (iSet)
        {
            tilesiz[tile].x = lValue;
            return;
        }
        Gv_SetVar(lVar2, tilesiz[tile].x, g_i, g_p);
        return;
    case TILEDATA_YSIZE:
        if (iSet)
        {
            tilesiz[tile].y = lValue;
            return;
        }
        Gv_SetVar(lVar2, tilesiz[tile].y, g_i, g_p);
        return;
    case TILEDATA_GAMEFLAGS:
        if (iSet)
        {
            g_tile[tile].flags = lValue;
            return;
        }
        Gv_SetVar(lVar2, g_tile[tile].flags, g_i, g_p);
        return;
    case TILEDATA_ANIMFRAMES:
        if (iSet)
        {
            picanm[tile].num = lValue;
            return;
        }
        Gv_SetVar(lVar2, picanm[tile].num, g_i, g_p);
        return;
    case TILEDATA_XOFFSET:
        if (iSet)
        {
            picanm[tile].xofs = lValue;
            return;
        }
        Gv_SetVar(lVar2, picanm[tile].xofs, g_i, g_p);
        return;
    case TILEDATA_YOFFSET:
        if (iSet)
        {
            picanm[tile].yofs = lValue;
            return;
        }
        Gv_SetVar(lVar2, picanm[tile].yofs, g_i, g_p);
        return;
    case TILEDATA_ANIMSPEED:
        if (iSet)
        {
            picanm[tile].sf = (picanm[tile].sf & ~PICANM_ANIMSPEED_MASK) | (lValue & PICANM_ANIMSPEED_MASK);
            return;
        }
        Gv_SetVar(lVar2, (picanm[tile].sf & PICANM_ANIMSPEED_MASK), g_i, g_p);
        return;
    case TILEDATA_ANIMTYPE:
        if (iSet)
        {
            picanm[tile].sf = (picanm[tile].sf & ~PICANM_ANIMTYPE_MASK) | ((lValue << PICANM_ANIMTYPE_SHIFT) & PICANM_ANIMTYPE_MASK);
            return;
        }
        Gv_SetVar(lVar2, (picanm[tile].sf & PICANM_ANIMTYPE_MASK) >> PICANM_ANIMTYPE_SHIFT, g_i, g_p);
        return;
    default:
        return;
    }
}

void __fastcall X_AccessUserdef(int iSet, int labelNum, int lVar2, int lParm2)
{
    int const lValue = (iSet) ? Gv_GetVar(lVar2, g_i, g_p) : 0;
    auto const& udLabel = UserdefsLabels[labelNum];

    if (EDUKE32_PREDICT_FALSE(udLabel.flags & LABEL_HASPARM2 && (unsigned)lParm2 >= (unsigned)udLabel.maxParm2))
    {
        CON_ERRPRINTF("%s[%d] invalid for userdef", udLabel.name, lParm2);
        return;
    }

    switch (udLabel.lId)
    {
    case USERDEFS_GOD:
        if (iSet)
        {
            ud.god = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.god, g_i, g_p);
        return;

    case USERDEFS_WARP_ON:
        if (iSet)
        {
            ud.warp_on = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.warp_on, g_i, g_p);
        return;

    case USERDEFS_CASHMAN:
        if (iSet)
        {
            ud.cashman = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.cashman, g_i, g_p);
        return;

    case USERDEFS_EOG:
        if (iSet)
        {
            ud.eog = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.eog, g_i, g_p);
        return;

    case USERDEFS_SHOWALLMAP:
        if (iSet)
        {
            ud.showallmap = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.showallmap, g_i, g_p);
        return;

    case USERDEFS_SHOW_HELP:
        if (iSet)
        {
            ud.show_help = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.show_help, g_i, g_p);
        return;

    case USERDEFS_SCROLLMODE:
        if (iSet)
        {
            ud.scrollmode = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.scrollmode, g_i, g_p);
        return;

    case USERDEFS_CLIPPING:
        if (iSet)
        {
            ud.noclip = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.noclip, g_i, g_p);
        return;

        //  case USERDEFS_USER_NAME:
        //      if(iSet)
        //      {
        //          ud.user_name[MAXPLAYERS][32] = lValue;
        //          return;
        //      }
        //      Gv_SetVar(lVar2, ud.user_name[MAXPLAYERS][32], g_i, g_p);
        //      return;

        //  case USERDEFS_RIDECULE:
        //      if(iSet)
        //      {
        //          ud.ridecule = lValue;
        //          return;
        //      }
        //      Gv_SetVar(lVar2, ud.ridecule, g_i, g_p);
        //      return;

        //  case USERDEFS_SAVEGAME:
        //      if(iSet)
        //      {
        //          ud.savegame = lValue;
        //          return;
        //      }
        //      Gv_SetVar(lVar2, ud.savegame, g_i, g_p);
        //      return;

        //  case USERDEFS_PWLOCKOUT:
        //      if(iSet)
        //      {
        //          ud.pwlockout = lValue;
        //          return;
        //      }
        //      Gv_SetVar(lVar2, ud.pwlockout, g_i, g_p);
        //      return;

        //  case USERDEFS_RTSNAME:
        //      if(iSet)
        //      {
        //          ud.rtsname = lValue;
        //          return;
        //      }
        //      Gv_SetVar(lVar2, ud.rtsname, g_i, g_p);
        //      return;

    case USERDEFS_OVERHEAD_ON:
        if (iSet)
        {
            ud.overhead_on = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.overhead_on, g_i, g_p);
        return;

    case USERDEFS_LAST_OVERHEAD:
        if (iSet)
        {
            ud.last_overhead = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.last_overhead, g_i, g_p);
        return;

    case USERDEFS_SHOWWEAPONS:
        if (iSet)
        {
            ud.showweapons = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.showweapons, g_i, g_p);
        return;

    case USERDEFS_PAUSE_ON:
        if (iSet)
        {
            ud.pause_on = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.pause_on, g_i, g_p);
        return;

    case USERDEFS_FROM_BONUS:
        if (iSet)
        {
            ud.from_bonus = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.from_bonus, g_i, g_p);
        return;

    case USERDEFS_CAMERASPRITE:
        if (iSet)
        {
            ud.camerasprite = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.camerasprite, g_i, g_p);
        return;

    case USERDEFS_LAST_CAMSPRITE:
        if (iSet)
        {
            ud.last_camsprite = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.last_camsprite, g_i, g_p);
        return;

    case USERDEFS_LAST_LEVEL:
        if (iSet)
        {
            ud.last_level = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.last_level, g_i, g_p);
        return;

    case USERDEFS_SECRETLEVEL:
        if (iSet)
        {
            ud.secretlevel = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.secretlevel, g_i, g_p);
        return;

    case USERDEFS_CONST_VISIBILITY:
        if (iSet)
        {
            ud.const_visibility = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.const_visibility, g_i, g_p);
        return;

    case USERDEFS_UW_FRAMERATE:
        if (iSet)
        {
            ud.uw_framerate = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.uw_framerate, g_i, g_p);
        return;

    case USERDEFS_CAMERA_TIME:
        if (iSet)
        {
            ud.camera_time = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.camera_time, g_i, g_p);
        return;

    case USERDEFS_FOLFVEL:
        if (iSet)
        {
            ud.folfvel = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.folfvel, g_i, g_p);
        return;

    case USERDEFS_FOLAVEL:
        if (iSet)
        {
            ud.folavel = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.folavel, g_i, g_p);
        return;

    case USERDEFS_FOLX:
        if (iSet)
        {
            ud.folx = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.folx, g_i, g_p);
        return;

    case USERDEFS_FOLY:
        if (iSet)
        {
            ud.foly = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.foly, g_i, g_p);
        return;

    case USERDEFS_FOLA:
        if (iSet)
        {
            ud.fola = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.fola, g_i, g_p);
        return;

    case USERDEFS_RECCNT:
        if (iSet)
        {
            ud.reccnt = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.reccnt, g_i, g_p);
        return;

    case USERDEFS_ENTERED_NAME:
        if (iSet)
        {
            ud.entered_name = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.entered_name, g_i, g_p);
        return;

    case USERDEFS_SCREEN_TILTING:
        if (iSet)
        {
            ud.screen_tilting = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.screen_tilting, g_i, g_p);
        return;

    case USERDEFS_SHADOWS:
        if (iSet)
        {
            ud.shadows = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.shadows, g_i, g_p);
        return;

    case USERDEFS_FTA_ON:
        if (iSet)
        {
            ud.fta_on = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.fta_on, g_i, g_p);
        return;

    case USERDEFS_EXECUTIONS:
        if (iSet)
        {
            ud.executions = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.executions, g_i, g_p);
        return;

    case USERDEFS_AUTO_RUN:
        if (iSet)
        {
            ud.auto_run = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.auto_run, g_i, g_p);
        return;

    case USERDEFS_COORDS:
        if (iSet)
        {
            ud.coords = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.coords, g_i, g_p);
        return;

    case USERDEFS_TICKRATE:
        if (iSet)
        {
            ud.showfps = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.showfps, g_i, g_p);
        return;

    case USERDEFS_M_COOP:
        if (iSet)
        {
            ud.m_coop = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.m_coop, g_i, g_p);
        return;

    case USERDEFS_COOP:
        if (iSet)
        {
            ud.coop = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.coop, g_i, g_p);
        return;

    case USERDEFS_SCREEN_SIZE:
        if (iSet)
        {
            if (ud.screen_size != lValue)
            {
                ud.screen_size = lValue;
                G_UpdateScreenArea();
            }
            return;
        }
        Gv_SetVar(lVar2, ud.screen_size, g_i, g_p);
        return;

    case USERDEFS_LOCKOUT:
        if (iSet)
        {
            ud.lockout = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.lockout, g_i, g_p);
        return;

    case USERDEFS_CROSSHAIR:
        if (iSet)
        {
            ud.crosshair = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.crosshair, g_i, g_p);
        return;

        /*
    case USERDEFS_WCHOICE:
        if(iSet)
        {
            ud.wchoice = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.wchoice, g_i, g_p);
        return;
        */

    case USERDEFS_PLAYERAI:
        if (iSet)
        {
            ud.playerai = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.playerai, g_i, g_p);
        return;

    case USERDEFS_RESPAWN_MONSTERS:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_RESPAWNMONSTERS, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_RESPAWNMONSTERS), g_i, g_p);
        return;

    case USERDEFS_RESPAWN_ITEMS:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_RESPAWNITEMS, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_RESPAWNITEMS), g_i, g_p);
        return;

    case USERDEFS_RESPAWN_INVENTORY:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_RESPAWNINVENTORY, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_RESPAWNINVENTORY), g_i, g_p);
        return;

    case USERDEFS_RECSTAT:
        if (iSet)
        {
            ud.recstat = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.recstat, g_i, g_p);
        return;

    case USERDEFS_MONSTERS_OFF:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_NOMONSTERS, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_NOMONSTERS), g_i, g_p);
        return;

    case USERDEFS_BRIGHTNESS:
        if (iSet)
        {
            ud.brightness = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.brightness, g_i, g_p);
        return;

    case USERDEFS_M_RESPAWN_ITEMS:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_RESPAWNITEMS, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_RESPAWNITEMS), g_i, g_p);
        return;

    case USERDEFS_M_RESPAWN_MONSTERS:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_RESPAWNMONSTERS, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_RESPAWNMONSTERS), g_i, g_p);
        return;

    case USERDEFS_M_RESPAWN_INVENTORY:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_RESPAWNINVENTORY, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_RESPAWNINVENTORY), g_i, g_p);
        return;

    case USERDEFS_M_RECSTAT:
        if (iSet)
        {
            ud.m_recstat = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.m_recstat, g_i, g_p);
        return;

    case USERDEFS_M_MONSTERS_OFF:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_NOMONSTERS, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_NOMONSTERS), g_i, g_p);
        return;

    case USERDEFS_DETAIL:
        if (iSet)
        {
            ud.detail = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.detail, g_i, g_p);
        return;

    case USERDEFS_M_FFIRE:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_FRIENDLYFIRE, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_FRIENDLYFIRE), g_i, g_p);
        return;

    case USERDEFS_FFIRE:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_FRIENDLYFIRE, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_FRIENDLYFIRE), g_i, g_p);
        return;

    case USERDEFS_M_PLAYER_SKILL:
        if (iSet)
        {
            ud.m_player_skill = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.m_player_skill, g_i, g_p);
        return;

    case USERDEFS_M_LEVEL_NUMBER:
        if (iSet)
        {
            ud.m_level_number = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.m_level_number, g_i, g_p);
        return;

    case USERDEFS_M_VOLUME_NUMBER:
        if (iSet)
        {
            ud.m_volume_number = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.m_volume_number, g_i, g_p);
        return;

    case USERDEFS_MULTIMODE:
        if (iSet)
        {
            ud.multimode = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.multimode, g_i, g_p);
        return;

    case USERDEFS_PLAYER_SKILL:
        if (iSet)
        {
            ud.player_skill = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.player_skill, g_i, g_p);
        return;

    case USERDEFS_LEVEL_NUMBER:
        if (iSet)
        {
            ud.level_number = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.level_number, g_i, g_p);
        return;

    case USERDEFS_VOLUME_NUMBER:
        if (iSet)
        {
            ud.volume_number = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.volume_number, g_i, g_p);
        return;

    case USERDEFS_M_MARKER:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_MARKERS, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_MARKERS), g_i, g_p);
        return;

    case USERDEFS_MARKER:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_MARKERS, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_MARKERS), g_i, g_p);
        return;

    case USERDEFS_MOUSEFLIP:
        if (iSet)
        {
            ud.mouseflip = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.mouseflip, g_i, g_p);
        return;

    case USERDEFS_STATUSBARSCALE:
        if (iSet)
        {
            ud.statusbarscale = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.statusbarscale, g_i, g_p);
        return;

    case USERDEFS_DRAWWEAPON:
        if (iSet)
        {
            ud.drawweapon = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.drawweapon, g_i, g_p);
        return;

    case USERDEFS_MOUSEAIMING:
        if (iSet)
        {
            ud.mouseaiming = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.mouseaiming, g_i, g_p);
        return;

    case USERDEFS_WEAPONSWITCH:
        if (iSet)
        {
            ud.weaponswitch = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.weaponswitch, g_i, g_p);
        return;

    case USERDEFS_DEMOCAMS:
        if (iSet)
        {
            ud.democams = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.democams, g_i, g_p);
        return;

    case USERDEFS_COLOR:
        if (iSet)
        {
            ud.color = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.color, g_i, g_p);
        return;

    case USERDEFS_MSGDISPTIME:
        if (iSet)
        {
            ud.msgdisptime = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.msgdisptime, g_i, g_p);
        return;

    case USERDEFS_STATUSBARMODE:
        if (iSet)
        {
            ud.statusbarmode = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.statusbarmode, g_i, g_p);
        return;

    case USERDEFS_M_NOEXITS:
        if (iSet)
        {
            M_DMFLAGS_SET(DMFLAG_NOEXITS, lValue);
            return;
        }
        Gv_SetVar(lVar2, M_DMFLAGS_TEST(DMFLAG_NOEXITS), g_i, g_p);
        return;

    case USERDEFS_NOEXITS:
        if (iSet)
        {
            DMFLAGS_SET(DMFLAG_NOEXITS, lValue);
            return;
        }
        Gv_SetVar(lVar2, DMFLAGS_TEST(DMFLAG_NOEXITS), g_i, g_p);
        return;

    case USERDEFS_AUTOVOTE:
        if (iSet)
        {
            ud.autovote = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.autovote, g_i, g_p);
        return;

    case USERDEFS_AUTOMSG:
        if (iSet)
        {
            ud.automsg = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.automsg, g_i, g_p);
        return;

    case USERDEFS_IDPLAYERS:
        if (iSet)
        {
            ud.idplayers = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.idplayers, g_i, g_p);
        return;

    case USERDEFS_TEAM:
        if (iSet)
        {
            ud.team = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.team, g_i, g_p);
        return;

    case USERDEFS_VIEWBOB:
        if (iSet)
        {
            ud.viewbob = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.viewbob, g_i, g_p);
        return;

    case USERDEFS_WEAPONSWAY:
        if (iSet)
        {
            ud.weaponsway = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.weaponsway, g_i, g_p);
        return;

    case USERDEFS_ANGLEINTERPOLATION:
        if (iSet)
        {
            ud.angleinterpolation = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.angleinterpolation, g_i, g_p);
        return;

    case USERDEFS_OBITUARIES:
        if (iSet)
        {
            ud.obituaries = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.obituaries, g_i, g_p);
        return;

    case USERDEFS_LEVELSTATS:
        if (iSet)
        {
            ud.levelstats = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.levelstats, g_i, g_p);
        return;

    case USERDEFS_CROSSHAIRSCALE:
        if (iSet)
        {
            ud.crosshairscale = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.crosshairscale, g_i, g_p);
        return;

    case USERDEFS_ALTHUD:
        if (iSet)
        {
            ud.althud = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.althud, g_i, g_p);
        return;

    case USERDEFS_DISPLAY_BONUS_SCREEN:
        if (iSet)
        {
            ud.display_bonus_screen = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.display_bonus_screen, g_i, g_p);
        return;

    case USERDEFS_SHOW_LEVEL_TEXT:
        if (iSet)
        {
            ud.show_level_text = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.show_level_text, g_i, g_p);
        return;

    case USERDEFS_WEAPONSCALE:
        if (iSet)
        {
            ud.weaponscale = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.weaponscale, g_i, g_p);
        return;

    case USERDEFS_TEXTSCALE:
        if (iSet)
        {
            ud.textscale = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.textscale, g_i, g_p);
        return;

    case USERDEFS_RETURN:
        if (lParm2 == 0)
        {
            if (iSet)
            {
                Gv_SetVarX(sysVarIDs.RETURN, lValue);
                return;
            }
            Gv_SetVar(lVar2, Gv_GetVarX(sysVarIDs.RETURN), g_i, g_p);
        }
        else
        {
            if (iSet)
            {
                ud.returnvar[lParm2 - 1] = lValue;
                return;
            }
            Gv_SetVar(lVar2, ud.returnvar[lParm2 - 1], g_i, g_p);
        }
        return;

    case USERDEFS_TEAMPAL:
        if (iSet)
        {
            teams[lParm2].palnum = lValue;
            return;
        }
        Gv_SetVar(lVar2, teams[lParm2].palnum, g_i, g_p);
        return;

    case USERDEFS_TEAMFRAGS:
        if (iSet)
        {
            teams[lParm2].frags = lValue;
            return;
        }
        Gv_SetVar(lVar2, teams[lParm2].frags, g_i, g_p);
        return;

    case USERDEFS_TEAMSUICIDES:
        if (iSet)
        {
            teams[lParm2].suicides = lValue;
            return;
        }
        Gv_SetVar(lVar2, teams[lParm2].suicides, g_i, g_p);
        return;

    case USERDEFS_DMFLAGS:
        if (iSet)
        {
            ud.dmflags = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.dmflags, g_i, g_p);
        return;

    case USERDEFS_M_DMFLAGS:
        if (iSet)
        {
            ud.m_dmflags = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.m_dmflags, g_i, g_p);
        return;

    case USERDEFS_DRAW_YXASPECT:
        if (iSet)
        {
            rotatesprite_yxaspect = lValue;
            return;
        }
        Gv_SetVar(lVar2, rotatesprite_yxaspect, g_i, g_p);
        return;

    case USERDEFS_MONSTERS_KILLED:
        if (iSet)
        {
            ud.monsters_killed = lValue;
            return;
        }
        Gv_SetVar(lVar2, ud.monsters_killed, g_i, g_p);
        return;

    default:
        return;
    }
}

void __fastcall X_AccessActiveProjectile(int iSet, int lVar1, int lLabelID, int lVar2)
{
    int lValue = 0;
    int proj = g_i;

    if (lVar1 != sysVarIDs.THISACTOR)
        proj=Gv_GetVar(lVar1, g_i, g_p);

    if ((proj < 0 || proj >= MAXSPRITES) && g_scriptSanityChecks)
    {
//        OSD_Printf("X_AccessActiveProjectile(): invalid projectile (%d)\n",proj);
        OSD_Printf(CON_ERROR "tried to %s %s on invalid target projectile (%d) %d %d from %s\n",g_errorLineNum,VM_GetKeywordForID(g_tw),
                   iSet?"set":"get",ProjectileLabels[lLabelID].name,proj,g_i,TrackerCast(g_sp->picnum),
                   (lVar1<MAXGAMEVARS)?aGameVars[lVar1].szLabel:"extended");
        insptr += (lVar2 == MAXGAMEVARS);
        return;
    }

    if (iSet)
        lValue=Gv_GetVar(lVar2, g_i, g_p);

    switch (lLabelID)
    {
    case PROJ_WORKSLIKE:
        if (iSet)
        {
            actor[proj].projectile.workslike=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.workslike, g_i, g_p);
        return;

    case PROJ_SPAWNS:
        if (iSet)
        {
            actor[proj].projectile.spawns=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.spawns, g_i, g_p);
        return;

    case PROJ_SXREPEAT:
        if (iSet)
        {
            actor[proj].projectile.sxrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.sxrepeat, g_i, g_p);
        return;

    case PROJ_SYREPEAT:
        if (iSet)
        {
            actor[proj].projectile.syrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.syrepeat, g_i, g_p);
        return;

    case PROJ_SOUND:
        if (iSet)
        {
            actor[proj].projectile.sound=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.sound, g_i, g_p);
        return;

    case PROJ_ISOUND:
        if (iSet)
        {
            actor[proj].projectile.isound=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.isound, g_i, g_p);
        return;

    case PROJ_VEL:
        if (iSet)
        {
            actor[proj].projectile.vel=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.vel, g_i, g_p);
        return;

    case PROJ_EXTRA:
        if (iSet)
        {
            actor[proj].projectile.extra=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.extra, g_i, g_p);
        return;

    case PROJ_DECAL:
        if (iSet)
        {
            actor[proj].projectile.decal=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.decal, g_i, g_p);
        return;

    case PROJ_TRAIL:
        if (iSet)
        {
            actor[proj].projectile.trail=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.trail, g_i, g_p);
        return;

    case PROJ_TXREPEAT:
        if (iSet)
        {
            actor[proj].projectile.txrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.txrepeat, g_i, g_p);
        return;

    case PROJ_TYREPEAT:
        if (iSet)
        {
            actor[proj].projectile.tyrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.tyrepeat, g_i, g_p);
        return;

    case PROJ_TOFFSET:
        if (iSet)
        {
            actor[proj].projectile.toffset=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.toffset, g_i, g_p);
        return;

    case PROJ_TNUM:
        if (iSet)
        {
            actor[proj].projectile.tnum=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.tnum, g_i, g_p);
        return;

    case PROJ_DROP:
        if (iSet)
        {
            actor[proj].projectile.drop=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.drop, g_i, g_p);
        return;

    case PROJ_CSTAT:
        if (iSet)
        {
            actor[proj].projectile.cstat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.cstat, g_i, g_p);
        return;

    case PROJ_CLIPDIST:
        if (iSet)
        {
            actor[proj].projectile.clipdist=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.clipdist, g_i, g_p);
        return;

    case PROJ_SHADE:
        if (iSet)
        {
            actor[proj].projectile.shade=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.shade, g_i, g_p);
        return;

    case PROJ_XREPEAT:
        if (iSet)
        {
            actor[proj].projectile.xrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.xrepeat, g_i, g_p);
        return;

    case PROJ_YREPEAT:
        if (iSet)
        {
            actor[proj].projectile.yrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.yrepeat, g_i, g_p);
        return;

    case PROJ_PAL:
        if (iSet)
        {
            actor[proj].projectile.pal=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.pal, g_i, g_p);
        return;

    case PROJ_EXTRA_RAND:
        if (iSet)
        {
            actor[proj].projectile.extra_rand=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.extra_rand, g_i, g_p);
        return;

    case PROJ_HITRADIUS:
        if (iSet)
        {
            actor[proj].projectile.hitradius=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.hitradius, g_i, g_p);
        return;

    case PROJ_MOVECNT:
        if (iSet)
        {
            actor[proj].projectile.movecnt=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.movecnt, g_i, g_p);
        return;

    case PROJ_OFFSET:
        if (iSet)
        {
            actor[proj].projectile.offset=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.offset, g_i, g_p);
        return;

    case PROJ_BOUNCES:
        if (iSet)
        {
            actor[proj].projectile.bounces=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.bounces, g_i, g_p);
        return;

    case PROJ_BSOUND:
        if (iSet)
        {
            actor[proj].projectile.bsound=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.bsound, g_i, g_p);
        return;

    case PROJ_RANGE:
        if (iSet)
        {
            actor[proj].projectile.range=lValue;
            return;
        }
        Gv_SetVar(lVar2, actor[proj].projectile.range, g_i, g_p);
        return;

    default:
        return;
    }
}

int32_t __fastcall VM_GetPlayer(int const playerNum, int32_t labelNum, int const lParm2)
{
    auto& thisPlayer = g_player[playerNum];
    auto& ps = *thisPlayer.ps;
    auto const& playerLabel = PlayerLabels[labelNum];

    switch (playerLabel.lId)
    {
        case PLAYER_ZOOM:               return ps.zoom;

        case PLAYER_EXITX:              return ps.exitx;
        case PLAYER_EXITY:              return ps.exity;

        case PLAYER_LOOGIEX:            return ps.loogiex[lParm2];
        case PLAYER_LOOGIEY:            return ps.loogiey[lParm2];
        case PLAYER_NUMLOOGS:           return ps.numloogs;
        case PLAYER_LOOGCNT:            return ps.loogcnt;

        case PLAYER_POSX:               return ps.pos.x;
        case PLAYER_POSY:               return ps.pos.y;
        case PLAYER_POSZ:               return ps.pos.z;

        case PLAYER_HORIZ:              return fix16_to_int(ps.q16horiz);
        case PLAYER_OHORIZ:             return fix16_to_int(ps.oq16horiz);
        case PLAYER_OHORIZOFF:          return fix16_to_int(ps.oq16horizoff);

        case PLAYER_INVDISPTIME:        return ps.invdisptime;

        case PLAYER_BOBPOSX:            return ps.bobposx;
        case PLAYER_BOBPOSY:            return ps.bobposy;

        case PLAYER_OPOSX:              return ps.opos.x;
        case PLAYER_OPOSY:              return ps.opos.y;
        case PLAYER_OPOSZ:              return ps.opos.z;

        case PLAYER_PYOFF:              return ps.pyoff;
        case PLAYER_OPYOFF:             return ps.opyoff;

        case PLAYER_POSXV:              return ps.vel.x;
        case PLAYER_POSYV:              return ps.vel.y;
        case PLAYER_POSZV:              return ps.vel.z;

        case PLAYER_LAST_PISSED_TIME:   return ps.last_pissed_time;

        case PLAYER_TRUEFZ:             return ps.truefz;
        case PLAYER_TRUECZ:             return ps.truecz;

        case PLAYER_PLAYER_PAR:         return ps.player_par;
        case PLAYER_VISIBILITY:         return thisPlayer.display.visibility;
        case PLAYER_BOBCOUNTER:         return ps.bobcounter;
        case PLAYER_WEAPON_SWAY:        return ps.weapon_sway;
        case PLAYER_PALS_TIME:          return ps.pals.f;
        case PLAYER_RANDOMFLAMEX:       return ps.randomflamex;
        case PLAYER_CRACK_TIME:         return ps.crack_time;
        case PLAYER_AIM_MODE:           return ps.aim_mode;

        case PLAYER_ANG:                return fix16_to_int(ps.q16ang);
        case PLAYER_OANG:               return fix16_to_int(ps.oq16ang);
        case PLAYER_ANGVEL:             return fix16_to_int(ps.q16angvel);

        case PLAYER_CURSECTNUM:         return ps.cursectnum;
        case PLAYER_LOOK_ANG:           return ps.look_ang;

        case PLAYER_LAST_EXTRA:         return ps.last_extra;
        case PLAYER_SUBWEAPON:          return ps.subweapon;
        case PLAYER_AMMO_AMOUNT:        return ps.ammo_amount[lParm2];
        case PLAYER_WACKEDBYACTOR:      return ps.wackedbyactor;
        case PLAYER_FRAG:               return ps.frag;
        case PLAYER_FRAGGEDSELF:        return ps.fraggedself;

        case PLAYER_CURR_WEAPON:        return ps.curr_weapon;
        case PLAYER_LAST_WEAPON:        return ps.last_weapon;

        case PLAYER_TIPINCS:            return ps.tipincs;
        case PLAYER_HORIZOFF:           return fix16_to_int(ps.q16horizoff);

        case PLAYER_WANTWEAPONFIRE:         return ps.wantweaponfire;
        case PLAYER_HOLODUKE_AMOUNT:        return ps.inv_amount[GET_HOLODUKE];
        case PLAYER_NEWOWNER:               return ps.newowner;
        case PLAYER_HURT_DELAY:             return ps.hurt_delay;
        case PLAYER_HBOMB_HOLD_DELAY:       return ps.hbomb_hold_delay;
        case PLAYER_JUMPING_COUNTER:        return ps.jumping_counter;
        case PLAYER_AIRLEFT:                return ps.airleft;
        case PLAYER_KNEE_INCS:              return ps.knee_incs;
        case PLAYER_ACCESS_INCS:            return ps.access_incs;
        case PLAYER_FTA:                    return ps.fta;
        case PLAYER_FTQ:                    return ps.ftq;
        case PLAYER_ACCESS_WALLNUM:         return ps.access_wallnum;
        case PLAYER_ACCESS_SPRITENUM:       return ps.access_spritenum;
        case PLAYER_KICKBACK_PIC:           return ps.kickback_pic;
        case PLAYER_GOT_ACCESS:             return ud.got_access; // MAKE ALIAS IN UD!
        case PLAYER_WEAPON_ANG:             return ps.weapon_ang;
        case PLAYER_FIRSTAID_AMOUNT:        return ps.inv_amount[GET_FIRSTAID];
        case PLAYER_SOMETHINGONPLAYER:      return ps.somethingonplayer;
        case PLAYER_ON_CRANE:               return ps.on_crane;
        case PLAYER_I:                      return ps.i;
        case PLAYER_ONE_PARALLAX_SECTNUM:   return ps.one_parallax_sectnum;
        case PLAYER_OVER_SHOULDER_ON:       return ps.over_shoulder_on;
        case PLAYER_RANDOM_CLUB_FRAME:      return ps.random_club_frame;
        case PLAYER_FIST_INCS:              return ps.fist_incs;
        case PLAYER_ONE_EIGHTY_COUNT:       return ps.one_eighty_count;
        case PLAYER_CHEAT_PHASE:            return ps.cheat_phase;
        case PLAYER_DUMMYPLAYERSPRITE:      return ps.dummyplayersprite;
        case PLAYER_EXTRA_EXTRA8:           return ps.extra_extra8;
        case PLAYER_QUICK_KICK:             return ps.quick_kick;
        case PLAYER_HEAT_AMOUNT:            return ps.inv_amount[GET_HEATS];
        case PLAYER_ACTORSQU:               return ps.actorsqu;
        case PLAYER_TIMEBEFOREEXIT:         return ps.timebeforeexit;
        case PLAYER_CUSTOMEXITSOUND:        return ps.customexitsound;
        case PLAYER_WEAPRECS:               return ps.weaprecs[lParm2];
        case PLAYER_WEAPRECCNT:             return ps.weapreccnt;
        case PLAYER_INTERFACE_TOGGLE_FLAG:  return ps.interface_toggle_flag;
        case PLAYER_ROTSCRNANG:             return ps.rotscrnang;
        case PLAYER_OROTSCRNANG:            return ps.orotscrnang;
        case PLAYER_DEAD_FLAG:              return ps.dead_flag;
        case PLAYER_SHOW_EMPTY_WEAPON:      return ps.show_empty_weapon;
        case PLAYER_SCUBA_AMOUNT:           return ps.inv_amount[GET_SCUBA];
        case PLAYER_JETPACK_AMOUNT:         return ps.inv_amount[GET_JETPACK];
        case PLAYER_STEROIDS_AMOUNT:        return ps.inv_amount[GET_STEROIDS];
        case PLAYER_SHIELD_AMOUNT:          return ps.inv_amount[GET_SHIELD];
        case PLAYER_HOLODUKE_ON:            return ps.holoduke_on;
        case PLAYER_PYCOUNT:                return ps.pycount;
        case PLAYER_WEAPON_POS:             return ps.weapon_pos;
        case PLAYER_FRAG_PS:                return ps.frag_ps;
        case PLAYER_TRANSPORTER_HOLD:       return ps.transporter_hold;
        case PLAYER_LAST_FULL_WEAPON:       return ps.last_full_weapon;
        case PLAYER_FOOTPRINTSHADE:         return ps.footprintshade;
        case PLAYER_BOOT_AMOUNT:            return ps.inv_amount[GET_BOOTS];
        case PLAYER_SCREAM_VOICE:           return ps.scream_voice;
        case PLAYER_GM:                     return ps.gm;
        case PLAYER_ON_WARPING_SECTOR:      return ps.on_warping_sector;
        case PLAYER_FOOTPRINTCOUNT:         return ps.footprintcount;
        case PLAYER_HBOMB_ON:               return ps.hbomb_on;
        case PLAYER_JUMPING_TOGGLE:         return ps.jumping_toggle;
        case PLAYER_RAPID_FIRE_HOLD:        return ps.rapid_fire_hold;
        case PLAYER_ON_GROUND:              return ps.on_ground;
        case PLAYER_INVEN_ICON:             return ps.inven_icon;
        case PLAYER_BUTTONPALETTE:          return ps.buttonpalette;
        case PLAYER_JETPACK_ON:             return ps.jetpack_on;
        case PLAYER_SPRITEBRIDGE:           return ps.spritebridge;
        case PLAYER_LASTRANDOMSPOT:         return ps.lastrandomspot;
        case PLAYER_SCUBA_ON:               return ps.scuba_on;
        case PLAYER_FOOTPRINTPAL:           return ps.footprintpal;

        case PLAYER_HEAT_ON:                return ps.heat_on;

        case PLAYER_HOLSTER_WEAPON:         return ps.holster_weapon;
        case PLAYER_FALLING_COUNTER:        return ps.falling_counter;
        case PLAYER_GOTWEAPON:              return ps.gotweapon[lParm2];
        case PLAYER_TOGGLE_KEY_FLAG:        return ps.toggle_key_flag;
        case PLAYER_KNUCKLE_INCS:           return ps.knuckle_incs;
        case PLAYER_WALKING_SND_TOGGLE:     return ps.walking_snd_toggle;
        case PLAYER_PALOOKUP:               return ps.palookup;
        case PLAYER_HARD_LANDING:           return ps.hard_landing;
        case PLAYER_MAX_SECRET_ROOMS:       return ud.max_secret_rooms; // DEPRECATE & MAKE ALIAS IN UD!
        case PLAYER_SECRET_ROOMS:           return ud.secret_rooms;     // DEPRECATE & MAKE ALIAS IN UD!

        case PLAYER_PALS:
        {
            switch (lParm2)
            {
                case 0: return ps.pals.r;
                case 1: return ps.pals.g;
                case 2: return ps.pals.b;
            }
            return 0;
        }

        case PLAYER_MAX_ACTORS_KILLED:      return ud.total_monsters; // DEPRECATE & MAKE ALIAS IN UD!
        case PLAYER_ACTORS_KILLED:          return ps.actors_killed;

        case PLAYER_RETURN_TO_CENTER:       return ps.return_to_center;
        case PLAYER_RUNSPEED:               return ps.runspeed;
        case PLAYER_SBS:                    return ps.sbs;
        case PLAYER_RELOADING:              return ps.reloading;
        case PLAYER_AUTO_AIM:               return ps.auto_aim;
        case PLAYER_MOVEMENT_LOCK:          return ps.movement_lock;

        case PLAYER_SOUND_PITCH:            return ps.sound_pitch;

        case PLAYER_WEAPONSWITCH:           return ps.weaponswitch;

        case PLAYER_TEAM:                   return ps.team;
        case PLAYER_MAX_PLAYER_HEALTH:      return ps.max_player_health;
        case PLAYER_MAX_SHIELD_AMOUNT:      return ps.max_shield_amount;
        case PLAYER_MAX_AMMO_AMOUNT:        return ps.max_ammo_amount[lParm2];
        case PLAYER_LAST_QUICK_KICK:        return ps.last_quick_kick;
        case PLAYER_CONNECTED:              return thisPlayer.connected; // Read-only.

        case PLAYER_LOWSPRITE:              return ps.lowsprite; // [JM] Lowest sprite touching. Read-only.
        case PLAYER_HIGHSPRITE:             return ps.highsprite; // [JM] Lowest sprite touching. // Read-only.

        default: EDUKE32_UNREACHABLE_SECTION(return -1;);
    }
}

void __fastcall VM_SetPlayer(int const playerNum, int const labelNum, int const lParm2, int32_t const newValue)
{
    auto const& playerLabel = PlayerLabels[labelNum];

    auto& thisPlayer = g_player[playerNum];
    auto& ps = *thisPlayer.ps;

    if ((thisPlayer.ps == NULL && playerLabel.lId != PLAYER_CONNECTED) && g_scriptSanityChecks)
    {
        CON_ERRPRINTF("tried to access NULL player! %d\n", playerNum);
        return;
    }

    switch (playerLabel.lId)
    {
        case PLAYER_ZOOM:               ps.zoom                 = newValue; return;

        case PLAYER_EXITX:              ps.exitx                = newValue; return;
        case PLAYER_EXITY:              ps.exity                = newValue; return;

        case PLAYER_LOOGIEX:            ps.loogiex[lParm2]      = newValue; return;
        case PLAYER_LOOGIEY:            ps.loogiey[lParm2]      = newValue; return;
        case PLAYER_NUMLOOGS:           ps.numloogs             = newValue; return;
        case PLAYER_LOOGCNT:            ps.loogcnt              = newValue; return;

        case PLAYER_POSX:               ps.pos.x                = newValue; return;
        case PLAYER_POSY:               ps.pos.y                = newValue; return;
        case PLAYER_POSZ:               ps.pos.z                = newValue; return;

        case PLAYER_HORIZ:              ps.q16horiz             = fix16_from_int(newValue); return;
        case PLAYER_OHORIZ:             ps.oq16horiz            = fix16_from_int(newValue); return;
        case PLAYER_OHORIZOFF:          ps.oq16horizoff         = fix16_from_int(newValue); return;

        case PLAYER_INVDISPTIME:        ps.invdisptime          = newValue; return;

        case PLAYER_BOBPOSX:            ps.bobposx              = newValue; return;
        case PLAYER_BOBPOSY:            ps.bobposy              = newValue; return;

        case PLAYER_OPOSX:              ps.opos.x               = newValue; return;
        case PLAYER_OPOSY:              ps.opos.y               = newValue; return;
        case PLAYER_OPOSZ:              ps.opos.z               = newValue; return;

        case PLAYER_PYOFF:              ps.pyoff                = newValue; return;
        case PLAYER_OPYOFF:             ps.opyoff               = newValue; return;

        case PLAYER_POSXV:              ps.vel.x                = newValue; return;
        case PLAYER_POSYV:              ps.vel.y                = newValue; return;
        case PLAYER_POSZV:              ps.vel.z                = newValue; return;

        case PLAYER_LAST_PISSED_TIME:   ps.last_pissed_time     = newValue; return;

        case PLAYER_TRUEFZ:             ps.truefz               = newValue; return;
        case PLAYER_TRUECZ:             ps.truecz               = newValue; return;

        case PLAYER_PLAYER_PAR:         ps.player_par           = newValue; return;
        case PLAYER_VISIBILITY:         thisPlayer.display.visibility = newValue; return;
        case PLAYER_BOBCOUNTER:         ps.bobcounter           = newValue; return;
        case PLAYER_WEAPON_SWAY:        ps.weapon_sway          = newValue; return;
        case PLAYER_PALS_TIME:          ps.pals.f               = newValue; return;
        case PLAYER_RANDOMFLAMEX:       ps.randomflamex         = newValue; return;
        case PLAYER_CRACK_TIME:         ps.crack_time           = newValue; return;
        case PLAYER_AIM_MODE:           ps.aim_mode             = newValue; return;

        case PLAYER_ANG:                ps.q16ang               = fix16_from_int(newValue); return;
        case PLAYER_OANG:               ps.oq16ang              = fix16_from_int(newValue); return;
        case PLAYER_ANGVEL:             ps.q16angvel            = fix16_from_int(newValue); return;

        case PLAYER_CURSECTNUM:         ps.cursectnum           = newValue; return;
        case PLAYER_LOOK_ANG:           ps.look_ang             = newValue; return;
        case PLAYER_LAST_EXTRA:         ps.last_extra           = newValue; return;
        case PLAYER_SUBWEAPON:          ps.subweapon            = newValue; return;
        case PLAYER_AMMO_AMOUNT:        ps.ammo_amount[lParm2]  = newValue; return;
        case PLAYER_WACKEDBYACTOR:      ps.wackedbyactor        = newValue; return;
        case PLAYER_FRAG:               ps.frag                 = newValue; return;
        case PLAYER_FRAGGEDSELF:        ps.fraggedself          = newValue; return;

        case PLAYER_CURR_WEAPON:
        {
            ps.curr_weapon = newValue;
            Gv_SetVar(sysVarIDs.WEAPON, ps.curr_weapon, g_i, playerNum);
            return;
        }
        case PLAYER_LAST_WEAPON:            ps.last_weapon              = newValue; return;

        case PLAYER_TIPINCS:                ps.tipincs                  = newValue; return;
        case PLAYER_HORIZOFF:               ps.q16horizoff              = fix16_from_int(newValue); return;

        case PLAYER_WANTWEAPONFIRE:         ps.wantweaponfire           = newValue; return;
        case PLAYER_HOLODUKE_AMOUNT:        ps.inv_amount[GET_HOLODUKE] = newValue; return;
        case PLAYER_NEWOWNER:               ps.newowner                 = newValue; return;
        case PLAYER_HURT_DELAY:             ps.hurt_delay               = newValue; return;
        case PLAYER_HBOMB_HOLD_DELAY:       ps.hbomb_hold_delay         = newValue; return;
        case PLAYER_JUMPING_COUNTER:        ps.jumping_counter          = newValue; return;
        case PLAYER_AIRLEFT:                ps.airleft                  = newValue; return;
        case PLAYER_KNEE_INCS:              ps.knee_incs                = newValue; return;
        case PLAYER_ACCESS_INCS:            ps.access_incs              = newValue; return;
        case PLAYER_FTA:                    ps.fta                      = newValue; return;
        case PLAYER_FTQ:                    ps.ftq                      = newValue; return;
        case PLAYER_ACCESS_WALLNUM:         ps.access_wallnum           = newValue; return;
        case PLAYER_ACCESS_SPRITENUM:       ps.access_spritenum         = newValue; return;
        case PLAYER_KICKBACK_PIC:           ps.kickback_pic             = newValue; return;
        case PLAYER_GOT_ACCESS:             ud.got_access               = newValue; return; // SHOULD MAKE ALIAS IN UD LABELS!
        case PLAYER_WEAPON_ANG:             ps.weapon_ang               = newValue; return;
        case PLAYER_FIRSTAID_AMOUNT:        ps.inv_amount[GET_FIRSTAID] = newValue; return;
        case PLAYER_SOMETHINGONPLAYER:      ps.somethingonplayer        = newValue; return;
        case PLAYER_ON_CRANE:               ps.on_crane                 = newValue; return;
        case PLAYER_I:                      ps.i                        = newValue; return;
        case PLAYER_ONE_PARALLAX_SECTNUM:   ps.one_parallax_sectnum     = newValue; return;
        case PLAYER_OVER_SHOULDER_ON:       ps.over_shoulder_on         = newValue; return;
        case PLAYER_RANDOM_CLUB_FRAME:      ps.random_club_frame        = newValue; return;
        case PLAYER_FIST_INCS:              ps.fist_incs                = newValue; return;
        case PLAYER_ONE_EIGHTY_COUNT:       ps.one_eighty_count         = newValue; return;
        case PLAYER_CHEAT_PHASE:            ps.cheat_phase              = newValue; return;
        case PLAYER_DUMMYPLAYERSPRITE:      ps.dummyplayersprite        = newValue; return;
        case PLAYER_EXTRA_EXTRA8:           ps.extra_extra8             = newValue; return;
        case PLAYER_QUICK_KICK:             ps.quick_kick               = newValue; return;
        case PLAYER_HEAT_AMOUNT:            ps.inv_amount[GET_HEATS]    = newValue; return;
        case PLAYER_ACTORSQU:               ps.actorsqu                 = newValue; return;
        case PLAYER_TIMEBEFOREEXIT:         ps.timebeforeexit           = newValue; return;
        case PLAYER_CUSTOMEXITSOUND:        ps.customexitsound          = newValue; return;
        case PLAYER_WEAPRECS:               ps.weaprecs[lParm2]         = newValue; return;
        case PLAYER_WEAPRECCNT:             ps.weapreccnt               = newValue; return;
        case PLAYER_INTERFACE_TOGGLE_FLAG:  ps.interface_toggle_flag    = newValue; return;
        case PLAYER_ROTSCRNANG:             ps.rotscrnang               = newValue; return;
        case PLAYER_OROTSCRNANG:            ps.orotscrnang              = newValue; return;
        case PLAYER_DEAD_FLAG:              ps.dead_flag                = newValue; return;
        case PLAYER_SHOW_EMPTY_WEAPON:      ps.show_empty_weapon        = newValue; return;
        case PLAYER_SCUBA_AMOUNT:           ps.inv_amount[GET_SCUBA]    = newValue; return;
        case PLAYER_JETPACK_AMOUNT:         ps.inv_amount[GET_JETPACK]  = newValue; return;
        case PLAYER_STEROIDS_AMOUNT:        ps.inv_amount[GET_STEROIDS] = newValue; return;
        case PLAYER_SHIELD_AMOUNT:          ps.inv_amount[GET_SHIELD]   = newValue; return;
        case PLAYER_HOLODUKE_ON:            ps.holoduke_on              = newValue; return;
        case PLAYER_PYCOUNT:                ps.pycount                  = newValue; return;
        case PLAYER_WEAPON_POS:             ps.weapon_pos               = newValue; return;
        case PLAYER_FRAG_PS:                ps.frag_ps                  = newValue; return;
        case PLAYER_TRANSPORTER_HOLD:       ps.transporter_hold         = newValue; return;
        case PLAYER_LAST_FULL_WEAPON:       ps.last_full_weapon         = newValue; return;
        case PLAYER_FOOTPRINTSHADE:         ps.footprintshade           = newValue; return;
        case PLAYER_BOOT_AMOUNT:            ps.inv_amount[GET_BOOTS]    = newValue; return;
        case PLAYER_SCREAM_VOICE:           ps.scream_voice             = newValue; return;
        case PLAYER_GM:                     ps.gm                       = newValue; return;
        case PLAYER_ON_WARPING_SECTOR:      ps.on_warping_sector        = newValue; return;
        case PLAYER_FOOTPRINTCOUNT:         ps.footprintcount           = newValue; return;
        case PLAYER_HBOMB_ON:               ps.hbomb_on                 = newValue; return;
        case PLAYER_JUMPING_TOGGLE:         ps.jumping_toggle           = newValue; return;
        case PLAYER_RAPID_FIRE_HOLD:        ps.rapid_fire_hold          = newValue; return;
        case PLAYER_ON_GROUND:              ps.on_ground                = newValue; return;
        case PLAYER_INVEN_ICON:             ps.inven_icon               = newValue; return;
        case PLAYER_BUTTONPALETTE:          ps.buttonpalette            = newValue; return;
        case PLAYER_JETPACK_ON:             ps.jetpack_on               = newValue; return;
        case PLAYER_SPRITEBRIDGE:           ps.spritebridge             = newValue; return;
        case PLAYER_LASTRANDOMSPOT:         ps.lastrandomspot           = newValue; return;
        case PLAYER_SCUBA_ON:               ps.scuba_on                 = newValue; return;
        case PLAYER_FOOTPRINTPAL:           ps.footprintpal             = newValue; return;

        case PLAYER_HEAT_ON:
        {
            if (ps.heat_on != newValue)
            {
                ps.heat_on = newValue;
                P_UpdateScreenPal(&ps);
            }
            return;
        }

        case PLAYER_HOLSTER_WEAPON:         ps.holster_weapon           = newValue; return;
        case PLAYER_FALLING_COUNTER:        ps.falling_counter          = newValue; return;
        case PLAYER_GOTWEAPON:              ps.gotweapon[lParm2]        = newValue; return;
        case PLAYER_TOGGLE_KEY_FLAG:        ps.toggle_key_flag          = newValue; return;
        case PLAYER_KNUCKLE_INCS:           ps.knuckle_incs             = newValue; return;
        case PLAYER_WALKING_SND_TOGGLE:     ps.walking_snd_toggle       = newValue; return;
        case PLAYER_PALOOKUP:               ps.palookup                 = newValue; return;
        case PLAYER_HARD_LANDING:           ps.hard_landing             = newValue; return;
        case PLAYER_MAX_SECRET_ROOMS:       ud.max_secret_rooms         = newValue; return; // DEPRECATE & MAKE ALIAS IN UD!
        case PLAYER_SECRET_ROOMS:           ud.secret_rooms             = newValue; return; // DEPRECATE & MAKE ALIAS IN UD!

        case PLAYER_PALS:
        {
            switch (lParm2)
            {
                case 0: ps.pals.r = newValue; break;
                case 1: ps.pals.g = newValue; break;
                case 2: ps.pals.b = newValue; break;
            }
            return;
        }

        case PLAYER_MAX_ACTORS_KILLED:      ud.total_monsters           = newValue; return; // DEPRECATE & MAKE ALIAS IN UD!
        case PLAYER_ACTORS_KILLED:          ps.actors_killed            = newValue; return;

        case PLAYER_RETURN_TO_CENTER:       ps.return_to_center         = newValue; return;
        case PLAYER_RUNSPEED:               ps.runspeed                 = newValue; return;
        case PLAYER_SBS:                    ps.sbs                      = newValue; return;
        case PLAYER_RELOADING:              ps.reloading                = newValue; return;
        case PLAYER_AUTO_AIM:               ps.auto_aim                 = newValue; return;
        case PLAYER_MOVEMENT_LOCK:          ps.movement_lock            = newValue; return;

        case PLAYER_SOUND_PITCH:
            ps.sound_pitch = newValue;
            if (playerNum == screenpeek)
                S_ChangeAllSoundPitches(newValue);
            return;

        case PLAYER_WEAPONSWITCH:           ps.weaponswitch             = newValue; return;

        case PLAYER_TEAM:                   ps.team                     = newValue; return;
        case PLAYER_MAX_PLAYER_HEALTH:      ps.max_player_health        = newValue; return;
        case PLAYER_MAX_SHIELD_AMOUNT:      ps.max_shield_amount        = newValue; return;
        case PLAYER_MAX_AMMO_AMOUNT:        ps.max_ammo_amount[lParm2]  = newValue; return;
        case PLAYER_LAST_QUICK_KICK:        ps.last_quick_kick          = newValue; return;
        case PLAYER_CONNECTED:              return; // Read-only.

        case PLAYER_LOWSPRITE:              return; // [JM] Lowest sprite touching. Read-only.
        case PLAYER_HIGHSPRITE:             return; // [JM] Lowest sprite touching. // Read-only.

        default: EDUKE32_UNREACHABLE_SECTION(return;);
    }
}

void __fastcall X_AccessPlayerInput(int iSet, int lVar1, int lLabelID, int lVar2)
{
    int lValue=0;
    int iPlayer=g_p;

    if (lVar1 != sysVarIDs.THISACTOR)
        iPlayer=Gv_GetVar(lVar1, g_i, g_p);

    if (((unsigned)iPlayer >= MAXPLAYERS || g_player[iPlayer].ps == NULL) && g_scriptSanityChecks)
    {
        insptr += (lVar2 == MAXGAMEVARS);
        OSD_Printf(CON_ERROR "invalid target player (%d) %d\n",g_errorLineNum,VM_GetKeywordForID(g_tw),iPlayer,g_i);
        return;
    }

    if (iSet)
        lValue=Gv_GetVar(lVar2, g_i, g_p);

    switch (lLabelID)
    {
    case INPUT_AVEL:
        if (iSet)
        {
            g_player[iPlayer].inputBits->q16avel=fix16_from_int(lValue);
            return;
        }
        Gv_SetVar(lVar2, fix16_to_int(g_player[iPlayer].inputBits->q16avel), g_i, g_p);
        return;

    case INPUT_HORZ:
        if (iSet)
        {
            g_player[iPlayer].inputBits->q16horz=fix16_from_int(lValue);
            return;
        }
        Gv_SetVar(lVar2, fix16_to_int(g_player[iPlayer].inputBits->q16horz), g_i, g_p);
        return;

    case INPUT_FVEL:
        if (iSet)
        {
            g_player[iPlayer].inputBits->fvel=lValue;
            return;
        }
        Gv_SetVar(lVar2, g_player[iPlayer].inputBits->fvel, g_i, g_p);
        return;

    case INPUT_SVEL:
        if (iSet)
        {
            g_player[iPlayer].inputBits->svel=lValue;
            return;
        }
        Gv_SetVar(lVar2, g_player[iPlayer].inputBits->svel, g_i, g_p);
        return;

    case INPUT_BITS:
        if (iSet)
        {
            g_player[iPlayer].inputBits->bits=lValue;
            return;
        }
        Gv_SetVar(lVar2, g_player[iPlayer].inputBits->bits, g_i, g_p);
        return;

    case INPUT_EXTBITS:
        if (iSet)
        {
            g_player[iPlayer].inputBits->extbits=lValue;
            return;
        }
        Gv_SetVar(lVar2, g_player[iPlayer].inputBits->extbits, g_i, g_p);
        return;
    default:
        return;
    }
}

void __fastcall X_AccessWall(int iSet, int lVar1, int lLabelID, int lVar2)
{
    int const lValue = (iSet) ? Gv_GetVar(lVar2, g_i, g_p) : 0;
    int iWall = Gv_GetVar(lVar1, g_i, g_p);

    if ((iWall<0 || iWall >= numwalls) && g_scriptSanityChecks)
    {
        insptr += (lVar2 == MAXGAMEVARS);
        OSD_Printf(CON_ERROR "Invalid wall %d\n",g_errorLineNum,VM_GetKeywordForID(g_tw),iWall);
        return;
    }

    switch (lLabelID)
    {
    case WALL_X:
        if (iSet)
        {
            wall[iWall].x=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].x, g_i, g_p);
        return;

    case WALL_Y:
        if (iSet)
        {
            wall[iWall].y=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].y, g_i, g_p);
        return;

    case WALL_POINT2:
        if (iSet)
        {
            wall[iWall].point2=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].point2, g_i, g_p);
        return;

    case WALL_NEXTWALL:
        if (iSet)
        {
            wall[iWall].nextwall=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].nextwall, g_i, g_p);
        return;

    case WALL_NEXTSECTOR:
        if (iSet)
        {
            wall[iWall].nextsector=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].nextsector, g_i, g_p);
        return;

    case WALL_CSTAT:
        if (iSet)
        {
            wall[iWall].cstat=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].cstat, g_i, g_p);
        return;

    case WALL_PICNUM:
        if (iSet)
        {
            wall[iWall].picnum=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].picnum, g_i, g_p);
        return;

    case WALL_OVERPICNUM:
        if (iSet)
        {
            wall[iWall].overpicnum=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].overpicnum, g_i, g_p);
        return;

    case WALL_SHADE:
        if (iSet)
        {
            wall[iWall].shade=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].shade, g_i, g_p);
        return;

    case WALL_PAL:
        if (iSet)
        {
            wall[iWall].pal=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].pal, g_i, g_p);
        return;

    case WALL_XREPEAT:
        if (iSet)
        {
            wall[iWall].xrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].xrepeat, g_i, g_p);
        return;

    case WALL_YREPEAT:
        if (iSet)
        {
            wall[iWall].yrepeat=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].yrepeat, g_i, g_p);
        return;

    case WALL_XPANNING:
        if (iSet)
        {
            wall[iWall].xpanning=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].xpanning, g_i, g_p);
        return;

    case WALL_YPANNING:
        if (iSet)
        {
            wall[iWall].ypanning=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].ypanning, g_i, g_p);
        return;

    case WALL_LOTAG:
        if (iSet)
        {
            wall[iWall].lotag=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].lotag, g_i, g_p);
        return;

    case WALL_HITAG:
        if (iSet)
        {
            wall[iWall].hitag=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].hitag, g_i, g_p);
        return;

    case WALL_EXTRA:
        if (iSet)
        {
            wall[iWall].extra=lValue;
            return;
        }
        Gv_SetVar(lVar2, wall[iWall].extra, g_i, g_p);
        return;
    default:
        return;
    }
}

void __fastcall X_AccessSector(int iSet, int iSector, int labelNum, int lVar2)
{
    int const lValue = (iSet) ? Gv_GetVar(lVar2, g_i, g_p) : 0;
    auto const& sectLabel = SectorLabels[labelNum];

    sectortype* const s = &sector[iSector];
    switch (sectLabel.lId)
    {
        case SECTOR_WALLPTR:
            if (iSet)
            {
                s->wallptr = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->wallptr, g_i, g_p);
            return;

        case SECTOR_WALLNUM:
            if (iSet)
            {
                s->wallnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->wallnum, g_i, g_p);
            return;

        case SECTOR_CEILINGZ:
            if (iSet)
            {
                s->ceilingz = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingz, g_i, g_p);
            return;

        case SECTOR_CEILINGZVEL:
        {
            if (iSet)
            {
                s->extra = lValue;
                int const goal = GetAnimationGoal(iSector, SECTANIM_CEILING);
                if (goal != -1)
                    SetAnimation(iSector, SECTANIM_CEILING, goal, s->extra);

                return;
            }
            Gv_SetVar(lVar2, (GetAnimationGoal(iSector, SECTANIM_CEILING) == -1) ? 0 : s->extra, g_i, g_p);
            return;
        }

        case SECTOR_CEILINGZGOAL:
            if (iSet)
            {
                SetAnimation(iSector, SECTANIM_CEILING, lValue, s->extra);
                return;
            }
            Gv_SetVar(lVar2, GetAnimationGoal(iSector, SECTANIM_CEILING), g_i, g_p);
            return;

        case SECTOR_FLOORZ:
            if (iSet)
            {
                s->floorz = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorz, g_i, g_p);
            return;

        case SECTOR_FLOORZVEL:
        {
            if (iSet)
            {
                s->extra = lValue;
                int const goal = GetAnimationGoal(iSector, SECTANIM_FLOOR);
                if (goal != -1)
                    SetAnimation(iSector, SECTANIM_FLOOR, goal, s->extra);

                return;
            }
            Gv_SetVar(lVar2, (GetAnimationGoal(iSector, SECTANIM_FLOOR) == -1) ? 0 : s->extra, g_i, g_p);
            return;
        }

        case SECTOR_FLOORZGOAL:
            if (iSet)
            {
                SetAnimation(iSector, SECTANIM_FLOOR, lValue, s->extra);
                return;
            }
            Gv_SetVar(lVar2, GetAnimationGoal(iSector, SECTANIM_FLOOR), g_i, g_p);
            return;

        case SECTOR_CEILINGSTAT:
            if (iSet)
            {
                s->ceilingstat = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingstat, g_i, g_p);
            return;

        case SECTOR_FLOORSTAT:
            if (iSet)
            {
                s->floorstat = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorstat, g_i, g_p);
            return;

        case SECTOR_CEILINGPICNUM:
            if (iSet)
            {
                s->ceilingpicnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingpicnum, g_i, g_p);
            return;

        case SECTOR_CEILINGSLOPE:
            if (iSet)
            {
                s->ceilingheinum = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingheinum, g_i, g_p);
            return;

        case SECTOR_CEILINGSHADE:
            if (iSet)
            {
                s->ceilingshade = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingshade, g_i, g_p);
            return;

        case SECTOR_CEILINGPAL:
            if (iSet)
            {
                s->ceilingpal = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingpal, g_i, g_p);
            return;

        case SECTOR_CEILINGXPANNING:
            if (iSet)
            {
                s->ceilingxpanning = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingxpanning, g_i, g_p);
            return;

        case SECTOR_CEILINGYPANNING:
            if (iSet)
            {
                s->ceilingypanning = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->ceilingypanning, g_i, g_p);
            return;

        case SECTOR_FLOORPICNUM:
            if (iSet)
            {
                s->floorpicnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorpicnum, g_i, g_p);
            return;

        case SECTOR_FLOORSLOPE:
            if (iSet)
            {
                s->floorheinum = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorheinum, g_i, g_p);
            return;

        case SECTOR_FLOORSHADE:
            if (iSet)
            {
                s->floorshade = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorshade, g_i, g_p);
            return;

        case SECTOR_FLOORPAL:
            if (iSet)
            {
                s->floorpal = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorpal, g_i, g_p);
            return;

        case SECTOR_FLOORXPANNING:
            if (iSet)
            {
                s->floorxpanning = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorxpanning, g_i, g_p);
            return;

        case SECTOR_FLOORYPANNING:
            if (iSet)
            {
                s->floorypanning = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->floorypanning, g_i, g_p);
            return;

        case SECTOR_VISIBILITY:
            if (iSet)
            {
                s->visibility = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->visibility, g_i, g_p);
            return;

        case SECTOR_ALIGNTO:
            if (iSet)
            {
                s->fogpal = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->fogpal, g_i, g_p);
            return;

        case SECTOR_LOTAG:
            if (iSet)
            {
                s->lotag = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->lotag, g_i, g_p);
            return;

        case SECTOR_HITAG:
            if (iSet)
            {
                s->hitag = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->hitag, g_i, g_p);
            return;

        case SECTOR_EXTRA:
            if (iSet)
            {
                s->extra = lValue;
                return;
            }
            Gv_SetVar(lVar2, s->extra, g_i, g_p);
            return;

        default:
            return;
    }
}

void __fastcall X_AccessSprite(int iSet, int iActor, int labelNum, int lVar2, int lParm2)
{
    int const lValue = (iSet) ? Gv_GetVar(lVar2, g_i, g_p) : 0;
    auto const& actorLabel = ActorLabels[labelNum];

    switch (actorLabel.lId)
    {
        case ACTOR_X:
            if (iSet)
            {
                sprite[iActor].x = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].x, g_i, g_p);
            return;

        case ACTOR_Y:
            if (iSet)
            {
                sprite[iActor].y = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].y, g_i, g_p);
            return;

        case ACTOR_Z:
            if (iSet)
            {
                sprite[iActor].z = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].z, g_i, g_p);
            return;

        case ACTOR_CSTAT:
            if (iSet)
            {
                sprite[iActor].cstat = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].cstat, g_i, g_p);
            return;

        case ACTOR_PICNUM:
            if (iSet)
            {
                sprite[iActor].picnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].picnum, g_i, g_p);
            return;

        case ACTOR_SHADE:
            if (iSet)
            {
                sprite[iActor].shade = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].shade, g_i, g_p);
            return;

        case ACTOR_PAL:
            if (iSet)
            {
                sprite[iActor].pal = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].pal, g_i, g_p);
            return;

        case ACTOR_CLIPDIST:
            if (iSet)
            {
                sprite[iActor].clipdist = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].clipdist, g_i, g_p);
            return;

        case ACTOR_BLEND:
            //case ACTOR_DETAIL:
            if (iSet)
            {
                sprite[iActor].blend = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].blend, g_i, g_p);
            return;

        case ACTOR_XREPEAT:
            if (iSet)
            {
                sprite[iActor].xrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].xrepeat, g_i, g_p);
            return;

        case ACTOR_YREPEAT:
            if (iSet)
            {
                sprite[iActor].yrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].yrepeat, g_i, g_p);
            return;

        case ACTOR_XOFFSET:
            if (iSet)
            {
                sprite[iActor].xoffset = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].xoffset, g_i, g_p);
            return;

        case ACTOR_YOFFSET:
            if (iSet)
            {
                sprite[iActor].yoffset = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].yoffset, g_i, g_p);
            return;

        case ACTOR_SECTNUM:
            if (iSet)
            {
                changespritesect(iActor, lValue);
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].sectnum, g_i, g_p);
            return;

        case ACTOR_STATNUM:
            if (iSet)
            {
                changespritestat(iActor, lValue);
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].statnum, g_i, g_p);
            return;

        case ACTOR_ANG:
            if (iSet)
            {
                sprite[iActor].ang = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].ang, g_i, g_p);
            return;

        case ACTOR_OWNER:
            if (iSet)
            {
                sprite[iActor].owner = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].owner, g_i, g_p);
            return;

        case ACTOR_XVEL:
            if (iSet)
            {
                sprite[iActor].xvel = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].xvel, g_i, g_p);
            return;

        case ACTOR_YVEL:
            if (iSet)
            {
                sprite[iActor].yvel = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].yvel, g_i, g_p);
            return;

        case ACTOR_ZVEL:
            if (iSet)
            {
                sprite[iActor].zvel = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].zvel, g_i, g_p);
            return;

        case ACTOR_LOTAG:
            if (iSet)
            {
                sprite[iActor].lotag = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].lotag, g_i, g_p);
            return;

        case ACTOR_HITAG:
            if (iSet)
            {
                sprite[iActor].hitag = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].hitag, g_i, g_p);
            return;

        case ACTOR_EXTRA:
            if (iSet)
            {
                sprite[iActor].extra = lValue;
                return;
            }
            Gv_SetVar(lVar2, sprite[iActor].extra, g_i, g_p);
            return;

        case ACTOR_HTCGG:
            if (iSet)
            {
                actor[iActor].cgg = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].cgg, g_i, g_p);
            return;

        case ACTOR_HTPICNUM:
            if (iSet)
            {
                actor[iActor].htpicnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].htpicnum, g_i, g_p);
            return;

        case ACTOR_HTANG:
            if (iSet)
            {
                actor[iActor].htang = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].htang, g_i, g_p);
            return;

        case ACTOR_HTEXTRA:
            if (iSet)
            {
                actor[iActor].htextra = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].htextra, g_i, g_p);
            return;

        case ACTOR_HTOWNER:
            if (iSet)
            {
                actor[iActor].htowner = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].htowner, g_i, g_p);
            return;

        case ACTOR_HTMOVFLAG:
            if (iSet)
            {
                actor[iActor].movflag = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].movflag, g_i, g_p);
            return;

        case ACTOR_HTTEMPANG:
            if (iSet)
            {
                actor[iActor].tempang = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].tempang, g_i, g_p);
            return;

        case ACTOR_HTSTAYPUT:
            if (iSet)
            {
                actor[iActor].actorstayput = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].actorstayput, g_i, g_p);
            return;

        case ACTOR_HTDISPICNUM:
            if (iSet)
            {
                actor[iActor].dispicnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].dispicnum, g_i, g_p);
            return;

        case ACTOR_HTTIMETOSLEEP:
            if (iSet)
            {
                actor[iActor].timetosleep = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].timetosleep, g_i, g_p);
            return;

        case ACTOR_HTFLOORZ:
            if (iSet)
            {
                actor[iActor].floorz = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].floorz, g_i, g_p);
            return;

        case ACTOR_HTCEILINGZ:
            if (iSet)
            {
                actor[iActor].ceilingz = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].ceilingz, g_i, g_p);
            return;

        case ACTOR_HTLASTVX:
            if (iSet)
            {
                actor[iActor].lastvx = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].lastvx, g_i, g_p);
            return;

        case ACTOR_HTLASTVY:
            if (iSet)
            {
                actor[iActor].lastvy = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].lastvy, g_i, g_p);
            return;

        case ACTOR_HTBPOSX:
            if (iSet)
            {
                actor[iActor].bpos.x = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].bpos.x, g_i, g_p);
            return;

        case ACTOR_HTBPOSY:
            if (iSet)
            {
                actor[iActor].bpos.y = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].bpos.y, g_i, g_p);
            return;

        case ACTOR_HTBPOSZ:
            if (iSet)
            {
                actor[iActor].bpos.z = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].bpos.z, g_i, g_p);
            return;

        case ACTOR_HTG_T:
            if (iSet)
            {
                actor[iActor].t_data[lParm2] = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].t_data[lParm2], g_i, g_p);
            return;

        case ACTOR_ANGOFF:
            if (iSet)
            {
                spriteext[iActor].mdangoff = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].mdangoff, g_i, g_p);
            return;

        case ACTOR_PITCH:
            if (iSet)
            {
                spriteext[iActor].mdpitch = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].mdpitch, g_i, g_p);
            return;

        case ACTOR_ROLL:
            if (iSet)
            {
                spriteext[iActor].mdroll = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].mdroll, g_i, g_p);
            return;

        case ACTOR_MDXOFF:
            if (iSet)
            {
                spriteext[iActor].mdpivot_offset.x = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].mdpivot_offset.x, g_i, g_p);
            return;

        case ACTOR_MDYOFF:
            if (iSet)
            {
                spriteext[iActor].mdpivot_offset.y = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].mdpivot_offset.y, g_i, g_p);
            return;

        case ACTOR_MDZOFF:
            if (iSet)
            {
                spriteext[iActor].mdpivot_offset.z = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].mdpivot_offset.z, g_i, g_p);
            return;

        case ACTOR_MDFLAGS:
            if (iSet)
            {
                spriteext[iActor].flags = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].flags, g_i, g_p);
            return;

        case ACTOR_XPANNING:
            if (iSet)
            {
                spriteext[iActor].xpanning = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].xpanning, g_i, g_p);
            return;

        case ACTOR_YPANNING:
            if (iSet)
            {
                spriteext[iActor].ypanning = lValue;
                return;
            }
            Gv_SetVar(lVar2, spriteext[iActor].ypanning, g_i, g_p);
            return;

        case ACTOR_HTFLAGS:
            if (iSet)
            {
                actor[iActor].flags = lValue;
                return;
            }
            Gv_SetVar(lVar2, actor[iActor].flags, g_i, g_p);
            return;

        case ACTOR_ALPHA:
            if (iSet)
            {
                spriteext[iActor].alpha = (float)lValue * (1.f / 255.0f);;
                return;
            }
            Gv_SetVar(lVar2, (uint8_t)(spriteext[iActor].alpha * 255.0f), g_i, g_p);
            return;

        default:
            return;
    }
}

void __fastcall X_AccessTsprite(int iSet, int iActor, int labelNum, int lVar2)
{
    int const lValue = (iSet) ? Gv_GetVar(lVar2, g_i, g_p) : 0;
    auto const& tsprLabel = TsprLabels[labelNum];

    auto const tspr = spriteext[iActor].tspr;
    switch (tsprLabel.lId)
    {
        case ACTOR_X:
            if (iSet)
            {
                tspr->x = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->x, g_i, g_p);
            return;

        case ACTOR_Y:
            if (iSet)
            {
                tspr->y = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->y, g_i, g_p);
            return;

        case ACTOR_Z:
            if (iSet)
            {
                tspr->z = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->z, g_i, g_p);
            return;

        case ACTOR_CSTAT:
            if (iSet)
            {
                tspr->cstat = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->cstat, g_i, g_p);
            return;

        case ACTOR_PICNUM:
            if (iSet)
            {
                tspr->picnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->picnum, g_i, g_p);
            return;

        case ACTOR_SHADE:
            if (iSet)
            {
                tspr->shade = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->shade, g_i, g_p);
            return;

        case ACTOR_PAL:
            if (iSet)
            {
                tspr->pal = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->pal, g_i, g_p);
            return;

        case ACTOR_CLIPDIST:
            if (iSet)
            {
                tspr->clipdist = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->clipdist, g_i, g_p);
            return;

        case ACTOR_BLEND:
            //case ACTOR_DETAIL:
            if (iSet)
            {
                tspr->blend = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->blend, g_i, g_p);
            return;

        case ACTOR_XREPEAT:
            if (iSet)
            {
                tspr->xrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->xrepeat, g_i, g_p);
            return;

        case ACTOR_YREPEAT:
            if (iSet)
            {
                tspr->yrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->yrepeat, g_i, g_p);
            return;

        case ACTOR_XOFFSET:
            if (iSet)
            {
                tspr->xoffset = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->xoffset, g_i, g_p);
            return;

        case ACTOR_YOFFSET:
            if (iSet)
            {
                tspr->yoffset = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->yoffset, g_i, g_p);
            return;

        case ACTOR_SECTNUM:
            if (iSet)
            {
                tspr->sectnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->sectnum, g_i, g_p);
            return;

        case ACTOR_STATNUM:
            if (iSet)
            {
                tspr->statnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->statnum, g_i, g_p);
            return;

        case ACTOR_ANG:
            if (iSet)
            {
                tspr->ang = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->ang, g_i, g_p);
            return;

        case ACTOR_OWNER:
            if (iSet)
            {
                tspr->owner = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->owner, g_i, g_p);
            return;

#if 1
        case ACTOR_XVEL:
            if (iSet)
            {
                tspr->xvel = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->xvel, g_i, g_p);
            return;

        case ACTOR_YVEL:
            if (iSet)
            {
                tspr->yvel = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->yvel, g_i, g_p);
            return;

        case ACTOR_ZVEL:
            if (iSet)
            {
                tspr->zvel = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->zvel, g_i, g_p);
            return;

        case ACTOR_LOTAG:
            if (iSet)
            {
                tspr->lotag = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->lotag, g_i, g_p);
            return;

        case ACTOR_HITAG:
            if (iSet)
            {
                tspr->hitag = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->hitag, g_i, g_p);
            return;

        case ACTOR_EXTRA:
            if (iSet)
            {
                tspr->extra = lValue;
                return;
            }
            Gv_SetVar(lVar2, tspr->extra, g_i, g_p);
            return;
#endif

        default:
            return;
    }
}

void __fastcall X_AccessProjectile(int iSet, int tileNum, int labelNum, int lVar2)
{
    int const lValue = (iSet) ? Gv_GetVar(lVar2, g_i, g_p) : 0;
    auto const& projLabel = ProjectileLabels[labelNum];

    if (EDUKE32_PREDICT_FALSE((unsigned)tileNum >= MAXTILES || g_tile[tileNum].proj == NULL))
    {
        CON_ERRPRINTF("invalid projectile %d\n", tileNum);
        return;
    }

    auto& proj = *g_tile[tileNum].proj;
    switch (projLabel.lId)
    {
        case PROJ_WORKSLIKE:
            if (iSet)
            {
                proj.workslike = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.workslike, g_i, g_p);
            return;

        case PROJ_SPAWNS:
            if (iSet)
            {
                proj.spawns = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.spawns, g_i, g_p);
            return;

        case PROJ_SXREPEAT:
            if (iSet)
            {
                proj.sxrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.sxrepeat, g_i, g_p);
            return;

        case PROJ_SYREPEAT:
            if (iSet)
            {
                proj.syrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.syrepeat, g_i, g_p);
            return;

        case PROJ_SOUND:
            if (iSet)
            {
                proj.sound = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.sound, g_i, g_p);
            return;

        case PROJ_ISOUND:
            if (iSet)
            {
                proj.isound = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.isound, g_i, g_p);
            return;

        case PROJ_VEL:
            if (iSet)
            {
                proj.vel = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.vel, g_i, g_p);
            return;

        case PROJ_EXTRA:
            if (iSet)
            {
                proj.extra = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.extra, g_i, g_p);
            return;

        case PROJ_DECAL:
            if (iSet)
            {
                proj.decal = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.decal, g_i, g_p);
            return;

        case PROJ_TRAIL:
            if (iSet)
            {
                proj.trail = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.trail, g_i, g_p);
            return;

        case PROJ_TXREPEAT:
            if (iSet)
            {
                proj.txrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.txrepeat, g_i, g_p);
            return;

        case PROJ_TYREPEAT:
            if (iSet)
            {
                proj.tyrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.tyrepeat, g_i, g_p);
            return;

        case PROJ_TOFFSET:
            if (iSet)
            {
                proj.toffset = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.toffset, g_i, g_p);
            return;

        case PROJ_TNUM:
            if (iSet)
            {
                proj.tnum = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.tnum, g_i, g_p);
            return;

        case PROJ_DROP:
            if (iSet)
            {
                proj.drop = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.drop, g_i, g_p);
            return;

        case PROJ_CSTAT:
            if (iSet)
            {
                proj.cstat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.cstat, g_i, g_p);
            return;

        case PROJ_CLIPDIST:
            if (iSet)
            {
                proj.clipdist = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.clipdist, g_i, g_p);
            return;

        case PROJ_SHADE:
            if (iSet)
            {
                proj.shade = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.shade, g_i, g_p);
            return;

        case PROJ_XREPEAT:
            if (iSet)
            {
                proj.xrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.xrepeat, g_i, g_p);
            return;

        case PROJ_YREPEAT:
            if (iSet)
            {
                proj.yrepeat = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.yrepeat, g_i, g_p);
            return;

        case PROJ_PAL:
            if (iSet)
            {
                proj.pal = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.pal, g_i, g_p);
            return;

        case PROJ_EXTRA_RAND:
            if (iSet)
            {
                proj.extra_rand = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.extra_rand, g_i, g_p);
            return;

        case PROJ_HITRADIUS:
            if (iSet)
            {
                proj.hitradius = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.hitradius, g_i, g_p);
            return;

        case PROJ_MOVECNT:
            if (iSet)
            {
                proj.movecnt = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.movecnt, g_i, g_p);
            return;

        case PROJ_OFFSET:
            if (iSet)
            {
                proj.offset = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.offset, g_i, g_p);
            return;

        case PROJ_BOUNCES:
            if (iSet)
            {
                proj.bounces = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.bounces, g_i, g_p);
            return;

        case PROJ_BSOUND:
            if (iSet)
            {
                proj.bsound = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.bsound, g_i, g_p);
            return;

        case PROJ_RANGE:
            if (iSet)
            {
                proj.range = lValue;
                return;
            }
            Gv_SetVar(lVar2, proj.range, g_i, g_p);
            return;

        default:
            return;
    }
}
