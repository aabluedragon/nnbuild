// Shared Mapster32 movement, hitscan, visibility, and point-rotation VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif
#ifndef M32EXEC_HIT_X
# define M32EXEC_HIT_X(HIT) ((HIT).x)
#endif
#ifndef M32EXEC_HIT_Y
# define M32EXEC_HIT_Y(HIT) ((HIT).y)
#endif
#ifndef M32EXEC_HIT_Z
# define M32EXEC_HIT_Z(HIT) ((HIT).z)
#endif

case CON_CLIPMOVE:
            insptr++;
            {
                vec3_t vect;
                int32_t retvar=*insptr++, xvar=*insptr++, yvar=*insptr++, z=M32EXEC_GETVAR(*insptr++), sectnumvar=*insptr++;
                int32_t xvect=M32EXEC_GETVAR(*insptr++), yvect=M32EXEC_GETVAR(*insptr++);
                int32_t walldist=M32EXEC_GETVAR(*insptr++), floordist=M32EXEC_GETVAR(*insptr++), ceildist=M32EXEC_GETVAR(*insptr++);
                int32_t clipmask=M32EXEC_GETVAR(*insptr++);
                int16_t sectnum;

                vect.x = M32EXEC_GETVAR(xvar);
                vect.y = M32EXEC_GETVAR(yvar);
                vect.z = z;
                sectnum = M32EXEC_GETVAR(sectnumvar);

                X_ERROR_INVALIDSECT(sectnum);

                M32EXEC_SETVAR(retvar, clipmove(&vect, &sectnum, xvect, yvect, walldist, floordist, ceildist, clipmask));
                M32EXEC_SETVAR(sectnumvar, sectnum);
                M32EXEC_SETVAR(xvar, vect.x);
                M32EXEC_SETVAR(yvar, vect.y);

                continue;
            }

        case CON_HITSCAN:
            insptr++;
            {
                vec3_t vect;
                hitdata_t hit;

                vect.x = M32EXEC_GETVAR(*insptr++);
                vect.y = M32EXEC_GETVAR(*insptr++);
                vect.z = M32EXEC_GETVAR(*insptr++);

                {
                    int32_t sectnum=M32EXEC_GETVAR(*insptr++);
                    int32_t vx=M32EXEC_GETVAR(*insptr++), vy=M32EXEC_GETVAR(*insptr++), vz=M32EXEC_GETVAR(*insptr++);
                    int32_t hitsectvar=*insptr++, hitwallvar=*insptr++, hitspritevar=*insptr++;
                    int32_t hitxvar=*insptr++, hityvar=*insptr++, hitzvar=*insptr++, cliptype=M32EXEC_GETVAR(*insptr++);

                    X_ERROR_INVALIDSECT(sectnum);
                    hitscan((const vec3_t *)&vect, sectnum, vx, vy, vz, &hit, cliptype);
                    M32EXEC_SETVAR(hitsectvar, hit.sect);
                    M32EXEC_SETVAR(hitwallvar, hit.wall);
                    M32EXEC_SETVAR(hitspritevar, hit.sprite);
                    M32EXEC_SETVAR(hitxvar, M32EXEC_HIT_X(hit));
                    M32EXEC_SETVAR(hityvar, M32EXEC_HIT_Y(hit));
                    M32EXEC_SETVAR(hitzvar, M32EXEC_HIT_Z(hit));
                }
                continue;
            }

        case CON_CANSEE:
            insptr++;
            {
                int32_t x1=M32EXEC_GETVAR(*insptr++), y1=M32EXEC_GETVAR(*insptr++), z1=M32EXEC_GETVAR(*insptr++);
                int32_t sect1=M32EXEC_GETVAR(*insptr++);
                int32_t x2=M32EXEC_GETVAR(*insptr++), y2=M32EXEC_GETVAR(*insptr++), z2=M32EXEC_GETVAR(*insptr++);
                int32_t sect2=M32EXEC_GETVAR(*insptr++), rvar=*insptr++;

                X_ERROR_INVALIDSECT(sect1);
                X_ERROR_INVALIDSECT(sect2);

                M32EXEC_SETVAR(rvar, cansee(x1,y1,z1,sect1,x2,y2,z2,sect2));
                continue;
            }

        case CON_ROTATEPOINT:
            insptr++;
            {
                vec2_t pivot = { M32EXEC_GETVAR(*insptr), M32EXEC_GETVAR(*(insptr+1)) };
                vec2_t p = { M32EXEC_GETVAR(*(insptr+2)), M32EXEC_GETVAR(*(insptr+3)) };
                insptr += 4;
                int32_t daang=M32EXEC_GETVAR(*insptr++);
                int32_t x2var=*insptr++, y2var=*insptr++;
                vec2_t p2;

                rotatepoint(pivot,p,daang,&p2);
                M32EXEC_SETVAR(x2var, p2.x);
                M32EXEC_SETVAR(y2var, p2.y);
                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
#undef M32EXEC_HIT_X
#undef M32EXEC_HIT_Y
#undef M32EXEC_HIT_Z
