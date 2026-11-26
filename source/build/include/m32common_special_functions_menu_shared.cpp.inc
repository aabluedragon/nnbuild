////////// SPECIAL FUNCTIONS MENU //////////

#ifndef M32COMMON_FUNC_MENU_FREE_SCRIPT
# define M32COMMON_FUNC_MENU_FREE_SCRIPT(ptr) Xfree(ptr)
#endif

#ifndef M32COMMON_FUNC_MENU_DELETE_SPRITE
# define M32COMMON_FUNC_MENU_DELETE_SPRITE(spriteNum, deletedCount) \
    do { if (deletesprite(spriteNum) == 0) (deletedCount)++; } while (0)
#endif

#ifndef M32COMMON_FUNC_MENU_MAX_TILE_PROMPT
# define M32COMMON_FUNC_MENU_MAX_TILE_PROMPT (MAXTILES-1)
#endif

static void FuncMenu_Process(const StatusBarMenu *m, int32_t col, int32_t row);

static StatusBarMenu g_specialFuncMenu = MENU_INITIALIZER(
    "Special functions", 8, FuncMenu_Process,

    {
        "Replace invalid tiles",
        "Delete all spr of tile #",
        "Set map sky shade",
        "Set map sky height",
        "Global Z coord shift",
        "Resize selection",
        "Global shade divide",
        "Global visibility divide"
    }
);

// "External functions":

void FuncMenu(void)
{
    M_EnterMainLoop(&g_specialFuncMenu);
}

void registerMenuFunction(const char *funcname, int32_t stateidx)
{
    if (funcname)
        M_RegisterFunction(&g_specialFuncMenu, funcname, stateidx, NULL);
    else
        M_UnregisterFunction(&g_specialFuncMenu, stateidx);
}

// The processing function...

static int32_t correct_picnum(int16_t *picnumptr)
{
    int32_t picnum = *picnumptr;

    if ((unsigned)picnum >= MAXTILES || tilesiz[picnum].x <= 0)
    {
        *picnumptr = 0;
        return 1;
    }

    return 0;
}

