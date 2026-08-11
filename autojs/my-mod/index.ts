/// vbd.params="-l1 --gm duke -g ~/games/duke3da/duke3d.grp -l1 -v1"

// VibeBuild32 auto-loads only index.ts (or index.js) from this project.
import { on, log, drawText, define, getActor, spawn } from 'vb';
import fs from 'fs';
import cvar from 'cvar';
const vbLegacy = require('vb'); // CommonJS require() is supported too.

let frames = 0;
const liztroop = define('LIZTROOP');


on('start', () => {
  log('TypeScript mod loaded. LIZTROOP is', liztroop, 'player:', vbLegacy.player().sprite);
  // Built-in modules are typed too; this is safe even when the file does not exist.
  if (fs.exists('autojs-notes.txt')) log(fs.readFile('autojs-notes.txt'));
  cvar.set('r_maxfps', 60);
});

on('STRAFELEFT', () => {
  const name = "heyyy";
  log('Player is strafing left' + name);
});

on('actorSpawned', event => {
  if (event.tile === liztroop) log('A trooper spawned:', event.sprite);
});

on('frame', () => {
  if (++frames % 60 !== 0) return;
  if (liztroop === undefined) return;

  const player = vbLegacy.player();
  const angle = Number(getActor(player.sprite, 'ang'));
  const distance = 1024;
  const radians = angle * Math.PI / 1024;
  spawn(
    liztroop,
    Math.round(player.x + Math.cos(radians) * distance),
    Math.round(player.y + Math.sin(radians) * distance),
    player.z,
    player.sprite,
  );
  drawText(9001, 8, 8, `TS mod: ${frames} frames`);
});
