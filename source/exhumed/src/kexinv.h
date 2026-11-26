//-------------------------------------------------------------------------
//
// The remaster's collectibles for NNBuild.
//
// PowerSlave Exhumed carries the console game's inventory, which Build
// PowerSlave never had: six artifacts that change how the player moves, eight
// transmitter pieces that decide the ending, 31 team dolls and five ankhs.
// The converter emits them as Build pickups on four of the "pickable item"
// lotags the original accepts and ignores, the hitag naming the piece; this
// module is what picks them up, remembers them, and describes them.
//
//-------------------------------------------------------------------------
#ifndef __kexinv_h__
#define __kexinv_h__

#include <stdint.h>

enum
{
    kKexArtiSandals = 0,    // higher jump
    kKexArtiMask,           // breathe under water much longer (the artifact is permanent)
    kKexArtiShawl,          // hold jump while falling to float
    kKexArtiAnklets,        // half damage from lava and slime
    kKexArtiScepter,        // walks through force fields (masked walls listed in kexforce.txt) and laser beams
    kKexArtiFeather,        // hold jump while falling to hover
    kKexArtiCount
};

// KEX's weapon-boost orb is carried through Build's otherwise inert lotag 10.
#define KEX_POWER_DROP_HITAG 2400

// The lotags the converter uses (kexmap.py KEX_INV_LOTAG_*).
enum
{
    // KEX-only health/ammo pickups. These are all inert "pickable item"
    // lotags in DOS PowerSlave, so the classic game keeps its old behaviour.
    // Their hitags are, respectively: KEX health points and orb scale (1/2).
    kKexLotagFullHealth  = 15,
    kKexLotagFullAmmo    = 24,
    kKexLotagArtifact    = 34,   // hitag: artifact index
    kKexLotagTransmitter = 36,   // hitag: piece 0..7
    kKexLotagDoll        = 39,   // hitag: doll 0..30
    kKexLotagAnkh        = 40,   // hitag: ankh 1..5
    kKexLotagAmmo        = 41,   // hitag: actor scale, 1 or 2
    kKexLotagHealth      = 42,   // hitag: KEX health amount, 20 or 50
};

#define KEX_DOLL_COUNT      31
#define KEX_DOLLS_DOLPHIN   10   // the remaster's first team-doll ability
#define KEX_DOLLS_VULTURE   14   // ... followed by hold-jump flight
#define KEX_TRANS_PIECES    8
#define KEX_CAMPAIGN_MAX_MAPS 64

struct KexInv
{
    uint8_t  artifacts;      // bit per kKexArti*
    uint8_t  transmitter;    // bit per piece
    uint8_t  ankhs;          // bit per ankh (bit 1..5)
    uint8_t  map;            // this level's map scroll picked up
    uint32_t dolls;          // bit per doll
};

extern KexInv KexInvList[];

// Remaster Game Settings. These affect only the KEX path; DOS PowerSlave
// retains its original scarab/life and weapon presentation behaviour.
extern int kex_enable_checkpoints;
extern float kex_view_bob_speed;
extern float kex_view_bob_height;
extern float kex_turn_sway;
extern int kex_weapon_jump_bobbing;

// Remaster HUD Settings.  These stay out of the classic PowerSlave HUD and
// expose the same three controls Nightdive registers as g_showPlayTime,
// g_showHudMessages, and g_showCrosshair (a 0..3 scale, where 0 is hidden).
extern int kex_show_play_time;
extern int kex_show_hud_messages;
extern float kex_crosshair_size;

// PowerSlave Exhumed's three campaign difficulties.  The DOS game has no
// equivalent setting, so this state is consulted only by the KEX path.
enum KexDifficulty
{
    kKexDifficultyNormal = 0,
    kKexDifficultyHard,
    kKexDifficultyPharaoh,
    kKexDifficultyCount
};

// KEX actor definitions express health and damage in visible gameplay points.
// The native Exhumed runlist stores health at four internal units per point
// and multiplies runlist_DamageEnemy() input by the same factor. Keep the
// conversion explicit so actor health, thresholds and diagnostics cannot mix
// the two domains accidentally.
#define KEX_HEALTH_UNIT_SCALE 4
static inline int KexHealthUnits(int nPoints) { return nPoints * KEX_HEALTH_UNIT_SCALE; }
static inline int KexHealthPoints(int nUnits) { return nUnits / KEX_HEALTH_UNIT_SCALE; }

int  KexDifficulty_Get(void);
void KexDifficulty_Set(int nDifficulty);
int  KexDifficulty_PainChance(int nNormalChance); // Pharaoh halves the actor definition's byte
int  KexDifficulty_PainRoll(int nNormalChance);   // exact UInt8() < adjusted chance rule
int  KexDifficulty_AICooldown(void);              // ranged-attack delay for the selected difficulty

