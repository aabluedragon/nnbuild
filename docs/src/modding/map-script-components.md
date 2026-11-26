# Map script components

A map script component is a script file that the map editor attaches to an object in a map: a sprite, a sector or a wall. When the level runs, the game calls functions that the file exports.

Use this model when behavior belongs to a place or an object in a level: a door, a hazard, a pickup, a terminal.

> This is not the class-based component system. That one lives in code and uses `VBComponent` classes. See [Components](components.md). The two models are separate. A map script component is a module with exported functions, not a class.

## Requirements

- An editor project: a folder with a `vibebuild.project.json` file. See [Projects and stand-alone games](../editor/projects.md).
- **Modern Mode** turned on, for the project or for the map.
- Script files inside the project folder. Allowed extensions: `.js`, `.ts`, `.mjs`, `.mts`.
- A Duke runtime game.

## Attaching a script in the editor

1. Turn on Modern Mode: **File → Project → Enable Modern Mode**, and confirm the compatibility warning. A single map can override this in **File → Map settings...** with **Inherit**, **Off** or **On**.
2. Select a sprite, a sector or a wall.
3. In the **Inspector**, open **Script components** and click **Add Component**.
4. Pick a script that is already in the project. Or choose **New TypeScript component...** or **New JavaScript component...**, enter a project path such as `Scripts/Door.ts`, and click **Create and Attach**.

Each attached script is listed under **Script components** with a **Remove** button.

- One object can have several components.
- One script file can be attached to many objects.
- A map can hold up to 1,024 attachments.

## The template

**New JavaScript component...** creates this file:

```js
export function onAttach(component) {
    // Initialize component.state for this instance.
}

export function onUpdate(component, deltaSeconds) {
    // Update visuals each frame.
}

export function onFixedUpdate(component, deltaSeconds) {
    // Update gameplay each simulation tick.
}
```

**New TypeScript component...** creates the same file with types:

```ts
type ComponentContext = { kind: string; index: number; map: string; state: Record<string, unknown> };

export function onAttach(component: ComponentContext) {
    // Initialize component.state for this instance.
}

export function onUpdate(component: ComponentContext, deltaSeconds: number) {
    // Update visuals each frame.
}

export function onFixedUpdate(component: ComponentContext, deltaSeconds: number) {
    // Update gameplay each simulation tick.
}
```

The TypeScript template declares its own `ComponentContext` type, so it works in a project that has no declaration file.

### Types in `vb.d.ts`

The `vb.d.ts` file that the [mod generator](quick-start.md) writes also declares types for map script components:

```ts
declare type MapComponentContext = {
  kind: 'sprite' | 'sector' | 'wall';
  index: number;
  map: string;
  state: Record<string, any>;
};

declare type MapComponentModule = {
  onAttach?(component: MapComponentContext): void | (() => void);
  onUpdate?(component: MapComponentContext, deltaSeconds: number): void;
  onFixedUpdate?(component: MapComponentContext, deltaSeconds: number): void;
  onDetach?(component: MapComponentContext): void;
};
```

`MapComponentContext` is the object every hook receives. `MapComponentModule` describes the functions a component file may export.

The editor does not add `vb.d.ts` to a project. If you want these types in your code editor, copy `vb.d.ts` from a generated mod project into your project and include it in your editor's TypeScript settings. The game does not need the file.

## Hooks

A module can export any of these four functions. All are optional.

| Hook | Called | Use it for |
| --- | --- | --- |
| `onAttach(component)` | Once, when the level has loaded and the component becomes active. | Setting up `component.state`. It may return a cleanup function. |
| `onUpdate(component, deltaSeconds)` | On every drawn frame while the level is being played. Not in menus and not while paused. | Visuals and HUD. |
| `onFixedUpdate(component, deltaSeconds)` | Once per game tick, after the world has moved. Not while paused. | Game logic. |
| `onDetach(component)` | When the component ends: the level is unloaded, the game returns to the menu, a playtest stops, the game exits, or the component's sprite is removed. | Undoing what the component set up. |

Details:

