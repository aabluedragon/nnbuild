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
//#include "types.h"
#include "scriplib.h"
//#include "file_lib.h"
//#include "mathutil.h"
#include "gamedefs.h"
#include "keyboard.h"
#include "mouse.h"  // JBF 20030809
#include "function.h"
#include "control.h"
#include "fx_man.h"
#include "sounds.h"
#include "config.h"
#include "osd.h"
#include "osdfuncs.h"
#include "osdcmds.h"
#include "scriptfile.h"
#include "grpscan.h"
#include "gamedef.h"
#include "kplib.h"
//#include "crc32.h"
//#include "util_lib.h"
#include "hightile.h"
#include "al_midi.h"
#include "colmatch.h"
#include "renderlayer.h"

#ifdef DISCORD_RPC
#include "discord.h"
#endif

#ifdef _WIN32
#include <shellapi.h>
#define UPDATEINTERVAL 604800 // 1w
#include "winbits.h"
#else
static int usecwd = 0;
#endif /* _WIN32 */
int g_scriptSanityChecks = 1;

#define IDFSIZE 479985668
#define IDFILENAME "DUKE3D.IDF"

const char* AppProperName = APPNAME;
const char* AppTechnicalName = APPBASENAME;

int g_cameraDistance = 0, g_cameraClock = 0;
int qe, cp;
static int g_commandSetup = 0;
int g_noSetup = 0;
int g_noAutoLoad = 0;
static int g_noSound = 0;
static int g_noMusic = 0;

// NetDuke32 TO-DO: Move to cmdline.h when I get a chance.
static const char *CommandMap = NULL;
const char *CommandName = NULL;
#ifndef RANCID_NETWORKING
static const char *CommandNet = NULL;
#endif
static int g_keepAddr = 0;
int CommandWeaponChoice = 0;

static strllist *CommandPaths = NULL, *CommandGrps = NULL;

char boardfilename[BMAX_PATH] = {0}, currentboardfilename[BMAX_PATH] = {0};
char root[BMAX_PATH];

static char firstdemofile[80] = { '\0' };
static int userconfiles = 0;

static int netparamcount = 0;
static char **netparam = NULL;

int recfilep,totalreccnt;
int debug_on = 0,g_noEnemies = 0;

char *rtsptr;

static FILE *frecfilep = (FILE *)NULL;

int g_restorePalette,g_screenCapture;
static int g_noLogoAnim = 0;
static int g_noLogo = 0;

char eduke32dat[BMAX_PATH] = "eduke32.dat";
char defaultduke3dgrp[BMAX_PATH] = "duke3d.grp";
char *duke3dgrp = defaultduke3dgrp;
const char *duke3dgrpstring = NULL;
static char defaultconfilename[BMAX_PATH] = {"EDUKE.CON"};
static char *confilename = defaultconfilename;
const char *duke3ddef = "duke3d.def";
const char *eduke32def = "eduke32.def";
char mod_dir[BMAX_PATH] = "/";

int g_Shareware = 0;
int g_gameType = 0;

int quotebot, quotebotgoal;
int user_quote_time[MAXUSERQUOTES];
char user_quote[MAXUSERQUOTES][178];
// char typebuflen,typebuf[41];

static int MAXCACHE1DSIZE = (96*1024*1024);

int tempwallptr;

static int nonsharedtimer;

static void G_DrawCameraText(short i);
static int G_MoveLoop(void);
static int G_DoMoveThings(void);
static int G_PlaybackDemo(void);

int althud_numbertile = 2930;
int althud_numberpal = 0;
int althud_shadows = 1;
int althud_flashing = 1;
int hud_glowingquotes = 1;
int hud_showmapname = 1;

int g_levelTextTime = 0;

int kopen4loadfrommod(char *filename, char searchfirst)
{
    static char fn[BMAX_PATH];
    int r;

    Bsprintf(fn,"%s/%s",mod_dir,filename);
    r = kopen4load(fn,searchfirst);
    if (r < 0)
        r = kopen4load(filename,searchfirst);
    return r;
}

enum
{
    T_INCLUDE = 0,
    T_INTERFACE = 0,
    T_LOADGRP = 1,
    T_MODE = 1,
    T_CACHESIZE = 2,
    T_ALLOW = 2,
    T_NOAUTOLOAD,
    T_MUSIC,
    T_SOUND,
    T_FILE,
    T_ADDPLAYERCOL,
    T_DELPLAYERCOL,
    T_ID
};

static int getatoken(scriptfile *sf, tokenlist *tl, int ntokens)
{
    char *tok;
    int i;

    if (!sf) return T_ERROR;
    tok = scriptfile_gettoken(sf);
    if (!tok) return T_EOF;

    for (i=ntokens-1;i>=0;i--)
    {
        if (!Bstrcasecmp(tok, tl[i].text))
            return tl[i].tokenid;
    }
    return T_ERROR;
}

void SetIfGreater(int32_t *variable, int32_t potentialValue)
{
    if (potentialValue > *variable)
        *variable = potentialValue;
}

void G_SetStatusBarScale(int sc)
{
    ud.statusbarscale = min(100,max(10,sc));
    G_UpdateScreenArea();
}

static inline int sbarx(int x)
{
    if (ud.screen_size == 4 /*|| ud.statusbarmode == 1*/) return scale(x<<16,ud.statusbarscale,100);
    return (((320l<<16) - scale(320l<<16,ud.statusbarscale,100)) >> 1) + scale(x<<16,ud.statusbarscale,100);
}

static inline int sbarxr(int x)
{
    if (ud.screen_size == 4 /*|| ud.statusbarmode == 1*/) return (320l<<16) - scale(x<<16,ud.statusbarscale,100);
    return (((320l<<16) - scale(320l<<16,ud.statusbarscale,100)) >> 1) + scale(x<<16,ud.statusbarscale,100);
}

static inline int sbary(int y)
{
    return ((200l<<16) - scale(200l<<16,ud.statusbarscale,100) + scale(y<<16,ud.statusbarscale,100));
}

static inline int sbarsc(int sc)
{
    return scale(sc,ud.statusbarscale,100);
}

int32_t sbarx16(int32_t x)
{
    if (ud.screen_size == 4) return sbarsc(x);
    return (((320 << 16) - sbarsc(320 << 16)) >> 1) + sbarsc(x);
}

int32_t sbary16(int32_t y)
{
    return (200 << 16) - sbarsc(200 << 16) + sbarsc(y);
}

static inline int textsc(int sc)
{
    // prevent ridiculousness to a degree
    if (xdim <= 320) return sc;
    else if (xdim <= 640) return scale(sc,min(200,ud.textscale),100);
    else if (xdim <= 800) return scale(sc,min(300,ud.textscale),100);
    else if (xdim <= 1024) return scale(sc,min(350,ud.textscale),100);
    return scale(sc,ud.textscale,100);
}

static void G_PatchStatusBar(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t const scl = sbarsc(65536);
    int32_t const tx = sbarx16((160 << 16) - (tilesiz[BOTTOMSTATUSBAR].x << 15)); // centered
    int32_t const ty = sbary(200 - tilesiz[BOTTOMSTATUSBAR].y);

    int32_t const clx1 = sbarsc(scale(x1, xdim, 320)), cly1 = sbarsc(scale(y1, ydim, 200));
    int32_t const clx2 = sbarsc(scale(x2, xdim, 320)), cly2 = sbarsc(scale(y2, ydim, 200));
    int32_t const clofx = (xdim - sbarsc(xdim)) >> 1, clofy = (ydim - sbarsc(ydim));

    rotatesprite(tx, ty, scl, 0, BOTTOMSTATUSBAR, 4, 0, 10 + 16 + 64, clx1 + clofx, cly1 + clofy, clx2 + clofx - 1, cly2 + clofy - 1);
}

// get the string length until the next '\n'
int32_t G_GetStringLineLength(const char *text, const char *end, const int32_t iter)
{
    int32_t length = 0;

    while (*text != '\n' && text != end)
    {
        ++length;

        text += iter;
    }

    return length;
}

int32_t G_GetStringNumLines(const char *text, const char *end, const int32_t iter)
{
    int32_t count = 1;

    while (text != end)
    {
        if (*text == '\n')
            ++count;
        text += iter;
    }

    return count;
}
// Note: Neither of these care about TEXT_LINEWRAP. This is intended.

// This function requires you to Bfree() the returned char*.
char* G_GetSubString(const char *text, const char *end, const int32_t iter, const int32_t length)
{
    char *line = (char*)Bmalloc((length + 1) * sizeof(char));
    int32_t counter = 0;

    while (counter < length && text != end)
    {
        line[counter] = *text;

        text += iter;
        ++counter;
    }

    line[counter] = '\0';

    return line;
}

// assign the character's tilenum
int32_t G_GetStringTile(int32_t font, char *t, int32_t f)
{
    if (f & TEXT_DIGITALNUMBER)
        return *t - '0' + font; // copied from digitalnumber
    else if (f & (TEXT_BIGALPHANUM | TEXT_GRAYFONT))
    {
        int32_t offset = (f & TEXT_GRAYFONT) ? 26 : 0;

        if (*t >= '0' && *t <= '9')
            return *t - '0' + font + ((f & TEXT_GRAYFONT) ? 26 : -10);
        else if (*t >= 'a' && *t <= 'z')
            return *t - 'a' + font + ((f & TEXT_GRAYFONT) ? -26 : 26);
        else if (*t >= 'A' && *t <= 'Z')
            return *t - 'A' + font;
        else switch (*t)
        {
        case '_':
        case '-':
            return font - (11 + offset);
            break;
        case '.':
            return font + (BIGPERIOD - (BIGALPHANUM + offset));
            break;
        case ',':
            return font + (BIGCOMMA - (BIGALPHANUM + offset));
            break;
        case '!':
            return font + (BIGX - (BIGALPHANUM + offset));
            break;
        case '?':
            return font + (BIGQ - (BIGALPHANUM + offset));
            break;
        case ';':
            return font + (BIGSEMI - (BIGALPHANUM + offset));
            break;
        case ':':
            return font + (BIGCOLIN - (BIGALPHANUM + offset));
            break;
        case '\\':
        case '/':
            return font + (68 - offset); // 3008-2940
            break;
        case '%':
            return font + (69 - offset); // 3009-2940
            break;
        case '`':
        case '\"': // could be better hacked in
        case '\'':
            return font + (BIGAPPOS - (BIGALPHANUM + offset));
            break;
        default: // unknown character
            *t = ' '; // whitespace-ize
            return font;
            break;
        }
    }
    else
        return *t - '!' + font; // uses ASCII order
}

// qstrdim
vec2_t G_ScreenTextSize(const int32_t font,
    int32_t x, int32_t y, const int32_t z, const int32_t blockangle,
    const char *str, const int32_t o,
    int32_t xspace, int32_t yline, int32_t xbetween, int32_t ybetween,
    const int32_t f,
    int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    vec2_t size = { 0, 0, }; // eventually the return value
    vec2_t pos = { 0, 0, }; // holds the coordinate position as we draw each character tile of the string
    vec2_t extent = { 0, 0, }; // holds the x-width of each character and the greatest y-height of each line
    vec2_t offset = { 0, 0, }; // temporary; holds the last movement made in both directions

    int32_t tile;
    char t;

    // set the start and end points depending on direction
    int32_t iter = (f & TEXT_BACKWARDS) ? -1 : 1; // iteration direction

    const char *end;
    const char *text;

    UNREFERENCED_PARAMETER(x1);
    UNREFERENCED_PARAMETER(y1);
    UNREFERENCED_PARAMETER(x2);
    UNREFERENCED_PARAMETER(y2);

    if (str == NULL)
        return size;

    end = (f & TEXT_BACKWARDS) ? str - 1 : Bstrchr(str, '\0');
    text = (f & TEXT_BACKWARDS) ? Bstrchr(str, '\0') - 1 : str;

    // optimization: justification in both directions
    if ((f & TEXT_XJUSTIFY) && (f & TEXT_YJUSTIFY))
    {
        size.x = xbetween;
        size.y = ybetween;
        return size;
    }

    // for best results, we promote 320x200 coordinates to full precision before any math
    if (!(o & ROTATESPRITE_FULL16))
    {
        x <<= 16;
        y <<= 16;
        xspace <<= 16;
        yline <<= 16;
        xbetween <<= 16;
        ybetween <<= 16;
    }
    // coordinate values should be shifted left by 16

    // handle zooming where applicable
    xspace = scale(xspace, z, 65536);
    yline = scale(yline, z, 65536);
    xbetween = scale(xbetween, z, 65536);
    ybetween = scale(ybetween, z, 65536);
    // size/width/height/spacing/offset values should be multiplied or scaled by $z, zoom (since 100% is 65536, the same as 1<<16)

    // loop through the string
    while ((t = *text) && text != end)
    {
        // handle escape sequences
        if (t == '^' && Bisdigit(*(text + iter)) && !(f & TEXT_LITERALESCAPE))
        {
            text += iter + iter;
            if (Bisdigit(*text))
                text += iter;
            continue;
        }

        // handle case bits
        if (f & TEXT_UPPERCASE)
        {
            if (f & TEXT_INVERTCASE) // optimization...?
            { // v^ important that these two ifs remain separate due to the else below
                if (Bisupper(t))
                    t = Btolower(t);
            }
            else if (Bislower(t))
                t = Btoupper(t);
        }
        else if (f & TEXT_INVERTCASE)
        {
            if (Bisupper(t))
                t = Btolower(t);
            else if (Bislower(t))
                t = Btoupper(t);
        }

        // translate the character to a tilenum
        tile = G_GetStringTile(font, &t, f);

        // reset this here because we haven't printed anything yet this loop
        extent.x = 0;

        // reset this here because the act of printing something on this line means that we include the margin above in the total size
        offset.y = 0;

        // handle each character itself in the context of screen drawing
        switch (t)
        {
        case '\t':
        case ' ':
            // width
            extent.x = xspace;

            if (f & (TEXT_INTERNALSPACE | TEXT_TILESPACE))
            {
                char space = '.'; // this is subject to change as an implementation detail
                if (f & TEXT_TILESPACE)
                    space = '\x7F'; // tile after '~'
                tile = G_GetStringTile(font, &space, f);

                extent.x += (tilesiz[tile].x * z);
            }

            // prepare the height // near-CODEDUP the other two near-CODEDUPs for this section
            {
                int32_t tempyextent = yline;

                if (f & (TEXT_INTERNALLINE | TEXT_TILELINE))
                {
                    char line = 'A'; // this is subject to change as an implementation detail
                    if (f & TEXT_TILELINE)
                        line = '\x7F'; // tile after '~'
                    tile = G_GetStringTile(font, &line, f);

                    tempyextent += tilesiz[tile].y * z;
                }

                SetIfGreater(&extent.y, tempyextent);
            }

            if (t == '\t')
                extent.x <<= 2; // *= 4

            break;

        case '\n': // near-CODEDUP "if (wrap)"
                   // save the position
            pos.x -= offset.x;
            SetIfGreater(&size.x, pos.x);

            // reset the position
            pos.x = 0;

            // prepare the height
            {
                int32_t tempyextent = yline;

                if (f & (TEXT_INTERNALLINE | TEXT_TILELINE))
                {
                    char line = 'A'; // this is subject to change as an implementation detail
                    if (f & TEXT_TILELINE)
                        line = '\x7F'; // tile after '~'
                    tile = G_GetStringTile(font, &line, f);

                    tempyextent += tilesiz[tile].y * z;
                }

                SetIfGreater(&extent.y, tempyextent);
            }

            // move down the line height
            if (!(f & TEXT_YOFFSETZERO))
                pos.y += extent.y;

            // reset the current height
            extent.y = 0;

            // line spacing
            offset.y = (f & TEXT_YJUSTIFY) ? 0 : ybetween; // ternary to prevent overflow
            pos.y += offset.y;

            break;

        default:
            // width
            extent.x = tilesiz[tile].x * z;

            // obnoxious hardcoded functionality from gametext
            if ((f & TEXT_GAMETEXTNUMHACK) && t >= '0' && t <= '9')
            {
                char numeral = '0'; // this is subject to change as an implementation detail
                extent.x = (tilesiz[G_GetStringTile(font, &numeral, f)].x - 1) * z;
            }

            // height
            SetIfGreater(&extent.y, (tilesiz[tile].y * z));

            break;
        }

        // incrementing the coordinate counters
        offset.x = 0;

        // advance the x coordinate
        if (!(f & TEXT_XOFFSETZERO))
            offset.x += extent.x;

        // account for text spacing
        if (!((f & TEXT_GAMETEXTNUMHACK) && t >= '0' && t <= '9') // this "if" line ONLY == replicating hardcoded stuff
            && !(f & TEXT_XJUSTIFY)) // to prevent overflow
            offset.x += xbetween;

        // line wrapping
        if ((f & TEXT_LINEWRAP) && !(f & TEXT_XRIGHT) && !(f & TEXT_XCENTER) && blockangle % 512 == 0)
        {
            int32_t wrap = 0;
            const int32_t ang = blockangle % 2048;

            // this is the only place in qstrdim where angle actually affects direction, but only in the wrapping measurement
            switch (ang)
            {
            case 0:
                wrap = (x + (pos.x + offset.x) > (320 << 16)); // ((x2 - USERQUOTE_RIGHTOFFSET)<<16)
                break;
            case 512:
                wrap = (y + (pos.x + offset.x) > (200 << 16)); // ((y2 - USERQUOTE_RIGHTOFFSET)<<16)
                break;
            case 1024:
                wrap = (x - (pos.x + offset.x) < 0); // ((x1 + USERQUOTE_RIGHTOFFSET)<<16)
                break;
            case 1536:
                wrap = (y - (pos.x + offset.x) < 0); // ((y1 + USERQUOTE_RIGHTOFFSET)<<16)
                break;
            }
            if (wrap) // near-CODEDUP "case '\n':"
            {
                // save the position
                SetIfGreater(&size.x, pos.x);

                // reset the position
                pos.x = 0;

                // prepare the height
                {
                    int32_t tempyextent = yline;

                    if (f & (TEXT_INTERNALLINE | TEXT_TILELINE))
                    {
                        char line = 'A'; // this is subject to change as an implementation detail
                        if (f & TEXT_TILELINE)
                            line = '\x7F'; // tile after '~'
                        tile = G_GetStringTile(font, &line, f);

                        tempyextent += tilesiz[tile].y * z;
                    }

                    SetIfGreater(&extent.y, tempyextent);
                }

                // move down the line height
                if (!(f & TEXT_YOFFSETZERO))
                    pos.y += extent.y;

                // reset the current height
                extent.y = 0;

                // line spacing
                offset.y = (f & TEXT_YJUSTIFY) ? 0 : ybetween; // ternary to prevent overflow
                pos.y += offset.y;
            }
            else
                pos.x += offset.x;
        }
        else
            pos.x += offset.x;

        // save some trouble with calculation
        if (!(f & TEXT_XOFFSETZERO))
            offset.x -= extent.x;

        // iterate to the next character in the string
        text += iter;
    }

    // calculate final size
    pos.x -= offset.x;
    if (f & TEXT_XOFFSETZERO)
        pos.x += extent.x;
    pos.y -= offset.y;
    pos.y += extent.y;

    SetIfGreater(&size.x, pos.x);
    SetIfGreater(&size.y, pos.y);

    // justification where only one of the two directions is set, so we have to iterate
    if (f & TEXT_XJUSTIFY)
        size.x = xbetween;
    if (f & TEXT_YJUSTIFY)
        size.y = ybetween;

    // return values in the same manner we receive them
    if (!(o & ROTATESPRITE_FULL16))
    {
        size.x >>= 16;
        size.y >>= 16;
    }

    return size;
}

void G_AddCoordsFromRotation(vec2_t *coords, const vec2_t *unitDirection, const int32_t magnitude)
{
    coords->x += scale(magnitude, unitDirection->x, 16384);
    coords->y += scale(magnitude, unitDirection->y, 16384);
}

// screentext
vec2_t G_ScreenText(const int32_t font,
    int32_t x, int32_t y, const int32_t z, const int32_t blockangle, const int32_t charangle,
    const char *str, const int32_t shade, int32_t pal, int32_t o, const int32_t alpha,
    int32_t xspace, int32_t yline, int32_t xbetween, int32_t ybetween, const int32_t f,
    const int32_t x1, const int32_t y1, const int32_t x2, const int32_t y2)
{
    vec2_t size = { 0, 0, }; // eventually the return value
    vec2_t origin = { 0, 0, }; // where to start, depending on the alignment
    vec2_t pos = { 0, 0, }; // holds the coordinate position as we draw each character tile of the string
    vec2_t extent = { 0, 0, }; // holds the x-width of each character and the greatest y-height of each line
    const vec2_t Xdirection = { sintable[(blockangle + 512) & 2047], sintable[blockangle & 2047], };
    const vec2_t Ydirection = { sintable[(blockangle + 1024) & 2047], sintable[(blockangle + 512) & 2047], };

    int32_t tile;
    char t;

    // set the start and end points depending on direction
    int32_t iter = (f & TEXT_BACKWARDS) ? -1 : 1; // iteration direction

    const char *end;
    const char *text;

    if (str == NULL)
        return size;

    end = (f & TEXT_BACKWARDS) ? str - 1 : Bstrchr(str, '\0');
    text = (f & TEXT_BACKWARDS) ? Bstrchr(str, '\0') - 1 : str;

    // for best results, we promote 320x200 coordinates to full precision before any math
    if (!(o & ROTATESPRITE_FULL16))
    {
        x <<= 16;
        y <<= 16;
        xspace <<= 16;
        yline <<= 16;
        xbetween <<= 16;
        ybetween <<= 16;
    }
    // coordinate values should be shifted left by 16

    // eliminate conflicts, necessary here to get the correct size value
    // especially given justification's special handling in G_ScreenTextSize()
    if ((f & TEXT_XRIGHT) || (f & TEXT_XCENTER) || (f & TEXT_XJUSTIFY) || (f & TEXT_YJUSTIFY) || blockangle % 512 != 0)
        o &= ~TEXT_LINEWRAP;

    // size is the return value, and we need it for alignment
    size = G_ScreenTextSize(font, x, y, z, blockangle, str, o | ROTATESPRITE_FULL16, xspace, yline, (f & TEXT_XJUSTIFY) ? 0 : xbetween, (f & TEXT_YJUSTIFY) ? 0 : ybetween, f & ~(TEXT_XJUSTIFY | TEXT_YJUSTIFY), x1, y1, x2, y2);

    // handle zooming where applicable
    xspace = scale(xspace, z, 65536);
    yline = scale(yline, z, 65536);
    xbetween = scale(xbetween, z, 65536);
    ybetween = scale(ybetween, z, 65536);
    // size/width/height/spacing/offset values should be multiplied or scaled by $z, zoom (since 100% is 65536, the same as 1<<16)

    // alignment
    // near-CODEDUP "case '\n':"
    {
        int32_t lines = G_GetStringNumLines(text, end, iter);

        if ((f & TEXT_XJUSTIFY) || (f & TEXT_XRIGHT) || (f & TEXT_XCENTER))
        {
            const int32_t length = G_GetStringLineLength(text, end, iter);

            int32_t linewidth = size.x;

            if (lines != 1)
            {
                char *line = G_GetSubString(text, end, iter, length);

                linewidth = G_ScreenTextSize(font, x, y, z, blockangle, line, o | ROTATESPRITE_FULL16, xspace, yline, 0, 0, f & ~(TEXT_XJUSTIFY | TEXT_YJUSTIFY | TEXT_BACKWARDS), x1, y1, x2, y2).x;

                Bfree(line);
            }

            if (f & TEXT_XJUSTIFY)
            {
                size.x = xbetween;

                xbetween = (length == 1) ? 0 : ((xbetween - linewidth) / (length - 1));

                linewidth = size.x;
            }

            if (f & TEXT_XRIGHT)
                origin.x = -linewidth;
            else if (f & TEXT_XCENTER)
                origin.x = -(linewidth / 2);
        }

        if (f & TEXT_YJUSTIFY)
        {
            const int32_t tempswap = ybetween;
            ybetween = (lines == 1) ? 0 : ((ybetween - size.y) / (lines - 1));
            size.y = tempswap;
        }

        if (f & TEXT_YBOTTOM)
            origin.y = -size.y;
        else if (f & TEXT_YCENTER)
            origin.y = -(size.y / 2);
    }

    // loop through the string
    while ((t = *text) && text != end)
    {
        int32_t orientation = o;
        int32_t angle = blockangle + charangle;

        // handle escape sequences
        if (t == '^' && Bisdigit(*(text + iter)) && !(f & TEXT_LITERALESCAPE))
        {
            char smallbuf[4];

            text += iter;
            smallbuf[0] = *text;

            text += iter;
            if (Bisdigit(*text))
            {
                smallbuf[1] = *text;
                smallbuf[2] = '\0';
                text += iter;
            }
            else
                smallbuf[1] = '\0';

            if (!(f & TEXT_IGNOREESCAPE))
                pal = Batoi(smallbuf);

            continue;
        }

        // handle case bits
        if (f & TEXT_UPPERCASE)
        {
            if (f & TEXT_INVERTCASE) // optimization...?
            { // v^ important that these two ifs remain separate due to the else below
                if (Bisupper(t))
                    t = Btolower(t);
            }
            else if (Bislower(t))
                t = Btoupper(t);
        }
        else if (f & TEXT_INVERTCASE)
        {
            if (Bisupper(t))
                t = Btolower(t);
            else if (Bislower(t))
                t = Btoupper(t);
        }

        // translate the character to a tilenum
        tile = G_GetStringTile(font, &t, f);

        switch (t)
        {
        case '\t':
        case ' ':
        case '\n':
        case '\x7F':
            break;

        default:
        {
            vec2_t location = { x, y, };

            G_AddCoordsFromRotation(&location, &Xdirection, origin.x);
            G_AddCoordsFromRotation(&location, &Ydirection, origin.y);

            G_AddCoordsFromRotation(&location, &Xdirection, pos.x);
            G_AddCoordsFromRotation(&location, &Ydirection, pos.y);

            rotatesprite_(location.x, location.y, z, angle, tile, shade, pal, 2 | orientation, alpha, 0, x1, y1, x2, y2);

            break;
        }
        }

        // reset this here because we haven't printed anything yet this loop
        extent.x = 0;

        // handle each character itself in the context of screen drawing
        switch (t)
        {
        case '\t':
        case ' ':
            // width
            extent.x = xspace;

            if (f & (TEXT_INTERNALSPACE | TEXT_TILESPACE))
            {
                char space = '.'; // this is subject to change as an implementation detail
                if (f & TEXT_TILESPACE)
                    space = '\x7F'; // tile after '~'
                tile = G_GetStringTile(font, &space, f);

                extent.x += (tilesiz[tile].x * z);
            }

            // prepare the height // near-CODEDUP the other two near-CODEDUPs for this section
            {
                int32_t tempyextent = yline;

                if (f & (TEXT_INTERNALLINE | TEXT_TILELINE))
                {
                    char line = 'A'; // this is subject to change as an implementation detail
                    if (f & TEXT_TILELINE)
                        line = '\x7F'; // tile after '~'
                    tile = G_GetStringTile(font, &line, f);

                    tempyextent += tilesiz[tile].y * z;
                }

                SetIfGreater(&extent.y, tempyextent);
            }

            if (t == '\t')
                extent.x <<= 2; // *= 4

            break;

        case '\n': // near-CODEDUP "if (wrap)"
                   // reset the position
            pos.x = 0;

            // prepare the height
            {
                int32_t tempyextent = yline;

                if (f & (TEXT_INTERNALLINE | TEXT_TILELINE))
                {
                    char line = 'A'; // this is subject to change as an implementation detail
                    if (f & TEXT_TILELINE)
                        line = '\x7F'; // tile after '~'
                    tile = G_GetStringTile(font, &line, f);

                    tempyextent += tilesiz[tile].y * z;
                }

                SetIfGreater(&extent.y, tempyextent);
            }

            // move down the line height
            if (!(f & TEXT_YOFFSETZERO))
                pos.y += extent.y;

            // reset the current height
            extent.y = 0;

            // line spacing
            pos.y += ybetween;

            // near-CODEDUP "alignments"
            if ((f & TEXT_XJUSTIFY) || (f & TEXT_XRIGHT) || (f & TEXT_XCENTER))
            {
                const int32_t length = G_GetStringLineLength(text, end, iter);

                char *line = G_GetSubString(text, end, iter, length);

                int32_t linewidth = G_ScreenTextSize(font, x, y, z, blockangle, line, o | ROTATESPRITE_FULL16, xspace, yline, 0, 0, f & ~(TEXT_XJUSTIFY | TEXT_YJUSTIFY | TEXT_BACKWARDS), x1, y1, x2, y2).x;

                Bfree(line);

                if (f & TEXT_XJUSTIFY)
                {
                    xbetween = (length == 1) ? 0 : ((xbetween - linewidth) / (length - 1));

                    linewidth = size.x;
                }

                if (f & TEXT_XRIGHT)
                    origin.x = -linewidth;
                else if (f & TEXT_XCENTER)
                    origin.x = -(linewidth / 2);
            }

            break;

        default:
            // width
            extent.x = tilesiz[tile].x * z;

            // obnoxious hardcoded functionality from gametext
            if ((f & TEXT_GAMETEXTNUMHACK) && t >= '0' && t <= '9')
            {
                char numeral = '0'; // this is subject to change as an implementation detail
                extent.x = (tilesiz[G_GetStringTile(font, &numeral, f)].x - 1) * z;
            }

            // height
            SetIfGreater(&extent.y, (tilesiz[tile].y * z));

            break;
        }

        // incrementing the coordinate counters
        {
            int32_t xoffset = 0;

            // advance the x coordinate
            if (!(f & TEXT_XOFFSETZERO))
                xoffset += extent.x;

            // account for text spacing
            if (!((f & TEXT_GAMETEXTNUMHACK) && t >= '0' && t <= '9')) // this "if" line ONLY == replicating hardcoded stuff
                xoffset += xbetween;

            // line wrapping
            if (f & TEXT_LINEWRAP)
            {
                int32_t wrap = 0;
                const int32_t ang = blockangle % 2048;

                // it's safe to make some assumptions and not go through G_AddCoordsFromRotation() since we limit to four directions
                switch (ang)
                {
                case 0:
                    wrap = (x + (pos.x + xoffset) > (320 << 16)); // ((x2 - USERQUOTE_RIGHTOFFSET)<<16)
                    break;
                case 512:
                    wrap = (y + (pos.x + xoffset) > (200 << 16)); // ((y2 - USERQUOTE_RIGHTOFFSET)<<16)
                    break;
                case 1024:
                    wrap = (x - (pos.x + xoffset) < 0); // ((x1 + USERQUOTE_RIGHTOFFSET)<<16)
                    break;
                case 1536:
                    wrap = (y - (pos.x + xoffset) < 0); // ((y1 + USERQUOTE_RIGHTOFFSET)<<16)
                    break;
                }
                if (wrap) // near-CODEDUP "case '\n':"
                {
                    // reset the position
                    pos.x = 0;

                    // prepare the height
                    {
                        int32_t tempyextent = yline;

                        if (f & (TEXT_INTERNALLINE | TEXT_TILELINE))
                        {
                            char line = 'A'; // this is subject to change as an implementation detail
                            if (f & TEXT_TILELINE)
                                line = '\x7F'; // tile after '~'
                            tile = G_GetStringTile(font, &line, f);

                            tempyextent += tilesiz[tile].y * z;
                        }

                        SetIfGreater(&extent.y, tempyextent);
                    }

                    // move down the line height
                    if (!(f & TEXT_YOFFSETZERO))
                        pos.y += extent.y;

                    // reset the current height
                    extent.y = 0;

                    // line spacing
                    pos.y += ybetween;
                }
                else
                    pos.x += xoffset;
            }
            else
                pos.x += xoffset;
        }

        // iterate to the next character in the string
        text += iter;
    }

    // return values in the same manner we receive them
    if (!(o & ROTATESPRITE_FULL16))
    {
        size.x >>= 16;
        size.y >>= 16;
    }

    return size;
}

int32_t G_PrintGameText(int32_t f, int32_t tile, int32_t x, int32_t y, const char *t,
    int32_t s, int32_t p, int32_t o,
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z)
{
    int32_t ac;
    char centre;
    int32_t squishtext = ((f & 2) != 0);
    int32_t shift = 16, widthx = 320;
    int32_t ox, oy, origx = x, origy = y;

    if (t == NULL)
        return -1;

    if (o & 256)
    {
        widthx = 320 << 16;
        shift = 0;
    }

    if ((centre = (x == (widthx >> 1))))
    {
        const char *oldt = t;
        int32_t newx = 0;

        do
        {
            int32_t i;

            if (*t == 32)
            {
                newx += ((((f & 8) ? 8 : 5) - squishtext) * z) >> 16;
                continue;
            }

            if (*t == '^' && isdigit(*(t + 1)))
            {
                t += 1 + isdigit(*(t + 2));
                continue;
            }

            ac = *t - '!' + tile;

            if (ac < tile || ac >(tile + 93)) break;

            newx += i = ((((f & 8) ? 8 : tilesiz[ac].x) - squishtext) * z) >> 16;

            if (*t >= '0' && *t <= '9')
                newx -= i - ((8 * z) >> 16);
        } while (*(++t));

        t = oldt;

        x = (f & 4) ?
            (xres >> 1) - textsc(newx >> 1) :
            (widthx >> 1) - ((o & 256) ? newx << 15 : newx >> 1);
    }

    ox = x;
    oy = y;

    do
    {
        int32_t i;

        if (*t == 32)
        {
            x += ((((f & 8) ? 8 : 5) - squishtext) * z) >> 16;
            continue;
        }

        if (*t == '^' && isdigit(*(t + 1)))
        {
            char smallbuf[4];

            if (!isdigit(*(++t + 1)))
            {
                smallbuf[0] = *(t);
                smallbuf[1] = '\0';
                p = atoi(smallbuf);
                continue;
            }

            smallbuf[0] = *(t++);
            smallbuf[1] = *(t);
            smallbuf[2] = '\0';
            p = atoi(smallbuf);
            continue;
        }

        ac = *t - '!' + tile;

        if (ac < tile || ac >(tile + 93))
            break;

        if (o & 256)
        {
            ox = x += (x - ox) << 16;
            oy = y += (y - oy) << 16;
        }

        if (f & 4)
            rotatesprite(textsc(x << shift), (origy << shift) + textsc((y - origy) << shift), textsc(z),
                0, ac, s, p, (8 | 16 | (o & 1) | (o & 32)), x1, y1, x2, y2);
        else if (f & 1)
            rotatesprite(x << shift, (y << shift), z, 0, ac, s, p, (8 | 16 | (o & 1) | (o & 32)), x1, y1, x2, y2);
        else rotatesprite(x << shift, (y << shift), z, 0, ac, s, p, (2 | o), x1, y1, x2, y2);

        x += i = (f & 8) ?
            ((8 - squishtext) * z) >> 16 :
            ((tilesiz[ac].x - squishtext) * z) >> 16;

        if ((*t >= '0' && *t <= '9'))
            x -= i - ((8 * z) >> 16);

        if ((o & 256) == 0) //  wrapping long strings doesn't work for precise coordinates due to overflow
        {
            if (((f & 4) ? textsc(x) : x) > (ud.config.ScreenWidth - USERQUOTE_RIGHTOFFSET))
                x = origx, y += (8 * z) >> 16;
        }
    } while (*(++t));

    return x;
}

int G_GameTextLen(int x,const char *t)
{
    int ac;

    if (t == NULL)
        return -1;

    do
    {
        if (*t == 32)
        {
            x+=5;
            continue;
        }
        else ac = *t - '!' + STARTALPHANUM;

        if (ac < STARTALPHANUM || ac > (STARTALPHANUM + 93))
            break;

        if ((*t >= '0' && *t <= '9'))
            x += 8;
        else x += tilesiz[ac].x;
    }
    while (*(++t));
    return (textsc(x));
}

int mpgametext(int y,const char *t,int s,int dabits)
{
    return(G_PrintGameText(4,STARTALPHANUM, 5,y,t,s,0,dabits,0, 0, xdim-1, ydim-1, 65536));
}

int minitext_(int x,int y,const char *t,int s,int p,int sb)
{
    int ac;
    char ch,cmode;
//    int ht = usehightile;

    cmode = (sb&256)!=0;
    sb &= 255;

//    usehightile = (ht && !r_downsize);
    do
    {
        if (*t == '^' && isdigit(*(t+1)))
        {
            char smallbuf[4];
            if (!isdigit(*(++t+1)))
            {
                smallbuf[0] = *(t);
                smallbuf[1] = '\0';
                p = atoi(smallbuf);
                continue;
            }
            smallbuf[0] = *(t++);
            smallbuf[1] = *(t);
            smallbuf[2] = '\0';
            p = atoi(smallbuf);
            continue;
        }
        ch = Btoupper(*t);
        if (ch == 32)
        {
            x+=5;
            continue;
        }
        else ac = ch - '!' + MINIFONT;

        if (cmode) rotatesprite(sbarx(x),sbary(y),sbarsc(65536L),0,ac,s,p,sb,0,0,xdim-1,ydim-1);
        else rotatesprite(x<<16,y<<16,65536L,0,ac,s,p,sb,0,0,xdim-1,ydim-1);
        x += 4; // tilesiz[ac].x+1;

    }
    while (*(++t));
//    usehightile = ht;
    return (x);
}

void G_AddUserQuote(const char *daquote)
{
    int i;

    for (i=MAXUSERQUOTES-1;i>0;i--)
    {
        Bstrcpy(user_quote[i],user_quote[i-1]);
        user_quote_time[i] = user_quote_time[i-1];
    }
    Bstrcpy(user_quote[0],daquote);
    OSD_Printf("%s\n",daquote);

    user_quote_time[0] = ud.msgdisptime;
    pub = NUMPAGES;
}

void G_HandleSpecialKeys(void)
{
    DukePlayer_t *myplayer = g_player[myconnectindex].ps;
    if (myplayer == NULL)
        return;

    CONTROL_ProcessBinds();

    if (ALT_IS_PRESSED && KB_KeyPressed(sc_Enter))
    {
        if (videoSetGameMode(!ud.config.ScreenMode,ud.config.ScreenWidth,ud.config.ScreenHeight,ud.config.ScreenBPP, ud.detail))
        {
            OSD_Printf(OSD_ERROR "Failed setting fullscreen video mode.\n");
            if (videoSetGameMode(ud.config.ScreenMode, ud.config.ScreenWidth, ud.config.ScreenHeight, ud.config.ScreenBPP, ud.detail))
                G_GameExit("Failed to recover from failure to set fullscreen video mode.\n");
        }
        else ud.config.ScreenMode = !ud.config.ScreenMode;
        KB_ClearKeyDown(sc_Enter);
        g_restorePalette = 1;
        G_UpdateScreenArea();
    }

    if (KB_UnBoundKeyPressed(sc_F12))
    {
        KB_ClearKeyDown(sc_F12);
        videoCaptureScreen("duke0000.tga",0);
        P_DoQuote(103, myplayer);
    }

    // only dispatch commands here when not in a game
    if (!(myplayer->gm&MODE_GAME))
    {
        OSD_DispatchQueued();
    }

    if (qe == 0 && KB_KeyPressed(sc_LeftControl) && KB_KeyPressed(sc_LeftAlt) && (KB_KeyPressed(sc_Delete)||KB_KeyPressed(sc_End)))
    {
        qe = 1;
        G_GameExit("Quick Exit.");
    }
}

static void gameTimerHandler(void)
{
    MUSIC_Update();
    G_HandleSpecialKeys();
#ifdef DISCORD_RPC
    CheckDiscord();
#endif
}

static void G_ShowCacheLocks(void)
{
    short i,k;

    auto indexes = g_cache.getIndex();

    k = 0;
    for (i=g_cache.numBlocks()-1;i>=0;i--)
        if ((*indexes[i].lock) >= 200)
        {
            Bsprintf(tempbuf,"Locked- %d: Leng:%d, Lock:%d",i,indexes[i].leng,*indexes[i].lock);
            printext256(0L,k,31,-1,tempbuf,1);
            k += 6;
        }

    k += 6;

    for (i=10;i>=0;i--)
        if (rts_lumplockbyte[i] >= 200)
        {
            Bsprintf(tempbuf,"RTS Locked %d:",i);
            printext256(0L,k,31,-1,tempbuf,1);
            k += 6;
        }
}

int A_CheckInventorySprite(spritetype *s)
{
    switch (DynamicTileMap[s->picnum])
    {
    case FIRSTAID__STATIC:
    case STEROIDS__STATIC:
    case HEATSENSOR__STATIC:
    case BOOTS__STATIC:
    case JETPACK__STATIC:
    case HOLODUKE__STATIC:
    case AIRTANK__STATIC:
        return 1;
    }
    return 0;
}

void G_DrawTile(int x, int y, int tilenum, int shade, int orientation)
{
    int p = sector[g_player[screenpeek].ps->cursectnum].floorpal, a = 0;

    if (orientation&4)
        a = 1024;

    rotatesprite((orientation&256)?x:(x<<16),(orientation&256)?y:(y<<16),65536L,a,tilenum,shade,p,2|orientation,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
}

void G_DrawTilePal(int x, int y, int tilenum, int shade, int orientation, int p)
{
    int a = 0;

    if (orientation&4)
        a = 1024;

    rotatesprite((orientation&256)?x:(x<<16),(orientation&256)?y:(y<<16),65536L,a,tilenum,shade,p,2|orientation,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
}

void G_DrawTileSmall(int x, int y, int tilenum, int shade, int orientation)
{
    int p = sector[g_player[screenpeek].ps->cursectnum].floorpal, a = 0;

    if (orientation&4)
        a = 1024;

    rotatesprite((orientation&256)?x:(x<<16),(orientation&256)?y:(y<<16),32768L,a,tilenum,shade,p,2|orientation,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
}

void G_DrawTilePalSmall(int x, int y, int tilenum, int shade, int orientation, int p)
{
    int a = 0;

    if (orientation&4)
        a = 1024;

    rotatesprite((orientation&256)?x:(x<<16),(orientation&256)?y:(y<<16),32768L,a,tilenum,shade,p,2|orientation,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
}

#define POLYMOSTTRANS (1)
#define POLYMOSTTRANS2 (1|32)

// draws inventory numbers in the HUD for both the full and mini status bars
static void G_DrawInvNum(int x,int y,char num1,char ha,char sbits)
{
    char dabuf[80] = {0};
    int shd = (x < 0);

    if (shd) x = -x;

    Bsprintf(dabuf,"%d",num1);
    if (num1 > 99)
    {
        if (shd && ud.screen_size == 4 && videoGetRenderMode() >= 3 && althud_shadows)
        {
            rotatesprite(sbarx(x-4+1),sbary(y+1),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,4,POLYMOSTTRANS|sbits,0,0,xdim-1,ydim-1);
            rotatesprite(sbarx(x+1),sbary(y+1),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,4,POLYMOSTTRANS|sbits,0,0,xdim-1,ydim-1);
            rotatesprite(sbarx(x+4+1),sbary(y+1),sbarsc(65536L),0,THREEBYFIVE+dabuf[2]-'0',ha,4,POLYMOSTTRANS|sbits,0,0,xdim-1,ydim-1);
        }
        rotatesprite(sbarx(x-4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,sbits,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,sbits,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[2]-'0',ha,0,sbits,0,0,xdim-1,ydim-1);
        return;
    }
    if (num1 > 9)
    {
        if (shd && ud.screen_size == 4 && videoGetRenderMode() >= 3 && althud_shadows)
        {
            rotatesprite(sbarx(x+1),sbary(y+1),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,4,POLYMOSTTRANS|sbits,0,0,xdim-1,ydim-1);
            rotatesprite(sbarx(x+4+1),sbary(y+1),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,4,POLYMOSTTRANS|sbits,0,0,xdim-1,ydim-1);
        }

        rotatesprite(sbarx(x),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,sbits,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,sbits,0,0,xdim-1,ydim-1);
        return;
    }
    rotatesprite(sbarx(x+4+1),sbary(y+1),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,4,sbits,0,0,xdim-1,ydim-1);
    rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,sbits,0,0,xdim-1,ydim-1);
}

static void G_DrawWeapNum(short ind,int x,int y,int num1, int num2,char ha)
{
    char dabuf[80] = {0};

    rotatesprite(sbarx(x-7),sbary(y),sbarsc(65536L),0,THREEBYFIVE+ind+1,ha-10,7,10,0,0,xdim-1,ydim-1);
    rotatesprite(sbarx(x-3),sbary(y),sbarsc(65536L),0,THREEBYFIVE+10,ha,0,10,0,0,xdim-1,ydim-1);

    if (VOLUMEONE && (ind > HANDBOMB_WEAPON || ind < 0))
    {
        minitextshade(x+1,y-4,"ORDER",20,11,2+8+16+256);
        return;
    }

    rotatesprite(sbarx(x+9),sbary(y),sbarsc(65536L),0,THREEBYFIVE+11,ha,0,10,0,0,xdim-1,ydim-1);

    if (num1 > 99) num1 = 99;
    if (num2 > 99) num2 = 99;

    Bsprintf(dabuf,"%d",num1);
    if (num1 > 9)
    {
        rotatesprite(sbarx(x),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,10,0,0,xdim-1,ydim-1);
    }
    else rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);

    Bsprintf(dabuf,"%d",num2);
    if (num2 > 9)
    {
        rotatesprite(sbarx(x+13),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+17),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        return;
    }
    rotatesprite(sbarx(x+13),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
}

static void G_DrawWeapNum2(char ind,int x,int y,int num1, int num2,char ha)
{
    char dabuf[80] = {0};

    rotatesprite(sbarx(x-7),sbary(y),sbarsc(65536L),0,THREEBYFIVE+ind+1,ha-10,7,10,0,0,xdim-1,ydim-1);
    rotatesprite(sbarx(x-4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+10,ha,0,10,0,0,xdim-1,ydim-1);
    rotatesprite(sbarx(x+13),sbary(y),sbarsc(65536L),0,THREEBYFIVE+11,ha,0,10,0,0,xdim-1,ydim-1);

    Bsprintf(dabuf,"%d",num1);
    if (num1 > 99)
    {
        rotatesprite(sbarx(x),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+8),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[2]-'0',ha,0,10,0,0,xdim-1,ydim-1);
    }
    else if (num1 > 9)
    {
        rotatesprite(sbarx(x+4),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+8),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,10,0,0,xdim-1,ydim-1);
    }
    else rotatesprite(sbarx(x+8),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);

    Bsprintf(dabuf,"%d",num2);
    if (num2 > 99)
    {
        rotatesprite(sbarx(x+17),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+21),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+25),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[2]-'0',ha,0,10,0,0,xdim-1,ydim-1);
    }
    else if (num2 > 9)
    {
        rotatesprite(sbarx(x+17),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(x+21),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[1]-'0',ha,0,10,0,0,xdim-1,ydim-1);
        return;
    }
    else
        rotatesprite(sbarx(x+25),sbary(y),sbarsc(65536L),0,THREEBYFIVE+dabuf[0]-'0',ha,0,10,0,0,xdim-1,ydim-1);
}

static void G_DrawWeapAmounts(DukePlayer_t *p,int x,int y,int u)
{
    int cw = p->curr_weapon;

    if (u&4)
    {
        if (u != -1) G_PatchStatusBar(88,178,88+37,178+6); //original code: (96,178,96+12,178+6);
        G_DrawWeapNum2(PISTOL_WEAPON,x,y,
                       p->ammo_amount[PISTOL_WEAPON],p->max_ammo_amount[PISTOL_WEAPON],
                       12-20*(cw == PISTOL_WEAPON));
    }
    if (u&8)
    {
        if (u != -1) G_PatchStatusBar(88,184,88+37,184+6); //original code: (96,184,96+12,184+6);
        G_DrawWeapNum2(SHOTGUN_WEAPON,x,y+6,
                       p->ammo_amount[SHOTGUN_WEAPON],p->max_ammo_amount[SHOTGUN_WEAPON],
                       (!p->gotweapon[SHOTGUN_WEAPON]*9)+12-18*
                       (cw == SHOTGUN_WEAPON));
    }
    if (u&16)
    {
        if (u != -1) G_PatchStatusBar(88,190,88+37,190+6); //original code: (96,190,96+12,190+6);
        G_DrawWeapNum2(CHAINGUN_WEAPON,x,y+12,
                       p->ammo_amount[CHAINGUN_WEAPON],p->max_ammo_amount[CHAINGUN_WEAPON],
                       (!p->gotweapon[CHAINGUN_WEAPON]*9)+12-18*
                       (cw == CHAINGUN_WEAPON));
    }
    if (u&32)
    {
        if (u != -1) G_PatchStatusBar(127,178,127+29,178+6); //original code: (135,178,135+8,178+6);
        G_DrawWeapNum(RPG_WEAPON,x+39,y,
                      p->ammo_amount[RPG_WEAPON],p->max_ammo_amount[RPG_WEAPON],
                      (!p->gotweapon[RPG_WEAPON]*9)+12-19*
                      (cw == RPG_WEAPON));
    }
    if (u&64)
    {
        if (u != -1) G_PatchStatusBar(127,184,127+29,184+6); //original code: (135,184,135+8,184+6);
        G_DrawWeapNum(HANDBOMB_WEAPON,x+39,y+6,
                      p->ammo_amount[HANDBOMB_WEAPON],p->max_ammo_amount[HANDBOMB_WEAPON],
                      (((!p->ammo_amount[HANDBOMB_WEAPON])|(!p->gotweapon[HANDBOMB_WEAPON]))*9)+12-19*
                      ((cw == HANDBOMB_WEAPON) || (cw == HANDREMOTE_WEAPON)));
    }
    if (u&128)
    {
        if (u != -1) G_PatchStatusBar(127,190,127+29,190+6); //original code: (135,190,135+8,190+6);

        if (p->subweapon&(1<<GROW_WEAPON))
            G_DrawWeapNum(SHRINKER_WEAPON,x+39,y+12,
                          p->ammo_amount[GROW_WEAPON],p->max_ammo_amount[GROW_WEAPON],
                          (!p->gotweapon[GROW_WEAPON]*9)+12-18*
                          (cw == GROW_WEAPON));
        else
            G_DrawWeapNum(SHRINKER_WEAPON,x+39,y+12,
                          p->ammo_amount[SHRINKER_WEAPON],p->max_ammo_amount[SHRINKER_WEAPON],
                          (!p->gotweapon[SHRINKER_WEAPON]*9)+12-18*
                          (cw == SHRINKER_WEAPON));
    }
    if (u&256)
    {
        if (u != -1) G_PatchStatusBar(158,178,162+29,178+6); //original code: (166,178,166+8,178+6);

        G_DrawWeapNum(DEVISTATOR_WEAPON,x+70,y,
                      p->ammo_amount[DEVISTATOR_WEAPON],p->max_ammo_amount[DEVISTATOR_WEAPON],
                      (!p->gotweapon[DEVISTATOR_WEAPON]*9)+12-18*
                      (cw == DEVISTATOR_WEAPON));
    }
    if (u&512)
    {
        if (u != -1) G_PatchStatusBar(158,184,162+29,184+6); //original code: (166,184,166+8,184+6);

        G_DrawWeapNum(TRIPBOMB_WEAPON,x+70,y+6,
                      p->ammo_amount[TRIPBOMB_WEAPON],p->max_ammo_amount[TRIPBOMB_WEAPON],
                      (!p->gotweapon[TRIPBOMB_WEAPON]*9)+12-18*
                      (cw == TRIPBOMB_WEAPON));
    }

    if (u&65536L)
    {
        if (u != -1) G_PatchStatusBar(158,190,162+29,190+6); //original code: (166,190,166+8,190+6);

        G_DrawWeapNum(-1,x+70,y+12,
                      p->ammo_amount[FREEZE_WEAPON],p->max_ammo_amount[FREEZE_WEAPON],
                      (!p->gotweapon[FREEZE_WEAPON]*9)+12-18*
                      (cw == FREEZE_WEAPON));
    }
}

static void G_DrawDigiNum(int x,int y,int n,char s,char cs)
{
    int i, j = 0, k, p, c;
    char b[10];

    Bsnprintf(b,10,"%d",n);
    i = Bstrlen(b);

    for (k=i-1;k>=0;k--)
    {
        p = DIGITALNUM+*(b+k)-'0';
        j += tilesiz[p].x+1;
    }
    c = x-(j>>1);

    j = 0;
    for (k=0;k<i;k++)
    {
        p = DIGITALNUM+*(b+k)-'0';
        rotatesprite(sbarx(c+j),sbary(y),sbarsc(65536L),0,p,s,0,cs,0,0,xdim-1,ydim-1);
        j += tilesiz[p].x+1;
    }
}

void G_DrawTXDigiNumZ(int starttile, int x,int y,int n,int s,int pal,int cs,int x1, int y1, int x2, int y2, int z)
{
    int i, j = 0, k, p, c;
    char b[10];
    int shift = (cs&256)?0:16;

    //ltoa(n,b,10);
    Bsnprintf(b,10,"%d",n);
    i = Bstrlen(b);

    for (k=i-1;k>=0;k--)
    {
        p = starttile+*(b+k)-'0';
        j += (tilesiz[p].x+1)*z/65536;
    }
    if (cs&256) j<<=16;
    c = x-(j>>1);

    j = 0;
    for (k=0;k<i;k++)
    {
        p = starttile+*(b+k)-'0';
        rotatesprite((c+j)<<shift,y<<shift,z,0,p,s,pal,2|cs,x1,y1,x2,y2);
        /*        rotatesprite((c+j)<<16,y<<16,65536L,0,p,s,pal,cs,0,0,xdim-1,ydim-1);
                rotatesprite(x<<16,y<<16,32768L,a,tilenum,shade,p,2|orientation,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);*/
        j += ((tilesiz[p].x+1)*z/65536)<<((cs&256)?16:0);
    }
}

static void G_DrawAltDigiNum(int x,int y,int n,char s,char cs)
{
    int i, j = 0, k, p, c;
    char b[10];
    int rev = (x < 0);
    int shd = (y < 0);

    if (rev) x = -x;
    if (shd) y = -y;

    Bsnprintf(b,10,"%d",n);
    i = Bstrlen(b);

    for (k=i-1;k>=0;k--)
    {
        p = althud_numbertile+*(b+k)-'0';
        j += tilesiz[p].x+1;
    }
    c = x-(j>>1);

    if (rev)
    {
//        j = 0;
        for (k=0;k<i;k++)
        {
            p = althud_numbertile+*(b+k)-'0';
            if (shd && videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarxr(c+j-1),sbary(y+1),sbarsc(65536L),0,p,s,4,cs|POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
            rotatesprite(sbarxr(c+j),sbary(y),sbarsc(65536L),0,p,s,althud_numberpal,cs,0,0,xdim-1,ydim-1);
            j -= tilesiz[p].x+1;
        }
        return;
    }
    j = 0;
    for (k=0;k<i;k++)
    {
        p = althud_numbertile+*(b+k)-'0';
        if (shd && videoGetRenderMode() >= 3 && althud_shadows)
            rotatesprite(sbarx(c+j+1),sbary(y+1),sbarsc(65536L),0,p,s,4,cs|POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
        rotatesprite(sbarx(c+j),sbary(y),sbarsc(65536L),0,p,s,althud_numberpal,cs,0,0,xdim-1,ydim-1);
        j += tilesiz[p].x+1;
    }
}

static void G_DrawInventory(DukePlayer_t *p)
{
    int n, j = 0, xoff = 0, y;

    n = (p->jetpack_amount > 0)<<3;
    if (n&8) j++;
    n |= (p->scuba_amount > 0)<<5;
    if (n&32) j++;
    n |= (p->steroids_amount > 0)<<1;
    if (n&2) j++;
    n |= (p->holoduke_amount > 0)<<2;
    if (n&4) j++;
    n |= (p->firstaid_amount > 0);
    if (n&1) j++;
    n |= (p->heat_amount > 0)<<4;
    if (n&16) j++;
    n |= (p->boot_amount > 0)<<6;
    if (n&64) j++;

    xoff = 160-(j*11);

    j = 0;

//    if (ud.screen_size > 4)
    y = 154;
//    else y = (ud.drawweapon == 2?150:172);

    /*    if (ud.screen_size == 4 && ud.drawweapon != 2)
        {
            xoff += 65;
            if (ud.multimode > 1)
                xoff -= 9;
        }
    */
    while (j <= 9)
    {
        if (n&(1<<j))
        {
            switch (n&(1<<j))
            {
            case   1:
                rotatesprite(xoff<<16,y<<16,65536L,0,FIRSTAID_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            case   2:
                rotatesprite((xoff+1)<<16,y<<16,65536L,0,STEROIDS_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            case   4:
                rotatesprite((xoff+2)<<16,y<<16,65536L,0,HOLODUKE_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            case   8:
                rotatesprite(xoff<<16,y<<16,65536L,0,JETPACK_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            case  16:
                rotatesprite(xoff<<16,y<<16,65536L,0,HEAT_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            case  32:
                rotatesprite(xoff<<16,y<<16,65536L,0,AIRTANK_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            case 64:
                rotatesprite(xoff<<16,(y-1)<<16,65536L,0,BOOT_ICON,0,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                break;
            }

            xoff += 22;

            if (p->inven_icon == j+1)
                rotatesprite((xoff-2)<<16,(y+19)<<16,65536L,1024,ARROW,-32,0,2+16,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
        }

        j++;
    }
}

void G_DrawFrags(void)
{
    int i, j = 0;
    const int32_t orient = 2 + 8 + 16 + 64;
    int32_t const xpos = (320 << 15) - (tilesiz[FRAGBAR].x << 15);

    TRAVERSE_CONNECT(i)
    if (i > j) j = i;

    for (i = 0; i <= (j >> 2); i++)
        rotatesprite_fs(xpos, (8 * i) << 16, 65600, 0, FRAGBAR, 0, 0, orient);

    TRAVERSE_CONNECT(i)
    {
        minitext(21+(73*(i&3)),2+((i&28)<<1),&g_player[i].user_name[0],/*sprite[g_player[i].ps->i].pal*/g_player[i].ps->palookup,2+8+16);
        Bsprintf(tempbuf,"%d",g_player[i].ps->frag-g_player[i].ps->fraggedself);
        minitext(17+50+(73*(i&3)),2+((i&28)<<1),tempbuf,/*sprite[g_player[i].ps->i].pal*/g_player[i].ps->palookup,2+8+16);
    }
}

#define SBY (200-tilesiz[BOTTOMSTATUSBAR].y)

static void G_DrawStatusBar(int snum)
{
    DukePlayer_t *p = g_player[snum].ps;
    int i, j, o, ss = ud.screen_size, u;
    int permbit = 0;

    if (ss < 4) return;

    if (g_player[snum].ps->gm&MODE_MENU)
        if ((g_currentMenu >= 400  && g_currentMenu <= 405))
            return;

    if (videoGetRenderMode() >= 3) pus = NUMPAGES;   // JBF 20040101: always redraw in GL

    if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
    {
        if (pus)
        {
            G_DrawFrags();
        }
        else
        {
            TRAVERSE_CONNECT(i)
            if (g_player[i].ps->frag != sbar.frag[i])
            {
                G_DrawFrags();
                break;
            }

        }
        TRAVERSE_CONNECT(i)
        if (i != myconnectindex)
            sbar.frag[i] = g_player[i].ps->frag;
    }

    if (ss == 4)   //DRAW MINI STATUS BAR:
    {
        if (ud.althud) // althud
        {
            static int ammo_sprites[MAX_WEAPONS];

            if (!ammo_sprites[0])
            {
                /* this looks stupid but it lets us initialize static memory to dynamic values
                   these values can be changed from the CONs with dynamic tile remapping
                   but we don't want to have to recreate the values in memory every time
                   the HUD is drawn */

                int asprites[MAX_WEAPONS] = { BOOTS, AMMO, SHOTGUNAMMO,
                                              BATTERYAMMO, RPGAMMO, HBOMBAMMO, CRYSTALAMMO, DEVISTATORAMMO,
                                              TRIPBOMBSPRITE, FREEZEAMMO+1, HBOMBAMMO, GROWAMMO
                                            };
                Bmemcpy(ammo_sprites,asprites,sizeof(ammo_sprites));
            }

//            rotatesprite(sbarx(5+1),sbary(200-25+1),sbarsc(49152L),0,SIXPAK,0,4,10+16+1+32,0,0,xdim-1,ydim-1);
//            rotatesprite(sbarx(5),sbary(200-25),sbarsc(49152L),0,SIXPAK,0,0,10+16,0,0,xdim-1,ydim-1);
            if (videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarx(2+1),sbary(200-21+1),sbarsc(49152L),0,COLA,0,4,10+16+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
            rotatesprite(sbarx(2),sbary(200-21),sbarsc(49152L),0,COLA,0,0,10+16,0,0,xdim-1,ydim-1);

            if (sprite[p->i].pal == 1 && p->last_extra < 2)
                G_DrawAltDigiNum(40,-(200-22),1,-16,10+16);
            else if (!althud_flashing || p->last_extra > (p->max_player_health>>2) || (int32_t)totalclock&32)
            {
                int s = -8;
                if (althud_flashing && p->last_extra > p->max_player_health)
                    s += (sintable[((int32_t)totalclock<<5)&2047]>>10);
                G_DrawAltDigiNum(40,-(200-22),p->last_extra,s,10+16);
            }

            if (videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarx(62+1),sbary(200-25+1),sbarsc(49152L),0,SHIELD,0,4,10+16+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
            rotatesprite(sbarx(62),sbary(200-25),sbarsc(49152L),0,SHIELD,0,0,10+16,0,0,xdim-1,ydim-1);

            G_DrawAltDigiNum(105,-(200-22),p->shield_amount,-16,10+16);

            if (videoGetRenderMode() >= 3 && althud_shadows)
            {
                if (ud.got_access&1) rotatesprite(sbarxr(39-1),sbary(200-43+1),sbarsc(32768),0,ACCESSCARD,0,4,10+16+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
                if (ud.got_access&4) rotatesprite(sbarxr(34-1),sbary(200-41+1),sbarsc(32768),0,ACCESSCARD,0,4,10+16+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
                if (ud.got_access&2) rotatesprite(sbarxr(29-1),sbary(200-39+1),sbarsc(32768),0,ACCESSCARD,0,4,10+16+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
            }

            if (ud.got_access&1) rotatesprite(sbarxr(39),sbary(200-43),sbarsc(32768),0,ACCESSCARD,0,0,10+16,0,0,xdim-1,ydim-1);
            if (ud.got_access&4) rotatesprite(sbarxr(34),sbary(200-41),sbarsc(32768),0,ACCESSCARD,0,23,10+16,0,0,xdim-1,ydim-1);
            if (ud.got_access&2) rotatesprite(sbarxr(29),sbary(200-39),sbarsc(32768),0,ACCESSCARD,0,21,10+16,0,0,xdim-1,ydim-1);

            i = 32768;
            if (p->curr_weapon == PISTOL_WEAPON) i = 16384;
            if (videoGetRenderMode() >= 3 && althud_shadows)
                rotatesprite(sbarxr(57-1),sbary(200-15+1),sbarsc(i),0,ammo_sprites[p->curr_weapon],0,4,2+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
            rotatesprite(sbarxr(57),sbary(200-15),sbarsc(i),0,ammo_sprites[p->curr_weapon],0,0,2,0,0,xdim-1,ydim-1);

            if (p->curr_weapon == HANDREMOTE_WEAPON) i = HANDBOMB_WEAPON;
            else i = p->curr_weapon;
            if (p->curr_weapon != KNEE_WEAPON &&
                    (!althud_flashing || (int32_t)totalclock&32 || p->ammo_amount[i] > (p->max_ammo_amount[i]/10)))
                G_DrawAltDigiNum(-20,-(200-22),p->ammo_amount[i],-16,10+16);

            o = 102;
            permbit = 0;

            if (p->inven_icon)
            {
                switch (p->inven_icon)
                {
                case 1:
                    i = FIRSTAID_ICON;
                    break;
                case 2:
                    i = STEROIDS_ICON;
                    break;
                case 3:
                    i = HOLODUKE_ICON;
                    break;
                case 4:
                    i = JETPACK_ICON;
                    break;
                case 5:
                    i = HEAT_ICON;
                    break;
                case 6:
                    i = AIRTANK_ICON;
                    break;
                case 7:
                    i = BOOT_ICON;
                    break;
                default:
                    i = -1;
                }
                if (i >= 0)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        rotatesprite(sbarx(231-o+1),sbary(200-21-2+1),sbarsc(65536L),0,i,0,4,10+16+permbit+POLYMOSTTRANS2,0,0,xdim-1,ydim-1);
                    rotatesprite(sbarx(231-o),sbary(200-21-2),sbarsc(65536L),0,i,0,0,10+16+permbit,0,0,xdim-1,ydim-1);
                }

                if (videoGetRenderMode() >= 3 && althud_shadows)
                    minitext(292-30-o+1,190-3+1,"%",4,POLYMOSTTRANS+10+16+permbit + 256);
                minitext(292-30-o,190-3,"%",6,10+16+permbit + 256);

                j = 0x80000000;
                switch (p->inven_icon)
                {
                case 1:
                    i = p->firstaid_amount;
                    break;
                case 2:
                    i = ((p->steroids_amount+3)>>2);
                    break;
                case 3:
                    i = ((p->holoduke_amount+15)/24);
                    j = p->holoduke_on;
                    break;
                case 4:
                    i = ((p->jetpack_amount+15)>>4);
                    j = p->jetpack_on;
                    break;
                case 5:
                    i = p->heat_amount/12;
                    j = p->heat_on;
                    break;
                case 6:
                    i = ((p->scuba_amount+63)>>6);
                    break;
                case 7:
                    i = (p->boot_amount>>1);
                    break;
                }
                G_DrawInvNum(-(284-30-o),200-6-3,(char)i,0,10+permbit);
                if (j > 0)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        minitext(288-30-o+1,180-3+1,"ON",4,POLYMOSTTRANS+10+16+permbit + 256);
                    minitext(288-30-o,180-3,"ON",0,10+16+permbit + 256);
                }
                else if ((unsigned int)j != 0x80000000)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        minitext(284-30-o+1,180-3+1,"OFF",4,POLYMOSTTRANS+10+16+permbit + 256);
                    minitext(284-30-o,180-3,"OFF",2,10+16+permbit + 256);
                }
                if (p->inven_icon >= 6)
                {
                    if (videoGetRenderMode() >= 3 && althud_shadows)
                        minitext(284-35-o+1,180-3+1,"AUTO",4,POLYMOSTTRANS+10+16+permbit + 256);
                    minitext(284-35-o,180-3,"AUTO",2,10+16+permbit + 256);
                }

            }
            return;
        }
        rotatesprite(sbarx(5),sbary(200-28),sbarsc(65536L),0,HEALTHBOX,0,21,10+16,0,0,xdim-1,ydim-1);
        if (p->inven_icon)
            rotatesprite(sbarx(69),sbary(200-30),sbarsc(65536L),0,INVENTORYBOX,0,21,10+16,0,0,xdim-1,ydim-1);

        if (sprite[p->i].pal == 1 && p->last_extra < 2) // frozen
            G_DrawDigiNum(20,200-17,1,-16,10+16);
        else G_DrawDigiNum(20,200-17,p->last_extra,-16,10+16);

        rotatesprite(sbarx(37),sbary(200-28),sbarsc(65536L),0,AMMOBOX,0,21,10+16,0,0,xdim-1,ydim-1);

        if (p->curr_weapon == HANDREMOTE_WEAPON) i = HANDBOMB_WEAPON;
        else i = p->curr_weapon;
        G_DrawDigiNum(53,200-17,p->ammo_amount[i],-16,10+16);

        o = 158;
        permbit = 0;
        if (p->inven_icon)
        {
            switch (p->inven_icon)
            {
            case 1:
                i = FIRSTAID_ICON;
                break;
            case 2:
                i = STEROIDS_ICON;
                break;
            case 3:
                i = HOLODUKE_ICON;
                break;
            case 4:
                i = JETPACK_ICON;
                break;
            case 5:
                i = HEAT_ICON;
                break;
            case 6:
                i = AIRTANK_ICON;
                break;
            case 7:
                i = BOOT_ICON;
                break;
            default:
                i = -1;
            }
            if (i >= 0) rotatesprite(sbarx(231-o),sbary(200-21),sbarsc(65536L),0,i,0,0,10+16+permbit,0,0,xdim-1,ydim-1);

            minitext(292-30-o,190,"%",6,10+16+permbit + 256);

            j = 0x80000000;
            switch (p->inven_icon)
            {
            case 1:
                i = p->firstaid_amount;
                break;
            case 2:
                i = ((p->steroids_amount+3)>>2);
                break;
            case 3:
                i = ((p->holoduke_amount+15)/24);
                j = p->holoduke_on;
                break;
            case 4:
                i = ((p->jetpack_amount+15)>>4);
                j = p->jetpack_on;
                break;
            case 5:
                i = p->heat_amount/12;
                j = p->heat_on;
                break;
            case 6:
                i = ((p->scuba_amount+63)>>6);
                break;
            case 7:
                i = (p->boot_amount>>1);
                break;
            }
            G_DrawInvNum(284-30-o,200-6,(char)i,0,10+permbit);
            if (j > 0) minitext(288-30-o,180,"ON",0,10+16+permbit + 256);
            else if ((unsigned int)j != 0x80000000) minitext(284-30-o,180,"OFF",2,10+16+permbit + 256);
            if (p->inven_icon >= 6) minitext(284-35-o,180,"AUTO",2,10+16+permbit + 256);
        }
        return;
    }

    //DRAW/UPDATE FULL STATUS BAR:

    if (pus)
    {
        pus = 0;
        u = -1;
    }
    else u = 0;

    if (sbar.frag[myconnectindex] != p->frag)
    {
        sbar.frag[myconnectindex] = p->frag;
        u |= 32768;
    }
    if (sbar.got_access != ud.got_access)
    {
        sbar.got_access = ud.got_access;
        u |= 16384;
    }

    if (sbar.last_extra != p->last_extra)
    {
        sbar.last_extra = p->last_extra;
        u |= 1;
    }

    {
        int lAmount=Gv_GetVarByLabel("PLR_MORALE",-1, p->i, snum);
        if (lAmount == -1)
        {
            if (sbar.shield_amount != p->shield_amount)
            {
                sbar.shield_amount = p->shield_amount;
                u |= 2;
            }

        }
        else
        {
            if (sbar.shield_amount != lAmount)
            {
                sbar.shield_amount = lAmount;
                u |= 2;
            }

        }
    }

    if (sbar.curr_weapon != p->curr_weapon)
    {
        sbar.curr_weapon = p->curr_weapon;
        u |= (4+8+16+32+64+128+256+512+1024+65536L);
    }

    for (i=1;i<MAX_WEAPONS;i++)
    {
        if (sbar.ammo_amount[i] != p->ammo_amount[i])
        {
            sbar.ammo_amount[i] = p->ammo_amount[i];
            if (i < 9)
                u |= ((2<<i)+1024);
            else u |= 65536L+1024;
        }

        if (sbar.gotweapon[i] != p->gotweapon[i])
        {
            sbar.gotweapon[i] = p->gotweapon[i];
            if (i < 9)
                u |= ((2<<i)+1024);
            else u |= 65536L+1024;
        }
    }
    if (sbar.inven_icon != p->inven_icon)
    {
        sbar.inven_icon = p->inven_icon;
        u |= (2048+4096+8192);
    }
    if (sbar.holoduke_on != p->holoduke_on)
    {
        sbar.holoduke_on = p->holoduke_on;
        u |= (4096+8192);
    }
    if (sbar.jetpack_on != p->jetpack_on)
    {
        sbar.jetpack_on = p->jetpack_on;
        u |= (4096+8192);
    }
    if (sbar.heat_on != p->heat_on)
    {
        sbar.heat_on = p->heat_on;
        u |= (4096+8192);
    }
    if (sbar.firstaid_amount != p->firstaid_amount)
    {
        sbar.firstaid_amount = p->firstaid_amount;
        u |= 8192;
    }
    if (sbar.steroids_amount != p->steroids_amount)
    {
        sbar.steroids_amount = p->steroids_amount;
        u |= 8192;
    }
    if (sbar.holoduke_amount != p->holoduke_amount)
    {
        sbar.holoduke_amount = p->holoduke_amount;
        u |= 8192;
    }
    if (sbar.jetpack_amount != p->jetpack_amount)
    {
        sbar.jetpack_amount = p->jetpack_amount;
        u |= 8192;
    }
    if (sbar.heat_amount != p->heat_amount)
    {
        sbar.heat_amount = p->heat_amount;
        u |= 8192;
    }
    if (sbar.scuba_amount != p->scuba_amount)
    {
        sbar.scuba_amount = p->scuba_amount;
        u |= 8192;
    }
    if (sbar.boot_amount != p->boot_amount)
    {
        sbar.boot_amount = p->boot_amount;
        u |= 8192;
    }
    if (u == 0) return;

    //0 - update health
    //1 - update armor
    //2 - update PISTOL_WEAPON ammo
    //3 - update SHOTGUN_WEAPON ammo
    //4 - update CHAINGUN_WEAPON ammo
    //5 - update RPG_WEAPON ammo
    //6 - update HANDBOMB_WEAPON ammo
    //7 - update SHRINKER_WEAPON ammo
    //8 - update DEVISTATOR_WEAPON ammo
    //9 - update TRIPBOMB_WEAPON ammo
    //10 - update ammo display
    //11 - update inventory icon
    //12 - update inventory on/off
    //13 - update inventory %
    //14 - update keys
    //15 - update kills
    //16 - update FREEZE_WEAPON ammo

    if (u == -1)
    {
        G_PatchStatusBar(0,0,320,200);
        if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
            rotatesprite(sbarx(277+1),sbary(SBY+7-1),sbarsc(65536L),0,KILLSICON,0,0,10+16,0,0,xdim-1,ydim-1);
    }
    if (ud.multimode > 1 && (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
    {
        if (u&32768)
        {
            if (u != -1) G_PatchStatusBar(276,SBY+17,299,SBY+17+10);
            G_DrawDigiNum(287,SBY+17,max(p->frag-p->fraggedself,0),-16,10+16);
        }
    }
    else
    {
        if (u&16384)
        {
            if (u != -1) G_PatchStatusBar(275,SBY+18,299,SBY+18+12);
            if (ud.got_access&4) rotatesprite(sbarx(275),sbary(SBY+16),sbarsc(65536L),0,ACCESS_ICON,0,23,10+16,0,0,xdim-1,ydim-1);
            if (ud.got_access&2) rotatesprite(sbarx(288),sbary(SBY+16),sbarsc(65536L),0,ACCESS_ICON,0,21,10+16,0,0,xdim-1,ydim-1);
            if (ud.got_access&1) rotatesprite(sbarx(281),sbary(SBY+23),sbarsc(65536L),0,ACCESS_ICON,0,0,10+16,0,0,xdim-1,ydim-1);
        }
    }
    if (u&(4+8+16+32+64+128+256+512+65536L)) G_DrawWeapAmounts(p,96,SBY+16,u);

    if (u&1)
    {
        if (u != -1) G_PatchStatusBar(20,SBY+17,43,SBY+17+11);
        if (sprite[p->i].pal == 1 && p->last_extra < 2)
            G_DrawDigiNum(32,SBY+17,1,-16,10+16);
        else G_DrawDigiNum(32,SBY+17,p->last_extra,-16,10+16);
    }
    if (u&2)
    {
        int lAmount=Gv_GetVarByLabel("PLR_MORALE",-1, p->i, snum);
        if (u != -1) G_PatchStatusBar(52,SBY+17,75,SBY+17+11);
        if (lAmount == -1)
            G_DrawDigiNum(64,SBY+17,p->shield_amount,-16,10+16);
        else
            G_DrawDigiNum(64,SBY+17,lAmount,-16,10+16);
    }

    if (u&1024)
    {
        if (u != -1) G_PatchStatusBar(196,SBY+17,219,SBY+17+11);
        if (p->curr_weapon != KNEE_WEAPON)
        {
            if (p->curr_weapon == HANDREMOTE_WEAPON) i = HANDBOMB_WEAPON;
            else i = p->curr_weapon;
            G_DrawDigiNum(230-22,SBY+17,p->ammo_amount[i],-16,10+16);
        }
    }

    if (u&(2048+4096+8192))
    {
        if (u != -1)
        {
            if (u&(2048+4096))
            {
                G_PatchStatusBar(231,SBY+13,265,SBY+13+18);
            }
            else
            {
                G_PatchStatusBar(250,SBY+24,261,SBY+24+6);
            }

        }
        if (p->inven_icon)
        {
            o = 0;
//            permbit = 128;

            if (u&(2048+4096))
            {
                switch (p->inven_icon)
                {
                case 1:
                    i = FIRSTAID_ICON;
                    break;
                case 2:
                    i = STEROIDS_ICON;
                    break;
                case 3:
                    i = HOLODUKE_ICON;
                    break;
                case 4:
                    i = JETPACK_ICON;
                    break;
                case 5:
                    i = HEAT_ICON;
                    break;
                case 6:
                    i = AIRTANK_ICON;
                    break;
                case 7:
                    i = BOOT_ICON;
                    break;
                }
                rotatesprite(sbarx(231-o),sbary(SBY+13),sbarsc(65536L),0,i,0,0,10+16+permbit,0,0,xdim-1,ydim-1);
                minitext(292-30-o,SBY+24,"%",6,10+16+permbit + 256);
                if (p->inven_icon >= 6) minitext(284-35-o,SBY+14,"AUTO",2,10+16+permbit + 256);
            }
            if (u&(2048+4096))
            {
                switch (p->inven_icon)
                {
                case 3:
                    j = p->holoduke_on;
                    break;
                case 4:
                    j = p->jetpack_on;
                    break;
                case 5:
                    j = p->heat_on;
                    break;
                default:
                    j = 0x80000000;
                }
                if (j > 0) minitext(288-30-o,SBY+14,"ON",0,10+16+permbit + 256);
                else if ((unsigned int)j != 0x80000000) minitext(284-30-o,SBY+14,"OFF",2,10+16+permbit + 256);
            }
            if (u&8192)
            {
                switch (p->inven_icon)
                {
                case 1:
                    i = p->firstaid_amount;
                    break;
                case 2:
                    i = ((p->steroids_amount+3)>>2);
                    break;
                case 3:
                    i = ((p->holoduke_amount+15)/24);
                    break;
                case 4:
                    i = ((p->jetpack_amount+15)>>4);
                    break;
                case 5:
                    i = p->heat_amount/12;
                    break;
                case 6:
                    i = ((p->scuba_amount+63)>>6);
                    break;
                case 7:
                    i = (p->boot_amount>>1);
                    break;
                }
                G_DrawInvNum(284-30-o,SBY+28,(char)i,0,10+permbit);
            }
        }
    }
}

#define COLOR_RED 248
#define COLOR_WHITE 31
#define LOW_FPS 30

static void G_PrintFPS(void)
{
    // adapted from ZDoom because I like it better than what we had
    // applicable ZDoom code available under GPL from csDoom
    static int FrameCount = 0;
    static int LastCount = 0;
    static int LastSec = 0;
    static int LastMS = 0;
    int ms = timerGetTicks();
    int howlong = ms - LastMS;
    if (howlong >= 0)
    {
        int thisSec = ms/1000;
        int x = (xdim <= 640);
        if (ud.tickrate)
        {
            int chars = Bsprintf(tempbuf, "%2u ms (%3u fps)", howlong, LastCount);

            printext256(windowxy2.x-(chars<<(3-x))+1,windowxy1.y+2,0,-1,tempbuf,x);
            printext256(windowxy2.x-(chars<<(3-x)),windowxy1.y+1,
                        (LastCount < LOW_FPS) ? COLOR_RED : COLOR_WHITE,-1,tempbuf,x);

            // lag meter
            if (numplayers > 1 && (totalclock - lastpackettime) > 1)
            {
                for (howlong = ((int32_t)totalclock - lastpackettime);howlong>0 && howlong<(xdim>>2);howlong--)
                    printext256(4L*howlong,0,COLOR_WHITE,-1,".",0);
            }
        }

        if (LastSec < thisSec)
        {
            g_currentFrameRate = LastCount = FrameCount / (thisSec - LastSec);
            LastSec = thisSec;
            FrameCount = 0;
        }
        FrameCount++;
    }
    LastMS = ms;
}

static void G_PrintCoords(int snum)
{
    int y = 16;
    char ang[16], horiz[16], horizoff[16];

    if ((GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR))
    {
        if (ud.multimode > 4)
            y = 32;
        else if (ud.multimode > 1)
            y = 24;
    }
    sprintf(tempbuf,"XYZ= (%d,%d,%d)",g_player[snum].ps->pos.x,g_player[snum].ps->pos.y,g_player[snum].ps->pos.z);
    printext256(250L,y,31,-1,tempbuf,0);
    fix16_to_str(g_player[snum].ps->q16horiz, horiz, 2);
    fix16_to_str(g_player[snum].ps->q16horizoff, horizoff, 2);
    fix16_to_str(g_player[snum].ps->q16ang, ang, 2);
    Bsprintf(tempbuf, "A/H/HO= %s, %s, %s", ang, horiz, horizoff);
    printext256(250L, y += 9,31,-1,tempbuf,0);

    Bsprintf(tempbuf,"ZV= %d",g_player[snum].ps->vel.z);
    printext256(250L, y += 9,31,-1,tempbuf,0);

    Bsprintf(tempbuf,"OG= %d",g_player[snum].ps->on_ground);
    printext256(250L, y += 9,31,-1,tempbuf,0);

    Bsprintf(tempbuf,"SECTL= %d",TrackerCast(sector[g_player[snum].ps->cursectnum].lotag));
    printext256(250L, y += 9,31,-1,tempbuf,0);

    Bsprintf(tempbuf,"RANDOMSEED= %d",randomseed);
    printext256(250L, y += 9,31,-1,tempbuf,0);

    Bsprintf(tempbuf, "G_RANDOM.GLOBAL= %d", g_random.global);
    printext256(250L, y += 9, 31, -1, tempbuf, 0);

    Bsprintf(tempbuf,"THOLD= %d",g_player[snum].ps->transporter_hold);
    printext256(250L, y += 18,31,-1,tempbuf,0);

    Bsprintf(tempbuf, "ONWARP= %d", g_player[snum].ps->on_warping_sector);
    printext256(250L, y += 9, 31, -1, tempbuf, 0);
}

// this handles both multiplayer and item pickup message type text
// both are passed on to gametext

static void G_PrintGameQuotes(void)
{
    int i, j, k, l;

    k = 1;
    if (GTFLAGS(GAMETYPE_FRAGBAR) && ud.screen_size > 0 && ud.multimode > 1)
    {
        j = 0;
        k += 8;
        TRAVERSE_CONNECT(i)
        if (i > j) j = i;

        if (j >= 4 && j <= 8) k += 8;
        else if (j > 8 && j <= 12) k += 16;
        else if (j > 12) k += 24;
    }

    if (g_player[screenpeek].ps->fta > 1 && (g_player[screenpeek].ps->ftq < 115 || g_player[screenpeek].ps->ftq > 117))
    {
        if (g_player[screenpeek].ps->fta > 6)
            k += 7;
        else k += g_player[screenpeek].ps->fta; /*if (g_player[screenpeek].ps->fta > 2)
            k += 3;
        else k += 1; */
    }

    /*    if (xdim >= 640 && ydim >= 480)
            k = scale(k,ydim,200); */

    j = k;

//    quotebot = min(quotebot,j);
    //  quotebotgoal = min(quotebotgoal,j);
//    if (g_player[myconnectindex].ps->gm&MODE_TYPE) j -= 8;
    //quotebotgoal = j;
    //j = quotebot;
    j = scale(j,ydim,200);
    for (i=MAXUSERQUOTES-1;i>=0;i--)
    {
        if (user_quote_time[i] <= 0) continue;
        k = user_quote_time[i];
        if (hud_glowingquotes)
        {
            if (k > 4) { mpgametext(j,user_quote[i],(sintable[(((int32_t)totalclock+(i<<2))<<5)&2047]>>11),2+8+16); j += textsc(8); }
            else if (k > 2) { mpgametext(j,user_quote[i],(sintable[(((int32_t)totalclock+(i<<2))<<5)&2047]>>11),2+8+16+1); j += textsc(k<<1); }
            else { mpgametext(j,user_quote[i],(sintable[(((int32_t)totalclock+(i<<2))<<5)&2047]>>11),2+8+16+1+32); j += textsc(k<<1); }
        }
        else
        {
            if (k > 4) { mpgametext(j,user_quote[i],0,2+8+16); j += textsc(8); }
            else if (k > 2) { mpgametext(j,user_quote[i],0,2+8+16+1); j += textsc(k<<1); }
            else { mpgametext(j,user_quote[i],0,2+8+16+1+32); j += textsc(k<<1); }
        }
        l = G_GameTextLen(USERQUOTE_LEFTOFFSET,OSD_StripColors(tempbuf,user_quote[i]));
        while (l > (ud.config.ScreenWidth - USERQUOTE_RIGHTOFFSET))
        {
            l -= (ud.config.ScreenWidth-USERQUOTE_RIGHTOFFSET);
            if (k > 4) j += textsc(8);
            else j += textsc(k<<1);

        }
    }

    if ((klabs(quotebotgoal-quotebot) <= 16) && (ud.screen_size <= 8))
        quotebot += ksgn(quotebotgoal-quotebot);
    else
        quotebot = quotebotgoal;

    if (g_player[screenpeek].ps->fta <= 1) return;

    if (ScriptQuotes[g_player[screenpeek].ps->ftq] == NULL)
    {
        OSD_Printf(OSD_ERROR "%s %d null quote %d\n",__FILE__,__LINE__,g_player[screenpeek].ps->ftq);
        return;
    }

    k = 0;

    if (g_player[screenpeek].ps->ftq == 115 || g_player[screenpeek].ps->ftq == 116)
    {
        k = 140;//quotebot-8-4;
    }
    else if (GTFLAGS(GAMETYPE_FRAGBAR) && ud.screen_size > 0 && ud.multimode > 1)
    {
        j = 0;
        k = 8;
        TRAVERSE_CONNECT(i)
        if (i > j) j = i;

        if (j >= 4 && j <= 8) k += 8;
        else if (j > 8 && j <= 12) k += 16;
        else if (j > 12) k += 24;
    }

    j = g_player[screenpeek].ps->fta;
    if (!hud_glowingquotes)
    {
        if (j > 4) gametext(320>>1,k,ScriptQuotes[g_player[screenpeek].ps->ftq],0,2+8+16);
        else if (j > 2) gametext(320>>1,k,ScriptQuotes[g_player[screenpeek].ps->ftq],0,2+8+16+1);
        else gametext(320>>1,k,ScriptQuotes[g_player[screenpeek].ps->ftq],0,2+8+16+1+32);
        return;
    }
    if (j > 4) gametext(320>>1,k,ScriptQuotes[g_player[screenpeek].ps->ftq],(sintable[((int32_t)totalclock<<5)&2047]>>11),2+8+16);
    else if (j > 2) gametext(320>>1,k,ScriptQuotes[g_player[screenpeek].ps->ftq],(sintable[((int32_t)totalclock<<5)&2047]>>11),2+8+16+1);
    else gametext(320>>1,k,ScriptQuotes[g_player[screenpeek].ps->ftq],(sintable[((int32_t)totalclock<<5)&2047]>>11),2+8+16+1+32);
}

void P_DoQuote(int q, DukePlayer_t *p)
{
    if (oldnet_predicting)
        return;

    int cq = 0;

    if (q & MAXQUOTES)
    {
        cq = 1;
        q &= ~MAXQUOTES;
    }

    if (ScriptQuotes[q] == NULL)
    {
        OSD_Printf(OSD_ERROR "%s %d null quote %d\n",__FILE__,__LINE__,q);
        return;
    }

    if (ud.fta_on == 0)
        return;

    if (p->fta > 0 && q != 115 && q != 116)
        if (p->ftq == 115 || p->ftq == 116) return;

    p->fta = 100;

    //            if(p->ftq != q || q == 26)
    // || q == 26 || q == 115 || q ==116 || q == 117 || q == 122)
    {
        if (p->ftq != q)
            if (p == g_player[screenpeek].ps)
            {
                if (cq) OSD_Printf(OSDTEXT_BLUE "%s\n",ScriptQuotes[q]);
                else OSD_Printf("%s\n",ScriptQuotes[q]);
            }

        p->ftq = q;
        pub = NUMPAGES;
        pus = NUMPAGES;
    }
}

static void G_DisplayExtraScreens(void)
{
    int flags = Gv_GetVarByLabel("LOGO_FLAGS",255, -1, -1);

    S_StopMusic();
    FX_StopAllSounds();

    if (!VOLUMEALL || flags & LOGO_SHAREWARESCREENS)
    {
        videoSetViewableArea(0,0,xdim-1,ydim-1);
        renderFlushPerms();
        //g_player[myconnectindex].ps->palette = palette;
        P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 1);    // JBF 20040308
        fadepal(0,0,0, 0,64,7);
        KB_FlushKeyboardQueue();
        rotatesprite(0,0,65536L,0,3291,0,0,2+8+16+64, 0,0,xdim-1,ydim-1);
        IFISSOFTMODE fadepal(0,0,0, 63,0,-7);
        else videoNextPage();

        while (!KB_KeyWaiting())
            gameHandleEvents();

        fadepal(0,0,0, 0,64,7);
        KB_FlushKeyboardQueue();
        rotatesprite(0,0,65536L,0,3290,0,0,2+8+16+64, 0,0,xdim-1,ydim-1);
        IFISSOFTMODE fadepal(0,0,0, 63,0,-7);
        else videoNextPage();

        while (!KB_KeyWaiting())
            gameHandleEvents();
    }

    if (flags & LOGO_TENSCREEN)
    {
        videoSetViewableArea(0,0,xdim-1,ydim-1);
        renderFlushPerms();
        //g_player[myconnectindex].ps->palette = palette;
        P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 1);    // JBF 20040308
        fadepal(0,0,0, 0,64,7);
        KB_FlushKeyboardQueue();
        rotatesprite(0,0,65536L,0,TENSCREEN,0,0,2+8+16+64, 0,0,xdim-1,ydim-1);
        IFISSOFTMODE fadepal(0,0,0, 63,0,-7);
        else videoNextPage();

        while (!KB_KeyWaiting() && totalclock < 2400)
            gameHandleEvents();
    }
}

extern int qsetmode;
extern int g_doQuickSave;

void G_GameExit(const char *t)
{
    if (*t != 0 && g_player[myconnectindex].ps != NULL)
        g_player[myconnectindex].ps->palette = BASEPAL;

    if (numplayers > 1)
        allowtimetocorrecterrorswhenquitting();

    mmulti_uninitmultiplayers();

    if (ud.recstat == 1) G_CloseDemoWrite();
    else if (ud.recstat == 2)
    {
        if (frecfilep) fclose(frecfilep);
    } // JBF: fixes crash on demo playback

    if (!qe && !cp)
    {
        if (playerswhenstarted > 1 && g_player[myconnectindex].ps->gm&MODE_GAME && GTFLAGS(GAMETYPE_SCORESHEET) && *t == ' ')
        {
            G_BonusScreen(1);
            videoSetGameMode(ud.config.ScreenMode,ud.config.ScreenWidth,ud.config.ScreenHeight,ud.config.ScreenBPP, ud.detail);
        }

        if (*t != 0 && *(t+1) != 'V' && *(t+1) != 'Y')
            G_DisplayExtraScreens();
    }

    if (*t != 0) initprintf("%s\n",t);

    if (qsetmode == 200)
        G_Shutdown();

    if (*t != 0)
    {
        //setvmode(0x3);    // JBF
        //binscreen();
        //            if(*t == ' ' && *(t+1) == 0) *t = 0;
        //printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
        if (!(t[0] == ' ' && t[1] == 0))
        {
            char titlebuf[256];
            Bsprintf(titlebuf,HEAD2 " (%s)",s_buildTimestamp);
            wm_msgbox(titlebuf, "%s", t);
        }
    }

    uninitgroupfile();

    //unlink("duke3d.tmp");

    exit(0);
}

char inputloc = 0;

int _EnterText(int small,int x,int y,char *t,int dalen,int c)
{
    char ch;
    int i;

    while ((ch = KB_GetCh()) != 0 || (g_player[myconnectindex].ps->gm&MODE_MENU && MOUSE_GetButtons()&RIGHT_MOUSE))
    {
        if (ch == asc_BackSpace)
        {
            if (inputloc > 0)
            {
                inputloc--;
                *(t+inputloc) = 0;
            }
        }
        else
        {
            if (ch == asc_Enter)
            {
                KB_ClearKeyDown(sc_Enter);
                KB_ClearKeyDown(sc_kpad_Enter);
                return (1);
            }
            else if (ch == asc_Escape || (g_player[myconnectindex].ps->gm&MODE_MENU && MOUSE_GetButtons()&RIGHT_MOUSE))
            {
                KB_ClearKeyDown(sc_Escape);
                MOUSE_ClearButton(RIGHT_MOUSE);
                return (-1);
            }
            else if (ch >= 32 && inputloc < dalen && ch < 127)
            {
                ch = Btoupper(ch);
                if (c != 997 || (ch >= '0' && ch <= '9'))
                {
                    // JBF 20040508: so we can have numeric only if we want
                    *(t+inputloc) = ch;
                    *(t+inputloc+1) = 0;
                    inputloc++;
                }
            }
        }
    }

    if (c == 999) return(0);
    if (c == 998)
    {
        char b[91],ii;
        for (ii=0;ii<inputloc;ii++)
            b[(unsigned char)ii] = '*';
        b[(unsigned char)inputloc] = 0;
        if (g_player[myconnectindex].ps->gm&MODE_TYPE)
            x = mpgametext(y,b,c,2+8+16);
        else x = gametext(x,y,b,c,2+8+16);
    }
    else
    {
        if (g_player[myconnectindex].ps->gm&MODE_TYPE)
            x = mpgametext(y,t,c,2+8+16);
        else x = gametext(x,y,t,c,2+8+16);
    }
    c = 4-(sintable[((int32_t)totalclock<<4)&2047]>>11);

    i = G_GameTextLen(USERQUOTE_LEFTOFFSET, OSD_StripColors(tempbuf,t));
    while (i > (ud.config.ScreenWidth - USERQUOTE_RIGHTOFFSET))
    {
        i -= (ud.config.ScreenWidth - USERQUOTE_RIGHTOFFSET);
        if (small&1)
            y += textsc(6);
        y += 8;
    }

    if (small&1)
        rotatesprite(textsc(x)<<16,(y<<16),32768,0,SPINNINGNUKEICON+(((int32_t)totalclock>>3)%7),c,0,(small&1)?(8|16):2+8,0,0,xdim-1,ydim-1);
    else rotatesprite((x+((small&1)?4:8))<<16,((y+((small&1)?0:4))<<16),32768,0,SPINNINGNUKEICON+(((int32_t)totalclock>>3)%7),c,0,(small&1)?(8|16):2+8,0,0,xdim-1,ydim-1);
    return (0);
}

static inline void G_MoveClouds(void)
{
    if (totalclock > cloudtotalclock || totalclock < (cloudtotalclock-7))
    {
        int i;

        cloudtotalclock = (int32_t)totalclock+6;

        for (i=g_numClouds-1;i>=0;i--)
        {
            cloudx[i] += (sintable[(fix16_to_int(g_player[screenpeek].ps->q16ang)+512)&2047]>>9);
            cloudy[i] += (sintable[fix16_to_int(g_player[screenpeek].ps->q16ang)&2047]>>9);

            sector[clouds[i]].ceilingxpanning = cloudx[i]>>6;
            sector[clouds[i]].ceilingypanning = cloudy[i]>>6;
        }
    }
}

static void G_DrawOverheadMap(int cposx, int cposy, int czoom, short cang)
{
    int i, j, k, l, x1, y1, x2=0, y2=0, x3, y3, x4, y4, ox, oy, xoff, yoff;
    int dax, day, cosang, sinang, xspan, yspan, sprx, spry;
    int xrepeat, yrepeat, z1, z2, startwall, endwall, tilenum, daang;
    int xvect, yvect, xvect2, yvect2;
    short p;
    char col;
    walltype *wal, *wal2;
    spritetype *spr;
    
    int32_t tmpydim = (xdim * 5) / 8;
    renderSetAspect(65536, divscale16(tmpydim * 320, xdim * 200));

    xvect = sintable[(-cang)&2047] * czoom;
    yvect = sintable[(1536-cang)&2047] * czoom;
    xvect2 = mulscale16(xvect,yxaspect);
    yvect2 = mulscale16(yvect,yxaspect);

    //Draw red lines
    for (i=numsectors-1;i>=0;i--)
    {
        if (!(show2dsector[i>>3]&(1<<(i&7)))) continue;

        startwall = sector[i].wallptr;
        endwall = sector[i].wallptr + sector[i].wallnum;

        z1 = sector[i].ceilingz;
        z2 = sector[i].floorz;

        for (j=startwall,wal=&wall[startwall];j<endwall;j++,wal++)
        {
            k = wal->nextwall;
            if (k < 0) continue;

            //if ((show2dwall[j>>3]&(1<<(j&7))) == 0) continue;
            //if ((k > j) && ((show2dwall[k>>3]&(1<<(k&7))) > 0)) continue;

            if (sector[wal->nextsector].ceilingz == z1)
                if (sector[wal->nextsector].floorz == z2)
                    if (((wal->cstat|wall[wal->nextwall].cstat)&(16+32)) == 0) continue;

            col = 139; //red
            if ((wal->cstat|wall[wal->nextwall].cstat)&1) col = 234; //magenta

            if (!(show2dsector[wal->nextsector>>3]&(1<<(wal->nextsector&7))))
                col = 24;
            else continue;

            ox = wal->x-cposx;
            oy = wal->y-cposy;
            x1 = dmulscale16(ox,xvect,-oy,yvect)+(xdim<<11);
            y1 = dmulscale16(oy,xvect2,ox,yvect2)+(ydim<<11);

            wal2 = &wall[wal->point2];
            ox = wal2->x-cposx;
            oy = wal2->y-cposy;
            x2 = dmulscale16(ox,xvect,-oy,yvect)+(xdim<<11);
            y2 = dmulscale16(oy,xvect2,ox,yvect2)+(ydim<<11);

            renderDrawLine(x1,y1,x2,y2,col);
        }
    }

    //Draw sprites
    k = g_player[screenpeek].ps->i;
    for (i=numsectors-1;i>=0;i--)
    {
        if (!(show2dsector[i>>3]&(1<<(i&7)))) continue;
        for (j=headspritesect[i];j>=0;j=nextspritesect[j])
            //if ((show2dsprite[j>>3]&(1<<(j&7))) > 0)
        {
            spr = &sprite[j];

            if (j == k || (spr->cstat&0x8000) || spr->cstat == 257 || spr->xrepeat == 0) continue;

            col = 71; //cyan
            if (spr->cstat&1) col = 234; //magenta

            sprx = spr->x;
            spry = spr->y;

            if ((spr->cstat&257) != 0) switch (spr->cstat&48)
                {
                case 0:
                    break;

                    ox = sprx-cposx;
                    oy = spry-cposy;
                    x1 = dmulscale16(ox,xvect,-oy,yvect);
                    y1 = dmulscale16(oy,xvect2,ox,yvect2);

                    ox = (sintable[(spr->ang+512)&2047]>>7);
                    oy = (sintable[(spr->ang)&2047]>>7);
                    x2 = dmulscale16(ox,xvect,-oy,yvect);
                    y2 = dmulscale16(oy,xvect,ox,yvect);

                    x3 = mulscale16(x2,yxaspect);
                    y3 = mulscale16(y2,yxaspect);

                    renderDrawLine(x1-x2+(xdim<<11),y1-y3+(ydim<<11),
                                x1+x2+(xdim<<11),y1+y3+(ydim<<11),col);
                    renderDrawLine(x1-y2+(xdim<<11),y1+x3+(ydim<<11),
                                x1+x2+(xdim<<11),y1+y3+(ydim<<11),col);
                    renderDrawLine(x1+y2+(xdim<<11),y1-x3+(ydim<<11),
                                x1+x2+(xdim<<11),y1+y3+(ydim<<11),col);
                    break;

                case 16:
                    if (spr->picnum == LASERLINE)
                    {
                        x1 = sprx;
                        y1 = spry;
                        tilenum = spr->picnum;
                        xoff = picanm[tilenum].xofs + spr->xoffset;
                        if ((spr->cstat&4) > 0) xoff = -xoff;
                        k = spr->ang;
                        l = spr->xrepeat;
                        dax = sintable[k&2047]*l;
                        day = sintable[(k+1536)&2047]*l;
                        l = tilesiz[tilenum].x;
                        k = (l>>1)+xoff;
                        x1 -= mulscale16(dax,k);
                        x2 = x1+mulscale16(dax,l);
                        y1 -= mulscale16(day,k);
                        y2 = y1+mulscale16(day,l);

                        ox = x1-cposx;
                        oy = y1-cposy;
                        x1 = dmulscale16(ox,xvect,-oy,yvect);
                        y1 = dmulscale16(oy,xvect2,ox,yvect2);

                        ox = x2-cposx;
                        oy = y2-cposy;
                        x2 = dmulscale16(ox,xvect,-oy,yvect);
                        y2 = dmulscale16(oy,xvect2,ox,yvect2);

                        renderDrawLine(x1+(xdim<<11),y1+(ydim<<11),
                                    x2+(xdim<<11),y2+(ydim<<11),col);
                    }

                    break;

                case 32:

                    tilenum = spr->picnum;
                    xoff = picanm[tilenum].xofs;
                    yoff = picanm[tilenum].yofs;
                    if ((spr->cstat&4) > 0) xoff = -xoff;
                    if ((spr->cstat&8) > 0) yoff = -yoff;

                    k = spr->ang;
                    cosang = sintable[(k+512)&2047];
                    sinang = sintable[k];
                    xspan = tilesiz[tilenum].x;
                    xrepeat = spr->xrepeat;
                    yspan = tilesiz[tilenum].y;
                    yrepeat = spr->yrepeat;

                    dax = ((xspan>>1)+xoff)*xrepeat;
                    day = ((yspan>>1)+yoff)*yrepeat;
                    x1 = sprx + dmulscale16(sinang,dax,cosang,day);
                    y1 = spry + dmulscale16(sinang,day,-cosang,dax);
                    l = xspan*xrepeat;
                    x2 = x1 - mulscale16(sinang,l);
                    y2 = y1 + mulscale16(cosang,l);
                    l = yspan*yrepeat;
                    k = -mulscale16(cosang,l);
                    x3 = x2+k;
                    x4 = x1+k;
                    k = -mulscale16(sinang,l);
                    y3 = y2+k;
                    y4 = y1+k;

                    ox = x1-cposx;
                    oy = y1-cposy;
                    x1 = dmulscale16(ox,xvect,-oy,yvect);
                    y1 = dmulscale16(oy,xvect2,ox,yvect2);

                    ox = x2-cposx;
                    oy = y2-cposy;
                    x2 = dmulscale16(ox,xvect,-oy,yvect);
                    y2 = dmulscale16(oy,xvect2,ox,yvect2);

                    ox = x3-cposx;
                    oy = y3-cposy;
                    x3 = dmulscale16(ox,xvect,-oy,yvect);
                    y3 = dmulscale16(oy,xvect2,ox,yvect2);

                    ox = x4-cposx;
                    oy = y4-cposy;
                    x4 = dmulscale16(ox,xvect,-oy,yvect);
                    y4 = dmulscale16(oy,xvect2,ox,yvect2);

                    renderDrawLine(x1+(xdim<<11),y1+(ydim<<11),
                                x2+(xdim<<11),y2+(ydim<<11),col);

                    renderDrawLine(x2+(xdim<<11),y2+(ydim<<11),
                                x3+(xdim<<11),y3+(ydim<<11),col);

                    renderDrawLine(x3+(xdim<<11),y3+(ydim<<11),
                                x4+(xdim<<11),y4+(ydim<<11),col);

                    renderDrawLine(x4+(xdim<<11),y4+(ydim<<11),
                                x1+(xdim<<11),y1+(ydim<<11),col);

                    break;
                }
        }
    }

    //Draw white lines
    for (i=numsectors-1;i>=0;i--)
    {
        if (!(show2dsector[i>>3]&(1<<(i&7)))) continue;

        startwall = sector[i].wallptr;
        endwall = sector[i].wallptr + sector[i].wallnum;

        k = -1;
        for (j=startwall,wal=&wall[startwall];j<endwall;j++,wal++)
        {
            if (wal->nextwall >= 0) continue;

            //if ((show2dwall[j>>3]&(1<<(j&7))) == 0) continue;

            if (tilesiz[wal->picnum].x == 0) continue;
            if (tilesiz[wal->picnum].y == 0) continue;

            if (j == k)
            {
                x1 = x2;
                y1 = y2;
            }
            else
            {
                ox = wal->x-cposx;
                oy = wal->y-cposy;
                x1 = dmulscale16(ox,xvect,-oy,yvect)+(xdim<<11);
                y1 = dmulscale16(oy,xvect2,ox,yvect2)+(ydim<<11);
            }

            k = wal->point2;
            wal2 = &wall[k];
            ox = wal2->x-cposx;
            oy = wal2->y-cposy;
            x2 = dmulscale16(ox,xvect,-oy,yvect)+(xdim<<11);
            y2 = dmulscale16(oy,xvect2,ox,yvect2)+(ydim<<11);

            renderDrawLine(x1,y1,x2,y2,24);
        }
    }

    videoSetCorrectedAspect();

    TRAVERSE_CONNECT(p)
    {
        if (ud.scrollmode && p == screenpeek) continue;

        ox = sprite[g_player[p].ps->i].x-cposx;
        oy = sprite[g_player[p].ps->i].y-cposy;
        daang = (sprite[g_player[p].ps->i].ang-cang)&2047;
        if (p == screenpeek)
        {
            ox = 0;
            oy = 0;
            daang = 0;
        }
        x1 = mulscale(ox,xvect,16) - mulscale(oy,yvect,16);
        y1 = mulscale(oy,xvect2,16) + mulscale(ox,yvect2,16);

        if (p == screenpeek || GTFLAGS(GAMETYPE_OTHERPLAYERSINMAP))
        {
            if (sprite[g_player[p].ps->i].xvel > 16 && g_player[p].ps->on_ground)
                i = APLAYERTOP+(((int32_t)totalclock>>4)&3);
            else
                i = APLAYERTOP;

            j = klabs(g_player[p].ps->truefz-g_player[p].ps->pos.z)>>8;
            j = mulscale(czoom*(sprite[g_player[p].ps->i].yrepeat+j),yxaspect,16);

            if (j < 22000) j = 22000;
            else if (j > (65536<<1)) j = (65536<<1);

            rotatesprite((x1<<4)+(xdim<<15),(y1<<4)+(ydim<<15),j,daang,i,sprite[g_player[p].ps->i].shade,
                         (g_player[p].ps->cursectnum > -1)?sector[g_player[p].ps->cursectnum].floorpal:0,
                         (sprite[g_player[p].ps->i].cstat&2)>>1,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
        }
    }
}

int GetDistance2D(int x1, int y1, int x2, int y2)
{
    int x = klabs(x1 - x2);
    int y = klabs(y1 - y2);

    if (x < y) swaplong(&x, &y);

    {
        int t = y + (y >> 1);
        return (x - (x >> 5) - (x >> 7) + (t >> 2) + (t >> 6));
    }
}

// Player IDs
#define ID_ALPHA 64                     // Starting Alpha
#define ID_ALPHA_RANGE (255-ID_ALPHA)   // Alpha Remainder
#define ID_YPOS 145                     // Y position for id text
#define ID_TIME (TICRATE*2)             // Time for id text
#define ID_FADETIME (TICRATE*1)         // Remaining time to start fading id text
#define KILLID_TIME (TICRATE*3)
#define KILLID_FADETIME (TICRATE*2)

#define WITHIN_TIME(starttime, totaltime) ((starttime - (int32_t)totalclock) > 0 && (starttime - (int32_t)totalclock) <= totaltime)

static int32_t get_xdim(int32_t x)
{
    const int32_t screenwidth = scale(240 << 16, xdim, ydim);
    return scale((x << 16) + (screenwidth >> 1) - (160 << 16), xdim, screenwidth);
}
static int32_t get_ydim(int32_t y)
{
    y = mulscale16((y << 16) + rotatesprite_y_offset - (200 << 15), rotatesprite_yxaspect) + (200 << 15);
    return scale(y, ydim, 200 << 16);
}

void G_DisplayPlayerId(void)
{
    int idalpha;

    if (ud.idplayers && (ud.multimode > 1))
    {
        // TODO: (Optimization) Consider moving this segment to a point in the code that operates at 120hz.
        int i;

        hitdata_t hit;

        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].ps->holoduke_on != -1)
                sprite[g_player[i].ps->holoduke_on].cstat ^= 256;
        }

        hitscan(&ud.camerapos, ud.camerasect,
            sintable[(fix16_to_int(ud.cameraq16ang) + 512) & 2047],
            sintable[fix16_to_int(ud.cameraq16ang) & 2047],
            fix16_to_int(F16(100) - ud.cameraq16horiz) << 11, &hit, 0xffff0030);

        TRAVERSE_CONNECT(i)
        {
            if (g_player[i].ps->holoduke_on != -1)
                sprite[g_player[i].ps->holoduke_on].cstat ^= 256;
        }

        if ((hit.sprite >= 0) && !(g_player[myconnectindex].ps->gm & MODE_MENU) && sprite[hit.sprite].picnum == APLAYER && sprite[hit.sprite].yvel != screenpeek && g_player[sprite[hit.sprite].yvel].ps->dead_flag == 0)
        {
            if (GetDistance2D(ud.camerapos.x, ud.camerapos.y, sprite[hit.sprite].x, sprite[hit.sprite].y) < 9216)
            {
                ud.playerid_num = sprite[hit.sprite].yvel;
                ud.playerid_time = (int32_t)totalclock + ID_TIME;
            }
        }
        // End segment

        if (WITHIN_TIME(ud.playerid_time, ID_TIME))
        {
            idalpha = ID_ALPHA;
            if (WITHIN_TIME(ud.playerid_time, ID_FADETIME))
            {
                idalpha += ID_ALPHA_RANGE - scale(ud.playerid_time - (int32_t)totalclock, ID_ALPHA_RANGE, ID_FADETIME);
            }

            G_ScreenText(STARTALPHANUM, 160, ID_YPOS, 65536, 0, 0, g_player[ud.playerid_num].user_name, -127, g_player[ud.playerid_num].ps->palookup, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);

            if ((!GTFLAGS(GAMETYPE_COOP) && !GTFLAGS(GAMETYPE_PLAYERSFRIENDLY)) && ((GTFLAGS(GAMETYPE_TDM) && (g_player[ud.playerid_num].pteam != g_player[screenpeek].pteam)) || !GTFLAGS(GAMETYPE_TDM)))
            {
                G_ScreenText(MINIFONT, 160, ID_YPOS + 8, 65536, 0, 0, "Enemy", -127, 2, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
            }
            else if ((GTFLAGS(GAMETYPE_COOP) || GTFLAGS(GAMETYPE_PLAYERSFRIENDLY)) || (GTFLAGS(GAMETYPE_TDM) && (g_player[ud.playerid_num].pteam == g_player[screenpeek].pteam)))
            {
                G_ScreenText(MINIFONT, 160, ID_YPOS + 8, 65536, 0, 0, "Friendly", -127, 6, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);

                Bsprintf(tempbuf, "^12Health: ^02%d ^07/ ^12Armor: ^06%d", TrackerCast(sprite[g_player[ud.playerid_num].ps->i].extra), g_player[ud.playerid_num].ps->shield_amount);
                G_ScreenText(MINIFONT, 160, ID_YPOS + 14, 65536, 0, 0, tempbuf, -127, 12, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 3, 5, 1, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
            }
        }
    }

    if (ud.multimode > 1)
    {
        if (WITHIN_TIME(g_player[screenpeek].fragged_time, KILLID_TIME))
        {
            idalpha = ID_ALPHA;
            if (WITHIN_TIME(g_player[screenpeek].fragged_time, KILLID_FADETIME))
            {
                idalpha += ID_ALPHA_RANGE - scale(g_player[screenpeek].fragged_time - (int32_t)totalclock, ID_ALPHA_RANGE, KILLID_FADETIME);
            }

            // HACK: Modulo palookup to keep under 3 digits. ScreenText really needs a 3-digit form of color code.
            Bsprintf(tempbuf, "KILLED ^%02d%s", g_player[g_player[screenpeek].fragged_id].ps->palookup % 100, g_player[g_player[screenpeek].fragged_id].user_name);
            G_ScreenText(STARTALPHANUM, 160, ID_YPOS - 9, 65536, 0, 0, tempbuf, -127, 0, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }

        if (WITHIN_TIME(g_player[screenpeek].fraggedby_time, KILLID_TIME))
        {
            idalpha = ID_ALPHA;
            if (WITHIN_TIME(g_player[screenpeek].fraggedby_time, KILLID_FADETIME))
            {
                idalpha += ID_ALPHA_RANGE - scale(g_player[screenpeek].fraggedby_time - (int32_t)totalclock, ID_ALPHA_RANGE, KILLID_FADETIME);
            }

            // HACK: Modulo palookup to keep under 3 digits. ScreenText really needs a 3-digit form of color code.
            Bsprintf(tempbuf, "KILLED BY ^%02d%s", g_player[g_player[screenpeek].fraggedby_id].ps->palookup % 100, g_player[g_player[screenpeek].fraggedby_id].user_name);
            G_ScreenText(STARTALPHANUM, 160, ID_YPOS - 18, 65536, 0, 0, tempbuf, -127, 0, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, idalpha, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }

        if (ud.limit_hit == 1)
        {
            int pal = 0;
            rotatesprite_(0, 0, 65536L, 0, BETASCREEN, 0, 4, RS_AUTO | RS_TOPLEFT, 128, 0, get_xdim(160-120), get_ydim(ID_YPOS - 37), get_xdim(160+120), get_ydim(ID_YPOS - 28));

            if (GTFLAGS(GAMETYPE_TDM))
            {
                pal = teams[ud.winner].palnum;
                Bsprintf(tempbuf, "%s Team ^8Wins!", teams[ud.winner].name);
            }
            else
            {
                pal = g_player[ud.winner].ps->palookup;
                Bsprintf(tempbuf, "%s ^8Wins!", g_player[ud.winner].user_name);
            }

            G_ScreenText(STARTALPHANUM, 160, ID_YPOS - 36, 65536, 0, 0, tempbuf, -127, pal, RS_AUTO | RS_NOCLIP | RS_TOPLEFT, 0, 4, 8, 0, 1, TEXT_XCENTER | TEXT_UPPERCASE, 0, 0, xdim, ydim);
        }
    }
}

void G_AddKillToFeed(int victim, int attacker)
{
    g_player[victim].fraggedby_id = attacker;
    g_player[victim].fraggedby_time = g_player[attacker].fragged_time = (int32_t)totalclock + KILLID_TIME;
    g_player[attacker].fragged_id = victim;
}

////////// TINT ACCUMULATOR //////////

typedef struct {
    int32_t r, g, b;
    // f: 0-63 scale
    int32_t maxf, sumf;
} palaccum_t;

#define PALACCUM_INITIALIZER { 0, 0, 0, 0, 0 }

/* For a picture frame F and n tints C_1, C_2, ... C_n weighted a_1, a_2,
* ... a_n (on a 0-1 scale), the faded frame is calculated as
*
*    F_new := (1-max_i(a_i))*F + d*sum_i(a_i), where
*
*    d := max_i(a_i)/sum_i(a_i).
*
* This means that
*  1) tint application is independent of their order.
*  2) going from n+1 to n tints is continuous when the leaving one has faded.
*
* But note that for more than one tint, the composite tint will in general
* change its hue as the ratio of the weights of the individual ones changes.
*/
static void palaccum_add(palaccum_t* pa, const palette_t* pal, int32_t f)
{
    f = clamp(f, 0, 63);
    if (f == 0)
        return;

    pa->maxf = max(pa->maxf, f);
    pa->sumf += f;

    pa->r += f * clamp(pal->r, 0, 63);
    pa->g += f * clamp(pal->g, 0, 63);
    pa->b += f * clamp(pal->b, 0, 63);
}

static void G_FadePalaccum(const palaccum_t* pa)
{
    videoFadePalette(tabledivide32_noinline(pa->r, pa->sumf) << 2,
        tabledivide32_noinline(pa->g, pa->sumf) << 2,
        tabledivide32_noinline(pa->b, pa->sumf) << 2, pa->maxf << 2);
}

void G_DisplayRest(int smoothratio)
{
    int a, i, j;
    palaccum_t tint = PALACCUM_INITIALIZER;

    DukePlayer_t *pp = g_player[screenpeek].ps;
    walltype *wal;
    int cposx,cposy,cang;

#if defined(USE_OPENGL)
    // this takes care of fullscreen tint for OpenGL
    if (videoGetRenderMode() >= 3)
    {
        polytint_t& fstint = hictinting[MAXPALOOKUPS - 1];
        if (pp->palette == WATERPAL)
        {
            fstint.r = 224;
            fstint.g = 192;
            fstint.b = 255;
            fstint.f = 0;
        }
        else if (pp->palette == SLIMEPAL)
        {
            fstint.r = 208;
            fstint.g = 255;
            fstint.b = 192;
            fstint.f = 0;
        }
        else
        {
            fstint.r = 255;
            fstint.g = 255;
            fstint.b = 255;
            fstint.f = 0;
        }
    }
#endif /* USE_OPENGL && POLYMOST */

    rotatespritesmoothratio = smoothratio;

    // this does pain tinting etc from the CON
    palaccum_add(&tint, &pp->pals, pp->pals.f);

    static const palette_t loogiepal = { 0, 63, 0, 0 };
    palaccum_add(&tint, &loogiepal, pp->loogcnt >> 1);

    // reset a normal palette
    if (g_restorePalette)
    {
        //videoSetPalette(ud.brightness>>2,&pp->palette[0],0);
        P_SetGamePalette(pp,pp->palette,2);
        g_restorePalette = 0;
    }

    if (ud.show_help)
    {
        switch (ud.show_help)
        {
        case 1:
            rotatesprite(0,0,65536L,0,TEXTSTORY,0,0,10+16+64, 0,0,xdim-1,ydim-1);
            break;
        case 2:
            rotatesprite(0,0,65536L,0,F1HELP,0,0,10+16+64, 0,0,xdim-1,ydim-1);
            break;
        }

        if (KB_KeyPressed(sc_Escape) || MOUSE_GetButtons()&RIGHT_MOUSE)
        {
            KB_ClearKeyDown(sc_Escape);
            MOUSE_ClearButton(RIGHT_MOUSE);
            ud.show_help = 0;
            if (ud.multimode < 2 && ud.recstat != 2)
            {
                ready2send = 1;
                totalclock = ototalclock;
            }
            G_UpdateScreenArea();
        }
        return;
    }

    i = pp->cursectnum;
    if (i > -1)
    {
        show2dsector[i>>3] |= (1<<(i&7));
        wal = &wall[sector[i].wallptr];
        for (j=sector[i].wallnum;j>0;j--,wal++)
        {
            i = wal->nextsector;
            if (i < 0) continue;
            if (wal->cstat&0x0071) continue;
            if (wall[wal->nextwall].cstat&0x0071) continue;
            if (sector[i].lotag == 32767) continue;
            if (sector[i].ceilingz >= sector[i].floorz) continue;
            show2dsector[i>>3] |= (1<<(i&7));
        }
    }

    if (ud.camerasprite == -1)
    {
        if (ud.overhead_on != 2)
        {
            if (pp->newowner >= 0)
                G_DrawCameraText(pp->newowner);
            else
            {
                Net_UsePredictedPointers();
                P_DisplayWeapon(screenpeek);
                Net_UseOriginalPointers();

                if (pp->over_shoulder_on == 0)
                    P_DisplayScubaMask(screenpeek);
            }
            G_MoveClouds();
        }

        if (ud.overhead_on > 0)
        {
            // smoothratio = min(max(smoothratio,0),65536);
            smoothratio = calc_smoothratio((int32_t)totalclock, ototalclock);
            G_DoInterpolations(smoothratio);
            if (ud.scrollmode == 0)
            {
                if (pp->newowner == -1 && !ud.pause_on)
                {
                    DukePlayer_t* camPlayer = (screenpeek == myconnectindex && numplayers > 1) ? &predictedPlayer : pp;

                    cposx = camPlayer->opos.x+mulscale16((int)(camPlayer->pos.x - camPlayer->opos.x),smoothratio);
                    cposy = camPlayer->opos.y+mulscale16((int)(camPlayer->pos.y - camPlayer->opos.y),smoothratio);
                    cang = fix16_to_int(camPlayer->oq16ang) + mulscale16((fix16_to_int(camPlayer->q16ang + F16(1024) - camPlayer->oq16ang) & 2047) - 1024, smoothratio);
                }
                else
                {
                    cposx = pp->opos.x;
                    cposy = pp->opos.y;
                    cang = fix16_to_int(pp->oq16ang);
                }
            }
            else
            {
                if (!ud.pause_on)
                {
                    ud.fola += ud.folavel>>3;
                    ud.folx += (ud.folfvel*sintable[(512+2048-ud.fola)&2047])>>14;
                    ud.foly += (ud.folfvel*sintable[(512+1024-512-ud.fola)&2047])>>14;
                }
                cposx = ud.folx;
                cposy = ud.foly;
                cang = ud.fola;
            }

            if (ud.overhead_on == 2)
            {
                videoClearViewableArea(0L);
                renderDrawMapView(cposx,cposy,pp->zoom,cang);
            }
            G_DrawOverheadMap(cposx,cposy,pp->zoom,cang);

            G_RestoreInterpolations();

            if (ud.overhead_on == 2)
            {
                if (ud.screen_size > 0) a = 147;
                else a = 179;
                minitext(5,a+6,EpisodeNames[ud.volume_number],0,2+8+16);
                minitext(5,a+6+6,MapInfo[ud.volume_number*MAXLEVELS + ud.level_number].name,0,2+8+16);
            }
        }
    }

    if (pp->invdisptime > 0) G_DrawInventory(pp);

    Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
    X_OnEvent(EVENT_DISPLAYSBAR, g_player[screenpeek].ps->i, screenpeek, -1);
    if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
        G_DrawStatusBar(screenpeek);

    G_PrintGameQuotes();

    if (ud.show_level_text && hud_showmapname && g_levelTextTime > 1)
    {
        int bits = 10+16;

        if (g_levelTextTime > 4)
            bits = bits;
        else if (g_levelTextTime > 2)
            bits |= 1;
        else bits |= 1+32;
        if (MapInfo[(ud.volume_number*MAXLEVELS) + ud.level_number].name != NULL)
        {
            if (currentboardfilename[0] != 0 && ud.volume_number == 0 && ud.level_number == 7)
                menutext_(160,75,-g_levelTextTime+22/*(sintable[(totalclock<<5)&2047]>>11)*/,0,currentboardfilename,bits);
            else menutext_(160,75,-g_levelTextTime+22/*(sintable[(totalclock<<5)&2047]>>11)*/,0,MapInfo[(ud.volume_number*MAXLEVELS) + ud.level_number].name,bits);
        }
    }

    if (KB_KeyPressed(sc_Escape) && ud.overhead_on == 0
            && ud.show_help == 0
            && g_player[myconnectindex].ps->newowner == -1)
    {
        if ((g_player[myconnectindex].ps->gm&MODE_MENU) == MODE_MENU && g_currentMenu < 51)
        {
            KB_ClearKeyDown(sc_Escape);
            g_player[myconnectindex].ps->gm &= ~MODE_MENU;
            if (ud.multimode < 2 && ud.recstat != 2)
            {
                ready2send = 1;
                totalclock = ototalclock;
                g_cameraClock = (int32_t)totalclock;
                g_cameraDistance = 65536L;
            }
            walock[TILE_SAVESHOT] = 199;
            G_UpdateScreenArea();
        }
        else if ((g_player[myconnectindex].ps->gm&MODE_MENU) != MODE_MENU &&
                 g_player[myconnectindex].ps->newowner == -1 &&
                 (g_player[myconnectindex].ps->gm&MODE_TYPE) != MODE_TYPE)
        {
            KB_ClearKeyDown(sc_Escape);
            FX_StopAllSounds();
            S_ClearSoundLocks();

            S_MenuSound();

            g_player[myconnectindex].ps->gm |= MODE_MENU;

            if (ud.multimode < 2 && ud.recstat != 2) ready2send = 0;

            if (g_player[myconnectindex].ps->gm&MODE_GAME) ChangeToMenu(50);
            else ChangeToMenu(0);
            screenpeek = myconnectindex;
        }
    }

    G_DisplayPlayerId();

    X_OnEvent(EVENT_DISPLAYREST, g_player[screenpeek].ps->i, screenpeek, -1);

    if (g_player[myconnectindex].ps->newowner == -1 && ud.overhead_on == 0 && ud.crosshair && ud.camerasprite == -1)
    {
        Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
        X_OnEvent(EVENT_DISPLAYCROSSHAIR, g_player[screenpeek].ps->i, screenpeek, -1);
        if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
            rotatesprite((160L-(g_player[myconnectindex].ps->look_ang>>1))<<16,100L<<16,scale(65536,ud.crosshairscale,100),0,CROSSHAIR,0,CROSSHAIR_PAL,2+1,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
    }
#if 0
    if (GametypeFlags[ud.coop] & GAMETYPE_TDM)
    {
        for (i=0;i<ud.multimode;i++)
        {
            if (g_player[i].ps->team == g_player[myconnectindex].ps->team)
            {
                j = min(max((G_GetAngleDelta(getangle(g_player[i].ps->pos.x-g_player[myconnectindex].ps->pos.x,g_player[i].ps->pos.y-g_player[myconnectindex].ps->pos.y),g_player[myconnectindex].ps->ang))>>1,-160),160);
                rotatesprite((160-j)<<16,100L<<16,65536L,0,DUKEICON,0,0,2+1,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
            }
        }
    }
#endif

    if (ud.pause_on==1 && (g_player[myconnectindex].ps->gm&MODE_MENU) == 0)
        menutext(160,100,0,0,"GAME PAUSED");

    if (ud.coords)
        G_PrintCoords(screenpeek);

#if defined(USE_OPENGL)
    {
        extern int mdpause;

        mdpause = 0;
        if (ud.pause_on || (g_player[myconnectindex].ps->gm&MODE_MENU && numplayers < 2))
            mdpause = 1;
    }
#endif

    G_PrintFPS();

    // JBF 20040124: display level stats in screen corner
    if ((ud.overhead_on != 2 && ud.levelstats) && (g_player[myconnectindex].ps->gm&MODE_MENU) == 0)
    {
        if (ud.screen_size == 4)
        {
            i = scale(ud.althud?tilesiz[BIGALPHANUM].y+10:tilesiz[INVENTORYBOX].y+2,ud.statusbarscale,100);
//            j = scale(scale(6,ud.config.ScreenWidth,320),ud.statusbarscale,100);
        }
        else if (ud.screen_size > 2)
        {
            i = scale(tilesiz[BOTTOMSTATUSBAR].y+1,ud.statusbarscale,100);
            //          j = scale(2,ud.config.ScreenWidth,320);
        }
        else
        {
            i = 2;
            //        j = scale(2,ud.config.ScreenWidth,320);
        }
        j = scale(2,ud.config.ScreenWidth,320);

        Bsprintf(tempbuf,"T:^15%d:%02d.%02d",
                 (g_player[myconnectindex].ps->player_par/(26*60)),
                 (g_player[myconnectindex].ps->player_par/26)%60,
                 ((g_player[myconnectindex].ps->player_par%26)*38)/10
                );
        G_PrintGameText(13,STARTALPHANUM, j,scale(200-i,ud.config.ScreenHeight,200)-textsc(21),tempbuf,0,10,26,0, 0, xdim-1, ydim-1, 65536);

        if (ud.player_skill > 3 || (ud.multimode > 1 && !GTFLAGS(GAMETYPE_PLAYERSFRIENDLY)))
            Bsprintf(tempbuf,"K:^15%d",(ud.multimode>1 &&!GTFLAGS(GAMETYPE_PLAYERSFRIENDLY))?g_player[myconnectindex].ps->frag-g_player[myconnectindex].ps->fraggedself:g_player[myconnectindex].ps->actors_killed);
        else
        {
            if (g_player[myconnectindex].ps->actors_killed >= g_player[myconnectindex].ps->max_actors_killed)
                Bsprintf(tempbuf,"K:%d/%d",g_player[myconnectindex].ps->actors_killed,
                         g_player[myconnectindex].ps->max_actors_killed>g_player[myconnectindex].ps->actors_killed?
                         g_player[myconnectindex].ps->max_actors_killed:g_player[myconnectindex].ps->actors_killed);
            else
                Bsprintf(tempbuf,"K:^15%d/%d",g_player[myconnectindex].ps->actors_killed,
                         g_player[myconnectindex].ps->max_actors_killed>g_player[myconnectindex].ps->actors_killed?
                         g_player[myconnectindex].ps->max_actors_killed:g_player[myconnectindex].ps->actors_killed);
        }
        G_PrintGameText(13,STARTALPHANUM, j,scale(200-i,ud.config.ScreenHeight,200)-textsc(14),tempbuf,0,10,26,0, 0, xdim-1, ydim-1, 65536);

        if (g_player[myconnectindex].ps->secret_rooms == g_player[myconnectindex].ps->max_secret_rooms)
            Bsprintf(tempbuf,"S:%d/%d", g_player[myconnectindex].ps->secret_rooms,g_player[myconnectindex].ps->max_secret_rooms);
        else Bsprintf(tempbuf,"S:^15%d/%d", g_player[myconnectindex].ps->secret_rooms,g_player[myconnectindex].ps->max_secret_rooms);
        G_PrintGameText(13,STARTALPHANUM, j,scale(200-i,ud.config.ScreenHeight,200)-textsc(7),tempbuf,0,10,26,0, 0, xdim-1, ydim-1, 65536);
    }

    if (g_player[myconnectindex].ps->gm&MODE_TYPE)
        Net_EnterMessage();
    else
        M_DisplayMenus();

    {
        static int32_t applied = 0;

        if (tint.maxf)
        {
            G_FadePalaccum(&tint);
            applied = 1;
        }
        else if (applied)
        {
            // be sure to always un-apply a tint.
            videoFadePalette(0, 0, 0, 0);
            applied = 0;
        }
    }
}

static void G_DoThirdPerson(DukePlayer_t *pp, vec3_t *vect, short *vsectnum, int16_t ang, int16_t horiz)
{
    spritetype *sp = &sprite[pp->i];
    int i, hx, hy;
    int nx = (sintable[(ang+1536)&2047]>>4);
    int ny = (sintable[(ang+1024)&2047]>>4);
    int nz = (horiz - 100) * 128;
    int32_t daang;
    short bakcstat = sp->cstat;

    hitdata_t hit;

    sp->cstat &= (short)~0x101;

    updatesectorz(vect->x, vect->y, vect->z, vsectnum);

    hitscan(vect,*vsectnum,nx,ny,nz,&hit,CLIPMASK1);

    if (*vsectnum < 0)
    {
        sp->cstat = bakcstat;
        return;
    }

    hx = hit.pos.x - (vect->x);
    hy = hit.pos.y - (vect->y);

    if (klabs(nx)+klabs(ny) > klabs(hx)+klabs(hy))
    {
        *vsectnum = hit.sect;
        if (hit.wall >= 0)
        {
            daang = getangle(wall[wall[hit.wall].point2].x-wall[hit.wall].x,
                             wall[wall[hit.wall].point2].y-wall[hit.wall].y);

            i = nx*sintable[daang]+ny*sintable[(daang+1536)&2047];
            if (klabs(nx) > klabs(ny)) hx -= mulscale28(nx,i);
            else hy -= mulscale28(ny,i);
        }
        else if (hit.sprite < 0)
        {
            if (klabs(nx) > klabs(ny)) hx -= (nx>>5);
            else hy -= (ny>>5);
        }
        if (klabs(nx) > klabs(ny)) i = divscale16(hx,nx);
        else i = divscale16(hy,ny);
        if (i < g_cameraDistance) g_cameraDistance = i;
    }

    vect->x += mulscale16(nx, g_cameraDistance);
    vect->y += mulscale16(ny, g_cameraDistance);
    vect->z += mulscale16(nz, g_cameraDistance);

    g_cameraDistance = min(g_cameraDistance+(((int32_t)totalclock-g_cameraClock)<<10),65536);
    g_cameraClock = (int32_t)totalclock;

    updatesectorz(vect->x, vect->y, vect->z,vsectnum);

    sp->cstat = bakcstat;
}

//REPLACE FULLY
void G_DrawBackground(void)
{
    int dapicnum;
    int x,y,x1,y1,x2,y2,rx;

    renderFlushPerms();

    switch (ud.m_volume_number)
    {
    default:
        dapicnum = BIGHOLE;
        break;
    case 1:
        dapicnum = BIGHOLE;
        break;
    case 2:
        dapicnum = BIGHOLE;
        break;
    }

    if (tilesiz[dapicnum].x == 0 || tilesiz[dapicnum].y == 0)
    {
        pus = pub = NUMPAGES;
        return;
    }

    y1 = 0;
    y2 = ydim;
    if (g_player[myconnectindex].ps->gm & MODE_GAME || ud.recstat == 2)
        //if (ud.recstat == 0 || ud.recstat == 1 || (ud.recstat == 2 && ud.reccnt > 0)) // JBF 20040717
    {
        /*
        if (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR)
        {
            if (ud.multimode > 1) y1 += scale(ydim,8,200);
            if (ud.multimode > 4) y1 += scale(ydim,8,200);
        }
        */
    }
    else
    {
        // when not rendering a game, fullscreen wipe
#define MENUTILE (!videoGetRenderMode()?MENUSCREEN:LOADSCREEN)
        Gv_SetVar(g_iReturnVarID,tilesiz[MENUTILE].x==320&&tilesiz[MENUTILE].y==200?MENUTILE:BIGHOLE, -1, -1);
        X_OnEvent(EVENT_GETMENUTILE, -1, myconnectindex, -1);
        if (Gv_GetVarByLabel("MENU_TILE", tilesiz[MENUTILE].x==320&&tilesiz[MENUTILE].y==200?0:1, -1, -1))
        {
            for (y=y1;y<y2;y+=tilesiz[Gv_GetVar(g_iReturnVarID, -1, -1)].y)
                for (x=0;x<xdim;x+=tilesiz[Gv_GetVar(g_iReturnVarID, -1, -1)].x)
                    rotatesprite(x<<16,y<<16,65536L,0,Gv_GetVar(g_iReturnVarID, -1, -1),bpp==8?16:8,0,8+16+64,0,0,xdim-1,ydim-1);
        }
        else rotatesprite(320<<15,200<<15,65536L,0,Gv_GetVar(g_iReturnVarID, -1, -1),bpp==8?16:8,0,2+8+64,0,0,xdim-1,ydim-1);
        return;
    }

    y2 = scale(ydim,200-scale(tilesiz[BOTTOMSTATUSBAR].y,ud.statusbarscale,100),200);

    if ((ud.screen_size > 8) || ((GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR) && ud.screen_size >= 4))
    {
        // across top
        for (y = 0; y<windowxy1.y; y += tilesiz[dapicnum].y)
            for (x = 0; x<xdim; x += tilesiz[dapicnum].x)
                rotatesprite(x << 16, y << 16, 65536L, 0, dapicnum, 8, 0, 8 + 16 + 64, 0, y1, xdim - 1, windowxy1.y - 1);
    }

    if (ud.screen_size > 8)
    {
        // sides
        rx = windowxy2.x-windowxy2.x%tilesiz[dapicnum].x;
        for (y=windowxy1.y-windowxy1.y%tilesiz[dapicnum].y; y<windowxy2.y; y+=tilesiz[dapicnum].y)
            for (x=0; x<windowxy1.x || x+rx<xdim; x+=tilesiz[dapicnum].x)
            {
                rotatesprite(x<<16,y<<16,65536L,0,dapicnum,8,0,8+16+64,0,windowxy1.y,windowxy1.x-1,windowxy2.y-1);
                rotatesprite((x+rx)<<16,y<<16,65536L,0,dapicnum,8,0,8+16+64,windowxy2.x,windowxy1.y,xdim-1,windowxy2.y-1);
            }

        // along bottom
        for (y=windowxy2.y-(windowxy2.y%tilesiz[dapicnum].y); y<y2; y+=tilesiz[dapicnum].y)
            for (x=0; x<xdim; x+=tilesiz[dapicnum].x)
                rotatesprite(x<<16,y<<16,65536L,0,dapicnum,8,0,8+16+64,0,windowxy2.y,xdim-1,y2-1);
    }

    // draw in the bits to the left and right of the non-fullsize status bar
    if (ud.screen_size >= 8 && ud.statusbarmode == 0)
    {
        /*
        y1 = y2;
        x2 = (xdim - scale(xdim,ud.statusbarscale,100)) >> 1;
        x1 = xdim-x2;
        x1 -= x1%tilesiz[dapicnum].x;
        for (y=y1-y1%tilesiz[dapicnum].y; y<y2; y+=tilesiz[dapicnum].y)
        for (x=0;x<x2 || x1+x<xdim; x+=tilesiz[dapicnum].x)
        {
            rotatesprite(x<<16,y<<16,65536L,0,dapicnum,8,0,8+16+64,0,y1,x2-1,ydim-1);
            rotatesprite((x+x1)<<16,y<<16,65536L,0,dapicnum,8,0,8+16+64,xdim-x2,y1,xdim-1,ydim-1);
        }
        */
        // when not rendering a game, fullscreen wipe
        x2 = (xdim - scale((int32_t)(ydim*1.333333333333333333f), ud.statusbarscale, 100)) >> 1;
        for (y = y2 - y2%tilesiz[dapicnum].y; y<ydim; y += tilesiz[dapicnum].y)
            for (x = 0; x<xdim >> 1; x += tilesiz[dapicnum].x)
            {
                rotatesprite(x << 16, y << 16, 65536L, 0, dapicnum, 8, 0, 8 + 16 + 64, 0, y2, x2, ydim - 1);
                rotatesprite((xdim - x) << 16, y << 16, 65536L, 0, dapicnum, 8, 0, 8 + 16 + 64, xdim - x2 - 1, y2, xdim - 1, ydim - 1);
            }

    }

    if (ud.screen_size > 8)
    {
        y = 0;
        if (GametypeFlags[ud.coop] & GAMETYPE_FRAGBAR)
        {
            if (ud.multimode > 1) y += 8;
            if (ud.multimode > 4) y += 8;
        }

        x1 = max(windowxy1.x-4,0);
        y1 = max(windowxy1.y-4,y);
        x2 = min(windowxy2.x+4,xdim-1);
        y2 = min(windowxy2.y+4,scale(ydim,200-scale(tilesiz[BOTTOMSTATUSBAR].y,ud.statusbarscale,100),200)-1);

        for (y=y1+4;y<y2-4;y+=64)
        {
            rotatesprite(x1<<16,y<<16,65536L,0,VIEWBORDER,0,0,8+16+64,x1,y1,x2,y2);
            rotatesprite((x2+1)<<16,(y+64)<<16,65536L,1024,VIEWBORDER,0,0,8+16+64,x1,y1,x2,y2);
        }

        for (x=x1+4;x<x2-4;x+=64)
        {
            rotatesprite((x+64)<<16,y1<<16,65536L,512,VIEWBORDER,0,0,8+16+64,x1,y1,x2,y2);
            rotatesprite(x<<16,(y2+1)<<16,65536L,1536,VIEWBORDER,0,0,8+16+64,x1,y1,x2,y2);
        }

        rotatesprite(x1<<16,y1<<16,65536L,0,VIEWBORDER+1,0,0,8+16+64,x1,y1,x2,y2);
        rotatesprite((x2+1)<<16,y1<<16,65536L,512,VIEWBORDER+1,0,0,8+16+64,x1,y1,x2,y2);
        rotatesprite((x2+1)<<16,(y2+1)<<16,65536L,1024,VIEWBORDER+1,0,0,8+16+64,x1,y1,x2,y2);
        rotatesprite(x1<<16,(y2+1)<<16,65536L,1536,VIEWBORDER+1,0,0,8+16+64,x1,y1,x2,y2);
    }

    pus = pub = NUMPAGES;
}

static int32_t ror_sprite = -1;

extern float r_ambientlight;

char ror_protectedsectors[MAXSECTORS];
int32_t drawing_ror = 0;

static void G_OROR_DupeSprites(const spritetype *sp)
{
    // dupe the sprites touching the portal to the other sector
    int32_t k;
    const spritetype *refsp;

    refsp = &sprite[sp->yvel];

    for (k = headspritesect[sp->sectnum]; k != -1; k = nextspritesect[k])
    {
        if (spritesortcnt >= MAXSPRITESONSCREEN)
            break;

        if (sprite[k].picnum != SECTOREFFECTOR && (sprite[k].z >= sp->z) && !(sprite[k].cstat & 32768))
        {
            Bmemcpy(&tsprite[spritesortcnt], &sprite[k], sizeof(spritetype));

            tsprite[spritesortcnt].x += (refsp->x - sp->x);
            tsprite[spritesortcnt].y += (refsp->y - sp->y);
            tsprite[spritesortcnt].z = tsprite[spritesortcnt].z - sp->z + actor[sp->yvel].ceilingz;
            tsprite[spritesortcnt].sectnum = refsp->sectnum;
            tsprite[spritesortcnt].owner = k;

            //            OSD_Printf("duped sprite of pic %d at %d %d %d\n",tsprite[spritesortcnt].picnum,tsprite[spritesortcnt].x,tsprite[spritesortcnt].y,tsprite[spritesortcnt].z);
            spritesortcnt++;
        }
    }
}

static void G_SE40(int32_t smoothratio)
{
    if ((unsigned)ror_sprite < MAXSPRITES)
    {
        int32_t x, y, z;
        int16_t sect;
        int32_t level = 0;
        const spritetype *const sp = &sprite[ror_sprite];
        const int32_t sprite2 = sp->yvel;

        if ((unsigned)sprite2 >= MAXSPRITES)
            return;

        if (klabs(sector[sp->sectnum].floorz - sp->z) < klabs(sector[sprite[sprite2].sectnum].floorz - sprite[sprite2].z))
            level = 1;

        x = ud.camerapos.x - sp->x;
        y = ud.camerapos.y - sp->y;
        z = ud.camerapos.z - (level ? sector[sp->sectnum].floorz : sector[sp->sectnum].ceilingz);

        sect = sprite[sprite2].sectnum;
        updatesector(sprite[sprite2].x + x, sprite[sprite2].y + y, &sect);

        if (sect != -1)
        {
            int32_t renderz, picnum;
            // XXX: PK: too large stack allocation for my taste
            int16_t backupstat[MAXSECTORS];
            int32_t backupz[MAXSECTORS];
            int32_t i;
            int32_t pix_diff, newz;
            //                initprintf("drawing ror\n");

            if (level)
            {
                if (sp->ang < 1024)
                    renderz = sector[sprite[sprite2].sectnum].ceilingz;
                else
                    renderz = sprite[sprite2].z - (sprite[sprite2].yrepeat * tilesiz[sprite[sprite2].picnum].y << 1);

                picnum = sector[sprite[sprite2].sectnum].ceilingpicnum;
                sector[sprite[sprite2].sectnum].ceilingpicnum = 562;
                tilesiz[562].x = tilesiz[562].y = 0;

                pix_diff = klabs(z) >> 8;
                newz = -((pix_diff / 128) + 1) * (128 << 8);

                for (i = 0; i < numsectors; i++)
                {
                    backupstat[i] = sector[i].ceilingstat;
                    backupz[i] = sector[i].ceilingz;
                    if (!ror_protectedsectors[i] || (ror_protectedsectors[i] && sp->lotag == SE_41_ROR_LOWER))
                    {
                        sector[i].ceilingstat = 1;
                        sector[i].ceilingz += newz;
                    }
                }
            }
            else
            {
                if (sp->ang < 1024)
                    renderz = sector[sprite[sprite2].sectnum].floorz;
                else
                    renderz = sprite[sprite2].z;

                picnum = sector[sprite[sprite2].sectnum].floorpicnum;
                sector[sprite[sprite2].sectnum].floorpicnum = 562;
                tilesiz[562].x = tilesiz[562].y = 0;

                pix_diff = klabs(z) >> 8;
                newz = ((pix_diff / 128) + 1) * (128 << 8);

                for (i = 0; i < numsectors; i++)
                {
                    backupstat[i] = sector[i].floorstat;
                    backupz[i] = sector[i].floorz;
                    if (!ror_protectedsectors[i] || (ror_protectedsectors[i] && sp->lotag == SE_40_ROR_UPPER))
                    {
                        sector[i].floorstat = 1;
                        sector[i].floorz += newz;
                    }
                }
            }

            renderDrawRoomsQ16(sprite[sprite2].x + x, sprite[sprite2].y + y,
                z + renderz, ud.cameraq16ang, ud.cameraq16horiz, sect);
            drawing_ror = 1 + level;

            if (drawing_ror == 2) // viewing from top
                G_OROR_DupeSprites(sp);

            G_DoSpriteAnimations(ud.camerapos.x, ud.camerapos.y, ud.camerapos.z, fix16_to_int(ud.cameraq16ang), smoothratio);
            renderDrawMasks();

            if (level)
            {
                sector[sprite[sprite2].sectnum].ceilingpicnum = picnum;
                for (i = 0; i < numsectors; i++)
                {
                    sector[i].ceilingstat = backupstat[i];
                    sector[i].ceilingz = backupz[i];
                }
            }
            else
            {
                sector[sprite[sprite2].sectnum].floorpicnum = picnum;

                for (i = 0; i < numsectors; i++)
                {
                    sector[i].floorstat = backupstat[i];
                    sector[i].floorz = backupz[i];
                }
            }
        }
    }
}

void G_HandleMirror(int32_t x, int32_t y, int32_t z, fix16_t a, fix16_t q16horiz, int32_t smoothratio)
{
    if ((gotpic[MIRROR >> 3] & pow2char[MIRROR & 7]))
    {
        if (g_mirrorCount == 0)
        {
            // NOTE: We can have g_mirrorCount==0 but gotpic'd MIRROR,
            // for example in LNGA2.
            gotpic[MIRROR >> 3] &= ~pow2char[MIRROR & 7];

            //give scripts the chance to reset gotpics for effects that run in EVENT_DISPLAYROOMS
            //EVENT_RESETGOTPICS must be called after the last call to EVENT_DISPLAYROOMS in a frame, but before any engine-side renderDrawRoomsQ16
            //VM_OnEvent(EVENT_RESETGOTPICS, -1, -1);
            return;
        }

        int32_t i = 0, dst = INT32_MAX;

        for (bssize_t k = g_mirrorCount - 1; k >= 0; k--)
        {
            if (!wallvisible(x, y, g_mirrorWall[k]))
                continue;

            const int32_t j =
                klabs(wall[g_mirrorWall[k]].x - x) +
                klabs(wall[g_mirrorWall[k]].y - y);

            if (j < dst)
                dst = j, i = k;
        }

        if (wall[g_mirrorWall[i]].overpicnum != MIRROR)
        {
            // Try to find a new mirror wall in case the original one was broken.

            int32_t startwall = sector[g_mirrorSector[i]].wallptr;
            int32_t endwall = startwall + sector[g_mirrorSector[i]].wallnum;

            for (bssize_t k = startwall; k < endwall; k++)
            {
                int32_t j = wall[k].nextwall;
                if (j >= 0 && (wall[j].cstat & 32) && wall[j].overpicnum == MIRROR)  // cmp. premap.c
                {
                    g_mirrorWall[i] = j;
                    break;
                }
            }
        }

        if (wall[g_mirrorWall[i]].overpicnum == MIRROR)
        {
            int32_t tposx, tposy;
            fix16_t tang;

            //prepare to render any scripted EVENT_DISPLAYROOMS extras as mirrored
            renderPrepareMirror(x, y, z, a, q16horiz, g_mirrorWall[i], &tposx, &tposy, &tang);

            int32_t j = g_visibility;
            g_visibility = (j >> 1) + (j >> 2);

            //backup original camera position
            auto origCam = ud.camerapos;
            fix16_t origCamq16ang = ud.cameraq16ang;
            fix16_t origCamq16horiz = ud.cameraq16horiz;

            //set the camera inside the mirror facing out
            ud.camerapos = { tposx, tposy, z };
            ud.cameraq16ang = tang;
            ud.cameraq16horiz = q16horiz;

            display_mirror = 1;
            //VM_OnEventWithReturn(EVENT_DISPLAYROOMS, g_player[0].ps->i, 0, 0);
            X_OnEvent(EVENT_DISPLAYROOMS, g_player[screenpeek].ps->i, screenpeek, -1);
            display_mirror = 0;

            //reset the camera position
            ud.camerapos = origCam;
            ud.cameraq16ang = origCamq16ang;
            ud.cameraq16horiz = origCamq16horiz;

            //give scripts the chance to reset gotpics for effects that run in EVENT_DISPLAYROOMS
            //EVENT_RESETGOTPICS must be called after the last call to EVENT_DISPLAYROOMS in a frame, but before any engine-side renderDrawRoomsQ16
            //VM_OnEvent(EVENT_RESETGOTPICS, -1, -1);

            //prepare to render the mirror
            renderPrepareMirror(x, y, z, a, q16horiz, g_mirrorWall[i], &tposx, &tposy, &tang);

            int32_t didmirror;

            yax_preparedrawrooms();
            didmirror = renderDrawRoomsQ16(tposx, tposy, z, tang, q16horiz, g_mirrorSector[i] + MAXSECTORS);
            //POGO: if didmirror == 0, we may simply wish to abort instead of rendering with yax_drawrooms (which may require cleaning yax state)
            if (videoGetRenderMode() != REND_CLASSIC || didmirror)
                yax_drawrooms(G_DoSpriteAnimations, g_mirrorSector[i], didmirror, smoothratio);

            display_mirror = 1;
            G_DoSpriteAnimations(tposx, tposy, z, fix16_to_int(tang), smoothratio);
            display_mirror = 0;

            renderDrawMasks();
            renderCompleteMirror();   //Reverse screen x-wise in this function
            g_visibility = j;
        }
    }
}

static void G_ClearGotMirror()
{
    gotpic[MIRROR >> 3] &= ~pow2char[MIRROR & 7];
}

static inline vec3_t G_GetCameraPosition(int32_t const i, int32_t const smoothratio)
{
    auto const cs = (uspriteptr_t)&sprite[i];
    const ActorData_t* const ca = &actor[i];

    return { ca->bpos.x + mulscale16(cs->x - ca->bpos.x, smoothratio),
             ca->bpos.y + mulscale16(cs->y - ca->bpos.y, smoothratio),
             ca->bpos.z + mulscale16(cs->z - ca->bpos.z, smoothratio) };
}

static void G_CalculateCameraInterpolation(DukePlayer_t *p, vec3_t* pos, fix16_t* q16ang, fix16_t* q16horiz, int16_t* sectnum, int32_t smoothratio)
{
    pos->x = p->opos.x + mulscale16((int)(p->pos.x - p->opos.x), smoothratio);
    pos->y = p->opos.y + mulscale16((int)(p->pos.y - p->opos.y), smoothratio);
    pos->z = p->opos.z + mulscale16((int)(p->pos.z - p->opos.z), smoothratio);
    *q16ang = p->oq16ang + mulscale16(((p->q16ang + F16(1024) - p->oq16ang) & 0x7FFFFFF) - F16(1024), smoothratio);
    *q16horiz = p->oq16horiz + p->oq16horizoff + mulscale16((p->q16horiz + p->q16horizoff - p->oq16horiz - p->oq16horizoff), smoothratio);
    *sectnum = p->cursectnum;
    *q16ang += fix16_from_int(p->look_ang);

    if (p->over_shoulder_on == 0)
    {
        if (ud.viewbob)
            pos->z += p->opyoff + mulscale16((int)(p->pyoff - p->opyoff), smoothratio);
    }
}

void G_DrawRooms(int snum,int smoothratio)
{
    int floorZ, ceilZ;
    int i;
    DukePlayer_t *p = (screenpeek == myconnectindex && numplayers > 1) ? &predictedPlayer :  g_player[snum].ps;
    PlayerDisplay_t* pDisp = &g_player[snum].display;
    short tang;
    int tiltcx,tiltcy,tiltcs=0;    // JBF 20030807

    int const viewingRange = viewingrange;
    rotatespritesmoothratio = smoothratio;

    if (pub > 0 || videoGetRenderMode() >= 3) // JBF 20040101: redraw background always
    {
        if (ud.screen_size >= 8)
            G_DrawBackground();
        pub = 0;
    }

    X_OnEvent(EVENT_DISPLAYSTART, p->i, snum, -1);

    if (ud.overhead_on == 2 || ud.show_help || (p->cursectnum == -1 && videoGetRenderMode() < 3))
        return;

    if (r_usenewaspect)
    {
        newaspect_enable = 1;
        videoSetCorrectedAspect();
    }

//    smoothratio = min(max(smoothratio,0),65536);

    g_visibility = (int)(pDisp->visibility * ((numplayers > 1 && !ud.allow_visibility) ? 1.f : r_ambientlightrecip)); // Do not allow visibility changes in online play.

    if (ud.pause_on || g_player[snum].ps->on_crane > -1) smoothratio = 65536;

    ud.camerasect = p->cursectnum;

    if (videoGetRenderMode() < 3 && (ud.camerasect < 0 || ud.camerasect >= MAXSECTORS)) return;

    G_DoInterpolations(smoothratio);

    G_AnimateCamSprite();

    if (ud.camerasprite >= 0)
    {
        spritetype *pSprite = &sprite[ud.camerasprite];

        if (pSprite->yvel < 0) pSprite->yvel = -100;
        else if (pSprite->yvel > 199) pSprite->yvel = 300;

        ud.cameraq16ang = fix16_from_int(actor[ud.camerasprite].tempang
            +mulscale16(((pSprite->ang+1024-actor[ud.camerasprite].tempang)&2047)-1024,smoothratio));

#if 0//POLYMER
        if (videoGetRenderMode() == 4)
            polymer_setanimatesprites(G_DoSpriteAnimations, s->x, s->y, ud.cameraang, smoothratio);
#endif

        G_SE40(smoothratio);

        yax_preparedrawrooms();
        renderDrawRoomsQ16(pSprite->x, pSprite->y, pSprite->z-(4<<8),ud.cameraq16ang, pSprite->yvel, pSprite->sectnum);
        yax_drawrooms(G_DoSpriteAnimations, pSprite->sectnum, 0, smoothratio);
        G_DoSpriteAnimations(pSprite->x, pSprite->y, pSprite->z, fix16_to_int(ud.cameraq16ang),smoothratio);
        renderDrawMasks();
    }
    else
    {
        int vr = divscale22(1,sprite[p->i].yrepeat+28);
        int screenTilting = (videoGetRenderMode() == REND_CLASSIC
            && ((ud.screen_tilting && p->rotscrnang)));

        //vr = Blrintf(float(vr) * tanf(ud.fov * (fPI / 360.f))); // OLDMP FIXME: FOV Calc

        if (!r_usenewaspect)
            renderSetAspect(vr, yxaspect);
        else
            renderSetAspect(mulscale16(vr, viewingrange), yxaspect);

        if (g_screenCapture)
        {
            walock[TILE_SAVESHOT] = 199;
            if (waloff[TILE_SAVESHOT] == 0)
                g_cache.allocateBlock(&waloff[TILE_SAVESHOT],200*320,&walock[TILE_SAVESHOT]);
            renderSetTarget(TILE_SAVESHOT,200L,320L);
        }
        else if (screenTilting)
        {
            int32_t oviewingrange = viewingrange;  // save it from renderSetAspect()
            const int16_t tang = (ud.screen_tilting) ? p->rotscrnang : 0;

            if (tang == 1024)
                screenTilting = 2;
            else
            {
                // Maximum possible allocation size passed to allocache() below
                // since there is no equivalent of free() for allocache().
#if MAXYDIM >= 640
                int const maxTiltSize = 640 * 640;
#else
                int const maxTiltSize = 320 * 320;
#endif
                // To render a tilted screen in high quality, we need at least
                // 640 pixels of *Y* dimension.
#if MAXYDIM >= 640
                // We also need
                //  * xdim >= 640 since tiltcx will be passed as setview()'s x2
                //    which must be less than xdim.
                //  * ydim >= 640 (sic!) since the tile-to-draw-to will be set
                //    up with dimension 400x640, but the engine's arrays like
                //    lastx[] are alloc'd with *xdim* elements! (This point is
                //    the dynamic counterpart of the #if above since we now
                //    allocate these engine arrays tightly.)
                // XXX: The engine should be in charge of setting up everything
                // so that no oob access occur.
                if (xdim >= 640 && ydim >= 640)
                {
                    tiltcs = 2;
                    tiltcx = 640;
                    tiltcy = 400;
                }
                else
#endif
                {
                    // JBF 20030807: Increased tilted-screen quality
                    tiltcs = 1;

                    // NOTE: The same reflections as above apply here, too.
                    // TILT_SETVIEWTOTILE_320.
                    tiltcx = 320;
                    tiltcy = 200;
                }

                // If the view is rotated (not 0 or 180 degrees modulo 360 degrees),
                // we render onto a square tile and display a portion of that
                // rotated on-screen later on.
                const int32_t viewtilexsiz = (tang & 1023) ? tiltcx : tiltcy;
                const int32_t viewtileysiz = tiltcx;

                walock[TILE_TILT] = CACHE1D_PERMANENT;
                if (waloff[TILE_TILT] == 0)
                    g_cache.allocateBlock(&waloff[TILE_TILT], maxTiltSize, &walock[TILE_TILT]);

                renderSetTarget(TILE_TILT, viewtilexsiz, viewtileysiz);

                if ((tang & 1023) == 512)
                {
                    //Block off unscreen section of 90ø tilted screen
                    int const j = tiltcx - (60 * tiltcs);
                    for (bssize_t i = (60 * tiltcs) - 1; i >= 0; i--)
                    {
                        startumost[i] = 1;
                        startumost[i + j] = 1;
                        startdmost[i] = 0;
                        startdmost[i + j] = 0;
                    }
                }

                int vRange = (tang & 511);

                if (vRange > 256)
                    vRange = 512 - vRange;

                vRange = sintable[vRange + 512] * 8 + sintable[vRange] * 5;
                renderSetAspect(mulscale16(oviewingrange, vRange >> 1), yxaspect);
            }
        }
        else if (videoGetRenderMode() >= REND_POLYMOST && ud.screen_tilting)
        {
#ifdef USE_OPENGL
            // This screen rotation interp is crapola (doesn't look better than 30fps most of the time)
            // and, it just breaks all of the time in a jittery mess. Need to reimplement.
            //renderSetRollAngle(p->orotscrnang + mulscale16(((p->rotscrnang - p->orotscrnang + 1024)&2047)-1024,smoothratio));
            //p->orotscrnang = p->rotscrnang; // JBF: save it for next time

            renderSetRollAngle((p->rotscrnang & 2047));
#endif
        }
#ifdef USE_OPENGL
        else
        {
            renderSetRollAngle(0);
        }
#endif

        if (p->newowner < 0)
        {
            DukePlayer_t* camPlayer = (snum == myconnectindex) && (numplayers > 1) ? &predictedPlayer : p;

            G_CalculateCameraInterpolation(camPlayer, &ud.camerapos, &ud.cameraq16ang, &ud.cameraq16horiz, &ud.camerasect, smoothratio);

            if (p->over_shoulder_on > 0)
                G_DoThirdPerson(p, &ud.camerapos, &ud.camerasect, fix16_to_int(ud.cameraq16ang), fix16_to_int(ud.cameraq16horiz));
        } 
        else
        {
            vec3_t const camVect = G_GetCameraPosition(p->newowner, smoothratio);

            // Looking through viewscreen
            ud.camerapos = camVect;
            ud.cameraq16ang = fix16_from_int(sprite[p->newowner].ang) + fix16_from_int(p->look_ang);
            ud.cameraq16horiz = fix16_from_int(100 + sprite[p->newowner].shade);
            ud.camerasect = sprite[p->newowner].sectnum;
        }
        
        ceilZ = actor[p->i].ceilingz;
        floorZ = actor[p->i].floorz;

        if (g_earthquakeTime > 0 && p->on_ground == 1)
        {
            ud.camerapos.z += 256-(((g_earthquakeTime)&1)<<9);
            ud.cameraq16ang += fix16_from_int((2-((g_earthquakeTime)&2))<<2);
        }

        if (sprite[p->i].pal == 1) ud.camerapos.z -= (18<<8);

        if (p->newowner >= 0)
            ud.cameraq16horiz = fix16_from_int(100+sprite[p->newowner].shade);
        else if (p->spritebridge == 0)
        {
            if (ud.camerapos.z < (p->truecz + (4<<8))) ud.camerapos.z = ceilZ + (4<<8);
            else if (ud.camerapos.z > (p->truefz - (4<<8))) ud.camerapos.z = floorZ - (4<<8);
        }

        while (ud.camerasect >= 0)
        {
            getzsofslope(ud.camerasect,ud.camerapos.x,ud.camerapos.y,&ceilZ,&floorZ);
            if (yax_getbunch(ud.camerasect, YAX_CEILING) >= 0)
            {
                if (ud.camerapos.z < ceilZ)
                {
                    updatesectorz(ud.camerapos.x, ud.camerapos.y, ud.camerapos.z, &ud.camerasect);
                    break;  // since CAMERA(sect) might have been updated to -1
                    // NOTE: fist discovered in WGR2 SVN r134, til' death level 1
                    //  (Lochwood Hollow).  A problem REMAINS with Polymost, maybe classic!
                }
            }
            else
            {
                if (ud.camerapos.z < ceilZ + (4 << 8)) ud.camerapos.z = ceilZ + (4 << 8);
            }

            if (yax_getbunch(ud.camerasect, YAX_FLOOR) >= 0)
            {
                if (ud.camerapos.z > floorZ)
                    updatesectorz(ud.camerapos.x, ud.camerapos.y, ud.camerapos.z, &ud.camerasect);
            }
            else
            {
                if (ud.camerapos.z > floorZ - (4 << 8)) ud.camerapos.z = floorZ - (4 << 8);
            }

            break;
        }

        X_OnEvent(EVENT_DISPLAYROOMS, g_player[screenpeek].ps->i, screenpeek, -1);

        ud.cameraq16horiz = fix16_clamp(ud.cameraq16horiz, F16(HORIZ_MIN), F16(HORIZ_MAX));

        G_HandleMirror(ud.camerapos.x, ud.camerapos.y, ud.camerapos.z, ud.cameraq16ang, ud.cameraq16horiz, smoothratio);
        G_ClearGotMirror();

#if 0//POLYMER
        if (videoGetRenderMode() == 4)
            polymer_setanimatesprites(G_DoSpriteAnimations, ud.camerapos.x,ud.camerapos.y,ud.cameraang,smoothratio);
#endif
        G_SE40(smoothratio);

        yax_preparedrawrooms();
        renderDrawRoomsQ16(ud.camerapos.x,ud.camerapos.y,ud.camerapos.z,ud.cameraq16ang,ud.cameraq16horiz,ud.camerasect);
        yax_drawrooms(G_DoSpriteAnimations, ud.camerasect, 0, smoothratio);

        // dupe the sprites touching the portal to the other sector
        if ((unsigned)ror_sprite < MAXSPRITES && drawing_ror == 1)
            G_OROR_DupeSprites(&sprite[ror_sprite]);

        G_DoSpriteAnimations(ud.camerapos.x,ud.camerapos.y,ud.camerapos.z,fix16_to_int(ud.cameraq16ang),smoothratio);
        drawing_ror = 0;
        renderDrawMasks();

        if (g_screenCapture == 1)
        {
            renderRestoreTarget();
            g_screenCapture = 0;
            //            walock[TILE_SAVESHOT] = 1;
        }
        else if (videoGetRenderMode() == REND_CLASSIC && ((ud.screen_tilting && p->rotscrnang) || ud.detail==0))
        {
            if (ud.screen_tilting) tang = p->rotscrnang;
            else tang = 0;

            if (videoGetRenderMode() == REND_CLASSIC)
            {
                renderRestoreTarget();
                picanm[TILE_TILT].xofs = picanm[TILE_TILT].yofs = 0;

                i = (tang&511);
                if (i > 256) i = 512-i;
                i = sintable[i+512]*8 + sintable[i]*5;
                if ((1-ud.detail) == 0) i >>= 1;
                i>>=(tiltcs-1); // JBF 20030807
                rotatesprite(160<<16,100<<16,i,tang+512,TILE_TILT,0,0,4+2+64,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
                walock[TILE_TILT] = 199;
            }
        }
    }

    G_RestoreInterpolations();

    {
        // Totalclock count of last step of p->visibility converging towards
        // ud.const_visibility.
        static int32_t lastvist;
        const int32_t visdif = ud.const_visibility - pDisp->visibility;

        // Check if totalclock was cleared (e.g. restarted game).
        if (totalclock < lastvist)
            lastvist = 0;

        // Every 2nd totalclock increment (each 1/60th second), ...
        while (totalclock >= lastvist + 2)
        {
            // ... approximately three-quarter the difference between
            // p->visibility and ud.const_visibility.
            const int32_t visinc = visdif >> 2;

            if (klabs(visinc) == 0)
            {
                pDisp->visibility = ud.const_visibility;
                break;
            }

            pDisp->visibility += visinc;
            lastvist = (int32_t)totalclock;
        }
    }

    if (r_usenewaspect)
    {
        newaspect_enable = 0;
        renderSetAspect(viewingRange, tabledivide32_noinline(65536 * ydim * 8, xdim * 5));
    }
}

static void G_DumpDebugInfo(void)
{
    int i,j,x;
    int16_t q16ang;
   // FILE * fp=fopen("debug.con","w");

    X_ScriptInfo();
    OSD_Printf("\n");

    OSD_Printf("Current gamevar values:\n");
    for (i=0;i<MAX_WEAPONS;i++)
    {
        for (j=0;j<numplayers;j++)
        {
            OSD_Printf("Player %d\n\n",j);
            OSD_Printf("WEAPON%d_CLIP %" PRIdPTR "\n",i,aplWeaponClip[i][j]);
            OSD_Printf("WEAPON%d_RELOAD %" PRIdPTR "\n",i,aplWeaponReload[i][j]);
            OSD_Printf("WEAPON%d_FIREDELAY %" PRIdPTR "\n",i,aplWeaponFireDelay[i][j]);
            OSD_Printf("WEAPON%d_TOTALTIME %" PRIdPTR "\n",i,aplWeaponTotalTime[i][j]);
            OSD_Printf("WEAPON%d_HOLDDELAY %" PRIdPTR "\n",i,aplWeaponHoldDelay[i][j]);
            OSD_Printf("WEAPON%d_FLAGS %" PRIdPTR "\n",i,aplWeaponFlags[i][j]);
            OSD_Printf("WEAPON%d_SHOOTS %" PRIdPTR "\n",i,aplWeaponShoots[i][j]);
            OSD_Printf("WEAPON%d_SPAWNTIME %" PRIdPTR "\n",i,aplWeaponSpawnTime[i][j]);
            OSD_Printf("WEAPON%d_SPAWN %" PRIdPTR "\n",i,aplWeaponSpawn[i][j]);
            OSD_Printf("WEAPON%d_SHOTSPERBURST %" PRIdPTR "\n",i,aplWeaponShotsPerBurst[i][j]);
            OSD_Printf("WEAPON%d_WORKSLIKE %" PRIdPTR "\n",i,aplWeaponWorksLike[i][j]);
            OSD_Printf("WEAPON%d_INITIALSOUND %" PRIdPTR "\n",i,aplWeaponInitialSound[i][j]);
            OSD_Printf("WEAPON%d_FIRESOUND %" PRIdPTR "\n",i,aplWeaponFireSound[i][j]);
            OSD_Printf("WEAPON%d_SOUND2TIME %" PRIdPTR "\n",i,aplWeaponSound2Time[i][j]);
            OSD_Printf("WEAPON%d_SOUND2SOUND %" PRIdPTR "\n",i,aplWeaponSound2Sound[i][j]);
            OSD_Printf("WEAPON%d_RELOADSOUND1 %" PRIdPTR "\n",i,aplWeaponReloadSound1[i][j]);
            OSD_Printf("WEAPON%d_RELOADSOUND2 %" PRIdPTR "\n",i,aplWeaponReloadSound2[i][j]);
            OSD_Printf("WEAPON%d_SELECTSOUND %" PRIdPTR "\n",i,aplWeaponSelectSound[i][j]);
        }
        OSD_Printf("\n");
    }
    for (x=0;x<MAXSTATUS;x++)
    {
        j = headspritestat[x];
        while (j >= 0)
        {
            OSD_Printf("Sprite %d (%d,%d,%d) (picnum: %d)\n",j,TrackerCast(sprite[j].x),TrackerCast(sprite[j].y),TrackerCast(sprite[j].z),TrackerCast(sprite[j].picnum));
            for (i=0;i<g_gameVarCount;i++)
            {
                if (aGameVars[i].dwFlags & (GAMEVAR_PERACTOR))
                {
                    if (aGameVars[i].val.plValues[j] != aGameVars[i].lDefault)
                    {
                        OSD_Printf("gamevar %s ",aGameVars[i].szLabel);
                        OSD_Printf("%" PRIdPTR "",aGameVars[i].val.plValues[j]);
                        OSD_Printf(" GAMEVAR_PERACTOR");
                        if (aGameVars[i].dwFlags != GAMEVAR_PERACTOR)
                        {
                            OSD_Printf(" // ");
                            if (aGameVars[i].dwFlags & (GAMEVAR_SYSTEM))
                            {
                                OSD_Printf(" (system)");
                            }
                        }
                        OSD_Printf("\n");
                    }
                }
            }
            OSD_Printf("\n");
            j = nextspritestat[j];
        }
    }
    Gv_DumpValues();
   // fclose(fp);

    q16ang = fix16_to_int(g_player[myconnectindex].ps->q16ang);
    saveboard("debug.map",&g_player[myconnectindex].ps->pos, q16ang, g_player[myconnectindex].ps->cursectnum);
}

int A_InsertSprite(int whatsect,int s_x,int s_y,int s_z,int s_pn,int s_s,int s_xr,int s_yr,int s_a,int s_ve,int s_zv,int s_ow,int s_ss)
{
    int i = insertsprite(whatsect,s_ss);
    int p;
    spritetype *s = &sprite[i];

    if (oldnet_predicting)
    {
        OSD_Printf("^08A_InsertSprite^12: ^02ATTEMPTING TO SPAWN ACTOR FROM PREDICTING STATE! FAIL.\n");
        OSD_Printf("^12Owner SpriteNum:^07 %d, ^12Owner PicNum:^07 %d\n", s_ow, TrackerCast(sprite[s_ow].picnum));
        OSD_Printf("^12Spawnee SpriteNum:^07 %d, ^12Spawnee PicNum:^07 %d\n", i, s_pn);

        sprintf(tempbuf, "^02Critical Error in A_InsertSprite! See logs.");
        G_AddUserQuote(tempbuf);
        return -1;
    }

    if (i < 0)
    {
        G_DumpDebugInfo();
        OSD_Printf("Failed spawning sprite with tile %d from sprite %d (%d) at x:%d,y:%d,z:%d,sector:%d\n",s_pn,s_ow,TrackerCast(sprite[s_ow].picnum),s_x,s_y,s_z,whatsect);
        G_GameExit("Too many sprites spawned.");
    }

    actor[i].bpos.x = s_x;
    actor[i].bpos.y = s_y;
    actor[i].bpos.z = s_z;

    s->x = s_x;
    s->y = s_y;
    s->z = s_z;
    s->cstat = 0;
    s->picnum = s_pn;
    s->shade = s_s;
    s->xrepeat = s_xr;
    s->yrepeat = s_yr;
    s->pal = 0;

    s->ang = s_a;
    s->xvel = s_ve;
    s->zvel = s_zv;
    s->owner = s_ow;
    s->xoffset = 0;
    s->yoffset = 0;
    s->yvel = 0;
    s->clipdist = 0;
    s->pal = 0;
    s->lotag = 0;
    s->blend = 0;

    if (s_ow > -1 && s_ow < MAXSPRITES)
    {
        actor[i].picnum = sprite[s_ow].picnum;
        actor[i].floorz = actor[s_ow].floorz;
        actor[i].ceilingz = actor[s_ow].ceilingz;
    }

    actor[i].lastvx = 0;
    actor[i].lastvy = 0;

    actor[i].timetosleep = 0;
    actor[i].actorstayput = -1;
    actor[i].extra = -1;
    actor[i].owner = s_ow;
    actor[i].cgg = 0;
    actor[i].movflag = 0;
    actor[i].tempang = 0;
    actor[i].dispicnum = 0;

    T1(i)=T3(i)=T4(i)=T6(i)=T7(i)=T8(i)=T9(i)=0;

    actor[i].flags = 0;

    if (actorscrptr[s_pn])
    {
        s->extra = *actorscrptr[s_pn];
        T5(i) = *(actorscrptr[s_pn]+1);
        T2(i) = *(actorscrptr[s_pn]+2);
        s->hitag = *(actorscrptr[s_pn]+3);
    }
    else
    {
        T2(i)=T5(i)=0;
        s->extra = 0;
        s->hitag = 0;
    }

    if (show2dsector[SECT(i)>>3]&(1<<(SECT(i)&7))) show2dsprite[i>>3] |= (1<<(i&7));
    else show2dsprite[i>>3] &= ~(1<<(i&7));

    spriteext[i] = {};
    spritesmooth[i] = {};

    /*
        if(s->sectnum < 0)
        {
            s->xrepeat = s->yrepeat = 0;
            changespritestat(i, STAT_MISC);
        }
    */
    A_ResetVars(i);
    actor[i].flags = 0;

    if (s_ss == STAT_PROJECTILE) // Moved from PROJECTILE_RPG to here, so we can maniuplate these in EVENT_EGS.
    {
        auto pProj = &ProjectileData[s_pn];

        sprite[i].pal = (pProj->pal >= 0) ? pProj->pal : 0;
        sprite[i].cstat = (pProj->cstat >= 0) ? pProj->cstat : 128;
        sprite[i].clipdist = (pProj->clipdist != 255) ? pProj->clipdist : 40;
        sprite[i].shade = clamp(pProj->shade, -128, 127);

        actor[i].projectile = ProjectileData[s_pn];
    }

    if (apScriptGameEvent[EVENT_EGS])
    {
        extern int block_deletesprite;
        int pl=A_FindPlayer(&sprite[i],&p);

        block_deletesprite++;
        X_OnEvent(EVENT_EGS,i, pl, p);
        block_deletesprite--;
    }

    return(i);
}

int A_Spawn(int j, int pn)
{
    int i, s, startwall, endwall, sect, clostest=0;
    int x, y, d, p;
    spritetype *sp;

    if (j >= 0)
    {
        i = A_InsertSprite(sprite[j].sectnum,sprite[j].x,sprite[j].y,sprite[j].z
                           ,pn,0,0,0,0,0,0,j,0);
        actor[i].picnum = sprite[j].picnum;
    }
    else
    {
        i = pn;
        actor[i].picnum = PN(i);
        actor[i].timetosleep = 0;
        actor[i].extra = -1;

        actor[i].bpos = sprite[i].pos;

        OW(i) = actor[i].owner = i;
        actor[i].cgg = 0;
        actor[i].movflag = 0;
        actor[i].tempang = 0;
        actor[i].dispicnum = 0;
        actor[i].floorz = sector[SECT(i)].floorz;
        actor[i].ceilingz = sector[SECT(i)].ceilingz;

        actor[i].lastvx = 0;
        actor[i].lastvy = 0;
        actor[i].actorstayput = -1;

        T1(i) = T2(i) = T3(i) = T4(i) = T5(i) = T6(i) = T7(i) = T8(i) = T9(i) = 0;

        actor[i].flags = 0;

        if (PN(i) != SPEAKER && PN(i) != LETTER && PN(i) != DUCK && PN(i) != TARGET && PN(i) != TRIPBOMB && PN(i) != VIEWSCREEN && PN(i) != VIEWSCREEN2 && (CS(i)&48))
            if (!(PN(i) >= CRACK1 && PN(i) <= CRACK4))
            {
                if (SS(i) == 127) return i;
                if (A_CheckSwitchTile(i) == 1 && (CS(i)&16))
                {
                    if (PN(i) != ACCESSSWITCH && PN(i) != ACCESSSWITCH2 && sprite[i].pal)
                    {
                        if ((ud.multimode < 2) || (ud.multimode > 1 && !GTFLAGS(GAMETYPE_DMSWITCHES)))
                        {
                            sprite[i].xrepeat = sprite[i].yrepeat = 0;
                            sprite[i].cstat = SLT(i) = SHT(i) = 0;
                            return i;
                        }
                    }
                    CS(i) |= 257;
                    if (sprite[i].pal && PN(i) != ACCESSSWITCH && PN(i) != ACCESSSWITCH2)
                        sprite[i].pal = 0;
                    return i;
                }

                if (SHT(i))
                {
                    changespritestat(i, STAT_FALLER);
                    CS(i) |=  257;
                    SH(i) = g_impactDamage;
                    return i;
                }
            }

        s = PN(i);

        if (CS(i)&1) CS(i) |= 256;

        if (actorscrptr[s])
        {
            SH(i) = *(actorscrptr[s]);
            T5(i) = *(actorscrptr[s]+1);
            T2(i) = *(actorscrptr[s]+2);
            if (*(actorscrptr[s]+3) && SHT(i) == 0)
                SHT(i) = *(actorscrptr[s]+3);
        }
        else T2(i) = T5(i) = 0;
    }

    sp = &sprite[i];
    sect = sp->sectnum;

    //some special cases that can't be handled through the dynamictostatic system.
    if (((sp->picnum >= BOLT1)&&(sp->picnum <= BOLT1+3))||((sp->picnum >= SIDEBOLT1)&&(sp->picnum <= SIDEBOLT1+3)))
    {
        T1(i) = sp->xrepeat;
        T2(i) = sp->yrepeat;
        sp->yvel = 0;
        changespritestat(i, STAT_STANDABLE);
    }
    else if (((sp->picnum >= CAMERA1)&&(sp->picnum <= CAMERA1+3))||(sp->picnum==CAMERAPOLE)||(sp->picnum==GENERICPOLE))
    {
        if (sp->picnum != GENERICPOLE)
        {
            sp->extra = 1;

            if (g_damageCameras) sp->cstat = 257;
            else sp->cstat = 0;
        }
        if (ud.multimode < 2 && sp->pal != 0)
        {
            sp->xrepeat = sp->yrepeat = 0;
            changespritestat(i, STAT_MISC);

        }
        else
        {
            sp->pal = 0;
            if (!(sp->picnum == CAMERAPOLE || sp->picnum == GENERICPOLE))
            {
                sp->picnum = CAMERA1;
                changespritestat(i, STAT_ACTOR);
            }
        }
    }
    else switch (DynamicTileMap[sp->picnum])
        {
        default:
            if (actorscrptr[sp->picnum])
            {
                if (j == -1 && sp->lotag > ud.player_skill)
                {
                    sp->xrepeat=sp->yrepeat=0;
                    changespritestat(i, STAT_MISC);
                    break;
                }

                //  Init the size
                if (sp->xrepeat == 0 || sp->yrepeat == 0)
                    sp->xrepeat = sp->yrepeat = 1;

                if (A_CheckSpriteFlags(i, SFLAG_BADGUY))
                {
                    if (ud.monsters_off == 1)
                    {
                        sp->xrepeat=sp->yrepeat=0;
                        changespritestat(i, STAT_MISC);
                        break;
                    }

                    A_Fall(i);

                    if (A_CheckSpriteFlags(i, SFLAG_BADGUYSTAYPUT))
                        actor[i].actorstayput = sp->sectnum;

                    g_player[myconnectindex].ps->max_actors_killed++;
                    sp->clipdist = 80;
                    if (j >= 0)
                    {
                        if (sprite[j].picnum == RESPAWN)
                            actor[i].tempang = sprite[i].pal = sprite[j].pal;
                        changespritestat(i, STAT_ACTOR);
                    }
                    else changespritestat(i, STAT_ZOMBIEACTOR);
                }
                else
                {
                    sp->clipdist = 40;
                    sp->owner = i;
                    changespritestat(i, STAT_ACTOR);
                }

                actor[i].timetosleep = 0;

                if (j >= 0)
                    sp->ang = sprite[j].ang;
            }
            break;
        case FOF__STATIC:
            sp->xrepeat = sp->yrepeat = 0;
            changespritestat(i, STAT_MISC);
            break;
        case WATERSPLASH2__STATIC:
            if (j >= 0)
            {
                setspritez_old(i,sprite[j].x,sprite[j].y,sprite[j].z);
                sp->xrepeat = sp->yrepeat = 8+(krand()&7);
            }
            else sp->xrepeat = sp->yrepeat = 16+(krand()&15);

            sp->shade = -16;
            sp->cstat |= 128;
            if (j >= 0)
            {
                if (sector[sp->sectnum].lotag == ST_2_UNDERWATER)
                {
                    sp->z = getceilzofslope(SECT(i),SX(i),SY(i))+(16<<8);
                    sp->cstat |= 8;
                }
                else if (sector[sp->sectnum].lotag == ST_1_ABOVE_WATER)
                    sp->z = getflorzofslope(SECT(i),SX(i),SY(i));
            }

            if (sector[sect].floorpicnum == FLOORSLIME ||
                    sector[sect].ceilingpicnum == FLOORSLIME)
                sp->pal = 7;
            fallthrough__;
        case NEON1__STATIC:
        case NEON2__STATIC:
        case NEON3__STATIC:
        case NEON4__STATIC:
        case NEON5__STATIC:
        case NEON6__STATIC:
        case DOMELITE__STATIC:
            if (sp->picnum != WATERSPLASH2)
                sp->cstat |= 257;
            fallthrough__;
        case NUKEBUTTON__STATIC:
            if (sp->picnum == DOMELITE)
                sp->cstat |= 257;
            fallthrough__;
        case JIBS1__STATIC:
        case JIBS2__STATIC:
        case JIBS3__STATIC:
        case JIBS4__STATIC:
        case JIBS5__STATIC:
        case JIBS6__STATIC:
        case HEADJIB1__STATIC:
        case ARMJIB1__STATIC:
        case LEGJIB1__STATIC:
        case LIZMANHEAD1__STATIC:
        case LIZMANARM1__STATIC:
        case LIZMANLEG1__STATIC:
        case DUKETORSO__STATIC:
        case DUKEGUN__STATIC:
        case DUKELEG__STATIC:
            changespritestat(i, STAT_MISC);
            break;
        case TONGUE__STATIC:
            if (j >= 0)
                sp->ang = sprite[j].ang;
            sp->z -= 38<<8;
            sp->zvel = 256-(krand()&511);
            sp->xvel = 64-(krand()&127);
            changespritestat(i, STAT_PROJECTILE);
            break;
        case NATURALLIGHTNING__STATIC:
            sp->cstat &= ~257;
            sp->cstat |= 32768;
            break;
        case TRANSPORTERSTAR__STATIC:
        case TRANSPORTERBEAM__STATIC:
            if (j == -1) break;
            if (sp->picnum == TRANSPORTERBEAM)
            {
                sp->xrepeat = 31;
                sp->yrepeat = 1;
                sp->z = sector[sprite[j].sectnum].floorz-(40<<8);
            }
            else
            {
                if (sprite[j].statnum == 4)
                {
                    sp->xrepeat = 8;
                    sp->yrepeat = 8;
                }
                else
                {
                    sp->xrepeat = 48;
                    sp->yrepeat = 64;
                    if (sprite[j].statnum == 10 || A_CheckEnemySprite(&sprite[j]))
                        sp->z -= (32<<8);
                }
            }

            sp->shade = -127;
            sp->cstat = 128|2;
            sp->ang = sprite[j].ang;

            sp->xvel = 128;
            changespritestat(i, STAT_MISC);
            A_SetSprite(i,CLIPMASK0);
            setspritez_old(i,sp->x,sp->y,sp->z);
            break;

        case FRAMEEFFECT1_13__STATIC:
            if (PLUTOPAK) break;
            fallthrough__;
        case FRAMEEFFECT1__STATIC:
            if (j >= 0)
            {
                sp->xrepeat = sprite[j].xrepeat;
                sp->yrepeat = sprite[j].yrepeat;
                T2(i) = sprite[j].picnum;
            }
            else sp->xrepeat = sp->yrepeat = 0;

            changespritestat(i, STAT_MISC);

            break;

        case LASERLINE__STATIC:
            sp->yrepeat = 6;
            sp->xrepeat = 32;

            if (g_tripbombLaserMode == 1)
                sp->cstat = 16 + 2;
            else if (g_tripbombLaserMode == 0 || g_tripbombLaserMode == 2)
                sp->cstat = 16;
            else
            {
                sp->xrepeat = 0;
                sp->yrepeat = 0;
            }

            if (j >= 0) sp->ang = actor[j].temp_data[5]+512;
            changespritestat(i, STAT_MISC);
            break;

        case FORCESPHERE__STATIC:
            if (j == -1)
            {
                sp->cstat = (short) 32768;
                changespritestat(i, STAT_ZOMBIEACTOR);
            }
            else
            {
                sp->xrepeat = sp->yrepeat = 1;
                changespritestat(i, STAT_MISC);
            }
            break;

        case BLOOD__STATIC:
            sp->xrepeat = sp->yrepeat = 16;
            sp->z -= (26<<8);
            if (j >= 0 && sprite[j].pal == 6)
                sp->pal = 6;
            changespritestat(i, STAT_MISC);
            break;

        case BLOODPOOL__STATIC:
        case PUKE__STATIC:
        {
            short s1;
            s1 = sp->sectnum;

            updatesector(sp->x+108,sp->y+108,&s1);
            if (s1 >= 0 && sector[s1].floorz == sector[sp->sectnum].floorz)
            {
                updatesector(sp->x-108,sp->y-108,&s1);
                if (s1 >= 0 && sector[s1].floorz == sector[sp->sectnum].floorz)
                {
                    updatesector(sp->x+108,sp->y-108,&s1);
                    if (s1 >= 0 && sector[s1].floorz == sector[sp->sectnum].floorz)
                    {
                        updatesector(sp->x-108,sp->y+108,&s1);
                        if (s1 >= 0 && sector[s1].floorz != sector[sp->sectnum].floorz)
                        {
                            sp->xrepeat = sp->yrepeat = 0;
                            changespritestat(i, STAT_MISC);
                            break;
                        }

                    }
                    else
                    {
                        sp->xrepeat = sp->yrepeat = 0;
                        changespritestat(i, STAT_MISC);
                        break;
                    }

                }
                else
                {
                    sp->xrepeat = sp->yrepeat = 0;
                    changespritestat(i, STAT_MISC);
                    break;
                }

            }
            else
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
                break;
            }

        }

        if (sector[SECT(i)].lotag == ST_1_ABOVE_WATER)
        {
            changespritestat(i, STAT_MISC);
            break;
        }

        if (j >= 0 && sp->picnum != PUKE)
        {
            if (sprite[j].pal == 1)
                sp->pal = 1;
            else if (sprite[j].pal != 6 && sprite[j].picnum != NUKEBARREL && sprite[j].picnum != TIRE)
            {
                if (sprite[j].picnum == FECES)
                    sp->pal = 7; // Brown
                else sp->pal = 2; // Red
            }
            else sp->pal = 0;  // green

            if (sprite[j].picnum == TIRE)
                sp->shade = 127;
        }
        sp->cstat |= 32;
        fallthrough__;

        case FECES__STATIC:
            if (j >= 0)
                sp->xrepeat = sp->yrepeat = 1;
            changespritestat(i, STAT_MISC);
            break;

        case BLOODSPLAT1__STATIC:
        case BLOODSPLAT2__STATIC:
        case BLOODSPLAT3__STATIC:
        case BLOODSPLAT4__STATIC:
            sp->cstat |= 16;
            sp->xrepeat = 7+(krand()&7);
            sp->yrepeat = 7+(krand()&7);
            sp->z -= (16<<8);
            if (j >= 0 && sprite[j].pal == 6)
                sp->pal = 6;
            A_AddToDeleteQueue(i);
            changespritestat(i, STAT_MISC);
            break;

        case TRIPBOMB__STATIC:
            if (sp->lotag > ud.player_skill)
            {
                sp->xrepeat=sp->yrepeat=0;
                changespritestat(i, STAT_MISC);
                break;
            }

            sp->xrepeat=4;
            sp->yrepeat=5;

            sp->owner = i;
            sp->hitag = i;

            sp->xvel = 16;
            A_SetSprite(i,CLIPMASK0);
            actor[i].temp_data[0] = 17;
            actor[i].temp_data[2] = 0;
            actor[i].temp_data[5] = sp->ang;
            changespritestat(i, 2);
            break;

        case SPACEMARINE__STATIC:
            if (sp->picnum == SPACEMARINE)
            {
                sp->extra = 20;
                sp->cstat |= 257;
            }
            changespritestat(i, STAT_ZOMBIEACTOR);
            break;

        case HYDRENT__STATIC:
        case PANNEL1__STATIC:
        case PANNEL2__STATIC:
        case SATELITE__STATIC:
        case FUELPOD__STATIC:
        case SOLARPANNEL__STATIC:
        case ANTENNA__STATIC:
        case GRATE1__STATIC:
        case CHAIR1__STATIC:
        case CHAIR2__STATIC:
        case CHAIR3__STATIC:
        case BOTTLE1__STATIC:
        case BOTTLE2__STATIC:
        case BOTTLE3__STATIC:
        case BOTTLE4__STATIC:
        case BOTTLE5__STATIC:
        case BOTTLE6__STATIC:
        case BOTTLE7__STATIC:
        case BOTTLE8__STATIC:
        case BOTTLE10__STATIC:
        case BOTTLE11__STATIC:
        case BOTTLE12__STATIC:
        case BOTTLE13__STATIC:
        case BOTTLE14__STATIC:
        case BOTTLE15__STATIC:
        case BOTTLE16__STATIC:
        case BOTTLE17__STATIC:
        case BOTTLE18__STATIC:
        case BOTTLE19__STATIC:
        case OCEANSPRITE1__STATIC:
        case OCEANSPRITE2__STATIC:
        case OCEANSPRITE3__STATIC:
        case OCEANSPRITE5__STATIC:
        case MONK__STATIC:
        case INDY__STATIC:
        case LUKE__STATIC:
        case JURYGUY__STATIC:
        case SCALE__STATIC:
        case VACUUM__STATIC:
        case FANSPRITE__STATIC:
        case CACTUS__STATIC:
        case CACTUSBROKE__STATIC:
        case HANGLIGHT__STATIC:
        case FETUS__STATIC:
        case FETUSBROKE__STATIC:
        case CAMERALIGHT__STATIC:
        case MOVIECAMERA__STATIC:
        case IVUNIT__STATIC:
        case POT1__STATIC:
        case POT2__STATIC:
        case POT3__STATIC:
        case TRIPODCAMERA__STATIC:
        case SUSHIPLATE1__STATIC:
        case SUSHIPLATE2__STATIC:
        case SUSHIPLATE3__STATIC:
        case SUSHIPLATE4__STATIC:
        case SUSHIPLATE5__STATIC:
        case WAITTOBESEATED__STATIC:
        case VASE__STATIC:
        case PIPE1__STATIC:
        case PIPE2__STATIC:
        case PIPE3__STATIC:
        case PIPE4__STATIC:
        case PIPE5__STATIC:
        case PIPE6__STATIC:
            sp->clipdist = 32;
            sp->cstat |= 257;
            fallthrough__;
        case OCEANSPRITE4__STATIC:
            changespritestat(i, STAT_DEFAULT);
            break;
        case FEMMAG1__STATIC:
        case FEMMAG2__STATIC:
            sp->cstat &= ~257;
            changespritestat(i, STAT_DEFAULT);
            break;
        case DUKETAG__STATIC:
        case SIGN1__STATIC:
        case SIGN2__STATIC:
            if (ud.multimode < 2 && sp->pal)
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
            }
            else sp->pal = 0;
            break;
        case MASKWALL1__STATIC:
        case MASKWALL2__STATIC:
        case MASKWALL3__STATIC:
        case MASKWALL4__STATIC:
        case MASKWALL5__STATIC:
        case MASKWALL6__STATIC:
        case MASKWALL7__STATIC:
        case MASKWALL8__STATIC:
        case MASKWALL9__STATIC:
        case MASKWALL10__STATIC:
        case MASKWALL11__STATIC:
        case MASKWALL12__STATIC:
        case MASKWALL13__STATIC:
        case MASKWALL14__STATIC:
        case MASKWALL15__STATIC:
            j = sp->cstat&60;
            sp->cstat = j|1;
            changespritestat(i, STAT_DEFAULT);
            break;
        case FOOTPRINTS__STATIC:
        case FOOTPRINTS2__STATIC:
        case FOOTPRINTS3__STATIC:
        case FOOTPRINTS4__STATIC:
            if (j >= 0)
            {
                short s1;
                s1 = sp->sectnum;

                updatesector(sp->x+84,sp->y+84,&s1);
                if (s1 >= 0 && sector[s1].floorz == sector[sp->sectnum].floorz)
                {
                    updatesector(sp->x-84,sp->y-84,&s1);
                    if (s1 >= 0 && sector[s1].floorz == sector[sp->sectnum].floorz)
                    {
                        updatesector(sp->x+84,sp->y-84,&s1);
                        if (s1 >= 0 && sector[s1].floorz == sector[sp->sectnum].floorz)
                        {
                            updatesector(sp->x-84,sp->y+84,&s1);
                            if (s1 >= 0 && sector[s1].floorz != sector[sp->sectnum].floorz)
                            {
                                sp->xrepeat = sp->yrepeat = 0;
                                changespritestat(i, STAT_MISC);
                                break;
                            }

                        }
                        else
                        {
                            sp->xrepeat = sp->yrepeat = 0;
                            break;
                        }

                    }
                    else
                    {
                        sp->xrepeat = sp->yrepeat = 0;
                        break;
                    }

                }
                else
                {
                    sp->xrepeat = sp->yrepeat = 0;
                    break;
                }

                sp->cstat = 32+((g_player[sprite[j].yvel].ps->footprintcount&1)<<2);
                sp->ang = sprite[j].ang;
            }

            sp->z = sector[sect].floorz;
            if (sector[sect].lotag != ST_1_ABOVE_WATER && sector[sect].lotag != ST_2_UNDERWATER)
                sp->xrepeat = sp->yrepeat = 32;

            A_AddToDeleteQueue(i);
            changespritestat(i, STAT_MISC);
            break;

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
            sp->yvel = sp->hitag;
            sp->hitag = -1;
            if (sp->picnum == PODFEM1) sp->extra <<= 1;
            fallthrough__;
        case BLOODYPOLE__STATIC:

        case QUEBALL__STATIC:
        case STRIPEBALL__STATIC:

            if (sp->picnum == QUEBALL || sp->picnum == STRIPEBALL)
            {
                sp->cstat = 256;
                sp->clipdist = 8;
            }
            else
            {
                sp->cstat |= 257;
                sp->clipdist = 32;
            }

            changespritestat(i, STAT_ZOMBIEACTOR);
            break;

        case DUKELYINGDEAD__STATIC:
            if (j >= 0 && sprite[j].picnum == APLAYER)
            {
                sp->xrepeat = sprite[j].xrepeat;
                sp->yrepeat = sprite[j].yrepeat;
                sp->shade = sprite[j].shade;
                sp->pal = g_player[sprite[j].yvel].ps->palookup;
            }
            fallthrough__;
        case DUKECAR__STATIC:
        case HELECOPT__STATIC:
            //                if(sp->picnum == HELECOPT || sp->picnum == DUKECAR) sp->xvel = 1024;
            sp->cstat = 0;
            sp->extra = 1;
            sp->xvel = 292;
            sp->zvel = 360;
            fallthrough__;
        case RESPAWNMARKERRED__STATIC:
        case BLIMP__STATIC:

            if (sp->picnum == RESPAWNMARKERRED)
            {
                sp->xrepeat = sp->yrepeat = 24;
                if (j >= 0) sp->z = actor[j].floorz; // -(1<<4);
            }
            else
            {
                sp->cstat |= 257;
                sp->clipdist = 128;
            }
            fallthrough__;
        case MIKE__STATIC:
            if (sp->picnum == MIKE)
            {
                sp->yvel = sp->hitag;
                sp->hitag = 0;
            }
            fallthrough__;
        case WEATHERWARN__STATIC:
            changespritestat(i, STAT_ACTOR);
            break;

        case SPOTLITE__STATIC:
            T1(i) = sp->x;
            T2(i) = sp->y;
            break;
        case BULLETHOLE__STATIC:
            sp->xrepeat = sp->yrepeat = 3;
            sp->cstat = 16+(krand()&12);
            A_AddToDeleteQueue(i);
            changespritestat(i, STAT_MISC);
            break;
        case MONEY__STATIC:
        case MAIL__STATIC:
        case PAPER__STATIC:
            actor[i].temp_data[0] = krand()&2047;
            sp->cstat = krand()&12;
            sp->xrepeat = sp->yrepeat = 8;
            sp->ang = krand()&2047;

            changespritestat(i, STAT_MISC);
            break;

        case VIEWSCREEN__STATIC:
        case VIEWSCREEN2__STATIC:
            sp->owner = i;
            sp->lotag = 1;
            sp->extra = 1;
            changespritestat(i, STAT_STANDABLE);
            break;

        case SHELL__STATIC: //From the player
        case SHOTGUNSHELL__STATIC:
            if (j >= 0)
            {
                int snum,a;

                if (sprite[j].picnum == APLAYER)
                {
                    snum = sprite[j].yvel;
                    a = fix16_to_int(g_player[snum].ps->q16ang)-(krand()&63)+8;  //Fine tune

                    T1(i) = krand()&1;
                    sp->z = (3<<8)+g_player[snum].ps->pyoff+g_player[snum].ps->pos.z-
                            (fix16_to_int(g_player[snum].ps->q16horizoff+g_player[snum].ps->q16horiz-F16(100))<<4);
                    if (sp->picnum == SHOTGUNSHELL)
                        sp->z += (3<<8);
                    sp->zvel = -(krand()&255);
                }
                else
                {
                    a = sp->ang;
                    sp->z = sprite[j].z-PHEIGHT+(3<<8);
                }

                sp->x = sprite[j].x+(sintable[(a+512)&2047]>>7);
                sp->y = sprite[j].y+(sintable[a&2047]>>7);

                sp->shade = -8;

                if (sp->yvel == 1 || NAM)
                {
                    sp->ang = a+512;
                    sp->xvel = 30;
                }
                else
                {
                    sp->ang = a-512;
                    sp->xvel = 20;
                }
                sp->xrepeat=sp->yrepeat=4;

                changespritestat(i, STAT_MISC);
            }
            break;

        case RESPAWN__STATIC:
            sp->extra = 66-13;
            fallthrough__;
        case MUSICANDSFX__STATIC:
            if (ud.multimode < 2 && sp->pal == 1)
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
                break;
            }
            sp->cstat = (short)32768;
            changespritestat(i, STAT_FX);
            break;

        case EXPLOSION2__STATIC:
        case EXPLOSION2BOT__STATIC:
        case BURNING__STATIC:
        case BURNING2__STATIC:
        case SMALLSMOKE__STATIC:
        case SHRINKEREXPLOSION__STATIC:
        case COOLEXPLOSION1__STATIC:

            if (j >= 0)
            {
                sp->ang = sprite[j].ang;
                sp->shade = -64;
                sp->cstat = 128|(krand()&4);
            }

            if (sp->picnum == EXPLOSION2 || sp->picnum == EXPLOSION2BOT)
            {
                sp->xrepeat = 48;
                sp->yrepeat = 48;
                sp->shade = -127;
                sp->cstat |= 128;
            }
            else if (sp->picnum == SHRINKEREXPLOSION)
            {
                sp->xrepeat = 32;
                sp->yrepeat = 32;
            }
            else if (sp->picnum == SMALLSMOKE)
            {
                // 64 "money"
                sp->xrepeat = 24;
                sp->yrepeat = 24;
            }
            else if (sp->picnum == BURNING || sp->picnum == BURNING2)
            {
                sp->xrepeat = 4;
                sp->yrepeat = 4;
            }

            if (j >= 0)
            {
                x = yax_getflorzofslope(sp->sectnum,sp->pos.vec2);
                if (sp->z > x-(12<<8))
                    sp->z = x-(12<<8);
            }

            changespritestat(i, STAT_MISC);

            break;

        case PLAYERONWATER__STATIC:
            if (j >= 0)
            {
                sp->xrepeat = sprite[j].xrepeat;
                sp->yrepeat = sprite[j].yrepeat;
                sp->zvel = 128;
                if (sector[sp->sectnum].lotag != ST_2_UNDERWATER)
                    sp->cstat |= 32768;
            }
            changespritestat(i, STAT_DUMMYPLAYER);
            break;

        case APLAYER__STATIC:
            sp->xrepeat = sp->yrepeat = 0;
            //j = ud.coop;
            //if(j == 2) j = 0;
            j=(GametypeFlags[ud.coop] & GAMETYPE_COOPSPAWN) / GAMETYPE_COOPSPAWN ;
            if (ud.multimode < 2 || (ud.multimode > 1 && j != sp->lotag))
                changespritestat(i, STAT_MISC);
            else
                changespritestat(i, STAT_PLAYER);
            break;
        case WATERBUBBLE__STATIC:
            if (j >= 0 && sprite[j].picnum == APLAYER)
                sp->z -= (16<<8);
            if (sp->picnum == WATERBUBBLE)
            {
                if (j >= 0)
                    sp->ang = sprite[j].ang;
                sp->xrepeat = sp->yrepeat = 4;
            }
            else sp->xrepeat = sp->yrepeat = 32;

            changespritestat(i, STAT_MISC);
            break;

        case CRANE__STATIC:

            sp->cstat |= 64|257;

            sp->picnum += 2;
            sp->z = sector[sect].ceilingz+(48<<8);
            T5(i) = tempwallptr;

            g_origins[tempwallptr].x = sp->x;
            g_origins[tempwallptr].y = sp->y;
            g_origins[tempwallptr+2].x = sp->z;

            s = headspritestat[STAT_DEFAULT];
            while (s >= 0)
            {
                if (sprite[s].picnum == CRANEPOLE && SHT(i) == (sprite[s].hitag))
                {
                    g_origins[tempwallptr+2].y = s;

                    T2(i) = sprite[s].sectnum;

                    sprite[s].xrepeat = 48;
                    sprite[s].yrepeat = 128;

                    g_origins[tempwallptr+1].x = sprite[s].x;
                    g_origins[tempwallptr+1].y = sprite[s].y;

                    sprite[s].x = sp->x;
                    sprite[s].y = sp->y;
                    sprite[s].z = sp->z;
                    sprite[s].shade = sp->shade;

                    setspritez_old(s,sprite[s].x,sprite[s].y,sprite[s].z);
                    break;
                }
                s = nextspritestat[s];
            }

            tempwallptr += 3;
            sp->owner = -1;
            sp->extra = 8;
            changespritestat(i, STAT_STANDABLE);
            break;

        case WATERDRIP__STATIC:
            if (j >= 0 && (sprite[j].statnum == 10 || sprite[j].statnum == 1))
            {
                sp->shade = 32;
                if (sprite[j].pal != 1)
                {
                    sp->pal = 2;
                    sp->z -= (18<<8);
                }
                else sp->z -= (13<<8);
                sp->ang = getangle(g_player[connecthead].ps->pos.x-sp->x,g_player[connecthead].ps->pos.y-sp->y);
                sp->xvel = 48-(krand()&31);
                A_SetSprite(i,CLIPMASK0);
            }
            else if (j == -1)
            {
                sp->z += (4<<8);
                T1(i) = sp->z;
                T2(i) = krand()&127;
            }
            
            sp->xrepeat = 24;
            sp->yrepeat = 24;

            changespritestat(i, STAT_STANDABLE);
            break;

        case TRASH__STATIC:
            sp->ang = krand()&2047;

            sp->xrepeat = 24;
            sp->yrepeat = 24;

            changespritestat(i, STAT_STANDABLE);
            break;

        case WATERDRIPSPLASH__STATIC:

            sp->xrepeat = 24;
            sp->yrepeat = 24;

            changespritestat(i, STAT_STANDABLE);
            break;

        case PLUG__STATIC:
            sp->lotag = 9999;
            changespritestat(i, STAT_STANDABLE);
            break;
        case TOUCHPLATE__STATIC:
            T3(i) = sector[sect].floorz;
            if (sector[sect].lotag != ST_1_ABOVE_WATER && sector[sect].lotag != ST_2_UNDERWATER)
                sector[sect].floorz = sp->z;
            if ((sp->pal == 1 && ud.multimode > 1) ||
                (sp->pal == 2 && (!GTFLAGS(GAMETYPE_COOP) || ud.multimode < 2)) || // World Tour
                (sp->pal == 3 && (GTFLAGS(GAMETYPE_COOP) || ud.multimode < 2)) || // World Tour
                (sp->pal >= 4 && ud.multimode > 1)
                )
            {
                sp->xrepeat=sp->yrepeat=0;
                changespritestat(i, STAT_MISC);
                break;
            }

            sp->cstat |= 32768;
            changespritestat(i, STAT_STANDABLE);
            break;
        case WATERBUBBLEMAKER__STATIC:
            if (sp->hitag && sp->picnum == WATERBUBBLEMAKER)
            {
                // JBF 20030913: Pisses off X_Move(), eg. in bobsp2
                OSD_Printf(OSD_ERROR "WARNING: WATERBUBBLEMAKER %d @ %d,%d with hitag!=0. Applying fixup.\n",
                           i,TrackerCast(sp->x),TrackerCast(sp->y));
                sp->hitag = 0;
            }
            sp->cstat |= 32768;
            changespritestat(i, STAT_STANDABLE);
            break;

        case MASTERSWITCH__STATIC:
            if (sp->picnum == MASTERSWITCH)
                sp->cstat |= 32768;
            sp->yvel = 0;
            changespritestat(i, STAT_STANDABLE);
            break;
        case TARGET__STATIC:
        case DUCK__STATIC:
        case LETTER__STATIC:
            sp->extra = 1;
            sp->cstat |= 257;
            changespritestat(i, STAT_ACTOR);
            break;
        case OCTABRAINSTAYPUT__STATIC:
        case LIZTROOPSTAYPUT__STATIC:
        case PIGCOPSTAYPUT__STATIC:
        case LIZMANSTAYPUT__STATIC:
        case BOSS1STAYPUT__STATIC:
        case PIGCOPDIVE__STATIC:
        case COMMANDERSTAYPUT__STATIC:
        case BOSS4STAYPUT__STATIC:
            actor[i].actorstayput = sp->sectnum;
            fallthrough__;
        case BOSS1__STATIC:
        case BOSS2__STATIC:
        case BOSS3__STATIC:
        case BOSS4__STATIC:
        case ROTATEGUN__STATIC:
        case GREENSLIME__STATIC:
            if (sp->picnum == GREENSLIME)
                sp->extra = 1;
            fallthrough__;
        case DRONE__STATIC:
        case LIZTROOPONTOILET__STATIC:
        case LIZTROOPJUSTSIT__STATIC:
        case LIZTROOPSHOOT__STATIC:
        case LIZTROOPJETPACK__STATIC:
        case LIZTROOPDUCKING__STATIC:
        case LIZTROOPRUNNING__STATIC:
        case LIZTROOP__STATIC:
        case OCTABRAIN__STATIC:
        case COMMANDER__STATIC:
        case PIGCOP__STATIC:
        case LIZMAN__STATIC:
        case LIZMANSPITTING__STATIC:
        case LIZMANFEEDING__STATIC:
        case LIZMANJUMP__STATIC:
        case ORGANTIC__STATIC:
        case RAT__STATIC:
        case SHARK__STATIC:

            if (sp->pal == 0)
            {
                switch (DynamicTileMap[sp->picnum])
                {
                case LIZTROOPONTOILET__STATIC:
                case LIZTROOPSHOOT__STATIC:
                case LIZTROOPJETPACK__STATIC:
                case LIZTROOPDUCKING__STATIC:
                case LIZTROOPRUNNING__STATIC:
                case LIZTROOPSTAYPUT__STATIC:
                case LIZTROOPJUSTSIT__STATIC:
                case LIZTROOP__STATIC:
                    sp->pal = 22;
                    break;
                }
            }

            if (sp->picnum == BOSS4STAYPUT || sp->picnum == BOSS1 || sp->picnum == BOSS2 || sp->picnum == BOSS1STAYPUT || sp->picnum == BOSS3 || sp->picnum == BOSS4)
            {
                if (j >= 0 && sprite[j].picnum == RESPAWN)
                    sp->pal = sprite[j].pal;
                if (sp->pal)
                {
                    sp->clipdist = 80;
                    sp->xrepeat = 40;
                    sp->yrepeat = 40;
                }
                else
                {
                    sp->xrepeat = 80;
                    sp->yrepeat = 80;
                    sp->clipdist = 164;
                }
            }
            else
            {
                if (sp->picnum != SHARK)
                {
                    sp->xrepeat = 40;
                    sp->yrepeat = 40;
                    sp->clipdist = 80;
                }
                else
                {
                    sp->xrepeat = 60;
                    sp->yrepeat = 60;
                    sp->clipdist = 40;
                }
            }

            if (j >= 0) sp->lotag = 0;

            if ((sp->lotag > ud.player_skill) || ud.monsters_off == 1)
            {
                sp->xrepeat=sp->yrepeat=0;
                changespritestat(i, STAT_MISC);
                break;
            }
            else
            {
                A_Fall(i);

                if (sp->picnum == RAT)
                {
                    sp->ang = krand()&2047;
                    sp->xrepeat = sp->yrepeat = 48;
                    sp->cstat = 0;
                }
                else
                {
                    sp->cstat |= 257;

                    if (sp->picnum != SHARK)
                        g_player[myconnectindex].ps->max_actors_killed++;
                }

                if (sp->picnum == ORGANTIC) sp->cstat |= 128;

                if (j >= 0)
                {
                    actor[i].timetosleep = 0;
                    A_PlayAlertSound(i);
                    changespritestat(i, STAT_ACTOR);
                }
                else changespritestat(i, STAT_ZOMBIEACTOR);
            }

            if (sp->picnum == ROTATEGUN)
                sp->zvel = 0;

            break;

        case LOCATORS__STATIC:
            sp->cstat |= 32768;
            changespritestat(i, STAT_LOCATOR);
            break;

        case ACTIVATORLOCKED__STATIC:
        case ACTIVATOR__STATIC:
            sp->cstat = (short) 32768;
            if (sp->picnum == ACTIVATORLOCKED)
                sector[sp->sectnum].lotag |= 16384;
            changespritestat(i, STAT_ACTIVATOR);
            break;

        case DOORSHOCK__STATIC:
            sp->cstat |= 1+256;
            sp->shade = -12;
            changespritestat(i, STAT_STANDABLE);
            break;

        case OOZ__STATIC:
        case OOZ2__STATIC:
            sp->shade = -12;

            if (j >= 0)
            {
                if (sprite[j].picnum == NUKEBARREL)
                    sp->pal = 8;
                A_AddToDeleteQueue(i);
            }

            changespritestat(i, STAT_ACTOR);

            A_GetZLimits(i);

            j = (actor[i].floorz-actor[i].ceilingz)>>9;

            sp->yrepeat = j;
            sp->xrepeat = 25-(j>>1);
            sp->cstat |= (krand()&4);

            break;

        case HEAVYHBOMB__STATIC:
            if (j >= 0)
                sp->owner = j;
            else sp->owner = i;
            sp->xrepeat = sp->yrepeat = 9;
            sp->yvel = 4;
            fallthrough__;

        case REACTOR2__STATIC:
        case REACTOR__STATIC:
        case RECON__STATIC:

            if (sp->picnum == RECON)
            {
                if (sp->lotag > ud.player_skill)
                {
                    sp->xrepeat = sp->yrepeat = 0;
                    changespritestat(i, STAT_MISC);
                    return i;
                }
                g_player[myconnectindex].ps->max_actors_killed++;
                actor[i].temp_data[5] = 0;
                if (ud.monsters_off == 1)
                {
                    sp->xrepeat = sp->yrepeat = 0;
                    changespritestat(i, STAT_MISC);
                    break;
                }
                sp->extra = 130;
            }

            if (sp->picnum == REACTOR || sp->picnum == REACTOR2)
                sp->extra = g_impactDamage;

            CS(i) |= 257; // Make it hitable

            if (ud.multimode < 2 && sp->pal != 0)
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
                break;
            }
            sp->pal = 0;
            SS(i) = -17;

            changespritestat(i, STAT_ZOMBIEACTOR);
            break;

        case ATOMICHEALTH__STATIC:
        case STEROIDS__STATIC:
        case HEATSENSOR__STATIC:
        case SHIELD__STATIC:
        case AIRTANK__STATIC:
        case TRIPBOMBSPRITE__STATIC:
        case JETPACK__STATIC:
        case HOLODUKE__STATIC:

        case FIRSTGUNSPRITE__STATIC:
        case CHAINGUNSPRITE__STATIC:
        case SHOTGUNSPRITE__STATIC:
        case RPGSPRITE__STATIC:
        case SHRINKERSPRITE__STATIC:
        case FREEZESPRITE__STATIC:
        case DEVISTATORSPRITE__STATIC:

        case SHOTGUNAMMO__STATIC:
        case FREEZEAMMO__STATIC:
        case HBOMBAMMO__STATIC:
        case CRYSTALAMMO__STATIC:
        case GROWAMMO__STATIC:
        case BATTERYAMMO__STATIC:
        case DEVISTATORAMMO__STATIC:
        case RPGAMMO__STATIC:
        case BOOTS__STATIC:
        case AMMO__STATIC:
        case AMMOLOTS__STATIC:
        case COLA__STATIC:
        case FIRSTAID__STATIC:
        case SIXPAK__STATIC:

            if (j >= 0)
            {
                sp->lotag = 0;
                sp->z -= (32<<8);
                sp->zvel = -1024;
                A_SetSprite(i,CLIPMASK0);
                sp->cstat = krand()&4;
            }
            else
            {
                sp->owner = i;
                sp->cstat = 0;
            }

            if ((ud.multimode < 2 && sp->pal != 0) || (sp->lotag > ud.player_skill))
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
                break;
            }

            sp->pal = 0;
            fallthrough__;

        case ACCESSCARD__STATIC:

            if (sp->picnum == ATOMICHEALTH)
                sp->cstat |= 128;

            if (ud.multimode > 1 && !GTFLAGS(GAMETYPE_ACCESSCARDSPRITES) && sp->picnum == ACCESSCARD)
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
                break;
            }
            else
            {
                if (sp->picnum == AMMO)
                    sp->xrepeat = sp->yrepeat = 16;
                else sp->xrepeat = sp->yrepeat = 32;
            }

            sp->shade = -17;

            if (j >= 0) changespritestat(i, STAT_ACTOR);
            else
            {
                changespritestat(i, STAT_ZOMBIEACTOR);
                A_Fall(i);
            }
            break;

        case WATERFOUNTAIN__STATIC:
            SLT(i) = 1;
            fallthrough__;

        case TREE1__STATIC:
        case TREE2__STATIC:
        case TIRE__STATIC:
        case CONE__STATIC:
        case BOX__STATIC:
            CS(i) = 257; // Make it hittable
            sprite[i].extra = 1;
            changespritestat(i, STAT_STANDABLE);
            break;

        case FLOORFLAME__STATIC:
            sp->shade = -127;
            changespritestat(i, STAT_STANDABLE);
            break;

        case BOUNCEMINE__STATIC:
            sp->owner = i;
            sp->cstat |= 1+256; //Make it hitable
            sp->xrepeat = sp->yrepeat = 24;
            sp->shade = -127;
            sp->extra = g_impactDamage<<2;
            changespritestat(i, STAT_ZOMBIEACTOR);
            break;

        case STEAM__STATIC:
            if (j >= 0)
            {
                sp->ang = sprite[j].ang;
                sp->cstat = 16+128+2;
                sp->xrepeat=sp->yrepeat=1;
                sp->xvel = -8;
                A_SetSprite(i,CLIPMASK0);
            }
            changespritestat(i, STAT_STANDABLE);
            break;

        case CEILINGSTEAM__STATIC:
            changespritestat(i, STAT_STANDABLE);
            break;

        case SECTOREFFECTOR__STATIC:
            sp->cstat |= 32768;
            sp->xrepeat = sp->yrepeat = 0;

            switch (sp->lotag)
            {
            case SE_40_ROR_UPPER:
            case SE_41_ROR_LOWER:
                sp->cstat = 32;
                sp->xrepeat = sp->yrepeat = 64;
                changespritestat(i, STAT_EFFECTOR);
                for (j = 0; j < MAXSPRITES; j++)
                    if (sprite[j].picnum == SECTOREFFECTOR && (sprite[j].lotag == SE_40_ROR_UPPER || sprite[j].lotag == SE_41_ROR_LOWER) &&
                        sprite[j].hitag == sp->hitag && i != j)
                    {
                        //                        initprintf("found ror match\n");
                        sp->yvel = j;
                        break;
                    }
                goto SPAWN_END;
                break;
            case SE_46_ROR_PROTECTED:
                ror_protectedsectors[sp->sectnum] = 1;
                fallthrough__;
            case SE_49_POINT_LIGHT:
            case SE_50_SPOT_LIGHT:
                changespritestat(i, STAT_EFFECTOR);
                goto SPAWN_END;
                break;
            }

            sp->yvel = sector[sect].extra;

            switch (sp->lotag)
            {
            case SE_28_LIGHTNING:
                T6(i) = 65;// Delay for lightning
                break;
            case SE_7_TELEPORT: // Transporters!!!!
            case SE_23_ONE_WAY_TELEPORT:// XPTR END
                if (sp->lotag != SE_23_ONE_WAY_TELEPORT)
                {
                    for (j=0;j<MAXSPRITES;j++)
                        if (sprite[j].statnum < MAXSTATUS && sprite[j].picnum == SECTOREFFECTOR && (sprite[j].lotag == SE_7_TELEPORT || sprite[j].lotag == SE_23_ONE_WAY_TELEPORT) && i != j && sprite[j].hitag == SHT(i))
                        {
                            OW(i) = j;
                            break;
                        }
                }
                else OW(i) = i;

                T5(i) = sector[sect].floorz == SZ(i);
                sp->cstat = 0;
                changespritestat(i, STAT_TRANSPORT);
                return i;
            case SE_1_PIVOT:
                sp->owner = -1;
                T1(i) = 1;
                break;
            case SE_18_INCREMENTAL_SECTOR_RISE_FALL:

                if (sp->ang == 512)
                {
                    T2(i) = sector[sect].ceilingz;
                    if (sp->pal)
                        sector[sect].ceilingz = sp->z;
                }
                else
                {
                    T2(i) = sector[sect].floorz;
                    if (sp->pal)
                        sector[sect].floorz = sp->z;
                }

                sp->hitag <<= 2;
                break;

            case SE_19_EXPLOSION_LOWERS_CEILING:
                sp->owner = -1;
                break;
            case SE_25_PISTON: // Pistons
                T4(i) = sector[sect].ceilingz;
                T5(i) = 1;
                sector[sect].ceilingz = sp->z;
                G_SetInterpolation(&sector[sect].ceilingz);
                break;
            case SE_35:
                sector[sect].ceilingz = sp->z;
                break;
            case SE_27_DEMO_CAM:
                if (ud.recstat == 1)
                {
                    sp->xrepeat=sp->yrepeat=64;
                    sp->cstat &= 32768;
                }
                break;
            case SE_12_LIGHT_SWITCH:

                T2(i) = sector[sect].floorshade;
                T3(i) = sector[sect].ceilingshade;
                break;

            case SE_13_EXPLOSIVE:

                T1(i) = sector[sect].ceilingz;
                T2(i) = sector[sect].floorz;

                if (klabs(T1(i)-sp->z) < klabs(T2(i)-sp->z))
                    sp->owner = 1;
                else sp->owner = 0;

                if (sp->ang == 512)
                {
                    if (sp->owner)
                        sector[sect].ceilingz = sp->z;
                    else
                        sector[sect].floorz = sp->z;
                }
                else
                    sector[sect].ceilingz = sector[sect].floorz = sp->z;

                if (sector[sect].ceilingstat&1)
                {
                    sector[sect].ceilingstat ^= 1;
                    T4(i) = 1;

                    if (!sp->owner && sp->ang==512)
                    {
                        sector[sect].ceilingstat ^= 1;
                        T4(i) = 0;
                    }

                    sector[sect].ceilingshade =
                        sector[sect].floorshade;

                    if (sp->ang==512)
                    {
                        startwall = sector[sect].wallptr;
                        endwall = startwall+sector[sect].wallnum;
                        for (j=startwall;j<endwall;j++)
                        {
                            x = wall[j].nextsector;
                            if (x >= 0)
                                if (!(sector[x].ceilingstat&1))
                                {
                                    sector[sect].ceilingpicnum =
                                        sector[x].ceilingpicnum;
                                    sector[sect].ceilingshade =
                                        sector[x].ceilingshade;
                                    break; //Leave earily
                                }
                        }
                    }
                }

                break;

            case SE_17_WARP_ELEVATOR:

                T3(i) = sector[sect].floorz; //Stopping loc

                j = nextsectorneighborz(sect, sector[sect].floorz, -1, -1);
                if (j >= 0)
                {
                    T4(i) = sector[j].ceilingz;
                }
                else
                {
                    // use elevator sector's ceiling as heuristic
                    T4(i) = sector[sect].ceilingz;

                    OSD_Printf(OSD_ERROR "WARNING: SE17 sprite %d using own sector's ceilingz to "
                        "determine when to warp. Sector %d adjacent to a door?\n", i, sect);
                }

                j = nextsectorneighborz(sect, sector[sect].ceilingz, 1, 1);
                if (j >= 0)
                    T5(i) = sector[j].floorz;
                else
                {
                    // XXX: we should return to the menu for this and similar failures
                    Bsprintf(tempbuf, "SE 17 (warp elevator) setup failed: sprite %d at (%d, %d)",
                        i, TrackerCast(sprite[i].x), TrackerCast(sprite[i].y));
                    G_GameExit(tempbuf);
                }

                G_SetInterpolation(&sector[sect].floorz);
                G_SetInterpolation(&sector[sect].ceilingz);

                break;

            case SE_24_CONVEYOR:
                sp->yvel <<= 1;
            case SE_36_PROJ_SHOOTER:
                break;

            case SE_20_STRETCH_BRIDGE:
            {
                int q;

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                //find the two most clostest wall x's and y's
                q = 0x7fffffff;

                for (s=startwall;s<endwall;s++)
                {
                    x = wall[s].x;
                    y = wall[s].y;

                    d = FindDistance2D(sp->x-x,sp->y-y);
                    if (d < q)
                    {
                        q = d;
                        clostest = s;
                    }
                }

                T2(i) = clostest;

                q = 0x7fffffff;

                for (s=startwall;s<endwall;s++)
                {
                    x = wall[s].x;
                    y = wall[s].y;

                    d = FindDistance2D(sp->x-x,sp->y-y);
                    if (d < q && s != T2(i))
                    {
                        q = d;
                        clostest = s;
                    }
                }

                T3(i) = clostest;
            }

            break;

            case SE_3_RANDOM_LIGHTS_AFTER_SHOT_OUT:

                T4(i)=sector[sect].floorshade;

                sector[sect].floorshade = sp->shade;
                sector[sect].ceilingshade = sp->shade;

                sp->owner = sector[sect].ceilingpal<<8;
                sp->owner |= sector[sect].floorpal;

                //fix all the walls;

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                for (s=startwall;s<endwall;s++)
                {
                    if (!(wall[s].hitag&1))
                        wall[s].shade=sp->shade;
                    if ((wall[s].cstat&2) && wall[s].nextwall >= 0)
                        wall[wall[s].nextwall].shade = sp->shade;
                }
                break;

            case SE_31_FLOOR_RISE_FALL:
                T2(i) = sector[sect].floorz;
                //    T3(i) = sp->hitag;
                if (sp->ang != 1536) sector[sect].floorz = sp->z;

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                for (s=startwall;s<endwall;s++)
                    if (wall[s].hitag == 0) wall[s].hitag = 9999;

                G_SetInterpolation(&sector[sect].floorz);

                break;
            case SE_32_CEILING_RISE_FALL:
                T2(i) = sector[sect].ceilingz;
                T3(i) = sp->hitag;
                if (sp->ang != 1536) sector[sect].ceilingz = sp->z;

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                for (s=startwall;s<endwall;s++)
                    if (wall[s].hitag == 0) wall[s].hitag = 9999;

                G_SetInterpolation(&sector[sect].ceilingz);

                break;

            case SE_4_RANDOM_LIGHTS: //Flashing lights

                T3(i) = sector[sect].floorshade;

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                sp->owner = sector[sect].ceilingpal<<8;
                sp->owner |= sector[sect].floorpal;

                for (s=startwall;s<endwall;s++)
                    if (wall[s].shade > T4(i))
                        T4(i) = wall[s].shade;

                break;

            case SE_9_DOWN_OPEN_DOOR_LIGHTS:
                if (sector[sect].lotag &&
                        labs(sector[sect].ceilingz-sp->z) > 1024)
                    sector[sect].lotag |= 32768; //If its open
                fallthrough__;
            case SE_8_UP_OPEN_DOOR_LIGHTS:
                //First, get the ceiling-floor shade

                T1(i) = sector[sect].floorshade;
                T2(i) = sector[sect].ceilingshade;

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                for (s=startwall;s<endwall;s++)
                    if (wall[s].shade > T3(i))
                        T3(i) = wall[s].shade;

                T4(i) = 1; //Take Out;

                break;

            case SE_11_SWINGING_DOOR://Pivitor rotater
                if (sp->ang>1024) T4(i) = 2;
                else T4(i) = -2;
                fallthrough__;
            case SE_0_ROTATING_SECTOR:
            case SE_2_EARTHQUAKE://Earthquakemakers
            case SE_5://Boss Creature
            case SE_6_SUBWAY://Subway
            case SE_14_SUBWAY_CAR://Caboos
            case SE_15_SLIDING_DOOR://Subwaytype sliding door
            case SE_16_REACTOR://That rotating blocker reactor thing
            case SE_26://ESCELATOR
            case SE_30_TWO_WAY_TRAIN://No rotational subways

                if (sp->lotag == SE_0_ROTATING_SECTOR)
                {
                    if (sector[sect].lotag == ST_30_ROTATE_RISE_BRIDGE)
                    {
                        if (sp->pal) sprite[i].clipdist = 1;
                        else sprite[i].clipdist = 0;
                        T4(i) = sector[sect].floorz;
                        sector[sect].hitag = i;
                    }

                    for (j = MAXSPRITES-1;j>=0;j--)
                    {
                        if (sprite[j].statnum < MAXSTATUS)
                            if (sprite[j].picnum == SECTOREFFECTOR &&
                                    sprite[j].lotag == SE_1_PIVOT &&
                                    sprite[j].hitag == sp->hitag)
                            {
                                if (sp->ang == 512)
                                {
                                    sp->x = sprite[j].x;
                                    sp->y = sprite[j].y;
                                }
                                break;
                            }
                    }
                    if (j == -1)
                    {
//                        Bsprintf(tempbuf,"Found lonely Sector Effector (lotag 0) at (%d,%d)\n",sp->x,sp->y);
//                        G_GameExit(tempbuf);
                        OSD_Printf(OSD_ERROR "Found lonely Sector Effector (lotag 0) at (%d,%d)\n",TrackerCast(sp->x),TrackerCast(sp->y));
                        changespritestat(i, STAT_ACTOR);
                        if (apScriptGameEvent[EVENT_SPAWN])
                        {
                            int pl=A_FindPlayer(&sprite[i],&p);
                            X_OnEvent(EVENT_SPAWN,i, pl, p);
                        }
                        return i;
                    }
                    sp->owner = j;
                }

                startwall = sector[sect].wallptr;
                endwall = startwall+sector[sect].wallnum;

                T2(i) = tempwallptr;
                for (s=startwall;s<endwall;s++)
                {
                    g_origins[tempwallptr].x = wall[s].x-sp->x;
                    g_origins[tempwallptr].y = wall[s].y-sp->y;
                    tempwallptr++;
                    if (tempwallptr > 2047)
                    {
                        Bsprintf(tempbuf,"Too many moving sectors at (%d,%d).\n",TrackerCast(wall[s].x),TrackerCast(wall[s].y));
                        G_GameExit(tempbuf);
                    }
                }
                if (sp->lotag == SE_30_TWO_WAY_TRAIN || sp->lotag == SE_6_SUBWAY || sp->lotag == SE_14_SUBWAY_CAR || sp->lotag == SE_5)
                {

                    startwall = sector[sect].wallptr;
                    endwall = startwall+sector[sect].wallnum;

                    if (sector[sect].hitag == -1)
                        sp->extra = 0;
                    else sp->extra = 1;

                    sector[sect].hitag = i;

                    j = 0;

                    for (s=startwall;s<endwall;s++)
                    {
                        if (wall[ s ].nextsector >= 0 &&
                                sector[ wall[ s ].nextsector].hitag == 0 &&
                                sector[ wall[ s ].nextsector].lotag < 3)
                        {
                            s = wall[s].nextsector;
                            j = 1;
                            break;
                        }
                    }

                    if (j == 0)
                    {
                        Bsprintf(tempbuf,"Subway found no zero'd sectors with locators\nat (%d,%d).\n",TrackerCast(sp->x),TrackerCast(sp->y));
                        G_GameExit(tempbuf);
                    }

                    sp->owner = -1;
                    T1(i) = s;

                    if (sp->lotag != SE_30_TWO_WAY_TRAIN)
                        T4(i) = sp->hitag;
                }

                else if (sp->lotag == SE_16_REACTOR)
                    T4(i) = sector[sect].ceilingz;

                else if (sp->lotag == SE_26)
                {
                    T4(i) = sp->x;
                    T5(i) = sp->y;
                    if (sp->shade==sector[sect].floorshade) //UP
                        sp->zvel = -256;
                    else
                        sp->zvel = 256;

                    sp->shade = 0;
                }
                else if (sp->lotag == SE_2_EARTHQUAKE)
                {
                    T6(i) = sector[sp->sectnum].floorheinum;
                    sector[sp->sectnum].floorheinum = 0;
                }
            }

            switch (sp->lotag)
            {
            case SE_6_SUBWAY:
            case SE_14_SUBWAY_CAR:
                j = A_CallSound(sect,i);
                if (j == -1) j = SUBWAY;
                actor[i].lastvx = j;
                fallthrough__;
            case SE_30_TWO_WAY_TRAIN:
                if (numplayers > 1) break;
                fallthrough__;
            case SE_0_ROTATING_SECTOR:
            case SE_1_PIVOT:
            case SE_5:
            case SE_11_SWINGING_DOOR:
            case SE_15_SLIDING_DOOR:
            case SE_16_REACTOR:
            case SE_26:
                Sect_SetInterpolation(i);
                break;
            }

            changespritestat(i, STAT_EFFECTOR);
            break;

        case SEENINE__STATIC:
        case OOZFILTER__STATIC:

            sp->shade = -16;
            if (sp->xrepeat <= 8)
            {
                sp->cstat = (short)32768;
                sp->xrepeat=sp->yrepeat=0;
            }
            else sp->cstat = 1+256;
            sp->extra = g_impactDamage<<2;
            sp->owner = i;

            changespritestat(i, STAT_STANDABLE);
            break;

        case CRACK1__STATIC:
        case CRACK2__STATIC:
        case CRACK3__STATIC:
        case CRACK4__STATIC:
        case FIREEXT__STATIC:
            if (sp->picnum == FIREEXT)
            {
                sp->cstat = 257;
                sp->extra = g_impactDamage<<2;
            }
            else
            {
                sp->cstat |= (sp->cstat & 48) ? 1 : 17;
                sp->extra = 1;
            }

            if (ud.multimode < 2 && sp->pal != 0)
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
                break;
            }

            sp->pal = 0;
            sp->owner = i;
            changespritestat(i, STAT_STANDABLE);
            sp->xvel = 8;
            A_SetSprite(i,CLIPMASK0);
            break;

        case TOILET__STATIC:
        case STALL__STATIC:
            sp->lotag = 1;
            sp->cstat |= 257;
            sp->clipdist = 8;
            sp->owner = i;
            break;

        case CANWITHSOMETHING__STATIC:
        case CANWITHSOMETHING2__STATIC:
        case CANWITHSOMETHING3__STATIC:
        case CANWITHSOMETHING4__STATIC:
        case RUBBERCAN__STATIC:
            sp->extra = 0;
            fallthrough__;

        case EXPLODINGBARREL__STATIC:
        case HORSEONSIDE__STATIC:
        case FIREBARREL__STATIC:
        case NUKEBARREL__STATIC:
        case FIREVASE__STATIC:
        case NUKEBARRELDENTED__STATIC:
        case NUKEBARRELLEAKED__STATIC:
        case WOODENHORSE__STATIC:

            if (j >= 0)
                sp->xrepeat = sp->yrepeat = 32;
            sp->clipdist = 72;
            A_Fall(i);
            if (j >= 0)
                sp->owner = j;
            else sp->owner = i;
            fallthrough__;

        case EGG__STATIC:
            if (ud.monsters_off == 1 && sp->picnum == EGG)
            {
                sp->xrepeat = sp->yrepeat = 0;
                changespritestat(i, STAT_MISC);
            }
            else
            {
                if (sp->picnum == EGG)
                    sp->clipdist = 24;
                sp->cstat = 257|(krand()&4);
                changespritestat(i, STAT_ZOMBIEACTOR);
            }
            break;

        case TOILETWATER__STATIC:
            sp->shade = -16;
            changespritestat(i, STAT_STANDABLE);
            break;
        }

SPAWN_END:

    if (apScriptGameEvent[EVENT_SPAWN])
    {
        int pl=A_FindPlayer(&sprite[i],&p);
        X_OnEvent(EVENT_SPAWN,i, pl, p);
    }

    return i;
}

// OLDMP MERGE CHECKME
static int32_t G_MaybeTakeOnFloorPal(tspriteptr_t datspr, int32_t sect)
{
    int32_t dapal = sector[sect].floorpal;

    if (dapal && !g_noFloorPal[dapal]
        && !A_CheckSpriteFlags(datspr->owner, SFLAG_NOPAL))
    {
        datspr->pal = dapal;
        return 1;
    }

    return 0;
}

static inline void G_DoEventAnimSprites(int tspriteNum)
{
    int const tsprOwner = tsprite[tspriteNum].owner;

    if ((((unsigned)tsprOwner >= MAXSPRITES || (spriteext[tsprOwner].flags & SPREXT_TSPRACCESS) != SPREXT_TSPRACCESS))
        && tsprite[tspriteNum].statnum != TSPR_TEMP)
        return;

    spriteext[tsprOwner].tspr = &tsprite[tspriteNum];
    X_OnEvent(EVENT_ANIMATESPRITES, tsprOwner, screenpeek, -1);
    spriteext[tsprOwner].tspr = NULL;
}

#ifdef _MSC_VER
// Visual C thought this was a bit too hard to optimise so we'd better
// tell it not to try... such a pussy it is.
//#pragma auto_inline(off)
#pragma optimize("g",off)
#endif
void G_DoSpriteAnimations(int32_t x, int32_t y, int32_t z, int32_t a, int32_t smoothratio)
{
    UNREFERENCED_PARAMETER(z);

    int i, j, k, p, sect;
    intptr_t l, t1,t3,t4;
    spritetype* s;
    tspritetype *t;
    int switchpic;

    if (!spritesortcnt) return;

    ror_sprite = -1;

    for (j = spritesortcnt - 1; j >= 0; j--) //Between drawrooms() and renderDrawMasks()
    {
        //is the perfect time to animate sprites
        t = &tsprite[j];
        i = t->owner;
        s = &sprite[i];

        switch (DynamicTileMap[s->picnum])
        {
        case SECTOREFFECTOR__STATIC:
            if (s->lotag == SE_40_ROR_UPPER || s->lotag == SE_41_ROR_LOWER)
            {
                t->cstat = 32768;

                if (ror_sprite == -1) ror_sprite = i;
            }

            if (t->lotag == SE_27_DEMO_CAM && ud.recstat == 1)
            {
                t->picnum = 11 + (((int32_t)totalclock >> 3) & 1);
                t->cstat |= 128;
            }
            else
                t->xrepeat = t->yrepeat = 0;
            break;
        }
    }

    for (j=spritesortcnt-1;j>=0; j--)
    {
        t = &tsprite[j];
        i = t->owner;
        s = &sprite[t->owner];

        //greenslime can't be handled through the dynamictostatic system due to addition on constant
        if ((t->picnum >= GREENSLIME)&&(t->picnum <= GREENSLIME+7))
        {
            if ((numplayers > 1) && (screenpeek == myconnectindex) && (g_player[myconnectindex].ps->somethingonplayer == i))
            {
                t->statnum = TSPR_TEMP;
            }
        }
        else switch (DynamicTileMap[t->picnum])
            {
            case BLOODPOOL__STATIC:
            case PUKE__STATIC:
            case FOOTPRINTS__STATIC:
            case FOOTPRINTS2__STATIC:
            case FOOTPRINTS3__STATIC:
            case FOOTPRINTS4__STATIC:
                if (t->shade == 127) continue;
                break;
            case RESPAWNMARKERRED__STATIC:
            case RESPAWNMARKERYELLOW__STATIC:
            case RESPAWNMARKERGREEN__STATIC:
                if (ud.marker == 0)
                    t->xrepeat = t->yrepeat = 0;
                continue;
            case CHAIR3__STATIC:
#if defined(USE_OPENGL)
                if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(t->picnum,t->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
                {
                    t->cstat &= ~4;
                    break;
                }
#endif
                k = (((t->ang+3072+128-a)&2047)>>8)&7;
                if (k>4)
                {
                    k = 8-k;
                    t->cstat |= 4;
                }
                else t->cstat &= ~4;
                t->picnum = s->picnum+k;
                break;
            case BLOODSPLAT1__STATIC:
            case BLOODSPLAT2__STATIC:
            case BLOODSPLAT3__STATIC:
            case BLOODSPLAT4__STATIC:
                if (ud.lockout) t->xrepeat = t->yrepeat = 0;
                else if (t->pal == 6)
                {
                    t->shade = -127;
                    continue;
                }
                fallthrough__;

            case BULLETHOLE__STATIC:
            case CRACK1__STATIC:
            case CRACK2__STATIC:
            case CRACK3__STATIC:
            case CRACK4__STATIC:
                t->shade = 16;
                continue;
            case NEON1__STATIC:
            case NEON2__STATIC:
            case NEON3__STATIC:
            case NEON4__STATIC:
            case NEON5__STATIC:
            case NEON6__STATIC:
                continue;

            default:
                if (((t->cstat&16)) || (A_CheckEnemySprite(t) && t->extra > 0) || t->statnum == 10)
                    continue;
            }

        if (A_CheckSpriteFlags(t->owner,SFLAG_NOSHADE))
            l = sprite[t->owner].shade;
        else
        {
            if (sector[t->sectnum].ceilingstat&1)
                l = sector[t->sectnum].ceilingshade;
            else
                l = sector[t->sectnum].floorshade;
            if (l < -127) l = -127;
            if (l > 128) l =  127;
        }
        t->shade = l;
    }

    for (j=spritesortcnt-1;j>=0; j--) //Between drawrooms() and renderDrawMasks()
    {
        //is the perfect time to animate sprites
        t = &tsprite[j];
        i = t->owner;
        s = &sprite[i];

        switch (DynamicTileMap[s->picnum])
        {
        case NATURALLIGHTNING__STATIC:
            t->shade = -127;
            break;
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
        case MAN__STATIC:
        case MAN2__STATIC:
        case WOMAN__STATIC:
        case NAKED1__STATIC:
        case PODFEM1__STATIC:
        case FEMMAG1__STATIC:
        case FEMMAG2__STATIC:
        case FEMPIC1__STATIC:
        case FEMPIC2__STATIC:
        case FEMPIC3__STATIC:
        case FEMPIC4__STATIC:
        case FEMPIC5__STATIC:
        case FEMPIC6__STATIC:
        case FEMPIC7__STATIC:
        case BLOODYPOLE__STATIC:
        case FEM6PAD__STATIC:
        case STATUE__STATIC:
        case STATUEFLASH__STATIC:
        case OOZ__STATIC:
        case OOZ2__STATIC:
        case WALLBLOOD1__STATIC:
        case WALLBLOOD2__STATIC:
        case WALLBLOOD3__STATIC:
        case WALLBLOOD4__STATIC:
        case WALLBLOOD5__STATIC:
        case WALLBLOOD7__STATIC:
        case WALLBLOOD8__STATIC:
        case SUSHIPLATE1__STATIC:
        case SUSHIPLATE2__STATIC:
        case SUSHIPLATE3__STATIC:
        case SUSHIPLATE4__STATIC:
        case FETUS__STATIC:
        case FETUSJIB__STATIC:
        case FETUSBROKE__STATIC:
        case HOTMEAT__STATIC:
        case FOODOBJECT16__STATIC:
        case DOLPHIN1__STATIC:
        case DOLPHIN2__STATIC:
        case TOUGHGAL__STATIC:
        case TAMPON__STATIC:
        case XXXSTACY__STATIC:
            if (ud.lockout)
            {
                t->xrepeat = t->yrepeat = 0;
                //continue;
            }
        }
        switch (s->picnum)
        {

        case 4946:
        case 4947:
        case 693:
        case 2254:
        case 4560:
        case 4561:
        case 4562:
        case 4498:
        case 4957:
            if (ud.lockout)
            {
                t->xrepeat = t->yrepeat = 0;
                //continue;
            }
        }

        if (t->statnum == TSPR_TEMP) continue;
        if (s->statnum != 1 && s->picnum == APLAYER && g_player[s->yvel].ps->newowner == -1 && s->owner >= 0)
        {
            t->x -= mulscale16(65536-smoothratio,g_player[s->yvel].ps->pos.x - g_player[s->yvel].ps->opos.x);
            t->y -= mulscale16(65536-smoothratio,g_player[s->yvel].ps->pos.y - g_player[s->yvel].ps->opos.y);
            t->z -= mulscale16(65536-smoothratio,g_player[s->yvel].ps->pos.z - g_player[s->yvel].ps->opos.z);
            
            // Dead or frozen?
            if (g_player[s->yvel].ps->dead_flag || s->extra <= 0)
                t->z += (20<<8); // Matches corpse height of vanilla Duke3D.
        }
        else if (s->picnum != CRANEPOLE)
        {
            t->x -= mulscale16(65536-smoothratio,s->x-actor[i].bpos.x);
            t->y -= mulscale16(65536-smoothratio,s->y-actor[i].bpos.y);
            t->z -= mulscale16(65536-smoothratio,s->z-actor[i].bpos.z);
        }

        sect = s->sectnum;
        t1 = T2(i);
        t3 = T4(i);
        t4 = T5(i);
        switchpic = s->picnum;
        //some special cases because dynamictostatic system can't handle addition to constants
        if ((s->picnum >= SCRAP6)&&(s->picnum<=SCRAP6+7))
            switchpic = SCRAP5;
        else if ((s->picnum==MONEY+1)||(s->picnum==MAIL+1)||(s->picnum==PAPER+1))
            switchpic--;

        switch (DynamicTileMap[switchpic])
        {
        case DUKELYINGDEAD__STATIC:
            t->z += (24<<8);
            break;
        case BLOODPOOL__STATIC:
        case FOOTPRINTS__STATIC:
        case FOOTPRINTS2__STATIC:
        case FOOTPRINTS3__STATIC:
        case FOOTPRINTS4__STATIC:
            if (t->pal == 6)
                t->shade = -127;
            fallthrough__;
        case PUKE__STATIC:
        case MONEY__STATIC:
            //case MONEY+1__STATIC:
        case MAIL__STATIC:
            //case MAIL+1__STATIC:
        case PAPER__STATIC:
            //case PAPER+1__STATIC:
            if (ud.lockout && s->pal == 2)
            {
                t->xrepeat = t->yrepeat = 0;
                //continue;
            }
            break;
        case TRIPBOMB__STATIC:
            continue;
        case FORCESPHERE__STATIC:
            if (t->statnum == 5)
            {
                short sqa,sqb;

                sqa =
                    getangle(
                        sprite[s->owner].x-g_player[screenpeek].ps->pos.x,
                        sprite[s->owner].y-g_player[screenpeek].ps->pos.y);
                sqb =
                    getangle(
                        sprite[s->owner].x-t->x,
                        sprite[s->owner].y-t->y);

                if (klabs(G_GetAngleDelta(sqa,sqb)) > 512)
                    if (ldist(&sprite[s->owner],t) < ldist(&sprite[g_player[screenpeek].ps->i],&sprite[s->owner]))
                        t->xrepeat = t->yrepeat = 0;
            }
            continue;
        case BURNING__STATIC:
        case BURNING2__STATIC:
            if (sprite[s->owner].statnum == 10)
            {
                if (display_mirror == 0 && sprite[s->owner].yvel == screenpeek && g_player[sprite[s->owner].yvel].ps->over_shoulder_on == 0)
                    t->xrepeat = 0;
                else
                {
                    t->ang = getangle(x-t->x,y-t->y);
                    t->x = sprite[s->owner].x;
                    t->y = sprite[s->owner].y;
                    t->x += sintable[(t->ang+512)&2047]>>10;
                    t->y += sintable[t->ang&2047]>>10;
                }
            }
            break;

        case ATOMICHEALTH__STATIC:
            t->z -= (4<<8);
            break;
        case CRYSTALAMMO__STATIC:
            t->shade = (sintable[((int32_t)totalclock<<4)&2047]>>10);
            continue;
        case VIEWSCREEN__STATIC:
        case VIEWSCREEN2__STATIC:
            if (camsprite >= 0 && actor[OW(i)].temp_data[0] == 1)
            {
                t->picnum = STATIC;
                t->cstat |= (rand()&12);
                t->xrepeat += 8;
                t->yrepeat += 8;
            }
            else if (camsprite >= 0 && waloff[TILE_VIEWSCR] && walock[TILE_VIEWSCR] > 200)
            {
                t->picnum = TILE_VIEWSCR;
            }
            break;

        case SHRINKSPARK__STATIC:
            t->picnum = SHRINKSPARK+(((int32_t)totalclock>>4)&3);
            break;
        case GROWSPARK__STATIC:
            t->picnum = GROWSPARK+(((int32_t)totalclock>>4)&3);
            break;
        case RPG__STATIC:

#if defined(USE_OPENGL)
            if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(t->picnum,t->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
            {
                int v=getangle(t->xvel,t->zvel>>4);
                if (v>1023)v-=2048;
                spriteext[i].mdpitch=v;

                t->cstat &= ~4;
                break;
            }
#endif
            k = getangle(s->x-x,s->y-y);
            k = (((s->ang+3072+128-k)&2047)/170);
            if (k > 6)
            {
                k = 12-k;
                t->cstat |= 4;
            }
            else t->cstat &= ~4;
            t->picnum = RPG+k;
            break;

        case RECON__STATIC:

#if defined(USE_OPENGL)
            if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(t->picnum,t->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
            {
                t->cstat &= ~4;
                break;
            }
#endif
            k = getangle(s->x-x,s->y-y);
            k = (((s->ang+3072+128-k)&2047)/170);

            if (k>6)
            {
                k = 12-k;
                t->cstat |= 4;
            }
            else t->cstat &= ~4;

            if (klabs(t3) > 64) k += 7;
            t->picnum = RECON+k;

            break;

        case APLAYER__STATIC:

            p = s->yvel;

            if (t->pal == 1) // Frozen?
                t->z -= (18<<8);

            if (g_player[p].ps->over_shoulder_on > 0 && g_player[p].ps->newowner < 0 && s->owner != -1)
            {
                if(p == screenpeek) // Let's only make the player we're peeking transparent.
                    t->cstat |= 2;

                if (screenpeek == myconnectindex && numplayers >= 2)
                {
                    t->x = predictedPlayer.opos.x+mulscale16((int)(predictedPlayer.pos.x - predictedPlayer.opos.x),smoothratio);
                    t->y = predictedPlayer.opos.y+mulscale16((int)(predictedPlayer.pos.y - predictedPlayer.opos.y),smoothratio);
                    t->z = predictedPlayer.opos.z+mulscale16((int)(predictedPlayer.pos.z - predictedPlayer.opos.z),smoothratio)+(40<<8);
                    t->ang = fix16_to_int(predictedPlayer.oq16ang+mulscale16(((predictedPlayer.q16ang+1024 - predictedPlayer.oq16ang)&2047)-1024,smoothratio));
                    t->sectnum = predictedPlayer.cursectnum;
                }
            }

            if (ud.multimode > 1 && (display_mirror || screenpeek != p || s->owner == -1))
            {
                if (ud.showweapons && sprite[g_player[p].ps->i].extra > 0 && g_player[p].ps->curr_weapon > 0)
                {
                    memcpy((spritetype *)&tsprite[spritesortcnt],(spritetype *)t,sizeof(spritetype));

                    tsprite[spritesortcnt].statnum = TSPR_TEMP;
                    tsprite[spritesortcnt].shade = t->shade;
                    tsprite[spritesortcnt].cstat = 0;
                    tsprite[spritesortcnt].pal = 0;
                    tsprite[spritesortcnt].picnum = (g_player[p].ps->curr_weapon==GROW_WEAPON?GROWSPRITEICON:WeaponPickupSprites[g_player[p].ps->curr_weapon]);
                    tsprite[spritesortcnt].z -= (51<<8);

                    if (tsprite[spritesortcnt].picnum == HEAVYHBOMB)
                    {
                        tsprite[spritesortcnt].xrepeat = 10;
                        tsprite[spritesortcnt].yrepeat = 10;
                    }
                    else
                    {
                        tsprite[spritesortcnt].xrepeat = 16;
                        tsprite[spritesortcnt].yrepeat = 16;
                    }
                    spritesortcnt++;
                }

                if (g_player[p].inputBits->extbits & (EXTBIT_CHAT) && !ud.pause_on)
                {
                    memcpy((spritetype *)&tsprite[spritesortcnt],(spritetype *)t,sizeof(spritetype));

                    tsprite[spritesortcnt].statnum = TSPR_TEMP;

                    tsprite[spritesortcnt].yrepeat = (t->yrepeat>>3);
                    if (t->yrepeat < 4) t->yrepeat = 4;

                    tsprite[spritesortcnt].cstat = 0;

                    tsprite[spritesortcnt].picnum = RESPAWNMARKERGREEN;

                    // Code for spinning voxel logos
                    tsprite[spritesortcnt].ang -= ((int32_t)totalclock << 6);

                    tsprite[spritesortcnt].z -= (60<<8);
                    tsprite[spritesortcnt].xrepeat = 32;
                    tsprite[spritesortcnt].yrepeat = 32;
                    tsprite[spritesortcnt].pal = 20;
                    spritesortcnt++;
                }
            }

            if (s->owner == -1)
            {

#if defined(USE_OPENGL)
                if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(s->picnum,t->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
                {
                    k = 0;
                    t->cstat &= ~4;
                }
                else
#endif
                {
                    k = (((s->ang+3072+128-a)&2047)>>8)&7;
                    if (k>4)
                    {
                        k = 8-k;
                        t->cstat |= 4;
                    }
                    else t->cstat &= ~4;
                }

                if (sector[t->sectnum].lotag == ST_2_UNDERWATER) k += 1795-1405;
                else if ((actor[i].floorz-s->z) > (64<<8)) k += 60;

                t->picnum += k;
                t->pal = g_player[p].ps->palookup;

                goto PALONLY;
            }

            // CLAMP PLAYER SPRITE Y TO FLOOR
            if (g_player[p].ps->on_crane == -1 && ((sector[s->sectnum].lotag&0x7ff) != 1) && !HEAD_ABOVE_TROR_WATER(p))
            {
                l = s->z-actor[g_player[p].ps->i].floorz+(3<<8);
                if (l > 1024 && s->yrepeat > 32 && s->extra > 0)
                    s->yoffset = (signed char)(l/(s->yrepeat<<2));
                else s->yoffset=0;
            }

            if (g_player[p].ps->newowner > -1)
            {
                t4 = *(actorscrptr[APLAYER]+1);
                t3 = 0;
                t1 = *(actorscrptr[APLAYER]+2);
            }

            if (ud.camerasprite == -1 && g_player[p].ps->newowner == -1)
                if (s->owner >= 0 && display_mirror == 0 && g_player[p].ps->over_shoulder_on == 0)
                    if (ud.multimode < 2 || (ud.multimode > 1 && p == screenpeek))
                    {
                        t->owner = -1;
                        t->xrepeat = t->yrepeat = 0;
                        continue;
                    }

PALONLY:

            G_MaybeTakeOnFloorPal(t, sect);

            if (s->owner == -1) continue;

            if (t->z > actor[i].floorz && t->xrepeat < 32)
                t->z = actor[i].floorz;

            break;

        case JIBS1__STATIC:
        case JIBS2__STATIC:
        case JIBS3__STATIC:
        case JIBS4__STATIC:
        case JIBS5__STATIC:
        case JIBS6__STATIC:
        case HEADJIB1__STATIC:
        case LEGJIB1__STATIC:
        case ARMJIB1__STATIC:
        case LIZMANHEAD1__STATIC:
        case LIZMANARM1__STATIC:
        case LIZMANLEG1__STATIC:
        case DUKELEG__STATIC:
        case DUKEGUN__STATIC:
        case DUKETORSO__STATIC:
            if (ud.lockout)
            {
                t->xrepeat = t->yrepeat = 0;
                //continue;
            }
            if (t->pal == 6) t->shade = -120;
            fallthrough__;
        case SCRAP1__STATIC:
        case SCRAP2__STATIC:
        case SCRAP3__STATIC:
        case SCRAP4__STATIC:
        case SCRAP5__STATIC:
            if (actor[i].picnum == BLIMP && t->picnum == SCRAP1 && s->yvel >= 0)
                t->picnum = s->yvel;
            else t->picnum += T1(i);
            t->shade -= 6;

            G_MaybeTakeOnFloorPal(t, sect);
            break;

        case WATERBUBBLE__STATIC:
            if (sector[t->sectnum].floorpicnum == FLOORSLIME)
            {
                t->pal = 7;
                break;
            }
            fallthrough__;
        default:
            G_MaybeTakeOnFloorPal(t, sect);
            break;
        }

        if (actorscrptr[s->picnum])
        {
            if ((intptr_t *)t4 >= &script[0] && (intptr_t *)t4 <= (&script[0] + g_scriptSize))
            {
                l = *(((intptr_t *)t4)+2); //For TerminX: was *(int *)(t4+8)

#if defined(USE_OPENGL)
                if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(s->picnum,t->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
                {
                    k = 0;
                    t->cstat &= ~4;
                }
                else
#endif
                    switch (l)
                    {
                    case 2:
                        k = (((s->ang+3072+128-a)&2047)>>8)&1;
                        break;

                    case 3:
                    case 4:
                        k = (((s->ang+3072+128-a)&2047)>>7)&7;
                        if (k > 3)
                        {
                            t->cstat |= 4;
                            k = 7-k;
                        }
                        else t->cstat &= ~4;
                        break;

                    case 5:
                        k = getangle(s->x-x,s->y-y);
                        k = (((s->ang+3072+128-k)&2047)>>8)&7;
                        if (k>4)
                        {
                            k = 8-k;
                            t->cstat |= 4;
                        }
                        else t->cstat &= ~4;
                        break;
                    case 7:
                        k = getangle(s->x-x,s->y-y);
                        k = (((s->ang+3072+128-k)&2047)/170);
                        if (k>6)
                        {
                            k = 12-k;
                            t->cstat |= 4;
                        }
                        else t->cstat &= ~4;
                        break;
                    case 8:
                        k = (((s->ang+3072+128-a)&2047)>>8)&7;
                        t->cstat &= ~4;
                        break;
                    default:
                        k = 0;
                        break;
                    }

                t->picnum += k + (*(intptr_t *)t4) + l * t3;

                if (l > 0) while (tilesiz[t->picnum].x == 0 && t->picnum > 0)
                        t->picnum -= l;       //Hack, for actors

                if (actor[i].dispicnum >= 0)
                    actor[i].dispicnum = t->picnum;
            }
            else if (display_mirror == 1)
                t->cstat |= 4;
        }

        if (g_player[screenpeek].ps->heat_amount > 0 && g_player[screenpeek].ps->heat_on && (A_CheckEnemySprite(s) || A_CheckSpriteFlags(t->owner,SFLAG_NVG) || s->picnum == APLAYER || s->statnum == 13))
        {
            t->pal = 6;
            t->shade = 0;
        }

        if (s->statnum == 13 || A_CheckEnemySprite(s) || A_CheckSpriteFlags(t->owner,SFLAG_SHADOW) || (s->picnum == APLAYER && s->owner >= 0))
            if (t->statnum != TSPR_TEMP && s->picnum != EXPLOSION2 && s->picnum != HANGLIGHT && s->picnum != DOMELITE)
                if (s->picnum != HOTMEAT)
                {
                    if (actor[i].dispicnum < 0)
                    {
                        actor[i].dispicnum++;
                        continue;
                    }
                    else if (ud.shadows && spritesortcnt < (MAXSPRITESONSCREEN-2))
                    {
                        int daz,xrep,yrep;

                        if ((sector[sect].lotag&0xff) > 2 || s->statnum == STAT_PROJECTILE || s->statnum == STAT_MISC || s->picnum == DRONE || s->picnum == COMMANDER)
                            daz = sector[sect].floorz;
                        else
                            daz = actor[i].floorz;

                        if ((s->z-daz) < (8<<8))
                            if (g_player[screenpeek].ps->pos.z < daz)
                            {
                                memcpy((spritetype *)&tsprite[spritesortcnt],(spritetype *)t,sizeof(spritetype));

                                tsprite[spritesortcnt].statnum = TSPR_TEMP;

                                tsprite[spritesortcnt].yrepeat = (t->yrepeat>>3);
                                if (t->yrepeat < 4) t->yrepeat = 4;

                                tsprite[spritesortcnt].shade = 127;
                                tsprite[spritesortcnt].cstat |= 2;

                                tsprite[spritesortcnt].z = daz;
                                xrep = tsprite[spritesortcnt].xrepeat;// - (klabs(daz-t->z)>>11);
                                tsprite[spritesortcnt].xrepeat = xrep;
                                tsprite[spritesortcnt].pal = 4;

                                yrep = tsprite[spritesortcnt].yrepeat;// - (klabs(daz-t->z)>>11);
                                tsprite[spritesortcnt].yrepeat = yrep;

#if defined(USE_OPENGL)
                                if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(t->picnum,t->pal) >= 0)
                                {
                                    tsprite[spritesortcnt].yrepeat = 0;
                                    // 512:trans reverse
                                    //1024:tell MD2SPRITE.C to use Z-buffer hacks to hide overdraw issues
                                    tsprite[spritesortcnt].cstat |= (512+1024);
                                }
                                else if (videoGetRenderMode() >= 3)
                                {
                                    int ii;

                                    ii = getangle(tsprite[spritesortcnt].x-g_player[screenpeek].ps->pos.x,
                                                  tsprite[spritesortcnt].y-g_player[screenpeek].ps->pos.y);

                                    tsprite[spritesortcnt].x += sintable[(ii+2560)&2047]>>9;
                                    tsprite[spritesortcnt].y += sintable[(ii+2048)&2047]>>9;
                                }
#endif
                                spritesortcnt++;
                            }
                    }
                }

        switch (DynamicTileMap[s->picnum])
        {
        case LASERLINE__STATIC:
            if (sector[t->sectnum].lotag == ST_2_UNDERWATER) t->pal = 8;
            t->z = sprite[s->owner].z-(3<<8);
            if (g_tripbombLaserMode == 2 && g_player[screenpeek].ps->heat_on == 0)
                t->yrepeat = 0;
            fallthrough__;
        case EXPLOSION2__STATIC:
        case EXPLOSION2BOT__STATIC:
        case FREEZEBLAST__STATIC:
        case ATOMICHEALTH__STATIC:
        case FIRELASER__STATIC:
        case SHRINKSPARK__STATIC:
        case GROWSPARK__STATIC:
        case CHAINGUN__STATIC:
        case SHRINKEREXPLOSION__STATIC:
        case RPG__STATIC:
        case FLOORFLAME__STATIC:
            if (t->picnum == EXPLOSION2)
            {
                g_player[screenpeek].display.visibility = -127;
                //g_restorePalette = 1;   // JBF 20040101: why?
            }
            t->shade = -127;
            break;
        case FIRE__STATIC:
        case FIRE2__STATIC:
        case BURNING__STATIC:
        case BURNING2__STATIC:
            if (sprite[s->owner].picnum != TREE1 && sprite[s->owner].picnum != TREE2)
                t->z = sector[t->sectnum].floorz;
            t->shade = -127;
            break;
        case COOLEXPLOSION1__STATIC:
            t->shade = -127;
            t->picnum += (s->shade>>1);
            break;
        case PLAYERONWATER__STATIC:
#if defined(USE_OPENGL)
            if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(s->picnum,s->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
            {
                k = 0;
                t->cstat &= ~4;
            }
            else
#endif
            {
                k = (((t->ang+3072+128-a)&2047)>>8)&7;
                if (k>4)
                {
                    k = 8-k;
                    t->cstat |= 4;
                }
                else t->cstat &= ~4;
            }

            t->picnum = s->picnum+k+((T1(i)<4)*5);
            t->shade = sprite[s->owner].shade;

            break;

        case WATERSPLASH2__STATIC:
            t->picnum = WATERSPLASH2+t1;
            break;
        case SHELL__STATIC:
            t->picnum = s->picnum+(T1(i)&1);
            fallthrough__;
        case SHOTGUNSHELL__STATIC:
            t->cstat |= 12;
            if (T1(i) > 1) t->cstat &= ~4;
            if (T1(i) > 2) t->cstat &= ~12;
            break;
        case FRAMEEFFECT1_13__STATIC:
            if (PLUTOPAK) break;
            fallthrough__;
        case FRAMEEFFECT1__STATIC:
            if (s->owner >= 0 && sprite[s->owner].statnum < MAXSTATUS)
            {
                if (sprite[s->owner].picnum == APLAYER)
                    if (ud.camerasprite == -1)
                        if (screenpeek == sprite[s->owner].yvel && display_mirror == 0)
                        {
                            t->owner = -1;
                            break;
                        }
                if ((sprite[s->owner].cstat&32768) == 0)
                {
                    if (!actor[s->owner].dispicnum)
                        t->picnum = actor[i].temp_data[1];
                    else t->picnum = actor[s->owner].dispicnum;
                    
                    if (!G_MaybeTakeOnFloorPal(t, sect))
                        t->pal = sprite[s->owner].pal;

                    t->shade = sprite[s->owner].shade;
                    t->ang = sprite[s->owner].ang;
                    t->cstat = 2|sprite[s->owner].cstat;
                }
            }
            break;

        case CAMERA1__STATIC:
        case RAT__STATIC:

#if defined(USE_OPENGL)
            if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(s->picnum,s->pal) >= 0 && !(spriteext[i].flags&SPREXT_NOTMD))
            {
                t->cstat &= ~4;
                break;
            }
#endif

            k = (((t->ang+3072+128-a)&2047)>>8)&7;
            if (k>4)
            {
                k = 8-k;
                t->cstat |= 4;
            }
            else t->cstat &= ~4;
            t->picnum = s->picnum+k;
            break;
        }

        actor[i].dispicnum = t->picnum;
        if (sector[t->sectnum].floorpicnum == MIRROR)
            t->xrepeat = t->yrepeat = 0;
    }

    if (apScriptGameEvent[EVENT_ANIMATESPRITES])
    {
        for (j = spritesortcnt - 1; j >= 0; j--)
            G_DoEventAnimSprites(j);
    }
}
#ifdef _MSC_VER
//#pragma auto_inline()
#pragma optimize("",on)
#endif

char CheatStrings[][MAXCHEATLEN] =
{
    "cornholio",    // 0
    "stuff",        // 1
    "scotty###",    // 2
    "coords",       // 3
    "view",         // 4
    "time",         // 5
    "unlock",       // 6
    "cashman",      // 7
    "items",        // 8
    "rate",         // 9
    "skill#",       // 10
    "beta",         // 11
    "hyper",        // 12
    "monsters",     // 13
    "<RESERVED>",   // 14
    "<RESERVED>",   // 15
    "todd",         // 16
    "showmap",      // 17
    "kroz",         // 18
    "allen",        // 19
    "clip",         // 20
    "weapons",      // 21
    "inventory",    // 22
    "keys",         // 23
    "debug",        // 24
    "<RESERVED>",   // 25
    "sfm",          // 26
};

enum cheats
{
    CHEAT_CORNHOLIO,
    CHEAT_STUFF,
    CHEAT_SCOTTY,
    CHEAT_COORDS,
    CHEAT_VIEW,
    CHEAT_TIME,
    CHEAT_UNLOCK,
    CHEAT_CASHMAN,
    CHEAT_ITEMS,
    CHEAT_RATE,
    CHEAT_SKILL,
    CHEAT_BETA,
    CHEAT_HYPER,
    CHEAT_MONSTERS,
    CHEAT_RESERVED,
    CHEAT_RESERVED2,
    CHEAT_TODD,
    CHEAT_SHOWMAP,
    CHEAT_KROZ,
    CHEAT_ALLEN,
    CHEAT_CLIP,
    CHEAT_WEAPONS,
    CHEAT_INVENTORY,
    CHEAT_KEYS,
    CHEAT_DEBUG,
    CHEAT_RESERVED3,
    CHEAT_SCREAMFORME,
};

void G_CheatGetInv(void)
{
    Gv_SetVar(g_iReturnVarID, 400, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETSTEROIDS, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->steroids_amount =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, 1200, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETHEAT, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->heat_amount     =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, 200, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETBOOT, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->boot_amount          =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, 100, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETSHIELD, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->shield_amount =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, 6400, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETSCUBA, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->scuba_amount =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, 2400, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETHOLODUKE, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->holoduke_amount =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, 1600, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETJETPACK, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->jetpack_amount =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }

    Gv_SetVar(g_iReturnVarID, g_player[myconnectindex].ps->max_player_health, g_player[myconnectindex].ps->i, myconnectindex);
    X_OnEvent(EVENT_CHEATGETFIRSTAID, g_player[myconnectindex].ps->i, myconnectindex, -1);
    if (Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex) >=0)
    {
        g_player[myconnectindex].ps->firstaid_amount =
            Gv_GetVar(g_iReturnVarID, g_player[myconnectindex].ps->i, myconnectindex);
    }
}

signed char cheatbuf[MAXCHEATLEN],cheatbuflen;

static void G_DoCheats(void)
{
    short ch, i, j, k=0, weapon;
    static int z=0;
    char consolecheat = 0;  // JBF 20030914

    if (osdcmd_cheatsinfo_stat.cheatnum != -1)
    {
        // JBF 20030914
        k = osdcmd_cheatsinfo_stat.cheatnum;
        osdcmd_cheatsinfo_stat.cheatnum = -1;
        consolecheat = 1;
    }

    if ((g_player[myconnectindex].ps->gm&MODE_TYPE) || (g_player[myconnectindex].ps->gm&MODE_MENU))
        return;

    if (VOLUMEONE && !z)
    {
        Bstrcpy(CheatStrings[2],"scotty##");
        Bstrcpy(CheatStrings[6],"<RESERVED>");
        z=1;
    }

    if (consolecheat && numplayers < 2 && ud.recstat == 0)
        goto FOUNDCHEAT;

    if (g_player[myconnectindex].ps->cheat_phase == 1)
    {
        while (KB_KeyWaiting())
        {
            ch = Btolower(KB_GetCh());

            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')))
            {
                g_player[myconnectindex].ps->cheat_phase = 0;
                //                             P_DoQuote(46,g_player[myconnectindex].ps);
                return;
            }

            cheatbuf[cheatbuflen++] = ch;
            cheatbuf[cheatbuflen] = 0;
            //            KB_ClearKeysDown();

            if (cheatbuflen > MAXCHEATLEN)
            {
                g_player[myconnectindex].ps->cheat_phase = 0;
                return;
            }

            for (k=0;k < NUMCHEATCODES;k++)
            {
                for (j = 0;j<cheatbuflen;j++)
                {
                    if (cheatbuf[j] == CheatStrings[k][j] || (CheatStrings[k][j] == '#' && ch >= '0' && ch <= '9'))
                    {
                        if (CheatStrings[k][j+1] == 0) goto FOUNDCHEAT;
                        if (j == cheatbuflen-1) return;
                    }
                    else break;
                }
            }

            g_player[myconnectindex].ps->cheat_phase = 0;
            return;

FOUNDCHEAT:
            {
                switch (k)
                {
                case CHEAT_WEAPONS:

                    j = 0;

                    if (VOLUMEONE)
                        j = 6;

                    for (weapon = PISTOL_WEAPON;weapon < MAX_WEAPONS-j;weapon++)
                    {
                        P_AddAmmo(weapon, g_player[myconnectindex].ps, g_player[myconnectindex].ps->max_ammo_amount[weapon]);
                        g_player[myconnectindex].ps->gotweapon[weapon]  = 1;
                    }

                    KB_FlushKeyboardQueue();
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    P_DoQuote(119,g_player[myconnectindex].ps);
                    return;

                case CHEAT_INVENTORY:
                    KB_FlushKeyboardQueue();
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    G_CheatGetInv();
                    P_DoQuote(120,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    return;

                case CHEAT_KEYS:
                    ud.got_access =  7;
                    KB_FlushKeyboardQueue();
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    P_DoQuote(121,g_player[myconnectindex].ps);
                    return;

                case CHEAT_DEBUG:
                    debug_on = 1-debug_on;
                    KB_FlushKeyboardQueue();
                    g_player[myconnectindex].ps->cheat_phase = 0;

                    G_DumpDebugInfo();
                    Bsprintf(tempbuf,"GAMEVARS DUMPED TO DEBUG.CON");
                    G_AddUserQuote(tempbuf);
                    Bsprintf(tempbuf,"MAP DUMPED TO DEBUG.MAP");
                    G_AddUserQuote(tempbuf);
                    break;

                case CHEAT_CLIP:
                    ud.clipping = 1-ud.clipping;
                    KB_FlushKeyboardQueue();
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    P_DoQuote(112+ud.clipping,g_player[myconnectindex].ps);
                    return;

                case CHEAT_RESERVED2:
                    g_player[myconnectindex].ps->gm = MODE_EOL;
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_ALLEN:
                    P_DoQuote(79,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_ClearKeyDown(sc_N);
                    return;

                case CHEAT_CORNHOLIO:
                case CHEAT_KROZ:
                    ud.god = 1-ud.god;

                    if (ud.god)
                    {
                        pus = 1;
                        pub = 1;
                        sprite[g_player[myconnectindex].ps->i].cstat = 257;

                        actor[g_player[myconnectindex].ps->i].temp_data[0] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[1] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[2] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[3] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[4] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[5] = 0;

                        sprite[g_player[myconnectindex].ps->i].hitag = 0;
                        sprite[g_player[myconnectindex].ps->i].lotag = 0;
                        sprite[g_player[myconnectindex].ps->i].pal = g_player[myconnectindex].ps->palookup;

                        P_DoQuote(17,g_player[myconnectindex].ps);
                    }
                    else
                    {
                        ud.god = 0;
                        sprite[g_player[myconnectindex].ps->i].extra = g_player[myconnectindex].ps->max_player_health;
                        actor[g_player[myconnectindex].ps->i].extra = -1;
                        g_player[myconnectindex].ps->last_extra = g_player[myconnectindex].ps->max_player_health;
                        P_DoQuote(18,g_player[myconnectindex].ps);
                    }

                    sprite[g_player[myconnectindex].ps->i].extra = g_player[myconnectindex].ps->max_player_health;
                    actor[g_player[myconnectindex].ps->i].extra = 0;
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_SCREAMFORME:
                    ud.god = 1-ud.god;

                    if (ud.god)
                    {
                        pus = 1;
                        pub = 1;
                        sprite[g_player[myconnectindex].ps->i].cstat = 257;

                        actor[g_player[myconnectindex].ps->i].temp_data[0] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[1] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[2] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[3] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[4] = 0;
                        actor[g_player[myconnectindex].ps->i].temp_data[5] = 0;

                        sprite[g_player[myconnectindex].ps->i].hitag = 0;
                        sprite[g_player[myconnectindex].ps->i].lotag = 0;
                        sprite[g_player[myconnectindex].ps->i].pal = g_player[myconnectindex].ps->palookup;
                        Bstrcpy(ScriptQuotes[122],"Scream for me, Long Beach!");
                        P_DoQuote(122,g_player[myconnectindex].ps);
                        G_CheatGetInv();
                        for (weapon = PISTOL_WEAPON;weapon < MAX_WEAPONS;weapon++)
                            g_player[myconnectindex].ps->gotweapon[weapon]  = 1;

                        for (weapon = PISTOL_WEAPON;
                                weapon < (MAX_WEAPONS);
                                weapon++)
                            P_AddAmmo(weapon, g_player[myconnectindex].ps, g_player[myconnectindex].ps->max_ammo_amount[weapon]);
                        ud.got_access = 7;
                    }
                    else
                    {
                        sprite[g_player[myconnectindex].ps->i].extra = g_player[myconnectindex].ps->max_player_health;
                        actor[g_player[myconnectindex].ps->i].extra = -1;
                        g_player[myconnectindex].ps->last_extra = g_player[myconnectindex].ps->max_player_health;
                        P_DoQuote(18,g_player[myconnectindex].ps);
                    }

                    sprite[g_player[myconnectindex].ps->i].extra = g_player[myconnectindex].ps->max_player_health;
                    actor[g_player[myconnectindex].ps->i].extra = 0;
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_STUFF:

                    j = 0;

                    if (VOLUMEONE)
                        j = 6;

                    for (weapon = PISTOL_WEAPON;weapon < MAX_WEAPONS-j;weapon++)
                        g_player[myconnectindex].ps->gotweapon[weapon]  = 1;

                    for (weapon = PISTOL_WEAPON;
                            weapon < (MAX_WEAPONS-j);
                            weapon++)
                        P_AddAmmo(weapon, g_player[myconnectindex].ps, g_player[myconnectindex].ps->max_ammo_amount[weapon]);
                    G_CheatGetInv();
                    ud.got_access =              7;
                    P_DoQuote(5,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;

                    //                        P_DoQuote(21,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    g_player[myconnectindex].ps->inven_icon = 1;
                    return;

                case CHEAT_SCOTTY:
                case CHEAT_SKILL:
                    if (k == CHEAT_SCOTTY)
                    {
                        i = Bstrlen(CheatStrings[k])-3+VOLUMEONE;
                        if (!consolecheat)
                        {
                            // JBF 20030914
                            short volnume,levnume;
                            if (VOLUMEALL)
                            {
                                volnume = cheatbuf[i] - '0';
                                levnume = (cheatbuf[i+1] - '0')*10+(cheatbuf[i+2]-'0');
                            }
                            else
                            {
                                volnume =  cheatbuf[i] - '0';
                                levnume =  cheatbuf[i+1] - '0';
                            }

                            volnume--;
                            levnume--;

                            if ((VOLUMEONE && volnume > 0) || volnume > g_numVolumes-1 ||
                                    levnume >= MAXLEVELS || MapInfo[volnume*MAXLEVELS+levnume].filename == NULL)
                            {
                                g_player[myconnectindex].ps->cheat_phase = 0;
                                KB_FlushKeyboardQueue();
                                return;
                            }

                            ud.m_volume_number = ud.volume_number = volnume;
                            ud.m_level_number = ud.level_number = levnume;
                        }
                        else
                        {
                            // JBF 20030914
                            ud.m_volume_number = ud.volume_number = osdcmd_cheatsinfo_stat.volume;
                            ud.m_level_number = ud.level_number = osdcmd_cheatsinfo_stat.level;
                        }
                    }
                    else
                    {
                        i = Bstrlen(CheatStrings[k])-1;
                        ud.m_player_skill = ud.player_skill = cheatbuf[i] - '1';
                    }
                    if (numplayers > 1 && myconnectindex == connecthead)
                        Net_NewGame(ud.m_volume_number,ud.m_level_number);
                    else g_player[myconnectindex].ps->gm |= MODE_RESTART;

                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_COORDS:
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    ud.coords = 1-ud.coords;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_VIEW:
                    if (g_player[myconnectindex].ps->over_shoulder_on)
                        g_player[myconnectindex].ps->over_shoulder_on = 0;
                    else
                    {
                        g_player[myconnectindex].ps->over_shoulder_on = 1;
                        g_cameraDistance = 0;
                        g_cameraClock = (int32_t)totalclock;
                    }
                    P_DoQuote(22,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_TIME:

                    P_DoQuote(21,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_UNLOCK:
                    if (VOLUMEONE) return;

                    for (i=numsectors-1;i>=0;i--) //Unlock
                    {
                        j = sector[i].lotag;
                        if (j == -1 || j == 32767) continue;
                        if ((j & 0x7fff) > 2)
                        {
                            if (j&(0xffff-16384))
                                sector[i].lotag &= (0xffff-16384);
                            G_OperateSectors(i,g_player[myconnectindex].ps->i);
                        }
                    }
                    G_OperateForceFields(g_player[myconnectindex].ps->i,-1);

                    P_DoQuote(100,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_CASHMAN:
                    ud.cashman = 1-ud.cashman;
                    KB_ClearKeyDown(sc_N);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    return;

                case CHEAT_ITEMS:
                    G_CheatGetInv();
                    ud.got_access =              7;
                    P_DoQuote(5,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_SHOWMAP: // SHOW ALL OF THE MAP TOGGLE;
                    ud.showallmap = 1-ud.showallmap;
                    if (ud.showallmap)
                    {
                        for (i=0;i<(MAXSECTORS>>3);i++)
                            show2dsector[i] = 255;
                        for (i=0;i<(MAXWALLS>>3);i++)
                            show2dwall[i] = 255;
                        P_DoQuote(111,g_player[myconnectindex].ps);
                    }
                    else
                    {
                        for (i=0;i<(MAXSECTORS>>3);i++)
                            show2dsector[i] = 0;
                        for (i=0;i<(MAXWALLS>>3);i++)
                            show2dwall[i] = 0;
                        P_DoQuote(1,g_player[myconnectindex].ps);
                    }
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_TODD:
                    P_DoQuote(99,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_RATE:
                    ud.tickrate = !ud.tickrate;
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_BETA:
                    P_DoQuote(105,g_player[myconnectindex].ps);
                    KB_ClearKeyDown(sc_H);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_HYPER:
                    g_player[myconnectindex].ps->steroids_amount = 399;
                    g_player[myconnectindex].ps->heat_amount = 1200;
                    g_player[myconnectindex].ps->cheat_phase = 0;
                    P_DoQuote(37,g_player[myconnectindex].ps);
                    KB_FlushKeyboardQueue();
                    return;

                case CHEAT_MONSTERS:
                {
                   const char *s[] = { "ON", "OFF", "ON" };

                    g_noEnemies++;
                    if (g_noEnemies == 3) g_noEnemies = 0;
                    g_player[screenpeek].ps->cheat_phase = 0;
                    Bsprintf(ScriptQuotes[122],"MONSTERS: %s",s[(unsigned char)g_noEnemies]);
                    P_DoQuote(122,g_player[myconnectindex].ps);
                    KB_FlushKeyboardQueue();
                    return;
                }
                case CHEAT_RESERVED:
                case CHEAT_RESERVED3:
                    ud.eog = 1;
                    g_player[myconnectindex].ps->gm |= MODE_EOL;
                    KB_FlushKeyboardQueue();
                    return;
                }
            }
        }
    }
    else
    {
        if (KB_KeyPressed((unsigned char)CheatKeys[0]))
        {
            if (g_player[myconnectindex].ps->cheat_phase >= 0 && numplayers < 2 && ud.recstat == 0)
            {
                if (CheatKeys[0] == CheatKeys[1])
                    KB_ClearKeyDown((unsigned char)CheatKeys[0]);
                g_player[myconnectindex].ps->cheat_phase = -1;
            }
        }

        if (KB_KeyPressed((unsigned char)CheatKeys[1]))
        {
            if (g_player[myconnectindex].ps->cheat_phase == -1)
            {
                if (ud.player_skill == 4)
                {
                    P_DoQuote(22,g_player[myconnectindex].ps);
                    g_player[myconnectindex].ps->cheat_phase = 0;
                }
                else
                {
                    g_player[myconnectindex].ps->cheat_phase = 1;
                    //                    P_DoQuote(25,g_player[myconnectindex].ps);
                    cheatbuflen = 0;
                }
                KB_FlushKeyboardQueue();
            }
            else if (g_player[myconnectindex].ps->cheat_phase != 0)
            {
                g_player[myconnectindex].ps->cheat_phase = 0;
                KB_ClearKeyDown((unsigned char)CheatKeys[0]);
                KB_ClearKeyDown((unsigned char)CheatKeys[1]);
            }
        }
    }
}

static void G_HandleLocalKeys(void)
{
    int i,ch;
    int j;

    CONTROL_ProcessBinds();

    if (ud.recstat == 2)
    {
        ControlInfo noshareinfo;
        CONTROL_GetInput(&noshareinfo);
    }

    if (g_player[myconnectindex].gotvote == 0 && voting != -1 && voting != myconnectindex)
    {
        if (KB_UnBoundKeyPressed(sc_F1) || KB_UnBoundKeyPressed(sc_F2) || ud.autovote)
        {
            tempbuf[0] = PACKET_TYPE_MAP_VOTE;
            tempbuf[1] = myconnectindex;
            tempbuf[2] = (KB_UnBoundKeyPressed(sc_F1) || ud.autovote?ud.autovote-1:0);

            TRAVERSE_CONNECT(i)
            {
                if (i != myconnectindex) mmulti_sendpacket(i,(unsigned char*)tempbuf,3);
                if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
            }
            G_AddUserQuote("VOTE CAST");
            g_player[myconnectindex].gotvote = 1;
            KB_ClearKeyDown(sc_F1);
            KB_ClearKeyDown(sc_F2);
            voting = -1;
        }
    }

    if (!ALT_IS_PRESSED && ud.overhead_on == 0 && (g_player[myconnectindex].ps->gm & MODE_TYPE) == 0)
    {
        if (BUTTON(gamefunc_Enlarge_Screen))
        {
            CONTROL_ClearButton(gamefunc_Enlarge_Screen);
            if (!SHIFTS_IS_PRESSED)
            {
                if (ud.screen_size > 0)
                    S_PlaySound(THUD);
                if (videoGetRenderMode() >= 3 && ud.screen_size == 8 && ud.statusbarmode == 0)
                    ud.statusbarmode = 1;
                else ud.screen_size -= 4;

                if (ud.statusbarscale == 100 && ud.statusbarmode == 1)
                {
                    ud.statusbarmode = 0;
                    ud.screen_size -= 4;
                }
            }
            else
            {
                ud.statusbarscale += 4;
                G_SetStatusBarScale(ud.statusbarscale);
            }
            G_UpdateScreenArea();
        }

        if (BUTTON(gamefunc_Shrink_Screen))
        {
            CONTROL_ClearButton(gamefunc_Shrink_Screen);
            if (!SHIFTS_IS_PRESSED)
            {
                if (ud.screen_size < 64) S_PlaySound(THUD);
                if (videoGetRenderMode() >= 3 && ud.screen_size == 8 && ud.statusbarmode == 1)
                    ud.statusbarmode = 0;
                else ud.screen_size += 4;
            }
            else
            {
                ud.statusbarscale -= 4;
                if (ud.statusbarscale < 37)
                    ud.statusbarscale = 37;
                G_SetStatusBarScale(ud.statusbarscale);
            }
            G_UpdateScreenArea();
        }
    }

    if (g_player[myconnectindex].ps->cheat_phase == 1 || (g_player[myconnectindex].ps->gm&(MODE_MENU|MODE_TYPE))) return;

    if (BUTTON(gamefunc_See_Coop_View) && (GTFLAGS(GAMETYPE_COOPVIEW) || ud.recstat == 2))
    {
        CONTROL_ClearButton(gamefunc_See_Coop_View);
        screenpeek = connectpoint2[screenpeek];
        if (screenpeek == -1) screenpeek = connecthead;
        g_restorePalette = 1;
    }

    if (ud.multimode > 1 && BUTTON(gamefunc_Show_Opponents_Weapon))
    {
        CONTROL_ClearButton(gamefunc_Show_Opponents_Weapon);
        ud.showweapons = 1-ud.showweapons;
        P_DoQuote(82-ud.showweapons,g_player[screenpeek].ps);
    }

    if (BUTTON(gamefunc_Toggle_Crosshair))
    {
        CONTROL_ClearButton(gamefunc_Toggle_Crosshair);
        ud.crosshair = !ud.crosshair;
        if (ud.crosshair)
            P_DoQuote(20,g_player[screenpeek].ps);
        else P_DoQuote(21,g_player[screenpeek].ps);
    }

    if (ud.overhead_on && BUTTON(gamefunc_Map_Follow_Mode))
    {
        CONTROL_ClearButton(gamefunc_Map_Follow_Mode);
        ud.scrollmode = 1-ud.scrollmode;
        if (ud.scrollmode)
        {
            ud.folx = g_player[screenpeek].ps->opos.x;
            ud.foly = g_player[screenpeek].ps->opos.y;
            ud.fola = fix16_to_int(g_player[screenpeek].ps->oq16ang);
        }
        P_DoQuote(83+ud.scrollmode,g_player[myconnectindex].ps);
    }

    if (SHIFTS_IS_PRESSED || ALT_IS_PRESSED)
    {
        i = 0;
        if (KB_UnBoundKeyPressed(sc_F1))
        {
            KB_ClearKeyDown(sc_F1);
            i = 1;
        }
        if (KB_UnBoundKeyPressed(sc_F2))
        {
            KB_ClearKeyDown(sc_F2);
            i = 2;
        }
        if (KB_UnBoundKeyPressed(sc_F3))
        {
            KB_ClearKeyDown(sc_F3);
            i = 3;
        }
        if (KB_UnBoundKeyPressed(sc_F4))
        {
            KB_ClearKeyDown(sc_F4);
            i = 4;
        }
        if (KB_UnBoundKeyPressed(sc_F5))
        {
            KB_ClearKeyDown(sc_F5);
            i = 5;
        }
        if (KB_UnBoundKeyPressed(sc_F6))
        {
            KB_ClearKeyDown(sc_F6);
            i = 6;
        }
        if (KB_UnBoundKeyPressed(sc_F7))
        {
            KB_ClearKeyDown(sc_F7);
            i = 7;
        }
        if (KB_UnBoundKeyPressed(sc_F8))
        {
            KB_ClearKeyDown(sc_F8);
            i = 8;
        }
        if (KB_UnBoundKeyPressed(sc_F9))
        {
            KB_ClearKeyDown(sc_F9);
            i = 9;
        }
        if (KB_UnBoundKeyPressed(sc_F10))
        {
            KB_ClearKeyDown(sc_F10);
            i = 10;
        }

        if (i)
        {
            if (SHIFTS_IS_PRESSED)
            {
                if (i == 5 && g_player[myconnectindex].ps->fta > 0 && g_player[myconnectindex].ps->ftq == 26)
                {
                    i = (VOLUMEALL?MAXVOLUMES*MAXLEVELS:6);
                    g_musicIndex++;
                    while ((MapInfo[g_musicIndex].musicfn == NULL) && g_musicIndex < i)
                        g_musicIndex++;
                    if (g_musicIndex == i)
                        g_musicIndex = 0;
                    if (MapInfo[g_musicIndex].musicfn != NULL)
                    {
                        if (S_PlayMusic(&MapInfo[g_musicIndex].musicfn[0],g_musicIndex))
                            Bsprintf(ScriptQuotes[26],"PLAYING %s",&MapInfo[g_musicIndex].alt_musicfn[0]);
                        else
                            Bsprintf(ScriptQuotes[26],"PLAYING %s",&MapInfo[g_musicIndex].musicfn[0]);
                        P_DoQuote(26,g_player[myconnectindex].ps);
                    }
                    return;
                }

                G_AddUserQuote(ud.ridecule[i-1]);

                ch = 0;

                tempbuf[ch] = PACKET_TYPE_MESSAGE;
                tempbuf[ch+1] = 255;
                tempbuf[ch+2] = 0;
                Bstrcat(tempbuf+2,ud.ridecule[i-1]);

                i = 2+strlen(ud.ridecule[i-1]);

                if (ud.multimode > 1)
                    TRAVERSE_CONNECT(ch)
                {
                    if (ch != myconnectindex) mmulti_sendpacket(ch,(unsigned char*)tempbuf,i);
                    if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
                }

                pus = NUMPAGES;
                pub = NUMPAGES;

                return;

            }

            if (ud.lockout == 0)
                if (ud.config.SoundToggle && ALT_IS_PRESSED && (RTS_NumSounds() > 0) && g_RTSPlaying == 0 && (ud.config.VoiceToggle & 1))
                {
                    rtsptr = (char *)RTS_GetSound(i-1);
                    FX_Play3D(rtsptr,RTS_SoundLength(i-1), FX_ONESHOT, 0,0,0,255, fix16_one, -i);

                    g_RTSPlaying = 7;

                    if (ud.multimode > 1)
                    {
                        tempbuf[0] = PACKET_TYPE_RTS;
                        tempbuf[1] = i;

                        TRAVERSE_CONNECT(ch)
                        {
                            if (ch != myconnectindex) mmulti_sendpacket(ch,(unsigned char*)tempbuf,2);
                            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && (myconnectindex != connecthead)) break; //slaves in M/S mode only send to master
                        }
                    }

                    pus = NUMPAGES;
                    pub = NUMPAGES;

                    return;
                }
        }
    }

    if (!ALT_IS_PRESSED && !SHIFTS_IS_PRESSED)
    {

        if (ud.multimode > 1 && BUTTON(gamefunc_SendMessage))
        {
            KB_FlushKeyboardQueue();
            CONTROL_ClearButton(gamefunc_SendMessage);
            g_player[myconnectindex].ps->gm |= MODE_TYPE;
            typebuf[0] = 0;
            inputloc = 0;
        }

        if (KB_UnBoundKeyPressed(sc_F1) || (ud.show_help && (KB_KeyPressed(sc_Space) || KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || MOUSE_GetButtons()&LEFT_MOUSE)))
        {
            KB_ClearKeyDown(sc_F1);
            KB_ClearKeyDown(sc_Space);
            KB_ClearKeyDown(sc_kpad_Enter);
            KB_ClearKeyDown(sc_Enter);
            MOUSE_ClearButton(LEFT_MOUSE);
            ud.show_help ++;

            if (ud.show_help > 2)
            {
                ud.show_help = 0;
                if (ud.multimode < 2 && ud.recstat != 2) ready2send = 1;
                G_UpdateScreenArea();
            }
            else
            {
                videoSetViewableArea(0,0,xdim-1,ydim-1);
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 0;
                    totalclock = ototalclock;
                }
            }
        }

        //        if(ud.multimode < 2)
        {
            if (ud.recstat != 2 && KB_UnBoundKeyPressed(sc_F2))
            {
                KB_ClearKeyDown(sc_F2);

FAKE_F2:
                if (sprite[g_player[myconnectindex].ps->i].extra <= 0)
                {
                    P_DoQuote(118,g_player[myconnectindex].ps);
                    return;
                }
                ChangeToMenu(350);
                g_screenCapture = 1;
                G_DrawRooms(myconnectindex,65536);
                //savetemp("duke3d.tmp",waloff[TILE_SAVESHOT],160*100);
                g_screenCapture = 0;
                FX_StopAllSounds();
                S_ClearSoundLocks();

                //                videoSetViewableArea(0,0,xdim-1,ydim-1);
                g_player[myconnectindex].ps->gm |= MODE_MENU;

                if (ud.multimode < 2)
                {
                    ready2send = 0;
                    totalclock = ototalclock;
                    screenpeek = myconnectindex;
                }
            }

            if (KB_UnBoundKeyPressed(sc_F3))
            {
                KB_ClearKeyDown(sc_F3);

FAKE_F3:
                ChangeToMenu(300);
                FX_StopAllSounds();
                S_ClearSoundLocks();

                //                videoSetViewableArea(0,0,xdim-1,ydim-1);
                g_player[myconnectindex].ps->gm |= MODE_MENU;
                if (ud.multimode < 2 && ud.recstat != 2)
                {
                    ready2send = 0;
                    totalclock = ototalclock;
                }
                screenpeek = myconnectindex;
            }
        }

        if (KB_UnBoundKeyPressed(sc_F4) && ud.config.FXDevice >= 0)
        {
            KB_ClearKeyDown(sc_F4);
            FX_StopAllSounds();
            S_ClearSoundLocks();

            g_player[myconnectindex].ps->gm |= MODE_MENU;
            if (ud.multimode < 2 && ud.recstat != 2)
            {
                ready2send = 0;
                totalclock = ototalclock;
            }
            ChangeToMenu(701);

        }

        if ((BUTTON(gamefunc_Quick_Save) || g_doQuickSave == 1) && (g_player[myconnectindex].ps->gm&MODE_GAME))
        {
            CONTROL_ClearButton(gamefunc_Quick_Save);
            g_doQuickSave = 0;

            if (g_lastSaveSlot == -1) goto FAKE_F2;

            KB_FlushKeyboardQueue();

            if (sprite[g_player[myconnectindex].ps->i].extra <= 0)
            {
                P_DoQuote(118,g_player[myconnectindex].ps);
                return;
            }
            g_screenCapture = 1;
            G_DrawRooms(myconnectindex,65536);
            //savetemp("duke3d.tmp",waloff[TILE_SAVESHOT],160*100);
            g_screenCapture = 0;
            if (g_lastSaveSlot >= 0)
            {
                /*                inputloc = Bstrlen(&ud.savegame[g_lastSaveSlot][0]);
                                g_currentMenu = 360+g_lastSaveSlot;
                                probey = g_lastSaveSlot; */
                if (ud.multimode > 1)
                    G_SavePlayer(-1-(g_lastSaveSlot));
                else G_SavePlayer(g_lastSaveSlot);
            }
        }

        if (BUTTON(gamefunc_Third_Person_View))
        {
            CONTROL_ClearButton(gamefunc_Third_Person_View);

            if (g_player[myconnectindex].ps->over_shoulder_on)
                g_player[myconnectindex].ps->over_shoulder_on = 0;
            else
            {
                g_player[myconnectindex].ps->over_shoulder_on = 1;
                g_cameraDistance = 0;
                g_cameraClock = (int32_t)totalclock;
            }
            P_DoQuote(109 + g_player[myconnectindex].ps->over_shoulder_on, g_player[myconnectindex].ps);
        }

        if (KB_UnBoundKeyPressed(sc_F5) && ud.config.MusicDevice >= 0)
        {
            KB_ClearKeyDown(sc_F5);
            if (MapInfo[g_musicIndex].alt_musicfn != NULL)
                Bstrcpy(ScriptQuotes[26],&MapInfo[g_musicIndex].alt_musicfn[0]);
            else if (MapInfo[g_musicIndex].musicfn != NULL)
            {
                Bstrcpy(ScriptQuotes[26],&MapInfo[g_musicIndex].musicfn[0]);
                Bstrcat(ScriptQuotes[26],".  USE SHIFT-F5 TO CHANGE.");
            }
            else ScriptQuotes[26][0] = '\0';
            P_DoQuote(26,g_player[myconnectindex].ps);
        }

        if (KB_UnBoundKeyPressed(sc_F8))
        {
            KB_ClearKeyDown(sc_F8);
            ud.fta_on = !ud.fta_on;
            if (ud.fta_on) P_DoQuote(23,g_player[myconnectindex].ps);
            else
            {
                ud.fta_on = 1;
                P_DoQuote(24,g_player[myconnectindex].ps);
                ud.fta_on = 0;
            }
        }

        if ((BUTTON(gamefunc_Quick_Load) || g_doQuickSave == 2) && (g_player[myconnectindex].ps->gm&MODE_GAME))
        {
            CONTROL_ClearButton(gamefunc_Quick_Load);
            g_doQuickSave = 0;

            if (g_lastSaveSlot == -1) goto FAKE_F3;

            if (g_lastSaveSlot >= 0)
            {
                KB_FlushKeyboardQueue();
                KB_ClearKeysDown();
                FX_StopAllSounds();

                if (ud.multimode > 1)
                {
                    G_LoadPlayer(-1-g_lastSaveSlot);
                    g_player[myconnectindex].ps->gm = MODE_GAME;
                }
                else
                {
                    i = G_LoadPlayer(g_lastSaveSlot);
                    if (i == 0)
                        g_player[myconnectindex].ps->gm = MODE_GAME;
                }
            }
        }

        if (KB_UnBoundKeyPressed(sc_F10))
        {
            KB_ClearKeyDown(sc_F10);
            ChangeToMenu(500);
            FX_StopAllSounds();
            S_ClearSoundLocks();
            g_player[myconnectindex].ps->gm |= MODE_MENU;
            if (ud.multimode < 2 && ud.recstat != 2)
            {
                ready2send = 0;
                totalclock = ototalclock;
            }
        }

        if (ud.overhead_on != 0)
        {

            j = (int32_t)totalclock-nonsharedtimer;
            nonsharedtimer += j;
            if (BUTTON(gamefunc_Enlarge_Screen))
                g_player[myconnectindex].ps->zoom += mulscale6(j,max(g_player[myconnectindex].ps->zoom,256));
            if (BUTTON(gamefunc_Shrink_Screen))
                g_player[myconnectindex].ps->zoom -= mulscale6(j,max(g_player[myconnectindex].ps->zoom,256));

            if ((g_player[myconnectindex].ps->zoom > 2048))
                g_player[myconnectindex].ps->zoom = 2048;
            if ((g_player[myconnectindex].ps->zoom < 48))
                g_player[myconnectindex].ps->zoom = 48;

        }
    }

    if (KB_KeyPressed(sc_Escape) && ud.overhead_on && g_player[myconnectindex].ps->newowner == -1)
    {
        KB_ClearKeyDown(sc_Escape);
        ud.last_overhead = ud.overhead_on;
        ud.overhead_on = 0;
        ud.scrollmode = 0;
        G_UpdateScreenArea();
    }

    if (BUTTON(gamefunc_AutoRun))
    {
        CONTROL_ClearButton(gamefunc_AutoRun);
        ud.auto_run = 1-ud.auto_run;
        P_DoQuote(85+ud.auto_run,g_player[myconnectindex].ps);
    }

    if (BUTTON(gamefunc_Map))
    {
        CONTROL_ClearButton(gamefunc_Map);
        if (ud.last_overhead != ud.overhead_on && ud.last_overhead)
        {
            ud.overhead_on = ud.last_overhead;
            ud.last_overhead = 0;
        }
        else
        {
            ud.overhead_on++;
            if (ud.overhead_on == 3) ud.overhead_on = 0;
            ud.last_overhead = ud.overhead_on;
        }
        g_restorePalette = 1;
        G_UpdateScreenArea();
    }
    /*
        if (KB_UnBoundKeyPressed(sc_F11))
        {
            KB_ClearKeyDown(sc_F11);
            ud.brightness+=8;
            if (SHIFTS_IS_PRESSED) ud.brightness-=16;

            if (ud.brightness > (7<<3))
                ud.brightness = 0;
            else if (ud.brightness < 0)
                ud.brightness = (7<<3);

            videoSetPalette(ud.brightness>>2,&g_player[myconnectindex].ps->palette[0],0);
            if (ud.brightness < 40) P_DoQuote(29 + (ud.brightness>>3) ,g_player[myconnectindex].ps);
            else if (ud.brightness < 80) P_DoQuote(96 + (ud.brightness>>3) - 5,g_player[myconnectindex].ps);
        }
    */
    if (KB_UnBoundKeyPressed(sc_F11))
    {
        KB_ClearKeyDown(sc_F11);
        ChangeToMenu(232);
        FX_StopAllSounds();
        S_ClearSoundLocks();
        g_player[myconnectindex].ps->gm |= MODE_MENU;
        if (ud.multimode < 2 && ud.recstat != 2)
        {
            ready2send = 0;
            totalclock = ototalclock;
        }
    }
}

static void G_ShowParameterHelp(void)
{
    const char *s = "Usage: " APPBASENAME " [files] [options]\n"
              "Example: " APPBASENAME " -q4 -a -m -tx -map nukeland.map\n\n"
              "Files can be *.grp/zip/con/def\n"
              "\n"
              "-allowvisibility\tAllows visibility adjustment in multiplayer."
              "\n"
              "-cfg [file.cfg]\tUse an alternate configuration file\n"
              "-c#\t\tUse MP mode #, 1 = Dukematch, 2 = Coop, 3 = Team Dukematch\n"
              "-d[file.dmo]\tPlay a demo\n"
              "-g[file.grp]\tUse an extra group file\n"
              "-h[file.def]\tUse an alternate def\n"
              "-j[dir]\t\tAdds a directory to EDuke32's search list\n"
              "-l#\t\tWarp to level #, see -v\n"
              "-map [file.map]\tLoads a map\n"
              "-m\t\tDisable monsters\n"
              "-nam/-ww2gi\tRun in NAM or WW2GI-compatible mode\n"
              "-net <command>\tEnable multiplayer (see below)\n"
              "\n"
              "net command examples:\n"
              "-net /n0:3\tHost 3-player match in master/slave mode.\n"
              "-net /n0 192.168.0.1\tJoin specified IP in master/slave mode.\n"
              "\n"
              "-noffire\t\tDisable friendly fire\n"
              "-r\t\tRecord demo\n"
              "-s#\t\tSet skill level (1-4)\n"
#if STARTUP_WINDOW
              "-setup/nosetup\tEnables/disables startup window\n"
#endif
              "-t#\t\tSet respawn mode: 1 = Monsters, 2 = Items, 3 = Inventory, x = All\n"
#if !defined(_WIN32)
              "-usecwd\t\tRead game data and configuration file from working directory\n"
#endif
              "-u#########\tUser's favorite weapon order (default: 3425689071)\n"
              "-v#\t\tWarp to volume #, see -l\n"
              "-wstay\t\tEnable Weapon Stay\n"
              "-x[game.con]\tLoad custom CON script\n"
              "-y##\t\tSet frag limit for DukeMatch\n"
              "-#\t\tLoad and run a game from slot # (0-9)\n"
//              "\n-?/--help\tDisplay this help message and exit\n"
              "\nSee eduke32 -debughelp for debug parameters"
              ;
#if _WIN32
    Bsprintf(tempbuf,HEAD2 " (%s)", s_buildTimestamp);
    wm_msgbox(tempbuf,s);
#else
    initprintf("%s\n",s);
#endif
}

static void G_ShowDebugHelp(void)
{
    const char *s = "Usage: " APPBASENAME " [files] [options]\n"
              "\n"
              "-a\t\tUse fake player AI (fake multiplayer only)\n"
              "-cachesize #\tSets cache size, in Kb\n"
              "-fNUM\t\tSend fewer packets in multiplayer (1, 2, 4) (deprecated)\n"
              "-game_dir [dir]\tDuke3d_w32 compatibility option, see -j\n"
              "-gamegrp   \tSelects which file to use as main grp\n"
              "-i#\t\tUse networking mode (1/0)\n"
              "-name [name]\tPlayer name in multiplay\n"
              "-nD\t\tDump default gamevars to gamevars.txt\n"
              "-noautoload\tDisable loading content from autoload dir\n"
              "-nologo\t\tSkip the logo anim\n"
              "-ns/-nm\t\tDisable sound or music\n"
              "-q#\t\tFake multiplayer with # (2-8) players\n"
              "-rmnet\t\tUse network config file (OBSOLETE, see -net)\n"
              "-stun\t\tUse UDP hole punching for multiplayer connections\n"
              "-unstable   \tForce EDuke32 to execute unsafe CON commands (and crash)\n"
              "-w\t\tShow coordinates\n"
              "-z#/-condebug\tEnable line-by-line CON compile debugging at level #\n"
              ;
#ifdef _WIN32
    Bsprintf(tempbuf,HEAD2 " (%s)",s_buildTimestamp);
    wm_msgbox(tempbuf,s);
#else
    initprintf("%s\n",s);
#endif
}

#ifndef RANCID_NETWORKING
#if 0
static signed int rancid_players = 0;
static char rancid_ip_strings[MAXPLAYERS][32], rancid_local_port_string[8];

extern int getexternaladdress(char *buffer, const char *host, int port);

static int load_rancid_net(const char *fn)
{
    int tokn;

    tokenlist rancidtokens[] =
    {
        { "interface",       T_INTERFACE       },
        { "mode",            T_MODE            },
        { "allow",           T_ALLOW           },
    };

    scriptfile *script;

    script = scriptfile_fromfile(fn);
    if (!script) return -1;

    while (1)
    {
        tokn = getatoken(script,rancidtokens,sizeof(rancidtokens)/sizeof(tokenlist));
        switch (tokn)
        {
        case T_INTERFACE:
        {
            char *ip;

            if (scriptfile_getstring(script,&ip)) break;
            Bstrcpy(rancid_ip_strings[MAXPLAYERS-1],ip);
            Bstrcpy(rancid_ip_strings[rancid_players++],ip);
            if (strtok(ip,":"))
            {
                char *p = strtok(NULL,":");

                if (p != NULL)
                {
                    if (atoi(p) > 1024)
                        Bsprintf(rancid_local_port_string,"-p %s",p);
                }
            }
        }
        break;
        case T_MODE:
        {
            char *mode;

            if (scriptfile_getstring(script,&mode)) break;
        }
        break;
        case T_ALLOW:
        {
            char *ip;

            if (scriptfile_getstring(script,&ip)) break;
            Bstrcpy(rancid_ip_strings[rancid_players++],ip);
        }
        break;
        case T_EOF:
            return(0);
        default:
            break;
        }
    }

    scriptfile_close(script);
    scriptfile_clearsymbols();

    return 0;
}

static inline int stringsort(const char *p1, const char *p2)
{
    return Bstrcmp(&p1[0],&p2[0]);
}

// Not supported with the enet network backend currently
static void setup_rancid_net(const char *fn)
{
    int i;

    if (load_rancid_net(fn) != -1)
    {
        char tmp[32];

        if (!Bstrlen(rancid_ip_strings[MAXPLAYERS-1])||!Bstrlen(rancid_ip_strings[1]))
        {
            if (!Bstrlen(rancid_ip_strings[MAXPLAYERS-1]))
                initprintf("rmnet: Interface not defined!\n");
            if (!Bstrlen(rancid_ip_strings[1]))
                initprintf("rmnet: No peers configured!\n");
            G_GameExit("Malformed network configuration file!");
            return;
        }

        if (g_keepAddr == 0)
        {
            for (i=0;i<rancid_players;i++)
            {
                if (Bstrcmp(rancid_ip_strings[i],rancid_ip_strings[MAXPLAYERS-1]))
                {
                    Bstrncpy(tempbuf,rancid_ip_strings[i], 8);
                    Bstrcpy(tmp,strtok(tempbuf,"."));
                    if (Bstrcmp(tmp,"10") == 0)
                    {
                        i = 0;
                        break;
                    }
                    else if (Bstrcmp(tmp,"192") == 0)
                    {
                        Bstrcpy(tmp,strtok(NULL,"."));
                        if (Bstrcmp(tmp,"168") == 0)
                        {
                            i = 0;
                            break;
                        }
                    }
                    else if (Bstrcmp(tmp,"172") == 0)
                    {
                        Bstrcpy(tmp,strtok(NULL,"."));
                        if (Bstrcmp(tmp,"16") == 0)
                        {
                            i = 0;
                            break;
                        }
                    }
                    else if (Bstrcmp(tmp,"169") == 0)
                    {
                        Bstrcpy(tmp,strtok(NULL,"."));
                        if (Bstrcmp(tmp,"254") == 0)
                        {
                            i = 0;
                            break;
                        }
                    }
                }
            }

            Bstrcpy(tempbuf,rancid_ip_strings[MAXPLAYERS-1]);
            Bstrcpy(tmp,strtok(tempbuf,"."));
            if (i == rancid_players && ((Bstrcmp(tmp,"192") == 0) || (Bstrcmp(tmp,"172") == 0) || (Bstrcmp(tmp,"169") == 0) || (Bstrcmp(tmp,"10") == 0)))
            {
                int ii = getexternaladdress(tempbuf, "checkip.dyndns.org", 8245);
                if (!ii) ii = getexternaladdress(tempbuf, "checkip.dyndns.org", 80);
                if (ii)
                {
                    if (tempbuf[0])
                    {
                        for (i=0;i<rancid_players;i++)
                        {
                            if (Bstrcmp(rancid_ip_strings[i],rancid_ip_strings[MAXPLAYERS-1]) == 0)
                            {
                                Bstrcpy(rancid_ip_strings[MAXPLAYERS-1],tempbuf);
                                Bstrcpy(rancid_ip_strings[i],tempbuf);
                            }
                        }
                    }
                }
                else initprintf("rmnet: Unable to get external interface address!  Expect problems...\n");
            }
        }
        qsort((char *)rancid_ip_strings, rancid_players, sizeof(rancid_ip_strings[0]), (int(*)(const void*,const void*))stringsort);

        g_networkBroadcastMode = NETMODE_P2P;
        netparamcount = rancid_players;
        if (rancid_local_port_string[0] == '-')
            netparamcount++;
        netparam = (char **)Bcalloc(netparamcount, sizeof(char **));

        for (i=0;i<rancid_players;i++)
        {
            if (Bstrcmp(rancid_ip_strings[i],rancid_ip_strings[MAXPLAYERS-1]) == 0)
                Bsprintf(rancid_ip_strings[i],"/n1");
            netparam[i] = (char *)&rancid_ip_strings[i];
        }
        if (i != netparamcount)
            netparam[i] = (char *)&rancid_local_port_string;
    }
}
#endif

#endif

// OLDMP MERGE CHECKME
static BUILDVFS_FIND_REC* finddirshigh = NULL, * findfileshigh = NULL;
static int32_t currentlist = 0;
static fnlist_t fnlist;

#if 0
static void clearfilenames(void)
{
    klistfree(fnlist.finddirs);
    klistfree(fnlist.findfiles);
    fnlist.finddirs = fnlist.findfiles = NULL;
    fnlist.numfiles = fnlist.numdirs = 0;
}
#endif

static int32_t getfilenames(const char* path, const char* kind)
{
    fnlist_getnames(&fnlist, path, kind, BUILDVFS_FIND_DRIVE, 0);

    finddirshigh = fnlist.finddirs;
    findfileshigh = fnlist.findfiles;
    currentlist = (findfileshigh != NULL);

    return 0;
}

static const char *autoloadmasks[] = { "*.grp", "*.zip", "*.pk3" };
#define NUMAUTOLOADMASKS 3

static void G_DoAutoload(const char *fn)
{
    int i;

    for (i=0;i<NUMAUTOLOADMASKS;i++)
    {
        Bsprintf(tempbuf,"autoload/%s",fn);
        getfilenames(tempbuf,autoloadmasks[i]);
        while (fnlist.findfiles)
        {
            Bsprintf(tempbuf,"autoload/%s/%s",fn,fnlist.findfiles->name);
            initprintf("Using group file '%s'.\n",tempbuf);
            initgroupfile(tempbuf);
            fnlist.findfiles = fnlist.findfiles->next;
        }
    }
}

static char *makename(char *destname, char *OGGname, char *origname)
{
    if (!origname)
        return destname;

    if (destname)
        Bfree(destname);
    destname = (char*)Bcalloc(Bstrlen(OGGname) + Bstrlen(origname) + 1, sizeof(char));
    if (!destname)
        return NULL;

    Bstrcpy(destname,(*OGGname)?OGGname:origname);

    if (*OGGname&&OGGname[Bstrlen(OGGname)-1]=='/')
    {
        while (*origname=='/')
            origname++;
        Bstrcat(destname,origname);
    }

    OGGname=Bstrchr(destname,'.');
    if (OGGname)
        Bstrcpy(OGGname,".ogg");
    else Bstrcat(destname,".ogg");

    return destname;
}

static int AL_DefineSound(int ID,char *name)
{
    if (ID>=MAXSOUNDS)
        return 1;
    g_sounds[ID].filename1=makename(g_sounds[ID].filename1,name,g_sounds[ID].filename);
//    initprintf("(%s)(%s)(%s)\n",g_sounds[ID].filename1,name,g_sounds[ID].filename);
//    S_LoadSound(ID);
    return 0;
}

static int AL_DefineMusic(char *ID,char *name)
{
    int lev,ep,sel;char b1,b2;

    if (!ID)
        return 1;
    if (!Bstrcmp(ID,"intro"))
    {
        sel=MAXVOLUMES*MAXLEVELS;  ID=EnvMusicFilename[0];
    }
    else if (!Bstrcmp(ID,"briefing"))
    {
        sel=MAXVOLUMES*MAXLEVELS+1;
        ID=EnvMusicFilename[1];
    }
    else if (!Bstrcmp(ID,"loading"))
    {
        sel=MAXVOLUMES*MAXLEVELS+2;
        ID=EnvMusicFilename[2];
    }
    else
    {
        sscanf(ID,"%c%d%c%d",&b1,&ep,&b2,&lev);
        lev--;
        ep--;
        if (toupper(b1)!='E'||toupper(b2)!='L'||lev>=MAXLEVELS||ep>=MAXVOLUMES)
            return 1;
        sel=(ep*MAXLEVELS)+lev;
        ID=MapInfo[sel].musicfn;
    }

    MapInfo[sel].alt_musicfn=makename(MapInfo[sel].alt_musicfn,name,ID);
//    initprintf("%-15s | ",ID);
//    initprintf("%3d %2d %2d | %s\n",sel,ep,lev,MapInfo[sel].alt_musicfn);
//    S_PlayMusic(ID,sel);
    return 0;
}

static int parsedefinitions_game(scriptfile *script, const int preload)
{
    int tokn;

    tokenlist tokens[] =
    {
        { "include",           T_INCLUDE          },
        { "#include",          T_INCLUDE          },
        { "loadgrp",           T_LOADGRP          },
        { "cachesize",         T_CACHESIZE        },
        { "noautload",         T_NOAUTOLOAD       },
        { "music",             T_MUSIC            },
        { "sound",             T_SOUND            },
        { "addplayercolor",    T_ADDPLAYERCOL     },
        { "delplayercolor",    T_DELPLAYERCOL     },
    };

    tokenlist sound_musictokens[] =
    {
        { "id",   T_ID  },
        { "file", T_FILE },
    };

    while (1)
    {
        tokn = getatoken(script,tokens,sizeof(tokens)/sizeof(tokenlist));
        switch (tokn)
        {
        case T_LOADGRP:
        {
            char *fn;

            pathsearchmode = 1;
            if (!scriptfile_getstring(script,&fn) && preload)
            {
                int j = initgroupfile(fn);

                if (j == -1)
                    initprintf("Could not find group file '%s'.\n",fn);
                else
                {
                    initprintf("Using group file '%s'.\n",fn);
                    if (!g_noAutoLoad)
                        G_DoAutoload(fn);
                }

            }
            pathsearchmode = 0;
        }
        break;
        case T_CACHESIZE:
        {
            int j;
            if (scriptfile_getnumber(script,&j) || !preload) break;

            if (j > 0) MAXCACHE1DSIZE = j<<10;
        }
        break;
        case T_INCLUDE:
        {
            char *fn;
            if (!scriptfile_getstring(script,&fn))
            {
                scriptfile *included = scriptfile_fromfile(fn);

                if (!included)
                {
//                    initprintf("Warning: Failed including %s on line %s:%d\n",
//                               fn, script->filename,scriptfile_getlinum(script,cmdtokptr));
                }
                else
                {
                    parsedefinitions_game(included, preload);
                    scriptfile_close(included);
                }
            }
            break;
        }
        case T_NOAUTOLOAD:
            if (preload)
                g_noAutoLoad = 1;
            break;
        case T_MUSIC:
        {
            char *tinttokptr = script->ltextptr;
            char *ID=NULL,*fn,*tfn = NULL;
            char *musicend;

            if (scriptfile_getbraces(script,&musicend)) break;
            while (script->textptr < musicend)
            {
                switch (getatoken(script,sound_musictokens,sizeof(sound_musictokens)/sizeof(tokenlist)))
                {
                case T_ID:
                    scriptfile_getstring(script,&ID);
                    break;
                case T_FILE:
                    scriptfile_getstring(script,&fn);
                    break;
                }
            }
            if (!preload)
            {
                int i;
                if (ID==NULL)
                {
                    initprintf("Error: missing ID for music definition near line %s:%d\n", script->filename, scriptfile_getlinum(script,tinttokptr));
                    break;
                }

                i = pathsearchmode;
                pathsearchmode = 1;
                if (findfrompath(fn,&tfn) < 0)
                {
                    char buf[BMAX_PATH];

                    Bstrcpy(buf,fn);
                    kzfindfilestart(buf);
                    if (!kzfindfile(buf))
                    {
                        initprintf("Error: file '%s' does not exist\n",fn);
                        pathsearchmode = i;
                        break;
                    }
                }
                else Bfree(tfn);
                pathsearchmode = i;

                if (AL_DefineMusic(ID,fn))
                    initprintf("Error: invalid music ID on line %s:%d\n", script->filename, scriptfile_getlinum(script,tinttokptr));
            }
        }
        break;

        case T_SOUND:
        {
            char *tinttokptr = script->ltextptr;
            char *fn, *tfn = NULL;
            int num=-1;
            char *musicend;

            if (scriptfile_getbraces(script,&musicend)) break;
            while (script->textptr < musicend)
            {
                switch (getatoken(script,sound_musictokens,sizeof(sound_musictokens)/sizeof(tokenlist)))
                {
                case T_ID:
                    scriptfile_getsymbol(script,&num);
                    break;
                case T_FILE:
                    scriptfile_getstring(script,&fn);
                    break;
                }
            }
            if (!preload)
            {
                int i;

                if (num==-1)
                {
                    initprintf("Error: missing ID for sound definition near line %s:%d\n", script->filename, scriptfile_getlinum(script,tinttokptr));
                    break;
                }

                i = pathsearchmode;
                pathsearchmode = 1;
                if (findfrompath(fn,&tfn) < 0)
                {
                    char buf[BMAX_PATH];

                    Bstrcpy(buf,fn);
                    kzfindfilestart(buf);
                    if (!kzfindfile(buf))
                    {
                        initprintf("Error: file '%s' does not exist\n",fn);
                        pathsearchmode = i;
                        break;
                    }
                }
                else Bfree(tfn);
                pathsearchmode = i;

                if (AL_DefineSound(num,fn))
                    initprintf("Error: invalid sound ID on line %s:%d\n", script->filename, scriptfile_getlinum(script,tinttokptr));
            }
        }
        break;

        case T_ADDPLAYERCOL:
        {
            int palnum;
            char* palname;

            if (scriptfile_getsymbol(script, &palnum)) break;
            if (scriptfile_getstring(script, &palname)) break;

            playerColor_add(palname, palnum);
        }
        break;

        case T_DELPLAYERCOL:
        {
            int palnum;

            if (scriptfile_getsymbol(script, &palnum)) break;
            
            playerColor_delete(palnum);
        }
        break;

        case T_EOF:
            return(0);
        default:
            break;
        }
    }
    return 0;
}

static int loaddefinitions_game(const char *fn, int preload)
{
    scriptfile *script;

    script = scriptfile_fromfile(fn);
    if (!script) return -1;

    parsedefinitions_game(script, preload);

    scriptfile_close(script);
    scriptfile_clearsymbols();

    return 0;
}

void G_AddGroup(const char *buffer)
{
    struct strllist *s;
    s = (struct strllist *)Bcalloc(1,sizeof(struct strllist));
    s->str = Bstrdup(buffer);
    if (Bstrchr(s->str,'.') == 0)
        Bstrcat(s->str,".grp");

    if (CommandGrps)
    {
        struct strllist *t;
        for (t = CommandGrps; t->next; t=t->next) ;
        t->next = s;
        return;
    }
    CommandGrps = s;
}

void G_AddPath(const char *buffer)
{
    struct strllist *s;
    s = (struct strllist *)Bcalloc(1,sizeof(struct strllist));
    s->str = strdup(buffer);

    if (CommandPaths)
    {
        struct strllist *t;
        for (t = CommandPaths; t->next; t=t->next) ;
        t->next = s;
        return;
    }
    CommandPaths = s;
}


static void G_CheckCommandLine(int argc, char const* const* argv)
{
    short i, j;
    char *c;
    int firstnet = 0;
    char *k;

    i = 1;

    ud.fta_on = 1;
    ud.god = 0;
    ud.m_respawn_items = 0;
    ud.m_respawn_monsters = 0;
    ud.m_respawn_inventory = 0;
    ud.m_weaponstay = 0;
    ud.m_ffire = 1;
    ud.warp_on = 0;
    ud.cashman = 0;
    ud.m_player_skill = ud.player_skill = 2;
    g_player[0].wchoice[0] = 3;
    g_player[0].wchoice[1] = 4;
    g_player[0].wchoice[2] = 5;
    g_player[0].wchoice[3] = 7;
    g_player[0].wchoice[4] = 8;
    g_player[0].wchoice[5] = 6;
    g_player[0].wchoice[6] = 0;
    g_player[0].wchoice[7] = 2;
    g_player[0].wchoice[8] = 9;
    g_player[0].wchoice[9] = 1;
    Bsprintf(ud.wchoice, "3457860291");

    if (argc > 1)
    {
        initprintf("Application parameters: ");
        while (i < argc)
            initprintf("%s ",argv[i++]);
        initprintf("\n");

        i = 1;
        while (i < argc)
        {
            c = const_cast<char *>(argv[i]);
            if (((*c == '/') || (*c == '-')) && (!firstnet))
            {
                if (!Bstrcasecmp(c+1,"?") || !Bstrcasecmp(c+1,"help") || !Bstrcasecmp(c+1,"-help"))
                {
                    G_ShowParameterHelp();
                    exit(0);
                }
                if (!Bstrcasecmp(c+1,"?") || !Bstrcasecmp(c+1,"debughelp") || !Bstrcasecmp(c+1,"-debughelp"))
                {
                    G_ShowDebugHelp();
                    exit(0);
                }
                if (!Bstrcasecmp(c+1,"grp") || !Bstrcasecmp(c+1,"g"))
                {
                    if (argc > i+1)
                    {
                        G_AddGroup(argv[i+1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"game_dir"))
                {
                    if (argc > i+1)
                    {
                        Bstrcpy(mod_dir,argv[i+1]);
                        G_AddPath(argv[i+1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"cfg"))
                {
                    if (argc > i+1)
                    {
                        Bstrcpy(setupfilename,argv[i+1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"gamegrp"))
                {
                    if (argc > i+1)
                    {
                        Bstrcpy(defaultduke3dgrp,argv[i+1]);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"nam"))
                {
                    strcpy(defaultduke3dgrp, "nam.grp");
                    strcpy(defaultconfilename, "nam.con");
                    g_gameType = GAMENAM;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"ww2gi"))
                {
                    strcpy(defaultduke3dgrp, "ww2gi.grp");
                    strcpy(defaultconfilename, "ww2gi.con");
                    g_gameType = GAMEWW2;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1, "wstay"))
                {
                    ud.m_weaponstay = 1;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"setup"))
                {
                    g_commandSetup = TRUE;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"nosetup"))
                {
                    g_noSetup = 1;
                    g_commandSetup = 0;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"noautoload"))
                {
                    initprintf("Autoload disabled\n");
                    g_noAutoLoad = 1;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"keepaddr"))
                {
                    g_keepAddr = 1;
                    i++;
                    continue;
                }
#if 0 // OLDMP MERGE CHECKME
                if (!Bstrcasecmp(c+1,"stun"))
                {
                    natfree = 1; //Addfaz NatFree
                    i++;
                    continue;
                }
#endif
#ifndef RANCID_NETWORKING
                if (!Bstrcasecmp(c+1,"rmnet"))
                {
                    if (argc > i+1)
                    {
                        g_noSetup = TRUE;
                        g_networkBroadcastMode = NETMODE_P2P;
                        CommandNet = argv[i+1];
                        i++;
                    }
                    i++;
                    continue;
                }
#endif
                if (!Bstrcasecmp(c+1,"net")
#ifdef RANCID_NETWORKING
                        || !Bstrcasecmp(c+1,"rmnet")
#endif
                   )
                {
                    g_noSetup = TRUE;
                    firstnet = i;
                    netparamcount = argc - i - 1;
                    netparam = (char **)Bcalloc(netparamcount, sizeof(char**));
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"name"))
                {
                    if (argc > i+1)
                    {
                        CommandName = argv[i+1];
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"map"))
                {
                    if (argc > i+1)
                    {
                        CommandMap = argv[i+1];
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "noffire"))
                {
                    ud.m_ffire = 0;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c + 1, "allowvisibility"))
                {
                    ud.allow_visibility = 1;
                    initprintf("Visibility adjustment enabled for multiplayer.\n");
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"condebug"))
                {
                    g_scriptDebug = 1;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"nologo"))
                {
                    g_noLogo = 1;
                    i++;
                    continue;
                }
#if !defined(_WIN32)
                if (!Bstrcasecmp(c+1,"usecwd"))
                {
                    usecwd = 1;
                    i++;
                    continue;
                }
#endif
                if (!Bstrcasecmp(c+1,"unstable"))
                {
                    initprintf("WARNING WARNING WARNING WARNING\n"
                               "EDuke32's runtime script error detection has been disabled via "
                               "the '-unstable' command line parameter.  Bug reports from this "
                               "mode are NOT welcome and you should expect crashes in certain "
                               "mods.  Please run EDuke32 without '-unstable' before sending "
                               "any bug reports.\n");
                    g_scriptSanityChecks = 0;
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"cachesize"))
                {
                    if (argc > i+1)
                    {
                        unsigned int j = atol(argv[i+1]);
                        MAXCACHE1DSIZE = j<<10;
                        initprintf("Cache size: %dkB\n",j);
                        i++;
                    }
                    i++;
                    continue;
                }
                if (!Bstrcasecmp(c+1,"noinstancechecking"))
                {
                    i++;
                    continue;
                }
#if defined(RENDERTYPEWIN) && defined(USE_OPENGL)
                if (!Bstrcasecmp(c+1,"forcegl"))
                {
                    forcegl = 1;
                    i++;
                    continue;
                }
#endif
            }

            if (firstnet > 0)
            {
                if (*c == '-' || *c == '/')
                {
                    c++;
                    if (((c[0] == 'n') || (c[0] == 'N')) && (c[1] == '0'))
                    {
                        g_networkBroadcastMode = NETMODE_MASTERSLAVE;
                        initprintf("Network mode: master/slave\n");
                    }
                    else if (((c[0] == 'n') || (c[0] == 'N')) && (c[1] == '1'))
                    {
                        g_networkBroadcastMode = NETMODE_P2P;
                        initprintf("Network mode: peer-to-peer\n");
                    }

                }
                netparam[i-firstnet-1] = const_cast<char*>(argv[i]);
                i++;
                continue;
            }

            if ((*c == '/') || (*c == '-'))
            {
                c++;
                switch (*c)
                {
                case 'a':
                case 'A':
                    ud.playerai = 1;
                    initprintf("Other player AI.\n");
                    break;
                case 'c':
                case 'C':
                    c++;
                    ud.m_coop = 0;
                    while ((*c >= '0')&&(*c <= '9'))
                    {
                        ud.m_coop *= 10;
                        ud.m_coop += *c - '0';
                        c++;
                    }
                    ud.m_coop--;

                    // Make sure that COOP still defaults to weapon stay ON.
                    if (GametypeFlags[ud.m_coop] & GAMETYPE_WEAPSTAY)
                    {
                        ud.m_weaponstay = 1;
                    }
                    break;
                case 'd':
                case 'D':
                    c++;
                    if (strchr(c,'.') == 0)
                        Bstrcat(c,".dmo");
                    initprintf("Play demo %s.\n",c);
                    Bstrcpy(firstdemofile,c);
                    break;
                case 'g':
                case 'G':
                    c++;
                    if (!*c) break;
                    G_AddGroup(c);
                    break;
                case 'h':
                case 'H':
                    c++;
                    if (*c)
                    {
                        duke3ddef = c;
                        initprintf("Using DEF file: %s.\n",duke3ddef);
                    }
                    break;
                case 'i':
                case 'I':
                    c++;
                    if (*c == '0') g_networkBroadcastMode = NETMODE_MASTERSLAVE;
                    if (*c == '1') g_networkBroadcastMode = NETMODE_P2P;
                    initprintf("Network Mode %d\n",g_networkBroadcastMode);
                    break;
                case 'j':
                case 'J':
                    c++;
                    if (!*c) break;
                    G_AddPath(c);
                    break;
                case 'l':
                case 'L':
                    ud.warp_on = 1;
                    c++;
                    ud.m_level_number = ud.level_number = (atoi(c)-1)%MAXLEVELS;
                    break;
                case 'm':
                case 'M':
                    if (*(c+1) != 'a' && *(c+1) != 'A')
                    {
                        ud.m_monsters_off = 1;
                        ud.m_player_skill = ud.player_skill = 0;
                        initprintf("Monsters off.\n");
                    }
                    break;
                case 'n':
                case 'N':
                    c++;
                    if (*c == 's' || *c == 'S')
                    {
                        g_noSound = 2;
                        initprintf("Sound off.\n");
                    }
                    else if (*c == 'm' || *c == 'M')
                    {
                        g_noMusic = 1;
                        initprintf("Music off.\n");
                    }
                    else if (*c == 'd' || *c == 'D')
                    {
                       // FILE * fp=fopen("gamevars.txt","w");
                        Gv_Init();
                        Gv_DumpValues();
                       // fclose(fp);
                        initprintf("Game variables saved.\n");
                    }
                    else
                    {
                        G_ShowParameterHelp();
                        exit(-1);
                    }
                    break;
                case 'q':
                case 'Q':
                    initprintf("Fake multiplayer mode.\n");
                    if (*(++c) == 0) ud.multimode = 1;
                    else ud.multimode = atoi(c)%17;
                    ud.m_coop = ud.coop = 0;
                    ud.m_marker = ud.marker = 1;
                    ud.m_respawn_monsters = ud.respawn_monsters = 1;
                    ud.m_respawn_items = ud.respawn_items = 1;
                    ud.m_respawn_inventory = ud.respawn_inventory = 1;
                    break;
                case 'r':
                case 'R':
                    ud.m_recstat = 1;
                    initprintf("Demo record mode on.\n");
                    break;
                case 's':
                case 'S':
                    c++;
                    ud.m_player_skill = ud.player_skill = (atoi(c)%5);
                    break;
                case 't':
                case 'T':
                    c++;
                    if (Bstrstr(c, "x") != NULL)
                    {
                        initprintf("Respawn All (Items/Inventory/Monsters).\n");
                        ud.m_respawn_monsters = 1;
                        ud.m_respawn_items = 1;
                        ud.m_respawn_inventory = 1;
                    }
                    else
                    {
                        if (Bstrstr(c, "1") != NULL)
                        {
                            initprintf("Respawn Monsters.\n");
                            ud.m_respawn_monsters = 1;
                        }

                        if (Bstrstr(c, "2") != NULL)
                        {
                            initprintf("Respawn Items.\n");
                            ud.m_respawn_items = 1;
                        }

                        if (Bstrstr(c, "3") != NULL)
                        {
                            initprintf("Respawn Inventory.\n");
                            ud.m_respawn_inventory = 1;
                        }
                    }

                    break;
                case 'u':
                case 'U':
                    CommandWeaponChoice = 1;
                    c++;
                    j = 0;
                    if (*c)
                    {
                        initprintf("Using favorite weapon order(s).\n");
                        while (*c)
                        {
                            g_player[0].wchoice[j] = *c-'0';
                            ud.wchoice[j] = *c;
                            c++;
                            j++;
                        }
                        while (j < 10)
                        {
                            if (j == 9)
                            {
                                g_player[0].wchoice[9] = 1;
                                ud.wchoice[9] = '1';
                            }
                            else
                            {
                                g_player[0].wchoice[j] = 2;
                                ud.wchoice[j] = '2';
                            }

                            j++;
                        }
                    }
                    else
                    {
                        initprintf("Using default weapon orders.\n");
                        g_player[0].wchoice[0] = 3;
                        g_player[0].wchoice[1] = 4;
                        g_player[0].wchoice[2] = 5;
                        g_player[0].wchoice[3] = 7;
                        g_player[0].wchoice[4] = 8;
                        g_player[0].wchoice[5] = 6;
                        g_player[0].wchoice[6] = 0;
                        g_player[0].wchoice[7] = 2;
                        g_player[0].wchoice[8] = 9;
                        g_player[0].wchoice[9] = 1;
                        Bsprintf(ud.wchoice, "3457860291");
                    }
                    break;
                case 'v':
                case 'V':
                    c++;
                    ud.warp_on = 1;
                    ud.m_volume_number = ud.volume_number = atoi(c)-1;
                    break;
                case 'w':
                case 'W':
                    ud.coords = 1;
                    break;
                case 'x':
                case 'X':
                    c++;
                    if (*c)
                    {
                        confilename = c;
                        userconfiles = 1;
                        initprintf("Using CON file '%s'.\n",confilename);
                    }
                    break;
                case 'y':
                case 'Y':
                    c++;
                    ud.fraglimit = atoi(c);
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    ud.warp_on = 2 + (*c) - '0';
                    break;
                case 'z':
                case 'Z':
                    c++;
                    g_scriptDebug = atoi(c);
                    if (!g_scriptDebug)
                        g_scriptDebug = 1;
                    break;
                }
            }
            else
            {
                k = Bstrchr(c,'.');
                if (k)
                {
                    if (!Bstrcasecmp(k,".map"))
                    {
                        CommandMap = argv[i++];
                        continue;
                    }
                    if (!Bstrcasecmp(k,".grp") || !Bstrcasecmp(k,".zip") || !Bstrcasecmp(k,".pk3"))
                    {
                        G_AddGroup(argv[i++]);
                        continue;
                    }
                    if (!Bstrcasecmp(k,".con"))
                    {
                        confilename = const_cast<char*>(argv[i++]);
                        userconfiles = 1;
                        initprintf("Using CON file '%s'.\n",confilename);
                        continue;
                    }
                    if (!Bstrcasecmp(k,".def"))
                    {
                        duke3ddef = argv[i++];
                        initprintf("Using DEF file: %s.\n",duke3ddef);
                        continue;
                    }
                }
            }
            i++;
        }
    }
}

static void G_DisplayLogo(void)
{
    int soundanm = 0;
    int logoflags=Gv_GetVarByLabel("LOGO_FLAGS",255, -1, -1);

    ready2send = 0;

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown(); // JBF

    videoSetViewableArea(0,0,xdim-1,ydim-1);
    videoClearViewableArea(0L);
    IFISSOFTMODE G_FadePalette(0,0,0,63);

    renderFlushPerms();
    videoNextPage();

    Bsprintf(tempbuf,"%s - " APPNAME,duke3dgrpstring);
    wm_setapptitle(tempbuf);

    S_StopMusic();
    FX_StopAllSounds(); // JBF 20031228
    S_ClearSoundLocks();  // JBF 20031228
    if (ud.multimode < 2 && (logoflags & LOGO_ENABLED) && !g_noLogo)
    {
        if (VOLUMEALL && (logoflags & LOGO_PLAYANIM))
        {

            if (!KB_KeyWaiting() && g_noLogoAnim == 0)
            {
                Net_GetPackets();
                G_PlayAnim("logo.anm",5);
                IFISSOFTMODE G_FadePalette(0,0,0,63);
                KB_FlushKeyboardQueue();
                KB_ClearKeysDown(); // JBF
            }

            videoClearViewableArea(0L);
            videoNextPage();
        }

        if (logoflags & LOGO_PLAYMUSIC)
        {
            g_musicIndex = -1; // hack
            S_PlayMusic(&EnvMusicFilename[0][0],MAXVOLUMES*MAXLEVELS);
        }

        if (!NAM)
        {
            fadepal(0,0,0, 0,64,7);
            //g_player[myconnectindex].ps->palette = drealms;
            //G_FadePalette(0,0,0,63);
            if (logoflags & LOGO_3DRSCREEN)
            {
                P_SetGamePalette(g_player[myconnectindex].ps, DREALMSPAL, 11);    // JBF 20040308
                rotatesprite(0,0,65536L,0,DREALMS,0,0,2+8+16+64, 0,0,xdim-1,ydim-1);
                videoNextPage();
                fadepal(0,0,0, 63,0,-7);
                totalclock = 0;
                while (totalclock < (TICRATE*7) && !KB_KeyWaiting() && !MOUSE_GetButtons()&LEFT_MOUSE)
                {
                    gameHandleEvents();
                    if (g_restorePalette)
                    {
                        P_SetGamePalette(g_player[myconnectindex].ps,g_player[myconnectindex].ps->palette,0);
                        g_restorePalette = 0;
                    }
                }
            }
            KB_ClearKeysDown(); // JBF
            MOUSE_ClearButton(LEFT_MOUSE);
        }

        fadepal(0,0,0, 0,64,7);
        videoClearViewableArea(0L);
        videoNextPage();
        if (logoflags & LOGO_TITLESCREEN)
        {
            //g_player[myconnectindex].ps->palette = titlepal;
            P_SetGamePalette(g_player[myconnectindex].ps, TITLEPAL, 11);   // JBF 20040308
            renderFlushPerms();
            rotatesprite(0,0,65536L,0,BETASCREEN,0,0,2+8+16+64,0,0,xdim-1,ydim-1);
            KB_FlushKeyboardQueue();
            fadepal(0,0,0, 63,0,-7);
            totalclock = 0;

            while (totalclock < (860+120) && !KB_KeyWaiting() && !MOUSE_GetButtons()&LEFT_MOUSE)
            {
                rotatesprite(0,0,65536L,0,BETASCREEN,0,0,2+8+16+64,0,0,xdim-1,ydim-1);
                if (logoflags & LOGO_DUKENUKEM)
                {
                    if (totalclock > 120 && totalclock < (120+60))
                    {
                        if (soundanm == 0)
                        {
                            soundanm = 1;
                            S_PlaySound(PIPEBOMB_EXPLODE);
                        }
                        rotatesprite(160<<16,104<<16,((int32_t)totalclock-120)<<10,0,DUKENUKEM,0,0,2+8,0,0,xdim-1,ydim-1);
                    }
                    else if (totalclock >= (120+60))
                        rotatesprite(160<<16,(104)<<16,60<<10,0,DUKENUKEM,0,0,2+8,0,0,xdim-1,ydim-1);
                }
                else soundanm = 1;
                if (logoflags & LOGO_THREEDEE)
                {
                    if (totalclock > 220 && totalclock < (220+30))
                    {
                        if (soundanm == 1)
                        {
                            soundanm = 2;
                            S_PlaySound(PIPEBOMB_EXPLODE);
                        }

                        rotatesprite(160<<16,(104)<<16,60<<10,0,DUKENUKEM,0,0,2+8,0,0,xdim-1,ydim-1);
                        rotatesprite(160<<16,(129)<<16,((int32_t)totalclock - 220)<<11,0,THREEDEE,0,0,2+8,0,0,xdim-1,ydim-1);
                    }
                    else if (totalclock >= (220+30))
                        rotatesprite(160<<16,(129)<<16,30<<11,0,THREEDEE,0,0,2+8,0,0,xdim-1,ydim-1);
                }
                else soundanm = 2;
                if (PLUTOPAK && (logoflags & LOGO_PLUTOPAKSPRITE))
                {
                    // JBF 20030804
                    if (totalclock >= 280 && totalclock < 395)
                    {
                        rotatesprite(160<<16,(151)<<16,(410-(int32_t)totalclock)<<12,0,PLUTOPAKSPRITE+1,0,0,2+8,0,0,xdim-1,ydim-1);
                        if (soundanm == 2)
                        {
                            soundanm = 3;
                            S_PlaySound(FLY_BY);
                        }
                    }
                    else if (totalclock >= 395)
                    {
                        if (soundanm == 3)
                        {
                            soundanm = 4;
                            S_PlaySound(PIPEBOMB_EXPLODE);
                        }
                        rotatesprite(160<<16,(151)<<16,30<<11,0,PLUTOPAKSPRITE+1,0,0,2+8,0,0,xdim-1,ydim-1);
                    }
                }

                X_OnEvent(EVENT_LOGO, -1, screenpeek, -1);

                gameHandleEvents();

                if (g_restorePalette)
                {
                    P_SetGamePalette(g_player[myconnectindex].ps,g_player[myconnectindex].ps->palette,0);
                    g_restorePalette = 0;
                }
                videoNextPage();
            }
        }
        KB_ClearKeysDown(); // JBF
        MOUSE_ClearButton(LEFT_MOUSE);
    }

    waitforeverybody();

    renderFlushPerms();
    videoClearViewableArea(0L);
    videoNextPage();

    //g_player[myconnectindex].ps->palette = palette;
    P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 0);    // JBF 20040308
    S_PlaySound(NITEVISION_ONOFF);

    //G_FadePalette(0,0,0,0);
    videoClearViewableArea(0L);
}

void freehash();
static void G_FreeCONMem(void)
{
    int i;
    extern char *bitptr;

    for (i=(MAXLEVELS*(MAXVOLUMES+1))-1;i>=0;i--) // +1 volume for "intro", "briefing" music
    {
        if (MapInfo[i].name != NULL) Bfree(MapInfo[i].name);
        if (MapInfo[i].filename != NULL) Bfree(MapInfo[i].filename);
        if (MapInfo[i].musicfn != NULL) Bfree(MapInfo[i].musicfn);
        if (MapInfo[i].alt_musicfn != NULL) Bfree(MapInfo[i].alt_musicfn);
        if (MapInfo[i].savedstate != NULL) G_FreeMapState(i);
    }

    for (i=MAXQUOTES-1;i>=0;i--)
    {
        if (ScriptQuotes[i] != NULL) Bfree(ScriptQuotes[i]);
        if (ScriptQuoteRedefinitions[i] != NULL) Bfree(ScriptQuoteRedefinitions[i]);
    }

    for (i=g_gameVarCount-1;i>=0;i--)
    {
        if (aGameVars[i].szLabel != NULL) Bfree(aGameVars[i].szLabel);
        if (aGameVars[i].dwFlags & (GAMEVAR_USER_MASK) && aGameVars[i].val.plValues != NULL)
            Bfree(aGameVars[i].val.plValues);
    }

    for (i=g_gameArrayCount-1;i>=0;i--)
    {
        if (aGameArrays[i].szLabel != NULL) Bfree(aGameArrays[i].szLabel);
        if (aGameArrays[i].plValues != NULL) Bfree(aGameArrays[i].plValues);
    }

    for (i=MAXPLAYERS-1;i>=0;i--)
    {
        if (g_player[i].ps != NULL) Bfree(g_player[i].ps);
        if (g_player[i].inputBits != NULL) Bfree(g_player[i].inputBits);
    }

    for (i=MAXSOUNDS-1;i>=0;i--)
    {
        if (g_sounds[i].filename != NULL) Bfree(g_sounds[i].filename);
        if (g_sounds[i].filename1 != NULL) Bfree(g_sounds[i].filename1);
    }

    if (label != NULL) Bfree(label);
    if (labelcode != NULL) Bfree(labelcode);
    if (script != NULL) Bfree(script);
    if (bitptr != NULL) Bfree(bitptr);

    freehash();
    hash_free(&gamefuncH);
}

/*
===================
=
= ShutDown
=
===================
*/

void G_Shutdown(void)
{
    CONFIG_WriteSetup(0);
    S_SoundShutdown();
    S_MusicShutdown();
    timerUninit();
    CONTROL_Shutdown();
    KB_Shutdown();
    G_FreeCONMem();
#ifdef DISCORC_RPC
    Discord_Shutdown();
#endif
    engineUnInit();
}

/*
===================
=
= G_Startup
=
===================
*/

static void G_CompileScripts(void)
{
    int i, psm = pathsearchmode;
    label     = (char *)&sprite[0]; // V8: 16384*44/64 = 11264  V7: 4096*44/64 = 2816
    labelcode = (intptr_t *)&sector[0]; // V8: 4096*40/4 = 40960    V7: 1024*40/4 = 10240
    labeltype = (intptr_t *)&wall[0];   // V8: 16384*32/4 = 131072  V7: 8192*32/4 = 65536

    Bcorrectfilename(confilename,0);
    // if we compile for a V7 engine wall[] should be used for label names since it's bigger
    pathsearchmode = 1;
    if (userconfiles == 0)
    {
        i = kopen4loadfrommod(confilename,0);
        if (i!=-1)
            kclose(i);
        else Bsprintf(confilename,"GAME.CON");
    }
    C_Compile(confilename);

    if (g_loadFromGroupOnly)
    {
        if (userconfiles == 0)
        {
            i = kopen4loadfrommod("EDUKE.CON",1);
            if (i!=-1)
            {
                Bsprintf(confilename,"EDUKE.CON");
                kclose(i);
            }
            else Bsprintf(confilename,"GAME.CON");
        }
        C_Compile(confilename);
    }

    if ((unsigned int)g_numLabels > MAXSPRITES*sizeof(spritetype)/64)   // see the arithmetic above for why
        G_GameExit("Error: too many labels defined!");
    else
    {
        char *newlabel;
        intptr_t *newlabelcode;

        newlabel     = (char *)malloc(g_numLabels<<6);
        newlabelcode = (intptr_t *)malloc(g_numLabels*sizeof(intptr_t));

        if (!newlabel || !newlabelcode)
        {
            G_GameExit("Error: out of memory retaining labels\n");
        }

        copybuf(label,     newlabel, (g_numLabels*64)/4);
        copybuf(labelcode, newlabelcode, (g_numLabels*sizeof(intptr_t))/4);

        label = newlabel;
        labelcode = newlabelcode;
    }

    Bmemset(sprite, 0, MAXSPRITES * sizeof(spritetype));
    Bmemset(sector, 0, MAXSECTORS * sizeof(sectortype));
    Bmemset(wall, 0, MAXWALLS * sizeof(walltype));

    X_OnEvent(EVENT_INIT, -1, -1, -1);
    pathsearchmode = psm;
}

static void G_CheckGametype(void)
{
    //    initprintf("ud.m_coop=%i before sanitization\n",ud.m_coop);
    ud.m_coop = clamp(ud.m_coop, 0, g_numGametypes-1);
    Bsprintf(tempbuf,"%s\n",GametypeNames[ud.m_coop]);
    initprintf("%s", tempbuf);
    //     initprintf("ud.m_coop=%i after sanitisation\n",ud.m_coop);
}

void G_LoadLookups(void)
{
    int32_t j;
    buildvfs_kfd fp;

    if ((fp = kopen4loadfrommod("lookup.dat", 0)) == buildvfs_kfd_invalid)
        if ((fp = kopen4loadfrommod("lookup.dat", 1)) == buildvfs_kfd_invalid)
            return;

    j = paletteLoadLookupTable(fp);

    if (j < 0)
    {
        if (j == -1)
            initprintf("ERROR loading \"lookup.dat\": failed reading enough data.\n");

        return kclose(fp);
}

    uint8_t paldata[768];

    for (j = 1; j <= 5; j++)
    {
        // Account for TITLE and REALMS swap between basepal number and on-disk order.
        int32_t basepalnum = (j == 3 || j == 4) ? 4 + 3 - j : j;

        if (kread_and_test(fp, paldata, 768))
            return kclose(fp);

        for (unsigned char& k : paldata)
            k <<= 2;

        paletteSetColorTable(basepalnum, paldata);
    }

    kclose(fp);
}

static void G_PostLoadPalette(void)
{
    //if (!(duke3d_globalflags & DUKE3D_NO_PALETTE_CHANGES))
    {
        // Make color index 255 of default/water/slime palette black.
        if (basepaltable[BASEPAL] != NULL)
            Bmemset(&basepaltable[BASEPAL][255 * 3], 0, 3);
        if (basepaltable[WATERPAL] != NULL)
            Bmemset(&basepaltable[WATERPAL][255 * 3], 0, 3);
        if (basepaltable[SLIMEPAL] != NULL)
            Bmemset(&basepaltable[SLIMEPAL][255 * 3], 0, 3);
    }

    //if (!(duke3d_globalflags & DUKE3D_NO_HARDCODED_FOGPALS))
        paletteSetupDefaultFog();

    //if (!(duke3d_globalflags & DUKE3D_NO_PALETTE_CHANGES))
        paletteFixTranslucencyMask();

    palettePostLoadLookups();
}

// Set up new-style multi-psky handling.
void G_InitMultiPsky(int CLOUDYOCEAN__DYN, int MOONSKY1__DYN, int BIGORBIT1__DYN, int LA__DYN)
{
    // When adding other multi-skies, take care that the tileofs[] values are
    // <= PSKYOFF_MAX. (It can be increased up to MAXPSKYTILES, but should be
    // set as tight as possible.)

    // The default sky properties (all others are implicitly zero):
    psky_t* sky = tileSetupSky(DEFAULTPSKY);
    sky->lognumtiles = 3;
    sky->horizfrac = 32768;

    // CLOUDYOCEAN
    // Aligns with the drawn scene horizon because it has one itself.
    sky = tileSetupSky(CLOUDYOCEAN__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac = 65536;

    // MOONSKY1
    //        earth          mountain   mountain         sun
    sky = tileSetupSky(MOONSKY1__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac = 32768;
    sky->tileofs[6] = 1;
    sky->tileofs[1] = 2;
    sky->tileofs[4] = 2;
    sky->tileofs[2] = 3;

    // BIGORBIT1   // orbit
    //       earth1         2           3           moon/sun
    sky = tileSetupSky(BIGORBIT1__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac = 32768;
    sky->tileofs[5] = 1;
    sky->tileofs[6] = 2;
    sky->tileofs[7] = 3;
    sky->tileofs[2] = 4;

    // LA // la city
    //       earth1         2           3           moon/sun
    sky = tileSetupSky(LA__DYN);
    sky->lognumtiles = 3;
    sky->horizfrac = 16384 + 1024;
    sky->tileofs[0] = 1;
    sky->tileofs[1] = 2;
    sky->tileofs[2] = 1;
    sky->tileofs[3] = 3;
    sky->tileofs[4] = 4;
    sky->tileofs[5] = 0;
    sky->tileofs[6] = 2;
    sky->tileofs[7] = 3;

#if 0
    // This assertion should hold. See note above.
    for (bssize_t i = 0; i < pskynummultis; ++i)
        for (bssize_t j = 0; j < (1 << multipsky[i].lognumtiles); ++j)
            Bassert(multipsky[i].tileofs[j] <= PSKYOFF_MAX);
#endif
}

extern int startwin_run(void);
static void G_SetupGameButtons(void);

#define SETFLAG(Tilenum, Flag) SpriteFlags[Tilenum] |= Flag
// Has to be after setting the dynamic names (e.g. SHARK).
static void A_InitEnemyFlags(void)
{
    int DukeEnemies[] = {
        SHARK, RECON, DRONE,
        LIZTROOPONTOILET, LIZTROOPJUSTSIT, LIZTROOPSTAYPUT, LIZTROOPSHOOT,
        LIZTROOPJETPACK, LIZTROOPSHOOT, LIZTROOPDUCKING, LIZTROOPRUNNING, LIZTROOP,
        OCTABRAIN, COMMANDER, COMMANDERSTAYPUT, PIGCOP, PIGCOPSTAYPUT, PIGCOPDIVE, EGG,
        LIZMAN, LIZMANSPITTING, LIZMANJUMP, ORGANTIC,
        BOSS1, BOSS2, BOSS3, BOSS4, RAT, ROTATEGUN };

    int SolidEnemies[] = { TANK, BOSS1, BOSS2, BOSS3, BOSS4, RECON, ROTATEGUN };
    int NoWaterDipEnemies[] = { OCTABRAIN, COMMANDER, DRONE };
    int GreenSlimeFoodEnemies[] = { LIZTROOP, LIZMAN, PIGCOP, NEWBEAST };

    for (bssize_t i = GREENSLIME; i <= GREENSLIME + 7; i++)
        SETFLAG(i, SFLAG_HARDCODED_BADGUY);

    for (bssize_t i = ARRAY_SIZE(DukeEnemies) - 1; i >= 0; i--)
        SETFLAG(DukeEnemies[i], SFLAG_HARDCODED_BADGUY);

    for (bssize_t i = ARRAY_SIZE(SolidEnemies) - 1; i >= 0; i--)
        SETFLAG(SolidEnemies[i], SFLAG_NODAMAGEPUSH);

    for (bssize_t i = ARRAY_SIZE(NoWaterDipEnemies) - 1; i >= 0; i--)
        SETFLAG(NoWaterDipEnemies[i], SFLAG_NOWATERDIP);

    for (bssize_t i = ARRAY_SIZE(GreenSlimeFoodEnemies) - 1; i >= 0; i--)
        SETFLAG(GreenSlimeFoodEnemies[i], SFLAG_GREENSLIMEFOOD);
}
#undef SETFLAG

static void G_PostCreateGameState(void)
{
    A_InitEnemyFlags();
    Net_InitializeStructPointers();
}

static void G_Startup(void)
{
    int i;

    timerInit(TICRATE);
    timerSetCallback(gameTimerHandler);

    G_CompileScripts();

    if (engineInit())
    {
        wm_msgbox("Build Engine Initialization Error",
                  "There was a problem initializing the Build engine: %s", engineerrstr);
        G_FreeCONMem();
        exit(1);
    }

    G_InitDynamicTiles();
    G_InitMultiPsky(CLOUDYOCEAN, MOONSKY1, BIGORBIT1, LA);
    G_PostCreateGameState();

    if (ud.multimode > 1) G_CheckGametype();

    if (g_noSound) ud.config.SoundToggle = 0;
    if (g_noMusic) ud.config.MusicToggle = 0;

    if (CommandName)
    {
        //        Bstrncpy(szPlayerName, CommandName, 9);
        //        szPlayerName[10] = '\0';
        Bstrcpy(tempbuf,CommandName);

        while (Bstrlen(OSD_StripColors(tempbuf,tempbuf)) > 10)
            tempbuf[Bstrlen(tempbuf)-1] = '\0';

        Bstrncpy(szPlayerName,tempbuf,sizeof(szPlayerName)-1);
        szPlayerName[sizeof(szPlayerName)-1] = '\0';
    }

    if (CommandMap)
    {
        if (VOLUMEONE)
        {
            initprintf("The -map option is available in the registered version only!\n");
            boardfilename[0] = 0;
        }
        else
        {
            char *dot, *slash;

            boardfilename[0] = '/';
            boardfilename[1] = 0;
            Bstrcat(boardfilename, CommandMap);

            dot = Bstrrchr(boardfilename,'.');
            slash = Bstrrchr(boardfilename,'/');
            if (!slash) slash = Bstrrchr(boardfilename,'\\');

            if ((!slash && !dot) || (slash && dot < slash))
                Bstrcat(boardfilename,".map");

            Bcorrectfilename(boardfilename,0);

            i = kopen4loadfrommod(boardfilename,0);
            if (i!=-1)
            {
                initprintf("Using level: '%s'.\n",boardfilename);
                kclose(i);
            }
            else
            {
                initprintf("Level '%s' not found.\n",boardfilename);
                boardfilename[0] = 0;
            }
        }
    }

    if (VOLUMEONE)
    {
        initprintf("*** You have run Duke Nukem 3D %d times. ***\n\n",ud.executions);
        if (ud.executions >= 50) initprintf("IT IS NOW TIME TO UPGRADE TO THE COMPLETE VERSION!!!\n");
    }

    for (i=0;i<MAXPLAYERS;i++)
        g_player[i].playerreadyflag = 0;

#if 0
#ifndef RANCID_NETWORKING
    if (CommandNet)
    {
        setup_rancid_net(CommandNet);
        if (Bstrlen(rancid_ip_strings[MAXPLAYERS-1]))
        {
            initprintf("rmnet: Using %s as sort IP\n",rancid_ip_strings[MAXPLAYERS-1]);
            initprintf("rmnet: %d players\n",rancid_players);
        }
        CommandNet = NULL;
    }
#endif
#endif

#ifdef RANCID_NETWORKING
    // TODO: split this up in the same fine-grained manner as eduke32 network backend, to
    // allow for event handling
    mmulti_initmultiplayers(netparamcount,netparam);

    if (quitevent)
    {
        G_Shutdown();
        return;
    }

#else
    if (initmultiplayersparms(netparamcount,netparam))
    {
        initprintf("Waiting for players...\n");
        while (initmultiplayerscycle())
        {
            handleevents();
            if (quitevent)
            {
                G_Shutdown();
                return;
            }
        }
    }
#endif

    if (netparam) Bfree(netparam);
    netparam = NULL;
    netparamcount = 0;

    if (numplayers > 1)
        initprintf("Multiplayer initialized.\n");

    //initprintf("* Hold Esc to Abort. *\n");
//    initprintf("Loading art header...\n");

    {
        char cwd[BMAX_PATH];

        if (getcwd(cwd,BMAX_PATH) && mod_dir[0] != '/')
        {
            chdir(mod_dir);
//            initprintf("root '%s'\nmod '%s'\ncwd '%s'\n",root,mod_dir,cwd);
            if (artLoadFiles("tiles000.art",MAXCACHE1DSIZE) < 0)
            {
                chdir(cwd);
                if (artLoadFiles("tiles000.art",MAXCACHE1DSIZE) < 0)
                    G_GameExit("Failed loading art.");
            }
            chdir(cwd);
        }
        else if (artLoadFiles("tiles000.art",MAXCACHE1DSIZE) < 0)
            G_GameExit("Failed loading art.");
    }

    S_PrecacheSounds();

//    initprintf("Loading palette/lookups...\n");
    G_LoadLookups();

    ReadSaveGameHeaders();

    tilesiz[MIRROR].x = tilesiz[MIRROR].y = 0;

    screenpeek = myconnectindex;

    if (g_networkBroadcastMode == 255)
        g_networkBroadcastMode = NETMODE_P2P;
}

void G_UpdatePlayerFromMenu(void)
{
    if (ud.recstat != 0)
        return;

    if (numplayers > 1)
    {
        Net_SendPlayerOptions();
        if (sprite[g_player[myconnectindex].ps->i].picnum == APLAYER && sprite[g_player[myconnectindex].ps->i].pal != 1)
            sprite[g_player[myconnectindex].ps->i].pal = g_player[myconnectindex].pcolor;
    }
    else
    {
        g_player[myconnectindex].ps->aim_mode = ud.mouseaiming;
        g_player[myconnectindex].ps->auto_aim = ud.config.AutoAim;
        g_player[myconnectindex].ps->weaponswitch = ud.weaponswitch;
        g_player[myconnectindex].ps->palookup = g_player[myconnectindex].pcolor = ud.color;
        g_player[myconnectindex].pteam = ud.team;

        if (sprite[g_player[myconnectindex].ps->i].picnum == APLAYER && sprite[g_player[myconnectindex].ps->i].pal != 1)
            sprite[g_player[myconnectindex].ps->i].pal = g_player[myconnectindex].pcolor;
    }
}

void G_BackToMenu(void)
{
    boardfilename[0] = 0;
    if (ud.recstat == 1) G_CloseDemoWrite();
    ud.warp_on = 0;
    g_player[myconnectindex].ps->gm = MODE_MENU;
    ChangeToMenu(0);
    KB_FlushKeyboardQueue();
    Bsprintf(tempbuf,APPNAME " - %s",duke3dgrpstring);
    wm_setapptitle(tempbuf);
}

void app_crashhandler(void)
{
    G_CloseDemoWrite();
    X_ScriptInfo();
    Net_SendQuit();
}

static void G_FatalEngineInitError(void)
{
#ifdef DEBUGGINGAIDS
    debug_break();
#endif
    //G_Cleanup();
    Bsprintf(tempbuf, "There was a problem initializing the engine: %s\n", engineerrstr);
    ERRprintf("%s", tempbuf);
    //fatal_exit(tempbuf);
    G_Shutdown();
}

static int G_EndOfLevel(void)
{
    auto& p = *g_player[myconnectindex].ps;

    P_SetGamePalette(&p, BASEPAL, 0);
    P_UpdateScreenPal(&p);

    if (p.gm & MODE_EOL)
    {
        G_CloseDemoWrite();
        waitforeverybody();

        ready2send = 0;

        if (ud.display_bonus_screen == 1)
        {
            int const ssize = ud.screen_size;
            ud.screen_size = 0;
            G_UpdateScreenArea();
            ud.screen_size = ssize;
            G_BonusScreen(0);
        }

        // Clear potentially loaded per-map ART only after the bonus screens.
        artClearMapArt();

        if (ud.eog)
        {
            ud.eog = 0;
            if (ud.multimode < 2)
            {
                if (!VOLUMEALL)
                {
                    G_DoOrderScreen();
                }
                p.gm = MODE_MENU;
                ChangeToMenu(0);
                probey = 0;
                return 2;
            }
            else
            {
                ud.m_level_number = 0;
                ud.level_number = 0;
            }
        }
    }

    ud.display_bonus_screen = 1;
    ready2send = 0;

    waitforeverybody();

    if (numplayers > 1)
        p.gm = MODE_GAME;

    if (G_EnterLevel(p.gm))
    {
        G_BackToMenu();
        return 2;
    }

    return 1;
}

static void G_DrawFrame()
{
    // only allow binds to function if the player is actually in a game (not in a menu, typing, et cetera) or demo
    CONTROL_BindsEnabled = (g_player[myconnectindex].ps->gm == MODE_GAME || g_player[myconnectindex].ps->gm == MODE_DEMO);
    G_HandleLocalKeys();
    OSD_DispatchQueued();
   
    //P_GetInput(myconnectindex, true); // [JM] We're not ready for uncapped input yet.

    int smoothRatio = calc_smoothratio((int32_t)totalclock, ototalclock);

    // Necessary anymore?
    if (ud.statusbarmode == 1 && (ud.statusbarscale == 100 || !videoGetRenderMode()))
    {
        ud.statusbarmode = 0;
        G_UpdateScreenArea();
    }

    // Hideous hack to get players to not trail behind in mirrors or look fucky in third person.
    if (numplayers > 1 && (screenpeek == myconnectindex))
    {
        Net_SwapPredictedLinkedLists();
        g_player[myconnectindex].ps = &predictedPlayer;
        auto pSprite = originalPlayer->i;
        auto oldSprite = sprite[pSprite];
        sprite[pSprite] = predicted_sprite[pSprite];

        // And an even worse hack to get slimers to attach to your face.
        // Get rid of this once we have full actor prediction again.
        // And retarget slimer sprite to it's predicted version.
        auto slimerSprite = g_player[myconnectindex].ps->somethingonplayer;
        vec3_t slimerPos;
        int16_t slimerSect;
        if (slimerSprite > -1)
        {
            slimerPos = sprite[slimerSprite].pos;
            slimerSect = sprite[slimerSprite].sectnum;
            auto tData = actor[slimerSprite].temp_data[2];

            vec3_t pos;
            fix16_t q16ang, q16horiz;
            int16_t sectnum;
            G_CalculateCameraInterpolation(&predictedPlayer, &pos, &q16ang, &q16horiz, &sectnum, smoothRatio);

            sprite[slimerSprite].x = pos.x + (sintable[(fix16_to_int(q16ang) + 512) & 2047] >> 7);
            sprite[slimerSprite].y = pos.y + (sintable[fix16_to_int(q16ang) & 2047] >> 7);
            sprite[slimerSprite].z = pos.z - tData + (8 << 8);
            sprite[slimerSprite].z += fix16_to_int(F16(100) - q16horiz) << 4;
            changespritesect(slimerSprite, sectnum);
        }

        G_DrawRooms(screenpeek, smoothRatio);

        if (slimerSprite > -1)
        {
            sprite[slimerSprite].pos = slimerPos;
            changespritesect(slimerSprite, slimerSect);
        }

        g_player[myconnectindex].ps = originalPlayer;
        sprite[pSprite] = oldSprite;
        Net_SwapPredictedLinkedLists();
    }
    else
    {
        G_DrawRooms(screenpeek, smoothRatio);
    }

    if (videoGetRenderMode() >= REND_POLYMOST)
        G_DrawBackground();
    G_DisplayRest(smoothRatio);

    // Shoud this go into DisplayRest?
    // ---------
    if (g_player[myconnectindex].gotvote == 0 && voting != -1 && voting != myconnectindex)
    {
        Bsprintf(tempbuf, "%s^00 HAS CALLED A VOTE FOR MAP", g_player[voting].user_name);
        gametext(160, 40, tempbuf, 0, 2 + 8 + 16);
        Bsprintf(tempbuf, "%s (E%dL%d)", MapInfo[vote_episode * MAXLEVELS + vote_map].name, vote_episode + 1, vote_map + 1);
        gametext(160, 48, tempbuf, 0, 2 + 8 + 16);
        gametext(160, 70, "PRESS F1 TO ACCEPT, F2 TO DECLINE", 0, 2 + 8 + 16);
    }

    if (BUTTON(gamefunc_Show_DukeMatch_Scores))
        G_ShowScores(false);

    if (debug_on)
        G_ShowCacheLocks();

    Net_DisplaySyncMsg();

    if (VOLUMEONE)
    {
        if (ud.show_help == 0 && g_showShareware > 0 && (g_player[myconnectindex].ps->gm & MODE_MENU) == 0)
            rotatesprite((320 - 50) << 16, 9 << 16, 65536L, 0, BETAVERSION, 0, 0, 2 + 8 + 16 + 128, 0, 0, xdim - 1, ydim - 1);
    }
    // ---------

    videoNextPage();
    S_Update();
}

int app_main(int32_t argc, char const* const* argv)
{
    int i = 0, j;
    char cwd[BMAX_PATH];
//    extern char datetimestring[];

#ifdef _WIN32
#ifndef DEBUGGINGAIDS
    if (!G_CheckCmdSwitch(argc, argv, "-noinstancechecking") && !windowsCheckAlreadyRunning())
    {
        if (!wm_ynbox(APPNAME, "It looks like " APPNAME " is already running.\n\n"
            "Are you sure you want to start another copy?"))
            return 3;
    }
#endif
#endif

#ifdef _WIN32
    tempbuf[GetModuleFileName(NULL,root,BMAX_PATH)] = 0;
    Bcorrectfilename(root,1);
    chdir(root);
#else
    getcwd(root,BMAX_PATH);
    strcat(root,"/");
#endif

    OSD_SetLogFile(APPBASENAME ".log");
    OSD_SetParameters(0, 0, 0, 12, 2, 12, OSD_ERROR, OSDTEXT_RED, gamefunctions[gamefunc_Show_Console][0] == '\0' ? OSD_PROTECTED : 0);
    OSD_SetFunctions(
        GAME_drawosdchar,
        GAME_drawosdstr,
        GAME_drawosdcursor,
        GAME_getcolumnwidth,
        GAME_getrowheight,
        GAME_clearbackground,
        (int(*)(void))GetTime,
        GAME_onshowosd
    );
    Bsprintf(tempbuf,HEAD2 " (%s)",s_buildTimestamp);
    wm_setapptitle(tempbuf);

    initprintf("%s\n",apptitle);
//    initprintf("Compiled %s\n",datetimestring);
//    initprintf("Copyright (c) 1996, 2003 3D Realms Entertainment\n");
//    initprintf("Copyright (c) 2008 EDuke32 team and contributors\n");

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    addsearchpath("/usr/share/games/jfduke3d");
    addsearchpath("/usr/local/share/games/jfduke3d");
    addsearchpath("/usr/share/games/eduke32");
    addsearchpath("/usr/local/share/games/eduke32");
#elif defined(__APPLE__)
    addsearchpath("/Library/Application Support/JFDuke3D");
    addsearchpath("/Library/Application Support/EDuke32");
#endif

    ud.multimode = 1;

    initsynccrc();

    enginecompatibilitymode = ENGINE_19961112;

    G_CheckCommandLine(argc,argv);

#if defined(RENDERTYPEWIN) && defined(USE_OPENGL)
    if (forcegl) initprintf("GL driver blacklist disabled.\n");
#endif

    playerColor_init();
    g_player[0].ps = (DukePlayer_t *) Bcalloc(1, sizeof(DukePlayer_t));
    g_player[0].inputBits = (input_t *) Bcalloc(1, sizeof(input_t));
    g_player[0].connected = 1;

    if (getcwd(cwd,BMAX_PATH))
    {
        addsearchpath(cwd);
#if defined(__APPLE__)
        /* Dirty hack on OS X to also look for gamedata inside the application bundle - rhoenie 08/08 */
        char seekinappcontainer[BMAX_PATH];
        Bsnprintf(seekinappcontainer,sizeof(seekinappcontainer),"%s/EDuke32.app/", cwd);
        addsearchpath(seekinappcontainer);
#endif
    }

    if (CommandPaths)
    {
        struct strllist *s;
        while (CommandPaths)
        {
            s = CommandPaths->next;
            addsearchpath(CommandPaths->str);

            free(CommandPaths->str);
            free(CommandPaths);
            CommandPaths = s;
        }
    }

#if defined(_WIN32)
    if (!access("user_profiles_enabled", F_OK))
#else
    if (usecwd == 0 && access("user_profiles_disabled", F_OK))
#endif
    {
        char *homedir;
        int asperr;

        if ((homedir = Bgethomedir()))
        {
            Bsnprintf(cwd,sizeof(cwd),"%s/"
#if defined(_WIN32)
                      "EDuke32 Settings"
#elif defined(__APPLE__)
                      "Library/Application Support/EDuke32"
#else
                      ".eduke32"
#endif
                      ,homedir);
            asperr = addsearchpath(cwd);
            if (asperr == -2)
            {
                if (Bmkdir(cwd,S_IRWXU) == 0) asperr = addsearchpath(cwd);
                else asperr = -1;
            }
            if (asperr == 0)
                chdir(cwd);
            Bfree(homedir);
        }
    }

    hash_init(&gamefuncH);
    for (i=NUMGAMEFUNCTIONS-1;i>=0;i--)
    {
        char *str = Bstrtolower(Bstrdup(gamefunctions[i]));
        hash_add(&gamefuncH,gamefunctions[i],i, 0);
        hash_add(&gamefuncH,str,i, 0);
        Bfree(str);
    }

    i = CONFIG_ReadSetup();
    if (getenv("DUKE3DGRP")) duke3dgrp = getenv("DUKE3DGRP");

#ifdef _WIN32
//    initprintf("build %d\n",(char)atoi(BUILDDATE));
#endif
    if (enginePreInit())
    {
        wm_msgbox("Build Engine Initialization Error",
                  "There was a problem initializing the Build engine: %s", engineerrstr);
        exit(1);
    }

    initprintf("Using config file '%s'.\n",setupfilename);

#ifdef DISCORD_RPC
    InitDiscord();
#endif

    ScanGroups();
    {
        // try and identify the 'defaultduke3dgrp' in the set of GRPs.
        // if it is found, set up the environment accordingly for the game it represents.
        // if it is not found, choose the first GRP from the list of
        struct grpfile *fg, *first = NULL;
        int i;
        for (fg = foundgrps; fg; fg=fg->next)
        {
            for (i = 0; i<numgrpfiles; i++) if (fg->crcval == grpfiles[i].crcval) break;
            if (i == numgrpfiles) continue; // unrecognised grp file
            fg->game = grpfiles[i].game;
            if (!first) first = fg;
            if (!Bstrcasecmp(fg->name, defaultduke3dgrp))
            {
                g_gameType = grpfiles[i].game;
                duke3dgrpstring = grpfiles[i].name;
                break;
            }
        }
        if (!fg && first)
        {
            Bstrcpy(defaultduke3dgrp, first->name);
            g_gameType = first->game;
            duke3dgrpstring = grpfiles[0].name;
        }
        else if (!fg) duke3dgrpstring = "Unknown GRP";
    }

// OLDMP MERGE CHECKME
#ifdef STARTUP_SETUP_WINDOW
    if (i < 0 || (!g_noSetup && (ud.configversion != BYTEVERSION_JF || ud.config.ForceSetup)) || g_commandSetup)
    {
        if (quitevent || !startwin_run())
        {
#ifdef DISCORD_RPC
            Discord_Shutdown();
#endif

            engineUnInit();
            exit(0);
        }
    }
#endif

    FreeGroups();

    if (WW2GI || NAM)
    {
        // overwrite the default GRP and CON so that if the user chooses
        // something different, they get what they asked for
        if (WW2GI)
        {
            Bsprintf(defaultduke3dgrp,"ww2gi.grp");
            Bsprintf(defaultconfilename, "ww2gi.con");
        }
        else
        {
            Bsprintf(defaultduke3dgrp,"nam.grp");
            Bsprintf(defaultconfilename, "nam.con");
        }

        Bsprintf(GametypeNames[0],"GRUNTMATCH (SPAWN)");
        Bsprintf(GametypeNames[2],"GRUNTMATCH (NO SPAWN)");
    }

    if (mod_dir[0] != '/')
    {
        Bstrcat(root,mod_dir);
        addsearchpath(root);
//        addsearchpath(mod_dir);
#if defined(USE_OPENGL)
        Bsprintf(tempbuf,"%s/%s",mod_dir,TEXCACHEFILE);
        Bstrcpy(TEXCACHEFILE,tempbuf);
#endif
    }

    i = initgroupfile(eduke32dat);

    if (i == -1)
        initprintf("Warning: could not find internal data file '%s'!\n", eduke32dat);
    else 
        initprintf("Using '%s' for internal data.\n", eduke32dat);

    i = -1;

    i = initgroupfile(duke3dgrp);

    if (i == -1)
        initprintf("Warning: could not find group file '%s'!\n",duke3dgrp);
    else
        initprintf("Using group file '%s' as main group file.\n", duke3dgrp);

    if (!g_noAutoLoad)
    {
        int ii;

        for (ii=0;ii<NUMAUTOLOADMASKS;ii++)
        {
            getfilenames("autoload",autoloadmasks[ii]);
            while (fnlist.findfiles)
            {
                Bsprintf(tempbuf,"autoload/%s", fnlist.findfiles->name);
                initprintf("Using group file '%s'.\n",tempbuf);
                initgroupfile(tempbuf);
                fnlist.findfiles = fnlist.findfiles->next;
            }
        }

        if (i != -1)
            G_DoAutoload(duke3dgrp);
    }

    if (mod_dir[0] != '/')
    {
        int ii;

        for (ii=0;ii<NUMAUTOLOADMASKS;ii++)
        {
            Bsprintf(tempbuf,"%s/",mod_dir);
            getfilenames(tempbuf,autoloadmasks[ii]);
            while (fnlist.findfiles)
            {
                Bsprintf(tempbuf,"%s/%s",mod_dir, fnlist.findfiles->name);
                initprintf("Using group file '%s'.\n",tempbuf);
                initgroupfile(tempbuf);
                fnlist.findfiles = fnlist.findfiles->next;
            }
        }
    }

    //loaddefinitions_game(eduke32def, TRUE);
    loaddefinitions_game(duke3ddef, TRUE);

    {
        struct strllist *s;

        pathsearchmode = 1;
        while (CommandGrps)
        {
            s = CommandGrps->next;
            j = initgroupfile(CommandGrps->str);
            if (j == -1) initprintf("Could not find group file '%s'.\n",CommandGrps->str);
            else
            {
                g_groupFileHandle = j;
                initprintf("Using group file '%s'.\n",CommandGrps->str);
                if (!g_noAutoLoad)
                    G_DoAutoload(CommandGrps->str);
            }

            free(CommandGrps->str);
            free(CommandGrps);
            CommandGrps = s;
        }
        pathsearchmode = 0;
    }

    i = kopen4load("DUKESW.BIN",1); // JBF 20030810
    if (i!=-1)
    {
        g_Shareware = 1;
        kclose(i);
    }

#if 0
    copyprotect();
    if (cp) return;
#endif

#if 0 // OLDMP MERGE CHECKME
    if (netparamcount > 0) _buildargc = (argc -= netparamcount+1);  // crop off the net parameters
#endif

    // gotta set the proper title after we compile the CONs if this is the full version

    Bsprintf(tempbuf,"%s - " APPNAME,duke3dgrpstring);
    wm_setapptitle(tempbuf);


//    initprintf("\n");

    if (g_scriptDebug)
        initprintf("CON debugging activated (level %d).\n",g_scriptDebug);

    if (VOLUMEONE)
    {
        initprintf("Distribution of shareware Duke Nukem 3D is restricted in certain ways.\n");
        initprintf("Please read LICENSE.DOC for more details.\n");
    }

    G_Startup(); // a bunch of stuff including compiling cons

    ud.color = playerColor_getValidPal(ud.m_color);
    g_player[0].ps->palookup = g_player[0].pcolor = ud.color;

    if (numplayers > 1)
        ud.multimode = numplayers;
    
    for (i=1;i<ud.multimode;i++)
    {
        g_player[i].ps = (DukePlayer_t *) Bcalloc(1,sizeof(DukePlayer_t));
        g_player[i].inputBits = (input_t *) Bcalloc(1,sizeof(input_t));
        g_player[i].connected = 1;

        // Propagate initial settings for player to all.
        memcpy(g_player[i].ps, g_player[0].ps, sizeof(DukePlayer_t));
    }

    auto& myplayer = *g_player[myconnectindex].ps;
    myplayer.palette = BASEPAL;

    i = 1;
    for (j=numplayers;j<ud.multimode;j++)
    {
        Bsprintf(g_player[j].user_name,"PLAYER %d",j+1);
        g_player[j].ps->team = g_player[j].pteam = i;
        g_player[j].ps->weaponswitch = 3;
        g_player[j].ps->auto_aim = 0;
        i = 1-i;
    }

    if (quitevent) return 4;
    if (!loaddefinitionsfile(eduke32def))
    {
        initprintf("Internal def file '%s' loaded.\n", eduke32def);
        loaddefinitions_game(eduke32def, FALSE);
    }

    if (!loaddefinitionsfile(duke3ddef))
    {
        initprintf("Definitions file '%s' loaded.\n",duke3ddef);
        loaddefinitions_game(duke3ddef, FALSE);
    }
    //     initprintf("numplayers=%i\n",numplayers);

    if (enginePostInit())
        G_FatalEngineInitError();

    G_PostLoadPalette();

    Net_SendVersion();
    Net_SendPlayerName();

    if (numplayers > 1)
    {
        mmulti_sendlogon(); // Does nothing
        Net_SendPlayerOptions();
        Net_SendWeaponChoice();
        Net_SendUserMapName();
        Net_SendFragLimit();
        Net_SendInitialSettings();
        Net_GetPackets();
        waitforeverybody();
    }
    else if (boardfilename[0] != 0)
    {
        ud.m_level_number = 7;
        ud.m_volume_number = 0;
        ud.warp_on = 1;
    }

    // getnames();

    if (ud.multimode > 1)
        playerswhenstarted = ud.multimode;

    ud.last_level = -1;

    if (Bstrcasecmp(ud.rtsname,"DUKE.RTS") == 0 ||
            Bstrcasecmp(ud.rtsname,"WW2GI.RTS") == 0 ||
            Bstrcasecmp(ud.rtsname,"NAM.RTS") == 0)
    {
        // ud.last_level is used as a flag here to reset the string to DUKE.RTS after load
        if (WW2GI)
        {
            ud.last_level = 1;
            Bstrcpy(ud.rtsname, "WW2GI.RTS");
        }
        else if (NAM)
        {
            ud.last_level = 1;
            Bstrcpy(ud.rtsname, "NAM.RTS");
        }
        else
        {
            ud.last_level = 1;
            Bstrcpy(ud.rtsname, "DUKE.RTS");
        }
    }

    // [JM] I'll need to make sure this doesn't fuck up a few command lines.
    CONFIG_WriteSetup(1);
    CONFIG_ReadSetup();

    RTS_Init(ud.rtsname);
    if (rts_numlumps) initprintf("Using .RTS file '%s'\n",ud.rtsname);

    if (ud.last_level == 1)
    {
        ud.last_level = -1;
        Bstrcpy(ud.rtsname, "DUKE.RTS");
    }

    initprintf("Initializing OSD...\n");

    Bsprintf(tempbuf,HEAD2 " - %s (%s)",s_buildRev, s_buildTimestamp);
    OSD_SetVersion(tempbuf, 10, 0);
    registerosdcommands();

    if (CONTROL_Startup(controltype_keyboardandmouse, &GetTime, TICRATE))
    {
#ifdef DISCORD_RPC
        Discord_Shutdown();
#endif
        engineUnInit();
        exit(1);
    }
    G_SetupGameButtons();
    CONFIG_SetupMouse();
    CONFIG_SetupJoystick();

    CONTROL_JoystickEnabled = (ud.config.UseJoystick && CONTROL_JoyPresent);
    CONTROL_MouseEnabled = (ud.config.UseMouse && CONTROL_MousePresent);

    // JBF 20040215: evil and nasty place to do this, but joysticks are evil and nasty too
    for (i=0; i < joystick.numAxes; i++)
        joySetDeadZone(i,ud.config.JoystickAnalogueDead[i],ud.config.JoystickAnalogueSaturate[i]);

    if (videoSetGameMode(ud.config.ScreenMode,ud.config.ScreenWidth,ud.config.ScreenHeight,ud.config.ScreenBPP, ud.detail) < 0)
    {
        int i = 0;
        int xres[] = {ud.config.ScreenWidth,800,640,320};
        int yres[] = {ud.config.ScreenHeight,600,480,240};
        int bpp[] = {32,16,8};

        initprintf("Failure setting video mode %dx%dx%d %s! Attempting safer mode...\n",
                   ud.config.ScreenWidth,ud.config.ScreenHeight,ud.config.ScreenBPP,ud.config.ScreenMode?"fullscreen":"windowed");

#if defined(USE_OPENGL)
        {
            int j = 0;
            while (videoSetGameMode(0,xres[i],yres[i],bpp[j], ud.detail) < 0)
            {
                initprintf("Failure setting video mode %dx%dx%d windowed! Attempting safer mode...\n",xres[i],yres[i],bpp[i]);
                j++;
                if (j == 3)
                {
                    i++;
                    j = 0;
                }
                if (i == 4)
                    G_GameExit("Unable to set failsafe video mode!");
            }
        }
#else
        while (videoSetGameMode(0,xres[i],yres[i],8) < 0)
        {
            initprintf("Failure setting video mode %dx%dx%d windowed! Attempting safer mode...\n",xres[i],yres[i],8);
            i++;
        }
#endif
        ud.config.ScreenWidth = xres[i];
        ud.config.ScreenHeight = yres[i];
        ud.config.ScreenBPP = bpp[i];
    }

    videoSetPalette(ud.brightness >> 2, g_player[myconnectindex].ps->palette, 0);

    CONFIG_ReadSettings();
    OSD_Exec("autoexec.cfg");

    CONFIG_SetDefaultKeys(keydefaults, true);

    system_getcvars();

    initprintf("Initializing music...\n");
    S_MusicStartup();
    initprintf("Initializing sound...\n");
    S_SoundStartup();

    FX_StopAllSounds();
    S_ClearSoundLocks();

    if (ud.warp_on > 1 && ud.multimode < 2)
    {
        videoClearViewableArea(0L);
        //g_player[myconnectindex].ps->palette = palette;
        //G_FadePalette(0,0,0,0);
        P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 0);    // JBF 20040308
        rotatesprite(320<<15,200<<15,65536L,0,LOADSCREEN,0,0,2+8+64,0,0,xdim-1,ydim-1);
        menutext(160,105,0,0,"LOADING SAVED GAME...");
        videoNextPage();

        if (G_LoadPlayer(ud.warp_on-2))
            ud.warp_on = 0;
    }

MAIN_LOOP_RESTART:
    totalclock = 0;
    ototalclock = 0;
    lockclock = 0;

    G_GetCrosshairColor();
    G_SetCrosshairColor(CrosshairColors.r, CrosshairColors.g, CrosshairColors.b);

    if (ud.warp_on == 0)
    {
        if (ud.multimode > 1 && boardfilename[0] != 0)
        {
            ud.m_level_number = 7;
            ud.m_volume_number = 0;

            waitforeverybody();

            TRAVERSE_CONNECT(i)
            {
                P_ResetWeapons(i);
                P_ResetInventory(i);
            }

            G_NewGame(ud.m_volume_number,ud.m_level_number,ud.m_player_skill);

            if (G_EnterLevel(MODE_GAME)) G_BackToMenu();
        }
        else G_DisplayLogo();
    }
    else if (ud.warp_on == 1)
    {
        G_NewGame(ud.m_volume_number,ud.m_level_number,ud.m_player_skill);

        if (G_EnterLevel(MODE_GAME)) G_BackToMenu();
    }
    else G_UpdateScreenArea();

    if (ud.warp_on == 0 && G_PlaybackDemo())
    {
        FX_StopAllSounds();
        S_ClearSoundLocks();
        g_noLogoAnim = 1;
        goto MAIN_LOOP_RESTART;
    }

    myplayer.aim_mode = ud.mouseaiming;
    myplayer.auto_aim = ud.config.AutoAim;
    myplayer.weaponswitch = ud.weaponswitch;
    g_player[myconnectindex].pteam = ud.team;

    if (GametypeFlags[ud.coop] & GAMETYPE_TDM)
        myplayer.palookup = g_player[myconnectindex].pcolor = teams[g_player[myconnectindex].pteam].palnum;
    else
    {
        if (ud.color) myplayer.palookup = g_player[myconnectindex].pcolor = playerColor_getValidPal(ud.color);
        else myplayer.palookup = g_player[myconnectindex].pcolor;
    }

    ud.warp_on = 0;
    KB_KeyDown[sc_Pause] = 0;   // JBF: I hate the pause key

    do // main loop
    {
        if (gameHandleEvents() && quitevent)
        {
            // JBF
            KB_KeyDown[sc_Escape] = 1;
            quitevent = 0;
        }

        static bool frameJustDrawn;
        //bool gameUpdate = false;
        //double gameUpdateStartTime = timerGetHiTicks();

        if (((ud.multimode > 1) || (myplayer.gm & (MODE_MENU | MODE_DEMO)) == 0) && (int32_t)(totalclock - ototalclock) >= TICSPERFRAME)
        {
            do
            {
                if (!frameJustDrawn)
                    break;

                frameJustDrawn = false;
                P_GetInput(myconnectindex, false);
                netInput = localInput;
                localInput = {}; // Input has been processed, flush.
                
                // The while condition in this loop is a duplicate of the if condition above. Why?
                // Why not check for readytosend in the above "if" and cut this out?
                do
                {
                    if (ready2send == 0)
                        break;

                    ototalclock += TICSPERFRAME;

                    Net_HandleInput();

                    if (((ud.show_help == 0 && (myplayer.gm & MODE_MENU) != MODE_MENU) || ud.recstat == 2 || (ud.multimode > 1))
                        && (myplayer.gm & MODE_GAME))
                    {
                        G_MoveLoop();
                    }
                } while (((ud.multimode > 1) || (myplayer.gm & (MODE_MENU | MODE_DEMO)) == 0) && (int32_t)(totalclock - ototalclock) >= TICSPERFRAME + 1);

                //gameUpdate = true;
                //g_gameUpdateTime = timerGetHiTicks() - gameUpdateStartTime;

                //if (g_gameUpdateAvgTime <= 0.0)
                //    g_gameUpdateAvgTime = g_gameUpdateTime;

                //g_gameUpdateAvgTime = ((GAMEUPDATEAVGTIMENUMSAMPLES - 1.f) * g_gameUpdateAvgTime + g_gameUpdateTime) / ((float)GAMEUPDATEAVGTIMENUMSAMPLES);
            } while (0);
        }

        G_DoCheats();

        if (myplayer.gm & (MODE_EOL | MODE_RESTART))
        {
            switch (G_EndOfLevel())
            {
            case 1: continue;
            case 2: goto MAIN_LOOP_RESTART;
            }
        }

        if (engineFPSLimit())
        {
            G_DrawFrame();

            //if (gameUpdate)
            //    g_gameUpdateAndDrawTime = timerGetHiTicks() - gameUpdateStartTime;

            frameJustDrawn = true;
        }

        // KEEP THIS AT THE END OF THIS WHILE LOOP
        if (myplayer.gm & MODE_DEMO)
            goto MAIN_LOOP_RESTART;
    } while (1);

    G_GameExit(" ");

    return 0;
}

static int demo_version;

static int G_OpenDemoRead(int g_whichDemo) // 0 = mine
{
    char d[13];
    char ver;
    int i;

    Bstrcpy(d, "demo_.dmo");

    if (g_whichDemo == 10)
        d[4] = 'x';
    else
        d[4] = '0' + g_whichDemo;

    ud.reccnt = 0;

    if (g_whichDemo == 1 && firstdemofile[0] != 0)
    {
        if ((recfilep = kopen4loadfrommod(firstdemofile,g_loadFromGroupOnly)) == -1) return(0);
    }
    else
        if ((recfilep = kopen4loadfrommod(d,g_loadFromGroupOnly)) == -1) return(0);

    if (kread(recfilep,&ud.reccnt,sizeof(int)) != sizeof(int)) goto corrupt;
    if (kread(recfilep,&ver,sizeof(char)) != sizeof(char)) goto corrupt;

    if (ver != BYTEVERSION /*&& ver != 116 && ver != 117*/)
    {
        /* old demo playback */
        if (ver == BYTEVERSION_JF)   initprintf("Demo %s is for Regular edition.\n", d);
        else if (ver == BYTEVERSION_JF+1) initprintf("Demo %s is for Atomic edition.\n", d);
        else if (ver == BYTEVERSION_JF+2) initprintf("Demo %s is for Shareware version.\n", d);
//        else OSD_Printf("Demo %s is of an incompatible version (%d).\n", d, ver);
        kclose(recfilep);
        ud.reccnt=0;
        demo_version = 0;
        return 0;
    }
    else
    {
        demo_version = ver;
        OSD_Printf("Demo %s is of version %d.\n", d, ver);
    }

    if (kread(recfilep,(char *)&ud.volume_number,sizeof(char)) != sizeof(char)) goto corrupt;
    OSD_Printf("ud.volume_number: %d\n",ud.volume_number);
    if (kread(recfilep,(char *)&ud.level_number,sizeof(char)) != sizeof(char)) goto corrupt;
    OSD_Printf("ud.level_number: %d\n",ud.level_number);
    if (kread(recfilep,(char *)&ud.player_skill,sizeof(char)) != sizeof(char)) goto corrupt;
    OSD_Printf("ud.player_skill: %d\n",ud.player_skill);
    if (kread(recfilep,(char *)&ud.m_coop,sizeof(char)) != sizeof(char)) goto corrupt;
    OSD_Printf("ud.m_coop: %d\n",ud.m_coop);
    if (kread(recfilep,(char *)&ud.m_ffire,sizeof(char)) != sizeof(char)) goto corrupt;
    OSD_Printf("ud.m_ffire: %d\n",ud.m_ffire);
    if (kread(recfilep,(short *)&ud.multimode,sizeof(short)) != sizeof(short)) goto corrupt;
    OSD_Printf("ud.multimode: %d\n",ud.multimode);
    if (kread(recfilep,(short *)&ud.m_monsters_off,sizeof(short)) != sizeof(short)) goto corrupt;
    OSD_Printf("ud.m_monsters_off: %d\n",ud.m_monsters_off);
    if (kread(recfilep,(int *)&ud.m_respawn_monsters,sizeof(int)) != sizeof(int)) goto corrupt;
    OSD_Printf("ud.m_respawn_monsters: %d\n",ud.m_respawn_monsters);
    if (kread(recfilep,(int *)&ud.m_respawn_items,sizeof(int)) != sizeof(int)) goto corrupt;
    OSD_Printf("ud.m_respawn_items: %d\n",ud.m_respawn_items);
    if (kread(recfilep,(int *)&ud.m_respawn_inventory,sizeof(int)) != sizeof(int)) goto corrupt;
    OSD_Printf("ud.m_respawn_inventory: %d\n",ud.m_respawn_inventory);
    if (kread(recfilep,(int *)&ud.playerai,sizeof(int)) != sizeof(int)) goto corrupt;
    OSD_Printf("ud.playerai: %d\n",ud.playerai);
    if (kread(recfilep,(int *)&i,sizeof(int)) != sizeof(int)) goto corrupt;

    if (kread(recfilep,(char *)boardfilename,sizeof(boardfilename)) != sizeof(boardfilename)) goto corrupt;

    if (boardfilename[0] != 0)
    {
        ud.m_level_number = 7;
        ud.m_volume_number = 0;
    }

    if (kread(recfilep,(int *)&ud.m_noexits,sizeof(int)) != sizeof(int)) goto corrupt;

    for (i=0;i<ud.multimode;i++)
    {
        if (!g_player[i].ps)
            g_player[i].ps = (DukePlayer_t *) Bcalloc(1,sizeof(DukePlayer_t));
        if (!g_player[i].inputBits)
            g_player[i].inputBits = (input_t *) Bcalloc(1,sizeof(input_t));

        if (kread(recfilep,(char *)g_player[i].user_name,sizeof(g_player[i].user_name)) != sizeof(g_player[i].user_name)) goto corrupt;
        OSD_Printf("ud.user_name: %s\n",g_player[i].user_name);
        if (kread(recfilep,(int *)&g_player[i].ps->aim_mode,sizeof(int)) != sizeof(int)) goto corrupt;
        if (kread(recfilep,(int *)&g_player[i].ps->auto_aim,sizeof(int)) != sizeof(int)) goto corrupt;  // JBF 20031126
        if (kread(recfilep,(int *)&g_player[i].ps->weaponswitch,sizeof(int)) != sizeof(int)) goto corrupt;
        if (kread(recfilep,(int *)&g_player[i].pcolor,sizeof(int)) != sizeof(int)) goto corrupt;
        g_player[i].ps->palookup = g_player[i].pcolor;
        if (kread(recfilep,(int *)&g_player[i].pteam,sizeof(int)) != sizeof(int)) goto corrupt;
        g_player[i].ps->team = g_player[i].pteam;
    }
    i = ud.reccnt/((TICRATE/TICSPERFRAME)*ud.multimode);
    OSD_Printf("demo duration: %d min %d sec\n", i/60, i%60);

    ud.god = ud.cashman = ud.eog = ud.showallmap = 0;
    ud.clipping = ud.scrollmode = ud.overhead_on = ud.pause_on = 0;

    G_NewGame(ud.volume_number,ud.level_number,ud.player_skill);
    return(1);
corrupt:
    OSD_Printf(OSD_ERROR "Demo %d header is corrupt.\n",g_whichDemo);
    ud.reccnt = 0;
    kclose(recfilep);
    return 0;
}

void G_OpenDemoWrite(void)
{
    char d[13];
    int dummylong = 0, demonum=1;
    char ver;
    short i;

    if (ud.recstat == 2) kclose(recfilep);

    ver = BYTEVERSION;

    while (1)
    {
        if (demonum == 10000) return;
        Bsprintf(d, "demo%d.dmo", demonum++);
        frecfilep = fopen(d, "rb");
        if (frecfilep == NULL) break;
        Bfclose(frecfilep);
    }

    if ((frecfilep = fopen(d,"wb")) == NULL) return;
    fwrite(&dummylong,4,1,frecfilep);
    fwrite(&ver,sizeof(char),1,frecfilep);
    fwrite((char *)&ud.volume_number,sizeof(char),1,frecfilep);
    fwrite((char *)&ud.level_number,sizeof(char),1,frecfilep);
    fwrite((char *)&ud.player_skill,sizeof(char),1,frecfilep);
    fwrite((char *)&ud.m_coop,sizeof(char),1,frecfilep);
    fwrite((char *)&ud.m_ffire,sizeof(char),1,frecfilep);
    fwrite((short *)&ud.multimode,sizeof(short),1,frecfilep);
    fwrite((short *)&ud.m_monsters_off,sizeof(short),1,frecfilep);
    fwrite((int *)&ud.m_respawn_monsters,sizeof(int),1,frecfilep);
    fwrite((int *)&ud.m_respawn_items,sizeof(int),1,frecfilep);
    fwrite((int *)&ud.m_respawn_inventory,sizeof(int),1,frecfilep);
    fwrite((int *)&ud.playerai,sizeof(int),1,frecfilep);
    fwrite((int *)&ud.auto_run,sizeof(int),1,frecfilep);
    fwrite((char *)boardfilename,sizeof(boardfilename),1,frecfilep);
    fwrite((int *)&ud.m_noexits,sizeof(int),1,frecfilep);

    for (i=0;i<ud.multimode;i++)
    {
        fwrite((char *)&g_player[i].user_name,sizeof(g_player[i].user_name),1,frecfilep);
        fwrite((int *)&g_player[i].ps->aim_mode,sizeof(int),1,frecfilep);
        fwrite((int *)&g_player[i].ps->auto_aim,sizeof(int),1,frecfilep);       // JBF 20031126
        fwrite(&g_player[i].ps->weaponswitch,sizeof(int),1,frecfilep);
        fwrite(&g_player[i].pcolor,sizeof(int),1,frecfilep);
        fwrite(&g_player[i].pteam,sizeof(int),1,frecfilep);
    }

    totalreccnt = 0;
    ud.reccnt = 0;
}

static void G_DemoRecord(void)
{
    short i;

    TRAVERSE_CONNECT(i)
    {
        copybufbyte(g_player[i].inputBits,&recsync[ud.reccnt],sizeof(input_t));
        ud.reccnt++;
        totalreccnt++;
        if (ud.reccnt >= RECSYNCBUFSIZ)
        {
            dfwrite(recsync,sizeof(input_t)*ud.multimode,ud.reccnt/ud.multimode,frecfilep);
            ud.reccnt = 0;
        }
    }
}

void G_CloseDemoWrite(void)
{
    if (ud.recstat == 1)
    {
        if (ud.reccnt > 0)
        {
            dfwrite(recsync,sizeof(input_t)*ud.multimode,ud.reccnt/ud.multimode,frecfilep);

            fseek(frecfilep,SEEK_SET,0L);
            fwrite(&totalreccnt,sizeof(int),1,frecfilep);
            ud.recstat = ud.m_recstat = 0;
        }
        fclose(frecfilep);
    }
}

static int g_whichDemo = 1;

// extern int syncs[];
static int G_PlaybackDemo(void)
{
    int i,j,l;
    int foundemo = 0;
    static int in_menu = 0;

    if (ready2send) return 0;

RECHECK:

    in_menu = g_player[myconnectindex].ps->gm&MODE_MENU;

    pub = NUMPAGES;
    pus = NUMPAGES;

    renderFlushPerms();

    if (ud.multimode < 2) foundemo = G_OpenDemoRead(g_whichDemo);

    if (foundemo == 0)
    {
        if (g_whichDemo > 1)
        {
            g_whichDemo = 1;
            goto RECHECK;
        }
        fadepal(0,0,0, 0,63,7);
        P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 1);    // JBF 20040308
        G_DrawBackground();
        M_DisplayMenus();
        //g_player[myconnectindex].ps->palette = palette;
        videoNextPage();
        fadepal(0,0,0, 63,0,-7);
        ud.reccnt = 0;
    }
    else
    {
        ud.recstat = 2;
        g_whichDemo++;
        if (g_whichDemo == 10) g_whichDemo = 1;
        if (G_EnterLevel(MODE_DEMO)) ud.recstat = foundemo = 0;
    }

    if (foundemo == 0 || in_menu || KB_KeyWaiting() || numplayers > 1)
    {
        FX_StopAllSounds();
        S_ClearSoundLocks();
        g_player[myconnectindex].ps->gm |= MODE_MENU;
    }

    ready2send = 0;
    i = 0;

    KB_FlushKeyboardQueue();

    while (ud.reccnt > 0 || foundemo == 0)
    {
        if (foundemo)
            while (totalclock >= (lockclock+TICSPERFRAME))
            {
                if ((i == 0) || (i >= RECSYNCBUFSIZ))
                {
                    i = 0;
                    l = min(ud.reccnt,RECSYNCBUFSIZ);
                    if (kdfread(recsync,sizeof(input_t)*ud.multimode,l/ud.multimode,recfilep) != l/ud.multimode)
                    {
                        OSD_Printf(OSD_ERROR "Demo %d is corrupt.\n", g_whichDemo-1);
                        foundemo = 0;
                        ud.reccnt = 0;
                        kclose(recfilep);
                        g_player[myconnectindex].ps->gm |= MODE_MENU;
                        goto RECHECK;
                    }
                }

                TRAVERSE_CONNECT(j)
                {
                    copybufbyte(&recsync[i],&inputfifo[g_player[j].movefifoend&(MOVEFIFOSIZ-1)][j],sizeof(input_t));
                    g_player[j].movefifoend++;
                    i++;
                    ud.reccnt--;
                }
                G_DoMoveThings();
            }

        if (foundemo == 0)
            G_DrawBackground();
        else
        {
            G_HandleLocalKeys();

            int smoothRatio = calc_smoothratio((int32_t)totalclock, lockclock);
            G_DrawRooms(screenpeek,smoothRatio);
            G_DisplayRest(smoothRatio);

            if (ud.multimode > 1 && g_player[myconnectindex].ps->gm)
                Net_GetPackets();

            if (g_player[myconnectindex].gotvote == 0 && voting != -1 && voting != myconnectindex)
                gametext(160,60,"PRESS F1 TO ACCEPT, F2 TO DECLINE",0,2+8+16);
        }

        if ((g_player[myconnectindex].ps->gm&MODE_MENU) && (g_player[myconnectindex].ps->gm&MODE_EOL))
            goto RECHECK;

        if (KB_KeyPressed(sc_Escape) && (g_player[myconnectindex].ps->gm&MODE_MENU) == 0 && (g_player[myconnectindex].ps->gm&MODE_TYPE) == 0)
        {
            KB_ClearKeyDown(sc_Escape);
            FX_StopAllSounds();
            S_ClearSoundLocks();
            g_player[myconnectindex].ps->gm |= MODE_MENU;
            ChangeToMenu(0);
            S_MenuSound();
        }

        if (g_player[myconnectindex].ps->gm&MODE_TYPE)
        {
            Net_EnterMessage();
            if ((g_player[myconnectindex].ps->gm&MODE_TYPE) != MODE_TYPE)
                g_player[myconnectindex].ps->gm = MODE_MENU;
        }
        else
        {
            if (ud.recstat != 2)
                M_DisplayMenus();
            if (ud.multimode > 1  && g_currentMenu != 20003 && g_currentMenu != 20005 && g_currentMenu != 210)
            {
                ControlInfo noshareinfo;
                CONTROL_GetInput(&noshareinfo);
                if (BUTTON(gamefunc_SendMessage))
                {
                    KB_FlushKeyboardQueue();
                    CONTROL_ClearButton(gamefunc_SendMessage);
                    g_player[myconnectindex].ps->gm = MODE_TYPE;
                    typebuf[0] = 0;
                    inputloc = 0;
                }
            }
        }

        G_PrintGameQuotes();

        if (ud.last_camsprite != ud.camerasprite)
        {
            ud.last_camsprite = ud.camerasprite;
            ud.camera_time = (int32_t)totalclock+(TICRATE*2);
        }

        if (VOLUMEONE)
        {
            if (ud.show_help == 0 && (g_player[myconnectindex].ps->gm&MODE_MENU) == 0)
                rotatesprite((320-50)<<16,9<<16,65536L,0,BETAVERSION,0,0,2+8+16+128,0,0,xdim-1,ydim-1);
        }

        gameHandleEvents();
        videoNextPage();

        if (g_player[myconnectindex].ps->gm==MODE_END || g_player[myconnectindex].ps->gm==MODE_GAME)
        {
            if (foundemo)
                kclose(recfilep);
            return 0;
        }
    }
    ud.multimode = numplayers;  // fixes 2 infinite loops after watching demo
    kclose(recfilep);

#if 0
    {
        unsigned int crcv;
        // sync checker
        +       initcrc32table();
        crc32init(&crcv);
        crc32block(&crcv, (unsigned char *)wall, sizeof(wall));
        crc32block(&crcv, (unsigned char *)sector, sizeof(sector));
        crc32block(&crcv, (unsigned char *)sprite, sizeof(sprite));
        crc32finish(&crcv);
        initprintf("Checksum = %08X\n",crcv);
    }
#endif

    if (g_player[myconnectindex].ps->gm&MODE_MENU) goto RECHECK;
    return 1;
}

static int G_MoveLoop()
{
    int i;

    if (numplayers > 1)
    {
        Net_DoPrediction(PREDICTSTATE_PROCESS);
    }

    if (numplayers < 2)
        bufferjitter = 0;

    while (g_player[myconnectindex].movefifoend-movefifoplc > bufferjitter)
    {
        TRAVERSE_CONNECT(i)
            if ((movefifoplc == g_player[i].movefifoend) && !g_gameQuit)
                break;

        //i is >= 0 only if last loop was broken, meaning a player wasn't caught up
        if (i >= 0)
            break;

        if (G_DoMoveThings())
            return 1;
    }

    return 0;
}

static int G_DoMoveThings(void)
{
    int i, j;
//    char ch;

    TRAVERSE_CONNECT(i)
    if (TEST_SYNC_KEY(g_player[i].inputBits->bits, SK_MULTIFLAG))
    {
        multiflag = 2;
        multiwhat = (g_player[i].inputBits->bits>>(SK_MULTIFLAG+1))&1;
        multipos = (unsigned)(g_player[i].inputBits->bits>>(SK_MULTIFLAG+2))&15;
        multiwho = i;

        if (multiwhat)
        {
            G_SavePlayer(multipos);
            multiflag = 0;

            if (multiwho != myconnectindex)
            {
                Bsprintf(ScriptQuotes[122],"%s^00 SAVED A MULTIPLAYER GAME",&g_player[multiwho].user_name[0]);
                P_DoQuote(122,g_player[myconnectindex].ps);
            }
            else
            {
                Bstrcpy(ScriptQuotes[122],"MULTIPLAYER GAME SAVED");
                P_DoQuote(122,g_player[myconnectindex].ps);
            }
            break;
        }
        else
        {
            //            waitforeverybody();

            j = G_LoadPlayer(multipos);

            multiflag = 0;

            if (j == 0)
            {
                if (multiwho != myconnectindex)
                {
                    Bsprintf(ScriptQuotes[122],"%s^00 LOADED A MULTIPLAYER GAME",&g_player[multiwho].user_name[0]);
                    P_DoQuote(122,g_player[myconnectindex].ps);
                }
                else
                {
                    Bstrcpy(ScriptQuotes[122],"MULTIPLAYER GAME LOADED");
                    P_DoQuote(122,g_player[myconnectindex].ps);
                }
                return 1;
            }
        }
    }

    ud.camerasprite = -1;
    lockclock += TICSPERFRAME;
    
    if (g_RTSPlaying > 0) g_RTSPlaying--;

    for (i=0;i<MAXUSERQUOTES;i++)
        if (user_quote_time[i])
        {
            user_quote_time[i]--;
            if (user_quote_time[i] > ud.msgdisptime)
                user_quote_time[i] = ud.msgdisptime;
            if (!user_quote_time[i]) pub = NUMPAGES;
        }

    if (g_showShareware > 0)
    {
        g_showShareware--;
        if (g_showShareware == 0)
        {
            pus = NUMPAGES;
            pub = NUMPAGES;
        }
    }

    // Copy input FIFO bits into player's own inputBits struct, and advance our place in the FIFO.
    TRAVERSE_CONNECT(i)
    {
        *g_player[i].inputBits = inputfifo[movefifoplc & (MOVEFIFOSIZ - 1)][i];
        Net_CheckPlayerQuit(i);
    }
    movefifoplc++;

    Net_GetSyncStat();

    G_UpdateInterpolations();

    everyothertime++;
    g_moveThingsCount++;

    if (ud.recstat == 1) G_DemoRecord();

    // G_DoWorldSim() -------------------------
    if (ud.pause_on == 0)
    {
        // Moved to pause check since this can desync the game if someone pauses during a quake
        if (g_earthquakeTime > 0)
            g_earthquakeTime--;

        //g_random.global += randomseed;
        g_globalRandom = seed_krand(&g_random.global);
        A_MoveDummyPlayers();//ST 13
    }

    TRAVERSE_CONNECT(i)
    {
        // P_HandlePlayerColor
        if (g_player[i].inputBits->extbits&(EXTBIT_CHANGETEAM))
        {
            g_player[i].ps->team = g_player[i].pteam;
            if (GametypeFlags[ud.coop] & GAMETYPE_TDM)
            {
                actor[g_player[i].ps->i].picnum = APLAYERTOP;
                P_QuickKill(g_player[i].ps);
            }
        }
        if (GametypeFlags[ud.coop] & GAMETYPE_TDM)
            g_player[i].ps->palookup = g_player[i].pcolor = teams[g_player[i].ps->team].palnum;

        if (sprite[g_player[i].ps->i].pal != 1)
            sprite[g_player[i].ps->i].pal = g_player[i].pcolor;
        // P_HandlePlayerColor

        sounds_predictable = true;
        G_HandleSharedKeys(i);
        sounds_predictable = false;

        if (ud.pause_on == 0)
        {
            sounds_predictable = true;
            P_ProcessInput(i);
            sounds_predictable = false;
            P_CheckSectors(i);
        }
    }

    if (ud.pause_on == 0)
        G_MoveWorld();
    // G_DoWorldSim() -------------------------

    Net_CorrectPrediction();

    if ((everyothertime&1) == 0)
    {
        G_AnimateWalls();
        A_MoveCyclers();
    }

#if 0//POLYMER
    //polymer invalidate
    updatesectors = 1;
#endif

    return 0;
}

static void G_DrawCameraText(short i)
{
    char flipbits;
    int x , y;

    if (!T1(i))
    {
        rotatesprite(24<<16,33<<16,65536L,0,CAMCORNER,0,0,2,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
        rotatesprite((320-26)<<16,34<<16,65536L,0,CAMCORNER+1,0,0,2,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
        rotatesprite(22<<16,163<<16,65536L,512,CAMCORNER+1,0,0,2+4,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
        rotatesprite((310-10)<<16,163<<16,65536L,512,CAMCORNER+1,0,0,2,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
        if ((int32_t)totalclock&16)
            rotatesprite(46<<16,32<<16,65536L,0,CAMLIGHT,0,0,2,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
    }
    else
    {
        flipbits = ((int32_t)totalclock<<1)&48;
        for (x=0;x<394;x+=64)
            for (y=0;y<200;y+=64)
                rotatesprite(x<<16,y<<16,65536L,0,STATIC,0,0,2+flipbits,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
    }
}

#if 0
void vglass(int x,int y,short a,short wn,short n)
{
    int z, zincs;
    short sect;

    sect = wall[wn].nextsector;
    if (sect == -1) return;
    zincs = (sector[sect].floorz-sector[sect].ceilingz) / n;

    for (z = sector[sect].ceilingz;z < sector[sect].floorz; z += zincs)
        A_InsertSprite(sect,x,y,z-(krand()&8191),GLASSPIECES+(z&(krand()%3)),-32,36,36,a+128-(krand()&255),16+(krand()&31),0,-1,5);
}
#endif

void A_SpawnWallGlass(int i,int wallnum,int n)
{
    int j, xv, yv, z, x1, y1;
    short sect;
    int a;

    sect = -1;

    if (wallnum < 0)
    {
        for (j=n-1; j >= 0 ;j--)
        {
            a = SA(i)-256+(krand()&511)+1024;
            A_InsertSprite(SECT(i),SX(i),SY(i),SZ(i),GLASSPIECES+(j%3),-32,36,36,a,32+(krand()&63),1024-(krand()&1023),i,5);
        }
        return;
    }

    j = n+1;

    x1 = wall[wallnum].x;
    y1 = wall[wallnum].y;

    xv = wall[wall[wallnum].point2].x-x1;
    yv = wall[wall[wallnum].point2].y-y1;

    x1 -= ksgn(yv);
    y1 += ksgn(xv);

    xv /= j;
    yv /= j;

    for (j=n;j>0;j--)
    {
        x1 += xv;
        y1 += yv;

        updatesector(x1,y1,&sect);
        if (sect >= 0)
        {
            z = sector[sect].floorz-(krand()&(klabs(sector[sect].ceilingz-sector[sect].floorz)));
            if (z < -(32<<8) || z > (32<<8))
                z = SZ(i)-(32<<8)+(krand()&((64<<8)-1));
            a = SA(i)-1024;
            A_InsertSprite(SECT(i),x1,y1,z,GLASSPIECES+(j%3),-32,36,36,a,32+(krand()&63),-(krand()&1023),i,5);
        }
    }
}

void A_SpawnGlass(int i,int n)
{
    int j, k, a, z;

    for (j=n;j>0;j--)
    {
        a = krand()&2047;
        z = SZ(i)-((krand()&16)<<8);
        k = A_InsertSprite(SECT(i),SX(i),SY(i),z,GLASSPIECES+(j%3),krand()&15,36,36,a,32+(krand()&63),-512-(krand()&2047),i,5);
        sprite[k].pal = sprite[i].pal;
    }
}

void A_SpawnCeilingGlass(int i,int sectnum,int n)
{
    int j, xv, yv, z, x1, y1, a,s;
    int startwall = sector[sectnum].wallptr;
    int endwall = startwall+sector[sectnum].wallnum;

    for (s=startwall;s<(endwall-1);s++)
    {
        x1 = wall[s].x;
        y1 = wall[s].y;

        xv = (wall[s+1].x-x1)/(n+1);
        yv = (wall[s+1].y-y1)/(n+1);

        for (j=n;j>0;j--)
        {
            x1 += xv;
            y1 += yv;
            a = krand()&2047;
            z = sector[sectnum].ceilingz+((krand()&15)<<8);
            A_InsertSprite(sectnum,x1,y1,z,GLASSPIECES+(j%3),-32,36,36,a,(krand()&31),0,i,5);
        }
    }
}

void A_SpawnRandomGlass(int i,int wallnum,int n)
{
    int j, xv, yv, z, x1, y1;
    short sect = -1;
    int a, k;

    if (wallnum < 0)
    {
        for (j=n-1; j >= 0 ;j--)
        {
            a = krand()&2047;
            k = A_InsertSprite(SECT(i),SX(i),SY(i),SZ(i)-(krand()&(63<<8)),GLASSPIECES+(j%3),-32,36,36,a,32+(krand()&63),1024-(krand()&2047),i,5);
            sprite[k].pal = krand()&15;
        }
        return;
    }

    j = n+1;
    x1 = wall[wallnum].x;
    y1 = wall[wallnum].y;

    xv = (wall[wall[wallnum].point2].x-wall[wallnum].x)/j;
    yv = (wall[wall[wallnum].point2].y-wall[wallnum].y)/j;

    for (j=n;j>0;j--)
    {
        x1 += xv;
        y1 += yv;

        updatesector(x1,y1,&sect);
        z = sector[sect].floorz-(krand()&(klabs(sector[sect].ceilingz-sector[sect].floorz)));
        if (z < -(32<<8) || z > (32<<8))
            z = SZ(i)-(32<<8)+(krand()&((64<<8)-1));
        a = SA(i)-1024;
        k = A_InsertSprite(SECT(i),x1,y1,z,GLASSPIECES+(j%3),-32,36,36,a,32+(krand()&63),-(krand()&2047),i,5);
        sprite[k].pal = krand()&7;
    }
}

static void G_SetupGameButtons(void)
{
    CONTROL_DefineFlag(gamefunc_Move_Forward,false);
    CONTROL_DefineFlag(gamefunc_Move_Backward,false);
    CONTROL_DefineFlag(gamefunc_Turn_Left,false);
    CONTROL_DefineFlag(gamefunc_Turn_Right,false);
    CONTROL_DefineFlag(gamefunc_Strafe,false);
    CONTROL_DefineFlag(gamefunc_Fire,false);
    CONTROL_DefineFlag(gamefunc_Open,false);
    CONTROL_DefineFlag(gamefunc_Run,false);
    CONTROL_DefineFlag(gamefunc_AutoRun,false);
    CONTROL_DefineFlag(gamefunc_Jump,false);
    CONTROL_DefineFlag(gamefunc_Crouch,false);
    CONTROL_DefineFlag(gamefunc_Look_Up,false);
    CONTROL_DefineFlag(gamefunc_Look_Down,false);
    CONTROL_DefineFlag(gamefunc_Look_Left,false);
    CONTROL_DefineFlag(gamefunc_Look_Right,false);
    CONTROL_DefineFlag(gamefunc_Strafe_Left,false);
    CONTROL_DefineFlag(gamefunc_Strafe_Right,false);
    CONTROL_DefineFlag(gamefunc_Aim_Up,false);
    CONTROL_DefineFlag(gamefunc_Aim_Down,false);
    CONTROL_DefineFlag(gamefunc_Weapon_1,false);
    CONTROL_DefineFlag(gamefunc_Weapon_2,false);
    CONTROL_DefineFlag(gamefunc_Weapon_3,false);
    CONTROL_DefineFlag(gamefunc_Weapon_4,false);
    CONTROL_DefineFlag(gamefunc_Weapon_5,false);
    CONTROL_DefineFlag(gamefunc_Weapon_6,false);
    CONTROL_DefineFlag(gamefunc_Weapon_7,false);
    CONTROL_DefineFlag(gamefunc_Weapon_8,false);
    CONTROL_DefineFlag(gamefunc_Weapon_9,false);
    CONTROL_DefineFlag(gamefunc_Weapon_10,false);
    CONTROL_DefineFlag(gamefunc_Inventory,false);
    CONTROL_DefineFlag(gamefunc_Inventory_Left,false);
    CONTROL_DefineFlag(gamefunc_Inventory_Right,false);
    CONTROL_DefineFlag(gamefunc_Holo_Duke,false);
    CONTROL_DefineFlag(gamefunc_Jetpack,false);
    CONTROL_DefineFlag(gamefunc_NightVision,false);
    CONTROL_DefineFlag(gamefunc_MedKit,false);
    CONTROL_DefineFlag(gamefunc_TurnAround,false);
    CONTROL_DefineFlag(gamefunc_SendMessage,false);
    CONTROL_DefineFlag(gamefunc_Map,false);
    CONTROL_DefineFlag(gamefunc_Shrink_Screen,false);
    CONTROL_DefineFlag(gamefunc_Enlarge_Screen,false);
    CONTROL_DefineFlag(gamefunc_Center_View,false);
    CONTROL_DefineFlag(gamefunc_Holster_Weapon,false);
    CONTROL_DefineFlag(gamefunc_Show_Opponents_Weapon,false);
    CONTROL_DefineFlag(gamefunc_Map_Follow_Mode,false);
    CONTROL_DefineFlag(gamefunc_See_Coop_View,false);
    CONTROL_DefineFlag(gamefunc_Mouse_Aiming,false);
    CONTROL_DefineFlag(gamefunc_Toggle_Crosshair,false);
    CONTROL_DefineFlag(gamefunc_Steroids,false);
    CONTROL_DefineFlag(gamefunc_Quick_Kick,false);
    CONTROL_DefineFlag(gamefunc_Next_Weapon,false);
    CONTROL_DefineFlag(gamefunc_Previous_Weapon,false);

    CONTROL_DefineFlag(gamefunc_Alt_Fire, FALSE);
    CONTROL_DefineFlag(gamefunc_Last_Weapon, FALSE);
    CONTROL_DefineFlag(gamefunc_Quick_Save, FALSE);
    CONTROL_DefineFlag(gamefunc_Quick_Load, FALSE);
    CONTROL_DefineFlag(gamefunc_Alt_Weapon, FALSE);
    CONTROL_DefineFlag(gamefunc_Third_Person_View, FALSE);
    CONTROL_DefineFlag(gamefunc_Toggle_Crouch, FALSE);
}

// [JM] Needed for build linkage
void M32RunScript(const char* s) { UNREFERENCED_PARAMETER(s); }
void G_Polymer_UnInit(void) { }
const char* G_DefaultDefFile(void)
{
    return duke3ddef;
}

/*
===================
=
= GetTime
=
===================
*/

int GetTime(void)
{
    return (int32_t)totalclock;
}
