# Building tools

These tools build common shapes in a few steps and help you work with heights
and with many objects at once. You find most of them in the **Build** menu of
the toolbar.

Heights on this page are in *height units*. One height unit is 256 in the
Inspector's **Z** fields. See
[Words you will meet](overview.md#words-you-will-meet).

## Pillars

A pillar is a solid block standing inside a room. In the map it is a closed
ring of solid walls inside a sector.

1. Select the sector that will hold the pillar. **Shift**-click inside it in
   Plan. Selecting any object inside the sector also works.
2. Choose **Build → Draw pillar**. The toolbar shows "Draw pillar".
3. Click each corner of the pillar inside the sector.
4. Press **Enter**, or click the first corner again, to close the outline.

The editor returns to the Select tool and the status line says "Pillar added".

Rules:

- The whole outline must be inside the sector.
- It must not touch or cross the sector's walls.
- No sprite, and not the player start, may be inside it.

If a rule is broken, the status line shows:

```text
Draw the pillar entirely inside an open area of the sector, clear of sprites and the start.
```

The pillar's walls take their tile, shade and palette from the first wall of
the sector. Select a pillar wall afterwards to change them.

**Draw pillar** is greyed out when nothing in a sector is selected.

## Stairs

The stairs tool adds a row of new sectors. Each one is a step higher, or
lower, than the one before.

1. Choose **Build → Stairs...**. The **Build stairs** window opens.
2. In Plan, hold **Ctrl** and click the solid wall where the stairs begin.
3. Set the three numbers.
4. Click **Create stairs**.

| Field | Meaning | Allowed | Starts at |
| --- | --- | --- | --- |
| **Steps** | How many steps to build | 2 to 32 | 4 |
| **Tread depth** | How deep each step is, in map units | 128 to 4096 | 384 |
| **Rise per step** | Height of each step. Positive goes up, negative goes down. | −16 to 16, not 0 | 8 |

The steps lead away from the wall you selected. Each step keeps the ceiling
height of the room it starts from.

Rules:

- The wall must be solid.
- The floor and ceiling of the starting room must be level, not sloped.
- Every step needs at least 48 units between its floor and the ceiling.
- The stairs must not overlap or cross existing rooms.

| Message | What to do |
| --- | --- |
| Select a solid wall at the foot of the stairs. | Pick a wall that has no room behind it. |
| Use 2–32 steps, at least 128 units of tread, and at most 16 units of rise per step. | Bring the numbers into range. |
| Use a level floor and ceiling at the start of the stairs. | Remove the slope from the starting room. |
| The stair flight needs more headroom. Raise the parent ceiling or reduce the rise. | Raise the ceiling of the starting room, or use fewer or lower steps. |
| The stair flight would overlap existing rooms. | Start from another wall, or shorten the flight. |
| The stair flight reaches the edge of the map or the classic map limits. | Shorten the flight. |

The whole staircase is one undo step.

## Catwalks

A catwalk is a platform that the player can walk on and also walk under. It is
made from one flat sprite that shows a tile.

1. Choose **Build → Catwalk...**. The **Build catwalk** window opens.
2. Set the numbers.
3. Click **Place in Plan**. The toolbar shows "Place catwalk".
4. Click the centre point of the catwalk in Plan.

| Field | Meaning | Starts at |
| --- | --- | --- |
| **Tile** | The picture on the platform | 342 |
| **Length** | Size along the platform, in map units. At least 128. | 1024 |
| **Width** | Size across the platform, in map units. At least 128. | 512 |
| **Height above floor** | Height of the walking surface, in height units. At least 48. | 48 |
| **Angle (0–2047)** | Direction of the platform | 0 |

At angle 0 the length runs left to right in Plan. At 512 it runs top to
bottom.

Rules:

- The tile must have art.
- The whole catwalk must be inside one sector.
- It needs 48 units of clearance below it and 48 units above it.
- It must not cross a wall or a pillar.
- The tile must be able to cover the size as one sprite. If it cannot, use a
  larger tile or a smaller catwalk.

In Plan a catwalk is drawn as a blue rectangle. Select it like any sprite. To
change its height later, edit **Above floor** in the Inspector.

To put objects on a catwalk, choose its level in the
[floor list](#show-one-floor-at-a-time) and then place sprites as usual.

## The Section view

Section is a side view. It cuts through the map along a straight line and
shows the floors and ceilings that the line crosses. Use it to check heights
and clearances.

1. Select an object where you want to look.
2. Click **Section** above the Scene. The cut runs through the selection.

To move the cut:

- Click **Along X** or **Along Y** to turn the cut by a quarter turn. The
  button shows the current direction.
- Click **Pick cut**, then click a spot in Plan.
- Click **-** or **+** to shift the cut by one snap step.
- Select another object and press **F**.

What the view shows:

| Drawing | Meaning |
| --- | --- |
| A box labelled "Sector N … clearance" | A sector, from its floor to its ceiling, with the space between them |
| A blue line labelled "Catwalk … above · … below" | A catwalk, with the space above it and below it |
| An orange line with two dots | The travel of a lift, between the floor heights of its neighbours |

Move the pointer over a sector or a catwalk to read its exact floor, ceiling
and clearance at the top of the view. The line at the bottom shows where the
cut is.

Click a sector or a catwalk in Section to select it. This also sets the floor
list in Plan to that level.

If the cut does not cross any part of the map, the view says "Move the cut
through the map."

## Show one floor at a time

Maps often have rooms at several heights, and catwalks above floors. The floor
list hides everything that is not on the level you are working on.

The list is above the Scene in Plan, to the right of **Frame**. It shows
**All floors** at first.

1. Open the list.
2. Choose a level. There is one entry for each floor height and each catwalk
   height in the map.

While a level is chosen:

- Plan shows only the rooms, walls and sprites on that level.
- Clicking selects only things on that level.
- A sprite you place over a catwalk on that level lands on the catwalk.
- **Play From Here** starts you on that level.

Choose **All floors** to see everything again. Opening another map also
returns the list to **All floors**.

## Batch selection

Batch selection lets you pick several walls and sprites, and change them
together.

1. Click **Batch Select** in the toolbar. The button lights up.
2. Click a sprite to add it to the group. Click it again to remove it.
3. Hold **Ctrl** and click a wall to add or remove it.

The top of the Inspector shows how many objects are in the group. Open
**Selected objects** to see the list. Click an entry to bring that object into
view. Click **Clear** to empty the group.

Good to know:

- The group holds walls and sprites only.
- While **Batch Select** is on, you cannot drag objects in Plan.
- Turning **Batch Select** off empties the group.
- Adding or deleting sectors, walls or sprites also empties the group.

## Batch edits

The batch tools appear at the top of the Inspector while **Batch Select** is
on and the group is not empty. They are in two sections, **Move** and
**Appearance**. Each button makes one undo step.

### Move

| Field | Meaning |
| --- | --- |
| **Offset X**, **Offset Y** | How far to move, in map units |
| **Rise (sprites)** | How far to raise the sprites, in height units. Negative lowers them. |

Click **Move selection**.

- Walls move sideways only. Corners shared with neighbouring rooms move with
  them.
- Sprites keep their height above the floor, plus the rise you entered.
- A sprite must stay inside the map and between its floor and ceiling.

### Appearance

- **Shade offset** and **Adjust shade**: the number is added to the shade of
  every wall and sprite in the group. A negative number makes them brighter.
- **Align wall textures**: see [Wall alignment](#wall-alignment).
- **Height above floor** and **Set sprite height**: see
  [Sprite heights](#sprite-heights).

### Tile

Choose a tile in the **Tiles** tab of Assets and click **Apply tile**. Every
wall and sprite in the group gets that tile.

## Wall alignment

Use this when a picture should run smoothly across several walls.

1. Turn on **Batch Select**.
2. **Ctrl**-click each wall of the run.
3. Click **Align wall textures** in the Inspector.

The first wall of the run stays as it is. Each following wall continues the
picture where the previous wall ends.

Rules:

- Select at least two walls.
- All of them must be solid walls, not portals.
- All of them must show the same tile.
- They must form one unbroken run in one sector, each wall starting where the
  previous one ends.

| Message | Meaning |
| --- | --- |
| Select two or more connected walls with the same tile. | Fewer than two walls are in the group. |
| Use solid, unflipped walls with the same available tile. | A wall is a portal, shows a different tile, or has a flip or overlay flag set. |
| Select a continuous run of walls in one sector. | The walls do not join end to end. |

## Sprite heights

A new sprite stands on the floor. Switches, lights and decorations usually
belong higher up.

**One sprite**

1. Select the sprite.
2. Type a number in **Above floor** in the Inspector and press **Enter**.

The height is measured from the floor. If the sprite stands on a catwalk, it
is measured from the catwalk.

**Several sprites**

1. Turn on **Batch Select** and click the sprites.
2. Type a number in **Height above floor**. It starts at 40, which is the
   player's eye height.
3. Click **Set sprite height**.

Each sprite is set to that height above its own floor. The editor refuses the
change if a sprite would end up above its ceiling.
