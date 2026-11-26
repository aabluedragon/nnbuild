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

#include "vfs.h"

// Shared Mapster32 compiler state.
#include "../../build/include/m32def_compiler_state_shared.cpp.inc"

#define M32DEF_HAS_INCLUDEOPTIONAL
#define M32DEF_HAS_WHILEVARE
#define M32DEF_HAS_WHILEVARVARE
#define M32DEF_HAS_IFSOUND
#define M32DEF_HAS_IFNOSOUNDS
#define M32DEF_HAS_IFIN2D3DMODE
#define M32DEF_HAS_UPDATESECTOR_NEIGHBOR
#define M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
#define M32DEF_HAS_SOUND_VAR_COMMANDS
#define M32DEF_HAS_DRAWLINE256
#include "../../build/include/m32def_keyword_tables_shared.cpp.inc"
#undef M32DEF_HAS_DRAWLINE256
#undef M32DEF_HAS_SOUND_VAR_COMMANDS
#undef M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
#undef M32DEF_HAS_UPDATESECTOR_NEIGHBOR
#undef M32DEF_HAS_IFIN2D3DMODE
#undef M32DEF_HAS_IFNOSOUNDS
#undef M32DEF_HAS_IFSOUND
#undef M32DEF_HAS_WHILEVARVARE
#undef M32DEF_HAS_WHILEVARE
#undef M32DEF_HAS_INCLUDEOPTIONAL

#include "../../build/include/m32def_member_labels_shared.cpp.inc"

#define M32DEF_LOG_SCRIPT_RESIZE(size, bytes) LOG_F(INFO, "Resizing code buffer to %d*%d bytes", (size), (bytes))
#define M32DEF_LOG_SCRIPT_RELOCATION(oldptr, newptr) DLOG_F(INFO, "Relocated compiled code from 0x%08" PRIxPTR " to 0x%08" PRIxPTR, (uintptr_t)(oldptr), (uintptr_t)(newptr))
#define M32DEF_UNTERMINATED_COMMENT_MESSAGE "found '/*' with no '*/'."
#define M32DEF_SHARED_LEXER_AND_HASHES
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

//Returns its code # if keyword, -1 on eof or non-keyword
#define M32DEF_LEFTBRACE_KEYWORD_MESSAGE "expected whitespace between '{' and '%s'"
#define M32DEF_RIGHTBRACE_KEYWORD_MESSAGE "expected whitespace between '}' and '%s'"
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
        sscanf(textptr+2, "%" SCNx32, (uint32_t *)num);
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

#define M32DEF_BAD_ARRAY_OR_STRUCT_SYMBOL_MESSAGE "symbol '%s' is neither an array name nor one of '(t)sprite', 'sector', 'wall' or 'light'."
#define M32DEF_LOCAL_ARRAY_INDEX_MESSAGE "local array index %d out of bounds. Size of local array '%s': %d."
#define M32DEF_EXPECT_STRUCT_LABEL_MESSAGE "syntax error. Expected '.<label>' after '%s[...]'"
#define M32DEF_WRONG_SIMPLE_VAR_TYPE_MESSAGE "variable '%s' is of the wrong type: expected simple var."
#define M32DEF_SHARED_GETNEXTVARTYPE
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_INVALID_DEFINITION_CHAR_MESSAGE "invalid character '%c' in definition!"
#define M32DEF_MALFORMED_BRANCH_MESSAGE "malformed '%s' branch"
#define M32DEF_EMPTY_BRANCH_MESSAGE "empty '%s' branch"
#define M32DEF_SHARED_VALUE_AND_BRANCH_HELPERS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_COUNT_CASE_STATEMENTS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_ORPHAN_ELSE_WARNING "found 'else' with no 'if'."
#define M32DEF_SHARED_PARSE_ELSE_BRANCH
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_SWITCH
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_CASE_OUTSIDE_SWITCH_MESSAGE "found 'case' statement when not in switch"
#define M32DEF_DEFAULT_OUTSIDE_SWITCH_MESSAGE "found 'default' statement when not in switch"
#define M32DEF_MULTIPLE_DEFAULT_MESSAGE "multiple 'default' statements found in switch"
#define M32DEF_ENDSWITCH_WITHOUT_SWITCH_MESSAGE "found 'endswitch' without matching 'switch'"
#define M32DEF_EXTRA_RIGHT_BRACE_MESSAGE "found more '}' than '{'."
#define M32DEF_SHARED_PARSE_SWITCH_CASES_AND_BRACES
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_DEFSTATE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_ENDS_WITHOUT_STATE_MESSAGE "found 'ends' without open 'state'."
#define M32DEF_LOG_DEFINED_STATE(stateIndex, stateName) LOG_F(INFO, "Defined State %3d '%s'.", (stateIndex), (stateName))
#define M32DEF_LOG_REDEFINED_STATE(stateIndex, stateName) LOG_F(INFO, "Redefined State %3d '%s'.", (stateIndex), (stateName))
#define M32DEF_SHARED_PARSE_ENDSTATE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_ENDEVENT_WITHOUT_ONEVENT_MESSAGE "found 'endevent' without open 'onevent'."
#define M32DEF_LOG_INVALID_EVENT_ID() LOG_F(ERROR, "%s:%d: invalid event ID.", g_szScriptFileName, g_lineNumber)
#define M32DEF_LOG_DEFINED_EVENT(eventIndex, eventName) LOG_F(INFO, "Defined Event %3d '%s'.", (eventIndex), (eventName))
#define M32DEF_LOG_REDEFINED_EVENT(eventIndex, eventName) LOG_F(INFO, "Redefined Event %3d '%s'.", (eventIndex), (eventName))
#define M32DEF_SHARED_PARSE_EVENT_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_DEFINE_TOP_LEVEL_MESSAGE "Can only 'define' at top level."
#define M32DEF_SHARED_PARSE_DEFINE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_LOCAL_VAR_ALREADY_DEFINED_MESSAGE "local variable '%s' already defined."
#define M32DEF_SHARED_PARSE_GAMEVAR_ARRAY_DEFINITIONS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_STATE_NOT_FOUND_MESSAGE "state '%s' not found."
#define M32DEF_SHARED_PARSE_SORT_OR_STATE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_UPDATESECTOR_NEIGHBOR
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

