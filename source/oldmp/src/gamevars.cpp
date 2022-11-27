//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers

This file is part of EDuke32

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------

#include "duke3d.h"
#include "gamedef.h"
#include "osd.h"
#include "gamevars.h"

extern int g_i,g_p;

void Gv_RefreshPointers(void);
extern void G_FreeMapState(int mapnum);
static void Gv_Free(void) /* called from Gv_ReadSave() and Gv_ResetVars() */
{
    for (auto& gameVar : aGameVars)
    {
        if (gameVar.dwFlags & GAMEVAR_USER_MASK)
            DO_FREE_AND_NULL(gameVar.val.plValues);
        gameVar.dwFlags |= GAMEVAR_RESET;
    }

    for (auto& gameArray : aGameArrays)
    {
        if (gameArray.plValues)
            DO_FREE_AND_NULL(gameArray.plValues);
        gameArray.bReset = 1;
    }

    g_gameVarCount=g_gameArrayCount=0;

    hash_init(&h_gamevars);
    hash_init(&h_arrays);

    return;
}

void Gv_Clear(void)
{
    Gv_Free();

    // Now, only do work that Gv_Free() hasn't done.
    for (auto& gameVar : aGameVars)
        DO_FREE_AND_NULL(gameVar.szLabel);

    for (auto& gameArray : aGameArrays)
        DO_FREE_AND_NULL(gameArray.szLabel);

    hash_init(&h_gamevars);
    hash_init(&h_arrays);
    return;
}

int Gv_ReadSave(int fil)
{
    int i, j;
    intptr_t l;
    char savedstate[MAXVOLUMES*MAXLEVELS];

    Bmemset(&savedstate,0,sizeof(savedstate));

    //     AddLog("Reading gamevars from savegame");

    Gv_Free(); // nuke 'em from orbit, it's the only way to be sure...

    //  Bsprintf(g_szBuf,"CP:%s %d",__FILE__,__LINE__);
    //  AddLog(g_szBuf);

    if (kdfread(&g_gameVarCount,sizeof(g_gameVarCount),1,fil) != 1) goto corrupt;
    for (i=0;i<g_gameVarCount;i++)
    {
        if (kdfread(&(aGameVars[i]),sizeof(gamevar_t),1,fil) != 1) goto corrupt;
        aGameVars[i].szLabel=(char*)Xcalloc(MAXVARLABEL,sizeof(char));
        if (kdfread(aGameVars[i].szLabel,sizeof(char) * MAXVARLABEL, 1, fil) != 1) goto corrupt;
        hash_add(&h_gamevars,aGameVars[i].szLabel,i, 1);

        if (aGameVars[i].dwFlags & GAMEVAR_PERPLAYER)
        {
            aGameVars[i].val.plValues=(intptr_t*)Xcalloc(MAXPLAYERS, sizeof(intptr_t));
            if (kdfread(aGameVars[i].val.plValues,sizeof(intptr_t) * MAXPLAYERS, 1, fil) != 1) goto corrupt;
        }
        else if (aGameVars[i].dwFlags & GAMEVAR_PERACTOR)
        {
            aGameVars[i].val.plValues= (intptr_t*)Xcalloc(MAXSPRITES, sizeof(intptr_t));
            if (kdfread(&aGameVars[i].val.plValues[0],sizeof(intptr_t), MAXSPRITES, fil) != MAXSPRITES) goto corrupt;
        }
    }
    //  Bsprintf(g_szBuf,"CP:%s %d",__FILE__,__LINE__);
    //  AddLog(g_szBuf);

    Gv_InitWeaponPointers();

    //  Bsprintf(g_szBuf,"CP:%s %d",__FILE__,__LINE__);
    //  AddLog(g_szBuf);
    Gv_RefreshPointers();

    if (kdfread(&g_gameArrayCount,sizeof(g_gameArrayCount),1,fil) != 1) goto corrupt;
    for (i=0;i<g_gameArrayCount;i++)
    {
        if (kdfread(&(aGameArrays[i]),sizeof(gamearray_t),1,fil) != 1) goto corrupt;
        aGameArrays[i].szLabel=(char*)Xcalloc(MAXARRAYLABEL,sizeof(char));
        if (kdfread(aGameArrays[i].szLabel,sizeof(char) * MAXARRAYLABEL, 1, fil) != 1) goto corrupt;
        hash_add(&h_arrays,aGameArrays[i].szLabel,i, 1);

        aGameArrays[i].plValues=(intptr_t*)Xcalloc(aGameArrays[i].size,sizeof(intptr_t));
        if (kdfread(aGameArrays[i].plValues,sizeof(intptr_t) * aGameArrays[i].size, 1, fil) < 1) goto corrupt;
    }

    //  Bsprintf(g_szBuf,"CP:%s %d",__FILE__,__LINE__);
    //  AddLog(g_szBuf);

    if (kdfread(&savedstate[0],sizeof(savedstate),1,fil) != 1) goto corrupt;

    for (i=0;i<(MAXVOLUMES*MAXLEVELS);i++)
    {
        if (savedstate[i])
        {
            if (g_mapInfo[i].savedstate == NULL)
                g_mapInfo[i].savedstate = (mapstate_t*)Xcalloc(1,sizeof(mapstate_t));
            if (kdfread(g_mapInfo[i].savedstate,sizeof(mapstate_t),1,fil) != sizeof(mapstate_t)) goto corrupt;
            for (j=0;j<g_gameVarCount;j++)
            {
                if (aGameVars[j].dwFlags & GAMEVAR_NORESET) continue;
                if (aGameVars[j].dwFlags & GAMEVAR_PERPLAYER)
                {
                    if (!g_mapInfo[i].savedstate->vars[j])
                        g_mapInfo[i].savedstate->vars[j] = (intptr_t*)Xcalloc(MAXPLAYERS, sizeof(intptr_t));
                    if (kdfread(&g_mapInfo[i].savedstate->vars[j][0],sizeof(intptr_t) * MAXPLAYERS, 1, fil) != 1) goto corrupt;
                }
                else if (aGameVars[j].dwFlags & GAMEVAR_PERACTOR)
                {
                    if (!g_mapInfo[i].savedstate->vars[j])
                        g_mapInfo[i].savedstate->vars[j] = (intptr_t*)Xcalloc(MAXSPRITES, sizeof(intptr_t));
                    if (kdfread(&g_mapInfo[i].savedstate->vars[j][0],sizeof(intptr_t), MAXSPRITES, fil) != MAXSPRITES) goto corrupt;
                }
            }
        }
        else if (g_mapInfo[i].savedstate)
        {
            G_FreeMapState(i);
        }
    }

    if (kdfread(&l,sizeof(l),1,fil) != 1) goto corrupt;
    if (kdfread(g_szBuf,l,1,fil) != 1) goto corrupt;
    g_szBuf[l]=0;
    OSD_Printf("%s\n",g_szBuf);

#if 0
    {
        FILE *fp;
        AddLog("Dumping Vars...");
        fp=fopen("xxx.txt","w");
        if (fp)
        {
            Gv_DumpValues(fp);
            fclose(fp);
        }
        AddLog("Done Dumping...");
    }
#endif
    return(0);
corrupt:
    return(1);
}

