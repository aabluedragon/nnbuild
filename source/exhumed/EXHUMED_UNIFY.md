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
| `--startnow` / `-sn` | start with whoever is present |
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
# dedicated server, 2 slots, starts as soon as both are in
./vibebuild32 -gm exhumed -g <STUFF.DAT> -nosetup -name SERVER \
    -dedicated -startnow -net /p23513 /n0:2

# client
./vibebuild32 -gm exhumed -g <STUFF.DAT> -nosetup -name PLAYER \
    -wnd -res640x480 -net /p23514 /n0 127.0.0.1:23513
```

Confirm with `VB_EXNET_DEBUG=120` on both that the tick numbers advance and the
CRCs at equal ticks are identical, and that neither log contains `Out Of Sync`.

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

- **Late join and rejoin.** A player has to be present when the level starts.
  Duke's late-join path streams a savegame snapshot to the joiner and integrates
  it at a frozen tick; Exhumed's savegame code is not wired to the net path, so
  that is still to do.
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

`source/exhumed/tools/kexmap.py` does that conversion:

```
source/exhumed/tools/kexmap.py "/path/to/PowerSlave Exhumed" ~/games/powerslave

./vibebuild32 -gm exhumed -j ~/games/powerslave \
    -kex "/path/to/PowerSlave Exhumed" -map THOTH.MAP
```

It writes one Build `.map` per level plus an `exhumed.def`. `-kex <dir>` mounts
the install's `PowerslaveEX.kpf`, which is a plain zip the Build engine reads
natively, so the levels are drawn with the remaster's **own** PNG textures --
nothing is copied out and no palette reduction happens. You still need the Build
PowerSlave data (`STUFF.DAT`) for the HUD, weapons and sounds: this brings the
remaster's levels, not a whole game.

All 25 retail levels convert, keeping 97.7% of their sectors.

### The map format

Nightdive ships `.dmap` text sources alongside compiled `.map` files for the
sample levels in `docs/examples/`, which is what made the binary format
tractable -- every field below was checked against the matching text source.
The retail levels ship compiled only.

```
header   'PSM\x01' + 8 x u32: ntex, nvert, nsector, nface, ntile,
                               nzone, nevent, nactor
strings  u16-length-prefixed: sky texture, then the texture table
verts    nvert x 16: float x, y, z + RGBA (baked vertex lighting)
sectors  nsector x 20: u16 faceStart, u16 wallFaceEnd, ...
         faces[faceStart..wallFaceEnd] are the walls; wallFaceEnd+1 is the
         ceiling face and wallFaceEnd+2 the floor face
faces    nface x 32: i16 tileStart, i16 tileEnd (-1 = untextured),
         u16 vertexStart (4 corners), i16 sectorLink (-1 = solid),
         float angle, float nx/ny/nz, u16 flags, i16 pad,
         i16 tileVertStart, i16 tileVertEnd
tiles    ntile x 16: u8[4], u16[4] vertex indices, u16 textureIndex, u16 pad
actors   nactor x 20, at the very end: u16 type, u16 ?, i16 x, i16 y, i16 z,
         i16 tag, u16 param1, u16 param2, u16 layer, u16 ?
