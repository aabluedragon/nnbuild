#ifndef M32COMMON_FREE_SEEN_NEXTWALLS
# define M32COMMON_FREE_SEEN_NEXTWALLS(ptr) Xfree(ptr)
#endif

int32_t CheckMapCorruption(int32_t printfromlev, uint64_t tryfixing)
{
    int32_t i, j;
    int32_t ewall=0;  // expected wall index

    int32_t errlevel=0, bad=0;
    int32_t heinumcheckstat = 0;  // 1, 2

    uint8_t *seen_nextwalls = NULL;
    int16_t *lastnextwallsource = NULL;

    numcorruptthings = 0;

    if (numsectors>MAXSECTORS)
        CORRUPTCHK_PRINT(5, 0, CCHK_PANIC "SECTOR LIMIT EXCEEDED (MAXSECTORS=%d)!!!", MAXSECTORS);

    if (numwalls>MAXWALLS)
        CORRUPTCHK_PRINT(5, 0, CCHK_PANIC "WALL LIMIT EXCEEDED (MAXWALLS=%d)!!!", MAXWALLS);

    if (numsectors>MAXSECTORS || numwalls>MAXWALLS)
    {
        corruptlevel = bad;
        return bad;
    }

    if (numsectors==0 || numwalls==0)
    {
        if (numsectors>0)
            CORRUPTCHK_PRINT(5, 0, CCHK_PANIC " Have sectors but no walls!");
        if (numwalls>0)
            CORRUPTCHK_PRINT(5, 0, CCHK_PANIC " Have walls but no sectors!");
        return bad;
    }

    if (!corruptcheck_noalreadyrefd)
    {
        seen_nextwalls = (uint8_t *)Xcalloc(bitmap_size(numwalls),1);
        lastnextwallsource = (int16_t *)Xmalloc(numwalls*sizeof(lastnextwallsource[0]));
    }

    for (i=0; i<numsectors; i++)
    {
        const int32_t w0 = sector[i].wallptr;
        const int32_t numw = sector[i].wallnum;
        const int32_t endwall = w0 + numw - 1;  // inclusive

        bad = 0;

        if (w0 < 0 || w0 > numwalls)
        {
            if (w0 < 0 || w0 >= MAXWALLS)
                CORRUPTCHK_PRINT(5, CORRUPT_SECTOR|i, "SECTOR[%d].WALLPTR=%d INVALID!!!", i, w0);
            else
                CORRUPTCHK_PRINT(5, CORRUPT_SECTOR|i, "SECTOR[%d].WALLPTR=%d out of range (numwalls=%d)", i, w0, numw);
        }

        if (w0 != ewall)
            CORRUPTCHK_PRINT(4, CORRUPT_SECTOR|i, "SECTOR[%d].WALLPTR=%d inconsistent, expected %d", i, w0, ewall);

        if (numw <= 1)
            CORRUPTCHK_PRINT(5, CORRUPT_SECTOR|i, CCHK_PANIC "SECTOR[%d].WALLNUM=%d INVALID!!!", i, numw);
        else if (numw==2)
            CORRUPTCHK_PRINT(3, CORRUPT_SECTOR|i, "SECTOR[%d].WALLNUM=2, expected at least 3", i);

        ewall += numw;

        if (endwall >= numwalls)
            CORRUPTCHK_PRINT(5, CORRUPT_SECTOR|i, "SECTOR[%d]: wallptr+wallnum=%d out of range: numwalls=%d", i, endwall, numwalls);

        // inconsistent cstat&2 and heinum checker
        if (corruptcheck_heinum)
        {
            const char *cflabel[2] = {"ceiling", "floor"};

            for (j=0; j<2; j++)
            {
                const int32_t cs = !!(SECTORFLD(i,stat, j)&2);
                const int32_t hn = !!SECTORFLD(i,heinum, j);

                if (cs != hn && heinumcheckstat <= 1)
                {
                    if (numcorruptthings < MAXCORRUPTTHINGS &&
                        (heinumcheckstat==1 || (heinumcheckstat==0 && (tryfixing & (1ull<<numcorruptthings)))))
                    {
                        setslope(i, j, 0);
                        OSD_Printf(CCHK_CORRECTED "auto-correction: reset sector %d's %s slope\n",
                                   i, cflabel[j]);
                        heinumcheckstat = 1;
                    }
                    else if (corruptcheck_heinum==2 && heinumcheckstat==0)
                    {
                        CORRUPTCHK_PRINT(1, CORRUPT_SECTOR|i,
                                         "SECTOR[%d]: inconsistent %sstat&2 and heinum", i, cflabel[j]);
                    }

                    if (heinumcheckstat != 1)
                        heinumcheckstat = 2;
                }
            }
        }

        errlevel = max(errlevel, bad);

        if (bad < 4)
        {
            for (j=w0; j<=endwall; j++)
            {
                const int32_t nw = wall[j].nextwall;
                const int32_t ns = wall[j].nextsector;

                bad = 0;

                // First, some basic wall sanity checks.

                if (wall[j].point2 < w0 || wall[j].point2 > endwall)
                {
                    if (wall[j].point2 < 0 || wall[j].point2 >= MAXWALLS)
                        CORRUPTCHK_PRINT(5, CORRUPT_WALL|j, CCHK_PANIC "WALL[%d].POINT2=%d INVALID!!!",
                                         j, TrackerCast(wall[j].point2));
                    else
                        CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].POINT2=%d out of range [%d, %d]",
                                         j, TrackerCast(wall[j].point2), w0, endwall);
                }

                if (nw >= numwalls)
                {
                    const int32_t onumct = numcorruptthings;

                    if (TRYFIX_NONE())
                    {
                        if (nw >= MAXWALLS)
                            CORRUPTCHK_PRINT(5, CORRUPT_WALL|j, "WALL[%d].NEXTWALL=%d INVALID!!!",
                                             j, nw);
                        else
                            CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].NEXTWALL=%d out of range: numwalls=%d",
                                             j, nw, numwalls);
                        OSD_Printf("    will make wall %d white on tryfix\n", j);
                    }
                    else if (TRYFIX_CNUM(onumct))  // CODEDUP MAKE_WALL_WHITE
                    {
                        wall[j].nextwall = wall[j].nextsector = -1;
                        OSD_Printf(CCHK_CORRECTED "auto-correction: made wall %d white\n", j);
                    }
                }

                if (ns >= numsectors)
                {
                    const int32_t onumct = numcorruptthings;

                    if (TRYFIX_NONE())
                    {
                        if (ns >= MAXSECTORS)
                            CORRUPTCHK_PRINT(5, CORRUPT_WALL|j, "WALL[%d].NEXTSECTOR=%d INVALID!!!",
                                             j, ns);
                        else
                            CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].NEXTSECTOR=%d out of range: numsectors=%d",
                                             j, ns, numsectors);
                        OSD_Printf("    will make wall %d white on tryfix\n", j);
                    }
                    else if (TRYFIX_CNUM(onumct))  // CODEDUP MAKE_WALL_WHITE
                    {
                        wall[j].nextwall = wall[j].nextsector = -1;
                        OSD_Printf(CCHK_CORRECTED "auto-correction: made wall %d white\n", j);
                    }
                }

                if (nw>=w0 && nw<=endwall)
                    CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].NEXTWALL is its own sector's wall", j);

                if (wall[j].x==POINT2(j).x && wall[j].y==POINT2(j).y)
                    CORRUPTCHK_PRINT(3, CORRUPT_WALL|j, "WALL[%d] has length 0", j);

