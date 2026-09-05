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

    import kexseqtool
    waspseq = kexseqtool.read_seq(files['WASP.SEQ'])

    anim_cache = {}
    def anim_frame(anim, frame, direction):
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        fr = anim_cache[anim][frame]
        return fr['rot'][direction] if 'rot' in fr else fr.get('sprites', [])

    def add_actor_frame(seq, anim, frame, direction, tile, chunk_flip=None):
        """Append one composited frame with the KEX actor origin preserved."""
        layers = anim_frame(anim, frame, direction)
        assert layers, '%s frame %d direction %d is empty' % (anim, frame, direction)
        bounds = []
        for group, base, index, ox, oy, _flip in layers:
            rect = kexmap._sprite_table(Z, names, group, base).get(index)
            assert rect is not None, '%s/%s sprite %d is missing' % (group, base, index)
            bounds.append((ox, oy, ox + rect[2], oy + rect[3]))
        x0 = min(v[0] for v in bounds); y0 = min(v[1] for v in bounds)
        x1 = max(v[2] for v in bounds); y1 = max(v[3] for v in bounds)
        seq['fr_base'].append(len(seq['ck_pict']))
        seq['fr_size'].append(1)
        seq['fr_flag'].append(0)
        # seq_ReadSequence subtracts centerx/centery; seq_PlotSequence then
        # applies the opposite offset. Storing the KEX crop centre here keeps
        # its authored (0,0) actor origin fixed as differently sized wings move.
        seq['ck_x'].append(seq['centerx'] + int((x0 + x1) / 2))
        seq['ck_y'].append(seq['centery'] + int((y0 + y1) / 2))
        seq['ck_pict'].append(tile)
        seq['ck_flag'].append((1 if any(layer[5] for layer in layers) else 0)
                              if chunk_flip is None else chunk_flip)

    # The stock WASP.SEQ has a 13-tick attack and a six-tick pain/backoff
    # action. Retail AI_Wasp instead owns distinct four-tick base/chase
    # animations, an eight-tick attack, and executes A_WaspBackoff on the last
    # attack frame. Build an exact action layout on a private contiguous tile
    # run so the native controller can retain those authored state boundaries.
    # The five source indices used only by wasp_death are already emitted as
    # the dedicated 7823..7827 gib tiles and do not belong in this sequence.
    WASP_TILE_BASE = 7870
    wasp_source = {}
    for anim in ('monsters/wasp_base', 'monsters/wasp_chase',
                 'monsters/wasp_attack'):
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame, fr in enumerate(anim_cache[anim]):
            views = fr['rot'][:5]
            for direction, layers in enumerate(views):
                for _group, base, index, _ox, _oy, _flip in layers:
                    if base == 'wasp' and index not in wasp_source:
                        wasp_source[index] = ('wasp', anim, frame, direction)
    assert set(wasp_source) == {1, 2, 3, 4, 5, 6, 7, 8, 9,
                                12, 14, 15, 16, 17, 18}, \
        'wasp movement sprite census changed'
    wasp_tile = {index: WASP_TILE_BASE + ordinal
                 for ordinal, index in enumerate(sorted(wasp_source))}
    mapping.update({wasp_tile[index]: spec for index, spec in wasp_source.items()})

    waspexact = dict(tag=b'DS', centerx=waspseq['centerx'],
                     centery=waspseq['centery'], seq_base=[], seq_size=[],
                     seq_flag=[], fr_base=[], fr_size=[], fr_flag=[], ck_x=[],
                     ck_y=[], ck_pict=[], ck_flag=[],
                     snd_names=[b'WASP_BUZ', b'WASP_STG', b'WASP_HIT'],
                     snd_events=[], trailing=b'')
    wasp_groups = {
        'base': ('monsters/wasp_base', 0, 4),
        'attack': ('monsters/wasp_attack', 9, 8),
        # The controller moves backward for this one frame after attack frame
        # seven, reproducing A_WaspBackoff before entering chase.
        'backoff': ('monsters/wasp_attack', 18, 1),
        'chase': ('monsters/wasp_chase', 27, 4),
    }
    for seq in range(36):
        waspexact['seq_base'].append(len(waspexact['fr_base']))
        waspexact['seq_flag'].append(-1 if seq == 35 else 0)
        if seq in (8, 17, 26):
            waspexact['seq_size'].append(0)
            continue
        group = next((value for value in wasp_groups.values()
                      if value[1] <= seq < value[1] + 8), None)
        if group is None:
            # KEX death gibs and destroys the host immediately. Keep a safe
            # terminal slot for the native run record's unreachable actions.
            waspexact['seq_size'].append(1)
            add_actor_frame(waspexact, 'monsters/wasp_base', 0, 0,
                            wasp_tile[6])
            continue
        anim, base, count = group
        direction = seq - base
        waspexact['seq_size'].append(count)
        source_start = 7 if base == 18 else 0
        for frame in range(count):
            source_frame = source_start + frame
            layers = anim_frame(anim, source_frame, direction)
            assert len(layers) == 1 and layers[0][1] == 'wasp'
            add_actor_frame(waspexact, anim, source_frame, direction,
                            wasp_tile[layers[0][2]])
    waspexact['snd_events'] = [
        (waspexact['seq_base'][27], 0),
        (waspexact['seq_base'][9], 1),
        (waspexact['seq_base'][9] + 3, 2),
    ]
    q3 = '"' * 3
    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'kexwaspseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'WASP.SEQ with the retail base/chase/attack/backoff cadence and\n'
                'callback sounds on private remaster-art tiles (7870+).\n'
                'kexmap.convert_install writes it beside the converted maps.\n' + q3 + '\n\n')
        f.write('KEX_WASP_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict', 'ck_flag',
                  'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, waspexact[k]))
        f.write('}\n')
    print('wrote %s: %d wasp tiles, WASP.SEQ %d chunks' %
          (seqout, len(wasp_source), len(waspexact['ck_pict'])))

    # Sentry timing is also a behavior contract: retail has a one-frame idle,
    # eight-frame chase, twelve-frame pain animation (goto on frame ten), and
    # ten-frame attack firing on frame six. The demo ROACH.SEQ compresses that
    # attack to eight frames and cannot distinguish idle from chase. Sentry
    # frames contain two to four sprite layers and its pain rotations are not
    # perfectly mirror-symmetric, so allocate one tile for every distinct
    # eight-view composite and bake each layer's flip into the PNG. ChunkFlag
    # stays clear to avoid applying that flip a second time at draw time.
    SENTRY_TILE_BASE = 7890
    sentry_anims = ('monsters/sentry_idle', 'monsters/sentry_chase',
                    'monsters/sentry_pain', 'monsters/sentry_attack')
    sentry_composites = {}
    for anim in sentry_anims:
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame, fr in enumerate(anim_cache[anim]):
            for direction, layers in enumerate(fr['rot']):
                signature = tuple(layers)
                sentry_composites.setdefault(signature,
                                             ('sentry', anim, frame, direction))
    assert len(sentry_composites) == 71, 'sentry composite census changed'
    sentry_tile = {signature: SENTRY_TILE_BASE + ordinal
                   for ordinal, signature in enumerate(sentry_composites)}
    mapping.update({sentry_tile[signature]: spec
                    for signature, spec in sentry_composites.items()})

    sentryseq = dict(tag=b'DS', centerx=160, centery=156,
                     seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
                     fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
                     ck_flag=[], snd_names=[b'ROCH_WLD', b'ROCH_GUN'],
                     snd_events=[], trailing=b'')
    sentry_groups = {
        'idle': ('monsters/sentry_idle', 0, 1),
        'chase': ('monsters/sentry_chase', 9, 8),
        'pain': ('monsters/sentry_pain', 18, 12),
        'attack': ('monsters/sentry_attack', 27, 10),
    }
    for seq in range(36):
        sentryseq['seq_base'].append(len(sentryseq['fr_base']))
        sentryseq['seq_flag'].append(-1 if seq == 35 else 0)
        if seq in (8, 17, 26):
            sentryseq['seq_size'].append(0)
            continue
        group = next((value for value in sentry_groups.values()
                      if value[1] <= seq < value[1] + 8), None)
        if group is None:
            sentryseq['seq_size'].append(1)
            signature = tuple(anim_frame('monsters/sentry_idle', 0, 0))
            add_actor_frame(sentryseq, 'monsters/sentry_idle', 0, 0,
                            sentry_tile[signature], chunk_flip=0)
            continue
        anim, base, count = group
        direction = seq - base
        sentryseq['seq_size'].append(count)
        for frame in range(count):
            signature = tuple(anim_frame(anim, frame, direction))
            add_actor_frame(sentryseq, anim, frame, direction,
                            sentry_tile[signature], chunk_flip=0)
            # sentry_attack frame six is the authored muzzle-flash frame.
            if base == 27 and frame == 6:
                sentryseq['fr_flag'][-1] |= 4
    sentryseq['snd_events'] = [
        (sentryseq['seq_base'][18], 0),
        (sentryseq['seq_base'][27] + 6, 1),
    ]
    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'kexsentryseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'ROACH.SEQ for KEX Sentries with exact idle/chase/pain/attack\n'
                'timing and all eight authored composite rotations (7890+).\n'
                'kexmap.convert_install writes it beside converted maps.\n' + q3 + '\n\n')
        f.write('KEX_SENTRY_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict', 'ck_flag',
                  'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, sentryseq[k]))
        f.write('}\n')
    print('wrote %s: %d Sentry composites, ROACH.SEQ %d chunks' %
          (seqout, len(sentry_composites), len(sentryseq['ck_pict'])))

    # Set's native/demo layout stretches the retail leap start (13 -> 15),
    # soul cast (15 -> 18) and death (30 -> 112), and it has no directional
    # one-frame idle group. Compile a dedicated layout whose action groups are
    # exact, while preserving a soul-picture slot for BuildSoul(). Set's body
    # is assembled from up to six 64x64 layers; precompose all eight authored
    # rotations so per-layer source flips and origin offsets survive intact.
    SET_TILE_BASE = 20000
    set_anims = ('monsters/set_idle', 'monsters/set_chase',
                 'monsters/set_melee', 'monsters/set_leap_start',
                 'monsters/set_leap_loop', 'monsters/set_leap_land',
                 'monsters/set_fire_souls', 'monsters/set_pain',
                 'monsters/set_death', 'monsters/set_death_loop')
    set_composites = {}
    for anim in set_anims:
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame, fr in enumerate(anim_cache[anim]):
            views = fr['rot'] if 'rot' in fr else [fr.get('sprites', [])]
            for direction, layers in enumerate(views):
                signature = tuple(layers)
                set_composites.setdefault(signature, ('set', anim, frame, direction))
    assert len(set_composites) == 171, 'Set composite census changed'
    set_tile = {signature: SET_TILE_BASE + ordinal
                for ordinal, signature in enumerate(set_composites)}
    mapping.update({set_tile[signature]: spec
                    for signature, spec in set_composites.items()})

    setseq = dict(tag=b'DS', centerx=160, centery=177,
                  seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
                  fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
                  ck_flag=[],
                  snd_names=[b'K_SETSTP', b'SET_STEP', b'SET_HIT\0',
                             b'SET_JUMP', b'SET_LAND', b'SET_GR2\0',
                             b'GHOST109', b'SET_SPRT', b'RA_SPRIT',
                             b'SET_DETH', b'SET_GR1\0'],
                  snd_events=[], trailing=b'')
    # base: (anim, direction count, frames per view)
    set_groups = {
        0:  ('monsters/set_idle',        8, 1),
        9:  ('monsters/set_chase',       8, 20),
        18: ('monsters/set_melee',       8, 18),
        27: ('monsters/set_leap_start',  8, 13),
        36: ('monsters/set_leap_loop',   8, 1),
        45: ('monsters/set_leap_land',   8, 12),
        54: ('monsters/set_fire_souls',  8, 15),
        63: ('monsters/set_pain',         8, 8),
        72: ('monsters/set_death',        1, 30),
        73: ('monsters/set_death_loop',   1, 1),
    }
    for seq in range(76):
        setseq['seq_base'].append(len(setseq['fr_base']))
        setseq['seq_flag'].append(-1 if seq == 75 else 0)
        group = next(((base,) + value for base, value in set_groups.items()
                      if base <= seq < base + value[1]), None)
        if group is not None:
            base, anim, _dirs, count = group
            direction = seq - base
            setseq['seq_size'].append(count)
            for frame in range(count):
                signature = tuple(anim_frame(anim, frame, direction))
                add_actor_frame(setseq, anim, frame, direction,
                                set_tile[signature], chunk_flip=0)
                if base == 18 and frame in (8, 17):
                    setseq['fr_flag'][-1] |= 0x80
                if base == 54 and frame == 7:
                    setseq['fr_flag'][-1] |= 0x80
                if base == 54 and frame == 9:
                    setseq['fr_flag'][-1] |= 0x04
            continue
        if seq == 74:
            # Native death souls use SET.SEQ's existing, visible soul picture.
            setseq['seq_size'].append(1)
            setseq['fr_base'].append(len(setseq['ck_pict']))
            setseq['fr_size'].append(1); setseq['fr_flag'].append(0)
            setseq['ck_x'].append(155); setseq['ck_y'].append(158)
            setseq['ck_pict'].append(2678); setseq['ck_flag'].append(0)
            continue
        if seq == 75:
            # Safe terminal art for an unreachable native compatibility slot.
            setseq['seq_size'].append(1)
            signature = tuple(anim_frame('monsters/set_death_loop', 0, 0))
            add_actor_frame(setseq, 'monsters/set_death_loop', 0, 0,
                            set_tile[signature], chunk_flip=0)
            continue
        setseq['seq_size'].append(0)

    def set_event(slot, frame, sound):
        setseq['snd_events'].append((setseq['seq_base'][slot] + frame, sound))
    set_event(9, 5, 0); set_event(9, 15, 0)
    set_event(18, 0, 1); set_event(18, 9, 2)
    set_event(27, 9, 3)
    set_event(45, 6, 4)
    set_event(54, 6, 5); set_event(54, 7, 6)
    set_event(63, 3, 2); set_event(63, 7, 10)
    set_event(72, 0, 2); set_event(72, 2, 7)
    set_event(72, 7, 7); set_event(72, 10, 2)
    set_event(72, 12, 7); set_event(72, 17, 7)
    set_event(72, 18, 8); set_event(72, 28, 9)

    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'kexsetseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'SET.SEQ with exact retail idle/chase/melee/leap/soul/pain/death\n'
                'timing and 171 precomposited authored views on tiles 20000+.\n'
                'kexmap.convert_install writes it beside converted maps.\n' + q3 + '\n\n')
        f.write('KEX_SET_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict',
                  'ck_flag', 'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, setseq[k]))
        f.write('}\n')
    print('wrote %s: %d Set composites, SET.SEQ %d chunks' %
          (seqout, len(set_composites), len(setseq['ck_pict'])))

    # Selkis needs a dedicated layout for behavior as well as art. The stock
    # SCORP.SEQ pads the nine-frame melee to eleven ticks and routes death
    # through four unrelated DOS malfunction sequences before a forty-frame
    # spider burst. Retail instead has exact 1/8/6/9/25-frame directional
    # states, three health-dependent single-view malfunction states, and a
    # three-frame explosion loop whose frame-one callback owns the 24-count
    # death timer. Precompose all eight views because the source is layered
    # and its authored west rotations include per-layer flips.
    SELKIS_TILE_BASE = 20200
    selkis_anims = ('monsters/selkis_idle', 'monsters/selkis_chase',
                    'monsters/selkis_shoot', 'monsters/selkis_melee',
                    'monsters/selkis_pain', 'monsters/selkis_wounded',
                    'monsters/selkis_malfunction1',
                    'monsters/selkis_malfunction2',
                    'monsters/selkis_explode', 'monsters/selkis_corpse')
    selkis_composites = {}
    for anim in selkis_anims:
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame, fr in enumerate(anim_cache[anim]):
            views = fr['rot'] if 'rot' in fr else [fr.get('sprites', [])]
            for direction, layers in enumerate(views):
                signature = tuple(layers)
                selkis_composites.setdefault(
                    signature, ('selkis', anim, frame, direction))
    assert len(selkis_composites) == 167, 'Selkis composite census changed'
    selkis_tile = {signature: SELKIS_TILE_BASE + ordinal
                   for ordinal, signature in enumerate(selkis_composites)}
    mapping.update({selkis_tile[signature]: spec
                    for signature, spec in selkis_composites.items()})

    selkisseq = dict(tag=b'DS', centerx=160, centery=159,
                     seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
                     fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
                     ck_flag=[],
                     snd_names=[b'ROCH_GUN', b'SCRP_CLA', b'SLK_AT2\0',
                                b'SCRP_ICU', b'SCRP_HIT', b'SCORP_Z2',
                                b'SCRP_ZP1'],
                     snd_events=[], trailing=b'')
    # base: (animation, direction count, frames per view)
    selkis_groups = {
        0:  ('monsters/selkis_idle',          8, 1),
        9:  ('monsters/selkis_chase',         8, 8),
        18: ('monsters/selkis_shoot',         8, 6),
        27: ('monsters/selkis_melee',         8, 9),
        36: ('monsters/selkis_pain',          8, 25),
        45: ('monsters/selkis_wounded',       1, 40),
        46: ('monsters/selkis_malfunction1',  1, 12),
        47: ('monsters/selkis_malfunction2',  1, 20),
        48: ('monsters/selkis_explode',       1, 3),
        49: ('monsters/selkis_corpse',        1, 1),
    }
    for seq in range(51):
        selkisseq['seq_base'].append(len(selkisseq['fr_base']))
        selkisseq['seq_flag'].append(-1 if seq == 50 else 0)
        group = next(((base,) + value for base, value in selkis_groups.items()
                      if base <= seq < base + value[1]), None)
        if group is not None:
            base, anim, _dirs, count = group
            direction = seq - base
            selkisseq['seq_size'].append(count)
            for frame in range(count):
                signature = tuple(anim_frame(anim, frame, direction))
                add_actor_frame(selkisseq, anim, frame, direction,
                                selkis_tile[signature], chunk_flip=0)
                if base == 18 and frame == 3:
                    selkisseq['fr_flag'][-1] |= 0x84
                if base == 27 and frame in (0, 6):
                    selkisseq['fr_flag'][-1] |= 0x80
            continue
        if seq == 50:
            selkisseq['seq_size'].append(1)
            signature = tuple(anim_frame('monsters/selkis_corpse', 0, 0))
            add_actor_frame(selkisseq, 'monsters/selkis_corpse', 0, 0,
                            selkis_tile[signature], chunk_flip=0)
            continue
        selkisseq['seq_size'].append(0)

    def selkis_event(slot, frame, sound):
        selkisseq['snd_events'].append(
            (selkisseq['seq_base'][slot] + frame, sound))
    selkis_event(18, 3, 0)
    selkis_event(27, 0, 1); selkis_event(27, 2, 2)
    selkis_event(27, 5, 1); selkis_event(27, 8, 1)
    selkis_event(36, 0, 3)
    selkis_event(45, 15, 3)
    selkis_event(46, 7, 4); selkis_event(46, 8, 5)
    selkis_event(47, 16, 6)
    selkis_event(48, 2, 6)

    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'kexselkisseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'SCORP.SEQ with exact retail Selkis idle/chase/shoot/melee/pain/\n'
                'malfunction/explosion/corpse timing and 167 precomposited\n'
                'authored views on tiles 20200+.\n' + q3 + '\n\n')
        f.write('KEX_SELKIS_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size',
                  'seq_flag', 'fr_base', 'fr_size', 'fr_flag', 'ck_x',
                  'ck_y', 'ck_pict', 'ck_flag', 'snd_names', 'snd_events',
                  'trailing'):
            f.write('    %r: %r,\n' % (k, selkisseq[k]))
        f.write('}\n')
    print('wrote %s: %d Selkis composites, SCORP.SEQ %d chunks' %
          (seqout, len(selkis_composites), len(selkisseq['ck_pict'])))

    # The Queen is one retail actor split across four authored object types:
    # the body, the post-death snake, its ten linked tail pieces, and the
    # thrown seedling pod. The demo files stretch and repurpose those states,
    # so compile an exact layout for the native KEX controller. Queen frames
    # are layered and asymmetric; bake every authored rotation into one tile
    # and leave chunk flips clear. Callback-only frames with no source sprite
    # carry safe art here and are hidden by the controller on that same tick.
    QUEEN_TILE_BASE = 20400
    queen_anims = (
        'monsters/queen_idle', 'monsters/queen_base',
        'monsters/queen_chase', 'monsters/queen_chase_charge',
        'monsters/queen_after_charge', 'monsters/queen_flap',
        'monsters/queen_hurl', 'monsters/queen_tail_missile',
        'monsters/queen_pain', 'monsters/queen_death',
        'monsters/queen_snake_idle', 'monsters/queen_snake_chase',
        'monsters/queen_snake_melee', 'monsters/queen_snake_explode',
        'monsters/queen_snake_tail',
        'monsters/queen_snake_tail_explode', 'monsters/seedling_spawn')
    queen_composites = {}
    for anim in queen_anims:
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame, fr in enumerate(anim_cache[anim]):
            views = fr['rot'] if 'rot' in fr else [fr.get('sprites', [])]
            for direction, layers in enumerate(views):
                signature = tuple(layers)
                if not signature:
                    continue
                queen_composites.setdefault(
                    signature, ('queen', anim, frame, direction))
    assert len(queen_composites) == 280, 'Queen composite census changed'
    queen_tile = {signature: QUEEN_TILE_BASE + ordinal
                  for ordinal, signature in enumerate(queen_composites)}
    assert max(queen_tile.values()) == 20679, 'Queen tile range changed'
    mapping.update({queen_tile[signature]: spec
                    for signature, spec in queen_composites.items()})

    queenseq = dict(tag=b'DS', centerx=160, centery=171,
                    seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
                    fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
                    ck_flag=[],
                    snd_names=[b'Q_TAIL\0\0', b'Q_SPIT\0\0', b'REX-BITE',
                               b'Q_GURGLE', b'Q_SIGHT\0', b'Q_DETH\0\0',
                               b'Q_CRY\0\0\0'],
                    snd_events=[], trailing=b'')
    # base: (animation, direction count, exact rendered ticks per view)
    queen_groups = {
        0:   ('monsters/queen_idle',          8, 1),
        9:   ('monsters/queen_base',          8, 10),
        18:  ('monsters/queen_chase',         8, 10),
        27:  ('monsters/queen_chase_charge',  8, 10),
        36:  ('monsters/queen_after_charge',  8, 10),
        45:  ('monsters/queen_flap',          8, 11),
        54:  ('monsters/queen_hurl',          8, 30),
        63:  ('monsters/queen_tail_missile',  8, 16),
        72:  ('monsters/queen_pain',           1, 13),
        73:  ('monsters/queen_death',          1, 62),
        74:  ('monsters/queen_snake_idle',     8, 1),
        83:  ('monsters/queen_snake_chase',    8, 5),
        92:  ('monsters/queen_snake_melee',    8, 1),
        # Twenty-one explosion callbacks hold four ticks each; the final
        # destroy callback is one tick, matching the retail delay directives.
        101: ('monsters/queen_snake_explode',  1, 85),
    }

    def queen_frame(seq, anim, frame, direction, fallback):
        layers = anim_frame(anim, frame, direction)
        if not layers:
            anim, frame, direction = fallback
            layers = anim_frame(anim, frame, direction)
        signature = tuple(layers)
        add_actor_frame(seq, anim, frame, direction,
                        queen_tile[signature], chunk_flip=0)

    for seq in range(104):
        queenseq['seq_base'].append(len(queenseq['fr_base']))
        queenseq['seq_flag'].append(-1 if seq == 103 else 0)
        group = next(((base,) + value for base, value in queen_groups.items()
                      if base <= seq < base + value[1]), None)
        if group is not None:
            base, anim, _dirs, count = group
            direction = seq - base
            queenseq['seq_size'].append(count)
            for tick in range(count):
                source_frame = min(21, tick // 4) if base == 101 else tick
                fallback = (('monsters/queen_snake_idle', 0, 0)
                            if base == 101 else
                            ('monsters/queen_idle', 0, 0))
                queen_frame(queenseq, anim, source_frame, direction, fallback)
                if base == 45 and tick == 1:
                    queenseq['fr_flag'][-1] |= 0x80
                if base == 54 and tick == 12:
                    queenseq['fr_flag'][-1] |= 0x04
                if base == 54 and tick == 16:
                    queenseq['fr_flag'][-1] |= 0x80
                if base == 63 and tick == 7:
                    queenseq['fr_flag'][-1] |= 0x84
                if base == 63 and tick == 8:
                    queenseq['fr_flag'][-1] |= 0x04
                if base == 92 and tick == 0:
                    queenseq['fr_flag'][-1] |= 0x80
                if base == 101 and tick < 84 and tick % 4 == 0:
                    queenseq['fr_flag'][-1] |= 0x80
            continue
        if seq in (102, 103):
            queenseq['seq_size'].append(1)
            queen_frame(queenseq, 'monsters/queen_snake_tail', 0, 0,
                        ('monsters/queen_snake_idle', 0, 0))
            continue
        queenseq['seq_size'].append(0)

    def queen_event(slot, frame, sound):
        queenseq['snd_events'].append(
            (queenseq['seq_base'][slot] + frame, sound))
    for direction in range(8):
        queen_event(45 + direction, 1, 2)
        queen_event(54 + direction, 12, 1)
        queen_event(63 + direction, 7, 0)
    queen_event(72, 0, 3); queen_event(72, 1, 4)
    queen_event(73, 4, 5)
    queen_event(101, 0, 6)

    queeneggseq = dict(tag=b'DS', centerx=160, centery=167,
                       seq_base=[], seq_size=[], seq_flag=[], fr_base=[],
                       fr_size=[], fr_flag=[], ck_x=[], ck_y=[], ck_pict=[],
                       ck_flag=[], snd_names=[], snd_events=[], trailing=b'')
    spawn_signature = tuple(anim_frame('monsters/seedling_spawn', 0, 0))
    for seq, count in enumerate((2, 1)):
        queeneggseq['seq_base'].append(len(queeneggseq['fr_base']))
        queeneggseq['seq_size'].append(count)
        queeneggseq['seq_flag'].append(-1 if seq == 1 else 0)
        for frame in range(count):
            # seedling_spawn frame one is a callback-only empty frame.
            add_actor_frame(queeneggseq, 'monsters/seedling_spawn', 0, 0,
                            queen_tile[spawn_signature], chunk_flip=0)

    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'kexqueenseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'QUEEN.SEQ and QUEENEGG.SEQ with exact retail Queen body,\n'
                'snake, linked-tail, and seedling-pod timing. All 280 visible\n'
                'authored composites occupy tiles 20400..20679.\n' + q3 + '\n\n')
        for symbol, data in (('KEX_QUEEN_SEQ', queenseq),
                             ('KEX_QUEENEGG_SEQ', queeneggseq)):
            f.write('%s = {\n' % symbol)
            for key in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size',
                        'seq_flag', 'fr_base', 'fr_size', 'fr_flag', 'ck_x',
                        'ck_y', 'ck_pict', 'ck_flag', 'snd_names',
                        'snd_events', 'trailing'):
                f.write('    %r: %r,\n' % (key, data[key]))
            f.write('}\n\n')
    print('wrote %s: %d Queen composites, QUEEN.SEQ %d chunks, '
          'QUEENEGG.SEQ %d chunks' %
          (seqout, len(queen_composites), len(queenseq['ck_pict']),
           len(queeneggseq['ck_pict'])))

    # The 99 AI_Bird actors now keep their own authored action cadence. All 36
    # source sprites fit in the private 7600..7635 run: tile 7600+n is source
    # sprite n, including the feather at n=32. Five unique views are stored;
    # directional SEQ slots mirror the remaining three exactly as KEX does.
    BIRD_TILE_BASE = 7600
    bird = {}
    for anim in ('monsters/bird_fly_chase', 'monsters/bird_attack',
                 'monsters/bird_feather', 'monsters/bird_fly_calm'):
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame, fr in enumerate(anim_cache[anim]):
            views = fr['rot'][:5] if 'rot' in fr else [fr.get('sprites', [])]
            for direction, layers in enumerate(views):
                for _group, base, index, _ox, _oy, _flip in layers:
                    if base == 'bird' and index not in bird:
                        bird[index] = ('bird', anim, frame, direction)
    assert set(bird) == set(range(36)), 'bird source tile census changed'
    bird = {BIRD_TILE_BASE + index: spec for index, spec in bird.items()}
    mapping.update(bird)
    final['bird'] = 1.0

    birdseq = dict(tag=b'DS', centerx=160, centery=100, seq_base=[],
                   seq_size=[], seq_flag=[], fr_base=[], fr_size=[],
                   fr_flag=[], ck_x=[], ck_y=[], ck_pict=[], ck_flag=[],
                   snd_names=[b'BIRD_WNG', b'BIRD_WN2', b'BIRD_ATK'],
                   snd_events=[], trailing=b'')
    bird_groups = {
        'calm': ('monsters/bird_fly_calm', 0, 1),
        'attack': ('monsters/bird_attack', 9, 8),
        'retreat': ('monsters/bird_fly_retreat', 18, 12),
        'chase': ('monsters/bird_fly_chase', 27, 6),
    }
    for seq in range(36):
        birdseq['seq_base'].append(len(birdseq['fr_base']))
        birdseq['seq_flag'].append(-1 if seq == 35 else 0)
        if seq in (8, 17, 26):
            birdseq['seq_size'].append(0)
            continue
        group = next((value for value in bird_groups.values()
                      if value[1] <= seq < value[1] + 8), None)
        if group is None:
            birdseq['seq_size'].append(1)
            # The terminal single-view slot is unreachable after KEX's
            # immediate bird death, but carrying the feather there makes the
            # generated file's art census complete and keeps it independently
            # inspectable.
            add_actor_frame(birdseq, 'monsters/bird_feather', 0, 0,
                            BIRD_TILE_BASE + 32)
            continue
        anim, base, count = group
        direction = seq - base
        birdseq['seq_size'].append(count)
        if anim not in anim_cache:
            anim_cache[anim] = kexmap.parse_anim_full(Z, names, anim)
        for frame in range(count):
            # bird_attack's authored delay is eight ticks although its source
            # table contains seven identical visual frames; hold the seventh.
            source_frame = min(frame, len(anim_cache[anim]) - 1)
            layers = anim_frame(anim, source_frame, direction)
            assert len(layers) == 1 and layers[0][1] == 'bird'
            add_actor_frame(birdseq, anim, source_frame, direction,
                            BIRD_TILE_BASE + layers[0][2])
    birdseq['snd_events'] = [
        (birdseq['seq_base'][27] + 1, 0),
        (birdseq['seq_base'][27] + 4, 1),
        (birdseq['seq_base'][9], 2),
    ]
    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kexbirdseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'HAWK.SEQ for the crows: authored calm/chase/attack/retreat timing\n'
                'on all remaster bird sprites (7600+); kexmap.convert_install writes\n'
                'it out with kexseqtool.write_seq_bytes.\n' + q3 + '\n\n')
        f.write('KEX_BIRD_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict', 'ck_flag',
                  'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, birdseq[k]))
        f.write('}\n')
    print('wrote %s: %d bird tiles, HAWK.SEQ %d chunks' %
          (seqout, len(bird), len(birdseq['ck_pict'])))

    # Queen's hatched AI_Seedling also runs through BuildWasp(), but it is not
    # a small Wasp. Give that controller a dedicated optional SEEDLING.SEQ.
    # Keep the action indices the native run record expects (0 and 9), while
    # using the retail actor's exact one-frame chase and six-frame bite rather
    # than stretching those poses across WASP.SEQ's 4/13-frame cadence.
    SEEDLING_TILE_BASE = 7850
    seedling = {}
    for d in range(5):
        # Frames 0..2 and 3..5 of seedling_attack intentionally share their
        # respective retail sprites, so only two attack tiles per view are
        # needed even though the sequence itself retains all six ticks.
        seedling[SEEDLING_TILE_BASE + d] = \
            ('seedling', 'monsters/seedling_chase', 0, d)
        seedling[SEEDLING_TILE_BASE + 5 + d] = \
            ('seedling', 'monsters/seedling_attack', 0, d)
        seedling[SEEDLING_TILE_BASE + 10 + d] = \
            ('seedling', 'monsters/seedling_attack', 3, d)
    mapping.update(seedling)
    final['seedling'] = 1.0

    seedseq = dict(tag=b'DS', centerx=waspseq['centerx'],
                   centery=waspseq['centery'], seq_base=[], seq_size=[],
                   seq_flag=[], fr_base=[], fr_size=[], fr_flag=[], ck_x=[],
                   ck_y=[], ck_pict=[], ck_flag=[], snd_names=[b'SEED_ATK'],
                   snd_events=[], trailing=b'')
    mirror_view = (0, 1, 2, 3, 4, 3, 2, 1)

    for seq in range(31):
        seedseq['seq_base'].append(len(seedseq['fr_base']))
        seedseq['seq_flag'].append(-1 if seq == 30 else 0)
        if seq in (8, 17, 26):
            seedseq['seq_size'].append(0)
            continue

        if 0 <= seq <= 7:
            d = seq
            seedseq['seq_size'].append(1)
            add_actor_frame(seedseq, 'monsters/seedling_chase', 0, d,
                            SEEDLING_TILE_BASE + mirror_view[d])
        elif 9 <= seq <= 16:
            d = seq - 9
            seedseq['seq_size'].append(6)
            for frame in range(6):
                tile = SEEDLING_TILE_BASE + (5 if frame < 3 else 10) + mirror_view[d]
                add_actor_frame(seedseq, 'monsters/seedling_attack', frame, d, tile)
        elif 18 <= seq <= 25:
            # The dedicated KEX controller cannot enter native Wasp pain, but
            # leave this directional action safe for old/corrupt save states.
            d = seq - 18
            seedseq['seq_size'].append(1)
            add_actor_frame(seedseq, 'monsters/seedling_chase', 0, d,
                            SEEDLING_TILE_BASE + mirror_view[d])
        else:
            # Native Wasp's single-view death/gib slots are unreachable because
            # seedling_death gibs and removes the actor immediately.
            seedseq['seq_size'].append(1)
            add_actor_frame(seedseq, 'monsters/seedling_chase', 0, 0,
                            SEEDLING_TILE_BASE)

    # Only the bite belongs on a seedling. Retail executes it on attack frame
    # three, alongside A_MeleeAttack, rather than on WASP.SEQ's sting frame.
    seedseq['snd_events'] = [(seedseq['seq_base'][9] + 3, 0)]
    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'kexseedlingseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'SEEDLING.SEQ for Queen seedlings: native-compatible action slots\n'
                'with the remaster one-frame chase and exact six-frame bite.\n'
                'kexmap.convert_install writes it beside the converted maps.\n' + q3 + '\n\n')
        f.write('KEX_SEEDLING_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict', 'ck_flag',
                  'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, seedseq[k]))
        f.write('}\n')
    print('wrote %s: %d seedling tiles, SEEDLING.SEQ %d chunks'
          % (seqout, len(seedling), len(seedseq['ck_pict'])))

    # The scorpions. The remaster's AI_Scorp is AI_Spider to the letter --
    # same health, speed, radius, anims of the same shape, its leap even
    # calling A_SpiderLeap -- in a scorpion's shell, and Build has no such
    # creature (its SCORP.SEQ is Selkis). So, as the crows: SCORPION.SEQ is
    # SPIDER.SEQ with every tile retargeted to a scorpion tile on 7700+,
    # composited from monsters/scorp_*; spider.cpp animates a scorpion from
    # kSeqScorpion. Unlike the creatures above, ALL the spider's tiles are
    # mapped, not only the ones the demo lacks -- the demo has the whole
    # spider, and none of it is a scorpion.
    SCORP_TILE_BASE = 7700
    SCORP_GROUPS = [
        (8,  8, 'monsters/scorp_base',   None),                    # walk, 6 = 6
        (16, 8, 'monsters/scorp_base',   [0] * 6),                 # idle
        (32, 8, 'monsters/scorp_attack', [0, 2, 4, 6, 8, 10, 12]), # 13 kex into 7
        (24, 8, 'monsters/scorp_leap',   [0]),
        (0,  8, 'monsters/scorp_leap',   [0] * 8),                 # airborne / landing
        (40, 1, 'monsters/scorp_death',  [0] * 5 + [1] * 5 + [2] * 15),
        (41, 1, 'monsters/scorp_death',  [2] * 6),
        (42, 1, 'monsters/scorp_gib01',  [0] * 6),
        (43, 1, 'monsters/scorp_gib02',  [0] * 6),
        (44, 1, 'monsters/scorp_gib03',  [0] * 6),
        (45, 1, 'monsters/scorp_gib04',  [0] * 6),
        (46, 1, 'monsters/scorp_gib02',  [0] * 6),
        (47, 1, 'monsters/scorp_gib03',  [0] * 6),
    ]
    _c, sseqs, sframes, schunks = read_seq(files['SPIDER.SEQ'])
    skin = {}
    for start, ndirs, anim, fmap in SCORP_GROUPS:
        kn = len(kexmap.parse_anim_full(Z, names, anim))
        if not kn:
            print('!! scorp: anim %s not found' % anim)
            continue
        for d in range(min(ndirs, 5)):
            base, size, _f = sseqs[start + d]
            for fi in range(size):
                kf = fmap[fi] if fmap is not None and fi < len(fmap) \
                    else min(kn - 1, fi * kn // max(1, size))
                fbase, fsize, _ff = sframes[base + fi]
                for ci in range(fbase, fbase + fsize):
                    pict = schunks[ci][2]
                    if pict not in skin:
                        skin[pict] = (anim, kf, d if ndirs > 1 else 0)
    scorp_tiles = {}
    for i, pict in enumerate(sorted(skin)):
        anim, kf, d = skin[pict]
        mapping[SCORP_TILE_BASE + i] = ('scorp', anim, kf, d)
        scorp_tiles[pict] = SCORP_TILE_BASE + i
    final['scorp'] = 1.0
    spider = kexseqtool.read_seq(files['SPIDER.SEQ'])
    spider['ck_pict'] = [scorp_tiles.get(p, p) for p in spider['ck_pict']]
    seqout = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kexscorpseq.py')
    with open(seqout, 'w') as f:
        f.write(q3 + 'Generated by kexseqmap_gen.py -- do not edit by hand.\n\n'
                'SCORPION.SEQ for the scorpions: the demo SPIDER.SEQ with its tiles\n'
                'retargeted to the scorpion tiles (7700+); kexmap.convert_install\n'
                'writes it out with kexseqtool.write_seq_bytes.\n' + q3 + '\n\n')
        f.write('KEX_SCORP_SEQ = {\n')
        for k in ('tag', 'centerx', 'centery', 'seq_base', 'seq_size', 'seq_flag',
                  'fr_base', 'fr_size', 'fr_flag', 'ck_x', 'ck_y', 'ck_pict', 'ck_flag',
                  'snd_names', 'snd_events', 'trailing'):
            f.write('    %r: %r,\n' % (k, spider[k]))
        f.write('}\n')
    print('wrote %s: %d scorpion tiles, SCORPION.SEQ %d chunks' % (seqout, len(scorp_tiles), len(spider['ck_pict'])))

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
