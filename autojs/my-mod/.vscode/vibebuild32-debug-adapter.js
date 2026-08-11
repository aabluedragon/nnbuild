#!/usr/bin/env node
// Zero-dependency Debug Adapter Protocol bridge for VibeBuild32.
// VS Code speaks DAP here; this translates it to the game's localhost-only
// source-debugger bridge at http://127.0.0.1:<debugPort>.
const net = require('net');
const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const listenPort = Number(process.argv[process.argv.indexOf('--port') + 1] || 4711);
let nextSeq = 1;
const write = (socket, message) => {
  if (socket.destroyed || socket.writableEnded || !socket.writable) return false;
  const json = JSON.stringify({ seq: nextSeq++, ...message });
  return socket.write(`Content-Length: ${Buffer.byteLength(json)}\r\n\r\n${json}`);
};
const response = (socket, request, body = {}, success = true, message) =>
  write(socket, { type: 'response', request_seq: request.seq, command: request.command, success, message, body });
const event = (socket, name, body = {}) => write(socket, { type: 'event', event: name, body });

// `/// vbd.params="-l 1 -v 1"` is an optional top-of-index debug directive.
// `vb.params` remains accepted for compatibility. Keep
// parsing local instead of invoking a shell: whitespace and quotes work, but
// no shell expansion or command substitution can occur.
function splitVBParams(text) {
  const args = []; let token = '', quote = '', escaped = false;
  for (const char of text) {
    if (escaped) { token += char; escaped = false; continue; }
    if (char === '\\') { escaped = true; continue; }
    if (quote) { if (char === quote) quote = ''; else token += char; continue; }
    if (char === '"' || char === "'") { quote = char; continue; }
    if (/\s/.test(char)) { if (token) { args.push(token); token = ''; } continue; }
    token += char;
  }
  if (escaped) token += '\\';
  if (quote) throw new Error('Unterminated quote in /// vbd.params directive');
  if (token) args.push(token);
  return args.map(arg => arg === '~' ? (process.env.HOME || arg) :
    (arg.startsWith('~/') && process.env.HOME ? path.join(process.env.HOME, arg.slice(2)) : arg));
}

function unwrapVBParams(text) {
  text = text.trim();
  // The directive convention quotes the whole list. Strip only a matching
  // outer pair so splitVBParams can still honour quotes inside an argument.
  if (text.length >= 2 && (text[0] === '"' || text[0] === "'") && text.at(-1) === text[0])
    return text.slice(1, -1);
  return text;
}

