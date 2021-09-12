//-------------------------------------------------------------------------
/*
Copyright (C) 2016 EDuke32 developers and contributors

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

#ifndef SCREENS_H
#define SCREENS_H

#define CROSSHAIR_PAL (MAXPALOOKUPS-RESERVEDPALS-1)

static inline int G_LastMapInfoIndex(void)
{
    Bassert(ud.last_level >= 1);  // NOTE: last_level is 1-based
    return ud.volume_number * MAXLEVELS + ud.last_level - 1;
}

void P_SetGamePalette(DukePlayer_t* player, uint32_t palid, int32_t set);

void G_ShowScores(bool bonus);

void G_FadePalette(int32_t r, int32_t g, int32_t b, int32_t e);
void fadepal(int r, int g, int b, int start, int end, int step);

void G_DoOrderScreen(void);

void G_BonusScreen(int bonusonly);

void G_GetCrosshairColor(void);
void G_SetCrosshairColor(int32_t r, int32_t g, int32_t b);

extern palette_t CrosshairColors;
extern palette_t DefaultCrosshairColors;
extern int32_t g_crosshairSum;

#endif