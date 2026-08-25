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
source/exhumed/tools/kexmap.py "/path/to/PowerSlave Exhumed" ~/games/powerslave \
    [/path/to/powerslave_demo]   # with STUFF.DAT: rewrites ARROW.SEQ and SET.SEQ too

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

- A sector whose walls do not make a single closed ring. That used to be 226
  of 9837 across the retail maps and is now none of 11329: the ring walk
  prunes what hangs off the ring -- a chain to nowhere, or a chord across the
  room (a partition wall, a doubled face) meeting it at over-visited points,
  shortest first and only while the graph stays in one piece -- and rejects
  a ring that crosses itself rather than emit it. The chord is a free-standing
  wall Build could not hold anyway; the room it was hiding is kept. The last
  two were float noise, corners at -3456.01 beside -3456.0, which snapping the
  ring's points to a tenth of a unit closes.
- **Light that varies across one surface.** Build has exactly one shade per wall
  and one per floor, and Kex bakes its lighting per vertex, several vertices to
  a wall. The falloff around a fire is mostly *vertical*: on the wall behind
  TOMB's first fire pot the baked luminance runs 39 at the top to 255 at the
  bottom of a single 384-unit face. A Build wall cannot gradient at all, so the
  pool of light the remaster paints around a flame comes out as one averaged
  shade for the whole wall. Walls near a fire do end up brighter than walls
  away from it -- the ambient step is there -- but the soft edge is not.
- **Sloped floors and ceilings** are converted (contour-split where Build's
  one-axis slope cannot follow), and dropped per sector only when the fit
  is worse than 8 world units at any corner: 6 of the 2437 sloped sectors
  across the retail maps come out flat.

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
into a grid so each edge only tests the ones near it. Portal retention went from
90.4% to 93.0%, recovering 1044 passages, for 8.3% more walls (the largest map
is 3160 walls against Build's 16384).

What is left is 5.7%: edges that face several *different* neighbours stacked
vertically -- a shaft in TOMB opens onto one sector at z 2048 and another at
z -256 through the same wall. A Build wall carries one `nextsector`, so only the
tallest survives and the other opening reads as solid. Fixing those means
splitting the sector itself into vertical bands and joining the bands with TROR
bunches, which is a much larger piece of work than the seam mirroring above.

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
  / 16384 now, the save version is 10000 for the bigger per-sector tables
  it writes, and a level past the version-7 loader's limits is written
  with map version 8 (same layout on disk; `engineLoadBoard` keys the
  limits on the version). No level is at the budget. `VB_KEXSAVE_TEST=1`
  saves at tick 100 and loads the save back at 160, comparing the player's
  sector and health -- MAGMA's 1060 sectors round-trip. A sloped sector is cut along ITS contour, not the
  light's (a half with no level-line wall could not be tilted at all), its
  pieces pinned to the seam, and the cut is kept only if both halves still
  tilt both surfaces within `KEX_SLOPE_TOLERANCE`; a sector on either end
  of a vertical link stays whole, because build_tror() pairs seam rings
  edge for edge.
- `_split_walls_light()` subdivides a lit wall edge whose field changes
  `KEX_LIGHT_WALL_STEPS` (8) or more along its length into up to
  `KEX_LIGHT_WALL_PIECES` (3) pieces, and every wall -- split or not --
  takes its shade at its own midpoint rather than the face average.

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
fix) and checks that every portal is paired.

**Not carried: a wall's vertical gradient.** 40% of walls are lit
differently top and bottom by six steps or more, and Build has no way to
say so short of stacking sectors (TROR) at every band. The wall's shade is
the field at mid-height.

### Breakable props

The remaster's vases and barrels are shootable actors, and none of that
survived conversion -- they came out as decor sprites that stand there and
ignore bullets. The actor def carries `health 30`, `shootable 1`, `solid 1` and
a `deathAnim`, and the anim is the whole recipe:

    vase01_death { frame {
        action A_PlayLocalSound("sounds/glassbreak.wav")
        action A_Spawn("Fx_VaseExplodePuff", 0, 0, 32)
        action A_RandomVaseItem()
        action A_TossActor("Obj_Vase01_Shard01", 0, 0, 32, 8, 8, 4, 16)
        ... one per shard ...
        action A_Destroy() } }

