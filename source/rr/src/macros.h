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

#ifndef EDUKE32_MACROS_H_
#define EDUKE32_MACROS_H_

#include "mmulti.h"

// Macros, some from SW source

#ifndef ZOFFSET3
# define ZOFFSET3 (8<<8)
#endif

int RT_KRand2(void);
int32_t A_InsertSprite(int16_t whatsect, int32_t s_x, int32_t s_y, int32_t s_z, int16_t s_pn, int8_t s_s, uint8_t s_xr,
                       uint8_t s_yr, int16_t s_a, int16_t s_ve, int16_t s_zv, int16_t s_ow, int16_t s_ss);

static FORCE_INLINE int32_t _krand(void)
{
    randomseed = (randomseed * 27584621ul) + 1ul;
    return ((uint32_t) randomseed)>>16;
}

static FORCE_INLINE int32_t krand2(void)
{
    if (REALITY)
        return RT_KRand2();
    return _krand();
}

#define BGSTRETCH (ud.bgstretch ? 1024 : 0)

#define WIN_IS_PRESSED ( KB_KeyPressed( sc_RightWin ) || KB_KeyPressed( sc_LeftWin ) )
#define ALT_IS_PRESSED ( KB_KeyPressed( sc_RightAlt ) || KB_KeyPressed( sc_LeftAlt ) )
#define SHIFTS_IS_PRESSED ( KB_KeyPressed( sc_RightShift ) || KB_KeyPressed( sc_LeftShift ) )

static inline void RANDOMSCRAP(spritetype *s, int i)
{
    int32_t r1 = krand2(), r2 = krand2(), r3 = krand2(), r4 = krand2(), r5 = krand2(), r6 = krand2(), r7 = krand2();
    if (REALITY)
    {
        swap(&r1, &r7);
        swap(&r2, &r6);
        swap(&r3, &r5);
    }
    A_InsertSprite(s->sectnum,s->x+(r7&255)-128,s->y+(r6&255)-128,s->z-ZOFFSET3-(r5&8191),
        SCRAP6+(r4&15),-8,RR?16:48,RR?16:48,r3&2047,(r2&63)+64,-512-(r1&2047),i,5);
}

#define GTFLAGS(x) (g_gametypeFlags[ud.coop] & x)

#define TRAVERSE_SPRITE_SECT(l, o, n)    (o) = (l); ((o) != -1) && ((n) = nextspritesect[o]); (o) = (n)
#define TRAVERSE_SPRITE_STAT(l, o, n)    (o) = (l); ((o) != -1) && ((n) = nextspritestat[o]); (o) = (n)
#define TRAVERSE_CONNECT(i)              i = 0; i != -1; i = connectpoint2[i]

#define TEST(flags,mask) ((flags) & (mask))
#define SET(flags,mask) ((flags) |= (mask))
#define RESET(flags,mask) ((flags) &= ~(mask))
#define FLIP(flags,mask) ((flags) ^= (mask))

// mask definitions

#define BIT(shift)     (1u<<(shift))

#define TEST_SYNC_KEY(bits, sync_num) (!!TEST((bits), BIT(sync_num)))

#define AFLAMABLE(X) (X==BOX||X==TREE1||X==TREE2||X==TIRE||X==CONE)
#define rnd(X) ((krand2()>>8)>=(255-(X)))

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

#define CSTAT_WALL_BLOCK            BIT(0)
#define CSTAT_WALL_BOTTOM_SWAP      BIT(1)
#define CSTAT_WALL_ALIGN_BOTTOM     BIT(2)
#define CSTAT_WALL_XFLIP            BIT(3)
#define CSTAT_WALL_MASKED           BIT(4)
#define CSTAT_WALL_1WAY             BIT(5)
#define CSTAT_WALL_BLOCK_HITSCAN    BIT(6)
#define CSTAT_WALL_TRANSLUCENT      BIT(7)
#define CSTAT_WALL_YFLIP            BIT(8)
#define CSTAT_WALL_TRANS_FLIP       BIT(9)
#define CSTAT_WALL_BLOCK_ACTOR (BIT(14)) // my def
#define CSTAT_WALL_WARP_HITSCAN (BIT(15)) // my def

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
//       bit 11: 1 = determine shade based only on its own shade member (see CON's spritenoshade command), i.e.
//                   don't take over shade from parallaxed ceiling/nonparallaxed floor
//                   (NOTE: implemented on the game side)
//       bit 12: reserved
//       bit 13: reserved
//       bit 14: reserved
//       bit 15: 1 = Invisible sprite, 0 = not invisible
#define CSTAT_SPRITE_NOSHADE        BIT(11)
#define CSTAT_SPRITE_BREAKABLE (CSTAT_SPRITE_BLOCK_HITSCAN)

#define SP(i)  sprite[i].yvel
#define SX(i)  sprite[i].x
#define SY(i)  sprite[i].y
#define SZ(i)  sprite[i].z
#define SS(i)  sprite[i].shade
#define PN(i)  sprite[i].picnum
#define SA(i)  sprite[i].ang
//#define SV  sprite[i].xvel
//#define ZV  sprite[i].zvel
//#define RX  sprite[i].xrepeat
//#define RY  sprite[i].yrepeat
#define OW(i)  sprite[i].owner
#define CS(i)  sprite[i].cstat
#define SH(i)  sprite[i].extra
//#define CX  sprite[i].xoffset
//#define CY  sprite[i].yoffset
//#define CD  sprite[i].clipdist
//#define PL  sprite[i].pal
#define SLT(i)  sprite[i].lotag
#define SHT(i)  sprite[i].hitag
#define SECT(i) sprite[i].sectnum

#define T1(i)  actor[i].t_data[0]
#define T2(i)  actor[i].t_data[1]
#define T3(i)  actor[i].t_data[2]
#define T4(i)  actor[i].t_data[3]
#define T5(i)  actor[i].t_data[4]
#define T6(i)  actor[i].t_data[5]

#endif