void Gv_WriteSave(FILE *fil)
{
    int i, j;
    intptr_t l;
    char savedstate[MAXVOLUMES*MAXLEVELS];

    Bmemset(&savedstate,0,sizeof(savedstate));

    //   AddLog("Saving Game Vars to File");
    dfwrite(&g_gameVarCount,sizeof(g_gameVarCount),1,fil);

    for (i=0;i<g_gameVarCount;i++)
    {
        dfwrite(&(aGameVars[i]),sizeof(gamevar_t),1,fil);
        dfwrite(aGameVars[i].szLabel,sizeof(char) * MAXVARLABEL, 1, fil);

        if (aGameVars[i].dwFlags & GAMEVAR_PERPLAYER)
        {
            //Bsprintf(g_szBuf,"Writing value array for %s (%d)",aGameVars[i].szLabel,sizeof(int) * MAXPLAYERS);
            //AddLog(g_szBuf);
            dfwrite(aGameVars[i].val.plValues,sizeof(intptr_t) * MAXPLAYERS, 1, fil);
        }
        else if (aGameVars[i].dwFlags & GAMEVAR_PERACTOR)
        {
            //Bsprintf(g_szBuf,"Writing value array for %s (%d)",aGameVars[i].szLabel,sizeof(int) * MAXSPRITES);
            //AddLog(g_szBuf);
            dfwrite(&aGameVars[i].val.plValues[0],sizeof(intptr_t), MAXSPRITES, fil);
        }
    }

    dfwrite(&g_gameArrayCount,sizeof(g_gameArrayCount),1,fil);

    for (i=0;i<g_gameArrayCount;i++)
    {
        dfwrite(&(aGameArrays[i]),sizeof(gamearray_t),1,fil);
        dfwrite(aGameArrays[i].szLabel,sizeof(char) * MAXARRAYLABEL, 1, fil);
        dfwrite(aGameArrays[i].plValues,sizeof(intptr_t) * aGameArrays[i].size, 1, fil);
    }

    for (i=0;i<(MAXVOLUMES*MAXLEVELS);i++)
        if (g_mapInfo[i].savedstate != NULL)
            savedstate[i] = 1;

    dfwrite(&savedstate[0],sizeof(savedstate),1,fil);

    for (i=0;i<(MAXVOLUMES*MAXLEVELS);i++)
        if (g_mapInfo[i].savedstate)
        {
            dfwrite(g_mapInfo[i].savedstate,sizeof(mapstate_t),1,fil);
            for (j=0;j<g_gameVarCount;j++)
            {
                if (aGameVars[j].dwFlags & GAMEVAR_NORESET) continue;
                if (aGameVars[j].dwFlags & GAMEVAR_PERPLAYER)
                {
                    dfwrite(&g_mapInfo[i].savedstate->vars[j][0],sizeof(intptr_t) * MAXPLAYERS, 1, fil);
                }
                else if (aGameVars[j].dwFlags & GAMEVAR_PERACTOR)
                {
                    dfwrite(&g_mapInfo[i].savedstate->vars[j][0],sizeof(intptr_t) * MAXSPRITES, 1, fil);
                }
            }
        }

    Bsprintf(g_szBuf,"EOF: EDuke32");
    l=strlen(g_szBuf);
    dfwrite(&l,sizeof(l),1,fil);
    dfwrite(g_szBuf,l,1,fil);
}

void Gv_DumpValues(void)
{
    int i;

    OSD_Printf("// Current Game Definitions\n\n");

    for (i=0;i<g_gameVarCount;i++)
    {
        if (aGameVars[i].dwFlags & (GAMEVAR_SECRET))
            continue; // do nothing...

        OSD_Printf("gamevar %s ",aGameVars[i].szLabel);

        if (aGameVars[i].dwFlags & (GAMEVAR_INT32PTR))
            OSD_Printf("%d",*((int*)aGameVars[i].val.lValue));
        else if (aGameVars[i].dwFlags & (GAMEVAR_INT16PTR))
            OSD_Printf("%d",*((short*)aGameVars[i].val.lValue));
        else if (aGameVars[i].dwFlags & (GAMEVAR_INT8PTR))
            OSD_Printf("%d",*((char*)aGameVars[i].val.lValue));
        else
            OSD_Printf("%" PRIdPTR "",aGameVars[i].val.lValue);
        if (aGameVars[i].dwFlags & (GAMEVAR_PERPLAYER))
            OSD_Printf(" GAMEVAR_PERPLAYER");
        else if (aGameVars[i].dwFlags & (GAMEVAR_PERACTOR))
            OSD_Printf(" GAMEVAR_PERACTOR");
        else
            OSD_Printf(" %d",aGameVars[i].dwFlags & (GAMEVAR_USER_MASK));
        OSD_Printf(" // ");
        if (aGameVars[i].dwFlags & (GAMEVAR_SYSTEM))
            OSD_Printf(" (system)");
        if (aGameVars[i].dwFlags & (GAMEVAR_PTR_MASK))
            OSD_Printf(" (pointer)");
        if (aGameVars[i].dwFlags & (GAMEVAR_READONLY))
            OSD_Printf(" (read only)");
        OSD_Printf("\n");
    }
    OSD_Printf("\n// end of game definitions\n");
}

void Gv_ResetVars(void) /* this is called during a new game and nowhere else */
{
    Gv_Free();

    for (auto& aGameVar : aGameVars)
    {
        if (aGameVar.szLabel != NULL)
            Gv_NewVar(aGameVar.szLabel, (aGameVar.dwFlags & GAMEVAR_NODEFAULT) ? aGameVar.val.lValue : aGameVar.lDefault, aGameVar.dwFlags);
    }

    for (auto& aGameArray : aGameArrays)
    {
        if (aGameArray.szLabel != NULL && aGameArray.bReset)
            Gv_NewArray(aGameArray.szLabel, aGameArray.size);
    }
}

int Gv_NewArray(const char *pszLabel, intptr_t asize)
{
    int i;

    if (g_gameArrayCount >= MAXGAMEARRAYS)
    {
        g_errorCnt++;
        C_ReportError(-1);
        LOG_F(ERROR, "%s:%d: too many arrays!",g_scriptFileName,g_lineNumber);
        return 0;
    }

    if (Bstrlen(pszLabel) > (MAXARRAYLABEL-1))
    {
        g_errorCnt++;
        C_ReportError(-1);
        LOG_F(ERROR, "%s:%d: array name `%s' exceeds limit of %d characters.",g_scriptFileName,g_lineNumber,pszLabel, MAXARRAYLABEL);
        return 0;
    }
    i = hash_find(&h_arrays,pszLabel);
    if (i >=0 && !aGameArrays[i].bReset)
    {
        // found it it's a duplicate in error
        g_warningCnt++;
        C_ReportError(WARNING_DUPLICATEDEFINITION);
        return 0;
    }

    i = g_gameArrayCount;

    if (aGameArrays[i].szLabel == NULL)
        aGameArrays[i].szLabel= (char*)Xcalloc(MAXVARLABEL,sizeof(char));
    if (aGameArrays[i].szLabel != pszLabel)
        Bstrcpy(aGameArrays[i].szLabel,pszLabel);
    aGameArrays[i].plValues=(intptr_t*)Xcalloc(asize,sizeof(intptr_t));
    aGameArrays[i].size=asize;
    aGameArrays[i].bReset=0;
    g_gameArrayCount++;
    hash_add(&h_arrays,aGameArrays[i].szLabel,i, 1);
    return 1;
}

