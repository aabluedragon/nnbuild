#ifndef GAMEEXEC_H
#define GAMEEXEC_H

#include "build.h"
#include "gamedef.h"  // vmstate_t
#include "sector.h"  // mapstate_t

static FORCE_INLINE int VM_HaveEvent(int const nEventID)
{
    return !!apScriptGameEvent[nEventID];
}

void X_OnEvent(int iEventID, int sActor, int sPlayer, int lDist);
int X_OnEventWithReturn(int iEventID, int sActor, int sPlayer, int nReturn);

#define CON_ERRPRINTF(Text, ...) do { \
        OSD_Printf("Line %d, %s: " Text, g_errorLineNum, VM_GetKeywordForID(g_tw), ## __VA_ARGS__); \
    } while (0)

#endif // GAMEEXEC_H