//-------------------------------------------------------------------------
//
// The remaster's collectibles for NNBuild. See kexinv.h.
//
//-------------------------------------------------------------------------
#include "build.h"
#include "compat.h"
#include "baselayer.h"
#include "exhumed.h"
#include "engine.h"
#include "player.h"
#include "status.h"
#include "sound.h"
#include "lighting.h"
#include "gun.h"
#include "anims.h"
#include "kexinv.h"
#include "items.h"
#include "random.h"
#include "move.h"
#include "runlist.h"
#include "menu.h"
#include "kexbreak.h"   // NNBuild: KexSound_Get
#include "kexmenu.h"
#include "ramses.h"     // KexLanguage_Index/Code
#include "save.h"

KexInv KexInvList[kMaxPlayers];
static uint8_t s_kexDifficulty = kKexDifficultyNormal;
static uint8_t s_kexUnlockedMaps[KEX_CAMPAIGN_MAX_MAPS];
static uint64_t s_kexPlayTimeTicks;

struct KexCampaignPlayerState
{
    int16_t health;
    int16_t magic;
    uint16_t weapons;
    int16_t ammo[kMaxWeapons];
    int16_t currentWeapon;
    int16_t lastWeapon;
    int16_t pistolClip;
    int16_t playerClip;
    char items[8];
    uint8_t valid;
};

static KexCampaignPlayerState s_campaignPlayer[kMaxPlayers];

// The ordinary EXSV path predates the remaster and deliberately stores only
// its local campaign state in the menu sidecar. EXSN is a frozen multiplayer
// world image, so it needs every inventory and campaign slot in-process.
class KexInventorySnapshotLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void KexInventorySnapshotLoadSave::Load()
{
    if (!LoadSave::IsNetworkSnapshot())
        return;
    Read(KexInvList, sizeof(KexInvList));
    Read(&s_kexDifficulty, sizeof(s_kexDifficulty));
    Read(s_kexUnlockedMaps, sizeof(s_kexUnlockedMaps));
    Read(&s_kexPlayTimeTicks, sizeof(s_kexPlayTimeTicks));
    Read(s_campaignPlayer, sizeof(s_campaignPlayer));
}

void KexInventorySnapshotLoadSave::Save()
{
    if (!LoadSave::IsNetworkSnapshot())
        return;
    Write(KexInvList, sizeof(KexInvList));
    Write(&s_kexDifficulty, sizeof(s_kexDifficulty));
    Write(s_kexUnlockedMaps, sizeof(s_kexUnlockedMaps));
    Write(&s_kexPlayTimeTicks, sizeof(s_kexPlayTimeTicks));
    Write(s_campaignPlayer, sizeof(s_campaignPlayer));
}

static KexInventorySnapshotLoadSave *s_kexInventorySnapshotLoadSave;

void KexInventorySnapshotLoadSaveConstruct(void)
{
    s_kexInventorySnapshotLoadSave = new KexInventorySnapshotLoadSave();
}

#define KEX_INV_SAVE_SLOTS 5

void KexCampaign_NewGame(void)
{
    Bmemset(s_kexUnlockedMaps, 0, sizeof(s_kexUnlockedMaps));
    Bmemset(s_campaignPlayer, 0, sizeof(s_campaignPlayer));
    s_kexPlayTimeTicks = 0;
}

uint64_t KexPlayTime_Ticks(void)
{
    return s_kexPlayTimeTicks;
}

uint64_t KexPlayTime_Seconds(void)
{
    return s_kexPlayTimeTicks / kTimerTicks;
}

int KexCampaign_MapUnlocked(int nMap)
{
    return (unsigned)nMap < (unsigned)KEX_CAMPAIGN_MAX_MAPS
        && s_kexUnlockedMaps[nMap] != 0;
}

void KexCampaign_UnlockMap(int nMap)
{
    if ((unsigned)nMap < (unsigned)KEX_CAMPAIGN_MAX_MAPS)
        s_kexUnlockedMaps[nMap] = 1;
}

void KexCampaign_StagePlayer(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return;

    Player const &p = PlayerList[nPlayer];
    KexCampaignPlayerState &save = s_campaignPlayer[nPlayer];
    save.health = p.nHealth;
    save.magic = p.nMagic;
    save.weapons = nPlayerWeapons[nPlayer];
    Bmemcpy(save.ammo, p.nAmmo, sizeof(save.ammo));
    save.currentWeapon = p.nCurrentWeapon;
    save.lastWeapon = p.nLastWeapon;
    save.pistolClip = nPistolClip[nPlayer];
    save.playerClip = nPlayerClip[nPlayer];
    Bmemcpy(save.items, p.items, sizeof(save.items));
    save.valid = 1;
}

void KexCampaign_FinishSpawn(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers
        || !s_campaignPlayer[nPlayer].valid)
        return;

    Player &p = PlayerList[nPlayer];
    KexCampaignPlayerState &save = s_campaignPlayer[nPlayer];
    int const savedHealth = save.health;

    p.nMagic = (short)clamp((int)save.magic, 0, (int)kMaxMagic);
    nPlayerWeapons[nPlayer] = save.weapons;
    for (int i = 0; i < kMaxWeapons; i++)
        p.nAmmo[i] = (short)clamp((int)save.ammo[i], 0, KexInv_AmmoMax(i));
    Bmemcpy(p.items, save.items, sizeof(save.items));
    nPistolClip[nPlayer] = save.pistolClip;
    nPlayerClip[nPlayer] = save.playerClip;

    int nWeapon = save.currentWeapon;
    if ((unsigned)nWeapon >= (unsigned)kMaxWeapons
        || !(nPlayerWeapons[nPlayer] & (1 << nWeapon)))
        nWeapon = kWeaponSword;
    SetNewWeaponImmediate((short)nPlayer, (short)nWeapon);
    p.nLastWeapon = ((unsigned)save.lastWeapon < (unsigned)kMaxWeapons)
        ? save.lastWeapon : kWeaponSword;

    // Old V3 sidecars can be paired with the pre-spawn backing save emitted
    // by earlier builds, whose health field was zero. Native never saves a
    // dead player; treat that legacy value as a fresh full-health campaign.
    p.nHealth = (short)(savedHealth > 0
        ? clamp(savedHealth, 1, KexInv_MaxHealth(nPlayer))
        : KexInv_MaxHealth(nPlayer));
    save.valid = 0;

    if (nPlayer == nLocalPlayer)
    {
        SetHealthFrame(0);
        SetMagicFrame();
    }
}

int KexDifficulty_Get(void)
{
    return s_kexDifficulty;
}

void KexDifficulty_Set(int nDifficulty)
{
    s_kexDifficulty = (uint8_t)clamp(nDifficulty, (int)kKexDifficultyNormal,
                                     (int)kKexDifficultyPharaoh);
}

int KexDifficulty_PainChance(int nNormalChance)
{
    int const chance = clamp(nNormalChance, 0, 255);
    return KexDifficulty_Get() == kKexDifficultyPharaoh ? chance >> 1 : chance;
}

int KexDifficulty_PainRoll(int nNormalChance)
{
    // kexSlaveAI::InflictDamage always consumes one random byte, including
    // for actors whose painChance is zero, then compares it strictly below
    // the (Pharaoh-adjusted) byte stored on the actor.
    return RandomSize(8) < KexDifficulty_PainChance(nNormalChance);
}

int KexDifficulty_AICooldown(void)
{
    // kexSlaveAI::SetAttackDelay, recovered from the remaster executable:
    // Normal 16..31; Hard chooses that range or 8..15 equiprobably;
    // Pharaoh 4..7. This timer gates the generic ranged-attack animation.
    switch (KexDifficulty_Get())
    {
        case kKexDifficultyHard:
            if (!RandomBit())
                return 8 + RandomSize(3);
            break;
        case kKexDifficultyPharaoh:
            return 4 + RandomSize(2);
        default:
            break;
    }
    return 16 + RandomSize(4);
}

void KexInv_Reset(int nPlayer)
{
    if ((unsigned)nPlayer < (unsigned)kMaxPlayers)
        Bmemset(&KexInvList[nPlayer], 0, sizeof(KexInv));
}

void KexInv_InitAmmo(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return;

    // kexSlavePlayer::Reset copies every weaponInfo maxAmmo value into the
    // player, including weapons not owned yet. A weapon pickup only exposes
    // its already-full pool; it does not grant a second packet of ammo.
    for (int nWeapon = 0; nWeapon < kMaxWeapons; nWeapon++)
        PlayerList[nPlayer].nAmmo[nWeapon] = KexInv_AmmoMax(nWeapon);
}

int KexInv_WeaponForPickupLotag(int nItem)
{
    switch (nItem)
    {
        case 9:  return kWeaponGrenade;
        case 26: return kWeaponSword;
        case 28:
        case 52: return kWeaponPistol;
        case 29:
        case 53: return kWeaponM60;
        case 30:
        case 54: return kWeaponFlamer;
        case 31: return kWeaponManacle;
        case 32:
        case 56: return kWeaponStaff;
        case 33:
        case 57: return kWeaponRing;
        default: return -1;
    }
}

// VB_KEXINV_TEST=<ticks>: that many ticks into a level, take every collectible
// it holds as if walked over, log the tally, then page through the status
// screen unattended, screenshotting each page (see KexMenu_Status).
static int s_selftest;
static int s_pickupSelftest;
static int s_difficultySelftest;
static int s_shrineSelftest;
static int s_shrineTestPhase;
static int s_shrineTestArtifact;
static int s_shrineTestExpected;
static int16_t s_shrineTestPad;
static int s_checkpointSelftest;
static int s_checkpointTestPhase;
static int s_checkpointTestWallX;
static int16_t s_checkpointTestGoneId = -1;
static int16_t s_checkpointTestReturnId = -1;
static int s_deathSelftest;
static int s_deathTestPhase;
static int s_deathTestTicks;
static int32_t s_deathTestX, s_deathTestY, s_deathTestWallX;
static int16_t s_deathTestSector;
static KexInv s_deathTestInv;
static uint16_t s_deathTestWeapons;
static int16_t s_deathTestPistolAmmo;
static int s_wantScreen;
static int s_saveSelftestDone;
static int s_travelSelftest;
static int s_travelTestTarget;
static int s_travelTestPhase;
static int s_travelTestPostTicks;
static int s_travelTestExpectedHealth;
static int s_travelTestExpectedAmmo;
static int s_travelTestExpectedWeapon;
static int s_menuLoadSelftest;
static int s_menuLoadTestSlot;
static int s_menuLoadTestPhase;

// Environment-only test delays are expressed in native player ticks.  Count
// down to zero so a value of one runs on the first tick instead of expiring.
static bool KexInv_TestCountdownElapsed(int &ticks)
{
    return ticks > 0 && --ticks == 0;
}

static int KexCheckpoint_FindActor(int nActorId);
static void KexInv_RunSaveSelftest(void);

// ---------------------------------------------------------------------------
// Checkpoints
//
// The remaster does not use Build PowerSlave's spare-lives respawn. Touching
// an Obj_Checkpoint records a compact player snapshot and the actor's authored
// position. Death rebuilds the map, then reapplies the state that existed when
// the pad was touched. Native keeps three authored-ID bit arrays for this: a
// vase broken or enemy killed before the checkpoint stays gone, while one
// broken or killed after it returns. The converted Build world additionally
// snapshots geometry and channel values so opened routes reconstruct at the
// same checkpoint state instead of trapping the player behind a reset door.
// ---------------------------------------------------------------------------
struct KexCheckpointPlayerState
{
    int16_t health;
    int16_t magic;
    uint16_t keys;
    uint16_t weapons;
    int16_t ammo[kMaxWeapons];
    int16_t currentWeapon;
    int16_t lastWeapon;
    int16_t oldWeapon;
    int16_t pistolClip;
    int16_t playerClip;
    int16_t doubleTimer;
    char items[8];
    KexInv inv;
};

struct KexCheckpointState
{
    int32_t x, y, z;
    int16_t sector, angle, id;
    int16_t numSectors, numWalls;
    uint8_t valid, pending;
    char map[BMAX_PATH];
    KexCheckpointPlayerState player;
    uint8_t activated[(kMaxSprites + 7) >> 3];
    uint8_t removed[(kMaxSprites + 7) >> 3];
    int16_t channels[kMaxChannels];
    sectortype sectors[kMaxSectors];
    walltype walls[kMaxWalls];
};

// RestartLevel() restores the persistent state that existed when this map was
// entered when no checkpoint has been touched. Keep that baseline separate
// from the latest checkpoint snapshot: a death before the first scarab must
// discard pickups from the failed attempt without making a synthetic scarab.
struct KexCheckpointEntryState
{
    KexCheckpointPlayerState player;
    uint8_t valid;
};

static KexCheckpointState s_checkpoint;
static KexCheckpointEntryState s_checkpointEntry;
static uint8_t s_checkpointDeathTicks[kMaxPlayers];
static uint8_t s_checkpointRemoved[(kMaxSprites + 7) >> 3];
static int16_t s_checkpointActorId[kMaxSprites];
static uint8_t s_checkpointWorldLive;

int kex_enable_checkpoints = 1;
// These mirror PowerSlave Exhumed's g_viewBobSpeed, g_viewBobHeight,
// g_turnSway and w_jumpSpringApply defaults. The renderer clamps the floats
// at their authored menu limits so an old or hand-edited config remains safe.
float kex_view_bob_speed = 1.f;
float kex_view_bob_height = 1.f;
float kex_turn_sway = 1.f;
int kex_weapon_jump_bobbing = 1;
// PowerSlave Exhumed initializes all three HUD Cvars at 1.0.  Its crosshair
// Cvar is a float: zero is a supported config-level off switch, while its
// slider deliberately presents the authored 0.05--3.0 range.
int kex_show_play_time = 1;
int kex_show_hud_messages = 1;
float kex_crosshair_size = 1.f;

static char const *KexCheckpoint_MapLeaf(char const *map)
{
    char const *leaf = map;
    for (char const *p = map; p && *p; p++)
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    return leaf;
}

static int KexCheckpoint_MapMatches(void)
{
    // KEX campaign saves deliberately recover an installed map through its
    // portable leaf name when the generated directory has moved. A checkpoint
    // sidecar follows the same rule, otherwise the restored snapshot would be
    // discarded by BeginLevel() immediately after a perfectly valid load.
    return !s_checkpoint.map[0] || !gUserMapFilename[0]
        || !Bstrcasecmp(s_checkpoint.map, gUserMapFilename)
        || !Bstrcasecmp(KexCheckpoint_MapLeaf(s_checkpoint.map),
                        KexCheckpoint_MapLeaf(gUserMapFilename));
}

static void KexCheckpoint_ClearActive(void)
{
    Bmemset(&s_checkpoint, 0, sizeof(s_checkpoint));
    s_checkpoint.id = -1;
    s_checkpoint.sector = -1;
    Bmemset(s_checkpointDeathTicks, 0, sizeof(s_checkpointDeathTicks));
    Bmemset(s_checkpointRemoved, 0, sizeof(s_checkpointRemoved));
    Bmemset(s_checkpointActorId, -1, sizeof(s_checkpointActorId));
    s_checkpointWorldLive = 0;
}

void KexCheckpoint_Reset(void)
{
    KexCheckpoint_ClearActive();
    Bmemset(&s_checkpointEntry, 0, sizeof(s_checkpointEntry));
}

static int KexCheckpoint_Bit(uint8_t const *bits, int id)
{
    return (unsigned)id < (unsigned)kMaxSprites
        && (bits[id >> 3] & (1 << (id & 7)));
}

static void KexCheckpoint_SetBit(uint8_t *bits, int id)
{
    if ((unsigned)id < (unsigned)kMaxSprites)
        bits[id >> 3] |= (uint8_t)(1 << (id & 7));
}

static int KexCheckpoint_ActorBit(int id)
{
    return KexCheckpoint_Bit(s_checkpoint.activated, id);
}

static void KexCheckpoint_CaptureWorld(void)
{
    s_checkpoint.numSectors = (int16_t)numsectors;
    s_checkpoint.numWalls = (int16_t)numwalls;
    Bmemcpy(s_checkpoint.sectors, sector, numsectors * sizeof(sectortype));
    Bmemcpy(s_checkpoint.walls, wall, numwalls * sizeof(walltype));
    Bmemcpy(s_checkpoint.removed, s_checkpointRemoved, sizeof(s_checkpoint.removed));
    for (int i = 0; i < kMaxChannels; i++)
        s_checkpoint.channels[i] = sRunChannels[i].c;
}

