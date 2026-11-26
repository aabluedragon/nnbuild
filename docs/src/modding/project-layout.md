# Project layout

This page explains how the game finds scripts, how `import` and `require` find other files, which TypeScript the game understands, and what the generated configuration files are for.

## How a script gets loaded

| Way | Example | Notes |
| --- | --- | --- |
| Auto-loaded project | `autojs/my-mod/index.ts` | Loaded at every start. No option needed. |
| `--js <file>` | `./vibebuild32 --js mods/test.js` | Short form `-js`. Can be given several times. `--js=<file>` also works. |
| Bare path | `./vibebuild32 ./server.ts` | A command-line word that does not start with `-` and ends in `.js`, `.ts`, `.mjs`, `.mts`, `.cjs` or `.cts`. Duke runtime only; use `--js` for Shadow Warrior. |
| Script attached to a map object | `Scripts/Station.js` in an editor project | A different model with its own hooks. See [Map script components](map-script-components.md). |

### Auto-loaded projects

At startup the game scans the folder `autojs`. It looks exactly one level deep:

- `autojs/<name>/index.ts` is loaded if it exists.
- Otherwise `autojs/<name>/index.js` is loaded.
- If both exist, only `index.ts` is loaded.
- Folders whose name starts with `.` are skipped.
- Files directly inside `autojs/`, and files in deeper folders, are not loaded by themselves. Your entry file must `import` them.

The game scans `autojs` in two places:

1. The folder you started the program from.
2. The folder the game runs in, if it is a different one. The game switches to the folder of the game data it selected. A stand-alone editor project runs inside its own project folder.

The second scan is skipped when you play an editor project from a ZIP file and did not pass `--allow-project-scripts`.

### Files given on the command line

A relative `--js` path is resolved against the folder you started the program from.

After a `-net` option, a bare path is only treated as a script if the file exists. Put script paths before `-net`, or use `--js`.

### Load order and limits

- Scripts from `--js` and bare paths are loaded first, in command-line order. Auto-loaded projects follow, in the order the operating system lists the folders. Do not rely on the order between two auto-loaded mods.
- A file is loaded only once, even if it is named twice.
- At most 64 entry scripts are loaded.
- All scripts run in one JavaScript runtime. They share the global object, the `vb` event handlers and the HUD text ids. Each entry file is its own module, so top-level `const` and `let` names do not collide.
- Scripts are loaded once per launch. There is no hot reload.

### Scripts in an editor project

An editor project can contain scripts in two forms:

- **Script components** attached to sprites, sectors and walls. They are listed in the map's settings file (`<map>.vbmap.json`) and need Modern Mode. See [Map script components](map-script-components.md).
- An `autojs/` folder inside a stand-alone project. It is scanned as described above.

When the project is a folder, both forms run without extra options. When the project is a ZIP, scripts are treated as untrusted and do not run unless you pass `--allow-project-scripts`:

```sh
./vibebuild32 --allow-project-scripts --project "/path/to/My Project.zip"
```

Only pass this option for projects you trust. A script can read and write files and open network connections.

## Entry files are ES modules

Every entry file is loaded as an ES module. This applies to all extensions, including `.cjs`.

- `import` and `export` work.
- The code runs in strict mode.
- These globals exist without an import: `vb`, `cvar`, `console`, `fetch`, `require`, `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`.
- `fs`, `dgram` and `net` are not globals. Import or require them.

## Module resolution

### Built-in modules

These names always refer to built-in modules:

`vb`, `cvar`, `fs`, `node:fs`, `fs/promises`, `node:fs/promises`, `dgram`, `node:dgram`, `net`, `node:net`

Each has named exports and a default export:

```ts
import { on, log } from 'vb';      // named
import vb from 'vb';               // default: one object with everything
import fs from 'fs';
```

See [API reference](api-reference.md) and [Built-in modules](modules.md).

### `import` of your own files

```ts
import { clamp } from './util.ts';
import { table } from '../shared/table.js';
```

- A path is resolved against the folder of the file that contains the `import`.
- Write the complete file name with its extension. The game does not try `.js`, `.ts` or `index` files for you.
- A name that is not a built-in module is always treated as a file path. There is no `node_modules` lookup. `import x from 'lodash'` looks for a file named `lodash` next to your script.
- Absolute paths work.
- Files ending in `.ts`, `.mts` or `.cts` are converted from TypeScript. All other files are read as JavaScript.
- A module is identified by its path text. Import a file with the same spelling everywhere, or it is loaded twice.

