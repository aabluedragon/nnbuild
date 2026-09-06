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
import sys, os, struct, math, zipfile, re, io, wave, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kexmap as K
import kexseqtool as S
import kexmenu as Menu


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


def verify_level_textures(z, include_testmaps):
    """Every texture a converted face or sky can use must be a real PNG.

    PSM texture tables are broader than the set of face references, and may
    contain trailing padding.  Audit the latter rather than requiring every
    string-table entry: a malformed unused name must not consume a Build tile,
    but a missing material on a rendered surface is a conversion failure.
    """
    used = {}
    for name in z.namelist():
        lower = name.lower()
        if (not lower.startswith('maps/') or not lower.endswith('.map')
                or '/old/' in lower
                or (not include_testmaps and '/testmaps/' in lower)):
            continue
        data = z.read(name)
        if data[:4] != b'PSM\x01':
            continue
        try:
            m = K.locate(data)
        except (ValueError, struct.error) as exc:
            return ['cannot parse source map %s while auditing textures: %s' % (name, exc)]
        refs = {m.textures[tex] for _uvs, tex in m.tiles if m.textures[tex]}
        if m.sky:
            refs.add(m.sky)
        for texture in refs:
            used.setdefault(texture, set()).add(os.path.basename(name))

    archive = {name.lower(): name for name in z.namelist()}
    out = []
    for texture, owners in sorted(used.items()):
        real = archive.get(texture.lower())
        if real is None:
            out.append('referenced texture %r is absent (maps: %s)'
                       % (texture, ', '.join(sorted(owners))))
            continue
        with z.open(real) as f:
            if K.png_size(f.read(32)) is None:
                out.append('referenced texture %r is not a PNG (maps: %s)'
                           % (texture, ', '.join(sorted(owners))))
    return out


def verify_sound_aliases(z, outdir):
    """Audit the complete remaster sound bridge, not only selected effects."""
    try:
        rows = {}
        for line in open(os.path.join(outdir, 'kexsound.txt')):
            text = line.strip()
            if not text or text.startswith('#'):
                continue
            cols = text.split()
            if len(cols) != 3:
                return ['malformed kexsound.txt row: %r' % text]
            alias, path, loop_text = cols
            try:
                loop = int(loop_text)
            except ValueError:
                return ['sound alias %s has non-integer loop flag %r' % (alias, loop_text)]
            if alias in rows:
                return ['sound alias %s is duplicated' % alias]
            rows[alias] = (path, loop)
    except OSError as exc:
        return ['cannot read kexsound.txt: %s' % exc]

    out = []
    expected = K.KEX_SOUNDS
    for alias, value in sorted(expected.items()):
        if rows.get(alias) != value:
            out.append('sound alias %s is %r, expected %r' %
                       (alias, rows.get(alias), value))
    for alias in sorted(set(rows) - set(expected)):
        out.append('unexpected sound alias %s' % alias)

    archive = {name.lower(): name for name in z.namelist()}
    for alias, (path, loop) in sorted(expected.items()):
        if len(alias) > 23 or len(path) > 31:
            out.append('sound alias %s exceeds native loader field limits' % alias)
        if loop not in (0, 1):
            out.append('sound alias %s has invalid loop flag %d' % (alias, loop))
        real = archive.get(path.lower())
        if real is None:
            out.append('sound alias %s points to missing %s' % (alias, path))
            continue
        data = z.read(real)[:12]
        if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
            out.append('sound alias %s does not point to a WAV: %s' % (alias, path))

    source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src', 'kexbreak.cpp'))
    try:
        native = re.sub(r'\s+', '', open(source).read())
        for needle in ('#definekMaxKexSounds200', 'charname[24];charpath[32];',
                       'KexTable_Load("kexsound.txt",kMaxKexSounds,KexSound_ParseLine)'):
            if needle not in native:
                out.append('native sound-alias bridge changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native sound-alias bridge: %s' % exc)
    return out


def verify_tactile(z):
    """Validate every authored KEX controller curve and its native bridge."""
    out = []
    expected = {
        'bombthrow', 'braceletfire', 'cstafffire', 'fthrowfire', 'm90fire',
        'machfire', 'machflesh', 'machhit', 'pistolfire', 'ppain01',
        'ppain02', 'ppain03', 'pshock', 'pwpain01', 'pwpain02', 'pwpain03',
        'pwpain04', 'pwpain05', 'quake', 'rafire', 'shockold',
    }
    names = {os.path.splitext(os.path.basename(name))[0]
             for name in z.namelist()
             if name.lower().startswith('tactile/') and name.lower().endswith('.bnvib')}
    if names != expected:
        out.append('tactile curve roster differs: missing %s, unexpected %s' %
                   (sorted(expected - names), sorted(names - expected)))

    for curve in sorted(expected & names):
        path = next(name for name in z.namelist()
                    if name.lower() == ('tactile/' + curve + '.bnvib').lower())
        data = z.read(path)
        if len(data) < 12:
            out.append('tactile curve is truncated: %s' % path)
            continue
        version, motors, rate = struct.unpack_from('<IHH', data)
        if version == 4:
            header, length = 12, struct.unpack_from('<I', data, 8)[0]
        elif version == 12 and len(data) >= 20:
            header, length = 20, struct.unpack_from('<I', data, 16)[0]
            loop_first, loop_last = struct.unpack_from('<II', data, 8)
            if loop_first > loop_last or loop_last >= length // 4:
                out.append('tactile loop range is invalid: %s' % path)
        else:
            out.append('tactile curve has unsupported version %d: %s' % (version, path))
            continue
        if motors != 3 or rate != 200 or length != len(data) - header or length % 4:
            out.append('tactile curve has invalid 200 Hz two-motor layout: %s' % path)
        elif not (0 < length // 4 <= 256):
            out.append('tactile curve exceeds native frame cache: %s' % path)

    expected_actions = {
        'sprites/weapons/bomb_anim.txt': {'bombthrow'},
        'sprites/weapons/cobra_staff_anim.txt': {'cstafffire'},
        'sprites/weapons/flamethrower_anim.txt': {'fthrowfire'},
        'sprites/weapons/m60_anim.txt': {'m90fire'},
        'sprites/weapons/machete_anim.txt': {'machfire', 'machflesh', 'machhit'},
        'sprites/weapons/manacle_anim.txt': {'shockold', 'braceletfire'},
        'sprites/weapons/pistol_anim.txt': {'pistolfire'},
        'sprites/weapons/ring_of_ra_anim.txt': {'rafire'},
    }
    for path, curves in expected_actions.items():
        try:
            text = z.read(path).decode('latin-1')
        except KeyError:
            out.append('missing tactile action source: %s' % path)
            continue
        found = set(re.findall(r'A_Play(?:Looping)?Tactile\("tactile/([^"]+)', text))
        if found != curves:
            out.append('tactile actions in %s are %s, expected %s' %
                       (path, sorted(found), sorted(curves)))
        if 'fthrowfire' in curves or 'shockold' in curves:
            if 'A_StopLoopingTactile()' not in text:
                out.append('looped tactile action lacks its stop action: %s' % path)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        tactile = open(os.path.join(srcdir, 'kextactile.cpp')).read()
        sound = open(os.path.join(srcdir, 'sound.cpp')).read()
        sound_header = open(os.path.join(srcdir, 'sound.h')).read()
        move = open(os.path.join(srcdir, 'move.cpp')).read()
        for curve in sorted(expected):
            if '"%s"' % curve not in tactile:
                out.append('native tactile bridge does not map %s' % curve)
        for needle in ('KexTactile_Load', 'version == 4', 'version == 12',
                       'kKexTactileTickRate = 30', 'kex_vibration_volume'):
            if needle not in tactile:
                out.append('native tactile bridge changed: %s' % needle)
        if 'KexTactile_OnSound(szSoundName[index], nSprite)' not in sound:
            out.append('sound playback no longer dispatches authored tactile curves')
        if 'KexTactile_StopAll();' not in sound or 'KexTactile_StopSprite(' not in sound:
            out.append('sound stop path no longer stops looped tactile curves')
        maximum = re.search(r'#define\s+kMaxSoundNameLen\s+(\d+)', sound_header)
        if not maximum or int(maximum.group(1)) < len('k_electrocute') + 1:
            out.append('native sound-name field still truncates KEX-only aliases')
        if 'KexTactile_Quake();' not in move or 'KexTactile_Tick();' not in move:
            out.append('native tactile bridge no longer drives quake or playback ticks')
        if 'CONTROL_JoystickEnabled' not in tactile:
            out.append('native tactile bridge ignores disabled controller input')
    except OSError as exc:
        out.append('cannot read native tactile bridge: %s' % exc)
    return out


def verify_manacle(z):
    """Keep the charged Manacle beam on the remaster's single aim line."""
    out = []
    names = {name.lower(): name for name in z.namelist()}
    pspr = names.get('scripts/p_pspr.txt')
    if pspr is None:
        return ['retail Manacle action source is missing']

    source = z.read(pspr).decode('latin-1').replace('\r', '')
    start = source.find('void A_FireBolts(')
    brace = source.find('{', start)
    if start < 0 or brace < 0:
        return ['retail A_FireBolts action is missing']
    depth = 0
    end = -1
    for i in range(brace, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end < 0:
        return ['retail A_FireBolts action is unterminated']

    retail = re.sub(r'\s+', '', source[start:end])
    for needle in ('for(inti=0;i<manacleChargeTime+1;++i)',
                   'bolt.GetRotation().z=actor.GetRotation().z;',
                   'bolt.GetRotation().x=actor.GetRotation().x;',
                   'dir.x=(Math::Sin(actor.GetRotation().z)*cp)*64.0f;',
                   'dir.y=(Math::Cos(actor.GetRotation().z)*cp)*64.0f;'):
        if needle not in retail:
            out.append('retail A_FireBolts contract changed: %s' % needle)
    if 'A_Quake(' in retail:
        out.append('retail A_FireBolts unexpectedly applies a quake')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'gun.cpp')).read())
    except OSError as exc:
        return out + ['cannot read native Manacle release: %s' % exc]
    start = native.find('casekWeaponManacle:')
    end = native.find('casekWeaponMummified:', start)
    release = native[start:end] if start >= 0 and end >= 0 else ''
    for needle in ('intconstnBolts=1+s_manacleCharge[nPlayer];',
                   'for(inti=0;i<nBolts;i++)',
                   'BuildBullet(nPlayerSprite,18,ebp,ebx,nHeight,nAngle,var_50,var_1C);',
                   'KexManacle_LastShot=nBolts;'):
        if needle not in release:
            out.append('native Manacle release changed: %s' % needle)
    for forbidden in ('intconstspread=', '(nAngle+', 'nQuake[nPlayer]='):
        if forbidden in release:
            out.append('native Manacle release has non-retail %s' % forbidden)
    return out


def verify_controller_defaults():
    """A fresh cfg must make the remaster's controller UI and haptics usable."""
    out = []
    source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src', 'config.cpp'))
    try:
        native = re.sub(r'\s+', '', open(source).read())
        for needle in ('staticvoidCONFIG_SetGameControllerDefaults(void)',
                       'CONTROLLER_BUTTON_A,gamefunc_Open',
                       'CONTROLLER_BUTTON_B,gamefunc_Escape',
                       'CONTROLLER_BUTTON_START,gamefunc_Pause',
                       'CONTROLLER_AXIS_LEFTX,analog_strafing',
                       'CONTROLLER_AXIS_RIGHTY,analog_lookingupanddown',
                       'CONTROLLER_AXIS_TRIGGERLEFT,1,gamefunc_Jump',
                       'CONTROLLER_AXIS_TRIGGERRIGHT,1,gamefunc_Fire',
                       'CONFIG_SetGameControllerDefaults();'):
            if needle not in native:
                out.append('fresh controller defaults changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native controller defaults: %s' % exc)
    menu_source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src', 'kexmenu.cpp'))
    try:
        menu = open(menu_source).read()
        for needle in ('static void KexMenu_Mouse(void)', 'KexMenu_SetMouseSensitivity',
                       'm_mouse_settings', 'm_msens_x', 'm_msens_y',
                       'CONTROL_MouseAxesSensitivity',
                       'static void KexMenu_Gamepad(void)',
                       'static void KexMenu_GamepadDeadzones(void)',
                       'KexMenu_SetControllerScale', 'KexMenu_SetControllerDeadzone',
                       'm_gamepad_settings', 'm_vibration_volume',
                       'm_joylookspeed_x', 'm_joylookspeed_y', 'm_joymovesens',
                       'm_joyinvertlook', 'm_deadzone', 'm_lstick', 'm_rstick',
                       'CONTROLLER_AXIS_RIGHTX', 'CONTROLLER_AXIS_RIGHTY',
                       'CONTROLLER_AXIS_LEFTX', 'CONTROLLER_AXIS_LEFTY',
                       'kex_vibration_volume'):
            if needle not in menu:
                out.append('remaster gamepad settings changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native gamepad menu: %s' % exc)
    return out


def verify_actor_pool_guards():
    """Creature pools must reject exhaustion without changing free-slot state."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    pools = (
        ('anubis.cpp', 'BuildAnubis', 'AnubisCount'),
        ('lion.cpp', 'BuildLion', 'LionCount'),
        ('queen.cpp', 'BuildQueen', 'QueenCount'),
        ('rex.cpp', 'BuildRex', 'RexCount'),
        ('roach.cpp', 'BuildRoach', 'RoachCount'),
        ('scorp.cpp', 'BuildScorp', 'ScorpCount'),
        ('set.cpp', 'BuildSet', 'SetCount'),
    )
    for filename, constructor, pool in pools:
        try:
            native = re.sub(r'\s+', '', open(os.path.join(source_dir, filename)).read())
        except OSError as exc:
            out.append('cannot read %s pool guard: %s' % (filename, exc))
            continue
        start = native.find(constructor + '(')
        body = native[start:start + 600] if start >= 0 else ''
        guard = 'if(%s<=0)return-1;' % pool
        allocation = '--%s' % pool
        if (guard not in body or allocation not in body
                or body.find(guard) > body.find(allocation)):
            out.append('%s can corrupt %s when its pool is exhausted'
                       % (constructor, pool))
    return out


def verify_input_compat(z):
    """The remaster's stock bind vocabulary must reach the KEX input packet."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    default_name = next((name for name in z.namelist()
                         if name.lower() == 'default.cfg'), None)
    if default_name is None:
        return ['remaster default.cfg is missing']
    default = z.read(default_name).decode('utf-8').replace('\r', '')
    bound_commands = set(re.findall(r'^bind\s+[^\s]+\s+"([^"\s]+)', default, re.M))

    try:
        commands = re.sub(r'\s+', '', open(os.path.join(source_dir, 'osdcmds.cpp')).read())
        for command in sorted(bound_commands - {'screenshot'}):
            if '"%s"' % command not in commands:
                out.append('KEX default bind command is unregistered: %s' % command)
        for needle in ('staticintosdcmd_kexcompat(osdcmdptr_tparm)', 'if(!g_kexMode)',
                       '"automap",gamefunc_Map',
                       '"+weapprev",gamefunc_Previous_Weapon',
                       '"+weapnext",gamefunc_Next_Weapon',
                       '"*weaponwheel",gamefunc_Weapon_Wheel',
                       'CONTROL_ButtonFlags[gamefunc]',
                       'staticintKexCompat_MouseButton(charconst*name)',
                       '"mouse_left",0', '"mouse_wheel_up",4',
                       '"mouse_wheel_down",5',
                       'staticintKexCompat_JoystickButton(charconst*name)',
                       'JoystickDigitalFunctions[axis][1]=gamefunc;',
                       'CONTROLLER_AXIS_TRIGGERLEFT',
                       'CONTROLLER_AXIS_TRIGGERRIGHT',
                       '"r_fov"', '"r_antialiasing"', '"r_rhimaxanisotropic"',
                       'gFov=Blrintf(s_kexFov);',
                       'glanisotropy=s_kexAnisotropy;'):
            if needle not in commands:
                out.append('KEX input command bridge changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read KEX input command bridge: %s' % exc)

    try:
        config = re.sub(r'\s+', '', open(os.path.join(source_dir, 'config.cpp')).read())
        if ('"Weapon_8"' not in config or '"Weapon_Wheel"' not in config
                or '"7","","8","","","","Pause",""' not in config):
            out.append('KEX Manacle default binding changed')
        for needle in ('constcharkexkeydefaults[kMaxGameFunctions*2][kMaxGameFuncLen]',
                       'CONFIG_SetDefaultKeys(g_kexMode?kexkeydefaults:keydefaults);',
                       '"Q",""', '"LShift",""',
                       'MouseFunctions[4][0]=gamefunc_Next_Weapon;',
                       'MouseFunctions[5][0]=gamefunc_Previous_Weapon;',
                       'CONTROL_BindKey(sc_F5,"screenshot",1,KB_ScanCodeToString(sc_F5));'):
            if needle not in config:
                out.append('KEX fresh input default changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read KEX input defaults: %s' % exc)

    try:
        startup = re.sub(r'\s+', '', open(os.path.join(source_dir, 'exhumed.cpp')).read())
        if 'CONFIG_SetDefaultKeys(g_kexMode?kexkeydefaults:keydefaults,true);' not in startup:
            out.append('KEX late input defaults changed')
        if 'KexMenu_Status(kexWeaponWheel);' not in startup:
            out.append('KEX weapon-wheel selector dispatch changed')
    except OSError as exc:
        out.append('cannot read KEX input startup defaults: %s' % exc)

    try:
        menu = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexmenu.cpp')).read())
        for needle in ('staticvoidKexMenu_Bindings(void)', 's_kexBindings[]',
                       'm_bind_weaponwheel', 'm_bind_screenshot',
                       'gamefunc_Weapon_Wheel', 'KexMenu_CaptureBinding',
                       'KexMenu_CopyCommandsExcept', 'binding.command',
                       'VB_KEX_BINDINGS_TEST', 'KexBindingstest:',
                       'CONFIG_MapKey(binding.gamefunc',
                       'kKexBindings', 'KexMenu_Bindings();',
                       'voidKexMenu_Status(boolweaponWheel)',
                       'SetNewWeapon(nLocalPlayer,weapon);'):
            if needle not in menu:
                out.append('KEX Bindings menu changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read KEX Bindings menu: %s' % exc)

    try:
        native_input = re.sub(r'\s+', '', open(os.path.join(source_dir, 'input.cpp')).read())
        for needle in ('if(g_kexMode)',
                       'staticintconsts_kexWeaponForSlot[]={0,1,2,4,3,5,6,7};',
                       'BUTTON(gamefunc_Weapon_1+slot)',
                       'for(inti=6;i>=0;i--)'):
            if needle not in native_input:
                out.append('KEX input packet mapping changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read KEX input packet bridge: %s' % exc)
    return out


def verify_game_settings():
    """Keep the KEX-only Game Settings UI tied to its native behavior."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        inventory = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexinv.cpp')).read())
        for needle in ('intkex_enable_checkpoints=1;',
                       '!kex_enable_checkpoints||nTotalPlayers!=1',
                       'if(!kex_enable_checkpoints)KexCheckpoint_ClearActive();',
                       'returng_kexMode&&kex_enable_checkpoints&&nTotalPlayers==1'):
            if needle not in inventory:
                out.append('native checkpoint setting changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native checkpoint setting: %s' % exc)
    try:
        menu = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexmenu.cpp')).read())
        for needle in ('intkex_original_overworld=0;', 'staticvoidKexMenu_Game(void)',
                       'm_checkpoints', 'm_overworldScreen', 'm_viewBobSpeed',
                       'm_viewBobHeight', 'm_turnSway', 'm_weaponJumpBobbing',
                       'kKexTileOverworldOriginal=7017',
                       'kKexTileEyeOriginalBase=7550',
                       'original?L.originalX:L.x',
                       'original?originalArrow[d]:hdArrow[d]'):
            if needle not in menu:
                out.append('native Game Settings menu changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native Game Settings menu: %s' % exc)
    try:
        commands = re.sub(r'\s+', '', open(os.path.join(source_dir, 'osdcmds.cpp')).read())
        for needle in ('"kex_enable_checkpoints"', '"kex_original_overworld"',
                       '"kex_view_bob_speed"', '"kex_view_bob_height"',
                       '"kex_turn_sway"', '"kex_weapon_jump_bobbing"'):
            if needle not in commands:
                out.append('Game Settings cvar changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read Game Settings cvars: %s' % exc)
    tool = os.path.abspath(os.path.join(os.path.dirname(__file__), 'kexmenu.py'))
    try:
        converter = open(tool).read()
        for needle in ('TILE_OVERWORLD_ORIGINAL = 7017',
                       'TILE_EYE_ORIGINAL_BASE = 7550',
                       "'gfx/overworld_orig.png'",
                       "'sprites/misc/overworld_anim.txt'",
                       'original_x = int(float(field(\'overworld_x\', \'0\')))'):
            if needle not in converter:
                out.append('Game Settings converter changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read Game Settings converter: %s' % exc)
    return out


def verify_display_settings():
    """Keep the KEX display-preference menu honest about renderer support."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        menu = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexmenu.cpp')).read())
        for needle in ('staticvoidKexMenu_Display(void)', 'm_display_settings',
                       'm_fov', 'm_antialiasing', 'm_anisotrophy',
                       'KexRender_SetFov(KexRender_Fov()+direction);',
                       'KexRender_SetAntialiasing(KexRender_Antialiasing()+direction);',
                       'KexRender_SetAnisotropy(KexRender_Anisotropy()+direction);',
                       'SMAA(COMPAT)'):
            if needle not in menu:
                out.append('native Display Settings menu changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native Display Settings menu: %s' % exc)
    try:
        commands = re.sub(r'\s+', '', open(os.path.join(source_dir, 'osdcmds.cpp')).read())
        for needle in ('staticfloats_kexFov=64.f;', 'staticints_kexAntialiasing=7;',
                       'staticints_kexAnisotropy=16;', 'floatKexRender_Fov(void)',
                       'voidKexRender_SetFov(floatvalue)',
                       'intKexRender_Antialiasing(void)',
                       'voidKexRender_SetAntialiasing(intvalue)',
                       'intKexRender_Anisotropy(void)',
                       'voidKexRender_SetAnisotropy(intvalue)', 'gltexapplyprops()',
                       '"r_fov"', '"r_antialiasing"', '"r_rhimaxanisotropic"'):
            if needle not in commands:
                out.append('Display Settings runtime bridge changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read Display Settings runtime bridge: %s' % exc)
    try:
        header = re.sub(r'\s+', '', open(os.path.join(source_dir, 'osdcmds.h')).read())
        for needle in ('floatKexRender_Fov(void);', 'voidKexRender_SetFov(floatvalue);',
                       'intKexRender_Antialiasing(void);',
                       'voidKexRender_SetAntialiasing(intvalue);',
                       'intKexRender_Anisotropy(void);',
                       'voidKexRender_SetAnisotropy(intvalue);'):
            if needle not in header:
                out.append('Display Settings header bridge changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read Display Settings header bridge: %s' % exc)
    return out


def verify_hud_settings():
    """Keep remaster HUD controls tied to their KEX-only visible behavior."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        inventory = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexinv.cpp')).read())
        for needle in ('intkex_show_play_time=1;', 'intkex_show_hud_messages=1;',
                       'floatkex_crosshair_size=1.f;', 's_kexPlayTimeTicks+=4;',
                       's_saveMagicV4', 's_saveMagicV5',
                       'slots[nSlot].playTimeTicks=s_kexPlayTimeTicks;',
                       's_kexPlayTimeTicks=slot.playTimeTicks;',
                       'intconstmigratedV4=', 's_kexPlayTimeTicks==0;'):
            if needle not in inventory:
                out.append('native HUD setting changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native HUD settings: %s' % exc)
    try:
        menu = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexmenu.cpp')).read())
        for needle in ('staticvoidKexMenu_Hud(void)', 'm_hudSettings',
                       'm_showPlayTime', 'm_hudtext', 'm_crosshairsize',
                       'kex_show_hud_messages&&message_timer',
                       'KexPlayTime_Seconds()'):
            if needle not in menu:
                out.append('native HUD settings menu changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native HUD settings menu: %s' % exc)
    try:
        view = re.sub(r'\s+', '', open(os.path.join(source_dir, 'view.cpp')).read())
        for needle in ('clamp(kex_crosshair_size,0.f,3.f)',
                       '65536.f*crosshairSize'):
            if needle not in view:
                out.append('native HUD crosshair changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native HUD crosshair: %s' % exc)
    try:
        input_source = re.sub(r'\s+', '', open(os.path.join(source_dir, 'exhumed.cpp')).read())
        for needle in ('if(g_kexMode)kex_crosshair_size=kex_crosshair_size>0.f?0.f:1.f;',
                       'elsegShowCrosshair=!gShowCrosshair;'):
            if needle not in input_source:
                out.append('HUD crosshair toggle changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read HUD crosshair toggle: %s' % exc)
    try:
        commands = re.sub(r'\s+', '', open(os.path.join(source_dir, 'osdcmds.cpp')).read())
        for needle in ('"kex_show_play_time"', '"kex_show_hud_messages"',
                       '"kex_crosshair_size"'):
            if needle not in commands:
                out.append('HUD Settings cvar changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read HUD Settings cvars: %s' % exc)
    return out


def verify_checkpoint_saves():
    """A saved KEX slot must rebuild its most recent scarab snapshot safely."""
    out = []
    source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src',
                                          'kexinv.cpp'))
    try:
        native = re.sub(r'\s+', '', open(source).read())
        for needle in ('staticcharconsts_checkpointSaveMagicV1[8]="KEXCHK1";',
                       'structKexCheckpointDiskHeader',
                       'structKexCheckpointDiskState',
                       'staticvoidKexCheckpoint_Save(intnSlot)',
                       'KexCheckpoint_WriteHeader(fp,0,0)',
                       'staticintKexCheckpoint_Load(intnSlot)',
                       'header.payloadSize!=expectedSize',
                       'if(checksum!=header.checksum)',
                       's_checkpoint.pending=1;',
                       'KexCheckpoint_Save(nSlot);',
                       'KexCheckpoint_Reset();',
                       '&&KexCheckpoint_Load(nSlot)',
                       'KexCheckpoint_MapLeaf(s_checkpoint.map)',
                       'KexCheckpoint_MapLeaf(gUserMapFilename)'):
            if needle not in native:
                out.append('durable checkpoint save contract changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read durable checkpoint save bridge: %s' % exc)
    return out


def verify_named_save_state():
    """Snapshot callers need distinct, exact save paths rather than slot 3."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(source_dir, 'save.cpp')).read())
        for needle in ('VB_EX_SAVE_TEST_FILE',
                       'LoadSave::SaveGame(Exhumed_SaveTestFilename());',
                       'LoadSave::LoadGame(Exhumed_SaveTestFilename());',
                       'charconst*constfilename=pzFile&&pzFile[0]?pzFile:"save3.gam";',
                       'hLFile=buildvfs_open_read(filename);',
                       'hSFile=buildvfs_open_write(filename);'):
            if needle not in native:
                out.append('named save-state path changed: %s' % needle)
        if native.count('charconst*constfilename=pzFile&&pzFile[0]?pzFile:"save3.gam";') != 2:
            out.append('named save-state path is not shared by load and save')
    except OSError as exc:
        out.append('cannot read named save-state bridge: %s' % exc)
    try:
        header = re.sub(r'\s+', '', open(os.path.join(source_dir, 'save.h')).read())
        for needle in ('staticvoidLoadGame(constchar*path);',
                       'staticvoidSaveGame(constchar*path);'):
            if needle not in header:
                out.append('named save-state header changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read named save-state header: %s' % exc)
    try:
        inventory = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexinv.cpp')).read())
        menu = re.sub(r'\s+', '', open(os.path.join(source_dir, 'menu.cpp')).read())
        for needle in ('VB_EXHUMED_SAVE_FILE', 'VB_KEXINV_SAVE_FILE',
                       'menu_GameSave(2);', 'menu_GameSave(3);',
                       'Kexmenuloadtest:FAIL(isolatedVB_EXHUMED_SAVE_FILEandVB_KEXINV_SAVE_FILErequired)'):
            if needle not in inventory:
                out.append('isolated KEX campaign-save probe changed: %s' % needle)
        for needle in ('charconst*constoverrideName=Bgetenv("VB_EXHUMED_SAVE_FILE");',
                       'menu_ApplyGameStats();', 'KexInv_Load(nSlot,GameStats.nMap)',
                       'KexCampaign_StagePlayer(nLocalPlayer);'):
            if needle not in menu:
                out.append('KEX menu/classic-save pairing changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read isolated KEX campaign-save bridge: %s' % exc)
    return out


def verify_startnow_roster():
    """Reserved mmulti slots must not become lockstep players before snapshot join."""
    out = []
    source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src', 'vbnet.cpp'))
    try:
        native = re.sub(r'\s+', '', open(source).read())
        for needle in ('EXNET_PT_ROSTER',
                       'staticuint8_ts_activeSlot[kMaxPlayers];',
                       'staticintExNet_ApplyRoster(uint8_tconst*slots)',
                       'staticintExNet_WaitForRoster(void)',
                       'nTotalPlayers=(short)s_activePlayers;',
                       'nLocalPlayer=ExNet_PlayerForSlot(myconnectindex);',
                       'wConsoleNode=ExNet_PlayerForSlot(connecthead);',
                       'if(!ExNet_SlotActive(p))continue;',
                       'sPlayerInput[player]=s_ring[p][t&EXNET_RINGMASK];',
                       'LogoffPlayer(player);'):
            if needle not in native:
                out.append('start-now active-roster contract changed: %s' % needle)
        if 'nTotalPlayers=(short)numplayers;' in native:
            out.append('reserved transport slots still become game players')
    except OSError as exc:
        out.append('cannot read start-now roster bridge: %s' % exc)
    return out


def verify_network_snapshot_state():
    """Late joining needs a separate, all-player snapshot without changing EXSV."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        save = re.sub(r'\s+', '', open(os.path.join(source_dir, 'save.cpp')).read())
        for needle in ('staticcharconstkNetworkSnapshotSig[]="EXSN";',
                       'staticuint16_tconstkNetworkSnapshotVersion=2;',
                       'intLoadSave::IsNetworkSnapshot()',
                       'voidLoadSave::LoadNetworkSnapshot(constchar*pzFile)',
                       'voidLoadSave::SaveNetworkSnapshot(constchar*pzFile)',
                       'Exhumed_LoadStateFile(pzFile,kNetworkSnapshotSig,kNetworkSnapshotVersion,1);',
                       'Exhumed_SaveStateFile(pzFile,kNetworkSnapshotSig,kNetworkSnapshotVersion,1);'):
            if needle not in save:
                out.append('network snapshot format changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read network snapshot format: %s' % exc)
    try:
        player = re.sub(r'\s+', '', open(os.path.join(source_dir, 'player.cpp')).read())
        for needle in ('if(LoadSave::IsNetworkSnapshot())',
                       'Read(PlayerList,sizeof(PlayerList));',
                       'Write(PlayerList,sizeof(PlayerList));',
                       'Read(sPlayerSave,sizeof(sPlayerSave));',
                       'Write(sPlayerSave,sizeof(sPlayerSave));',
                       'nLocalSpr=PlayerList[nLocalPlayer].nSprite;'):
            if needle not in player:
                out.append('all-player snapshot contract changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read all-player snapshot bridge: %s' % exc)
    try:
        move = re.sub(r'\s+', '', open(os.path.join(source_dir, 'move.cpp')).read())
        for needle in ('Read(nBodySprite,LoadSave::IsNetworkSnapshot()?sizeof(nBodySprite):sizeof(nBodySprite[0])*nCurBodyNum);',
                       'Write(nBodySprite,LoadSave::IsNetworkSnapshot()?sizeof(nBodySprite):sizeof(nBodySprite[0])*nCurBodyNum);',
                       'Read(nBodyGunSprite,sizeof(nBodyGunSprite));',
                       'Write(nBodyGunSprite,sizeof(nBodyGunSprite));',
                       'Read(nChunkSprite,LoadSave::IsNetworkSnapshot()?sizeof(nChunkSprite):sizeof(nChunkSprite[0])*nCurChunkNum);',
                       'Write(nChunkSprite,LoadSave::IsNetworkSnapshot()?sizeof(nChunkSprite):sizeof(nChunkSprite[0])*nCurChunkNum);',
                       'Read(&nChunkTotal,sizeof(nChunkTotal));',
                       'Write(&nChunkTotal,sizeof(nChunkTotal));'):
            if needle not in move:
                out.append('network snapshot allocator-pool contract changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read network snapshot allocator pools: %s' % exc)
    try:
        inv = re.sub(r'\s+', '', open(os.path.join(source_dir, 'kexinv.cpp')).read())
        for needle in ('classKexInventorySnapshotLoadSave:publicLoadSave',
                       'Read(KexInvList,sizeof(KexInvList));',
                       'Write(KexInvList,sizeof(KexInvList));',
                       'Read(s_campaignPlayer,sizeof(s_campaignPlayer));',
                       'Write(s_campaignPlayer,sizeof(s_campaignPlayer));'):
            if needle not in inv:
                out.append('KEX snapshot inventory contract changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read KEX snapshot inventory bridge: %s' % exc)
    try:
        net = re.sub(r'\s+', '', open(os.path.join(source_dir, 'vbnet.cpp')).read())
        for needle in ('VB_EXNET_SNAPSHOT_FILE',
                       'LoadSave::SaveNetworkSnapshot(path);',
                       'LoadSave::LoadNetworkSnapshot(path);',
                       'EXNETsnapshottest:PASS(all-playerEXSN,localspriteOK)'):
            if needle not in net:
                out.append('network snapshot round-trip probe changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read network snapshot round-trip probe: %s' % exc)
    return out


def verify_latejoin_protocol():
    """A reserved slot may join only through an atomic EXSN freeze/rebase."""
    out = []
    source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        net = re.sub(r'\s+', '', open(os.path.join(source_dir, 'vbnet.cpp')).read())
        for needle in ('EXNET_PT_LATE_ANNOUNCE',
                       'EXNET_PT_LATE_READY',
                       'EXNET_PT_LATE_FREEZE',
                       'EXNET_PT_LATE_FROZEN',
                       'EXNET_PT_LATE_SNAPSHOT',
                       'EXNET_PT_LATE_GO',
                       'staticvoidExNet_HostCheckLateJoin(void)',
                       'staticintExNet_SendLateSnapshot(void)',
                       'LoadSave::SaveNetworkSnapshot(path);',
                       'intExNet_LoadLateJoinSnapshot(void)',
                       'LoadSave::LoadNetworkSnapshot(path);',
                       'staticintExNet_ApplyLateRoster(void)',
                       'staticintExNet_SpawnLatePlayer(intplayer)',
                       'staticints_latePendingRejoin;',
                       'staticints_lateGoRejoin;',
                       'go[3]=(uint8_t)s_latePendingRejoin;',
                       'intconstrejoin=ExNet_SlotActive(slot)&&s_gone[slot]==2;',
                       'if(ExNet_SlotActive(slot)&&!rejoin)continue;',
                       'if(PlayerCount!=nTotalPlayers||PlayerList[player].someNetVal!=-1)',
                       'if(ExNet_SlotActive(slot)&&!s_gone[slot]&&!s_lateFrozen[slot])',
                       's_lateFreezeTick=s_netTick+EXNET_LATE_FREEZE_LEAD;',
                       's_sendTick=EXNET_INPUT_DELAY;',
                       'ExNet_FinishLatePromotion(player)'):
            if needle not in net:
                out.append('late-join protocol contract changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read late-join protocol: %s' % exc)
    try:
        game = re.sub(r'\s+', '', open(os.path.join(source_dir, 'exhumed.cpp')).read())
        for needle in ('ExNet_SetLateJoin(netLateJoin);',
                       'if(ExNet_IsLateJoiner())',
                       'if(!ExNet_LoadLateJoinSnapshot())',
                       'gotoLATEJOIN_READY;',
                       'if(!lateJoinRestored)ExNet_LevelStart();'):
            if needle not in game:
                out.append('late-join startup handoff changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read late-join startup handoff: %s' % exc)
    return out


def verify_directed_portal_runtime():
    """Dynamic Kex face destinations must not be overwritten as reciprocals."""
    out = []
    source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src',
                                          'kexbreak.cpp'))
    try:
        native = re.sub(r'\s+', '', open(source).read())
    except OSError as exc:
        return ['cannot read native directed-portal bridge: %s' % exc]
    for needle in (
            'staticvoidKexWall_ApplyPortals(',
            'if((unsigned)owner>=(unsigned)numsectors||s_kexDynamicWall[r.nextwall])',
            'intconsttargetIsDynamic=s_kexDynamicWall[r.nextwall];',
            '(!targetIsDynamic&&(wall[r.nextwall].nextwall!=nWall||wall[r.nextwall].nextsector!=owner))',
            'VB_KEX_PORTAL_TEST'):
        if needle not in native:
            out.append('native directed-portal runtime changed: %s' % needle)
    return out


def verify_campaign_metadata(z, kexdir, outdir):
    """Check music, overworld links, and the campaign start against mapInfo."""
    out = []
    info_name = next((name for name in z.namelist()
                      if name.lower() == 'defs/mapinfo.txt'), None)
    if info_name is None:
        return ['defs/mapInfo.txt is missing']
    info = z.read(info_name).decode('latin-1').replace('\r', '')
    music = [(K.KEX_MAP_PREFIX + os.path.basename(mapname).upper(), track)
             for mapname, track in re.findall(
                 r'map\s+"maps/([^"]+)".*?musicTrack\s+"([^"]+)"', info, re.S)]
    blocks = re.findall(r'Map\s+(\w+)\s*\{(.*?)\n\}', info, re.S)
    order = [ident for ident, _body in blocks]

    def field(body, key, default=''):
        match = re.search(key + r'\s+("?)([^"\n]*)\1', body)
        return match.group(2).strip() if match else default

    localizations = {}
    for code, language in Menu.UI_LANGUAGES:
        loc_name = next((name for name in z.namelist()
                         if name.lower() == 'localization/loc_%s.txt' % language), None)
        if loc_name is None:
            out.append('localization/loc_%s.txt is missing' % language)
            continue
        localization = z.read(loc_name).decode('utf-8').replace('\r', '')
        localizations[code] = dict(re.findall(r'(str_\d+)\s*=\s*"([^"]*)"',
                                               localization))
    if out:
        return out

    def world_rows(titles):
        expected_world = []
        for _ident, body in blocks:
            mapname = K.KEX_MAP_PREFIX + os.path.basename(field(body, 'map')).upper()
            # mapInfo owns the exact original-console pixels. The generated
            # UI records those beside the affine-transformed repaint position,
            # so both remaster overworld settings have auditable node placement.
            original_x = int(float(field(body, 'overworld_x', '0')))
            original_y = int(float(field(body, 'overworld_y', '0')))
            ox = original_x / float(Menu.OVERWORLD_ORIGINAL_W)
            oy = original_y / float(Menu.OVERWORLD_ORIGINAL_H)
            x = int(round((Menu.OW_AX + ox * Menu.OW_AW) * Menu.OVERWORLD_W))
            y = int(round((Menu.OW_AY + oy * Menu.OW_AH) * Menu.OVERWORLD_H))
            links = [order.index(field(body, 'nextmap_' + direction))
                     if field(body, 'nextmap_' + direction) in order else -1
                     for direction in ('north', 'east', 'south', 'west')]
            key = field(body, 'title').lstrip('$')
            expected_world.append((mapname, x, y, original_x, original_y,
                                   links, titles.get(key, key)))
        return expected_world

    expected_worlds = {code: world_rows(titles)
                       for code, titles in localizations.items()}
    expected_world = expected_worlds['en']

    try:
        actual_music = []
        music_path = os.path.join(outdir, 'kexmusic.txt')
        if os.path.getsize(music_path) > 8191:
            out.append('kexmusic.txt exceeds native 8191-byte table buffer')
        for line in open(music_path):
            cols = line.split()
            if len(cols) != 2:
                out.append('malformed kexmusic.txt row: %r' % line.rstrip())
                continue
            actual_music.append(tuple(cols))
        if actual_music != music:
            out.append('kexmusic.txt differs from defs/mapInfo.txt')

        for code, expected in expected_worlds.items():
            filename = 'kexworld.txt' if code == 'en' else 'kexworld_%s.txt' % code
            actual_world = []
            for line in open(os.path.join(outdir, filename), encoding='utf-8'):
                text = line.strip()
                if not text or text.startswith('#'):
                    continue
                cols = text.split(maxsplit=9)
                if len(cols) != 10:
                    out.append('malformed %s row: %r' % (filename, text))
                    continue
                try:
                    actual_world.append((cols[0], int(cols[1]), int(cols[2]),
                                         int(cols[3]), int(cols[4]),
                                         [int(v) for v in cols[5:9]], cols[9]))
                except ValueError:
                    out.append('non-integer %s link row: %r' % (filename, text))
            if actual_world != expected:
                out.append('%s map identity, positions, links, or titles differ '
                           'from the remaster metadata' % filename)

        start = open(os.path.join(outdir, 'kexstart.txt')).read().strip()
        if not expected_world or start != expected_world[0][0]:
            out.append('kexstart.txt is %r, expected %r' %
                       (start, expected_world[0][0] if expected_world else None))
    except OSError as exc:
        out.append('cannot read generated campaign metadata: %s' % exc)

    for _mapname, track in music:
        path = os.path.join(kexdir, track)
        try:
            with open(path, 'rb') as f:
                if f.read(4) != b'OggS':
                    out.append('campaign track is not an OGG: %s' % track)
        except OSError:
            out.append('campaign track is missing: %s' % track)
    for track in ('music/overworld.ogg', 'music/title.ogg', 'music/credu1.ogg'):
        try:
            with open(os.path.join(kexdir, track), 'rb') as f:
                if f.read(4) != b'OggS':
                    out.append('menu track is not an OGG: %s' % track)
        except OSError:
            out.append('menu track is missing: %s' % track)

    source = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native_source = open(os.path.join(source, 'exhumed.cpp')).read()
        native = re.sub(r'\s+', '', native_source)
        menu_source = open(os.path.join(source, 'kexmenu.cpp')).read()
        ramses_source = open(os.path.join(source, 'ramses.cpp')).read()
        ramses = re.sub(r'\s+', '', ramses_source)
        inv_header = open(os.path.join(source, 'kexinv.h')).read()
        for needle in ('kopen4loadfrommod("kexmusic.txt",0)',
                       'charconst*Kex_MusicForMap(charconst*mapname)',
                       'Kex_MusicForMap(gUserMapFilename)',
                       'staticchars_table[8192]'):
            if needle not in native:
                out.append('native campaign-music bridge changed: %s' % needle)
        # The campaign tracks deliberately live *outside* PowerslaveEX.kpf.
        # Mounting the archive alone keeps art working while silently losing
        # music, so preserve the paired -kex archive + install-directory path.
        mount = re.search(r'Bstrcasecmp\s*\(\s*pOpt\s*,\s*"kex"\s*\).*?'
                          r'G_AddGroup\s*\(\s*kpf\s*\)\s*;.*?'
                          r'G_AddPath\s*\(\s*kexdir\s*\)\s*;', native_source, re.S)
        if not mount:
            out.append('native -kex mount no longer adds its external asset directory')
        for needle in ('KexLanguage_VoiceIndex()',):
            if needle not in native:
                out.append('native remaster movie language fallback changed: %s' % needle)
        for needle in ('intKexLanguage_VoiceIndex()', 'language==4?0:language',
                       '"en","fr","de","es","it"'):
            if needle not in ramses:
                out.append('native remaster Italian media fallback changed: %s' % needle)
        capacity = re.search(r'#define\s+KEX_CAMPAIGN_MAX_MAPS\s+(\d+)', inv_header)
        if not capacity:
            out.append('native campaign map capacity is missing')
        elif len(expected_world) > int(capacity.group(1)):
            out.append('campaign has %d maps, exceeding native capacity %s' %
                       (len(expected_world), capacity.group(1)))
        if 'KexTable_Load(filename,KEX_CAMPAIGN_MAX_MAPS,KexMenu_ParseLevel)' not in re.sub(r'\s+', '', menu_source):
            out.append('native overworld no longer enforces its campaign map capacity')
        menu = re.sub(r'\s+', '', menu_source)
        for needle in ('playMusicFile("music/overworld.ogg",true)',
                       'playMusicFile("music/title.ogg",true)',
                       'playMusicFile("music/credu1.ogg",true)',
                       'Kex_MusicForMap(current)', 'KexLanguage_Code()',
                       '"kexworld_%s.txt"'):
            if needle not in menu:
                out.append('native remaster menu-music bridge changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native campaign-music bridge: %s' % exc)
    return out


def verify_inventory_metadata(z, outdir):
    """Keep remaster inventory/pickup wording tied to its localization source."""
    out = []
    names = {name.lower(): name for name in z.namelist()}

    def read(rel, encoding='latin-1'):
        real = names.get(rel.lower())
        return z.read(real).decode(encoding).replace('\r', '') if real else None

    dolls = read('defs/actors/teamdolls.txt')
    if dolls is None:
        return ['defs/actors/teamdolls.txt is missing']
    localizations = {}
    for code, language in Menu.UI_LANGUAGES:
        localization = read('localization/loc_%s.txt' % language, 'utf-8')
        if localization is None:
            out.append('localization/loc_%s.txt is missing' % language)
            continue
        localizations[code] = dict(re.findall(
            r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"', localization))
    if out:
        return out

    # Must match kexmenu.py: these are the KEX status/arsenal/artifact fields
    # that Build can represent.  Values intentionally retain escaped "\\n"
    # and "${0}" because KexInv_Format expands them at render time.
    inv_keys = [('status', 'str_088'), ('page0', 'str_044'), ('page1', 'str_045'),
                ('page2', 'str_046'), ('page3', 'str_047'),
                ('noarti', 'str_064'), ('doll', 'str_071'), ('trans', 'str_072'),
                ('transfull', 'str_073'), ('transn', 'str_074'), ('trans0', 'str_075'),
                ('maxhp', 'str_108'), ('mapname', 'str_111'),
                ('difficultytitle', 'm_select_difficulty'),
                ('difficulty0', 'm_difficulty_normal'),
                ('difficulty1', 'm_difficulty_hard'),
                ('difficulty2', 'm_difficulty_pharaoh')]
    inv_keys += [('weap%d' % i, 'str_%03d' % (56 + i)) for i in range(8)]
    inv_keys += [('arti%d' % i, 'str_%03d' % (65 + i)) for i in range(6)]
    inv_keys += [('artiname%d' % i, 'str_%03d' % (100 + i)) for i in range(6)]
    inv_keys += [(key, key) for key in (
        'm_new_game', 'm_load_game', 'm_options', 'm_about', 'm_quit',
        'm_game_paused', 'm_resume', 'm_reload_last_checkpoint',
        'm_restart_level', 'm_exit_title_screen', 'm_go_back', 'm_yes',
        'm_no', 'm_press_start', 'm_prompt_select', 'm_prompt_back',
        'm_prompt_menu', 'm_overwrite_game_confirm', 'm_quit_game_confirm',
        'm_reload_checkpoint_confirm', 'm_reload_level_confirm',
        'm_exit_game_confirm', 'm_empty_slot', 'm_audio_settings',
        'm_display_settings', 'm_antialiasing', 'm_anisotrophy', 'm_fov',
        'm_hudSettings', 'm_showPlayTime', 'm_hudtext', 'm_crosshairsize',
        'm_game_settings', 'm_checkpoints', 'm_overworldScreen',
        'm_new', 'm_original', 'm_viewBobSpeed', 'm_viewBobHeight',
        'm_turnSway', 'm_weaponJumpBobbing',
        'm_mouse_settings', 'm_msens_x', 'm_msens_y',
        'm_gamepad_settings', 'm_vibration', 'm_vibration_volume',
        'm_joylookspeed_x', 'm_joylookspeed_y', 'm_joymovesens',
        'm_joyinvertlook', 'm_deadzone', 'm_lstick', 'm_rstick',
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
    doll_names = [('dollname%d' % (int(number) - 1), text)
                  for number, text in re.findall(
                      r'Actor\s+TD_Doll(\d+).*?pickupMessage\s+"([^"]*)"',
                      dolls, re.S)]

    pickups = {
        10: 'str_149', 15: 'str_107', 24: 'str_110', 9: 'str_095',
        12: 'str_106', 41: 'str_109', 42: 'str_106', 23: 'str_101',
        28: 'str_093', 29: 'str_094', 30: 'str_096', 32: 'str_097',
        33: 'str_098', 45: 'str_112', 46: 'str_113', 47: 'str_114',
        48: 'str_115', 11: 'str_111', 31: 'str_099',
    }
    for code, strings in localizations.items():
        inv_filename = 'kexinv.txt' if code == 'en' else 'kexinv_%s.txt' % code
        pickup_filename = 'kexpickup.txt' if code == 'en' else 'kexpickup_%s.txt' % code
        expected_inv = [(key, strings[value]) for key, value in inv_keys if value in strings]
        expected_inv += doll_names
        expected_pickups = [(lotag, strings[key]) for lotag, key in sorted(pickups.items())
                            if key in strings]

        try:
            actual_inv = []
            for raw in open(os.path.join(outdir, inv_filename), encoding='utf-8'):
                line = raw.rstrip('\r\n')
                if not line or line.startswith('#'):
                    continue
                key, sep, text = line.partition(' ')
                if not sep or not key or not text:
                    out.append('malformed %s row: %r' % (inv_filename, line))
                    continue
                if len(text.encode('utf-8')) > 255:
                    out.append('%s value exceeds native 255-byte buffer: %s' %
                               (inv_filename, key))
                actual_inv.append((key, text))
            if actual_inv != expected_inv:
                out.append('%s differs from remaster localization/team-doll metadata' %
                           inv_filename)
        except OSError as exc:
            out.append('cannot read %s: %s' % (inv_filename, exc))

        try:
            actual_pickups = []
            pickup_path = os.path.join(outdir, pickup_filename)
            if os.path.getsize(pickup_path) > 4095:
                out.append('%s exceeds native 4095-byte table buffer' % pickup_filename)
            for raw in open(pickup_path, encoding='utf-8'):
                line = raw.rstrip('\r\n')
                if not line or line.startswith('#'):
                    continue
                lotag, sep, text = line.partition(' ')
                try:
                    number = int(lotag)
                except ValueError:
                    out.append('non-integer %s lotag: %r' % (pickup_filename, line))
                    continue
                if not sep or not text:
                    out.append('malformed %s row: %r' % (pickup_filename, line))
                    continue
                if len(line.encode('utf-8')) > 191:
                    out.append('%s row exceeds native 191-byte line buffer: %d' %
                               (pickup_filename, number))
                if len(text.encode('utf-8')) > 127:
                    out.append('%s value exceeds native 127-byte buffer: %d' %
                               (pickup_filename, number))
                actual_pickups.append((number, text))
            if actual_pickups != expected_pickups:
                out.append('%s differs from remaster localization metadata' % pickup_filename)
        except OSError as exc:
            out.append('cannot read %s: %s' % (pickup_filename, exc))

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        inventory = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexinv.cpp')).read())
        pickup = re.sub(r'\s+', '', open(os.path.join(srcdir, 'exhumed.cpp')).read())
        for needle in ('kopen4loadfrommod("kexinv.txt",0)', '"kexinv_%s.txt"',
                       'KexLanguage_Index()', 'chars_result[256]',
                       "p[0]=='\\\\'&&p[1]=='n'"):
            if needle not in inventory:
                out.append('native inventory-text bridge changed: %s' % needle)
        for needle in ('kopen4loadfrommod("kexpickup.txt",0)', '"kexpickup_%s.txt"',
                       'KexLanguage_Index()', 'chars_result[128]'):
            if needle not in pickup:
                out.append('native pickup-caption bridge changed: %s' % needle)
    except OSError as exc:
        out.append('cannot read native inventory-text bridge: %s' % exc)
    return out


def verify_menu_assets(z, outdir):
    """Audit the generated remaster menu/HUD art and its runtime contracts."""
    out = []
    try:
        deftext = open(os.path.join(outdir, 'kexmenu.def')).read()
        rows = re.findall(r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"',
                          deftext)
    except OSError as exc:
        return ['cannot read kexmenu.def: %s' % exc]
    tiles = {}
    for tile_text, rel in rows:
        tile = int(tile_text)
        if tile in tiles:
            out.append('kexmenu.def defines tile %d more than once' % tile)
        tiles[tile] = rel
        path = os.path.join(outdir, rel)
        try:
            if K.png_size(open(path, 'rb').read(32)) is None:
                out.append('menu tile %d is not a PNG: %s' % (tile, rel))
        except OSError:
            out.append('menu tile %d is missing: %s' % (tile, rel))
    if not tiles:
        return out + ['kexmenu.def contains no tile definitions']

    # Fixed-size items are passed to rotatesprite at their authored aspect.
    # A wrong extraction size visibly letterboxes a row, title card, or map.
    dimensions = {
        Menu.TILE_OVERWORLD: (Menu.OVERWORLD_W, Menu.OVERWORLD_H),
        Menu.TILE_OVERWORLD_ORIGINAL: (Menu.OVERWORLD_ORIGINAL_W,
                                       Menu.OVERWORLD_ORIGINAL_H),
        Menu.TILE_BACKDROP: (Menu.BACKDROP_W, Menu.BACKDROP_H),
        Menu.TILE_TITLE: (320, 200),
        Menu.TILE_ROW: (Menu.ROW_W, Menu.ROW_H),
        Menu.TILE_ROW_SEL: (Menu.ROW_W, Menu.ROW_H),
        Menu.TILE_BTN: (Menu.BTN_W, Menu.BTN_H),
        Menu.TILE_BTN_SEL: (Menu.BTN_W, Menu.BTN_H),
        Menu.TILE_BLACK: (8, 8), Menu.TILE_KEYCAP: (26, 14),
        Menu.TILE_MARKER: (9, 9),
    }
    for _source, tile, size in Menu.ART:
        if size:
            dimensions[tile] = size
    for tile, wanted in sorted(dimensions.items()):
        rel = tiles.get(tile)
        if rel is None:
            out.append('required menu tile %d is absent' % tile)
            continue
        try:
            actual = K.png_size(open(os.path.join(outdir, rel), 'rb').read(32))
            if actual != wanted:
                out.append('menu tile %d is %s, expected %s' % (tile, actual, wanted))
        except OSError:
            pass  # reported in the general census above

    # Each remaster overworld setting has a source-ordered cursor animation;
    # their consecutive tile ranges are how kexmenu.cpp discovers the counts.
    names = {name.lower(): name for name in z.namelist()}
    expected_eyes = {}
    for label, anim_rel, cursor, tile_base, arrows in (
            ('HD', 'sprites/misc/overworld_hd_anim.txt', 'mapcursor_hd',
             Menu.TILE_EYE_BASE,
             (Menu.TILE_ARROW_UP, Menu.TILE_ARROW_RIGHT,
              Menu.TILE_ARROW_DOWN, Menu.TILE_ARROW_LEFT)),
            ('original', 'sprites/misc/overworld_anim.txt', 'mapcursor',
             Menu.TILE_EYE_ORIGINAL_BASE,
             (Menu.TILE_ARROW_ORIGINAL_UP, Menu.TILE_ARROW_ORIGINAL_RIGHT,
              Menu.TILE_ARROW_ORIGINAL_DOWN, Menu.TILE_ARROW_ORIGINAL_LEFT))):
        anim_name = names.get(anim_rel)
        if anim_name is None:
            out.append('%s overworld cursor animation source is missing' % label)
            expected_eyes[label] = 0
            continue
        anim = z.read(anim_name).decode('latin-1').replace('\r', '')
        match = re.search(re.escape(cursor) + r'\s*\{(.*?)\n\}', anim, re.S)
        count = len(re.findall(r'"[^"]*"\s*,\s*(\d+)', match.group(1))) if match else 0
        if count == 0:
            out.append('%s overworld cursor animation has no frames' % label)
        expected_eyes[label] = count
        for frame in range(count):
            if tile_base + frame not in tiles:
                out.append('%s overworld cursor frame %d has no menu tile' % (label, frame))
        for tile in arrows:
            if tile not in tiles:
                out.append('%s overworld arrow tile %d is absent' % (label, tile))
    try:
        anim_rows = dict(line.split() for line in open(os.path.join(outdir, 'kexanim.txt'))
                         if line.strip() and not line.startswith('#'))
        actual_eyes = {
            'HD': int(anim_rows.get('eyeframes', '-1')),
            'original': int(anim_rows.get('originaleyeframes', '-1')),
        }
        for label, expected in expected_eyes.items():
            if actual_eyes[label] != expected:
                out.append('kexanim.txt %s overworld cursor count is %d, expected %d' %
                           (label, actual_eyes[label], expected))
    except (OSError, ValueError, IndexError):
        out.append('kexanim.txt is missing or malformed')

    try:
        # kexmenu.py deliberately does not slice glyphs for localization keys
        # used only by remaster screens Build does not implement.  Audit the
        # strings that the generated Build UI can actually display instead.
        font_chars = set(range(32, 127))
        for code, _language in Menu.UI_LANGUAGES:
            inv_filename = 'kexinv.txt' if code == 'en' else 'kexinv_%s.txt' % code
            pickup_filename = 'kexpickup.txt' if code == 'en' else 'kexpickup_%s.txt' % code
            world_filename = 'kexworld.txt' if code == 'en' else 'kexworld_%s.txt' % code
            for filename, columns in ((inv_filename, 1), (pickup_filename, 1),
                                      (world_filename, 10)):
                for raw in open(os.path.join(outdir, filename), encoding='utf-8'):
                    row = raw.rstrip('\r\n')
                    if not row or row.startswith('#'):
                        continue
                    text = (row.partition(' ')[2] if columns == 1
                            else (row.split(maxsplit=9)[9] if len(row.split(maxsplit=9)) == 10 else ''))
                    font_chars.update(ord(ch) for ch in text if ord(ch) >= 32)
        extra_chars = sorted(font_chars - set(range(32, 127)))

        glyphs = {}
        for raw in open(os.path.join(outdir, 'kexfont.txt'), encoding='utf-8'):
            cols = raw.split()
            if not cols or cols[0] == '#' or cols[0] == 'scale':
                continue
            if len(cols) != 6:
                out.append('malformed kexfont.txt row: %r' % raw.rstrip())
                continue
            cp, tile, alt_tile, _w, _h, _yoff = (int(value) for value in cols)
            if cp in glyphs:
                out.append('duplicate kexfont codepoint %d' % cp)
            glyphs[cp] = (tile, alt_tile)
        if not font_chars <= set(glyphs):
            out.append('kexfont.txt does not cover every emitted localized menu codepoint')
        extra_tiles = []
        extra_alt_tiles = []
        for cp, (tile, alt_tile) in glyphs.items():
            if cp < 127:
                want_tile = Menu.TILE_FONT_BASE + cp - 32
                want_alt_tile = Menu.TILE_FONT_ALT + cp - 32
                if tile != want_tile or alt_tile != want_alt_tile:
                    out.append('font codepoint %d has tiles %d/%d, expected %d/%d' %
                               (cp, tile, alt_tile, want_tile, want_alt_tile))
            else:
                extra_tiles.append(tile)
                extra_alt_tiles.append(alt_tile)
            if tile < 0 or alt_tile < 0 or tile not in tiles or alt_tile not in tiles:
                out.append('font codepoint %d lacks its menu tile pair' % cp)
        # The converter also keeps glyphs for a few supported remaster strings
        # not yet displayed by Build.  Their exact codepoint ordering is an
        # implementation detail, but their tile ranges must remain compact and
        # collision-free, after the inventory art and before creature art.
        if sorted(extra_tiles) != list(range(Menu.TILE_FONT_EXTRA_BASE,
                                             Menu.TILE_FONT_EXTRA_BASE + len(extra_tiles))):
            out.append('extended gold font tiles are not a compact unique range')
        if sorted(extra_alt_tiles) != list(range(Menu.TILE_FONT_ALT_EXTRA_BASE,
                                                 Menu.TILE_FONT_ALT_EXTRA_BASE + len(extra_alt_tiles))):
            out.append('extended blue font tiles are not a compact unique range')
    except (OSError, UnicodeDecodeError, ValueError):
        out.append('kexfont.txt is missing or malformed')

    try:
        main_def = open(os.path.join(outdir, 'exhumed.def')).read()
        if not re.search(r'^include\s+"kexmenu\.def"\s*$', main_def, re.M):
            out.append('exhumed.def does not include kexmenu.def')
        native_source = open(os.path.join(
            os.path.dirname(__file__), '..', 'src', 'kexmenu.cpp')).read()
        native = re.sub(r'\s+', '', native_source)
        for needle in ('KexTable_Load("kexfont.txt",INT32_MAX,KexMenu_ParseGlyph)',
                       'tilesiz[kKexTileBackdrop].x>0&&tilesiz[kKexTilePanel].x>0',
                       'KexMenu_Glyph(\'A\')', 'KexMenu_NextCodepoint',
                       '#defineKEX_MAX_GLYPHS128', 'KexMenu_Options()',
                       'KexMenu_Display()', 'm_display_settings', 'm_antialiasing',
                       'm_anisotrophy', 'KexRender_SetFov',
                       'KexRender_SetAnisotropy',
                       'KexMenu_Game()', 'KexMenu_Language()', 'kex_language=sel;',
                       'kex_original_overworld',
                       'KexMenu_TextWidth(label,textHeight)>textWidth',
                       'while(textHeight>6', 'CONTROL_GetInput(&input)',
                       'BUTTON(gamefunc_Open)', 'LastSeenInput::Joystick',
                       'CONTROL_LastSeenInput=LastSeenInput::Keyboard'):
            if needle not in native:
                out.append('native remaster menu bridge changed: %s' % needle)
        tile_ids = {
            'kKexTileBackdrop': Menu.TILE_BACKDROP,
            'kKexTilePanel': Menu.TILE_PANEL,
            'kKexTileCursor': Menu.TILE_CURSOR,
            'kKexTileRow': Menu.TILE_ROW,
            'kKexTileRowSel': Menu.TILE_ROW_SEL,
            'kKexTileBtn': Menu.TILE_BTN,
            'kKexTileBtnSel': Menu.TILE_BTN_SEL,
            'kKexTileCursorR': Menu.TILE_CURSOR_R,
            'kKexTileOverworld': Menu.TILE_OVERWORLD,
            'kKexTileOverworldOriginal': Menu.TILE_OVERWORLD_ORIGINAL,
            'kKexTileMarker': Menu.TILE_MARKER,
            'kKexTileArrow': Menu.TILE_ARROW_UP,
            'kKexTileArrowUp': Menu.TILE_ARROW_UP,
            'kKexTileArrowRight': Menu.TILE_ARROW_RIGHT,
            'kKexTileArrowDown': Menu.TILE_ARROW_DOWN,
            'kKexTileArrowLeft': Menu.TILE_ARROW_LEFT,
            'kKexTileKeycap': Menu.TILE_KEYCAP,
            'kKexTileBlack': Menu.TILE_BLACK,
            'kKexTileTitle': Menu.TILE_TITLE,
            'kKexTileEyeBase': Menu.TILE_EYE_BASE,
            'kKexTileEyeOriginalBase': Menu.TILE_EYE_ORIGINAL_BASE,
            'kKexTileArrowOriginalUp': Menu.TILE_ARROW_ORIGINAL_UP,
            'kKexTileArrowOriginalRight': Menu.TILE_ARROW_ORIGINAL_RIGHT,
            'kKexTileArrowOriginalDown': Menu.TILE_ARROW_ORIGINAL_DOWN,
            'kKexTileArrowOriginalLeft': Menu.TILE_ARROW_ORIGINAL_LEFT,
            'kKexFontBase': Menu.TILE_FONT_BASE,
            'kKexFontAlt': Menu.TILE_FONT_ALT,
            'kKexTileHudHeadL': Menu.TILE_HUD_HEAD_L,
            'kKexTileHudHeadR': Menu.TILE_HUD_HEAD_R,
            'kKexTileHudMainL': Menu.TILE_HUD_MAIN_L,
            'kKexTileHudMainR': Menu.TILE_HUD_MAIN_R,
            'kKexTileHudRed': Menu.TILE_HUD_RED,
            'kKexTileHudBlue': Menu.TILE_HUD_BLUE,
            'kKexTileHudScarab': Menu.TILE_HUD_SCARAB,
            'kKexTileHudTrough': Menu.TILE_HUD_TROUGH,
            'kKexTileHudAirBack': Menu.TILE_HUD_AIR_BACK,
            'kKexTileHudAirFront': Menu.TILE_HUD_AIR_FRONT,
            'kKexTileHudDolphin': Menu.TILE_HUD_DOLPHIN,
            'kKexTileHudVulture': Menu.TILE_HUD_VULTURE,
            'kKexTileHudSpark': Menu.TILE_HUD_SPARK,
            'kKexTileHudWings': Menu.TILE_HUD_WINGS,
            'kKexTileHudWingsR': Menu.TILE_HUD_WINGS_R,
            'kKexTileInvWeapon': Menu.TILE_INV_WEAPON,
            'kKexTileInvKey': Menu.TILE_INV_KEY,
            'kKexTileInvArtifact': Menu.TILE_INV_ARTIFACT,
            'kKexTileInvTrans': Menu.TILE_INV_TRANS,
            'kKexTileInvTransOn': Menu.TILE_INV_TRANS_ON,
            'kKexTileInvDoll': Menu.TILE_INV_DOLL,
            'kKexTileInvMapClosed': Menu.TILE_INV_MAP_CLOSED,
            'kKexTileInvMapOpen': Menu.TILE_INV_MAP_OPEN,
            'kKexTileInvPlateL': Menu.TILE_INV_PLATE_L,
            'kKexTileInvPlateR': Menu.TILE_INV_PLATE_R,
            'kKexTileInvArrowL': Menu.TILE_INV_ARROW_L,
            'kKexTileInvArrowR': Menu.TILE_INV_ARROW_R,
            'kKexTileInvSlot': Menu.TILE_INV_SLOT,
        }
        for name, wanted in tile_ids.items():
            match = re.search(r'\b%s\s*=\s*(\d+)' % name, native_source)
            if not match or int(match.group(1)) != wanted:
                out.append('menu tile ID %s is %s, expected %d' %
                           (name, match.group(1) if match else 'missing', wanted))
    except OSError as exc:
        out.append('cannot read native remaster menu bridge: %s' % exc)
    return out


def verify_steep_terrain(z, outdir):
    """Keep COLONY_SAT's extreme static crater walls on the fine terrace.

    Its two 416-z-per-world-unit floors exceed Build's 128-unit slope cap,
    so they cannot be made exact. Five contour levels and a centred capped
    tilt bring their worst sampled error below 1152 Build z without pushing
    any map near the engine sector/wall limits. This catches an accidental
    return to the old coarse four-level terrace (about 3000 Build z wrong).
    """
    name = next((n for n in z.namelist() if n.lower() == 'maps/colony_sat.map'), None)
    if name is None:
        return ['COLONY_SAT source map is missing']
    path = os.path.join(outdir, K.KEX_MAP_PREFIX + 'COLONY_SAT.MAP')
    if not os.path.exists(path):
        return ['COLONY_SAT was not converted']
    m = K.locate(z.read(name))
    convsects, convwalls, convranges = K.convert(m)
    keep, _remap = K.sector_remap(convsects, convranges)
    build_sectors, _walls = load_build(path)
    targets = {104, 111}
    errors = []
    for build_index, sector in enumerate(build_sectors):
        if build_index >= len(keep):
            continue
        source = m.kex_sector_src[keep[build_index]]
        if source not in targets:
            continue
        srcsec = m.sectors[source]
        floor = m.faces[min(srcsec.wallEnd + 2, m.nface - 1)]
        plane = K._plane_build(m, floor)
        points = sector['pts']
        if plane is None or len(points) < 3:
            continue
        cx = sum(x for x, _y in points) / len(points)
        cy = sum(y for _x, y in points) / len(points)
        for x, y in [(cx, cy)] + [((cx + px) / 2, (cy + py) / 2) for px, py in points]:
            if _inside(points, x, y):
                errors.append(abs(_zat(sector, x, y, 'f') - K._plane_z(plane, x, y)))
    if not errors:
        return ['COLONY_SAT steep crater sectors have no emitted samples']
    worst = max(errors)
    if worst > 1152:
        return ['COLONY_SAT steep-terrain error is %.0f Build z, expected <= 1152' % worst]
    return []


# ---------------------------------------------------------------------------
# The event audits: what the converter should have made of each Kex event,
# checked against the written level. Each of these caught a real fault once
# (see EXHUMED_UNIFY.md, "The completability audit" and after).
# ---------------------------------------------------------------------------
GUARDED = {1: 1, 3: 1, 4: 1, 5: 1, 6: 1, 2: 24, 7: 24, 22: 8, 21: 9, 25: 9,
           24: 12, 8: 75, 9: 75, 66: 35}
LISTEN = {2, 7, 8, 9, 21, 22, 24, 25, 48, 49}
FIRE = {23, 50, 70, 53, 200, 201, 202, 203, 66}
BROADCAST = {1, 3, 4, 5, 6}
RULE_TAGS = {'SUNKEN': {1001, 1002, 2, 1007}, 'SELBUROW': {1010, 1004}, 'SETARENA': {1010, 1002},
             'KILARENA': {1010}, 'TOMB': {666, 1020}, 'TOMBEND': {0}, 'SANCTUAR': {1000},
             'KILENTRY': {1000, 1001, 1002, 1003, 1004, 1005}, 'SHRINE': {1001}, 'PEAK': {1005}, 'THOTH': {1003}}
MOVERS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 21, 22, 23, 24, 25, 49, 66, 70, 52, 53, 54, 63, 200, 201, 202, 203}
MARKERS = {72, 73, 74, 81, 84, 85, 86, 87, 64, 65, 66, 67, 68, 69, 71, 77, 82, 83, 94, 95, 96, 98, 99}


def audit(m, path, base, z, outdir):
    """Findings as strings; empty when the level is as it should be."""
    import collections
    out = []
    sects, walls = load_build(path)
    d = open(path, 'rb').read(); o = 4 + 4 * 3 + 2 + 2
    start_z = struct.unpack_from('<i', d, 12)[0]
    ns = struct.unpack_from('<H', d, o)[0]; o += 2
    tags = [struct.unpack_from('<hh', d, o + 40 * i + 34) for i in range(ns)]
    o += 40 * ns; nw = struct.unpack_from('<H', d, o)[0]
    wbase = o + 2
    wlot = [struct.unpack_from('<h', d, wbase + 32 * i + 26)[0] for i in range(nw)]
    wcstat = [struct.unpack_from('<h', d, wbase + 32 * i + 14)[0] for i in range(nw)]
    wover = [struct.unpack_from('<h', d, wbase + 32 * i + 18)[0] for i in range(nw)]
    wextra = [struct.unpack_from('<h', d, wbase + 32 * i + 30)[0] for i in range(nw)]
    o = wbase + 32 * nw; nsp = struct.unpack_from('<H', d, o)[0]; o += 2
    sprs = [(struct.unpack_from('<h', d, o + 44 * i + 24)[0],)
            + struct.unpack_from('<hhh', d, o + 44 * i + 38) for i in range(nsp)]
    sprxy = [struct.unpack_from('<ii', d, o + 44 * i) for i in range(nsp)]
    sprz = [struct.unpack_from('<i', d, o + 44 * i + 8)[0]
            for i in range(nsp)]
    sprvel = [struct.unpack_from('<hhh', d, o + 44 * i + 32) for i in range(nsp)]

    # Guarded events: the sector (first m.nsector are the sources) carries the expected lotag.
    untagged = collections.Counter()
    by_sector = collections.defaultdict(set)
    for et, es, tag, par in m.events:
        by_sector[es].add(et)
    for et, es, tag, par in m.events:
        if et not in GUARDED or es >= ns or tags[es][0] % 1000 == GUARDED[et]:
            continue
        # A trigger door doubled with a press door comes out as the press door
        # (which still listens on the channel), and a lift level with every
        # neighbour has nowhere to travel: neither is a fault.
        if et in (2, 7) and by_sector[es] & {1, 3, 4, 5, 6} and tags[es][0] % 1000 == 1:
            continue
        if et in (24, 21, 25, 22):
            wp, wn = sects[es]['wp'], sects[es]['wn']
            nbf = set(sects[walls[w][4]]['fz'] for w in range(wp, wp + wn) if walls[w][4] >= 0)
            if not (nbf - {sects[es]['fz']}):
                continue
        untagged[K.KEX_EVENTS.get(et, et)] += 1
    for k, v in sorted(untagged.items()):
        out.append("%d %s event(s) left without their effect" % (v, k))
    unknown_events = collections.Counter(
        et for et, es, _tag, _par in m.events
        if et not in K.KEX_EVENTS and not (et == 0 and es == 65535))
    if unknown_events:
        out.append("used Kex event type(s) have no conversion: %s" % sorted(unknown_events.items()))

    # Key doors: the thousands are a bitmask (1 2 4 8), never 3 or 5..7.
    bad = sum(1 for lo, hi in tags if lo % 1000 == 1 and hi >= 1000 and (hi // 1000) not in (1, 2, 4, 8))
    if bad:
        out.append("%d key door(s) with a key number instead of a key bit" % bad)

    # Dangling tags: listened on, fired by nothing the level or a rule provides.
    fired = set(); listened = set()
    for et, es, tag, par in m.events:
        if tag in (-1, 0):                # 0 is shared specially, -1 is no tag
            continue
        if et in FIRE or et in BROADCAST: fired.add(tag)
        if et in LISTEN: listened.add(tag)
    fired |= RULE_TAGS.get(base, set())
    # The focused Kex test maps deliberately put a player-operated lift on a
    # fresh tag without an authored remote emitter. The retail audit still
    # catches dangling graph edges; test maps prove direct-entry behavior.
    if not base.startswith('TEST_'):
        for t in sorted(listened - fired):
            out.append("tag %d is listened on but nothing fires it" % t)

    # Mover sectors cut into pieces (the same Kex source behind several Build sectors).
    # Pieces are appended after the sources; a mover source that spawned any is a fault.
    convsects, convwalls, convranges = K.convert(m)
    _keep, convremap = K.sector_remap(convsects, convranges)
    convback = {new: old for old, new in convremap.items()}
    # The writer rounds Kex X/Y coordinates to the integer Build grid.  A
    # contour seam may be valid in source space yet collapse to a point after
    # that round.  Besides creating a bad wall, such a first edge makes a
    # Build slope flat, so audit every emitted edge rather than only looking
    # for an already-sloped first edge in the written map.
    collapsed = []
    for si, (start, count) in enumerate(convranges):
        if convsects[si] is None:
            continue
        for wi in range(start, start + count):
            w = convwalls[wi]
            end = convwalls[w['point2']]
            if w['x'] == end['x'] and w['y'] == end['y']:
                collapsed.append((si, wi - start))
    if collapsed:
        out.append("%d emitted wall edge(s) collapse on Build's integer grid: %s" %
                   (len(collapsed), collapsed[:8]))
    dropped_sources = [i for i in range(m.nsector)
                       if i >= len(convsects) or convsects[i] is None]
    if dropped_sources:
        out.append("source sector(s) have no Build representation: %s" % dropped_sources[:24])

    pieces = collections.Counter(m.kex_sector_src)
    cut = sum(1 for et, es, tag, par in m.events if et in MOVERS and pieces[es] > 1)
    if cut:
        out.append("%d mover sector(s) cut into pieces" % cut)

    # FLOOR_TRIGGER_PLATE is a real kexFloor, not Build's static lotag-21
    # trigger. Kex first raises its floor by 16 source units, then lowers it
    # at two units per 60 Hz update and fires the channel on arrival. The
    # marker carries that original target z, while the written sector keeps
    # the raised starting floor.
    plate_rise = 16 * K.SCALE * K.WORLD
    expected_plates = []
    for et, es, tag, _par in m.events:
        if et != 23 or es not in convremap:
            continue
        cs = convsects[es]
        if cs is None:
            continue
        expected_plates.append((convremap[es], tag, cs['floorz'] + plate_rise))
    actual_plates = [(sprs[i][0], sprs[i][2], sprz[i])
                     for i in range(nsp) if sprs[i][1] == 84]
    if collections.Counter(actual_plates) != collections.Counter(expected_plates):
        missing = collections.Counter(expected_plates) - collections.Counter(actual_plates)
        extra = collections.Counter(actual_plates) - collections.Counter(expected_plates)
        out.append('floor-trigger plate markers differ: missing %s extra %s'
                   % (list(missing.elements())[:8], list(extra.elements())[:8]))
    for sec, _tag, target in expected_plates:
        if sec >= len(sects) or sects[sec]['fz'] != target - plate_rise:
            out.append('floor-trigger plate sector %d is not raised by %d Build z'
                       % (sec, plate_rise))

    # ArtifactEvent() has the opposite ownership direction from an ordinary
    # pickup: type 53 takes the already-held artifact, fires 1000 + its bit,
    # then creates it at each type-54 floor-triangle centre. The converted map
    # therefore has construction-only pads (68) and destinations (86), never
    # a visible conditional artifact placed at load.
    shrine_pads, shrine_spawns = K._artifact_shrine_rows(m, convremap, base)
    expected_pads = [(sect, artifact) for _map, sect, artifact in shrine_pads]
    expected_spawns = [(sect, artifact, x, y, z)
                       for _map, sect, artifact, x, y, z in shrine_spawns]
    actual_pads = [(sect, hitag) for sect, lotag, hitag, _extra in sprs if lotag == 68]
    actual_spawns = [(sect, hitag, sprxy[i][0], sprxy[i][1], sprz[i])
                     for i, (sect, lotag, hitag, _extra) in enumerate(sprs)
                     if lotag == 86]
    if collections.Counter(actual_pads) != collections.Counter(expected_pads):
        out.append('artifact-shrine pad markers differ: expected %s, emitted %s'
                   % (sorted(expected_pads), sorted(actual_pads)))
    if collections.Counter(actual_spawns) != collections.Counter(expected_spawns):
        out.append('artifact-shrine destination markers differ: expected %s, emitted %s'
                   % (sorted(expected_spawns), sorted(actual_spawns)))
    if base == 'KILENTRY':
        if len(expected_pads) != 6 or len(expected_spawns) != 6:
            out.append('KILENTRY artifact-shrine census changed: pads %d destinations %d'
                       % (len(expected_pads), len(expected_spawns)))
        planted = [(sect, lotag, hitag) for sect, lotag, hitag, _extra in sprs
                   if lotag in (23, K.KEX_INV_LOTAG_ARTIFACT)]
        if planted:
            out.append('KILENTRY starts with visible shrine artifact pickup(s): %s'
                       % planted[:8])

    # Generic Build movers run at 30 Hz. The mapper encodes the Kex source-rate
    # band in their lotag thousands: source speed four -> 768 Build z/update;
    # slow close type 9 -> 192 (selected by runlist.cpp). The custom plates and
    # paired DROP_PAD controller below deliberately have no native lotag mover.
    rate_for = {1: 2, 2: 2, 3: 2, 4: 2, 5: 2, 6: 2, 7: 2,
                8: 2, 9: 1, 21: 2, 22: 2, 24: 2, 25: 2}
    bad_rates = []
    for et, es, _tag, _par in m.events:
        rate = rate_for.get(et)
        effect = GUARDED.get(et)
        if rate is None or effect is None or es not in convremap:
            continue
        lotag = tags[convremap[es]][0]
        if lotag % 1000 == effect and lotag // 1000 != rate:
            bad_rates.append((es, K.KEX_EVENTS.get(et, et), lotag, rate))
    if bad_rates:
        out.append('Kex mover speed band differs: %s' % bad_rates[:12])

    # DROP_PAD is not a Build lotag-6 elevator. The Kex loader swaps its
    # event sector with params: every split parameter-sector piece is a floor
    # and every original-sector piece is a linked ceiling. The generated
    # table is the native controller's complete geometry contract.
    expected_pads = []
    for group, piece, ceiling, tag in getattr(m, 'kex_drop_surfaces', ()):
        if piece in convremap:
            expected_pads.append((group, convremap[piece], ceiling, tag))
    actual_pads = []
    try:
        with open(os.path.join(outdir, 'kexpad.txt')) as f:
            for line in f:
                cols = line.split()
                if len(cols) == 5 and cols[0].upper() == base:
                    actual_pads.append(tuple(map(int, cols[1:])))
    except OSError as exc:
        out.append('paired drop-pad table is missing: %s' % exc)
    if collections.Counter(actual_pads) != collections.Counter(expected_pads):
        missing = collections.Counter(expected_pads) - collections.Counter(actual_pads)
        extra = collections.Counter(actual_pads) - collections.Counter(expected_pads)
        out.append('paired drop-pad surfaces differ: missing %s extra %s'
                   % (list(missing.elements())[:8], list(extra.elements())[:8]))
    for _group, piece, _ceiling, _tag in expected_pads:
        if tags[piece][0] % 1000 == 6:
            out.append('paired drop-pad surface %d still has generic lotag 6' % piece)

    pad_groups = collections.defaultdict(list)
    for group, piece, ceiling, tag in expected_pads:
        pad_groups[group].append((piece, ceiling, tag))
    for group, surfaces in sorted(pad_groups.items()):
        floors = [piece for piece, ceiling, _tag in surfaces if not ceiling]
        ceilings = [piece for piece, ceiling, _tag in surfaces if ceiling]
        if not floors or not ceilings:
            out.append('paired drop-pad group %d lacks a floor or linked ceiling' % group)
            continue
        heights = {sects[piece]['fz'] - sects[piece]['cz'] for piece in ceilings}
        if len(heights) != 1 or next(iter(heights)) <= 0:
            out.append('paired drop-pad group %d has inconsistent travel heights: %s'
                       % (group, sorted(heights)))

    # Tags outside Build's direct channel range need their generated allotment
    # so a completed pad can receive its Kex Reset() event later in the map.
    try:
        with open(os.path.join(outdir, 'kexchan.txt')) as f:
            pad_channels = {(os.path.splitext(cols[0].upper())[0], int(cols[1]))
                            for line in f for cols in [line.split()]
                            if len(cols) == 3}
        for tag in sorted({_tag for _group, _piece, _ceiling, _tag in expected_pads}):
            if (tag < -1 or tag >= 1000) and (K.KEX_MAP_PREFIX + base, tag) not in pad_channels:
                out.append('paired drop-pad tag %d has no channel allotment' % tag)
    except OSError as exc:
        out.append('drop-pad channel table is missing: %s' % exc)

    # Directed face collision and visibility. Kex's Solid bit is independent
    # of texture, Invisible and Underwater, while its two actor-selective bits
    # travel through a private negative wall.extra marker. Check the exact
    # emitted wall order used by write_build(), including one-way portals.
    ordered = []
    for old in _keep:
        st, num = convranges[old]
        ordered.extend(convwalls[st:st + num])
    if len(ordered) != nw:
        out.append("converted wall count differs: expected %d, emitted %d" % (len(ordered), nw))
    else:
        bad_solid = []
        bad_open = []
        bad_marker = []
        bad_invisible = []
        marker_for = {
            K.KEX_WALL_CLIP_AI_FALLERS: K.KEX_WALL_EXTRA_BLOCK_AI_FALLERS,
            K.KEX_WALL_CLIP_NONPLAYERS: K.KEX_WALL_EXTRA_BLOCK_NONPLAYERS,
            K.KEX_WALL_CLIP_AI_FALLERS | K.KEX_WALL_CLIP_NONPLAYERS:
                K.KEX_WALL_EXTRA_BLOCK_BOTH,
        }
        for wi, (cw, cstat, over, extra) in enumerate(zip(ordered, wcstat, wover, wextra)):
            flags = cw.get('_kex_face_flags')
            if flags is None:
                continue
            if flags & K.KEX_FACE_SOLID:
                if (cstat & (1 | 64)) != (1 | 64):
                    bad_solid.append(wi)
            elif cstat & (1 | 64):
                bad_open.append(wi)
            expected_extra = marker_for.get(cw.get('_kex_clip', 0), -1)
            if expected_extra != -1 and extra != expected_extra:
                bad_marker.append((wi, extra, expected_extra))
            if cw.get('_kex_invisible') and ((cstat & 16) or ('overpicnum' in cw and over <= 0)):
                bad_invisible.append(wi)
        if bad_solid:
            out.append("Solid Kex face lacks Build collision on wall(s) %s" % bad_solid[:12])
        if bad_open:
            out.append("nonsolid Kex face gained static collision on wall(s) %s" % bad_open[:12])
        if bad_marker:
            out.append("directed actor-clipping marker differs on wall(s) %s" % bad_marker[:12])
        if bad_invisible:
            out.append("Invisible Kex face draws or lost force-field identity on wall(s) %s"
                       % bad_invisible[:12])

    # Edges with vertically stacked neighbours are carried in kexportal.txt.
    # Compare every topology/bounds/flag field (picnum is globally allocated by
    # convert_install, while this audit's standalone convert uses local slots).
    portal_path = os.path.join(os.path.dirname(path), 'kexportal.txt')
    actual_portals = []
    if os.path.exists(portal_path):
        with open(portal_path) as pf:
            for line in pf:
                p = line.split()
                if not p or p[0].startswith('#') or p[0].upper() != base:
                    continue
                try:
                    actual_portals.append(tuple(int(v) for v in p[1:]))
                except ValueError:
                    out.append("malformed kexportal row: %s" % line.strip())
    portal_entries = K._kex_portal_entries(base, convsects, convwalls, convranges)
    expected_portals = [tuple(r[1:]) for r, _metadata in portal_entries]
    actual_shape = [r[:-1] for r in actual_portals]
    expected_shape = [r[:-1] for r in expected_portals]
    if actual_shape != expected_shape:
        missing = collections.Counter(expected_shape) - collections.Counter(actual_shape)
        extra = collections.Counter(actual_shape) - collections.Counter(expected_shape)
        out.append("height-selected portal rows differ: %d missing, %d extra"
                   % (sum(missing.values()), sum(extra.values())))
    bad_portal_pic = sum(1 for r in actual_portals if r[-1] < 0)
    if bad_portal_pic:
        out.append("%d height-selected portal row(s) have an invalid picnum" % bad_portal_pic)

    # Authored Toggle geometry. Compare the complete logical-face table,
    # recursive closure masks and every converted wall/vertical side against
    # a fresh conversion of the KEX source. This catches both dropped small
    # openings embedded in longer collinear walls and stale side tables.
    toggle_path = os.path.join(os.path.dirname(path), 'kextoggle.txt')
    actual_toggle = []
    if os.path.exists(toggle_path):
        with open(toggle_path) as tf:
            for line in tf:
                p = line.split()
                if not p or p[0].startswith('#') or p[0].upper() != base:
                    continue
                try:
                    if p[1] not in ('F', 'W', 'S'):
                        raise ValueError
                    actual_toggle.append((p[1],) + tuple(int(v, 0) for v in p[2:]))
                except (IndexError, ValueError):
                    out.append("malformed kextoggle row: %s" % line.strip())

    expected_toggle = []
    for row in K.kex_toggle_rows(m, base, convsects, convwalls, convranges):
        expected_toggle.append((row[1],) + tuple(
            int(v, 0) if isinstance(v, str) else int(v) for v in row[2:]))
    if actual_toggle != expected_toggle:
        missing = collections.Counter(expected_toggle) - collections.Counter(actual_toggle)
        extra = collections.Counter(actual_toggle) - collections.Counter(expected_toggle)
        out.append("authored Toggle rows differ: %d missing, %d extra"
                   % (sum(missing.values()), sum(extra.values())))

    logical_toggle = {r[1] for r in expected_toggle if r[0] == 'F'}
    represented_toggle = {r[1] for r in expected_toggle if r[0] in ('W', 'S')}
    missing_representation = sorted(logical_toggle - represented_toggle)
    if missing_representation:
        out.append("logical Toggle face(s) have no runtime geometry: %s"
                   % missing_representation[:16])

    bad_toggle_closure = []
    for r in actual_toggle:
        if r[0] != 'F' or len(r) != 14:
            continue
        face = r[1]
        words = r[6:14]
        if (face not in logical_toggle
                or not (words[face >> 5] & (1 << (face & 31)))
                or any(words[bit >> 5] & (1 << (bit & 31))
                       for bit in range(len(logical_toggle), K.KEX_TOGGLE_MAX_FACES))):
            bad_toggle_closure.append(face)
    if bad_toggle_closure:
        out.append("Toggle closure masks are malformed for face(s) %s"
                   % bad_toggle_closure[:16])

    bad_toggle_wall = []
    dynamic_portals = {r[:-2] for r in actual_portals}
    for r in actual_toggle:
        if r[0] != 'W' or len(r) != 11:
            continue
        (_op, face, wi, nextwall, nextsector, top0, bottom0, top1, bottom1,
         dynamic, clearreverse) = r
        if (face not in logical_toggle or not (0 <= wi < len(walls))
                or not (0 <= nextwall < len(walls))
                or not (0 <= nextsector < len(sects))
                or top0 > bottom0 or top1 > bottom1
                or dynamic not in (0, 1) or clearreverse not in (0, 1)):
            bad_toggle_wall.append((face, wi, 'shape'))
            continue
        if not dynamic and (walls[wi][3] != nextwall or walls[wi][4] != nextsector):
            bad_toggle_wall.append((face, wi, 'static topology'))
        if dynamic and (wi, nextwall, nextsector, top0, bottom0, top1, bottom1) \
                not in dynamic_portals:
            bad_toggle_wall.append((face, wi, 'portal binding'))
    if bad_toggle_wall:
        out.append("Toggle wall topology differs for %s" % bad_toggle_wall[:12])

    seam_sides = collections.defaultdict(set)
    seam_sectors = collections.defaultdict(lambda: collections.defaultdict(set))
    for r in actual_toggle:
        if r[0] != 'S' or len(r) != 5:
            continue
        _op, face, seam, nsector, cf = r
        if face not in logical_toggle or not (0 <= nsector < len(sects)) or cf not in (0, 1):
            seam_sides[seam].add(-1)
        else:
            seam_sides[seam].add(cf)
            seam_sectors[seam][cf].add(nsector)
    bad_seams = sorted(seam for seam, sides in seam_sides.items() if sides != {0, 1})
    if bad_seams:
        out.append("Toggle vertical seam(s) lack matching ceiling/floor sides: %s"
                   % bad_seams[:16])

    # YAX wall links use equal complete directed edges. Several retail split
    # rings have two lower walls beginning at one vertex, so checking only the
    # first coordinate can silently join unrelated rooms.
    ambiguous_seam_edges = []
    for seam, bycf in seam_sectors.items():
        lower = []
        for nsector in bycf[0]:
            lower.extend(range(sects[nsector]['wp'],
                               sects[nsector]['wp'] + sects[nsector]['wn']))
        endpoints = collections.defaultdict(list)
        for wi in lower:
            point2 = walls[wi][2]
            if 0 <= point2 < len(walls):
                endpoints[walls[wi][:2] + walls[point2][:2]].append(wi)
        for nsector in bycf[1]:
            for wi in range(sects[nsector]['wp'],
                            sects[nsector]['wp'] + sects[nsector]['wn']):
                point2 = walls[wi][2]
                if not (0 <= point2 < len(walls)):
                    continue
                matches = endpoints[walls[wi][:2] + walls[point2][:2]]
                if len(matches) > 1:
                    ambiguous_seam_edges.append((seam, wi, matches[:3]))
    if ambiguous_seam_edges:
        out.append("Toggle vertical seam has ambiguous full-edge matches: %s"
                   % ambiguous_seam_edges[:12])

    # The serialized word after the low face flags is the compiler's map-local
    # Moveable group, not padding or the high half of the flags. Kex uses it to
    # keep doors/lifts dynamic after their static mesh is compiled. Build
    # already reevaluates its wall spans when a sector surface moves, while the
    # height-selected layer binds a face endpoint only when it coincides with a
    # live ceiling/floor. A locked door/lift face may instead describe the
    # fully open envelope: that endpoint equals either the grouped surface's
    # source height or its authored target and intentionally remains fixed.
    source_face_owner = {}
    move_surfaces = collections.defaultdict(list)
    for si, source_sector in enumerate(m.sectors):
        for fi in range(source_sector.faceStart, min(source_sector.wallEnd + 1, m.nface)):
            source_face_owner[fi] = (si, 'w')
        for offset, surface in ((1, 'c'), (2, 'f')):
            fi = source_sector.wallEnd + offset
            if fi < m.nface:
                source_face_owner[fi] = (si, surface)
                group = m.faces[fi].moveGroup
                if group >= 0:
                    move_surfaces[group].append((si, surface))

    runtime_wall_owner = {wi: si for si, s in enumerate(sects)
                          for wi in range(s['wp'], s['wp'] + s['wn'])}
    bad_move_driver = []
    bad_move_bound = []
    for row, metadata in portal_entries:
        face_index = metadata.get('face', -1)
        group = metadata.get('moveGroup', -1)
        if (not (0 <= face_index < m.nface)
                or source_face_owner.get(face_index, (-1, '?'))[1] != 'w'
                or m.faces[face_index].moveGroup != group):
            bad_move_driver.append((face_index, group))
            continue
        if group < 0:
            continue
        drivers = move_surfaces.get(group, ())
        if not drivers:
            bad_move_driver.append((face_index, group))
            continue
        (_map, wi, _nextwall, nsector, top0, bottom0, top1, bottom1,
         _flags, _picnum) = row
        owner = runtime_wall_owner.get(wi, -1)
        if owner < 0 or not (0 <= nsector < len(sects)) or not (0 <= wi < len(walls)):
            bad_move_bound.append((face_index, group, 'topology'))
            continue
        point2 = walls[wi][2]
        if not (0 <= point2 < len(walls)):
            bad_move_bound.append((face_index, group, 'point2'))
            continue
        for endpoint, (top, bottom) in enumerate(((top0, bottom0), (top1, bottom1))):
            npoint = wi if endpoint == 0 else point2
            x, y = walls[npoint][:2]
            for surface, value in (('c', top), ('f', bottom)):
                live = any(abs(value - _zat(sects[side], x, y, surface)) <= 8
                           for side in (owner, nsector))
                fixed = False
                for driver_sector, driver_surface in drivers:
                    if driver_surface != surface:
                        continue
                    source_sector = m.sectors[driver_sector]
                    driver_face = m.faces[source_sector.wallEnd
                                          + (1 if surface == 'c' else 2)]
                    authored = K._plane_z(K._plane_build(m, driver_face), x, y)
                    target = -struct.unpack_from(
                        '<h', m.data, m.secOff + driver_sector * 20
                        + (6 if surface == 'c' else 8))[0] * K.SCALE * K.WORLD
                    if abs(value - authored) <= 8 or abs(value - target) <= 8:
                        fixed = True
                        break
                if not live and not fixed:
                    bad_move_bound.append((face_index, group, surface, endpoint))
    if bad_move_driver:
        out.append("moveable portal face/group lacks its compiled flat driver: %s"
                   % bad_move_driver[:12])
    if bad_move_bound:
        out.append("moveable portal endpoint is neither live-bound nor in its authored envelope: %s"
                   % bad_move_bound[:12])

    # Runtime portal endpoints that coincide with a moving surface are bound
    # to it before the floater's load-time offset and cosine motion begin.
    # Prove every row bordering one of those surfaces has the exact endpoint
    # needed for that binding (within Build's slope-rounding tolerance).
    float_path = os.path.join(os.path.dirname(path), 'kexfloat.txt')
    float_surfaces = {}
    if os.path.exists(float_path):
        with open(float_path) as ff:
            for line in ff:
                p = line.split()
                if (not p or p[0].startswith('#') or p[0].upper() != base
                        or len(p) < 4):
                    continue
                try:
                    float_surfaces[int(p[2])] = bool(int(p[3]))
                except ValueError:
                    out.append("malformed kexfloat row: %s" % line.strip())
    wall_owner = runtime_wall_owner
    bad_float_portals = 0
    for r in actual_portals:
        wi, _nextwall, nsector, top0, bottom0, top1, bottom1, _flags, _picnum = r
        if wi not in wall_owner or not (0 <= walls[wi][2] < len(walls)):
            continue
        points = (walls[wi][:2], walls[walls[wi][2]][:2])
        for nside in (wall_owner[wi], nsector):
            ceiling = float_surfaces.get(nside)
            if ceiling is None:
                continue
            values = (top0, top1) if ceiling else (bottom0, bottom1)
            surface = 'c' if ceiling else 'f'
            if any(abs(value - _zat(sects[nside], point[0], point[1], surface)) > 8
                   for value, point in zip(values, points)):
                bad_float_portals += 1
    if bad_float_portals:
        out.append("%d portal/floating-surface endpoint binding(s) differ"
                   % bad_float_portals)

    # Switches: every switch event got at least one lotag-7 wall.
    nsw = sum(1 for e in m.events if 200 <= e[0] <= 203); nwl = sum(1 for l in wlot if l == 7)
    if nsw > nwl:
        out.append("%d switch event(s) but only %d switch wall(s)" % (nsw, nwl))

    # Checkpoints use their original KEX actor-array index as persistent ID.
    # A reload uses this ID to reactivate all pads touched before death, so a
    # collapsed or renumbered hitag would make geographically distinct pads
    # alias one another.
    expected_cp = {i for i, a in enumerate(m.actors) if a.type == 2000}
    emitted_cp = [hi for _sect, lo, hi, _extra in sprs if lo == 59]
    if set(emitted_cp) != expected_cp or len(emitted_cp) != len(set(emitted_cp)):
        out.append("checkpoint IDs differ: expected %s, emitted %s"
                   % (sorted(expected_cp), sorted(emitted_cp)))

    # Every converted source actor carries -(source index + 2) in raw sprite
    # extra. Runtime decodes it before constructors reuse extra, allowing a
    # checkpoint to suppress actors removed before it without stale runlists.
    actor_ids = [-extra - 2 for _sect, _lo, _hi, extra in sprs if extra <= -2]
    if len(actor_ids) != len(set(actor_ids)) or any(i < 0 or i >= len(m.actors) for i in actor_ids):
        out.append("authored actor IDs are duplicated or out of range")
    expected_actor_ids = {i for i, actor in enumerate(m.actors)
                          if actor.type != 0 and actor.sector >= 0}
    unexpected_actor_ids = sorted(set(actor_ids) - expected_actor_ids)
    if unexpected_actor_ids:
        out.append("off-map/invalid source actor(s) were emitted: %s"
                   % unexpected_actor_ids[:24])
    missing_actor_ids = sorted(expected_actor_ids - set(actor_ids))
    if missing_actor_ids:
        out.append("valid source actor(s) have no emitted sprite: %s" % missing_actor_ids[:24])

    # kexWorld::SpawnMapActor() passes the compiled position to the player
    # factory too. Keep the map header's initial Z exact instead of replacing
    # an authored airborne/sloped start with the sector's nominal floor.
    player = next((actor for actor in m.actors
                   if actor.type == K.AT_PLAYER and actor.sector >= 0), None)
    if player is not None:
        expected_start_z = int(round(-player.z * K.SCALE * K.WORLD))
        if start_z != expected_start_z:
            out.append("player start Z differs from KEX source: %d, expected %d"
                       % (start_z, expected_start_z))

    # KEX's SpawnMapActor() passes every map actor's authored x/y/z into its
    # factory. The native Build factories decide themselves whether a given
    # actor belongs on the floor, so the converted map must not pre-flatten
    # this input. Check the serialized Z for every native gameplay actor;
    # this covers grounded Mummy/Spider as well as airborne Bird/Wasp/Fish.
    native_z = {}
    for actor_id, actor in enumerate(m.actors):
        ent = K.ACTOR_MAP.get(actor.type)
        if actor.sector < 0 or ent is None or ent[0] < 100:
            continue
        native_z[actor_id] = (ent[0],
                              int(round(-actor.z * K.SCALE * K.WORLD)))
    emitted_native_z = {}
    for sprite_index, (_sect, lotag, _hitag, extra) in enumerate(sprs):
        actor_id = -extra - 2
        wanted = native_z.get(actor_id)
        if wanted is not None and lotag == wanted[0]:
            emitted_native_z.setdefault(actor_id, []).append(sprz[sprite_index])
    missing_native_z = sorted(set(native_z) - set(emitted_native_z))
    wrong_native_z = [(actor_id, values, native_z[actor_id][1])
                      for actor_id, values in sorted(emitted_native_z.items())
                      if values != [native_z[actor_id][1]]]
    if missing_native_z:
        out.append("native actor(s) lack authored-Z sprites: %s" % missing_native_z[:24])
    if wrong_native_z:
        out.append("native actor Z differs from KEX source: %s" % wrong_native_z[:12])

    # Logical Kex tags must have one-to-one Build channel allotments. In
    # particular, dormant (-1) traps use a private pseudo-tag rather than the
    # old hard-coded channel 999, which collided with live high tags.
    cmap = K._channels(m)
    if len(cmap.values()) != len(set(cmap.values())):
        out.append("distinct Kex tags share a Build channel")
    wrong_trap_channel = []
    wrong_trap_metadata = []
    for sprite_index, (_sect, lo, hi, extra) in enumerate(sprs):
        if extra > -2:
            continue
        actor_id = -extra - 2
        if not (0 <= actor_id < len(m.actors)):
            continue
        a = m.actors[actor_id]
        trap = K.KEX_TRAP_ACTORS.get(a.type)
        if trap is None:
            continue
        tagkey = K.KEX_TAG0 if a.tag == 0 else a.tag
        if trap == K.KEX_TRAP_LOTAG_BEAM:
            expected = 1000 if a.tag == -1 else K._chan(m, tagkey)
        else:
            expected = K._chan(m, K.KEX_DEAD_TRAP if a.tag == -1 else tagkey)
        if lo != trap or hi != expected:
            wrong_trap_channel.append((actor_id, a.tag, hi, expected))
        if trap != K.KEX_TRAP_LOTAG_BEAM:
            xvel, yvel, _zvel = sprvel[sprite_index]
            if xvel != actor_id or yvel != -(a.sector + 1):
                wrong_trap_metadata.append(
                    (actor_id, xvel, actor_id, yvel, -(a.sector + 1)))
    if wrong_trap_channel:
        out.append("trap channels differ for %s" % wrong_trap_channel[:8])
    if wrong_trap_metadata:
        out.append("trap actor/source-sector metadata differs for %s"
                   % wrong_trap_metadata[:8])

    # A source actor's tag+1 can remove the active factory in that actor's
    # sector, regardless of the actor's class. The converter carries one
    # lotag-72 construction record per such actor so runtime can preserve the
    # original actor-array ordering even across split Build sectors.
    factory_sectors = {a.sector for a in m.actors
                       if a.type in (25, 126, 128) and a.sector >= 0}
    expected_stops = []
    for actor_id, a in enumerate(m.actors):
        if a.sector not in factory_sectors or a.tag + 1 == -1:
            continue
        stop_tag = a.tag + 1
        stop_key = K.KEX_TAG0 if stop_tag == 0 else stop_tag
        expected_stops.append((actor_id, a.sector, K._chan(m, stop_key)))
    actual_stops = []
    for sprite_index, (_sect, lo, hi, _extra) in enumerate(sprs):
        if lo != K.KEX_TRAP_LOTAG_STOP:
            continue
        xvel, yvel, _zvel = sprvel[sprite_index]
        actual_stops.append((xvel, -yvel - 1, hi))
    if sorted(actual_stops) != sorted(expected_stops):
        out.append("trap factory stop records differ: expected %s, emitted %s"
                   % (sorted(expected_stops)[:12], sorted(actual_stops)[:12]))

    # REMOTE_TRIGGER is a source dispatcher, not Build's static lotag-21
    # touchplate. Its construction marker retains the raw tag, allocated
    # channel and one-shot retirement condition independently.
    expected_remote = [(sect, chan, rawtag, one_shot)
                       for _map, sect, rawtag, chan, one_shot
                       in K._remote_trigger_rows(m, convremap, base)]
    actual_remote = []
    for sprite_index, (sect, lotag, hitag, _extra) in enumerate(sprs):
        if lotag != 87:
            continue
        xvel, yvel, _zvel = sprvel[sprite_index]
        actual_remote.append((sect, hitag, xvel, yvel))
    if collections.Counter(actual_remote) != collections.Counter(expected_remote):
        out.append("remote-trigger markers differ: expected %s, emitted %s"
                   % (sorted(expected_remote)[:12], sorted(actual_remote)[:12]))
    remote_sectors = {sect for sect, _chan, _tag, _one in expected_remote}
    still_generic = [sect for sect in remote_sectors
                     if sect < len(tags) and tags[sect][0] % 1000 == 21]
    if still_generic:
        out.append("remote-trigger sectors reverted to generic lotag 21: %s"
                   % still_generic[:12])
    wrong_sector = []
    for sect, _lo, _hi, extra in sprs:
        if extra > -2:
            continue
        actor_id = -extra - 2
        if not (0 <= actor_id < len(m.actors)) or m.actors[actor_id].sector < 0:
            continue
        old = convback.get(sect)
        got = m.kex_sector_src[old] if old is not None else -1
        if got != m.actors[actor_id].sector:
            wrong_sector.append((actor_id, m.actors[actor_id].sector, got))
    if wrong_sector:
        out.append("authored actor sector differs for %s" % wrong_sector[:8])
    cp_meta = {-extra - 2 for _sect, lo, _hi, extra in sprs if lo == 59 and extra <= -2}
    if cp_meta != expected_cp:
        out.append("checkpoint actor metadata differs: expected %s, emitted %s"
                   % (sorted(expected_cp), sorted(cp_meta)))

    # SCRIPTED_MOVE (event 49) supplies a linked source sector in params.
    # KEX moves the event sector's ceiling together with that partner's floor;
    # the construction marker carries the final Build partner in xvel + 1.
    expected_scripted = []
    for etype, source, tag, partner in m.events:
        if etype != 49 or source not in convremap:
            continue
        expected_scripted.append((source, tag,
                                  convremap.get(partner, -1)))
    actual_scripted = []
    for sprite_index, (sect, lotag, hitag, _extra) in enumerate(sprs):
        if lotag != 64:
            continue
        source = convback.get(sect)
        xvel, _yvel, _zvel = sprvel[sprite_index]
        actual_scripted.append((source, hitag, xvel - 1))
    if sorted(actual_scripted) != sorted(expected_scripted):
        out.append("scripted-sector links differ: expected %s, emitted %s"
                   % (sorted(expected_scripted), sorted(actual_scripted)))

    # Sector flag 0x20 is Kex's "No standing on edges" collision rule. The
    # first Build piece retains each source sector's index, so every surviving
    # flagged source must at least carry its lotag-83 marker there. Additional
    # markers on split descendants are expected and checked by the converter's
    # runtime count.
    expected_noedge = {i for i, s in enumerate(m.sectors) if s.raw[9] & 32 and i < ns}
    emitted_noedge = {sect for sect, lo, _hi, _extra in sprs if lo == 83}
    if not expected_noedge.issubset(emitted_noedge):
        out.append("no-standing-edge markers missing from source sector(s) %s"
                   % sorted(expected_noedge - emitted_noedge))

    # Sprites inside a solid sector (floor on ceiling) that are not markers.
    solid = sum(1 for sect, lo, hi, _extra in sprs if 0 <= sect < ns and lo and lo not in MARKERS
                and sects[sect]['fz'] - sects[sect]['cz'] <= 0 and tags[sect][0] % 1000 != 35)   # a bomb wall may hide a pickup
    if solid:
        out.append("%d sprite(s) inside a solid sector" % solid)
    return out


def verify_decor(z, outdir):
    """The retail light actors must retain their authored loops and phase bit.

    randomizeFrames is an initial phase offset, not noAdvanceFrames. The bug
    this catches emitted one PNG for every fire and consequently no animation
    row at all.
    """
    out = []
    table = os.path.join(outdir, 'kexdecor.txt')
    try:
        rows = [tuple(map(int, line.split())) for line in open(table)
                if line.strip() and not line.startswith('#')]
    except (OSError, ValueError):
        return ['kexdecor.txt is missing or malformed']

    bybase = {}
    claimed = {}
    valid_rows = []
    for row in rows:
        if len(row) != 4:
            out.append('animated scenery row has %d columns: %s' % (len(row), row))
            continue
        base, count, ticks, randomphase = row
        if base <= 0 or count <= 1 or ticks <= 0 or randomphase not in (0, 1):
            out.append('invalid animated scenery row: %s' % (row,))
            continue
        if base in bybase:
            out.append('duplicate animated scenery base tile %d' % base)
        bybase[base] = row
        valid_rows.append(row)
        for tile in range(base, base + count):
            if tile in claimed:
                out.append('animated scenery tile %d overlaps bases %d and %d'
                           % (tile, claimed[tile], base))
            claimed[tile] = base

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    tilefiles = {int(tile): path for tile, path in re.findall(
        r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}
    global_anims = [(int(a), int(b)) for a, b in re.findall(
        r'^animtilerange\s+(\d+)\s+(\d+)\s+', deftext, re.M)]

    for base, count, ticks, randomphase in valid_rows:
        dims = set()
        hashes = set()
        for tile in range(base, base + count):
            rel = tilefiles.get(tile)
            if not rel:
                out.append('animated scenery tile %d has no tilefromtexture' % tile)
                continue
            path = os.path.join(outdir, rel)
            try:
                blob = open(path, 'rb').read()
                if blob[:8] != b'\x89PNG\r\n\x1a\n' or len(blob) < 24:
                    raise ValueError
                dims.add(struct.unpack_from('>II', blob, 16))
                hashes.add(hashlib.sha256(blob).digest())
            except (OSError, ValueError, struct.error):
                out.append('animated scenery frame is missing or invalid: %s' % path)
        if len(dims) != 1:
            out.append('animated scenery base %d does not share one canvas: %s'
                       % (base, sorted(dims)))
        if len(hashes) < 2:
            out.append('animated scenery base %d has no visual frame change' % base)
        if any(lo <= base < hi + 1 or base <= lo < base + count
               for lo, hi in global_anims):
            out.append('animated scenery base %d is also globally animtilerange-driven' % base)

    actors = K.load_kex_actors(z)
    names = {n.lower(): n for n in z.namelist()}

    def authored_cadence(actor):
        frames = K.parse_anim_full(z, names, actor['anim'])
        delays = {f['delay'] for f in frames}
        speed = actor.get('animspeed', .5)
        if (not frames or len(delays) != 1 or next(iter(delays)) <= 0
                or speed <= 0):
            return frames, None
        ticks = 2.0 * next(iter(delays)) / speed
        rounded = int(round(ticks))
        return frames, rounded if rounded > 0 and abs(ticks - rounded) < 1e-6 else None

    expected = 0
    for actor_type, actor in sorted(actors.items()):
        if ('light source' not in (actor.get('group') or '').lower()
                or not actor.get('randomize') or actor.get('still')):
            continue
        authored, ticks = authored_cadence(actor)
        if len(authored) <= 1 or ticks is None:
            continue
        expected += 1
        match = re.search(r'tilefromtexture\s+(\d+)\s*\{\s*file\s+'
                          r'"kexart/actors/a%03d_0\.png"' % actor_type, deftext)
        if not match:
            out.append('%s has no generated frame zero' % actor['name'])
            continue
        base = int(match.group(1))
        row = bybase.get(base)
        wanted = (base, len(authored), ticks, 1)
        if row != wanted:
            out.append('%s animation is %s, expected %s' % (actor['name'], row, wanted))
    if expected == 0:
        out.append('retail actor census found no randomized animated light sources')

    # Every generated decor row must retain all authored poses, including the
    # 12-frame pyramid, 16-frame ankhs/helicopter and the slower frame-delay
    # loops. The old non-light eight-frame/15-Hz fallback shortened all of
    # them, while skull_candle_idle's delay 3 ran three times too fast.
    for base, count, ticks, randomphase in valid_rows:
        rel = tilefiles.get(base, '')
        match = re.fullmatch(r'kexart/actors/a(\d+)_0\.png', rel)
        if not match:
            out.append('animated scenery row %d is not sourced from an actor tile: %s'
                       % (base, rel))
            continue
        actor = actors.get(int(match.group(1)))
        if not actor:
            out.append('animated scenery row %d names unknown actor type %s'
                       % (base, match.group(1)))
            continue
        authored, expected_ticks = authored_cadence(actor)
        if len(authored) <= 1 or expected_ticks is None:
            out.append('%s has unsupported authored scenery cadence' % actor['name'])
            continue
        wanted = (base, len(authored), expected_ticks,
                  int(bool(actor.get('randomize'))))
        if (base, count, ticks, randomphase) != wanted:
            out.append('%s animation is %s, expected %s'
                       % (actor['name'], (base, count, ticks, randomphase), wanted))
    return out


def verify_breakables(z, outdir, include_testmaps=False):
    """Every generated shootable prop keeps its authored local-sound recipe."""
    out = []
    table = os.path.join(outdir, 'kexbreak.txt')
    rows = []
    try:
        for line in open(table):
            if not line.strip() or line.startswith('#'):
                continue
            row = tuple(map(int, line.split()))
            if len(row) != 13:
                out.append('breakable row has %d columns, expected 13: %s'
                           % (len(row), row))
                continue
            rows.append(row)
    except (OSError, ValueError):
        return ['kexbreak.txt is missing or malformed']

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    actor_base = {}
    for tile, actor_type in re.findall(
            r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"kexart/actors/a(\d+)_0\.png"',
            deftext):
        actor_base[int(actor_type)] = int(tile)

    actors = K.load_kex_actors(z)
    names = {n.lower(): n for n in z.namelist()}
    expected = {}
    puff_types = []

    # The retail definitions contain the twelve numbered vases and the barrel.
    # Obj_Vase11 has a complete authored recipe but is intentionally absent
    # from the 25 campaign maps; the optional test maps place it.  Keep the
    # authored and generated censuses separate so a missing actor tile or
    # metadata row is reported at the correct layer.
    authored_puff_types = []
    for actor in actors.values():
        ordinary = bool(actor.get('still') and actor.get('shootable')
                        and actor.get('death')
                        and len(K._all_frames(z, names, actor['anim'])) > 1)
        if not ordinary:
            continue
        effect = K._death_spawn_effect(z, names, actor['death'])
        if effect != 'Fx_VaseExplodePuff':
            out.append('%s ordinary death effect is %s'
                       % (actor['name'], effect))
        else:
            authored_puff_types.append(actor['name'])

    for actor_type, base in sorted(actor_base.items()):
        actor = actors.get(actor_type)
        if not actor:
            continue
        mine = actor.get('anim') == 'misc/magic8ball_idle'
        ordinary = bool(actor.get('still') and actor.get('shootable')
                        and actor.get('death')
                        and len(K._all_frames(z, names, actor['anim'])) > 1)
        exploding = False
        if (not ordinary and actor.get('shootable') and actor.get('death')
                and actor.get('randomize') and K._death_blast(z, names, actor['death'])):
            exploding = bool(K._all_frames(
                z, names, actor['death'].replace('_death', '_shards')))
        if not (mine or ordinary or exploding):
            continue
        recipe = (K.KEX_BREAK_SOUND_NONE if mine
                  else K._death_sound(z, names, actor['death']))
        expected[base] = (actor['name'], recipe,
                          int(round(actor.get('height', 0))))
        if ordinary:
            effect = K._death_spawn_effect(z, names, actor['death'])
            if effect == 'Fx_VaseExplodePuff':
                puff_types.append(actor['name'])

    bytile = {}
    for row in rows:
        tile, recipe = row[0], row[11]
        if tile in bytile:
            out.append('duplicate breakable base tile %d' % tile)
        bytile[tile] = row
        if recipe not in range(K.KEX_BREAK_SOUND_FLESH + 1):
            out.append('breakable tile %d has invalid sound recipe %d' % (tile, recipe))
        if row[9] != int(recipe == K.KEX_BREAK_SOUND_WOOD):
            out.append('breakable tile %d legacy wood bit disagrees with recipe %d'
                       % (tile, recipe))

    for tile, (name, recipe, spawnheight) in sorted(expected.items()):
        row = bytile.get(tile)
        if row is None:
            out.append('%s has no breakable metadata row' % name)
        elif row[11] != recipe:
            out.append('%s break sound is %d, authored recipe is %d'
                       % (name, row[11], recipe))
        elif row[12] != spawnheight:
            out.append('%s spawn height is %d, authored actor height is %d'
                       % (name, row[12], spawnheight))
    for tile in sorted(set(bytile) - set(expected)):
        out.append('unexpected breakable metadata for tile %d' % tile)

    expected_counts = tuple(sum(recipe == i for _name, recipe, _height in expected.values())
                            for i in range(K.KEX_BREAK_SOUND_FLESH + 1))
    actual_counts = tuple(sum(row[11] == i for row in rows)
                          for i in range(K.KEX_BREAK_SOUND_FLESH + 1))
    # Eleven ordinary vase types are placed in the campaign: ten glass plus
    # Vase09's flesh recipe.  The barrel is wood; the two exploding vases and
    # Magic 8 Ball have no local break sound.  Test maps add Vase11, the
    # eleventh glass type.
    wanted_counts = (3, 11 if include_testmaps else 10, 1, 1)
    if expected_counts != wanted_counts:
        out.append('retail break-sound census changed: expected %s, found %s'
                   % (wanted_counts, expected_counts))
    if actual_counts != wanted_counts:
        out.append('converted break-sound census differs: expected %s, found %s'
                   % (wanted_counts, actual_counts))
    if len(authored_puff_types) != 13:
        out.append('authored Fx_VaseExplodePuff type census changed from 13 to %d'
                   % len(authored_puff_types))
    unplaced_puff_types = set(authored_puff_types) - set(puff_types)
    wanted_puff_count = 13 if include_testmaps else 12
    wanted_unplaced = set() if include_testmaps else {'Obj_Vase11'}
    if len(puff_types) != wanted_puff_count or unplaced_puff_types != wanted_unplaced:
        out.append('placed Fx_VaseExplodePuff census changed: %d placed, unplaced %s'
                   % (len(puff_types), sorted(unplaced_puff_types)))

    # AI_Magic8Ball's death animation is a precise countdown, not an
    # approximate gameplay delay: ChangeAnim immediately runs detonate frame
    # zero (setting health to 30), five detonate frames reach the loop, and
    # then 29 loop callbacks decrement it to one and select the explosion.
    enemy_real = names.get('scripts/p_enemy.txt')
    enemy_text = z.read(enemy_real).decode('latin-1').replace('\r', '') \
        if enemy_real else ''

    def function_body(text, name):
        match = re.search(r'\bvoid\s+' + re.escape(name) + r'\s*\(', text)
        if not match:
            return ''
        try:
            return K._brace_block(text, match.end())[0]
        except ValueError:
            return ''

    def anim_body(text, name):
        match = re.search(r'^' + re.escape(name) + r'\s*$', text, re.M)
        if not match:
            return ''
        try:
            return K._brace_block(text, match.end())[0]
        except ValueError:
            return ''

    start_countdown = re.sub(r'\s+', '', function_body(enemy_text,
                                                           'A_StartCountdown'))
    detonate_countdown = re.sub(r'\s+', '', function_body(enemy_text,
                                                              'A_DetonateCountdown'))
    if 'actor.GetHealth()=30;' not in start_countdown:
        out.append('Magic 8 Ball countdown start health is no longer 30')
    for token in ('if(actor.GetHealth()<=1){return;}',
                  'if(--actor.GetHealth()==1)',
                  'magic8ball_explode_water', 'magic8ball_explode'):
        if token not in detonate_countdown:
            out.append('Magic 8 Ball countdown contract changed: %s' % token)

    mine_anim_real = names.get('sprites/misc/magic8ball_anim.txt')
    mine_anim_text = z.read(mine_anim_real).decode('latin-1').replace('\r', '') \
        if mine_anim_real else ''
    detonate_body = anim_body(mine_anim_text, 'magic8ball_detonate')
    loop_body = anim_body(mine_anim_text, 'magic8ball_detonate_loop')
    detonate_frames = len(K.parse_anim_full(z, names, 'misc/magic8ball_detonate'))
    loop_frames = len(K.parse_anim_full(z, names, 'misc/magic8ball_detonate_loop'))
    if (detonate_frames != 5 or loop_frames != 3
            or 'A_StartCountdown' not in detonate_body
            or 'A_PlayLocalSound("sounds/mineact.wav")' not in detonate_body
            or 'goto "misc/magic8ball_detonate_loop"' not in detonate_body
            or loop_body.count('A_DetonateCountdown') != loop_frames):
        out.append('Magic 8 Ball detonate animation contract changed')
    expected_mine_fuse = detonate_frames + (30 - 2)
    if K.KEX_MINE_FUSE != expected_mine_fuse:
        out.append('Magic 8 Ball fuse is %d, expected %d from KEX animation'
                   % (K.KEX_MINE_FUSE, expected_mine_fuse))
    mine_actor_types = [actor_type for actor_type, actor in actors.items()
                        if actor['name'] == 'AI_Magic8Ball']
    if len(mine_actor_types) != 1:
        out.append('Magic 8 Ball actor type census changed: %s' % mine_actor_types)
    mine_actor_type = mine_actor_types[0] if len(mine_actor_types) == 1 else 13
    mine_tiles = [tile for tile, (name, _recipe, _height) in expected.items()
                  if name == 'AI_Magic8Ball']
    if len(mine_tiles) != 1:
        out.append('Magic 8 Ball actor tile census changed: %s' % mine_tiles)
    elif mine_tiles[0] not in bytile:
        out.append('Magic 8 Ball has no breakable metadata row')
    else:
        mine_row = bytile[mine_tiles[0]]
        if mine_row[7] != K.KEX_MINE_SIGHT * K.WORLD:
            out.append('Magic 8 Ball sight is %d, expected %d'
                       % (mine_row[7], K.KEX_MINE_SIGHT * K.WORLD))
        if mine_row[8] != expected_mine_fuse:
            out.append('Magic 8 Ball metadata fuse is %d, expected %d'
                       % (mine_row[8], expected_mine_fuse))
        expected_art_frames = 1 + detonate_frames + loop_frames
        if K.KEX_MINE_ART_FRAMES != expected_art_frames:
            out.append('Magic 8 Ball art frame count is %d, expected %d'
                       % (K.KEX_MINE_ART_FRAMES, expected_art_frames))
        mine_art = []
        for frame in range(expected_art_frames):
            tile = mine_tiles[0] + frame
            rel = 'kexart/actors/a%03d_%d.png' % (mine_actor_type, frame)
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('Magic 8 Ball countdown tile %d is absent from exhumed.def'
                           % tile)
                continue
            mine_art.append(os.path.join(outdir, rel))
        try:
            import kexpng as _png
            dims = {_png.read(path)[:2] for path in mine_art}
            if len(mine_art) != expected_art_frames or len(dims) != 1:
                out.append('Magic 8 Ball countdown art does not use one %d-frame canvas'
                           % expected_art_frames)
        except (OSError, ValueError, AssertionError, struct.error) as exc:
            out.append('Magic 8 Ball countdown PNG audit failed: %s' % exc)

    effects = z.read(names['defs/actors/effects.txt']).decode('latin-1').replace('\r', '')
    actor_match = re.search(r'Actor\s+Fx_VaseExplodePuff\s*\{(.*?)\n\}',
                            effects, re.S)
    actor_compact = re.sub(r'\s+', '', actor_match.group(1)) \
        if actor_match else ''
    for token in ('scale2.0', 'animSpeed0.25',
                  'spawnAnim"effects/vasePuff"', 'fullbright1',
                  'color"0.50.50.25"'):
        if token not in actor_compact:
            out.append('Fx_VaseExplodePuff actor contract changed: %s' % token)
    alias = K.parse_anim_full(z, names, 'effects/vasePuff')
    if (len(alias) != 1 or alias[0]['delay'] != 0
            or alias[0].get('goto') != 'effects/magicPuff'
            or alias[0].get('sprites')
            or alias[0].get('spawn_light')
                != (384.0, .5, .5, .25, 15.0, 2.0)):
        out.append('effects/vasePuff alias/light contract changed')

    if (K.KEX_VASE_PUFF_TILE_BASE != 21981
            or K.KEX_VASE_PUFF_VISIBLE_FRAMES != 7
            or K.KEX_VASE_PUFF_FRAME_HOLD != 2):
        out.append('converter vase-puff tile/cadence contract changed')
    try:
        seq = S.read_seq(open(os.path.join(outdir, 'KEXVASEPUFF.SEQ'),
                              'rb').read())
        tiles = [K.KEX_VASE_PUFF_TILE_BASE + i
                 for i in range(K.KEX_VASE_PUFF_VISIBLE_FRAMES)
                 for _ in range(K.KEX_VASE_PUFF_FRAME_HOLD)]
        count = len(tiles)
        if (seq['seq_base'] != [0] or seq['seq_size'] != [count]
                or seq['fr_base'] != list(range(count))
                or seq['fr_size'] != [1] * count
                or seq['fr_flag'] != [4] * count
                or seq['ck_x'] != [0] * count
                or seq['ck_y'] != [0] * count
                or seq['ck_pict'] != tiles
                or seq['ck_flag'] != [0] * count
                or seq['snd_names'] or seq['snd_events']):
            out.append('KEXVASEPUFF.SEQ is not the exact 7x2-frame contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXVASEPUFF.SEQ is missing or invalid: %s' % exc)

    try:
        import kexpng as _png
        dims, hashes = set(), set()
        totals = [0, 0, 0]
        opaque = 0
        for frame in range(K.KEX_VASE_PUFF_VISIBLE_FRAMES):
            tile = K.KEX_VASE_PUFF_TILE_BASE + frame
            rel = 'kexart/vasepuff/vase_%d.png' % frame
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('vase-puff tile %d is absent from exhumed.def' % tile)
                continue
            path = os.path.join(outdir, rel)
            blob = open(path, 'rb').read()
            w, h, pixels = _png.read(path)
            dims.add((w, h))
            hashes.add(hashlib.sha256(blob).digest())
            for p in range(0, len(pixels), 4):
                if pixels[p+3]:
                    totals[0] += pixels[p]
                    totals[1] += pixels[p+1]
                    totals[2] += pixels[p+2]
                    opaque += 1
        if len(dims) != 1:
            out.append('vase-puff frames do not share one centred canvas: %s'
                       % sorted(dims))
        if len(hashes) != K.KEX_VASE_PUFF_VISIBLE_FRAMES:
            out.append('vase-puff visual frames are not all distinct')
        if (not opaque or totals[0] * 5 < totals[1] * 4
                or totals[1] * 5 < totals[0] * 4
                or totals[0] * 10 <= totals[2] * 14
                or totals[1] * 10 <= totals[2] * 14):
            out.append('vase-puff art lost its authored olive color multiplier')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('vase-puff PNG audit failed: %s' % exc)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        required_sounds = (
            ('blood_4', 'sounds/fleshbreak.wav'),
            ('k_gls1', 'sounds/glass01.wav'), ('k_gls2', 'sounds/glass02.wav'),
            ('k_gls3', 'sounds/glass03.wav'), ('k_wood1', 'sounds/wood01.wav'),
            ('k_wood2', 'sounds/wood02.wav'), ('k_wood3', 'sounds/wood03.wav'),
        )
        for alias, wav in required_sounds:
            if not re.search(r'^%s\s+%s\s+0$' % (alias, re.escape(wav)), sounds, re.M):
                out.append('KEX shard bounce sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing')

    misc_real = names.get('scripts/p_misc.txt')
    misc_text = z.read(misc_real).decode('latin-1').replace('\r', '') \
        if misc_real else ''
    start = misc_text.find('void A_RandomVaseItem')
    end = misc_text.find('\nvoid ', start + 1) if start >= 0 else -1
    vase_fn = re.sub(r'\s+', '', misc_text[start:end if end >= 0 else None]) \
        if start >= 0 else ''
    for token in ('switch(r)', 'case0:',
                  'actor.GetOrigin().z+actor.GetHeight()',
                  'ObjectFactory().SpawnActor("AI_Scorp",x,y,z,0,actor.GetSector())'):
        if token not in vase_fn:
            out.append('retail A_RandomVaseItem case 0 changed: %s' % token)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
    except OSError:
        return out + ['cannot read native breakable controller']
    native_needles = (
        'kKexBreakSoundNone', 'kKexBreakSoundGlass', 'kKexBreakSoundWood',
        'kKexBreakSoundFlesh', 'if(fields<12)',
        'bd>0?kKexBreakSoundNone', 'KexBreak_PlaySound(t,nSprite)',
        'KexSound_Get("blood_4")', 'KexBreak_ShardBounceSound(t)',
        'VB_KEXBREAK_SOUND_TEST',
        'glass==10||glass==11', 's_legacyBreakTable?legacy:normal',
        'KexVasePuff_Spawn(', 's_vasePuffsSpawned',
        'Kexbreakpufftest:', 'Kexbreaksoundplaybacktest:',
        'BuildSpider(-1,x,y,z,nSector,RandomSize(11),1)',
        'VB_KEXBREAK_SCORPTEST', 'KexSpiderIdentityTest()',
        'KexBreak_DropItem(intnSprite,KexBreakTypeconst&t)',
        'sprite[nSprite].z-t.spawnheight*16*6',
        's_vaseScorpsAtHeight',
        'Kexbreakscorpiontest:',
        'staticvoidKexBreak_ArmMine(intnProp)',
        'staticintKexBreak_MinePicnum(KexBreakTypeconst&t,intnFuse)',
        'VB_KEX_MINE_TEST', 'Kexminecountdowntest:',
        's_prop[nProp].nFuse=t.fuse',
        'sprite[nSprite].cstat&=~0x100;',
        'sprite[nSprite].picnum=t.tile+1;',
        'sprite[nSprite].picnum=KexBreak_MinePicnum(t,s_prop[nProp].nFuse);',
        'KexSound_Get("k_mine")',
        't.fuse<=0||s_prop[nProp].nFuse>=0',
        'if(--s_prop[nProp].nFuse>0){')
    for needle in native_needles:
        if needle not in native:
            out.append('native breakable controller no longer implements %s' % needle)
    if 't.wood' in native or 'kSoundPotPc1+(t.wood?' in native:
        out.append('native breakable controller reverted to the barrel-only sound guess')
    start = native.rfind('staticvoidKexBreak_Shatter(')
    sound = native.find('KexBreak_PlaySound(', start)
    puff = native.find('KexVasePuff_Spawn(', start)
    drop = native.find('KexBreak_DropItem(', puff)
    chunks = native.find('KexTossBouncyCreatureChunk(', drop)
    if start < 0 or sound < start or puff < sound or drop < puff or chunks < drop:
        out.append('ordinary break callback order is no longer sound/puff/item/shards')
    return out


def verify_drop_effects(z, outdir):
    """Enemy A_RandomItem height, delayed orb actors, art, and callback bridge."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    def read(rel):
        real = names.get(rel.lower())
        return z.read(real).decode('latin-1').replace('\r', '') if real else ''

    misc = read('scripts/p_misc.txt')
    start = misc.find('void A_RandomItem')
    end = misc.find('\nvoid ', start + 1) if start >= 0 else -1
    random_item = re.sub(r'\s+', '', misc[start:end if end >= 0 else None]) \
        if start >= 0 else ''
    source_needles = (
        'intr=kexSlaveRandomizer::UInt8()&7',
        'floatx=actor.GetOrigin().x',
        'floaty=actor.GetOrigin().y',
        'floatz=actor.GetOrigin().z+actor.GetHeight()',
        'if(r>2)', 'Player().GetHealth()<50',
        '(kexSlaveRandomizer::UInt8()&3)!=0',
        'ObjectFactory().SpawnActor("Fx_HealthSpawn",x,y,z,0,actor.GetSector())',
        'ObjectFactory().SpawnActor(((r&1)==0)?"Fx_AmmoSpawn":"Fx_HealthSpawn",x,y,z,0,actor.GetSector())',
        'kexSlaveRandomizer::UInt8()<128', 'obj.GetScale()=2.0f')
    for needle in source_needles:
        if needle not in random_item:
            out.append('retail A_RandomItem contract changed: %s' % needle)

    actors = {actor['name']: actor for actor in K.load_kex_actors(z).values()}
    effects = read('defs/actors/effects.txt')
    actor_specs = (
        ('Fx_AmmoSpawn', '0.2850.2851.0'),
        ('Fx_HealthSpawn', '1.00.2850.285'))
    for actor_name, color in actor_specs:
        actor = actors.get(actor_name)
        if (not actor or actor.get('anim') != 'effects/orb_spawn'
                or actor.get('radius') != 64 or actor.get('height') != 64
                or abs(actor.get('scale', 0) - 1.0) > 1e-6
                or not actor.get('fullbright')):
            out.append('%s base actor definition changed' % actor_name)
        match = re.search(r'Actor\s+' + re.escape(actor_name)
                          + r'\s*\{(.*?)\n\}', effects, re.S)
        compact = re.sub(r'\s+', '', match.group(1)) if match else ''
        for token in ('classname"kexSlaveActor"', 'radius64', 'height64',
                      'stepHeight32', 'scale1.0',
                      'spawnAnim"effects/orb_spawn"', 'fullbright1',
                      'color"%s"' % color, 'transparency128'):
            if token not in compact:
                out.append('%s actor contract changed: %s' % (actor_name, token))

    frames = K.parse_anim_full(z, names, 'effects/orb_spawn')
    holds = (3, 3, 3, 3, 3, 2, 2, 4)
    if (len(frames) != 25 or not frames
            or frames[0].get('delay') != 20
            or frames[0].get('sprites') or frames[0].get('destroy')
            or any(f.get('delay') != 1 for f in frames[1:])
            or frames[1].get('local_sound') != 'sounds/spawn.wav'
            or any(f.get('local_sound') for f in frames[2:])
            or any(not f.get('sprites') for f in frames[1:24])
            or not frames[23].get('spawn_orb')
            or any(f.get('spawn_orb') for f in frames[:23] + frames[24:])
            or not frames[24].get('destroy') or frames[24].get('sprites')):
        out.append('retail effects/orb_spawn is no longer the exact '
                   '20-blank/23-visible/callback/destroy contract')
    elif sum(holds) != 23:
        out.append('orb-spawn verifier hold census is invalid')
    else:
        cursor = 1
        poses = []
        for hold in holds:
            group = frames[cursor:cursor + hold]
            pose = group[0].get('sprites') if group else None
            if len(group) != hold or not pose \
                    or any(f.get('sprites') != pose for f in group):
                out.append('retail effects/orb_spawn visual hold cadence changed')
                break
            poses.append(tuple(tuple(layer) for layer in pose))
            cursor += hold
        if cursor != 24 or len(set(poses)) != 8:
            out.append('retail effects/orb_spawn no longer has eight distinct '
                       'poses held 3/3/3/3/3/2/2/4 ticks')

    if (K.KEX_AMMO_SPAWN_TILE_BASE != 21988
            or K.KEX_HEALTH_SPAWN_TILE_BASE != 21996
            or K.KEX_ORB_SPAWN_UNIQUE_FRAMES != 8
            or K.KEX_ORB_SPAWN_BLANK_TICKS != 20
            or K.KEX_ORB_SPAWN_LIFETIME != 43):
        out.append('converter enemy-drop tile/lifetime contract changed')

    expected_fr_base = [0] * 20 + list(range(23))
    expected_fr_size = [0] * 20 + [1] * 23
    visual_index = [i for i, hold in enumerate(holds) for _ in range(hold)]
    for label, tile_base in (('AMMO', K.KEX_AMMO_SPAWN_TILE_BASE),
                             ('HEALTH', K.KEX_HEALTH_SPAWN_TILE_BASE)):
        filename = 'KEX%sSPAWN.SEQ' % label
        try:
            seq = S.read_seq(open(os.path.join(outdir, filename), 'rb').read())
            tiles = [tile_base + i for i in visual_index]
            if (seq['tag'] != b'DS' or seq['centerx'] != 0 or seq['centery'] != 0
                    or seq['seq_base'] != [0] or seq['seq_size'] != [43]
                    or seq['seq_flag'] != [-1]
                    or seq['fr_base'] != expected_fr_base
                    or seq['fr_size'] != expected_fr_size
                    or seq['fr_flag'] != [4] * 43
                    or seq['ck_x'] != [0] * 23
                    or seq['ck_y'] != [0] * 23
                    or seq['ck_pict'] != tiles
                    or seq['ck_flag'] != [0] * 23
                    or seq['snd_names'] != [b'ITEM_SPE']
                    or seq['snd_events'] != [(20, 0)] or seq['trailing']):
                out.append('%s is not the exact 20-blank + 23-visible '
                           'fullbright spawn-sound contract' % filename)
        except (OSError, ValueError, AssertionError, struct.error) as exc:
            out.append('%s is missing or invalid: %s' % (filename, exc))

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
        import kexpng as _png
        all_dims = set()
        for label, tile_base, channel in (
                ('ammo', K.KEX_AMMO_SPAWN_TILE_BASE, 2),
                ('health', K.KEX_HEALTH_SPAWN_TILE_BASE, 0)):
            hashes = set()
            totals = [0, 0, 0]
            opaque = 0
            for frame in range(K.KEX_ORB_SPAWN_UNIQUE_FRAMES):
                tile = tile_base + frame
                rel = 'kexart/%sspawn/%s_%d.png' % (label, label, frame)
                if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                                 % (tile, re.escape(rel)), deftext):
                    out.append('%s-spawn tile %d is absent from exhumed.def'
                               % (label, tile))
                    continue
                path = os.path.join(outdir, rel)
                blob = open(path, 'rb').read()
                w, h, pixels = _png.read(path)
                all_dims.add((w, h))
                hashes.add(hashlib.sha256(blob).digest())
                for p in range(0, len(pixels), 4):
                    if pixels[p + 3]:
                        totals[0] += pixels[p]
                        totals[1] += pixels[p + 1]
                        totals[2] += pixels[p + 2]
                        opaque += 1
            if len(hashes) != K.KEX_ORB_SPAWN_UNIQUE_FRAMES:
                out.append('%s-spawn art does not contain eight distinct poses'
                           % label)
            other = max(totals[(channel + 1) % 3], totals[(channel + 2) % 3])
            if not opaque or totals[channel] <= other * 2:
                out.append('%s-spawn art lost its authored %s tint'
                           % (label, 'blue' if channel == 2 else 'red'))
        if len(all_dims) != 1:
            out.append('ammo/health spawn frames do not share one actor-centred '
                       'canvas: %s' % sorted(all_dims))
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('enemy-drop PNG/DEF audit failed: %s' % exc)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        if not re.search(r'^item_spe\s+sounds/spawn\.wav\s+0$', sounds, re.M):
            out.append('spawn.wav ITEM_SPE alias is missing')
    except OSError:
        out.append('kexsound.txt is missing for enemy-drop sound audit')

    actor_heights = {
        'AI_Anubis': 192, 'AI_Bastet': 192, 'AI_Bird': 128,
        'AI_Queen': 256, 'AI_Mantis': 384, 'AI_Mummy': 192,
        'AI_Wasp': 64, 'AI_Fish': 64, 'AI_Selkis': 160,
        'AI_Sentry': 192, 'AI_Set': 192, 'AI_Spider': 64,
        'AI_Scorp': 64, 'AI_Seedling': 96,
    }
    for actor_name, height in sorted(actor_heights.items()):
        actor = actors.get(actor_name)
        actual = int(round(actor.get('height', -1))) if actor else -1
        if actual != height:
            out.append('%s authored height is %d, expected %d'
                       % (actor_name, actual, height))

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        runlist = re.sub(r'\s+', '', open(os.path.join(srcdir, 'runlist.cpp')).read())
        anims = re.sub(r'\s+', '', open(os.path.join(srcdir, 'anims.cpp')).read())
        seqcpp = re.sub(r'\s+', '', open(os.path.join(srcdir, 'sequence.cpp')).read())
        seqhdr = re.sub(r'\s+', '', open(os.path.join(srcdir, 'sequence.h')).read())
    except OSError as exc:
        return out + ['cannot read native enemy-drop bridge: %s' % exc]

    native_needles = (
        'kSeqKexAmmoSpawn:kSeqKexHealthSpawn',
        'hitag==2||hitag==50?48:24,4|0x80',
        'sprite[nSprite].xvel=lotag', 'sprite[nSprite].yvel=hitag',
        'sprite[nSprite].zvel=picnum',
        'voidKexDrop_OrbSpawnPickup(intnEffectSprite)',
        'KexDrop_Spawn(sprite[nEffectSprite].sectnum',
        'VB_KEXDROP_ORB_TEST',
        'Kexenemy-droporbtest:%s(%d/%dexact20+23-tickdelayedpickups)',
        'if(exact==-1)KexDrop_Spawn(',
        'intconstr=RandomSize(3)', 'if(r>2)',
        'PlayerList[nPlayer].nHealth<kMaxHealth/4',
        '(RandomSize(8)&3)!=0', 'RandomSize(8)<128',
        'sprite[nSprite].z-actorHeight*16*6')
    for needle in native_needles:
        if needle not in native:
            out.append('native enemy-drop bridge no longer implements %s' % needle)
    for stat, height in ((99, 64), (101, 192), (102, 192), (103, 64),
                         (104, 192), (105, 192), (118, 384), (120, 192),
                         (121, 256), (122, 160)):
        if 'case%d:return%d;' % (stat, height) not in native:
            out.append('native enemy-drop actor-height map lost status %d -> %d'
                       % (stat, height))
    for needle in ('case107:', 'sprite[nSprite].hitag==1?128',
                   'sprite[nSprite].hitag==2?96:64'):
        if needle not in native:
            out.append('native shared-flyer height map changed: %s' % needle)

    capture = runlist.find('intconstnKexDropZ=sprite[nSprite].z-KexDrop_ActorHeight(nSprite)*16*6;')
    dispatch = runlist.find('runlist_SendMessageToRunRec(', capture)
    drop = runlist.find('KexDrop_RandomItemAt(nKexDropSector,nKexDropX,nKexDropY,nKexDropZ,nSprite2)', dispatch)
    if capture < 0 or dispatch < capture or drop < dispatch:
        out.append('generic enemy death no longer captures the authored top '
                   'before controller deletion and then starts A_RandomItem')
    for needle in ('nFrame==42', 'kSeqKexAmmoSpawn', 'kSeqKexHealthSpawn',
                   'KexDrop_OrbSpawnPickup(nSprite)'):
        if needle not in anims:
            out.append('animation callback bridge changed: %s' % needle)
    for needle in ('#definekMaxSEQFiles97', '"kexammospawn"',
                   '"kexhealthspawn"', '"kexgibdeath"',
                   '"kexbirdfeather"'):
        if needle not in seqcpp:
            out.append('optional sequence table changed: %s' % needle)
    for needle in ('kSeqKexAmmoSpawn', 'kSeqKexHealthSpawn'):
        if needle not in seqhdr:
            out.append('optional sequence enum changed: %s' % needle)
    return out


def verify_gib_death_effect(z, outdir):
    """Fx_Gibs01 source, generated art/SEQ, all callers, and native bridge."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    def read(rel):
        real = names.get(rel.lower())
        return z.read(real).decode('latin-1').replace('\r', '') if real else ''

    misc = read('scripts/p_misc.txt')
    start = misc.find('void A_GibDeath')
    end = misc.find('\nvoid ', start + 1) if start >= 0 else -1
    gib_fn = re.sub(r'\s+', '', misc[start:end if end >= 0 else None]) \
        if start >= 0 else ''
    for needle in (
            'floatx=actor.GetOrigin().x',
            'floaty=actor.GetOrigin().y',
            'floatz=actor.GetOrigin().z+offset',
            'ObjectFactory().SpawnActor("Fx_Gibs01",x,y,z,0,actor.GetSector())',
            'if(!(gibisnull))', 'gib.GetScale()=actor.GetScale()'):
        if needle not in gib_fn:
            out.append('retail A_GibDeath contract changed: %s' % needle)

    actors = {actor['name']: actor for actor in K.load_kex_actors(z).values()}
    actor = actors.get('Fx_Gibs01')
    if (not actor or actor.get('anim') != 'effects/gib_death'
            or actor.get('radius') != 64 or actor.get('height') != 64
            or abs(actor.get('scale', 0) - 1.0) > 1e-6
            or not actor.get('fullbright')):
        out.append('Fx_Gibs01 base actor definition changed')
    effects = read('defs/actors/effects.txt')
    match = re.search(r'Actor\s+Fx_Gibs01\s*\{(.*?)\n\}', effects, re.S)
    compact_actor = re.sub(r'\s+', '', match.group(1)) if match else ''
    for needle in ('classname"kexSlaveActor"', 'radius64', 'height64',
                   'stepHeight32', 'scale1.0',
                   'spawnAnim"effects/gib_death"', 'fullbright1'):
        if needle not in compact_actor:
            out.append('Fx_Gibs01 actor contract changed: %s' % needle)

    frames = K.parse_anim_full(z, names, 'effects/gib_death')
    expected_visual_index = [0, 0, 1, 1, 2, 3, 3, 4, 5, 6, 7, 8, 9]
    if (len(frames) != 19 or any(f.get('delay') != 1 for f in frames)
            or (frames and frames[0].get('sprites'))
            or any(not f.get('sprites') for f in frames[1:14])
            or any(f.get('sprites') for f in frames[14:])
            or any(f.get('random_item') for f in frames[:-1])
            or not frames or not frames[-1].get('random_item')
            or any(f.get('destroy') for f in frames[:-1])
            or not frames or not frames[-1].get('destroy')
            or any(f.get('local_sound') for f in frames)):
        out.append('retail effects/gib_death is no longer the exact '
                   'blank/13-art/5-blank delayed callback contract')
    elif frames:
        signatures = []
        visual_index = []
        for frame in frames[1:14]:
            signature = tuple(tuple(layer) for layer in frame['sprites'])
            if signature not in signatures:
                signatures.append(signature)
            visual_index.append(signatures.index(signature))
        if len(signatures) != 10 or visual_index != expected_visual_index:
            out.append('retail effects/gib_death visual pose cadence changed: %s'
                       % visual_index)

    # There are exactly eleven shipped A_GibDeath calls. Their offsets are
    # gameplay data: Fx_Gibs01 later applies its own height of 64 at the drop.
    expected_callers = sorted((
        ('sprites/monsters/anubis_anim.txt', 64),
        ('sprites/monsters/bastet_anim.txt', 0),
        ('sprites/monsters/bird_anim.txt', 0),
        ('sprites/monsters/fish_anim.txt', 0),
        ('sprites/monsters/mummy_anim.txt', 0),
        ('sprites/monsters/queen_death_anim.txt', 64),
        ('sprites/monsters/scorp_anim.txt', 0),
        ('sprites/monsters/seedling_anim.txt', 32),
        ('sprites/monsters/sentry_anim.txt', 64),
        ('sprites/monsters/spider_anim.txt', 0),
        ('sprites/monsters/wasp_anim.txt', 0),
    ))
    actual_callers = []
    for lower, real in names.items():
        if not lower.startswith('sprites/monsters/') or not lower.endswith('.txt'):
            continue
        text = z.read(real).decode('latin-1')
        for value in re.findall(r'A_GibDeath\s*\(\s*([0-9.]+)\s*\)', text):
            actual_callers.append((lower, int(round(float(value)))))
    if sorted(actual_callers) != expected_callers:
        out.append('retail A_GibDeath caller/offset census changed: %s'
                   % sorted(actual_callers))

    if (K.KEX_GIB_DEATH_TILE_BASE != 22004
            or K.KEX_GIB_DEATH_UNIQUE_FRAMES != 10
            or K.KEX_GIB_DEATH_LIFETIME != 19):
        out.append('converter gib-death tile/lifetime contract changed')

    frame_sizes = [0] + [1] * 13 + [0] * 5
    frame_bases = []
    cursor = 0
    for size in frame_sizes:
        frame_bases.append(cursor)
        cursor += size
    try:
        seq = S.read_seq(open(os.path.join(outdir, 'KEXGIBDEATH.SEQ'),
                              'rb').read())
        tiles = [K.KEX_GIB_DEATH_TILE_BASE + i
                 for i in expected_visual_index]
        if (seq['tag'] != b'IH' or seq['centerx'] != 0 or seq['centery'] != 0
                or seq['seq_base'] != [0] or seq['seq_size'] != [19]
                or seq['seq_flag'] != [-1]
                or seq['fr_base'] != frame_bases
                or seq['fr_size'] != frame_sizes
                or seq['fr_flag'] != [4] * 19
                or seq['ck_x'] != [0] * 13 or seq['ck_y'] != [0] * 13
                or seq['ck_pict'] != tiles or seq['ck_flag'] != [0] * 13
                or seq['snd_names'] or seq['snd_events'] or seq['trailing']):
            out.append('KEXGIBDEATH.SEQ is not the exact fullbright '
                       'blank/13-art/5-blank contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXGIBDEATH.SEQ is missing or invalid: %s' % exc)

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
        import kexpng as _png
        dims, hashes = set(), set()
        opaque = 0
        for frame in range(K.KEX_GIB_DEATH_UNIQUE_FRAMES):
            tile = K.KEX_GIB_DEATH_TILE_BASE + frame
            rel = 'kexart/gibdeath/gib_%d.png' % frame
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('gib-death tile %d is absent from exhumed.def' % tile)
                continue
            path = os.path.join(outdir, rel)
            blob = open(path, 'rb').read()
            w, h, pixels = _png.read(path)
            dims.add((w, h))
            hashes.add(hashlib.sha256(blob).digest())
            opaque += sum(1 for p in range(3, len(pixels), 4) if pixels[p])
        if len(dims) != 1:
            out.append('gib-death frames do not share one actor-centred canvas: %s'
                       % sorted(dims))
        if len(hashes) != K.KEX_GIB_DEATH_UNIQUE_FRAMES:
            out.append('gib-death art does not contain ten distinct poses')
        if not opaque:
            out.append('gib-death art is fully transparent')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('gib-death PNG/DEF audit failed: %s' % exc)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    native = {}
    for filename in ('sequence.cpp', 'sequence.h', 'anims.cpp', 'kexbreak.cpp',
                     'runlist.cpp', 'anubis.cpp', 'lion.cpp', 'fish.cpp',
                     'mummy.cpp', 'spider.cpp', 'wasp.cpp', 'roach.cpp',
                     'queen.cpp'):
        try:
            native[filename] = re.sub(
                r'\s+', '', open(os.path.join(srcdir, filename)).read())
        except OSError as exc:
            out.append('cannot read native gib-death bridge %s: %s'
                       % (filename, exc))

    for needle in ('#definekMaxSEQFiles97', '"kexgibdeath"'):
        if needle not in native.get('sequence.cpp', ''):
            out.append('optional gib-death sequence table changed: %s' % needle)
    if 'kSeqKexGibDeath' not in native.get('sequence.h', ''):
        out.append('optional gib-death sequence enum changed')

    anims = native.get('anims.cpp', '')
    anim_needles = (
        'intKexGibDeath_Spawn(',
        'intconsteffectZ=z-nKexZOffset*16*6',
        'BuildAnim(-1,kSeqKexGibDeath,0,x,y,effectZ,nSector,clamp(nRepeat,1,255),4)',
        'sprite[GetAnimSprite(nAnim)].xvel=(short)nKiller',
        'intconstdropZ=sprite[nSprite].z-64*16*6',
        'KexDrop_RandomItemAt(sprite[nSprite].sectnum,sprite[nSprite].x,sprite[nSprite].y,dropZ,sprite[nSprite].xvel)',
        'nFrame==18', 'KexGibDeath_RandomItem(nSprite)',
        'VB_KEX_GIB_DEATH_TEST',
        'Kexgib-deathactortest:%s(%d/%dexactblank+13-art+5-blankcallbacksateffect+64)')
    for needle in anim_needles:
        if needle not in anims:
            out.append('native Fx_Gibs01 bridge no longer implements %s' % needle)

    route_specs = {
        'anubis.cpp': {64: 2}, 'lion.cpp': {0: 2},
        'fish.cpp': {0: 2}, 'mummy.cpp': {0: 2},
        'spider.cpp': {0: 2}, 'wasp.cpp': {0: 2, 32: 1},
        'roach.cpp': {64: 1}, 'queen.cpp': {64: 1},
    }
    for filename, offsets in route_specs.items():
        calls = re.findall(r'KexGibDeath_Spawn\((.*?)\);',
                           native.get(filename, ''))
        if len(calls) != sum(offsets.values()):
            out.append('%s routes %d Fx_Gibs01 paths, expected %d'
                       % (filename, len(calls), sum(offsets.values())))
            continue
        for offset, wanted in offsets.items():
            actual = sum((',sprite[nSprite].sectnum,%d,' % offset) in call
                         for call in calls)
            if actual != wanted:
                out.append('%s routes %d offset-%d gib paths, expected %d'
                           % (filename, actual, offset, wanted))

    breaking = native.get('kexbreak.cpp', '')
    for needle in ('intKexGibDeath_OwnsDrop(intnStat)',
                   '!seq_IsLoaded(kSeqKexGibDeath)',
                   'case99:', 'case101:', 'case102:', 'case103:',
                   'case104:', 'case105:', 'case107:', 'case121:'):
        if needle not in breaking:
            out.append('gib-death drop ownership map changed: %s' % needle)
    runlist = native.get('runlist.cpp', '')
    capture = runlist.find('intconstnKexDropStat=sprite[nSprite].statnum;')
    dispatch = runlist.find('runlist_SendMessageToRunRec(', capture)
    suppress = runlist.find('!KexGibDeath_OwnsDrop(nKexDropStat)', dispatch)
    if capture < 0 or dispatch < capture or suppress < dispatch:
        out.append('lethal-hit hook no longer suppresses the duplicate immediate '
                   'drop for Fx_Gibs01-owned deaths')
    return out


def verify_ramses(z, outdir):
    """Audit the authored cinematic ranges and every shipped facial stream."""
    out = []
    names = {n.lower(): n for n in z.namelist()}
    expected = []
    for phase, anim, repeat, ticks in K.KEX_RAMSES_ANIMS:
        expected.append((phase, len(K._all_frames(z, names, anim, 1000)), repeat, ticks))

    table = os.path.join(outdir, 'kexramses.txt')
    actual = []
    if not os.path.exists(table):
        return ['kexramses.txt is missing']
    with open(table) as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith('#'):
                continue
            try:
                if len(p) != 5:
                    raise ValueError
                actual.append((p[0],) + tuple(int(v) for v in p[1:]))
            except ValueError:
                out.append('malformed kexramses row: %s' % line.strip())

    if [r[0] for r in actual] != [r[0] for r in expected]:
        out.append('Ramses phase order differs: expected %s, got %s'
                   % ([r[0] for r in expected], [r[0] for r in actual]))
    byphase = {r[0]: r for r in actual}
    for phase, count, repeat, ticks in expected:
        got = byphase.get(phase)
        if got is None:
            out.append('Ramses %s range is missing' % phase)
        elif got[2:] != (count, repeat, ticks):
            out.append('Ramses %s metadata differs: expected %s, got %s'
                       % (phase, (count, repeat, ticks), got[2:]))

    claimed = {}
    deftext = ''
    defpath = os.path.join(outdir, 'exhumed.def')
    if os.path.exists(defpath):
        with open(defpath, errors='replace') as f:
            deftext = f.read()
    else:
        out.append('exhumed.def is missing')
    dimensions = {}
    for row in actual:
        phase, first, count, _repeat, _ticks = row
        if first <= 0 or count <= 0:
            out.append('Ramses %s has an invalid tile range' % phase)
            continue
        dims = set()
        for frame in range(count):
            tile = first + frame
            if tile in claimed:
                out.append('Ramses tile %d overlaps %s and %s' % (tile, claimed[tile], phase))
            claimed[tile] = phase
            rel = 'kexart/ramses/%s_%02d.png' % (phase, frame)
            path = os.path.join(outdir, rel)
            if ('tilefromtexture %d { file "%s" }' % (tile, rel)) not in deftext:
                out.append('Ramses tile %d is absent from exhumed.def' % tile)
            try:
                with open(path, 'rb') as pf:
                    head = pf.read(24)
                if head[:8] != b'\x89PNG\r\n\x1a\n' or len(head) < 24:
                    raise ValueError
                dims.add(struct.unpack_from('>II', head, 16))
            except (OSError, ValueError, struct.error):
                out.append('Ramses frame is missing or invalid: %s' % rel)
        if len(dims) != 1:
            out.append('Ramses %s frames do not share one canvas: %s' % (phase, sorted(dims)))
        dimensions[phase] = dims
    spirit_dims = [dimensions.get(p, set()) for p in ('spawn', 'talk', 'vanish')]
    if spirit_dims and any(d != spirit_dims[0] for d in spirit_dims[1:]):
        out.append('Ramses spirit phases do not share one origin canvas')
    for a, b in zip(actual, actual[1:]):
        if b[1] != a[1] + a[2]:
            out.append('Ramses tile ranges are not contiguous between %s and %s'
                       % (a[0], b[0]))

    speech_bad = []
    max_samples = 0
    for lang in ('ENG', 'FRE', 'GER', 'SPA'):
        for number in range(1, 15):
            stem = 'SP_%s%02d' % (lang, number)
            script = names.get(('scripts/ramses/%s.txt' % stem).lower())
            audio = names.get(('ramses/%s.wav' % stem).lower())
            if script is None or audio is None:
                speech_bad.append('%s missing script/audio' % stem)
                continue
            text = z.read(script).decode('latin-1')
            left, right = text.find('{'), text.rfind('}')
            values = [int(v) for v in re.findall(r'\d+', text[left + 1:right])] \
                if 0 <= left < right else []
            max_samples = max(max_samples, len(values))
            if not values or len(values) > 8192 or any(v < 0 or v > 15 for v in values):
                speech_bad.append('%s has %d invalid facial samples' % (stem, len(values)))
                continue
            try:
                with wave.open(io.BytesIO(z.read(audio)), 'rb') as wav:
                    seconds = wav.getnframes() / float(wav.getframerate())
                if abs(len(values) - seconds * 30.0) > 1.1:
                    speech_bad.append('%s facial rate is %.3f/sec' %
                                      (stem, len(values) / max(seconds, .001)))
            except (wave.Error, EOFError, ZeroDivisionError):
                speech_bad.append('%s wav header is unreadable' % stem)
    if speech_bad:
        out.append('Ramses speech stream audit: %s' % speech_bad[:8])
    if max_samples > 8192:
        out.append('Ramses facial stream exceeds the runtime buffer: %d' % max_samples)

    # The two scripted-sector moves are authored in KEX source units and KEX
    # advances them at 60 Hz. The Build game update is 30 Hz, so its bridge
    # must consume two speed quanta per update: TOMB's 320@4 reaches its end
    # in 40 Build ticks and TOMBEND's 320@6 in 27.
    script_specs = {
        'scripts/map/TOMB_script.txt': (
            'MoveScriptedSector(666, -256, -1, true)',
            'MoveScriptedSector(666, 320, 4, true)'),
        'scripts/map/TOMBEND_script.txt': (
            'MoveScriptedSector(0, 64, -1, true)',
            'MoveScriptedSector(0, -320, 6, true)'),
    }
    for rel, tokens in script_specs.items():
        real = names.get(rel.lower())
        text = re.sub(r'\s+', '', z.read(real).decode('latin-1')) if real else ''
        for token in tokens:
            if re.sub(r'\s+', '', token) not in text:
                out.append('retail scripted-sector source changed: %s' % token)
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        for needle in ('KEX_Z_PER_UNIT96',
                       'intconstspeed=m.rule->speed*2;',
                       'sector[m.sector].ceilingz+=dz;',
                       'sector[m.linkedSector].floorz+=dz;',
                       'm.expectedTicks=(abs(m.rule->amount)+speed-1)/speed;',
                       'arrivedafter%dticks(expected%d)',
                       'VB_KEXSCRIPT_END_TEST'):
            if needle not in native:
                out.append('native scripted-sector 60-to-30Hz bridge changed: %s' % needle)
    except OSError:
        out.append('cannot read native scripted-sector bridge')
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        init = re.sub(r'\s+', '', open(os.path.join(srcdir, 'init.cpp')).read())
        for needle in ("s_['kexscripted']=(etag,eparam)",
                       "KexScripted_Add(sprite[nSprite].sectnum,nHitag,",
                       'sprite[nSprite].xvel-1'):
            if needle not in mapper + init:
                out.append('scripted-sector partner marker bridge changed: %s' % needle)
    except OSError:
        out.append('cannot read scripted-sector marker bridge')
    return out


def verify_movers(z, outdir):
    """Protect the remaster's ordinary mover rates and sinking floor plates."""
    out = []
    names = [n for n in z.namelist()
             if n.lower().startswith('maps/') and n.lower().endswith('.map')
             and '/old/' not in n.lower()]
    retail_names = [n for n in names if '/testmaps/' not in n.lower()]
    plate_events = 0
    pad_events = 0
    slow_doors = 0
    for name in retail_names:
        m = K.locate(z.read(name))
        plate_events += sum(e[0] == 23 for e in m.events)
        pad_events += sum(e[0] == 48 for e in m.events)
        slow_doors += sum(e[0] == 9 for e in m.events)
    if plate_events != 26:
        out.append('retail FLOOR_TRIGGER_PLATE census changed from 26 to %d' % plate_events)
    if slow_doors != 2:
        out.append('retail DOOR_CLOSE_SLOW census changed from 2 to %d' % slow_doors)
    if pad_events != 28:
        out.append('retail DROP_PAD census changed from 28 to %d' % pad_events)

    # kexWorld::ReadEvents(), not kexFloatingPlatform::Spawn(), decides which
    # event classes become movers. Spawn has latent cases for 43/46/47, but
    # the world loader creates only these six classes; 42 and 69 are likewise
    # named event enums that stay static. Keep the converter on that precise
    # boundary so an implementation detail cannot animate extra retail map
    # geometry.
    want_floaters = {
        'FLOATER_128', 'FLOATER_128_ANG_120', 'FLOATER_128_ANG_180',
        'FLOATER_64_ANG_USER', 'FLOATER_128_ANG_RAND_256',
        'FLOATER_512_ANG_USER',
    }
    if set(K.KEX_FLOATERS) != want_floaters:
        out.append('Kex ReadEvents floater construction set changed: %s'
                   % sorted(K.KEX_FLOATERS))

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        runlist = re.sub(r'\s+', '', open(os.path.join(srcdir, 'runlist.cpp')).read())
        switch = re.sub(r'\s+', '', open(os.path.join(srcdir, 'switch.cpp')).read())
    except OSError as exc:
        return ['cannot read Kex mover bridge: %s' % exc]

    for needle in ("s_['kexplate']=(etag,s_['floorz'])",
                   "s_['floorz']-=16*SCALE*WORLD",
                   "w['_kexswitchtag']=etag",
                   "w['_kexswitchtype']=etype",
                   'lotag=2001', 'lotag,hitag=2024,chan',
                   "1075ifname=='DOOR_CLOSE_SLOW'else2075",
                   'lotag,hitag=2008,chan', 'lotag,hitag=2009,chan',
                   'lotag,hitag=2012,chan', 'm.kex_drop_surfaces=_drop_pad_surfaces',
                   '_write_table(outdir,"kexpad.txt","mapgroupsectorceilingtag",',
                   'out.append((si,84,tag,target))',
                   "out.append((si,85,w['_kexswitchtag']",
                   "w.get('_kexswitchtype',0)))",
                   "'k_sw1':('sounds/switch.wav',0)",
                   "'k_sw2':('sounds/switch2.wav',0)",
                   "'k_fplate':('sounds/fplate.wav',0)",
                   "'k_plat':('sounds/platstart.wav',1)",
                   "'k_stonestop':('sounds/stonestop.wav',0)",
                   "'k_padfall':('sounds/platfall.wav',0)",
                   "'k_teleport':('sounds/teleport.wav',0)"):
        if needle not in mapper:
            out.append('mapper no longer preserves Kex mover rule: %s' % needle)
    for needle in ('#defineKEX_PLATE_STEP(2*KEX_Z_PER_UNIT*2)',
                   'voidKexPlate_Add(intnSector,intnTag,intnTarget)',
                   'voidKexPlate_ChannelFired(intnChannel)',
                   'voidKexSwitch_Add(intnSector,intnTag,intnOffPic,intnType,intx,inty)',
                   'intKexSwitch_PlaySound(intnWall,intnPlayer)',
                   's.type==202||s.type==203?"k_sw2":"k_sw1"',
                   'VB_KEXSWITCH_TEST',
                   'staticvoidKexSwitch_TestTick(void)',
                   'runlist_SignalRun(wall[nWall].lotag-1,nLocalPlayer|0x40000);',
                   'KexSwitch_ResetByTag(tag)', 'KexPlate_ResetChain(p.tag+1)',
                   'KexSound_Get("k_fplate")', 'D3PlayFX(sound,nSprite)',
                   'VB_KEX_PLATE_SOUND_TEST',
                   'KexPlate_Move(p,p.bottom,kKexPlateDown)',
                   'KexScript_FireTag(p.tag==0?100000:p.tag,"floortriggerplatelowered")',
                   'VB_KEX_PLATE_TEST', 'staticintKexPlate_TestPlacePlayer(KexPlateconst&p,intnPlayer)',
                   'sRunChannels[s_plate[i].resetChannel].d=0;',
                   'Kexplatetest:%s(sector%d,%d/%dticks)', 'voidKexPad_Init(void)',
                   'voidKexPad_ChannelFired(intchan)',
                   'KexPad_Move(p,min(p.delta-p.current,',
                   'KexSwitch_ResetByTag(p.tag)', 'VB_KEXPAD_TEST',
                   'staticvoidKexPad_Land(KexPadconst&p)', 'KexQuake(16)',
                   'KexSound_Get("k_padfall")',
                   'PlayFXAtXYZ(sound,x,y,sector[nSector].floorz,nSector)',
                   'VB_KEXPAD_TEST', 'KexPad_Land(p)'):
        if needle not in native:
            out.append('native floor-trigger plate bridge changed: %s' % needle)
    try:
        obj = re.sub(r'\s+', '', open(os.path.join(srcdir, 'object.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex mover-sound bridge: %s' % exc]
    for needle in ('staticboolKexElevSound(shortnSprite,intnVal,boolbStop)',
                   'bStop?"k_stonestop":(nVal&2?"k_plat":"amb_ston")',
                   'sounds/platstart.wav', 'sounds/stonemov.wav',
                   'sounds/stonestop.wav', 'VB_KEXMOVER_SOUND_TEST',
                   'if(!KexElevSound(Elevator[nElev].nSprite,var_18,true))'):
        if needle not in obj:
            out.append('native Kex mover-sound bridge changed: %s' % needle)
    for needle in ('KexPlate_ChannelFired(ChannelList)',
                   'kexMover?90:60', 'kexMover?90:150',
                   'intconstkexMoveSpeed=kexMover?(kexRate==1?192:768):nSpeed*100',
                   'kexMover?kexMoveSpeed:400',
                   'kexMover?kexMoveSpeed:nSpeed*100'):
        if needle not in runlist:
            out.append('native Kex mover timing bridge changed: %s' % needle)
    # Only the Kex lift cases need the direct player-entry path. Restrict the
    # static contract to those cases rather than accepting an unrelated
    # step-switch elsewhere in runlist.cpp.
    for label, start, end in (('lowering lift', 'case9:', 'case10:'),
                              ('raising lift', 'case12:', 'case13:')):
        block = runlist[runlist.find(start):runlist.find(end, runlist.find(start))]
        for needle in ('if(g_kexMode)',
                       'BuildKexSwStepOn(nChannel,nSector)'):
            if needle not in block:
                out.append('native Kex %s direct-entry bridge changed: %s'
                           % (label, needle))
    for needle in ('intBuildKexSwStepOn(intnChannel,intnSector)',
                   'BuildSwStepOn(nChannel,BuildLink(2,1,1),nSector)',
                   'SwitchData[nSwitch].field_14=1',
                   'if(SwitchData[nSwitch].field_14)',
                   'runlist_ChangeChannel(nChannel,1);'):
        if needle not in switch:
            out.append('native Kex tagged-lift state bridge changed: %s' % needle)
    for needle in ('#include"kexbreak.h"',
                   'KexSwitch_PlaySound(nWall,a&0xFFFF)',
                   'if(!KexSwitch_PlaySound(nWall,a&0xFFFF))'):
        if needle not in switch:
            out.append('native Kex switch sound bridge changed: %s' % needle)
    for needle in ('staticvoidKexLift_TestTaggedStep(intnChannel,intnSector)',
                   'runlist_SignalRun(sRunChannels[nChannel].a,0x50000)',
                   'sRunChannels[nChannel].c==1&&(sRunChannels[nChannel].d&2)',
                   'VB_KEXLIFT_TEST'):
        if needle not in runlist:
            out.append('native Kex tagged-lift regression probe changed: %s' % needle)

    # The remaster ships focused event maps outside the campaign. Its trap
    # test has the exact plate -> tag+1 switch-reset relationship that the
    # native ResetWallSwitchFromTag() path exercises. Convert them in-memory
    # so neither the raw-tag marker nor its off tile can silently disappear.
    test_expect = {
        'maps/testmaps/test_floor_triggers.map': ([1], []),
        'maps/testmaps/test_traps.map': ([666], [667]),
    }
    for name, (want_plates, want_switches) in test_expect.items():
        if name not in names:
            out.append('shipped Kex plate regression map missing: %s' % name)
            continue
        m = K.locate(z.read(name))
        sects, walls, ranges = K.convert(m, tilebase=0)
        _keep, remap = K.sector_remap(sects, ranges)
        sprites = K.build_sprites(m, sects, walls, ranges, remap)
        got_plates = sorted(s['hitag'] for s in sprites if s['lotag'] == 84)
        got_switches = sorted(s['hitag'] for s in sprites if s['lotag'] == 85)
        if got_plates != want_plates or got_switches != want_switches:
            out.append('%s plate reset markers differ: plates %s/%s switches %s/%s'
                       % (name, got_plates, want_plates, got_switches, want_switches))

    # The marker's yvel is intentionally free construction metadata: it keeps
    # the Kex wall-event class so the runtime can reproduce UseWallSwitch's
    # two exact player-local press sounds. The focused switch map exercises
    # this serialization independently of the native press interaction.
    switch_test = 'maps/testmaps/test_switch.map'
    if switch_test not in names:
        out.append('shipped Kex switch regression map missing: %s' % switch_test)
    else:
        m = K.locate(z.read(switch_test))
        sects, walls, ranges = K.convert(m, tilebase=0)
        _keep, remap = K.sector_remap(sects, ranges)
        sprites = K.build_sprites(m, sects, walls, ranges, remap)
        got = sorted(s['yvel'] for s in sprites if s['lotag'] == 85)
        want = sorted(e[0] for e in m.events if 200 <= e[0] <= 203)
        if got != want:
            out.append('%s switch sound classes differ: markers %s source %s'
                       % (switch_test, got, want))
        got_xy = sorted((s['x'], s['y']) for s in sprites if s['lotag'] == 85)
        want_xy = []
        for w in walls:
            if '_kexswitchtag' not in w:
                continue
            w2 = walls[w['point2']]
            want_xy.append((int((w['x'] + w2['x']) * K.WORLD / 2),
                            int((w['y'] + w2['y']) * K.WORLD / 2)))
        if got_xy != sorted(want_xy):
            out.append('%s switch marker midpoint differs: markers %s walls %s'
                       % (switch_test, got_xy, sorted(want_xy)))

    # TEST_LIFTS deliberately gives its direct-entry type-21 and type-24
    # sectors nonzero tags. EnterSectorSpecial() ignores that tag when a
    # grounded player enters, so both still need Build's local step link.
    lift_test = 'maps/testmaps/test_lifts.map'
    if lift_test not in names:
        out.append('shipped Kex lift regression map missing: %s' % lift_test)
    else:
        m = K.locate(z.read(lift_test))
        events = [(etype, esect, tag) for etype, esect, tag, _param in m.events
                  if etype in (21, 24)]
        if events != [(21, 7, 1), (24, 63, 2)]:
            out.append('%s tagged direct-lift contract changed: %s'
                       % (lift_test, events))

    # test_platforms is the remaster's focused DROP_PAD source map. Its pad
    # has a dormant (-1) reset tag and its stepped-on floor splits in two,
    # so it protects both the no-channel case and the paired-surface table
    # from regressing to the former generic lotag-6 elevator.
    pad_test = 'maps/testmaps/test_platforms.map'
    if pad_test not in names:
        out.append('shipped Kex drop-pad regression map missing: %s' % pad_test)
    else:
        m = K.locate(z.read(pad_test))
        sects, _walls, ranges = K.convert(m, tilebase=0)
        _keep, remap = K.sector_remap(sects, ranges)
        pads = [(group, remap[piece], ceiling, tag)
                for group, piece, ceiling, tag in m.kex_drop_surfaces
                if piece in remap]
        floors = [piece for _group, piece, ceiling, _tag in pads if not ceiling]
        ceilings = [piece for _group, piece, ceiling, _tag in pads if ceiling]
        if (len(pads) != 3 or len(floors) != 2 or len(ceilings) != 1
                or {group for group, _piece, _ceiling, _tag in pads} != {0}
                or {tag for _group, _piece, _ceiling, tag in pads} != {-1}):
            out.append('%s paired drop-pad conversion changed: %s' % (pad_test, pads))
        if any(sects[piece]['lotag'] % 1000 == 6 for _group, piece, _ceiling, _tag in pads):
            out.append('%s drop pad reverted to generic lotag 6' % pad_test)
    return out


def verify_sealed_doors(z):
    """Protect UseWallSpecial's type-7 direct-use exception."""
    out = []
    names = [n for n in z.namelist()
             if n.lower().startswith('maps/') and n.lower().endswith('.map')
             and '/old/' not in n.lower()]
    retail = [n for n in names if '/testmaps/' not in n.lower()]
    count = sum(sum(e[0] == 7 for e in K.locate(z.read(n)).events) for n in retail)
    if count != 109:
        out.append('retail DOOR_OPEN_ONLY_MESSAGE census changed from 109 to %d' % count)

    try:
        localization = z.read('localization/loc_english.txt').decode('utf-8')
    except (KeyError, UnicodeDecodeError) as exc:
        out.append('cannot read remaster localization for sealed door: %s' % exc)
    else:
        if not re.search(r'^str_092\s*=\s*"This Door is Sealed"\s*$', localization,
                         flags=re.MULTILINE):
            out.append('remaster $str_92 sealed-door text changed')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        init = re.sub(r'\s+', '', open(os.path.join(srcdir, 'init.cpp')).read())
        player = re.sub(r'\s+', '', open(os.path.join(srcdir, 'player.cpp')).read())
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex sealed-door bridge: %s' % exc]
    for needle in ("s_['kexsealeddoor']=True", 'out.append((si,91,0))'):
        if needle not in mapper:
            out.append('mapper sealed-door marker bridge changed: %s' % needle)
    for needle in ('case91:', 'KexSealedDoor_Add(sprite[nSprite].sectnum)',
                   'KexSealedDoor_Reset()'):
        if needle not in init:
            out.append('init sealed-door marker bridge changed: %s' % needle)
    for needle in ('if(!KexSealedDoor_Use(nearTagSector))',
                   'runlist_SignalRun(sector[nearTagSector].lotag-1,'):
        if needle not in player:
            out.append('player sealed-door use bridge changed: %s' % needle)
    for needle in ('intKexSealedDoor_Use(intnSector)',
                   'StatusMessage(300,"ThisDoorisSealed")',
                   'VB_KEXSEALED_TEST', 'KexSealedDoor_Tick()'):
        if needle not in native:
            out.append('native sealed-door bridge changed: %s' % needle)

    # TEST_SWITCH has two type-7 doors on the same channel.  It protects both
    # marker serialization and the rule that remote/channel activation is
    # distinct from a player's direct use.
    test = 'maps/testmaps/test_switch.map'
    if test not in names:
        return out + ['shipped Kex sealed-door regression map missing: %s' % test]
    m = K.locate(z.read(test))
    sects, walls, ranges = K.convert(m, tilebase=0)
    _keep, remap = K.sector_remap(sects, ranges)
    sprites = K.build_sprites(m, sects, walls, ranges, remap)
    want = sorted(remap[esect] for etype, esect, _tag, _param in m.events
                  if etype == 7 and esect in remap)
    got = sorted(s['sectnum'] for s in sprites if s['lotag'] == 91)
    if got != want:
        out.append('%s sealed-door markers differ: markers %s source %s'
                   % (test, got, want))
    return out


def verify_locked_doors(z):
    """Protect UseLockedDoor's four source-localized missing-key messages."""
    out = []
    names = [n for n in z.namelist()
             if n.lower().startswith('maps/') and n.lower().endswith('.map')
             and '/old/' not in n.lower()]
    retail = [n for n in names if '/testmaps/' not in n.lower()]
    counts = {etype: sum(sum(e[0] == etype for e in K.locate(z.read(n)).events)
                         for n in retail)
              for etype in (3, 4, 5, 6)}
    if counts != {3: 12, 4: 12, 5: 13, 6: 11}:
        out.append('retail locked-door census changed from {3: 12, 4: 12, 5: 13, 6: 11} to %s'
                   % counts)
    try:
        localization = z.read('localization/loc_english.txt').decode('utf-8')
    except (KeyError, UnicodeDecodeError) as exc:
        out.append('cannot read remaster localization for locked doors: %s' % exc)
    else:
        want = {
            116: 'You Need the Symbol of Power', 117: 'You Need the Symbol of Time',
            118: 'You Need the Symbol of War', 119: 'You Need the Symbol of Earth',
        }
        for key, text in want.items():
            if not re.search(r'^str_%03d\s*=\s*"%s"\s*$' % (key, re.escape(text)),
                             localization, flags=re.MULTILINE):
                out.append('remaster $str_%03d locked-door text changed' % key)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        switch = re.sub(r'\s+', '', open(os.path.join(srcdir, 'switch.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex locked-door bridge: %s' % exc]
    for needle in ("key={'DOOR_KEY1':1,'DOOR_KEY2':2,",
                   "hitag=((1<<(key-1))ifkeyelse0)*1000+chan"):
        if needle not in mapper:
            out.append('mapper locked-door key-mask bridge changed: %s' % needle)
    for needle in ('staticcharconst*KexLockedDoor_Message(uint16_tkeyMask)',
                   'case1<<12:return"YouNeedtheSymbolofPower"',
                   'case1<<13:return"YouNeedtheSymbolofTime"',
                   'case1<<14:return"YouNeedtheSymbolofWar"',
                   'case1<<15:return"YouNeedtheSymbolofEarth"',
                   'KexLockedDoor_Message(SwitchData[nSwitch].field_12)',
                   'VB_KEXLOCK_TEST', 'FuncSwPressSector(nLocalPlayer|0x40000,0,nRun)'):
        if needle not in switch:
            out.append('native locked-door bridge changed: %s' % needle)

    test = 'maps/testmaps/test_door.map'
    if test not in names:
        return out + ['shipped Kex locked-door regression map missing: %s' % test]
    m = K.locate(z.read(test))
    events = [e for e in m.events if e[0] in (3, 4, 5, 6)]
    if events != [(3, 4, -1, -1)]:
        out.append('%s source locked-door contract changed: %s' % (test, events))
    return out


def verify_teleports(z, outdir, include_testmaps=False):
    """Check Kex TeleportEvent's exact 71 -> 72 destination contract."""
    out = []
    names = [n for n in z.namelist()
             if n.lower().startswith('maps/') and n.lower().endswith('.map')
             and '/old/' not in n.lower()
             and (include_testmaps or '/testmaps/' not in n.lower())]
    expected = []
    for name in names:
        m = K.locate(z.read(name))
        sects, _walls, ranges = K.convert(m, tilebase=0)
        _keep, remap = K.sector_remap(sects, ranges)
        base = os.path.splitext(os.path.basename(name))[0].upper()
        expected.extend(K._teleport_rows(m, remap, base))

    retail = [r for r in expected if not r[0].startswith('TEST_')]
    focused = [r for r in expected if r[0].startswith('TEST_')]
    if len(retail) != 1:
        out.append('retail Kex teleport census changed from 1 to %d' % len(retail))
    if include_testmaps and len(focused) != 2:
        out.append('focused Kex teleport census changed from 2 to %d' % len(focused))

    try:
        actual = []
        for line in open(os.path.join(outdir, 'kextele.txt')):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            fields = line.split()
            if len(fields) != 7:
                out.append('malformed kextele row: %s' % line)
                continue
            if not include_testmaps and fields[0].upper().startswith('TEST_'):
                continue
            actual.append((fields[0],) + tuple(map(int, fields[1:])))
    except (OSError, ValueError) as exc:
        return out + ['cannot read kextele.txt: %s' % exc]

    if sorted(actual) != sorted(expected):
        out.append('kextele rows differ from Kex 71 -> 72 TeleportEvent: '
                   'expected %s, found %s' % (sorted(expected), sorted(actual)))

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(os.path.dirname(__file__),
                                            'kexmap.py')).read())
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex teleport bridge: %s' % exc]
    for needle in ('def_teleport_rows(m,remap,mapkey):',
                   'ifetype==72andetagnotindestinations:',
                   'ifetype!=71:', 'out.append((mapkey,remap[esect],remap[dsect],cx,cy,cz,-1))'):
        if needle not in mapper:
            out.append('mapper no longer preserves Kex teleport rule: %s' % needle)
    for needle in ('t.ang=ang<0?-1:', 'if(!KexPlayer_OnSectorFloor(nPlayer,nSector))',
                   'sprite[nSprite].z=getflorzofslope(s_tele[0].pad,', 'if(s_tele[i].ang>=0)',
                   'legacygeneratedtablessuppliedafacing', 'VB_KEXTELE_TEST',
                   'mychangespritesect(nSprite,s_tele[0].pad)',
                   'sprite[nSprite].ang==s_teleTestAng',
                   'KexSound_Get("k_teleport")', 'sounds/teleport.wav',
                   's_teleFlash=255', 's_teleFlash=max(0,s_teleFlash-8)',
                   'intKexTele_FlashAlpha(void)', 'VB_KEXTELE_TEST'):
        if needle not in native:
            out.append('native Kex teleport bridge changed: %s' % needle)
    tele_tick = native[native.find('voidKexTele_Tick(void)'):
                       native.find('intKexBreak_Available(void)')]
    for needle in ('sprite[nSprite].xvel=0;', 'sprite[nSprite].yvel=0;',
                   'sprite[nSprite].zvel=0;'):
        if needle in tele_tick:
            out.append('native Kex teleport bridge incorrectly clears momentum: %s'
                       % needle)
    try:
        view = re.sub(r'\s+', '', open(os.path.join(srcdir, 'view.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex teleport overlay bridge: %s' % exc]
    for needle in ('#include"palette.h"', 'KexTele_FlashAlpha()',
                   'fullscreen_tint_gl(0,255,8,(uint8_t)teleAlpha)',
                   'videoGetRenderMode()>=REND_POLYMOST'):
        if needle not in view:
            out.append('Kex teleport HUD-overlay bridge changed: %s' % needle)
    return out


def verify_shrines(z, outdir):
    """Protect Kex ArtifactEvent's consume-then-spawn behavior."""
    out = []
    name = next((n for n in z.namelist()
                 if n.lower() == 'maps/kilentry.map'), None)
    if name is None:
        return ['retail KILENTRY shrine map is missing']
    m = K.locate(z.read(name))
    sects, _walls, ranges = K.convert(m, tilebase=0)
    _keep, remap = K.sector_remap(sects, ranges)
    pads, spawns = K._artifact_shrine_rows(m, remap, 'KILENTRY')
    if len(pads) != 6 or len(spawns) != 6:
        out.append('KILENTRY ArtifactEvent census changed: pads %d destinations %d'
                   % (len(pads), len(spawns)))
    if sorted(artifact for _map, _sect, artifact in pads) != list(range(6)):
        out.append('KILENTRY shrine pads no longer cover artifacts 0..5')
    if sorted(artifact for _map, _sect, artifact, _x, _y, _z in spawns) != list(range(6)):
        out.append('KILENTRY shrine destinations no longer cover artifacts 0..5')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(os.path.dirname(__file__),
                                            'kexmap.py')).read())
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexinv.cpp')).read())
        init = re.sub(r'\s+', '', open(os.path.join(srcdir, 'init.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex shrine bridge: %s' % exc]
    for needle in ('def_artifact_shrine_rows(m,remap,mapkey):',
                   'out.append((si,86,arti-1,z,0,x,y))'):
        if needle not in mapper:
            out.append('mapper no longer preserves Kex shrine rule: %s' % needle)
    for needle in ('KexInv_AddShrineSpawn', 'KexPlayer_OnSectorFloor(nPlayer,pad.padSector)',
                   'sprite[nSprite].z=getflorzofslope(pad.padSector,',
                   'KexInv_TakeArtifact(nPlayer,pad.nArtifact);',
                   'KexScript_ArtifactTaken(pad.nArtifact);', 'VB_KEXSHRINE_TEST'):
        if needle not in native:
            out.append('native Kex shrine bridge changed: %s' % needle)
    if 'case86:' not in init:
        out.append('Kex shrine destination marker is not registered by init.cpp')
    return out


def verify_remote_triggers(z, outdir, include_testmaps=False):
    """Protect the source dispatcher semantics of Kex REMOTE_TRIGGER."""
    out = []
    names = [n for n in z.namelist()
             if n.lower().startswith('maps/') and n.lower().endswith('.map')
             and '/old/' not in n.lower()
             and (include_testmaps or '/testmaps/' not in n.lower())]
    rows = []
    for name in names:
        m = K.locate(z.read(name))
        sects, _walls, ranges = K.convert(m, tilebase=0)
        _keep, remap = K.sector_remap(sects, ranges)
        base = os.path.splitext(os.path.basename(name))[0].upper()
        rows.extend(K._remote_trigger_rows(m, remap, base))

    retail = [r for r in rows if not r[0].startswith('TEST_')]
    focused = [r for r in rows if r[0].startswith('TEST_')]
    retail_one = [r for r in retail if r[4]]
    if len(retail) != 418 or len(retail_one) != 84:
        out.append('retail REMOTE_TRIGGER census changed: %d regions, %d one-shot'
                   % (len(retail), len(retail_one)))
    if len(retail) - len(retail_one) != 334:
        out.append('retail REMOTE_TRIGGER repeatable census changed: %d'
                   % (len(retail) - len(retail_one)))
    if include_testmaps and (len(focused) != 1 or not focused[0][4]):
        out.append('focused REMOTE_TRIGGER one-shot test changed: %s' % focused)
    if not any(tag == 0 for _map, _sect, tag, _chan, _one in retail):
        out.append('retail REMOTE_TRIGGER tag-zero route disappeared')
    if not any(tag < -1 for _map, _sect, tag, _chan, _one in retail):
        out.append('retail REMOTE_TRIGGER signed-tag route disappeared')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(os.path.dirname(__file__),
                                            'kexmap.py')).read())
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        init = re.sub(r'\s+', '', open(os.path.join(srcdir, 'init.cpp')).read())
        move = re.sub(r'\s+', '', open(os.path.join(srcdir, 'move.cpp')).read())
    except OSError as exc:
        return out + ['cannot read Kex remote-trigger bridge: %s' % exc]
    for needle in ('def_remote_trigger_rows(m,remap,mapkey):',
                   'KEX_REMOTE_ONE_SHOT_TYPES=(7,8,9,21,22,24)',
                   "elifname=='REMOTE_TRIGGER':", 'lotag=87'):
        if needle not in mapper:
            out.append('mapper no longer preserves Kex remote rule: %s' % needle)
    for needle in ('voidKexRemote_Add(intnSector,intnTag,intnChannel,intbOneShot)',
                   'KexPlate_ResetChain((int)r.tag+1)',
                   'Kex_FireChannel(r.channel)', 'if(r.oneShot)r.done=1',
                   'VB_KEXREMOTE_TEST', 'KexScript_FireTag(p.tag==0?100000:p.tag,'):
        if needle not in native:
            out.append('native remote-trigger bridge changed: %s' % needle)
    if 'case87:' not in init or 'KexRemote_Add(' not in init:
        out.append('remote-trigger marker is not registered by init.cpp')
    if 'KexRemote_Tick();' not in move:
        out.append('remote-trigger dispatcher is not ticked')
    return out


def verify_grounded_specials():
    """All Kex sector specials except type 50 require floor contact."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        inventory = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexinv.cpp')).read())
    except OSError as exc:
        return ['cannot read Kex grounded-special bridge: %s' % exc]
    for needle in ('intKexPlayer_OnSectorFloor(intnPlayer,intnSector)',
                   'getflorzofslope((int16_t)nSector,',
                   '#defineKEX_PLAYER_STEP_HEIGHT48',
                   'klabs(sprite[nSprite].z-floor)<=KEX_Z_PER_UNIT*KEX_PLAYER_STEP_HEIGHT',
                   'if(!KexPlayer_OnSectorFloor(nPlayer,nSector))',
                   'if(!KexPlayer_OnSectorFloor(nPlayer,p.sector))',
                   'KexPlayer_OnSectorFloor(nPlayer,s_padSurface[j].sector)',
                   'KexPlayer_OnSectorFloor(nPlayer,sc.sector)'):
        if needle not in native:
            out.append('native grounded-special bridge changed: %s' % needle)
    if 'KexPlayer_OnSectorFloor(nPlayer,pad.padSector)' not in inventory:
        out.append('artifact pads no longer require Kex floor contact')
    remote_start = native.find('voidKexRemote_Tick(void)')
    remote_end = native.find('structKexFloatRow', remote_start)
    remote = native[remote_start:remote_end]
    if 'KexPlayer_OnSectorFloor' in remote:
        out.append('REMOTE_TRIGGER incorrectly acquired a floor-contact gate')
    return out


def verify_hazards(z):
    """Protect the remaster's distinct lava/slime player-damage rules."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        header = re.sub(r'\s+', '', open(os.path.join(srcdir, 'init.h')).read())
        init = re.sub(r'\s+', '', open(os.path.join(srcdir, 'init.cpp')).read())
        move = re.sub(r'\s+', '', open(os.path.join(srcdir, 'move.cpp')).read())
    except OSError as exc:
        return ['cannot read Kex lava/slime bridge: %s' % exc]

    # The mapper deliberately keeps the classic lotag-96 damage marker, but
    # its two values identify the two KEX floor-face flags to the native path.
    for needle in ('out.append((si,96,40))', 'out.append((si,96,16))',
                   'KEX_WALL_EXTRA_LAVA=-32004',
                   'KEX_WALL_EXTRA_SLIME=-32005',
                   "walls[-1]['_kex_hazard']=KEX_WALL_EXTRA_LAVA",
                   "walls[-1]['_kex_hazard']=KEX_WALL_EXTRA_SLIME",
                   "hazard=w.get('_kex_hazard')"):
        if needle not in mapper:
            out.append('mapper no longer records Kex lava/slime marker: %s' % needle)
    for needle in ('enumKexSectorHazard', 'kKexHazardLava', 'kKexHazardSlime',
                   'kKexHazardWallLava=-32004', 'kKexHazardWallSlime=-32005',
                   'externuint8_tKexHazard[]'):
        if needle not in header:
            out.append('Kex hazard declaration changed: %s' % needle)
    for needle in ('uint8_tKexHazard[kMaxSectors]',
                   'KexHazard[i]=kKexHazardNone',
                   'KexHazard[nSector]|=nHitag==40?kKexHazardLava',
                   'nHitag==16?kKexHazardSlime'):
        if needle not in init:
            out.append('Kex hazard construction changed: %s' % needle)
    for needle in ('staticuint8_ts_kexHazardTicks[kMaxPlayers][2]',
                   'KexHazard_ResetPlayer(shortnSprite,uint8_tnHazards)',
                   's_kexHazardTicks[nPlayer][tick]=(uint8_t)(previous+2)',
                   'intconstperiod=nHazard==kKexHazardSlime&&anklets?64:32',
                   'nDamage=anklets?50:max(1,((int)PlayerList[nPlayer].nHealth+3)/4)',
                   'nDamage=anklets?5:375',
                   'KexHazard_DamagePlayer(nSprite,kKexHazardLava,kexWasAirborne)',
                   'KexHazard_DamagePlayer(nSprite,kKexHazardSlime,kexWasAirborne)',
                   '(nRet&0xC000)==0x8000',
                   'wall[nWall].extra==kKexHazardWallLava',
                   'wall[nWall].extra==kKexHazardWallSlime',
                   'hazard==kKexHazardSlime&&kexWasAirborne'):
        if needle not in move:
            out.append('native Kex lava/slime rule changed: %s' % needle)
    return out


def verify_forcefields(z, outdir):
    """Keep Scepter force fields contact-driven, paired, and persistent."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        move = re.sub(r'\s+', '', open(os.path.join(srcdir, 'move.cpp')).read())
        view = re.sub(r'\s+', '', open(os.path.join(srcdir, 'view.cpp')).read())
        save = re.sub(r'\s+', '', open(os.path.join(srcdir, 'save.cpp')).read())
    except OSError as exc:
        return ['cannot read Kex force-field bridge: %s' % exc]

    # The remaster has real FF_FORCEFIELD faces, including KILENTRY's
    # height-selected bands. This rejects a texture-only/no-topology bridge.
    source_faces = 0
    source_maps = set()
    for name in z.namelist():
        low = name.lower()
        if not low.startswith('maps/') or not low.endswith('.map') or '/old/' in low:
            continue
        try:
            m = K.locate(z.read(name))
        except (ValueError, struct.error):
            continue
        count = sum(1 for f in m.faces if f.flags & K.KEX_FACE_FORCEFIELD)
        if count:
            source_faces += count
            source_maps.add(os.path.splitext(os.path.basename(name))[0].upper())
    expected_maps = {'COLONY_SAT', 'KILENTRY', 'MARSH', 'PASS', 'SELBUROW', 'SHRINE', 'THOTH'}
    if source_faces != 50 or source_maps != expected_maps:
        out.append('remaster FF_FORCEFIELD census changed: %d faces on %s'
                   % (source_faces, sorted(source_maps)))

    for needle in ("'k_forceoff':('sounds/forcefieldoff.wav',0)",
                   "'k_electrocute':('sounds/pelectrocute.wav',0)",
                   'KEX_FACE_FORCEFIELD'):
        if needle not in mapper:
            out.append('force-field mapper contract changed: %s' % needle)
    for needle in ('intKexForce_OnWallHit(intnSprite,intnWall)',
                   'runlist_DamageEnemy(nSprite,-1,50)',
                   'intKexForce_StunMove(intnSprite)',
                   's_kexForceStun[nPlayer]=30',
                   'KexForce_OpenPortal(nPortal)',
                   'KexForce_ClearStaticWall(nWall)',
                   'if(s_kexPortal[p].nextsector==owner)',
                   'voidKexForce_Tick(void)',
                   's_kexForceFlash=max(0,s_kexForceFlash-8)',
                   's_kexForceFlash=192',
                   'voidKexHud_DamageFlash(void)',
                   's_kexDamageFlash=max(0,s_kexDamageFlash-16)',
                   's_kexPickupFlash=max(0,s_kexPickupFlash-16)',
                   'Write(s_kexForceOpen,sizeof(s_kexForceOpen))',
                   'Read(s_kexForceOpen,sizeof(s_kexForceOpen))',
                   's_kexForceOpen[which]'):
        if needle not in native:
            out.append('native contact force-field rule changed: %s' % needle)
    if 'KexWall_ForceOpen' in native:
        out.append('force fields are globally passable to a Scepter holder')
    if 'kSaveVersion=10004' not in save:
        out.append('force-field save-format revision is missing')
    if 'KexForce_StunMove(nSprite)' not in move:
        out.append('electrocution no longer blocks remaster player movement')
    if 'KexForce_Tick();' not in move:
        out.append('electrocution feedback no longer advances every tick')
    if 'KexHudFlash_Tick();' not in move:
        out.append('damage/pickup feedback no longer advances every tick')
    for needle in ('KexHud_DamageFlashAlpha()', 'fullscreen_tint_gl(255,32,32,',
                   'KexHud_PickupFlashAlpha()', 'fullscreen_tint_gl(32,32,255,',
                   'KexForce_ElectrocuteAlpha()', 'fullscreen_tint_gl(32,255,32,'):
        if needle not in view:
            out.append('remaster HUD overlay changed: %s' % needle)
    force_call = move.find('KexForce_OnWallHit(nSprite,nWall);')
    hazard_call = move.find('KexHazard_DamagePlayer(nSprite,hazard,')
    if force_call < 0 or hazard_call < 0 or force_call > hazard_call:
        out.append('force field is no longer handled before wall lava/slime')

    try:
        force_rows = set()
        for line in open(os.path.join(outdir, 'kexforce.txt')):
            p = line.split()
            if len(p) == 2 and not p[0].startswith('#') and p[0] != 'map':
                force_rows.add(p[0].replace('KEX_', '').replace('.MAP', ''))
        if force_rows != expected_maps:
            out.append('kexforce.txt maps changed: %s' % sorted(force_rows))

        dynamic = 0
        for line in open(os.path.join(outdir, 'kexportal.txt')):
            p = line.split()
            if len(p) == 10 and not p[0].startswith('#'):
                dynamic += int(p[8]) & K.KEX_FACE_FORCEFIELD != 0 and int(p[9]) == 8146
        if dynamic != 7:
            out.append('height-selected KILENTRY force bands changed: %d' % dynamic)
    except (OSError, ValueError) as exc:
        out.append('cannot read converted force-field metadata: %s' % exc)
    return out


def verify_fall_damage():
    """Keep KEX's CheckFallDamage formula out of the DOS landing branch."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        inv = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexinv.cpp')).read())
        player = re.sub(r'\s+', '', open(os.path.join(srcdir, 'player.cpp')).read())
    except OSError as exc:
        return ['cannot read Kex fall-damage bridge: %s' % exc]
    for needle in ('intKexInv_AnkhCount(intnPlayer)',
                   'KexInv_Bits(KexInvList[nPlayer].ankhs)',
                   'base*(KexInv_AnkhCount(nPlayer)+1)'):
        if needle not in inv:
            out.append('Kex Ankh multiplier changed: %s' % needle)
    for needle in ('if(g_kexMode)',
                   'intnDamage=(zVel-5472)/12',
                   'nDamage*=KexInv_AnkhCount(nPlayer)+1',
                   'runlist_DamageEnemy(nPlayerSprite,-1,(short)nDamage)',
                   'KexSound_Get("jon_fdie")',
                   'elseif(zVel>=6500)'):
        if needle not in player:
            out.append('native Kex fall-damage rule changed: %s' % needle)
    return out


def verify_air_supply():
    """Keep KEX's 64-unit, 60 Hz underwater-air model separate from DOS."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        player = re.sub(r'\s+', '', open(os.path.join(srcdir, 'player.cpp')).read())
        hud = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexmenu.cpp')).read())
        inv = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexinv.h')).read())
        save = re.sub(r'\s+', '', open(os.path.join(srcdir, 'save.cpp')).read())
    except OSError as exc:
        return ['cannot read Kex air-supply bridge: %s' % exc]
    for needle in ('staticvoidKexAirSupply_Update(intnPlayer,shortnSprite,intbUnderwater)',
                   'for(inttick=0;tick<2;tick++)',
                   'intconstperiod=masked?100:5',
                   'p.nAir=min(64,p.nAir+16)',
                   'p.nAir=max(0,p.nAir-2)',
                   'staticuint8_ts_kexNeedGasp[kMaxPlayers]',
                   'sprite[nSprite].zvel-=3584',
                   'KexSound_Get(((int32_t)totalclock&1)?"k_dolph1":"k_dolph2")',
                   'KexSound_Get("jon_gasp")',
                   'Read(&s_kexNeedGasp[nLocalPlayer],sizeof(s_kexNeedGasp[nLocalPlayer]))',
                   'Write(&s_kexNeedGasp[nLocalPlayer],sizeof(s_kexNeedGasp[nLocalPlayer]))',
                   '(nBreathTimer[nPlayer]&0x7f)==0',
                   'runlist_DamageEnemy(nSprite,-1,nBreathTimer[nPlayer]>>2)',
                   'PlayerList[nPlayer].nAir=g_kexMode?64:100',
                   'nBreathTimer[nPlayer]=g_kexMode?0:90',
                   'KexInv_GiveArtifact(nPlayer,kKexArtiMask);PlayerCheckItemRespawnOrDelete(nPickupSprite,nItem);break;',
                   'if(g_kexMode){KexAirSupply_Update(nPlayer,nPlayerSprite,var_5C);}'):
        if needle not in player:
            out.append('native Kex air-supply rule changed: %s' % needle)
    for needle in ('clamp((int)pl.nAir,0,64)', 'bw*air/64'):
        if needle not in hud:
            out.append('Kex air HUD scale changed: %s' % needle)
    for needle in ('#defineKEX_DOLLS_DOLPHIN10',
                   '#defineKEX_DOLLS_VULTURE14'):
        if needle not in inv:
            out.append('Kex team-doll ability threshold changed: %s' % needle)
    for needle in ('KexInv_DollCount(nPlayer)>=KEX_DOLLS_DOLPHIN',
                   'x*=2', 'y*=2'):
        if needle not in player:
            out.append('Kex Dolphin swim rule changed: %s' % needle)
    if 'kSaveVersion=10004' not in save:
        out.append('Kex air-state save-format revision is missing')
    return out


def verify_player_damage():
    """Keep the KEX-only puppet OnDamage feedback and alias widths intact."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        mapper = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        player = re.sub(r'\s+', '', open(os.path.join(srcdir, 'player.cpp')).read())
        move = re.sub(r'\s+', '', open(os.path.join(srcdir, 'move.cpp')).read())
        sound = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
    except OSError as exc:
        return ['cannot read KEX player-damage bridge: %s' % exc]

    for needle in ("'k_electrocute':('sounds/pelectrocute.wav',0)",
                   "'k_jdie1':('sounds/pdeath01.wav',0)",
                   "'k_jdie2':('sounds/pdeath02.wav',0)",
                   "'k_whit5':('sounds/pwpain05.wav',0)"):
        if needle not in mapper:
            out.append('KEX player-damage alias changed: %s' % needle)
    for needle in ('structKexSoundAlias{charname[24];charpath[32];uint8_tloop;}',
                   'Bsscanf(line,"%23s%31s%d",a.name,a.path,&loop)',
                   'staticstruct{charname[24];intnSound;}s_ownSound'):
        if needle not in sound:
            out.append('KEX sound-name capacity changed: %s' % needle)
    for needle in ('staticvoidKexPlayer_DamageFeedback(intnPlayer,shortnSprite,shortnInstigator)',
                   'KexForce_IsElectrocuted(nSprite)',
                   'KexHazard_IsActive(nPlayer)',
                   'KexPlayer_PlaySound(nSprite,"k_electrocute")',
                   'KexPlayer_PlaySound(nSprite,"k_jdie1")',
                   'KexPlayer_PlaySound(nSprite,"jon_drwn")',
                   'KexPlayer_PlaySound(nSprite,"jon_die3")',
                   'intconstnViewSector=nPlayerViewSect[nPlayer]',
                   'if(!g_kexMode&&nSprite2>-1)'):
        if needle not in player:
            out.append('KEX player-damage feedback changed: %s' % needle)
    for needle in ('intKexHazard_IsActive(intnPlayer)',
                   'KexSound_Get("jon_pois")',
                   'KexSound_Get("k_jdie2")'):
        if needle not in move:
            out.append('KEX hazard-death feedback changed: %s' % needle)
    for needle in ('s_kexForceDamage[nPlayer]=1',
                   's_kexForceDamage[nPlayer]=0',
                   '&&s_kexForceDamage[nPlayer]!=0'):
        if needle not in sound:
            out.append('KEX electric-damage lifetime changed: %s' % needle)
    return out


def verify_player_use():
    """Keep the remaster's use ray and direct-event set separate from DOS."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        player = re.sub(r'\s+', '', open(os.path.join(srcdir, 'player.cpp')).read())
        move = re.sub(r'\s+', '', open(os.path.join(srcdir, 'move.cpp')).read())
        wall = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        switch = re.sub(r'\s+', '', open(os.path.join(srcdir, 'switch.cpp')).read())
    except OSError as exc:
        return ['cannot read KEX player-use bridge: %s' % exc]
    for needle in ('intconstuseRange=g_kexMode?956:1024',
                   '&nearTagSector,&nearTagWall,&nearTagSprite,(int32_t*)&nearHitDist,useRange,2,NULL',
                   '(!g_kexMode||KexUse_WallAllowed(nearTagWall))',
                   '(!g_kexMode||KexUse_SectorAllowed(nearTagSector))',
                   'if(!KexSealedDoor_Use(nearTagSector))'):
        if needle not in player:
            out.append('KEX player-use contract changed: %s' % needle)
    if 'staticconstexprintkKexPlayerContactRadius2=1143' not in move:
        out.append('KEX player-radius conversion changed')
    if 'intKexSwitch_PlaySound(intnWall,intnPlayer)' not in wall:
        out.append('KEX switch-use sound bridge changed')
    if 'if(!KexSwitch_PlaySound(nWall,a&0xFFFF))' not in switch:
        out.append('KEX switch-use sound dispatch changed')
    for needle in ('intKexUse_WallAllowed(intnWall)',
                   'intKexUse_SectorAllowed(intnSector)',
                   'case2001:', 'case1075:', 'case2075:', 'case2009:',
                   'return!KexHold_Sector(nSector)',
                   'if(s_kexSealedDoor[nSector])'):
        if needle not in wall:
            out.append('KEX direct-use allowlist changed: %s' % needle)
    return out


def verify_player_travel():
    """Keep source travel-map IDs and destinationPosition warps intact."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        travel = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        player = re.sub(r'\s+', '', open(os.path.join(srcdir, 'player.cpp')).read())
        loop = re.sub(r'\s+', '', open(os.path.join(srcdir, 'exhumed.cpp')).read())
        inventory = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexinv.cpp')).read())
        mapper = re.sub(r'\s+', '', open(os.path.join(os.path.dirname(__file__), 'kexmap.py')).read())
    except OSError as exc:
        return ['cannot read KEX player-travel bridge: %s' % exc]
    for needle in ('intKexTravel_PrepareSpawn(intnPlayer,intnAngle)',
                   'if(nMap==24)nMap=22',
                   'if(nMap==0||nMap==22)',
                   'KexTravel_ScheduleWarp(0,-16896,82944)',
                   'KexTravel_ScheduleWarp(0,-16896,21504)',
                   'SetSavePoint(nPlayer,s_kexTravelWarp.x,s_kexTravelWarp.y,s_kexTravelWarp.z,nSector,(short)nAngle)'):
        if needle not in travel:
            out.append('KEX travel destination bridge changed: %s' % needle)
    for needle in ('s_travelTestTarget==1||s_travelTestTarget==22||s_travelTestTarget==24',
                   'sprite[nPlayerSprite].x==0&&sprite[nPlayerSprite].y==-16896&&sprite[nPlayerSprite].sectnum==expectedSector'):
        if needle not in inventory:
            out.append('KEX travel runtime regression probe changed: %s' % needle)
    for needle in ('staticvoidKex_ChangeMap(charconst*name)',
                   'KexCampaign_StagePlayer(nLocalPlayer);',
                   '{"KILENTRY",7,kKexScriptChangeMap,0,-1,"KILARENA",{0,0}}',
                   '{"KILARENA",0,kKexScriptChangeMap,1,-1,"KILENTRY",{0,0}}'):
        if needle not in travel:
            out.append('KEX scripted travel persistence changed: %s' % needle)
    for needle in ('sprite[nPickupSprite].hitag==22||sprite[nPickupSprite].hitag==24',
                   'KexTravel_ChangeMap(22)'):
        if needle not in player:
            out.append('KEX Ramses travel pickup bridge changed: %s' % needle)
    if 'KexTravel_PrepareSpawn(i,inita)' not in loop:
        out.append('KEX travel warp is no longer applied before RestartPlayer')
    if ('RestartPlayer(i);InitPlayerKeys(i);KexCampaign_FinishSpawn(i);'
            'KexCheckpoint_CaptureEntry(i);KexCheckpoint_FinishSpawn(i);') not in loop:
        out.append('KEX spawn persistence ordering changed')
    if 'voidInitPlayerKeys(shortnPlayer){PlayerList[nPlayer].keys=0;}' not in player:
        out.append('KEX map-ready key reset changed')
    campaign = inventory[inventory.find('structKexCampaignPlayerState'):
                         inventory.find('intKexDifficulty_Get')]
    for needle in ('save.health=p.nHealth', 'save.weapons=nPlayerWeapons[nPlayer]',
                   'Bmemcpy(save.ammo,p.nAmmo,sizeof(save.ammo))',
                   'save.currentWeapon=p.nCurrentWeapon',
                   'save.valid=1', 'save.valid=0'):
        if needle not in campaign:
            out.append('KEX carried player-state contract changed: %s' % needle)
    for forbidden in ('int16_tkeys;', 'save.keys=', 'p.keys=save.keys'):
        if forbidden in campaign:
            out.append('KEX campaign state incorrectly carries map-local keys: %s' % forbidden)
    checkpoint = inventory[inventory.find('structKexCheckpointEntryState'):]
    for needle in ('structKexCheckpointEntryState',
                   'staticKexCheckpointEntryStates_checkpointEntry',
                   'voidKexCheckpoint_CaptureEntry(intnPlayer)',
                   'KexCheckpoint_CapturePlayerState(nPlayer,s_checkpointEntry.player)',
                   'elseif(s_checkpointEntry.valid)',
                   'KexCheckpoint_RestorePlayerState(nPlayer,s_checkpointEntry.player)',
                   '!Bmemcmp(&KexInvList[nPlayer],&s_deathTestInv,sizeof(KexInv))'):
        if needle not in checkpoint:
            out.append('KEX entrance-reload persistence changed: %s' % needle)
    for needle in ('intKexCheckpoint_HasCheckpoint(void)',
                   'intKexCheckpoint_Reload(intnPlayer,intbRestartLevel)',
                   'if(!bRestartLevel&&!KexCheckpoint_HasCheckpoint())return0',
                   'if(bRestartLevel)', 'KexCheckpoint_ClearActive();'):
        if needle not in checkpoint:
            out.append('KEX manual checkpoint-reload contract changed: %s' % needle)
    pause = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexmenu.cpp')).read())
    for needle in ('"RELOADLASTCHECKPOINT"', '"RESTARTLEVEL"',
                   'casekKexPauseReload:', 'casekKexPauseRestart:',
                   'return5;', 'return6;'):
        if needle not in pause:
            out.append('KEX checkpoint pause action changed: %s' % needle)
    for needle in ('case5:if(KexCheckpoint_Reload(nLocalPlayer,0))gotoLOOP1;',
                   'case6:if(KexCheckpoint_Reload(nLocalPlayer,1))gotoLOOP1;'):
        if needle not in loop:
            out.append('KEX checkpoint pause dispatch changed: %s' % needle)
    if "'Obj_Ramses_Body':(60,22)" not in mapper:
        out.append('converter no longer emits source Map_Tomb2 index 22')
    return out


def verify_light_fields(z, outdir, include_testmaps=False):
    """Every converted KEX map must retain face-local source light samples."""
    out = []
    try:
        files = os.listdir(outdir)
    except OSError as exc:
        return ['cannot list converted KEX directory: %s' % exc]

    maps = sorted(name for name in files
                  if name.upper().startswith('KEX_') and name.upper().endswith('.MAP'))
    if not maps:
        return ['converted KEX directory has no maps']

    # Use the same source-map roster as convert_install().  Campaign maps live
    # directly under maps/, while --testmaps adds maps/testmaps/*.map; matching
    # by basename also preserves the deliberately mixed retail casing.
    source_by_stem = {}
    for source in z.namelist():
        low = source.lower()
        if (not low.startswith('maps/') or not low.endswith('.map')
                or '/old/' in low
                or (not include_testmaps and '/testmaps/' in low)):
            continue
        source_by_stem.setdefault(os.path.splitext(os.path.basename(source))[0].lower(),
                                  source)
    for name in maps:
        stem = os.path.splitext(name)[0]
        path = os.path.join(outdir, stem + '.LIT')
        try:
            lines = open(path, encoding='ascii').read().splitlines()
            header = lines[0].split()
            if len(header) != 3 or header[0] != 'KEXLIT3':
                raise ValueError('not a KEXLIT3 sidecar')
            declared, mapped = int(header[1]), int(header[2])
        except (OSError, ValueError, IndexError) as exc:
            out.append('%s is missing or malformed: %s' % (os.path.basename(path), exc))
            continue

        samples, surfaces = [], []
        malformed = False
        if len(lines) != 1 + declared + mapped or declared <= 0:
            malformed = True
        for line in lines[1:1 + declared]:
            try:
                fields = tuple(int(value) for value in line.split())
            except ValueError:
                malformed = True
                break
            if len(fields) != 7:
                malformed = True
                break
            samples.append(fields)
        for line in lines[1 + declared:]:
            try:
                fields = tuple(int(value) for value in line.split())
            except ValueError:
                malformed = True
                break
            if len(fields) != 3:
                malformed = True
                break
            surfaces.append(fields)
        if (malformed or declared != len(samples) or mapped != len(surfaces)
                or any(channel < 0 or channel > 255
                       for _face, _x, _y, _z, r, g, b in samples
                       for channel in (r, g, b))
                or samples != sorted(samples)
                or len({sample[:4] for sample in samples}) != len(samples)
                or surfaces != sorted(surfaces)
                or len(set(surfaces)) != len(surfaces)):
            out.append('%s has an invalid KEXLIT3 table (%d samples, %d surfaces)'
                       % (os.path.basename(path), declared, mapped))
            continue

        source = source_by_stem.get(stem[4:].lower()) \
            if stem.upper().startswith('KEX_') else None
        if source is None:
            out.append('%s has no source map for its light field' % stem)
            continue
        try:
            m = K.locate(z.read(source))
            build_sectors, build_walls = load_build(os.path.join(outdir, name))
        except (ValueError, struct.error, OSError) as exc:
            out.append('%s cannot validate face provenance: %s' % (stem, exc))
            continue

        expected_surface = {(2, i): None for i in range(len(build_sectors) * 2)}
        expected_surface.update(((1, i * 2), None) for i in range(len(build_walls)))
        actual_surface = {(kind, ident): face for kind, ident, face in surfaces}
        missing = sorted(set(expected_surface) - set(actual_surface))
        if missing:
            out.append('%s lacks %d emitted-surface light mappings' % (stem, len(missing)))
        if any(face < -1 or face >= m.nface for _kind, _ident, face in surfaces):
            out.append('%s maps a surface to an invalid source face' % stem)

        wanted_faces = {face for face in actual_surface.values()
                        if 0 <= face < m.nface and K._lit(m.faces[face])}
        expected_samples = set()
        for face in wanted_faces:
            f = m.faces[face]
            acc = {}
            for vi in range(f.tileVertStart, min(f.tileVertEnd + 1, len(m.verts))):
                x, y, z_, (r, g, b, _a) = m.verts[vi]
                key = (int(round(x * K.WORLD)), int(round(-y * K.WORLD)),
                       int(round(-z_ * K.SCALE * K.WORLD)))
                total = acc.get(key)
                if total is None:
                    acc[key] = [r, g, b, 1]
                else:
                    total[0] += r; total[1] += g; total[2] += b; total[3] += 1
            expected_samples.update((face, x, y, z_, int(round(r / count)),
                                     int(round(g / count)), int(round(b / count)))
                                    for (x, y, z_), (r, g, b, count) in acc.items())
        if set(samples) != expected_samples:
            out.append('%s face-local samples differ from retail vertex lighting' % stem)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        vlight = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexvlight.cpp')).read())
        bridge = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
        polymost = re.sub(r'\s+', '', open(os.path.join(srcdir, '..', '..', 'build', 'src', 'polymost.cpp')).read())
    except OSError as exc:
        return out + ['cannot read native KEX light-field bridge: %s' % exc]
    for needle, source in (
            ('Bstrncat(path,".LIT",sizeof(path)-Bstrlen(path)-1)', vlight),
            ('KEXLIT3', vlight), ('KexVL_CurrentFace', vlight),
            ('KexVL_FaceColor', vlight), ('KexVL_ColorAt', vlight),
            ('s.wallnum', vlight),
            ('polymost_kexShadeAt=KexVL_ShadeAt', bridge),
            ('polymost_kexColorAt=KexVL_ColorAt', bridge),
            ('polymost_kexVertexLight=KexVL_Ready()&&!Bgetenv("VB_KEX_FLATLIGHT")', bridge),
            ('polymost_kexVertexColor', polymost), ('sectnum,wallnum,0', polymost)):
        if needle not in source:
            out.append('native KEX light-field bridge changed: %s' % needle)
    return out


def verify_sprite_interpolation():
    """Keep KEX-only high-refresh sprite presentation off game state."""
    out = []
    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    engine_dir = os.path.abspath(os.path.join(srcdir, '..', '..', 'build', 'src'))
    try:
        view = re.sub(r'\s+', '', open(os.path.join(srcdir, 'view.cpp')).read())
        loop = re.sub(r'\s+', '', open(os.path.join(srcdir, 'exhumed.cpp')).read())
        engine = re.sub(r'\s+', '', open(os.path.join(engine_dir, 'engine.cpp')).read())
    except OSError as exc:
        return ['cannot read KEX sprite-interpolation bridge: %s' % exc]

    for needle in ('voidKexView_SnapshotSprites(void)',
                   'if(!g_kexMode)return;',
                   'staticKexSpritePoses_kexSpritePose[MAXSPRITES];',
                   'pose.sectnum!=t.sectnum',
                   'kKexSpriteInterpolationMaxDelta',
                   't.x=pose.x+mulscale16(dx,s_kexSpriteSmoothRatio);',
                   'VB_KEX_SPRITE_INTERP_TEST'):
        if needle not in view:
            out.append('KEX sprite interpolation changed: %s' % needle)
    for needle in ('insertsprite_callback=KexView_SpriteInserted;',
                   'deletesprite_callback=KexView_SpriteDeleted;',
                   'KexView_SnapshotSprites();'):
        if needle not in loop:
            out.append('KEX sprite interpolation tick bridge changed: %s' % needle)
    for needle in ('void(*insertsprite_callback)(int16_tspritenum)=NULL;',
                   'insertsprite_callback((int16_t)newspritenum);',
                   'void(*deletesprite_callback)(int16_tspritenum)=NULL;',
                   'deletesprite_callback(spritenum);'):
        if needle not in engine:
            out.append('sprite lifecycle observer changed: %s' % needle)
    return out


def verify_flyers(z, outdir):
    """Crow/wasp/Queen-seedling art and authored actor contract."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    expected_actors = {
        'AI_Bird': ('defs/actors/monsters/bird.txt',
                    {'health': 10, 'radius': 16, 'scale': 1.5, 'moveSpeed': 8}),
        'AI_Wasp': ('defs/actors/monsters/wasp.txt',
                    {'health': 60, 'radius': 18, 'scale': 1.25, 'moveSpeed': 16}),
        'AI_Seedling': ('defs/actors/monsters/queen.txt',
                        {'health': 100, 'radius': 36, 'scale': 2.0, 'moveSpeed': 16}),
    }
    for actor, (rel, fields) in expected_actors.items():
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1') if real else ''
        match = re.search(r'Actor\s+' + re.escape(actor) + r'\s*\{(.*?)\n\}', text, re.S)
        if not match:
            out.append('retail actor definition is missing: %s' % actor)
            continue
        body = match.group(1)
        for field, wanted in fields.items():
            value = re.search(r'^\s*' + re.escape(field) + r'\s+([-+\d.]+)', body, re.M)
            if not value or abs(float(value.group(1)) - wanted) > 1e-6:
                out.append('%s %s changed from authored %s' % (actor, field, wanted))

    # These callbacks determine behavior timing, not merely appearance. Count
    # frame blocks before each action so a retail data update cannot silently
    # invalidate the generated SEQ event positions.
    for rel, anim, action, wanted_frame in (
            ('sprites/monsters/wasp_anim.txt', 'wasp_chase',
             r'A_PlayLocalSound\("sounds/waspact\.wav"\)', 0),
            ('sprites/monsters/wasp_anim.txt', 'wasp_attack',
             r'A_PlayLocalSound\("sounds/waspatk\.wav"\)', 0),
            ('sprites/monsters/wasp_anim.txt', 'wasp_attack',
             r'A_MeleeAttack\(16,\s*20\)', 0),
            ('sprites/monsters/wasp_anim.txt', 'wasp_attack',
             r'A_PlayLocalSound\("sounds/wasphit\.wav"\)', 3),
            ('sprites/monsters/wasp_anim.txt', 'wasp_attack',
             r'A_WaspBackoff\(\)', 7),
            ('sprites/monsters/bird_anim.txt', 'bird_attack',
             r'A_MeleeAttack\(32,\s*20\)', 0),
            ('sprites/monsters/seedling_anim.txt', 'seedling_attack',
             r'A_MeleeAttack\(48,\s*20\)', 3)):
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        match = re.search(r'^' + re.escape(anim) + r'\s*$', text, re.M)
        if not match:
            out.append('retail animation is missing: %s' % anim)
            continue
        body, _end = K._brace_block(text, match.end())
        hit = re.search(r'\baction\s+' + action, body)
        frame = len(re.findall(r'^\s*frame\s*$', body[:hit.start()], re.M)) - 1 \
            if hit else -1
        if frame != wanted_frame:
            out.append('%s callback %s moved from authored frame %d to %d'
                       % (anim, action, wanted_frame, frame))
    wasp_real = names.get('sprites/monsters/wasp_anim.txt')
    wasp_text = z.read(wasp_real).decode('latin-1').replace('\r', '') if wasp_real else ''
    for anim, frames, bob_count in (('wasp_base', 4, 0),
                                    ('wasp_chase', 4, 4),
                                    ('wasp_attack', 8, 0)):
        match = re.search(r'^' + anim + r'\s*$', wasp_text, re.M)
        if not match:
            continue
        body, _end = K._brace_block(wasp_text, match.end())
        actual_frames = len(re.findall(r'^\s*frame\s*$', body, re.M))
        actual_bobs = len(re.findall(r'\baction\s+A_WaspBob\(\)', body))
        if actual_frames != frames:
            out.append('%s changed from %d authored frames to %d'
                       % (anim, frames, actual_frames))
        if actual_bobs != bob_count:
            out.append('%s has %d A_WaspBob callbacks, expected %d'
                       % (anim, actual_bobs, bob_count))
    wasp_script = re.sub(r'\s+', '', z.read(names['scripts/p_enemy.txt']).decode('latin-1')) \
        if 'scripts/p_enemy.txt' in names else ''
    for token in (
            'voidA_WaspBob(kexSlaveActor@actor)',
            'floatfHeightLevel=((actor.GetOrigin().z+actor.GetHeight())-pTarget.GetOrigin().z)-pTarget.GetHeight();',
            'if(fHeightLevel>128.0f||fHeightLevel<-128.0f)',
            'floatfDist=pTarget.GetOrigin().Distance(actor.GetOrigin());',
            'if(fDist<=192.0f)',
            'intbob=(actor.GetGameTicks()/32)%360;',
            'actor.GetVelocity().z=Math::Cos(float(bob))*2.0f;'):
        if token not in wasp_script:
            out.append('A_WaspBob authored contract changed: %s' % token)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        wasp_native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'wasp.cpp')).read())
    except OSError:
        wasp_native = ''
        out.append('cannot read native Wasp bob bridge')
    for token in (
            'staticconstexprintkKexWaspTicksPerCycle=360*32;',
            'return-mulscale14(Cos(angle),2*kKexZPerUnit);',
            'dx*=16;dy*=16;',
            'dx*dx+dy*dy+dz*dz>reach*reach;',
            'intconstz0=KexWaspBobTick(wasp.nAttackTime,wasp.dTime,writeBob);',
            'intconstz1=KexWaspBobTick(wasp.nAttackTime,wasp.dTime,false);',
            'KexWaspBobAdvance(nWasp,nSprite,WaspList[nWasp].nTarget,nAction==1)',
            'KexWaspAngleChase(nSprite,nTarget,',
            'KexWaspMove(nSprite,kexWaspZMove);',
            'KexWaspbobtest:%s'):
        if token not in wasp_native:
            out.append('native A_WaspBob bridge no longer implements %s' % token)

    for token in (
            'voidA_BirdWander(kexSlaveActor@actor)',
            'actor.m_fScratchParam1=(kexSlaveRandomizer::RFloat()*2048);',
            'if(((actor.GetGameTicks()+int(actor.m_fScratchParam1))&512)!=0)',
            'actor.GetVelocity().x=Math::Sin(actor.GetRotation().z)*2.0f;',
            'actor.GetVelocity().y=Math::Cos(actor.GetRotation().z)*2.0f;',
            'voidA_BirdRetreat(kexSlaveActor@actor)',
            'actor.GetRotation().z+=Math::Deg2Rad(4.0f);',
            'minHeight=(actor.GetCeilingHeight()-actor.GetHeight())-1.0f;',
            'if(actor.GetVelocity().z<3.0f||(actor.GetOrigin().z+actor.GetVelocity().z)>=minHeight)'):
        if token not in wasp_script:
            out.append('A_Bird authored contract changed: %s' % token)

    for token in (
            'staticboolKexBirdSequenceExact()',
            'returnSeqSize[base]==1&&SeqSize[base+9]==8&&SeqSize[base+18]==12&&SeqSize[base+27]==6;',
            'gameTicks=(short)((sourceTicks+2)&2047);',
            'intconstturn=((gameTicks+wasp.field_14)&512)!=0?384:-384;',
            'returnzVelocity>-(3*kKexZPerUnit)||(int64_t)z+zVelocity<=stopZ;',
            'KexBirdMovePair(nSprite);',
            'KexBirdflighttest:%s'):
        if token not in wasp_native:
            out.append('native A_Bird bridge no longer implements %s' % token)
    bird_real = names.get('sprites/monsters/bird_anim.txt')
    bird_text = z.read(bird_real).decode('latin-1').replace('\r', '') if bird_real else ''
    bird_attack = re.search(r'^bird_attack\s*$', bird_text, re.M)
    if bird_attack:
        body, _end = K._brace_block(bird_text, bird_attack.end())
        if not re.search(r'\bdelay\s+8\b', body):
            out.append('bird_attack no longer has its authored eight-tick delay')

    feather_match = re.search(r'^bird_feather\s*$', bird_text, re.M)
    feather_body, _end = K._brace_block(bird_text, feather_match.end()) \
        if feather_match else ('', -1)
    feather_frames = K.parse_anim_full(z, names, 'monsters/bird_feather')
    feather_actions = [i for i, block in enumerate(
        re.split(r'^\s*frame\s*$', feather_body, flags=re.M)[1:])
        if 'A_ThrustZVelocity(-0.5)' in block and 'A_FeatherDrop()' in block]
    if (len(feather_frames) != K.KEX_BIRD_FEATHER_LIFETIME
            or any(f.get('delay') != 1 for f in feather_frames)
            or feather_actions != [8, 14]):
        out.append('bird_feather no longer has its authored 15-frame f8/f14 contract')

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    tilefiles = {int(tile): path for tile, path in re.findall(
        r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}

    for filename, first, last, sound in (
            ('WASP.SEQ', 7870, 7884, b'WASP_STG'),
            ('HAWK.SEQ', 7600, 7635, b'BIRD_ATK'),
            ('SEEDLING.SEQ', 7850, 7864, b'SEED_ATK')):
        path = os.path.join(outdir, filename)
        try:
            seq = S.read_seq(open(path, 'rb').read())
        except (OSError, AssertionError, ValueError, struct.error):
            out.append('%s is missing or malformed' % filename)
            continue
        wanted_tiles = set(range(first, last + 1))
        actual_tiles = set(seq['ck_pict'])
        if actual_tiles != wanted_tiles:
            out.append('%s tile coverage differs: %d missing, %d extra'
                       % (filename, len(wanted_tiles - actual_tiles),
                          len(actual_tiles - wanted_tiles)))
        if sound not in [n.rstrip(b'\0') for n in seq['snd_names']]:
            out.append('%s has no %s sound' % (filename, sound.decode('ascii')))
        if filename == 'WASP.SEQ':
            if len(seq['seq_size']) != 36:
                out.append('WASP.SEQ has %d action slots, expected 36'
                           % len(seq['seq_size']))
            elif (seq['seq_size'][0:8] != [4] * 8
                  or seq['seq_size'][9:17] != [8] * 8
                  or seq['seq_size'][18:26] != [1] * 8
                  or seq['seq_size'][27:35] != [4] * 8):
                out.append('WASP.SEQ does not retain authored 4/8/1/4 action timing')
            wanted_events = ([
                (seq['seq_base'][27], 0),
                (seq['seq_base'][9], 1),
                (seq['seq_base'][9] + 3, 2),
            ] if len(seq['seq_base']) > 34 else [])
            if seq['snd_events'] != wanted_events:
                out.append('WASP.SEQ act/attack/hit events do not match authored frames')
            if [n.rstrip(b'\0') for n in seq['snd_names']] != \
                    [b'WASP_BUZ', b'WASP_STG', b'WASP_HIT']:
                out.append('WASP.SEQ sound table differs from the retail Wasp events')
        elif filename == 'HAWK.SEQ':
            if len(seq['seq_size']) != 36:
                out.append('HAWK.SEQ has %d action slots, expected 36'
                           % len(seq['seq_size']))
            elif (seq['seq_size'][0:8] != [1] * 8
                  or seq['seq_size'][9:17] != [8] * 8
                  or seq['seq_size'][18:26] != [12] * 8
                  or seq['seq_size'][27:35] != [6] * 8):
                out.append('HAWK.SEQ does not retain authored calm/attack/retreat/chase timing')
            wanted_events = ([
                (seq['seq_base'][27] + 1, 0),
                (seq['seq_base'][27] + 4, 1),
                (seq['seq_base'][9], 2),
            ] if len(seq['seq_base']) > 34 else [])
            if seq['snd_events'] != wanted_events:
                out.append('HAWK.SEQ flap/attack events do not match authored frames')
            if [n.rstrip(b'\0') for n in seq['snd_names']] != \
                    [b'BIRD_WNG', b'BIRD_WN2', b'BIRD_ATK']:
                out.append('HAWK.SEQ sound table differs from the retail bird events')
        elif filename == 'SEEDLING.SEQ':
            if len(seq['seq_size']) != 31:
                out.append('SEEDLING.SEQ has %d action slots, expected 31'
                           % len(seq['seq_size']))
            elif (seq['seq_size'][0:8] != [1] * 8
                  or seq['seq_size'][9:17] != [6] * 8):
                out.append('SEEDLING.SEQ does not retain authored 1/6-frame chase/attack timing')
            wanted_event = (seq['seq_base'][9] + 3, 0) if len(seq['seq_base']) > 9 else None
            if seq['snd_events'] != [wanted_event]:
                out.append('SEEDLING.SEQ bite event is not attack frame three')
        for tile in wanted_tiles:
            rel = tilefiles.get(tile)
            if not rel:
                out.append('%s tile %d has no tilefromtexture' % (filename, tile))
                continue
            try:
                with open(os.path.join(outdir, rel), 'rb') as image:
                    head = image.read(24)
                if head[:8] != b'\x89PNG\r\n\x1a\n' or len(head) < 24:
                    raise ValueError
            except (OSError, ValueError):
                out.append('%s tile %d has no valid PNG' % (filename, tile))

    try:
        feather_seq = S.read_seq(open(os.path.join(outdir, 'KEXBIRDFEATHER.SEQ'),
                                      'rb').read())
        table = K._sprite_table(z, names, 'monsters', 'bird')
        expected_x, expected_y = [], []
        for frame in feather_frames:
            group, base, index, ox, oy, flip = frame['sprites'][0]
            rect = table.get(index)
            if ((group, base, index, flip) != ('monsters', 'bird', 32, 0)
                    or rect is None):
                raise ValueError('retail feather sprite changed')
            expected_x.append(160 + int((ox + ox + rect[2]) / 2))
            expected_y.append(100 + int((oy + oy + rect[3]) / 2))
        if (feather_seq['tag'] != b'IH' or feather_seq['centerx'] != 160
                or feather_seq['centery'] != 100
                or feather_seq['seq_base'] != [0]
                or feather_seq['seq_size'] != [K.KEX_BIRD_FEATHER_LIFETIME]
                or feather_seq['seq_flag'] != [-1]
                or feather_seq['fr_base'] != list(range(K.KEX_BIRD_FEATHER_LIFETIME))
                or feather_seq['fr_size'] != [1] * K.KEX_BIRD_FEATHER_LIFETIME
                or feather_seq['fr_flag'] != [0] * K.KEX_BIRD_FEATHER_LIFETIME
                or feather_seq['ck_x'] != expected_x
                or feather_seq['ck_y'] != expected_y
                or feather_seq['ck_pict'] != [7632] * K.KEX_BIRD_FEATHER_LIFETIME
                or feather_seq['ck_flag'] != [0] * K.KEX_BIRD_FEATHER_LIFETIME
                or feather_seq['snd_names'] or feather_seq['snd_events']
                or feather_seq['trailing']):
            out.append('KEXBIRDFEATHER.SEQ is not the exact 15-frame crop loop')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXBIRDFEATHER.SEQ is missing or invalid: %s' % exc)

    try:
        anim_native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'anims.cpp')).read())
        seq_native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'sequence.cpp')).read())
        header_native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'sequence.h')).read())
    except OSError as exc:
        out.append('cannot read native Bird feather bridge: %s' % exc)
        anim_native = seq_native = header_native = ''
    for token in (
            'intKexBirdFeather_Spawn(',
            'BuildAnim(-1,kSeqKexBirdFeather,0,x,y,z,nSector,64,kAnimLoop)',
            'AnimList[nAnim].nFrame=RandomSize(4)%kKexBirdFeatherLifetime;',
            'intconstnextFrame=(nFrame+1)%kKexBirdFeatherLifetime;',
            'if(nextFrame==8||nextFrame==14)',
            'sprite[nSprite].zvel=kKexBirdFeatherFallVelocity;',
            'KexBirdFeatherDrop(nSprite)',
            'KexBirdfeathertest:%s'):
        if token not in anim_native:
            out.append('native Obj_Bird_Feathers bridge no longer implements %s' % token)
    if '#definekMaxSEQFiles97' not in seq_native or '"kexbirdfeather"' not in seq_native:
        out.append('native Obj_Bird_Feathers optional sequence table changed')
    if 'kSeqKexBirdFeather' not in header_native:
        out.append('native Obj_Bird_Feathers optional sequence enum changed')

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        if not re.search(r'^seed_atk\s+sounds/queenatk05\.wav\s+0$', sounds, re.M):
            out.append('seedling bite sound alias is missing')
        if not re.search(r'^bird_wn2\s+sounds/birdflap02\.wav\s+0$', sounds, re.M):
            out.append('second bird flap sound alias is missing')
        if not re.search(r'^wasp_hit\s+sounds/wasphit\.wav\s+0$', sounds, re.M):
            out.append('Wasp hit sound alias is missing')
        if not (re.search(r'^wasp_die\s+sounds/waspdeath02\.wav\s+0$', sounds, re.M)
                and re.search(r'^wasp_dy1\s+sounds/waspdeath01\.wav\s+0$', sounds, re.M)):
            out.append('Wasp random death sound pair is missing')
    except OSError:
        out.append('kexsound.txt is missing')
    return out


def verify_sentry(z, outdir):
    """Exact AI_Sentry actor, animation, sound and generated ROACH.SEQ contract."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    actor_real = names.get('defs/actors/monsters/sentry.txt')
    actor_text = z.read(actor_real).decode('latin-1').replace('\r', '') \
        if actor_real else ''
    actor_match = re.search(r'Actor\s+AI_Sentry\s*\{(.*?)\n\}', actor_text, re.S)
    expected = {'health': 180, 'radius': 48, 'height': 192,
                'scale': 1.5, 'moveSpeed': 8, 'turnSpeed': 2,
                'painChance': 24}
    if not actor_match:
        out.append('retail actor definition is missing: AI_Sentry')
    else:
        body = actor_match.group(1)
        for field, wanted in expected.items():
            value = re.search(r'^\s*' + re.escape(field) + r'\s+([-+\d.]+)',
                              body, re.M)
            if not value or abs(float(value.group(1)) - wanted) > 1e-6:
                out.append('AI_Sentry %s changed from authored %s'
                           % (field, wanted))

    anim_real = names.get('sprites/monsters/sentry_anim.txt')
    anim_text = z.read(anim_real).decode('latin-1').replace('\r', '') \
        if anim_real else ''
    anim_bodies = {}
    for anim, count in (('sentry_idle', 1), ('sentry_chase', 8),
                        ('sentry_pain', 12), ('sentry_attack', 10)):
        match = re.search(r'^' + anim + r'\s*$', anim_text, re.M)
        if not match:
            out.append('retail animation is missing: %s' % anim)
            continue
        body, _end = K._brace_block(anim_text, match.end())
        anim_bodies[anim] = body
        actual = len(re.findall(r'^\s*frame\s*$', body, re.M))
        if actual != count:
            out.append('%s changed from %d authored frames to %d'
                       % (anim, count, actual))

    def callback_frame(anim, pattern):
        body = anim_bodies.get(anim, '')
        hit = re.search(pattern, body)
        return (len(re.findall(r'^\s*frame\s*$', body[:hit.start()], re.M)) - 1
                if hit else -1)

    callbacks = (
        ('sentry_chase', r'\baction\s+A_SentryChatter\(\)', 0),
        ('sentry_pain', r'\baction\s+A_PlayLocalSound\("sounds/sentrypain01\.wav"\)', 0),
        ('sentry_pain', r'\bgoto\s+"monsters/sentry_chase"', 10),
        ('sentry_attack', r'\baction\s+A_PlayLocalSound\("sounds/sentryfire\.wav"\)', 6),
        ('sentry_attack', r'\baction\s+A_FireProjectile\("Proj_Blaster",\s*0,\s*0,\s*4,\s*96,\s*24\)', 6),
        ('sentry_attack', r'\bgoto\s+"monsters/sentry_chase"', 9),
    )
    for anim, pattern, wanted in callbacks:
        actual = callback_frame(anim, pattern)
        if actual != wanted:
            out.append('%s callback %s moved from frame %d to %d'
                       % (anim, pattern, wanted, actual))

    path = os.path.join(outdir, 'ROACH.SEQ')
    try:
        seq = S.read_seq(open(path, 'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        return out + ['ROACH.SEQ is missing or malformed']
    wanted_tiles = set(range(7890, 7961))
    actual_tiles = set(seq['ck_pict'])
    if actual_tiles != wanted_tiles:
        out.append('ROACH.SEQ exact tile coverage differs: %d missing, %d extra'
                   % (len(wanted_tiles - actual_tiles),
                      len(actual_tiles - wanted_tiles)))
    if len(seq['seq_size']) != 36:
        out.append('ROACH.SEQ has %d exact action slots, expected 36'
                   % len(seq['seq_size']))
    elif (seq['seq_size'][0:8] != [1] * 8
          or seq['seq_size'][9:17] != [8] * 8
          or seq['seq_size'][18:26] != [12] * 8
          or seq['seq_size'][27:35] != [10] * 8):
        out.append('ROACH.SEQ does not retain authored 1/8/12/10 timing')
    wanted_events = ([
        (seq['seq_base'][18], 0),
        (seq['seq_base'][27] + 6, 1),
    ] if len(seq['seq_base']) > 34 else [])
    if seq['snd_events'] != wanted_events:
        out.append('ROACH.SEQ pain/fire sounds are not on authored frames 0/6')
    if [n.rstrip(b'\0') for n in seq['snd_names']] != \
            [b'ROCH_WLD', b'ROCH_GUN']:
        out.append('ROACH.SEQ sound table differs from retail Sentry callbacks')
    wanted_bright = ([seq['seq_base'][27 + d] + 6 for d in range(8)]
                     if len(seq['seq_base']) > 34 else [])
    actual_bright = [i for i, flag in enumerate(seq['fr_flag']) if flag & 4]
    if actual_bright != wanted_bright:
        out.append('ROACH.SEQ muzzle fullbright flags differ from attack frame six')
    if any(seq['ck_flag']):
        out.append('ROACH.SEQ precomposited eight-view tiles are being flipped twice')

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    tilefiles = {int(tile): rel for tile, rel in re.findall(
        r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}
    for tile in wanted_tiles:
        rel = tilefiles.get(tile)
        try:
            with open(os.path.join(outdir, rel), 'rb') as image:
                head = image.read(24)
            if head[:8] != b'\x89PNG\r\n\x1a\n' or len(head) < 24:
                raise ValueError
        except (OSError, TypeError, ValueError):
            out.append('ROACH.SEQ tile %d has no valid PNG' % tile)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        aliases = (
            ('roch_icu', 'sentryact01'), ('roch_wld', 'sentrypain01'),
            ('roch_gun', 'sentryfire'), ('roch_die', 'sentrydeath01'),
            ('roch_die2', 'sentrydeath02'), ('roch_act2', 'sentryact02'),
            ('roch_act3', 'sentryact03'), ('roch_act4', 'sentryact04'))
        for alias, wav in aliases:
            if not re.search(r'^' + alias + r'\s+sounds/' + wav + r'\.wav\s+0$',
                             sounds, re.M):
                out.append('Sentry sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing')
    return out


def verify_health_units():
    """The native runlist uses four internal health units per KEX point."""
    out = []
    src = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    expected = {
        'anubis.cpp': ('KexHealthUnits(100)',),
        'fish.cpp': ('KexHealthUnits(40)',),
        'lavadude.cpp': ('KexHealthUnits(400)',),
        'lion.cpp': ('KexHealthUnits(150)',),
        'mummy.cpp': ('KexHealthUnits(130)',),
        'roach.cpp': ('KexHealthUnits(180)',),
        'scorp.cpp': ('KexHealthUnits(4000)', 'KexHealthPoints(nHealth)'),
        'set.cpp': ('KexHealthUnits(3000)', 'KexHealthPoints(nHealth)'),
        'spider.cpp': ('KexHealthUnits(20)',),
        'wasp.cpp': ('KexHealthUnits(10)', 'KexHealthUnits(60)',
                     'KexHealthUnits(100)'),
        'queen.cpp': ('KexHealthUnits(3600)', 'KexHealthUnits(200)'),
    }
    try:
        header = open(os.path.join(src, 'kexinv.h')).read()
        if not re.search(r'#define\s+KEX_HEALTH_UNIT_SCALE\s+4\b', header):
            out.append('KEX health unit scale is not four')
    except OSError:
        out.append('cannot read kexinv.h health-unit contract')
    for filename, needles in expected.items():
        try:
            text = open(os.path.join(src, filename)).read()
        except OSError:
            out.append('cannot read health implementation: %s' % filename)
            continue
        for needle in needles:
            if needle not in text:
                out.append('%s no longer applies %s' % (filename, needle))
    return out


def verify_common_enemies(z, outdir):
    """Retail dimensions and melee callbacks for native counterparts."""
    out = []
    names = {n.lower(): n for n in z.namelist()}
    actor_specs = {
        'AI_Anubis': ('defs/actors/monsters/anubis.txt',
                      {'radius': 48, 'height': 192, 'scale': 1.5,
                       'health': 100, 'moveSpeed': 8}),
        'AI_Mummy': ('defs/actors/monsters/mummy.txt',
                     {'radius': 48, 'height': 192, 'scale': 1.5,
                      'health': 130, 'moveSpeed': 6, 'meleeExtraDist': 48}),
        'AI_Bastet': ('defs/actors/monsters/bastet.txt',
                      {'radius': 48, 'height': 192, 'scale': 1.5,
                       'health': 150, 'moveSpeed': 24, 'meleeExtraDist': 16,
                       'noEnterWater': 1, 'noDropOff': 1, 'painChance': 24}),
        'AI_Fish': ('defs/actors/monsters/fish.txt',
                    {'radius': 16, 'height': 64, 'scale': 1.0,
                     'health': 40, 'moveSpeed': 8, 'meleeExtraDist': 16,
                     'noExitWater': 1, 'painChance': 0, 'flying': 1,
                     'flyAdjustViewLevel': 1, 'noInFighting': 1,
                     'collideZOffset': -24}),
        'AI_Bird': ('defs/actors/monsters/bird.txt',
                    {'radius': 16, 'height': 128, 'scale': 1.5,
                     'health': 10, 'moveSpeed': 8, 'flying': 1,
                     'flyAdjustViewLevel': 1, 'gravity': 0,
                     'collideZOffset': -40}),
        'AI_Wasp': ('defs/actors/monsters/wasp.txt',
                    {'radius': 18, 'height': 64, 'scale': 1.25,
                     'health': 60, 'moveSpeed': 16, 'flying': 1,
                     'gravity': 0, 'floatBob': 1,
                     'collideZOffset': -24}),
        'AI_Spider': ('defs/actors/monsters/spider.txt',
                      {'radius': 16, 'height': 64, 'scale': 1.0,
                       'health': 20, 'moveSpeed': 5, 'meleeExtraDist': 20}),
        'AI_Scorp': ('defs/actors/monsters/scorp.txt',
                     {'radius': 16, 'height': 64, 'scale': 1.0,
                      'health': 20, 'moveSpeed': 5}),
    }
    actor_bodies = {}
    for actor, (rel, fields) in actor_specs.items():
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        match = re.search(r'Actor\s+' + re.escape(actor) + r'\s*\{(.*?)\n\}',
                          text, re.S)
        if not match:
            out.append('retail actor definition is missing: %s' % actor)
            continue
        body = match.group(1)
        actor_bodies[actor] = body
        for field, wanted in fields.items():
            value = re.search(r'^\s*' + re.escape(field)
                              + r'\s+([-+\d.]+)', body, re.M)
            actual = float(value.group(1)) if value else None
            if actual is None or abs(actual - wanted) > 1e-6:
                out.append('%s %s changed from authored %s to %s'
                           % (actor, field, wanted, actual))

    anubis_body = actor_bodies.get('AI_Anubis', '')
    for field, wanted in (('spawnAnim', 'anubis_idle'),
                          ('chaseAnim', 'anubis_chase'),
                          ('painAnim', 'anubis_pain'),
                          ('meleeAnim', 'anubis_melee'),
                          ('rangeAttackAnim', 'anubis_attack_range'),
                          ('deathAnim', 'anubis_death_pc'),
                          ('gibAnim', 'anubis_death')):
        value = re.search(r'^\s*' + field + r'\s+(\S+)', anubis_body, re.M)
        actual = value.group(1).strip('"').rsplit('/', 1)[-1] if value else None
        if actual != wanted:
            out.append('AI_Anubis %s changed from %s to %s'
                       % (field, wanted, actual))

    mummy_body = actor_bodies.get('AI_Mummy', '')
    for field, wanted in (('spawnAnim', 'mummy_idle'),
                          ('chaseAnim', 'mummy_chase'),
                          ('painAnim', 'mummy_pain'),
                          ('meleeAnim', 'mummy_melee'),
                          ('rangeAttackAnim', 'mummy_range_attack'),
                          ('deathAnim', 'mummy_death_pc'),
                          ('gibAnim', 'mummy_death')):
        value = re.search(r'^\s*' + field + r'\s+(\S+)', mummy_body, re.M)
        actual = value.group(1).strip('"').rsplit('/', 1)[-1] if value else None
        if actual != wanted:
            out.append('AI_Mummy %s changed from %s to %s'
                       % (field, wanted, actual))

    for actor, contract in {
            'AI_Bastet': (('spawnAnim', 'bastet_idle'),
                          ('chaseAnim', 'bastet_chase'),
                          ('painAnim', 'bastet_pain'),
                          ('meleeAnim', 'bastet_attack'),
                          ('deathAnim', 'bastet_death')),
            'AI_Fish': (('spawnAnim', 'fish_idle'),
                        ('chaseAnim', 'fish_chase'),
                        ('meleeAnim', 'fish_attack'),
                        ('deathAnim', 'fish_death'))}.items():
        body = actor_bodies.get(actor, '')
        for field, wanted in contract:
            value = re.search(r'^\s*' + field + r'\s+(\S+)', body, re.M)
            actual = value.group(1).strip('"').rsplit('/', 1)[-1] \
                if value else None
            if actual != wanted:
                out.append('%s %s changed from %s to %s'
                           % (actor, field, wanted, actual))

    # Animation callbacks are the combat contract: frame, range and logical
    # damage. These values differ sharply from the native DOS counterparts.
    melee_specs = {
        ('sprites/monsters/anubis_anim.txt', 'anubis_melee'): {
            'frames': 24,
            'hits': [(6, 48.0, 20), (19, 48.0, 20)],
            'sounds': [(3, 'anubismelee'), (6, 'anubisslap'),
                       (16, 'anubismelee'), (19, 'anubisslap')],
            'checks': [(4, 'monsters/anubis_melee_miss', 0.0),
                       (17, 'monsters/anubis_melee_miss', 0.0)],
        },
        ('sprites/monsters/mummy_anim.txt', 'mummy_melee'): {
            'frames': 13, 'hits': [(11, 48.0, 20)],
            'sounds': [(3, 'mumatk02')],
            'checks': [(12, 'monsters/mummy_chase', 48.0)],
        },
        ('sprites/monsters/bastet_anim.txt', 'bastet_attack'): {
            'frames': 11, 'hits': [(1, 48.0, 20), (7, 48.0, 20)],
            'sounds': [(1, 'bastetact01'), (2, 'bastetatk01'),
                       (6, 'bastetact01'), (7, 'bastetatk01')],
            'checks': [(10, 'monsters/bastet_chase', 48.0)],
        },
        ('sprites/monsters/fish_anim.txt', 'fish_attack'): {
            'frames': 14, 'hits': [(12, 48.0, 20)],
            'sounds': [(2, 'fishatk'), (8, 'fishatk')],
            'checks': [(13, 'monsters/fish_chase', 16.0)],
        },
        ('sprites/monsters/spider_anim.txt', 'spider_attack'): {
            # The retail comment says 13, but the parsed definition contains
            # fourteen frame blocks (two pre-roll blocks plus ##000..##011).
            'frames': 14, 'hits': [(6, 20.0, 10)],
            'sounds': [(6, 'spidchomp')],
            'checks': [(0, 'monsters/spider_base', 20.0)],
        },
        ('sprites/monsters/scorp_anim.txt', 'scorp_attack'): {
            'frames': 13, 'hits': [(12, 16.0, 10)],
            'sounds': [(12, 'spidchomp')],
            'checks': [(11, 'monsters/scorp_base', 0.0)],
        },
    }
    for (rel, anim), wanted in melee_specs.items():
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        match = re.search(r'^' + re.escape(anim) + r'\s*$', text, re.M)
        frames = []
        if match:
            body, _end = K._brace_block(text, match.end())
            for frame_match in re.finditer(r'^\s*frame\s*$', body, re.M):
                frame_body, _frame_end = K._brace_block(body, frame_match.end())
                frames.append(frame_body)

        actual_hits = []
        actual_sounds = []
        actual_checks = []
        for frame, frame_body in enumerate(frames):
            for hit in re.finditer(r'A_MeleeAttack\(\s*([-+\d.]+)\s*,\s*(\d+)',
                                       frame_body):
                actual_hits.append((frame, float(hit.group(1)), int(hit.group(2))))
            for sound in re.finditer(
                    r'A_PlayLocalSound\("sounds/([^"/]+)\.wav"\)', frame_body):
                actual_sounds.append((frame, sound.group(1)))
            for check in re.finditer(
                    r'A_CheckMelee\("([^"]+)"\s*,\s*([-+\d.]+)', frame_body):
                actual_checks.append((frame, check.group(1), float(check.group(2))))

        if len(frames) != wanted['frames']:
            out.append('%s frame count changed from %d to %d'
                       % (anim, wanted['frames'], len(frames)))
        for label, actual in (('hits', actual_hits), ('sounds', actual_sounds),
                              ('checks', actual_checks)):
            if actual != wanted[label]:
                out.append('%s melee %s differ: expected %s, found %s'
                           % (anim, label, wanted[label], actual))

    # Bastet and Fish now use their complete retail live art rather than the
    # DOS LION/FISH frames with callback numbers overlaid. Lock every authored
    # delay, transition, directional view, teleport callback, fullbright bite,
    # gib inventory, and script-side random-decision contract.
    common_anim_text = {}
    for creature in ('bastet', 'fish'):
        real = names.get('sprites/monsters/%s_anim.txt' % creature)
        common_anim_text[creature] = z.read(real).decode('latin-1').replace('\r', '') \
            if real else ''

    def common_raw_frames(creature, name):
        text = common_anim_text[creature]
        match = re.search(r'^' + re.escape(name) + r'\s*$', text, re.M)
        if not match:
            return []
        body, _end = K._brace_block(text, match.end())
        frames = []
        for frame_match in re.finditer(r'^\s*frame\s*$', body, re.M):
            frame_body, _frame_end = K._brace_block(body, frame_match.end())
            frames.append(frame_body)
        return frames

    exact_common = {
        'bastet': {
            'timing': {
                'bastet_chase': [1] * 12,
                'bastet_attack': [1] * 11,
                'bastet_pain': [1] * 17,
                'bastet_teleport_out': [1] * 37 + [4],
                'bastet_teleport_in': [1] * 23,
                'bastet_idle': [1],
            },
            'directional': {'bastet_chase', 'bastet_attack', 'bastet_pain'},
            'composites': 168,
        },
        'fish': {
            'timing': {
                'fish_chase': [1] * 8,
                'fish_idle': [1],
                'fish_attack': [1] * 14,
            },
            'directional': {'fish_chase', 'fish_idle', 'fish_attack'},
            'composites': 107,
        },
    }
    common_parsed = {}
    for creature, contract in exact_common.items():
        composites = set()
        for name, wanted_delays in contract['timing'].items():
            parsed = K.parse_anim_full(z, names, 'monsters/' + name)
            common_parsed[name] = parsed
            actual_delays = [frame.get('delay', 1) for frame in parsed]
            if actual_delays != wanted_delays:
                out.append('%s delays changed from %s to %s'
                           % (name, wanted_delays, actual_delays))
            if name in contract['directional']:
                if any('rot' not in frame or len(frame['rot']) != 8
                       or any(not view for view in frame['rot'])
                       for frame in parsed):
                    out.append('%s no longer has eight nonempty authored rotations'
                               % name)
            else:
                for frame, data in enumerate(parsed):
                    if not data.get('sprites', []):
                        allowed_empty = (name == 'bastet_teleport_out'
                                         and frame == len(parsed) - 1)
                        if not allowed_empty:
                            out.append('%s frame %d is unexpectedly spriteless'
                                       % (name, frame))
            for data in parsed:
                views = data['rot'] if 'rot' in data else \
                    ([data.get('sprites', [])] if data.get('sprites', []) else [])
                composites.update(tuple(view) for view in views)
        if len(composites) != contract['composites']:
            out.append('%s authored composite census changed from %d to %d'
                       % (creature, contract['composites'], len(composites)))

    for name, frame, wanted in (
            ('bastet_pain', 16, 'monsters/bastet_chase'),
            ('bastet_teleport_out', 37, 'monsters/bastet_chase'),
            ('bastet_teleport_in', 22, 'monsters/bastet_chase')):
        parsed = common_parsed.get(name, [])
        actual = parsed[frame].get('goto') if frame < len(parsed) else None
        if actual != wanted:
            out.append('%s frame %d goto changed from %s to %s'
                       % (name, frame, wanted, actual))

    def common_frame_has(creature, name, frame, needles):
        frames = common_raw_frames(creature, name)
        if frame >= len(frames):
            return
        compact = re.sub(r'\s+', '', frames[frame])
        for needle in needles:
            if re.sub(r'\s+', '', needle) not in compact:
                out.append('%s frame %d lost callback: %s'
                           % (name, frame, needle))

    common_frame_has('bastet', 'bastet_chase', 0,
                     ('A_FaceTarget()', 'A_BastetTryTeleport()'))
    for frame in range(1, 12):
        common_frame_has('bastet', 'bastet_chase', frame,
                         ('A_BastetTryTeleport()',))
    common_frame_has('bastet', 'bastet_attack', 0,
                     ('A_BastetCheckTeleport()',))
    common_frame_has('bastet', 'bastet_pain', 6,
                     ('A_BastetScream()',))
    common_frame_has('bastet', 'bastet_pain', 15,
                     ('A_BastetPainTeleport()',))
    common_frame_has('bastet', 'bastet_teleport_out', 0,
                     ('A_ClearBurnState()',))
    common_frame_has('bastet', 'bastet_teleport_out', 10,
                     ('A_PlayLocalSound("sounds/bastetteleout.wav")',
                      'A_SpawnLight(1024,0.75,0.75,0.75,20,2)'))
    common_frame_has('bastet', 'bastet_teleport_out', 37,
                     ('A_Hide()', 'delay 4'))
    common_frame_has('bastet', 'bastet_teleport_in', 0, ('A_Show()',))
    common_frame_has('bastet', 'bastet_teleport_in', 1,
                     ('A_PlayLocalSound("sounds/bastettelein.wav")',
                      'A_SpawnLight(1024,0.75,0.75,0.75,20,2)'))
    common_frame_has('bastet', 'bastet_teleport_in', 16,
                     ('A_PlayLocalSound("sounds/bastetact01.wav")',))
    common_frame_has('bastet', 'bastet_teleport_in', 22,
                     ('A_BastetTeleportIn()',))

    fish_attack_raw = common_raw_frames('fish', 'fish_attack')
    fish_fullbright = [i for i, frame in enumerate(fish_attack_raw)
                       if re.search(r'^\s*fullbright\s*$', frame, re.M)]
    if fish_fullbright != [4, 5, 10, 11]:
        out.append('fish_attack fullbright frames changed from [4, 5, 10, 11] to %s'
                   % fish_fullbright)
    common_frame_has('fish', 'fish_attack', 0,
                     ('A_ThrustZVelocity(0.0)',))

    for creature, counts in (
            ('bastet', {'Obj_Bastet_Gib01': 1, 'Obj_Bastet_Gib02': 2,
                        'Obj_Bastet_Gib03': 1, 'Obj_Bastet_Gib04': 3}),
            ('fish', {'Obj_Fish_Gibs01': 3, 'Obj_Fish_Gibs02': 2,
                      'Obj_Fish_Gibs03': 2})):
        death = common_raw_frames(creature, creature + '_death')
        compact = re.sub(r'\s+', '', death[0]) if death else ''
        for needle in ('A_GibDeath(0.0)', 'A_Destroy()'):
            if re.sub(r'\s+', '', needle) not in compact:
                out.append('%s_death lost callback: %s' % (creature, needle))
        for actor, wanted in counts.items():
            actual = compact.count('A_TossActor("%s"' % actor)
            if actual != wanted:
                out.append('%s_death tosses %d %s actors, expected %d'
                           % (creature, actual, actor, wanted))
        sounds = ('bastetatk01.wav', 'bastetdeath01.wav') \
            if creature == 'bastet' else ('fishdeath.wav',)
        for wav in sounds:
            needle = 'A_PlayLocalSound("sounds/%s")' % wav
            if needle not in compact:
                out.append('%s_death lost sound callback: %s'
                           % (creature, wav))

    bastet_script_real = names.get('scripts/p_enemy.txt')
    bastet_script = z.read(bastet_script_real).decode('latin-1').replace('\r', '') \
        if bastet_script_real else ''
    script_contract = {
        'A_BastetTryTeleport': (
            'RandomDecision(actor, 14)', 'kexSlaveRandomizer::UInt8() & 63',
            'AF_SOLID|AF_SHOOTABLE', 'bastet_teleport_out'),
        'A_BastetPainTeleport': (
            'kexSlaveRandomizer::UInt8() & 3', 'AF_SOLID|AF_SHOOTABLE',
            'bastet_teleport_out'),
        'A_BastetTryTeleportIn': (
            'RandomDecision(actor, 30)', 'kexSlaveRandomizer::UInt8() & 7',
            'bastet_teleport_in'),
        'A_BastetCheckTeleport': (
            'AF_HIDDEN', 'RandomDecision(actor, 30)',
            'kexSlaveRandomizer::UInt8() & 7', 'bastet_chase'),
        'A_BastetTeleportIn': ('AF_SOLID|AF_SHOOTABLE',
                               'actor.m_fScratchParam1 = 30'),
        'A_BastetScream': ('bastetpain01.wav', 'bastetpain02.wav'),
    }
    for function, needles in script_contract.items():
        match = re.search(r'void\s+' + re.escape(function)
                          + r'\s*\([^)]*\)\s*', bastet_script)
        body = K._brace_block(bastet_script, match.end())[0] if match else ''
        compact = re.sub(r'\s+', '', body)
        if not body:
            out.append('retail Bastet script is missing: %s' % function)
            continue
        for needle in needles:
            if re.sub(r'\s+', '', needle) not in compact:
                out.append('%s lost authored logic: %s' % (function, needle))

    # Anubis needs expanded range/pain/death frames and a dedicated miss group.
    # Audit both animation files because the normal PC death is intentionally
    # separate from the directional live/gib sheet.
    anubis_real = names.get('sprites/monsters/anubis_anim.txt')
    anubis_pc_real = names.get('sprites/monsters/anubis_pc_anim.txt')
    anubis_text = z.read(anubis_real).decode('latin-1').replace('\r', '') \
        if anubis_real else ''
    anubis_pc_text = z.read(anubis_pc_real).decode('latin-1').replace('\r', '') \
        if anubis_pc_real else ''

    anubis_timing = {
        'anubis_idle': [1],
        'anubis_chase': [1] * 18,
        'anubis_melee': [1] * 24,
        'anubis_attack_range': [1] * 25,
        'anubis_pain': [1] * 10,
        'anubis_melee_miss': [1, 8],
        'anubis_death_pc': [2, 2, 3, 3, 2, 2, 5, -1],
        'anubis_death': [1],
    }
    anubis_parsed = {}
    for name, delays in anubis_timing.items():
        parsed = K.parse_anim_full(z, names, 'monsters/' + name)
        anubis_parsed[name] = parsed
        actual = [frame.get('delay', 1) for frame in parsed]
        if actual != delays:
            out.append('%s delays changed from %s to %s'
                       % (name, delays, actual))
        directional = name not in ('anubis_death_pc', 'anubis_death')
        if directional and any('rot' not in frame or len(frame['rot']) != 8
                               or any(not view for view in frame['rot'])
                               for frame in parsed):
            out.append('%s no longer has eight nonempty authored rotations'
                       % name)
        if name == 'anubis_death_pc' \
                and any('sprites' not in frame or not frame['sprites']
                        for frame in parsed):
            out.append('%s is no longer a nonempty single-view animation'
                       % name)

    for name, frame, wanted in (
            ('anubis_attack_range', 24, 'monsters/anubis_chase'),
            ('anubis_pain', 9, 'monsters/anubis_chase'),
            ('anubis_melee_miss', 1, 'monsters/anubis_chase')):
        parsed = anubis_parsed.get(name, [])
        actual = parsed[frame].get('goto') if frame < len(parsed) else None
        if actual != wanted:
            out.append('%s frame %d goto changed from %s to %s'
                       % (name, frame, wanted, actual))

    anubis_composites = set()
    for name in ('anubis_idle', 'anubis_chase', 'anubis_melee',
                 'anubis_attack_range', 'anubis_pain',
                 'anubis_melee_miss', 'anubis_death_pc'):
        for frame in anubis_parsed.get(name, []):
            views = frame['rot'] if 'rot' in frame else [frame.get('sprites', [])]
            anubis_composites.update(tuple(view) for view in views)
    if len(anubis_composites) != 128:
        out.append('Anubis authored composite census changed from 128 to %d'
                   % len(anubis_composites))

    def anubis_raw_frames(name):
        text = anubis_pc_text if name == 'anubis_death_pc' else anubis_text
        match = re.search(r'^' + re.escape(name) + r'\s*$', text, re.M)
        if not match:
            return []
        body, _end = K._brace_block(text, match.end())
        frames = []
        for frame_match in re.finditer(r'^\s*frame\b', body, re.M):
            frame_body, _frame_end = K._brace_block(body, frame_match.end())
            frames.append(frame_body)
        return frames

    anubis_raw = {name: anubis_raw_frames(name) for name in anubis_timing}
    def anubis_frame_has(name, frame, needles):
        frames = anubis_raw[name]
        if frame >= len(frames):
            return
        compact = re.sub(r'\s+', '', frames[frame])
        for needle in needles:
            if re.sub(r'\s+', '', needle) not in compact:
                out.append('%s frame %d lost callback: %s'
                           % (name, frame, needle))

    anubis_frame_has('anubis_attack_range', 0, ('A_FaceTarget()',))
    anubis_frame_has('anubis_attack_range', 3, ('A_FaceTarget()',))
    anubis_frame_has('anubis_attack_range', 8,
                      ('A_FaceTarget()',
                       'A_PlayLocalSound("sounds/anubisproj.wav")',
                       'A_FireProjectile("Proj_Anubis",0,0,4,96,16)'))
    anubis_frame_has('anubis_pain', 4,
                      ('A_PlayLocalSound("sounds/anubispain.wav")',))
    anubis_frame_has('anubis_death_pc', 0, ('A_AnubisDeath()',))
    anubis_frame_has('anubis_death_pc', 1,
                      ('A_ThrustZVelocity(6.0)',))
    anubis_frame_has('anubis_death_pc', 4, ('A_Die()',))
    anubis_frame_has('anubis_death_pc', 7, ('A_RandomItem()',))
    anubis_frame_has('anubis_death', 0,
                      ('A_GibDeath(64.0)', 'A_AnubisDeath()', 'A_Destroy()'))
    anubis_gib = re.sub(r'\s+', '', anubis_raw['anubis_death'][0]) \
        if anubis_raw['anubis_death'] else ''
    for actor, wanted in {
            'Obj_Anubis_Gib01': 1, 'Obj_Anubis_Gib02': 2,
            'Obj_Anubis_Gib03': 2, 'Obj_Anubis_Gib04': 4}.items():
        actual = anubis_gib.count('A_TossActor("%s"' % actor)
        if actual != wanted:
            out.append('anubis_death tosses %d %s actors, expected %d'
                       % (actual, actor, wanted))

    enemy_real = names.get('scripts/p_enemy.txt')
    enemy_script = re.sub(r'\s+', '',
        z.read(enemy_real).decode('latin-1')) if enemy_real else ''
    for needle in ('voidA_AnubisDeath(kexSlaveActor@actor)',
                   'kexSlaveRandomizer::UInt8()&1',
                   'sounds/anubisdeath01.wav',
                   'sounds/anubisdeath02.wav'):
        if needle not in enemy_script:
            out.append('A_AnubisDeath lost exact script behavior: %s' % needle)

    # Mummy uses delayed KEX frames that cannot be represented by the stock
    # MUMMY.SEQ without expansion. Audit the source timing and callbacks that
    # define the exact loose sequence and its native controller transitions.
    mummy_real = names.get('sprites/monsters/mummy_anim.txt')
    mummy_text = z.read(mummy_real).decode('latin-1').replace('\r', '') \
        if mummy_real else ''

    def mummy_raw_frames(name):
        match = re.search(r'^' + re.escape(name) + r'\s*$', mummy_text, re.M)
        if not match:
            return []
        body, _end = K._brace_block(mummy_text, match.end())
        frames = []
        for frame_match in re.finditer(r'^\s*frame\s*$', body, re.M):
            frame_body, _frame_end = K._brace_block(body, frame_match.end())
            frames.append(frame_body)
        return frames

    mummy_timing = {
        'mummy_idle': [1],
        'mummy_chase': [1] * 16,
        'mummy_melee': [1] * 13,
        'mummy_range_attack': [1] * 30 + [8, 1],
        'mummy_pain': [8, 1],
        'mummy_death_pc': [8, 7, 5, 4, 3, 3, 2, 2, -1],
        'mummy_death': [1],
    }
    mummy_parsed = {}
    for name, delays in mummy_timing.items():
        path = 'monsters/' + name
        parsed = K.parse_anim_full(z, names, path)
        mummy_parsed[name] = parsed
        actual = [frame.get('delay', 1) for frame in parsed]
        if actual != delays:
            out.append('%s delays changed from %s to %s'
                       % (name, delays, actual))
        directional = name not in ('mummy_death_pc', 'mummy_death')
        if directional and any('rot' not in frame or len(frame['rot']) != 8
                               or any(not view for view in frame['rot'])
                               for frame in parsed):
            out.append('%s no longer has eight nonempty authored rotations'
                       % name)
        if name == 'mummy_death_pc' \
                and any('sprites' not in frame or not frame['sprites']
                        for frame in parsed):
            out.append('%s is no longer a nonempty single-view animation'
                       % name)
        if name == 'mummy_death' \
                and any('sprites' not in frame for frame in parsed):
            out.append('mummy_death is no longer a single-view callback frame')

    for name, frame, wanted in (
            ('mummy_range_attack', 31, 'monsters/mummy_chase'),
            ('mummy_pain', 1, 'monsters/mummy_chase')):
        parsed = mummy_parsed.get(name, [])
        actual = parsed[frame].get('goto') if frame < len(parsed) else None
        if actual != wanted:
            out.append('%s frame %d goto changed from %s to %s'
                       % (name, frame, wanted, actual))

    composites = set()
    for name in ('mummy_idle', 'mummy_chase', 'mummy_melee',
                 'mummy_range_attack', 'mummy_pain', 'mummy_death_pc'):
        for frame in mummy_parsed.get(name, []):
            views = frame['rot'] if 'rot' in frame else [frame.get('sprites', [])]
            composites.update(tuple(view) for view in views)
    if len(composites) != 108:
        out.append('Mummy authored composite census changed from 108 to %d'
                   % len(composites))

    raw = {name: mummy_raw_frames(name) for name in mummy_timing}
    def mummy_frame_has(name, frame, needles):
        frames = raw[name]
        if frame >= len(frames):
            return
        compact = re.sub(r'\s+', '', frames[frame])
        for needle in needles:
            if re.sub(r'\s+', '', needle) not in compact:
                out.append('%s frame %d lost callback: %s'
                           % (name, frame, needle))

    mummy_frame_has('mummy_range_attack', 0,
                    ('A_PlayLocalSound("sounds/mumatk01.wav")',))
    mummy_frame_has('mummy_range_attack', 6,
                    ('A_PlayLocalSound("sounds/mumact02.wav")',
                     'A_FireProjectile("Proj_Cobra_Red",0,0,2,96,12)'))
    mummy_frame_has('mummy_death_pc', 0,
                    ('A_MummyDeath()', 'A_MummyDeathEffect()'))
    mummy_frame_has('mummy_death_pc', 4, ('A_Die()',))
    mummy_frame_has('mummy_death_pc', 7, ('A_RandomItem()',))
    mummy_frame_has('mummy_death', 0,
                    ('A_GibDeath(0.0)', 'A_MummyDeath()', 'A_Destroy()'))
    gib_frame = re.sub(r'\s+', '', raw['mummy_death'][0]) \
        if raw['mummy_death'] else ''
    gib_counts = {
        'Obj_Mummy_Gib01': 1, 'Obj_Mummy_Gib02': 1,
        'Obj_Mummy_Gib03': 2, 'Obj_Mummy_Gib04': 2,
    }
    for actor, wanted in gib_counts.items():
        actual = gib_frame.count('A_TossActor("%s"' % actor)
        if actual != wanted:
            out.append('mummy_death tosses %d %s actors, expected %d'
                       % (actual, actor, wanted))
    if 'CheckMummyRevive' in mummy_text:
        out.append('retail Mummy animation unexpectedly contains resurrection logic')

    # The two skins now have dedicated loose sequences rather than squeezing
    # retail callbacks into the demo SPIDER.SEQ's seven-frame attack.  Audit
    # both the source motion groups and the complete generated SEQ/DEF contract.
    motion_specs = {
        'monsters/spider_base': 6, 'monsters/spider_attack': 14,
        'monsters/spider_leap': 1, 'monsters/spider_check_ground': 1,
        'monsters/spider_falling_check_ground': 1,
        'monsters/spider_death': 3,
        'monsters/scorp_base': 12, 'monsters/scorp_attack': 13,
        'monsters/scorp_leap': 1, 'monsters/scorp_check_ground': 1,
        'monsters/scorp_death': 3,
    }
    for anim, wanted_frames in motion_specs.items():
        frames = K.parse_anim_full(z, names, anim)
        if len(frames) != wanted_frames:
            out.append('%s exact motion count changed from %d to %d'
                       % (anim, wanted_frames, len(frames)))
        elif anim.endswith('_death'):
            if ([frame.get('delay', 1) for frame in frames] != [1, 2, 1]
                    or any('sprites' not in frame or not frame['sprites']
                           for frame in frames)):
                out.append('%s no longer supplies its 1/2/1 single-view death'
                           % anim)
        elif any('rot' not in frame or len(frame['rot']) != 8
                 or any(not layers for layers in frame['rot']) for frame in frames):
            out.append('%s no longer supplies eight nonempty authored rotations' % anim)

    for creature, gib_counts in (
            ('spider', {'Obj_Spider_Gib%02d' % i: 1 for i in range(1, 10)}),
            ('scorp', {'Obj_Scorp_Gib01': 1, 'Obj_Scorp_Gib02': 3,
                       'Obj_Scorp_Gib03': 3, 'Obj_Scorp_Gib04': 2})):
        real = names.get('sprites/monsters/%s_anim.txt' % creature)
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        match = re.search(r'^' + creature + r'_death\s*$', text, re.M)
        death_frames = []
        if match:
            body, _end = K._brace_block(text, match.end())
            for frame_match in re.finditer(r'^\s*frame\b', body, re.M):
                frame_body, _frame_end = K._brace_block(body, frame_match.end())
                death_frames.append(re.sub(r'\s+', '', frame_body))
        if len(death_frames) != 3:
            out.append('%s_death callback frame count changed from 3 to %d'
                       % (creature, len(death_frames)))
            continue
        if 'A_FaceTarget()' not in death_frames[0]:
            out.append('%s_death frame 0 lost A_FaceTarget' % creature)
        final = death_frames[2]
        for needle in ('A_GibDeath(0.0)',
                       'A_PlayLocalSound("sounds/spiddeath.wav")',
                       'A_Destroy()'):
            if needle not in final:
                out.append('%s_death frame 2 lost callback: %s'
                           % (creature, needle))
        for actor, wanted in gib_counts.items():
            actual = final.count('A_TossActor("%s"' % actor)
            if actual != wanted:
                out.append('%s_death tosses %d %s actors, expected %d'
                           % (creature, actual, actor, wanted))

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
        tilefiles = {int(tile): path for tile, path in re.findall(
            r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}
    except OSError:
        tilefiles = {}
        out.append('exhumed.def is missing for exact Spider/Scorpion art audit')

    # LION/FISH retain stock-only compatibility and effect slots, so audit the
    # generated live groups separately from the copied frames and require the
    # complete private authored tile runs.
    try:
        lion_seq = S.read_seq(open(os.path.join(outdir, 'LION.SEQ'), 'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        lion_seq = None
        out.append('LION.SEQ is missing or malformed')
    if lion_seq is not None:
        lion_sizes = ([12] * 8 + [41, 23] + [11] * 8 + [15] * 8
                      + [1] * 18 + [17] * 8 + [80, 15, 1, 1, 1])
        if lion_seq['seq_size'] != lion_sizes:
            out.append('LION.SEQ does not retain exact Bastet '
                       '12/11/17/41/23 live cadence')
        if (len(lion_seq['seq_base']) != 57
                or len(lion_seq['fr_base']) != 620
                or len(lion_seq['ck_pict']) != 623):
            out.append('LION.SEQ has %d slots/%d frames/%d chunks, '
                       'expected 57/620/623'
                       % (len(lion_seq['seq_base']), len(lion_seq['fr_base']),
                          len(lion_seq['ck_pict'])))
        generated_slots = list(range(18)) + list(range(44, 52)) + [54]
        generated_frames = []
        for slot in generated_slots:
            if slot < len(lion_seq['seq_base']):
                first = lion_seq['seq_base'][slot]
                generated_frames.extend(range(first, first + lion_seq['seq_size'][slot]))
        if (len(generated_frames) != 385
                or any(lion_seq['fr_size'][frame] != 1
                       or lion_seq['fr_flag'][frame] != 0
                       for frame in generated_frames)):
            out.append('LION.SEQ exact Bastet groups are no longer 385 '
                       'one-chunk callback-free ticks')
        if any(lion_seq['ck_flag'][lion_seq['fr_base'][frame]] != 0
               for frame in generated_frames):
            out.append('LION.SEQ exact Bastet groups unexpectedly retain chunk flips')
        if lion_seq['snd_events'] != [(525, 2), (590, 0), (607, 4)]:
            out.append('LION.SEQ copied compatibility sounds changed: %s'
                       % lion_seq['snd_events'])

        wanted_tiles = set(range(21500, 21668))
        actual_tiles = set(lion_seq['ck_pict']) & set(range(21500, 21700))
        if actual_tiles != wanted_tiles:
            out.append('LION.SEQ exact Bastet tile coverage differs: %d missing, %d extra'
                       % (len(wanted_tiles - actual_tiles),
                          len(actual_tiles - wanted_tiles)))
        missing_defs = [tile for tile in wanted_tiles
                        if tilefiles.get(tile) != 'kexart/seq/t%04d.png' % tile]
        missing_pngs = [tile for tile in wanted_tiles
                        if not os.path.isfile(os.path.join(
                            outdir, 'kexart', 'seq', 't%04d.png' % tile))]
        if missing_defs:
            out.append('LION.SEQ has %d missing/misdirected exact DEF tile rows'
                       % len(missing_defs))
        if missing_pngs:
            out.append('LION.SEQ has %d missing exact composite PNGs'
                       % len(missing_pngs))

    try:
        fish_seq = S.read_seq(open(os.path.join(outdir, 'FISH.SEQ'), 'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        fish_seq = None
        out.append('FISH.SEQ is missing or malformed')
    if fish_seq is not None:
        fish_sizes = ([8] * 8 + [1] * 8 + [8] * 8 + [14] * 8
                      + [4, 8, 8, 2, 27, 8, 8, 47, 6, 6, 6, 0, 15, 14, 1, 0, 1])
        if fish_seq['seq_size'] != fish_sizes:
            out.append('FISH.SEQ does not retain exact 8/1/14 live cadence '
                       'and terminal exact marker')
        if (len(fish_seq['seq_base']) != 49
                or len(fish_seq['fr_base']) != 409
                or len(fish_seq['ck_pict']) != 415):
            out.append('FISH.SEQ has %d slots/%d frames/%d chunks, '
                       'expected 49/409/415'
                       % (len(fish_seq['seq_base']), len(fish_seq['fr_base']),
                          len(fish_seq['ck_pict'])))
        generated_slots = list(range(16)) + list(range(24, 32)) + [48]
        generated_frames = []
        expected_bright = set()
        for slot in generated_slots:
            if slot >= len(fish_seq['seq_base']):
                continue
            first = fish_seq['seq_base'][slot]
            generated_frames.extend(range(first, first + fish_seq['seq_size'][slot]))
            if 24 <= slot < 32:
                expected_bright.update(first + frame for frame in (4, 5, 10, 11))
        if (len(generated_frames) != 185
                or any(fish_seq['fr_size'][frame] != 1
                       for frame in generated_frames)):
            out.append('FISH.SEQ exact groups are no longer 185 one-chunk ticks')
        actual_bright = {frame for frame in generated_frames
                         if fish_seq['fr_flag'][frame] & 4}
        if actual_bright != expected_bright:
            out.append('FISH.SEQ fullbright attack ticks differ: %d missing, %d extra'
                       % (len(expected_bright - actual_bright),
                          len(actual_bright - expected_bright)))
        if any(fish_seq['fr_flag'][frame] & ~4 for frame in generated_frames):
            out.append('FISH.SEQ exact groups unexpectedly carry native callbacks')
        if any(fish_seq['ck_flag'][fish_seq['fr_base'][frame]] != 0
               for frame in generated_frames):
            out.append('FISH.SEQ exact groups unexpectedly retain chunk flips')
        if fish_seq['snd_events'] != [(248, 1537)]:
            out.append('FISH.SEQ copied effect sounds changed: %s'
                       % fish_seq['snd_events'])

        wanted_tiles = set(range(21700, 21807))
        actual_tiles = set(fish_seq['ck_pict']) & set(range(21700, 21900))
        if actual_tiles != wanted_tiles:
            out.append('FISH.SEQ exact tile coverage differs: %d missing, %d extra'
                       % (len(wanted_tiles - actual_tiles),
                          len(actual_tiles - wanted_tiles)))
        missing_defs = [tile for tile in wanted_tiles
                        if tilefiles.get(tile) != 'kexart/seq/t%04d.png' % tile]
        missing_pngs = [tile for tile in wanted_tiles
                        if not os.path.isfile(os.path.join(
                            outdir, 'kexart', 'seq', 't%04d.png' % tile))]
        if missing_defs:
            out.append('FISH.SEQ has %d missing/misdirected exact DEF tile rows'
                       % len(missing_defs))
        if missing_pngs:
            out.append('FISH.SEQ has %d missing exact composite PNGs'
                       % len(missing_pngs))

    sequence_specs = (
        ('SPIDER.SEQ', 6, 14, 3, 20700, 45, 291),
        ('SCORPION.SEQ', 12, 13, 2, 20900, 48, 419),
    )
    for filename, base_frames, attack_frames, leap_frames, tilebase, tilecount, total_frames \
            in sequence_specs:
        try:
            seq = S.read_seq(open(os.path.join(outdir, filename), 'rb').read())
        except (OSError, AssertionError, ValueError, struct.error):
            out.append('%s is missing or malformed' % filename)
            continue

        expected_sizes = ([base_frames] * 24 + [leap_frames] * 8
                          + [attack_frames] * 8 + [4] + [1] * 7)
        if seq['seq_size'] != expected_sizes:
            out.append('%s does not retain exact %d/%d/%d/4 '
                       'base/attack/leap/death cadence'
                       % (filename, base_frames, attack_frames, leap_frames))
        if len(seq['seq_base']) != 48 or len(seq['fr_base']) != total_frames:
            out.append('%s has %d slots/%d frames, expected 48/%d'
                       % (filename, len(seq['seq_base']), len(seq['fr_base']),
                          total_frames))
        if (seq['fr_base'] != list(range(total_frames))
                or seq['fr_size'] != [1] * total_frames
                or len(seq['ck_pict']) != total_frames):
            out.append('%s is no longer one precomposited chunk per authored frame'
                       % filename)
        if any(seq['fr_flag']) or any(seq['ck_flag']):
            out.append('%s unexpectedly carries native callbacks or a second image flip'
                       % filename)
        if seq['snd_names'] or seq['snd_events']:
            out.append('%s duplicates callbacks owned by the KEX Spider controller'
                       % filename)

        wanted_tiles = set(range(tilebase, tilebase + tilecount))
        actual_tiles = set(seq['ck_pict'])
        if actual_tiles != wanted_tiles:
            out.append('%s exact tile coverage differs: %d missing, %d extra'
                       % (filename, len(wanted_tiles - actual_tiles),
                          len(actual_tiles - wanted_tiles)))
        missing_defs = [tile for tile in wanted_tiles
                        if tilefiles.get(tile) != 'kexart/seq/t%04d.png' % tile]
        missing_pngs = [tile for tile in wanted_tiles
                        if not os.path.isfile(os.path.join(
                            outdir, 'kexart', 'seq', 't%04d.png' % tile))]
        if missing_defs:
            out.append('%s has %d missing/misdirected exact DEF tile rows'
                       % (filename, len(missing_defs)))
        if missing_pngs:
            out.append('%s has %d missing exact composite PNGs'
                       % (filename, len(missing_pngs)))

    try:
        anubis_seq = S.read_seq(open(os.path.join(outdir, 'ANUBIS.SEQ'),
                                     'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        anubis_seq = None
        out.append('ANUBIS.SEQ is missing or malformed')
    if anubis_seq is not None:
        anubis_sizes = ([1] * 8 + [18] * 8 + [24] * 8 + [25] * 8
                        + [10] * 8 + [20, 1, 20, 1]
                        + [4, 30, 30, 30, 30, 30] + [9] * 8)
        if anubis_seq['seq_size'] != anubis_sizes:
            out.append('ANUBIS.SEQ does not retain exact '
                       '1/18/24/25/10/20/1/9 cadence')
        if (len(anubis_seq['seq_base']) != 58
                or len(anubis_seq['fr_base']) != 892
                or len(anubis_seq['ck_pict']) != 1042):
            out.append('ANUBIS.SEQ has %d slots/%d frames/%d chunks, '
                       'expected 58/892/1042'
                       % (len(anubis_seq['seq_base']),
                          len(anubis_seq['fr_base']),
                          len(anubis_seq['ck_pict'])))

        exact_frames = []
        for slot in list(range(44)) + list(range(50, 58)):
            if slot < len(anubis_seq['seq_base']):
                first = anubis_seq['seq_base'][slot]
                exact_frames.extend(range(first,
                    first + anubis_seq['seq_size'][slot]))
        if (len(exact_frames) != 738
                or any(anubis_seq['fr_size'][frame] != 1
                       for frame in exact_frames)):
            out.append('ANUBIS.SEQ exact actions are no longer 738 '
                       'one-chunk authored ticks')
        drummer_chunks = 0
        for slot in range(44, min(50, len(anubis_seq['seq_base']))):
            first = anubis_seq['seq_base'][slot]
            drummer_chunks += sum(anubis_seq['fr_size'][
                first:first + anubis_seq['seq_size'][slot]])
        if drummer_chunks != 304:
            out.append('ANUBIS.SEQ retained %d demo drummer chunks, expected 304'
                       % drummer_chunks)
        if any(anubis_seq['fr_flag']) or any(anubis_seq['ck_flag']):
            out.append('ANUBIS.SEQ unexpectedly carries native callback flags '
                       'or a second image flip')
        wanted_events = [(666, 2), (700, 9731), (730, 4), (738, 5),
                         (810, 8196), (813, 8196), (816, 8196)]
        if anubis_seq['snd_events'] != wanted_events:
            out.append('ANUBIS.SEQ drummer sound events changed from %s to %s'
                       % (wanted_events, anubis_seq['snd_events']))

        wanted_tiles = set(range(21300, 21428))
        actual_tiles = set(anubis_seq['ck_pict']) & set(range(21300, 21500))
        if actual_tiles != wanted_tiles:
            out.append('ANUBIS.SEQ exact tile coverage differs: %d missing, %d extra'
                       % (len(wanted_tiles - actual_tiles),
                          len(actual_tiles - wanted_tiles)))
        missing_defs = [tile for tile in wanted_tiles
                        if tilefiles.get(tile) != 'kexart/seq/t%04d.png' % tile]
        missing_pngs = [tile for tile in wanted_tiles
                        if not os.path.isfile(os.path.join(
                            outdir, 'kexart', 'seq', 't%04d.png' % tile))]
        if missing_defs:
            out.append('ANUBIS.SEQ has %d missing/misdirected exact DEF tile rows'
                       % len(missing_defs))
        if missing_pngs:
            out.append('ANUBIS.SEQ has %d missing exact composite PNGs'
                       % len(missing_pngs))

    try:
        mummy_seq = S.read_seq(open(os.path.join(outdir, 'MUMMY.SEQ'),
                                    'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        mummy_seq = None
        out.append('MUMMY.SEQ is missing or malformed')
    if mummy_seq is not None:
        mummy_sizes = ([16] * 8 + [1] * 8 + [13] * 8 + [39] * 8
                       + [35] + [1] * 17 + [9] * 8 + [1] * 4)
        if mummy_seq['seq_size'] != mummy_sizes:
            out.append('MUMMY.SEQ does not retain exact 16/1/13/39/35/1/9 cadence')
        if (len(mummy_seq['seq_base']) != 62
                or len(mummy_seq['fr_base']) != 680):
            out.append('MUMMY.SEQ has %d slots/%d frames, expected 62/680'
                       % (len(mummy_seq['seq_base']),
                          len(mummy_seq['fr_base'])))
        if (mummy_seq['fr_base'] != list(range(680))
                or mummy_seq['fr_size'] != [1] * 680
                or len(mummy_seq['ck_pict']) != 680):
            out.append('MUMMY.SEQ is no longer one precomposited chunk per tick')
        if any(mummy_seq['fr_flag']) or any(mummy_seq['ck_flag']):
            out.append('MUMMY.SEQ unexpectedly carries native callbacks or flips')
        if mummy_seq['snd_names'] or mummy_seq['snd_events']:
            out.append('MUMMY.SEQ duplicates callbacks owned by the Mummy controller')

        wanted_tiles = set(range(21100, 21208))
        actual_tiles = set(mummy_seq['ck_pict'])
        if actual_tiles != wanted_tiles:
            out.append('MUMMY.SEQ exact tile coverage differs: %d missing, %d extra'
                       % (len(wanted_tiles - actual_tiles),
                          len(actual_tiles - wanted_tiles)))
        missing_defs = [tile for tile in wanted_tiles
                        if tilefiles.get(tile) != 'kexart/seq/t%04d.png' % tile]
        missing_pngs = [tile for tile in wanted_tiles
                        if not os.path.isfile(os.path.join(
                            outdir, 'kexart', 'seq', 't%04d.png' % tile))]
        if missing_defs:
            out.append('MUMMY.SEQ has %d missing/misdirected exact DEF tile rows'
                       % len(missing_defs))
        if missing_pngs:
            out.append('MUMMY.SEQ has %d missing exact composite PNGs'
                       % len(missing_pngs))

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    source_specs = {
        'lion.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?72:60;',
            'sprite[nSprite].xrepeat=g_kexMode?60:40;',
            'sprite[nSprite].yrepeat=g_kexMode?60:40;',
            'staticboolKexBastetExactSequence()',
            'SeqSize[nBase+8]==41', 'SeqSize[nBase+9]==23',
            'SeqSize[nBase+10]==11', 'SeqSize[nBase+44]==17',
            '(RandomLong()&nMask)==(totalmoves&nMask)',
            'intconstnWorldSpeed=24*6;',
            'sprite[nSprite].cstat&=~0x101;',
            'nRenderedFrame==37', 'sprite[nSprite].cstat|=0x8000;',
            'nRenderedFrame==22', 'sprite[nSprite].cstat&=~0x8000;',
            'nRenderedFrame==15', 'KexBastetTryTeleportIn(nLion,nSprite)',
            'nRenderedFrame==6', '((uint8_t)RandomByte()&1)?"cat_pain2":"cat_pain1"',
            'KexBastetStartLight(nLion,nSprite)',
            'KexBastetBeginGibTimer(nLion,nSprite,nKiller)',
            'LionList[nLion].nAction=11',
            'KexBastetOwnsDeathDrop(intnSprite)',
            'KexAIEnterMeleeRange(nSprite,nTarget,16)',
            'KexMeleeRange(nSprite,nTarget,48)',
            'KexMeleeAttackRange(nSprite,nTarget,48)',
            'nRenderedFrame==1||nRenderedFrame==7',
            'runlist_DamageEnemy(nTarget,nSprite,20);'),
        'fish.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?24:80;',
            'staticboolKexFishExactSequence()',
            'SeqSize[nBase+48]==1',
            'KexSound_Get("fish_act")',
            'sprite[nTarget].statnum==100',
            'KexFishBeginGibTimer(nFish,nSprite,nKiller)',
            'FishList[nFish].nAction=9',
            'KexFishOwnsDeathDrop(intnSprite)',
            'nRenderedFrame==12',
            'KexAIEnterMeleeRange(nSprite,nTarget,16)',
            'KexMeleeRange(nSprite,nTarget,16)',
            'KexMeleeAttackRange(nSprite,nTarget,48)',
            'runlist_DamageEnemy(nTarget,nSprite,20);',
            'runlist_DamageEnemy(nTarget,nSprite,2);'),
        'spider.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?24:15;',
            'KexSpiderExactSequence(shortnSpider)',
            'SeqSize[nBase+8]==(scorpion?12:6)',
            'SeqSize[nBase+24]==(scorpion?2:3)',
            'SeqSize[nBase+32]==(scorpion?13:14)',
            'SeqSize[nBase+40]==4', 'SeqSize[nBase+41]==1',
            'KexSpiderLeapVelocityFromDelta(', 'zvel=-kKexSpiderLeapZ;',
            'staticvoidKexSpiderSourcePhysicsStep(shortnSprite)',
            'sprite[nSprite].zvel+=kKexSpiderGravity;',
            'sprite[nSprite].xvel/=2;', 'KexSpiderGroundCallback(nSprite)',
            'KexSpiderLeapTick(nSpider,nSprite,nTarget)',
            'KexSound_Get("spi_jump")', 'VB_KEX_SPIDER_LEAP_TEST',
            'KexContactRadius2(nSprite)', 'KexContactRadius2(nTarget)',
            'sprite[nTarget].x=691;', 'sprite[nTarget].x=692;',
            'changespritestat(nTarget,99);',
            'exactSequence?11:5', 'exactSequence?12:6',
            'exactSequence?6:3',
            's_spiderScorp[nSpider]?16:20',
            'KexAIEnterMeleeRange(nSprite,nTarget,s_spiderScorp[nSpider]?0:20)',
            'KexMeleeAttackRange(nSprite,nTarget,hitReach)',
            'runlist_DamageEnemy(nTarget,nSprite,10);',
            'runlist_DamageEnemy(nTarget,nSprite,3);',
            'SpiderList[nSpider].nFrame>=SeqSize[nSeq]',
            'nRenderedFrame==3', 'KexSpiderBeginGibTimer(nSpider,nSprite)',
            'SpiderList[nSpider].nAction=6',
            'KexSpiderOwnsDeathDrop(intnSprite)',
            'sprite[nSprite].extra=s_spiderScorp[nSpider]?1:0;',
            'sprite[nSprite].extra==1',
            'nPic>=kKexExactScorpTileBase',
            'KexSpiderIdentityTest()'),
        'anubis.cpp': (
            'nRenderedFrame==6||nRenderedFrame==19',
            'KexAIEnterMeleeRange(nSprite,nTarget,0)',
            'KexMeleeRange(nSprite,nTarget,0)',
            'KexMeleeAttackRange(nSprite,nTarget,48)',
            'KexSound_Get("k_anslap")',
            'runlist_DamageEnemy(nTarget,nSprite,20);',
            'staticboolKexAnubisExactSequence()',
            'KexAnubisActionSeq', 'SeqSize[nBase+24]==25',
            'SeqSize[nBase+32]==10', 'SeqSize[nBase+40]==20',
            'SeqSize[nBase+50]==9',
            'nRemainingHealth<KexHealthUnits(-2-(100>>4))',
            'nDamageType==kKexDamageExplosion',
            'nDamageType==kKexDamageFire',
            'KexSound_Get(RandomBit()?"k_andie2":"anu_died")',
            'KexSound_Get("anu_fire")', 'KexSound_Get("anu_hit")',
            'sprite[nSprite].zvel=-(6*16*6);',
            'nRenderedFrame==10', 'nRenderedFrame==19',
            'AnubisList[nAnubis].nAction=15',
            'KexAnubisOwnsDeathDrop(intnSprite)'),
        'mummy.cpp': (
            'nRenderedFrame==11', 'nRenderedFrame==12',
            'KexAIEnterMeleeRange(nSprite,nTarget,48)',
            'KexMeleeRange(nSprite,nTarget,48)',
            'KexMeleeAttackRange(nSprite,nTarget,48)',
            'KexSound_Get("m_nstaff")',
            'runlist_DamageEnemy(nTarget,nSprite,20);',
            'staticboolKexMummyExactSequence()',
            'SeqSize[nBase+24]==39', 'SeqSize[nBase+32]==35',
            'nRemainingHealth<KexHealthUnits(-2-(130>>4))',
            'nDamageType==kKexDamageExplosion',
            'nDamageType==kKexDamageFire',
            'KexSound_Get("m_watha")', 'KexSound_Get("m_chant0")',
            'nRenderedFrame==24', 'nRenderedFrame==32',
            'MummyList[nMummy].nAction=8',
            'KexMummyOwnsDeathDrop(intnSprite)',
            'if(!g_kexMode)CheckMummyRevive(nMummy);',
            'sprite[nSprite].cstat&=~0x100;'),
        'runlist.cpp': (
            'intnKexDamageType=kKexDamageGeneric;',
            'if(!KexAnubisOwnsDeathDrop(nSprite)'
            '&&!KexMummyOwnsDeathDrop(nSprite)'
            '&&!KexBastetOwnsDeathDrop(nSprite)'
            '&&!KexFishOwnsDeathDrop(nSprite)'
            '&&!KexSpiderOwnsDeathDrop(nSprite)'
            '&&!KexGibDeath_OwnsDrop(nKexDropStat))'),
        'bullet.cpp': (
            'pBullet->nType==10||pBullet->nType==14',
            'nKexDamageType=kKexDamageFire;',
            'nKexDamageType=previousDamageType;'),
        'anims.cpp': (
            'nKexDamageType=kKexDamageFire;',
            'runlist_DamageEnemy(nIgnitedSprite,-1,',
            'nKexDamageType=previousDamageType;'),
        'move.cpp': (
            'intKexContactRadius2(shortnSprite)',
            'intKexActorHeightBuild(shortnSprite)',
            'intKexTossCreatureChunk(intnVal,intnPic)',
            'kKexTossChunkMarker=-2',
            'sprite[nSprite].z-=32*kKexTossZUnit;',
            'KexTossRange(-8*kKexTossXYVelocityUnit,',
            'KexTossRange(-16*kKexTossZUnit,',
            'KexTossRandomInt()', 'KexTossRangeForRoll(',
            'KexTossPreserveClassicRandomCursor()',
            'KexTossSourcePhysicsStep(nSprite,bouncySound,sourceStep);',
            'KexTossBounceSoundName(kKexTossBounceSoundGlass,0)',
            'KexTossPlayBounceSound(nSprite,(KexTossBounceSound)bouncySound,sourceStep);',
            'KexTossBouncyCreatureChunk(intnVal,intnPic,KexTossBounceSoundsound)',
            'KexTossGibExpireTick(nSprite)',
            'for(intsourceStep=0;sourceStep<2;sourceStep++)',
            'if(bouncyToss&&KexTossAtRest(nSprite))',
            'intKexTossClipdist(intnPic,boolbouncy)',
            'intKexTossActorHeight(intnPic,boolbouncy)',
            'sprite[nSprite].z-KexTossActorHeight(sprite[nSprite].picnum,bouncy)+sprite[nSprite].zvel<=ceiling',
            'nPic>=7820&&nPic<=7822',
            'Kextosstest:',
            'voidKexQuake(intnTicks)',
            's_kexQuakeTicks=max(0,nTicks)',
            'Kexquaketest:',
            'KexQuake(60);', 'KexQuake(8);',
            'KexQuake_Tick();',
            'kKexPlayerContactRadius2=1143',
            'sprite[nSprite].statnum==100',
            'intKexMeleeRange(shortnSprite,shortnTarget,intnReach)',
            'KexContactRadius2(nSprite)', 'KexContactRadius2(nTarget)',
            '(int64_t)nReach*12',
            'dx*dx+dy*dy+dz*dz<=radius*radius',
            'boolKexAIWithinSightDistance(intxDistance,intyDistance)',
            'kKexAISightDistance=3000*6',
            'g_kexMode?KexAIWithinSightDistance(dx,dy)',
            'sprite[nSprite].z-KexActorHeightBuild(nSprite)/2',
            'sprite[nPlayerSprite].z-KexActorHeightBuild(nPlayerSprite)/2',
            'height=KexDrop_ActorHeight(nSprite)',
            'sprite[nSprite].clipdist==48',
            'sprite[nSprite].clipdist==54',
            'sprite[nSprite].hitag!=1',
            'AngleDiff(sprite[nSprite].ang,getangle(dx,dy))<=338',
            'boolKexAIEnterMeleeRange(shortnSprite,shortnTarget,intnReach)',
            'AngleDiff(sprite[nSprite].ang,getangle(dx,dy))>338',
            'targetZ-=28*16*6',
            'boolKexMeleeAttackRange(shortnSprite,shortnTarget,intnReach)',
            'targetZ-=KexActorHeightBuild(nTarget)/2',
            'Kexcommonenemyacquisitiontest:',
            'FindPlayer(nProbe,0)==nPlayer',
            'sprite[nProbe].ang=(sprite[nProbe].ang+339)&kAngleMask;',
            'sprite[nPlayer]=savedPlayer;'),
        'init.cpp': (
            'VB_KEX_COMMON_ENEMY_TEST',
            '{101,72,60,"Anubis"}', '{102,72,60,"Mummy"}',
            '{104,72,60,"Bastet"}', '{103,24,40,"Fish"}',
            '{99,24,40,"Spider/Scorpion"}',
            'Kexcommonenemydimensionstest:',
            'KexBastet/Fishsequencelifecycletest:',
            'SeqSize[bastetSeq+8]==41',
            'SeqSize[bastetSeq+9]==23',
            'SeqSize[fishSeq+48]==1',
            'KexSpider/Scorpionsequencecadencetest:',
            'SeqSize[spiderSeq+32+direction]==14',
            'SeqSize[scorpSeq+32+direction]==13',
            'SeqSize[spiderSeq+40]==4',
            'SeqSize[scorpSeq+40]==4',
            'KexSpiderIdentityTest()',
            'KexAnubissequence/deathselectortest:',
            'SeqSize[anubisSeq+24+direction]==25',
            'SeqSize[anubisSeq+40]==20',
            'SeqSize[anubisSeq+50+direction]==9',
            'KexAnubisGibSelectorTest()',
            'KexMummysequence/deathselectortest:',
            'SeqSize[mummySeq+24+direction]==39',
            'SeqSize[mummySeq+32]==35',
            'KexMummyGibSelectorTest()',
            'Kexcommonenemymeleerangetest:',
            'KexMeleeRange(nSource,nTarget,48)',
            'KexAIEnterMeleeRange(nSource,nTarget,0)',
            'KexMeleeAttackRange(nSource,nTarget,16)',
            'sprite[nTarget].x=1147;', 'changespritestat(nTarget,100);',
            'Kexcommonenemysightrangetest:',
            'KexAIWithinSightDistance(17999,0)',
            '!KexAIWithinSightDistance(18000,0)'),
    }
    for filename, needles in source_specs.items():
        try:
            compact = re.sub(r'\s+', '', open(os.path.join(srcdir, filename)).read())
        except OSError:
            out.append('cannot read common-enemy implementation: %s' % filename)
            continue
        for needle in needles:
            if needle not in compact:
                out.append('%s no longer implements %s' % (filename, needle))

    try:
        converter = re.sub(r'\s+', '', open(os.path.join(
            os.path.dirname(__file__), 'kexmap.py')).read())
        for needle in (
                'KEX_EXACT_SPIDER_TILE_BASE=20700',
                'KEX_EXACT_SCORP_TILE_BASE=20900',
                'KEX_EXACT_MUMMY_TILE_BASE=21100',
                'KEX_EXACT_ANUBIS_TILE_BASE=21300',
                'KEX_EXACT_BASTET_TILE_BASE=21500',
                'KEX_EXACT_FISH_TILE_BASE=21700',
                'defbuild_bastet_fish_sequences(Z,demo_dir,outdir,verbose=True):',
                'defbuild_spider_sequences(Z,outdir,verbose=True):',
                'defbuild_anubis_sequence(Z,demo_dir,outdir,verbose=True):',
                'defbuild_mummy_sequence(Z,outdir,verbose=True):',
                "('spider',KEX_EXACT_SPIDER_TILE_BASE,(6,14,1,1,1,3),45,'SPIDER.SEQ',",
                "('scorp',KEX_EXACT_SCORP_TILE_BASE,(12,13,1,1,3),48,'SCORPION.SEQ',",
                'frame_map=[(kind,frame)forframein(0,1,1,2)]',
                "'death':[8,7,5,4,3,3,2,2,-1]",
                "'death':[2,2,3,3,2,2,5,-1]",
                'len(seq[\'fr_base\'])!=892',
                'len(seq[\'fr_base\'])!=680',
                'expected_frames=620ifspec[\'creature\']==\'bastet\'else409',
                'KEX_SEQ_EXTRA_TILES.update(build_bastet_fish_sequences(z,demo_dir,outdir,verbose))',
                'KEX_SEQ_EXTRA_TILES.update(build_spider_sequences(z,outdir,verbose))',
                'KEX_SEQ_EXTRA_TILES.update(build_anubis_sequence(z,demo_dir,outdir,verbose))',
                'KEX_SEQ_EXTRA_TILES.update(build_mummy_sequence(z,outdir,verbose))'):
            if needle not in converter:
                out.append('kexmap.py no longer implements %s' % needle)
    except OSError:
        out.append('cannot read exact common-enemy converter implementation')

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        sound_specs = {
            'k_anmele': 'anubismelee', 'k_anslap': 'anubisslap',
            'k_andie2': 'anubisdeath02',
            'm_nstaff': 'mumatk02', 'm_watha': 'mumatk01',
            'm_chant0': 'mumact02',
            'm_deth_kex1': 'mumdeath01',
            'm_deth_kex2': 'mumdeath02',
            'm_deth_kex3': 'mumdeath03',
            'cat_icu': 'bastetact02',
            'cat_warp': 'bastetact01',
            'cat_xpld': 'bastetatk01',
            'cat_pain1': 'bastetpain01', 'cat_pain2': 'bastetpain02',
            'cat_deth': 'bastetdeath01',
            'tele_2': 'bastetteleout', 'tele_3': 'bastettelein',
            'fish_act': 'fishact', 'fishbit2': 'fishatk',
            'fishdies': 'fishdeath',
            'spi_atak': 'spidchomp',
        }
        for alias, wav in sound_specs.items():
            if not re.search(r'^' + alias + r'\s+sounds/' + wav + r'\.wav\s+0$',
                             sounds, re.M):
                out.append('common-enemy melee sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing')
    return out


def verify_set(z, outdir):
    """Exact AI_Set definition, script, callback and generated sequence contract."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    actor_real = names.get('defs/actors/monsters/set.txt')
    actor_text = z.read(actor_real).decode('latin-1').replace('\r', '') \
        if actor_real else ''
    actor_match = re.search(r'Actor\s+AI_Set\s*\{(.*?)\n\}', actor_text, re.S)
    expected = {'health': 3000, 'radius': 96, 'height': 192,
                'stepHeight': 48, 'scale': 2.0, 'moveSpeed': 10,
                'painChance': 24, 'noEnterWater': 1, 'noInFighting': 1}
    if not actor_match:
        out.append('retail actor definition is missing: AI_Set')
    else:
        body = actor_match.group(1)
        for field, wanted in expected.items():
            value = re.search(r'^\s*' + re.escape(field) + r'\s+([-+\d.]+)',
                              body, re.M)
            if not value or abs(float(value.group(1)) - wanted) > 1e-6:
                out.append('AI_Set %s changed from authored %s' % (field, wanted))

    script_real = names.get('scripts/p_enemy.txt')
    script = z.read(script_real).decode('latin-1').replace('\r', '') \
        if script_real else ''
    script_checks = (
        (r'RandomDecision\(ai,\s*30\)', 'Set decision cadence'),
        (r'GetHealth\(\)\s*<\s*1500.*?UInt8\(\)\s*&\s*1',
         'Set soul threshold/coin flip'),
        (r'GetHealth\(\)\s*<\s*2000.*?UInt8\(\)\s*&\s*3',
         'Set leap threshold/quarter roll'),
        (r'FireRemoteEventFromTag\(1010\)', 'Set death remote event'),
        (r'InflictDamage\(ai\.CastToActor\(\),\s*ADT_GENERIC,\s*40\)',
         'Set landing damage'))
    for pattern, label in script_checks:
        if not re.search(pattern, script, re.S):
            out.append('%s changed in retail p_enemy script' % label)

    anim_real = names.get('sprites/monsters/set_anim.txt')
    anim_text = z.read(anim_real).decode('latin-1').replace('\r', '') \
        if anim_real else ''
    counts = {'set_idle': 1, 'set_chase': 20, 'set_melee': 18,
              'set_leap_start': 13, 'set_leap_loop': 1,
              'set_leap_land': 12, 'set_fire_souls': 15,
              'set_pain': 8, 'set_death': 30, 'set_death_loop': 1}
    bodies = {}
    for anim, wanted in counts.items():
        match = re.search(r'^' + re.escape(anim) + r'\s*$', anim_text, re.M)
        if not match:
            out.append('retail animation is missing: %s' % anim)
            continue
        body, _end = K._brace_block(anim_text, match.end())
        bodies[anim] = body
        actual = len(re.findall(r'^\s*frame\s*$', body, re.M))
        if actual != wanted:
            out.append('%s changed from %d authored frames to %d'
                       % (anim, wanted, actual))

    def callback_frames(anim, pattern):
        body = bodies.get(anim, '')
        return [len(re.findall(r'^\s*frame\s*$', body[:m.start()], re.M)) - 1
                for m in re.finditer(pattern, body)]

    callback_checks = (
        ('set_chase', r'\baction\s+A_SetThink\(\)', list(range(20))),
        ('set_chase', r'sounds/setstomp02\.wav', [5, 15]),
        ('set_melee', r'A_MeleeAttack\(48,\s*40\)', [8, 17]),
        ('set_melee', r'sounds/setstomp01\.wav', [0]),
        ('set_melee', r'sounds/setpain\.wav', [9]),
        ('set_leap_start', r'A_SetLeap\(\)', [12]),
        ('set_leap_start', r'sounds/setjump\.wav', [9]),
        ('set_leap_loop', r'A_SetCheckGround\(\)', [0]),
        ('set_leap_land', r'A_Quake\(8\)', [6]),
        ('set_fire_souls', r'A_FireProjectile\("Proj_Soul",\s*0,\s*0,\s*50,\s*192,\s*16\)', [7]),
        ('set_fire_souls', r'sounds/setatk02\.wav', [6]),
        ('set_fire_souls', r'sounds/setatk03\.wav', [7]),
        ('set_pain', r'sounds/setpain\.wav', [3]),
        ('set_pain', r'sounds/setact01\.wav', [7]),
        ('set_death', r'A_SetDeath\(\)', [29]),
        ('set_death', r'sounds/setdeath\.wav', [28]),
    )
    for anim, pattern, wanted in callback_checks:
        actual = callback_frames(anim, pattern)
        if actual != wanted:
            out.append('%s callback %s moved from %s to %s'
                       % (anim, pattern, wanted, actual))

    path = os.path.join(outdir, 'SET.SEQ')
    try:
        seq = S.read_seq(open(path, 'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        return out + ['SET.SEQ is missing or malformed']
    if len(seq['seq_size']) != 76:
        out.append('SET.SEQ has %d exact action slots, expected 76'
                   % len(seq['seq_size']))
    else:
        groups = ((0, 8, 1), (9, 8, 20), (18, 8, 18), (27, 8, 13),
                  (36, 8, 1), (45, 8, 12), (54, 8, 15), (63, 8, 8))
        for base, dirs, frames in groups:
            if seq['seq_size'][base:base + dirs] != [frames] * dirs:
                out.append('SET.SEQ slot %d does not retain %d authored frames'
                           % (base, frames))
        if [seq['seq_size'][i] for i in (72, 73, 74, 75)] != [30, 1, 1, 1]:
            out.append('SET.SEQ death/death-loop/soul/terminal cadence differs')

    wanted_tiles = set(range(20000, 20171))
    actual_private = {tile for tile in seq['ck_pict'] if tile >= 20000}
    if actual_private != wanted_tiles or set(seq['ck_pict']) - wanted_tiles != {2678}:
        out.append('SET.SEQ exact tile coverage differs: %d private missing, extras %s'
                   % (len(wanted_tiles - actual_private),
                      sorted(set(seq['ck_pict']) - wanted_tiles - {2678})))
    wanted_names = [b'K_SETSTP', b'SET_STEP', b'SET_HIT', b'SET_JUMP',
                    b'SET_LAND', b'SET_GR2', b'GHOST109', b'SET_SPRT',
                    b'RA_SPRIT', b'SET_DETH', b'SET_GR1']
    if [n.rstrip(b'\0') for n in seq['snd_names']] != wanted_names:
        out.append('SET.SEQ sound table differs from retail callbacks')
    if len(seq['seq_base']) > 72:
        event_specs = ((9, 5, 0), (9, 15, 0), (18, 0, 1), (18, 9, 2),
                       (27, 9, 3), (45, 6, 4), (54, 6, 5), (54, 7, 6),
                       (63, 3, 2), (63, 7, 10), (72, 0, 2), (72, 2, 7),
                       (72, 7, 7), (72, 10, 2), (72, 12, 7), (72, 17, 7),
                       (72, 18, 8), (72, 28, 9))
        wanted_events = [(seq['seq_base'][slot] + frame, sound)
                         for slot, frame, sound in event_specs]
        if seq['snd_events'] != wanted_events:
            out.append('SET.SEQ sound events differ from authored callback frames')
        wanted_melee = sorted(seq['seq_base'][18 + d] + f
                              for d in range(8) for f in (8, 17))
        actual_melee = sorted(i for i, flag in enumerate(seq['fr_flag'])
                              if flag & 0x80 and i < seq['seq_base'][54])
        if actual_melee != wanted_melee:
            out.append('SET.SEQ melee flags differ from frames 8/17')
        wanted_fire = [seq['seq_base'][54 + d] + 7 for d in range(8)]
        actual_fire = [i for i, flag in enumerate(seq['fr_flag'])
                       if flag & 0x80 and i >= seq['seq_base'][54]]
        if actual_fire != wanted_fire:
            out.append('SET.SEQ soul callbacks differ from frame 7')
        wanted_bright = [seq['seq_base'][54 + d] + 9 for d in range(8)]
        actual_bright = [i for i, flag in enumerate(seq['fr_flag']) if flag & 4]
        if actual_bright != wanted_bright:
            out.append('SET.SEQ fullbright frames differ from soul-cast frame 9')
    if any(seq['ck_flag']):
        out.append('SET.SEQ precomposited eight-view tiles are being flipped twice')

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    tilefiles = {int(tile): rel for tile, rel in re.findall(
        r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}
    for tile in wanted_tiles:
        rel = tilefiles.get(tile)
        try:
            with open(os.path.join(outdir, rel), 'rb') as image:
                head = image.read(24)
            if head[:8] != b'\x89PNG\r\n\x1a\n' or len(head) < 24:
                raise ValueError
        except (OSError, TypeError, ValueError):
            out.append('SET.SEQ tile %d has no valid PNG' % tile)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        aliases = (('k_setstp', 'setstomp02'), ('set_step', 'setstomp01'),
                   ('set_hit', 'setpain'), ('set_jump', 'setjump'),
                   ('set_land', 'setland'), ('set_gr2', 'setatk02'),
                   ('ghost109', 'setatk03'), ('set_sprt', 'setquake'),
                   ('ra_sprit', 'setatk01'), ('set_deth', 'setdeath'),
                   ('set_gr1', 'setact01'), ('set_icu', 'setact02'))
        for alias, wav in aliases:
            if not re.search(r'^' + alias + r'\s+sounds/' + wav + r'\.wav\s+0$',
                             sounds, re.M):
                out.append('Set sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing')
    return out


def verify_queen(z, outdir):
    """Exact Queen body, seed pod, linked snake tail and death lifecycle."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    actor_real = names.get('defs/actors/monsters/queen.txt')
    actor_text = z.read(actor_real).decode('latin-1').replace('\r', '') \
        if actor_real else ''
    actor_specs = {
        'AI_Queen': {'radius': 64, 'height': 256, 'stepHeight': 48,
                     'scale': 1.5, 'health': 3600, 'moveSpeed': 0,
                     'painChance': 0, 'lookAllAround': 1,
                     'noEnterWater': 1, 'noDropOff': 1,
                     'noInFighting': 1, 'friction': 1, 'turnSpeed': 16},
        'AI_Queen_Snake': {'radius': 32, 'height': 48, 'stepHeight': 48,
                           'scale': 2.0, 'health': 200, 'moveSpeed': 16,
                           'solid': 1, 'shootable': 0, 'painChance': 0,
                           'noDropOff': 1, 'noInFighting': 1,
                           'retreatAfterMelee': 1, 'turnSpeed': 4},
        'Obj_Queen_Snake_Tail': {'radius': 64, 'height': 48,
                                 'stepHeight': 48, 'scale': 2.0,
                                 'health': 200, 'solid': 0,
                                 'shootable': 0, 'moveable': 0},
        'Obj_Seedling_Spawn': {'radius': 36, 'height': 32,
                               'stepHeight': 256, 'scale': 1.0,
                               'moveable': 1, 'noDropOff': 1},
    }
    for actor, expected in actor_specs.items():
        match = re.search(r'Actor\s+' + re.escape(actor)
                          + r'\s*\{(.*?)\n\}', actor_text, re.S)
        if not match:
            out.append('retail actor definition is missing: %s' % actor)
            continue
        body = match.group(1)
        for field, wanted in expected.items():
            value = re.search(r'^\s*' + re.escape(field)
                              + r'\s+([-+\d.]+)', body, re.M)
            actual = float(value.group(1)) if value else None
            if actual is None or abs(actual - wanted) > 1e-6:
                out.append('%s %s changed from authored %s to %s'
                           % (actor, field, wanted, actual))
    for index in range(1, 7):
        actor = 'Obj_QueenGib%02d' % index
        match = re.search(r'Actor\s+' + actor + r'\s*\{(.*?)\n\}',
                          actor_text, re.S)
        if not match:
            out.append('retail Queen gib definition is missing: %s' % actor)
            continue
        compact = re.sub(r'\s+', '', match.group(1))
        for needle in ('radius28', 'height56', 'scale1.5'):
            if needle not in compact:
                out.append('%s lost authored %s' % (actor, needle))

    script_real = names.get('scripts/p_enemy.txt')
    script = z.read(script_real).decode('latin-1').replace('\r', '') \
        if script_real else ''

    def function_body(name):
        match = re.search(r'\bvoid\s+' + re.escape(name) + r'\s*\(', script)
        return K._brace_block(script, match.end())[0] if match else ''

    script_specs = {
        'A_QueenThink': (
            'actor.GetHealth()<=2400',
            'actor.ChangeAnim("monsters/queen_pain")',
            'turnAngles>0.025f', 'turnAngles<-0.025f',
            '!(RandomDecision(actor,14))',
            '(kexSlaveRandomizer::UInt8()&7)==0',
            'actor.ChangeAnim("monsters/queen_tail_missile")',
            'RandomDecision(actor,62)',
            'kexSlaveRandomizer::UInt8()<=16',
            'kexSlaveRandomizer::RFloat()*Math::Deg2Rad(90.0f)',
            'strafeAngles=-strafeAngles', '*4.0f'),
        'A_QueenThinkStumpy': (
            'RandomDecision(actor,14)',
            'kexSlaveRandomizer::UInt8()<=16',
            'actor.GetHealth()<=1200',
            '(kexSlaveRandomizer::UInt8()&1)!=0',
            'maxQueenSeeds<8',
            'actor.ChangeAnim("monsters/queen_hurl")',
            '(kexSlaveRandomizer::UInt8()&7)==0',
            'actor.ChangeAnim("monsters/queen_chase_charge")'),
        'A_QueenCheckState': (
            'actor.GetHealth()>2400',
            'actor.ChangeAnim("monsters/queen_base")'),
        'A_QueenCheckRefire': (
            '(kexSlaveRandomizer::UInt8()&3)==0',
            'actor.ChangeAnim("monsters/queen_chase")'),
        'A_QueenFireProjectile': (
            'actor.FireActor("Proj_Seedling",0,0,128)',
            'proj.SetSourceOwner(actor)',
            'actor.GetTarget().GetHeight()*0.5f', '*32.0f',
            'proj.SetTarget(actor)'),
        'A_QueenAfterPain': (
            'actor.GetFlags()&=~AF_DISABLEFRICTION',
            'actor.GetMoveSpeed()=8.0f'),
        'A_QueenChargeThink': (
            'dist>=(192*192)', 'Math::Deg2Rad(11.25f)',
            'Math::Deg2Rad(5.625f)', '*16.0f',
            'target.InflictDamage(actor,ADT_GENERIC,40)',
            'actor.ChangeAnim("monsters/queen_after_charge")'),
        'A_QueenHurlSeedling': (
            'actor.FireActor("Obj_Seedling_Spawn",0,0,128)',
            'seed.GetRotation().z=actor.GetRotation().z', '*32.0f',
            'maxQueenSeeds++'),
        'A_QueenSnakeSetup': ('intcount=10', 'AddSnakeTail(tail,count)'),
        'A_UpdateQueenTailJoints': (
            'actor.GetOrigin().Lerp(actor.GetTarget().GetOrigin(),0.2f)',
            'actor.LinkSector(actor.GetTarget().GetSector())'),
        'A_QueenSnakeTailDeath': (
            'actor.GetTarget().GetFlags()|=AF_SHOOTABLE',),
        'A_QueenSnakeHop': (
            '(actor.GetGameTicks()&512)==0',
            'Math::Deg2Rad(22.5f)', 'Math::Deg2Rad(2.8125f)',
            '(kexSlaveRandomizer::UInt8()&31)!=0',
            '16.0f+Math::RandFloat()*16.0f', '<=512.0f'),
        'A_QueenSnakeExplode': (
            'kexSlaveRandomizer::RFloat()*128.0f',
            'actor.FireActor("Fx_Explosion01",x,y,z+192.0f)'),
        'A_QueenDeath': ('sounds/queenroar.wav',
                         'FireRemoteEventFromTag(1010)'),
    }
    for function, needles in script_specs.items():
        body = function_body(function)
        compact = re.sub(r'\s+', '', body)
        if not body:
            out.append('retail Queen script is missing: %s' % function)
            continue
        for needle in needles:
            if needle not in compact:
                out.append('%s lost authored logic: %s' % (function, needle))

    anim_paths = [n for key, n in names.items()
                  if key.startswith('sprites/monsters/queen')
                  and key.endswith('.txt')]
    seed_real = names.get('sprites/monsters/seedling_anim.txt')
    if seed_real:
        anim_paths.append(seed_real)
    anim_text = '\n'.join(z.read(n).decode('latin-1').replace('\r', '')
                          for n in anim_paths)
    counts = {
        'queen_idle': 1, 'queen_base': 10, 'queen_chase': 10,
        'queen_chase_charge': 10, 'queen_after_charge': 10,
        'queen_flap': 11, 'queen_hurl': 30,
        'queen_tail_missile': 16, 'queen_pain': 13,
        'queen_death': 62, 'queen_snake_tail': 1,
        'queen_snake_tail_explode': 1, 'queen_snake_chase': 5,
        'queen_snake_idle': 1, 'queen_snake_melee': 1,
        'queen_snake_explode': 22, 'seedling_spawn': 2,
    }
    bodies = {}
    for anim, wanted in counts.items():
        match = re.search(r'^' + re.escape(anim) + r'\s*$', anim_text, re.M)
        if not match:
            out.append('retail animation is missing: %s' % anim)
            continue
        body, _end = K._brace_block(anim_text, match.end())
        bodies[anim] = body
        actual = len(re.findall(r'^\s*frame\s*$', body, re.M))
        if actual != wanted:
            out.append('%s changed from %d authored frames to %d'
                       % (anim, wanted, actual))

    def callback_frames(anim, pattern):
        body = bodies.get(anim, '')
        return [len(re.findall(r'^\s*frame\s*$', body[:m.start()], re.M)) - 1
                for m in re.finditer(pattern, body)]

    callback_checks = (
        ('queen_base', r'\baction\s+A_QueenThink\(\)', list(range(10))),
        ('queen_chase', r'\baction\s+A_QueenThinkStumpy\(\)', list(range(10))),
        ('queen_chase', r'\baction\s+A_QueenCheckState\(\)', [0]),
        ('queen_chase_charge', r'\baction\s+A_QueenChargeThink\(\)', list(range(10))),
        ('queen_after_charge', r'\bgoto\s+"monsters/queen_chase"', [9]),
        ('queen_flap', r'A_MeleeAttack\(48,\s*50\)', [1]),
        ('queen_flap', r'A_CheckMelee\("monsters/queen_chase",\s*0\)', [10]),
        ('queen_hurl', r'A_QueenHurlSeedling\(\)', [16]),
        ('queen_hurl', r'A_QueenCheckRefire\(\)', [29]),
        ('queen_tail_missile', r'\baction\s+A_QueenThink\(\)', list(range(15))),
        ('queen_tail_missile', r'A_QueenFireProjectile\(\)', [7]),
        ('queen_tail_missile', r'A_QueenCheckRefire\(\)', [15]),
        ('queen_pain', r'A_QueenAfterPain\(\)', [0]),
        ('queen_pain', r'\bgoto\s+"monsters/queen_chase"', [12]),
        ('queen_death', r'A_Spawn\("AI_Queen_Snake"', [61]),
        ('queen_snake_tail', r'A_UpdateQueenTailJoints\(\)', [0]),
        ('queen_snake_tail_explode', r'A_QueenSnakeTailDeath\(\)', [0]),
        ('queen_snake_chase', r'A_QueenSnakeHop\(\)', list(range(5))),
        ('queen_snake_idle', r'A_QueenSnakeSetup\(\)', [0]),
        ('queen_snake_melee', r'A_MeleeAttack\(48,\s*30\)', [0]),
        ('queen_snake_explode', r'A_QueenSnakeExplode\(\)', list(range(21))),
        ('queen_snake_explode', r'A_QueenDeath\(\)', [0]),
        ('queen_snake_explode', r'A_Destroy\(\)', [21]),
        ('seedling_spawn', r'A_Spawn\("AI_Seedling"', [1]),
        ('seedling_spawn', r'\bdelay\s+60\b', [0]),
    )
    for anim, pattern, wanted in callback_checks:
        actual = callback_frames(anim, pattern)
        if actual != wanted:
            out.append('%s callback %s moved from %s to %s'
                       % (anim, pattern, wanted, actual))

    try:
        seq = S.read_seq(open(os.path.join(outdir, 'QUEEN.SEQ'), 'rb').read())
        egg = S.read_seq(open(os.path.join(outdir, 'QUEENEGG.SEQ'), 'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        return out + ['QUEEN.SEQ or QUEENEGG.SEQ is missing or malformed']
    if len(seq['seq_size']) != 104:
        out.append('QUEEN.SEQ has %d exact action slots, expected 104'
                   % len(seq['seq_size']))
    else:
        groups = ((0, 8, 1), (9, 8, 10), (18, 8, 10), (27, 8, 10),
                  (36, 8, 10), (45, 8, 11), (54, 8, 30), (63, 8, 16),
                  (74, 8, 1), (83, 8, 5), (92, 8, 1))
        for base, dirs, frames in groups:
            if seq['seq_size'][base:base + dirs] != [frames] * dirs:
                out.append('QUEEN.SEQ slot %d no longer has %d frames'
                           % (base, frames))
        if [seq['seq_size'][i] for i in (72, 73, 101, 102, 103)] \
                != [13, 62, 85, 1, 1]:
            out.append('QUEEN.SEQ pain/death/snake/tail cadence differs')
    if egg['seq_size'] != [2, 1]:
        out.append('QUEENEGG.SEQ no longer has 2-frame spawn + terminal slots')

    wanted_tiles = set(range(20400, 20680))
    actual_tiles = set(seq['ck_pict']) | set(egg['ck_pict'])
    if actual_tiles != wanted_tiles:
        out.append('Queen exact tile coverage differs: %d missing, %d extra'
                   % (len(wanted_tiles - actual_tiles),
                      len(actual_tiles - wanted_tiles)))
    if any(seq['ck_flag']) or any(egg['ck_flag']):
        out.append('Queen precomposited authored views are being flipped twice')
    wanted_names = [b'Q_TAIL', b'Q_SPIT', b'REX-BITE', b'Q_GURGLE',
                    b'Q_SIGHT', b'Q_DETH', b'Q_CRY']
    if [n.rstrip(b'\0') for n in seq['snd_names']] != wanted_names:
        out.append('QUEEN.SEQ sound table differs from retail callbacks')
    if len(seq['seq_base']) == 104:
        event_specs = []
        for direction in range(8):
            event_specs.extend(((45 + direction, 1, 2),
                                (54 + direction, 12, 1),
                                (63 + direction, 7, 0)))
        event_specs.extend(((72, 0, 3), (72, 1, 4),
                            (73, 4, 5), (101, 0, 6)))
        wanted_events = [(seq['seq_base'][slot] + frame, sound)
                         for slot, frame, sound in event_specs]
        if seq['snd_events'] != wanted_events:
            out.append('QUEEN.SEQ sound events differ from authored frames')

        wanted_80 = set()
        for direction in range(8):
            wanted_80.add(seq['seq_base'][45 + direction] + 1)
            wanted_80.add(seq['seq_base'][54 + direction] + 16)
            wanted_80.add(seq['seq_base'][63 + direction] + 7)
            wanted_80.add(seq['seq_base'][92 + direction])
        wanted_80.update(seq['seq_base'][101] + i * 4 for i in range(21))
        actual_80 = {i for i, flag in enumerate(seq['fr_flag']) if flag & 0x80}
        if actual_80 != wanted_80:
            out.append('QUEEN.SEQ callback flags differ from authored frames')
        wanted_4 = set()
        for direction in range(8):
            wanted_4.add(seq['seq_base'][54 + direction] + 12)
            wanted_4.add(seq['seq_base'][63 + direction] + 7)
            wanted_4.add(seq['seq_base'][63 + direction] + 8)
        actual_4 = {i for i, flag in enumerate(seq['fr_flag']) if flag & 4}
        if actual_4 != wanted_4:
            out.append('QUEEN.SEQ fullbright flags differ from authored frames')

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    tilefiles = {int(tile): rel for tile, rel in re.findall(
        r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}
    for tile in wanted_tiles:
        rel = tilefiles.get(tile)
        try:
            with open(os.path.join(outdir, rel), 'rb') as image:
                head = image.read(24)
            if head[:8] != b'\x89PNG\r\n\x1a\n' or len(head) < 24:
                raise ValueError
        except (OSError, TypeError, ValueError):
            out.append('Queen exact tile %d has no valid PNG' % tile)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        aliases = (('q_tail', 'queenatk04'), ('q_spit', 'queenatk01'),
                   ('rex-bite', 'queenatk02'), ('q_gurgle', 'queenact02'),
                   ('q_sight', 'queenact01'), ('q_deth', 'queendeath01'),
                   ('q_cry', 'queenroar'), ('q_stumpy', 'queenatk03'),
                   ('seed_atk', 'queenatk05'))
        for alias, wav in aliases:
            if not re.search(r'^' + re.escape(alias) + r'\s+sounds/'
                             + wav + r'\.wav\s+0$', sounds, re.M):
                out.append('Queen sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'queen.cpp')).read())
        wasp = re.sub(r'\s+', '', open(os.path.join(srcdir, 'wasp.cpp')).read())
        kexbreak = re.sub(r'\s+', '', open(os.path.join(srcdir, 'kexbreak.cpp')).read())
    except OSError:
        return out + ['cannot read native Queen controller']
    native_needles = (
        'KexQueenSequenceExact()', 'KexQueenActionSeq', 'KexQueenSnakeSeq',
        'SeqSize[queen+73]==62', 'SeqSize[queen+101]==85',
        'KexHealthUnits(3600)', 'bExactQueen?96:100',
        'bExactQueen?60:80', 'KexQueenSeedCount()<8',
        'BuildKexAIFireProjectile(nSprite,12,nTarget,-64,128,32,0)',
        'runlist_DamageEnemy(nTarget,nSprite,50)',
        'runlist_DamageEnemy(nTarget,nSprite,40)',
        'sprite[nSprite2].z=sprite[nSprite].z-128*16*6',
        'QueenEgg[nEgg].field_C=60', 'KexQueenHatchSeedPod',
        'MoveQA[nHead-1]-=nDamage', 'MoveQA[i]=KexHealthUnits(200)',
        'sprite[nTail].x+=(sprite[nParent].x-sprite[nTail].x)/5',
        'sprite[tailspr[QueenHead.field_E-1]].cstat|=0x100',
        'runlist_DamageEnemy(nTarget,nSprite,30)',
        'nFrame<=80&&(nFrame&3)==0', 'nFrame>=84',
        'Read(tailspr,sizeof(short)*kClassicTails)',
        'Write(tailspr,sizeof(short)*kClassicTails)',
        'kKexTailSaveMarker', 'KexQueenNormalizeLoadedState(exactSave)',
        'KexQueenSanitizeBody(nQueen)', 'KexQueenSanitizeSnake()',
        'save7+3/migrate%d',
        'KexQueen_SaveTestPrepare(void)', 'KexQueen_SaveTestValidate(void)',
        's_queenTestSnakeExplosions==21', 'VB_KEX_QUEEN_TEST',
        'boolconstreuseAuthored=QueenCount==0',
        'reuseAuthored?"reused":"spawned"',
        'if(QueenCount<=0)return-1;',
        'shortconstfreeBeforeOverflow=QueenCount',
        '&&QueenCount==freeBeforeOverflow')
    for needle in native_needles:
        if needle not in native:
            out.append('native Queen controller no longer implements %s' % needle)
    if 'intKexSeedlingCount()' not in wasp:
        out.append('native Queen seed cap no longer counts live seedlings')
    for needle in ('VB_KEX_QUEEN_SAVE_TEST', 'KexQueen_SaveTestPrepare()',
                   'KexQueen_SaveTestValidate()'):
        if needle not in kexbreak:
            out.append('Queen end-to-end save probe no longer invokes %s' % needle)
    return out


def verify_selkis(z, outdir):
    """Exact AI_Selkis actor, script, sequence, callbacks and native controller."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    actor_real = names.get('defs/actors/monsters/selkis.txt')
    actor_text = z.read(actor_real).decode('latin-1').replace('\r', '') \
        if actor_real else ''
    actor_match = re.search(r'Actor\s+AI_Selkis\s*\{(.*?)\n\}', actor_text, re.S)
    expected = {'health': 4000, 'radius': 80, 'height': 160,
                'stepHeight': 48, 'scale': 2.5, 'moveSpeed': 12,
                'painChance': 16, 'noEnterWater': 1, 'noInFighting': 1}
    if not actor_match:
        out.append('retail actor definition is missing: AI_Selkis')
    else:
        body = actor_match.group(1)
        for field, wanted in expected.items():
            value = re.search(r'^\s*' + re.escape(field) + r'\s+([-+\d.]+)',
                              body, re.M)
            if not value or abs(float(value.group(1)) - wanted) > 1e-6:
                out.append('AI_Selkis %s changed from authored %s'
                           % (field, wanted))

    script_real = names.get('scripts/p_enemy.txt')
    script = z.read(script_real).decode('latin-1').replace('\r', '') \
        if script_real else ''

    def function_body(name):
        match = re.search(r'\bvoid\s+' + re.escape(name) + r'\s*\(', script)
        if not match:
            return ''
        body, _end = K._brace_block(script, match.end())
        return body

    think = re.sub(r'\s+', '', function_body('A_SelkisThink'))
    countdown = re.sub(r'\s+', '', function_body('A_SelkisCountDown'))
    think_needles = (
        'threshold=(96-(ai.GetHealth()/83))/4',
        'RandomDecision(ai,30)',
        '(kexSlaveRandomizer::UInt8()&7)==4',
        '(kexSlaveRandomizer::UInt8()&7)!=3',
        'ai.GetHealth()<1000',
        'ai.ChangeAnim("monsters/selkis_malfunction2")',
        'ai.GetHealth()<2000',
        'ai.ChangeAnim("monsters/selkis_malfunction1")',
        'ai.GetHealth()<3000',
        'ai.ChangeAnim("monsters/selkis_wounded")')
    countdown_needles = (
        'actor.GetHealth()=24',
        'actor.GetPainChance()=0',
        'actor.GetFlags()&=~(AF_SOLID|AF_SHOOTABLE)',
        '--actor.GetHealth()<=0',
        'actor.PlaySound("sounds/selkisdeath.wav")',
        'actor.ChangeAnim("monsters/selkis_corpse")',
        'FireRemoteEventFromTag(1010)',
        '(actor.GetHealth()&1)==1',
        'SpawnActor("AI_Scorp"',
        '(kexSlaveRandomizer::UInt8()&1)==0',
        'actor.FireActor("Fx_Explosion01",x,y,z+192.0f)')
    if not think:
        out.append('retail Selkis script is missing: A_SelkisThink')
    else:
        for needle in think_needles:
            if needle not in think:
                out.append('A_SelkisThink lost authored logic: %s' % needle)
    if not countdown:
        out.append('retail Selkis script is missing: A_SelkisCountDown')
    else:
        for needle in countdown_needles:
            if needle not in countdown:
                out.append('A_SelkisCountDown lost authored logic: %s' % needle)

    anim_real = names.get('sprites/monsters/selkis_anim.txt')
    anim_text = z.read(anim_real).decode('latin-1').replace('\r', '') \
        if anim_real else ''
    counts = {'selkis_idle': 1, 'selkis_chase': 8, 'selkis_shoot': 6,
              'selkis_melee': 9, 'selkis_pain': 25,
              'selkis_wounded': 40, 'selkis_malfunction1': 12,
              'selkis_malfunction2': 20, 'selkis_death': 1,
              'selkis_explode': 3, 'selkis_corpse': 1}
    bodies = {}
    for anim, wanted in counts.items():
        match = re.search(r'^' + re.escape(anim) + r'\s*$', anim_text, re.M)
        if not match:
            out.append('retail animation is missing: %s' % anim)
            continue
        body, _end = K._brace_block(anim_text, match.end())
        bodies[anim] = body
        actual = len(re.findall(r'^\s*frame\s*$', body, re.M))
        if actual != wanted:
            out.append('%s changed from %d authored frames to %d'
                       % (anim, wanted, actual))

    def callback_frames(anim, pattern):
        body = bodies.get(anim, '')
        return [len(re.findall(r'^\s*frame\s*$', body[:m.start()], re.M)) - 1
                for m in re.finditer(pattern, body)]

    wounded_faces = [0, 3, 5, 8, 10, 14, 18, 20, 22, 23, 25, 27,
                     29, 31, 33, 35, 38]
    callback_checks = (
        ('selkis_chase', r'\baction\s+A_SelkisThink\(\)', list(range(8))),
        ('selkis_shoot', r'A_FireProjectile\("Proj_Cobra_Red_Selkis",\s*0,\s*0,\s*2,\s*144,\s*64\)', [3]),
        ('selkis_shoot', r'sounds/sentryfire\.wav', [3]),
        ('selkis_shoot', r'\bgoto\s+"monsters/selkis_chase"', [5]),
        ('selkis_melee', r'A_MeleeAttack\(48,\s*20\)', [0, 6]),
        ('selkis_melee', r'sounds/selkisatk01\.wav', [0, 5, 8]),
        ('selkis_melee', r'sounds/selkisatk02\.wav', [2]),
        ('selkis_melee', r'A_CheckMelee\("monsters/selkis_chase",\s*0\)', [8]),
        ('selkis_pain', r'sounds/selkisact\.wav', [0]),
        ('selkis_pain', r'\baction\s+A_FaceTarget\(\)', list(range(24))),
        ('selkis_pain', r'\bgoto\s+"monsters/selkis_chase"', [24]),
        ('selkis_wounded', r'\baction\s+A_FaceTarget\(\)', wounded_faces),
        ('selkis_wounded', r'sounds/selkisact\.wav', [15]),
        ('selkis_wounded', r'\bgoto\s+"monsters/selkis_chase"', [39]),
        ('selkis_malfunction1', r'\baction\s+A_FaceTarget\(\)', [0, 1, 4, 6, 9]),
        ('selkis_malfunction1', r'sounds/selkishurt\.wav', [7]),
        ('selkis_malfunction1', r'sounds/selkisfizz01\.wav', [8]),
        ('selkis_malfunction1', r'\bgoto\s+"monsters/selkis_chase"', [11]),
        ('selkis_malfunction2', r'\baction\s+A_FaceTarget\(\)', list(range(19))),
        ('selkis_malfunction2', r'sounds/selkisfizz02\.wav', [16]),
        ('selkis_malfunction2', r'\bgoto\s+"monsters/selkis_chase"', [19]),
        ('selkis_death', r'A_SelkisCountdown\(1\)', [0]),
        ('selkis_death', r'\bdelay\s+0\b', [0]),
        ('selkis_death', r'\bgoto\s+"monsters/selkis_explode"', [0]),
        ('selkis_explode', r'A_SelkisCountdown\(0\)', [1]),
        ('selkis_explode', r'sounds/selkisfizz02\.wav', [2]))
    for anim, pattern, wanted in callback_checks:
        actual = callback_frames(anim, pattern)
        if actual != wanted:
            out.append('%s callback %s moved from %s to %s'
                       % (anim, pattern, wanted, actual))
    if callback_frames('selkis_shoot', r'\bfullbright\b') != [3]:
        out.append('selkis_shoot fullbright moved from authored frame 3')

    path = os.path.join(outdir, 'SCORP.SEQ')
    try:
        seq = S.read_seq(open(path, 'rb').read())
    except (OSError, AssertionError, ValueError, struct.error):
        return out + ['SCORP.SEQ is missing or malformed']
    if len(seq['seq_size']) != 51:
        out.append('SCORP.SEQ has %d exact action slots, expected 51'
                   % len(seq['seq_size']))
    else:
        groups = ((0, 8, 1), (9, 8, 8), (18, 8, 6),
                  (27, 8, 9), (36, 8, 25))
        for base, dirs, frames in groups:
            if seq['seq_size'][base:base + dirs] != [frames] * dirs:
                out.append('SCORP.SEQ slot %d does not retain %d authored frames'
                           % (base, frames))
        if [seq['seq_size'][i] for i in range(45, 51)] != [40, 12, 20, 3, 1, 1]:
            out.append('SCORP.SEQ malfunction/death/corpse cadence differs')

    wanted_tiles = set(range(20200, 20367))
    actual_tiles = set(seq['ck_pict'])
    if actual_tiles != wanted_tiles:
        out.append('SCORP.SEQ exact tile coverage differs: %d missing, %d extra'
                   % (len(wanted_tiles - actual_tiles),
                      len(actual_tiles - wanted_tiles)))
    wanted_names = [b'ROCH_GUN', b'SCRP_CLA', b'SLK_AT2', b'SCRP_ICU',
                    b'SCRP_HIT', b'SCORP_Z2', b'SCRP_ZP1']
    if [n.rstrip(b'\0') for n in seq['snd_names']] != wanted_names:
        out.append('SCORP.SEQ sound table differs from retail Selkis callbacks')
    if len(seq['seq_base']) > 49:
        event_specs = ((18, 3, 0), (27, 0, 1), (27, 2, 2),
                       (27, 5, 1), (27, 8, 1), (36, 0, 3),
                       (45, 15, 3), (46, 7, 4), (46, 8, 5),
                       (47, 16, 6), (48, 2, 6))
        wanted_events = [(seq['seq_base'][slot] + frame, sound)
                         for slot, frame, sound in event_specs]
        if seq['snd_events'] != wanted_events:
            out.append('SCORP.SEQ sound events differ from authored callback frames')
        wanted_fire = [seq['seq_base'][18 + d] + 3 for d in range(8)]
        actual_fire = [i for i, flag in enumerate(seq['fr_flag'])
                       if (flag & 0x84) == 0x84]
        if actual_fire != wanted_fire:
            out.append('SCORP.SEQ ranged callback/fullbright differs from frame 3')
        wanted_melee = sorted(seq['seq_base'][27 + d] + frame
                              for d in range(8) for frame in (0, 6))
        actual_melee = sorted(i for i, flag in enumerate(seq['fr_flag'])
                              if (flag & 0x80) and i not in actual_fire)
        if actual_melee != wanted_melee:
            out.append('SCORP.SEQ melee callbacks differ from frames 0/6')
    if any(seq['ck_flag']):
        out.append('SCORP.SEQ precomposited eight-view tiles are being flipped twice')

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
    except OSError:
        return out + ['exhumed.def is missing']
    tilefiles = {int(tile): rel for tile, rel in re.findall(
        r'tilefromtexture\s+(\d+)\s*\{\s*file\s+"([^"]+)"', deftext)}
    for tile in wanted_tiles:
        rel = tilefiles.get(tile)
        try:
            with open(os.path.join(outdir, rel), 'rb') as image:
                head = image.read(24)
            if head[:8] != b'\x89PNG\r\n\x1a\n' or len(head) < 24:
                raise ValueError
        except (OSError, TypeError, ValueError):
            out.append('SCORP.SEQ tile %d has no valid PNG' % tile)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        aliases = (('roch_gun', 'sentryfire'), ('scrp_cla', 'selkisatk01'),
                   ('slk_at2', 'selkisatk02'), ('scrp_icu', 'selkisact'),
                   ('scrp_hit', 'selkishurt'), ('scorp_z2', 'selkisfizz01'),
                   ('scrp_zp1', 'selkisfizz02'), ('scrp_cry', 'selkisdeath'))
        for alias, wav in aliases:
            if not re.search(r'^' + alias + r'\s+sounds/' + wav + r'\.wav\s+0$',
                             sounds, re.M):
                out.append('Selkis sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    try:
        native = re.sub(r'\s+', '', open(os.path.join(srcdir, 'scorp.cpp')).read())
    except OSError:
        return out + ['cannot read native Selkis controller']
    native_needles = (
        'KexSelkisSequenceExact()', 'KexSelkisActionSeq',
        'SeqSize[base+27]==9', 'SeqSize[base+48]==3',
        '(RandomLong()&30)!=(totalmoves&30)',
        'KexHealthPoints(scorpion[nScorp].nHealth)',
        'health<1000', 'health<2000', 'health<3000',
        'sprite[nTarget].statnum==100', 'KexDifficulty_PainRoll(16)',
        'nRenderedFrame==0||nRenderedFrame==6',
        'runlist_DamageEnemy(nTarget,nSprite,20)', 'nRenderedFrame==3',
        'BuildKexAIFireProjectile(nSprite,16,nTarget,2,144,64,0)',
        'nRenderedFrame==5', 'nRenderedFrame==1',
        'KexSelkisCountDown(nScorp,nSprite)', '--scorpion[nScorp].g',
        'scorpion[nScorp].g&1',
        'BuildSpider(-1,x,y,z,sprite[nSprite].sectnum,sprite[nSprite].ang,1)',
        'KexExplosion01_Spawn(x,y,z,sprite[nSprite].sectnum,192,0)',
        'VB_KEX_SELKIS_TEST')
    for needle in native_needles:
        if needle not in native:
            out.append('native Selkis controller no longer implements %s' % needle)
    return out


def verify_projectiles(z, outdir):
    """Retail enemy-projectile definitions/callbacks and native KEX bridge."""
    out = []
    names = {n.lower(): n for n in z.namelist()}

    projectile_real = names.get('defs/actors/projectiles.txt')
    projectile_text = z.read(projectile_real).decode('latin-1').replace('\r', '') \
        if projectile_real else ''
    if not projectile_text:
        return ['retail projectile definitions are missing']

    # The shared spawner's fireball branch creates built-in actor type 200 at
    # the muzzle. It is deliberately not Fx_FireballExplode: the shooting puff
    # is the direct, red-tinted magicPuff recipe.
    effects_real = names.get('defs/actors/effects.txt')
    effects_text = z.read(effects_real).decode('latin-1').replace('\r', '') \
        if effects_real else ''
    muzzle_match = re.search(r'Actor\s+Fx_FireballShootPuff\s*\{(.*?)\n\}',
                             effects_text, re.S)
    if not muzzle_match:
        out.append('retail Fx_FireballShootPuff definition is missing')
    else:
        muzzle_compact = re.sub(r'\s+', '', muzzle_match.group(1))
        for token in ('scale2.0', 'animSpeed0.25',
                      'spawnAnim"effects/magicPuff"', 'fullbright1',
                      'color"0.750.250.25"'):
            if token not in muzzle_compact:
                out.append('retail muzzle-puff recipe changed: %s' % token)
    defines_real = names.get('defs/defines.txt')
    defines_text = z.read(defines_real).decode('latin-1') if defines_real else ''
    if not re.search(r'^#define\s+Fx_FireballShootPuff\s+200\s*$',
                     defines_text.replace('\r', ''), re.M):
        out.append('Fx_FireballShootPuff is no longer built-in actor type 200')
    muzzle_frames = K.parse_anim_full(z, names, 'effects/magicPuff')
    if (len(muzzle_frames) != K.KEX_MUZZLE_VISIBLE_FRAMES + 1
            or any(f['delay'] != 1 for f in muzzle_frames)
            or not muzzle_frames[-1].get('destroy')
            or any(not f.get('sprites') for f in muzzle_frames[:-1])):
        out.append('retail effects/magicPuff visual lifetime changed')

    # Proj_Fireball and Proj_Boulder both spawn this distinct orange impact
    # actor. Its zero-delay alias creates the dlight and, because ChangeAnim()
    # is followed by the ordinary frame increment, enters magicPuff at frame 1.
    impact_match = re.search(r'Actor\s+Fx_FireballExplode\s*\{(.*?)\n\}',
                             effects_text, re.S)
    if not impact_match:
        out.append('retail Fx_FireballExplode definition is missing')
    else:
        impact_compact = re.sub(r'\s+', '', impact_match.group(1))
        for token in ('scale2.0', 'animSpeed0.25',
                      'spawnAnim"effects/fireballPuff"', 'fullbright1',
                      'color"0.750.3750.25"'):
            if token not in impact_compact:
                out.append('retail impact-puff recipe changed: %s' % token)
    if not re.search(r'^#define\s+Fx_FireballExplode\s+1003\s*$',
                     defines_text.replace('\r', ''), re.M):
        out.append('Fx_FireballExplode is no longer built-in actor type 1003')
    impact_alias = K.parse_anim_full(z, names, 'effects/fireballPuff')
    if (len(impact_alias) != 1 or impact_alias[0]['delay'] != 0
            or impact_alias[0].get('goto') != 'effects/magicPuff'
            or impact_alias[0].get('sprites')
            or impact_alias[0].get('spawn_light')
               != (512.0, .75, .375, .25, 15.0, 2.0)):
        out.append('retail effects/fireballPuff alias/light contract changed')
    if (K.KEX_IMPACT_FIRST_FRAME != 1 or K.KEX_IMPACT_VISIBLE_FRAMES != 7
            or K.KEX_IMPACT_FRAME_HOLD != 2):
        out.append('converter impact-puff frame-selection contract changed')

    # Red and green cobra callbacks spawn differently tinted aliases of the
    # same magicPuff run, with distinct fixed-radius lights.
    cobra_effects = (
        ('Fx_RedCobraExplode', 'effects/cobraPuff2',
         'color"0.750.250.25"',
         (384.0, .75, .375, .375, 15.0, 1.0)),
        ('Fx_GreenCobraExplode', 'effects/cobraPuff1',
         'color"0.3750.750.375"',
         (768.0, .375, .75, .375, 15.0, 1.0)),
    )
    for actor, alias, color, light in cobra_effects:
        match = re.search(r'Actor\s+' + re.escape(actor)
                          + r'\s*\{(.*?)\n\}', effects_text, re.S)
        if not match:
            out.append('retail cobra-puff actor is missing: %s' % actor)
            continue
        compact = re.sub(r'\s+', '', match.group(1))
        for token in ('scale2.0', 'animSpeed0.25',
                      'spawnAnim"%s"' % alias, 'fullbright1', color):
            if token not in compact:
                out.append('%s recipe changed: %s' % (actor, token))
        parsed = K.parse_anim_full(z, names, alias)
        if (len(parsed) != 1 or parsed[0]['delay'] != 0
                or parsed[0].get('goto') != 'effects/magicPuff'
                or parsed[0].get('sprites')
                or parsed[0].get('spawn_light') != light):
            out.append('%s alias/light contract changed' % alias)
    if (K.KEX_COBRA_RED_TILE_BASE != 21915
            or K.KEX_COBRA_GREEN_TILE_BASE != 21922
            or K.KEX_COBRA_VISIBLE_FRAMES != 7
            or K.KEX_COBRA_FRAME_HOLD != 2
            or K.KEX_COBRA_CONTROLLER_FRAMES != 12):
        out.append('converter cobra-puff tile/cadence contract changed')

    anubis_impact = K.parse_anim_full(
        z, names, 'projectiles/anubis_fireball_explode')
    if (len(anubis_impact) != K.KEX_ANUBIS_IMPACT_VISIBLE_FRAMES + 1
            or K.KEX_ANUBIS_IMPACT_TILE_BASE != 21929
            or K.KEX_ANUBIS_IMPACT_VISIBLE_FRAMES != 13
            or any(f['delay'] != 1 for f in anubis_impact)
            or any(f.get('destroy') for f in anubis_impact[:-1])
            or not anubis_impact[-1].get('destroy')
            or any(not f.get('sprites') for f in anubis_impact)):
        out.append('retail Anubis-fireball impact lifetime changed')

    # Missing boolean fields are false. Proj_Soul intentionally omits
    # homingMaxPitch and receives the projectile class's 0.25 default.
    expected_projectiles = {
        'Proj_Anubis': {'radius': 36, 'height': 32, 'scale': 1,
                        'damage': 20, 'fullbright': 1},
        'Proj_Blaster': {'radius': 36, 'height': 32, 'scale': 1,
                         'damage': 30, 'fullbright': 1},
        'Proj_Cobra_Red': {'radius': 16, 'height': 64, 'scale': 1,
                           'floorOffset': 8, 'damage': 0,
                           'impactWallsOnly': 1, 'homing': 1,
                           'noHomingThrust': 1, 'homingTurnAngles': .03,
                           'homingMaxPitch': .43},
        'Proj_Cobra_Red_Selkis': {'radius': 16, 'height': 64, 'scale': 1,
                                  'floorOffset': 8, 'damage': 0,
                                  'impactWallsOnly': 1, 'homing': 1,
                                  'noHomingThrust': 1,
                                  'homingTurnAngles': .028,
                                  'homingMaxPitch': 1.0},
        'Proj_Soul': {'radius': 36, 'height': 32, 'scale': 1,
                      'floorOffset': 8, 'damage': 40,
                      'impactWallsOnly': 1, 'homing': 1,
                      'noHomingThrust': 0, 'homingTurnAngles': .0218,
                      'homingMaxPitch': .25},
        'Proj_Fireball': {'radius': 25, 'height': 32, 'scale': 1,
                          'damage': 100, 'fullbright': 1},
        'Proj_Boulder': {'radius': 25, 'height': 32, 'scale': 2,
                         'stepHeight': 36, 'health': 8, 'damage': 35,
                         'fullbright': 1, 'friction': 0,
                         'moveable': 1, 'bouncy': 1, 'expires': 1,
                         'gravity': .75, 'bouncePower': 1.9375},
        'Proj_Boulder_Piece': {'radius': 32, 'height': 32, 'scale': 1,
                               'moveable': 1, 'bouncy': 1,
                               'gravity': .75},
        'Proj_AmunBomb': {'radius': 9, 'height': 16, 'scale': .5,
                          'damage': 0, 'fullbright': 1, 'moveable': 1,
                          'bouncy': 0, 'expires': 0, 'gravity': .75},
        'Proj_Seedling': {'radius': 36, 'height': 32, 'scale': 1,
                          'damage': 20, 'fullbright': 1},
    }
    defaults = {'floorOffset': 0, 'damage': 0, 'fullbright': 0,
                'impactWallsOnly': 0, 'homing': 0,
                'noHomingThrust': 0, 'homingTurnAngles': .03125,
                'homingMaxPitch': .25, 'health': 100, 'moveable': 0,
                'bouncy': 0, 'expires': 0, 'gravity': .75,
                'stepHeight': 16, 'friction': .5, 'bouncePower': 2}
    for actor, fields in expected_projectiles.items():
        match = re.search(r'Actor\s+' + re.escape(actor)
                          + r'\s*\{(.*?)\n\}', projectile_text, re.S)
        if not match:
            out.append('retail projectile definition is missing: %s' % actor)
            continue
        body = match.group(1)
        for field, wanted in fields.items():
            value = re.search(r'^\s*' + re.escape(field)
                              + r'\s+([-+\d.]+)', body, re.M)
            actual = float(value.group(1)) if value else defaults.get(field)
            if actual is None or abs(actual - wanted) > 1e-6:
                out.append('%s %s changed from authored %s to %s'
                           % (actor, field, wanted, actual))

    death_anims = {
        'Proj_Anubis': 'projectiles/anubis_fireball_explode',
        'Proj_Blaster': 'monsters/sentry_projectile_destroy',
        'Proj_Cobra_Green': 'projectiles/cobra_green_death',
        'Proj_Ra_Fire': 'projectiles/rafire_explode',
        'Proj_Beam': 'projectiles/beam_destroy',
        'Proj_Fireball': 'projectiles/fireball_death',
        'Proj_Seedling': 'monsters/seedling_explode',
        'Proj_Cobra_Red': 'projectiles/cobra_red_death',
        'Proj_Cobra_Red_Selkis': 'projectiles/cobra_red_death',
        'Proj_Soul': 'projectiles/soul_explode',
    }
    for actor, wanted in death_anims.items():
        match = re.search(r'Actor\s+' + re.escape(actor)
                          + r'\s*\{(.*?)\n\}', projectile_text, re.S)
        actual = re.search(r'^\s*deathAnim\s+"([^"]+)"',
                           match.group(1), re.M) if match else None
        if not actual or actual.group(1) != wanted:
            out.append('%s deathAnim changed from %s' % (actor, wanted))

    launchers = {
        'AI_Anubis': ('defs/actors/monsters/anubis.txt', 48, 1.5),
        'AI_Sentry': ('defs/actors/monsters/sentry.txt', 48, 1.5),
        'AI_Set': ('defs/actors/monsters/set.txt', 96, 2.0),
        'AI_Selkis': ('defs/actors/monsters/selkis.txt', 80, 2.5),
        'AI_Mummy': ('defs/actors/monsters/mummy.txt', 48, 1.5),
        'AI_Queen': ('defs/actors/monsters/queen.txt', 64, 1.5),
        'AI_Mantis': ('defs/actors/monsters/mantis.txt', 48, 3.0),
    }
    for actor, (rel, radius, scale) in launchers.items():
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        match = re.search(r'Actor\s+' + re.escape(actor)
                          + r'\s*\{(.*?)\n\}', text, re.S)
        if not match:
            out.append('retail launcher definition is missing: %s' % actor)
            continue
        body = match.group(1)
        for field, wanted in (('radius', radius), ('scale', scale)):
            value = re.search(r'^\s*' + field + r'\s+([-+\d.]+)', body, re.M)
            actual = float(value.group(1)) if value else None
            if actual is None or abs(actual - wanted) > 1e-6:
                out.append('%s %s changed from authored %s to %s'
                           % (actor, field, wanted, actual))

    mantis_real = names.get('defs/actors/monsters/mantis.txt')
    mantis_text = z.read(mantis_real).decode('latin-1').replace('\r', '') \
        if mantis_real else ''
    mantis_match = re.search(r'Actor\s+AI_Mantis\s*\{(.*?)\n\}',
                             mantis_text, re.S)
    if mantis_match:
        mantis_body = mantis_match.group(1)
        mantis_fields = {
            'height': 384, 'health': 400, 'moveSpeed': 64,
            'painChance': 16, 'noInFighting': 1, 'solid': 0,
            'shootable': 0, 'hidden': 1,
        }
        for field, wanted in mantis_fields.items():
            value = re.search(r'^\s*' + re.escape(field)
                              + r'\s+([-+\d.]+)', mantis_body, re.M)
            actual = float(value.group(1)) if value else None
            if actual is None or abs(actual - wanted) > 1e-6:
                out.append('AI_Mantis %s changed from authored %s to %s'
                           % (field, wanted, actual))
    else:
        out.append('retail Mantis actor definition is missing')

    callback_specs = (
        ('sprites/monsters/anubis_anim.txt',
         ('Proj_Anubis', 0, 0, 4, 96, 16)),
        ('sprites/monsters/sentry_anim.txt',
         ('Proj_Blaster', 0, 0, 4, 96, 24)),
        ('sprites/monsters/set_anim.txt',
         ('Proj_Soul', 0, 0, 50, 192, 16)),
        ('sprites/monsters/selkis_anim.txt',
         ('Proj_Cobra_Red_Selkis', 0, 0, 2, 144, 64)),
        ('sprites/monsters/mummy_anim.txt',
         ('Proj_Cobra_Red', 0, 0, 2, 96, 12)),
        ('sprites/monsters/mantis_attack_anim.txt',
         ('Proj_Fireball', 0, 0, 8, 192, 16)),
    )
    callback_re = re.compile(
        r'A_FireProjectile\("([^"]+)"\s*,\s*([-+\d.]+)\s*,'
        r'\s*([-+\d.]+)\s*,\s*([-+\d.]+)\s*,\s*([-+\d.]+)\s*,'
        r'\s*([-+\d.]+)\s*\)')
    for rel, wanted in callback_specs:
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        actual = [(m.group(1),) + tuple(float(v) for v in m.groups()[1:])
                  for m in callback_re.finditer(text)]
        numeric_wanted = (wanted[0],) + tuple(float(v) for v in wanted[1:])
        if numeric_wanted not in actual:
            out.append('%s lacks authored callback %s' % (rel, wanted))

    # The Mantis has two genuinely different retail fire paths. Audit the
    # shipped scripts and animation timing, rather than accepting the old
    # native single-volley approximation merely because it launches type 10.
    script_real = names.get('scripts/p_enemy.txt')
    script_text = z.read(script_real).decode('latin-1').replace('\r', '') \
        if script_real else ''

    def script_body(name):
        match = re.search(r'void\s+' + re.escape(name)
                          + r'\s*\([^)]*\)\s*', script_text)
        return K._brace_block(script_text, match.end())[0] if match else ''

    script_checks = {
        'A_MantisThink': (
            'kexSlaveRandomizer::UInt8()>32',
            'actor.CanSeeLocation(org,2000)',
            'randCount=kexSlaveRandomizer::Size(3)',
            'actor.m_fScratchParam1>=2',
            'actor.m_fScratchParam0<(randCount+3)',
            'actor.ChangeAnim("monsters/mantis_peak")',
            'actor.m_fScratchParam0++',
            'actor.m_fScratchParam1++',
            'actor.ChangeAnim("monsters/mantis_rise")'),
        'A_MantisAttackThink': (
            '(kexSlaveRandomizer::UInt8()&127)==0',
            'actor.ChangeAnim("monsters/mantis_attack_psx")',
            'actor.m_fScratchParam2=1+kexSlaveRandomizer::Size(3)',
            'actor.ChangeAnim("monsters/mantis_attack_saturn")'),
        'A_MantisCheckAttack': (
            'actor.m_fScratchParam2<=0||actor.GetTarget()isnull',
            'actor.m_fScratchParam2--',
            'actor.ChangeAnim("monsters/mantis_attack_saturn")'),
        'A_MantisAttack': (
            'actor.FireActor("Proj_Fireball",0,0,192)',
            'speed=8.0f+(Math::RandFloat()*8.0f)',
            'proj.SetTarget(actor)'),
        'A_MantisExplode': (
            'A_SpawnBouncyBalls(actor,192.0f)',),
    }
    for function, needles in script_checks.items():
        body = script_body(function)
        compact = re.sub(r'\s+', '', body)
        if not body:
            out.append('retail Mantis script is missing: %s' % function)
            continue
        for needle in needles:
            if needle not in compact:
                out.append('%s no longer contains authored logic: %s'
                           % (function, needle))
        if function == 'A_MantisAttack' \
                and compact.count('RFloat()*0.0625f') != 2:
            out.append('A_MantisAttack no longer has independent yaw/pitch spread')

    attack_real = names.get('sprites/monsters/mantis_attack_anim.txt')
    attack_text = z.read(attack_real).decode('latin-1').replace('\r', '') \
        if attack_real else ''

    def animation_frames(text, name):
        match = re.search(r'^' + re.escape(name) + r'\s*$',
                          text, re.M)
        if not match:
            return []
        body, _end = K._brace_block(text, match.end())
        frames = []
        for frame in re.finditer(r'^\s*frame\s*$', body, re.M):
            frame_body, _frame_end = K._brace_block(body, frame.end())
            frames.append(frame_body)
        return frames

    # Direct projectile damage and death-animation radial damage are distinct
    # contracts. In particular, the red cobras have zero direct damage but a
    # live blast callback. Audit every gameplay-relevant impact animation.
    impact_specs = (
        ('sprites/projectiles/cobra_anim.txt', 'cobra_red_death', 224, 50, 0),
        ('sprites/projectiles/cobra_anim.txt', 'cobra_green_death', 512, 100, 0),
        ('sprites/projectiles/fireball_anim.txt', 'fireball_death', 192, 64, 1),
        ('sprites/projectiles/boulder_anim.txt', 'boulder_explode', 192, 64, 1),
        ('sprites/projectiles/amun_bomb_anim.txt', 'amun_bomb_death', 512, 140, 1),
        ('sprites/projectiles/amun_bomb_anim.txt', 'amun_bomb_death_water', 512, 140, 1),
    )
    impact_frames = {}
    for rel, anim, radius, damage, toggle in impact_specs:
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        frames = animation_frames(text, anim)
        impact_frames[anim] = frames
        if not frames:
            out.append('%s is missing authored impact animation %s' % (rel, anim))
            continue
        radial = re.compile(r'A_RadialBlast\(\s*%d\s*,\s*%d\s*,\s*%d\s*\)'
                            % (radius, damage, toggle))
        if not radial.search(frames[0]):
            out.append('%s lost A_RadialBlast(%d,%d,%d) on impact frame 0'
                       % (anim, radius, damage, toggle))
    boulder_frames = impact_frames.get('boulder_explode', [])
    if boulder_frames:
        toss = re.compile(r'A_TossActor\("Proj_Boulder_Piece"\s*,\s*0\s*,\s*0\s*,'
                          r'\s*32\s*,\s*8\s*,\s*8\s*,\s*4\s*,\s*16\s*\)')
        actual_tosses = len(toss.findall(boulder_frames[0]))
        if actual_tosses != 4:
            out.append('boulder_explode toss count changed from 4 to %d'
                       % actual_tosses)
    fireball_frames = impact_frames.get('fireball_death', [])
    if fireball_frames:
        frame = fireball_frames[0]
        sound = 'A_PlayLocalSound("sounds/pbexplode.wav")'
        spawn = 'A_Spawn("Fx_FireballExplode", 0, 0, 0)'
        radial = 'A_RadialBlast(192, 64, 1)'
        if sound not in frame or spawn not in frame \
                or not (frame.find(sound) < frame.find(spawn) < frame.find(radial)):
            out.append('fireball_death lost sound/spawn/blast callback ordering')
    if boulder_frames:
        frame = boulder_frames[0]
        spawn = 'A_Spawn("Fx_FireballExplode", 0, 0, 2)'
        sound = 'A_PlayLocalSound("sounds/pbexplode.wav")'
        radial = 'A_RadialBlast(192, 64, 1)'
        if spawn not in frame or sound not in frame \
                or not (frame.find(spawn) < frame.find(sound) < frame.find(radial)):
            out.append('boulder_explode lost +2 spawn/sound/blast callback ordering')

    # Cobra and Soul impacts are timed callback-only animations: no sprite art
    # belongs to the projectile itself once it has impacted.
    for anim, callback in (('cobra_red_death', 'A_RedCobraExplode()'),
                           ('cobra_green_death', 'A_CStaffExplode()')):
        frames = impact_frames.get(anim, [])
        if (len(frames) != 5
                or any(callback not in frame or 'delay 3' not in frame
                       for frame in frames[:4])
                or 'A_Destroy()' not in frames[4]
                or 'delay 1' not in frames[4]):
            out.append('%s lost its four callbacks at 3-tick cadence' % anim)

    def read_anim(rel, anim):
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') if real else ''
        return animation_frames(text, anim)

    soul_frames = read_anim('sprites/projectiles/soul_anim.txt',
                            'soul_explode')
    if (len(soul_frames) != 5
            or any('A_RedCobraExplode()' not in frame or 'delay 3' not in frame
                   for frame in soul_frames[:4])
            or 'A_Destroy()' not in soul_frames[4]
            or any('A_RadialBlast' in frame for frame in soul_frames)):
        out.append('soul_explode lost its non-radial four-red-puff contract')

    for rel, anim in (('sprites/monsters/seedling_anim.txt',
                       'seedling_explode'),
                      ('sprites/projectiles/rafire_anim.txt',
                       'rafire_explode')):
        frames = read_anim(rel, anim)
        spawn = 'A_Spawn("Fx_FireballExplode", 0, 0, 2)'
        sound = 'A_PlayLocalSound("sounds/pbexplode.wav")'
        if (len(frames) != 2 or spawn not in frames[0]
                or sound not in frames[0]
                or frames[0].find(spawn) > frames[0].find(sound)
                or 'A_RadialBlast' in ''.join(frames)
                or 'A_Destroy()' not in frames[1]):
            out.append('%s lost its +2 spawn/sound non-radial contract' % anim)

    sentry_destroy = read_anim('sprites/monsters/sentry_anim.txt',
                               'sentry_projectile_destroy')
    beam_destroy = read_anim('sprites/projectiles/beam_anim.txt',
                             'beam_destroy')
    if (len(sentry_destroy) != 1
            or 'A_Destroy()' not in sentry_destroy[0]
            or re.search(r'\bsprites\b', sentry_destroy[0])):
        out.append('sentry_projectile_destroy is no longer an invisible destroy')
    if (len(beam_destroy) != 1
            or 'A_DestroyBoltTails()' not in beam_destroy[0]
            or 'A_Destroy()' not in beam_destroy[0]
            or beam_destroy[0].find('A_DestroyBoltTails()')
               > beam_destroy[0].find('A_Destroy()')
            or re.search(r'\bsprites\b', beam_destroy[0])):
        out.append('beam_destroy is no longer an invisible tail/destroy callback')

    # Both random-puff callbacks consume one centred 32-unit draw per axis,
    # spawn the corresponding effect, and attach pbexplode only on success.
    callback_scripts = {}
    for rel in ('scripts/p_enemy.txt', 'scripts/p_pspr.txt'):
        real = names.get(rel)
        if real:
            callback_scripts[rel] = z.read(real).decode('latin-1').replace('\r', '')
    for function, effect in (('A_RedCobraExplode', 'Fx_RedCobraExplode'),
                             ('A_CStaffExplode', 'Fx_GreenCobraExplode')):
        body = ''
        for text in callback_scripts.values():
            match = re.search(r'void\s+' + function + r'\s*\([^)]*\)', text)
            if match:
                body = K._brace_block(text, match.end())[0]
                break
        compact = re.sub(r'\s+', '', body)
        if (compact.count('kexSlaveRandomizer::RFloatVal(32.0f)') != 3
                or ('SpawnActor("%s",x,y,z,0,actor.GetSector())' % effect)
                   not in compact
                or 'if(puffisnull)' not in compact
                or 'puff.PlaySound("sounds/pbexplode.wav")' not in compact):
            out.append('%s random spawn/sound contract changed' % function)

    pspr_real = names.get('scripts/p_pspr.txt')
    pspr_text = z.read(pspr_real).decode('latin-1').replace('\r', '') \
        if pspr_real else ''
    pspr_compact = re.sub(r'\s+', '', pspr_text)
    bomb_script_checks = (
        'floatbombChargeThrow=16.0f;',
        'bomb.GetVelocity()=(dir*bombChargeThrow);',
        'bomb.GetVelocity().z=8.0f+((bombChargeThrow+8.0f)*dir.z);',
        'bombChargeThrow=16.0f;',
        'bombChargeThrow+=1.0f;',
        'if(bombChargeThrow>48.0f)',
        'bombChargeThrow=48.0f;',
    )
    for needle in bomb_script_checks:
        if needle not in pspr_compact:
            out.append('retail Amun-bomb script no longer contains %s' % needle)

    bomb_anim_real = names.get('sprites/weapons/bomb_anim.txt')
    bomb_anim_text = z.read(bomb_anim_real).decode('latin-1').replace('\r', '') \
        if bomb_anim_real else ''
    bomb_charge = animation_frames(bomb_anim_text, 'bomb_charge')
    bomb_throw = animation_frames(bomb_anim_text, 'bomb_throw')
    if len(bomb_charge) != 1 or 'A_ChargeBombThrow()' not in bomb_charge[0]:
        out.append('bomb_charge lost its one-frame A_ChargeBombThrow callback')
    if len(bomb_throw) < 2 \
            or 'A_ThrowAmunBomb()' not in bomb_throw[1] \
            or 'A_ConsumeAmmo(1)' not in bomb_throw[1]:
        out.append('bomb_throw frame 1 lost throw/consume ordering')

    mantis_anims = {
        name: animation_frames(attack_text, name) for name in (
            'mantis_attack', 'mantis_attack_think',
            'mantis_attack_psx', 'mantis_attack_saturn')
    }
    for name, wanted in (('mantis_attack', 3),
                         ('mantis_attack_think', 1),
                         ('mantis_attack_psx', 9),
                         ('mantis_attack_saturn', 5)):
        actual = len(mantis_anims[name])
        if actual != wanted:
            out.append('%s frame count changed from %d to %d'
                       % (name, wanted, actual))

    common = mantis_anims['mantis_attack']
    think = mantis_anims['mantis_attack_think']
    psx = mantis_anims['mantis_attack_psx']
    saturn = mantis_anims['mantis_attack_saturn']
    if len(common) == 3:
        if 'A_FaceTarget()' not in common[0]:
            out.append('mantis_attack no longer faces on common frame 0')
        if 'goto "monsters/mantis_attack_think"' not in common[2]:
            out.append('mantis_attack no longer enters think after frame 2')
    if len(think) == 1 and 'A_MantisAttackThink()' not in think[0]:
        out.append('mantis_attack_think lost its chooser callback')
    if len(psx) == 9:
        psx_calls = [i for i, frame in enumerate(psx)
                     if 'A_MantisAttack()' in frame]
        if psx_calls != [1, 2, 3, 4, 5]:
            out.append('mantis_attack_psx callbacks changed from frames 1..5: %s'
                       % psx_calls)
        if 'goto "monsters/mantis_submerge"' not in psx[8]:
            out.append('mantis_attack_psx no longer submerges after frame 8')
    if len(saturn) == 5:
        if 'A_FaceTarget()' not in saturn[0] \
                or 'A_PlayLocalSound("sounds/mantisatk01.wav")' not in saturn[0]:
            out.append('mantis_attack_saturn lost its frame-0 face/sound cue')
        if 'A_FaceTarget()' not in saturn[1] \
                or 'A_FireProjectile("Proj_Fireball"' not in saturn[1] \
                or 'delay 4' not in saturn[1]:
            out.append('mantis_attack_saturn lost its frame-1 fire contract')
        if any('delay 4' not in frame for frame in saturn[1:]):
            out.append('mantis_attack_saturn no longer has four delayed tail frames')
        if 'A_MantisCheckAttack()' not in saturn[4]:
            out.append('mantis_attack_saturn lost its terminal repeat check')

    # The hidden chase/peek/rise/submerge loop is as authored as the attack.
    # Its callbacks control when the Mantis can be seen, shot, and blocked.
    lifecycle_files = {
        'mantis_spawn': 'sprites/monsters/mantis_anim.txt',
        'mantis_chase': 'sprites/monsters/mantis_anim.txt',
        'mantis_peak': 'sprites/monsters/mantis_anim.txt',
        'mantis_rise': 'sprites/monsters/mantis_rise_anim.txt',
        'mantis_pain': 'sprites/monsters/mantis_pain_anim.txt',
        'mantis_submerge': 'sprites/monsters/mantis_submerge_anim.txt',
        'mantis_splash': 'sprites/monsters/mantis_splash_anim.txt',
        'mantis_death': 'sprites/monsters/mantis_death_anim.txt',
    }
    lifecycle = {}
    for name, rel in lifecycle_files.items():
        real = names.get(rel.lower())
        text = z.read(real).decode('latin-1').replace('\r', '') \
            if real else ''
        lifecycle[name] = animation_frames(text, name)

    lifecycle_counts = {
        'mantis_spawn': 1, 'mantis_chase': 1, 'mantis_peak': 62,
        'mantis_rise': 60, 'mantis_pain': 15,
        'mantis_submerge': 24, 'mantis_splash': 21,
        'mantis_death': 54,
    }
    for name, wanted in lifecycle_counts.items():
        actual = len(lifecycle[name])
        if actual != wanted:
            out.append('%s frame count changed from %d to %d'
                       % (name, wanted, actual))

    def require_frame(name, frame, needles):
        frames = lifecycle[name]
        if frame >= len(frames):
            return
        for needle in needles:
            if needle not in frames[frame]:
                out.append('%s frame %d lost callback: %s'
                           % (name, frame, needle))

    require_frame('mantis_spawn', 0, ('delay 1',))
    require_frame('mantis_chase', 0, ('A_MantisThink()', 'delay 4'))
    require_frame('mantis_peak', 0, ('A_Show()',))
    require_frame('mantis_peak', 2,
                  ('A_PlayLocalSound("sounds/mantisact01.wav")',))
    require_frame('mantis_peak', 24,
                  ('A_PlayLocalSound("sounds/mantisact03.wav")',))
    require_frame('mantis_peak', 37,
                  ('A_PlayLocalSound("sounds/mantisact02.wav")',))
    require_frame('mantis_peak', 61,
                  ('A_Hide()', 'goto "monsters/mantis_chase"'))
    require_frame('mantis_rise', 0,
                  ('A_FaceTarget()', 'A_Show()', 'A_Quake(60)'))
    require_frame('mantis_rise', 4,
                  ('A_PlayLocalSound("sounds/mantisact02.wav")',))
    require_frame('mantis_rise', 12,
                  ('A_PlayLocalSound("sounds/mantisact04.wav")',))
    require_frame('mantis_rise', 14,
                  ('A_PlayLocalSound("sounds/mantisrise.wav")',))
    require_frame('mantis_rise', 31, ('A_MantisSetBlocking(1)',))
    require_frame('mantis_rise', 59,
                  ('goto "monsters/mantis_attack"',))
    require_frame('mantis_pain', 0,
                  ('A_PlayLocalSound("sounds/mantispain.wav")',
                   'A_MantisSetBlocking(0)'))
    require_frame('mantis_pain', 14,
                  ('goto "monsters/mantis_submerge"',))
    require_frame('mantis_submerge', 3, ('A_MantisSetBlocking(0)',))
    require_frame('mantis_submerge', 23,
                  ('goto "monsters/mantis_splash"',))
    require_frame('mantis_splash', 4,
                  ('A_PlayLocalSound("sounds/mantisact03.wav")',))
    require_frame('mantis_splash', 10,
                  ('A_PlayLocalSound("sounds/mantisact04.wav")',))
    require_frame('mantis_splash', 20,
                  ('A_Hide()', 'goto "monsters/mantis_chase"'))
    require_frame('mantis_death', 0,
                  ('A_TriggerEvent()',
                   'A_PlayLocalSound("sounds/mantisdeath.wav")',
                   'A_MantisSetBlocking(0)', 'A_MantisExplode()'))
    require_frame('mantis_death', 5, ('A_MantisExplode()',))
    require_frame('mantis_death', 8, ('A_MantisExplode()',))
    require_frame('mantis_death', 12, ('A_MantisSetBlocking(0)',))
    require_frame('mantis_death', 37,
                  ('A_PlayLocalSound("sounds/mantisact03.wav")',))
    require_frame('mantis_death', 43,
                  ('A_PlayLocalSound("sounds/mantisact04.wav")',))
    require_frame('mantis_death', 53, ('A_Hide()', 'A_Destroy()'))

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        if not re.search(r'^lav_act1\s+sounds/mantisact01\.wav\s+0$',
                         sounds, re.M):
            out.append('Mantis act01 sound alias is missing')
        if not re.search(r'^lav_act4\s+sounds/mantisact04\.wav\s+0$',
                         sounds, re.M):
            out.append('Mantis act04 sound alias is missing')
    except OSError:
        out.append('kexsound.txt is missing')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    source_specs = {
        'anubis.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?72:60;',
            'sprite[nSprite].xrepeat=g_kexMode?60:40;',
            'BuildKexAIFireProjectile(nSprite,8,nTarget,4,96,16,0);'),
        'roach.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?72:60;',
            'sprite[nSprite].xrepeat=g_kexMode?60:40;',
            'BuildKexAIFireProjectile(nSprite,13,nTarget,4,96,24,0);'),
        'set.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?144:110;',
            'sprite[nSprite].xrepeat=g_kexMode?80:87;',
            'BuildKexAIFireProjectile(nSprite,11,nTarget,50,192,16,1);'),
        'scorp.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?120:70;',
            'sprite[nSprite].xrepeat=g_kexMode?100:80;',
            'BuildKexAIFireProjectile(nSprite,16,nTarget,2,144,64,0);'),
        'mummy.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?72:32;',
            'sprite[nSprite].xrepeat=g_kexMode?60:42;',
            'BuildKexAIFireProjectile(nSprite,9,nTarget,2,96,12,0)'),
        'queen.cpp': (
            'sprite[nSprite].clipdist=bExactQueen?96:100;',
            'sprite[nSprite].xrepeat=bExactQueen?60:80;',
            'BuildKexAIFireProjectile(nSprite,12,nTarget,-64,128,32,0);',
            'VB_KEX_QUEEN_TEST'),
        'lavadude.cpp': (
            'sprite[nSprite].clipdist=g_kexMode?72:127;',
            'sprite[nSprite].xrepeat=g_kexMode?120:200;',
            'KexHealthUnits(400)',
            'BuildKexAIFireProjectile(nSprite,10,nTarget,8,192,16,0)',
            'BuildKexMantisFireProjectile(nSprite,nTarget)',
            '((uint8_t)RandomByte()&127)==0',
            '1+RandomSize(3)',
            '(uint8_t)RandomByte()>32',
            'KexMantisCanSeeTarget(nSprite,nTarget,2000)',
            'scratch1>=2',
            'scratch0<nRandCount+3',
            'KexMantisBeginAction(nLava,nSprite,nTarget,1)',
            'KexMantisBeginAction(nLava,nSprite,nTarget,2)',
            'KexMantisTimelineTick',
            'KexQuake(60)',
            'KexMantisPlaySound(nSprite,"lav_act1")',
            'KexMantisPlaySound(nSprite,"lav_act4")',
            'KexMantisBeginDeath(nLava,nSprite)',
            'nextFrame==5||nextFrame==8',
            'nextFrame>=53',
            'sprite[nTarget].statnum==100',
            'VB_KEX_MANTIS_TEST'),
        'kexbreak.cpp': (
            'KexBouncyBalls_Spawn',
            'z-offset*16*6',
            'KexProjectile_StartCobraBurst(',
            'KexProjectile_SpawnFireballImpact(',
            'int64_tconstdz=((int64_t)targetZ-sourceZ)/16',
            'dx*dx+dy*dy+dz*dz>(int64_t)t.sight*t.sight',
            'sourceZ=sprite[nSprite].z-32*16*6',
            'targetZ=sprite[nPlayer].z-(160*16*6)/2'),
        'grenade.cpp': (
            'kKexBombMinSpeed=16', 'kKexBombMaxSpeed=48',
            'kKexBombGravity=72', 'kKexBombBlastRadius=512',
            'kKexBombBlastDamage=140',
            'kKexBombBuildBlastRadius=kKexBombBlastRadius*6/256',
            'KexGrenade_Charge', 'KexGrenadePitchDirection',
            'if(GrenadeList[nGrenade].field_10<0)return;',
            'VB_KEX_BOMB_TEST', 'nofuse/bounce'),
        'gun.cpp': (
            'if(!g_kexMode)AddAmmo(nPlayer,kWeaponGrenade,-1);',
            'KexGrenade_Charge(nPlayer);'),
        'snake.cpp': (
            'g_kexMode?100:BulletInfo[kWeaponStaff].nDamage',
            'g_kexMode?512*6/256:BulletInfo[kWeaponStaff].nRadius'),
        'object.cpp': (
            'kKexTrapFireballInterval=60',
            'kKexTrapLaserInterval=10',
            'KexTrapFireEvent', 'KexTrapStartGroup',
            'if(delay>interval)delay-=interval;',
            'extraDelay+=8;', 'ConfigureKexTrap', 'KexTrapStop_Add',
            'KexTrapSpawnMuzzlePuff',
            'seq_IsLoaded(kSeqKexMuzzle)',
            'KexTrapConfigured(nTrap)&&nType==14',
            'exact?48:40', 'puffs%d/%d', 'exact-seq%d',
            'source-ordertag/tag+1', 'VB_KEX_TRAP_TEST'),
        'anims.cpp': (
            'KexImpactLight_Tick()', 'seq_IsLoaded(kSeqKexImpact)',
            'seq_IsLoaded(kSeqKexCobraRed)',
            'seq_IsLoaded(kSeqKexCobraGreen)',
            'seq_IsLoaded(kSeqKexAnuImpact)',
            'radius=512*4*6', 'radius=384*4*6', 'radius=768*4*6',
            'fadeSteps=15',
            'Kex_TransientGlow(sprite[nSprite].sectnum',
            'KexProjectile_CobraBurstFrame(nSprite,nFrame)'),
        'lighting.cpp': (
            'Kex_TransientGlowRec',
            '((int64_t)distance*127)/radius',
            'AddFlashRec(2,i,&wall[i].shade,&wall[i].pal,add)'),
        'move.cpp': ('KexImpactLight_Tick();',),
        'sequence.cpp': ('"amunbomb"', '"kexmuzzle"', '"keximpact"',
                         '"kexcobrared"', '"kexcobragreen"',
                         '"kexcobractl"', '"kexanuimpact"'),
    }
    for filename, needles in source_specs.items():
        try:
            compact = re.sub(r'\s+', '', open(os.path.join(srcdir, filename)).read())
        except OSError:
            out.append('cannot read projectile launcher: %s' % filename)
            continue
        for needle in needles:
            if needle not in compact:
                out.append('%s no longer implements %s' % (filename, needle))

    try:
        bullet = open(os.path.join(srcdir, 'bullet.cpp')).read()
        compact = re.sub(r'\s+', '', bullet)
        bridge_checks = (
            'if(!kexLaunch&&pBulletInfo->field_4>30000)',
            'returng_kexMode&&(nType==9||nType==11||nType==15||nType==16);',
            'unsignedconstclipMask=CLIPMASK1;',
            'BuildKexAIFireProjectile',
            'KexBullet_CheckAIFireProjectile',
            'KexBulletTargetCenterZ',
            'sprite[nTarget].z-KexActorHeightBuild(nTarget)/2',
            'logicalTargetCenterExact',
            '(160*16*6)/2',
            'BuildKexMantisFireProjectile',
            'KexBullet_CheckMantisFireProjectile',
            '{12,96,-64,128,32,0}',
            'Anubis/Sentry/Set/Selkis/Mummy/Queen/Mantis-Saturn/Mantis-PSX',
            'return{224,50,false};',
            'return{192,64,true};',
            'KexBulletApplyImpactBlast',
            'KexBulletBuildBlastRadius',
            'KexTossBouncyCreatureChunk(nSprite,kKexBoulderShardTile,kKexTossBounceSoundGlass)',
            'returnnType==17?80:40;',
            'kKexBoulderGravity=72',
            'kKexBoulderFloorContacts=8',
            'kKexBoulderHeight=32*16*6',
            'kKexBoulderStepHeight=36*16*6',
            'kKexBoulderBounceNumerator=31',
            'kKexBoulderBounceDenominator=16',
            'kKexSpawnerFireballSpeed=12*6',
            'kKexSpawnerLaserSpeed=16*6',
            'kKexSpawnerFireballHeight=32*16*6',
            'kKexSpawnerFireballStepHeight=36*16*6',
            'kKexSpawnerLaserHeight=8*16*6',
            'KexSpawnerLaunchSpeed', 'KexSpawnerProjectile',
            'KexMoveSpawnerProjectile',
            'KexBulletDamage(15)==100', 'KexBulletRadius(15)==36',
            'KexBoulderReflect', 'KexMoveBoulder',
            'KexBoulderStopped', 'KexBoulderHandleBounce',
            'KexBoulderPhysicsContractMatches',
            'KexBulletExactImpactAvailable',
            'KexBulletHandleDeathVisual',
            'KexBulletUsesRemasterDeath',
            'KexBulletSuppressClassicRadial',
            'KexProjectile_SpawnFireballImpact',
            'KexProjectile_StartCobraBurst',
            'KexProjectile_CobraBurstFrame',
            'KexBulletSpawnAnubisImpact',
            'KexProjectileImpactSound',
            'BuildAnim(-1,kSeqKexImpact,0,x,y,',
            'z-nKexZOffset*16*6',
            'BuildAnim(-1,kSeqKexAnuImpact,0,x,y,z,',
            'BuildAnim(-1,kSeqKexCobraCtl,0,x,y,z,',
            'nFrame!=2&&nFrame!=5&&nFrame!=8',
            'RandomSize(16)*amplitude*2',
            'if(!exactImpact)AddFlash(nSector,x,y,z,128);',
            'if(!kexExactImpact)',
            'suppressClassicPuff=KexBulletUsesRemasterDeath(pBullet->nType)',
            'if(!kexAuthoredBlast&&!KexBulletSuppressClassicRadial(nType)',
            'authoreddeathblasts', 'exact3-Dfireball/laser/boulderlaunch',
            'exactorange/cobra/Anubisimpactart,cadenceandlight',
            'nofalseSoul/SeedlingradialorBlaster/Beampuff',
            'VB_KEX_PROJECTILE_TEST')
        for needle in bridge_checks:
            if needle not in compact:
                out.append('native projectile bridge is missing: %s' % needle)
        if 'CLIPMASK0' in bullet:
            out.append('enemy projectile movement reverted to actor pass-through')
        for ntype in (9, 16):
            if ('if(nType==%d)return0;' % ntype) not in compact:
                out.append('native projectile type %d is no longer zero direct damage' % ntype)
    except OSError:
        out.append('cannot read native projectile bridge')

    # The hidden spawner actor is not the launch point. Retail computes the
    # polygon centroid named by params1, then offsets it 32 units along the
    # params2 face normal. Ensure every shipping boulder record can still be
    # resolved by the converter's exact helper.
    if not callable(getattr(K, '_spawner_origin', None)):
        out.append('converter is missing exact fireball-spawner muzzle geometry')
    else:
        unresolved = []
        boulder_count = 0
        for rel in names.values():
            if not rel.lower().startswith('maps/') or not rel.lower().endswith('.map'):
                continue
            try:
                m = K.locate(z.read(rel))
            except (ValueError, struct.error):
                continue
            for actor_index, actor in enumerate(m.actors):
                if actor.type != 128:
                    continue
                boulder_count += 1
                if K._spawner_origin(m, actor) is None:
                    unresolved.append((rel, actor_index, actor.p1, actor.p2))
        if boulder_count != 14:
            out.append('retail boulder-spawner count changed from 14 to %d'
                       % boulder_count)
        if unresolved:
            out.append('boulder spawner muzzle geometry is unresolved: %s'
                       % unresolved[:8])

    # Generated world-projectile sequence and private effect/projectile art.
    try:
        muzzle = S.read_seq(open(os.path.join(outdir, 'KEXMUZZLE.SEQ'), 'rb').read())
        expected_tiles = [K.KEX_MUZZLE_TILE_BASE + i
                          for i in range(K.KEX_MUZZLE_VISIBLE_FRAMES)
                          for _ in range(K.KEX_MUZZLE_FRAME_HOLD)]
        count = len(expected_tiles)
        if (muzzle['seq_base'] != [0] or muzzle['seq_size'] != [count]
                or muzzle['fr_base'] != list(range(count))
                or muzzle['fr_size'] != [1] * count
                or muzzle['fr_flag'] != [4] * count
                or muzzle['ck_x'] != [0] * count
                or muzzle['ck_y'] != [0] * count
                or muzzle['ck_pict'] != expected_tiles
                or muzzle['ck_flag'] != [0] * count):
            out.append('KEXMUZZLE.SEQ is not the exact 8x2-frame fullbright contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXMUZZLE.SEQ is missing or invalid: %s' % exc)

    try:
        impact = S.read_seq(open(os.path.join(outdir, 'KEXIMPACT.SEQ'), 'rb').read())
        expected_tiles = [K.KEX_IMPACT_TILE_BASE + i
                          for i in range(K.KEX_IMPACT_VISIBLE_FRAMES)
                          for _ in range(K.KEX_IMPACT_FRAME_HOLD)]
        count = len(expected_tiles)
        if (impact['seq_base'] != [0] or impact['seq_size'] != [count]
                or impact['fr_base'] != list(range(count))
                or impact['fr_size'] != [1] * count
                or impact['fr_flag'] != [4] * count
                or impact['ck_x'] != [0] * count
                or impact['ck_y'] != [0] * count
                or impact['ck_pict'] != expected_tiles
                or impact['ck_flag'] != [0] * count
                or impact['snd_names'] or impact['snd_events']):
            out.append('KEXIMPACT.SEQ is not the exact 7x2-frame fullbright contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXIMPACT.SEQ is missing or invalid: %s' % exc)

    for seqname, tile_base in (('KEXCOBRARED.SEQ',
                                K.KEX_COBRA_RED_TILE_BASE),
                               ('KEXCOBRAGREEN.SEQ',
                                K.KEX_COBRA_GREEN_TILE_BASE)):
        try:
            cobra = S.read_seq(open(os.path.join(outdir, seqname), 'rb').read())
            expected_tiles = [tile_base + i
                              for i in range(K.KEX_COBRA_VISIBLE_FRAMES)
                              for _ in range(K.KEX_COBRA_FRAME_HOLD)]
            count = len(expected_tiles)
            if (cobra['seq_base'] != [0] or cobra['seq_size'] != [count]
                    or cobra['fr_base'] != list(range(count))
                    or cobra['fr_size'] != [1] * count
                    or cobra['fr_flag'] != [4] * count
                    or cobra['ck_x'] != [0] * count
                    or cobra['ck_y'] != [0] * count
                    or cobra['ck_pict'] != expected_tiles
                    or cobra['ck_flag'] != [0] * count
                    or cobra['snd_names'] or cobra['snd_events']):
                out.append('%s is not the exact 7x2-frame puff contract'
                           % seqname)
        except (OSError, ValueError, AssertionError, struct.error) as exc:
            out.append('%s is missing or invalid: %s' % (seqname, exc))

    try:
        ctl = S.read_seq(open(os.path.join(outdir, 'KEXCOBRACTL.SEQ'),
                              'rb').read())
        count = K.KEX_COBRA_CONTROLLER_FRAMES
        if (ctl['seq_base'] != [0] or ctl['seq_size'] != [count]
                or ctl['fr_base'] != list(range(count))
                or ctl['fr_size'] != [1] * count
                or ctl['fr_flag'] != [0] * count
                or ctl['ck_x'] != [0] * count
                or ctl['ck_y'] != [0] * count
                or ctl['ck_pict'] != [0] * count
                or ctl['ck_flag'] != [0] * count
                or ctl['snd_names'] or ctl['snd_events']):
            out.append('KEXCOBRACTL.SEQ is not the exact 12-tick controller')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXCOBRACTL.SEQ is missing or invalid: %s' % exc)

    try:
        anu = S.read_seq(open(os.path.join(outdir, 'KEXANUIMPACT.SEQ'),
                              'rb').read())
        count = K.KEX_ANUBIS_IMPACT_VISIBLE_FRAMES
        expected_tiles = [K.KEX_ANUBIS_IMPACT_TILE_BASE + i
                          for i in range(count)]
        if (anu['seq_base'] != [0] or anu['seq_size'] != [count]
                or anu['fr_base'] != list(range(count))
                or anu['fr_size'] != [1] * count
                or anu['fr_flag'] != [4] * count
                or anu['ck_x'] != [0] * count
                or anu['ck_y'] != [0] * count
                or anu['ck_pict'] != expected_tiles
                or anu['ck_flag'] != [0] * count
                or anu['snd_names'] or anu['snd_events']):
            out.append('KEXANUIMPACT.SEQ is not the exact 13-frame contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXANUIMPACT.SEQ is missing or invalid: %s' % exc)

    try:
        amun = S.read_seq(open(os.path.join(outdir, 'AMUNBOMB.SEQ'), 'rb').read())
        if amun['seq_base'] != [0] or amun['seq_size'] != [1] \
                or amun['fr_base'] != [0] or amun['fr_size'] != [1] \
                or amun['ck_pict'] != [7848] \
                or not (amun['fr_flag'][0] & 4):
            out.append('AMUNBOMB.SEQ is not the exact one-frame/fullbright tile-7848 contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('AMUNBOMB.SEQ is missing or invalid: %s' % exc)
    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
        import kexpng as _png
        muzzle_dims = set()
        muzzle_hashes = set()
        red = green = blue = opaque = 0
        for frame in range(K.KEX_MUZZLE_VISIBLE_FRAMES):
            tile = K.KEX_MUZZLE_TILE_BASE + frame
            rel = 'kexart/muzzle/fireball_%d.png' % frame
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('muzzle-puff tile %d is absent from exhumed.def' % tile)
                continue
            path = os.path.join(outdir, rel)
            try:
                blob = open(path, 'rb').read()
                w, h, pixels = _png.read(path)
                muzzle_dims.add((w, h))
                muzzle_hashes.add(hashlib.sha256(blob).digest())
                for p in range(0, len(pixels), 4):
                    if pixels[p+3]:
                        red += pixels[p]
                        green += pixels[p+1]
                        blue += pixels[p+2]
                        opaque += 1
            except (OSError, ValueError, AssertionError, struct.error) as exc:
                out.append('muzzle-puff PNG is missing or invalid: %s' % exc)
        if len(muzzle_dims) != 1:
            out.append('muzzle-puff frames do not share one centred canvas: %s'
                       % sorted(muzzle_dims))
        if len(muzzle_hashes) != K.KEX_MUZZLE_VISIBLE_FRAMES:
            out.append('muzzle-puff visual frames are not all distinct')
        if not opaque or red <= green * 2 or red <= blue * 2:
            out.append('muzzle-puff art lost its authored red color multiplier')

        impact_dims = set()
        impact_hashes = set()
        impact_red = impact_green = impact_blue = impact_opaque = 0
        for frame in range(K.KEX_IMPACT_VISIBLE_FRAMES):
            tile = K.KEX_IMPACT_TILE_BASE + frame
            rel = 'kexart/impact/fireball_%d.png' % frame
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('impact-puff tile %d is absent from exhumed.def' % tile)
                continue
            path = os.path.join(outdir, rel)
            try:
                blob = open(path, 'rb').read()
                w, h, pixels = _png.read(path)
                impact_dims.add((w, h))
                impact_hashes.add(hashlib.sha256(blob).digest())
                for p in range(0, len(pixels), 4):
                    if pixels[p+3]:
                        impact_red += pixels[p]
                        impact_green += pixels[p+1]
                        impact_blue += pixels[p+2]
                        impact_opaque += 1
            except (OSError, ValueError, AssertionError, struct.error) as exc:
                out.append('impact-puff PNG is missing or invalid: %s' % exc)
        if len(impact_dims) != 1:
            out.append('impact-puff frames do not share one centred canvas: %s'
                       % sorted(impact_dims))
        if len(impact_hashes) != K.KEX_IMPACT_VISIBLE_FRAMES:
            out.append('impact-puff visual frames are not all distinct')
        if (not impact_opaque
                or impact_red * 10 <= impact_green * 17
                or impact_green * 10 <= impact_blue * 13):
            out.append('impact-puff art lost its authored orange color multiplier')

        for label, tile_base, artdir, dominant in (
                ('red-cobra', K.KEX_COBRA_RED_TILE_BASE,
                 'kexart/cobrared', 0),
                ('green-cobra', K.KEX_COBRA_GREEN_TILE_BASE,
                 'kexart/cobragreen', 1)):
            dims = set()
            hashes = set()
            totals = [0, 0, 0]
            opaque = 0
            for frame in range(K.KEX_COBRA_VISIBLE_FRAMES):
                tile = tile_base + frame
                rel = '%s/cobra_%d.png' % (artdir, frame)
                if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                                 % (tile, re.escape(rel)), deftext):
                    out.append('%s puff tile %d is absent from exhumed.def'
                               % (label, tile))
                    continue
                try:
                    blob = open(os.path.join(outdir, rel), 'rb').read()
                    w, h, pixels = _png.read(os.path.join(outdir, rel))
                    dims.add((w, h))
                    hashes.add(hashlib.sha256(blob).digest())
                    for p in range(0, len(pixels), 4):
                        if pixels[p+3]:
                            totals[0] += pixels[p]
                            totals[1] += pixels[p+1]
                            totals[2] += pixels[p+2]
                            opaque += 1
                except (OSError, ValueError, AssertionError, struct.error) as exc:
                    out.append('%s puff PNG is missing or invalid: %s'
                               % (label, exc))
            if len(dims) != 1:
                out.append('%s puff frames do not share one centred canvas: %s'
                           % (label, sorted(dims)))
            if len(hashes) != K.KEX_COBRA_VISIBLE_FRAMES:
                out.append('%s puff visual frames are not all distinct' % label)
            other = max(totals[(dominant + 1) % 3],
                        totals[(dominant + 2) % 3])
            if not opaque or totals[dominant] * 2 <= other * 3:
                out.append('%s puff art lost its authored color multiplier'
                           % label)

        anu_dims = set()
        anu_hashes = set()
        for frame in range(K.KEX_ANUBIS_IMPACT_VISIBLE_FRAMES):
            tile = K.KEX_ANUBIS_IMPACT_TILE_BASE + frame
            rel = 'kexart/anubisimpact/fireball_%d.png' % frame
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('Anubis-impact tile %d is absent from exhumed.def'
                           % tile)
                continue
            try:
                blob = open(os.path.join(outdir, rel), 'rb').read()
                w, h, _pixels = _png.read(os.path.join(outdir, rel))
                anu_dims.add((w, h))
                anu_hashes.add(hashlib.sha256(blob).digest())
            except (OSError, ValueError, AssertionError, struct.error) as exc:
                out.append('Anubis-impact PNG is missing or invalid: %s' % exc)
        if len(anu_dims) != 1:
            out.append('Anubis-impact frames do not share one centred canvas: %s'
                       % sorted(anu_dims))
        if len(anu_hashes) != 4:
            out.append('Anubis-impact authored hold groups changed from four: %d'
                       % len(anu_hashes))
        for tile in (7848, 7849):
            rel = 'kexart/seq/t%04d.png' % tile
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('private projectile tile %d is absent from exhumed.def' % tile)
            if not os.path.isfile(os.path.join(outdir, rel)):
                out.append('private projectile tile %d PNG is missing' % tile)
    except OSError:
        out.append('exhumed.def is missing for projectile impact-art audit')

    if K.KEX_SEQ_EXTRA_TILES.get(7848) \
            != ('amunbomb', 'projectiles/amun_bomb_idle', 0, 0):
        out.append('converter no longer maps tile 7848 to amun_bomb_idle')
    if K.KEX_SEQ_EXTRA_TILES.get(7849) \
            != ('boulder_shard', 'projectiles/boulder_shard', 0, 0):
        out.append('converter no longer maps tile 7849 to boulder_shard')
    return out


def verify_explosion_effects(z, outdir):
    """Shared Fx_Explosion01/Fx_Explosion01_Water source and native bridge."""
    out = []
    names = {n.lower(): n for n in z.namelist()}
    if (K.KEX_EXPLOSION_TILE_BASE != 21942
            or K.KEX_EXPLOSION_VISIBLE_FRAMES != 21
            or K.KEX_WATER_EXPLOSION_TILE_BASE != 21963
            or K.KEX_WATER_EXPLOSION_VISIBLE_FRAMES != 18
            or K.KEX_WATER_EXPLOSION_LIFETIME != 21):
        out.append('converter shared-explosion tile/lifetime contract changed')

    def read(rel):
        real = names.get(rel.lower())
        return z.read(real).decode('latin-1').replace('\r', '') if real else ''

    effects = read('defs/actors/effects.txt')
    for actor_name, anim, scale in (
            ('Fx_Explosion01', 'effects/explosion01', '4.0'),
            ('Fx_Explosion01_Water', 'effects/explosion01_water', '2.0')):
        match = re.search(r'Actor\s+' + re.escape(actor_name)
                          + r'\s*\{(.*?)\n\}', effects, re.S)
        compact = re.sub(r'\s+', '', match.group(1)) if match else ''
        for token in ('scale' + scale, 'spawnAnim"' + anim + '"',
                      'fullbright1'):
            if token not in compact:
                out.append('%s actor contract changed: %s'
                           % (actor_name, token))
        if 'animSpeed' in compact:
            out.append('%s no longer uses the default .5 animation speed'
                       % actor_name)

    light = (768.0, .75, .375, .25, 20.0, 10.0)
    land = K.parse_anim_full(z, names, 'effects/explosion01')
    water = K.parse_anim_full(z, names, 'effects/explosion01_water')
    if (len(land) != K.KEX_EXPLOSION_VISIBLE_FRAMES + 1
            or any(f['delay'] != 1 for f in land)
            or any(f.get('destroy') for f in land[:-1])
            or not land or not land[-1].get('destroy')
            or land[0].get('spawn_light') != light
            or any(not f.get('sprites') for f in land[:-1])):
        out.append('retail effects/explosion01 is no longer the exact '
                   '21-tick art/light contract')
    if (len(water) != K.KEX_WATER_EXPLOSION_LIFETIME + 1
            or any(f['delay'] != 1 for f in water)
            or any(f.get('destroy') for f in water[:-1])
            or not water or not water[-1].get('destroy')
            or water[0].get('spawn_light') != light
            or water[0].get('sprites')
            or any(not water[i].get('sprites') for i in range(1, 19))
            or any(water[i].get('sprites') for i in (19, 20, 21))):
        out.append('retail effects/explosion01_water is no longer the exact '
                   'blank/18-art/blank 21-tick contract')

    effect_anims = re.sub(r'\s+', '', read('sprites/effects/effects_anim.txt'))
    for anim, sound in (('explosion01', 'explode03'),
                        ('explosion01_water', 'explode02')):
        marker = anim + '{'
        start = effect_anims.find(marker)
        sound_at = effect_anims.find(
            'A_PlayLocalSound("sounds/%s.wav")' % sound, start)
        light_at = effect_anims.find(
            'A_SpawnLight(768,0.75,0.375,0.25,20,10)', start)
        if start < 0 or sound_at < start or light_at < sound_at:
            out.append('%s frame-zero sound/light callback order changed'
                       % anim)

    # These are all authored spawners of the shared actors. In particular,
    # Amun and Magic 8 Ball choose the water actor, while exploding vases do
    # not; all blast callbacks happen after the effect spawn.
    source_recipes = (
        ('sprites/projectiles/amun_bomb_anim.txt',
         ('A_Spawn("Fx_Explosion01",0,0,32)',
          'A_RadialBlast(512,140,1)',
          'A_Spawn("Fx_Explosion01_Water",0,0,32)')),
        ('sprites/misc/magic8ball_anim.txt',
         ('A_Spawn("Fx_Explosion01",0,0,32)',
          'A_Spawn("Fx_Explosion01_Water",0,0,32)',
          'A_RadialBlast(512,140,1)')),
        ('sprites/monsters/queen_snake_anim.txt',
         ('A_Spawn("Fx_Explosion01",0,0,8)',)),
        ('sprites/monsters/seedling_anim.txt',
         ('A_Spawn("Fx_Explosion01",0,0,32)',)),
        ('sprites/objects/vase_exp01_anim.txt',
         ('A_Spawn("Fx_Explosion01",0,0,32)',
          'A_RadialBlast(512,140,1)')),
        ('sprites/objects/vase02_exp_anim.txt',
         ('A_Spawn("Fx_Explosion01",0,0,32)',
          'A_RadialBlast(512,140,1)')),
        ('scripts/p_enemy.txt',
         ('actor.FireActor("Fx_Explosion01",x,y,z+192.0f)',)),
    )
    for rel, tokens in source_recipes:
        compact = re.sub(r'\s+', '', read(rel))
        for token in tokens:
            if token not in compact:
                out.append('%s explosion recipe changed: %s' % (rel, token))
    scripts = re.sub(r'\s+', '', read('scripts/p_enemy.txt'))
    if scripts.count('actor.FireActor("Fx_Explosion01",x,y,z+192.0f)') != 2:
        out.append('Selkis/Queen +192 explosion callback census changed')

    try:
        land_seq = S.read_seq(open(os.path.join(outdir, 'KEXEXPLODE.SEQ'),
                                   'rb').read())
        count = K.KEX_EXPLOSION_VISIBLE_FRAMES
        if (land_seq['seq_base'] != [0] or land_seq['seq_size'] != [count]
                or land_seq['fr_base'] != list(range(count))
                or land_seq['fr_size'] != [1] * count
                or land_seq['fr_flag'] != [4] * count
                or land_seq['ck_x'] != [0] * count
                or land_seq['ck_y'] != [0] * count
                or land_seq['ck_pict'] != [K.KEX_EXPLOSION_TILE_BASE + i
                                           for i in range(count)]
                or land_seq['ck_flag'] != [0] * count
                or land_seq['snd_names'] or land_seq['snd_events']):
            out.append('KEXEXPLODE.SEQ is not the exact 21-tick fullbright contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXEXPLODE.SEQ is missing or invalid: %s' % exc)

    try:
        water_seq = S.read_seq(open(os.path.join(outdir, 'KEXWATEREXP.SEQ'),
                                    'rb').read())
        count = K.KEX_WATER_EXPLOSION_LIFETIME
        expected_size = [0] + [1] * K.KEX_WATER_EXPLOSION_VISIBLE_FRAMES + [0, 0]
        expected_base = []
        cursor = 0
        for size in expected_size:
            expected_base.append(cursor)
            cursor += size
        if (water_seq['seq_base'] != [0] or water_seq['seq_size'] != [count]
                or water_seq['fr_base'] != expected_base
                or water_seq['fr_size'] != expected_size
                or water_seq['fr_flag'] != [4] * count
                or water_seq['ck_x'] != [0] * cursor
                or water_seq['ck_y'] != [0] * cursor
                or water_seq['ck_pict'] != [K.KEX_WATER_EXPLOSION_TILE_BASE + i
                                            for i in range(cursor)]
                or water_seq['ck_flag'] != [0] * cursor
                or water_seq['snd_names'] or water_seq['snd_events']):
            out.append('KEXWATEREXP.SEQ is not the exact blank/18-art/blank contract')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXWATEREXP.SEQ is missing or invalid: %s' % exc)

    try:
        sounds = open(os.path.join(outdir, 'kexsound.txt')).read()
        for alias, wav in (('bomb_exp', 'explode03'), ('bomb_wat', 'explode02')):
            if not re.search(r'^%s\s+sounds/%s\.wav\s+0$' % (alias, wav),
                             sounds, re.M):
                out.append('explosion sound alias is missing: %s' % alias)
    except OSError:
        out.append('kexsound.txt is missing for explosion-sound audit')

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    source_specs = {
        'sequence.cpp': ('"kexexplode"', '"kexwaterexp"'),
        'anims.cpp': ('KexExplosion01_Spawn',
                      'bWater?48:96', '"bomb_wat":"bomb_exp"',
                      'radius=768*4*6', 'fadeSteps=20'),
        'grenade.cpp': ('KexExplosion01_Spawn',
                        'exact21-tickland/watereffectsat+32'),
        'queen.cpp': ('sprite[nSprite].sectnum,32,0',
                      'sprite[nTail].sectnum,8,0',
                      'KexExplosion01_Spawn(x,y,z,nSector,192,0)'),
        'scorp.cpp': ('KexExplosion01_Spawn(x,y,z,sprite[nSprite].sectnum,192,0)',),
        'kexbreak.cpp': ('bMine&&bUnderwater', 'KexExplosion01_Spawn('),
    }
    native = {}
    for filename, needles in source_specs.items():
        try:
            compact = re.sub(r'\s+', '', open(os.path.join(srcdir, filename)).read())
            native[filename] = compact
        except OSError:
            out.append('cannot read native explosion bridge: %s' % filename)
            continue
        for needle in needles:
            if needle not in compact:
                out.append('%s no longer implements %s' % (filename, needle))
    grenade = native.get('grenade.cpp', '')
    start = grenade.find('voidExplodeGrenade(')
    spawn = grenade.find('KexExplosion01_Spawn(', start)
    radial = grenade.find('runlist_RadialDamageEnemy(', start)
    if start < 0 or spawn < start or radial < spawn:
        out.append('Amun death no longer spawns its effect before radial damage')
    breaking = native.get('kexbreak.cpp', '')
    # The file has a forward declaration before the vase-projectile helpers;
    # select the later definition so their own radial calls cannot confuse
    # this callback-order assertion.
    start = breaking.rfind('staticvoidKexBreak_Shatter(')
    spawn = breaking.find('KexExplosion01_Spawn(', start)
    radial = breaking.find('runlist_RadialDamageEnemy(', start)
    if start < 0 or spawn < start or radial < spawn:
        out.append('mine/vase death no longer spawns its effect before radial damage')

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
        import kexpng as _png
        art_specs = (
            ('land explosion', K.KEX_EXPLOSION_TILE_BASE,
             K.KEX_EXPLOSION_VISIBLE_FRAMES, 'kexart/explosion', 'explosion',
             land[:K.KEX_EXPLOSION_VISIBLE_FRAMES]),
            ('water explosion', K.KEX_WATER_EXPLOSION_TILE_BASE,
             K.KEX_WATER_EXPLOSION_VISIBLE_FRAMES,
             'kexart/waterexplosion', 'water', water[1:19]),
        )
        for label, tilebase, count, artdir, basename, source_frames in art_specs:
            dims, hashes = set(), set()
            opaque = 0
            for frame in range(count):
                tile = tilebase + frame
                rel = '%s/%s_%d.png' % (artdir, basename, frame)
                if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                                 % (tile, re.escape(rel)), deftext):
                    out.append('%s tile %d is absent from exhumed.def'
                               % (label, tile))
                    continue
                try:
                    path = os.path.join(outdir, rel)
                    blob = open(path, 'rb').read()
                    w, h, pixels = _png.read(path)
                    dims.add((w, h))
                    hashes.add(hashlib.sha256(blob).digest())
                    opaque += sum(1 for p in range(3, len(pixels), 4)
                                  if pixels[p])
                except (OSError, ValueError, AssertionError, struct.error) as exc:
                    out.append('%s PNG is missing or invalid: %s' % (label, exc))
            expected_unique = len(set(tuple(tuple(layer)
                                                for layer in f.get('sprites', ()))
                                      for f in source_frames))
            if len(dims) != 1:
                out.append('%s frames do not share one actor-centred canvas: %s'
                           % (label, sorted(dims)))
            if len(hashes) != expected_unique:
                out.append('%s visual pose count is %d, expected %d'
                           % (label, len(hashes), expected_unique))
            if not opaque:
                out.append('%s art is fully transparent' % label)
    except OSError:
        out.append('exhumed.def is missing for shared explosion-art audit')
    return out


def verify_water_splash_effect(z, outdir):
    """Fx_WaterSplash source, generated art and tossed-actor bridge."""
    out = []
    names = {n.lower(): n for n in z.namelist()}
    if (K.KEX_WATER_SPLASH_TILE_BASE != 22014
            or K.KEX_WATER_SPLASH_LIFETIME != 23):
        out.append('converter water-splash tile/lifetime contract changed')

    def read(rel):
        real = names.get(rel.lower())
        return z.read(real).decode('latin-1').replace('\r', '') if real else ''

    effects = read('defs/actors/effects.txt')
    match = re.search(r'Actor\s+Fx_WaterSplash\s*\{(.*?)\n\}',
                      effects, re.S)
    compact = re.sub(r'\s+', '', match.group(1)) if match else ''
    for token in ('classname"kexSlaveActor"', 'radius64', 'height64',
                  'stepHeight32', 'scale1.0',
                  'spawnAnim"effects/splash"', 'fullbright1'):
        if token not in compact:
            out.append('Fx_WaterSplash actor contract changed: %s' % token)
    if 'animSpeed' in compact:
        out.append('Fx_WaterSplash no longer uses the default .5 animation speed')

    frames = K.parse_anim_full(z, names, 'effects/splash')
    if (len(frames) != K.KEX_WATER_SPLASH_LIFETIME
            or any(f.get('delay') != 1 for f in frames)
            or any(f.get('destroy') for f in frames[:-1])
            or not frames or not frames[-1].get('destroy')
            or any(not f.get('sprites') for f in frames)
            or any(f.get('local_sound') for f in frames)):
        out.append('retail effects/splash is no longer the exact '
                   '23-tick fullbright surface actor')

    try:
        seq = S.read_seq(open(os.path.join(outdir, 'KEXWATERSPLASH.SEQ'),
                              'rb').read())
        count = K.KEX_WATER_SPLASH_LIFETIME
        if (seq['tag'] != b'IH' or seq['centerx'] != 0 or seq['centery'] != 0
                or seq['seq_base'] != [0] or seq['seq_size'] != [count]
                or seq['seq_flag'] != [-1]
                or seq['fr_base'] != list(range(count))
                or seq['fr_size'] != [1] * count
                or seq['fr_flag'] != [4] * count
                or seq['ck_x'] != [0] * count or seq['ck_y'] != [0] * count
                or seq['ck_pict'] != [K.KEX_WATER_SPLASH_TILE_BASE + i
                                      for i in range(count)]
                or seq['ck_flag'] != [0] * count
                or seq['snd_names'] or seq['snd_events'] or seq['trailing']):
            out.append('KEXWATERSPLASH.SEQ is not the exact 23-tick '
                       'fullbright surface actor')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('KEXWATERSPLASH.SEQ is missing or invalid: %s' % exc)

    try:
        deftext = open(os.path.join(outdir, 'exhumed.def')).read()
        import kexpng as _png
        dims, hashes = set(), set()
        opaque = 0
        for frame in range(K.KEX_WATER_SPLASH_LIFETIME):
            tile = K.KEX_WATER_SPLASH_TILE_BASE + frame
            rel = 'kexart/watersplash/splash_%d.png' % frame
            if not re.search(r'tilefromtexture\s+%d\s*\{\s*file\s+"%s"'
                             % (tile, re.escape(rel)), deftext):
                out.append('water-splash tile %d is absent from exhumed.def'
                           % tile)
                continue
            path = os.path.join(outdir, rel)
            blob = open(path, 'rb').read()
            w, h, pixels = _png.read(path)
            dims.add((w, h))
            hashes.add(hashlib.sha256(blob).digest())
            opaque += sum(1 for p in range(3, len(pixels), 4) if pixels[p])
        source_unique = len(set(tuple(tuple(layer) for layer in f['sprites'])
                                for f in frames))
        if len(dims) != 1:
            out.append('water-splash frames do not share one actor-centred '
                       'canvas: %s' % sorted(dims))
        if len(hashes) != source_unique:
            out.append('water-splash art pose census changed: %d, expected %d'
                       % (len(hashes), source_unique))
        if not opaque:
            out.append('water-splash art is fully transparent')
    except (OSError, ValueError, AssertionError, struct.error) as exc:
        out.append('water-splash PNG/DEF audit failed: %s' % exc)

    srcdir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src'))
    native = {}
    for filename in ('sequence.cpp', 'sequence.h', 'anims.cpp', 'anims.h',
                     'move.cpp', 'bullet.cpp'):
        try:
            native[filename] = re.sub(
                r'\s+', '', open(os.path.join(srcdir, filename)).read())
        except OSError as exc:
            out.append('cannot read native water-splash bridge %s: %s'
                       % (filename, exc))
    for needle in ('#definekMaxSEQFiles97', '"kexwatersplash"',
                   '!Bstrcasecmp(seqName,"kexwatersplash")'):
        if needle not in native.get('sequence.cpp', ''):
            out.append('optional water-splash sequence table changed: %s'
                       % needle)
    if 'kSeqKexWaterSplash' not in native.get('sequence.h', ''):
        out.append('optional water-splash sequence enum changed')
    for needle in ('intKexWaterSplash_Spawn(intx,inty,intz,shortnSector)',
                   'BuildAnim(-1,kSeqKexWaterSplash,0,x,y,z,nSector,24,4)',
                   'KexSound_Get("spl_big")', 'PlayFXAtXYZ(nSound,x,y,z,nSector)',
                   'voidKexWaterSplash_OnTransition(',
                   'SectBelow[nOldSector]==nNewSector',
                   'oldZ>getflorzofslope(nOldSector,oldX,oldY)',
                   'getceilzofslope(nNewSector,newX,newY)'):
        if needle not in native.get('anims.cpp', ''):
            out.append('native Fx_WaterSplash bridge changed: %s' % needle)
    if ('intKexWaterSplash_Spawn(intx,inty,intz,shortnSector)' not in native.get('anims.h', '')
            or 'voidKexWaterSplash_OnTransition(' not in native.get('anims.h', '')):
        out.append('native Fx_WaterSplash declaration changed')
    move = native.get('move.cpp', '')
    for needle in ('shortconstoldSector=sprite[nSprite].sectnum',
                   'intconstoldX=sprite[nSprite].x',
                   'intconstoldY=sprite[nSprite].y',
                   'intconstoldZ=sprite[nSprite].z',
                   'shortconstnewSector=sprite[nSprite].sectnum',
                   'KexWaterSplash_OnTransition(oldSector,oldX,oldY,oldZ,newSector,'):
        if needle not in move:
            out.append('native tossed-water transition changed: %s' % needle)
    bullet = native.get('bullet.cpp', '')
    for needle in ('shortconstnStartSector=pSprite->sectnum',
                   'KexBulletUsesRemasterDeath(nType)',
                   'KexWaterSplash_OnTransition(nStartSector,x,y,z,',
                   '&&!KexBulletUsesRemasterDeath(nType)'):
        if needle not in bullet:
            out.append('native projectile-water transition changed: %s' % needle)
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    kexdir, outdir = sys.argv[1], sys.argv[2]
    include_testmaps = '--testmaps' in sys.argv[3:]
    only = set(a.upper() for a in sys.argv[3:] if a != '--testmaps')
    findings = 0
    audited = 0

    def report(label, lines):
        """Print audit findings and make them affect the process result.

        This verifier is used as the conversion gate.  Reporting a broken
        portal, event, actor, or source bridge while returning zero lets a
        failed remaster conversion be installed or shipped as if it passed.
        """
        nonlocal findings
        for line in lines:
            print("%-12s   ! %s" % (label, line))
            findings += 1

    with zipfile.ZipFile(os.path.join(kexdir, K.KPF)) as z:
        checks = (
            ("DECOR", lambda: verify_decor(z, outdir)),
            ("BREAKABLE", lambda: verify_breakables(z, outdir, include_testmaps)),
            ("DROPS", lambda: verify_drop_effects(z, outdir)),
            ("GIBDEATH", lambda: verify_gib_death_effect(z, outdir)),
            ("RAMSES", lambda: verify_ramses(z, outdir)),
            ("MOVERS", lambda: verify_movers(z, outdir)),
            ("SEALED", lambda: verify_sealed_doors(z)),
            ("LOCKED", lambda: verify_locked_doors(z)),
            ("TELEPORT", lambda: verify_teleports(z, outdir, include_testmaps)),
            ("SHRINES", lambda: verify_shrines(z, outdir)),
            ("REMOTE", lambda: verify_remote_triggers(z, outdir, include_testmaps)),
            ("GROUND", verify_grounded_specials),
            ("HAZARD", lambda: verify_hazards(z)),
            ("FORCE", lambda: verify_forcefields(z, outdir)),
            ("FALL", verify_fall_damage),
            ("AIR", verify_air_supply),
            ("PLAYERDMG", verify_player_damage),
            ("PLAYERUSE", verify_player_use),
            ("TRAVEL", verify_player_travel),
            ("LIGHTFIELD", lambda: verify_light_fields(z, outdir, include_testmaps)),
            ("INTERPOLATION", verify_sprite_interpolation),
            ("LEVELTEX", lambda: verify_level_textures(z, include_testmaps)),
            ("SOUNDS", lambda: verify_sound_aliases(z, outdir)),
            ("TACTILE", lambda: verify_tactile(z)),
            ("MANACLE", lambda: verify_manacle(z)),
            ("CONTROLLER", verify_controller_defaults),
            ("ACTORPOOLS", verify_actor_pool_guards),
            ("INPUTCOMPAT", lambda: verify_input_compat(z)),
            ("GAMESETTINGS", verify_game_settings),
            ("DISPLAYSETTINGS", verify_display_settings),
            ("HUDSETTINGS", verify_hud_settings),
            ("CHECKPOINTSAVE", verify_checkpoint_saves),
            ("SAVEPATH", verify_named_save_state),
            ("STARTNOW", verify_startnow_roster),
            ("SNAPSHOT", verify_network_snapshot_state),
            ("LATEJOIN", verify_latejoin_protocol),
            ("PORTALRUNTIME", verify_directed_portal_runtime),
            ("CAMPAIGN", lambda: verify_campaign_metadata(z, kexdir, outdir)),
            ("INVENTORY", lambda: verify_inventory_metadata(z, outdir)),
            ("MENU", lambda: verify_menu_assets(z, outdir)),
            ("TERRAIN", lambda: verify_steep_terrain(z, outdir)),
            ("FLYERS", lambda: verify_flyers(z, outdir)),
            ("SENTRY", lambda: verify_sentry(z, outdir)),
            ("SET", lambda: verify_set(z, outdir)),
            ("QUEEN", lambda: verify_queen(z, outdir)),
            ("SELKIS", lambda: verify_selkis(z, outdir)),
            ("PROJECTILE", lambda: verify_projectiles(z, outdir)),
            ("EXPLOSION", lambda: verify_explosion_effects(z, outdir)),
            ("WATERSPLASH", lambda: verify_water_splash_effect(z, outdir)),
            ("COMMONAI", lambda: verify_common_enemies(z, outdir)),
            ("HEALTH", verify_health_units),
        )
        for label, check in checks:
            report(label, check())
        names = [n for n in z.namelist() if n.lower().startswith('maps/') and n.lower().endswith('.map')
                 and '/old/' not in n.lower()
                 and (include_testmaps or '/testmaps/' not in n.lower())]
        for n in sorted(names):
            base = os.path.splitext(os.path.basename(n))[0].upper()
            if only and base not in only:
                continue
            data = z.read(n)
            if data[:4] != b'PSM\x01':
                continue
            path = os.path.join(outdir, K.KEX_MAP_PREFIX + base + '.MAP') \
                if hasattr(K, 'KEX_MAP_PREFIX') else os.path.join(outdir, 'KEX_' + base + '.MAP')
            if not os.path.exists(path):
                report(base, ["source map was not converted"])
                continue
            m = K.locate(data)
            r = verify(m, path)
            print("%-12s %4d sectors %5d walls | z error p50 %4.0f p90 %5.0f p99 %5.0f max %5.0f  >128: %4d/%d | unpaired portals %d, zero-length first walls %d"
                  % (base, r['sectors'], r['walls'], r['p50'], r['p90'], r['p99'], r['max'], r['over'], r['samples'], r['unpaired'], r['zero_first']))
            report(base, audit(m, path, base, z, outdir))
            audited += 1
    print("audited maps: %d" % audited)
    print("findings: %d" % findings)
    return 1 if findings else 0


if __name__ == '__main__':
    raise SystemExit(main())
