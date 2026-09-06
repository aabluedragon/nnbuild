# Powerslave/Exhumed in the VibeBuild32 unified binary

Powerslave/Exhumed support uses the PCExhumed runtime imported from NBlood's
`source/exhumed` tree. It is not a Duke/NAM-family variant, so it cannot reuse
`Duke_app_main` the way NAM, Napalm, and World War II GI do.

The integration follows the existing non-duplicating merged-module pattern:

1. Compile the PCExhumed game objects into `obj/exhumed/`.
2. Prelink them into `obj/exhumed_merged.o` with the generic `MERGE_RULE` in
   `GNUmakefile`.
3. Localize symbols that collide with the already-linked games.
4. Dispatch only the shared engine-facing callbacks from
   `source/vibebuild32/src/gameselect.cpp`.

The exported callbacks are renamed in the imported source:

| canonical engine name | PCExhumed symbol              | file             |
|-----------------------|-------------------------------|------------------|
| `app_main`            | `Exhumed_app_main`            | `src/exhumed.cpp` |
| `G_DefaultDefFile`    | `Exhumed_G_DefaultDefFile`    | `src/common.cpp`  |
| `faketimerhandler`    | `Exhumed_faketimerhandler`    | `src/exhumed.cpp` |

Selection is handled by shared selector metadata:

- `-gm exhumed`, `-gm powerslave`, `-gm power-slave`, `-gm "power slave"`, or
  `-gm pcexhumed`
- `STUFF.DAT` basename inference
- PCExhumed/Powerslave full and demo data-file signatures from
  `source/exhumed/src/grpscan.cpp`

Future Build-engine game additions should follow the same rule used here and in
the Duke/NAM-family selector entries: reuse an existing runtime when it already
supports the game, and add a new merged module only when the game has its own
runtime that cannot safely run inside an existing one.

## Bring-up fixes

Three defects kept a Powerslave/Exhumed launch from reaching gameplay in the
unified binary:

- **No ART loaded at all.** `InstallEngine()` called
  `artLoadFiles("tiles%03i.art", ...)`. NBlood's engine formats that string;
  eduke32's `artGetIndexedFileName()` instead substitutes digits *positionally*
  into a literal `tilesNNN.art` name, so the format string became
  `tiles000i.art` and no ART file was ever opened. Every `tilesiz` stayed 0x0,
  so nothing drew, and `PlayMovie()` then crashed writing the first frame
  through a null `waloff[kMovieTile]`. The call now passes the literal
  `"tiles000.art"`, exactly as the Blood and RR imports were fixed.
- **First-run hang on the startup window.** `Exhumed_app_main` gated
  `startwin_run()` on `readSetup < 0` regardless of `-nosetup`, so a fresh
  config blocked on a setup dialog this build does not ship for Exhumed. The
  first-run term now respects `g_noSetup`, matching SW, Blood and RR.
- **No way to verify a run without a human at the keyboard.** `GameDisplay()`
  honours `VB_CP_CAPTURE=<frames>`, the same debug hook SW and the Duke runtime
  use: it saves one screenshot that many drawn frames in.

Verified on the Powerslave **demo** (`STUFF.DAT`, 15904838 bytes, CRC
`0x1D8C7645`): logo -> title screens -> `BOOK.MOV` -> menu all run, and
`-1` drops straight into LEV1 with correct geometry, sprites, weapon and status
bar.

Known demo-data limits (not bugs): the demo ships no CD audio, so
`playCDtrack()` logs `Error opening music track NN` and plays nothing.

### Running it

```
./vibebuild32 -gm exhumed -g /path/to/powerslave/STUFF.DAT -nosetup
```

`-gm exhumed` is optional when the `-g` path names `STUFF.DAT` -- the selector
infers the game from the basename and from the full/demo data-file signatures.

## Multiplayer

PCExhumed shipped with its network layer stubbed out -- `network.cpp` was a set
of empty function bodies, and nothing of the original PowerSlave serial/IPX code
survived the port. So Powerslave multiplayer here is new code
(`source/exhumed/src/vbnet.cpp` / `.h`) built on the same VibeBuild32 stack the
other games use.

What made it tractable is that the *game-side* multiplayer scaffolding was never
removed: `nNetStartSprite[]` is already filled from map sprites with lotag 62,
`RestartPlayer()` already spawns at those starts and paints the per-player floor
marker, `PlayerList[]` / `nPlayerScore[]` / `playerNames[]` are already
per-player, and `LogoffPlayer()` already tears a player down. Only the transport
and the input exchange were missing.

### Architecture

**Transport** is mmulti (`source/build/src/mmulti.cpp`) -- the same reliable,
ordered, dual-stack IPv4+IPv6 UDP channel Duke and Shadow Warrior use, so `-net`
peer specs, the masterserver, the NAT hole-punch and the file transfer behave
identically here. In mmulti's master/slave mode a slave-to-slave message is
wrapped in a proxy header and fanned out by the master, exactly as SW does it;
in P2P mode the wrapper is skipped.

**Simulation** is deterministic lockstep with a fixed input delay, which is a
natural fit: Exhumed's sim is one `GameMove()` per tick reading
`sPlayerInput[0..N-1]`, so there is nothing to reconcile. Each machine publishes
its own `PlayerInput` for a tick `EXNET_INPUT_DELAY` (3 ticks, 100 ms at 30 Hz)
ahead of the one being simulated, and no machine runs tick T until it holds
every live player's input for T. Publishing is driven by *simulation* progress,
not by the wall clock, so the pipeline is self-regulating: a stalled peer stops
us producing too, and nothing runs away.

Every machine meets at a ready barrier before tick 0 of each level
(`ExNet_LevelStart`), so the tick streams all start from the same instant. The
barrier counts level starts rather than latching a flag: a peer's READY
routinely lands *before* this machine gets to the barrier (the joiner drains the
host's file stream first, and the host announces while the joiner is still
loading ART), and a flag cleared on entry threw that packet away -- the joiner
then sat at the barrier until the host's watchdog dropped it. READY carries a
generation instead, and a peer counts as ready once its generation catches up.

**Desync detection** stamps a CRC over exactly the state the shared input stream
is supposed to determine -- every player's position, angle and health, plus the
RNG cursor (`RandomGetSeed()`) and `totalmoves` -- after every tick, and
publishes it every 8 ticks. A mismatch logs `Out Of Sync` once and shows it on
the HUD. `VB_EXNET_DEBUG=<ticks>` traces the lockstep cursor (tick, CRC, publish
cursor, local position, health): without it a clean "no desyncs" result proves
little, since a sim that never advanced also never disagrees.

**Leaving.** A clean quit broadcasts the leave with the tick it takes effect on,
so every machine drops the player at the same instant. A client that *crashed*
never publishes another input, and lockstep would otherwise wait on it forever;
the master notices the silence after 12 s and broadcasts the eviction with an
agreed tick, so the survivors stay identical.

**Chat** (`T` to open, Enter to send, Esc to cancel) is deliberately outside the
tick stream: it travels on the same reliable channel but never reaches
`sPlayerInput` or any simulated state, so a message can land on different ticks
on different machines without costing a bit of determinism. The prompt is a
per-frame state machine rather than the blocking text entry the save menu uses,
because a blocking prompt would stall this machine's tick stream and leave every
peer waiting on it. The same line can be sent from the console with
`say <text>`, which is the only way a headless dedicated server -- which has no
HUD -- can talk to its players.

**Player names** are set by `-name` locally and by each peer's `OPTIONS` packet,
during the connect, well before the first `RestartPlayer()`. That function used
to overwrite them unconditionally with the stock `JOEn` placeholder, so every
level start renamed everyone; it now only fills a name in when the netcode has
not supplied one.

**Host->joiner file transfer** (`--netfile`, and the user map when one is named)
has two constraints that are easy to get wrong:

- The receiver mounts a completed GRP immediately, so a joiner has to have every
  file *before* `InstallEngine()` reads the ART -- otherwise the mod is mounted
  after the tiles are already loaded and changes nothing. The host therefore ends
  the stream with an explicit "files done" packet and the joiner blocks on it
  (`ExNet_ReceiveGameFiles`).
- mmulti's per-peer reliable ring holds 512 unacked entries and a file is sent as
  one packet per 1 KB chunk with no natural pacing, so the sender drains the
  window before every chunk (`EXNET_RELIABLE_WINDOW_MAX`, the same shape as
  Duke's `Net_DrainReliableWindow`). Without that, a 2 MB `--netfile` did not
  even deliver its header: the ring wrapped, unacked chunks were overwritten,
  and the joiner sat waiting until the master's watchdog dropped it.

**Local input** is gathered once per *tick*, not once per game-loop pass, and
that distinction is the whole reason turning works. `PlayerInterruptKeys()`
accumulates (`nPlayerDAng += q16avel`) at 120 Hz with the keyboard's share
landing only on every 4th call, and `GetLocalInput()` zeroes the accumulator as
it copies it out. The loop spins about ten thousand times per published tick, so
gathering unconditionally threw away all but a ten-thousandth of the turn input
-- the player could walk and shoot but not turn, with keys or mouse. The branch
now asks `ExNet_WantsInput()` first, which is the same tick-boundary gate
single-player uses. (`Exhumed_faketimerhandler()` also has to be called
explicitly at the top of that branch, before `bInMove` is set, or
`PlayerInterruptKeys()` never runs there at all.)

**Roster** is host-authoritative before the first level loads. A `-net /n0:4`
host using `--startnow --nplayers 2`, for example, starts a two-player
simulation when one client is present: the two unused transport slots are not
players, do not receive file-stream traffic, and cannot hold the ready barrier
or input lockstep open. Game-player indices are compact while wire packets keep
their stable mmulti slot numbers. By default that roster remains fixed for the
session. A host that adds `--latejoin` may admit an unused slot mid-level: it
first waits for the client to mount its game files, freezes every active player
at one named lockstep tick, streams a separate all-player `EXSN` world image,
then creates the compact new player on every machine and restarts the input
pipeline with shared idle delay ticks. The old level never reloads on the
joiner, so it enters the same world rather than a parallel copy of it. A slot
whose player left or timed out may also rejoin through that same handoff: its
compact player record and score stay at the original index, its hidden old body
is deterministically replaced with a fresh multiplayer spawn, and the returning
client resumes that exact slot. The `EXSN` image includes all reusable
body/chunk/gun allocation pools as well as the visible world, preventing the
staging level from changing the rejoin spawn allocation.

### Options

Every VibeBuild32 multiplayer flag (`source/glue/netargs.h`) is accepted, with
the same meaning it has in the other games:

| Flag | Effect |
|------|--------|
| `-net /p<port> /n0:<N>` | host, listening on `<port>`, for `N` players |
| `-net /p<port> /n0 <host>:<port>` | join `<host>` |
| `--advertise` / `-ad <master>` | register this listen server with a masterserver |
| `--lobbylist` / `-ll <master>` | print what a masterserver is advertising |
| `--joinlobby` / `-jl <master>` | query a masterserver and join the first server it lists |
| `--netfile` / `-nf <path>` | (host) stream this file to joiners before the level loads |
| `--headless` / `-hl`, `--dedicated` / `-dd` | windowless dedicated server with a stdin console |
| `--nplayers` / `-np <N>` | start once `N` players are connected |
| `--startnow` / `-sn` | start with the present players; remaining slots are safely reserved |
| `--latejoin` / `-lj` | host opt-in: promote a reserved slot mid-level using freeze + `EXSN`; requires `--startnow` and a free slot |
| `--wait` / `-w` | dedicated: hold in the lobby instead of auto-starting |
| `-name <name>`, `-color <0-7>` | player identity |
| `-punchto <addr>`, `-masteraddr <addr>` | client NAT hole-punch |

Every flag above is accepted with one dash or two (`-name` / `--name`,
`-joinlobby` / `--joinlobby`), and `--flag=value` works as well. This matters
more than it looks: `--joinlobby` used to fall through to the shared
`VB_NetArg()` handler, which accepted and silently swallowed it because Exhumed
passes `NULL` for that hook, so the canonical spelling of the flag did nothing.

`--lobbylist` prints the listing and then continues launching the game, exactly
as it does in the other games; it is not a query-and-exit tool.

A networked launch skips the title sequence and the menu and goes straight to
level 1 (or to `-<n>` / `-map <file>` if given).

### Local test recipe

```
# four transport slots, but start the two-player game as soon as one client is in;
# --latejoin makes the remaining slots eligible to enter this running level
./vibebuild32 -gm exhumed -g <STUFF.DAT> -nosetup -name SERVER \
    -dedicated -startnow --nplayers 2 --latejoin -net /p23513 /n0:4

# client
./vibebuild32 -gm exhumed -g <STUFF.DAT> -nosetup -name PLAYER \
    -wnd -res640x480 -net /p23514 /n0 127.0.0.1:23513

# after the first two are already moving, this client is frozen/snapshotted in
./vibebuild32 -gm exhumed -g <STUFF.DAT> -nosetup -name LATE \
    -wnd -res640x480 -net /p23515 /n0 127.0.0.1:23513
```

Confirm with `VB_EXNET_DEBUG=120` on both that the tick numbers advance and the
CRCs at equal ticks are identical, and that neither log contains `Out Of Sync`.
For a late join, all three logs should report the same CRC again from rebased
tick 0 after the host logs its `EXSN` stream and each log reports that the
transport slot was promoted.
To test a rejoin, close one original client, wait at least 12 seconds for the
host to log its deterministic timeout, then start a replacement client against
the same host. Its transport slot is reclaimed, rather than appended as another
game player; the host and returner should again have equal CRCs from rebased
tick 0 and neither log may contain `Out Of Sync`.
For an all-player network-state round trip on the host, set
`VB_EXNET_SNAPSHOT_FILE=/tmp/exhumed-world.exsn`; at tick 120 it writes an
`EXSN` image, deliberately corrupts both active player records, reloads it, and
logs `EXNET snapshot test: PASS` before lockstep continues. The ordinary
`VB_KEXSAVE_TEST=1` path remains the separate `EXSV` campaign-save format.

Masterserver / lobby path, all on one machine:

```
# 1. the standalone lobby + STUN server (no game, no data files)
./vibebuild32 -masterserver 23519

# 2. an advertised dedicated Powerslave server, offering a mod file to joiners
./vibebuild32 -gm exhumed -j <demo dir> -nosound -nomusic -headless \
    --dedicated --advertise 127.0.0.1:23519 --nplayers 2 --netfile MYMOD.GRP \
    -name HOSTY -net -n0:2

# 3. see what the masterserver is advertising
./vibebuild32 -gm exhumed -j <demo dir> --lobbylist 127.0.0.1:23519

# 4. join whatever it lists -- no address typed anywhere
./vibebuild32 -gm exhumed -j <other dir> -name JOINER --joinlobby 127.0.0.1:23519
```

The server's stdin is a console, so `say hello` on the host's terminal shows up
on the joiner. `-n0:N` is mmulti's own syntax (master/slave, `N` players);
mmulti takes `-` or `/` for its own flags, and a bare address with no `/n#`
defaults to `/n0`, which is what `--joinlobby` hands it.

### Not yet supported

- **Arbitrary roster reshuffles.** `--latejoin` can reclaim the same departed
  transport slot and compact player record, or append a previously unused slot.
  It does not replace one live player with another, compact the roster after a
  departure, or reorder players mid-level.
- **In-menu server browser.** Exhumed has no such menu; `--joinlobby` covers
  discovery and joining from the command line.
- **The demo's own maps have no deathmatch starts.** `ExamineSprites()` falls
  back to the single-player start for everyone when a map has no lotag-62
  sprites, so on the demo levels players spawn together.

---

## The Nightdive remaster (Kex engine)

*PowerSlave Exhumed* (Nightdive, 2022) is not a Build game at all: it is a Kex
engine title with its own map format, its own PNG art and none of the original
data. It is also not the same game as the one this port plays -- the remaster is
built from the **console** (PSX/Saturn) version, whose 25 levels are a different
campaign from the DOS Build version's.

What makes a bridge possible is that Kex levels are close cousins of Build
levels. From Nightdive's own level-editing document: *"The levels in Powerslave
are very similar to BUILD engine games... only one ceiling and floor can exist
per sector and walls are always fully vertical."* Sectors are convex, walls are
vertical, portals connect sectors. So the geometry converts.

Actor placement keeps the same distinction as the runtime: enemies are handed
to Build constructors at their sector floor, while scenery, pickups, and travel
objects retain the remaster's authored Z. That matters for wall torches, lamps,
and pedestal items whose origins are well above the floor.

`source/exhumed/tools/kexmap.py` does that conversion:

```
source/exhumed/tools/kexmap.py "/path/to/PowerSlave Exhumed" ~/games/powerslave \
    [/path/to/powerslave_demo]   # with STUFF.DAT: rewrites ARROW.SEQ and SET.SEQ too
source/exhumed/tools/kexverify.py "/path/to/PowerSlave Exhumed" ~/games/powerslave

# Optional: include the converter's focused regression maps as KEX_TEST_*.MAP.
source/exhumed/tools/kexmap.py --testmaps "/path/to/PowerSlave Exhumed" ~/games/powerslave
# The verifier must use the same scope as the conversion.
source/exhumed/tools/kexverify.py "/path/to/PowerSlave Exhumed" ~/games/powerslave --testmaps

./vibebuild32 -gm exhumed -j ~/games/powerslave \
    -kex "/path/to/PowerSlave Exhumed" -map THOTH.MAP
```

It writes one Build `.map` per level plus an `exhumed.def`. `-kex <dir>` mounts
the install's `PowerslaveEX.kpf`, which is a plain zip the Build engine reads
natively, so the levels are drawn with the remaster's **own** PNG textures --
nothing is copied out and no palette reduction happens. You still need the Build
PowerSlave data (`STUFF.DAT`) for the HUD, weapons and sounds: this brings the
remaster's levels, not a whole game.

`--testmaps` is intentionally opt-in: normal conversion remains the 25-level
retail campaign, while the extra focused maps support converter and runtime
regressions.

Kex teleport pads are bridged natively: event 71 is the stepped-on pad and
the first matching event 72 supplies its destination. The player arrives at
the source floor triangle's centre with their existing heading intact, exactly
as Kex's `TeleportEvent()` does; `VB_KEXTELE_TEST=1` exercises that path on
`KEX_TEST_TELEPORT.MAP` without input.

All 25 retail levels convert, keeping 97.7% of their sectors.

### The map format

Nightdive ships `.dmap` text sources alongside compiled `.map` files for the
sample levels in `docs/examples/`, which is what made the binary format
tractable -- every field below was checked against the matching text source.
The retail levels ship compiled only.

```
header   'PSM\x01' + 8 x u32: ntex, nvert, nsector, nface, ntile,
                               nuvquad, nevent, nactor
strings  u16-length-prefixed: sky texture, then the texture table
verts    nvert x 16: float x, y, z + RGBA (baked vertex lighting)
sectors  nsector x 20: u16 faceStart, u16 wallFaceEnd, u16 ?, i16 ?, i16 ?, u16[4] 0, u16 flags
         The third word is 128 on most levels and a per-level constant
         elsewhere (KILENTRY 89, TOMB 91, MARSH 96, SELBUROW 108, MAGMA,
         SELPATH, SLAVCAMP and SUNKEN 112) with a handful of sectors
         differing inside a level -- always the last few of the level,
         KARNAK's 420-423 of 424, MAGMA's 537-539 of 540: later additions
         carrying a newer editor default. It does not track the vertex
         light, and it is tooling residue rather than anything drawn. The fourth and fifth read as
         signed heights. The last word is the sector flag word (256
         submerged, 16 wading, 32 no standing on edges).
         faces[faceStart..wallFaceEnd] are the walls; wallFaceEnd+1 is the
         ceiling face and wallFaceEnd+2 the floor face
faces    nface x 32: i16 tileStart, i16 tileEnd (-1 = untextured),
         u16 vertexStart (4 corners), i16 sectorLink (-1 = solid),
         float angle, float nx/ny/nz, u16 flags,
         i16 moveGroup (-1 = static), i16 tileVertStart, i16 tileVertEnd
tiles    ntile x 16: u8[4] local geometry-vertex indices,
         i16[4] UV-pool indices, u16 textureIndex, u16 pad
uvquads  nuvquad x 32: four float (u, v) pairs; tile UV indices address the
         flattened float2[4*nuvquad] pool
events   nevent x 8: u16 type, u16 sector, i16 tag, i16 params
actors   nactor x 20, at the very end: u16 type, i16 sector, i16 x, i16 y,
         i16 z, i16 tag, i16 param1, i16 param2, float storedAngleDegrees
```

Two things are worth knowing before touching the reader:

- **The string table's length is ambiguous.** An empty sky and the empty
  "no texture" slot are indistinguishable, so the entry count is `ntex+1` for
  some maps and `ntex+2` for others. Rather than guess, the reader locates the
  vertex array by *validating* the face table that follows it: face tile ranges
  must partition `0..ntile-1` in order and every link must name a real sector.
  The tile UV indices are validated too. That is a strong enough check to pin
  the offset, and it parses all 40 PSM maps Nightdive ships (25 retail and 15
  tests).
- **A sector's wall faces are not stored in loop order.** They have to be
  chained end-point to start-point to recover the polygon. Taking them as given
  produces sectors whose walls do not join, which Build loads without complaint
  and then renders as nothing.

### What converts, and what does not

Converted: sector and wall geometry, floor and ceiling heights, portals between
sectors, wall/floor/ceiling textures, the player start and facing, and the
enemies and pickups.

**Scale.** Kex and Build PowerSlave are the same spaces at different sizes, and
the player walking these levels is Build PowerSlave's: eye height 14080 z units
(880 world), collision radius 232. Kex authors a corridor 256 units wide and 512
tall, so converted 1:1 the player's eye sits *above the ceiling* and its radius
nearly fills the corridor -- which is why the first level rendered as a smear of
wall. The factor is measured, not guessed: across Exhumed's own LEV1 and the
converted TOMB, median sector height is 3200 vs 512 (6.25) and median wall
length 1032 vs 181 (5.7). Two independent measures of one ratio, so `WORLD = 6`;
afterwards the converted medians are 3072 and 1086 against the original's 3200
and 1032.

**Texture scale.** Build shows exactly `xrepeat*8` texels across a wall, so
xrepeat follows the wall's *length* -- a fixed value smears every wall by the
ratio of its length to the tile. The original game runs about 20.4 world units
per texel (measured from its own shipped xrepeat values); at `WORLD = 6` a Kex
texel covers 6, which is finer, as it should be. `yrepeat` is a vertical scale
and does not depend on length: the original pairs yrepeat 7 with its 20.4 units
per texel, which first pinned the constant at ~143 and yrepeat at 24; measured
across the 13150 walls of the shipped levels the constant is 128, and yrepeat
is 21 (see `KEX_YREPEAT`).

**Lighting.** Kex bakes its lighting into vertex colours, greyscale in these
levels (measured: colour spread 0 at both the median and the 90th percentile),
so it maps onto Build's shade with nothing lost. The catch is *which* vertices:
a face's four corner vertices carry a flat 32 on every face in the level, while
the tile vertices span 25..255 with a median of 58. Averaging the corners gives
every surface an identical shade -- the flat, unlit look. The curve is a square
root, because Build's shade table darkens fast and a linear map of a median-58
level puts most of it where nothing is legible.

**Slopes.** Build tilts a sector about its FIRST WALL: height varies with the
perpendicular distance from it, `z = z0 + heinum*perpDist/256`. Only the
component of the plane's gradient perpendicular to that wall survives -- so the
converter rotates each sector's wall loop to start on the wall most
perpendicular to the slope, which makes most of them exact. That is safe because
nextwall is matched by coordinate afterwards, not by index.

When no existing wall represents the plane closely enough, `_split_slopes()`
cuts the polygon along a contour, then lets each piece choose its own first
wall and slope. Floors and ceilings that tilt in different directions can be
split repeatedly, and terrain steeper than Build's signed `heinum` is emitted
as narrow sloped terraces. Every surviving piece is checked against Build's
integer slope formula; a residual piece is flattened only when another split
would not materially reduce the error.

For a terrace whose source gradient exceeds the signed 16-bit `heinum` cap,
the capped tilt is vertically centred over its strip instead of being anchored
to one seam. This minimizes the strip's worst height error; COLONY_SAT's two
416-z-per-world-unit crater floors now stay within 1152 Build z at the
verifier's interior samples (rather than the previous 1536 bound).

**Lighting is calibrated against the remaster, not eyeballed.** Frames from a
playthrough of TOMB were sampled over the 3D viewport and compared with the same
view here. The level was rendering about 2.3x too bright, and matching only the
median produced a *bimodal* image -- blown-out near walls, a third of the frame
pure black -- whose average happened to be right. What has to match is the
distribution:

```
reference    p05 20   p25 30   p50 37   p75 47   p95 81   black  0%
median-only  p05  0   p25  0   p50 39   p75 82   p95 108  black 29%
```

The shade band is therefore compressed and lifted off zero (`KEX_SHADE_MIN` /
`KEX_SHADE_SPAN`) rather than spanning the full ramp. The endpoints come from a
measured response of shade to rendered luminance -- 17 -> 85, 25 -> 75, 47 -> 39,
105 -> 0 -- which is flat at the top and then falls off a cliff, nothing like the
linear ramp that reasoning from Build's palette suggests.

Two cautions for anyone re-running this. eduke32 caches processed textures in
`texturecache`, so changing a PNG under an existing tile number does nothing
until that file is deleted -- an earlier round of these numbers was measured
against a stale cache. And `-res` does not take effect here; every capture came
out 1470x956 regardless, which briefly made a resolution test look meaningful
when it was comparing identical renders.

**The noise band, and how not to debug one.** Converted levels showed a band of
RGB noise along the top of the screen -- neighbour-pixel roughness 248 against
the stock game's 0. It was blamed in turn on the sky ceilings, the tile aspect,
`texturecache`, resolution, parallax, ceiling heights, the 1D cache size and a
filename collision with the .kpf. All wrong.

The actual cause was the per-level music. This block runs every frame until it
reports success, and a failed attempt left `cdSuccess` false -- so the game
re-read a 14 MB ogg and allocated a fresh buffer *on every frame*, and that
thrash scribbled the frame. The original `playCDtrack()` never showed it only
because the track files it asks for do not exist, so it fails at the open and
costs nothing.

Two things made it look like a map bug for far longer than it should have. The
figure was bit-identical, 248.3, through every change -- which reads as "nothing
you are changing is involved", not "try a different map feature". And the
bisects were confounded: byte-identical map data rendered clean under one name
and broken under another, because the *name* decided whether `kexmusic.txt` had
an entry and therefore whether music was attempted at all.

The guard is on `bNoSound`, not `MusicToggle`: this port never implements
`-nomusic`, so `MusicToggle` is always 1 and guarding on it guards nothing.

**Sky.** Build will not treat a tile as a sky panorama until `tileSetupSky()`
has seen it, which is what the DEF's `multipsky` does. Without it a parallaxed
ceiling draws black however the tile is prepared -- which is why two earlier
attempts at this concluded, wrongly, that parallax simply did not work here and
fell back to painting the sky flat on the ceiling plane. With `multipsky` the
sky sits at the horizon properly: p50 goes from 20 to 63 and p95 to 150 against
the remaster's 162.

Kex has no sky tile: the sky is named in the map header and a ceiling
simply carries *no tiles* where it shows through. Untextured ceilings were being
handed texture slot 0 -- 169 of them in KARNAK alone -- so its courtyards were
capped with garbage. They now get the map's own sky with Build's parallax bit.
The same holds for untextured *floors* and *solid walls* -- KILARENA alone has
127 floors, 165 ceilings and 80 walls with no tile -- which had been wearing
whichever texture the map listed first. They wear the sky now too, the floors
parallaxed like the ceilings. (A census that counted these on the Build side
was briefly reporting the writer's own bug: the force-field table loop had been
spliced into the middle of the sector remap, so floors were only remapped as a
side effect of it, for one sector, on the seven levels that have force fields.
KARNAK's sand and KILARENA's teal deck are back.)

**Steep slopes.** Kex maps a tile along its face; Build projects a floor or
ceiling texture onto the horizontal plane, so a slope stretches it by 1/cos of
its angle -- 2.2x on the 63-degree machine panels flanking KILARENA's entrance,
which rendered as grey and teal noise. Relative alignment (stat bit 64) maps the
texture along the slope from the sector's first wall, and the converter already
turns that wall to the slope's contour line, so the texture runs up the slope
unstretched: the panels are `colony152`'s rings again. It is set on slopes of at
least 45 degrees (`STEEP_HEINUM`, 520 planes across the levels, 330 of them
over 63 degrees) and left off gentler terrain, where world alignment tiles
seamlessly across the many small pieces and the stretch stays under 1.4x.

**Which band a wall wears, and where its texture starts.** Build draws a
two-sided wall in two parts and hands them to different walls: the part above
the opening wears *this* wall's picnum, the part below it -- a step's riser --
wears the *next* wall's, the one on the far side (polymost's "under" pass
takes `nwal->picnum` and `nwal->ypanning`). Kex keeps each band with the
sector it faces: the lintel over a doorway is a solid face of the room, the
riser of a step a solid face of the lower sector, and the step's upper side
has nothing on that edge but the portal. The converter took the first solid
face on the edge, or on an all-portal edge the portal face itself, so every
riser wore whichever face came first -- and, once untextured faces meant the
sky, the sky: 35,000 portal walls across the levels carried the sky tile.
`_wall_bands()` runs after the walls are paired and gives each wall its own
sector's band above the portal if there is one, else the far sector's band
below it (518 portal walls are left with the sky, edges with no band on either
side, where no part is ever drawn).

