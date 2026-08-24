<!-- Research artifact: the implementation-ready spec for compiling the
     remaster's sprite anims into Build .SEQ files, so the creatures whose
     art the shareware STUFF.DAT lacks (389 zero-size tiles: wasp, sentry,
     Set, queen, Selkis, bird) become visible in converted levels. Produced
     by a multi-agent research pass over sequence.cpp, the demo's SEQ/ART
     data and the remaster's anim scripts; the format writer round-tripped
     byte-identical on ANUBIS/MUMMY/WASP.SEQ. Not yet implemented. -->

# Compiling remaster sprite anims into Build .SEQ files — implementation spec

Goal: `kexmap.py` emits loose `WASP.SEQ`, `ROACH.SEQ`, `SET.SEQ`, `QUEEN.SEQ`, `QUEENEGG.SEQ`, `SCORP.SEQ` (+ tiles via `exhumed.def`) next to the converted maps, overriding STUFF.DAT via `kopen4loadfrommod`, so the demo's 389 zero-size monster tiles stop making these creatures invisible. No engine changes.

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

Engine: `wasp.cpp:60` `ActionSeq[] = {{0,0},{0,0},{9,0},{18,0},{27,1},{28,1},{29,1}}`; xrepeat 50 normal / 20 egg-wasp (wasp.cpp:132–139); pain action 3 is dead code (wasp.cpp:226–232 sets then immediately overwrites `nAction=1`). Demo file: tag 'SD', center (160,148), 31 sub-seqs, 229 frames, 224 chunks, 1 chunk/frame. All 29 tiles 2432–2460 are 0×0. Demo per-dir tile block `2432+5d` (d=0..4) = {flapA, flapB, C recoil, D sting, E follow-through}. `WASPSMAL.SEQ`/`WASPBIG.SEQ` exist in the GRP but are never loaded.

Kex: `AI_Wasp` (type 7): spawnAnim `wasp_base`, chaseAnim `wasp_chase` (identical frames), meleeAnim `wasp_attack`, deathAnim `wasp_death` (1 frame, no sprites — gibs via `A_TossActor` ×8 of `wasp_gib01..05`, sprites 10/11/0/13/19), scale 1.25, health 60. Sheet `sprites/monsters/wasp.png` 512×256.

| Build sub-seq | frames | ← kex source | mapping |
|---|---|---|---|
| 0+d fly (idle+chase) | 4 | wasp_base rot d | 1:1 — both A,A,B,B. Sprites (N/NE/E/ES/S): A = 6/15/3/12/1, B = 8/17/5/14/2 |
| 8, 17, 26 | 0 | — | empty separators (required) |
| 9+d attack | 13 | wasp_attack rot d | Build f0–3 ↔ kex f0–3 (A,A,B,B); Build f6–8 (sting D) ↔ kex f4–7 (sting: 9/18/7/16/4); Build f4,5,11,12 (C) → reuse flapB; f9,10 (E) → reuse sting or flapA (no kex art) |
| 18+d pain | 6 | — (kex painChance 0) | hold C stand-in (flapB); unreachable at runtime |
| 27 death tumble | 32 | — (kex gibs) | reproduce demo structure from fly tiles: 2433,2434,2457,2458,2459,2453,2432,… (2–3 ticks/tile, some y-flip); 2457–2459 = y-flipped fly sprites 6/1 (demo itself y-flips here) |
| 28 plummet loop | 7 | — | 2459 stand-in ×7 (mixed x-flip in demo) |
| 29 splat | 1 | — | tile 2460 stand-in: composite of gib sprites 10,11,0,13,19 (flagged, §3.2) |
| 30 unused | 5 | — | 5 chunk-less frames, SeqFlag −1, matching demo |

