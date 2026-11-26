// Shared Mapster32 neartag, sprite positioning, and slope helper VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_NEARTAG:
            insptr++;
            {
                // neartag(int32_t x, int32_t y, int32_t z, short sectnum, short ang,  //Starting position & angle
                //         short *neartagsector,    //Returns near sector if sector[].tag != 0
                //         short *neartagwall,      //Returns near wall if wall[].tag != 0
                //         short *neartagsprite,    //Returns near sprite if sprite[].tag != 0
                //         int32_t *neartaghitdist, //Returns actual distance to object (scale: 1024=largest grid size)
                //         int32_t neartagrange,    //Choose maximum distance to scan (scale: 1024=largest grid size)
                //         char tagsearch)          //1-lotag only, 2-hitag only, 3-lotag&hitag

                int32_t x=M32EXEC_GETVAR(*insptr++), y=M32EXEC_GETVAR(*insptr++), z=M32EXEC_GETVAR(*insptr++);
                int32_t sectnum=M32EXEC_GETVAR(*insptr++), ang=M32EXEC_GETVAR(*insptr++);
                int32_t neartagsectorvar=*insptr++, neartagwallvar=*insptr++, neartagspritevar=*insptr++, neartaghitdistvar=*insptr++;
                int32_t neartagrange=M32EXEC_GETVAR(*insptr++), tagsearch=M32EXEC_GETVAR(*insptr++);

                int16_t neartagsector, neartagwall, neartagsprite;
                int32_t neartaghitdist;

                X_ERROR_INVALIDSECT(sectnum);
                neartag(x, y, z, sectnum, ang, &neartagsector, &neartagwall, &neartagsprite,
                        &neartaghitdist, neartagrange, tagsearch, NULL);

                M32EXEC_SETVAR(neartagsectorvar, neartagsector);
                M32EXEC_SETVAR(neartagwallvar, neartagwall);
                M32EXEC_SETVAR(neartagspritevar, neartagsprite);
                M32EXEC_SETVAR(neartaghitdistvar, neartaghitdist);
                continue;
            }

        case CON_BSETSPRITE:  // was CON_SETSPRITE
            insptr++;
            {
                int32_t spritenum = M32EXEC_GETVAR(*insptr++);
                vec3_t davector;

                davector.x = M32EXEC_GETVAR(*insptr++);
                davector.y = M32EXEC_GETVAR(*insptr++);
                davector.z = M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSPRI(spritenum);
                setsprite(spritenum, &davector);
                continue;
            }

        case CON_GETFLORZOFSLOPE:
        case CON_GETCEILZOFSLOPE:
            insptr++;
            {
                int32_t sectnum = M32EXEC_GETVAR(*insptr++), x = M32EXEC_GETVAR(*insptr++), y = M32EXEC_GETVAR(*insptr++);
                int32_t var=*insptr++;

                X_ERROR_INVALIDSECT(sectnum);
                if (tw == CON_GETFLORZOFSLOPE)
                    M32EXEC_SETVAR(var, getflorzofslope(sectnum,x,y));
                else
                    M32EXEC_SETVAR(var, getceilzofslope(sectnum,x,y));
                continue;
            }

        case CON_ALIGNFLORSLOPE:
        case CON_ALIGNCEILSLOPE:
            insptr++;
            {
                int32_t sectnum = M32EXEC_GETVAR(*insptr++), x = M32EXEC_GETVAR(*insptr++), y = M32EXEC_GETVAR(*insptr++);
                int32_t z=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSECT(sectnum);
                if (tw == CON_ALIGNFLORSLOPE)
                    alignflorslope(sectnum, x,y,z);
                else
                    alignceilslope(sectnum, x,y,z);
                continue;
            }

// CURSPR
        case CON_SETFIRSTWALL:
            insptr++;
            {
                int32_t sect=M32EXEC_GETVAR(*insptr++), wal=M32EXEC_GETVAR(*insptr++);

                X_ERROR_INVALIDSECT(sect);
                setfirstwall(sect, wal);
            }
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
