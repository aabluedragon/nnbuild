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

#ifndef _gamedef_h_
#define _gamedef_h_

#include "actors.h"
#include "build.h"  // hashtable_t
#include "common.h"  // tokenlist
#include "player.h"  // projectile_t

#define LABEL_CHAR sizeof(uint8_t)
#define LABEL_SHORT sizeof(uint16_t)
#define LABEL_INT sizeof(uint32_t)
#define LABEL_UNSIGNED 8
#define LABEL_HASPARM2 16
#define LABEL_ISSTRING 32
#define LABEL_READFUNC 64
#define LABEL_WRITEFUNC 128

#define VM_IFELSE_MAGIC_BIT (1<<31)
#define VM_INSTMASK 0xfff

#define VM_DECODE_INST(xxx) ((int)((xxx) & VM_INSTMASK))
#define VM_DECODE_LINE_NUMBER(xxx) ((int)((xxx&~VM_IFELSE_MAGIC_BIT) >> 12))

#define MAXLABELS 16384
#define LAST_LABEL (label+(g_labelCnt<<6))

typedef struct SystemVarID_s {
    int32_t RETURN          = -1;
    int32_t WEAPON          = -1;
    int32_t WORKSLIKE       = -1;
    int32_t ZRANGE          = -1;
    int32_t ANGRANGE        = -1;
    int32_t AUTOAIMANGLE    = -1;
    int32_t LOTAG           = -1;
    int32_t HITAG           = -1;
    int32_t TEXTURE         = -1;
    int32_t THISACTOR       = -1;

    int32_t RESPAWN_MONSTERS    = -1;
    int32_t RESPAWN_ITEMS       = -1;
    int32_t RESPAWN_INVENTORY   = -1;
    int32_t MONSTERS_OFF        = -1;
    int32_t MARKER              = -1;
    int32_t FFIRE               = -1;

    int32_t structs = -1;
} SystemVarID_t;
extern SystemVarID_t sysVarIDs;

extern const char* EventNames[MAXEVENTS];
extern intptr_t apScriptEvents[MAXEVENTS];
extern int32_t g_currentEvent;

extern int otherp;
extern int g_currentweapon;
extern int g_gun_pos;
extern int g_looking_arc;
extern int g_currentweapon;
extern int g_weapon_xoffset;
extern int g_gs;
extern int g_kb;
extern int g_looking_angSR1;
extern char g_scriptFileName[BMAX_PATH];
extern int g_totalLines,g_lineNumber;
extern int g_errorCnt,g_warningCnt;

extern uint8_t* bitptr;

typedef struct {
    const char* token;
    int32_t val;
} tokenmap_t;

struct vmofs
{
    char* fn;
    struct vmofs* next;
    int offset;
};
extern struct vmofs* vmoffset;

static inline const char* C_GetFileForOffset(int const offset)
{
    auto ofs = vmoffset;
    while (ofs->offset > offset)
        ofs = ofs->next;
    return ofs->fn;
}
#define VM_FILENAME(xxx) C_GetFileForOffset((xxx)-apScript)

#define CON_ERROR OSD_ERROR "Line %d, %s: "

extern int g_i,g_p;
extern int g_scriptSanityChecks;

extern int g_errorLineNum;
extern int g_tw;
extern const tokenmap_t vm_keywords[];
extern spritetype *g_sp;

void C_Compile(const char* fn);
char const* VM_GetKeywordForID(int32_t id);

// Enums
enum
{
    LABEL_ACTION = 0x01,
    LABEL_ACTOR = 0x02,
    LABEL_AI = 0x04,
    LABEL_DEFINE = 0x08,
    LABEL_EVENT = 0x10,
    LABEL_MOVE = 0x20,
    LABEL_STATE = 0x40,

    LABEL_ANY = -1,
};