function readVBParams(projectRoot) {
  for (const filename of ['index.ts', 'index.js']) {
    let source;
    try { source = fs.readFileSync(path.join(projectRoot, filename), 'utf8'); }
    catch (_) { continue; }
    for (const line of source.split(/\r?\n/, 32)) {
      const match = /^\s*\/\/\/\s*vbd?\.params\s*=\s*(.*?)\s*$/.exec(line);
      if (match) return splitVBParams(unwrapVBParams(match[1]));
      // The directive is intentionally a header option, not a runtime comment.
      if (line.trim() && !/^\s*\/\//.test(line)) break;
    }
  }
  return [];
}

function httpGet(port, path) {
  return new Promise((resolve, reject) => {
    const req = http.get({ host: '127.0.0.1', port, path, timeout: 700 }, res => {
      let body = ''; res.setEncoding('utf8'); res.on('data', chunk => body += chunk); res.on('end', () => resolve(body));
    });
    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', reject);
  });
}

class Session {
  constructor(socket) { this.socket = socket; this.port = 4712; this.engineSeq = 100000; this.pending = new Map(); this.polling = false; this.child = null; this.bridgeFailures = 0; this.ended = false; }
  endSession() {
    if (this.ended) return;
    this.ended = true; this.polling = false;
    event(this.socket, 'terminated'); setTimeout(() => this.socket.end(), 0);
  }
  async bridge(message) { await httpGet(this.port, `/send?m=${encodeURIComponent(JSON.stringify(message))}`); }
  async terminateGame() {
    // A source breakpoint blocks the game thread until Continue. Release it
    // before stopping the process so Stop never leaves the game paused.
    try { await this.bridge({ type: 'request', request: { request_seq: this.engineSeq++, command: 'continue', args: {} } }); } catch (_) {}
    try { await httpGet(this.port, '/terminate'); } catch (_) { /* It may already be gone. */ }
    // A launch session owns the game's PID, so terminate it directly as well.
    // This is deliberately independent of the bridge response: the debug
    // transport may already be gone while the child is still running.
    if (this.child && !this.child.killed) {
      try { this.child.kill('SIGTERM'); } catch (_) { /* It already exited. */ }
    }
  }
  async engine(command, args, request) {
    const request_seq = this.engineSeq++;
    this.pending.set(request_seq, request);
    await this.bridge({ type: 'request', request: { request_seq, command, args: args || {} } });
  }
  async waitForBridge() {
    for (let tries = 0; tries < 100; ++tries) {
      try { await httpGet(this.port, '/poll?client=vscode'); this.startPolling(); return; }
      catch (_) { await new Promise(resolve => setTimeout(resolve, 100)); }
    }
    throw new Error(`Could not attach to VibeBuild32 on port ${this.port}. Start it with -dt.`);
  }
  startPolling() {
    if (this.polling) return; this.polling = true;
    const poll = async () => {
      if (!this.polling) return;
      try { const text = await httpGet(this.port, '/poll?client=vscode'); this.bridgeFailures = 0; if (text) this.handleEngine(JSON.parse(text)); }
      catch (_) { if (++this.bridgeFailures >= 3) { this.endSession(); return; } }
      setTimeout(poll, 40);
    };
    poll();
  }
  handleEngine(message) {
    if (message.type === 'event') {
      const stopped = message.event || {};
      event(this.socket, 'stopped', { reason: stopped.reason || 'breakpoint', threadId: 1, allThreadsStopped: true, description: `${stopped.filename || ''}:${stopped.line || 0}` });
      return;
    }
    if (message.type !== 'response') return;
    const request = this.pending.get(message.request_seq);
    if (!request) return;
    this.pending.delete(message.request_seq);
    const body = message.body;
    if (request.command === 'stackTrace') {
      const stackFrames = (Array.isArray(body) ? body : []).map(frame => ({ id: frame.id, name: frame.name || '<anonymous>', source: frame.filename ? { name: frame.filename.split('/').pop(), path: frame.filename } : undefined, line: frame.line || 1, column: frame.column || 1 }));
      response(this.socket, request, { stackFrames, totalFrames: stackFrames.length });
    } else if (request.command === 'scopes') {
      response(this.socket, request, { scopes: (Array.isArray(body) ? body : []).map(scope => ({ name: scope.name, variablesReference: scope.reference || 0, expensive: scope.name === 'Global' })) });
    } else if (request.command === 'variables') {
      const variables = Object.entries(body || {}).map(([name, value]) => ({ name, value: typeof value === 'string' ? value : JSON.stringify(value), variablesReference: 0 }));
      response(this.socket, request, { variables });
    } else if (request.command === 'evaluate') response(this.socket, request, { result: body && body.result !== undefined ? String(body.result) : 'undefined', variablesReference: 0 });
    else if (request.command === 'continue') { response(this.socket, request, { allThreadsContinued: true }); event(this.socket, 'continued', { threadId: 1, allThreadsContinued: true }); }
    else response(this.socket, request, {});
  }
  async handle(request) {
    if (this.ended) return;
    try {
      switch (request.command) {
        case 'initialize':
          response(this.socket, request, { supportsConfigurationDoneRequest: true, supportsEvaluateForHovers: true, supportsStepBack: false, supportsRestartRequest: false, supportsTerminateRequest: true }); break;
        case 'launch': {
          this.port = Number(request.arguments.debugPort || 4712);
          const executable = request.arguments.gameExecutable;
          const data = request.arguments.gameData;
          if (!executable || !data) throw new Error('launch requires gameExecutable and gameData');
          const projectRoot = request.arguments.modRoot || path.resolve(__dirname, '..');
          const params = readVBParams(projectRoot);
          const gameArgs = ['-gm', request.arguments.game || 'duke', '-g', data, ...params];
          if (params.length) console.log(`VibeBuild32 parameters from ${path.basename(projectRoot)}/index: ${params.join(' ')}`);
          this.child = spawn(executable, gameArgs, { cwd: request.arguments.cwd || process.cwd(), stdio: 'inherit' });
          // The game can be closed from its own window, not only through DAP
          // Stop. Notify VS Code immediately in either case; waiting for the
          // localhost debugger bridge to time out leaves the UI looking live.
          this.child.once('exit', () => this.endSession());
          this.child.once('error', () => this.endSession());
          await this.waitForBridge(); response(this.socket, request, {}); event(this.socket, 'initialized'); break;
        }
        case 'attach': this.port = Number(request.arguments.debugPort || 4712); await this.waitForBridge(); response(this.socket, request, {}); event(this.socket, 'initialized'); break;
        case 'configurationDone':
          // Tell the runtime it may now fire its one-shot `start` callbacks:
          // source breakpoints have been installed at this point.
          await this.bridge({ type: 'configurationDone' });
          response(this.socket, request, {}); break;
        case 'setBreakpoints': {
          const source = request.arguments.source || {}, wanted = request.arguments.breakpoints || [];
          await this.bridge({ type: 'breakpoints', breakpoints: { path: source.path, breakpoints: wanted.map(bp => ({ line: bp.line })) } });
          response(this.socket, request, { breakpoints: wanted.map(bp => ({ verified: true, line: bp.line })) }); break;
        }
        case 'threads': response(this.socket, request, { threads: [{ id: 1, name: 'VibeBuild32 JavaScript' }] }); break;
        case 'stackTrace': await this.engine('stackTrace', {}, request); break;
        case 'scopes': await this.engine('scopes', { frame: request.arguments.frameId || 0 }, request); break;
        case 'variables': await this.engine('variables', { variablesReference: request.arguments.variablesReference }, request); break;
        case 'evaluate': await this.engine('evaluate', { expression: request.arguments.expression || '', frame: request.arguments.frameId || 0 }, request); break;
        case 'continue': await this.engine('continue', {}, request); break;
        case 'next': await this.engine('next', {}, request); break;
        case 'stepIn': await this.engine('stepIn', {}, request); break;
        case 'stepOut': await this.engine('stepOut', {}, request); break;
        case 'terminate':
          this.polling = false; await this.terminateGame(); response(this.socket, request, {});
          this.endSession(); break;
        case 'disconnect':
          this.polling = false; await this.terminateGame(); response(this.socket, request, {});
          // VS Code's Stop button normally uses `disconnect`, not `terminate`.
          // Sending a response alone leaves its debug UI active; DAP requires
          // the terminated event to end the session in the client.
          this.endSession(); break;
        default: response(this.socket, request, {}, false, `Unsupported request: ${request.command}`);
      }
    } catch (error) { response(this.socket, request, {}, false, error.message); }
  }
}

const server = net.createServer(socket => {
  let buffer = Buffer.alloc(0); const session = new Session(socket);
  socket.on('error', () => {}); // A client can close while an async engine reply is in flight.
  socket.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
      const split = buffer.indexOf('\r\n\r\n'); if (split < 0) return;
      const match = /^Content-Length:\s*(\d+)/im.exec(buffer.subarray(0, split).toString()); if (!match) { socket.destroy(); return; }
      const end = split + 4 + Number(match[1]); if (buffer.length < end) return;
      const request = JSON.parse(buffer.subarray(split + 4, end).toString()); buffer = buffer.subarray(end);
      if (request.type === 'request') session.handle(request);
    }
  });
  socket.on('close', () => session.polling = false);
});

// VS Code may run the pre-launch task again while the adapter from an earlier
// debug session is still alive. The existing adapter is exactly what the
// debugServer setting needs, so treat that as success instead of a task error.
server.on('error', error => {
  if (error.code === 'EADDRINUSE') {
    console.log(`VibeBuild32 DAP adapter already listening on ${listenPort}`);
    process.exit(0);
  }
  throw error;
});
server.listen(listenPort, '127.0.0.1', () => console.log(`VibeBuild32 DAP adapter listening on ${listenPort}`));
