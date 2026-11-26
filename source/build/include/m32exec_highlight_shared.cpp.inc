// Shared Mapster32 highlight VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_BITMAP_SET
# define M32EXEC_BITMAP_SET(BITMAP, INDEX) bitmap_set((BITMAP), (INDEX))
#endif
#ifndef M32EXEC_BITMAP_CLEAR
# define M32EXEC_BITMAP_CLEAR(BITMAP, INDEX) bitmap_clear((BITMAP), (INDEX))
#endif

case CON_UPDATEHIGHLIGHT:
            insptr++;
            update_highlight();
            continue;

        case CON_UPDATEHIGHLIGHTSECTOR:
            insptr++;
            update_highlightsector();
            continue;

        case CON_SETHIGHLIGHT:
            insptr++;
            {
                int32_t what=M32EXEC_GETVAR(*insptr++), index=M32EXEC_GETVAR(*insptr++), doset = M32EXEC_GETVAR(*insptr++);

                if (highlightsectorcnt >= 0)
                {
                    M32_ERROR("sector highlight active or pending, cannot highlight sprites/walls");
                    continue;
                }

                if (what&16384)
                {
                    index &= ~16384;
                    if (index < 0 || index>=MAXSPRITES || sprite[index].statnum==MAXSTATUS)
                    {
                        M32_ERROR("Invalid sprite index %d", index);
                        continue;
                    }

                    if (doset)
                        M32EXEC_BITMAP_SET(show2dsprite, index);
                    else
                        M32EXEC_BITMAP_CLEAR(show2dsprite, index);
                }
                else
                {
                    if (index < 0 || index>=numwalls)
                    {
                        M32_ERROR("Invalid wall index %d", index);
                        continue;
                    }

                    if (doset)
                        M32EXEC_BITMAP_SET(show2dwall, index);
                    else
                        M32EXEC_BITMAP_CLEAR(show2dwall, index);
                }

                vm.miscflags |= VMFLAG_MISC_UPDATEHL;

                continue;
            }

        case CON_SETHIGHLIGHTSECTOR:
            insptr++;
            {
                int32_t index=M32EXEC_GETVAR(*insptr++), doset = M32EXEC_GETVAR(*insptr++);

                if (highlightcnt >= 0)
                {
                    M32_ERROR("sprite/wall highlight active or pending, cannot highlight sectors");
                    continue;
                }

                X_ERROR_INVALIDSECT(index);

                if (doset)
                    M32EXEC_BITMAP_SET(hlsectorbitmap, index);
                else
                    M32EXEC_BITMAP_CLEAR(hlsectorbitmap, index);

                vm.miscflags |= VMFLAG_MISC_UPDATEHLSECT;

                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_BITMAP_SET
#undef M32EXEC_BITMAP_CLEAR
