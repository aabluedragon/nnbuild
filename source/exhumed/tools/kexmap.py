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
        -g "/path/to/PowerSlave Exhumed/PowerslaveEX.kpf" -map KARNAK.MAP

The .kpf is a zip, which the Build engine mounts natively, so the textures are
read straight out of the remaster's own archive -- nothing is copied or
converted, and no palette reduction happens.

You still need the Build-engine PowerSlave data (STUFF.DAT) for the HUD,
weapons and sounds; this brings the remaster's *levels*, not a whole game.

WHAT CONVERTS
    Sector and wall geometry, floor and ceiling heights, portals between
    sectors, wall/floor/ceiling textures, the player start.

WHAT DOES NOT (yet)
    Actors -- enemies, pickups, doors, switches, triggers. The levels are
    walkable but empty.
    Room-over-room. Kex lets one wall link several sectors; plain Build cannot,
    so only the tallest portal on an edge survives and sectors that are not a
    single closed ring are dropped (about 2% of them across the retail maps).
    Sloped floors and ceilings, which come out flat.
    Baked vertex lighting, which the format carries but Build has no place for.

THE FORMAT
    Reverse-engineered against the .dmap/.map pairs Nightdive ships in
    docs/examples/ -- every field was checked against the matching text source.
    See PSM.__init__ below for the layout.