### `require`

`require` is a global function.

```js
const vb = require('vb');
const fs = require('fs');
const util = require('./util.js');
```

- For built-in names it returns an object with the module's functions.
- For files it works like CommonJS: the file gets `exports`, `module` and `require`, and `require` returns `module.exports`. The result is cached per path.
- Write the complete file name with its extension.
- A relative path is resolved against the folder of the **entry script that was loaded last**. It is not resolved against the file that calls `require`.

Because of the last rule:

- Call `require` for your own files only at the top level of your entry file, while it is being loaded. A `require` that runs later, for example inside an event handler, can look in another mod's folder.
- A file loaded with `require` cannot reliably `require` files from its own sub-folder.
- In map script components, use `import`.

Prefer `import` for your own files. It does not have these problems.

## TypeScript support and its limits

The game does not contain the TypeScript compiler. A small built-in converter removes type syntax and passes the rest to the JavaScript engine. It does not check types and it does not understand all of TypeScript.

The converter works on text patterns. Some valid TypeScript is converted wrongly. Read this section before you write a larger mod in TypeScript. If you want no surprises, write JavaScript with `// @ts-check` and JSDoc types, as the JavaScript starter does.

### What is handled

| Feature | Example |
| --- | --- |
| Type annotations on variables at the top level, in `function` bodies, in class bodies and methods, and in `if`/`for`/`while` blocks | `const n: number = 1;` |
| Parameter types, optional parameters, rest parameters, destructured parameters | `(e: ActorEvent, n?: number, ...rest: string[])` |
| Return types | `function f(): void {}`, `(x: number): number => x * 2` |
| `interface` declarations | `interface P { x: number; y: number }` |
| Simple `type` aliases | <code>type Id = number &#124; string;</code> |
| `import type …` on one line, and `export type …` on one line with no `;` before its end | `import type { ActorEvent } from 'vb';` |
| `as` casts, `as const`, non-null `!` after a name, `)` or `]` | `value as string`, `list[0]!.name` |
| `public`, `private`, `protected`, `readonly`, `override` on class members, and `abstract class` | `private readonly max = 3;` |
| Class fields with types, optional fields | `count: number = 0; hp?: number;` |
| Type arguments on calls and `new`, generic function declarations | `new Map<string, number>()`, `function id<T>(v: T): T` |
| `enum` with number or string members, also `export enum` | `enum Color { Red, Green = 5 }` |
| `catch (e: unknown)` | |

An `enum` becomes a plain object: `enum Color { Red, Green = 5 }` becomes `const Color = { Red:0, Green:5 }`. There is no reverse lookup from number to name.

### What is not handled

These produce a syntax error when the game loads the file, unless noted.

| Not supported | Use instead |
| --- | --- |
| A type annotation on `const` or `let` directly inside an arrow function body: `() => { const n: number = 1; }` | Leave the annotation out, use a cast, or use a `function`. Annotations in the parameter list and in nested blocks are fine. |
| `switch` statements: the `:` after `case x` and `default` is taken for a type. Statement labels have the same problem. | `if` / `else if` |
| `import * as name from …` | A default import: `import vb from 'vb';` |
| `import { type X, y }` | A separate `import type { X }` line |
| `export interface …`, and `export type …` that spans lines or contains `;` | Do not export types. Keep them in the file that uses them, or in a `.d.ts` file. |
| A `type` alias whose object type contains a function type followed by more members | An `interface`, or method syntax: `reply(text: string): void` |
| `const enum` | `enum` |
| `declare …` statements | A `.d.ts` file |
| Function overload signatures, `abstract` methods | One implementation |
| `implements`, generic classes (`class Box<T>`), generic arrow functions (`<T>(x: T) => x`) | Plain classes and `function` declarations |
| `satisfies`, casts written `<T>value`, `namespace`, decorators, JSX | |
| Constructor parameter properties: `constructor(private x: number)`. No error, but the field is **not** created. | Declare the field and assign it in the constructor. |
| TypeScript keywords used as variable names, property names or object keys: `interface`, `enum`, `public`, `private`, `protected`, `readonly`, `abstract`, `declare`, `override`. For example `{ public: true }`. | Other names. |
| The name `type` directly followed by another word, for example `for (const type of list)`. `{ type: 'x' }` and `e.type === 'x'` are fine. | Another name, such as `kind`. |

