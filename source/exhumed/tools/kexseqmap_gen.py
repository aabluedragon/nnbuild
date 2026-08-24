#!/usr/bin/env python3
"""Generate kexseqtiles.py: which remaster frame fills which stock SEQ tile.

The engine animates creatures through STUFF.DAT's .SEQ files, whose structure
(sub-seq order, trigger flags, sound events) is an engine-side contract -- but
in the shareware demo 389 of the monster tiles those files reference are
zero-size, so wasps, sentries, Set, the queen and Selkis are invisible. Rather
than write new SEQ files, the converter fills the missing tiles on their
ORIGINAL numbers via exhumed.def; this script derives the mapping

    tile -> (creature, remaster anim, frame, view direction 0..4)

by walking each demo SEQ with the sub-seq -> anim tables from KEXSEQ_PLAN.md:
for every frame of every mapped sub-seq, each zero-size chunk tile takes the
remaster frame that plays at that moment, first occurrence winning (held poses
share tiles, exactly the stock economy). Views 5-7 never need art -- the stock
data mirrors them from 3-1 with chunk x-flips, sharing tile numbers.

Usage:
    python3 kexseqmap_gen.py /path/to/powerslave_demo "/path/to/PowerSlave Exhumed"

Regenerate and commit kexseqtiles.py whenever the mapping tables change.
"""
import os
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kexmap


# ---------------------------------------------------------------------------
# Demo data readers
# ---------------------------------------------------------------------------

def grp_files(path):
    d = open(path, 'rb').read()
    assert d[:12] == b'KenSilverman', path
    n = struct.unpack_from('<I', d, 12)[0]
    off = 16 + 16 * n
    out = {}
    for i in range(n):
        nm = d[16+16*i:16+16*i+12].rstrip(b'\0').decode('latin-1')
        sz = struct.unpack_from('<I', d, 16+16*i+12)[0]
        out[nm.upper()] = d[off:off+sz]
        off += sz
    return out


def art_sizes(files):
    """{tile: (w, h)} across every TILES0xx.ART in the GRP."""
    sizes = {}
    for nm, d in files.items():
        if not nm.endswith('.ART'):
            continue
        ver, _num, start, end = struct.unpack_from('<iiii', d, 0)
        cnt = end - start + 1
        xs = struct.unpack_from('<%dh' % cnt, d, 16)
        ys = struct.unpack_from('<%dh' % cnt, d, 16 + 2*cnt)
        for i in range(cnt):
            sizes[start + i] = (xs[i], ys[i])
    return sizes


def read_seq(d):
    """Parse one .SEQ into (center, seqs, frames, chunks); file-local indices.
    seqs = [(base, size, flag)], frames = [(base, size, flag)],
    chunks = [(x, y, pict, flag)]."""
    o = 0
    def rd(n):
        nonlocal o
        v = struct.unpack_from('<%dh' % n, d, o)
        o += 2 * n
        return v
    (tag,) = rd(1)
    assert tag in (0x4849, 0x5344), hex(tag)
    cx, cy, nseq = rd(3)
    sb = rd(nseq); ss = rd(nseq); sf = rd(nseq)
    (nfr,) = rd(1)
    fb = rd(nfr); fs = rd(nfr); ff = rd(nfr)
    (nck,) = rd(1)
    kx = rd(nck); ky = rd(nck); kp = rd(nck); kf = rd(nck)
    return ((cx, cy),
            list(zip(sb, ss, sf)),
            list(zip(fb, fs, ff)),
            list(zip(kx, ky, kp, kf)))


# ---------------------------------------------------------------------------
# The mapping tables -- KEXSEQ_PLAN.md section 2, transcribed.
#
# Each group: (first sub-seq, dir count, remaster anim, framemap) where
# framemap is None (linear demo->kex stretch) or an explicit list giving the
# kex frame for each demo frame of the sub-seq. Dir count 8 walks only dirs
# 0..4 (5..7 share tiles via flips); 1 is a single-view sub-seq.
# ---------------------------------------------------------------------------

