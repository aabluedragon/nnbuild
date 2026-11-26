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
TILE_KEYCAP_MID = 7016      # stretchable centre of a wide hint key-cap
TILE_OVERWORLD_ORIGINAL = 7017  # alternate original-console overworld map
TILE_KEY_ENTER  = 7018      # native keyboard Enter prompt, including its label
TILE_KEY_ESCAPE = 7019      # native keyboard Escape prompt, including its label
TILE_EYE_BASE   = 7020      # the spinning Eye of Ra, one tile per frame (31)
TILE_KEY_LEFT   = 7058      # native keyboard Left prompt
TILE_KEY_RIGHT  = 7059      # native keyboard Right prompt
TILE_HUD_HEAD_L = 7060      # the remaster's status bar
TILE_HUD_HEAD_R = 7061
TILE_HUD_MAIN_L = 7062      # the in-game frames are main_left/main_right;
TILE_HUD_MAIN_R = 7063      # bar_left/bar_right are the *menu* plates
TILE_HUD_RED    = 7064
TILE_HUD_BLUE   = 7065
TILE_HUD_SCARAB = 7066      # ornament between each head and its frame
TILE_HUD_TROUGH = 7067      # the unfilled part of a bar slot
TILE_HUD_AIR_BACK = 7068    # the underwater air gauge: red disc, blue sphere
TILE_HUD_AIR_FRONT = 7069
TILE_HUD_DOLPHIN = 7070     # the team-doll reward badges, top left
TILE_HUD_VULTURE = 7071
TILE_HUD_SPARK  = 7072      # the jewel's heading spark, cut from hud.png
TILE_HUD_WINGS  = 7073      # widescreen replacement for the scarab ornaments
TILE_HUD_WINGS_R= 7074
# Generic KEX controller prompts for menu footers.  The KPF has separate
# platform-specific sets too; generic is the safe visual fallback when SDL
# cannot identify the physical controller family.
TILE_PAD_A      = 7075
TILE_PAD_B      = 7076
TILE_PAD_LEFT   = 7077
TILE_PAD_RIGHT  = 7078
TILE_DISPLAY_ROW = 7079      # compact KEX Display Settings value row
TILE_DISPLAY_ROW_SEL = 7080  # selected compact KEX Display Settings row
# KEX draws its slider stops as solid UI primitives rather than text glyphs.
# Keep explicit tiles for the pale default and the gold live marker so they
# remain crisp at the remaster's compact control scale.
TILE_SLIDER_DEFAULT = 7081
TILE_SLIDER_CURRENT = 7082
TILE_ROW        = 7003      # menu row, unselected
TILE_ROW_SEL    = 7004      # menu row, selected (the KEX red down state)
TILE_BTN        = 7005      # dialog button, unselected
TILE_BTN_SEL    = 7006      # dialog button, selected
TILE_FONT_BASE  = 7100      # compact gold smallfont: native menu entries
TILE_FONT_ALT   = 7300      # bigfont as shipped: blue body / gold outline
# ASCII occupies the historical contiguous ranges above.  The 26 accented and
# punctuation glyphs used by the remaster's French/German/Spanish UI live in
# the free tail before the creature art begins at 7600.
TILE_FONT_ALT_EXTRA_BASE = 7450
TILE_FONT_EXTRA_BASE     = 7480
# The original-console map has its own 31-frame cursor and four arrows.  This
# tail is free after the extended font but before the creature art at 7600.
TILE_EYE_ORIGINAL_BASE = 7550
TILE_ARROW_ORIGINAL_UP = 7581
TILE_ARROW_ORIGINAL_RIGHT = 7582
TILE_ARROW_ORIGINAL_DOWN = 7583
TILE_ARROW_ORIGINAL_LEFT = 7584
TILE_INV_WEAPON = 7400      # the status screen: eight weapon icons
TILE_INV_KEY    = 7410      # four keys, dim then lit (7410+2*key+lit)
TILE_INV_ARTIFACT = 7420    # six artifacts
TILE_INV_TRANS  = 7430      # eight transmitter pieces, then the lit screen
TILE_INV_TRANS_ON = 7438
TILE_INV_DOLL   = 7440
TILE_INV_MAP_CLOSED = 7441
TILE_INV_MAP_OPEN = 7442
TILE_INV_PLATE_L = 7443     # bar_left / bar_right: the title plates
TILE_INV_PLATE_R = 7444
TILE_INV_ARROW_L = 7445     # page arrows
TILE_INV_ARROW_R = 7446
TILE_INV_SLOT   = 7447      # wheelslot: the plate behind each weapon

# Build's rotatesprite scales a tile uniformly, so anything that is not square
# has to be authored at the exact aspect it is drawn at or it letterboxes. These
# are the measured KEX main-menu plate bounds in Build's 320x200 space; keep the
# generated images and kexmenu.cpp in step.  The native panel is 140 units wide
# (including its ten-unit stone inset), with 120x16 button plates.
ROW_W, ROW_H = 120, 16
BTN_W, BTN_H = 44, 13
# Display Settings uses the very same KEX plate, but its renderer squashes it
# into a short control. Build's rotatesprite deliberately preserves aspect, so
# author that state at its final 120x10 size instead of letterboxing it.
DISPLAY_ROW_W, DISPLAY_ROW_H = 120, 10