// KEEPINSYNC gamevars.cpp: "special vars for struct access"
enum QuickStructureAccess_t
{
    STRUCT_SPRITE,
    STRUCT_SPRITE_INTERNAL__,
    STRUCT_ACTOR_INTERNAL__,
    STRUCT_SPRITEEXT_INTERNAL__,
    STRUCT_SECTOR,
    STRUCT_SECTOR_INTERNAL__,
    STRUCT_WALL,
    STRUCT_WALL_INTERNAL__,
    STRUCT_PLAYER,
    STRUCT_PLAYER_INTERNAL__,
    STRUCT_ACTORVAR,
    STRUCT_PLAYERVAR,
    STRUCT_TSPR,
    STRUCT_PROJECTILE,
    STRUCT_THISPROJECTILE,
    STRUCT_USERDEF,
    STRUCT_INPUT,
    STRUCT_TILEDATA,
    //STRUCT_PALDATA,
    NUMQUICKSTRUCTS,
};

enum ScriptError_t
{
    ERROR_CLOSEBRACKET,
    ERROR_EVENTONLY,
    ERROR_EXCEEDSMAXPALS,
    ERROR_EXCEEDSMAXTILES,
    ERROR_EXPECTEDKEYWORD,
    ERROR_FOUNDWITHIN,
    ERROR_ISAKEYWORD,
    ERROR_NOENDSWITCH,
    ERROR_NOTAGAMEDEF,
    ERROR_NOTAGAMEVAR,
    ERROR_NOTAGAMEARRAY,
    ERROR_NOTAMEMBER,
    ERROR_GAMEARRAYBNC,
    ERROR_GAMEARRAYBNO,
    ERROR_INVALIDARRAYWRITE,
    ERROR_OPENBRACKET,
    ERROR_PARAMUNDEFINED,
    ERROR_SYMBOLNOTRECOGNIZED,
    ERROR_SYNTAXERROR,
    ERROR_VARREADONLY,
    ERROR_VARTYPEMISMATCH,
    WARNING_BADGAMEVAR,
    WARNING_DUPLICATECASE,
    WARNING_DUPLICATEDEFINITION,
    WARNING_EVENTSYNC,
    WARNING_LABELSONLY,
    WARNING_NAMEMATCHESVAR,
    WARNING_REVEVENTSYNC
};

enum ScriptKeywords_t
{
    // ---
    // Specialized setvar keywords
    CON_SETVAR_GLOBAL,
    CON_SETVAR_PLAYER,
    CON_SETVAR_ACTOR,
    // ---

