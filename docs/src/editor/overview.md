# Map editor overview

NNBuild has a map editor built into the game. You draw a room, press **F5**, and
walk through it a moment later. This page explains what the editor makes, how
to open it, and what each part of its window is for.

## What you can make

**Classic maps.** The editor opens and saves version 7 `.MAP` files, the map
format of Duke Nukem 3D and the games built on it. You choose which game a map
is for. The choices are:

- Duke Nukem 3D 1.3D
- Duke Nukem 3D Atomic / Plutonium
- NAM
- NAPALM
- WWII GI

**Projects.** A project is a folder that keeps your maps, scripts, textures and
sounds together with a small settings file. You can pack a project into one ZIP
file for players. See [Projects](projects.md).

**Stand-alone games.** A project can bring its own rules, art and palette, so it
runs without the data files of another game. The example game in
`examples/ashline-relay/` is made this way. See
[Stand-alone games](projects.md#stand-alone-games).

The editor also includes:

- [everyday editing tools](editing-maps.md) with undo and redo,
- [building tools](building-tools.md) for pillars, stairs and catwalks,
- [linked mechanisms](mechanisms.md) such as lifts and doors,
- [playtesting and a map check](playtesting.md),
- [live editing](live-editing.md) with other people,
- [control by AI assistants](ai-and-mcp.md),
- the [classic Build tools](classic-build-tools.md) for advanced work.

## Requirements

- **SDL2 and OpenGL.** The editor needs a build of the program that uses SDL2
  and OpenGL, and OpenGL must be working. It does not open in a headless
  (no-window) session.
- **A full game.** The editor does not open with shareware game data.
- **A local session.** The editor does not open while you are in a multiplayer
  game.

If the game is running in an 8-bit video mode, the editor switches to a 32-bit
window of at least 1024 by 680 pixels. Your video settings come back when you
return to the title menu.

## Open the editor

### From the title menu

1. Start the game.
2. Choose **MAP EDITOR** in the title menu. It sits between **CREDITS** and
   **QUIT**.

The menu entry is hidden when the editor cannot open, and when the running game
is Ion Fury.

### From the command line

Add `--editor` to start directly in the editor. To open a map at once, put a
`.map` path straight after `--editor`.

```sh
./nnbuild -gamegrp /path/to/DUKE3D.GRP --editor
./nnbuild -gamegrp /path/to/DUKE3D.GRP --editor "My Map.map"
```

To open a project for editing, add `--project` with the project folder:

```sh
./nnbuild --editor --project examples/ashline-relay
```

The editor then opens the project's entry map. See [Projects](projects.md).

If the editor cannot open, the program writes this message to its log and
exits:

```text
The map editor requires a local Duke3D game with SDL2 and OpenGL.
```

### Leave the editor

Choose **File → Return to title**. If the map has unsaved changes, the editor
asks whether to save, discard or cancel.

## The window

The window is divided into fixed panels. There is a screenshot on the
[Live editing](live-editing.md) page.

| Panel | Where | What it is for |
| --- | --- | --- |
| **Toolbar** | Top | The **File** and **Build** menus, the **Session** button, **Undo** and **Redo**, the map name, **Playtest F5** and **Spawn...**. A second row holds the tools: **Select Q**, **Room R**, **Sprite E**, **Player Start T**, **Batch Select**, and the **Snap** grid size. |
| **Hierarchy** | Left | A list of everything in the map: the player start, every sector and every sprite. Type in **Search scene** to filter it. Click a row to select the object. Double-click to bring it into view. |
| **Scene** | Centre | The map itself. Buttons at the top switch between **Plan** (a floor plan), **3D** (a textured view you fly through) and **Section** (a side view). **Frame** brings the selection into view. |
| **Inspector** | Right | The properties of the selected object: heights, textures, positions, tags and flags. |
| **Assets** | Bottom | Three tabs. **Tiles** shows the game's pictures. **Entities** shows common objects to place. **Project** shows the files of the open project. |
| **Status line** | Very bottom | The latest message. Problems are shown in red. On the right it shows the number of sectors, walls and sprites in the map. |

A star after the map name in the toolbar means the map has unsaved changes.

Some tools open a small extra window on the right side: the mechanism presets,
**Build stairs**, **Build catwalk**, **Map check** and **Change history**.

## Words you will meet

Build-engine maps are made of a few simple things. The editor uses these
names for them.

| Word | Meaning |
| --- | --- |
| **Sector** | One room or area of floor. A sector is a closed outline with one floor height and one ceiling height. |
| **Wall** | One straight edge of a sector's outline. |
| **Solid wall** | A wall with nothing behind it. |
| **Portal** | A wall shared by two sectors. The player can see and walk through it. In Plan, portals are drawn in teal and solid walls in light grey. |
| **Vertex** | A corner point where two walls meet. |
| **Sprite** | An object placed in a sector: an enemy, a weapon, a switch, a decoration, or an invisible marker that controls an effect. |
| **Entity** | The editor's word for a sprite that does something in the game, such as an enemy or a pickup. |
| **Tile** | A numbered picture from the game's art. Walls, floors, ceilings and sprites each show a tile. |
| **Tag** | A number stored on a sector, wall or sprite that tells the game how it behaves. Each object has two: **LoTag** and **HiTag**. A switch and the thing it controls usually share a tag number. |
| **Player start** | The place and direction where the player begins the map. |

Two kinds of numbers need a short explanation.

**Heights.** The **Z** fields in the Inspector hold the map's raw height
values. Z grows downward, so a lower number is higher up. The building tools
and the editor's messages use *height units*. One height unit is 256 in the Z
fields. For example, "48 units of headroom" means a difference of 12,288
between **Floor Z** and **Ceiling Z**.

**Angles.** A full turn is 2,048 steps. In Plan, angle 0 points right, 512
points down, 1,024 points left and 1,536 points up.

## Your first room in five minutes

1. **Open the editor.** Choose **MAP EDITOR** in the title menu. A new map
   called `Untitled.map` opens. It already has one room and a player start in
   the middle. The status line says "Starter room ready".
2. **Look at the plan.** Scroll the mouse wheel to zoom. Hold the right mouse
   button and drag to pan.
3. **Look in 3D.** Click **3D** above the Scene. Hold the right mouse button
   and move the mouse to look around. While you hold it, press **W**, **A**,
   **S** and **D** to fly. Click **Plan** to go back.
4. **Add a second room.** Hold **Ctrl** and click one wall of the room. The
   Inspector now shows that wall. Click **Extend room**. A new room appears
   behind the wall, joined to the first one.
5. **Change the floor.** The new room is selected. Open the **Tiles** tab in
   Assets, click a picture you like, and click **Apply tile**. To change the
   ceiling instead, click **Ceiling** in the Inspector first.
6. **Place an enemy.** Open the **Entities** tab and click **Trooper**. Click
   inside a room to place it. Press **Q** to go back to the Select tool.
7. **Play it.** Press **F5**. The game starts in your map. Press **F5** again
   to return to the editor.
8. **Save.** Press **Ctrl+S**. Choose a folder, type a name and click **Save
   map**. The editor adds `.map` to the name for you.

## Where to go next

- [Editing maps](editing-maps.md): the everyday tools and all shortcuts.
- [Building tools](building-tools.md): pillars, stairs, catwalks and batch
  edits.
- [Linked mechanisms](mechanisms.md): lifts, doors, traps and light switches.
- [Playtesting](playtesting.md): test runs and the map check.
- [Projects](projects.md): folders, ZIP files, stand-alone games, scripts and
  prefabs.
- [Live editing](live-editing.md): edit one map with other people.
- [AI and MCP](ai-and-mcp.md): let an AI assistant work in the editor.
- [Classic Build tools](classic-build-tools.md): the original-style editor.