# The down texture is an authored brown plate whose native renderer applies a
# warm selection colour.  Preserve the source artwork's shading while applying
# that same modulation; using the unmodified PNG loses KEX's characteristic
# deep-red active row.
SELECT_TINT = (204, 64, 40)

# KEX's title painting is 16:9 on screen.  The renderer stretches the
# 320x200 virtual canvas non-uniformly across our 20:13 presentation, so the
# generated source tile is pre-squashed to compensate: after that stretch it
# is exactly 16:9 instead of the subtly too-tall 1024x576 result.
BACKDROP_W, BACKDROP_H = 1024, 554

ART = [
    ('gfx/title_backdrop.png',      TILE_BACKDROP,  (BACKDROP_W, BACKDROP_H)),
    # The remaster tiles this stone swatch on a 32x32 cell cadence.
    ('gfx/menu/menu_bg.png',        TILE_PANEL,     (32, 32)),
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
OVERWORLD_ORIGINAL_W, OVERWORLD_ORIGINAL_H = 640, 512

# Italian has a text table but no matching Ramses speech or cinematics.  The
# runtime therefore displays this table and deliberately falls back to English
# for its spoken/movie presentation.
UI_LANGUAGES = (('en', 'english'), ('fr', 'french'),
                ('de', 'german'), ('es', 'spanish'), ('it', 'italian'))

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


def crop_opaque(width, height, rgba):
    """Trim transparent atlas padding while preserving the visible sprite."""
    opaque = [i for i in range(width * height) if rgba[i * 4 + 3] != 0]
    if not opaque:
        return width, height, rgba
    left = min(i % width for i in opaque)
    right = max(i % width for i in opaque)
    top = min(i // width for i in opaque)
    bottom = max(i // width for i in opaque)
    cropped_width, cropped_height = right - left + 1, bottom - top + 1
    return (cropped_width, cropped_height,
            png.crop(width, height, rgba, left, top, cropped_width, cropped_height))

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

        localizations = {}
        for code, language in UI_LANGUAGES:
            blob = grab('localization/loc_%s.txt' % language)
            if blob is None:
                continue
            localizations[code] = dict(re.findall(
                r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"',
                blob.decode('utf-8').replace('\r', '')))

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

        # g_useOriginalOverworldScreen is a real remaster option, not a
        # recolour of the HD painting. Preserve its original 640x512 image as
        # a second tile so the native menu can swap the exact source art.
        original = grab('gfx/overworld_orig.png')
        if original is not None:
            original_path = os.path.join(artdir, 'overworld_orig.png')
            open(original_path, 'wb').write(original)
            original_w, original_h, _ = png.read(original_path)
            if (original_w, original_h) != (OVERWORLD_ORIGINAL_W, OVERWORLD_ORIGINAL_H):
                raise ValueError('gfx/overworld_orig.png is %dx%d, expected %dx%d'
                                 % (original_w, original_h, OVERWORLD_ORIGINAL_W,
                                    OVERWORLD_ORIGINAL_H))
            lines.append((TILE_OVERWORLD_ORIGINAL, 'kexart/overworld_orig.png'))
            if verbose: print("  tile %d  %-30s %dx%d" %
                              (TILE_OVERWORLD_ORIGINAL, 'overworld_orig.png',
                               original_w, original_h))

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

        # KEX's ordinary plate and active selection are separate authored
        # states. The latter is its red menubutton_down texture, not a
        # synthetic darkening of the brown up state.
        button_art = {}
        for state in ('up', 'down'):
            source_name = 'gfx/menu/menubutton_%s.png' % state
            blob = grab(source_name)
            if blob is None:
                if verbose: print("  missing %s" % source_name)
                continue
            tmp = os.path.join(artdir, 'menubutton_%s.png' % state)
            open(tmp, 'wb').write(blob)
            button_art[state] = png.read(tmp)

        for tile, name, size, selected in (
                (TILE_ROW,     'row.png',     (ROW_W, ROW_H), False),
                (TILE_ROW_SEL, 'row_sel.png', (ROW_W, ROW_H), True),
                (TILE_DISPLAY_ROW, 'display_row.png',
                 (DISPLAY_ROW_W, DISPLAY_ROW_H), False),
                (TILE_DISPLAY_ROW_SEL, 'display_row_sel.png',
                 (DISPLAY_ROW_W, DISPLAY_ROW_H), True),
                (TILE_BTN,     'btn.png',     (BTN_W, BTN_H), False),
                (TILE_BTN_SEL, 'btn_sel.png', (BTN_W, BTN_H), True)):
            art = button_art.get('down' if selected else 'up')
            if art is None:
                continue
            bw, bh, brgba = art
            # The authored PNG reserves transparent texels on its right edge.
            # KEX sizes the *visible* plate to its widget rectangle, whereas
            # Build scales the complete tile.  Leaving that padding in makes
            # every source-port row visibly narrower (and shifts the visual
            # centre away from its centred label), so crop it before fitting
            # the art to the measured KEX bounds.
            bw, bh, brgba = crop_opaque(bw, bh, brgba)
            pix = png.resize(bw, bh, brgba, *size)
            if selected:
                pix = png.tint(size[0], size[1], pix, *SELECT_TINT)
            png.write(os.path.join(artdir, name), size[0], size[1], pix)
            lines.append((tile, 'kexart/' + name))
            if verbose: print("  tile %d  %-30s %dx%d" % (tile, name, size[0], size[1]))

        # Sliders in the original KEX UI use narrow solid stops: the default
        # position is pale and the adjusted value is warm gold.  They are
        # procedural renderer primitives there, not a PNG in the KPF, so make
        # equally direct pixel tiles here instead of abusing a font character.
        for tile, name, colour in (
                (TILE_SLIDER_DEFAULT, 'slider_default.png', (215, 207, 184, 255)),
                (TILE_SLIDER_CURRENT, 'slider_current.png', (245, 190, 70, 255))):
            width, height = 2, 6
            pix = bytearray(colour * (width * height))
            png.write(os.path.join(artdir, name), width, height, pix)
            lines.append((tile, 'kexart/' + name))
            if verbose: print("  tile %d  %-30s %dx%d" % (tile, name, width, height))

        # rotatesprite has no x-flip bit and the anchor moves under rotation, so
        # the right-hand cursor is authored pre-mirrored rather than flipped at
        # draw time.
        cur = grab('gfx/menu/cursor.png')
        if cur is not None:
            tmp = os.path.join(artdir, 'cursor.png')
            open(tmp, 'wb').write(cur)
            cw, ch, crgba = png.read(tmp)
            # cursor.png already carries the cut-out silhouette authored by
            # the remaster.  Preserve that alpha; forcing transparent pixels
            # opaque turns the cursor bounds into conspicuous black squares.
            m = bytearray(len(crgba))
            for y in range(ch):
                for x in range(cw):
                    d = (y * cw + x) * 4; sx = (y * cw + (cw - 1 - x)) * 4
                    m[d:d+4] = crgba[sx:sx+4]
            png.write(os.path.join(artdir, 'cursor_r.png'), cw, ch, m)
            lines.append((TILE_CURSOR_R, 'kexart/cursor_r.png'))
            if verbose: print("  tile %d  %-30s %dx%d" % (TILE_CURSOR_R, 'cursor_r.png', cw, ch))

        # Both map paintings have their own source cursor sheet. Parse their
        # sprite/animation tables instead of treating the original setting as
        # an HD map behind an HD cursor.
        def extract_overworld_cursor(label, sprite_name, anim_name, sheet_name,
                                     cursor_name, eye_base, arrows):
            spritetxt = grab(sprite_name)
            animtxt = grab(anim_name)
            sheet = grab(sheet_name)
            if spritetxt is None or animtxt is None or sheet is None:
                return 0
            spritetxt = spritetxt.decode('latin-1')
            animtxt = animtxt.decode('latin-1')
            # The original cursor sheet is also named overworld.png. Keep this
            # decoder scratch file distinct from the HD map tile written above.
            sheetpath = os.path.join(artdir, 'cursor_sheet_%s.png' % label)
            open(sheetpath, 'wb').write(sheet)
            ow, oh, orgba = png.read(sheetpath)
            os.remove(sheetpath)
            rects = {}
            for m in re.finditer(r'sprite\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', spritetxt):
                i, x, y, w, h = (int(v) for v in m.groups())
                rects[i] = (x, y, w, h)

            def anim_frames(name):
                m = re.search(re.escape(name) + r'\s*\{(.*?)\n\}', animtxt, re.S)
                if not m: return []
                return [int(v) for v in re.findall(r'"[^"]*"\s*,\s*(\d+)', m.group(1))]

            cursor_dir = os.path.join(artdir, label)
            os.makedirs(cursor_dir, exist_ok=True)
            count = 0
            for n, idx in enumerate(anim_frames(cursor_name)):
                r = rects.get(idx)
                if r is None: continue
                x, y, w, h = r
                fn = 'kexart/%s/f%02d.png' % (label, n)
                png.write(os.path.join(outdir, fn), w, h,
                          png.crop(ow, oh, orgba, x, y, w, h))
                lines.append((eye_base + n, fn))
                count = n + 1

            for name, tile in arrows:
                f = anim_frames(name)
                if not f or f[0] not in rects: continue
                x, y, w, h = rects[f[0]]
                fn = 'kexart/%s.png' % name
                png.write(os.path.join(outdir, fn), w, h,
                          png.crop(ow, oh, orgba, x, y, w, h))
                lines.append((tile, fn))
            if verbose: print("  %s overworld cursor: %d spin frames + 4 arrows" %
                              (label, count))
            return count

        eyecount = extract_overworld_cursor(
            'eye', 'sprites/misc/overworld_hd_sprite.txt',
            'sprites/misc/overworld_hd_anim.txt', 'sprites/misc/overworld_hd.png',
            'mapcursor_hd', TILE_EYE_BASE,
            (('maparrow_up_hd', TILE_ARROW_UP),
             ('maparrow_right_hd', TILE_ARROW_RIGHT),
             ('maparrow_down_hd', TILE_ARROW_DOWN),
             ('maparrow_left_hd', TILE_ARROW_LEFT)))
        original_eyecount = extract_overworld_cursor(
            'eye_orig', 'sprites/misc/overworld_sprite.txt',
            'sprites/misc/overworld_anim.txt', 'sprites/misc/overworld.png',
            'mapcursor', TILE_EYE_ORIGINAL_BASE,
            (('maparrow_up', TILE_ARROW_ORIGINAL_UP),
             ('maparrow_right', TILE_ARROW_ORIGINAL_RIGHT),
             ('maparrow_down', TILE_ARROW_ORIGINAL_DOWN),
             ('maparrow_left', TILE_ARROW_ORIGINAL_LEFT)))
        if eyecount or original_eyecount:
            with open(os.path.join(outdir, 'kexanim.txt'), 'w') as f:
                f.write("eyeframes %d\n" % eyecount)
                f.write("originaleyeframes %d\n" % original_eyecount)

        # Flat plates the layout needs and the remaster does not ship as art:
        # a black square (tiled for banners and the panels' black surround) and
        # the compact dark key-cap the hint bar puts around "Enter" and "Esc".
        png.write(os.path.join(artdir, 'black.png'), 8, 8, png.solid(8, 8, 0, 0, 0))
        lines.append((TILE_BLACK, 'kexart/black.png'))
        kc = png.solid(11, 11, 24, 24, 24)
        km = png.solid(11, 11, 24, 24, 24)
        for yy in range(11):
            for xx in range(11):
                q = (yy * 11 + xx) * 4
                if xx == 0 or yy == 0 or xx == 10 or yy == 10:
                    kc[q:q+3] = bytes((92, 92, 92))
                elif xx == 1 or yy == 1:
                    kc[q:q+3] = bytes((58, 58, 58))
                # The centre tile keeps the cap's top/bottom bevel but omits
                # the vertical outline, allowing LEFT/RIGHT and controller
                # glyph names to span multiple cells without a stretched or
                # visibly repeated border.
                if yy == 0 or yy == 10:
                    km[q:q+3] = bytes((92, 92, 92))
                elif yy == 1:
                    km[q:q+3] = bytes((58, 58, 58))
        png.write(os.path.join(artdir, 'keycap.png'), 11, 11, kc)
        lines.append((TILE_KEYCAP, 'kexart/keycap.png'))
        png.write(os.path.join(artdir, 'keycap_mid.png'), 11, 11, km)
        lines.append((TILE_KEYCAP_MID, 'kexart/keycap_mid.png'))

        # KEX ships the rendered keyboard prompts used by its footer.  They
        # include the correct soft bevel and title-case labels, so use them
        # rather than approximating an Enter/Escape or an arrow pair with the
        # menu font and synthetic keycap geometry.
        for source, tile, filename in (
                ('gfx/controller/keyboard/13.png', TILE_KEY_ENTER, 'key_enter.png'),
                ('gfx/controller/keyboard/27.png', TILE_KEY_ESCAPE, 'key_escape.png'),
                # KEX's platform-agnostic input enum stores the cursor keys
                # after the navigation keys: 278 is Right and 279 is Left.
                ('gfx/controller/keyboard/279.png', TILE_KEY_LEFT, 'key_left.png'),
                ('gfx/controller/keyboard/278.png', TILE_KEY_RIGHT, 'key_right.png')):
            blob = grab(source)
            if blob is None:
                if verbose: print("  missing %s" % source)
                continue
            path = os.path.join(artdir, filename)
            open(path, 'wb').write(blob)
            w, h, _ = png.read(path)
            lines.append((tile, 'kexart/' + filename))
            if verbose: print("  tile %d  %-30s %dx%d" % (tile, source, w, h))

        # Use the remaster's generic controller glyphs rather than placing
        # A/B in a fabricated keyboard cap. f0002 and f0001 are its bottom
        # (accept/A) and right (back/B) face buttons; f0014/f0012 carry the
        # left/right D-pad highlights used by the adjust prompt.
        for source, tile, filename in (
                ('gfx/controller/generic/f0002.png', TILE_PAD_A, 'pad_a.png'),
                ('gfx/controller/generic/f0001.png', TILE_PAD_B, 'pad_b.png'),
                ('gfx/controller/generic/f0014.png', TILE_PAD_LEFT, 'pad_left.png'),
                ('gfx/controller/generic/f0012.png', TILE_PAD_RIGHT, 'pad_right.png')):
            blob = grab(source)
            if blob is None:
                if verbose: print("  missing %s" % source)
                continue
            path = os.path.join(artdir, filename)
            open(path, 'wb').write(blob)
            w, h, _ = png.read(path)
            lines.append((tile, 'kexart/' + filename))
            if verbose: print("  tile %d  %-30s %dx%d" % (tile, source, w, h))

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

        # The remaster's status bar. The in-game frames are main_left /
        # main_right (136x24) -- bar_left / bar_right, which this used to draw,
        # are the *menu* plates and have neither the gauge boxes nor the jewel.
        #
        # kexSlaveHudLocal renders the status bar in a 320x240 canvas, not as
        # a 416-unit strip.  Its two main frames are placed at (24,216) and
        # (160,216), the 40x48 heads at (0,192)/(280,192), and its gauge slots
        # are 86x8 at (54,222)/(196,222).  Build's 320x200 status coordinate
        # system represents the same 4:3 display with 6/5-taller pixels, so
        # the source art keeps its width and is compressed only vertically.
        # Store the converted dimensions directly: KexDrawTile can then draw
        # them 1:1 without uniform-scale letterboxing or horizontal shrinkage.
        HUD_MAIN = (136, 20)    # source 136x24
        HUD_HEAD = (40, 40)     # source  40x48
        HUD_SCAR = (32, 20)     # source  32x24
        for name, tile, size in (('gfx/head_left.png',  TILE_HUD_HEAD_L, HUD_HEAD),
                                 ('gfx/head_right.png', TILE_HUD_HEAD_R, HUD_HEAD),
                                 ('gfx/main_left.png',  TILE_HUD_MAIN_L, HUD_MAIN),
                                 ('gfx/main_right.png', TILE_HUD_MAIN_R, HUD_MAIN),
                                 ('gfx/scarab.png',     TILE_HUD_SCARAB, HUD_SCAR),
                                 # The rest are drawn at native width, 5/6 tall
                                 # (the remaster's 240-line canvas on 200).
                                 ('gfx/airsupply_back.png',  TILE_HUD_AIR_BACK,  (64, 53)),
                                 ('gfx/airsupply_front.png', TILE_HUD_AIR_FRONT, (64, 53)),
                                 ('gfx/dolphin.png',         TILE_HUD_DOLPHIN,   (16, 27)),
                                 ('gfx/vulture.png',         TILE_HUD_VULTURE,   (28, 27)),
                                 ('gfx/wings.png',            TILE_HUD_WINGS,    (96, 20))):
            blob = grab(name)
            if blob is None: continue
            fn = 'kexart/' + os.path.basename(name)
            tmp = os.path.join(outdir, fn)
            open(tmp, 'wb').write(blob)
            w_, h_, pix = png.read(tmp)
            if (w_, h_) != size:
                png.write(tmp, size[0], size[1], png.resize(w_, h_, pix, *size))
            lines.append((tile, fn))
            if name == 'gfx/wings.png':
                # Build has no horizontal-flip bit for rotatesprite.  The
                # right ornament is the mirrored copy used by Kex.
                w_, h_, pix = png.read(tmp)
                mirror = bytearray(w_ * h_ * 4)
                for yy in range(h_):
                    for xx in range(w_):
                        a = (yy * w_ + xx) * 4
                        b = (yy * w_ + (w_ - 1 - xx)) * 4
                        mirror[b:b + 4] = pix[a:a + 4]
                fnr = 'kexart/wings_r.png'
                png.write(os.path.join(outdir, fnr), w_, h_, mirror)
                lines.append((TILE_HUD_WINGS_R, fnr))
        # Flat fills. kexSlaveHudLocal draws each source 86x8 gauge twice: the
        # bright body and then a one-pixel dark lower edge. These go down
        # *under* the frame, whose alpha hole clips their slanted outer ends.
        # The centre jewel's spark: hud.png (the console bar as one atlas)
        # keeps three frames of the jewel in its right column, x 225..256,
        # rows 0-22 / 21-43 / 42-64, a blue spark at the top, the upper right
        # and the right of the jewel; the remaster picks one by the player's
        # heading in 45-degree sectors and mirrors it for the other side -- a
        # compass on the bar. Here the spark itself is cut out (its blue
        # pixels in the first frame) and drawn at eight points around our
        # jewel; the frames' backgrounds are already in main_left/right.
        atlas = grab('gfx/hud.png')
        if atlas is not None:
            tmp = os.path.join(artdir, '_hud.png')
            open(tmp, 'wb').write(atlas)
            aw, ah, apix = png.read(tmp)
            os.remove(tmp)
            pts = [(x, y) for y in range(0, 22) for x in range(225, 256)
                   if apix[(y*aw+x)*4+3] > 128 and apix[(y*aw+x)*4+2] > 150
                   and apix[(y*aw+x)*4+2] > apix[(y*aw+x)*4] + 60]
            if pts:
                x0, y0 = min(p[0] for p in pts), min(p[1] for p in pts)
                x1, y1 = max(p[0] for p in pts) + 1, max(p[1] for p in pts) + 1
                sw, sh = x1 - x0, y1 - y0
                spark = png.blank(sw, sh)
                for (x, y) in pts:
                    spark[((y-y0)*sw + (x-x0))*4:((y-y0)*sw + (x-x0))*4+4] = apix[(y*aw+x)*4:(y*aw+x)*4+4]
                png.write(os.path.join(artdir, 'hud_spark.png'), sw, sh, spark)
                lines.append((TILE_HUD_SPARK, 'kexart/hud_spark.png'))
        def hud_fill(light, shadow):
            pix = png.blank(8, 8)
            for y in range(8):
                rgb = light if y < 7 else shadow
                for x in range(8):
                    q = (y * 8 + x) * 4
                    pix[q:q+4] = bytes(rgb + (255,))
            return pix

        # Exact RGBA values from the remaster's kexSlaveHudLocal draw calls.
        png.write(os.path.join(artdir, 'hud_red.png'), 8, 8,
                  hud_fill((255, 32, 32), (64, 8, 8)))
        png.write(os.path.join(artdir, 'hud_blue.png'), 8, 8,
                  hud_fill((32, 32, 255), (8, 8, 64)))
        png.write(os.path.join(artdir, 'hud_trough.png'), 8, 8, png.solid(8, 8, 96, 72, 48))
        lines.append((TILE_HUD_RED, 'kexart/hud_red.png'))
        lines.append((TILE_HUD_BLUE, 'kexart/hud_blue.png'))
        lines.append((TILE_HUD_TROUGH, 'kexart/hud_trough.png'))

        # The status screen's art, one tile per piece at native size; the
        # screen scales each to fit its cell, so nothing here needs an aspect.
        inv = [('gfx/menu/menuweapon_%d.png' % i, TILE_INV_WEAPON + i) for i in range(8)]
        inv += [('gfx/menu/menukey_%d_%d.png' % (k, lit), TILE_INV_KEY + 2 * k + lit)
                for k in range(4) for lit in range(2)]
        inv += [('gfx/menu/menuartifact_%d.png' % i, TILE_INV_ARTIFACT + i) for i in range(6)]
        inv += [('gfx/menu/menutransmitter_%d.png' % i, TILE_INV_TRANS + i) for i in range(8)]
        inv += [('gfx/menu/menutransmitter_on.png', TILE_INV_TRANS_ON),
                ('gfx/menu/menuteamdoll.png',       TILE_INV_DOLL),
                ('gfx/menu/menumap_closed.png',     TILE_INV_MAP_CLOSED),
                ('gfx/menu/menumap_open.png',       TILE_INV_MAP_OPEN),
                ('gfx/bar_left.png',                TILE_INV_PLATE_L),
                ('gfx/bar_right.png',               TILE_INV_PLATE_R),
                ('gfx/menu/menuarrow_left.png',     TILE_INV_ARROW_L),
                ('gfx/menu/menuarrow_right.png',    TILE_INV_ARROW_R),
                ('gfx/wheelslot.png',               TILE_INV_SLOT)]
        os.makedirs(os.path.join(artdir, 'inv'), exist_ok=True)
        ninv = 0
        for name, tile in inv:
            blob = grab(name)
            if blob is None:
                if verbose: print("  missing %s" % name)
                continue
            fn = 'kexart/inv/' + os.path.basename(name)
            open(os.path.join(outdir, fn), 'wb').write(blob)
            lines.append((tile, fn))
            ninv += 1
        if verbose: print("  status screen: %d pieces" % ninv)

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
    # Keep the established ASCII tile IDs intact, then add exactly the
    # non-ASCII codepoints that the four supported UI tables require.
    codepoints = set(range(32, 127))
    for strings in localizations.values():
        for text in strings.values():
            # Some localization-table entries belong to Kex UI screens that
            # Build does not implement (for example German m_loading uses an
            # ellipsis the shipped font has no glyph for).  Only slice glyphs
            # the remaster's font actually provides; the verifier separately
            # requires every emitted Build UI string to be covered.
            codepoints.update(ord(ch) for ch in text
                              if ord(ch) >= 32 and (ord(ch) in glyphs or ord(ch) in small))
    extra = {cp: i for i, cp in enumerate(sorted(codepoints - set(range(32, 127))))}
    metrics = []
    for cp in sorted(codepoints):
        # KEX presents ordinary menu labels with smallfont, while title prompts
        # use the larger blue-and-gold bigfont.  Keep each image at its authored
        # metric size; kexmenu.cpp reads both metric sets from kexfont.txt.
        sg = small.get(cp)
        bg = glyphs.get(cp)

        if sg is not None and sgold is not None:
            sx, sy, menu_w, menu_h, menu_yoff = sg
            if menu_w <= 0 or menu_h <= 0:
                menu_pix = bytearray()
            else:
                menu_pix = png.crop(sw, sh, sgold, sx, sy, menu_w, menu_h)
                if upscale > 1:
                    menu_pix = png.resize(menu_w, menu_h, menu_pix,
                                          menu_w * upscale, menu_h * upscale)
                    menu_w *= upscale; menu_h *= upscale; menu_yoff *= upscale
        elif bg is not None:
            bx, by, menu_w, menu_h, menu_yoff = bg
            menu_pix = png.crop(aw, ah, gold, bx, by, menu_w, menu_h)
        else:
            continue

        if bg is not None:
            bx, by, title_w, title_h, title_yoff = bg
            title_pix = png.crop(aw, ah, argba, bx, by, title_w, title_h)
        elif sg is not None and srgba is not None:
            sx, sy, title_w, title_h, title_yoff = sg
            title_pix = png.crop(sw, sh, srgba, sx, sy, title_w, title_h)
            if upscale > 1:
                title_pix = png.resize(title_w, title_h, title_pix,
                                       title_w * upscale, title_h * upscale)
                title_w *= upscale; title_h *= upscale; title_yoff *= upscale
        else:
            continue

        if menu_w <= 0 or menu_h <= 0 or title_w <= 0 or title_h <= 0:
            metrics.append((cp, -1, -1, 0, 0, 0, 0, 0)); continue
        fn  = 'kexart/font/g%03d.png' % cp
        fna = 'kexart/fontalt/g%03d.png' % cp
        png.write(os.path.join(outdir, fn), menu_w, menu_h, menu_pix)
        png.write(os.path.join(outdir, fna), title_w, title_h, title_pix)
        if cp < 127:
            tile = TILE_FONT_BASE + (cp - 32)
            alt_tile = TILE_FONT_ALT + (cp - 32)
        else:
            tile = TILE_FONT_EXTRA_BASE + extra[cp]
            alt_tile = TILE_FONT_ALT_EXTRA_BASE + extra[cp]
        lines.append((tile, fn))
        lines.append((alt_tile, fna))
        metrics.append((cp, tile, alt_tile, menu_w, menu_h, title_w, title_h,
                        menu_yoff))

    from kexmap import write_def
    write_def(os.path.join(outdir, 'kexmenu.def'), lines,
              header="// NNBuild: Nightdive PowerSlave Exhumed menu art, generated by\n"
                     "// source/exhumed/tools/kexmenu.py -- do not edit by hand.\n")

    # Pickup captions, in the remaster's own words. Build PowerSlave already has
    # a SetItemPickupMessage(); this just supplies the remaster's wording for the
    # items that exist in both, keyed by the Build lotag the converter assigns.
    # Build lotag -> localization key. Only items that mean the same thing in
    # both games; anything ambiguous is left to the original wording.
    PICKUPS = {
        10: 'str_149', 15: 'str_107', 24: 'str_110', 9: 'str_095',
        12: 'str_106', 41: 'str_109', 42: 'str_106', 23: 'str_101',
        28: 'str_093', 29: 'str_094', 30: 'str_096', 32: 'str_097',
        33: 'str_098', 45: 'str_112', 46: 'str_113', 47: 'str_114',
        48: 'str_115', 11: 'str_111', 31: 'str_099',
    }
    for code, _language in UI_LANGUAGES:
        strs = localizations.get(code, {})
        rows = [(lot, strs[k]) for lot, k in sorted(PICKUPS.items()) if k in strs]
        if not rows:
            continue
        filename = 'kexpickup.txt' if code == 'en' else 'kexpickup_%s.txt' % code
        with open(os.path.join(outdir, filename), 'w') as f:
            for lot, text in rows:
                f.write("%d %s\n" % (lot, text))
        if verbose: print("  pickup captions (%s): %d" % (code, len(rows)))

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
        dolls = read('defs/actors/teamdolls.txt') or ''

    # kexinv.txt: every string the status screen and the collectible pickups
    # show, keyed by name, straight from the remaster's localization table --
    # the weapon and artifact descriptions with their embedded "\n" left as the
    # two characters, and "${0}" where a count goes; the engine expands both.
    # The team dolls' names are the pickupMessage of each doll's actor def.
    INV = [('status', 'str_088'), ('page0', 'str_044'), ('page1', 'str_045'),
           ('page2', 'str_046'), ('page3', 'str_047'),
           ('noarti', 'str_064'), ('doll', 'str_071'), ('trans', 'str_072'),
           ('transfull', 'str_073'), ('transn', 'str_074'), ('trans0', 'str_075'),
           ('maxhp', 'str_108'), ('mapname', 'str_111'),
           ('difficultytitle', 'm_select_difficulty'),
           ('difficulty0', 'm_difficulty_normal'),
           ('difficulty1', 'm_difficulty_hard'),
           ('difficulty2', 'm_difficulty_pharaoh')]
    INV += [('weap%d' % i, 'str_%03d' % (56 + i)) for i in range(8)]
    INV += [('arti%d' % i, 'str_%03d' % (65 + i)) for i in range(6)]
    INV += [('artiname%d' % i, 'str_%03d' % (100 + i)) for i in range(6)]
    INV += [(key, key) for key in ('m_new_game', 'm_load_game', 'm_options',
            'm_about', 'm_quit', 'm_game_paused', 'm_resume',
            'm_reload_last_checkpoint', 'm_restart_level',
            'm_exit_title_screen', 'm_go_back', 'm_yes', 'm_no',
            'm_press_start', 'm_prompt_select', 'm_prompt_back', 'm_prompt_menu',
            'm_overwrite_game_confirm', 'm_load_game_confirm', 'm_travel',
            'm_quit_game_confirm',
            'm_reload_checkpoint_confirm', 'm_reload_level_confirm',
            'm_exit_game_confirm', 'm_reset_settings_confirm', 'm_empty_slot',
            'm_input_settings', 'm_reset_settings', 'm_audio_settings',
            'm_audio_output', 'm_music_volume', 'm_sound_volume', 'm_stereo',
            'm_video_settings', 'm_videoResolution', 'm_displayresolution',
            'm_windowmode', 'm_windowed', 'm_fullscreen', 'm_borderless',
            'm_verticalSync', 'm_maxFrameRate', 'm_brightness', 'm_unlimited',
            'm_applychanges', 'm_video_apply_confirm', 'm_advanced_display_settings',
            'm_display_settings', 'm_antialiasing', 'm_anisotrophy', 'm_fov',
            'm_hudSettings', 'm_showPlayTime', 'm_hudtext', 'm_crosshairsize',
            'm_game_settings', 'm_checkpoints', 'm_overworldScreen',
            'm_new', 'm_original',
            'm_viewBobSpeed', 'm_viewBobHeight', 'm_turnSway',
            'm_weaponJumpBobbing',
            'm_mouse_settings', 'm_msens_x', 'm_msens_y', 'm_msmooth',
            'm_gamepad_settings', 'm_vibration', 'm_vibration_volume',
            'm_joylookspeed_x', 'm_joylookspeed_y', 'm_joymovesens',
            'm_joysmoothing', 'm_joyinvertlook', 'm_swapjoysticks',
            'm_deadzone', 'm_lstick', 'm_rstick',
            'm_bindings', 'm_bind_weaponwheel', 'm_bind_inventory',
            'm_bind_automap', 'm_bind_mapzoomin', 'm_bind_mapzoomout',
            'm_bind_screenshot',
            'm_bind_forward', 'm_bind_backward', 'm_bind_use',
            'm_bind_strafe_left', 'm_bind_strafe_right', 'm_bind_attack',
            'm_bind_jump', 'm_bind_weap_0', 'm_bind_weap_1',
            'm_bind_weap_2', 'm_bind_weap_3', 'm_bind_weap_4',
            'm_bind_weap_5', 'm_bind_weap_6', 'm_bind_weap_7',
            'm_bind_weap_next', 'm_bind_weap_prev', 'm_bind_turn_left',
            'm_bind_turn_right', 'm_next_page', 'm_bind_prompt',
            'm_prompt_cancel',
            'm_on', 'm_off', 'm_language_settings', 'm_language')]
    doll_names = re.findall(r'Actor\s+TD_Doll(\d+).*?pickupMessage\s+"([^"]*)"', dolls, re.S)
    for code, _language in UI_LANGUAGES:
        strs = localizations.get(code, {})
        rows = [(key, strs[value]) for key, value in INV if value in strs]
        rows += [('dollname%d' % (int(number) - 1), text)
                 for number, text in doll_names]
        if not rows:
            continue
        filename = 'kexinv.txt' if code == 'en' else 'kexinv_%s.txt' % code
        with open(os.path.join(outdir, filename), 'w') as f:
            for key, text in rows:
                f.write("%s %s\n" % (key, text))
        if verbose: print("  inventory strings (%s): %d (%d doll names)" %
                          (code, len(rows), len(doll_names)))

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

        blocks = re.findall(r'Map\s+(\w+)\s*\{(.*?)\n\}', mapinfo, re.S)
        order = [b[0] for b in blocks]
        for code, _language in UI_LANGUAGES:
            names = localizations.get(code, {})
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
                # Store source-pixel coordinates alongside their transformed
                # repaint position. The menu's original-overworld option must
                # put each node at the exact mapInfo location, not an inverse
                # approximation of a rounded HD coordinate.
                original_x = int(float(field('overworld_x', '0')))
                original_y = int(float(field('overworld_y', '0')))
                ox = original_x / float(OVERWORLD_ORIGINAL_W)
                oy = original_y / float(OVERWORLD_ORIGINAL_H)
                w_, h_ = owsize if owsize else (640, 512)
                px = int(round((OW_AX + ox * OW_AW) * w_)) if owsize else int(ox * 640)
                py = int(round((OW_AY + oy * OW_AH) * h_)) if owsize else int(oy * 512)
                rows.append((mapfile, str(px), str(py), str(original_x), str(original_y),
                             title, links))
            filename = 'kexworld.txt' if code == 'en' else 'kexworld_%s.txt' % code
            with open(os.path.join(outdir, filename), 'w') as f:
                f.write("# map repaint_x repaint_y original_x original_y north east south west title\n")
                for mapfile, x, y, original_x, original_y, title, links in rows:
                    f.write("%s %s %s %s %s %d %d %d %d %s\n"
                            % (mapfile, x, y, original_x, original_y,
                               links[0], links[1], links[2], links[3], title))
            if verbose: print("  overworld (%s): %d levels" % (code, len(rows)))

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
        f.write("# codepoint menu_tile title_tile menu_w menu_h title_w title_h yoffset\n")
        f.write("scale %g\n" % scale)
        for cp, tile, alt_tile, w, h, title_w, title_h, yo in metrics:
            f.write("%d %d %d %d %d %d %d %d\n" %
                    (cp, tile, alt_tile, w, h, title_w, title_h, yo))

    if verbose:
        print("  %d glyphs sliced from bigfont (atlas %dx%d, scale %g)"
              % (sum(1 for m in metrics if m[1] >= 0), aw, ah, scale))
    return len(lines)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit("usage: kexmenu.py <PowerSlave Exhumed dir> <output dir>")
    n = extract(sys.argv[1], sys.argv[2])
    print("wrote %d menu tiles" % n)
