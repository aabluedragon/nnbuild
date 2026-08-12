//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

// This object is shared by the editors of *all* Build games!

#include "compat.h"

#include "m32script.h"
#include "m32def.h"

#include "sounds_mapster32.h"

#include "osd.h"
#include "keys.h"
#include "common.h"

#include "colmatch.h"

// Shared Mapster32 VM setup and validation helpers.
#define M32EXEC_GETVAR Gv_GetVarX
#include "../../build/include/m32exec_vm_shared.cpp.inc"

int32_t VM_Execute(int32_t once)
{
    int32_t tw = *insptr;

    // jump directly into the loop, saving us from the checks during the first iteration
    goto skip_check;

    while (!once)
    {
        if (vm.flags)
            return 1;

        tw = *insptr;

skip_check:
        //      Bsprintf(g_szBuf,"Parsing: %d",*insptr);
        //      AddLog(g_szBuf);

        g_errorLineNum = tw>>12;
        g_tw = (tw &= 0xFFF);

        switch (tw)
        {
// *** basic commands
        case CON_NULLOP:
            insptr++;
            continue;

        case CON_STATE:
        {
            instype *const tempscrptr = insptr+2;
            const int32_t stateidx = *(insptr+1), o_g_st = vm.g_st, oret=vm.flags&VMFLAG_RETURN;
            void *const olocalvars = aGameArrays[M32_LOCAL_ARRAY_ID].vals;
#ifdef M32_LOCALS_VARARRAY
            int32_t localvars[statesinfo[stateidx].numlocals];
#else
            int32_t localvars[M32_LOCALS_FIXEDNUM];
#endif

            // needed since any read access before initialization would cause undefined behaviour
            if (statesinfo[stateidx].numlocals > 0)
                Bmemset(localvars, 0, statesinfo[stateidx].numlocals*sizeof(int32_t));

            insptr = apScript + statesinfo[stateidx].ofs;
            vm.g_st = 1+MAXEVENTS+stateidx;
            aGameArrays[M32_LOCAL_ARRAY_ID].vals = localvars;
            VM_Execute(0);
            aGameArrays[M32_LOCAL_ARRAY_ID].vals = olocalvars;
            vm.g_st = o_g_st;
            vm.flags &= ~VMFLAG_RETURN;
            vm.flags |= oret;
            insptr = tempscrptr;
        }
        continue;

        case CON_RETURN:
            vm.flags |= VMFLAG_RETURN;
            return 1;
        case CON_BREAK:
            vm.flags |= VMFLAG_BREAK;
            // XXX: may not be cleared subsequently?
            fallthrough__;
        case CON_ENDS:
            return 1;

        case CON_ELSE:
            insptr++;
            insptr += *insptr;
            continue;

        case CON_ENDSWITCH:
            vm.flags &= ~VMFLAG_BREAK;
            fallthrough__;
        case CON_ENDEVENT:
            insptr++;
            return 1;

        case CON_SWITCH:
            insptr++; // p-code
            {
                // command format:
                // variable ID to check
                // script offset to 'end'
                // count of case statements
                // script offset to default case (null if none)
                // For each case: value, ptr to code
                //AddLog("Processing Switch...");
                int32_t lValue=Gv_GetVarX(*insptr++), lEnd=*insptr++, lCases=*insptr++;
                instype *lpDefault=insptr++, *lpCases=insptr, *lCodeInsPtr;
                int32_t bMatched=0, lCheckCase;
                int32_t left,right;

                insptr += lCases*2;
                lCodeInsPtr = insptr;
                //Bsprintf(g_szBuf,"lEnd= %d *lpDefault=%d",lEnd,*lpDefault); AddLog(g_szBuf);
                //Bsprintf(g_szBuf,"Checking %d cases for %d",lCases, lValue); AddLog(g_szBuf);
                left = 0;
                right = lCases-1;
                while (!bMatched)
                {
                    //Bsprintf(g_szBuf,"Checking #%d Value= %d",lCheckCase, lpCases[lCheckCase*2]); AddLog(g_szBuf);
                    lCheckCase=(left+right)/2;
                    //                initprintf("(%2d..%2d..%2d) [%2d..%2d..%2d]==%2d\n",left,lCheckCase,right,lpCases[left*2],lpCases[lCheckCase*2],lpCases[right*2],lValue);
                    if (lpCases[lCheckCase*2] > lValue)
                        right = lCheckCase-1;
                    else if (lpCases[lCheckCase*2] < lValue)
                        left = lCheckCase+1;
                    else if (lpCases[lCheckCase*2] == lValue)
                    {
                        //AddLog("Found Case Match");
                        //Bsprintf(g_szBuf,"insptr=%d. lCheckCase=%d, offset=%d, &script[0]=%d", (int32_t)insptr,(int32_t)lCheckCase,lpCases[lCheckCase*2+1],(int32_t)&script[0]); AddLog(g_szBuf);
                        // fake a 2-d Array
                        insptr = lCodeInsPtr + lpCases[lCheckCase*2+1];
                        //Bsprintf(g_szBuf,"insptr=%d. ",     (int32_t)insptr); AddLog(g_szBuf);
                        VM_Execute(0);
                        //AddLog("Done Executing Case");
                        bMatched=1;
                    }

                    if (right-left < 0)
                        break;
                }

                if (!bMatched)
                {
                    if (*lpDefault >= 0)
                    {
                        //AddLog("No Matching Case: Using Default");
                        insptr = lCodeInsPtr + *lpDefault;
                        VM_Execute(0);
                    }
//                    else
//                    {
//                        //AddLog("No Matching Case: No Default to use");
//                    }
                }
                insptr = (instype *)(lCodeInsPtr + lEnd);
                vm.flags &= ~VMFLAG_BREAK;
                //Bsprintf(g_szBuf,"insptr=%d. ",     (int32_t)insptr); AddLog(g_szBuf);
                //AddLog("Done Processing Switch");
                continue;
            }

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_array_control_shared.cpp.inc"

// *** var & varvar ops
        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_var_ops_shared.cpp.inc"

// *** other math
        case CON_FTOI:
            insptr++;
            {
                int32_t bits=Gv_GetVarX(*insptr), scale=*(insptr+1);
                float fval = *((float *)&bits);
// rounding must absolutely be!
//OSD_Printf("ftoi: bits:%8x, scale=%d, fval=%f, (int32_t)(fval*scale)=%d\n", bits, scale, fval, (int32_t)(fval*scale));
                Gv_SetVarX(*insptr, (int32_t)Blrintf(fval * scale));
            }
            insptr += 2;
            continue;

        case CON_ITOF:
            insptr++;
            {
                int32_t scaled=Gv_GetVarX(*insptr), scale=*(insptr+1);
                float fval = (float)scaled/(float)scale;
                Gv_SetVarX(*insptr, *((int32_t *)&fval));
            }
            insptr += 2;
            continue;

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_math_ops_shared.cpp.inc"

// *** if & while
        #define M32EXEC_GETVAR Gv_GetVarX
#include "../../build/include/m32exec_varvar_conditionals_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#include "../../build/include/m32exec_whilevar_shared.cpp.inc"
        #define M32EXEC_GETVAR Gv_GetVarX
#include "../../build/include/m32exec_whilevarvar_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#define M32EXEC_SECTBITMAP_SIZE (MAXSECTORS>>3)
#define M32EXEC_SECTBITMAP_TEST(BITMAP, INDEX) ((BITMAP)[(INDEX)>>3]&(1<<((INDEX)&7)))
#include "../../build/include/m32exec_collect_sort_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_for_iter_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#include "../../build/include/m32exec_conditionals_shared.cpp.inc"

// vvv CURSPR
        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SHOW2D_TEST(BITMAP, INDEX) ((BITMAP)[(INDEX)>>3]&(1<<((INDEX)&7)))
#define M32EXEC_DISTPOS ((spritetype *)&pos)
#include "../../build/include/m32exec_sprite_conditionals_shared.cpp.inc"
// ^^^

// *** BUILD functions
        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #define M32EXEC_USPRITE_FROM_TSPRITE(TSP) ((uspritetype *)(TSP))
        #include "../../build/include/m32exec_sprite_build_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #include "../../build/include/m32exec_geometry_shared.cpp.inc"
        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #define M32EXEC_HIT_X(HIT) ((HIT).pos.x)
        #define M32EXEC_HIT_Y(HIT) ((HIT).pos.y)
        #define M32EXEC_HIT_Z(HIT) ((HIT).pos.z)
        #include "../../build/include/m32exec_space_query_shared.cpp.inc"
        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #include "../../build/include/m32exec_neartag_slope_shared.cpp.inc"

        case CON_UPDATECURSECTNUM:
            insptr++;
            updatesectorz(pos.x, pos.y, pos.z, &cursectnum);
            continue;

        case CON_UPDATESECTOR:
        case CON_UPDATESECTORZ:
            insptr++;
            {
                int32_t x=Gv_GetVarX(*insptr++), y=Gv_GetVarX(*insptr++);
                int32_t z=(tw==CON_UPDATESECTORZ)?Gv_GetVarX(*insptr++):0;
                int32_t var=*insptr++;
                int16_t w;

                X_ERROR_INVALIDCI();
                w=sprite[vm.spriteNum].sectnum;

                if (tw==CON_UPDATESECTOR) updatesector(x,y,&w);
                else updatesectorz(x,y,z,&w);

                Gv_SetVarX(var, w);
                continue;
            }

        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #include "../../build/include/m32exec_sprite_links_misc_shared.cpp.inc"

// *** stuff
        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_BITMAP_SET(BITMAP, INDEX) ((BITMAP)[(INDEX)>>3] |= (1<<((INDEX)&7)))
        #define M32EXEC_BITMAP_CLEAR(BITMAP, INDEX) ((BITMAP)[(INDEX)>>3] &= ~(1<<((INDEX)&7)))
        #include "../../build/include/m32exec_highlight_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #include "../../build/include/m32exec_log_debug_shared.cpp.inc"

// *** strings
        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #define M32EXEC_PRINTEXT256_ALLOWED() in3dmode()
        #define M32EXEC_PRINTEXT16_COL(C) editorcolors[(C)&255]
        #include "../../build/include/m32exec_text_input_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_qstring_shared.cpp.inc"

        // *** findnear*
// CURSPR vvv
        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_findnear_shared.cpp.inc"

        // ^^^

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_SETVAR Gv_SetVarX
#include "../../build/include/m32exec_fields_shared.cpp.inc"
// ^^^
        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_EDITOR_COLOR_INDEX(C) ((C)&15)
#include "../../build/include/m32exec_drawline16_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#define M32EXEC_EDITOR_COLOR_INDEX(C) ((C)&15)
#include "../../build/include/m32exec_drawcircle16_shared.cpp.inc"

        #define M32EXEC_GETVAR Gv_GetVarX
#include "../../build/include/m32exec_rotatesprite_shared.cpp.inc"

// *** sounds
        #define M32EXEC_GETVAR Gv_GetVarX
        #define M32EXEC_SETVAR Gv_SetVarX
        #include "../../build/include/m32exec_sound_shared.cpp.inc"

        #include "../../build/include/m32exec_mode_interaction_shared.cpp.inc"

        #include "../../build/include/m32exec_default_error_shared.cpp.inc"
        }
    }

    return 0;
}
