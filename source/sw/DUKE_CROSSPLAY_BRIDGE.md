# Duke map compatibility bridge in the Shadow Warrior runtime

This document is the engineering handoff for the Shadow-Warrior-runtime +
Duke-map compatibility layer. It should be updated whenever bridge behavior
changes.

## Goal

When launched like this:

```sh
./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g ~/games/duke3da/duke3d.grp -map E1L1.MAP -wnd -res1280x720 -sc -sm
```

the active runtime is Shadow Warrior, but the loaded level is a Duke-family map.
The bridge must preserve Duke map semantics broadly:

- Duke sector effectors, activators, locators, switches, doors, trains,
  teleporters, elevators, lights, explosions, sounds, and sprite behaviors
  should operate like Duke.
- SW player movement/weapons should remain playable inside the Duke map.
- Duke map art, palette, sounds, and music should be selected from Duke data
  where the map expects Duke resources.
- The code should be source-of-truth-driven by map tags/control sprites and
  shared metadata. Avoid one-off fixes for specific coordinates or specific
  screenshots.

## Code map

Main entry points:

- `source/vibebuild32/src/gameselect.cpp`
  - public CLI parsing and attached/separated argument normalization;
  - game selection and domain routing;
  - `-gm.lvls` / `--game.levels`;
  - crossplay data mounting rules.
- `source/build/src/tiles.cpp`
  - map tile capture/remapping and foreign ART data.
- `source/build/src/palette.cpp`
  - runtime/foreign palette handling.
- `source/sw/src/game.cpp`
  - most of the Duke-map compatibility bridge;
  - capture, ticking, player carry, sector effectors, sprite behaviors.
- `source/sw/src/sprite.cpp`
  - SW missile movement; contains the current preflight guard for Duke maps.
- `source/sw/src/weapon.cpp`
  - SW projectile/hitscan paths that need to feed Duke sprite/sector behavior.
- `source/sw/src/player.cpp`, `source/sw/src/ninja.cpp`
  - SW player dimensions, movement, and weapon/player state.
- `source/sw/src/sounds.cpp`, `source/sw/src/digi.h`
  - Duke sound aliases used by the bridge.

The bridge is active when `SW_CrossplayDukeMapBridgeActive()` returns true:

```cpp
artCrossplayMapArtPrepared()
&& SW_CrossplayIsDukeLikeGame(artCrossplayEffectiveDomainGame(ART_CROSSPLAY_DOMAIN_LEVELS))
```

Current Duke-like map domains are Duke, World Tour, NAM, Napalm, and WW2GI.
Ion Fury is a Duke-runtime game, but it is not currently treated as a Duke-like
map domain by this SW bridge.

## Load-time flow

`source/sw/src/game.cpp::LoadLevel()` currently does this after loading the map:

```text
artCrossplayCaptureMapSprites()
artCrossplayPrepareForeignMapArt(filename)
artCrossplayApplyMapTileRemap()
SW_CrossplayCaptureDukeMapEffectors()
```

Then SW loads ART with base runtime ART pinned first when foreign data exists.
This is intended to avoid SW weapon/HUD art being remapped to Duke art while
still allowing Duke map tiles to render.

## Captured Duke map systems

`SW_CrossplayCaptureDukeMapEffectors()` and related helpers currently build
runtime state for:

- sector effectors;
- activators, activator-locked sectors, touchplates, masterswitches;
- GPSPEED and locator sprites;
- subway/train/two-way train sectors;
- SE7 teleporters and water/stack warps;
- floor/ceiling movers and sector Z animations;
- warp elevators;
- rotators and rotating-rise sectors;
- swing doors, sliding doors, and stretch bridges;
- auto-closing sectors;
- explosive sectors and invisible explosive sources;
- lights, lightning, and quake/debris effects;
- respawns;
- waves;
- projectile shooters;
- `MUSICANDSFX` map sounds;
- tripmines and tripmine laser beams;
- visible Duke sprites that need behavior in the SW runtime.

The bridge removes/culls Duke control sprites after capture, but should preserve
visible Duke decorative/interactive sprites and attach behavior to them.

## Sprite behavior bridge

Visible Duke sprites are registered through the `SW_CrossplayDukeSpriteBehavior`
path in `source/sw/src/game.cpp`. The behavior flags currently cover:

- switches;
- breakables;
- actors;
- explosives;
- animated sprites;
- alive/awake actor state;
- visual overrides;
- tripmines;
- invisible explosive/control sources.

Known intended behaviors:

- Duke actors shown in Duke maps should use Duke-like movement, attacks,
  projectiles, death, and sounds.
- Duke breakable sprites should react to SW hitscan, melee, and projectile
  impacts.
