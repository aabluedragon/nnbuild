#!/usr/bin/env python3
"""Extract the Nightdive PowerSlave Exhumed menu art into Build tiles.

The remaster's menu is a stone panel over the twin-Anubis backdrop, with its own
gold/blue display font. All of it lives in PowerslaveEX.kpf as PNGs, so the
bridge is the same one the level converter uses: give each piece a fixed tile
number in a DEF and let the Build engine draw the remaster's own art.

The font is the fiddly part. Kex stores it as one atlas plus a .kfont metrics
file (codepoint, x, y, w, h, yoffset), and Build's rotatesprite can only draw a
whole tile -- so each glyph is sliced out of the atlas into its own tile. The
metrics that the game needs at draw time are written back out as kexfont.txt,
which keeps the tool and the C++ side in step without either hardcoding the
other's numbering.
"""
import os, re, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kexpng as png

# Fixed tile numbers, mirrored by KEXTILE_* in source/exhumed/src/kexmenu.cpp.
TILE_BACKDROP   = 7000
TILE_PANEL      = 7001
TILE_CURSOR     = 7002
TILE_CURSOR_R   = 7007      # pre-mirrored: rotatesprite has no x-flip bit
TILE_OVERWORLD  = 7008      # the level-select map
TILE_MARKER     = 7009      # current-level marker on that map
TILE_ARROW_UP   = 7010      # direction indicators toward a linked level
TILE_ARROW_RIGHT= 7011
TILE_ARROW_DOWN = 7012
TILE_ARROW_LEFT = 7013
TILE_KEYCAP     = 7014      # key-cap plate on the hint bar
TILE_BLACK      = 7015      # flat black, for banners and panel borders
TILE_EYE_BASE   = 7020      # the spinning Eye of Ra, one tile per frame (31)
TILE_HUD_HEAD_L = 7060      # the remaster's status bar
TILE_HUD_HEAD_R = 7061
TILE_HUD_BAR_L  = 7062
TILE_HUD_BAR_R  = 7063
TILE_HUD_RED    = 7064
TILE_HUD_BLUE   = 7065
TILE_ROW        = 7003      # menu row, unselected
TILE_ROW_SEL    = 7004      # menu row, selected (red)
TILE_BTN        = 7005      # dialog button, unselected
TILE_BTN_SEL    = 7006      # dialog button, selected
TILE_FONT_BASE  = 7100      # gold body / dark outline: menu entries
TILE_FONT_ALT   = 7300      # the atlas as shipped: blue body / gold outline

# Build's rotatesprite scales a tile uniformly, so anything that is not square
# has to be authored at the exact aspect it is drawn at or it letterboxes. These
# are the sizes kexmenu.cpp draws in Build's 320x200 space; keep the two in step.
ROW_W, ROW_H = 134, 17
BTN_W, BTN_H = 44, 13

# The backdrop is 1920x1080; a Build tile that size is 2 MB of paletted art for
# something that is only ever drawn stretched to the screen, so box-downsample it.
BACKDROP_W, BACKDROP_H = 1024, 576

ART = [
    ('gfx/title_backdrop.png',      TILE_BACKDROP,  (BACKDROP_W, BACKDROP_H)),
    ('gfx/menu/menu_bg.png',        TILE_PANEL,     None),
    ('gfx/menu/cursor.png',         TILE_CURSOR,    None),
]

# The overworld is drawn from the remaster's repainted map (gfx/overworld.dds),
# not the original gfx/overworld_orig.png -- the two are different paintings and
# the reference screens show the repaint. mapInfo.txt's overworld_x/y are still
# in the ORIGINAL map's 640x512 space, so they have to be carried across.
#
# The transform below was measured, not guessed: both maps were reduced to
# luminance and a 4-parameter (offset, scale per axis) search maximised their
# correlation, giving 0.71 -- high for a repaint. Node positions placed through
# it land on the right landmarks.
OW_AX, OW_AW = 0.1800, 0.6450     # original u -> repaint u
OW_AY, OW_AH = -0.0050, 1.0100    # original v -> repaint v
OVERWORLD_W, OVERWORLD_H = 1200, 614

KFONT_LINE = re.compile(r'^\s*(\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)')

