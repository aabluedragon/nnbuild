// VibeBuild32 embedded JavaScript API declarations.
declare type ActorEvent = { sprite: number; owner: number; tile: number };
declare type VBVector3 = { x: number; y: number; z: number };
declare type VBTransform = { position: VBVector3; rotation: VBVector3; scale: VBVector3 };
declare class VBNode {
  readonly id: number; name: string; tag: string; readonly destroyed: boolean;
  readonly activeSelf: boolean; readonly activeInHierarchy: boolean; transform: VBTransform;
  SetActive(active: boolean): boolean;
  AddComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T;
  GetComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T | undefined;
  GetComponents<T extends VBComponent>(type?: VBComponentConstructor<T>): T[];
  Destroy(): boolean;
}
declare abstract class VBComponent {
  /** Build sprite this component is attached to; -1 after destruction/detach. */
  sprite: number; node: VBNode | null; readonly gameObject: VBNode | null; readonly transform: VBTransform | undefined;
  enabled: boolean; readonly isActiveAndEnabled: boolean;
  GetComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T | undefined;
  GetComponents<T extends VBComponent>(type?: VBComponentConstructor<T>): T[];
  AddComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T | undefined;
  Destroy(): boolean;
  awake(): void; start(): void; onSpawn(): void; fixedUpdate(dt: number): void; update(dt: number): void; lateUpdate(dt: number): void; onDestroy(): void; onEnable(): void; onDisable(): void;
}
declare type VBComponentConstructor<T extends VBComponent = VBComponent> = new () => T;
declare class VBPrefab { readonly tile: number; readonly components: readonly VBComponentConstructor[]; }
declare type ChatEvent = { player: number; text: string };
declare type MenuEvent = { menu: number; previousMenu?: number };
declare type ConEvent = { sprite: number; player: number; dist: number };
declare type Player = { sprite: number; x: number; y: number; z: number };
declare type SessionPlayer = { index: number; name: string; sprite: number; isLocal: boolean; isHost: boolean };
declare type GameSession = {
  /** True after a map has successfully entered play. */ started: boolean;
  /** One-based, matching Duke's player-visible skill/episode/level numbers. */ skill: number; episode: number; level: number;
  episodeName?: string; levelName?: string; userMap?: string;
  localPlayer: number; hostPlayer?: number; hostName?: string; serverAddress?: string; players: SessionPlayer[];
};
declare type ConValue = number | undefined;
declare interface VBConsole { log(...args: unknown[]): void; error(...args: unknown[]): void; warn(...args: unknown[]): void; info(...args: unknown[]): void; }
declare interface VBHeaders { get(name: string): string | null; }
declare interface VBFetchResponse { readonly status: number; readonly ok: boolean; readonly url: string; readonly headers: VBHeaders; text(): Promise<string>; json<T = unknown>(): Promise<T>; arrayBuffer(): Promise<ArrayBuffer>; }
declare interface VBFetchOptions { method?: string; body?: string | Uint8Array; headers?: Record<string, string>; }
declare function fetch(url: string, options?: VBFetchOptions): Promise<VBFetchResponse>;
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
declare type VBLifecycleEventName = 'start' | 'shutdown' | 'frame' | 'playerDied' | 'gameStarted' | 'levelChanged' | 'levelFinished' | 'leftGame' | 'beforeQuit';
declare type VBEventName = VBLifecycleEventName | 'menuOpened' | 'menuClosed' | 'menuChanged' | 'escapeMenuOpened' | 'actorSpawned' | 'actorDestroyed' | 'chatMessage' | ConEventName;
declare type VBEventMap = {
  start: void; shutdown: void; frame: void; playerDied: void; gameStarted: void; levelChanged: void; levelFinished: void; leftGame: void; beforeQuit: void;
  menuOpened: MenuEvent; menuClosed: MenuEvent; menuChanged: MenuEvent; escapeMenuOpened: MenuEvent;
  actorSpawned: ActorEvent; actorDestroyed: ActorEvent; chatMessage: ChatEvent;
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
  VBComponent: typeof VBComponent; VBNode: typeof VBNode; VBPrefab: typeof VBPrefab;
  attach<T extends VBComponent>(entity: number | VBNode, component: T | VBComponentConstructor<T>): T;
  detach(entity: number | VBNode, component: VBComponent): boolean;
  getComponent<T extends VBComponent>(entity: number | VBNode, type: VBComponentConstructor<T>): T | undefined;
  getComponents<T extends VBComponent>(entity: number | VBNode, type?: VBComponentConstructor<T>): T[];
  setComponentEnabled(component: VBComponent, enabled: boolean): void;
  createNode(...components: VBComponentConstructor[]): VBNode; destroyNode(node: VBNode): boolean;
  createPrefab(tile: number, ...components: VBComponentConstructor[]): VBPrefab;
  spawnPrefab(prefab: VBPrefab, x?: number, y?: number, z?: number, owner?: number): number;
  sendMessage(text: string): void; player(): Player; session(): GameSession;
  /** Replace every matching built-in menu label before it is rendered. */ setMenuText(original: string, replacement: string): void;
  /** Use an ART tile as the normal menu/loading background; pass null to restore the game default. */ setMenuBackground(tile: number | null): void;
  currentMenu(): number; openMenu(menuId: number): void;
  /** Redirect an item selected in a built-in menu. Use -1 as itemIndex for every route from that menu; null removes an override. */ setMenuRoute(menuId: number, itemIndex: number, destination: number | null): void;
  menuSelection(): number; setMenuSelection(index: number): void;
  getGamevar(name: string, sprite?: number, player?: number): ConValue; setGamevar(name: string, value: number, sprite?: number, player?: number): void;
  getActor(i: number, member: string, parm2?: number): ConValue; setActor(i: number, member: string, value: number, parm2?: number): void;
  getPlayer(i: number, member: string, parm2?: number): ConValue; setPlayer(i: number, member: string, value: number, parm2?: number): void;
  getInput(i: number, member: string): ConValue; setInput(i: number, member: string, value: number): void;
  getSector(i: number, member: string, parm2?: number): ConValue; setSector(i: number, member: string, value: number, parm2?: number): void; getWall(i: number, member: string, parm2?: number): ConValue; setWall(i: number, member: string, value: number, parm2?: number): void;
  getProjectile(i: number, member: string, parm2?: number): ConValue; setProjectile(i: number, member: string, value: number, parm2?: number): void; getActiveProjectile(i: number, member: string, parm2?: number): ConValue; setActiveProjectile(i: number, member: string, value: number, parm2?: number): void; getTileData(i: number, member: string, parm2?: number): ConValue; setTileData(i: number, member: string, value: number, parm2?: number): void;
  getUserdef(member: string, parm2?: number): ConValue; setUserdef(member: string, value: number, parm2?: number): void; define(name: string): ConValue;
  sound(num: number, sprite?: number): void; shoot(sprite: number, tile: number): number; addAmmo(player: number, weapon: number, amount: number): void; addWeapon(player: number, weapon: number, switchTo?: number): void; run(command: string): void;
}
declare interface FSPromises { readFile(path: string, options?: unknown): Promise<string | Uint8Array>; writeFile(path: string, data: string, options?: unknown): Promise<void>; appendFile(path: string, data: string, options?: unknown): Promise<void>; exists(path: string): Promise<boolean>; unlink(path: string): Promise<void>; rename(oldPath: string, newPath: string): Promise<void>; }
declare interface FS { readFile(path: string, options?: unknown): string | Uint8Array; readFile(path: string, options: unknown, callback: (error: Error | null, data?: string | Uint8Array) => void): void; readFile(path: string, callback: (error: Error | null, data?: string | Uint8Array) => void): void; writeFile(path: string, data: string): void; writeFile(path: string, data: string, options: unknown, callback: (error: Error | null) => void): void; writeFile(path: string, data: string, callback: (error: Error | null) => void): void; appendFile(path: string, data: string): void; appendFile(path: string, data: string, options: unknown, callback: (error: Error | null) => void): void; appendFile(path: string, data: string, callback: (error: Error | null) => void): void; exists(path: string): boolean; exists(path: string, callback: (exists: boolean) => void): void; readFileSync(path: string, options?: unknown): string | Uint8Array; writeFileSync(path: string, data: string, options?: unknown): void; appendFileSync(path: string, data: string, options?: unknown): void; existsSync(path: string): boolean; readonly promises: FSPromises; }
declare interface CVar { run(command: string): void; set(name: string, value: unknown): void; }
declare const console: VBConsole; declare const vb: VB; declare const cvar: CVar;
declare interface DgramRemoteInfo { address: string; family: 'IPv4' | 'IPv6'; port: number; size: number; }
declare interface DgramSocket { on(event: 'message', listener: (message: Uint8Array, rinfo: DgramRemoteInfo) => void): this; on(event: 'error', listener: (error: Error) => void): this; on(event: 'listening' | 'close', listener: () => void): this; bind(port: number, callback?: () => void): this; bind(port: number, address: string, callback?: () => void): this; send(message: string | Uint8Array, port: number, callback?: (error: Error | null) => void): void; send(message: string | Uint8Array, port: number, address: string, callback?: (error: Error | null) => void): void; close(callback?: () => void): void; address(): { address: string; family: 'IPv4' | 'IPv6'; port: number }; }
declare interface Dgram { createSocket(type: 'udp4' | 'udp6', callback?: (message: Uint8Array, rinfo: DgramRemoteInfo) => void): DgramSocket; }
declare interface NetSocket { on(event: 'connect' | 'end', listener: () => void): this; on(event: 'data', listener: (data: Uint8Array) => void): this; on(event: 'error', listener: (error: Error) => void): this; on(event: 'close', listener: (hadError: boolean) => void): this; write(data: string | Uint8Array, callback?: () => void): boolean; end(data?: string | Uint8Array, callback?: () => void): this; destroy(): this; setNoDelay(noDelay?: boolean): this; }
declare interface Net { createConnection(port: number, callback?: () => void): NetSocket; createConnection(port: number, host: string, callback?: () => void): NetSocket; connect: Net['createConnection']; }
declare function require(name: 'vb'): VB; declare function require(name: 'fs'): FS; declare function require(name: 'dgram'): Dgram; declare function require(name: 'net'): Net; declare function require(name: 'cvar'): CVar; declare function require(name: string): unknown;
declare module 'vb' { export { VBComponent, VBNode, VBPrefab }; export type { ActorEvent, ChatEvent, MenuEvent, ConEvent, ConEventName, ConEventShortName, ConValue, Player, SessionPlayer, GameSession, VBComponentConstructor, VBEventMap, VBEventName, VBLifecycleEventName, VBConsole, VBTransform, VBVector3 }; const api: VB; export default api; export const on: VB['on']; export const off: VB['off']; export const log: VB['log']; export const drawText: VB['drawText']; export const clearText: VB['clearText']; export const breakpoint: VB['breakpoint']; export const spawn: VB['spawn']; export const A_Spawn: VB['A_Spawn']; export const A_InsertSprite: VB['A_InsertSprite']; export const attach: VB['attach']; export const detach: VB['detach']; export const getComponent: VB['getComponent']; export const getComponents: VB['getComponents']; export const setComponentEnabled: VB['setComponentEnabled']; export const createNode: VB['createNode']; export const destroyNode: VB['destroyNode']; export const createPrefab: VB['createPrefab']; export const spawnPrefab: VB['spawnPrefab']; export const sendMessage: VB['sendMessage']; export const player: VB['player']; export const session: VB['session']; export const setMenuText: VB['setMenuText']; export const setMenuBackground: VB['setMenuBackground']; export const getGamevar: VB['getGamevar']; export const setGamevar: VB['setGamevar']; export const run: VB['run']; export const define: VB['define']; export const sound: VB['sound']; export const shoot: VB['shoot']; export const addAmmo: VB['addAmmo']; export const addWeapon: VB['addWeapon']; export const getActor: VB['getActor']; export const setActor: VB['setActor']; export const getPlayer: VB['getPlayer']; export const setPlayer: VB['setPlayer']; export const getInput: VB['getInput']; export const setInput: VB['setInput']; export const getSector: VB['getSector']; export const setSector: VB['setSector']; export const getWall: VB['getWall']; export const setWall: VB['setWall']; export const getProjectile: VB['getProjectile']; export const setProjectile: VB['setProjectile']; export const getActiveProjectile: VB['getActiveProjectile']; export const setActiveProjectile: VB['setActiveProjectile']; export const getTileData: VB['getTileData']; export const setTileData: VB['setTileData']; export const getUserdef: VB['getUserdef']; export const setUserdef: VB['setUserdef']; }
declare module 'vb' { export type { SessionPlayer, GameSession }; export const session: VB['session']; export const setMenuText: VB['setMenuText']; export const setMenuBackground: VB['setMenuBackground']; export const currentMenu: VB['currentMenu']; export const openMenu: VB['openMenu']; export const setMenuRoute: VB['setMenuRoute']; export const menuSelection: VB['menuSelection']; export const setMenuSelection: VB['setMenuSelection']; }
declare module 'fs' { export type { FSPromises }; const api: FS; export default api; export const readFile: FS['readFile']; export const writeFile: FS['writeFile']; export const appendFile: FS['appendFile']; export const exists: FS['exists']; export const readFileSync: FS['readFileSync']; export const writeFileSync: FS['writeFileSync']; export const appendFileSync: FS['appendFileSync']; export const existsSync: FS['existsSync']; export const promises: FS['promises']; }
declare module 'node:fs' { export * from 'fs'; import api from 'fs'; export default api; }
declare module 'fs/promises' { import api from 'fs'; const promises: FSPromises; export default promises; export const readFile: FSPromises['readFile']; export const writeFile: FSPromises['writeFile']; export const appendFile: FSPromises['appendFile']; export const exists: FSPromises['exists']; export const unlink: FSPromises['unlink']; export const rename: FSPromises['rename']; }
declare module 'node:fs/promises' { export * from 'fs/promises'; import api from 'fs/promises'; export default api; }
declare module 'dgram' { export type { DgramRemoteInfo, DgramSocket }; const api: Dgram; export default api; export const createSocket: Dgram['createSocket']; }
declare module 'node:dgram' { export * from 'dgram'; import api from 'dgram'; export default api; }
declare module 'net' { export type { NetSocket }; const api: Net; export default api; export const createConnection: Net['createConnection']; export const connect: Net['connect']; }
declare module 'node:net' { export * from 'net'; import api from 'net'; export default api; }
declare module 'fs' { export const unlink: (path: string, callback?: (error: Error | null) => void) => void; export const rename: (oldPath: string, newPath: string, callback?: (error: Error | null) => void) => void; export const unlinkSync: (path: string) => void; export const renameSync: (oldPath: string, newPath: string) => void; }
declare interface FS { unlink(path: string, callback?: (error: Error | null) => void): void; rename(oldPath: string, newPath: string, callback?: (error: Error | null) => void): void; unlinkSync(path: string): void; renameSync(oldPath: string, newPath: string): void; }
declare module 'cvar' { const api: CVar; export default api; export const run: CVar['run']; export const set: CVar['set']; }
