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

#include "m32script.h"
#include "m32def.h"
#include "osd.h"
#include "keys.h"
#ifdef POLYMER
#include "polymer.h"
#endif

#define _m32vars_c_
#include "m32structures.cpp"

#define M32VARS_SHARED_CLEAR_AND_ARRAY
#include "../../build/include/m32vars_core_shared.cpp.inc"

#define M32VARS_REPORT_REDEFINE_INTERNAL(pszLabel) do { \
    C_ReportError(-1); \
    LOG_F(WARNING, "%s:%d: warning: cannot redefine internal gamevar `%s'.", g_szScriptFileName, g_lineNumber, pszLabel); \
} while (0)
#define M32VARS_SHARED_NEWVAR
#include "../../build/include/m32vars_core_shared.cpp.inc"

#include "../../build/include/m32vars_accessors_shared.cpp.inc"

#define M32VARS_SHARED_KEY_ARRAYS
#include "../../build/include/m32vars_core_shared.cpp.inc"

static void Gv_AddSystemVars(void)
{
    // only call ONCE
    int32_t hlcnt_id, hlscnt_id;

#define M32VARS_SHARED_COMMON_SYSTEM_VARS
#include "../../build/include/m32vars_core_shared.cpp.inc"

#define M32VARS_SHARED_CLIPBOARD_VARS
#include "../../build/include/m32vars_core_shared.cpp.inc"
    Gv_NewVar("tempxpanning",(intptr_t)&tempxpanning, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempypanning",(intptr_t)&tempypanning, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempxvel",(intptr_t)&tempxvel, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempyvel",(intptr_t)&tempyvel, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempzvel",(intptr_t)&tempzvel, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempstatnum",(intptr_t)&tempstatnum, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempblend",(intptr_t)&tempblend, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);
    Gv_NewVar("tempsectornum",(intptr_t)&tempsectornum, GAMEVAR_INTPTR|GAMEVAR_SYSTEM|GAMEVAR_READONLY);

#define M32VARS_STARTPOS_INTPTR_FLAGS (GAMEVAR_INTPTR | GAMEVAR_SYSTEM)
#define M32VARS_STARTPOS_SHORTPTR_FLAGS (GAMEVAR_SHORTPTR | GAMEVAR_SYSTEM)
#define M32VARS_HAS_MOUSEBITS
#define M32VARS_SHARED_START_MOUSE_VARS
#include "../../build/include/m32vars_core_shared.cpp.inc"

#define M32VARS_SHARED_EDITOR_VIEW_VARS
#include "../../build/include/m32vars_core_shared.cpp.inc"

#define M32VARS_SHARED_POLYMER_VARS
#include "../../build/include/m32vars_core_shared.cpp.inc"

#define M32VARS_SHARED_SYSTEM_ARRAYS
#include "../../build/include/m32vars_core_shared.cpp.inc"
}

#define M32VARS_SHARED_INIT
#include "../../build/include/m32vars_core_shared.cpp.inc"
