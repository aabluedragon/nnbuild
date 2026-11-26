// Shared Mapster32 variable arithmetic VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_RANDVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr, mulscale16(krand(), *(insptr+1)+1));
            insptr += 2;
            continue;

        case CON_DISPLAYRANDVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr, mulscale15(system_15bit_rand(), *(insptr+1)+1));
            insptr += 2;
            continue;

        case CON_SETVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr, *(insptr+1));
            insptr += 2;
            continue;

        case CON_SETVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_MULVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr, M32EXEC_GETVAR(*insptr) * *(insptr+1));
            insptr += 2;
            continue;

        case CON_DIVVAR:
            insptr++;
            if (*(insptr+1) == 0)
            {
                M32_ERROR("Divide by zero.");
                insptr += 2;
                continue;
            }
            M32EXEC_SETVAR(*insptr, M32EXEC_GETVAR(*insptr) / *(insptr+1));
            insptr += 2;
            continue;

        case CON_MODVAR:
            insptr++;
            if (*(insptr+1) == 0)
            {
                M32_ERROR("Mod by zero.");
                insptr += 2;
                continue;
            }
            M32EXEC_SETVAR(*insptr,M32EXEC_GETVAR(*insptr)%*(insptr+1));
            insptr += 2;
            continue;

        case CON_ANDVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr,M32EXEC_GETVAR(*insptr) & *(insptr+1));
            insptr += 2;
            continue;

        case CON_ORVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr,M32EXEC_GETVAR(*insptr) | *(insptr+1));
            insptr += 2;
            continue;

        case CON_XORVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr,M32EXEC_GETVAR(*insptr) ^ *(insptr+1));
            insptr += 2;
            continue;

        case CON_RANDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j,mulscale16(krand(), M32EXEC_GETVAR(*insptr++)+1));
            }
            continue;

        case CON_DISPLAYRANDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j,mulscale15(system_15bit_rand(), M32EXEC_GETVAR(*insptr++)+1));
            }
            continue;

        case CON_MULVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j)*M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_DIVVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                int32_t l2=M32EXEC_GETVAR(*insptr++);

                if (l2==0)
                {
                    M32_ERROR("Divide by zero.");
                    continue;
                }
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j)/l2);
                continue;
            }

        case CON_MODVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                int32_t l2=M32EXEC_GETVAR(*insptr++);

                if (l2==0)
                {
                    M32_ERROR("Mod by zero.");
                    continue;
                }

                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) % l2);
                continue;
            }

        case CON_ANDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) & M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_XORVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) ^ M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_ORVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) | M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_SUBVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr, M32EXEC_GETVAR(*insptr) - *(insptr+1));
            insptr += 2;
            continue;

        case CON_SUBVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) - M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_ADDVAR:
            insptr++;
            M32EXEC_SETVAR(*insptr, M32EXEC_GETVAR(*insptr) + *(insptr+1));
            insptr += 2;
            continue;

        case CON_ADDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) + M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_SHIFTVARL:
            insptr++;
            M32EXEC_SETVAR(*insptr, M32EXEC_GETVAR(*insptr) << *(insptr+1));
            insptr += 2;
            continue;

        case CON_SHIFTVARVARL:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) << M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_SHIFTVARR:
            insptr++;
            M32EXEC_SETVAR(*insptr, M32EXEC_GETVAR(*insptr) >> *(insptr+1));
            insptr += 2;
            continue;

        case CON_SHIFTVARVARR:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, M32EXEC_GETVAR(j) >> M32EXEC_GETVAR(*insptr++));
            }
            continue;

        case CON_SIN:
            insptr++;
            M32EXEC_SETVAR(*insptr, sintable[M32EXEC_GETVAR(*(insptr+1))&2047]);
            insptr += 2;
            continue;

        case CON_COS:
            insptr++;
            M32EXEC_SETVAR(*insptr, sintable[(M32EXEC_GETVAR(*(insptr+1))+512)&2047]);
            insptr += 2;
            continue;

        case CON_DISPLAYRAND:
            insptr++;
            M32EXEC_SETVAR(*insptr++, system_15bit_rand());
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
