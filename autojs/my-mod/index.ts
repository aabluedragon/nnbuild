/// vbd.params="-gm duke -g ~/games/duke3da/duke3d.grp -v1 -l1 -sc"
// Duke demo: after 3 seconds, spawn one turret in front of the player.
// The turret is a prefab with a VBComponent that aims at the player and fires every 3 seconds.
import {
  VBComponent,
  createNode,
  createPrefab,
  define,
  getActor,
  log,
  on,
  player,
  session,
  setActor,
  shoot,
  spawnPrefab,
} from 'vb';

const SPAWN_DELAY_SECONDS = 3;
const FIRE_INTERVAL_SECONDS = 3;
const TURRET_DISTANCE = 2048;
const TURRET_TILE_NAME = 'ROTATEGUN';
const PROJECTILE_TILE_NAME = 'RPG';

let demoStarted = false;
let projectileTile: number | undefined;
let activeActorStat = 1;
let turretPrefab: ReturnType<typeof createPrefab> | undefined;

class DelayedTurretSpawner extends VBComponent {
  private elapsed = 0;
  private done = false;

  override update(dt: number) {
    if (this.done) return;

    this.elapsed += dt;
    if (this.elapsed < SPAWN_DELAY_SECONDS) return;

    if (!turretPrefab) return;

    const position = pointInFrontOfPlayer(TURRET_DISTANCE);
    if (!position) return;

    this.done = true;

    const turret = spawnPrefab(turretPrefab, position.x, position.y, position.z, position.owner);

    if (turret < 0) {
      log('Could not spawn turret.');
    } else {
      log('Spawned turret:', turret);
    }

    // This component lives on an empty node. Remove that one-shot node after it has done its job.
    if (this.gameObject) this.gameObject.Destroy();
    else this.Destroy();
  }
}

class PlayerSeekingTurret extends VBComponent {
  private fireTimer = 0;

  override onSpawn() {
    makeTurretSolidAndKillable(this.sprite);
    this.aimAtPlayer();
    log('Turret component attached to sprite:', this.sprite);
  }

  override update(dt: number) {
    if (this.sprite < 0) return;

    this.aimAtPlayer();
    this.fireTimer += dt;

    if (this.fireTimer < FIRE_INTERVAL_SECONDS) return;
    this.fireTimer = 0;

    if (projectileTile === undefined) return;

    const projectile = shoot(this.sprite, projectileTile);
    if (projectile >= 0) this.aimProjectileAtPlayer(projectile);
    log('Turret fired at player. Projectile sprite:', projectile);
  }

  private aimAtPlayer() {
    const target = player();
    if (target.sprite < 0) return;

    const x = actorNumber(this.sprite, 'x', target.x);
    const y = actorNumber(this.sprite, 'y', target.y);
    setActor(this.sprite, 'ang', angleTo(x, y, target.x, target.y));
  }

  private aimProjectileAtPlayer(projectile: number) {
    const target = player();
    if (target.sprite < 0) return;

    const x = actorNumber(projectile, 'x', target.x);
    const y = actorNumber(projectile, 'y', target.y);
    const z = actorNumber(projectile, 'z', target.z);
    const distance = Math.max(1, distance2d(x, y, target.x, target.y));

    setActor(projectile, 'ang', angleTo(x, y, target.x, target.y));
    setActor(projectile, 'zvel', Math.round(((target.z - z) * 256) / distance));
  }
}

on('start', () => {
  log('VBComponent turret demo loaded.');
  if (session().started) startDemo();
});

on('gameStarted', () => {
  startDemo();
});

function startDemo() {
  if (demoStarted) return;

  if (!prepareTurretPrefab()) {
    log(`Turret demo skipped: this demo needs Duke symbols ${TURRET_TILE_NAME} and ${PROJECTILE_TILE_NAME}.`);
    return;
  }

  demoStarted = true;

  const spawner = createNode(DelayedTurretSpawner);
  spawner.name = 'Delayed turret spawner';
  log(`A turret will spawn in front of the player in ${SPAWN_DELAY_SECONDS} seconds.`);
}

function pointInFrontOfPlayer(distance: number) {
  const target = player();
  if (target.sprite < 0) return undefined;

  const angle = actorNumber(target.sprite, 'ang', 0);
  const radians = (angle * Math.PI) / 1024;

  return {
    x: Math.round(target.x + Math.cos(radians) * distance),
    y: Math.round(target.y + Math.sin(radians) * distance),
    z: target.z - (16 << 8),
    owner: target.sprite,
  };
}

function makeTurretSolidAndKillable(sprite: number) {
  if (sprite < 0) return;
  setActor(sprite, 'statnum', activeActorStat);
  setActor(sprite, 'extra', 90);
  setActor(sprite, 'htowner', sprite);
  setActor(sprite, 'httimetosleep', 0);
  setActor(sprite, 'clipdist', 64);
  setActor(sprite, 'xrepeat', 48);
  setActor(sprite, 'yrepeat', 48);
  setActor(sprite, 'cstat', actorNumber(sprite, 'cstat', 0) | 257);
}

function angleTo(fromX: number, fromY: number, toX: number, toY: number) {
  return Math.round((Math.atan2(toY - fromY, toX - fromX) * 1024) / Math.PI) & 2047;
}

function distance2d(fromX: number, fromY: number, toX: number, toY: number) {
  return Math.hypot(toX - fromX, toY - fromY);
}

function actorNumber(sprite: number, member: string, fallback: number) {
  if (sprite < 0) return fallback;
  const value = getActor(sprite, member);
  return typeof value === 'number' ? value : fallback;
}

function prepareTurretPrefab() {
  if (turretPrefab) return true;

  const turretTile = getRequiredDefine(TURRET_TILE_NAME);
  const rocketTile = getRequiredDefine(PROJECTILE_TILE_NAME);
  if (turretTile === undefined || rocketTile === undefined) return false;

  activeActorStat = getRequiredDefine('STAT_ACTOR') ?? activeActorStat;
  projectileTile = rocketTile;
  turretPrefab = createPrefab(turretTile, PlayerSeekingTurret);
  return true;
}

function getRequiredDefine(name: string) {
  const value = define(name);
  if (typeof value !== 'number') return undefined;
  return value;
}