CREATURES = {
    'wasp': dict(seq='WASP.SEQ', scale_ref=None, actor_scale=1.25, groups=[
        (0,  8, 'monsters/wasp_base',   None),
        # Build's 13-frame attack around the kex 8: wind-up 0-3, recoil holds
        # on flapB, sting 4-6, follow-through reuses the sting and flap art.
        (9,  8, 'monsters/wasp_attack', [0, 1, 2, 3, 3, 3, 4, 5, 6, 4, 4, 3, 3]),
        (18, 8, 'monsters/wasp_base',   [3, 3, 3, 3, 3, 3]),   # pain: dead code
    ], manual={
        # Death-tumble extras beyond the shared fly tiles, and the splat.
        2457: ('monsters/wasp_base', 0, 0),
        2458: ('monsters/wasp_base', 2, 0),
        2459: ('monsters/wasp_base', 0, 4),
        2460: ('monsters/wasp_gib01', 0, 0),   # splat: wasp_death itself is spriteless
    }),

    'sentry': dict(seq='ROACH.SEQ', scale_ref=(3226, 'monsters/sentry_attack', 6, 0),
                   actor_scale=1.5, groups=[
        (0,  8, 'monsters/sentry_chase',  None),
        (8,  8, 'monsters/sentry_pain',   None),
        # 10 kex frames into 8 Build ones, keeping the kex fire frame (6) on
        # the Build trigger frame (4): take kex 2..9.
        (16, 8, 'monsters/sentry_attack', [2, 3, 4, 5, 6, 7, 8, 9]),
        (24, 8, 'monsters/sentry_idle',   [0, 0, 0, 0, 0]),
        # Death: frames 0-12 are zero tiles painted from the pain anim; the
        # rest (shell, debris, corpse) survive in the demo and stay untouched.
        (32, 1, 'monsters/sentry_pain',   [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11]
                                          + [11] * 25),
    ], manual={}),

    'set': dict(seq='SET.SEQ', scale_ref=None, actor_scale=2.0, groups=[
        (0,  8, 'monsters/set_chase',      None),
        (9,  8, 'monsters/set_melee',      None),
        (18, 8, 'monsters/set_leap_start', [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12]),
        (27, 8, 'monsters/set_leap_loop',  None),
        (36, 8, 'monsters/set_leap_land',  None),
        (45, 8, 'monsters/set_fire_souls', [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 14, 14, 14]),
        (63, 8, 'monsters/set_pain',       None),      # stun: unreachable
        (72, 1, 'monsters/set_death',      None),      # 112 <- 30 stretch
        (74, 1, 'monsters/set_death_loop', None),
        (77, 1, 'monsters/set_idle',       None),      # dormant statue hold
        (78, 1, 'monsters/set_chase',      [0] * 29),  # wake stand-in
        (76, 1, 'monsters/set_death',      [20]),      # gib chunk pic
    ], manual={}),

    'queen': dict(seq='QUEEN.SEQ', scale_ref=None, actor_scale=2.0, groups=[
        (0,  8, 'monsters/queen_base',         None),
        (9,  8, 'monsters/queen_chase',        None),
        (18, 8, 'monsters/queen_tail_missile', None),
        (27, 8, 'monsters/queen_hurl',         None),
        (36, 8, 'monsters/queen_flap',         None),
        (45, 8, 'monsters/queen_pain',         None),  # single-view kex art on all dirs
        (53, 1, 'monsters/queen_pain',         None),
        (54, 1, 'monsters/queen_death',        None),
        (56, 1, 'monsters/queen_snake_idle',   None),
        (57, 1, 'monsters/queen_gib01',        None),
        (58, 1, 'monsters/queen_gib02',        None),
        (59, 1, 'monsters/queen_gib03',        None),
        (60, 1, 'monsters/queen_gib05',        None),
        (74, 8, 'monsters/queen_snake_melee',  None),
        (82, 8, 'monsters/queen_snake_melee',  None),
        (90, 8, 'monsters/queen_snake_chase',  None),
    ], manual={
        # queen.cpp:1400 retextures gibs to kTile3117-3119 + kTile3126; only
        # 3126 is zero in the demo.
        3126: ('monsters/queen_gib01', 0, 0),
    }),

    'queenegg': dict(seq='QUEENEGG.SEQ', scale_ref=None, actor_scale=1.0, groups=[
        (0,  8, 'monsters/seedling_idle',   None),
        (9,  8, 'monsters/seedling_attack', None),
        (18, 1, 'monsters/seedling_spawn',  None),     # only 3147 is zero
        (23, 1, 'monsters/seedling_spawn',  [0]),      # wasp egg stand-in
    ], manual={}),

    'selkis': dict(seq='SCORP.SEQ', scale_ref=None, actor_scale=2.5, groups=[
        (0,  8, 'monsters/selkis_idle',         None),
        (8,  8, 'monsters/selkis_chase',        None),
        (19, 8, 'monsters/selkis_shoot',        None),
        (29, 8, 'monsters/selkis_melee',        None),
        (37, 8, 'monsters/selkis_pain',         None),
        (45, 1, 'monsters/selkis_wounded',      None),
        (46, 1, 'monsters/selkis_malfunction1', None),
        (47, 1, 'monsters/selkis_malfunction2', None),
        (48, 1, 'monsters/selkis_explode',      None),
        (50, 1, 'monsters/selkis_explode',      [i % 3 for i in range(40)]),
    ], manual={}),
}