The same pass fixes the texture origin. The compiled UV pool establishes the
ordinary world period as 128 Kex units per 64-pixel texture. Build hangs a
texture from a reference height -- the ceiling for a solid wall, the far
ceiling for the part above an opening, the near floor for a riser -- and from
the wall's first point. `ypanning` (256ths of the tile) and `xpanning` (texels,
on the axis-aligned walls; a diagonal's Kex mapping is not this simple) put
those seams back on the 128-unit grid; a wall drawn in both parts has one
`ypanning`, and the taller part wins -- unless the wall is masked, when the
grate or force field is the part that shows and takes it. One more thing
`xpanning` absorbs: `xrepeat` shows a whole multiple of 8 texels, while Kex
edges need half a texel per source unit. The texture is shifted by half the
rounding error so each end carries half.

**Flats at Kex scale.** Build maps a floor or ceiling texture at a fixed 16
world units per texel (polymost's `ft *= 1/16`; 8 with the expand bit) -- a
64-texel tile repeats every 1024 units, 171 Kex units, where the remaster's
compiled UV mapping repeats every 128. That is 1.33x too coarse. No field
changes that scale, so a flat's texture gets a *second* tile of
128*WORLD/16 = 48 texels a
side -- the image box-filtered down to that, written to `kexart/flats/`, so
the classic 8-bit renderer, which never sees a hires replacement, still draws
the floor -- with the full PNG as its hires replacement: the tile's footprint
sets the repeat (48 x 16 = 768 units = 128 Kex units) and under polymost the
image keeps every pixel (`KEX_FLAT_TEXELS`, `fassign`; 373 across the levels,
animated ones as consecutive runs with their own `animtilerange`). Walls keep
the original tile, and sectors are remapped through the flat table instead.
Proved on a probe first -- MAGMA's start with its floor on a `dummytile` of
that size, next to the same view on the old tile -- and checked in both
renderers (`ScreenBPP = 8` for classic).

**Mosaic faces and the UV pool.** Nightdive's level-editing document (shipped
in the install's `docs/`) describes a face as a mosaic of tiles, with multiple
textures allowed on one face. The compiled map keeps that: each tile record's
`u8[4]` select local geometry vertices, its `i16[4]` select four float2 values
from the UV pool, and it names one texture. The retail loader reads that sixth
header field as groups of eight floats, the mesh builder copies the selected
values directly into each vertex, and `SlaveMain.shader` samples them without
another scale. Across 1,335,110 usable tile edges in the retail maps, every
map's median is exactly one 64-pixel repeat per 128 source units. That field was
previously mislabeled `nzone` and ignored; inferring one texel per source unit
made all converted wall and flat art twice as dense. It is now parsed and
validated as `nuvquad`, and the converter's repeats, panning period, long-wall
limit, flat tile footprint and composite sampling use the compiled 128-unit
period. 9,376 of
the retail levels' 24,706 textured wall faces hold two or more textures -- a
trim band over a main texture is the usual shape -- and the converter had
been keeping the first and dropping the rest. A Build wall has one texture,
so a face that shows one texture at every height (the cells of a row need
not line up: cave walls stagger them) now gets a *composite* tile: 64 texels
wide, one repeat that the wall mapping repeats across, and as tall as the
face, each row copied from its texture in world phase (`_face_rows`,
`_bake_composites`, `kexart/comp/`). It hangs from the face top with no
`ypanning`: on a solid one-sided wall under a flat ceiling over a flat floor,
where the Build wall and the face coincide; on the band above a doorway,
which Build draws from this wall's own ceiling when cstat 4 is set; and on a
step's riser, which Build draws from the far floor with the *near* wall's
texture -- each only when that wall's picnum serves no other part. 7,223
converted walls carry one (5,797 solid, 695 lintels, 731 risers), 866
distinct patterns, 6 MB of PNG. Faces that show two textures at one height
(5,629, tall cave walls mostly) keep their first texture; a capped 2D set of
the small ones would be 1,589 faces in 1,521 patterns and 120 million texels,
not worth it. A switch is one cell of such a face too: 11 of the 69 switches
were buried under a composite until `_place_switch()` learned to drop it --
the wall must carry the OFF tile, which the game flips to the next one. And a
composite is still art: a face with an animated texture in it (19 faces, lava
and water bands) keeps its plain tile so it keeps moving. Two more details:
`yrepeat` is an integer, 11 for the exact 10.67, so a repeating texture is
slightly compressed; composites are baked at that ratio and the whole face
still lands on the whole wall. And a two-sided wall whose picnum serves both a lintel above the
opening and the far riser below it can show only one texture: 96 such walls
across the levels differ, and stay as they were.

The 655 mosaic *flats* get composites too (`_flat_cells`,
`_bake_flat_composites`, `kexart/fcomp/`): a tile the size of the face's
bounding box at the flats' 48 texels per 128 units, each texel sampled from
the cell under it in world phase, so it joins seamlessly onto the plain
flats around it. Build maps a flat from the world origin -- measured with a
four-quadrant probe tile on MAGMA's start: u = x/16, v = -y/16 in Build
units, the classic renderer's `-globalposy` -- and the flat's panning only
reaches half a large tile (its unit is 2^floor(log2 width)/256 texels), so
the composite is baked already shifted onto that grid: its period is the
bounding box, so the wrap is seamless and no panning is needed. Level flats
only; 407 floors and 183 ceilings in the converted levels, 278 patterns,
1 MB.

**Long walls.** `xrepeat` follows a wall's length, one texel per two Kex units,
and is an 8-bit field. The corrected limit is 4080 source units and no retail
edge exceeds it. `_split_long()` remains for custom maps: it cuts an over-limit
edge into equal pieces after the T-junction pass, from the lexically smaller
end so both sides of a portal edge still pair by coordinate.

**Animated surfaces.** `defs/animPicInfo.txt` lists each animated texture's
frames and speed. Build plays an animation as a run of *consecutive* tiles, so
tile numbers can no longer be handed out as levels are read: the converter now
parses every level first, then assigns tiles, placing an animated texture's
frames directly after it and emitting `animtilerange`.

**Music.** Every level names its own track in `defs/mapInfo.txt` and the OGGs sit
beside the .kpf, so `-kex` puts the install directory on the search path and the
converter writes the pairing to `kexmusic.txt`. Falling back to the CD track
numbering would play whatever the *original* game's level N used, which is a
different level entirely.

**Facing.** The actor record's float at offset 16 is not the angle the text
source shows -- the source's angle in radians is `radians(90 - stored)`, checked
against every sample map with a turned actor. That source angle is a compass
heading (clockwise from +y), so after the y-flip the whole chain collapses to
Build angle `= -stored`.

**Authored sectors.** The signed word at actor offset 2 is the source sector
index. The retail loader preserves it and passes the complete 20-byte record to
the actor factory. A purely geometric lookup disagreed for 370 of the 4,923
retail actors, chiefly wall traps and actors on stacked/shared boundaries. The
converter now keeps the source identity and uses position/height only to select
one of its contour-split descendants. Wall emitters are moved just inside that
descendant along whichever face-normal direction enters it; that direction is
also used for firing, avoiding a reversed face winding sending the projectile
into the wall. The census resolves all 4,910 on-map records to their authored
source sector; the other 13 are deliberately off-map editor/script records.

Actors ride on a happy accident of Build PowerSlave's design: `ProcessSpriteTag()`
spawns everything from a map sprite's **lotag** -- 100..118 are creatures,
6..60 are items -- so a Kex actor converts to a plain sprite with the right
lotag and the engine builds the rest. The two rosters are cousins rather than
twins (the remaster is the console version, and not every console enemy has a
Build counterpart), so `ACTOR_MAP` maps what genuinely corresponds and omits the
rest rather than substituting something wrong. Verified by counting what the
engine actually spawned: KARNAK 88 creatures / 103 live sprites, SLAVCAMP
125 / 161.

Not converted:

- A sector whose walls do not make a single closed ring. That used to be 226
  of 9837 across the retail maps and is now none of 11329: the ring walk
  prunes what hangs off the ring -- a chain to nowhere, or a chord across the
  room (a partition wall, a doubled face) meeting it at over-visited points,
  shortest first and only while the graph stays in one piece -- and rejects
  a ring that crosses itself rather than emit it. The chord is a free-standing
  wall Build could not hold anyway; the room it was hiding is kept. The last
  two were float noise, corners at -3456.01 beside -3456.0, which snapping the
  ring's points to a tenth of a unit closes.
- **Independently oriented floor and ceiling slopes.** Build tilts both
  surfaces in a sector about the same first wall.  When Kex gives a room a
  floor and ceiling whose gradients need incompatible axes, no single Build
  sector can reproduce both exactly.  The converter contour-splits first and
  retains the lower-error representation for each residual surface; one may
  remain flat at its mean rather than making the other surface materially
  worse.  Extremely steep terrain is represented as capped sloped terraces.

### Rebuilding a sector's wall loop

Kex stores a sector's wall faces in no particular order, so the loop has to be
rebuilt by walking end-point to start-point. Three things about the source
broke that walk, and between them they were throwing away 230 sectors across the
retail maps -- 106 of MINES' 605, a fifth of the level.

**A face's first two vertices are not consistently wound.** One side of MINES
sector 16 is built from ten faces along y=1152, and the same span
(544,1152)-(640,1152) appears in one face left-to-right and in the next
right-to-left. Keying the edge on the directed pair made those two different
edges and the walk dead-ended. Edges are now keyed by the undirected segment and
the ring is walked over a point adjacency, which picks the direction.

**The first two vertices are not always the horizontal edge.** On some faces
they are the vertical one, so they project to a single 2D point. Those were
skipped as degenerate, which silently dropped a whole side. A wall face is a
vertical quad, so the segment now comes from the widest-apart pair of its four
projected corners.

**A side can have no face at all.** MINES sector 17 is a rectangle with three
faces: nothing is ever drawn on the fourth side, so the source does not store
one. 90 of MINES' sectors are shaped like that. An open chain with exactly two
loose ends is now closed with a solid wall textured from the sector's floor.

Sector retention went from 97.7% to 99.7%, and MINES from 499 sectors to 604.

### Portals, and the T-junctions that were eating them

Build pairs a portal by exact edge correspondence: sector A's wall from P to Q
is a portal into B only when B has a wall from Q to P. Kex has no such rule, so
it happily runs one long wall alongside a neighbour whose side is broken into
several -- a T-junction. When that happens the reversed edge is never found and
the portal is dropped, which turns a doorway into a solid wall.

It was not a corner case. 978 of the 10602 portals in the first eight retail
maps -- 9.2% -- were being lost exactly this way. The converter now splits every
sector edge at the vertices other sectors put on it, with the vertices bucketed
into a grid so each edge only tests the ones near it. That recovered 1044
passages. After the later slope and lighting splits the largest retail map is
5118 walls, still well inside Build's 16384-wall limit.

The remaining case was edges facing several *different* neighbours stacked
vertically -- a shaft can open onto one sector high on the wall and another low
on the same wall. A Build wall carries only one `nextsector`; choosing the
tallest face made every other band solid or connected it to the wrong stacked
room.

The converter now resolves every authored face link independently and writes
only competing or otherwise lost alternatives to `kexportal.txt`: 9982 directed
face records across the 25 retail maps, with MINES the largest at 1101. At each
render and `clipmove`, the Kex-only runtime selects the face containing the
camera or actor Z, temporarily installs its exact `nextwall`/`nextsector` pair
and directed face flags, supplies a reciprocal Build link only when the target
is a static wall, and restores every touched wall immediately afterward. A
dynamic target keeps its own selected directed mapping, preventing stacked
Kex rooms from being overwritten by an artificial reverse connection. Ordinary
one-neighbour portals stay native
Build geometry, and non-Kex maps never enter this path.

Face endpoints attached to a sector ceiling or floor retain that attachment at
runtime. Lifts, touchplates, bomb sectors and floating platforms therefore open
and close the appropriate vertical band instead of leaving a stale portal at
its authored height. `kexverify.py` compares every emitted topology, bound and
flag field, while `VB_KEX_PORTAL_TEST=1` selects each live face and verifies the
temporary topology, masked art, applicable static reciprocal link and
byte-for-byte restoration.
KILENTRY passed all 179 faces, CAVERN_SAT passed after its signed floating-
platform offset (449 active, two correctly closed), and MINES passed all 1101.

### True Room over Room

Kex marks a vertical join the same way it marks a doorway: the ceiling or floor
FACE carries a `link` to the sector on the other side, exactly as a wall face
does. There are 300 of them across the 25 retail levels, 299 symmetric, 299
with the upper sector's floor at precisely the lower sector's ceiling, and 291
of 292 with wall rings matching edge for edge. The converter was ignoring the
field entirely.

Build expresses this with TROR, and eduke32 stores it without changing the
struct layout: the version int is 9 instead of 7, the bunch number rides in
`ceilingxpanning` / `floorxpanning` with `YAX_BIT` set in the matching stat, and
a seam wall's partner index rides in `lotag` (sector above) or `extra` (sector
below) with bit 1<<10 or 1<<11 in `cstat`. `yax_update()` lifts all of it into
its own arrays at load and puts the hijacked fields back, so panning and tags
stay usable afterwards. `NEW_MAP_FORMAT` is off in this tree, so that compat
encoding is the one that applies.

The converter groups the links into bunches with a union-find over sector
sides -- so several sectors meeting on one plane share a bunch rather than
getting one bunch per pair -- and links the seam walls by coordinate, but only
where the face is genuinely untextured. Exhumed also needed
the draw path itself: only Duke called `yax_preparedrawrooms()` /
`yax_drawrooms()`, and both return immediately when a map has no bunches, so the
original PowerSlave levels are untouched. The per-layer sprite callback is
`analyzesprites()`, which walks `tsprite[]` for whatever `renderDrawRooms` left
in `spritesortcnt` -- the same shape Duke's `G_DoSpriteAnimations` has.

**What it does not do is change what you see, and the reason is worth writing
down.** Every one of the 300 linked floor faces is *textured*, and so are 299 of
the 300 linked ceilings. A vertical link is therefore an adjacency -- "this
sector is directly below that one" -- and not an opening. The two planes
coincide in 299 of 300 cases, so TROR faithfully renders the neighbour's surface
in exactly the place the sector's own surface would have been. Loading TOMB
reports 21 bunches, and a capture from the level start is pixel-identical to the
same capture before the change: 0 of 200,760 sampled pixels differ, maximum
channel delta 0. The player does not fall through the start floor, because the
surface on the other side of the seam is solid.

So the remaster's levels contain no room-over-room *openings*. The support is
real and the data is now represented rather than discarded -- which is what
makes vertical adjacency available to clipping and to `updatesector` -- but
nothing in these 25 levels renders differently because of it, and the entrance
overhang is a lighting difference, not a stacking one.

### Lighting

The baked lighting lives on each face's TILE vertices. A face with no tiles has
none, and its four corner vertices carry a flat 32 on every face in the level --
so the corner fallback is a constant that says nothing. That mattered more than
it sounds: in TOMB 901 of 1629 wall faces have no tile vertices, and **884 of
those are portals**, which Kex leaves untextured because you see through them.
Build still draws a portal wall's upper and lower parts, so every step, ledge
and doorframe in the game was being drawn at the single shade a luminance of 32
produces -- 47 of the engine's 64 -- and that is most of why the levels read
grey and flat. Those faces now borrow the sector's floor shade, which is lit and
which a step's visible face sits on. Median wall shade in TOMB went 47 -> 29.

Measured against a capture of the remaster from the same spot in the same level,
the solid (non-sky) pixels moved from p05/p25/p50 = 18/34/54 to 24/52/72,
against the remaster's 26/48/92.

**Gradients.** A face is not one shade but a field of them, one per tile
vertex, and measured over the 25 levels 46% of floors and ceilings and 40%
of walls span six or more Build shade steps across their own extent (the
90th percentile is 29 steps across a floor and 37 up a wall). Build has one
shade per wall and one per floor or ceiling, so the gradient is carried by
geometry:

- `_light_fit()` fits a least-squares plane to each lit face's tile-vertex
  luminance -- in (x, y) for a flat, (along-wall, z) for a wall -- and
  `_shade_at()` reads it at a point, clamped to the range the face's own
  vertices measured so nothing is extrapolated past them.
- `_split_light()` cuts a floor or ceiling whose field spans
  `KEX_LIGHT_CUT_STEPS` (10) or more along a contour of its light plane,
  with the slope splitter's own `cut()`, each half then shaded at its
  centre. The widest spans go first until the level reaches
  `KEX_LIGHT_SECTOR_BUDGET` (1800) sectors; every cut costs a sector plus
  the T-junction walls it leaves on the neighbours. The budget used to be
  the DOS game's `kMaxSectors 1024` / `kMaxWalls 8192`, which MAGMA alone
  hit (it wants 94 cuts, 1060 sectors); those tables are the engine's 4096
  / 16384 now, the save version is 10004 for the bigger per-sector tables
  it writes, and a level past the version-7 loader's limits is written
  with map version 8 (same layout on disk; `engineLoadBoard` keys the
  limits on the version). No level is at the budget. `VB_KEXSAVE_TEST=1`
  saves at tick 100 and loads the save back at 160, comparing the player's
  sector and health -- MAGMA's 1060 sectors round-trip.
  `VB_EX_SAVE_TEST_FILE=/tmp/.../snapshot` gives that same round-trip an exact
  isolated file path, the form used for future network state snapshots.
  A sloped sector is cut along ITS contour, not the light's (a half with no
  level-line wall could not be tilted at all), its
  pieces pinned to the seam, and the cut is kept only if both halves still
  tilt both surfaces within `KEX_SLOPE_TOLERANCE`; a sector on either end
  of a vertical link stays whole, because build_tror() pairs seam rings
  edge for edge.
- `_split_walls_light()` subdivides a lit wall edge whose field changes
  `KEX_LIGHT_WALL_STEPS` (8) or more along its length into up to
  `KEX_LIGHT_WALL_PIECES` (3) pieces, and every wall -- split or not --
  takes its shade at its own midpoint rather than the face average.

