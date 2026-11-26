# Playtesting

A playtest runs your map in the real game without leaving the editor. This
page also covers the **Map check**, which looks for common mistakes.

## Start a playtest

1. Press **F5**, or click **Playtest F5** in the toolbar.
2. The game starts in your map as a normal single-player game. The words
   "PLAYTEST F5 TO EDIT" appear at the top of the screen for a few seconds.

Good to know:

- You do not need to save first. The playtest runs on a temporary copy, and
  your map file is not touched.
- The map must be complete enough to play. It needs at least one sector and a
  player start inside a sector. If something is missing, the status line says
  what.
- The playtest uses the game that is running now. To test a map made for
  another game, start NNBuild with that game's data.
- [Script components](projects.md#script-components) run during a playtest
  when Modern Mode is on for the map.

If the game cannot start the map, you are returned to the editor with this
message:

```text
Duke3D could not start this playtest. Check the start and sector geometry.
```

## Return to the editor

Use any of these:

- Press **F5** again.
- Press **Escape** to open the menu and choose **RETURN TO EDITOR**. Choose
  **RESUME PLAYTEST** to keep playing.
- Finish the level. Reaching the end of the level returns you to the editor.

The status line says "Playtest stopped". Your map, the undo history, the view
and the selection are as you left them.

## Choose a spawn

Click **Spawn...** beside the **Playtest** button to start somewhere other
than the map's player start.

| Entry | Starts you at |
| --- | --- |
| **Map start** | The map's player start |
| **DukeMatch start #N · Sector S** | A multiplayer start sprite whose LoTag is 0 |
| **Co-op start #N · Sector S** | A multiplayer start sprite whose LoTag is 1 |
| **Player start #N · Sector S** | A start sprite with any other LoTag |

N is the sprite's number and S is its sector. The list has one entry for each
player start sprite in the map. You add such sprites with the **Co-op start**
entity in the **Entities** tab. A new one has LoTag 0. Set its **LoTag** to 1
in the Inspector to make it a co-op start.

Choosing an entry starts the playtest at once. The map is not changed.

## Play From Here

Use this to test one room without walking there.

1. Right-click a floor in Plan or in 3D. Do not drag while you click.
2. Choose **Play From Here**.

The playtest starts at that spot. From 3D, you face the way the camera was
looking. From Plan, you face the way the map's player start faces.

In 3D, right-click a floor or a catwalk.

The spot must have room for the player. If it does not, the status line shows
one of these messages and the playtest does not start.

| Message | Meaning |
| --- | --- |
| Choose a walkable surface inside a sector. | The spot is not inside a sector. |
| This surface needs at least 48 units of headroom. | The ceiling is too low there. |
| The player start is too close to a blocking wall. Move it inward. | The spot is within 128 map units of a wall the player cannot pass. |
| A blocking floor sprite leaves too little room for the player. | A flat blocking sprite is just above the spot. |
| A blocking sprite overlaps the player start. | A blocking object stands on the spot. |

If you change the map while the menu is open, **Play From Here** is greyed
out. Right-click again.

## Play on a catwalk

A [catwalk](building-tools.md#catwalks) has a floor below it, so one spot in
Plan can mean two places.

- In Plan, with the floor list on **All floors**, right-click a spot under a
  catwalk. The menu shows two entries. **Play From Here** starts you on the
  floor. **Play on catwalk** starts you on top of the catwalk.
- In Plan, with a level chosen in the
  [floor list](building-tools.md#show-one-floor-at-a-time), **Play From Here**
  starts you on that level.
- In 3D, right-click the catwalk itself and choose **Play From Here**.

## Playtest during a live session

A playtest is local. The other participants keep editing, and they see
"Playtesting" beside your name. See [Live editing](live-editing.md).

## Map check

The map check looks through the whole map and lists what it finds.

1. Choose **Build → Check map**. The **Map check** window opens.
2. Read the list. The top line names the game the map is checked for and
   counts the findings.
3. Click the name of an object, such as "Sector 4" or "Sprite 12", to select
   it and bring it into view in Plan.
4. Fix the problem. The list updates when the map changes. You can also click
   **Refresh**.

When nothing is wrong, the window says "No issues found."

### Findings about the player start

| Finding | What to do |
| --- | --- |
| Place a Player Start inside a sector. | Use the **Player Start** tool. |
| A player needs 48 units of headroom and a start between the floor and ceiling. | Raise the ceiling, or move the start. |
| The player start is too close to a blocking wall. Move it inward. | Move the start at least 128 map units from solid walls. |
| A blocking floor sprite leaves too little room for the player. | Move the start, or the sprite. |
| A blocking sprite overlaps the player start. | Move the start, or the sprite. |

For maps that target Duke Nukem 3D 1.3D or Atomic / Plutonium, the same checks
run on every multiplayer start sprite. Such a finding begins with the name of
the start, for example "Co-op start #12 · Sector 3." A start sprite that lies
outside its sector is reported as "Move this spawn inside its assigned
sector."

### Findings about tiles

| Finding | What to do |
| --- | --- |
| Floor, Ceiling, Wall, Overlay or Sprite tile N is outside the target’s tile range. | Choose a tile number that the target game has. See [Map limits](editing-maps.md#map-limits). |
| Floor, Ceiling, Wall, Overlay or Sprite tile N has no loaded art. | The tile number is empty in the loaded game. Choose another tile, or add the art to your project. |
| Load the selected base game and its project art to check tile availability. | The map is for a different game than the one that is running. The editor cannot check which tiles have art. Start NNBuild with the target game to check. |

### Findings about switches

This check runs for maps that target Duke Nukem 3D 1.3D or Atomic /
Plutonium.

| Finding | What to do |
| --- | --- |
| Switch tag N has no matching activator, light, or other known receiver. | The switch does nothing. Give it the tag of the thing it should control, or create that thing. See [Linked mechanisms](mechanisms.md#see-what-is-linked). |

### Findings about the map format

| Finding | What to do |
| --- | --- |
| Sector flags exceed the classic v7 target format. | Clear the extra bits in the sector's **Flags**. |
| Wall flags exceed the classic v7 target format. | Clear the extra bits in the wall's **Flags**. |
| Sprite flags exceed the classic v7 target format. | Clear the extra bits in the sprite's **Flags**. |
| The two sides of this portal do not point to each other. | The portal is damaged. Repair it with the [classic Build tools](classic-build-tools.md), or delete and redraw the sector. |

### Findings about script components

| Finding | What to do |
| --- | --- |
| Enable modern features or remove the script attachment. | The map has script components but Modern Mode is off. Turn it on, or remove the components. |
| Script attachment requires NNBuild. Stock *game* plays the map without it. | A reminder. The script runs only in NNBuild. Other programs load the map without it. |
| An attachment lost its map object. Reattach *script*. | The object the script was attached to is gone. Attach the script to an object again, or detach it in **Map settings**. |
| Missing project script *script*. | The script file is not in the project. Add it, or detach it. |

See [Script components](projects.md#script-components).

### Findings that stop saving

The last finding in the list can be the reason the map cannot be saved or
played yet. It is one of these.

| Finding |
| --- |
| A classic map needs a sector and must fit the version 7 sector, wall and sprite limits. |
| Use Player Start to place Duke inside a sector. |
| A sector points to a missing wall. |
| A sector uses a tile outside this Duke3D edition's art range. |
| A sector uses flags from an extended map format. |
| Two sectors own the same wall. |
| A wall does not belong to a closed sector loop. |
| A portal points to a missing wall or sector. |
| A wall uses an out-of-range position or tile. |
| A masked or one-way wall needs an overlay tile. |
| A wall uses flags from an extended map format. |
| A sprite uses an invalid sector, angle, status or tile. |
| A sprite uses flags from an extended map format. |
