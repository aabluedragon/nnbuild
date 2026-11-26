// Shared Mapster32 VM setup and validation helpers used by Duke, RR, and Blood editors.

// from macros.h
#define rnd(X) ((krand()>>8)>=(255-(X)))

vmstate_t vm;
vmstate_t vm_default =
{
    -1,   // g_i
    0,    // g_st
    NULL, // g_sp
    0,    // flags
    0,    // miscflags
};

int32_t g_errorLineNum, g_tw;

uint8_t aEventEnabled[MAXEVENTS];

uint32_t m32_drawlinepat=0xffffffff;
int32_t m32_script_expertmode = 0;

instype *insptr;

static instype *x_sortingstateptr;

//#include "m32structures.cpp"

static int CheckArray(int aidx)
{
    if (!(aidx >= 0 && aidx < g_gameArrayCount))
        M32_ERROR("Invalid array %d!", aidx);

    return (vm.flags&VMFLAG_ERROR);
}

#ifdef DEBUGGINGAIDS
void X_Disasm(ofstype beg, int32_t size)
{
    instype *p;

    if (!apScript) return;
    if (beg<0 || beg+size>g_scriptSize) return;

    initprintf("beg=%d, size=%d:  ", beg, size);
    for (p=apScript+beg; p<apScript+beg+size; p++)
    {
        if (*p>>12 && (*p&0xFFF)<CON_END)
            initprintf("%s ", keyw[*p&0xFFF]);
        else
            initprintf("%d ", *p);
    }
    initprintf("\n");
}
#endif

void VM_ScriptInfo(void)
{
    if (apScript)
    {
        instype *p;
        if (insptr)
            for (p=max(insptr-20,apScript); p<min(insptr+20, apScript+g_scriptSize); p++)
            {
                if (p==insptr) initprintf("<<");

                if (*p>>12 && (*p&0xFFF)<CON_END)
                    initprintf("\n%5d: L%5d:  %s ",(int32_t)(p-apScript),(int32_t)(*p>>12),keyw[*p&0xFFF]);
                else initprintf(" %d",*p);

                if (p==insptr) initprintf(">>");
            }
        initprintf(" \n");
        if (vm.spriteNum >= 0)
            initprintf("current sprite: %d\n",vm.spriteNum);
        if (g_tw>=0 && g_tw<CON_END)
            initprintf("g_errorLineNum: %d, g_tw: %s\n",g_errorLineNum,keyw[g_tw]);
        else
            initprintf("g_errorLineNum: %d, g_tw: %d\n",g_errorLineNum,g_tw);
    }
}

void M32_PostScriptExec(void)
{
    if (vm.miscflags&VMFLAG_MISC_UPDATEHL)
    {
        update_highlight();
        vm.miscflags &= ~VMFLAG_MISC_UPDATEHL;
    }

    if (vm.miscflags&VMFLAG_MISC_UPDATEHLSECT)
    {
        update_highlightsector();
        if (!in3dmode())
            ovh_whiteoutgrab(1);
        vm.miscflags &= ~VMFLAG_MISC_UPDATEHLSECT;
    }
}

void VM_OnEvent(int32_t iEventID, int32_t spriteNum)
{
    if (iEventID < 0 || iEventID >= MAXEVENTS)
    {
        M32_PRINTERROR("Invalid event ID");
        return;
    }

    if (aEventOffsets[iEventID] < 0 || !aEventEnabled[iEventID])
    {
        //Bsprintf(g_szBuf,"No event found for %d",iEventID);
        //AddLog(g_szBuf);
        return;
    }

    {
        instype *const oinsptr=insptr;
        vmstate_t vm_backup;
        void *const olocalvars = aGameArrays[M32_LOCAL_ARRAY_ID].vals;
#ifdef M32_LOCALS_VARARRAY
        int32_t localvars[aEventNumLocals[iEventID]];
#else
        int32_t localvars[M32_LOCALS_FIXEDNUM];
#endif

        // Initialize 'state'-local variables to 0.
        if (aEventNumLocals[iEventID] > 0)
            Bmemset(localvars, 0, aEventNumLocals[iEventID]*sizeof(int32_t));

        Bmemcpy(&vm_backup, &vm, sizeof(vmstate_t));

        vm.spriteNum = spriteNum;    // current sprite ID
        if (vm.spriteNum >= 0)
            vm.pSprite = &sprite[vm.spriteNum];

        vm.g_st = 1+iEventID;

        vm.flags = 0;

        insptr = apScript + aEventOffsets[iEventID];

        aGameArrays[M32_LOCAL_ARRAY_ID].vals = localvars;
        VM_Execute(0);
        aGameArrays[M32_LOCAL_ARRAY_ID].vals = olocalvars;

        if (vm.flags&VMFLAG_ERROR)
        {
            aEventEnabled[iEventID] = 0;
            message("ERROR executing %s. Event disabled.", label+(iEventID*MAXLABELLEN));
        }

        M32_PostScriptExec();

        // restore old values...
        Bmemcpy(&vm, &vm_backup, sizeof(vmstate_t));
        insptr = oinsptr;

        //AddLog("End of Execution");
    }
}