**Vertex colours.** The Polymost KEX path now reads `KEXLIT4` sidecars. Every
emitted floor, ceiling, wall and masked wall is mapped back to its source Kex
face, and the sidecar retains each face-local RGB vertex (including coincident
positions) plus the source tile triangles. At every Build polygon vertex the
renderer queries that triangle mesh with barycentric RGB interpolation,
avoiding the old blend of a wall, floor and portal band merely because they
share a coordinate. The campaign's 536,539 lit vertices are grayscale, but
`TEST_COLOREDLIGHTS` has 800 chromatic samples (up to 218 channel difference),
all retained by the converter and renderer. When Build's converted geometry
places a query outside an authored tile, the face-local lookup remains as a
safe fallback. `KEXLIT2` and pre-provenance sidecars still load as monochrome
compatibility data; the geometry splits remain the hardware-independent
fallback.

KARNAK goes from 494 sectors and 2311 walls to 669 and 3793; TOMB from 285
/ 1361 to 451 / 2515. Two things this turned up in the older passes: a
contour cut's crossing point can land a hair (0.0004 units, KARNAK sector
163) from a neighbour's corner, and the T-junction pass then split the seam
into a sliver that rounds to a ZERO-LENGTH wall -- which, as a sloped
sector's first wall, makes the engine (`i == 0` in getflorzofslope) draw
the sector flat; `_split_tjunctions()` now ignores hits within one Build
unit of an edge's ends. And a sloped sky ceiling was held at the highest
point over the sector's RING, so each piece of a cut sector got its own
sky height; it is held over the face's quad now. `kexverify.py` measures
a converted level against the Kex planes at sample points (KARNAK's 90th
percentile floor/ceiling error went from 672 to 38 z units with the sliver
fix), checks that every portal is paired, and runs the event audits: every
guarded event with its effect, key doors with key bits, no tag listened on
that nothing fires, no mover sector cut into pieces, every switch on a
wall, no sprite inside a solid sector.

**Per-vertex shading.** All of the above carries a smooth field as a flat
shade per piece, and it showed: every piece a block of its own brightness
with a seam to the next, the fire pot brightening its whole sector at once
(`Kex_SpriteGlow`), where the remaster's fire pot throws a halo on the wall
behind it and its floor darkens into the corners. The remaster draws each
face as a Gouraud mosaic of its tile vertices, so the port does the same
now. kexmap.py writes every tile vertex beside the map as `KEX_<NAME>.LIT`
("x y z lum" in Build units; TOMB has 10,322 after merging shared corners),
`kexvlight.cpp` loads it per level into a 768-unit grid, and
`KexVL_ShadeAt()` blends the samples within ~190 Kex units of a point
(weight 1/d^4, so the nearest cell corners rule) through the converter's own
luminance-to-shade curve. polymost does the rest in Kex mode
(`polymost_kexVertexLight`): `polymost_drawalls` and `polymost_drawmaskwall`
describe the surface each polygon lies on -- a wall's two projected ends
with their 1/depth, or a flat's sector and plane -- and `polymost_drawpoly`
recovers each screen vertex's world position from it (1/depth is linear in
screen x along a wall; a flat's plane gives z, z and screen y the depth,
depth and screen x the camera point, the view rotation undone), asks for its
shade, and colours the vertex with it. A vertex per corner is not enough --
a wall or floor piece hundreds of units across shaded from its corners
alone averages the fire pot's halo away -- so `polymost_kexTessellate()`
first cuts each polygon along the world's 128-unit cell grid, the mesh the
remaster itself draws: world grid lines project to straight screen lines, so
the pieces are a convex polygon split by lines (at most eight cuts per axis,
96 pieces), each drawn through the same path. The colours ride in a second VBO on
the fixed-function colour attribute, which `polymost1Vert.glsl` already
reads as `v_color = gl_Color`; the per-polygon `glColor4f` path is untouched
for everything else. Skies (`getpskyidx`), sprites, the HUD and 8-bit tiles
keep their polygon shade; the sector glow of `KexLight_Tick` stays off when
the field is loaded, since the glow is in the field. `VB_KEX_FLATLIGHT=1`
turns it off for comparison: on TOMB's temple doorway 68% of the frame's
pixels change, 18% by more than 60 levels -- the halo on the panels behind
the two fire pots, the corridor fading into its depth.

A wall's vertical gradient rides along with this: 40% of walls are lit
differently top and bottom by six steps or more, which no per-wall shade
could say; the vertex shade can.

### The hit flash

The remaster blinks a creature white for an instant when it takes damage.
Direct damage marks the sprite in `runlist_DamageEnemy()`; explosive and fire
damage mark it after `runlist_CheckRadialDamage()` has confirmed that damage
was actually dealt. Both use the same four-tick Kex-only marker. A prop killed
by the hit would normally disappear before a rendered frame, so its shatter
path leaves a short-lived, nonblocking visual copy for the white blink while
the real prop still breaks immediately. `analyzesprites()` draws every marked
sprite fullbright in pal 30, and pal 30 is white two ways: for a hires tile
exhumed.def's `tint { pal 30 ... flags 64 }` screen-blends the pixels with
white when the texture is loaded for that pal, and for a tile still drawn from
the original 8-bit art -- which goes through a palette lookup, and pal 30 had
none, so it drew in pal 0 -- `KexFlash_MakePal()` builds a lookup that maps
every colour to the palette's whitest entry. The mark decays with
`KexFlash_Tick()` beside the light tick.

`VB_KEX_FLASHTEST=1` holds every creature and prop in the flash: COLONY_PSX's
mantis (hires) and CAVERN_PSX's mummy and vase (8-bit) all capture as white
silhouettes. `VB_KEX_FLASH_AUDIT=1` traces the normal short marker instead;
direct creature hits, radial hits on Anubis, mummies and wasps, and fatal vase
copies were all observed reaching the renderer.

### Breakable props

The remaster's vases and barrels are shootable actors, and none of that
survived conversion -- they came out as decor sprites that stand there and
ignore bullets. The actor def carries `health 30`, `shootable 1`, `solid 1` and
a `deathAnim`, and the anim is the whole recipe:

The remaster's machete breaks the vases as well as the guns do: the sword's
close-range hit, which only damaged creatures by statnum, now also sends its
damage to a breakable prop's run (`KexBreak_IsProp`).

    vase01_death { frame {
        action A_PlayLocalSound("sounds/glassbreak.wav")
        action A_Spawn("Fx_VaseExplodePuff", 0, 0, 32)
        action A_RandomVaseItem()
        action A_TossActor("Obj_Vase01_Shard01", 0, 0, 32, 8, 8, 4, 16)
        ... one per shard ...
        action A_Destroy() } }

All thirteen ordinary breakable actor definitions -- twelve vases plus the
barrel -- call that same `Fx_VaseExplodePuff` at z+32. Fresh conversion follows
its zero-delay `effects/vasePuff` jump into `effects/magicPuff`, so the skipped
source frame 0 stays skipped and frames 1--7 each last two Exhumed ticks. The
result is the exact 14-tick, scale-2, fullbright olive puff, with its
radius-384, 15-step light. The unused generic `Fx_ExplodePuff` has no shipping
caller and is deliberately not substituted. Runtime keeps the authored order:
local sound, puff, random item, shard tosses, then destroy. Twelve of those
types are placed in the shipping maps; `Obj_Vase11` is defined but unused.

The shard art is already in the prop's own idle anim: frame 0 is the intact pot
and the frames after it are the shards. That is also why holding the prop on
frame 0 (`noAdvanceFrames`) is what stopped it flickering into a sliver of
debris -- the "random sprite" it flickered to was a shard.

The local sound is data too, rather than a vase/barrel guess. The converter
reads `A_PlayLocalSound` from each death animation and writes a `breaksound`
recipe to `kexbreak.txt`: ten ordinary vase types use `glassbreak.wav`, the
barrel uses `woodbreak.wav`, and `Obj_Vase09` deliberately uses
`fleshbreak.wav`. The two exploding vases and `AI_Magic8Ball` use no local
break sound; their explosion/countdown already supplies the authored audio.
The table also carries each actor's authored height for runtime spawn
callbacks.
The native reader still accepts the old 11-column table, inferring silent blast
props and the old glass/wood split, while a fresh conversion preserves Vase09
exactly.

Build has the pieces this needs. A prop registers a runlist run the way
`BuildObject()` does, so `runlist_DamageEnemy()` -- which looks the run up
through `sprite.owner` -- routes a 0x80000 damage message straight to it, and
the debris enters the KEX-only toss path. It reproduces the shared
`A_TossActor(0,0,32,8,8,4,16)` contract used by every shipped gib and shard:
spawn 32 KEX units above the source, independently launch at +/-8 XY and
+4..16 Z, then simulate the actor's two 60-Hz `.75` gravity / grounded `.5`
friction updates for each native 30-Hz tick. Source-order ceiling checks use
the authored 56-unit gib, 64-unit fish-gib, or 32-unit shard height and reflect
before the floor check. The breakable shards retain their authored `bouncy=1`
reflection and `A_DestroyAtRest`, while creature gibs retain `expires=1` (100
ticks stationary, then a 32-tick shrink). The countdown starts on the same
60-Hz source tick that movement reaches zero; `A_DestroyAtRest` instead runs
before a shard's following movement tick. Classic
fire-cauldron chunks keep their original stronger throw. Toss chunks also use
the actor's own collision radius: 28 for ordinary creature gibs, 32 for fish,
vase/barrel, explosive-vase and boulder pieces, rather than the old fixed
160-unit chunk radius. TOMB alone has 23 converted breakable instances.

`A_RandomVaseItem` is transcribed rather than guessed: the remaster ships its
AngelScript in the .kpf, so `scripts/p_misc.txt` gives the real table --
`r = Max(8)`, with 0 spawning a scorpion, 3 the ammo pickup, 5 the health
pickup, and the rest falling through or dropping nothing. The Sandals-gated
`r = 1` branch now releases a regular wasp through Build's existing wasp
constructor. The Scepter fireball swarm and Shawl cobra actors use a
Kex-specific runlist path because the classic game has no equivalent
constructors. The rare
`Item_Map` vase roll is now wired to Build's map pickup and uses the remaster's
map sprite. `Item_Power` uses the remaster's full-power orb and fills the
classic weapon pools to their native caps, including the Manacle's six-shot
pool. The Scepter-gated vase branch now fires twenty short-lived, bouncing
Ra-fire orbs using the remaster's generated projectile art; their 20 damage
and wall/floor collision are handled by a Kex-specific runlist path, and
their expiry now emits the remaster's fireball puff and `pbexplode.wav`. The
Shawl branch fires three homing green cobras; their wall/floor impact runs
the remaster's `A_CStaffExplode` plus `A_RadialBlast(512, 100)` recipe through
the same path. The Pharaoh-only difficulty branch remains outside the classic
difficulty model, and the dust puff plus each prop's authored break callback
run through PowerSlave's native effects.

Every branch uses the source callback's `origin.z + actor.GetHeight()`, not the
vessel's floor origin. Fresh `kexbreak.txt` rows preserve the exact 80- or
128-unit ordinary-vessel height (and the other breakable heights), and runtime
converts the upward KEX offset through the shared 16x Z and 6x world scales.
Items therefore appear above the vessel and a case-0 scorpion begins at that
same authored top before gravity takes over. Older 12-column conversions keep
their previous floor-origin fallback.

Types 49 and 50 are the EXPLODING pots -- `defs/defines.txt` names them
`Obj_Vase_Explode01/02`, where the old console enum in scripts/actorTypes.txt
had FLAMEPOT/SNAKEPOT, and trusting the old enum had turned all 232 of them
into fuel-canister and cobra-ammo pickups. They hold a random still frame
(`randomizeFrames`), keep their shard art in a separate `*_shards` anim, and
their death anim carries `A_RadialBlast(512, 140)` -- so kexbreak.txt grew two
blast columns. `KexBreak_Shatter()` now spawns the exact 21-tick, scale-4
`Fx_Explosion01` at z+32 before `runlist_RadialDamageEnemy`; the source names
the land actor even when the pot is underwater. The helper carries the
authored `explode03.wav`, fullbright frames and fading orange light, while an
older conversion without its optional sequence retains the previous native
fallback. Radius 512 Kex units x WORLD / 256 = 12 against the cauldron's 20.
The blast sets off neighbouring pots, as it does there.

Two more corrections from the same defines pass: the vase drop table's case 0
spawns a 20-health `AI_Scorp`, not `BuildScorp` (the 20000-health Selkis boss).
It now passes the scorpion identity into the shared Spider controller, selecting
the exact generated `SCORPION.SEQ` instead of accidentally spawning an ordinary
spider. The remaster's
"Mantis" is Build's lava dude wearing a new name (its rise/attack/death frames
are pixel-identical to the LAVAG tiles, and its def says
noLavaDamage/noExitLava/hidden); it is mapped to lotag 108, with its KEX health
and fireball damage restored, with authored attack and death sounds, and its
death animation emitting three twenty-orb Ra-fire bursts. The KEX actor uses 400
health, radius 48 (Build clipdist 72), scale 3 (repeat 120), movement speed 64,
pain chance 16 and no in-fighting; classic lava-dude stats and targeting remain
unchanged.

Its attack is the retail script rather than the old one-volley approximation.
After the three common lead-in ticks, a 1-in-128 byte roll selects the rare
nine-tick PSX branch: source frames 5 through 9 each launch one fireball from
the actor origin at height 192, with independent signed 0.0625-radian yaw and
pitch spread and a continuous 8–16-unit speed. Otherwise the Saturn branch
chooses `1 + Size(3)` and, because its terminal callback checks before it
decrements, emits two through nine 13-tick volleys. Each Saturn volley uses
the exact `A_FireProjectile(..., 8, 192, 16)` muzzle, height and fixed speed;
it faces both on the sound cue and again on the firing frame. The packed
KEX-only attack substate stays inside the existing frame field.

The hidden lifecycle now follows `A_MantisThink` as well. Target acquisition
uses the remaster's 3000-unit all-around sight test; the one-frame chase anim
runs its callback every four ticks. That callback retains the `UInt8() <= 32`
gate, 2000-unit radial LOS, clamped `Size(3)`, and both persistent scratch
counters that choose a peek or full rise. Peek and rise use stock LAVAG slots
0 and 1 for the authored 62 and 60 ticks. Peek shows on frame 0, plays
`mantisact01/03/02` on 2/24/37, and hides on 61. Rise faces, shows, and starts
the source's fixed 60-tick local camera shake (rather than the native
distance-scaled quake) on frame 0, plays `mantisact02/04` and `mantisrise` on
4/12/14, becomes solid
and shootable only on 31, then enters attack after 59. Pain is 15 ticks with
`mantispain.wav` and immediate unblock; submerge and splash are 24+21 ticks,
unblocking on submerge frame 3 and hiding on splash frame 20. The attack state
and mutually exclusive scratch-counter state all fit in `Lava.nFrame`, so the
classic save-record layout remains unchanged.

Retail `mantis_death` is 54 source frames, not the inherited 159-tick native
death. Its visuals are the matching stock pain frames 0–8, submerge 0–23, and
splash 0–19; source frame 53 hides and destroys immediately. Frame 0 fires the
tagged event, plays `mantisdeath.wav`, clears blocking, and emits twenty
bouncing Ra-fire orbs. Frames 5 and 8 emit another twenty each, while death
frames 37 and 43 retain the two splash sounds. The FireActor height offset is
converted through the same 16× Z and 6× world scales as other KEX projectiles.

`VB_KEX_MANTIS_TEST=1` deterministically checks the 62-tick peek, 60-tick rise,
common prefix, minimum two-volley Saturn path, five-shot PSX path, 24+21
submerge/splash path, 53-tick destroy transition, callback masks, projectile
recipes, tagged event, all three death bursts, and actor stats.

The camel is the remaster's level exit (lotag 60) and the checkpoint scarab
its save point (lotag 59); both were standing around as decor.

`VB_KEXBREAK_TEST=<ticks>` shatters every prop in the level that many ticks in,
which is how the shard and drop paths are exercised without a player. It also
requires one successfully allocated exact z+32 puff for every ordinary
instance before reporting its break-puff PASS line.

### Light sources

The remaster groups 19 actor types as `group "Light Sources"` -- its fire pots,
wall torches, candles and the water lamp. None of them declares a `lightRadius`
or `lightIntensity` (only four projectiles in the whole game do), because in Kex
their glow is baked into the vertex colours: on the wall behind TOMB's first
fire pot the baked luminance runs 39 at the top of one face to 255 at the
bottom. `KEXLIT4` preserves the authored tile triangles and renders that
gradient with their RGB interpolation in Polymost. The older per-sector
real-light path remains only for a missing sidecar or a non-Polymost
compatibility renderer.

The flame itself is an authored loop as well. Fire pots carry 12 frames and
wall torches 16; their `randomizeFrames` flag chooses a starting phase, it does
not freeze the actor. The converter used to mistake that flag for a hold-frame
instruction and discard every frame after frame zero. It now preserves the
complete light-source loop in `kexdecor.txt`, and `KexDecor_ViewTile()` chooses
the view tile from `totalclock` with a deterministic per-instance phase so a
row of torches does not pulse in lockstep. The separate `noAdvanceFrames` flag
remains the only instruction that holds an actor on one frame. `kexverify.py`
checks every randomized animated light against the authored KPF frame count,
checks that each loop contains visibly distinct PNGs, and rejects accidental
global `animtilerange` animation; offscreen captures of CAVERN's opening fire
also show its flame silhouette changing between frames.

`kexmap.py` writes the tiles it should apply to into `kexlight.txt`, and
`KexLight_Init()` collects the sprites carrying them once per level.
`KexLight_Tick()` then casts each one from `MoveThings()`, right after
`UndoFlashes()` has cleared the previous tick's -- so nothing accumulates,
which is the same discipline the original explosion lighting keeps.

The cast itself is `Kex_SpriteGlow()` in `lighting.cpp`, which is `AddFlash()`'s
sweep with two deliberate differences. The falloff is parameterised: `AddFlash`
drives the shade 255 steps toward full bright at the sprite, which is right for
an explosion and far too much for a pot of fire standing in a room, so the peak
and the radius come from the table (`36` and `<< 6`, i.e. 2304 world units).
And what reaches the flats differs -- an explosion sums every wall's
contribution, which saturates the floor of any sector it goes off in, while a
fire hands them the brightest single wall so a large room stays dim at the far
end. Sprites are left alone; a fire lighting itself looks wrong.

The peak and shift are a first setting, not a match measured against a capture
the way the distance shading below was.

Every table the converter generates -- `kexbreak.txt`, `kexlight.txt`,
`kexfont.txt`, `kexworld.txt` -- is the same shape: a `#` comment line, then one
record of space-separated fields per line. Each had grown its own copy of open,
slurp, NUL-terminate, walk the lines, skip the comments. They now share one
`KexTable_Load()` in `kextable.h`, which hands a parse function one
NUL-terminated line at a time; on the writing side `_write_table()` in
`kexmap.py` emits what it expects. On the engine side every light in
`lighting.cpp`, the original explosions included, now records and brightens a
surface through a single `AddFlashRec()`: take a flash slot, save the shade,
push the pal past the palette so `UndoFlashes()` knows to restore it, subtract
the falloff. That was seven copies of the same ten lines.

### Movies

Nightdive ships fifteen Ogg Theora movies next to the .kpf -- the NightDive,
Kex and Throwback logos, the intro and both endings, each in four languages --
and in -kex mode DoTitle() now opens with them: the three logos, then
`movies/intro-<lang>.ogv` in the `kex_language` tongue, then the PRESS START
hold. Without the decoder (or under the classic renderer) the old sequence,
ending in the same story told by book.mov, stands in.

kexmovie.cpp is the player: libogg splits the container, libtheoradec decodes
the video, and the movie's own Vorbis stream goes to the engine's mixer
untouched -- its pages, concatenated, are already a valid standalone .ogg,
and audiolib plays Vorbis natively, so no vorbis linkage is needed. Frames
are converted to RGB in software and drawn as one textured quad; both come
from Homebrew's libtheoradec/libogg, detected by pkg-config in the makefile
(HAVE_THEORA), so no sources are vendored.

Two lessons paid for in screenshots. Polymost's palette-lookup program and
matrices are re-bound by every videoNextPage() 2D flush, so ALL of the quad's
GL state -- glUseProgram(0), matrices, binds -- is asserted per frame; set
once before the loop, it was gone by the second frame, and the RGB texture
sampled through the palette program came out kaleidoscoped. (The raw
glUseProgram is deliberate: the buildgl wrapper tracks the bound program and
thought 0 was already active.) And window activation at startup leaves
phantom key events that skipped each logo the moment it began; the first
dozen frames of every movie flush the keyboard instead of listening to it.

The decode itself was verified pixel-perfect against ffmpeg's reading of the
same frame before any GL debugging began -- which is what confined the search
to the draw path.

### Creature art

The engine animates every creature through STUFF.DAT's .SEQ files, and the
shareware demo ships all 78 of them -- but 389 of the monster tiles those
files reference are zero-size in the demo's ART, which made every wasp, crow,
sentry, Set, queen and Selkis a converted level spawns invisible.

The converter now uses two compatible paths. Creatures whose native timing is
still suitable retain the demo SEQ layout and receive remaster art on the
missing original tile numbers. Creatures whose retail callbacks cannot fit
that layout -- currently Wasp, Bird, Queen Seedling, Sentry, Selkis and Set -- receive a
generated loose SEQ with an explicit KEX action table. Their controllers
recognize the generated cadence and fall back to the stock table when it is
absent, so classic data remains supported. In both paths, sub-seq indices,
flags and sound events remain an engine-side contract.

kexseqmap_gen.py derives the stock-tile mapping from the demo SEQs and the
remaster animation scripts, and also emits the dedicated exact sequence data.
build_seq_tiles() in kexmap.py composites the committed kexseqtiles.py entries
from the sprite sheets at conversion time. parse_anim_full() keeps all eight
rotation blocks separate instead of flattening them like the decor path. Most
generated actors can share mirrored west-side views. Sentry, Selkis and Set are
deliberately different because their layered poses are asymmetric: all eight
views are precomposited and their chunk flip bits stay clear.

Two measurements made this simple. Chunk positions in a SEQ are TILE-CENTRE
coordinates, so a replacement of similar proportions lands where the original
art did without touching offsets. And the remaster's monster sheets are the
ORIGINAL art resolution: the sentry's surviving attack tile (3226, 97x113)
matches its kex composite 1.000:1, and the Selkis muzzle matches its sheet
slice -- an actor's kex `scale` is a renderer multiplier over there, already
encoded here in each creature's Build repeat values, so nothing is rescaled.

Verified in game: the Wasp flies and attacks in MARSH, the Sentry pursues and
fires in COLONY_PSX, and Selkis passes its complete exact lifecycle probe in
SELBUROW, all using remaster art. The four legacy sound names that neither the demo nor an earlier alias
covered (REX-BITE, REX_GRGL, REX-ICU and GHOST109) now resolve to the nearest
remaster recordings.

KEX Sentries use a generated 36-slot, 249-frame/chunk `ROACH.SEQ`. Seventy-one
precomposited eight-view images occupy tiles 7890-7960 and preserve the retail
1-frame idle, 8-frame chase, 12-frame pain and 10-frame attack groups. The
pain sound is on frame 0, pain returns to chase after rendered frame 10, and
the firing sound, projectile callback and fullbright muzzle are all on attack
frame 6. Pursuit also emits the authored chatter and initial sight cue.

KEX actor definitions express health and attack damage in visible gameplay
points, while the native runlist stores health at four internal units per
point and scales incoming logical damage by the same factor. `KexHealthUnits`
and `KexHealthPoints` make that boundary explicit. Every adapted enemy --
Anubis, Fish, Mantis, Bastet, Mummy, Spider/Scorpion, all three flyers,
Sentry, Selkis, Set and both Queen forms -- stores its authored health at
four times the displayed value; thresholds and diagnostics convert back to
logical points. Classic health values and balance are unchanged, and
`kexverify.py` asserts this contract in the source so a newly adapted actor
cannot silently become four times too fragile.

