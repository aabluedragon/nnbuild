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

#include "baselayer.h"
#include "cache1d.h"
#include "common_game.h"
#include "compat.h"
#include "crc32.h"
#include "duke3d.h"
#include "grpscan.h"
#include "scriptfile.h"

#include "vfs.h"

#define DUKE_GRP_INCLUDE_FULL_DUKE_GAMES 0
#define DUKE_GRP_ENABLE_DN64_SCAN 1
#define DUKE_GRP_EXTRA_INTERNAL_ENTRIES \
    { "Redneck Rampage",                       RR_CRC,     141174222, GAMEFLAG_RR,                           0, NULL, NULL }, \
    { "Redneck Rampage: Rides Again",          RRRA_CRC,   191798609, GAMEFLAG_RR|GAMEFLAG_RRRA,             0, NULL, NULL },
#define DUKE_GRP_EXTRA_SCRIPT_SYMBOLS \
    scriptfile_addsymbolvalue("GAMEFLAG_RR", GAMEFLAG_RR); \
    scriptfile_addsymbolvalue("GAMEFLAG_RRRA", GAMEFLAG_RR|GAMEFLAG_RRRA);

#include "../../build/include/duke_grpscan_shared.cpp.inc"