```

Two things are worth knowing before touching the reader:

- **The string table's length is ambiguous.** An empty sky and the empty
  "no texture" slot are indistinguishable, so the entry count is `ntex+1` for
  some maps and `ntex+2` for others. Rather than guess, the reader locates the
  vertex array by *validating* the face table that follows it: face tile ranges
  must partition `0..ntile-1` in order and every link must name a real sector.
  That is a strong enough check to pin the offset, and it parses all 39 maps
  Nightdive ships.
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
per texel, pinning the constant at ~143, so 6 units per texel gives 24.

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

The rest cannot be represented at all, and a mis-tilted floor is worse than a
flat one, so every slope is checked against Build's own formula at the face's
four corners and dropped if any lands more than 8 world units out. On TOMB that
keeps 52 of 92 with a worst-case error of 6.3 world units; before the check the
worst was 803.

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

Kex has no sky tile:**Sky.** Build will not treat a tile as a sky panorama until `tileSetupSky()`
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

- A sector whose walls are not a single closed ring is dropped rather than
  emitted broken (226 of 9837 across the retail maps).
- **Light that varies across one surface.** Build has exactly one shade per wall
  and one per floor, and Kex bakes its lighting per vertex, several vertices to
  a wall. The falloff around a fire is mostly *vertical*: on the wall behind
  TOMB's first fire pot the baked luminance runs 39 at the top to 255 at the
  bottom of a single 384-unit face. A Build wall cannot gradient at all, so the
  pool of light the remaster paints around a flame comes out as one averaged
  shade for the whole wall. Walls near a fire do end up brighter than walls
  away from it -- the ambient step is there -- but the soft edge is not.
- **Sloped floors and ceilings** are converted, but dropped per sector when the
  fit is worse than 8 world units at any corner.

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

The converter now groups the links into bunches with a union-find over sector
sides -- so several sectors meeting on one plane share a bunch rather than
getting one bunch per pair -- and links the seam walls by coordinate. 291
bunches across 16 maps, well inside the 256-per-map limit. Exhumed also needed
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

### Distance shading

The remaster does no distance shading at all -- everything it paints is baked
into the vertex colours, which the converter turns into per-surface shades.
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

Build will not treat a tile as a sky panorama until `tileSetupSky()` has seen
it, which is what `multipsky` does -- without it a parallax ceiling draws black.

### Related fixes

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

Kex keeps a sector's trigger in a separate event table rather than on the sector.
Its size fell out of arithmetic: the gap between the tile array and the actors is
exactly `nzone*32 + nevent*8` across all fifteen sample maps, so zones are 32
bytes and events 8. The record is

```
u16 eventType   u16 sector   i16 eventTag   i16 params
```

verified against the `.dmap` sources on every event in test_door, test_switch
and test_lifts.

The useful part is what `eventType` turned out to be: **the same numbering Build
PowerSlave uses for its own sector effects.** Kex type 1 is Exhumed's "Ceiling
Doom door", 7 is "Touchplate floor lower, multiple", 21 is "Touchplate", 23 is
"Floor raise, Synchronize", 24 is "Ceiling door, channel trigger only". The
console and DOS versions descend from one design and kept the tag numbers.

So no new game code was needed. An event becomes `sector.lotag` (the effect) and
`sector.hitag` (the channel), and `runlist_ProcessSectorTag()` builds the
elevator and its press-trigger exactly as it does for the original levels. TOMB
gets 12 tagged sectors, KARNAK 34.

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

which at 320 units is 30 / 25 / 105 / 105 / 25 / 30. Measured off a 1970px-wide
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
wording rather than being given a caption that is merely plausible.

### The overworld

The remaster picks levels off a hand-painted map rather than from a list: each
level sits at a point on it, joined to its neighbours by compass directions, and
the arrow keys walk that graph. New Game goes there after the save slot. The
marker is the Eye of Ra, spinning through the remaster's own 31-frame cycle --
the sprite sheet, the index-to-rectangle table and the frame order are all
parsed out of `sprites/misc/overworld_hd_*`, so it spins the way Kex spins it.

None of the campaign is hardcoded. `kexmenu.py` reads the positions and the
`nextmap_north/east/south/west` links out of the remaster's own
`defs/mapInfo.txt` and resolves the `$str_NNN` level names against its
localization table, writing the lot to `kexworld.txt` for the game to load.

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

### The pause menu

`menu_Menu(1)` -- the in-level menu -- gets the remaster's version too: GAME
PAUSED, RESUME, OPTIONS, EXIT TO TITLE SCREEN, QUIT, over whatever is behind it.
"Exit to title screen" is a new return code (4) that the classic menu never
produces, so the case added to the in-level dispatch is only ever reached in
Kex mode.
