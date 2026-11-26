// Shared Mapster32 sector collection and array sorting VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif
#ifndef M32EXEC_SECTBITMAP_SIZE
# define M32EXEC_SECTBITMAP_SIZE bitmap_size(MAXSECTORS)
#endif
#ifndef M32EXEC_SECTBITMAP_TEST
# define M32EXEC_SECTBITMAP_TEST(BITMAP, INDEX) bitmap_test((BITMAP), (INDEX))
#endif

case CON_COLLECTSECTORS:
            insptr++;
            {
                const int32_t aridx=*insptr++, startsectnum=M32EXEC_GETVAR(*insptr++);
                const int32_t numsectsVar=*insptr++, state=*insptr++;

                if (CheckArray(aridx))
                    continue;

                gamearray_t *const gar = &aGameArrays[aridx];
                Bassert((gar->dwFlags & (GAMEARRAY_READONLY|GAMEARRAY_VARSIZE)) == 0);

                const int32_t o_g_st=vm.g_st, arsize = gar->size;
                instype *const end=insptr;
                int16_t sectcnt, numsects=0;

                // XXX: relies on -fno-strict-aliasing
                int16_t *const sectlist = (int16_t *)gar->vals;  // actually an int32_t array
                int32_t *const sectlist32 = (int32_t *)sectlist;

                int32_t j, startwall, endwall, ns;
                static uint8_t sectbitmap[M32EXEC_SECTBITMAP_SIZE];

                X_ERROR_INVALIDSECT(startsectnum);
                if (arsize < numsectors)
                {
                    M32_ERROR("Array size must be at least numsectors (=%d) for collecting!",
                              numsectors);
                    continue;
                }

                // collect!
                bfirst_search_init(sectlist, sectbitmap, &numsects, MAXSECTORS, startsectnum);

                for (sectcnt=0; sectcnt<numsects; sectcnt++)
                    for (WALLS_OF_SECTOR(sectlist[sectcnt], j))
                        if ((ns=wall[j].nextsector) >= 0 && wall[j].nextsector<numsectors)
                        {
                            if (M32EXEC_SECTBITMAP_TEST(sectbitmap, ns))
                                continue;
                            vm.g_st = 1+MAXEVENTS+state;
                            insptr = apScript + statesinfo[state].ofs;
                            g_iReturnVar = ns;
                            VM_Execute(0);
                            if (g_iReturnVar)
                                bfirst_search_try(sectlist, sectbitmap, &numsects, wall[j].nextsector);
                        }

                // short->int sector list
                for (j=numsects-1; j>=0; j--)
                    sectlist32[j] = sectlist[j];

                M32EXEC_SETVAR(numsectsVar, numsects);
                g_iReturnVar = 0;

                // restore some VM state
                vm.g_st = o_g_st;
                insptr = end;
            }
            continue;

        case CON_SORT:
            insptr++;
            {
                const int32_t aridx=*insptr++, count=M32EXEC_GETVAR(*insptr++), state=*insptr++;
                const int32_t o_g_st = vm.g_st;
                instype *const end = insptr;

                if (CheckArray(aridx))
                    continue;

                if (count <= 0)
                    continue;

                gamearray_t *const gar = &aGameArrays[aridx];
                Bassert((gar->dwFlags & (GAMEARRAY_READONLY|GAMEARRAY_VARSIZE)) == 0);

                if (count > gar->size)
                {
                    M32_ERROR("Count of elements to sort (%d) exceeds array size (%d)!",
                              count, gar->size);
                    continue;
                }

                if (state < 0)
                {
                    qsort(gar->vals, count, sizeof(int32_t), X_DoSortDefault);
                }
                else
                {
                    x_sortingstateptr = apScript + statesinfo[state].ofs;
                    vm.g_st = 1+MAXEVENTS+state;
                    qsort(gar->vals, count, sizeof(int32_t), X_DoSort);
                    vm.g_st = o_g_st;
                    insptr = end;
                }
            }
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
#undef M32EXEC_SECTBITMAP_SIZE
#undef M32EXEC_SECTBITMAP_TEST
