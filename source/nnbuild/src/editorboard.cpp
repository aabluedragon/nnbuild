#include "editorboard.h"

#include <cmath>

#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
#endif

namespace DukeEditor {
namespace {

bool samePoint(vec2_t a, vec2_t b)
{
    return a.x == b.x && a.y == b.y;
}

int64_t cross(vec2_t a, vec2_t b, vec2_t p)
{
    return (int64_t(b.x) - a.x) * (int64_t(p.y) - a.y) - (int64_t(b.y) - a.y) * (int64_t(p.x) - a.x);
}

int gcd(int a, int b)
{
    while (b)
    {
        int const remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

bool onSegment(vec2_t a, vec2_t b, vec2_t p)
{
    return cross(a, b, p) == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x)
                               && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

bool crosses(vec2_t a, vec2_t b, vec2_t c, vec2_t d)
{
    if (onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b))
        return true;
    return (cross(a, b, c) < 0) != (cross(a, b, d) < 0) &&
           (cross(c, d, a) < 0) != (cross(c, d, b) < 0);
}

bool usablePoint(vec2_t p)
{
    return p.x >= -BXY_MAX && p.x <= BXY_MAX && p.y >= -BXY_MAX && p.y <= BXY_MAX;
}

bool validPolygon(std::vector<vec2_t> const &points, int64_t &area)
{
    if (points.size() < 3)
        return false;
    area = 0;
    for (size_t i = 0, n = points.size(); i < n; ++i)
    {
        auto const a = points[i], b = points[(i + 1) % n];
        if (!usablePoint(a) || samePoint(a, b))
            return false;
        area += cross({}, a, b);
        for (size_t j = i + 2; j < n && (i != 0 || j + 1 < n); ++j)
            if (crosses(a, b, points[j], points[(j + 1) % n]))
                return false;
    }
    return area != 0;
}

bool contains(Board const &board, int sectorIndex, vec2_t p)
{
    auto const &area = board.sectors[sectorIndex];
    bool inside = false;
    for (int i = area.wallptr; i < area.wallptr + area.wallnum; ++i)
    {
        auto const a = board.walls[i].xy, b = board.walls[board.walls[i].point2].xy;
        if (onSegment(a, b, p))
            return true;
        if ((a.y > p.y) != (b.y > p.y) && (cross(a, b, p) > 0) == (b.y > a.y))
            inside = !inside;
    }
    return inside;
}

bool matchingPortal(Board const &board, int a, int b)
{
    auto const &wa = board.walls[a], &wb = board.walls[b];
    return samePoint(wa.xy, board.walls[wb.point2].xy) && samePoint(wb.xy, board.walls[wa.point2].xy);
}

void updateRepeat(uwalltypev7 &wall, vec2_t end)
{
    auto const length = std::hypot(double(end.x) - wall.x, double(end.y) - wall.y);
    wall.xrepeat = clamp(int(length * wall.yrepeat / 1024), 1, 255);
}

int slopeAt(Board const &board, usectortypev7 const &sector, vec2_t p, int z, int heinum, int stat)
{
    if (!(stat & 2))
        return z;
    auto const a = board.walls[sector.wallptr].xy, b = board.walls[board.walls[sector.wallptr].point2].xy;
    auto const length = int64_t(std::hypot(double(b.x) - a.x, double(b.y) - a.y)) << 5;
    if (!length)
        return z;
    return int(clamp(int64_t(z) + (cross(a, b, p) >> 3) * heinum / length, int64_t(INT32_MIN), int64_t(INT32_MAX)));
}

struct Input
{
    const std::vector<uint8_t> &data;
    size_t offset = 0;

    template <typename T>
    bool read(T &value)
    {
        if (sizeof(T) > data.size() - offset)
            return false;
        Bmemcpy(&value, data.data() + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    template <typename T>
    bool readList(std::vector<T> &values, int limit)
    {
        uint16_t count;
        if (!read(count))
            return false;
        count = B_LITTLE16(count);
        if (count > limit || sizeof(T) * count > data.size() - offset)
            return false;
        values.resize(count);
        for (auto &value : values)
            read(value);
        return true;
    }
};

template <typename T>
void append(std::vector<uint8_t> &bytes, T const &value)
{
    auto const data = reinterpret_cast<const uint8_t *>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
}

void fixEndian(usectortypev7 &s)
{
    s.wallptr = B_LITTLE16(s.wallptr); s.wallnum = B_LITTLE16(s.wallnum);
    s.ceilingz = B_LITTLE32(s.ceilingz); s.floorz = B_LITTLE32(s.floorz);
    s.ceilingstat = B_LITTLE16(s.ceilingstat); s.floorstat = B_LITTLE16(s.floorstat);
    s.ceilingpicnum = B_LITTLE16(s.ceilingpicnum); s.floorpicnum = B_LITTLE16(s.floorpicnum);
    s.ceilingheinum = B_LITTLE16(s.ceilingheinum); s.floorheinum = B_LITTLE16(s.floorheinum);
    s.lotag = B_LITTLE16(s.lotag); s.hitag = B_LITTLE16(s.hitag); s.extra = B_LITTLE16(s.extra);
}

void fixEndian(uwalltypev7 &w)
{
    w.x = B_LITTLE32(w.x); w.y = B_LITTLE32(w.y);
    w.point2 = B_LITTLE16(w.point2); w.nextwall = B_LITTLE16(w.nextwall); w.nextsector = B_LITTLE16(w.nextsector);
    w.cstat = B_LITTLE16(w.cstat); w.picnum = B_LITTLE16(w.picnum); w.overpicnum = B_LITTLE16(w.overpicnum);
    w.lotag = B_LITTLE16(w.lotag); w.hitag = B_LITTLE16(w.hitag); w.extra = B_LITTLE16(w.extra);
}

void fixEndian(uspritetypev7 &s)
{
    s.x = B_LITTLE32(s.x); s.y = B_LITTLE32(s.y); s.z = B_LITTLE32(s.z);
    s.cstat = B_LITTLE16(s.cstat); s.picnum = B_LITTLE16(s.picnum);
    s.sectnum = B_LITTLE16(s.sectnum); s.statnum = B_LITTLE16(s.statnum);
    s.ang = B_LITTLE16(s.ang); s.owner = B_LITTLE16(s.owner);
    s.xvel = B_LITTLE16(s.xvel); s.yvel = B_LITTLE16(s.yvel); s.zvel = B_LITTLE16(s.zvel);
    s.lotag = B_LITTLE16(s.lotag); s.hitag = B_LITTLE16(s.hitag); s.extra = B_LITTLE16(s.extra);
}

template <typename T>
void appendList(std::vector<uint8_t> &bytes, const std::vector<T> &items)
{
    append(bytes, static_cast<uint16_t>(B_LITTLE16(items.size())));
    for (auto item : items)
    {
        fixEndian(item);
        append(bytes, item);
    }
}

bool readBytes(std::string const &path, std::vector<uint8_t> &bytes)
{
    if (auto const file = buildvfs_fopen_read(path.c_str()))
    {
        auto const length = buildvfs_flength(file);
        if (length >= 4 && length <= int64_t(maxBoardBytes))
            bytes.resize(length);
        bool const ok = !bytes.empty() && buildvfs_fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
        buildvfs_fclose(file);
        return ok;
    }
    auto const fd = kopen4load(path.c_str(), 0);
    if (fd == buildvfs_kfd_invalid)
        return false;
    auto const length = kfilelength(fd);
    if (length >= 4 && length <= int64_t(maxBoardBytes))
        bytes.resize(length);
    bool const ok = !bytes.empty() && kread(fd, bytes.data(), bytes.size()) == int(bytes.size());
    kclose(fd);
    return ok;
}

std::string temporaryMap(std::string const &directory)
{
#ifdef _WIN32
    char path[MAX_PATH];
    return GetTempFileNameA(directory.c_str(), "nnb", 0, path) ? path : "";
#else
    std::string path = directory + "/.nnbmap-XXXXXX";
    auto const fd = mkstemp(&path[0]);
    if (fd == -1)
        return {};
    close(fd);
    return path;
#endif
}

} // namespace

int Board::wallSector(int wallIndex) const
{
    for (int i = 0; i < int(sectors.size()); ++i)
        if (wallIndex >= sectors[i].wallptr && wallIndex < sectors[i].wallptr + sectors[i].wallnum)
            return i;
    return -1;
}

bool Board::containsPoint(int section, vec2_t point) const
{
    return unsigned(section) < sectors.size() && contains(*this, section, point);
}

int Board::sectorAt(vec2_t point, int preferred) const
{
    if (unsigned(preferred) < sectors.size() && contains(*this, preferred, point))
        return preferred;
    for (int i = 0; i < int(sectors.size()); ++i)
        if (i != preferred && contains(*this, i, point))
            return i;
    return -1;
}

int Board::floorAt(int index, vec2_t point) const
{
    auto const &s = sectors[index];
    return slopeAt(*this, s, point, s.floorz, s.floorheinum, s.floorstat);
}

int Board::ceilingAt(int index, vec2_t point) const
{
    auto const &s = sectors[index];
    return slopeAt(*this, s, point, s.ceilingz, s.ceilingheinum, s.ceilingstat);
}

std::vector<int> Board::linkedVertices(std::vector<int> pending) const
{
    std::vector<int> previous(walls.size());
    std::vector<bool> visited(walls.size());
    for (int i = 0; i < int(walls.size()); ++i)
        previous[walls[i].point2] = i;
    for (int vertex : pending)
        visited[vertex] = true;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        int const vertex = pending[i];
        for (int edge : {vertex, previous[vertex]})
        {
            auto const twin = walls[edge].nextwall;
            if (twin < 0 || walls[twin].nextwall != edge || !matchingPortal(*this, edge, twin))
                continue;
            int const other = edge == vertex ? walls[twin].point2 : twin;
            if (!visited[other])
            {
                visited[other] = true;
                pending.push_back(other);
            }
        }
    }
    return pending;
}

bool Board::addRoom(std::vector<vec2_t> points, int fromWall)
{
    int64_t area;
    if (sectors.size() >= MAXSECTORSV7 || walls.size() + points.size() > MAXWALLSV7 ||
        !validPolygon(points, area))
        return false;
    if (area < 0)
        std::reverse(points.begin(), points.end());

    int const startWall = walls.size(), newSector = sectors.size();
    usectortypev7 room{};
    room.floorpicnum = 899;
    room.ceilingpicnum = 742;
    room.ceilingz = -(64 << 8);
    room.extra = -1;
    if (fromWall >= 0)
    {
        auto const &parent = sectors[wallSector(fromWall)];
        room.floorz = parent.floorz;
        room.ceilingz = parent.ceilingz;
        room.floorpicnum = parent.floorpicnum;
        room.ceilingpicnum = parent.ceilingpicnum;
        room.floorshade = parent.floorshade;
        room.ceilingshade = parent.ceilingshade;
    }
    room.wallptr = startWall;
    room.wallnum = points.size();
    sectors.push_back(room);

    for (int i = 0; i < room.wallnum; ++i)
    {
        int const next = (i + 1) % room.wallnum;
        uwalltypev7 edge{};
        edge.xy = points[i];
        edge.point2 = startWall + next;
        edge.nextwall = edge.nextsector = -1;
        edge.picnum = fromWall >= 0 ? walls[fromWall].picnum : 757;
        edge.overpicnum = -1;
        edge.yrepeat = fromWall >= 0 ? walls[fromWall].yrepeat : 8;
        edge.extra = -1;
        updateRepeat(edge, points[next]);
        walls.push_back(edge);
    }

    // Exactly reversed edges form portals between adjacent rooms.
    auto link = [&](int i, int j) {
        walls[i].nextwall = j;
        walls[i].nextsector = wallSector(j);
        walls[j].nextwall = i;
        walls[j].nextsector = newSector;
    };
    if (fromWall >= 0)
        for (int i = startWall; i < int(walls.size()); ++i)
            if (matchingPortal(*this, i, fromWall))
            {
                link(i, fromWall);
                break;
            }
    for (int i = startWall; i < int(walls.size()); ++i)
        if (walls[i].nextwall == -1)
            for (int j = 0; j < startWall; ++j)
                if (walls[j].nextwall == -1 && matchingPortal(*this, i, j))
                {
                    link(i, j);
                    break;
                }
    return true;
}

bool Board::addPillar(int section, std::vector<vec2_t> points, int tile)
{
    int64_t area;
    if (unsigned(section) >= sectors.size() || walls.size() + points.size() > MAXWALLSV7 ||
        !validPolygon(points, area))
        return false;
    auto &room = sectors[section];
    int const first = room.wallptr + room.wallnum;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (!contains(*this, section, points[i]))
            return false;
        for (int w = room.wallptr; w < first; ++w)
            if (crosses(points[i], points[(i + 1) % points.size()], walls[w].xy, walls[walls[w].point2].xy))
                return false;
    }

    // Check for a hole, sprite, or spawn entirely inside the new pillar.
    auto covers = [&](vec2_t point) {
        bool inside = false;
        for (size_t i = 0; i < points.size(); ++i)
        {
            auto const a = points[i], b = points[(i + 1) % points.size()];
            if (onSegment(a, b, point))
                return true;
            if ((a.y > point.y) != (b.y > point.y) && (cross(a, b, point) > 0) == (b.y > a.y))
                inside = !inside;
        }
        return inside;
    };
    for (int w = room.wallptr; w < first; ++w)
        if (covers(walls[w].xy))
            return false;
    for (auto const &sprite : sprites)
        if (sprite.sectnum == section && covers(sprite.xy))
            return false;
    if (startSector == section && covers({start.x, start.y}))
        return false;

    if (area > 0)
        std::reverse(points.begin(), points.end());
    int const count = points.size();
    auto const material = walls[room.wallptr];
    for (auto &edge : walls)
    {
        if (edge.point2 >= first) edge.point2 += count;
        if (edge.nextwall >= first) edge.nextwall += count;
    }
    for (auto &other : sectors)
        if (other.wallptr >= first)
            other.wallptr += count;
    room.wallnum += count;
    walls.insert(walls.begin() + first, count, {});
    for (int i = 0; i < count; ++i)
    {
        auto &edge = walls[first + i];
        int const next = (i + 1) % count;
        edge.xy = points[i];
        edge.point2 = first + next;
        edge.nextwall = edge.nextsector = edge.overpicnum = edge.extra = -1;
        edge.picnum = tile;
        edge.shade = material.shade;
        edge.pal = material.pal;
        edge.yrepeat = material.yrepeat;
        updateRepeat(edge, points[next]);
    }
    return true;
}

bool Board::extendRoom(int wallIndex, int depth)
{
    auto const edge = walls[wallIndex];
    if (edge.nextwall != -1)
        return false;
    auto const end = walls[edge.point2].xy;
    int const dx = end.x - edge.x, dy = end.y - edge.y;
    double const length = std::hypot(dx, dy);
    if (!length)
        return false;
    vec2_t const offset{int(std::lround(dy * depth / length)), int(std::lround(-dx * depth / length))};
    return addRoom({end, edge.xy, {edge.x + offset.x, edge.y + offset.y}, {end.x + offset.x, end.y + offset.y}}, wallIndex);
}

bool Board::addStairs(int wallIndex, int steps, int tread, int rise, std::string &error)
{
    auto fail = [&](const char *message) { error = message; return false; };
    int const parent = wallSector(wallIndex);
    if (parent < 0 || walls[wallIndex].nextwall >= 0)
        return fail("Select a solid wall at the foot of the stairs.");
    if (steps < 2 || steps > 32 || tread < 128 || tread > 4096 || !rise || rise < -16 || rise > 16)
        return fail("Use 2–32 steps, at least 128 units of tread, and at most 16 units of rise per step.");
    auto const room = sectors[parent];
    if ((room.floorstat | room.ceilingstat) & 2)
        return fail("Use a level floor and ceiling at the start of the stairs.");

    Board next = *this;
    for (int i = 1; i <= steps; ++i)
    {
        int const begin = next.walls.size();
        if (!next.extendRoom(wallIndex, tread))
            return fail("The stair flight reaches the edge of the map or the classic map limits.");
        auto &step = next.sectors.back();
        int64_t const z = int64_t(room.floorz) - i * rise * 256;
        if (z > INT32_MAX || z < int64_t(room.ceilingz) + (48 << 8))
            return fail("The stair flight needs more headroom. Raise the parent ceiling or reduce the rise.");
        step.floorz = z;
        step.ceilingz = room.ceilingz;

        int const join = next.walls[wallIndex].nextwall;
        if (join < begin)
            return fail("The stair tread is too narrow to form a portal.");
        wallIndex = next.walls[next.walls[join].point2].point2;
        auto strictlyInside = [&](int section, vec2_t point) {
            auto const &s = next.sectors[section];
            for (int w = s.wallptr; w < s.wallptr + s.wallnum; ++w)
                if (onSegment(next.walls[w].xy, next.walls[next.walls[w].point2].xy, point))
                    return false;
            return contains(next, section, point);
        };
        int const stepIndex = next.sectors.size() - 1;
        for (int w = 0; w < begin; ++w)
        {
            auto const a = next.walls[w].xy, b = next.walls[next.walls[w].point2].xy;
            if (strictlyInside(stepIndex, a))
                return fail("The stair flight would overlap existing rooms.");
            for (int s = begin; s < begin + step.wallnum; ++s)
            {
                auto const c = next.walls[s].xy, d = next.walls[next.walls[s].point2].xy;
                if (samePoint(a, c) || samePoint(a, d) || samePoint(b, c) || samePoint(b, d))
                    continue;
                if (crosses(a, b, c, d))
                    return fail("The stair flight would cross an existing wall.");
            }
        }
        for (int s = 0; s < stepIndex; ++s)
            if (strictlyInside(s, next.walls[wallIndex].xy))
                return fail("The stair flight would overlap an existing room.");
    }
    *this = std::move(next);
    return true;
}

bool Board::splitWall(int wallIndex, int *newVertex)
{
    auto const edge = walls[wallIndex];
    int const twin = edge.nextwall;
    if (walls.size() + (twin >= 0 ? 2 : 1) > MAXWALLSV7 ||
        (twin >= 0 && (walls[twin].nextwall != wallIndex || !matchingPortal(*this, wallIndex, twin))))
        return false;
    for (auto const &w : walls)
        if (w.nextwall >= 0 && (w.nextwall == wallIndex || w.nextwall == twin) &&
            w.point2 >= 0 && walls[w.nextwall].nextwall != &w - walls.data())
            return false;
    auto const end = walls[edge.point2].xy;
    int const dx = end.x - edge.x, dy = end.y - edge.y;
    int const segments = gcd(std::abs(dx), std::abs(dy));
    if (segments < 2)
        return false;
    int const firstSegments = segments / 2;
    vec2_t const mid{edge.x + (dx / segments) * firstSegments, edge.y + (dy / segments) * firstSegments};

    auto remap = [wallIndex, twin](int index) { return index + (index > wallIndex) + (twin >= 0 && index > twin); };
    std::vector<uwalltypev7> updated;
    updated.reserve(walls.size() + 2);
    for (int i = 0; i < int(walls.size()); ++i)
    {
        auto w = walls[i];
        w.point2 = remap(w.point2);
        if (w.nextwall >= 0)
            w.nextwall = remap(w.nextwall);
        if (i != wallIndex && i != twin)
        {
            updated.push_back(w);
            continue;
        }

        auto second = w;
        second.xy = mid;
        if (w.nextwall >= 0)
            ++w.nextwall;
        w.point2 = updated.size() + 1;
        int const lengthPart = i == wallIndex ? firstSegments : segments - firstSegments;
        int const firstRepeat = clamp((w.xrepeat * lengthPart + segments / 2) / segments, 1, std::max(1, w.xrepeat - 1));
        w.xrepeat = firstRepeat;
        second.xrepeat = std::max(1, second.xrepeat - firstRepeat);
        if (w.cstat & 8)
            w.xpanning += second.xrepeat * 8;
        else
            second.xpanning += firstRepeat * 8;
        updated.push_back(w);
        updated.push_back(second);
    }

    auto const firstSector = wallSector(wallIndex), secondSector = wallSector(twin);
    for (int i = 0; i < int(sectors.size()); ++i)
    {
        sectors[i].wallptr = remap(sectors[i].wallptr);
        sectors[i].wallnum += (i == firstSector) + (i == secondSector);
    }
    walls = std::move(updated);
    if (newVertex)
        *newVertex = remap(wallIndex) + 1;
    return true;
}

void Board::deleteSector(int sectorIndex)
{
    auto const removed = sectors[sectorIndex];
    int const begin = removed.wallptr, end = begin + removed.wallnum;
    for (int i = int(sprites.size()) - 1; i >= 0; --i)
    {
        if (sprites[i].sectnum == sectorIndex)
            deleteSprite(i);
        else if (sprites[i].sectnum > sectorIndex)
            --sprites[i].sectnum;
    }
    sectors.erase(sectors.begin() + sectorIndex);
    walls.erase(walls.begin() + begin, walls.begin() + end);
    for (auto &sector : sectors)
        if (sector.wallptr >= end)
            sector.wallptr -= removed.wallnum;
    for (auto &wall : walls)
    {
        if (wall.point2 >= end)
            wall.point2 -= removed.wallnum;
        if (wall.nextsector == sectorIndex || (wall.nextwall >= begin && wall.nextwall < end))
            wall.nextwall = wall.nextsector = -1;
        else
        {
            if (wall.nextwall >= end)
                wall.nextwall -= removed.wallnum;
            if (wall.nextsector > sectorIndex)
                --wall.nextsector;
        }
    }
    if (startSector == sectorIndex)
        startSector = -1;
    else if (startSector > sectorIndex)
        --startSector;
}

void Board::deleteSprite(int spriteIndex)
{
    sprites.erase(sprites.begin() + spriteIndex);
    for (auto &sprite : sprites)
        if (sprite.owner == spriteIndex)
            sprite.owner = -1;
        else if (sprite.owner > spriteIndex)
            --sprite.owner;
}

bool Board::addSprite(int tile, vec2_t point, int preferredSector)
{
    int const section = sectorAt(point, preferredSector);
    if (section < 0 || sprites.size() >= MAXSPRITESV7)
        return false;
    uspritetypev7 entity{};
    entity.xy = point;
    entity.z = floorAt(section, point);
    entity.picnum = tile;
    entity.sectnum = section;
    entity.owner = entity.extra = -1;
    entity.xrepeat = entity.yrepeat = 40;
    entity.clipdist = 32;
    sprites.push_back(entity);
    return true;
}

bool Board::moveSprite(int index, vec2_t point)
{
    auto &sprite = sprites[index];
    int const nextSector = sectorAt(point, sprite.sectnum);
    if (nextSector < 0)
        return false;
    int64_t const height = int64_t(sprite.z) - floorAt(sprite.sectnum, sprite.xy);
    int64_t const z = height + floorAt(nextSector, point);
    if (z < INT32_MIN || z > INT32_MAX)
        return false;
    sprite.xy = point;
    sprite.z = z;
    sprite.sectnum = nextSector;
    return true;
}

bool Board::setStart(vec2_t point, int preferredSector)
{
    int const index = sectorAt(point, preferredSector);
    if (index < 0)
        return false;
    int const ceiling = ceilingAt(index, point), floor = floorAt(index, point);
    if (int64_t(floor) - ceiling < (48 << 8))
        return false;
    start = {point.x, point.y, floor - (40 << 8)};
    startSector = index;
    return true;
}

bool Board::validate(int tileLimit, std::string &error, bool draft) const
{
    auto fail = [&error](char const *message) { error = message; return false; };
    if ((!draft && sectors.empty()) || sectors.size() > MAXSECTORSV7 || walls.size() > MAXWALLSV7 || sprites.size() > MAXSPRITESV7)
        return fail("A classic map needs a sector and must fit the version 7 sector, wall and sprite limits.");

    std::vector<int> owners(walls.size(), -1);
    std::vector<int> incoming(walls.size());
    auto tileValid = [tileLimit](int tile) { return tile >= 0 && tile < tileLimit; };
    for (int i = 0; i < int(sectors.size()); ++i)
    {
        auto const &s = sectors[i];
        // Duke3D uses two-wall sectors for some legacy effects.
        if (s.wallptr < 0 || s.wallnum < 2 || unsigned(s.wallptr + s.wallnum) > walls.size())
            return fail("A sector points to a missing wall.");
        if (!tileValid(s.floorpicnum) || !tileValid(s.ceilingpicnum))
            return fail("A sector uses a tile outside this Duke3D edition's art range.");
        if ((s.floorstat | s.ceilingstat) & ~0x7f)
            return fail("A sector uses flags from an extended map format.");
        for (int w = s.wallptr; w < s.wallptr + s.wallnum; ++w)
        {
            if (owners[w] != -1)
                return fail("Two sectors own the same wall.");
            owners[w] = i;
        }
    }
    for (int i = 0; i < int(walls.size()); ++i)
    {
        auto const &w = walls[i];
        if (owners[i] < 0 || unsigned(w.point2) >= walls.size() || owners[w.point2] != owners[i] ||
            ++incoming[w.point2] > 1 || samePoint(w.xy, walls[w.point2].xy))
            return fail("A wall does not belong to a closed sector loop.");
        if (w.nextwall < -1 || w.nextsector < -1 ||
            w.nextwall >= int(walls.size()) || w.nextsector >= int(sectors.size()) ||
            ((w.nextwall == -1) != (w.nextsector == -1)))
            return fail("A portal points to a missing wall or sector.");
        if (!usablePoint(w.xy) || !tileValid(w.picnum) || (w.overpicnum != -1 && !tileValid(w.overpicnum)))
            return fail("A wall uses an out-of-range position or tile.");
        if ((w.cstat & 48) && w.overpicnum < 0)
            return fail("A masked or one-way wall needs an overlay tile.");
        if (w.cstat & ~0x3ff)
            return fail("A wall uses flags from an extended map format.");
    }
    for (auto const &s : sprites)
    {
        // Retail maps can park dormant sprites far outside the editable
        // grid. Preserve their v7 coordinates when opening and saving.
        if (unsigned(s.sectnum) >= sectors.size() || unsigned(s.statnum) >= MAXSTATUS ||
            !tileValid(s.picnum) || unsigned(s.ang) >= 2048)
            return fail("A sprite uses an invalid sector, angle, status or tile.");
        if ((s.cstat & ~0x83ff) || (s.cstat & 48) == 48)
            return fail("A sprite uses flags from an extended map format.");
    }
    if (draft && startSector == -1)
        return true;
    if (unsigned(startSector) >= sectors.size() || !usablePoint({start.x, start.y}) ||
        unsigned(angle) >= 2048 || !contains(*this, startSector, {start.x, start.y}))
        return fail("Use Player Start to place Duke inside a sector.");
    return true;
}

bool Board::validateGeometryChange(const Board &before, std::string &error) const
{
    if (!SameTopology(*this, before))
        return true;

    std::vector<bool> visited(walls.size());
    for (int startWall = 0; startWall < int(walls.size()); ++startWall)
    {
        if (visited[startWall])
            continue;
        std::vector<vec2_t> loop;
        int64_t previousArea = 0;
        bool changed = false;
        int w = startWall;
        do
        {
            visited[w] = true;
            auto const next = walls[w].point2;
            loop.push_back(walls[w].xy);
            changed |= !samePoint(walls[w].xy, before.walls[w].xy);
            previousArea += cross({}, before.walls[w].xy, before.walls[next].xy);
            w = next;
        }
        while (w != startWall);
        if (!changed)
            continue;
        int64_t area;
        if (!validPolygon(loop, area) || ((area < 0) != (previousArea < 0)))
        {
            error = "The move would cross or reverse a sector boundary.";
            return false;
        }
    }
    for (int i = 0; i < int(sprites.size()); ++i)
    {
        auto const &sprite = sprites[i], &original = before.sprites[i];
        if (!contains(*this, sprite.sectnum, sprite.xy) && contains(before, original.sectnum, original.xy))
        {
            error = "The move would leave a sprite outside its sector.";
            return false;
        }
    }
    return true;
}

void Board::install() const
{
    numsectors = sectors.size();
    numwalls = walls.size();
#ifdef NEW_MAP_FORMAT
    for (int i = 0; i < numsectors; ++i)
    {
        Bmemcpy(reinterpret_cast<char *>(&sector[i]), &sectors[i], sizeof(usectortypev7));
        inplace_vx_from_v7_sector(&sector[i]);
        inplace_vx_tweak_sector(&sector[i], 0);
    }
    for (int i = 0; i < numwalls; ++i)
    {
        Bmemcpy(reinterpret_cast<char *>(&wall[i]), &walls[i], sizeof(uwalltypev7));
        inplace_vx_from_v7_wall(&wall[i]);
        inplace_vx_tweak_wall(&wall[i], 0);
    }
#else
    if (numsectors) Bmemcpy(static_cast<void *>(sector), sectors.data(), sizeof(sectortypev7) * numsectors);
    if (numwalls) Bmemcpy(static_cast<void *>(wall), walls.data(), sizeof(walltypev7) * numwalls);
#endif
    initspritelists();
    for (auto const &entity : sprites)
    {
        int const index = insertsprite(entity.sectnum, entity.statnum);
        Bmemcpy(static_cast<void *>(&sprite[index]), &entity, sizeof(entity));
    }
#ifdef YAX_ENABLE
    yax_update(1);
#endif
    Bmemset(spriteext, 0, sizeof(spriteext_t) * MAXSPRITES);
}

Board NewBoard()
{
    Board board;
    board.addRoom({{-2048, -1536}, {2048, -1536}, {2048, 1536}, {-2048, 1536}});
    board.setStart({0, 0}, 0);
    return board;
}

bool SameTopology(const Board &a, const Board &b)
{
    if (a.sectors.size() != b.sectors.size() || a.walls.size() != b.walls.size() || a.sprites.size() != b.sprites.size())
        return false;
    for (size_t i = 0; i < a.sectors.size(); ++i)
        if (a.sectors[i].wallptr != b.sectors[i].wallptr || a.sectors[i].wallnum != b.sectors[i].wallnum)
            return false;
    for (size_t i = 0; i < a.walls.size(); ++i)
        if (a.walls[i].point2 != b.walls[i].point2 || a.walls[i].nextwall != b.walls[i].nextwall ||
            a.walls[i].nextsector != b.walls[i].nextsector)
            return false;
    return true;
}

template <typename T>
bool mergeRecords(T const &previous, T const &edited, T &current)
{
    if (!Bmemcmp(&edited, &previous, sizeof(T)))
        return true;
    if (Bmemcmp(&current, &previous, sizeof(T)))
        return !Bmemcmp(&current, &edited, sizeof(T));
    current = edited;
    return true;
}

bool MergeBoard(const Board &base, const Board &edited, const Board &current, Board &merged)
{
    if (!SameTopology(base, edited) || !SameTopology(base, current))
        return false;
    merged = current;
    auto mergeList = [](auto const &base, auto const &edit, auto &out) {
        for (size_t i = 0; i < base.size(); ++i)
            if (!mergeRecords(base[i], edit[i], out[i]))
                return false;
        return true;
    };
    return mergeList(base.sectors, edited.sectors, merged.sectors) &&
           mergeList(base.walls, edited.walls, merged.walls) &&
           mergeList(base.sprites, edited.sprites, merged.sprites) &&
           mergeRecords(base.start, edited.start, merged.start) &&
           mergeRecords(base.angle, edited.angle, merged.angle) &&
           mergeRecords(base.startSector, edited.startSector, merged.startSector);
}

std::vector<uint8_t> EncodeBoard(const Board &board)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(26 + board.sectors.size() * sizeof(usectortypev7)
                     + board.walls.size() * sizeof(uwalltypev7) + board.sprites.size() * sizeof(uspritetypev7));
    append(bytes, static_cast<int32_t>(B_LITTLE32(7)));
    append(bytes, static_cast<int32_t>(B_LITTLE32(board.start.x)));
    append(bytes, static_cast<int32_t>(B_LITTLE32(board.start.y)));
    append(bytes, static_cast<int32_t>(B_LITTLE32(board.start.z)));
    append(bytes, static_cast<int16_t>(B_LITTLE16(board.angle)));
    append(bytes, static_cast<int16_t>(B_LITTLE16(board.startSector)));
    appendList(bytes, board.sectors);
    appendList(bytes, board.walls);
    appendList(bytes, board.sprites);
    return bytes;
}

bool DecodeBoard(const std::vector<uint8_t> &bytes, Board &board, int tileLimit, std::string &error, bool draft)
{
    Input input{bytes};
    if (bytes.size() > maxBoardBytes)
    {
        error = "Could not read a classic map at that path.";
        return false;
    }

    uint32_t version;
    if (!input.read(version) || B_LITTLE32(version) != 7)
    {
        error = "Choose a Duke3D version 7 .MAP file.";
        return false;
    }

    Board candidate;
    if (!input.read(candidate.start) || !input.read(candidate.angle) || !input.read(candidate.startSector) ||
        !input.readList(candidate.sectors, MAXSECTORSV7) || !input.readList(candidate.walls, MAXWALLSV7) ||
        !input.readList(candidate.sprites, MAXSPRITESV7) || input.offset != input.data.size())
    {
        error = "The file has invalid record lengths or exceeds the classic map limits.";
        return false;
    }
    candidate.start.x = B_LITTLE32(candidate.start.x);
    candidate.start.y = B_LITTLE32(candidate.start.y);
    candidate.start.z = B_LITTLE32(candidate.start.z);
    candidate.angle = B_LITTLE16(candidate.angle);
    candidate.startSector = B_LITTLE16(candidate.startSector);
    for (auto &s : candidate.sectors) fixEndian(s);
    for (auto &w : candidate.walls) fixEndian(w);
    for (auto &s : candidate.sprites) fixEndian(s);
    if (!candidate.validate(tileLimit, error, draft))
        return false;
    board = std::move(candidate);
    return true;
}

bool ReadBoard(const std::string &path, Board &board, int tileLimit, std::string &error)
{
    std::vector<uint8_t> bytes;
    if (!readBytes(path, bytes))
    {
        error = "Could not read a classic map at that path.";
        return false;
    }
    return DecodeBoard(bytes, board, tileLimit, error, false);
}

bool WriteBoard(const std::string &path, const Board &board, int tileLimit, std::string &error)
{
    if (!board.validate(tileLimit, error))
        return false;
    auto const separator = path.find_last_of("/\\");
    auto const temp = temporaryMap(separator == std::string::npos ? "." : path.substr(0, separator + 1));
    if (temp.empty())
    {
        error = "Could not create a temporary file in the destination folder.";
        return false;
    }
    auto const bytes = EncodeBoard(board);
    auto const output = buildvfs_fopen_write(temp.c_str());
    bool written = false;
    if (output)
    {
        bool const copied = buildvfs_fwrite(bytes.data(), 1, bytes.size(), output) == bytes.size();
        written = buildvfs_fclose(output) == 0 && copied;
    }
#ifdef _WIN32
    bool const published = written && MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
    bool const published = written && rename(temp.c_str(), path.c_str()) == 0;
#endif
    if (published)
        return true;
    remove(temp.c_str());
    error = "The map could not be saved. Check the destination and available space.";
    return false;
}

std::string CreatePlaytestPath()
{
#ifdef _WIN32
    char directory[MAX_PATH];
    auto const length = GetTempPathA(MAX_PATH, directory);
    return length > 0 && length < MAX_PATH ? temporaryMap(directory) : std::string{};
#else
    auto const directory = getenv("TMPDIR");
    return temporaryMap(directory && *directory ? directory : "/tmp");
#endif
}

} // namespace DukeEditor
