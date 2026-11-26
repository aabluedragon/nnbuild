# Built-in modules

Besides [`vb`](api-reference.md), scripts can use a few modules and global functions that look like their Node.js or browser counterparts.

This is not Node.js. Only the functions listed on this page exist. A function that Node.js has and this page does not list is not available.

| Name | Kind | Import | Purpose |
| --- | --- | --- | --- |
| [`fs`](#fs) | module | `fs`, `node:fs` | Files and folders |
| [`fs/promises`](#fspromises) | module | `fs/promises`, `node:fs/promises` | Promise versions of the file functions |
| [`dgram`](#dgram) | module | `dgram`, `node:dgram` | UDP sockets |
| [`net`](#net) | module | `net`, `node:net` | Outgoing TCP connections |
| [`cvar`](#cvar) | module and global | `cvar` | Console commands |
| [`fetch`](#fetch) | global | | HTTP and HTTPS requests |
| [Timers](#timers) | globals | | `setTimeout`, `setInterval`, … |
| [`console`](#console) | global | | Logging |
| [`require`](#require) | global | | CommonJS-style loading |

All of these work in every game that runs scripts, except `cvar`, which needs the Duke runtime.

## How callbacks are scheduled

The JavaScript engine runs on the game's main thread. Callbacks do not interrupt your code. The game runs them once per frame, in this order:

1. UDP messages, TCP events, finished `fetch` requests.
2. `fs` callbacks.
3. The `frame` event.
4. Timers that are due.
5. Promise continuations (code after `await`, `.then` handlers).

So a timer or a network callback is never more exact than one frame. On a headless server the loop still runs, and all of this works.

An error that a callback does not catch closes the game. See [how errors are reported](debugging-vscode.md#how-errors-are-reported).

## Promises and errors

A promise that is rejected at a moment when no handler is attached to it closes the game with an "unhandled promise rejection" error. The game does not wait to see whether you attach a handler later.

This has practical consequences:

- An `async` function that throws before its first `await` ends the game, even when the caller uses `try`/`catch` or `.catch()`. Put a `try`/`catch` **inside** the `async` function.
- The functions of `fs.promises` reject immediately when the operation fails. Such a failure ends the game. Use the `…Sync` functions with `try`/`catch`, or check with `existsSync` first.
- `response.json()` rejects immediately when the body is not valid JSON. Use `JSON.parse(await response.text())` inside `try`/`catch`.
- `await fetch(…)` inside `try`/`catch` is safe. The request fails later, when the handler is already attached.

## fs

```ts
import fs from 'fs';
import { readFileSync, writeFileSync } from 'fs';
```

A relative path is relative to the folder the game runs in. That is usually the folder of the selected game data, not the folder of your script. Use absolute paths if the location matters.

### Synchronous functions

| Function | Returns | Description |
| --- | --- | --- |
| `readFileSync(path, options?)` | `Uint8Array` or string | Reads a whole file. |
| `writeFileSync(path, data)` | `undefined` | Creates or replaces a file. |
| `appendFileSync(path, data)` | `undefined` | Appends to a file. Creates it if needed. |
| `existsSync(path)` | boolean | `true` if the file can be opened for reading. |
| `unlinkSync(path)` | `undefined` | Deletes a file. |
| `renameSync(oldPath, newPath)` | `undefined` | Renames or moves a file. |
| `mkdirSync(path)` | `undefined` | Creates one folder. No error if it already exists. |
| `readdirSync(path)` | array of strings | The names in a folder, without `.` and `..`. |
| `statSync(path)` | object | `{ size, isFile(), isDirectory() }`. Named `FSStats` in `vb.d.ts`. |

- **Text or bytes.** `readFileSync(path)` returns a `Uint8Array`. To get a string, pass an encoding: `readFileSync(path, 'utf8')` or `readFileSync(path, { encoding: 'utf8' })`. The file is always read as UTF-8, whatever encoding name you pass.
- **`data` is text.** `writeFileSync` and `appendFileSync` convert `data` to a string. A `Uint8Array` is written as its text form (`1,2,3`), not as bytes. Binary writing is not supported.
- **Errors.** A failed operation throws an error, for example `fs.readFile: cannot open file`. Catch it with `try`/`catch`. `existsSync` never throws.
- `mkdirSync` does not create parent folders. Options are ignored.
- `readdirSync` and `statSync` ignore their options argument.

### Callback functions

| Function | Callback receives |
| --- | --- |
| `readFile(path, callback)` or `readFile(path, options, callback)` | `(error, data)` |
| `writeFile(path, data, callback)` | `(error)` |
| `appendFile(path, data, callback)` | `(error)` |
| `exists(path, callback)` | `(exists)` |
| `unlink(path, callback)` | `(error)` |
| `rename(oldPath, newPath, callback)` | `(error)` |

- `error` is `null` on success.
- The file operation itself happens immediately. Only the callback is delayed until the next frame.
- Without a callback these functions behave exactly like their `…Sync` versions and return the result directly.

### fs.promises

`fs.promises` is an object with six functions. Each returns a promise.

`readFile(path, options?)`, `writeFile(path, data)`, `appendFile(path, data)`, `exists(path)`, `unlink(path)`, `rename(oldPath, newPath)`

The same object is the module `fs/promises`:

```ts
import promises from 'fs/promises';
import { on, log } from 'vb';

on('start', async () => {
  await promises.writeFile('notes.txt', 'hello');
  const text = await promises.readFile('notes.txt', 'utf8');
  log(`notes.txt contains: ${text}`);
});
```

Read the warning under [Promises and errors](#promises-and-errors): a failing call ends the game. Use these functions only where failure is not expected.

### Not implemented

Everything else from Node's `fs`, including: `open`/`read`/`write`/`close` on file handles, streams, `copyFile`, `rm`, `rmdir`, `readdir` and `stat` in callback or promise form, `watch`, `access`, `chmod`, recursive `mkdir`, file descriptors, `Buffer`.

## dgram

UDP sockets.

```ts
import dgram from 'dgram';

const socket = dgram.createSocket('udp4');
socket.on('message', (message, rinfo) => {
  // message is a Uint8Array; rinfo is { address, port, family, size }
});
socket.bind(40123, '0.0.0.0', () => {
  // ready to receive
});
socket.send('hello', 40123, '127.0.0.1');
```

| Function | Returns | Description |
| --- | --- | --- |
| `dgram.createSocket(type, onMessage?)` | socket | `type` is `'udp4'` or `'udp6'`. An optional function is added as a `message` listener. |
| `socket.on(event, listener)` | the socket | Adds a listener. |
| `socket.bind(port, callback?)` | the socket | Starts receiving on `port`, on the loopback address only. |
| `socket.bind(port, address, callback)` | the socket | Starts receiving on `port` at `address`. |
| `socket.send(message, port, address?, callback?)` | `undefined` | Sends one datagram. `message` is a string or a `Uint8Array`. |
| `socket.close(callback?)` | `undefined` | Closes the socket. |
| `socket.address()` | `{ address, port, family }` | The local address. Throws if the socket is closed. |

Events:

| Event | Listener receives | When |
| --- | --- | --- |
| `message` | `(message, rinfo)` | A datagram arrived. `message` is a `Uint8Array`. `rinfo` is `{ address, port, family, size }`. `family` is `'IPv4'` or `'IPv6'`. |
| `listening` | nothing | `bind` succeeded. |
| `close` | nothing | The socket was closed. |
| `error` | `(error)` | Receiving failed. |

Things to know:

- **`bind(port)` listens on loopback only.** Without an address the socket accepts datagrams only from the same computer. To receive from other computers, pass an address **and** a callback: `socket.bind(port, '0.0.0.0', () => {})`. The address argument is ignored when no callback follows it.
- `send` without an address sends to the same computer.
- `send` throws if the datagram cannot be sent. With a callback, the callback is called first with an error, or with `null`.
- Sockets are checked once per frame. Up to 65,536 bytes of a datagram are received.
- Keep the socket in a variable that stays alive, for example at the top of your file. A socket that is no longer referenced is closed when it is garbage-collected.
- To turn received bytes into text, use `String.fromCharCode(...message)` for plain ASCII.
- Host names are resolved while your code waits. A slow lookup pauses the game.

Not implemented: `once`, `off`/`removeListener`, `connect`, `setBroadcast`, `addMembership` and the other multicast functions, `ref`/`unref`, `send` with offset and length.

## net

Outgoing TCP connections. There is no TCP server.

```ts
import net from 'net';

const socket = net.createConnection(8080, '127.0.0.1', () => {
  socket.write('hello\n');
});
socket.on('data', (data) => {
  // data is a Uint8Array
});
socket.on('close', (hadError) => {
  // the connection is gone
});
```

| Function | Returns | Description |
| --- | --- | --- |
| `net.createConnection(port, host?, onConnect?)` | socket | Starts connecting. `host` defaults to `127.0.0.1`. The optional function is added as a `connect` listener. |
| `net.connect(…)` | socket | Same function. |
| `socket.on(event, listener)` | the socket | Adds a listener. |
| `socket.write(data, callback?)` | boolean | Sends `data` (string or `Uint8Array`). `true` if the send succeeded. |
| `socket.end(data?, callback?)` | the socket | Optionally sends `data`, then closes the sending side. |
| `socket.destroy()` | the socket | Closes the connection immediately. |
| `socket.setNoDelay(noDelay?)` | the socket | Turns off send buffering in the operating system. Default `true`. |

Events:

| Event | Listener receives | When |
| --- | --- | --- |
| `connect` | nothing | The connection is established. |
| `data` | `(data)` | Bytes arrived, as a `Uint8Array`. |
| `end` | nothing | The other side closed the connection. `close` follows. |
| `error` | `(error)` | Connecting, sending or receiving failed. `close` follows for connection and receive errors. |
| `close` | `(hadError)` | The socket is closed. `hadError` is `true` after an error. |

Things to know:

- `port` must be between 1 and 65535. Otherwise `createConnection` throws a `RangeError`.
- `createConnection` throws if the host name cannot be resolved. The lookup happens while your code waits.
- Wait for `connect` before you call `write`. A `write` on a socket that is not connected fails: it returns `false` and fires `error`.
- `write` sends at once and does not queue. Keep messages small. `write` on a closed socket throws.
- The `write` callback is called immediately, whether or not the send succeeded.
- Keep the socket in a variable that stays alive.

Not implemented: `createServer` and everything about listening, `new net.Socket()`, `setEncoding`, `pause`/`resume`, `once`, `off`, `setTimeout`, `setKeepAlive`, address properties, TLS.

## cvar

**Duke runtime.** Runs console commands. It is a module and also a global object.

```ts
import cvar from 'cvar';

cvar.run('map e1l2');
cvar.set('r_maxfps', 60);
```

| Function | Description |
| --- | --- |
| `cvar.run(command)` | Runs one console command line. |
| `cvar.set(name, value)` | Short for `cvar.run(name + ' ' + value)`. |

- Commands are queued. They run after your code returns, on the next pass of the game loop, during play and at the title menu. See [`run`](api-reference.md#console).
- `vb.run` is the same function as `cvar.run`.
- You cannot read the value of a console variable, and you get no result back.
- In Shadow Warrior the functions exist, but the commands are never executed.

## fetch

`fetch(url, options?)` returns a promise for a response object. The request runs on a background thread. The promise settles on a later frame. `fetch` is a global function. It needs no import.

Options:

| Option | Type | Meaning |
| --- | --- | --- |
| `method` | string | For example `'POST'`. Default `GET`. |
| `headers` | object | Header names and string values. |
| `body` | string or `Uint8Array` | The request body. Set `method` as well when you send a body. |

The response object:

| Member | Meaning |
| --- | --- |
| `status` | The HTTP status code. |
| `ok` | `true` for status 200 to 299. |
| `url` | The final URL, after redirects. |
| `headers.get(name)` | The value of a response header, or `null`. The name is not case-sensitive. |
| `text()` | Promise for the body as a string. |
| `json()` | Promise for the body parsed as JSON. |
| `arrayBuffer()` | Promise for the body as an `ArrayBuffer`. |

Things to know:

- An HTTP error status such as 404 does not reject the promise. Check `response.ok`.
- A network failure rejects the promise with an `Error`.
- Use `try`/`catch` around `await fetch(…)`. Read [Promises and errors](#promises-and-errors) before you use `json()`.
- You can read the body as often as you like. It is fully downloaded before the promise resolves. There is no streaming.

```ts
import { on, log } from 'vb';

on('start', async () => {
  try {
    const response = await fetch('https://example.com/api', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hello: 'world' }),
    });
    const text = await response.text();
    log(`status ${response.status}: ${text.length} characters`);
  } catch (err) {
    log(`request failed: ${err}`);
  }
});
```

Platform notes:

- **macOS** uses libcurl.
- **Linux** uses the system's libcurl, loaded on the first `fetch` call (`libcurl.so.4` or `libcurl-gnutls.so.4`). If it is not installed, the promise is rejected with `fetch: libcurl is not installed on this system`.
- **Windows** uses the HTTP client built into Windows. Name lookup and connecting time out after 10 seconds, sending and receiving after 30 seconds.
- With libcurl, redirects are followed and connecting times out after 10 seconds. There is no limit on the total duration of a request.

Not implemented: `Request`, `Response` and `Headers` constructors, `AbortController` and `signal`, `statusText`, `blob()`, `formData()`, streaming bodies, iterating over headers, the options `redirect`, `credentials`, `mode` and `cache`.

## Timers

| Function | Returns | Description |
| --- | --- | --- |
| `setTimeout(callback, ms?, ...args)` | timer id (number) | Calls `callback(...args)` once, after at least `ms` milliseconds. |
| `setInterval(callback, ms?, ...args)` | timer id (number) | Calls `callback(...args)` repeatedly, every `ms` milliseconds. |
| `clearTimeout(id)` | `undefined` | Cancels a timer. |
| `clearInterval(id)` | `undefined` | Cancels a timer. Same function as `clearTimeout`. |

- `callback` must be a function. A string of code is not accepted.
- The shortest delay is 1 millisecond. A missing or smaller `ms` becomes 1.
- Timers are checked once per frame, after the `frame` event. A timer never fires earlier than the next frame.
- An interval that falls behind does not catch up. The next call is scheduled from the moment the previous one ran.
- Timers use real time. They keep running in menus, while the game is paused, and across level changes.
- All timers are removed when the game shuts down.

Not implemented: `setImmediate`, `timer.unref()`, `timer.refresh()`.

## console

`console.log`, `console.info`, `console.warn` and `console.error` all do the same as [`vb.log`](api-reference.md#logging): they write one line to the game log. There are no log levels.

Not implemented: `console.table`, `console.time`, `console.assert`, `console.trace` and the rest.

## require

`require(name)` loads a built-in module or a file in CommonJS style.

```js
const fs = require('fs');
const helpers = require('./helpers.js');
```

Built-in names: `vb`, `cvar`, `fs`, `node:fs`, `fs/promises`, `node:fs/promises`, `dgram`, `node:dgram`, `net`, `node:net`.

For the rules that apply to your own files, see [`require`](project-layout.md#require). There is no `module`, `exports`, `__dirname` or `__filename` at the top level of an entry file, because entry files are ES modules. A file loaded through `require` does receive `module` and `exports`.

## What else is missing

There is no `process`, no `Buffer`, no `path`, `os`, `http`, `https`, `child_process`, `crypto` or `events` module, and no `node_modules` lookup. The standard JavaScript language itself is complete: `Promise`, `Map`, `Set`, typed arrays, `JSON`, `Math`, `Date`, regular expressions, classes, `async`/`await`.
