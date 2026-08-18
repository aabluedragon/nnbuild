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
  `breakable-clears-block` check kept the other half honest -- a prop that
  started solid must stop blocking once broken, or the player is walled out of
  whatever it was covering.  (That check has since become
  `breakable-keeps-dukes-solidity`: once Duke's own damage code runs on the prop,
  whether the bit clears is Duke's decision, not the bridge's.  See below.)

- **Duke's enemies wear Duke's colours.**  Every assault trooper was the blue of
  its raw art.  Duke's `A_Spawn()` puts most of its enemies on an alternate
  palookup -- a pal-0 LIZTROOP is assigned `pal = 22` -- and the bridge could not
  carry that index over, because palette 22 is a different palette in the host.
  It zeroed the pal instead, which drew the art unrecoloured.  The recolour is
  now baked into the generated art: `artCrossplayLoadForeignGameTilePal()` reads
  the source game's own `LOOKUP.DAT` and composes its palette with the existing
  runtime-palette match, so the tile arrives already wearing Duke's colours and
  still draws at pal 0.  Generated tiles are keyed by (tile, source pal), so a
  tile used at two Duke palettes costs two art slots.

- **A solid prop stays solid until it is broken, and Duke decides which.**  The
  map's own blocking bit is preserved, and `VB_DukeAI_MapSpriteSolidBits()` asks
  Duke for the rest: its per-tile `A_Spawn()` cases plus its generic rules (a
  wall-aligned switch tile is solid, a tagged aligned sprite becomes a shootable
  faller, solid implies hittable).  The bridge used to guess this from a short
  list, so the player walked through E1L1's rooftop vent, its grates and every
  masked wall on the level.  `VB_CP_CSTAT_DUMP=1` dumps every map sprite's
  post-setup cstat; the same dump runs in native Duke (`premap.cpp`) and
  `A_Spawn(-1,i)` keeps the map's sprite index, so the two diff sprite for
  sprite.  That diff is how this was found and is the tool for any future
  "doesn't match Duke" report about sprite state.

- **Duke's own code decides what a damaged prop does.**  Five separate reports
  -- the vent's break sound, props that stayed solid, E1L1's yellow tape and
  vent surviving a gas-canister blast, a hydrant that broke without spraying
  water, a wall crack still drawn on a wall Duke had already blown open -- were
  one bug: the bridge carried a hand-written copy of Duke's
  `A_DamageObject()` / `A_DamageWall()` / `A_RadiusDamage()`, built from tables,
  and the copy drifted from Duke every time Duke's real code did something the
  tables did not describe.  It is gone.  `SW_CrossplayDamageDukeMapSprite()` now
  hands the hit to `VB_DukeAI_QueueMapObjectDamage()`, and Duke's own
  `A_DamageObject_Internal()` runs on the prop at the top of the next Duke tic
  -- where Duke's status lists are installed, so `A_Spawn()` works.  The queue
  supplies the prop's Duke tile and a temporary invisible source projectile
  (Duke's damage code reads the *source* sprite's tile to decide what kind of
  hit it was, and its `extra` for the damage), then deletes it again.
  Anything Duke spawns in response -- a hydrant's water, a crack's explosion,
  glass and scrap -- lands in Duke's lists and the host adopts it when it takes
  the lists back, which is the same path enemy gibs already take.
  The one-tic delay is real and visible to tests: the harness's breakables phase
  arms on one visit and judges on a later one instead of checking synchronously.

- **An exploding canister shakes the room.**  Duke shakes the view by setting a
  countdown (`g_earthquakeTime`, 16 tics for a detonating charge or an RPG); the
  host owns the camera, so it drains that value with `VB_DukeAI_TakeEarthquake()`
  after each Duke tic and drives its own `SpawnQuake()` from it.

- **A blast reaches everything near it, whoever set it off.**  Duke's
  `A_RadiusDamage()` walks Duke's own status lists, and the host's props live
  outside them so they keep animating -- so a Duke gas canister going off beside
  E1L1's yellow tape and vent cover left both untouched.  The host now installs
  `VB_DukeAI_SetRadiusDamageHook()` and runs the same blast over its own props
  (Duke-owned sprites are skipped: Duke already dealt to those).  The host's own
  explosion path had the mirror-image hole -- a detonating charge, crack or
  tripmine only ever hurt the player -- so it runs the full blast too, which is
  also what finally removes a crack from a wall the charge just opened.  Chained
  detonations are Duke's behaviour and are kept; a depth guard only stops two
  props setting each other off forever.

- **The use key needs the tag Duke's spawn hands out.**  Duke finds what is in
  front of the player with a lotag-only `neartag()` search, and `A_Spawn()` is
  what puts a lotag on a water fountain, a toilet and a stall (and 9999 on a
  PLUG).  The bridge carried the map's untagged sprite straight through, so the
  use key never saw a Duke water fountain and the player could not drink from
  it.  `VB_DukeAI_MapSpriteSpawnLotag()` supplies that tag, the same way
  `VB_DukeAI_MapSpriteSolidBits()` supplies the cstat.  Props whose answer to a
  use press is their own rather than a map tag are handled before the tag is
  treated as a map tag -- otherwise a fountain's tag 1 would operate whatever
  else on the level wears tag 1.  The fountain heals a point per press and shows
  its water only while it is being drunk from, as Duke's does; the bridge used
  to run that animation constantly.

- **...and it has to wear them on every frame.**  A trooper drew in Duke's
  colours while it walked and snapped back to the blue of its raw art the moment
  it shot.  The recolour is generated per (tile, Duke pal), but the lookup took
  the level's existing map remap first whenever the tile had one -- and that
  remap is the tile's *pal 0* art, generated by the level's own art pass without
  a palette.  The tiles a map places are exactly the base tiles of each actor
  family (`LIZTROOP`, `LIZTROOPSTAYPUT`, `LIZTROOPDUCKING`, `LIZTROOPJETPACK`,
  `LIZTROOPONTOILET`), so every frame generated on demand was correct and every
  frame that landed on a base tile was not.  It also made a pal 21 and a pal 22
  trooper draw identically, since both resolved to that one uncoloured tile.
  The recolour is generated first now; the map's copy is only the fallback for a
  tile whose recolour cannot be made.  `VB_CP_PAL_DEBUG=1` reports which path
  answered each resolution -- the fix takes E1L1 from 17 `via=maptile` hits to
  none.

- **A charge has to shake the room itself.**  E1L1's rooftop canisters went off
  without the jolt Duke gives them.  Duke opens `DETONATE` with
  `g_earthquakeTime = 16` and every charge, crack and tripmine reaches it -- but
  a charge is one of the few things the bridge deliberately does *not* hand to
  Duke, because detonating one drives the SE_13 that opens the wall and those
  effectors are the host's.  So the host's own detonation path has to shake for
  itself; both it and the per-tic harvest of Duke's earthquakes now go through
  one place.