def composite_dims(Z, names, anim, fi, d):
    """Bounding-box size of one composited view, in remaster pixels."""
    frames = kexmap.parse_anim_full(Z, names, anim)
    if not frames or fi >= len(frames):
        return None
    fr = frames[fi]
    layers = fr['rot'][d] if 'rot' in fr else fr.get('sprites', [])
    rects = []
    for g, base, idx, ox, oy, _flip in layers:
        tbl = kexmap._sprite_table(Z, names, g, base)
        r = tbl.get(idx)
        if r:
            rects.append((r, ox, oy))
    if not rects:
        return None
    x0 = min(ox for _r, ox, oy in rects)
    y0 = min(oy for _r, ox, oy in rects)
    x1 = max(ox + r[2] for r, ox, oy in rects)
    y1 = max(oy + r[3] for r, ox, oy in rects)
    return (x1 - x0, y1 - y0)


def main(demo_dir, kex_dir):
    files = grp_files(os.path.join(demo_dir, 'STUFF.DAT'))
    sizes = art_sizes(files)
    Z = zipfile.ZipFile(os.path.join(kex_dir, kexmap.KPF))
    names = {n.lower(): n for n in Z.namelist()}

    mapping = {}
    scales = {}
    for creature, spec in sorted(CREATURES.items()):
        _center, seqs, frames, chunks = read_seq(files[spec['seq']])
        anim_len = {}
        mapped = 0
        for start, ndirs, anim, fmap in spec['groups']:
            if anim not in anim_len:
                anim_len[anim] = len(kexmap.parse_anim_full(Z, names, anim))
            kn = anim_len[anim]
            if not kn:
                print('!! %s: anim %s not found' % (creature, anim))
                continue
            for d in range(min(ndirs, 5)):
                ss = start + d
                if ss >= len(seqs):
                    print('!! %s: sub-seq %d out of range' % (creature, ss))
                    continue
                base, size, _flag = seqs[ss]
                for fi in range(size):
                    kf = fmap[fi] if fmap is not None and fi < len(fmap) \
                        else min(kn - 1, fi * kn // max(1, size))
                    fbase, fsize, _ff = frames[base + fi]
                    for ci in range(fbase, fbase + fsize):
                        _x, _y, pict, _cf = chunks[ci]
                        if sizes.get(pict, (0, 0)) != (0, 0):
                            continue
                        if pict in mapping:
                            continue
                        mapping[pict] = (creature, anim,
                                         kf, d if ndirs > 1 else 0)
                        mapped += 1
        for tile, (anim, kf, d) in spec['manual'].items():
            if tile not in mapping:
                mapping[tile] = (creature, anim, kf, d)
                mapped += 1

        # Zero tiles this creature's SEQ still references, unmapped (unused
        # sub-seqs: taunts, padding) -- report for the record.
        left = set()
        for base, size, _f in seqs:
            for fi in range(size):
                fbase, fsize, _ff = frames[base + fi]
                for ci in range(fbase, fbase + fsize):
                    pict = chunks[ci][2]
                    if sizes.get(pict, (0, 0)) == (0, 0) and pict not in mapping:
                        left.add(pict)
        print('%-9s mapped %3d tiles, %2d zero tiles left in unused sub-seqs'
              % (creature, mapped, len(left)))

        # Scale: from the reference tile where one survives, else from the
        # calibrated constant scaled by the actors' kex scale ratio.
        ref = spec['scale_ref']
        if ref:
            tile, anim, kf, d = ref
            dims = composite_dims(Z, names, anim, kf, d)
            dw, dh = sizes[tile]
            scales[creature] = (dh / dims[1], spec['actor_scale'])
            print('          scale ref tile %d: demo %dx%d vs kex %dx%d -> %.3f'
                  % (tile, dw, dh, dims[0], dims[1], dh / dims[1]))

    # The reference says the remaster's monster sheets are the ORIGINAL art
    # resolution: sentry tile 3226 vs its kex composite is exactly 1.0, and
    # the surviving Selkis muzzle matches its sheet slice 1:1. An actor's kex
    # `scale` is a renderer multiplier over there, already encoded here in
    # each creature's Build repeat values -- so no rescale, for anyone.
    for px, _asc in scales.values():
        assert abs(px - 1.0) < 0.1, 'reference contradicts 1:1 art (%.3f)' % px
    final = {creature: 1.0 for creature in CREATURES}

    # The crows. Build has no bird AI, so the 99 AI_Bird actors fly as wasps
    # -- but they need not LOOK like wasps. HAWK.SEQ is loaded by the engine
    # (kSeqHawk) and used by nothing, so it becomes a bird-skinned copy of
    # WASP.SEQ: the same sub-seq layout, frames, chunk offsets and flags,
    # with every wasp tile retargeted to a bird tile on 7600+ that the tile
    # pass composites from the remaster's bird sheet. wasp.cpp animates a
    # crow from kSeqHawk instead of kSeqWasp. The sound table takes bird
    # names that KEX_SOUNDS aliases to the remaster's bird wavs.
    BIRD_TILE_BASE = 7600
    BIRD_ANIM = {
        ('monsters/wasp_base',   0): ('monsters/bird_fly_chase', 0),   # wings up
        ('monsters/wasp_base',   2): ('monsters/bird_fly_chase', 3),   # wings down
        ('monsters/wasp_base',   3): ('monsters/bird_fly_chase', 0),
        ('monsters/wasp_attack', 3): ('monsters/bird_attack', 0),
        ('monsters/wasp_attack', 4): ('monsters/bird_attack', 3),
        ('monsters/wasp_attack', 5): ('monsters/bird_attack', 5),
        ('monsters/wasp_gib01',  0): ('monsters/bird_death', 0),
    }
    bird = {}
    for tile, (cr, anim, kf, d) in list(mapping.items()):
        if cr != 'wasp':
            continue
        a2 = BIRD_ANIM.get((anim, kf), ('monsters/bird_fly_chase', 0))
        bird[BIRD_TILE_BASE + (tile - 2432)] = ('bird', a2[0], a2[1], d)
    mapping.update(bird)
    final['bird'] = 1.0

    import kexseqtool
    wasp = kexseqtool.read_seq(files['WASP.SEQ'])
    wasp['ck_pict'] = [BIRD_TILE_BASE + (p - 2432) if 2432 <= p <= 2460 else p
                       for p in wasp['ck_pict']]
    BIRD_SOUND = {b'WASP_BUZ': b'BIRD_ACT', b'WASP_DIE': b'BIRD_DIE', b'WASP_DY1': b'BIRD_DIE',
                  b'WASP_STG': b'BIRD_ATK', b'WASP_WNG': b'BIRD_WNG'}
    wasp['snd_names'] = [BIRD_SOUND.get(n.rstrip(b'\0'), n.rstrip(b'\0')).ljust(8, b'\0')
                        for n in wasp['snd_names']]
    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kexbirdseq.py')
    q3 = '"' * 3
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'HAWK.SEQ for the crows: the demo WASP.SEQ with its tiles retargeted\n'
                'to the bird tiles (7600+) and its sounds renamed; kexmap.convert_install\n'
                'writes it out with kexseqtool.write_seq_bytes.\n' + q3 + '\n\n')
        f.write('KEX_BIRD_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict', 'ck_flag',
                  'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, wasp[k]))
        f.write('}\n')
    print('wrote %s: %d bird tiles, HAWK.SEQ %d chunks' % (seqout, len(bird), len(wasp['ck_pict'])))

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'kexseqtiles.py')
    with open(out, 'w') as f:
        f.write('"""Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'Which remaster anim frame fills which zero-size stock SEQ tile:\n'
                '    tile: (creature, anim, frame, view direction 0..4)\n'
                'and the per-creature pixel scale (demo px per remaster px).\n"""\n\n')
        f.write('KEX_SEQ_SCALE = %r\n\n' % final)
        f.write('KEX_SEQ_TILES = {\n')
        for tile in sorted(mapping):
            f.write('    %d: %r,\n' % (tile, mapping[tile]))
        f.write('}\n')
    print('wrote %s: %d tiles' % (out, len(mapping)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
