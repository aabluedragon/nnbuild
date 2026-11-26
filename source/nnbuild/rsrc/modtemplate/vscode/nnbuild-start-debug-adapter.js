#!/usr/bin/env node
// Start a single-session DAP adapter, then exit only after it is accepting.
// The adapter exits as soon as VS Code or the game exits, so this detached
// bootstrap never leaves an old debug session behind.
// connections.  A normal (non-background) VS Code preLaunchTask avoids the
// race where `debugServer` is contacted before a background task's matcher
// has observed its ready line.
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

const portArgument = process.argv.indexOf('--port');
const port = Number(portArgument >= 0 ? process.argv[portArgument + 1] : 4714);
const adapter = path.join(__dirname, 'nnbuild-debug-adapter.js');
const healthProbe = 'NNBuild-DAP-Health\n';

function adapterReady() {
  return new Promise(resolve => {
    const socket = net.connect({ host: '127.0.0.1', port });
    let doneOnce = false;
    const done = value => {
      if (doneOnce) return;
      doneOnce = true;
      socket.destroy();
      resolve(value);
    };
    socket.once('connect', () => socket.write(healthProbe));
    socket.on('data', chunk => done(chunk.toString('utf8') === 'ok\n'));
    socket.once('error', () => done(false));
    socket.setTimeout(250, () => done(false));
  });
}

async function main() {
  if (await adapterReady()) return;

  const child = spawn(process.execPath, [adapter, '--port', String(port)], {
    detached: true,
    stdio: 'ignore',
  });
  child.unref();

  for (let attempt = 0; attempt < 50; ++attempt) {
    await new Promise(resolve => setTimeout(resolve, 50));
    if (await adapterReady()) return;
  }
  throw new Error(`NNBuild DAP adapter did not start on port ${port}`);
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