void Gv_NewVar(const char* pszLabel, intptr_t lValue, uint32_t dwFlags)
{
    int i, j;

    //Bsprintf(g_szBuf,"Gv_NewVar(%s, %d, %X)",pszLabel, lValue, dwFlags);
    //AddLog(g_szBuf);

    if (g_gameVarCount >= MAXGAMEVARS)
    {
        g_errorCnt++;
        C_ReportError(-1);
        LOG_F(ERROR, "%s:%d: too many gamevars!",g_scriptFileName,g_lineNumber);
        return;
    }

    if (Bstrlen(pszLabel) > (MAXVARLABEL-1))
    {
        g_errorCnt++;
        C_ReportError(-1);
        LOG_F(ERROR, "%s:%d: variable name `%s' exceeds limit of %d characters.",g_scriptFileName,g_lineNumber,pszLabel, MAXVARLABEL);
        return;
    }

    i = hash_find(&h_gamevars,pszLabel);

    if (i >= 0 && !(aGameVars[i].dwFlags & GAMEVAR_RESET))
    {
        // found it...
        if (aGameVars[i].dwFlags & (GAMEVAR_INT32PTR|GAMEVAR_INT16PTR|GAMEVAR_INT8PTR))
        {
            //  			   warning++;
            //  			   LOG_F("%s:%d: warning: Internal gamevar '%s' cannot be redefined.\n",g_scriptFileName,g_lineNumber,LAST_LABEL);
            C_ReportError(-1);
            LOG_F(WARNING, "%s:%d: cannot redefine internal gamevar `%s'.",g_scriptFileName,g_lineNumber,LAST_LABEL);
            return;
        }
        else if (!(aGameVars[i].dwFlags & GAMEVAR_DEFAULT) && !(aGameVars[i].dwFlags & GAMEVAR_SYSTEM))
        {
            // it's a duplicate in error
            g_warningCnt++;
            C_ReportError(WARNING_DUPLICATEDEFINITION);
            return;
        }
    }

    if (i == -1)
        i = g_gameVarCount;

    // Set values
    if ((aGameVars[i].dwFlags & GAMEVAR_SYSTEM) == 0)
    {
        if (aGameVars[i].szLabel == NULL)
            aGameVars[i].szLabel=(char*)Xcalloc(MAXVARLABEL,sizeof(char));
        if (aGameVars[i].szLabel != pszLabel)
            Bstrcpy(aGameVars[i].szLabel,pszLabel);
        aGameVars[i].dwFlags=dwFlags;

        if (aGameVars[i].dwFlags & GAMEVAR_USER_MASK)
        {
            // only free if not system
            if (aGameVars[i].val.plValues)
                DO_FREE_AND_NULL(aGameVars[i].val.plValues);
        }
    }

    // if existing is system, they only get to change default value....
    aGameVars[i].lDefault = lValue;
    aGameVars[i].dwFlags &= ~GAMEVAR_RESET;

    if (i == g_gameVarCount)
    {
        // we're adding a new one.
        hash_add(&h_gamevars, aGameVars[i].szLabel, g_gameVarCount++, 0);
    }

    if (aGameVars[i].dwFlags & GAMEVAR_PERPLAYER)
    {
        if (!aGameVars[i].val.plValues)
            aGameVars[i].val.plValues=(intptr_t*)Xcalloc(MAXPLAYERS, sizeof(intptr_t));

        for (j=MAXPLAYERS-1;j>=0;j--)
            aGameVars[i].val.plValues[j]=lValue;
    }
    else if (aGameVars[i].dwFlags & GAMEVAR_PERACTOR)
    {
        if (!aGameVars[i].val.plValues)
            aGameVars[i].val.plValues=(intptr_t*)Xcalloc(MAXSPRITES, sizeof(intptr_t));
        for (j=MAXSPRITES-1;j>=0;j--)
            aGameVars[i].val.plValues[j]=lValue;
    }
    else aGameVars[i].val.lValue = lValue;
}

void A_ResetVars(int iActor)
{
    int i=(MAXGAMEVARS-1);
    for (;i>=0;i--)
        if ((aGameVars[i].dwFlags & GAMEVAR_PERACTOR) && !(aGameVars[i].dwFlags & GAMEVAR_NODEFAULT))
            aGameVars[i].val.plValues[iActor]=aGameVars[i].lDefault;
}

static int Gv_GetVarIndex(const char *szGameLabel)
{
    int i = hash_find(&h_gamevars,szGameLabel);
    if (i == -1)
    {
        OSD_Printf(OSD_ERROR "Gv_GetVarDataPtr(): INTERNAL ERROR: couldn't find gamevar %s!\n",szGameLabel);
        return 0;
    }
    return i;
}

int __fastcall Gv_GetVar(int id, int iActor, int iPlayer)
{
    if (id == MAXGAMEVARS)
        return(*insptr++);

    if (id == sysVarIDs.THISACTOR)
        return iActor;

    if (id == sysVarIDs.RESPAWN_MONSTERS)   return DMFLAGS_TEST(DMFLAG_RESPAWNMONSTERS);
    if (id == sysVarIDs.RESPAWN_ITEMS)      return DMFLAGS_TEST(DMFLAG_RESPAWNITEMS);
    if (id == sysVarIDs.RESPAWN_INVENTORY)  return DMFLAGS_TEST(DMFLAG_RESPAWNINVENTORY);
    if (id == sysVarIDs.MONSTERS_OFF)       return DMFLAGS_TEST(DMFLAG_NOMONSTERS);
    if (id == sysVarIDs.MARKER)             return DMFLAGS_TEST(DMFLAG_MARKERS);
    if (id == sysVarIDs.FFIRE)              return DMFLAGS_TEST(DMFLAG_FRIENDLYFIRE);

    {
        int neg = 0;

        if (id >= g_gameVarCount || id < 0)
        {
            if (id&(MAXGAMEVARS<<2)) // array
            {
                int index=Gv_GetVar(*insptr++,iActor,iPlayer);

                if (id&(MAXGAMEVARS<<1)) // negative array access
                    neg = 1;

                id &= ~((MAXGAMEVARS<<2)|(MAXGAMEVARS<<1));

                if (index >= aGameArrays[id].size || index < 0)
                {
                    OSD_Printf(CON_ERROR "Gv_GetVar(): invalid array index (%s[%d])\n",g_errorLineNum,VM_GetKeywordForID(g_tw),aGameArrays[id].szLabel,index);
                    return -1;
                }
                return(neg?-aGameArrays[id].plValues[index]:aGameArrays[id].plValues[index]);
            }

            if ((id&(MAXGAMEVARS<<1)) == 0)
            {
                OSD_Printf(CON_ERROR "Gv_GetVar(): invalid gamevar ID (%d)\n",g_errorLineNum,VM_GetKeywordForID(g_tw),id);
                return -1;
            }

            neg = 1;
            id &= ~(MAXGAMEVARS<<1);
        }

        auto const& var = aGameVars[id & (MAXGAMEVARS - 1)];

        int returnValue = 0;

        switch (var.dwFlags & (GAMEVAR_USER_MASK | GAMEVAR_PTR_MASK))
        {
            default: returnValue = var.val.lValue; break;
            case GAMEVAR_PERACTOR:  returnValue = var.val.plValues[iActor & (MAXSPRITES - 1)]; break;
            case GAMEVAR_PERPLAYER: returnValue = var.val.plValues[iPlayer & (MAXPLAYERS - 1)]; break;
            case GAMEVAR_RAWQ16PTR:
            case GAMEVAR_INT32PTR: returnValue = *(int32_t*)var.val.lValue; break;
            case GAMEVAR_INT16PTR: returnValue = *(int16_t*)var.val.lValue; break;
            case GAMEVAR_INT8PTR: returnValue = *(uint8_t*)var.val.lValue; break;
            case GAMEVAR_Q16PTR:   returnValue = fix16_to_int(*(fix16_t*)var.val.lValue); break;
        }

        return NEGATE_ON_CONDITION(returnValue, neg);
    }
}

