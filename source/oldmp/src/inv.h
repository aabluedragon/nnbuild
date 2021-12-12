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

#ifndef INV_H
#define INV_H

enum InventoryItem_t {
    GET_STEROIDS,
    GET_SHIELD,
    GET_SCUBA,
    GET_HOLODUKE,
    GET_JETPACK,
    GET_ACCESS   = 6,
    GET_HEATS,
    GET_FIRSTAID = 9,
    GET_BOOTS
};

enum DukeWeapon_t {
    KNEE_WEAPON,
    PISTOL_WEAPON,
    SHOTGUN_WEAPON,
    CHAINGUN_WEAPON,
    RPG_WEAPON,
    HANDBOMB_WEAPON,
    SHRINKER_WEAPON,
    DEVISTATOR_WEAPON,
    TRIPBOMB_WEAPON,
    FREEZE_WEAPON,
    HANDREMOTE_WEAPON,
    GROW_WEAPON,
    MAX_WEAPONS
};

#endif