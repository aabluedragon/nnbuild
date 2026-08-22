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
KEX_YREPEAT = max(1, min(255, int(round(143.0 / KEX_UNITS_PER_TEXEL))))

def _edges(m, si):
    """The sector's 2D edges, chained into a closed loop.

    Kex stores a sector's wall faces in no particular order -- the loop has to
    be rebuilt by walking end-point to start-point. Faces that share an edge
    (a portal plus the solid pieces above and below it) collapse into one.
    """
    s = m.sectors[si]
    order = []; groups = {}
    for fi in range(s.faceStart, min(s.wallEnd+1, m.nface)):
        f = m.faces[fi]
        a = m.verts[f.vertStart]; b = m.verts[f.vertStart+1]
        key = (round(a[0],2), round(a[1],2), round(b[0],2), round(b[1],2))
        if key[0:2] == key[2:4]:
            continue                      # zero-length edge, nothing to walk
        if key not in groups:
            groups[key] = []; order.append(key)
        groups[key].append(fi)
    if not order:
        return []
    bystart = {}
    for k in order:
        bystart.setdefault(k[0:2], []).append(k)
    loop = []; seen = set()
    cur = order[0]
    while cur is not None and cur not in seen:
        seen.add(cur); loop.append(cur)
        nxt = None
        for cand in bystart.get(cur[2:4], ()):
            if cand not in seen: nxt = cand; break
        cur = nxt
    if len(loop) != len(order):
        # Not a single closed ring (stacked/room-over-room geometry). Keeping a
        # partial loop would emit a broken sector, so drop it.
        return []
    if loop[-1][2:4] != loop[0][0:2]:
        return []
    return [(k, groups[k]) for k in loop]

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


# A slope is kept only if every corner lands within this many Build z units
# (128 = 8 world units); past that a flat surface is the smaller lie.
KEX_SLOPE_TOLERANCE = 128


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
KEX_SHADE_RANGE = 28

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
    return max(0, min(KEX_SHADE_RANGE, int(round((1.0 - lum ** 0.5) * KEX_SHADE_RANGE))))


def _pic(m, f, tilebase):
    if f.tileStart < 0: return tilebase
    return tilebase + m.tiles[f.tileStart][1]