"""
import os, re, struct, sys, zipfile
import kexmenu

# ---------------------------------------------------------------------------
# Reading the Kex map
# ---------------------------------------------------------------------------
# 
# Reverse-engineered against the .dmap/.map pairs Nightdive ships in
# docs/examples/: every field below was checked against the matching text source.
# 
#   header   'PSM\\x01' + 8 x u32:
#              ntex, nvert, nsector, nface, ntile, nzone, nevent, nactor
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
#            float angle, float nx, ny, nz, u16 flags, i16 pad,
#            i16 tileVertStart, i16 tileVertEnd
#   tiles    ntile x 16: u8[4], u16[4] vertex indices, u16 textureIndex, u16 pad
#   zones    nzone x 32
#   events   nevent x 8: u16 eventType, u16 sector, i16 eventTag, i16 params
#            eventType is the SAME numbering Build PowerSlave uses for its own
#            sector effects (1 = ceiling door, 7 = touchplate floor lower,
#            21 = touchplate, 24 = channel-triggered ceiling door), because the
#            console and DOS versions descend from one design. So an event maps
#            straight onto sector.lotag/hitag and the game's own
#            runlist_ProcessSectorTag() builds the door.
#   actors   nactor x 20, at the very end of the file:
#            u16 type, u16 ?, i16 x, i16 y, i16 z, i16 tag,
#            u16 param1, u16 param2, u16 layer, u16 ?


class Face:
    __slots__=('tileStart','tileEnd','vertStart','link','angle','n','flags',
               'tileVertStart','tileVertEnd')
class Sector:
    __slots__=('faceStart','wallEnd','raw')
class Actor:
    __slots__=('type','x','y','z','tag','p1','p2','layer','ang','raw')

class PSM:
    def __init__(self, data, vstart=None):
        d=self.data=data
        if d[:4]!=b'PSM\x01': raise ValueError('not a Kex PSM map')
        (self.ntex,self.nvert,self.nsector,self.nface,
         self.ntile,self.nzone,self.nevent,self.nactor)=struct.unpack_from('<8I',d,4)
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
            f.tileVertStart,f.tileVertEnd=struct.unpack_from('<2h',d,p+28)
            self.faces.append(f)
        self.tileOff=self.faceOff+self.nface*32
        self.tiles=[]
        for i in range(self.ntile):
            p=self.tileOff+i*16
            self.tiles.append((struct.unpack_from('<4H',d,p+4),
                               struct.unpack_from('<H',d,p+12)[0]))
        # zones (32 bytes each) then events (8), then the actors at the end.
        # Both sizes were solved from the gap between the tile array and the
        # actor array across all fifteen sample maps: gap = nzone*32 + nevent*8,
        # exact for every one of them.
        self.zoneOff = self.tileOff + self.ntile*16
        self.eventOff = self.zoneOff + self.nzone*32
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
            a.type=u[0]&0xffff; a.x,a.y,a.z=u[2],u[3],u[4]
            a.tag=u[5]; a.p1=u[6]; a.p2=u[7]; a.layer=u[8]; a.raw=u
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
            if f.vertStart+3 >= self.nvert: return False
            if f.tileStart==-1:
                if f.tileEnd!=-1: return False
            else:
                if f.tileStart!=last+1 or f.tileEnd<f.tileStart or f.tileEnd>=self.ntile:
                    return False
                last=f.tileEnd
        return last==self.ntile-1 and self.tileOff+self.ntile*16 <= len(self.data)-self.nactor*20

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
# The original game runs about 20.4 world units per texel (measured from its own
# shipped xrepeat values); at WORLD=6 a Kex texel covers 6 world units, which is
# finer -- as it should be, the remaster's art has more resolution.
#     xrepeat = length / (8 * units_per_texel)
KEX_UNITS_PER_TEXEL = WORLD

# yrepeat is a vertical scale and does not depend on the wall's length. The
# original game pairs yrepeat 7 with its 20.4 units per texel, which pins the
# constant at ~143; at 6 units per texel that lands on 24.
# Build shows xrepeat*8 texels across a wall, and vertically one texel every
# 128/yrepeat world units. That 128 is measured rather than assumed: across the
# 13150 walls in the six levels that ship in STUFF.DAT, yrepeat * (world units
# per texel across) has its mode at exactly 128 (5735 walls) and a median of
# 129.6, which is what square texels give. Kex authors one texel per Kex unit
# and a Kex unit is WORLD world units, so across is 6.0 and yrepeat wants to be
# 128/6 = 21.3. The 143 this used to carry produced 24, squashing every wall
# texture 12% vertically.
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
    groups = {}; order = []
    for fi in range(s.faceStart, min(s.wallEnd+1, m.nface)):
        f = m.faces[fi]
        # And the first two vertices are not always the horizontal edge: on some
        # faces they are the vertical one, so they project to a single 2D point.
        # Skipping those as degenerate dropped a whole side of the ring, which
        # left 90 of MINES' sectors as an open chain of three segments. A wall
        # face is a vertical quad, so its four corners give exactly two distinct
        # 2D points -- take the widest-apart pair and the direction sorts itself
        # out below.
        pts = []
        for k in range(4):
            v = m.verts[f.vertStart + k]
            pt = (round(v[0], 2), round(v[1], 2))
            if pt not in pts:
                pts.append(pt)
        if len(pts) < 2:
            continue                      # genuinely degenerate in 2D
        if len(pts) == 2:
            pa, pb = pts
        else:
            pa, pb = max(((p, q) for i, p in enumerate(pts) for q in pts[i+1:]),
                         key=lambda pq: (pq[0][0]-pq[1][0])**2 + (pq[0][1]-pq[1][1])**2)
        key = (pa, pb) if pa <= pb else (pb, pa)
        if key not in groups:
            groups[key] = []; order.append(key)
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
    if any(len(v) != 2 for v in adj.values()):
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

# How many times one sector may be cut in half along a contour -- 4 allows 16
# strips, which is what a crater wall steeper than Build's steepest slope
# terraces into. See _split_slopes().
KEX_SPLIT_DEPTH = 4


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
    return src, pinned


# Fires light their surroundings: peak brightening in Build shade steps at the
# sprite, falling off linearly to nothing at (peak << shift) world units.
KEX_LIGHT_PEAK = 36
KEX_LIGHT_SHIFT = 6


def _zspan(m, f):
    zs = [m.verts[f.vertStart+k][2] for k in range(4)]
    return min(zs), max(zs)

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


def _pic(m, f, tilebase):
    if f.tileStart < 0: return tilebase
    return tilebase + m.tiles[f.tileStart][1]

def _surface(pl, ring, wx, wy, dx, dy, sky=False, ceiling=False):
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
        return int(round(min(zs))), 0          # Build z grows downward
    z_, h_ = _slope(pl, wx, wy, dx, dy)
    err = _ring_error(pl, ring, z_, h_, wx, wy, dx, dy)
    mean = sum(zs) / len(zs)
    flat = max(abs(v - mean) for v in zs)
    if err <= KEX_SLOPE_TOLERANCE or err <= flat:
        return z_, h_
    return int(round(mean)), 0


def convert(m, tilebase=4096, switch_pairs=None):
    walls = []; sects = []; ranges = []
    # Kex keeps a sector's trigger in a separate event table; Build PowerSlave
    # wants it on the sector as lotag (the effect) and hitag (the channel).
    sector_events = {}
    for etype, esect, etag, eparam in getattr(m, 'events', ()):
        if esect < m.nsector and etype:
            sector_events[esect] = (etype, esect, etag, eparam)
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
    _split_tjunctions(m, edge_cache)
    pieces = {}
    for i, o in enumerate(src):
        pieces.setdefault(o, []).append(i)

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
            portal = None; pspan = -1.0
            for fi in fis:
                if isinstance(fi, _Cut):
                    portal = fi; break
                f = m.faces[fi]
                if f.link >= 0:
                    lo,hi = _zspan(m,f)
                    if hi-lo >= pspan: pspan = hi-lo; portal = f
            if fis and not isinstance(fis[0], _Cut):
                solid = next((m.faces[fi] for fi in fis if m.faces[fi].link < 0), m.faces[fis[0]])
            else:
                # A side the source never gave a face: either synthesised by
                # _edges to close the ring, where nothing is ever drawn and it
                # stays solid, or the seam of a contour split, which is a
                # portal into the other half. Either way the sector's floor
                # lends the texture.
                solid = ff_face
            # Build shows exactly xrepeat*8 texels across a wall (polymost's
            # u spans wal->xrepeat*8), and Kex authors its levels at one texel
            # per world unit -- Nightdive's own doc: a 64x64 texture on a wall
            # 512 units wide takes 8 tile columns. So xrepeat follows the wall's
            # LENGTH; a fixed value smears every wall by the ratio of its length
            # to 64.
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
                              shade=_shade(m, solid if _lit(solid) else ff_face)))
        ranges.append((start, len(walls)-start))
        cf = cf_face
        ff = ff_face
        w0 = walls[start]
        w1 = walls[start + 1] if len(walls) - start > 1 else w0
        dxw, dyw = (w1['x'] - w0['x']) * WORLD, (w1['y'] - w0['y']) * WORLD
        wx0, wy0 = w0['x']*WORLD, w0['y']*WORLD
        ring = [(w['x']*WORLD, w['y']*WORLD) for w in walls[start:]]
        cz, cheinum = _surface(_plane_build(m, cf), ring, wx0, wy0, dxw, dyw,
                               sky=(cf.tileStart < 0), ceiling=True)
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
        sects.append(dict(yax_ceil=(cf.link if cf.tileStart < 0 else -1),
                          yax_floor=(ff.link if ff.tileStart < 0 else -1),
                          wallptr=start, wallnum=len(walls)-start,
                          lotag=0, hitag=0,
                          kexevent=(ev if ev else None),
                          ceilingsky=(cf.tileStart < 0),
                          ceilingz=cz, floorz=fz,
                          ceilingheinum=cheinum, floorheinum=fheinum,
                          ceilingstat=(2 if cheinum else 0), floorstat=(2 if fheinum else 0),
                          ceilingpicnum=_pic(m,cf,tilebase), floorpicnum=_pic(m,ff,tilebase),
                          ceilingshade=_shade(m,cf), floorshade=_shade(m,ff)))

    # nextwall: the neighbour's wall carrying the same edge reversed. A face
    # links to a SOURCE sector; when that sector was cut the edge may sit in
    # any of its pieces, so all of them are searched and nextsector is set to
    # the piece that actually has it.
    for si,(start,num) in enumerate(ranges):
        for i in range(num):
            w = walls[start+i]
            ns = w['nextsector']
            if not (0 <= ns < len(ranges)):
                w['nextsector'] = -1; continue
            nxt = walls[w['point2']]
            hit = -1
            for cand in pieces.get(src[ns] if ns < len(src) else ns, [ns]):
                ns_start, ns_num = ranges[cand]
                for j in range(ns_num):
                    o = walls[ns_start+j]; op = walls[o['point2']]
                    if (o['x'],o['y']) == (nxt['x'],nxt['y']) and (op['x'],op['y']) == (w['x'],w['y']):
                        hit = ns_start+j; break
                if hit >= 0:
                    w['nextsector'] = cand; break
            w['nextwall'] = hit
            if hit < 0: w['nextsector'] = -1
    for wi,w in enumerate(walls):          # a portal only counts if both halves agree
        if w['nextwall'] >= 0 and walls[w['nextwall']]['nextwall'] != wi:
            w['nextwall'] = -1; w['nextsector'] = -1

    _resolve_events(m, sects, walls, ranges, src, switch_pairs)
    m.kex_flat_sprites = _flat_sprites(m, sects, walls, ranges, src)
    return sects, walls, ranges

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
                if t <= 1e-6 or t >= 1.0 - 1e-6:
                    continue
                hits.append((t, vx, vy))
            if not hits:
                out.append((k, fis)); continue

            hits.sort()
            px, py = x1, y1
            for _t, vx, vy in hits:
                if (vx, vy) == (px, py):
                    continue
                out.append(((px, py, vx, vy), fis))
                px, py = vx, vy
                nsplit += 1
            out.append(((px, py, x2, y2), fis))
        edge_cache[si] = out
    if verbose and nsplit:
        print("  T-junctions: %d edge splits so both sides of a portal agree" % nsplit)
    return nsplit


def _neighbours(sects, walls, ranges, si):
    """The distinct live sectors this piece opens onto."""
    start, num = ranges[si]
    out = set()
    for i in range(num):
        ns = walls[start+i]['nextsector']
        if 0 <= ns < len(sects) and sects[ns]:
            out.add(ns)
    return out


def _resolve_events(m, sects, walls, ranges, src, switch_pairs=None):
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
      FLOOR_TRIGGER_PLATE and REMOTE_TRIGGER -> 21, the touchplate: stand in
        the sector, the channel fires. REMOTE_TRIGGER is the walk-in trigger
        region and the most common event in the game; an earlier version read
        it as Build 50, a floor elevator, and sank 436 sectors at load.
      FLOOR_LOWER -> 8 (permanent floor lower), FLOOR_LOWER_WAIT_RAISE and
        FLOOR_LOWER_ACTIVE -> 9 (lift down), FLOOR_RAISE_WAIT_LOWER -> 12
        (lift up), DROP_PAD -> 6 (touchplate floor lower).
      EXPLODE -> 35, the destructible sector (the bomb wall).
      SWITCH1-4 -> nothing on the sector: the switch lives on a WALL in Build
        (wall lotag 7, runlist_ProcessWallTag), handled below.

    Every mover Build builds at load walks the neighbour list for the height it
    travels to and asserts it found one, so each mapping is guarded on the
    neighbour that effect actually needs; a door with nowhere to open stays
    static rather than crashing the load or diving out of the map.

    Not yet carried across, left untagged on purpose: the eight FLOATER
    variants and SCRIPTED_MOVE (moving platforms -- Build's waypoint movers
    need waypoint sprites synthesised from the angle baked into the variant
    name), SCRIPT/SCRIPT_REPEATABLE (the per-level AngelScript hooks),
    ARTIFACT_TRIGGER/_SPAWN (no artifact inventory yet), ACTOR_TRIGGER (fires
    when tagged actors die), and TELEPORT_START/_DEST (Build PowerSlave has no
    sector teleporter)."""
    for si, s_ in enumerate(sects):
        if s_ is None:
            continue
        ev = s_.pop('kexevent', None)
        if not ev:
            continue
        etype, _esect, etag, eparam = ev
        name = KEX_EVENTS.get(etype)
        chan = max(0, etag) % 1000
        nbs = _neighbours(sects, walls, ranges, si)
        # Build z grows DOWNWARD: a neighbour ceiling above ours has a smaller
        # ceilingz, a neighbour floor below ours a greater floorz.
        ceil_above  = any(sects[n]['ceilingz'] < s_['ceilingz'] for n in nbs)
        floor_below = any(sects[n]['floorz'] > s_['floorz'] for n in nbs)
        floor_above = any(sects[n]['floorz'] < s_['floorz'] for n in nbs)

        lotag = hitag = 0
        if name in ('DOOR_NORMAL', 'DOOR_KEY1', 'DOOR_KEY2', 'DOOR_KEY3',
                    'DOOR_KEY4') and ceil_above:
            lotag = 1
            key = {'DOOR_KEY1': 1, 'DOOR_KEY2': 2,
                   'DOOR_KEY3': 3, 'DOOR_KEY4': 4}.get(name, 0)
            hitag = key * 1000 + chan
        elif name in ('DOOR_TRIGGER_ONLY', 'DOOR_OPEN_ONLY_MESSAGE') and ceil_above:
            lotag, hitag = 24, chan
        elif name in ('DOOR_CLOSE', 'DOOR_CLOSE_SLOW'):
            lotag, hitag = 75, chan
        elif name in ('FLOOR_TRIGGER_PLATE', 'REMOTE_TRIGGER'):
            lotag, hitag = 21, chan
        elif name == 'FLOOR_LOWER' and floor_below:
            lotag, hitag = 8, chan
        elif name in ('FLOOR_LOWER_WAIT_RAISE', 'FLOOR_LOWER_ACTIVE') and floor_below:
            lotag, hitag = 9, chan
        elif name == 'FLOOR_RAISE_WAIT_LOWER' and floor_above:
            lotag, hitag = 12, chan
        elif name == 'DROP_PAD' and floor_above:
            lotag, hitag = 6, chan
        elif name == 'EXPLODE':
            lotag, hitag = 35, chan
        elif name in ('SWITCH1', 'SWITCH2', 'SWITCH3_WOOD', 'SWITCH4_METAL'):
            _place_switch(m, sects, walls, ranges, src, si, chan, eparam,
                          switch_pairs or {})
        s_['lotag'], s_['hitag'] = lotag, hitag