static void KexCheckpoint_CapturePlayerState(int nPlayer, KexCheckpointPlayerState &save)
{
    Player const &p = PlayerList[nPlayer];
    save.health = p.nHealth;
    save.magic = p.nMagic;
    save.keys = p.keys;
    save.weapons = nPlayerWeapons[nPlayer];
    Bmemcpy(save.ammo, p.nAmmo, sizeof(save.ammo));
    save.currentWeapon = p.nCurrentWeapon;
    save.lastWeapon = p.nLastWeapon;
    save.oldWeapon = nPlayerOldWeapon[nPlayer];
    save.pistolClip = nPistolClip[nPlayer];
    save.playerClip = nPlayerClip[nPlayer];
    save.doubleTimer = nPlayerDouble[nPlayer];
    Bmemcpy(save.items, p.items, sizeof(save.items));
    save.inv = KexInvList[nPlayer];
}

static void KexCheckpoint_RestorePlayerState(int nPlayer, KexCheckpointPlayerState const &save)
{
    Player &p = PlayerList[nPlayer];

    KexInvList[nPlayer] = save.inv;
    p.nMagic = clamp((int)save.magic, 0, (int)kMaxMagic);
    p.keys = save.keys;
    nPlayerWeapons[nPlayer] = save.weapons;
    Bmemcpy(p.nAmmo, save.ammo, sizeof(save.ammo));
    Bmemcpy(p.items, save.items, sizeof(save.items));
    nPistolClip[nPlayer] = save.pistolClip;
    nPlayerClip[nPlayer] = save.playerClip;
    nPlayerDouble[nPlayer] = save.doubleTimer;

    int nWeapon = save.currentWeapon;
    if ((unsigned)nWeapon >= (unsigned)kMaxWeapons
        || !(nPlayerWeapons[nPlayer] & (1 << nWeapon)))
        nWeapon = kWeaponSword;
    SetNewWeaponImmediate((short)nPlayer, (short)nWeapon);
    p.nLastWeapon = save.lastWeapon;
    nPlayerOldWeapon[nPlayer] = save.oldWeapon;
    p.nHealth = (short)clamp((int)save.health, 1, KexInv_MaxHealth(nPlayer));
    nPlayerLives[nPlayer] = max((short)1, nPlayerLives[nPlayer]);

    if (nPlayer == nLocalPlayer)
    {
        SetHealthFrame(0);
        SetMagicFrame();
    }
}

static void KexCheckpoint_CapturePlayer(int nPlayer)
{
    KexCheckpoint_CapturePlayerState(nPlayer, s_checkpoint.player);
}

static void KexCheckpoint_RestorePlayer(int nPlayer)
{
    KexCheckpoint_RestorePlayerState(nPlayer, s_checkpoint.player);
}

static void KexCheckpoint_ActivateSprite(int nSprite)
{
    short const nAnim = sprite[nSprite].owner;
    if (nAnim >= 0)
    {
        AnimList[nAnim].nSeq++;
        AnimFlags[nAnim] &= ~kAnimLoop;
        AnimList[nAnim].nFrame = 0;
    }
    changespritestat((short)nSprite, 899);
}

static void KexCheckpoint_ReactivateActors(void)
{
    if (!g_kexMode || !s_checkpoint.pending || !s_checkpoint.valid)
        return;

    for (int nSprite = headspritestat[959]; nSprite >= 0; )
    {
        int const nNext = nextspritestat[nSprite];
        if (KexCheckpoint_ActorBit(sprite[nSprite].hitag))
            KexCheckpoint_ActivateSprite(nSprite);
        nSprite = nNext;
    }
}

void DestroyItemAnim(short nSprite);     // items.cpp
void ProcessSpriteTag(short nSprite, short nLotag, short nHitag);   // init.cpp

// KILENTRY's shrines. Kex ArtifactEvent() is deliberately a hand-off, not a
// gift: a player carrying the pad's artifact consumes it on ARTIFACT_TRIGGER,
// fires 1000 + artifact, then recreates it at every ARTIFACT_SPAWN with that
// tag. The two marker lists stay separate because the native event loop also
// permits more than one destination for a single pad tag.
enum { kKexMaxShrinePads = 32, kKexMaxShrineSpawns = 32 };
struct KexShrinePad
{
    int16_t padSector, nArtifact;
};
struct KexShrineSpawn
{
    int16_t sector, nArtifact, picnum;
    int32_t x, y, z;
};
static KexShrinePad s_shrinePad[kKexMaxShrinePads];
static KexShrineSpawn s_shrineSpawn[kKexMaxShrineSpawns];
static int s_shrinePads, s_shrineSpawns;

void KexInv_BeginLevel(void)
{
    // AddShrinePad() runs while ExamineSprites() constructs the map, so this
    // must happen immediately before that pass rather than in LevelStart().
    Bmemset(s_shrinePad, 0, sizeof(s_shrinePad));
    Bmemset(s_shrineSpawn, 0, sizeof(s_shrineSpawn));
    s_shrinePads = s_shrineSpawns = 0;
    Bmemset(s_checkpointActorId, -1, sizeof(s_checkpointActorId));
    s_checkpointWorldLive = 0;

    // A pending same-map reload is the one path on which checkpoint state
    // crosses LoadLevel(). Every ordinary map load starts a fresh checkpoint
    // set, as the native game does.
    if (!g_kexMode || !s_checkpoint.pending || !KexCheckpoint_MapMatches())
        KexCheckpoint_Reset();
    else
    {
        Bmemset(s_checkpointRemoved, 0, sizeof(s_checkpointRemoved));
        if (s_checkpoint.valid)
        {
            Bmemcpy(s_checkpointRemoved, s_checkpoint.removed, sizeof(s_checkpointRemoved));
            // Sprite construction applies removeIfOwned and artifact-held
            // rules. Feed those rules the checkpoint snapshot, not anything
            // collected between the checkpoint and death.
            KexInvList[nLocalPlayer] = s_checkpoint.player.inv;
            nPlayerWeapons[nLocalPlayer] = s_checkpoint.player.weapons;
        }
        else if (s_checkpointEntry.valid)
        {
            // Native RestartLevel restores persistentData before LoadMap().
            // This is the equivalent entry snapshot for an untouched map.
            KexInvList[nLocalPlayer] = s_checkpointEntry.player.inv;
            nPlayerWeapons[nLocalPlayer] = s_checkpointEntry.player.weapons;
        }
    }
}

int KexCheckpoint_RegisterSprite(int nSprite)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return 1;

    // kexmap.py stores -(source actor index + 2) in the otherwise unused raw
    // map extra. Decode it before a Build constructor repurposes that member.
    int const encoded = sprite[nSprite].extra;
    if (encoded > -2)
        return 1;
    int const id = -encoded - 2;
    sprite[nSprite].extra = 0;
    if ((unsigned)id >= (unsigned)kMaxSprites)
        return 1;

    s_checkpointActorId[nSprite] = (int16_t)id;
    return !(s_checkpoint.pending && s_checkpoint.valid
             && KexCheckpoint_Bit(s_checkpoint.removed, id));
}

void KexCheckpoint_SpriteDeleted(int nSprite)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return;
    int const id = s_checkpointActorId[nSprite];
    s_checkpointActorId[nSprite] = -1;
    if (s_checkpointWorldLive && (unsigned)id < (unsigned)kMaxSprites)
        KexCheckpoint_SetBit(s_checkpointRemoved, id);
}

void KexCheckpoint_LevelReady(void)
{
    if (g_kexMode)
        s_checkpointWorldLive = 1;
}

void KexCheckpoint_WorldLoaded(void)
{
    if (!g_kexMode || !s_checkpoint.pending || !s_checkpoint.valid
        || !KexCheckpoint_MapMatches())
        return;

    if (s_checkpoint.numSectors != numsectors || s_checkpoint.numWalls != numwalls)
    {
        OSD_Printf(OSD_ERROR "Kex checkpoint %d world dimensions changed (%d/%d -> %d/%d); using rebuilt geometry\n",
                   s_checkpoint.id, s_checkpoint.numSectors, s_checkpoint.numWalls,
                   numsectors, numwalls);
        return;
    }

    // Preserve topology and the runlist-owned tag fields while restoring all
    // visible/collidable geometry. Those fields were freshly reconstructed
    // and their old run indices are not valid in the new map instance.
    for (int i = 0; i < numsectors; i++)
    {
        int16_t const wallptr = sector[i].wallptr, wallnum = sector[i].wallnum;
        int16_t const lotag = sector[i].lotag, hitag = sector[i].hitag, extra = sector[i].extra;
        sector[i] = s_checkpoint.sectors[i];
        sector[i].wallptr = wallptr; sector[i].wallnum = wallnum;
        sector[i].lotag = lotag; sector[i].hitag = hitag; sector[i].extra = extra;
    }
    for (int i = 0; i < numwalls; i++)
    {
        int16_t const point2 = wall[i].point2, nextwall = wall[i].nextwall;
        int16_t const nextsector = wall[i].nextsector;
        int16_t const lotag = wall[i].lotag, hitag = wall[i].hitag, extra = wall[i].extra;
        wall[i] = s_checkpoint.walls[i];
        wall[i].point2 = point2; wall[i].nextwall = nextwall; wall[i].nextsector = nextsector;
        wall[i].lotag = lotag; wall[i].hitag = hitag; wall[i].extra = extra;
    }

    // Re-signal only allotted channels. Controllers retain their freshly
    // rebuilt run records, then continue from the geometry above toward the
    // same state they had when the checkpoint was marked.
    for (int i = 0; i < kMaxChannels; i++)
        if (s_checkpoint.channels[i] >= 0 && sRunChannels[i].c >= 0
            && sRunChannels[i].c != s_checkpoint.channels[i])
            runlist_ChangeChannel(i, s_checkpoint.channels[i]);
    for (int i = 0; i < kMaxChannels; i++)
        if (s_checkpoint.channels[i] >= 0 && sRunChannels[i].c >= 0)
            runlist_ReadyChannel(i);
}

int KexCheckpoint_Mark(int nPlayer, int nSprite)
{
    if (!g_kexMode || !kex_enable_checkpoints || nTotalPlayers != 1
        || (unsigned)nPlayer >= (unsigned)kMaxPlayers
        || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return 0;

    int const id = sprite[nSprite].hitag;
    if (s_checkpoint.valid && s_checkpoint.id == id && KexCheckpoint_MapMatches())
        return 0;

    s_checkpoint.valid = 1;
    s_checkpoint.pending = 0;
    s_checkpoint.id = (int16_t)id;
    s_checkpoint.x = sprite[nSprite].x;
    s_checkpoint.y = sprite[nSprite].y;
    s_checkpoint.z = sprite[nSprite].z;
    s_checkpoint.sector = sprite[nSprite].sectnum;
    s_checkpoint.angle = sprite[nSprite].ang;
    Bstrncpyz(s_checkpoint.map, gUserMapFilename, sizeof(s_checkpoint.map));
    KexCheckpoint_SetBit(s_checkpoint.activated, id);
    KexCheckpoint_CapturePlayer(nPlayer);
    KexCheckpoint_CaptureWorld();
    SetSavePoint(nPlayer, s_checkpoint.x, s_checkpoint.y, s_checkpoint.z,
                 s_checkpoint.sector, s_checkpoint.angle);
    s_checkpointDeathTicks[nPlayer] = 0;

    OSD_Printf("Kex checkpoint %d marked in sector %d\n", id, s_checkpoint.sector);
    return 1;
}

void KexCheckpoint_CaptureEntry(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers
        || s_checkpoint.pending)
        return;

    KexCheckpoint_CapturePlayerState(nPlayer, s_checkpointEntry.player);
    s_checkpointEntry.valid = 1;
}

void KexCheckpoint_DeathStart(int nPlayer)
{
    if (g_kexMode && nTotalPlayers == 1
        && (unsigned)nPlayer < (unsigned)kMaxPlayers)
        s_checkpointDeathTicks[nPlayer] = 0;
}

static void KexCheckpoint_RequestReload(int nPlayer)
{
    if (!g_kexMode || nTotalPlayers != 1 || s_checkpoint.pending
        || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return;

    // Disabling checkpoints is a remaster setting, not just a hidden pause
    // item. Drop a previously marked snapshot before death handling so the
    // reload really returns to this level's entrance state.
    if (!kex_enable_checkpoints)
        KexCheckpoint_ClearActive();
    if (s_checkpoint.valid && !KexCheckpoint_MapMatches())
        KexCheckpoint_Reset();
    if (!s_checkpoint.valid)
        Bstrncpyz(s_checkpoint.map, gUserMapFilename, sizeof(s_checkpoint.map));

    s_checkpoint.pending = 1;
    nPlayerLives[nPlayer] = max((short)1, nPlayerLives[nPlayer]);
    levelnew = levelnum;
    OSD_Printf("Kex death: reloading %s from %s\n",
               gUserMapFilename,
               s_checkpoint.valid ? "checkpoint" : "level entrance");
}

int KexCheckpoint_HasCheckpoint(void)
{
    return g_kexMode && kex_enable_checkpoints && nTotalPlayers == 1 && s_checkpoint.valid
        && !s_checkpoint.pending && KexCheckpoint_MapMatches();
}

int KexCheckpoint_Reload(int nPlayer, int bRestartLevel)
{
    if (!g_kexMode || nTotalPlayers != 1 || s_checkpoint.pending
        || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return 0;

    if (!bRestartLevel && !KexCheckpoint_HasCheckpoint())
        return 0;

    if (bRestartLevel)
    {
        // Keep s_checkpointEntry: kexGameLocal::RestartLevel restores the
        // persistent state held when this map was entered, not a fresh game.
        KexCheckpoint_ClearActive();
    }

    KexCheckpoint_RequestReload(nPlayer);
    return 1;
}

void KexCheckpoint_DeathTick(int nPlayer)
{
    if (!g_kexMode || nTotalPlayers != 1
        || (unsigned)nPlayer >= (unsigned)kMaxPlayers || s_checkpoint.pending)
        return;

    // Native waits 60 updates at 60 Hz. Exhumed's game simulation advances at
    // 30 Hz, so 30 player ticks preserve the same one-second death pause.
    if (++s_checkpointDeathTicks[nPlayer] >= 30)
        KexCheckpoint_RequestReload(nPlayer);
}

int KexCheckpoint_PrepareSpawn(int nPlayer)
{
    if (!g_kexMode || nTotalPlayers != 1 || !s_checkpoint.pending
        || !s_checkpoint.valid || nPlayer != nLocalPlayer)
        return 0;

    int16_t nSector = s_checkpoint.sector;
    if ((unsigned)nSector >= (unsigned)numsectors)
        updatesector(s_checkpoint.x, s_checkpoint.y, &nSector);
    if ((unsigned)nSector >= (unsigned)numsectors)
    {
        OSD_Printf(OSD_ERROR "Kex checkpoint %d has no valid sector; using level entrance\n",
                   s_checkpoint.id);
        s_checkpoint.valid = 0;
        return 0;
    }

    s_checkpoint.sector = nSector;
    SetSavePoint(nPlayer, s_checkpoint.x, s_checkpoint.y, s_checkpoint.z,
                 nSector, s_checkpoint.angle);
    return 1;
}

void KexCheckpoint_FinishSpawn(int nPlayer)
{
    if (!g_kexMode || nTotalPlayers != 1 || !s_checkpoint.pending
        || nPlayer != nLocalPlayer)
        return;

    if (s_checkpoint.valid)
        KexCheckpoint_RestorePlayer(nPlayer);
    else if (s_checkpointEntry.valid)
        KexCheckpoint_RestorePlayerState(nPlayer, s_checkpointEntry.player);
    s_checkpoint.pending = 0;
    s_checkpointDeathTicks[nPlayer] = 0;

    if (s_checkpointTestPhase == 1)
    {
        Player const &p = PlayerList[nPlayer];
        int const nPlayerSprite = p.nSprite;
        int const nGoneSprite = KexCheckpoint_FindActor(s_checkpointTestGoneId);
        int const nReturnSprite = KexCheckpoint_FindActor(s_checkpointTestReturnId);
        bool active = false;
        for (int nSprite = headspritestat[899]; nSprite >= 0; nSprite = nextspritestat[nSprite])
            if (sprite[nSprite].hitag == s_checkpoint.id)
            { active = true; break; }

        bool const ok = s_checkpoint.valid && nPlayerSprite >= 0 && active
            && s_checkpointTestGoneId >= 0 && nGoneSprite < 0
            && s_checkpointTestReturnId >= 0 && nReturnSprite >= 0
            && sprite[nPlayerSprite].x == s_checkpoint.x
            && sprite[nPlayerSprite].y == s_checkpoint.y
            && sprite[nPlayerSprite].sectnum == s_checkpoint.sector
            && sprite[nPlayerSprite].ang == s_checkpoint.angle
            && p.nHealth == min(317, KexInv_MaxHealth(nPlayer))
            && p.nMagic == 321
            && p.nAmmo[kWeaponPistol] == 17
            && KexInvList[nPlayer].map == 1
            && numwalls > 0 && wall[0].x == s_checkpointTestWallX;
        OSD_Printf("Kex checkpoint test: %s (id %d, active %d, actors %d gone/%d returned as sprite %d, player %d/%d sector %d, health %d magic %d pistol %d, world wall0 %d)\n",
                   ok ? "PASS" : "FAIL", s_checkpoint.id, active,
                   s_checkpointTestGoneId, s_checkpointTestReturnId, nReturnSprite,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].x : INT_MIN,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].y : INT_MIN,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].sectnum : -1,
                   p.nHealth, p.nMagic, p.nAmmo[kWeaponPistol],
                   numwalls > 0 ? (int)wall[0].x : INT_MIN);
        s_checkpointTestPhase = 2;
    }

    if (s_deathTestPhase == 1)
    {
        Player const &p = PlayerList[nPlayer];
        int const nPlayerSprite = p.nSprite;
        bool const persistentStateRestored =
            !Bmemcmp(&KexInvList[nPlayer], &s_deathTestInv, sizeof(KexInv))
            && nPlayerWeapons[nPlayer] == s_deathTestWeapons
            && p.nAmmo[kWeaponPistol] == s_deathTestPistolAmmo;
        bool const ok = !s_checkpoint.valid && nPlayerSprite >= 0
            && sprite[nPlayerSprite].x == s_deathTestX
            && sprite[nPlayerSprite].y == s_deathTestY
            && sprite[nPlayerSprite].sectnum == s_deathTestSector
            && p.nHealth == KexInv_MaxHealth(nPlayer)
            && numwalls > 0 && wall[0].x == s_deathTestWallX
            && persistentStateRestored
            && s_deathTestTicks >= 29 && s_deathTestTicks <= 30;
        OSD_Printf("Kex entrance death test: %s (delay %d ticks, player %d/%d sector %d, health %d, persistent %d, world wall0 %d)\n",
                   ok ? "PASS" : "FAIL", s_deathTestTicks,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].x : INT_MIN,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].y : INT_MIN,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].sectnum : -1,
                   p.nHealth, persistentStateRestored,
                   numwalls > 0 ? (int)wall[0].x : INT_MIN);
        s_deathTestPhase = 2;
    }
}

