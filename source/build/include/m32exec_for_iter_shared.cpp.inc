// Shared Mapster32 special-purpose FOR iteration VM case.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_FOR:  // special-purpose iteration
            insptr++;
            {
                const int32_t var = *insptr++, how = *insptr++;
                const int32_t parm2 = how<=ITER_DRAWNSPRITES ? 0 : M32EXEC_GETVAR(*insptr++);
                instype *const end = insptr + *insptr, *const beg = ++insptr;
                const int32_t vm_i_bak = vm.spriteNum;
                auto const vm_sp_bak = vm.pUSprite;

                if (vm.flags&VMFLAG_ERROR)
                    continue;

                switch (how)
                {
                case ITER_ALLSPRITES:
                    for (bssize_t jj=0; jj<MAXSPRITES && !vm.flags; jj++)
                    {
                        if (sprite[jj].statnum == MAXSTATUS)
                            continue;
                        M32EXEC_SETVAR(var, jj);
                        vm.spriteNum = jj;
                        vm.pSprite = &sprite[jj];
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_ALLSECTORS:
                    for (bssize_t jj=0; jj<numsectors && !vm.flags; jj++)
                    {
                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_ALLWALLS:
                    for (bssize_t jj=0; jj<numwalls && !vm.flags; jj++)
                    {
                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_ACTIVELIGHTS:
#ifdef POLYMER
                    for (bssize_t jj=0; jj<PR_MAXLIGHTS; jj++)
                    {
                        if (!prlights[jj].flags.active)
                            continue;

                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
#else
                    M32_ERROR("Polymer not compiled in, iteration over lights forbidden.");
#endif
                    break;

                case ITER_SELSPRITES:
                    for (bssize_t ii=0; ii<highlightcnt && !vm.flags; ii++)
                    {
                        int jj = highlight[ii];
                        if (jj&0xc000)
                        {
                            jj &= (MAXSPRITES-1);
                            M32EXEC_SETVAR(var, jj);
                            vm.spriteNum = jj;
                            vm.pSprite = &sprite[jj];
                            insptr = beg;
                            VM_Execute(1);
                        }
                    }
                    break;
                case ITER_SELSECTORS:
                    for (bssize_t ii=0; ii<highlightsectorcnt && !vm.flags; ii++)
                    {
                        int jj=highlightsector[ii];
                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_SELWALLS:
                    for (bssize_t ii=0; ii<highlightcnt && !vm.flags; ii++)
                    {
                        int jj=highlight[ii];
                        if (jj&0xc000)
                            continue;
                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_DRAWNSPRITES:
                {
                    uspritetype lastSpriteBackup;
                    auto const lastSpritePtr = (uspritetype *)&sprite[MAXSPRITES-1];

                    // Back up sprite MAXSPRITES-1.
                    Bmemcpy(&lastSpriteBackup, lastSpritePtr, sizeof(uspritetype));

                    EDUKE32_STATIC_ASSERT(sizeof(uspritetype) == sizeof(tspritetype)); // see TSPRITE_SIZE
                    for (bssize_t ii=0; ii<spritesortcnt && !vm.flags; ii++)
                    {
                        vm.pUSprite = lastSpritePtr;
                        Bmemcpy(lastSpritePtr, &tsprite[ii], sizeof(tspritetype));

                        M32EXEC_SETVAR(var, ii);
                        insptr = beg;
                        VM_Execute(1);

                        // Copy over potentially altered tsprite.
                        Bmemcpy(&tsprite[ii], lastSpritePtr, sizeof(tspritetype));
                    }

                    // Restore sprite MAXSPRITES-1.
                    Bmemcpy(lastSpritePtr, &lastSpriteBackup, sizeof(uspritetype));
                    break;
                }
                case ITER_SPRITESOFSECTOR:
                    if (parm2 < 0 || parm2 >= MAXSECTORS)
                        goto badindex;
                    for (bssize_t jj=headspritesect[parm2]; jj>=0 && !vm.flags; jj=nextspritesect[jj])
                    {
                        M32EXEC_SETVAR(var, jj);
                        vm.spriteNum = jj;
                        vm.pSprite = &sprite[jj];
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_WALLSOFSECTOR:
                    if (parm2 < 0 || parm2 >= MAXSECTORS)
                        goto badindex;
                    for (bssize_t jj=sector[parm2].wallptr, endwall=jj+sector[parm2].wallnum-1;
                            jj<=endwall && !vm.flags; jj++)
                    {
                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_LOOPOFWALL:
                    if (parm2 < 0 || parm2 >= numwalls)
                        goto badindex;
                    {
                        int jj = parm2;
                        do
                        {
                            M32EXEC_SETVAR(var, jj);
                            insptr = beg;
                            VM_Execute(1);
                            jj = wall[jj].point2;
                        }
                        while (jj != parm2 && !vm.flags);
                    }
                    break;
                case ITER_RANGE:
                    for (bssize_t jj=0; jj<parm2 && !vm.flags; jj++)
                    {
                        M32EXEC_SETVAR(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                default:
                    M32_ERROR("Unknown iteration type %d!", how);
                    continue;
badindex:
                    OSD_Printf("%sLine %d, %s %s: index %d out of range!\n", osd->draw.highlight,
                        g_errorLineNum,keyw[g_tw], iter_tokens[how].token, parm2);
                    vm.flags |= VMFLAG_ERROR;
                    continue;
                }
                vm.spriteNum = vm_i_bak;
                vm.pUSprite  = vm_sp_bak;
                vm.flags     &= ~VMFLAG_BREAK;
                insptr       = end;
            }
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