static void FuncMenu_Process(const StatusBarMenu *m, int32_t col, int32_t row)
{
    int32_t i, j;

    switch (col)
    {

    case 1:
    case 2:
    {
        const int32_t stateidx = (Bassert(m->auxdata[col*8 + row] < g_stateCount),
                                  m->auxdata[col*8 + row]);

        const char *statename = statesinfo[stateidx].name;
        int32_t snlen = Bstrlen(statename);
        char *tmpscript = (char *)Xmalloc(1+5+1+snlen+1);

        tmpscript[0] = ' ';  // don't save in history
        Bmemcpy(&tmpscript[1], "state", 5);
        tmpscript[1+5] = ' ';
        Bmemcpy(&tmpscript[1+5+1], statename, snlen);
        tmpscript[1+5+1+snlen] = 0;

        M32RunScript(tmpscript);
        M32COMMON_FUNC_MENU_FREE_SCRIPT(tmpscript);

        if (vm.flags&VMFLAG_ERROR)
            printmessage16("There were errors while executing the menu function");
        else if (lastpm16time != totalclock)
            printmessage16("Menu function executed successfully");
    }
    break;

    case 0:
    {
        switch (row)
        {

        case 0:
        {
            j = 0;

            for (i=0; i<MAXSECTORS; i++)
            {
                j += correct_picnum(&sector[i].ceilingpicnum);
                j += correct_picnum(&sector[i].floorpicnum);
            }

            for (i=0; i<MAXWALLS; i++)
            {
                j += correct_picnum(&wall[i].picnum);
                j += correct_picnum(&wall[i].overpicnum);
            }
            for (i=0; i<MAXSPRITES; i++)
                j += correct_picnum(&sprite[i].picnum);

            printmessage16("Replaced %d invalid tiles",j);
        }
        break;

        case 1:
        {
            char tempbuf[64];
            Bsprintf(tempbuf,"Delete all sprites of tile #: ");
            i = getnumber16(tempbuf,-1,M32COMMON_FUNC_MENU_MAX_TILE_PROMPT,1);
            if (i >= 0)
            {
                int32_t k = 0;
                for (j=0; j<MAXSPRITES; j++)
                    if (sprite[j].picnum == i)
                        M32COMMON_FUNC_MENU_DELETE_SPRITE(j, k);
                printmessage16("%d sprite(s) deleted",k);
            }
            else printmessage16("Aborted");
        }
        break;

        case 2:
        {
            j=getnumber16("Set map sky shade:    ",0,128,1);

            for (i=0; i<numsectors; i++)
            {
                if (sector[i].ceilingstat&1)
                    sector[i].ceilingshade = j;
            }
            printmessage16("All parallax skies adjusted");
        }
        break;

        case 3:
        {
            j=getnumber16("Set map sky height:    ",0,16777216,1);
            if (j != 0)
            {
                for (i=0; i<numsectors; i++)
                {
                    if (sector[i].ceilingstat&1)
                        sector[i].ceilingz = j;
                }
                printmessage16("All parallax skies adjusted");
            }
            else printmessage16("Aborted");
        }
        break;

        case 4:
        {
            j=getnumber16("Z offset:    ",0,16777216,1);
            if (j!=0)
            {
                for (i=0; i<numsectors; i++)
                {
                    sector[i].ceilingz += j;
                    sector[i].floorz += j;
                }
                for (i=0; i<MAXSPRITES; i++)
                    sprite[i].z += j;
                printmessage16("Map adjusted");
            }
            else printmessage16("Aborted");
        }
        break;

        case 5:
        {
            j=getnumber16("Percentage of original:    ",100,1000,0);

            if (j!=100 && j!=0)
            {
                int32_t w, currsector, start_wall, end_wall;
                double size = (j/100.f);

                for (i = 0; i < highlightsectorcnt; i++)
                {
                    currsector = highlightsector[i];
                    sector[currsector].ceilingz = (int32_t)(sector[currsector].ceilingz*size);
                    sector[currsector].floorz = (int32_t)(sector[currsector].floorz*size);

                    // Do all the walls in the sector
                    start_wall = sector[currsector].wallptr;
                    end_wall = start_wall + sector[currsector].wallnum;

                    for (w = start_wall; w < end_wall; w++)
                    {
                        wall[w].x = (int32_t)(wall[w].x*size);
                        wall[w].y = (int32_t)(wall[w].y*size);
                        wall[w].yrepeat = min((int32_t)(wall[w].yrepeat/size),255);
                    }

                    w = headspritesect[highlightsector[i]];
                    while (w >= 0)
                    {
                        sprite[w].x = (int32_t)(sprite[w].x*size);
                        sprite[w].y = (int32_t)(sprite[w].y*size);
                        sprite[w].z = (int32_t)(sprite[w].z*size);
                        sprite[w].xrepeat = min(max((int32_t)(sprite[w].xrepeat*size),1),255);
                        sprite[w].yrepeat = min(max((int32_t)(sprite[w].yrepeat*size),1),255);
                        w = nextspritesect[w];
                    }
                }
                printmessage16("Map scaled");
            }
            else printmessage16("Aborted");
        }
        break;

        case 6:
        {
            j=getnumber16("Shade divisor:    ",1,128,1);
            if (j > 1)
            {
                for (i=0; i<numsectors; i++)
                {
                    sector[i].ceilingshade /= j;
                    sector[i].floorshade /= j;
                }
                for (i=0; i<numwalls; i++)
                    wall[i].shade /= j;
                for (i=0; i<MAXSPRITES; i++)
                    sprite[i].shade /= j;
                printmessage16("Shades adjusted");
            }
            else printmessage16("Aborted");
        }
        break;

        case 7:
        {
            j=getnumber16("Visibility divisor:    ",1,128,0);
            if (j > 1)
            {
                for (i=0; i<numsectors; i++)
                {
                    if (sector[i].visibility < 240)
                        sector[i].visibility /= j;
                    else sector[i].visibility = 240 + (sector[i].visibility>>4)/j;
                }
                printmessage16("Visibility adjusted");
            }
            else printmessage16("Aborted");
        }
        break;

        }  // switch (row)
    }
    break;  // switch (col) / case 0

    }  // switch (col)
}

#undef M32COMMON_FUNC_MENU_FREE_SCRIPT
#undef M32COMMON_FUNC_MENU_DELETE_SPRITE
#undef M32COMMON_FUNC_MENU_MAX_TILE_PROMPT
