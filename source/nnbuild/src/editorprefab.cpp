#include "editorprefab.h"

#include "editortools.h"

namespace DukeEditor {

bool CapturePrefab(Board const &board, MapSettings const &settings, std::vector<int> const &indexes,
                   GameTarget target, Prefab &prefab, std::string &error)
{
    if (indexes.empty())
    {
        error = "Select a sprite or a group of sprites to create a prefab.";
        return false;
    }
    std::vector<int> localIndex(board.sprites.size(), -1);
    for (size_t i = 0; i < indexes.size(); ++i)
    {
        int const index = indexes[i];
        if (unsigned(index) >= board.sprites.size() || localIndex[index] >= 0)
        {
            error = "Select each prefab sprite once.";
            return false;
        }
        localIndex[index] = i;
    }

    Prefab result;
    result.target = target;
    auto const origin = board.sprites[indexes.front()].xy;
    for (int index : indexes)
    {
        auto const &source = board.sprites[index];
        int const support = IsPlatform(source) ? -1 : PlatformAt(board, source.sectnum, source.xy, true, source.z);
        int const floor = support >= 0 && localIndex[support] < 0 ? board.sprites[support].z :
                          board.floorAt(source.sectnum, source.xy);
        int64_t const x = int64_t(source.x) - origin.x, y = int64_t(source.y) - origin.y;
        int64_t const z = int64_t(source.z) - floor;
        if (x < -2 * BXY_MAX || x > 2 * BXY_MAX || y < -2 * BXY_MAX || y > 2 * BXY_MAX ||
            z < INT32_MIN || z > INT32_MAX)
        {
            error = "The selection contains a sprite outside the prefab coordinate range.";
            return false;
        }

        PrefabSprite entry;
        entry.sprite = source;
        entry.sprite.xyz = {int(x), int(y), int(z)};
        entry.sprite.sectnum = -1;
        entry.sprite.owner = unsigned(source.owner) < localIndex.size() ? localIndex[source.owner] : -1;
        for (auto const &attachment : settings.scripts)
            if (attachment.kind == "sprite" && attachment.index == index)
                entry.components.push_back(attachment.script);
        result.sprites.push_back(std::move(entry));
    }
    prefab = std::move(result);
    return true;
}

bool InstantiatePrefab(Board &board, Prefab const &prefab, vec2_t position, int preferredSector,
                       bool useFloorLayer, int floorLayer, std::vector<ScriptAttachment> &components,
                       std::string &error)
{
    if (prefab.sprites.empty() || board.sprites.size() + prefab.sprites.size() > MAXSPRITESV7)
    {
        error = "This placement needs more sprite slots in the map.";
        return false;
    }

    std::vector<uspritetypev7> placed;
    std::vector<ScriptAttachment> attachments;
    int const first = board.sprites.size();
    for (auto const &entry : prefab.sprites)
    {
        auto sprite = entry.sprite;
        int64_t const x = int64_t(position.x) + sprite.x, y = int64_t(position.y) + sprite.y;
        if (x < -BXY_MAX || x > BXY_MAX || y < -BXY_MAX || y > BXY_MAX)
        {
            error = "Keep the prefab within the map grid.";
            return false;
        }
        sprite.xy = {int(x), int(y)};
        int const section = LayerAt(board, sprite.xy, preferredSector, useFloorLayer, floorLayer);
        if (section < 0)
        {
            error = "Place the whole prefab inside the selected floor level.";
            return false;
        }
        int const platform = useFloorLayer ? PlatformAt(board, section, sprite.xy, true, floorLayer) : -1;
        int const floor = platform < 0 ? board.floorAt(section, sprite.xy) : board.sprites[platform].z;
        int64_t const z = int64_t(floor) + sprite.z;
        if (z < INT32_MIN || z > INT32_MAX)
        {
            error = "The prefab's height is outside the map coordinate range.";
            return false;
        }
        sprite.z = z;
        sprite.sectnum = section;
        if (sprite.owner >= 0)
            sprite.owner += first;
        for (auto const &script : entry.components)
            attachments.push_back({"sprite", script, first + int(placed.size())});
        placed.push_back(sprite);
    }
    board.sprites.insert(board.sprites.end(), placed.begin(), placed.end());
    components = std::move(attachments);
    return true;
}

} // namespace DukeEditor
