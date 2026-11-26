#ifndef NNBUILD_EDITORPREFAB_H
#define NNBUILD_EDITORPREFAB_H

#include "editorboard.h"
#include "editorproject.h"

namespace DukeEditor {

struct PrefabSprite
{
    // XY relative to the first sprite, Z relative to the floor and owner local to the prefab.
    uspritetypev7 sprite{};
    std::vector<std::string> components;
};

struct Prefab
{
    GameTarget target = GameTarget::Inherit;
    std::vector<PrefabSprite> sprites;
};

constexpr char prefabSuffix[] = ".vbprefab";
bool IsPrefabAsset(std::string const &path);
bool ProjectFileExists(std::string const &root, std::string const &path);
bool ReadPrefab(std::string const &root, std::string const &path, Prefab &prefab, std::string &error);
bool WritePrefab(std::string const &root, std::string const &path, Prefab const &prefab, bool overwrite, std::string &error);
bool CreateComponentAsset(std::string const &root, std::string const &path, std::string &error);

bool CapturePrefab(Board const &board, MapSettings const &settings, std::vector<int> const &indexes,
                   GameTarget target, Prefab &prefab, std::string &error);
bool InstantiatePrefab(Board &board, Prefab const &prefab, vec2_t position, int preferredSector,
                       bool useFloorLayer, int floorLayer, std::vector<ScriptAttachment> &components,
                       std::string &error);

} // namespace DukeEditor

#endif