#ifdef YAX_ENABLE
                // Various TROR checks.
                {
                    int32_t cf;

                    for (cf=0; cf<2; cf++)
                    {
                        const int32_t ynw = yax_getnextwall(j, cf);

                        if (ynw >= 0)
                        {
                            if (ynw >= numwalls)
                                CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL %d's %s=%d out of range: numwalls=%d",
                                                 j, YUPDOWNWALL[cf], ynw, numwalls);
                            else
                            {
                                int32_t ynextwallok = 1;

                                if (j == ynw)
                                {
                                    CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL %d's %s is itself",
                                                     j, YUPDOWNWALL[cf]);
                                    ynextwallok = 0;
                                }
                                else if (!walls_have_equal_endpoints(j, ynw))
                                {
                                    CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL %d's and its %s=%d's "
                                                     "endpoints are inconsistent", j, YUPDOWNWALL[cf], ynw);
                                    ynextwallok = 0;
                                }

                                {
                                    const int16_t bunchnum = yax_getbunch(i, cf);
                                    const int32_t onumct = numcorruptthings;

                                    if (bunchnum < 0 || bunchnum >= numyaxbunches)
                                    {
                                        if (tryfixing == 0ull)
                                        {
                                            CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL %d has %s=%d, "
                                                             "but its %s bunchnum=%d is invalid",
                                                             j, YUPDOWNWALL[cf], ynw,
                                                             cf==YAX_CEILING? "ceiling":"floor", bunchnum);
                                            OSD_Printf("    will clear wall %d's %s to -1 on tryfix\n",
                                                       j, yupdownwall[cf]);

                                        }
                                        else if (tryfixing & (1ull<<onumct))
                                        {
                                            yax_setnextwall(j, cf, -1);
                                            OSD_Printf(CCHK_CORRECTED "auto-correction: cleared wall %d's %s to -1\n",
                                                       j, yupdownwall[cf]);
                                        }
                                    }
                                    else if (!ynextwallok && onumct < MAXCORRUPTTHINGS)
                                    {
                                        if ((tryfixing & (1ull<<onumct)) || 4>=printfromlev)
                                            correct_yax_nextwall(j, bunchnum, cf, tryfixing!=0ull);
                                    }
                                }

                                if (ynextwallok)
                                {
                                    const int32_t onumct = numcorruptthings;
                                    const int32_t ynwp2 = yax_getnextwall(ynw, !cf);

                                    if (ynwp2 != j)
                                    {
                                        CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL %d's %s=%d's reverse link wrong"
                                                         " (expected %d, have %d)", j, YUPDOWNWALL[cf], ynw, j, ynwp2);
                                        if (onumct < MAXCORRUPTTHINGS)
                                        {
                                            if (tryfixing & (1ull<<onumct))
                                            {
                                                yax_setnextwall(ynw, !cf, j);
                                                OSD_Printf(CCHK_CORRECTED "auto-correction: set wall %d's %s=%d's %s to %d\n",
                                                           j, yupdownwall[cf], ynw, yupdownwall[!cf], j);
                                            }
                                            else if (4>=printfromlev)
                                            {
                                                OSD_Printf("   will set wall %d's %s=%d's %s to %d on tryfix\n",
                                                           j, yupdownwall[cf], ynw, yupdownwall[!cf], j);
                                            }
                                        }
                                    }
                                }   // brace woot!
                            }
                        }
                    }
                }