def parse_kfont(text):
    """codepoint -> (x, y, w, h, yoffset). Later entries win, as Kex does."""
    out = {}
    inmap = False
    scale = 1.0
    for line in text.replace('\r', '').split('\n'):
        s = line.split('//')[0]
        if s.strip().startswith('scale'):
            try: scale = float(s.split()[1])
            except Exception: pass
        if 'mapchar' in s: inmap = True; continue
        if not inmap: continue
        if '}' in s: break
        m = KFONT_LINE.match(s)
        if m:
            cp, x, y, w, h, yo = (int(v) for v in m.groups())
            out[cp] = (x, y, w, h, yo)
    return out, scale

def extract(kexdir, outdir, verbose=True):
    # Imported here rather than at module scope: kexmap imports this module, so
    # a top-level import of it would be circular.
    from kexmap import KEX_MAP_PREFIX
    kpf = os.path.join(kexdir, 'PowerslaveEX.kpf')
    artdir = os.path.join(outdir, 'kexart')
    os.makedirs(artdir, exist_ok=True)
    lines = []

    owsize = None
    with zipfile.ZipFile(kpf) as z:
        have = {n.lower(): n for n in z.namelist()}

        def grab(name):
            real = have.get(name.lower())
            return z.read(real) if real else None

        dds = grab('gfx/overworld.dds')
        if dds is not None:
            ddspath = os.path.join(artdir, 'overworld.dds')
            open(ddspath, 'wb').write(dds)
            dw, dh, drgba = png.read_dds_dxt1(ddspath)
            drgba = png.downsample(dw, dh, drgba, OVERWORLD_W, OVERWORLD_H)
            png.write(os.path.join(artdir, 'overworld.png'), OVERWORLD_W, OVERWORLD_H, drgba)
            os.remove(ddspath)
            owsize = (OVERWORLD_W, OVERWORLD_H)
            lines.append((TILE_OVERWORLD, 'kexart/overworld.png'))
            if verbose: print("  tile %d  %-30s %dx%d" % (TILE_OVERWORLD, 'overworld.dds', OVERWORLD_W, OVERWORLD_H))

        for name, tile, resize in ART:
            blob = grab(name)
            if blob is None:
                if verbose: print("  missing %s" % name)
                continue
            tmp = os.path.join(artdir, os.path.basename(name))
            open(tmp, 'wb').write(blob)
            w, h, rgba = png.read(tmp)
            if resize and (w, h) != resize:
                rgba = png.downsample(w, h, rgba, *resize); w, h = resize
                png.write(tmp, w, h, rgba)
            lines.append((tile, 'kexart/' + os.path.basename(name)))
            if verbose: print("  tile %d  %-30s %dx%d" % (tile, name, w, h))

        # The row and button plates are the remaster's own button art, resized
        # to the exact rectangle the game draws, and tinted for the selected
        # state the way Kex tints it.
        btn = grab('gfx/menu/menubutton_up.png')
        if btn is not None:
            tmp = os.path.join(artdir, 'menubutton_up.png')
            open(tmp, 'wb').write(btn)
            bw, bh, brgba = png.read(tmp)
            for tile, name, size, rgb in (
                    (TILE_ROW,     'row.png',     (ROW_W, ROW_H), None),
                    (TILE_ROW_SEL, 'row_sel.png', (ROW_W, ROW_H), (255, 70, 70)),
                    (TILE_BTN,     'btn.png',     (BTN_W, BTN_H), (48, 48, 150)),
                    (TILE_BTN_SEL, 'btn_sel.png', (BTN_W, BTN_H), (110, 110, 245))):
                pix = png.resize(bw, bh, brgba, *size)
                if rgb:
                    # The row highlight reads best as the stone stained red
                    # (a multiply); the dialog buttons have to actually look
                    # blue, which needs a blend -- see kexpng.blend().
                    pix = (png.tint(size[0], size[1], pix, *rgb) if tile == TILE_ROW_SEL
                           else png.blend(size[0], size[1], pix, *rgb))
                png.write(os.path.join(artdir, name), size[0], size[1], pix)
                lines.append((tile, 'kexart/' + name))
                if verbose: print("  tile %d  %-30s %dx%d" % (tile, name, size[0], size[1]))

        # rotatesprite has no x-flip bit and the anchor moves under rotation, so
        # the right-hand cursor is authored pre-mirrored rather than flipped at
        # draw time.
        cur = grab('gfx/menu/cursor.png')
        if cur is not None:
            tmp = os.path.join(artdir, 'cursor.png')
            open(tmp, 'wb').write(cur)
            cw, ch, crgba = png.read(tmp)
            # The remaster backs the statue with an opaque black plate, which is
            # what makes it readable against the busy artwork behind the panel.
            for i in range(cw * ch):
                q = i * 4
                a = crgba[q+3]
                for c in range(3):
                    crgba[q+c] = crgba[q+c] * a // 255
                crgba[q+3] = 255
            png.write(tmp, cw, ch, crgba)
            m = bytearray(len(crgba))
            for y in range(ch):
                for x in range(cw):
                    d = (y * cw + x) * 4; sx = (y * cw + (cw - 1 - x)) * 4
                    m[d:d+4] = crgba[sx:sx+4]
            png.write(os.path.join(artdir, 'cursor_r.png'), cw, ch, m)
            lines.append((TILE_CURSOR_R, 'kexart/cursor_r.png'))
            if verbose: print("  tile %d  %-30s %dx%d" % (TILE_CURSOR_R, 'cursor_r.png', cw, ch))

        # The overworld marker is the Eye of Ra, spinning: the remaster ships it
        # as a 31-frame sheet plus a sprite table (index -> rect) and an
        # animation table naming the frame order. Both are parsed rather than
        # eyeballed, so the spin plays in the order Kex plays it. The four
        # direction arrows live on the same sheet.
        eyecount = 0
        spritetxt = grab('sprites/misc/overworld_hd_sprite.txt')
        animtxt = grab('sprites/misc/overworld_hd_anim.txt')
        sheet = grab('sprites/misc/overworld_hd.png')
        if spritetxt is not None: spritetxt = spritetxt.decode('latin-1')
        if animtxt is not None: animtxt = animtxt.decode('latin-1')
        if spritetxt and animtxt and sheet:
            sheetpath = os.path.join(artdir, 'overworld_hd.png')
            open(sheetpath, 'wb').write(sheet)
            ow, oh, orgba = png.read(sheetpath)
            rects = {}
            for m in re.finditer(r'sprite\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', spritetxt):
                i, x, y, w, h = (int(v) for v in m.groups())
                rects[i] = (x, y, w, h)

            def anim_frames(name):
                m = re.search(re.escape(name) + r'\s*\{(.*?)\n\}', animtxt, re.S)
                if not m: return []
                return [int(v) for v in re.findall(r'"[^"]*"\s*,\s*(\d+)', m.group(1))]

            os.makedirs(os.path.join(artdir, 'eye'), exist_ok=True)
            for n, idx in enumerate(anim_frames('mapcursor_hd')):
                r = rects.get(idx)
                if r is None: continue
                x, y, w, h = r
                fn = 'kexart/eye/f%02d.png' % n
                png.write(os.path.join(outdir, fn), w, h, png.crop(ow, oh, orgba, x, y, w, h))
                lines.append((TILE_EYE_BASE + n, fn))
                eyecount = n + 1

            for name, tile in (('maparrow_up_hd', TILE_ARROW_UP),
                               ('maparrow_right_hd', TILE_ARROW_RIGHT),
                               ('maparrow_down_hd', TILE_ARROW_DOWN),
                               ('maparrow_left_hd', TILE_ARROW_LEFT)):
                f = anim_frames(name)
                if not f: continue
                r = rects.get(f[0])
                if r is None: continue
                x, y, w, h = r
                fn = 'kexart/%s.png' % name
                png.write(os.path.join(outdir, fn), w, h, png.crop(ow, oh, orgba, x, y, w, h))
                lines.append((tile, fn))
            if verbose: print("  overworld cursor: %d spin frames + 4 arrows" % eyecount)
            with open(os.path.join(outdir, 'kexanim.txt'), 'w') as f:
                f.write("eyeframes %d\n" % eyecount)

        # Flat plates the layout needs and the remaster does not ship as art:
        # a black square (tiled for banners and the panels' black surround) and
        # the grey key-cap the hint bar puts around "Enter" and "Esc".
        png.write(os.path.join(artdir, 'black.png'), 8, 8, png.solid(8, 8, 0, 0, 0))
        lines.append((TILE_BLACK, 'kexart/black.png'))
        kc = png.solid(26, 14, 96, 96, 96)
        for yy in range(14):
            for xx in range(26):
                if xx == 0 or yy == 0 or xx == 25 or yy == 13:
                    q = (yy * 26 + xx) * 4
                    kc[q:q+3] = bytes((150, 150, 150))
        png.write(os.path.join(artdir, 'keycap.png'), 26, 14, kc)
        lines.append((TILE_KEYCAP, 'kexart/keycap.png'))

        # A marker for where you are on the overworld. The remaster draws its
        # player sprite there; nothing in the .kpf is a clean stand-in, so this
        # is a small gold pip with a dark surround, which reads at map scale.
        mk = 9
        pix = png.solid(mk, mk, 0, 0, 0, 0)
        for yy in range(mk):
            for xx in range(mk):
                d = abs(xx - mk // 2) + abs(yy - mk // 2)
                q = (yy * mk + xx) * 4
                if d <= mk // 2:
                    col = (250, 200, 70) if d <= mk // 2 - 2 else (25, 25, 60)
                    pix[q:q+4] = bytes(col + (255,))
        png.write(os.path.join(artdir, 'marker.png'), mk, mk, pix)
        lines.append((TILE_MARKER, 'kexart/marker.png'))

        # The remaster's status bar: two Anubis heads at the ends, an air bar
        # and a health bar. Its pieces ship separately, so they are laid out at
        # draw time rather than blitted as one strip.
        # Authored at exactly the aspect kexmenu.cpp draws them at. rotatesprite
        # scales uniformly, so a 176x24 bar asked for a 144x14 rectangle draws
        # only 102 wide and leaves a gap -- which is what put black holes in the
        # middle and ends of the bar. HUD_BAR_* / HUD_HEAD_* here are twice the
        # drawn size in Build's 320x200 space.
        HUD_BAR = (288, 28)     # drawn at 144x14
        HUD_HEAD = (32, 40)     # drawn at  16x20
        for name, tile, size in (('gfx/head_left.png',  TILE_HUD_HEAD_L, HUD_HEAD),
                                 ('gfx/head_right.png', TILE_HUD_HEAD_R, HUD_HEAD),
                                 ('gfx/bar_left.png',   TILE_HUD_BAR_L,  HUD_BAR),
                                 ('gfx/bar_right.png',  TILE_HUD_BAR_R,  HUD_BAR)):
            blob = grab(name)
            if blob is None: continue
            fn = 'kexart/' + os.path.basename(name)
            tmp = os.path.join(outdir, fn)
            open(tmp, 'wb').write(blob)
            w_, h_, pix = png.read(tmp)
            if (w_, h_) != size:
                png.write(tmp, size[0], size[1], png.resize(w_, h_, pix, *size))
            lines.append((tile, fn))
        # Flat fills for the bars themselves, tinted the way the remaster tints
        # them: air blue on the left, health red on the right.
        png.write(os.path.join(artdir, 'hud_red.png'), 8, 8, png.solid(8, 8, 196, 24, 24))
        png.write(os.path.join(artdir, 'hud_blue.png'), 8, 8, png.solid(8, 8, 40, 90, 220))
        lines.append((TILE_HUD_RED, 'kexart/hud_red.png'))
        lines.append((TILE_HUD_BLUE, 'kexart/hud_blue.png'))

        kf = grab('fonts/bigfont.kfont')
        atlas = grab('fonts/bigfont.png')
        if kf is None or atlas is None:
            raise SystemExit("no bigfont in %s" % kpf)
        open(os.path.join(artdir, 'bigfont.png'), 'wb').write(atlas)
        glyphs, scale = parse_kfont(kf.decode('latin-1'))

        # bigfont carries letters, digits and three punctuation marks and
        # nothing else -- no '?', no '.', no '!'. smallfont has the full ASCII
        # set, so the gaps are filled from there and scaled to bigfont's cap
        # height. Without this a question mark silently vanishes from
        # "quit the game?".
        skf = grab('fonts/smallfont.kfont')
        satlas = grab('fonts/smallfont.png')
        small, smallscale = ({}, 1.0)
        if skf and satlas:
            open(os.path.join(artdir, 'smallfont.png'), 'wb').write(satlas)
            small, smallscale = parse_kfont(skf.decode('latin-1'))

    aw, ah, argba = png.read(os.path.join(artdir, 'bigfont.png'))
    gold = png.swap_fill_outline(aw, ah, argba)
    sw = sh = 0; sgold = srgba = None
    smallpath = os.path.join(artdir, 'smallfont.png')
    if small and os.path.exists(smallpath):
        sw, sh, srgba = png.read(smallpath)
        # smallfont ships gold already -- the opposite way round from bigfont --
        # so it is the swapped copy that plays the "shipped colours" role here.
        sgold = srgba
        srgba = png.swap_fill_outline(sw, sh, sgold, fill=(90, 120, 240), outline=(245, 190, 90))
    # bigfont is authored at 4x display size and smallfont at 2x, so a smallfont
    # glyph has to be doubled to sit alongside one from bigfont.
    upscale = max(1, int(round((smallscale or 1.0) / (scale or 1.0)))) if small else 1
    os.makedirs(os.path.join(artdir, 'font'), exist_ok=True)
    os.makedirs(os.path.join(artdir, 'fontalt'), exist_ok=True)
    metrics = []
    for cp in range(32, 127):
        g = glyphs.get(cp)
        src = (aw, ah, gold, argba, 1)
        if g is None:
            g = small.get(cp)
            if g is None or srgba is None:
                continue
            src = (sw, sh, sgold, srgba, upscale)
        x, y, w, h, yo = g
        if w <= 0 or h <= 0:
            metrics.append((cp, -1, 0, 0, yo)); continue
        atw, ath, agold, araw, mul = src
        gpix = png.crop(atw, ath, agold, x, y, w, h)
        apix = png.crop(atw, ath, araw,  x, y, w, h)
        if mul > 1:
            gpix = png.resize(w, h, gpix, w * mul, h * mul)
            apix = png.resize(w, h, apix, w * mul, h * mul)
            w, h = w * mul, h * mul
        fn  = 'kexart/font/g%03d.png' % cp
        fna = 'kexart/fontalt/g%03d.png' % cp
        png.write(os.path.join(outdir, fn),  w, h, gpix)
        png.write(os.path.join(outdir, fna), w, h, apix)
        tile = TILE_FONT_BASE + (cp - 32)
        lines.append((tile, fn))
        lines.append((TILE_FONT_ALT + (cp - 32), fna))
        metrics.append((cp, tile, w, h, yo))

    with open(os.path.join(outdir, 'kexmenu.def'), 'w') as f:
        f.write("// VibeBuild32: Nightdive PowerSlave Exhumed menu art, generated by\n")
        f.write("// source/exhumed/tools/kexmenu.py -- do not edit by hand.\n\n")
        for tile, path in lines:
            f.write('tilefromtexture %d { file "%s" }\n' % (tile, path))
            f.write('texture %d { pal 0 { file "%s" } }\n' % (tile, path))

    # Pickup captions, in the remaster's own words. Build PowerSlave already has
    # a SetItemPickupMessage(); this just supplies the remaster's wording for the
    # items that exist in both, keyed by the Build lotag the converter assigns.
    loc_txt = None
    with zipfile.ZipFile(kpf) as z:
        for n in z.namelist():
            if n.lower().endswith('localization/loc_english.txt'):
                loc_txt = z.read(n).decode('latin-1'); break
    if loc_txt:
        strs = dict(re.findall(r'(str_\d+)\s*=\s*"([^"]*)"', loc_txt.replace('\r', '')))
        # Build lotag -> localization key. Only items that mean the same thing in
        # both games; anything ambiguous is left to the original wording.
        PICKUPS = {
            9:  'str_095',   # grenade / Amun bomb
            12: 'str_106',   # small health
            14: 'str_107',   # full health
            23: 'str_101',   # Sobek mask
            28: 'str_093',   # pistol
            29: 'str_094',   # M-60
            30: 'str_096',   # flame thrower
            32: 'str_097',   # cobra staff
            33: 'str_098',   # ring of Ra
            45: 'str_112',   # the four symbols
            46: 'str_113',
            47: 'str_114',
            48: 'str_115',
            11: 'str_111',   # map
        }
        rows = [(lot, strs[k]) for lot, k in sorted(PICKUPS.items()) if k in strs]
        if rows:
            with open(os.path.join(outdir, 'kexpickup.txt'), 'w') as f:
                for lot, txt in rows:
                    f.write("%d %s\n" % (lot, txt))
            if verbose: print("  pickup captions: %d" % len(rows))

    # The overworld: the remaster picks levels off a hand-painted map rather
    # than a list. mapInfo.txt has each level's position on it and how the
    # levels join up; the display names live in the localization table.
    with zipfile.ZipFile(kpf) as z:
        def read(pathname):
            for n in z.namelist():
                if n.lower() == pathname:
                    return z.read(n).decode('latin-1')
            return None
        mapinfo = read('defs/mapinfo.txt')
        loc = read('localization/loc_english.txt')

    if mapinfo:
        # Every level names its own track; the OGGs sit beside the .kpf.
        pairs = []
        for m2 in re.finditer(r'map\s+"maps/([^"]+)".*?musicTrack\s+"([^"]+)"', mapinfo, re.S):
            pairs.append((os.path.basename(m2.group(1)).upper(), m2.group(2)))
        if pairs:
            with open(os.path.join(outdir, 'kexmusic.txt'), 'w') as f:
                for mp, tr in pairs:
                    f.write("%s%s %s\n" % (KEX_MAP_PREFIX, mp, tr))
            if verbose: print("  music: %d levels" % len(pairs))

        names = {}
        if loc:
            for m in re.finditer(r'(str_\d+)\s*=\s*"([^"]*)"', loc):
                names[m.group(1)] = m.group(2)
        blocks = re.findall(r'Map\s+(\w+)\s*\{(.*?)\n\}', mapinfo, re.S)
        order = [b[0] for b in blocks]
        rows = []
        for ident, body in blocks:
            def field(key, default=''):
                m = re.search(key + r'\s+("?)([^"\n]*)\1', body)
                return m.group(2).strip() if m else default
            title = field('title')
            title = names.get(title.lstrip('$'), title.lstrip('$'))
            mapfile = KEX_MAP_PREFIX + os.path.basename(field('map')).upper()
            links = []
            for d in ('north', 'east', 'south', 'west'):
                nxt = field('nextmap_' + d, '')
                links.append(order.index(nxt) if nxt in order else -1)
            # Carried into the repainted map's pixel space here, so the game
            # never has to know the two maps differ.
            ox = float(field('overworld_x', '0')) / 640.0
            oy = float(field('overworld_y', '0')) / 512.0
            w_, h_ = owsize if owsize else (640, 512)
            px = int(round((OW_AX + ox * OW_AW) * w_)) if owsize else int(ox * 640)
            py = int(round((OW_AY + oy * OW_AH) * h_)) if owsize else int(oy * 512)
            rows.append((mapfile, str(px), str(py), title, links))
        with open(os.path.join(outdir, 'kexworld.txt'), 'w') as f:
            f.write("# map x y north east south west title\n")
            for mapfile, x, y, title, links in rows:
                f.write("%s %s %s %d %d %d %d %s\n"
                        % (mapfile, x, y, links[0], links[1], links[2], links[3], title))
        if verbose: print("  overworld: %d levels" % len(rows))

    # The campaign's first level, so "NEW GAME" starts the remaster's campaign
    # rather than the original game's level 1. mapInfo.txt lists the maps in
    # order; the first Map block is the starting one.
    with zipfile.ZipFile(kpf) as z:
        info = None
        for n in z.namelist():
            if n.lower().endswith('defs/mapinfo.txt'):
                info = z.read(n).decode('latin-1'); break
    if info:
        m = re.search(r'map\s+"maps/([^"]+)"', info)
        if m:
            with open(os.path.join(outdir, 'kexstart.txt'), 'w') as f:
                f.write(KEX_MAP_PREFIX + m.group(1).upper() + "\n")
            if verbose: print("  campaign starts at %s" % m.group(1).upper())

    with open(os.path.join(outdir, 'kexfont.txt'), 'w') as f:
        f.write("# codepoint tile width height yoffset   (font scale %g)\n" % scale)
        f.write("scale %g\n" % scale)
        for cp, tile, w, h, yo in metrics:
            f.write("%d %d %d %d %d\n" % (cp, tile, w, h, yo))

    if verbose:
        print("  %d glyphs sliced from bigfont (atlas %dx%d, scale %g)"
              % (sum(1 for m in metrics if m[1] >= 0), aw, ah, scale))
    return len(lines)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit("usage: kexmenu.py <PowerSlave Exhumed dir> <output dir>")
    n = extract(sys.argv[1], sys.argv[2])
    print("wrote %d menu tiles" % n)
