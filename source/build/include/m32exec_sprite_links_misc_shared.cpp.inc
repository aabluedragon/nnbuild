// Shared Mapster32 sprite linked-list, visibility, and small misc VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_HEADSPRITESTAT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=M32EXEC_GETVAR(*insptr++);
                if (j < 0 || j > MAXSTATUS)
                {
                    M32_ERROR("invalid status list %d", j);
                    continue;
                }
                M32EXEC_SETVAR(i,headspritestat[j]);
                continue;
            }

        case CON_PREVSPRITESTAT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                M32EXEC_SETVAR(i,prevspritestat[j]);
                continue;
            }

        case CON_NEXTSPRITESTAT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                M32EXEC_SETVAR(i,nextspritestat[j]);
                continue;
            }

        case CON_HEADSPRITESECT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSECT(j);
                M32EXEC_SETVAR(i,headspritesect[j]);
                continue;
            }

        case CON_PREVSPRITESECT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                M32EXEC_SETVAR(i,prevspritesect[j]);
                continue;
            }

        case CON_NEXTSPRITESECT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                M32EXEC_SETVAR(i,nextspritesect[j]);
                continue;
            }

        case CON_CANSEESPR:
            insptr++;
            {
                int32_t lVar1 = M32EXEC_GETVAR(*insptr++), lVar2 = M32EXEC_GETVAR(*insptr++), res;

                if (lVar1<0 || lVar1>=MAXSPRITES || sprite[lVar1].statnum==MAXSTATUS)
                {
                    M32_ERROR("Invalid sprite %d", lVar1);
                }
                if (lVar2<0 || lVar2>=MAXSPRITES || sprite[lVar2].statnum==MAXSTATUS)
                {
                    M32_ERROR("Invalid sprite %d", lVar2);
                }

                if (vm.flags&VMFLAG_ERROR) res=0;
                else res=cansee(sprite[lVar1].x,sprite[lVar1].y,sprite[lVar1].z,sprite[lVar1].sectnum,
                                    sprite[lVar2].x,sprite[lVar2].y,sprite[lVar2].z,sprite[lVar2].sectnum);

                M32EXEC_SETVAR(*insptr++, res);
                continue;
            }

        case CON_CHANGESPRITESTAT:
        case CON_CHANGESPRITESECT:
            insptr++;
            {
                int32_t i = M32EXEC_GETVAR(*insptr++);
                int32_t j = M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSPRI(i);
                if (j<0 || j >= (tw==CON_CHANGESPRITESTAT?MAXSTATUS:numsectors))
                {
                    M32_ERROR("Invalid %s: %d", tw==CON_CHANGESPRITESTAT?"statnum":"sector", j);
                    continue;
                }

                if (tw == CON_CHANGESPRITESTAT)
                {
                    if (sprite[i].statnum == j) continue;
                    changespritestat(i,j);
                }
                else
                {
                    if (sprite[i].sectnum == j) continue;
                    changespritesect(i,j);
                }
                continue;
            }

        case CON_DRAGPOINT:
            insptr++;
            {
                int32_t wallnum = M32EXEC_GETVAR(*insptr++), newx = M32EXEC_GETVAR(*insptr++), newy = M32EXEC_GETVAR(*insptr++);

                if (wallnum<0 || wallnum>=numwalls)
                {
                    M32_ERROR("Invalid wall %d", wallnum);
                    continue;
                }
                dragpoint(wallnum,newx,newy,0);
                continue;
            }

        case CON_SECTOROFWALL:
            insptr++;
            {
                int32_t j = *insptr++;
                M32EXEC_SETVAR(j, sectorofwall(M32EXEC_GETVAR(*insptr++)));
            }
            continue;

        case CON_FIXREPEATS:
            insptr++;
            fixrepeats(M32EXEC_GETVAR(*insptr++));
            continue;

        case CON_GETCLOSESTCOL:
            insptr++;
            {
                int32_t r = M32EXEC_GETVAR(*insptr++), g = M32EXEC_GETVAR(*insptr++), b = M32EXEC_GETVAR(*insptr++);
                M32EXEC_SETVAR(*insptr++, paletteGetClosestColor(r, g, b));
                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