The shard art is already in the prop's own idle anim: frame 0 is the intact pot
and the frames after it are the shards. That is also why holding the prop on
frame 0 (`noAdvanceFrames`) is what stopped it flickering into a sliver of
debris -- the "random sprite" it flickered to was a shard.

Build has the pieces this needs. A prop registers a runlist run the way
`BuildObject()` does, so `runlist_DamageEnemy()` -- which looks the run up
through `sprite.owner` -- routes a 0x80000 damage message straight to it, and
the shards are `BuildCreatureChunk()`, the same physics chunks the exploding
fire cauldron throws. `clipdist` comes from the actor's own radius rather than
Build's default, so the prop blocks the circle the remaster gives it. 12 prop
types, 23 of them standing in TOMB alone.

`A_RandomVaseItem` is transcribed rather than guessed: the remaster ships its
AngelScript in the .kpf, so `scripts/p_misc.txt` gives the real table --
`r = Max(8)`, with 0 spawning a scorpion, 3 the ammo pickup, 5 the health
pickup, and the rest falling through or dropping nothing. The branches gated on
the scepter, sandals or shawl cannot apply, because Exhumed has no such
artifacts; that is the same path the remaster takes before the player owns any
of them, so the fall-throughs land where they do there. The rare Item_Map and
Item_Power rolls are left out for the same reason, and the dust puff and
`glassbreak.wav` become PowerSlave's own pot sound.

