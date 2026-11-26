// Shared Mapster32 variable accessors for Duke3D, Blood, and Redneck Rampage.
// The including translation unit supplies game-local state, VM_Access* helpers,
// and the public accessor names below.

#ifndef M32VARS_GETVAR_NAME
# define M32VARS_GETVAR_NAME Gv_GetVar
#endif
#ifndef M32VARS_SETVAR_NAME
# define M32VARS_SETVAR_NAME Gv_SetVar
#endif

int32_t __fastcall Gv_GetVarN(int32_t id)  // 'N' for "no side-effects"... vars and locals only!
{
    if (id == M32_THISACTOR_VAR_ID)
        return vm.spriteNum;

    switch (id&M32_VARTYPE_MASK)
    {
    case M32_FLAG_VAR:
        id &= (MAXGAMEVARS-1);

        switch (aGameVars[id].dwFlags & (GAMEVAR_USER_MASK|GAMEVAR_PTR_MASK))
        {
        case 0:
            return aGameVars[id].val.lValue;
        case GAMEVAR_PERBLOCK:
            return aGameVars[id].val.plValues[vm.g_st];
        case GAMEVAR_FLOATPTR:
        case GAMEVAR_INTPTR:
            return *((int32_t *)aGameVars[id].val.lValue);
        case GAMEVAR_SHORTPTR:
            return *((int16_t *)aGameVars[id].val.lValue);
        case GAMEVAR_CHARPTR:
            return *((uint8_t *)aGameVars[id].val.lValue);
        default:
            M32_ERROR("Gv_GetVarN(): WTF??");
            return -1;
        }

    case M32_FLAG_LOCAL:
    {
        int32_t index = id&(MAXGAMEVARS-1);
        // no bounds checking since it's done at script compilation time
        return ((int32_t *)aGameArrays[M32_LOCAL_ARRAY_ID].vals)[index];
    }

    default:
        M32_ERROR("Gv_GetVarN(): invalid var code %0x08x", id);
        return -1;
    }
}

int32_t __fastcall M32VARS_GETVAR_NAME(int32_t id)
{
    int32_t negateResult = !!(id&M32_FLAG_NEGATE);

    if (id == M32_THISACTOR_VAR_ID)
        return vm.spriteNum;

    id &= ~M32_FLAG_NEGATE;

    if ((id & M32_BITS_MASK) == M32_FLAG_CONSTANT)
    {
        switch (id&3)
        {
        case 0:
            return ((int16_t)(id>>16));
        case 1:
            return constants[(id>>16)&0xffff];
        case 2:
            return (labelval[(id>>16)&0xffff] ^ -negateResult) + negateResult;
        default:
            M32_ERROR("Gv_GetVarX() (constant): WTF??");
            return -1;
        }
    }


    switch (id&M32_VARTYPE_MASK)
    {
    case M32_FLAG_ARRAY:
    {
        int32_t index;

        index = (int32_t)((id>>16)&0xffff);
        if (!(id&M32_FLAG_CONSTANTINDEX))
            index = Gv_GetVarN(index);

        id &= (MAXGAMEARRAYS-1);

        const int32_t siz = Gv_GetArraySize(id);
        const gamearray_t *const gar = &aGameArrays[id];

        if (index < 0 || index >= siz)
        {
            M32_ERROR("Gv_GetVarX(): invalid array index (%s[%d])", gar->szLabel, index);
            return -1;
        }

        if (gar->dwFlags & GAMEARRAY_STRIDE2)
            index <<= 1;

        switch (gar->dwFlags & GAMEARRAY_TYPE_MASK)
        {
        case 0:
        case GAMEARRAY_INT32:
            return (((int32_t *)gar->vals)[index] ^ -negateResult) + negateResult;
        case GAMEARRAY_INT16:
            return (((int16_t *)gar->vals)[index] ^ -negateResult) + negateResult;
        case GAMEARRAY_UINT8:
            return (((uint8_t *)gar->vals)[index] ^ -negateResult) + negateResult;
        default:
            M32_ERROR("Gv_GetVarX() (array): WTF??");
            return -1;
        }
    }
    case M32_FLAG_STRUCT:
    {
        int32_t index, memberid;

        index = (id>>16)&0x7fff;
        if (!(id&M32_FLAG_CONSTANTINDEX))
            index = Gv_GetVarN(index);

        memberid = (id>>2)&63;

        switch (id&3)
        {
        case M32_SPRITE_VAR_ID:
            return (VM_AccessSprite(0, index, memberid, 0) ^ -negateResult) + negateResult;
        case M32_SECTOR_VAR_ID:
            return (VM_AccessSector(0, index, memberid, 0) ^ -negateResult) + negateResult;
        case M32_WALL_VAR_ID:
            return (VM_AccessWall(0, index, memberid, 0) ^ -negateResult) + negateResult;
        case M32_TSPRITE_VAR_ID:
            return (VM_AccessTsprite(0, index, memberid, 0) ^ -negateResult) + negateResult;
        default:
            M32_ERROR("Gv_GetVarX(): WTF??");
            return -1;
        }
    }
    case M32_FLAG_VAR:
    {
        id &= (MAXGAMEVARS-1);

        switch (aGameVars[id].dwFlags & (GAMEVAR_USER_MASK|GAMEVAR_PTR_MASK))
        {
        case 0:
            return (aGameVars[id].val.lValue ^ -negateResult) + negateResult;
        case GAMEVAR_PERBLOCK:
            return (aGameVars[id].val.plValues[vm.g_st] ^ -negateResult) + negateResult;
        case GAMEVAR_FLOATPTR:
        {
            union { int32_t ival; float fval; };

            fval = *(float *)aGameVars[id].val.plValues;
            if (negateResult)
                fval *= -1;
            return ival;
        }
        case GAMEVAR_INTPTR:
            return (*((int32_t *)aGameVars[id].val.lValue) ^ -negateResult) + negateResult;
        case GAMEVAR_SHORTPTR:
            return (*((int16_t *)aGameVars[id].val.lValue) ^ -negateResult) + negateResult;
        case GAMEVAR_CHARPTR:
            return (*((uint8_t *)aGameVars[id].val.lValue) ^ -negateResult) + negateResult;
        default:
            M32_ERROR("Gv_GetVarX(): WTF??");
            return -1;
        }
    }
    case M32_FLAG_LOCAL:
    {
        int32_t index = id&(MAXGAMEVARS-1);
        // no bounds checking since it's done at script compilation time
        return (((int32_t *)aGameArrays[M32_LOCAL_ARRAY_ID].vals)[index] ^ -negateResult) + negateResult;
    }
    }  // switch (id&M32_VARTYPE_MASK)

    return 0;  // never reached
}


