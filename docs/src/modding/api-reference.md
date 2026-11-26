# API reference: the `nnb` module

The `nnb` module is the link between a script and the game. This page lists every export, grouped by task.

## Using the module

```ts
import { on, log, drawText } from 'nnb';   // named imports
```

```ts
import nnb from 'nnb';                     // default import: one object with everything
nnb.log('hello');
```

```js
const nnb = require('nnb');                // CommonJS style
```

A global object named `nnb` with the same functions also exists, so short scripts need no import at all.

In TypeScript files use the first or second form. `import * as nnb from 'nnb'` does not work there. See [TypeScript support and its limits](project-layout.md#typescript-support-and-its-limits).

The module exports functions and three classes. It exports no constants. Use [`define()`](#gamevars-and-con-data) to look up the game's named constants, such as tile numbers.

## Availability

Functions marked **Duke** work only in the Duke runtime games: Duke Nukem 3D, World Tour, Ion Fury, NAM, NAPALM and WWII GI. In Shadow Warrior these functions exist but throw `ReferenceError: This nnb API is not supported by the active game`.

Only `on`, `off`, `log` and `breakpoint` work in every game that runs scripts.

## General rules

- **Numbers are integers.** Every numeric argument is converted to a 32-bit integer. Fractions are cut off. A value that is not a number becomes `0`.
- **Unknown names give `undefined`.** A getter returns `undefined` when the member, gamevar or constant name does not exist. A setter with an unknown name does nothing.
- **Indices are not checked.** `getActor`, `getSector`, `getWall`, `getPlayer` and the other struct functions pass your index straight to the engine. An index that is out of range, such as `-1`, reads or writes memory that does not belong to the object. The game can crash. Check an index before you use it.
- **Too few arguments throw.** Most functions throw a `TypeError` when a required argument is missing.
- **Changes are local.** A script changes the game only on the computer it runs on. See [Keeping the game in sync](server-scripts.md#keeping-the-game-in-sync).

## Logging

| Function | Returns | Description |
| --- | --- | --- |
| `log(...values)` | `undefined` | Writes one line to the game log and console, prefixed with `[js]`. |

- Each value is converted to a string. The values are joined with single spaces.
- Objects print as `[object Object]`. Use `JSON.stringify(value)` to see their content.
- A line is cut off after 1023 characters.
- `console.log`, `console.info`, `console.warn` and `console.error` are the same function.

## Events

| Function | Returns | Description |
| --- | --- | --- |
| `on(name, handler)` | `undefined` | Calls `handler` every time the event `name` fires. |
| `off(name)` | `undefined` | Removes **all** handlers of the event `name`. |

- `name` is a string. The event names are listed in [Events](events.md).
- `handler` is a function. Most events pass one event object. Some pass nothing.
- You can register several handlers for one event. They run in the order you registered them.
- For CON events the `EVENT_` prefix is optional and the name is not case-sensitive. `on('jump', …)` and `on('EVENT_JUMP', …)` are the same.
- `on` throws a `TypeError` if `handler` is not a function.
- `off` cannot remove a single handler. It also removes handlers that other mods and the [component system](components.md) registered for that event. Avoid `off('frame')` and `off('actorDestroyed')` if you use components.

```ts
import { on, log } from 'nnb';

on('actorSpawned', (e) => {
  log(`sprite ${e.sprite} spawned with tile ${e.tile}`);
});
```

## HUD text

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `drawText(id, x, y, text, pal?, shade?)` | `undefined` | Shows a line of text on the screen, or replaces the line with the same `id`. |
| `clearText(id?)` | `undefined` | Removes the line with this `id`. Without an argument it removes all lines. |

- `id`: any integer you choose. It names the line. Calling `drawText` again with the same `id` replaces the text.
- `x`, `y`: position of the text in a 320 × 200 screen. `0, 0` is the top-left corner.
- `text`: the string to show. At most 191 characters are kept.
- `pal`: palette number that colors the text. Default `0`.
- `shade`: brightness. `0` is normal, negative values are brighter, positive values are darker. Default `0`.

A line stays on screen until you clear or replace it. You do not need to draw it again every frame. Lines are shown during play and on the title and menu screens.

There are 64 lines in total, shared by all loaded scripts. When all 64 are in use, a new `id` is ignored. Pick ids that other mods are unlikely to use.

## Spawning and actors

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `spawn(tile, x?, y?, z?, owner?)` | sprite index, or `-1` | Creates an actor of tile `tile` and initializes it like a normal game actor. |
| `A_Spawn(spriteNum, tileNum)` | sprite index | The engine's spawn routine. Creates an actor of tile `tileNum` at the position of the existing sprite `spriteNum`, which becomes its owner. |
| `A_InsertSprite(whatsect, s_x, s_y, s_z, s_pn, s_s, s_xr, s_yr, s_a, s_ve, s_zv, s_ow, s_ss)` | sprite index | The engine's low-level insert. Creates a raw sprite without running actor setup. |
| `getActor(sprite, member, parm2?)` | number, or `undefined` | Reads a member of a sprite. |
| `setActor(sprite, member, value, parm2?)` | `undefined` | Writes a member of a sprite. |
| `shoot(sprite, tile)` | number | Makes the sprite fire the projectile or weapon with tile `tile`. Returns the engine's result, or `-1` if `sprite` is out of range. |
| `getProjectile(tile, member)` | number, or `undefined` | Reads a setting of the projectile type defined for `tile`. |
| `setProjectile(tile, member, value)` | `undefined` | Writes a setting of the projectile type defined for `tile`. |
| `getActiveProjectile(sprite, member)` | number, or `undefined` | Reads a setting of one flying projectile sprite. |
| `setActiveProjectile(sprite, member, value)` | `undefined` | Writes a setting of one flying projectile sprite. |
| `getTileData(tile, member)` | number, or `undefined` | Reads data of a tile, such as its size. |
| `setTileData(tile, member, value)` | `undefined` | Writes data of a tile. |

### `spawn`

- `tile`: tile number. An invalid number returns `-1`.
- `x`, `y`, `z`: map position. If all three are `0` or missing, the actor appears at the local player's position.
- `owner`: optional sprite index. Its sector is used as the starting point to find the sector of `x`, `y`. Without it the local player's sector is used.
- Returns `-1` when no sector can be found, for example when no game is running.
- The new actor fires [`actorSpawned`](events.md#actor-events).

In multiplayer do not call `spawn`. Use the synchronized console command instead: `run('spawn <tile> <player>')`. See [Server scripts](server-scripts.md#keeping-the-game-in-sync).

### `A_Spawn` and `A_InsertSprite`

These are the engine's own functions with their original names and argument order. They are for people who know the engine.

- `A_Spawn(spriteNum, tileNum)`: when `spriteNum` is `-1`, the second argument is not a tile. It is the index of an existing sprite that is then set up as an actor.
- `A_InsertSprite` arguments, in order: sector, x, y, z, tile, shade, x-repeat, y-repeat, angle, velocity, z-velocity, owner sprite, status list number. All 13 are required.
- If the map has no free sprite left, the game exits.

### Actor members

`member` is the name CON uses for the field. Names are not case-sensitive. Common ones:

`x`, `y`, `z`, `ang`, `sectnum`, `statnum`, `picnum` (tile), `cstat`, `shade`, `pal`, `xrepeat`, `yrepeat`, `xvel`, `yvel`, `zvel`, `owner`, `lotag`, `hitag`, `extra` (health for most actors)

`parm2` is an extra index that a few members need. Most members ignore it.

Tile data members: `xsize`, `ysize`, `animframes`, `xoffset`, `yoffset`, `animspeed`, `animtype`, `gameflags`.

## Players

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `player()` | `{ sprite, x, y, z }` | The local player's sprite index and position. `sprite` is `-1` and the position is `0` when there is no player. |
| `getPlayer(player, member, parm2?)` | number, or `undefined` | Reads a member of a player. |
| `setPlayer(player, member, value, parm2?)` | `undefined` | Writes a member of a player. |
| `getInput(player, member)` | number, or `undefined` | Reads the player's current input. |
| `setInput(player, member, value)` | `undefined` | Writes the player's current input. |
| `addAmmo(player, weapon, amount)` | `undefined` | Adds ammo for weapon number `weapon`, up to the weapon's maximum. |
| `addWeapon(player, weapon, switchTo?)` | `undefined` | Gives the weapon. `switchTo` defaults to `1`: the player switches to it. Pass `0` to keep the current weapon. |

- `player` is the player index. In a single-player game it is `0`.
- `addAmmo` and `addWeapon` do nothing when the player index is invalid. They do not check the weapon number.
- Common player members: `posx`, `posy`, `posz`, `ang`, `cursectnum`, `i` (the player's sprite index), `curr_weapon`, `ammo_amount` (needs `parm2` = weapon number), `gotweapon` (needs `parm2` = weapon number), `on_ground`, `dead_flag`, `max_player_health`, `timebeforeexit`.
- Input members: `avel`, `q16avel`, `horz`, `q16horz`, `fvel`, `svel`, `bits`, `extbits`.

```ts
import { getPlayer, player, getActor } from 'nnb';

const me = player();
if (me.sprite >= 0) {
  const health = getActor(me.sprite, 'extra');
  const ammoOfWeapon1 = getPlayer(0, 'ammo_amount', 1);
}
```

## Sectors and walls

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `getSector(sector, member)` | number, or `undefined` | Reads a member of a sector. |
| `setSector(sector, member, value)` | `undefined` | Writes a member of a sector. |
| `getWall(wall, member)` | number, or `undefined` | Reads a member of a wall. |
| `setWall(wall, member, value)` | `undefined` | Writes a member of a wall. |

Common sector members: `ceilingz`, `floorz`, `ceilingpicnum`, `floorpicnum`, `ceilingshade`, `floorshade`, `ceilingpal`, `floorpal`, `ceilingstat`, `floorstat`, `visibility`, `lotag`, `hitag`, `extra`, `wallptr`, `wallnum`.

Common wall members: `x`, `y`, `point2`, `nextwall`, `nextsector`, `cstat`, `picnum`, `overpicnum`, `shade`, `pal`, `xrepeat`, `yrepeat`, `xpanning`, `ypanning`, `lotag`, `hitag`, `extra`.

Heights (`z` values) get smaller as you go up.

## Gamevars and CON data

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `getGamevar(name, sprite?, player?)` | number, or `undefined` | Reads a CON gamevar. `undefined` if no gamevar has this name. |
| `setGamevar(name, value, sprite?, player?)` | `undefined` | Writes a CON gamevar. Does nothing if no gamevar has this name. |
| `define(name)` | number, or `undefined` | Returns the value of a CON constant created with `define`, such as a tile name. |
| `getUserdef(member, parm2?)` | number, or `undefined` | Reads a member of the game's global settings structure (`userdef` in CON). |
| `setUserdef(member, value, parm2?)` | `undefined` | Writes a member of the game's global settings structure. |

- Gamevar names and `define` names are case-sensitive.
- `sprite` and `player` select which copy to use for per-actor and per-player gamevars. For global gamevars leave them out. Both default to `-1`.
- `getUserdef` and `setUserdef` take the member name first. They have no index argument.
- Common userdef members: `god`, `multimode`, `level_number`, `volume_number`, `player_skill`, `eog`, `pause_on`.

```ts
import { define, getGamevar, setGamevar, getUserdef } from 'nnb';

const trooper = define('LIZTROOP');             // a tile number in Duke Nukem 3D
const gravity = getGamevar('gravitationalconstant');
setGamevar('gravitationalconstant', 88);
const players = getUserdef('multimode');
```

## Sounds and weapons

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `sound(num, sprite?)` | `undefined` | Plays sound number `num`. With a valid `sprite` index the sound comes from that sprite. Without it the sound is not positioned. |
| `shoot(sprite, tile)` | number | See [Spawning and actors](#spawning-and-actors). |
| `addAmmo(player, weapon, amount)` | `undefined` | See [Players](#players). |
| `addWeapon(player, weapon, switchTo?)` | `undefined` | See [Players](#players). |

Sound numbers are the ones the game's CON files define. If the CON files give a sound a name, `define('<NAME>')` returns its number.

## Menus

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `setMenuText(original, replacement)` | `undefined` | Whenever the menu draws a string that is exactly `original`, it draws `replacement` instead. |
| `setMenuBackground(tile)` | `undefined` | Uses tile `tile` as the full-screen picture behind the menu when no game is shown. Pass `null` to restore the default. |
| `currentMenu()` | number | The id of the menu page that is current. |
| `openMenu(menuId)` | `undefined` | Switches the menu system to the page `menuId`. |
| `setMenuRoute(menuId, itemIndex, destination)` | `undefined` | Redirects a menu item to another page. |
| `menuSelection()` | number | The index of the highlighted menu item. |
| `setMenuSelection(index)` | `undefined` | Highlights the menu item with this index. |

- Menu ids are the game's internal page numbers. Page `0` is the main menu.
- `setMenuText` affects text drawn with the game's large menu font. Replacements stay until the game exits.
- `setMenuBackground` ignores tiles that do not exist. A CON handler for `EVENT_GETMENUTILE` can still override your tile.
- `setMenuRoute(menuId, itemIndex, destination)`: when the player is on page `menuId`, has item `itemIndex` highlighted, and the game changes page, the game goes to `destination` instead.
  - `itemIndex` `-1` matches every item of that page that has no route of its own.
  - `destination` `-1` keeps the current page. `-2` closes the menu.
  - `destination` `null` removes the route.
- Use the [menu events](events.md#menu-events) to learn page ids: log `e.menu` from a `menuChanged` handler.

## Session and multiplayer

**Duke.**

| Function | Returns | Description |
| --- | --- | --- |
| `session()` | object | A snapshot of the current game session. |

The returned object is a copy. It does not change later. Call `session()` again for fresh values.

| Field | Type | Meaning |
| --- | --- | --- |
| `started` | boolean | `true` while a level is being played. |
| `skill` | number | Skill level, starting at `1`. |
| `episode` | number | Episode number, starting at `1`. |
| `level` | number | Level number inside the episode, starting at `1`. |
| `episodeName` | string or `undefined` | Name of the episode. |
| `levelName` | string or `undefined` | Name of the level. |
| `userMap` | string or `undefined` | File name of the user map, when one is loaded. |
| `localPlayer` | number | Index of the player on this computer, or `-1`. |
| `hostPlayer` | number or `undefined` | Index of the hosting player. |
| `hostName` | string or `undefined` | Name of the hosting player. |
| `serverAddress` | string or `undefined` | On a client that joined through an address: that address. `undefined` on the host. |
| `players` | array | One entry per player slot. |

Each entry of `players`:

| Field | Type | Meaning |
| --- | --- | --- |
| `index` | number | Player index. Use it with `getPlayer`, `addAmmo` and similar functions. |
| `name` | string | Player name. |
| `sprite` | number | The player's sprite index, or `-1`. |
| `isLocal` | boolean | `true` for the player on this computer. |
| `isHost` | boolean | `true` for the hosting player. |

## Chat and server commands

| Function | Returns | Description |
| --- | --- | --- |
| `sendMessage(text)` | `undefined` | **Duke.** Sends a chat message. |
| `registerCommand(name, handler)` | `undefined` | **Duke, server only.** Registers a `!name` chat command. |

### `sendMessage`

- On the host of a multiplayer game the message goes to every player and is shown as `Server: <text>`.
- Otherwise it is sent as a normal chat line from the local player: `<name>: <text>`.
- An empty string is ignored.
- The message does not fire [`chatMessage`](events.md#chat-events) on the computer that calls `sendMessage`. The other computers receive it as a `chatMessage`.
- Be careful with a handler that answers every `chatMessage` with `sendMessage`. If two computers run such a script, they keep answering each other.

### `registerCommand`

- It throws a `TypeError` unless this computer is hosting a multiplayer game in master/slave mode. It throws in single-player games and on clients.
- `name` may start with `!`. It may contain only letters, digits, `_` and `-`, at most 32 characters. It is not case-sensitive.
- Registering the same name again replaces the earlier handler.
- When a player types `!name some text` in chat, the handler receives one object. `nnb.d.ts` calls its type `CommandRequest`:

| Field | Type | Meaning |
| --- | --- | --- |
| `player` | number | Index of the player who sent the command. |
| `command` | string | The command name, in lower case, without `!`. |
| `args` | string | Everything after the command name, with leading spaces removed. One string, not split. |
| `reply(text)` | function | Sends `text` to that player only. |

- If the handler returns a string, the string is sent to that player, like `reply`.

See [Server scripts](server-scripts.md) for details and a complete example.

## Console

| Function | Returns | Description |
| --- | --- | --- |
| `run(command)` | `undefined` | **Duke.** Runs a console command, for example `run('map e1l2')`. |

- A leading `/` is allowed and ignored.
- The command does not run immediately. It is put in a queue and runs after your code returns, the next time the game loop passes. This works during play and at the title menu.
- You cannot read a command's output from the script.
- `run` is the same as `cvar.run` from the [`cvar` module](modules.md#cvar).

## Debugging

| Function | Returns | Description |
| --- | --- | --- |
| `breakpoint()` | `undefined` | When DevTools is enabled, pauses script activity after the current function returns. Does nothing otherwise. |

See [Debugging with VS Code](debugging-vscode.md#the-breakpoint-function).

## Components, nodes and prefabs

**Duke.** These exports are explained in [Components](components.md). This is the summary.

| Export | Returns | Description |
| --- | --- | --- |
| `NNBComponent` | class | Base class for your components. |
| `NNBNode` | class | An empty object that can hold components. `new NNBNode()` creates one. |
| `NNBPrefab` | class | A tile plus a list of component classes. Create it with `createPrefab`. |
| `attach(entity, component)` | the component | Attaches a component to a sprite index or an `NNBNode`. `component` is a class or an instance. |
| `detach(entity, component)` | boolean | Removes one component. `true` if it was attached. |
| `getComponent(entity, type)` | component, or `undefined` | The first attached component that is an instance of `type`. |
| `getComponents(entity, type?)` | array | All attached components, or only those of `type`. |
| `setComponentEnabled(component, enabled)` | `undefined` | Turns a component on or off. Same as assigning `component.enabled`. |
| `createNode(...components)` | `NNBNode` | Creates a node and attaches the given components. |
| `destroyNode(node)` | boolean | Destroys a node and all its components. `false` if it was already destroyed. |
| `createPrefab(tile, ...components)` | `NNBPrefab` | Describes an actor tile together with component classes. |
| `spawnPrefab(prefab, x?, y?, z?, owner?)` | sprite index, or `-1` | Calls `spawn` for the prefab's tile and attaches its components to the new sprite. |

## Alphabetical list

All 58 exports:

`A_InsertSprite`, `A_Spawn`, `addAmmo`, `addWeapon`, `attach`, `breakpoint`, `clearText`, `createNode`, `createPrefab`, `currentMenu`, `define`, `destroyNode`, `detach`, `drawText`, `getActiveProjectile`, `getActor`, `getComponent`, `getComponents`, `getGamevar`, `getInput`, `getPlayer`, `getProjectile`, `getSector`, `getTileData`, `getUserdef`, `getWall`, `log`, `menuSelection`, `off`, `on`, `openMenu`, `player`, `registerCommand`, `run`, `sendMessage`, `session`, `setActiveProjectile`, `setActor`, `setComponentEnabled`, `setGamevar`, `setInput`, `setMenuBackground`, `setMenuRoute`, `setMenuSelection`, `setMenuText`, `setPlayer`, `setProjectile`, `setSector`, `setTileData`, `setUserdef`, `setWall`, `shoot`, `sound`, `spawn`, `spawnPrefab`, `NNBComponent`, `NNBNode`, `NNBPrefab`
