#ifndef _GAME_H
#define _GAME_H

extern char boardfilename[BMAX_PATH];
extern char currentboardfilename[BMAX_PATH];
const char* G_DefaultRtsFile(void);

#define USERMAPMUSICFAKEVOLUME MAXVOLUMES
#define USERMAPMUSICFAKELEVEL (MAXLEVELS-1)
#define USERMAPMUSICFAKESLOT ((USERMAPMUSICFAKEVOLUME * MAXLEVELS) + USERMAPMUSICFAKELEVEL)

#define VIEWSCREEN_ACTIVE_DISTANCE 2048

#if defined(GEKKO) || defined(__OPENDINGUX__)
# define VIEWSCREENFACTOR 0
#elif defined(__ANDROID__)
# define VIEWSCREENFACTOR 1
#else
# define VIEWSCREENFACTOR 2
#endif

// Forward declarations
#ifdef __cplusplus
extern "C" {
#endif

void M32RunScript(const char* s); // Needed, because reasons.

#ifdef __cplusplus
}
#endif

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

static inline bool G_CheckFemSprite(int32_t spriteNum)
{
    switch (DynamicTileMap[sprite[spriteNum].picnum])
    {
        case FEM1__STATIC:
        case FEM2__STATIC:
        case FEM3__STATIC:
        case FEM4__STATIC:
        case FEM5__STATIC:
        case FEM6__STATIC:
        case FEM7__STATIC:
        case FEM8__STATIC:
        case FEM9__STATIC:
        case FEM10__STATIC:
        case PODFEM1__STATIC:
        case NAKED1__STATIC:
        case STATUE__STATIC:
        case TOUGHGAL__STATIC:
            return true;
    }

    return false;
}

static inline vec3_t G_GetCameraPosition(int32_t const i, int32_t const smoothratio)
{
    auto const cs = (uspriteptr_t)&sprite[i];
    const ActorData_t* const ca = &actor[i];

    return { ca->bpos.x + mulscale16(cs->x - ca->bpos.x, smoothratio),
             ca->bpos.y + mulscale16(cs->y - ca->bpos.y, smoothratio),
             ca->bpos.z + mulscale16(cs->z - ca->bpos.z, smoothratio) };
}

template <typename T>
static inline int G_GetViewscreenSizeShift(T const* spr)
{
#if VIEWSCREENFACTOR == 0
    UNREFERENCED_PARAMETER(spr);
    return VIEWSCREENFACTOR;
#else
    static CONSTEXPR int const mask = (1 << VIEWSCREENFACTOR) - 1;
    const int rem = (spr->xrepeat & mask) | (spr->yrepeat & mask);

    for (int i = 0; i < VIEWSCREENFACTOR; i++)
        if (rem & (1 << i))
            return i;

    return VIEWSCREENFACTOR;
#endif
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