static FORCE_INLINE void __fastcall setvar__(int const gameVar, int const newValue, int const spriteNum, int const playerNum)
{
    gamevar_t& var = aGameVars[gameVar];
    switch (var.dwFlags & (GAMEVAR_USER_MASK | GAMEVAR_PTR_MASK))
    {
    default: var.val.lValue = newValue; break;
    case GAMEVAR_PERPLAYER: var.val.plValues[playerNum & (MAXPLAYERS - 1)] = newValue; break;
    case GAMEVAR_PERACTOR:  var.val.plValues[spriteNum & (MAXSPRITES - 1)] = newValue; break;
    case GAMEVAR_RAWQ16PTR:
    case GAMEVAR_INT32PTR:    *((int32_t*)var.val.lValue) = (int32_t)newValue; break;
    case GAMEVAR_INT16PTR:  *((int16_t*)var.val.lValue) = (int16_t)newValue; break;
    case GAMEVAR_INT8PTR:   *((uint8_t*)var.val.lValue) = (uint8_t)newValue; break;
    case GAMEVAR_Q16PTR:    *(fix16_t*)var.val.lValue = fix16_from_int((int16_t)newValue); break;
    }
}

void __fastcall Gv_SetVar(int id, int lValue, int iActor, int iPlayer)
{
    if (id<0 || id >= g_gameVarCount)
    {
        OSD_Printf(CON_ERROR "Gv_SetVar(): tried to set invalid gamevar ID (%d) from sprite %d (%d), player %d\n",g_errorLineNum,VM_GetKeywordForID(g_tw),id,g_i,TrackerCast(sprite[g_i].picnum),g_p);
        return;
    }
    //Bsprintf(g_szBuf,"SGVI: %d ('%s') to %d for %d %d",id,aGameVars[id].szLabel,lValue,iActor,iPlayer);
    //AddLog(g_szBuf);

    switch (aGameVars[id].dwFlags & (GAMEVAR_USER_MASK|GAMEVAR_PTR_MASK))
    {
    case GAMEVAR_PERPLAYER:
        if (iPlayer < 0 || iPlayer > MAXPLAYERS-1)
        {
            OSD_Printf(CON_ERROR "Gv_SetVar(): invalid player (%d) for per-player gamevar %s from sprite %d, player %d\n",g_errorLineNum,VM_GetKeywordForID(g_tw),iPlayer,aGameVars[id].szLabel,g_i,g_p);
            return;
        }
        break;
    case GAMEVAR_PERACTOR:
        if (iActor < 0 || iActor > MAXSPRITES-1)
        {
            OSD_Printf(CON_ERROR "Gv_SetVar(): invalid sprite (%d) for per-actor gamevar %s from sprite %d (%d), player %d\n",g_errorLineNum,VM_GetKeywordForID(g_tw),iActor,aGameVars[id].szLabel,g_i,TrackerCast(sprite[g_i].picnum),g_p);
            return;
        }
        break;
    }

    setvar__(id, lValue, iActor, iPlayer);
}


int __fastcall Gv_GetVarX(int id)
{
    return Gv_GetVar(id, g_i, g_p);
}

void __fastcall Gv_SetVarX(int id, int lValue)
{
    setvar__(id, lValue, g_i, g_p);
}

int Gv_GetVarByLabel(const char *szGameLabel, int lDefault, int iActor, int iPlayer)
{
    int gameVar = hash_find(&h_gamevars,szGameLabel);
    return EDUKE32_PREDICT_TRUE(gameVar >= 0) ? Gv_GetVar(gameVar, iActor, iPlayer) : lDefault;
}

static intptr_t* Gv_GetVarDataPtr(const char* szGameLabel)
{
    int const gameVar = hash_find(&h_gamevars, szGameLabel);

    if (EDUKE32_PREDICT_FALSE((unsigned)gameVar >= MAXGAMEVARS))
        return NULL;

    gamevar_t& var = aGameVars[gameVar];

    if (var.dwFlags & (GAMEVAR_USER_MASK | GAMEVAR_PTR_MASK))
        return var.val.plValues;

    return &(var.val.lValue);
}

// Will set members that were overridden at CON translation time to 1.
// For example, if
//   gamevar WEAPON1_SHOOTS 2200 GAMEVAR_PERPLAYER
// was specified at file scope, g_weaponOverridden[1].Shoots will be 1.
//weapondata_t g_weaponOverridden[MAX_WEAPONS]; // Seemingly unused?

static weapondata_t weapondefaults[MAX_WEAPONS] = {
    /*
        WorksLike, Clip, Reload, FireDelay, TotalTime, HoldDelay,
        Flags,
        Shoots, SpawnTime, Spawn, ShotsPerBurst, InitialSound, FireSound, Sound2Time, Sound2Sound,
        ReloadSound1, ReloadSound2, SelectSound, FlashColor
    */
#ifndef EDUKE32_STANDALONE
    {
        KNEE_WEAPON, 0, 0, 7, 14, 0,
        WEAPON_NOVISIBLE | WEAPON_RANDOMRESTART | WEAPON_AUTOMATIC,
        KNEE__STATIC, 0, 0, 0, 0, 0, 0,
        0, EJECT_CLIP, INSERT_CLIP, 0, 0
    },

    {
        PISTOL_WEAPON, 12, 27, 2, 5, 0,
        WEAPON_RELOAD_TIMING,
        SHOTSPARK1__STATIC, 2, SHELL__STATIC, 0, 0, PISTOL_FIRE, 0, 0,
        EJECT_CLIP, INSERT_CLIP, INSERT_CLIP, 255 + (95 << 8)
    },

    {
        SHOTGUN_WEAPON, 0, 13, 4, 30, 0,
        WEAPON_CHECKATRELOAD,
        SHOTGUN__STATIC, 24, SHOTGUNSHELL__STATIC, 7, 0, SHOTGUN_FIRE, 15, SHOTGUN_COCK,
        EJECT_CLIP, INSERT_CLIP, SHOTGUN_COCK, 255 + (95 << 8)
    },

    {
        CHAINGUN_WEAPON, 0, 0, 3, 12, 3,
        WEAPON_AUTOMATIC | WEAPON_FIREEVERYTHIRD | WEAPON_AMMOPERSHOT | WEAPON_SPAWNTYPE3 | WEAPON_RESET,
        CHAINGUN__STATIC, 1, SHELL__STATIC, 0, 0, CHAINGUN_FIRE, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 255 + (95 << 8)
    },

    {
        RPG_WEAPON, 0, 0, 4, 20, 0,
        0,
        RPG__STATIC, 0, 0, 0, 0, 0, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 255 + (95 << 8)
    },

    {
        HANDBOMB_WEAPON, 0, 30, 6, 19, 12,
        WEAPON_THROWIT,
        HEAVYHBOMB__STATIC, 0, 0, 0, 0, 0, 0,
        0, EJECT_CLIP, INSERT_CLIP, 0, 0
    },

    {
        SHRINKER_WEAPON, 0, 0, 10, 12, 0,
        WEAPON_GLOWS,
        SHRINKER__STATIC, 0, 0, 0, SHRINKER_FIRE, 0, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 176 + (252 << 8) + (120 << 16)
    },

    {
        DEVISTATOR_WEAPON, 0, 0, 3, 6, 5,
        WEAPON_FIREEVERYOTHER | WEAPON_AMMOPERSHOT,
        RPG__STATIC, 0, 0, 2, CAT_FIRE, 0, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 255 + (95 << 8)
    },

    {
        TRIPBOMB_WEAPON, 0, 16, 3, 16, 7,
        WEAPON_NOVISIBLE | WEAPON_STANDSTILL | WEAPON_CHECKATRELOAD,
        HANDHOLDINGLASER__STATIC, 0, 0, 0, 0, 0, 0,
        0, EJECT_CLIP, INSERT_CLIP, 0, 0
    },

    {
        FREEZE_WEAPON, 0, 0, 3, 5, 0,
        WEAPON_RESET,
        FREEZEBLAST__STATIC, 0, 0, 0, CAT_FIRE, CAT_FIRE, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 72 + (88 << 8) + (140 << 16)
    },

    {
        HANDREMOTE_WEAPON, 0, 10, 2, 10, 0,
        WEAPON_BOMB_TRIGGER | WEAPON_NOVISIBLE,
        0, 0, 0, 0, 0, 0, 0,
        0, EJECT_CLIP, INSERT_CLIP, 0, 0
    },

    {
        GROW_WEAPON, 0, 0, 3, 5, 0,
        WEAPON_GLOWS,
        GROWSPARK__STATIC, 0, 0, 0, 0, EXPANDERSHOOT, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 216 + (52 << 8) + (20 << 16)
    },

#if 0
    {
        FLAMETHROWER_WEAPON, 0, 0, 2, 16, 0,
        WEAPON_RESET,
        FIREBALL__STATIC, 0, 0, 0, FLAMETHROWER_INTRO, FLAMETHROWER_INTRO, 0, 0,
        EJECT_CLIP, INSERT_CLIP, SELECT_WEAPON, 216 + (52 << 8) + (20 << 16)
    },
#endif

#endif
};

