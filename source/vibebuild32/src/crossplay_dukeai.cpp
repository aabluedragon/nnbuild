//-------------------------------------------------------------------------
// VibeBuild32 crossplay: Duke enemy AI hosted by a foreign game runtime.
//
// When a non-Duke runtime (currently Shadow Warrior) plays a Duke-family
// level, the enemies must behave exactly like Duke's.  Approximating Duke's
// behavior in the host runtime cannot do that: Duke enemies are defined by
// Duke's CON scripts plus the hard-coded cases in actors.cpp.  So instead of
// reimplementing them, this module brings up the parts of Duke's game state
// the actor system needs and runs Duke's own actor code over the Duke sprites
// in the shared engine sprite array.
//
// Division of labor with the host runtime (see source/sw/game.cpp):
//
//   host                                   this module
//   ----                                   -----------
//   owns the player, weapons, HUD          owns Duke actors, their AI and
//   owns sectors/effectors                 their projectiles/effects
//   parks Duke sprites in a host statnum   restores Duke statnums/picnums
//     and display tiles between tics         for the duration of a Duke tic
//   mirrors its player into here           reports damage dealt to the player
//
// Duke's picnums and statnums are authoritative while Duke code runs, because
// all of its AI keys off them; the host swaps sprites back into its own
// display space after each tic.  The engine's sprite/sector arrays are shared
// by both games, which is what makes this possible at all.
//-------------------------------------------------------------------------

#include "duke3d.h"
#include "actors.h"
#include "player.h"

extern "C"
{
int VB_DukeAI_CompileScripts(void);
int VB_DukeAI_BeginLevel(int playerSkill);
int VB_DukeAI_SpawnMapActor(int spriteNum);
void VB_DukeAI_SetPlayer(int playerNum, int spriteNum, int32_t x, int32_t y, int32_t z, int32_t ang, int16_t sectnum);
void VB_DukeAI_SetPlayerCount(int count);
int VB_DukeAI_TakePlayerDamageFor(int playerNum);
void VB_DukeAI_RunTic(void);
int VB_DukeAI_IsEnemyTile(int tileNum);
int VB_DukeAI_IsAdoptableTile(int tileNum);
int VB_DukeAI_HasActorCode(int tileNum);
int VB_DukeAI_MapSpriteSolidBits(int tileNum, int mapCstat, int mapHitag);
int VB_DukeAI_TileIsTripmine(int tileNum);
int VB_DukeAI_TileIsLaserLine(int tileNum);
int VB_DukeAI_TileIsFireExt(int tileNum);
int VB_DukeAI_MirrorTile(void);
int VB_DukeAI_SpriteIsNoShade(int spriteNum);
void VB_DukeAI_ActorState(int spriteNum, int *outStat, int *outAction, int *outMove, int *outCurFrame, int *outStayput);
void VB_DukeAI_WatchSprite(int spriteNum);
void VB_DukeAI_WatchedState(int *outStat, int *outXvel, int *outZvel, int *outCurFrame, int *outHitag);
void VB_DukeAI_WatchedPos(int *outX, int *outY, int *outZ, int *outAng, int *outAction, int *outMove);
int VB_DukeAI_WatchedFloorDist(void);
int VB_DukeAI_WatchedPicnum(void);
char const *VB_DukeAI_SoundFilename(int soundNum);
int VB_DukeAI_HighestSoundIndex(void);
void VB_DukeAI_SetSoundHook(int (*hook)(int soundNum, int spriteNum));   // sounds.cpp
void VB_DukeAI_SetMapTriggerHook(void (*hook)(int hitag));               // actors.cpp
int VB_DukeAI_SpriteIsAlive(int spriteNum);
int VB_DukeAI_SpriteIsEnemy(int spriteNum);
int VB_DukeAI_PlayerStatnum(void);
int VB_DukeAI_PlayerTileNumber(void);
void VB_DukeAI_BeginPlayerSprite(int spriteNum);
int VB_DukeAI_SpriteHealth(int spriteNum);
void VB_DukeAI_DamageActor(int spriteNum, int damage, int hitKind, int impactAng, int ownerSprite);
void VB_DukeAI_MoveActorsOnly(void);   // actors.cpp
}

