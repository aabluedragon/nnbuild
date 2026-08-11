# VibeBuild32 debugging in VS Code

Open this `my-mod` folder in VS Code. In **Run and Debug**, select either:

- **Launch VibeBuild32 (Duke)** — starts the game with DevTools enabled, then attaches.
- **Attach to running VibeBuild32** — attaches to a game already started with `-dt`.

Set TypeScript or JavaScript breakpoints in `index.ts` / `index.js`, then press F5.
The adapter is dependency-free and is started automatically by the included task.

To append game arguments only in debug mode (VS Code Launch or `-dt` /
`--devtools`), place this header directive near the top of `index.ts` or
`index.js`:

```ts
/// vbd.params="-l 1 -v 1"
```

Quoted values are supported; the directive is passed directly as arguments and
is never evaluated by a shell.