- **A blast reaches every sprite, not just the ones the bridge has a name for.**
  The yellow "POLICE LINE DO NOT CROSS" tape on E1L1's rooftop survived the
  canisters going off beside it.  Duke's `A_RadiusDamage()` hands *every* sprite
  in range to `A_DamageObject_Internal()` and lets Duke's code decide; the bridge
  only delivered the blast to sprites its own behavior table had classified, and
  the tape (tile 930) is unnamed decorative art that no table lists.  What
  destroys it is Duke's generic spawn rule rather than any per-tile case: a
  *tagged, aligned* map sprite becomes a `STAT_FALLER`, and `G_MoveFallers()`
  drops it once an explosion has worn its health down.  Every Duke map sprite the
  host is not animating now reaches Duke's damage code, so Duke can apply that
  rule.  The host's own machinery is excluded -- sector effectors, activators,
  locators and the pickups converted into SW items are the host's to drive, and
  handing either to Duke would take it out of the system that runs it.
  `VB_CP_TEST_BLAST=<hitag>` detonates a charge group the way shooting one does,
  which is how this was reproduced without a human at the keyboard.

  Widening that delivery has one hard limit, learned the loud way: handing a
  sprite to the damage path means calling Duke's `A_Spawn()` on it, and
  `A_Spawn()` builds a *live* actor.  Pointed at an enemy's slot it resurrects
  the enemy -- blowing up E1L1's rooftop canisters filled the roof with pigcops,
  raised out of the slots of ones already dealt with -- and pointed at a spent
  charge it re-arms it.  So the widening covers scenery only: anything Duke would
  run (`VB_DukeAI_HasActorCode()`, `VB_DukeAI_IsEnemyTile()`) and the host's own
  charges are held back, and reach Duke through adoption at load or respawn time,
  which is where their state is set up properly.  What is left is what should have
  been there all along -- the tape, the vent cover, the fan, the masked walls.

- **A stale claim on a recycled slot must never delete a live host sprite.**
  This is what was eating SW's rockets.  Every record the bridge keeps about a
  Duke sprite is written on one tic and read on a later one, and in between the
  engine can free that slot and hand it straight back to the host.  A rocket that
  happened to draw such a slot was deleted by `SW_CrossplayDeleteDukeSprite()`
  within a tic of leaving the launcher: ammo spent, sound played, recoil applied,
  no projectile -- and no explosion either, because the deletion went through the
  engine's `deletesprite()` rather than SW's `KillSprite()`, so the USER record
  leaked and nothing on the SW side ever learned the sprite was gone.  That
  silence is why it looked like the launcher had simply failed to fire.

  The fix is the discriminator the rest of the bridge already uses: a sprite
  carrying an SW `User[]` record is the host's, because Duke's bridge sprites
  never have one, so a claim on it is stale by definition.  The deleter now drops
  the claim instead of the sprite, and `SW_CrossplayDukeAIEnterDukeSpace()`
  retires ownership flags naming a freed or host-owned slot before anything reads
  them.

  Measured with `VB_CP_TEST_ROCKET=1` (64 shots per run): E1L1 went from 9 rockets
  destroyed without moving to 0, repeatably, and every remaining rocket now dies
  through SW's own hit path with an explosion.  E1L4 was and stays 64/64.  Note
  that a rocket legitimately dies in preflight when something solid is within
  ~1200 units ahead -- `TestMissileSetPos()` -- which is vanilla SW behaviour and
  accounts for all 64 kills on E1L2, where the player start faces a wall 500 units
  away.

- **Scenery Duke shoots through must not stop the host's bullets.**  The bridge
  set `CSTAT_SPRITE_BLOCK_HITSCAN` on *every* prop it classified, where Duke adds
  it only to a sprite the map already made solid or that its own per-tile spawn
  makes solid.  Both bits are Duke's answer now
  (`VB_DukeAI_MapSpriteSolidBits()`), with the bridge's own list applied on top so
  a prop it drives stays hittable and its damage path can still run.  E1L1's
  rooftop vent, fan and grates keep exactly the cstats they had.

  This was found while chasing a report that SW's rocket launcher sometimes did
  not fire, and it is worth recording that **it did not fix that**: A/B'd with the
  old behaviour restored, rocket survival was identical (E1L1 10 instant deaths
  of 64 either way, E1L4 19 of 64).  It stands as a fidelity fix and nothing more.
  Two measurement traps were behind an earlier, wrong conclusion that it had
  helped -- the before and after numbers had been taken with different liveness
  predicates, and the probe that named a rock sprite as the culprit was computing
  distance with `SQ()` on full map coordinates, which overflows 32 bits and put a
  sprite 53768 units away "at the muzzle".  Both are fixed;
  `VB_CP_TEST_ROCKET=1` reports flight distance and names anything solid within
  900 units of the muzzle when a rocket dies without moving.

- **A cracked wall is Duke's to run, and nothing host-side may run inside a Duke
  tic.**  Shooting a crack did nothing, and a crack left over a wall opened some
  other way stayed drawn across it.  Two causes, one report.

  The crack was classified as nothing at all -- no behavior record, so no damage
  path reached it.  It is a `STAT_STANDABLE` actor: Duke watches it for an
  explosive hit and answers by arming the charges that share its hitag and
  jumping to `DETONATE`, which hands that hitag to the host's trigger hook,
  shakes the room, blows the wall open and deletes the crack.  So `CRACK1`..`4`
  are adoptable now, exactly as `FIREEXT` already was and for the same reason:
  the hook is what lets Duke's detonation drive the host's effectors.

  That exposed the second cause, which was mine.  Duke's earthquake was spawned
  as soon as its countdown was read -- *before* the swap back, while Duke's
  status lists were still installed.  `SpawnQuake()` is a host sprite insert, so
  it took the slot the detonating crack had just freed and linked it into a Duke
  list, and the swap back adopted it: a blank sprite, picnum 0, drawn ever after
  as whatever tile 0 generates, sitting exactly where the crack had been.  The
  quake, the map triggers and the blasts are all deferred past
  `LeaveDukeSpace()` now.  The rule this keeps proving: **host code must never
  create or delete sprites while Duke's lists are installed.**
  `VB_CP_WATCH_TILES=<t,t,...>` reports every change to the sprites of a given
  Duke tile, which is how both halves of this were pinned down.

