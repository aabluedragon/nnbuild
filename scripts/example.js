import fs from 'fs';
let frames = 0, spawns = 0, chats = 0;
vb.on('start', () => vb.log('demo started'));
vb.on('frame', () => {
  frames++;
  vb.drawText(1, 20, 18, `JS frame ${frames}  spawns:${spawns}  chats:${chats}`, 0, 0);
  if (frames === 40) { vb.log('spawning a pig cop (tile 1680) at the player'); vb.spawn(1680, 0, 0, 0); }  // 1680 = LIZTROOP
});
vb.on('actorSpawned', (e) => { spawns++; if (spawns <= 5) vb.log(`actorSpawned tile=${e.tile} sprite=${e.sprite}`); });
vb.on('chatMessage', (e) => { chats++; vb.log(`chatMessage: ${e.text}`); });
vb.on('playerDied', () => vb.log('EVENT: player died!'));
