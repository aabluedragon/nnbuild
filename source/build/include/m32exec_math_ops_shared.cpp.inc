// Shared Mapster32 math helper VM cases after FTOI/ITOF.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_CLAMP:
            insptr++;
            {
                int32_t var=*insptr++, min=M32EXEC_GETVAR(*insptr++), max=M32EXEC_GETVAR(*insptr++);
                int32_t val=M32EXEC_GETVAR(var);

                if (val<min) M32EXEC_SETVAR(var, min);
                else if (val>max) M32EXEC_SETVAR(var, max);
            }
            continue;

        case CON_INV:
            M32EXEC_SETVAR(*(insptr+1), -M32EXEC_GETVAR(*(insptr+1)));
            insptr += 2;
            continue;

        case CON_SQRT:
            insptr++;
            {
                // syntax sqrt <invar> <outvar>
                int32_t lInVarID=*insptr++, lOutVarID=*insptr++;

                M32EXEC_SETVAR(lOutVarID, ksqrt((uint32_t)M32EXEC_GETVAR(lInVarID)));
                continue;
            }

        case CON_LDIST:
        case CON_DIST:
            insptr++;
            {
                int32_t distvar = *insptr++, xvar = M32EXEC_GETVAR(*insptr++), yvar = M32EXEC_GETVAR(*insptr++);

                if (xvar < 0 || xvar >= MAXSPRITES || sprite[xvar].statnum==MAXSTATUS)
                {
                    M32_ERROR("invalid sprite %d", xvar);
                }
                if (yvar < 0 || yvar >= MAXSPRITES || sprite[yvar].statnum==MAXSTATUS)
                {
                    M32_ERROR("invalid sprite %d", yvar);
                }
                if (vm.flags&VMFLAG_ERROR) continue;

                if (tw==CON_DIST)
                    M32EXEC_SETVAR(distvar, dist(&sprite[xvar],&sprite[yvar]));
                else
                    M32EXEC_SETVAR(distvar, ldist(&sprite[xvar],&sprite[yvar]));
                continue;
            }

        case CON_GETANGLE:
            insptr++;
            {
                int32_t angvar = *insptr++;
                int32_t xvar = M32EXEC_GETVAR(*insptr++);
                int32_t yvar = M32EXEC_GETVAR(*insptr++);

                M32EXEC_SETVAR(angvar, getangle(xvar,yvar));
                continue;
            }

        case CON_GETINCANGLE:
            insptr++;
            {
                int32_t angvar = *insptr++;
                int32_t xvar = M32EXEC_GETVAR(*insptr++);
                int32_t yvar = M32EXEC_GETVAR(*insptr++);

                M32EXEC_SETVAR(angvar, G_GetAngleDelta(xvar,yvar));
                continue;
            }

        case CON_A2XY:
        case CON_AH2XYZ:
            insptr++;
            {
                int32_t ang=M32EXEC_GETVAR(*insptr++), horiz=(tw==CON_A2XY)?100:M32EXEC_GETVAR(*insptr++);
                int32_t xvar=*insptr++, yvar=*insptr++;

                int32_t x = sintable[(ang+512)&2047];
                int32_t y = sintable[ang&2047];

                if (tw==CON_AH2XYZ)
                {
                    int32_t zvar=*insptr++, z=0;

                    horiz -= 100;
                    if (horiz)
                    {
                        int32_t veclen = ksqrt(200*200 + horiz*horiz);
                        int32_t dacos = divscale14(200, veclen);

                        x = mulscale14(x, dacos);
                        y = mulscale14(y, dacos);
                        z = divscale14(-horiz, veclen);
                    }

                    M32EXEC_SETVAR(zvar, z);
                }

                M32EXEC_SETVAR(xvar, x);
                M32EXEC_SETVAR(yvar, y);

                continue;
            }

        case CON_MULSCALE:
            insptr++;
            {
                int32_t var1 = *insptr++, var2 = M32EXEC_GETVAR(*insptr++);
                int32_t var3 = M32EXEC_GETVAR(*insptr++), var4 = M32EXEC_GETVAR(*insptr++);

                M32EXEC_SETVAR(var1, mulscale(var2, var3, var4));
                continue;
            }
        case CON_DIVSCALE:
            insptr++;
            {
                int32_t var1 = *insptr++, var2 = M32EXEC_GETVAR(*insptr++);
                int32_t var3 = M32EXEC_GETVAR(*insptr++), var4 = M32EXEC_GETVAR(*insptr++);

                M32EXEC_SETVAR(var1, divscale(var2, var3, var4));
                continue;
            }
        case CON_SCALEVAR:
            insptr++;
            {
                int32_t var1 = *insptr++, var2 = M32EXEC_GETVAR(*insptr++);
                int32_t var3 = M32EXEC_GETVAR(*insptr++), var4 = M32EXEC_GETVAR(*insptr++);

                M32EXEC_SETVAR(var1, scale(var2, var3, var4));
                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