- **Duke's toilet.**  Pressing use on one did nothing.  In Duke it is a short
  sequence rather than a single action, and it is worth writing out because none
  of it lives in CON: `P_CheckSectors()` plays `DUKE_URINATE`, holsters the
  weapon, and gives back a tenth of full health (topping up to full rather than
  overshooting); a second later the player tic plays `FLUSH_TOILET` and
  `DUKE_PISSRELIEF` -- the "ahh, much better" -- and a second after that the
  weapon comes back up.  Pressing it again before that finishes only flushes.

  The bridge does not run `P_CheckSectors()`, so all of it is implemented against
  the host: `PF_WEAPON_DOWN` is SW's own holster flag, the health is the host's,
  and the countdown runs from the host's player tic.  Duke's countdown ticks at
  26 to the second and SW's at 40, so the delays are expressed in SW tics to keep
  the same timing rather than the same numbers.

  Two things had to be fixed to get there.  `SW_CrossplayUseDukeProp()` read the
  tile out of the bridge's sprite behavior table, and Duke's toilets carry no
  behavior record at all -- the same thing that hides the security monitors -- so
  every unclassified prop was silently inert; it falls back to the map's own
  record of the tile now, which is what the damage path already did.  And SW had
  no `digi` entry for `pissing.voc` or `ahmuch03.voc`, so those two sounds mapped
  to nothing and simply did not play; `flush.voc` was already there.

  Two timings live in Duke's `last_pissed_time` and they are nothing like each
  other.  The little sequence above happens in its first two seconds, but the
  value starts at `26*220` and the player cannot relieve himself again until it
  reaches zero -- so the health is a once-every-few-minutes thing, not something
  to stand there and repeat.  Reading only the near end of that countdown made
  the whole thing a two-second cooldown, and the toilet handed out health as fast
  as the key could be pressed.  The cooldown is the full 220 seconds now, with
  the flush/relief at one second in and the weapon back up at two; a press inside
  it still answers -- Duke flushes -- but gives nothing away.

  `VB_CP_SELFTEST=world` covers it as `toilet-reachable`, `toilet-use-answers`,
  `toilet-heals`, `toilet-urinate-sound`, `toilet-holsters`, `toilet-relief-sound`,
  `toilet-weapon-returns`, `toilet-cooldown-answers` and
  `toilet-cooldown-no-repeat`.  The test presses through `neartag()` and
  `SW_CrossplayOperateDukeMapEffectors()` rather than calling the prop handler
  directly, so it covers the path the use key actually takes.  Whether the flush
  is audible on a given press depends on whether the last one has finished, which
  a headless run cannot judge -- it never retires its voice nodes -- so the
  cooldown checks assert that the press was answered and handed out nothing,
  rather than asserting a sound.

  `A_CheckSoundPlaying()` is answered from SW's own 3D voice list rather than
  from a handle the bridge remembered, because handles are recycled: once a voice
  finishes its slot can go to an unrelated sound, and testing the old handle then
  reports the first sound as playing forever.  A voice node carries the digi
  number it is playing, so matching on that is exact.

- **Keycards and card readers.**  A Duke `ACCESSCARD` was already converted into
  the matching Shadow Warrior card at load, so the player could pick one up, but
  the reader it opens ignored it entirely: `ACCESSSWITCH`/`ACCESSSWITCH2` were
  treated as ordinary switches, so pressing use on one opened the door whether or
  not the player was carrying anything, and carrying the card was worth nothing.

  Duke's rule is in `checkhitswitch()` and is short: the reader's *palette* names
  the card -- 0 blue, 21 red, 23 yellow -- and without it the player gets a quote
  (`BLUE/RED/YELLOW ACCESS CARD REQUIRED`) and nothing else.  With it, the reader
  operates its lotag, `P_ConsumeKeycard()` takes the card back out of the HUD,
  and the sprite advances to its lit frame -- which Duke's switch table does not
  list, so a used reader is inert from then on and cannot be swiped twice.

  All of that now happens on the bridge's side, keyed off the same palette.  The
  card the player is holding is read out of SW's own `HasKey[]` slots, because
  that is where the pickup put it: SW files cards as red 0, blue 1, green 2,
  yellow 3, which is what its HUD and its own locked doors read.  Three smaller
  faults came out with it:

  - The bridge fell back to operating a switch's *hitag* as a second map tag when
    the lotag opened nothing.  Duke never does: `P_ActivateSwitch()` plays the
    hitag as a **sound**, through `S_SoundIsValid()`.  Every stock card reader
    carries 212 (`MONITOR_ACTIVE`) there, so the fallback fired whatever else in
    the level happened to wear tag 212.  It is played as a sound now -- the beep
    a reader makes -- and never operated.
  - A refused reader fell through to the operate loop's plain "sprite has a
    lotag" search, which opened the door anyway.  A switch sprite is the last
    word on its own tag now.
  - The use ray only ever walked *down* from the player's eyes, the way Duke's
    does, but SW's eyes are not where Duke's are.  E1L1's only card reader sits
    above the ray entirely and could not be pressed at all.  The ray is walked up
    as well as down now.
  - On some levels there was no card to find at all.  The bridge applied Duke's
    pickup skill filter -- `lotag > player_skill`, delete -- to everything it
    recognised as a pickup, but Duke runs that check *above* `case ACCESSCARD` in
    `A_Spawn()`, so a card never sees it, and mappers use a card's lotag for
    other things: E3L6 tags each of its two cards with the door tag it opens
    (6166 and 6666).  Read as a skill both are above every difficulty, so both
    cards were deleted at load and the level could not be finished.  Keycards are
    exempt from the skill lotag now.

  Not modelled: Duke's twelve-tic pause with the hand-and-card drawn on the HUD
  before the door moves.  The reader answers immediately here.

  `VB_CP_SELFTEST=world` covers it as `card-reader-reachable`,
  `card-reader-refuses-without-card`, `card-reader-stays-dark`,
  `card-pickup-grants-key`, `card-reader-opens-with-card`,
  `card-reader-consumes-card`, `card-reader-lights-up`,
  `card-reader-is-one-shot` and `cards-survive-load` -- the last counts keycards
  the load pass deleted and asserts it is zero, since a level whose card is gone
  cannot be finished and nothing else would notice.  The test presses through `neartag()` and
  `SW_CrossplayOperateDukeMapEffectors()`, and picks the card up by walking onto
  it rather than by setting the flag, so both halves are the real path.  A level
  usually has several readers in places the harness cannot stand in front of, so
  they are tried in turn until one answers `neartag()`.  The card sprite is
  matched on its `USER` record's `ID` rather than its picnum: a Duke level loads
  Duke's art, so SW's own item tiles are relocated into the generated range and
  the picnum no longer reads as `RED_CARD`.

- **A rocket must explode on a mirror, not slide along it.**  Fired at a Duke
  mirror the rocket skated across the glass and kept going.  Two faults, one of
  them general.

  The general one: SW keeps its own `WALLFX` bitfield in `wall[].extra` and
  assumes every wall has one -- `WeaponMoveHit()` asserts it is not -1 -- while a
  Duke map leaves it at -1, every bit set.  So every wall in a Duke level read as
  `WALLFX_SECTOR_OBJECT` and more besides.  Wall extra is sanitised at load now,
  alongside the sector extra that already was (1975 walls on E1L1).

  The one that caused the sliding was the bridge's own.  `WeaponMoveHit()` gave
  the wall to `SW_CrossplayDamageDukeMapWall()` and, when it reported the hit
  handled, set `u->ret = 0` before returning true.  But `DoRocket()` only
  detonates `if (WeaponMoveHit() && u->ret)`, so the rocket survived its own
  impact and hit the same wall again on the next tic, and the next.  A mirror
  triggers it because its sector carries a light, so the bridge reported the hit
  handled for that alone.  Nothing about a Duke wall reacting -- breaking,
  flickering, setting off a charge -- is a reason for the missile to live, so the
  flag is left alone now.  Measured with `VB_CP_TEST_ROCKET_MIRROR=1`, which
  parks the rocket harness in front of a mirror: E1L1 went from 1099 wall impacts
  to 1.

  Breaking the glass is then Duke's rule, and an explosive-only one:
  `RADIUSEXPLOSION`, `SEENINE`, `HEAVYHBOMB`, `RPG`, `HYDRENT`, `OOZFILTER` or
  `EXPLODINGBARREL` swaps the tile for `MIRRORBROKE` and clears the masking bit;
  anything else leaves it standing.  Swapping the tile is also what stops the
  reflection, because `JS_DrawMirrors()` only renders one for a mirror tile the
  renderer asked for.  `VB_CP_SELFTEST=world` covers it as
  `mirror-needs-explosive`, `mirror-explosive-breaks`, `mirror-stops-reflecting`
  and `mirror-breaks-once`.  Note that the mirrors in these maps carry cstat
  97/101 and never had the masking bit, so Duke's `cstat &= ~16` is a no-op on
  them and is not worth asserting -- the tile change is the real evidence.

