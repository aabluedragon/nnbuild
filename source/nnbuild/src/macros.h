#ifndef MACROS_H
#define MACROS_H

// Macros, some from SW source

#define TRAVERSE_SPRITE_SECT(l, o, n)    for ((o) = (l); (n) = nextspritesect[o], (o) != -1; (o) = (n))
#define TRAVERSE_SPRITE_STAT(l, o, n)    for ((o) = (l); (n) = nextspritestat[o], (o) != -1; (o) = (n))
#define TRAVERSE_CONNECT(i)   for (i = connecthead; i != -1; i = connectpoint2[i])

#define TEST(flags,mask) ((flags) & (mask))
#define SET(flags,mask) ((flags) |= (mask))
#define RESET(flags,mask) ((flags) &= ~(mask))
#define FLIP(flags,mask) ((flags) ^= (mask))

// mask definitions

#define BIT(shift)     (1<<(shift))

#define TEST_SYNC_KEY(bits, sync_num) TEST(bits, BIT(sync_num))

#define IFWITHIN(B,E) if((PN(i))>=(B) && (PN(i))<=(E))
#define IFMOVING if(A_SetSprite(i,CLIPMASK0))
#define IFHIT(i) j=A_IncurDamage(i);if(j >= 0)
#define AFLAMABLE(X) (X==BOX||X==TREE1||X==TREE2||X==TIRE||X==CONE)
#define rnd(X) ((krand()>>8)>=(255-(X)))

//
// NETWORK - REDEFINABLE SHARED (SYNC) KEYS BIT POSITIONS
//

#define SK_JUMP         0
#define SK_CROUCH       1
#define SK_FIRE         2
#define SK_AIM_UP       3
#define SK_AIM_DOWN     4
#define SK_RUN          5
#define SK_LOOK_LEFT    6
#define SK_LOOK_RIGHT   7

// weapons take up 4 bits...
#define SK_WEAPON_BITS  8
#define SK_WEAPON_BITS1 9
#define SK_WEAPON_BITS2 10
#define SK_WEAPON_BITS3 11
#define SK_WEAPON_MASK ((1 << SK_WEAPON_BITS) | (1 << SK_WEAPON_BITS1) | (1 << SK_WEAPON_BITS2) | (1 << SK_WEAPON_BITS3))

#define SK_STEROIDS     12
#define SK_LOOK_UP      13
#define SK_LOOK_DOWN    14
#define SK_NIGHTVISION  15
#define SK_MEDKIT       16
#define SK_MULTIFLAG    17
#define SK_CENTER_VIEW  18
#define SK_HOLSTER      19
#define SK_INV_LEFT     20
#define SK_PAUSE        21
#define SK_QUICK_KICK   22
#define SK_AIMMODE      23
#define SK_HOLODUKE     24
#define SK_JETPACK      25
#define SK_GAMEQUIT     26
#define SK_INV_RIGHT    27
#define SK_TURNAROUND   28
#define SK_OPEN         29
#define SK_INVENTORY    30
#define SK_ESCAPE       31
    
#include "../../build/include/game_stat_flags_shared.h"

//cstat, bit 0: 1 = Blocking sprite (use with clipmove, getzrange)    "B"
//       bit 1: 1 = 50/50 transluscence, 0 = normal                   "T"
//       bit 2: 1 = x-flipped, 0 = normal                             "F"
//       bit 3: 1 = y-flipped, 0 = normal                             "F"
//       bits 5-4: 00 = FACE sprite (default)                         "R"
//                 01 = WALL sprite (like masked walls)
//                 10 = FLOOR sprite (parallel to ceilings&floors)
//                 11 = SPIN sprite (face sprite that can spin 2draw style - not done yet)
//       bit 6: 1 = 1-sided sprite, 0 = normal                        "1"
//       bit 7: 1 = Real centered centering, 0 = foot center          "C"
//       bit 8: 1 = Blocking sprite (use with hitscan)                "H"
//       bit 9: reserved
//       bit 10: reserved
//       bit 11: reserved
//       bit 12: reserved
//       bit 13: reserved
//       bit 14: reserved
//       bit 15: 1 = Invisible sprite, 0 = not invisible
#define CSTAT_SPRITE_BREAKABLE (CSTAT_SPRITE_BLOCK_HITSCAN)

#define SP(i)  sprite[i].yvel
#define SX(i)  sprite[i].x
#define SY(i)  sprite[i].y
#define SZ(i)  sprite[i].z
#define SS(i)  sprite[i].shade
#define PN(i)  sprite[i].picnum
#define SA(i)  sprite[i].ang
//#define SV(i)  sprite[i].xvel
//#define ZV(i)  sprite[i].zvel
//#define RX(i)  sprite[i].xrepeat
//#define RY(i)  sprite[i].yrepeat
#define OW(i)  sprite[i].owner
#define CS(i)  sprite[i].cstat
#define SH(i)  sprite[i].extra
//#define CX(i)  sprite[i].xoffset
//#define CY(i)  sprite[i].yoffset
//#define CD(i)  sprite[i].clipdist
//#define PL(i)  sprite[i].pal
#define SLT(i)  sprite[i].lotag
#define SHT(i)  sprite[i].hitag
#define SECT(i) sprite[i].sectnum

#define T1(i)  actor[i].t_data[0]
#define T2(i)  actor[i].t_data[1]
#define T3(i)  actor[i].t_data[2]
#define T4(i)  actor[i].t_data[3]
#define T5(i)  actor[i].t_data[4]
#define T6(i)  actor[i].t_data[5]
#define T7(i)  actor[i].t_data[6]
#define T8(i)  actor[i].t_data[7]
#define T9(i)  actor[i].t_data[8]

#endif // MACROS_H
