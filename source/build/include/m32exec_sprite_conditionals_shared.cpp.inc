// Shared Mapster32 current-sprite conditional VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SHOW2D_TEST
# define M32EXEC_SHOW2D_TEST(BITMAP, INDEX) bitmap_test((BITMAP), (INDEX))
#endif
#ifndef M32EXEC_DISTPOS
# define M32EXEC_DISTPOS (&pos)
#endif

case CON_IFSPRITEPAL:
            insptr++;
            X_ERROR_INVALIDSP();
            VM_DoConditional(vm.pSprite->pal == M32EXEC_GETVAR(*insptr));
            continue;

        case CON_IFHIGHLIGHTED:
            insptr++;
            {
                int32_t id=*insptr++, index=M32EXEC_GETVAR(*insptr);

                if (index<0 || (id==M32_SPRITE_VAR_ID && index>=MAXSPRITES) || (id==M32_WALL_VAR_ID && index>=numwalls))
                {
                    M32_ERROR("%s index %d out of range!", id==M32_SPRITE_VAR_ID?"Sprite":"Wall", index);
                    continue;
                }

                if (id==M32_SPRITE_VAR_ID)
                    VM_DoConditional(M32EXEC_SHOW2D_TEST(show2dsprite, index));
                else
                    VM_DoConditional(M32EXEC_SHOW2D_TEST(show2dwall, index));
            }
            continue;

        case CON_IFANGDIFFL:
            insptr++;
            {
                int32_t j;
                X_ERROR_INVALIDSP();
                j = klabs(G_GetAngleDelta(ang, vm.pSprite->ang));
                VM_DoConditional(j <= M32EXEC_GETVAR(*insptr));
            }
            continue;

        case CON_IFAWAYFROMWALL:
        {
            int16_t s1;
            int32_t j = 0;

            X_ERROR_INVALIDSP();
            s1 = vm.pSprite->sectnum;
            updatesector(vm.pSprite->x+108,vm.pSprite->y+108,&s1);
            if (s1 == vm.pSprite->sectnum)
            {
                updatesector(vm.pSprite->x-108,vm.pSprite->y-108,&s1);
                if (s1 == vm.pSprite->sectnum)
                {
                    updatesector(vm.pSprite->x+108,vm.pSprite->y-108,&s1);
                    if (s1 == vm.pSprite->sectnum)
                    {
                        updatesector(vm.pSprite->x-108,vm.pSprite->y+108,&s1);
                        if (s1 == vm.pSprite->sectnum)
                            j = 1;
                    }
                }
            }
            VM_DoConditional(j);
        }
        continue;

        case CON_IFCANSEE:
        {
            int32_t j;

            X_ERROR_INVALIDSP();
            j = cansee(vm.pSprite->x,vm.pSprite->y,vm.pSprite->z/*-((krand()&41)<<8)*/,vm.pSprite->sectnum,
                       pos.x, pos.y, pos.z /*-((krand()&41)<<8)*/, cursectnum);
            VM_DoConditional(j);
        }
        continue;

        case CON_IFONWATER:
            X_ERROR_INVALIDSP();
            VM_DoConditional(sector[vm.pSprite->sectnum].lotag == 1 && klabs(vm.pSprite->z-sector[vm.pSprite->sectnum].floorz) < (32<<8));
            continue;

        case CON_IFINWATER:
            X_ERROR_INVALIDSP();
            VM_DoConditional(sector[vm.pSprite->sectnum].lotag == 2);
            continue;

        case CON_IFACTOR:
            insptr++;
            X_ERROR_INVALIDSP();
            VM_DoConditional(vm.pSprite->picnum == M32EXEC_GETVAR(*insptr));
            continue;

        case CON_IFINSIDE:
            insptr++;
            {
                int32_t x=M32EXEC_GETVAR(*insptr++), y=M32EXEC_GETVAR(*insptr++), sectnum=M32EXEC_GETVAR(*insptr++), res;

                res = inside(x, y, sectnum);
                if (res == -1)
                {
                    M32_ERROR("Sector index %d out of range!", sectnum);
                    continue;
                }
                insptr--;
                VM_DoConditional(res);
            }
            continue;

        case CON_IFOUTSIDE:
            X_ERROR_INVALIDSP();
            VM_DoConditional(sector[vm.pSprite->sectnum].ceilingstat&1);
            continue;

        case CON_IFPDISTL:
            insptr++;
            {
                X_ERROR_INVALIDSP();
                VM_DoConditional(dist(M32EXEC_DISTPOS, vm.pSprite) < M32EXEC_GETVAR(*insptr));
            }
            continue;

        case CON_IFPDISTG:
            insptr++;
            {
                X_ERROR_INVALIDSP();
                VM_DoConditional(dist(M32EXEC_DISTPOS, vm.pSprite) > M32EXEC_GETVAR(*insptr));
            }
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SHOW2D_TEST
#undef M32EXEC_DISTPOS
