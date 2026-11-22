#ifndef VIBEBUILD32_SERVERLEVEL_H
#define VIBEBUILD32_SERVERLEVEL_H

#include "osd.h"

bool Net_QueueServerLevel(const char *pack, const char *level);
bool Net_ServerLevelChangePending(void);
void Net_RunServerLevelChange(void);
int Net_MapCycleCommand(const osdfuncparm_t *parm);

void Net_SetServerLaunchFile(const char *path, bool probe);
bool Net_RestoreServerLaunch(void);
bool Net_IsContentLaunch(void);
bool Net_IsContentProbe(void);
bool Net_ApplyServerLaunchLevel(void);
bool Net_CheckLaunchMap(void);
void Net_RestoreServerSettings(void);

#endif
