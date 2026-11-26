# Modding overview

NNBuild can be modded in four ways. You can combine them.

> The program file is called `nnbuild` (`nnbuild.exe` on Windows). The examples here use that name.

## Kinds of mods

| Kind | What it is | Use it when |
| --- | --- | --- |
| **Script mod** | `.js` or `.ts` files run by the built-in QuickJS JavaScript engine. | You want new game logic, HUD text, file or network access, without editing the game's own script files. |
| **Classic mod** | CON scripts, GRP/ZIP/PK3 archives, DEF files and user maps, loaded the way EDuke32 loads them. | You play an existing mod, or you already write CON. |
| **Editor project** | A folder or ZIP made with the map editor: maps, art, sounds and optional scripts attached to map objects. A *stand-alone* project also ships its own game data. | You build levels, or a complete small game. |
| **Server script** | A script mod that runs on a multiplayer host or dedicated server. | You run a server and want chat commands, announcements or automatic map changes. |

### Script mods (JavaScript and TypeScript)

A script mod is a folder with an `index.ts` or `index.js` file. The game loads it at startup. Node.js is not needed to run it: TypeScript files are converted by the game itself.

Scripts talk to the game through the `vb` module. They can also use a small set of Node-style modules (`fs`, `dgram`, `net`), `fetch`, and timers.

- [Quick start](quick-start.md): create and run your first mod.
- [Project layout](project-layout.md): how scripts are found, how `import` works, what TypeScript is supported.
- [API reference](api-reference.md): every function of the `vb` module.
- [Events](events.md): every event you can listen to.
- [Components](components.md): class-based components for sprites and nodes.
- [Built-in modules](modules.md): `fs`, `dgram`, `net`, `cvar`, `fetch`, timers.
- [TypeScript examples](typescript-examples.md): short complete mods.
- [Debugging with VS Code](debugging-vscode.md): breakpoints, DevTools, error reports.

### Classic CON, GRP and ZIP mods

Classic mods keep working. Pass the files on the command line:

```sh
./nnbuild mymod.grp MYGAME.CON
./nnbuild --grp mymod.zip --con MYGAME.CON
```

A bare file name ending in `.grp`, `.zip`, `.pk3`, `.con`, `.def`, `.map` or `.rts` is loaded as that kind of file. See [Launch options](../reference/command-line.md) for the full list.

A script mod can work together with a classic mod. It can read and write CON gamevars and listen to CON events. See [Events](events.md) and [API reference](api-reference.md).

### Editor projects and stand-alone games

The map editor stores a project as a folder with a `nnbuild.project.json` file. A project can be played, edited, or exported as a ZIP. See [Projects and stand-alone games](../editor/projects.md).

A project with *Modern Mode* turned on can attach scripts to sprites, sectors and walls. These scripts use a different, simpler model than normal script mods. See [Map script components](map-script-components.md).

### Server scripts

Any script mod can run on a multiplayer host or on a headless dedicated server. On a server a script can also register `!commands` that players type into chat. See [Server scripts](server-scripts.md).

## Which games run scripts

| Game | Script support |
| --- | --- |
| Duke Nukem 3D, World Tour, Ion Fury, NAM, NAPALM, WWII GI | Full. These games share the *Duke runtime*. Every API in this section works. |
| Shadow Warrior | Core only. Scripts load and receive `start`, `frame` and `shutdown`. `log`, timers, `fs`, `dgram`, `net` and `fetch` work. Functions that read or change game state throw an error. |
| Blood, Redneck Rampage, Exhumed | Scripts are not loaded. |

Pages in this section mark an API as **Duke runtime** when it only works in the first group.

## Words used in this section

- **Tile**: a numbered picture in the game's art files. Every kind of object is identified by its tile number.
- **Sprite**: an object in the map, such as an enemy, an item, a projectile or a player. A sprite has an index number. The number is reused after the sprite is removed.
- **Actor**: a sprite that runs game logic.
- **Sector**: an area of the map with a floor and a ceiling. A **wall** is one edge of a sector.
- **CON**: the game's original scripting language (`GAME.CON` and related files).
- **CON event**: a hook named `EVENT_…` that the engine calls at a fixed point, for example `EVENT_JUMP`.
- **Gamevar**: a named integer variable of the CON system. It can be global, per player or per actor.
- **Console**: the in-game command line. A script can run console commands.

## Things to know before you start

- Scripts are loaded once, when the game starts. Restart the game to load a changed script.
- An error that your script does not catch closes the game and shows the error text. See [how errors are reported](debugging-vscode.md#how-errors-are-reported).
- A script runs only on the computer that loaded it. In multiplayer, a change that only one computer makes to the game world breaks synchronization. See [Server scripts](server-scripts.md#keeping-the-game-in-sync).
