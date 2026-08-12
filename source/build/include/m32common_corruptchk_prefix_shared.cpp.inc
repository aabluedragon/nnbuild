//// port of a.m32's corruptchk ////
// Compile wall loop checks? 0: no, 1: partial, 2: full.
#define CCHK_LOOP_CHECKS 0
// returns value from 0 (all OK) to 5 (panic!)
#define CCHK_PANIC OSDTEXT_DARKRED "PANIC!!!^O "
//#define CCHKPREF OSDTEXT_RED "^O"
#define CCHK_CORRECTED OSDTEXT_GREEN " -> "

#define CORRUPTCHK_PRINT(errlev, what, fmt, ...) do  \
{ \
    bad = max(bad, errlev); \
    if (numcorruptthings>=MAXCORRUPTTHINGS) \
        goto too_many_errors; \
    corruptthings[numcorruptthings++] = (what); \
    if (errlev >= printfromlev) \
        OSD_Printf("#%d: " fmt "\n", numcorruptthings, ## __VA_ARGS__); \
} while (0)

#ifdef YAX_ENABLE
static int32_t walls_have_equal_endpoints(int32_t w1, int32_t w2)
{
    int32_t n1 = wall[w1].point2, n2 = wall[w2].point2;

    return (wall[w1].x==wall[w2].x && wall[w1].y==wall[w2].y &&
            wall[n1].x==wall[n2].x && wall[n1].y==wall[n2].y);
}

static void correct_yax_nextwall(int32_t wallnum, int32_t bunchnum, int32_t cf, int32_t tryfixingp)
{
    int32_t i, j, startwall, endwall;
    int32_t nummatching=0, lastwall[2] = { -1, -1 };

    for (SECTORS_OF_BUNCH(bunchnum, !cf, i))
        for (WALLS_OF_SECTOR(i, j))
        {
            //  v v v shouldn't happen, 'stupidity safety'
            if (j!=wallnum && walls_have_equal_endpoints(wallnum, j))
            {
                lastwall[nummatching++] = j;
                if (nummatching==2)
                    goto outofloop;
            }
        }
outofloop:
    if (nummatching==1)
    {
        if (!tryfixingp)
        {
            OSD_Printf("    will set wall %d's %s to %d on tryfix\n",
                       wallnum, yupdownwall[cf], lastwall[0]);
        }
        else
        {
            int32_t setreverse = 0;
            yax_setnextwall(wallnum, cf, lastwall[0]);
            if (yax_getnextwall(lastwall[0], !cf) < 0)
            {
                setreverse = 1;
                yax_setnextwall(lastwall[0], !cf, wallnum);
            }

            OSD_Printf("auto-correction: set wall %d's %s to %d%s\n",
                       wallnum, yupdownwall[cf], lastwall[0], setreverse?" and its reverse link":"");
        }
    }
    else if (!tryfixingp)
    {
        if (nummatching > 1)
        {
            OSD_Printf("    found more than one matching wall: at least %d and %d\n",
                       lastwall[0], lastwall[1]);
        }
        else if (nummatching == 0)
        {
            OSD_Printf("    found no matching walls!\n");
        }
    }
}
#endif

// in reverse orientation
static int32_t walls_are_consistent(int32_t w1, int32_t w2)
{
    return (wall[w2].x==POINT2(w1).x && wall[w2].y==POINT2(w1).y &&
            wall[w1].x==POINT2(w2).x && wall[w1].y==POINT2(w2).y);
}

static void suggest_nextsector_correction(int32_t nw, int32_t j)
{
    // wall j's nextsector is inconsistent with its nextwall... what shall we do?

    if (nw>=0 && nw<numwalls)
    {
        // maybe wall[j].nextwall's nextwall is right?
        if (wall[nw].nextwall==j && walls_are_consistent(nw, j))
            OSD_Printf("   suggest setting wall[%d].nextsector to %d\n",
                       j, sectorofwall_noquick(nw));
    }

    // alternative
    if (wall[j].nextsector>=0 && wall[j].nextsector<numsectors)
    {
        int32_t w, startwall, endwall;
        for (WALLS_OF_SECTOR(wall[j].nextsector, w))
        {
            // XXX: need clearing some others?
            if (walls_are_consistent(w, j))
            {
                OSD_Printf(" ? suggest setting wall[%d].nextwall to %d\n",
                           j, w);
                break;
            }
        }
    }

    OSD_Printf(" ?? suggest making wall %d white\n", j);
}

static void do_nextsector_correction(int32_t nw, int32_t j)
{
    if (corrupt_tryfix_alt==0)
    {
        if (nw>=0 && nw<numwalls)
            if (wall[nw].nextwall==j && walls_are_consistent(nw, j))
            {
                int32_t newns = sectorofwall_noquick(nw);
                wall[j].nextsector = newns;
                OSD_Printf(CCHK_CORRECTED "auto-correction: set wall[%d].nextsector=%d\n",
                           j, newns);
            }
    }
    else if (corrupt_tryfix_alt==1)
    {
        if (wall[j].nextsector>=0 && wall[j].nextsector<numsectors)
        {
            int32_t w, startwall, endwall;
            for (WALLS_OF_SECTOR(wall[j].nextsector, w))
                if (walls_are_consistent(w, j))
                {
                    wall[j].nextwall = w;
                    OSD_Printf(CCHK_CORRECTED "auto-correction: set wall[%d].nextwall=%d\n",
                               j, w);
                    break;
                }
        }
    }
    else if (corrupt_tryfix_alt==2)
    {
        wall[j].nextwall = wall[j].nextsector = -1;
        OSD_Printf(CCHK_CORRECTED "auto-correction: made wall %d white\n", j);
    }
}