Actions 4/5/6 (`.b=1`) stay single-view. SD table: names `[WASP_BUZ, WASP_DIE, WASP_DY1, WASP_STG, WASP_WNG]` — all 5 VOCs exist in the demo GRP, no aliases needed. Events that actually fire: fly seq0 f0 → WASP_WNG|0x2000; attack seq9 f7 → WASP_STG; death seq27 f0 → WASP_DIE. (Demo also has WASP_BUZ on f0 of fly/attack dirs 1–7 and WASP_DY1 on pain seq18 f5 — never fire; copying all 18 events verbatim is safest.) Kex wav equivalents (reference only): waspact/waspatk/wasphit/waspdeath01/02; WASP_WNG has no kex counterpart. All FrameFlags 0.

### 2.2 BIRD — no Build AI; resolved to stay on WASP.SEQ (see §3.1)

Facts: `AT['CROW'] = (117, 0)` and `AT['MOSQUITO'] = (117, 0)` (kexmap.py ~1463–1464) → both spawn via `init.cpp:841 case 117:` `BuildWasp`. `HAWK.SEQ` is loaded (SeqNames[51], `kSeqHawk`, sequence.h:81) but `kSeqHawk` is referenced by **zero** .cpp files — dead data. Demo HAWK.SEQ: 2622 B, 'SD' with empty sound table, center (160,100), 54 sub-seqs (39–53 empty), 163 frames/chunks; layout: 0–7 fly 12 f/dir (11 in dir7), 9–16 glide 1 f (2 in dir3), 18–25 fly-step 6 f, 27–34 attack pose 1 f (tiles 3009–3013), 36 death fall (3014), 37 feather (3015, 10×12 **exists**), 38 (3016, 13×8 **exists**); tiles 2979–3014 all zero-size. Quirk: its dir groups are phase-rotated 90° (unique views 0,1,2,6,7; mirror axis through 2/6) vs the standard wasp/kex convention (unique 0–4, axis 0/4) — a regenerated file should use the standard convention.

