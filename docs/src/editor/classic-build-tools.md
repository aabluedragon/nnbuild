# Classic Build tools

NNBuild also contains the original-style Build editor, with the Mapster32
tools for Duke Nukem 3D maps. The program calls it the *native Build tools*.
It has its own 2D and 3D modes and is driven mostly from the keyboard.

The [modern editor](overview.md) is meant for everyday work. The classic
tools are there for the jobs it does not cover. A map can move from the
modern editor to the classic tools and back.

## When to use the classic tools

- For operations the modern editor does not have, such as splitting and
  joining sectors, extra wall loops, detailed wall alignment, sprite
  ornamenting, and Mapster32 scripts.
- To repair a map that the [map check](playtesting.md#map-check) reports as
  damaged.
- When you already know Build or Mapster32 and prefer its keys.

The classic tools are part of builds that use SDL2 on Windows, Linux, macOS
and BSD. In other builds the menu entry is missing.

## Open the classic tools from the modern editor

1. Choose **File → Open Build tools**.
2. The classic editor opens in its own window. It holds a copy of your map as
   it is now, including unsaved changes.
3. Do your work there.
4. Save the map in the classic editor.
5. Close the classic editor.

Back in the modern editor, the status line says "Applied edits from the
native Build tools". The whole visit is one undo step.

Good to know:

- While the classic tools are open, the status line of the modern editor
  reminds you: "Save the map and exit the native Build tools to apply your
  edits."
- **File → Open Build tools** is greyed out until you close the classic
  editor.
- If you close the classic editor without saving, nothing changes.
- Be in the modern editor, not in a playtest, at the moment you close the
  classic editor. Otherwise the result is not applied. See
  [When the result is not applied](#when-the-result-is-not-applied).

## How the map moves between the two

The modern editor writes your map to a temporary file and starts the classic
editor on that file. When the classic editor closes, the modern editor reads
the file again.

The modern editor reads classic version 7 maps only. Keep the map within the
[map limits](editing-maps.md#map-limits) while you work in the classic tools,
and do not use features that need a newer map format.

What happens next depends on whether the modern map changed while the classic
tools were open.

| Modern map since you opened the classic tools | Result |
| --- | --- |
| Not changed | The classic result replaces the map. |
| Changed, and both sides only changed properties or positions of existing objects | The two sets of changes are merged, object by object. |
| Changed, and either side added or removed sectors, walls or sprites | Not applied |
| Changed, and both sides changed the same object in different ways | Not applied |

In practice: if you plan to add, join or delete anything in the classic
tools, leave the modern editor alone until you are back.

### When the result is not applied

Your classic work is not lost. The status line shows where the file is:

```text
The native draft is available at /tmp/.nnbmap-AbC123/draft.map
```

The path on your machine will differ. You can open that file with **File →
Open map...** and save it under a name of your own.

## In a live session

During a [live session](live-editing.md), the classic tools work on your own
copy of the shared map. The other participants keep editing.

- When you come back, your result is sent to the session as one edit.
- Edits that the others made in the meantime have changed the modern map.
  The table above applies. Changes to different objects merge. If something
  conflicts, the status line gives the path of your saved classic draft.
- For work that adds, joins or deletes things, agree with the others that
  they wait until you are back.

## Open the classic tools from the command line

Use `--build-editor`, with a map file if you like. This opens the classic
tools directly, without the modern editor.

```sh
./nnbuild --build-editor "My Map.map"
./nnbuild -gamegrp /path/to/DUKE3D.GRP --build-editor "My Map.map"
```

To continue in the modern editor later, save the map as a classic version 7
map and open it there:

```sh
./nnbuild -gamegrp /path/to/DUKE3D.GRP --editor "My Map.map"
```

A build without the classic tools answers `--build-editor` with "This build
has no native Build tools."

## Control by an AI assistant

The classic tools can be driven through MCP. See [AI and MCP](ai-and-mcp.md).

- Start them with `--build-editor` and `--mcp-http PORT` or `--mcp-stdio`.
  The tools `game_status`, `console_execute`, `console_output` and `ui_input`
  are available. `game_status` reports the mode `build_tools`.
- From the modern editor, the tool `editor_control` with the action
  `build_tools` opens the classic tools. Give it a `port` to make the classic
  editor offer its own MCP address on that TCP port. Choose a port that the
  game itself does not use.
- The console command `saveboard` saves the map in the classic editor. An
  assistant sends it with `console_execute` before closing the classic
  editor.
