#ifndef M32_OSD_COMMAND_RETURN_TYPE
# define M32_OSD_COMMAND_RETURN_TYPE int
#endif
#ifndef M32_OSD_COMMAND_PARAM
# define M32_OSD_COMMAND_PARAM osdcmdptr_t
#endif

static M32_OSD_COMMAND_RETURN_TYPE osdcmd_addpath(M32_OSD_COMMAND_PARAM parm)
{
    char pathname[BMAX_PATH];

    if (parm->numparms != 1) return OSDCMD_SHOWHELP;

    Bstrcpy(pathname,parm->parms[0]);
    addsearchpath(pathname);
    return OSDCMD_OK;
}

static M32_OSD_COMMAND_RETURN_TYPE osdcmd_initgroupfile(M32_OSD_COMMAND_PARAM parm)
{
    char file[BMAX_PATH];

    if (parm->numparms != 1) return OSDCMD_SHOWHELP;

    Bstrcpy(file,parm->parms[0]);
    initgroupfile(file);
    return OSDCMD_OK;
}

static M32_OSD_COMMAND_RETURN_TYPE osdcmd_sensitivity(M32_OSD_COMMAND_PARAM parm)
{
    if (parm->numparms != 1)
    {
        OSD_Printf("\"sensitivity\" is \"%.2f\"\n",msens);
        return OSDCMD_SHOWHELP;
    }
    msens = atof(parm->parms[0]);
    OSD_Printf("sensitivity %.2f\n",msens);
    return OSDCMD_OK;
}

static M32_OSD_COMMAND_RETURN_TYPE osdcmd_noclip(M32_OSD_COMMAND_PARAM UNUSED(parm))
{
    UNREFERENCED_CONST_PARAMETER(parm);
    m32_clipping--;
    if (m32_clipping < 0)
        m32_clipping = 2;
    OSD_Printf("Clipping %s\n", m32_clipping==0 ? "disabled" :
               (m32_clipping==1 ? "non-masks only" : "enabled"));

    return OSDCMD_OK;
}

static M32_OSD_COMMAND_RETURN_TYPE osdcmd_testplay_addparam(M32_OSD_COMMAND_PARAM parm)
{
    int32_t slen;

    if (parm->numparms != 1)
    {
        OSD_Printf("additional parameters for test playing: %s%s%s\n",
                   testplay_addparam ? "\"" : "",
                   testplay_addparam ? testplay_addparam : "<empty>",
                   testplay_addparam ? "\"" : "");
        return OSDCMD_OK;
    }

    slen = Bstrlen(parm->parms[0]);

    if (slen > 0)
    {
        if (!testplay_addparam)
            testplay_addparam = (char *)Xmalloc(slen+1);
        else
            testplay_addparam = (char *)Xrealloc(testplay_addparam, slen+1);

        Bmemcpy(testplay_addparam, parm->parms[0], slen);
        testplay_addparam[slen] = 0;
    }
    else
    {
        DO_FREE_AND_NULL(testplay_addparam);
    }

    return OSDCMD_OK;
}


