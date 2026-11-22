#ifndef VIBEBUILD32_EDITORBOARD_H
#define VIBEBUILD32_EDITORBOARD_H

#include "build.h"

#include <string>
#include <vector>

namespace DukeEditor {

constexpr size_t maxBoardBytes = 26 + MAXSECTORSV7 * sizeof(usectortypev7)
    + MAXWALLSV7 * sizeof(uwalltypev7) + MAXSPRITESV7 * sizeof(uspritetypev7);

struct Board
{
    std::vector<usectortypev7> sectors;
    std::vector<uwalltypev7> walls;
    std::vector<uspritetypev7> sprites;
    vec3_t start{};
    int16_t angle = 0, startSector = -1;

    int wallSector(int wallIndex) const;
    bool containsPoint(int sector, vec2_t point) const;
    int sectorAt(vec2_t point, int preferred = -1) const;
    int floorAt(int sector, vec2_t point) const;
    int ceilingAt(int sector, vec2_t point) const;
    std::vector<int> linkedVertices(std::vector<int> vertices) const;
    bool addRoom(std::vector<vec2_t> points, int fromWall = -1);
    bool addPillar(int sector, std::vector<vec2_t> points, int tile);
    bool addStairs(int wall, int steps, int tread, int rise, std::string &error);
    bool extendRoom(int wallIndex, int depth);
    bool splitWall(int wallIndex, int *newVertex = nullptr);
    void deleteSector(int sectorIndex);
    void deleteSprite(int spriteIndex);
    bool addSprite(int tile, vec2_t point, int preferredSector);
    bool moveSprite(int spriteIndex, vec2_t point);
    bool setStart(vec2_t point, int preferredSector);
    bool validate(int tileLimit, std::string &error, bool draft = false) const;
    bool validateGeometryChange(const Board &before, std::string &error) const;
    void install() const;
};

Board NewBoard();
bool SameTopology(const Board &a, const Board &b);
bool MergeBoard(const Board &base, const Board &edited, const Board &current, Board &merged);
std::vector<uint8_t> EncodeBoard(const Board &board);
bool DecodeBoard(const std::vector<uint8_t> &bytes, Board &board, int tileLimit, std::string &error, bool draft);
bool ReadBoard(const std::string &path, Board &board, int tileLimit, std::string &error);
bool WriteBoard(const std::string &path, const Board &board, int tileLimit, std::string &error);
std::string CreatePlaytestPath();

} // namespace DukeEditor

#endif
