#ifndef VIBEBUILD32_EDITORTOOLS_H
#define VIBEBUILD32_EDITORTOOLS_H

#include "editorlive.h"
#include "editorproject.h"

namespace DukeEditor {

int ObjectSector(Board const &board, ObjectRef object);
std::string ObjectLabel(ObjectRef object);
vec2_t ObjectPosition(Board const &board, ObjectRef object);
int LayerAt(Board const &board, vec2_t point, int preferred, bool filtered, int z);

struct PlayPoint
{
    vec2_t xy{};
    int sector = -1, angle = -1;
    int platform = -1;
    int spawn = -1;
};
bool MakePlayStart(Board &copy, PlayPoint point, GameTarget target, std::string &error);
std::string SpawnLabel(Board const &board, int sprite);
std::array<vec2_t, 4> PlatformCorners(uspritetypev7 const &sprite);
bool IsPlatform(uspritetypev7 const &sprite);
int PlatformAt(Board const &board, int section, vec2_t point, bool filtered, int z);
int SpriteFloor(Board const &board, int sprite);
bool AddCatwalk(Board &board, int section, vec2_t center, int tile, int length, int width,
                int height, int angle, std::string &error);

enum class Mechanism { Lift, Door, Trap, Light };
struct MechanismSetup
{
    Mechanism type = Mechanism::Lift;
    int section = -1, firstStop = -1, secondStop = -1;
    int speed = 256, drop = 384, lightShade = -12, lightPalette = 0;
    std::vector<int> buttons;
};
bool DukeMechanisms(GameTarget target);
const char *MechanismName(Mechanism type);
std::vector<int> Neighbors(Board const &board, int section);
bool AddMechanism(Board &board, MechanismSetup const &setup, GameTarget target, int &tag, std::string &error);

struct MapLink
{
    ObjectRef source, target;
    int tag;
};
std::vector<MapLink> MapLinks(Board const &board);
bool TranslateObjects(Board &board, std::vector<ObjectRef> const &objects, vec3_t offset, std::string &error);
bool AlignWalls(Board &board, std::vector<ObjectRef> const &objects, std::string &error);
bool SetObjectHeight(Board &board, std::vector<ObjectRef> const &objects, int height, std::string &error);

struct MapIssue
{
    ObjectRef object;
    std::string message;
};
std::vector<MapIssue> CheckMap(Board const &board, GameTarget target, bool modern, MapSettings const &settings,
                              std::string const &assetRoot, bool loadedTarget);

} // namespace DukeEditor

#endif
