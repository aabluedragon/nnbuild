// Shared Mapster32 time/date, logging, and debug VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif
#ifndef M32EXEC_DEBUG_PRINT
# define M32EXEC_DEBUG_PRINT(VALUE) initprintf("%d\n", (VALUE))
#endif

case CON_GETTIMEDATE:
            insptr++;
            {
                int32_t v1=*insptr++,v2=*insptr++,v3=*insptr++,v4=*insptr++,v5=*insptr++,v6=*insptr++,v7=*insptr++,v8=*insptr++;
                time_t rawtime;
                struct tm *ti;

                time(&rawtime);
                ti = localtime(&rawtime);
                // initprintf("Time&date: %s\n",asctime (ti));

                M32EXEC_SETVAR(v1, ti->tm_sec);
                M32EXEC_SETVAR(v2, ti->tm_min);
                M32EXEC_SETVAR(v3, ti->tm_hour);
                M32EXEC_SETVAR(v4, ti->tm_mday);
                M32EXEC_SETVAR(v5, ti->tm_mon);
                M32EXEC_SETVAR(v6, ti->tm_year+1900);
                M32EXEC_SETVAR(v7, ti->tm_wday);
                M32EXEC_SETVAR(v8, ti->tm_yday);
                continue;
            }

        case CON_ADDLOG:
        {
            insptr++;

            OSD_Printf("L=%d\n", g_errorLineNum);
            continue;
        }

        case CON_ADDLOGVAR:
            insptr++;
            {
                char buf[80] = "", buf2[80] = "";
                int32_t code = (int32_t)*insptr, val = M32EXEC_GETVAR(code);
                int32_t negate=code&M32_FLAG_NEGATE;

                if (code & (0xFFFFFFFF-(MAXGAMEVARS-1)))
                {
                    if ((code&M32_VARTYPE_MASK)==M32_FLAG_ARRAY || (code&M32_VARTYPE_MASK)==M32_FLAG_STRUCT)
                    {
                        if (code&M32_FLAG_CONSTANT)
                            Bsprintf(buf2, "%d", (code>>16)&0xffff);
                        else
                        {
                            char *label = aGameVars[(code>>16)&(MAXGAMEVARS-1)].szLabel;
                            Bsprintf(buf2, "%s", label?label:"???");
                        }
                    }
                    else if ((code&M32_VARTYPE_MASK)==M32_FLAG_LOCAL)
                        Bsprintf(buf2, "%d", code&(MAXGAMEVARS-1));

                    if ((code&0x0000FFFC) == M32_FLAG_CONSTANT) // addlogvar for a constant.. why not? :P
                    {
                        switch (code&3)
                        {
                        case 0: Bsprintf(buf, "(immediate constant)"); break;
                        case 1: Bsprintf(buf, "(indirect constant)"); break;
                        case 2: Bsprintf(buf, "(label constant)"); break;
                        default: Bsprintf(buf, "(??? constant)"); break;
                        }
                    }
                    else
                    {
                        switch (code&M32_VARTYPE_MASK)
                        {
                        case M32_FLAG_ARRAY:
                            Bsnprintf(buf, sizeof(buf), "%s[%s]", aGameArrays[code&(MAXGAMEARRAYS-1)].szLabel
                                      ? aGameArrays[code&(MAXGAMEARRAYS-1)].szLabel : "???", buf2);
                            break;
                        case M32_FLAG_STRUCT:
                        {
                            int32_t memberid=(code>>2)&63, lightp = (memberid >= LIGHT_X);
                            const char *pp1[4] = {"sprite","sector","wall","tsprite"};
                            const memberlabel_t *pp2[4] = {SpriteLabels, SectorLabels, WallLabels, SpriteLabels};
                            if (lightp)
                            {
                                pp1[3] = "light";
                                pp2[3] = LightLabels;
                                memberid -= LIGHT_X;
                            }

                            Bsnprintf(buf, sizeof(buf), "%s[%s].%s", pp1[code&3], buf2, pp2[code&3][memberid].name);
                        }
                        break;
                        case M32_FLAG_VAR:
                            Bstrcpy(buf, "???");
                            break;
                        case M32_FLAG_LOCAL:
                            Bsnprintf(buf, sizeof(buf), ".local[%s]", buf2);
                            break;
                        }
                    }
                }
                else
                {
                    if (aGameVars[code].dwFlags & GAMEVAR_PERBLOCK)
                    {
                        Bsprintf(buf2, "(%s", vm.g_st==0? "top-level) " : vm.g_st<=MAXEVENTS? "event" : "state");
                        if (vm.g_st >= 1+MAXEVENTS && vm.g_st <1+MAXEVENTS+g_stateCount)
                            Bsprintf(buf, " `%s') ", statesinfo[vm.g_st-1-MAXEVENTS].name);
                        else if (vm.g_st > 0)
                            Bsprintf(buf, " %d) ", vm.g_st-1);
                        Bstrcat(buf2, buf);
                    }

                    Bsnprintf(buf, sizeof(buf), "%s%s", buf2, aGameVars[code].szLabel ? aGameVars[code].szLabel : "???");
                }

                OSD_Printf("L%d: %s%s=%d\n", g_errorLineNum, negate?"-":"", buf, val);

                insptr++;
                continue;
            }

        case CON_DEBUG:
            insptr++;
            M32EXEC_DEBUG_PRINT(*insptr++);
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
#undef M32EXEC_DEBUG_PRINT