- Invisible Duke explosive source sprites must remain invisible, but still
  trigger explosions.
- Tripmines need visible red laser beams and should detonate on player crossing.
- Projectiles fired by Duke actors and turrets need visible Duke projectile art.

## Recent uncommitted bridge edits

The latest bridge work before this doc update focused on making the bridge more
general rather than fixing individual screenshots:

- `SW_CrossplaySyncDukeMovedPlayer()` centralizes player position/sprite/sector
  synchronization after Duke-compatible sector movement.
- Sector floor/ceiling movement, sector translation, warp elevator movement,
  train movement, rotators, conveyors, and related helpers use that shared sync
  path instead of duplicating partial player updates.
- Train carry state tracks the current and last train sector with a grace window
  to support standing and jumping inside Duke trains.
- SE7 teleporter detection now considers the SE sprite Z and the enclosing
  sector floor/ceiling planes. This is intended to cover floor teleporters and
  vertical shaft/pipe teleporters.
- Non-floor teleporter destination Z uses a bounded relative offset rather than
  a hard-coded `dest.z + 6144`.
- `source/sw/src/sprite.cpp` contains
  `SW_CrossplayIgnoreDukeMapMissilePreflightHit()`, which prevents SW missiles
  and shurikens from being killed during initial spawn/preflight in Duke maps
  such as E1L1 rooftop.

## Duke enemies run Duke's own code

Enemy behavior is not approximated any more.  Duke enemies are defined by Duke's
CON scripts plus the hard-coded cases in `actors.cpp`, so anything reimplemented
in the SW runtime will differ from Duke no matter how carefully it is tuned.
Instead, the Duke actor system itself runs inside the SW session:

- `source/vibebuild32/src/crossplay_dukeai.cpp` brings up the Duke state the
  actor system needs (one allocated player, compiled CON scripts, enemy flag
  tables) and exposes a `VB_DukeAI_*` API.  Duke's objects are not symbol
  localized by the per-game merge, so SW can call into them directly.
- `VB_DukeAI_MoveActorsOnly()` (in `actors.cpp`) runs only the actor half of
  Duke's world tic; the SW bridge keeps owning the player, sectors and
  effectors.
- The SW side hands Duke every adoptable map sprite at load time, straight off
  the freshly loaded map, before SW's own sprite preparation resizes, converts
  or culls anything.

Two pieces of shared engine state have to be swapped around each Duke tic,
because both games disagree about what it means:

- **statnums.**  SW's `STAT_MISC` is Duke's `STAT_ACTOR`, and so on.  The status
  lists are rebuilt to contain only Duke's sprites for the tic, then rebuilt for
  SW afterwards with Duke's sprites parked in `STAT_NO_STATE`.  The engine's
  free list at `MAXSTATUS` is never touched, so Duke can still spawn.
- **picnums.**  Duke's AI keys off Duke tiles while the foreign-art layer gives
  SW a different tile to draw, so sprites hold Duke tiles during the tic and
  display tiles outside it.

The player is mirrored into Duke each tic (position, sector, angle) and its
sprite is presented as Duke's own `APLAYER` with a large health pool; whatever
Duke's actors take off that pool is applied to the SW player.  SW weapon hits
go the other way through `VB_DukeAI_DamageActor()`, which fills in the same
`actor_t` fields Duke's own weapons do, so pain, knockback, gibbing and death
are Duke's.

What this gets right by construction, rather than by tuning:

- exact Duke health (CON `strength`): LIZTROOP 30, PIGCOP 100, COMMANDER 350;
- exact Duke skill filtering, driven by `ud.player_skill` mirrored from SW;
- Duke's wake logic, movement, attack cadence, projectiles and death;
- Duke's sprite animation.  Duke never animates by changing `sprite[].picnum`;
  the drawn tile comes from the actor's CON action and the viewer's angle, so
  the bridge asks Duke for it via `VB_DukeAI_DisplayPicnum()`.  Without that,
  every enemy stands frozen on one frame.  Duke also steps back through frames
  whose art is missing; that test must be applied to the tile *after* it is
  remapped into the host's tile space, because Duke tile numbers mean something
  else in SW's numbering -- resolving it in the wrong space walked a firing
  turret onto unrelated tiles and made it appear to vanish;
- Duke's actor palettes.  Duke's spawn code assigns Duke palookup indices (a
  LIZTROOP gets pal 22).  Those indices select a completely different palette
  in the host, so Duke's pal is kept in Duke space alongside its tile and the
  sprite draws at pal 0, where the remapped foreign art already carries Duke's
  own colors;
- Duke's tic rate.  SW runs at 40 tics/sec and Duke at 30, so the Duke world is
  stepped on its own clock; one Duke tic per SW tic makes every enemy move and
  animate a third too fast.
