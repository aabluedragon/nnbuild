// VibeBuild32 embedded JavaScript API declarations.
declare type ActorEvent = { sprite: number; owner: number; tile: number };
declare type ChatEvent = { player: number; text: string };
declare type ConEvent = { sprite: number; player: number; dist: number };
declare type Player = { sprite: number; x: number; y: number; z: number };
declare type ConValue = number | undefined;
declare interface VBConsole { log(...args: unknown[]): void; error(...args: unknown[]): void; warn(...args: unknown[]): void; info(...args: unknown[]): void; }
// Every CON EVENT_* name accepted by vb.on(). The prefix is optional at runtime.
declare type ConEventShortName =
  | 'ACTIVATESWITCH' | 'AIMDOWN' | 'AIMUP' | 'ALTFIRE' | 'ALTWEAPON' | 'ANIMATESPRITES'
  | 'BOTGETSPRITESCORE' | 'CHANGEWEAPON' | 'CHEATGETBOOT' | 'CHEATGETFIRSTAID' | 'CHEATGETHEAT'
  | 'CHEATGETHOLODUKE' | 'CHEATGETJETPACK' | 'CHEATGETSCUBA' | 'CHEATGETSHIELD' | 'CHEATGETSTEROIDS'
  | 'CHECKTOUCHDAMAGE' | 'CROUCH' | 'DAMAGECEILING' | 'DAMAGEFLOOR' | 'DAMAGESPRITE'
  | 'DISPLAYACCESS' | 'DISPLAYBONUSSCREEN' | 'DISPLAYCROSSHAIR' | 'DISPLAYLEVELSTATS'
  | 'DISPLAYLOADINGSCREEN' | 'DISPLAYMENU' | 'DISPLAYMENUREST' | 'DISPLAYREST' | 'DISPLAYROOMS'
  | 'DISPLAYSBAR' | 'DISPLAYSTART' | 'DISPLAYTIP' | 'DISPLAYWEAPON' | 'DOFIRE' | 'DOQUAKE'
  | 'DRAWWEAPON' | 'EGS' | 'ENTERLEVEL' | 'FAKEDOMOVETHINGS' | 'FIRE' | 'FIREWEAPON'
  | 'FRAGGED' | 'GAME' | 'GETAUTOAIMANGLE' | 'GETLOADTILE' | 'GETMENUTILE' | 'GETSHOTRANGE'
  | 'HOLODUKEOFF' | 'HOLODUKEON' | 'HOLSTER' | 'INCURDAMAGE' | 'INIT' | 'INVENTORY'
  | 'INVENTORYLEFT' | 'INVENTORYRIGHT' | 'JUMP' | 'KILLIT' | 'LASTWEAPON' | 'LOADACTOR'
  | 'LOGO' | 'LOOKDOWN' | 'LOOKLEFT' | 'LOOKRIGHT' | 'LOOKUP' | 'MOVEBACKWARD' | 'MOVEFORWARD'
  | 'NEWGAME' | 'NEXTWEAPON' | 'POSTDAMAGESPRITE' | 'POSTWEAPONSHOOT' | 'PREGAME' | 'PRELEVEL'
  | 'PRESSEDFIRE' | 'PREVIOUSWEAPON' | 'PREWEAPONSHOOT' | 'PREWORLD' | 'PROCESSINPUT'
  | 'QUICKKICK' | 'RESETINVENTORY' | 'RESETPLAYER' | 'RESETWEAPONS' | 'RETURNTOCENTER'
  | 'SELECTWEAPON' | 'SETDEFAULTS' | 'SOARDOWN' | 'SOARUP' | 'SOUND' | 'SPAWN'
  | 'STRAFELEFT' | 'STRAFERIGHT' | 'SWIMDOWN' | 'SWIMUP' | 'TURNAROUND' | 'TURNLEFT'
  | 'TURNRIGHT' | 'USE' | 'USEJETPACK' | 'USEMEDKIT' | 'USENIGHTVISION' | 'USESTEROIDS'
  | 'WEAPKEY1' | 'WEAPKEY2' | 'WEAPKEY3' | 'WEAPKEY4' | 'WEAPKEY5' | 'WEAPKEY6'
  | 'WEAPKEY7' | 'WEAPKEY8' | 'WEAPKEY9' | 'WEAPKEY10' | 'WORLD';
