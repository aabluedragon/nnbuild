# Linked mechanisms

In Build-engine games, moving parts are made from numbers. A lift is a sector
with a special tag. A hidden marker sprite inside it listens for a signal. A
switch on a wall sends that signal because it carries the same tag number.
Setting this up by hand means typing matching numbers in several places.

A *preset* does it for you. You choose the sector, the walls for the
switches and a few settings. The editor then writes the tags and places the
sprites in one step.

If *sector*, *sprite* or *tag* are new words, see
[Words you will meet](overview.md#words-you-will-meet).

## Which maps can use presets

The presets create Duke Nukem 3D mechanisms. They work when the map's
[game target](projects.md#game-targets) is **Duke Nukem 3D 1.3D** or **Duke
Nukem 3D Atomic / Plutonium**. For other targets the menu is greyed out.

## The presets

Open them from **Build → Linked mechanisms**.

| Preset | What the player gets |
| --- | --- |
| **Lift** | A platform that travels between two floor heights, with call buttons |
| **Ceiling door** | A door that opens upward when a switch is used |
| **Crusher trap** | A floor that drops and a ceiling that closes down on it, set off by a trigger |
| **Light switch** | A switch that turns the lights of a room on and off |

## Steps for every preset

1. Select the sector that should move or change. This is the *target*.
2. Choose **Build → Linked mechanisms** and pick a preset. A window with the
   preset's name opens.
3. Check **Target**. It shows the selected sector. To change it, pick another
   sector from the list, or select one in the Scene and click **Use selected
   sector**.
4. Fill in the settings of the preset. They are described below.
5. Add a wall for each switch:
   1. Hold **Ctrl** and click a solid wall in Plan. You can also click the
      wall in 3D.
   2. Click **Add selected wall** in the preset window.
   3. Repeat for more switches.
6. Click **Create linked mechanism**.

The status line confirms the result and shows the tag number that was used,
for example "Create Lift · tag 100". The whole mechanism is one undo step.

Each chosen wall appears in the window. Click its name to bring it into view.
Click **Remove** to take it out of the list.

While the window is open, Plan draws a line from each chosen wall to the
target.

If a solid wall is selected when you open a preset, that wall is added as the
first switch, and its sector becomes the target.

## Rules for every preset

- The target sector must not have a tag yet. Its **LoTag** must be 0.
- The target must not already contain effector, activator or speed marker
  sprites.
- You need at least one switch wall.
- Each switch wall must be solid and at least 128 map units wide.
- The room in front of each switch wall needs 48 height units between floor
  and ceiling.
- For **Lift**, **Ceiling door** and **Crusher trap**, the floor and ceiling of
  the target must be level, not sloped.

If a rule is broken, the status line says what to change and nothing is
created.

## What every preset creates

- **A new tag number.** The editor picks the lowest number from 100 upward
  that is not used as a LoTag or HiTag anywhere in the map.
- **One switch sprite per wall.** It is placed in the middle of the wall,
  just in front of it, facing into the room. Its **LoTag** is the new tag
  number.
- **Marker sprites inside the target.** They are placed together at a free
  point in the sector. The player never sees them.

## Lift

A lift is a sector whose floor moves between two heights.

**Before you start**

- Build the lift platform as its own sector.
- It needs two neighbours with different floor heights. These are the two
  *stops*. The player must be able to walk through the portals to them.
- Set the platform's **Floor Z** to the same height as one of the stops.

**Settings**

| Setting | Meaning |
| --- | --- |
| **First stop**, **Second stop** | The two neighbouring sectors the lift serves. The editor suggests them. |
| **Speed (Build)** | Movement speed, from 1 to 32767. It starts at 256. |
| **Call buttons** | The walls that get a button. Add one at each stop. |

**Extra rules**

- The two stops must have different floor heights.
- The platform's floor must be level with one of them.
- There must be 48 units between the higher stop's floor and the platform's
  ceiling.
- Every neighbour of the platform must be level and at one of the two stop
  heights.

**What is created**

| Where | What |
| --- | --- |
| Target sector | **LoTag** set to 17 |
| Inside the target | An Activator sprite with the new tag as **LoTag** |
| Inside the target | A speed marker sprite (GPSPEED) with the speed as **LoTag** |
| Each chosen wall | A POWERSWITCH1 sprite, 40 units above the floor |

## Ceiling door

A ceiling door is a sector whose ceiling moves up to open and down to close.

**Before you start**

- Build the door as its own sector between two rooms.
- Either close it, by setting **Ceiling Z** equal to **Floor Z**, or leave it
  open with at least 48 units of height.
- At least one neighbouring room must have its ceiling 48 units or more above
  the door's floor.

**Settings**

| Setting | Meaning |
| --- | --- |
| **Speed (Build)** | Movement speed, from 1 to 32767. It starts at 256. |
| **Switches** | The walls that get a switch |

**What is created**

| Where | What |
| --- | --- |
| Target sector | **LoTag** set to 20 |
| Inside the target | An Activator sprite with the new tag as **LoTag** |
| Inside the target | A speed marker sprite (GPSPEED) with the speed as **LoTag** |
| Each chosen wall | A POWERSWITCH1 sprite, 40 units above the floor |

## Crusher trap

In the game, the room looks normal until the trigger is used. Then the floor
drops and the ceiling comes down after it, stopping 8 units above the dropped
floor. Using the trigger again resets the trap. The player can use the trigger
or shoot it.

**Before you start**

- The trap sector needs 48 units between floor and ceiling.
- Put the trigger in a different sector, for example a neighbouring room.

**Settings**

| Setting | Meaning |
| --- | --- |
| **Floor drop** | How far the floor falls, from 16 to 4096 height units. It starts at 384. |
| **Speed (Build)** | Movement speed, from 1 to 32767. It starts at 24576. The editor may lower it a little so the drop divides evenly. |
| **Switches** | The walls that get a trigger |

**What is created**

| Where | What |
| --- | --- |
| Target sector | **Floor Z** lowered by the floor drop. The editor shows the floor in its dropped position. |
| Inside the target | An Activator sprite with the new tag as **LoTag** |
| Inside the target | A speed marker sprite (GPSPEED) with the speed as **LoTag** |
| Inside the target | A Sector Effector with **LoTag** 31 and angle 512, at the old floor height |
| Inside the target | A Sector Effector with **LoTag** 32 and angle 1536, 8 units above the dropped floor |
| Each chosen wall | An enlarged DIPSWITCH2 sprite, 32 units above the floor |

## Light switch

Using the switch brightens the room to the shade you choose. Using it again
returns the room to its own shade.

**Settings**

| Setting | Meaning |
| --- | --- |
| **Light shade** | The shade of the room when the light is on. It must be lower, which means brighter, than the room's current floor shade. It starts at −12. |
| **Light palette** | The palette of the room when the light is on, from 0 to 255. It starts at 0. |
| **Switches** | The walls that get a switch |

For a clear effect, make the room dark first by raising the **Shade** of its
floor and ceiling. Then run the preset.

A sloped sector may be used for this preset.

**What is created**

| Where | What |
| --- | --- |
| Inside the target | A Sector Effector with **LoTag** 12. Its **HiTag** is the new tag. Its **Shade** and **Palette** are the light values. |
| Each chosen wall | A LIGHTSWITCH sprite, 40 units above the floor |

## See what is linked

Choose **Build → Show links** to draw the links of the whole map. Opening a
preset turns this on for you.

- In Plan, a line runs from each switch to the sector that reacts to it.
- Select a switch, a marker or a linked sector. Its lines turn orange, and the
  tag number appears on each line.
- In 3D, a small floor plan in the top right corner shows the same lines.

Choose **Build → Show links** again to hide them.

A link is drawn when both ends exist:

| End | What counts |
| --- | --- |
| Switch | A wall or sprite that shows one of the game's switch tiles and has a **LoTag** above 0 |
| Receiver | An Activator, locked Activator, Master Switch or Respawn sprite with the same **LoTag** |
| Receiver | A Sector Effector of type 12, 21, 24, 25 or 34 whose **HiTag** is that number |
| Receiver | A wall with a force field overlay and the same **LoTag** |

Links are shown for the same two game targets as the presets.

## Change a mechanism

A preset writes ordinary map data. Nothing is locked. To change a mechanism,
edit the objects it created.

| To change | Edit |
| --- | --- |
| Which switch works the mechanism | The **LoTag** of the switch sprite |
| The speed | The **LoTag** of the speed marker sprite in the target |
| The lit shade or palette of a light | The **Shade** and **Palette** of the Sector Effector in the target |
| The height of a switch on the wall | **Above floor** of the switch sprite |
| The look of a switch | **Tile** of the switch sprite |

To find the marker sprites, select the target sector, turn on **Show links**
and look for the sprites inside it in Plan. You can also search the
**Hierarchy**.

To remove a mechanism straight after creating it, press **Ctrl+Z**. Later, do
it by hand: delete the marker sprites and the switch sprites, and set the
sector's **LoTag** back to 0. For a crusher trap, also raise **Floor Z** back
to its old value.

A preset refuses a target that already holds marker sprites. Change that
mechanism by hand instead.

## Check for broken links

**Build → Check map** lists every switch whose tag has no receiver. See
[Map check](playtesting.md#map-check).