//PK vvv ------------
// FIXME: The way the different options are handled is horribly inconsistent.
static M32_OSD_COMMAND_RETURN_TYPE osdcmd_vars_pk(M32_OSD_COMMAND_PARAM parm)
{
    const int32_t setval = (parm->numparms >= 1);

    // this is something of a misnomer, since it's actually accel+decel
    if (!Bstrcasecmp(parm->name, "pk_turnaccel"))
    {
        if (setval)
        {
            pk_turnaccel = atoi_safe(parm->parms[0]);
            pk_turnaccel = pk_turnaccel<=pk_turndecel ? (pk_turndecel+1):pk_turnaccel;
            pk_turnaccel = pk_turnaccel>256 ? 256:pk_turnaccel;
        }

        OSD_Printf("Turning acceleration+declaration is %d\n", pk_turnaccel);
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "pk_turndecel"))
    {
        if (setval)
        {
            pk_turndecel = atoi_safe(parm->parms[0]);
            pk_turndecel = pk_turndecel<=0 ? 1:pk_turndecel;
            pk_turndecel = pk_turndecel>=pk_turnaccel ? (pk_turnaccel-1):pk_turndecel;
            pk_turndecel = pk_turndecel>128 ? 128:pk_turndecel;
        }

        OSD_Printf("Turning deceleration is %d\n", pk_turndecel);
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "pk_quickmapcycling"))
    {
        OSD_Printf("Quick map cycling ((LShift-)Ctrl-X): %s\n",
                   (quickmapcycling = !quickmapcycling) ? "enabled":"disabled");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "pk_uedaccel"))
    {
        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        if (setval)
            pk_uedaccel = clamp(atoi_safe(parm->parms[0]), 0, 5);

        OSD_Printf("UnrealEd mouse navigation acceleration is %d\n", pk_uedaccel);
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "osd_tryscript"))
    {
        m32_osd_tryscript = !m32_osd_tryscript;
        OSD_Printf("Try M32 script execution on invalid OSD command: %s\n", m32_osd_tryscript?"on":"off");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "sideview_reversehorizrot"))
    {
        sideview_reversehrot = !sideview_reversehrot;
        OSD_Printf("Side view reverse horizontal rotation: %s\n", sideview_reversehrot?"on":"off");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "script_expertmode"))
    {
        if (setval)
            m32_script_expertmode = !!atoi_safe(parm->parms[0]);

        if (m32_script_expertmode)
            OSD_Printf("M32 Script expert mode %sENABLED.  Be sure to know what you are doing!\n",
                       setval ? "" : "is ");
        else
            OSD_Printf("M32 Script expert mode %sDISABLED.\n", setval ? "" : "is ");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "fixmaponsave_sprites"))
    {
        OSD_Printf("Fix sprite sectnums on map saving: %s\n",
                   (fixmaponsave_sprites = !fixmaponsave_sprites) ? "enabled":"disabled");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "show_heightindicators"))
    {
        static const char *how[3] = {"none", "two-sided walls only", "all"};

        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        if (setval)
            showheightindicators = clamp(atoi_safe(parm->parms[0]), 0, 2);
        OSD_Printf("height indicators: %s\n", how[showheightindicators]);

        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "show_ambiencesounds"))
    {
        static const char *how[3] = {"none", "current sector only", "all"};

        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        if (setval)
            showambiencesounds = clamp(atoi_safe(parm->parms[0]), 0, 2);
        OSD_Printf("ambience sound circles: %s\n", how[showambiencesounds]);

        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "corruptcheck_noalreadyrefd"))
    {
        corruptcheck_noalreadyrefd = !corruptcheck_noalreadyrefd;
        OSD_Printf("%s 'already referenced' corruption (i.e. one-to-many nextwalls)\n",
                   corruptcheck_noalreadyrefd?"Ignore":"Regard");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "corruptcheck_game_duke3d"))
    {
        corruptcheck_game_duke3d = !corruptcheck_game_duke3d;
        OSD_Printf("%s Duke3D issues\n",
                   !corruptcheck_game_duke3d?"Ignore":"Regard");
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "corruptcheck_heinum"))
    {
        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        static const char *mode[3] = {"disabled", "auto-correct only", "auto-correct and warn"};

        if (setval)
            corruptcheck_heinum = clamp(atoi_safe(parm->parms[0]), 0, 2);

        OSD_Printf("Check inconsistent ceilingstat/floorstat bit 2 and .heinum: %s\n",
                   mode[corruptcheck_heinum]);

        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "keeptexturestretch"))
    {
        keeptexturestretch = !keeptexturestretch;
        OSD_Printf("Keep texture stretching when dragging wall vertices: %s\n",
                   ONOFF(keeptexturestretch));
        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "m32_2d3dmode"))
    {
        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        if (setval)
            m32_2d3dmode = atoi_safe(parm->parms[0]);

        OSD_Printf("Experimental 2d/3d hybrid mode: %s\n",
            ONOFF(m32_2d3dmode));

        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "pointhighlightdist"))
    {
        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        if (setval)
            pointhighlightdist = atoi_safe(parm->parms[0]);

        OSD_Printf("Point highlight distance: %d\n", pointhighlightdist);

        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "linehighlightdist"))
    {
        if (parm->numparms > 1)
            return OSDCMD_SHOWHELP;

        if (setval)
            linehighlightdist = atoi_safe(parm->parms[0]);

        OSD_Printf("Line highlight distance: %d\n", linehighlightdist);

        return OSDCMD_OK;
    }

    if (!Bstrcasecmp(parm->name, "corruptcheck"))
    {
        if (parm->numparms >= 1)
        {
            if (!Bstrcasecmp(parm->parms[0], "now"))
            {
                int32_t printfromlevel = 1;
                if (parm->numparms > 1)
                    printfromlevel = clamp(atoi_safe(parm->parms[1]), 1, 5);
                if (CheckMapCorruption(printfromlevel, 0)==0)
                    OSD_Printf("All OK.\n");
                return OSDCMD_OK;
            }

            if (!Bstrcasecmp(parm->parms[0], "tryfix"))
            {
                uint64_t whicherrs = parm->numparms==1 ? 0xffffffffffffffffull : 0;
                corrupt_tryfix_alt = 0;

                if (whicherrs==0)
                {
                    int32_t i, n, m;
                    char *endptr;
                    for (i=1; i<parm->numparms; i++)
                    {
                        if (i==parm->numparms-1)
                        {
                            if (!Bstrcmp(parm->parms[i], "??"))
                            {
                                corrupt_tryfix_alt = 2;
                                if (parm->numparms==2)
                                    whicherrs = 0xffffffffffffffffull;
                                break;
                            }
                            else if (!Bstrcmp(parm->parms[i], "?"))
                            {
                                corrupt_tryfix_alt = 1;
                                if (parm->numparms==2)
                                    whicherrs = 0xffffffffffffffffull;
                                break;
                            }
                        }

                        n = (int32_t)Bstrtol(parm->parms[i], &endptr, 10);
                        if (endptr != parm->parms[i])
                        {
                            if (*endptr=='-')
                            {
                                m = (int32_t)Bstrtol(endptr+1, NULL, 10);
                                if (n>=1 && n<=m && m<=MAXCORRUPTTHINGS)
                                {
                                    uint64_t mask = 0xffffffffffffffffull;
                                    m = m-n+1;
                                    mask >>= (MAXCORRUPTTHINGS-m);
                                    mask <<= (n-1);

                                    whicherrs |= mask;
                                }
                            }
                            else
                            {
                                if (n>=1 && n<=MAXCORRUPTTHINGS)
                                    whicherrs |= (1ull<<(n-1));
                            }
                        }
                    }
                }

                CheckMapCorruption(whicherrs?5:3, whicherrs);
                return OSDCMD_OK;
            }

            if (isdigit(parm->parms[0][0]))
            {
                autocorruptcheck = clamp(atoi_safe(parm->parms[0]), 0, 3600);
                corruptchecktimer = (int32_t) totalclock + 120*autocorruptcheck;
            }

            return OSDCMD_OK;
        }

        if (parm->numparms <= 1)
        {
            if (autocorruptcheck)
                OSD_Printf("auto corruption check: %d seconds\n", autocorruptcheck);
            else
                OSD_Printf("auto corruption check: off\n");
            return OSDCMD_OK;
        }

        return OSDCMD_SHOWHELP;
    }

    return OSDCMD_OK;
}


#undef M32_OSD_COMMAND_RETURN_TYPE
#undef M32_OSD_COMMAND_PARAM