- **A broken hydrant, and everything else Duke drives through CON's player
  state.**  The water from a smashed fire hydrant roared -- far too loud, and
  shifting constantly -- and could not be drunk from.  Both are one CON actor,
  `TOILETWATER`, which a hydrant, a toilet and a smashed fountain all spawn, and
  both came from state the host owns and was not handing over.

  *The sound.*  CON runs the gurgle with `soundonce`, which is "play unless this
  is already playing", answered by `A_CheckSoundPlaying()`.  With the host owning
  playback, Duke's own `g_sounds[]` bookkeeping is never filled in, so that
  answer was always "nothing is playing" and the gurgle restarted on **every
  tic** -- a pile of overlapping voices rather than a running tap.  The host now
  remembers the voice handle it started for each (sound, sprite) and answers the
  question itself, through `VB_DukeAI_SetSoundCheckHook()`.  Measured: two
  gurgles over 200 tics, where before there was one per tic.

  *The drinking.*  Three separate things had to be true and none were.  CON asks
  `ifhitspace`, which reads the player's input bits -- the host never wrote them,
  so the branch could not fire; the use key is mirrored each tic now.  It then
  asks `ifphealthl MAXPLAYERHEALTH`, and Duke's mirrored player carried a flat
  30000-point pool so that damage could be read back as a delta -- which made
  every health test in CON meaningless, this one permanently false.  The host's
  real health is mirrored instead, and the delta is measured against whatever was
  written.  Finally the delta itself was clamped to positive on the way back, so
  even once Duke healed the player the host never heard about it: `addphealth`
  moves that pool *up*, and discarding anything below zero threw away every heal
  Duke's props hand out.  It is signed now.

  This last part is worth stating plainly, because it is not specific to
  hydrants: **Duke's props that give the player something back all work this
  way**, and none of them could function until the mirror carried health both
  ways.  Duke's own pacing survives -- `ifactioncount 32` means one point per 32
  action tics, which is the slow trickle the original has.

  `VB_CP_SELFTEST=destruct` covers it as `hydrant-sprays`, `hydrant-reachable`,
  `hydrant-sound-once` and `hydrant-drink-heals` (4 heals over 200 tics, matching
  the four `addphealth` calls Duke's CON makes in that time).  The harness has to
  force the use key and the facing angle through the mirror rather than writing
  SW's own input, because SW rebuilds both from the input device every frame and
  Duke's `pfacing` allows only 128 angle units either side.

- **Duke's security cameras.**  A monitor showed the blank screen art and nothing
  else; pressing use on one did nothing.  Nothing connected the cameras to a
  screen.

  Duke pairs them by tag: a `VIEWSCREEN`/`VIEWSCREEN2` carries the hitag of the
  camera group it watches and a `CAMERA1` carries that same value as its lotag
  (E1L1: five monitors on hitag 317, seven cameras on lotag 317).  Both are
  captured at load, and pressing use steps through the group the way
  `P_CycleCameras()` does -- putting the player's view behind the camera, moving
  to the next one on each further press, and handing the view back once the group
  runs out.

  The feed is rendered with SW's own `drawroomstotile()`, the call its camera
  mirrors already use, so a monitor shows the world exactly as SW draws it rather
  than as Duke would.  A screen owns one tile, claimed through the new
  `artCrossplayReserveTile()` so the generated-art allocator cannot hand the same
  slot out twice.

  A monitor is recognised by its own capture list rather than the sprite behavior
  table, which never classified it -- the lotag `A_Spawn()` gives it exists only
  so `neartag()` can find it, and treating that as a map tag would operate
  everything else in the level wearing tag 1.

  That much worked, and then four things about it turned out not to be Duke:

  - **The cameras did not move.**  It had been recorded here that `CAMERA1` is an
    adoptable tile and Duke's own actor code panned the ones whose hitag asked
    for it.  It does not: a captured camera is the bridge's, not Duke's, and
    nothing ran its sweep -- `camera-pans` measured E1L1's camera 54 (hitag 200)
    holding one angle for forty tics.  Duke's `CAMERA1` case reads the hitag as
    the width of a sweep and walks `ang` across it -- out, back twice as far, out
    again, then a jump and round once more -- and that arithmetic now runs on
    Duke's own tic, beside `VB_DukeAI_RunTic()`, so a camera pans at the speed it
    pans in Duke.  Most stock cameras have a hitag; the ones without stand still,
    which `camera-holds-still` pins down so the sweep cannot leak onto them.
  - **The deathmatch cameras were in the cycle.**  `A_Spawn()` shrinks a
    `CAMERA1` with a palette to nothing and files it away in single player --
    those are the multiplayer ones -- and the bridge captured them anyway, so
    pressing use walked through viewpoints Duke never shows (two of E1L1's
    seven).  They are skipped at capture now, and shrunk the way Duke shrinks
    them.
  - **The player could walk out of a camera.**  Duke's `P_ProcessInput()` returns
    before it reads a single movement key while `newowner >= 0`: the player is
    frozen at the spot where they pressed use and the only way out is the use key.
    The bridge instead handed the view back the moment they nudged a direction.
    The player is held now, and the use key -- cycling on, then off the end of the
    group -- is what releases them.  It is also the last thing
    `SW_CrossplayOperateDukeMapEffectors()` tries, so a monitor destroyed while
    the player is inside its camera cannot leave them frozen there for good.
  - **The feed died when the player stepped back out.**  `G_ClearCameraView()`
    deliberately leaves `g_curViewscreen` and the screen's bound camera alone, so
    in Duke a monitor goes on showing what its camera sees until the player walks
    away from it.  The bridge cleared the binding along with the view, so the
    screen went dark the moment the player came back to their own eyes.  Clearing
    now does what Duke's does -- hand back the view, free the cameras, leave the
    screen bound -- and the screen is released when the player leaves its active
    range.

  That last change also fixed something the bridge had been doing per monitor:
  Duke keeps a single `g_curViewscreen` and renders the world again only for
  that one.  The bridge ran a feed for *every* screen in range, which on E1L1's
  lobby wall is five extra full renders a frame.  `camera-feed-exclusive` holds
  it to one.

  Monitors sit in shallow alcove sectors, so there is no arithmetic that reliably
  says "in front of one": `VB_CP_SELFTEST=world` finds a stand point by probing
  rooms that adjoin the alcove until `neartag()` itself returns the monitor,
  which is why `camera-reachable` is a reported check rather than test
  scaffolding.  It now walks the use key's own probe heights in the use key's own
  order, and tries every monitor rather than only the first -- E1L3 mounts its
  first one where the harness cannot stand, and that check had been failing on
  that level since it was written.  The rest of the group is
  `camera-use-engages`, `camera-matches-group`, `camera-feed-drawn`,
  `camera-use-cycles`, `camera-holds-player`, `camera-use-releases`,
  `camera-feed-persists`, `camera-feed-exclusive`, `camera-pans`,
  `camera-holds-still` and `cameras-are-single-player`.

  Not modelled: shooting a camera out.  Duke only allows it when a CON asks for
  it, and stock Duke3D sets `CAMERASDESTRUCTABLE NO`, so the `STATIC` a broken
  camera puts on its monitor never happens in the shipped game.  The selection
  path already refuses a camera that has been hidden, which is the state Duke's
  damaged-camera code leaves behind.

- **A prop with CON actor code is still Duke's to answer for.**  E1L1's clocks
  could not be broken -- melee, bullets, blasts, nothing.  Duke gives much of its
  scenery a `useractor` state (`useractor notenemy CLOCK WEAK state breakobject`),
  and the bridge read that as "Duke is already running this tile, leave it alone".
  Nothing had adopted the sprite, though: it sat in the bridge's own scenery list
  with no behavior record, so both the melee path and the blast pass declined
  every hit and no one ran it.  The test is about the *tile* where it should have
  been about the *sprite*, and the callers already check ownership before they get
  there.  Enemy tiles and charge tiles are still excluded -- those reach Duke by
  other routes -- but a tile that merely has actor code is handed over now, and
  Duke turns the clock into a `BROKENCLOCK` as it should.

  `VB_CP_SELFTEST=destruct` covers it as `duke-con-prop-takes-a-hit`, which picks
  a Duke scenery prop that has CON code and is not owned by Duke, hits it, and
  requires Duke to have answered.  Nothing else covered this class: such a prop
  has no behavior record, so it never appeared in the breakable list.

- **Duke's flames animate the way Duke animates them.**  Reported as the street
  fire in E1L1 not being smooth.  It was not the art -- the crossplay layer
  already copies a tile's animation data and reserves a consecutive run for its
  frames -- and not the frame-resolution fallback either, which turns out to fire
  only for E1L4's octabrain.  Duke's flames are not picanm at all: they are CON
  `action`s, and the bridge was approximating them with a handful of frames on a
  slow counter.  `FIRE_FRAMES` is `-1 14 1 1 1` -- fourteen frames from one tile
  below the base, a new one every tic -- against the four-every-four-tics the
  bridge showed.  All four of them are Duke's own numbers now, counted on Duke's
  tic rather than SW's: `STEAMFRAMES 0 5 1 1 1`, `BURNING_FLAME 0 12 1 1 2`,
  `FIRE_FRAMES -1 14 1 1 1`, `FFLAME_FR 0 16 1 1 1`.  Covered by `flame-animates`,
  which watches a flame for two seconds and requires it to reach at least five
  distinct frames; the old four-frame approximation fails it.

- **A player who gets behind a mirror has to come back.**  A mirror is drawn from
  inside a small sector behind the glass, and nothing keeps the player out of it
  but the mirror wall's blocking bit.

  That bit does hold: `clipmove()` was driven into every Duke mirror wall in E1L1
  and E1L2, at five heights, with the player's own clipdist and clip mask, and it
  never crosses -- so the reported walk-in is not a hole in the wall.  What the
  bit does not stop is the player *ending up* there: a jump that lands on the wall
  line can flip `cursectnum` across it, and `pushmove()` -- which SW runs twice
  per move to unwedge the player -- then shoves them further in.  Once inside, the
  same bit blocks them from walking out.

  Nothing can legitimately stand in a mirror sector, so the bridge treats being
  there as the error and does what SW already does when a player falls out of the
  world: puts them back where they last legitimately were.  Covered by
  `mirror-keeps-player-out` (the wall holds) and `mirror-releases-player` (a
  player placed inside one is returned).  Worth being straight about: the exact
  route in was never reproduced, so the guard is the fix rather than the cause
  being removed.

- **Player skins, from both games.**  Shadow Warrior dresses its player by
  palette -- eight colours of Lo Wang, and only in multiplayer -- while Duke
  dresses its own by art, and already carries a skin list holding Duke, the stock
  enemies, Lo Wang and Shelly.  On a Duke level both sets should be on offer.

  Duke's half of this already existed and did not need rewriting: `P_SkinRenderTile()`
  maps an APLAYER action onto the equivalent frames of whichever skin is chosen,
  and its Lo Wang skin already reaches across for Shadow Warrior's art.  What was
  needed was the same lookup addressed from the other side, so the bridge passes a
  state and a frame rather than one of Duke's CON action numbers, and the tile that
  comes back is brought across with `artCrossplayLoadForeignGameTile()` -- a host
  that is not Duke has none of Duke's art loaded, so the raw tile number would
  index the wrong game's tiles.  Two things had to be added on Duke's side: its own
  APLAYER art, which `P_SkinRenderTile()` does not answer for because Duke's
  renderer draws that directly, and a validity test that does not read the host's
  tiles at Duke's tile numbers.

  The list is Shadow Warrior's colours followed by whichever of Duke's skins the
  loaded game data can draw -- eighteen with an Atomic GRP -- and it is chosen from
  a Player Setup entry under Options.  A colour skin sets the drawn player's
  palette; a Duke skin swaps the drawn player's art.  Only the drawn copy changes:
  the world sprite keeps what it had, because Duke's enemies aim at that.

- **What an enemy drops has to be pickable.**  Duke's enemies drop by spawning a
  Duke pickup mid-fight (`state drop_shotgun` and friends, gated on
  `SPAWNAMMOODDS`), and the pass that turns a Duke pickup into a Shadow Warrior
  one runs only at map load.  A drop therefore kept Duke's tile and, having no SW
  `USER` record, could not be picked up at all.

  Converting one after the fact needs two things the load-time path gets for
  free: the Duke tile, which a runtime sprite has no map record of and which comes
  from Duke's own side instead, and the `USER` record plus state that
  `SpriteSetup()`'s per-icon switch would have given it.  That switch is a
  whole-map pass, so the icon-to-state lookup it implies now lives beside those
  states in `sprite.cpp` and can be applied to one sprite.  The conversion runs
  once per Duke tic with the host's status lists back in place, because converting
  a sprite splices it into an SW status list and doing that inside Duke's tic
  would leave it in Duke's.

  `VB_CP_SELFTEST=actors` covers it as `duke-drops-become-pickups`.  Duke's drop
  is random, so what is asserted is the invariant rather than the event: nothing
  Duke owns may still be sitting on a tile that should have become an SW pickup.
  Worth knowing: Duke actors do not die in the headless harness, so the conversion
  itself has not been watched happening -- only the invariant it maintains.

- **A converted pickup shows Shadow Warrior's icon, not Duke's.**  Items used to
  keep the Duke map's art on the grounds that it is what the level was built to
  look like, while weapons showed the SW icon.  That left the player reading
  Duke's icons for Shadow Warrior's contents -- a Duke first aid box that gives an
  SW medkit.  Both now show what they actually give.

- **The earthquake by E1L1's stairs has to blow the hole open, not just crack
  the ground.**  The quake ran -- the floor split and tilted, the room shook --
  but the opening in the side never appeared.

  The quake itself turned out to be exact.  Traced against Duke's own runtime,
  every one of E1L1's five `SE 2` sectors ends on the same coordinates and the
  same `floorheinum` as Duke's, on all four of the level's quake tags.  The hole
  is not part of the quake at all: tag 215's touchplate fires three master
  switches, two of which start the quakes in sectors 127 and 128, while the third
  sits in sector 122 on a 32-tic delay next to a `SEENINE` charge and an `SE 13`
  explosive sector.  Duke's master switch arms the charges standing in its own
  sector -- `shade = -31`, which is what lights a Duke fuse -- and it is that
  charge going off which sets off the `SE 13` and blows the hole.

  The bridge's master switch drove the effectors in its sector (quakes, drop
  floors, rise/falls, shooters, lights) but never armed the charges, so the third
  switch did nothing at all.  It arms them now, the way Duke does: a charge Duke
  has adopted gets the shade, since Duke's own code is what will run it, and one
  the bridge is running gets its fuse lit directly.

  Three timings came out with it, all the same conversion the doors needed: the
  master switch's hitag delay is a count of Duke's tics rather than SW's, so is a
  charge's fuse, and `SE 13` assigns its ceiling home unconditionally on the
  non-512 branch -- the step above that line is dead code in Duke's own source,
  so a sector blown open this way arrives in a single tic rather than sliding
  into place.

  With those, sector 122 opens to exactly Duke's geometry, 1.27s after the
  touchplate against Duke's 1.43s.  What is left of that gap is not this
  mechanism: Duke's actor code is driven once per SW tic, so everything Duke owns
  runs a third faster than it does in Duke.  That is a much larger question than
  one charge, and is left alone deliberately.

  `VB_CP_SELFTEST=destruct` covers it as `master-switch-arms-charges`, checked
  before the phase's own blast because that blast sets off every charge on the
  map and would hide it.  It arms the switch directly rather than through
  `SW_CrossplayOperateDukeTag()`, which refuses a tag whose sectors are still
  moving from an earlier phase, and it samples every tic -- a charge with no fuse
  time of its own is armed, pops and is gone again well inside the wait.

  Correcting the fuse rate moved when charges go off, which moved what the rest
  of the destruct group sees, and three of its checks turned out to have been
  passing on the old timing by luck: `crack-sprite-removed` and
  `breakable-damage` judged the hit before Duke's own tic had run the hand-over.
  Their windows now allow for a fuse's worth of Duke tics rather than a handful
  of SW's.