Enemy death supplies now retain the whole `A_RandomItem` actor path rather
than creating a pickup immediately on the sector floor. The death controller
captures the host origin plus its authored actor height before a callback can
delete the host, then rolls the retail ammo/health/low-health mercy branches.
An accepted roll creates `Fx_AmmoSpawn` or `Fx_HealthSpawn`: a translucent,
fullbright blue or red orb assembly at scale 1 or 2. Its generated SEQ holds
the source's initial blank for 20 Exhumed ticks, displays the eight authored
poses for 23 ticks in their 3/3/3/3/3/2/2/4 cadence, plays `spawn.wav` when
the art appears, and runs `A_SpawnOrbPickup` on tick 42. Only then is the
native ammo or health item made at the same point with the effect's scale and
value. The shared flyer controller distinguishes Wasp (height 64), Bird
(128), and Seedling (96); every other adapted enemy likewise uses its exact
definition height. An older conversion without the two optional effect SEQs
keeps the immediate native-pickup fallback. `VB_KEXDROP_ORB_TEST=1` exercises
both colors, both scales, the translucency and the delayed callback in a
hidden runtime probe.

Gib deaths also retain the separate retail `Fx_Gibs01` actor instead of
reducing it to an invisible timer. `KEXGIBDEATH.SEQ` uses tiles 22004-22013
for ten source-composited poses: tick 0 is blank, ticks 1-13 show the authored
fullbright burst, and ticks 14-18 are blank. On tick 18 the effect itself runs
`A_RandomItem` and is destroyed. `A_GibDeath` copies the dying actor's scale
and first moves the effect by the caller's offset; the item point is therefore
the original origin plus that offset plus the effect's own 64-unit height,
not the dead creature's height. The eleven shipped callers are Anubis and
Sentry/Queen body at +64, Seedling at +32, and Bastet, Bird, Fish, Mummy,
Spider, Scorpion and Wasp at +0. Their controllers all route through one
save-safe native actor and suppress the lethal-hit fallback roll while that
exact sequence is loaded. `VB_KEX_GIB_DEATH_TEST=1` checks both inherited
scales, both representative offsets, fullbright rendering and the delayed
effect-top callback.

The KEX Sentry controller matches retail health 180, radius 48 (Build clipdist 72),
scale 1.5 (repeat 60), movement speed 8, turn speed 2 and pain chance 24. Death
immediately chooses one of the two sounds, tosses the authored nine pieces
from sentry_gib01 through sentry_gib05, and destroys the host instead of
leaving the classic roach corpse. It also starts the shared gib-death actor at
the authored +64 offset before deletion. Those five gib sprites remain on
tiles 7800-7804. Exact-sequence detection confines these changes to generated
KEX data; the stock sequence layout and classic behavior remain available.

Its `Proj_Blaster` also has a dedicated remaster projectile frame; KEX
type-13 bullets render that 12x12 sprite on tile 7805. Enemy launches no
longer approximate the callbacks through the native generic/hitscan path:
the KEX bridge mirrors `A_FireProjectile` by adding callback distance to the
source actor radius, aiming from the target's logical KEX centre (rather than
its animated Build sprite height), applying the authored
vertical muzzle offset, recalculating pitch from the final muzzle position,
and converting the callback speed into Build's 6x XY / 96x Z space. The exact
Anubis, Sentry, Set, Selkis and Mummy recipes are respectively
`4/96/16`, `4/96/24`, `50/192/16`, `2/144/64` and `2/96/12` for
distance/height/speed. Queen's origin-launched seed and both Saturn/PSX Mantis
fire paths use the same bridge. A deterministic `VB_KEX_PROJECTILE_TEST`
checks all eight launch paths without requiring a live boss encounter.

The remaster flamethrower is also a real world-projectile weapon rather than
the DOS ignition-only bullet. Its four firing frames each create a
5-damage, radius-36 `Proj_Flame`; only the first consumes fuel. The player
bridge preserves the puppet's 95.25-unit radius and the authored
`A_FireProjectile(..., -48, 80, 16)` distance/height/speed callback, then
uses the actor's 1/64 starting scale, 15-unit (30-tick) fizzle growth/fade and its
three-frame flight plus fifteen-art/one-blank-frame sizzle. `KEXFLAME.SEQ` and the same
projectile probe keep its generated data and native launch/impact path in
lockstep without changing classic Exhumed behavior.

The KEX Ring of Ra is likewise a physical `Proj_Ra_Fire`, not the DOS
target-following `Ra[]` eye. Every firing frame launches the fullbright,
radius-33, height-64, 20-damage orb from an alternating `+/-32` side muzzle;
its eight-`GameTicks` aim wave, 16-unit speed, upward 12-unit bias and
pitch response mirror `A_FireRa`. The orb owns the remaster's 0.75 gravity,
perfect bounce, 120-tick expiry and `rafire_explode` impact effect. The
classic controller remains the fallback for non-KEX data. Active Ra actors
serialize with their source owner and motion state, and the projectile probe
checks all eight launch phases without changing classic Exhumed behavior.
`VB_KEXSAVE_TEST=1 VB_KEX_RA_SAVE_TEST=1` additionally saves and restores a
live player orb, checking its position, owner, velocity and lifetime.

Projectile death callbacks are independent of direct actor damage and run on
wall, actor, floor and ceiling impacts. Both red Cobra types therefore retain
zero direct damage but execute the authored radius-224, 50-damage blast; their
toggle argument is false. Mantis and trap fireballs deal 100 directly, then
run the radius-192, 64-damage opening blast. Boulder type 17 deals 35 directly,
uses scale 2/repeat 80, executes the same 192/64 opening blast and tosses four
`Proj_Boulder_Piece` actors using private tile 7849. `KexBulletApplyImpactBlast`
converts those retail radii to native radial ranges 5/4 and runs before the
impact presentation. The projectile probe includes this complete impact
matrix, so zero direct Cobra damage can no longer erase its death blast.

Fireball and boulder impacts no longer borrow the DOS grenade/fire-poof art.
Both retail death animations create `Fx_FireballExplode`: a scale-2,
fullbright orange `(0.75, 0.375, 0.25)` actor whose zero-delay
`effects/fireballPuff` alias creates a radius-512, 15-step, two-pass dynamic
light and jumps to `effects/magicPuff`. KEX's `ChangeAnim`/increment ordering
skips magic frame 0; frames 1--7 are visible and frame 8 destroys on entry.
The converter composites those seven shipped frames on tiles 21908--21914 and
writes `KEXIMPACT.SEQ`, duplicating each frame for the same 15-fps cadence at
Exhumed's 30 Hz. The native KEX-only path plays `pbexplode.wav`, preserves the
boulder's authored +2-unit effect offset, and fades a fixed-radius light over
the saved animation's fourteen ticks. Build's shade light records do not carry
RGB or pass count, so the orange is retained in the effect pixels while nearby
geometry receives the matching radius/fade luminance. If the optional sequence
is absent, an older conversion keeps the former native effect fallback.
Classic maps never select this path.

The remaining enemy-projectile deaths are dispatched from their retail
`deathAnim` as well, rather than inheriting whichever DOS `BulletInfo` record
hosts them. Red Cobra and Soul deaths call `A_RedCobraExplode` at elapsed ticks
0, 3, 6 and 9; the Shawl's green Cobra does the same through
`A_CStaffExplode`. Each callback consumes three centred 32-unit random offsets,
spawns a scale-2 fullbright puff and attaches `pbexplode.wav` only if that puff
was allocated. The converter derives both effects from shipped magic frames
1--7: `KEXCOBRARED.SEQ` uses tiles 21915--21921, tint
`(0.75, 0.25, 0.25)` and radius-384 light; `KEXCOBRAGREEN.SEQ` uses tiles
21922--21928, tint `(0.375, 0.75, 0.375)` and radius 768. Their fourteen-tick
lights use the same fixed-radius fade as the orange effect.

An invisible twelve-frame `KEXCOBRACTL.SEQ` carries the delayed callbacks and
is an ordinary `Anim`, so the 0/3/6/9 cadence and tick-12 destruction survive a
save instead of living in an unsaved side timer. Red Cobra types 9 and 16 still
run their separate 224/50 non-opening blast after the first puff. Set's Soul
uses the red four-puff presentation but has no death blast; the classic type-11
radius is explicitly suppressed. Queen's Seedling and the Scepter's Ra orb
spawn the orange effect at the authored +2 Z offset, then play the sound, with
no radial damage. The Shawl's green Cobra performs its 512/100 non-opening
blast after the first green puff. These two vase-only projectile controllers
share the same native effect paths rather than retaining oversized stock
animations and long flashes.

`Proj_Anubis` changes to its own shipped breakup art. Source frames 0--12 are
visible for one Exhumed tick each; source frame 13 destroys on entry and is not
drawn. They are precomposited on tiles 21929--21941 in
`KEXANUIMPACT.SEQ`, while the projectile's blue light continues through those
thirteen ticks. In contrast, `Proj_Blaster` and `Proj_Beam` have immediate
destroy-only death animations and intentionally produce no impact sprite.
All adapted KEX projectile types suppress native actor-hit puffs, and every
type 8--17 suppresses legacy radial fallback; only an explicitly decoded KEX
blast can deal area damage. Older conversions may retain a cosmetic fallback
when optional exact artwork is missing, but they cannot regain the erroneous
Soul/Seedling blast. The projectile probe checks all of these contracts.

The same projectile bridge supplies the remaster's dedicated Anubis fireball
and Mantis fireball loop sprites on tiles 7806 and 7807. Anubis now also uses
the authored radius 48 and scale 1.5 (Build clipdist 72/repeat 60), and its
shots receive the authored transient glow while they are in flight. Classic
Anubis dimensions and launch behavior remain unchanged.

The converter writes a 58-slot, 892-frame `ANUBIS.SEQ`. Idle/chase/melee/range/
pain are the exact 1/18/24/25/10-tick, eight-view retail groups; the previously
skipped melee-miss recovery is nine ticks; and the single-view normal death is
20 ticks followed by a permanent corpse. Its 128 precomposited authored images
occupy tiles 21300-21427. The six demo drummer slots and their seven sound
events are copied unchanged, while cadence detection keeps old conversions on
the stock controller path.

The melee path no longer inherits the DOS Anubis' two 7-damage flags. It runs
the retail 24-frame callback schedule: `anubismelee.wav` on frames 3/16,
`anubisslap.wav` and 20 damage on frames 6/19, with the authored zero-extra-
distance checks on frames 4/17 and the full miss hold before chase resumes.
Range attacks face the target on ticks 0/3/8, then play `anubisproj.wav` and
launch the 4/96/16 fireball on tick 8. Pain plays `anubispain.wav` on tick 4.

Lethal hits now use the remaster's actual death selector: fire and explosions
always choose `anubis_death`, while generic damage gibs only below the strict
100-health overkill boundary (post-hit -8 is normal, -9 is gib). Normal death
chooses either `anubisdeath01.wav` or `anubisdeath02.wav`, applies the authored
vertical impulse on tick 2, becomes non-solid on tick 10, and runs
`A_RandomItem` on tick 19. Gib death tosses one gib1, two gib2, two gib3 and
four gib4 chunks (tiles 7808-7811), then preserves the generic effect's delayed
tick-18 item callback in the shared visible `Fx_Gibs01` actor. Its invisible
host state remains only for save/cleanup ownership and old-conversion fallback.
Classic death selection, magic drops, sequences and corpses remain unchanged.

KEX Mummies use the authored radius 48 and scale 1.5 (clipdist 72/repeat 60).
Their zero-damage red cobra is emitted at the exact 2/96/12 callback recipe;
the native random homing/mummification substitution remains classic-only.
The converter writes a 62-slot, 680-tick `MUMMY.SEQ`: idle/chase/melee/range/
pain are 1/16/13/39/9 ticks, normal death is 35 ticks, and the corpse is a
one-frame permanent hold. All eight live rotations and the single-view death
are 108 precomposited authored images on tiles 21100-21207. Frame clamping and
cadence detection leave the stock sequence as a safe old-conversion fallback.

Mummy melee now has the retail 13-tick loop instead of the native 16-frame,
5-damage attack: `mumatk02.wav` is on frame 3, the sole 20-damage staff hit is
on frame 11, and frame 12 returns to chase only when the target has left the
authored 48-unit melee reach. The ranged timeline plays `mumatk01.wav` at tick
0, then `mumact02.wav` and the cobra callback at tick 6; the DOS resurrection
callback is never run in KEX mode.

Death selection now matches `kexSlaveActor::InflictDamage`: fire and explosion
always use `gibAnim`, while generic damage gibs only below the strict
`-2-(130>>4)` overkill threshold (post-hit -10 is normal, -11 gibs). Normal
death keeps the corpse solid until `A_Die` at tick 24, scales it from 1.5 to
1.25 at tick 0, rolls `A_RandomItem` at tick 32, and holds the final frame.
Gib death selects one of three death recordings, tosses one gib1, one gib2,
two gib3 and two gib4 pieces on tiles 7812-7815, then preserves the generic
gib effect's tick-18 item callback in the shared visible actor; the save-safe
invisible host remains only as controller state and an old-conversion fallback.

Bastet now uses a generated 57-slot, 620-frame/623-chunk `LION.SEQ`. Its 168
precomposited authored images occupy tiles 21500-21667 and preserve all eight
views of the 12-tick chase, 11-tick attack and 17-tick pain groups, plus the
single-view 41-tick teleport-out, 23-tick teleport-in and one-tick idle groups.
The controller recognizes that exact cadence and retains the stock lion layout
as an old-conversion fallback.

The exact controller evaluates the remaster's masked random teleport decisions
on every authored callback tick. Teleport-out clears burning, emits the sound
and 20-tick light on tick 10, and hides on tick 37; teleport-in makes the actor
visible on tick 0, emits its sound/light on tick 1, restores solid/shootable
flags and the 30-tick cooldown on tick 22, then resumes pursuit. Pain can enter
the same teleport lifecycle on tick 15, and attack tick 0 performs its authored
teleport check. This also guarantees that an actor entering through the hidden
path cannot remain permanently invisible.

Bastet uses its authored radius 48 and scale 1.5 (Build clipdist 72/repeat 60),
rather than retaining the classic lion's 60/40 dimensions. Attack activity is
on ticks 1/6, attack sound and the two 20-point strikes are on ticks 2/7 and
1/7 respectively, and tick 10 checks the authored melee reach. On death both
local sounds play, then one gib1, two gib2, one gib3 and three gib4 chunks on
tiles 7816-7819 are tossed. A save-serialized invisible timer preserves the
death lifetime, while the shared visible gib actor owns its delayed tick-18
random-item callback, avoiding a duplicate classic magic drop. Without the
optional effect sequence, the timer retains the compatible fallback callback.

KEX Fish use a generated 49-slot, 409-frame/415-chunk `FISH.SEQ`. Its 107
precomposited authored images occupy tiles 21700-21806 and preserve all eight
views of the 8-tick chase, one-tick idle and 14-tick attack groups. Attack
ticks 4/5/10/11 carry the retail fullbright flags; `fishatk.wav` plays on ticks
2/8, the single 20-point bite occurs on tick 12, and tick 13 performs the
16-unit continuation check. Damage is evaluated from the authored melee radius
instead of requiring `movesprite` to collide on that exact tick.

Fish use radius 16 (Build clipdist 24), scale 1.0 (repeat 40), movement speed 8
and the authored no-in-fighting target rule; their first player acquisition
also emits the retail activity sound. Death plays `fishdeath.wav`, tosses three
gib1, two gib2 and two gib3 pieces on tiles 7820-7822, and hands the delayed
random-item callback to the shared visible gib actor. A save-safe invisible
host preserves cleanup and the old-conversion fallback. The classic clipdist
80, collision-driven 2-damage path, sequence cadence and targeting remain
unchanged outside exact KEX mode.

KEX Wasps now use their authored eight-piece death pattern (gib1, gib2,
gib3, three gib4 and two gib5), randomly choose `waspdeath01.wav` or
`waspdeath02.wav`, start the shared offset-zero gib effect, and destroy the
host; generated gib tiles are 7823-7827.
Their generated `WASP.SEQ` is no longer the demo's stretched approximation:
it preserves the retail four-frame base, four-frame chase, eight-frame sting,
and one-tick backoff groups on 15 origin-anchored movement tiles (7870-7884).
The chase cue is on frame 0, attack sound and 20-damage melee callback are on
attack frame 0, `wasphit.wav` is on frame 3, and frame 7 initiates the authored
20-unit backward step before pursuit resumes. Their KEX health/radius/scale/
speed are 60/18/1.25/16 and `painChance 0` prevents the classic hit reaction;
classic Wasp timing and balance remain unchanged. `A_WaspBob` is a chase-frame
callback, not an idle effect: each of the four chase frames writes
`cos(GameTicks/32 degrees) * 2` only when there is no target, or when the
target is within +/-128 height units and more than 192 units away in full 3D.
The KEX bridge retains that source actor clock at 60 Hz: its default `.5`
animation speed invokes the authored callback on the first of two sampled
source steps per Exhumed tick, while the second preserves its velocity. Both
steps apply AI_Wasp's `.94` vertical friction and their up-positive result is
converted to Build's down-positive Z. A suppressed callback leaves velocity
unchanged. Its yaw chase uses the sampled vertical motion rather than the
classic pitch-as-`zvel` routine. Old converted saves recognize the new
save-safe state and restart at the equivalent global phase.
`VB_KEX_WASP_BOB_TEST=1` validates the cosine phase, friction, 3D boundary
tests and Z inversion in a hidden runtime probe. The controller recognizes the
exact loose sequence by its cadence and safely falls back to the stock layout
when it is absent.

The shared Spider controller now gives both retail skins their common radius
16 (Build clipdist 24), rather than the classic spider's clipdist 15, and
selects the correct KEX death recipe. The four authored death ticks face the
killer on tick 0, hold the middle pose on ticks 1-2, then play `spiddeath.wav`,
toss nine Spider gibs or one Scorpion gib1, three gib2, three gib3 and two gib4
pieces, and destroy the visible host on tick 3. These assets occupy tiles
7828-7840. The generic gib effect's delayed `A_RandomItem` callback is retained
by the shared visible actor. An invisible, save-serialized 19-tick host state
keeps controller cleanup and supplies only the old-conversion fallback. The
KEX ground/recovery callbacks also apply the authored four-unit radial
push-away when a Spider or Scorpion lands on its nearby live target, preventing
the landing animation from remaining interpenetrated. Their leap follows the
retail 60 Hz controller rather than the DOS rocket impulse: the launch aims
with the full three-dimensional target vector, has a constant 15-unit upward
impulse, takes two source physics steps per native update, applies .5 XY
friction while grounded and .75 gravity while airborne, and evaluates the
check-ground callback between those two steps. Spider keeps the authored
launch/rise/fall poses; Scorpion keeps launch/check-ground. Old saved leaps
without the new private state marker safely settle instead of retaining the
former velocity.

Their DOS three-flag, 3-damage attack is replaced in KEX mode by one authored
10-damage callback and one `spidchomp.wav` cue. The converter now writes exact
loose `SPIDER.SEQ` and `SCORPION.SEQ` files instead of squeezing them through
the demo's seven-frame attack slot. Spider retains its 6-frame base, three-pose
launch/rise/fall leap and all 14 attack blocks, including the range check on
frame 0 and hit on frame 6. Scorpion retains its 12-frame base, two-pose
launch/check-ground leap and all 13 attack blocks, with the range check on
frame 11 and hit on frame 12. All eight authored rotations are precomposited
with a stable actor origin; the 45 Spider composites occupy tiles 20700-20744
and the 48 Scorpion composites occupy 20900-20947. The files contain 291 and
419 frames respectively. `VB_KEX_SPIDER_LEAP_TEST=1` validates the launch
normalization, source-rate friction/gravity bridge, check-ground threshold and
the authored player/non-player target push-away boundaries in the hidden runtime
probe.
The skin identity is mirrored into serialized sprite state and reconstructed
on cold load, including a private-tile fallback for older saves. Runtime cadence
detection preserves the former compressed callback mapping as a safe fallback
for an old conversion, and frame clamping keeps saves portable if those loose
sequence files are later removed.

