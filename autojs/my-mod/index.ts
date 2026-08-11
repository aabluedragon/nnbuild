/// vbd.params="-gm sw -g '/Users/alonamir/games/Shadow Warrior Complete/Sw.grp' -l 2"
// This sample deliberately uses only APIs shared by every VibeBuild32 game.
import { log, on } from 'vb';

let frames = 0;

on('start', () => {
  log('Shadow Warrior TypeScript mod loaded.');
});

on('frame', () => {
  // Put a VS Code breakpoint on the next line: it runs every game frame.
  const breakpointProbe = `Shadow Warrior frame ${++frames}`;
  if (frames % 60 === 0) log(breakpointProbe);
});
