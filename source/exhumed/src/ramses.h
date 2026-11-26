//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2020 EDuke32 developers and contributors
Copyright (C) 2020 sirlemonhead, Nuke.YKT

This file is part of PCExhumed.

PCExhumed is free software; you can redistribute it and/or
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

#ifndef __ramses_h__
#define __ramses_h__

extern short nSpiritSprite;
extern short nHeadStage;

// 0 English, 1 French, 2 German, 3 Spanish, 4 Italian. Italian has remaster
// text but no matching Ramses speech/cinematics, which use English instead.
extern int kex_language;
int KexLanguage_Index();
int KexLanguage_VoiceIndex();
char const *KexLanguage_Code();

void InitSpiritHead();
int DoSpiritHead();
void KexRamses_Reset();
int KexRamses_Tick();
int KexRamses_ViewTile(int nSprite);

#endif
