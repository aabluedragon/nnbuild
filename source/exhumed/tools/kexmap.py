#!/usr/bin/env python3
"""kexmap -- play the Nightdive PowerSlave Exhumed levels in VibeBuild32.

The 2022 Nightdive remaster is a Kex engine game: its own map format, its own
PNG art, none of the original Build data. But its levels are close cousins of
Build levels -- convex sectors, exactly one floor and one ceiling each,
always-vertical walls, portals between sectors -- so the geometry converts.

    ./kexmap.py "/path/to/PowerSlave Exhumed" /path/to/your/powerslave/dir

writes one Build .map per Kex level plus an exhumed.def that points the
renderer at the remaster's own PNG textures. Then:

    ./vibebuild32 -gm exhumed -j /path/to/your/powerslave/dir \
        -kex "/path/to/PowerSlave Exhumed" -map KEX_KARNAK.MAP

The .kpf is a zip, which the Build engine mounts natively, so the textures are
read straight out of the remaster's own archive -- nothing is copied or
converted, and no palette reduction happens.

You still need the Build-engine PowerSlave data (STUFF.DAT) for the HUD,
weapons and sounds; this brings the remaster's *levels*, not a whole game.

Pass --testmaps after the usual paths to include the remaster's focused test
maps (for example KEX_TEST_PLATFORMS.MAP) in addition to the retail campaign.

WHAT CONVERTS
    Sector and wall geometry, sloped floors and ceilings, horizontal and
    vertical (TROR) portals, wall/floor/ceiling textures, baked vertex
    lighting, the player start, campaign actors, events and movers. Geometry
    that exceeds one Build sector's slope or lighting limits is split into
    compatible pieces and tied back together by the runtime side tables.

WHAT IS APPROXIMATED
    This is a compatibility layer, not a KEX renderer inside Build. KEX's
    arbitrary wall mosaics, per-vertex light and multi-link portals are baked,
    split or selected at runtime using Build-compatible geometry. The retail
    census currently drops no valid source sector, actor, texture or used event
    type; kexverify.py audits every generated map and the runtime side tables.

THE FORMAT
    Reverse-engineered against the .dmap/.map pairs Nightdive ships in
    docs/examples/ -- every field was checked against the matching text source.
    See PSM.__init__ below for the layout.
"""
import math, os, re, struct, sys, zipfile
import kexmenu

# ---------------------------------------------------------------------------
# Reading the Kex map
# ---------------------------------------------------------------------------
# 
# Reverse-engineered against the .dmap/.map pairs Nightdive ships in
# docs/examples/: every field below was checked against the matching text source.
# 
#   header   'PSM\\x01' + 8 x u32:
#              ntex, nvert, nsector, nface, ntile, nuvquad, nevent, nactor
#   strings  u16-length-prefixed: the sky texture then the texture table. The
#            exact entry count is ambiguous (an empty sky and the empty
#            "no texture" slot are indistinguishable), so the caller locates the
#            vertex array by validating the face table that follows -- see
#            locate().
#   verts    nvert x 16: float x, y, z + RGBA (baked vertex lighting)
#   sectors  nsector x 20: u16 faceStart, u16 wallFaceEnd, ...
#            faces[faceStart..wallFaceEnd] are the walls; wallFaceEnd+1 is the
#            ceiling face and wallFaceEnd+2 the floor face.
#   faces    nface x 32: i16 tileStart, i16 tileEnd (-1 = untextured),
#            u16 vertexStart (4 corners), i16 sectorLink (-1 = solid),
#            float angle, float nx, ny, nz, u16 flags,
#            i16 moveGroup (-1 = static),
#            i16 tileVertStart, i16 tileVertEnd
#   tiles    ntile x 16: u8[4] local geometry-vertex indices,
#            i16[4] UV-pool indices, u16 textureIndex, u16 pad
#   uvquads  nuvquad x 32: four float (u, v) texture coordinates. Each
#            tile's four i16 indices address the flattened coordinate pool.
#   events   nevent x 8: u16 eventType, u16 sector, i16 eventTag, i16 params
#            eventType uses the remaster's SLAVE_EVENT_* numbering and is
#            translated through KEX_EVENTS/_resolve_events below.
#   actors   nactor x 20, at the very end of the file:
#            u16 type, i16 sector, i16 x, i16 y, i16 z, i16 tag,
#            i16 param1, i16 param2, float storedAngleDegrees


class Face:
    __slots__=('tileStart','tileEnd','vertStart','link','angle','n','flags',
               'moveGroup','tileVertStart','tileVertEnd')
class Sector:
    __slots__=('faceStart','wallEnd','raw')
class Actor:
    __slots__=('type','sector','x','y','z','tag','p1','p2','ang','raw')

class PSM:
    def __init__(self, data, vstart=None):
        d=self.data=data
        if d[:4]!=b'PSM\x01': raise ValueError('not a Kex PSM map')
        (self.ntex,self.nvert,self.nsector,self.nface,
         self.ntile,self.nuvquad,self.nevent,self.nactor)=struct.unpack_from('<8I',d,4)
        # Compatibility aliases for older audit scripts. The sixth field is a
        # UV pool, not spatial/gameplay zones.
        self.nzone = self.nuvquad
        o=36; strs=[]
        while len(strs) < self.ntex+2:
            ln=struct.unpack_from('<H',d,o)[0]; o+=2
            strs.append(d[o:o+ln].decode('latin-1')); o+=ln
        self.sky=strs[0]
        self.textures=[s for s in strs[1:]]
        if vstart is None: vstart=o
        self.vstart=vstart
        o=vstart
        self.verts=[]
        for _ in range(self.nvert):
            x,y,z=struct.unpack_from('<3f',d,o)
            self.verts.append((x,y,z,(d[o+12],d[o+13],d[o+14],d[o+15]))); o+=16
        self.secOff=o
        self.sectors=[]
        for i in range(self.nsector):
            s=Sector(); p=self.secOff+i*20
            s.faceStart,s.wallEnd=struct.unpack_from('<2H',d,p)
            s.raw=struct.unpack_from('<10H',d,p)
            self.sectors.append(s)
        self.faceOff=self.secOff+self.nsector*20
        self.faces=[]
        for i in range(self.nface):
            f=Face(); p=self.faceOff+i*32
            f.tileStart,f.tileEnd=struct.unpack_from('<2h',d,p)
            f.vertStart=struct.unpack_from('<H',d,p+4)[0]
            f.link=struct.unpack_from('<h',d,p+6)[0]
            f.angle=struct.unpack_from('<f',d,p+8)[0]
            f.n=struct.unpack_from('<3f',d,p+12)
            f.flags=struct.unpack_from('<H',d,p+24)[0]
            f.moveGroup=struct.unpack_from('<h',d,p+26)[0]
            f.tileVertStart,f.tileVertEnd=struct.unpack_from('<2h',d,p+28)
            self.faces.append(f)
        self.tileOff=self.faceOff+self.nface*32
        self.tiles=[]
        self.tileCorners=[]
        for i in range(self.ntile):
            p=self.tileOff+i*16
            self.tileCorners.append(tuple(d[p:p+4]))
            self.tiles.append((struct.unpack_from('<4h',d,p+4),
                               struct.unpack_from('<H',d,p+12)[0]))
        # Four UV pairs per 32-byte block, then events (8), then the actors at
        # the end. The retail loader reads eight floats per block and its mesh
        # builder indexes this storage as float2[4*nuvquad] from each tile's
        # four i16 indices. The shipped shader samples those values unchanged.
        # Both sizes were solved from the gap between the tile array and the
        # actor array across all fifteen sample maps: gap = nuvquad*32 +
        # nevent*8, exact for every one of them.
        self.uvOff = self.tileOff + self.ntile*16
        self.zoneOff = self.uvOff       # compatibility alias; see nzone above
        self.uvs = [struct.unpack_from('<2f', d, self.uvOff + i*8)
                    for i in range(self.nuvquad*4)]
        self.eventOff = self.uvOff + self.nuvquad*32
        self.events = []
        for i in range(self.nevent):
            q = self.eventOff + i*8
            if q + 8 > len(d): break
            etype, esect = struct.unpack_from('<2H', d, q)
            etag, eparam = struct.unpack_from('<2h', d, q+4)
            self.events.append((etype, esect, etag, eparam))

        actOff=len(d)-self.nactor*20
        self.actors=[]
        for i in range(self.nactor):
            a=Actor(); p=actOff+i*20
            u=struct.unpack_from('<10h',d,p)
            a.type=u[0]&0xffff; a.sector=u[1]; a.x,a.y,a.z=u[2],u[3],u[4]
            a.tag=u[5]; a.p1=u[6]; a.p2=u[7]; a.raw=u
            # Facing, as a float at offset 16 -- in degrees, and rotated:
            # the text source's angle (radians) is radians(90 - stored).
            # Checked against every sample map that has a turned actor.
            a.ang=struct.unpack_from('<f', d, p+16)[0]
            self.actors.append(a)

    def valid(self):
        """The face table is self-checking: tile ranges must partition
        0..ntile-1 in order and every link must name a real sector."""
        last=-1
        for f in self.faces:
            if not (-1 <= f.link < self.nsector): return False
            if f.moveGroup < -1: return False
            if f.vertStart+3 >= self.nvert: return False
            if f.tileStart==-1:
                if f.tileEnd!=-1: return False
            else:
                if f.tileStart!=last+1 or f.tileEnd<f.tileStart or f.tileEnd>=self.ntile:
                    return False
                last=f.tileEnd
        if last != self.ntile-1 or self.eventOff+self.nevent*8 > len(self.data)-self.nactor*20:
            return False
        for corners, (uvs, tex) in zip(self.tileCorners, self.tiles):
            if tex >= len(self.textures): return False
            for corner, uv in zip(corners, uvs):
                if corner == 255 or uv == -1:
                    if corner != 255 or uv != -1: return False
                elif not (0 <= uv < len(self.uvs)):
                    return False
        return True

def locate(data):
    """Parse with the correct vertex-array start, found by validating."""
    probe=PSM(data)
    base=probe.vstart
    for delta in (0,-2,2,-4,4,-6,6):
        try: m=PSM(data, base+delta)
        except Exception: continue
        if m.valid(): return m
    raise ValueError('could not locate the face table')

def load(path):
    return locate(open(path, "rb").read())

# ---------------------------------------------------------------------------
# Kex geometry -> Build sectors and walls
# ---------------------------------------------------------------------------

SCALE = 16
AT_PLAYER = 0
STEEP_HEINUM = 4096      # 45 degrees: slopes at least this steep get relative texture alignment

# Converted maps are written with this prefix because their names would
# otherwise collide with the remaster's own maps/*.MAP inside the mounted .kpf,
# and the engine resolves the name into the archive instead of to the converted
# file on disk. That collision is what produced the band of RGB noise along the
# top of the screen: byte-identical map data rendered clean under a unique name
# and broken under TOMB.MAP, every time.
KEX_MAP_PREFIX = "KEX_"

# Kex and Build PowerSlave are the same spaces at different scales, and the
# player that walks these levels is Build PowerSlave's: eye height 14080 z units
# (880 world), collision radius 232, step clearance 5120 z (320 world). Kex
# authors a corridor at 256 units wide and 512 tall, so at 1:1 the player's eye
# sits ABOVE the ceiling and its radius nearly fills the corridor -- which is
# what made the first level render as a smear of wall.
#
# The factor is measured against the original game rather than guessed. Across
# Exhumed's own LEV1 and the converted TOMB:
#     median sector height   3200 vs  512  ->  6.25
#     median wall length     1032 vs  181  ->  5.7
# Two independent measures of the same ratio, so 6.
WORLD = 6

# Build shows xrepeat*8 texels across a wall, so xrepeat follows wall LENGTH.
# The compiled remaster maps one 64-pixel texture repeat over 128 Kex units.
# This is not inferred from editor cells: the sixth PSM field is the UV pool,
# the retail mesh builder copies it directly into vertices, and the shipped
# shader samples it unchanged. Across 1,335,110 usable retail tile edges every
# map's median is exactly 128 units/repeat.
#     xrepeat = length / (8 * units_per_texel)
KEX_TEXTURE_REPEAT = 128.0       # Kex source units per 64-pixel texture
KEX_SOURCE_UNITS_PER_TEXEL = KEX_TEXTURE_REPEAT / 64.0
KEX_UNITS_PER_TEXEL = WORLD * KEX_SOURCE_UNITS_PER_TEXEL

# yrepeat is a vertical scale and does not depend on the wall's length. The
# original game pairs yrepeat 7 with its 20.4 units per texel.
# Build shows xrepeat*8 texels across a wall, and vertically one texel every
# 128/yrepeat world units. That 128 is measured rather than assumed: across the
# 13150 walls in the six levels that ship in STUFF.DAT, yrepeat * (world units
# per texel across) has its mode at exactly 128 (5735 walls) and a median of
# 129.6, which is what square texels give. A Kex texel covers two source units
# and a source unit is WORLD Build world units, so yrepeat wants to be
# 128/(2*WORLD) = 10.67.
KEX_YREPEAT = max(1, min(255, int(round(128.0 / KEX_UNITS_PER_TEXEL))))

def _edges(m, si):
    """The sector's 2D edges, chained into a closed loop.

    Kex stores a sector's wall faces in no particular order, so the loop has to
    be rebuilt by walking end-point to start-point, and faces that share an edge
    -- a portal plus the solid bands above and below it -- collapse into one.

    The catch is that a face's first two vertices are NOT consistently wound.
    One side of MINES sector 16 is built from ten faces along y=1152, and the
    same span (544,1152)-(640,1152) appears in one face left-to-right and in the
    next right-to-left. Keying on the directed pair made those two different
    edges, the walk dead-ended, and the whole sector was thrown away. That is
    what was costing 106 of MINES' 605 sectors and 230 across the retail maps.

    So edges are keyed by the UNDIRECTED segment and the ring is walked over a
    point adjacency, which picks the direction. A sector whose walls make more
    than one ring is still dropped: Build can hold inner loops, but the rest of
    this converter assumes one ring per sector.
    """
    s = m.sectors[si]
    # Gather projected face spans first.  A Kex wall is often a mosaic: one
    # long solid face can cover the whole side while smaller portal/Toggle
    # faces occupy sub-spans of that same line.  Treating each original span
    # as one graph edge makes the short spans look like chords hanging off a
    # valid ring, so the pruning below discards them.  PASS sector 80 is the
    # canonical failure: three authored bomb-wall openings sit inside one
    # longer x=-1408 face and none survived conversion.
    raw = []
    for fi in range(s.faceStart, min(s.wallEnd+1, m.nface)):
        f = m.faces[fi]
        # And the first two vertices are not always the horizontal edge: on some
        # faces they are the vertical one, so they project to a single 2D point.
        # Skipping those as degenerate dropped a whole side of the ring, which
        # left 90 of MINES' sectors as an open chain of three segments. A wall
        # face is a vertical quad, so its four corners give exactly two distinct
        # 2D points -- take the widest-apart pair and the direction sorts itself
        # out below.
        # To a tenth of a unit: the data has corners at -3456.01 beside
        # -3456.0 (SANCTUAR) and -1919.99 beside -1920 (PEAK), and read to a
        # hundredth those are two points, which turns a closed ring into a
        # chain with two loose ends.
        pts = []
        for k in range(4):
            v = m.verts[f.vertStart + k]
            pt = (round(v[0], 1), round(v[1], 1))
            if pt not in pts:
                pts.append(pt)
        if len(pts) < 2:
            continue                      # genuinely degenerate in 2D
        if len(pts) == 2:
            pa, pb = pts
        else:
            pa, pb = max(((p, q) for i, p in enumerate(pts) for q in pts[i+1:]),
                         key=lambda pq: (pq[0][0]-pq[1][0])**2 + (pq[0][1]-pq[1][1])**2)
        raw.append((pa, pb, fi))
    if not raw:
        return []

    # Split every span at every collinear face endpoint.  The resulting atomic
    # edge carries all source faces that cover it, preserving both the outer
    # ring and the independent vertical bands on that ring.  Endpoints have
    # already been rounded to a tenth above; the small scale-relative epsilon
    # is only for binary floating-point representation of those tenths.
    allpoints = {p for a, b, _fi in raw for p in (a, b)}

    def on_segment(a, b, p):
        dx, dy = b[0] - a[0], b[1] - a[1]
        px, py = p[0] - a[0], p[1] - a[1]
        eps = 1e-7 * max(1.0, abs(dx) + abs(dy))
        return (abs(dx * py - dy * px) <= eps
                and min(a[0], b[0]) - eps <= p[0] <= max(a[0], b[0]) + eps
                and min(a[1], b[1]) - eps <= p[1] <= max(a[1], b[1]) + eps)

    groups = {}; order = []
    for pa, pb, fi in raw:
        dx, dy = pb[0] - pa[0], pb[1] - pa[1]
        cuts = [p for p in allpoints if on_segment(pa, pb, p)]
        cuts.sort(key=lambda p: (p[0] - pa[0]) * dx + (p[1] - pa[1]) * dy)
        for a, b in zip(cuts, cuts[1:]):
            if a == b:
                continue
            key = (a, b) if a <= b else (b, a)
            if key not in groups:
                groups[key] = []; order.append(key)
            if fi not in groups[key]:
                groups[key].append(fi)
    if not order:
        return []

    adj = {}
    for key in order:
        adj.setdefault(key[0], []).append(key)
        adj.setdefault(key[1], []).append(key)

    # A closed ring visits every point exactly twice. Two points visited once is
    # an open chain, and that is common rather than broken: MINES sector 17 is a
    # rectangle whose fourth side simply has no wall face in the source, because
    # nothing there is ever drawn. 90 of MINES' sectors are shaped like that.
    # Close the chain with a solid wall and keep the sector.
    loose = [p for p, v in adj.items() if len(v) == 1]
    if len(loose) == 2:
        key = (loose[0], loose[1]) if loose[0] <= loose[1] else (loose[1], loose[0])
        if key not in groups:
            groups[key] = []          # no face: solid, textured from the floor
            order.append(key)
            adj.setdefault(key[0], []).append(key)
            adj.setdefault(key[1], []).append(key)
    # What is left over after that is a ring with something hanging off it:
    # a chain to nowhere (a point visited once), or a chord across the room --
    # a partition wall, a doubled face -- meeting the ring at two points
    # visited three times. Build has no free-standing walls, so the chord is
    # let go and the ring kept; the alternative was losing the room. Leaves
    # are pruned first, then chords between over-visited points, keeping the
    # graph in one piece, until every point is visited exactly twice.
    def drop(key):
        order.remove(key)
        for p in key:
            adj[p].remove(key)
            if not adj[p]:
                del adj[p]
    changed = True
    while changed:
        changed = False
        for p, v in list(adj.items()):
            if len(v) == 1 and p in adj:
                drop(v[0]); changed = True
    def connected():
        if not adj:
            return False
        start = next(iter(adj)); seen = {start}; stack = [start]
        while stack:
            p = stack.pop()
            for k in adj[p]:
                q = k[1] if k[0] == p else k[0]
                if q not in seen:
                    seen.add(q); stack.append(q)
        return len(seen) == len(adj)
    def seglen(k):
        return (k[0][0]-k[1][0])**2 + (k[0][1]-k[1][1])**2
    while any(len(v) > 2 for v in adj.values()):
        chords = [k for k in order if len(adj[k[0]]) > 2 and len(adj[k[1]]) > 2]
        if not chords:
            # A chord ending on a plain ring point: take the shortest edge at
            # an over-visited point that leaves the rest in one piece, then
            # prune whatever it left dangling.
            chords = [k for k in order if len(adj[k[0]]) > 2 or len(adj[k[1]]) > 2]
        for k in sorted(chords, key=seglen):
            saved = {p: list(v) for p, v in adj.items()}; saved_order = list(order)
            drop(k)
            if connected():
                break
            adj.clear(); adj.update(saved); order[:] = saved_order
        else:
            return []
        changed = True
        while changed:
            changed = False
            for p, v in list(adj.items()):
                if len(v) == 1 and p in adj:
                    drop(v[0]); changed = True
    if not order or any(len(v) != 2 for v in adj.values()):
        return []

    used = set(); loop = []
    first = order[0]
    cur, pt = first, first[1]
    used.add(first); loop.append((first[0], first[1]))
    while True:
        nxt = None
        for cand in adj[pt]:
            if cand not in used:
                nxt = cand; break
        if nxt is None:
            break
        used.add(nxt)
        other = nxt[1] if nxt[0] == pt else nxt[0]
        loop.append((pt, other))
        pt = other
    if len(loop) != len(order) or pt != first[0]:
        return []                          # several rings, or it never closed
    # A ring that crosses itself is worse than no ring: it would be drawn and
    # walked as a sector. Only a pruned ring can come out that way.
    def crosses(a, b, c, d):
        def side(p, q, r):
            return (q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0])
        return (side(a, b, c) * side(a, b, d) < 0) and (side(c, d, a) * side(c, d, b) < 0)
    n = len(loop)
    for i in range(n):
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue
            if crosses(loop[i][0], loop[i][1], loop[j][0], loop[j][1]):
                return []
    return [((a[0], a[1], b[0], b[1]),
             groups[(a, b) if a <= b else (b, a)]) for a, b in loop]

def _plane_build(m, f):
    """A Kex floor/ceiling face as a Build-space plane: (z0, gx, gy).

    z is taken at the face's first vertex; gx/gy are the gradient in Build z
    units per world unit. Kex has z up and this conversion flips y, so
        bz = -kz * 16 * WORLD,  bx = kx * WORLD,  by = -ky * WORLD
    and the plane's Kex gradient (-nx/nz, -ny/nz) becomes (16*nx/nz, -16*ny/nz).
    """
    nx, ny, nz = f.n
    if abs(nz) < 1e-6:
        return None
    gx = 16.0 * nx / nz
    gy = -16.0 * ny / nz
    vx, vy, vz = m.verts[f.vertStart][0], m.verts[f.vertStart][1], m.verts[f.vertStart][2]
    return (-vz * SCALE * WORLD, vx * WORLD, -vy * WORLD, gx, gy)


def _slope(plane, wx, wy, dx, dy):
    """Build heinum and the z at the first wall, for a plane.

    Build tilts a sector about its FIRST WALL: the height changes with the
    perpendicular distance from that wall, positive to its left, as
    z = z0 + heinum*perpDist/256 (from getflorzofslopeptr). So only the
    component of the gradient perpendicular to that wall survives -- which is
    why the wall loop is rotated to line up with the slope before this runs.
    """
    import math as _m
    z0, px, py, gx, gy = plane
    L = _m.hypot(dx, dy)
    if L < 1e-6:
        return int(round(z0 + gx*(wx-px) + gy*(wy-py))), 0
    nxu, nyu = -dy / L, dx / L          # left normal of the wall direction
    heinum = int(round(256.0 * (gx*nxu + gy*nyu)))
    zwall = z0 + gx*(wx-px) + gy*(wy-py)
    return int(round(zwall)), max(-32768, min(32767, heinum))


def _slope_error(m, f, z0, heinum, wx, wy, dx, dy):
    """Largest corner error, in Build z units, if this face is emitted sloped.

    Build tilts about ONE wall, so a plane whose gradient is not perpendicular
    to any of the sector's walls cannot be represented -- the component along
    the wall is simply lost. Rotating the loop fixes most of them exactly; this
    measures what is left so the rest can fall back to flat rather than being
    emitted wildly mis-tilted.
    """
    import math as _m
    i = int(_m.hypot(dx, dy)) << 5
    if i == 0:
        return 0.0
    worst = 0.0
    for k in range(4):
        idx = f.vertStart + k
        if idx >= len(m.verts): break
        v = m.verts[idx]
        ax, ay = v[0]*WORLD, -v[1]*WORLD
        j = int(dx*(ay-wy) - dy*(ax-wx)) >> 3
        got = z0 + (heinum * j) // i
        worst = max(worst, abs(got - (-v[2]*SCALE*WORLD)))
    return worst


# A slope counts as exact when every ring vertex lands within this many Build z
# units (128 = 8 world units). Past that, whichever of "best tilt" and "flat at
# the mean" lies less is used -- see _surface().
KEX_SLOPE_TOLERANCE = 128

# How many times one sector may be cut in half along a contour. Five levels
# keep the steep COLONY_SAT crater walls visibly closer to their source planes
# while every retail/test map still fits Build's extended sector/wall limits.
KEX_SPLIT_DEPTH = 5


def _plane_z(pl, x, y):
    """Build z of a plane from _plane_build at Build world (x, y)."""
    z0, px, py, gx, gy = pl
    return z0 + gx*(x-px) + gy*(y-py)


def _sloped(pl):
    return bool(pl) and (abs(pl[3]) > 1e-3 or abs(pl[4]) > 1e-3)


def _ring_error(pl, ring, z0, heinum, wx, wy, dx, dy):
    """Largest error, in Build z, of the tilt (z0, heinum) about the first wall
    (wx, wy)->(wx+dx, wy+dy), measured at the sector's OWN ring vertices against
    the plane. Mirrors getflorzofslope's integer arithmetic.

    This replaces measuring at the face's four stored vertices, which are the
    bounding quad of the rotated plane and lie OUTSIDE any sector that is not a
    rectangle aligned with its slope. Measured there a triangle's ceiling looked
    wrong by more than it could ever be inside the triangle."""
    import math as _m
    i = int(_m.hypot(dx, dy)) << 5
    if i == 0:
        return 0.0
    worst = 0.0
    for ax, ay in ring:
        j = int(dx*(ay-wy) - dy*(ax-wx)) >> 3
        got = z0 + (heinum * j) // i
        worst = max(worst, abs(got - _plane_z(pl, ax, ay)))
    return worst


def _surface_errors(edges, cp, fp, ring):
    """For each wall of `edges` (Kex coords, any orientation) as the first wall:
    (ceiling error, floor error) of the best tilt about it, at the ring."""
    import math as _m
    out = []
    for k, _f in edges:
        wx, wy = k[0]*WORLD, -k[1]*WORLD
        dx, dy = (k[2]-k[0])*WORLD, -(k[3]-k[1])*WORLD
        if _m.hypot(dx, dy) < 1e-6:
            out.append((1e9, 1e9)); continue
        errs = []
        for pl in (cp, fp):
            if not _sloped(pl):
                errs.append(0.0); continue
            z_, h_ = _slope(pl, wx, wy, dx, dy)
            errs.append(_ring_error(pl, ring, z_, h_, wx, wy, dx, dy))
        out.append(tuple(errs))
    return out


def _flat_error(pl, ring):
    """Largest error of emitting the plane flat at its mean over the ring."""
    if not _sloped(pl):
        return 0.0
    zs = [_plane_z(pl, x, y) for x, y in ring]
    mean = sum(zs) / len(zs)
    return max(abs(v - mean) for v in zs)


def _best_first_wall(edges, cp, fp):
    """Index of the wall to start the loop on: fewest surfaces left inexact,
    then the least total lie. Build tilts BOTH surfaces about one wall, so when
    floor and ceiling disagree this is a trade; _split_slopes() cuts such
    sectors so the trade is only ever over the lesser surface."""
    ring = [(k[0]*WORLD, -k[1]*WORLD) for k, _ in edges]
    flats = (_flat_error(cp, ring), _flat_error(fp, ring))
    best = None
    for i, errs in enumerate(_surface_errors(edges, cp, fp, ring)):
        fails = sum(1 for e in errs if e > KEX_SLOPE_TOLERANCE)
        key = (fails, sum(min(e, f) for e, f in zip(errs, flats)))
        if best is None or key < best[0]:
            best = (key, i, errs)
    return best[1], best[2]


class _Cut(object):
    """The seam a contour split leaves between the two halves of a sector: a
    wall with no face of its own that is a portal into the other half."""
    __slots__ = ('link',)
    def __init__(self, link):
        self.link = link


def _split_slopes(m, edge_cache, verbose=False):
    """Cut every sloped sector Build cannot tilt along contours of the slope.

    Build tilts a sector about its FIRST WALL only: z changes with the distance
    from that wall and is constant along it. So a plane is exact in a sector
    only when some wall is a level line of the plane. Most are -- a ramp's side
    walls run up the ramp, its ends run across it -- but 385 of the 3046 sloped
    surfaces in the retail maps have no such wall: triangles with the slope
    running diagonally, and the terrain of MAGMA and COLONY_SAT. Those used to
    be emitted FLAT, and flat at the highest corner of the face's bounding quad
    at that, which for a triangle is a point outside the sector: TOMB's
    entrance staircase had its ceiling at 2262 over a sector whose real ceiling
    runs 2048..2176, and the canyon floors of MAGMA were off by thousands.

    The cure is geometric. A line of constant height through the sector splits
    it in two, and in each half that line IS a wall -- a level one. Start each
    half's loop on it and the plane is exact. That is one extra sector and one
    portal wall per cut, and nothing else has to know: the seam is a plain
    portal, the halves take the source sector's faces and events, and the
    T-junctions a cut leaves on the neighbours are closed by
    _split_tjunctions() afterwards like any other.

    Cutting recurses, because one cut is not always enough. Two things send a
    half back onto the worklist: a floor and ceiling that slope in different
    directions (both tilt about the same first wall, so only one can be exact
    per piece), and terrain steeper than Build's slope itself -- heinum is 16
    bits, 128 z per xy unit, about 83 degrees, and COLONY_SAT's crater walls
    are authored at 416. Those can never be exact, so each cut must earn its
    keep: a piece is kept only when its worst surviving error is meaningfully
    below its parent's, and the recursion stops at KEX_SPLIT_DEPTH either way,
    which turns a cliff into a terrace of maximally-tilted strips rather than
    one flat ledge at the mean.

    Returns (src, pinned): src[i] is the SOURCE sector of piece i (the first
    m.nsector entries are the identity), pinned[i] True when piece i's edge
    list already starts on its cut and must not be rotated."""
    import math as _m
    src = list(range(m.nsector))
    pinned = {}
    ncut = 0

    def cost(edges, cp, fp, first):
        """Worst error _surface() will leave, tilting about wall `first`."""
        k0 = edges[first][0]
        wx, wy = k0[0]*WORLD, -k0[1]*WORLD
        dx, dy = (k0[2]-k0[0])*WORLD, -(k0[3]-k0[1])*WORLD
        if _m.hypot(dx, dy) < 1e-6:
            return 1e18
        ring = [(k[0]*WORLD, -k[1]*WORLD) for k, _ in edges]
        worst = 0.0
        for pl in (cp, fp):
            if not _sloped(pl):
                continue
            z_, h_ = _slope(pl, wx, wy, dx, dy)
            worst = max(worst, min(_ring_error(pl, ring, z_, h_, wx, wy, dx, dy),
                                   _flat_error(pl, ring)))
        return worst

    def cut(edges, pl, this_index, new_index):
        """One contour cut of `edges` along a level line of plane `pl`.
        Returns (ring_a, ring_b) with each ring starting on its seam and
        clockwise for Build, or None when no usable cut exists."""
        gx, gy = pl[3], pl[4]
        # Height along the gradient at each ring vertex, in Kex coordinates:
        # Build x = X*WORLD, Build y = -Y*WORLD, so gx*x + gy*y ~ gx*X - gy*Y.
        pts = [(k[0], k[1]) for k, _ in edges]
        t = [gx*X - gy*Y for X, Y in pts]
        tmin, tmax = min(t), max(t)
        if tmax - tmin < 1e-6:
            return None
        # Cut through the vertex nearest the middle of the range -- but only a
        # vertex genuinely inside it. Two of TOMB sector 39's floor corners
        # differ by one z unit, and "any vertex strictly between the extremes"
        # cut a sliver off between them. With no usable vertex the cut goes
        # through the middle of the range instead, entering and leaving through
        # edges; one more T-junction, but sound.
        mid = (tmin + tmax) / 2.0
        margin = 0.2 * (tmax - tmin)
        inner = [(abs(t[i] - mid), i) for i in range(len(t))
                 if t[i] - tmin > margin and tmax - t[i] > margin]
        tv = t[min(inner)[1]] if inner else mid
        eps = max(1e-9, 1e-6 * abs(tv))

        # Walk the ring once, handing each vertex to the side(s) it is on and
        # inserting the crossing point where an edge passes through the cut.
        # Every point remembers the edge it starts on, so the wall it begins
        # keeps that edge's faces; the one pair of consecutive points that lie
        # on the cut line bound the seam.
        A = []; B = []
        n = len(pts)
        for i in range(n):
            P, Pn = pts[i], pts[(i+1) % n]
            a, b = t[i] - tv, t[(i+1) % n] - tv
            sa = 0 if abs(a) <= eps else (1 if a > 0 else -1)
            sb = 0 if abs(b) <= eps else (1 if b > 0 else -1)
            if sa >= 0: A.append((P, i, sa == 0))
            if sa <= 0: B.append((P, i, sa == 0))
            if sa * sb < 0:
                u = a / (a - b)
                Q = (P[0] + (Pn[0]-P[0])*u, P[1] + (Pn[1]-P[1])*u)
                A.append((Q, i, True)); B.append((Q, i, True))
        if len(A) < 3 or len(B) < 3:
            return None

        def ring_of(points, other):
            out = []; cut_at = -1
            for j in range(len(points)):
                (x0, y0), e0, on0 = points[j]
                (x1, y1), _e1, on1 = points[(j+1) % len(points)]
                if on0 and on1 and cut_at < 0 and (x0, y0) != (x1, y1):
                    out.append(((x0, y0, x1, y1), [_Cut(other)])); cut_at = len(out)-1
                else:
                    out.append(((x0, y0, x1, y1), edges[e0][1]))
            if cut_at < 0:
                return None
            # Build wants the loop clockwise in its own (y-down) space, which
            # the main loop checks with a positive shoelace sum; settle it
            # here so the seam can be put first and stay first.
            q = [(k[0], -k[1]) for k, _ in out]
            area = sum(q[i][0]*q[(i+1) % len(q)][1] - q[(i+1) % len(q)][0]*q[i][1]
                       for i in range(len(q)))
            if area < 0:
                out = [((k[2], k[3], k[0], k[1]), f) for k, f in reversed(out)]
                cut_at = len(out) - 1 - cut_at
            return out[cut_at:] + out[:cut_at]

        ra = ring_of(A, new_index)
        rb = ring_of(B, this_index)
        if ra is None or rb is None:
            return None
        return ra, rb

    work = [(si, 0) for si in range(m.nsector)]
    while work:
        si, depth = work.pop()
        edges = edge_cache.get(si) or []
        if len(edges) < 3 or depth >= KEX_SPLIT_DEPTH:
            continue
        sec = m.sectors[src[si]]
        cf = m.faces[min(sec.wallEnd+1, m.nface-1)]
        ff = m.faces[min(sec.wallEnd+2, m.nface-1)]
        cp = _plane_build(m, cf) if cf.tileStart >= 0 else None
        fp = _plane_build(m, ff)
        if not (_sloped(cp) or _sloped(fp)):
            continue
        if pinned.get(si):
            now = cost(edges, cp, fp, 0)
        else:
            first, errs = _best_first_wall(edges, cp, fp)
            now = cost(edges, cp, fp, first)
        if now <= KEX_SLOPE_TOLERANCE:
            continue
        # The surface that would lie more, about the wall the piece will
        # actually use, is the one the cut follows.
        pl = fp
        if _sloped(cp) and _sloped(fp):
            ring = [(k[0]*WORLD, -k[1]*WORLD) for k, _ in edges]
            k0 = edges[0 if pinned.get(si) else first][0]
            wx, wy = k0[0]*WORLD, -k0[1]*WORLD
            dx, dy = (k0[2]-k0[0])*WORLD, -(k0[3]-k0[1])*WORLD
            worst = -1.0
            for p2 in (cp, fp):
                z_, h_ = _slope(p2, wx, wy, dx, dy)
                e = min(_ring_error(p2, ring, z_, h_, wx, wy, dx, dy),
                        _flat_error(p2, ring))
                if e > worst:
                    worst = e; pl = p2
        elif _sloped(cp):
            pl = cp
        new_index = len(src)
        halves = cut(edges, pl, si, new_index)
        if halves is None:
            continue
        ra, rb = halves
        # Each cut has to earn its keep: on terrain steeper than heinum can
        # tilt no cut is ever exact, and halving a flat lie forever is not
        # progress worth its sectors.
        after = max(cost(ra, cp, fp, 0), cost(rb, cp, fp, 0))
        if not (after <= KEX_SLOPE_TOLERANCE or after <= 0.66 * now):
            continue
        edge_cache[si] = ra
        edge_cache[new_index] = rb
        src.append(src[si])
        pinned[si] = True
        pinned[new_index] = True
        ncut += 1
        work.append((si, depth + 1))
        work.append((new_index, depth + 1))
    if verbose and ncut:
        print("  slopes: %d contour cuts so Build can tilt what Kex sloped" % ncut)
    _split_slopes.cut = cut
    _split_slopes.cost = cost
    return src, pinned


# A floor or ceiling whose lighting spans this many Build shade steps across
# the sector is cut in two along a contour of its light plane, each half at
# its own shade -- the widest spans first, until the level has this many
# sectors. Every cut costs a sector plus the T-junction walls it leaves on
# the neighbours; the game's tables are kMaxSectors 4096 / kMaxWalls 16384
# now (the DOS 1024 / 8192 used to be the budget), and a level past the
# version-7 limits is written as version 8.
KEX_LIGHT_CUT_STEPS = 10
# Wading depth for sectors flagged 16 (MARSH), Build's 94 hitag -- SectDepth = hitag << 8.
KEX_WADE_DEPTH = 10      # the DOS maps use 10
KEX_LIGHT_SECTOR_BUDGET = 1800
# A wall whose lighting changes this much along its length is subdivided so
# each piece can carry its own shade, into at most this many pieces.
KEX_LIGHT_WALL_STEPS = 8
KEX_LIGHT_WALL_PIECES = 3


def _split_light(m, edge_cache, src, pinned, verbose=False):
    """Cut sectors along contours of their baked light so Build's one shade
    per floor and ceiling can follow the remaster's gradients.

    The cut is _split_slopes()'s, driven by the light plane of the more
    graded of the two surfaces instead of the height plane. A light cut
    leaves its pieces UNPINNED: a sloped piece then re-picks its first wall
    with _best_first_wall(), and the slope's own contour seam is still a
    wall of each half, so the tilt stays exact. Returns {piece: (ceiling
    shade, floor shade)} evaluated at each piece's centre; the main loop uses
    the face average where a piece is absent."""
    shades = {}
    ncut = 0

    def field(f):
        fit = _light_fit(m, f)
        if fit is None or fit[3]:
            return None
        return fit

    def span(edges, fit):
        a, b, c, lmin, lmax = fit[0], fit[1], fit[2], fit[6], fit[7]
        ls = [max(lmin, min(lmax, a + b*k[0] + c*k[1])) for k, _ in edges]
        return abs(_shade_of_lum(max(ls)) - _shade_of_lum(min(ls)))

    def centre_shade(edges, f):
        n = len(edges)
        X = sum(k[0] for k, _ in edges) / n; Y = sum(k[1] for k, _ in edges) / n
        return _shade_at(m, f, X, Y)

    def surfaces(si):
        sec = m.sectors[src[si]]
        cf = m.faces[min(sec.wallEnd+1, m.nface-1)]
        ff = m.faces[min(sec.wallEnd+2, m.nface-1)]
        return cf, ff

    def best_cut(si):
        edges = edge_cache.get(si) or []
        if len(edges) < 3:
            return None
        cf, ff = surfaces(si)
        best = None
        for f in ((ff,) if cf.tileStart < 0 else (ff, cf)):
            fit = field(f)
            if fit is None:
                continue
            sp = span(edges, fit)
            if sp >= KEX_LIGHT_CUT_STEPS and (best is None or sp > best[0]):
                best = (sp, fit)
        return best

    # A sector on either end of a vertical link stays whole: build_tror()
    # pairs the two rings of a seam edge for edge, and a piece has no
    # partner on the other side.
    tror = set()
    for ks, sec in enumerate(m.sectors):
        cf = m.faces[min(sec.wallEnd+1, m.nface-1)]
        ff = m.faces[min(sec.wallEnd+2, m.nface-1)]
        for f in (cf, ff):
            if f.tileStart < 0 and f.link >= 0:
                tror.add(ks); tror.add(f.link)
    # A sector that moves, opens, fires or is stood on stays whole: a door
    # cut in two on tag 0 became two private doors, one of which stayed shut
    # across the doorway when the other was used (35 plain doors and 16 key
    # doors were), and a lift's pieces moved one at a time. Only the walk-in
    # trigger regions may be cut -- every piece is a plate on the same
    # channel, which is harmless.
    keep_whole = set(e[1] for e in getattr(m, 'events', ()) if e[0] not in (50,))
    cands = []
    for si in range(len(src)):
        if src[si] in tror or src[si] in keep_whole:
            continue
        b = best_cut(si)
        if b is not None:
            cands.append((b[0], si, b[1]))
    cands.sort(reverse=True)
    skipped = 0
    for sp, si, fit in cands:
        if len(src) >= KEX_LIGHT_SECTOR_BUDGET:
            skipped += 1
            continue
        # cut() reads the gradient in Build's y-down frame: t = gx*X - gy*Y.
        a, b, c = fit[0], fit[1], fit[2]
        plane = (0, 0, 0, b, -c)
        pin = False
        cf, ff = surfaces(si)
        cp = _plane_build(m, cf) if cf.tileStart >= 0 else None
        fp = _plane_build(m, ff)
        if _sloped(fp) or _sloped(cp):
            # A sloped sector is cut along ITS contour, not the light's: a
            # half without a level-line wall could no longer be tilted at
            # all. When the slope splitter already pinned a first seam, keep
            # the surface that seam actually follows. Choosing the floor
            # unconditionally can reorient a previously aligned diagonal
            # ceiling during a later light cut and flatten it again.
            if pinned.get(si) and _sloped(cp) and _sloped(fp):
                ring = [(k[0] * WORLD, -k[1] * WORLD) for k, _ in edge_cache[si]]
                ceiling_error, floor_error = _surface_errors(edge_cache[si], cp, fp, ring)[0]
                plane = cp if ceiling_error <= floor_error else fp
            else:
                plane = fp if _sloped(fp) else cp
            pin = True
        new_index = len(src)
        halves = _split_slopes.cut(edge_cache[si], plane, si, new_index)
        if halves is None:
            continue
        ra, rb = halves
        if pin:
            # A floor and a ceiling that slope differently were made exact by
            # _split_slopes()' own cuts; a light cut must not undo that. Keep
            # it only when both halves still tilt both surfaces as well as
            # the piece did.
            cost = _split_slopes.cost
            now = cost(edge_cache[si], cp, fp, 0 if pinned.get(si) else
                       _best_first_wall(edge_cache[si], cp, fp)[0])
            after = max(cost(ra, cp, fp, 0), cost(rb, cp, fp, 0))
            if after > KEX_SLOPE_TOLERANCE and after > now:
                continue
        edge_cache[si] = ra
        edge_cache[new_index] = rb
        src.append(src[si])
        pinned[si] = pin
        pinned[new_index] = pin
        ncut += 1
    for si in range(len(src)):
        edges = edge_cache.get(si) or []
        if len(edges) < 3:
            continue
        cf, ff = surfaces(si)
        shades[si] = (centre_shade(edges, cf) if cf.tileStart >= 0 else _shade(m, cf),
                      centre_shade(edges, ff))
    if verbose and (ncut or skipped):
        print("  light: %d contour cuts so floors and ceilings can carry their gradients%s"
              % (ncut, (", %d more wanted but the level is at its sector budget" % skipped) if skipped else ""))
    return shades


def _split_walls_light(m, edge_cache, verbose=False):
    """Subdivide lit wall edges whose light changes along their length, so
    each piece takes the shade of its own stretch. The neighbour across a
    portal is left with a T-junction that _split_tjunctions() closes."""
    import math as _m
    nsplit = 0
    # An event sector's walls stay whole too: a switch face cut into pieces
    # left only the piece nearest the face centre as the switch, the rest
    # inert though textured as it.
    keep_whole = set(e[1] for e in getattr(m, 'events', ()) if e[0] not in (50,))
    for si, edges in list(edge_cache.items()):
        out = []
        if si < m.nsector and si in keep_whole:
            continue
        for k, fis in edges:
            faces = [m.faces[fi] for fi in fis if not isinstance(fi, _Cut)]
            f = next((q for q in faces if q.link < 0 and _lit(q)), None) \
                or next((q for q in faces if _lit(q)), None)
            fit = _light_fit(m, f) if f is not None else None
            n = 1
            if fit is not None and fit[3]:
                a, b, c, wall, ux, uy, lmin, lmax = fit
                lo, hi = _zspan(m, f); Z = (lo + hi) / 2.0
                l0 = max(lmin, min(lmax, a + b*(k[0]*ux + k[1]*uy) + c*Z))
                l1 = max(lmin, min(lmax, a + b*(k[2]*ux + k[3]*uy) + c*Z))
                steps = abs(_shade_of_lum(l0) - _shade_of_lum(l1))
                if steps >= KEX_LIGHT_WALL_STEPS and _m.hypot(k[2]-k[0], k[3]-k[1]) >= 2 * KEX_UNITS_PER_TEXEL:
                    n = min(KEX_LIGHT_WALL_PIECES, 1 + int(steps // KEX_LIGHT_WALL_STEPS))
            if n <= 1:
                out.append((k, fis)); continue
            # The endpoints stay exactly what they were -- a ring's edges
            # meet by equality, and so do the two sides of a portal.
            pts = [(k[0], k[1])] + [(k[0] + (k[2]-k[0]) * i / float(n), k[1] + (k[3]-k[1]) * i / float(n))
                                    for i in range(1, n)] + [(k[2], k[3])]
            for i in range(n):
                out.append(((pts[i][0], pts[i][1], pts[i+1][0], pts[i+1][1]), fis))
            nsplit += n - 1
        edge_cache[si] = out
    if verbose and nsplit:
        print("  light: %d wall pieces so walls can carry their gradients" % nsplit)


# Fires light their surroundings: peak brightening in Build shade steps at the
# sprite, falling off linearly to nothing at (peak << shift) world units.
KEX_LIGHT_PEAK = 36
KEX_LIGHT_SHIFT = 6


def _zspan(m, f):
    zs = [m.verts[f.vertStart+k][2] for k in range(4)]
    return min(zs), max(zs)


def _face_bounds_on_edge(m, f, edge):
    """Build top/bottom of a vertical face at both ends of `edge`.

    A converted wall can be a subsegment of the authored face after the
    T-junction and light splitters. Interpolate the quad's two vertical edge
    pairs so the runtime can choose the vertically stacked neighbour at the
    actor/camera's exact height, not from one coarse min/max for the face.
    """
    columns = {}
    for i in range(4):
        x, y, z = m.verts[f.vertStart + i][:3]
        columns.setdefault((x, y), []).append(z)
    if len(columns) < 2:
        lo, hi = _zspan(m, f)
        top, bot = int(round(-hi * SCALE * WORLD)), int(round(-lo * SCALE * WORLD))
        return top, bot, top, bot
    pts = list(columns)
    a, b = max(((a, b) for i, a in enumerate(pts) for b in pts[i + 1:]),
               key=lambda q: (q[1][0] - q[0][0]) ** 2 + (q[1][1] - q[0][1]) ** 2)
    dx, dy = b[0] - a[0], b[1] - a[1]
    den = dx * dx + dy * dy
    alo, ahi = min(columns[a]), max(columns[a])
    blo, bhi = min(columns[b]), max(columns[b])

    def at(x, y):
        t = 0.0 if den <= 1e-12 else ((x - a[0]) * dx + (y - a[1]) * dy) / den
        t = max(0.0, min(1.0, t))
        lo = alo + (blo - alo) * t
        hi = ahi + (bhi - ahi) * t
        return int(round(-hi * SCALE * WORLD)), int(round(-lo * SCALE * WORLD))

    return at(edge[0], edge[1]) + at(edge[2], edge[3])

# Kex bakes its lighting into the vertex colours, and in these levels it is
# greyscale (measured: colour spread 0 at both the median and the 90th
# percentile), so it maps onto Build's shade with nothing lost. The range is
# wide and dark -- TOMB's median vertex luminance is 40 of 255 -- which is
# exactly the atmosphere that goes missing when everything is drawn at shade 0.
#
# The curve is a square root rather than linear: Build's shade table darkens
# fast, and a straight mapping of a median-40 level puts most of it at the
# bottom of the ramp where nothing is legible.
# Calibrated against the remaster itself, by sampling the 3D viewport of frames
# from a playthrough of this level. What matters is the DISTRIBUTION, not the
# average -- matching only the median produced a bimodal image (blown-out near
# walls, a third of the frame pure black) whose average happened to be right:
#
#     reference   p05 20   p25 30   p50 37   p75 47   p95 81   black  0%
#     median-only p05  0   p25  0   p50 39   p75 82   p95 108  black 29%
#
# So the shade band is compressed and lifted off zero rather than spanning the
# full ramp. The endpoints come from a measured response curve of shade to
# rendered luminance (17 -> 85, 25 -> 75, 47 -> 39, 105 -> 0), which is flat at
# the top and then falls off a cliff -- nothing like the linear ramp that
# reasoning from Build's palette would suggest.
KEX_SHADE_MIN  = -10         # brightest surface: still not fullbright
# Face flag 0x400: 181 solid faces across the levels, 164 of them COLONY_PSX's
# colony142/colony149 light panels (the lit strips, the glowing slot), plus a
# few on KILARENA, KILENTRY and PEAK -- a self-lit surface, drawn at the
# brightest shade whatever the vertex light around it says.
KEX_FACE_SOLID            = 0x0001
KEX_FACE_BLOCK_AI_FALLERS = 0x0002
KEX_FACE_INVISIBLE        = 0x0004
KEX_FACE_BLOCK_NONPLAYERS = 0x0008
KEX_FACE_LAVA             = 0x0020
KEX_FACE_WATER            = 0x0040
KEX_FACE_SLIME            = 0x0080
KEX_FACE_TOGGLE           = 0x0100
KEX_FACE_FORCEFIELD       = 0x0200
KEX_FACE_SELFLIT          = 0x0400
KEX_FACE_HIDDEN           = 0x1000
KEX_FACE_PORTAL           = 0x2000
KEX_FACE_UNDERWATER       = 0x4000
KEX_FACE_FORCE_SKY        = 0x8000

# Build has no persistent per-direction actor clipping flags. The converter
# carries the two Kex-only rules in wall.extra until InitSectFlag() records
# them, before normal Exhumed map setup clears wall.extra. These values are
# deliberately far from -1 and from every non-negative YAX wall link.
KEX_WALL_CLIP_AI_FALLERS = 1
KEX_WALL_CLIP_NONPLAYERS = 2
KEX_WALL_EXTRA_BLOCK_AI_FALLERS = -32001
KEX_WALL_EXTRA_BLOCK_NONPLAYERS = -32002
KEX_WALL_EXTRA_BLOCK_BOTH = -32003
# The two KEX floor/face hazard kinds. These stay in wall.extra because Build
# has no per-wall lava/slime flag and this private negative range is unused by
# normal maps.
KEX_WALL_EXTRA_LAVA = -32004
KEX_WALL_EXTRA_SLIME = -32005
KEX_SHADE_SPAN = 88         # darkest lands ~75, dim but never crushed to black
KEX_SHADE_MAX  = 127

def _lit(f):
    """Does this face carry baked lighting of its own?

    A Kex face is lit on its TILE vertices, and a face with no tiles has none:
    the four corner vertices carry a flat 32 on every face in the level. In TOMB
    901 of 1629 wall faces have no tile vertices and 884 of those are portals --
    the openings between sectors, which Kex does not texture because you see
    through them. Build still draws a portal wall's upper and lower parts, so
    every step and ledge in the game was being drawn at the one shade the flat
    32 produces (47 of 64 -- almost black), which is most of why the levels read
    grey and unlit.
    """
    return f.tileVertStart >= 0 and f.tileVertEnd >= f.tileVertStart


def _shade(m, f):
    # The lighting is on the face's TILE vertices, not its four corners: the
    # corner quad carries a flat 32 on every face in the level (measured), while
    # the tile vertices span 25..255 with a median of 58. Averaging the corners
    # gives every surface the same shade, which is exactly the flat, unlit look.
    n = 0; tot = 0
    if f.tileVertStart >= 0 and f.tileVertEnd >= f.tileVertStart:
        for i in range(f.tileVertStart, min(f.tileVertEnd + 1, len(m.verts))):
            r, g, b, a = m.verts[i][3]
            tot += (r*30 + g*59 + b*11) // 100; n += 1
    if not n:
        for k in range(4):
            i = f.vertStart + k
            if i >= len(m.verts): break
            r, g, b, a = m.verts[i][3]
            tot += (r*30 + g*59 + b*11) // 100; n += 1
    if not n:
        return 0
    lum = (tot / float(n)) / 255.0
    return max(-128, min(KEX_SHADE_MAX,
                      int(round(KEX_SHADE_MIN + (1.0 - lum ** 0.5) * KEX_SHADE_SPAN))))


def _shade_of_lum(lum):
    """Vertex luminance 0..255 -> Build shade, the calibrated curve above."""
    lum = max(0.0, min(255.0, lum)) / 255.0
    return max(-128, min(KEX_SHADE_MAX,
                      int(round(KEX_SHADE_MIN + (1.0 - lum ** 0.5) * KEX_SHADE_SPAN))))


# Kex lights every TILE vertex, so a face is not one shade but a field of
# them: measured over the 25 levels, 46% of floors and ceilings and 40% of
# walls span six or more Build shade steps across their own extent (the 90th
# percentile is 29 steps for a floor, 37 up a wall). Build has one shade per
# wall and per floor, so the gradient is carried by geometry instead -- see
# _split_light() and _split_walls_light() -- and the per-piece shade comes
# from this: a least-squares plane through the face's tile-vertex luminance,
# in (x, y) for a floor or ceiling and in (along-wall, z) for a wall.  Keep the
# cache on the parsed map: verifier processes maps one by one, so a global
# id(m)-keyed cache can alias a later map after Python reuses an object ID.

def _light_fit(m, f):
    """(a, b, c) with luminance = a + b*u + c*w, or None when the face is
    unlit. u, w are Kex (X, Y) for a flat and (along-wall, Z) for a wall."""
    cache = getattr(m, '_light_fit_cache', None)
    if cache is None:
        cache = m._light_fit_cache = {}
    if f in cache:
        return cache[f]
    fit = None
    if _lit(f):
        wall = abs(f.n[2]) <= 0.5
        ux, uy = -f.n[1], f.n[0]
        pts = []
        for i in range(f.tileVertStart, min(f.tileVertEnd + 1, len(m.verts))):
            X, Y, Z, (r, g, b, a) = m.verts[i]
            l = (r*30 + g*59 + b*11) / 100.0
            pts.append((X*ux + Y*uy, Z, l) if wall else (X, Y, l))
        n = len(pts)
        if n >= 3:
            mu = sum(q[0] for q in pts) / n; mw = sum(q[1] for q in pts) / n
            ml = sum(q[2] for q in pts) / n
            suu = sum((q[0]-mu)**2 for q in pts); sww = sum((q[1]-mw)**2 for q in pts)
            suw = sum((q[0]-mu)*(q[1]-mw) for q in pts)
            sul = sum((q[0]-mu)*(q[2]-ml) for q in pts); swl = sum((q[1]-mw)*(q[2]-ml) for q in pts)
            det = suu*sww - suw*suw
            if abs(det) > 1e-6:
                b = (sul*sww - swl*suw) / det; c = (swl*suu - sul*suw) / det
            else:
                b = sul/suu if suu > 1e-6 else 0.0; c = swl/sww if sww > 1e-6 else 0.0
            # Never extrapolated past what the face's own vertices measured.
            lmin, lmax = min(q[2] for q in pts), max(q[2] for q in pts)
            fit = (ml - b*mu - c*mw, b, c, wall, ux, uy, lmin, lmax)
    cache[f] = fit
    return fit


def _shade_at(m, f, X, Y, Z=None):
    """The face's shade at Kex point (X, Y[, Z]); the face average when it
    has no usable field."""
    fit = _light_fit(m, f)
    if fit is None:
        return _shade(m, f)
    a, b, c, wall, ux, uy, lmin, lmax = fit
    if wall:
        if Z is None:
            lo, hi = _zspan(m, f); Z = (lo + hi) / 2.0
        l = a + b*(X*ux + Y*uy) + c*Z
    else:
        l = a + b*X + c*Y
    return _shade_of_lum(max(lmin, min(lmax, l)))


def _pic(m, f, tilebase):
    if f.tileStart < 0: return -1          # untextured: the writer gives it the sky
    return tilebase + m.tiles[f.tileStart][1]

def _surface(pl, ring, wx, wy, dx, dy, sky=False, ceiling=False, quad=None):
    """(z, heinum) to emit for one floor or ceiling plane over a sector whose
    first wall is (wx, wy)->(wx+dx, wy+dy) and whose walls stand at `ring`.

    A flat plane is flat. A sloped one is tilted about the first wall when that
    reproduces it within KEX_SLOPE_TOLERANCE at every ring vertex -- which is
    what _best_first_wall() and _split_slopes() arrange -- and otherwise
    whichever of that tilt and flat-at-the-mean lies less is used. The mean is
    over the sector's OWN corners; the old fallback took the highest corner of
    the face's bounding quad, a point outside any non-rectangular sector, and
    that is what put TOMB's staircase ceiling a hundred units above the real
    one. A sloped sky ceiling is simply held at its highest point: nothing of
    it is drawn, and the walls below want it out of the way."""
    if pl is None:
        return 0, 0
    zs = [_plane_z(pl, x, y) for x, y in ring]
    if not _sloped(pl):
        return int(round(zs[0])), 0
    if sky and ceiling:
        # Held at the plane's highest point over the face's own quad when
        # given, not over this ring: a sector cut into pieces would otherwise
        # give each piece a different sky ceiling, and the neighbour's upper
        # wall shows up against the lower one as a slab across the sky.
        if quad:
            zs = [_plane_z(pl, x, y) for x, y in quad]
        return int(round(min(zs))), 0          # Build z grows downward
    z_, h_ = _slope(pl, wx, wy, dx, dy)
    err = _ring_error(pl, ring, z_, h_, wx, wy, dx, dy)
    mean = sum(zs) / len(zs)
    flat = max(abs(v - mean) for v in zs)

    # A plane steeper than Build's signed-16-bit heinum limit is clipped by
    # _slope().  Keeping its source height exact on the first wall puts the
    # entire residual on the far edge, which can make that *capped* slope look
    # worse than a flat surface and discard every contour strip.  Centre the
    # residual vertically instead: a terrace is allowed to step at its seam,
    # and this minimises the largest error over the piece.  Do not perturb
    # ordinary aligned slopes, whose exact first-wall height keeps neighbours
    # continuous.
    if err > KEX_SLOPE_TOLERANCE and h_ in (-32768, 32767):
        import math as _m
        i = int(_m.hypot(dx, dy)) << 5
        if i:
            residual = []
            for x, y in ring:
                j = int(dx * (y - wy) - dy * (x - wx)) >> 3
                residual.append(_plane_z(pl, x, y) - (z_ + (h_ * j) // i))
            shift = int(round((min(residual) + max(residual)) * 0.5))
            if shift:
                shifted = z_ + shift
                shifted_error = _ring_error(pl, ring, shifted, h_, wx, wy, dx, dy)
                if shifted_error < err:
                    z_, err = shifted, shifted_error
    if err <= KEX_SLOPE_TOLERANCE or err <= flat:
        return z_, h_
    return int(round(mean)), 0


def convert(m, tilebase=4096, switch_pairs=None, ramses=None):
    walls = []; sects = []; ranges = []
    # Kex keeps a sector's trigger in a separate event table; Build PowerSlave
    # wants it on the sector as lotag (the effect) and hitag (the channel).
    # A sector may carry several events (six do: THOTH's plate that is also
    # a switch, SHRINE's door that is both pressed and triggered, a key
    # door doubled with a plain one); all are kept, in an order where the
    # one that should shape the sector comes last -- a key door over a
    # plain one, a door over a plate -- and _resolve_events() applies each.
    RANK = {50: 0, 23: 0, 200: 1, 201: 1, 202: 1, 203: 1, 1: 2, 2: 2, 7: 2, 3: 3, 4: 3, 5: 3, 6: 3}
    sector_events = {}
    for etype, esect, etag, eparam in getattr(m, 'events', ()):
        if esect < m.nsector and etype:
            sector_events.setdefault(esect, []).append((etype, esect, etag, eparam))
    for es in sector_events:
        sector_events[es].sort(key=lambda e: RANK.get(e[0], 2))
    edge_cache = {}
    for si in range(m.nsector):
        try:
            edge_cache[si] = _edges(m, si)
        except Exception:
            edge_cache[si] = []
    # Sectors Build cannot tilt are cut in two along a contour first, so that
    # the T-junctions the cut leaves on their neighbours are closed with the
    # rest. Pieces are appended after the source sectors; src maps them back.
    src, pinned = _split_slopes(m, edge_cache)
    # Then along the light, so a floor lit from one end is bands of shade
    # rather than one average; then the walls along their length.
    light_shades = _split_light(m, edge_cache, src, pinned)
    _split_walls_light(m, edge_cache)
    _split_tjunctions(m, edge_cache)
    _split_long(edge_cache)
    _drop_collapsed_build_edges(edge_cache)
    pieces = {}
    for i, o in enumerate(src):
        pieces.setdefault(o, []).append(i)
    # Retain the source identity of split descendants for actor placement and
    # verification. The first nsector entries remain the identity.
    m.kex_sector_src = src
    m.kex_sector_pieces = pieces

    for si in range(len(src)):
        s = m.sectors[src[si]]
        edges = edge_cache[si]
        if len(edges) < 3:
            ranges.append((len(walls), 0))
            sects.append(None); continue
        pts = [(k[0], -k[1]) for k,_ in edges]
        area = sum(pts[i][0]*pts[(i+1)%len(pts)][1] - pts[(i+1)%len(pts)][0]*pts[i][1]
                   for i in range(len(pts)))
        # Build wants a sector's outer loop clockwise in its own coordinate
        # space, where y grows downward -- which is a POSITIVE shoelace sum.
        if area < 0:
            edges = [( (k[2],k[3],k[0],k[1]), f) for k,f in reversed(edges)]

        # Build can only tilt a sector about its first wall, so if this sector's
        # floor or ceiling is sloped, start the loop on the wall that leaves
        # the least lie over both surfaces -- a level line of the slope when
        # there is one, and then the tilt is exact. Rotating the ring is safe:
        # nextwall is matched by coordinate afterwards, not by index. A piece
        # cut by _split_slopes() already starts on its contour and stays put.
        ff_face = m.faces[min(s.wallEnd+2, m.nface-1)]
        cf_face = m.faces[min(s.wallEnd+1, m.nface-1)]
        cf_p = _plane_build(m, cf_face) if cf_face.tileStart >= 0 else None
        ff_p = _plane_build(m, ff_face)
        if not pinned.get(si) and (_sloped(cf_p) or _sloped(ff_p)):
            best, _errs = _best_first_wall(edges, cf_p, ff_p)
            edges = edges[best:] + edges[:best]

        start = len(walls)
        for i,(k,fis) in enumerate(edges):
            portal = None; portal_fi = -1; pspan = -1.0
            for fi in fis:
                if isinstance(fi, _Cut):
                    portal = fi; break
                f = m.faces[fi]
                if f.link >= 0:
                    lo,hi = _zspan(m,f)
                    if hi-lo >= pspan:
                        pspan = hi-lo; portal = f; portal_fi = fi
            portal_flags = 0
            portal_alts = []
            if portal is not None and not isinstance(portal, _Cut):
                # Collision flags belong to directed faces, not to textures.
                # Several vertical face bands can describe one edge, so keep
                # every flag aimed at the selected neighbouring sector.
                for fi in fis:
                    if isinstance(fi, _Cut):
                        continue
                    pf = m.faces[fi]
                    if pf.link == portal.link:
                        portal_flags |= pf.flags
                # Keep every vertical neighbour on this same 2D edge. Build's
                # wall struct has room for only one; kexportal.txt lets the
                # KEX-only runtime select the one containing the actor/view Z.
                seen_alts = set()
                for fi in fis:
                    if isinstance(fi, _Cut):
                        continue
                    pf = m.faces[fi]
                    if pf.link < 0:
                        continue
                    bounds = _face_bounds_on_edge(m, pf, k)
                    tex = m.tiles[pf.tileStart][1] if pf.tileStart >= 0 else -1
                    key = (pf.link, pf.flags, tex) + bounds
                    if key in seen_alts:
                        continue
                    seen_alts.add(key)
                    portal_alts.append(dict(face=fi, moveGroup=pf.moveGroup,
                                            link=pf.link, flags=pf.flags,
                                            texture=tex, bounds=bounds,
                                            nextwall=-1, nextsector=-1))
            if fis and not isinstance(fis[0], _Cut):
                solid_fi = next((fi for fi in fis if m.faces[fi].link < 0), fis[0])
                solid = m.faces[solid_fi]
            else:
                # A side the source never gave a face: either synthesised by
                # _edges to close the ring, where nothing is ever drawn and it
                # stays solid, or the seam of a contour split, which is a
                # portal into the other half. Either way the sector's floor
                # lends the texture.
                solid_fi = min(s.wallEnd + 2, m.nface - 1)
                solid = ff_face
            # Build shows exactly xrepeat*8 texels across a wall (polymost's
            # u spans wal->xrepeat*8). Kex's compiled UVs put one 64-pixel
            # repeat over 128 source units, so xrepeat follows the wall's
            # length at half a texel per source unit.
            import math as _m
            ex, ey = k[2] - k[0], k[3] - k[1]
            length = _m.hypot(ex, ey)
            walls.append(dict(x=int(round(k[0])), y=int(round(-k[1])),
                              point2=start+((i+1)%len(edges)),
                              nextwall=-1,
                              nextsector=portal.link if portal is not None else -1,
                              cstat=0, picnum=_pic(m, solid, tilebase),
                              xrepeat=max(1, min(255, int(round(length * WORLD / (8.0 * KEX_UNITS_PER_TEXEL))))),
                              yrepeat=KEX_YREPEAT,
                              # An unlit portal face has nothing of its own to
                              # go on, so borrow the sector's floor -- a step's
                              # visible face sits on the floor and is lit with
                              # it. The flat corner value it used to fall back
                              # to is the same number everywhere and says
                              # nothing at all.
                              shade=(KEX_SHADE_MIN if (solid.flags & KEX_FACE_SELFLIT)
                                     else _shade_at(m, solid if _lit(solid) else ff_face,
                                                    (k[0] + k[2]) / 2.0, (k[1] + k[3]) / 2.0)),
                              _kex_light_face=solid_fi,
                              _kex_mask_light_face=portal_fi))
            # Kex keeps rendering and collision independent. A portal face
            # may be untextured yet Solid, textured but passable, Invisible,
            # or actor-selective. Build's masked art is therefore assigned
            # separately from its block bits. Underwater is a medium flag,
            # not an exception to Solid. Force-field tiles remain in
            # overpicnum even when Invisible so KexForce_OnWallHit can
            # identify and remove the contacted barrier face.
            walls[-1]['_fis'] = fis; walls[-1]['_portal'] = portal; walls[-1]['_ff'] = ff_face
            # kexPuppet::OnCollide also applies LavaDamage/SlimeDamage when
            # a *solid wall* face bears those flags. The classic sector-damage
            # tag handles only floors; retain the wall case in private MAP
            # metadata for movesprite()'s KEX-only collision bridge.
            if portal is None and solid.link < 0:
                if solid.flags & KEX_FACE_LAVA:
                    walls[-1]['_kex_hazard'] = KEX_WALL_EXTRA_LAVA
                elif solid.flags & KEX_FACE_SLIME:
                    walls[-1]['_kex_hazard'] = KEX_WALL_EXTRA_SLIME
            if portal_alts:
                walls[-1]['_kex_portal_alts'] = portal_alts
            if (portal is None and fis and not isinstance(fis[0], _Cut) and solid.link < 0
                    and not _sloped(cf_p) and not _sloped(ff_p)):
                rows = _face_rows(m, solid)
                if rows is not None:
                    # the Build wall spans the sector's ceiling to floor; the
                    # composite is cut to the face, so they must coincide
                    ft = max(m.verts[solid.vertStart + q][2] for q in range(4))
                    fb = min(m.verts[solid.vertStart + q][2] for q in range(4))
                    ct = _zspan(m, cf_face)[1]; fz = _zspan(m, ff_face)[0]
                    if abs(ft - ct) <= 1.0 and abs(fb - fz) <= 1.0:
                        walls[-1]['_comp'] = rows
            if portal is not None and not isinstance(portal, _Cut):
                w = walls[-1]
                w['_kex_face_flags'] = portal_flags
                w['_kex_invisible'] = bool(portal.flags & KEX_FACE_INVISIBLE)
                if portal.tileStart >= 0:
                    w['overpicnum'] = _pic(m, portal, tilebase)
                    if not (portal.flags & KEX_FACE_INVISIBLE):
                        w['cstat'] |= 16
                    tex = m.tiles[portal.tileStart][1]
                    tname = m.textures[tex] if tex < len(m.textures) else ''
                    if (portal_flags & KEX_FACE_FORCEFIELD) or 'forcefield' in tname.lower():
                        m.kex_force_tiles = getattr(m, 'kex_force_tiles', set()) | {w['overpicnum']}
                if portal_flags & KEX_FACE_SOLID:
                    w['cstat'] |= 1 | 64
                else:
                    clip = 0
                    if portal_flags & KEX_FACE_BLOCK_AI_FALLERS:
                        clip |= KEX_WALL_CLIP_AI_FALLERS
                    if portal_flags & KEX_FACE_BLOCK_NONPLAYERS:
                        clip |= KEX_WALL_CLIP_NONPLAYERS
                    if clip:
                        w['_kex_clip'] = clip
        ranges.append((start, len(walls)-start))
        cf = cf_face
        ff = ff_face
        w0 = walls[start]
        w1 = walls[start + 1] if len(walls) - start > 1 else w0
        dxw, dyw = (w1['x'] - w0['x']) * WORLD, (w1['y'] - w0['y']) * WORLD
        wx0, wy0 = w0['x']*WORLD, w0['y']*WORLD
        ring = [(w['x']*WORLD, w['y']*WORLD) for w in walls[start:]]
        cz, cheinum = _surface(_plane_build(m, cf), ring, wx0, wy0, dxw, dyw,
                               sky=(cf.tileStart < 0), ceiling=True,
                               quad=[(m.verts[cf.vertStart+k][0]*WORLD, -m.verts[cf.vertStart+k][1]*WORLD)
                                     for k in range(4) if cf.vertStart+k < len(m.verts)])
        fz, fheinum = _surface(ff_p, ring, wx0, wy0, dxw, dyw)
        if cz >= fz: cz = fz - 8           # never let the ceiling sit under the floor
        # Kex has no sky tile: the sky is named in the map header and a ceiling
        # simply carries NO tiles where the sky shows through. Untextured
        # ceilings were being given texture slot 0 -- 169 of them in KARNAK --
        # which is why its outdoor areas looked wrong.
        # Events are resolved into Build tags at the end of convert(), once the
        # portals are known; the raw record rides along until then.
        ev = sector_events.get(src[si])
        # Kex marks a vertical opening the same way it marks a horizontal one:
        # the ceiling or floor FACE carries a link to the sector on the other
        # side. 300 of them across the 25 levels. Build calls this TROR.
        # A vertical link is only an OPENING when the face carries no tiles, the
        # same way a wall portal does. In the retail maps it almost never is --
        # see build_tror().
        fkey = _flat_cells(m, ff) if not fheinum and ff.link < 0 else None
        ckey = _flat_cells(m, cf) if not cheinum and cf.link < 0 else None
        sects.append(dict(floorsky=(ff.tileStart < 0 and ff.link < 0),
                          _fcomp_f=fkey, _fcomp_c=ckey,
                          _kex_light_floor_face=min(s.wallEnd + 2, m.nface - 1),
                          _kex_light_ceiling_face=min(s.wallEnd + 1, m.nface - 1),
                          yax_ceil=(cf.link if cf.tileStart < 0 else -1),
                          yax_floor=(ff.link if ff.tileStart < 0 else -1),
                          wallptr=start, wallnum=len(walls)-start,
                          lotag=0, hitag=0,
                          kexevent=(ev if ev else None),
                          ceilingsky=(cf.tileStart < 0),
                          ceilingz=cz, floorz=fz,
                          ceilingheinum=cheinum, floorheinum=fheinum,
                          ceilingstat=(2 if cheinum else 0), floorstat=(2 if fheinum else 0),
                          ceilingpicnum=_pic(m,cf,tilebase), floorpicnum=_pic(m,ff,tilebase),
                          ceilingshade=(KEX_SHADE_MIN if cf.flags & KEX_FACE_SELFLIT
                                        else light_shades.get(si, (_shade(m,cf), 0))[0]),
                          floorshade=(KEX_SHADE_MIN if ff.flags & KEX_FACE_SELFLIT
                                      else light_shades.get(si, (0, _shade(m,ff)))[1])))

    # nextwall: the neighbour's wall carrying the same edge reversed. A face
    # links to a SOURCE sector; when that sector was cut the edge may sit in
    # any of its pieces, so all of them are searched and nextsector is set to
    # the piece that actually has it.
    def match_target(w, source_sector):
        nxt = walls[w['point2']]
        for cand in pieces.get(source_sector, [source_sector]):
            if not (0 <= cand < len(ranges)):
                continue
            ns_start, ns_num = ranges[cand]
            for j in range(ns_num):
                o = walls[ns_start+j]; op = walls[o['point2']]
                if ((o['x'], o['y']) == (nxt['x'], nxt['y'])
                        and (op['x'], op['y']) == (w['x'], w['y'])):
                    return cand, ns_start + j
        return -1, -1

    for si,(start,num) in enumerate(ranges):
        for i in range(num):
            w = walls[start+i]
            ns = w['nextsector']
            if not (0 <= ns < len(ranges)):
                w['nextsector'] = -1; continue
            target_source = src[ns] if ns < len(src) else ns
            cand, hit = match_target(w, target_source)
            if hit >= 0:
                w['nextsector'] = cand
            w['nextwall'] = hit
            if hit < 0: w['nextsector'] = -1

    # Resolve every stacked alternative independently of the one selected as
    # the static Build fallback. This is deliberately done before reciprocal
    # fallback below: a Kex face's own authored destination is authoritative.
    for w in walls:
        for alt in w.get('_kex_portal_alts', ()):
            alt['nextsector'], alt['nextwall'] = match_target(w, alt['link'])
    # A portal only counts if both halves agree -- but a half that found its
    # partner by coordinate and whose partner is simply unpaired adopts it.
    # TOMB's temple sill: the porch's face links to the sill, the sill's face
    # back links to sector 257, a dummy elsewhere (260 and 262, the fire-pot
    # platforms, do the same), so the sill's half never found the porch and
    # both halves used to be cleared: a solid, sky-textured wall across the
    # doorway that blocked walking and let the eye through. Only a partner
    # that has no other partner is adopted; two halves that point at
    # different walls are still both cleared.
    sec_of = {}
    for si,(st,num) in enumerate(ranges):
        for i in range(num):
            sec_of[st+i] = si
    for wi,w in enumerate(walls):
        if w['nextwall'] < 0: continue
        o = walls[w['nextwall']]
        if o['nextwall'] == wi: continue
        if o['nextwall'] < 0 and wi in sec_of:
            o['nextwall'] = wi; o['nextsector'] = sec_of[wi]
        else:
            w['nextwall'] = -1; w['nextsector'] = -1
    for wi,w in enumerate(walls):          # masked art is visible from both sides
        if w['nextwall'] >= 0 and 'overpicnum' in w and 'overpicnum' not in walls[w['nextwall']]:
            o = walls[w['nextwall']]
            # Collision is directed in Kex. In particular SLAVCAMP has one
            # textured passable face whose reverse face is Solid, so copying
            # the block bits here would erase a real one-way opening.
            o['overpicnum'] = w['overpicnum']; o['cstat'] |= w['cstat'] & 16
            o['_kex_mask_light_face'] = w.get('_kex_mask_light_face', -1)
            # This is renderer glue, not a second authored face.  If the
            # source is a Toggle face, opening it must remove this copied view
            # from the reverse side without changing that side's collision.
            o['_kex_copied_overpic_from'] = wi
    _wall_bands(m, sects, walls, ranges, tilebase)

    _resolve_events(m, sects, walls, ranges, src, switch_pairs, tilebase)
    m.kex_flat_sprites = _flat_sprites(m, sects, walls, ranges, src)
    m.kex_float_surfaces = _float_surfaces(m, sects, src)
    m.kex_drop_surfaces = _drop_pad_surfaces(m, sects, src)
    m.kex_extra_sprites = []
    if ramses is not None:
        # The pad: this map's SCRIPT_REPEATABLE sectors run the Ramses script,
        # so they take sector effect 80, the flag the player-touch trigger
        # tests. The head: a lotag-61 sprite where the script spawns him -- or
        # as close to it as a sector reaches. TOMB's scripted origin floats
        # inside the casket structure, solid space no sector covers, so the
        # point walks back toward the pad until one holds it.
        pad = None
        for si, s_ in enumerate(sects):
            if s_ is not None and s_.get('kexevent63'):
                s_['lotag'], s_['hitag'] = 80, 0
                if pad is None:
                    start, num = ranges[si]
                    pad = (sum(walls[start+i]['x'] for i in range(num)) / max(1, num),
                           sum(walls[start+i]['y'] for i in range(num)) / max(1, num))
        kx, ky, kz = ramses
        # Wall (and find_sector) coordinates are raw Kex units with y flipped;
        # the Build scale is applied at write-out, and again when the sprite
        # is emitted in build_sprites(). The scripted spawn point sits behind
        # the casket. Its source coordinate is outside the surviving converted
        # ring, so walk from the pad toward it until a sector contains the head,
        # keeping it in the upper third of the room where the frozen camera
        # looks. The casket's SCRIPTED_MOVE itself is carried separately.
        hx, hy = kx, -ky
        tx, ty = pad if pad else (hx, hy)
        # LEV1 stands its head ~200 Kex units from the player and marks it
        # translucent and z-centred (cstat 0x82). Match that: too close and
        # the face sprite hugs the near plane and never draws.
        for t in (0.56, 0.4, 0.25, 0.1, 0.0):
            x = tx + (hx - tx) * t
            y = ty + (hy - ty) * t
            si = find_sector(sects, walls, ranges, x, y)
            if si >= 0:
                cz, fz = sects[si]['ceilingz'], sects[si]['floorz']
                m.kex_extra_sprites.append(dict(
                    x=int(x), y=int(y), z=max(cz + 4096, fz - 16384),
                    lotag=61, hitag=0, ang=0, cstat=0x82))
                break
    return sects, walls, ranges

KEX_TILE_Z = int(KEX_TEXTURE_REPEAT * SCALE * WORLD)  # one texture repeat in Build z

KEX_COMPOSITE_MAX_H = 1024      # Build's tallest tile

def _face_rows(m, f):
    """A wall face's mosaic as rows, or None when one texture covers it.

    A Kex face is a mosaic of 128-unit cells that can each hold a different
    texture -- a trim band over a main texture is the common case -- and
    9,376 of the retail levels' 24,706 textured wall faces do. A Build wall
    has one texture, so such a face gets a COMPOSITE tile: 64 texels wide
    (one repeat, the mapping repeats it across the wall) and as tall as the
    face, each row copied from its texture in world phase. Only faces that
    show one texture at every height qualify (the cells of a row need not
    line up: cave walls stagger them), and only up to Build's tallest tile.

    Returns (top % 128, ((offset from top, height, texture), ...)) -- the
    phase is part of the key because the cells sit on the world grid.
    """
    import struct as _st
    if f.tileStart < 0:
        return None
    V = m.verts; d = m.data
    cells = []; texs = set()
    for ti in range(f.tileStart, f.tileEnd + 1):
        q = m.tileOff + ti * 16
        corners = [V[f.tileVertStart + k] for k in d[q:q + 4]]
        tex = m.textures[_st.unpack_from('<H', d, q + 12)[0]]
        zt = round(max(v[2] for v in corners)); zb = round(min(v[2] for v in corners))
        if zt > zb:
            cells.append((zt, zb, tex)); texs.add(tex)
    if len(texs) < 2:
        return None
    top = round(max(V[f.vertStart + k][2] for k in range(4)))
    bot = round(min(V[f.vertStart + k][2] for k in range(4)))
    if top - bot > KEX_COMPOSITE_MAX_H or top - bot <= 0:
        return None
    zs = sorted(set([z for c in cells for z in c[:2]]), reverse=True)
    if abs(zs[0] - top) > 1 or abs(zs[-1] - bot) > 1:
        return None                      # the cells do not reach the face's edges
    rows = []
    for hi, lo in zip(zs, zs[1:]):
        cover = set(t for zt, zb, t in cells if zt >= hi and zb <= lo)
        if len(cover) != 1:
            return None                  # a gap, or two textures at one height
        t = cover.pop()
        if rows and rows[-1][2] == t:
            rows[-1][1] = lo
        else:
            rows.append([hi, lo, t])
    return (top % int(KEX_TEXTURE_REPEAT),
            tuple((top - hi, hi - lo, t) for hi, lo, t in rows))


def _flat_cells(m, f):
    """A floor or ceiling's mosaic, or None when one texture covers it.

    Returns (x0, y0, W, H, cells): the cells' bounding box in Kex units, the
    composite's size in texels (KEX_FLAT_TEXELS per 128 units), and the cells
    as (x0, y0, x1, y1, texture). Build maps a flat from the world origin
    (u = x/16, v = -y/16 in its own units, one texel per 16), and the flat's
    panning only reaches half a large tile, so the composite is baked
    already shifted onto that grid: its period is the bounding box, so the
    wrap is seamless, and no panning is needed.
    """
    import struct as _st
    if f.tileStart < 0:
        return None
    V = m.verts; d = m.data
    cells = []; texs = set()
    for ti in range(f.tileStart, f.tileEnd + 1):
        q = m.tileOff + ti * 16
        cs = [V[f.tileVertStart + k] for k in d[q:q + 4]]
        tex = m.textures[_st.unpack_from('<H', d, q + 12)[0]]
        x0 = min(v[0] for v in cs); x1 = max(v[0] for v in cs)
        y0 = min(v[1] for v in cs); y1 = max(v[1] for v in cs)
        if x1 - x0 < 0.5 or y1 - y0 < 0.5:
            continue
        cells.append((x0, y0, x1, y1, tex)); texs.add(tex)
    if len(texs) < 2:
        return None
    bx0 = min(c[0] for c in cells); bx1 = max(c[2] for c in cells)
    by0 = min(c[1] for c in cells); by1 = max(c[3] for c in cells)
    W = int(round((bx1 - bx0) * KEX_FLAT_TEXELS / KEX_TEXTURE_REPEAT))
    H = int(round((by1 - by0) * KEX_FLAT_TEXELS / KEX_TEXTURE_REPEAT))
    if W < 2 or H < 2 or W > KEX_COMPOSITE_MAX_H or H > KEX_COMPOSITE_MAX_H:
        return None
    return (round(bx0, 2), round(by0, 2), W, H, tuple(sorted((round(c[0], 2), round(c[1], 2), round(c[2], 2), round(c[3], 2), c[4]) for c in cells)))


def _bake_flat_composites(kpf, fcomp_assign, outdir, verbose=True):
    """Write kexart/fcomp/<tile>.png for every flat composite; {tile: path}."""
    import kexpng as _png
    outd = os.path.join(outdir, 'kexart', 'fcomp')
    os.makedirs(outd, exist_ok=True)
    cache = {}; out = {}
    k = KEX_FLAT_TEXELS / KEX_TEXTURE_REPEAT  # Build tile texels per Kex unit
    with zipfile.ZipFile(kpf) as z:
        have = {n.lower(): n for n in z.namelist()}
        def texture(name):
            if name in cache:
                return cache[name]
            real = have.get(name.lower())
            if real is None:
                cache[name] = None; return None
            tmp = os.path.join(outd, '_src.png')
            open(tmp, 'wb').write(z.read(real))
            try:
                w_, h_, pix = _png.read(tmp)
            finally:
                os.remove(tmp)
            if (w_, h_) != (64, 64):
                pix = _png.resize(w_, h_, pix, 64, 64)
            cache[name] = pix
            return pix
        for key, tile in sorted(fcomp_assign.items(), key=lambda kv: kv[1]):
            x0, y0, W, H, cells = key
            buf = bytearray(W * H * 4)
            for j in range(H):
                # column i of the tile is world u = i (mod W); the face's
                # u range starts at x0*k, so wrap back into the box
                ky = y0 + ((j + 0.5 - y0 * k) % H) / k
                for i in range(W):
                    kx = x0 + ((i + 0.5 - x0 * k) % W) / k
                    tex = None
                    for cx0, cy0, cx1, cy1, t in cells:
                        if cx0 <= kx < cx1 and cy0 <= ky < cy1:
                            tex = t; break
                    if tex is None:
                        continue
                    pix = texture(tex)
                    if pix is None:
                        continue
                    sx = int(kx / KEX_SOURCE_UNITS_PER_TEXEL) % 64
                    sy = int(ky / KEX_SOURCE_UNITS_PER_TEXEL) % 64
                    o = (j * W + i) * 4
                    buf[o:o + 4] = pix[(sy * 64 + sx) * 4:(sy * 64 + sx) * 4 + 4]
            path = os.path.join(outd, 'f%d.png' % tile)
            _png.write(path, W, H, bytes(buf))
            out[tile] = 'kexart/fcomp/f%d.png' % tile
    if verbose:
        print("  flat composites: %d mosaic floors and ceilings baked into tiles" % len(out))
    return out


def _bake_composites(kpf, comp_assign, outdir, verbose=True):
    """Write kexart/comp/<tile>.png for every composite; {tile: path}."""
    import kexpng as _png
    outd = os.path.join(outdir, 'kexart', 'comp')
    os.makedirs(outd, exist_ok=True)
    cache = {}
    out = {}
    with zipfile.ZipFile(kpf) as z:
        have = {n.lower(): n for n in z.namelist()}
        def texture(name):
            if name in cache:
                return cache[name]
            real = have.get(name.lower())
            if real is None:
                cache[name] = None; return None
            tmp = os.path.join(outd, '_src.png')
            open(tmp, 'wb').write(z.read(real))
            try:
                w_, h_, pix = _png.read(tmp)
            finally:
                os.remove(tmp)
            if (w_, h_) != (64, 64):
                pix = _png.resize(w_, h_, pix, 64, 64); w_, h_ = 64, 64
            cache[name] = pix
            return pix
        for key, tile in sorted(comp_assign.items(), key=lambda kv: kv[1]):
            phase, rows = key
            source_h = sum(h for _o, h, _t in rows)
            H = max(1, int(round(source_h / KEX_SOURCE_UNITS_PER_TEXEL)))
            buf = bytearray(64 * H * 4)
            for r in range(H):
                kr = min(source_h - 1e-6,
                         (r + 0.5) * KEX_SOURCE_UNITS_PER_TEXEL)
                tex = next((name for off, h, name in rows if off <= kr < off + h), None)
                pix = texture(tex) if tex is not None else None
                if pix is None:
                    continue
                # Texture row zero sits on the 128-unit world grid: at world z
                # the row is (-z/2) mod 64, and z = top - kr.
                src = int(math.floor((kr - phase) / KEX_SOURCE_UNITS_PER_TEXEL)) % 64
                buf[r * 256:(r + 1) * 256] = pix[src * 256:(src + 1) * 256]
            # yrepeat is an integer, 11 for 128/12 = 10.67, so a wall shows
            # 11/10.67 of a two-units-per-texel tile over its height: a
            # repeating texture never minds, but a composite cut to the face
            # would not land exactly on its bottom band. Bake at that ratio so
            # the whole face lands on the whole wall.
            ratio = KEX_YREPEAT / (128.0 / KEX_UNITS_PER_TEXEL)
            H2 = max(1, int(round(H * ratio)))
            if H2 != H:
                buf2 = bytearray(64 * H2 * 4)
                for r2 in range(H2):
                    r = min(H - 1, int(r2 / ratio))
                    buf2[r2 * 256:(r2 + 1) * 256] = buf[r * 256:(r + 1) * 256]
                buf = buf2; H = H2
            path = os.path.join(outd, 'c%d.png' % tile)
            _png.write(path, 64, H, bytes(buf))
            out[tile] = 'kexart/comp/c%d.png' % tile
    if verbose:
        print("  composites: %d mosaic wall faces baked into tiles" % len(out))
    return out

# Build maps a floor or ceiling texture at 16 world units per texel (polymost's
# ft *= 1/16; 8 with the expand bit), a fixed scale no field can change, so a
# 64-texel tile repeats every 1024 units -- 171 Kex units, where Kex's compiled
# UV mapping repeats every 128. A flat's texture therefore gets a SECOND tile
# of 128*WORLD/16 texels a side -- the
# image box-filtered down, for the classic renderer -- whose hires replacement
# is the full PNG: the tile's footprint sets the repeat (48 texels * 16 = 768
# units = 128 Kex units) and the image keeps every pixel under polymost. Walls
# keep the original tile; a texture used on both gets both.
KEX_FLAT_TEXELS = KEX_TEXTURE_REPEAT * WORLD / 16.0

def _wall_bands(m, sects, walls, ranges, tilebase):
    """The texture and texture origin of each wall's drawn parts.

    Build draws a two-sided wall in two parts and hands them to different
    walls: the part above the opening wears THIS wall's picnum, the part
    below it -- a step's riser -- wears the NEXT wall's, the one on the far
    side. Kex keeps each band with the sector it faces: the lintel above a
    doorway is a solid face of the room, the riser of a step a solid face of
    the lower sector. So a wall's picnum must be its own sector's band above
    the portal when there is one, and otherwise the far sector's band below
    it. Taking the first solid face on the edge -- and, on an edge that is
    all portal, the portal face itself -- gave every riser the texture of
    whichever face came first, and lately the sky.

    Kex also maps its textures in world space: the tile seams sit on
    multiples of 64 units in z (on the faces whose top is off that grid,
    703 interior tile vertices of KARNAK's are on it and 8 are not) and along
    the axes. Build hangs a texture from a reference height -- the ceiling
    for a solid wall, the far ceiling for the part above an opening, the
    near floor for a riser -- and from the wall's first point. ypanning and
    xpanning, in 256ths of the tile and in texels, put the seams back on the
    grid. A wall drawn in both parts has one ypanning; the taller part wins.
    """
    sec_of = {}
    for si, (st, num) in enumerate(ranges):
        for i in range(num):
            sec_of[st + i] = si
    T = float(KEX_TILE_Z)
    for wi, w in enumerate(walls):
        fis = w.get('_fis'); portal = w.get('_portal'); ff = w.get('_ff')
        si = sec_of.get(wi)
        if si is None or sects[si] is None:
            continue
        A = sects[si]
        nw = w['nextwall']
        if nw < 0 or portal is None or isinstance(portal, _Cut) or fis is None:
            ref = A['ceilingz']
        else:
            n = walls[nw]; B = sects[sec_of[nw]]
            lo, hi = _zspan(m, portal)
            def band(faces, above):
                best = None
                for fi in faces or ():
                    if isinstance(fi, _Cut): continue
                    f = m.faces[fi]
                    if f.link >= 0 or f.tileStart < 0: continue
                    flo, fhi = _zspan(m, f)
                    ok = flo >= hi - 1.0 if above else fhi <= lo + 1.0
                    if ok and (best is None or (fhi - flo) > best[0]):
                        best = (fhi - flo, fi)
                return best[1] if best else None
            above_fi = band(fis, True)
            below_fi = band(n.get('_fis'), False)
            above = m.faces[above_fi] if above_fi is not None else None
            below = m.faces[below_fi] if below_fi is not None else None
            if above is not None:
                w['picnum'] = _pic(m, above, tilebase)
                w['_kex_light_face'] = above_fi
            elif below is not None:
                w['picnum'] = _pic(m, below, tilebase)
                w['_kex_light_face'] = below_fi
            elif w['picnum'] < 0 and ff is not None:
                w['picnum'] = _pic(m, ff, tilebase)
            upper = B['ceilingz'] - A['ceilingz']      # > 0: our part above the opening
            riser = B['floorz'] - A['floorz']          # > 0: their riser, in our texture
            ref = B['ceilingz'] if upper >= riser else A['floorz']
            # A mosaic band gets a composite too, when this wall's picnum
            # serves only that one part: the part above the opening hangs
            # from our ceiling (cstat 4), the far riser from their floor.
            KZ = -1.0 / (SCALE * WORLD)
            if above is not None and riser <= 0 and upper > 0 and not A['ceilingheinum'] and not B['ceilingheinum']:
                key = _face_rows(m, above)
                if key is not None:
                    flo, fhi = _zspan(m, above)
                    if abs(fhi - A['ceilingz'] * KZ) <= 1 and abs(flo - B['ceilingz'] * KZ) <= 1:
                        w['_comp'] = key; w['cstat'] |= 4
            elif below is not None and above is None and upper <= 0 and riser > 0 and not A['floorheinum'] and not B['floorheinum']:
                key = _face_rows(m, below)
                if key is not None:
                    flo, fhi = _zspan(m, below)
                    # their riser runs from their floor (bottom) up to ours (top)
                    if abs(fhi - A['floorz'] * KZ) <= 1 and abs(flo - B['floorz'] * KZ) <= 1:
                        w['_comp'] = key; w['cstat'] &= ~4
            if 'overpicnum' in w:
                # a grate or force field hangs from the higher ceiling
                # (polymost's masked pass) and is the part that shows
                ref = max(A['ceilingz'], B['ceilingz'])
        w['ypanning'] = 0 if '_comp' in w else int(round((ref % T) / T * 256.0)) % 256
        # along the wall, on the axis-aligned ones (the Kex mapping of a
        # diagonal is not this simple, so those keep their start)
        p2 = walls[w['point2']]
        dx, dy = p2['x'] - w['x'], -(p2['y'] - w['y'])        # back to Kex y
        kx, ky = w['x'], -w['y']
        if dy == 0 and dx != 0:
            xpan = kx if dx > 0 else -kx
        elif dx == 0 and dy != 0:
            xpan = ky if dy > 0 else -ky
        else:
            xpan = 0
        # xrepeat shows a whole multiple of 8 texels across the wall, so a
        # wall not a multiple of 8 units long is stretched by up to 4 texels
        # -- 33,000 walls by a texel or more, 12,000 by three -- and with the
        # seams pinned at the start all of it lands at the far join. Shift
        # the texture by half the error so each end carries half.
        L = (dx * dx + dy * dy) ** 0.5
        xpan /= KEX_SOURCE_UNITS_PER_TEXEL
        xpan -= (w['xrepeat'] * 8 - L / KEX_SOURCE_UNITS_PER_TEXEL) / 2.0
        w['xpanning'] = int(round(xpan)) % 64
    for w in walls:
        w.pop('_fis', None); w.pop('_portal', None); w.pop('_ff', None)


def write_lit(path, m, sects, walls, ranges):
    """Write the KEX baked-light field with emitted-surface provenance.

    KEXLIT4 keeps every source face's RGB vertices and tile triangles.  The
    older position-only table averaged colours where a floor, wall, or portal
    band shared a vertex, so Polymost could blend unrelated surfaces.  KEXLIT3
    fixed that provenance but still estimated the colour between vertices by
    distance.  The remaster rasterizes each authored tile as two triangles, so
    preserve that exact connectivity too.  Each generated Build surface points
    at the source face it renders; the MAP geometry remains untouched.
    """
    keep, remap = sector_remap(sects, ranges)
    wremap = {}
    for old in keep:
        start, count = ranges[old]
        for i in range(count):
            wremap[start + i] = len(wremap)

    surfaces = []
    for old in keep:
        s = sects[old]
        base = remap[old] * 2
        surfaces.append((2, base, s.get('_kex_light_floor_face', -1)))
        surfaces.append((2, base + 1, s.get('_kex_light_ceiling_face', -1)))
    for old in keep:
        start, count = ranges[old]
        for i in range(count):
            w = walls[start + i]
            base = wremap[start + i] * 2
            surfaces.append((1, base, w.get('_kex_light_face', -1)))
            if 'overpicnum' in w:
                surfaces.append((1, base + 1, w.get('_kex_mask_light_face', -1)))

    face_ids = sorted({face for _kind, _id, face in surfaces
                       if 0 <= face < m.nface and _lit(m.faces[face])})
    samples = []
    triangles = []
    for face in face_ids:
        f = m.faces[face]
        sample_first = len(samples)
        for vi in range(f.tileVertStart, min(f.tileVertEnd + 1, len(m.verts))):
            x, y, z, (r, g, b, _a) = m.verts[vi]
            # Keep duplicate positions separate.  Kex's mesh can deliberately
            # split a position into distinct vertices with distinct lighting;
            # averaging those was harmless for the campaign's grayscale data,
            # but cannot reproduce the renderer's per-triangle interpolation.
            samples.append((face, vi, int(round(x * WORLD)), int(round(-y * WORLD)),
                            int(round(-z * SCALE * WORLD)), r, g, b))

        if f.tileStart < 0:
            continue
        for ti in range(f.tileStart, f.tileEnd + 1):
            corners = [c for c in m.tileCorners[ti] if c != 255]
            if len(corners) not in (3, 4):
                continue
            if any(f.tileVertStart + c < f.tileVertStart
                   or f.tileVertStart + c >= min(f.tileVertEnd + 1, len(m.verts))
                   for c in corners):
                continue
            ids = [sample_first + c for c in corners]
            triangles.append((face, ids[0], ids[1], ids[2]))
            if len(ids) == 4:
                triangles.append((face, ids[0], ids[2], ids[3]))

    with open(path, 'w') as f:
        f.write("KEXLIT4 %d %d %d\n" % (len(samples), len(surfaces), len(triangles)))
        for face, _vi, x, y, z, r, g, b in samples:
            f.write("%d %d %d %d %d %d %d\n" % (face, x, y, z, r, g, b))
        for kind, ident, face in sorted(surfaces):
            f.write("%d %d %d\n" % (kind, ident, face))
        for face, a, b, c in triangles:
            f.write("%d %d %d %d\n" % (face, a, b, c))
    return len(samples)


def _split_long(edge_cache):
    """Split every edge that is too long for Build's 8-bit xrepeat.

    xrepeat follows the wall's length, one texel per two Kex units, and tops out at
    255 -- so a wall over 4080 Kex units would be drawn with 255 and its texture
    smeared by the ratio. No retail edge exceeds that corrected limit, but
    custom maps may; cut those into equal pieces from the lexically smaller end
    so both sides of a portal edge produce the same points and still pair by
    coordinate afterwards.
    """
    MAXLEN = 255.0 * 8.0 * KEX_UNITS_PER_TEXEL / WORLD
    for si, es in list(edge_cache.items()):
        out = []
        for k, fis in es:
            x1, y1, x2, y2 = k
            L = ((x2-x1)**2 + (y2-y1)**2) ** 0.5
            n = int(L // MAXLEN) + 1
            if n < 2:
                out.append((k, fis)); continue
            a, b = sorted(((x1, y1), (x2, y2)))
            pts = [a] + [(round(a[0] + (b[0]-a[0]) * j / n, 1), round(a[1] + (b[1]-a[1]) * j / n, 1))
                         for j in range(1, n)] + [b]
            if (x1, y1) != a:
                pts.reverse()
            for j in range(n):
                out.append(((pts[j][0], pts[j][1], pts[j+1][0], pts[j+1][1]), fis))
        edge_cache[si] = out


def _split_tjunctions(m, edge_cache, verbose=False):
    """Split every sector edge at vertices that other sectors put on it.

    Build pairs a portal by exact edge correspondence: sector A's wall from P to
    Q is only a portal into B when B has a wall from Q to P. Kex has no such
    rule, so it happily runs one long wall alongside a neighbour whose side is
    broken into several -- a T-junction. When that happens the reversed edge is
    not found and the portal is dropped, which turns a doorway into a solid
    wall. It is not a rare corner case: 978 of the 10602 portals in the first
    eight retail maps, 9.2%, were being lost exactly this way.

    Splitting A's long edge at B's intermediate vertices makes both sides agree.
    The vertices come from every sector's ring, bucketed into a grid so each
    edge only tests the ones near it.
    """
    CELL = 256.0
    grid = {}
    for es in edge_cache.values():
        for k, _f in es:
            for (x, y) in ((k[0], k[1]), (k[2], k[3])):
                grid.setdefault((int(x // CELL), int(y // CELL)), set()).add((x, y))

    nsplit = 0
    for si, es in edge_cache.items():
        out = []

        def append_buildable(a, b, c, d, fis):
            """Keep only edges that survive the writer's integer grid.

            Contour cuts are allowed to end at fractional Kex coordinates.
            A nearby vertex from an unrelated sector can then make the
            T-junction pass split off a perfectly real, but sub-unit, tail.
            write_build() rounds both endpoints to the Build map's integer
            coordinates, turning that tail into a zero-length wall.  When it
            was the first edge of a contour-split sector, _surface() had no
            direction from which to construct its slope and flattened the
            whole piece.  Do this test in the same coordinate space as the
            writer, while retaining the unsnapped endpoint for the following
            edge, so the remaining segment is still the authored contour.
            """
            if int(round(a)) == int(round(c)) and int(round(b)) == int(round(d)):
                return False
            out.append(((a, b, c, d), fis))
            return True

        for k, fis in es:
            x1, y1, x2, y2 = k
            dx, dy = x2 - x1, y2 - y1
            L2 = dx*dx + dy*dy
            if L2 <= 1e-9:
                out.append((k, fis)); continue

            # every grid cell the segment touches
            cand = set()
            steps = int(max(abs(dx), abs(dy)) // CELL) + 2
            for i in range(steps + 1):
                t = i / float(steps)
                cx, cy = int((x1 + dx*t) // CELL), int((y1 + dy*t) // CELL)
                for ox in (-1, 0, 1):
                    for oy in (-1, 0, 1):
                        cand |= grid.get((cx+ox, cy+oy), set())

            hits = []
            for (vx, vy) in cand:
                ex, ey = vx - x1, vy - y1
                # colinear, and strictly inside the segment
                if abs(ex*dy - ey*dx) > 1.0 * (L2 ** 0.5):
                    continue
                t = (ex*dx + ey*dy) / L2
                # Not within one Build unit (1/16 here) of either end: a
                # contour cut's crossing point can land a hair from a
                # neighbour's corner, and the sliver that split would leave
                # rounds to a zero-length wall -- which, as a sloped
                # sector's first wall, makes the engine draw the sector flat
                # (KARNAK's front door and towers were, before this).
                L = L2 ** 0.5
                if t * L < 1.0 / WORLD or (1.0 - t) * L < 1.0 / WORLD:
                    continue
                hits.append((t, vx, vy))
            if not hits:
                out.append((k, fis)); continue

            hits.sort()
            px, py = x1, y1
            for _t, vx, vy in hits:
                if (vx, vy) == (px, py) or ((vx-px)**2 + (vy-py)**2) < 1.0 / (WORLD * WORLD):
                    continue
                append_buildable(px, py, vx, vy, fis)
                px, py = vx, vy
                nsplit += 1
            append_buildable(px, py, x2, y2, fis)
        edge_cache[si] = out
    if verbose and nsplit:
        print("  T-junctions: %d edge splits so both sides of a portal agree" % nsplit)
    return nsplit


def _drop_collapsed_build_edges(edge_cache):
    """Remove edges which disappear on the integer Build coordinate grid.

    Splitting for slopes, light fields, T-junctions, and texture length uses
    Kex's fractional coordinates, while the Build writer stores a rounded
    integer coordinate.  An otherwise valid fractional tail can therefore
    become a point only at write time.  Dropping it here keeps the adjacent
    edges joined, avoids a phantom zero-length wall, and crucially leaves a
    contour-split slope with a usable first wall.
    """
    removed = 0
    for si, edges in edge_cache.items():
        kept = []
        for k, fis in edges:
            if int(round(k[0])) == int(round(k[2])) and int(round(k[1])) == int(round(k[3])):
                removed += 1
                continue
            kept.append((k, fis))
        edge_cache[si] = kept
    return removed


# Floater variants constructed by kexWorld::ReadEvents(), mapped to (travel
# height, phase, spawn offset) in Kex units and degrees.  The mover class has
# latent Spawn() cases for 43, 46 and 47, and the event enum also names 42 and
# 69, but the remaster's loader never calls SetupFloatingPlatforms() for any
# of those five types.  They must therefore remain static here too.
#
# The retail mover does not bob one floor: SetupFloatingPlatforms() swaps the
# event's source/parameter sectors, then its tick moves the original source
# CEILING and the parameter sector's FLOOR by the same cosine delta. USER
# takes the original event/source sector number as its degree phase; RAND
# chooses a runtime phase in [-256,+256]. The converter writes every surviving
# split piece of both surfaces to kexfloat.txt for the native mover.
KEX_FLOAT_RANDOM_PHASE = 32767
KEX_FLOATERS = {
    'FLOATER_128':                 (128,    0,    0),
    'FLOATER_128_ANG_120':         (128,  120,    0),
    'FLOATER_128_ANG_180':         (128,  180,    0),
    'FLOATER_64_ANG_USER':          (64, 'user',   0),
    'FLOATER_512_ANG_USER':        (512, 'user',   0),
    'FLOATER_128_ANG_RAND_256':    (128, 'random', 0),
}


# Build channels are the low three digits of a tag (runlist_AllocChannel gets
# hitag % 1000), and a channel's state starts out as that number. Kex tags run
# past 1000 -- KILENTRY pairs artifact triggers 1..6 with doors 1001..1006,
# SUNKEN's scripts fire 1001 and 1002 beside lifts on 2 -- and folding them
# with % 1000 welded those together. So the channels are allotted per map:
# tags under 1000 keep their number, and every tag from 1000 up takes a free
# number from 999 downward. The engine gets the tags-over-1000 part of the
# table as kexchan.txt, for the scripts it runs that fire tags by name.
# Kex tag 0 is a tag like any other to the events that fire and listen on
# channels: KILARENA's five walk-in regions and its trigger-only door all
# carry 0. Build's hitag 0 gives each effect a private channel, which would
# leave that door shut for good, so tag 0 on those event types is allotted a
# real channel under this pseudo-tag. Plain doors, keys and lifts keep 0 --
# a shared channel would open every door in the level together.
KEX_TAG0 = 100000
KEX_DEAD_TRAP = 100001
KEX_SHARED0 = ('REMOTE_TRIGGER', 'FLOOR_TRIGGER_PLATE', 'SWITCH1', 'SWITCH2',
               'SWITCH3_WOOD', 'SWITCH4_METAL', 'DOOR_TRIGGER_ONLY',
               'DOOR_OPEN_ONLY_MESSAGE', 'FLOOR_LOWER', 'DOOR_CLOSE', 'DOOR_CLOSE_SLOW')
# SendRemoteTrigger() clears its stepped-on type-50 event only if a matching
# target event has one of these mover types.  The source enum values matter;
# their readable names overlap unrelated Build effects.
KEX_REMOTE_ONE_SHOT_TYPES = (7, 8, 9, 21, 22, 24)

def _channels(m):
    # -1 alone means "no tag". Other negative values are real, signed Kex
    # tags: MAGMA has a REMOTE_TRIGGER and fireball spawner both tagged -5449,
    # and the retail executable sign-extends and compares those fields exactly.
    tags = set(e[2] for e in m.events if e[2] > 0 or e[2] < -1)
    traps = [a for a in m.actors if a.type in KEX_TRAP_ACTORS]
    if (any(e[2] == 0 and KEX_EVENTS.get(e[0]) in KEX_SHARED0 for e in m.events)
            or any(a.tag == 0 for a in traps)):
        tags.add(KEX_TAG0)
    tags |= set(a.tag for a in traps if a.tag > 0 or a.tag < -1)
    # FireActorEventFromTag(tag) removes a sector's active spawner factory
    # when *any* source actor in that sector has actor.tag+1 == tag. Preserve
    # those channels too, including tag zero through its shared pseudo-key.
    factory_sectors = {a.sector for a in traps
                       if a.type != 127 and a.sector >= 0}
    for a in m.actors:
        if a.sector not in factory_sectors:
            continue
        stop_tag = a.tag + 1
        if stop_tag == -1:             # -1 remains KEX's no-tag sentinel
            continue
        tags.add(KEX_TAG0 if stop_tag == 0 else stop_tag)
    # A non-beam trap tagged -1 is dormant, not attached to an arbitrary
    # real channel. Reserve one private channel for those records. The old
    # hard-coded 999 collided with MAGMA tag 6001 and SETARENA tag 1002.
    if any(a.tag == -1 and a.type != 127 for a in traps):
        tags.add(KEX_DEAD_TRAP)
    chanmap = {t: t for t in tags if 0 < t < 1000}
    used = set(chanmap.values())
    free = 999
    # Allocate high tags first to keep established generated channel numbers
    # stable, then the rare signed-negative tags.
    for t in (sorted(t for t in tags if t >= 1000)
              + sorted(t for t in tags if t < -1)):
        while free in used:
            free -= 1
        chanmap[t] = free
        used.add(free)
    return chanmap

def _chan(m, tag):
    if tag in (-1, 0):
        return 0
    if not hasattr(m, 'kex_chanmap'):
        m.kex_chanmap = _channels(m)
    return m.kex_chanmap.get(tag, tag % 1000)


def _neighbours(sects, walls, ranges, si):
    """The distinct live sectors this piece opens onto."""
    start, num = ranges[si]
    out = set()
    for i in range(num):
        ns = walls[start+i]['nextsector']
        if 0 <= ns < len(sects) and sects[ns]:
            out.add(ns)
    return out


def _resolve_events(m, sects, walls, ranges, src, switch_pairs=None, tilebase=0):
    """Turn each sector's Kex event into the Build tags that DO the same thing.

    Build encodes a sector effect as lotag (speed*1000 + effect) and hitag
    (key*1000 + channel), and runlist_ProcessSectorTag() builds the movers at
    load. The Kex event's tag is the channel and its number names the effect
    per KEX_EVENTS. The correspondences below are behavioural, each checked
    against the effect's own code in runlist.cpp:

      DOOR_NORMAL/KEY1-4 -> 1: press-to-open ceiling door, auto-close, the key
        carried in hitag thousands. DOOR_TRIGGER_ONLY and OPEN_ONLY_MESSAGE
        (open only by channel; the remaster shows a "find another way" message
        on use) -> 24, the channel-trigger-only ceiling door.
      DOOR_CLOSE/_SLOW -> 75, the ceiling that travels to the floor.
      REMOTE_TRIGGER -> an invisible lotag-87 Kex dispatcher. It is the
        walk-in trigger region and the most common event in the game; it resets
        tag+1 wall switches/live plates and may retire after firing a mover.
        An earlier version read it as Build 50, a floor elevator, and sank
        436 sectors at load.
      FLOOR_TRIGGER_PLATE -> a native lotag-84 Kex plate: it begins 16
      source units raised, sinks at its authored speed, and fires only on
      arrival.
      FLOOR_LOWER -> 8 (permanent floor lower), FLOOR_LOWER_WAIT_RAISE and
      FLOOR_LOWER_ACTIVE -> 9 (lift down), FLOOR_RAISE_WAIT_LOWER -> 12
      (lift up). DROP_PAD is a native paired controller: it moves the
      parameter floor and original-sector ceiling together after its authored
      delay/acceleration, so kexpad.txt records every split surface instead
      of trying to approximate it with a Build touchplate. Generic Kex movers
      that advance four units at 60 Hz use exact 768-Build-z updates; the
      two-unit plate uses 384.
      EXPLODE -> 35, the destructible sector (the bomb wall).
      SWITCH1-4 -> nothing on the sector: the switch lives on a WALL in Build
        (wall lotag 7, runlist_ProcessWallTag), handled below.

    Every mover Build builds at load walks the neighbour list for the height it
    travels to and asserts it found one, so each mapping is guarded on the
    neighbour that effect actually needs; a door with nowhere to open stays
    static rather than crashing the load or diving out of the map.

    SCRIPTED_MOVE becomes a lotag 64 sprite naming the sector and its tag; the
    moves themselves are the level scripts', transcribed in the engine.

    FLOATER is emitted separately as paired moving surfaces in kexfloat.txt.
    SCRIPT/SCRIPT_REPEATABLE, ARTIFACT_TRIGGER/_SPAWN and ACTOR_TRIGGER use
    native Kex marker paths below; no retail event type is deliberately left
    unsupported here."""
    for si, s_ in enumerate(sects):
        if s_ is None:
            continue
        evs = s_.pop('kexevent', None)
        if not evs:
            continue
        for ev in evs:
          _resolve_one(m, sects, walls, ranges, src, switch_pairs, tilebase, si, s_, ev)


def _resolve_one(m, sects, walls, ranges, src, switch_pairs, tilebase, si, s_, ev):
        etype, _esect, etag, eparam = ev
        name = KEX_EVENTS.get(etype)
        if name is None:
            raise ValueError('unsupported Kex event type %d in sector %d' % (etype, si))
        if name == 'NONE':
            return
        if etype == 63:
            s_['kexevent63'] = True     # the Ramses pads; see convert()
        chan = _chan(m, KEX_TAG0 if (etag == 0 and name in KEX_SHARED0) else etag)
        nbs = _neighbours(sects, walls, ranges, si)
        # Build z grows DOWNWARD: a neighbour ceiling above ours has a smaller
        # ceilingz, a neighbour floor below ours a greater floorz.
        ceil_above  = any(sects[n]['ceilingz'] < s_['ceilingz'] for n in nbs)
        floor_below = any(sects[n]['floorz'] > s_['floorz'] for n in nbs)
        floor_above = any(sects[n]['floorz'] < s_['floorz'] for n in nbs)

        lotag = hitag = 0
        if name in ('DOOR_NORMAL', 'DOOR_KEY1', 'DOOR_KEY2', 'DOOR_KEY3',
                    'DOOR_KEY4') and ceil_above:
            lotag = 2001
            key = {'DOOR_KEY1': 1, 'DOOR_KEY2': 2,
                   'DOOR_KEY3': 3, 'DOOR_KEY4': 4}.get(name, 0)
            # The thousands are a BITMASK of keys, not a number: the DOS maps
            # carry 1, 4, 6, 7, 8 and 9 there (power 1, time 2, war 4, earth
            # 8, and doors wanting several), and runlist.cpp tests
            # (keys & (thousands << 12)) == mask. "3 * 1000" asked the War
            # door for the Power and Time keys and "4 * 1000" the Earth door
            # for the War key, on every level with those doors.
            hitag = ((1 << (key - 1)) if key else 0) * 1000 + chan
        elif name in ('DOOR_TRIGGER_ONLY', 'DOOR_OPEN_ONLY_MESSAGE') and ceil_above:
            lotag, hitag = 2024, chan
            # OPEN_ONLY: opened once by its tag (a script, a boss, an
            # artifact), it stays open; Build's 24 would close it 60 ticks
            # later. A lotag 73 sprite tells the engine to leave out the
            # pause switch (init.cpp, runlist.cpp).
            if name == 'DOOR_OPEN_ONLY_MESSAGE':
                s_['kexhold'] = True
                # UseWallSpecial() does not open this door on a direct use:
                # it prints "$str_92" instead.  Preserve that Kex-only
                # interaction independently of the channel-driven mover.
                s_['kexsealeddoor'] = True
        elif name in ('DOOR_CLOSE', 'DOOR_CLOSE_SLOW'):
            # Kex authors these shut, ceiling on the floor, and opens them at
            # spawn so the tag can close them; Build's 75 travels between the
            # map's ceiling and its floor, so the ceiling is raised here to
            # the highest neighbour's -- open until fired. Ten of them across
            # seven levels stood shut for good before this.
            if ceil_above:
                s_['ceilingz'] = min(sects[n]['ceilingz'] for n in nbs)
                s_['ceilingheinum'] = 0
                s_['ceilingstat'] &= ~2
            # DOOR_CLOSE moves at four source units per Kex tick; the slow
            # variant moves at one.  The high speed band both drives the
            # native elevator and distinguishes the slow pair in runlist.cpp.
            lotag, hitag = (1075 if name == 'DOOR_CLOSE_SLOW' else 2075), chan
        elif name == 'FLOOR_TRIGGER_PLATE':
            # kexWorld::ReadEvents raises these plates by 16 source units at
            # map load. kexFloor then lowers them at two units/60 Hz and only
            # calls SendRemoteTrigger once it reaches this authored floor.
            # Build's generic touchplate only fires a channel, so lotag 84
            # carries the real target to the native KexPlate bridge instead.
            s_['kexplate'] = (etag, s_['floorz'])
            s_['floorz'] -= 16 * SCALE * WORLD
        elif name == 'REMOTE_TRIGGER':
            # Emitted as a native construction marker after the final sector
            # remap. A Build touchplate loses Kex's switch-reset and one-shot
            # dispatcher semantics.
            pass
        elif name == 'FLOOR_LOWER' and floor_below:
            lotag, hitag = 2008, chan
        elif name in ('FLOOR_LOWER_WAIT_RAISE', 'FLOOR_LOWER_ACTIVE') and floor_below:
            lotag, hitag = 2009, chan
            if name == 'FLOOR_LOWER_ACTIVE':          # down while the tag is on: no return
                s_['kexhold'] = True
        elif name == 'FLOOR_RAISE_WAIT_LOWER' and floor_below:
            # Kex authors the lift at the TOP, level with the ledge it serves,
            # and lowers it at spawn; Build's 12 starts low and rises to the
            # higher neighbour. So the floor drops to the lowest neighbour's
            # here and the authored height is the ledge it rises back to.
            # All 18 in the retail maps were untagged before this, the old
            # guard wanting a higher neighbour that never exists.
            s_['floorz'] = max(sects[n]['floorz'] for n in nbs)
            s_['floorheinum'] = 0
            s_['floorstat'] &= ~2
            lotag, hitag = 2012, chan
        elif name == 'DROP_PAD':
            # SetupFloatingPlatforms() swaps event sector/params, then moves
            # the parameter floor and original ceiling as one accelerated
            # pad. kexpad.txt drives the native paired controller below.
            pass
        elif name == 'EXPLODE':
            # A bomb wall. Build's 35 is a sector whose floor stands raised
            # -- the block's health is that height -- and drops to the
            # neighbour's floor when blown; Kex authors the volume open and
            # raises it at spawn. So the floor goes to the ceiling here,
            # solid until blown. The engine keeps the faces' own textures,
            # fires the tag and chains the siblings on it (object.cpp).
            s_['kexbomb'] = s_['floorz']       # where it drops to: a lotag 74 sprite at that z
            s_['floorz'] = s_['ceilingz']
            s_['floorheinum'] = 0
            s_['floorstat'] &= ~2
            lotag, hitag = 35, chan
        elif name in ('SWITCH1', 'SWITCH2', 'SWITCH3_WOOD', 'SWITCH4_METAL'):
            _place_switch(m, sects, walls, ranges, src, si, chan, etag, etype, eparam,
                          switch_pairs or {}, tilebase)
        elif name in KEX_FLOATERS:
            # Kept out of Build's one-surface lotag-95 bob. _float_surfaces()
            # records the paired source ceiling and parameter floor after all
            # sector splitting, and the native Kex mover advances them as one.
            pass
        elif name == 'SCRIPTED_MOVE':
            # A sector the level's script moves (the casket lids in TOMB and
            # TOMBEND are the only two). The move itself is scripted, so the
            # sprite carries the source sector paired by event.params too.
            # KEX moves the event sector's ceiling and that partner's floor,
            # not both surfaces of the event sector (kexScriptedMover::Tick).
            s_['kexscripted'] = (etag, eparam)
        elif name == 'ACTOR_TRIGGER':
            # Fires its tag when the creatures standing in it at load are all
            # dead: MAGMA's three, each holding one Mantis and opening a door
            # on the same tag. A lotag 66 sprite for the engine's KexTrigger.
            s_['kexactortrig'] = chan
        elif name in ('SCRIPT', 'SCRIPT_REPEATABLE'):
            # Runs the level script numbered tag+1 when entered. The three
            # SCRIPT sectors in the game (KILENTRY, KILARENA, SUNKEN) and the
            # Ramses pads are transcribed in the engine; the sprite names
            # the sector and the script (lotag 67, hitag = tag).
            s_['kexscript'] = etag
            if name == 'SCRIPT_REPEATABLE':
                s_['kexevent63'] = True
        elif name == 'ARTIFACT_SPAWN':
            # KILENTRY's six shrine destinations. Kex ArtifactEvent() only
            # reaches these after a player carrying tag-1 steps on its paired
            # ARTIFACT_TRIGGER pad: it consumes that artifact, fires tag
            # 1000 + artifact, then spawns a fresh pickup on this sector's
            # first floor triangle. The construction marker below preserves
            # that runtime-only spawn rather than planting an item at load.
            s_['kexartispawn'] = etag
        elif name == 'ARTIFACT_TRIGGER':
            s_['kexartitrig'] = etag
        s_['lotag'], s_['hitag'] = lotag, hitag


def _place_switch(m, sects, walls, ranges, src, si, chan, etag, etype, eparam, switch_pairs, tilebase=0):
    """A Kex switch is a sector event; a Build switch is a WALL.

    The switch graphic is one tile among several on one wall face of the event
    sector, and the event's params field packs the map-local texture indices of
    the off and on art as (off << 8) | on -- defs/switchInfo.txt carries the
    same pairing by name. Build wants that wall to carry lotag 7 and the OFF
    tile as picnum: runlist_ProcessWallTag() then builds a press switch that
    fires the channel and flips the face to picnum+1, which is why
    convert_install() pins every switchInfo ON texture to the tile right after
    its OFF."""
    off_idx = (eparam >> 8) & 0xFF
    sec = m.sectors[src[si]]
    start, num = ranges[si]
    for fi in range(sec.faceStart, min(sec.wallEnd + 1, m.nface)):
        f = m.faces[fi]
        if f.tileStart < 0:
            continue
        for t in range(f.tileStart, f.tileEnd + 1):
            tex = m.tiles[t][1]
            name = m.textures[tex] if tex < len(m.textures) else ''
            if tex == off_idx or name in switch_pairs:
                # the face's wall in this piece: the one whose midpoint lies
                # closest to the face's own centre -- among the walls that
                # SHOW the switch (picnum is the face's texture and the wall
                # is solid); a long face's centre can lie nearer a portal
                # wall of the same sector, which THOTH's did.
                fx = sum(m.verts[f.vertStart+k][0] for k in range(4)) / 4.0
                fy = sum(-m.verts[f.vertStart+k][1] for k in range(4)) / 4.0
                best = None
                for pass_ in (0, 1):
                    for i in range(num):
                        w = walls[start+i]; w2 = walls[w['point2']]
                        if pass_ == 0 and (w['picnum'] != _pic(m, f, tilebase) or w['nextsector'] >= 0):
                            continue
                        mx = (w['x'] + w2['x']) / 2.0
                        my = (w['y'] + w2['y']) / 2.0
                        d = (mx - fx*WORLD)**2 + (my - fy*WORLD)**2
                        if best is None or d < best[0]:
                            best = (d, i)
                    if best is not None:
                        break
                if best is not None:
                    w = walls[start+best[1]]
                    w['lotag'] = 7
                    w.pop('_comp', None)     # the switch art, not a composite of the face
                    w['hitag'] = chan
                    w['picnum'] = tex
                    # A FLOOR_TRIGGER_PLATE resets wall switches carrying
                    # tag+1 before it starts down. Keep the raw source tag
                    # alongside the translated Build channel so the native
                    # plate bridge can restore this exact off texture.
                    w['_kexswitchtag'] = etag
                    # The remaster chooses its press sound from the event
                    # class (200/201: switch.wav; 202/203: switch2.wav), not
                    # from its artwork or destination tag.
                    w['_kexswitchtype'] = etype
                    return True
    # No switch art on any wall of this piece. Only the source sector's first
    # piece falls back -- a stray extra switch on a split half would be worse
    # than none -- and it takes the first wall so the channel still exists and
    # the level stays completable.
    if num and si < m.nsector:
        walls[start]['lotag'] = 7
        walls[start].pop('_comp', None)
        walls[start]['hitag'] = chan
        walls[start]['_kexswitchtag'] = etag
        walls[start]['_kexswitchtype'] = etype
    return False


def _flat_sprites(m, sects, walls, ranges, src):
    """Sprites Build wants standing in a sector to declare what its floor IS.

    Water: Kex links an air sector's floor face (flags bit 6) to the water
    sector under it, whose ceiling face links back (bit 6 | bit 14) and whose
    inside is flagged underwater (bit 14). Build says the same thing with a
    lotag 98 sprite in the air sector whose hitag names the water sector
    (SetBelow + SnapSectors) and a lotag 99 in the water sector naming the air
    (SetAbove + kSectUnderwater) -- init.cpp:960-990. 256 such pairs across
    the retail maps were being read as solid floor: you could not dive
    anywhere.

    Lava and slime: a floor face flagged 0x20 burns and 0x80 stings in Kex;
    Build marks the sector with a lotag 96 sprite, hitag = damage*4.

    Returns (piece, lotag, partner_source_or_hitag) records; build_sprites()
    turns them into sprites once the sector remap exists."""
    out = []
    for si, s_ in enumerate(sects):
        if s_ is None:
            continue
        sec = m.sectors[src[si]]
        ff = m.faces[min(sec.wallEnd + 2, m.nface - 1)]
        cf = m.faces[min(sec.wallEnd + 1, m.nface - 1)]
        if ff.link >= 0 and (ff.flags & 0x40):
            out.append((si, 98, ff.link))          # air side: water below
        if cf.link >= 0 and (cf.flags & 0x40):
            out.append((si, 99, cf.link))          # water side: air above
        if ff.flags & 0x20:
            out.append((si, 96, 40))               # lava: damage 10
        elif ff.flags & 0x80:
            out.append((si, 96, 16))               # slime: damage 4
        # The SECTOR's own flag word (raw[9]): 256 is a submerged volume --
        # 404 of SUNKEN's 541 sectors, of which only the 74 with an air face
        # above carried a water marker, the rest dry to the engine -- and 16
        # is water to wade through (MARSH's 148 marsh floors). A lotag 82
        # sprite marks the sector under water without a surface partner
        # (init.cpp); Build's own 94 carries the wading depth. Bit 32 is the
        # retail editor's "No standing on edges": Kex collision must not use
        # this sector's floor/ceiling merely because the actor radius overlaps
        # its polygon. Lotag 83 preserves it for the filtered getzrange path.
        if (sec.raw[9] & 256) and not (cf.link >= 0 and (cf.flags & 0x40)):
            out.append((si, 82, 0))                # submerged, no surface here
        if sec.raw[9] & 16:
            out.append((si, 94, KEX_WADE_DEPTH))   # wading water
        if sec.raw[9] & 32:
            out.append((si, 83, 0))                # do not stand on this sector's edges
        if s_.get('kexscripted') is not None:
            tag, partner = s_['kexscripted']
            # Keep the source sector until build_sprites() has the final
            # old-to-new remap. xvel is otherwise unused by this
            # construction-only marker; +1 leaves zero as no partner.
            out.append((si, 64, tag, None,
                        (partner + 1 if partner >= 0 else 0)))
        if s_.get('kexplate') is not None:
            tag, target = s_['kexplate']
            # Construction marker: target is the floor height before the
            # remaster's mandatory 16-unit raised starting position.
            out.append((si, 84, tag, target))
        # Construction markers for Kex's ResetWallSwitchFromTag(). Build
        # normally only retains the translated channel, so save the original
        # event tag, class and actual off pic on every converted switch wall.
        start, num = ranges[si]
        for i in range(num):
            w = walls[start + i]
            if '_kexswitchtag' not in w:
                continue
            w2 = walls[w['point2']]
            out.append((si, 85, w['_kexswitchtag'], None, w['picnum'],
                        (w['x'] + w2['x']) * WORLD / 2,
                        (w['y'] + w2['y']) * WORLD / 2,
                        w.get('_kexswitchtype', 0)))
        if s_.get('kexhold'):
            out.append((si, 73, 0))                   # the mover holds where its tag put it
        if s_.get('kexsealeddoor'):
            out.append((si, 91, 0))                   # direct use says "This Door is Sealed"
        if s_.get('kexbomb') is not None:
            out.append((si, 74, 0, s_['kexbomb']))    # the bomb wall's floor once blown
        if s_.get('kexactortrig') is not None:
            out.append((si, 66, s_['kexactortrig']))  # kill-count trigger
        if s_.get('kexscript') is not None:
            out.append((si, 67, s_['kexscript']))     # level script sector
        if s_.get('kexartitrig') is not None and 1 <= s_['kexartitrig'] <= 6:
            out.append((si, 68, s_['kexartitrig'] - 1))   # shrine pad, hitag = artifact
        arti = s_.get('kexartispawn')
        # ArtifactEvent() uses the original source sector's first floor
        # triangle, not the converted polygon's centre. A split source sector
        # has one canonical first Build piece (the one retaining its source
        # index), so emit this invisible lotag-86 destination marker once.
        if (arti is not None and 1 <= arti <= 6 and si == src[si]
                and 0 <= src[si] < m.nsector):
            floor = m.faces[min(m.sectors[src[si]].wallEnd + 2, m.nface - 1)]
            if floor.vertStart + 2 < m.nvert:
                verts = [m.verts[floor.vertStart + i] for i in range(3)]
                x = int(round(sum(v[0] for v in verts) * WORLD / 3.0))
                y = int(round(-sum(v[1] for v in verts) * WORLD / 3.0))
                # Kex spawns 16 source units above the floor. Build's z axis
                # points the other way, hence the negative converted offset.
                z = int(round(-(sum(v[2] for v in verts) / 3.0 + 16) * SCALE * WORLD))
                out.append((si, 86, arti - 1, z, 0, x, y))
    return out


def _float_surfaces(m, sects, src):
    """Return every converted surface belonging to a Kex floating slab.

    Each record is ``(group, piece, ceiling, height, phase, offset)``. A Kex
    sector can become several Build sectors when slopes or baked lighting are
    split; all pieces of the event/source sector's ceiling and all pieces of
    its parameter/partner sector's floor must move or the platform tears open.
    A group is emitted only when both authored sides survived conversion.
    """
    pieces = {}
    for piece, source in enumerate(src):
        if piece < len(sects) and sects[piece] is not None:
            pieces.setdefault(source, []).append(piece)

    out = []
    group = 0
    for etype, esect, _etag, eparam in getattr(m, 'events', ()):
        spec = KEX_FLOATERS.get(KEX_EVENTS.get(etype))
        if spec is None:
            continue
        ceilings = pieces.get(esect, ())
        floors = pieces.get(eparam, ())
        if not ceilings or not floors:
            continue
        height, phase, offset = spec
        if phase == 'user':
            phase = esect                 # native event.params after its setup swap
        elif phase == 'random':
            phase = KEX_FLOAT_RANDOM_PHASE
        for piece in ceilings:
            out.append((group, piece, 1, height, phase, offset))
        for piece in floors:
            out.append((group, piece, 0, height, phase, offset))
        group += 1
    return out


def _drop_pad_surfaces(m, sects, src):
    """Paired surfaces for Kex DROP_PAD after SetupFloatingPlatforms() swaps
    its event sector with params: the parameter floor is the pad, while the
    original event sector's ceiling moves with it."""
    pieces = {}
    for piece, source in enumerate(src):
        if piece < len(sects) and sects[piece] is not None:
            pieces.setdefault(source, []).append(piece)
    out = []
    group = 0
    for etype, esect, etag, eparam in getattr(m, 'events', ()):
        if KEX_EVENTS.get(etype) != 'DROP_PAD':
            continue
        floors, ceilings = pieces.get(eparam, ()), pieces.get(esect, ())
        if not floors or not ceilings:
            continue
        for piece in floors:
            out.append((group, piece, 0, etag))
        for piece in ceilings:
            out.append((group, piece, 1, etag))
        group += 1
    return out


def _teleport_rows(m, remap, mapkey):
    """Return native Kex 71 -> 72 teleport rows for one converted map.

    TeleportEvent() scans the event array in order: event 71 is the stepped-on
    source, while the first matching event 72 names the arrival sector. It
    positions an actor at the centre of the destination floor face's first
    triangle and leaves its heading alone.
    """
    destinations = {}
    for etype, esect, etag, _eparam in getattr(m, 'events', ()):
        if etype == 72 and etag not in destinations:
            destinations[etag] = esect

    out = []
    for etype, esect, etag, _eparam in getattr(m, 'events', ()):
        if etype != 71:
            continue
        dsect = destinations.get(etag)
        if (dsect is None or esect not in remap or dsect not in remap
                or not (0 <= dsect < m.nsector)):
            continue
        floor = m.faces[min(m.sectors[dsect].wallEnd + 2, m.nface - 1)]
        if floor.vertStart + 2 >= m.nvert:
            continue
        verts = [m.verts[floor.vertStart + i] for i in range(3)]
        cx = int(round(sum(v[0] for v in verts) * WORLD / 3.0))
        cy = int(round(-sum(v[1] for v in verts) * WORLD / 3.0))
        cz = int(round(-sum(v[2] for v in verts) * SCALE * WORLD / 3.0))
        out.append((mapkey, remap[esect], remap[dsect], cx, cy, cz, -1))
    return out


def _remote_trigger_rows(m, remap, mapkey):
    """Return Kex type-50 source regions and their exact dispatch metadata.

    SendRemoteTrigger() always resets raw tag+1 switches/live type-23 plates,
    then fires the source tag. It retires the stepped-on source only after a
    matching target of type 7/8/9/21/22/24.  Event records with sector -1 are
    global data rather than regions an actor can enter, so they have no marker.
    ``mapkey`` makes verifier diagnostics identify the originating level.
    """
    one_shot_tags = {tag for etype, _sect, tag, _param in getattr(m, 'events', ())
                     if etype in KEX_REMOTE_ONE_SHOT_TYPES}
    out = []
    for etype, esect, tag, _param in getattr(m, 'events', ()):
        if etype != 50 or not (0 <= esect < m.nsector) or esect not in remap:
            continue
        tagkey = KEX_TAG0 if tag == 0 else tag
        out.append((mapkey, remap[esect], tag, _chan(m, tagkey),
                    int(tag in one_shot_tags)))
    return out


def _artifact_shrine_rows(m, remap, mapkey):
    """Return Kex ArtifactEvent's event-53 pads and event-54 spawn points.

    ArtifactEvent() consumes artifact ``tag - 1`` at a type-53 pad and spawns
    it at every matching type-54 event. Each spawn is the destination floor
    face's first triangle centre, raised 16 source units before Build's
    inverted/scaled z conversion. ``mapkey`` is retained with the rows so the
    verifier can report the originating converted level directly.
    """
    pads, spawns = [], []
    for etype, esect, etag, _eparam in getattr(m, 'events', ()):
        if not (1 <= etag <= len(KEX_ARTI_NAMES)) or esect not in remap:
            continue
        artifact = etag - 1
        if etype == 53:
            pads.append((mapkey, remap[esect], artifact))
            continue
        if etype != 54 or not (0 <= esect < m.nsector):
            continue
        floor = m.faces[min(m.sectors[esect].wallEnd + 2, m.nface - 1)]
        if floor.vertStart + 2 >= m.nvert:
            continue
        verts = [m.verts[floor.vertStart + i] for i in range(3)]
        x = int(round(sum(v[0] for v in verts) * WORLD / 3.0))
        y = int(round(-sum(v[1] for v in verts) * WORLD / 3.0))
        z = int(round(-(sum(v[2] for v in verts) / 3.0 + 16) * SCALE * WORLD))
        spawns.append((mapkey, remap[esect], artifact, x, y, z))
    return pads, spawns


# Build sound name -> the remaster wav that is the same sound, from a curated
# pass over both rosters: the Build names are mnemonics (anu_ = anubis, jon_ =
# the player, m_ = mummy, scrp_ = Selkis...), the remaster's are spelled out,
# and defs/actors + the anim scripts say which wav plays for which action on
# their side. Everything marked C below also passed an acoustic check -- a
# 6-second envelope cross-correlation between the demo VOC and the wav -- and
# blasted/menu_up2/jon_t_4 are bit-for-bit the same recording. The third field
# marks the loopers (the flamethrower jet and the stone-slide rumble), read
# off the original VOCs' own loop blocks. 69 Build sounds have no counterpart
# in the remaster (Am-mit, the drums, most ambience loops) and stay on their
# VOCs.
# Where Ramses appears. The remaster spawns Obj_Ramses from each hub script --
# TOMB_script.txt: SpawnActor("Obj_Ramses", -13, 3143, -734), TOMBEND_script:
# (0, 3136, -224 + 160) -- and the pad in front of him is the map's only
# event-63 (SCRIPT_REPEATABLE) sector. Build's own Ramses wants a lotag-61
# head sprite and the 0x8000 sector flag (sector effect 80) on the pad, which
# is exactly what these rows make the converter emit.
KEX_RAMSES = {
    'TOMB.MAP':    (-13, 3143, -734),
    'TOMBEND.MAP': (0, 3136, -64),
}

KEX_SOUNDS = {
    'alarm': ('sounds/ding02.wav', 0),
    'amb_lav1': ('sounds/mantisact02.wav', 0),
    'amb_lav2': ('sounds/mantisact03.wav', 0),
    'amb_ston': ('sounds/stonemov.wav', 1),
    'ammo': ('sounds/ammo.wav', 0),
    'anu_died': ('sounds/anubisdeath01.wav', 0),
    'k_andie2': ('sounds/anubisdeath02.wav', 0),  # A_AnubisDeath's second roll
    'anu_fire': ('sounds/anubisproj.wav', 0),
    'anu_hit': ('sounds/anubispain.wav', 0),
    'anu_icu': ('sounds/anubisact.wav', 0),
    'blasted': ('sounds/blasted2.wav', 0),
    # Names no VOC carries: sounds the remaster has and Build never did,
    # played by name through KexSound_Get() (kexbreak.cpp).
    'k_sw1': ('sounds/switch.wav', 0),              # Kex switch events 200/201
    'k_sw2': ('sounds/switch2.wav', 0),             # Kex switch events 202/203
    'k_fplate': ('sounds/fplate.wav', 0),           # Kex event 23 floor trigger plate
    # Mover.cpp distinguishes a stone door from a moving floor/lift.  The
    # original Build elevator sound slots do not retain that distinction on
    # every map, so object.cpp asks for these KEX-only aliases directly.
    'k_plat': ('sounds/platstart.wav', 1),           # kexFloor / kexLift movement
    'k_stonestop': ('sounds/stonestop.wav', 0),      # every kexDoor/Floor/Lift endpoint
    'k_padfall': ('sounds/platfall.wav', 0),         # kexDropPad landing
    'k_teleport': ('sounds/teleport.wav', 0),        # Kex event 71 -> 72 player arrival
    'k_trans': ('sounds/om_transmitter.wav', 0),   # a transmitter piece taken
    'k_mine': ('sounds/mineact.wav', 0),           # the mine arming
    'k_bould': ('sounds/boulder.wav', 0),          # boulder face reflection
    'k_bldhit': ('sounds/pbexplode.wav', 0),       # ... and its impact
    'k_forceoff': ('sounds/forcefieldoff.wav', 0), # Scepter disables a contacted field group
    'k_electrocute': ('sounds/pelectrocute.wav', 0), # PF_ELECTROCUTE nonfatal shock
    'k_jdie1': ('sounds/pdeath01.wav', 0),         # PF_ELECTROCUTE fatal shock
    'k_jdie2': ('sounds/pdeath02.wav', 0),         # lava/slime fatal contact
    'k_whit1': ('sounds/pwpain01.wav', 0),
    'k_whit2': ('sounds/pwpain02.wav', 0),
    'k_whit3': ('sounds/pwpain03.wav', 0),
    'k_whit4': ('sounds/pwpain04.wav', 0),
    'k_whit5': ('sounds/pwpain05.wav', 0),
    'k_dolph1': ('sounds/dolphin01.wav', 0),
    'k_dolph2': ('sounds/dolphin02.wav', 0),
    'k_gls1': ('sounds/glass01.wav', 0),           # A_TossActor shard floor bounce
    'k_gls2': ('sounds/glass02.wav', 0),
    'k_gls3': ('sounds/glass03.wav', 0),
    'k_wood1': ('sounds/wood01.wav', 0),
    'k_wood2': ('sounds/wood02.wav', 0),
    'k_wood3': ('sounds/wood03.wav', 0),
    'bomb_exp': ('sounds/explode03.wav', 0),       # Fx_Explosion01 frame zero
    'bomb_wat': ('sounds/explode02.wav', 0),       # Fx_Explosion01_Water
    'k_anmele': ('sounds/anubismelee.wav', 0),     # the Anubis' melee hit
    'k_anslap': ('sounds/anubisslap.wav', 0),      # the Anubis' damaging slap
    'k_setstp': ('sounds/setstomp02.wav', 0),      # Set's melee hit
    'k_quake': ('sounds/quake.wav', 0),             # TOMBEND's Ramses bier sinks
    'blood_1': ('sounds/flesh01.wav', 0),
    'blood_2': ('sounds/flesh02.wav', 0),
    'blood_3': ('sounds/flesh03.wav', 0),
    'blood_4': ('sounds/fleshbreak.wav', 0),
    'cat_clos': ('sounds/bastetscratch01.wav', 0),
    'cat_deth': ('sounds/bastetdeath01.wav', 0),
    'cat_icu': ('sounds/bastetact02.wav', 0),
    'cat_pain1': ('sounds/bastetpain01.wav', 0),
    'cat_pain2': ('sounds/bastetpain02.wav', 0),
    'cat_warp': ('sounds/bastetact01.wav', 0),
    'cat_xpld': ('sounds/bastetatk01.wav', 0),
    'co_fires': ('sounds/cstafffire.wav', 0),
    'co_new1': ('sounds/cstaffup.wav', 0),
    'fishbit2': ('sounds/fishatk.wav', 0),
    'fish_act': ('sounds/fishact.wav', 0),
    'fishdies': ('sounds/fishdeath.wav', 0),
    'ft_fires': ('sounds/fthrowfire.wav', 1),
    'ft_off2': ('sounds/fthrowdown.wav', 0),
    'ft_on2': ('sounds/fthrowup.wav', 0),
    'grn_boom': ('sounds/explode01.wav', 0),
    'grn_toss': ('sounds/bombthrow.wav', 0),
    'hawk_hrt': ('sounds/birdattack.wav', 0),
    # The crows' HAWK.SEQ (kexbirdseq.py) names these in place of the wasp's.
    'bird_act': ('sounds/birdact.wav', 0),
    'bird_atk': ('sounds/birdattack.wav', 0),
    'bird_die': ('sounds/birddeath.wav', 0),
    'bird_wng': ('sounds/birdflap01.wav', 0),
    'bird_wn2': ('sounds/birdflap02.wav', 0),
    # Queen's hatched AI_Seedling uses the optional SEEDLING.SEQ skin.
    'seed_atk': ('sounds/queenatk05.wav', 0),
    # Classic-arrow fallback only. The KEX factory path correctly uses
    # fballshoot.wav for both its fireball and laser projectile variants.
    'tr_arrow': ('sounds/sentryfire.wav', 0),
    # The Manacle's MANACLE.SEQ names these (build_manacle_seq).
    'brc_up':   ('sounds/braceletup.wav', 0),
    'brc_fire': ('sounds/braceletfire.wav', 0),
    'brc_chrg': ('sounds/shockold.wav', 0),
    'hawkfire': ('sounds/queenatk04.wav', 0),
    'item_key': ('sounds/keypickup.wav', 0),
    'item_reg': ('sounds/health.wav', 0),
    'item_spe': ('sounds/spawn.wav', 0),
    'item_use': ('sounds/select.wav', 0),
    'jon_die3': ('sounds/pdeath03.wav', 0),
    'jon_drwn': ('sounds/pdrown.wav', 0),
    'jon_fall': ('sounds/pfall.wav', 0),
    'jon_fdie': ('sounds/pxdeath.wav', 0),
    'jon_gasp': ('sounds/pwgasp.wav', 0),
    'jon_hit1': ('sounds/ppain01.wav', 0),
    'jon_hit2': ('sounds/ppain02.wav', 0),
    'jon_hit3': ('sounds/ppain03.wav', 0),
    'jon_jump': ('sounds/pjump05.wav', 0),
    'jon_pois': ('sounds/psizzle.wav', 0),
    'jon_scub': ('sounds/pwbreathe.wav', 0),
    'jon_t_4': ('sounds/yeah.wav', 0),
    'jon_t_6': ('sounds/pjump03.wav', 0),
    'jon_wade': ('sounds/splash02.wav', 0),
    # Mantis animation callbacks with no matching demo VOC name.
    'lav_act1': ('sounds/mantisact01.wav', 0),
    'lav_act4': ('sounds/mantisact04.wav', 0),
    'lav_deth': ('sounds/mantisrise.wav', 0),
    'lav_fire': ('sounds/mantisatk01.wav', 0),
    'lav_death': ('sounds/mantisdeath.wav', 0),
    'lav_hurt': ('sounds/mantispain.wav', 0),
    'lava_pow': ('sounds/explode02.wav', 0),
    'laz-fire': ('sounds/sentryfire.wav', 0),
    'm_chant0': ('sounds/mumact02.wav', 0),
    'm_deth2': ('sounds/mumdeath01.wav', 0),
    'm_deth_kex1': ('sounds/mumdeath01.wav', 0),
    'm_deth_kex2': ('sounds/mumdeath02.wav', 0),
    'm_deth_kex3': ('sounds/mumdeath03.wav', 0),
    'm_hits_u': ('sounds/mumslap01.wav', 0),
    'm_nstaff': ('sounds/mumatk02.wav', 0),
    'm_resr_x': ('sounds/mumact01.wav', 0),
    'm_skul_w': ('sounds/magicspawn.wav', 0),
    'm_watha': ('sounds/mumatk01.wav', 0),
    'mana1': ('sounds/health.wav', 0),
    'mana2': ('sounds/fhealth.wav', 0),
    'menu_up2': ('sounds/checkpoint.wav', 0),
    'mg_pow': ('sounds/m90fire.wav', 0),
    'p_click': ('sounds/click.wav', 0),
    'p_fire': ('sounds/pistolfire.wav', 0),
    # Authored prop-death sounds. Most vases use glass, the barrel uses wood,
    # and Obj_Vase09 uses flesh; exploding vases and the mine have no local
    # break sound (their explosion/countdown supplies its own audio).
    'pot_pc1': ('sounds/glassbreak.wav', 0),
    'pot_pc2': ('sounds/woodbreak.wav', 0),
    'q_cry': ('sounds/queenroar.wav', 0),
    'q_deth': ('sounds/queendeath01.wav', 0),
    'q_gurgle': ('sounds/queenact02.wav', 0),
    'q_ouch': ('sounds/spidpain.wav', 0),
    'q_sight': ('sounds/queenact01.wav', 0),
    'q_spit': ('sounds/queenatk01.wav', 0),
    'q_stumpy': ('sounds/queenatk03.wav', 0),
    'q_tail': ('sounds/queenatk04.wav', 0),
    'ra_on': ('sounds/rafire.wav', 0),
    'ra_sprit': ('sounds/setatk01.wav', 0),
    'roch_die': ('sounds/sentrydeath01.wav', 0),
    'roch_die2': ('sounds/sentrydeath02.wav', 0),
    'roch_icu': ('sounds/sentryact01.wav', 0),
    'roch_act2': ('sounds/sentryact02.wav', 0),
    'roch_act3': ('sounds/sentryact03.wav', 0),
    'roch_act4': ('sounds/sentryact04.wav', 0),
    'roch_gun': ('sounds/sentryfire.wav', 0),
    'roch_wld': ('sounds/sentrypain01.wav', 0),
    'scorp_z2': ('sounds/selkisfizz01.wav', 0),
    'scrp_cla': ('sounds/selkisatk01.wav', 0),
    'scrp_cry': ('sounds/selkisdeath.wav', 0),
    'scrp_hit': ('sounds/selkishurt.wav', 0),
    'scrp_icu': ('sounds/selkisact.wav', 0),
    'slk_at2': ('sounds/selkisatk02.wav', 0),
    'scrp_tal': ('sounds/ammo.wav', 0),
    'scrp_zp1': ('sounds/selkisfizz02.wav', 0),
    'scrp_zp2': ('sounds/selkisfizz01.wav', 0),
    'set_deth': ('sounds/setdeath.wav', 0),
    'set_gr1': ('sounds/setact01.wav', 0),
    'set_gr2': ('sounds/setatk02.wav', 0),
    'set_hit': ('sounds/setpain.wav', 0),
    'set_icu': ('sounds/setact02.wav', 0),
    'set_jump': ('sounds/setjump.wav', 0),
    'set_land': ('sounds/setland.wav', 0),
    'set_sprt': ('sounds/setquake.wav', 0),
    'set_step': ('sounds/setstomp01.wav', 0),
    'slash': ('sounds/bastetscratch01.wav', 0),
    'spi-hurt': ('sounds/spidpain.wav', 0),
    'spi_atak': ('sounds/spidchomp.wav', 0),
    'spi_jump': ('sounds/spidjump.wav', 0),
    'spi_newd': ('sounds/spiddeath.wav', 0),
    'spl_big': ('sounds/splash01.wav', 0),
    'spl_smal': ('sounds/splash02.wav', 0),
    'sw_away': ('sounds/machdown.wav', 0),
    'sw_hit': ('sounds/machflesh.wav', 0),
    'sw_out': ('sounds/machup.wav', 0),
    'sw_slash': ('sounds/machhit.wav', 0),
    'sw_swipe': ('sounds/machfire.wav', 0),
    'swi_foot': ('sounds/menu_select.wav', 0),
    'swi_ston': ('sounds/switch2.wav', 0),
    'swi_wtr1': ('sounds/switch3.wav', 0),
    'tele_1': ('sounds/spawn.wav', 0),
    'tele_2': ('sounds/bastetteleout.wav', 0),
    'tele_3': ('sounds/bastettelein.wav', 0),
    'tick1': ('sounds/timer.wav', 0),
    'tr_fire': ('sounds/fballshoot.wav', 0),
    'vatr_stp': ('sounds/stonestop.wav', 0),
    'wasp_buz': ('sounds/waspact.wav', 0),
    'rex-bite':  ('sounds/queenatk02.wav', 0),
    'rex_grgl':  ('sounds/queenact02.wav', 0),
    'rex-icu':   ('sounds/setquake.wav', 0),
    'ghost109':  ('sounds/setatk03.wav', 0),
    'wasp_die': ('sounds/waspdeath02.wav', 0),
    'wasp_dy1': ('sounds/waspdeath01.wav', 0),
    'wasp_hit': ('sounds/wasphit.wav', 0),
    'wasp_stg': ('sounds/waspatk.wav', 0),
    'weapon': ('sounds/fammo.wav', 0),
}


def build_angle(stored_deg):
    """Kex facing -> Build facing.

    The actor record stores a float at offset 16 which is NOT the angle the text
    source shows: the source's angle in radians is radians(90 - stored), checked
    against every sample map that has a turned actor.

    That source angle is a compass heading -- clockwise from +y -- not the usual
    counter-clockwise-from-+x. So a Kex heading phi points along
    (sin phi, cos phi); flipping y for Build turns that into (sin phi, -cos phi),
    which is Build angle phi - 90. With phi = 90 - stored, the whole chain
    collapses to simply -stored. Confirmed against TOMB, whose player then faces
    down the hall it actually opens onto.
    """
    return int(round(-stored_deg * 2048.0 / 360.0)) & 2047


def start_angle(walls, ranges, si, x, y):
    """Face the way the level opens, not whatever wall happens to be ahead.

    This is only the fallback for a custom map with no player actor. Retail
    starts use the record's compiled angle through build_angle(). Aim such a
    fallback at the nearest portal, then at the sector centre.
    """
    import math as _m
    start, num = ranges[si]
    best = None
    cx = cy = 0.0
    for i in range(num):
        w = walls[start+i]; nxt = walls[w['point2']]
        mx, my = (w['x']+nxt['x'])/2.0, (w['y']+nxt['y'])/2.0
        cx += w['x']; cy += w['y']
        if w['nextsector'] >= 0:
            d = (mx-x)**2 + (my-y)**2
            if best is None or d < best[0]: best = (d, mx, my)
    if best is None:
        if num: best = (0, cx/num, cy/num)
        else: return 0
    dx, dy = best[1]-x, best[2]-y
    if dx == 0 and dy == 0: return 0
    # Build angles: 0 = +x, 2048 = full turn, y grows downward.
    return int(round(_m.atan2(dy, dx) * 1024.0 / _m.pi)) & 2047


def player_start(m, sects, walls, ranges):
    for a in m.actors:
        if a.type == AT_PLAYER:
            x,y,z = a.x, -a.y, a.z
            si = actor_sector(m, sects, walls, ranges, a, x, y,
                              -z * SCALE * WORLD)
            if si >= 0:
                # kexWorld::SpawnMapActor() gives the player actor its stored
                # Z just like every other map actor. Most starts happen to be
                # on the floor, but CAVERN_PSX and a few sloped starts are
                # deliberately above it, so do not erase that spawn/fall.
                return x, y, int(round(-z * SCALE * WORLD)), si, build_angle(a.ang)
    for si,s in enumerate(sects):
        if s and ranges[si][1] >= 3:
            xs = [walls[ranges[si][0]+i]['x'] for i in range(ranges[si][1])]
            ys = [walls[ranges[si][0]+i]['y'] for i in range(ranges[si][1])]
            return sum(xs)//len(xs), sum(ys)//len(ys), s['floorz'], si, None
    return 0,0,0,0,None

def find_sector(sects, walls, ranges, x, y, z=None, candidates=None):
    """The sector at (x, y) -- and, when z is given, at that height too.

    Kex stacks rooms: one sector's footprint sits directly on top of another's,
    with the passage under the entrance porch in TOMB as the clearest case. 849
    sectors across the retail maps share an identical footprint with another
    one, and 470 of the 4923 actors (9.5%) stand somewhere that more than one
    sector covers. Picking the first footprint that matches puts a tenth of the
    game's actors -- and, in TOMB, the player start -- on the wrong floor,
    sometimes a thousand units out. So z decides between the candidates: prefer
    a sector that actually spans it, else the one whose floor is nearest.

    z is a Build z (grows downward), the same units as sects[si]['floorz'].
    """
    hits = []
    indices = range(len(ranges)) if candidates is None else candidates
    for si in indices:
        if not (0 <= si < len(ranges)):
            continue
        start, num = ranges[si]
        if num < 3: continue
        inside=False; j=num-1
        for i in range(num):
            xi,yi = walls[start+i]['x'], walls[start+i]['y']
            xj,yj = walls[start+j]['x'], walls[start+j]['y']
            if (yi>y) != (yj>y) and x < (xj-xi)*(y-yi)/float(yj-yi if yj!=yi else 1)+xi:
                inside = not inside
            j=i
        if inside:
            if z is None:
                return si
            hits.append(si)
    if not hits:
        return -1
    if len(hits) == 1:
        return hits[0]
    # Build z grows downward: ceilingz <= z <= floorz means "inside this room".
    spans = [si for si in hits
             if sects[si] and sects[si]['ceilingz'] <= z <= sects[si]['floorz']]
    pool = spans or hits
    return min(pool, key=lambda si: abs(sects[si]['floorz'] - z))


def actor_sector(m, sects, walls, ranges, a, x=None, y=None, z=None):
    """Converted sector for an authored actor.

    Actor+2 in the PSM record is the source sector index; the retail loader
    keeps it and hands the record to the game's actor factory. Prefer that
    authored identity, using x/y/z only to select one of the descendants when
    conversion split the source sector. Fall back to a geometric search for
    off-map/custom records and source sectors whose ring could not survive.
    """
    if x is None: x = a.x
    if y is None: y = -a.y
    if z is None: z = -a.z * SCALE * WORLD
    pieces = getattr(m, 'kex_sector_pieces', {}).get(a.sector, ())
    si = find_sector(sects, walls, ranges, x, y, z, pieces)
    if si >= 0:
        return si

    # Wall-mounted actors are commonly authored 4..16 units beyond the ring,
    # on the visible/firing side of the wall. Kex still instantiates them in
    # the record's sector. Select the nearest surviving descendant instead of
    # silently moving them into the adjacent room.
    valid = [p for p in pieces if 0 <= p < len(ranges) and sects[p] is not None
             and ranges[p][1] >= 3]
    if valid:
        spans = [p for p in valid if sects[p]['ceilingz'] <= z <= sects[p]['floorz']]
        pool = spans or valid
        def dist2(p):
            start, num = ranges[p]
            best = float('inf')
            for i in range(num):
                w = walls[start+i]; w2 = walls[w['point2']]
                dx, dy = w2['x']-w['x'], w2['y']-w['y']
                den = dx*dx + dy*dy
                t = max(0.0, min(1.0, ((x-w['x'])*dx + (y-w['y'])*dy) /
                                      float(den))) if den else 0.0
                qx, qy = w['x'] + t*dx, w['y'] + t*dy
                best = min(best, (x-qx)**2 + (y-qy)**2)
            return (best, abs(sects[p]['floorz'] - z))
        return min(pool, key=dist2)
    return find_sector(sects, walls, ranges, x, y, z)

# ---------------------------------------------------------------------------
# Actors
# ---------------------------------------------------------------------------
# Build PowerSlave spawns everything from a map sprite's lotag: ProcessSpriteTag()
# turns 100..118 into creatures and 6..60 into items. So an actor converts to a
# plain sprite with the right lotag and the engine does the rest.
#
# The two games' rosters are cousins, not twins -- the remaster is the console
# version, whose enemies do not all have a Build counterpart. Gameplay actors
# use a corresponding native AI where one exists; the converter composites the
# remaster's own art for scenery and for actors with no original Build tile.
AT = dict(PLAYER=0, ANUBIS=1, BASTET=2, CROW=3, QUEEN=4, MANTIS=5, MUMMY=6,
          MOSQUITO=7, FISH=8, SELKIS=9, SENTRY=10, SET=11, SPIDER=12,
          BUGKEY=29, TIMEKEY=30, XKEY=31, PLANTKEY=32, PYRAMID=33,
          MASK=35, HEALTH=40, AMMO=41, PISTOL=42, M60=43, ANUSBOMB=44,
          FLAMER=45, STAFF=46, RING=47, FLAMEPOT=49, SNAKEPOT=50,
          FULLAMMO=51, FULLHEALTH=52)

# Kex actor type -> (Build lotag, hitag). hitag carries the pickup amount for the
# ammo tags, which is what AddAmmo() reads.
KEX_PICKUP_LOTAG_FULL_HEALTH, KEX_PICKUP_LOTAG_FULL_AMMO = 15, 24
KEX_PICKUP_LOTAG_AMMO, KEX_PICKUP_LOTAG_HEALTH = 41, 42
ACTOR_MAP = {
    AT['ANUBIS']:   (100, 0),   # Anubis warrior
    AT['MUMMY']:    (101, 0),
    AT['BASTET']:   (102, 0),   # lion
    AT['SENTRY']:   (103, 0),   # Kilmaat sentry -> roach
    # The remaster's "Mantis" is Build's lava dude wearing a new name: its
    # rise/attack/death frames are pixel-identical to the LAVAG tiles and its
    # def says noLavaDamage/noExitLava/hidden. 104 was the Kilmaat roach.
    AT['MANTIS']:   (108, 0),
    AT['SPIDER']:   (105, 0),
    AT['FISH']:     (106, 0),
    AT['SET']:      (111, 0),
    AT['SELKIS']:   (112, 0),   # scorpion
    AT['QUEEN']:    (113, 0),
    AT['MOSQUITO']: (117, 0),   # wasp
    AT['CROW']:     (117, 1),   # no Build crow: the wasp's AI in a bird's feathers
                                # (hitag 1 -> BuildWasp animates it from HAWK.SEQ)

    # KEX computes these from the current weapon and its own per-weapon caps;
    # use otherwise inert Build pickup lotags and handle them in kexinv.cpp.
    AT['AMMO']:      (KEX_PICKUP_LOTAG_AMMO, 1),       # hitag = actor scale
    AT['FULLAMMO']:  (KEX_PICKUP_LOTAG_FULL_AMMO, 0),
    AT['ANUSBOMB']:  (9, 0),    # grenade
    AT['HEALTH']:    (KEX_PICKUP_LOTAG_HEALTH, 20),    # KEX health points
    AT['FULLHEALTH']:(KEX_PICKUP_LOTAG_FULL_HEALTH, 0),
    AT['MASK']:      (23, 0),   # Sobek mask
    AT['PISTOL']:    (28, 0),   # .357
    AT['M60']:       (29, 0),
    AT['FLAMER']:    (30, 0),
    AT['STAFF']:     (32, 0),   # cobra staff
    AT['RING']:      (33, 0),   # eye of Ra
    AT['BUGKEY']:    (45, 0),   # power key
    AT['TIMEKEY']:   (46, 0),
    AT['XKEY']:      (47, 0),   # war key
    AT['PLANTKEY']:  (48, 0),   # earth key
    AT['PYRAMID']:   (60, 0),   # golden sarcophagus: ends the level

    # These two numbers are straight from the remaster's defs/defines.txt, and
    # so is the correction above: the old console enum in scripts/actorTypes.txt
    # calls 49/50 FLAMEPOT/SNAKEPOT, but defines.txt says Obj_Vase_Explode01/02
    # -- the exploding pots, 232 of them, which were being converted into fuel
    # canisters and cobra ammo. They now flow through build_actor_tiles() as
    # breakables with a radial blast (see kexbreak.txt's explode columns).
    58:   (60, 0),   # Obj_Camel: the remaster's level exit -> Build's
    2000: (59, 0),   # Obj_Checkpoint -> Build's save-point scarab
    # AI_Scorp: the remaster's small scorpion, which is AI_Spider to the
    # letter in a scorpion's shell (60 of them across eight levels). The
    # spider's AI; hitag 1 has BuildSpider animate it from the exact generated
    # SCORPION.SEQ, with every authored rotation precomposited on private tiles.
    14:   (105, 1),
}

# The remaster's collectibles, which Build PowerSlave never had: the six
# artifacts, the eight transmitter pieces, the 31 team dolls and the five
# max-health ankhs. All 50 are in the retail maps (KILENTRY alone holds a full
# set of artifacts). Build's item lotags 6..60 include a handful the game
# accepts as "pickable item" and then does nothing with (10, 15, 24, 31, 34..36,
# 39..44, 51, 58); four of those carry these, the hitag saying which one, and
# the engine's Kex path (kexinv.cpp) turns them into inventory. Their art is the
# remaster's own sprite, sliced by build_actor_tiles() like any other actor.
# Keyed by actor NAME rather than number, resolved against defs/defines.txt at
# conversion time, because the doll numbering has a hole in it (170 is unused).
#
# The Sobek Mask is not here: Arti_Mask is Build's own mask pickup (lotag 23)
# and stays that, the engine flagging the artifact on the side.
KEX_INV_LOTAG_ARTIFACT, KEX_INV_LOTAG_TRANSMITTER = 34, 36
KEX_ARTI_NAMES = ['Arti_Sandals', 'Arti_Mask', 'Arti_Shawl', 'Arti_Anklets',
                  'Arti_Scepter', 'Arti_Feather']
KEX_ARTI_TYPE = {}                  # artifact index -> actor type, from defines.txt
KEX_INV_LOTAG_DOLL, KEX_INV_LOTAG_ANKH = 39, 40
KEX_INV_ACTORS = {
    # A_RandomVaseItem can spawn this even though it is not normally placed
    # in a map. Keep its native Build map lotag so the ordinary pickup path
    # reveals the current level and records it for the Kex status screen.
    'Item_Map':       (11, 0),
    'Item_Power':     (10, 2400),  # KEX's timed weapon-boost orb
    'Item_Ammo':      (KEX_PICKUP_LOTAG_AMMO, 1),
    'Item_Health':    (KEX_PICKUP_LOTAG_HEALTH, 20),
    'Item_Ammo_Full': (KEX_PICKUP_LOTAG_FULL_AMMO, 0),
    'Item_Health_Full': (KEX_PICKUP_LOTAG_FULL_HEALTH, 0),
    'Proj_Ra_Fire':   (0, 0),      # generated only for the Scepter vase swarm
    'Proj_Cobra_Green': (0, 0),    # generated only for the Shawl vase trio
    'Obj_Choppa':     (60, 63),    # TOMBEND exit; giveArtifacts = PA_ALL
    'Obj_Ramses_Body': (60, 22),   # KILARENA travel object -> Map_Tomb2
    'Obj_WarpPryamid': (60, 1),    # travel object -> Map_Tomb1, requestSave
    'Arti_Sandals': (KEX_INV_LOTAG_ARTIFACT, 0),
    'Arti_Shawl':   (KEX_INV_LOTAG_ARTIFACT, 2),
    'Arti_Anklets': (KEX_INV_LOTAG_ARTIFACT, 3),
    'Arti_Scepter': (KEX_INV_LOTAG_ARTIFACT, 4),
    'Arti_Feather': (KEX_INV_LOTAG_ARTIFACT, 5),
}
KEX_INV_ACTORS.update(('Transmitter_%d' % (i + 1), (KEX_INV_LOTAG_TRANSMITTER, i)) for i in range(8))
KEX_INV_ACTORS.update(('TD_Doll%02d' % (i + 1), (KEX_INV_LOTAG_DOLL, i)) for i in range(31))
KEX_INV_ACTORS.update(('Item_Health_Ankah%d' % i, (KEX_INV_LOTAG_ANKH, i)) for i in range(1, 6))

# The traps. Special_FireballSpawner (196 across the retail maps), the
# LaserSpawner (20, COLONY) and the BoulderSpawner (14) are all one Kex class,
# kexSlaveFireballSpawner: a hidden emitter on a face. Its tag starts one
# source-sector factory (60 ticks for fireballs/boulders, 10 for lasers), each
# emitter is staggered by eight ticks in source actor order, and tag+1 removes
# the factory. Build PowerSlave's traps provide the projectiles; KEX-only
# metadata on their hidden source sprites drives the exact factory lifecycle.
# The record's p2 is the wall FACE the spawner sits on, and that face's normal
# points into the room: that is the firing direction, not the actor's angle
# (which is 0 on most of them).
#
# Special_LaserBeamSpawner (127) is different: kexSlaveLaserBeam, a standing
# force field -- a jittering chain of bolt sprites from the wall to whatever
# the beam hits, hurting on touch, off while its tag's channel is active, and
# -1 for the ones only the Kilmaat Scepter gets past. Build has nothing like
# it; the engine's KexBeam (kexbreak.cpp) builds it from a lotag 65 origin.
KEX_TRAP_LOTAG_FIREBALL = 71
KEX_TRAP_LOTAG_BEAM = 65
# The laser spawner takes Build's other trap, the arrow (lotag 77, bullet 15):
# a fast straight shot, which is what a laser bolt is. Its ARROW.SEQ is
# rewritten onto the remaster's bolt sprite when the demo data is at hand
# (patch_demo_seqs), so the shot looks like one too; otherwise it is a spear.
KEX_TRAP_LOTAG_ARROW = 77
# The boulder spawner throws native KEX bridge bullet 17 on BOULDER.SEQ
# (FIRETRAP.SEQ's layout on the remaster's boulder sprite). The engine gives
# it the authored gravity, bouncePower, eight floor contacts and actor-impact
# blast; the converter supplies the exact face-centroid-plus-normal muzzle.
KEX_TRAP_LOTAG_BOULDER = 69
# A converted-only marker consumed during level construction. One is emitted
# for every source actor in a sector containing a factory, because retail lets
# any actor's tag+1 stop that sector's active factory.
KEX_TRAP_LOTAG_STOP = 72
KEX_TRAP_ACTORS = {25: KEX_TRAP_LOTAG_FIREBALL, 126: KEX_TRAP_LOTAG_ARROW,
                   128: KEX_TRAP_LOTAG_BOULDER, 127: KEX_TRAP_LOTAG_BEAM}

# Tiles beyond the stock creatures' that build_seq_tiles() composites: the
# beam bolt as the arrow bullet's two views (7636 side-on, stretched into a
# streak the way the remaster's "stretchy" Proj_Beam is; 7637 head-on). The
# exact crow sequence now owns 7600..7635, one tile per retail bird sprite.
KEX_SEQ_EXTRA_TILES = {
    7636: ('beam_side', 'projectiles/beam_loop', 0, 0),
    7637: ('beam_head', 'projectiles/beam_loop', 0, 0),
    7638: ('boulder',   'projectiles/boulder_loop', 0, 0),   # the boulder bullet
    # The KEX sentry's death script tosses this exact nine-piece pattern.  The
    # pieces are single-frame actors, so give the native chunk system one tile
    # per authored gib rather than substituting the classic roach chunks.
    7800: ('sentry', 'monsters/sentry_gib01', 0, 0),
    7801: ('sentry', 'monsters/sentry_gib02', 0, 0),
    7802: ('sentry', 'monsters/sentry_gib03', 0, 0),
    7803: ('sentry', 'monsters/sentry_gib04', 0, 0),
    7804: ('sentry', 'monsters/sentry_gib05', 0, 0),
    7805: ('sentry', 'monsters/sentry_projectile_loop', 0, 0),
    7806: ('anubis', 'projectiles/anubis_fireball_loop', 0, 0),
    7807: ('fireball', 'projectiles/fireball_loop', 0, 0),
    7808: ('anubis', 'monsters/anubis_gib1', 0, 0),
    7809: ('anubis', 'monsters/anubis_gib2', 0, 0),
    7810: ('anubis', 'monsters/anubis_gib3', 0, 0),
    7811: ('anubis', 'monsters/anubis_gib4', 0, 0),
    7812: ('mummy', 'monsters/mummy_gibs01', 0, 0),
    7813: ('mummy', 'monsters/mummy_gibs02', 0, 0),
    7814: ('mummy', 'monsters/mummy_gibs03', 0, 0),
    7815: ('mummy', 'monsters/mummy_gibs04', 0, 0),
    7816: ('bastet', 'monsters/bastet_gib01', 0, 0),
    7817: ('bastet', 'monsters/bastet_gib02', 0, 0),
    7818: ('bastet', 'monsters/bastet_gib03', 0, 0),
    7819: ('bastet', 'monsters/bastet_gib04', 0, 0),
    7820: ('fish', 'monsters/fish_gibs1', 0, 0),
    7821: ('fish', 'monsters/fish_gibs2', 0, 0),
    7822: ('fish', 'monsters/fish_gibs3', 0, 0),
    7823: ('wasp', 'monsters/wasp_gib01', 0, 0),
    7824: ('wasp', 'monsters/wasp_gib02', 0, 0),
    7825: ('wasp', 'monsters/wasp_gib03', 0, 0),
    7826: ('wasp', 'monsters/wasp_gib04', 0, 0),
    7827: ('wasp', 'monsters/wasp_gib05', 0, 0),
    7828: ('spider', 'monsters/spider_gib01', 0, 0),
    7829: ('spider', 'monsters/spider_gib02', 0, 0),
    7830: ('spider', 'monsters/spider_gib03', 0, 0),
    7831: ('spider', 'monsters/spider_gib04', 0, 0),
    7832: ('spider', 'monsters/spider_gib05', 0, 0),
    7833: ('spider', 'monsters/spider_gib06', 0, 0),
    7834: ('spider', 'monsters/spider_gib07', 0, 0),
    7835: ('spider', 'monsters/spider_gib08', 0, 0),
    7836: ('spider', 'monsters/spider_gib09', 0, 0),
    7837: ('spider', 'monsters/scorp_gib01', 0, 0),
    7838: ('spider', 'monsters/scorp_gib02', 0, 0),
    7839: ('spider', 'monsters/scorp_gib03', 0, 0),
    7840: ('spider', 'monsters/scorp_gib04', 0, 0),
    7841: ('queen', 'monsters/queen_gib01', 0, 0),
    7842: ('queen', 'monsters/queen_gib02', 0, 0),
    7843: ('queen', 'monsters/queen_gib03', 0, 0),
    7844: ('queen', 'monsters/queen_gib04', 0, 0),
    7845: ('queen', 'monsters/queen_gib05', 0, 0),
    7846: ('queen', 'monsters/queen_gib06', 0, 0),
    7847: ('cobra', 'projectiles/cobra_red_loop', 0, 0),
    7848: ('amunbomb', 'projectiles/amun_bomb_idle', 0, 0),
    7849: ('boulder_shard', 'projectiles/boulder_shard', 0, 0),
}
KEX_SEQ_EXTRA_SCALE = {'beam_side': (5.0, 1.5), 'beam_head': (1.5, 1.5),
                       'manacle': (1.0, 200.0 / 240.0)}

# The Manacle, the remaster's eighth weapon. Build PowerSlave has no such
# weapon, so this is the one creature-or-weapon sequence authored from
# scratch: MANACLE.SEQ, in the layout gun.cpp's WeaponInfo row expects --
# 0 raise, 1 lower, 2 idle, 3 charge (the remaster's fire + charge anims, held
# on the last frame while fire is down), 4 release (the shot on frame 0, flag
# 0x80). Frames are the remaster's weapons/manacle_* anims one for one; each
# distinct sprite becomes a tile from KEX_MANACLE_TILE_BASE, squashed to
# Build's 320x200 from the remaster's 320x240 hand frame, and the chunk sits
# at the sprite's centre the way every weapon SEQ in STUFF.DAT places its
# hand art (the loader subtracts the file's centre, the draw adds 160,100).
KEX_MANACLE_TILE_BASE = 7640
KEX_MANACLE_ANIMS = ['weapons/manacle_raise', 'weapons/manacle_lower', 'weapons/manacle_idle',
                     ('weapons/manacle_fire', 'weapons/manacle_charge'), 'weapons/manacle_release']
KEX_MANACLE_YSCALE = 200.0 / 240.0

# Native counterparts whose retail animation files do not share the demo SEQ
# cadence get private precomposited tile runs and exact loose sequence files.
# These ranges follow the exact Queen run (20400..20679) and stay clear of
# install art, which currently ends below 12000. Two hundred tiles per actor
# leave hard guard bands around every measured authored composite census.
KEX_EXACT_SPIDER_TILE_BASE = 20700
KEX_EXACT_SCORP_TILE_BASE = 20900
KEX_EXACT_MUMMY_TILE_BASE = 21100
KEX_EXACT_ANUBIS_TILE_BASE = 21300
KEX_EXACT_BASTET_TILE_BASE = 21500
KEX_EXACT_FISH_TILE_BASE = 21700
# Fx_FireballShootPuff is created only by Special_FireballSpawner. Keep its
# private eight-frame run beyond the exact creature ranges so no map texture or
# actor-art allocation can interleave with the SEQ tiles.
KEX_MUZZLE_TILE_BASE = 21900
KEX_MUZZLE_VISIBLE_FRAMES = 8
KEX_MUZZLE_FRAME_HOLD = 2       # KEX 60 Hz * animSpeed .25 -> Build 30 Hz / 2
KEX_IMPACT_TILE_BASE = 21908
KEX_IMPACT_FIRST_FRAME = 1       # fireballPuff's zero-delay goto skips magic frame 0
KEX_IMPACT_VISIBLE_FRAMES = 7    # magic frames 1..7; frame 8 destroys on entry
KEX_IMPACT_FRAME_HOLD = 2
KEX_COBRA_RED_TILE_BASE = 21915
KEX_COBRA_GREEN_TILE_BASE = 21922
KEX_COBRA_VISIBLE_FRAMES = 7     # both zero-delay aliases enter magic frame 1
KEX_COBRA_FRAME_HOLD = 2
KEX_COBRA_CONTROLLER_FRAMES = 12 # callbacks at t=0/3/6/9; destroy at t=12
KEX_ANUBIS_IMPACT_TILE_BASE = 21929
KEX_ANUBIS_IMPACT_VISIBLE_FRAMES = 13  # source frame 13 destroys on entry
KEX_EXPLOSION_TILE_BASE = 21942
KEX_EXPLOSION_VISIBLE_FRAMES = 21       # source frame 21 destroys on entry
KEX_WATER_EXPLOSION_TILE_BASE = 21963
KEX_WATER_EXPLOSION_VISIBLE_FRAMES = 18 # source frames 1..18 carry art
KEX_WATER_EXPLOSION_LIFETIME = 21       # blank at ticks 0, 19 and 20
KEX_VASE_PUFF_TILE_BASE = 21981
KEX_VASE_PUFF_VISIBLE_FRAMES = 7         # zero-delay alias enters magic frame 1
KEX_VASE_PUFF_FRAME_HOLD = 2
KEX_AMMO_SPAWN_TILE_BASE = 21988
KEX_HEALTH_SPAWN_TILE_BASE = 21996
KEX_ORB_SPAWN_UNIQUE_FRAMES = 8
KEX_ORB_SPAWN_BLANK_TICKS = 20
KEX_ORB_SPAWN_LIFETIME = 43              # pickup callback on final visible tick 42
KEX_GIB_DEATH_TILE_BASE = 22004
KEX_GIB_DEATH_UNIQUE_FRAMES = 10
KEX_GIB_DEATH_LIFETIME = 19               # A_RandomItem + destroy on blank tick 18
KEX_WATER_SPLASH_TILE_BASE = 22014
KEX_WATER_SPLASH_LIFETIME = 23             # Fx_WaterSplash destroys on source frame 22
KEX_FLAME_TILE_BASE = 22037
KEX_FLAME_FLIGHT_FRAMES = 3
KEX_FLAME_SIZZLE_VISIBLE_FRAMES = 15       # source sizzle frames 0..14
KEX_FLAME_SIZZLE_LIFETIME = 16             # then one blank tick before destroy
KEX_FLAME_FIZZLE_LIFETIME = 15             # 30 .5-speed animation edges
KEX_BIRD_FEATHER_LIFETIME = 15             # randomized, looped Obj_Bird_Feathers
KEX_EXACT_SPIDER_TILE_SPAN = 200
KEX_EXACT_MUMMY_TILE_SPAN = 200
KEX_EXACT_ANUBIS_TILE_SPAN = 200
KEX_EXACT_COMMON_TILE_SPAN = 200

def build_manacle_seq(Z, outdir, verbose=True):
    """MANACLE.SEQ plus the tile rows its chunks reference."""
    import kexseqtool
    names = {n.lower(): n for n in Z.namelist()}
    tbl = _sprite_table(Z, names, 'weapons', 'manacle')
    tiles = {}                     # frame layer signature -> tile
    extra = {}                     # tile -> KEX_SEQ_TILES row
    seq = dict(tag=b'DS', centerx=160, centery=100, seq_base=[], seq_size=[], seq_flag=[],
               fr_base=[], fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[], ck_flag=[],
               snd_names=[b'BRC_UP\0\0', b'BRC_FIRE', b'BRC_CHRG'], snd_events=[], trailing=b'')
    SOUND = {('weapons/manacle_raise', 4): 0, ('weapons/manacle_release', 0): 1,
             ('weapons/manacle_charge', 0): 2}
    for ss, spec in enumerate(KEX_MANACLE_ANIMS):
        anims = spec if isinstance(spec, tuple) else (spec,)
        seq['seq_base'].append(len(seq['fr_base']))
        nframes = 0
        for anim in anims:
            frames = parse_anim_full(Z, names, anim)
            for fi, fr in enumerate(frames):
                layers = fr.get('sprites', []) or (fr['rot'][0] if 'rot' in fr else [])
                seq['fr_base'].append(len(seq['ck_x']))
                nck = 0
                # One chunk per frame: build_seq_tiles composites every layer
                # of the frame into the one tile, so the chunk sits at the
                # frame's bounding box, not at any one layer's.
                rects = [(r, ox, oy) for g, base, idx, ox, oy, flip in layers
                         for r in (tbl.get(idx),) if r is not None]
                if rects:
                    key = tuple((idx, ox, oy, flip) for g, base, idx, ox, oy, flip in layers)
                    if key not in tiles:
                        tiles[key] = KEX_MANACLE_TILE_BASE + len(tiles)
                        extra[tiles[key]] = ('manacle', anim, fi, 0)
                    x0 = min(ox for r, ox, oy in rects)
                    y0 = min(oy for r, ox, oy in rects)
                    w = max(ox + r[2] for r, ox, oy in rects) - x0
                    h = (max(oy + r[3] for r, ox, oy in rects) - y0) * KEX_MANACLE_YSCALE
                    cx = 160 + x0 + w / 2.0
                    cy = (214 + y0) * KEX_MANACLE_YSCALE + h / 2.0
                    seq['ck_x'].append(int(round(cx))); seq['ck_y'].append(int(round(cy)))
                    seq['ck_pict'].append(tiles[key]); seq['ck_flag'].append(0)
                    nck += 1
                seq['fr_size'].append(nck)
                flag = 0
                if anim == 'weapons/manacle_release' and fi == 0:
                    flag |= 0x80
                if anim == 'weapons/manacle_charge':
                    flag |= 0x04                    # the charge glows
                seq['fr_flag'].append(flag)
                snd = SOUND.get((anim, fi))
                if snd is not None:
                    seq['snd_events'].append((len(seq['fr_base']) - 1, snd))
                nframes += 1
        seq['seq_size'].append(nframes)
        seq['seq_flag'].append(0)
    with open(os.path.join(outdir, 'MANACLE.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print("  the Manacle: MANACLE.SEQ, %d frames, %d tiles" % (len(seq['fr_base']), len(tiles)))
    return extra


def build_amunbomb_seq(Z, outdir, verbose=True):
    """Write the one-frame world sequence for retail Proj_AmunBomb."""
    import kexseqtool
    names = {n.lower(): n for n in Z.namelist()}
    frames = parse_anim_full(Z, names, 'projectiles/amun_bomb_idle')
    if len(frames) != 1:
        raise ValueError('retail amun_bomb_idle changed from one frame to %d'
                         % len(frames))
    layers = frames[0].get('sprites', [])
    bounds = []
    for group, sheetbase, index, ox, oy, _flip in layers:
        rect = _sprite_table(Z, names, group, sheetbase).get(index)
        if rect is None:
            raise ValueError('%s/%s sprite %d is missing'
                             % (group, sheetbase, index))
        bounds.append((ox, oy, ox + rect[2], oy + rect[3]))
    if not bounds:
        raise ValueError('retail amun_bomb_idle has no drawable layers')
    x0 = min(v[0] for v in bounds); y0 = min(v[1] for v in bounds)
    x1 = max(v[2] for v in bounds); y1 = max(v[3] for v in bounds)
    centerx, centery = 160, 100
    seq = dict(tag=b'DS', centerx=centerx, centery=centery,
               seq_base=[0], seq_size=[1], seq_flag=[0],
               fr_base=[0], fr_size=[1], fr_flag=[4],
               ck_x=[centerx + int(round((x0 + x1) / 2.0))],
               ck_y=[centery + int(round((y0 + y1) / 2.0))],
               ck_pict=[7848], ck_flag=[0], snd_names=[], snd_events=[],
               trailing=b'')
    with open(os.path.join(outdir, 'AMUNBOMB.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  Amun bomb: AMUNBOMB.SEQ, exact one-frame world projectile')


def build_bastet_fish_sequences(Z, demo_dir, outdir, verbose=True):
    """Exact retail LION.SEQ/FISH.SEQ live groups and composite tile rows.

    Both native files contain useful compatibility/effect slots outside their
    KEX actors' live action groups. Copy those slots byte-for-byte at the
    frame/chunk level, while replacing Bastet chase/attack/pain/teleports/idle
    and Fish chase/idle/attack with every authored rotation and delay tick.
    """
    import kexseqtool
    names = {n.lower(): n for n in Z.namelist()}
    if not demo_dir:
        raise ValueError('exact Bastet/Fish sequences need the demo STUFF.DAT directory')
    grp = os.path.join(demo_dir, 'STUFF.DAT')
    stock_files = kexseqtool.grp_extract(grp, {'LION.SEQ', 'FISH.SEQ'}) \
        if os.path.isfile(grp) else {}
    if set(stock_files) != {'LION.SEQ', 'FISH.SEQ'}:
        raise ValueError('exact Bastet/Fish sequences cannot read LION.SEQ/FISH.SEQ from %s'
                         % grp)

    specs = (
        dict(creature='bastet', filename='LION.SEQ', tilebase=KEX_EXACT_BASTET_TILE_BASE,
             expected_tiles=168,
             anims={
                 'chase': 'monsters/bastet_chase',
                 'attack': 'monsters/bastet_attack',
                 'pain': 'monsters/bastet_pain',
                 'teleport_out': 'monsters/bastet_teleport_out',
                 'teleport_in': 'monsters/bastet_teleport_in',
                 'idle': 'monsters/bastet_idle',
             },
             delays={
                 'chase': [1] * 12, 'attack': [1] * 11,
                 'pain': [1] * 17, 'teleport_out': [1] * 37 + [4],
                 'teleport_in': [1] * 23, 'idle': [1],
             },
             directional={'chase', 'attack', 'pain'},
             generated={
                 **{d: ('chase', d) for d in range(8)},
                 8: ('teleport_out', 0), 9: ('teleport_in', 0),
                 **{10 + d: ('attack', d) for d in range(8)},
                 **{44 + d: ('pain', d) for d in range(8)},
                 54: ('idle', 0),
             },
             expected_slots=57, marker_slot=None),
        dict(creature='fish', filename='FISH.SEQ', tilebase=KEX_EXACT_FISH_TILE_BASE,
             expected_tiles=107,
             anims={
                 'chase': 'monsters/fish_chase',
                 'idle': 'monsters/fish_idle',
                 'attack': 'monsters/fish_attack',
             },
             delays={'chase': [1] * 8, 'idle': [1], 'attack': [1] * 14},
             directional={'chase', 'idle', 'attack'},
             generated={
                 **{d: ('chase', d) for d in range(8)},
                 **{8 + d: ('idle', d) for d in range(8)},
                 **{24 + d: ('attack', d) for d in range(8)},
                 # Stock slot 48 is empty and terminal. A one-frame private
                 # idle pose makes the exact file distinguishable without
                 # changing any action or Fish limb/effect slot.
                 48: ('idle', 0),
             },
             expected_slots=49, marker_slot=48),
    )

    all_extra = {}
    for spec in specs:
        stock = kexseqtool.read_seq(stock_files[spec['filename']])
        if len(stock['seq_base']) != spec['expected_slots']:
            raise ValueError('%s stock slot count changed from %d to %d'
                             % (spec['filename'], spec['expected_slots'],
                                len(stock['seq_base'])))

        anim = {kind: parse_anim_full(Z, names, path)
                for kind, path in spec['anims'].items()}
        for kind, wanted in spec['delays'].items():
            actual = [frame.get('delay', 1) for frame in anim[kind]]
            if actual != wanted:
                raise ValueError('%s delay cadence changed: %s, expected %s'
                                 % (spec['anims'][kind], actual, wanted))
            for frame, data in enumerate(anim[kind]):
                if kind in spec['directional']:
                    if ('rot' not in data or len(data['rot']) != 8
                            or any(not layers for layers in data['rot'])):
                        raise ValueError('%s frame %d lacks eight nonempty rotations'
                                         % (spec['anims'][kind], frame))
                elif ('sprites' not in data or not data['sprites']):
                    # bastet_teleport_out's terminal callback frame is
                    # deliberately spriteless because A_Hide runs on entry.
                    if not (spec['creature'] == 'bastet'
                            and kind == 'teleport_out'
                            and frame == len(anim[kind]) - 1):
                        raise ValueError('%s frame %d is not a nonempty single view'
                                         % (spec['anims'][kind], frame))

        composites = {}
        for kind, frames in anim.items():
            for frame, data in enumerate(frames):
                views = data['rot'] if 'rot' in data else \
                    ([data.get('sprites', [])] if data.get('sprites', []) else [])
                for direction, layers in enumerate(views):
                    composites.setdefault(tuple(layers),
                                          (spec['anims'][kind], frame, direction))
        if len(composites) != spec['expected_tiles']:
            raise ValueError('%s composite census changed: %d, expected %d'
                             % (spec['creature'], len(composites),
                                spec['expected_tiles']))
        if len(composites) >= KEX_EXACT_COMMON_TILE_SPAN:
            raise ValueError('%s exact sequence exceeds its reserved tile span'
                             % spec['creature'])

        tile_for = {signature: spec['tilebase'] + ordinal
                    for ordinal, signature in enumerate(composites)}
        all_extra.update({tile_for[signature]:
                          (spec['creature'] + '_exact',) + source
                          for signature, source in composites.items()})

        seq = dict(tag=stock['tag'], centerx=stock['centerx'],
                   centery=stock['centery'], seq_base=[], seq_size=[],
                   seq_flag=[], fr_base=[], fr_size=[], fr_flag=[], ck_x=[],
                   ck_y=[], ck_pict=[], ck_flag=[],
                   snd_names=list(stock['snd_names']), snd_events=[], trailing=b'')
        old_to_new_frame = {}

        def append_stock(slot):
            first = stock['seq_base'][slot]
            size = stock['seq_size'][slot]
            seq['seq_base'].append(len(seq['fr_base']))
            seq['seq_size'].append(size)
            seq['seq_flag'].append(stock['seq_flag'][slot])
            for old_frame in range(first, first + size):
                old_to_new_frame[old_frame] = len(seq['fr_base'])
                seq['fr_base'].append(len(seq['ck_pict']))
                seq['fr_size'].append(stock['fr_size'][old_frame])
                seq['fr_flag'].append(stock['fr_flag'][old_frame])
                first_chunk = stock['fr_base'][old_frame]
                for old_chunk in range(first_chunk,
                                       first_chunk + stock['fr_size'][old_frame]):
                    seq['ck_x'].append(stock['ck_x'][old_chunk])
                    seq['ck_y'].append(stock['ck_y'][old_chunk])
                    seq['ck_pict'].append(stock['ck_pict'][old_chunk])
                    seq['ck_flag'].append(stock['ck_flag'][old_chunk])

        def timeline(kind, direction):
            result = []
            for frame, data in enumerate(anim[kind]):
                delay = data.get('delay', 1)
                if delay < 0:
                    delay = 1
                result.extend((kind, frame, direction) for _tick in range(delay))
            return result

        def append_generated(slot, kind, direction):
            frames = timeline(kind, direction)
            seq['seq_base'].append(len(seq['fr_base']))
            seq['seq_size'].append(len(frames))
            seq['seq_flag'].append(stock['seq_flag'][slot])
            for frame_kind, frame, frame_direction in frames:
                data = anim[frame_kind][frame]
                layers = data['rot'][frame_direction] if 'rot' in data \
                    else data.get('sprites', [])
                if not layers:
                    # A SEQ frame still needs a safe chunk because the native
                    # controller resolves picnum before running A_Hide. Reuse
                    # the preceding visible teleport pose; rendering is then
                    # suppressed by the callback in the same simulation tick.
                    previous = frame - 1
                    while previous >= 0 and not anim[frame_kind][previous].get('sprites', []):
                        previous -= 1
                    if previous < 0:
                        raise ValueError('%s has no visible pose before empty frame %d'
                                         % (spec['anims'][frame_kind], frame))
                    layers = anim[frame_kind][previous]['sprites']
                bounds = []
                for group, sheetbase, index, ox, oy, _flip in layers:
                    rect = _sprite_table(Z, names, group, sheetbase).get(index)
                    if rect is None:
                        raise ValueError('%s/%s sprite %d is missing'
                                         % (group, sheetbase, index))
                    bounds.append((ox, oy, ox + rect[2], oy + rect[3]))
                x0 = min(v[0] for v in bounds); y0 = min(v[1] for v in bounds)
                x1 = max(v[2] for v in bounds); y1 = max(v[3] for v in bounds)

                seq['fr_base'].append(len(seq['ck_pict']))
                seq['fr_size'].append(1)
                seq['fr_flag'].append(
                    4 if spec['creature'] == 'fish' and frame_kind == 'attack'
                    and frame in (4, 5, 10, 11) else 0)
                seq['ck_x'].append(seq['centerx'] + int((x0 + x1) / 2))
                seq['ck_y'].append(seq['centery'] + int((y0 + y1) / 2))
                seq['ck_pict'].append(tile_for[tuple(layers)])
                seq['ck_flag'].append(0)

        for slot in range(spec['expected_slots']):
            generated = spec['generated'].get(slot)
            if generated is None:
                append_stock(slot)
            else:
                append_generated(slot, *generated)

        seq['snd_events'] = [(old_to_new_frame[old], code)
                             for old, code in stock['snd_events']
                             if old in old_to_new_frame]
        with open(os.path.join(outdir, spec['filename']), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(seq))

        expected_frames = 620 if spec['creature'] == 'bastet' else 409
        if len(seq['fr_base']) != expected_frames:
            raise ValueError('%s exact sequence is %d frames, expected %d'
                             % (spec['creature'], len(seq['fr_base']),
                                expected_frames))
        if verbose:
            print('  %s: exact %s written (%d frames, %d chunks, %d composite tiles)'
                  % (spec['creature'], spec['filename'], len(seq['fr_base']),
                     len(seq['ck_pict']), len(composites)))

    return all_extra


def build_spider_sequences(Z, outdir, verbose=True):
    """Exact retail SPIDER.SEQ/SCORPION.SEQ plus their composite tile rows.

    The native controller addresses the same 48 slots in both files:
    0/8/16/24/32 are pain/chase/idle/leap/attack direction groups. The leap
    group contains the source launch/check-ground poses (plus Spider's falling
    check-ground pose). Slot 40 is the authored single-view four-tick death,
    slot 41 is the hidden generic-gib timer's safe pose, and 42..47 remain
    compatibility poses. All live actions retain every authored frame and all
    eight rotations.
    """
    import kexseqtool
    names = {n.lower(): n for n in Z.namelist()}
    extra = {}
    specs = (
        ('spider', KEX_EXACT_SPIDER_TILE_BASE,
         (6, 14, 1, 1, 1, 3), 45, 'SPIDER.SEQ',
         ('leap', 'check_ground', 'falling_check_ground')),
        ('scorp', KEX_EXACT_SCORP_TILE_BASE,
         (12, 13, 1, 1, 3), 48, 'SCORPION.SEQ',
         ('leap', 'check_ground')),
    )

    for creature, tilebase, expected_frames, expected_tiles, filename, leap_kinds in specs:
        kinds = ('base', 'attack') + leap_kinds + ('death',)
        anim_name = {kind: 'monsters/%s_%s' % (creature, kind)
                     for kind in kinds}
        anim = {kind: parse_anim_full(Z, names, path)
                for kind, path in anim_name.items()}
        actual_frames = tuple(len(anim[kind]) for kind in kinds)
        if actual_frames != expected_frames:
            raise ValueError('%s retail base/attack/leap/check/death cadence changed: %s, expected %s'
                             % (creature, actual_frames, expected_frames))
        if [frame.get('delay', 1) for frame in anim['death']] != [1, 2, 1]:
            raise ValueError('%s retail death delays changed' % creature)

        # A signature includes every layer's sheet/index, authored offset and
        # flip.  build_seq_tiles() bakes that complete composite into one PNG,
        # so identical signatures can safely share a tile across actions.
        composites = {}
        for kind in kinds:
            for frame, fr in enumerate(anim[kind]):
                if kind == 'death':
                    views = [fr.get('sprites', [])]
                elif 'rot' in fr and len(fr['rot']) == 8:
                    views = fr['rot']
                else:
                    raise ValueError('%s frame %d is not an eight-view animation'
                                     % (anim_name[kind], frame))
                for direction, layers in enumerate(views):
                    if not layers:
                        raise ValueError('%s frame %d direction %d is empty'
                                         % (anim_name[kind], frame, direction))
                    composites.setdefault(tuple(layers),
                                          (anim_name[kind], frame, direction))
        if len(composites) != expected_tiles:
            raise ValueError('%s composite census changed: %d, expected %d'
                             % (creature, len(composites), expected_tiles))
        if len(composites) >= KEX_EXACT_SPIDER_TILE_SPAN:
            raise ValueError('%s exact sequence exceeds its reserved tile span' % creature)

        tile_for = {signature: tilebase + ordinal
                    for ordinal, signature in enumerate(composites)}
        for signature, source in composites.items():
            extra[tile_for[signature]] = (creature + '_exact',) + source

        seq = dict(tag=b'IH', centerx=160, centery=116,
                   seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
                   fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
                   ck_flag=[], snd_names=[], snd_events=[], trailing=b'')

        for slot in range(48):
            seq['seq_base'].append(len(seq['fr_base']))
            seq['seq_flag'].append(-1 if slot == 47 else 0)
            if slot < 8:
                kind, direction = 'base', slot       # KEX pain is unreachable
                frame_map = [(kind, frame) for frame in range(len(anim[kind]))]
            elif slot < 16:
                kind, direction = 'base', slot - 8   # chase
                frame_map = [(kind, frame) for frame in range(len(anim[kind]))]
            elif slot < 24:
                kind, direction = 'base', slot - 16  # idle
                frame_map = [(kind, frame) for frame in range(len(anim[kind]))]
            elif slot < 32:
                direction = slot - 24
                frame_map = [(kind, 0) for kind in leap_kinds]
            elif slot < 40:
                kind, direction = 'attack', slot - 32
                frame_map = [(kind, frame) for frame in range(len(anim[kind]))]
            elif slot == 40:
                kind, direction = 'death', 0
                frame_map = [(kind, frame) for frame in (0, 1, 1, 2)]
            else:
                kind, direction = 'death', 0
                frame_map = [(kind, 2)]

            seq['seq_size'].append(len(frame_map))
            for kind, frame in frame_map:
                data = anim[kind][frame]
                layers = data['rot'][direction] if 'rot' in data \
                    else data['sprites']
                bounds = []
                for group, sheetbase, index, ox, oy, _flip in layers:
                    rect = _sprite_table(Z, names, group, sheetbase).get(index)
                    if rect is None:
                        raise ValueError('%s/%s sprite %d is missing'
                                         % (group, sheetbase, index))
                    bounds.append((ox, oy, ox + rect[2], oy + rect[3]))
                x0 = min(v[0] for v in bounds); y0 = min(v[1] for v in bounds)
                x1 = max(v[2] for v in bounds); y1 = max(v[3] for v in bounds)

                seq['fr_base'].append(len(seq['ck_pict']))
                seq['fr_size'].append(1)
                seq['fr_flag'].append(0)
                # seq_ReadSequence subtracts the file centre.  Place the
                # composited crop at its authored centre so the actor origin
                # remains stationary as legs/tail change extents.
                seq['ck_x'].append(seq['centerx'] + int((x0 + x1) / 2))
                seq['ck_y'].append(seq['centery'] + int((y0 + y1) / 2))
                seq['ck_pict'].append(tile_for[tuple(layers)])
                seq['ck_flag'].append(0)  # layer flips are baked into the PNG

        with open(os.path.join(outdir, filename), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(seq))
        if verbose:
            print('  %ss: exact %s written (%d frames, %d composite tiles)'
                  % (creature, filename, len(seq['fr_base']), len(composites)))

    return extra


def build_anubis_sequence(Z, demo_dir, outdir, verbose=True):
    """Exact retail ANUBIS.SEQ plus its precomposited tile rows.

    Slots 0..43 keep the native action offsets but use the remaster's exact
    live and normal-death timelines.  Demo drummer slots 44..49 are copied
    verbatim, including their seven sound events, and slots 50..57 add the
    authored nine-tick melee-miss recovery that the stock table cannot name.
    """
    import kexseqtool
    names = {n.lower(): n for n in Z.namelist()}
    anim_name = {
        kind: 'monsters/' + suffix for kind, suffix in (
            ('idle', 'anubis_idle'), ('chase', 'anubis_chase'),
            ('melee', 'anubis_melee'),
            ('range', 'anubis_attack_range'), ('pain', 'anubis_pain'),
            ('miss', 'anubis_melee_miss'),
            ('death', 'anubis_death_pc'))
    }
    anim = {kind: parse_anim_full(Z, names, path)
            for kind, path in anim_name.items()}

    expected_delays = {
        'idle': [1], 'chase': [1] * 18, 'melee': [1] * 24,
        'range': [1] * 25, 'pain': [1] * 10, 'miss': [1, 8],
        'death': [2, 2, 3, 3, 2, 2, 5, -1],
    }
    for kind, wanted in expected_delays.items():
        actual = [frame.get('delay', 1) for frame in anim[kind]]
        if actual != wanted:
            raise ValueError('%s retail delay cadence changed: %s, expected %s'
                             % (anim_name[kind], actual, wanted))
    for kind, frame, wanted in (
            ('range', 24, 'monsters/anubis_chase'),
            ('pain', 9, 'monsters/anubis_chase'),
            ('miss', 1, 'monsters/anubis_chase')):
        if anim[kind][frame].get('goto') != wanted:
            raise ValueError('%s frame %d no longer goes to %s'
                             % (anim_name[kind], frame, wanted))

    for kind in ('idle', 'chase', 'melee', 'range', 'pain', 'miss'):
        for frame, data in enumerate(anim[kind]):
            if ('rot' not in data or len(data['rot']) != 8
                    or any(not layers for layers in data['rot'])):
                raise ValueError('%s frame %d lacks eight nonempty rotations'
                                 % (anim_name[kind], frame))
    if any('sprites' not in frame or not frame['sprites']
           for frame in anim['death']):
        raise ValueError('%s is no longer a nonempty single-view animation'
                         % anim_name['death'])

    composites = {}
    for kind in ('idle', 'chase', 'melee', 'range', 'pain', 'miss', 'death'):
        for frame, data in enumerate(anim[kind]):
            views = data['rot'] if 'rot' in data else [data['sprites']]
            for direction, layers in enumerate(views):
                composites.setdefault(tuple(layers),
                                      (anim_name[kind], frame, direction))
    if len(composites) != 128:
        raise ValueError('anubis composite census changed: %d, expected 128'
                         % len(composites))
    if len(composites) >= KEX_EXACT_ANUBIS_TILE_SPAN:
        raise ValueError('anubis exact sequence exceeds its reserved tile span')

    tile_for = {signature: KEX_EXACT_ANUBIS_TILE_BASE + ordinal
                for ordinal, signature in enumerate(composites)}
    extra = {tile_for[signature]: ('anubis_exact',) + source
             for signature, source in composites.items()}

    if not demo_dir:
        raise ValueError('exact ANUBIS.SEQ needs the demo STUFF.DAT directory')
    grp = os.path.join(demo_dir, 'STUFF.DAT')
    stock_data = kexseqtool.grp_extract(grp, {'ANUBIS.SEQ'}).get('ANUBIS.SEQ') \
        if os.path.isfile(grp) else None
    if stock_data is None:
        raise ValueError('exact ANUBIS.SEQ cannot read %s' % grp)
    stock = kexseqtool.read_seq(stock_data)
    if (len(stock['seq_base']) != 50
            or stock['seq_size'][44:50] != [4, 30, 30, 30, 30, 30]):
        raise ValueError('demo ANUBIS.SEQ drummer layout changed')

    def timeline(kind, direction=0, include_forever=False):
        out = []
        for frame, data in enumerate(anim[kind]):
            delay = data.get('delay', 1)
            if delay < 0:
                if include_forever:
                    out.append((kind, frame, direction))
                continue
            out.extend((kind, frame, direction) for _tick in range(delay))
        return out

    seq = dict(tag=b'DS', centerx=stock['centerx'], centery=stock['centery'],
               seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
               fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
               ck_flag=[], snd_names=list(stock['snd_names']), snd_events=[],
               trailing=b'')
    old_to_new_frame = {}

    def append_generated(frames):
        seq['seq_base'].append(len(seq['fr_base']))
        seq['seq_size'].append(len(frames))
        seq['seq_flag'].append(0)
        for kind, frame, direction in frames:
            data = anim[kind][frame]
            layers = data['rot'][direction] if 'rot' in data else data['sprites']
            bounds = []
            for group, sheetbase, index, ox, oy, _flip in layers:
                rect = _sprite_table(Z, names, group, sheetbase).get(index)
                if rect is None:
                    raise ValueError('%s/%s sprite %d is missing'
                                     % (group, sheetbase, index))
                bounds.append((ox, oy, ox + rect[2], oy + rect[3]))
            x0 = min(v[0] for v in bounds); y0 = min(v[1] for v in bounds)
            x1 = max(v[2] for v in bounds); y1 = max(v[3] for v in bounds)

            seq['fr_base'].append(len(seq['ck_pict']))
            seq['fr_size'].append(1)
            seq['fr_flag'].append(0)
            seq['ck_x'].append(seq['centerx'] + int((x0 + x1) / 2))
            seq['ck_y'].append(seq['centery'] + int((y0 + y1) / 2))
            seq['ck_pict'].append(tile_for[tuple(layers)])
            seq['ck_flag'].append(0)

    def append_stock(slot):
        first = stock['seq_base'][slot]
        size = stock['seq_size'][slot]
        seq['seq_base'].append(len(seq['fr_base']))
        seq['seq_size'].append(size)
        seq['seq_flag'].append(stock['seq_flag'][slot])
        for old_frame in range(first, first + size):
            old_to_new_frame[old_frame] = len(seq['fr_base'])
            seq['fr_base'].append(len(seq['ck_pict']))
            seq['fr_size'].append(stock['fr_size'][old_frame])
            seq['fr_flag'].append(stock['fr_flag'][old_frame])
            first_chunk = stock['fr_base'][old_frame]
            for old_chunk in range(first_chunk,
                                   first_chunk + stock['fr_size'][old_frame]):
                seq['ck_x'].append(stock['ck_x'][old_chunk])
                seq['ck_y'].append(stock['ck_y'][old_chunk])
                seq['ck_pict'].append(stock['ck_pict'][old_chunk])
                seq['ck_flag'].append(stock['ck_flag'][old_chunk])

    for slot in range(58):
        if slot < 8:
            append_generated(timeline('idle', slot))
        elif slot < 16:
            append_generated(timeline('chase', slot - 8))
        elif slot < 24:
            append_generated(timeline('melee', slot - 16))
        elif slot < 32:
            append_generated(timeline('range', slot - 24))
        elif slot < 40:
            append_generated(timeline('pain', slot - 32))
        elif slot == 40 or slot == 42:
            append_generated(timeline('death', include_forever=True))
        elif slot == 41 or slot == 43:
            append_generated([('death', 7, 0)])
        elif slot < 50:
            append_stock(slot)
        else:
            append_generated(timeline('miss', slot - 50))

    seq['snd_events'] = [(old_to_new_frame[frame], code)
                         for frame, code in stock['snd_events']
                         if frame in old_to_new_frame]
    expected_sizes = ([1] * 8 + [18] * 8 + [24] * 8 + [25] * 8
                      + [10] * 8 + [20, 1, 20, 1]
                      + [4, 30, 30, 30, 30, 30] + [9] * 8)
    if seq['seq_size'] != expected_sizes:
        raise ValueError('internal ANUBIS.SEQ slot layout is inconsistent')
    if len(seq['fr_base']) != 892 or len(seq['ck_pict']) != 1042:
        raise ValueError('internal ANUBIS.SEQ is %d frames/%d chunks, expected 892/1042'
                         % (len(seq['fr_base']), len(seq['ck_pict'])))
    if len(seq['snd_events']) != 7:
        raise ValueError('internal ANUBIS.SEQ retained %d drummer sounds, expected 7'
                         % len(seq['snd_events']))

    with open(os.path.join(outdir, 'ANUBIS.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  Anubis: exact ANUBIS.SEQ written '
              '(%d frames, %d chunks, %d composite tiles)'
              % (len(seq['fr_base']), len(seq['ck_pict']), len(composites)))
    return extra


def build_mummy_sequence(Z, outdir, verbose=True):
    """Exact retail MUMMY.SEQ plus its precomposited tile rows.

    KEX animation delays are real simulation ticks, while a Build SEQ advances
    one frame per tick.  Expand delayed frames here so the native controller
    sees the exact 1/16/13/39/9/35 cadence.  The last normal-death frame has a
    KEX delay of -1; it is represented once in the death group and then by the
    one-frame corpse group that the controller holds forever.
    """
    import kexseqtool
    names = {n.lower(): n for n in Z.namelist()}
    anim_name = {
        kind: 'monsters/mummy_' + suffix for kind, suffix in (
            ('idle', 'idle'), ('chase', 'chase'), ('melee', 'melee'),
            ('range', 'range_attack'), ('pain', 'pain'),
            ('death', 'death_pc'))
    }
    anim = {kind: parse_anim_full(Z, names, path)
            for kind, path in anim_name.items()}

    expected_delays = {
        'idle': [1], 'chase': [1] * 16, 'melee': [1] * 13,
        'range': [1] * 30 + [8, 1], 'pain': [8, 1],
        'death': [8, 7, 5, 4, 3, 3, 2, 2, -1],
    }
    for kind, wanted in expected_delays.items():
        actual = [frame.get('delay', 1) for frame in anim[kind]]
        if actual != wanted:
            raise ValueError('%s retail delay cadence changed: %s, expected %s'
                             % (anim_name[kind], actual, wanted))
    expected_gotos = {
        ('range', 31): 'monsters/mummy_chase',
        ('pain', 1): 'monsters/mummy_chase',
    }
    for (kind, frame), wanted in expected_gotos.items():
        if anim[kind][frame].get('goto') != wanted:
            raise ValueError('%s frame %d no longer goes to %s'
                             % (anim_name[kind], frame, wanted))

    # Every live action is eight-view; death is the authored single-view fall.
    for kind in ('idle', 'chase', 'melee', 'range', 'pain'):
        for frame, data in enumerate(anim[kind]):
            if ('rot' not in data or len(data['rot']) != 8
                    or any(not layers for layers in data['rot'])):
                raise ValueError('%s frame %d lacks eight nonempty rotations'
                                 % (anim_name[kind], frame))
    if any('sprites' not in frame or not frame['sprites']
           for frame in anim['death']):
        raise ValueError('%s is no longer a nonempty single-view animation'
                         % anim_name['death'])

    # Allocate a tile once per complete layered image.  Repeated delayed ticks
    # and repeated corpse slots then point at the same authored composite.
    composites = {}
    for kind in ('idle', 'chase', 'melee', 'range', 'pain', 'death'):
        for frame, data in enumerate(anim[kind]):
            views = data['rot'] if 'rot' in data else [data['sprites']]
            for direction, layers in enumerate(views):
                composites.setdefault(tuple(layers),
                                      (anim_name[kind], frame, direction))
    if len(composites) != 108:
        raise ValueError('mummy composite census changed: %d, expected 108'
                         % len(composites))
    if len(composites) >= KEX_EXACT_MUMMY_TILE_SPAN:
        raise ValueError('mummy exact sequence exceeds its reserved tile span')

    tile_for = {signature: KEX_EXACT_MUMMY_TILE_BASE + ordinal
                for ordinal, signature in enumerate(composites)}
    extra = {tile_for[signature]: ('mummy_exact',) + source
             for signature, source in composites.items()}

    def timeline(kind, direction=0, include_forever=False):
        out = []
        for frame, data in enumerate(anim[kind]):
            delay = data.get('delay', 1)
            if delay < 0:
                if include_forever:
                    out.append((kind, frame, direction))
                continue
            out.extend((kind, frame, direction) for _tick in range(delay))
        return out

    slots = []
    for slot in range(62):
        if slot < 8:
            frames = timeline('chase', slot)
        elif slot < 16:
            frames = timeline('idle', slot - 8)
        elif slot < 24:
            frames = timeline('melee', slot - 16)
        elif slot < 32:
            frames = timeline('range', slot - 24)
        elif slot == 32:
            frames = timeline('death', include_forever=True)
        elif slot < 50:
            frames = [('death', 8, 0)]       # death/corpse/revive-safe slots
        elif slot < 58:
            frames = timeline('pain', slot - 50)
        else:
            frames = [('idle', 0, slot - 58)]
        slots.append(frames)

    expected_sizes = ([16] * 8 + [1] * 8 + [13] * 8 + [39] * 8
                      + [35] + [1] * 17 + [9] * 8 + [1] * 4)
    if [len(slot) for slot in slots] != expected_sizes:
        raise ValueError('internal MUMMY.SEQ slot layout is inconsistent')

    seq = dict(tag=b'IH', centerx=160, centery=116,
               seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
               fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
               ck_flag=[], snd_names=[], snd_events=[], trailing=b'')
    for frames in slots:
        seq['seq_base'].append(len(seq['fr_base']))
        seq['seq_size'].append(len(frames))
        seq['seq_flag'].append(0)
        for kind, frame, direction in frames:
            data = anim[kind][frame]
            layers = data['rot'][direction] if 'rot' in data else data['sprites']
            bounds = []
            for group, sheetbase, index, ox, oy, _flip in layers:
                rect = _sprite_table(Z, names, group, sheetbase).get(index)
                if rect is None:
                    raise ValueError('%s/%s sprite %d is missing'
                                     % (group, sheetbase, index))
                bounds.append((ox, oy, ox + rect[2], oy + rect[3]))
            x0 = min(v[0] for v in bounds); y0 = min(v[1] for v in bounds)
            x1 = max(v[2] for v in bounds); y1 = max(v[3] for v in bounds)

            seq['fr_base'].append(len(seq['ck_pict']))
            seq['fr_size'].append(1)
            seq['fr_flag'].append(0)
            seq['ck_x'].append(seq['centerx'] + int((x0 + x1) / 2))
            seq['ck_y'].append(seq['centery'] + int((y0 + y1) / 2))
            seq['ck_pict'].append(tile_for[tuple(layers)])
            seq['ck_flag'].append(0)

    if len(seq['fr_base']) != 680:
        raise ValueError('internal MUMMY.SEQ frame count is %d, expected 680'
                         % len(seq['fr_base']))
    with open(os.path.join(outdir, 'MUMMY.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  mummies: exact MUMMY.SEQ written (%d frames, %d composite tiles)'
              % (len(seq['fr_base']), len(composites)))
    return extra


def patch_demo_seqs(demo_dir, outdir, verbose=True):
    """Two of STUFF.DAT's sequence files, rewritten for the Kex data dir:

    ARROW.SEQ: the arrow bullet's two tiles retargeted to the beam bolt, so
    the laser spawners' shots are bolts.
    SET.SEQ: Set's melee hit (FrameFlag 0x80) moved from the demo's frames 6
    and 15 of the swing to the remaster's 8 and 17 -- set_melee lands two
    frames later than the DOS data does."""
    import kexseqtool
    grp = os.path.join(demo_dir, 'STUFF.DAT')
    if not os.path.exists(grp):
        if verbose: print("  no %s: ARROW.SEQ and SET.SEQ left as shipped" % grp)
        return
    d = kexseqtool.grp_extract(grp, ['ARROW.SEQ', 'SET.SEQ', 'FIRETRAP.SEQ'])
    if 'FIRETRAP.SEQ' in d:
        # BOULDER.SEQ: the trap fireball's eight views, three frames each, all
        # of them the one round boulder sprite.
        s = kexseqtool.read_seq(d['FIRETRAP.SEQ'])
        s['ck_pict'] = [7638 for _p in s['ck_pict']]
        with open(os.path.join(outdir, 'BOULDER.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(s))
    if 'ARROW.SEQ' in d:
        s = kexseqtool.read_seq(d['ARROW.SEQ'])
        s['ck_pict'] = [{1287: 7636, 1289: 7637}.get(p, p) for p in s['ck_pict']]
        with open(os.path.join(outdir, 'ARROW.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(s))
    if 'SET.SEQ' in d:
        s = kexseqtool.read_seq(d['SET.SEQ'])
        moved = 0
        for ss in range(9, 17):                    # melee, eight views
            base, size = s['seq_base'][ss], s['seq_size'][ss]
            for f_from, f_to in ((6, 8), (15, 17)):
                if f_to < size and s['fr_flag'][base + f_from] & 0x80:
                    s['fr_flag'][base + f_from] &= ~0x80
                    s['fr_flag'][base + f_to] |= 0x80
                    moved += 1
        with open(os.path.join(outdir, 'SET.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(s))
        if verbose: print("  demo SEQs: ARROW.SEQ on the beam bolt, BOULDER.SEQ, SET.SEQ melee hit moved (%d flags)" % moved)
KEX_BEAM_PROJECTILE = 'Proj_Beam'      # the bolt sprite the beam is drawn with

def _face_angle(m, fi):
    """Build yaw along a face's inward normal, or None if it is vertical."""
    if not (0 <= fi < m.nface):
        return None
    nx, ny, nz = m.faces[fi].n
    if abs(nx) < 1e-6 and abs(ny) < 1e-6:
        return None
    # Build flips y, and its angles run 0..2047 clockwise with +y down.
    return int(round(math.atan2(-ny, nx) * 1024.0 / math.pi)) & 2047

def _spawner_origin(m, a):
    """Return kexSlaveFireballSpawner's polygon-centre muzzle."""
    if not (0 <= a.p2 < m.nface):
        return None
    face = m.faces[a.p2]
    # Shipping Special_BoulderSpawner records use -3 to keep the explicit
    # actor position while still borrowing params2's face normal. This also
    # permits ceiling/floor launchers whose direction face lives elsewhere.
    if a.p1 == -3:
        return tuple((a.x, a.y, a.z)[axis] + face.n[axis] * 32.0
                     for axis in range(3))
    if not (0 <= a.p1 < m.ntile):
        return None
    if not (face.tileStart <= a.p1 <= face.tileEnd):
        return None
    corners = [i for i in m.tileCorners[a.p1] if i != 0xff]
    if len(corners) not in (3, 4) \
            or any(face.vertStart + i >= m.nvert for i in corners):
        return None
    points = [m.verts[face.vertStart + i] for i in corners]
    inv = 1.0 / len(points)
    # SpawnFireball averages the polygon vertices, then moves 32 authored
    # units along the wall face's inward normal.
    return tuple(sum(p[axis] for p in points) * inv + face.n[axis] * 32.0
                 for axis in range(3))

# The Manacle is the remaster's eighth weapon. The native compatibility path
# adds its inventory/weapon slot and generated MANACLE.SEQ, so CHAOS's pickup
# maps to lotag 31 instead of being discarded.
KEX_SKIP_ACTORS = set()
KEX_INV_ACTORS['Item_Weapon_Manacle'] = (31, 0)   # the eighth weapon (kWeaponManacle); see build_manacle_seq

# The Kex event table. The numbers do NOT mean what Build PowerSlave's sector
# effects mean -- only DOOR_NORMAL happens to share its 1. The names are the
# remaster's own: its editor ships the full SLAVE_EVENT_* enum inside
# anubis_Shipping_Galaxy_x64.exe as two parallel 41-entry tables (the values at
# file offset 0x52a820, the name pointers at 0xa415e0), and the pairing checks
# out against every behaviour the shipped test maps demonstrate: test_door's
# use-doors are 1, its key door 3 with a key actor beside it, test_switch's
# room is 200 firing a 7, test_floor_triggers' plate is 23 and its walk-in
# trigger region 50, test_lifts' lift is 24, test_mantis_trigger fires 7 off a
# 201, test_teleport pairs 71 with 72. An earlier version of this file assumed
# the numbering was Build's, which made roughly 900 of the 1009 events across
# the retail maps wrong or dead -- most visibly 50, REMOTE_TRIGGER, the walk-in
# trigger and the most common event in the game (436 records), which Build 50
# read as a floor elevator and sank on the spot.
KEX_EVENTS = {
    0: 'NONE',                     # sectorless no-op sentinels in SETPALAC
    1: 'DOOR_NORMAL',              2: 'DOOR_TRIGGER_ONLY',
    3: 'DOOR_KEY1',                4: 'DOOR_KEY2',
    5: 'DOOR_KEY3',                6: 'DOOR_KEY4',
    7: 'DOOR_OPEN_ONLY_MESSAGE',   8: 'DOOR_CLOSE',
    9: 'DOOR_CLOSE_SLOW',
    21: 'FLOOR_LOWER_WAIT_RAISE',  22: 'FLOOR_LOWER',
    23: 'FLOOR_TRIGGER_PLATE',     24: 'FLOOR_RAISE_WAIT_LOWER',
    25: 'FLOOR_LOWER_ACTIVE',
    41: 'FLOATER_128',             42: 'FLOATER_320_Z_160',
    43: 'FLOATER_128_ANG_60',      44: 'FLOATER_128_ANG_120',
    45: 'FLOATER_128_ANG_180',     46: 'FLOATER_128_ANG_NEG_60',
    47: 'FLOATER_128_ANG_NEG_120', 48: 'DROP_PAD',
    49: 'SCRIPTED_MOVE',           50: 'REMOTE_TRIGGER',
    52: 'SCRIPT',                  53: 'ARTIFACT_TRIGGER',
    54: 'ARTIFACT_SPAWN',          60: 'FLOATER_64_ANG_USER',
    63: 'SCRIPT_REPEATABLE',       65: 'FLOATER_128_ANG_RAND_256',
    66: 'EXPLODE',                 68: 'FLOATER_512_ANG_USER',
    69: 'FLOATER_160_Z_NEG_128',   70: 'ACTOR_TRIGGER',
    71: 'TELEPORT_START',          72: 'TELEPORT_DEST',
    200: 'SWITCH1',                201: 'SWITCH2',
    202: 'SWITCH3_WOOD',           203: 'SWITCH4_METAL',
}

# Item lotag -> the picnum the original maps give that item's sprite.
#
# Build PowerSlave spawns an item from its sprite's lotag, but for most items
# BuildItemAnim() finds nItemAnimInfo[lotag - 6].a == -1 and builds no anim
# sprite -- so the sprite keeps whatever picnum the MAP gave it, forever. The
# converter was writing picnum 1 for every gameplay actor, and tile 1 in
# PowerSlave's art is a 64x128 carved stone face panel. That is why a slab was
# standing where the .357 belongs in TOMB.
#
# Recovered by scanning the six levels that ship in STUFF.DAT and taking the
# picnum each item lotag actually carries; every one of these was unanimous or
# near-unanimous across the maps, and each tile was checked by eye against the
# item it claims to be (877 is the speed loader, 488 the revolver, 899 the
# cobra staff, and so on).
#
# Enemies are absent on purpose: BuildAnubis() and friends assign picnum
# themselves and then let the SEQ system drive it per frame, so the map's value
# is never seen.
ITEM_PICNUM = {
    6:  877,    # speed loader
    7:  879,    # fuel canister
    8:  881,    # M-60 ammo belt
    9:  878,    # grenade
    12: 718,    # berry twig
    14: 332,    # cobra venom bowl
    23: 754,    # Sobek mask
    28: 488,    # .357
    29: 490,    # M-60
    30: 491,    # flame thrower
    32: 899,    # cobra staff
    37: 3457,   # cobra staff ammo
    45: 3328,   # power key
    46: 3320,   # time key
    47: 3340,   # war key
    48: 1580,   # earth key
    60: 3458,   # golden sarcophagus
    # 33, the Eye of Ra, appears in no map that ships in STUFF.DAT, so there is
    # no picnum to recover for it. It gets a tile built from the remaster's own
    # sprite instead -- see build_sprites().
}

# ---------------------------------------------------------------------------
# The remaster's own scenery
#
# Build PowerSlave only knows the actors its own lotags spawn, which leaves the
# levels as bare boxes -- the rooms in the remaster are full of fire pots,
# vases, torches and lamps (36 fire pots and 23 vases in TOMB alone). All of it
# is in the .kpf, reachable by following the chain the game itself uses:
#
#   defs/defines.txt      actor name  -> the number a map stores
#   defs/actors/**.txt    actor name  -> spawnAnim, scale, fullbright
#   sprites/<g>/*_anim.txt    anim    -> frames, each a stack of sprite layers
#   sprites/<g>/*_sprite.txt  index   -> rectangle on that group's sheet
#
# A frame is a STACK, not a single image -- a fire pot is the pot plus the
# flame, each with its own offset -- so the layers are composited into one tile.
# ---------------------------------------------------------------------------
def load_kex_actors(Z):
    """{type number: {name, anim, scale, fullbright}} from the remaster's defs."""
    names = {n.lower(): n for n in Z.namelist()}
    def read(path):
        real = names.get(path.lower())
        return Z.read(real).decode('latin-1') if real else None

    name2type = {}
    defines = read('defs/defines.txt') or ''
    for m in re.finditer(r'^#define\s+(\w+)\s+(\d+)', defines, re.M):
        name2type[m.group(1)] = int(m.group(2))

    defs = {}
    for n in Z.namelist():
        if not (n.lower().startswith('defs/actors/') and n.lower().endswith('.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        for m in re.finditer(r'^Actor\s+(\w+)\s*\{(.*?)\n\}', txt, re.S | re.M):
            nm, body = m.group(1), m.group(2)
            def field(k, d=None):
                mm = re.search(k + r'\s+"?([^"\n]+)"?', body)
                if not mm:
                    return d
                # Some defs carry a trailing // comment on the value, e.g.
                # `radius 90.0 // 95.25`.
                return mm.group(1).split('//')[0].strip()
            # noAdvanceFrames means the engine holds the actor on frame 0.
            # The vases and barrels all carry it: the extra frames in their
            # spawnAnim are not an animation, they are variant looks and
            # break-up shards, so cycling them made a vase flicker between a
            # pot and a sliver of debris. randomizeFrames is different: it
            # chooses an initial phase and then continues advancing. Every
            # fire and torch uses it so neighbouring flames do not pulse in
            # lockstep.
            defs[nm] = dict(name=nm, anim=field('spawnAnim'),
                            scale=float(field('scale', '1') or 1),
                            animspeed=float(field('animSpeed', '.5') or .5),
                            height=float(field('height', '0') or 0),
                            fullbright=int(field('fullbright', '0') or 0),
                            still=int(field('noAdvanceFrames', '0') or 0),
                            randomize=int(field('randomizeFrames', '0') or 0),
                            health=int(field('health', '0') or 0),
                            shootable=int(field('shootable', '0') or 0),
                            solid=int(field('solid', '0') or 0),
                            death=field('deathAnim'),
                            radius=float(field('radius', '0') or 0),
                            group=field('group', '') or '')

    out = {}
    for nm, t in name2type.items():
        d = defs.get(nm)
        if d and d['anim']:
            out[t] = d
    return out


def _sprite_table(Z, names, group, sheetbase):
    """index -> (x, y, w, h) for one sprite sheet."""
    txt = None
    real = names.get(('sprites/%s/%s_sprite.txt' % (group, sheetbase)).lower())
    if real: txt = Z.read(real).decode('latin-1')
    if txt is None: return {}
    return {int(m.group(1)): tuple(int(v) for v in m.groups()[1:])
            for m in re.finditer(r'sprite\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', txt)}


# How many frames the compact paths (props, generic scenery) initially inspect.
# Animated remaster actors are expanded to their full authored run below; their
# private tiles are dynamic allocations, so no fixed Build range is consumed.
KEX_ANIM_MAX_FRAMES = 8
KEX_LIGHT_ANIM_MAX_FRAMES = 32
KEX_DECOR_ANIM_MAX_FRAMES = 64

def _all_frames(Z, names, anim, limit=KEX_ANIM_MAX_FRAMES):
    """Each frame's layers: [[(group, sheetbase, index, ox, oy), ...], ...]"""
    if '/' not in anim: return []
    group, animname = anim.split('/', 1)
    for n in Z.namelist():
        ln = n.lower()
        if not (ln.startswith('sprites/%s/' % group.lower()) and ln.endswith('_anim.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        m = re.search(r'^' + re.escape(animname) + r'\s*\n\{(.*?)\n\}', txt, re.S | re.M)
        if not m: continue
        frames = []
        for fm in re.finditer(r'frame\s*\{(.*?)\n\s*\}', m.group(1), re.S):
            layers = []
            for lm in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)', fm.group(1)):
                ref = lm.group(1)
                g, base = ref.split('/', 1) if '/' in ref else (group, ref)
                layers.append((g, base, int(lm.group(2)), int(lm.group(3)), int(lm.group(4))))
            if layers: frames.append(layers)
            if len(frames) >= limit: break
        if frames: return frames
    return []


# The mine: A_CanSeeTarget's 256 units, and the exact KEX countdown expressed
# in our 30 Hz bridge ticks. ChangeAnim immediately executes detonate frame 0;
# after the five detonate frames it enters the loop and makes its first of 29
# health decrements on tick 5. The 30 -> 1 transition that selects the blast
# therefore happens on tick 33 (5 + 28), not after an approximate delay.
KEX_MINE_SIGHT = 256
KEX_MINE_FUSE = 33
KEX_MINE_DETONATE_FRAMES = 5
KEX_MINE_LOOP_FRAMES = 3
KEX_MINE_ART_FRAMES = 1 + KEX_MINE_DETONATE_FRAMES + KEX_MINE_LOOP_FRAMES

KEX_BREAK_SOUND_NONE = 0
KEX_BREAK_SOUND_GLASS = 1
KEX_BREAK_SOUND_WOOD = 2
KEX_BREAK_SOUND_FLESH = 3
KEX_BREAK_SOUNDS = {
    'sounds/glassbreak.wav': KEX_BREAK_SOUND_GLASS,
    'sounds/woodbreak.wav': KEX_BREAK_SOUND_WOOD,
    'sounds/fleshbreak.wav': KEX_BREAK_SOUND_FLESH,
}


def _death_sound(Z, names, anim):
    """Exact A_PlayLocalSound recipe for one prop death animation.

    Zero means the authored animation has no local sound. An unknown callback
    is an error rather than a silent fallback: a new retail recipe needs an
    explicit runtime mapping before the converter may claim to support it.
    """
    if not anim or '/' not in anim:
        return KEX_BREAK_SOUND_NONE
    group, animname = anim.split('/', 1)
    for n in Z.namelist():
        ln = n.lower()
        if not (ln.startswith('sprites/%s/' % group.lower()) and ln.endswith('_anim.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        m = re.search(r'^' + re.escape(animname) + r'\s*\n\{(.*?)\n\}', txt, re.S | re.M)
        if not m:
            continue
        sound = re.search(r'A_PlayLocalSound\(\s*"([^"]+)"', m.group(1), re.I)
        if not sound:
            return KEX_BREAK_SOUND_NONE
        path = sound.group(1).replace('\\', '/').lower()
        if path not in KEX_BREAK_SOUNDS:
            raise ValueError('unsupported break sound %s in %s' % (path, anim))
        return KEX_BREAK_SOUNDS[path]
    return KEX_BREAK_SOUND_NONE


def _death_spawn_effect(Z, names, anim):
    """The first A_Spawn actor named by a breakable death frame, or None."""
    if not anim or '/' not in anim:
        return None
    group, animname = anim.split('/', 1)
    for n in Z.namelist():
        ln = n.lower()
        if not (ln.startswith('sprites/%s/' % group.lower())
                and ln.endswith('_anim.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        match = re.search(r'^' + re.escape(animname) + r'\s*$', txt, re.M)
        if not match:
            continue
        body, _ = _brace_block(txt, match.end())
        spawn = re.search(r'A_Spawn\(\s*"([^"]+)"', body)
        return spawn.group(1) if spawn else None
    return None


def _death_blast(Z, names, anim):
    """(radius, damage, openFaces) of A_RadialBlast, or None.

    The third argument is gameplay, not an unused script parameter: the retail
    radial-blast path uses it to open authored Toggle faces.  Cobra shots pass
    zero; Amun bombs, fireballs, boulders, mines and exploding vases pass one.
    """
    if '/' not in anim: return None
    group, animname = anim.split('/', 1)
    for n in Z.namelist():
        ln = n.lower()
        if not (ln.startswith('sprites/%s/' % group.lower()) and ln.endswith('_anim.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        m = re.search(r'^' + re.escape(animname) + r'\s*\n\{(.*?)\n\}', txt, re.S | re.M)
        if not m: continue
        b = re.search(r'A_RadialBlast\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)', m.group(1))
        if b: return (int(b.group(1)), int(b.group(2)), int(b.group(3)) == 1)
    return None


def _first_frame(Z, names, anim):
    f = _all_frames(Z, names, anim, 1)
    return f[0] if f else []


def build_actor_tiles(Z, outdir, wanted_types, kexactors, tilebase, verbose=True):
    """Composite each actor's first frame into a tile. {type: (tile, scale, fullbright)}"""
    import kexpng as _png
    names = {n.lower(): n for n in Z.namelist()}
    outdir_art = os.path.join(outdir, 'kexart', 'actors')
    os.makedirs(outdir_art, exist_ok=True)
    # Clear it first: an actor that used to emit seven frames and now emits one
    # would otherwise leave the other six behind, unreferenced but confusing.
    for stale in os.listdir(outdir_art):
        if stale.endswith('.png'):
            os.remove(os.path.join(outdir_art, stale))

    sheets = {}
    def sheet(group, base):
        key = (group, base)
        if key not in sheets:
            real = names.get(('sprites/%s/%s.png' % (group, base)).lower())
            if real is None:
                sheets[key] = None
            else:
                tmp = os.path.join(outdir_art, '_%s_%s.png' % (group, base))
                open(tmp, 'wb').write(Z.read(real))
                sheets[key] = _png.read(tmp)
                os.remove(tmp)
        return sheets[key]

    out = {}; tile = tilebase; defs = []; animdefs = []; breakables = []; lights = []
    for t in sorted(wanted_types):
        a = kexactors.get(t)
        if not a: continue
        # Cobra's spawn frame only runs setup code and jumps into its loop;
        # the visible frame is therefore the loop, not the empty spawn frame.
        art_anim = ('projectiles/cobra_green_loop'
                    if a['anim'] == 'projectiles/cobra_green_spawn' else a['anim'])
        is_light = 'light source' in (a.get('group') or '').lower()
        frames = _all_frames(Z, names, art_anim,
                             KEX_LIGHT_ANIM_MAX_FRAMES if is_light
                             else KEX_ANIM_MAX_FRAMES)
        if not frames: continue
        breakable = a.get('still') and a.get('shootable') and a.get('death')
        # The exploding pots hold a random still frame (randomizeFrames) and
        # keep their shard art in a separate *_shards anim; the blast radius
        # and damage sit in the death anim's A_RadialBlast action.
        explode = (0, 0, False)
        if not breakable and a.get('shootable') and a.get('death') and a.get('randomize'):
            blast = _death_blast(Z, names, a['death'])
            if blast:
                explode = blast
                shards = _all_frames(Z, names, a['death'].replace('_death', '_shards'))
                frames = frames[:1] + shards
                breakable = len(frames) > 1
        elif a.get('still') and not breakable:
            frames = frames[:1]
        # AI_Magic8Ball, the remaster's mine (62 in SUNKEN, 4 in MAGMA): a
        # shootable sphere that arms when it can see a player within 256
        # units (A_CanSeeTarget in its idle), plays mineact.wav, counts down
        # and blasts (A_RadialBlast(512, 140) in magic8ball_explode) -- or,
        # under water, blasts and drops a random item. Its death anim is the
        # countdown too, so shooting it only starts the fuse. Keep idle plus
        # every authored countdown pose as adjacent private tiles: the native
        # bridge selects the five detonate frames once, then loops the three
        # countdown frames until its precise blast callback. They share one
        # compositor canvas, so an armed mine does not jump in place. It has
        # no shards; the last two breakable columns carry sight radius/fuse.
        mine = None
        if a.get('anim') == 'misc/magic8ball_idle':
            blast = _death_blast(Z, names, 'misc/magic8ball_explode') or (512, 140, True)
            detonate = _all_frames(Z, names, 'misc/magic8ball_detonate',
                                   KEX_MINE_DETONATE_FRAMES)
            loop = _all_frames(Z, names, 'misc/magic8ball_detonate_loop',
                               KEX_MINE_LOOP_FRAMES)
            if (len(detonate) != KEX_MINE_DETONATE_FRAMES
                    or len(loop) != KEX_MINE_LOOP_FRAMES):
                raise ValueError('Magic 8 Ball countdown frame contract changed')
            frames = frames[:1] + detonate + loop
            breakable = True
            mine = (KEX_MINE_SIGHT * WORLD, KEX_MINE_FUSE)
            explode = blast

        decor_ticks = 0
        if len(frames) > 1 and not a.get('still') and not breakable:
            # The old eight-frame cap was fine for a flickering fire but cut
            # the Pyramid, ankhs, checkpoint and helicopter loops short. KEX
            # advances by animSpeed/frame.delay at 60 Hz; native Exhumed runs
            # this actor bridge at 30 Hz and totalclock advances four units
            # per native tick, so one authored pose lasts 2*delay/animSpeed
            # totalclock units. All shipping decor animations have one common
            # delay; fail loudly if a future source needs a variable cadence.
            frames = _all_frames(Z, names, art_anim, KEX_DECOR_ANIM_MAX_FRAMES)
            full = parse_anim_full(Z, names, art_anim)
            if len(frames) != len(full) or len(frames) >= KEX_DECOR_ANIM_MAX_FRAMES:
                raise ValueError('%s animated scenery frame count changed' % a['name'])
            delays = {f['delay'] for f in full}
            if len(delays) != 1 or next(iter(delays)) <= 0 or a['animspeed'] <= 0:
                raise ValueError('%s animated scenery cadence is non-uniform' % a['name'])
            exact_ticks = 2.0 * next(iter(delays)) / a['animspeed']
            decor_ticks = int(round(exact_ticks))
            if decor_ticks <= 0 or abs(exact_ticks - decor_ticks) > 1e-6:
                raise ValueError('%s animated scenery cadence is not integral' % a['name'])

        # Every frame of one actor is composited into the SAME box, so the
        # sprite does not jitter as the flame changes shape.
        allrects = []
        for layers in frames:
            rects = []
            for g, base, idx, ox, oy in layers:
                tbl = _sprite_table(Z, names, g, base)
                r = tbl.get(idx)
                sh = sheet(g, base)
                if r is None or sh is None: continue
                rects.append((sh, r, ox, oy))
            allrects.append(rects)
        allrects = [r for r in allrects if r]
        if not allrects: continue

        flat = [x for rects in allrects for x in rects]
        x0 = min(ox for _, _, ox, oy in flat)
        y0 = min(oy for _, _, ox, oy in flat)
        x1 = max(ox + r[2] for _, r, ox, oy in flat)
        y1 = max(oy + r[3] for _, r, ox, oy in flat)
        w, h = max(1, x1 - x0), max(1, y1 - y0)
        if w > 512 or h > 512: continue

        first = tile
        for fi, rects in enumerate(allrects):
            img = _png.blank(w, h)
            for (sw, sh_, spix), r, ox, oy in rects:
                sub = _png.crop(sw, sh_, spix, r[0], r[1], r[2], r[3])
                _png.paste(w, h, img, r[2], r[3], sub, ox - x0, oy - y0)
            fn = 'kexart/actors/a%03d_%d.png' % (t, fi)
            _png.write(os.path.join(outdir, fn), w, h, img)
            defs.append((tile, fn))
            tile += 1
        out[t] = (first, a['scale'], a['fullbright'])
        # The remaster groups its fires, torches and lamps as "Light Sources" --
        # 19 actor types. They declare no lightRadius of their own (only four
        # projectiles do), because in Kex their glow is baked into the vertex
        # colours. Polymost's KEXLIT4 path draws that field directly; the
        # fallback renderer puts the glow back as a real light instead. These
        # are the tiles that should cast that fallback light.
        if is_light:
            lights.append(first)
        if len(allrects) > 1 and not a.get('still') and not breakable:
            # Source frame delays and actor animSpeed determine cadence; KEX
            # randomizeFrames remains a stable per-instance phase offset.
            animdefs.append((first, len(allrects), decor_ticks,
                             int(bool(a.get('randomize')))))
        # Follow the death animation's callback, not the actor's name. Vase09
        # intentionally breaks with fleshbreak.wav, while exploding vases have
        # no local sound. The mine's deathAnim is its arming countdown and plays
        # mineact.wav there; KexBreak_MineTick already does that, so its eventual
        # blast must not replay it as a break sound.
        break_sound = (KEX_BREAK_SOUND_NONE if mine or not breakable
                       else _death_sound(Z, names, a.get('death')))
        blast_radius, blast_damage, blast_toggle = explode
        if (breakable and not mine and blast_damage <= 0
                and _death_spawn_effect(Z, names, a.get('death'))
                    != 'Fx_VaseExplodePuff'):
            raise ValueError('%s no longer uses Fx_VaseExplodePuff'
                             % a['name'])
        wood = int(break_sound == KEX_BREAK_SOUND_WOOD)  # legacy table reader
        if mine:
            breakables.append((first, a['health'], first, 0, a.get('radius', 0),
                               blast_radius, blast_damage) + mine
                              + (wood, int(blast_toggle), break_sound,
                                 a.get('height', 0)))
        elif breakable and len(allrects) > 1:
            # frame 0 is the intact object; the rest are the shards its death
            # anim tosses, one A_TossActor per shard.
            breakables.append((first, a['health'], first + 1, len(allrects) - 1,
                               a.get('radius', 0), blast_radius, blast_damage,
                               0, 0, wood, int(blast_toggle), break_sound,
                               a.get('height', 0)))
    if verbose:
        print("  scenery: %d actor types, %d animated, %d breakable, from the remaster's own sprites"
              % (len(out), len(animdefs), len(breakables)))
    return out, defs, animdefs, breakables, lights


KEX_RAMSES_ANIMS = (
    ('spawn',  'misc/ramses_spawn',     48, 2),
    ('talk',   'misc/ramses_talk',      48, 0),
    ('vanish', 'misc/ramses_vanish',    48, 2),
    ('body',   'misc/ramses_body_rest', 36, 0),
)


def build_ramses_tiles(Z, outdir, tilebase, verbose=True):
    """Composite the remaster's complete Ramses presentation.

    The ordinary decor path intentionally caps an actor at eight frames. Ramses
    is a scripted cinematic actor instead: 55 materialisation frames lead into
    twelve manually selected talk poses, followed by 21 vanish frames. Keep a
    common canvas centred on the KEX actor origin so changing Build tiles never
    makes him jitter, and serialize the ranges for ramses.cpp.
    """
    import kexpng as _png
    names = {n.lower(): n for n in Z.namelist()}
    artdir = os.path.join(outdir, 'kexart', 'ramses')
    os.makedirs(artdir, exist_ok=True)
    for stale in os.listdir(artdir):
        if stale.endswith('.png'):
            os.remove(os.path.join(artdir, stale))

    sheets = {}
    def sheet(group, base):
        key = (group, base)
        if key not in sheets:
            real = names.get(('sprites/%s/%s.png' % (group, base)).lower())
            if real is None:
                sheets[key] = None
            else:
                tmp = os.path.join(artdir, '_sheet.png')
                with open(tmp, 'wb') as f:
                    f.write(Z.read(real))
                sheets[key] = _png.read(tmp)
                os.remove(tmp)
        return sheets[key]

    parsed = {}
    for phase, anim, _repeat, _ticks in KEX_RAMSES_ANIMS:
        frames = _all_frames(Z, names, anim, 1000)
        rectframes = []
        for layers in frames:
            rects = []
            for group, base, index, ox, oy in layers:
                rect = _sprite_table(Z, names, group, base).get(index)
                image = sheet(group, base)
                if rect is not None and image is not None:
                    rects.append((image, rect, ox, oy))
            if rects:
                rectframes.append(rects)
        parsed[phase] = rectframes

    # Spawn/talk/vanish are the same actor and therefore share one origin box.
    # The resting body is much wider and gets its own symmetric box.
    def symmetric_bounds(phases):
        flat = [r for phase in phases for frame in parsed[phase] for r in frame]
        if not flat:
            return None
        mx = max(max(abs(ox), abs(ox + rect[2])) for _img, rect, ox, _oy in flat)
        my = max(max(abs(oy), abs(oy + rect[3])) for _img, rect, _ox, oy in flat)
        return max(1, mx), max(1, my)

    bounds = {
        'spirit': symmetric_bounds(('spawn', 'talk', 'vanish')),
        'body': symmetric_bounds(('body',)),
    }
    defs = []
    rows = []
    tile = tilebase
    for phase, _anim, repeat, ticks in KEX_RAMSES_ANIMS:
        frames = parsed[phase]
        half = bounds['body' if phase == 'body' else 'spirit']
        if not frames or half is None:
            continue
        hx, hy = half
        width, height = hx * 2, hy * 2
        first = tile
        for frame_index, rects in enumerate(frames):
            image = _png.blank(width, height)
            for (sw, sh, pixels), rect, ox, oy in rects:
                sub = _png.crop(sw, sh, pixels, rect[0], rect[1], rect[2], rect[3])
                _png.paste(width, height, image, rect[2], rect[3], sub,
                           hx + ox, hy + oy)
            rel = 'kexart/ramses/%s_%02d.png' % (phase, frame_index)
            _png.write(os.path.join(outdir, rel), width, height, image)
            defs.append((tile, rel))
            tile += 1
        rows.append((phase, first, len(frames), repeat, ticks))

    if verbose:
        print("  Ramses: %d remaster presentation frames (%s)"
              % (len(defs), ', '.join('%s %d' % (r[0], r[2]) for r in rows)))
    return defs, rows


def _write_table(outdir, fn, header, rows, what, verbose=True):
    """One of the tables kexbreak.cpp reads: a "#" header line then one record
    per line. Kept in one place on both sides -- KexTable_Load() there parses
    exactly what this writes."""
    with open(os.path.join(outdir, fn), "w") as f:
        f.write("# %s\n" % header)
        for row in rows:
            f.write(" ".join(str(v) for v in row) + "\n")
    if verbose:
        print("  %s: %d written to %s" % (what, len(rows), fn))


# ---------------------------------------------------------------------------
# Creature art for the stock .SEQ files
#
# The engine animates every creature through STUFF.DAT's .SEQ files, and the
# shareware demo ships all 78 of them -- but 389 of the monster tiles those
# files reference are zero-size in the demo's ART, which is why every wasp,
# sentry, Set, queen and Selkis a converted level spawns is invisible. The SEQ
# structure itself is an engine-side contract (sub-seq order, trigger flags,
# sound events), so it is kept exactly as shipped and only the MISSING TILES
# are filled: KEX_SEQ_TILES (generated by kexseqmap_gen.py from the demo's own
# SEQ data plus the mapping tables in KEXSEQ_PLAN.md) says which remaster anim
# frame and view angle each zero tile shows, and build_seq_tiles() composites
# them into PNGs wired up on the ORIGINAL tile numbers through exhumed.def.
# Chunk placement stays the demo's -- chunks store tile-centre positions, so a
# replacement of similar proportions lands where the original art did.
# ---------------------------------------------------------------------------

def _brace_block(text, start):
    """(body, end) of the brace block whose '{' is at/after start."""
    i = text.index('{', start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i+1:j], j
    raise ValueError('unbalanced braces')


_ROTATIONS = ('rotation_N', 'rotation_NE', 'rotation_E', 'rotation_ES',
              'rotation_S', 'rotation_SW', 'rotation_W', 'rotation_WN')


def _parse_sprites(body):
    """[(group, base, index, ox, oy, flip)] from one sprites { } body."""
    out = []
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)', body):
        ref = m.group(1)
        g, base = ref.split('/', 1) if '/' in ref else ('monsters', ref)
        out.append((g, base, int(m.group(2)), int(m.group(3)), int(m.group(4)),
                    int(m.group(5))))
    return out


def parse_anim_full(Z, names, anim):
    """Every frame of an anim, rotations and all.

    Returns a list of frames; each has delay (default 1), optional goto, and
    either {'rot': [8 sprite-lists]} for an eight-view anim (fixed file order
    N, NE, E, ES, S, SW, W, WN) or {'sprites': [...]} for a single-view one.
    Unlike _all_frames, which flattens a frame's layers for the one-tile decor
    path, this keeps the views apart -- a creature's Build direction d draws
    KEX rotation d."""
    if '/' not in anim:
        return []
    group, animname = anim.split('/', 1)
    for n in Z.namelist():
        ln = n.lower()
        if not (ln.startswith('sprites/%s/' % group.lower()) and ln.endswith('_anim.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        m = re.search(r'^' + re.escape(animname) + r'\s*$', txt, re.M)
        if not m:
            continue
        body, _ = _brace_block(txt, m.end())
        frames = []
        pos = 0
        while True:
            fm = re.search(r'\bframe\b', body[pos:])
            if not fm:
                break
            fbody, fend = _brace_block(body, pos + fm.start())
            pos = pos + fm.start() + (fend - (pos + fm.start()))
            rots = {}
            rpos = 0
            for rname in _ROTATIONS:
                rm = re.search(r'\b' + rname + r'\b(?!E|S|W|N)', fbody)
                if rm:
                    rbody, _ = _brace_block(fbody, rm.end())
                    rots[rname] = _parse_sprites(rbody)
            delay_match = re.search(r'\bdelay\s+(-?\d+)', fbody)
            goto_match = re.search(r'\bgoto\s+"([^"]+)"', fbody)
            sound_match = re.search(r'\bA_PlayLocalSound\s*\(\s*"([^"]+)"',
                                    fbody)
            light_match = re.search(
                r'\bA_SpawnLight\s*\(\s*([-+\d.]+)\s*,\s*([-+\d.]+)\s*,'
                r'\s*([-+\d.]+)\s*,\s*([-+\d.]+)\s*,\s*([-+\d.]+)\s*,'
                r'\s*([-+\d.]+)\s*\)', fbody)
            frame = {
                'delay': int(delay_match.group(1)) if delay_match else 1,
                'goto': goto_match.group(1) if goto_match else None,
                'destroy': bool(re.search(r'\bA_Destroy\s*\(', fbody)),
                'local_sound': sound_match.group(1) if sound_match else None,
                'spawn_orb': bool(re.search(r'\bA_SpawnOrbPickup\s*\(', fbody)),
                'random_item': bool(re.search(r'\bA_RandomItem\s*\(', fbody)),
                'spawn_light': tuple(float(v) for v in light_match.groups())
                    if light_match else None,
            }
            if rots:
                frame['rot'] = [rots.get(r, []) for r in _ROTATIONS]
            else:
                frame['sprites'] = _parse_sprites(fbody)
            frames.append(frame)
        if frames:
            return frames
    return []


# Pixels of demo art per pixel of remaster art, per creature, folding in the
# actor's own kex scale. Calibrated where a visible demo reference tile exists
# (the sentry's attack tile 3226 survives at 97x113) and carried to the others
# by each actor's scale; tweakable per creature after a visual pass.
KEX_SEQ_PXSCALE = {}


def build_seq_tiles(Z, outdir, verbose=True):
    """Composite the remaster frames KEX_SEQ_TILES names into PNGs on the
    stock tile numbers. Returns def rows [(tile, relpath)]."""
    try:
        from kexseqtiles import KEX_SEQ_TILES, KEX_SEQ_SCALE
    except ImportError:
        if verbose:
            print("  creature art: kexseqtiles.py missing; stock SEQ tiles stay empty")
        return []
    import kexpng as _png
    names = {n.lower(): n for n in Z.namelist()}
    art_dir = os.path.join(outdir, 'kexart', 'seq')
    os.makedirs(art_dir, exist_ok=True)
    for stale in os.listdir(art_dir):
        if stale.endswith('.png'):
            os.remove(os.path.join(art_dir, stale))

    sheets = {}
    def sheet(group, base):
        key = (group, base)
        if key not in sheets:
            real = names.get(('sprites/%s/%s.png' % (group, base)).lower())
            if real is None:
                sheets[key] = None
            else:
                data = Z.read(real)
                tmp = os.path.join(art_dir, '_tmp_sheet.png')
                with open(tmp, 'wb') as f:
                    f.write(data)
                sheets[key] = _png.read(tmp)
                os.remove(tmp)
        return sheets[key]

    anims = {}
    def frames_of(anim):
        if anim not in anims:
            anims[anim] = parse_anim_full(Z, names, anim)
        return anims[anim]

    rows = []
    made = 0
    tiles = dict(KEX_SEQ_TILES)
    tiles.update(KEX_SEQ_EXTRA_TILES)
    scales = dict(KEX_SEQ_SCALE)
    scales.update(KEX_SEQ_EXTRA_SCALE)
    for tile, (creature, anim, fi, d) in sorted(tiles.items()):
        frames = frames_of(anim)
        if not frames or fi >= len(frames):
            continue
        fr = frames[fi]
        layers = fr['rot'][d] if 'rot' in fr else fr.get('sprites', [])
        if not layers:
            continue
        rects = []
        for g, base, idx, ox, oy, flip in layers:
            tbl = _sprite_table(Z, names, g, base)
            r = tbl.get(idx)
            sh = sheet(g, base)
            if r is None or sh is None:
                continue
            rects.append((sh, r, ox, oy, flip))
        if not rects:
            continue
        x0 = min(ox for _s, _r, ox, oy, _f in rects)
        y0 = min(oy for _s, _r, ox, oy, _f in rects)
        x1 = max(ox + r[2] for _s, r, ox, oy, _f in rects)
        y1 = max(oy + r[3] for _s, r, ox, oy, _f in rects)
        w, h = max(1, x1 - x0), max(1, y1 - y0)
        img = _png.blank(w, h)
        for (sw, sh_, spix), r, ox, oy, flip in rects:
            sub = _png.crop(sw, sh_, spix, r[0], r[1], r[2], r[3])
            if flip:
                flipped = bytearray(len(sub))
                for yy in range(r[3]):
                    row = sub[yy*r[2]*4:(yy+1)*r[2]*4]
                    for xx in range(r[2]):
                        flipped[(yy*r[2]+xx)*4:(yy*r[2]+xx+1)*4] = \
                            row[(r[2]-1-xx)*4:(r[2]-xx)*4]
                sub = flipped
            _png.paste(w, h, img, r[2], r[3], sub, ox - x0, oy - y0)
        scale = scales.get(creature, 1.0)
        sx, sy = scale if isinstance(scale, tuple) else (scale, scale)
        if abs(sx - 1.0) > 1e-3 or abs(sy - 1.0) > 1e-3:
            nw, nh = max(1, int(round(w*sx))), max(1, int(round(h*sy)))
            img = _png.resize(w, h, img, nw, nh)
            w, h = nw, nh
        fn = 'kexart/seq/t%04d.png' % tile
        _png.write(os.path.join(outdir, fn), w, h, img)
        rows.append((tile, fn))
        made += 1
    if verbose:
        print("  creature art: %d stock SEQ tiles filled from the remaster's sprites"
              % made)
    return rows


def _build_effect_frame_tiles(Z, names, outdir, frames, artdir_rel,
                              basename, tile_base, tint, label):
    """Composite single-view KEX effect frames on one actor-centred canvas."""
    import kexpng as _png

    artdir = os.path.join(outdir, artdir_rel)
    os.makedirs(artdir, exist_ok=True)
    for stale in os.listdir(artdir):
        if stale.endswith('.png'):
            os.remove(os.path.join(artdir, stale))

    sheets = {}
    def sheet(group, base):
        key = (group, base)
        if key not in sheets:
            real = names.get(('sprites/%s/%s.png' % key).lower())
            if real is None:
                raise ValueError('missing %s sprite sheet %s/%s'
                                 % (label, group, base))
            tmp = os.path.join(artdir, '_sheet.png')
            with open(tmp, 'wb') as f:
                f.write(Z.read(real))
            sheets[key] = _png.read(tmp)
            os.remove(tmp)
        return sheets[key]

    composed = []
    extent_x = extent_y = 0
    for frame in frames:
        layers = frame.get('sprites', [])
        if not layers or 'rot' in frame:
            raise ValueError('%s is no longer single-view art' % label)
        rects = []
        for group, base, index, ox, oy, flip in layers:
            rect = _sprite_table(Z, names, group, base).get(index)
            if rect is None:
                raise ValueError('missing %s sprite %s/%s:%d'
                                 % (label, group, base, index))
            rects.append((sheet(group, base), rect, ox, oy, flip))
            extent_x = max(extent_x, -ox, ox + rect[2])
            extent_y = max(extent_y, -oy, oy + rect[3])
        composed.append(rects)

    # A symmetric transparent canvas keeps the actor origin at the Build tile
    # centre on every frame while retaining the authored per-layer offsets.
    width, height = max(2, extent_x * 2), max(2, extent_y * 2)
    rows = []
    for frame_index, rects in enumerate(composed):
        image = _png.blank(width, height)
        for (sw, sh, pixels), rect, ox, oy, flip in rects:
            sub = _png.crop(sw, sh, pixels, rect[0], rect[1], rect[2], rect[3])
            if flip:
                flipped = bytearray(len(sub))
                for yy in range(rect[3]):
                    for xx in range(rect[2]):
                        src = (yy * rect[2] + (rect[2] - 1 - xx)) * 4
                        dst = (yy * rect[2] + xx) * 4
                        flipped[dst:dst+4] = sub[src:src+4]
                sub = flipped
            _png.paste(width, height, image, rect[2], rect[3], sub,
                       extent_x + ox, extent_y + oy)
        # kexActor::color multiplies source art at draw time.
        image = _png.tint(width, height, image, *tint)
        rel = '%s/%s_%d.png' % (artdir_rel, basename, frame_index)
        _png.write(os.path.join(outdir, rel), width, height, image)
        rows.append((tile_base + frame_index, rel))
    return rows


def build_kex_muzzle_sequence(Z, outdir, verbose=True):
    """Write Special_FireballSpawner's exact shooting-puff art and timing.

    Retail creates Fx_FireballShootPuff: effects/magicPuff at scale 2,
    animSpeed .25, fullbright, multiplied by (0.75, 0.25, 0.25). Its ninth
    frame executes A_Destroy as soon as that frame is entered, so frames 0..7
    are the visible run. KEX updates at 60 Hz; two copies of each frame produce
    the same 15 Hz cadence in Exhumed's 30 Hz simulation.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actors = load_kex_actors(Z)
    muzzle = next((d for d in actors.values()
                   if d['name'] == 'Fx_FireballShootPuff'), None)
    if (muzzle is None or muzzle['anim'] != 'effects/magicPuff'
            or abs(muzzle['scale'] - 2.0) > 1e-6 or not muzzle['fullbright']):
        raise ValueError('Fx_FireballShootPuff definition changed')

    frames = parse_anim_full(Z, names, muzzle['anim'])
    if (len(frames) != KEX_MUZZLE_VISIBLE_FRAMES + 1
            or any(f['delay'] != 1 for f in frames)
            or not frames[-1].get('destroy')):
        raise ValueError('effects/magicPuff timing/lifetime changed')
    frames = frames[:KEX_MUZZLE_VISIBLE_FRAMES]
    rows = _build_effect_frame_tiles(
        Z, names, outdir, frames, 'kexart/muzzle', 'fireball',
        KEX_MUZZLE_TILE_BASE, (191, 64, 64), 'effects/magicPuff')

    tiles = [KEX_MUZZLE_TILE_BASE + i
             for i in range(KEX_MUZZLE_VISIBLE_FRAMES)
             for _ in range(KEX_MUZZLE_FRAME_HOLD)]
    count = len(tiles)
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0], seq_size=[count], seq_flag=[-1],
               fr_base=list(range(count)), fr_size=[1] * count,
               fr_flag=[4] * count,       # Fx_FireballShootPuff.fullbright
               ck_x=[0] * count, ck_y=[0] * count,
               ck_pict=tiles, ck_flag=[0] * count,
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXMUZZLE.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  fireball traps: exact %d-frame red muzzle puff on tiles %d-%d'
              % (count, KEX_MUZZLE_TILE_BASE,
                 KEX_MUZZLE_TILE_BASE + KEX_MUZZLE_VISIBLE_FRAMES - 1))
    return rows


def build_kex_impact_sequence(Z, outdir, verbose=True):
    """Write Fx_FireballExplode's exact visible frames and cadence.

    The spawn anim is a zero-delay alias which creates a radius-512 orange
    light, then KEX's update code ChangesAnim and increments immediately. That
    skips magicPuff frame 0. Frames 1..7 render at 15 fps; frame 8 executes
    A_Destroy on entry and is never visible. The native bridge owns the light,
    sound and boulder's +2-unit spawn offset because those are callbacks, not
    pixels in this sequence.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actors = load_kex_actors(Z)
    impact = next((d for d in actors.values()
                   if d['name'] == 'Fx_FireballExplode'), None)
    if (impact is None or impact['anim'] != 'effects/fireballPuff'
            or abs(impact['scale'] - 2.0) > 1e-6
            or not impact['fullbright']):
        raise ValueError('Fx_FireballExplode definition changed')

    effects_real = names.get('defs/actors/effects.txt')
    effects_text = Z.read(effects_real).decode('latin-1').replace('\r', '') \
        if effects_real else ''
    effect_match = re.search(r'Actor\s+Fx_FireballExplode\s*\{(.*?)\n\}',
                             effects_text, re.S)
    compact = re.sub(r'\s+', '', effect_match.group(1)) \
        if effect_match else ''
    for token in ('animSpeed0.25', 'color"0.750.3750.25"'):
        if token not in compact:
            raise ValueError('Fx_FireballExplode recipe changed: %s' % token)

    alias = parse_anim_full(Z, names, 'effects/fireballPuff')
    expected_light = (512.0, .75, .375, .25, 15.0, 2.0)
    if (len(alias) != 1 or alias[0]['delay'] != 0
            or alias[0].get('goto') != 'effects/magicPuff'
            or alias[0].get('sprites')
            or alias[0].get('spawn_light') != expected_light):
        raise ValueError('effects/fireballPuff alias/light contract changed')

    magic = parse_anim_full(Z, names, 'effects/magicPuff')
    if (len(magic) != 9 or any(f['delay'] != 1 for f in magic)
            or not magic[-1].get('destroy')
            or any(not f.get('sprites') for f in magic[:-1])):
        raise ValueError('effects/magicPuff timing/lifetime changed')
    first = KEX_IMPACT_FIRST_FRAME
    frames = magic[first:first + KEX_IMPACT_VISIBLE_FRAMES]
    rows = _build_effect_frame_tiles(
        Z, names, outdir, frames, 'kexart/impact', 'fireball',
        KEX_IMPACT_TILE_BASE, (191, 96, 64), 'effects/fireballPuff')

    tiles = [KEX_IMPACT_TILE_BASE + i
             for i in range(KEX_IMPACT_VISIBLE_FRAMES)
             for _ in range(KEX_IMPACT_FRAME_HOLD)]
    count = len(tiles)
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0], seq_size=[count], seq_flag=[-1],
               fr_base=list(range(count)), fr_size=[1] * count,
               fr_flag=[4] * count,       # Fx_FireballExplode.fullbright
               ck_x=[0] * count, ck_y=[0] * count,
               ck_pict=tiles, ck_flag=[0] * count,
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXIMPACT.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  projectile impacts: exact %d-frame orange fireball puff on '
              'tiles %d-%d' %
              (count, KEX_IMPACT_TILE_BASE,
               KEX_IMPACT_TILE_BASE + KEX_IMPACT_VISIBLE_FRAMES - 1))
    return rows


def build_kex_cobra_impact_sequences(Z, outdir, verbose=True):
    """Write the red/green cobra puffs and their four-callback timer.

    cobra_{red,green}_death invokes a randomized puff on entry and again after
    3, 6 and 9 Exhumed ticks, then destroys at tick 12. Each spawned effect is
    a zero-delay alias into magicPuff frame 1 and has the same fourteen-tick
    visible lifetime as Fx_FireballExplode, with its own tint and light radius.
    KEXCOBRACTL.SEQ is invisible at runtime; its twelve frames make the delayed
    callbacks ordinary serialized Anim state rather than an unsaved side timer.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actors = load_kex_actors(Z)
    actor_by_name = {d['name']: d for d in actors.values()}
    effects_real = names.get('defs/actors/effects.txt')
    effects_text = Z.read(effects_real).decode('latin-1').replace('\r', '') \
        if effects_real else ''
    magic = parse_anim_full(Z, names, 'effects/magicPuff')
    if (len(magic) != 9 or any(f['delay'] != 1 for f in magic)
            or not magic[-1].get('destroy')
            or any(not f.get('sprites') for f in magic[:-1])):
        raise ValueError('effects/magicPuff timing/lifetime changed')
    frames = magic[1:1 + KEX_COBRA_VISIBLE_FRAMES]

    specs = (
        ('Fx_RedCobraExplode', 'effects/cobraPuff2',
         (384.0, .75, .375, .375, 15.0, 1.0),
         KEX_COBRA_RED_TILE_BASE, (191, 64, 64),
         'kexart/cobrared', 'cobra', 'KEXCOBRARED.SEQ'),
        ('Fx_GreenCobraExplode', 'effects/cobraPuff1',
         (768.0, .375, .75, .375, 15.0, 1.0),
         KEX_COBRA_GREEN_TILE_BASE, (96, 191, 96),
         'kexart/cobragreen', 'cobra', 'KEXCOBRAGREEN.SEQ'),
    )
    rows = []
    for actor_name, alias_name, expected_light, tile_base, tint, artdir, basename, seqname in specs:
        actor = actor_by_name.get(actor_name)
        if (actor is None or actor['anim'] != alias_name
                or abs(actor['scale'] - 2.0) > 1e-6
                or not actor['fullbright']):
            raise ValueError('%s definition changed' % actor_name)
        actor_match = re.search(r'Actor\s+' + re.escape(actor_name)
                                + r'\s*\{(.*?)\n\}', effects_text, re.S)
        compact = re.sub(r'\s+', '', actor_match.group(1)) \
            if actor_match else ''
        if 'animSpeed0.25' not in compact:
            raise ValueError('%s animSpeed changed' % actor_name)

        alias = parse_anim_full(Z, names, alias_name)
        if (len(alias) != 1 or alias[0]['delay'] != 0
                or alias[0].get('goto') != 'effects/magicPuff'
                or alias[0].get('sprites')
                or alias[0].get('spawn_light') != expected_light):
            raise ValueError('%s alias/light contract changed' % alias_name)

        rows.extend(_build_effect_frame_tiles(
            Z, names, outdir, frames, artdir, basename, tile_base, tint,
            alias_name))
        tiles = [tile_base + i for i in range(KEX_COBRA_VISIBLE_FRAMES)
                 for _ in range(KEX_COBRA_FRAME_HOLD)]
        count = len(tiles)
        seq = dict(tag=b'IH', centerx=0, centery=0,
                   seq_base=[0], seq_size=[count], seq_flag=[-1],
                   fr_base=list(range(count)), fr_size=[1] * count,
                   fr_flag=[4] * count,
                   ck_x=[0] * count, ck_y=[0] * count,
                   ck_pict=tiles, ck_flag=[0] * count,
                   snd_names=[], snd_events=[], trailing=b'')
        with open(os.path.join(outdir, seqname), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(seq))

    controller_count = KEX_COBRA_CONTROLLER_FRAMES
    controller = dict(tag=b'IH', centerx=0, centery=0,
                      seq_base=[0], seq_size=[controller_count], seq_flag=[-1],
                      fr_base=list(range(controller_count)),
                      fr_size=[1] * controller_count,
                      fr_flag=[0] * controller_count,
                      ck_x=[0] * controller_count,
                      ck_y=[0] * controller_count,
                      ck_pict=[0] * controller_count,
                      ck_flag=[0] * controller_count,
                      snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXCOBRACTL.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(controller))

    if verbose:
        print('  projectile impacts: exact red/green four-puff cobra deaths '
              '(ticks 0/3/6/9, tiles %d-%d)' %
              (KEX_COBRA_RED_TILE_BASE,
               KEX_COBRA_GREEN_TILE_BASE + KEX_COBRA_VISIBLE_FRAMES - 1))
    return rows


def build_kex_anubis_impact_sequence(Z, outdir, verbose=True):
    """Write Proj_Anubis's own exact thirteen-frame visible death run."""
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actors = load_kex_actors(Z)
    anubis = next((d for d in actors.values() if d['name'] == 'Proj_Anubis'), None)
    if (anubis is None or anubis['death'] != 'projectiles/anubis_fireball_explode'
            or abs(anubis['scale'] - 1.0) > 1e-6
            or not anubis['fullbright']):
        raise ValueError('Proj_Anubis impact definition changed')

    frames = parse_anim_full(Z, names,
                             'projectiles/anubis_fireball_explode')
    if (len(frames) != KEX_ANUBIS_IMPACT_VISIBLE_FRAMES + 1
            or any(f['delay'] != 1 for f in frames)
            or any(f.get('destroy') for f in frames[:-1])
            or not frames[-1].get('destroy')
            or any(not f.get('sprites') for f in frames)):
        raise ValueError('anubis_fireball_explode timing/lifetime changed')
    visible = frames[:KEX_ANUBIS_IMPACT_VISIBLE_FRAMES]
    rows = _build_effect_frame_tiles(
        Z, names, outdir, visible, 'kexart/anubisimpact', 'fireball',
        KEX_ANUBIS_IMPACT_TILE_BASE, (255, 255, 255),
        'projectiles/anubis_fireball_explode')

    tiles = [KEX_ANUBIS_IMPACT_TILE_BASE + i
             for i in range(KEX_ANUBIS_IMPACT_VISIBLE_FRAMES)]
    count = len(tiles)
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0], seq_size=[count], seq_flag=[-1],
               fr_base=list(range(count)), fr_size=[1] * count,
               fr_flag=[4] * count,
               ck_x=[0] * count, ck_y=[0] * count,
               ck_pict=tiles, ck_flag=[0] * count,
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXANUIMPACT.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  projectile impacts: exact %d-frame Anubis fireball death on '
              'tiles %d-%d' %
              (count, KEX_ANUBIS_IMPACT_TILE_BASE,
               KEX_ANUBIS_IMPACT_TILE_BASE + count - 1))
    return rows


def build_kex_explosion_sequences(Z, outdir, verbose=True):
    """Write the exact shared land and underwater explosion actors.

    Both effects run at the default KEX actor rate (.5 at 60 Hz), hence one
    source frame per Exhumed tick. The land actor renders source frames 0..20.
    The water actor is blank on frame 0, renders 1..18, then remains blank on
    19 and 20; source frame 21 destroys immediately and is never displayed.
    Their frame-zero sound and dynamic-light callbacks remain native because
    they must happen when the actor is successfully spawned, before the
    caller's A_RadialBlast.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actors = load_kex_actors(Z)
    actor_by_name = {d['name']: d for d in actors.values()}
    effects_real = names.get('defs/actors/effects.txt')
    effects_text = Z.read(effects_real).decode('latin-1').replace('\r', '') \
        if effects_real else ''
    anim_real = names.get('sprites/effects/effects_anim.txt')
    anim_text = Z.read(anim_real).decode('latin-1').replace('\r', '') \
        if anim_real else ''

    def actor_contract(actor_name, anim_name, scale):
        actor = actor_by_name.get(actor_name)
        if (actor is None or actor['anim'] != anim_name
                or abs(actor['scale'] - scale) > 1e-6
                or not actor['fullbright']):
            raise ValueError('%s definition changed' % actor_name)
        match = re.search(r'Actor\s+' + re.escape(actor_name)
                          + r'\s*\{(.*?)\n\}', effects_text, re.S)
        compact = re.sub(r'\s+', '', match.group(1)) if match else ''
        if 'animSpeed' in compact:
            raise ValueError('%s no longer uses default animSpeed .5'
                             % actor_name)

    def first_frame_contract(anim_name, sound_path, expected_light):
        match = re.search(r'^' + re.escape(anim_name) + r'\s*$',
                          anim_text, re.M)
        if not match:
            raise ValueError('%s animation is missing' % anim_name)
        body, _ = _brace_block(anim_text, match.end())
        frame = re.search(r'\bframe\b', body)
        if not frame:
            raise ValueError('%s has no first frame' % anim_name)
        first, _ = _brace_block(body, frame.end())
        compact = re.sub(r'\s+', '', first)
        sound = 'A_PlayLocalSound("%s")' % sound_path
        light = ('A_SpawnLight(%g,%g,%g,%g,%g,%g)' % expected_light)
        if sound not in compact or light not in compact \
                or compact.index(sound) > compact.index(light):
            raise ValueError('%s frame-zero sound/light recipe changed'
                             % anim_name)

    light = (768.0, .75, .375, .25, 20.0, 10.0)
    actor_contract('Fx_Explosion01', 'effects/explosion01', 4.0)
    actor_contract('Fx_Explosion01_Water',
                   'effects/explosion01_water', 2.0)
    first_frame_contract('explosion01', 'sounds/explode03.wav', light)
    first_frame_contract('explosion01_water', 'sounds/explode02.wav', light)

    land = parse_anim_full(Z, names, 'effects/explosion01')
    water = parse_anim_full(Z, names, 'effects/explosion01_water')
    if (len(land) != KEX_EXPLOSION_VISIBLE_FRAMES + 1
            or any(f['delay'] != 1 for f in land)
            or any(f.get('destroy') for f in land[:-1])
            or not land[-1].get('destroy')
            or land[0].get('spawn_light') != light
            or any(not f.get('sprites') for f in land[:-1])):
        raise ValueError('effects/explosion01 timing/art/light changed')
    if (len(water) != KEX_WATER_EXPLOSION_LIFETIME + 1
            or any(f['delay'] != 1 for f in water)
            or any(f.get('destroy') for f in water[:-1])
            or not water[-1].get('destroy')
            or water[0].get('spawn_light') != light
            or water[0].get('sprites')
            or any(not water[i].get('sprites') for i in range(1, 19))
            or any(water[i].get('sprites') for i in (19, 20, 21))):
        raise ValueError('effects/explosion01_water timing/art/light changed')

    land_visible = land[:KEX_EXPLOSION_VISIBLE_FRAMES]
    water_visible = water[1:1 + KEX_WATER_EXPLOSION_VISIBLE_FRAMES]
    rows = _build_effect_frame_tiles(
        Z, names, outdir, land_visible, 'kexart/explosion', 'explosion',
        KEX_EXPLOSION_TILE_BASE, (255, 255, 255), 'effects/explosion01')
    rows.extend(_build_effect_frame_tiles(
        Z, names, outdir, water_visible, 'kexart/waterexplosion', 'water',
        KEX_WATER_EXPLOSION_TILE_BASE, (255, 255, 255),
        'effects/explosion01_water'))

    land_count = KEX_EXPLOSION_VISIBLE_FRAMES
    land_seq = dict(tag=b'IH', centerx=0, centery=0,
                    seq_base=[0], seq_size=[land_count], seq_flag=[-1],
                    fr_base=list(range(land_count)), fr_size=[1] * land_count,
                    fr_flag=[4] * land_count,
                    ck_x=[0] * land_count, ck_y=[0] * land_count,
                    ck_pict=[KEX_EXPLOSION_TILE_BASE + i
                             for i in range(land_count)],
                    ck_flag=[0] * land_count,
                    snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXEXPLODE.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(land_seq))

    water_count = KEX_WATER_EXPLOSION_LIFETIME
    frame_base = []
    frame_size = []
    water_tiles = []
    for frame in range(water_count):
        frame_base.append(len(water_tiles))
        if 1 <= frame <= KEX_WATER_EXPLOSION_VISIBLE_FRAMES:
            frame_size.append(1)
            water_tiles.append(KEX_WATER_EXPLOSION_TILE_BASE + frame - 1)
        else:
            frame_size.append(0)
    water_seq = dict(tag=b'IH', centerx=0, centery=0,
                     seq_base=[0], seq_size=[water_count], seq_flag=[-1],
                     fr_base=frame_base, fr_size=frame_size,
                     fr_flag=[4] * water_count,
                     ck_x=[0] * len(water_tiles),
                     ck_y=[0] * len(water_tiles), ck_pict=water_tiles,
                     ck_flag=[0] * len(water_tiles),
                     snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXWATEREXP.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(water_seq))

    if verbose:
        print('  explosions: exact 21-tick land/water actors on tiles %d-%d '
              '(water blanks 0/19/20)' %
              (KEX_EXPLOSION_TILE_BASE,
               KEX_WATER_EXPLOSION_TILE_BASE
               + KEX_WATER_EXPLOSION_VISIBLE_FRAMES - 1))
    return rows


def build_kex_vase_puff_sequence(Z, outdir, verbose=True):
    """Write Fx_VaseExplodePuff's exact tinted magic-puff timeline."""
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actors = load_kex_actors(Z)
    vase = next((d for d in actors.values()
                 if d['name'] == 'Fx_VaseExplodePuff'), None)
    if (vase is None or vase['anim'] != 'effects/vasePuff'
            or abs(vase['scale'] - 2.0) > 1e-6 or not vase['fullbright']):
        raise ValueError('Fx_VaseExplodePuff definition changed')

    effects_real = names.get('defs/actors/effects.txt')
    effects_text = Z.read(effects_real).decode('latin-1').replace('\r', '') \
        if effects_real else ''
    match = re.search(r'Actor\s+Fx_VaseExplodePuff\s*\{(.*?)\n\}',
                      effects_text, re.S)
    compact = re.sub(r'\s+', '', match.group(1)) if match else ''
    for token in ('animSpeed0.25', 'color"0.50.50.25"'):
        if token not in compact:
            raise ValueError('Fx_VaseExplodePuff recipe changed: %s' % token)

    alias = parse_anim_full(Z, names, 'effects/vasePuff')
    expected_light = (384.0, .5, .5, .25, 15.0, 2.0)
    if (len(alias) != 1 or alias[0]['delay'] != 0
            or alias[0].get('goto') != 'effects/magicPuff'
            or alias[0].get('sprites')
            or alias[0].get('spawn_light') != expected_light):
        raise ValueError('effects/vasePuff alias/light contract changed')

    magic = parse_anim_full(Z, names, 'effects/magicPuff')
    if (len(magic) != 9 or any(f['delay'] != 1 for f in magic)
            or not magic[-1].get('destroy')
            or any(not f.get('sprites') for f in magic[:-1])):
        raise ValueError('effects/magicPuff timing/lifetime changed')
    frames = magic[1:1 + KEX_VASE_PUFF_VISIBLE_FRAMES]
    rows = _build_effect_frame_tiles(
        Z, names, outdir, frames, 'kexart/vasepuff', 'vase',
        KEX_VASE_PUFF_TILE_BASE, (128, 128, 64), 'effects/vasePuff')

    tiles = [KEX_VASE_PUFF_TILE_BASE + i
             for i in range(KEX_VASE_PUFF_VISIBLE_FRAMES)
             for _ in range(KEX_VASE_PUFF_FRAME_HOLD)]
    count = len(tiles)
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0], seq_size=[count], seq_flag=[-1],
               fr_base=list(range(count)), fr_size=[1] * count,
               fr_flag=[4] * count,
               ck_x=[0] * count, ck_y=[0] * count,
               ck_pict=tiles, ck_flag=[0] * count,
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXVASEPUFF.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  breakables: exact 14-tick Fx_VaseExplodePuff on tiles %d-%d'
              % (KEX_VASE_PUFF_TILE_BASE,
                 KEX_VASE_PUFF_TILE_BASE
                 + KEX_VASE_PUFF_VISIBLE_FRAMES - 1))
    return rows


def build_kex_orb_spawn_sequences(Z, outdir, verbose=True):
    """Write the exact delayed Fx_AmmoSpawn/Fx_HealthSpawn actors."""
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actor_by_name = {d['name']: d for d in load_kex_actors(Z).values()}
    expected_actors = {
        'Fx_AmmoSpawn': ('0.2850.2851.0', KEX_AMMO_SPAWN_TILE_BASE,
                         (73, 73, 255), 'ammo'),
        'Fx_HealthSpawn': ('1.00.2850.285', KEX_HEALTH_SPAWN_TILE_BASE,
                           (255, 73, 73), 'health'),
    }
    effects_real = names.get('defs/actors/effects.txt')
    effects_text = Z.read(effects_real).decode('latin-1').replace('\r', '') \
        if effects_real else ''
    for actor_name, (color, _base, _tint, _label) in expected_actors.items():
        actor = actor_by_name.get(actor_name)
        if (actor is None or actor.get('anim') != 'effects/orb_spawn'
                or abs(actor.get('scale', 0) - 1.0) > 1e-6
                or actor.get('height') != 64 or actor.get('radius') != 64
                or not actor.get('fullbright')):
            raise ValueError('%s definition changed' % actor_name)
        match = re.search(r'Actor\s+' + re.escape(actor_name)
                          + r'\s*\{(.*?)\n\}', effects_text, re.S)
        compact = re.sub(r'\s+', '', match.group(1)) if match else ''
        for token in ('transparency128', 'color"%s"' % color):
            if token not in compact:
                raise ValueError('%s recipe changed: %s' % (actor_name, token))

    frames = parse_anim_full(Z, names, 'effects/orb_spawn')
    holds = (3, 3, 3, 3, 3, 2, 2, 4)
    if (len(frames) != 25 or frames[0].get('delay') != 20
            or frames[0].get('sprites') or frames[0].get('destroy')
            or any(f.get('delay') != 1 for f in frames[1:])
            or frames[1].get('local_sound') != 'sounds/spawn.wav'
            or any(f.get('local_sound') for f in frames[2:])
            or not frames[23].get('spawn_orb')
            or any(f.get('spawn_orb') for f in frames[:23] + frames[24:])
            or not frames[24].get('destroy') or frames[24].get('sprites')
            or any(not f.get('sprites') for f in frames[1:24])):
        raise ValueError('effects/orb_spawn timing/callback contract changed')

    unique = []
    cursor = 1
    for hold in holds:
        group = frames[cursor:cursor + hold]
        if len(group) != hold or any(f.get('sprites') != group[0].get('sprites')
                                     for f in group[1:]):
            raise ValueError('effects/orb_spawn visual hold cadence changed')
        unique.append(group[0])
        cursor += hold
    if cursor != 24:
        raise ValueError('effects/orb_spawn visible lifetime changed')

    rows = []
    for actor_name, (_color, tile_base, tint, label) in expected_actors.items():
        rows.extend(_build_effect_frame_tiles(
            Z, names, outdir, unique, 'kexart/%sspawn' % label, label,
            tile_base, tint, actor_name))

        visual_tiles = [tile_base + i for i, hold in enumerate(holds)
                        for _ in range(hold)]
        frame_count = KEX_ORB_SPAWN_BLANK_TICKS + len(visual_tiles)
        if frame_count != KEX_ORB_SPAWN_LIFETIME:
            raise ValueError('orb-spawn generated lifetime changed')
        seq = dict(tag=b'DS', centerx=0, centery=0,
                   seq_base=[0], seq_size=[frame_count], seq_flag=[-1],
                   fr_base=[0] * KEX_ORB_SPAWN_BLANK_TICKS
                           + list(range(len(visual_tiles))),
                   fr_size=[0] * KEX_ORB_SPAWN_BLANK_TICKS
                           + [1] * len(visual_tiles),
                   fr_flag=[4] * frame_count,
                   ck_x=[0] * len(visual_tiles),
                   ck_y=[0] * len(visual_tiles),
                   ck_pict=visual_tiles, ck_flag=[0] * len(visual_tiles),
                   snd_names=[b'ITEM_SPE'], snd_events=[(20, 0)], trailing=b'')
        filename = 'KEX%sSPAWN.SEQ' % label.upper()
        with open(os.path.join(outdir, filename), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(seq))

    if verbose:
        print('  enemy drops: exact 20-blank + 23-visible-tick ammo/health '
              'spawn actors on tiles %d-%d' %
              (KEX_AMMO_SPAWN_TILE_BASE,
               KEX_HEALTH_SPAWN_TILE_BASE + KEX_ORB_SPAWN_UNIQUE_FRAMES - 1))
    return rows


def build_kex_gib_death_sequence(Z, outdir, verbose=True):
    """Write Fx_Gibs01's exact blank/art/blank delayed-drop actor."""
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actor = next((d for d in load_kex_actors(Z).values()
                  if d['name'] == 'Fx_Gibs01'), None)
    if (actor is None or actor.get('anim') != 'effects/gib_death'
            or actor.get('radius') != 64 or actor.get('height') != 64
            or abs(actor.get('scale', 0) - 1.0) > 1e-6
            or not actor.get('fullbright')):
        raise ValueError('Fx_Gibs01 actor definition changed')

    frames = parse_anim_full(Z, names, actor['anim'])
    if (len(frames) != KEX_GIB_DEATH_LIFETIME
            or any(f.get('delay') != 1 for f in frames)
            or frames[0].get('sprites')
            or any(not f.get('sprites') for f in frames[1:14])
            or any(f.get('sprites') for f in frames[14:])
            or any(f.get('destroy') for f in frames[:-1])
            or not frames[-1].get('destroy')
            or any(f.get('random_item') for f in frames[:-1])
            or not frames[-1].get('random_item')
            or any(f.get('local_sound') for f in frames)):
        raise ValueError('effects/gib_death timing/callback contract changed')

    unique = []
    visual_index = []
    for frame in frames[1:14]:
        signature = tuple(tuple(layer) for layer in frame['sprites'])
        signatures = [tuple(tuple(layer) for layer in f['sprites'])
                      for f in unique]
        if signature not in signatures:
            unique.append(frame)
            signatures.append(signature)
        visual_index.append(signatures.index(signature))
    if (len(unique) != KEX_GIB_DEATH_UNIQUE_FRAMES
            or visual_index != [0, 0, 1, 1, 2, 3, 3, 4, 5, 6, 7, 8, 9]):
        raise ValueError('effects/gib_death visual pose cadence changed')

    rows = _build_effect_frame_tiles(
        Z, names, outdir, unique, 'kexart/gibdeath', 'gib',
        KEX_GIB_DEATH_TILE_BASE, (255, 255, 255), 'effects/gib_death')

    frame_sizes = [0] + [1] * 13 + [0] * 5
    frame_bases = []
    cursor = 0
    for size in frame_sizes:
        frame_bases.append(cursor)
        cursor += size
    tiles = [KEX_GIB_DEATH_TILE_BASE + i for i in visual_index]
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0], seq_size=[KEX_GIB_DEATH_LIFETIME], seq_flag=[-1],
               fr_base=frame_bases, fr_size=frame_sizes,
               fr_flag=[4] * KEX_GIB_DEATH_LIFETIME,
               ck_x=[0] * len(tiles), ck_y=[0] * len(tiles),
               ck_pict=tiles, ck_flag=[0] * len(tiles),
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXGIBDEATH.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  enemy gibs: exact blank + 13-art + 5-blank Fx_Gibs01 '
              'on tiles %d-%d' %
              (KEX_GIB_DEATH_TILE_BASE,
               KEX_GIB_DEATH_TILE_BASE + KEX_GIB_DEATH_UNIQUE_FRAMES - 1))
    return rows


def build_kex_water_splash_sequence(Z, outdir, verbose=True):
    """Write Fx_WaterSplash's exact 23-frame surface-crossing actor.

    Fx_WaterSplash has the default KEX actor animation rate (.5 at 60 Hz),
    so each source frame corresponds to one PCExhumed 30-Hz animation tick.
    The splash actor itself has no callbacks; the crossing actor owns its
    splash01 sound before this actor is spawned.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    actor = next((d for d in load_kex_actors(Z).values()
                  if d['name'] == 'Fx_WaterSplash'), None)
    if (actor is None or actor.get('anim') != 'effects/splash'
            or actor.get('radius') != 64 or actor.get('height') != 64
            or abs(actor.get('scale', 0) - 1.0) > 1e-6
            or not actor.get('fullbright')):
        raise ValueError('Fx_WaterSplash actor definition changed')

    frames = parse_anim_full(Z, names, actor['anim'])
    if (len(frames) != KEX_WATER_SPLASH_LIFETIME
            or any(f.get('delay') != 1 for f in frames)
            or any(f.get('destroy') for f in frames[:-1])
            or not frames[-1].get('destroy')
            or any(not f.get('sprites') for f in frames)
            or any(f.get('local_sound') for f in frames)):
        raise ValueError('effects/splash timing/callback contract changed')

    rows = _build_effect_frame_tiles(
        Z, names, outdir, frames, 'kexart/watersplash', 'splash',
        KEX_WATER_SPLASH_TILE_BASE, (255, 255, 255), 'effects/splash')
    count = KEX_WATER_SPLASH_LIFETIME
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0], seq_size=[count], seq_flag=[-1],
               fr_base=list(range(count)), fr_size=[1] * count,
               fr_flag=[4] * count,
               ck_x=[0] * count, ck_y=[0] * count,
               ck_pict=[KEX_WATER_SPLASH_TILE_BASE + i
                        for i in range(count)], ck_flag=[0] * count,
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXWATERSPLASH.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  water splash: exact 23-tick Fx_WaterSplash on tiles %d-%d' %
              (KEX_WATER_SPLASH_TILE_BASE,
               KEX_WATER_SPLASH_TILE_BASE + count - 1))
    return rows


def build_kex_flame_sequence(Z, outdir, verbose=True):
    """Write Proj_Flame's flight loop and impact sizzle at native cadence.

    Both animations inherit the actor class's default .5 speed: at KEX's
    60 Hz that is one source frame per PCExhumed tick. The flight loop thus
    remains three frames, while the sizzle draws source frames 0..14, has one
    blank frame, then destroys itself on source frame 16.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    flight = parse_anim_full(Z, names, 'projectiles/flameproj_loop')
    sizzle = parse_anim_full(Z, names, 'projectiles/flameproj_sizzle')
    if (len(flight) != KEX_FLAME_FLIGHT_FRAMES
            or any(f.get('delay') != 1 or not f.get('sprites')
                   for f in flight)
            or len(sizzle) != 17
            or any(f.get('delay') != 1 for f in sizzle)
            or any(not f.get('sprites')
                   for f in sizzle[:KEX_FLAME_SIZZLE_VISIBLE_FRAMES])
            or sizzle[KEX_FLAME_SIZZLE_VISIBLE_FRAMES].get('sprites')
            or not sizzle[-1].get('destroy')):
        raise ValueError('Proj_Flame animation contract changed')

    sizzle = sizzle[:KEX_FLAME_SIZZLE_VISIBLE_FRAMES]
    flight_rows = _build_effect_frame_tiles(
        Z, names, outdir, flight, 'kexart/flameflight', 'flame',
        KEX_FLAME_TILE_BASE, (255, 255, 255), 'projectiles/flameproj_loop')
    sizzle_rows = _build_effect_frame_tiles(
        Z, names, outdir, sizzle, 'kexart/flamesizzle', 'flame',
        KEX_FLAME_TILE_BASE + KEX_FLAME_FLIGHT_FRAMES, (255, 255, 255),
        'projectiles/flameproj_sizzle')

    tile_count = KEX_FLAME_FLIGHT_FRAMES + KEX_FLAME_SIZZLE_VISIBLE_FRAMES
    frame_count = KEX_FLAME_FLIGHT_FRAMES + KEX_FLAME_SIZZLE_LIFETIME
    frame_sizes = [1] * tile_count + [0]
    frame_bases = list(range(tile_count)) + [tile_count]
    seq = dict(tag=b'IH', centerx=0, centery=0,
               seq_base=[0, KEX_FLAME_FLIGHT_FRAMES],
               seq_size=[KEX_FLAME_FLIGHT_FRAMES,
                         KEX_FLAME_SIZZLE_LIFETIME],
               seq_flag=[-1, -1], fr_base=frame_bases,
               fr_size=frame_sizes, fr_flag=[4] * frame_count,
               ck_x=[0] * tile_count, ck_y=[0] * tile_count,
               ck_pict=[KEX_FLAME_TILE_BASE + i for i in range(tile_count)],
               ck_flag=[0] * tile_count, snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXFLAME.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  flamethrower: exact 3-frame Proj_Flame loop plus '
              '15-art/1-blank impact sizzle on tiles %d-%d' %
              (KEX_FLAME_TILE_BASE, KEX_FLAME_TILE_BASE + tile_count - 1))
    return flight_rows + sizzle_rows


def build_kex_bird_feather_sequence(Z, outdir, verbose=True):
    """Write Obj_Bird_Feathers' randomized, looping 15-frame actor SEQ.

    All frames use source sprite 32, already assigned to tile 7632 by the
    generated HAWK art. The changing chunk centres retain its authored crop
    offsets without duplicating that one feather tile fifteen times.
    """
    import kexseqtool

    names = {n.lower(): n for n in Z.namelist()}
    feather = next((d for d in load_kex_actors(Z).values()
                    if d['name'] == 'Obj_Bird_Feathers'), None)
    if (feather is None or feather.get('anim') != 'monsters/bird_feather'
            or abs(feather.get('scale', 0) - 1.0) > 1e-6
            or feather.get('radius') != 28 or feather.get('height') != 56
            or not feather.get('randomize')):
        raise ValueError('Obj_Bird_Feathers actor definition changed')

    frames = parse_anim_full(Z, names, feather['anim'])
    if (len(frames) != KEX_BIRD_FEATHER_LIFETIME
            or any(frame.get('delay') != 1 for frame in frames)
            or any('rot' in frame or len(frame.get('sprites', ())) != 1
                   for frame in frames)):
        raise ValueError('bird_feather timing/layout changed')

    sheet = _sprite_table(Z, names, 'monsters', 'bird')
    chunk_x, chunk_y = [], []
    for frame in frames:
        group, base, index, ox, oy, flip = frame['sprites'][0]
        rect = sheet.get(index)
        if ((group, base, index, flip) != ('monsters', 'bird', 32, 0)
                or rect is None):
            raise ValueError('bird_feather no longer uses unflipped sprite 32')
        chunk_x.append(160 + int((ox + ox + rect[2]) / 2))
        chunk_y.append(100 + int((oy + oy + rect[3]) / 2))

    source = Z.read(names['sprites/monsters/bird_anim.txt']).decode('latin-1').replace('\r', '')
    match = re.search(r'^bird_feather\s*$', source, re.M)
    body, _end = _brace_block(source, match.end()) if match else ('', -1)
    actions = []
    frame_bodies = re.split(r'^\s*frame\s*$', body, flags=re.M)[1:]
    for frame_index, frame_body in enumerate(frame_bodies):
        if 'A_ThrustZVelocity(-0.5)' in frame_body and 'A_FeatherDrop()' in frame_body:
            actions.append(frame_index)
    if actions != [8, 14]:
        raise ValueError('bird_feather f8/f14 fall/drop callbacks changed')

    seq = dict(tag=b'IH', centerx=160, centery=100,
               seq_base=[0], seq_size=[KEX_BIRD_FEATHER_LIFETIME], seq_flag=[-1],
               fr_base=list(range(KEX_BIRD_FEATHER_LIFETIME)),
               fr_size=[1] * KEX_BIRD_FEATHER_LIFETIME,
               fr_flag=[0] * KEX_BIRD_FEATHER_LIFETIME,
               ck_x=chunk_x, ck_y=chunk_y,
               ck_pict=[7632] * KEX_BIRD_FEATHER_LIFETIME,
               ck_flag=[0] * KEX_BIRD_FEATHER_LIFETIME,
               snd_names=[], snd_events=[], trailing=b'')
    with open(os.path.join(outdir, 'KEXBIRDFEATHER.SEQ'), 'wb') as f:
        f.write(kexseqtool.write_seq_bytes(seq))
    if verbose:
        print('  bird death: exact randomized 15-frame feather loop on tile 7632')


def build_sprites(m, sects, walls, ranges, remap, decor=None):
    """Kex actors -> Build sprites carrying the lotag Build PowerSlave expects."""
    out = []
    trap_anchors = {}
    for actor_index, a in enumerate(m.actors):
        # ReadActors() never calls SpawnMapActor for sector -1. This applies
        # to scenery as well as gameplay actors and spawners: off-map editor
        # records must not be recovered merely because their coordinates lie
        # near converted geometry.
        if a.sector < 0:
            continue
        trap = KEX_TRAP_ACTORS.get(a.type)
        if trap is not None:
            origin = None if trap == KEX_TRAP_LOTAG_BEAM \
                else _spawner_origin(m, a)
            if origin is None:
                x, y, z = a.x, -a.y, -a.z * SCALE * WORLD
            else:
                x, y, z = origin[0], -origin[1], -origin[2] * SCALE * WORLD
            ang = _face_angle(m, a.p2)
            if ang is None:
                ang = build_angle(a.ang)
            normal_z = m.faces[a.p2].n[2] \
                if trap != KEX_TRAP_LOTAG_BEAM and 0 <= a.p2 < m.nface else 0
            # Many emitters are authored just beyond their sector's wall. The
            # record still assigns them to the firing room; walk the Build
            # origin into that authored sector along the face normal.
            si = actor_sector(m, sects, walls, ranges, a, x, y, z)
            inside = (si >= 0 and find_sector(sects, walls, ranges, x, y, z,
                                              (si,)) == si)
            if si < 0 or si not in remap or not inside:
                found = None
                base_x, base_y = x, y
                ca = math.cos(ang * math.pi / 1024.0)
                sa = math.sin(ang * math.pi / 1024.0)
                for step in range(1, 17):
                    for sign in (1, -1):
                        nx = base_x + ca * 8 * step * sign
                        ny = base_y + sa * 8 * step * sign
                        ns = actor_sector(m, sects, walls, ranges, a, nx, ny, z)
                        if (ns >= 0 and ns in remap and
                                find_sector(sects, walls, ranges, nx, ny, z,
                                            (ns,)) == ns):
                            found = (nx, ny, ns, sign)
                            break
                    if found is not None:
                        break
                if found is not None:
                    x, y, si, sign = found
                    # Face winding is not uniform. The normal that enters the
                    # actor's authored sector is also its firing direction.
                    if sign < 0:
                        ang = (ang + 1024) & 2047
            if si < 0 or si not in remap:
                continue
            # The tag is the channel. A beam with -1 is always on (hitag 1000
            # says so to the engine); another trap with -1 gets a reserved
            # channel nothing fires. Signed-negative tags below -1 are real
            # and must remain connected to an identically tagged trigger.
            tagkey = KEX_TAG0 if a.tag == 0 else a.tag
            if trap == KEX_TRAP_LOTAG_BEAM:
                hitag = 1000 if a.tag == -1 else _chan(m, tagkey)
            else:
                hitag = _chan(m, KEX_DEAD_TRAP if a.tag == -1 else tagkey)
            row = dict(x=int(round(x * WORLD)), y=int(round(y * WORLD)), z=int(z),
                       sectnum=remap[si], ang=ang,
                       lotag=trap, hitag=hitag, picnum=1, shade=0,
                       xrepeat=64, yrepeat=64, cstat=0,
                       zvel=max(-16384, min(16384,
                           int(round(normal_z * 16384.0)))),
                       extra=-(actor_index + 2))
            if trap != KEX_TRAP_LOTAG_BEAM:
                # BuildTrap retains these on its hidden source sprite. xvel
                # restores global actor ordering; the negative yvel is both an
                # explicit KEX-factory marker and the unsplit source sector.
                row['xvel'] = actor_index
                row['yvel'] = -(a.sector + 1)
                trap_anchors.setdefault(a.sector, row)
            out.append(row)
            continue
        ent = ACTOR_MAP.get(a.type)
        dec = (decor or {}).get(a.type)
        if ent is None and dec is None:
            continue
        x, y = a.x, -a.y
        si = actor_sector(m, sects, walls, ranges, a, x, y,
                          -a.z * SCALE * WORLD)
        if si < 0 and ent is None:
            # Scenery authored a step inside its wall -- a torch, a pot on a
            # ledge -- is walked into the room along its facing. Gameplay
            # actors are not: Kex itself rejects actors with sector -1, such
            # as KILENTRY's stacked off-map editor records.
            ang = build_angle(a.ang) * math.pi / 1024.0
            for step in range(1, 7):
                x, y = a.x + math.cos(ang) * 8 * step, -a.y + math.sin(ang) * 8 * step
                si = actor_sector(m, sects, walls, ranges, a, x, y,
                                  -a.z * SCALE * WORLD)
                if si >= 0:
                    break
        if si < 0 or si not in remap:
            continue                       # actor sits in a sector we dropped
        if ent is not None:
            # Gameplay actors keep Build PowerSlave's own lotag spawning, so its
            # AI and its art take over. Enemies overwrite picnum themselves, but
            # an item whose nItemAnimInfo entry has no anim keeps the picnum the
            # map gave it -- so it has to be the real one, or the item draws as
            # whatever tile 1 happens to be.
            lotag, hitag = ent
            # kexSlaveCheckpoint stores the source actor ID and uses that ID
            # to reactivate every checkpoint already touched after a reload.
            # Preserve the original actor-array index instead of collapsing
            # every Obj_Checkpoint to hitag zero.
            if lotag == 59:
                hitag = actor_index
            pic = ITEM_PICNUM.get(lotag)
            if lotag == 60 and hitag in (1, 24, 63) and dec is not None:
                pic = dec[0]
            if pic is None and dec is not None:
                pic = dec[0]        # no original tile: the remaster's own sprite
            # SpawnMapActor() gives every KEX map actor its authored Z. Keep
            # that invariant through the generated Build sprite: controllers
            # such as Anubis/Lion/Set deliberately replace it with the floor,
            # while Mummy, Spider, Bird, Wasp and Fish retain it. Flattening
            # before ProcessSpriteTag() ran silently destroyed the remaster's
            # authored vertical placement for that latter group.
            z = int(round(-a.z * SCALE * WORLD))
            out.append(dict(x=int(round(x * WORLD)), y=int(round(y * WORLD)), z=z,
                            sectnum=remap[si], ang=build_angle(a.ang),
                            lotag=lotag, hitag=hitag, picnum=(pic or 1), shade=0,
                            xrepeat=64, yrepeat=64, extra=-(actor_index + 2)))
        else:
            tile, scale, fullbright = dec
            # A Build sprite's world height is (tileheight * yrepeat) / 4, and
            # Kex authors at one texel per unit, so the repeat depends only on
            # the actor's scale and the world factor -- not on the tile size.
            rep = max(1, min(255, int(round(4.0 * scale * WORLD))))
            out.append(dict(x=int(round(x * WORLD)), y=int(round(y * WORLD)),
                            z=int(round(-a.z * SCALE * WORLD)),
                            sectnum=remap[si], ang=build_angle(a.ang),
                            lotag=0, hitag=0, picnum=tile,
                            shade=(-96 if fullbright else KEX_SHADE_MIN),
                            xrepeat=rep, yrepeat=rep, extra=-(actor_index + 2)))

    # Retail's stop branch is intentionally broader than the start branch:
    # every map actor can remove a factory in its own source sector. These
    # construction-only records retain that branch and its actor-array order.
    for actor_index, a in enumerate(m.actors):
        anchor = trap_anchors.get(a.sector)
        stop_tag = a.tag + 1
        if anchor is None or stop_tag == -1:
            continue
        stop_key = KEX_TAG0 if stop_tag == 0 else stop_tag
        out.append(dict(x=anchor['x'], y=anchor['y'], z=anchor['z'],
                        sectnum=anchor['sectnum'], ang=0,
                        lotag=KEX_TRAP_LOTAG_STOP, hitag=_chan(m, stop_key),
                        picnum=1, shade=0, xrepeat=1, yrepeat=1, cstat=0,
                        xvel=actor_index, yvel=-(a.sector + 1), extra=0))

    # REMOTE_TRIGGER is source-side dispatch, not a generic Build touchplate.
    # The marker stores the exact raw signed tag in xvel, the mapped channel in
    # hitag and its one-shot condition in yvel. Its position is irrelevant;
    # assignment to the final Build sector is the complete trigger region.
    source_for = {new: old for old, new in remap.items()}
    for _map, si, rawtag, chan, one_shot in _remote_trigger_rows(m, remap, ''):
        old = source_for[si]
        start, num = ranges[old]
        cx = sum(walls[start + i]['x'] for i in range(num)) / max(1, num)
        cy = sum(walls[start + i]['y'] for i in range(num)) / max(1, num)
        out.append(dict(x=int(cx), y=int(cy), z=sects[old]['floorz'],
                        sectnum=si, ang=0, lotag=87, hitag=chan, picnum=1,
                        shade=0, xrepeat=1, yrepeat=1, cstat=0,
                        xvel=rawtag, yvel=one_shot))

    for e in getattr(m, 'kex_extra_sprites', ()):
        si = find_sector(sects, walls, ranges, e['x'], e['y'])
        if si < 0 or si not in remap:
            continue
        out.append(dict(x=e['x'] * WORLD, y=e['y'] * WORLD, z=e['z'],
                        sectnum=remap[si], ang=e.get('ang', 0),
                        cstat=e.get('cstat', 0),
                        lotag=e['lotag'], hitag=e['hitag'], picnum=1, shade=0,
                        xrepeat=64, yrepeat=64))

    # Water pairing and damage floors, recorded by _flat_sprites(). A record's
    # partner is a SOURCE sector index; its first piece always keeps the source
    # index, so the remap of that index is the Build sector the hitag names.
    for entry in getattr(m, 'kex_flat_sprites', ()):
        si, lotag, arg = entry[0], entry[1], entry[2]
        z_at = entry[3] if len(entry) > 3 else None   # a marker that names a height of its own
        xvel = entry[4] if len(entry) > 4 else 0
        x_at = entry[5] if len(entry) > 5 else None   # a marker placed on a specific wall
        y_at = entry[6] if len(entry) > 6 else None
        yvel = entry[7] if len(entry) > 7 else 0       # a marker's Kex event class
        if si not in remap:
            continue
        pic = 1
        if lotag in (98, 99):
            if arg not in remap:
                continue
            hitag = remap[arg]
        else:
            hitag = arg
        if lotag == 64:
            partner = xvel - 1
            xvel = remap[partner] + 1 if partner in remap else 0
        if lotag == KEX_INV_LOTAG_ARTIFACT:
            t = KEX_ARTI_TYPE.get(arg & 15)
            if decor and t in decor:
                pic = decor[t][0]
        elif lotag == 86:
            if arg == 1:
                pic = ITEM_PICNUM[23]       # Sobek Mask is Build's own item
            else:
                t = KEX_ARTI_TYPE.get(arg)
                if decor and t in decor:
                    pic = decor[t][0]
        elif lotag == 23:
            pic = ITEM_PICNUM[23]
        start, num = ranges[si]
        cx = sum(walls[start+i]['x'] for i in range(num)) / max(1, num)
        cy = sum(walls[start+i]['y'] for i in range(num)) / max(1, num)
        out.append(dict(x=int(cx if x_at is None else x_at),
                        y=int(cy if y_at is None else y_at),
                        z=(sects[si]['floorz'] if z_at is None else z_at),
                        sectnum=remap[si], ang=0,
                        lotag=lotag, hitag=hitag, picnum=pic, shade=0,
                        xrepeat=64, yrepeat=64, xvel=xvel, yvel=yvel))
    return out


def sector_remap(sects, ranges):
    """Sectors that survived, and old index -> new index for the ones that did."""
    keep = [i for i,s in enumerate(sects) if s is not None and ranges[i][1] >= 3]
    return keep, {old:new for new,old in enumerate(keep)}


def _kex_portal_entries(mapkey, sects, walls, ranges):
    """Runtime rows and their source-face metadata.

    Ordinary one-neighbour portals stay native Build links. Only an edge with
    two or more vertical face bands, or whose sole authored link could not be
    made the static reciprocal Build pair, needs the height-selected runtime
    layer. Indices are remapped in exactly write_build()'s output order. The
    metadata is retained for kexverify.py; only the integer row is serialized.
    """
    keep, remap = sector_remap(sects, ranges)
    wremap = {}
    for old in keep:
        st, num = ranges[old]
        for i in range(num):
            wremap[st + i] = len(wremap)

    entries = []
    for old in keep:
        st, num = ranges[old]
        for i in range(num):
            wi = st + i
            w = walls[wi]
            alts = [a for a in w.get('_kex_portal_alts', ())
                    if a['nextsector'] in remap and a['nextwall'] in wremap]
            if not alts:
                continue
            default_ok = any(a['nextsector'] == w['nextsector']
                             and a['nextwall'] == w['nextwall'] for a in alts)
            if len(alts) == 1 and default_ok:
                continue
            for a in alts:
                top0, bot0, top1, bot1 = a['bounds']
                row = (mapkey, wremap[wi], wremap[a['nextwall']],
                       remap[a['nextsector']], top0, bot0, top1, bot1,
                       a['flags'], a.get('picnum', 0))
                entries.append((row, a))
    return entries


def kex_portal_rows(mapkey, sects, walls, ranges):
    """Serializable height-selected portal rows; see _kex_portal_entries()."""
    return [row for row, _metadata in _kex_portal_entries(mapkey, sects, walls, ranges)]


# ---------------------------------------------------------------------------
# Authored destructible faces
#
# Kex face flag 0x100 is called Toggle by the retail editor.  It is active
# gameplay: A_RadialBlast's third argument gates a face search, then the engine
# clears Solid|Toggle, sets Portal and rebuilds both linked sectors.  The face
# already links to the room behind it, so this is an authored bomb wall -- not
# procedural damage art and not a new overlapping room.
#
# One source face can become several Build walls after lighting/slope splits,
# and one Build wall can carry several vertical Kex faces.  kextoggle.txt keeps
# the source-face identity across both transformations.  F records describe a
# logical face and its exact recursive retail closure; W records identify each
# horizontal representation; S records identify the two sides of a vertical
# seam, which the runtime turns into a YAX bunch only after it opens.
KEX_TOGGLE_MAX_FACES = 256
KEX_TOGGLE_WORDS = KEX_TOGGLE_MAX_FACES // 32


def _kex_face_layout(m):
    owner = {}
    kind = {}
    bysector = {}
    for si, s in enumerate(m.sectors):
        faces = list(range(s.faceStart, min(s.wallEnd + 3, m.nface)))
        bysector[si] = faces
        for fi in faces:
            owner[fi] = si
            kind[fi] = 0 if fi <= s.wallEnd else (1 if fi == s.wallEnd + 1 else 2)
    return owner, kind, bysector


def _kex_face_bbox(m, fi):
    f = m.faces[fi]
    v = m.verts[f.vertStart:min(f.vertStart + 4, len(m.verts))]
    return (min(q[0] for q in v), min(q[1] for q in v), min(q[2] for q in v),
            max(q[0] for q in v), max(q[1] for q in v), max(q[2] for q in v))


def _kex_bbox_overlap(a, b, dimensions=3):
    return all(a[d] <= b[d + 3] and b[d] <= a[d + 3]
               for d in range(dimensions))


def _kex_toggle_faces(m):
    """Logical Toggle faces and the faces each one opens, by local index.

    This mirrors retail kexSlaveMap::OpenToggleFace (0x1401b8800 in the GOG
    binary): overlapping XY faces in the owner sector, every Toggle face in
    the directly linked sector, then Toggle faces one link farther that
    overlap either that intervening face or the original face.  Recursion
    continues until no Toggle bit remains, so each row stores the transitive
    closure and the runtime can perform the same one-shot operation cheaply.
    """
    owner, kind, bysector = _kex_face_layout(m)
    faces = sorted(fi for fi in owner if m.faces[fi].flags & KEX_FACE_TOGGLE)
    if len(faces) > KEX_TOGGLE_MAX_FACES:
        raise ValueError("%d Toggle faces exceed runtime limit %d"
                         % (len(faces), KEX_TOGGLE_MAX_FACES))
    local = {fi: i for i, fi in enumerate(faces)}
    bbox = {fi: _kex_face_bbox(m, fi) for fi in owner}
    direct = {fi: set() for fi in faces}
    for fi in faces:
        f = m.faces[fi]
        if f.link < 0:
            continue
        # First pass in the retail routine: other Toggle faces in the source
        # sector whose projected rectangles touch this one.
        for fj in bysector[owner[fi]]:
            if (fj != fi and fj in local
                    and _kex_bbox_overlap(bbox[fi], bbox[fj], 2)):
                direct[fi].add(fj)

        # Every Toggle face in the directly linked sector is opened.
        for middle in bysector.get(f.link, ()):
            if middle in local:
                direct[fi].add(middle)

            # The retail routine also looks through every linked face there.
            # A Toggle face on the far side opens when it overlaps either that
            # intervening face or the original selected face in 3D.
            ml = m.faces[middle].link
            if ml < 0:
                continue
            for far in bysector.get(ml, ()):
                if (far in local
                        and (_kex_bbox_overlap(bbox[far], bbox[middle])
                             or _kex_bbox_overlap(bbox[far], bbox[fi]))):
                    direct[fi].add(far)

    closures = {}
    for fi in faces:
        seen = {fi}; pending = [fi]
        while pending:
            cur = pending.pop()
            for nxt in direct[cur]:
                if nxt not in seen:
                    seen.add(nxt); pending.append(nxt)
        closures[fi] = seen
    return faces, local, owner, kind, bbox, closures


def kex_toggle_rows(m, mapkey, sects, walls, ranges):
    """Rows for kextoggle.txt, in write_build()'s final index space."""
    faces, local, owner, kind, bbox, closures = _kex_toggle_faces(m)
    if not faces:
        return []

    keep, remap = sector_remap(sects, ranges)
    wremap = {}
    for old in keep:
        st, num = ranges[old]
        for i in range(num):
            wremap[st + i] = len(wremap)

    rows = []
    for fi in faces:
        b = bbox[fi]
        cx = int(round((b[0] + b[3]) * 0.5 * WORLD))
        cy = int(round(-(b[1] + b[4]) * 0.5 * WORLD))
        cz = int(round(-(b[2] + b[5]) * 0.5 * SCALE * WORLD))
        words = [0] * KEX_TOGGLE_WORDS
        for opened in closures[fi]:
            q = local[opened]
            words[q >> 5] |= 1 << (q & 31)
        rows.append((mapkey, 'F', local[fi], kind[fi], cx, cy, cz)
                    + tuple('0x%08x' % w for w in words))

    # A wall is dynamic exactly when kexportal.txt owns it.  Its base Build
    # state may represent another vertical band, so only the selected runtime
    # row is hidden after opening; ordinary one-neighbour walls can be changed
    # permanently in place.
    dynamic = {row[1] for row, _metadata
               in _kex_portal_entries(mapkey, sects, walls, ranges)}
    wallrows = set()
    for old in keep:
        st, num = ranges[old]
        for i in range(num):
            wi = st + i
            outwall = wremap[wi]
            for alt in walls[wi].get('_kex_portal_alts', ()):
                fi = alt.get('face', -1)
                if fi not in local or kind[fi] != 0:
                    continue
                if alt['nextsector'] not in remap or alt['nextwall'] not in wremap:
                    continue
                top0, bot0, top1, bot1 = alt['bounds']
                reverse = alt['nextwall']
                clearreverse = int(walls[reverse].get('_kex_copied_overpic_from') == wi)
                wallrows.add((mapkey, 'W', local[fi], outwall,
                              wremap[reverse], remap[alt['nextsector']],
                              top0, bot0, top1, bot1,
                              int(outwall in dynamic), clearreverse))
    rows.extend(sorted(wallrows, key=lambda r: tuple(int(v) for v in r[2:])))

    # Flat Toggle faces come in floor/ceiling pairs on one coincident plane.
    # Give each pair a seam id.  Every split descendant of both source sectors
    # joins that seam when either face's retail closure opens it.
    flat = [fi for fi in faces if kind[fi] in (1, 2)]
    seam_for = {}
    seam_members = []
    for fi in flat:
        if fi in seam_for:
            continue
        target = m.faces[fi].link
        reverse = next((fj for fj in flat
                        if owner[fj] == target and m.faces[fj].link == owner[fi]
                        and kind[fj] == (3 - kind[fi])
                        and _kex_bbox_overlap(bbox[fi], bbox[fj])), None)
        members = [fi] + ([reverse] if reverse is not None else [])
        seam = len(seam_members)
        seam_members.append(members)
        for fj in members:
            seam_for[fj] = seam

    pieces = getattr(m, 'kex_sector_pieces', {})
    seamrows = set()
    for seam, members in enumerate(seam_members):
        for fi in members:
            cf = 0 if kind[fi] == 1 else 1       # YAX_CEILING / YAX_FLOOR
            for piece in pieces.get(owner[fi], [owner[fi]]):
                if piece in remap:
                    seamrows.add((mapkey, 'S', local[fi], seam, remap[piece], cf))

        # Be robust to a custom one-sided Toggle seam: its linked sector still
        # has to become the opposite half of the bunch even without a Toggle
        # record of its own.
        if len(members) == 1:
            fi = members[0]
            target = m.faces[fi].link
            cf = 1 if kind[fi] == 1 else 0
            for piece in pieces.get(target, [target]):
                if piece in remap:
                    seamrows.add((mapkey, 'S', local[fi], seam, remap[piece], cf))
    rows.extend(sorted(seamrows, key=lambda r: tuple(int(v) for v in r[2:])))
    return rows


# ---------------------------------------------------------------------------
# True Room over Room
#
# Kex stacks rooms and marks the join the same way it marks a doorway: the
# ceiling or floor FACE carries a link to the sector on the other side, exactly
# as a wall face does. There are 300 such links across the 25 retail levels,
# 299 of them symmetric and 299 with the upper sector's floor at precisely the
# lower sector's ceiling.
#
# They are NOT openings, though, and that distinction is what matters. Every one
# of those 300 linked floor faces is textured, and so are 299 of the 300 linked
# ceilings -- a wall face that is a portal carries no tiles, and these do. So a
# vertical link means "the sector below me is that one", not "you can see or
# fall through here", and both surfaces stay solid.
#
# Converting them to bunches anyway is actively wrong. A bunched floor is not
# drawn: the engine draws the sectors on the other side of the seam instead. In
# 220 of the 291 seams the upper sector's floor and the lower sector's ceiling
# disagree -- usually in shade, by 8 to 16 steps -- so those floors came out
# lit like the room underneath. Measured at 3.5% of the frame changing inside a
# bunched sector, for no gain anywhere, since there is nothing to see through.
#
# So a seam only becomes a bunch when the face is genuinely untextured, which is
# the same test a wall portal passes. Exactly one face in the retail maps meets
# it, so in practice every level converts as plain version 7. The machinery
# stays because it is correct, and a map with a real vertical opening would use
# it.
#
# Build's TROR ("YAX") expresses this with *bunches*. A bunch is one horizontal
# seam: every sector whose floor lies on it, and every sector whose ceiling
# lies on it, share a bunch number, and the renderer draws through the seam
# instead of capping it. eduke32 stores that in the map without changing the
# struct layout -- the version int is 9 instead of 7, the bunch number rides in
# ceilingxpanning / floorxpanning with YAX_BIT set in the matching stat, and a
# seam wall's partner index rides in lotag (the sector above) or extra (the
# sector below) with bit 1<<10 or 1<<11 set in cstat. yax_update() lifts all of
# that into its own arrays at load and puts the hijacked fields back, which is
# why the panning and tag fields stay usable afterwards.
YAX_BIT          = 1024
YAX_NEXTWALL_CEIL  = 1 << 10     # this wall has a partner in the sector above
YAX_NEXTWALL_FLOOR = 1 << 11     # ... and in the sector below
YAX_MAXBUNCHES   = 256
YAX_CEILING, YAX_FLOOR = 0, 1


def build_tror(sects, ranges, keep, remap, wremap, out_walls, verbose=False):
    """Assign TROR bunches and seam wall links. Returns the number of bunches.

    Mutates out_walls (cstat/lotag/extra) and returns {(build_sector, cf):
    bunch} for the caller to pack into the panning fields.
    """
    # Collect the seams: an upper sector's floor meeting a lower sector's
    # ceiling. The link is stored in Kex sector indices, so it has to survive
    # the drop-and-renumber pass first.
    seams = []
    for old in keep:
        s = sects[old]
        below = s.get('yax_floor', -1)
        if below is None or below < 0 or below not in remap:
            continue
        lo = sects[below]
        # A seam is only a seam if the two planes actually coincide; one link
        # in the retail maps does not, and a bunch there would tear.
        if s['floorz'] != lo['ceilingz']:
            continue
        seams.append((remap[old], remap[below], old, below))

    if not seams:
        return {}, 0

    # Union-find over sides, so that several sectors meeting on one plane end
    # up in a single bunch rather than one bunch per pair.
    parent = {}
    def find(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb: parent[ra] = rb
    for up, dn, _o, _b in seams:
        union((up, YAX_FLOOR), (dn, YAX_CEILING))

    groups = {}
    for node in list(parent):
        groups.setdefault(find(node), []).append(node)

    bunch = {}
    n = 0
    for root, nodes in sorted(groups.items()):
        if n >= YAX_MAXBUNCHES:
            break
        # Every extended ceiling must have a matching floor, or yax_update's
        # bunch bookkeeping counts a bunch that has no other side.
        if not any(cf == YAX_FLOOR for _s, cf in nodes): continue
        if not any(cf == YAX_CEILING for _s, cf in nodes): continue
        for node in nodes:
            bunch[node] = n
        n += 1

    # Seam wall links.  Match the complete directed edge, not only its first
    # vertex: split-sector rings can contain two different walls beginning at
    # the same coordinate.  A non-unique or absent edge stays unlinked, which
    # the engine tolerates and is safer than joining unrelated spaces.
    for up, dn, oldup, olddn in seams:
        if (up, YAX_FLOOR) not in bunch:
            continue
        ust, unum = ranges[oldup]; dst, dnum = ranges[olddn]
        below_edge = {}
        for i in range(dnum):
            wi = wremap[dst + i]
            w = out_walls[wi]
            p = out_walls[w['point2']]
            key = (w['x'], w['y'], p['x'], p['y'])
            below_edge[key] = None if key in below_edge else wi
        for i in range(unum):
            wi = wremap[ust + i]
            w = out_walls[wi]
            p = out_walls[w['point2']]
            wj = below_edge.get((w['x'], w['y'], p['x'], p['y']))
            if wj is None:
                continue
            w['cstat'] |= YAX_NEXTWALL_FLOOR
            w['extra'] = wj
            out_walls[wj]['cstat'] |= YAX_NEXTWALL_CEIL
            out_walls[wj]['lotag'] = wi

    if verbose and n:
        print("  TROR: %d vertical openings -> %d bunches" % (len(seams), n))
    return bunch, n


def write_build(path, sects, walls, ranges, start, sprites=()):
    sx, sy, sz, ss, sang = start
    # The map's own facing when it has one; otherwise aim at the way out.
    if sang is None:
        sang = start_angle(walls, ranges, ss, sx, sy) if ss < len(ranges) else 0
    keep, remap = sector_remap(sects, ranges)
    wremap = {}; out_walls = []
    for old in keep:
        st,num = ranges[old]
        for i in range(num):
            wremap[st+i] = len(out_walls); out_walls.append(dict(walls[st+i]))
    for w in out_walls:
        w['point2'] = wremap[w['point2']]
        w.setdefault('lotag', 0)
        w.setdefault('extra', -1)
        if w['nextsector'] in remap and w['nextwall'] in wremap:
            w['nextwall'] = wremap[w['nextwall']]; w['nextsector'] = remap[w['nextsector']]
        else:
            w['nextwall'] = -1; w['nextsector'] = -1

    bunch, nbunches = build_tror(sects, ranges, keep, remap, wremap, out_walls)

    # Preserve Kex's directed actor-only portal rules through the MAP load.
    # YAX also owns wall.extra on a locked floor seam; fail conversion instead
    # of silently corrupting either meaning if a custom map combines them.
    clip_extra = {
        KEX_WALL_CLIP_AI_FALLERS: KEX_WALL_EXTRA_BLOCK_AI_FALLERS,
        KEX_WALL_CLIP_NONPLAYERS: KEX_WALL_EXTRA_BLOCK_NONPLAYERS,
        KEX_WALL_CLIP_AI_FALLERS | KEX_WALL_CLIP_NONPLAYERS: KEX_WALL_EXTRA_BLOCK_BOTH,
    }
    for w in out_walls:
        marker = clip_extra.get(w.get('_kex_clip', 0))
        hazard = w.get('_kex_hazard')
        if marker is not None and hazard is not None:
            raise ValueError("Kex hazard wall also owns actor-only clip metadata")
        marker = marker if marker is not None else hazard
        if marker is None:
            continue
        if w['extra'] != -1:
            raise ValueError("Kex hazard/clip wall also owns a YAX floor link")
        w['extra'] = marker

    b = bytearray()
    # Map version 9 is version 7 with TROR: identical struct layout on disk,
    # but engineLoadBoard then runs yax_update() over it. Maps without a single
    # vertical opening stay at 7 so nothing about them changes.
    # Past the version-7 loader's 1024 sectors / 8192 walls the header says
    # 8 (9 with TROR): same layout on disk, engineLoadBoard() lifts the limits
    # to 4096 / 16384.
    b += struct.pack('<i', 9 if nbunches else (8 if len(keep) > 1024 or len(out_walls) > 8192 else 7))
    b += struct.pack('<iii', sx * WORLD, sy * WORLD, sz)   # x/y are world units; only z is 1/16
    b += struct.pack('<hh', sang, remap.get(ss,0))
    b += struct.pack('<H', len(keep))
    for old in keep:
        s = sects[old]; st,num = ranges[old]
        ns = remap[old]
        cb = bunch.get((ns, YAX_CEILING), -1)
        fb = bunch.get((ns, YAX_FLOOR), -1)
        cstat = s['ceilingstat'] | (YAX_BIT if cb >= 0 else 0)
        fstat = s['floorstat']   | (YAX_BIT if fb >= 0 else 0)
        # Kex maps a tile along its face; Build projects a floor texture onto
        # the horizontal plane, so a steep slope stretches it by 1/cos --
        # 2.2x on KILARENA's 63-degree machine panels, which read as noise.
        # Relative alignment (bit 64) maps along the slope from the first
        # wall, which the converter has already turned to the contour line.
        # Kept off gentle terrain, where world alignment tiles seamlessly
        # between the many small pieces and the stretch is under 1.4x.
        if abs(s['ceilingheinum']) >= STEEP_HEINUM and not cstat & 1: cstat |= 64
        if abs(s['floorheinum'])   >= STEEP_HEINUM and not fstat & 1: fstat |= 64
        b += struct.pack('<hhiihhhhbBBBhhbBBBBBhhh',
            wremap[st], num, s['ceilingz'], s['floorz'],
            cstat, fstat,
            s['ceilingpicnum'], s['ceilingheinum'], s['ceilingshade'], 0,
            max(cb, 0), 0,          # ceilingxpanning carries the ceiling bunch
            s['floorpicnum'], s['floorheinum'], s['floorshade'], 0,
            max(fb, 0), 0,          # floorxpanning carries the floor bunch
            0, 0,
            s['lotag'], s['hitag'], 0)
    b += struct.pack('<H', len(out_walls))
    for w in out_walls:
        b += struct.pack('<iihhhhhhbBBBBBhhh',
            w['x'] * WORLD, w['y'] * WORLD, w['point2'], w['nextwall'], w['nextsector'],
            w['cstat'], w['picnum'], w.get('overpicnum', 0), w['shade'], 0, w['xrepeat'], w['yrepeat'],
            w.get('xpanning', 0), w.get('ypanning', 0),
            w['lotag'], w.get('hitag', 0), w['extra'])   # a switch's channel rides in the wall's hitag
    b += struct.pack('<H', len(sprites))
    for s in sprites:
        b += struct.pack('<iiihhbBBBBBbbhhhhhhhhhh',
            s['x'], s['y'], s['z'], s.get('cstat', 0), s['picnum'], s['shade'], 0, 32, 0,
            s['xrepeat'], s['yrepeat'], 0, 0,
            s['sectnum'], 0, s['ang'], -1,
            s.get('xvel', 0), s.get('yvel', 0), s.get('zvel', 0),
            s['lotag'], s['hitag'], s.get('extra', 0))
    open(path,'wb').write(b)
    return len(keep), len(out_walls), len(sprites)

# ---------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------

def png_size(blob):
    # IHDR is always the first chunk: 8-byte signature, 4-byte length, "IHDR"
    if blob[:8] != b'\x89PNG\r\n\x1a\n': return None
    w, h = struct.unpack_from('>II', blob, 16)
    return w, h

def collect(kpf_path, names):
    """Return {name: (w,h)} for each texture, read straight out of the .kpf."""
    out = {}
    with zipfile.ZipFile(kpf_path) as z:
        have = {n.lower(): n for n in z.namelist()}
        for n in names:
            if not n: continue
            real = have.get(n.lower())
            if real is None: continue
            with z.open(real) as f:
                sz = png_size(f.read(32))
            if sz: out[n] = sz
    return out

DEF_HEADER = ("// VibeBuild32: textures from the Nightdive PowerSlave Exhumed remaster.\n"
              "// Generated by source/exhumed/tools/kexmap.py -- do not edit by hand.\n"
              "// Pal 30 is the hit flash: a creature that takes damage is drawn in it for\n"
              "// three ticks, fullbright; screen-blended white, its art is a white silhouette.\n"
              "tint { pal 30 red 255 green 255 blue 255 flags 64 }\n")

def write_def(path, assignments, animdefs=(), skytiles=(), flatdefs=(), header=DEF_HEADER):
    """One DEF entry per Kex texture. `assignments` is {tile: png}, or any
    sequence of (tile, png) already in the order it should be written.

    tilefromtexture gives the engine a real tile (it needs the dimensions to
    size a wall's texture repeat, and the software renderer needs pixels);
    texture points the hardware renderer at the same PNG so nothing is lost to
    a palette conversion. Both are needed for every tile, which is why the menu
    art comes through here too rather than emitting its own .def.
    """
    items = sorted(assignments.items()) if hasattr(assignments, 'items') else list(assignments)
    with open(path, 'w') as f:
        f.write(header + "\n")
        for tile, name in items:
            f.write('tilefromtexture %d { file "%s" }\n' % (tile, name))
            f.write('texture %d { pal 0 { file "%s" } }\n' % (tile, name))
        if flatdefs:
            f.write("// Flats: a tile sized for Build's fixed 16 units per texel, so the\n")
            f.write("// texture repeats every 128 Kex units as in the remaster; the hires\n")
            f.write("// replacement is the full-size image. See KEX_FLAT_TEXELS in kexmap.py.\n")
            for tile, small, name in flatdefs:
                f.write('tilefromtexture %d { file "%s" }\n' % (tile, small))
                f.write('texture %d { pal 0 { file "%s" } }\n' % (tile, name))
        if skytiles:
            f.write("\n// Parallax skies. Build will not treat a tile as a sky panorama\n")
            f.write("// until tileSetupSky() has seen it, which is what multipsky does --\n")
            f.write("// without this the ceiling drew black however the tile was prepared.\n")
            for st in sorted(skytiles):
                f.write('multipsky %d { horizfrac 65536 lognumtiles 0 tile 0 %d }\n' % (st, st))

        if animdefs:
            f.write("\n// Animated surfaces -- water, slime and the like. Build plays an\n")
            f.write("// animation as a run of consecutive tiles, which is why the frames\n")
            f.write("// are allocated directly after the texture they belong to.\n")
            # animtilerange <firsttile> <lasttile> <speed> <type>. The first
            # version passed the first tile twice, which pushed speed into the
            # type slot and made the engine reject every one of them. Type 2 is
            # a forward loop; speed is clamped 0..15, higher being slower.
            for first, last, speed in animdefs:
                f.write('animtilerange %d %d %d 2\n' % (first, last, max(0, min(15, speed))))


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------
KPF = "PowerslaveEX.kpf"

# Map textures and actor art are numbered upward from here. The menu tool owns
# 7000..7505 (kexmenu.py), and with the collectibles' sprites added the run
# from 6000 reached 6988 -- one level short of overwriting the menu font.
def convert_install(kexdir, outdir, tilebase=8000, verbose=True, demo_dir=None,
                    include_testmaps=False):
    kpf = os.path.join(kexdir, KPF)
    if not os.path.isfile(kpf):
        raise SystemExit("no %s in %s -- point me at the PowerSlave Exhumed install"
                         % (KPF, kexdir))
    os.makedirs(outdir, exist_ok=True)

    with zipfile.ZipFile(kpf) as z:
        names = [n for n in z.namelist()
                 if n.lower().startswith('maps/') and n.lower().endswith('.map')
                 and '/old/' not in n.lower()
                 and (include_testmaps or '/testmaps/' not in n.lower())]
        maps = {os.path.basename(n): z.read(n) for n in sorted(names)}
        anim_src = z.read([n for n in z.namelist()
                           if n.lower() == 'defs/animpicinfo.txt'][0]).decode('latin-1') \
                   if any(n.lower() == 'defs/animpicinfo.txt' for n in z.namelist()) else ''

    # Parse every level first, then assign tiles. Tile numbers cannot be handed
    # out as levels are read any more: Build animates a texture by playing a
    # RANGE of consecutive tiles, so an animated texture's frames have to sit
    # immediately after it, and that has to be known before any level bakes a
    # picnum.
    parsed = {}
    failed = []
    for name, blob in sorted(maps.items()):
        try:
            parsed[name] = PSM_from_bytes(blob)
        except Exception as e:
            failed.append((name, str(e)))

    # base texture -> (speed, [frame files]), from the remaster's own table
    anims = {}
    cur = None
    for line in anim_src.replace('\r', '').split('\n'):
        t = line.strip()
        m2 = re.match(r'^"([^"]+)"', t)
        if m2 and '{' not in t:
            cur = m2.group(1); anims[cur] = [5, []]; continue
        if cur is None: continue
        if t.startswith('speed'):
            try: anims[cur][0] = int(t.split()[1])
            except Exception: pass
        m3 = re.match(r'^texture_\d+\s+"([^"]+)"', t)
        if m3: anims[cur][1].append(m3.group(1))
        if t == '}': cur = None
    anims = {k: v for k, v in anims.items() if v[1]}

    # Every actor type that appears in any level, so the scenery art can be
    # sliced once and shared.
    with zipfile.ZipFile(kpf) as z:
        kexactors = load_kex_actors(z)
        for t, d in kexactors.items():
            if d['name'] in KEX_INV_ACTORS:
                ACTOR_MAP[t] = KEX_INV_ACTORS[d['name']]
        skip_types = {t for t, d in kexactors.items() if d['name'] in KEX_SKIP_ACTORS}
        for t, d in kexactors.items():
            if d['name'] in KEX_ARTI_NAMES:
                KEX_ARTI_TYPE[KEX_ARTI_NAMES.index(d['name'])] = t
        beam_type = next((t for t, d in kexactors.items() if d['name'] == KEX_BEAM_PROJECTILE), None)
        used_types = set()
        for m in parsed.values():
            used_types.update(a.type for a in m.actors)
        used_types -= skip_types
        used_types -= set(KEX_TRAP_ACTORS)
        if beam_type is not None:
            used_types.add(beam_type)          # the beams are drawn with its sprite
        # Item_Map is normally created by A_RandomVaseItem, not placed in a
        # level, so force its art into the generated texture set.
        map_type = next((t for t, d in kexactors.items() if d['name'] == 'Item_Map'), None)
        if map_type is not None:
            used_types.add(map_type)
        power_type = next((t for t, d in kexactors.items() if d['name'] == 'Item_Power'), None)
        if power_type is not None:
            used_types.add(power_type)
        ammo_type = next((t for t, d in kexactors.items() if d['name'] == 'Item_Ammo'), None)
        if ammo_type is not None:
            used_types.add(ammo_type)
        health_type = next((t for t, d in kexactors.items() if d['name'] == 'Item_Health'), None)
        if health_type is not None:
            used_types.add(health_type)
        ra_type = next((t for t, d in kexactors.items() if d['name'] == 'Proj_Ra_Fire'), None)
        if ra_type is not None:
            used_types.add(ra_type)
        cobra_type = next((t for t, d in kexactors.items() if d['name'] == 'Proj_Cobra_Green'), None)
        if cobra_type is not None:
            used_types.add(cobra_type)
        cobra_joint_type = next((t for t, d in kexactors.items() if d['name'] == 'Fx_GreenCobraJoint'), None)
        if cobra_joint_type is not None:
            used_types.add(cobra_joint_type)
        choppa_type = next((t for t, d in kexactors.items() if d['name'] == 'Obj_Choppa'), None)
        if choppa_type is not None:
            # Obj_Choppa uses the level-finish lotag, but its sprite is the
            # remaster's helicopter rather than Build's sarcophagus tile.
            used_types.add(choppa_type)
        ramses_body_type = next((t for t, d in kexactors.items() if d['name'] == 'Obj_Ramses_Body'), None)
        if ramses_body_type is not None:
            used_types.add(ramses_body_type)
        # Gameplay actors keep Build's own art -- except the few items for which
        # no original picnum could be recovered, which need a tile built from
        # the remaster's sprite to have anything to draw.
        used_types -= {t for t, (lo, h) in ACTOR_MAP.items()
                       if (lo in ITEM_PICNUM and not (lo == 60 and h in (1, 24, 63))) or lo >= 100}
        used_types.discard(AT_PLAYER)

    # Switches flip between two textures, and Build's wall switch flips between
    # picnum and picnum+1 (BuildWallFace in runlist_ProcessWallTag) -- so every
    # switchInfo ON texture must land on the tile right after its OFF, the same
    # way an animation's frames follow their base.
    with zipfile.ZipFile(kpf) as z:
        sw_src = ''
        for n in z.namelist():
            if n.lower() == 'defs/switchinfo.txt':
                sw_src = z.read(n).decode('latin-1'); break
    sw_pairs = dict(re.findall(r'"([^"]+)"\s*\{\s*on\s+"([^"]+)"', sw_src))
    sw_ons = set(sw_pairs.values())

    assign = {}; nexttile = tilebase
    animdefs = []
    wanted = []
    for name in sorted(parsed):
        m = parsed[name]
        # The PSM string table can carry unused or malformed entries (the
        # focused platforms map has one such trailing name). Only a texture
        # index referenced by a face tile can reach a Build wall/floor, so do
        # not assign an ART slot or emit a DEF entry for table padding.
        used_textures = {tex for _uvs, tex in m.tiles}
        wanted.extend(m.textures[tex] for tex in sorted(used_textures)
                      if m.textures[tex])
        if m.sky: wanted.append(m.sky)
    for t in wanted:
        if t in assign or t in sw_ons: continue   # an ON waits for its OFF
        assign[t] = nexttile; nexttile += 1
        on = sw_pairs.get(t)
        if on and on not in assign:
            assign[on] = nexttile; nexttile += 1
        if t in anims:
            speed, frames = anims[t]
            for f in frames:
                assign.setdefault(f, nexttile)   # frames get the tiles that follow
                nexttile += 1
            animdefs.append((assign[t], assign[t] + len(frames), speed))
    for t in wanted:
        if t not in assign:                       # an ON whose OFF never shows
            assign[t] = nexttile; nexttile += 1

    # The flats' own tiles (see KEX_FLAT_TEXELS), animated ones as runs too.
    flat_used = set()
    for m in parsed.values():
        for sec in m.sectors:
            for fi in (sec.wallEnd + 1, sec.wallEnd + 2):
                f = m.faces[min(fi, m.nface - 1)]
                if f.tileStart >= 0:
                    flat_used.add(m.textures[m.tiles[f.tileStart][1]])
    fassign = {}
    for t in sorted(flat_used):
        if t in fassign: continue
        fassign[t] = nexttile; nexttile += 1
        if t in anims:
            speed, frames = anims[t]
            for f in frames:
                fassign.setdefault(f, nexttile); nexttile += 1
            animdefs.append((fassign[t], fassign[t] + len(frames), speed))

    # Composite tiles for the mosaic wall faces (see _face_rows), one per
    # distinct pattern across the levels.
    fcomp_assign = {}; fcomp_used = set()
    for m in parsed.values():
        for sec in m.sectors:
            for fi in (sec.wallEnd + 1, sec.wallEnd + 2):
                f = m.faces[min(fi, m.nface - 1)]
                if f.link >= 0: continue
                key = _flat_cells(m, f)
                # a composite is still art: a face with an animated texture
                # (lava, water) keeps its plain tile so it keeps moving
                if key is not None and key not in fcomp_assign and not any(c[4] in anims for c in key[4]):
                    fcomp_assign[key] = nexttile; nexttile += 1
    comp_assign = {}; comp_used = set()
    for m in parsed.values():
        for sec in m.sectors:
            for fi in range(sec.faceStart, min(sec.wallEnd + 1, m.nface)):
                f = m.faces[fi]
                if f.link >= 0: continue
                key = _face_rows(m, f)
                if key is not None and key not in comp_assign and not any(t in anims for _o, _h, t in key[1]):
                    comp_assign[key] = nexttile; nexttile += 1

    with zipfile.ZipFile(kpf) as z:
        decor, decordefs, decoranims, breakables, lights = build_actor_tiles(
            z, outdir, used_types, kexactors, nexttile, verbose)
        ramsesdefs, ramsesrows = build_ramses_tiles(
            z, outdir, nexttile + len(decordefs), verbose)
        KEX_SEQ_EXTRA_TILES.update(build_manacle_seq(z, outdir, verbose))
        build_amunbomb_seq(z, outdir, verbose)
        KEX_SEQ_EXTRA_TILES.update(build_bastet_fish_sequences(
            z, demo_dir, outdir, verbose))
        KEX_SEQ_EXTRA_TILES.update(build_spider_sequences(z, outdir, verbose))
        KEX_SEQ_EXTRA_TILES.update(build_anubis_sequence(
            z, demo_dir, outdir, verbose))
        KEX_SEQ_EXTRA_TILES.update(build_mummy_sequence(z, outdir, verbose))
        seqdefs = build_seq_tiles(z, outdir, verbose)
        muzzledefs = build_kex_muzzle_sequence(z, outdir, verbose)
        impactdefs = build_kex_impact_sequence(z, outdir, verbose)
        cobradefs = build_kex_cobra_impact_sequences(z, outdir, verbose)
        anubisimpactdefs = build_kex_anubis_impact_sequence(z, outdir, verbose)
        explosiondefs = build_kex_explosion_sequences(z, outdir, verbose)
        vasepuffdefs = build_kex_vase_puff_sequence(z, outdir, verbose)
        orbspawndefs = build_kex_orb_spawn_sequences(z, outdir, verbose)
        gibdeathdefs = build_kex_gib_death_sequence(z, outdir, verbose)
        watersplashdefs = build_kex_water_splash_sequence(z, outdir, verbose)
        flamedefs = build_kex_flame_sequence(z, outdir, verbose)
        build_kex_bird_feather_sequence(z, outdir, verbose)
    # AI_Sentry's exact action timing and eight-view layered art. The stock
    # Roach sequence compresses its ten-frame ranged attack to eight frames.
    try:
        from kexsentryseq import KEX_SENTRY_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'ROACH.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_SENTRY_SEQ))
        if verbose: print("  sentries: exact ROACH.SEQ written (%d chunks)" % len(KEX_SENTRY_SEQ['ck_pict']))
    except ImportError:
        if verbose: print("  sentries: kexsentryseq.py missing; keeping the demo timing")
    # AI_Wasp's exact retail action timing. The stock demo file stretches the
    # eight-frame sting over thirteen frames and has no distinct chase group.
    try:
        from kexwaspseq import KEX_WASP_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'WASP.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_WASP_SEQ))
        if verbose: print("  wasps: exact WASP.SEQ written (%d chunks)" % len(KEX_WASP_SEQ['ck_pict']))
    except ImportError:
        if verbose: print("  wasps: kexwaspseq.py missing; keeping the demo timing")
    # The crows' dedicated sequence file on bird tiles.
    try:
        from kexbirdseq import KEX_BIRD_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'HAWK.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_BIRD_SEQ))
        if verbose: print("  crows: HAWK.SEQ written (%d chunks)" % len(KEX_BIRD_SEQ['ck_pict']))
    except ImportError:
        if verbose: print("  crows: kexbirdseq.py missing; crows keep the wasp's look")
    # Queen seedlings share Build's flyer controller too, but have their own
    # remaster art and bite sound. The optional sequence keeps that identity
    # after the thrown egg hatches.
    try:
        from kexseedlingseq import KEX_SEEDLING_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'SEEDLING.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_SEEDLING_SEQ))
        if verbose: print("  Queen seedlings: SEEDLING.SEQ written (%d chunks)" % len(KEX_SEEDLING_SEQ['ck_pict']))
    except ImportError:
        if verbose: print("  Queen seedlings: kexseedlingseq.py missing; hatchlings keep the wasp's look")
    if demo_dir:
        patch_demo_seqs(demo_dir, outdir, verbose)
    # Selkis' exact retail action groups supersede the stock-layout art
    # fallback. In particular, melee is nine rather than eleven frames and
    # the three-frame explosion loop drives its 24-count death callback.
    try:
        from kexselkisseq import KEX_SELKIS_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'SCORP.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_SELKIS_SEQ))
        if verbose: print("  Selkis: exact SCORP.SEQ written (%d chunks)" % len(KEX_SELKIS_SEQ['ck_pict']))
    except ImportError:
        if verbose: print("  Selkis: kexselkisseq.py missing; keeping the patched demo timing")
    # AI_Set's exact retail groups supersede patch_demo_seqs()' compatible
    # stock-layout fallback. Write it after that fallback so the 13-frame leap,
    # 15-frame soul cast and 30-frame death cannot be overwritten by SET.SEQ
    # from STUFF.DAT.
    try:
        from kexsetseq import KEX_SET_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'SET.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_SET_SEQ))
        if verbose: print("  Set: exact SET.SEQ written (%d chunks)" % len(KEX_SET_SEQ['ck_pict']))
    except ImportError:
        if verbose: print("  Set: kexsetseq.py missing; keeping the patched demo timing")
    # The Queen's retail controller uses action groups and callback timing
    # that cannot be represented by the demo layouts. Install the exact body,
    # snake/tail, and thrown seedling-pod files after patch_demo_seqs() so its
    # compatible fallback cannot overwrite them.
    try:
        from kexqueenseq import KEX_QUEEN_SEQ, KEX_QUEENEGG_SEQ
        import kexseqtool
        with open(os.path.join(outdir, 'QUEEN.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_QUEEN_SEQ))
        with open(os.path.join(outdir, 'QUEENEGG.SEQ'), 'wb') as f:
            f.write(kexseqtool.write_seq_bytes(KEX_QUEENEGG_SEQ))
        if verbose:
            print("  Queen: exact QUEEN.SEQ (%d chunks) and QUEENEGG.SEQ "
                  "(%d chunks) written" %
                  (len(KEX_QUEEN_SEQ['ck_pict']),
                   len(KEX_QUEENEGG_SEQ['ck_pict'])))
    except ImportError:
        if verbose:
            print("  Queen: kexqueenseq.py missing; keeping the patched demo timing")
    nexttile += len(decordefs) + len(ramsesdefs)
    if beam_type is not None and beam_type in decor:
        tile, scale, _fb = decor[beam_type]
        with open(os.path.join(outdir, 'kextrap.txt'), 'w') as f:
            f.write("# beam <tile> <repeat>   the bolt sprite KexBeam chains along a laser beam\n")
            f.write("beam %d %d\n" % (tile, max(1, min(255, int(round(4.0 * scale * WORLD))))))
        if verbose: print("  laser beam bolt: tile %d" % tile)

    done = 0
    teleports = []
    floatrows = []
    padrows = []
    portalrows = []
    togglerows = []
    chanrows = []
    forcerows = []
    for name in sorted(parsed):
        m = parsed[name]
        for tag, chan in sorted(_channels(m).items()):
            if tag != KEX_DEAD_TRAP and (tag < -1 or tag >= 1000):
                chanrows.append((KEX_MAP_PREFIX + os.path.basename(name).upper(), tag, chan))
        local = [assign.get(t, tilebase) for t in m.textures]
        sects, walls, ranges = convert(m, tilebase=0, switch_pairs=sw_pairs,
                                       ramses=KEX_RAMSES.get(name.upper()))
        skytile = assign.get(m.sky)
        for w in walls:
            # An untextured solid face -- some 420 across the levels, all
            # flagged "Invisible" -- shows the sky in Kex; there is no invisible
            # one-sided wall in Build, so it wears the sky texture rather than
            # whichever texture index 0 happens to be.
            if '_comp' in w and w['_comp'] in comp_assign:
                w['picnum'] = comp_assign[w['_comp']]; comp_used.add(w.pop('_comp'))
            else:
                w.pop('_comp', None)
                w['picnum'] = local[w['picnum']] if 0 <= w['picnum'] < len(local) else (skytile if (w['picnum'] < 0 and skytile is not None) else tilebase)
            if 'overpicnum' in w:      # a masked portal's texture, the same way
                o = w['overpicnum']
                w['overpicnum'] = local[o] if o < len(local) else 0
                if o in getattr(m, 'kex_force_tiles', ()):
                    m.kex_force_global = getattr(m, 'kex_force_global', set()) | {w['overpicnum']}
            for alt in w.get('_kex_portal_alts', ()):
                tex = alt.pop('texture', -1)
                alt['picnum'] = local[tex] if 0 <= tex < len(local) else 0
        for tile in sorted(getattr(m, 'kex_force_global', ())):
            forcerows.append((KEX_MAP_PREFIX + os.path.basename(name).upper(), tile))
        flocal = [fassign.get(t, assign.get(t, tilebase)) for t in m.textures]
        for s in sects:
            if s is None: continue
            s['ceilingpicnum'] = flocal[s['ceilingpicnum']] if 0 <= s['ceilingpicnum'] < len(flocal) else tilebase
            s['floorpicnum']   = flocal[s['floorpicnum']]   if 0 <= s['floorpicnum']   < len(flocal) else tilebase
            fk = s.pop('_fcomp_f', None); ck = s.pop('_fcomp_c', None)
            if fk is not None and fk in fcomp_assign:
                s['floorpicnum'] = fcomp_assign[fk]; fcomp_used.add(fk)
            if ck is not None and ck in fcomp_assign:
                s['ceilingpicnum'] = fcomp_assign[ck]; fcomp_used.add(ck)
            # An untextured floor (KILARENA's arena has 127) is the void Kex
            # draws nothing on: the sky, parallaxed, as an untextured ceiling.
            if s['floorpicnum'] < 0 or s.get('floorsky'):
                s['floorpicnum'] = skytile if skytile is not None else tilebase
                s['floorstat'] |= 1
            if s['ceilingsky'] and skytile is not None:
                # A sky ceiling keeps the height Kex gives it. It used to be
                # capped 2048 world units above the floor, on the suspicion that
                # a ceiling that far off, nearly edge-on at the horizon, was
                # the source of the noise band along the top of the screen;
                # the band turned out to be the map-name collision, and the
                # parallax sky never draws the plane anyway. What the cap did
                # do was cut off everything above it: 956 of the 2,653 capped
                # sky sectors across the levels have a neighbour with a
                # textured ceiling above the cap -- TOMB's brick canopy over
                # the temple porch, roofs, lintels -- and their front bands and
                # undersides vanished behind a sky plane 341 units up.
                # Drawn as an ordinary textured ceiling rather than with Build's
                # parallax bit. The remaster's skies are 256x512 portrait tiles;
                # fed to the parallax sky renderer they came out black with a
                # band of noise along the top of the screen -- 31% of the frame.
                # A plain ceiling shows the right sky colour and costs only the
                # parallax scroll, which barely reads in these walled courtyards.
                s['ceilingpicnum'] = skytile
                # The sky is lit by definition -- shading it down was what made
                # a daylit level read as night.
                s['ceilingshade'] = 0
                # Parallax: a sky has to sit at the horizon rather than be a lid
                # painted on the ceiling plane. An earlier attempt at this drew
                # black, but that was a portrait tile shaded down to 30; squared
                # and unshaded it is worth trying properly.
                s['ceilingstat'] |= 1
        start = player_start(m, sects, walls, ranges)
        _, remap = sector_remap(sects, ranges)
        mapkey = name.upper().rsplit('.', 1)[0]
        portalrows.extend(kex_portal_rows(mapkey, sects, walls, ranges))
        togglerows.extend(kex_toggle_rows(m, mapkey, sects, walls, ranges))
        for group, piece, ceiling, height, phase, offset in getattr(m, 'kex_float_surfaces', ()):
            if piece in remap:
                floatrows.append((mapkey, group, remap[piece], ceiling,
                                  height, phase, offset))
        for group, piece, ceiling, tag in getattr(m, 'kex_drop_surfaces', ()):
            if piece in remap:
                padrows.append((mapkey, group, remap[piece], ceiling, tag))
        sprites = build_sprites(m, sects, walls, ranges, remap, decor)

        # Teleports. Kex 71 is the pad the player steps on and 72 the arrival.
        # TeleportEvent() places the actor at the first floor triangle's centre
        # and deliberately preserves its heading; event.params is ignored.
        # Build PowerSlave has no sector teleporter, so the engine reads these
        # rows instead.
        teleports.extend(_teleport_rows(m, remap, mapkey))
        ns, nw, nsp = write_build(os.path.join(outdir, KEX_MAP_PREFIX + name.upper()),
                                  sects, walls, ranges, start, sprites)
        write_lit(os.path.join(outdir, KEX_MAP_PREFIX + name.upper().rsplit('.', 1)[0] + '.LIT'),
                  m, sects, walls, ranges)
        portals = sum(1 for w in walls if w['nextsector'] >= 0)
        if verbose:
            print("  %-16s %4d kex sectors -> %4d build sectors, %5d walls, %4d portals, "
                  "%3d/%d actors" % (name, m.nsector, ns, nw, portals, nsp, m.nactor))
        done += 1


    sizes = collect(kpf, list(assign))
    usable = {tile: name for name, tile in assign.items() if name in sizes}
    missing = len(assign) - len(usable)      # level textures with no PNG in the .kpf
    for tl, fn in decordefs:                 # actor tiles, which are composited here
        usable[tl] = fn
    for tl, fn in ramsesdefs:                # complete scripted Ramses animations
        usable[tl] = fn
    for tl, fn in seqdefs:                   # the stock SEQ tiles the demo lacks
        usable[tl] = fn
    for tl, fn in muzzledefs:                # fireball-spawner shooting puff
        usable[tl] = fn
    for tl, fn in impactdefs:                # fireball/boulder impact puff
        usable[tl] = fn
    for tl, fn in cobradefs:                 # red/green cobra impact puffs
        usable[tl] = fn
    for tl, fn in anubisimpactdefs:          # Anubis fireball death frames
        usable[tl] = fn
    for tl, fn in explosiondefs:             # shared land/water blast actors
        usable[tl] = fn
    for tl, fn in vasepuffdefs:              # ordinary vase/barrel death puff
        usable[tl] = fn
    for tl, fn in orbspawndefs:               # delayed enemy ammo/health drops
        usable[tl] = fn
    for tl, fn in gibdeathdefs:                # generic fullbright gib/death actor
        usable[tl] = fn
    for tl, fn in watersplashdefs:             # air-to-water splash actor
        usable[tl] = fn
    for tl, fn in flamedefs:                   # player Proj_Flame flight/sizzle
        usable[tl] = fn

    # The skies ship as 256x512 portrait tiles, and they are DOME maps, not
    # Build panoramas: the top rows are the zenith, the bottom rows the nadir,
    # and both are flat pole colour with the cloud band in between. Squashing
    # the whole thing into Build's parallax band -- which is what this used to
    # do -- puts the nadir's flat navy on screen as a dark stripe above the
    # clouds, which is the banded sky. Cropping the nadir off first leaves the
    # tile running zenith at the top to horizon at the bottom, which is exactly
    # what Build's parallax band wants: the clouds land on the horizon and
    # looking up clamps to the zenith colour.
    import kexpng as _png

    def _crop_nadir(w, h, pix):
        """Drop the flat pole band at the bottom of a dome map."""
        def row_flat(y, ref):
            for x in range(0, w, 4):
                o = (y * w + x) * 4
                if (abs(pix[o] - ref[0]) > 6 or abs(pix[o+1] - ref[1]) > 6
                        or abs(pix[o+2] - ref[2]) > 6):
                    return False
            return True
        o = ((h - 1) * w) * 4
        ref = (pix[o], pix[o+1], pix[o+2])
        y = h - 1
        while y > 0 and row_flat(y, ref):
            y -= 1
        cut = y + 1
        # Only trust it when the flat band is a real pole, not a stray row.
        if cut >= h or (h - cut) < h // 10:
            return w, h, pix
        return w, cut, _png.crop(w, h, pix, 0, 0, w, cut)
    skydir = os.path.join(outdir, 'kexart')
    os.makedirs(skydir, exist_ok=True)
    with zipfile.ZipFile(kpf) as z:
        have = {n.lower(): n for n in z.namelist()}
        for tile, name in list(usable.items()):
            if '/skies/' not in name.lower():
                continue
            real = have.get(name.lower())
            if real is None:
                continue
            tmp = os.path.join(skydir, os.path.basename(name))
            open(tmp, 'wb').write(z.read(real))
            w_, h_, pix = _png.read(tmp)
            w0, h0 = w_, h_
            w_, h_, pix = _crop_nadir(w_, h_, pix)

            # Build puts the tile's vertical CENTRE on the horizon: polymost
            # computes the sky's v origin from (tilesiz.y >> 1). The crop above
            # runs zenith at the top to horizon at the bottom, so its centre is
            # half way up the sky and everything below that never appears --
            # which is why the clouds sat too low and the visible band came out
            # stretched over the whole view. Doubling the tile and repeating the
            # horizon row underneath puts the real horizon on the centre line.
            out_h = h_ * 2
            buf = bytearray(w_ * out_h * 4)
            buf[:w_ * h_ * 4] = pix[:w_ * h_ * 4]
            last = pix[(h_ - 1) * w_ * 4: h_ * w_ * 4]
            for y in range(h_, out_h):
                buf[y * w_ * 4:(y + 1) * w_ * 4] = last
            _png.write(tmp, w_, out_h, bytes(buf))
            usable[tile] = 'kexart/' + os.path.basename(name)
    skytiles = set()
    for m in parsed.values():
        if m.sky and m.sky in assign:
            skytiles.add(assign[m.sky])
    # The flat tile carries real pixels -- the image box-filtered down to its
    # texel count -- so the classic 8-bit renderer, which never sees a hires
    # replacement, still draws the floor; polymost draws the full image on it.
    flatdefs = []
    flatdir = os.path.join(outdir, 'kexart', 'flats')
    os.makedirs(flatdir, exist_ok=True)
    with zipfile.ZipFile(kpf) as z:
        have = {n.lower(): n for n in z.namelist()}
        for t, tl in sorted(fassign.items(), key=lambda kv: kv[1]):
            if t not in assign or assign[t] not in usable or t not in sizes: continue
            real = have.get(t.lower())
            if real is None: continue
            w_, h_ = sizes[t]
            nw_ = max(1, int(round(w_ * KEX_FLAT_TEXELS / 64.0)))
            nh_ = max(1, int(round(h_ * KEX_FLAT_TEXELS / 64.0)))
            small = os.path.join(flatdir, os.path.basename(t))
            tmp = small + '.src'
            open(tmp, 'wb').write(z.read(real))
            try:
                sw, sh, pix = _png.read(tmp)
                _png.write(small, nw_, nh_, _png.downsample(sw, sh, pix, nw_, nh_))
            finally:
                os.remove(tmp)
            flatdefs.append((tl, 'kexart/flats/' + os.path.basename(t), usable[assign[t]]))
    if verbose:
        print("  flats: %d textures given %d-texel tiles" % (len(flatdefs), int(KEX_FLAT_TEXELS)))
    for tile, path in sorted(_bake_composites(kpf, {k: t for k, t in comp_assign.items() if k in comp_used}, outdir, verbose).items()):
        flatdefs.append((tile, path, path))
    for tile, path in sorted(_bake_flat_composites(kpf, {k: t for k, t in fcomp_assign.items() if k in fcomp_used}, outdir, verbose).items()):
        flatdefs.append((tile, path, path))
    write_def(os.path.join(outdir, "exhumed.def"), usable, animdefs, skytiles, flatdefs)

    # Breakables. The remaster's vases and barrels are shootable props: the
    # actor def gives health and a deathAnim, and that anim tosses one shard
    # actor per shard and destroys the pot -- e.g. vase01_death plays
    # glassbreak.wav, spawns a dust puff, drops a random item and tosses
    # Obj_Vase01_Shard01..06. The shard art is already in the object's own idle
    # anim: frame 0 is the intact pot and the frames after it are the shards,
    # which is why holding the pot on frame 0 (noAdvanceFrames) is what stops it
    # flickering into a sliver of debris.
    def _clipdist(radius):
        # Build clips a sprite at clipdist<<2 world units, and Kex authors the
        # radius in its own units, so the world factor applies and the result
        # is divided by four.
        return max(1, min(255, int(round(radius * WORLD / 4.0))))

    # The radial blast of the exploding pots is in the remaster's own units.
    # blasttoggle is A_RadialBlast's third argument: one opens authored Toggle
    # faces, zero (the cobra projectiles) does not.
    # ... and the mine's sight radius (Build units) and fuse (ticks), zero for
    # everything that is not a mine.
    _write_table(outdir, "kexbreak.txt",
                 "tile health shardtile0 shardcount clipdist blastradius blastdamage sight fuse wood blasttoggle breaksound spawnheight",
                 [(tile, health, shard0, nshards, _clipdist(radius), bl_r, bl_d,
                   sight, fuse, wood, blasttoggle, breaksound,
                   int(round(spawnheight)))
                  for tile, health, shard0, nshards, radius, bl_r, bl_d,
                      sight, fuse, wood, blasttoggle, breaksound, spawnheight
                      in sorted(breakables)],
                 "breakables", verbose)

    # Vases and monster deaths create these actors at runtime even when none
    # is authored in a level. kexbreak.cpp needs their generated KEX tiles.
    map_tile = 0
    if map_type is not None and map_type in decor:
        map_tile = decor[map_type][0]
    power_tile = 0
    if power_type is not None and power_type in decor:
        power_tile = decor[power_type][0]
    ra_tile = 0
    if ra_type is not None and ra_type in decor:
        ra_tile = decor[ra_type][0]
    cobra_tile = 0
    if cobra_type is not None and cobra_type in decor:
        cobra_tile = decor[cobra_type][0]
    cobra_joint_tile = 0
    if cobra_joint_type is not None and cobra_joint_type in decor:
        cobra_joint_tile = decor[cobra_joint_type][0]
    ammo_tile = 0
    if ammo_type is not None and ammo_type in decor:
        ammo_tile = decor[ammo_type][0]
    health_tile = 0
    if health_type is not None and health_type in decor:
        health_tile = decor[health_type][0]
    _write_table(outdir, "kexdrop.txt",
                 "maptile powertile ratile cobratile cobrajointtile ammotile healthtile",
                 [(map_tile, power_tile, ra_tile, cobra_tile, cobra_joint_tile, ammo_tile, health_tile)]
                 if all((map_tile, power_tile, ra_tile, cobra_tile, cobra_joint_tile,
                         ammo_tile, health_tile)) else [],
                 "rare drop art", verbose)

    # Light sources: tile, then the peak brightening in shade steps and the
    # falloff shift (radius = peak << shift, in Build world units).
    _write_table(outdir, "kexlight.txt", "tile peak shift",
                 [(tile, KEX_LIGHT_PEAK, KEX_LIGHT_SHIFT) for tile in sorted(lights)],
                 "light sources", verbose)

    # Animated scenery is selected in analyzesprites rather than through
    # Build's global animtilerange clock. That preserves Kex's per-instance
    # randomizeFrames phase: two adjacent braziers advance through the same
    # authored frames without changing shape on the same tick.
    _write_table(outdir, "kexdecor.txt", "tile count ticks randomphase",
                 sorted(decoranims), "animated scenery ranges", verbose)

    # The remaster's scripted spirit: complete tile ranges, KEX actor repeat,
    # and totalclock ticks per visual frame (two ticks = its native 60 Hz).
    _write_table(outdir, "kexramses.txt", "phase first count repeat ticks",
                 ramsesrows, "Ramses presentation ranges", verbose)

    # Teleport pads, per map: the Kex-71 pad's Build sector, then its Kex-72
    # arrival sector and floor-triangle centre. A negative angle preserves the
    # player's heading, as Kex TeleportEvent() does.
    _write_table(outdir, "kextele.txt", "map padsector destsector x y z ang",
                 teleports, "teleports", verbose)

    # Floating slabs: every split piece of the source ceiling and partner
    # floor, grouped so the engine applies one cosine delta to the whole pair.
    _write_table(outdir, "kexfloat.txt", "map group sector ceiling height phase offset",
                 floatrows, "floating-platform surfaces", verbose)

    _write_table(outdir, "kexpad.txt", "map group sector ceiling tag",
                 padrows, "paired drop-pad surfaces", verbose)

    # A Kex edge may open into different sectors in vertical bands. Build has
    # one nextsector per wall, so the runtime selects these alternatives at the
    # moving actor/camera's Z and restores the ordinary topology afterwards.
    _write_table(outdir, "kexportal.txt",
                 "map wall nextwall nextsector top0 bottom0 top1 bottom1 flags picnum",
                 portalrows, "height-selected wall portals", verbose)

    # Authored bomb walls/floors/ceilings. F is one logical source face plus
    # its retail recursive-open bitset; W maps it to converted wall bands; S
    # maps a vertical pair to sectors that become a YAX bunch when destroyed.
    _write_table(outdir, "kextoggle.txt",
                 "map op  F:id kind x y z closure[8]  W:id wall nextwall nextsector top0 bottom0 top1 bottom1 dynamic clearreverse  S:id seam sector ceiling_or_floor",
                 togglerows, "authored Toggle faces", verbose)

    # Tags of 1000 and up, and the channel each was allotted (see _channels).
    _write_table(outdir, "kexchan.txt", "map tag channel", chanrows, "channel allotments", verbose)
    _write_table(outdir, "kexforce.txt", "map tile", forcerows, "force field tiles", verbose)

    # Sounds: LoadSound() consults this before opening the original VOC, so the
    # remaster's own recordings play wherever one corresponds.
    _write_table(outdir, "kexsound.txt", "buildname remasterpath loop",
                 [(name, path, loop) for name, (path, loop) in sorted(KEX_SOUNDS.items())],
                 "sound aliases", verbose)

    # The menu art rides along: exhumed.def is the one file the game loads by
    # name, so it pulls in the generated menu definitions.
    print("menu art:")
    kexmenu.extract(kexdir, outdir)
    with open(os.path.join(outdir, "exhumed.def"), "a") as f:
        f.write('\ninclude "kexmenu.def"\n')
    print("converted %d/%d levels into %s" % (done, len(maps), outdir))
    # usable also holds the actor tiles composited above, so the shortfall is
    # counted against the level textures alone rather than against the total.
    print("%d tiles wired up in exhumed.def (%d level textures had no PNG in the .kpf)"
          % (len(usable), missing))
    for name, err in failed:
        print("  FAILED %s: %s" % (name, err))
    print()
    print("Play one with:")
    print('  ./vibebuild32 -gm exhumed -j %s \\' % outdir)
    firstmap = 'KEX_' + sorted(maps)[0].upper() if maps else "KEX_LEVEL.MAP"
    print('      -kex "%s" -map %s' % (kexdir, firstmap))
    return done


def PSM_from_bytes(blob):
    return locate(blob)


if __name__ == '__main__':
    argv = [arg for arg in sys.argv[1:] if arg != '--testmaps']
    if len(argv) < 2:
        raise SystemExit(__doc__.strip().split('\n\n')[1])
    convert_install(argv[0], argv[1], demo_dir=argv[2] if len(argv) > 2 else None,
                    include_testmaps='--testmaps' in sys.argv[1:])