- Duke's enemy sounds.  Duke decides which sound plays and when, which is part
  of the behavior, but SW owns the audio device: Duke calling `FX_Init()` /
  `FX_SetCallBack()` underneath SW would tear down SW's own sound.  So
  `A_PlaySound()`/`S_PlaySound()` are routed to a host hook that plays the sound
  through SW.  The pairing is built from data, not a hand-written table: Duke's
  CON `definesound` gives every sound a filename and SW's sound table already
  carries the Duke `.voc` files by name, so matching the two maps 165 sounds.

SW's explosions reach Duke's sprites through `SW_CrossplayExplodeDukeMapSprites()`.
SW damages by walking its own status lists and reading the SW USER record off each
sprite, and Duke's sprites have neither, so a grenade or rocket used to detonate
inside a Duke enemy without touching it.  Duke actors take the blast through
Duke's damage protocol, with Duke's distance falloff, so enemies gib the way
Duke's do; the strength comes from SW's own `DamageData` entry for that
explosion, so an SW weapon hits Duke's actors exactly as hard as it hits SW's.

The same pass reaches the bridge's own Duke props, which is what lets a grenade
blow open Duke's cracked walls: `CRACK1`-`CRACK4` and the `SEENINE`/`OOZFILTER`
charges are explosive sources wired to the map's SE_13 explosive sectors.

Those charges are deliberately **not** handed to Duke.  Detonating one drives the
SE_13 effector that opens the wall, and those effectors are run by the bridge,
not by Duke, so a Duke-owned charge would explode without opening anything.
Duke's spawn-time appearance rule is applied to them in the bridge instead: a
charge narrower than `xrepeat <= 8` is hidden but stays explosive, which is how
mappers bury charges inside vents and walls.

Duke weapon and ammo pickups are converted to the SW weapons the player
actually gets, and show the SW weapon's own icon; other Duke items keep the
map's original art, which is what the level was built to look like.

Tripbombs and `SEENINE`/`OOZFILTER` charges are adopted too, for the same
reason: Duke's own code lays out the laser line sprites and decides what walking
into the beam does, and Duke's spawn rule is what makes a narrow charge
(`xrepeat <= 8`) invisible while still explosive.  Map authors rely on that to
hide charges inside vents and walls.

`VB_CP_DUKEAI=0` falls back to the bridge's older approximation.

## Automated verification

The suite runs in **phase groups**, because its phases shared one level and a
later phase saw whatever the earlier ones had destroyed -- tripmines were being
blown up before they could be tested, and reordering the phases only moved the
problem to a different phase.  `VB_CP_SELFTEST` selects a group, and the sweep
runs the game once per group so each starts from a pristine level:

```sh
for group in actors world destruct tripmines; do
    VB_CP_SELFTEST=$group ./vibebuild32 -gm sw -g "/path/Sw.grp" -gm.lvls duke \
        -g /path/duke3d.grp -map E1L1.MAP -wnd -res640x480 -sc -sm -nosound -nomusic
done
```

`VB_CP_SELFTEST=1` still runs every group in one pass, which is convenient but
carries the cross-phase contamination this split exists to avoid.

Isolating the groups also settled several long-standing "failures" as artifacts
of that contamination rather than bridge faults, and made the two remaining
checks tell the truth:

- a tripmine population is counted *before* the approach, since walking up to one
  mine can put the player through another's beam and set it off;
- ping-pong is a teleport that fires again inside Duke's 13-tic hold, not simply
  a second teleport: a player left standing on a destination pad legitimately
  goes again once the hold expires (measured gaps are ~101 tics).


The bridge has a self-test harness, enabled with `VB_CP_SELFTEST=1`, that drives
the local player through the systems captured from the Duke map and prints one
`CPTEST` line per checked behavior before quitting:

```sh
VB_CP_SELFTEST=1 ./vibebuild32 -gm sw -g "/path/Sw.grp" -gm.lvls duke \
    -g /path/duke3d.grp -map E1L1.MAP -wnd -res640x480 -sc -sm -nosound -nomusic
```

It covers Duke actors (engagement, attacks, animation frames, frame/art
resolution, sounds, death by SW direct hits and by explosions), teleporters (fires exactly once, no ping-pong), trains (player carry
slip), breakables, explosives, doors/rotators/lifts, and tripmine beams and
detonation.  `VB_CP_CAPTURE=<frames>` saves one screenshot that many frames in,
under both the SW and Duke runtimes, so a Duke level can be compared against
itself across runtimes.

Last recorded result: E1L1-E1L8 and E3L6 all report `fail=0`.
Rabid Transit's four trains carry the player with zero slip, deterministically.

