//-------------------------------------------------------------------------
/*
Copyright (C) 2026 NNBuild contributors

This file is part of PCExhumed.

PCExhumed is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

PCExhumed is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

#ifndef __kextactile_h__
#define __kextactile_h__

// The KEX remaster ships authored 200 Hz .bnvib controller curves.  These
// controls affect only its optional -kex bridge, never the classic campaign.
extern int kex_vibration;
extern int kex_vibration_volume;

void KexTactile_Reset(void);
void KexTactile_StopAll(void);
void KexTactile_StopSprite(short nSprite);
void KexTactile_OnSound(char const *name, short nSprite);
void KexTactile_Quake(void);
void KexTactile_Tick(void);

#endif
