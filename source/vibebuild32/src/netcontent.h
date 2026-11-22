#ifndef VIBEBUILD32_NETCONTENT_H
#define VIBEBUILD32_NETCONTENT_H

#include <string>
#include <vector>

void G_ReplaceCommandGroups(std::vector<std::string> const &paths);
bool G_HaveCommandGroups(void);

bool Net_BootstrapContent(void);
bool Net_SendStartupContent(int slot);
void Net_KeepWaitingPlayersAlive(int activeSlot);
bool Net_HandleContentPacket(int slot, const char *packet, int length);
bool Net_ContentOnly(void);
bool Net_SendGameFiles(int slot);
bool Net_SendMap(const char *map, int slot);
bool Net_SendLooseFile(const char *path);
bool Net_StageUserMap(const char *path, std::string &staged);
bool Net_IsCachedMap(const char *map);
bool Net_ResolvePack(const char *name, std::string &path);
bool Net_ResolveContentPath(const char *name, std::string &path);
bool Net_ValidPackName(const char *name);

#endif
