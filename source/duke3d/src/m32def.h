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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#ifndef m32def_h_
#define m32def_h_

// Per-game feature flags for the shared Mapster32 declaration body.
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
#include "../../build/include/m32def_shared.h"

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

#endif
