// Shared Mapster32 simple geometry/math helper VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_GETZRANGE:
            insptr++;
            {
                vec3_t vect;

                vect.x = M32EXEC_GETVAR(*insptr++);
                vect.y = M32EXEC_GETVAR(*insptr++);
                vect.z = M32EXEC_GETVAR(*insptr++);

                {
                    int32_t sectnum=M32EXEC_GETVAR(*insptr++);
                    int32_t ceilzvar=*insptr++, ceilhitvar=*insptr++, florzvar=*insptr++, florhitvar=*insptr++;
                    int32_t walldist=M32EXEC_GETVAR(*insptr++), clipmask=M32EXEC_GETVAR(*insptr++);
                    int32_t ceilz, ceilhit, florz, florhit;

                    X_ERROR_INVALIDSECT(sectnum);
                    getzrange(&vect, sectnum, &ceilz, &ceilhit, &florz, &florhit, walldist, clipmask);
                    M32EXEC_SETVAR(ceilzvar, ceilz);
                    M32EXEC_SETVAR(ceilhitvar, ceilhit);
                    M32EXEC_SETVAR(florzvar, florz);
                    M32EXEC_SETVAR(florhitvar, florhit);
                }
                continue;
            }

        case CON_CALCHYPOTENUSE:
            insptr++;
            {
                int32_t retvar=*insptr++;
                int64_t dax=M32EXEC_GETVAR(*insptr++), day=M32EXEC_GETVAR(*insptr++);
                int64_t hypsq = dax*dax + day*day;

                if (hypsq > (int64_t)INT32_MAX)
                    M32EXEC_SETVAR(retvar, (int32_t)sqrt((double)hypsq));
                else
                    M32EXEC_SETVAR(retvar, ksqrt((uint32_t)hypsq));

                continue;
            }

        case CON_LINEINTERSECT:
        case CON_RAYINTERSECT:
            insptr++;
            {
                int32_t x1=M32EXEC_GETVAR(*insptr++), y1=M32EXEC_GETVAR(*insptr++), z1=M32EXEC_GETVAR(*insptr++);
                int32_t x2=M32EXEC_GETVAR(*insptr++), y2=M32EXEC_GETVAR(*insptr++), z2=M32EXEC_GETVAR(*insptr++);
                int32_t x3=M32EXEC_GETVAR(*insptr++), y3=M32EXEC_GETVAR(*insptr++), x4=M32EXEC_GETVAR(*insptr++), y4=M32EXEC_GETVAR(*insptr++);
                int32_t intxvar=*insptr++, intyvar=*insptr++, intzvar=*insptr++, retvar=*insptr++;
                int32_t intx, inty, intz, ret;

                if (tw==CON_LINEINTERSECT)
                    ret = lintersect(x1, y1, z1, x2, y2, z2, x3, y3, x4, y4, &intx, &inty, &intz);
                else
                    ret = rayintersect(x1, y1, z1, x2, y2, z2, x3, y3, x4, y4, &intx, &inty, &intz);

                M32EXEC_SETVAR(retvar, ret);
                if (ret)
                {
                    M32EXEC_SETVAR(intxvar, intx);
                    M32EXEC_SETVAR(intyvar, inty);
                    M32EXEC_SETVAR(intzvar, intz);
                }

                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
