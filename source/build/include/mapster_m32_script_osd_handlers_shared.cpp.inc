// M32 script vvv
static int osdcmd_include(osdcmdptr_t parm)
{
    if (parm->numparms != 1)
        return OSDCMD_SHOWHELP;
    C_Compile(parm->parms[0], 1);
    return OSDCMD_OK;
}

static int osdcmd_scriptinfo(osdcmdptr_t UNUSED(parm))
{
    UNREFERENCED_CONST_PARAMETER(parm);
    C_CompilationInfo();
    return OSDCMD_OK;
}

#ifdef DEBUGGINGAIDS
extern void X_Disasm(ofstype beg, int32_t size);

static int osdcmd_disasm(osdcmdptr_t parm)
{
    int32_t i;

    if (parm->numparms != 2)
        return OSDCMD_SHOWHELP;

    if (!isdigit(parm->parms[1][0]))
        return OSDCMD_SHOWHELP;

    i=atoi_safe(parm->parms[1]);

    if (parm->parms[0][0]=='s')
    {
        if (i>=0 && i<g_stateCount)
            X_Disasm(statesinfo[i].ofs, statesinfo[i].codesize);
    }
    else
    {
        if (i>=0 && i<MAXEVENTS && aEventOffsets[i]>=0)
            X_Disasm(aEventOffsets[i], aEventSizes[i]);
    }
    return OSDCMD_OK;
}
#endif

static int osdcmd_do(osdcmdptr_t parm)
{
    if (parm->numparms==0)
        return OSDCMD_SHOWHELP;

    int32_t onumconstants = g_numSavedConstants;

    intptr_t oscrofs = (g_scriptPtr-apScript);
    int32_t  ofs     = 2 * (parm->numparms > 0);  // true if "do" command
    int32_t  slen    = Bstrlen(parm->raw + ofs);
    char *   tp      = (char *)Xmalloc(slen+2);

    Bmemcpy(tp, parm->raw+ofs, slen);

    // M32script call from 'special functions' menu
    int32_t dontsavehist = (slen == 0 || tp[0] == ' ');

    // needed so that subsequent commands won't execute old stuff.
    tp[slen]   = '\n';
    tp[slen+1] = '\0';

    g_didDefineSomething = 0;

    C_Compile(tp, 0);

    if (parm->numparms >= 0)
        DO_FREE_AND_NULL(tp);

    if (g_numCompilerErrors)
    {
//        g_scriptPtr = script + oscrofs;  // handled in C_Compile()
        Xfree(tp);
        return OSDCMD_OK;
    }

    if (g_didDefineSomething == 0)
    {
        g_numSavedConstants = onumconstants;

        *g_scriptPtr = CON_RETURN + (g_lineNumber<<12);
        g_scriptPtr  = apScript + oscrofs;

        insptr = apScript + oscrofs;
        Bmemcpy(&vm, &vm_default, sizeof(vmstate_t));

        if (in3dmode() && AIMING_AT_SPRITE)
        {
            vm.spriteNum = searchwall;
            vm.pSprite   = &sprite[vm.spriteNum];
        }

        // If OSD is down, that would interfere with user input, so don't consider
        // m32script executed from the console as 'interactive'. Which leaves only
        // that from the 'special functions' menu
        if (OSD_GetRowsCur() < 0)
            vm.miscflags |= VMFLAG_MISC_INTERACTIVE;

        VM_Execute(0);

        M32_PostScriptExec();

        if (!(vm.flags&VMFLAG_ERROR) && !dontsavehist)
            SaveInHistory(parm->raw);

        OSD_Printf(OSDTEXT_GREEN "%s\n", parm->raw);
//        asksave = 1; // handled in Access(Sprite|Sector|Wall)
    }

    return OSDCMD_OK;
}

void M32RunScript(const char *s)
{
    osdfuncparm_t parm;

    parm.numparms = -1;
    parm.raw = s;

    osdcmd_do(&parm);
}

static int osdcmd_endisableevent(osdcmdptr_t parm)
{
    int32_t i, j, enable;

    if (!label) return OSDCMD_OK;

    if (parm->numparms < 1)
    {
        OSD_Printf("--- Defined events:\n");
        for (i=0; i<MAXEVENTS; i++)
            if (aEventOffsets[i] >= 0)
                OSD_Printf("%s (%d): %s\n", label+(i*MAXLABELLEN), i, aEventEnabled[i]?"on":"off");
        return OSDCMD_OK;
    }

    enable = !Bstrcasecmp(parm->name, "enableevent");

    if (parm->numparms == 1)
    {
        if (!Bstrcasecmp(parm->parms[0], "all") || !Bstrcasecmp(parm->parms[0], "a"))
        {
            for (i=0; i<MAXEVENTS; i++)
                aEventEnabled[i] = enable?1:0;
            OSD_Printf("%sabled all events.\n", enable?"En":"Dis");
            return OSDCMD_OK;
        }
    }

    for (i=0; i<parm->numparms; i++)
    {
        char buf[64] = "EVENT_", buf2[64];

        if (isdigit(parm->parms[i][0]))
        {
            j = atoi_safe(parm->parms[i]);
            Bsprintf(buf2, "event %d", j);
        }
        else if (!Bstrncmp(parm->parms[i], "EVENT_", 6))
        {
            j = hash_find(&h_labels, parm->parms[i]);
            Bstrncpyz(buf2, parm->parms[i], sizeof(buf2));
        }
        else
        {
            Bstrncat(buf, parm->parms[i], sizeof(buf)-6-1);
            j = hash_find(&h_labels, buf);
            Bmemcpy(buf2, buf, sizeof(buf2));
        }

        if (j>=0 && j<MAXEVENTS)
        {
            aEventEnabled[j] = enable?1:0;
            OSD_Printf("%sabled %s.\n", enable?"En":"Dis", buf2);
        }
        else
            OSD_Printf("Invalid event %s.\n", buf2);
    }
    return OSDCMD_OK;
}