void KexInv_AddShrinePad(int nSprite, int nArtifact)
{
    if (g_kexMode && s_shrinePads < kKexMaxShrinePads
        && (unsigned)nArtifact < (unsigned)kKexArtiCount)
    {
        KexShrinePad &sh = s_shrinePad[s_shrinePads++];
        sh.padSector = sprite[nSprite].sectnum;
        sh.nArtifact = (int16_t)nArtifact;
    }
    mydeletesprite(nSprite);
}

void KexInv_AddShrineSpawn(int nSprite, int nArtifact)
{
    if (g_kexMode && s_shrineSpawns < kKexMaxShrineSpawns
        && (unsigned)nArtifact < (unsigned)kKexArtiCount)
    {
        KexShrineSpawn &sh = s_shrineSpawn[s_shrineSpawns++];
        sh.sector = sprite[nSprite].sectnum;
        sh.nArtifact = (int16_t)nArtifact;
        sh.picnum = sprite[nSprite].picnum;
        sh.x = sprite[nSprite].x; sh.y = sprite[nSprite].y; sh.z = sprite[nSprite].z;
    }
    mydeletesprite(nSprite);
}

static void KexInv_ShrineReveal(KexShrineSpawn const &sh)
{
    int const nSprite = insertsprite(sh.sector, 0);
    if (nSprite < 0)
        return;
    sprite[nSprite].x = sh.x; sprite[nSprite].y = sh.y; sprite[nSprite].z = sh.z;
    sprite[nSprite].picnum = sh.picnum;
    sprite[nSprite].cstat = 0;
    sprite[nSprite].shade = -64;
    sprite[nSprite].pal = 0;
    sprite[nSprite].xrepeat = sprite[nSprite].yrepeat = 64;
    sprite[nSprite].xoffset = sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = 0;
    sprite[nSprite].owner = -1;
    sprite[nSprite].lotag = sprite[nSprite].hitag = 0;
    sprite[nSprite].extra = -1;
    // Arti_Mask is still Build's Sobek Mask pickup; the other five use the
    // KEX-only item tag. Both pickup paths restore the same artifact bit.
    int const lotag = sh.nArtifact == kKexArtiMask ? 23 : kKexLotagArtifact;
    ProcessSpriteTag((short)nSprite, lotag, sh.nArtifact == kKexArtiMask ? 0 : sh.nArtifact);
    AddFlash(sh.sector, sh.x, sh.y, sh.z, 80);
    PlayFX2(StaticSound[44] | 0x2000, (short)nSprite);   // tele_1: the remaster's appear sound
    OSD_Printf("Kex shrine: artifact %d revealed in sector %d\n", sh.nArtifact, sh.sector);
}

static void KexInv_TakeArtifact(int nPlayer, int nArtifact)
{
    if ((unsigned)nPlayer >= (unsigned)kMaxPlayers
        || (unsigned)nArtifact >= (unsigned)kKexArtiCount)
        return;
    KexInvList[nPlayer].artifacts &= (uint8_t)~(1 << nArtifact);
}

static void KexInv_ShrineTick(void)
{
    if (!g_kexMode)
        return;
    for (int i = 0; i < s_shrinePads; i++)
    {
        KexShrinePad const &pad = s_shrinePad[i];
        for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
        {
            int const nSprite = PlayerList[nPlayer].nSprite;
            if (nSprite < 0 || !KexPlayer_OnSectorFloor(nPlayer, pad.padSector)
                || !KexInv_HasArtifact(nPlayer, pad.nArtifact))
                continue;

            // The native loop leaves the artifact alone if no type-54 event
            // shares this tag. Do the same before consuming it or firing the
            // remote channel.
            int nSpawn = 0;
            for (int j = 0; j < s_shrineSpawns; j++)
                nSpawn += s_shrineSpawn[j].nArtifact == pad.nArtifact;
            if (!nSpawn)
                continue;

            KexInv_TakeArtifact(nPlayer, pad.nArtifact);
            KexScript_ArtifactTaken(pad.nArtifact);
            for (int j = 0; j < s_shrineSpawns; j++)
            {
                KexShrineSpawn const &sh = s_shrineSpawn[j];
                if (sh.nArtifact == pad.nArtifact)
                    KexInv_ShrineReveal(sh);
            }
        }
    }
}

// VB_KEXSHRINE_TEST=<ticks>: force a player carrying the first loaded shrine
// artifact onto its pad, then assert the source bit was consumed and every
// event-54 destination materialised an item at its authored Kex position.
static void KexInv_ShrineTestPrepare(void)
{
    if (s_shrineTestPhase != 0 || !KexInv_TestCountdownElapsed(s_shrineSelftest))
        return;

    int const nPlayer = nLocalPlayer;
    int const nSprite = (unsigned)nPlayer < (unsigned)nTotalPlayers
        ? PlayerList[nPlayer].nSprite : -1;
    if (nSprite < 0 || !s_shrinePads || !s_shrineSpawns)
    {
        OSD_Printf("Kex shrine test: FAIL (player %d, pads %d, destinations %d)\n",
                   nSprite, s_shrinePads, s_shrineSpawns);
        s_shrineTestPhase = 2;
        return;
    }

    KexShrinePad const &pad = s_shrinePad[0];
    mychangespritesect(nSprite, pad.padSector);
    sprite[nSprite].z = getflorzofslope(pad.padSector,
                                        sprite[nSprite].x, sprite[nSprite].y);
    s_shrineTestArtifact = pad.nArtifact;
    s_shrineTestExpected = 0;
    for (int i = 0; i < s_shrineSpawns; i++)
        s_shrineTestExpected += s_shrineSpawn[i].nArtifact == pad.nArtifact;
    if (!s_shrineTestExpected)
    {
        OSD_Printf("Kex shrine test: FAIL (pad artifact %d has no destination)\n",
                   pad.nArtifact);
        s_shrineTestPhase = 2;
        return;
    }

    // Seed only the bit; Kex's pickup event must not be pre-fired by this
    // test. The following shrine tick is the path under test.
    KexInvList[nPlayer].artifacts |= (uint8_t)(1 << pad.nArtifact);
    s_shrineTestPad = pad.padSector;
    s_shrineTestPhase = 1;
    s_shrineSelftest = 0;
    OSD_Printf("Kex shrine test: forcing artifact %d into pad sector %d\n",
               pad.nArtifact, pad.padSector);
}

static void KexInv_ShrineTestCheck(void)
{
    if (s_shrineTestPhase != 1)
        return;

    int materialized = 0;
    int const itemStat = 900 + (s_shrineTestArtifact == kKexArtiMask
                                ? 23 : kKexLotagArtifact);
    for (int j = 0; j < s_shrineSpawns; j++)
    {
        KexShrineSpawn const &sh = s_shrineSpawn[j];
        if (sh.nArtifact != s_shrineTestArtifact)
            continue;
        for (int i = 0; i < kMaxSprites; i++)
        {
            if (sprite[i].statnum == itemStat && sprite[i].sectnum == sh.sector
                && sprite[i].x == sh.x && sprite[i].y == sh.y && sprite[i].z == sh.z)
            {
                materialized++;
                break;
            }
        }
    }
    int const nPlayer = nLocalPlayer;
    int const nSprite = PlayerList[nPlayer].nSprite;
    bool const ok = nSprite >= 0 && sprite[nSprite].sectnum == s_shrineTestPad
        && !KexInv_HasArtifact(nPlayer, s_shrineTestArtifact)
        && materialized == s_shrineTestExpected;
    OSD_Printf("Kex shrine test: %s (artifact %d consumed, destinations %d/%d, pad %d)\n",
               ok ? "PASS" : "FAIL", s_shrineTestArtifact,
               materialized, s_shrineTestExpected, s_shrineTestPad);
    s_shrineTestPhase = 2;
}

void KexInv_LevelStart(void)
{
    // Direct-map and unattended launches do not pass through the New Game
    // menu.  This test/developer override selects the same shared state before
    // RestartPlayer() asks for the difficulty-dependent health ceiling.
    if (g_kexMode)
    {
        char const *difficulty = Bgetenv("VB_KEX_DIFFICULTY");
        if (difficulty)
            KexDifficulty_Set(Batoi(difficulty));
        if (Bgetenv("VB_KEXOVERWORLD_TEST"))
            KexMenu_LevelCount();
    }

    // LoadObjects() has rebuilt pristine checkpoint actors. Put every pad
    // touched before death back into its activated visual state without
    // touching the newly reset enemies, pickups or level geometry.
    KexCheckpoint_ReactivateActors();

    for (int i = 0; i < kMaxPlayers; i++)
        KexInvList[i].map = 0;

    if (g_kexMode)
    {
        for (int i = 0; i < kMaxSprites; i++)
        {
            int const st = sprite[i].statnum;
            int const nWeapon = KexInv_WeaponForPickupLotag(st - 900);
            if (nWeapon >= 0 && (nPlayerWeapons[0] & (1 << nWeapon)))
            {
                // Every Item_Weapon actor has removeIfOwned. KEX applies it
                // while initializing the actor, before it can be touched.
                DestroyItemAnim(i);
                mydeletesprite(i);
                continue;
            }
        }
        if (s_shrinePads || s_shrineSpawns)
            OSD_Printf("Kex shrines: %d pads, %d destinations on this level\n",
                       s_shrinePads, s_shrineSpawns);
    }
    { char const *e = getenv("VB_KEXINV_TEST"); s_selftest = e ? Batoi(e) : 0; }
    { char const *e = getenv("VB_KEXPICKUP_TEST"); s_pickupSelftest = e ? Batoi(e) : 0; }
    { char const *e = getenv("VB_KEXDIFF_TEST"); s_difficultySelftest = e ? Batoi(e) : 0; }
    if (s_shrineTestPhase == 0)
    {
        char const *e = getenv("VB_KEXSHRINE_TEST");
        s_shrineSelftest = e ? Batoi(e) : 0;
    }
    if (!s_saveSelftestDone && Bgetenv("VB_KEXCAMPAIGNSAVE_TEST"))
    {
        s_saveSelftestDone = 1;
        KexInv_RunSaveSelftest();
    }
    if (s_travelTestPhase == 0)
    {
        char const *e = Bgetenv("VB_KEXTRAVEL_TEST");
        s_travelSelftest = e ? 20 : 0;
        s_travelTestTarget = e ? Batoi(e) : -1;
    }
    else if (s_travelTestPhase == 1)
        s_travelTestPostTicks = 2;
    if (s_menuLoadTestPhase == 0)
    {
        char const *e = Bgetenv("VB_KEXMENU_LOAD_TEST");
        s_menuLoadSelftest = e ? 20 : 0;
        s_menuLoadTestSlot = e ? clamp(Batoi(e), 0, KEX_INV_SAVE_SLOTS - 1) : -1;
    }
    if (s_deathTestPhase == 0)
    {
        char const *e = getenv("VB_KEXDEATH_TEST");
        s_deathSelftest = e ? Batoi(e) : 0;
    }
    if (s_checkpointTestPhase == 0)
    {
        char const *e = getenv("VB_KEXCHECKPOINT_TEST");
        s_checkpointSelftest = e ? Batoi(e) : 0;
    }
    s_wantScreen = 0;
}

void PlayerCheckItemPickup(int nPlayer, int nPickupSprite, int valueFlag);   // player.cpp
void StartDeathSeq(int nPlayer, int nVal);                                  // player.cpp

static void KexInv_RunTravelSelftest(void)
{
    char const *const sidecar = Bgetenv("VB_KEXINV_SAVE_FILE");
    char const *const slotText = Bgetenv("VB_KEXTRAVEL_TEST_SLOT");
    if (!sidecar || !sidecar[0] || !slotText)
    {
        OSD_Printf("Kex travel save test: FAIL (isolated VB_KEXINV_SAVE_FILE and VB_KEXTRAVEL_TEST_SLOT required)\n");
        s_travelTestPhase = 2;
        return;
    }

    int nTravel = -1;
    for (int i = 0; i < kMaxSprites; i++)
        if (sprite[i].statnum == 960 && sprite[i].hitag == s_travelTestTarget)
        {
            nTravel = i;
            break;
        }
    if (nTravel < 0)
    {
        OSD_Printf("Kex travel save test: FAIL (no lotag-60/hitag-%d actor in %s)\n",
                   s_travelTestTarget, gUserMapFilename);
        s_travelTestPhase = 2;
        return;
    }

    SavePosition = (short)clamp(Batoi(slotText), 0, KEX_INV_SAVE_SLOTS - 1);
    int const nPlayer = nLocalPlayer;
    int const fullArtifacts = (1 << kKexArtiCount) - 1;
    if (s_travelTestTarget == 1 || s_travelTestTarget == 22 || s_travelTestTarget == 24)
    {
        s_travelTestExpectedHealth = min(73, KexInv_MaxHealth(nPlayer));
        s_travelTestExpectedAmmo = 3;
        s_travelTestExpectedWeapon = kWeaponPistol;
        PlayerList[nPlayer].nHealth = (short)s_travelTestExpectedHealth;
        nPlayerWeapons[nPlayer] |= 1 << s_travelTestExpectedWeapon;
        SetNewWeaponImmediate((short)nPlayer, (short)s_travelTestExpectedWeapon);
        PlayerList[nPlayer].nLastWeapon = kWeaponSword;
        PlayerList[nPlayer].nAmmo[s_travelTestExpectedWeapon]
            = (short)s_travelTestExpectedAmmo;
        nPistolClip[nPlayer] = (short)s_travelTestExpectedAmmo;
    }
    PlayerCheckItemPickup(nPlayer, nTravel, 0);

    char mapUp[BMAX_PATH];
    Bstrncpyz(mapUp, gUserMapFilename, sizeof(mapUp));
    Bstrupr(mapUp);
    int ok = 0;
    if (s_travelTestTarget == 1)
        ok = levelnew == 0 && Bstrstr(mapUp, "KEX_TOMB.MAP");
    else if (s_travelTestTarget == 22 || s_travelTestTarget == 24)
        ok = levelnew == 0 && Bstrstr(mapUp, "KEX_TOMBEND.MAP");
    else if (s_travelTestTarget == 63)
        ok = levelnew > 99 && KexInvList[nPlayer].artifacts == fullArtifacts;

    OSD_Printf("Kex travel save test: transition %s (hitag %d, slot %d, destination %s, level sentinel %d, artifacts %02x)\n",
               ok ? "PASS" : "FAIL", s_travelTestTarget, SavePosition + 1,
               gUserMapFilename, levelnew, KexInvList[nPlayer].artifacts);
    s_travelTestPhase = ok ? (s_travelTestTarget == 63 ? 3 : 1) : 2;
}

