#!/usr/bin/env python3
"""Check a converted level against its Kex source.

    kexverify.py <kex install dir> <outdir> [LEVEL ...]

For each level: how far the Build floors and ceilings sit from the Kex
planes at sample points inside every sector (p50 / p90 / p99 / max in Build
z units, and the count over 128), whether every portal is paired both
ways, and whether any sloped sector's first wall rounds to zero length --
the engine draws such a sector flat -- and the event audits: every guarded
event with its effect, key doors with key bits, no tag listened on that
nothing fires, no mover sector cut into pieces, every switch on a wall, no
sprite inside a solid sector. Written for the light-gradient cuts, useful
for any change to kexmap.py; each audit caught a real fault once."""
import sys, os, struct, math, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kexmap as K


def _inside(pts, x, y):
    c = False; n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]; x2, y2 = pts[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xi = x1 + (y - y1) * (x2 - x1) / float(y2 - y1)
            if x < xi: c = not c
    return c


def load_build(path):
    d = open(path, 'rb').read(); o = 4 + 4 * 3 + 2 + 2
    ns = struct.unpack_from('<H', d, o)[0]; o += 2
    secs = []
    for i in range(ns):
        wp, wn, cz, fz, cst, fst, cpic, chn, csh, cpal, cxp, cyp, fpic, fhn, fsh = \
            struct.unpack_from('<hhiihhhhbBBBhhb', d, o + 40 * i)
        secs.append(dict(wp=wp, wn=wn, cz=cz, fz=fz, chn=chn, fhn=fhn, cst=cst, fst=fst))
    o += 40 * ns
    nw = struct.unpack_from('<H', d, o)[0]; o += 2
    walls = [struct.unpack_from('<iihhh', d, o + 32 * i) for i in range(nw)]
    for s in secs:
        s['pts'] = [(walls[w][0], walls[w][1]) for w in range(s['wp'], s['wp'] + s['wn'])]
    return secs, walls


def _zat(s, x, y, which):
    """Build's getflorzofslope / getceilzofslope."""
    z = s['fz'] if which == 'f' else s['cz']
    hn = s['fhn'] if which == 'f' else s['chn']
    st = s['fst'] if which == 'f' else s['cst']
    if not hn or not (st & 2) or len(s['pts']) < 2:
        return z
    (wx, wy), (x2, y2) = s['pts'][0], s['pts'][1]
    dx, dy = x2 - wx, y2 - wy; L = math.hypot(dx, dy)
    if L == 0:
        return z
    return z + hn * (dx * (y - wy) - dy * (x - wx)) / (256.0 * L)


def verify(m, path):
    W = K.WORLD
    kex = []
    for si in range(m.nsector):
        try:
            edges = K._edges(m, si)
        except Exception:
            continue
        if len(edges) < 3:
            continue
        sec = m.sectors[si]
        cf = m.faces[min(sec.wallEnd + 1, m.nface - 1)]
        ff = m.faces[min(sec.wallEnd + 2, m.nface - 1)]
        kex.append(([(k[0], k[1]) for k, _ in edges], K._plane_build(m, ff),
                    K._plane_build(m, cf) if cf.tileStart >= 0 else None))
    secs, walls = load_build(path)
    errs = []; zero_first = 0
    for s in secs:
        if len(s['pts']) < 3:
            continue
        if (s['fhn'] or s['chn']) and s['pts'][0] == s['pts'][1]:
            zero_first += 1
        cx = sum(p[0] for p in s['pts']) / len(s['pts']); cy = sum(p[1] for p in s['pts']) / len(s['pts'])
        for (x, y) in [(cx, cy)] + [((cx + p[0]) / 2, (cy + p[1]) / 2) for p in s['pts']]:
            if not _inside(s['pts'], x, y):
                continue
            X, Y = x / float(W), -y / float(W)
            zf, zc = _zat(s, x, y, 'f'), _zat(s, x, y, 'c')
            best = None
            for ring, fp, cp in kex:
                if not _inside(ring, X, Y):
                    continue
                e = abs(K._plane_z(fp, x, y) - zf)
                if cp is not None and not (s['cst'] & 1):
                    e = max(e, abs(K._plane_z(cp, x, y) - zc))
                if best is None or e < best:
                    best = e
            if best is not None:
                errs.append(best)
    unpaired = sum(1 for wi, w in enumerate(walls) if w[3] >= 0 and walls[w[3]][3] != wi)
    errs.sort(); n = len(errs)
    q = lambda f: errs[min(n - 1, int(n * f))] if n else 0
    return dict(sectors=len(secs), walls=len(walls), samples=n, p50=q(.5), p90=q(.9), p99=q(.99),
                max=(errs[-1] if errs else 0), over=sum(1 for e in errs if e > 128),
                unpaired=unpaired, zero_first=zero_first)


# ---------------------------------------------------------------------------
# The event audits: what the converter should have made of each Kex event,
# checked against the written level. Each of these caught a real fault once
# (see EXHUMED_UNIFY.md, "The completability audit" and after).
# ---------------------------------------------------------------------------
GUARDED = {1: 1, 3: 1, 4: 1, 5: 1, 6: 1, 2: 24, 7: 24, 22: 8, 21: 9, 25: 9,
           24: 12, 48: 6, 8: 75, 9: 75, 66: 35}
LISTEN = {2, 7, 8, 9, 21, 22, 24, 25, 49}
FIRE = {23, 50, 70, 53, 200, 201, 202, 203, 66}
BROADCAST = {1, 3, 4, 5, 6, 48}
RULE_TAGS = {'SUNKEN': {1001, 1002, 2, 1007}, 'SELBUROW': {1010, 1004}, 'SETARENA': {1010, 1002},
             'KILARENA': {1010}, 'TOMB': {666, 1020}, 'TOMBEND': {0}, 'SANCTUAR': {1000},
             'KILENTRY': {1000, 1001, 1002, 1003, 1004, 1005}, 'SHRINE': {1001}, 'PEAK': {1005}, 'THOTH': {1003}}
MOVERS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 21, 22, 23, 24, 25, 48, 49, 66, 70, 52, 53, 54, 63, 200, 201, 202, 203}
MARKERS = {73, 74, 81, 64, 65, 66, 67, 68, 69, 71, 77, 82, 94, 95, 96, 98, 99}