Kex: `AI_Bird` — health 10, painChance 0, fullbright 1, sightSound birdact.wav; anims: bird_fly_calm 1×8, bird_fly_chase 6×8 (flap sounds f1/f4), bird_fly_retreat 12×8, bird_attack 7×8 (one pose; melee f0, delay 8, goto retreat), bird_death 3 (f0/f1 = chase poses f4/f5; f2 spriteless gib+destroy), bird_feather 15×1 (sprite 32, 20×3). Sheet bird.png 256×256, 36 sprites. If a bird-skinned WASP.SEQ were built: fly 0–7 ← bird_fly_chase (6 f/dir); attack 9–16 ← bird_attack pose held; pain 18–25 ← retreat/chase f0 (no kex pain); death 27 ← bird_death f0–f1 alternated; 28 ← feather frames; 29 ← feather-pile tile (matches HAWK.SEQ's surviving 3015/3016). Sound aliases it would need: `hawk_flp` → sounds/birdflap01.wav, `hawk_die` → sounds/birddeath.wav (existing: `hawk_hrt` → birdattack.wav, kexmap.py KEX_SOUNDS; demo VOCs HAWK_HRT/HAWKFIRE exist).

### 2.3 SENTRY — `ROACH.SEQ` (kSeqRoach, SeqNames[50], sequence.cpp:131/sequence.h:80)

Engine: `init.cpp:928` lotag 104 → `BuildRoach(1,…)` (start dormant, action 0); `init.cpp:938` lotag 103 → `BuildRoach(0,…)` (start alert, action 1); xrepeat/yrepeat 40 (roach.cpp:110–111). `ActionSeq` (roach.cpp:40–48): 0={24,0} dormant idle, 1={0,0} alert idle, 2={0,0} walk, 3={16,0} attack (fires bullet type 13 on 0x80, roach.cpp:394–397), 4={8,0} pain, 5={32,1} death, 6={42,1} corpse. Demo file: 'SD', center (160,156), 46 sub-seqs, 707 frames, 849 chunks. Zero-size tiles: 3200–3225, 3227–3253. Present: 3226 (97×113 attack), 3254/3255 (debris 29×4, 26×5), 3261/3262 (5×5), 3266 (shell 41×42), 3270 (corpse 54×23), unused 1885–1894.

Kex: `AI_Sentry` — scale 1.5, health 180, sightSound sentryact01.wav; sentry_idle 1 f, sentry_chase 8 f (A_SentryChatter f0), sentry_pain 12 f (sentrypain01 f0), sentry_attack 10 f (sentryfire + `A_FireProjectile("Proj_Blaster")` **at f6**), sentry_death 1 f (gib: 9× A_TossActor of sentry_gib01–05, A_Destroy). Projectile Proj_Blaster (damage 30) maps to Build bullet 13 art (ROACSHOT/ROCHFIRE.SEQ — out of scope).

| Build sub-seq | frames | ← kex | mapping / flags / sounds (dir-0) |
|---|---|---|---|
| 0–7 walk & alert idle | 8 | sentry_chase | 1:1 |
| 8–15 pain | 12 | sentry_pain | 1:1; SD HAWK_HRT @ f0 |
| 16–23 attack | 8 | sentry_attack | 10→8: take kex f2–9 so kex fire-f6 lands on Build local f4; flags 0x84 @ f4, 0x04 @ f5; SD LAZ-FIRE @ f4 |
| 24–31 dormant idle | 5 | sentry_idle | 1→5 repeat (SeqSize ≥ 2 required — roach.cpp:265 checks `field_2 == 1`); demo flags 0x04 @ f3,f4 (blink), keep; no sound (demo's ROCH_WLD entries only sat on dirs ≠0 — never fired) |
| 32 death | 38 | — | keep demo frames 13–37 verbatim (tiles 3254/3255/3266/3270 exist) and paint f0–12 from sentry_pain rotation_N; SD ROCH_DIE @ f0, SPISPLAT|0x2000 @ f23; up to 5 chunks/frame (shell 3266 + debris) |
| 33 (4 f), 34–41 (50 f ×8, tiles 1885–1894 present), 43–45 (0 f) | — | unused by roach.cpp | emit empty (size 0), but **file must reach ≥43 sub-seqs** so index 42 exists (demo pads to 46, SeqFlag[45]=−1) |
| 42 corpse | 1 | — | tile 3270 already visible — reference as-is |

Sounds: HAWK_HRT (VOC present; alias `hawk_hrt`→birdattack.wav), LAZ-FIRE (**no VOC** — alias `laz-fire`→sentryfire.wav required, exists in KEX_SOUNDS), ROCH_DIE (**no VOC** — alias `roch_die`→sentrydeath01.wav, exists), ROCH_WLD (alias exists, moot), SPISPLAT (VOC present, no alias needed). Note kexmap ACTOR_MAP emits only lotag 103 for AI_Sentry, but lotag 104's dormant group (24–31) must still be compiled — the action is reachable.

### 2.4 SET — `SET.SEQ` (kSeqSet=48, sequence.h:78; lotag 111, init.cpp:871 → BuildSet)

Set's projectile (bullet 11, `BulletInfo[11]={30,-1,1000,-1, nSeq=52 kSeqSetGhost, field_C=53 kSeqSetGBlow, 4,20,48}`, bullet.cpp:94) uses SETGHOST/SETGBLOW.SEQ whose tiles (2681–2686, 3037, 3290–3295) all exist — not part of the problem. Demo SET.SEQ: 'SD', center (160,177), **81 sub-seqs, 1270 frames, 1305 chunks**, 12 sound names, 104 events. Zero tiles: 2559–2642 and 2687 (85 tiles — every body tile). Present: 2467 (35×28), 2678 (47×88 soul), 3034–3036, 3291–3292. Sprite repeat 87×96. State flow: spawn→1 dormant→2 wake→7 leap→chase 3 with random 7/6; 4 melee; 6 fire souls; 8/9 air/land; 10→11 death. Action 5 (stun) is never entered.

Kex: `AI_Set` — scale 2.0, health 3000, painChance 24; anims set_idle 1×8, set_chase 20×8 (A_SetThink; setstomp02 f5,f15), set_melee 18×8 (hits f8,f17), set_leap_start 13×8 (setjump f9, launch f12), set_leap_loop 1×8, set_leap_land 12×8 (setland+quake f6), set_fire_souls 15×8 (fires Proj_Soul f7, fullbright f9), set_pain 8×8, set_death 30×1, set_death_loop 1×1. Script: leap at HP<2000 (25%/think), souls at HP<1500 (50%/think).

| Build sub-seq | frames (demo) | ← kex | mapping |
|---|---|---|---|
| 0–7 walk (actions 0,3) | 20 | set_chase | 1:1 (20→20) |
| 9–16 melee (action 4) | 18 | set_melee | 1:1; FrameFlag 0x80 on f6 & f15 (demo positions; kex hits f8/f17 — keep the demo pair) |
| 18–25 leap wind-up (7) | 15 | set_leap_start | 1:1 f0–12, hold last pose f13–14 (or write SeqSize 13 — code reads size from file) |
| 27–34 airborne loop (8) | 1 | set_leap_loop | 1:1 |
| 36–43 landing (9) | 12 | set_leap_land | 1:1 |
| 45–52 fire souls (6) | 18 | set_fire_souls | f0–14 1:1 + hold ×3; 0x84 on projectile frame (demo f10; kex fires f7, original `##009 0 128` comment marks f9 — use f10 to match demo timing), 0x04 on fullbright tail (demo f11–17) |
| 54–61 unused | 18 | — | dead code; fill with set_pain/set_chase duplicates or 1-frame stubs — indices only must survive |
| 63–70 stun (action 5, unreachable) | 30 | set_pain | 8→30 stretch or SeqSize 8; harmless |
| 72 death (10) | 112 | set_death | 30→112 stretch/hold; 0x84 gib-spawn flag mid-anim (demo f81), 0x04 fullbright f79–91; optionally re-add demo's spirit-overlay second chunks (2467, 3291/3292, 3034–3036 — tiles exist) on f58–92 |
| 74 dead loop (11) | 1 | set_death_loop | 1:1 |
| 75 soul pic (BuildSoul/FuncSoul) | 1 | — | tile 2678 **exists** — no work |
| 76 gib-chunk pic (action 10) | 1 | — | no kex counterpart — synthesize (crop of a death pose) — flagged |
| 77 dormant statue (1) | 70 | set_idle | 70-frame hold of set_idle rotation_N; pose A f0–35 (tile 2559), pose B f36–69 (2687) both reuse it — flagged approximation |
| 78 wake (2) | 29 | — | synthesize: set_idle pose (f0–7 ≙ 2559) → first chase/leap pose (f8–28 ≙ 2560) — flagged |
| 8,17,26,35,44,53,62,71,73,79,80 | 0 | — | empty separators; SeqFlag[80]=−1; total must be exactly 81 |

SD events (dir-0): walk f5,f15 SET_LAND|0x2000; melee f0,f9 SET_LAND|0x2000, f13 SET_HIT|0x2000; leap f9 SET_JUMP; land f0 SET_LAND|0x2000, f6 RA_SPRIT|0x2000; fire f10 SET_SPRT; stun f4 REX_ICU; death f0 SET_GR2|0x2000, f57 REX-ICU|0x2000, f79 RA_SPRIT|0x2000, f111 SET_DETH|0x2000; soul seq75 f0 GHOST109|0x0200; dormant f35 SET_GR1|0x2000; wake f8 SET_ICU. Demo VOCs present: only RA_SPRIT, REX_ICU, SET_LAND. Aliases already in KEX_SOUNDS: set_land→setland.wav, set_jump→setjump.wav, set_hit→setpain.wav, set_sprt→setquake.wav, set_gr1→setact01.wav, set_gr2→setatk02.wav, set_icu→setact02.wav, set_deth→setdeath.wav, ra_sprit→setatk01.wav. **Gaps**: `REX-ICU` (add `rex-icu`→sounds/setquake.wav) and `GHOST109` (add `ghost109`→sounds/setatk03.wav, nearest).

### 2.5 QUEEN — `QUEEN.SEQ` (kSeqQueen=49) + `QUEENEGG.SEQ` (kSeqQueenEgg=56); lotag 113 (init.cpp:861 → BuildQueen, statnum 121)

`QUEENTAI.SEQ` in STUFF.DAT is **never loaded** ("queentai" not in SeqNames[]); tail drawn from QUEEN.SEQ sub-seq 73 ×7 (queen.cpp:1069). Demo QUEEN.SEQ: 'SD', center (160,171), **107 sub-seqs, 893 frames, 1321 chunks**. QUEENEGG.SEQ: center (160,167), **26 sub-seqs, 95 frames, 99 chunks**. Zero tiles: 3072–3090, 3092–3095, 3097–3102, 3104–3116, 3121–3127, 3130–3142, 3144–3176, 3310–3315. Present: 3015–3016, 3091, 3096, 3103, 3117–3120, 3128–3129, 3143, 3177–3181, 3281–3309, 2467–2473. Body `ActionSeq` queen.cpp:44, `HeadSeq` :58, `EggSeq` :70. 0x80 read at queen.cpp:1200/1346: action 4 → BuildBullet(type 12), action 5 → BuildQueenEgg.

Kex: `AI_Queen` (queen_idle/chase/flap/death), `AI_Queen_Snake` (snake idle/chase/melee/explode), `Obj_Queen_Snake_Tail`, `AI_Seedling`, `Obj_QueenGib01–06`. Anims: queen_base 10×8, queen_chase 10×8, queen_chase_charge 10×8, queen_after_charge 10×8, queen_flap 11×8, queen_hurl 30×8, queen_tail_missile 16×8 (fires f7, fullbright f7–8), queen_pain 13×1, queen_death 62×1, queen_snake_idle 1×8, _chase 5×8, _melee 1×8, _explode 22×1, _tail 1×1, seedling_* (idle 1×8, chase 1×8, attack 6×8, spawn 2×1, projectile 1×1, death 1×1), gibs 1×1 ×6.

QUEEN.SEQ map:

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

Sound aliases: existing q_tail→queenatk04, q_spit→queenatk01, q_gurgle→queenact02, q_cry→queenroar, q_deth→queendeath01, scrp_hit→selkishurt, scrp_icu→selkisact. **Gaps**: `rex-bite`→sounds/queenatk02.wav, `rex_grgl`→sounds/queenact02.wav (both VOCs missing from demo, no alias — silent today).

### 2.6 SELKIS — `SCORP.SEQ` (kSeqScorp, SeqNames[24], sequence.cpp:105; lotag 112, init.cpp:866 → BuildScorp)

Engine: statnum 122, xrepeat/yrepeat 80, health 20000, cstat 0x101. `ActionSeq` (scorp.cpp:57–68): 0={0,0} idle, 1={8,0} walk, 2={29,0} melee (damage 7 on 0x80, scorp.cpp:351), 3={19,0} ranged homing bullet 16 on 0x80 (:379), 4={45,1}…7={48,1} pain A–D (non-lethal hit picks `RandomSize(2)+4`; death cycles 6|7 ×10), 8={50,1} death burst (**spawns one spider per frame**, BuildSpider, :414), 9={53,1} final death → delete. Demo: 'SD', center (160,159), 64 seqs, 692 frames, 841 chunks, 18 sound names, 82 events. Zero tiles: 2698–2713, 2716–2719, 2722–2750. Present: 2714 (25×22), 2715 (31×16), 2720, 2721 (zap muzzles), gore 2390–2393, 2467–2469, 3033–3037.

Kex: `AI_Selkis` — health 4000, scale 2.5; selkis_idle 1×8, selkis_chase 8×8, selkis_shoot 6×8 (fires `Proj_Cobra_Red_Selkis` f3), selkis_melee 9×8 (A_MeleeAttack f0 & f6), selkis_pain 25×8, selkis_wounded 40×1, selkis_malfunction1 12×1, selkis_malfunction2 20×1, selkis_death 1×1, selkis_explode 3×1 (countdown loop spawns 12 AI_Scorp), selkis_corpse 1×1. Sheet selkis.png 1024×512, 103 sprites ≤64 px, **original resolution** (slice 8 = 28×22 vs demo muzzle 2714 = 25×22).

| Build sub-seq | frames (demo) | ← kex | fit |
|---|---|---|---|
| 0–7 idle | 1 | selkis_idle | exact |
| 8–15 walk | 8 | selkis_chase | exact (demo seq 10 had 7 — write 8 everywhere) |
| 19–26 ranged (+27 unused spare) | 6 | selkis_shoot | exact; 0x84 on f3 (kex fires there); demo has multi-chunk f3 (body 2713 + muzzle 2714) — muzzle tiles 2714/2715/2720/2721 exist, reuse them as second chunks |
| 29–36 melee | 11 | selkis_melee (9) | emit 9 or pad f0/f5 to 11; **add 0x80 on kex strike frames 0 and 6** — demo melee has NO 0x80 (claw never damaged); deliberate fix, noted |
| 37–44 roar/pain 8-rot | 25 | selkis_pain | exact; unused by PC code but clearly its port — emit; SD SCRP_CRY f0|0x2000, SCRP_CLA |
| 45 pain A | 35 | selkis_wounded (40) | count free; SD SCRP_HIT f0|0x2000, SCRP_ICU f14|0x2000 |
| 46 pain B | 19 | selkis_malfunction1 (12) | SD SCORP_Z2 f8|0x2200, f15|0x2400, SCRP_HIT f14 |
| 47 pain C | 20 | selkis_malfunction2 (20) | exact; SD SCRP_ZP1 f9|0x600, f16|0x800 |
| 48 pain D | 3 | selkis_explode (3) | exact; SD SCRP_ZP1 f2|0x800 |
| 50 death burst | 40 | selkis_explode looped ~13× | length ≈40 keeps original spider count; SD SCRP_ZAP f6|0x600 |
| 53 final death | 85 | selkis_corpse held (+ explode prefix) — or keep demo seq 53 verbatim: its gore tiles 2390–93/2467–69/3033–37 exist | flagged; SD Q_OUCH f0, P_FIRE f11, RA_SPRT2 f14, GRN_BOOM f60 (all |0x2000) |
| 16–17 (22-f taunts), 18, 28, 49 (2 f zap, 2740/2741), 51 (30 f), 52 (3 f), 54–63 | var | — | unused by scorp.cpp; emit size 0 — **total seq count must reach ≥54** (demo: 64, SeqFlag[63]=−1) |

Walk SD: GRN_PIN f0|0x2000 (VOC present), HAWKFIRE f5|0x2000 (alias hawkfire→queenatk04). Ranged SD: ROCH_GUN f0|0x2000 (alias→sentryfire), SCRP_TAL f3|0x2600 (alias→ammo.wav). Melee SD: HAWK_HRT f0|0x2000, REX_BITE f5|0x2000 (VOC present). Unaliased-but-VOC-present: GRN_PIN, REX_BITE, SCRP_ZP4, SCRP_ZAP, RA_SPRT2 (all 18 VOCs exist in demo — everything plays). Sight sound is engine-side `StaticSound[kSoundScorpionICU]` = SoundFiles[41] "scrp_icu" (sound.cpp:70–149) — already aliased.

## 3. Cross-creature open problems + resolutions

1. **WASP.SEQ triple identity** — CROW, MOSQUITO and (presumably) WASP all map to lotag 117 → BuildWasp; one SEQ skins all three. **Resolution**: compile WASP.SEQ from **AI_Wasp art** (the native creature; birds/mosquitoes become wasps). HAWK.SEQ (kSeqHawk=51) is loaded but referenced by zero code — a real bird needs a new AI + spawn case (engine change, out of scope now). Keep §2.2's bird→WASP.SEQ table on file as the alternative skin, and if HAWK.SEQ is ever revived, regenerate it with the standard rotation convention (unique 0–4, x-flip 5–7), not its stock 90°-rotated layout.
2. **Deaths the remaster gibs away** (Build needs persistent multi-frame death anims): wasp 27/28/29 (32+7+1 f) — synth from fly tiles + y-flips per §2.1, splat tile 2460 composited from gib sprites; sentry 32 — hybrid (paint f0–12 from pain, keep existing tiles f13–37); set 72 — stretch set_death 30→112 + existing overlay chunks; set 76 gib tile — crop a death pose; queen 56 head-pop — snake idle front frame; selkis 50/53 — explode loop ×13 / corpse hold (or keep demo seq 53 verbatim: gore tiles exist). All resolutions are cosmetic-only; state machines depend only on SeqSize + the flagged frames.
3. **Missing pain anims**: wasp pain 18+d (dead code — any stand-in), set stun 63–70 (unreachable — set_pain stretch), queen pain 45–52 (kex queen_pain is 13 f **single-view**; must replicate across 8 dirs — accept the art loss), bird n/a. No engine risk in any of these.
4. **Frame-count mismatches**: sentry attack 10→8 (take kex f2–9, fire frame alignment preserved); set leap 15 vs 13 and fire 18 vs 15 (hold last pose or shrink SeqSize — engine reads size from file, either works; prefer holding to keep demo timing); selkis melee 11 vs 9 (pad); queen hurl uneven 30/25 (emit uniform 30). Rule: whenever a 0x80 trigger exists, place it on the **demo's frame index** and map surrounding kex frames to preserve it.
5. **Sound alias gaps** — add to `KEX_SOUNDS` (kexmap.py:1256): `rex-bite`→sounds/queenatk02.wav, `rex_grgl`→sounds/queenact02.wav, `rex-icu`→sounds/setquake.wav, `ghost109`→sounds/setatk03.wav; if the bird skin is ever used: `hawk_flp`→sounds/birdflap01.wav, `hawk_die`→sounds/birddeath.wav. Everything else either has a demo VOC or an existing alias (§2 tables).
6. **Hardcoded picnums outside SEQs**: queen.cpp:1400 retextures gibs to kTile3117–3119 (exist) and **kTile3126 (zero — must be filled via DEF with queen_gib01 art)**. No other creature has this.
7. **Behavior fix flags** (deliberate deviations from demo data, keep noted): selkis melee gains 0x80 on f0/f6 (stock boss claw dealt no damage); everything else copies demo flag positions exactly.
8. **Pixel scale calibration**: kex sheets are original-resolution for selkis (slice 28×22 vs tile 25×22 → ≈1:1) but the wasp research estimated 1.6–2×. **Resolution**: per-creature scale factor calibrated against a *visible* demo reference tile where one exists (sentry 3226 = 97×113, selkis 2714 = 25×22, queen head 3285–3289, soul 2678 = 47×88, wasp none → derive from actor `scale` 1.25 vs the others' calibrated ratios); Build repeat values (wasp 50/20, roach 40, set 87×96, scorp 80) do the rest. Emit the factor as a per-creature constant in the spec table so it's tweakable after a visual pass.
9. **Palette**: chunk tiles go in via `tilefromtexture` PNGs like the existing actor tiles (kexmap.py already ships truecolor PNGs through exhumed.def — "no palette reduction happens", kexmap.py:19) — no quantization needed; transparent pixels stay transparent PNG alpha.
10. **Chunk offset range**: `tspr->xoffset/yoffset` are int8 — assert `|chunk x|,|y| ≤ 127` after scaling; the big bosses (set canvas height ~139 above anchor, selkis, queen) sit near the limit — if a composited frame exceeds it, split it into ≥2 vertically stacked chunks (multi-chunk frames are fully supported and the stock bosses already use up to 5).

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
   `frame_map` = list of kex frame indices per Build frame (handles 10→8 trims, 1→5 repeats, 13→112 stretches, hold-last). `('demo',)` entries (sentry death tail, selkis seq 53 option, wasp seq 30 trailer) need `read_seq(demo_bytes)` — the scratchpad parser (`seqtool.py`) inverted from `write_seq`. Per-file constants: `center` (WASP (160,148), ROACH (160,156), SET (160,177), QUEEN (160,171), QUEENEGG (160,167), SCORP (160,159)), `scale`, required total sub-seq counts (31 / 46 / 81 / 107 / 26 / 64).

6. `compile_creature_seq(Z, names, spec, alloc, demo_seq=None) -> seq_file` — walks the spec: for `group8`, emits 8 sub-seqs — dirs 0–4 composite fresh tiles from rotations N..S; dirs 5–7 reuse dir 3/2/1 tiles with `flags:1` and mirrored x (`chunk.x = -x` pre-negation is handled by the engine — store the same magnitude, flag set, per §1.4); dir-0 gets the flags/sounds, dirs 1–7 get flag copies (harmless, matches stock). Uses tiles-by-number directly for demo-present art (3270, 3284–3289, 3128, 3015/3016, 2714/2715/2720/2721, 2467–2473, 2390–2393, 3254/3255, 3266, 2678, 3117–3120).

7. `compile_creature_seqs(kpf, outdir, demo_grp_path, alloc, verbose)` — top-level: opens STUFF.DAT (for `('demo',)` copies and validation), loops `CREATURE_SPECS`, writes `outdir/wasp.seq`, `roach.seq`, `set.seq`, `queen.seq`, `queenegg.seq`, `scorp.seq`; calls `alloc.force(3126, gib_art)`; returns def rows to append to `write_def`'s assignments. Called from `convert_install` right after `build_actor_tiles`.

8. `verify_seq(path, expect_nseqs, checks)` — reparse own output; assert sub-seq count, nonzero SeqSize at every code-referenced index (wasp {0,9,18,27,28,29}, roach {0,8,16,24,32,42}, set {0,9,18,27,36,45,63,72,74,75,76,77,78}, queen {0,9,18,27,36,45,53,54,55,56,57–60,65–72,73,74,82,90}, queenegg {9,18,19,23,24,25}, scorp {0,8,19,29,45,46,47,48,50,53}), presence of 0x80 on each required trigger frame, all chunk tiles either allocated or in the demo-present whitelist, all `|x−cx|,|y−cy| ≤ 127`, sound names ≤ 8 chars. Round-trip `write_seq(read_seq(bytes)) == bytes` as a self-test on the demo files at startup (already proven for 3 files).

9. `KEX_SOUNDS` additions (edit at :1256): `'rex-bite': ('sounds/queenatk02.wav', 0)`, `'rex_grgl': ('sounds/queenact02.wav', 0)`, `'rex-icu': ('sounds/setquake.wav', 0)`, `'ghost109': ('sounds/setatk03.wav', 0)` — flows to kexsound.txt automatically via the existing writer (:2453).

**Test recipe**: run converter, launch windowed (`-cfg vbtest.cfg`) on a converted map containing each lotag (117 wasp, 103/104 sentry, 111 Set, 113 queen, 112 Selkis); confirm visibility in all 8 view angles, attack trigger effects (sentry bullet, Set soul, queen egg, selkis zap/claw damage), death sequences, and that the demo's own ANUBIS/MUMMY (untouched files) still render — proving global seq/frame/chunk limits weren't blown.

Key file paths: `/Users/alonamir/dev/eduke32/source/exhumed/src/sequence.cpp`, `wasp.cpp`, `roach.cpp`, `set.cpp`, `queen.cpp`, `scorp.cpp`, `init.cpp`, `exhumed.cpp` (:2556/:2577 load order), `/Users/alonamir/dev/eduke32/source/exhumed/tools/kexmap.py`; scratch tooling: `/private/tmp/claude-501/-Users-alonamir-dev-eduke32/8338d716-9146-4e6d-a50c-c933a3d0904b/scratchpad/{seqtool.py, writeseq.py, seqparse.py, parse_set_seq.py, scorp_seq.json, set_seq_dump.txt}`.