static void KexInv_RunMenuLoadSelftest(void)
{
    char const *const classicSave = Bgetenv("VB_EXHUMED_SAVE_FILE");
    char const *const sidecar = Bgetenv("VB_KEXINV_SAVE_FILE");
    if (!classicSave || !classicSave[0] || !sidecar || !sidecar[0]
        || !Bstrcmp(sidecar, "kexinv.sav"))
    {
        OSD_Printf("Kex menu load test: FAIL (isolated VB_EXHUMED_SAVE_FILE and VB_KEXINV_SAVE_FILE required)\n");
        s_menuLoadTestPhase = 2;
        return;
    }

    Player const livePlayer = PlayerList[nLocalPlayer];
    KexInv const liveInv = KexInvList[nLocalPlayer];
    uint16_t const liveWeapons = nPlayerWeapons[nLocalPlayer];
    short const liveClip = nPlayerClip[nLocalPlayer];
    short const livePistolClip = nPistolClip[nLocalPlayer];
    short const liveItem = nPlayerItem[nLocalPlayer];
    short const liveLives = nPlayerLives[nLocalPlayer];
    int const liveDifficulty = KexDifficulty_Get();
    int const liveUserMap = bUserMap;
    uint8_t liveUnlocked[KEX_CAMPAIGN_MAX_MAPS];
    KexCampaignPlayerState liveCampaignPlayer[kMaxPlayers];
    Bmemcpy(liveUnlocked, s_kexUnlockedMaps, sizeof(liveUnlocked));
    Bmemcpy(liveCampaignPlayer, s_campaignPlayer,
            sizeof(liveCampaignPlayer));
    char liveMap[BMAX_PATH];
    Bstrncpyz(liveMap, gUserMapFilename, sizeof(liveMap));

    char expectedMap[BMAX_PATH] = "";
    int complete = 0;
    int const hadMappedSave = KexInv_SaveSlotInfo(s_menuLoadTestSlot, expectedMap,
                                                  sizeof(expectedMap), &complete);
    SavePosition = (short)s_menuLoadTestSlot;
    int const nMap = menu_GameLoad(SavePosition);
    int const loadedHealth = PlayerList[nLocalPlayer].nHealth;
    int const loadedAmmo = PlayerList[nLocalPlayer].nAmmo[kWeaponPistol];
    int const loadedWeapon = PlayerList[nLocalPlayer].nCurrentWeapon;
    int restoredHealth = loadedHealth;
    int restoredAmmo = loadedAmmo;
    int restoredWeapon = loadedWeapon;
    if (nMap > 0 && nMap <= 99)
    {
        PlayerList[nLocalPlayer].nHealth = (short)KexInv_MaxHealth(nLocalPlayer);
        PlayerList[nLocalPlayer].nAmmo[kWeaponPistol]
            = (short)KexInv_AmmoMax(kWeaponPistol);
        PlayerList[nLocalPlayer].nCurrentWeapon = kWeaponSword;
        KexCampaign_FinishSpawn(nLocalPlayer);
        restoredHealth = PlayerList[nLocalPlayer].nHealth;
        restoredAmmo = PlayerList[nLocalPlayer].nAmmo[kWeaponPistol];
        restoredWeapon = PlayerList[nLocalPlayer].nCurrentWeapon;
    }

    char expectedLeaf[BMAX_PATH];
    char const *leaf = expectedMap;
    for (char const *p = expectedMap; *p; p++)
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    Bstrncpyz(expectedLeaf, leaf, sizeof(expectedLeaf));

    char loadedLeaf[BMAX_PATH];
    leaf = gUserMapFilename;
    for (char const *p = gUserMapFilename; *p; p++)
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    Bstrncpyz(loadedLeaf, leaf, sizeof(loadedLeaf));

    int const ok = hadMappedSave && (complete ? nMap > 99
        : nMap > 0 && bUserMap && !Bstrcasecmp(expectedLeaf, loadedLeaf)
          && loadedHealth > 0 && restoredHealth == loadedHealth
          && restoredAmmo == loadedAmmo && restoredWeapon == loadedWeapon);
    OSD_Printf("Kex menu load test: %s (slot %d, sidecar map %s, selected %s, sentinel %d, health %d/%d, ammo %d/%d, weapon %d/%d, complete %d)\n",
               ok ? "PASS" : "FAIL", SavePosition + 1, expectedMap,
               bUserMap ? gUserMapFilename : "none", nMap, restoredHealth,
               loadedHealth, restoredAmmo, loadedAmmo, restoredWeapon,
               loadedWeapon, complete);
    s_menuLoadTestPhase = ok ? 1 : 2;

    // menu_GameLoad() is normally called between game loops. This probe runs
    // inside MoveThings(), so do not let the loaded slot's old sprite/run IDs
    // execute against the currently live map for the remainder of the tick.
    PlayerList[nLocalPlayer] = livePlayer;
    KexInvList[nLocalPlayer] = liveInv;
    nPlayerWeapons[nLocalPlayer] = liveWeapons;
    nPlayerClip[nLocalPlayer] = liveClip;
    nPistolClip[nLocalPlayer] = livePistolClip;
    nPlayerItem[nLocalPlayer] = liveItem;
    nPlayerLives[nLocalPlayer] = liveLives;
    KexDifficulty_Set(liveDifficulty);
    bUserMap = liveUserMap;
    Bstrncpyz(gUserMapFilename, liveMap, BMAX_PATH);
    Bmemcpy(s_kexUnlockedMaps, liveUnlocked, sizeof(liveUnlocked));
    Bmemcpy(s_campaignPlayer, liveCampaignPlayer,
            sizeof(liveCampaignPlayer));
    SetPlayerItem(nLocalPlayer, liveItem);
    SetHealthFrame(0);
    SetMagicFrame();
}

static int KexCheckpoint_FindActor(int nActorId)
{
    if ((unsigned)nActorId >= (unsigned)kMaxSprites)
        return -1;
    for (int nSprite = 0; nSprite < kMaxSprites; nSprite++)
        if (s_checkpointActorId[nSprite] == nActorId
            && sprite[nSprite].statnum != MAXSTATUS)
            return nSprite;
    return -1;
}

static bool KexCheckpoint_TakeTestPickup(int nPlayer, int nExcludeId, int16_t &nTakenId)
{
    // Full-health and full-ammo actors are ordinary one-shot KEX pickups and
    // carry no persistent inventory ownership rule of their own. That makes
    // them suitable probes: their post-reload presence is controlled solely
    // by the checkpoint's authored-actor removal bits.
    for (int nSprite = 0; nSprite < kMaxSprites; nSprite++)
    {
        int const nActorId = s_checkpointActorId[nSprite];
        int const nItem = sprite[nSprite].statnum - 900;
        if (nActorId < 0 || nActorId == nExcludeId
            || (nItem != kKexLotagFullHealth && nItem != kKexLotagFullAmmo))
            continue;

        PlayerList[nPlayer].nHealth = 1;
        for (int nWeapon = 0; nWeapon < kMaxWeapons; nWeapon++)
            PlayerList[nPlayer].nAmmo[nWeapon] = 0;
        PlayerCheckItemPickup(nPlayer, nSprite, 0);
        if (sprite[nSprite].statnum == MAXSTATUS
            && KexCheckpoint_Bit(s_checkpointRemoved, nActorId))
        {
            nTakenId = (int16_t)nActorId;
            return true;
        }
    }
    return false;
}

static bool KexInv_TestWeaponPickup(int nPlayer, int nItem, int nWeapon)
{
    int const nPlayerSprite = PlayerList[nPlayer].nSprite;
    if (nPlayerSprite < 0)
        return false;

    int const nSprite = insertsprite(sprite[nPlayerSprite].sectnum, 900 + nItem);
    if (nSprite < 0)
        return false;
    sprite[nSprite].x = sprite[nPlayerSprite].x;
    sprite[nSprite].y = sprite[nPlayerSprite].y;
    sprite[nSprite].z = sprite[nPlayerSprite].z;
    sprite[nSprite].owner = -1;
    sprite[nSprite].hitag = 0;

    PlayerCheckItemPickup(nPlayer, nSprite, 0);
    return sprite[nSprite].statnum == MAXSTATUS
        && (nPlayerWeapons[nPlayer] & (1 << nWeapon))
        && PlayerList[nPlayer].nAmmo[nWeapon] == KexInv_AmmoMax(nWeapon);
}

extern int KexManacle_LastShot;    // gun.cpp
static int s_manacleTest;

int KexInv_TestFire(void) { return s_manacleTest > 20; }
int KexInv_TestAir(void)  { return s_manacleTest > 0; }

static void KexInv_RunDifficultySelftest(void)
{
    int const difficulty = KexDifficulty_Get();
    int const expected255 = difficulty == kKexDifficultyPharaoh ? 127 : 255;
    int const expected24 = difficulty == kKexDifficultyPharaoh ? 12 : 24;
    bool ok = KexDifficulty_PainChance(-1) == 0
        && KexDifficulty_PainChance(24) == expected24
        && KexDifficulty_PainChance(300) == expected255;

    int const expectedMin = difficulty == kKexDifficultyPharaoh ? 4
        : difficulty == kKexDifficultyHard ? 8 : 16;
    int const expectedMax = difficulty == kKexDifficultyPharaoh ? 7 : 31;
    int observedMin = INT_MAX, observedMax = INT_MIN;
    bool sawHardShort = false, sawHardLong = false;
    for (int i = 0; i < 4096; i++)
    {
        int const delay = KexDifficulty_AICooldown();
        observedMin = min(observedMin, delay);
        observedMax = max(observedMax, delay);
        sawHardShort |= delay <= 15;
        sawHardLong |= delay >= 16;
        ok = ok && delay >= expectedMin && delay <= expectedMax;
    }
    if (difficulty == kKexDifficultyHard)
        ok = ok && sawHardShort && sawHardLong;

    int nTestSprite = -1;
    int const nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
    if (nPlayerSprite >= 0)
        nTestSprite = insertsprite(sprite[nPlayerSprite].sectnum, 99);
    int scaledMove = -1;
    if (nTestSprite >= 0)
    {
        scaledMove = KexScaleCreatureMove(nTestSprite, 1000);
        mydeletesprite(nTestSprite);
    }
    int const expectedMove = difficulty == kKexDifficultyPharaoh ? 1100 : 1000;
    ok = ok && scaledMove == expectedMove;
    if (nPlayerSprite >= 0)
        ok = ok && KexScaleCreatureMove(nPlayerSprite, 1000) == 1000;

    OSD_Printf("Kex difficulty test: %s (difficulty %d, pain 24->%d 255->%d, cooldown observed %d..%d expected %d..%d, creature move 1000->%d)\n",
               ok ? "PASS" : "FAIL", difficulty,
               KexDifficulty_PainChance(24), KexDifficulty_PainChance(255),
               observedMin, observedMax, expectedMin, expectedMax, scaledMove);
}

static void KexCheckpoint_RunSelftest(void)
{
    int const nPlayer = nLocalPlayer;
    int const nPlayerSprite = PlayerList[nPlayer].nSprite;
    int const nCheckpoint = headspritestat[959];
    if (nPlayerSprite < 0 || nCheckpoint < 0 || numwalls <= 0)
    {
        OSD_Printf("Kex checkpoint test: FAIL (player %d, checkpoint %d, walls %d)\n",
                   nPlayerSprite, nCheckpoint, numwalls);
        s_checkpointTestPhase = 2;
        return;
    }

    // A checkpoint is not an authored-map reset: preserve geometry and an
    // actor removal made before the mark, then prove later changes roll back.
    s_checkpointTestGoneId = -1;
    s_checkpointTestReturnId = -1;
    if (!KexCheckpoint_TakeTestPickup(nPlayer, -1, s_checkpointTestGoneId))
    {
        OSD_Printf("Kex checkpoint test: FAIL (no authored pre-checkpoint pickup probe)\n");
        s_checkpointTestPhase = 2;
        return;
    }
    wall[0].x += 111;
    PlayerList[nPlayer].nHealth = (short)min(317, KexInv_MaxHealth(nPlayer));
    PlayerList[nPlayer].nMagic = 321;
    PlayerList[nPlayer].nAmmo[kWeaponPistol] = 17;
    KexInvList[nPlayer].map = 1;
    PlayerCheckItemPickup(nPlayer, nCheckpoint, 0);
    if (!s_checkpoint.valid || sprite[nCheckpoint].statnum != 899)
    {
        OSD_Printf("Kex checkpoint test: FAIL (could not touch sprite %d)\n", nCheckpoint);
        s_checkpointTestPhase = 2;
        return;
    }

    if (!KexCheckpoint_TakeTestPickup(nPlayer, s_checkpointTestGoneId,
                                      s_checkpointTestReturnId))
    {
        OSD_Printf("Kex checkpoint test: FAIL (no authored post-checkpoint pickup probe)\n");
        s_checkpointTestPhase = 2;
        return;
    }

    s_checkpointTestWallX = wall[0].x;
    PlayerList[nPlayer].nHealth = 1;
    PlayerList[nPlayer].nMagic = 0;
    PlayerList[nPlayer].nAmmo[kWeaponPistol] = 0;
    KexInvList[nPlayer].map = 0;
    wall[0].x += 12345;
    s_checkpointTestPhase = 1;
    s_checkpointSelftest = 0;
    KexCheckpoint_RequestReload(nPlayer);
}