def audit(m, path, base):
    """Findings as strings; empty when the level is as it should be."""
    import collections
    out = []
    sects, walls = load_build(path)
    d = open(path, 'rb').read(); o = 4 + 4 * 3 + 2 + 2
    ns = struct.unpack_from('<H', d, o)[0]; o += 2
    tags = [struct.unpack_from('<hh', d, o + 40 * i + 34) for i in range(ns)]
    o += 40 * ns; nw = struct.unpack_from('<H', d, o)[0]
    wlot = [struct.unpack_from('<h', d, o + 2 + 32 * i + 26)[0] for i in range(nw)]
    o += 2 + 32 * nw; nsp = struct.unpack_from('<H', d, o)[0]; o += 2
    sprs = [(struct.unpack_from('<h', d, o + 44 * i + 24)[0],) + struct.unpack_from('<hh', d, o + 44 * i + 38) for i in range(nsp)]

    # Guarded events: the sector (first m.nsector are the sources) carries the expected lotag.
    untagged = collections.Counter()
    for et, es, tag, par in m.events:
        if et in GUARDED and es < ns and tags[es][0] % 1000 != GUARDED[et]:
            untagged[K.KEX_EVENTS.get(et, et)] += 1
    for k, v in sorted(untagged.items()):
        out.append("%d %s event(s) left without their effect" % (v, k))

    # Key doors: the thousands are a bitmask (1 2 4 8), never 3 or 5..7.
    bad = sum(1 for lo, hi in tags if lo % 1000 == 1 and hi >= 1000 and (hi // 1000) not in (1, 2, 4, 8))
    if bad:
        out.append("%d key door(s) with a key number instead of a key bit" % bad)

    # Dangling tags: listened on, fired by nothing the level or a rule provides.
    fired = set(); listened = set()
    for et, es, tag, par in m.events:
        if tag <= 0:                      # 0 is the shared tag 0, -1 no tag
            continue
        if et in FIRE or et in BROADCAST: fired.add(tag)
        if et in LISTEN: listened.add(tag)
    fired |= RULE_TAGS.get(base, set())
    for t in sorted(listened - fired):
        out.append("tag %d is listened on but nothing fires it" % t)

    # Mover sectors cut into pieces (the same Kex source behind several Build sectors).
    # Pieces are appended after the sources; a mover source that spawned any is a fault.
    ec = {si: K._edges(m, si) for si in range(m.nsector)}
    src, pinned = K._split_slopes(m, ec); K._split_light(m, ec, src, pinned)
    pieces = collections.Counter(src)
    cut = sum(1 for et, es, tag, par in m.events if et in MOVERS and pieces[es] > 1)
    if cut:
        out.append("%d mover sector(s) cut into pieces" % cut)

    # Switches: every switch event got at least one lotag-7 wall.
    nsw = sum(1 for e in m.events if 200 <= e[0] <= 203); nwl = sum(1 for l in wlot if l == 7)
    if nsw > nwl:
        out.append("%d switch event(s) but only %d switch wall(s)" % (nsw, nwl))

    # Sprites inside a solid sector (floor on ceiling) that are not markers.
    solid = sum(1 for sect, lo, hi in sprs if 0 <= sect < ns and lo and lo not in MARKERS
                and sects[sect]['fz'] - sects[sect]['cz'] <= 0 and tags[sect][0] % 1000 != 35)   # a bomb wall may hide a pickup
    if solid:
        out.append("%d sprite(s) inside a solid sector" % solid)
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    kexdir, outdir = sys.argv[1], sys.argv[2]
    only = set(a.upper() for a in sys.argv[3:])
    with zipfile.ZipFile(os.path.join(kexdir, K.KPF)) as z:
        names = [n for n in z.namelist() if n.lower().startswith('maps/') and n.lower().endswith('.map')
                 and '/old/' not in n.lower() and '/testmaps/' not in n.lower()]
        for n in sorted(names):
            base = os.path.splitext(os.path.basename(n))[0].upper()
            if only and base not in only:
                continue
            path = os.path.join(outdir, K.KEX_MAP_PREFIX + base + '.MAP') \
                if hasattr(K, 'KEX_MAP_PREFIX') else os.path.join(outdir, 'KEX_' + base + '.MAP')
            if not os.path.exists(path):
                print("%-12s (not converted)" % base); continue
            m = K.locate(z.read(n))
            r = verify(m, path)
            print("%-12s %4d sectors %5d walls | z error p50 %4.0f p90 %5.0f p99 %5.0f max %5.0f  >128: %4d/%d | unpaired portals %d, zero-length first walls %d"
                  % (base, r['sectors'], r['walls'], r['p50'], r['p90'], r['p99'], r['max'], r['over'], r['samples'], r['unpaired'], r['zero_first']))
            for line in audit(m, path, base):
                print("%-12s   ! %s" % (base, line))


if __name__ == '__main__':
    main()