void __fastcall M32VARS_SETVAR_NAME(int32_t id, int32_t lValue)
{
    switch (id&M32_VARTYPE_MASK)
    {
    case M32_FLAG_ARRAY:
    {
        int32_t index;

        index = (id>>16)&0xffff;
        if (!(id&M32_FLAG_CONSTANTINDEX))
            index = Gv_GetVarN(index);

        id &= (MAXGAMEARRAYS-1);

        const int32_t siz = Gv_GetArraySize(id);
        gamearray_t *const gar = &aGameArrays[id];

        if (index < 0 || index >= siz)
        {
            M32_ERROR("Gv_SetVarX(): invalid array index %s[%d], size=%d", gar->szLabel, index, siz);
            return;
        }

        // NOTE: GAMEARRAY_READONLY arrays can be modified in expert mode.
        Bassert((gar->dwFlags & GAMEARRAY_STRIDE2) == 0);

        switch (gar->dwFlags & GAMEARRAY_TYPE_MASK)
        {
        case 0:
        case GAMEARRAY_INT32:
            ((int32_t *)gar->vals)[index] = lValue;
            return;
        case GAMEARRAY_INT16:
            ((int16_t *)gar->vals)[index] = (int16_t)lValue;
            return;
        case GAMEARRAY_UINT8:
            ((uint8_t *)gar->vals)[index] = (uint8_t)lValue;
            return;
        default:
            M32_ERROR("Gv_SetVarX() (array): WTF??");
            return;
        }
        return;
    }
    case M32_FLAG_STRUCT:
    {
        int32_t index, memberid;

        index = (id>>16)&0x7fff;
        if (!(id&M32_FLAG_CONSTANTINDEX))
            index = Gv_GetVarN(index);

        memberid = (id>>2)&63;

        switch (id&3)
        {
        case M32_SPRITE_VAR_ID:
            VM_AccessSprite(1, index, memberid, lValue);
            return;
        case M32_SECTOR_VAR_ID:
            VM_AccessSector(1, index, memberid, lValue);
            return;
        case M32_WALL_VAR_ID:
            VM_AccessWall(1, index, memberid, lValue);
            return;
        case M32_TSPRITE_VAR_ID:
            VM_AccessTsprite(1, index, memberid, lValue);
            return;
        default:
            M32_ERROR("Gv_SetVarX(): WTF??");
            return;
        }
    }
    case M32_FLAG_VAR:
    {
        id &= (MAXGAMEVARS-1);

        switch (aGameVars[id].dwFlags & (GAMEVAR_USER_MASK|GAMEVAR_PTR_MASK))
        {
        case 0:
            aGameVars[id].val.lValue=lValue;
            return;
        case GAMEVAR_PERBLOCK:
            aGameVars[id].val.plValues[vm.g_st] = lValue;
            return;
        case GAMEVAR_FLOATPTR:
        {
            union { int32_t ival; float fval; };
            ival = lValue;

            if (fval!=fval || fval<-3.4e38 || fval > 3.4e38)
            {
                M32_ERROR("Gv_SetVarX(): tried to set float var to NaN or infinity");
                return;
            }
        }
        fallthrough__;
        case GAMEVAR_INTPTR:
            *((int32_t *)aGameVars[id].val.lValue)=(int32_t)lValue;
            return;
        case GAMEVAR_SHORTPTR:
            *((int16_t *)aGameVars[id].val.lValue)=(int16_t)lValue;
            return;
        case GAMEVAR_CHARPTR:
            *((uint8_t *)aGameVars[id].val.lValue)=(uint8_t)lValue;
            return;
        default:
            M32_ERROR("Gv_SetVarX(): WTF??");
            return;
        }
    }
    case M32_FLAG_LOCAL:
    {
        int32_t index = id&(MAXGAMEVARS-1);
        ((int32_t *)aGameArrays[M32_LOCAL_ARRAY_ID].vals)[index] = lValue;
        return;
    }
    }
}


#undef M32VARS_GETVAR_NAME
#undef M32VARS_SETVAR_NAME