#endif
                // Check for ".nextsector is its own sector"
                if (ns == i)
                {
                    if (!bad)
                    {
                        const int32_t onumct = numcorruptthings;
                        const int32_t safetoclear = (nw==j || (wall[nw].nextwall==-1 && wall[nw].nextsector==-1));

                        CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].NEXTSECTOR is its own sector", j);
                        if (onumct < MAXCORRUPTTHINGS)
                        {
                            if (tryfixing & (1ull<<onumct))
                            {
                                if (safetoclear)
                                {
                                    wall[j].nextwall = wall[j].nextsector = -1;
                                    OSD_Printf(CCHK_CORRECTED "auto-correction: cleared wall %d's nextwall"
                                               " and nextsector\n", j);
                                }
                                else
                                    do_nextsector_correction(nw, j);
                            }
                            else if (4>=printfromlev)
                            {
                                if (safetoclear)
                                    OSD_Printf("   will clear wall %d's nextwall and nextsector on tryfix\n", j);
                                else
                                    suggest_nextsector_correction(nw, j);
                            }
                        }
                    }
                }

                // Check for ".nextwall already referenced from wall ..."
                if (!corruptcheck_noalreadyrefd && nw>=0 && nw<numwalls)
                {
                    if (bitmap_test(seen_nextwalls, nw))
                    {
                        const int32_t onumct = numcorruptthings;

                        const int16_t lnws = lastnextwallsource[nw];
                        const int16_t nwnw = wall[nw].nextwall;

                        CORRUPTCHK_PRINT(3, CORRUPT_WALL|j, "WALL[%d].NEXTWALL=%d already referenced from wall %d",
                                         j, nw, lnws);

                        if (onumct < MAXCORRUPTTHINGS && (nwnw==j || nwnw==lnws))
                        {
                            const int32_t walltoclear = nwnw==j ? lnws : j;

                            if (tryfixing & (1ull<<onumct))
                            {
                                wall[walltoclear].nextsector = wall[walltoclear].nextwall = -1;
                                OSD_Printf(CCHK_CORRECTED "auto-correction: cleared wall %d's nextwall and nextsector tags to -1\n",
                                           walltoclear);
                            }
                            else if (3 >= printfromlev)
                                OSD_Printf("    wall[%d].nextwall=%d, suggest clearing wall %d's nextwall and nextsector tags to -1\n",
                                           nw, nwnw, walltoclear);
                        }
                    }
                    else
                    {
                        seen_nextwalls[nw>>3] |= 1<<(nw&7);
                        lastnextwallsource[nw] = j;
                    }
                }

                // Various checks of .nextsector and .nextwall
                if (bad < 4)
                {
                    const int32_t onumct = numcorruptthings;

                    if ((ns^nw)<0)
                    {
                        CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].NEXTSECTOR=%d and .NEXTWALL=%d inconsistent:"
                                         " missing one next pointer", j, ns, nw);
                        if (onumct < MAXCORRUPTTHINGS)
                        {
                            if (tryfixing & (1ull<<onumct))
                                do_nextsector_correction(nw, j);
                            else if (4>=printfromlev)
                                suggest_nextsector_correction(nw, j);
                        }
                    }
                    else if (ns>=0)
                    {
                        if (nw<sector[ns].wallptr || nw>=sector[ns].wallptr+sector[ns].wallnum)
                        {
                            CORRUPTCHK_PRINT(4, CORRUPT_WALL|j, "WALL[%d].NEXTWALL=%d out of .NEXTSECTOR=%d's bounds [%d .. %d]",
                                             j, nw, ns, TrackerCast(sector[ns].wallptr), sector[ns].wallptr+sector[ns].wallnum-1);
                            if (onumct < MAXCORRUPTTHINGS)
                            {
                                if (tryfixing & (1ull<<onumct))
                                    do_nextsector_correction(nw, j);
                                else if (4 >= printfromlev)
                                    suggest_nextsector_correction(nw, j);
                            }
                        }
#if 0
                        // this one usually appears together with the "already referenced" corruption
                        else if (wall[nw].nextsector != i || wall[nw].nextwall != j)
                        {
                            CORRUPTCHK_PRINT(4, CORRUPT_WALL|nw, "WALL %d nextwall's backreferences inconsistent. Expected nw=%d, ns=%d; got nw=%d, ns=%d",
                                             nw, i, j, wall[nw].nextsector, wall[nw].nextwall);
                        }
#endif
                    }
                }

                errlevel = max(errlevel, bad);
            }
        }