Types 49 and 50 are the EXPLODING pots -- `defs/defines.txt` names them
`Obj_Vase_Explode01/02`, where the old console enum in scripts/actorTypes.txt
had FLAMEPOT/SNAKEPOT, and trusting the old enum had turned all 232 of them
into fuel-canister and cobra-ammo pickups. They hold a random still frame
(`randomizeFrames`), keep their shard art in a separate `*_shards` anim, and
their death anim carries `A_RadialBlast(512, 140)` -- so kexbreak.txt grew two
blast columns, and `KexBreak_Shatter()` reuses the fire cauldron's recipe
(flash, the floor explosion anim whose SEQ carries the boom, then
`runlist_RadialDamageEnemy`; radius 512 Kex units x WORLD / 256 = 12 against
the cauldron's 20). The blast sets off neighbouring pots, as it does there.

Two more corrections from the same defines pass: the vase drop table's case 0
spawned `BuildScorp` -- the 20000-HP Selkis boss -- where the remaster spawns
a 20-HP scorpion critter; Build has no small scorpion, so the spider stands in.
And the remaster's "Mantis" is Build's lava dude wearing a new name (its
rise/attack/death frames are pixel-identical to the LAVAG tiles, and its def
says noLavaDamage/noExitLava/hidden); it was mapped to 104, the Kilmaat roach,
whose art the demo largely lacks. The camel is the remaster's level exit
(lotag 60) and the checkpoint scarab its save point (lotag 59); both were
standing around as decor.

`VB_KEXBREAK_TEST=<ticks>` shatters every prop in the level that many ticks in,
which is how the shard and drop paths were exercised without a player: 23 props,
6 of 6 shards each, no crashes.

### Light sources

The remaster groups 19 actor types as `group "Light Sources"` -- its fire pots,
wall torches, candles and the water lamp. None of them declares a `lightRadius`
or `lightIntensity` (only four projectiles in the whole game do), because in Kex
their glow is baked into the vertex colours: on the wall behind TOMB's first
fire pot the baked luminance runs 39 at the top of one face to 255 at the
bottom. Build carries a single shade per wall and cannot hold a gradient across
one, so the glow goes back in as a real light instead of coming across as part
of the geometry.

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

The fix keeps the SEQ files exactly as shipped -- their sub-seq order, trigger
flags and sound events are an engine-side contract -- and fills the missing
tiles on their ORIGINAL numbers through exhumed.def. kexseqmap_gen.py derives
the mapping from the demo's own SEQ data plus the sub-seq -> remaster-anim
tables in KEXSEQ_PLAN.md: for every frame of every mapped sub-seq, each
zero-size chunk tile takes the remaster frame playing at that moment, first
occurrence winning (held poses share tiles, which is the stock economy too).
Views 5-7 need no art at all: the stock data mirrors them from views 3-1 with
chunk x-flips, sharing tile numbers -- the same convention the remaster's
rotation blocks use, so Build direction k maps to kex rotation k throughout.
The result is committed as kexseqtiles.py (292 tiles); build_seq_tiles() in
kexmap.py composites them from the sprite sheets at conversion time.
parse_anim_full() reads the anim scripts with their eight rotation blocks
kept apart, where the decor path had flattened every view into one image.

Two measurements made this simple. Chunk positions in a SEQ are TILE-CENTRE
coordinates, so a replacement of similar proportions lands where the original
art did without touching offsets. And the remaster's monster sheets are the
ORIGINAL art resolution: the sentry's surviving attack tile (3226, 97x113)
matches its kex composite 1.000:1, and the Selkis muzzle matches its sheet
slice -- an actor's kex `scale` is a renderer multiplier over there, already
encoded here in each creature's Build repeat values, so nothing is rescaled.

Verified in game: the wasp flies and attacks in MARSH, Selkis advances in
SELBUROW, the sentry leaps in COLONY_PSX -- all in the remaster's art, walking
the stock state machines with the stock trigger frames. The four sound names
those SEQs use that neither the demo nor an alias covered (REX-BITE, REX_GRGL,
REX-ICU, GHOST109) now alias to the remaster's nearest recordings.

The fuller road not taken -- compiling entirely new SEQ files from the
remaster's anims -- is specified to the byte in KEXSEQ_PLAN.md with a
round-trip-proven writer (kexseqtool.py), for the day the frame timings
themselves should follow the remaster.

### The crows

The 99 `AI_Bird` actors (seven levels; PASS and PEAK have 26 and 21) flew as
wasps, and kept doing so after the creature-art pass because Build has no
bird AI and the wasp's SEQ tiles are the wasp's. They still fly as wasps --
the AI is the wasp's -- but they look like the remaster's crows. HAWK.SEQ is
a file the engine loads (`kSeqHawk`) and nothing references, so
kexseqmap_gen.py makes it a bird-skinned copy of the demo's WASP.SEQ: the
same 31 sub-sequences, frames, chunk offsets, flags and sound events, byte
for byte the same size, with each of the 29 wasp tiles retargeted to a bird
tile on 7600+ (`kexbirdseq.py`, written out by the converter) that the tile
pass composites from `sprites/monsters/bird.png` -- wings up and down from
`bird_fly_chase`, the sting frames from `bird_attack`, the splat from
`bird_death`. The sound table names `BIRD_ACT/ATK/DIE/WNG`, which
`KEX_SOUNDS` aliases to the remaster's bird wavs. The converter marks a crow
with hitag 1 and `BuildWasp()` animates it from `kSeqHawk` at repeat 40
instead of 50, the bird sheet being at actor scale 1.0 to the wasp's 1.25.
The flag lives beside `WaspList[]`, not in it, so the saved struct is
untouched.

### The scorpions

A census of every actor in the 25 retail maps (types against
`ACTOR_MAP`, the traps, the inventory and the decor fallback) found two
gameplay actors that were coming out as decor: `AI_Scorp`, 60 of them
across eight levels, and `AI_Magic8Ball`, 66 in two. The remaster's
scorpion is `AI_Spider` to the letter -- the same health (20), speed,
radius and step height, anims of the same shape (12-frame walk, 13-frame
attack, one-frame leap that even calls `A_SpiderLeap`, three-frame death)
-- in a scorpion's shell, and Build has no such creature (its SCORP.SEQ is
Selkis). So it gets the crows' treatment: `ACTOR_MAP[14] = (105, 1)`,
spider with hitag 1, and `BuildSpider(..., bScorp)` animates it from
`kSeqScorpion`. SCORPION.SEQ (`kexscorpseq.py`, written by
kexseqmap_gen.py) is the demo's SPIDER.SEQ with every tile retargeted to a
scorpion tile on 7700+ that the tile pass composites from
`monsters/scorp_*`; unlike the other creatures, which only fill the tiles
the demo lacks, all 53 of the spider's tiles are mapped, because the demo
has the whole spider and none of it is a scorpion. The frame map is in
`SCORP_GROUPS`: walk 6 for 6, idle on walk frame 0, the 13-frame attack on
Build's 7 by even frames, the leap pose for both the leap and the airborne
sub-sequences, the death's three frames stretched over Build's 25. The
sounds are the spider's, which is what the scorpion's def names too.
`seq_IsLoaded()` lets a scorpion fall back to the spider's look if the
file is missing; `VB_KEX_LOOKAT_TILE=7700-7752` with `VB_KEX_LOOKAT=99`
picks one out to look at (the creatures share statnum 99), and the look-at
hook now keeps facing its target for a while, since creatures move.