    CON_ADDAMMO,
    CON_IFRND,
    CON_ENDA,
    CON_IFCANSEE,
    CON_IFHITWEAPON,
    CON_ACTION,
    CON_IFPDISTL,
    CON_IFPDISTG,
    CON_ELSE,
    CON_STRENGTH,
    CON_BREAK,
    CON_SHOOT,
    CON_PALFROM,
    CON_SOUND,
    CON_FALL,
    CON_STATE,
    CON_ENDS,
    CON_COMMENT,
    CON_IFAI,
    CON_KILLIT,
    CON_ADDWEAPON,
    CON_AI,
    CON_ADDPHEALTH,
    CON_IFDEAD,
    CON_IFSQUISHED,
    CON_SIZETO,
    CON_LEFTBRACE,
    CON_RIGHTBRACE,
    CON_SPAWN,
    CON_MOVE,
    CON_IFWASWEAPON,
    CON_IFACTION,
    CON_IFACTIONCOUNT,
    CON_RESETACTIONCOUNT,
    CON_DEBRIS,
    CON_PSTOMP,
    CON_BLOCKCOMMENT,
    CON_CSTAT,
    CON_IFMOVE,
    CON_RESETPLAYER,
    CON_IFONWATER,
    CON_IFINWATER,
    CON_IFCANSHOOTTARGET,
    CON_IFCOUNT,
    CON_RESETCOUNT,
    CON_ADDINVENTORY,
    CON_IFACTORNOTSTAYPUT,
    CON_HITRADIUS,
    CON_IFP,
    CON_COUNT,
    CON_IFACTOR,
    CON_IFSTRENGTH,
    CON_GUTS,
    CON_IFSPAWNEDBY,
    CON_WACKPLAYER,
    CON_IFGAPZL,
    CON_IFHITSPACE,
    CON_IFOUTSIDE,
    CON_IFMULTIPLAYER,
    CON_OPERATE,
    CON_IFINSPACE,
    CON_DEBUG,
    CON_ENDOFGAME,
    CON_IFBULLETNEAR,
    CON_IFRESPAWN,
    CON_IFFLOORDISTL,
    CON_IFCEILINGDISTL,
    CON_SPRITEPAL,
    CON_IFPINVENTORY,
    CON_CACTOR,
    CON_IFPHEALTHL,
    CON_QUOTE,
    CON_IFINOUTERSPACE,
    CON_IFNOTMOVING,
    CON_RESPAWNHITAG,
    CON_TIP,
    CON_IFSPRITEPAL,
    CON_MONEY,
    CON_SOUNDONCE,          // 87
    CON_ADDKILLS,           // 88
    CON_STOPSOUND,          // 89
    CON_IFAWAYFROMWALL,     // 90
    CON_IFCANSEETARGET,     // 91
    CON_GLOBALSOUND,        // 92
    CON_LOTSOFGLASS,        // 93
    CON_IFGOTWEAPONCE,      // 94
    CON_GETLASTPAL,         // 95
    CON_PKICK,              // 96
    CON_MIKESND,            // 97
    CON_SIZEAT,             // 99
    CON_ADDSTRENGTH,        // 100
    CON_CSTATOR,            // 101
    CON_MAIL,               // 102
    CON_PAPER,              // 103
    CON_TOSSWEAPON,         // 104
    CON_SLEEPTIME,          // 105
    CON_NULLOP,             // 106
    CON_IFNOSOUNDS,         // 109
    CON_CLIPDIST,           // 110
    CON_IFANGDIFFL,         // 111
    CON_IFVARL,             // 113
    CON_IFVARG,             // 114
    CON_SETVARVAR,          // 115
    CON_SETVAR,             // 116
    CON_ADDVARVAR,          // 117
    CON_ADDVAR,             // 118
    CON_IFVARVARL,          // 119
    CON_IFVARVARG,          // 120
    CON_ADDLOGVAR,          // 121
    CON_ADDLOG,             // 122
    CON_ENDEVENT,           // 124
    CON_IFVARE,             // 125
    CON_IFVARVARE,          // 126
    CON_SPGETLOTAG,         // 127 !DEPRECATED!
    CON_SPGETHITAG,         // 128 !DEPRECATED!
    CON_SECTGETLOTAG,       // 129 !DEPRECATED!
    CON_SECTGETHITAG,       // 130 !DEPRECATED!
    CON_IFSOUND,            // 131
    CON_GETTEXTUREFLOOR,    // 132
    CON_GETTEXTURECEILING,  // 133
    CON_INITTIMER,          // 134
    CON_STARTTRACK,         // 135
    CON_RANDVAR,            // 136
    CON_GETANGLETOTARGET,   // 138
    CON_GETACTORANGLE,      // 139
    CON_SETACTORANGLE,      // 140
    CON_MULVAR,             // 141
    CON_MULVARVAR,          // 142
    CON_DIVVAR,             // 143
    CON_DIVVARVAR,          // 144
    CON_MODVAR,             // 145
    CON_MODVARVAR,          // 146
    CON_ANDVAR,             // 147
    CON_ANDVARVAR,          // 148
    CON_ORVAR,              // 149
    CON_ORVARVAR,           // 150
    CON_GETPLAYERANGLE,     // 151
    CON_SETPLAYERANGLE,     // 152
    CON_LOCKPLAYER,         // 153
    CON_SETSECTOR,          // 154
    CON_GETSECTOR,          // 155
    CON_SETACTOR,           // 156
    CON_GETACTOR,           // 157
    CON_SETWALL,            // 158
    CON_GETWALL,            // 159
    CON_FINDNEARACTOR,      // 160
    CON_FINDNEARACTORVAR,   // 161
    CON_SETACTORVAR,        // 162
    CON_GETACTORVAR,        // 163
    CON_ESPAWN,             // 164
    CON_GETPLAYER,          // 165
    CON_SETPLAYER,          // 166
    CON_SQRT,               // 167
    CON_ESPAWNVAR,          // 169
    CON_GETUSERDEF,         // 170
    CON_SETUSERDEF,         // 171
    CON_SUBVARVAR,          // 172
    CON_SUBVAR,             // 173
    CON_IFVARN,             // 174
    CON_IFVARVARN,          // 175
    CON_IFVARAND,           // 176
    CON_IFVARVARAND,        // 177
    CON_MYOS,               // 178
    CON_MYOSPAL,            // 179
    CON_DISPLAYRAND,        // 180
    CON_SIN,                // 181
    CON_XORVARVAR,          // 182
    CON_XORVAR,             // 183
    CON_RANDVARVAR,         // 184
    CON_MYOSX,              // 185
    CON_MYOSPALX,           // 186
    CON_GMAXAMMO,           // 187
    CON_SMAXAMMO,           // 188
    CON_STARTLEVEL,         // 189
    CON_ESHOOT,             // 190
    CON_QSPAWN,             // 191
    CON_ROTATESPRITE,       // 192
    CON_COS,                // 195
    CON_FINDNEARACTOR3D,    // 197
    CON_FINDNEARACTOR3DVAR, // 198
    CON_FLASH,              // 199
    CON_QSPAWNVAR,          // 200
    CON_EQSPAWN,            // 201
    CON_EQSPAWNVAR,         // 202
    CON_MINITEXT,           // 203
    CON_GAMETEXT,           // 204
    CON_DIGITALNUMBER,      // 205
    CON_ADDWEAPONVAR,       // 206
    CON_SETPROJECTILE,      // 207
    CON_ANGOFF,             // 208
    CON_UPDATESECTOR,       // 209
    CON_INSERTSPRITEQ,      // 210
    CON_ANGOFFVAR,          // 211
    CON_WHILEVARN,          // 212
    CON_SWITCH,             // 213
    CON_ENDSWITCH,          // 216
    CON_SOUNDVAR,           // 218
    CON_FINDPLAYER,         // 219
    CON_FINDOTHERPLAYER,    // 220
    CON_ACTIVATEBYSECTOR,   // 221
    CON_OPERATESECTORS,     // 222
    CON_OPERATERESPAWNS,    // 223
    CON_OPERATEACTIVATORS,  // 224
    CON_OPERATEMASTERSWITCHES,  // 225
    CON_CHECKACTIVATORMOTION,   // 226
    CON_ZSHOOT,             // 227
    CON_DIST,               // 228
    CON_LDIST,              // 229
    CON_SHIFTVARL,          // 230
    CON_SHIFTVARR,          // 231
    CON_GETANGLE,           // 233
    CON_WHILEVARVARN,       // 234
    CON_HITSCAN,            // 235
    CON_TIME,               // 236
    CON_GETPLAYERVAR,       // 237
    CON_SETPLAYERVAR,       // 238
    CON_MULSCALE,           // 239
    CON_SETASPECT,          // 240
    CON_EZSHOOT,            // 241 
    CON_MOVESPRITE,         // 243
    CON_CHECKAVAILWEAPON,   // 244
    CON_SOUNDONCEVAR,       // 245
    CON_UPDATESECTORZ,      // 246
    CON_STOPALLSOUNDS,      // 247
    CON_SSP,                // 248
    CON_STOPSOUNDVAR,       // 249
    CON_DISPLAYRANDVAR,     // 250
    CON_DISPLAYRANDVARVAR,  // 251
    CON_CHECKAVAILINVEN,    // 252
    CON_GLOBALSOUNDVAR,     // 253
    CON_GUNIQHUDID,         // 254
    CON_GETPROJECTILE,      // 255
    CON_GETTHISPROJECTILE,  // 256
    CON_SETTHISPROJECTILE,  // 257
    CON_USERQUOTE,          // 260  
    CON_REDEFINEQUOTE,
    CON_QSPRINTF,
    CON_GETPNAME,
    CON_QSTRCAT,
    CON_QSTRCPY,
    CON_SETSPRITE,
    CON_ROTATEPOINT,
    CON_DRAGPOINT,
    CON_GETZRANGE,
    CON_CHANGESPRITESTAT,
    CON_GETCEILZOFSLOPE,
    CON_GETFLORZOFSLOPE,
    CON_NEARTAG,
    CON_CHANGESPRITESECT,
    CON_SPRITEFLAGS,
    CON_SAVEGAMEVAR,
    CON_READGAMEVAR,
    CON_FINDNEARSPRITE,
    CON_FINDNEARSPRITEVAR,
    CON_FINDNEARSPRITE3D,
    CON_FINDNEARSPRITE3DVAR,
    CON_SETINPUT,
    CON_GETINPUT,
    CON_SAVE,
    CON_CANSEE,
    CON_CANSEESPR,
    CON_FINDNEARACTORZ,
    CON_FINDNEARACTORZVAR,
    CON_FINDNEARSPRITEZ,
    CON_FINDNEARSPRITEZVAR,
    CON_GETCURRADDRESS,
    CON_JUMP,
    CON_QSTRLEN,
    CON_GETINCANGLE,
    CON_QUAKE,
    CON_SHOWVIEW,
    CON_HEADSPRITESTAT,
    CON_PREVSPRITESTAT,
    CON_NEXTSPRITESTAT,
    CON_HEADSPRITESECT,
    CON_PREVSPRITESECT,
    CON_NEXTSPRITESECT,
    CON_GETKEYNAME,
    CON_QSUBSTR,
    CON_GAMETEXTZ,
    CON_DIGITALNUMBERZ,
    CON_HITRADIUSVAR,
    CON_ROTATESPRITE16,
    CON_SETARRAY,
    CON_RESIZEARRAY,
    CON_WRITEARRAYTOFILE,
    CON_READARRAYFROMFILE,
    CON_STARTTRACKVAR,
    CON_QGETSYSSTR,
    CON_GETTICKS,
    CON_GETTSPR,
    CON_SETTSPR,
    CON_SAVEMAPSTATE,
    CON_LOADMAPSTATE,
    CON_CLEARMAPSTATE,
    CON_CMENU,
    CON_GETTIMEDATE, 
    CON_ACTIVATECHEAT,
    CON_SETGAMEPALETTE,
    CON_IFVAROR,
    CON_IFVARVAROR,
    CON_IFVARXOR,
    CON_IFVARVARXOR,
    CON_IFVAREITHER,
    CON_IFVARVAREITHER,
    CON_GETARRAYSIZE,
    CON_SAVENN,
    CON_QSTRDIM,
    CON_SCREENTEXT,
    CON_RETURN,
    CON_ACTORSOUND,
    CON_ROTATESPRITEA,
    CON_GETTEAMNAME,
    CON_KLABS,
    CON_SHIFTVARVARL,
    CON_SHIFTVARVARR,
    CON_GETTILEDATA,
    CON_SETTILEDATA,
    CON_INTERPOLATE,
    CON_LDISTVAR,
    CON_DISTVAR,
    CON_SECTSETINTERPOLATION,
    CON_SECTCLEARINTERPOLATION,
    CON_STOPACTORSOUND,
    CON_INV,
    CON_OPCODE_END,
    // ---

