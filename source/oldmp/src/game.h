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

static inline int G_GetMusicIdx(const char* str)
{
    int32_t lev, ep;
    signed char b1, b2;

    int numMatches = sscanf(str, "%c%d%c%d", &b1, &ep, &b2, &lev);

    if (numMatches != 4 || Btoupper(b1) != 'E' || Btoupper(b2) != 'L')
        return -1;

    if ((unsigned)--lev >= MAXLEVELS)
        return -2;

    if (ep == 0)
        return (MAXVOLUMES * MAXLEVELS) + lev;

    if ((unsigned)--ep >= MAXVOLUMES)
        return -2;

    return (ep * MAXLEVELS) + lev;
}

static inline int32_t calc_smoothratio(ClockTicks const totalclk, ClockTicks const ototalclk)
{
    if (ud.pause_on || ud.show_help /*|| g_player[myconnectindex].ps->gm & MODE_MENU*/)
        return 65536;

    return calc_smoothratio(totalclk, ototalclk, REALGAMETICSPERSEC);
}

static inline int32_t get_xdim(int32_t x)
{
    const int32_t screenwidth = scale(240 << 16, xdim, ydim);
    return scale((x << 16) + (screenwidth >> 1) - (160 << 16), xdim, screenwidth);
}
static inline int32_t get_ydim(int32_t y)
{
    y = mulscale16((y << 16) + rotatesprite_y_offset - (200 << 15), rotatesprite_yxaspect) + (200 << 15);
    return scale(y, ydim, 200 << 16);
}

#endif