### The mines

The other actor the census found as decor, `AI_Magic8Ball`: 62 of them in
SUNKEN and four in MAGMA, a black sphere the remaster's anims describe
completely -- the idle polls `A_CanSeeTarget(detonate, 256)`, the detonate
plays `mineact.wav` and starts a countdown, and `magic8ball_explode` is
`Fx_Explosion01` plus `A_RadialBlast(512, 140)`; under water,
`magic8ball_explode_water` also drops a random item. Its death anim is the
countdown too (health 60, shootable). It rides the breakables: kexmap.py
emits it as a one-tile breakable with the explode anim's blast and two
new `kexbreak.txt` columns, the sight radius in Build units (256 * WORLD)
and the fuse in ticks (`KEX_MINE_SIGHT`, `KEX_MINE_FUSE` = 45, the
remaster timing its loop in code). `KexBreak_MineTick()` arms it when a
living player it can `cansee` is within sight, blinks it while it counts,
drops the random item if its sector is under water, and blasts through the
exploding pots' `KexBreak_Shatter` -- under water without the fire, as a
burst of the game's own bubbles, the remaster's
`magic8ball_explode_water`; being shot or blasted to zero starts the fuse
instead of shattering. `VB_KEX_LOOKAT=0 VB_KEX_LOOKAT_TILE=<its
tile>` (8761 in the current tables) with `VB_KEXBREAK_TEST=100000` logs
the arming and the blast.

A sound audit alongside (every `sounds/*.wav` the remaster's defs and
anims name, against `KEX_SOUNDS`): 107 of the demo's 170 VOCs are
aliased and 82 of the 118 remaster wavs referenced are reached. The one
that mattered was the breakables': every vase anim plays
`glassbreak.wav` and the barrel's `woodbreak.wav`, while `pot_pc1`/`2`
were aliased to the thrown vessels' `glass01`/`02`; they are the break
sounds now, and a `wood` column in kexbreak.txt sends barrels to the
second. The remaining ones have no Build sound at all, so they get names
no VOC carries -- `k_trans`, `k_mine`, `k_bould`, `k_bldhit`, `k_anmele`,
`k_setstp` in `KEX_SOUNDS` -- and `KexSound_Get(name)` plays them:
`LoadSound()` resolves any name through kexsound.txt, so a Kex-only name
loads the wav straight into a free one of the 200 sound slots (the roster
is 80), and -1 plays nothing outside Kex mode. They sound on a transmitter
piece taken, the mine arming, the boulder trap's throw and the boulder's
impact (`KexBullet_ImpactSound`, on bullets whose SEQ is the boulder's),
and the Anubis' and Set's melee hits. Verified muted only -- the aliases
resolve and nothing else changed.

### Ramses

The remaster's Ramses ships complete in the .kpf: the speeches as
`ramses/SP_<LANG><NN>.wav` in four languages, and the lip-sync as AngelScript
const-int arrays in `scripts/ramses/SP_*.txt` -- one 4-bit head state per
1/30 s of audio, bit 3 the mouth. The spirit-head machinery in ramses.cpp (the
swirl of golden pixels, the materialise, the eyes, the mouth tiles) is the
same one the DOS game uses and works untouched; all a speech IS to it is a
list of totalclock ticks at which the mouth toggles -- the .PUP format -- so
`Kex_RamsesStart()` derives those ticks from the bit-3 edges of the script
array (one entry = 4 ticks of the 120 Hz clock) and plays the wav through the
music channel, which is what `CDplaying()` watches anyway. Which speech: the
hub script chooses by artifacts collected, and `Kex_RamsesSpeech()` is that
function (`GetRamsesDialog()` in scripts/map/TOMB_script.txt) transcribed:
feather 11, scepter 10, anklets 8 or 9 with the cobra staff, shawl 6 or 7 with
the flame thrower, mask 4 or 5 with the bombs, sandals 3, nothing 1 -- and
coming back after he has spoken once gets the "alternate" 12 or 2. TOMBEND
gets 14 with the transmitter assembled and 13 without. `kex_language` (0..3)
picks the language; Italian falls back to English in the remaster itself.

The converter's side: the pads in TOMB and TOMBEND are the game's only
SCRIPT_REPEATABLE (63) sectors, so KEX_RAMSES turns them into sector effect 80
-- the 0x8000 flag the player-touch trigger tests -- and plants the lotag-61
head sprite. Three placement lessons, all learned the visible way: the
scripted spawn point sits BEHIND the casket (the remaster slides the casket
open with a scripted sector move, carried over below), a face sprite hugging
the near plane never draws at all (LEV1 stands its own head ~200 units out,
translucent and z-centred -- cstat 0x82 -- and now so does this one), and a
face sprite's z is its bottom, not its centre.

Where the DOS game ends the level when Ramses finishes -- his speech IS its
intro cinematic -- `Kex_RamsesEnd()` instead puts back everything
`InitSpiritHead()` took: the sprites it hid (minus the ones that were already
hidden, which are remembered now), the player's freeze, the one-shot pad flag,
and the level's own music. One latent engine bug fell out: `InitSpiritHead()`
reads the gold head tile's pixels without ever loading it, and every DOS path
happened to have it cached; the kex path did not, and crashed.

Verified end to end on TOMB: trigger, materialise, 116 seconds of SP_ENG01
with the mouth moving, release with play resumed and no level advance.

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

Kex keeps a sector's trigger in a separate event table rather than on the sector.
Its size fell out of arithmetic: the gap between the tile array and the actors is
exactly `nzone*32 + nevent*8` across all fifteen sample maps, so zones are 32
bytes and events 8. The record is

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
(with the key in `hitag/1000`), triggers to touchplate 21, lifts to 9/12,
floors to 6/8, bomb walls to 35 -- each guarded on the neighbour the Build
mover walks to at load, since `runlist_ProcessSectorTag()` asserts it finds
one. A switch becomes a *wall* tag: Build switches are wall lotag 7 flipping
`picnum` to `picnum+1`, so `_place_switch()` finds the wall face carrying the
switchInfo texture and the tile assigner pins every switch's ON texture to the
tile after its OFF, the way animation frames follow their base.

Water came out of the same pass. Kex links an air sector's floor face (flags
bit 6) to the underwater sector beneath it; Build says the same with lotag
98/99 sprite pairs (`SetBelow`/`SetAbove` + `SnapSectors`). The 256 links
across the retail maps were previously read as solid floor -- nowhere in the
game could you dive. Floors flagged lava (0x20) or slime (0x80) get Build's
lotag 96 damage sprite.