void KexInv_Tick(void)
{
    // GameMove runs once per 4 of the engine's 120 Hz ticks.  Unlike
    // totalclock it is not called by paused/menu presentation, and unlike a
    // wall-clock timer it cannot charge slow frames twice.  This mirrors the
    // remaster's gameplay play-time counter across map changes and deaths.
    if (g_kexMode && levelnew < 0)
        s_kexPlayTimeTicks += 4;

    KexInv_ShrineTestPrepare();
    KexInv_ShrineTick();
    KexInv_ShrineTestCheck();
    if (s_travelTestPhase == 0 && KexInv_TestCountdownElapsed(s_travelSelftest))
        KexInv_RunTravelSelftest();
    if (s_travelTestPhase == 1 && s_travelTestPostTicks > 0
        && --s_travelTestPostTicks == 0)
    {
        char mapUp[BMAX_PATH];
        Bstrncpyz(mapUp, gUserMapFilename, sizeof(mapUp));
        Bstrupr(mapUp);
        int const mapOK = s_travelTestTarget == 1
            ? Bstrstr(mapUp, "KEX_TOMB.MAP") != NULL
            : Bstrstr(mapUp, "KEX_TOMBEND.MAP") != NULL;
        int const healthOK = PlayerList[nLocalPlayer].nHealth
            == s_travelTestExpectedHealth;
        int const ammoOK = PlayerList[nLocalPlayer].nAmmo[s_travelTestExpectedWeapon]
            == s_travelTestExpectedAmmo;
        int const weaponOK = PlayerList[nLocalPlayer].nCurrentWeapon
            == s_travelTestExpectedWeapon;
        int const nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
        // Both fixed-map travel callbacks have real authored positions, not
        // merely destination maps. Accepting their map starts here would hide
        // a broken source-position conversion.
        int const fixedTravel = s_travelTestTarget == 1
            || s_travelTestTarget == 22 || s_travelTestTarget == 24;
        int const expectedSector = s_travelTestTarget == 1 ? 5 : 0;
        int const positionOK = !fixedTravel
            || ((unsigned)nPlayerSprite < (unsigned)kMaxSprites
                && sprite[nPlayerSprite].x == 0 && sprite[nPlayerSprite].y == -16896
                && sprite[nPlayerSprite].sectnum == expectedSector);
        int const ok = mapOK && healthOK && ammoOK && weaponOK && positionOK;
        OSD_Printf("Kex travel save test: post-spawn %s (destination %s, health %d/%d, ammo %d/%d, weapon %d/%d, warp %d/%d sector %d %s)\n",
                   ok ? "PASS" : "FAIL", gUserMapFilename,
                   PlayerList[nLocalPlayer].nHealth, s_travelTestExpectedHealth,
                   PlayerList[nLocalPlayer].nAmmo[s_travelTestExpectedWeapon],
                   s_travelTestExpectedAmmo, PlayerList[nLocalPlayer].nCurrentWeapon,
                   s_travelTestExpectedWeapon,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].x : INT_MIN,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].y : INT_MIN,
                   nPlayerSprite >= 0 ? (int)sprite[nPlayerSprite].sectnum : -1,
                   positionOK ? "authored" : "wrong");
        s_travelTestPhase = ok ? 3 : 2;
    }
    if (s_menuLoadTestPhase == 0 && KexInv_TestCountdownElapsed(s_menuLoadSelftest))
        KexInv_RunMenuLoadSelftest();
    if (s_deathTestPhase == 0 && KexInv_TestCountdownElapsed(s_deathSelftest))
    {
        int const nPlayer = nLocalPlayer;
        int const nPlayerSprite = PlayerList[nPlayer].nSprite;
        if (nPlayerSprite < 0 || numwalls <= 0 || s_checkpoint.valid)
        {
            OSD_Printf("Kex entrance death test: FAIL (player %d, walls %d, checkpoint %d)\n",
                       nPlayerSprite, numwalls, s_checkpoint.valid);
            s_deathTestPhase = 2;
        }
        else
        {
            s_deathTestX = sprite[nPlayerSprite].x;
            s_deathTestY = sprite[nPlayerSprite].y;
            s_deathTestSector = sprite[nPlayerSprite].sectnum;
            s_deathTestWallX = wall[0].x;
            s_deathTestInv = KexInvList[nPlayer];
            s_deathTestWeapons = nPlayerWeapons[nPlayer];
            s_deathTestPistolAmmo = PlayerList[nPlayer].nAmmo[kWeaponPistol];
            wall[0].x += 7777;
            // The reload must discard state earned between map entry and
            // death. Deliberately alter all three persistent containers so
            // the post-spawn assertion catches an entry snapshot regression.
            KexInvList[nPlayer].artifacts ^= 0x3f;
            nPlayerWeapons[nPlayer] ^= (1 << kWeaponManacle);
            PlayerList[nPlayer].nAmmo[kWeaponPistol] ^= 0x55;
            s_deathTestTicks = 0;
            s_deathTestPhase = 1;
            StartDeathSeq(nPlayer, 0);
        }
    }
    else if (s_deathTestPhase == 1)
        s_deathTestTicks++;
    if (s_checkpointTestPhase == 0 && KexInv_TestCountdownElapsed(s_checkpointSelftest))
        KexCheckpoint_RunSelftest();
    if (KexInv_TestCountdownElapsed(s_difficultySelftest))
        KexInv_RunDifficultySelftest();
    if (s_manacleTest > 0)
    {
        // Hold fire for 60 ticks (five charge steps), then release; report.
        s_manacleTest--;
        if (s_manacleTest == 24 || s_manacleTest == 12)
            videoCaptureScreen("cpshot0000.png", 0);   // charging, then the release
        if (s_manacleTest == 1)
            OSD_Printf("Kex manacle test: weapon %d state %d, last release fired %d bolts, ammo %d\n",
                       PlayerList[nLocalPlayer].nCurrentWeapon, PlayerList[nLocalPlayer].nWeaponState,
                       KexManacle_LastShot, PlayerList[nLocalPlayer].nAmmo[kWeaponManacle]);
    }
    if (KexInv_TestCountdownElapsed(s_pickupSelftest))
    {
        int const p = nLocalPlayer;
        bool initialAmmo = nPlayerWeapons[p] == (1 << kWeaponSword);
        for (int i = 0; i < kMaxWeapons; i++)
            initialAmmo = initialAmmo && PlayerList[p].nAmmo[i] == KexInv_AmmoMax(i);

        bool const pistolPickup = KexInv_TestWeaponPickup(p, 28, kWeaponPistol);
        bool const bombPickup = KexInv_TestWeaponPickup(p, 9, kWeaponGrenade);
        int const selected = PlayerList[p].nNewWeapon;
        bool const duplicateRemoved = KexInv_TestWeaponPickup(p, 28, kWeaponPistol)
            && PlayerList[p].nNewWeapon == selected;

        PlayerList[p].nHealth = 0;
        PlayerList[p].nCurrentWeapon = kWeaponPistol;
        for (int i = 0; i < kMaxWeapons; i++)
            PlayerList[p].nAmmo[i] = 0;
        nPlayerDouble[p] = 0;

        int const h1 = KexInv_PickupHealth(p, 20);
        int const healthSmall = PlayerList[p].nHealth;
        int const h2 = KexInv_PickupHealth(p, 50);
        int const healthLarge = PlayerList[p].nHealth;
        int const a1 = KexInv_PickupAmmo(p, 1);
        int const ammoSmall = PlayerList[p].nAmmo[kWeaponPistol];
        int const a2 = KexInv_PickupAmmo(p, 2);
        int const ammoLarge = PlayerList[p].nAmmo[kWeaponPistol];
        int const hf = KexInv_PickupFullHealth(p);
        int const baseMaxHealth = KexInv_MaxHealth(p);
        int const fullHealth = PlayerList[p].nHealth;
        int const af = KexInv_PickupFullAmmo(p);
        int const pw = KexInv_PickupPower(p);
        for (int i = 1; i <= 5; i++)
            KexInv_Pickup(p, kKexLotagAnkh, i);
        int const upgradedMaxHealth = KexInv_MaxHealth(p);
        KexInv_Pickup(p, kKexLotagAnkh, 1);  // a duplicate mask bit must not raise it again
        bool const ankhUpgrade = upgradedMaxHealth == baseMaxHealth * 6
            && KexInv_MaxHealth(p) == upgradedMaxHealth
            && PlayerList[p].nHealth == upgradedMaxHealth;
        int const healthScale = KexDifficulty_Get() == kKexDifficultyNormal ? 4 : 2;
        bool const ok = initialAmmo && pistolPickup && bombPickup && duplicateRemoved
            && selected == kWeaponGrenade && h1 && h2 && a1 && a2 && hf && af && pw && ankhUpgrade
            && healthSmall == 20 * healthScale && healthLarge == 70 * healthScale
            && fullHealth == baseMaxHealth
            && ammoSmall == 6 && ammoLarge == 19
            && PlayerList[p].nAmmo[kWeaponPistol] == KexInv_AmmoMax(kWeaponPistol)
            && PlayerList[p].nAmmo[kWeaponM60] == KexInv_AmmoMax(kWeaponM60)
            && PlayerList[p].nAmmo[kWeaponManacle] == KexInv_AmmoMax(kWeaponManacle)
            && nPlayerDouble[p] == KEX_POWER_DROP_HITAG;
        OSD_Printf("Kex pickup test: %s (difficulty %d, initial hidden ammo %s, weapon unlock pistol/bomb/duplicate %d/%d/%d, health %d/%d/full %d, ankhs %d->%d, pistol %d/%d/full %d, M60 %d, Manacle %d, boost %d)\n",
                   ok ? "PASS" : "FAIL", KexDifficulty_Get(),
                   initialAmmo ? "full" : "wrong",
                   pistolPickup, bombPickup, duplicateRemoved,
                   healthSmall, healthLarge, fullHealth, baseMaxHealth, upgradedMaxHealth,
                   ammoSmall, ammoLarge, PlayerList[p].nAmmo[kWeaponPistol],
                   PlayerList[p].nAmmo[kWeaponM60], PlayerList[p].nAmmo[kWeaponManacle],
                   nPlayerDouble[p]);
    }
    if (!KexInv_TestCountdownElapsed(s_selftest))
        return;
    int taken = 0;
    for (int i = 0; i < kMaxSprites; i++)
    {
        int const st = sprite[i].statnum;
        if (st != 900 + kKexLotagArtifact && st != 900 + kKexLotagTransmitter
            && st != 900 + kKexLotagDoll && st != 900 + kKexLotagAnkh)
            continue;
        PlayerCheckItemPickup(nLocalPlayer, i, 0);
        taken++;
    }
    // And the Manacle: equip it, hold fire for a while, let go.
    nPlayerWeapons[nLocalPlayer] |= 1 << kWeaponManacle;
    PlayerList[nLocalPlayer].nAmmo[kWeaponManacle] = 6;
    SetNewWeaponImmediate(nLocalPlayer, kWeaponManacle);
    s_manacleTest = 80;
    // Every doll, so both reward badges are on the HUD in the captures.
    KexInvList[nLocalPlayer].dolls = (1u << KEX_DOLL_COUNT) - 1;
    KexInv const &inv = KexInvList[nLocalPlayer];
    OSD_Printf("Kex inventory: %d pickups taken -> artifacts %02x transmitter %02x dolls %d ankhs %02x (max health %d, health %d)\n",
               taken, inv.artifacts, inv.transmitter, KexInv_DollCount(nLocalPlayer), inv.ankhs,
               KexInv_MaxHealth(nLocalPlayer), PlayerList[nLocalPlayer].nHealth);
    s_wantScreen = 1;
}

int KexInv_TestWantsScreen(void)
{
    int const r = s_wantScreen;
    s_wantScreen = 0;
    return r;
}

int KexInv_TestActive(void) { return getenv("VB_KEXINV_TEST") != NULL; }

void KexInv_GiveArtifact(int nPlayer, int nArtifact)
{
    if ((unsigned)nPlayer < (unsigned)kMaxPlayers && (unsigned)nArtifact < (unsigned)kKexArtiCount)
    {
        int const had = (KexInvList[nPlayer].artifacts >> nArtifact) & 1;
        KexInvList[nPlayer].artifacts |= (uint8_t)(1 << nArtifact);
        if (!had)
        {
            KexScript_ArtifactTaken(nArtifact);   // opens what the artifact opens (tag 1000 + index)
        }
    }
}

int KexInv_HasArtifact(int nPlayer, int nArtifact)
{
    return (unsigned)nPlayer < (unsigned)kMaxPlayers
        && (KexInvList[nPlayer].artifacts >> nArtifact) & 1;
}

static int KexInv_Bits(uint32_t v)
{
    int n = 0;
    for (; v; v >>= 1)
        n += v & 1;
    return n;
}

int KexInv_AnkhCount(int nPlayer)
{
    return (unsigned)nPlayer < (unsigned)kMaxPlayers ? KexInv_Bits(KexInvList[nPlayer].ankhs) : 0;
}

int KexInv_DollCount(int nPlayer)  { return KexInv_Bits(KexInvList[nPlayer].dolls); }
int KexInv_TransCount(int nPlayer) { return KexInv_Bits(KexInvList[nPlayer].transmitter); }