def convert(m, tilebase=4096):
    walls = []; sects = []; ranges = []
    for si in range(m.nsector):
        s = m.sectors[si]
        edges = _edges(m, si)
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
        # floor or ceiling is sloped, start the loop on the wall most
        # perpendicular to the slope -- then the tilt is exact instead of a
        # projection. Rotating the ring is safe: nextwall is matched by
        # coordinate afterwards, not by index.
        cf_p = _plane_build(m, m.faces[min(s.wallEnd+1, m.nface-1)])
        ff_p = _plane_build(m, m.faces[min(s.wallEnd+2, m.nface-1)])
        grad = None
        for pl in (ff_p, cf_p):
            if pl and (abs(pl[3]) > 1e-3 or abs(pl[4]) > 1e-3):
                grad = (pl[3], pl[4]); break
        if grad is not None and len(edges) > 2:
            import math as _m
            gl = _m.hypot(*grad) or 1.0
            best = 0; bestdot = None
            for i2, (k2, _f2) in enumerate(edges):
                dx2, dy2 = k2[2]-k2[0], -(k2[3]-k2[1])
                L2 = _m.hypot(dx2, dy2)
                if L2 < 1e-6: continue
                dot = abs((dx2*grad[0] + dy2*grad[1]) / (L2*gl))
                if bestdot is None or dot < bestdot:
                    bestdot = dot; best = i2
            edges = edges[best:] + edges[:best]

        start = len(walls)
        for i,(k,fis) in enumerate(edges):
            portal = None; pspan = -1.0
            for fi in fis:
                f = m.faces[fi]
                if f.link >= 0:
                    lo,hi = _zspan(m,f)
                    if hi-lo >= pspan: pspan = hi-lo; portal = f
            solid = next((m.faces[fi] for fi in fis if m.faces[fi].link < 0), m.faces[fis[0]])
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
                              shade=_shade(m, solid)))
        ranges.append((start, len(walls)-start))
        cf = m.faces[min(s.wallEnd+1, m.nface-1)]
        ff = m.faces[min(s.wallEnd+2, m.nface-1)]
        clo,chi = _zspan(m,cf); flo,fhi = _zspan(m,ff)
        cz = int(round(-chi*SCALE*WORLD)); fz = int(round(-flo*SCALE*WORLD))
        cheinum = fheinum = 0
        w0 = walls[start]
        w1 = walls[start + 1] if len(walls) - start > 1 else w0
        dxw, dyw = (w1['x'] - w0['x']) * WORLD, (w1['y'] - w0['y']) * WORLD
        wx0, wy0 = w0['x']*WORLD, w0['y']*WORLD
        if cf_p and (abs(cf_p[3]) > 1e-3 or abs(cf_p[4]) > 1e-3):
            z_, h_ = _slope(cf_p, wx0, wy0, dxw, dyw)
            if _slope_error(m, cf, z_, h_, wx0, wy0, dxw, dyw) <= KEX_SLOPE_TOLERANCE:
                cz, cheinum = z_, h_
        if ff_p and (abs(ff_p[3]) > 1e-3 or abs(ff_p[4]) > 1e-3):
            z_, h_ = _slope(ff_p, wx0, wy0, dxw, dyw)
            if _slope_error(m, ff, z_, h_, wx0, wy0, dxw, dyw) <= KEX_SLOPE_TOLERANCE:
                fz, fheinum = z_, h_
        if cz >= fz: cz = fz - 8           # never let the ceiling sit under the floor
        # Kex has no sky tile: the sky is named in the map header and a ceiling
        # simply carries NO tiles where the sky shows through. Untextured
        # ceilings were being given texture slot 0 -- 169 of them in KARNAK --
        # which is why its outdoor areas looked wrong.
        sects.append(dict(wallptr=start, wallnum=len(walls)-start,
                          ceilingsky=(cf.tileStart < 0),
                          ceilingz=cz, floorz=fz,
                          ceilingheinum=cheinum, floorheinum=fheinum,
                          ceilingstat=(2 if cheinum else 0), floorstat=(2 if fheinum else 0),
                          ceilingpicnum=_pic(m,cf,tilebase), floorpicnum=_pic(m,ff,tilebase),
                          ceilingshade=_shade(m,cf), floorshade=_shade(m,ff)))

    # nextwall: the neighbour's wall carrying the same edge reversed
    for si,(start,num) in enumerate(ranges):
        for i in range(num):
            w = walls[start+i]
            ns = w['nextsector']
            if not (0 <= ns < len(ranges)) or ranges[ns][1] == 0:
                w['nextsector'] = -1; continue
            nxt = walls[w['point2']]
            ns_start, ns_num = ranges[ns]
            hit = -1
            for j in range(ns_num):
                o = walls[ns_start+j]; op = walls[o['point2']]
                if (o['x'],o['y']) == (nxt['x'],nxt['y']) and (op['x'],op['y']) == (w['x'],w['y']):
                    hit = ns_start+j; break
            w['nextwall'] = hit
            if hit < 0: w['nextsector'] = -1
    for wi,w in enumerate(walls):          # a portal only counts if both halves agree
        if w['nextwall'] >= 0 and walls[w['nextwall']]['nextwall'] != wi:
            w['nextwall'] = -1; w['nextsector'] = -1
    return sects, walls, ranges

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
            si = find_sector(sects, walls, ranges, x, y)
            if si >= 0:
                # The player's feet: Build PowerSlave adds its own eye height.
                return x, y, sects[si]['floorz'], si, build_angle(a.ang)
    for si,s in enumerate(sects):
        if s and ranges[si][1] >= 3:
            xs = [walls[ranges[si][0]+i]['x'] for i in range(ranges[si][1])]
            ys = [walls[ranges[si][0]+i]['y'] for i in range(ranges[si][1])]
            return sum(xs)//len(xs), sum(ys)//len(ys), s['floorz'], si, None
    return 0,0,0,0,None

