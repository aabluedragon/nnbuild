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
sectors, wall/floor/ceiling textures, the player start.

Not converted:

- **Actors.** Enemies, pickups, doors, switches and triggers are all read (type,
  position, tag, params) but nothing is emitted yet, so the levels are walkable
  and empty. This is the biggest remaining piece.
- **Room-over-room.** Kex lets one wall link several sectors; plain Build cannot,
  so only the tallest portal on an edge survives, and a sector whose walls are
  not a single closed ring is dropped rather than emitted broken (226 of 9837
  sectors across the retail maps).
- **Sloped floors and ceilings**, which come out flat.
- **Baked vertex lighting**, which the format carries per vertex and Build has
  no place for.

### Related fixes

`-map` never worked in this port: it compared the whole argument token against
`"map"` and then read the filename from `pChar+4`, one past the terminating NUL
of a token that had to be exactly `-map`. No spelling of the flag could name a
file. It now accepts `-map <file>` and `-map<file>`.