// kexSlavePlayer::GetMaxHealth is (healthUpgradeCount + 1) * base health.
// The remaster's base is 200 on Normal and 100 on Hard/Pharaoh; Build stores
// health at four internal units per KEX point.
int KexInv_MaxHealth(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return kMaxHealth;
    int const base = KexDifficulty_Get() == kKexDifficultyNormal ? kMaxHealth : kMaxHealth / 2;
    return base * (KexInv_AnkhCount(nPlayer) + 1);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static char *s_text;
static int s_textLanguage = -1;

static void KexInv_LoadText(void)
{
    int const language = KexLanguage_Index();
    if (s_text && s_textLanguage == language)
        return;

    if (s_text)
    {
        Xfree(s_text);
        s_text = NULL;
    }
    s_textLanguage = language;

    char filename[32];
    if (language == 0)
        Bstrcpy(filename, "kexinv.txt");
    else
        Bsnprintf(filename, sizeof(filename), "kexinv_%s.txt", KexLanguage_Code());
    int fil = kopen4loadfrommod(filename, 0);
    // Old conversion directories have only English metadata.  Keep them
    // usable when a language changes instead of making their UI blank.
    if (fil == -1 && language != 0)
        fil = kopen4loadfrommod("kexinv.txt", 0);
    int len = fil != -1 ? kfilelength(fil) : 0;
    s_text = (char *)Xcalloc(1, len + 1);
    if (fil != -1)
    {
        kread(fil, s_text, len);
        kclose(fil);
    }
    s_text[len] = 0;
}

char const *KexInv_Text(char const *key)
{
    // A menu frame often asks for several translated strings before it draws
    // any of them (for example, a label and its current value).  A single
    // scratch string made every previously returned pointer alias the latest
    // lookup, turning "WINDOW MODE: WINDOWED" into "WINDOW MODE: WINDOW
    // MODE".  Keep a modest ring so all of the live strings in one draw pass
    // retain their own text without allocating in the menu loop.
    static char s_result[32][256];
    static unsigned int s_resultIndex;
    char * const result = s_result[s_resultIndex++ % ARRAY_SIZE(s_result)];
    KexInv_LoadText();

    int const klen = (int)Bstrlen(key);
    for (char const *p = s_text; *p; )
    {
        char const *eol = Bstrchr(p, '\n');
        int const linelen = eol ? (int)(eol - p) : (int)Bstrlen(p);
        if (linelen > klen && p[klen] == ' ' && !Bstrncmp(p, key, klen))
        {
            int n = min(linelen - klen - 1, (int)sizeof(s_result[0]) - 1);
            Bmemcpy(result, p + klen + 1, n);
            while (n > 0 && (result[n-1] == '\r' || result[n-1] == ' '))
                n--;
            result[n] = 0;
            return result;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return NULL;
}

void KexInv_Format(char *out, int outSize, char const *key, int num)
{
    char const *src = KexInv_Text(key);
    if (!src)
    {
        Bstrncpyz(out, key, outSize);
        return;
    }
    int o = 0;
    for (char const *p = src; *p && o < outSize - 1; )
    {
        if (p[0] == '\\' && p[1] == 'n')
        {
            out[o++] = '\n'; p += 2;
        }
        else if (!Bstrncmp(p, "${0}", 4))
        {
            o += Bsnprintf(out + o, outSize - o, "%d", num); p += 4;
        }
        else
            out[o++] = *p++;
    }
    out[min(o, outSize - 1)] = 0;
}

// ---------------------------------------------------------------------------
// Pickups
// ---------------------------------------------------------------------------
// The remaster stacks pickup captions at the top of the screen, newest at
// the bottom, up to four at once, each in the gold menu font and pulsing
// slowly until it times out. KexHud_Draw() draws the stack; this keeps it.
#define KEX_CAPTION_LINES 4
#define KEX_CAPTION_TICKS 90          // three seconds at the game's 30 Hz
struct KexCaption { char text[64]; int ticks; };
static KexCaption s_caption[KEX_CAPTION_LINES];

int KexInv_CaptionLine(int i, char const **text, int *ticks)
{
    if ((unsigned)i >= KEX_CAPTION_LINES || s_caption[i].ticks <= 0)
        return 0;
    *text = s_caption[i].text; *ticks = s_caption[i].ticks;
    return 1;
}

void KexInv_CaptionTick(void)
{
    for (int i = 0; i < KEX_CAPTION_LINES; i++)
        if (s_caption[i].ticks > 0 && --s_caption[i].ticks == 0)
        {
            // close the gap so the stack stays packed from the top
            for (int j = i; j + 1 < KEX_CAPTION_LINES; j++) s_caption[j] = s_caption[j + 1];
            s_caption[KEX_CAPTION_LINES - 1].ticks = 0;
            i--;
        }
}

static void KexInv_Caption(int nPlayer, char const *key, int num)
{
    if (nPlayer != nLocalPlayer)
        return;
    char buf[256];
    KexInv_Format(buf, sizeof(buf), key, num);
    // The caption is the first line: the descriptions' second line has to wait
    // for the status screen.
    for (char *q = buf; *q; q++)
        if (*q == '\n') { *q = 0; break; }
    if (!buf[0])
        return;
    int slot = 0;
    while (slot < KEX_CAPTION_LINES && s_caption[slot].ticks > 0) slot++;
    if (slot == KEX_CAPTION_LINES)          // full: the oldest goes
    {
        for (int j = 0; j + 1 < KEX_CAPTION_LINES; j++) s_caption[j] = s_caption[j + 1];
        slot = KEX_CAPTION_LINES - 1;
    }
    Bstrncpyz(s_caption[slot].text, buf, sizeof(s_caption[slot].text));
    s_caption[slot].ticks = KEX_CAPTION_TICKS;
}

int KexInv_Pickup(int nPlayer, int nItem, int nHitag)
{
    if ((unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return 0;
    KexInv &inv = KexInvList[nPlayer];
    char key[32];

    switch (nItem)
    {
        case kKexLotagArtifact:
            if ((unsigned)nHitag >= (unsigned)kKexArtiCount)
                return 0;
            KexInv_GiveArtifact(nPlayer, nHitag);      // and fires what it opens
            Bsprintf(key, "artiname%d", nHitag);
            KexInv_Caption(nPlayer, key, 0);
            return 1;

        case kKexLotagTransmitter:
            { int const s = KexSound_Get("k_trans"); if (s >= 0) PlayFX2(s, PlayerList[nPlayer].nSprite); }   // om_transmitter.wav
            if ((unsigned)nHitag >= KEX_TRANS_PIECES)
                return 0;
            inv.transmitter |= (uint8_t)(1 << nHitag);
            {
                int const n = KexInv_TransCount(nPlayer);
                KexInv_Caption(nPlayer, n >= KEX_TRANS_PIECES ? "transfull" : "transn", n);
            }
            return 1;

        case kKexLotagDoll:
            if ((unsigned)nHitag >= KEX_DOLL_COUNT)
                return 0;
            inv.dolls |= 1u << nHitag;
            Bsprintf(key, "dollname%d", nHitag);
            KexInv_Caption(nPlayer, key, 0);
            return 1;

        case kKexLotagAnkh:
            if (nHitag < 1 || nHitag > 5)
                return 0;
            // Raises the ceiling (KexInv_MaxHealth) and fills up to it.
            inv.ankhs |= (uint8_t)(1 << nHitag);
            PlayerList[nPlayer].nHealth = KexInv_MaxHealth(nPlayer);
            if (nPlayer == nLocalPlayer)
                SetHealthFrame(1);
            KexInv_Caption(nPlayer, "maxhp", 0);
            return 1;
    }
    return 0;
}

int KexInv_PickupPower(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return 0;

    // kexSlavePickupPower writes Item_Power's giveAmount (2400) to the
    // player's weapon-power timer. DOS PowerSlave already has precisely that
    // timer: nPlayerDouble doubles the firing animation/rate and damage, is
    // saved with the player, and warns shortly before it expires.
    nPlayerDouble[nPlayer] = KEX_POWER_DROP_HITAG;
    return 1;
}

// The remaster's weaponInfo.txt caps, reordered to Build's internal weapon
// order (KEX has bombs before the flame thrower). Keeping these caps in KEX
// mode is important: the ordinary orb is proportional to the weapon's cap.
static int const s_kexAmmoMax[kMaxWeapons] = { 0, 60, 38, 65, 15, 16, 250, 6 };

int KexInv_AmmoMax(int nAmmoType)
{
    if ((unsigned)nAmmoType >= (unsigned)kMaxWeapons)
        return 300;
    return s_kexAmmoMax[nAmmoType];
}

int KexInv_PickupAmmo(int nPlayer, int nScale)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return 0;

    int const nWeapon = PlayerList[nPlayer].nCurrentWeapon;
    if ((unsigned)nWeapon >= (unsigned)kMaxWeapons || !WeaponInfo[nWeapon].bUsesAmmo)
        return 0;
    int const nAmmoType = WeaponInfo[nWeapon].nAmmoType;
    int const nMax = KexInv_AmmoMax(nAmmoType);
    if (PlayerList[nPlayer].nAmmo[nAmmoType] >= nMax)
        return 0;

    // kexSlavePickupAmmo computes floor(maxAmmo * actorScale / multiplier),
    // with multiplier 9.0 in Item_Ammo and a minimum result of one.
    int const amount = max(1, nMax * max(nScale, 1) / 9);
    int const changed = AddAmmo(nPlayer, nAmmoType, amount);
    if (changed && nAmmoType == kWeaponM60)
        CheckClip(nPlayer);
    return changed;
}

int KexInv_PickupHealth(int nPlayer, int nAmount)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers || nAmount <= 0
        || PlayerList[nPlayer].nHealth >= KexInv_MaxHealth(nPlayer))
        return 0;

    // Item_Health gives 20 KEX points; a scale-2 orb gives 50. Native halves
    // healing on both Hard and Pharaoh. Build uses four internal health units
    // per KEX point, hence multipliers four and two respectively.
    int const scale = KexDifficulty_Get() == kKexDifficultyNormal ? 4 : 2;
    PlayerList[nPlayer].nHealth = min(KexInv_MaxHealth(nPlayer),
                                     PlayerList[nPlayer].nHealth + nAmount * scale);
    if (nPlayer == nLocalPlayer)
        SetHealthFrame(1);
    return 1;
}

int KexInv_PickupFullHealth(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers
        || PlayerList[nPlayer].nHealth >= KexInv_MaxHealth(nPlayer))
        return 0;
    PlayerList[nPlayer].nHealth = KexInv_MaxHealth(nPlayer);
    if (nPlayer == nLocalPlayer)
        SetHealthFrame(1);
    return 1;
}

int KexInv_PickupFullAmmo(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return 0;

    // fullAmmo=1 loops over all eight KEX weapon slots. The native pickup is
    // consumed even when every slot is already full, so this returns success
    // unconditionally after applying the caps.
    for (int nWeapon = kWeaponPistol; nWeapon <= kWeaponManacle; nWeapon++)
    {
        if (!WeaponInfo[nWeapon].bUsesAmmo)
            continue;
        int const nAmmoType = WeaponInfo[nWeapon].nAmmoType;
        AddAmmo(nPlayer, nAmmoType, KexInv_AmmoMax(nAmmoType));
    }
    CheckClip(nPlayer);
    return 1;
}

// ---------------------------------------------------------------------------
// Save slots: a file beside savgamea.sav, one record per slot, so the classic
// save layout (which the loader seeks into by fixed size) is untouched.
// ---------------------------------------------------------------------------
#define KEX_INV_SAVE_FILE   "kexinv.sav"
static char const s_saveMagicV1[8] = "KEXINV1";
static char const s_saveMagicV2[8] = "KEXINV2";
static char const s_saveMagicV3[8] = "KEXINV3";
static char const s_saveMagicV4[8] = "KEXINV4";
static char const s_saveMagicV5[8] = "KEXINV5";

enum KexInvSaveFlags
{
    kKexSaveValid        = 1,
    kKexSaveComplete     = 2,
    kKexSaveLegacyNoMap  = 4,
};

// KEXINV2 stored only inventory and difficulty. Keep its exact layout here so
// existing campaign slots can be migrated when V3 is first written.
struct KexInvSaveRecordV2
{
    KexInv inv;
    uint8_t difficulty;
    uint8_t reserved[3];
};

// Every remaster level is loaded through the user-map path. The classic save's
// one-byte numeric map is therefore not enough to identify it; V3 added the
// actual map and whether mapDestination -2 completed the campaign. Keep its
// exact layout for migration.
struct KexInvSaveRecordV3
{
    KexInv inv;
    uint8_t difficulty;
    uint8_t flags;
    uint8_t reserved[2];
    char map[BMAX_PATH];
};

// V4 additionally carries the remaster's map-unlock vector. The native save
// writes one byte per map; the sidecar reserves a future-safe fixed ceiling
// and records how many bytes are meaningful.
struct KexInvSaveRecordV4
{
    KexInv inv;
    uint8_t difficulty;
    uint8_t flags;
    uint8_t unlockedCount;
    uint8_t reserved;
    char map[BMAX_PATH];
    uint8_t unlocked[KEX_CAMPAIGN_MAX_MAPS];
};

// V5 extends the V4 campaign graph with actual active-play time.  Keep V4's
// exact record separate so an existing sidecar is migrated rather than being
// misread with this platform's struct padding.
struct KexInvSaveRecord
{
    KexInv inv;
    uint8_t difficulty;
    uint8_t flags;
    uint8_t unlockedCount;
    uint8_t reserved;
    char map[BMAX_PATH];
    uint8_t unlocked[KEX_CAMPAIGN_MAX_MAPS];
    uint64_t playTimeTicks;
};

EDUKE32_STATIC_ASSERT(sizeof(KexInvSaveRecordV2) == 12);
EDUKE32_STATIC_ASSERT(sizeof(KexInvSaveRecordV3) == 268);
EDUKE32_STATIC_ASSERT(sizeof(KexInvSaveRecordV4) == 332);

static char const *KexInv_SaveFileName(void)
{
    // Tests can isolate the sidecar without touching a player's real slots.
    // Normal launches never set this and retain the established filename.
    char const *const overrideName = Bgetenv("VB_KEXINV_SAVE_FILE");
    return overrideName && overrideName[0] ? overrideName : KEX_INV_SAVE_FILE;
}

// A KEX checkpoint is deliberately kept beside the small campaign sidecar,
// rather than inflating every inventory record by the maximum Build map size.
// This preserves V1--V5 compatibility and lets each of the five slots retain
// its own geometry/actor snapshot.  It is a native-endian sidecar like the
// classic savgamea.sav backing record; the magic and checksum make a partial
// or stale write safely fall back to the level entrance.
static char const s_checkpointSaveMagicV1[8] = "KEXCHK1";

struct KexCheckpointDiskHeader
{
    char magic[8];
    uint32_t payloadSize;
    uint32_t checksum;
};

struct KexCheckpointDiskState
{
    int32_t x, y, z;
    int16_t sector, angle, id;
    int16_t numSectors, numWalls;
    uint8_t valid;
    uint8_t reserved[3];
    char map[BMAX_PATH];
    KexCheckpointPlayerState player;
    uint8_t activated[(kMaxSprites + 7) >> 3];
    uint8_t removed[(kMaxSprites + 7) >> 3];
    int16_t channels[kMaxChannels];
};

EDUKE32_STATIC_ASSERT(sizeof(KexCheckpointDiskHeader) == 16);

static int KexCheckpoint_SaveFileName(int nSlot, char *path, int pathSize)
{
    if ((unsigned)nSlot >= KEX_INV_SAVE_SLOTS || !path || pathSize <= 0)
        return 0;
    int const written = Bsnprintf(path, pathSize, "%s.chk%d", KexInv_SaveFileName(), nSlot);
    return written >= 0 && written < pathSize;
}

static size_t KexCheckpoint_DiskPayloadSize(int nSectors, int nWalls)
{
    if ((unsigned)nSectors > (unsigned)kMaxSectors
        || (unsigned)nWalls > (unsigned)kMaxWalls)
        return 0;
    return sizeof(KexCheckpointDiskState)
        + (size_t)nSectors * sizeof(sectortype)
        + (size_t)nWalls * sizeof(walltype);
}

static uint32_t KexCheckpoint_Checksum(uint32_t checksum, void const *data, size_t size)
{
    uint8_t const *bytes = (uint8_t const *)data;
    for (size_t i = 0; i < size; i++)
        checksum = (checksum ^ bytes[i]) * UINT32_C(16777619);
    return checksum;
}

static int KexCheckpoint_WriteHeader(FILE *fp, uint32_t payloadSize,
                                     uint32_t checksum)
{
    KexCheckpointDiskHeader header;
    Bmemcpy(header.magic, s_checkpointSaveMagicV1, sizeof(header.magic));
    header.payloadSize = payloadSize;
    header.checksum = checksum;
    return fwrite(&header, sizeof(header), 1, fp) == 1;
}

static void KexCheckpoint_Save(int nSlot)
{
    char path[BMAX_PATH];
    if (!KexCheckpoint_SaveFileName(nSlot, path, sizeof(path)))
        return;

    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        OSD_Printf(OSD_ERROR "Kex checkpoint save: cannot write %s\n", path);
        return;
    }

    // Overwriting a slot before reaching a scarab must not resurrect the
    // previous campaign's checkpoint when that slot is loaded later.
    if (!kex_enable_checkpoints || !s_checkpoint.valid || s_checkpoint.pending
        || !KexCheckpoint_MapMatches())
    {
        int const ok = KexCheckpoint_WriteHeader(fp, 0, 0);
        fclose(fp);
        if (!ok)
            OSD_Printf(OSD_ERROR "Kex checkpoint save: short write to %s\n", path);
        return;
    }

    int const nSectors = s_checkpoint.numSectors;
    int const nWalls = s_checkpoint.numWalls;
    size_t const payloadSize = KexCheckpoint_DiskPayloadSize(nSectors, nWalls);
    if (!payloadSize || payloadSize > UINT32_MAX)
    {
        fclose(fp);
        OSD_Printf(OSD_ERROR "Kex checkpoint save: invalid world dimensions for %s\n", path);
        return;
    }

    KexCheckpointDiskState disk;
    Bmemset(&disk, 0, sizeof(disk));
    disk.x = s_checkpoint.x; disk.y = s_checkpoint.y; disk.z = s_checkpoint.z;
    disk.sector = s_checkpoint.sector; disk.angle = s_checkpoint.angle;
    disk.id = s_checkpoint.id;
    disk.numSectors = (int16_t)nSectors; disk.numWalls = (int16_t)nWalls;
    disk.valid = 1;
    Bstrncpyz(disk.map, s_checkpoint.map, sizeof(disk.map));
    disk.player = s_checkpoint.player;
    Bmemcpy(disk.activated, s_checkpoint.activated, sizeof(disk.activated));
    Bmemcpy(disk.removed, s_checkpoint.removed, sizeof(disk.removed));
    Bmemcpy(disk.channels, s_checkpoint.channels, sizeof(disk.channels));

    uint32_t checksum = UINT32_C(2166136261);
    checksum = KexCheckpoint_Checksum(checksum, &disk, sizeof(disk));
    checksum = KexCheckpoint_Checksum(checksum, s_checkpoint.sectors,
                                      (size_t)nSectors * sizeof(sectortype));
    checksum = KexCheckpoint_Checksum(checksum, s_checkpoint.walls,
                                      (size_t)nWalls * sizeof(walltype));
    int const ok = KexCheckpoint_WriteHeader(fp, (uint32_t)payloadSize, checksum)
        && fwrite(&disk, sizeof(disk), 1, fp) == 1
        && fwrite(s_checkpoint.sectors, sizeof(sectortype), nSectors, fp) == (size_t)nSectors
        && fwrite(s_checkpoint.walls, sizeof(walltype), nWalls, fp) == (size_t)nWalls;
    fclose(fp);
    if (!ok)
        OSD_Printf(OSD_ERROR "Kex checkpoint save: short write to %s\n", path);
}

