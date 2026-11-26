#include "duke3d.h"
#include "editortools.h"

#include <cmath>
#include <map>
#include <set>

namespace DukeEditor {
namespace {

constexpr int eyeHeight = 40 << 8;
constexpr int playerHeight = 48 << 8;
constexpr int walkRadius = 128;
constexpr int bridgeFlags = CSTAT_SPRITE_ALIGNMENT_FLOOR | CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;

bool fail(std::string &error, std::string message)
{
    error = std::move(message);
    return false;
}

bool validSector(Board const &board, int index) { return unsigned(index) < board.sectors.size(); }
bool validWall(Board const &board, int index) { return unsigned(index) < board.walls.size(); }
bool validSprite(Board const &board, int index) { return unsigned(index) < board.sprites.size(); }

bool hasArt(int tile)
{
    return unsigned(tile) < MAXTILES && tilesiz[tile].x > 0 && tilesiz[tile].y > 0;
}

double wallDistance(vec2_t point, vec2_t a, vec2_t b)
{
    double const x = double(b.x) - a.x, y = double(b.y) - a.y;
    double const length = x*x + y*y;
    double const t = length ? clamp(((point.x - double(a.x))*x + (point.y - double(a.y))*y) / length, 0., 1.) : 0.;
    return std::hypot(point.x - a.x - t*x, point.y - a.y - t*y);
}

vec2_t wallMiddle(Board const &board, int index)
{
    auto const a = board.walls[index].xy, b = board.walls[board.walls[index].point2].xy;
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}

bool findMarker(Board const &board, int section, vec2_t &marker)
{
    auto const &room = board.sectors[section];
    auto fits = [&](vec2_t p) {
        if (!board.containsPoint(section, p))
            return false;
        for (int w = room.wallptr; w < room.wallptr + room.wallnum; ++w)
            if (wallDistance(p, board.walls[w].xy, board.walls[board.walls[w].point2].xy) < 8.)
                return false;
        marker = p;
        return true;
    };
    if (fits(ObjectPosition(board, {ObjectKind::Sector, section})))
        return true;
    for (int w = room.wallptr; w < room.wallptr + room.wallnum; ++w)
    {
        auto const a = board.walls[w].xy, b = board.walls[board.walls[w].point2].xy;
        auto const mid = wallMiddle(board, w);
        double const length = std::hypot(double(b.x - a.x), double(b.y - a.y));
        if (!length)
            continue;
        for (int inset : {64, 24, 8})
            if (fits({mid.x - int(std::lround(inset * (b.y - a.y) / length)),
                      mid.y + int(std::lround(inset * (b.x - a.x) / length))}))
                return true;
    }
    return false;
}

bool swatch(int tile)
{
    for (int base : {ACCESSSWITCH__, ACCESSSWITCH2__, LIGHTSWITCH__, LIGHTSWITCH2__, SPACEDOORSWITCH__,
         SPACELIGHTSWITCH__, FRANKENSTINESWITCH__, DIPSWITCH__, DIPSWITCH2__, DIPSWITCH3__, TECHSWITCH__,
         POWERSWITCH1__, POWERSWITCH2__, LOCKSWITCH1__, HANDSWITCH__, PULLSWITCH__, ALIENSWITCH__, SLOTDOOR__})
        if (tile == base || tile == base + 1)
            return true;
    return tile >= MULTISWITCH__ && tile <= MULTISWITCH__ + 3;
}

using Receivers = std::map<int, std::vector<ObjectRef>>;

Receivers linkReceivers(Board const &board)
{
    Receivers receivers;
    for (int i = 0; i < int(board.sprites.size()); ++i)
    {
        auto const &s = board.sprites[i];
        if (s.picnum == ACTIVATOR__ || s.picnum == ACTIVATORLOCKED__ || s.picnum == MASTERSWITCH__ ||
            s.picnum == RESPAWN__)
            receivers[s.lotag].push_back({ObjectKind::Sprite, i});
        else if (s.picnum == SECTOREFFECTOR__ && (s.lotag == 12 || s.lotag == 21 || s.lotag == 24 ||
                                                 s.lotag == 25 || s.lotag == 34))
            receivers[s.hitag].push_back({ObjectKind::Sprite, i});
    }
    for (int i = 0; i < int(board.walls.size()); ++i)
        if (board.walls[i].lotag > 0 && !swatch(board.walls[i].picnum) &&
            ((board.walls[i].overpicnum >= W_FORCEFIELD__ && board.walls[i].overpicnum <= W_FORCEFIELD__ + 2) ||
             board.walls[i].overpicnum == BIGFORCE__))
            receivers[board.walls[i].lotag].push_back({ObjectKind::Wall, i});
    return receivers;
}

int freeTag(Board const &board)
{
    std::set<int> used;
    for (auto const &s : board.sectors) { used.insert(s.hitag); used.insert(s.lotag); }
    for (auto const &w : board.walls) { used.insert(w.hitag); used.insert(w.lotag); }
    for (auto const &s : board.sprites) { used.insert(s.hitag); used.insert(s.lotag); }
    // Leave space for the conventional low-numbered sector effects.
    for (int tag = 100; tag < INT16_MAX; ++tag)
        if (!used.count(tag))
            return tag;
    return 0;
}

bool insideSprite(uspritetypev7 const &sprite, vec2_t point)
{
    auto const corners = PlatformCorners(sprite);
    bool positive = false, negative = false;
    for (size_t i = 0; i < corners.size(); ++i)
    {
        auto const a = corners[i], b = corners[(i + 1) % corners.size()];
        int64_t const cross = int64_t(b.x - a.x) * (int64_t(point.y) - a.y) -
                              int64_t(b.y - a.y) * (int64_t(point.x) - a.x);
        positive |= cross > 0;
        negative |= cross < 0;
    }
    return (positive || negative) && !(positive && negative);
}

bool crossesPlatformEdge(vec2_t a, vec2_t b, std::array<vec2_t, 4> const &corners)
{
    auto side = [](vec2_t start, vec2_t end, vec2_t p) {
        return (int64_t(end.x) - start.x) * (int64_t(p.y) - start.y) -
               (int64_t(end.y) - start.y) * (int64_t(p.x) - start.x);
    };
    auto opposite = [](int64_t x, int64_t y) { return x && y && ((x < 0) != (y < 0)); };
    for (int i = 0; i < 4; ++i)
    {
        auto const c = corners[i], d = corners[(i + 1) % 4];
        if (opposite(side(a, b, c), side(a, b, d)) && opposite(side(c, d, a), side(c, d, b)))
            return true;
    }
    return false;
}

struct VerticalSpace { int ceiling, floor; };

VerticalSpace initialSpace(Board const &board, int section, vec2_t point, GameTarget target)
{
    VerticalSpace space{board.ceilingAt(section, point), board.floorAt(section, point)};
    if (DukeMechanisms(target))
        for (auto const &entity : board.sprites)
            if (entity.sectnum == section && entity.picnum == SECTOREFFECTOR__ && entity.ang != 1536)
            {
                if (entity.lotag == 31) space.floor = entity.z;
                if (entity.lotag == 32) space.ceiling = entity.z;
            }
    return space;
}

bool inPlayerBounds(Board const &board, int section, vec2_t point, int z, int platform, VerticalSpace space,
                    std::string &error)
{
    auto const ceiling = space.ceiling;
    auto const floor = platform >= 0 ? board.sprites[platform].z : space.floor;
    if (int64_t(floor) - ceiling < playerHeight || int64_t(z) - ceiling < (4 << 8) || z > floor)
        return fail(error, "A player needs 48 units of headroom and a start between the floor and ceiling.");
    auto const &area = board.sectors[section];
    for (int i = area.wallptr; i < area.wallptr + area.wallnum; ++i)
    {
        auto const &edge = board.walls[i];
        if (edge.nextsector >= 0 && !(edge.cstat & 1) &&
            int64_t(board.floorAt(edge.nextsector, point)) - floor >= -(16 << 8) &&
            int64_t(z) - board.ceilingAt(edge.nextsector, point) >= (4 << 8))
            continue;
        if (wallDistance(point, edge.xy, board.walls[edge.point2].xy) < walkRadius)
            return fail(error, "The player start is too close to a blocking wall. Move it inward.");
    }
    for (int i = 0; i < int(board.sprites.size()); ++i)
    {
        auto const &s = board.sprites[i];
        if (s.sectnum != section || !(s.cstat & CSTAT_SPRITE_BLOCK) || s.picnum == APLAYER__ || i == platform)
            continue;
        if ((s.cstat & 48) == CSTAT_SPRITE_ALIGNMENT_FLOOR)
        {
            if (s.z > z - (4 << 8) && s.z < floor && hasArt(s.picnum) && insideSprite(s, point))
                return fail(error, "A blocking floor sprite leaves too little room for the player.");
        }
        else if (std::hypot(double(s.x) - point.x, double(s.y) - point.y) < walkRadius + (s.clipdist << 2))
        {
            int const height = hasArt(s.picnum) ? tilesiz[s.picnum].y * s.yrepeat * 4 : playerHeight;
            auto const bottom = int64_t(s.z) + (s.cstat & CSTAT_SPRITE_YCENTER ? height / 2 : 0);
            if (bottom > z - (4 << 8) && bottom - height < floor)
                return fail(error, "A blocking sprite overlaps the player start.");
        }
    }
    return true;
}

} // namespace

int ObjectSector(Board const &board, ObjectRef object)
{
    switch (object.kind)
    {
        case ObjectKind::Sector: return validSector(board, object.index) ? object.index : -1;
        case ObjectKind::Wall:
        case ObjectKind::Vertex: return board.wallSector(object.index);
        case ObjectKind::Sprite: return validSprite(board, object.index) ? board.sprites[object.index].sectnum : -1;
        case ObjectKind::Start: return board.startSector;
        default: return -1;
    }
}

std::string ObjectLabel(ObjectRef object)
{
    switch (object.kind)
    {
        case ObjectKind::Sector: return "Sector " + std::to_string(object.index);
        case ObjectKind::Wall: return "Wall " + std::to_string(object.index);
        case ObjectKind::Vertex: return "Vertex " + std::to_string(object.index);
        case ObjectKind::Sprite: return "Sprite " + std::to_string(object.index);
        case ObjectKind::Start: return "Player Start";
        default: return "Map";
    }
}

vec2_t ObjectPosition(Board const &board, ObjectRef object)
{
    switch (object.kind)
    {
        case ObjectKind::Wall: if (validWall(board, object.index)) return wallMiddle(board, object.index); break;
        case ObjectKind::Vertex: if (validWall(board, object.index)) return board.walls[object.index].xy; break;
        case ObjectKind::Sprite: if (validSprite(board, object.index)) return board.sprites[object.index].xy; break;
        case ObjectKind::Start: return {board.start.x, board.start.y};
        case ObjectKind::Sector:
            if (validSector(board, object.index))
            {
                auto const &s = board.sectors[object.index];
                vec2_t low{BXY_MAX, BXY_MAX}, high{-BXY_MAX, -BXY_MAX};
                for (int w = s.wallptr; w < s.wallptr + s.wallnum; ++w)
                {
                    auto const p = board.walls[w].xy;
                    low = {std::min(low.x, p.x), std::min(low.y, p.y)};
                    high = {std::max(high.x, p.x), std::max(high.y, p.y)};
                }
                return {(low.x + high.x) / 2, (low.y + high.y) / 2};
            }
            break;
        default: break;
    }
    return {};
}

int LayerAt(Board const &board, vec2_t point, int preferred, bool filtered, int z)
{
    if (!filtered)
        return board.sectorAt(point, preferred);
    int best = -1;
    int64_t distance = INT64_MAX;
    for (int s = 0; s < int(board.sectors.size()); ++s)
        if (board.containsPoint(s, point) && board.ceilingAt(s, point) <= z && board.floorAt(s, point) >= z)
        {
            int64_t const below = int64_t(board.floorAt(s, point)) - z;
            if (below < distance || (below == distance && s == preferred))
            {
                distance = below;
                best = s;
            }
        }
    return best;
}

std::array<vec2_t, 4> PlatformCorners(uspritetypev7 const &sprite)
{
    auto const tile = sprite.picnum;
    if (!hasArt(tile))
        return {};
    auto const size = tilesiz[tile];
    int const xoffset = (picanm[tile].xofs + sprite.xoffset) * (sprite.cstat & CSTAT_SPRITE_XFLIP ? -1 : 1);
    int const yoffset = (picanm[tile].yofs + sprite.yoffset) * (sprite.cstat & CSTAT_SPRITE_YFLIP ? -1 : 1);
    double const sinAngle = sintable[sprite.ang & 2047] / 16384.;
    double const cosAngle = sintable[(sprite.ang + 512) & 2047] / 16384.;
    int const x = (size.x / 2 + xoffset) * sprite.xrepeat;
    int const y = (size.y / 2 + yoffset) * sprite.yrepeat;
    auto corner = [&](int width, int length) {
        return vec2_t{int(std::floor(sprite.x + (sinAngle * width + cosAngle * length) / 4)),
                      int(std::floor(sprite.y + (-cosAngle * width + sinAngle * length) / 4))};
    };
    return {corner(x, y), corner(x - size.x * sprite.xrepeat, y),
            corner(x - size.x * sprite.xrepeat, y - size.y * sprite.yrepeat),
            corner(x, y - size.y * sprite.yrepeat)};
}

int PlatformAt(Board const &board, int section, vec2_t point, bool filtered, int z)
{
    int best = -1;
    for (int i = 0; i < int(board.sprites.size()); ++i)
    {
        auto const &s = board.sprites[i];
        if (s.sectnum == section && IsPlatform(s) && (!filtered || s.z >= z) &&
            (best < 0 || s.z < board.sprites[best].z) && insideSprite(s, point))
            best = i;
    }
    return best;
}

bool IsPlatform(uspritetypev7 const &sprite)
{
    constexpr int flags = CSTAT_SPRITE_ALIGNMENT_MASK | CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_INVISIBLE;
    return (sprite.cstat & flags) == (CSTAT_SPRITE_ALIGNMENT_FLOOR | CSTAT_SPRITE_BLOCK) && hasArt(sprite.picnum) &&
           sprite.x >= -BXY_MAX && sprite.x <= BXY_MAX && sprite.y >= -BXY_MAX && sprite.y <= BXY_MAX;
}

int SpriteFloor(Board const &board, int index)
{
    auto const &s = board.sprites[index];
    int const platform = IsPlatform(s) ? -1 : PlatformAt(board, s.sectnum, s.xy, true, s.z);
    return platform >= 0 ? board.sprites[platform].z : board.floorAt(s.sectnum, s.xy);
}

std::string SpawnLabel(Board const &board, int sprite)
{
    if (sprite == -1)
        return "Map start";
    if (!validSprite(board, sprite) || board.sprites[sprite].picnum != APLAYER__)
        return {};
    auto const &entity = board.sprites[sprite];
    auto name = entity.lotag == 0 ? "DukeMatch" : entity.lotag == 1 ? "Co-op" : "Player";
    return std::string(name) + " start #" + std::to_string(sprite) + " · Sector " + std::to_string(entity.sectnum);
}

bool MakePlayStart(Board &copy, PlayPoint point, GameTarget target, std::string &error)
{
    if (point.spawn >= 0)
    {
        if (SpawnLabel(copy, point.spawn).empty())
            return fail(error, "Choose a player start sprite.");
        auto const &sprite = copy.sprites[point.spawn];
        point.xy = sprite.xy;
        point.sector = sprite.sectnum;
        point.angle = sprite.ang;
        point.platform = PlatformAt(copy, point.sector, point.xy, true, sprite.z);
        if (point.platform >= 0 && copy.sprites[point.platform].z != sprite.z)
            point.platform = -1;
    }
    if (!validSector(copy, point.sector) || copy.sectorAt(point.xy, point.sector) != point.sector)
        return fail(error, "Choose a walkable surface inside a sector.");
    if (point.platform >= 0 && (!validSprite(copy, point.platform) || !IsPlatform(copy.sprites[point.platform]) ||
        copy.sprites[point.platform].sectnum != point.sector || !insideSprite(copy.sprites[point.platform], point.xy)))
        return fail(error, "Choose a point on the catwalk.");
    auto const space = initialSpace(copy, point.sector, point.xy, target);
    int const floor = point.platform >= 0 ? copy.sprites[point.platform].z : space.floor;
    if (int64_t(floor) - space.ceiling < playerHeight)
        return fail(error, "This surface needs at least 48 units of headroom.");
    int const z = floor - eyeHeight;
    if (!inPlayerBounds(copy, point.sector, point.xy, z, point.platform, space, error))
        return false;
    copy.start = {point.xy.x, point.xy.y, z};
    copy.startSector = point.sector;
    if (point.angle >= 0)
        copy.angle = point.angle & 2047;
    return true;
}

bool AddCatwalk(Board &board, int section, vec2_t center, int tile, int length, int width,
                int height, int angle, std::string &error)
{
    if (!validSector(board, section) || !hasArt(tile) || length < 128 || width < 128 || length > BXY_MAX || width > BXY_MAX ||
        height < 48 || height > 32768 || unsigned(angle) >= 2048 ||
        board.sectorAt(center, section) != section)
        return fail(error, "Choose a catwalk center in an open room, a tile with art, and at least 48 units of height.");
    auto const size = tilesiz[tile];
    int const xRepeat = std::lround(width * 4. / size.x);
    int const yRepeat = std::lround(length * 4. / size.y);
    if (xRepeat < 1 || xRepeat > 255 || yRepeat < 1 || yRepeat > 255)
        return fail(error, "That tile cannot cover these dimensions in one classic sprite. Use a larger tile or shorter spans.");
    if (board.sprites.size() >= MAXSPRITESV7)
        return fail(error, "This map has reached the classic sprite limit.");
    uspritetypev7 sprite{};
    int64_t const z = int64_t(board.floorAt(section, center)) - height * 256;
    if (z < INT32_MIN)
        return fail(error, "The catwalk height is outside the map range.");
    sprite.xy = center;
    sprite.z = z;
    sprite.sectnum = section;
    sprite.picnum = tile;
    sprite.cstat = bridgeFlags;
    sprite.ang = angle;
    sprite.xrepeat = xRepeat;
    sprite.yrepeat = yRepeat;
    sprite.owner = sprite.extra = -1;
    sprite.clipdist = 32;

    auto const corners = PlatformCorners(sprite);
    auto const &room = board.sectors[section];
    for (auto const point : corners)
        if (board.sectorAt(point, section) != section ||
            int64_t(board.floorAt(section, point)) - z < playerHeight ||
            z - board.ceilingAt(section, point) < playerHeight)
            return fail(error, "Keep the catwalk inside one room with 48 units of clearance above and below it.");
    for (int i = room.wallptr; i < room.wallptr + room.wallnum; ++i)
    {
        auto const &edge = board.walls[i];
        if (crossesPlatformEdge(edge.xy, board.walls[edge.point2].xy, corners) ||
            (insideSprite(sprite, edge.xy) && std::none_of(corners.begin(), corners.end(),
                [&](vec2_t corner) { return edge.x == corner.x && edge.y == corner.y; })))
            return fail(error, "The catwalk crosses a sector boundary or pillar.");
    }
    board.sprites.push_back(sprite);
    return true;
}

bool DukeMechanisms(GameTarget target)
{
    return target == GameTarget::Duke13 || target == GameTarget::DukeAtomic;
}

const char *MechanismName(Mechanism type)
{
    switch (type)
    {
        case Mechanism::Lift: return "Lift";
        case Mechanism::Door: return "Ceiling door";
        case Mechanism::Trap: return "Crusher trap";
        case Mechanism::Light: return "Light switch";
    }
    return "";
}

std::vector<int> Neighbors(Board const &board, int section)
{
    std::vector<int> neighbors;
    if (validSector(board, section))
    {
        auto const &s = board.sectors[section];
        for (int w = s.wallptr; w < s.wallptr + s.wallnum; ++w)
        {
            int const next = board.walls[w].nextsector;
            if (next >= 0 && next != section && !(board.walls[w].cstat & 1) &&
                std::find(neighbors.begin(), neighbors.end(), next) == neighbors.end())
                neighbors.push_back(next);
        }
    }
    return neighbors;
}

bool AddMechanism(Board &board, MechanismSetup const &setup, GameTarget target, int &tag, std::string &error)
{
    if (!DukeMechanisms(target))
        return fail(error, "Choose Duke3D 1.3D or Atomic for these stock Duke mechanisms.");
    if (!validSector(board, setup.section) || setup.buttons.empty())
        return fail(error, "Choose a target sector and at least one solid wall for a switch.");
    auto const &room = board.sectors[setup.section];
    if (room.lotag || ((room.floorstat | room.ceilingstat) & 2 &&
                       setup.type != Mechanism::Light))
        return fail(error, "Choose an untagged target sector with level surfaces for moving geometry.");
    for (auto const &s : board.sprites)
        if (s.sectnum == setup.section && (s.picnum == SECTOREFFECTOR__ || s.picnum == ACTIVATOR__ ||
                                          s.picnum == ACTIVATORLOCKED__ || s.picnum == GPSPEED__))
            return fail(error, "The target already has effectors, activators, or a speed marker. Adjust its existing links in Inspector.");

    auto const neighbors = Neighbors(board, setup.section);
    if (setup.type == Mechanism::Lift)
    {
        auto hasStop = [&](int s) { return std::find(neighbors.begin(), neighbors.end(), s) != neighbors.end(); };
        if (!hasStop(setup.firstStop) || !hasStop(setup.secondStop))
            return fail(error, "Both stops must share a passable portal with the lift.");
        auto const a = board.sectors[setup.firstStop].floorz, b = board.sectors[setup.secondStop].floorz;
        auto const top = std::min(a, b), bottom = std::max(a, b);
        if (top == bottom || (room.floorz != top && room.floorz != bottom) ||
            (int64_t(top) - room.ceilingz < playerHeight))
            return fail(error, "Use two different floor heights, align the platform to either stop, and allow 48 units of headroom at the top.");
        for (int s : neighbors)
            if ((board.sectors[s].floorstat & 2) || (board.sectors[s].floorz != top && board.sectors[s].floorz != bottom))
                return fail(error, "The stock lift needs exactly two reachable, level neighboring floor heights.");
    }
    if (setup.type == Mechanism::Door)
    {
        bool opens = false;
        for (int s : neighbors)
            opens |= int64_t(room.floorz) - board.sectors[s].ceilingz >= playerHeight;
        if (!opens || room.floorz < room.ceilingz ||
            (room.floorz != room.ceilingz && int64_t(room.floorz) - room.ceilingz < playerHeight))
            return fail(error, "Use a closed door or a full-height opening next to a room with a high enough ceiling.");
    }
    int64_t const trapFloor = int64_t(room.floorz) + setup.drop * 256LL;
    if (setup.type == Mechanism::Trap && (setup.drop < 16 || setup.drop > 4096 || trapFloor > INT32_MAX ||
        int64_t(room.floorz) - room.ceilingz < playerHeight))
        return fail(error, "Use a trap sector with 48 units of clearance and a floor drop of 16–4096 units.");
    if (setup.speed < 1 || setup.speed > INT16_MAX)
        return fail(error, "Enter a classic movement speed between 1 and 32767.");
    if (setup.type == Mechanism::Light && (setup.lightShade >= room.floorshade || setup.lightShade < -128 ||
                                          unsigned(setup.lightPalette) > 255))
        return fail(error, "Use a light shade lower than the current floor shade and a palette between 0 and 255.");

    vec2_t marker;
    if (!findMarker(board, setup.section, marker))
        return fail(error, "The target needs a clear point inside its walls for the effect markers.");
    int const switchTile = setup.type == Mechanism::Trap ? DIPSWITCH2__ :
                           setup.type == Mechanism::Light ? LIGHTSWITCH__ : POWERSWITCH1__;
    bool const shootable = setup.type == Mechanism::Trap;
    std::vector<uspritetypev7> switches;
    for (int wall : setup.buttons)
    {
        if (!validWall(board, wall) || board.walls[wall].nextwall >= 0)
            return fail(error, "Choose a solid wall for each switch.");
        auto const &edge = board.walls[wall];
        auto const a = edge.xy, b = board.walls[edge.point2].xy;
        auto const middle = wallMiddle(board, wall);
        double const length = std::hypot(double(b.x - a.x), double(b.y - a.y));
        if (length < 128.)
            return fail(error, "Choose a switch wall at least 128 units wide.");
        int const section = board.wallSector(wall);
        vec2_t const point{middle.x - int(std::lround(24 * (b.y - a.y) / length)),
                           middle.y + int(std::lround(24 * (b.x - a.x) / length))};
        if (board.sectorAt(point, section) != section ||
            int64_t(board.floorAt(section, point)) - board.ceilingAt(section, point) < playerHeight)
            return fail(error, "The room beside that switch needs 48 units of clear height.");
        if (setup.type == Mechanism::Trap && section == setup.section)
            return fail(error, "Place the crusher switch in a different sector from the moving floor and ceiling.");
        uspritetypev7 button{};
        button.xy = point;
        button.z = board.floorAt(section, point) - (shootable ? 32 << 8 : eyeHeight);
        button.sectnum = section;
        button.picnum = switchTile;
        button.ang = getangle(a.y - b.y, b.x - a.x);
        button.cstat = CSTAT_SPRITE_ALIGNMENT_WALL | CSTAT_SPRITE_YCENTER | CSTAT_SPRITE_BLOCK_HITSCAN;
        button.xrepeat = shootable ? 48 : 32;
        button.yrepeat = shootable ? 52 : 32;
        button.clipdist = 32;
        button.owner = button.extra = -1;
        switches.push_back(button);
    }

    int const link = freeTag(board);
    int const markers = setup.type == Mechanism::Trap ? 4 : setup.type == Mechanism::Light ? 1 : 2;
    if (!link || board.sprites.size() + markers + switches.size() > MAXSPRITESV7)
        return fail(error, "The map has run out of free tags or sprite slots.");
    auto addMarker = [&](int tile, int lotag, int z) -> uspritetypev7 & {
        uspritetypev7 item{};
        item.xy = marker;
        item.z = z;
        item.sectnum = setup.section;
        item.picnum = tile;
        item.lotag = lotag;
        item.owner = item.extra = -1;
        item.xrepeat = item.yrepeat = 16;
        board.sprites.push_back(item);
        return board.sprites.back();
    };
    if (setup.type == Mechanism::Light)
    {
        auto &light = addMarker(SECTOREFFECTOR__, 12, room.floorz);
        light.hitag = link;
        light.shade = setup.lightShade;
        light.pal = setup.lightPalette;
    }
    else
    {
        addMarker(ACTIVATOR__, link, room.floorz);
        int speed = setup.speed;
        if (setup.type == Mechanism::Trap)
        {
            // Classic SE31 stops once it is within one step of the target.
            int const travel = setup.drop * 256;
            speed = std::min(speed, travel);
            while (travel % speed)
                --speed;
        }
        addMarker(GPSPEED__, speed, room.floorz);
        if (setup.type == Mechanism::Trap)
        {
            addMarker(SECTOREFFECTOR__, 31, room.floorz).ang = 512;
            addMarker(SECTOREFFECTOR__, 32, int(trapFloor) - (8 << 8)).ang = 1536;
            board.sectors[setup.section].floorz = trapFloor;
        }
        else
            board.sectors[setup.section].lotag = setup.type == Mechanism::Lift ? 17 : 20;
    }
    for (auto button : switches)
    {
        button.lotag = link;
        board.sprites.push_back(button);
    }
    tag = link;
    return true;
}

std::vector<MapLink> MapLinks(Board const &board)
{
    auto const receivers = linkReceivers(board);
    std::vector<MapLink> links;
    auto connect = [&](ObjectRef source, int tile, int tag) {
        if (!swatch(tile) || tag <= 0)
            return;
        auto const match = receivers.find(tag);
        if (match != receivers.end())
            for (auto const target : match->second)
            {
                // A visual overview stays bounded even if a map repeats thousands of tags.
                if (links.size() >= MAXWALLSV7)
                    break;
                links.push_back({source, target, tag});
            }
    };
    for (int i = 0; i < int(board.walls.size()); ++i)
        connect({ObjectKind::Wall, i}, board.walls[i].picnum, board.walls[i].lotag);
    for (int i = 0; i < int(board.sprites.size()); ++i)
        connect({ObjectKind::Sprite, i}, board.sprites[i].picnum, board.sprites[i].lotag);
    return links;
}

bool TranslateObjects(Board &board, std::vector<ObjectRef> const &objects, vec3_t offset, std::string &error)
{
    if (objects.empty() || !(offset.x || offset.y || offset.z))
        return fail(error, "Select objects and enter a movement offset.");
    Board next = board;
    std::vector<int> vertices;
    auto shifted = [&](vec2_t point, vec2_t &result) {
        auto const x = int64_t(point.x) + offset.x, y = int64_t(point.y) + offset.y;
        if (x < -BXY_MAX || x > BXY_MAX || y < -BXY_MAX || y > BXY_MAX)
            return false;
        result = {int(x), int(y)};
        return true;
    };
    for (auto const object : objects)
        if (object.kind == ObjectKind::Wall && validWall(board, object.index))
        {
            if (!offset.x && !offset.y)
                return fail(error, "Select sprites to move vertically. Walls use their sector’s floor and ceiling.");
            vertices.push_back(object.index);
            vertices.push_back(board.walls[object.index].point2);
        }
    for (int i : next.linkedVertices(std::move(vertices)))
        if (!shifted(board.walls[i].xy, next.walls[i].xy))
            return fail(error, "A wall would move outside the editor grid.");
    for (auto const object : objects)
        if (object.kind == ObjectKind::Sprite && validSprite(board, object.index))
        {
            auto const &old = board.sprites[object.index];
            auto &item = next.sprites[object.index];
            vec2_t point;
            if (!shifted(old.xy, point))
                return fail(error, "A sprite would move outside the editor grid.");
            int const section = next.sectorAt(point, old.sectnum);
            if (section < 0)
                return fail(error, "A sprite would move outside the map.");
            int64_t const z = int64_t(old.z) - board.floorAt(old.sectnum, old.xy) +
                              next.floorAt(section, point) + offset.z;
            if (z < next.ceilingAt(section, point) || z > next.floorAt(section, point))
                return fail(error, "A sprite would move through a ceiling or floor.");
            item.xy = point;
            item.sectnum = section;
            item.z = z;
        }
    if (!next.validateGeometryChange(board, error))
        return false;
    board = std::move(next);
    return true;
}

bool AlignWalls(Board &board, std::vector<ObjectRef> const &objects, std::string &error)
{
    std::vector<int> selected;
    for (auto const object : objects)
        if (object.kind == ObjectKind::Wall && validWall(board, object.index))
            selected.push_back(object.index);
    if (selected.size() < 2)
        return fail(error, "Select two or more connected walls with the same tile.");
    int const tile = board.walls[selected.front()].picnum;
    if (!hasArt(tile) || std::any_of(selected.begin(), selected.end(), [&](int w) {
        auto const &edge = board.walls[w];
        return edge.picnum != tile || (edge.cstat & (8 | 48 | 256)) || edge.nextwall >= 0;
    }))
        return fail(error, "Use solid, unflipped walls with the same available tile.");
    std::vector<int> order;
    int first = selected.front();
    for (int i : selected)
        if (std::none_of(selected.begin(), selected.end(), [&](int w) {
            return board.walls[w].point2 == i;
        }))
        {
            first = i;
            break;
        }
    for (int w = first; std::find(selected.begin(), selected.end(), w) != selected.end() &&
                        std::find(order.begin(), order.end(), w) == order.end(); w = board.walls[w].point2)
        order.push_back(w);
    if (order.size() != selected.size())
        return fail(error, "Select a continuous run of walls in one sector.");
    auto const size = tilesiz[tile];
    auto referenceZ = [&](int w) {
        auto const &edge = board.walls[w];
        int const s = board.wallSector(w);
        return edge.cstat & 4 ? board.floorAt(s, edge.xy) : board.ceilingAt(s, edge.xy);
    };
    for (size_t i = 1; i < order.size(); ++i)
    {
        auto const &prev = board.walls[order[i - 1]];
        auto &next = board.walls[order[i]];
        next.xpanning = (prev.xpanning + prev.xrepeat * 8) % size.x;
        int64_t const delta = int64_t(referenceZ(order[i])) - referenceZ(order[i - 1]);
        next.ypanning = uint8_t(prev.ypanning + delta * prev.yrepeat / (size.y * 8));
        next.yrepeat = prev.yrepeat;
    }
    return true;
}

bool SetObjectHeight(Board &board, std::vector<ObjectRef> const &objects, int height, std::string &error)
{
    std::vector<std::pair<int, int>> heights;
    for (auto const object : objects)
        if (object.kind == ObjectKind::Sprite && validSprite(board, object.index))
        {
            auto const &s = board.sprites[object.index];
            auto const z = int64_t(SpriteFloor(board, object.index)) - height * 256LL;
            if (height < 0 || z < board.ceilingAt(s.sectnum, s.xy))
                return fail(error, "At least one sprite would move above its sector ceiling.");
            heights.emplace_back(object.index, int(z));
        }
    if (heights.empty())
        return fail(error, "Select one or more sprites to set their height above the floor.");
    for (auto const height : heights)
        board.sprites[height.first].z = height.second;
    return true;
}

std::vector<MapIssue> CheckMap(Board const &board, GameTarget target, bool modern, MapSettings const &settings,
                              std::string const &assetRoot, bool loadedTarget)
{
    std::vector<MapIssue> issues;
    auto add = [&](ObjectRef object, std::string message) { issues.push_back({object, std::move(message)}); };
    auto tile = [&](ObjectRef object, int id, const char *field) {
        if (id < 0 || id >= Target(target).tiles)
            add(object, std::string(field) + " tile " + std::to_string(id) + " is outside the target’s tile range.");
        else if (loadedTarget && !hasArt(id))
            add(object, std::string(field) + " tile " + std::to_string(id) + " has no loaded art.");
    };
    if (!loadedTarget)
        add({}, "Load the selected base game and its project art to check tile availability.");
    if (!validSector(board, board.startSector))
        add({}, "Place a Player Start inside a sector.");
    else
    {
        vec2_t const point{board.start.x, board.start.y};
        int const platform = PlatformAt(board, board.startSector, point, true, board.start.z);
        std::string error;
        if (!inPlayerBounds(board, board.startSector, point, board.start.z, platform,
                            initialSpace(board, board.startSector, point, target), error))
            add({ObjectKind::Start, 0}, std::move(error));
    }
    auto const receivers = DukeMechanisms(target) ? linkReceivers(board) : Receivers{};
    auto checkSwitch = [&](ObjectRef object, int id, int tag) {
        if (!DukeMechanisms(target) || !swatch(id) || tag == -1)
            return;
        bool const linked = receivers.count(tag) || (id >= MULTISWITCH__ && id <= MULTISWITCH__ + 3 &&
            (receivers.count(tag + 1) || receivers.count(tag + 2) || receivers.count(tag + 3)));
        if (tag <= 0 || !linked)
            add(object, "Switch tag " + std::to_string(tag) + " has no matching activator, light, or other known receiver.");
    };
    for (int i = 0; i < int(board.sectors.size()); ++i)
    {
        auto const &s = board.sectors[i];
        ObjectRef const ref{ObjectKind::Sector, i};
        tile(ref, s.floorpicnum, "Floor");
        tile(ref, s.ceilingpicnum, "Ceiling");
        if ((s.floorstat | s.ceilingstat) & ~0x7f)
            add(ref, "Sector flags exceed the classic v7 target format.");
    }
    for (int i = 0; i < int(board.walls.size()); ++i)
    {
        auto const &w = board.walls[i];
        ObjectRef const ref{ObjectKind::Wall, i};
        tile(ref, w.picnum, "Wall");
        if (w.overpicnum >= 0 || (w.cstat & 48))
            tile(ref, w.overpicnum, "Overlay");
        if (w.cstat & ~0x3ff)
            add(ref, "Wall flags exceed the classic v7 target format.");
        if (w.nextwall >= 0 && (!validWall(board, w.nextwall) || board.walls[w.nextwall].nextwall != i))
            add(ref, "The two sides of this portal do not point to each other.");
        checkSwitch(ref, w.picnum, w.lotag);
    }
    for (int i = 0; i < int(board.sprites.size()); ++i)
    {
        auto const &s = board.sprites[i];
        ObjectRef const ref{ObjectKind::Sprite, i};
        tile(ref, s.picnum, "Sprite");
        if ((s.cstat & ~0x83ff) || (s.cstat & 48) == 48)
            add(ref, "Sprite flags exceed the classic v7 target format.");
        if (DukeMechanisms(target) && s.picnum == APLAYER__ && validSector(board, s.sectnum))
        {
            std::string error;
            auto const z = int64_t(s.z) - eyeHeight;
            auto const support = PlatformAt(board, s.sectnum, s.xy, true, s.z);
            if (z < INT32_MIN || !board.containsPoint(s.sectnum, s.xy) ||
                !inPlayerBounds(board, s.sectnum, s.xy, z, support, initialSpace(board, s.sectnum, s.xy, target), error))
            {
                if (error.empty()) error = "Move this spawn inside its assigned sector.";
                add(ref, SpawnLabel(board, i) + ". " + error);
            }
        }
        checkSwitch(ref, s.picnum, s.lotag);
    }
    for (auto const &attachment : settings.scripts)
    {
        auto const kind = attachment.kind == "wall" ? ObjectKind::Wall : attachment.kind == "sector" ?
                          ObjectKind::Sector : ObjectKind::Sprite;
        ObjectRef const object{kind, attachment.index};
        if (!modern)
            add(object, "Enable modern features or remove the script attachment.");
        else if (target != GameTarget::Standalone)
            add(object, "Script attachment requires NNBuild. Stock " + std::string(Target(target).label) + " plays the map without it.");
        if (ObjectSector(board, object) < 0)
            add({}, "An attachment lost its map object. Reattach " + attachment.script + ".");
        if (assetRoot.empty() || !buildvfs_exists((assetRoot + "/" + attachment.script).c_str()))
            add(object, "Missing project script " + attachment.script + ".");
    }
    std::string error;
    if (!board.validate(Target(target).tiles, error))
        add({}, std::move(error));
    return issues;
}

} // namespace DukeEditor