- **Duke's episodes, and finishing a level.**  New Game offered Shadow Warrior's
  two episodes, and no Duke level ever ended: the nuke button flipped between two
  frames and nothing more, and the bridge had no level-exit path at all.

  SW's progression is a flat index -- `Level` indexes `LevelInfo[]`, the episode
  menu picks the index its first level sits at, and finishing a level does
  `Level++` -- so the Duke side needs a table in the same shape.  Duke names its
  levels `E<episode>L<level>.MAP`, so the table is built at startup by asking the
  GRP which of those it actually contains rather than assuming a layout: the
  Atomic GRP reports four episodes and 41 levels, 1.3D fewer, and shareware one.
  `LevelInfo[]` and the episode name arrays are sized for either game now, since
  Duke has more levels than Shadow Warrior's twenty-nine, and `MAX_LEVELS` was a
  compile-time bound in two places that had to become a runtime one.

  The episode menu is laid out by hand in Shadow Warrior for exactly two entries;
  it is rebuilt from whatever the GRP held when a Duke GRP is in play, with the
  subtitles following the rows.  The shareware gate comes off those rows, because
  whether an episode exists has already been settled by whether its first map does.

  Duke's `NUKEBUTTON` is not a switch and was being run as one.  It is a fixed
  sequence: frame +1 at 8 tics, +2 at 16 where the fist starts, +3 at fist 26, the
  pipebomb bang and a white flash at 28, and the level ends past 42.  Its lotag is
  the secret-level number, not a map tag, so operating it as one fired whatever
  else in the level wore that number.  It has its own handler now, on its own
  countdown, and the countdown is in Duke's tics rather than SW's.  Duke's other
  exit -- a switch tagged 65535 -- ends the level too.

  `VB_CP_SELFTEST=world` covers it as `nuke-button-starts`,
  `nuke-button-animates` (all four of Duke's frames, not the two a generic switch
  would flip), `nuke-button-ends-level` and `nuke-button-advances-level`.  The
  phase puts the level back afterwards, since otherwise the rest of the suite
  would have no level left to run in.

