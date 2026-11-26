# Debugging with VS Code

Script mods can be debugged with real breakpoints. The mod generator writes everything VS Code needs into the project's `.vscode` folder.

This page covers the VS Code setup, the game's debug options, the in-game DevTools, and how script errors are reported.

## What you need

- A project made with `--gen-ts` or `--gen-js`. See [Quick start](quick-start.md).
- Visual Studio Code.
- Node.js on your `PATH`. The debug adapter is a small Node.js program. The game itself does not need Node.js.
- A normal desktop build of the game (SDL2 with OpenGL). Other builds have no debug connection.

Open the **project folder** in VS Code, for example `autojs/my-mod`. Do not open the folder above it.

### Platforms

- **Linux**: the complete flow on this page is verified: generate a project, launch through the adapter, stop at a breakpoint in `index.ts`, read the stack and the variables, evaluate, continue, disconnect.
- **macOS**: the debugger was first written for macOS.
- **Windows**: the debug connection is built into the Windows version too, but debugging is not yet tested on Windows.

## The files in `.vscode`

| File | Purpose |
| --- | --- |
| `launch.json` | Two debug configurations: launch the game, or attach to a running game. |
| `tasks.json` | One task that starts the debug adapter before a debug session. |
| `vibebuild32-debug-adapter.js` | The debug adapter. It translates between VS Code and the game. |
| `vibebuild32-start-debug-adapter.js` | Starts the adapter in the background and waits until it is ready. |
| `README.md` | A short guide. |

### `launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Launch VibeBuild32",
      "type": "node",
      "request": "launch",
      "debugServer": 4714,
      "preLaunchTask": "VibeBuild32: Start VS Code debug adapter",
      "gameExecutable": "${workspaceFolder}/../../vibebuild32",
      "cwd": "${workspaceFolder}/../..",
      "debugPort": 8501
    },
    {
      "name": "Attach to running VibeBuild32",
      "type": "node",
      "request": "attach",
      "debugServer": 4714,
      "preLaunchTask": "VibeBuild32: Start VS Code debug adapter",
      "debugPort": 8501
    }
  ]
}
```

- `debugServer: 4714` tells VS Code to talk to a debug adapter that is already listening on port 4714.
- `preLaunchTask` starts that adapter first.
- `gameExecutable` is the program to start. The default points two folders up from the project, because a generated project lives in `<folder>/autojs/<name>/` and the game is expected in `<folder>`. Change it if your executable is somewhere else.
- `cwd` is the folder the game is started in. It must be the folder that contains `autojs/`, or the game will not find your mod.
- `debugPort: 8501` is the port of the game's debug connection.

### `tasks.json`

```json
{
  "version": "2.0.0",
  "tasks": [{
    "label": "VibeBuild32: Start VS Code debug adapter",
    "type": "process",
    "command": "node",
    "args": ["${workspaceFolder}/.vscode/vibebuild32-start-debug-adapter.js", "--port", "4714"],
    "options": { "env": { "NODE_OPTIONS": "" } }
  }]
}
```

The task runs `node`. If Node.js is not installed, the task fails and the debug session does not start.

### The debug adapter

- The start script checks whether an adapter already answers on port 4714. If not, it starts `vibebuild32-debug-adapter.js` as a background process and waits until it accepts connections. Then the task ends and VS Code continues.
- The adapter has no dependencies. There is nothing to `npm install`.
- The adapter serves one debug session and then exits. If no session starts within 60 seconds, it exits too.
- The adapter listens only on the local computer.

## Ports

| Port | Used for | Where it is set |
| --- | --- | --- |
| 4714 | VS Code ↔ debug adapter | `--port` in `tasks.json` and `debugServer` in `launch.json`. Both must be the same number. |
| 8501 | Debug adapter ↔ game | `debugPort` in `launch.json`, and the port of the game's `--debug` option. |

Both connections are local (`127.0.0.1`). If another program uses port 4714, the task fails with `VibeBuild32 DAP adapter did not start on port 4714`. Choose another port and change it in both files.

## The two debug configurations

### Launch VibeBuild32

1. Set breakpoints.
2. Choose **Launch VibeBuild32** in **Run and Debug** and press **F5**.

The adapter starts the game with the single option `-dbg` and waits for its debug connection. VS Code then sends your breakpoints. Only after that does the game load your scripts. Breakpoints in top-level code and in `start` handlers therefore work.

Pressing **Stop** ends the game.

The game is started with `-dbg` and nothing else. To choose a game, data files or a map, use the [`vbd.params` directive](#launch-arguments-the-vbdparams-directive).

### Attach to running VibeBuild32

1. Start the game yourself with `--debug` (or `--devtools`).
2. Choose **Attach to running VibeBuild32** and press **F5**.

Attach within the first few seconds after the game starts if you want breakpoints in top-level code or in `start` handlers. The game holds your scripts back for about five seconds to give you that chance. See [When scripts start in debug mode](#when-scripts-start-in-debug-mode). If you attach later, debugging works, but that code has already run.

**Stop** or **Disconnect** ends the debug session and leaves the game running. Continue the game first if it is paused.

## Game options for debugging

| Option | Effect |
| --- | --- |
| `--debug`, `-dbg` | Opens the debug connection on port 8501. Skips the game picker and the startup window. Scripts start later than usual, see below. |
| `--debug <port>`, `-dbg <port>`, `-dbg<port>` | The same on another port. Change `debugPort` in `launch.json` to match. A custom port is read by the Duke runtime games. |
| `--devtools`, `-dt` | Everything `--debug` does, plus the [in-game DevTools](#in-game-devtools). Forces windowed mode. |

### When scripts start in debug mode

Without `--debug`, the game loads scripts 30 frames after it starts.

With `--debug` or `--devtools`, the game gives a debugger time to connect:

- If the VS Code debug adapter contacts the game, the game waits until VS Code has sent your breakpoints and finished its setup. Then it loads the scripts. There is no time limit for this step.
- If no adapter contacts the game, the scripts are loaded after 300 frames. That is about five seconds at 60 frames per second.
- If VS Code attaches after the scripts were loaded, you can still debug. Top-level code and `start` handlers have run by then.

There is no reason to add `--debug` to a normal launch. It delays your scripts and opens a local debug port.

## Launch arguments: the `vbd.params` directive

The **Launch VibeBuild32** configuration cannot pass game arguments. Put them into your entry file instead, as a special comment near the top:

```ts
/// vbd.params="--game duke --grp ~/games/duke3d/duke3d.grp --skip-cinema"
import { on, log } from 'vb';
```

When the game is started in debug mode, it reads this line and adds the arguments to its own command line.

Rules:

- The line starts with three slashes: `/// vbd.params=…`.
- It must be in the comment block at the top of the file. Only `//` comment lines and empty lines may come before it. It must be within the first 32 lines.
- The value is split into arguments at spaces. The quotes around the whole value are optional. Use inner quotes for an argument that contains spaces:

  ```ts
  /// vbd.params="--game sw --grp '/home/me/Shadow Warrior/SW.GRP'"
  ```

