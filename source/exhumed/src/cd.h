//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2019 EDuke32 developers and contributors
Copyright (C) 2019 sirlemonhead, Nuke.YKT

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

#ifndef __cd_h__
#define __cd_h__

void setCDaudiovolume(int val);
bool playCDtrack(int nTrack, bool bLoop);
// Ramses dialogue shares the streamed-track mixer but is a sound effect, not
// soundtrack music.  Its call opts out of the music preference explicitly.
bool playMusicFile(const char *name, bool bLoop, bool honorMusicToggle = true);
void StartfadeCDaudio();
int StepFadeCDaudio();
bool CDplaying();
void StopCD();
void StopCDMusic();
int fadecdaudio();

#endif