## Last recorded verification

Build:

```sh
make -j8 vibebuild32
```

Last recorded result: linked successfully with warnings only.

E1L1 smoke:

```sh
timeout 12s ./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g /Users/alonamir/games/duke3da/duke3d.grp -map E1L1.MAP -wnd -res640x480 -sc -sm -nosound -nomusic
```

Last recorded result: startup ran until the timeout. Logs showed Duke parallax
sky, rotators, teleporter sectors, activators/touchplates/masterswitches,
floor/ceiling movers, swing/sliding/stretch doors, auto-close sectors,
explosives, lights, respawns, quake, map sounds, and Duke sprite behaviors.

E3L6 / Rabid Transit smoke:

```sh
timeout 12s ./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g /Users/alonamir/games/duke3da/duke3d.grp -map E3L6.MAP -wnd -res640x480 -sc -sm -nosound -nomusic
```

Previously recorded result: `SIGSEGV` around startup after bridge capture/palette
remap logging.  This no longer reproduces; E3L6 starts and runs. The log before the crash showed capture of Duke rotators, train
sectors with locators, teleporter sectors, water/stack warps,
activators/touchplates/masterswitches, floor/ceiling movers, auto-close
sectors, explosives, lights, respawns, waves, sounds, Duke player dimensions,
and Duke sprite behaviors.

This crash is the first thing to debug before claiming Rabid Transit behavior is
fixed.

Suggested crash command:

```sh
lldb --batch -o run -o "bt all" -- ./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g /Users/alonamir/games/duke3da/duke3d.grp -map E3L6.MAP -wnd -res640x480 -sc -sm -nosound -nomusic
```

## Current known open issues

Fixed and covered by the self-test harness:

- E1L1 pipe/shaft teleporters work in both directions and fire exactly once.
  Duke triggers a non-floor SE7 purely on the player's Z passing the SE sprite
  Z; accepting the sector floor as a trigger plane made anyone standing in the
  sector teleport, which is what produced the ping-pong.  The post-teleport hold
  is also re-armed from the return teleporter's own trigger instead of plain
  sector membership, which used to latch a shaft to one direction.
- E1L1 swinging/rotating doors.  Duke refuses to operate a swinging door group
  while any leaf of it is still moving; without that guard two door sectors
  sharing an activator tag toggled each other back in the same tic.  Activator
  tags and their target sectors are also each operated once per trigger.
- Duke actors move at all.  `clipmove()` takes 18.14 fixed-point velocities and
  the bridge was passing plain units per tic, which truncated to no movement.
- Duke enemy behavior, speed, animation, palette, sounds, damage and death.
- SW explosions damage Duke enemies, charges, cracked walls and breakables, at
  SW's own weapon strength.