#if CCHK_LOOP_CHECKS
        // Wall loop checks.
        if (bad < 4 && numw >= 4)
        {
            const int32_t outerloopstart = determine_outer_loop(i);

            if (outerloopstart == -1)
                CORRUPTCHK_PRINT(2, CORRUPT_SECTOR|i, "SECTOR %d contains no outer (clockwise) loop", i);
            else if (outerloopstart == -2)
                CORRUPTCHK_PRINT(2, CORRUPT_SECTOR|i, "SECTOR %d contains more than one outer (clockwise) loops", i);
# if CCHK_LOOP_CHECKS >= 2
            else
            {
                // Now, check for whether every wall-point of every inner loop of
                // this sector (<i>) is inside the outer one.

                for (j=w0; j<=endwall; /* will step by loops */)
                {
                    const int32_t nextloopstart = get_nextloopstart(j);

                    if (j != outerloopstart)
                    {
                        int32_t k;

                        for (k=j; k<nextloopstart; k++)
                            if (!loopinside(wall[k].x, wall[k].y, outerloopstart))
                            {
                                CORRUPTCHK_PRINT(4, CORRUPT_WALL|k, "WALL %d (of sector %d) is outside the outer loop", k, i);
                                goto end_wall_loop_checks;
                            }
                    }

                    j = nextloopstart;
                }
end_wall_loop_checks:
                ;
            }
# endif
            errlevel = max(errlevel, bad);
        }