- **Duke's doors, lifts and platforms move at Duke's speed, and the right way.**
  Reported as "the door in the arcade room doesn't open properly".  Reasoning
  about it from the source went nowhere -- the swing arithmetic checked out, the
  distances checked out -- so the answer was to stop reasoning and build an
  oracle: a door tracer compiled into *both* runtimes, operating the same sector
  on the same map and printing that sector's floorz, ceilingz, lotag and every
  wall coordinate once a tic.  Running E1L1 under `-gm duke` and under
  `-gm sw -gm.lvls duke` and diffing the two gave the answer in one pass, and
  four separate faults with it.  Two notes on building one of these, both learned
  the hard way: the two sides have to call the *same* entry point (`activatebysector()`
  and `SW_CrossplayActivateDukeBySector()`, not Duke's single-sector
  `G_OperateSectors()`), and the two runtimes tick at different rates -- 30 for
  Duke, 40 for SW -- so what is compared is the settled geometry and the time in
  *seconds*, never the state at tic N.

  - **Everything moved at a third of Duke's speed.**  Two causes multiplying
    together.  Duke advances a sector animation by `extra * TICSPERFRAME` -- four
    times the sector's speed value -- on every one of its thirty-a-second tics;
    the bridge stepped by the bare value once per forty-a-second tic.  And the
    speed value itself was always the 256 default, because Duke's `prelevel()`
    writes 256 into every sector's extra and then `A_Spawn()` lets a GPSPEED
    sprite overwrite it -- while the bridge looked for a GPSPEED sprite only when
    a door was actually pressed, by which time the control-sprite cull had long
    since deleted it.  E1L1 has three doors whose GPSPEED says 320, 160 and 360.
    Both are captured at load now, into the bridge's own table rather than into
    `sector[].extra`, which SW uses for its own purposes and the bridge zeroes.
  - **Sliding doors slid the wrong way.**  Duke turns the effector round rather
    than running the travel backwards -- `SA(i) += 1024` in `G_OperateSectors()`
    -- and the door then always slides forwards along the angle it is now facing.
    Sliding along the angle the mapper placed sent every one of them the wrong
    way on the first press, into the doorway they were supposed to clear.
  - **A door pair travelled together instead of parting.**  Duke's
    `STAT_EFFECTOR` list runs highest sprite index first, because
    `changespritestat()` pushes to the head and the map spawns in ascending
    order.  Where two leaves share a wall point both drag it, and the last one to
    run decides where it ends up -- so the order is not an implementation detail,
    it is which way the middle of the door goes.  The bridge ran capture order,
    which is the other way round.
  - **Auto-closing doors closed late, or never.**  Duke counts an SE 10's delay
    from the moment the door is open, *including* while it is still opening, and
    only defers the actual close if the door is still moving when the count runs
    out.  The bridge suspended counting until the door had settled, which added
    the opening time to every delay -- and the delay was being counted in SW's
    tics against a number written in Duke's.

  - **A door left standing open in the map read as shut.**  Duke's `prelevel()`
    marks a ceiling or splitting door whose floor is already below its ceiling as
    open, so the first press closes it.  Without that the bridge asked such a
    door to open to where it already was: nothing moved, and it only began
    working on the second press.

  `VB_CP_SELFTEST=destruct` covers the doors as `operable-moves`, one line per
  door.  Four things were wrong with that check, and all four hid faults:

  - it sampled two doors per kind, which is how a sliding door that slid
    backwards survived in E1L1 for as long as it did.  It takes every door now;
  - it never saw Duke's ceiling doors, floor doors, lifts or platforms at all --
    those have no capture list of their own, being started straight off the
    sector lotag -- and they were exactly the ones running at a third speed.
    They are collected off the lotag now, skipping locked sectors and sectors
    holding an activator, since Duke aims a press on those somewhere else;
  - its "did the sector move" signature was `ceilingz + floorz`, and a splitting
    door drops its floor by precisely as much as it lifts its ceiling.  Every one
    of Duke's splitting doors read as "never moved".  The two are weighted
    differently now;
  - and a door asked to travel to where it already stood was scored as a
    failure.  Duke's ceiling doors do that legitimately -- one with no higher
    neighbour to open into resolves its target to its own floor -- so the check
    now demands motion only when the animation was given somewhere to go.

  Also fixed there: the destruct group ended at the hydrant, so breakables,
  explosives and every door had been silently skipped in the group sweep since
  the hydrant phase was added.

  The `ST_27` extend-o-bridges took one more pass.  They looked like a grouping
  fault -- Duke appearing to extend one walkway where the bridge extended all six
  -- and that reading was wrong: it was an artefact of a tracer that operated one
  sector per time slot, so by the time a given sector's slot came round its group
  had already been run and finished in an earlier one.  Traced properly, with a
  single press and all six sectors logged together, both runtimes extend all six.
  What was left was the stopping point: Duke steps by 8 until `(t3>>6) >=
  (yvel>>6)` and then puts the last step back, so a bridge comes to rest eight
  units short of its sector speed rounded down to a multiple of 64.

  Measured on E1L1 against Duke's own runtime: every door, lift, platform and
  bridge in the level now settles in the same place, within 0.02s of the time
  Duke takes.  Before the change, none of the ones that moved vertically did.

  Two things to know before building another of these tracers.  Its Duke half
  segfaults Duke's own runtime if it is left to operate sector after sector over
  a long run -- one press per run is what is known to be safe.  And a
  slot-per-sector design cannot be trusted for anything that moves in groups, for
  the reason above; log every sector of interest on every tic instead.

