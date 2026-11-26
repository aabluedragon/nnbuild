import fs from 'fs';
let frames = 0, spawns = 0, chats = 0;
nnb.on('start', () => nnb.log('demo started'));
nnb.on('frame', () => {
  frames++;
  nnb.drawText(1, 20, 18, `JS frame ${frames}  spawns:${spawns}  chats:${chats}`, 0, 0);
  if (frames === 40) { nnb.log('spawning a pig cop (tile 1680) at the player'); nnb.spawn(1680, 0, 0, 0); }  // 1680 = LIZTROOP
});
nnb.on('actorSpawned', (e) => { spawns++; if (spawns <= 5) nnb.log(`actorSpawned tile=${e.tile} sprite=${e.sprite}`); });
nnb.on('chatMessage', (e) => { chats++; nnb.log(`chatMessage: ${e.text}`); });
nnb.on('playerDied', () => nnb.log('EVENT: player died!'));
