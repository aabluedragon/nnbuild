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

Current recorded result: `SIGSEGV` around startup after bridge capture/palette
remap logging. The log before the crash showed capture of Duke rotators, train
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

The user reported these issues in SW runtime + Duke maps. Some have partial
bridge code now, but they still require verification:

- E1L1 rooftop: SW shurikens/projectiles failed to spawn/fire in some areas.
- E1L1 pipe/shaft: falling down the pipe should teleport like Duke.
- E1L1 rotating/spinning doors should behave like Duke.
- Duke skybox/parallax should look correct under SW runtime.
- Duke palette/art mixing should keep SW weapon/HUD art correct and Duke map art
  correct.
- Rabid Transit trains should carry the player smoothly when standing and
  jumping.
- Rabid Transit teleporters should not bounce the player back and forth.
- Rabid Transit teleporter floor sprites should animate correctly.
- Duke vending machines, trash cans, glass, gas bottles, and invisible gas
  bottle/explosive source sprites should behave like Duke.
- SW shurikens, sword, fists, hitscan weapons, and projectiles should damage
  Duke breakables and actors.
- Duke enemies/turrets/tripmines should move/attack/detonate with correct Duke
  sounds/projectile art.
- Duke map music and Duke actor/object sounds should play when the map domain is
  Duke.

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
