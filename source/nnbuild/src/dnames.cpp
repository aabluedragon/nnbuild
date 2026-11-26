//-------------------------------------------------------------------------
/*
Copyright (C) EDuke32 developers and contributors

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

#include "compat.h"
#include "build.h"

#include "dnames.h"
#include "global.h"

#define DNAMES_EXTRA_NNBUILD_SOUNDS
#define DNAMES_EXPOSE_DYN_TILE_LIST_SIZE
#include "../../build/include/dnames_shared.cpp.inc"
