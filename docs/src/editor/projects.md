# Projects

A project is a folder that keeps everything for a mod or a game in one place:
maps, scripts, art and sounds, plus a small settings file. You can pack the
folder into one ZIP file and give it to players.

This page uses the example game in `examples/ashline-relay/` to show how the
parts fit together.

## The project folder

A folder is a project when it has a file called `nnbuild.project.json` at
its top level. Every other file in the folder is an *asset*. You may arrange
assets in subfolders as you like.

The example project looks like this:

```text
ashline-relay/
  nnbuild.project.json        project settings
  GAME.CON                    game rules (stand-alone games)
  tiles000.art                art (stand-alone games)
  palette.dat                 colours (stand-alone games)
  Tiles.tsv                   a list of tile numbers, for people to read
  README.md
  Maps/
    Ashline.map               the map
    Ashline.map.vbmap.json    settings for that map
  Scripts/
    Station.js                a script component
  Sounds/
    alarm.wav, relay.mid, ...
  Attribution/
    licence notices for the art
```

## Create or open a project

Choose **File → Project...**. The **Project settings** dialog opens. You can
also click **Project...** in the **Project** tab of the Assets panel.

### Create

1. Type the path of the folder in **Folder**.
2. Click **Create project**.

The folder is created if it does not exist. Its parent folder must exist. A
folder that already has project settings is refused.

A new project starts with these settings:

| Setting | Starts as |
| --- | --- |
| Project name | The folder's name |
| Base game | The game that is running |
| Modern Mode | Off |
| Entry map | None |

### Open

1. Type the path of the project folder in **Folder**.
2. Click **Open folder**.

To open a project when you start the program, use `--editor` with `--project`:

```sh
./nnbuild --editor --project examples/ashline-relay
```

Starting this way also loads the project's own files together with the game.
Use it when the project brings its own art.

### Change the settings

With a project open, the dialog shows:

| Control | What it does |
| --- | --- |
| **Project name** | Type a name and press **Enter**. |
| **Base game** | The [game target](#game-targets) for the maps of the project |
| **Enable Modern Mode** | Turns on [Modern Mode](#modern-mode) |
| **Entry map** | Shows the current [entry map](#entry-map) |
| **Asset file**, **Add asset** | Copies a file into the project |
| **Close project** | Closes the project. The files stay where they are. |

Each change is written to `nnbuild.project.json` at once.

A relative path in these dialogs starts from the folder in which you started
NNBuild.

## The settings file

`nnbuild.project.json` is a small text file. This is the one from the
example project:

```json
{
  "name": "Ashline Relay",
  "target": "standalone",
  "entry": "Maps/Ashline.map",
  "modern": true,
  "version": 1
}
```

| Field | Type | Needed | Meaning |
| --- | --- | --- | --- |
| `version` | number | Yes | Must be `1`. |
| `name` | text | Yes | The project's name, from 1 to 128 characters. |
| `target` | text | Yes | The base game. One of the IDs under [Game targets](#game-targets), but not `inherit`. |
| `entry` | text | No | The map a player starts in, as a path inside the project that ends in `.map`. Leave it empty for none. |
| `modern` | true or false | No | `true` turns on Modern Mode. If the field is missing, Modern Mode is off. |

The editor writes this file for you. If you edit it by hand and it is no
longer valid, the project does not open and you see "The folder needs a valid
nnbuild.project.json."

### Paths inside a project

Paths in the settings files are written from the top of the project folder,
with forward slashes, for example `Maps/Ashline.map`.

A path is refused if it:

- starts or ends with a slash,
- contains `\`, `:`, `<`, `>`, `"`, `|`, `?` or `*`,
- has a part that ends in a dot or a space, such as `..`,
- has a part named `CON`, `NUL`, `PRN`, `AUX`, `COM1` to `COM9` or `LPT1` to
  `LPT9`.

Two files whose paths differ only in upper and lower case cannot both be in a
project that you export.

## Add assets

Use any of these:

- **Copy files into the folder** with your file manager. Then click
  **Refresh** in the **Project** tab.
- **Save a map into the folder** with **File → Save as...**.
- **Add asset.** In **Project settings**, type the path of a file in **Asset
  file** and click **Add asset**. The file is copied to the top level of the
  project under the same name. An existing file is not replaced.
- **Drag a file onto the editor window** while a project is open. The
  **Project settings** dialog opens with the path filled in. Click **Add
  asset**. A dragged `.map` file is opened for editing instead.

### The Project tab

The **Project** tab of the Assets panel lists every file and folder of the
project.

| Control | What it does |
| --- | --- |
| **Project...** | Opens **Project settings** |
| **Map settings...** | Opens the settings of the current map |
| **Search assets** | Filters the list |
| **Refresh** | Reads the folder again |
| **Set entry map** | Appears when a map is selected. Makes it the entry map. |
| **Place Prefab** | Appears when a prefab is selected and Modern Mode is on |

Double-click a map to open it. Double-click a prefab to place it.

Each row starts with a small badge that shows which game the file is for:
**D3**, **DA**, **N**, **NP**, **GI** or **S**. See the table under
[Game targets](#game-targets). The entry map is marked "(Entry)" and prefabs
are marked "(Prefab)".

## Entry map

The entry map is the map a player starts in when they run the project.

1. Open the **Project** tab.
2. Click a map in the list.
3. Click **Set entry map**.

If a project has no entry map, running it starts the first map found in the
folder. A player can also name a map with
[`--project-map`](#how-a-player-runs-a-project).

## Game targets

A *game target* says which game a map is made for. These are all of them:

| ID | Name in the editor | Badge | Highest tile number |
| --- | --- | --- | --- |
| `duke13` | Duke Nukem 3D 1.3D | D3 | 3,839 |
| `duke_atomic` | Duke Nukem 3D Atomic / Plutonium | DA | 5,119 |
| `nam` | NAM | N | 5,119 |
| `napalm` | NAPALM | NP | 5,119 |
| `ww2gi` | WWII GI | GI | 5,119 |
| `standalone` | Standalone | S | 5,119 |
| `inherit` | Inherit | | Per-map only. The map uses the project's base game. |

The target affects four things:

- **Tile range.** A map cannot be saved or exported if it uses a tile number
  above the target's limit.
- **Mechanisms.** The [mechanism presets](mechanisms.md), the link display and
  the switch check work for `duke13` and `duke_atomic`.
- **Game data.** When a player runs the project, NNBuild looks for that game's
  data files. For `duke_atomic` it accepts the Atomic Edition, the Plutonium
  Pak and World Tour.
- **Stand-alone launch.** `standalone` makes the project run from its own
  files. See [Stand-alone games](#stand-alone-games).

Changing the target does not change the map. Tile numbers and tags stay as
they are, so use the target game's own tiles and tags when you build.

You set the target in three places:

| Where | Applies to |
| --- | --- |
| **Base game** in **Project settings** | Every map of the project that does not say otherwise |
| **Base game** in **Map settings** | The current map |
| **Import as** in the **Open map** dialog | The map you are opening |

With no project open, a map is checked against the game that is running.

The Tiles tab and the playtest always use the game that is running. To see and
test another game's art, start NNBuild with that game's data.

## Per-map settings

A map can override the project's settings. Choose **File → Map settings...**.

| Control | What it does |
| --- | --- |
| **Base game** | **Inherit**, or one game target for this map |
| **Modern Mode** | **Inherit**, **Off** or **On** for this map |
| **Object script**, **Attach to selected object** | Attaches a script to the selected object. Shown when Modern Mode is on. |
| **Detach** | Removes one script attachment from the list |
| **MAP**, **Export MAP** | Writes a plain classic map. See [Export a classic map](#export-a-classic-map). |

The line under the lists shows the game that is in effect for the map.

Map settings belong to the map. Changing them marks the map as unsaved. They
are written when you save the map.

### The map settings file

The settings are stored beside the map in a file with the map's full name plus
`.vbmap.json`. For `Maps/Ashline.map` this is `Maps/Ashline.map.vbmap.json`.

The file is written only when the map has something to store: a game target of
its own, a Modern Mode choice, or script components. A plain classic map has
no such file.

This is a shortened copy of the one in the example project:

```json
{
  "target": "inherit",
  "scripts": [
    {
      "kind": "sector",
      "script": "Scripts/Station.js",
      "index": 29
    },
    {
      "kind": "sprite",
      "script": "Scripts/Station.js",
      "index": 15
    }
  ],
  "version": 1
}
```

| Field | Type | Meaning |
| --- | --- | --- |
| `version` | number | Must be `1`. |
| `target` | text | A game target ID, or `inherit` |
| `modern` | true or false | `true` is **On** and `false` is **Off**. Leave the field out for **Inherit**. |
| `scripts` | list | The script components of the map. At most 1,024. |
| `scripts[].kind` | text | `sector`, `wall` or `sprite` |
| `scripts[].script` | text | Path of the script inside the project. It must end in `.js`, `.ts`, `.mjs` or `.mts`. |
| `scripts[].index` | number | The number of the sector, wall or sprite. `-1` means the object was lost and the script is unassigned. |

Keep this file together with its map when you copy or move the map.

## Import and export

### Export a classic map

Use this to hand out a single map without project data.

1. Choose **File → Map settings...**.
2. Type the destination in **MAP**.
3. Click **Export MAP**.

The editor adds `.map` if you leave it out. It writes only the map, with no
settings file beside it. An existing file is not replaced.

### Export a project ZIP

1. Save the current map.
2. Choose **File → Project...**.
3. Type the path of the ZIP file in **ZIP**.
4. Click **Export ZIP**.

The ZIP holds every file and folder of the project. Before writing it, the
editor checks that the entry map exists and that every map in the project is
valid for its game target. If a map fails, the message names it.

An existing ZIP file is not replaced. Choose a new name.

A project ZIP can hold up to 16,384 files and folders.

### Import a project ZIP

1. Choose **File → Project...**.
2. Type the path of the ZIP file in **ZIP**.
3. Type the path of a new folder in **Folder**. The folder must not exist yet.
   Its parent must exist.
4. Click **Import ZIP into folder**.

The project is unpacked and opened. You can also drag a project ZIP onto the
editor window. This opens the dialog with **ZIP** filled in.

A ZIP is accepted as a project when `nnbuild.project.json` is at the top
level of the archive, not inside a subfolder.

## How a player runs a project

A player needs NNBuild and your project as a folder or as a ZIP.

| The player has | Command |
| --- | --- |
| A project folder | `./nnbuild --project "/path/to/My Project"` |
| A project ZIP | `./nnbuild "My Project.zip"` |
| A project ZIP, written out in full | `./nnbuild --project "My Project.zip"` |

**Drag and drop.** Dragging the ZIP onto the program's icon starts it the same
way, because the system passes the ZIP as the first argument.

**Another map.** Add `--project-map` with a path inside the project to start
in a different map:

```sh
./nnbuild "My Project.zip" --project-map "Maps/Outpost.map"
```

**Edit instead of play.** Add `--editor`:

```sh
./nnbuild --editor --project "/path/to/My Project"
```

What happens when a project starts:

- A ZIP is unpacked to a temporary folder. The folder is removed when the
  program closes.
- The project folder is added to the places where the game looks for files.
- The startup window and the logo are skipped.
- The entry map starts.

### Game data

A project for one of the classic games uses the player's own copy of that
game. If NNBuild cannot find the data, it stops with a message like this:

```text
This project needs NAM data. Add the data folder with -j or pass -gamegrp.
```

The player then adds the folder that holds the game data:

```sh
./nnbuild -j/path/to/game-data --project "/path/to/My Project"
```

A [stand-alone](#stand-alone-games) project does not need any of this.

### Scripts in a ZIP

Scripts inside a project ZIP do not run unless the player allows them:

```sh
./nnbuild --allow-project-scripts "My Project.zip"
```

Without the option, the project still starts, but its script components stay
off. A project that is run from a folder does not need the option.

## Stand-alone games

A stand-alone game does not use the data files of another game. Set **Base
game** to **Standalone**.

The top level of the project must then contain these three files:

| File | What it is |
| --- | --- |
| `GAME.CON` | The game's rules script |
| `tiles000.art` | The first art file |
| `palette.dat` | The colour palette |

If one is missing, the project does not start:

```text
A standalone project needs its own GAME.CON, tiles000.art, and palette.dat.
```

Add whatever else your `GAME.CON` uses, such as sounds, music and more art
files.

When a stand-alone project starts, NNBuild works from inside the project
folder, loads the project's `GAME.CON`, and does not load add-ons from the
autoload folder.

### The example: Ashline Relay

`examples/ashline-relay/` is a one-map stand-alone game.

- `nnbuild.project.json` sets the target to `standalone`, the entry map to
  `Maps/Ashline.map`, and Modern Mode to on.
- `GAME.CON`, `tiles000.art` and `palette.dat` are the three required files.
  The art and sounds come from Freedoom. The notices are in `Attribution/`.
- `Scripts/Station.js` is one script. The map's settings file attaches it to
  four sectors and four sprites.
- `Tiles.tsv` lists which picture is at which tile number. It is a reference
  for people. The program does not read it.

Run it from the top of the source tree:

```sh
./nnbuild --project examples/ashline-relay
```

Open it in the editor:

```sh
./nnbuild --editor --project examples/ashline-relay
```

Run it from an exported ZIP, with its scripts allowed:

```sh
./nnbuild --allow-project-scripts --project "/path/to/Ashline Relay.zip"
```

## Modern Mode

Modern Mode turns on the features that only NNBuild has:

- [script components](#script-components) on sectors, walls and sprites,
- [prefabs](#prefabs).

When you turn it on, the editor shows this warning and asks you to confirm:

```text
Modern Mode requires NNBuild. This project is incompatible with other source ports and the original game.
```

**For the whole project**

1. Choose **File → Project...**.
2. Tick **Enable Modern Mode**.
3. Click **Enable Modern Mode** in the warning.

**For one map**

1. Choose **File → Map settings...**.
2. Set **Modern Mode** to **On**, **Off** or **Inherit**. If your choice turns
   Modern Mode on for the map, the same warning appears.

The map's own choice wins. **Inherit** uses the project's setting. With no
project open, Modern Mode is off unless the map says **On**.

## Script components

A script component is a JavaScript or TypeScript file attached to a sector, a
wall or a sprite. It runs while the map is played and gives that object its
own behaviour. Modern Mode must be on.

How to write components is covered in
[Map script components](../modding/map-script-components.md). This section
shows how to attach them in the editor.

**Attach a component**

1. Select a sector, a wall or a sprite.
2. In the Inspector, find **Script components** and click **Add Component**.
3. Choose one of these:
   - **New TypeScript component...** or **New JavaScript component...**. Type
     a path inside the project, such as `Scripts/Door.ts`, and click **Create
     and Attach**. The editor writes a starter file with empty functions.
     This needs an open project.
   - An existing script from the list.

An object can have several components. Click **Remove** beside one to detach
it. Outside a live session, attaching and removing can be undone.

You can also attach by path. Open **File → Map settings...**, type the path in
**Object script**, and click **Attach to selected object**. The same dialog
lists every attachment of the map with a **Detach** button.

**What a component can contain**

| Function | When it runs |
| --- | --- |
| `onAttach(component)` | When the object becomes active. It may return a clean-up function. |
| `onUpdate(component, deltaSeconds)` | Every frame. Use it for visuals. |
| `onFixedUpdate(component, deltaSeconds)` | Every game tick. Use it for gameplay. |
| `onDetach(component)` | When the object or the level is unloaded, or the playtest stops |

`component` has four fields: `kind`, `index`, `map`, and `state`. Use `state`
to keep values for that one attachment.

**Good to know**

- A map can have up to 1,024 component attachments.
- Each playtest loads the script files again, so you can edit a script and
  press **F5** to try it.
- When you add or delete geometry, the editor keeps each attachment on its
  object. If it cannot, the attachment becomes unassigned and the
  [map check](playtesting.md#map-check) reports it.
- Players who run your project from a ZIP must allow scripts. See
  [Scripts in a ZIP](#scripts-in-a-zip).

## Prefabs

A prefab is a saved group of sprites that you can place again and again, such
as a desk with a lamp and a switch. It is stored in the project as a file
ending in `.vbprefab`. Prefabs need an open project with Modern Mode on.

**Create a prefab**

1. Select a sprite. For several, turn on
   [Batch Select](building-tools.md#batch-selection) and click each sprite.
2. Click **Create Prefab...** in the Inspector.
3. Type a path inside the project, such as `Prefabs/Desk.vbprefab`.
4. Click **Save Prefab**.

The editor adds `.vbprefab` if you leave it out. An existing file is not
replaced.

A prefab stores:

- where each sprite is, measured from the first sprite you selected,
- how high each sprite is above the floor,
- every property of each sprite,
- the script components attached to each sprite,
- the game target of the map it came from.

A `.vbprefab` file is JSON text. It holds `version` (always `1`), `target` (a
game target ID) and a `sprites` list. Each entry in the list has these number fields: `x`, `y`, `z`, `tile`,
`flags`, `palette`, `shade`, `blend`, `clip_distance`, `repeat_x`, `repeat_y`,
`offset_x`, `offset_y`, `angle`, `status`, `owner`, `velocity_x`,
`velocity_y`, `velocity_z`, `lotag`, `hitag` and `extra`. It also has a
`components` list with the paths of its scripts.

**Place a prefab**

1. Open the **Project** tab.
2. Double-click the prefab. You can also click it once and click **Place
   Prefab**. The toolbar shows "Place prefab".
3. Click in Plan. Small circles show where the sprites will land.
4. Click again for another copy. Press **Q** when you are done.

Each copy is one undo step. Its sprites are ordinary sprites that you can move
and change.

Rules:

- Every sprite of the prefab must land inside a sector.
- The script files that the prefab refers to must be in the project.
- In a [live session](live-editing.md), only the host can place a prefab that
  has script components.

To share prefabs, export the project as a ZIP. The prefab files and the
scripts travel with it.
