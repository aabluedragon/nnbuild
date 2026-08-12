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
#include "cache1d.h"
#include "sounds_mapster32.h"

//#include "osd.h"
#include "keys.h"

// Shared Mapster32 compiler state.
#include "../../build/include/m32def_compiler_state_shared.cpp.inc"

#define M32DEF_HAS_IFSOUND
#define M32DEF_HAS_IFNOSOUNDS
#define M32DEF_HAS_SOUND_VAR_COMMANDS
#include "../../build/include/m32def_keyword_tables_shared.cpp.inc"
#undef M32DEF_HAS_SOUND_VAR_COMMANDS
#undef M32DEF_HAS_IFNOSOUNDS
#undef M32DEF_HAS_IFSOUND

#define M32DEF_SPRITE_SECT_STAT_READONLY 1
#include "../../build/include/m32def_member_labels_shared.cpp.inc"
#undef M32DEF_SPRITE_SECT_STAT_READONLY

#define M32DEF_LOG_SCRIPT_RESIZE(size, bytes) initprintf("Resizing code buffer to %d*%d bytes\n", (size), (bytes))
#define M32DEF_LOG_SCRIPT_RELOCATION(oldptr, newptr) buildprint("Relocating compiled code from to 0x", hex((intptr_t)(oldptr)), " to 0x", hex((intptr_t)(newptr)), "\n")
#define M32DEF_UNTERMINATED_COMMENT_MESSAGE "found `/*' with no `*/'."
#define M32DEF_SHARED_LEXER_AND_HASHES
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

//Returns its code # if keyword, -1 on eof or non-keyword
#define M32DEF_SHARED_GETNEXTKEYWORD
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_GAMEVAR_ARRAY_LOOKUP
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_REDEFINITION_COMPARE
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

// gamevar type restrictions
#define GV_WRITABLE GAMEVAR_READONLY
#define GV_SIMPLE GAMEVAR_SPECIAL

static int32_t parse_integer_literal(int32_t *num)
{
    if (textptr[0] == '0' && tolower(textptr[1])=='x')
        sscanf(textptr+2, "%" SCNx32, (uint32_t *)&num);
    else
    {
        long lnum;
        errno = 0;
        lnum = Bstrtol(textptr, NULL, 10);
        if (errno || (sizeof(long)>4 && (lnum<INT32_MIN || lnum>INT32_MAX)))
        {
            C_CUSTOMERROR("integer literal exceeds bitwidth.");
            return 1;
        }
        *num = (int32_t)lnum;
    }

    return 0;
}

#define M32DEF_SHARED_GETNEXTVARTYPE
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_LABELTYPE_FREE(ptr) Bfree(ptr)
#define M32DEF_SHARED_VALUE_AND_BRANCH_HELPERS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_COUNT_CASE_STATEMENTS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_ELSE_BRANCH
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_SWITCH
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_SWITCH_CASES_AND_BRACES
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_DEFSTATE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_ENDSTATE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_EVENT_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_DEFINE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_GAMEVAR_ARRAY_DEFINITIONS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_SORT_OR_STATE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_BUILD_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_GEOMETRY_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_TEXT_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_FINDNEAR_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_ARRAY_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_ARRAY_SIZE_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_VAR_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_MATH_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_QUOTE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_DRAW_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_SOUND_VAR_COMMANDS
#define M32DEF_SHARED_PARSE_SIMPLE_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_FOR_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_IFSOUND
#define M32DEF_HAS_IFNOSOUNDS
#define M32DEF_SHARED_PARSE_IF_WHILE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_INCLUDE_FILE_TYPE int32_t
#define M32DEF_INCLUDE_FILE_INVALID -1
#define M32DEF_INCLUDE_FILE_IS_INVALID(fp) ((fp) < 0)
#define M32DEF_INCLUDE_FILE_FREE(ptr) Bfree(ptr)
#define M32DEF_SHARED_PARSE_INCLUDE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_NULLOP_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_IFSOUND
#define M32DEF_HAS_IFNOSOUNDS
#define M32DEF_HAS_SOUND_VAR_COMMANDS
#include "../../build/include/m32def_parse_command_shared.cpp.inc"

#define M32DEF_ADD_MAXSOUNDS_DEFINE
#define M32DEF_SHARED_DEFAULT_DEFINITIONS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_COMPILATION_INFO_START() initprintf(" \n")
#define M32DEF_COMPILATION_INFO_CODE_SIZE(scriptSize, instSize) initprintf("Compiled code info: (size=%ld*%d bytes)\n", (scriptSize), (instSize))
#define M32DEF_COMPILATION_INFO_LABELS(userLabels, labelCapacity, indirectConstants) initprintf("  %d/%d user labels, %d/65536 indirect constants,\n", (userLabels), (labelCapacity), (indirectConstants))
#define M32DEF_COMPILATION_INFO_VARS(userVars, varCapacity, userArrays, arrayCapacity) initprintf("  %d/%d user variables, %d/%d user arrays\n", (userVars), (varCapacity), (userArrays), (arrayCapacity))
#define M32DEF_COMPILATION_INFO_EVENTS(stateCount, eventCount, eventCapacity) initprintf("  %d states, %d/%d defined events\n", (stateCount), (eventCount), (eventCapacity))
#define M32DEF_COMPILATION_INFO_QUOTES(quoteCount, quoteCapacity, redefinitionCount, redefinitionCapacity) initprintf("  %d/%d quotes, %d/%d quote redefinitions\n", (quoteCount), (quoteCapacity), (redefinitionCount), (redefinitionCapacity))
#define M32DEF_SHARED_COMPILATION_INFO
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_COMPILE_FILE_TYPE int32_t
#define M32DEF_COMPILE_FILE_INVALID -1
#define M32DEF_COMPILE_FILE_IS_INVALID(fp) ((fp) == -1)
#define M32DEF_COMPILE_FREE(ptr) Bfree(ptr)
#define M32DEF_SHARED_COMPILE
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"



EDUKE32_STATIC_ASSERT(ARRAY_SIZE(keyw)-1 == CON_END);

#define M32DEF_SHARED_REPORTERROR
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_PRINT_ERROR_POSITION_LINE(buf) initprintf("%s\n", (buf))
#define M32DEF_PRINT_ERROR_POSITION_FREE(buf) Bfree(buf)
#define M32DEF_SHARED_PRINT_ERROR_POSITION
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"