// Weapon classes the host runtime can report; see VB_DukeAI_DamageActor().
enum
{
    VB_DUKEAI_HIT_BULLET,
    VB_DUKEAI_HIT_MELEE,
    VB_DUKEAI_HIT_EXPLOSION,
    VB_DUKEAI_HIT_PROJECTILE,
};

// Duke's player health is mirrored, not owned, here: the host applies the
// damage Duke's actors deal to its own player.  The pool is re-armed every tic
// and has to fit sprite.extra, which is 16-bit; it only needs to be far above
// any single tic's damage so Duke's own death handling never runs.
static int32_t const kMirroredPlayerHealth = 30000;

static int32_t g_dukeAILevelReady;
static uint8_t g_dukeAIPendingDamage[MAXSPRITES];

int VB_DukeAI_BeginLevel(int playerSkill)
{
    if (!VB_DukeAI_CompileScripts())
        return 0;

    // Duke's spawn code gates enemies on skill (an enemy whose lotag exceeds
    // ud.player_skill is hidden), and refuses to spawn any of them when
    // monsters are off, so the host's skill has to be mirrored here.
    ud.player_skill = clamp(playerSkill, 0, 4);
    ud.m_player_skill = ud.player_skill;
    ud.multimode = 1;
    ud.coop = 0;

    // Duke's per-sprite actor records must not carry state over from whatever
    // the host runtime did with these sprite slots.
    Bmemset(actor, 0, sizeof(ActorData_t) * MAXSPRITES);

    for (int i = 0; i < MAXSPRITES; ++i)
        A_ResetVars(i);

    g_dukeAILevelReady = 1;
    return 1;
}

int VB_DukeAI_IsEnemyTile(int tileNum)
{
    if ((unsigned)tileNum >= MAXTILES)
        return 0;

    return A_CheckEnemyTile(tileNum) ? 1 : 0;
}

