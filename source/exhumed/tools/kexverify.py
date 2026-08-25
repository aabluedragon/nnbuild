#!/usr/bin/env python3
"""Check a converted level against its Kex source.

    kexverify.py <kex install dir> <outdir> [LEVEL ...]

For each level: how far the Build floors and ceilings sit from the Kex
planes at sample points inside every sector (p50 / p90 / p99 / max in Build
z units, and the count over 128), whether every portal is paired both
ways, and whether any sloped sector's first wall rounds to zero length --
the engine draws such a sector flat. Written for the light-gradient cuts,
useful for any change to the geometry side of kexmap.py."""
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
            r = verify(K.locate(z.read(n)), path)
            print("%-12s %4d sectors %5d walls | z error p50 %4.0f p90 %5.0f p99 %5.0f max %5.0f  >128: %4d/%d | unpaired portals %d, zero-length first walls %d"
                  % (base, r['sectors'], r['walls'], r['p50'], r['p90'], r['p99'], r['max'], r['over'], r['samples'], r['unpaired'], r['zero_first']))


if __name__ == '__main__':
    main()
