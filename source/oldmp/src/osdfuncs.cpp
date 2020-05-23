#include "duke3d.h"
#include "build.h"
#include "namesdyn.h"
#include "osdfuncs.h"
#include "compat.h"

int osdhightile = 0;
float osdscale = 1.f, osdrscale = 1.f;

#define OSD_SCALE(x) (int32_t)(osdscale != 1.f ? Blrintf(osdscale*(float)(x)) : (x))
#define OSD_SCALEDIV(x) (int32_t)Blrintf((float)(x) * osdrscale)
#define OSDCHAR_WIDTH (tilesiz[STARTALPHANUM + 'A' - '!'].x)
#define OSDCHAR_HEIGHT (tilesiz[STARTALPHANUM + 'A' - '!'].y + 1)

static inline int32_t GAME_isspace(int32_t ch)
{
    return (ch == 32 || ch == 9);
}

static inline int32_t GAME_getchartile(int32_t ch)
{
    const int32_t ac = ch - '!' + STARTALPHANUM;
    return (ac < STARTALPHANUM || ac > ENDALPHANUM) ? -1 : ac;
}

void GAME_drawosdchar(int32_t x, int32_t y, char ch, int32_t shade, int32_t pal)
{
    int16_t ac;
#ifndef USE_OPENGL
    int32_t usehightile = 0;
#endif
    int32_t ht = usehightile;

    if (GAME_isspace(ch)) return;
    if ((ac = GAME_getchartile(ch)) == -1)
        return;

    usehightile = (osdhightile && ht);
    rotatesprite_fs(OSD_SCALE((OSDCHAR_WIDTH*x) << 16),
        OSD_SCALE((y*OSDCHAR_HEIGHT) << 16),
        OSD_SCALE(65536.f), 0, ac, shade, pal, 8 | 16);
    usehightile = ht;
}

void GAME_drawosdstr(int32_t x, int32_t y, const char *ch, int32_t len, int32_t shade, int32_t pal)
{
    int16_t ac;
#ifdef USE_OPENGL
    const int32_t ht = usehightile;
    usehightile = (osdhightile && ht);
#endif

    x *= OSDCHAR_WIDTH;

    do
    {
        if (!GAME_isspace(*ch))
            if ((ac = GAME_getchartile(*ch)) >= 0)
            {
                OSD_GetShadePal(ch, &shade, &pal);
                rotatesprite_fs(OSD_SCALE(x << 16), OSD_SCALE((y*OSDCHAR_HEIGHT) << 16),
                    OSD_SCALE(65536.f), 0, ac, shade, pal, 8 | 16);
            }

        x += OSDCHAR_WIDTH;
        ch++;
    } while (--len);

#ifdef USE_OPENGL
    usehightile = ht;
#endif
}

void GAME_drawosdcursor(int32_t x, int32_t y, int32_t type, int32_t lastkeypress)
{
    int16_t ac;

    if (type) ac = SMALLFNTCURSOR;
    else ac = '_' - '!' + STARTALPHANUM;

    if (((BGetTime() - lastkeypress) & 0x40) == 0)
        rotatesprite_fs(OSD_SCALE((OSDCHAR_WIDTH*x) << 16),
            OSD_SCALE(((y*OSDCHAR_HEIGHT) + (type ? -1 : 2)) << 16),
            OSD_SCALE(65536.f), 0, ac, 0, 8, 8 | 16);
}

int32_t GAME_getcolumnwidth(int32_t w)
{
    return OSD_SCALEDIV(w / OSDCHAR_WIDTH);
}

int32_t GAME_getrowheight(int32_t h)
{
    return OSD_SCALEDIV(h / OSDCHAR_HEIGHT);
}

//#define BGTILE 311
//#define BGTILE 1156
#define BGTILE 1141	// BIGHOLE
#define BGTILE_SIZEX 128
#define BGTILE_SIZEY 128
#define BORDTILE 3250	// VIEWBORDER
#define BITSTH 1+32+8+16	// high translucency
#define BITSTL 1+8+16	// low translucency
#define BITS 8+16+64		// solid
#define SHADE 0
#define PALETTE 4

void GAME_onshowosd(int shown)
{
    // fix for TCs like Layre which don't have the BGTILE for some reason
    // most of this is copied from my dummytile stuff in defs.c
    if (!tilesiz[BGTILE].x || !tilesiz[BGTILE].y)
    {
        int j;

        tilesiz[BGTILE].x = BGTILE_SIZEX;
        tilesiz[BGTILE].y = BGTILE_SIZEY;
        faketile[BGTILE] = 1;
        // OLDMP FIXME
        //picanm[BGTILE] = 0;

        j = 15; while ((j > 1) && (pow2long[j] > BGTILE_SIZEX)) j--;
        picsiz[BGTILE] = ((char)j);
        j = 15; while ((j > 1) && (pow2long[j] > BGTILE_SIZEY)) j--;
        picsiz[BGTILE] += ((char)(j<<4));
    }

    G_UpdateScreenArea();
    if (numplayers == 1)
        if ((shown && !ud.pause_on) || (!shown && ud.pause_on))
            KB_KeyDown[sc_Pause] = 1;
}

void GAME_clearbackground(int c, int r)
{
    int x, y, xsiz, ysiz, tx2, ty2;
    int daydim, bits;

    UNREFERENCED_PARAMETER(c);

    if (videoGetRenderMode() < 3) bits = BITS;
    else bits = BITSTL;

    daydim = r<<3;

    xsiz = tilesiz[BGTILE].x;
    tx2 = xdim/xsiz;
    ysiz = tilesiz[BGTILE].y;
//    ty2 = ydim/ysiz;
    ty2 = daydim/ysiz;

    for (x=tx2;x>=0;x--)
        for (y=ty2;y>=0;y--)
//        for (y=ty2+1;y>=1;y--)
//            rotatesprite(x*xsiz<<16,((daydim-ydim)+(y*ysiz))<<16,65536L,0,BGTILE,SHADE,PALETTE,bits,0,0,xdim,daydim);
            rotatesprite(x*xsiz<<16,y*ysiz<<16,65536L,0,BGTILE,SHADE,PALETTE,bits,0,0,xdim,daydim);

    xsiz = tilesiz[BORDTILE].y;
    tx2 = xdim/xsiz;
    ysiz = tilesiz[BORDTILE].x;

    for (x=tx2;x>=0;x--)
        rotatesprite(x*xsiz<<16,(daydim+ysiz+1)<<16,65536L,1536,BORDTILE,SHADE-12,PALETTE,BITS,0,0,xdim,daydim+ysiz+1);
}

