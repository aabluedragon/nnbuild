#ifndef GAMEEXEC_H
#define GAMEEXEC_H

#include "build.h"
#include "gamedef.h"  // vmstate_t
#include "sector.h"  // mapstate_t

void X_ScriptInfo(void);

void X_OnEvent(int iEventID, int sActor, int sPlayer, int lDist);
int X_OnEventWithReturn(int iEventID, int sActor, int sPlayer, int nReturn);

int A_GetFurthestAngle(int const spriteNum, int const angDiv);

void A_GetZLimits(int iActor);
void A_Fall(int iActor);

static FORCE_INLINE int VM_HaveEvent(int const nEventID)
{
    return !!apScriptGameEvent[nEventID];
}

#define CON_ERRPRINTF(Text, ...) do { \
        OSD_Printf("Line %d, %s: " Text, g_errorLineNum, VM_GetKeywordForID(g_tw), ## __VA_ARGS__); \
    } while (0)

#endif // GAMEEXEC_H