// Shared Mapster32 VM cases for editor field access used by Duke, RR, and Blood editors.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif
#ifndef M32EXEC_STRUCT_TRACKERS
# ifdef USE_STRUCT_TRACKERS
#  define M32EXEC_STRUCT_TRACKERS 1
# elif defined(STRUCT_TRACKERS_ENABLED)
#  define M32EXEC_STRUCT_TRACKERS 1
# else
#  define M32EXEC_STRUCT_TRACKERS 0
# endif
#endif

case CON_GETTICKS:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, timerGetTicks());
            }
            continue;

        case CON_SETASPECT:
            insptr++;
            {
                int32_t daxrange = M32EXEC_GETVAR(*insptr++), dayxaspect = M32EXEC_GETVAR(*insptr++);
                if (daxrange < (1<<12)) daxrange = (1<<12);
                if (daxrange > (1<<20)) daxrange = (1<<20);
                if (dayxaspect < (1<<12)) dayxaspect = (1<<12);
                if (dayxaspect > (1<<20)) dayxaspect = (1<<20);
                renderSetAspect(daxrange, dayxaspect);
                continue;
            }

// vvv CURSPR
        case CON_SETI:
        {
            int32_t newcurspritei;

            insptr++;
            newcurspritei = M32EXEC_GETVAR(*insptr++);
            X_ERROR_INVALIDSPRI(newcurspritei);
            vm.spriteNum = newcurspritei;
            vm.pSprite = &sprite[vm.spriteNum];
            continue;
        }

        case CON_SIZEAT:
            insptr += 3;
            X_ERROR_INVALIDSP();
            vm.pSprite->xrepeat = (uint8_t) M32EXEC_GETVAR(*(insptr-2));
            vm.pSprite->yrepeat = (uint8_t) M32EXEC_GETVAR(*(insptr-1));
#if M32EXEC_STRUCT_TRACKERS
            if (vm.spriteNum != -1) spritechanged[vm.spriteNum]++;
#endif
            continue;

        case CON_CSTAT:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.pSprite->cstat = (int16_t) *(insptr-1);
#if M32EXEC_STRUCT_TRACKERS
            if (vm.spriteNum != -1) spritechanged[vm.spriteNum]++;
#endif
            continue;

        case CON_CSTATOR:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.pSprite->cstat |= (int16_t) M32EXEC_GETVAR(*(insptr-1));
#if M32EXEC_STRUCT_TRACKERS
            if (vm.spriteNum != -1) spritechanged[vm.spriteNum]++;
#endif
            continue;

        case CON_CLIPDIST:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.pSprite->clipdist = (uint8_t) M32EXEC_GETVAR(*(insptr-1));
#if M32EXEC_STRUCT_TRACKERS
            if (vm.spriteNum != -1) spritechanged[vm.spriteNum]++;
#endif
            continue;

        case CON_SPRITEPAL:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.pSprite->pal = M32EXEC_GETVAR(*(insptr-1));
#if M32EXEC_STRUCT_TRACKERS
            if (vm.spriteNum != -1) spritechanged[vm.spriteNum]++;
#endif
            continue;

        case CON_CACTOR:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.pSprite->picnum = M32EXEC_GETVAR(*(insptr-1));
#if M32EXEC_STRUCT_TRACKERS
            if (vm.spriteNum != -1) spritechanged[vm.spriteNum]++;
#endif
            continue;

        case CON_SPGETLOTAG:
            insptr++;
            X_ERROR_INVALIDSP();
            M32EXEC_SETVAR(M32_LOTAG_VAR_ID, vm.pSprite->lotag);
            continue;

        case CON_SPGETHITAG:
            insptr++;
            X_ERROR_INVALIDSP();
            M32EXEC_SETVAR(M32_HITAG_VAR_ID, vm.pSprite->hitag);
            continue;

        case CON_SECTGETLOTAG:
            insptr++;
            X_ERROR_INVALIDSP();
            M32EXEC_SETVAR(M32_LOTAG_VAR_ID, sector[vm.pSprite->sectnum].lotag);
            continue;

        case CON_SECTGETHITAG:
            insptr++;
            X_ERROR_INVALIDSP();
            M32EXEC_SETVAR(M32_HITAG_VAR_ID, sector[vm.pSprite->sectnum].hitag);
            continue;

        case CON_GETTEXTUREFLOOR:
            insptr++;
            X_ERROR_INVALIDSP();
            M32EXEC_SETVAR(M32_TEXTURE_VAR_ID, sector[vm.pSprite->sectnum].floorpicnum);
            continue;

        case CON_GETTEXTURECEILING:
            insptr++;
            X_ERROR_INVALIDSP();
            M32EXEC_SETVAR(M32_TEXTURE_VAR_ID, sector[vm.pSprite->sectnum].ceilingpicnum);
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
#undef M32EXEC_STRUCT_TRACKERS