- **Only an explosion opens a cracked wall, and opening it must take the crack
  with it.**  A thrown shuriken blew Duke's cracked walls open, and once open the
  crack stayed drawn across the hole.  Both came from one shortcut.

  Duke registers `CRACK1`-`CRACK4` as explosive *source* sprites, and the bridge
  had its own path from a wall hit to the charge group nearby: any hit on any
  wall in the sector, or within 2048 units, armed every charge whose hitag a
  source in range carried.  That answered to a shuriken, a bullet or a sword just
  as readily as to a rocket, and because it fired the charges directly it never
  touched the crack -- so `DETONATE`'s `KILLIT()` never ran and the sprite stayed
  on the wall.

  Duke's rule is in `actors.cpp` and is exact: a crack gives way only when
  `A_IncurDamage()` reports `FIREEXT`, `RPG`, `RADIUSEXPLOSION`, `SEENINE` or
  `OOZFILTER`, and any other weapon puts its cstat and angle back and leaves it
  standing.  Cracks are Duke's actors already, so the fix is to stop competing
  with them: the wall-hit path skips crack sources entirely, and what remains of
  it only runs for an explosive hit at all, since Duke's charges answer to blast
  damage rather than to being shot at.  Shooting the wall still works, by the
  route Duke uses -- the explosion's own radius pass hands the crack to Duke,
  which detonates it and deletes it.

  `VB_CP_SELFTEST=destruct` covers this as `crack-needs-explosive` (a shuriken
  fires no charges and leaves the crack standing), `crack-explosive-opens` /
  `crack-sprite-removed` (a rocket on the crack fires its group and removes it)
  and `crack-blast-opens` (an explosion *near* a crack does the same, which is
  how it is actually played).  The stages deliberately use cracks with different
  hitags: Duke's charges are one-shot, and running every stage against one crack
  made each stage measure the previous one's leftovers.

- **A sprite must never be in two of the engine's lists at once.**  Handing props
  to Duke made this the bridge's easiest mistake to make, and it cost three
  separate hangs.  The engine keeps its free slots on a status list of their own
  (`headspritestat[MAXSTATUS]`), so a slot spliced into a status list while it is
  still on the free chain welds the two together and the engine hands the same
  slot out twice; a slot spliced in twice leaves one list circular, and the next
  walk over it never ends.  Three rules now hold, one per place the bridge splices
  by hand:
  - the swap back skips a sprite Duke adopted during the tic (it is already in
    Duke's list),
  - ...and one Duke *deleted* during the tic, which is on the free chain now --
    a normal outcome, since a prop handed to Duke's damage code is Duke's to
    destroy,
  - and the damage queue skips any slot on the free chain, and takes one entry
    per sprite per tic.
  The symptom is worth recognising: the engine answers an exhausted pool by
  dumping every sprite and every per-actor gamevar, so a corrupted list shows up
  as the game apparently freezing while it writes hundreds of megabytes of log --
  E1L1's destruct pass produced 623MB of one gib tile before this was fixed.
  `VB_CP_SPRITE_DEBUG=1` prints `Numsprites`, the number of linked sprites and
  the length of the free chain after each Duke tic; when those three disagree, a
  list is crossed.

- **The bridge's "breakable" list is wider than Duke's.**  Delegating the damage
  exposed this: `PODFEM1`, `RUBBERCAN`, `FEMMAG1` and an undamageable `CAMERA1`
  are on the bridge's breakable list but have no case in Duke's
  `A_DamageObject_Internal()` at all, so Duke leaves them standing however hard
  they are hit -- and Duke's burnables (a tree, a box, a cone) only give way to
  an explosion or a fire weapon, never to bullets.  The old approximation broke
  all of them, which is why nobody noticed.  The `breakable-damage` check
  therefore asks for what the bridge owes -- every prop reaches Duke, and the
  ones Duke does break, break -- rather than for the approximation's behaviour.
  The list itself is still the bridge's own; deriving it from Duke, the way
  solidity now is, is the remaining piece of this work.

  The same correction applies to what a broken prop does about blocking.  Duke
  clears the blocking bit on a vent cover or a panel and *leaves it* on a water
  fountain, which breaks into `WATERFOUNTAINBROKE` and goes on standing there --
  E3L6's fountain is the case that showed it.  Since a damaged prop is Duke's by
  the time it is judged, its cstat is Duke's own answer, so the check
  (`breakable-keeps-dukes-solidity`) now only insists the bridge never *adds*
  solidity of its own; the `VB_CP_CSTAT_DUMP` diff against native Duke remains
  the tool for the rest.

- **A RECON needs somewhere to go.**  E1L2's patrol vehicle vanished the moment
  it noticed the player.  Duke's RECON navigates between LOCATORS sprites
  through `A_FindLocator()`, which walks `STAT_LOCATOR` -- and if it finds
  nothing it kills itself on the spot.  The host treats locators as hidden
  control sprites, so Duke's list was empty.  Locators are adoptable now; they
  are inert invisible markers, and the host reads its own by scanning every
  sprite rather than by status list, so handing them to Duke costs it nothing.

Verified across E1L1-E1L8 and E3L6, run per group: all 36 group-runs report
`fail=0`.

Older note, still relevant to reading results:  Seven report `fail=0`; the residuals are
the tripmine detonation check on E1L4 and E3L6, where the mines that survive the
run's earlier destructive phases are ones whose beams cannot be crossed (the
mechanism itself passes on E1L3 and E1L7).  The suite is sensitive to phase
timing -- changing the length of one phase moves every later one -- so a single
run's result should not be read too closely.

