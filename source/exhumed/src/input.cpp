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

#include "input.h"
#include "engine.h"
#include "exhumed.h"
#include "player.h"
#include "serial.h"
#include "network.h"
#include "keyboard.h"
#include "control.h"
#include "config.h"
#include "status.h"
#include <string.h>

int nNetMoves = 0;

short nInputStack = 0;

short bStackNode[kMaxPlayers];

short nTypeStack[kMaxPlayers];
PlayerInput sPlayerInput[kMaxPlayers];

int *pStackPtr;

// (nInputStack * 32) - 11;

void PushInput(PlayerInput *pInput, int edx)
{
    if (!bStackNode[edx])
    {
//		memcpy(sInputStack[nInputStack], pInput,
    }
}

int PopInput()
{
    if (!nInputStack)
        return -1;

    nInputStack--;

    // TEMP
    return 0;
}

void InitInput()
{
    memset(nTypeStack, 0, sizeof(nTypeStack));
    nInputStack = 0;
    memset(bStackNode, 0, sizeof(bStackNode));

//	pStackPtr = &sInputStack;
}

void ClearSpaceBar(short nPlayer)
{
    sPlayerInput[nPlayer].buttons &= 0x0FB;
    CONTROL_ClearButton(gamefunc_Open);
}

void GetLocalInput()
{
    // The original game has seven numbered weapon buttons.  PowerSlave
    // Exhumed adds the Manacle as slot 8 and puts Amun Bomb before the flame
    // thrower.  Keep the classic packet path byte-for-byte as it was (for
    // its demos and controls), but encode the remaster's authored ordering
    // when this is a -kex session.
    int weapon = 0;
    if (g_kexMode)
    {
        static int const s_kexWeaponForSlot[] = {
            0, 1, 2, 4, 3, 5, 6, 7
        };
        for (int slot = (int)ARRAY_SIZE(s_kexWeaponForSlot) - 1; slot >= 0; slot--)
        {
            if (BUTTON(gamefunc_Weapon_1 + slot))
            {
                weapon = s_kexWeaponForSlot[slot] + 1;
                break;
            }
        }
    }
    else
    {
        for (int i = 6; i >= 0; i--)
        {
            if (BUTTON(gamefunc_Weapon_1 + i))
            {
                weapon = i + 1;
                break;
            }
        }
    }

    if (PlayerList[nLocalPlayer].nHealth)
    {
        lLocalButtons = (BUTTON(gamefunc_Crouch) << 4) | (BUTTON(gamefunc_Fire) << 3)
                      | (BUTTON(gamefunc_Jump)<<0) | (weapon<<13);
    }
    else
    {
        lLocalButtons = 0;
    }

    lLocalButtons |= BUTTON(gamefunc_Open) << 2;

// TODO	ExecRecord(&sPlayerInput[nLocalPlayer], sizeof(PlayerInput));
}

void BackupInput()
{

}

void SendInput()
{

}

void LogoffPlayer(int nPlayer)
{
    if (nPlayer == nLocalPlayer)
        return;

    if (PlayerList[nPlayer].someNetVal == -1)
        return;

    memset(&sPlayerInput[nPlayer], 0, sizeof(sPlayerInput));

    sprite[nDoppleSprite[nPlayer]].cstat = 0x8000u;
    sprite[nPlayerFloorSprite[nPlayer]].cstat = 0x8000u;
    sprite[PlayerList[nPlayer].nSprite].cstat = 0x8000u;

    PlayerList[nPlayer].someNetVal = -1;

    StatusMessage(150, "Player %d has left the game", nPlayer);

// TODO	ClearPlayerInput(&sPlayerInput[nPlayer]);
    nNetPlayerCount--;
}

void UpdateInputs()
{
    nNetMoveFrames = moveframes;

    if (nNetPlayerCount)
    {
        if (bSerialPlay) {
            UpdateSerialInputs();
        }
        else {
            UpdateNetInputs();
        }

        nNetMoves++;

        if (!nNetMoves) {
            nNetMoves++;
        }
    }
}

/*
ClearSpaceBar_
    GetLocalInput_
    GetModemInput_
    BackupInput_
    SendInput_
    SendToUnAckd_
    LogoffPlayer_
    UpdateInputs_
    faketimerhandler_
*/

void ClearAllKeys()
{
    KB_ClearKeysDown();
    KB_FlushKeyboardQueue();
}

void WaitNoKey(int nSecs, void (*pFunc) (void))
{
    int nTotalTime = (kTimerTicks * nSecs) + (int)totalclock;

    while (nTotalTime > (int)totalclock)
    {
        HandleAsync();
        if (pFunc) {
            pFunc();
        }
    }
}

int WaitAnyKey(int nSecs)
{
    int nTotalTime = (int)totalclock + (kTimerTicks * nSecs);

    while (1)
    {
        HandleAsync();
        if (nTotalTime <= (int)totalclock || nSecs == -1) {
            return -1;
        }

        int i = 0;

        do
        {
            if (KB_KeyDown[i])
            {
                KB_KeyDown[i] = 0;
                return i;
            }

            i++;

        } while (i < 106);
    }
}


/*
    Name:  _nLocalPlayer
  Name:  _nNetPlayerCount
  Name:  _nModemPlayer
  Name:  _nNetMoves
  Name:  _lLocalButtons
  Name:  _lLocalCodes
  Name:  _nInputStack
  Name:  _bSyncNet
  Name:  _lStartupTime
  Name:  _bStackNode
  Name:  _sSync
  Name:  _nTypeStack
  Name:  _sPlayerInput
  Name:  _pStackPtr
  Name:  _sInputStack

  */
