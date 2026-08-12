// This object is shared by the editors of *all* Build games!

#include "compat.h"
#include "keys.h"
#include "build.h"
#include "cache1d.h"
#ifdef POLYMER
# include "polymer.h"
#endif
#include "editor.h"
#include "renderlayer.h"

#include "m32script.h"
#include "m32def.h"

#include "lz4.h"
#include "xxhash.h"

// XXX: This breaks editors for games other than Duke. The OSD needs a way to specify colors in abstract instead of concatenating palswap escape sequences.
#include "common_game.h"

#include "../../build/include/m32common_taglab_core_shared.cpp.inc"

int32_t taglab_load(const char *filename, int32_t flags)
{
    int32_t fil, len, i;
    char buf[BMAX_PATH], *dot, *filebuf;

    taglab_init();

    len = Bstrlen(filename);
    if (len >= BMAX_PATH-1)
        return -1;
    Bmemcpy(buf, filename, len+1);

    //
    dot = Bstrrchr(buf, '.');
    if (!dot)
        dot = &buf[len];

    if (dot-buf+8 >= BMAX_PATH)
        return -1;
    Bmemcpy(dot, ".maptags", 9);
    //

    if ((fil = kopen4load(buf,flags)) == -1)
        return -1;

    len = kfilelength(fil);

    filebuf = (char *)Xmalloc(len+1);
    if (!filebuf)
    {
        kclose(fil);
        return -1;
    }

    kread(fil, filebuf, len);
    filebuf[len] = 0;
    kclose(fil);

    // ----

    {
        int32_t tag;
        char *cp=filebuf, *bp, *ep;

        while (1)
        {
#define XTAGLAB_STRINGIFY(X) TAGLAB_STRINGIFY(X)
#define TAGLAB_STRINGIFY(X) #X
            i = sscanf(cp, "%d %" XTAGLAB_STRINGIFY(TAGLAB_MAX) "s", &tag, buf);
#undef XTAGLAB_STRINGIFY
#undef TAGLAB_STRINGIFY
            if (i != 2 || !buf[0] || tag<0 || tag>=32768)
                goto nextline;

            buf[TAGLAB_MAX-1] = 0;

            i = Bstrlen(buf);
            bp = buf; while (*bp && isspace(*bp)) bp++;
            ep = &buf[i-1]; while (ep>buf && isspace(*ep)) ep--;
            ep++;

            if (!(ep > bp))
                goto nextline;
            *ep = 0;

            taglab_add(bp, tag);
//initprintf("add tag %s:%d\n", bp, tag);
nextline:
            while (*cp && *cp!='\n')
                cp++;
            while (*cp=='\r' || *cp=='\n')
                cp++;
            if (*cp == 0)
                break;
        }
    }

    // ----
    Bfree(filebuf);

    return 0;
}

int32_t taglab_save(const char *mapname)
{
    int32_t fil, len, i;
    char buf[BMAX_PATH], *dot;
    const char *label;

    if (g_taglab.numlabels==0)
        return 1;

    Bstrncpyz(buf, mapname, BMAX_PATH);

    len = Bstrlen(buf);
    //
    dot = Bstrrchr(buf, '.');
    if (!dot)
        dot = &buf[len];

    if (dot-buf+8 >= BMAX_PATH)
        return -1;
    Bmemcpy(dot, ".maptags", 9);
    //

    if ((fil = Bopen(buf,BO_BINARY|BO_TRUNC|BO_CREAT|BO_WRONLY,BS_IREAD|BS_IWRITE)) == -1)
    {
        initprintf("Couldn't open \"%s\" for writing: %s\n", buf, strerror(errno));
        return -1;
    }

    for (i=0; i<32768; i++)
    {
        label = taglab_getlabel(i);
        if (!label)
            continue;

        len = Bsprintf(buf, "%d %s" OURNEWL, i, label);
        if (Bwrite(fil, buf, len)!=len)
            break;
    }

    Bclose(fil);

    return (i!=32768);
}

#include "../../build/include/m32common_taglab_lookup_shared.cpp.inc"

#include "../../build/include/m32common_undo_blood_rr_shared.cpp.inc"

////

#include "../../build/include/m32common_corruptchk_prefix_shared.cpp.inc"


#define M32COMMON_LOG_SPRITE_COUNT_MISMATCH(ournumsprites, numsprites) \
    initprintf("ournumsprites=%d, Numsprites=%d\n", (ournumsprites), (numsprites))
#include "../../build/include/m32common_spritelist_consistency_shared.cpp.inc"

#if CCHK_LOOP_CHECKS
// Return the least wall index of the outer loop of sector <sectnum>, or
//  -1 if there is none,
//  -2 if there is more than one.
static int32_t determine_outer_loop(int32_t sectnum)
{
    int32_t j, outerloopstart = -1;

    const int32_t startwall = sector[sectnum].wallptr;
    const int32_t endwall = startwall + sector[sectnum].wallnum - 1;

    for (j=startwall; j<=endwall; j=get_nextloopstart(j))
    {
        if (clockdir(j) == CLOCKDIR_CW)
        {
            if (outerloopstart == -1)
                outerloopstart = j;
            else if (outerloopstart >= 0)
                return -2;
        }
    }

    return outerloopstart;
}
#endif

#define TRYFIX_NONE() (tryfixing == 0ull)
#define TRYFIX_CNUM(onumct) (onumct < MAXCORRUPTTHINGS && (tryfixing & (1ull<<onumct)))

#define M32COMMON_FREE_SEEN_NEXTWALLS(ptr) Bfree(ptr)
#include "../../build/include/m32common_check_corruption_shared.cpp.inc"
////


#include "../../build/include/m32common_status_menu_shared.cpp.inc"

#define M32COMMON_FUNC_MENU_FREE_SCRIPT(ptr) Bfree(ptr)
#define M32COMMON_FUNC_MENU_DELETE_SPRITE(spriteNum, deletedCount) do { deletesprite(spriteNum); (deletedCount)++; } while (0)
#define M32COMMON_FUNC_MENU_MAX_TILE_PROMPT (MAXSPRITES-1)
#include "../../build/include/m32common_special_functions_menu_shared.cpp.inc"
