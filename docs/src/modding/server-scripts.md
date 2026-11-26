# Server scripts

A server script is a normal script mod that runs on the computer hosting a multiplayer game. It can run on a player's own machine when that player hosts, or on a dedicated server without a screen.

On a server a script can do things that make no sense elsewhere: answer `!commands` that players type into chat, make announcements, and change maps on a schedule.

Everything on this page needs the **Duke runtime**.

## Starting a server with a script

Give the script on the command line, like any other script:

```sh
./nnbuild --headless --startnow --js ./server.ts -net /p23513 /n0:8
```

- `--headless` starts a dedicated server with no window, graphics or sound.
- `--startnow` begins the match immediately and keeps the other slots open.
- `--js ./server.ts` loads the script. A bare path (`./server.ts`) also works if it comes before `-net`.
- `-net /p23513 /n0:8` hosts on port 23513 in master/slave mode with 8 slots.

A script in `autojs/<name>/` is loaded as well. See [Dedicated server](../multiplayer/dedicated-server.md) for the server options and [Project layout](project-layout.md#how-a-script-gets-loaded) for the ways to load scripts.

## What is different on a headless server

- The script runtime runs normally. The `frame` event, timers, `fs`, `dgram`, `net`, `fetch` and promises all work.
- Nothing is drawn. `drawText` and the menu functions have no visible effect.
- `sound` plays nothing.
- `log` writes to the server's log.
- The server starts directly in a level, before scripts are loaded. Your script does not get `gameStarted` or `levelChanged` for the first map. See [Events you can miss](events.md#events-you-can-miss).
- A script runs only on the computer that loaded it. It is not sent to the players.

## Chat commands

A player types a line that starts with `!` into the normal chat box, for example `!help`. In a master/slave multiplayer game such a line is not shown as chat. It is sent to the server and handled there.

Register a handler with `registerCommand`:

```ts
import { registerCommand } from 'nnb';

registerCommand('help', ({ reply }) => {
  reply('Commands: !help, !rules');
});

registerCommand('rules', () => 'Play the objective and keep chat friendly.');
```

The handler receives one object. In `nnb.d.ts` its type is named `CommandRequest`:

| Field | Type | Meaning |
| --- | --- | --- |
| `player` | number | Index of the player who typed the command. |
| `command` | string | The command name in lower case, without the `!`. |
| `args` | string | The rest of the line, with leading spaces removed. It is one string. Split it yourself. |
| `reply(text)` | function | Sends `text` to that player only. |

- Returning a string from the handler is the same as calling `reply` with it.
- A reply appears on the player's screen as `Server: <text>`. Other players do not see the command or the reply.
- `reply` can be called later, for example from a timer. It is ignored if the player has left in the meantime.

Rules and limits:

- A command name may contain letters, digits, `_` and `-`, up to 32 characters. Case does not matter: `!Help` runs the `help` handler.
- A command line can be at most 120 characters long.
- If no handler is registered for a command, the player gets `Unknown server command.`
- The host's own `!` lines are handled the same way.
- In a single-player game, and in peer-to-peer mode, a line starting with `!` is ordinary chat.

### `registerCommand` only works on the server

`registerCommand` throws an error when the computer is not hosting a multiplayer game in master/slave mode. An error that nothing catches closes the game. If your script may also be loaded in single-player, on a client, or in a peer-to-peer game, catch the error:

```ts
import { registerCommand, log } from 'nnb';

try {
  registerCommand('rules', () => 'Play the objective and keep chat friendly.');
} catch (err) {
  log(`chat commands are off: ${err}`);
}
```

If the first `registerCommand` succeeds, the computer is the server. The complete example below uses this to decide whether to start its timers.

## Messages to players

`sendMessage(text)` on the host sends `text` to every player. It is shown as `Server: <text>`.

```ts
import { sendMessage } from 'nnb';

sendMessage('Welcome. Type !help for commands.');
```

To talk to one player, use `reply` inside a command handler.

`sendMessage` does not fire the `chatMessage` event on the server itself. The players' computers receive the message as a `chatMessage` from the host. See [Chat events](events.md#chat-events).

## Timers

`setTimeout` and `setInterval` work on a server as everywhere else. They use real time and keep running across map changes.

```ts
import { sendMessage } from 'nnb';

const minute = 60_000;

setInterval(() => {
  sendMessage('Remember: type !rules for the server rules.');
}, 15 * minute);
```

See [Timers](modules.md#timers).

## Console commands from a script

`run(command)` runs a line as if the server operator had typed it into the console. `cvar.run` is the same function.

```ts
import { run } from 'nnb';

run('nextmap');           // advance to the next map of the rotation
run('monsters off');      // remove all monsters
run('spawn 1680 2');      // spawn tile 1680 next to player 2
```

- The command runs a moment later, after your code has returned.
- The script cannot read the command's output. It goes to the server log.
- See [Server administration](../multiplayer/server-admin.md) and [Console commands](../reference/console-commands.md) for the commands.

## Keeping the game in sync

Multiplayer in NNBuild is deterministic. Every computer simulates the same game from the same inputs. The game checks constantly that all computers still agree. If one computer changes the game world and the others do not, the game is out of sync.

A server script runs on one computer only. So it must not change the game world directly.

**Safe on a server:**

- Chat: `sendMessage`, `registerCommand`, `reply`.
- Timers, files, network: `setTimeout`, `setInterval`, `fs`, `fetch`, `dgram`, `net`.
- Reading game state: `session()`, `getPlayer`, `getActor`, `getGamevar` and the other getters.
- Host console commands that the server sends to all players and applies on every computer at the same moment. `spawn`, `monsters` and `nextmap` are commands of this kind.

**Not safe on a server:**

- `spawn()`, `A_Spawn()`, `A_InsertSprite()`, `spawnPrefab()`.
- Every setter: `setActor`, `setPlayer`, `setInput`, `setSector`, `setWall`, `setGamevar`, `setUserdef`, `setProjectile`, `setActiveProjectile`, `setTileData`.
- `addAmmo`, `addWeapon`, `shoot`.
- Changing `RETURN` or anything else from a CON event handler.

Replace direct changes with the synchronized console commands:

| Instead of | Use |
| --- | --- |
| `spawn(tile, x, y, z)` | `run('spawn <tile> <x> <y> <z>')` or `run('spawn <tile> <player>')` |
| Removing or restoring monsters with setters | `run('monsters off')`, `run('monsters on')`, `run('monsters <skill>')` |

The `spawn` console command takes a tile number or a tile name from the game's CON files. The second form places the actor in front of a player. The player is given by index or by name.

A script that changes the world is still possible in multiplayer if **every** computer loads the same script and the script makes the same changes at the same moment on all of them, for example from a CON event. That is a mod that all players install, not a server script.

## Errors stop the server

An error that your script does not catch closes the game. On a server that ends the match for everyone. Keep these habits:

- Wrap code that can fail in `try`/`catch`: file access, parsing player input, network replies.
- Put a `try`/`catch` inside every `async` function. See [Promises and errors](modules.md#promises-and-errors).
- Treat `args` as untrusted text. Players can type anything.

## Restarts

When the server changes to a different content pack, the server program restarts itself with the same command line. The script receives `shutdown` and is then loaded again from the beginning. Variables and timers are lost. Store anything that must survive in a file.

## A complete server script

```ts
// server.ts
// ./nnbuild --headless --startnow --js ./server.ts -net /p23513 /n0:8
import { log, run, sendMessage, registerCommand, session } from 'nnb';
import fs from 'fs';

const MINUTE = 60_000;
const ATOMIC_HEALTH = 100; // Duke Nukem 3D tile number
const NOTES_FILE = 'server-notes.txt';

function playerName(index: number): string {
  const found = session().players.find((p) => p.index === index);
  return found ? found.name : `player ${index}`;
}

let hosting = false;

try {
  // Chat commands. Only the player who typed the command sees the answer.
  registerCommand('help', () => 'Commands: !help, !rules, !players, !gift, !note <text>');
  registerCommand('rules', () => 'Play the objective and keep chat friendly.');

  registerCommand('players', ({ reply }) => {
    const names = session().players.map((p) => p.name).filter((name) => name.length > 0);
    reply(`${names.length} online: ${names.join(', ')}`);
  });

  // World changes go through a synchronized host console command.
  registerCommand('gift', ({ player, reply }) => {
    run(`spawn ${ATOMIC_HEALTH} ${player}`);
    reply('A gift is on its way.');
  });

  // File access works on a headless server too.
  registerCommand('note', ({ player, args }) => {
    if (args.length === 0) return 'Usage: !note <text>';
    try {
      fs.appendFileSync(NOTES_FILE, `${playerName(player)}: ${args}\n`);
      return 'Noted.';
    } catch (err) {
      log(`could not write ${NOTES_FILE}: ${err}`);
      return 'Sorry, the note could not be saved.';
    }
  });

  hosting = true;
} catch (err) {
  // registerCommand throws when this computer is not the server.
  log(`server script is idle: ${err}`);
}

if (hosting) {
  // A repeating announcement, then a map change one minute later.
  setInterval(() => {
    sendMessage('The map changes in one minute.');
    setTimeout(() => run('nextmap'), MINUTE);
  }, 10 * MINUTE);

  log('server script ready');
}
```

What it does:

- The first `registerCommand` throws when this computer is not the server. The `catch` logs that and leaves `hosting` at `false`, so nothing else happens.
- `!help` and `!rules` return a string. The string is sent to the player who asked.
- `!players` answers with the names from `session()`.
- `!gift` spawns an item in front of the player. It uses the `spawn` console command, which is applied on every computer, so the game stays in sync.
- `!note some text` appends a line to a file. A failed write is caught, logged and reported to the player. It does not stop the server.
- Every ten minutes the server announces a map change and runs `nextmap` one minute later. `nextmap` needs a map rotation. See [Server administration](../multiplayer/server-admin.md).
