<!-- Research artifact: the implementation-ready spec for compiling the
     remaster's sprite anims into Build .SEQ files, so the creatures whose
     art the shareware STUFF.DAT lacks (389 zero-size tiles: wasp, sentry,
     Set, queen, Selkis, bird) become visible in converted levels. Produced
     by a multi-agent research pass over sequence.cpp, the demo's SEQ/ART
     data and the remaster's anim scripts; the format writer round-tripped
     byte-identical on ANUBIS/MUMMY/WASP.SEQ. Kept current as implementation
     evidence and a ledger of remaining approximations. -->

# Compiling remaster sprite anims into Build .SEQ files — implementation spec

Current result: `kexmap.py` fills the compatible stock sequence tiles and
emits loose exact/retargeted sequences (`WASP`, `HAWK`, `SEEDLING`,
`SCORPION`, `MANACLE`, plus patched demo sequences) next to converted maps.
They override STUFF.DAT through the mod search path. Small KEX-only controller
branches preserve authored identities, callbacks and timings where the stock
state machines cannot express them; classic behavior remains isolated.

## 1. The .SEQ format — authoritative writer spec

Source of truth: `source/exhumed/src/sequence.cpp` `seq_ReadSequence()` (lines 164–352; `seq_LoadSequences()` at 364 calls it per name in `SeqNames[]`), render math in `seq_DrawGunSequence` (478), `seq_PlotArrowSequence` (557), `seq_PlotSequence` (605), `overwritesprite` (`enginesubs.cpp:32`). Verified against the demo GRP: reader + writer round-trip **byte-identical** on `ANUBIS.SEQ` (13564 B), `MUMMY.SEQ` (28774 B), `WASP.SEQ` (3480 B).

### 1.1 File layout (all values little-endian `int16`; no padding, no alignment)

```
offset  size          field
0       2             tag: multichar const 'HI' (0x4849) or 'SD' (0x5344).
                      On disk the bytes therefore read "IH" or "DS".
                      'SD' means a sound section follows the chunk arrays;
                      'HI' means the file ends after them.
                      (Loader check at sequence.cpp:186 accepts tag=='HI' or tag=='SD'.)
2       2             centerx   (anchor x on the authoring canvas)
4       2             centery   (anchor y)
6       2             nSeqs     (must be > 0)
8       2*nSeqs       SeqBase[]  first-frame index of each sequence (file-local, 0-based)
        2*nSeqs       SeqSize[]  frame COUNT of each sequence
        2*nSeqs       SeqFlag[]  stored into memory but NEVER read by the engine — write 0
        2             nFrames   (must be > 0)
        2*nFrames     FrameBase[] first-chunk index of each frame (file-local, 0-based)
        2*nFrames     FrameSize[] chunk COUNT of each frame
        2*nFrames     FrameFlag[] per-frame flags (§1.3)
        2             nChunks   (may be 0; each frame with FrameSize 0 draws nothing)
        2*nChunks     ChunkXpos[] tile-CENTER x on the canvas (§1.5)
        2*nChunks     ChunkYpos[] tile-CENTER y on the canvas
        2*nChunks     ChunkPict[] ART tile number
        2*nChunks     ChunkFlag[] bit 1 = x-flip, bit 2 = y-flip (§1.4)
--- only if tag == 'SD': ---
        2             nSoundNames
        8*nSoundNames sound names: EXACTLY 8 bytes each, ASCII, NUL- or space-
                      padded, NO extension, NO terminator required if 8 chars
                      (e.g. b"ANU_DIED", b"DRUM2\0\0\0"). Loader reads 8 bytes into a
                      10-byte-stride buffer (line 325); LoadSound compares/copies
                      with kMaxSoundNameLen==8 (sound.h:29) and strips trailing spaces.
        2             nSoundEvents
        4*nSoundEvents events: pairs (int16 frame, int16 code)
                      frame = FILE-GLOBAL frame index (0..nFrames-1, across ALL
                              sequences in this file — loader does FrameSound[vdi+frame],
                              vdi = base of this file's frames, line 346)
                      code  = soundNameIndex (bits 0–8, & 0x1FF)
                              | flag bits in 0xFE00 (§1.6)
EOF (no trailer, no checksum)
```

Arrays are struct-of-arrays, each written contiguously in the order above. Frames of a sequence and chunks of a frame must be contiguous and in order (Base + Size addressing; the original files are exactly packed `[0,size0), [size0,...)` with no gaps — write them packed). Multiple chunks per frame are drawn in array order.

### 1.2 File vs memory

The loader concatenates every .SEQ into global arrays and relocates:
- `SeqBase[i] += frames` (running global frame count), `FrameBase[i] += chunks`, sentinel appended to `FrameBase` (line 310) — none of this is in the file; file indices are file-local 0-based.
- `ChunkXpos[i] -= centerx; ChunkYpos[i] -= centery` (lines 299–300) — **in memory chunk coords are anchor-relative; in the file they are absolute canvas positions.**
- `FrameSound[]` is memset to −1, then each SD event stores `LoadSound(name) | (code & 0xFE00)`.
- Every `ChunkPict` tile is `tileLoad()`ed at load time — every tile referenced must exist (nonzero size) or you get invisible/garbage chunks. **Verified: DEF loading (exhumed.cpp:2556) precedes seq_LoadSequences (exhumed.cpp:2577), so `tilefromtexture` tiles are valid chunk targets.**
- Limits (global, across all 78 files): 4096 sequences, 18000 frames, 21000 chunks — the stock data uses most of this; a replacement SEQ that preserves the stock sub-seq layout replaces its counts roughly 1:1 and cannot balloon.

### 1.3 FrameFlag bits (per frame)

- `0x4` — fullbright: renderer forces shade −100 (sequence.cpp:505, 581, 629; gun.cpp:720 skips underwater except the Ring).
- `0x80` — "action fires now" trigger: sampled via `seq_GetFrameFlag`/`MoveSequence` return — weapon fires (gun.cpp:731), melee damage / projectile launch (mummy.cpp:372,390; spider.cpp:305; roach.cpp:394; queen.cpp:1200/1346; scorp.cpp:351,379). Note the roach samples it against the *post-increment* frame (roach.cpp:394 after `field_2++`) — keep trigger flags on the exact demo frame positions.
- All other bits: never tested anywhere (set.cpp checks `0x10` for a quake footstep in action 3, but no stock data sets it). Write 0.

### 1.4 ChunkFlag bits (per chunk)

- bit `0x1` — x-flip. 3D: `tspr->cstat |= 4` and `xoffset = +ChunkXpos` instead of `-ChunkXpos` (sequence.cpp:589–597, 674–682 — pre-negated because Build negates xoffset for x-flipped sprites). 2D gun: `nStat=11` → `overwritesprite` x-flip.
- bit `0x2` — y-flip: honored only by the 2D gun/status path (line 499); **ignored by the 3D sprite paths** — don't rely on it for monsters. (The wasp death tumble uses it in stock data; harmless to copy but expect no visual effect.)

### 1.5 Coordinate convention (ChunkXpos/ChunkYpos)

File values are the position of the **tile's center** on an authoring canvas whose point `(centerx, centery)` is the sprite anchor:

