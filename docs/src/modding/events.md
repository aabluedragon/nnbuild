# Events

A script reacts to the game by listening to events.

```ts
import { on } from 'nnb';

on('levelChanged', () => {
  // a level has been loaded
});

on('actorSpawned', (e) => {
  // e is { sprite, owner, tile }
});
```

`on(name, handler)` adds a handler. `off(name)` removes all handlers of that event. Both are described in the [API reference](api-reference.md#events).

All events on this page except `start`, `frame` and `shutdown` exist only in the **Duke runtime** games. Shadow Warrior fires only those three.

## How handlers run

- Handlers run on the game's main thread. While a handler runs, the game waits. Keep handlers short, especially `frame` handlers.
- Handlers of one event run in the order they were registered.
- If a handler throws an error that it does not catch, the game shows the error and closes. See [how errors are reported](debugging-vscode.md#how-errors-are-reported).
- Events that fire before your script has been loaded are not repeated later. Scripts are loaded a moment after the game starts. See [Events you can miss](#events-you-can-miss).

## Lifecycle events

These events pass no event object.

| Event | Fires when |
| --- | --- |
| `start` | Once, right after all scripts have been loaded. |
| `frame` | On every drawn frame, on the title screen, in menus and during play. Also on a headless server, which draws nothing but still runs its loop. |
| `gameStarted` | A game session begins: the first level of a new game has finished loading. It does not fire again for the following levels. It fires again for the next game after `leftGame`. |
| `levelChanged` | A level has finished loading. |
| `levelFinished` | The player reached the end of a level, before the score screen. |
| `leftGame` | The game goes back from a running game to the menu. Also when a single-player game ends after its last level. |
| `playerDied` | A player dies. It fires for every player, not only the local one. |
| `beforeQuit` | The game is about to exit. |
| `shutdown` | The script runtime is being closed. It follows `beforeQuit` at exit. It also fires before the program restarts itself for a content pack change in multiplayer. |

Notes:

- When the first level of a game loads, `gameStarted` fires first and `levelChanged` directly after it. For every later level only `levelChanged` fires.
- `playerDied` has no event object, so the handler cannot tell which player died. Check the players yourself, for example with `getPlayer(index, 'dead_flag')`.

### Events you can miss

Scripts are loaded during the first frames of the game loop. A level that started before that has already fired its events.

This happens when the game starts directly in a level:

- with `-map`, `-l` and `-v`, or a saved game slot on the command line,
- when you play an editor project with `--project`.

It can also happen in a multiplayer game started from the command line, including a dedicated server.

In these cases your script does not receive `gameStarted` or `levelChanged` for that first level. It still receives `start`. To cover both cases, do your setup in a function and call it from `start` when a game is already running:

```ts
import { on, session } from 'nnb';

function setup() {
  // level-dependent setup
}

on('start', () => {
  if (session().started) setup();
});
on('levelChanged', setup);
```

When the game starts on the title screen, scripts are loaded there, before any level.

For setup that belongs to a level, prefer `levelChanged` over `gameStarted`. `gameStarted` fires only once per session and is easier to miss.

## Menu events

The event object is `{ menu, previousMenu }`.

- `menu`: number. The id of the menu page that is current now.
- `previousMenu`: number, or `undefined` when there is none.

| Event | Fires when | `previousMenu` |
| --- | --- | --- |
| `menuOpened` | The menu became visible. Detected once per frame. | The page id seen on the previous frame. |
| `menuClosed` | The menu was closed. Detected once per frame. | The page id seen on the previous frame. |
| `menuChanged` | The menu switched to a different page. | The page that was current before. |
| `escapeMenuOpened` | The player opened the menu with the Escape key. | `undefined` |

Menu ids are the game's internal page numbers. Log `e.menu` to find the id of a page.

## Actor events

The event object is `{ sprite, owner, tile }`.

- `sprite`: index of the sprite.
- `tile`: the sprite's tile number. Use it to filter.
- `owner`: see the table.

| Event | Fires when | `owner` |
| --- | --- | --- |
| `actorSpawned` | The engine has finished setting up an actor. This covers actors created during play and the sprites of a map when a level loads. | Index of the sprite that spawned it, or `-1` when there is none. Map sprites and actors created with `spawn()` report `-1`. |
| `actorDestroyed` | An actor is about to be removed. The sprite still exists inside the handler, so you can read its members. | The sprite's `owner` member. |

`actorDestroyed` does not fire for the sprites of a level that is replaced by loading another level. The old sprites disappear without events.

`actorSpawned` fires very often while a level loads. Filter by `tile` first and do little work.

```ts
import { on, define, log } from 'nnb';

let trooper: number | undefined;
on('start', () => {
  trooper = define('LIZTROOP');
});

on('actorDestroyed', (e) => {
  if (e.tile === trooper) log(`trooper sprite ${e.sprite} removed`);
});
```

## Chat events

The event object is `{ player, text }`.

- `text`: the complete line as it is shown, including the sender's name and color codes. `^` followed by two digits is a color code.
- `player`: a player index, or `-1`. See the table.

`chatMessage` fires in exactly three situations. A chat line fires it once per computer.

| Situation | `player` | `text` looks like |
| --- | --- | --- |
| The local player sends a chat message to all players from the chat box. | The local player's index. | `Name^07: hello` |
| A chat message from another player arrives on this computer. | On the host: the sender's index. On every other computer: `-1`, because the host passes the message on. | `Name^07: hello` |
| A message from the server arrives on a client. That is a [`sendMessage`](api-reference.md#chat-and-server-commands) call on the host, or the reply to a `!command`. | The host's player index. | `^07Server: hello` |

What does not fire `chatMessage`:

- Other lines in the on-screen message area, such as kill messages.
- `sendMessage`, on the computer that calls it. The other computers receive the message and fire the event there.
- Server messages, on the host itself.
- A private chat message, on the computer that sends it. It fires on the computer of the player it is addressed to.
- A chat line that starts with `!` in a master/slave multiplayer game. It goes to the server's command handlers. See [Server scripts](server-scripts.md#chat-commands).

On a client, `player` is `-1` for chat from other players. If you need the sender there, read the name at the start of `text`.

Be careful with a handler that answers every `chatMessage` with `sendMessage`. If two computers run such a script, they keep answering each other.

## CON events

CON is the game's original scripting language. The engine calls CON *events* at fixed points, for example when a player jumps. A script can listen to every one of them.

```ts
import { on, setGamevar } from 'nnb';

// Cancel every jump.
on('EVENT_JUMP', (e) => {
  setGamevar('RETURN', 1, e.sprite, e.player);
});
```

### Names

- Use the CON name, for example `EVENT_JUMP`.
- The `EVENT_` prefix is optional and the name is not case-sensitive. `on('jump', …)` is the same.

### The event object

The event object is `{ sprite, player, dist }`. These are the three values the engine passes to the CON event.

- `sprite`: the sprite the event is about, or `-1`.
- `player`: the player index the event is about, or `-1`.
- `dist`: a distance where the event has one, otherwise `-1` or `0`.

Their exact meaning depends on the event. It is the same as for the CON event of that name.

### The `RETURN` gamevar

Many CON events use the gamevar `RETURN` to pass a value in or to take a decision back. Read and write it with the event's own sprite and player:

```ts
const value = getGamevar('RETURN', e.sprite, e.player);
setGamevar('RETURN', 1, e.sprite, e.player);
```

### Order

Your handlers run **before** the CON code of the same event. If the loaded CON files also handle the event, they can overwrite what you put in `RETURN`.

### Cost

When you listen to a CON event, the engine starts calling it even if no CON code uses it. Some events fire extremely often. `EVENT_GAME` fires for every sprite on every game tick. Listen to such events only if you need them.

### Examples of CON events

These were checked in the game's source.

| Event | Fires when | `sprite`, `player`, `dist` | `RETURN` |
| --- | --- | --- | --- |
| `EVENT_WORLD` | Once per game tick. | `-1`, `-1`, `0` | Not used. |
| `EVENT_GAME` | Once per game tick for every sprite. | The sprite, the nearest player, the distance to that player. | Not used. |
| `EVENT_SPAWN` | An actor has been spawned. | The new sprite, the nearest player, the distance to that player. | Not used. |
| `EVENT_KILLIT` | A sprite is about to be removed. | The sprite, the nearest player, the distance to that player. | Set it to a non-zero value to keep the sprite. |
| `EVENT_JUMP` | A player starts a jump. | The player's sprite, the player index, `-1`. | Set it to a non-zero value to cancel the jump. |
| `EVENT_USE` | A player presses the Use key. | The player's sprite, the player index, `-1`. | Set it to a non-zero value to ignore the key press. |
| `EVENT_ENTERLEVEL` | A level has loaded. Fires for the first player. | The player's sprite, the player index, `-1`. | Starts at `0`. If it is non-zero afterwards, the event also fires for the next player. |
| `EVENT_DISPLAYSBAR` | Every frame, before the status bar is drawn. | The viewed player's sprite, the viewed player, `-1`. | Set it to a non-zero value to hide the status bar. |
| `EVENT_DISPLAYREST` | Every frame, while the HUD is drawn. | The viewed player's sprite, the viewed player, `-1`. | Not used. |
| `EVENT_CHANGEMENU` | The menu is about to switch page. | The viewed player's sprite, the viewed player, `-1`. | Holds the id of the page the game wants to open. Write another id to redirect, `-1` to stay, `-2` to close the menu. |

### All CON event names

The engine registers 165 events:

```text
EVENT_ACTIVATECHEAT EVENT_ACTIVATESWITCH EVENT_AIMDOWN EVENT_AIMUP EVENT_ALTFIRE
EVENT_ALTWEAPON EVENT_ANIMATESPRITES EVENT_BOTGETSPRITESCORE EVENT_CAPIR EVENT_CHANGEMENU
EVENT_CHANGEWEAPON EVENT_CHEATGETBOOT EVENT_CHEATGETFIRSTAID EVENT_CHEATGETHEAT
EVENT_CHEATGETHOLODUKE EVENT_CHEATGETJETPACK EVENT_CHEATGETSCUBA EVENT_CHEATGETSHIELD
EVENT_CHEATGETSTEROIDS EVENT_CHECKFLOORDAMAGE EVENT_CHECKTOUCHDAMAGE
EVENT_CONTINUELEVELMUSICSLOT EVENT_CROUCH EVENT_CUTSCENE EVENT_DAMAGECEILING EVENT_DAMAGEFLOOR
EVENT_DAMAGESPRITE EVENT_DAMAGEWALL EVENT_DISPLAYACCESS EVENT_DISPLAYBONUSSCREEN
EVENT_DISPLAYBORDER EVENT_DISPLAYCROSSHAIR EVENT_DISPLAYCURSOR EVENT_DISPLAYEND
EVENT_DISPLAYFIST EVENT_DISPLAYINACTIVEMENU EVENT_DISPLAYINACTIVEMENUREST EVENT_DISPLAYKNEE
EVENT_DISPLAYKNUCKLES EVENT_DISPLAYLEVELSTATS EVENT_DISPLAYLOADINGSCREEN EVENT_DISPLAYMENU
EVENT_DISPLAYMENUREST EVENT_DISPLAYOVERHEADMAPPLAYER EVENT_DISPLAYOVERHEADMAPTEXT
EVENT_DISPLAYPOINTER EVENT_DISPLAYREST EVENT_DISPLAYROOMS EVENT_DISPLAYROOMSCAMERATILE
EVENT_DISPLAYROOMSEND EVENT_DISPLAYSBAR EVENT_DISPLAYSCUBA EVENT_DISPLAYSPIT EVENT_DISPLAYSTART
EVENT_DISPLAYTIP EVENT_DISPLAYWEAPON EVENT_DOFIRE EVENT_DOQUAKE EVENT_DRAWWEAPON EVENT_EGS
EVENT_ENDLEVELSCREEN EVENT_ENTERLEVEL EVENT_EXITGAMESCREEN EVENT_EXITPROGRAMSCREEN
EVENT_FAKEDOMOVETHINGS EVENT_FIRE EVENT_FIREWEAPON EVENT_FRAGGED EVENT_GAME
EVENT_GETAUTOAIMANGLE EVENT_GETBONUSTILE EVENT_GETLOADTILE EVENT_GETMENUTILE EVENT_GETSHOTRANGE
EVENT_HOLODUKEOFF EVENT_HOLODUKEON EVENT_HOLSTER EVENT_INCURDAMAGE EVENT_INIT
EVENT_INITCOMPLETE EVENT_INVENTORY EVENT_INVENTORYLEFT EVENT_INVENTORYRIGHT EVENT_JUMP
EVENT_KILLIT EVENT_LASTWEAPON EVENT_LOADACTOR EVENT_LOADGAME EVENT_LOGO EVENT_LOOKDOWN
EVENT_LOOKLEFT EVENT_LOOKRIGHT EVENT_LOOKUP EVENT_MAINMENUSCREEN EVENT_MENUCURSORLEFT
EVENT_MENUCURSORRIGHT EVENT_MENUCURSORSHADE EVENT_MENUSHADESELECTED EVENT_MOVEBACKWARD
EVENT_MOVEEFFECTORS EVENT_MOVEFORWARD EVENT_MOVESECTOR EVENT_NEWGAME EVENT_NEWGAMECUSTOM
EVENT_NEWGAMESCREEN EVENT_NEXTWEAPON EVENT_OPENMENUSOUND EVENT_OPERATEACTIVATORS
EVENT_PLAYLEVELMUSICSLOT EVENT_POSTDAMAGESPRITE EVENT_POSTSAVEGAME EVENT_POSTUPDATEANGLES
EVENT_POSTWEAPONSHOOT EVENT_PREACTORDAMAGE EVENT_PRECUTSCENE EVENT_PREGAME EVENT_PRELEVEL
EVENT_PRELOADGAME EVENT_PRESSEDFIRE EVENT_PREUPDATEANGLES EVENT_PREVIOUSWEAPON
EVENT_PREWEAPONSHOOT EVENT_PREWORLD EVENT_PROCESSINPUT EVENT_QUICKKICK EVENT_RECOGSOUND
EVENT_RESETGOTPICS EVENT_RESETINVENTORY EVENT_RESETPLAYER EVENT_RESETWEAPONS
EVENT_RETURNTOCENTER EVENT_SAVEGAME EVENT_SCREEN EVENT_SELECTWEAPON EVENT_SETDEFAULTS
EVENT_SKIPCUTSCENE EVENT_SOARDOWN EVENT_SOARUP EVENT_SOUND EVENT_SPAWN EVENT_STRAFELEFT
EVENT_STRAFERIGHT EVENT_SWIMDOWN EVENT_SWIMUP EVENT_TURNAROUND EVENT_TURNLEFT EVENT_TURNRIGHT
EVENT_UPDATESCREENAREA EVENT_USE EVENT_USEJETPACK EVENT_USEMEDKIT EVENT_USENIGHTVISION
EVENT_USESTEROIDS EVENT_VALIDATESTART EVENT_WEAPKEY1 EVENT_WEAPKEY2 EVENT_WEAPKEY3
EVENT_WEAPKEY4 EVENT_WEAPKEY5 EVENT_WEAPKEY6 EVENT_WEAPKEY7 EVENT_WEAPKEY8 EVENT_WEAPKEY9
EVENT_WEAPKEY10 EVENT_WORLD
```

A name on this list is accepted by `on()`. Whether and when the event fires depends on the game and on what happens in it.

### Names missing from `nnb.d.ts`

The generated `nnb.d.ts` knows 112 of these names. The other 53 work at run time, but the type check treats them as unknown event names and gives the handler an untyped argument:

```text
EVENT_ACTIVATECHEAT EVENT_CAPIR EVENT_CHANGEMENU EVENT_CHECKFLOORDAMAGE
EVENT_CONTINUELEVELMUSICSLOT EVENT_CUTSCENE EVENT_DAMAGEWALL EVENT_DISPLAYBORDER
EVENT_DISPLAYCURSOR EVENT_DISPLAYEND EVENT_DISPLAYFIST EVENT_DISPLAYINACTIVEMENU
EVENT_DISPLAYINACTIVEMENUREST EVENT_DISPLAYKNEE EVENT_DISPLAYKNUCKLES
EVENT_DISPLAYOVERHEADMAPPLAYER EVENT_DISPLAYOVERHEADMAPTEXT EVENT_DISPLAYPOINTER
EVENT_DISPLAYROOMSCAMERATILE EVENT_DISPLAYROOMSEND EVENT_DISPLAYSCUBA EVENT_DISPLAYSPIT
EVENT_ENDLEVELSCREEN EVENT_EXITGAMESCREEN EVENT_EXITPROGRAMSCREEN EVENT_GETBONUSTILE
EVENT_INITCOMPLETE EVENT_LOADGAME EVENT_MAINMENUSCREEN EVENT_MENUCURSORLEFT
EVENT_MENUCURSORRIGHT EVENT_MENUCURSORSHADE EVENT_MENUSHADESELECTED EVENT_MOVEEFFECTORS
EVENT_MOVESECTOR EVENT_NEWGAMECUSTOM EVENT_NEWGAMESCREEN EVENT_OPENMENUSOUND
EVENT_OPERATEACTIVATORS EVENT_PLAYLEVELMUSICSLOT EVENT_POSTSAVEGAME EVENT_POSTUPDATEANGLES
EVENT_PREACTORDAMAGE EVENT_PRECUTSCENE EVENT_PRELOADGAME EVENT_PREUPDATEANGLES EVENT_RECOGSOUND
EVENT_RESETGOTPICS EVENT_SAVEGAME EVENT_SCREEN EVENT_SKIPCUTSCENE EVENT_UPDATESCREENAREA
EVENT_VALIDATESTART
```

For these, give the argument its type yourself:

```ts
import { on } from 'nnb';

on('EVENT_DAMAGEWALL', (event) => {
  const e = event as ConEvent;
});
```

## Event summary

| Event | Event object |
| --- | --- |
| `start`, `frame`, `gameStarted`, `levelChanged`, `levelFinished`, `leftGame`, `playerDied`, `beforeQuit`, `shutdown` | none |
| `menuOpened`, `menuClosed`, `menuChanged`, `escapeMenuOpened` | `{ menu: number, previousMenu?: number }` |
| `actorSpawned`, `actorDestroyed` | `{ sprite: number, owner: number, tile: number }` |
| `chatMessage` | `{ player: number, text: string }` |
| any CON event | `{ sprite: number, player: number, dist: number }` |
