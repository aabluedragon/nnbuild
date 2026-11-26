# Quick start

This page creates a script mod with the built-in generator, runs it, and explains the edit-run loop.

## 1. Generate a project

Open a terminal in the folder you start the game from. Run one of these:

```sh
./nnbuild --gen-ts my-mod     # TypeScript starter
./nnbuild --gen-js my-mod     # JavaScript starter
```

Short forms: `-gts my-mod` and `-gjs my-mod`. The name can also be attached with `=`, for example `--gen-ts=my-mod`.

The program creates the project, prints its location, and exits. No game window opens.

```text
Created TypeScript project: /home/me/games/autojs/my-mod
```

Rules for the name:

- Only letters, digits, `-` and `_` are allowed.
- If you leave the name out, the generator picks the first free name `mod-1`, `mod-2`, …
- An existing folder is never overwritten. If `autojs/my-mod` already exists, the generator stops with `Could not create JavaScript project.`

The project is always created in `autojs/<name>/` inside the folder you ran the command from.

## 2. What the generator creates

```text
autojs/
└── my-mod/
    ├── index.ts        the entry point; the only file the game loads by itself
    ├── nnb.d.ts        type declarations for your editor
    ├── tsconfig.json   settings for your editor and for tsc
    ├── package.json    optional type check: npm run check
    └── .vscode/
        ├── launch.json
        ├── tasks.json
        ├── README.md
        ├── nnbuild-debug-adapter.js
        └── nnbuild-start-debug-adapter.js
```

With `--gen-js` you get `index.js` and `jsconfig.json` instead of `index.ts` and `tsconfig.json`. Everything else is the same.

The `.vscode` files are explained in [Debugging with VS Code](debugging-vscode.md). The other files are explained in [Project layout](project-layout.md).

### The TypeScript starter

`index.ts`:

```ts
// NNBuild auto-loads only index.ts (or index.js) from this project.
// Optional debug-only launch args, for example:
// /// nnbd.params="-gm duke -g ~/games/duke3da/duke3d.grp -sc"
import { on, log } from 'nnb';

let frames = 0;
on('start', () => {
  log('TypeScript mod loaded.');
});

on('frame', () => {
  const breakpointProbe = `TypeScript frame ${++frames}`;
  if (frames % 60 === 0) log(breakpointProbe);
});
```

`tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2020", "lib": ["ES2020"], "module": "ESNext",
    "moduleResolution": "Bundler", "allowImportingTsExtensions": true, "strict": true,
    "noEmit": true, "skipLibCheck": true
  },
  "include": ["*.ts", "nnb.d.ts"]
}
```

`package.json`:

```json
{
  "name": "nnbjs-mod",
  "private": true,
  "version": "0.1.0",
  "description": "NNBuild auto-loaded JavaScript mod",
  "types": "./nnb.d.ts",
  "scripts": { "check": "tsc --noEmit -p tsconfig.json" },
  "devDependencies": { "typescript": "^5.0.0" }
}
```

### The JavaScript starter

`index.js`:

```js
// NNBuild auto-loads only index.js (or index.ts) from this project.
// Optional debug-only launch args, for example:
// /// nnbd.params="-gm duke -g ~/games/duke3da/duke3d.grp -sc"
// @ts-check
/** @type {typeof import('nnb').default} */
const nnb = require('nnb');

let frames = 0;
nnb.on('start', () => {
  nnb.log('JavaScript mod loaded.');
});

nnb.on('frame', () => {
  const breakpointProbe = `JavaScript frame ${++frames}`;
  if (frames % 60 === 0) nnb.log(breakpointProbe);
});
```

`jsconfig.json`:

```json
{
  "compilerOptions": { "target": "ES2020", "lib": ["ES2020"], "checkJs": true, "allowJs": true, "noEmit": true, "strict": true },
  "include": ["*.js", "nnb.d.ts"]
}
```

The JavaScript `package.json` is the same as the TypeScript one, except that the `check` script is `tsc --noEmit -p jsconfig.json`.

What the starter does:

- `start` runs once, after the script has been loaded. The starter writes one line to the log.
- `frame` runs on every drawn frame. The starter writes a line every 60 frames.
- The third comment line shows the optional `nnbd.params` directive. It is inactive as generated, because it starts with `// ///`. See [Debugging with VS Code](debugging-vscode.md#launch-arguments-the-nnbdparams-directive).

## 3. How the game finds and loads it

You do not register the mod anywhere. At startup the game looks for a folder named `autojs` in the folder you started it from. For every sub-folder it loads `index.ts`. If there is no `index.ts`, it loads `index.js`.

Scripts are not loaded at the very first instant. The game loads them during its first frames, on the title screen or in the level you launched into. Then it fires `start`.

More ways to load scripts are listed in [Project layout](project-layout.md#how-a-script-gets-loaded).

## 4. Run it

Start the game from the same folder, the one that contains `autojs/`:

```sh
./nnbuild --game duke --grp /path/to/duke3d.grp
```

The game log shows these lines. The log is the file `nnbuild.log` and the in-game console.

```text
[js] loaded /home/me/games/autojs/my-mod/index.ts
[js] TypeScript mod loaded.
[js] TypeScript frame 60
[js] TypeScript frame 120
```

Scripts run fully in the Duke runtime games and partly in Shadow Warrior. See [Which games run scripts](overview.md#which-games-run-scripts).

## 5. The edit-run loop

1. Edit `index.ts` or `index.js` in any editor.
2. Start the game again. Scripts are read once per launch. There is no reload while the game runs.
3. Read the log. Use `log()` or `console.log()` to print values.

If a script has a syntax error or throws an error that nothing catches, the game shows the error text in a dialog and closes. Fix the script and start again. See [how errors are reported](debugging-vscode.md#how-errors-are-reported).

Try a first change. Replace the `start` handler with this and restart:

```ts
import { on, log, drawText } from 'nnb';

on('start', () => {
  log('TypeScript mod loaded.');
  drawText(1, 8, 8, 'My first mod');
});
```

`My first mod` now stays in the top-left corner of the screen.

## 6. Optional: check types with Node.js

The game does not check types. It only removes them. To get real type errors, install Node.js and run this inside the project folder:

```sh
npm install
npm run check
```

This runs the TypeScript compiler without producing files. Your editor shows the same errors if it understands `tsconfig.json`.

Two things to keep in mind:

- The game's own TypeScript support is limited. Code that passes `npm run check` can still fail in the game. Read [TypeScript support and its limits](project-layout.md#typescript-support-and-its-limits) before you write much TypeScript.
- The generated `nnb.d.ts` differs from the game in a few details. See [the list](project-layout.md#where-nnbdts-differs-from-the-game).

## Without the generator

The generator is a convenience. A mod is only a folder with an entry file:

```text
autojs/hello/index.js
```

```js
nnb.on('start', () => nnb.log('hello'));
```

You can also load one file directly with `--js`:

```sh
./nnbuild --js ./hello.js
```
