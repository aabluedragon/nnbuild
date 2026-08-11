#!/usr/bin/env node
// One-shot, zero-dependency Debug Adapter Protocol bridge for VibeBuild32.
// It deliberately exits with its DAP client: no adapter or game is left behind.
const net = require('net');
const http = require('http');
const { spawn } = require('child_process');

const portArg = process.argv.indexOf('--port');
const listenPort = Number(portArg >= 0 ? process.argv[portArg + 1] : 4711);
let nextSeq = 1, activeSession = null, idleTimer = null, shuttingDown = false;
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const write = (socket, message) => {
  if (!socket || socket.destroyed || socket.writableEnded || !socket.writable) return false;
  const json = JSON.stringify({ seq: nextSeq++, ...message });
  return socket.write(`Content-Length: ${Buffer.byteLength(json)}\r\n\r\n${json}`);
};
const response = (s, r, body = {}, success = true, message) => write(s, { type: 'response', request_seq: r.seq, command: r.command, success, message, body });
const event = (s, name, body = {}) => write(s, { type: 'event', event: name, body });
function httpGet(port, path) {
  return new Promise((resolve, reject) => {
    const req = http.get({ host: '127.0.0.1', port, path, timeout: 700 }, res => {
      let body = ''; res.setEncoding('utf8'); res.on('data', c => body += c); res.on('end', () => resolve(body));
    });
    req.on('timeout', () => req.destroy(new Error('timeout'))); req.on('error', reject);
  });
}
function shutdownAdapter() {
  if (shuttingDown) return; shuttingDown = true;
  if (idleTimer) clearTimeout(idleTimer);
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 300).unref();
}
class Session {
  constructor(socket) { this.socket = socket; this.port = 8501; this.engineSeq = 100000; this.pending = new Map(); this.polling = false; this.child = null; this.ended = false; this.finishing = false; this.bridgeFailures = 0; }
  async bridge(message) { await httpGet(this.port, `/send?m=${encodeURIComponent(JSON.stringify(message))}`); }
  async waitForBridge() {
    for (let i = 0; i < 100; ++i) { try { await httpGet(this.port, '/poll?client=vscode'); this.startPolling(); return; } catch (_) { await delay(100); } }
    throw new Error(`Could not attach to VibeBuild32 on port ${this.port}.`);
  }
  startPolling() {
    if (this.polling) return; this.polling = true;
    const poll = async () => {
      if (!this.polling || this.ended) return;
      try { const text = await httpGet(this.port, '/poll?client=vscode'); this.bridgeFailures = 0; if (text) this.handleEngine(JSON.parse(text)); }
      catch (_) { if (++this.bridgeFailures >= 3) return this.finish(false); }
      setTimeout(poll, 40);
    }; poll();
  }
  handleEngine(message) {
    if (message.type === 'event') { const stopped = message.event || {}; event(this.socket, 'stopped', { reason: stopped.reason || 'breakpoint', threadId: 1, allThreadsStopped: true, description: `${stopped.filename || ''}:${stopped.line || 0}` }); return; }
    if (message.type !== 'response') return;
    const request = this.pending.get(message.request_seq); if (!request) return; this.pending.delete(message.request_seq);
    const body = message.body;
    if (request.command === 'stackTrace') { const frames = (Array.isArray(body) ? body : []).map(f => ({ id: f.id, name: f.name || '<anonymous>', source: f.filename ? { name: f.filename.split('/').pop(), path: f.filename } : undefined, line: f.line || 1, column: f.column || 1 })); response(this.socket, request, { stackFrames: frames, totalFrames: frames.length }); }
    else if (request.command === 'scopes') response(this.socket, request, { scopes: (Array.isArray(body) ? body : []).map(s => ({ name: s.name, variablesReference: s.reference || 0, expensive: s.name === 'Global' })) });
    else if (request.command === 'variables') response(this.socket, request, { variables: Object.entries(body || {}).map(([name, value]) => ({ name, value: typeof value === 'string' ? value : JSON.stringify(value), variablesReference: 0 })) });
    else if (request.command === 'evaluate') response(this.socket, request, { result: body && body.result !== undefined ? String(body.result) : 'undefined', variablesReference: 0 });
    else if (request.command === 'continue') { response(this.socket, request, { allThreadsContinued: true }); event(this.socket, 'continued', { threadId: 1, allThreadsContinued: true }); }
    else response(this.socket, request, {});
  }
  async stopOwnedChild() {
    if (!this.child || this.child.exitCode !== null || this.child.signalCode !== null) return;
    try { await this.bridge({ type: 'request', request: { request_seq: this.engineSeq++, command: 'continue', args: {} } }); } catch (_) {}
    this.child.kill('SIGTERM');
    for (let i = 0; i < 20 && this.child.exitCode === null && this.child.signalCode === null; ++i) await delay(100);
    if (this.child.exitCode === null && this.child.signalCode === null) { try { this.child.kill('SIGKILL'); } catch (_) {} }
  }
  async finish(stopGame) {
    if (this.finishing || this.ended) return; this.finishing = true; this.polling = false;
    if (stopGame) await this.stopOwnedChild();
    this.ended = true; this.pending.clear();
    event(this.socket, 'terminated');
    if (!this.socket.destroyed) this.socket.end();
    activeSession = null; setTimeout(shutdownAdapter, 20);
  }
  async engine(command, args, request) { const request_seq = this.engineSeq++; this.pending.set(request_seq, request); await this.bridge({ type: 'request', request: { request_seq, command, args: args || {} } }); }
  async handle(request) {
    if (this.ended) return;
    try { switch (request.command) {
      case 'initialize': response(this.socket, request, { supportsConfigurationDoneRequest: true, supportsEvaluateForHovers: true, supportsTerminateRequest: true }); break;
      case 'launch': {
        this.port = Number(request.arguments.debugPort || 8501); const exe = request.arguments.gameExecutable; if (!exe) throw new Error('launch requires gameExecutable');
        this.child = spawn(exe, ['-dbg'], { cwd: request.arguments.cwd || process.cwd(), stdio: 'inherit' });
        this.child.once('exit', () => this.finish(false)); this.child.once('error', () => this.finish(false));
        await this.waitForBridge(); response(this.socket, request, {}); event(this.socket, 'initialized'); break;
      }
      case 'attach': this.port = Number(request.arguments.debugPort || 8501); await this.waitForBridge(); response(this.socket, request, {}); event(this.socket, 'initialized'); break;
      case 'configurationDone': await this.bridge({ type: 'configurationDone' }); response(this.socket, request, {}); break;
      case 'setBreakpoints': { const source = request.arguments.source || {}, wanted = request.arguments.breakpoints || []; await this.bridge({ type: 'breakpoints', breakpoints: { path: source.path, breakpoints: wanted.map(bp => ({ line: bp.line })) } }); response(this.socket, request, { breakpoints: wanted.map(bp => ({ verified: true, line: bp.line })) }); break; }
      case 'threads': response(this.socket, request, { threads: [{ id: 1, name: 'VibeBuild32 JavaScript' }] }); break;
      case 'stackTrace': case 'scopes': case 'variables': case 'evaluate': case 'continue': case 'next': case 'stepIn': case 'stepOut': await this.engine(request.command, request.command === 'scopes' ? { frame: request.arguments.frameId || 0 } : request.command === 'variables' ? { variablesReference: request.arguments.variablesReference } : request.command === 'evaluate' ? { expression: request.arguments.expression || '', frame: request.arguments.frameId || 0 } : {}, request); break;
      case 'terminate': case 'disconnect': response(this.socket, request, {}); await this.finish(!!this.child); break;
      default: response(this.socket, request, {}, false, `Unsupported request: ${request.command}`);
    }} catch (error) { response(this.socket, request, {}, false, error.message); }
  }
}
const server = net.createServer(socket => {
  if (activeSession) return socket.destroy();
  if (idleTimer) clearTimeout(idleTimer); const session = activeSession = new Session(socket); let buffer = Buffer.alloc(0);
  socket.on('error', () => {});
  socket.on('data', chunk => { buffer = Buffer.concat([buffer, chunk]); for (;;) { const split = buffer.indexOf('\r\n\r\n'); if (split < 0) return; const match = /^Content-Length:\s*(\d+)/im.exec(buffer.subarray(0, split).toString()); if (!match) return socket.destroy(); const end = split + 4 + Number(match[1]); if (buffer.length < end) return; const request = JSON.parse(buffer.subarray(split + 4, end).toString()); buffer = buffer.subarray(end); if (request.type === 'request') session.handle(request); } });
  socket.on('close', () => { if (!session.ended) session.finish(!!session.child); });
});
server.on('error', error => { console.error(error.code === 'EADDRINUSE' ? `VibeBuild32 DAP port ${listenPort} is busy; wait for the previous debug session to finish.` : error.message); process.exit(1); });
server.listen(listenPort, '127.0.0.1', () => { console.log(`VibeBuild32 DAP adapter listening on ${listenPort}`); idleTimer = setTimeout(shutdownAdapter, 60000); });