- 2D proof: gun draw computes `x = ChunkXpos_mem + 160, y = ChunkYpos_mem + 100` → `overwritesprite` center mode subtracts `tilesiz>>1` (+ picanm ofs) so `(x,y)` is where the tile **center** lands (enginesubs.cpp:44–51).
- 3D proof: `tsp->xoffset = -ChunkXpos_mem` (or `+` if flipped), `tsp->yoffset = -ChunkYpos_mem` (sequence.cpp:674–684); engine adds these to picanm offsets (engine.cpp:5563–5569); positive offset moves the image up/left — the chunk's tile center is displaced `(+ChunkXpos_mem, +ChunkYpos_mem)` from the sprite anchor in screen coords (**+x right, +y down**).
- Practically: character's ground-contact/anchor at `(centerx, centery)`; per chunk, store the canvas coordinates of its tile's center. ANUBIS: center=(159,156), walk chunk y≈89–98 → tile centers ~60 px above the feet anchor. `tspr->xoffset/yoffset` are `int8_t`, so keep `|value − center| ≤ 127` per axis.

### 1.6 Sound event code bits (kept bits: `0xFE00`)

`D3PlayFX` (sound.cpp:1173–1177): bits `0xE00` = 3-bit random pitch-jitter magnitude, `0x1000` = force-play/unique slot, `0x2000` = allow re-trigger on same sprite (without it an identical already-playing sound on that sprite is skipped, line 1211), `0x4000` = looping (line 854). Stock: `0x2000|idx` for repeated footsteps, `0x2600`/`0x200`/`0x400`/`0x800` pitch variants; plain `idx` for one-shots. Never set `0x8000` (negative short = "no sound").

### 1.7 Tile offsets: picanm vs chunk offsets, and DEF

Engine sums `picanm[tile].xofs/yofs` **plus** the SEQ `tspr->xoffset/yoffset` (engine.cpp:5567). For compiled tiles: **leave picanm offsets 0 and express all placement through chunk coords** — nonzero picanm double-shifts. (`tilefromtexture N { file "..." xoffset a yoffset b }` — defs.cpp:899–936, clamped −128..127, stored at defs.cpp:981,1009 — could bake `xofs=-ChunkXpos_mem, yofs=-ChunkYpos_mem`, but cannot emulate per-chunk flips or the x-flip pre-negation; the chunk route is strictly more faithful.)

### 1.8 Verified writer (round-trip proven on ANUBIS/MUMMY/WASP; scripts at scratchpad `seqtool.py`, `writeseq.py`)

```python
import struct

def write_seq(path, seq_file):
    """Write a PowerSlave/Exhumed .SEQ file.

    seq_file = {
      "center": (cx, cy),        # anchor point on the authoring canvas
      "sequences": [             # e.g. 8 view-angle sequences per action
        { "flag": int,           # int16, stored but never read by the engine -> 0
          "frames": [
            { "flag": int,       # 0x4 = fullbright, 0x80 = action trigger
              "sound": None,     # or (name_str_max8, flagbits) with flagbits in 0xFE00:
                                 #   0xE00 pitch jitter, 0x1000 force, 0x2000 retrigger,
                                 #   0x4000 loop
              "chunks": [        # tiles composing this frame, drawn in order
                { "x": int,      # tile-CENTER pos relative to anchor, +x right
                  "y": int,      # +y down; keep |x|,|y| <= 127 (int8 tspr offsets)
                  "tile": int,   # ART tile number (must exist, nonzero size)
                  "flags": int}  # 1 = x-flip, 2 = y-flip (2D-only)
              ]}]}],
      "sound_names": None,       # optional explicit SD name-table order;
                                 # default sorted(set) matches the original tool
    }
    """
    cx, cy = seq_file["center"]
    seq_base, seq_size, seq_flag = [], [], []
    fr_base, fr_size, fr_flag = [], [], []
    ck_x, ck_y, ck_pict, ck_flag = [], [], [], []
    events = []                       # (file-global frame index, name, flagbits)
    for seq in seq_file["sequences"]:
        seq_base.append(len(fr_base))
        seq_size.append(len(seq["frames"]))
        seq_flag.append(seq.get("flag", 0))
        for fr in seq["frames"]:
            if fr.get("sound") is not None:
                name, bits = fr["sound"]
                events.append((len(fr_base), name, bits))
            fr_base.append(len(ck_x))
            fr_size.append(len(fr["chunks"]))
            fr_flag.append(fr.get("flag", 0))
            for ck in fr["chunks"]:
                ck_x.append(ck["x"] + cx)
                ck_y.append(ck["y"] + cy)
                ck_pict.append(ck["tile"])
                ck_flag.append(ck.get("flags", 0))
    names = seq_file.get("sound_names")
    if names is None:
        names = sorted({e[1] for e in events})
    out = bytearray()
    out += b"DS" if events or names else b"IH"   # int16 tag 'SD' / 'HI', little-endian
    out += struct.pack("<3h", cx, cy, len(seq_base))
    for arr in (seq_base, seq_size, seq_flag):
        out += struct.pack("<%dh" % len(arr), *arr)
    out += struct.pack("<h", len(fr_base))
    for arr in (fr_base, fr_size, fr_flag):
        out += struct.pack("<%dh" % len(arr), *arr)
    out += struct.pack("<h", len(ck_x))
    for arr in (ck_x, ck_y, ck_pict, ck_flag):
        out += struct.pack("<%dh" % len(arr), *arr)
    if events or names:
        out += struct.pack("<h", len(names))
        for nm in names:
            b = nm.encode("ascii") if isinstance(nm, str) else nm
            assert len(b) <= 8, nm
            out += b.ljust(8, b"\x00")
        out += struct.pack("<h", len(events))
        for frame, name, bits in events:
            idx = names.index(name)
            assert 0 <= idx < 512 and (bits & 0x1FF) == 0
            out += struct.pack("<hh", frame, idx | bits)
    with open(path, "wb") as f:
        f.write(bytes(out))
    return bytes(out)
```

### 1.9 Semantics the game relies on (writer gotchas)

