# Editing maps

This page covers the tools you use all the time: looking around, selecting,
drawing rooms, placing objects, changing properties, and saving. A complete
[shortcut table](#shortcuts) is at the end.

If words such as *sector*, *wall*, *sprite*, *tile* or *tag* are new to you,
read [Words you will meet](overview.md#words-you-will-meet) first.

## Views

Three buttons above the Scene choose the view: **Plan**, **3D** and
**Section**. The Section view is described in
[Building tools](building-tools.md#the-section-view).

### Plan

Plan is a floor plan seen from above. You draw and move things here.

| What you see | Meaning |
| --- | --- |
| Light grey line | A solid wall |
| Teal line | A portal (a wall shared by two sectors) |
| Orange | The selected object |
| Teal dot with a short line | A sprite and the direction it faces |
| Blue rectangle | A catwalk |
| Orange arrow | The player start and its direction |

To move around in Plan:

- Scroll the mouse wheel to zoom in and out around the pointer.
- Hold the right or middle mouse button and drag to pan.
- Press **F**, or click **Frame**, to fit the selected object's sector in the
  view. With nothing selected, the whole map is fitted.

### 3D

3D shows the map with its textures, as the player will see it.

- Hold the right mouse button and move the mouse to look around.
- While you hold the right button, press **W** and **S** to fly forward and
  back, **A** and **D** to move sideways, **Q** to rise and **E** to sink.
- Hold **Shift** as well to fly faster.
- Click a wall, a floor, a ceiling or a sprite to select it. Clicking a floor
  or a ceiling selects its sector and sets the Inspector to that surface.
- Press **F**, or click **Frame**, to move the camera to the middle of the
  selected sector. With nothing selected, the camera goes to the player start.

The camera cannot leave the map.

The drawing tools work in Plan. If you choose **Room**, **Sprite** or **Player
Start** while in 3D, the Scene switches to Plan. Opening 3D switches back to
the **Select** tool.

## The snap grid

The **Snap** list in the toolbar sets the grid size: **Off**, 32, 64, 128, 256,
512 or 1024. It starts at 128.

New corners, new sprites and the player start land on the grid. When you drag
something, it moves in whole grid steps. Choose **Off** for free placement.

## Select

Use the **Select** tool (**Q**). In Plan, what you get depends on where you
click.

| Action | Selects |
| --- | --- |
| Click a sprite or the player start | That sprite or the start |
| Click a corner | The vertex |
| Click a wall line | The wall |
| Click open floor | The sector |
| **Ctrl**-click | A wall only. Nearby sprites and vertices are ignored. |
| **Shift**-click | The sector under the pointer. Everything else is ignored. |

A portal is really two walls, one for each sector. Click just inside a sector,
close to the portal, to select that sector's side.

You can also select by clicking a row in the **Hierarchy**, or by clicking in
the 3D view.

On macOS, the Command key also works where this guide says **Ctrl**.

## Move

1. Choose the **Select** tool and make sure **Batch Select** is off.
2. Press the left mouse button on the object in Plan.
3. Drag, then release.

| You drag | What moves |
| --- | --- |
| A vertex | That corner |
| A wall | Both ends of the wall |
| A sector | The whole outline, the sprites inside it, and the player start if it is inside |
| A sprite | The sprite. It keeps its height above the floor. |
| The player start | The start |

Corners shared by neighbouring rooms move together, so rooms stay joined.

Each drag is one undo step. The editor refuses a move that would make walls
cross, turn a sector inside out, or leave a sprite outside its sector. The
status line tells you why and the map stays as it was.

To place something at exact numbers, type **X** and **Y** in the Inspector and
press **Enter**.

## Draw a room

1. Press **R**, or click **Room R**.
2. Click each corner of the room in Plan. If you click close to an existing
   corner, the new corner locks onto it.
3. To close the outline, click the first corner again or press **Enter**. A
   room needs at least three corners.

Press **Escape** to throw away an unfinished outline.

The Room tool stays active so you can draw another room. Press **Q** to return
to the Select tool.

A new room gets a default floor, ceiling and wall tile. Its floor is at Z 0 and
its ceiling is 64 height units above.

**Joining rooms.** If an edge of the new room has exactly the same two end
points as a solid wall of another room, the two walls become a portal. Use the
snap grid, or click on existing corners, to make the end points match.

If the outline crosses itself or cannot be closed, the status line shows:

```text
Close a simple outline with at least three corners. Shared edges must have matching endpoints.
```

## Extend a room from a wall

This is the quickest way to add a joined room.

1. Hold **Ctrl** and click a solid wall in Plan. You can also click the wall in
   3D.
2. In the Inspector, type a **Room depth** and press **Enter**. The value can
   be from 128 to 32768. It starts at 1536.
3. Click **Extend room**.

A rectangular room appears behind the wall. It copies the floor height, ceiling
height, tiles and shades of the room it came from. The wall you selected
becomes a portal.

**Extend room** is greyed out for a portal, because there is already a room
behind it.

## Split a wall

1. Select a wall.
2. Click **Split at midpoint** in the Inspector.

The wall becomes two walls with a new vertex in the middle. The new vertex is
selected, so you can drag it straight away to bend the wall. Splitting a
portal splits both of its sides.

A few walls have no usable middle point. The status line then says "This wall
cannot be split at its midpoint."

## Delete

Select a sector or a sprite. Then press **Delete**, or click **Delete sector**
or **Delete sprite** at the bottom of the Inspector.

- Deleting a sector also deletes the sprites inside it. Portals that led to it
  become solid walls.
- If the player start was in the deleted sector, place a new one before you
  save.
- Undo brings back the sector and its sprites.

Single walls and vertices cannot be deleted here. For work such as joining
or splitting sectors, use the [classic Build tools](classic-build-tools.md).

## Place sprites and entities

1. Choose what to place.
   - Open the **Entities** tab in Assets and click an entity. The editor
     switches to the **Sprite** tool for you.
   - Or open the **Tiles** tab, click a tile, and press **E**.
2. Click inside a sector in Plan.
3. Click again to place more. Press **Q** when you are done.

A new sprite stands on the floor at the point you clicked. To raise it, see
[Sprite heights](building-tools.md#sprite-heights).

The **Entities** tab offers these objects. The numbers are Duke Nukem 3D tile
numbers.

| Entity | Tile | Entity | Tile |
| --- | --- | --- | --- |
| Trooper | 1680 | Keycard | 60 |
| Pigcop | 2000 | Effector | 1 |
| Commander | 1820 | Activator | 2 |
| Shotgun | 28 | Touchplate | 3 |
| RPG | 23 | Respawn | 9 |
| Shells | 49 | Co-op start | 1405 |
| First Aid | 53 | Exit switch | 142 |

For any other object, find its tile in the **Tiles** tab and place that. When
you make a map for another game, use that game's own tiles and tags.

## Set the player start

1. Press **T**, or click **Player Start T**.
2. Click inside a sector in Plan.

The sector needs at least 48 height units between floor and ceiling. The start
is placed at eye height, 40 units above the floor.

Select the start to change its **Angle** in the Inspector. Click **Preview from
start** to see the 3D view from that spot.

You can also drag the start with the Select tool.

## The Assets panel

### Tiles

The **Tiles** tab shows the pictures of the game that is running.

- Type in **Search name or tile ID** to find a tile by name or number.
- Click a tile to make it the current tile. Its number is shown beside
  **Apply tile**.
- Click **Apply tile** to put the current tile on the selection. You can also
  double-click a tile.

What the tile is applied to depends on the selection:

| Selection | The tile goes on |
| --- | --- |
| Sector | The floor, or the ceiling if **Ceiling** is chosen in the Inspector |
| Wall | The wall |
| Sprite | The sprite |
| A [batch selection](building-tools.md#batch-selection) | Every wall and sprite in the group |

### Entities

The **Entities** tab holds the objects listed
[above](#place-sprites-and-entities). Click one to start placing it.

### Project

The **Project** tab lists the files of the open project. See
[Projects](projects.md).

## The Inspector

The Inspector shows the properties of the selected object.

- Type a number and press **Enter** to apply it.
- Tick boxes and buttons apply at once.
- Each change is one undo step.
- A folded section stays open or closed while you select other objects of the
  same kind.

### Sector

| Field | Meaning |
| --- | --- |
| **Floor Z**, **Ceiling Z** | Heights. A lower number is higher up. |
| **Floor** / **Ceiling** | Chooses which surface the fields below describe |
| **Tile** | The picture on that surface |
| **Shade** | Brightness, from −128 to 127. Lower is brighter. |
| **Palette** | Colour set, from 0 to 255 |
| **Parallax sky** | Draws the surface as sky |
| **Slope**, **Slope amount** | Tilts the surface |

The section **Tags and surface flags** adds **LoTag**, **HiTag**, **Extra**,
**Flags**, **Pan X**, **Pan Y** and **Visibility**. Tick **End level on entry**
to make the level end when the player walks into the sector. Such a sector is
marked "(Exit)" in the Hierarchy.

### Wall

The top line says "Solid boundary", or "Portal to sector N".

| Field | Meaning |
| --- | --- |
| **Tile**, **Shade**, **Palette** | As for a sector |
| **Blocking** | Stops the player from passing |
| **Mask overlay**, **Overlay tile** | Draws a second picture over the wall. **Overlay tile** chooses the picture. |

The section **Mapping and tags** adds **X repeat**, **Y repeat**, **Pan X**,
**Pan Y**, **Flags**, **LoTag**, **HiTag** and **Extra**. Repeat and pan
stretch and shift the picture on the wall.

Below that are **Split at midpoint**, **Room depth** and **Extend room**.

### Vertex

**X** and **Y**. Neighbouring corners at the same point move with it.

### Sprite

| Field | Meaning |
| --- | --- |
| **X**, **Y**, **Z** | Position |
| **Above floor** | Height above the floor, or above the catwalk the sprite stands on, in height units |
| **Angle** | Direction, from 0 to 2047 |
| **Tile**, **Shade**, **Palette** | As for a sector |
| **Blocking** | The player cannot walk through it |

The section **Sprite flags and tags** adds **LoTag**, **HiTag**, **Extra**,
**X repeat**, **Y repeat**, **Offset X**, **Offset Y**, **Clip distance**,
**Status** and **Flags**. The repeat values set the sprite's size.

### Player Start

**X**, **Y**, **Z**, **Angle**, and the **Preview from start** button.

## Undo and redo

- **Undo**: **Ctrl+Z**, or the **Undo** button.
- **Redo**: **Ctrl+Shift+Z** or **Ctrl+Y**, or the **Redo** button.

The editor keeps the last 48 changes. Undo clears the selection. Opening
another map, or starting a new one, empties the history.

**Build → Change history** lists recent changes with the newest first. Open a
change to see who made it and which objects it touched. Click an object in the
list to bring it into view.

In a live session, undo and redo work on the shared map. See
[Live editing](live-editing.md#change-history-and-shared-undo).

## Open and save

### New map

Choose **File → New map**, or press **Ctrl+N**. You get the starter room.

### Open a map

1. Choose **File → Open map...**, or press **Ctrl+O**.
2. Browse to the file. Click **Up** for the parent folder. Click a folder to
   enter it. The list shows folders and `.map` files.
3. Double-click a map, or click it once and then click **Open map**.

Other ways to open a map:

- Type a file name or a full path in the box and press **Enter**.
- Open **Maps in game data** in the dialog. It lists the levels that come with
  the game. Click a level, then click **Open map**.
- Drag a `.map` file from your file manager onto the editor window.

A map opened from the game data has no file of its own yet. The first time you
save it, the editor asks for a name.

The **Import as** list sets which game the map is for. Leave it on **Inherit**
unless the map belongs to a different game than the one you are running. See
[Game targets](projects.md#game-targets).

### Save

- **File → Save**, or **Ctrl+S**, saves to the current file.
- **File → Save as...**, or **Ctrl+Shift+S**, saves under a new name. You then
  keep working on the new file.

In the save dialog, the editor adds `.map` if you leave it out. If the file
already exists, it asks **Replace map?** first.

A map can only be saved when it is complete enough to play:

- it has at least one sector,
- the player start is inside a sector.

If the map uses project features, a small settings file is saved beside it.
See [Per-map settings](projects.md#per-map-settings).

### Unsaved changes

If you start a new map, open another one, join a session or leave the editor
with unsaved changes, the editor offers **Save and continue**, **Discard
changes** or **Cancel**.

## Map limits

The editor saves classic version 7 maps. These limits come from that format.

| Item | Limit |
| --- | --- |
| Sectors | 1,024 |
| Walls | 8,192 |
| Sprites | 4,096 |
| Coordinates | From −524,288 to 524,288 on X and Y |
| Tile numbers | Below 3,840 for Duke Nukem 3D 1.3D. Below 5,120 for the other game targets. |
| Script components | 1,024 per map |

The editor opens version 7 maps only. A map that uses flags from an extended
map format is refused with a message that names the problem.

## Shortcuts

### Keyboard

| Key | Action |
| --- | --- |
| **F5** | Start a playtest. During a playtest, return to the editor. |
| **Ctrl+N** | New map |
| **Ctrl+O** | Open a map |
| **Ctrl+S** | Save |
| **Ctrl+Shift+S** | Save as |
| **Ctrl+Z** | Undo |
| **Ctrl+Shift+Z**, **Ctrl+Y** | Redo |
| **Q** | Select tool |
| **R** | Room tool |
| **E** | Sprite tool |
| **T** | Player Start tool |
| **F** | Frame the selection |
| **Delete** | Delete the selected sector or sprite |
| **Enter** | Close the room or pillar outline. In a number field, apply the number. |
| **Escape** | Throw away the outline being drawn. In a dialog, cancel. |

On macOS, the Command key also works where the table says **Ctrl**.

Shortcuts are ignored while you type in a text box, while a menu or dialog is
open, and during a drag.

### Keyboard in 3D

These keys work while you hold the right mouse button. During that time they
do not switch tools.

| Key | Action |
| --- | --- |
| **W**, **S** | Fly forward, back |
| **A**, **D** | Move left, right |
| **Q**, **E** | Rise, sink |
| **Shift** | Fly faster |

### Mouse in Plan

| Action | Result |
| --- | --- |
| Wheel | Zoom |
| Right-drag or middle-drag | Pan |
| Click | Select, or use the current tool |
| **Ctrl**-click | Select a wall |
| **Shift**-click | Select a sector |
| Drag a selected object | Move it |
| Right-click a floor | Open the menu with **Play From Here** |

### Mouse in 3D

| Action | Result |
| --- | --- |
| Hold right button and move | Look around |
| Click | Select a wall, sprite, floor or ceiling |
| Right-click a floor or catwalk without moving | Open the menu with **Play From Here** |

### Mouse elsewhere

| Where | Action | Result |
| --- | --- | --- |
| Section view | Click a floor or catwalk | Select it and show only that level in Plan |
| Hierarchy | Click | Select |
| Hierarchy | Double-click | Select and bring into view |
| Tiles tab | Click | Choose the tile |
| Tiles tab | Double-click | Apply the tile to the selection |
| Entities tab | Click | Choose the entity and switch to the Sprite tool |
| File dialog | Click a folder | Enter the folder |
| File dialog | Double-click a map | Open it, or save over it |
