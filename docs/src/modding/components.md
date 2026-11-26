# Components

A component is a class that holds behavior for one thing: a sprite, or an empty object called a node. The game calls the component's methods at fixed moments. If you have used Unity, the model will look familiar.

Components are part of the `nnb` module and work only in the **Duke runtime** games.

> NNBuild has two things called components. This page describes the class-based components you create in code. Scripts that the map editor attaches to map objects are a different feature with different hooks. See [Map script components](map-script-components.md).

## A first component

```ts
import { NNBComponent, createNode, log } from 'nnb';

class Hello extends NNBComponent {
  start() {
    log('Hello component started');
  }

  update(dt: number) {
    // called every frame; dt is the time since the last frame, in seconds
  }
}

createNode(Hello);
```

1. Write a class that extends `NNBComponent`.
2. Override the methods you need.
3. Attach it to a sprite with `attach(sprite, Hello)`, or create a node for it with `createNode(Hello)`.

## Lifecycle methods

Override any of these. All are optional.

| Method | Called |
| --- | --- |
| `awake()` | When the component is attached. First call. |
| `onEnable()` | When the component becomes active: right after `awake()` if it is enabled, and every time it is enabled again later. |
| `onSpawn()` | When the component is attached, after `onEnable()`. |
| `start()` | When the component is attached, after `onSpawn()`. |
| `fixedUpdate(step)` | In fixed steps of 1/60 second, zero or more times per frame. `step` is always `1/60`. |
| `update(dt)` | Once per frame. `dt` is the time since the previous frame in seconds. |
| `lateUpdate(dt)` | Once per frame, after every component's `update`. |
| `onDisable()` | When the component stops being active: it was disabled, its node was deactivated, or it is being destroyed. |
| `onDestroy()` | When the component is removed: detached, destroyed, or its sprite was removed. Last call. |

Attaching calls `awake`, `onEnable`, `onSpawn` and `start` immediately, in that order, before `attach` returns.

A component is *active* when it is enabled and, if it sits on a node, the node is active and not destroyed. Only active components receive the three update calls.

### How the update calls are timed

The update calls are driven by the `frame` event.

- They run on every drawn frame. That includes the title screen, menus and a paused game.
- `dt` is measured with the computer's clock. It is `0` on the first frame and never larger than `0.25`.
- `fixedUpdate` runs as often as needed to keep up with real time, in steps of 1/60 second, at most 4 steps per frame. It is not tied to the game's own simulation tick.
- The update loop is installed the first time you attach a component that overrides `update`, `fixedUpdate` or `lateUpdate`.

## Properties and methods of a component

| Member | Meaning |
| --- | --- |
| `sprite` | The sprite index the component is attached to, or `-1`. |
| `node` | The `NNBNode` the component is attached to, or `null`. |
| `gameObject` | Same as `node`. |
| `transform` | The node's `transform`, or `undefined` for a component on a sprite. |
| `enabled` | `true` or `false`. Assign it to turn the component on or off. |
| `isActiveAndEnabled` | `true` when the component is active. Read-only. |
| `GetComponent(type)` | Another component on the same sprite or node. `undefined` if there is none. |
| `GetComponents(type?)` | All components on the same sprite or node, optionally filtered by class. |
| `AddComponent(type)` | Attaches another component to the same sprite or node and returns it. |
| `Destroy()` | Removes this component. Returns `true` on success. |

## Attaching to sprites

```ts
import { attach, detach, getComponent, getComponents } from 'nnb';

const pulse = attach(spriteIndex, Pulse);     // create and attach
const same = getComponent(spriteIndex, Pulse);
const all = getComponents(spriteIndex);
detach(spriteIndex, pulse);
```

- `attach(entity, component)` takes a sprite index or a node. `component` is a class, or an instance you created with `new`. It returns the component instance.
- A sprite can have several components, also several of the same class.
- `attach` throws when `entity` is not a non-negative whole number or a node, when the class does not extend `NNBComponent`, or when the instance is already attached somewhere.
- `attach` does not check that the sprite exists.
- After `detach` or `Destroy()` an instance can be attached again.

### When the sprite goes away

When a sprite is removed during play, all its components are destroyed automatically: `onDisable` if they were active, then `onDestroy`. Inside `onDestroy` the sprite still exists and `this.sprite` is still set.

This cleanup depends on the `actorDestroyed` event. Two things follow:

- **Level changes do not destroy components.** Loading another level replaces all sprites without firing `actorDestroyed`. A component stays attached to its index, and that index now belongs to a different sprite. Guard against this as the worked example below does, or detach your components yourself.
- **Do not call `off('actorDestroyed')` or `off('frame')`.** `off` removes every handler of an event, including the ones the component system uses.

## Nodes

A node is an empty object that is not part of the map. Use it for behavior that does not belong to one sprite, such as a HUD element or a game rule.

```ts
import { createNode, destroyNode } from 'nnb';

const node = createNode(Scoreboard, Timer);   // a node with two components
node.name = 'match rules';
node.SetActive(false);                        // components get onDisable
node.SetActive(true);                         // components get onEnable
destroyNode(node);                            // components get onDisable, onDestroy
```