// Map sprites the host should hand to Duke wholesale.  Enemies are the point of
// this module, and the tripbomb comes with them: Duke's own standable code is
// what lays out its laser line sprites and decides what crossing the beam does,
// so approximating it elsewhere is exactly what this bridge is trying to stop.
// True when Duke's compiled CON has an actor block for this tile, i.e. Duke
// itself would run per-tic code for such a sprite.  Not every one of those tiles
// is flagged as an enemy: E3L6's LIZMANSTAYPUT is a plain `actor` block that
// turns itself into a LIZMAN via `cactor` on its first tic, so an enemy-tile
// test alone leaves it behind on the host's approximation layer.
// The blocking bits Duke's own spawn leaves on a map sprite of this tile.
//
// The host keeps most Duke props on its own approximation layer instead of
// adopting them, so Duke's A_Spawn() never runs for them and the solidity it
// would have applied is lost.  The host used to guess from a short hand-written
// list, and everything missing from it was walk-through: E1L1's rooftop vent
// fan, its grates, every masked wall and every wall switch on the level.  This
// answers the same question from Duke's side, with Duke's own tile names and
// Duke's own generic rules, so it can be kept beside A_Spawn() as that changes.
//
// The per-tile cases are mechanically derived from the cstat assignments in
// A_Spawn(); tiles it makes merely hittable (QUEBALL/STRIPEBALL) are absent,
// and the cases A_Spawn() guards on game state are guarded here the same way.
int VB_DukeAI_MapSpriteSolidBits(int tileNum, int mapCstat, int mapHitag)
{
    int bits = mapCstat & (CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);

    if ((unsigned)tileNum >= MAXTILES)
        return bits;

    // A_Spawn()'s generic pass over aligned map sprites, which runs before the
    // per-tile switch and returns straight out of it.
    if ((mapCstat & CSTAT_SPRITE_ALIGNMENT) && tileNum != SPEAKER && tileNum != LETTER && tileNum != DUCK &&
        tileNum != TARGET && tileNum != TRIPBOMB && tileNum != VIEWSCREEN && tileNum != VIEWSCREEN2 &&
        !(tileNum >= CRACK1 && tileNum <= CRACK4))
    {
        if (A_CheckSwitchTileNum(tileNum) && (mapCstat & CSTAT_SPRITE_ALIGNMENT_WALL))
            return bits | CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;

        // A tagged aligned sprite becomes a shootable faller.
        if (mapHitag)
            return bits | CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;
    }

    switch (tileGetMapping(tileNum))
    {
    // A_Spawn() assigns these a bare hittable cstat, wiping any blocking bit
    // the map gave them.
    case FEMMAG1__:
    case FEMMAG2__:
    case NATURALLIGHTNING__:
    case QUEBALL__:
    case STRIPEBALL__:
        return 0;

    case CAMERA1__:
    case CAMERAPOLE__:
        return g_damageCameras ? (CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN) : 0;

    case PODFEM1__:
    case NAKED1__:
    case TOUGHGAL__:
    case BLOODYPOLE__:
    case ANTENNA__:
    case BLIMP__:
    case BOTTLE1__:
    case BOTTLE10__:
    case BOTTLE11__:
    case BOTTLE12__:
    case BOTTLE13__:
    case BOTTLE14__:
    case BOTTLE15__:
    case BOTTLE16__:
    case BOTTLE17__:
    case BOTTLE18__:
    case BOTTLE19__:
    case BOTTLE2__:
    case BOTTLE3__:
    case BOTTLE4__:
    case BOTTLE5__:
    case BOTTLE6__:
    case BOTTLE7__:
    case BOTTLE8__:
    case BOUNCEMINE__:
    case BOX__:
    case CACTUS__:
    case CACTUSBROKE__:
    case CAMERALIGHT__:
    case CANWITHSOMETHING__:
    case CANWITHSOMETHING2__:
    case CANWITHSOMETHING3__:
    case CANWITHSOMETHING4__:
    case CHAIR1__:
    case CHAIR2__:
    case CHAIR3__:
    case CONE__:
    case CRACK1__:
    case CRACK2__:
    case CRACK3__:
    case CRACK4__:
    case CRANE__:
    case DOMELITE__:
    case DOORSHOCK__:
    case DUCK__:
    case DUKECAR__:
    case DUKELYINGDEAD__:
    case EGG__:
    case EXPLODINGBARREL__:
    case FANSPRITE__:
    case FETUS__:
    case FETUSBROKE__:
    case FIREBARREL__:
    case FIREEXT__:
    case FIREVASE__:
    case FUELPOD__:
    case GRATE1__:
    case HANGLIGHT__:
    case HEAVYHBOMB__:
    case HELECOPT__:
    case HORSEONSIDE__:
    case HYDRENT__:
    case INDY__:
    case IVUNIT__:
    case JURYGUY__:
    case LETTER__:
    case LUKE__:
    case MASKWALL1__:
    case MASKWALL10__:
    case MASKWALL11__:
    case MASKWALL12__:
    case MASKWALL13__:
    case MASKWALL14__:
    case MASKWALL15__:
    case MASKWALL2__:
    case MASKWALL3__:
    case MASKWALL4__:
    case MASKWALL5__:
    case MASKWALL6__:
    case MASKWALL7__:
    case MASKWALL8__:
    case MASKWALL9__:
    case MONK__:
    case MOVIECAMERA__:
    case NUKEBARREL__:
    case NUKEBARRELDENTED__:
    case NUKEBARRELLEAKED__:
    case OCEANSPRITE1__:
    case OCEANSPRITE2__:
    case OCEANSPRITE3__:
    case OCEANSPRITE5__:
    case OOZFILTER__:
    case PANNEL1__:
    case PANNEL2__:
    case PIPE1__:
    case PIPE2__:
    case PIPE3__:
    case PIPE4__:
    case PIPE5__:
    case PIPE6__:
    case POT1__:
    case POT2__:
    case POT3__:
    case RUBBERCAN__:
    case SATELITE__:
    case SCALE__:
    case SEENINE__:
    case SOLARPANNEL__:
    case SPACEMARINE__:
    case STALL__:
    case SUSHIPLATE1__:
    case SUSHIPLATE2__:
    case SUSHIPLATE3__:
    case SUSHIPLATE4__:
    case SUSHIPLATE5__:
    case TARGET__:
    case TIRE__:
    case TOILET__:
    case TREE1__:
    case TREE2__:
    case TRIPODCAMERA__:
    case VACUUM__:
    case VASE__:
    case WAITTOBESEATED__:
    case WATERFOUNTAIN__:
    case WOODENHORSE__:
        bits |= CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;
        break;

    default:
        break;
    }

    // Duke's last word on any map sprite: solid implies hittable.
    if (bits & CSTAT_SPRITE_BLOCK)
        bits |= CSTAT_SPRITE_BLOCK_HITSCAN;

    return bits;
}