def _place_switch(m, sects, walls, ranges, src, si, chan, eparam, switch_pairs):
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
                # closest to the face's own centre
                fx = sum(m.verts[f.vertStart+k][0] for k in range(4)) / 4.0
                fy = sum(-m.verts[f.vertStart+k][1] for k in range(4)) / 4.0
                best = None
                for i in range(num):
                    w = walls[start+i]; w2 = walls[w['point2']]
                    mx = (w['x'] + w2['x']) / 2.0
                    my = (w['y'] + w2['y']) / 2.0
                    d = (mx - fx*WORLD)**2 + (my - fy*WORLD)**2
                    if best is None or d < best[0]:
                        best = (d, i)
                if best is not None:
                    w = walls[start+best[1]]
                    w['lotag'] = 7
                    w['hitag'] = chan
                    w['picnum'] = tex
                    return True
    # No switch art on any wall of this piece. Only the source sector's first
    # piece falls back -- a stray extra switch on a split half would be worse
    # than none -- and it takes the first wall so the channel still exists and
    # the level stays completable.
    if num and si < m.nsector:
        walls[start]['lotag'] = 7
        walls[start]['hitag'] = chan
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
    return out


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
KEX_SOUNDS = {
    'alarm': ('sounds/ding02.wav', 0),
    'amb_lav1': ('sounds/mantisact02.wav', 0),
    'amb_lav2': ('sounds/mantisact03.wav', 0),
    'amb_ston': ('sounds/stonemov.wav', 1),
    'ammo': ('sounds/ammo.wav', 0),
    'anu_died': ('sounds/anubisdeath01.wav', 0),
    'anu_fire': ('sounds/anubisproj.wav', 0),
    'anu_hit': ('sounds/anubispain.wav', 0),
    'anu_icu': ('sounds/anubisact.wav', 0),
    'blasted': ('sounds/blasted2.wav', 0),
    'blood_1': ('sounds/flesh01.wav', 0),
    'blood_2': ('sounds/flesh02.wav', 0),
    'blood_3': ('sounds/flesh03.wav', 0),
    'blood_4': ('sounds/fleshbreak.wav', 0),
    'cat_clos': ('sounds/bastetscratch01.wav', 0),
    'cat_deth': ('sounds/bastetdeath01.wav', 0),
    'cat_icu': ('sounds/bastetact02.wav', 0),
    'cat_warp': ('sounds/bastetact01.wav', 0),
    'cat_xpld': ('sounds/bastetatk01.wav', 0),
    'co_fires': ('sounds/cstafffire.wav', 0),
    'co_new1': ('sounds/cstaffup.wav', 0),
    'fishbit2': ('sounds/fishatk.wav', 0),
    'fishdies': ('sounds/fishdeath.wav', 0),
    'ft_fires': ('sounds/fthrowfire.wav', 1),
    'ft_off2': ('sounds/fthrowdown.wav', 0),
    'ft_on2': ('sounds/fthrowup.wav', 0),
    'grn_boom': ('sounds/explode01.wav', 0),
    'grn_toss': ('sounds/bombthrow.wav', 0),
    'hawk_hrt': ('sounds/birdattack.wav', 0),
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
    'lav_deth': ('sounds/mantisrise.wav', 0),
    'lav_fire': ('sounds/mantisatk01.wav', 0),
    'lav_hurt': ('sounds/mantispain.wav', 0),
    'lava_pow': ('sounds/explode02.wav', 0),
    'laz-fire': ('sounds/sentryfire.wav', 0),
    'm_chant0': ('sounds/mumact02.wav', 0),
    'm_deth2': ('sounds/mumdeath01.wav', 0),
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
    'pot_pc1': ('sounds/glass01.wav', 0),
    'pot_pc2': ('sounds/glass02.wav', 0),
    'q_cry': ('sounds/queenroar.wav', 0),
    'q_deth': ('sounds/queendeath01.wav', 0),
    'q_gurgle': ('sounds/queenact02.wav', 0),
    'q_ouch': ('sounds/spidpain.wav', 0),
    'q_spit': ('sounds/queenatk01.wav', 0),
    'q_tail': ('sounds/queenatk04.wav', 0),
    'ra_on': ('sounds/rafire.wav', 0),
    'ra_sprit': ('sounds/setatk01.wav', 0),
    'roch_die': ('sounds/sentrydeath01.wav', 0),
    'roch_gun': ('sounds/sentryfire.wav', 0),
    'roch_wld': ('sounds/sentrypain01.wav', 0),
    'scorp_z2': ('sounds/selkisfizz01.wav', 0),
    'scrp_cla': ('sounds/selkisatk01.wav', 0),
    'scrp_cry': ('sounds/selkisdeath.wav', 0),
    'scrp_hit': ('sounds/selkishurt.wav', 0),
    'scrp_icu': ('sounds/selkisact.wav', 0),
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
    'tick1': ('sounds/timer.wav', 0),
    'tr_fire': ('sounds/fballshoot.wav', 0),
    'vatr_stp': ('sounds/stonestop.wav', 0),
    'wasp_buz': ('sounds/waspact.wav', 0),
    'wasp_die': ('sounds/waspdeath02.wav', 0),
    'wasp_dy1': ('sounds/waspdeath01.wav', 0),
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

    The Kex actor record's angle field does not survive the compile in any form
    this reader could confirm -- the one float in it reads 90 where the text
    source says 0 -- so rather than write down a number that is probably wrong,
    aim at the nearest portal out of the starting sector, falling back to the
    sector's centre. Either way the player opens facing into the level.
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
            si = find_sector(sects, walls, ranges, x, y, -z * SCALE * WORLD)
            if si >= 0:
                # The player's feet: Build PowerSlave adds its own eye height.
                return x, y, sects[si]['floorz'], si, build_angle(a.ang)
    for si,s in enumerate(sects):
        if s and ranges[si][1] >= 3:
            xs = [walls[ranges[si][0]+i]['x'] for i in range(ranges[si][1])]
            ys = [walls[ranges[si][0]+i]['y'] for i in range(ranges[si][1])]
            return sum(xs)//len(xs), sum(ys)//len(ys), s['floorz'], si, None
    return 0,0,0,0,None

def find_sector(sects, walls, ranges, x, y, z=None):
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
    for si,(start,num) in enumerate(ranges):
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

# ---------------------------------------------------------------------------
# Actors
# ---------------------------------------------------------------------------
# Build PowerSlave spawns everything from a map sprite's lotag: ProcessSpriteTag()
# turns 100..118 into creatures and 6..60 into items. So an actor converts to a
# plain sprite with the right lotag and the engine does the rest.
#
# The two games' rosters are cousins, not twins -- the remaster is the console
# version, whose enemies do not all have a Build counterpart -- so this maps what
# genuinely corresponds and leaves the rest out rather than substituting
# something wrong. Decorations (torches, plants, columns, camp props) are skipped
# on purpose: they are pure scenery in Kex and have no Build equivalent at all.
AT = dict(PLAYER=0, ANUBIS=1, BASTET=2, CROW=3, QUEEN=4, MANTIS=5, MUMMY=6,
          MOSQUITO=7, FISH=8, SELKIS=9, SENTRY=10, SET=11, SPIDER=12,
          BUGKEY=29, TIMEKEY=30, XKEY=31, PLANTKEY=32, PYRAMID=33,
          MASK=35, HEALTH=40, AMMO=41, PISTOL=42, M60=43, ANUSBOMB=44,
          FLAMER=45, STAFF=46, RING=47, FLAMEPOT=49, SNAKEPOT=50,
          FULLAMMO=51, FULLHEALTH=52)

# Kex actor type -> (Build lotag, hitag). hitag carries the pickup amount for the
# ammo tags, which is what AddAmmo() reads.
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
    AT['CROW']:     (117, 0),   # no Build crow; the wasp is the closest flier

    AT['AMMO']:      (6, 10),   # speed loader
    AT['FULLAMMO']:  (8, 30),   # M-60 belt
    AT['ANUSBOMB']:  (9, 0),    # grenade
    AT['HEALTH']:    (12, 0),   # berry twig
    AT['FULLHEALTH']:(14, 0),   # cobra venom bowl
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
}

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
            # pot and a sliver of debris.
            defs[nm] = dict(name=nm, anim=field('spawnAnim'),
                            scale=float(field('scale', '1') or 1),
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


# How many frames of an animation are turned into tiles. Fire and water are the
# reason this exists at all -- with only the first frame every flame in the game
# stood still. Capped because Build animates a run of consecutive tiles and the
# run has to stay small enough to be worth the tile numbers.
KEX_ANIM_MAX_FRAMES = 8

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


def _death_blast(Z, names, anim):
    """(radius, damage) of the A_RadialBlast in a death anim, or None."""
    if '/' not in anim: return None
    group, animname = anim.split('/', 1)
    for n in Z.namelist():
        ln = n.lower()
        if not (ln.startswith('sprites/%s/' % group.lower()) and ln.endswith('_anim.txt')):
            continue
        txt = Z.read(n).decode('latin-1').replace('\r', '')
        m = re.search(r'^' + re.escape(animname) + r'\s*\n\{(.*?)\n\}', txt, re.S | re.M)
        if not m: continue
        b = re.search(r'A_RadialBlast\(\s*(\d+)\s*,\s*(\d+)', m.group(1))
        if b: return (int(b.group(1)), int(b.group(2)))
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
        frames = _all_frames(Z, names, a['anim'])
        if not frames: continue
        breakable = a.get('still') and a.get('shootable') and a.get('death')
        # The exploding pots hold a random still frame (randomizeFrames) and
        # keep their shard art in a separate *_shards anim; the blast radius
        # and damage sit in the death anim's A_RadialBlast action.
        explode = (0, 0)
        if not breakable and a.get('shootable') and a.get('death') and a.get('randomize'):
            blast = _death_blast(Z, names, a['death'])
            if blast:
                explode = blast
                shards = _all_frames(Z, names, a['death'].replace('_death', '_shards'))
                frames = frames[:1] + shards
                breakable = len(frames) > 1
        elif a.get('still') and not breakable:
            frames = frames[:1]
        if a.get('randomize') and not breakable:
            frames = frames[:1]

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
        # colours. Build cannot shade part of a wall, so the glow is put back as
        # a real light instead; these are the tiles that should cast one.
        if 'light source' in (a.get('group') or '').lower():
            lights.append(first)
        if len(allrects) > 1 and not a.get('still') and not breakable:
            animdefs.append((first, tile - 1, 3))
        if breakable and len(allrects) > 1:
            # frame 0 is the intact object; the rest are the shards its death
            # anim tosses, one A_TossActor per shard.
            breakables.append((first, a['health'], first + 1, len(allrects) - 1,
                               a.get('radius', 0)) + explode)
    if verbose:
        print("  scenery: %d actor types, %d animated, %d breakable, from the remaster's own sprites"
              % (len(out), len(animdefs), len(breakables)))
    return out, defs, animdefs, breakables, lights


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


def build_sprites(m, sects, walls, ranges, remap, decor=None):
    """Kex actors -> Build sprites carrying the lotag Build PowerSlave expects."""
    out = []
    for a in m.actors:
        ent = ACTOR_MAP.get(a.type)
        dec = (decor or {}).get(a.type)
        if ent is None and dec is None:
            continue
        x, y = a.x, -a.y
        si = find_sector(sects, walls, ranges, x, y, -a.z * SCALE * WORLD)
        if si < 0 or si not in remap:
            continue                       # actor sits in a sector we dropped
        if ent is not None:
            # Gameplay actors keep Build PowerSlave's own lotag spawning, so its
            # AI and its art take over. Enemies overwrite picnum themselves, but
            # an item whose nItemAnimInfo entry has no anim keeps the picnum the
            # map gave it -- so it has to be the real one, or the item draws as
            # whatever tile 1 happens to be.
            lotag, hitag = ent
            pic = ITEM_PICNUM.get(lotag)
            if pic is None and dec is not None:
                pic = dec[0]        # no original tile: the remaster's own sprite
            out.append(dict(x=x * WORLD, y=y * WORLD, z=sects[si]['floorz'],
                            sectnum=remap[si], ang=build_angle(a.ang),
                            lotag=lotag, hitag=hitag, picnum=(pic or 1), shade=0,
                            xrepeat=64, yrepeat=64))
        else:
            tile, scale, fullbright = dec
            # A Build sprite's world height is (tileheight * yrepeat) / 4, and
            # Kex authors at one texel per unit, so the repeat depends only on
            # the actor's scale and the world factor -- not on the tile size.
            rep = max(1, min(255, int(round(4.0 * scale * WORLD))))
            out.append(dict(x=x * WORLD, y=y * WORLD, z=sects[si]['floorz'],
                            sectnum=remap[si], ang=build_angle(a.ang),
                            lotag=0, hitag=0, picnum=tile,
                            shade=(-96 if fullbright else KEX_SHADE_MIN),
                            xrepeat=rep, yrepeat=rep))

    # Water pairing and damage floors, recorded by _flat_sprites(). A record's
    # partner is a SOURCE sector index; its first piece always keeps the source
    # index, so the remap of that index is the Build sector the hitag names.
    for si, lotag, arg in getattr(m, 'kex_flat_sprites', ()):
        if si not in remap:
            continue
        if lotag in (98, 99):
            if arg not in remap:
                continue
            hitag = remap[arg]
        else:
            hitag = arg
        start, num = ranges[si]
        cx = sum(walls[start+i]['x'] for i in range(num)) / max(1, num)
        cy = sum(walls[start+i]['y'] for i in range(num)) / max(1, num)
        out.append(dict(x=int(cx), y=int(cy), z=sects[si]['floorz'],
                        sectnum=remap[si], ang=0,
                        lotag=lotag, hitag=hitag, picnum=1, shade=0,
                        xrepeat=64, yrepeat=64))
    return out


def sector_remap(sects, ranges):
    """Sectors that survived, and old index -> new index for the ones that did."""
    keep = [i for i,s in enumerate(sects) if s is not None and ranges[i][1] >= 3]
    return keep, {old:new for new,old in enumerate(keep)}


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

    # Seam wall links. The two rings match edge for edge in all but one case,
    # so pair them by coordinate; where an edge has no partner the wall simply
    # goes unlinked, which the engine tolerates.
    for up, dn, oldup, olddn in seams:
        if (up, YAX_FLOOR) not in bunch:
            continue
        ust, unum = ranges[oldup]; dst, dnum = ranges[olddn]
        below_edge = {}
        for i in range(dnum):
            w = out_walls[wremap[dst + i]]
            below_edge[(w['x'], w['y'])] = wremap[dst + i]
        for i in range(unum):
            wi = wremap[ust + i]
            w = out_walls[wi]
            wj = below_edge.get((w['x'], w['y']))
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

    b = bytearray()
    # Map version 9 is version 7 with TROR: identical struct layout on disk,
    # but engineLoadBoard then runs yax_update() over it. Maps without a single
    # vertical opening stay at 7 so nothing about them changes.
    b += struct.pack('<i', 9 if nbunches else 7)
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
            w['cstat'], w['picnum'], 0, w['shade'], 0, w['xrepeat'], w['yrepeat'], 0, 0,
            w['lotag'], 0, w['extra'])
    b += struct.pack('<H', len(sprites))
    for s in sprites:
        b += struct.pack('<iiihhbBBBBBbbhhhhhhhhhh',
            s['x'], s['y'], s['z'], 0, s['picnum'], s['shade'], 0, 32, 0,
            s['xrepeat'], s['yrepeat'], 0, 0,
            s['sectnum'], 0, s['ang'], -1, 0, 0, 0, s['lotag'], s['hitag'], 0)
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
              "// Generated by source/exhumed/tools/kexmap.py -- do not edit by hand.\n")

