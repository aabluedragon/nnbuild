#ifndef _GAME_H
#define _GAME_H

extern char boardfilename[BMAX_PATH];
extern char currentboardfilename[BMAX_PATH];
const char* G_DefaultRtsFile(void);

#define USERMAPMUSICFAKEVOLUME MAXVOLUMES
#define USERMAPMUSICFAKELEVEL (MAXLEVELS-1)
#define USERMAPMUSICFAKESLOT ((USERMAPMUSICFAKEVOLUME * MAXLEVELS) + USERMAPMUSICFAKELEVEL)

static inline int G_HaveUserMap(void)
{
    return (boardfilename[0] != 0 && ud.level_number == 7 && ud.volume_number == 0);
}

static inline int Menu_HaveUserMap(void)
{
    return (boardfilename[0] != 0 && ud.m_level_number == 7 && ud.m_volume_number == 0);
}

static inline int32_t G_GetLogoFlags(void)
{
    return Gv_GetVarByLabel("LOGO_FLAGS", 255, -1, -1);
}

static inline int32_t calc_smoothratio(ClockTicks const totalclk, ClockTicks const ototalclk)
{
    if (ud.pause_on || ud.show_help || g_player[myconnectindex].ps->gm & MODE_MENU)
        return 65536;

    return calc_smoothratio(totalclk, ototalclk, REALGAMETICSPERSEC);
}

#endif