int VB_DukeAI_HasActorCode(int tileNum)
{
    if ((unsigned)tileNum >= MAXTILES)
        return 0;

    return g_tile[tileNum].execPtr != NULL ? 1 : 0;
}

int VB_DukeAI_IsAdoptableTile(int tileNum)
{
    if ((unsigned)tileNum >= MAXTILES)
        return 0;

    if (A_CheckEnemyTile(tileNum))
        return 1;

    switch (tileGetMapping(tileNum))
    {
    case TRIPBOMB__:
        return 1;

    // FIREEXT arms the host's charges and drives its effectors by hitag; it is
    // adoptable because the host installs VB_DukeAI_SetMapTriggerHook() to
    // receive that tag.  Without the hook it would explode without triggering
    // anything, which is why it was previously left out.
    case FIREEXT__:
        return 1;

    // Duke's own destructible props.  Each of these is self-contained in Duke --
    // it burns, breaks or explodes and damages what is nearby without reaching
    // into the map's sector effectors -- so Duke can own them outright and the
    // host stops approximating them.
    case EXPLODINGBARREL__:
    case FIREBARREL__:
    case NUKEBARREL__:
    case FIREVASE__:
    case BOUNCEMINE__:
    case CANWITHSOMETHING__:
    case CANWITHSOMETHING2__:
    case CANWITHSOMETHING3__:
    case CANWITHSOMETHING4__:
        return 1;

    // SEENINE/OOZFILTER charges are deliberately NOT adopted.  Detonating one
    // drives the SE_13 explosive sector that opens the wall, and those effectors
    // are run by the host bridge, not by Duke here -- adopting the charge would
    // make it explode without ever opening anything.  The host applies Duke's
    // own visibility rule to them instead.
    default:
        return 0;
    }
}

// Tile predicates the host uses to classify the Duke sprites it is holding.
// Outside a Duke tic those sprites carry the host's display tiles, so the host
// asks about the Duke tile it recorded rather than the sprite's current one.
int VB_DukeAI_TileIsTripmine(int tileNum)
{
    return (unsigned)tileNum < MAXTILES && tileGetMapping(tileNum) == TRIPBOMB__;
}

// Duke's mirror marker tile.  Duke and the host both recognise a mirror the same
// way -- a wall whose overpicnum is the mirror tile with cstat bit 32 -- so the
// host only needs to know which tile number Duke means.  It is a dynamic name,
// so it has to be read from here rather than hardcoded.
int VB_DukeAI_MirrorTile(void)
{
    return MIRROR;
}

// Duke lights most sprites from their sector and only uses the sprite's own
// shade for actors flagged NOSHADE.
// Duke's internal state for one actor, so a host can tell a stuck actor from one
// that is deliberately holding position.
void VB_DukeAI_ActorState(int spriteNum, int *outStat, int *outAction, int *outMove, int *outCurFrame,
                          int *outStayput)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return;

    if (outStat)     *outStat = sprite[spriteNum].statnum;
    if (outAction)   *outAction = AC_ACTION_ID(actor[spriteNum].t_data);
    if (outMove)     *outMove = AC_MOVE_ID(actor[spriteNum].t_data);
    if (outCurFrame) *outCurFrame = AC_CURFRAME(actor[spriteNum].t_data);
    if (outStayput)  *outStayput = actor[spriteNum].actorstayput;
}

int VB_DukeAI_SpriteIsNoShade(int spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return 0;

    return A_CheckSpriteFlags(spriteNum, SFLAG_NOSHADE) ? 1 : 0;
}

int VB_DukeAI_TileIsFireExt(int tileNum)
{
    return (unsigned)tileNum < MAXTILES && tileGetMapping(tileNum) == FIREEXT__;
}

int VB_DukeAI_TileIsLaserLine(int tileNum)
{
    return (unsigned)tileNum < MAXTILES && tileGetMapping(tileNum) == LASERLINE__;
}