#define M32DEF_RESIZE_SYSTEM_ARRAY_MESSAGE "can't resize system array '%s'."
#define M32DEF_SHARED_PARSE_ARRAY_SIZE_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_VAR_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_MATH_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PARSE_QUOTE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_DRAWLINE256
#define M32DEF_SHARED_PARSE_DRAW_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
#define M32DEF_HAS_SOUND_VAR_COMMANDS
#define M32DEF_SHARED_PARSE_SIMPLE_COMMANDS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_UNKNOWN_ITERATION_TYPE_MESSAGE "unknown iteration type '%s'."
#define M32DEF_SHARED_PARSE_FOR_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_WHILEVARE
#define M32DEF_HAS_WHILEVARVARE
#define M32DEF_HAS_IFSOUND
#define M32DEF_HAS_IFNOSOUNDS
#define M32DEF_HAS_IFIN2D3DMODE
#define M32DEF_SHARED_PARSE_IF_WHILE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_INCLUDEOPTIONAL
#define M32DEF_INCLUDE_LOG_MISSING(fileName, lineNumber, includeName) LOG_F(ERROR, "%s:%d: could not find file '%s'.", (fileName), (lineNumber), (includeName))
#define M32DEF_INCLUDE_LOG_OPTIONAL_MISSING(includeName) LOG_F(INFO, "Optional include %s absent. skipping.", (includeName))
#define M32DEF_INCLUDE_LOG_FOUND(includeName, bytes) LOG_F(INFO, "Including: %s (%d bytes)", (includeName), (bytes))
#define M32DEF_SHARED_PARSE_INCLUDE_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_NULLOP_ACCEPTS_RIGHTBRACE
#define M32DEF_NULLOP_WARNING "'nullop' found without accompanying branch."
#define M32DEF_SHARED_PARSE_NULLOP_COMMAND
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_HAS_INCLUDEOPTIONAL
#define M32DEF_HAS_WHILEVARE
#define M32DEF_HAS_WHILEVARVARE
#define M32DEF_HAS_IFSOUND
#define M32DEF_HAS_IFNOSOUNDS
#define M32DEF_HAS_IFIN2D3DMODE
#define M32DEF_HAS_UPDATESECTOR_NEIGHBOR
#define M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
#define M32DEF_HAS_SOUND_VAR_COMMANDS
#define M32DEF_HAS_DRAWLINE256
#include "../../build/include/m32def_parse_command_shared.cpp.inc"

#define M32DEF_ADD_EXTENDED_EVENTS
#define M32DEF_ADD_MAXSOUNDS_DEFINE
#define M32DEF_ADD_INT32_MIN_DEFINE
#define M32DEF_SHARED_DEFAULT_DEFINITIONS
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_COMPILATION_INFO
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_COMPILE_LOG_FILE_NOT_FOUND(path) LOG_F(WARNING, "M32 file '%s' not found.", (path))
#define M32DEF_COMPILE_LOG_COMPILING(path, bytes) LOG_F(INFO, "--- Compiling: %s (%d bytes)", (path), (bytes))
#define M32DEF_COMPILE_LOG_SCRIPT_COMPILED(ms) LOG_F(INFO, "Script compiled in %dms", (ms))
#define M32DEF_COMPILE_REPORT_ERRORS(errors, warnings) LOG_F(ERROR, "--- Found %d errors, %d warnings.", (errors), (warnings))
#define M32DEF_COMPILE_REPORT_WARNINGS(warnings) LOG_F(WARNING, "--- Found %d warnings.", (warnings))
#define M32DEF_COMPILE_AFTER_FILE_OPENED() VM_OnEvent(EVENT_INIT, -1)
#define M32DEF_SHARED_COMPILE
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"



EDUKE32_STATIC_ASSERT(ARRAY_SIZE(keyw)-1 == CON_END);

#define M32DEF_REPORT_BLOCK_CONTEXT(fileName, blockKind, blockName) LOG_F(INFO, "%s: In %s '%s':", (fileName), (blockKind), (blockName))
#define M32DEF_REPORT_TOP_LEVEL(fileName) LOG_F(INFO, "%s: At top level:", (fileName))
#define M32DEF_REPORT_ERROR(Text, ...) LOG_F(ERROR, "%s: %d: " Text, g_szScriptFileName, g_lineNumber, ## __VA_ARGS__)
#define M32DEF_REPORT_WARNING(Text, ...) LOG_F(WARNING, "%s:%d: " Text, g_szScriptFileName, g_lineNumber, ## __VA_ARGS__)
#define M32DEF_SHARED_REPORTERROR
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"

#define M32DEF_SHARED_PRINT_ERROR_POSITION
#include "../../build/include/m32def_compiler_helpers_shared.cpp.inc"