- **A slimer has to be able to get onto the player's face.**  Duke's slimer is
  not CON at all -- it is hardcoded in `G_MoveActors()`, and it latches on with
  one test: `if (g_player[p].ps->somethingonplayer == -1)`.  Duke sets that
  sentinel in its premap, which this bridge does not run, so the field sat at the
  zero every static starts life with and the test was permanently false.  The
  slimer would chase, crawl and sit at the player's feet forever, and never once
  bite.  `VB_DukeAI_BeginLevel()` now marks each player unset and
  `VB_DukeAI_SetPlayer()` seeds `somethingonplayer` (and `on_crane`, the same
  sentinel for the same reason -- a zero there reads as "sprite 0 already has
  me", which is what stops a crane picking the player up) on the first sync of a
  level.

  Once attached, Duke rewrites the slimer's position from the player's eye every
  tic, and that calculation is `s->z += (100 - horiz) << 4`.  The bridge had
  never mirrored the host's view pitch, so the thing would have hung at a fixed
  height while the player looked around; `VB_DukeAI_SetPlayerHoriz()` now carries
  SW's `q16horiz + q16horizoff` across.  Nothing else is needed for the view --
  the slimer is an ordinary sprite parked in front of the camera, and SW draws it
  because both games are the same engine.  Duke leaves its `sectnum` stale while
  it rides, which is Duke's own quirk and is left alone.

  Firing kills it, and not because the bullet reaches it: the slimer sets its
  own `cstat = 0` the moment it is on the player -- *"Don't block anything if on
  player"* -- so no shot can hit it and there is nothing there to damage.  Duke
  instead reads `SK_FIRE` straight out of the player's input bits and squibs the
  thing on the spot.  That was the only piece of Duke's actor code reading the
  fire bit, and the bridge had never mirrored it, so a latched slimer was
  unkillable: `VB_DukeAI_SetPlayerFireKey()` now carries it across beside the use
  key.  Duke exempts the weapons you cannot shoot something off your own face
  with -- pipe bomb, its detonator and the tripbomb -- so SW's grenade launcher
  and sticky bombs are exempt in the same way.

  Covered on E2L5 by `slimer-leeches-on`, `slimer-rides-the-face` (closest
  approach 124 units), `slimer-bites` and `slimer-dies-when-shot`, the last of
  which holds the fire key rather than calling a weapon directly -- pulling the
  trigger some other way would prove nothing about the path Duke actually uses.  The bite is counted as Duke player-
  damage events, not as a health drop: the harness pins the player at 1000 health
  every tic so runs survive, which would have made a health test vacuous.  With
  the sentinel reset commented out, `slimer-leeches-on` fails with
  `latched spr=-1`.

- **Duke's pistol ammo showed the wrong Shadow Warrior pickup.**  Duke's own
  `GAME.CON` feeds both `AMMO` and `AMMOLOTS` to `PISTOL_WEAPON`, and the bridge
  hands the pistol over as the shuriken -- but its ammo was mapped to the uzi
  clip, which belongs to the chaingun.  Both now show the shuriken, which in
  Shadow Warrior is the same pickup as the weapon.

- **No hitscan weapon could damage a Duke prop.**  Six of Shadow Warrior's
  hitscan paths -- `InitShotgun`, `InitUzi`, `InitEMP`, `InitHeartAttack`,
  `InitTurretMgun`, `InitSobjMachineGun` -- each reached SW's own
  `SPRX_BREAKABLE` list and stopped there, so a bullet passed through everything
  Duke owns.  Only a blast reached them, because `A_RadiusDamage()` never
  consults a sprite's cstat.  Each site now offers the hit to
  `SW_CrossplayDamageDukeMapSprite()` first.  Asserted as
  `duke-prop-takes-a-bullet`, which is reported *only* on maps where the harness
  could actually put a bullet into a Duke prop -- the direct-damage fallback the
  older `duke-con-prop-takes-a-hit` uses proves nothing about the hitscan path,
  and a pass from it would have been vacuous.

  Not fixed, deliberately: **the clock still cannot be shot**, in the bridge or
  in Duke.  Duke's `A_Spawn()` leaves `CLOCK` at `cstat = 80` -- wall-aligned and
  one-sided, without bit 8 (`CSTAT_SPRITE_BLOCK_HITSCAN`) -- and Duke's own
  weapons trace with `CLIPMASK1`, whose sprite mask *is* that bit.  Measured in
  Duke's own runtime: `hitscan()` from 64 positions around E1L1's clock reaches
  no clock at all, and force-setting the bit makes the very same probe report
  `hitscan CAN reach spr=181`.  A clock in Duke breaks to an explosion and
  ignores gunfire.  Giving Duke's CON-scripted wall props that bit here would be
  a deliberate divergence from Duke, and would work against the earlier fix that
  stops Duke scenery from eating the host's bullets.

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
| `VB_CP_KEY_DEBUG=1` | log what the use key found at each probe height: sprite, distance, behavior flags, source tile, switch state, lotag |
| `VB_CP_BREAK_DEBUG=1` | log each broken Duke prop's tile and break sound |
| `VB_CP_PAL_DEBUG=1` | log every (tile, Duke pal) art resolution and which path answered it |
| `VB_CP_SPRITE_DEBUG=1` | after each Duke tic, log `Numsprites`, linked sprites and free-chain length |
| `VB_CP_TEST_BLAST=<hitag>` | detonate the Duke charge group with that hitag, and hit any explosive source sprite carrying it (E1L1: rooftop canisters 253, cracked walls 214/223/237) |
| `VB_CP_WATCH_TILES=<t,t,...>` | report every picnum/statnum change to sprites whose Duke tile is one of these |
| `VB_CP_TEST_ROCKET_MIRROR=1` | with `VB_CP_TEST_ROCKET`, parks the harness in front of the first Duke mirror wall so rocket-versus-mirror behaviour can be counted |
| `VB_CP_TEST_ROCKET=1` | fire SW's rocket launcher repeatedly, reporting each rocket's flight distance and what was solid at the muzzle if it died there |
| `VB_CP_STRAY_DEBUG=1` | log host sprites found in Duke's lists and put back |
| `VB_CP_SLIME_DEBUG` | when a slimer is on the player's face, logs its cstat, statnum, health and what a ray fired straight ahead actually hits |
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