#endif
    }

    bad = 0;
    for (i=0; i<MAXSPRITES; i++)
    {
        if (sprite[i].statnum==MAXSTATUS)
            continue;

        if (sprite[i].sectnum<0 || sprite[i].sectnum>=numsectors)
            CORRUPTCHK_PRINT(4, CORRUPT_SPRITE|i, "SPRITE[%d].SECTNUM=%d. Expect problems!", i, TrackerCast(sprite[i].sectnum));

        if (sprite[i].statnum<0 || sprite[i].statnum>MAXSTATUS)
            CORRUPTCHK_PRINT(4, CORRUPT_SPRITE|i, "SPRITE[%d].STATNUM=%d. Expect problems!", i, TrackerCast(sprite[i].statnum));

        if (sprite[i].picnum<0 || sprite[i].picnum>=MAXTILES)
        {
            sprite[i].picnum = 0;
            CORRUPTCHK_PRINT(0, CORRUPT_SPRITE|i, "SPRITE[%d].PICNUM=%d out of range, resetting to 0", i, TrackerCast(sprite[i].picnum));
        }

        if (corruptcheck_game_duke3d)
        {
            const int32_t tilenum = sprite[i].picnum;

            if (tilenum >= 1 && tilenum <= 9 && (sprite[i].cstat & CSTAT_SPRITE_ALIGNMENT))
            {
                const int32_t onumct = numcorruptthings;

                CORRUPTCHK_PRINT(1, CORRUPT_SPRITE|i, "%s sprite %d is not face-aligned",
                                 names[tilenum], i);

                if (onumct < MAXCORRUPTTHINGS)
                {
                    if (tryfixing & (1ull<<onumct))
                    {
                        sprite[i].cstat &= ~(32+16);
                        OSD_Printf(CCHK_CORRECTED "auto-correction: cleared sprite[%d].cstat bits 16 and 32\n", i);
                    }
                    else if (1 >= printfromlev)
                        OSD_Printf("   suggest clearing sprite[%d].cstat bits 16 and 32\n", i);
                }
            }
        }

        if (klabs(sprite[i].x) > BXY_MAX || klabs(sprite[i].y) > BXY_MAX)
        {
            const int32_t onumct = numcorruptthings;

            CORRUPTCHK_PRINT(3, CORRUPT_SPRITE|i, "SPRITE %d at [%d, %d] is outside the maximal grid range [%d, %d]",
                             i, TrackerCast(sprite[i].x), TrackerCast(sprite[i].y), -BXY_MAX, BXY_MAX);

            if (onumct < MAXCORRUPTTHINGS)
            {
                int32_t x=0, y=0, ok=0;
                const int32_t sect = sprite[i].sectnum;

                if ((unsigned)sect < (unsigned)numsectors)
                {
                    const int32_t firstwall = sector[sect].wallptr;

                    if ((unsigned)firstwall < (unsigned)numwalls)
                    {
                        x = wall[firstwall].x;
                        y = wall[firstwall].y;
                        ok = 1;
                    }
                }

                if (!(tryfixing & (1ull<<onumct)))
                {
                    if (ok && 3 >= printfromlev)
                        OSD_Printf("   will reposition to its sector's (%d) first"
                                   " point [%d,%d] on tryfix\n", sect, x, y);
                }
                else
                {
                    if (ok)
                    {
                        sprite[i].x = x;
                        sprite[i].y = y;
                        OSD_Printf(CCHK_CORRECTED "auto-correction: repositioned sprite %d to "
                                   "its sector's (%d) first point [%d,%d]\n", i, sect, x, y);
                    }
                }
            }
        }
    }

    i = check_spritelist_consistency();
    if (i)
        CORRUPTCHK_PRINT(5, i<0?0:(CORRUPT_SPRITE|i), CCHK_PANIC "SPRITE LISTS CORRUPTED: error code %d, s=%d, i=%d!",
                         i, csc_s, csc_i);

    if (0)
    {
too_many_errors:
        if (printfromlev<=errlevel)
            OSD_Printf("!! too many errors, stopping. !!\n");
    }

    errlevel = max(errlevel, bad);

    if (errlevel)
    {
        if (printfromlev<=errlevel)
            OSD_Printf("-- corruption level: %d\n", errlevel);
        if (tryfixing)
            OSD_Printf("--\n");
    }

    if (seen_nextwalls)
    {
        M32COMMON_FREE_SEEN_NEXTWALLS(seen_nextwalls);
        M32COMMON_FREE_SEEN_NEXTWALLS(lastnextwallsource);
    }

    corruptlevel = errlevel;

    return errlevel;
}

#undef M32COMMON_FREE_SEEN_NEXTWALLS