static int KexCheckpoint_Load(int nSlot)
{
    char path[BMAX_PATH];
    if (!kex_enable_checkpoints || !KexCheckpoint_SaveFileName(nSlot, path, sizeof(path)))
        return 0;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0; // V1--V4 campaigns predate durable checkpoint sidecars.

    KexCheckpointDiskHeader header;
    KexCheckpointDiskState disk;
    Bmemset(&header, 0, sizeof(header));
    Bmemset(&disk, 0, sizeof(disk));
    int const readHeader = fread(&header, sizeof(header), 1, fp) == 1;
    if (!readHeader || Bmemcmp(header.magic, s_checkpointSaveMagicV1, sizeof(header.magic)))
    {
        fclose(fp);
        KexCheckpoint_Reset();
        return 0;
    }
    if (header.payloadSize == 0)
    {
        fclose(fp);
        KexCheckpoint_Reset();
        return 0;
    }

    size_t const maxPayload = KexCheckpoint_DiskPayloadSize(kMaxSectors, kMaxWalls);
    if (header.payloadSize < sizeof(disk) || header.payloadSize > maxPayload
        || fread(&disk, sizeof(disk), 1, fp) != 1)
    {
        fclose(fp);
        KexCheckpoint_Reset();
        OSD_Printf(OSD_ERROR "Kex checkpoint save: damaged data in %s\n", path);
        return 0;
    }

    disk.map[sizeof(disk.map) - 1] = 0;
    int const nSectors = disk.numSectors;
    int const nWalls = disk.numWalls;
    size_t const expectedSize = KexCheckpoint_DiskPayloadSize(nSectors, nWalls);
    if (!disk.valid || !disk.map[0] || !expectedSize
        || header.payloadSize != expectedSize
        || fread(s_checkpoint.sectors, sizeof(sectortype), nSectors, fp) != (size_t)nSectors
        || fread(s_checkpoint.walls, sizeof(walltype), nWalls, fp) != (size_t)nWalls)
    {
        fclose(fp);
        KexCheckpoint_Reset();
        OSD_Printf(OSD_ERROR "Kex checkpoint save: invalid world data in %s\n", path);
        return 0;
    }
    fclose(fp);

    uint32_t checksum = UINT32_C(2166136261);
    checksum = KexCheckpoint_Checksum(checksum, &disk, sizeof(disk));
    checksum = KexCheckpoint_Checksum(checksum, s_checkpoint.sectors,
                                      (size_t)nSectors * sizeof(sectortype));
    checksum = KexCheckpoint_Checksum(checksum, s_checkpoint.walls,
                                      (size_t)nWalls * sizeof(walltype));
    if (checksum != header.checksum)
    {
        KexCheckpoint_Reset();
        OSD_Printf(OSD_ERROR "Kex checkpoint save: checksum failed for %s\n", path);
        return 0;
    }

    // The map is loaded after menu_GameLoad() returns. Mark this state pending
    // so BeginLevel(), WorldLoaded(), and FinishSpawn() reconstruct it through
    // the same path used by a death reload.
    s_checkpoint.x = disk.x; s_checkpoint.y = disk.y; s_checkpoint.z = disk.z;
    s_checkpoint.sector = disk.sector; s_checkpoint.angle = disk.angle;
    s_checkpoint.id = disk.id;
    s_checkpoint.numSectors = (int16_t)nSectors;
    s_checkpoint.numWalls = (int16_t)nWalls;
    s_checkpoint.valid = 1;
    s_checkpoint.pending = 1;
    Bstrncpyz(s_checkpoint.map, disk.map, sizeof(s_checkpoint.map));
    s_checkpoint.player = disk.player;
    Bmemcpy(s_checkpoint.activated, disk.activated, sizeof(s_checkpoint.activated));
    Bmemcpy(s_checkpoint.removed, disk.removed, sizeof(s_checkpoint.removed));
    Bmemcpy(s_checkpoint.channels, disk.channels, sizeof(s_checkpoint.channels));

    if (!KexCheckpoint_MapMatches())
    {
        KexCheckpoint_Reset();
        OSD_Printf(OSD_ERROR "Kex checkpoint save: map mismatch in %s\n", path);
        return 0;
    }
    return 1;
}

static int KexInv_ReadSaveRecords(FILE *fp, KexInvSaveRecord *slots)
{
    char magic[8];
    Bmemset(slots, 0, sizeof(KexInvSaveRecord) * KEX_INV_SAVE_SLOTS);
    if (fread(magic, sizeof(magic), 1, fp) != 1)
        return 0;

    if (!Bmemcmp(magic, s_saveMagicV5, sizeof(magic)))
    {
        if (fread(slots, sizeof(KexInvSaveRecord) * KEX_INV_SAVE_SLOTS, 1, fp) != 1)
            return 0;
        for (int i = 0; i < KEX_INV_SAVE_SLOTS; i++)
        {
            slots[i].map[sizeof(slots[i].map) - 1] = 0;
            slots[i].flags &= kKexSaveValid | kKexSaveComplete | kKexSaveLegacyNoMap;
            slots[i].unlockedCount = (uint8_t)min((int)slots[i].unlockedCount,
                                                   KEX_CAMPAIGN_MAX_MAPS);
            for (int j = 0; j < slots[i].unlockedCount; j++)
                slots[i].unlocked[j] = !!slots[i].unlocked[j];
            if (slots[i].unlockedCount < KEX_CAMPAIGN_MAX_MAPS)
                Bmemset(&slots[i].unlocked[slots[i].unlockedCount], 0,
                        KEX_CAMPAIGN_MAX_MAPS - slots[i].unlockedCount);
        }
        return 5;
    }

    if (!Bmemcmp(magic, s_saveMagicV4, sizeof(magic)))
    {
        KexInvSaveRecordV4 oldSlots[KEX_INV_SAVE_SLOTS];
        if (fread(oldSlots, sizeof(oldSlots), 1, fp) != 1)
            return 0;
        for (int i = 0; i < KEX_INV_SAVE_SLOTS; i++)
        {
            oldSlots[i].map[sizeof(oldSlots[i].map) - 1] = 0;
            slots[i].inv = oldSlots[i].inv;
            slots[i].difficulty = oldSlots[i].difficulty;
            slots[i].flags = oldSlots[i].flags
                & (kKexSaveValid | kKexSaveComplete | kKexSaveLegacyNoMap);
            slots[i].unlockedCount = (uint8_t)min((int)oldSlots[i].unlockedCount,
                                                   KEX_CAMPAIGN_MAX_MAPS);
            Bstrncpyz(slots[i].map, oldSlots[i].map, sizeof(slots[i].map));
            Bmemcpy(slots[i].unlocked, oldSlots[i].unlocked,
                    slots[i].unlockedCount);
            for (int j = 0; j < slots[i].unlockedCount; j++)
                slots[i].unlocked[j] = !!slots[i].unlocked[j];
        }
        return 4;
    }

    if (!Bmemcmp(magic, s_saveMagicV3, sizeof(magic)))
    {
        KexInvSaveRecordV3 oldSlots[KEX_INV_SAVE_SLOTS];
        if (fread(oldSlots, sizeof(oldSlots), 1, fp) != 1)
            return 0;
        for (int i = 0; i < KEX_INV_SAVE_SLOTS; i++)
        {
            oldSlots[i].map[sizeof(oldSlots[i].map) - 1] = 0;
            slots[i].inv = oldSlots[i].inv;
            slots[i].difficulty = oldSlots[i].difficulty;
            slots[i].flags = oldSlots[i].flags
                & (kKexSaveValid | kKexSaveComplete | kKexSaveLegacyNoMap);
            Bstrncpyz(slots[i].map, oldSlots[i].map, sizeof(slots[i].map));

            // Older builds exposed the entire graph and therefore cannot tell
            // which routes had been earned. Keep those campaigns traversable.
            slots[i].unlockedCount = KEX_CAMPAIGN_MAX_MAPS;
            Bmemset(slots[i].unlocked, 1, sizeof(slots[i].unlocked));
        }
        return 3;
    }

    if (!Bmemcmp(magic, s_saveMagicV2, sizeof(magic)))
    {
        KexInvSaveRecordV2 oldSlots[KEX_INV_SAVE_SLOTS];
        if (fread(oldSlots, sizeof(oldSlots), 1, fp) != 1)
            return 0;
        for (int i = 0; i < KEX_INV_SAVE_SLOTS; i++)
        {
            slots[i].inv = oldSlots[i].inv;
            slots[i].difficulty = oldSlots[i].difficulty;
            slots[i].flags = kKexSaveLegacyNoMap;
            slots[i].unlockedCount = KEX_CAMPAIGN_MAX_MAPS;
            Bmemset(slots[i].unlocked, 1, sizeof(slots[i].unlocked));
        }
        return 2;
    }

    if (!Bmemcmp(magic, s_saveMagicV1, sizeof(magic)))
    {
        KexInv oldSlots[KEX_INV_SAVE_SLOTS];
        if (fread(oldSlots, sizeof(oldSlots), 1, fp) != 1)
            return 0;
        for (int i = 0; i < KEX_INV_SAVE_SLOTS; i++)
        {
            slots[i].inv = oldSlots[i];
            slots[i].flags = kKexSaveLegacyNoMap;
            slots[i].unlockedCount = KEX_CAMPAIGN_MAX_MAPS;
            Bmemset(slots[i].unlocked, 1, sizeof(slots[i].unlocked));
        }
        return 1;
    }
    return 0;
}

static int KexInv_CampaignStartMap(char *map, int mapSize)
{
    map[0] = 0;
    int const fil = kopen4loadfrommod("kexstart.txt", 0);
    if (fil == -1)
        return 0;

    int const len = min(kfilelength(fil), mapSize - 1);
    kread(fil, map, len);
    kclose(fil);
    map[len] = 0;
    for (char *p = map; *p; p++)
        if (*p == '\r' || *p == '\n')
        {
            *p = 0;
            break;
        }
    return map[0] != 0;
}

static int KexInv_MapAvailable(char const *map)
{
    int const fil = map && map[0] ? kopen4loadfrommod(map, 0) : -1;
    if (fil == -1)
        return 0;
    kclose(fil);
    return 1;
}

static void KexInv_SelectCampaignMap(char const *savedMap)
{
    char map[BMAX_PATH] = "";
    if (savedMap && savedMap[0])
    {
        Bstrncpyz(map, savedMap, sizeof(map));

        // A slot may have been created with an absolute or generated-directory
        // path that moved between installations. If the full name no longer
        // resolves, try its portable leaf through the mounted KEX search path.
        if (!KexInv_MapAvailable(map))
        {
            char const *leaf = map;
            for (char const *p = map; *p; p++)
                if (*p == '/' || *p == '\\')
                    leaf = p + 1;
            if (leaf != map && KexInv_MapAvailable(leaf))
            {
                char portable[BMAX_PATH];
                Bstrncpyz(portable, leaf, sizeof(portable));
                Bstrncpyz(map, portable, sizeof(map));
            }
        }
    }
    else
        KexInv_CampaignStartMap(map, sizeof(map));

    if (!map[0])
    {
        OSD_Printf(OSD_ERROR "Kex save: no campaign map or kexstart.txt is available\n");
        return;
    }

    Bstrncpyz(gUserMapFilename, map, BMAX_PATH);
    bUserMap = true;
}

int KexInv_SaveSlotInfo(int nSlot, char *map, int mapSize, int *complete)
{
    if (map && mapSize > 0)
        map[0] = 0;
    if (complete)
        *complete = 0;
    if (!g_kexMode || (unsigned)nSlot >= KEX_INV_SAVE_SLOTS)
        return 0;

    KexInvSaveRecord slots[KEX_INV_SAVE_SLOTS];
    FILE *fp = fopen(KexInv_SaveFileName(), "rb");
    if (!fp)
        return 0;
    int const version = KexInv_ReadSaveRecords(fp, slots);
    fclose(fp);
    if (version < 3 || !(slots[nSlot].flags & kKexSaveValid))
        return 0;

    if (map && mapSize > 0)
        Bstrncpyz(map, slots[nSlot].map, mapSize);
    if (complete)
        *complete = !!(slots[nSlot].flags & kKexSaveComplete);
    return 1;
}

void KexInv_Save(int nSlot)
{
    if (!g_kexMode || (unsigned)nSlot >= KEX_INV_SAVE_SLOTS)
        return;
    KexInvSaveRecord slots[KEX_INV_SAVE_SLOTS];
    Bmemset(slots, 0, sizeof(slots));

    char const *const saveFileName = KexInv_SaveFileName();
    FILE *fp = fopen(saveFileName, "rb");
    if (fp)
    {
        if (!KexInv_ReadSaveRecords(fp, slots))
            Bmemset(slots, 0, sizeof(slots));
        fclose(fp);
    }

    Bmemset(&slots[nSlot], 0, sizeof(slots[nSlot]));
    slots[nSlot].inv = KexInvList[nLocalPlayer];
    slots[nSlot].difficulty = (uint8_t)KexDifficulty_Get();
    slots[nSlot].flags = kKexSaveValid;
    slots[nSlot].unlockedCount = KEX_CAMPAIGN_MAX_MAPS;
    Bmemcpy(slots[nSlot].unlocked, s_kexUnlockedMaps,
            sizeof(slots[nSlot].unlocked));
    slots[nSlot].playTimeTicks = s_kexPlayTimeTicks;
    if (levelnew > 99)
        slots[nSlot].flags |= kKexSaveComplete;
    if (bUserMap && gUserMapFilename[0])
        Bstrncpyz(slots[nSlot].map, gUserMapFilename, sizeof(slots[nSlot].map));
    else
        KexInv_CampaignStartMap(slots[nSlot].map, sizeof(slots[nSlot].map));

    fp = fopen(saveFileName, "wb");
    if (!fp)
    {
        OSD_Printf(OSD_ERROR "Kex save: cannot write %s\n", saveFileName);
        return;
    }
    int const ok = fwrite(s_saveMagicV5, sizeof(s_saveMagicV5), 1, fp) == 1
        && fwrite(slots, sizeof(slots), 1, fp) == 1;
    fclose(fp);
    if (!ok)
        OSD_Printf(OSD_ERROR "Kex save: short write to %s\n", saveFileName);
    else
        KexCheckpoint_Save(nSlot);
}

int KexInv_Load(int nSlot, int nClassicMap)
{
    // A load can replace a live campaign with a legacy/no-checkpoint slot.
    // Clear its in-memory scarab state before inspecting the selected sidecar.
    KexCheckpoint_Reset();
    KexInv_Reset(nLocalPlayer);
    KexDifficulty_Set(kKexDifficultyNormal);
    KexCampaign_NewGame();
    if (!g_kexMode)
        return nClassicMap;

    // A pause-menu load must not inherit the map of the campaign currently in
    // memory when the selected slot is empty, old, or damaged.
    bUserMap = false;
    gUserMapFilename[0] = 0;
    if ((unsigned)nSlot >= KEX_INV_SAVE_SLOTS)
        return nClassicMap;

    KexInvSaveRecord slots[KEX_INV_SAVE_SLOTS];
    Bmemset(slots, 0, sizeof(slots));
    FILE *fp = fopen(KexInv_SaveFileName(), "rb");
    int version = 0;
    if (fp)
    {
        version = KexInv_ReadSaveRecords(fp, slots);
        fclose(fp);
    }

    KexInvSaveRecord const &slot = slots[nSlot];
    int const validMapped = version >= 3 && (slot.flags & kKexSaveValid);
    int const validLegacy = nClassicMap != 0 && (version > 0 && version < 3
        || version >= 3 && (slot.flags & kKexSaveLegacyNoMap));
    if (validMapped || validLegacy)
    {
        KexInvList[nLocalPlayer] = slots[nSlot].inv;
        KexDifficulty_Set(slots[nSlot].difficulty);
        int const unlockedCount = min((int)slot.unlockedCount,
                                      KEX_CAMPAIGN_MAX_MAPS);
        Bmemcpy(s_kexUnlockedMaps, slot.unlocked, unlockedCount);
        s_kexPlayTimeTicks = slot.playTimeTicks;
        if (validMapped && (slot.flags & kKexSaveComplete))
        {
            OSD_Printf("Kex save: slot %d is a completed campaign\n", nSlot + 1);
            return 100;
        }

        KexInv_SelectCampaignMap(validMapped ? slot.map : NULL);
        int const checkpointRestored = validMapped && bUserMap
            && KexCheckpoint_Load(nSlot);
        OSD_Printf("Kex save: slot %d restored %s%s%s (format V%d)\n",
                   nSlot + 1, bUserMap ? gUserMapFilename : "no map",
                   validLegacy ? ", exact legacy map unavailable" : "",
                   checkpointRestored ? ", checkpoint" : "", version);
        return bUserMap ? max(1, nClassicMap) : nClassicMap;
    }

    // A pre-sidecar classic slot selected from the KEX menu still belongs to
    // this mounted campaign. Its exact map cannot be inferred from nMap (all
    // converted levels use the user-map sentinel), but starting at the
    // authored opening is safer and more compatible than loading LEV1.
    if (nClassicMap != 0)
    {
        KexInv_SelectCampaignMap(NULL);
        OSD_Printf("Kex save: slot %d has no usable sidecar; using campaign start\n", nSlot + 1);
        return bUserMap ? max(1, nClassicMap) : nClassicMap;
    }
    return nClassicMap;
}

