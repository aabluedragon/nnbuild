# NNBuild debugging in VS Code

Open this project folder in VS Code. In **Run and Debug**, select either:

- **Launch NNBuild** — starts the executable with `-dbg`, then attaches.
- **Attach to running NNBuild** — attaches to a game already started with `-dbg` / `--debug` (or `-dt` / `--devtools`).

`-sm` / `--skip-menu` bypasses the game picker and native startup window. It is
implied by debugger and DevTools launches.
Use `-m` / `--menu` to force the native startup window instead; its **Show
startup menu by default** checkbox persists the normal-launch preference.
Use `-sc` / `--skip-cinema` to suppress each game's startup cinematics and
logo animations.
Use `-clr` / `--color` with `brown`, `gray`, `purple`, `red`, `yellow`,
`olive`, `green`, `blue`, or `#rrggbb` (for example, `-clr '#6a2ca0'`) to set
the local player's closest supported palette color in every game.
Use `-wnd` / `--window` to force windowed mode, or `-fscr` / `--fullscreen`
to force fullscreen mode.
Use `-res` / `--resolution WIDTHxHEIGHT` (for example, `-res 1280x720`) to
force the video resolution.

Set TypeScript or JavaScript breakpoints in `index.ts` / `index.js`, then press F5.
The adapter is dependency-free and is started automatically by the included task (localhost port 4714). The task waits for it to accept connections before VS Code starts the debug session.

Pass your game and game-data arguments through the optional debug-only header directive near the top of `index.ts` or
`index.js`:

```ts
/// vbd.params="-l 1 -v 1"
```

For example: `/// vbd.params="-gm duke -g ~/games/duke3da/duke3d.grp"`.
Quoted values are supported; the directive is passed directly as arguments and is never evaluated by a shell.
