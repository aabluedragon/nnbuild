// Shared Mapster32 sprite construction and link helper VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif
#ifndef M32EXEC_USPRITE_FROM_TSPRITE
# define M32EXEC_USPRITE_FROM_TSPRITE(TSP) ((uspriteptr_t)(TSP))
#endif

case CON_INSERTSPRITE:
            insptr++;
            {
                int32_t dasectnum = M32EXEC_GETVAR(*insptr++), ret;

                X_ERROR_INVALIDSECT(dasectnum);
                if (Numsprites >= MAXSPRITES)
                {
                    M32_ERROR("Maximum number of sprites reached.");
                    continue;
                }

                ret = insertsprite(dasectnum, 0);
                vm.spriteNum = ret;
                vm.pSprite = &sprite[ret];
            }
            continue;

        case CON_DUPSPRITE:
        case CON_TDUPSPRITE:
            insptr++;
            {
                int32_t ospritenum = M32EXEC_GETVAR(*insptr++), nspritenum;

                if (ospritenum<0 || ospritenum>=MAXSPRITES || sprite[ospritenum].statnum==MAXSTATUS)
                {
                    M32_ERROR("Tried to duplicate nonexistent sprite %d", ospritenum);
                }
                if ((tw==CON_DUPSPRITE && Numsprites >= MAXSPRITES) ||
                        (tw==CON_DUPSPRITE && spritesortcnt >= maxspritesonscreen))
                {
                    M32_ERROR("Maximum number of sprites reached.");
                }

                if (vm.flags&VMFLAG_ERROR)
                    continue;

                if (tw==CON_DUPSPRITE)
                {
                    nspritenum = insertsprite(sprite[ospritenum].sectnum, sprite[ospritenum].statnum);

                    if (nspritenum < 0)
                    {
                        M32_ERROR("Internal error.");
                        continue;
                    }

                    Bmemcpy(&sprite[nspritenum], &sprite[ospritenum], sizeof(spritetype));
                    vm.spriteNum = nspritenum;
                    vm.pSprite = &sprite[nspritenum];
                }
                else
                {
                    tspriteptr_t tsp = renderAddTSpriteFromSprite(ospritenum);
                    vm.spriteNum = -1;
                    EDUKE32_STATIC_ASSERT(sizeof(uspritetype) == sizeof(tspritetype)); // see TSPRITE_SIZE
                    vm.pUSprite = M32EXEC_USPRITE_FROM_TSPRITE(tsp);
                }
            }
            continue;

        case CON_DELETESPRITE:
            insptr++;
            {
                int32_t daspritenum = M32EXEC_GETVAR(*insptr++), ret;

                X_ERROR_INVALIDSPRI(daspritenum);
                ret = deletesprite(daspritenum);
                g_iReturnVar = ret;
            }
            continue;

        case CON_GETSPRITELINKTYPE:
            insptr++;
            {
                int32_t spritenum=M32EXEC_GETVAR(*insptr++), resvar = *insptr++;

                X_ERROR_INVALIDSPRI(spritenum);
                M32EXEC_SETVAR(resvar, taglab_linktags(1, spritenum));
            }
            continue;

        case CON_LASTWALL:
            insptr++;
            {
                int32_t dapoint = M32EXEC_GETVAR(*insptr++), resvar=*insptr++;

                if (dapoint<0 || dapoint>=numwalls)
                {
                    M32_ERROR("Invalid wall %d", dapoint);
                    continue;
                }

                M32EXEC_SETVAR(resvar, lastwall(dapoint));
            }
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
#undef M32EXEC_USPRITE_FROM_TSPRITE
