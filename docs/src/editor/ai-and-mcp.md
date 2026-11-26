# AI assistants and MCP

The Model Context Protocol (MCP) is an open standard that lets an AI
assistant use tools offered by another program. The assistant's side is
called the *MCP client*. NNBuild can be the other side, the *MCP server*.

NNBuild offers MCP in two ways.

| You want the assistant to | Use | Start NNBuild with |
| --- | --- | --- |
| Join a [live editing session](live-editing.md) as one more participant | The participant adapter | `--editor-mcp` |
| Control a game, a server or the editor on this machine | The built-in control endpoint | `--mcp-stdio` or `--mcp-http` |

Both speak MCP over one of two *transports*:

- **stdio.** The MCP client starts NNBuild itself and talks to it through the
  program's standard input and output.
- **HTTP.** NNBuild listens on a local web address and the client sends
  requests to it.

## Join a live session as a participant

The participant adapter is a small process with no window. It joins a live
session like a person would. The others see it in the **Participants** list
with its own colour and the mark "(AI)". Its pointer and its selection are
visible in their Plan view. Its changes appear in the shared change history
and can be undone like anyone else's.

1. A person [hosts a session](live-editing.md#host-a-session) in the editor.
2. The MCP client starts the adapter:

```sh
./nnbuild --editor-mcp 127.0.0.1:23514 --name "Layout Assistant"
```

The form of the command is:

```text
nnbuild --editor-mcp [host[:port]] [--name name] [--mcp-http port]
```

| Part | Meaning | If left out |
| --- | --- | --- |
| `host[:port]` | Address of the session host | `127.0.0.1`, port 23514 |
| `--name name` | The name the others see | `AI Editor` |
| `--mcp-http port` | Use the HTTP transport on this port | stdio is used |

Good to know:

- The adapter tries to join when it starts. If the session is not up yet, the
  assistant can join later with the `editor_connect` tool.
- Use one adapter process for each AI participant.
- The adapter does not choose a game and does not load game data. It starts
  at once and never asks a question. It may run on a different machine than
  the host.
- An AI participant does not have to match the host's game edition.
- Only the parts shown above may follow `--editor-mcp`. Any other option
  after it makes the adapter stop with a usage message.

### Example: stdio client for the adapter

Most MCP clients keep their servers in a JSON block like this one. Use the
full path of the program.

```json
{
  "mcpServers": {
    "nnbuild-live": {
      "command": "/full/path/to/nnbuild",
      "args": [
        "--editor-mcp", "127.0.0.1:23514",
        "--name", "Layout Assistant"
      ]
    }
  }
}
```

The name of the file and of the keys can differ between MCP clients. Check
your client's documentation.

## Control a game, a server or the editor

Add `--mcp-stdio` or `--mcp-http PORT` to a normal launch. The game then
offers tools to read its state, type console commands, send input, and drive
the editor and its projects.

```sh
./nnbuild -gamegrp /path/to/DUKE3D.GRP --editor --mcp-stdio
./nnbuild -gamegrp /path/to/DUKE3D.GRP --editor --mcp-http 8787
```

- With `--editor` the program starts in the editor. Without it, the assistant
  can open the editor with `editor_control` and the action `enter`.
- Add `-sm` to skip the startup window, if your setup shows one.
- The assistant acts as the local user. Offline, its edits are listed in the
  change history under the name "Assistant". In a live session, it uses the
  local participant's place.
- When the assistant points at something, a pointer labelled "Assistant (AI)"
  appears in Plan.
- With `--mcp-stdio`, the program's own text output goes to standard error.
  Standard output is kept for MCP.
- The options also work on a multiplayer server. Add them to the server's
  normal launch options. A dedicated server normally reads admin commands
  from standard input. With `--mcp-stdio` it does not, so send them with
  `console_execute`.

### Name the game

When data for several games is installed and the command line names none of
them, NNBuild normally asks on the console which game to start. It does not
ask when `--mcp-stdio` is given. It also does not ask when its standard input
is not a terminal, which is usual when another program starts it. In both
cases it starts the first game it finds.

To be sure that the right game starts, name it in one of these ways:

- Add `--game` with the game's name, for example `--game duke`. Other names
  are `worldtour`, `nam`, `napalm` and `ww2gi`.
- Give the game's data file, for example `-gamegrp /path/to/DUKE3D.GRP`.
  NNBuild recognises the game from a known data file.

### Example: stdio client for the game

```json
{
  "mcpServers": {
    "nnbuild": {
      "command": "/full/path/to/nnbuild",
      "args": [
        "--game", "duke",
        "-gamegrp", "/path/to/DUKE3D.GRP",
        "--editor",
        "--mcp-stdio"
      ]
    }
  }
}
```

Replace `duke` and the data file with those of the game you run.

### Example: HTTP client

Start NNBuild yourself, with a token:

```sh
NNBUILD_MCP_TOKEN=change-this-token ./nnbuild -gamegrp /path/to/DUKE3D.GRP --editor --mcp-http 8787
```

Then point the client at the address and give it the same token:

```json
{
  "mcpServers": {
    "nnbuild": {
      "type": "http",
      "url": "http://127.0.0.1:8787/mcp",
      "headers": {
        "Authorization": "Bearer change-this-token"
      }
    }
  }
}
```

## The HTTP endpoint

With `--mcp-http PORT`, NNBuild listens at:

```text
http://127.0.0.1:PORT/mcp
```

It listens on the local machine only. A program on another machine cannot
reach it. If a remote client must connect, put your own HTTPS proxy with a
login in front of it.

### Environment variables

| Variable | Effect |
| --- | --- |
| `NNBUILD_MCP_TOKEN` | When set, every request must carry the header `Authorization: Bearer` followed by this value. Other requests get the answer 401. |
| `NNBUILD_MCP_ORIGIN` | One extra value that is allowed in the `Origin` header of a request. |

**Token.** Without a token, any program on the same machine can call the
tools. The tools can run console commands and write files, so set a token.

**Origin.** Web browsers add an `Origin` header to requests. NNBuild accepts a
request with no `Origin` header. If the header is present, it must be
`http://127.0.0.1:PORT`, `http://localhost:PORT`, or exactly the value of
`NNBUILD_MCP_ORIGIN`. Other origins get the answer 403.

### Limits

- Requests use `POST /mcp`. A `GET` is answered with 405.
- Each request holds one JSON-RPC message of at most 128 KB and must state
  its `Content-Length`.
- NNBuild answers and then closes the connection.
- It serves up to eight connections at a time. A connection must finish
  within eight seconds.

## Protocol notes

- The server answers `initialize`, `ping`, `tools/list` and `tools/call`.
- It accepts the protocol versions `2024-11-05`, `2025-03-26` and
  `2025-06-18`, and otherwise answers with `2025-11-25`.
- Over stdio, each message is one line of JSON.
- A tool answers with JSON. When a tool fails, the answer is marked as an
  error and holds a sentence that says what to change.

## Which tools are available

| Tool | With `--editor-mcp` | Game with `--mcp-stdio` or `--mcp-http` | [`--build-editor`](classic-build-tools.md) with an MCP option |
| --- | --- | --- | --- |
| `editor_status` | Yes | Yes | No |
| `editor_connect` | Yes | No | No |
| `editor_disconnect` | Yes | No | No |
| `editor_inspect` | Yes | Yes | No |
| `editor_presence` | Yes | Yes | No |
| `editor_edit` | Yes | Yes | No |
| `editor_history` | Yes | Yes | No |
| `editor_export` | Yes | Yes | No |
| `editor_check` | No | Yes | No |
| `editor_control` | No | Yes | No |
| `project_status` | No | Yes | No |
| `project_control` | No | Yes | No |
| `game_status` | No | Yes | Yes |
| `server_status` | No | Yes | No |
| `console_execute` | No | Yes | Yes |
| `console_output` | No | Yes | Yes |
| `ui_input` | No | Yes | Yes |

In the game, the editor tools that read or change the map work only while the
editor is open.

## Rules that apply to all map tools

- Positions are in map units. X and Y run from −524,288 to 524,288.
- Z grows downward. One height unit is 256.
- Angles run from 0 to 2047.
- Sectors, walls and sprites are numbered from 0. A number is called an
  *index*.
- The map has a *revision* number that rises with every change. An edit must
  name the current revision. If the map changed in the meantime, the edit is
  refused and the assistant must read the map again.
- Adding or deleting sectors, walls or sprites can change the indexes of
  other objects. Read the map again after such an edit.

## Editor tools

### editor_status

Reads the state of the session and who is in it. No arguments.

| Field in the answer | Meaning |
| --- | --- |
| `state` | With the adapter: `offline`, `connecting`, `joined` or `hosting`. In the game: `closed`, `editing` or `playtest`. |
| `revision` | The current revision of the map |
| `slot` | This participant's place in the session |
| `tile_limit` | Tile numbers must be below this value |
| `players` | One entry for each participant: `slot`, `name`, `ai`, `activity`, and if present `selection` and `cursor` |
| `notice` | Adapter only. A message from the session, such as the reason for a refusal. |

In the game, the answer also has `session`, `name`, `path`, `view` (`plan`,
`3D` or `section`), `dirty`, `busy`, `can_undo` and `can_redo`.

### editor_connect

Adapter only. Joins a session, or joins again after a disconnect.

| Argument | Meaning |
| --- | --- |
| `address` | Optional. Host address. The default is the address the adapter was started with. |
| `name` | Optional. Name to show. The default is the name the adapter was started with. |

### editor_disconnect

Adapter only. Leaves the session. The adapter keeps running. No arguments.

### editor_inspect

Reads a summary of the map, or a range of its records.

| Argument | Meaning |
| --- | --- |
| `kind` | `map` for the summary only, or `sectors`, `walls` or `sprites`. The default is `map`. |
| `index` | First record to read. The default is 0. |
| `count` | How many records to read, from 1 to 80. The default is 32. |

Every answer holds `revision`, `sector_count`, `wall_count`, `sprite_count`
and `start` (the player start with `x`, `y`, `z`, `angle` and `sector`).

A record holds the property names listed under
[Properties for set](#properties-for-set), and these read-only fields:

| Record | Read-only fields |
| --- | --- |
| Sector | `index`, `first_wall`, `wall_count` |
| Wall | `index`, `x`, `y`, `point2` (the next wall of the outline), `next_wall` and `next_sector` (the other side of a portal, or −1), `sector` |
| Sprite | `index`, `x`, `y`, `sector` |

### editor_presence

Moves the assistant's visible pointer and selects an object, so that people
can follow what it is doing.

| Argument | Meaning |
| --- | --- |
| `x`, `y` | Where to point. Give both. |
| `kind` | What to select: `none`, `sector`, `wall`, `vertex`, `sprite` or `start` |
| `index` | The index of that object. Not needed for `none` and `start`. |
| `clear` | `true` removes the pointer and the selection |

### editor_edit

Makes one change to the map. See [Edit operations](#edit-operations).

| Argument | Meaning |
| --- | --- |
| `revision` | Required. The revision the assistant last read. |
| `operation` | Required. One of the operations below. |
| `label` | Optional. A short text for the change history, up to 64 characters. |
| other | The arguments of the chosen operation |

After a successful edit, the assistant's pointer and selection move to the
object it changed. The answer is the same as for `editor_status`.

### editor_history

Reads the most recent changes.

| Argument | Meaning |
| --- | --- |
| `count` | How many changes to read, from 1 to 24. The default is 8. |

Each change has `revision`, `author`, `slot`, `label`, `ai`,
`structure_changed`, and the lists `touched` and `removed` with the kind and
index of each object. The lists show up to 80 objects each. `touched_count`
and `removed_count` give the full numbers.

### editor_export

Saves the current map as a classic version 7 `.map` file. The file is written
on the machine where the adapter or the game runs.

| Argument | Meaning |
| --- | --- |
| `path` | Required. Must end in `.map`. Use a full path. |
| `overwrite` | `true` to replace an existing file. The default is `false`. |

With the adapter, this saves the adapter's own copy. It does not save the map
in the host's editor.

### editor_check

Game only. Runs the [map check](playtesting.md#map-check) on the local map.

| Argument | Meaning |
| --- | --- |
| `index` | First finding to read. The default is 0. |
| `count` | How many findings to read, from 1 to 80. The default is 40. |

The answer has `finding_count` and a list `findings`. Each finding has `kind`,
`index` and `message`.

### editor_control

Game only. Controls the editor window. The answer is the same as for
`editor_status`.

| Action | What it does | Arguments |
| --- | --- | --- |
| `enter` | Opens the editor from the title menu | |
| `new` | Starts a new map | `discard` |
| `open` | Opens a map | `path` (required), `target`, `discard` |
| `save` | Saves the map | `path`, `overwrite` |
| `undo`, `redo` | Takes back or repeats the last change | |
| `playtest` | Starts a playtest | none, or `spawn`, or `x`, `y`, `sector` with optional `angle` and `platform` |
| `stop_playtest` | Returns from the playtest to the editor | |
| `close` | Leaves the editor and returns to the title menu | `discard` |
| `plan`, `perspective`, `section` | Switches to the Plan, 3D or Section view | |
| `frame` | Brings the selection into view | |
| `host` | Hosts a live session | `port`, `name` |
| `join` | Joins a live session | `address` (required), `name`, `discard` |
| `leave` | Leaves the live session | |
| `build_tools` | Opens the [classic Build tools](classic-build-tools.md) on the map | `port` |

| Argument | Meaning |
| --- | --- |
| `path` | A map file. For `open` it can also be the file name of a level in the game data. For `save`, leave it out to save to the current file. |
| `target` | For `open`: the game the map is for. One of `inherit`, `duke13`, `duke_atomic`, `nam`, `napalm`, `ww2gi`, `standalone`. |
| `discard` | `true` allows the action to throw away unsaved changes. Without it, the action is refused while the map is unsaved. |
| `overwrite` | For `save`: `true` allows replacing another existing file. |
| `spawn` | For `playtest`: the index of a player start sprite to start from |
| `x`, `y`, `sector` | For `playtest`: a temporary start position. Give all three. |
| `angle` | For `playtest`: the direction to face, from 0 to 2047 |
| `platform` | For `playtest`: the index of a catwalk sprite to stand on at that position |
| `port` | For `host`: the UDP port, 23514 if left out. For `build_tools`: a TCP port on which the classic tools offer their own MCP endpoint. |
| `name` | For `host` and `join`: the name to show |
| `address` | For `join`: the host's address |

## Project tools

### project_status

Game only. Reads the open project and the settings of the current map.

| Argument | Meaning |
| --- | --- |
| `index` | First asset to list. The default is 0. |
| `count` | How many assets to list, from 1 to 80. The default is 40. |

| Field in the answer | Meaning |
| --- | --- |
| `root` | The project folder. Empty when no project is open. |
| `name`, `entry`, `target`, `modern` | The [project settings](projects.md#the-settings-file) |
| `requires_nnbuild` | `true` when Modern Mode is in effect |
| `compatibility_warning` | The Modern Mode warning text, when it applies |
| `map` | The current map's `target`, `effective_target`, `modern` (−1 inherit, 0 off, 1 on), `effective_modern` and `scripts` |
| `asset_count` | Number of files and folders in the project |
| `assets` | For each listed asset: `path`, `directory`, `target`, `icon`, and `type` (`folder`, `map`, `script`, `prefab` or `file`) |

### project_control

Game only. Manages the project. The editor must be open. The answer is the
same as for `project_status`.

| Action | What it does | Arguments |
| --- | --- | --- |
| `create` | Creates a project folder and opens it | `path` (required), `name`, `target`, `modern` |
| `open` | Opens a project folder | `path` (required) |
| `close` | Closes the project | |
| `import_zip` | Unpacks a project ZIP into a new folder and opens it | `path` (the ZIP), `destination` (the new folder) |
| `export_zip` | Writes the project to a ZIP | `path` (required), `overwrite` |
| `add_asset` | Copies a file into the project | `path` (the file), `destination`, `overwrite` |
| `settings` | Changes the project settings | `name`, `target`, `modern`, `entry` |
| `map_settings` | Changes the settings of the current map | `target`, `modern` |
| `attach` | Attaches a script component to an object | `kind`, `index`, `script` |
| `detach` | Removes a script component from an object | `kind`, `index`, `script` |
| `export_map` | Writes the current map as a plain classic map | `path` (required), `target`, `overwrite` |
| `create_component` | Creates a starter script and attaches it | `path` (the new script), `kind`, `index` |
| `save_prefab` | Saves sprites as a prefab | `path` (required), `sprites`, `overwrite` |
| `place_prefab` | Places a prefab in the map | `path` (required), `x`, `y`, `sector`, `floor_z` |

| Argument | Meaning |
| --- | --- |
| `path` | A file or folder. For `create_component`, `save_prefab` and `place_prefab` it is a path inside the project. |
| `destination` | For `import_zip`: the new folder. For `add_asset`: the path inside the project. If left out, the file keeps its name at the top of the project. |
| `name` | The project's name |
| `entry` | For `settings`: the entry map, as a path inside the project. An empty text clears it. |
| `target` | A game target: `duke13`, `duke_atomic`, `nam`, `napalm`, `ww2gi`, `standalone`, or `inherit` for a map |
| `modern` | `on` or `off`. For `map_settings` also `inherit`. |
| `kind` | `sector`, `wall` or `sprite` |
| `index` | The index of that object |
| `script` | Path of the script inside the project |
| `sprites` | For `save_prefab`: a list of sprite indexes |
| `x`, `y` | For `place_prefab`: where to place it |
| `sector` | For `place_prefab`: optional. The sector to prefer at that spot. |
| `floor_z` | For `place_prefab`: optional. The Z of the floor level to place on. |
| `overwrite` | `true` allows replacing an existing file |

Script components and prefabs need Modern Mode. See
[Modern Mode](projects.md#modern-mode).

## Game and server tools

### game_status

Reads what the program is doing. No arguments.

| Field in the answer | Meaning |
| --- | --- |
| `mode` | `menu`, `game`, `editor`, `playtest` or `build_tools` |
| `menu` | `true` while a menu is open |
| `headless` | `true` when the program runs without a window |
| `width`, `height` | Size of the picture in pixels |
| `episode`, `level`, `level_name`, `user_map` | The level being played |
| `player` | The local player's `x`, `y`, `z`, `angle`, `sector` and `health` |

The last two rows are present only during a game.

### server_status

Reads the multiplayer state. No arguments. The answer has everything from
`game_status`, and:

| Field in the answer | Meaning |
| --- | --- |
| `role` | `offline`, `host`, `client` or `dedicated` |
| `local_slot` | This machine's player number |
| `port` | The network port |
| `slots` | Number of player slots |
| `late_join`, `asynchronous`, `changing_map` | Server settings and state |
| `players` | For each connected player: `slot`, `name`, `host`, `spectator` and `bot` |

### console_execute

Runs one line in the game console. Server commands use the same console.

| Argument | Meaning |
| --- | --- |
| `command` | Required. One line of up to 4,096 characters. |

The answer holds the console text that the command produced, in the same form
as `console_output`.

Useful commands: `listsymbols` lists every command, and `help NAME` explains
one. On a server, `listplayers`, `changemap`, `mapcycle` and `kick` manage
the match.

### console_output

Reads recent console text.

| Argument | Meaning |
| --- | --- |
| `after` | Optional. A `cursor` value from an earlier answer. Only newer lines are returned. |
| `limit` | How many lines to read, from 1 to 80. The default is 32. |

The answer has `lines`, `cursor` (pass it as `after` next time) and `latest`.
The program keeps the last 256 lines.

### ui_input

Sends keyboard, mouse or text input to the game window, as if a person had
made it. The game's window must have the keyboard focus.

| Argument | Meaning |
| --- | --- |
| `events` | Required. A list of 1 to 32 events. |

| Event `type` | Other fields |
| --- | --- |
| `key_down`, `key_up` | `key`: an SDL key name such as `F5`, `Return`, `Escape` or `Left Shift` |
| `text` | `text`: up to 31 characters to type |
| `move` | `x`, `y`: pointer position in the window. Optional `dx`, `dy`: relative movement. |
| `button_down`, `button_up` | `button`: `left`, `middle` or `right`. `x`, `y`: pointer position. |
| `wheel` | `dy`: wheel steps, from −32 to 32 |

Send a key or button release in a later call than the press, so that the game
has time to see the key held down.

## Edit operations

These are the values of `operation` for `editor_edit`.

| Operation | Arguments | What it does |
| --- | --- | --- |
| `room` | `points`: a list of at least three `[x, y]` pairs | Adds a sector with that outline. An edge with the same two end points as an existing solid wall becomes a portal. |
| `extend` | `index`: a solid wall. Optional `depth`, from 128 to 32768. The default is 1536. | Adds a rectangular room behind the wall |
| `split` | `index`: a wall | Adds a vertex at the middle of the wall |
| `move_vertex` | `index`: the wall that starts at the vertex. `x`, `y`. | Moves the vertex, together with the corners joined to it |
| `sprite` | `tile`, `x`, `y`. Optional `sector` to prefer. | Places a sprite on the floor |
| `move_sprite` | `index`: a sprite. `x`, `y`. | Moves the sprite. It keeps its height above the floor. |
| `start` | `x`, `y`. Optional `sector` and `angle`. | Sets the player start |
| `set` | `kind`: `sector`, `wall`, `sprite` or `start`. `index` (not for `start`). `properties`: names and whole numbers. | Sets properties |
| `delete` | `kind`: `sector` or `sprite`. `index`. | Deletes the object |
| `batch` | `edits`: a list of 1 to 64 operations | Applies several operations as one change |

### Batch edits

Use `batch` for changes that belong together, such as a room with its
furniture.

```json
{
  "revision": 12,
  "operation": "batch",
  "label": "Add storage room",
  "edits": [
    { "operation": "extend", "index": 1, "depth": 1024 },
    { "operation": "sprite", "tile": 28, "x": 2560, "y": 0 }
  ]
}
```

On a new map, this adds a room behind the right-hand wall of the starter room
and places a shotgun in it. Replace `12` with the revision from your last
read.

- Each entry of `edits` uses the arguments of its own operation. Entries do
  not carry a `revision`.
- The entries run in order. An index in a later entry refers to the map as it
  is after the earlier entries.
- If any entry fails, the whole batch is dropped and nothing changes. The
  message names the entry that failed.
- A successful batch is one revision and one undo step.
- A batch is never merged with other people's edits. If the map changed since
  the given revision, the batch is refused.

### Properties for set

`set` changes the listed properties and leaves the others alone. Every name
must be one of those below and every value must be a whole number in range.
Otherwise nothing is changed.

| Kind | Property names |
| --- | --- |
| `sector` | `floor_z`, `ceiling_z`, `floor_tile`, `ceiling_tile`, `floor_flags`, `ceiling_flags`, `floor_slope`, `ceiling_slope`, `floor_shade`, `ceiling_shade`, `floor_palette`, `ceiling_palette`, `floor_pan_x`, `floor_pan_y`, `ceiling_pan_x`, `ceiling_pan_y`, `visibility`, `lotag`, `hitag`, `extra` |
| `wall` | `tile`, `overlay_tile`, `flags`, `shade`, `palette`, `repeat_x`, `repeat_y`, `pan_x`, `pan_y`, `lotag`, `hitag`, `extra` |
| `sprite` | `z`, `tile`, `flags`, `angle`, `shade`, `palette`, `repeat_x`, `repeat_y`, `offset_x`, `offset_y`, `clip_distance`, `status`, `lotag`, `hitag`, `extra` |
| `start` | `angle`, `z` |

For example, this darkens the floor of sector 2:

```json
{
  "revision": 13,
  "operation": "set",
  "kind": "sector",
  "index": 2,
  "properties": { "floor_shade": 16 }
}
```

The positions of walls and sprites are not properties. Use `move_vertex`,
`move_sprite` and `start` to change them.

## A good working order for an assistant

1. Call `editor_status` to check the connection and see who else is there.
2. Call `editor_inspect` with `kind` set to `map`, then read the sectors and
   walls of the area to work on.
3. Call `editor_presence` to point at that area, so people can see where the
   assistant is about to work.
4. Call `editor_edit` with the latest `revision`.
5. If the edit is refused, read the map again before trying again.
6. Read the changed records to confirm the result. In the game, run
   `editor_check` and try the map with `editor_control` and the action
   `playtest`.
7. Call `editor_presence` with `clear` set to `true` when the work is done.