- `SeqSize` is the AI's animation length (`nFrame >= SeqSize[nSeq]` everywhere) — must equal `len(frames)`. Frame counts of rotations 1–7 are cosmetically free (demo SCORP seq 10 has 7 frames vs 8 and merely over-reads into seq 11); only the base sub-seq's size drives the state machine — but write all 8 equal anyway.
- Monsters index sub-seqs as `SeqOffsets[file] + N` with N hardcoded per creature — **the per-file sub-seq order/count is an engine-side contract**; every replacement must reproduce the stock layout including empty separator sub-seqs.
- **Rotation encoding**: an 8-view group is 8 *consecutive* sub-seqs; `seq_PlotSequence` picks `val = ((sprite.ang − angleToCamera + 128) & 2047) >> 8` and draws `SeqBase[base + val]`; `ActionSeq[].b & 1` forces `val=0` (single view). val=0 = sprite **facing the camera** (front), +4 = back. Stock data draws 5 unique views (0–4) and mirrors 5–7 from 3–1 via ChunkFlag x-flip — identical to the kex convention (SW/W/WN reuse ES/E/NE cells with flip=1), so **Build dir k ↔ kex rotation block k** (fixed kex file order N, NE, E, ES, S, SW, W, WN). kexmap.py's `build_angle` (line 1381) is slope-+1 affine, so angular differences transfer 1:1.
- **Frame flags and sounds are read from the dir-0 sub-seq only** (AI passes `ActionSeq[].a` un-rotated to `seq_GetFrameFlag`/`seq_MoveSequence`). Emit triggers/sounds on dir 0; duplicating across dirs 1–7 like the stock data is harmless.
- `nSeqs`, `nFrames` ≥ 1 (loader `<= 0` checks at 204, 239); `nChunks` = 0 and per-seq/per-frame size 0 are legal.
- No sounds → tag `'HI'`, stop after chunk arrays. Sound-event frame indices are file-global; emit ascending (the writer's traversal order guarantees it).

## 2. Per-creature mapping

Common notation: "N+d" = sub-seq index; dir d=0..7 ↔ kex rotation N,NE,E,ES,S,SW,W,WN; dirs 5–7 = x-flipped (ChunkFlag 1) reuse of dirs 3–1 tile content.

### 2.1 WASP — `WASP.SEQ` (kSeqWasp=22, SeqNames[22], sequence.cpp:100/sequence.h:52)

The demo fallback remains a 31-slot, 229-frame file driven by native
`ActionSeq`, but KEX conversions now override it with `kexwaspseq.py`: a
36-slot, 137-frame/chunk sequence compiled directly from `wasp_base`,
`wasp_chase`, and `wasp_attack`. Fifteen distinct movement sprites occupy the
private contiguous range 7870-7884; each frame stores the retail crop centre,
so wing and sting poses retain a fixed actor origin. `WaspActionSeq` is
selected only when slot 9 is eight frames and slot 27 is four, leaving stock
data and non-KEX play on the original contract.

| Exact sub-seq group | frames/view | retail source | callback contract |
|---|---:|---|---|
| 0+d base | 4 | `wasp_base` | no chase cue |
| 9+d attack | 8 | `wasp_attack` | `waspatk` + `A_MeleeAttack(16,20)` f0; `wasphit` f3; backoff callback f7 |
| 18+d backoff | 1 | `wasp_attack` f7 | move backward at 20 authored units/tick, then chase |
| 27+d chase | 4 | `wasp_chase` | `waspact` f0; four authored `A_WaspBob` callback positions |
| 8/17/26 | 0 | separators | required directional layout padding |
| 35 | 1 | safe terminal pose | unreachable after immediate KEX gib death |

The sound table is exactly `[WASP_BUZ, WASP_STG, WASP_HIT]`, with events at
chase f0, attack f0 and attack f3. `KEX_SOUNDS` maps those names to
`waspact.wav`, `waspatk.wav` and `wasphit.wav`. Runtime damage is synchronized
to attack frame 0; attack frame 7 enters the one-tick backoff group. KEX stats
come from `AI_Wasp`: health 60, damage 20, radius 18, scale 1.25, speed 16,
height 64 and `painChance 0`. Death immediately tosses the retail 1/1/1/3/2
gib pattern on tiles 7823-7827 and randomly chooses `waspdeath01/02`.

`A_WaspBob` is not a base/idle motion. All four `wasp_chase` frames call it;
base and attack contain none. With no target it writes
`cos((GameTicks/32)%360) * 2`; with a target it returns without changing Z when
the source height delta is outside +/-128 or the full origin-to-origin 3D
distance is at most 192. The native exact-Wasp path retains a 60 Hz source
clock and vertical velocity in save-safe controller fields. KEX's default `.5`
animation speed invokes each authored chase callback on the first of two 60 Hz
source steps per 30 Hz Exhumed tick; the second preserves that velocity while
AI_Wasp's .94 vertical friction is applied, and both displacements are summed
after converting KEX up-positive Z to Build down-positive Z. It has a yaw-only
pursuit helper so the old pitch-in-`sprite.zvel` chase path cannot overwrite
the callback. `VB_KEX_WASP_BOB_TEST=1` exercises the exact
phase, friction, bounds and sign; `kexverify.py` audits both source and native
contracts.

### 2.2 BIRD — dedicated `HAWK.SEQ` identity on the Wasp run record

`AI_Bird` still uses the compact native Wasp record for runlist/save storage,
but `s_waspKind` and the mirrored `sprite.hitag` now preserve a distinct bird
identity. KEX birds select `BirdActionSeq` and `kSeqHawk`; classic records and
missing optional data retain their native fallback. Save reconstruction also
recognizes the private bird tile range.

`kexbirdseq.py` is an exact 36-slot, 217-frame/chunk file covering all 36
retail source sprites on tiles 7600-7635. It uses standard N..WN directional
slots and KEX-origin-preserving chunk anchors, replacing the stock HAWK file's
rotated and mostly zero-size layout.

| Exact sub-seq group | frames/view | retail source | callback contract |
|---|---:|---|---|
| 0+d calm | 1 | `bird_fly_calm` | calm wander controller |
| 9+d attack | 8 | `bird_attack` | seven visual frames plus authored `delay 8`; attack sound and melee f0 |
| 18+d retreat | 12 | `bird_fly_retreat` | upward impulse f0, turning retreat f1-f11 |
| 27+d chase | 6 | `bird_fly_chase` | flap01 f1, flap02 f4 |
| 35 | 1 | `bird_feather` | art census slot; the live loop is `KEXBIRDFEATHER.SEQ` |

The sound table is `[BIRD_WNG, BIRD_WN2, BIRD_ATK]`. Authored stats are
health 10, damage 20, radius 16, scale 1.5/repeat 60, speed 8, height 128,
fullbright and no infighting. Calm motion is two units/tick with 1.5-degree
steering; pursuit is eight units/tick. The exact bridge keeps each Bird's
60 Hz `GameTicks` and fractional Build-angle remainder in unused KEX-bird-only
controller fields: `A_BirdWander` writes two-unit XY velocity on the `.5`
animation edge and that zero-friction velocity moves on both source physics
ticks. `A_BirdRetreat` similarly writes its four-unit turn/vector before both
steps, then ends precisely when the script's vertical-velocity or
ceiling-minus-128-height test succeeds rather than on a native timer.
`VB_KEX_BIRD_FLIGHT_TEST=1` checks the source clock, fractional turns and
retreat boundary. The 32-unit melee callback occurs on attack frame 0, then
the bird rises and turns through the twelve-tick retreat. Death plays
`birddeath.wav`, creates seven `Obj_Bird_Feathers` actors with the source's
centred spawn range, and removes the host immediately. The optional
`KEXBIRDFEATHER.SEQ` holds the single retail feather tile (7632) through the
15 source crops; the native KEX-only bridge randomizes its initial frame,
loops it, writes the downward half-unit velocity on f8/f14, and removes it at
the source's 1.024-unit floor gate. `VB_KEX_BIRD_FEATHER_TEST=1` probes that
sequence/fall boundary without creating visible gameplay actors.

### 2.3 SENTRY — `ROACH.SEQ` (kSeqRoach, SeqNames[50], sequence.cpp:131/sequence.h:80)

The native fallback remains the demo controller and file: lotag 104 starts
dormant in action 0, lotag 103 starts alert in action 1, and `ActionSeq` uses
groups 24/0/0/16/8/32/42 for dormant, alert, chase, attack, pain, death and
corpse. That 46-slot file has 707 frames and 849 chunks and is still selected
outside KEX mode or whenever the exact loose-file signature is absent.

Retail `AI_Sentry` has health 180, radius 48, height 192, step height 48,
scale 1.5, speed 8, turn speed 2, pain chance 24 and `noDropOff 1`. Its exact
animation contract is idle 1 frame, chase 8 (`A_SentryChatter` at frame 0),
pain 12 (pain sound frame 0 and `goto sentry_chase` at frame 10), and attack
10 (`sentryfire`, fullbright and `A_FireProjectile("Proj_Blaster",0,0,4,96,24)`
at frame 6). Death is immediate and tosses nine authored gibs in the 2/1/1/2/3
pattern before destroying the actor. `Proj_Blaster` has damage 30 and radius
36 and is represented by native bullet type 13 with dedicated remaster art.

`kexseqmap_gen.py` emits the exact KEX file as 36 slots and 249 frames/chunks:

| Exact sub-seq group | frames/view | controller action | callback contract |
|---|---:|---|---|
| 0–7 | 1 | dormant + alert idle | exact idle pose |
| 9–16 | 8 | chase | authored movement cadence; chatter is controller-side |
| 18–25 | 12 | pain | `ROCH_WLD` frame 0; controller exits after rendered frame 10 |
| 27–34 | 10 | attack | `ROCH_GUN`, projectile and fullbright on frame 6 |
| 8,17,26 | 0 | separators | preserve eight-direction grouping |
| 35 | 1 | safe terminal | immediate KEX death bypasses corpse animation |

All 71 distinct layered composites, including the asymmetric pain rotations,
are pre-rendered for all eight views on tiles 7890–7960. Consequently every
chunk flip flag is zero: applying the stock west-view mirror would flip the
already composed layers a second time. `SentryActionSeq` maps actions
0/1/2/3/4/5/6 to slots 0/0/9/27/18/35/35. The controller recognizes the
exact file by slot 0 having one frame and slot 27 having ten; otherwise it
uses the classic action table.

KEX `BuildRoach` converts the retail dimensions to clipdist 72 and repeat 60,
uses speed 8 and incremental turn-speed-2 steering, plays `sentryact01.wav`
on acquisition, and retains chatter/pain/fire/death cues. The env-only
`VB_KEX_SENTRY_TEST=1` probe forces one attack and one pain cycle and verifies
the 1/8/12/10 cadence, attack frame 6, pain exit frame 10, fullbright flag,
stats and movement speed. `VB_KEX_SENTRY_AUDIT=1` logs the first live Sentry's
resolved values and whether the exact sequence was detected.

### 2.4 MANTIS — stock `LAVAG.SEQ` attack slot 10; lotag 108

KEX `AI_Mantis` deliberately reuses the pixel-identical native Lava art, so it
does not need another generated SEQ. The retail actor contract is health 400,
radius 48 / Build clipdist 72, height 384, scale 3 / repeat 120, move speed 64,
pain chance 16 and no in-fighting. Stock `LAVAG.SEQ` slot 10 already contains
the required source frames 0–12.

The native KEX controller supplies the timing that a static SEQ cannot express:
three one-tick common frames, then either a rare 1/128 PSX branch or the usual
Saturn branch. PSX renders frames 4–12 over nine ticks and calls the custom
`FireActor` bridge on frames 5–9 for five independent yaw/pitch-spread shots at
continuous speed 8–16. Saturn renders the exact 13-tick hold map
`4,5,5,5,5,10,10,10,10,11,11,11,11`; its frame-5 callback is
`A_FireProjectile("Proj_Fireball",0,0,8,192,16)`. The script's initial
`1 + Size(3)` and check-before-decrement ordering produce two through nine
volleys. KEX-only mode/count/timeline bits are packed into `Lava.nFrame`, so
the original raw save structure does not grow.

The rest of the retail state machine also uses the stock art verbatim. Hidden
chase runs `A_MantisThink` every four ticks after the generic two-tick target
look; the callback has its authored byte gate (`<= 32`), 2000-unit radial LOS,
clamped `Size(3)`, and persistent scratch0/scratch1 peek-vs-rise decision.
`mantis_peak` is slot 0 for 62 ticks, with show/hide and sounds at frames
0/2/24/37/61. `mantis_rise` is slot 1 for 60 ticks, faces/shows/quakes on frame
0, plays its sounds at 4/12/14, becomes solid and shootable at 31, and enters
the attack after 59. Pain, submerge, and splash retain their exact 15/24/21
timelines; blocking clears on pain frame 0 or submerge frame 3, and splash
hides on frame 20. The two mutually exclusive wake counters share three more
packed `nFrame` bits, still without changing the save record.

The 54-frame death is composed from the same stock views: pain frames 0–8,
submerge 0–23, then splash 0–19; source frame 53 hides and destroys before it
is drawn. Frame 0 fires the tagged event, death sound, unblock, and first
`A_MantisExplode`; frames 5 and 8 repeat the explosion, for the retail three
bursts of twenty Ra-fire orbs. The splash sounds remain on death frames 37 and
43.

`VB_KEX_MANTIS_TEST=1` forces and validates the 62-tick peek, 60-tick rise,
three-tick prefix, minimum two Saturn volleys (26 ticks), five-shot PSX branch
(nine ticks), 24+21 submerge/splash path, and 53-tick destroy transition. It
checks every immediate projectile, callback mask, tagged event, and all three
death bursts without leaving test projectiles in the map.

### 2.5 SET — `SET.SEQ` (kSeqSet=48, sequence.h:78; lotag 111, init.cpp:871 → BuildSet)

Set's projectile (bullet 11, `BulletInfo[11]={30,-1,1000,-1, nSeq=52 kSeqSetGhost, field_C=53 kSeqSetGBlow, 4,20,48}`, bullet.cpp:94) uses SETGHOST/SETGBLOW.SEQ whose tiles (2681–2686, 3037, 3290–3295) all exist — not part of the problem. Demo SET.SEQ: 'SD', center (160,177), **81 sub-seqs, 1270 frames, 1305 chunks**, 12 sound names, 104 events. Zero tiles: 2559–2642 and 2687 (85 tiles — every body tile). Present: 2467 (35×28), 2678 (47×88 soul), 3034–3036, 3291–3292. Sprite repeat 87×96. State flow: spawn→1 dormant→2 wake→7 leap→chase 3 with random 7/6; 4 melee; 6 fire souls; 8/9 air/land; 10→11 death. Action 5 (stun) is never entered.

Kex: `AI_Set` — radius 96, height 192, stepHeight 48, scale 2.0,
health 3000, movement speed 10, painChance 24, noEnterWater and
noInFighting. Its animations are set_idle 1×8, set_chase 20×8
(`A_SetThink`; setstomp02 f5/f15), set_melee 18×8 (40-damage hits f8/f17),
set_leap_start 13×8 (setjump f9, launch f12), set_leap_loop 1×8,
set_leap_land 12×8 (landing/quake f6), set_fire_souls 15×8 (one
`Proj_Soul` f7, fullbright f9), set_pain 8×8, set_death 30×1 and
set_death_loop 1×1.

The generated exact file has **76 slots, 737 frames and 737 chunks**. Every
direction is precomposited because Set's layered source rotations are not
mirror-equivalent; 171 unique images occupy tiles 20000–20170 and every chunk
flip is zero.

| exact slots | frames | action / callback contract |
|---|---:|---|
| 0–7 | 1 | directional idle; actions 0/1/2 compatibility pose |
| 9–16 | 20 | chase; `A_SetThink` every frame, stomp f5/f15 |
| 18–25 | 18 | melee; `0x80` and 40-damage callbacks on f8/f17 |
| 27–34 | 13 | leap start; jump sound f9, launch after rendered f12 |
| 36–43 | 1 | airborne loop; ground check every tick |
| 45–52 | 12 | landing; land sound, quake and grounded-player hit on f6 |
| 54–61 | 15 | soul cast; attack cue f6, one soul/`0x80` f7, fullbright f9 |
| 63–70 | 8 | pain; pain cue f3, action cue f7, return to chase |
| 72 | 30 | death; authored cue/quake schedule, remote tag 1010 on f29 |
| 73 | 1 | persistent death loop |
| 74 | 1 | native soul art tile 2678 for `BuildSoul` compatibility |
| 75 | 1 | safe terminal pose, `SeqFlag=-1` |
| 8/17/26/35/44/53/62/71 | 0 | separators |

Exact sound events are chase f5/f15 `K_SETSTP`; melee f0 `SET_STEP` and f9
`SET_HIT`; leap f9 `SET_JUMP`; land f6 `SET_LAND`; cast f6 `SET_GR2` and f7
`GHOST109`; pain f3 `SET_HIT` and f7 `SET_GR1`; death f0/f10 `SET_HIT`,
f2/f7/f12/f17 `SET_SPRT`, f18 `RA_SPRIT`, and f28 `SET_DETH`. All fixed-width
sound names are padded to eight bytes by the generator and resolve through
`kexsound.txt`.

`set.cpp` detects the exact cadence and selects a dedicated action table; if
the file is absent, it retains the stock 81-slot table and
`patch_demo_seqs()` fallback. `A_SetThink` uses the retail masked 1-in-16
decision cadence, checks souls below 1500 health with a coin flip before
checking leap below 2000 with a quarter roll, and computes speed from logical
health. KEX health is stored as `KexHealthUnits(3000)`. Death suppresses the
DOS twenty-soul recipe, sends tag 1010 after f29 and holds the corpse. The
env-only `VB_KEX_SET_TEST=1` probe forces melee/cast/pain/leap and verifies all
cadences, callback/fullbright frames, launch, dimensions, logical/internal
health and full-health speed; `VB_KEX_SET_AUDIT=1` logs live resolved values.

### 2.6 QUEEN — `QUEEN.SEQ` (kSeqQueen=49) + `QUEENEGG.SEQ` (kSeqQueenEgg=56); lotag 113 (init.cpp:861 → BuildQueen, statnum 121)

The retail path is now exact. `kexqueenseq.py` emits a 104-slot,
1002-frame/chunk `QUEEN.SEQ` and a two-action, three-frame/chunk
`QUEENEGG.SEQ`. All 280 nonempty authored composites are precomposited with
all eight rotations on tiles 20400–20679; callback-only empty frames carry
safe hidden art, and no chunk flip is applied. The exact groups are idle
1×8; base/chase/charge/after-charge 10×8 each; flap 11×8; hurl 30×8; tail
missile 16×8; pain 13×1; death 62×1; snake idle/chase/melee
1×8/5×8/1×8; snake explosion 85 rendered ticks; and tail 1×1. The seed pod
holds spawn frame 0 for exactly 60 ticks, then executes its spriteless hatch
callback. Both files are installed after `patch_demo_seqs()`.

The native exact controller recognizes that signature and keeps the demo
tables below only as fallback. It uses one 3600-point body pool (clip 96,
repeat 60), the scripted high-health strafe/tail decisions, the 2400-point
pain transition, low-health hurl/charge decisions, 50-damage flap, exact
type-12 `FireActor` launch, 40-damage charge and knockback, and all authored
callback/sound frames. The body death spawns the six Queen gibs and the
200-health snake. Ten non-solid tail links each have an independent
200-health gate: only the last is shootable, each death exposes its parent,
and the head becomes shootable only after all ten are gone. Joints lerp 0.2
toward their parent every tick. Snake melee deals 30 with retreat, and death
runs 21 explosions over 85 ticks before firing the exit event once. The
env-only `VB_KEX_QUEEN_TEST=1` probe verifies this complete lifecycle;
`VB_KEX_QUEEN_AUDIT=1` logs live map stats.

The ten-link runtime does not enlarge the original save record. The historical
seven-short `tailspr` field remains byte-for-byte fixed; the three exact-only
sprite ids are packed behind a marker in four `MoveQS` slots that the exact
joint controller does not use. An unmarked KEX save is migrated from the older
approximate controller: body phase health becomes one remaining 3600-point
pool, old body/snake action ids are translated, legacy eggs become 60-tick
seed pods, and action/frame bounds are sanitized before any exact sequence
lookup. `VB_KEXSAVE_TEST=1 VB_KEX_QUEEN_SAVE_TEST=1` creates a live ten-tail
snake, saves and reloads the complete world, and verifies body, head, all ten
sprite ids and health gates after deserialization. On KILARENA, the lifecycle
probe reuses its authored Queen controller because the retail campaign has one
native controller slot; it does not attempt an impossible second allocation.

`QUEENTAI.SEQ` in STUFF.DAT is **never loaded** ("queentai" not in SeqNames[]); tail drawn from QUEEN.SEQ sub-seq 73 ×7 (queen.cpp:1069). Demo QUEEN.SEQ: 'SD', center (160,171), **107 sub-seqs, 893 frames, 1321 chunks**. QUEENEGG.SEQ: center (160,167), **26 sub-seqs, 95 frames, 99 chunks**. Zero tiles: 3072–3090, 3092–3095, 3097–3102, 3104–3116, 3121–3127, 3130–3142, 3144–3176, 3310–3315. Present: 3015–3016, 3091, 3096, 3103, 3117–3120, 3128–3129, 3143, 3177–3181, 3281–3309, 2467–2473. Body `ActionSeq` queen.cpp:44, `HeadSeq` :58, `EggSeq` :70. 0x80 read at queen.cpp:1200/1346: action 4 → BuildBullet(type 12), action 5 → BuildQueenEgg.

Kex: `AI_Queen` (queen_idle/chase/flap/death), `AI_Queen_Snake` (snake idle/chase/melee/explode), `Obj_Queen_Snake_Tail`, `AI_Seedling`, `Obj_QueenGib01–06`. Anims: queen_base 10×8, queen_chase 10×8, queen_chase_charge 10×8, queen_after_charge 10×8, queen_flap 11×8, queen_hurl 30×8, queen_tail_missile 16×8 (fires f7, fullbright f7–8), queen_pain 13×1, queen_death 62×1, queen_snake_idle 1×8, _chase 5×8, _melee 1×8, _explode 22×1, _tail 1×1, seedling_* (idle 1×8, chase 1×8, attack 6×8, spawn 2×1, projectile 1×1, death 1×1), gibs 1×1 ×6.

Historical compatible demo-layout fallback map (used only when the exact
files are absent):

| sub-seq | frames | used by | ← kex | notes |
|---|---|---|---|---|
| 0–7 (+pad 8) | 10 | body 0 idle, 1 chase-p1 | queen_base | exact |
| 9–16 (+17) | 10 | body 2 chase-p2 (wingless) | queen_chase ("stumpy") | exact |
| 18–25 (+26) | 16 | body 4 tail-whip (fires bullet 12) | queen_tail_missile | exact; 0x84 @ f7 (kex fires f7 too), fullbright f8–9; SD Q_TAIL f7 |
| 27–34 (+35) | 30 (dirs 0–1), 25 (2–7) | body 5 egg spit | queen_hurl | emit uniform 30 for all dirs (demo's uneven counts over-read already); 0x80 f12–16 (5 eggs), 0x04 f12–25; SD Q_SPIT f12 (|0x2000), REX_GRGL f0|0x2000 |
| 36–43 (+44) | 11 | body 3 bite/flap advance | queen_flap | exact; SD REX-BITE f3–4, Q_GURGLE f10|0x2000; 0x80 f4–6 present in demo but unused for action 3 |
| 45–52 | 20 | body 6 lay-wasp-egg, 7 pain | queen_pain (13×1) | replicate to 8 dirs + stretch 13→20 — flagged (loses per-dir art); SD Q_CRY f5 |
| 53 | 13 | body 9 death-throes loop | queen_pain | exact count; Q_CRY|0x400 f6 |
| 54 | 61 | body 8 death | queen_death f0–60 (f61 is its gib trigger) | Q_DETH|0x2000 f4, Q_CRY|0x2000 f34 |
| 55 | 1 | body 10 corpse | — | tile 3117 **exists** — no work |
| 56 | 1 | head 0 (launched/bouncing) | queen_snake_idle front frame | flagged approximation |
| 57–60 | 1 each | gib picnums (queen.cpp:1391; engine retextures to kTile3117-3119 + kTile3126, queen.cpp:1400) | queen_gib01..06 | 3118/3119/3120/3016 exist; **kTile3126 is zero → must fill via DEF (gib art, atlas sprite 371)** |
| 61–64, 99–105 | 0 | padding | — | keep |
| 65–72 | 1 | head 1–5 fly/chase | — | tiles 3285–3289 **exist** — no work |
| 73 | 1 | tail ball ×7 | — | 3284 exists — no work |
| 74–81 | 1 | head 6 rise/roar | queen_snake_melee | 3311–3314 zero; SCRP_ICU f0 (VOC present) |
| 82–89 | 1 | head 7 | queen_snake_melee | SCRP_HIT f0 (VOC present) |
| 90–97 | 5 | head 8 chomp | queen_snake_chase | exact |
| 98 (3315 zero), 106 (3286 ok) | 1 | unused | — | empty/duplicate |

QUEENEGG.SEQ map: 0–7 (+8) hatchling idle 1 f ← seedling_idle (EggSeq[2] dead code; compile anyway); 9–16 (+17) hunt 6 f ← seedling_attack (exact; SD EGG_BONG f0, VOC present); 18 hatch 35 f — tiles 2467–2473, 3128 present, only **3147 zero** (patch from seedling_spawn; SD GRN_BOOM f9, alias `grn_boom`→explode01.wav); 19 thrown egg 1 f — 3128 exists; 20–22 padding; 23 wasp egg 1 f — tile 3127 zero, **no kex equivalent** (flagged: reuse seedling_spawn f0 egg art or 3128 art ×2); 24/25 shells — 3015/3016 exist. Head contact plays hardcoded `kSoundQTail|0x2000` (queen.cpp:882), head spawn `kSoundFishDies` (:1405) — not SD.

After hatching, KEX mode hands the actor to the dedicated Seedling branch of
the shared flyer record. `kexseedlingseq.py` emits a 31-slot, 68-frame/chunk
`SEEDLING.SEQ`: 0+d is the exact one-frame grounded chase, 9+d is the exact
six-frame attack, and the other native slots are safe compatibility poses.
Fifteen origin-anchored tiles occupy 7850-7864. `SEED_ATK` fires on attack
frame 3 beside `A_MeleeAttack(48,20)`. The controller enforces no-drop-off
ground movement and authored health/radius/scale/speed 100/36/2.0/16; death
tosses Queen gibs 1/3/4/5/6 and removes the actor.

Sound aliases include q_tail→queenatk04, q_spit→queenatk01,
q_gurgle→queenact02, q_sight→queenact01, q_cry→queenroar,
q_deth→queendeath01, rex-bite→queenatk02 and q_stumpy→queenatk03. The exact
sound/event table and every alias are audited by `kexverify.py`.

### 2.7 SELKIS — `SCORP.SEQ` (kSeqScorp, SeqNames[24], sequence.cpp:105; lotag 112, init.cpp:866 → BuildScorp)

Engine: statnum 122, xrepeat/yrepeat 80, health 20000, cstat 0x101. `ActionSeq` (scorp.cpp:57–68): 0={0,0} idle, 1={8,0} walk, 2={29,0} melee (damage 7 on 0x80, scorp.cpp:351), 3={19,0} ranged homing bullet 16 on 0x80 (:379), 4={45,1}…7={48,1} pain A–D (non-lethal hit picks `RandomSize(2)+4`; death cycles 6|7 ×10), 8={50,1} death burst (**spawns one spider per frame**, BuildSpider, :414), 9={53,1} final death → delete. Demo: 'SD', center (160,159), 64 seqs, 692 frames, 841 chunks, 18 sound names, 82 events. Zero tiles: 2698–2713, 2716–2719, 2722–2750. Present: 2714 (25×22), 2715 (31×16), 2720, 2721 (zap muzzles), gore 2390–2393, 2467–2469, 3033–3037.

Kex: `AI_Selkis` — health 4000, radius 80, scale 2.5 (native clipdist 120/repeat 100); selkis_idle 1×8, selkis_chase 8×8, selkis_shoot 6×8 (fires `Proj_Cobra_Red_Selkis` f3), selkis_melee 9×8 (A_MeleeAttack f0 & f6), selkis_pain 25×8, selkis_wounded 40×1, selkis_malfunction1 12×1, selkis_malfunction2 20×1, selkis_death 1×1, selkis_explode 3×1 (countdown loop spawns 12 AI_Scorp), selkis_corpse 1×1. Sheet selkis.png 1024×512, 103 sprites ≤64 px, **original resolution** (slice 8 = 28×22 vs demo muzzle 2714 = 25×22). The frame-3 callback is the exact `A_FireProjectile(..., 2, 144, 64)` recipe. Its type-16 KEX bridge bypasses the native hitscan shortcut: the cobra is a zero-damage moving projectile that collides with actors/walls, adjusts along floor/ceiling planes, and turns visually without changing velocity (`homing` plus `noHomingThrust`).

The dedicated `kexselkisseq.py` output is a 51-slot, 469-frame/chunk
`SCORP.SEQ`. It uses 167 precomposited authored views on private tiles
20200–20366; all eight directional views are explicit and no chunk flip is
required.

| Generated slots | retail action | frames | controller contract |
|---|---|---:|---|
| 0–7 | idle | 1×8 | exact directional pose |
| 9–16 | chase | 8×8 | `A_SelkisThink` every rendered frame |
| 18–25 | shoot | 6×8 | Cobra callback/fullbright on f3; chase after f5 |
| 27–34 | melee | 9×8 | 20 damage on f0/f6; range check after f8 |
| 36–43 | pain | 25×8 | authored facing callbacks through f23 |
| 45 | wounded | 40×1 | exact callback and sound cadence |
| 46 | malfunction1 | 12×1 | exact callback and sound cadence |
| 47 | malfunction2 | 20×1 | exact callback and sound cadence |
| 48 | explode | 3×1 | `A_SelkisCountDown` only on f1 |
| 49 | corpse | 1×1 | persistent and non-solid |
| 50 | terminal | 1×1 | SEQ terminator (`SeqFlag=-1`) |

Slots 8/17/26/35/44 are empty separators. The controller recognizes this
signature and otherwise retains the stock/demo action table. In the exact
path, the zero-delay death trampoline initializes 24 countdown callbacks; the
three-frame explosion loop therefore reaches the corpse on rendered tick 71,
spawns twelve `AI_Scorp` actors on alternating post-decrement values, emits
the probabilistic explosion effects, plays the death sound and fires remote
event 1010 once. The exact sound table includes the retail shoot, claw,
second attack, sight, hurt and two malfunction recordings; `slk_at2` maps to
`sounds/selkisatk02.wav`.

## 3. Cross-creature open problems + resolutions

1. **Flyer identity (resolved)** — Wasp, Bird and Queen Seedling share one native run-record type but carry a persistent KEX identity. Exact `WASP.SEQ`, `HAWK.SEQ` and `SEEDLING.SEQ` action tables are selected independently; hitag markers and private tile ranges reconstruct old/new saves without changing the classic saved struct.
2. **Deaths the remaster gibs away (resolved)** — Wasp, Bird and Seedling bypass persistent native death states and execute their authored immediate gib/feather recipes. Sentry uses its authored immediate nine-gib deletion. All eleven `A_GibDeath` callers spawn one shared fullbright `Fx_Gibs01`: blank tick 0, 13 visible ticks using ten poses, five final blank ticks, then `A_RandomItem` and destroy on tick 18. It inherits actor scale and applies the authored offsets (Anubis/Sentry/Queen +64, Seedling +32, all others +0), so the drop point is origin + offset + the effect's own height 64. Spider/Scorpion and the other delayed families keep save-serialized invisible host states only for cleanup and old-conversion fallback. Set, Selkis and Queen retain their exact authored death timelines; Queen continues as its linked snake actor before the final exit event.
3. **Missing pain anims**: Wasp, Bird and Seedling have authored `painChance 0` and never enter native pain in KEX mode; their dedicated sequence slots are safe compatibility poses. Set's eight-frame pain group is exact and reachable at painChance 24. Queen pain is the exact single-view 13-frame threshold transition.
4. **Frame-count mismatches**: Sentry, Set, Selkis and Queen are resolved by dedicated exact sequence/controller pairs. For native trigger-driven groups, keep each 0x80 callback on the controller's expected frame; dedicated exact controllers use the rendered retail frame directly.
5. **Sound alias gaps** — the flyer aliases are resolved (`wasp_hit`, both Wasp deaths, two bird flaps/attack/sight/death, and the Seedling bite). Existing boss aliases include `rex-bite`, `rex_grgl`, `rex-icu` and `ghost109`; every new exact sequence sound is asserted by `kexverify.py`.
6. **Hardcoded picnums outside SEQs**: the classic/fallback Queen path still references its DOS gib pictures. The exact KEX path addresses the six generated Queen gib tiles 7841–7846 directly.
7. **Behavior fix flags** (deliberate deviations from demo data, keep noted): exact Selkis uses the retail melee callbacks on f0/f6 (the stock boss sequence had no damaging claw flag). The exact controller applies the authored 20 damage from those rendered frames.
8. **Pixel scale calibration**: KEX monster sheets are original-resolution; actor `scale` is represented by Build repeats. Wasp/Bird/Seedling use 50/60/80 for authored 1.25/1.5/2.0, with one-to-one PNG pixels and origin-preserving chunks. Other creatures retain their per-controller repeat calibration.
9. **Palette**: chunk tiles go in via `tilefromtexture` PNGs like the existing actor tiles (kexmap.py already ships truecolor PNGs through exhumed.def — "no palette reduction happens", kexmap.py:19) — no quantization needed; transparent pixels stay transparent PNG alpha.
10. **Chunk offset range**: `tspr->xoffset/yoffset` are int8 — assert `|chunk x|,|y| ≤ 127` after scaling; the big bosses (set canvas height ~139 above anchor, selkis, queen) sit near the limit — if a composited frame exceeds it, split it into ≥2 vertically stacked chunks (multi-chunk frames are fully supported and the stock bosses already use up to 5).
11. **Water crossings (resolved)** — the KEX `A_TossActor` and remaster projectile bridges snapshot their pre-move sector, then mirror `kexActor::UpdateMovement`: an air-to-underwater transition plays `splash01.wav` and spawns the fullbright, scale-1 `Fx_WaterSplash` actor on the destination ceiling (water-surface) plane. KEX projectiles retain their velocity across that portal (the sole flame projectile is outside this bridge and keeps its authored water removal); the old universal native bullet deletion remains classic-only. The `FF_WATER` pre-move below-surface gate remains silent, matching the source's `bNoSplash` case. `KEXWATERSPLASH.SEQ` preserves all 23 default-rate source frames as actor-centred composites on tiles 22014–22036; this stays KEX-only and never substitutes the classic `BuildSplash` art or sound.

## 4. Implementation plan — kexmap.py (function level)

Reuse existing plumbing: `_sprite_table(Z, names, group, base)` (:1691, parses `sprite <idx> <x> <y> <w> <h>`), `sheet()` PNG loading pattern from `build_actor_tiles` (:1752, via `kexpng`), `_write_table` (:1855, kexsound.txt writer at :2453), `write_def`/def-assignment list (:2159), `KEX_SOUNDS` (:1256), orchestrator `convert_install` (:2199).

**New code, in dependency order:**

1. `parse_anim_full(Z, names, animname)` — replaces `_all_frames`'s first-frame-only view for creatures. Parses `sprites/monsters/<x>_anim.txt` blocks: per `frame { }` → `{'rotations': [8 sprite-lists] or None, 'sprites': [...] (single-view), 'fullbright': bool, 'fire': bool}`. Sprite tuple: `(group, base, idx, ox, oy, flip)`. Rotation blocks appear in fixed order `rotation_N, NE, E, ES, S, SW, W, WN`; single-view anims have a bare `sprites` block. Record `// ##NNN S F` conversion comments (original frame/sound/flag) when present — set_fire_souls carries the original 0x80 position this way.

2. `write_seq(path, seq_file)` — §1.8 verbatim (round-trip proven).

3. `class TileAllocator` — `alloc(image_bytes_hash) -> tile`: dedupes identical composited frames (held poses collapse to one tile, mirroring the stock one-tile-per-pose economy); allocates from `CREATURE_TILEBASE` (first free tile after `build_actor_tiles`' range); `force(tile, image)` for hardcoded picnums (3126). Emits `(tile, 'kexart/seq/<creature>_<n>.png')` def rows — **no xoffset/yoffset in the def** (picanm must stay 0, §1.7).

4. `composite_view(sheets, tables, layers, scale) -> (img, w, h, cx, cy)` — pastes each `(group, base, idx, ox, oy)` slice (skip flip-marked SW/W/WN layers — those views are not rasterized; they are emitted as x-flipped chunks of the ES/E/NE tile, matching stock mirroring): bbox over layers, `kexpng.blank/crop/paste` exactly as in `build_actor_tiles` (:1805–1830), then integer downscale by the creature's calibrated factor. Returns chunk-center offset relative to the feet origin: `cx = round((x0+w/2)*scale)`, `cy = round((y0+h/2)*scale)` (kex y is already negative-up-relative-to-feet; matches §1.5 +y-down after sign check against a demo reference chunk, e.g. selkis idle rot0 or set idle body-top −139). Assert `|cx|,|cy| ≤ 127`, else split into stacked chunks.

5. `CREATURE_SPECS` — one dict per output file, each a list of sub-seq entries in file order (this encodes every table in §2 literally):
   ```python
   ('group8', anim, nframes, frame_map, flags={local_f:bits}, sounds=[(local_f,'NAME',bits)])
   ('single', anim_or_synth, nframes, ...)   # .b==1 actions and pic-only seqs
   ('empty',)                                # separators / unused, SeqSize 0
   ('demo',)                                 # copy this sub-seq verbatim from the demo file
   ```
   `frame_map` = list of KEX frame indices per Build frame (handles repeats, long stretches and hold-last mappings). `('demo',)` entries need `read_seq(demo_bytes)`, the inverse of `write_seq`. Dedicated exact generators supersede this generic spec where controller timing differs: Sentry uses 36 slots, Spider/Scorpion 48 each, Anubis 58, Mummy 62, Bastet 57, Fish 49, Selkis 51, Queen 104 and QueenEgg 2. Per-file centers remain WASP (160,148), ANUBIS (159,156), ROACH (160,156), SET (160,177), QUEEN (160,171), QUEENEGG (160,167) and SCORP (160,159).

6. `compile_creature_seq(Z, names, spec, alloc, demo_seq=None) -> seq_file` — walks the generic spec: for `group8`, emits 8 sub-seqs, with dirs 0–4 composited from N..S and dirs 5–7 normally sharing mirrored ES/E/NE tiles. Sentry is the explicit exception: `kexseqmap_gen.py` precomposites all eight rotations, including per-layer source flips, and writes chunk flags of zero. Generic sequences may still use demo-present art by tile number where appropriate.

7. Sequence orchestration opens STUFF.DAT for fallback copies and validation, builds the compatible creature art, then writes generated exact files from committed data modules or directly from KPF animation metadata. `kexmap.convert_install()` installs `WASP.SEQ`, `HAWK.SEQ`, `SEEDLING.SEQ`, `ROACH.SEQ`, `SPIDER.SEQ`, `SCORPION.SEQ`, `ANUBIS.SEQ`, `MUMMY.SEQ`, `LION.SEQ`, `FISH.SEQ`, `SCORP.SEQ`, `SET.SEQ`, `QUEEN.SEQ`, `QUEENEGG.SEQ`, the one-frame fullbright `AMUNBOMB.SEQ`, `KEXGIBDEATH.SEQ`, `KEXBIRDFEATHER.SEQ`, and `KEXWATERSPLASH.SEQ`. The gib sequence composites ten exact `effects/gib_death` poses onto tiles 22004-22013 and emits the source's blank/13-art/5-blank cadence; the water-splash file composites all 23 `effects/splash` source poses on tiles 22014–22036, while the KEX-only moving-actor bridge owns the water-sector transition, source-plane placement and `spl_big` callback. The feather file loops its 15 source crop positions over bird tile 7632, while the runtime owns its random start/fall/drop callbacks. Amun-bomb tile 7848 and boulder-shard tile 7849 are composited directly from `projectiles/amun_bomb_idle` and `projectiles/boulder_shard`; their actor scale remains a runtime repeat concern. Exact Bastet/Fish, Selkis, Set and Queen files are written after `patch_demo_seqs()` so the stock-layout fallback cannot overwrite them. Exact Anubis keeps slots 44–49 from the demo verbatim for drummer compatibility while replacing live actions, adding the nine-tick miss group, and expanding normal death. Bastet and Fish likewise retain copied compatibility/effect slots while replacing every live group with the retail cadence.

8. `kexverify.py` reparses every result and asserts the code-referenced groups, callback frames, tile coverage, aliases and chunk bounds. Anubis checks all seven authored motion/death groups, both death sounds, normal/gib callbacks, the 58-slot/892-frame/1042-chunk mixed exact/demo layout and tiles 21300–21427. Spider and Scorpion check their authored three-frame/four-tick deaths, complete three-/two-pose leap callbacks, exact gib inventories, delayed random-item ownership, save-stable skin identity, 48-slot/291-frame/419-frame layouts and tiles 20700–20744/20900–20947. Mummy checks the complete delayed source timelines, normal/gib callbacks, 62-slot/680-tick layout and tiles 21100-21207. Bastet checks the 57-slot/620-frame/623-chunk layout, all teleport/pain/attack callbacks, 168 composites and tiles 21500–21667; Fish checks the 49-slot/409-frame/415-chunk layout, fullbright and sound callbacks, 107 composites and tiles 21700–21806. In addition to Sentry, Set and Selkis, exact Queen checks all four actor types, fourteen script functions, seventeen animations, the 104/2-slot layouts, flags/events, tiles 20400–20679, all 280 PNGs, zero flips and native lifecycle tokens. `GIBDEATH` independently locks the retail actor/function, all eleven caller offsets, the 19-tick SEQ, ten PNG/DEF rows, inherited scale and effect-height callback bridge, plus duplicate-drop suppression. The verifier also statically enforces the shared four-internal-units-per-KEX-health-point contract across every adapted actor.

9. `KEX_SOUNDS` includes the Queen's exact sequence aliases, including `rex-bite`, `q_gurgle`, `q_sight`, `q_stumpy`, `q_tail`, `q_spit`, `q_deth` and `q_cry`; these flow to kexsound.txt automatically.

**Test recipe**: run the converter and full verifier, then launch a converted map containing each lotag (100 Anubis, 101 Mummy, 102 Bastet, 103/104 Sentry, 106 Fish, 108 Mantis, 111 Set, 112 Selkis, 113 Queen and 117 Wasp). Unattended probes must use `VB_HIDE_WINDOW=1`, `SDL_AUDIODRIVER=dummy`, `-nosound -nomusic` and an isolated absolute config. Use `VB_KEX_COMMON_ENEMY_TEST=1`, `VB_KEX_FLYER_TEST=1`, `VB_KEX_SENTRY_TEST=1`, `VB_KEX_SET_TEST=1`, `VB_KEX_SELKIS_TEST=1`, `VB_KEX_QUEEN_TEST=1`, `VB_KEX_MANTIS_TEST=1`, `VB_KEX_PROJECTILE_TEST=1`, `VB_KEX_BOMB_TEST=1` and `VB_KEX_GIB_DEATH_TEST=1` in separate runs for deterministic cadence/callback/stat/projectile probes. The gib-death probe validates representative +0/+64 offsets, scale 1/2, fullbright flags and the effect-top callback; the projectile probe covers both launch recipes and the Cobra/fireball/boulder death-blast matrix; the bomb probe covers 16→48 charge, release-time ammo, radius/scale, velocity/gravity and 512/140 first-impact behavior. Run `VB_KEX_QUEEN_TEST=1`, and pair `VB_KEXSAVE_TEST=1` with `VB_KEX_QUEEN_SAVE_TEST=1`, on KILARENA for the exact lifecycle and ten-tail serialization probes; `VB_KEX_QUEEN_AUDIT=1` and `VB_KEX_PROJECTILE_AUDIT=1` report live resolved values. Confirm all eight views, attack effects, deaths, and that untouched demo creatures still render so global sequence/frame/chunk limits are covered.

Key file paths: `/Users/alonamir/dev/eduke32/source/exhumed/src/sequence.cpp`, `wasp.cpp`, `roach.cpp`, `set.cpp`, `queen.cpp`, `scorp.cpp`, `init.cpp`, `exhumed.cpp` (:2556/:2577 load order), `/Users/alonamir/dev/eduke32/source/exhumed/tools/kexmap.py`; scratch tooling: `/private/tmp/claude-501/-Users-alonamir-dev-eduke32/8338d716-9146-4e6d-a50c-c933a3d0904b/scratchpad/{seqtool.py, writeseq.py, seqparse.py, parse_set_seq.py, scorp_seq.json, set_seq_dump.txt}`.