The FLOATER variants turned out not to be movers at all. A sector cannot move
sideways in Kex any more than in Build, so `FLOATER_128_ANG_60` is a floor
bobbing on a sine, the `ANG` suffix its starting phase and the number its
excursion. Build PowerSlave already has that: a lotag 95 sprite makes the
sector's floor bob at fixed amplitude with a 16-step phase in the hitag. Each
FLOATER event becomes one such sprite, `KEX_FLOATER_PHASE` mapping the
angle-suffixed names to the phase (60 deg -> 3, 120 -> 5, 180 -> 8, and the
negatives the other way round), the `ANG_USER` variants taking the phase from
the event parameter and `ANG_RAND` from the sprite index. MAGMA's six lava
rafts and the three in test_platforms bob out of step with one another again.

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
in between, as in the remaster. ARTIFACT_SPAWN is KILENTRY's six shrines,
where an artifact appears for a player who arrived without it: the converter
plants the pickup with a mark on its hitag and `KexInv_LevelStart()` removes
it for a player who already owns that artifact. The ARTIFACT_TRIGGER pad on
the same tag is a lotag 68 sprite: the pickup is taken off the map at load
and put back -- with a flash and the teleport sound -- when a player steps on
the pad, which is the remaster's reveal.

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

Still left untagged deliberately: nothing in the event table. What remains
of the scripts is the per-level AngelScript that is not sector-triggered
(camera work, the Ramses animation itself), which is transcribed where it
matters and skipped where it is presentation.

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
- **Not drawn**: `gfx/wings.png`, which the remaster draws in place of the
  scarab ornaments only when the screen is wider than 4:3 (its canvas is
  240 tall and `aspect * 240` wide, the bar centred with an offset of
  `(width - 320) / 2`; ours is always the 320-wide layout).

### The traps