### Code that is converted wrongly without an error

These are the dangerous cases. The file loads, but it does something else than you wrote.

| You write | The game runs | Write this instead |
| --- | --- | --- |
| `return !ok;` | `return ok;` | `return (!ok);` |
| `return { a: 1, b: 2 };` | `return { a, b };` | `return ({ a: 1, b: 2 });` |
| `const { a: b } = obj;` | `const { a } = obj;` | `const b = obj.a;` |
| `import { on as listen } from 'vb';` | `import { on } from 'vb';` | Import `on` and use that name. |
| `/^(\w+):(.*)$/` | `/^(\w+)$/` | `new RegExp('^(\\w+):(.*)$')` |
| `a < b && c > (d)` | `a (d)` | `c > (d) && a < b`, or leave out the parentheses after `>` |

The first rule covers every `!` that directly follows a keyword such as `return`, `typeof`, `case`, `throw` or `await`. Wrap the expression in parentheses.

The second and third rules exist because the converter decides from the previous word whether `{` starts an object or a block. After `=`, `(`, `,`, `:` and `?` it is an object. After a keyword or a name it is a block, and `key: value` inside is taken for a type annotation.

### Rules that keep you safe

1. End every statement with `;`.
2. Never start a `return` value with `!` or `{`. Wrap it in `(` `)`.
3. Do not annotate local variables inside arrow function bodies.
4. Use `if` / `else if`, not `switch`.
5. Rename in two steps instead of with `as` or `key: name` patterns.
6. Keep types simple and on one line where you can.
7. Use default imports for modules: `import fs from 'fs';`.

### Line numbers

The converter keeps the line structure of your code, so line numbers in error messages and breakpoints match your source. There is one exception. An `interface`, a `type` alias or a type annotation that spans several lines is removed together with its line breaks. Every line after it is then reported with a lower number. Keep types on one line, or put multi-line declarations at the end of the file, or in a `.d.ts` file.

## The configuration files

None of these files is read by the game. They exist for your editor and for the optional type check.

| File | Role |
| --- | --- |
| `vb.d.ts` | Type declarations for the `vb` module, the built-in modules, the globals, and map script components. Your editor uses it for completion and type errors. |
| `tsconfig.json` | TypeScript projects. Tells the editor and `tsc` to check `*.ts` and `vb.d.ts`, to target ES2020, and to produce no output (`noEmit`). `allowImportingTsExtensions` lets you write `import './util.ts'`, which the game requires. |
| `jsconfig.json` | JavaScript projects. Turns on type checking of `.js` files (`checkJs`) against `vb.d.ts`. |
| `package.json` | Lets you run `npm install` and `npm run check`. Only needed for the type check. |
| `.vscode/` | Debugger setup. See [Debugging with VS Code](debugging-vscode.md). |

Both configuration files only include files in the project folder itself (`*.ts` or `*.js`). If you put code in sub-folders, add them to `include`.

### Where `vb.d.ts` differs from the game

`vb.d.ts` is written by the generator. It is a copy of a text stored in the game. It declares the `vb` module, the built-in modules, `fetch`, the timers, and the types for [map script components](map-script-components.md) (`MapComponentContext` and `MapComponentModule`).

In a few details the declarations do not match what the game does:

| `vb.d.ts` says | The game does |
| --- | --- |
| 112 CON event names are known. | All 165 names work. For the other 53 the type check gives the handler an untyped argument. See [Events](events.md#names-missing-from-vbdts). |
| `VBNode.activeSelf` is read-only. | It can be assigned. Assigning it is the same as calling `SetActive`. In TypeScript call `SetActive`. |
| `new VBPrefab()` takes no arguments. | `new VBPrefab(tile, ...components)` works. Use `createPrefab`, which is declared correctly. |
| `getSector`, `setSector`, `getWall`, `setWall` and the projectile and tile data functions accept an extra `parm2` argument. | The argument is ignored for these functions. Only actor, player and userdef members use it. |
| `require` is typed for `'vb'`, `'fs'`, `'dgram'`, `'net'` and `'cvar'`. | `'fs/promises'` and the `node:` names also work, but the type check sees their result as `unknown`. Use `import` for them. |