    // ---
    // these are the keywords that don't have instructions written into the bytecode
    // ie: parsed in C_ParseCommand, but no code in VM_DoExecute.
    CON_ACTOR,
    CON_BETANAME, // !DEPRECATED!
    CON_CASE,
    CON_CHEATKEYS,
    CON_DAMAGEEVENTTILE,
    CON_DAMAGEEVENTTILERANGE,
    CON_DEFAULT,
    CON_DEFINE,
    CON_DEFINECHEAT,
    CON_DEFINEGAMEFUNCNAME,
    CON_DEFINEGAMETYPE,
    CON_DEFINELEVELNAME,
    CON_DEFINEPROJECTILE,
    CON_DEFINEQUOTE,
    CON_DEFINESKILLNAME,
    CON_DEFINESOUND,
    CON_DEFINEVOLUMENAME,
    CON_DEFSTATE,
    CON_DYNAMICREMAP,
    CON_ENHANCED, // !DEPRECATED!
    CON_EVENTLOADACTOR,
    CON_GAMEARRAY,
    CON_GAMESTARTUP,
    CON_GAMEVAR,
    CON_INCLUDE,
    CON_MUSIC,
    CON_ONEVENT,
    CON_PRECACHE,
    CON_SCRIPTSIZE,
    CON_SETCFGNAME,
    CON_SETDEFNAME,
    CON_SETGAMENAME,
    CON_SPRITENOPAL,
    CON_SPRITENOSHADE,
    CON_SPRITENVG,
    CON_SPRITESHADOW,
    CON_USERACTOR,
    // ---

    CON_END
};
#endif