- A firing Duke actor no longer resolves onto unrelated tiles (the missing-art
  frame fallback was being applied in the host's tile space).
- Tripmine beams and player contact, and narrow invisible explosive charges.
- Leaving a Duke level no longer crashes.  The crossplay path skipped
  `JS_InitLockouts()` while `TerminateLevel()` always walks those lists.
- Duke breakables and explosives react to SW weapons.

Also fixed in the host, all from the two games disagreeing about a convention:

- **Sprite lighting.**  Duke *assigns* a sprite's draw shade from its sector
  (`t->shade = l`) and its spawn code has already stored that sector shade on the
  sprite; SW *adds* the sector shade to the sprite's own and darkens most
  statnums by a further 6.  Duke's sprites were therefore counting the sector
  shade twice and going nearly black in dim rooms.
  `SW_CrossplayDukeMapSpriteShade()` reports Duke's rule for Duke map sprites.
- **Switch frames.**  Duke draws a switch with the base tile off and base+1 on;
  the bridge had these inverted, so a flipped switch never showed as flipped.
  A switch the map places already-on is now recognised too.
- **Mirrors.**  Duke and SW recognise a mirror by the same rule and differ only
  in the tile number, so Duke's mirror walls are retagged with SW's mirror tile
  and SW's own mirror system takes them.  Walls whose remapped overpicnum lands
  on SW's mirror tile by coincidence are masked during that scan -- without
  that, ordinary Duke walls became mirrors and their sectors were rewritten.
- **Invisible blockers.**  Duke *assigns* `cstat = 32768` to a narrow charge,
  which clears the blocking bits as well as hiding it.  Only adding the
  invisible flag left invisible, zero-sized blockers all through Rabid Transit's
  tunnels, and rockets and grenades detonated on them as they spawned.
- **Ride interpolation.**  The bridge registers moving Duke sector walls with
  SW's interpolation, so the geometry glides between tics.  The player carry was
  also advancing `opos`, which snapped the player a whole tic per frame while
  the world slid smoothly -- felt as the view shaking on a train.  The carry now
  leaves `opos` behind so the ride interpolates with the walls.

Notes:

- The CON warnings in the log (`found 'else' with no 'if'`, and the two
  overwritten sound definitions) come from stock Duke `GAME.CON`/`USER.CON`.
  Running the same data under `-gm duke` prints exactly the same four warnings,
  so they are not a crossplay problem.

Fixed since:

- **Respawned Duke enemies now run Duke's AI.**  E1L1's ambushes (a touchplate
  spawns troopers behind the player -- two of the six are JETPACK troopers) went
  through the bridge's old respawn path, which registered the spawn with the old
  approximation layer: a trooper that walks and turns but can never fly.  That
  was the user-reported "trooper spins around itself in the box", and it evaded
  every map-sprite audit because it only exists after a trigger fires mid-game.
  `SW_CrossplaySpawnDukeRespawnedSprite` now adopts enemy tiles into Duke AI
  (falling back to the old path only if adoption fails), `VB_CP_FIRE_RESPAWNS=1`
  fires all captured triggers for testing, and the harness gained a
  `no-old-layer-actors` invariant so no actor can silently run the
  approximation again.  Diagnosed from a player session recording
  (`VB_CP_WATCH_AREA=1`): the spinner was sprite 525, original tile 1725, in a
  reused slot -- owned=0, behavior flags ACTOR|ALIVE.

- **FIREEXT is supported rather than excluded.**  It was previously left out
  because it arms the host's SEENINE/OOZFILTER charges and detonates through the
  SE effectors the host runs, so a Duke-owned one would explode without
  triggering anything -- the same trap the charges themselves fell into.  The
  fix is a seam rather than an exclusion: Duke reaches all of that machinery by
  hitag, so `VB_DukeAI_SetMapTriggerHook()` hands the host that hitag at Duke's
  `DETONATE`, and the host arms its own charges and fires its own effectors.
  Any future adopted tile that reaches `DETONATE` is covered by the same hook.
  A `duke-actor-map-trigger` check keeps it honest.

- **Duke owns its destructible props.**  EXPLODINGBARREL, FIREBARREL,
  NUKEBARREL, FIREVASE, BOUNCEMINE and the CANWITHSOMETHING family are adopted,
  so Duke's own code burns, breaks and explodes them instead of the bridge
  approximating it.  Each was checked to be self-contained first -- none reach
  into the map's sector effectors.  FIREEXT is deliberately *excluded* for the
  same reason as the charges: it arms SEENINE/OOZFILTER by hitag and detonates
  through the SE effectors the host runs, so a Duke-owned one would explode
  without triggering anything.  RECON needed nothing: it is a Duke enemy and was
  already adopted.

- **Netgame determinism.**  Two things in the bridge wrote client-local state
  into the shared simulation and would have desynced a lockstep game:
  - the drawn frame of a Duke actor was resolved in the *tic*, from the local
    camera, and written into `sprite[].picnum`/`cstat`.  Duke resolves that at
    draw time on the tsprite copy, and so does the bridge now
    (`SW_CrossplayDukeMapSpriteDisplayTile()`), which keeps the viewpoint out of
    shared state -- and is where Duke does it anyway;
  - only the *local* player was mirrored into Duke, so each client's actors
    would have chased a different target.  Every connected player is now
    mirrored into its own Duke slot in player order, which is also what lets
    Duke's actors pick targets properly in a co-op game.

  Netplay is still untested end to end; these remove the two hazards that were
  certain to break it.

- **Duke's explosive charges are a timed chain, not a single pop.**  Damaging one
  SEENINE/OOZFILTER arms *every* charge sharing its hitag; each then burns its
  own fuse (the sprite's lotag, counted off three at a time), steps through its
  damaged frames and detonates in turn, driving whatever effectors carry that
  hitag.  The bridge used to blow only the charge that was hit, so the rest of
  the chain never fired -- which is why E1L1's rooftop bottles did not open the
  vent: those canisters are hitag 190 with fuses of 0/30/60/110/120/140/160
  running from the roof into the building, and the vent opens off the later
  charges in that chain.
- **Duke sprites are interpolated.**  SW interpolates a sprite between tics only
  when it has an SW USER record, which Duke's sprites do not, so they were drawn
  at their raw 30Hz positions while the world around them rendered smoothly --
  an actor visibly stepped as it moved.  The bridge now records each Duke
  sprite's position every tic for the renderer to interpolate from.

- **Invisible blockers, properly this time.**  Clearing the blocking bits when
  hiding a narrow charge was being undone further down the same preparation
  loop, which re-applies `CSTAT_SPRITE_BLOCK`/`BLOCK_HITSCAN` to every explosive
  sprite.  Hidden charges are now `cstat = 32768` exactly, as Duke leaves them.
  This is what stopped rockets and grenades in Rabid Transit's tunnels: the
  charges were invisible, zero-sized and still solid.
- **A train runs the player down.**  Duke kills anyone its subway sweeps over
  while moving at `xvel >= 192`, dragging them to the train first; the bridge
  now does the same, keyed on Duke's own condition (the player's position
  resolves into the train sector although they were not in it).

- **More of Duke's switches change frame.**  The bridge kept its own table of
  switch tiles, and it was a subset of what Duke's `P_ActivateSwitch()` drives:
  LIGHTSWITCH2 (E1L1's theatre light plate), POWERSWITCH1/2 and LOCKSWITCH1 were
  missing, so flipping one fired its tag -- the lights did come on -- while the
  sprite stayed on its off frame.  All four are the `picnum++` kind, so they
  only had to be listed alongside the others in
  `SW_CrossplayDukeSwitchBaseTile()`.

- **Duke mirrors actually reflect.**  Duke's mirror walls are retagged with SW's
  mirror tile and registered by `JS_InitMirrors()` (above), but the reflection
  pass itself was still skipped on foreign levels: `draw.cpp` guarded the
  `JS_DrawMirrors()` call on the map not being a crossplay map.  That guard
  dated from before the retagging, when SW's mirror scan would have mistaken
  ordinary Duke walls for mirrors; with the retag and the coincidence mask in
  place it only meant the wall drew the flat mirror placeholder.  The pass now
  runs for crossplay maps too.

- **Lo Wang is Lo Wang's size again.**  The player's world sprite has to stay
  Duke-sized on Duke maps: Duke's enemies aim with `A_CheckHitSprite()`, a
  horizontal ray at shoulder height that must strike the player's sprite, and a
  sprite scaled for SW's taller player makes them miss entirely (measured:
  `duke-actors-attack` goes to `attacked=0` on E1L2/E1L4).  But Lo Wang's art is
  much larger than Duke's player art, so drawing at that scale had him towering
  over the level.  The two are now separate: `SW_PlayerSpriteXRepeat/YRepeat()`
  keep the world sprite Duke-correct, and `SW_CrossplayScalePlayerTSprite()`
  rescales the *drawn* copy in `analyzesprites()` to Lo Wang's own proportions,
  in proportion to the crossplay player height.

- **Adoption follows Duke's CON, not just its enemy flags.**  The load-time and
  respawn adoption test asked "is this an enemy tile", but a tile can have a
  plain `actor` block and still be a real Duke actor: E3L6's LIZMANSTAYPUT turns
  itself into a LIZMAN via `cactor` on its first tic, so an enemy-tile test left
  it on the approximation layer.  `VB_DukeAI_HasActorCode()` reports whether
  Duke's compiled CON has *any* actor block for the tile, and
  `SW_CrossplayDukeShouldRunDukeAI()` adopts on that when the sprite also
  carries the bridge's ACTOR|ALIVE behavior flags.  Found by the
  `no-old-layer-actors` invariant added with the respawn fix.

- **Nothing Duke breaks sounds electrical.**  E1L1's rooftop vent fan broke with
  a short-circuit crackle (`shorted.voc`).  Duke gives FANSPRITE
  `GLASS_HEAVYBREAK` in `A_DamageObject_Internal()`; the bridge's own break
  table had guessed `SHORT_CIRCUIT` for it, and for the water fountain and the
  machines and tech walls as well.  Duke never uses that sound for breaking
  anything -- it belongs to the shrinker and to sparking fixtures -- so every
  one of those was re-derived from Duke's own code: the fan and the machines
  break as glass, the water fountain breaks silently (it only spawns its
  water), everything Duke routes through `G_BreakWall()` plays Duke's
  `VENT_BUST` + `GLASS_HEAVYBREAK` pair, wall lights pick between the two glass
  sounds per shot as Duke does, and a broken fan drops its floor shadow to
  `FANSHADOWBROKE`.
  Duke's vent covers came along with it: GRATE1, PANNEL1/2/3 and CIRCLEPANNEL
  are breakable now (E1L1's rooftop vent cover is a PANNEL1, and was inert),
  each with Duke's broken tile and `VENT_BUST`.  Note this also takes those
  tiles off the bridge's generic tagged-sprite operator path, which is correct:
  Duke breaks a vent panel, it does not operate its tag.
  `VB_CP_BREAK_DEBUG=1` logs the tile and sound of every break.

- **A solid prop stays solid until it is broken.**  The player could walk into
  E1L1's rooftop vent without shooting it open.  Duke has no list of which props
  are solid -- a map sprite keeps the blocking bit its author gave it, and the
  break code is what clears it (`CS(i) &= 65535-256-1`).  The bridge was
  overriding that bit from a guess list and clearing it for everything not on
  it, so the vent cover (a floor-aligned PANNEL1, map `cstat` 289) was open
  ground.  The map's bit is now preserved; the list still applies on top, for
  tiles Duke's own spawn code makes solid regardless of the map.  A
  `breakable-clears-block` check keeps the other half honest -- a prop that
  started solid must stop blocking once broken, or the player is walled out of
  whatever it was covering.

Verified across E1L1-E1L8 and E3L6, run per group: all 36 group-runs report
`fail=0`.

Older note, still relevant to reading results:  Seven report `fail=0`; the residuals are
the tripmine detonation check on E1L4 and E3L6, where the mines that survive the
run's earlier destructive phases are ones whose beams cannot be crossed (the
mechanism itself passes on E1L3 and E1L7).  The suite is sensitive to phase
timing -- changing the length of one phase moves every later one -- so a single
run's result should not be read too closely.

Still open:

- Duke map palette/art mixing is only checked by eye.
- An actor that stands still is not necessarily wrong: reading Duke's own state
  for the E1L3 case showed `xvel = 0`, i.e. Duke's AI was holding that actor in
  place while its neighbours moved at 104/60/68.  The harness therefore judges
  actor behaviour across a map's sampled actors rather than demanding that every
  individual one engage.

The harness was tightened after an earlier pass had loosened it too far: an
actor sample must now have *half* its actors attacking rather than one; the
operable check samples only sectors Duke actually operates on a use-press and
then insists they move, instead of excusing whatever it happened to pick; and
the tripmine check tries successive mines rather than excusing a short beam.
The train-crush check counts the kill without carrying it out, because killing
the player ends the level and the rest of the run would measure a fresh map.

Earlier, and still true -- several "failures" were the test, not the bridge: an actor counts as attacking when it
*fires* (`VB_DukeAI_ShotCount()`), not only when a shot happens to land, since
landing depends on the map's geometry; tripmines are approached before their
beams are judged, because Duke only arms a mine when a player comes near; and
maps with no Duke actors skip the blast check instead of failing it.  Duke art is remapped
  into SW's palette by nearest-colour matching, so hues can shift slightly;
  side-by-side captures of E1L1 and E3L6 match in geometry, art and layout.

## Actor A/B harness (native Duke vs bridge)

For "this actor behaves differently than Duke" reports, the bridge has a
deterministic A/B rig, shared by both runtimes so traces diff line-by-line:

```sh
VB_CP_WATCH_TILE=1725 VB_CP_FORCEWAKE=150 [VB_CP_PINLOW=1] [VB_CP_PINDIST=2600] [VB_CP_BURN=1] \
    ./vibebuild32 -gm duke ... -map E1L1.MAP ...      # native reference
VB_CP_WATCH_TILE=1725 VB_CP_FORCEWAKE=150 ...  ./vibebuild32 -gm sw ... -gm.lvls duke ...   # bridge
```

`WATCH_TILE` pins the first sprite spawned as that tile; `FORCEWAKE` pins the
player beside it (LOS, kept alive), wakes it at the given tic, and logs one
`CPDBG tk=` line per world tic: statnum, z, zvel, xvel, ang, floordist,
ceilingz, action/move script ids, movflags (sprite hitag), AC_COUNT, x, y.
`PINLOW` stands the player on the ground instead (the way a real player
approaches), `PINDIST` sets the offset, `BURN` feeds periodic Duke FIRE damage
(an actor standing in flames).  All of it is env-gated and inert otherwise.

Findings recorded with this rig for the E1L1 jetpack trooper (tile 1725):

- Wake, 48-tic takeoff, ILL dive phase, fire-damage flinches: the bridge is
  tic-for-tic identical to native Duke (z, zvel, floordist, action ids and
  positions match exactly) until the krand streams diverge, in three staged
  scenarios including player-below and burning.
- A grounded trooper on the dumpster is ground-locked by Duke's own CON: it
  only re-enters jetpack when the player is *higher* (`ifp phigher`,
  GAME.CON:4175), its walk off the ledge is blocked by the engine's enemy step
  rule, and a blocked walk turns by `ang += 768` every 4th tic
  (actors.cpp `cgg`) -- the "spinning".  Native Duke does exactly the same, and
  in the burn A/B it was the *native* trooper that landed first.
- THE MASQUERADED PLAYER'S COLLISION SIZE (fixed; this one produced a wrong
  "it's native behaviour" verdict first, so it is worth its length).  During a
  Duke tic the SW player sprite carries Duke's APLAYER tile number so Duke's AI
  recognises it -- but `tilesiz[]` at that index holds the HOST's art
  dimensions, so every getzrange() a Duke actor ran saw a player body of
  garbage height.  A jetpack trooper diving at the player read the player's top
  as a floor one unit below itself for a single tic, CON's `iffloordistl 32`
  grounded it on the spot, and it ended up ground-locked and spinning near
  wherever it fell (E1L1's dumpster).  Enter/leave now swap Duke's real APLAYER
  dimensions into `tilesiz[]` for the span of the tic.  The staged A/Bs missed
  it because the pinned player was rarely under the flight path; the tell in
  the traces was a one-tic floordist collapse (fd 51 -> 1 -> 52) right as the
  actor crossed the player.  After the fix: 600/600 flying tics, zero dips.
  Lesson: an actor's z-environment includes the PLAYER's sprite; any masquerade
  must keep its collision dimensions Duke-correct, not just its picnum.
- Where behavior involves krand-timed AI decisions, the bridge matches native
  in behavior but not in timing: SW consumes the shared engine krand stream
  between Duke tics, so e.g. a re-jetpack that native takes at tic 132 can come
  at tic 210 in the bridge.  Behavior parity, phase-shifted randomness.

### Diagnostic environment variables

All of these are off unless set, and emit nothing in a normal run.  They exist
because the bugs above were only found by recording a *real* session -- staged
reproductions passed while the user's own game failed.

| Variable | What it does |
| --- | --- |
| `VB_CP_WATCH_TILE=<tile>` | trace the first actor spawned from this map tile |
| `VB_CP_WATCH_SPRITE=<idx>` | trace an exact sprite index (wins over the tile pin) |
| `VB_CP_WATCH_NEAREST=1` | each tic, log the enemy nearest the player |
| `VB_CP_WATCH_AREA=1` | log every Duke-flagged sprite around the player, owned or not |
| `VB_CP_FORCEWAKE=<tic>` | wake the watched actor at a fixed tic, for A/B determinism |
| `VB_CP_PINLOW` / `PINDIST` / `PINANG` / `VB_CP_APPROACH` | pin or walk the player to a fixed relation to the actor |
| `VB_CP_BURN=1` | apply fire damage to the watched actor |
| `VB_CP_FORCEHIDE` / `FORCEBOX` / `ZGUARD` | isolate visibility, collision box and z-clamp effects |
| `VB_CP_FIRE_RESPAWNS=1` | fire every captured map trigger, to exercise respawned actors |
| `VB_CP_ADOPT_DEBUG=1` | log tiles adopted via CON actor code rather than enemy flags |
| `VB_CP_TEST_SWITCH=1` | log switch activation and the resulting frame |
| `VB_CP_BREAK_DEBUG=1` | log each broken Duke prop's tile and break sound |
| `VB_CP_MIRROR_DEBUG` / `VB_CP_MIRROR_VIEW` | log mirror registration; pin the camera at a mirror |
| `VB_CP_PLAYERSIZE_DEBUG=1` | log the player's world vs drawn sprite scale |
| `VB_CP_CAPTURE=<frames>` | save one screenshot that many frames in |

## Debugging approach

Use these guardrails:

1. Reproduce with a small command and `-nosound -nomusic` unless audio is the
   specific target.
2. Prefer one generic bridge path over per-map fixes.
3. Keep control-sprite capture separate from visible sprite behavior.
4. When moving sectors, update:
   - sector geometry;
   - wall interpolation;
   - visible sprites in the sector;
   - player `pos`, `opos`, sprite position, `cursectnum`;
   - floor/ceiling Z range.
5. When fixing Duke sprite interaction, make sure all SW damage paths call into
   the same Duke bridge handler: hitscan, projectile impact, melee, shuriken,
   explosive radius.
6. When fixing art/palette, verify both directions:
   - Duke map sprites/textures in SW runtime;
   - SW player weapons/HUD in Duke maps.
7. After a bridge fix, smoke both E1L1 and E3L6 at minimum.

## Completion criteria

Do not call the SW-in-Duke bridge done until at least:

- `make -j8 vibebuild32` passes.
- E1L1 and E3L6 launch without crashing under SW runtime with Duke levels.
- The E1L1 pipe/shaft teleporter works.
- The E1L1 rotating door works.
- Rabid Transit train standing and jumping are smooth.
- Rabid Transit teleporters work once and do not ping-pong.
- Tripmines emit beams and detonate.
- Trooper, pigcop, turret, and basic Duke projectiles behave recognizably like
  Duke.
- Breakable/explosive props react to SW hitscan, melee, shuriken, and projectile
  weapons.
- Art/palette remains visually correct for both SW and Duke assets.