#define ADDWEAPONVAR(Weapidx, Membname) do { \
    Bsprintf(aszBuf, "WEAPON%d_" #Membname, Weapidx); \
    Bstrupr(aszBuf); \
    Gv_NewVar(aszBuf, weapondefaults[Weapidx].Membname, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM); \
} while (0)

void Gv_ResetSystemDefaults(void)
{
    // call many times...

    int i,j;
    char aszBuf[64];

    //AddLog("ResetWeaponDefaults");

    for (j=MAXPLAYERS-1;j>=0;j--)
    {
        for (i=MAX_WEAPONS-1;i>=0;i--)
        {
            Bsprintf(aszBuf,"WEAPON%d_CLIP",i);
            aplWeaponClip[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_RELOAD",i);
            aplWeaponReload[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_FIREDELAY",i);
            aplWeaponFireDelay[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_TOTALTIME",i);
            aplWeaponTotalTime[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_HOLDDELAY",i);
            aplWeaponHoldDelay[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_FLAGS",i);
            aplWeaponFlags[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SHOOTS",i);
            aplWeaponShoots[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SPAWNTIME",i);
            aplWeaponSpawnTime[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SPAWN",i);
            aplWeaponSpawn[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SHOTSPERBURST",i);
            aplWeaponShotsPerBurst[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_WORKSLIKE",i);
            aplWeaponWorksLike[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_INITIALSOUND",i);
            aplWeaponInitialSound[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_FIRESOUND",i);
            aplWeaponFireSound[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SOUND2TIME",i);
            aplWeaponSound2Time[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SOUND2SOUND",i);
            aplWeaponSound2Sound[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_RELOADSOUND1",i);
            aplWeaponReloadSound1[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_RELOADSOUND2",i);
            aplWeaponReloadSound2[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf,"WEAPON%d_SELECTSOUND",i);
            aplWeaponSelectSound[i][j]=Gv_GetVarByLabel(aszBuf,0, -1, j);
            Bsprintf(aszBuf, "WEAPON%d_FLASHCOLOR", i);
            aplWeaponFlashColor[i][j] = Gv_GetVarByLabel(aszBuf, 0, -1, j);
        }
    }

    sysVarIDs.RETURN = Gv_GetVarIndex("RETURN");
    sysVarIDs.WEAPON = Gv_GetVarIndex("WEAPON");
    sysVarIDs.WORKSLIKE = Gv_GetVarIndex("WORKSLIKE");
    sysVarIDs.ZRANGE = Gv_GetVarIndex("ZRANGE");
    sysVarIDs.ANGRANGE = Gv_GetVarIndex("ANGRANGE");
    sysVarIDs.AUTOAIMANGLE = Gv_GetVarIndex("AUTOAIMANGLE");
    sysVarIDs.LOTAG = Gv_GetVarIndex("LOTAG");
    sysVarIDs.HITAG = Gv_GetVarIndex("HITAG");
    sysVarIDs.TEXTURE = Gv_GetVarIndex("TEXTURE");
    sysVarIDs.THISACTOR = Gv_GetVarIndex("THISACTOR");

    sysVarIDs.RESPAWN_MONSTERS = Gv_GetVarIndex("RESPAWN_MONSTERS");
    sysVarIDs.RESPAWN_ITEMS = Gv_GetVarIndex("RESPAWN_ITEMS");
    sysVarIDs.RESPAWN_INVENTORY = Gv_GetVarIndex("RESPAWN_INVENTORY");
    sysVarIDs.MONSTERS_OFF = Gv_GetVarIndex("MONSTERS_OFF");
    sysVarIDs.MARKER = Gv_GetVarIndex("MARKER");
    sysVarIDs.FFIRE = Gv_GetVarIndex("FFIRE");

    sysVarIDs.structs = Gv_GetVarIndex("sprite");

    for (auto& tile : g_tile)
        if (tile.defproj)
            *tile.proj = *tile.defproj;

    //AddLog("EOF:ResetWeaponDefaults");
}

static void Gv_AddSystemVars(void)
{
    // only call ONCE
    char aszBuf[64];

    //AddLog("Gv_AddSystemVars");

    // special vars for struct access
    // KEEPINSYNC gamedef.h: enum QuickStructureAccess_t (including order)
    Gv_NewVar("sprite",         -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("__sprite__",     -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("__actor__",      -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("__spriteext__",  -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("sector",         -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("__sector__",     -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("wall",           -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("__wall__",       -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("player",         -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("__player__",     -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("actorvar",       -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("playervar",      -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("tspr",           -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("projectile",     -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("thisprojectile", -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("userdef",        -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("input",          -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    Gv_NewVar("tiledata",       -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);
    //Gv_NewVar("paldata",        -1, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_SPECIAL);

    if (NAM_WW2GI)
    {
        weapondefaults[PISTOL_WEAPON].Clip = 20;
        weapondefaults[PISTOL_WEAPON].Reload = 50;
        weapondefaults[PISTOL_WEAPON].Flags |= WEAPON_HOLSTER_CLEARS_CLIP;

        weapondefaults[SHRINKER_WEAPON].TotalTime = 30;

        weapondefaults[GROW_WEAPON].TotalTime = 30;

        if (WW2GI)
        {
            weapondefaults[KNEE_WEAPON].HoldDelay = 14;
            weapondefaults[KNEE_WEAPON].Reload = 30;

            weapondefaults[PISTOL_WEAPON].Flags |= WEAPON_AUTOMATIC;

            weapondefaults[SHOTGUN_WEAPON].TotalTime = 31;

            weapondefaults[CHAINGUN_WEAPON].FireDelay = 1;
            weapondefaults[CHAINGUN_WEAPON].HoldDelay = 10;
            weapondefaults[CHAINGUN_WEAPON].Reload = 30;
            weapondefaults[CHAINGUN_WEAPON].SpawnTime = 0;

            weapondefaults[RPG_WEAPON].Reload = 30;

            weapondefaults[DEVISTATOR_WEAPON].FireDelay = 2;
            weapondefaults[DEVISTATOR_WEAPON].Flags = WEAPON_FIREEVERYOTHER;
            weapondefaults[DEVISTATOR_WEAPON].Reload = 30;
            weapondefaults[DEVISTATOR_WEAPON].ShotsPerBurst = 0;
            weapondefaults[DEVISTATOR_WEAPON].TotalTime = 5;

            weapondefaults[TRIPBOMB_WEAPON].Flags = WEAPON_STANDSTILL;
            weapondefaults[TRIPBOMB_WEAPON].HoldDelay = 0;
            weapondefaults[TRIPBOMB_WEAPON].Reload = 30;

            weapondefaults[FREEZE_WEAPON].Flags = WEAPON_FIREEVERYOTHER;

            weapondefaults[HANDREMOTE_WEAPON].Reload = 30;

            weapondefaults[GROW_WEAPON].InitialSound = EXPANDERSHOOT;
        }
    }

    for (int i = 0; i < MAX_WEAPONS; i++)
    {
        ADDWEAPONVAR(i, Clip);
        ADDWEAPONVAR(i, FireDelay);
        ADDWEAPONVAR(i, FireSound);
        ADDWEAPONVAR(i, Flags);
        ADDWEAPONVAR(i, FlashColor);
        ADDWEAPONVAR(i, HoldDelay);
        ADDWEAPONVAR(i, InitialSound);
        ADDWEAPONVAR(i, Reload);
        ADDWEAPONVAR(i, ReloadSound1);
        ADDWEAPONVAR(i, ReloadSound2);
        ADDWEAPONVAR(i, SelectSound);
        ADDWEAPONVAR(i, Shoots);
        ADDWEAPONVAR(i, ShotsPerBurst);
        ADDWEAPONVAR(i, Sound2Sound);
        ADDWEAPONVAR(i, Sound2Time);
        ADDWEAPONVAR(i, Spawn);
        ADDWEAPONVAR(i, SpawnTime);
        ADDWEAPONVAR(i, TotalTime);
        ADDWEAPONVAR(i, WorksLike);
    }

    Gv_NewVar("GRENADE_LIFETIME", NAM_GRENADE_LIFETIME, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);
    Gv_NewVar("GRENADE_LIFETIME_VAR", NAM_GRENADE_LIFETIME_VAR, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);

    Gv_NewVar("STICKYBOMB_LIFETIME", NAM_GRENADE_LIFETIME, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);
    Gv_NewVar("STICKYBOMB_LIFETIME_VAR", NAM_GRENADE_LIFETIME_VAR, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);

    Gv_NewVar("TRIPBOMB_CONTROL", TRIPBOMB_TRIPWIRE, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);
    Gv_NewVar("PIPEBOMB_CONTROL", NAM?PIPEBOMB_TIMER:PIPEBOMB_REMOTE, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);

    Gv_NewVar("DMFLAGS", (intptr_t)&ud.dmflags, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);
    Gv_NewVar("RESPAWN_MONSTERS", 0, GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("RESPAWN_ITEMS", 0, GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("RESPAWN_INVENTORY", 0, GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("MONSTERS_OFF", 0, GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("MARKER", 0, GAMEVAR_SYSTEM | GAMEVAR_READONLY);
    Gv_NewVar("FFIRE", 0, GAMEVAR_SYSTEM | GAMEVAR_READONLY);

    Gv_NewVar("LEVEL",(intptr_t)&ud.level_number, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY);
    Gv_NewVar("VOLUME",(intptr_t)&ud.volume_number, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY);

    Gv_NewVar("COOP",(intptr_t)&ud.coop, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);
    Gv_NewVar("MULTIMODE",(intptr_t)&ud.multimode, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);

    Gv_NewVar("WEAPON", 0, GAMEVAR_PERPLAYER | GAMEVAR_READONLY | GAMEVAR_SYSTEM);
    Gv_NewVar("WORKSLIKE", 0, GAMEVAR_PERPLAYER | GAMEVAR_READONLY | GAMEVAR_SYSTEM);
    Gv_NewVar("RETURN", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("ZRANGE", 4, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);
    Gv_NewVar("ANGRANGE", 18, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);
    Gv_NewVar("AUTOAIMANGLE", 0, GAMEVAR_PERPLAYER | GAMEVAR_SYSTEM);
    Gv_NewVar("LOTAG", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("HITAG", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("TEXTURE", 0, GAMEVAR_SYSTEM);
    Gv_NewVar("THISACTOR", 0, GAMEVAR_READONLY | GAMEVAR_SYSTEM);
    Gv_NewVar("myconnectindex", (intptr_t)&myconnectindex, GAMEVAR_READONLY | GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("screenpeek", (intptr_t)&screenpeek, GAMEVAR_READONLY | GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("currentweapon",(intptr_t)&g_currentweapon, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("gs",(intptr_t)&g_gs, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("looking_arc",(intptr_t)&g_looking_arc, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("gun_pos",(intptr_t)&g_gun_pos, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("weapon_xoffset",(intptr_t)&g_weapon_xoffset, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("weaponcount",(intptr_t)&g_kb, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("looking_angSR1",(intptr_t)&g_looking_angSR1, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_SYNCCHECK);
    Gv_NewVar("xdim",(intptr_t)&xdim, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("ydim",(intptr_t)&ydim, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("windowx1",(intptr_t)&windowxy1.x, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("windowx2",(intptr_t)&windowxy2.x, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("windowy1",(intptr_t)&windowxy1.y, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("windowy2",(intptr_t)&windowxy2.y, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("totalclock",(intptr_t)&totalclock, GAMEVAR_INT32PTR | GAMEVAR_SYSTEM | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    //Gv_NewVar("lastvisinc",(intptr_t)&lastvisinc, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("numsectors",(intptr_t)&numsectors, GAMEVAR_SYSTEM | GAMEVAR_INT16PTR | GAMEVAR_READONLY);

    Gv_NewVar("current_menu",(intptr_t)&g_currentMenu, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY);
    Gv_NewVar("numplayers",(intptr_t)&numplayers, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY);
    Gv_NewVar("viewingrange",(intptr_t)&viewingrange, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("yxaspect",(intptr_t)&yxaspect, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("gravitationalconstant",(intptr_t)&g_spriteGravity, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);
    Gv_NewVar("gametype_flags",(intptr_t)&Gametypes[ud.coop].flags, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);
    Gv_NewVar("framerate",(intptr_t)&g_currentFrameRate, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);

    Gv_NewVar("camerax",(intptr_t)&ud.camerapos.x, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameray",(intptr_t)&ud.camerapos.y, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameraz",(intptr_t)&ud.camerapos.z, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameraang",(intptr_t)&ud.cameraq16ang, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameraq16ang", (intptr_t)&ud.cameraq16ang, GAMEVAR_SYSTEM | GAMEVAR_RAWQ16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("camerahoriz",(intptr_t)&ud.cameraq16horiz, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameraq16horiz", (intptr_t)&ud.cameraq16horiz, GAMEVAR_SYSTEM | GAMEVAR_RAWQ16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("camerasect",(intptr_t)&ud.camerasect, GAMEVAR_SYSTEM | GAMEVAR_INT16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameradist",(intptr_t)&g_cameraDistance, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("cameraclock",(intptr_t)&g_cameraClock, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);

    // Prediction
    Gv_NewVar("myx",(intptr_t)&predictedPlayer.pos.x, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myy",(intptr_t)&predictedPlayer.pos.y, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myz",(intptr_t)&predictedPlayer.pos.z, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("omyx",(intptr_t)&predictedPlayer.opos.x, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("omyy",(intptr_t)&predictedPlayer.opos.y, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("omyz",(intptr_t)&predictedPlayer.opos.z, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myxvel",(intptr_t)&predictedPlayer.vel.x, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myyvel",(intptr_t)&predictedPlayer.vel.y, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myzvel",(intptr_t)&predictedPlayer.vel.z, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_SYNCCHECK);

    Gv_NewVar("myhoriz",(intptr_t)&predictedPlayer.q16horiz, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myhorizoff",(intptr_t)&predictedPlayer.q16horizoff, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("omyhoriz",(intptr_t)&predictedPlayer.oq16horiz, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("omyhorizoff",(intptr_t)&predictedPlayer.oq16horizoff, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myang",(intptr_t)&predictedPlayer.q16ang, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("omyang",(intptr_t)&predictedPlayer.oq16ang, GAMEVAR_SYSTEM | GAMEVAR_Q16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("mycursectnum",(intptr_t)&predictedPlayer.cursectnum, GAMEVAR_SYSTEM | GAMEVAR_INT16PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myjumpingcounter",(intptr_t)&predictedPlayer.jumping_counter, GAMEVAR_SYSTEM | GAMEVAR_INT16PTR | GAMEVAR_SYNCCHECK);

    Gv_NewVar("myjumpingtoggle",(intptr_t)&predictedPlayer.jumping_toggle, GAMEVAR_SYSTEM | GAMEVAR_INT8PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myonground",(intptr_t)&predictedPlayer.on_ground, GAMEVAR_SYSTEM | GAMEVAR_INT8PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myhardlanding",(intptr_t)&predictedPlayer.hard_landing, GAMEVAR_SYSTEM | GAMEVAR_INT8PTR | GAMEVAR_SYNCCHECK);
    Gv_NewVar("myreturntocenter",(intptr_t)&predictedPlayer.return_to_center, GAMEVAR_SYSTEM | GAMEVAR_INT8PTR | GAMEVAR_SYNCCHECK);

    Gv_NewVar("predicting", (intptr_t)&oldnet_predicting, GAMEVAR_READONLY | GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);
    // ---

    Gv_NewVar("display_mirror",(intptr_t)&display_mirror, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY | GAMEVAR_SYNCCHECK);
    Gv_NewVar("randomseed",(intptr_t)&randomseed, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);

    Gv_NewVar("NUMWALLS",(intptr_t)&numwalls, GAMEVAR_SYSTEM | GAMEVAR_INT16PTR | GAMEVAR_READONLY);
    Gv_NewVar("NUMSECTORS",(intptr_t)&numsectors, GAMEVAR_SYSTEM | GAMEVAR_INT16PTR | GAMEVAR_READONLY);
    Gv_NewVar("Numsprites", (intptr_t)&Numsprites, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR | GAMEVAR_READONLY);

    Gv_NewVar("lastsavepos",(intptr_t)&g_lastSaveSlot, GAMEVAR_SYSTEM | GAMEVAR_INT32PTR);

    Gv_NewVar("numteams", (intptr_t)&numTeams, GAMEVAR_SYSTEM | GAMEVAR_INT8PTR | GAMEVAR_READONLY);

# ifdef USE_OPENGL
    Gv_NewVar("rendmode", (intptr_t)&rendmode, GAMEVAR_READONLY | GAMEVAR_INT32PTR | GAMEVAR_SYSTEM);
# else
    Gv_NewVar("rendmode", 0, GAMEVAR_READONLY | GAMEVAR_SYSTEM);
# endif
}

void Gv_Init(void)
{
    // only call ONCE

    //  LOG_F("Initializing game variables\n");
    //AddLog("Gv_Init");

    Gv_Clear();
    Gv_AddSystemVars();
    Gv_InitWeaponPointers();
    Gv_ResetSystemDefaults();
}

void Gv_InitWeaponPointers(void)
{
    int i;
    char aszBuf[64];
    // called from game Init AND when level is loaded...

    //AddLog("Gv_InitWeaponPointers");

    for (i = (MAX_WEAPONS - 1); i >= 0; i--)
    {
        Bsprintf(aszBuf, "WEAPON%d_CLIP", i);
        aplWeaponClip[i] = Gv_GetVarDataPtr(aszBuf);
        if (!aplWeaponClip[i])
        {
            LOG_F(ERROR, "NULL weapon!  WTF?!");
            // exit(0);
            G_Shutdown();
        }
        Bsprintf(aszBuf, "WEAPON%d_RELOAD", i);
        aplWeaponReload[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_FIREDELAY", i);
        aplWeaponFireDelay[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_TOTALTIME", i);
        aplWeaponTotalTime[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_HOLDDELAY", i);
        aplWeaponHoldDelay[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_FLAGS", i);
        aplWeaponFlags[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SHOOTS", i);
        aplWeaponShoots[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SPAWNTIME", i);
        aplWeaponSpawnTime[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SPAWN", i);
        aplWeaponSpawn[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SHOTSPERBURST", i);
        aplWeaponShotsPerBurst[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_WORKSLIKE", i);
        aplWeaponWorksLike[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_INITIALSOUND", i);
        aplWeaponInitialSound[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_FIRESOUND", i);
        aplWeaponFireSound[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SOUND2TIME", i);
        aplWeaponSound2Time[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SOUND2SOUND", i);
        aplWeaponSound2Sound[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_RELOADSOUND1", i);
        aplWeaponReloadSound1[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_RELOADSOUND2", i);
        aplWeaponReloadSound2[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_SELECTSOUND", i);
        aplWeaponSelectSound[i] = Gv_GetVarDataPtr(aszBuf);

        Bsprintf(aszBuf, "WEAPON%d_FLASHCOLOR", i);
        aplWeaponFlashColor[i] = Gv_GetVarDataPtr(aszBuf);
    }
}

void Gv_RefreshPointers(void)
{
    aGameVars[Gv_GetVarIndex("LEVEL")].val.lValue = (intptr_t)&ud.level_number;
    aGameVars[Gv_GetVarIndex("VOLUME")].val.lValue = (intptr_t)&ud.volume_number;

    aGameVars[Gv_GetVarIndex("COOP")].val.lValue = (intptr_t)&ud.coop;
    aGameVars[Gv_GetVarIndex("MULTIMODE")].val.lValue = (intptr_t)&ud.multimode;

    aGameVars[Gv_GetVarIndex("myconnectindex")].val.lValue = (intptr_t)&myconnectindex;
    aGameVars[Gv_GetVarIndex("screenpeek")].val.lValue = (intptr_t)&screenpeek;
    aGameVars[Gv_GetVarIndex("currentweapon")].val.lValue = (intptr_t)&g_currentweapon;
    aGameVars[Gv_GetVarIndex("gs")].val.lValue = (intptr_t)&g_gs;
    aGameVars[Gv_GetVarIndex("looking_arc")].val.lValue = (intptr_t)&g_looking_arc;
    aGameVars[Gv_GetVarIndex("gun_pos")].val.lValue = (intptr_t)&g_gun_pos;
    aGameVars[Gv_GetVarIndex("weapon_xoffset")].val.lValue = (intptr_t)&g_weapon_xoffset;
    aGameVars[Gv_GetVarIndex("weaponcount")].val.lValue = (intptr_t)&g_kb;
    aGameVars[Gv_GetVarIndex("looking_angSR1")].val.lValue = (intptr_t)&g_looking_angSR1;
    aGameVars[Gv_GetVarIndex("xdim")].val.lValue = (intptr_t)&xdim;
    aGameVars[Gv_GetVarIndex("ydim")].val.lValue = (intptr_t)&ydim;
    aGameVars[Gv_GetVarIndex("windowx1")].val.lValue = (intptr_t)&windowxy1.x;
    aGameVars[Gv_GetVarIndex("windowx2")].val.lValue = (intptr_t)&windowxy2.x;
    aGameVars[Gv_GetVarIndex("windowy1")].val.lValue = (intptr_t)&windowxy1.y;
    aGameVars[Gv_GetVarIndex("windowy2")].val.lValue = (intptr_t)&windowxy2.y;
    aGameVars[Gv_GetVarIndex("totalclock")].val.lValue = (intptr_t)&totalclock;
    //aGameVars[Gv_GetVarIndex("lastvisinc")].val.lValue = (intptr_t)&lastvisinc;
    aGameVars[Gv_GetVarIndex("numsectors")].val.lValue = (intptr_t)&numsectors;
    aGameVars[Gv_GetVarIndex("numplayers")].val.lValue = (intptr_t)&numplayers;
    aGameVars[Gv_GetVarIndex("current_menu")].val.lValue = (intptr_t)&g_currentMenu;
    aGameVars[Gv_GetVarIndex("viewingrange")].val.lValue = (intptr_t)&viewingrange;
    aGameVars[Gv_GetVarIndex("yxaspect")].val.lValue = (intptr_t)&yxaspect;
    aGameVars[Gv_GetVarIndex("gravitationalconstant")].val.lValue = (intptr_t)&g_spriteGravity;
    aGameVars[Gv_GetVarIndex("gametype_flags")].val.lValue = (intptr_t)&Gametypes[ud.coop].flags;
    aGameVars[Gv_GetVarIndex("framerate")].val.lValue = (intptr_t)&g_currentFrameRate;

    aGameVars[Gv_GetVarIndex("camerax")].val.lValue = (intptr_t)&ud.camerapos.x;
    aGameVars[Gv_GetVarIndex("cameray")].val.lValue = (intptr_t)&ud.camerapos.y;
    aGameVars[Gv_GetVarIndex("cameraz")].val.lValue = (intptr_t)&ud.camerapos.z;
    aGameVars[Gv_GetVarIndex("cameraang")].val.lValue = (intptr_t)&ud.cameraq16ang;
    aGameVars[Gv_GetVarIndex("cameraq16ang")].val.lValue = (intptr_t)&ud.cameraq16ang;
    aGameVars[Gv_GetVarIndex("camerahoriz")].val.lValue = (intptr_t)&ud.cameraq16horiz; // XXX FIXME
    aGameVars[Gv_GetVarIndex("cameraq16horiz")].val.lValue = (intptr_t)&ud.cameraq16horiz; // XXX FIXME
    aGameVars[Gv_GetVarIndex("camerasect")].val.lValue = (intptr_t)&ud.camerasect;
    aGameVars[Gv_GetVarIndex("cameradist")].val.lValue = (intptr_t)&g_cameraDistance;
    aGameVars[Gv_GetVarIndex("cameraclock")].val.lValue = (intptr_t)&g_cameraClock;

    // Prediction
    aGameVars[Gv_GetVarIndex("myx")].val.lValue = (intptr_t)&predictedPlayer.pos.x;
    aGameVars[Gv_GetVarIndex("myy")].val.lValue = (intptr_t)&predictedPlayer.pos.y;
    aGameVars[Gv_GetVarIndex("myz")].val.lValue = (intptr_t)&predictedPlayer.pos.z;
    aGameVars[Gv_GetVarIndex("omyx")].val.lValue = (intptr_t)&predictedPlayer.opos.x;
    aGameVars[Gv_GetVarIndex("omyy")].val.lValue = (intptr_t)&predictedPlayer.opos.y;
    aGameVars[Gv_GetVarIndex("omyz")].val.lValue = (intptr_t)&predictedPlayer.opos.z;
    aGameVars[Gv_GetVarIndex("myxvel")].val.lValue = (intptr_t)&predictedPlayer.vel.x;
    aGameVars[Gv_GetVarIndex("myyvel")].val.lValue = (intptr_t)&predictedPlayer.vel.y;
    aGameVars[Gv_GetVarIndex("myzvel")].val.lValue = (intptr_t)&predictedPlayer.vel.z;

    aGameVars[Gv_GetVarIndex("myhoriz")].val.lValue = (intptr_t)&predictedPlayer.q16horiz;
    aGameVars[Gv_GetVarIndex("myhorizoff")].val.lValue = (intptr_t)&predictedPlayer.q16horizoff;
    aGameVars[Gv_GetVarIndex("omyhoriz")].val.lValue = (intptr_t)&predictedPlayer.oq16horiz;
    aGameVars[Gv_GetVarIndex("omyhorizoff")].val.lValue = (intptr_t)&predictedPlayer.oq16horizoff;
    aGameVars[Gv_GetVarIndex("myang")].val.lValue = (intptr_t)&predictedPlayer.q16ang;
    aGameVars[Gv_GetVarIndex("omyang")].val.lValue = (intptr_t)&predictedPlayer.oq16ang;
    aGameVars[Gv_GetVarIndex("mycursectnum")].val.lValue = (intptr_t)&predictedPlayer.cursectnum;
    aGameVars[Gv_GetVarIndex("myjumpingcounter")].val.lValue = (intptr_t)&predictedPlayer.jumping_counter;

    aGameVars[Gv_GetVarIndex("myjumpingtoggle")].val.lValue = (intptr_t)&predictedPlayer.jumping_toggle;
    aGameVars[Gv_GetVarIndex("myonground")].val.lValue = (intptr_t)&predictedPlayer.on_ground;
    aGameVars[Gv_GetVarIndex("myhardlanding")].val.lValue = (intptr_t)&predictedPlayer.hard_landing;
    aGameVars[Gv_GetVarIndex("myreturntocenter")].val.lValue = (intptr_t)&predictedPlayer.return_to_center;

    aGameVars[Gv_GetVarIndex("predicting")].val.lValue = (intptr_t)&oldnet_predicting;
    // ---

    aGameVars[Gv_GetVarIndex("display_mirror")].val.lValue = (intptr_t)&display_mirror;
    aGameVars[Gv_GetVarIndex("randomseed")].val.lValue = (intptr_t)&randomseed;

    aGameVars[Gv_GetVarIndex("NUMWALLS")].val.lValue = (intptr_t)&numwalls;
    aGameVars[Gv_GetVarIndex("NUMSECTORS")].val.lValue = (intptr_t)&numsectors;
    aGameVars[Gv_GetVarIndex("Numsprites")].val.lValue = (intptr_t)&Numsprites;

    aGameVars[Gv_GetVarIndex("lastsavepos")].val.lValue = (intptr_t)&g_lastSaveSlot;

    aGameVars[Gv_GetVarIndex("numteams")].val.lValue = (intptr_t)&numTeams;

# ifdef USE_OPENGL
    aGameVars[Gv_GetVarIndex("rendmode")].val.lValue = (intptr_t)&rendmode;
# endif
}