Four actor types make up the remaster's traps, and none of them was being
converted: `Special_FireballSpawner` (196 across the retail maps -- GORGE and
SELPATH alone have 20 and 19), `Special_LaserSpawner` (20, COLONY),
`Special_BoulderSpawner` (14) and `Special_LaserBeamSpawner` (127, the
Kilmaat force fields in CHAOS, COLONY_SAT, SHRINE, SETPALAC and THOTH).
Whole rooms were missing the thing that made them dangerous.

The first three are one Kex class, `kexSlaveFireballSpawner`: a spawner on a
wall that fires into the room while its tag's channel is active. Build
PowerSlave's fireball trap is the same machine -- sprite lotag 71,
`BuildFireBall()`, `FuncTrap()` fires a bullet along the sprite's angle every
64 ticks while its channel is on -- so the fireball spawner becomes that.
The boulder spawner becomes the same trap throwing a new bullet type, 17:
the trap fireball's twin in speed, poof and radius, with the remaster's
damage (35) and its boulder sprite on BOULDER.SEQ, which is FIRETRAP.SEQ's
eight views on the one round tile. BOULDER.SEQ is a name the classic data
does not carry, so the sequence loader treats it as optional -- absent, it
loads nothing and says nothing. The boulder flies where the remaster's
rolls; that much is Build's projectile model. The laser spawner takes Build's
other trap, the arrow (lotag 77, bullet 15): a fast straight shot, which is
what a laser bolt is. Its ARROW.SEQ is rewritten onto the remaster's beam
bolt -- the side view stretched into a streak the way the remaster's
"stretchy" Proj_Beam is -- when the converter is given the demo data
(`kexmap.py <kex dir> <out dir> <demo dir>`, `patch_demo_seqs()`), and the
trap's `tr_arrow` sound is aliased to the sentry's laser. The same pass
moves Set's melee hit (the 0x80 frame flag in SET.SEQ) from the DOS data's
frames 6 and 15 of the swing to the remaster's 8 and 17, which is where
`set_melee` connects. The direction was the thing to
get right: the actor's own angle is 0 on most of them. The record's `p2` is
the wall FACE the spawner sits on (the actor stands one step off that face's
first vertex, in the face's sector), and the face normal points into the room
-- stepping along it stays in the sector, stepping against it leaves -- so
`_face_angle()` turns that normal into the Build angle. The tag is the
channel, as the sector events' tags are. Build keys a wall-texture flip off
the same hitag (the trap's "mouth" lighting up), which in a converted map
would catch a switch wall on the same channel in the same room, so in Kex
mode the trap is built with -1 there. `kMaxTraps` went from 40 to 64: COLONY
carries 30 spawners.

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
growing that record was not an option.

What they do, in Kex mode only:

- Sandals of Ikumptet: the jump is a quarter higher (-4480 against -3584).
- Sobek Mask: `Arti_Mask` was already Build's mask pickup (lotag 23), and
  stays that; the artifact bit is set beside it and the underwater breathing
  check treats it as a mask that never runs out.
- Shawl of Isis / Feather of Horus: jump held while falling clamps the fall to
  a float (1024) or a hover (128).
- Protective Anklets: lava and slime damage halved, where move.cpp applies it.
- Kilmaat Scepter: walks through the laser beams (see The traps).
- Transmitter: the ending. `FinishLevel()` on TOMBEND plays `goodend-*.ogv`
  with all eight pieces and `badend-*.ogv` otherwise, which is
  `EndRamsesEvent()`'s `GetQuestItems() != 0xFF` test in the TOMBEND script.
- Ankhs: each raises the health ceiling by a fifth of the base, so all five
  double it -- `KexInv_MaxHealth()` is what the respawn, the healing caps,
  the venom bowl and the HUD bar consult in Kex mode -- and fills the player
  up to it, with the remaster's "Maximum Health Increased!".
- Dolls: the tally, and the doll names as pickup captions (the pickupMessage
  of each doll's actor def -- the Lobotomy team, and Nightdive's).

The status screen is the console game's, on the Inventory key (Enter by
default; the converted maps have no usable items for that key to do anything
else with). Four pages -- Map, Arsenal, Artifacts, Transmitter, the remaster's
`str_044..047` -- walked with left and right, up and down picking an item so
its description shows in the plate at the bottom. Every icon is the
remaster's: `gfx/menu/menuweapon_*`, `menuartifact_*`, `menukey_*` (dim and
lit), `menutransmitter_*` and the lit screen `_on`, `menuteamdoll`,
`menumap_open/closed`, the `bar_left/right` plates under the title and
`wheelslot` behind each weapon. kexmenu.py slices them into tiles 7400..7447
and writes every string the screen shows to `kexinv.txt` (`KexInv_Text()`),
with the `\n` and `${0}` of the localization table left for the engine to
expand. The transmitter is drawn assembled, the pieces the player lacks as
dark silhouettes; the eight pieces' positions in the device are laid out here
by eye (`s_kexTransPos`), the remaster keeps that layout in code, not data.
The weapon order is the console game's, which swaps Build's flamer and
grenade; the eighth slot is the Manacle (below).