- A backslash keeps the next character as it is.
- An argument that starts with `~/` gets your home folder.
- The arguments are not run through a shell. Variables and wildcards are not expanded.
- The directive is only read when the game starts with `--debug`, `-dbg`, `--devtools` or `-dt`. In a normal launch it is an ordinary comment.
- The game reads it from `index.ts` (or `index.js`) of every project in the `autojs` folder of the folder it was started in. It is not read from files given with `--js`.

The generated starter contains the directive as an inactive example:

```ts
// /// vbd.params="-gm duke -g ~/games/duke3da/duke3d.grp -sc"
```

Remove the leading `// ` and adjust the paths to activate it.

## Setting breakpoints

Click in the gutter left of a line, as in any VS Code project.

Breakpoints work in entry files: `index.ts`, `index.js`, and files loaded with `--js`. They currently do not stop in:

- files loaded with a relative `import` or `require`, such as `import './util.ts'`. The game keeps the `./` in the path it records for the file, so the path is not the one VS Code sends. You can still step into such a file from a breakpoint in the entry file.
- [map script components](map-script-components.md).

Further rules:

- A breakpoint stops the game when the line is about to run. While it is stopped, the whole game is frozen.
- A breakpoint on a line that has no code of its own stops on the line directly below it.
- Only plain line breakpoints exist. A condition, a hit count or a log message is ignored: the breakpoint always stops.
- VS Code shows every breakpoint as confirmed, even on a line that never runs.
- The file path must be exactly the path the game loaded. The game resolves links to the real location. If you open the project through a symbolic link, breakpoints may not stop.
- Limits: breakpoints in up to 64 files, up to 128 per file.
- In TypeScript files, line numbers after a multi-line `interface` or `type` declaration are shifted. See [Line numbers](project-layout.md#line-numbers).

## Stepping and inspecting

While the game is stopped at a breakpoint:

| Action | Supported |
| --- | --- |
| Continue, Step Over, Step Into, Step Out | Yes |
| Call stack with function name, file and line | Yes |
| Variables in three scopes: **Local**, **Closure**, **Global** | Yes, as text |
| Hover over a variable name | Yes |
| Watch expressions and the Debug Console input | Limited, see below |
| Pause a running game | No |
| Stop on exceptions | No |
| Changing a variable's value | No |

How variables are shown:

- Each value is shown as text. Objects and arrays appear in JSON form and cannot be expanded.
- Functions and variables whose value is `undefined` are not listed.
- **Closure** lists the outer variables that the current function uses, such as top-level `let` and `const` of your module.

How expressions are evaluated:

- A single name, such as `frames`, is looked up in the Local and Closure variables of the selected stack frame. This is what hovering uses.
- Any other expression is evaluated as global code. It cannot see local variables or the top-level variables of your module. `vb.player()` works. `frames + 1` does not.

Other things to know:

- `log()` output does not appear in VS Code. Read the game log, `vibebuild32.log`.
- When the game exits, the debug session ends.

## In-game DevTools

Start the game with `--devtools` (or `-dt`) to open a DevTools view next to the game.

`--devtools` includes `--debug`. You can use DevTools without VS Code: the scripts then start about five seconds after the game. You can also attach VS Code at the same time. See [When scripts start in debug mode](#when-scripts-start-in-debug-mode).

What the view is depends on the platform:

| Platform | DevTools view |
| --- | --- |
| Linux, Windows | A second window, *VibeBuild32 JavaScript DevTools*. |
| macOS | A page in a Google Chrome app window. Google Chrome must be installed. |
| Builds without OpenGL | A read-only console panel drawn over the game. |

### The DevTools window (Linux and Windows)

- A status line: **Running**, or **PAUSED at …** with a **Resume** button.
- **Console** tab: the 12 most recent log lines, and an **Evaluate** field with a **Run** button.
- **Scope** tab: short hints.
- Closing the window hides it for the rest of the session.

The **Evaluate** field runs an expression as global code and shows the result as JSON text. For example `vb.player()` or `globalThis.myValue`. It cannot see the top-level variables of your modules. Store a value on `globalThis` if you want to inspect it.

An expression that throws an error is treated like any other script error: the game shows the error and closes. Type carefully.

### The DevTools page (macOS)

- A list of the loaded entry scripts. Click one to see its source with line numbers.
- Click the circle left of a line to set or clear a breakpoint.
- **Continue**, **Step over**, **Step into**, **Step out**.
- The paused location, the call stack and the variables of the selected frame.
- The most recent log lines.
- A field to evaluate an expression.

Press **Command+Option+I** in the game window to bring the page to the front.

### The `breakpoint()` function

`breakpoint()` is a function of the `vb` module. It pauses your scripts from code:

```ts
import { on, breakpoint, log } from 'vb';

on('levelChanged', () => {
  log('pausing scripts');
  breakpoint();
});
```

- It only works when the game was started with `--devtools`. Otherwise it does nothing.
- It does not freeze the game, and it does not stop in the middle of a function. The current function runs to its end.
- After that, no events, timers, `fs` callbacks, promise continuations or component updates are delivered. Network results still arrive: socket listeners keep running.
- The DevTools window shows **PAUSED at vb.breakpoint()**. Click **Resume** to continue.

The **Resume** button is part of the DevTools window on Linux and Windows. The DevTools page on macOS has no such button. On macOS use breakpoints in the source instead.

## How errors are reported

A script error that nothing catches is fatal. This applies to:

- a syntax error, or an error at the top level of a file while it is loaded,
- an error thrown in an event handler, a timer, a socket listener, an `fs` callback, a chat command handler or a component method,
- a rejected promise that has no handler. See [Promises and errors](modules.md#promises-and-errors).

When that happens, the game:

1. Writes the message and the stack trace to the game log, prefixed with `[js]`.
2. Copies the full text to the clipboard.
3. Shows a dialog titled *VibeBuild32 JavaScript Error* with the message and the stack trace.
4. Exits after you close the dialog.

The message names the place, for example `failed to load /…/index.ts`, `uncaught exception in frame handler #1 (disabled)` or `unhandled promise rejection`. The stack trace lists file names and line numbers.

Notes:

- Despite the word `(disabled)` in the message for event handlers, the game does not switch the handler off and carry on. It always exits. Use `try`/`catch` where an error is acceptable.
- A script file that cannot be read is only logged (`could not read …`). The game continues without it.
- For TypeScript files, a syntax error is reported by the JavaScript engine after the types have been removed. If the message makes no sense, the cause is often a construct the converter does not handle. See [TypeScript support and its limits](project-layout.md#typescript-support-and-its-limits).
- On a server, a script error stops the server. See [Errors stop the server](server-scripts.md#errors-stop-the-server).