// Runs Duke's own map-sprite spawn for one sprite, which is what gives the
// actor its Duke statnum, Duke health (CON "strength"), clipdist, cstat and
// actor_t state.  The caller must have restored the sprite's Duke picnum.
int VB_DukeAI_SpawnMapActor(int spriteNum)
{
    if (!g_dukeAILevelReady || (unsigned)spriteNum >= MAXSPRITES)
        return 0;

    if (sprite[spriteNum].statnum >= MAXSTATUS)
        return 0;

    A_ResetVars(spriteNum);
    A_Spawn(-1, spriteNum);

    return 1;
}

// Duke's actors pick targets from every connected player, so each of the host's
// players is mirrored into its own Duke slot.  Mirroring only the local player
// would also make the AI follow a different target on every client, which cannot
// survive a lockstep netgame.
void VB_DukeAI_SetPlayerCount(int count)
{
    if (!g_dukeAILevelReady)
        return;

    count = clamp(count, 1, MAXPLAYERS);

    for (int i = 0; i < count; ++i)
    {
        G_MaybeAllocPlayer(i);
        g_player[i].connected = 1;
    }

    for (int i = count; i < MAXPLAYERS; ++i)
        g_player[i].connected = 0;

    ud.multimode = count;
}

void VB_DukeAI_SetPlayer(int playerNum, int spriteNum, int32_t x, int32_t y, int32_t z, int32_t ang, int16_t sectnum)
{
    if (!g_dukeAILevelReady || (unsigned)spriteNum >= MAXSPRITES || (unsigned)playerNum >= MAXPLAYERS)
        return;

    DukePlayer_t *const pPlayer = g_player[playerNum].ps;
    if (pPlayer == NULL)
        return;

    pPlayer->opos = pPlayer->pos;
    pPlayer->pos = { x, y, z };

    // Duke's line-of-sight checks start from the player's sector, so it has to
    // be the sector the player is actually standing in.  The host's own sector
    // field is allowed to disagree with the player's position for a tic or two
    // while walking or falling, and feeding that through makes cansee() fail --
    // which reads as actors and tripbombs never noticing the player at all.
    int16_t validatedSect = sectnum;
    updatesector(x, y, &validatedSect);
    pPlayer->cursectnum = (validatedSect >= 0) ? validatedSect : sectnum;
    pPlayer->q16ang = fix16_from_int(ang & 2047);
    pPlayer->i = (int16_t)spriteNum;
    pPlayer->on_ground = 1;
    pPlayer->dead_flag = 0;
    pPlayer->jetpack_on = 0;
    pPlayer->holoduke_on = -1;
    pPlayer->newowner = -1;
    pPlayer->curr_weapon = PISTOL_WEAPON;

    // Duke keeps player health on the player's sprite.  The host owns the real
    // health, so give Duke a large pool and read back what its actors take off.
    sprite[spriteNum].extra = kMirroredPlayerHealth;
}

// Damage Duke's actors dealt to a mirrored player since the last call.
int VB_DukeAI_TakePlayerDamageFor(int playerNum)
{
    if (!g_dukeAILevelReady || (unsigned)playerNum >= MAXPLAYERS)
        return 0;

    DukePlayer_t *const pPlayer = g_player[playerNum].ps;
    if (pPlayer == NULL || (unsigned)pPlayer->i >= MAXSPRITES)
        return 0;

    int32_t const damage = kMirroredPlayerHealth - sprite[pPlayer->i].extra;
    sprite[pPlayer->i].extra = kMirroredPlayerHealth;

    return damage > 0 ? damage : 0;
}

// Duke decides what is an enemy from the actor's own SFLAG_BADGUY, which stays
// correct through every animation frame the actor cycles through.
int VB_DukeAI_SpriteIsEnemy(int spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES || sprite[spriteNum].statnum >= MAXSTATUS)
        return 0;

    return A_CheckEnemySprite(&sprite[spriteNum]) ? 1 : 0;
}

// Duke's CON "definesound" table, exposed so the host can pair Duke's sounds
// with its own copies of the same files instead of carrying a hand-written
// table of sound numbers.
char const *VB_DukeAI_SoundFilename(int soundNum)
{
    if (!g_dukeAILevelReady || (unsigned)soundNum > (unsigned)g_highestSoundIdx || g_sounds == NULL)
        return NULL;

    sound_t const *const pSound = g_sounds[soundNum];

    return (pSound != NULL && pSound->filename != NULL && pSound->filename[0] != '\0') ? pSound->filename : NULL;
}

