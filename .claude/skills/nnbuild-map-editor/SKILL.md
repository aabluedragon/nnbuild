---
name: nnbuild-map-editor
description: Build, inspect, revise, playtest, or export classic Duke3D maps and NNBuild projects. Use for the editor's Plan and 3D tools, modern script components and prefabs, and live editing as an MCP participant.
---

# NNBuild Map Editor

Read [Map Editor](../../../source/nnbuild/MAP_EDITOR.md) for launch commands,
GUI controls, and live session setup. Inspect the existing draft before placing
anything. Preserve intentional layout, effects, and texture choices.

## Choose an MCP connection

- Use `nnbuild --editor --mcp-stdio` with the current game data options
  to control the local editor, including offline drafts. Use
  `--mcp-http PORT` to control a game instance over loopback HTTP.
- Call `editor_control` to open or save the draft, undo or redo a change,
  change views, start a playtest, or return to editing. Use `project_status`
  to read game settings and asset icons. With `project_control`, choose
  project settings or an individual map override and copy or attach assets.
- Use `console_execute` for console commands and `console_output` to read
  responses. Use `server_status` for the map and connected players.
- Use `editor_control` with `action: "build_tools"` to work in the native
  editor. Supply a distinct `port` argument to connect to its console and
  send `ui_input` events.

## Join a live session as an independent participant

- Join a host using `nnbuild --editor-mcp address:port --name "Assistant"`.
  When the adapter is already configured, use `editor_status` to verify its
  connection or `editor_connect` to join. Keep a single adapter open so the
  host sees the same participant and cursor color throughout the task. Read
  collaborators' `activity`, `selection`, and `cursor` in the status roster
  to coordinate edits in the same part of the map.
- Read `editor_inspect` with `kind: "map"` for counts and the player start.
  Inspect `sectors`, `walls`, and `sprites` in ranges of up to 80 records.
  Indexes start at zero. A sector identifies its wall range. Each wall's
  `point2` identifies its endpoint and paired portal walls name their target
  sectors.
- Use `editor_presence` to expose your cursor with `x`, `y`, and an optional
  `kind` and `index`. Point to the area you are about to work on so a human can
  follow. Each successful edit also updates your selection and activity. Use
  `{"clear": true}` to clear your cursor and selection when done.
- Pass the latest `revision` to `editor_edit`. Reinspect before retrying a
  conflict, especially after any change that can renumber walls or sprites.
  Keep edits consistent with the active Duke3D edition and avoid assigning
  an occupied location or reversing a wall loop.
- Use `operation: "batch"` with a short `label` and up to 64 `edits` for a
  coordinated change. List normal edit operations in execution order and use
  the indexes that exist after each previous operation. The host applies
  one revision and one shared undo step on success. Read `editor_history`
  to verify the author and touched objects.

## Edit operations

| Operation | Arguments in addition to `revision` and `operation` |
| --- | --- |
| `room` | `points` as at least three `[x, y]` pairs |
| `extend` | Solid wall `index`, optional `depth` |
| `split` | Wall `index` |
| `move_vertex` | Wall vertex `index`, `x`, `y` |
| `sprite` | `tile`, `x`, `y`, optional preferred `sector` |
| `move_sprite` | Sprite `index`, `x`, `y` |
| `start` | `x`, `y`, optional `sector` and `angle` |
| `set` | `kind`, `index` for records, and numeric `properties` |
| `delete` | `kind` of `sector` or `sprite` and its `index` |

To apply `set`, use the editable field names from the corresponding
`editor_inspect` record. Geometry references and record indexes come from
the editor. Use movement and room operations to change them. Z increases
downward. Angles use 0 through 2047 Build units. A room shares a portal with
an adjoining room when their opposite wall endpoints match exactly.

## Author a playable draft

1. Inspect the map summary, then read the sectors and walls where you plan to
   work. Use `room` for a closed outline or `extend` for a portal off a solid
   wall. Use `split` to add a vertex to an existing edge and `move_vertex` to
   adjust its position. Reinspect indexes after a structural edit.
2. Set a player's spawn inside a sector with `start`. Use `sprite` to place
   objects such as a shotgun (`tile: 28`) or a trooper (`tile: 1680`). In the
   human editor, use the Assets panel to browse the active edition's tiles.
3. Change appearance and behavior with `set`. For example, use `kind: "sector"`
   with `properties: {"floor_shade": 16}` to darken that floor. A `kind` of
   `"start"` supports `angle` and `z`. Inspect any existing tags and flags
   before editing a map's effects.

Verify the result by inspecting affected sectors and sprites, including the
player start and relevant tags. Use `editor_control` with `playtest` and
`stop_playtest`, or press **F5** in the editor to test the draft. For a
temporary test location, use **Play From Here** in the GUI or pass the
`spawn` sprite index or `x`, `y`, and `sector` to `playtest`.
Use **Build → Check map** to focus potential issues or call `editor_check`
in the local workspace. Read the Map Editor
guide for linked mechanisms, pillars, stairs, catwalks, floor selection,
and batch Inspector edits.

Use `editor_export` with an authorized local `.map` path to save the current
draft. Export runs on the adapter's machine. Saving on the host is a separate
action in the host editor.

## Work with modern project components

- Check `project_status` for the effective Modern Mode setting and the
  NNBuild compatibility warning. Use `project_control` to attach a
  project-relative JS or TS asset to a sprite or create and attach a
  template with `create_component`.
- Use `save_prefab` with a `path` such as `Prefabs/Assembly.nnbprefab` and a
  `sprites` array. Use `place_prefab` with the asset path and `x` and `y`.
  Place scripted prefabs from the host's project workspace in live sessions.
- Use the attachment context's `state` object for instance data.
  `onUpdate` receives frame time and `onFixedUpdate` receives simulation
  time, both in seconds. Save the map before exporting its project ZIP.