static int KexInv_WriteLegacyTestFile(char const *path, int version,
                                      KexInv const &inv, int difficulty)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return 0;

    int ok = 0;
    if (version == 1)
    {
        KexInv slots[KEX_INV_SAVE_SLOTS];
        Bmemset(slots, 0, sizeof(slots));
        slots[0] = inv;
        ok = fwrite(s_saveMagicV1, sizeof(s_saveMagicV1), 1, fp) == 1
            && fwrite(slots, sizeof(slots), 1, fp) == 1;
    }
    else if (version == 2)
    {
        KexInvSaveRecordV2 slots[KEX_INV_SAVE_SLOTS];
        Bmemset(slots, 0, sizeof(slots));
        slots[1].inv = inv;
        slots[1].difficulty = (uint8_t)difficulty;
        ok = fwrite(s_saveMagicV2, sizeof(s_saveMagicV2), 1, fp) == 1
            && fwrite(slots, sizeof(slots), 1, fp) == 1;
    }
    else if (version == 3)
    {
        KexInvSaveRecordV3 slots[KEX_INV_SAVE_SLOTS];
        Bmemset(slots, 0, sizeof(slots));
        slots[2].inv = inv;
        slots[2].difficulty = (uint8_t)difficulty;
        slots[2].flags = kKexSaveValid;
        Bstrncpyz(slots[2].map, "KEX_TOMB.MAP", sizeof(slots[2].map));
        ok = fwrite(s_saveMagicV3, sizeof(s_saveMagicV3), 1, fp) == 1
            && fwrite(slots, sizeof(slots), 1, fp) == 1;
    }
    else if (version == 4)
    {
        KexInvSaveRecordV4 slots[KEX_INV_SAVE_SLOTS];
        Bmemset(slots, 0, sizeof(slots));
        slots[2].inv = inv;
        slots[2].difficulty = (uint8_t)difficulty;
        slots[2].flags = kKexSaveValid;
        slots[2].unlockedCount = KEX_CAMPAIGN_MAX_MAPS;
        slots[2].unlocked[0] = 1;
        slots[2].unlocked[5] = 1;
        Bstrncpyz(slots[2].map, "KEX_TOMB.MAP", sizeof(slots[2].map));
        ok = fwrite(s_saveMagicV4, sizeof(s_saveMagicV4), 1, fp) == 1
            && fwrite(slots, sizeof(slots), 1, fp) == 1;
    }
    fclose(fp);
    return ok;
}

static void KexInv_RunSaveSelftest(void)
{
    char const *const testPath = Bgetenv("VB_KEXINV_SAVE_FILE");
    char const *const classicPath = Bgetenv("VB_EXHUMED_SAVE_FILE");
    if (!testPath || !testPath[0] || !Bstrcmp(testPath, KEX_INV_SAVE_FILE)
        || !classicPath || !classicPath[0] || !Bstrcmp(testPath, classicPath))
    {
        OSD_Printf("Kex campaign save test: FAIL (set distinct isolated VB_EXHUMED_SAVE_FILE and VB_KEXINV_SAVE_FILE paths)\n");
        return;
    }

    KexInv const savedInv = KexInvList[nLocalPlayer];
    Player const savedPlayer = PlayerList[nLocalPlayer];
    uint16_t const savedWeapons = nPlayerWeapons[nLocalPlayer];
    short const savedPistolClip = nPistolClip[nLocalPlayer];
    short const savedPlayerClip = nPlayerClip[nLocalPlayer];
    short const savedOldWeapon = nPlayerOldWeapon[nLocalPlayer];
    int const savedDifficulty = KexDifficulty_Get();
    int const savedLevelNew = levelnew;
    int const savedUserMap = bUserMap;
    uint64_t const savedPlayTimeTicks = s_kexPlayTimeTicks;
    uint8_t savedUnlocked[KEX_CAMPAIGN_MAX_MAPS];
    KexCampaignPlayerState savedCampaignPlayer[kMaxPlayers];
    Bmemcpy(savedUnlocked, s_kexUnlockedMaps, sizeof(savedUnlocked));
    Bmemcpy(savedCampaignPlayer, s_campaignPlayer,
            sizeof(savedCampaignPlayer));
    char savedMap[BMAX_PATH];
    Bstrncpyz(savedMap, gUserMapFilename, sizeof(savedMap));

    char startMap[BMAX_PATH] = "";
    int const haveStart = KexInv_CampaignStartMap(startMap, sizeof(startMap));

    KexInv legacyV1;
    Bmemset(&legacyV1, 0, sizeof(legacyV1));
    legacyV1.artifacts = 0x15;
    legacyV1.transmitter = 0xa1;
    legacyV1.ankhs = 0x12;
    legacyV1.dolls = 0x12345678u;
    int const wroteV1 = KexInv_WriteLegacyTestFile(testPath, 1, legacyV1,
                                                   kKexDifficultyNormal);
    int const loadedV1 = KexInv_Load(0, 1);
    int const okV1 = wroteV1 && loadedV1 == 1
        && !Bmemcmp(&KexInvList[nLocalPlayer], &legacyV1, sizeof(legacyV1))
        && KexDifficulty_Get() == kKexDifficultyNormal && haveStart && bUserMap
        && !Bstrcasecmp(gUserMapFilename, startMap)
        && KexCampaign_MapUnlocked(0)
        && KexCampaign_MapUnlocked(KEX_CAMPAIGN_MAX_MAPS - 1);

    KexInv legacyV2;
    Bmemset(&legacyV2, 0, sizeof(legacyV2));
    legacyV2.artifacts = 0x22;
    legacyV2.transmitter = 0x5a;
    legacyV2.ankhs = 0x2a;
    legacyV2.map = 1;
    legacyV2.dolls = 0x0badc0deu;
    int const wroteV2 = KexInv_WriteLegacyTestFile(testPath, 2, legacyV2,
                                                   kKexDifficultyHard);
    int const loadedV2 = KexInv_Load(1, 1);
    int const okV2 = wroteV2 && loadedV2 == 1
        && !Bmemcmp(&KexInvList[nLocalPlayer], &legacyV2, sizeof(legacyV2))
        && KexDifficulty_Get() == kKexDifficultyHard && bUserMap
        && !Bstrcasecmp(gUserMapFilename, startMap)
        && KexCampaign_MapUnlocked(0)
        && KexCampaign_MapUnlocked(KEX_CAMPAIGN_MAX_MAPS - 1);

    // Save a deliberately relocated path. V5 must retain it on disk, then
    // recover through the basename mounted by -j when loading elsewhere.
    char leaf[BMAX_PATH];
    char const *leafStart = savedMap;
    for (char const *p = savedMap; *p; p++)
        if (*p == '/' || *p == '\\')
            leafStart = p + 1;
    Bstrncpyz(leaf, leafStart, sizeof(leaf));
    if (!leaf[0])
        Bstrncpyz(leaf, startMap, sizeof(leaf));
    char movedMap[BMAX_PATH];
    Bsnprintf(movedMap, sizeof(movedMap), "/moved-install/%s", leaf);

    KexInv expectedV3;
    Bmemset(&expectedV3, 0, sizeof(expectedV3));
    expectedV3.artifacts = 0x3f;
    expectedV3.transmitter = 0xc3;
    expectedV3.ankhs = 0x16;
    expectedV3.dolls = 0x3456789au;
    uint64_t const expectedPlayTimeTicks =
        (UINT64_C(25) * 3600 + UINT64_C(43) * 60 + 7) * kTimerTicks + 4;
    KexInvList[nLocalPlayer] = expectedV3;
    KexDifficulty_Set(kKexDifficultyPharaoh);
    KexCampaign_NewGame();
    s_kexPlayTimeTicks = expectedPlayTimeTicks;
    KexCampaign_UnlockMap(0);
    KexCampaign_UnlockMap(5);
    KexCampaign_UnlockMap(24);
    Bstrncpyz(gUserMapFilename, movedMap, BMAX_PATH);
    bUserMap = true;
    levelnew = 0;
    // The remaster's sidecar augments the classic menu slot; exercising the
    // real menu route captures the complete live player state before its
    // KEX map/inventory companion is written.
    menu_GameSave(2);

    KexInvSaveRecord diskSlots[KEX_INV_SAVE_SLOTS];
    Bmemset(diskSlots, 0, sizeof(diskSlots));
    int diskVersion = 0;
    FILE *fp = fopen(testPath, "rb");
    if (fp)
    {
        diskVersion = KexInv_ReadSaveRecords(fp, diskSlots);
        fclose(fp);
    }

    KexInv_Reset(nLocalPlayer);
    KexDifficulty_Set(kKexDifficultyNormal);
    bUserMap = false;
    gUserMapFilename[0] = 0;
    int const loadedV5 = KexInv_Load(2, 0);
    char restoredV5Map[BMAX_PATH];
    Bstrncpyz(restoredV5Map, gUserMapFilename, sizeof(restoredV5Map));
    int const expectedPortable = KexInv_MapAvailable(leaf);
    char const *const expectedMap = expectedPortable ? leaf : movedMap;
    int const okV5RoundTrip = diskVersion == 5
        && (diskSlots[2].flags & kKexSaveValid)
        && !Bstrcmp(diskSlots[2].map, movedMap)
        && diskSlots[2].playTimeTicks == expectedPlayTimeTicks
        && diskSlots[2].unlockedCount == KEX_CAMPAIGN_MAX_MAPS
        && diskSlots[2].unlocked[0] && diskSlots[2].unlocked[5]
        && diskSlots[2].unlocked[24] && !diskSlots[2].unlocked[1]
        && loadedV5 == 1 && bUserMap
        && !Bstrcasecmp(gUserMapFilename, expectedMap)
        && !Bmemcmp(&KexInvList[nLocalPlayer], &expectedV3, sizeof(expectedV3))
        && KexDifficulty_Get() == kKexDifficultyPharaoh
        && KexCampaign_MapUnlocked(0) && KexCampaign_MapUnlocked(5)
        && s_kexPlayTimeTicks == expectedPlayTimeTicks
        && KexCampaign_MapUnlocked(24) && !KexCampaign_MapUnlocked(1);

    // Writing slot 3 upgraded the whole V2 file. Slot 2 was not the one
    // rewritten, so prove its inventory/difficulty survived behind the
    // explicit legacy-no-map marker.
    int const loadedMigratedV2 = KexInv_Load(1, 1);
    int const preservedV2 = (diskSlots[1].flags & kKexSaveLegacyNoMap)
        && loadedMigratedV2 == 1
        && !Bmemcmp(&KexInvList[nLocalPlayer], &legacyV2, sizeof(legacyV2))
        && KexDifficulty_Get() == kKexDifficultyHard && bUserMap
        && !Bstrcasecmp(gUserMapFilename, startMap)
        && KexCampaign_MapUnlocked(KEX_CAMPAIGN_MAX_MAPS - 1);

    // V3 had exact-map metadata but no unlock vector. Loading it must retain
    // its map while granting the routes that older builds exposed globally.
    int const wroteV3 = KexInv_WriteLegacyTestFile(testPath, 3, expectedV3,
                                                   kKexDifficultyPharaoh);
    int const loadedV3 = KexInv_Load(2, 0);
    int const migratedV3 = wroteV3 && loadedV3 == 1 && bUserMap
        && !Bstrcasecmp(gUserMapFilename, "KEX_TOMB.MAP")
        && KexCampaign_MapUnlocked(0)
        && KexCampaign_MapUnlocked(KEX_CAMPAIGN_MAX_MAPS - 1);

    // V4 already had an exact map and unlock vector, but not play time. Its
    // migration must retain that campaign data and start the new timer at
    // zero rather than interpreting trailing bytes as a duration.
    s_kexPlayTimeTicks = expectedPlayTimeTicks;
    int const wroteV4 = KexInv_WriteLegacyTestFile(testPath, 4, expectedV3,
                                                   kKexDifficultyPharaoh);
    int const loadedV4 = KexInv_Load(2, 0);
    int const migratedV4 = wroteV4 && loadedV4 == 1 && bUserMap
        && !Bstrcasecmp(gUserMapFilename, "KEX_TOMB.MAP")
        && !Bmemcmp(&KexInvList[nLocalPlayer], &expectedV3, sizeof(expectedV3))
        && KexDifficulty_Get() == kKexDifficultyPharaoh
        && KexCampaign_MapUnlocked(0) && KexCampaign_MapUnlocked(5)
        && !KexCampaign_MapUnlocked(1) && s_kexPlayTimeTicks == 0;
    int const okV5 = okV5RoundTrip && preservedV2 && migratedV3 && migratedV4;

    KexInvList[nLocalPlayer] = expectedV3;
    KexDifficulty_Set(kKexDifficultyPharaoh);
    Bstrncpyz(gUserMapFilename, "KEX_TOMBEND.MAP", BMAX_PATH);
    bUserMap = true;
    levelnew = 100;
    menu_GameSave(3);
    KexInv_Reset(nLocalPlayer);
    KexDifficulty_Set(kKexDifficultyNormal);
    int const loadedComplete = KexInv_Load(3, 0);
    int const okComplete = loadedComplete == 100
        && !Bmemcmp(&KexInvList[nLocalPlayer], &expectedV3, sizeof(expectedV3))
        && KexDifficulty_Get() == kKexDifficultyPharaoh;

    Player &probe = PlayerList[nLocalPlayer];
    probe.nHealth = 73;
    probe.nMagic = 321;
    probe.items[0] = 4;
    probe.nAmmo[kWeaponPistol] = 3;
    probe.nCurrentWeapon = kWeaponPistol;
    probe.nLastWeapon = kWeaponSword;
    nPlayerWeapons[nLocalPlayer] = (1 << kWeaponSword) | (1 << kWeaponPistol);
    nPistolClip[nLocalPlayer] = 3;
    nPlayerClip[nLocalPlayer] = 17;
    KexCampaign_StagePlayer(nLocalPlayer);
    probe.nHealth = (short)KexInv_MaxHealth(nLocalPlayer);
    probe.nMagic = 0;
    probe.items[0] = 0;
    probe.nAmmo[kWeaponPistol] = (short)KexInv_AmmoMax(kWeaponPistol);
    probe.nCurrentWeapon = kWeaponSword;
    probe.nLastWeapon = kWeaponPistol;
    nPlayerWeapons[nLocalPlayer] = 1 << kWeaponSword;
    nPistolClip[nLocalPlayer] = 0;
    nPlayerClip[nLocalPlayer] = 0;
    KexCampaign_FinishSpawn(nLocalPlayer);
    int const okPlayerState = probe.nHealth == 73 && probe.nMagic == 321
        && probe.items[0] == 4 && probe.nAmmo[kWeaponPistol] == 3
        && probe.nCurrentWeapon == kWeaponPistol
        && probe.nLastWeapon == kWeaponSword
        && nPlayerWeapons[nLocalPlayer]
            == ((1 << kWeaponSword) | (1 << kWeaponPistol))
        && nPistolClip[nLocalPlayer] == 3 && nPlayerClip[nLocalPlayer] == 17;

    int const ok = okV1 && okV2 && okV5 && okComplete && okPlayerState;
    OSD_Printf("Kex campaign save test: %s (V1 %s, V2 %s, V3 %s, V4 %s, V5 map/unlocks/time %s/%s, player state %s, terminal %d, file %s)\n",
               ok ? "PASS" : "FAIL", okV1 ? "migrated" : "FAIL",
               okV2 && preservedV2 ? "migrated+preserved" : "FAIL",
               migratedV3 ? "migrated" : "FAIL", migratedV4 ? "migrated" : "FAIL",
               diskSlots[2].map,
               restoredV5Map, okPlayerState ? "preserved" : "FAIL",
               loadedComplete, testPath);

    KexInvList[nLocalPlayer] = savedInv;
    PlayerList[nLocalPlayer] = savedPlayer;
    nPlayerWeapons[nLocalPlayer] = savedWeapons;
    nPistolClip[nLocalPlayer] = savedPistolClip;
    nPlayerClip[nLocalPlayer] = savedPlayerClip;
    nPlayerOldWeapon[nLocalPlayer] = savedOldWeapon;
    KexDifficulty_Set(savedDifficulty);
    levelnew = savedLevelNew;
    bUserMap = savedUserMap;
    s_kexPlayTimeTicks = savedPlayTimeTicks;
    Bstrncpyz(gUserMapFilename, savedMap, BMAX_PATH);
    Bmemcpy(s_kexUnlockedMaps, savedUnlocked, sizeof(savedUnlocked));
    Bmemcpy(s_campaignPlayer, savedCampaignPlayer,
            sizeof(savedCampaignPlayer));
}