int VB_DukeAI_HighestSoundIndex(void)
{
    return g_dukeAILevelReady ? g_highestSoundIdx : 0;
}

int VB_DukeAI_PlayerStatnum(void)
{
    return STAT_PLAYER;
}

int VB_DukeAI_PlayerTileNumber(void)
{
    return APLAYER;
}

// Duke's actors only treat a sprite as the player if it looks like Duke's own
// player sprite: APLAYER, with yvel holding the player index and health in
// extra.  The host restores its own values after the tic.
void VB_DukeAI_BeginPlayerSprite(int spriteNum)
{
    if (!g_dukeAILevelReady || (unsigned)spriteNum >= MAXSPRITES)
        return;

    sprite[spriteNum].picnum = APLAYER;
    sprite[spriteNum].yvel = 0;
    sprite[spriteNum].extra = kMirroredPlayerHealth;
}

int VB_DukeAI_SpriteIsAlive(int spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return 0;

    return sprite[spriteNum].statnum < MAXSTATUS && sprite[spriteNum].extra > 0;
}

int VB_DukeAI_SpriteHealth(int spriteNum)
{
    if ((unsigned)spriteNum >= MAXSPRITES)
        return 0;

    return sprite[spriteNum].extra;
}

// Damage dealt by the host's weapons.  Duke's own weapons hand an actor its
// damage through these actor_t fields and let the actor's CON "ifhitweapon"
// state react, which is where Duke's pain, knockback, gibbing and death
// behavior all come from; the host's weapons therefore do exactly the same.
void VB_DukeAI_DamageActor(int spriteNum, int damage, int hitKind, int impactAng, int ownerSprite)
{
    if (!g_dukeAILevelReady || (unsigned)spriteNum >= MAXSPRITES || damage <= 0)
        return;

    if (sprite[spriteNum].statnum >= MAXSTATUS)
        return;

    // Duke actors react to *what* hit them, so map the host's weapon classes to
    // the Duke tiles its own weapons would have left in htpicnum.  The tile
    // names are dynamic, so this mapping belongs on this side of the bridge.
    int16_t weaponTile;
    switch (hitKind)
    {
    case VB_DUKEAI_HIT_MELEE:      weaponTile = KNEE; break;
    case VB_DUKEAI_HIT_EXPLOSION:  weaponTile = RPG; break;
    case VB_DUKEAI_HIT_PROJECTILE: weaponTile = FIRELASER; break;
    default:                       weaponTile = SHOTSPARK1; break;
    }

    actor[spriteNum].htpicnum = weaponTile;
    actor[spriteNum].htextra += damage;
    actor[spriteNum].htang = (int16_t)(impactAng & 2047);
    actor[spriteNum].htowner = (int16_t)ownerSprite;

    g_dukeAIPendingDamage[spriteNum] = 1;
}