| Member | Meaning |
| --- | --- |
| `id` | A unique number, assigned in creation order starting at `1`. |
| `name` | Free text. Starts as `Node <id>`. |
| `tag` | Free text. Starts as `Untagged`. |
| `transform` | `{ position, rotation, scale }`, each `{ x, y, z }`. Plain numbers for your own use. The game does not read them. |
| `activeSelf` | `true` or `false`. Assigning it is the same as `SetActive`. `nnb.d.ts` marks it read-only, so in TypeScript call `SetActive`. |
| `activeInHierarchy` | `true` when the node is active and not destroyed. Read-only. |
| `destroyed` | `true` after the node was destroyed. |
| `SetActive(active)` | Activates or deactivates the node. Enabled components get `onEnable` or `onDisable`. |
| `AddComponent(type)` | Attaches a component and returns it. |
| `GetComponent(type)`, `GetComponents(type?)` | Look up components on the node. |
| `Destroy()` | Same as `destroyNode(node)`. |

`new NNBNode()` also creates a node, without components. A destroyed node cannot take new components.

## Prefabs

A prefab is a recipe: a tile number and the component classes that every spawned copy gets.

```ts
import { createPrefab, spawnPrefab, define } from 'nnb';

const pulsingTrooper = createPrefab(define('LIZTROOP') ?? 0, Pulse);

const sprite = spawnPrefab(pulsingTrooper);            // at the local player
const other = spawnPrefab(pulsingTrooper, x, y, z);    // at a map position
```

- `createPrefab(tile, ...components)` returns an `NNBPrefab` with the fields `tile` and `components`. `new NNBPrefab(tile, ...components)` does the same, but `nnb.d.ts` does not declare its arguments. Use `createPrefab`.
- `spawnPrefab(prefab, x?, y?, z?, owner?)` calls [`spawn`](api-reference.md#spawn) with the prefab's tile. If that succeeds, it attaches a new instance of each component class to the new sprite. It returns the sprite index, or `-1`.
- Pass classes, not instances, to `createPrefab`. An instance can only be attached once.

> These prefabs exist only in code. The map editor has its own *prefab* assets (`.nnbprefab` files), which are saved groups of sprites. The two are unrelated.

## Worked example: pulsing enemies

This mod makes every trooper slowly grow and shrink. It shows a component on a sprite, per-instance state, cleanup, and the guard for level changes.

```ts
import { NNBComponent, attach, on, define, getActor, setActor, log } from 'nnb';

class Pulse extends NNBComponent {
  tile = -1;
  time = 0;

  onSpawn() {
    this.tile = getActor(this.sprite, 'picnum') ?? -1;
    log(`Pulse attached to sprite ${this.sprite}`);
  }

  update(dt: number) {
    // The sprite slot now holds something else (for example after a level change).
    if (getActor(this.sprite, 'picnum') !== this.tile) {
      this.Destroy();
      return;
    }
    this.time += dt;
    const size = 40 + Math.round(8 * Math.sin(this.time * 6));
    setActor(this.sprite, 'xrepeat', size);
    setActor(this.sprite, 'yrepeat', size);
  }

  onDestroy() {
    log(`Pulse removed from sprite ${this.sprite}`);
  }
}

let trooper: number | undefined;

on('start', () => {
  trooper = define('LIZTROOP');
});

on('actorSpawned', (e) => {
  if (e.tile === trooper) attach(e.sprite, Pulse);
});
```

How it works:

- `define('LIZTROOP')` looks up the trooper's tile number in the game's CON constants. It runs in `start`, when the constants are available.
- `actorSpawned` fires for every actor. The handler attaches a new `Pulse` to each trooper.
- `onSpawn` remembers which tile the sprite had.
- `update` runs every frame. It first checks that the sprite still has that tile. If not, the index was reused, and the component removes itself. Otherwise it changes the sprite's size.
- When the trooper's sprite is removed by the game, `onDestroy` runs automatically.

`time` and `tile` are normal class fields. Every attached instance has its own copy.

## Worked example: a node with a lifecycle

```ts
import { NNBComponent, createNode, destroyNode, log } from 'nnb';

class Ticker extends NNBComponent {
  seconds = 0;

  awake() { log('awake'); }
  onEnable() { log('onEnable'); }
  onSpawn() { log('onSpawn'); }
  start() { log('start'); }

  fixedUpdate(step: number) {
    this.seconds += step;
  }

  onDisable() { log('onDisable'); }
  onDestroy() { log(`onDestroy after ${this.seconds.toFixed(1)} s`); }
}

const node = createNode(Ticker);          // logs: awake, onEnable, onSpawn, start
node.transform.position = { x: 0, y: 0, z: 0 };

setTimeout(() => node.SetActive(false), 5000);   // logs: onDisable
setTimeout(() => node.SetActive(true), 6000);    // logs: onEnable
setTimeout(() => destroyNode(node), 10000);      // logs: onDisable, onDestroy
```

The ticker counts only while its node is active, so the final log shows about 9 seconds.

## Limits and pitfalls

- An error thrown inside a lifecycle method closes the game, like any uncaught script error.
- Inside `update`, `fixedUpdate` and `lateUpdate`, a component may destroy itself. Do not destroy components of *other* sprites or nodes from inside these methods. Do it later, for example with `setTimeout(() => other.Destroy(), 0)`.
- Components are local to the computer that runs the script. A component that changes the game world in multiplayer must run identically on every computer. See [Keeping the game in sync](server-scripts.md#keeping-the-game-in-sync).
- In TypeScript, keep to the supported subset. Class fields, typed method parameters and `extends` work. `implements`, generic classes and constructor parameter properties do not. See [TypeScript support and its limits](project-layout.md#typescript-support-and-its-limits).
