# VibeBuild32 JavaScript and TypeScript mods

VibeBuild32 embeds QuickJS and loads mods as ES modules. Mods can use either
TypeScript or JavaScript; TypeScript is transpiled by the game, so Node.js is
not needed when the game runs.

## Create and run a project

Use `--long-name` for a long option and `-short-name` for its short option.
The following forms create an editor-ready mod and exit without opening a game:

```sh
./vibebuild32 --gen-ts my-mod    # or: -gts my-mod
./vibebuild32 --gen-js my-mod    # or: -gjs my-mod
```

When no name is supplied, VibeBuild32 selects the next unused `mod-N` name.
The generated project contains exactly one auto-loaded entry point:

| mode | entry point | editor config |
|---|---|---|
| TypeScript | `autojs/<name>/index.ts` | `tsconfig.json` |
| JavaScript | `autojs/<name>/index.js` | `jsconfig.json` |

Each project also receives `vb.d.ts`, `package.json`, and `.vscode/` files for
launching and debugging. Open **the generated project folder** in VS Code.
`npm install` is optional; it is only needed for `npm run check`.

At launch, VibeBuild32 scans `autojs/<project>/` and loads only `index.ts` or
`index.js`; if both exist, `index.ts` wins. Loose files under `autojs/` are not
auto-loaded. Relative `import` and `require()` paths resolve from the importing
script.

```sh
./vibebuild32 --game duke --grp ~/games/duke3d/duke3d.grp
./vibebuild32 --game sw --grp '/Users/me/games/Shadow Warrior Complete/Sw.grp'
```

`--game`/`-gm` and `--grp`/`-g` accept their value immediately or after one or
more spaces (for example `-g/path/file.grp`, `-g /path/file.grp`, and
`--game duke`).

## API imports

Prefer imports; `on`, `off`, and `log` are not implicit globals.

```ts
import { on, log, drawText, define } from 'vb';
import fs from 'fs';
import cvar from 'cvar';

on('start', () => log('mod loaded'));
on('frame', () => drawText(1, 8, 8, 'Hello from TypeScript'));

// CommonJS remains supported.
const vb = require('vb');
```

`console.log()` uses the same logger as `log()`. Duke logs appear in the game
console/log; the debugger also shows them. In games using the shared generic
runtime, logs are written to the process log and debugger.

The generated `vb.d.ts` is the authoritative editor contract. It declares all
exported `vb` functions, component types, CON names/events, `fs`, `fs/promises`,
`dgram`, `net`, `cvar`, and `fetch` support. Do not add globals to work around a
missing import—add or correct the typed module export instead.

## Events and Duke bindings

```ts
import { on, getActor, setGamevar } from 'vb';

on('actorSpawned', event => {
  // event is { sprite, owner, tile }
});

on('EVENT_SPAWN', event => {
  // Every CON EVENT_* name is supported; EVENT_ is optional.
  setGamevar('RETURN', 0, event.sprite, event.player);
});
```

Lifecycle events include `start`, `shutdown`, `frame`, `gameStarted`,
`levelChanged`, `levelFinished`, `leftGame`, and `beforeQuit`. Menu events are
`menuOpened`, `menuClosed`, `menuChanged`, and `escapeMenuOpened`.

The Duke runtime exports `drawText`, actor/player/sector/wall accessors,
`define`, `spawn`/`A_Spawn`/`A_InsertSprite`, gamevars, menu controls, sound,
weapon helpers, components/prefabs, and session information. The generated
declarations list their exact names and parameters.

Shadow Warrior currently hosts the safe cross-game subset: module imports,
`on`, `off`, `log`, `breakpoint`, standard modules, `fetch`, and VS Code
debugging. Duke-only game-state APIs deliberately throw a JavaScript exception
in Shadow Warrior rather than dereferencing Duke state. An uncaught script
error is logged, displayed in a selectable/copyable in-game dialog, and closes
the game rather than continuing with a broken mod.

## Components and nodes

The Duke API supports Unity-style components. Components must subclass
`VBComponent`; instances can be attached to sprites or empty `VBNode`s.

```ts
import { VBComponent, createNode, createPrefab, spawnPrefab } from 'vb';

class Spin extends VBComponent {
  update(dt: number) { /* use this.node?.transform */ }
  onSpawn() {}
  onDestroy() {}
}

const node = createNode(Spin);
node.transform.position = { x: 0, y: 0, z: 0 };
node.transform.rotation = { x: 0, y: 0, z: 0 };
node.transform.scale = { x: 1, y: 1, z: 1 };

const prefab = createPrefab(1680, Spin);
const sprite = spawnPrefab(prefab);
```

Supported lifecycle methods are `awake`, `start`, `onSpawn`, `fixedUpdate`,
`update`, `lateUpdate`, `onEnable`, `onDisable`, and `onDestroy`.

## Built-in modules

The API intentionally resembles Node where it is implemented:

```ts
import fs from 'fs';
import promises from 'fs/promises';
import dgram from 'dgram';
import net from 'net';

fs.writeFileSync('note.txt', 'hello');
const text = await promises.readFile('note.txt');

const udp = dgram.createSocket('udp4');
udp.send('hello', 1234, '127.0.0.1');

const tcp = net.createConnection(1234, '127.0.0.1');
tcp.write('hello');

const response = await fetch('https://example.invalid/api', { method: 'GET' });
```

Consult `vb.d.ts` for the implemented overloads. This is an embedded runtime,
not a complete Node distribution: only declared APIs are supported. HTTP(S)
requests are available through `fetch`; Node's `http` and `https` module
objects are not currently exported.

## Debugging in VS Code

Generated `.vscode/launch.json` provides:

- **Launch VibeBuild32**: starts `vibebuild32 -dbg` and attaches automatically.
- **Attach to running VibeBuild32**: attaches to a game already started with
  `-dbg`/`--debug` or `-dt`/`--devtools`.

The generated pre-launch task starts the project-local DAP adapter on localhost
port **4714**. The game debugger bridge uses port **8501** by default; `-dbg`
with no value selects that port. Do not manually add game arguments in the
VS Code launch config: the adapter passes only `-dbg`.

Use ordinary red-dot source breakpoints in `index.ts` or `index.js`. Breakpoints
are installed before top-level module evaluation and `start` handlers run.
Hover values, Variables, Continue, and Stop are supported. Stop closes the
game and ends the VS Code debug session.

Use the optional debug-only header directive to add launch arguments from a
mod:

```ts
/// vbd.params="--game sw --grp '/Users/me/games/Shadow Warrior Complete/Sw.grp'"
```

`vbd.params` applies only to debug/devtools launches. Put it at the top of
`index.ts` or `index.js`. The launcher parses quoted paths and the normal option
spellings.

`--devtools`/`-dt` opens the separate DevTools window; `--debug`/`-dbg`,
`--devtools`/`-dt` imply `--skip-menu`/`-sm`. `--skip-menu`/`-sm` skips the
native game-selector GUI, while `--menu`/`-m` forces it.

## Extending the runtime

The lifecycle/API seam is in `source/glue/vbjs.h` and
`source/vibebuild32/src/vbjs.cpp`. Duke game-state bindings are implemented in
`source/vibebuild32/src/vbjs_duke.cpp`. When adding an API, implement the real
binding, export it from `vb`, and update the generated `vb.d.ts`; do not ship a
stub. Add an explicit safe implementation or a deliberate throwing boundary
for every other supported game.