// Duke promotes a sleeping actor the instant it is hit.  The host's damage
// arrives while Duke's sprite lists are swapped out, so the promotion is done
// here, at the top of the tic, where changespritestat() is safe.
static void VB_DukeAI_ApplyPendingDamage(void)
{
    for (int spriteNum = 0; spriteNum < MAXSPRITES; ++spriteNum)
    {
        if (!g_dukeAIPendingDamage[spriteNum])
            continue;

        g_dukeAIPendingDamage[spriteNum] = 0;

        if (sprite[spriteNum].statnum >= MAXSTATUS)
            continue;

        if (!A_CheckSpriteFlags(spriteNum, SFLAG_NODAMAGEPUSH) && sprite[spriteNum].extra > 0)
        {
            if ((sprite[spriteNum].cstat & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_FACING)
                sprite[spriteNum].ang = (actor[spriteNum].htang + 1024) & 2047;

            sprite[spriteNum].xvel = (int16_t)-(actor[spriteNum].htextra << 2);

            int16_t sectNum = sprite[spriteNum].sectnum;
            if ((unsigned)sectNum < MAXSECTORS)
            {
                pushmove(&sprite[spriteNum].xyz, &sectNum, 128L, 4L << 8, 4L << 8, CLIPMASK0);
                if (sectNum != sprite[spriteNum].sectnum && (unsigned)sectNum < MAXSECTORS)
                    changespritesect(spriteNum, sectNum);
            }
        }

        if (sprite[spriteNum].statnum == STAT_ZOMBIEACTOR)
        {
            changespritestat(spriteNum, STAT_ACTOR);
            actor[spriteNum].timetosleep = SLEEPTIME;
        }
    }
}

// One actor can be watched from the host; its Duke-space state is captured at
// the end of the tic, while its statnum and picnum still mean Duke's things.
static int g_dukeAIWatchSprite = -1;
static int g_dukeAIWatchStat, g_dukeAIWatchXvel, g_dukeAIWatchZvel, g_dukeAIWatchCurFrame, g_dukeAIWatchHitag;

void VB_DukeAI_WatchSprite(int spriteNum)
{
    g_dukeAIWatchSprite = spriteNum;
}

void VB_DukeAI_WatchedPos(int *outX, int *outY, int *outZ, int *outAng, int *outAction, int *outMove)
{
    if ((unsigned)g_dukeAIWatchSprite >= MAXSPRITES)
        return;

    if (outX)      *outX = sprite[g_dukeAIWatchSprite].x;
    if (outY)      *outY = sprite[g_dukeAIWatchSprite].y;
    if (outZ)      *outZ = sprite[g_dukeAIWatchSprite].z;
    if (outAng)    *outAng = sprite[g_dukeAIWatchSprite].ang;
    if (outAction) *outAction = AC_ACTION_ID(actor[g_dukeAIWatchSprite].t_data);
    if (outMove)   *outMove = AC_MOVE_ID(actor[g_dukeAIWatchSprite].t_data);
}

// Distance from the watched actor to the floor Duke believes is under it.  This
// is what CON's iffloordistl tests, and it is how a jetpack trooper decides to
// stop flying and walk.
int VB_DukeAI_WatchedFloorDist(void)
{
    if ((unsigned)g_dukeAIWatchSprite >= MAXSPRITES)
        return -1;

    return (actor[g_dukeAIWatchSprite].floorz - sprite[g_dukeAIWatchSprite].z) >> 8;
}

int VB_DukeAI_WatchedPicnum(void)
{
    return (unsigned)g_dukeAIWatchSprite < MAXSPRITES ? sprite[g_dukeAIWatchSprite].picnum : -1;
}

void VB_DukeAI_WatchedState(int *outStat, int *outXvel, int *outZvel, int *outCurFrame, int *outHitag)
{
    if (outStat)     *outStat = g_dukeAIWatchStat;
    if (outXvel)     *outXvel = g_dukeAIWatchXvel;
    if (outZvel)     *outZvel = g_dukeAIWatchZvel;
    if (outCurFrame) *outCurFrame = g_dukeAIWatchCurFrame;
    if (outHitag)    *outHitag = g_dukeAIWatchHitag;
}

void VB_DukeAI_RunTic(void)
{
    if (!g_dukeAILevelReady)
        return;

    VB_DukeAI_ApplyPendingDamage();
    VB_DukeAI_MoveActorsOnly();

    // Duke's actors queue the damage they deal in the target's actor record;
    // it is Duke's player code that turns that into lost health.  The host owns
    // the player, so run just that step here and report the result.
    for (int i = 0; i < ud.multimode; ++i)
        if (g_player[i].ps != NULL && (unsigned)g_player[i].ps->i < MAXSPRITES)
            A_IncurDamage(g_player[i].ps->i);

    if ((unsigned)g_dukeAIWatchSprite < MAXSPRITES)
    {
        g_dukeAIWatchStat = sprite[g_dukeAIWatchSprite].statnum;
        g_dukeAIWatchXvel = sprite[g_dukeAIWatchSprite].xvel;
        g_dukeAIWatchZvel = sprite[g_dukeAIWatchSprite].zvel;
        g_dukeAIWatchCurFrame = AC_CURFRAME(actor[g_dukeAIWatchSprite].t_data);
        g_dukeAIWatchHitag = sprite[g_dukeAIWatchSprite].hitag;
    }
}
