# TypeScript examples

Each example is a complete `index.ts`. Put it in `autojs/<name>/index.ts`, or generate a project first with `./vibebuild32 --gen-ts <name>` and replace the generated `index.ts`. See [Quick start](quick-start.md).

All examples keep to the TypeScript that the game can convert. See [TypeScript support and its limits](project-layout.md#typescript-support-and-its-limits).

Examples 5, 9 and 10 use only `on`, `log` and built-in modules. They also run in Shadow Warrior. The other examples need a Duke runtime game.

All examples pass the type check (`npm run check`) against the generated `vb.d.ts`.

## 1. Hello, HUD

Shows a fixed line of text and a frame counter that updates every 30 frames.

```ts
import { on, drawText } from 'vb';

let frames = 0;

on('start', () => {
  drawText(1, 8, 8, 'Hello from my mod');
});

on('frame', () => {
  frames++;
  if (frames % 30 === 0) drawText(2, 8, 18, `frames: ${frames}`);
});
```

Text stays on screen until it is replaced or cleared, so the first line is drawn only once.

## 2. Reacting to actor spawns

Counts every trooper the engine spawns and shows the count. This includes troopers that are part of a map when a level loads.

```ts
import { on, define, drawText, log } from 'vb';

let trooper: number | undefined;
let seen = 0;

on('start', () => {
  trooper = define('LIZTROOP');
  if (trooper === undefined) log('LIZTROOP is not defined by this game');
});

on('actorSpawned', (e) => {
  if (e.tile !== trooper) return;
  seen++;
  drawText(10, 8, 8, `Troopers spawned: ${seen}`);
});
```

`define('LIZTROOP')` asks the game for the tile number behind the name. The name comes from Duke Nukem 3D's CON files. In another game the lookup returns `undefined` and the mod only logs a message.

## 3. A custom chat command

Adds two commands that players type into chat on a server: `!players` lists who is online, `!roll 20` rolls a die.

```ts
import { registerCommand, session, log } from 'vb';

try {
  registerCommand('players', () => {
    const names = session().players.map((p) => p.name).join(', ');
    return `Online: ${names}`;
  });

  registerCommand('roll', ({ args, reply }) => {
    const sides = Math.max(2, parseInt(args, 10) || 6);
    reply(`You rolled ${1 + Math.floor(Math.random() * sides)} (d${sides})`);
  });
} catch (err) {
  log(`chat commands are not available: ${err}`);
}
```

`registerCommand` throws when the game is not a multiplayer server. The `try`/`catch` keeps the mod harmless in single-player. A handler can answer by returning a string or by calling `reply`. See [Server scripts](server-scripts.md#chat-commands).

## 4. A timed announcement that changes the map

Every ten minutes the host announces a map change and switches to the next map of the rotation one minute later.

```ts
import { sendMessage, run, session } from 'vb';

const minute = 60_000;

function isHost() {
  const s = session();
  return s.players.length > 1 && s.localPlayer === s.hostPlayer;
}

setInterval(() => {
  if (!isHost()) return;
  sendMessage('The map changes in one minute.');
  setTimeout(() => run('nextmap'), minute);
}, 10 * minute);
```

`run('nextmap')` runs the host's `nextmap` console command, which changes the map for all players together. The `isHost()` check makes the timer do nothing on other computers.

## 5. Reading and writing a file

Counts how often the mod has been started and keeps the number in a text file.

```ts
import fs from 'fs';
import { on, log } from 'vb';

const file = 'my-mod-launches.txt';

on('start', () => {
  let launches = 0;
  if (fs.existsSync(file)) {
    launches = parseInt(fs.readFileSync(file, 'utf8') as string, 10) || 0;
  }
  launches++;
  fs.writeFileSync(file, String(launches));
  log(`This mod has been started ${launches} time(s)`);
});
```

The `'utf8'` argument makes `readFileSync` return a string. Without it you get bytes. The file is created in the folder the game runs in.

## 6. Reading and setting a gamevar

Halves the gravity that pulls sprites down, and logs the old and the new value.

```ts
import { on, getGamevar, setGamevar, log } from 'vb';

const LOW_GRAVITY = 88;

function applyGravity() {
  const before = getGamevar('gravitationalconstant');
  if (before === undefined) return;
  setGamevar('gravitationalconstant', LOW_GRAVITY);
  log(`gravity ${before} -> ${getGamevar('gravitationalconstant')}`);
}

on('start', applyGravity);
on('levelChanged', applyGravity);
```

`gravitationalconstant` is a gamevar that the game itself provides. Its value is normally `176`. The function is called from `start` and from `levelChanged`, so it also covers a game that starts directly in a level. It sets a fixed value, so calling it more than once does no harm. This mod changes the game world. In multiplayer every player needs it.

## 7. A class-based component

Shows a frames-per-second counter. The logic lives in a component on a node.

```ts
import { VBComponent, createNode, drawText, clearText } from 'vb';

class FpsCounter extends VBComponent {
  frames = 0;
  elapsed = 0;

  update(dt: number) {
    this.frames++;
    this.elapsed += dt;
    if (this.elapsed < 1) return;
    drawText(20, 8, 38, `${this.frames} fps`);
    this.frames = 0;
    this.elapsed = 0;
  }

  onDestroy() {
    clearText(20);
  }
}

const hud = createNode(FpsCounter);
hud.name = 'fps counter';
```

`update` runs once per drawn frame with the time since the last frame. After one second the component shows how many frames it counted. See [Components](components.md).

## 8. A CON event listener

Stops a player from jumping while that player's health is below 25.

```ts
import { on, getActor, setGamevar } from 'vb';

on('EVENT_JUMP', (e) => {
  const health = getActor(e.sprite, 'extra') ?? 0;
  if (health < 25) setGamevar('RETURN', 1, e.sprite, e.player);
});
```

The engine fires `EVENT_JUMP` when a player starts a jump. `e.sprite` is that player's sprite and `extra` is its health. Setting the gamevar `RETURN` to a non-zero value tells the engine to cancel the jump. See [CON events](events.md#con-events). This mod changes the game world. In multiplayer every player needs it.

## 9. A fetch call

Downloads a web page when the mod starts and logs the status code and the length of the body.

```ts
import { on, log } from 'vb';

on('start', async () => {
  try {
    const response = await fetch('https://example.com/');
    const body = await response.text();
    log(`HTTP ${response.status}, ${body.length} characters`);
  } catch (err) {
    log(`request failed: ${err}`);
  }
});
```

The `try`/`catch` is inside the `async` function on purpose. A rejected promise that nothing handles closes the game. See [Promises and errors](modules.md#promises-and-errors).

## 10. A UDP socket

Opens a UDP socket on the local computer, sends itself one datagram and logs what arrives.

```ts
import dgram from 'dgram';
import { log } from 'vb';

const PORT = 40123;
const socket = dgram.createSocket('udp4');

socket.on('message', (message, rinfo) => {
  const text = String.fromCharCode(...message);
  log(`UDP ${rinfo.address}:${rinfo.port} says: ${text}`);
});

socket.bind(PORT, '127.0.0.1', () => {
  socket.send('ping', PORT, '127.0.0.1');
});
```

`message` is a `Uint8Array`. `String.fromCharCode(...message)` turns plain ASCII bytes into a string. The socket is stored in a top-level constant so that it stays open. To receive from other computers, bind to `'0.0.0.0'` instead of `'127.0.0.1'`. See [dgram](modules.md#dgram).

## 11. A player list on the HUD

Shows the index and name of up to eight players while a level is being played.

```ts
import { on, session, drawText, clearText } from 'vb';

const FIRST_ID = 100;
const MAX_LINES = 8;

on('frame', () => {
  const s = session();
  for (let i = 0; i < MAX_LINES; i++) clearText(FIRST_ID + i);
  if (s.started === false) return;
  s.players.slice(0, MAX_LINES).forEach((p, i) => {
    const tag = p.isHost ? ' (host)' : '';
    drawText(FIRST_ID + i, 8, 50 + i * 9, `${p.index}: ${p.name}${tag}`);
  });
});
```

`session()` returns a fresh snapshot each time it is called. The example clears its eight text ids every frame and draws the current players again, so the list follows players who join and leave.