def find_sector(sects, walls, ranges, x, y):
    for si,(start,num) in enumerate(ranges):
        if num < 3: continue
        inside=False; j=num-1
        for i in range(num):
            xi,yi = walls[start+i]['x'], walls[start+i]['y']
            xj,yj = walls[start+j]['x'], walls[start+j]['y']
            if (yi>y) != (yj>y) and x < (xj-xi)*(y-yi)/float(yj-yi if yj!=yi else 1)+xi:
                inside = not inside
            j=i
        if inside: return si
    return -1

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
    AT['MANTIS']:   (104, 0),
    AT['SPIDER']:   (105, 0),
    AT['FISH']:     (106, 0),
    AT['SET']:      (111, 0),
    AT['SELKIS']:   (112, 0),   # scorpion
    AT['QUEEN']:    (113, 0),
    AT['MOSQUITO']: (117, 0),   # wasp
    AT['CROW']:     (117, 0),   # no Build crow; the wasp is the closest flier

    AT['AMMO']:      (6, 10),   # speed loader
    AT['FLAMEPOT']:  (7, 20),   # fuel canister
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
    AT['SNAKEPOT']:  (37, 10),  # cobra staff ammo
    AT['BUGKEY']:    (45, 0),   # power key
    AT['TIMEKEY']:   (46, 0),
    AT['XKEY']:      (47, 0),   # war key
    AT['PLANTKEY']:  (48, 0),   # earth key
    AT['PYRAMID']:   (60, 0),   # golden sarcophagus: ends the level
}

def build_sprites(m, sects, walls, ranges, remap):
    """Kex actors -> Build sprites carrying the lotag Build PowerSlave expects."""
    out = []
    for a in m.actors:
        ent = ACTOR_MAP.get(a.type)
        if ent is None:
            continue
        x, y = a.x, -a.y
        si = find_sector(sects, walls, ranges, x, y)
        if si < 0 or si not in remap:
            continue                       # actor sits in a sector we dropped
        lotag, hitag = ent
        out.append(dict(x=x * WORLD, y=y * WORLD, z=sects[si]['floorz'], sectnum=remap[si],
                        ang=build_angle(a.ang), lotag=lotag, hitag=hitag))
    return out