void KexInv_Reset(int nPlayer);          // new game
void KexInv_InitAmmo(int nPlayer);       // KEX starts every hidden weapon pool full
void KexInv_BeginLevel(void);            // before map sprites are constructed
void KexInv_LevelStart(void);            // per level: the map scroll is per level
void KexInv_AddShrinePad(int nSprite, int nArtifact);   // KILENTRY's ARTIFACT_TRIGGER pads
void KexInv_AddShrineSpawn(int nSprite, int nArtifact); // KILENTRY's ARTIFACT_SPAWN destinations
int  KexInv_WeaponForPickupLotag(int nItem);             // Build item lotag -> weapon, or -1
int  KexInv_Pickup(int nPlayer, int nItem, int nHitag);   // 1 = taken
int  KexInv_PickupPower(int nPlayer);                      // timed weapon boost
int  KexInv_PickupAmmo(int nPlayer, int nScale);           // current weapon power
int  KexInv_PickupHealth(int nPlayer, int nAmount);        // amount is in KEX's 200-point scale
int  KexInv_PickupFullAmmo(int nPlayer);
int  KexInv_PickupFullHealth(int nPlayer);
int  KexInv_AmmoMax(int nAmmoType);                        // KEX cap, by Build ammo index
void KexInv_GiveArtifact(int nPlayer, int nArtifact);
int  KexInv_HasArtifact(int nPlayer, int nArtifact);
int  KexInv_AnkhCount(int nPlayer);
int  KexInv_DollCount(int nPlayer);
int  KexInv_TransCount(int nPlayer);
int  KexInv_MaxHealth(int nPlayer);      // difficulty base, multiplied by 1 + ankhs in Kex mode

// KEX checkpoints reload the map, then put the checkpoint-time player state
// back into the newly constructed world. The DOS save-point path only moves a
// freshly healed player and therefore cannot be used for remaster content.
void KexCheckpoint_Reset(void);
int  KexCheckpoint_Mark(int nPlayer, int nSprite);
void KexCheckpoint_DeathStart(int nPlayer);
void KexCheckpoint_DeathTick(int nPlayer);
int  KexCheckpoint_HasCheckpoint(void);
int  KexCheckpoint_Reload(int nPlayer, int bRestartLevel);
int  KexCheckpoint_RegisterSprite(int nSprite); // decode converter's authored actor ID; 0 = suppress on reload
void KexCheckpoint_SpriteDeleted(int nSprite);  // preserve authored removals at the next checkpoint
void KexCheckpoint_LevelReady(void);            // map construction is over; runtime removals now count
void KexCheckpoint_WorldLoaded(void);           // reapply checkpoint-time geometry and event channels
int  KexCheckpoint_PrepareSpawn(int nPlayer);
void KexCheckpoint_CaptureEntry(int nPlayer);   // source persistent state at this map's entrance
void KexCheckpoint_FinishSpawn(int nPlayer);

// Persistent campaign state used by the remaster's overworld. A new game
// starts with every destination locked; completing a map unlocks its authored
// neighbours, and entering a destination marks that map visited. Travel/load
// also stages persistent player combat state across RestartPlayer(), which
// otherwise heals the player, raises low ammo to DOS minimums and rewrites
// weapon selection while constructing every destination level.
void KexCampaign_NewGame(void);
int  KexCampaign_MapUnlocked(int nMap);
void KexCampaign_UnlockMap(int nMap);
void KexCampaign_StagePlayer(int nPlayer);
void KexCampaign_FinishSpawn(int nPlayer);

// Gameplay-only campaign time.  It advances with the 30 Hz simulation, not
// presentation or menu clocks, and is carried by the KEX campaign sidecar.
uint64_t KexPlayTime_Ticks(void);
uint64_t KexPlayTime_Seconds(void);

// kexinv.txt: the remaster's own wording, keyed by name (kexmenu.py writes
// it). KexInv_Format expands "${0}" to num and "\n" to a newline.
char const *KexInv_Text(char const *key);
void KexInv_Format(char *out, int outSize, char const *key, int num);

void KexInv_Save(int nSlot);
// Query mapped-slot metadata for the remaster slot screen. Legacy sidecars are
// migrated in memory; classic slot occupancy is recovered from savgamea.sav.
int  KexInv_SaveSlotInfo(int nSlot, char *map, int mapSize, int *complete);
// Restore the KEX sidecar and return the numeric map value the classic game
// loop should use.  KEX maps themselves are selected through
// gUserMapFilename; the returned value is only the loop's load/terminal
// sentinel. nClassicMap is the value recovered from savgamea.sav.
int  KexInv_Load(int nSlot, int nClassicMap);

void KexInv_Tick(void);                  // per tick: KEX shrine events and unattended probes
int  KexInv_TestWantsScreen(void);       // that test asking for the status screen
int  KexInv_TestActive(void);
int  KexInv_TestFire(void);              // the Manacle self-test holding fire
int  KexInv_TestAir(void);               // that test asking for the air gauge
int  KexInv_CaptionLine(int i, char const **text, int *ticks);   // the pickup caption stack, top down
void KexInv_CaptionTick(void);

#endif