- The game simulates 30 ticks per second. `deltaSeconds` of `onFixedUpdate` is always `1/30`.
- `deltaSeconds` of `onUpdate` is the real time since the previous `onUpdate`, at most `0.25`. It is `0` for the first call after the level starts and after a pause or a menu.
- If `onAttach` returns a function, the game calls it without arguments when the component ends, just before `onDetach`.
- Components end in the reverse order of their attachment.

## The context object

Every hook receives the same object for one attachment. The template calls it `component`.

| Field | Type | Meaning |
| --- | --- | --- |
| `kind` | string | `"sprite"`, `"sector"` or `"wall"`. |
| `index` | number | The index of the sprite, sector or wall in the map. Use it with `getActor`, `getSector` or `getWall`. |
| `map` | string | The path of the map file. In an editor playtest of a map that was never saved, the map's title. |
| `state` | object | An empty object, new for each attachment. Store this attachment's own values here. |

You may add your own fields to the context object. It stays the same object for all hooks of the attachment.

### Per-attachment state and shared state

A script file is loaded once per level. All attachments of that file share one module.

- Variables at the top of the file are shared by all attachments of the file.
- `component.state` belongs to one attachment.

This is useful. One file can serve several objects that work together through shared variables. The example below does this.

## What a component can use

- The global `vb` object, or `import { … } from 'vb'`. The whole [`vb` API](api-reference.md) is available.
- The other built-in modules and globals: `fs`, `cvar`, `fetch`, timers. See [Built-in modules](modules.md).
- `import` of other files in the project with relative paths, for example `import { clamp } from './lib/math.js'`. Write the full file name.
- TypeScript, within [the supported subset](project-layout.md#typescript-support-and-its-limits).

Rules:

- The module must finish loading immediately. Do not use `await` at the top level of the file. A module that does is skipped with a log message.
- Use `import`, not `require`, for your own files. See [`require`](project-layout.md#require).
- An error that a hook does not catch closes the game and shows the error.

## Reloading during a playtest

In the editor, press **F5** to playtest and **F5** again to return. Every playtest loads the component files again from disk, together with the files they import. Edit a script, save it, and start the next playtest to see the change. The editor does not need to be restarted.

Variables at the top of the file start fresh in every playtest and for every level.

Normal script mods (`autojs/…`) are different: they are loaded once when the game starts.

## When a sprite is removed

If the sprite of a component is removed during play, the component ends. Its cleanup function and `onDetach` run on the next update, and no further hooks are called.

A sprite that the game removes while the level is still being set up never gets its component. The log notes this.

## How attachments are stored

The editor saves attachments in a settings file next to the map, named `<map>.vbmap.json`:

```json
{
  "target": "inherit",
  "scripts": [
    { "kind": "sector", "script": "Scripts/Station.js", "index": 29 },
    { "kind": "sprite", "script": "Scripts/Station.js", "index": 15 }
  ],
  "version": 1
}
```

`script` is a path relative to the project folder. Modern Mode is stored as `"modern": true` in `vibebuild.project.json`, or as `"modern": true` or `false` in the map's settings file when the map overrides it.

## Playing a project with components

```sh
./vibebuild32 --project examples/ashline-relay
./vibebuild32 --editor --project examples/ashline-relay
```

Components of a project **folder** run without further options.

Components of an exported project **ZIP** do not run unless the player allows scripts:

```sh
./vibebuild32 --allow-project-scripts --project "/path/to/Ashline Relay.zip"
```

Without the option the level loads, but no component is attached. Only allow scripts from projects you trust.

## Example: Ashline Relay

The repository contains a small stand-alone game, `examples/ashline-relay`. Its whole mission logic is one file, `Scripts/Station.js`, attached to four sectors and four sprites. The player collects a power cell from a coolant pool, a seal opens, and the player transmits from a terminal.

### One file, several roles

The file is attached to different kinds of objects. `onAttach` finds out what each object is and stores a role in `state`:

```js
function identify(component) {
    if (component.kind === "sector") {
        switch (vb.getSector(component.index, "hitag")) {
            case 20201: return "seal";
            case 20202: return "coolant";
        }
    } else if (component.kind === "sprite") {
        switch (vb.getActor(component.index, "picnum")) {
            case 3860: return "relay";
            case 3850: return "cell";
            case 3872: return "indicator";
            case 3876: return "uplink";
        }
    }
    return "unknown";
}

export function onAttach(component) {
    const role = identify(component);
    component.state.role = role;
    component.state.elapsed = 0;
    component.state.phase = 0;
    component.state.z = component.kind === "sprite" ? vb.getActor(component.index, "z") : 0;
    if (role === "relay") vb.log("Ashline Relay online");
}
```

- A sector is recognized by its `hitag`, a number the level designer typed into the sector in the editor.
- A sprite is recognized by its tile number (`picnum`).
- The script uses the global `vb` object, so it needs no import.

This file is JavaScript. `switch` is fine here. In a `.ts` file use `if` / `else if`, because the game's TypeScript converter does not handle `case` labels.

### Shared mission state

The progress of the mission is kept in one object at the top of the file. All eight attachments see the same object:

```js
const mission = {
    cell: false,
    gateOpen: false,
    transmitted: false,
    message: "",
    messageTime: 0,
    coolantTimer: 0,
};
```

Values that belong to one object, such as its role and its original height, are in `component.state`.

### Game logic in `onFixedUpdate`

`onFixedUpdate` runs once per game tick for each attachment. It advances the attachment's own clock and then runs the code for its role:

```js
export function onFixedUpdate(component, dt) {
    component.state.elapsed += dt;
    switch (component.state.role) {
        case "relay": updateRelay(component, dt); break;
        case "coolant": updateCoolant(component); break;
        case "seal": updateSeal(component); break;
        case "cell": updateCell(component); break;
        case "indicator":
            if (mission.gateOpen && component.state.phase === 0) {
                vb.setActor(component.index, "picnum", 3878);
                component.state.phase = 1;
            }
            break;
        case "uplink": updateUplink(component); break;
    }
}
```

The power cell is a sprite. Until it is collected it floats up and down. When the player comes close, the script marks the cell as collected for everyone, hides the sprite, plays two sounds and shows a message:

```js
function playerNear(sprite, distance) {
    if (vb.getPlayer(0, "cursectnum") !== vb.getActor(sprite, "sectnum")) return false;
    const dx = vb.getActor(sprite, "x") - vb.getPlayer(0, "posx");
    const dy = vb.getActor(sprite, "y") - vb.getPlayer(0, "posy");
    return dx * dx + dy * dy < distance * distance;
}

function updateCell(component) {
    if (mission.cell) return;
    if (playerNear(component.index, 600)) {
        mission.cell = true;
        vb.setActor(component.index, "cstat", 32768);
        vb.sound(CELL_PICKUP);
        vb.sound(SEAL_RELEASE);
        signal("BLUE CELL ONLINE  /  UPLINK SEAL OPENING", 3.2);
        vb.log("Ashline Relay: blue cell recovered");
    } else {
        vb.setActor(component.index, "z", component.state.z + Math.round(620 * Math.sin(component.state.elapsed * 5)));
    }
}
```

- `cstat` `32768` makes a sprite invisible.
- `component.state.z` is the height saved in `onAttach`. The sprite moves around it.

The seal is a sector. Another attachment of the same file watches `mission.cell` and raises the sector's ceiling a little on every tick until it is open:

```js
function updateSeal(component) {
    if (!mission.cell || mission.gateOpen) return;
    const index = component.index;
    const current = vb.getSector(index, "ceilingz");
    const next = Math.max(OPEN_CEILING, current - 832);
    vb.setSector(index, "ceilingz", next);
    if (next === OPEN_CEILING) {
        mission.gateOpen = true;
        vb.log("Ashline Relay: uplink seal open");
    }
}
```

Heights get smaller as they go up, so subtracting raises the ceiling.

The other roles follow the same pattern:

- **coolant** (sector): cycles the floor picture with `setSector(index, "floorpicnum", …)`. When the player stands in the sector, it lowers the health of the player's sprite (`extra`) at most once every 0.55 seconds and plays a sound.
- **relay** (sprite): animates its tile and height. It also counts down the shared timers, because there is exactly one relay.

The uplink terminal is a sprite. It ends the mission:

```js
function updateUplink(component) {
    if (mission.transmitted || !mission.gateOpen || !playerNear(component.index, 860)) return;
    signal("USE THE UPLINK TERMINAL TO TRANSMIT", 0.35);
    if (!(vb.getInput(0, "bits") & USE)) return;
    mission.transmitted = true;
    vb.setActor(component.index, "picnum", 3875);
    vb.sound(TRANSMIT);
    signal("TRANSMISSION COMPLETE", 3);
    vb.setPlayer(0, "customexitsound", -1);
    vb.setUserdef("eog", 1);
    vb.setPlayer(0, "timebeforeexit", 90);
    vb.log("Ashline Relay: transmission complete");
}
```

- It does nothing until the seal is open and the player is close.
- `vb.getInput(0, "bits")` reads the keys player 0 holds. `USE` is `1 << 29`, the bit of the Use key.
- `vb.setUserdef("eog", 1)` marks this level as the end of the game. After the level the game returns to the menu.
- `vb.setPlayer(0, "timebeforeexit", 90)` starts the countdown that ends the level.

### The HUD in `onUpdate`

`onUpdate` runs every frame. Only the relay attachment draws, so the text is drawn once:

```js
export function onUpdate(component) {
    if (component.state.role !== "relay") return;
    const health = Math.max(0, vb.getActor(vb.player().sprite, "extra"));
    const ammo = vb.getPlayer(0, "ammo_amount", 1);
    const objective = mission.transmitted ? "RELAY LINK ESTABLISHED"
        : mission.cell ? "02  /  REACH THE UPLINK AND TRANSMIT"
        : "01  /  RECOVER THE BLUE CELL IN COOLANT";
    vb.drawText(710, 9, 23, objective, 0, -12);
    vb.drawText(711, 9, 187, "SUIT " + health + "    PULSE " + ammo, 0, -12);
    if (mission.messageTime > 0) vb.drawText(712, 9, 35, mission.message, 0, -14);
    else vb.clearText(712);
}
```

### Cleanup in `onDetach`

HUD text stays on screen until it is cleared. The relay removes its three lines when the level ends:

```js
export function onDetach(component) {
    if (component.state.role !== "relay") return;
    for (const id of [710, 711, 712]) vb.clearText(id);
}
```

### What to take from the example

- Decide the role of an object from data the level designer controls: a tag or a tile.
- Keep per-object values in `component.state` and shared progress at the top of the file.
- Put rules in `onFixedUpdate` and drawing in `onUpdate`.
- Clear HUD text and undo changes in `onDetach`.
- The example is single-player. It reads player `0` everywhere.

## Example: a cleanup function in TypeScript

This component makes a sector's floor blink. `onAttach` returns a function that restores the original brightness.

```ts
import { getSector, setSector } from 'vb';

export function onAttach(component: MapComponentContext) {
  if (component.kind !== 'sector') return;
  component.state.timer = 0;
  component.state.original = getSector(component.index, 'floorshade') ?? 0;
  // Runs when the component ends, just before onDetach.
  return () => setSector(component.index, 'floorshade', component.state.original);
}

export function onFixedUpdate(component: MapComponentContext, dt: number) {
  if (component.kind !== 'sector') return;
  component.state.timer += dt;
  const dark = Math.floor(component.state.timer * 2) % 2 === 1;
  setSector(component.index, 'floorshade', dark ? 24 : component.state.original);
}
```

`MapComponentContext` comes from `vb.d.ts`. The game removes the type when it loads the file, so the script also runs in a project without `vb.d.ts`.

The `kind` check matters. `component.index` is a sector number only when the script is attached to a sector.

## Limits

- Components run on the computer that plays the level. They are not synchronized between players. See [Keeping the game in sync](server-scripts.md#keeping-the-game-in-sync).
- The struct functions do not check indices. Use `component.index` only with the function that matches `component.kind`.
- HUD text ids are shared with all other scripts. Pick ids that are unlikely to collide.