def sector_remap(sects, ranges):
    """Sectors that survived, and old index -> new index for the ones that did."""
    keep = [i for i,s in enumerate(sects) if s is not None and ranges[i][1] >= 3]
    return keep, {old:new for new,old in enumerate(keep)}


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
        if w['nextsector'] in remap and w['nextwall'] in wremap:
            w['nextwall'] = wremap[w['nextwall']]; w['nextsector'] = remap[w['nextsector']]
        else:
            w['nextwall'] = -1; w['nextsector'] = -1
        if w['nextsector'] >= 0: w['cstat'] |= 0x20 if False else 0
    b = bytearray()
    b += struct.pack('<i', 7)
    b += struct.pack('<iii', sx * WORLD, sy * WORLD, sz)   # x/y are world units; only z is 1/16
    b += struct.pack('<hh', sang, remap.get(ss,0))
    b += struct.pack('<H', len(keep))
    for old in keep:
        s = sects[old]; st,num = ranges[old]
        b += struct.pack('<hhiihhhhbBBBhhbBBBBBhhh',
            wremap[st], num, s['ceilingz'], s['floorz'],
            s['ceilingstat'], s['floorstat'],
            s['ceilingpicnum'], s['ceilingheinum'], s['ceilingshade'], 0, 0, 0,
            s['floorpicnum'], s['floorheinum'], s['floorshade'], 0, 0, 0, 0, 0, 0, 0, 0)
    b += struct.pack('<H', len(out_walls))
    for w in out_walls:
        b += struct.pack('<iihhhhhhbBBBBBhhh',
            w['x'] * WORLD, w['y'] * WORLD, w['point2'], w['nextwall'], w['nextsector'],
            w['cstat'], w['picnum'], 0, w['shade'], 0, w['xrepeat'], w['yrepeat'], 0, 0, 0, 0, 0)
    b += struct.pack('<H', len(sprites))
    for s in sprites:
        b += struct.pack('<iiihhbBBBBBbbhhhhhhhhhh',
            s['x'], s['y'], s['z'], 0, 1, 0, 0, 32, 0, 64, 64, 0, 0,
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

def write_def(path, assignments, animdefs=()):
    """One DEF entry per Kex texture.

    tilefromtexture gives the engine a real tile (it needs the dimensions to
    size a wall's texture repeat, and the software renderer needs pixels);
    texture points the hardware renderer at the same PNG so nothing is lost to
    a palette conversion.
    """
    with open(path, 'w') as f:
        f.write("// VibeBuild32: textures from the Nightdive PowerSlave Exhumed remaster.\n")
        f.write("// Generated by source/exhumed/tools/kexmap.py -- do not edit by hand.\n\n")
        for tile, name in sorted(assignments.items()):
            f.write('tilefromtexture %d { file "%s" }\n' % (tile, name))
            f.write('texture %d { pal 0 { file "%s" } }\n' % (tile, name))
        if animdefs:
            f.write("\n// Animated surfaces -- water, slime and the like. Build plays an\n")
            f.write("// animation as a run of consecutive tiles, which is why the frames\n")
            f.write("// are allocated directly after the texture they belong to.\n")
            for first, last, speed in animdefs:
                f.write('animtilerange %d %d %d %d\n' % (first, first, last, max(1, speed)))


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

    assign = {}; nexttile = tilebase
    animdefs = []
    wanted = []
    for name in sorted(parsed):
        m = parsed[name]
        wanted.extend(t for t in m.textures if t)
        if m.sky: wanted.append(m.sky)
    for t in wanted:
        if t in assign: continue
        assign[t] = nexttile; nexttile += 1
        if t in anims:
            speed, frames = anims[t]
            for f in frames:
                assign.setdefault(f, nexttile)   # frames get the tiles that follow
                nexttile += 1
            animdefs.append((assign[t], assign[t] + len(frames), speed))

    done = 0
    for name in sorted(parsed):
        m = parsed[name]
        local = [assign.get(t, tilebase) for t in m.textures]
        sects, walls, ranges = convert(m, tilebase=0)
        for w in walls:
            w['picnum'] = local[w['picnum']] if w['picnum'] < len(local) else tilebase
        skytile = assign.get(m.sky)
        for s in sects:
            if s is None: continue
            s['ceilingpicnum'] = local[s['ceilingpicnum']] if s['ceilingpicnum'] < len(local) else tilebase
            s['floorpicnum']   = local[s['floorpicnum']]   if s['floorpicnum']   < len(local) else tilebase
            if s['ceilingsky'] and skytile is not None:
                s['ceilingpicnum'] = skytile
                s['ceilingstat'] |= 1        # Build's parallaxed sky
        start = player_start(m, sects, walls, ranges)
        _, remap = sector_remap(sects, ranges)
        sprites = build_sprites(m, sects, walls, ranges, remap)
        ns, nw, nsp = write_build(os.path.join(outdir, name.upper()),
                                  sects, walls, ranges, start, sprites)
        portals = sum(1 for w in walls if w['nextsector'] >= 0)
        if verbose:
            print("  %-16s %4d kex sectors -> %4d build sectors, %5d walls, %4d portals, "
                  "%3d/%d actors" % (name, m.nsector, ns, nw, portals, nsp, m.nactor))
        done += 1

    sizes = collect(kpf, list(assign))
    usable = {tile: name for name, tile in assign.items() if name in sizes}
    write_def(os.path.join(outdir, "exhumed.def"), usable, animdefs)

    # The menu art rides along: exhumed.def is the one file the game loads by
    # name, so it pulls in the generated menu definitions.
    print("menu art:")
    kexmenu.extract(kexdir, outdir)
    with open(os.path.join(outdir, "exhumed.def"), "a") as f:
        f.write('\ninclude "kexmenu.def"\n')
    print("converted %d/%d levels into %s" % (done, len(maps), outdir))
    print("%d textures wired up in exhumed.def (%d had no PNG in the .kpf)"
          % (len(usable), len(assign) - len(usable)))
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