static int32_t G_GetAngleDelta(int32_t a,int32_t na)
{
    a &= 2047;
    na &= 2047;

    if (klabs(a-na) < 1024)
    {
//        OSD_Printf("G_GetAngleDelta() returning %d\n",na-a);
        return na-a;
    }

    if (na > 1024) na -= 2048;
    if (a > 1024) a -= 2048;

//    OSD_Printf("G_GetAngleDelta() returning %d\n",na-a);
    return na-a;
}

static inline void __fastcall VM_DoConditional(int32_t condition)
{
    if (condition)
    {
        // skip 'else' pointer.. and...
        insptr+=2;
        VM_Execute(1);
        return;
    }

    insptr++;
    insptr += *insptr;
    if (((*insptr)&0xFFF) == CON_ELSE)
    {
        // else...
        // skip 'else' and...
        insptr+=2;
        VM_Execute(1);
    }
}

static int X_DoSortDefault(const void *lv, const void *rv)
{
    return *(int32_t const *)rv - *(int32_t const *)lv;
}

static int X_DoSort(const void *lv, const void *rv)
{
    m32_sortvar1 = *(int32_t const *)lv;
    m32_sortvar2 = *(int32_t const *)rv;
    insptr = x_sortingstateptr;
    VM_Execute(0);
    return g_iReturnVar;
}

// in interactive execution, allow the current sprite index to be the aimed-at sprite (in 3d mode)
#define X_ERROR_INVALIDCI()                                                                                                 \
    if ((vm.spriteNum < 0 || vm.spriteNum >= MAXSPRITES) &&                                                                 \
        (vm.g_st != 0 || searchstat != 3 || (vm.spriteNum = searchwall, vm.pSprite = &sprite[vm.spriteNum], 0)))            \
    {                                                                                                                       \
        M32_ERROR("Current sprite index invalid!");                                                                         \
        continue;                                                                                                           \
    }

#define X_ERROR_INVALIDSPRI(dasprite)                                                                                       \
    if (dasprite < 0 || dasprite >= MAXSPRITES)                                                                             \
    {                                                                                                                       \
        M32_ERROR("Invalid sprite index %d!", dasprite);                                                                    \
        continue;                                                                                                           \
    }

#define X_ERROR_INVALIDSECT(dasect)                                                                                         \
    if (dasect < 0 || dasect >= numsectors)                                                                                 \
    {                                                                                                                       \
        M32_ERROR("Invalid sector index %d!", dasect);                                                                      \
        continue;                                                                                                           \
    }

#define X_ERROR_INVALIDSP()                                                                                                 \
    if (!vm.pSprite && (vm.g_st != 0 || searchstat != 3 || (vm.pSprite = &sprite[searchwall], 0)))                          \
    {                                                                                                                       \
        M32_ERROR("Current sprite invalid!");                                                                               \
        continue;                                                                                                           \
    }

#define X_ERROR_INVALIDQUOTE(q, array)                                                                                      \
    if (q < 0 || q >= MAXQUOTES)                                                                                            \
    {                                                                                                                       \
        M32_ERROR("Invalid quote number %d!", q);                                                                           \
        continue;                                                                                                           \
    }                                                                                                                       \
    else if (array[q] == NULL)                                                                                              \
    {                                                                                                                       \
        M32_ERROR("Null quote %d!", q);                                                                                     \
        continue;                                                                                                           \
    }

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif

static char *GetMaybeInlineQuote(int32_t quotei)
{
    char *quotetext;
    if (quotei==-1)
    {
        quotetext = (char *)insptr;
        while (*insptr++) /* skip the string */;
    }
    else
    {
        quotei = M32EXEC_GETVAR(quotei);
        do { X_ERROR_INVALIDQUOTE(quotei, apStrings) } while (0);
        if (vm.flags&VMFLAG_ERROR)
            return NULL;
        quotetext = apStrings[quotei];
    }

    return quotetext;
}

#undef M32EXEC_GETVAR