Two things the converter leaves out on purpose. KILENTRY carries a complete
set of the six artifacts stacked on one point outside the map with a tag on
them -- a script-spawned hand-out for players who arrive without one, not a
room to walk into -- and placing that set would be a cheat. And the map tile
base moved from 6000 to 8000: with the collectibles' sprites added the
texture run from 6000 reached 6988, one level short of the menu art at 7000.

Every unattended run takes `VB_HIDE_WINDOW=1` (the window is created hidden
and never raised; rendering and screenshots go on offscreen) and `-nosound`,
which now silences the music as well as the effects. The unattended check is
`VB_KEXINV_TEST=<ticks>`: that far into a level it
takes every collectible the level holds as if walked over, logs the tally,
then pages through the status screen screenshotting each page.

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
spread 24 angle units apart and shakes the view. The bolt is bullet type 18
(100 damage, the beam bolt's `kSeqArrow` art, so it looks like the laser
traps' shot), so it uses the trap art already carried.

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

### The boss levels' scripts

The two level scripts not transcribed before, SELBUROW's and SETARENA's,
are the same script: `$script 0` at load checks whether the player already
holds the level's artifact -- Selkis' scepter, Set's shawl -- and if so
removes the boss (map actor 41 / 67, Selkis and Set themselves) and fires
tag 1010, the exit; otherwise `$script 1` polls every two seconds and fires
1010 once the boss actor is gone. Nothing was firing 1010 here, so neither
level could be finished. `s_bossRule` in kexbreak.cpp keys the boss on its
statnum (scorp.cpp 122, set.cpp 120): its death, seen by the generic hook
`runlist_DamageEnemy` already has, fires the tag; at load with the
artifact held the boss is killed the way `KillCreatures()` kills, by
damage, and the tag fires either way. `VB_KEXBOSS_TEST=1` kills the boss
at tick 60, `=2` grants the artifact and runs the load rule instead; both
log `Kex script: ... tag 1010 fired`.

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
  The pad Kex authors at the BOTTOM, level with the lake it sits in, and
  the event's param names a reference sector whose floor is the height it
  stands raised to until stepped on (CAVERN_PSX's 196 names 197); Build's 6
  does that dance but wants a higher NEIGHBOUR for the top. A lotag 81
  sprite carries the top's z, `runlist_ProcessSectorTag` builds the pad
  from it, and the converter lifts the pad's ceiling to the lake's when the
  top would pass it. With `VB_KEX_LOOKAT_TAG=17` on CAVERN_PSX the pad is
  seen standing at its top from load, and with `VB_KEX_LOOKAT_STAND=1` --
  the player put on the tagged sector instead of beside it -- sinking
  under the player's feet.
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
  reports both at level start. Flag 32 (45 sectors in seven levels) is
  still unread.

### The pause menu

`menu_Menu(1)` -- the in-level menu -- gets the remaster's version too: GAME
PAUSED, RESUME, OPTIONS, EXIT TO TITLE SCREEN, QUIT, over whatever is behind it.
"Exit to title screen" is a new return code (4) that the classic menu never
produces, so the case added to the in-level dispatch is only ever reached in
Kex mode.

### What is still not carried

Everything above is in; this is the list of what is not, so nobody has to
re-derive it from the code.

- **KILENTRY's stacked artifact set**, six actors on one point off the map
  with a tag: the script-side hand-out behind the shrines, and a cheat if
  placed.
- **Per-level AngelScript that is presentation**: camera moves, Ramses'
  materialise animation timings, the KILARENA intro. The sector-triggered
  parts are all transcribed (see the events and scripts sections).
- **The widescreen `wings.png`** (see the status bar section); the rest of
  the HUD is in.
- **Vertical light gradients on walls** (see Lighting).