def write_def(path, assignments, animdefs=(), skytiles=(), header=DEF_HEADER):
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

def convert_install(kexdir, outdir, tilebase=6000, verbose=True):
    kpf = os.path.join(kexdir, KPF)
    if not os.path.isfile(kpf):
        raise SystemExit("no %s in %s -- point me at the PowerSlave Exhumed install"
                         % (KPF, kexdir))
    os.makedirs(outdir, exist_ok=True)

    with zipfile.ZipFile(kpf) as z:
        names = [n for n in z.namelist()
                 if n.lower().startswith('maps/') and n.lower().endswith('.map')
                 and '/old/' not in n.lower() and '/testmaps/' not in n.lower()]
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
        used_types = set()
        for m in parsed.values():
            used_types.update(a.type for a in m.actors)
        # Gameplay actors keep Build's own art -- except the few items for which
        # no original picnum could be recovered, which need a tile built from
        # the remaster's sprite to have anything to draw.
        used_types -= {t for t, (lo, _h) in ACTOR_MAP.items() if lo in ITEM_PICNUM
                       or lo >= 100}
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
        wanted.extend(t for t in m.textures if t)
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

    with zipfile.ZipFile(kpf) as z:
        decor, decordefs, decoranims, breakables, lights = build_actor_tiles(
            z, outdir, used_types, kexactors, nexttile, verbose)
    nexttile += len(decordefs)
    animdefs.extend(decoranims)

    done = 0
    for name in sorted(parsed):
        m = parsed[name]
        local = [assign.get(t, tilebase) for t in m.textures]
        sects, walls, ranges = convert(m, tilebase=0, switch_pairs=sw_pairs)
        for w in walls:
            w['picnum'] = local[w['picnum']] if w['picnum'] < len(local) else tilebase
        skytile = assign.get(m.sky)
        for s in sects:
            if s is None: continue
            s['ceilingpicnum'] = local[s['ceilingpicnum']] if s['ceilingpicnum'] < len(local) else tilebase
            s['floorpicnum']   = local[s['floorpicnum']]   if s['floorpicnum']   < len(local) else tilebase
            if s['ceilingsky'] and skytile is not None:
                # Sky sectors reach absurd heights (TOMB's tallest is 15744
                # world units). A ceiling that far off is nearly edge-on at the
                # horizon, where the texture mapper's perspective divide
                # degenerates -- the suspected source of the noise band along
                # the top of the screen. Cap it well above anything reachable.
                cap = s['floorz'] - 2048 * SCALE
                if s['ceilingz'] < cap:
                    s['ceilingz'] = cap
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
        sprites = build_sprites(m, sects, walls, ranges, remap, decor)
        ns, nw, nsp = write_build(os.path.join(outdir, KEX_MAP_PREFIX + name.upper()),
                                  sects, walls, ranges, start, sprites)
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
    write_def(os.path.join(outdir, "exhumed.def"), usable, animdefs, skytiles)

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

    # The last two columns are the radial blast of the exploding pots, in the
    # remaster's own units (A_RadialBlast radius, damage); zero for the plain
    # ones that just break.
    _write_table(outdir, "kexbreak.txt",
                 "tile health shardtile0 shardcount clipdist blastradius blastdamage",
                 [(tile, health, shard0, nshards, _clipdist(radius), bl_r, bl_d)
                  for tile, health, shard0, nshards, radius, bl_r, bl_d in sorted(breakables)],
                 "breakables", verbose)

    # Light sources: tile, then the peak brightening in shade steps and the
    # falloff shift (radius = peak << shift, in Build world units).
    _write_table(outdir, "kexlight.txt", "tile peak shift",
                 [(tile, KEX_LIGHT_PEAK, KEX_LIGHT_SHIFT) for tile in sorted(lights)],
                 "light sources", verbose)

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
    print('      -g "%s" -map %s' % (kpf, sorted(maps)[0].upper() if maps else "LEVEL.MAP"))
    return done


def PSM_from_bytes(blob):
    return locate(blob)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__.strip().split('\n\n')[1])
    convert_install(sys.argv[1], sys.argv[2])
