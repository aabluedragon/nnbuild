// Shared Mapster32 structure-accessor translation-unit wrapper.

#ifndef M32_STRUCT_M32DEF_HEADER
# define M32_STRUCT_M32DEF_HEADER "m32def.h"
#endif

#include "compat.h"
#include "m32script.h"
#include M32_STRUCT_M32DEF_HEADER
#ifdef POLYMER
# include "prlights.h"
#endif

// how: bitfield: 1=set? 2=vars? 4=use spriteext[].tspr? (otherwise use tsprite[])
#define ACCESS_SET 1
#define ACCESS_USEVARS 2

/// This file is #included into other files, so don't define variables here!

#include "m32structures_shared.cpp.inc"