All six controllers share the remaster's contact-radius bridge: a
three-dimensional source-radius + target-radius + callback-reach comparison in
the remaster's WORLD=6 coordinate scale. It preserves the player puppet's
distinct hardcoded 95.25 radius (rather than substituting the classic Build
player's clipdist 58), so AI range and Spider/Scorpion landing push-away reach
the same boundary as KEX. The three source contracts stay distinct:
`KexAIEnterMeleeRange` is the generic `kexAI::CheckMeleeRange` (logical-centre
line-of-sight, forward cone where applicable, and the flying source's
step-height adjustment); `KexMeleeRange` is raw-origin `A_CheckMelee`; and
`KexMeleeAttackRange` is `A_MeleeAttack`, which for Fish raises the target by
half its logical height. Chase enters melee at each actor definition's
`meleeExtraDist`, `A_CheckMelee` decides whether the animation loops, and
`A_MeleeAttack` independently tests its own authored reach. This removes both
false hits after separation and missed hits caused by waiting for a literal
Build collision on the damage frame.

Target acquisition also now follows `kexAI`'s inherited `sightDistance`:
3,000 authored units (18,000 Build X/Y units) on each axis, with the source's
strict boundary. This replaces the original controllers' inconsistent
40/50/60/100/1000-unit search values only in KEX mode; line of sight and
classic behavior remain unchanged. Its KEX trace also uses the centres of the
logical source/puppet actors, as `CheckTargetSight` does, rather than the
native sequence image heights, and applies the source's 1.04-radian forward
cone to actors that lack `lookAllAround`. `VB_KEX_COMMON_ENEMY_TEST=1` includes
the exact inside/outside boundary probe and a reversible live-puppet
forward-cone/centre-trace acquisition check.

`kexverify.py` reads all six actor definitions and every retail frame block,
melee callback, range check and sound callback directly from the KPF. It also
locks the native KEX-only dimensions, range conversion, sound aliases, the
58-slot Anubis, 62-slot Mummy, 57-slot Bastet, 49-slot Fish and both 48-slot
Spider/Scorpion exact layouts, every private tile/DEF row and every generated
composite PNG.
`VB_KEX_COMMON_ENEMY_TEST=1` checks constructed sprites, Anubis'
1/18/24/25/10/9/20/1 cadence and -8/-9 death threshold, the 6/14 and 12/13
Spider/Scorpion movement/attack cadence, both four-tick death groups and their
save-stable identity, the 16/1/13/39/35/1/9 Mummy cadence and death threshold,
the Bastet 12/11/17/41/23 and Fish 8/1/14 live groups, delayed death-drop
ownership, and exact XY/Z range boundaries in a live level. Hidden,
dummy-audio, `-nosound -nomusic` probes validated
MARSH's 31 Bastets/57 Spiders, KARNAK's 9 Fish/63
Spider-or-Scorpions, TOMB's 10 Anubis, and SLAVCAMP's 21 Anubis, 4 Mummies, 27
Bastets and 57 Spider-or-Scorpions. Classic dimensions, timings and damage
remain unchanged. On a focused fixture with no ordinary enemy actors, the
dimensions line reports `SKIP` rather than a false failure; its sequence and
range checks still run.

The KEX Queen now uses a generated 104-slot, 1002-frame/chunk `QUEEN.SEQ`
and exact two-action `QUEENEGG.SEQ`. Its 280 layered authored composites
occupy tiles 20400-20679 with all eight rotations precomposited and no chunk
mirroring. Body cadence is exactly 1/10/10/10/10/11/30/16/13/62 for idle,
base, chase, charge, after-charge, flap, hurl, tail missile, pain and death;
snake cadence is 1/5/1/85 for idle, chase, melee and callback-only death.

The exact controller uses one authored 3600-point body pool, radius 64 and
scale 1.5 (clipdist 96/repeat 60). High health follows `A_QueenThink`'s
turn/strafe/tail decisions; crossing 2400 runs the 13-frame pain transition
and changes pursuit speed to eight. Low-health `A_QueenThinkStumpy` applies
the exact seed cap and hurl/charge rolls. Flap deals 50 on frame 1. Charge
runs continuously at 16 units/tick until contact, deals 40, applies 2x
velocity knockback and plays the ten-frame recovery. The type-12 seedling
projectile launches from the actor origin at z+128 and speed 32.

The hurl creates the real `Obj_Seedling_Spawn`, not a classic bouncing egg:
it starts at z+128 with the Queen's exact angle and 32-unit horizontal
velocity, has no gravity, holds for 60 ticks, then explodes at z+32 and
spawns `AI_Seedling`. Pending pods and live seedlings jointly enforce the
retail eight-seed cap. Classic Queen egg physics remain unchanged.

At body death frame 61 the six authored Queen gib actors on tiles 7841-7846
are tossed, the shared gib-death actor starts at +64, and the 200-health snake
appears at the same origin. The body is removed from the boss stat without
invalidating its saved run record.
When that egg hatches in KEX mode it now remains `AI_Seedling`, rather than
turning into a small flying Wasp: a dedicated grounded/no-drop-off controller
uses the exact one-frame chase and six-frame `SEEDLING.SEQ` bite, applies the
20-damage melee callback and `queenatk05.wav` on frame 3, and retains the
authored health 100, radius 36, scale 2.0 and speed 16. Its death tosses Queen
gibs 1/3/4/5/6, starts the shared effect at +32, and destroys the host. The
flyer identity is mirrored in the sprite save marker, and `BuildWasp()` returns
the controller index Queen's egg code expects, fixing the old out-of-range
reconstruction path.

Selkis’ type-16 Cobra projectile now renders the remaster’s red cobra loop
frame on tile 7847 and receives its transient light. The KEX path bypasses
the native type-16 hitscan shortcut and creates the real moving projectile at
the authored muzzle and 64-unit speed. `impactWallsOnly` means that the cobra
adjusts along floors and ceilings; it still collides with actors and vertical
walls, then explodes. Its actor collision intentionally deals the authored
zero direct damage, followed by the authored 224/50 radial death blast.
`homing` plus `noHomingThrust` turns its orientation toward the
target without curving its launch velocity.
Selkis now uses a generated 51-slot, 469-frame/chunk `SCORP.SEQ` on 167
precomposited authored tiles (20200-20366), with explicit eight-view art and
no chunk mirroring. Its exact idle/chase/shoot/melee/pain/wounded/malfunction
cadences are 1/8/6/9/25/40/12/20. The Cobra fires and lights the muzzle on
rendered frame 3; both rendered melee callbacks deal 20 damage on frames 0
and 6; pain and malfunction states retain their authored facing and sounds.

The zero-delay death trampoline enters the three-frame explosion loop with a
count of 24. Because `A_SelkisCountDown` exists only on explosion frame 1,
those 24 callbacks span 71 rendered ticks. Twelve scorpion-skinned critters
spawn on alternating post-decrement values, optional explosion effects use
the retail random cadence, the death sound and exit event fire once, and the
boss remains as a non-solid corpse. While alive, exact `A_SelkisThink`
decision rolls choose the health-dependent wounded/malfunction states and
increase steering speed from 12 to 24 KEX units as health falls. Pain chance
16 and player-only retargeting preserve the actor's no-in-fighting behavior.
Its authored 4000 health, radius 80 and scale 2.5 map to Build's four-unit
health contract, clipdist 120 and repeat 100. Classic Selkis keeps its native
sequence, dimensions, health and behavior.

KEX Set uses a generated 76-slot, 737-frame/chunk `SET.SEQ` on 171
precomposited eight-view tiles (20000-20170). It preserves the authored
idle/chase/melee/leap-start/air/land/soul/pain/death cadences of
1/20/18/13/1/12/15/8/30/1. Both 40-damage melee callbacks occur on rendered
frames 8 and 17; the soul is emitted on frame 7 and the muzzle is fullbright
on frame 9; leap launch occurs after frame 12; and the authored stomp, pain,
attack, quake and death sounds remain on their retail frames. The controller
detects this exact layout and retains the patched demo sequence as a fallback.

`A_SetThink` is mirrored directly: its masked decision gate runs at the retail
1-in-16 cadence, emits chatter with the authored byte roll, chooses souls only
below 1500 health on a coin flip, and chooses a leap only below 2000 health on
a quarter roll. Pursuit uses the health-scaled 10-to-20 KEX speed formula.
`A_SetLeap` launches on frame 12 at `moveSpeed * 0.5 + 5` horizontally and the
fixed 30-unit vertical impulse; landing advances through the 12-frame land
group, quakes on frame 6 and deals 40 damage to a grounded player. Pain chance
24, no-in-fighting targeting, 3000 health, radius 96 (Build clipdist 144),
scale 2.0 and the one-soul cast match the retail actor. Death plays its
30-frame animation, sends remote tag 1010 and holds the corpse without the DOS
twenty-soul substitution. Classic Set stats, sequence table and behavior are
unchanged.

KEX Mantis fireballs now retain the authored target pitch instead of inheriting
the substantially faster classic type-10 bullet velocity. Saturn shots use
the callback's fixed 16 KEX units per tick; only the rare five-shot PSX spread
uses its scripted continuous random 8–16 range.
Set’s KEX homing souls now inflict the authored 40 damage rather than the
classic type-11 record’s 30 damage. Their velocity curves with the authored
0.0218-radian turn cap. Selkis’ red cobra uses its 0.028-radian orientation
cap and the Mummy cobra uses 0.03, while `noHomingThrust` leaves both cobra
flight vectors unchanged. Classic homing remains unchanged.

The detached Queen snake uses the authored random grounded hop and turn caps,
including its half-height impulse within 512 authored units. Its ten tail
links are independent non-solid 200-health actors: only the terminal link is
shootable, destroying it exposes the previous link, and the 200-health head
becomes shootable only after all ten have been destroyed. Every joint lerps
0.2 toward its parent and follows the parent's sector, eliminating the old
ring-buffer approximation. Snake melee deals 30 and retreats afterward.
Death hides the spriteless host immediately, emits 21 randomized explosions
at four-tick intervals, and fires event 1010 once on the first of 85 rendered
ticks. Classic retains its seven-tail health ladder and save layout. Exact KEX
saves retain that same seven-short serialized field too: their extra three tail
sprite ids occupy marked spare slots in the old movement ring, which the exact
joint controller does not use. This keeps earlier save records aligned. When an
unmarked approximate-KEX save is loaded with the exact sequences, body phase
health and actions, old egg forms, and snake actions are translated before the
new controller runs; every action and frame is range-checked before indexing a
sequence. `VB_KEXSAVE_TEST=1 VB_KEX_QUEEN_SAVE_TEST=1` performs a real save/load
round trip with all ten links alive and validates every restored id and health
gate.

The sequence bridge now uses the round-trip-proven writer in `kexseqtool.py`
both to fill compatible demo layouts and to compile dedicated loose sequence
files where timing is behavior: exact `WASP.SEQ`, `HAWK.SEQ`,
`SEEDLING.SEQ`, `ROACH.SEQ`, `SCORP.SEQ`, `SET.SEQ`, `QUEEN.SEQ`,
`QUEENEGG.SEQ`, `SPIDER.SEQ`, `SCORPION.SEQ`, `MUMMY.SEQ`, and `MANACLE.SEQ`.
`KEXSEQ_PLAN.md` records
their source animations, action contracts and the remaining approximations.

### The crows

The 99 `AI_Bird` actors (seven levels; PASS and PEAK have 26 and 21) share the
native Wasp run record only as storage. In KEX mode that record carries a
distinct persistent bird identity and selects `kSeqHawk`, bird stats and bird
motion instead of inheriting Wasp behavior. The authored values are health 10,
damage 20, radius 16, scale 1.5 (repeat 60), speed 8, fullbright, and no
infighting or Wasp float bob.

`kexbirdseq.py` compiles an exact 36-slot `HAWK.SEQ` from all 36 retail bird
sprites on tiles 7600-7635, preserving each KEX crop's actor origin. Its action
groups are one-frame calm, eight-tick attack (the source has seven identical
visual frames plus `delay 8`), twelve-frame retreat and six-frame chase. Chase
plays the two wing sounds on frames 1 and 4; attack sound and the 20-damage
32-unit melee callback occur on frame 0. Calm birds perform `A_BirdWander` at
two units/tick with a 1.5-degree steering change; pursuit uses speed 8, and the
completed attack enters the authored rising/turning retreat before returning
to calm flight. The exact HAWK controller keeps the 60 Hz source `GameTicks`
and fractional turn remainder in KEX-bird-only saved fields. Its calm and
retreat velocity writes persist across both source physics ticks covered by one
30 Hz Exhumed update; retreat ends on the retail `velocity.z < 3` or
ceiling-minus-actor-height boundary, not a fixed timer. Sight acquisition cues
`birdact.wav`.

Birds ignore Wasp infighting and do not enter a pain animation. Death is
immediate, plays `birddeath.wav`, and emits seven transient
`Obj_Bird_Feathers` actors. `KEXBIRDFEATHER.SEQ` keeps the retail sprite-32
crop on tile 7632 through its randomized 15-frame loop; it preserves the
source's centred ±64/±64/±16 spawn range, f8/f14 half-unit downward writes,
and 1.024-unit floor-removal gate instead of treating feathers as generic
ballistic chunks. Identity is mirrored in `sprite.hitag`, so the unchanged
classic `Wasp` save struct can reconstruct bird, Wasp and Queen seedling
records; older saves are recognized from the private tile ranges.
`VB_KEX_FLYER_AUDIT=1` reports each identity's live stats and
`VB_KEX_FLYER_TEST=1` validates seedling 1/6, bird 1/8/12/6 and Wasp 4/8/1/4
cadence in an actual loaded level. `VB_KEX_BIRD_FLIGHT_TEST=1` is a hidden
source-math probe for Bird's 60 Hz persistence, exact fractional turns and
retreat ceiling gate.

### The scorpions

A census of every actor in the 25 retail maps (types against
`ACTOR_MAP`, the traps, the inventory and the decor fallback) found two
gameplay actors that were coming out as decor: `AI_Scorp`, 60 of them
across eight levels, and `AI_Magic8Ball`, 66 in two. The remaster's
scorpion shares the Spider controller, health (20), speed, radius, step
height and callbacks, but has its own 12-frame walk, 13-frame attack and
one-frame leap. `ACTOR_MAP[12] = (105, 1)` selects that skin and
`BuildSpider(..., bScorp)` animates it through optional `kSeqScorpion`.
`kexmap.py` compiles `SCORPION.SEQ` directly from every authored rotation:
48 slots, 408 ticks, one precomposited chunk per tick and 48 unique images
on tiles 20900-20947. No old seven-frame compression or 7700-range
retargeting remains. `seq_IsLoaded()` still provides a safe Spider-art
fallback if the optional file is absent. Vase case 0 uses this same identity;
`VB_KEXBREAK_TEST=10 VB_KEXBREAK_SCORPTEST=1` forces the callback for every
ordinary prop and checks each spawned controller marker before reporting PASS.

### The mines

The other actor the census found as decor, `AI_Magic8Ball`: 62 of them in
SUNKEN and four in MAGMA, a black sphere the remaster's anims describe
completely -- the idle polls `A_CanSeeTarget(detonate, 256)`, the detonate
plays `mineact.wav` and starts a countdown, and `magic8ball_explode` is
`Fx_Explosion01` plus `A_RadialBlast(512, 140)`; under water,
`magic8ball_explode_water` drops a random item and uses
`Fx_Explosion01_Water`. Its death anim is the countdown too (health 60,
shootable). It rides the breakables: kexmap.py
emits it as a one-tile breakable with the explode anim's blast and two
new `kexbreak.txt` columns, the sight radius in Build units (256 * WORLD)
and the fuse in ticks (`KEX_MINE_SIGHT`, `KEX_MINE_FUSE` = 45, the
remaster timing its loop in code). `KexBreak_MineTick()` arms it when a
living player is inside `A_CanSeeTarget`'s strict 256-unit 3-D radius and
logical eye-to-centre trace (rather than a 2-D Manhattan proxy), blinks it
while it counts, drops the random item if its sector is under water, and blasts
through the exploding pots' `KexBreak_Shatter`. The shared effect path chooses the exact
21-tick scale-4 land actor or scale-2 water actor at z+32, with the authored
sound, fullbright art and transient light before radial damage and Toggle-face
opening. Being shot or blasted to zero starts the fuse instead of shattering.
`VB_KEX_LOOKAT=0 VB_KEX_LOOKAT_TILE=<its
tile>` (8761 in the current tables) with `VB_KEXBREAK_TEST=100000` logs
the arming and the blast.

A sound audit alongside (every `sounds/*.wav` the remaster's defs and
anims name, against `KEX_SOUNDS`): 107 of the demo's 170 VOCs are
aliased and 82 of the 118 remaster wavs referenced are reached. Breakables
now retain four authored recipes: silent for the mine and exploding vases,
glass for ten ordinary vase types (eleven when the focused test maps add
`Obj_Vase11`), wood for the barrel, and flesh for `Obj_Vase09`.
`pot_pc1`/`2` map to `glassbreak.wav`/`woodbreak.wav`, while the existing
`blood_4` alias reaches `fleshbreak.wav`. `kexverify.py` derives and checks
the retail 3/10/1/1 and focused-test 3/11/1/1 censuses directly from the
actor and death-animation data; the runtime probe recognizes both while
retaining the historic eleven-column 3/11/1/0 fallback. `VB_KEXBREAK_SOUND_TEST=1`
checks that table at runtime and,
with `VB_KEXBREAK_TEST`, checks every shattered instance's dispatch; hidden,
muted probes covered TOMB's glass, MARSH's flesh, MINES' wood, and their silent
exploding props. Their bouncy shards likewise keep the authored `glass01..03`,
`wood01..03`, or `flesh01..03` floor-rebound family: KEX selects its variant
from session milliseconds modulo three when a fall exceeds four gravities, and
the 60-Hz toss bridge reproduces that timing without using the native gameplay
RNG. The remaining sounds with no Build counterpart get names
no VOC carries -- `k_trans`, `k_mine`, `k_bould`, `k_bldhit`, `k_anmele`,
`k_setstp`, and TOMBEND's `k_quake` in `KEX_SOUNDS` -- and
`KexSound_Get(name)` plays them:
`LoadSound()` resolves any name through kexsound.txt, so a Kex-only name
loads the wav straight into a free one of the 200 sound slots (the roster
is 80), and -1 plays nothing outside Kex mode. They sound on a transmitter
piece taken, the mine arming, the boulder's face bounce and its
impact (`KexBullet_ImpactSound`, on bullets whose SEQ is the boulder's),
and the Anubis' and Set's melee hits. Verified muted only -- the aliases
resolve and nothing else changed.

### Ramses

The remaster's Ramses ships complete in the .kpf: 55 materialisation frames,
12 manually selected talk poses, 21 vanish frames and the resting body used by
TOMBEND, plus speeches as `ramses/SP_<LANG><NN>.wav` in four languages and
AngelScript facial arrays in `scripts/ramses/SP_*.txt`. `build_ramses_tiles()`
composites all 89 frames on origin-centred canvases and writes their tile ranges,
KEX scales and visual frame periods to `kexramses.txt`. Converted KEX maps use
that actor; the DOS pixel-head, PUP data and level-ending behavior remain the
unchanged non-KEX path.

Each facial-array entry is one 4-bit state per 1/30 second. All bits matter:
bit 1 selects the lower six poses, bit 0 selects the second group of three, and
bits 2/3 select the closed/part/open variant, exactly reproducing the retail
script's `SetFrameID()` calculation. Parsing begins inside the array braces;
the old compatibility shortcut also parsed the digits in `g_SP_ENG01` and
shifted every sample by one. Which speech the hub chooses is
`GetRamsesDialog()` transcribed in `Kex_RamsesSpeech()`:
feather 11, scepter 10, anklets 8 or 9 with the cobra staff, shawl 6 or 7 with
the flame thrower, mask 4 or 5 with the bombs, sandals 3, nothing 1 -- and
coming back after he has spoken once gets the "alternate" 12 or 2. TOMBEND
gets 14 with the transmitter assembled and 13 without. `kex_language` (0..4)
picks English, French, German, Spanish or Italian for the generated UI tables.
The KPF has Italian text but no matching Ramses speech or cinematics, so Italian
uses the remaster's English media fallback while preserving its localized UI.
The remaster menu exposes this through **Options → Language Settings** (along
with the existing Audio Settings), so it is not a console-only selection.

The pads in TOMB and TOMBEND are the game's only
SCRIPT_REPEATABLE (63) sectors, so KEX_RAMSES turns them into sector effect 80
-- the 0x8000 flag the player-touch trigger tests -- and plants the lotag-61
head sprite. Three placement lessons, all learned the visible way: the
scripted spawn point sits BEHIND the casket (the remaster slides the casket
open with a scripted sector move, carried over below), and a face sprite
hugging the near plane never draws at all. LEV1 stands its own head about 200
units out and sets cstat 0x82 so it is translucent and origin-centred; the
generated actor does the same.

The native cinematic follows the two retail scripts rather than borrowing the
DOS presentation. TOMB opens the casket immediately, moves the camera with the
authored 1/16 interpolation, waits 0.5 seconds, then starts Ramses, speech and
facial data together. TOMBEND shows the body at `ramsesOrigin.z + 512`, begins
raising it after 0.5 seconds, replaces it with the spirit after 7 seconds, and
uses its own camera target. The visual 60 Hz materialise/vanish clock is selected
at render time, while the facial script and bob retain their observed 30 Hz
cadence. Use/attack/jump can skip after four seconds. End events use the script's
scheduled 120/20/.../47/43-second duration, not WAV length or audio-device
state, so `-nosound` and a slightly short recording cannot end a speech early.

No other level sprites or sectors are hidden or dimmed. Ending stops the speech
without the blocking fade loop, starts the authored vanish, releases play,
fires tag 1020 and restores the level music; a complete TOMBEND also sinks the
bier and plays its quake, while the incomplete branch goes directly to the bad
ending. A converted repeatable pad can span several Build sectors. The complete
set is disabled during release and rearmed four seconds later only after the
player leaves it, preventing a split piece from starting a second cinematic
during the first one's vanish.

`kexverify.py` checks the 55/12/21/1 ranges, shared canvases, PNG/DEF coverage,
and all 56 language/speech facial streams against their WAV rate and the runtime
buffer. Hidden, dummy-audio `VB_KEXRAMSES_VISUAL_TEST=1` runs passed on both TOMB
and TOMBEND, including the seven-second body prelude and all four phase changes.

The two remaining Kex travel actors now use the same pickup path: KILARENA's
`Obj_Ramses_Body` reloads `KEX_TOMBEND.MAP` (the source's `Map_Tomb2 = 22`) and
lands at its authored `destinationPosition`; `Obj_WarpPryamid` likewise carries
its authored `0 2816 -864` next-map position (converted to TOMB sector 5) into
`KEX_TOMB.MAP`. TOMBEND's `Obj_Choppa`
grants `PA_ALL` and finishes the level. The latter keeps the
transmitter-dependent good/bad ending decision in `FinishLevel()`.
Ordinary `Obj_Camel` exits open the remaster's overworld at the current node,
so selecting a neighbouring map does not fall through to Build's numeric-level
loader or reload the same user map.

### Sounds

The engine asks for 190 distinct sound names -- 79 in `SoundFiles[]` plus 131
more named inside the .SEQ files' sound tables -- and every one goes through
the single chokepoint `LoadSound()`, which tries `<name>.ogg/.wav/.voc` on the
search path. The remaster ships 202 `sounds/*.wav` (PCM16 mono, mostly PSX-ish
rates), named in plain words where Build's are eight-character mnemonics
(`anu_icu` = "anubis I-see-you" = `anubisact.wav`).

`KEX_SOUNDS` in kexmap.py pairs 121 of the 190 by meaning -- the actor defs
and anim scripts say which wav plays for which action on the remaster's side
-- with an acoustic cross-check between each demo VOC and its candidate wav;
three pairs are bit-identical recordings, which says the remaster reused the
original masters. The pairing lands in `kexsound.txt`, `KexSound_Alias()`
reads it, and `LoadSound()` asks there before opening the VOC. The 69 Build
sounds with no counterpart (Am-mit, the drums, most ambience loops) fall
through unchanged.

Remaster-only effects use the same alias path under private `k_*` names. A
campaign can request 34 distinct private sounds (weapons, projectiles, mines,
props and traps), so the old 16-slot lazy cache silently dropped later ones.
Its capacity is 64 now; the generated 151-row alias table itself fits in the
separate 200-row mapping table.

Playing the remaster's files uncovered three latent engine defects. The WAV
parser assumed `fmt ` and `data` sit back to back, so any file with a LIST
metadata chunk between them -- several of the remaster's -- was rejected;
`MV_PlayWAV` now walks the chunks. The loop test was `SoundBuf[26] == 6`,
which reads the VOC "repeat start" block type and on a WAV reads the middle of
the sample rate; `SoundIsLoop()` checks the buffer really is a VOC and takes
the kexsound loop flag otherwise (the flag itself was read off the original
VOCs' own loop blocks: the flamethrower jet and the stone-slide rumble). And
the pitch scaling divided first -- `7000 * (nFreq / 11025)` -- which is zero
for the remaster's 10997 Hz files; it divides last now.

### Controller vibration

The remaster's 21 `tactile/*.bnvib` curves are now read directly from its KPF
when `-kex` is active. They are 200 Hz, two-motor curves: weapon scripts drive
their authored fire/charge/stop events, and the bridge also covers the local
puppet's land/water/electric pain and `A_Quake` feedback. PCExhumed updates at
30 Hz, so each exact source interval is averaged before SDL holds it until the
next update; curves retain their original duration and motor balance without a
polling thread. Only the local player can start a curve, looped flame/manacle
feedback stops with its sound, and classic levels do nothing.

`kex_vibration` (default `1`) enables the feature and
`kex_vibration_volume` (default `50`, range `0..100`) controls its strength.
The sound-name field is 24 bytes so KEX-only names such as `k_electrocute` are
no longer silently truncated before their WAV or tactile lookup.

### Distance shading

The remaster does no distance shading at all -- everything it paints is baked
into vertex colours, which Polymost reproduces per vertex (and the converter
retains as per-surface shades for compatibility paths).
Exhumed sets `g_visibility` to 2048, four times Build's own default of 512, so
in Kex mode the engine was applying that falloff a second time on top of the
baked lighting. Anything far away went to pure black: measured at rgb(0,0,0)
down the entrance corridor in TOMB, where a capture of the remaster from the
same spot still shows (39,16,4), and 1.0% of the frame was fully crushed where
the remaster crushes none of it.

`Kex_BaseVisibility()` returns 512 in Kex mode and Exhumed's own 2048 otherwise,
so the original game is untouched. On the entrance view that took the solid
pixels from p05/p25/p50/p95 = 18/35/60/135 to 36/52/78/136 against the
remaster's 26/48/92/143, and pure black from 1.0% to none.

**Self-lit faces.** Face flag 0x400 marks 181 solid faces, 164 of them
COLONY_PSX's `colony142`/`colony149` light panels: a surface lit by itself. The
converter drew them at the vertex light around them, which left some at shade
50 beside others at -10. They take `KEX_SHADE_MIN` now (`KEX_FACE_SELFLIT`),
walls, floors and ceilings alike. The retail editor identifies the other face
bits used here: 0x1 Solid, 0x2 Block AI fallers, 0x4 Invisible, 0x8 Block
non-players, 0x20 Lava, 0x40 Water, 0x80 Slime, 0x100 Toggle, 0x200 Force
Field, 0x1000 Hidden, 0x2000 Portal, 0x4000 Underwater and 0x8000 Force sky
rendering. Collision and rendering are independent; the wall conversion below
therefore does not infer either one from whether a face happens to have a tile.

### The sky

The skies are **dome maps, not Build panoramas**: 256x512, with the top rows the
zenith, the bottom rows the nadir, and both flat pole colour with the cloud band
between them. Squashing the whole tile into Build's parallax band -- which is
what the converter used to do -- puts the nadir's flat navy on screen as a dark
stripe above the clouds. Cropping the nadir off first leaves the tile running
zenith at the top to horizon at the bottom, which is exactly what the parallax
band wants: the clouds land on the horizon, and looking up clamps to the zenith
colour. The band is detected rather than hardcoded, by walking up from the last
row while it stays flat, and only trusted when it is at least a tenth of the
image.

Cropping alone is not enough, though, because **Build puts the tile's vertical
centre on the horizon**: polymost derives the sky's v origin from
`(tilesiz.y >> 1)`. A zenith-to-horizon crop therefore has its centre half way
up the sky, and everything below that centre never appears -- so the whole
visible band came from the tile's upper half, which is the flat pole colour.
Measured on a capture: the sky was rgb(15,14,79) at every height, top to bottom,
with a light band only in the last few rows. That is the "stretched" sky.

The tile is now doubled in height with the horizon row repeated underneath, so
the real horizon lands on the centre line and the band above it is the dome's
zenith-to-horizon content in the right order. The same capture then reads
rgb(122,157,200) high up and rgb(172,200,214) near the horizon.

Build will not treat a tile as a sky panorama until `tileSetupSky()` has seen
it, which is what `multipsky` does -- without it a parallax ceiling draws black.

**Sky ceilings at their real height.** A sky ceiling used to be capped 2048
world units (341 Kex units) above its floor, on the suspicion that a ceiling
far off, nearly edge-on at the horizon, was the source of the noise band along
the top of the screen. The band was the map-name collision, and a parallax sky
never draws the plane anyway. What the cap did do was cut off everything above
it: 956 of the 2,653 capped sky sectors across the levels have a neighbour
with a textured ceiling above the cap -- TOMB's brick canopy over the temple
porch (Kex sector 258, ceiling `karnak259` at 1536 under a courtyard whose sky
sits at 1664), roofs, lintels -- and their front bands and undersides vanished
behind a sky plane 341 units up; the porch showed open sky where the remaster
shows the canopy. Sky ceilings keep Kex's height now.

**The player's step.** Walking into TOMB's temple stopped dead at the
doorway, as if at an invisible wall; a jump got past it. The doorway (Kex
sector 94, floor 1184) opens onto sector 101 whose floor is 1248: a 64-unit
step, the remaster's standard one-cell step. Exhumed's player moves with
`movesprite(..., 5120, -5120, ...)`, and `clipmove()` refuses a wall whose far
floor is more than 5120 z units -- 320 world units, 53 Kex units -- above the
feet. The remaster's player steps 66 (`Obj_Player`, `stepHeight 66`); 636
portal steps across the levels lie between 53 and 70 units, every one of them
a jump here. `Kex_PlayerStep()` is 70 Kex units (6720 z) in Kex mode and the
original 5120 otherwise, at the three player calls. `VB_KEX_WALK=<ticks>`
walks the player straight ahead for that long and logs where it gets to every
hundred ticks, which is how this was tried: from the courtyard the walk now
ends in sector 101, up the step.

The hunt also turned up a second doorway, behind the level start: the sill
into the other temple hall (Kex sector 261) had become a solid, sky-textured
wall that blocked walking and let the eye through. The porch's face links to
the sill, but the sill's face back links to sector 257, a dummy elsewhere
(260 and 262, the fire-pot platforms, do the same), so the sill's half never
found the porch and the "both halves agree" rule cleared both. A half that
found its partner by coordinate now adopts a partner that is simply unpaired;
two halves pointing at different walls are still both cleared. TOMB has 1,720
portals now, eight more.

### Related fixes

- `AngleChase()` (move.cpp) squared a flyer's step to take its length and
  the squares overflowed past ~512 units a move, clamping the length to
  INT_MAX -- flattening the dive -- and logging `overflow` some 1700 times a
  level in CAVERN. The length has a closed form (the step is (cos, sin) *
  speed * cos pitch, so its norm is |speed| * cos pitch), used where the
  squares would not fit; the exact path is kept where it always worked.

`-map` never worked in this port: it compared the whole argument token against
`"map"` and then read the filename from `pChar+4`, one past the terminating NUL
of a token that had to be exactly `-map`. No spelling of the flag could name a
file. It now accepts `-map <file>` and `-map<file>`.

### The remaster's menu

`-kex` also switches the front end to the remaster's: the book cinematic, a
PRESS START hold on the twin-Anubis backdrop, and a stone-panel menu with the
gold display font, red row highlight and the facing statue cursors.

It is gated on `-kex`, not on the art being present. That distinction matters:
the converted levels and the base game normally share a directory, so the menu
art can easily be on the search path while the *original* PowerSlave is what's
being played -- and the classic menu is the right one then.

`source/exhumed/tools/kexmenu.py` extracts the art. Three things about it are
worth knowing:

- **Everything is drawn with `rotatesprite`, which scales uniformly.** A plate
  authored at one aspect and drawn at another letterboxes, so the row and button
  plates are resized by the tool to exactly the rectangle the game draws them at
  (`ROW_W`/`ROW_H`, `BTN_W`/`BTN_H`, mirrored by the same constants in
  kexmenu.cpp). The stone panel is tiled in *square* cells for the same reason,
  with the last row and column pulled back to the edge so they overlap rather
  than leave a notch.
- **The first attempt drew rectangles by writing to `frameplace`.** That works
  in the software renderer and segfaults under OpenGL, where there is no such
  buffer. There are no framebuffer writes left.
- **The font needed two passes.** Kex ships one atlas plus a `.kfont` metrics
  file, and `rotatesprite` can only draw whole tiles, so each glyph is sliced
  into its own tile. bigfont is blue-bodied with a gold outline -- which is what
  the save-slot labels use -- while the menu entries are the other way round, so
  the tool emits both colourways. And bigfont has no `?`, `.` or `!` at all;
  those come from smallfont, upscaled to match, or a question mark would
  silently vanish from "quit the game?".

The cinematic is still played from `DoTitle()` rather than from the menu code:
`PlayMovie()`'s loop only advances when the audio backend feeds it a sample, so
calling it before the rest of the title setup spins forever.

### Doors, switches and lifts

Kex keeps a sector's trigger in a separate event table rather than on the
sector. The event table starts after `nuvquad*32` bytes of texture coordinates
and contains 8-byte records. The record is

```
u16 eventType   u16 sector   i16 eventTag   i16 params
```

verified against the `.dmap` sources on every event in test_door, test_switch
and test_lifts.

An earlier version of this section claimed `eventType` used **the same
numbering Build PowerSlave uses for its own sector effects**, on the strength
of type 1 doing in test_door what Build's effect 1 does. That was wrong for
nearly everything but 1: the remaster's editor ships the full `SLAVE_EVENT_*`
enum inside `anubis_Shipping_Galaxy_x64.exe` (the 41 values at file offset
0x52a820, the matching name pointers at 0xa415e0), and against that table
roughly 900 of the 1009 events across the retail maps were being mapped to the
wrong Build effect or to nothing. The worst was 50: Build 50 is a floor
elevator whose first stop is a neighbour's floor, so the sector sank at load --
but Kex 50 is `REMOTE_TRIGGER`, the walk-in trigger region, and the most
common event in the game (436 records). Kex 3..6 are the four key doors, 7 is
`DOOR_OPEN_ONLY_MESSAGE`, 22 lowers a floor, 23 is the touch plate, 24/25 are
lifts, 66 is the exploding bomb wall, 200..203 are the wall switches.

`KEX_EVENTS` in kexmap.py now carries that enum, and `_resolve_events()` maps
each event to the Build effect that *behaves* the same -- doors to 1/24/75
(with the key in `hitag/1000`), floor-trigger plates to a native controller,
lifts to 9/12, floors to 6/8, bomb walls to 35 -- each guarded on the neighbour the Build
mover walks to at load, since `runlist_ProcessSectorTag()` asserts it finds
one. A switch becomes a *wall* tag: Build switches are wall lotag 7 flipping
`picnum` to `picnum+1`, so `_place_switch()` finds the wall face carrying the
switchInfo texture and the tile assigner pins every switch's ON texture to the
tile after its OFF, the way animation frames follow their base.

Type 0 is also explicit as `NONE`: SETPALAC contains two sectorless
`(type 0, sector -1, tag -1)` no-op sentinels. They are not converted into
world state, while any future non-enumerated event stops conversion instead of
silently becoming an inert sector.

The invisible switch marker also retains the source event class and the exact
final Build-wall midpoint. That makes the native press path match
`UseWallSwitch()`: events 200/201 play `switch.wav`, 202/203 play
`switch2.wav`, both from the activating player. Keeping the scaled midpoint is
essential after conversion -- an earlier unscaled marker could bind a nearby
wall instead, breaking both the source sound and `tag+1` art reset. The focused
`TEST_SWITCH` map and CHAOS's retail type-203 switch are automated hidden-mode
regressions for the two sound classes.

`DOOR_OPEN_ONLY_MESSAGE` is an intentional exception to a normal Build door
use: its event tag can open the door, but a direct use in Kex executes
`UseWallSpecial()` case 7 and prints `$str_92`, **“This Door is Sealed”**.
The converter puts a KEX-only lotag-91 marker in every converted piece of
that event sector. The native use hook consumes only that marked use and shows
the source text; channel, script, boss and artifact activation keep using the
same channel-only door mover. No non-KEX map takes this path.

The four KEX key doors likewise retain their source feedback: a missing key
now maps its preserved Build key bit to `UseLockedDoor()`'s localization text,
“You Need the Symbol of Power/Time/War/Earth,” rather than the generic classic
message. This runs only in KEX mode; `TEST_DOOR` has the Power-key source case
and `VB_KEXLOCK_TEST=1` routes an automated missing-key press through the real
sector-switch handler.

`REMOTE_TRIGGER` deliberately is not another Build touchplate. The converter
places an invisible KEX-only marker in each of its 418 playable retail regions,
retaining the raw tag, mapped channel, and whether a matching target is one of
Kex's six one-shot mover types. Runtime resets raw `tag+1` switch art and live
floor plates, pulses the tag, and retires the region only for those targets --
84 retail regions do so; 334 remain repeatable. This also preserves KILARENA's
five tag-zero trigger regions through the allocated pseudo-channel and
MAGMA's signed trigger tags.

The remaster's `EnterSectorSpecial()` also gates every special except type 50
on the player's 48-unit step-height distance from the actual floor plane.
`KexPlayer_OnSectorFloor()` applies that same sloped-floor check to teleport
pads, artifact shrines, and script sectors; `REMOTE_TRIGGER` is intentionally
left ungated, matching the native exception.

The two generic lift mappings retain a local step-on link in KEX mode even
when their remote tag is nonzero: `EnterSectorSpecial()` starts type 21 and 24
directly on a grounded entry, while a remote event separately uses the tag.
The KEX-only step switch normalizes that direct route to Build state 1, instead
of treating tag 2 (or any other tag) as a state index. The remaster's
`TEST_LIFTS` map has tagged examples of both paths.

The type-23 `FLOOR_TRIGGER_PLATE` bridge also plays the remaster's
actor-local `sounds/fplate.wav` exactly once when its plate begins sinking,
before its tag+1 reset. The generated `k_fplate` alias keeps that sound KEX
only; `VB_KEX_PLATE_SOUND_TEST=1` reports the live event-to-WAV binding in a
hidden, muted test run.

Both event and actor tags are signed `i16`. Values below -1 are real channels,
not alternate spellings of the dormant -1 sentinel: MAGMA uses -5521, -5449
and -5448, and SLAVCAMP uses -666. `_channels()` now allocates those logical
tags exactly like large positive tags and writes the non-native mapping to
`kexchan.txt`; runtime scripts and traps resolve through the same table. The
verifier requires that mapping to remain one-to-one, while -1 keeps its
authored always-on/dormant meaning for the actor type that owns it.

Water came out of the same pass. Kex links an air sector's floor face (flags
bit 6) to the underwater sector beneath it; Build says the same with lotag
98/99 sprite pairs (`SetBelow`/`SetAbove` + `SnapSectors`). The 256 links
across the retail maps were previously read as solid floor -- nowhere in the
game could you dive. Floors flagged lava (0x20) or slime (0x80) get Build's
lotag 96 damage sprite.

`VB_KEX_AIR_TEST=1` runs the native 60 Hz air updater through one exact
unmasked drain period, the low-air gasp threshold, and surface recovery while
restoring the live player state afterward. `KEX_TEST_WATER.MAP` is its focused
hidden-run fixture.

The FLOATER variants are paired vertical movers, not Build's ordinary floor
bob. Retail `SetupFloatingPlatforms()` swaps the event fields and links two
surfaces: the original event sector's **ceiling** and the parameter sector's
**floor**. Every tick applies the same
`cos(time*2deg + phase + 180deg) * height/2` delta to both. The variants use
64, 128, 160, 320 or 512 Kex units of travel; the 320 and 160 variants shift
the pair by +160 or -128 at spawn. `ANG_USER` uses the original event sector
number as a degree phase (not the event parameter), while `ANG_RAND_256`
chooses a runtime phase in -256..+256 degrees.

The converter therefore does not plant a lotag-95 sprite. A source or partner
sector can be cut into several Build sectors by slope and lighting conversion,
so `_float_surfaces()` records every surviving ceiling/floor piece in
`kexfloat.txt`. `KexFloat` groups those pieces, applies the two spawn offsets,
and advances the exact two-degree cosine from `MoveThings()`, carrying actors
that are actually standing on a moving floor. The retail maps contain 44
movers and 98 converted surfaces; PEAK, TOMBEND and CAVERN_PSX pass the
`VB_KEXFLOAT_TEST=1` one-cycle paired-geometry probe, covering split partners,
runtime-random phases and both signed spawn offsets.

SCRIPTED_MOVE is the last of the sector events, and it is used twice in the
game: the casket lid in TOMB (tag 666) and Ramses' bier in TOMBEND (tag 0).
The event only names the sector; what moves it is a line each in the level
scripts -- TOMB's `$script 0` sinks the lid 256 units at load and `$script 1`
raises it 320 at speed 4 as Ramses rises, TOMBEND's lifts the bier 64 at load
and sinks it 320 at speed 6 after the good-ending address. The converter
marks the sector with a lotag 64 sprite carrying the tag, and `KexScripted`
in kexbreak.cpp keeps those four moves as a table keyed by level and tag,
applying the load-time one in `KexScripted_Init()` and the other from the
Ramses start and end hooks. A Kex unit is 96 of Build's z, and the lid is an
island sector 320 tall, so floor and ceiling move together. Which is why the
head used to appear "behind the casket": the lid it was authored behind had
never been lowered.

The rest of the event table is scripts and triggers, and there turned out to
be little of it. ACTOR_TRIGGER appears three times, all in MAGMA: a pen with
one Mantis in it and a door on the same tag. The converter plants a lotag 66
sprite, `KexTrigger` records the creatures standing in the sector at load,
and the death hook in `runlist_DamageEnemy` counts them down and fires the
channel. SCRIPT appears three times -- the arena's entrance in KILENTRY
(`$script 8`: ChangeMap KILARENA), the arena's exit in KILARENA (`$script 1`:
all six artifacts, ChangeMap KILENTRY) and a gate in SUNKEN (`$script 1`:
wait until the player holds the first key, then fire tags 2 and 1007) --
plus SUNKEN's load-time `$script 0`, which fires 1001 and 1002 to start two
doors open. A sector with tag N runs `$script N+1` when entered, which is
the same rule the Ramses pads follow. `KexScript` in kexbreak.cpp holds the
four as a table keyed by level and tag, and ChangeMap puts the new level's
file where the user map's was and lets the game loop reload -- no overworld
in between, as in the remaster. KILENTRY's six `ARTIFACT_TRIGGER` pads are
lotag-68 construction records and their matching `ARTIFACT_SPAWN` sectors are
invisible lotag-86 destination records. This is a hand-off, not a gift: KEX
only responds when the player already carries `tag - 1`; it consumes that
artifact, fires remote tag `1000 + artifact`, then recreates the pickup at the
first triangle centre of every matching destination floor, sixteen source
units above it. `KexInv` preserves that exact cycle (including a fresh pickup
sound and flash), so collecting the spawned item permits a later visit to the
pad to repeat it. No shrine artifact is planted when the level loads.

Two of those scripts exposed a channel bug. A Build channel is the low three
digits of a tag (`runlist_AllocChannel(hitag % 1000)`), and Kex tags run past
1000: KILENTRY pairs the shrine tags 1..6 with doors on 1001..1006, and
SUNKEN's load-time doors on 1001 and 1002 sit beside lifts on 2, so folding
with % 1000 welded them together. The converter now allots channels per
level -- tags under 1000 keep their number, tags from 1000 up take a free
number from 999 down -- and writes the over-1000 part of the table out as
`kexchan.txt`, which is how the engine's scripts fire a tag by the number the
level script names.

One thing about channels worth knowing before touching them: a channel's
state STARTS as its tag number. `runlist_AllocChannel` matches channels by
value, `runlist_FindChannel` seeds a fresh one with the tag, and nothing
resets it -- the classic levels allocate channels 372 and 228 the same way.
So the laser beams take "on" to mean "the channel is still at its load-time
state", and "fire" is what a touchplate does: set it nonzero, which the doors
read as open.

Still left untagged deliberately: nothing in the event table. All seven shipped
retail map scripts are now accounted for, including the non-sector Ramses camera,
body, animation and timing work. A stale note once named a "KILARENA intro" as
missing; the authoritative `KILARENA_script.txt` contains only script 1 (grant
all artifacts and change back to KILENTRY), which is already transcribed. There
is no intro action in that script, an alternate retail script, or the executable
string census.

A wrong turn worth recording: an earlier attempt read the door target straight
out of the sector record, having matched `targetCeiling 256` against a 256 at
index 3. That produced 258 doors in a 268-sector level -- index 3 is the ceiling
*height*, and the match was a coincidence of a 256-tall room. One counter-example
from data already in hand (test_breakables has 512 there with no target at all)
would have killed it before any code was written.

### The status bar

`-kex` replaces Build PowerSlave's ornate panel with the remaster's slim bar:
an Anubis head at each end, the air meter on the left and health on the right.
The pieces ship separately in the .kpf, so they are laid out at draw time
rather than blitted as one strip. The in-game frames are `main_left` /
`main_right` (136x24 each) -- **not** `bar_left` / `bar_right`, which are the
*menu* plates and carry neither the gauge boxes nor the jewel. The whole thing
is a 416-unit assembly stretched to the screen width:

    head(40) scarab(32) main_left(136) main_right(136) scarab(32) head(40)

which at 320 units is 30 / 25 / 105 / 105 / 25 / 30, and 18.5 tall with the
heads at 37: the remaster draws its HUD on a 240-unit canvas (426 wide at
16:9), where the band is 24 and the heads 48, and fitting the 416-unit
assembly to 320 is a scale of 0.77. The bar was 16 tall with 32 heads before,
a sixth too small beside the remaster. Measured off a 1970px-wide
capture of the remaster: the blue slot starts at x=501 and the red at x=1151,
so 141 native units span 477px and the horizontal scale is 4.63 px/unit. That
scale then predicts the two gauge boxes at native x 13.2..26.7 and 17.7..31.6;
they are actually at 13..26 and 18..31, and the jewel where the two frames meet
lands on the exact horizontal centre. Three independent confirmations, so the
layout is measured rather than guessed.

Each bar slot is a **hole** in the frame -- alpha 0 at x 31..117, rows 6..9 of
24 -- not a painted trough. The fill is therefore drawn *first* and the frame
laid over it, and the hole clips the fill to the slot's slanted end for free. I
had this backwards at first: a PNG viewer renders alpha 0 as white, and I read
the slot as a painted white area and drew the fills on top.

Every piece is authored by the tool at exactly the aspect it is drawn at, for
the same reason the menu plates are: `rotatesprite` scales uniformly, so a
176x24 bar asked for a 144x14 rectangle draws only 102 wide. That is what left
black holes in the middle and at both ends of the earlier bar. Flat fills are
the exception -- their aspect changes with the fill level, so they are drawn as
a run of square cells the height of the slot instead.

Pickup captions come from the remaster's localization table for the items that
mean the same thing in both games; anything ambiguous keeps the original
wording rather than being given a caption that is merely plausible. They are
shown the way the remaster shows them: stacked at the top of the screen,
centred, newest at the bottom, up to four at once in the gold menu font, each
pulsing slowly for three seconds (`KexInv_CaptionLine`, drawn by
`KexHud_Draw`); the old single message line at the top left is gone. And a
pickup washes the screen a strong blue for an instant, as the remaster does:
`ItemFlash()` tints (24, 24, 112) in Kex mode where the original gave a faint
white, and `KexInv_Pickup()` -- artifacts, dolls, transmitter pieces, which
never went through `ItemFlash` -- calls it too.

The rest of the HUD, read off the remaster's `kexSlaveHudLocal` draw code
(the texture names `gfx/airsupply_*`, `dolphin`, `vulture` are only
referenced from there, so the layout had to come from the disassembly):

- **The underwater air gauge**, `airsupply_back` (a red disc) with
  `airsupply_front` (a blue sphere) over it. It slides down from above the
  screen while the player is under water -- a quarter of the remaining way
  per frame -- to rest centred at (272, 52) of the remaster's 320x240 canvas,
  and back up at 8% per frame on surfacing; the sphere is scaled by the air
  left (the remaster's lungful is 64, ours `nAir` out of 100). Both are
  drawn at 64x53 here, the canvas being 320x200 (`KexHud_DrawAir`).
- **The team-doll badges** at the top left, the dolphin at (25, 12) and the
  vulture at (52, 12), each pulsing on a slow sine. They are the rewards the
  remaster keeps from the console game: at **sixteen dolls** the dolphin,
  and the player swims three times as fast (`0.488 * 3` on the move scale
  when not on the ground, here `x *= 3` on the input velocity in an
  underwater sector); at **twenty-eight** the vulture, and jump held in the
  air is flight (the remaster's jump plays `fly.wav` and sets its flying
  state; here `zvel = -1536` while jump is held off the floor). Thirty-one
  only adds an achievement. `KEX_DOLLS_DOLPHIN` / `KEX_DOLLS_VULTURE` in
  kexinv.h.
- **The jewel's spark**: `gfx/hud.png` (256x64, the console bar as one
  atlas) keeps three frames of the centre jewel in its right column, a
  blue spark at the top, the upper right and the right of the rim; the
  remaster draws one by the player's heading in 45-degree sectors, mirrored
  and flipped for the other five -- a compass on the bar, north up (Kex
  yaw 0 faces +Y, `x = sin yaw`, clockwise; in Build's terms yaw = angle +
  512). Here kexmenu.py cuts the spark itself out of the first frame
  (`hud_spark.png`) and `KexHud_DrawSpark` puts it at one of eight points
  around our jewel's centre.
- **Widescreen wings**: on a canvas at least 2.27:1, `gfx/wings.png` replaces
  each scarab ornament in the 544-unit HUD assembly. The converter emits the
  resized art and a mirrored right-hand copy, and the HUD keeps the ordinary
  416-unit assembly on narrower screens.

### The traps

Four actor types make up the remaster's traps, and none of them was being
converted: `Special_FireballSpawner` (196 across the retail maps -- GORGE and
SELPATH alone have 20 and 19), `Special_LaserSpawner` (20, COLONY),
`Special_BoulderSpawner` (14) and `Special_LaserBeamSpawner` (127, the
Kilmaat force fields in CHAOS, COLONY_SAT, SHRINE, SETPALAC and THOTH).
Whole rooms were missing the thing that made them dangerous.

The first three are one Kex class, `kexSlaveFireballSpawner`. Its tag is an
event pulse, not a continuously enabled channel: `FireActorEventFromTag()`
walks the source actor array, creates one factory per source sector that does
not already own one, and removes that factory when `tag+1` fires. A fireball
or boulder factory activates every 60 native ticks, a laser factory every 10,
and `extraDelay` staggers both factories and their emitters by eight ticks in
source actor order. The KEX-only `FuncTrap()` path now reproduces those exact
periods and phases rather than using Build's approximate 62/64-tick repeating
trap. It also preserves the source-sector identity when conversion splits one
Kex sector into several Build sectors. Converted lotag-72 construction records
retain the broader retail stop rule -- any source actor in that sector whose
tag is one less can remove the factory -- and the runtime consumes those
records before play. Classic PowerSlave traps keep their old channel and
countdown behavior unchanged.

The boulder spawner becomes the same trap throwing a new bullet type, 17:
the trap fireball's twin in speed, with the remaster's direct damage (35),
scale 2 and its boulder sprite on BOULDER.SEQ, which is FIRETRAP.SEQ's
eight views on the one round tile. BOULDER.SEQ is a name the classic data
does not carry, so the sequence loader treats it as optional -- absent, it
loads nothing and says nothing. On impact it now executes the retail 192/64
opening blast and throws four gravity-driven shard actors drawn from
`projectiles/boulder_shard` on tile 7849. It is no longer a flying-fireball
approximation: the KEX-only native path applies the authored 0.75 gravity,
1.9375 reflection power, 32-unit collision height, 36-unit step clearance and
eight expiring floor contacts. Walls and ceilings reflect it, actors take the
separate 35 direct damage, and the eighth floor impact (or a stopped
projectile) runs the death callback. Its four pieces then use that same shared
KEX `A_TossActor` launch and rest removal as the creature and vase debris. The
converter launches every shared
spawner from the source face polygon's exact centroid plus its 32-unit inward
normal rather than from the hidden editor actor, and the native path retains
all three normal components. That matters to 27 vertical and four sloped
retail fireball emitters which previously fired horizontally. Fireballs and
boulders use the authored speed 12; lasers use 16.

The fireball branch also creates built-in actor 200,
`Fx_FireballShootPuff`, at that same muzzle before it attempts to create the
projectile. It is not the projectile's orange explosion effect: the actor uses
the eight visible frames of `effects/magicPuff`, scale 2, fullbright and a
red `(0.75, 0.25, 0.25)` color multiplier. The converter composites those
shipped layers onto tiles 21900--21907 and writes optional `KEXMUZZLE.SEQ`;
each frame appears twice so the remaster's `.25` animation speed at 60 Hz stays
15 fps in Exhumed's 30 Hz simulation. Only configured KEX fireball spawners
create it. Laser, boulder and classic traps do not.

The laser spawner takes Build's
other trap, the arrow (lotag 77, bullet 15): a fast straight shot, which is
what a laser bolt is. Its ARROW.SEQ is rewritten onto the remaster's beam
bolt -- the side view stretched into a streak the way the remaster's
"stretchy" Proj_Beam is -- when the converter is given the demo data
(`kexmap.py <kex dir> <out dir> <demo dir>`, `patch_demo_seqs()`). The KEX
path gives it Proj_Beam's authored 100 direct damage, radius 36, height 8,
zero step height and plane-gliding `impactWallsOnly` behavior. Like the retail
shared spawner it plays `fballshoot.wav`; `tr_arrow` remains only a classic
arrow fallback. As a compatibility
fallback, the same pass moves Set's melee hit flags from the DOS data's frames
6 and 15 to 8 and 17. A normal conversion then supersedes that patched layout
with the generated exact `SET.SEQ`, including the retail leap, cast, pain and
death cadence. The direction was the thing to
get right: the actor's own angle is 0 on most of them. The record's `p2` is
the wall FACE the spawner sits on (the actor stands one step off that face's
first vertex, in the face's sector), and the face normal points into the room
-- stepping along it stays in the sector, stepping against it leaves -- so
`_face_angle()` turns the horizontal component into the Build yaw while zvel
carries its vertical component. The tag and tag+1
events use the same collision-free channel allotment as the sector events.
Build keys a wall-texture flip off
the same hitag (the trap's "mouth" lighting up), which in a converted map
would catch a switch wall on the same channel in the same room, so in Kex
mode the trap is built with -1 there. `kMaxTraps` went from 40 to 64: COLONY
carries 30 spawners. `VB_KEX_TRAP_TEST=1` runs a no-projectile deterministic
probe over the loaded map's real factory groups, phase clocks, shot counts and
an isolated stop event.

The laser beam is different: `kexSlaveLaserBeam` is a standing force field,
drawn in the remaster as a run of 16x16 bolt sprites (`projectiles/beam_loop`,
`A_BoltJitter`) from the emitter to the first wall, 100 damage on contact,
switched off by its tag's channel, and -1 on the ones only the Kilmaat
Scepter gets past. Build has nothing like it, so `KexBeam` in kexbreak.cpp
builds it: the converter plants a lotag 65 origin (hitag = channel, 1000 for
always-on) aimed along the face normal, `KexBeam_Init()` hitscans from it to
find the far end and lays a bolt every 96 units along the line with the
remaster's own bolt tile (`kextrap.txt` names it) -- the scan starting a
stride into the room, since a ray that begins on the emitter's own wall hits
that wall at zero length, and the spacing stretching on a level whose beams
would otherwise run the 1024-bolt pool dry (COLONY_SAT: 55 beams, 191
units) -- and each tick re-rolls a
small jitter on every bolt from a private LCG -- never the game's RNG, which
the lockstep peers must all draw from identically -- hides the run while the
channel is active, and hits any player within 96 units of the segment at
body height for 100 with a one-second cooldown, unless they hold the
scepter. Which is the scepter's whole job, so it now has one.

### The collectibles and the status screen

PowerSlave Exhumed is the console game, and the console game had an inventory
Build PowerSlave never did: six artifacts that change how the player moves,
eight transmitter pieces that decide the ending, 31 team dolls and five
ankhs. All of it is in the retail maps as actors -- `Arti_*`, `Transmitter_N`,
`TD_DollNN`, `Item_Health_AnkahN` -- and all of it was being converted into
scenery you could walk through.

They are pickups now. Build's item lotags 6..60 include a handful the original
accepts as "pickable item" and then does nothing with (10, 15, 24, 31, 34..36,
39..44, 51, 58); the converter (`KEX_INV_ACTORS`) puts the artifacts on 34,
the transmitter on 36, the dolls on 39 and the ankhs on 40, the hitag saying
which one, and `kexinv.cpp` is what takes them. The art in the world is the
remaster's own sprite, sliced by the same `build_actor_tiles()` pass as the
vases. The actors are resolved by NAME against defs/defines.txt rather than
by number, because the doll numbering has a hole in it (170 is unused).
Ownership is per player in `KexInvList[]`, reset with the rest of the
inventory on a new game, and saved beside the classic slot file as
`kexinv.sav` -- the loader seeks into `savgamea.sav` by fixed record size, so
growing that record was not an option. Its current `KEXINV3` records also
carry difficulty, the exact KEX map path and campaign-complete state; V1/V2
inventory records migrate on read.

What they do, in Kex mode only:

- Sandals of Ikumptet: the jump is a quarter higher (-4480 against -3584).
- Sobek Mask: `Arti_Mask` was already Build's mask pickup (lotag 23), and
  stays that; the artifact bit is set beside it. It is permanent but does not
  grant infinite air: it extends the remaster's underwater depletion cadence
  from every 5 source ticks to every 100, enough for the long water routes.
- Shawl of Isis / Feather of Horus: jump held while falling clamps the fall to
  a float (1024) or a hover (128).
- Protective Anklets: lava and slime damage halved, where move.cpp applies it.
- Kilmaat Scepter: walks through the laser beams (see The traps).
- Transmitter: the ending. `FinishLevel()` on TOMBEND plays `goodend-*.ogv`
  with all eight pieces and `badend-*.ogv` otherwise. In the KEX path, the
  incomplete branch now starts that bad ending as soon as Ramses finishes,
  matching `EndRamsesEvent()`'s `GetQuestItems() != 0xFF` test in the TOMBEND
  script. `VB_KEX_SKIPMOVIES=1` is an unattended-test override that skips the
  title, intro, and either ending; ordinary KEX gameplay always plays them.
- Ankhs: each raises the health ceiling by a fifth of the base, so all five
  double it -- `KexInv_MaxHealth()` is what the respawn, the healing caps,
  the venom bowl and the HUD bar consult in Kex mode -- and fills the player
  up to it, with the remaster's "Maximum Health Increased!".
- Dolls: the tally, and the doll names as pickup captions (the pickupMessage
  of each doll's actor def -- the Lobotomy team, and Nightdive's).

The status screen is the console game's, on the Inventory key (Shift in the
fresh KEX layout; the converted maps have no usable items for that key to do
anything else). Four pages -- Map, Arsenal, Artifacts, Transmitter, the remaster's
`str_044..047` -- walked with left and right, up and down picking an item so
its description shows in the plate at the bottom. Every icon is the
remaster's: `gfx/menu/menuweapon_*`, `menuartifact_*`, `menukey_*` (dim and
lit), `menutransmitter_*` and the lit screen `_on`, `menuteamdoll`,
`menumap_open/closed`, the `bar_left/right` plates under the title and
`wheelslot` behind each weapon. kexmenu.py slices them into tiles 7400..7447
and writes every string the screen shows to `kexinv.txt` plus the French,
German and Spanish variants (`KexInv_Text()`), with the `\n` and `${0}` of the
localization table left for the engine to expand. It also slices the remaster
font's accented glyphs and the menu decodes UTF-8, so those tables render as
authored rather than falling back to ASCII. The transmitter is drawn assembled,
the pieces the player lacks as
dark silhouettes; the eight pieces' positions in the device are laid out here
by eye (`s_kexTransPos`), the remaster keeps that layout in code, not data.
The weapon order is the console game's, which swaps Build's flamer and
grenade; `gun.cpp` applies that order to KEX cycling and pickup
auto-selection as well as this screen. The eighth slot is the Manacle (below).
The KEX weapon-wheel action opens the Arsenal alone, focused on the equipped
weapon: directions traverse its 4-by-2 grid and Select equips an owned weapon.

The KEX grenade slot now throws retail `Proj_AmunBomb`, not the DOS timed,
bouncing grenade under remaster art. `bombChargeThrow` starts at 16, advances
one unit on every held `bomb_charge` frame, clamps at 48, and resets only when
released; ammo is consumed on the throw callback rather than on entering the
hold state. The spawn trace uses the player's authored radius 90 and z offset
80. Horizontal velocity is `dir * charge`, while vertical velocity is
`8 + (charge + 8) * dir.z`, converted through the 6x XY / 96x Z metrics.
After release the bomb applies KEX's default 0.75 gravity, has no fuse and does
not bounce: its first wall, actor, floor or ceiling impact runs the radius-512,
140-damage opening blast. The converter writes one-frame fullbright
`AMUNBOMB.SEQ` and tile 7848 directly from `projectiles/amun_bomb_idle`; both
are optional and classic data keeps the original grenade lifecycle. The green
Cobra Staff likewise uses its authored 512/100 non-opening death blast instead
of the DOS 200/10 values. `VB_KEX_BOMB_TEST=1` validates charge, release-time
ammo, spawn scale, velocity, gravity, no-fuse/no-bounce state and blast recipe.

KILENTRY contains six artifact actor records stacked outside every sector.
Retail KEX assigns them sector -1 and does not instantiate them; the converter
now documents and mirrors that rule instead of treating them as a missing
script hand-out. The map tile base moved from 6000 to 8000: with the
collectibles' sprites added the texture run from 6000 reached 6988, one level
short of the menu art at 7000.

Every unattended run takes `VB_HIDE_WINDOW=1 SDL_AUDIODRIVER=dummy` (the window is created hidden
and never raised; rendering and screenshots go on offscreen),
`-nosound -nomusic`, and `-usecwd -nosetup -cfg /tmp/.../vbtest.cfg` with an absolute temporary
config path. A capture-capable probe also sets
`VB_SCREENSHOT_DIR=/tmp/.../screenshots`; game selection can change the working
directory, and this post-cvar test-only override keeps `VB_CP_CAPTURE` and
unattended KEX menu screenshots out of the installed game-data directory.
`-nomusic` now uses the same dashed option parsing as the other
standard launch flags, keeps effects enabled when `-nosound` is absent, and
prevents both level tracks and Theora movie audio from loading. Ramses dialogue
remains a sound-effect path and is not suppressed by `-nomusic`. The live
`mus_enabled` setting also stops a current soundtrack immediately without
interrupting dialogue; `VB_KEX_MUSIC_STOP_TEST=1` proves the live transition
against TOMB's real level track. Hidden runs
also log and dismiss native message boxes (answering
No to questions), so an unattended error cannot leave an invisible modal
dialog behind. `-nosetup` also prevents a fresh temporary config from opening
the native first-run setup window. The explicit config matters because the game writes it at
*startup* (`CONFIG_WriteSetup(1)`), not just on exit. `-cfg` is parsed ahead of
`CONFIG_ReadSetup()`, and its cvars file follows it as `<name>_cvars.cfg`
(`CONFIG_CvarsFilename()`), keeping the player's profile untouched. The
unattended check is
`VB_KEXINV_TEST=<ticks>`: that far into a level it
takes every collectible the level holds as if walked over, logs the tally,
then pages through the status screen screenshotting each page.
Enemy-sequence smoke tests use `VB_KEX_FLYER_AUDIT=1` and
`VB_KEX_FLYER_TEST=1`; the latter spawns a disposable protected seedling,
checks all three flyer sequence contracts and authored stats, removes it, and
restores the player's prior invincibility state. `VB_KEX_WASP_BOB_TEST=1` is a
separate no-actor probe for Wasp's 60 Hz chase bob: it checks the source cosine
phase, friction, exact full-3D/height gates and Build-Z conversion.
Sentry probes use `VB_KEX_SENTRY_AUDIT=1` to log the first live actor's stats
and exact-file detection, or `VB_KEX_SENTRY_TEST=1` to spawn a protected
disposable actor, force one attack and one pain cycle, verify the retail
1/8/12/10 cadence, frame-6 shot/fullbright, frame-10 pain exit, dimensions and
speed, then remove it and restore the player's invincibility.
Set probes use `VB_KEX_SET_AUDIT=1` for live logical health, dimensions,
health-scaled speed and exact-file detection. `VB_KEX_SET_TEST=1` spawns one
protected disposable Set and forces melee, soul-cast, pain and leap-start
cycles; it verifies the 1/20/18/13/1/12/15/8/30/1 cadence, melee frames 8/17,
soul frame 7, fullbright frame 9, launch frame 12, authored stats and internal
health scaling before restoring player invincibility.

### The Manacle

The remaster's eighth weapon, the console game's charged bolt: hold fire
and the hand crackles, release and it throws a fan of bolts, one more for
every stretch of charge. Build PowerSlave has seven weapons and no code for
an eighth, so this is the one weapon added from scratch. `kWeaponManacle`
sits between the ring and the mummified hand in `gun.h`; `kMaxWeapons` is 8
and `nAmmo[8]` takes the byte that was padding, so the save layout is
unchanged. The CHAOS pickup is lotag 31 (`Item_Weapon_Manacle` in
`KEX_INV_ACTORS`), which Build accepts as a pickup and did nothing with;
`player.cpp` gives the weapon with six charges in Kex mode and leaves the
lotag a no-op otherwise. `FillWeapons` and the weapon-select loops know the
eighth slot; the ammo cap is six.

The weapon runs on the stock state machine with the grenade's "hold the last
frame" trick in state 2: while fire is down the charge counter climbs one
step every `KEX_MANACLE_CHARGE_TICKS` (12) up to `KEX_MANACLE_MAX_CHARGE`
(8); letting go moves to state 3, whose first frame fires 1 + charge bolts
on the exact same authored aim line: the remaster's `A_FireBolts` loop sets
every beam to the player's current yaw and pitch, with no added spread or
weapon-local view quake. The bolt is bullet type 18 (100 damage, the beam
bolt's `kSeqArrow` art, so it looks like the laser traps' shot), so it uses
the trap art already carried.

`MANACLE.SEQ` is written by `build_manacle_seq()` in kexmap.py in the
layout the `WeaponInfo` row expects (raise, lower, idle, charge = the
remaster's fire + charge anims, release with frame 0 flagged 0x80 and the
charge frames fullbright). Each frame of the remaster's
`weapons/manacle_*` anims is one chunk on one tile from
`KEX_MANACLE_TILE_BASE` (7640), the frame's four or five 64x64 sprites
composited by `build_seq_tiles()` and squashed 240 to 200 vertically; the
chunk sits at the frame's bounding box relative to the remaster's hand
origin (screen y 214), which is what puts the idle wrist off the bottom of
the screen and the raised release hand fully on it. The raise's first seven
frames are empty in the remaster too -- the hand comes up from below. Three
sounds are cued from the SEQ (`BRC_UP`, `BRC_FIRE`, `BRC_CHRG`, aliased to
the remaster's bracelet wavs in `KEX_SOUNDS`). `seq_IsOptional()` lists
"manacle" with "boulder" so classic data logs nothing about the missing
file.

`VB_KEXINV_TEST` finishes its sweep by giving the weapon, holding fire and
releasing, capturing the charge and release frames and logging
`Kex manacle test: weapon 7 state 3, last release fired N bolts, ammo M`.

### The overworld

The remaster picks levels off a hand-painted map rather than from a list: each
level sits at a point on it, joined to its neighbours by compass directions, and
the arrow keys walk that graph. New Game goes there after the save slot. The
marker is the Eye of Ra, spinning through the remaster's own 31-frame cycle --
the sprite sheet, the index-to-rectangle table and the frame order are all
parsed out of `sprites/misc/overworld_hd_*`, so it spins the way Kex spins it.

None of the campaign is hardcoded. `kexmenu.py` reads the positions and the
`nextmap_north/east/south/west` links out of the remaster's own
`defs/mapInfo.txt` and resolves the `$str_NNN` level names against each
available localization table, writing `kexworld.txt` and its French, German,
Spanish and Italian variants for the game to load with `kex_language`.

**Campaign saves retain the remaster map.** Converted KEX levels use Build's
user-map path, so the classic slot's one-byte numeric `nMap` identifies every
one of them as the same sentinel and cannot restore the level by itself.
`KEXINV4` stores the full `gUserMapFilename`, a valid bit, the terminal
`mapDestination -2` state and the native one-byte-per-map unlock vector per
slot. Loading applies that state before
`LoadLevel()`: a path from an installation that moved is retried by basename
through the mounted KEX search path. V1/V2 slots retain their inventory and
difficulty but, because they never stored a map, resume at the authored
`kexstart.txt` opening rather than accidentally loading classic `LEV1`. V3
slots retain their exact map. Since V1--V3 exposed the whole graph and cannot
say which routes were earned, migration leaves their routes unlocked rather
than trapping an existing campaign.

**Checkpoint saves are durable too.** The compact `KEXINV4` records remain
unchanged; an active scarab writes a validated `KEXCHK1` companion beside its
own slot instead of padding every slot to the maximum map size. It contains the
checkpoint-time player state, removed authored actors, channel values and the
used sector/wall geometry. Loading marks that snapshot pending before
`LoadLevel()`, so the ordinary checkpoint-rebuild path restores its routes and
spawn state exactly as it does after death. A missing, older, damaged or
wrong-map companion safely starts the selected level at its entrance; saving a
slot before reaching a scarab writes an explicit empty companion and cannot
resurrect a prior campaign's checkpoint.

New Game clears every map byte, matching `kexSlaveGame::NewGame`; the opening
node can still be selected, but movement to a destination is accepted only
when its byte is set. Touching a level's Camel marks all four authored
neighbours from `kexworld.txt`, exactly like native
`UnlockAdjacentMaps(slaveMapInfo*)`, and selecting or directly entering a map
marks that destination visited. Persistent player state is staged across the
classic engine's `RestartPlayer()` rebuild, so loading or travelling no longer
silently heals the player, raises a low ammo pool to the DOS campaign minimum,
or rewrites the selected/previous weapon. Per-map keys and the map scroll are
deliberately not staged. Checkpoint restoration still runs afterward and
therefore retains precedence on a death reload.

The KEX slot picker bypasses the classic name-entry screen, so
`menu_GameSave()` now creates the fixed 641-byte `savgamea.sav` backing file
on a clean install. The remaster screen labels mapped slots with their map title
(or `CAMPAIGN COMPLETE`), disables empty load rows and confirms overwrites.
The native travel object's `requestSave` byte is honoured at the same point:
`Obj_WarpPryamid` changes to `Map_Tomb1` and then saves that destination;
`Obj_Choppa` grants all six artifacts, marks the campaign terminal and saves
before the ending. `Obj_Camel` and `Obj_Ramses_Body` do not set `requestSave`,
but the native map-transition commit itself updates the active slot; their
destination and unlock changes are therefore saved as well.

`VB_KEXCAMPAIGNSAVE_TEST=1` with an isolated `VB_KEXINV_SAVE_FILE` exercises
V1--V4 migration, relocated V5 map/unlock/play-time state, health staging and
terminal round trips. `VB_KEXOVERWORLD_TEST=1` validates the fresh lock state and exact
opening-node adjacency. `VB_KEXTRAVEL_TEST=1` on PEAK, `=22` on KILARENA, and
`=63` on TOMBEND, plus `VB_KEXTRAVEL_TEST_SLOT`, drive the actual
pyramid/Ramses-body/chopper actor respectively, while
`VB_KEXMENU_LOAD_TEST=<slot>` checks the two-file menu decode; tests can also
isolate the classic backing path with `VB_EXHUMED_SAVE_FILE`.

**Two maps, one coordinate space.** The remaster ships both the original
`gfx/overworld_orig.png` (640x512) and a repaint, `gfx/overworld.dds`
(6000x3072, DXT1) -- and it draws the repaint, while `mapInfo.txt`'s
`overworld_x/y` are still in the *original's* space. The transform between them
was measured rather than guessed: both were reduced to luminance and a
four-parameter search maximised their correlation, which peaked at 0.71 -- high
for a repaint -- and node positions placed through it land on the right
landmarks. The tool bakes the result into `kexworld.txt`, so the game never has
to know the two maps differ.

The DDS is decoded by averaging each DXT1 block rather than expanding it: 1.4
million blocks is more than a pure-Python per-pixel decode wants, the block
already stores its endpoints and weights, and the quarter-size result is more
resolution than a Build tile needs anyway.

**The classic port's own level-map screen is skipped in Kex mode.** `showmap()`
runs before every level start, and leaving it in meant picking a level twice --
the overworld, then the old map screen. Worse, `showmap()` returns a level
*number*, which overrode the map the overworld had just chosen. Skipping it also
cured a level that quit back out a few seconds after starting.

### The boss levels' scripts

The two level scripts not transcribed before, SELBUROW's and SETARENA's,
are the same script: `$script 0` at load checks whether the player already
holds the level's artifact -- Selkis' scepter, Set's shawl -- and if so
removes the boss (map actor 41 / 67, Selkis and Set themselves) and fires
tag 1010, the exit; otherwise `$script 1` polls every two seconds and fires
1010 once the boss actor is gone. Nothing was firing 1010 here, so neither
level could be finished. `s_bossRule` in kexbreak.cpp keys the boss on its
statnum (Selkis 122, Set 120, Queen 121). It preserves the two-second
absence poll for an actor removed before a reload, but death itself uses the
remaster's authored moment: Selkis fires 1010 when its 24th countdown
explosion becomes the persistent corpse, Set fires it on the final death
frame, and Queen fires it with the first snake-death explosion. The
artifact-held load branch uses the script's `actor.Remove()` semantics -- no
death timeline, drops, Selkis scorpions, corpse, or kill trigger -- then fires
the exit immediately, as the remaster does. This also fixes the
returning-player case, which previously removed the boss but left the exit
closed. `VB_KEXBOSS_TEST=1`
kills the boss at tick 60 and confirms the exit tag; `=2` grants an
artifact and runs the immediate load rule on the two artifact boss maps.

**The completability audit.** With those two found, every map was audited:
each event that only acts when its tag is fired (trigger-only and message
doors, door closes, channel lifts, scripted moves) against everything that
can fire the tag (plates, trigger regions, switches, actor triggers,
use-activated doors sharing the tag, EXPLODE sectors -- which fire on being
blown, as Build's bomb wall does -- and the transcribed rules). Fifteen
tags were dangling; nine were EXPLODE listeners misread by the audit, and
the rest are three remaster rules the scripts do not spell out:

- **KILARENA's 1010** is the same boss convention: the level holds the
  Kilmaat Queen (map actor 2), and her death opens the exit to the script
  sector that returns to KILENTRY. A third boss rule, statnum 121, no
  artifact.
- **Taking an artifact fires 1000 + its index.** SANCTUAR's message door
  1000 sits with the sandals (index 0), SETARENA's 1002 with the shawl (2),
  KILENTRY's six lifts 1000..1005 with its six shrines, and nothing else
  fires any of them: the remaster's own hardcoded rule.
  `KexInv_GiveArtifact` fires it on a new artifact, and `KexScript_Init`
  fires it at load for every artifact held, the way the boss levels' scripts
  treat an artifact brought in. SELBUROW's scepter would fire 1004, which
  nothing there listens on: a tag past 1000 the converter never allotted is
  now refused by `KexChan()` rather than falling through to `tag % 1000` --
  channel 0, which every tag-0 effect shares.
- **TOMB's 1020**, the message door beside Ramses' crypt and its script
  sector, opens when his speech ends (`KexScript_RamsesEnd`;
  `VB_KEXRAMSES_TEST=1` calls it at tick 100 without the speech, and with
  `VB_KEX_LOOKAT_TAG=1020` the door is seen to rise and hold).

Testing these turned up that none of the tag-past-1000 rules had ever
reached their channel on six levels: the kexchan.txt row parser read the
level name into `char[16]` with `%15s`, one short for the sixteen-character
names (KEX_KILENTRY.MAP, KEX_KILARENA.MAP, KEX_SANCTUAR.MAP,
KEX_SELBUROW.MAP, KEX_SETARENA.MAP, KEX_COLONY_*), so the name's last letter
fell into the tag field and the row was rejected; the lookup then fell
through to `tag % 1000` and the boss and load rules on those levels were
firing channel 0. The field is 32 wide now and an unresolved tag fires
nothing, with a log line saying so.

Watching the doors themselves (`VB_KEX_LOOKAT_TAG=<kex tag>` stands the
player before the first sector on that tag's channel and logs its
heights every ten ticks; the tags are read back from the map file, since
`runlist_ProcessSectorTag` clears them) found two more:

- **A channel found by its state is lost once a mover has run.**
  `runlist_AllocChannel()` matches `sRunChannels[].c`, which starts as the
  channel number; a door's elevator rewrites it on its first cycle, so a
  rule that resolved its channel at fire time was allocating a fresh one
  nobody listened on -- SANCTUAR's door never moved. The plates keep their
  channel INDEX from build time, and now so do the rules:
  `Kex_ChannelIndex()` resolves every tag a rule can fire in
  `KexScript_Init`, before any mover runs, and everything fires by index.
- **Open-only doors and active lifts came back.** Build's channel-only
  ceiling door (24) and lift down (9) are built with a pause switch that
  sends them back 60 / 150 ticks later, right for a plate-driven door and
  wrong for the remaster's `DOOR_OPEN_ONLY_MESSAGE` and
  `FLOOR_LOWER_ACTIVE`, opened or lowered once by a script, a boss or an
  artifact. The converter plants a lotag 73 sprite in such a sector
  (`kexhold`), `KexHold_Reset()` scans the loaded sprites for it from
  `InitSectFlag()` -- `LoadObjects()` processes sector tags before
  `ExamineSprites()` reaches the sprites -- and `runlist_ProcessSectorTag`
  leaves the pause switch out for those sectors. SANCTUAR's door,
  KILENTRY's lift and SETARENA's exit now stay where their tag put them;
  `DOOR_TRIGGER_ONLY` (2), driven by walk-in regions that fire again, keeps
  Build's auto-close.
- **`DOOR_CLOSE` doors stood shut for good.** Kex authors them closed,
  ceiling on the floor, and opens them at spawn so their tag can close
  them; Build's 75 travels between the map's ceiling and its floor, so a
  ceiling on the floor had nowhere to go and the ten of them across seven
  levels (SUNKEN's three, one each in GORGE, KILENTRY, MAGMA, MINES,
  SANCTUAR, two in SHRINE) blocked their passages from the start. The
  converter raises such a sector's ceiling to its highest neighbour's;
  `VB_KEXKEY_TEST=1` with `VB_KEX_LOOKAT_TAG=2` on SUNKEN shows the door
  standing open and descending once the key rule fires.
- **The bomb walls were open passages, and blew as the finale's towers.**
  `EXPLODE` became Build's 35, the energy block, whose sector stands with
  its floor raised (the block's health is that height) and drops to the
  neighbour's floor when blown -- but Kex authors the volume open and
  raises it at spawn, so the eleven in SETARENA, the two in MAGMA and those
  in COLONY_PSX and SELBUROW were walkable from the start, their faces
  repainted with the DOS energy texture, and blowing one printed "N
  TOWERS REMAINING" and, on the last, started the DOS finale. Now the
  converter raises the floor to the ceiling and leaves a lotag 74 sprite at
  the authored floor (`kexbomb`), read into a table with the hold markers;
  in Kex mode the block keeps the faces, takes the authored floor as where
  it drops to, blows on one blast, fires its tag and blows every sibling
  on it (`FuncEnergyBlock` on the channel message), and does none of the
  towers bookkeeping, and settles whatever stood on the block onto the
  dropped floor (SETARENA hides team doll 16 inside one).
  `VB_KEXEXPLODE_TEST=1` blows the first at tick 100;
  with `VB_KEX_LOOKAT_TAG=668` SETARENA's sector 22 is seen to drop from
  its ceiling to its authored floor, all eleven going together.
- **Every lift and every drop pad was untagged.** The same audit, run over
  the events whose Build mapping is guarded on a neighbour's height, found
  all 18 `FLOOR_RAISE_WAIT_LOWER` lifts and all 28 `DROP_PAD`s emitted
  with no effect at all. Kex authors the lift at the TOP, level with the
  ledge it serves, and lowers it at spawn; Build's 12 starts low and rises
  to a higher neighbour, which such a lift never has. The converter lowers
  it to the lowest neighbour's floor now (the ledge is the higher neighbour
  it rises back to), and an untagged one gets a step-on plate of its own in
  Kex mode -- ridden, it rises, waits its 150 ticks and comes back down.
  A drop pad is now kept out of Build's ordinary mover path altogether.
  `SetupFloatingPlatforms()` swaps its event sector with its parameter: the
  parameter-sector **floor** is the surface the player stands on, while the
  original event sector's **ceiling** is its linked mate. `kexmap.py` writes
  every surviving split piece of those two surfaces to `kexpad.txt`; the
  native `KexPad` controller waits the authored 30 Build ticks, simulates the
  Kex 60 Hz accelerated `(4+a)` fall, restores switches with the pad's raw
  tag on landing, and accepts that same tag to raise at four Kex units per
  source tick. Thus CAVERN_PSX's 196/197 pair and all 14 independent PEAK
  pads change only their authored paired geometry, rather than trying to
  approximate the behavior with a lotag-6 elevator or a lotag-81 marker.
  `VB_KEXPAD_TEST=1` runs the first pad through the complete descent/reset/
  return cycle and checks that every paired surface stayed in lockstep;
  `VB_KEXPAD_TEST=5200` targets PEAK's high-tag pad and also exercises the
  generated `kexchan.txt` allocation.
- **The War and Earth key doors asked for the wrong keys.** A key door's
  hitag thousands are a BITMASK of keys, not a number: the DOS maps carry
  1, 4, 6, 7, 8 and 9 there (power 1, time 2, war 4, earth 8, and doors
  wanting several), `PlayerPickupKey` sets `0x1000 << key` and
  `runlist_ProcessSectorTag` tests `(keys & (thousands << 12)) ==
  mask`. The converter wrote `key * 1000`, so every `DOOR_KEY3` wanted the
  Power and Time keys and every `DOOR_KEY4` the War key -- on eighteen
  levels. It writes `1 << (key - 1)` now. Every key door's key is in its
  own level (a census of DOOR_KEY1..4 against Key_Power/Time/War/Earth
  actors), so with the mask right nothing else stands in the way.
- **Tag 0 is a tag.** KILARENA's five walk-in regions and its trigger-only
  door all carry tag 0, and to Kex that links them; Build's hitag 0 gives
  every effect a private channel, which left that door shut for good. The
  converter allots tag 0 a real channel under the pseudo-tag 100000
  (`KEX_TAG0`) for the event types that fire and listen on channels --
  plain doors, keys and lifts keep 0, since a shared channel would open
  every door in the level together -- and the engine's channel row holds a
  32-bit tag for it. Standing on the first region (`VB_KEX_LOOKAT_TAG=100000
  VB_KEX_LOOKAT_STAND=1`, which traces the door on the same channel) opens
  door 185. The tag-0 lifts (KILENTRY's and TOMBEND's `FLOOR_LOWER_WAIT_RAISE`,
  the `FLOOR_RAISE_WAIT_LOWER`s already covered) get a step-on plate of
  their own in Kex mode, so they are ridden.
- **Doors cut in two by the light bands opened in halves.** The light
  gradient cuts (see Lighting) split whatever sector had a strong gradient,
  movers included: 35 plain doors and 16 key doors on tag 0 became two
  private doors each, one of which stayed shut across the doorway when the
  other was used, and a tag-0 lift's pieces moved one at a time.
  `_split_light` keeps every event sector whole now except the walk-in
  trigger regions, whose pieces are plates on the same channel. No mover is
  cut by the slope splitter either, and an event sector's walls are left
  whole by the wall-gradient splitter as well: a switch face cut into
  pieces had only the piece nearest the face centre as the switch, the
  rest inert though textured as it.
- **No switch had ever worked.** `write_build` packed every wall's hitag as
  a literal 0, so all 69 switches -- placed correctly on their walls with
  lotag 7 and the channel in the wall's hitag -- fired a private channel
  nothing listened on; whatever a switch opens in the remaster's levels
  (PEAK has eight) stayed shut. The writer writes the hitag now. Found by
  the event audits folded into `kexverify.py`, which also placed THOTH's
  switch on the wall that shows it (a long face's centre lay nearer a
  portal wall of the sector) and kept a sector's several events (six
  sectors carry two or more: THOTH's plate that is also a switch, a key
  door doubled with a plain one) instead of the last one only.
  `VB_KEX_LOOKAT_PRESS=1` with `VB_KEX_LOOKAT_TAG=<tag>` presses the first
  wall switch on that tag's channel the way the use key does, 40 ticks
  after the look-at; PEAK's switch on 89 and THOTH's on 25 are seen to
  open their doors.
- **Grates, bars and force fields were open air.** Kex does not derive
  collision from texture presence. A linked face can be textured but passable,
  untextured but Solid, or Invisible while retaining force-field identity.
  The converter now treats the retail flags directly: 0x1 sets both Build
  actor and projectile/hitscan blocking (`cstat` 1|64), regardless of texture,
  Invisible or Underwater; 0x2 blocks only AI fallers and only while the live
  floors on the two sides differ; and 0x8 blocks non-player actors. These are
  directed properties and are never copied onto the reverse face.

  A visible textured face is Build masked art (`cstat` 16 and its tile in
  `overpicnum`). An Invisible face is not drawn, but keeps `overpicnum` so the
  force-field controller can still identify it. Face flag 0x200 marks the
  Scepter force fields; `kexforce.txt` lists their tiles and
  `KexForce_Update()` opens both static and height-selected instances for a
  Scepter holder. The per-Z `kexportal.txt` records carry their own texture and
  flags, so stacked grates and force fields no longer inherit whichever face
  happened to become the Build wall's static fallback.
  `VB_KEX_LOOKAT_OVERPIC=<tile>` stands the player before a visible instance.
- **Most of SUNKEN was dry.** Water, lava and slime come from the FACE
  flags (a floor face's 0x40 is a water link, 0x20 lava, 0x80 slime), and
  only a sector with an air face above it got the engine's water-side
  marker (99) -- 74 in SUNKEN. The SECTOR record's own flag word
  (`raw[9]`) says more: 256 is a submerged volume (404 of SUNKEN's 541,
  and the deeper parts of every pool -- KARNAK's 78 all on the "sunken"
  water floors, 44 of them under a link face), 16 is water to wade
  (MARSH's 148 marsh floors). A lotag 82 sprite now marks a submerged
  sector with no surface above it (`kSectUnderwater` without a partner;
  the swim code takes `SectAbove` -1), and Build's own 94 carries the
  wading depth, hitag 10 as the DOS maps have it. SUNKEN goes from 74 to
  545 sectors under water, MARSH gets 172 to wade, and `Kex water: ...`
  reports both at level start. Sector flag 32 appears on 45 source sectors in
  seven levels. The retail editor names it **No standing on edges**, and the
  retail executable has exactly two game-side checks of sector flags `& 0x20`,
  both in actor z-range collision: a flagged neighbouring sector must not
  contribute its floor or ceiling merely because the actor radius overlaps
  its polygon while the actor origin remains outside. (The separate face flag
  `0x20` is lava.) `_flat_sprites()` emits lotag 83 on every surviving split
  descendant, producing 58 marked Build sectors. `KexNoStanding_Reset()`
  records them and `getzrange_filtered()` still traverses each neighbour but
  suppresses that neighbour's z candidates unless the actor origin is inside.
  The ordinary `getzrange()` ABI and every non-KEX map remain unchanged.
  `VB_KEX_NOEDGE_TEST=1` measured 15 suppressed edge surfaces across CAVERN's
  six marked sectors and 44 across SETARENA's seventeen; PEAK's unmarked map
  reported zero and stayed on the ordinary path.

### Shared remaster explosions

The large blast left by an Amun bomb is no longer the DOS grenade animation.
Fresh conversion writes the remaster's own `Fx_Explosion01` and
`Fx_Explosion01_Water` art as optional `KEXEXPLODE.SEQ` and
`KEXWATEREXP.SEQ`. Both actors keep their exact 21-tick lifetime. The land
version draws all 21 source frames at scale 4; the scale-2 water version keeps
its authored blank frame 0, eighteen visible frames, then blank frames 19 and
20 before destruction. Their frame-zero `explode03.wav`/`explode02.wav`
callbacks and radius-768, 20-step orange light are recreated only after the
effect actor is successfully allocated. Older converted directories continue
to use the classic animation rather than losing the effect.

`KexExplosion01_Spawn()` is shared by every native path that names these KEX
actors: Amun's land/water death at z+32, Magic 8 Ball's land/water blast,
exploding vases at z+32, Queen seed pods at z+32, snake-tail links at z+8, and
the randomized Selkis/Queen death bursts at z+192. The latter retain all three
authored centred random offsets. Only Magic 8 Ball chooses the water actor;
the exploding-vase source explicitly names the land actor even in water.
Effect spawn (including its sound) occurs before radial damage and Toggle-face
opening, matching the source callback order. Classic Exhumed never enters this
path.

The converter validates both actor definitions, every source frame, the
sound/light callbacks, blank-frame placement and all known spawn recipes before
writing output. `kexverify.py` independently checks those contracts, every
generated tile/PNG and SEQ row, both sound aliases, and native call ordering.
`VB_KEX_BOMB_TEST=1` also allocates both variants in-game and verifies their
sequence, +32 offset, scale, fullbright flag and aliases without displaying a
test window or requiring audio.

### Authored blast-open geometry

Kex face flag 0x100 is named **Toggle** in the retail editor. It is not a crack
decal or a damage sprite: when an `A_RadialBlast` call has its third argument
set, the remaster clears Solid and Toggle on an authored face, makes that face a
portal, and rebuilds the linked sectors. The room behind the wall was authored
in the retail map and becomes the same traversable space as its normal entrance.
This native compatibility path is gated by `g_kexMode`; it does not enable or
depend on the separate JavaScript crater experiment.

The retail maps contain 495 Toggle faces on 23 levels. Conversion writes their
495 logical records, 681 wall-band representations and 46 vertical sides (21
seams) to `kextoggle.txt`. Small openings embedded in a longer coplanar wall
used to disappear when the long span was treated as one edge. `_edges()` now
atomizes every collinear span at all authored endpoints, so all 495 faces retain
runtime geometry. A logical face also carries a precomputed 256-bit closure
that mirrors Kex's recursive opening rule across its owner sector, directly
linked sector, and overlapping faces reached through that sector. An impact
therefore unions bounded bitsets and updates only listed geometry; it does not
scan or rebuild the whole level and does not produce the former one-second
stall.

Blast selection follows the retail executable as well. The face target is its
bounding-box centre, horizontal distance uses the script radius, vertical
half-range is `radius * 23/40`, and both topological reachability and a sight
trace from the explosion to that centre are required. That prevents an
occluded, geographically overlapping room from opening as a second
"dimension." The shipped recipes with the opening argument set are preserved:
Magic 8 Ball, the two exploding vases and Amun's bomb use radius 512; boulders
and both fireball projectile paths use radius 192. Cobra blasts leave geometry
closed because their third argument is zero.

An ordinary one-neighbour wall opens permanently in place. A wall with several
neighbours at different heights keeps using the existing Z-selected portal
layer, with only the destroyed band enabled. Toggle floors and ceilings become
dynamic YAX room-over-room seams when opened, so crossing them changes sectors
instead of standing on the hole or being teleported vertically. Their wall
links require a unique match of both directed endpoints; matching only the
first vertex was ambiguous at eight split-sector edges and could connect an
unrelated space. Open bits, active seams and runtime YAX links are saved and
restored, while a level restart recreates the original closed map.

`kexverify.py` compares every face, closure, band, seam and dynamic portal
binding against a fresh conversion of all 25 retail maps. The runtime
`VB_KEX_TOGGLE_TEST=2` path opens every authored face through the real blast
code and validates collision, hidden reverse art, height-selected bindings,
vertical bunch membership and exact reciprocal seam edges. TOMB exercises 108
wall bands and 11 vertical seams in that exhaustive test.

### The pause menu

`menu_Menu(1)` -- the in-level menu -- gets the remaster's version too: GAME
PAUSED, RESUME, OPTIONS, EXIT TO TITLE SCREEN, QUIT, over whatever is behind it.
Its Options panel offers Audio Settings, Display Settings, HUD Settings, Game
Settings, Mouse Settings, Gamepad Settings, Bindings and Language Settings. Display
Settings provides the remaster's FOV and anisotropic-filtering preferences as immediate
native renderer controls, while clearly retaining its anti-aliasing value only as a
compatibility preference: Build does not have the remaster's KEX SMAA post-process.
Mouse Settings exposes the
remaster's independent X/Y mouse sensitivity controls through the native,
persisted input axes. HUD Settings carries the remaster's Play Time
and HUD Text toggles plus its 0.05--3.0 crosshair-size slider (a console value
of zero hides the crosshair). Play Time records only active 30 Hz gameplay,
never pauses or menus, and survives level travel, deaths, and campaign saves.
Game Settings carries the remaster's checkpoint toggle,
original-console overworld option, 0.5--10.0 view-bob speed and height sliders,
0.1--1.0 turn-sway slider, and weapon-jump-bobbing toggle. The visual sliders
are KEX-only render controls: they never enter player, map or network state.
Disabling checkpoints makes subsequent death reloads start from the level
entrance, while the overworld option swaps both the source 640x512 map and its
own source cursor/arrows. Gamepad Settings exposes the remaster's vibration
toggle and strength, right-stick Look Speed X/Y, unified left-stick Move
Sensitivity, right-stick Invert Look, and a Deadzone subpage for the left and
right sticks. These settings apply to the native axes immediately and persist
in the normal config; changing the language
reloads the localized menu, pickup and campaign text immediately and selects
the matching future dialogue/movie language. Menu rows shrink only when a
translated label needs it, so the longer French, German and Spanish actions
stay inside their authored remaster plates.

The same KEX menu screens also poll the configured controller actions rather
than only keyboard scancodes: movement/strafe navigates, Interact/Fire/Jump
selects, and Escape/Pause backs out. When a controller was the last input, the
hint bar changes its ENTER/ESC keycaps to A/B.
Fresh configurations now receive a matching SDL-standard gamepad layout: the
left and right sticks move/look, triggers jump/fire, A interacts, B backs out,
X/Back open the arsenal, Y opens the map, Start pauses, shoulders switch
weapons, and the D-pad turns/zooms. Existing controller entries in a player's
config replace only their own default slot as before.

KEX input compatibility is isolated to `-kex` sessions. Its `default.cfg` bind
vocabulary (`automap`, `+use`, weapon-wheel, map zoom, and the other remaster
commands) maps to the native input flags when a user executes or imports it.
The remaster's eight numbered weapon slots are preserved: the fourth key selects
Amun Bomb, the fifth selects the flame thrower, and 8 selects the Manacle.
Fresh KEX configurations start with Nightdive's keyboard and wheel layout,
including WASD, E, Tab, 1--8, Q/Shift, Alt/Ctrl map zoom, F5 screenshots and
wheel weapon cycling. Its `r_fov` setting drives the native field of view and
`r_rhimaxanisotropic` drives Build's OpenGL anisotropy, both from Display
Settings as well as the console; the engine has no runtime KEX SMAA control,
so `r_antialiasing` is retained as a clearly labelled compatibility preference
without claiming a different renderer setting is equivalent.
Bindings offers every remaster movement, map, screenshot, weapon-cycle and eight
numbered weapon action in five localized pages. It captures a native keyboard scan
code, rebuilds any shared-key commands safely, and persists through the normal config.
`VB_KEX_BINDINGS_TEST=1` performs that same screenshot-bind move in a hidden test
run, proves sibling commands on both keys survived, and restores its temporary binds.
Classic sessions retain their original seven-slot input packet and controls.
"Exit to title screen" is a new return code (4) that the classic menu never
produces, so the case added to the in-level dispatch is only ever reached in
Kex mode.

Kex actors also render between the old and current 30 Hz simulation poses, as
the remaster does on high-refresh displays.  The previous pose is captured
before each game tick and blended only into renderer `tsprite` copies: AI,
collision, saves and network state retain their exact tick positions.  Sprite
insertion and deletion invalidate that pose, and sector changes, teleports and
large scripted jumps draw directly at their destination rather than blending
through intervening geometry.  `VB_KEX_SPRITE_INTERP_TEST=1` proves that a
live renderer copy was blended at a fractional frame ratio without writing the
authoritative sprite. This bridge is inactive outside `-kex`.

### Known compatibility boundary

No valid retail source sector, actor, texture, used event type or shipped
per-level script is currently known to be omitted. The 25-map fresh-conversion
census found zero dropped source sectors and zero valid actors without an
emitted sprite; all 1,009 non-null events are represented (the two remaining
type-0 records are null sentinels), and all level textures resolve.

This remains a compatibility implementation on Build rather than a byte-for-byte
KEX engine port. Arbitrary mosaics, vertex lighting, multi-height wall links and
vertical portals are represented by generated tiles, geometry splits and runtime
selection, as documented above. `kexverify.py` and the focused runtime probes are
the regression boundary for those representations.
