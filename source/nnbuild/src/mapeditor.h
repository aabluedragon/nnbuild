#ifndef NNBUILD_MAPEDITOR_H
#define NNBUILD_MAPEDITOR_H

bool MapEditor_CanOpen();
void MapEditor_Request(const char *path = nullptr);
bool MapEditor_Requested();
bool MapEditor_Update(bool windowClosing);
bool MapEditor_Playing();
const char *MapEditor_PlaytestName();
const char *MapEditor_PlaytestSource();
void MapEditor_StopPlaytest();
void MapEditor_DrawPlaytestHint();
void MapEditor_Shutdown();

#endif
