# VibeBuild32 JavaScript scripting (QuickJS)

Run the game with a user script:

```
vibebuild32 -gm duke -js myscript.js
```

The script runs in an embedded [QuickJS-ng](https://github.com/quickjs-ng/quickjs)
runtime (ES2023). It is loaded as an **ES module**, so `import`/`export` work, and
a CommonJS-style `require()` and an `fs` module are also provided.

Use `import { on, off, log } from 'vb'` for the event and logging helpers, or
import the default `vb` object. `console.log()` is equivalent to `vb.log()`.

VibeBuild32 options use `--long-name` for their full form and `-short-name` for
their shorthand: for example `--grp`/`-g`, `--game`/`-gm`, and
`--gen-ts`/`-gts`. Older one-dash full names remain accepted for compatibility.

## Auto-loaded projects and TypeScript

On startup VibeBuild32 scans the launch directory's `autojs/` folder. It loads
only `autojs/<project>/index.ts` or `autojs/<project>/index.js` (TypeScript wins
when both exist), so loose files directly in `autojs/` are intentionally ignored.
TypeScript is transpiled in-process; it does not require Node.js at game runtime.

Create a ready-to-open project with either spelling below:

```
vibebuild32 -gjs my-mod          # JavaScript: index.js + jsconfig.json
vibebuild32 -gjs                 # JavaScript with the next unused name, e.g. mod-1
vibebuild32 -gts my-mod          # TypeScript: index.ts + tsconfig.json
vibebuild32 -gts
```

The generated project includes `index.js` or `index.ts`, `vb.d.ts`, a matching
`jsconfig.json` or `tsconfig.json`, and `package.json`. `index.ts` (or `index.js`)
is the only auto-loaded entry point.
Open that project folder in VS Code for completion and static
errors for every built-in `vb`, `fs`, and `cvar` API. `npm install` is optional and
only needed for the generated `npm run check` editor/type-check command.

`--devtools` (or `-dt`) forces windowed mode and opens a lightweight docked
DevTools panel on the right of the game. It has console logs, expression
evaluation (`$event` is the last listener argument), event breakpoints, and a
global-scope inspector. For a precise source breakpoint, add `vb.breakpoint()` at
the line you want to stop at, then inspect expressions and press Resume. The
regular in-game OSD remains available with F12.

## Modules

```js
import fs from 'fs';            // ES module
import { on, spawn } from 'vb'; // named imports from the vb builtin
const fs2 = require('fs');      // CommonJS also works
const mine = require('./util.js');
```

Relative imports/requires resolve against the main script's directory.

## CON events

Every CON event (`EVENT_GAME`, `EVENT_SPAWN`, `EVENT_DISPLAYREST`, `EVENT_JUMP`,
`EVENT_FIRE`, `EVENT_KILLIT`, … all `EVENT_*`) is listenable, whether or not any
CON code handles it:

```js
vb.on('EVENT_SPAWN', e => { /* e = {sprite, player, dist} */ });
vb.on('EVENT_GAME',  e => { /* runs every tic, per actor */ });
```

The handler gets `{sprite, player, dist}`. To read or override the event's CON
return value, use the RETURN gamevar: `vb.getGamevar('RETURN', e.sprite, e.player)`
/ `vb.setGamevar('RETURN', v, e.sprite, e.player)`. The `EVENT_` prefix is
optional (`vb.on('SPAWN', …)` also works).

Generated projects declare every supported `EVENT_*` value (and its prefix-free
form) in `vb.d.ts`. VS Code therefore completes event names and narrows the
callback payload automatically: `actorSpawned` gets `{ sprite, owner, tile }`,
`chatMessage` gets `{ player, text }`, every CON event gets `{ sprite, player,
dist }`, and lifecycle events (`start`, `frame`, `shutdown`, `playerDied`) have
no payload.

The generated declaration is the complete contract for the embedded runtime:
the global `vb`, `cvar`, `console`, typed `require()`, and the `vb`, `fs`, and
`cvar` ES modules. Lookups such as `vb.getGamevar()`, `vb.define()`, and the
`getActor`/`getPlayer` family are correctly typed as `number | undefined`, since
the engine returns `undefined` for an unknown gamevar, define, or CON member.

## `vb` — the game API (also available as the global `vb`)

Events — `vb.on(name, handler)` / `vb.off(name)`:

| event          | handler arg                        | fires when                         |
|----------------|------------------------------------|------------------------------------|
| `start`        | –                                  | the script has loaded              |
| `shutdown`     | –                                  | the game is exiting                |
| `frame`        | –                                  | every rendered frame               |
| `playerDied`   | –                                  | the local player dies              |
| `actorSpawned` | `{sprite, owner, tile}`            | any actor/sprite is spawned        |
| `chatMessage`  | `{player, text}`                   | a message/quote is shown (incl. MP chat) |

Functions:

```js
vb.log(...args)                      // -> game console + log file
vb.drawText(id, x, y, text, pal=0, shade=0)  // HUD text at (0..320, 0..200); reuse id to update
vb.clearText(id)                     // remove one (or all if no id)
vb.spawn(tile, x=0, y=0, z=0, owner=-1) // -> spriteNum (x/y/z=0 spawns at the player)
vb.sendMessage(text)                 // send a multiplayer chat message (also shown locally)
vb.getGamevar(name, sprite=-1, player=-1) // read any CON gamevar
vb.setGamevar(name, value, sprite=-1, player=-1) // write any CON gamevar
vb.player()                          // -> { sprite, x, y, z }
```

The gamevar bridge exposes **every CON gamevar** by label, which is how scripts
read/write game state that CON scripts use.

## All CON data (members + defines)

The full CON member surface is reachable by name, using the same label tables CON
itself uses. Every getter has a matching setter (`setActor`, `setPlayer`, …):

```js
vb.getActor(sprite, member, parm2=0)     // sprite[]/actor[] members (e.g. 'extra','picnum','xvel')
vb.getPlayer(player, member, parm2=0)    // player[] members (e.g. 'curr_weapon','ammo_amount' w/ parm2)
vb.getInput(player, member)              // input[]
vb.getSector(sector, member)             // sector[] (e.g. 'floorz','lotag')
vb.getWall(wall, member)                 // wall[]
vb.getUserdef(member, parm2=0)           // userdef[]
vb.getProjectile(tile, member)           // per-tile projectile defaults
vb.getActiveProjectile(sprite, member)   // a live projectile's state
vb.getTileData(tile, member)             // tiledata[]
vb.define(name)                          // any CON define/constant -> e.g. vb.define('LIZTROOP') === 1680
```

`parm2` is the array index some members take (e.g. `ammo_amount[weapon]`).

### CON action commands

```js
vb.sound(num, sprite=-1)        // play a sound (positional if sprite given)
vb.shoot(sprite, tile)          // fire a projectile from a sprite -> new sprite
vb.addAmmo(player, weapon, amount)
vb.addWeapon(player, weapon, switchTo=1)
```

## `cvar` — console / cvar commands

Run any OSD console command or cvar assignment (also available as `vb.run`):

```js
cvar.run('map E1L3');      // warp; a leading '/' is tolerated: cvar.run('/map E1L3')
cvar.run('god');           // cheats, commands, anything the console accepts
cvar.set('r_maxfps', 60);  // convenience for run('r_maxfps 60')
```

## `fs` — files

```js
fs.readFile(path)          // -> string (throws if missing)
fs.writeFile(path, data)   // overwrite
fs.appendFile(path, data)  // append
fs.exists(path)            // -> bool
```

## Example

```js
import fs from 'fs';
let kills = 0;
vb.on('start', () => vb.log('mod loaded'));
vb.on('frame', () => vb.drawText(1, 20, 18, 'kills: ' + kills));
vb.on('playerDied', () => { fs.appendFile('/tmp/deaths.log', Date.now() + '\n'); });
vb.on('actorSpawned', e => { if (e.tile === 1680 /*LIZTROOP*/) kills++; });
```

## Adding more events / bindings

The C seam is small and easy to extend:

* **New event** — call `VBJS_FireSimple("name")`, `VBJS_FireActor("name", sprite, owner, tile)`,
  or `VBJS_FireChat("name", player, text)` from any hook point in the game code.
* **New binding** — add a `JSValue js_vb_foo(...)` in `vbjs.cpp`, register it in
  `vbjs_define_vb()`, and (if it touches the game) route it through the
  `vbjs_game::` seam implemented in `vbjs_duke.cpp`.

Files: `source/glue/vbjs.h` (API), `source/vibebuild32/src/vbjs.cpp` (QuickJS
runtime — game-agnostic), `source/vibebuild32/src/vbjs_duke.cpp` (Duke bindings).
Other games can provide the same API by implementing the `vbjs_game::` seam.