declare type ConEventName = ConEventShortName | `EVENT_${ConEventShortName}`;
declare type VBLifecycleEventName = 'start' | 'shutdown' | 'frame' | 'playerDied';
declare type VBEventName = VBLifecycleEventName | 'actorSpawned' | 'chatMessage' | ConEventName;
declare type VBEventMap = {
  start: void; shutdown: void; frame: void; playerDied: void;
  actorSpawned: ActorEvent; chatMessage: ChatEvent;
} & { [K in ConEventName]: ConEvent };
declare interface VB {
  on<K extends VBEventName>(name: K, handler: (event: VBEventMap[K]) => void): void;
  /** Supports engine/plugin events that are not part of the built-in event list. */
  on(name: string, handler: (event?: unknown) => void): void;
  off<K extends VBEventName>(name?: K): void;
  off(name?: string): void;
  log(...args: unknown[]): void; drawText(id: number, x: number, y: number, text: string, pal?: number, shade?: number): void; clearText(id?: number): void; breakpoint(): void;
  spawn(tile: number, x?: number, y?: number, z?: number, owner?: number): number;
  A_Spawn(spriteNum: number, tileNum: number): number;
  A_InsertSprite(whatsect: number, s_x: number, s_y: number, s_z: number, s_pn: number, s_s: number, s_xr: number, s_yr: number, s_a: number, s_ve: number, s_zv: number, s_ow: number, s_ss: number): number;
  sendMessage(text: string): void; player(): Player;
  getGamevar(name: string, sprite?: number, player?: number): ConValue; setGamevar(name: string, value: number, sprite?: number, player?: number): void;
  getActor(i: number, member: string, parm2?: number): ConValue; setActor(i: number, member: string, value: number, parm2?: number): void;
  getPlayer(i: number, member: string, parm2?: number): ConValue; setPlayer(i: number, member: string, value: number, parm2?: number): void;
  getInput(i: number, member: string): ConValue; setInput(i: number, member: string, value: number): void;
  getSector(i: number, member: string, parm2?: number): ConValue; setSector(i: number, member: string, value: number, parm2?: number): void; getWall(i: number, member: string, parm2?: number): ConValue; setWall(i: number, member: string, value: number, parm2?: number): void;
  getProjectile(i: number, member: string, parm2?: number): ConValue; setProjectile(i: number, member: string, value: number, parm2?: number): void; getActiveProjectile(i: number, member: string, parm2?: number): ConValue; setActiveProjectile(i: number, member: string, value: number, parm2?: number): void; getTileData(i: number, member: string, parm2?: number): ConValue; setTileData(i: number, member: string, value: number, parm2?: number): void;
  getUserdef(member: string, parm2?: number): ConValue; setUserdef(member: string, value: number, parm2?: number): void; define(name: string): ConValue;
  sound(num: number, sprite?: number): void; shoot(sprite: number, tile: number): number; addAmmo(player: number, weapon: number, amount: number): void; addWeapon(player: number, weapon: number, switchTo?: number): void; run(command: string): void;
}
declare interface FS { readFile(path: string): string; writeFile(path: string, data: string): void; appendFile(path: string, data: string): void; exists(path: string): boolean; }
declare interface CVar { run(command: string): void; set(name: string, value: unknown): void; }
declare const console: VBConsole; declare const vb: VB; declare const cvar: CVar;
declare function require(name: 'vb'): VB; declare function require(name: 'fs'): FS; declare function require(name: 'cvar'): CVar; declare function require(name: string): unknown;
declare module 'vb' { export type { ActorEvent, ChatEvent, ConEvent, ConEventName, ConEventShortName, ConValue, Player, VBEventMap, VBEventName, VBLifecycleEventName, VBConsole }; const api: VB; export default api; export const on: VB['on']; export const off: VB['off']; export const log: VB['log']; export const drawText: VB['drawText']; export const clearText: VB['clearText']; export const breakpoint: VB['breakpoint']; export const spawn: VB['spawn']; export const A_Spawn: VB['A_Spawn']; export const A_InsertSprite: VB['A_InsertSprite']; export const sendMessage: VB['sendMessage']; export const player: VB['player']; export const getGamevar: VB['getGamevar']; export const setGamevar: VB['setGamevar']; export const run: VB['run']; export const define: VB['define']; export const sound: VB['sound']; export const shoot: VB['shoot']; export const addAmmo: VB['addAmmo']; export const addWeapon: VB['addWeapon']; export const getActor: VB['getActor']; export const setActor: VB['setActor']; export const getPlayer: VB['getPlayer']; export const setPlayer: VB['setPlayer']; export const getInput: VB['getInput']; export const setInput: VB['setInput']; export const getSector: VB['getSector']; export const setSector: VB['setSector']; export const getWall: VB['getWall']; export const setWall: VB['setWall']; export const getProjectile: VB['getProjectile']; export const setProjectile: VB['setProjectile']; export const getActiveProjectile: VB['getActiveProjectile']; export const setActiveProjectile: VB['setActiveProjectile']; export const getTileData: VB['getTileData']; export const setTileData: VB['setTileData']; export const getUserdef: VB['getUserdef']; export const setUserdef: VB['setUserdef']; }
declare module 'fs' { const api: FS; export default api; export const readFile: FS['readFile']; export const writeFile: FS['writeFile']; export const appendFile: FS['appendFile']; export const exists: FS['exists']; }
declare module 'cvar' { const api: CVar; export default api; export const run: CVar['run']; export const set: CVar['set']; }
