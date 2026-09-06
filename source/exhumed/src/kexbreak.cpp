//-------------------------------------------------------------------------
//
// The remaster's breakable props for VibeBuild32.  See kexbreak.h.
//
// The remaster's own recipe, from sprites/objects/vase01_anim.txt:
//
//     vase01_death { frame {
//         action A_PlayLocalSound("sounds/glassbreak.wav")
//         action A_Spawn("Fx_VaseExplodePuff", 0, 0, 32)
//         action A_RandomVaseItem()
//         action A_TossActor("Obj_Vase01_Shard01", 0, 0, 32, 8, 8, 4, 16)
//         ... one per shard ...
//         action A_Destroy() } }
//
// and the actor def gives `health 30`, `shootable 1`, `solid 1`. The shard art
// is already in the prop's own idle anim -- frame 0 is the intact pot and the
// frames after it are the shards, which is why holding the pot on frame 0
// (noAdvanceFrames) is what stops it flickering into a sliver of debris.
// kexmap.py writes that out as kexbreak.txt, including the exact authored
// local-sound recipe: none, glass, wood or flesh. Exploding vases and the mine
// deliberately use none because their explosion/countdown owns the audio.
//
// Build already has the pieces this needs. A prop registers a runlist run the
// way BuildObject() does, so a bullet that hits the sprite routes a 0x80000
// damage message straight to it, and the shards are BuildCreatureChunk() --
// the same physics chunks the exploding fire cauldron throws.
//
//-------------------------------------------------------------------------
#include "build.h"
#include "palette.h"
#include "polymost.h"
#include "compat.h"
#include "baselayer.h"

#include "exhumed.h"
#include "engine.h"
#include "runlist.h"
#include "sequence.h"
#include "sound.h"
#include "move.h"
#include "player.h"
#include "spider.h"
#include "wasp.h"
#include "scorp.h"
#include "set.h"
#include "anims.h"
#include "object.h"
#include "random.h"
#include "lighting.h"
#include "light.h"
#include "kextable.h"
#include "kexbreak.h"
#include "kexinv.h"
#include "init.h"
#include "queen.h"
#include "save.h"
#include "status.h"

void KillCreatures();   // object.cpp, undeclared there

#define kMaxKexBreakTypes 64
#define kMaxKexBreaks     512

enum KexBreakSound
{
    kKexBreakSoundNone,
    kKexBreakSoundGlass,
    kKexBreakSoundWood,
    kKexBreakSoundFlesh,
    kKexBreakSoundCount,
};

struct KexBreakType
{
    int16_t tile, health, shard0, nshards, clipdist;
    int16_t blastradius, blastdamage;   // A_RadialBlast, in the remaster's units
    int16_t sight, fuse;                // the mine: arms within sight (Build units), blasts fuse ticks later
    int16_t blasttoggle;                // A_RadialBlast's open-Toggle-faces argument
    int16_t breaksound;                 // KexBreakSound, extracted from the authored death animation
    int16_t spawnheight;                // actor height, KEX units: A_RandomVaseItem origin + height
};

struct KexBreakProp
{
    int16_t nSprite, nHealth, nType, nRunFree;
    int16_t nFuse;                      // -1 idle, else ticks to the blast
};

static KexBreakType s_type[kMaxKexBreakTypes];
static int          s_types = -1;          // -1 = not read yet
static int          s_legacyBreakTable;    // pre-breaksound 11-column conversion
static KexBreakProp s_prop[kMaxKexBreaks];
static int          s_props;
static int          s_selftest;   // VB_KEXBREAK_TEST: shatter everything after N ticks
static int          s_breakSelftestActive;
static int          s_soundTest;  // VB_KEXBREAK_SOUND_TEST: exact table/playback census
static int          s_soundExpected[kKexBreakSoundCount];
static int          s_soundPlayed[kKexBreakSoundCount];
static int          s_vasePuffsExpected;
static int          s_vasePuffsSpawned;
static int          s_scorpTest;  // VB_KEXBREAK_SCORPTEST: force every vase's case-0 AI_Scorp
static int          s_vaseScorpsSpawned;
static int          s_vaseScorpsAtHeight;
static int          s_mineTest;    // VB_KEX_MINE_TEST: exact authored countdown poses
static int          s_mineTestProp;
static int          s_mineTestSteps;
static int          s_mineTestPassed;
static int          s_orbTest;           // VB_KEXDROP_ORB_TEST: exact delayed drop actors
static int          s_orbTestStarted;
static int          s_orbTestExpected;
static int          s_orbTestCallbacks;
static int          s_orbTestPassed;
static uint8_t      s_orbTestEffect[kMaxSprites]; // 1 ammo, 2 health

// The map scroll is normally only created by the remaster's rare vase roll,
// so its generated actor tile is carried in a tiny side table rather than in
// every converted MAP.
static int s_kexMapTile = -1, s_kexPowerTile = -1, s_kexRaTile = -1, s_kexCobraTile = -1;
static int s_kexCobraJointTile = -1;
static int s_kexAmmoTile = -1, s_kexHealthTile = -1;
static int s_kexPowerTimer;

#define KEX_VASE_SHOT_BASE 1024
#define KEX_MAX_VASE_SHOTS 128
struct KexVaseShot
{
    int16_t nSprite, nRunFree;
    int16_t nJoint[4];
    int32_t trailx[4], traily[4], trailz[4];
    int32_t vx, vy, vz;
    int16_t ticks;
    uint8_t cobra;
};
static KexVaseShot s_vaseShot[KEX_MAX_VASE_SHOTS];
static int s_vaseShots;

static bool KexDrop_ParseLine(char *line, int nRec)
{
    (void)nRec;
    return Bsscanf(line, "%d %d %d %d %d %d %d", &s_kexMapTile, &s_kexPowerTile,
                   &s_kexRaTile, &s_kexCobraTile, &s_kexCobraJointTile,
                   &s_kexAmmoTile, &s_kexHealthTile) >= 4;
}

static void KexDrop_LoadTable(void)
{
    s_kexMapTile = -1;
    s_kexPowerTile = -1;
    s_kexRaTile = -1;
    s_kexCobraTile = -1;
    s_kexCobraJointTile = -1;
    s_kexAmmoTile = -1;
    s_kexHealthTile = -1;
    KexTable_Load("kexdrop.txt", 1, KexDrop_ParseLine);
}

static bool KexBreak_ParseLine(char *line, int nRec)
{
    int tile, health, shard0, nshards, clipdist = 32, br = 0, bd = 0;
    int sight = 0, fuse = 0, wood = 0, blasttoggle = 0, breaksound = -1;
    int spawnheight = 0;
    int const fields = Bsscanf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d",
                               &tile, &health, &shard0, &nshards, &clipdist, &br,
                               &bd, &sight, &fuse, &wood, &blasttoggle, &breaksound,
                               &spawnheight);
    if (fields < 4)
        return false;

    // Compatibility with conversions made before the explicit recipe column:
    // blast props had no local callback, barrel rows carried wood, and every
    // other old row used the ordinary glass sound. Only a fresh conversion can
    // distinguish Obj_Vase09's authored flesh callback from old vase rows.
    if (fields < 12)
    {
        s_legacyBreakTable = 1;
        breaksound = bd > 0 ? kKexBreakSoundNone
                            : (wood ? kKexBreakSoundWood : kKexBreakSoundGlass);
    }
    if (breaksound < kKexBreakSoundNone || breaksound >= kKexBreakSoundCount)
        return false;

    KexBreakType &t = s_type[nRec];
    t.tile = tile; t.health = health;
    t.shard0 = shard0; t.nshards = nshards;
    t.clipdist = clamp(clipdist, 1, 255);
    t.blastradius = br; t.blastdamage = bd;
    t.sight = sight; t.fuse = fuse; t.blasttoggle = !!blasttoggle;
    t.breaksound = breaksound;
    t.spawnheight = clamp(spawnheight, 0, 4095);
    return true;
}

static void KexBreak_LoadTable(void)
{
    s_legacyBreakTable = 0;
    s_types = KexTable_Load("kexbreak.txt", kMaxKexBreakTypes, KexBreak_ParseLine);
}

// ---------------------------------------------------------------------------
// Light sources. kexmap.py writes kexlight.txt out of the actors the remaster
// groups as "Light Sources" -- its fires, torches and lamps -- one line of
// "<tile> <peak> <shift>" each. Their glow is baked into the vertex colours in
// Kex, and Build cannot shade part of a wall, so it is put back as a real
// light: on the wall behind TOMB's first fire pot the baked luminance runs 39
// at the top to 255 at the bottom of a single face, and one shade per wall
// cannot hold that.
// ---------------------------------------------------------------------------
#define kMaxKexLightTypes 32
#define kMaxKexLights     256

struct KexLightType { int16_t tile, peak, shift; };

struct KexLightSource { int16_t nSprite, nType; };

static KexLightType   s_lightType[kMaxKexLightTypes];
static int            s_lightTypes = -1;    // -1 = not read yet
static KexLightSource s_light[kMaxKexLights];
static int            s_lights;

// Animated scenery. Build's animtilerange advances every use of one base tile
// in lockstep. Kex's randomizeFrames instead starts each instance on a stable
// different phase and keeps advancing it, which is especially visible when a
// row of fire pots shares one wall. kexdecor.txt carries the authored frame
// run and cadence generated from the retail animation files.
#define kMaxKexDecorAnims 64
struct KexDecorAnim { int16_t tile, count, ticks, randomphase; };
static KexDecorAnim s_decorAnim[kMaxKexDecorAnims];
static int s_decorAnims = -1;

static bool KexDecor_ParseLine(char *line, int nRec)
{
    int tile, count, ticks, randomphase;
    if (Bsscanf(line, "%d %d %d %d", &tile, &count, &ticks, &randomphase) != 4
        || tile <= 0 || count <= 1 || ticks <= 0)
        return false;
    s_decorAnim[nRec] = { (int16_t)tile, (int16_t)count, (int16_t)ticks,
                          (int16_t)!!randomphase };
    return true;
}

void KexDecor_Init(void)
{
    if (s_decorAnims < 0)
        s_decorAnims = KexTable_Load("kexdecor.txt", kMaxKexDecorAnims,
                                    KexDecor_ParseLine);
    if (!g_kexMode || s_decorAnims <= 0)
        return;

    int instances = 0;
    for (int i = 0; i < kMaxSprites; i++)
        if (sprite[i].statnum < kMaxStatus && sprite[i].sectnum >= 0
            && KexTable_FindTile(s_decorAnim, s_decorAnims, sprite[i].picnum) >= 0)
            instances++;
    if (instances)
        OSD_Printf("Kex animated scenery: %d instances from %d ranges\n",
                   instances, s_decorAnims);
}

int KexDecor_ViewTile(int nSprite)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return -1;
    if (s_decorAnims < 0)
        s_decorAnims = KexTable_Load("kexdecor.txt", kMaxKexDecorAnims,
                                    KexDecor_ParseLine);
    if (s_decorAnims <= 0)
        return -1;

    int const nAnim = KexTable_FindTile(s_decorAnim, s_decorAnims,
                                        sprite[nSprite].picnum);
    if (nAnim < 0)
        return -1;
    KexDecorAnim const &a = s_decorAnim[nAnim];

    uint32_t phase = 0;
    if (a.randomphase)
    {
        // Position and source sprite identity make this deterministic across
        // save/load while decor placed beside itself still gets another phase.
        uint32_t seed = (uint32_t)(nSprite + 1) * 0x9e3779b9u;
        seed ^= (uint32_t)sprite[nSprite].x * 0x85ebca6bu;
        seed ^= (uint32_t)sprite[nSprite].y * 0xc2b2ae35u;
        seed ^= seed >> 16;
        phase = seed % (uint32_t)a.count;
    }
    return a.tile + (((uint32_t)totalclock / (uint32_t)a.ticks + phase)
                     % (uint32_t)a.count);
}

static bool KexLight_ParseLine(char *line, int nRec)
{
    int tile, peak, shift;
    if (Bsscanf(line, "%d %d %d", &tile, &peak, &shift) != 3)
        return false;

    KexLightType &t = s_lightType[nRec];
    t.tile = tile; t.peak = peak; t.shift = shift;
    return true;
}

static void KexLight_LoadTable(void)
{
    s_lightTypes = KexTable_Load("kexlight.txt", kMaxKexLightTypes, KexLight_ParseLine);
}

void KexLight_Init(void)
{
    s_lights = 0;
    if (s_lightTypes < 0)
        KexLight_LoadTable();
    // The level's baked light field, for polymost's per-vertex shading; with
    // it the sources' glow is already in the field, so the per-sector glow
    // below stays off.
    polymost_kexVertexLight = 0;
    polymost_kexShadeAt = NULL;
    polymost_kexColorAt = NULL;
    if (g_kexMode && gUserMapFilename[0])
    {
        KexVL_Load(gUserMapFilename);
        polymost_kexShadeAt = KexVL_ShadeAt;
        polymost_kexColorAt = KexVL_ColorAt;
        polymost_kexVertexLight = KexVL_Ready() && !Bgetenv("VB_KEX_FLATLIGHT");
    }
    if (!g_kexMode || !s_lightTypes)
        return;

    for (int i = 0; i < kMaxSprites && s_lights < kMaxKexLights; i++)
    {
        if (sprite[i].statnum >= kMaxStatus || sprite[i].sectnum < 0)
            continue;

        int const nType = KexTable_FindTile(s_lightType, s_lightTypes, sprite[i].picnum);
        if (nType < 0)
            continue;

        s_light[s_lights].nSprite = i;
        s_light[s_lights].nType   = nType;
        s_lights++;
    }

    if (s_lights)
        OSD_Printf("Kex lights: %d sources from %d types\n", s_lights, s_lightTypes);
}

// Once per tick, after UndoFlashes() has cleared the previous one.
// ---------------------------------------------------------------------------
// The hit flash. The remaster blinks a creature white for an instant when it
// takes damage. runlist_DamageEnemy() marks the sprite, analyzesprites()
// draws a marked sprite fullbright in the flash palette -- pal kKexFlashPal,
// which exhumed.def tints pure white with a screen blend, so the hires art
// comes out as a white silhouette -- and the mark decays per tick.
// ---------------------------------------------------------------------------
static uint8_t s_flash[MAXSPRITES];
static uint8_t s_flashGhost[MAXSPRITES];
static uint8_t s_flashSeen[MAXSPRITES];
static int     s_flashTest = -1;    // VB_KEX_FLASHTEST=1: every creature and prop held in the flash
static int     s_flashAudit = -1;   // VB_KEX_FLASH_AUDIT=1: log mark -> rendered coverage

// The flash palette. A hires tile is whitened by exhumed.def's tint on pal
// 30 (a screen blend with white, applied to the pixels when the texture is
// loaded for that pal). A tile still drawn from the original 8-bit art goes
// through a palette lookup instead, and pal 30 had none, so it drew in pal
// 0: this builds one that maps every colour to the palette's whitest entry.
static void KexFlash_MakePal(void)
{
    static int done;
    if (done) return;
    done = 1;
    int best = 0, bestl = -1;
    for (int i = 0; i < 256; i++)
    {
        int const l = palette[i * 3] + palette[i * 3 + 1] + palette[i * 3 + 2];
        if (l > bestl) { bestl = l; best = i; }
    }
    char remap[256];
    Bmemset(remap, best, sizeof(remap));
    paletteMakeLookupTable(kKexFlashPal, remap, 63, 63, 63, 1);
}

static int KexFlash_AuditEnabled(void)
{
    if (s_flashAudit < 0)
        s_flashAudit = g_kexMode && Bgetenv("VB_KEX_FLASH_AUDIT") ? 1 : 0;
    return s_flashAudit;
}

static void KexFlash_Mark(int nSprite, char const *source)
{
    if (g_kexMode && (unsigned)nSprite < MAXSPRITES)
    {
        KexFlash_MakePal();
        s_flash[nSprite] = 4;
        s_flashSeen[nSprite] = 0;
        if (KexFlash_AuditEnabled())
            OSD_Printf("Kex hit flash: %s mark sprite %d stat %d\n",
                       source, nSprite, (int)sprite[nSprite].statnum);
    }
}

void KexFlash_Hit(int nSprite)       { KexFlash_Mark(nSprite, "direct"); }
void KexFlash_RadialHit(int nSprite) { KexFlash_Mark(nSprite, "radial"); }

int KexFlash_Active(int nSprite)
{
    if ((unsigned)nSprite >= MAXSPRITES)
        return 0;
    if (s_flashTest < 0)
        s_flashTest = g_kexMode && Bgetenv("VB_KEX_FLASHTEST") ? 1 : 0;
    if (s_flashTest && ((sprite[nSprite].statnum > 100 && sprite[nSprite].statnum < 200) || KexBreak_IsProp(nSprite)))
    {
        KexFlash_MakePal();
        return 1;
    }
    int const active = s_flash[nSprite] > 0;
    if (active && KexFlash_AuditEnabled() && !s_flashSeen[nSprite])
    {
        s_flashSeen[nSprite] = 1;
        OSD_Printf("Kex hit flash: rendered sprite %d%s\n", nSprite,
                   s_flashGhost[nSprite] ? " fatal ghost" : "");
    }
    return active;
}

void KexFlash_Tick(void)
{
    for (int i = 0; i < MAXSPRITES; i++)
    {
        if (s_flash[i] && --s_flash[i] == 0 && s_flashGhost[i])
        {
            s_flashGhost[i] = 0;
            if (sprite[i].statnum < kMaxStatus)
                mydeletesprite(i);
        }
    }
}

// A fatal hit used to delete a vase in the same simulation tick that marked
// it, so no rendered frame could ever contain the white blink. Keep a
// nonblocking visual copy for the remaining flash ticks while the real prop
// shatters immediately and retains its gameplay timing.
static void KexFlash_FatalProp(int nSprite)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites
        || sprite[nSprite].sectnum < 0)
        return;
    int const nGhost = insertsprite(sprite[nSprite].sectnum, 0);
    if (nGhost < 0)
        return;

    KexFlash_MakePal();
    spritetype const src = sprite[nSprite];
    auto &dst = sprite[nGhost];
    dst.x = src.x; dst.y = src.y; dst.z = src.z;
    dst.cstat = src.cstat & ~(0x101 | 0x8000);
    dst.picnum = src.picnum;
    dst.shade = -128;
    dst.pal = kKexFlashPal;
    dst.clipdist = 0;
    dst.xrepeat = src.xrepeat; dst.yrepeat = src.yrepeat;
    dst.xoffset = src.xoffset; dst.yoffset = src.yoffset;
    dst.ang = src.ang;
    dst.owner = -1; dst.lotag = 0; dst.hitag = 0; dst.extra = -1;
    dst.xvel = dst.yvel = dst.zvel = 0;
    s_flash[nGhost] = 4;
    s_flashGhost[nGhost] = 1;
    s_flashSeen[nGhost] = 0;
    if (KexFlash_AuditEnabled())
        OSD_Printf("Kex hit flash: fatal prop sprite %d -> ghost %d\n",
                   nSprite, nGhost);
}

int KexBreak_IsProp(int nSprite)
{
    for (int i = 0; i < s_props; i++)
        if (s_prop[i].nSprite == nSprite)
            return 1;
    return 0;
}

void KexLight_Tick(void)
{
    if (polymost_kexVertexLight)
        return;
    for (int i = 0; i < s_lights; i++)
    {
        spritetype const &s = sprite[s_light[i].nSprite];
        if (s.sectnum < 0 || (s.cstat & 0x8000))    // deleted or invisible
            continue;

        KexLightType const &t = s_lightType[s_light[i].nType];
        Kex_SpriteGlow(s.sectnum, s.x, s.y, t.peak, t.shift, 0);
    }

    // These are transient light sources, not map lights. They are rendered by
    // the native bullet controller as generated KEX tiles, so scan the bullet
    // status list here and give the authored light-emitting shots their glow
    // for the current tick. The vertex-light path already contains the
    // authored static lighting and intentionally does not accumulate dynamic
    // lights.
    if (g_kexMode)
        for (int i = 0; i < MAXSPRITES; i++)
            if (sprite[i].statnum == 200 && sprite[i].sectnum >= 0 && !(sprite[i].cstat & 0x8000))
            {
                int const peak = sprite[i].picnum == kKexAnubisFireTile ? 32
                    : sprite[i].picnum == kKexBlasterTile ? 24
                    : sprite[i].picnum == kKexSelkisCobraTile ? 24 : 0;
                if (peak)
                    Kex_SpriteGlow(sprite[i].sectnum, sprite[i].x, sprite[i].y, peak, 6, 0);
            }
}

// ---------------------------------------------------------------------------
// Sound aliases. kexmap.py writes kexsound.txt out of a curated pairing of the
// Build sound roster with the remaster's sounds/*.wav -- "<name> <path> <loop>"
// per line. LoadSound() asks here first, so the remaster's own recordings play
// wherever one corresponds; the 69 Build sounds with no counterpart fall
// through to their VOCs. The loop flag replaces the VOC-block test, which a
// WAV cannot answer.
// ---------------------------------------------------------------------------
#define kMaxKexSounds 200

// KEX actor scripts contain k_electrocute, which is thirteen characters.
// Keep the full script identifier: the old eleven-character parser silently
// made that otherwise valid alias impossible to look up.
struct KexSoundAlias { char name[24]; char path[32]; uint8_t loop; };

static KexSoundAlias s_soundAlias[kMaxKexSounds];
static int           s_soundAliases = -1;    // -1 = not read yet

static bool KexSound_ParseLine(char *line, int nRec)
{
    KexSoundAlias &a = s_soundAlias[nRec];
    int loop = 0;
    if (Bsscanf(line, "%23s %31s %d", a.name, a.path, &loop) < 2)
        return false;
    a.loop = !!loop;
    return true;
}

char const *KexSound_Alias(char const *name, int *pLoop)
{
    if (s_soundAliases < 0)
        s_soundAliases = KexTable_Load("kexsound.txt", kMaxKexSounds, KexSound_ParseLine);
    if (!g_kexMode)
        return NULL;
    for (int i = 0; i < s_soundAliases; i++)
        if (!Bstrncasecmp(s_soundAlias[i].name, name, sizeof(s_soundAlias[i].name) - 1))
        {
            if (pLoop)
                *pLoop = s_soundAlias[i].loop;
            return s_soundAlias[i].path;
        }
    return NULL;
}

// A sound the remaster has and Build never did -- the transmitter piece, the
// mine, the boulder, the Anubis and Set melee -- named in kexsound.txt under
// a name no VOC carries (k_*). LoadSound() resolves any name through the
// aliases, so it only needs asking; the index is cached, and -1 (no sound
// device, no alias) plays nothing.
// These aliases are requested lazily by enemy and trap code and remain cached
// across level changes. The current Kex paths use 34 distinct names, so the
// old 16-entry pool silently disabled whichever sounds were encountered later
// in a campaign.
#define kMaxKexOwnSounds 64
static struct { char name[24]; int nSound; } s_ownSound[kMaxKexOwnSounds];
static int s_ownSounds;

int KexSound_Get(char const *name)
{
    for (int i = 0; i < s_ownSounds; i++)
        if (!Bstrcasecmp(s_ownSound[i].name, name))
            return s_ownSound[i].nSound;
    if (s_ownSounds >= kMaxKexOwnSounds)
        return -1;
    int const n = g_kexMode && KexSound_Alias(name, NULL) ? LoadSound(name) : -1;
    Bstrncpy(s_ownSound[s_ownSounds].name, name, sizeof(s_ownSound[0].name) - 1);
    s_ownSound[s_ownSounds].name[sizeof(s_ownSound[0].name) - 1] = 0;
    s_ownSound[s_ownSounds++].nSound = n;
    return n;
}

// ---------------------------------------------------------------------------
// Teleports. Kex event 71 is a pad the player steps on and event 72 is the
// arrival point. Build PowerSlave has no sector teleporter, so kexmap.py
// writes kextele.txt -- "<map> <padsector> <destsector> <x> <y> <z> <ang>" --
// and stepping into the pad's sector moves the player the way RestartPlayer()
// places one. A negative angle preserves heading, which is Kex's native
// behavior; nonnegative angles remain accepted for old generated tables.
// One-shot per visit: leaving the pad re-arms it. There is exactly one pair in
// the retail maps, in KILARENA.
// ---------------------------------------------------------------------------
#define kMaxKexTeles 16

struct KexTele
{
    char map[16];
    int16_t pad, dest, ang;
    int32_t x, y, z;
};

static KexTele s_teleAll[64];
static int     s_teleAllCount = -1;        // -1 = not read yet
static KexTele s_tele[kMaxKexTeles];       // this level's rows
static int     s_teles;
static uint8_t s_teleOnPad[kMaxPlayers];
// kexHud::SetTeleportFlash starts at 255 and kexHud::Update subtracts four
// each 60 Hz update. PCExhumed advances gameplay at 30 Hz, so each game tick
// represents the two source updates and subtracts eight.
static int     s_teleFlash;
static int     s_teleTest, s_teleTestPhase, s_teleTestTicks;
static int16_t s_teleTestAng;
static fix16_t s_teleTestQAng, s_teleTestOAng;

static bool KexTele_ParseLine(char *line, int nRec)
{
    KexTele &t = s_teleAll[nRec];
    int pad, dest, x, y, z, ang;
    if (Bsscanf(line, "%15s %d %d %d %d %d %d", t.map, &pad, &dest,
                &x, &y, &z, &ang) < 7)
        return false;
    t.pad = (int16_t)pad; t.dest = (int16_t)dest;
    t.x = x; t.y = y; t.z = z;
    t.ang = ang < 0 ? -1 : (int16_t)(ang & kAngleMask);
    return true;
}

void KexTele_Init(void)
{
    s_teles = 0;
    Bmemset(s_teleOnPad, 0, sizeof(s_teleOnPad));
    s_teleFlash = 0;
    s_teleTest = s_teleTestPhase = s_teleTestTicks = 0;
    if (s_teleAllCount < 0)
        s_teleAllCount = KexTable_Load("kextele.txt", (int)ARRAY_SIZE(s_teleAll),
                                       KexTele_ParseLine);
    if (!g_kexMode)
        return;

    char nameUp[BMAX_PATH];
    Bstrncpyz(nameUp, gUserMapFilename, sizeof(nameUp));
    Bstrupr(nameUp);
    for (int i = 0; i < s_teleAllCount && s_teles < kMaxKexTeles; i++)
    {
        if (Bstrstr(nameUp, s_teleAll[i].map))
            s_tele[s_teles++] = s_teleAll[i];
    }
    if (s_teles)
        OSD_Printf("Kex teleports: %d pads on this level\n", s_teles);
    s_teleTest = g_kexMode && Bgetenv("VB_KEXTELE_TEST") != NULL;
    if (s_teleTest && !s_teles)
    {
        OSD_Printf(OSD_ERROR "Kex teleport test: FAIL (no pad on this level)\n");
        s_teleTest = 0;
    }
}

int KexTele_FlashAlpha(void)
{
    return g_kexMode ? s_teleFlash : 0;
}

void KexTele_Tick(void)
{
    if (s_teleFlash > 0)
        s_teleFlash = max(0, s_teleFlash - 8);

    // A focused, headless test may select a pad without requiring input. It
    // proves both the source event direction and the native no-facing-change
    // rule against kextele.txt's exact destination row.
    if (s_teleTest && s_teleTestPhase == 0)
    {
        int const nSprite = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nSprite < (unsigned)kMaxSprites)
        {
            s_teleTestAng = sprite[nSprite].ang;
            s_teleTestQAng = PlayerList[nLocalPlayer].q16angle;
            s_teleTestOAng = PlayerList[nLocalPlayer].q16oangle;
            mychangespritesect(nSprite, s_tele[0].pad);
            sprite[nSprite].z = getflorzofslope(s_tele[0].pad,
                                                sprite[nSprite].x,
                                                sprite[nSprite].y);
            s_teleTestPhase = 1;
            OSD_Printf("Kex teleport test: forcing pad sector %d -> %d\n",
                       s_tele[0].pad, s_tele[0].dest);
        }
        else if (++s_teleTestTicks == 120)
        {
            OSD_Printf(OSD_ERROR "Kex teleport test: FAIL (player unavailable)\n");
            s_teleTest = 0;
        }
    }
    for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
    {
        int const nSprite = PlayerList[nPlayer].nSprite;
        if (nSprite < 0)
            continue;
        int const nSector = sprite[nSprite].sectnum;
        int onPad = 0;
        for (int i = 0; i < s_teles; i++)
        {
            if (nSector != s_tele[i].pad)
                continue;
            if (!KexPlayer_OnSectorFloor(nPlayer, nSector))
                continue;               // every non-type-50 special is grounded
            onPad = 1;
            if (s_teleOnPad[nPlayer])
                break;

            sprite[nSprite].x = s_tele[i].x;
            sprite[nSprite].y = s_tele[i].y;
            sprite[nSprite].z = s_tele[i].z;
            // TeleportEvent assigns only Origin and Sector.  In particular,
            // it leaves the puppet's motion alone, so a player carries the
            // same momentum through a Kex pad instead of being stopped.
            mychangespritesect(nSprite, s_tele[i].dest);
            if (s_tele[i].ang >= 0) // legacy generated tables supplied a facing
            {
                PlayerList[nPlayer].q16angle = fix16_from_int(s_tele[i].ang);
                PlayerList[nPlayer].q16oangle = PlayerList[nPlayer].q16angle;
                sprite[nSprite].ang = s_tele[i].ang;
            }
            PlayerList[nPlayer].opos = sprite[nSprite].xyz;

            bool const testTeleport = s_teleTest && nPlayer == nLocalPlayer
                && s_teleTestPhase == 1;
            if (testTeleport)
            {
                int const okay = sprite[nSprite].sectnum == s_tele[i].dest
                    && sprite[nSprite].x == s_tele[i].x
                    && sprite[nSprite].y == s_tele[i].y
                    && sprite[nSprite].z == s_tele[i].z
                    && sprite[nSprite].ang == s_teleTestAng
                    && PlayerList[nPlayer].q16angle == s_teleTestQAng
                    && PlayerList[nPlayer].q16oangle == s_teleTestOAng;
                OSD_Printf("Kex teleport test: %s (71 pad -> 72 destination, heading %s)\n",
                           okay ? "PASS" : "FAIL", okay ? "preserved" : "changed");
                s_teleTest = 0;
            }

            OSD_Printf("Kex teleport: player %d -> sector %d\n",
                       nPlayer, s_tele[i].dest);
            // TeleportEvent plays teleport.wav and starts the green HUD flash
            // only for kexPuppet, i.e. the local player.  The old Build
            // tele_1/spawn.wav plus a world dlight was neither source effect.
            if (nPlayer == nLocalPlayer)
            {
                int const sound = KexSound_Get("k_teleport");
                if (sound >= 0)
                    D3PlayFX(sound, nSprite);
                // TeleportFlash is a dedicated 0,255,8 HUD overlay, not a
                // world light or a persistent palette alteration. DrawView
                // composites it exactly on hardware renderers; retain the
                // old palette approximation only for the classic renderer.
                s_teleFlash = 255;
#ifdef USE_OPENGL
                if (videoGetRenderMode() < REND_POLYMOST)
#endif
                    TintPalette(0, 255, 8);
                if (testTeleport)
                {
                    char const * const path = KexSound_Alias("k_teleport", NULL);
                    OSD_Printf("Kex teleport sound test: %s (sounds/teleport.wav, local green flash)\n",
                               path && !Bstrcasecmp(path, "sounds/teleport.wav") ? "PASS" : "FAIL");
                }
            }
            onPad = 0;                     // arrived elsewhere; pad re-arms
            break;
        }
        s_teleOnPad[nPlayer] = (uint8_t)onPad;
    }
}

int KexBreak_Available(void)
{
    if (s_types < 0)
        KexBreak_LoadTable();
    return g_kexMode && s_types > 0;
}

// Arm one prop: solid and hitscannable, and holding a run so that the damage
// message finds it. This mirrors BuildObject() -- the sprite's lotag is the
// run index plus one, which is where runlist_DamageEnemy() looks.
static void KexBreak_Arm(int nSprite, int nType)
{
    if (s_props >= kMaxKexBreaks)
        return;

    int const nProp = s_props++;

    sprite[nSprite].cstat |= 0x101;      // blocking + hitscan
    // Build clips a sprite at clipdist<<2, so the prop blocks the same circle
    // the remaster gives it rather than Build's default.
    sprite[nSprite].clipdist = (uint8_t)s_type[nType].clipdist;
    sprite[nSprite].lotag  = runlist_HeadRun() + 1;
    sprite[nSprite].hitag  = 0;
    sprite[nSprite].extra  = -1;
    sprite[nSprite].owner  = runlist_AddRunRec(sprite[nSprite].lotag - 1, nProp, kRunKexBreak);

    s_prop[nProp].nSprite  = nSprite;
    s_prop[nProp].nType    = nType;
    s_prop[nProp].nHealth  = s_type[nType].health;
    s_prop[nProp].nFuse    = -1;
    s_prop[nProp].nRunFree = runlist_AddRunRec(NewRun, nProp, kRunKexBreak);
}

static void KexBreak_Shatter(int nProp);

static void KexBreak_PlaySound(KexBreakType const &t, int nSprite)
{
    int sound = -1;
    switch (t.breaksound)
    {
        case kKexBreakSoundNone:
            break;
        case kKexBreakSoundGlass:
            sound = StaticSound[kSoundPotPc1];
            break;
        case kKexBreakSoundWood:
            sound = StaticSound[kSoundPotPc1 + 1];  // pot_pc2 follows pot_pc1
            break;
        case kKexBreakSoundFlesh:
            sound = KexSound_Get("blood_4");
            break;
    }
    if (s_soundTest && (unsigned)t.breaksound < kKexBreakSoundCount)
        s_soundPlayed[t.breaksound]++;
    if (sound >= 0)
        PlayFX2(sound, nSprite);
}

static KexTossBounceSound KexBreak_ShardBounceSound(KexBreakType const &t)
{
    // Exploding vase death animations have no local break sound, but their
    // shards still declare glass01..03. The ordinary local recipe otherwise
    // matches the shard actor's bounceSound_1..3 family exactly.
    switch (t.breaksound)
    {
        case kKexBreakSoundWood:  return kKexTossBounceSoundWood;
        case kKexBreakSoundFlesh: return kKexTossBounceSoundFlesh;
        default:                  return kKexTossBounceSoundGlass;
    }
}

// magic8ball_detonate frame zero always executes A_PlayLocalSound followed by
// A_StartCountdown, whether ChangeAnim came from sight or from the death anim.
// Keep those two local transitions together: its actor stops being shootable
// while the countdown runs, but remains solid until its explosion callback.
static void KexBreak_ArmMine(int nProp)
{
    KexBreakType const &t = s_type[s_prop[nProp].nType];
    if (t.fuse <= 0 || s_prop[nProp].nFuse >= 0)
        return;
    int const nSprite = s_prop[nProp].nSprite;
    s_prop[nProp].nHealth = 1;
    s_prop[nProp].nFuse = t.fuse;
    sprite[nSprite].cstat &= ~0x100;   // A_StartCountdown clears AF_SHOOTABLE
    // t.tile is idle. The conversion writes the five detonate poses followed
    // by the three-frame loop immediately after it, matching ChangeAnim's
    // immediate frame-zero action before the first native tick.
    sprite[nSprite].picnum = t.tile + 1;
    sprite[nSprite].shade = 0;
    int const sound = KexSound_Get("k_mine");
    PlayFX2(sound >= 0 ? sound : StaticSound[kSoundItemSpecial], nSprite);
}

static int KexBreak_MinePicnum(KexBreakType const &t, int nFuse)
{
    // magic8ball_detonate advances frames 1..4 on bridge ticks 1..4; its
    // fifth transition changes to loop frame 0 and performs decrement one.
    // The loop then runs frame 0, 1, 2 at one frame per tick. Frame zero was
    // displayed by KexBreak_ArmMine itself, so nFuse==t.fuse is not queried.
    constexpr int kDetonateFrames = 5;
    constexpr int kLoopFrames = 3;
    int const elapsed = t.fuse - nFuse;
    if (elapsed < kDetonateFrames)
        return t.tile + 1 + elapsed;
    return t.tile + 1 + kDetonateFrames
        + (elapsed - kDetonateFrames) % kLoopFrames;
}

// The mine (AI_Magic8Ball): arms when a player it can see comes within its
// sight radius, or when it is destroyed -- its death anim is the countdown --
// and blasts when the fuse runs out; under water it also drops a random item,
// the remaster's magic8ball_explode_water. Its authored detonate/loop poses
// replace the old synthetic shade blink while it counts.
static void KexBreak_MineTick(int nProp)
{
    KexBreakType const &t = s_type[s_prop[nProp].nType];
    int const nSprite = s_prop[nProp].nSprite;
    if (t.sight <= 0 && t.fuse <= 0)
        return;
    if (s_prop[nProp].nFuse < 0)
    {
        for (int i = 0; i < nTotalPlayers; i++)
        {
            int const nPlayer = PlayerList[i].nSprite;
            if (nPlayer < 0 || PlayerList[i].nHealth <= 0)
                continue;
            // A_CanSeeTarget calls kexActor::CanSee(): the Magic 8 Ball's
            // eye is origin + height/2 + stepHeight (16 + 16 KEX units),
            // its target is the player's centre, and maxDistance is a full
            // 3-D Euclidean radius. t.sight is already WORLD-scaled in XY;
            // Build Z / 16 is in that same basis.
            int const sourceZ = sprite[nSprite].z - 32 * 16 * 6;
            int const targetZ = sprite[nPlayer].z - (160 * 16 * 6) / 2;
            int64_t const dx = (int64_t)sprite[nPlayer].x - sprite[nSprite].x;
            int64_t const dy = (int64_t)sprite[nPlayer].y - sprite[nSprite].y;
            int64_t const dz = ((int64_t)targetZ - sourceZ) / 16;
            if (dx * dx + dy * dy + dz * dz > (int64_t)t.sight * t.sight)
                continue;
            if (!cansee(sprite[nSprite].x, sprite[nSprite].y, sourceZ,
                        sprite[nSprite].sectnum, sprite[nPlayer].x,
                        sprite[nPlayer].y, targetZ, sprite[nPlayer].sectnum))
                continue;
            KexBreak_ArmMine(nProp);
            if (s_breakSelftestActive)
                OSD_Printf("Kex mine: prop %d armed by player %d\n", nProp, i);
            break;
        }
        return;
    }
    if (--s_prop[nProp].nFuse > 0)
    {
        sprite[nSprite].picnum = KexBreak_MinePicnum(t, s_prop[nProp].nFuse);
        if (nProp == s_mineTestProp)
        {
            s_mineTestPassed &= sprite[nSprite].picnum
                == KexBreak_MinePicnum(t, s_prop[nProp].nFuse);
            s_mineTestSteps++;
        }
        return;
    }
    if (SectFlag[sprite[nSprite].sectnum] & kSectUnderwater)
        KexDrop_RandomItem(nSprite, -1);
    s_prop[nProp].nHealth = 0;
    if (nProp == s_mineTestProp)
    {
        bool const okay = s_mineTestPassed && s_mineTestSteps == t.fuse - 1;
        OSD_Printf("Kex mine countdown test: %s (%d/%d authored pose ticks)\n",
                   okay ? "PASS" : "FAIL", s_mineTestSteps, t.fuse - 1);
        s_mineTestProp = -1;
    }
    if (s_breakSelftestActive)
        OSD_Printf("Kex mine: prop %d blasts\n", nProp);
    KexBreak_Shatter(nProp);
}

void KexBreak_Init(void)
{
    Bmemset(s_flash, 0, sizeof(s_flash));
    Bmemset(s_flashGhost, 0, sizeof(s_flashGhost));
    Bmemset(s_flashSeen, 0, sizeof(s_flashSeen));
    KexDrop_LoadTable();
    s_kexPowerTimer = 5;
    s_vaseShots = 0;
    for (auto &s : s_vaseShot)
        s.nSprite = -1;
    s_props = 0;
    { char const *e = getenv("VB_KEXBREAK_TEST"); s_selftest = e ? Batoi(e) : 0; }
    s_breakSelftestActive = 0;
    s_soundTest = Bgetenv("VB_KEXBREAK_SOUND_TEST") != NULL;
    Bmemset(s_soundExpected, 0, sizeof(s_soundExpected));
    Bmemset(s_soundPlayed, 0, sizeof(s_soundPlayed));
    s_vasePuffsExpected = 0;
    s_vasePuffsSpawned = 0;
    s_scorpTest = s_selftest && Bgetenv("VB_KEXBREAK_SCORPTEST") != NULL;
    s_vaseScorpsSpawned = 0;
    s_vaseScorpsAtHeight = 0;
    s_mineTest = Bgetenv("VB_KEX_MINE_TEST") != NULL;
    s_mineTestProp = -1;
    s_mineTestSteps = 0;
    s_mineTestPassed = 1;
    s_orbTest = Bgetenv("VB_KEXDROP_ORB_TEST") != NULL;
    s_orbTestStarted = 0;
    s_orbTestExpected = s_orbTestCallbacks = s_orbTestPassed = 0;
    Bmemset(s_orbTestEffect, 0, sizeof(s_orbTestEffect));
    if (!KexBreak_Available())
    {
        if (s_soundTest)
            OSD_Printf("Kex break sound table test: FAIL (kexbreak.txt unavailable)\n");
        return;
    }

    // The converter emits these as ordinary decor sprites, so they arrive in
    // statnum 0 with the prop's tile as their picnum.
    for (int nSprite = headspritestat[0]; nSprite != -1; )
    {
        int const nNext = nextspritestat[nSprite];
        int const nType = KexTable_FindTile(s_type, s_types, sprite[nSprite].picnum);
        if (nType >= 0)
        {
            KexBreak_Arm(nSprite, nType);
            if (s_type[nType].blastdamage <= 0 && s_type[nType].nshards > 0)
                s_vasePuffsExpected++;
        }
        nSprite = nNext;
    }
    if (s_props)
        OSD_Printf("Kex breakables: %d props armed from %d types\n", s_props, s_types);
    if (s_mineTest)
    {
        for (int i = 0; i < s_props; i++)
        {
            KexBreakType const &t = s_type[s_prop[i].nType];
            if (t.fuse <= 0)
                continue;
            KexBreak_ArmMine(i);
            int const nSprite = s_prop[i].nSprite;
            s_mineTestProp = i;
            s_mineTestPassed = sprite[nSprite].picnum == t.tile + 1
                && !(sprite[nSprite].cstat & 0x100);
            break;
        }
        if (s_mineTestProp < 0)
            OSD_Printf("Kex mine countdown test: FAIL (no Magic 8 Ball in map)\n");
    }
    if (s_soundTest)
    {
        int typeCount[kKexBreakSoundCount] = {};
        for (int i = 0; i < s_types; i++)
            if ((unsigned)s_type[i].breaksound < kKexBreakSoundCount)
                typeCount[s_type[i].breaksound]++;
        for (int i = 0; i < s_props; i++)
            s_soundExpected[s_type[s_prop[i].nType].breaksound]++;
        // A normal retail conversion contains ten glass types and one flesh
        // vase.  --testmaps adds the otherwise unplaced Obj_Vase11 as the
        // eleventh glass type.  Old eleven-column exports predate the flesh
        // recipe, so retain their 3/11/1/0 compatibility check separately.
        int const glass = typeCount[kKexBreakSoundGlass];
        bool const normal = typeCount[kKexBreakSoundNone] == 3
                         && (glass == 10 || glass == 11)
                         && typeCount[kKexBreakSoundWood] == 1
                         && typeCount[kKexBreakSoundFlesh] == 1;
        bool const legacy = typeCount[kKexBreakSoundNone] == 3
                         && glass == 11
                         && typeCount[kKexBreakSoundWood] == 1
                         && typeCount[kKexBreakSoundFlesh] == 0;
        bool const okay = s_legacyBreakTable ? legacy : normal;
        OSD_Printf("Kex break sound table test: %s%s (none %d, glass %d, wood %d, flesh %d)\n",
                   okay ? "PASS" : "FAIL", s_legacyBreakTable ? " legacy fallback" : "",
                   typeCount[kKexBreakSoundNone],
                   typeCount[kKexBreakSoundGlass], typeCount[kKexBreakSoundWood],
                   typeCount[kKexBreakSoundFlesh]);
    }
}

void ProcessSpriteTag(short nSprite, short nLotag, short nHitag);

// A_RandomVaseItem, transcribed from the remaster's own scripts/p_misc.txt --
// it ships the AngelScript in the .kpf, so this is the real table rather than
// a guess:
//
//     r = Max(8)
//     case 0: scepter ? Item_Ammo : spawn AI_Scorp
//     case 1: sandals ? AI_Wasp : fall through
//     case 2: scepter ? bouncy balls : fall through
//     case 3: Item_Ammo
//     case 4: health > 100 && coinflip ? nothing : fall through
//     case 5: Item_Health
//     case 6: shawl ? three cobras : nothing
//     default: nothing
//
// The Sandals-gated wasp branch is supported by Build's existing wasp
// constructor. The Scepter fireball swarm and Shawl cobra actors use the
// small Kex-break projectile path below because the classic game has no
// equivalent constructors. The rare Item_Map roll is supported with the
// native lotag-11 pickup. Item_Power uses Build's matching timed weapon boost;
// Item_Ammo and Item_Health use KEX-only pickup lotags because their current-
// weapon and scale-dependent rules have no classic equivalent. Pharaoh's
// half-empty-vase and uncapped power-drop branches use the shared KEX
// difficulty selected by the remaster menu.
// Stand an item up at a point and let the item machinery finish it.
static int KexDrop_Spawn(int nSector, int x, int y, int z,
                         short lotag, short hitag, short picnum)
{
    int const nItem = insertsprite(nSector, 0);
    if (nItem < 0)
        return -1;

    sprite[nItem].x = x;
    sprite[nItem].y = y;
    sprite[nItem].z = z;
    sprite[nItem].cstat = 0;
    sprite[nItem].shade = -12;
    sprite[nItem].pal = 0;
    sprite[nItem].xrepeat = 64;
    sprite[nItem].yrepeat = 64;
    sprite[nItem].xoffset = 0;
    sprite[nItem].yoffset = 0;
    sprite[nItem].picnum = picnum;
    sprite[nItem].ang = 0;
    sprite[nItem].xvel = sprite[nItem].yvel = sprite[nItem].zvel = 0;
    sprite[nItem].owner = -1;
    sprite[nItem].extra = -1;

    ProcessSpriteTag(nItem, lotag, hitag);
    return nItem;
}

// A_RandomItem does not create its pickup immediately. It creates one of two
// translucent, fullbright orb-build actors, waits through a blank lead-in and
// the visible assembly, then A_SpawnOrbPickup copies its scale to the item.
// Keep the eventual lotag/value/art in fields BuildAnim otherwise leaves free.
static int KexDrop_SpawnOrb(int nSector, int x, int y, int z,
                            short lotag, short hitag, short picnum)
{
    int const nSeq = lotag == kKexLotagAmmo
        ? kSeqKexAmmoSpawn : kSeqKexHealthSpawn;
    if (!g_kexMode || !seq_IsLoaded(nSeq))
        return -1;

    int const nAnim = BuildAnim(-1, nSeq, 0, x, y, z, nSector,
                                hitag == 2 || hitag == 50 ? 48 : 24,
                                4 | 0x80);
    if (nAnim < 0)
        return -2;

    int const nSprite = GetAnimSprite(nAnim);
    sprite[nSprite].xvel = lotag;
    sprite[nSprite].yvel = hitag;
    sprite[nSprite].zvel = picnum;
    return nAnim;
}

void KexDrop_OrbSpawnPickup(int nEffectSprite)
{
    if (!g_kexMode || (unsigned)nEffectSprite >= (unsigned)kMaxSprites)
        return;

    short const lotag = sprite[nEffectSprite].xvel;
    short const hitag = sprite[nEffectSprite].yvel;
    short const picnum = sprite[nEffectSprite].zvel;
    if ((lotag != kKexLotagAmmo && lotag != kKexLotagHealth)
            || picnum <= 0)
        return;

    int const nItem = KexDrop_Spawn(sprite[nEffectSprite].sectnum,
        sprite[nEffectSprite].x, sprite[nEffectSprite].y,
        sprite[nEffectSprite].z, lotag, hitag, picnum);

    int const testKind = s_orbTestEffect[nEffectSprite];
    if (testKind)
    {
        bool const ammo = testKind == 1;
        bool const okay = nItem >= 0
            && lotag == (ammo ? kKexLotagAmmo : kKexLotagHealth)
            && hitag == (ammo ? 1 : 50)
            && picnum == (ammo ? s_kexAmmoTile : s_kexHealthTile)
            && sprite[nEffectSprite].xrepeat == (ammo ? 24 : 48)
            && (sprite[nEffectSprite].cstat & 2)
            && sprite[nItem].sectnum == sprite[nEffectSprite].sectnum
            && sprite[nItem].x == sprite[nEffectSprite].x
            && sprite[nItem].y == sprite[nEffectSprite].y
            && sprite[nItem].z == sprite[nEffectSprite].z
            && sprite[nItem].hitag == hitag;
        s_orbTestPassed += okay;
        s_orbTestCallbacks++;
        s_orbTestEffect[nEffectSprite] = 0;
        if (s_orbTestCallbacks == s_orbTestExpected)
            OSD_Printf("Kex enemy-drop orb test: %s (%d/%d exact 20+23-tick delayed pickups)\n",
                       s_orbTestPassed == s_orbTestExpected ? "PASS" : "FAIL",
                       s_orbTestPassed, s_orbTestExpected);
    }
}

static void KexDrop_OrbSelfTest(void)
{
    s_orbTestStarted = 1;
    s_orbTestExpected = 2;
    int const nPlayer = PlayerList[nLocalPlayer].nSprite;
    if ((unsigned)nPlayer >= (unsigned)kMaxSprites
            || s_kexAmmoTile <= 0 || s_kexHealthTile <= 0)
    {
        OSD_Printf("Kex enemy-drop orb test: FAIL (test actor/art unavailable)\n");
        return;
    }

    int const nAmmo = KexDrop_SpawnOrb(sprite[nPlayer].sectnum,
        sprite[nPlayer].x - 96, sprite[nPlayer].y, sprite[nPlayer].z,
        kKexLotagAmmo, 1, s_kexAmmoTile);
    int const nHealth = KexDrop_SpawnOrb(sprite[nPlayer].sectnum,
        sprite[nPlayer].x + 96, sprite[nPlayer].y, sprite[nPlayer].z,
        kKexLotagHealth, 50, s_kexHealthTile);
    if (nAmmo < 0 || nHealth < 0)
    {
        OSD_Printf("Kex enemy-drop orb test: FAIL (exact effect allocation unavailable)\n");
        return;
    }
    s_orbTestEffect[GetAnimSprite(nAmmo)] = 1;
    s_orbTestEffect[GetAnimSprite(nHealth)] = 2;
}

// The Scepter vase branch fires twenty Proj_Ra_Fire actors with no target.
// Build's bullet pool has fixed directional motion, so these use the existing
// Kex-break run type with a small independent motion record. The movement
// follows the same collision flags as CreatureChunk: walls reflect the
// horizontal vector, floors/ceilings reflect Z, and an enemy takes the Kex
// projectile's 20 damage.
static void KexVaseShot_Remove(int nShot)
{
    KexVaseShot &s = s_vaseShot[nShot];
    int const nSprite = s.nSprite;
    if (nSprite < 0)
        return;
    for (int i = 0; i < (int)ARRAY_SIZE(s.nJoint); i++)
        if (s.nJoint[i] >= 0)
            mydeletesprite(s.nJoint[i]);
    runlist_DoSubRunRec(sprite[nSprite].owner);
    runlist_FreeRun(sprite[nSprite].lotag - 1);
    runlist_SubRunRec(s.nRunFree);
    s.nSprite = -1;
    mydeletesprite(nSprite);
}

static void KexVaseCobra_Explode(int nSprite)
{
    // Proj_Cobra_Green's death animation is explicit: A_CStaffExplode,
    // followed by A_RadialBlast(512, 100), then three more randomized puffs
    // at t=3/6/9. The shared hidden controller preserves that cadence after
    // this temporary projectile sprite is removed.
    bool const exact = KexProjectile_StartCobraBurst(
        sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z,
        sprite[nSprite].sectnum, 0) != -1;
    runlist_RadialDamageEnemy(nSprite, 100, 512 * 6 / 256);
    if (!exact)
    {
        BuildAnim(-1, 23, 0, sprite[nSprite].x, sprite[nSprite].y,
                  sprite[nSprite].z, sprite[nSprite].sectnum, 40, 4);
        AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x,
                 sprite[nSprite].y, sprite[nSprite].z, 128);
    }
    if (s_breakSelftestActive)
        OSD_Printf("Kex vase: Shawl cobra impact blast (%s four-puff death)\n",
                   exact ? "exact" : "fallback");
}

static void KexVaseRa_Explode(int nSprite)
{
    // rafire_explode spawns Fx_FireballExplode and plays pbexplode.wav. The
    // orb has no radial damage on this path; its damage is dealt on contact.
    bool const exact = KexProjectile_SpawnFireballImpact(
        sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z,
        sprite[nSprite].sectnum, 2, 0) != -1;
    if (!exact)
    {
        BuildAnim(-1, 36, 0, sprite[nSprite].x, sprite[nSprite].y,
                  sprite[nSprite].z, sprite[nSprite].sectnum, 120, 4);
        AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x,
                 sprite[nSprite].y, sprite[nSprite].z, 128);
        int const sound = KexSound_Get("k_bldhit");
        if (sound >= 0)
            PlayFX2(sound, nSprite);
    }
    if (s_breakSelftestActive)
        OSD_Printf("Kex vase: Scepter Ra-fire expiry (%s +2 impact)\n",
                   exact ? "exact" : "fallback");
}

static void KexVaseShot_Tick(int nShot)
{
    KexVaseShot &s = s_vaseShot[nShot];
    int const nSprite = s.nSprite;
    if (nSprite < 0)
        return;

    if (s.cobra)
    {
        for (int i = (int)ARRAY_SIZE(s.nJoint) - 1; i > 0; i--)
        {
            s.trailx[i] = s.trailx[i - 1];
            s.traily[i] = s.traily[i - 1];
            s.trailz[i] = s.trailz[i - 1];
        }
        s.trailx[0] = sprite[nSprite].x;
        s.traily[0] = sprite[nSprite].y;
        s.trailz[0] = sprite[nSprite].z;
    }

    if (s.cobra)
    {
        int const nPlayer = PlayerList[nLocalPlayer].nSprite;
        if (nPlayer >= 0)
        {
            int const angle = GetMyAngle(sprite[nPlayer].x - sprite[nSprite].x,
                                         sprite[nPlayer].y - sprite[nSprite].y);
            int const speed = 96;
            int const tx = (Cos(angle) * speed) >> 10;
            int const ty = (Sin(angle) * speed) >> 10;
            // Proj_Cobra_Green has homingMaxPitch 1.57 and gravity 0. Use a
            // bounded vertical component as well; Build z grows downward.
            int const dx = sprite[nPlayer].x - sprite[nSprite].x;
            int const dy = sprite[nPlayer].y - sprite[nSprite].y;
            // Keep the squared distance inside ksqrt's 32-bit input even on
            // the largest converted maps.
            int const dscale = max(1, max(klabs(dx), klabs(dy)) / 16000 + 1);
            int const planar = max(1, ksqrt((dx / dscale) * (dx / dscale)
                                           + (dy / dscale) * (dy / dscale)) * dscale);
            int const tz = clamp((sprite[nPlayer].z - sprite[nSprite].z) * speed / planar,
                                 -speed, speed);
            s.vx = (s.vx * 15 + tx) / 16;
            s.vy = (s.vy * 15 + ty) / 16;
            s.vz = (s.vz * 15 + tz) / 16;
        }
    }
    int const nVal = movesprite(nSprite, s.vx, s.vy, s.vz, 16, 16, CLIPMASK1);
    if (nVal)
    {
        int const kind = nVal & 0x3C000;
        if (kind == 0xC000)
        {
            int const nHit = nVal & 0x3FFF;
            if (!s.cobra && sprite[nHit].statnum > 99 && sprite[nHit].statnum < 200)
                runlist_DamageEnemy(nHit, nSprite, 20);
        }
        if (nVal & 0x20000)
        {
            KexVaseShot_Remove(nShot);
            return;
        }
        if (s.cobra)
        {
            // Proj_Cobra_Green is impactWallsOnly: an actor contact does not
            // damage or bounce it, while a wall contact runs its death anim.
            if (kind == 0x10000 || kind == 0x8000)
            {
                KexVaseCobra_Explode(nSprite);
                KexVaseShot_Remove(nShot);
                return;
            }
        }
        else if (kind == 0x10000)
        {
            s.vz = -s.vz * 3 / 4;
        }
        else if (kind == 0x8000)
        {
            int const normal = GetWallNormal(nVal & 0x3FFF);
            int const dot = (s.vx * Cos(normal) + s.vy * Sin(normal)) >> 14;
            s.vx -= 2 * dot * Cos(normal);
            s.vy -= 2 * dot * Sin(normal);
        }
        else if (kind == 0xC000)
        {
            // Actor impact is not a wall stop for a bouncy Kex projectile;
            // keep its velocity and let the next tick move it clear.
            s.vx = -s.vx * 3 / 4;
            s.vy = -s.vy * 3 / 4;
        }
    }
    if (s.cobra)
    {
        for (int i = 0; i < (int)ARRAY_SIZE(s.nJoint); i++)
        {
            int const nJoint = s.nJoint[i];
            if (nJoint < 0)
                continue;
            sprite[nJoint].x = s.trailx[i];
            sprite[nJoint].y = s.traily[i];
            sprite[nJoint].z = s.trailz[i];
            int16_t nSector = sprite[nJoint].sectnum;
            updatesector(sprite[nJoint].x, sprite[nJoint].y, &nSector);
            if (nSector >= 0 && nSector != sprite[nJoint].sectnum)
                mychangespritesect(nJoint, nSector);
        }
    }
    if (!s.cobra)
        s.vz += 96; // light gravity, matching the remaster's bouncing orb
    if (--s.ticks <= 0)
    {
        if (!s.cobra)
            KexVaseRa_Explode(nSprite);
        KexVaseShot_Remove(nShot);
    }
}

static void KexVaseShot_Spawn(int nSector, int x, int y, int z, int cobra)
{
    int const tile = cobra ? s_kexCobraTile : s_kexRaTile;
    if (s_vaseShots >= KEX_MAX_VASE_SHOTS || tile <= 0)
        return;
    int const nSprite = insertsprite(nSector, 200);
    if (nSprite < 0)
        return;
    int const nShot = s_vaseShots++;
    KexVaseShot &s = s_vaseShot[nShot];
    Bmemset(&s, 0, sizeof(s));
    s.nSprite = nSprite;
    s.nRunFree = -1;
    for (auto &nJoint : s.nJoint)
        nJoint = -1;
    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z - (cobra ? 20 : 64) * 16 * 6;
    sprite[nSprite].cstat = 0;
    sprite[nSprite].shade = -64;
    sprite[nSprite].pal = 0;
    sprite[nSprite].xrepeat = sprite[nSprite].yrepeat = 32;
    sprite[nSprite].clipdist = 16;
    sprite[nSprite].picnum = tile;
    sprite[nSprite].owner = -1;
    sprite[nSprite].extra = -1;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1,
                                               KEX_VASE_SHOT_BASE + nShot, kRunKexBreak);
    s.nRunFree = runlist_AddRunRec(NewRun, KEX_VASE_SHOT_BASE + nShot, kRunKexBreak);

    if (cobra && s_kexCobraJointTile > 0)
    {
        for (int i = 0; i < (int)ARRAY_SIZE(s.nJoint); i++)
        {
            int const nJoint = insertsprite(nSector, 200);
            if (nJoint < 0)
                break;
            s.nJoint[i] = nJoint;
            s.trailx[i] = x; s.traily[i] = y; s.trailz[i] = sprite[nSprite].z;
            sprite[nJoint].x = x;
            sprite[nJoint].y = y;
            sprite[nJoint].z = sprite[nSprite].z;
            sprite[nJoint].cstat = 0;
            sprite[nJoint].shade = -64;
            sprite[nJoint].pal = 0;
            sprite[nJoint].xrepeat = sprite[nJoint].yrepeat = 32;
            sprite[nJoint].clipdist = 1;
            sprite[nJoint].picnum = s_kexCobraJointTile;
            sprite[nJoint].owner = -1;
            sprite[nJoint].extra = -1;
        }
    }

    s.cobra = cobra;
    if (cobra)
    {
        int const dir = RandomWord() & kAngleMask;
        s.vx = (Cos(dir) * 96) >> 10;
        s.vy = (Sin(dir) * 96) >> 10;
        s.vz = 0;
    }
    else
    {
        int const angle = RandomSize(15) * 1024 / 32768; // 0..pi radians
        int const amount = RandomSize(15) * 16 / 32768;
        s.vx = (Sin(angle) * amount * 6) >> 10;
        s.vy = (Cos(angle) * amount * 6) >> 10;
        s.vz = -(24 + RandomSize(8) * 24 / 255) * 16 * 6 / 30;
    }
    s.ticks = cobra ? 180 : 120;
}

static void KexVaseShot_Swarm(int nSector, int x, int y, int z)
{
    for (int i = 0; i < 20; i++)
        KexVaseShot_Spawn(nSector, x, y, z, 0);
}

void KexBouncyBalls_Spawn(int nSector, int x, int y, int z, int offset)
{
    // A_SpawnBouncyBalls uses twenty Proj_Ra_Fire actors.  Keep the same
    // projectile implementation used by the Scepter vase branch so Mantis'
    // death burst gets the remaster's bounce, expiry, damage and art.
    for (int i = 0; i < 20; i++)
        KexVaseShot_Spawn(nSector, x, y, z - offset * 16 * 6, 0);
    if (s_breakSelftestActive)
        OSD_Printf("Kex Mantis: twenty Ra-fire death orbs spawned\n");
}

static void KexVaseCobra_Trio(int nSector, int x, int y, int z)
{
    for (int i = 0; i < 3; i++)
        KexVaseShot_Spawn(nSector, x, y, z, 1);
}

// Exact kexSlaveActor::GetHeight values for every native controller which can
// reach A_RandomItem. The shared Wasp record mirrors its identity in hitag.
int KexDrop_ActorHeight(int nSprite)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return 0;
    switch (sprite[nSprite].statnum)
    {
        case 99:  return 64;   // AI_Spider / AI_Scorp
        case 101: return 192;  // AI_Anubis
        case 102: return 192;  // AI_Mummy
        case 103: return 64;   // AI_Fish
        case 104: return 192;  // AI_Bastet
        case 105: return 192;  // AI_Sentry
        case 107:               // AI_Wasp / AI_Bird / AI_Seedling
            return sprite[nSprite].hitag == 1 ? 128
                 : sprite[nSprite].hitag == 2 ? 96 : 64;
        case 118: return 384;  // AI_Mantis
        case 120: return 192;  // AI_Set
        case 121: return 256;  // AI_Queen host
        case 122: return 160;  // AI_Selkis
        default:  return 0;
    }
}

int KexGibDeath_OwnsDrop(int nStat)
{
    if (!g_kexMode || !seq_IsLoaded(kSeqKexGibDeath))
        return 0;

    // Every shipped A_GibDeath caller belongs to one of these controller
    // families. Their shared Fx_Gibs01 actor owns the delayed A_RandomItem;
    // the initial lethal-hit hook must not create a second, immediate roll.
    switch (nStat)
    {
        case 99:   // AI_Spider / AI_Scorp
        case 101:  // AI_Anubis
        case 102:  // AI_Mummy
        case 103:  // AI_Fish
        case 104:  // AI_Bastet
        case 105:  // AI_Sentry
        case 107:  // AI_Wasp / AI_Bird / AI_Seedling
        case 121:  // AI_Queen body
            return 1;
        default:
            return 0;
    }
}

static void KexDrop_CreateOrb(int nSector, int x, int y, int z,
                              short lotag, short hitag, short picnum)
{
    int const exact = KexDrop_SpawnOrb(nSector, x, y, z,
                                       lotag, hitag, picnum);
    // -1 means an older conversion without the optional exact sequence. -2
    // is a real actor-allocation failure and must not manufacture a pickup.
    if (exact == -1)
        KexDrop_Spawn(nSector, x, y, z, lotag, hitag, picnum);
}

// A_RandomItem, transcribed from scripts/p_misc.txt: rolled at the end of
// nearly every monster death (the gib anim calls it), and with the maps
// placing almost no health or ammo at all, these drops and the vases are the
// campaign's whole supply line. r = UInt8() & 7: 0-2 drop -- an even coin
// between ammo and health -- and 3-7 drop nothing unless the player is under a
// quarter health, when mercy gives a berry three times in four. The remaster's
// Player() is the player driving the death event; use nKiller here so the
// lockstep roll follows the responsible player rather than the local view.
void KexDrop_RandomItemAt(int nSector, int x, int y, int z, int nKiller)
{
    if (!g_kexMode || (unsigned)nSector >= (unsigned)numsectors)
        return;

    int const r = RandomSize(3);
    if (r > 2)
    {
        if (nKiller >= 0 && sprite[nKiller].statnum == 100)
        {
            int const nPlayer = GetPlayerFromSprite(nKiller);
            if (PlayerList[nPlayer].nHealth < kMaxHealth / 4 && (RandomSize(8) & 3) != 0)
                KexDrop_CreateOrb(nSector, x, y, z, kKexLotagHealth, 20,
                                  s_kexHealthTile > 0 ? s_kexHealthTile : 718);
        }
        return;
    }

    if ((r & 1) == 0)
    {
        int const big = RandomSize(8) < 128;
        KexDrop_CreateOrb(nSector, x, y, z, kKexLotagAmmo, big ? 2 : 1,
                          s_kexAmmoTile > 0 ? s_kexAmmoTile : 877);
    }
    else
    {
        int const big = RandomSize(8) < 128;
        KexDrop_CreateOrb(nSector, x, y, z, kKexLotagHealth, big ? 50 : 20,
                          s_kexHealthTile > 0 ? s_kexHealthTile : 718);
    }
}

void KexDrop_RandomItem(int nSprite, int nKiller, int actorHeight)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return;
    int const nSector = sprite[nSprite].sectnum;
    if ((unsigned)nSector >= (unsigned)numsectors)
        return;
    if (actorHeight < 0)
        actorHeight = KexDrop_ActorHeight(nSprite);
    KexDrop_RandomItemAt(nSector, sprite[nSprite].x, sprite[nSprite].y,
        sprite[nSprite].z - actorHeight * 16 * 6, nKiller);
}

static void KexBreak_DropItem(int nSprite, KexBreakType const &t)
{
    int const nSector = sprite[nSprite].sectnum;
    int const x = sprite[nSprite].x, y = sprite[nSprite].y;
    // A_RandomVaseItem creates every branch at origin.z + actor.GetHeight().
    // KEX Z grows upward and Build Z grows downward; both use the shared
    // 16x vertical and 6x world conversion used by the effect helpers.
    int const z = sprite[nSprite].z - t.spawnheight * 16 * 6;
    int const pharaoh = KexDifficulty_Get() == kKexDifficultyPharaoh;
    int const powerTest = s_breakSelftestActive && Bgetenv("VB_KEXBREAK_POWERTEST");
    int const mapTest = s_breakSelftestActive && Bgetenv("VB_KEXBREAK_MAPTEST");
    int const scepterTest = s_breakSelftestActive && Bgetenv("VB_KEXBREAK_SCEPTERTEST");
    int const shawlTest = s_breakSelftestActive && Bgetenv("VB_KEXBREAK_SHAWLTEST");

    // Pharaoh destroys half of all vases without reaching the drop table.
    // This draw precedes Max(8) in the shipped script, which matters to the
    // sequence as well as to the probability.
    if (!s_scorpTest && pharaoh && RandomBit() == 1)
        return;

    int const r = s_scorpTest ? 0 : RandomSize(3); // kexSlaveRandomizer::Max(8)

    // A_RandomVaseItem only considers its rare drops on one half of the vase
    // rolls. The map then has its own 1-in-32 gate; omitting that second gate
    // made nearly every player's first eligible vase hand out the automap.
    // Automap().IsAlreadyMapped() is represented by the per-level Kex
    // inventory bit; it is cleared by KexInv_LevelStart().
    if (!s_scorpTest && RandomBit() == 0)
    {
        int const mapRoll = RandomSize(5);       // kexSlaveRandomizer::UInt8() & 31
        if (!powerTest && (mapTest || mapRoll == 0)
            && !KexInvList[nLocalPlayer].map)
        {
            if (s_kexMapTile > 0)
                KexDrop_Spawn(nSector, x, y, z, 11, 0, s_kexMapTile);
            if (s_breakSelftestActive)
                OSD_Printf("Kex vase: map scroll spawned in sector %d\n", nSector);
            return;
        }

        if (pharaoh)
        {
            // Pharaoh has no shared cooldown and deliberately does not return:
            // this rare power orb can accompany the ordinary switch drop.
            if (powerTest || RandomSize(5) == 0)
            {
                if (s_kexPowerTile > 0)
                    KexDrop_Spawn(nSector, x, y, z, 10, KEX_POWER_DROP_HITAG, s_kexPowerTile);
                if (s_breakSelftestActive)
                    OSD_Printf("Kex vase: Pharaoh weapon power spawned in sector %d\n", nSector);
            }
        }
        else
        {
            // Normal and Hard share the script-global cooldown: after the
            // initial five eligible rolls, a 1/32 roll starts 10..19 more.
            if (powerTest || (s_kexPowerTimer <= 0 && RandomSize(5) == 0))
            {
                if (s_kexPowerTile > 0)
                    KexDrop_Spawn(nSector, x, y, z, 10, KEX_POWER_DROP_HITAG, s_kexPowerTile);
                s_kexPowerTimer = 10 + (RandomSize(8) % 10);
                if (s_breakSelftestActive)
                    OSD_Printf("Kex vase: weapon power spawned in sector %d\n", nSector);
                return;
            }
            if (s_kexPowerTimer > 0)
                s_kexPowerTimer--;
        }
    }

    // Case 1 checks Sandals first. Without them it falls through case 2, so a
    // Scepter holder gets the Ra-fire swarm for both r==1 and r==2.
    if (r == 1 && KexInv_HasArtifact(nLocalPlayer, kKexArtiSandals))
    {
        BuildWasp(-1, x, y, z, nSector, RandomSize(11), 0);
        return;
    }

    if ((r == 1 || r == 2 || scepterTest)
        && (KexInv_HasArtifact(nLocalPlayer, kKexArtiScepter) || scepterTest)
        && s_kexRaTile > 0)
    {
        KexVaseShot_Swarm(nSector, x, y, z);
        if (s_breakSelftestActive)
            OSD_Printf("Kex vase: Scepter swarm spawned in sector %d\n", nSector);
        return;
    }

    if ((r == 6 || shawlTest) && (KexInv_HasArtifact(nLocalPlayer, kKexArtiShawl) || shawlTest)
        && s_kexCobraTile > 0)
    {
        KexVaseCobra_Trio(nSector, x, y, z);
        if (s_breakSelftestActive)
            OSD_Printf("Kex vase: Shawl cobra trio spawned in sector %d\n", nSector);
        return;
    }

    short lotag = 0, hitag = 0, picnum = 0;

    switch (r)
    {
        case 0:
        {
            int const scepterHeld = !s_scorpTest
                && (KexInv_HasArtifact(nLocalPlayer, kKexArtiScepter)
                    || (s_breakSelftestActive && Bgetenv("VB_KEXBREAK_SCEPTER_SUBTEST")));
            if (scepterHeld
                && (RandomSize(8) & 7) >= 3)
            {
                lotag = kKexLotagAmmo; hitag = 1;
                picnum = s_kexAmmoTile > 0 ? s_kexAmmoTile : 877;
                if (s_breakSelftestActive)
                    OSD_Printf("Kex vase: Scepter ammo sub-roll\n");
                break;
            }
            // AI_Scorp shares the Spider controller but carries the exact
            // generated SCORPION.SEQ identity. BuildScorp is Selkis, the boss.
            int const nScorp = BuildSpider(-1, x, y, z, nSector,
                                           RandomSize(11), 1);
            if (s_scorpTest && nScorp >= 0
                && sprite[GetSpiderSprite(nScorp)].extra == 1)
            {
                s_vaseScorpsSpawned++;
                if (t.spawnheight > 0
                    && sprite[GetSpiderSprite(nScorp)].z == z
                    && z < sprite[nSprite].z)
                    s_vaseScorpsAtHeight++;
            }
            if (s_breakSelftestActive && scepterHeld)
                OSD_Printf("Kex vase: Scepter scorpion sub-roll\n");
            return;
        }

        case 1:
        case 2:
        case 3:
            lotag = kKexLotagAmmo; hitag = 1;
            picnum = s_kexAmmoTile > 0 ? s_kexAmmoTile : 877;
            break;

        case 4:
            // "health > 100" of the remaster's 200, so half of full here.
            if (PlayerList[nLocalPlayer].nHealth > (kMaxHealth / 2) && RandomBit() == 0)
                return;
            fallthrough__;

        case 5:
            lotag = kKexLotagHealth; hitag = 20;
            picnum = s_kexHealthTile > 0 ? s_kexHealthTile : 718;
            break;

        default:
            return;
    }

    // The post-spawn scale roll changes pickup strength as well as appearance:
    // ammo is maxAmmo*scale/9, while a large health orb uses 20*2.5 = 50.
    if (RandomSize(8) < 128)
        hitag = lotag == kKexLotagAmmo ? 2 : 50;
    KexDrop_Spawn(nSector, x, y, z, lotag, hitag, picnum);
}

// The death anim: break sound, exact puff/blast, random item/radial callback,
// one physics chunk per authored shard, then the prop is gone.
static void KexBreak_Shatter(int nProp)
{
    int const nSprite = s_prop[nProp].nSprite;
    KexBreakType const &t = s_type[s_prop[nProp].nType];

    KexFlash_FatalProp(nSprite);

    KexBreak_PlaySound(t, nSprite);

    if (t.blastdamage > 0)
    {
        // Both exploding vases and Magic 8 Ball call Fx_Explosion01 at +32
        // before A_RadialBlast. Only the mine switches to the dedicated water
        // actor; the vase death animations explicitly name the land actor.
        bool const bUnderwater = SectFlag[sprite[nSprite].sectnum]
            & kSectUnderwater;
        bool const bMine = t.sight > 0 && t.fuse > 0;
        bool const bExactExplosion = KexExplosion01_Spawn(
            sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z,
            sprite[nSprite].sectnum, 32, bMine && bUnderwater) != -1;

        // Conversions made before KEXEXPLODE.SEQ/KEXWATEREXP.SEQ retain the
        // previous native approximation. The remaster's A_RadialBlast radius
        // is in its own units; runlist compares (|dx|+|dy|)>>8, so 512 becomes
        // 512 * WORLD(6) / 256 = 12 against the cauldron's 20.
        if (!bExactExplosion && bUnderwater)
        {
            // magic8ball_explode_water: no fire under water, a burst of the
            // game's own bubbles (bubbles.cpp) where the blast is.
            int BuildBubble(int x, int y, int z, short nSector);
            for (int i = 0; i < 12; i++)
                BuildBubble(sprite[nSprite].x + (RandomSize(7) - 64) * 8, sprite[nSprite].y + (RandomSize(7) - 64) * 8,
                            sprite[nSprite].z - RandomSize(9) * 8, sprite[nSprite].sectnum);
        }
        else if (!bExactExplosion)
        {
            AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x, sprite[nSprite].y,
                     sprite[nSprite].z, 128);
            BuildAnim(-1, 36, 0, sprite[nSprite].x, sprite[nSprite].y,
                      sector[sprite[nSprite].sectnum].floorz,
                      sprite[nSprite].sectnum, 240, 4);
        }
        runlist_RadialDamageEnemy(nSprite, t.blastdamage,
                                  (int16_t)(t.blastradius * 6 / 256));
        if (t.blasttoggle)
            KexToggle_Blast(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z,
                            sprite[nSprite].sectnum, t.blastradius);
    }
    else
    {
        // Every ordinary vase/barrel death calls Fx_VaseExplodePuff at +32
        // after its local sound and before A_RandomVaseItem/toss callbacks.
        if (KexVasePuff_Spawn(sprite[nSprite].x, sprite[nSprite].y,
                sprite[nSprite].z, sprite[nSprite].sectnum, 32) >= 0)
            s_vasePuffsSpawned++;
        KexBreak_DropItem(nSprite, t);
    }

    // All authored shard calls use A_TossActor(0,0,32,8,8,4,16).  The shared
    // KEX chunk bridge preserves that launch and 60-Hz actor physics instead
    // of borrowing the native fire-cauldron throw.
    int nChunks = 0;
    KexTossBounceSound const bounceSound = KexBreak_ShardBounceSound(t);
    for (int i = 0; i < t.nshards; i++)
    {
        if (KexTossBouncyCreatureChunk(nSprite, t.shard0 + i, bounceSound) < 0)
            break;
        nChunks++;
    }
    if (s_breakSelftestActive)
        OSD_Printf("Kex breakables: prop %d (tile %d) -> %d/%d shards\n",
                   nProp, t.tile, nChunks, t.nshards);

    // Unregister and remove the prop, the way the exploding cauldron does.
    runlist_SubRunRec(sprite[nSprite].owner);
    runlist_SubRunRec(s_prop[nProp].nRunFree);
    s_prop[nProp].nSprite = -1;
    s_flash[nSprite] = 0;       // do not whiten a later sprite that reuses this slot
    mydeletesprite(nSprite);
}

void FuncKexBreak(int a, int b, int nRun)
{
    int const nProp = RunData[nRun].nVal;
    if (nProp >= KEX_VASE_SHOT_BASE)
    {
        int const nShot = nProp - KEX_VASE_SHOT_BASE;
        if ((unsigned)nShot < (unsigned)s_vaseShots && (a & kMessageMask) == k0x20000)
            KexVaseShot_Tick(nShot);
        return;
    }
    if (nProp < 0 || nProp >= s_props || s_prop[nProp].nSprite < 0)
        return;

    switch (a & 0x7F0000)
    {
        case 0x20000:   // per tick: nothing to do, unless it is a mine
            if (s_orbTest && !s_orbTestStarted)
                KexDrop_OrbSelfTest();
            KexBreak_MineTick(nProp);
            if (s_prop[nProp].nSprite < 0)
                return;
            if (s_selftest > 0 && --s_selftest == 0)
            {
                s_breakSelftestActive = 1;
                for (int i = 0; i < s_props; i++)
                    if (s_prop[i].nSprite >= 0)
                    { s_prop[i].nHealth = 0; KexBreak_Shatter(i); }

                bool const puffOkay = seq_IsLoaded(kSeqKexVasePuff)
                    && s_vasePuffsSpawned == s_vasePuffsExpected;
                OSD_Printf("Kex break puff test: %s (%d/%d exact +32 puffs)\n",
                           puffOkay ? "PASS" : "FAIL",
                           s_vasePuffsSpawned, s_vasePuffsExpected);

                if (s_scorpTest)
                {
                    bool const okay = s_vaseScorpsSpawned == s_vasePuffsExpected
                        && s_vaseScorpsAtHeight == s_vasePuffsExpected
                        && KexSpiderIdentityTest();
                    OSD_Printf("Kex break scorpion test: %s (%d/%d exact AI_Scorp identities at authored height)\n",
                               okay ? "PASS" : "FAIL",
                               s_vaseScorpsAtHeight, s_vasePuffsExpected);
                }

                if (s_soundTest)
                {
                    bool okay = true;
                    for (int i = 0; i < kKexBreakSoundCount; i++)
                        okay &= s_soundPlayed[i] == s_soundExpected[i];
                    OSD_Printf("Kex break sound playback test: %s "
                               "(none %d/%d, glass %d/%d, wood %d/%d, flesh %d/%d)\n",
                               okay ? "PASS" : "FAIL",
                               s_soundPlayed[kKexBreakSoundNone], s_soundExpected[kKexBreakSoundNone],
                               s_soundPlayed[kKexBreakSoundGlass], s_soundExpected[kKexBreakSoundGlass],
                               s_soundPlayed[kKexBreakSoundWood], s_soundExpected[kKexBreakSoundWood],
                               s_soundPlayed[kKexBreakSoundFlesh], s_soundExpected[kKexBreakSoundFlesh]);
                }

                // And the drop roll: kill everything, count what fell.
                int before = 0, after = 0;
                for (int i = 0; i < kMaxSprites; i++)
                    before += (sprite[i].statnum == 906 || sprite[i].statnum == 912);
                int const creatures = nCreaturesLeft;
                KillCreatures();
                for (int i = 0; i < kMaxSprites; i++)
                    after += (sprite[i].statnum == 906 || sprite[i].statnum == 912);
                OSD_Printf("Kex drops: %d creatures killed, %d items dropped\n",
                           creatures - nCreaturesLeft, after - before);
            }
            return;
        case 0x30000:
        case 0x90000:   // drawn as an ordinary sprite, so no sequence to plot
            return;

        case 0x80000:   // shot
        {
            if (s_type[s_prop[nProp].nType].fuse > 0 && s_prop[nProp].nFuse >= 0)
                return; // A_StartCountdown removed AF_SHOOTABLE.
            if (s_prop[nProp].nHealth <= 0)
                return;
            s_prop[nProp].nHealth -= (short)b;
            KexFlash_Hit(s_prop[nProp].nSprite);    // the remaster blinks a hit vase too
            if (s_prop[nProp].nHealth <= 0)
            {
                // A mine's death is its countdown.
                if (s_type[s_prop[nProp].nType].fuse > 0)
                    KexBreak_ArmMine(nProp);
                else
                    KexBreak_Shatter(nProp);
            }
            return;
        }

        case 0xA0000:   // caught in a blast
        {
            if (s_type[s_prop[nProp].nType].fuse > 0 && s_prop[nProp].nFuse >= 0)
                return; // A_StartCountdown removed AF_SHOOTABLE.
            if (s_prop[nProp].nHealth <= 0)
                return;
            int const nDamage = runlist_CheckRadialDamage(s_prop[nProp].nSprite);
            if (nDamage <= 0)
                return;
            s_prop[nProp].nHealth -= nDamage;
            KexFlash_Hit(s_prop[nProp].nSprite);
            if (s_prop[nProp].nHealth <= 0)
            {
                if (s_type[s_prop[nProp].nType].fuse > 0)
                    KexBreak_ArmMine(nProp);
                else
                    KexBreak_Shatter(nProp);
            }
            return;
        }

        default:
            return;
    }
}

// ---------------------------------------------------------------------------
// The scripted sector moves. Two sectors in the whole game carry SCRIPTED_MOVE:
// the casket lid in TOMB (tag 666) and Ramses' bier in TOMBEND (tag 0), and
// what moves them is a line each in the level scripts:
//
//   TOMB    $script 0 (load):        MoveScriptedSector(666, -256, -1)
//           $script 1 (Ramses):      MoveScriptedSector(666,  320,  4)
//   TOMBEND $script 0 (load):        MoveScriptedSector(  0,   64, -1)
//           EndRamsesEvent (good):   MoveScriptedSector(  0, -320,  6)
//
// -1 is instant. The amount is Kex units, up positive; a Kex unit is 96 of
// Build's z (SCALE 16 x WORLD 6), down positive. With bCeiling=true KEX moves
// the tagged sector's ceiling and event.params' floor together. KEX ticks
// movers at 60 Hz; MoveThings() is 30 Hz, so one native update applies two
// authored speed quanta.
// ---------------------------------------------------------------------------
#define KEX_Z_PER_UNIT 96
// kexPlayer::stepHeight in the remaster is 48 world units.
#define KEX_PLAYER_STEP_HEIGHT 48

int KexPlayer_OnSectorFloor(int nPlayer, int nSector)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)nTotalPlayers
        || (unsigned)nSector >= (unsigned)numsectors)
        return 0;
    int const nSprite = PlayerList[nPlayer].nSprite;
    if (nSprite < 0 || PlayerList[nPlayer].nHealth <= 0
        || sprite[nSprite].sectnum != nSector)
        return 0;
    // The remaster tests floor-plane distance against the player's 48-unit
    // StepHeight. Use the exact sloped Build floor under the actor, rather
    // than a sector's base z, so a script/pad remains grounded on a slope.
    int const floor = getflorzofslope((int16_t)nSector,
                                      sprite[nSprite].x, sprite[nSprite].y);
    return klabs(sprite[nSprite].z - floor) <= KEX_Z_PER_UNIT * KEX_PLAYER_STEP_HEIGHT;
}

struct KexScriptedRule
{
    char const *map;
    int16_t tag, atLoad, amount, speed;
    int8_t  when;
};
static KexScriptedRule const s_scriptedRule[] = {
    { "TOMB",    666, -256,  320, 4, 0 },
    { "TOMBEND",   0,   64, -320, 6, 1 },
};

struct KexScripted
{
    int16_t sector, linkedSector, tag;
    int32_t target;       // tagged sector ceiling z it is moving to
    int32_t step;         // Build z per tick, signed toward target; 0 = still
    int16_t ticks, expectedTicks;
    KexScriptedRule const *rule;
};
static KexScripted s_scripted[8];
static int s_scripteds;

void KexScripted_Reset(void) { s_scripteds = 0; }

void KexScripted_Add(int nSector, int nTag, int nLinkedSector)
{
    if (s_scripteds >= (int)ARRAY_SIZE(s_scripted) || (unsigned)nSector >= (unsigned)numsectors)
        return;
    KexScripted &m = s_scripted[s_scripteds++];
    m.sector = (int16_t)nSector;
    m.linkedSector = (unsigned)nLinkedSector < (unsigned)numsectors ? (int16_t)nLinkedSector : -1;
    m.tag = (int16_t)nTag;
    m.target = sector[nSector].ceilingz; m.step = 0;
    m.ticks = m.expectedTicks = 0; m.rule = NULL;
}

static void KexScripted_Shift(KexScripted &m, int32_t dz)
{
    // kexScriptedMover::Tick: MoveSector(main, !bCeiling, move), then
    // MoveSector(link, bCeiling, move). Every retail script passes true.
    sector[m.sector].ceilingz += dz;
    if ((unsigned)m.linkedSector < (unsigned)numsectors)
        sector[m.linkedSector].floorz += dz;
}

// The bare level name -- "TOMB" from <dir>/KEX_TOMB.MAP -- for the tables
// keyed by level. An exact match, not a substring: TOMB is a prefix of TOMBEND.
static void Kex_LevelName(char *out, int outSize)
{
    char nameUp[BMAX_PATH];
    Bstrncpyz(nameUp, gUserMapFilename, sizeof(nameUp));
    Bstrupr(nameUp);
    char *base = nameUp;
    for (char *p = nameUp; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (!Bstrncmp(base, "KEX_", 4)) base += 4;
    if (char *dot = Bstrrchr(base, '.')) *dot = 0;
    Bstrncpyz(out, base, outSize);
}

void KexScripted_Init(void)
{
    if (!g_kexMode || !s_scripteds)
        return;
    char base[32];
    Kex_LevelName(base, sizeof(base));

    for (int i = 0; i < s_scripteds; i++)
    {
        KexScripted &m = s_scripted[i];
        for (auto const &r : s_scriptedRule)
        {
            if (Bstrcmp(base, r.map) || r.tag != m.tag)
                continue;
            m.rule = &r;
            KexScripted_Shift(m, -r.atLoad * KEX_Z_PER_UNIT);
            m.target = sector[m.sector].ceilingz;
            OSD_Printf("Kex scripted: sector %d linked %d tag %d moved %d at load\n",
                       m.sector, m.linkedSector, m.tag, r.atLoad);
        }
    }
}

void KexScripted_Trigger(int when)
{
    for (int i = 0; i < s_scripteds; i++)
    {
        KexScripted &m = s_scripted[i];
        if (!m.rule || m.rule->when != when)
            continue;
        m.target = sector[m.sector].ceilingz - m.rule->amount * KEX_Z_PER_UNIT;
        int const speed = m.rule->speed * 2; // KEX 60 Hz -> Build 30 Hz
        m.step = (m.rule->amount > 0 ? -1 : 1) * speed * KEX_Z_PER_UNIT;
        m.ticks = 0;
        m.expectedTicks = (abs(m.rule->amount) + speed - 1) / speed;
        OSD_Printf("Kex scripted: sector %d tag %d moving %d\n", m.sector, m.tag, m.rule->amount);
    }
}

void KexScripted_Tick(void)
{
    // VB_KEXSCRIPT_TEST=<ticks>: fire the Ramses move that far into the level.
    static int s_test = -1;
    if (s_test < 0) { char const *e = getenv("VB_KEXSCRIPT_TEST"); s_test = e ? Batoi(e) : 0; }
    if (s_test > 0 && --s_test == 0)
        KexScripted_Trigger(0);
    // VB_KEXSCRIPT_END_TEST=<ticks>: likewise exercise the good-ending move
    // without waiting through TOMBEND's full Ramses conversation.
    static int s_endTest = -1;
    if (s_endTest < 0) { char const *e = getenv("VB_KEXSCRIPT_END_TEST"); s_endTest = e ? Batoi(e) : 0; }
    if (s_endTest > 0 && --s_endTest == 0)
        KexScripted_Trigger(1);

    for (int i = 0; i < s_scripteds; i++)
    {
        KexScripted &m = s_scripted[i];
        if (!m.step)
            continue;
        m.ticks++;
        int32_t const left = m.target - sector[m.sector].ceilingz;
        int32_t dz = m.step;
        if ((dz > 0 && dz > left) || (dz < 0 && dz < left))
            dz = left;
        KexScripted_Shift(m, dz);
        if (sector[m.sector].ceilingz == m.target)
        {
            m.step = 0;
            OSD_Printf("Kex scripted: sector %d tag %d arrived after %d ticks (expected %d)\n",
                       m.sector, m.tag, m.ticks, m.expectedTicks);
        }
    }
}

// ---------------------------------------------------------------------------
// Kex wall switches. ResetWallSwitchFromTag() changes switch art directly; it
// deliberately does not fire the channel because the same tag can drive a
// door, a lift or a trap. Conversion stores a small marker at every switch
// face with the source event tag and its original/off tile.
// ---------------------------------------------------------------------------
#define kMaxKexSwitches 64
struct KexSwitch
{
    int16_t tag, wall, offpic, type;
};
static KexSwitch s_kexSwitch[kMaxKexSwitches];
static int s_kexSwitches;
static int s_kexSwitchTest = -1;

void KexSwitch_Reset(void) { s_kexSwitches = 0; s_kexSwitchTest = -1; }

void KexSwitch_Add(int nSector, int nTag, int nOffPic, int nType, int x, int y)
{
    if (!g_kexMode || s_kexSwitches >= kMaxKexSwitches
        || (unsigned)nSector >= (unsigned)numsectors || nOffPic < 0)
        return;
    int best = -1;
    int64_t bestDist = INT64_MAX;
    int const end = sector[nSector].wallptr + sector[nSector].wallnum;
    for (int nWall = sector[nSector].wallptr; nWall < end; nWall++)
    {
        int const nNext = wall[nWall].point2;
        if ((unsigned)nNext >= (unsigned)numwalls)
            continue;
        // Keep doubled midpoint coordinates integral; construction markers
        // retain the precise midpoint even for an odd-length wall.
        int64_t const dx = (int64_t)wall[nWall].x + wall[nNext].x - 2 * (int64_t)x;
        int64_t const dy = (int64_t)wall[nWall].y + wall[nNext].y - 2 * (int64_t)y;
        int64_t const dist = dx * dx + dy * dy;
        if (dist < bestDist)
        {
            bestDist = dist;
            best = nWall;
        }
    }
    if (best < 0)
        return;
    KexSwitch &s = s_kexSwitch[s_kexSwitches++];
    s.tag = (int16_t)nTag;
    s.wall = (int16_t)best;
    s.offpic = (int16_t)nOffPic;
    s.type = (int16_t)nType;
    if (Bgetenv("VB_KEXSWITCH_TEST"))
        OSD_Printf("Kex switch sound marker: wall %d event %d tag %d off %d\n",
                   best, nType, nTag, nOffPic);
}

void KexSwitch_ResetByTag(int nTag)
{
    if (!g_kexMode)
        return;
    for (int i = 0; i < s_kexSwitches; i++)
    {
        KexSwitch const &s = s_kexSwitch[i];
        if (s.tag == nTag && (unsigned)s.wall < (unsigned)numwalls)
            wall[s.wall].picnum = s.offpic;
    }
}

int KexSwitch_PlaySound(int nWall, int nPlayer)
{
    if (!g_kexMode)
        return 0;
    for (int i = 0; i < s_kexSwitches; i++)
    {
        KexSwitch const &s = s_kexSwitch[i];
        if (s.wall != nWall)
            continue;

        // kexWorld::UseWallSwitch plays switch2.wav for types 202/203 and
        // switch.wav for 200/201. It is a player-actor sound, so attach it
        // to the player who sent this wall-use runlist message.
        char const * const name = s.type == 202 || s.type == 203 ? "k_sw2" : "k_sw1";
        char const * const want = s.type == 202 || s.type == 203
            ? "sounds/switch2.wav" : "sounds/switch.wav";
        char const * const path = KexSound_Alias(name, NULL);
        int const sound = KexSound_Get(name);
        if (Bgetenv("VB_KEXSWITCH_TEST"))
            OSD_Printf("Kex switch sound test: %s (event %d -> %s, player %d)\n",
                       path && !Bstrcasecmp(path, want) ? "PASS" : "FAIL",
                       s.type, path ? path : "missing", nPlayer);
        if (sound >= 0 && (unsigned)nPlayer < (unsigned)kMaxPlayers)
        {
            int const nSprite = PlayerList[nPlayer].nSprite;
            if ((unsigned)nSprite < (unsigned)kMaxSprites)
                D3PlayFX(sound, nSprite);
        }
        return 1;
    }
    return 0;
}

// VB_KEXSWITCH_TEST drives one authored switch through the normal player-use
// runlist message.  Wall-only switches do not have a sector mover for the
// generic look-at helper to target, so keep this focused probe with the
// marker table that resolves their remaster sound and reset art.
static void KexSwitch_TestTick(void)
{
    if (s_kexSwitchTest < 0)
        s_kexSwitchTest = g_kexMode && Bgetenv("VB_KEXSWITCH_TEST") ? 90 : 0;
    if (s_kexSwitchTest <= 0 || --s_kexSwitchTest != 0)
        return;

    for (int i = 0; i < s_kexSwitches; i++)
    {
        int const nWall = s_kexSwitch[i].wall;
        if ((unsigned)nWall >= (unsigned)numwalls || wall[nWall].lotag <= 0)
            continue;
        OSD_Printf("Kex switch test: pressing wall %d (event %d tag %d)\n",
                   nWall, s_kexSwitch[i].type, s_kexSwitch[i].tag);
        runlist_SignalRun(wall[nWall].lotag - 1, nLocalPlayer | 0x40000);
        return;
    }
    OSD_Printf(OSD_ERROR "Kex switch test: FAIL (no active Kex switch wall)\n");
}

// ---------------------------------------------------------------------------
// DOOR_OPEN_ONLY_MESSAGE.  Kex still constructs this as a channel-driven
// ceiling door, but kexWorld::UseWallSpecial deliberately does not fire that
// channel when the player uses the door.  It only prints localization key
// $str_92, "This Door is Sealed".  A lotag-91 construction sprite retains
// that one Kex distinction without changing ordinary Build door handling.
// ---------------------------------------------------------------------------
static uint8_t s_kexSealedDoor[kMaxSectors];
static int s_kexSealedDoorTest = -1;

void KexSealedDoor_Reset(void)
{
    Bmemset(s_kexSealedDoor, 0, sizeof(s_kexSealedDoor));
    s_kexSealedDoorTest = -1;
}

void KexSealedDoor_Add(int nSector)
{
    if (g_kexMode && (unsigned)nSector < (unsigned)numsectors)
        s_kexSealedDoor[nSector] = 1;
}

int KexSealedDoor_Use(int nSector)
{
    if (!g_kexMode || (unsigned)nSector >= (unsigned)numsectors
        || !s_kexSealedDoor[nSector])
        return 0;
    StatusMessage(300, "This Door is Sealed");
    return 1;
}

void KexSealedDoor_Tick(void)
{
    // VB_KEXSEALED_TEST checks the live marker path without input.  The
    // actual player use is routed through player.cpp above.
    if (s_kexSealedDoorTest < 0)
        s_kexSealedDoorTest = g_kexMode && Bgetenv("VB_KEXSEALED_TEST") ? 90 : 0;
    if (s_kexSealedDoorTest <= 0 || --s_kexSealedDoorTest != 0)
        return;
    for (int nSector = 0; nSector < numsectors; nSector++)
    {
        if (KexSealedDoor_Use(nSector))
        {
            OSD_Printf("Kex sealed-door test: PASS (sector %d -> This Door is Sealed)\n", nSector);
            return;
        }
    }
    OSD_Printf(OSD_ERROR "Kex sealed-door test: FAIL (no sealed-door marker on this level)\n");
}

// ---------------------------------------------------------------------------
// FLOOR_TRIGGER_PLATE. Kex raises the floor 16 source units while reading the
// map, then kexFloor lowers it by two units per 60 Hz tick and fires its tag
// only once it reaches the authored height. Build's lotag-21 touchplate has
// no moving floor and fires immediately, so this compact controller retains
// the real plate geometry, cadence and tag+1 reversal path.
// ---------------------------------------------------------------------------
#define kMaxKexPlates 64
#define KEX_PLATE_STEP (2 * KEX_Z_PER_UNIT * 2) // 2 Kex units @ 60 Hz -> Build @ 30 Hz
enum { kKexPlateIdle, kKexPlateDown, kKexPlateLowered, kKexPlateUp, kKexPlateDone };
struct KexPlate
{
    int16_t sector, tag, resetChannel;
    int32_t top, bottom, target;
    int16_t ticks, expectedTicks;
    uint8_t state;
};
static KexPlate s_plate[kMaxKexPlates];
static int s_plates;
static int KexChan(int tag);     // channel table below
static void Kex_FireChannel(int nChannel);

void KexPlate_Reset(void) { s_plates = 0; }

void KexPlate_Add(int nSector, int nTag, int nTarget)
{
    if (!g_kexMode || s_plates >= kMaxKexPlates
        || (unsigned)nSector >= (unsigned)numsectors)
        return;
    KexPlate &p = s_plate[s_plates++];
    Bmemset(&p, 0, sizeof(p));
    p.sector = (int16_t)nSector;
    p.tag = (int16_t)nTag;
    p.top = sector[nSector].floorz;
    p.bottom = p.target = nTarget;
    int const resetTag = nTag + 1;
    int const reset = KexChan(resetTag);
    p.resetChannel = reset >= 0 ? runlist_AllocChannel(reset) : -1;
    p.state = kKexPlateIdle;
    if (p.bottom <= p.top)
        OSD_Printf(OSD_ERROR "Kex plate: sector %d target %d is not below top %d\n",
                   nSector, nTarget, p.top);
}

static void KexPlate_Move(KexPlate &p, int32_t target, int state)
{
    int32_t const here = sector[p.sector].floorz;
    p.ticks = 0;
    p.expectedTicks = (abs(target - here) + KEX_PLATE_STEP - 1) / KEX_PLATE_STEP;
    p.target = target;
    p.state = (uint8_t)state;
}

static void KexPlate_Reverse(KexPlate &p)
{
    if (p.state == kKexPlateDown || p.state == kKexPlateLowered)
        KexPlate_Move(p, p.top, kKexPlateUp);
    else if (p.state == kKexPlateUp)
        KexPlate_Move(p, p.bottom, kKexPlateDown);
}

static void KexPlate_ResetChain(int tag)
{
    KexSwitch_ResetByTag(tag);
    // ResetWallSwitchFromTag() also calls Trigger() on any already-live
    // FLOOR_TRIGGER_PLATE carrying this raw tag. Idle and retired plates have
    // no Kex thinker, so they are intentionally untouched.
    for (int i = 0; i < s_plates; i++)
        if (s_plate[i].tag == tag && s_plate[i].state != kKexPlateIdle
            && s_plate[i].state != kKexPlateDone)
            KexPlate_Reverse(s_plate[i]);
}

static void KexPlate_Start(KexPlate &p, int nPlayer, int test)
{
    if (p.state != kKexPlateIdle)
        return;

    // EnterSectorSpecial() starts a type-23 kexFloor with the actor-local
    // fplate.wav before it resets tag+1 and begins lowering. The normal
    // player path supplies that actor; the focused no-input probe uses local.
    char const * const path = KexSound_Alias("k_fplate", NULL);
    int const sound = KexSound_Get("k_fplate");
    if (Bgetenv("VB_KEX_PLATE_SOUND_TEST"))
        OSD_Printf("Kex plate sound test: %s (event 23 -> %s, player %d)\n",
                   path && !Bstrcasecmp(path, "sounds/fplate.wav") ? "PASS" : "FAIL",
                   path ? path : "missing", nPlayer);
    if (sound >= 0 && (unsigned)nPlayer < (unsigned)kMaxPlayers)
    {
        int const nSprite = PlayerList[nPlayer].nSprite;
        if ((unsigned)nSprite < (unsigned)kMaxSprites)
            D3PlayFX(sound, nSprite);
    }
    KexPlate_ResetChain(p.tag + 1);
    KexPlate_Move(p, p.bottom, kKexPlateDown);
    if (test)
        OSD_Printf("Kex plate test: sector %d tag %d starts %d -> %d (%d ticks)\n",
                   p.sector, p.tag, p.top, p.target, p.expectedTicks);
    else
        OSD_Printf("Kex plate: sector %d tag %d sinking\n", p.sector, p.tag);
}

// The focused floor-trigger map starts the player in its one-shot remote
// trigger. Move the test player into the selected plate first, otherwise the
// normal remote path correctly resets that plate on the very next tick and
// prevents the probe from observing its arrival.
static int KexPlate_TestPlacePlayer(KexPlate const &p, int nPlayer)
{
    if ((unsigned)nPlayer >= (unsigned)nTotalPlayers)
        return 0;
    int const nSprite = PlayerList[nPlayer].nSprite;
    int const first = sector[p.sector].wallptr;
    int const count = sector[p.sector].wallnum;
    if ((unsigned)nSprite >= (unsigned)kMaxSprites || count <= 0)
        return 0;

    int64_t sumX = 0, sumY = 0;
    for (int w = first; w < first + count; w++)
    {
        sumX += wall[w].x;
        sumY += wall[w].y;
    }
    int const x = (int)(sumX / count);
    int const y = (int)(sumY / count);
    int16_t nSector = p.sector;
    updatesector(x, y, &nSector);
    if (nSector != p.sector)
        return 0;

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = sector[p.sector].floorz;
    sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
    mychangespritesect(nSprite, nSector);
    return 1;
}

void KexPlate_ChannelFired(int nChannel)
{
    if (!g_kexMode)
        return;
    for (int i = 0; i < s_plates; i++)
    {
        KexPlate &p = s_plate[i];
        if (p.resetChannel != nChannel || p.state == kKexPlateIdle
            || p.state == kKexPlateDone)
            continue;
        if (p.state == kKexPlateDown || p.state == kKexPlateLowered)
        {
            KexPlate_Reverse(p);
            OSD_Printf("Kex plate: sector %d tag %d reset\n", p.sector, p.tag);
        }
        else if (p.state == kKexPlateUp)
        {
            // kexFloor::Trigger reverses a live plate in either direction.
            KexPlate_Reverse(p);
            OSD_Printf("Kex plate: sector %d tag %d reversed\n", p.sector, p.tag);
        }
    }
}

void KexPlate_Tick(void)
{
    static int s_test = -1;
    if (s_test < 0)
    {
        char const *e = getenv("VB_KEX_PLATE_TEST");
        s_test = e ? Batoi(e) : 0;
    }
    if (s_test > 0 && --s_test == 0)
        for (int i = 0; i < s_plates; i++)
            if (s_plate[i].state == kKexPlateIdle)
            {
                if (KexPlate_TestPlacePlayer(s_plate[i], nLocalPlayer))
                {
                    // The focused map queues its tag-2 reset while loading.
                    // Consume that setup-only signal so this probe observes
                    // the selected plate's own descent; VB_KEXREMOTE_TEST
                    // independently validates the queued one-shot reset.
                    if (s_plate[i].resetChannel >= 0)
                        sRunChannels[s_plate[i].resetChannel].d = 0;
                    KexPlate_Start(s_plate[i], nLocalPlayer, 1);
                }
                else
                    OSD_Printf(OSD_ERROR "Kex plate test: FAIL (cannot place player in sector %d)\n",
                               s_plate[i].sector);
                break;
            }

    for (int i = 0; i < s_plates; i++)
    {
        KexPlate &p = s_plate[i];
        if (p.state == kKexPlateIdle)
        {
            for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
            {
                if (!KexPlayer_OnSectorFloor(nPlayer, p.sector))
                    continue;
                KexPlate_Start(p, nPlayer, 0);
                break;
            }
            continue;
        }
        if (p.state != kKexPlateDown && p.state != kKexPlateUp)
            continue;

        p.ticks++;
        int32_t const left = p.target - sector[p.sector].floorz;
        int32_t dz = left < 0 ? -KEX_PLATE_STEP : KEX_PLATE_STEP;
        if ((dz > 0 && dz > left) || (dz < 0 && dz < left))
            dz = left;
        sector[p.sector].floorz += dz;
        if (sector[p.sector].floorz != p.target)
            continue;

        if (p.state == kKexPlateDown)
        {
            p.state = kKexPlateLowered;
            OSD_Printf("Kex plate: sector %d tag %d arrived after %d ticks (expected %d)\n",
                       p.sector, p.tag, p.ticks, p.expectedTicks);
            if (Bgetenv("VB_KEX_PLATE_TEST"))
                OSD_Printf("Kex plate test: %s (sector %d, %d/%d ticks)\n",
                           p.ticks == p.expectedTicks ? "PASS" : "FAIL",
                           p.sector, p.ticks, p.expectedTicks);
            // Tag zero is a real Kex event tag for plate-style listeners, but
            // Build channel zero is private. The mapper allots it under the
            // same pseudo tag used by REMOTE_TRIGGER and wall switches.
            KexScript_FireTag(p.tag == 0 ? 100000 : p.tag,
                               "floor trigger plate lowered");
        }
        else
        {
            // kexFloor removes a type-23 thinker once it returns to the top;
            // its sector remains special, so it cannot be stepped on again.
            p.state = kKexPlateDone;
            OSD_Printf("Kex plate: sector %d tag %d reset complete\n", p.sector, p.tag);
        }
    }
}

// ---------------------------------------------------------------------------
// REMOTE_TRIGGER. EnterSectorSpecial() runs type 50 without a ground test;
// SendRemoteTrigger() resets tag+1 switches and live type-23 plates, pulses
// the source tag, and clears the source event only after it activated one of
// 7/8/9/21/22/24. Build's lotag-21 has none of those rules, so conversion
// leaves the sector ordinary and puts this invisible controller in it.
// ---------------------------------------------------------------------------
#define kMaxKexRemotes 512
struct KexRemote
{
    int16_t sector, tag, channel;
    uint8_t oneShot, done;
    uint16_t fires;
};
static KexRemote s_remote[kMaxKexRemotes];
static int s_remotes;
static int s_remoteTest, s_remoteTestRemote, s_remoteTestStage;

void KexRemote_Reset(void)
{
    s_remotes = 0;
    s_remoteTest = -1;
    s_remoteTestRemote = -1;
    s_remoteTestStage = 0;
}

void KexRemote_Add(int nSector, int nTag, int nChannel, int bOneShot)
{
    if (!g_kexMode || s_remotes >= kMaxKexRemotes
        || (unsigned)nSector >= (unsigned)numsectors
        || nChannel < 0 || nChannel >= kMaxChannels)
        return;
    KexRemote &r = s_remote[s_remotes++];
    Bmemset(&r, 0, sizeof(r));
    r.sector = (int16_t)nSector;
    r.tag = (int16_t)nTag;
    // Markers already carry the converter's exact channel mapping. Resolve
    // the index while channel state still names the channel, before movers can
    // replace that state with their own value.
    r.channel = (int16_t)runlist_AllocChannel(nChannel);
    r.oneShot = bOneShot != 0;
}

static void KexRemote_Fire(KexRemote &r)
{
    // ResetWallSwitchFromTag(tag+1) also reverses only *live* floor trigger
    // plates on that tag. KexPlate_ResetChain deliberately has both halves.
    KexPlate_ResetChain((int)r.tag + 1);
    Kex_FireChannel(r.channel);
    r.fires++;
    if (r.oneShot)
        r.done = 1;
}

void KexRemote_Tick(void)
{
    if (!g_kexMode)
        return;
    if (s_remoteTest < 0)
    {
        char const *e = Bgetenv("VB_KEXREMOTE_TEST");
        s_remoteTest = e ? Batoi(e) : 0;
    }
    if (s_remoteTest > 0 && --s_remoteTest == 0)
    {
        // The focused floor-trigger map has one type-50 region that activates
        // type-22 floors, so it is a deterministic source one-shot probe.
        for (int i = 0; i < s_remotes; i++)
            if (s_remote[i].oneShot)
            {
                int const nSprite = PlayerList[nLocalPlayer].nSprite;
                if (nSprite >= 0)
                {
                    mychangespritesect(nSprite, s_remote[i].sector);
                    s_remoteTestRemote = i;
                    s_remoteTestStage = 1;
                    OSD_Printf("Kex remote test: stepping source sector %d tag %d\n",
                               s_remote[i].sector, s_remote[i].tag);
                }
                break;
            }
    }

    for (int i = 0; i < s_remotes; i++)
    {
        KexRemote &r = s_remote[i];
        if (r.done)
            continue;
        for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
        {
            int const nSprite = PlayerList[nPlayer].nSprite;
            if (nSprite < 0 || PlayerList[nPlayer].nHealth <= 0
                || sprite[nSprite].sectnum != r.sector)
                continue;
            KexRemote_Fire(r);
            break;
        }
    }

    if (s_remoteTestStage == 1 && s_remoteTestRemote >= 0
        && s_remote[s_remoteTestRemote].fires > 0)
        s_remoteTestStage = 2;
    else if (s_remoteTestStage == 2)
    {
        KexRemote const &r = s_remote[s_remoteTestRemote];
        bool const okay = r.done && r.fires == 1 && r.channel >= 0
            && sRunChannels[r.channel].c > 0;
        OSD_Printf("Kex remote test: %s (one-shot tag %d, fires %d, channel %d)\n",
                   okay ? "PASS" : "FAIL", r.tag, r.fires, r.channel);
        s_remoteTestStage = 3;
    }
}

// ---------------------------------------------------------------------------
// Floating platforms. The remaster's SetupFloatingPlatforms() turns each
// FLOATER event into a two-surface slab: the original event sector's ceiling
// and the event parameter sector's floor move by the same amount. Its mover
// advances two degrees per tick and uses
//
//   cos(time * 2 degrees + phase + 180 degrees) * height / 2
//
// as its current height. Two variants also shift both surfaces at spawn.
// Build's lotag-95 bob moves one floor with one fixed amplitude and a 16-step
// phase, so kexmap.py instead writes every surviving split surface to
// kexfloat.txt. A Kex unit is 96 Build z units, with the sign inverted.
// ---------------------------------------------------------------------------
#define KEX_FLOAT_RANDOM_PHASE 32767
#define kMaxKexFloatRows       2048
#define kMaxKexFloaters        128

struct KexFloatRow
{
    int16_t group, sector, height, phase, offset;
    uint8_t ceiling;
};

struct KexFloat
{
    int16_t id, height, offset;
    float phase;
    int32_t current, minHeight, maxHeight;
    uint32_t time;
    uint8_t ceilings, floors;
};

struct KexFloatSurface
{
    int16_t sector, floater;
    int32_t base;
    uint8_t ceiling;
};

static KexFloatRow     s_floatRow[kMaxKexFloatRows];
static KexFloat        s_float[kMaxKexFloaters];
static KexFloatSurface s_floatSurface[kMaxKexFloatRows];
static int s_floatRows, s_floaters, s_floatSurfaces, s_floatTest;
static char s_floatMap[32];

void KexFloat_Reset(void)
{
    s_floatRows = s_floaters = s_floatSurfaces = 0;
    s_floatTest = 0;
}

static bool KexFloat_ParseLine(char *line, int nRec)
{
    char map[32];
    int group, nSector, ceiling, height, phase, offset;
    if (Bsscanf(line, "%31s %d %d %d %d %d %d", map, &group, &nSector,
                &ceiling, &height, &phase, &offset) < 7)
        return false;

    Bstrupr(map);
    char *base = map;
    if (!Bstrncmp(base, "KEX_", 4))
        base += 4;
    if (char *dot = Bstrrchr(base, '.'))
        *dot = 0;
    if (Bstrcmp(base, s_floatMap))
        return false;
    if ((unsigned)group > INT16_MAX || (unsigned)nSector >= (unsigned)numsectors
        || (unsigned)height > INT16_MAX || phase < INT16_MIN || phase > INT16_MAX
        || offset < INT16_MIN || offset > INT16_MAX || (ceiling != 0 && ceiling != 1))
        return false;

    KexFloatRow &r = s_floatRow[nRec];
    r.group = (int16_t)group;
    r.sector = (int16_t)nSector;
    r.ceiling = (uint8_t)ceiling;
    r.height = (int16_t)height;
    r.phase = (int16_t)phase;
    r.offset = (int16_t)offset;
    return true;
}

static void KexFloat_MoveSurface(KexFloatSurface const &s, int32_t dz, bool carry)
{
    if (!dz)
        return;
    if (s.ceiling)
        sector[s.sector].ceilingz += dz;
    else
    {
        // Kex creates the mover (and applies its spawn offset) before map
        // actors are spawned, so the load-time shift must not drag already
        // converted sprites with it. During play, carry only actors actually
        // resting on the surface; the classic bob helper moves every sprite
        // in the sector, including CAVERN_PSX's airborne wasp.
        if (carry)
            for (int nSprite = headspritesect[s.sector]; nSprite != -1;
                 nSprite = nextspritesect[nSprite])
                if (sprite[nSprite].statnum != 200
                    && sprite[nSprite].z >= getflorzofslope(s.sector,
                                                           sprite[nSprite].x,
                                                           sprite[nSprite].y) - 256)
                    sprite[nSprite].z += dz;
        sector[s.sector].floorz += dz;
    }
}

void KexFloat_Init(void)
{
    KexFloat_Reset();
    if (!g_kexMode)
        return;

    Kex_LevelName(s_floatMap, sizeof(s_floatMap));
    s_floatRows = KexTable_Load("kexfloat.txt", kMaxKexFloatRows, KexFloat_ParseLine);
    for (int i = 0; i < s_floatRows; i++)
    {
        KexFloatRow const &r = s_floatRow[i];
        int nFloat = -1;
        for (int j = 0; j < s_floaters; j++)
            if (s_float[j].id == r.group)
            {
                nFloat = j;
                break;
            }
        if (nFloat < 0)
        {
            if (s_floaters >= kMaxKexFloaters)
                continue;
            nFloat = s_floaters++;
            KexFloat &f = s_float[nFloat];
            Bmemset(&f, 0, sizeof(f));
            f.id = r.group;
            f.height = r.height;
            f.offset = r.offset;
            f.phase = r.phase == KEX_FLOAT_RANDOM_PHASE
                ? ((RandomSize(16) * (2.f / 65535.f)) - 1.f) * 256.f
                : (float)r.phase;
        }
        KexFloat &f = s_float[nFloat];
        if (r.height != f.height || r.offset != f.offset || s_floatSurfaces >= kMaxKexFloatRows)
            continue;
        KexFloatSurface &s = s_floatSurface[s_floatSurfaces++];
        s.sector = r.sector;
        s.floater = (int16_t)nFloat;
        s.ceiling = r.ceiling;
        if (s.ceiling)
            f.ceilings++;
        else
            f.floors++;
    }

    // Do not move half of a malformed pair. The generated retail table always
    // has at least one ceiling and one floor, even when either Kex sector was
    // split into several Build sectors for slopes or lighting.
    for (int i = 0; i < s_floaters; i++)
    {
        KexFloat &f = s_float[i];
        if (!f.ceilings || !f.floors)
            continue;
        int32_t const dz = -f.offset * KEX_Z_PER_UNIT;
        for (int j = 0; j < s_floatSurfaces; j++)
            if (s_floatSurface[j].floater == i)
                KexFloat_MoveSurface(s_floatSurface[j], dz, false);
    }
    for (int i = 0; i < s_floatSurfaces; i++)
    {
        KexFloatSurface &s = s_floatSurface[i];
        s.base = s.ceiling ? sector[s.sector].ceilingz : sector[s.sector].floorz;
    }

    s_floatTest = Bgetenv("VB_KEXFLOAT_TEST") != NULL;
    if (s_floaters)
        OSD_Printf("Kex floating platforms: %d movers, %d surfaces on this level\n",
                   s_floaters, s_floatSurfaces);
    if (s_floatTest)
        for (int i = 0; i < s_floaters; i++)
            OSD_Printf("Kex float test: group %d height %d phase %.3f offset %d, %d ceilings/%d floors\n",
                       s_float[i].id, s_float[i].height, s_float[i].phase,
                       s_float[i].offset, s_float[i].ceilings, s_float[i].floors);
}

void KexFloat_Tick(void)
{
    if (!g_kexMode || !s_floaters)
        return;

    for (int i = 0; i < s_floaters; i++)
    {
        KexFloat &f = s_float[i];
        if (!f.ceilings || !f.floors)
            continue;
        float const angle = ((float)f.time * 2.f + f.phase + 180.f) * (fPI / 180.f);
        int32_t const target = -Blrintf(cosf(angle) * (f.height * .5f) * KEX_Z_PER_UNIT);
        int32_t const dz = target - f.current;
        f.current = target;
        f.minHeight = min(f.minHeight, target);
        f.maxHeight = max(f.maxHeight, target);
        f.time++;
        for (int j = 0; j < s_floatSurfaces; j++)
            if (s_floatSurface[j].floater == i)
                KexFloat_MoveSurface(s_floatSurface[j], dz, true);
    }

    if (!s_floatTest)
        return;
    int bad = 0;
    for (int i = 0; i < s_floatSurfaces; i++)
    {
        KexFloatSurface const &s = s_floatSurface[i];
        KexFloat const &f = s_float[s.floater];
        int32_t const z = s.ceiling ? sector[s.sector].ceilingz : sector[s.sector].floorz;
        bad += z != s.base + f.current;
    }
    if (bad)
        OSD_Printf("Kex float test: FAIL (%d paired surfaces diverged)\n", bad);
    if (s_float[0].time == 180)
    {
        int valid = 1;
        for (int i = 0; i < s_floaters; i++)
            valid &= s_float[i].ceilings && s_float[i].floors
                && s_float[i].maxHeight - s_float[i].minHeight >= (s_float[i].height - 1) * KEX_Z_PER_UNIT;
        OSD_Printf("Kex float test: %s after one cycle (%d movers/%d surfaces, paired geometry intact)\n",
                   valid && !bad ? "PASS" : "FAIL", s_floaters, s_floatSurfaces);
        s_floatTest = 0;
    }
}

// ---------------------------------------------------------------------------
// DROP_PAD. SetupFloatingPlatforms swaps the event sector with params: the
// parameter floor is stepped on while the original sector ceiling follows it.
// The Kex thinker waits 30 Build ticks, then simulates its two 60 Hz falling
// updates per tick (4+a, 5+a source units) and uses fixed four-unit raises.
// ---------------------------------------------------------------------------
#define kMaxKexPadRows 128
#define kMaxKexPads 32

struct KexPadRow
{
    int16_t group, sector, tag;
    uint8_t ceiling;
};

struct KexPadSurface
{
    int16_t pad, sector;
    uint8_t ceiling;
    int32_t base;
};

struct KexPad
{
    int16_t id, chan, tag;
    int32_t delta, current;
    int16_t countdown, accel;
    uint8_t state, floors, ceilings;
};

enum
{
    kKexPadIdle,
    kKexPadCountdown,
    kKexPadFalling,
    kKexPadDown,
    kKexPadRaise,
};

static KexPadRow s_padRow[kMaxKexPadRows];
static KexPad s_pad[kMaxKexPads];
static KexPadSurface s_padSurface[kMaxKexPadRows];
static int s_padRows, s_pads, s_padSurfaces, s_padTest, s_padTestPad, s_padTestStage, s_padTestTicks;
static char s_padMap[32];

void KexPad_Reset(void)
{
    s_padRows = s_pads = s_padSurfaces = 0;
    s_padTest = s_padTestStage = s_padTestTicks = 0;
    s_padTestPad = -1;
}

static bool KexPad_ParseLine(char *line, int nRec)
{
    char map[32];
    int group, sec, ceil, tag;
    if (Bsscanf(line, "%31s %d %d %d %d", map, &group, &sec, &ceil, &tag) < 5)
        return false;
    Bstrupr(map);
    char *base = !Bstrncmp(map, "KEX_", 4) ? map + 4 : map;
    if (char *dot = Bstrrchr(base, '.'))
        *dot = 0;
    if (Bstrcmp(base, s_padMap) || (unsigned)group > INT16_MAX
        || (unsigned)sec >= (unsigned)numsectors || (ceil != 0 && ceil != 1))
        return false;
    s_padRow[nRec] = { (int16_t)group, (int16_t)sec, (int16_t)tag, (uint8_t)ceil };
    return true;
}

static bool KexPad_Valid(KexPad const &p)
{
    return p.floors && p.ceilings && p.delta > 0;
}

static void KexPad_Start(KexPad &p)
{
    if (p.state != kKexPadIdle)
        return;
    p.countdown = 30;       // 30 Build ticks = the KEX 30.0 / .5f delay
    p.accel = 0;
    p.current = 0;
    p.state = kKexPadCountdown;
}

static void KexPad_Move(KexPad &p, int32_t dz)
{
    int const nPad = &p - s_pad;
    for (int i = 0; i < s_padSurfaces; i++)
    {
        if (s_padSurface[i].pad != nPad)
            continue;
        KexFloatSurface s = { s_padSurface[i].sector, 0, s_padSurface[i].base,
                              s_padSurface[i].ceiling };
        KexFloat_MoveSurface(s, dz, true);
    }
    p.current += dz;
}

static int KexPad_GeometryErrors(KexPad const &p)
{
    int const nPad = &p - s_pad;
    int bad = 0;
    for (int i = 0; i < s_padSurfaces; i++)
    {
        KexPadSurface const &s = s_padSurface[i];
        if (s.pad != nPad)
            continue;
        int32_t const z = s.ceiling ? sector[s.sector].ceilingz : sector[s.sector].floorz;
        bad += z != s.base + p.current;
    }
    return bad;
}

// kexDropPad::Tick assigns the local player's ShakeTime to 16 and plays this
// one-shot exactly when the endpoint is observed (the tick after its final
// clamped move).  The mover belongs to the parameter sector after
// SetupFloatingPlatforms(), so its first floor surface is the correct audible
// pad position rather than the linked ceiling surface.
static void KexPad_Land(KexPad const &p)
{
    int const nPad = &p - s_pad;
    KexQuake(16);
    for (int i = 0; i < s_padSurfaces; i++)
    {
        KexPadSurface const &surface = s_padSurface[i];
        if (surface.pad != nPad || surface.ceiling)
            continue;
        int const nSector = surface.sector;
        int const nWalls = sector[nSector].wallnum;
        int x = 0, y = 0;
        for (int w = sector[nSector].wallptr; w < sector[nSector].wallptr + nWalls; w++)
        {
            x += wall[w].x;
            y += wall[w].y;
        }
        if (nWalls)
        {
            x /= nWalls;
            y /= nWalls;
        }
        int const sound = KexSound_Get("k_padfall");
        if (sound >= 0)
            PlayFXAtXYZ(sound, x, y, sector[nSector].floorz, nSector);
        if (s_padTest && nPad == s_padTestPad)
        {
            char const * const path = KexSound_Alias("k_padfall", NULL);
            OSD_Printf("Kex drop-pad landing test: %s (sounds/platfall.wav, local 16-tick quake)\n",
                       path && !Bstrcasecmp(path, "sounds/platfall.wav") ? "PASS" : "FAIL");
        }
        return;
    }
}

void KexPad_Init(void)
{
    KexPad_Reset();
    if (!g_kexMode)
        return;
    Kex_LevelName(s_padMap, sizeof(s_padMap));
    s_padRows = KexTable_Load("kexpad.txt", kMaxKexPadRows, KexPad_ParseLine);

    for (int i = 0; i < s_padRows; i++)
    {
        KexPadRow const &r = s_padRow[i];
        int nPad = -1;
        for (int j = 0; j < s_pads; j++)
            if (s_pad[j].id == r.group)
            {
                nPad = j;
                break;
            }
        if (nPad < 0)
        {
            if (s_pads >= kMaxKexPads)
                continue;
            nPad = s_pads++;
            KexPad &p = s_pad[nPad];
            Bmemset(&p, 0, sizeof(p));
            p.id = r.group;
            p.tag = r.tag;
            int const chan = KexChan(r.tag);
            p.chan = chan >= 0 ? runlist_AllocChannel(chan) : -1;
        }
        if (s_padSurfaces >= kMaxKexPadRows)
            continue;
        KexPadSurface &s = s_padSurface[s_padSurfaces++];
        s.pad = nPad;
        s.sector = r.sector;
        s.ceiling = r.ceiling;
        s.base = s.ceiling ? sector[s.sector].ceilingz : sector[s.sector].floorz;
        if (s.ceiling)
            s_pad[nPad].ceilings++;
        else
            s_pad[nPad].floors++;
    }

    for (int i = 0; i < s_pads; i++)
        for (int j = 0; j < s_padSurfaces; j++)
            if (s_padSurface[j].pad == i && s_padSurface[j].ceiling)
            {
                int32_t const delta = sector[s_padSurface[j].sector].floorz
                    - sector[s_padSurface[j].sector].ceilingz;
                if (!s_pad[i].delta)
                    s_pad[i].delta = delta;
                else if (s_pad[i].delta != delta)
                {
                    OSD_Printf(OSD_ERROR "Kex drop pad %d has mismatched linked heights\n",
                               s_pad[i].id);
                    s_pad[i].delta = INT32_MIN;
                }
            }

    char const *padTest = Bgetenv("VB_KEXPAD_TEST");
    s_padTest = padTest != NULL;
    if (s_pads)
        OSD_Printf("Kex drop pads: %d movers, %d paired surfaces on this level\n", s_pads, s_padSurfaces);
    if (s_padTest)
    {
        // "VB_KEXPAD_TEST=5200" selects the pad using that (possibly
        // remapped) Kex tag. Any other nonempty value uses the first pad.
        int const requestedTag = Batoi(padTest);
        s_padTestPad = requestedTag > 1 ? -1 : 0;
        if (requestedTag > 1)
            for (int i = 0; i < s_pads; i++)
                if (s_pad[i].tag == requestedTag)
                {
                    s_padTestPad = i;
                    break;
                }
        if (s_padTestPad < 0 || s_padTestPad >= s_pads || !KexPad_Valid(s_pad[s_padTestPad]))
        {
            OSD_Printf(OSD_ERROR "Kex drop-pad test: no valid pad selected\n");
            s_padTest = 0;
            return;
        }
        KexPad &p = s_pad[s_padTestPad];
        KexPad_Start(p);
        OSD_Printf("Kex drop-pad test: group %d tag %d starts (%d Build-z travel, 30-tick delay)\n",
                   p.id, p.tag, p.delta);
    }
}

void KexPad_ChannelFired(int chan)
{
    if (!g_kexMode || chan < 0)
        return;
    for (int i = 0; i < s_pads; i++)
        if (s_pad[i].chan == chan && s_pad[i].state == kKexPadDown)
        {
            s_pad[i].accel = 0;
            s_pad[i].state = kKexPadRaise;
        }
}

void KexPad_Tick(void)
{
    if (!g_kexMode || !s_pads)
        return;

    if (s_padTest)
        s_padTestTicks++;

    for (int i = 0; i < s_pads; i++)
    {
        KexPad &p = s_pad[i];
        if (!KexPad_Valid(p))
            continue;

        if (p.state == kKexPadIdle)
        {
            for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
            {
                for (int j = 0; j < s_padSurfaces; j++)
                    if (s_padSurface[j].pad == i && !s_padSurface[j].ceiling
                        && KexPlayer_OnSectorFloor(nPlayer, s_padSurface[j].sector))
                    {
                        KexPad_Start(p);
                        break;
                    }
                if (p.state != kKexPadIdle)
                    break;
            }
            continue;
        }

        if (p.state == kKexPadCountdown)
        {
            if (--p.countdown <= 0)
                p.state = kKexPadFalling;
            continue;
        }

        // The native 60 Hz mover checks the endpoint before each move. Two
        // such updates belong in each Build game tick, including the one
        // endpoint-only update after a clamped final move.
        for (int sub = 0; sub < 2; sub++)
        {
            if (p.state == kKexPadFalling)
            {
                if (p.current >= p.delta)
                {
                    KexPad_Land(p);
                    p.state = kKexPadDown;
                    KexSwitch_ResetByTag(p.tag);
                    if (s_padTest && !s_padTestStage && i == s_padTestPad)
                    {
                        int const bad = KexPad_GeometryErrors(p);
                        if (p.chan < 0)
                        {
                            // The focused platform map gives its pad the
                            // Kex no-tag sentinel: landing must latch it
                            // down instead of inventing a reset channel.
                            OSD_Printf("Kex drop-pad test: %s lower latch after %d ticks (no reset tag, paired geometry %s)\n",
                                       bad ? "FAIL" : "PASS", s_padTestTicks,
                                       bad ? "diverged" : "intact");
                            s_padTest = 0;
                        }
                        else
                        {
                            s_padTestStage = 1;
                            Kex_FireChannel(p.chan);
                            OSD_Printf("Kex drop-pad test: lower endpoint after %d ticks; reset tag %d fired\n",
                                       s_padTestTicks, p.tag);
                        }
                    }
                    break;
                }
                KexPad_Move(p, min(p.delta - p.current,
                                   (4 + p.accel++) * KEX_Z_PER_UNIT));
            }
            else if (p.state == kKexPadRaise)
            {
                if (p.current <= 0)
                {
                    p.current = 0;
                    p.state = kKexPadIdle;
                    if (s_padTest && s_padTestStage == 2 && i == s_padTestPad)
                    {
                        int const bad = KexPad_GeometryErrors(p);
                        OSD_Printf("Kex drop-pad test: %s after %d ticks (paired geometry %s)\n",
                                   bad ? "FAIL" : "PASS", s_padTestTicks,
                                   bad ? "diverged" : "intact");
                        s_padTest = 0;
                    }
                    break;
                }
                KexPad_Move(p, -min(p.current, 4 * KEX_Z_PER_UNIT));
            }
        }

        if (s_padTest && s_padTestStage == 1 && p.state == kKexPadRaise && i == s_padTestPad)
        {
            s_padTestStage = 2;
            OSD_Printf("Kex drop-pad test: tag %d reset reached the native pad listener\n", p.tag);
        }

        if (s_padTest && i == s_padTestPad && KexPad_GeometryErrors(p))
            OSD_Printf("Kex drop-pad test: FAIL (paired surfaces diverged at tick %d)\n",
                       s_padTestTicks);
    }
}

// ---------------------------------------------------------------------------
// Laser beams. The remaster draws a beam as a run of 16x16 bolt sprites
// (projectiles/beam_loop, A_BoltJitter) from the emitter to the first wall
// the beam meets; Proj_Beam does 100 on contact. Built here from the origin
// the converter plants: a hitscan finds the far end, a bolt every
// KEX_BOLT_STEP units along it, jittered a little each tick the way the
// remaster's are, hidden while the beam is off.
// ---------------------------------------------------------------------------
#define kMaxKexBeams    64
#define kMaxKexBolts    1024       // pool for all beams on the level (COLONY_SAT: 55 beams)
#define KEX_BOLT_STEP   96         // Build units between bolts: the 16px bolt at repeat 24 is 96 wide
#define KEX_BEAM_REACH  96         // how near the line counts as touching it
#define KEX_BEAM_DAMAGE 100
#define KEX_BEAM_COOLDOWN 30       // ticks between hits on one player

struct KexBeam
{
    int16_t nSprite;               // the hidden origin
    int16_t chan;                  // -1 = always on
    int16_t c0;                    // the channel's state at load: on while it stays so
    int32_t x0, y0, x1, y1, z;
    int16_t first, nBolts;         // its run in s_bolt[]
    uint8_t on;
    int16_t cooldown[kMaxPlayers];
};
struct KexBolt { int16_t nSprite; int32_t x, y, z; };

static KexBeam  s_beam[kMaxKexBeams];
static int      s_beams;
static KexBolt  s_bolt[kMaxKexBolts];
static int      s_bolts;
static int      s_beamTile = -1, s_beamRepeat = 24;
static int      s_beamTableRead;
static uint32_t s_beamSeed = 0x9E3779B9u;   // local jitter RNG: never the game's

static bool KexBeam_ParseLine(char *line, int)
{
    int tile, rep;
    if (Bsscanf(line, "beam %d %d", &tile, &rep) < 2)
        return false;
    s_beamTile = tile; s_beamRepeat = clamp(rep, 1, 255);
    return true;
}

void KexBeam_Reset(void) { s_beams = 0; s_bolts = 0; }

void KexBeam_Add(int nSprite, int nChannel)
{
    if (!g_kexMode || s_beams >= kMaxKexBeams)
    {
        mydeletesprite(nSprite);
        return;
    }
    KexBeam &b = s_beam[s_beams++];
    Bmemset(&b, 0, sizeof(b));
    b.nSprite = (int16_t)nSprite;
    b.chan = (int16_t)nChannel;
    b.x0 = sprite[nSprite].x; b.y0 = sprite[nSprite].y; b.z = sprite[nSprite].z;
    b.x1 = b.x0; b.y1 = b.y0;
    sprite[nSprite].cstat = 0x8000;
    changespritestat(nSprite, 0);
}

void KexBeam_Init(void)
{
    if (!s_beamTableRead)
    {
        s_beamTableRead = 1;
        KexTable_Load("kextrap.txt", 1, KexBeam_ParseLine);
    }
    if (!s_beams)
        return;
    if (s_beamTile < 0 || tilesiz[s_beamTile].x <= 0)
    {
        OSD_Printf("Kex beams: %d beams but no bolt tile (kextrap.txt); left dark\n", s_beams);
        s_beams = 0;
        return;
    }

    // Two passes: the scans first, so the level's total beam length can set
    // the spacing -- COLONY_SAT's 55 beams would otherwise run the pool dry
    // and leave the last of them dark.
    int total = 0;
    for (int i = 0; i < s_beams; i++)
    {
        KexBeam &b = s_beam[i];
        int const nSprite = b.nSprite;
        int const ang = sprite[nSprite].ang;
        // Emitters are authored on their wall, and a few a step above their
        // ceiling: keep the beam inside the room, and start the scan a stride
        // into it so the wall it hangs on is not the first thing it hits.
        int32_t cz, fz;
        getzsofslope(sprite[nSprite].sectnum, b.x0, b.y0, &cz, &fz);
        if (fz - cz > 2048)
            b.z = clamp(b.z, cz + 1024, fz - 1024);
        vec3_t start = { b.x0 + mulscale14(Cos(ang), 48), b.y0 + mulscale14(Sin(ang), 48), b.z };
        hitdata_t hit;
        hitscan(&start, sprite[nSprite].sectnum, Cos(ang), Sin(ang), 0, &hit, CLIPMASK1);
        if (hit.sect < 0)
            continue;
        b.x1 = hit.pos.x; b.y1 = hit.pos.y;
        total += ksqrt((b.x1 - b.x0) * (b.x1 - b.x0) + (b.y1 - b.y0) * (b.y1 - b.y0));
    }
    int const step = max(KEX_BOLT_STEP, total / (kMaxKexBolts - s_beams) + 1);

    for (int i = 0; i < s_beams; i++)
    {
        KexBeam &b = s_beam[i];
        int const nSprite = b.nSprite;
        int const len = ksqrt((b.x1 - b.x0) * (b.x1 - b.x0) + (b.y1 - b.y0) * (b.y1 - b.y0));
        if (len <= 0)
            continue;
        int n = len / step;
        if (n < 1) n = 1;
        b.first = (int16_t)s_bolts;
        int16_t nSector = sprite[nSprite].sectnum;
        for (int k = 0; k < n && s_bolts < kMaxKexBolts; k++)
        {
            // Bolts sit mid-step so the run starts just off the wall.
            int const t = step / 2 + k * step;
            int const bx = b.x0 + scale(b.x1 - b.x0, t, max(1, len));
            int const by = b.y0 + scale(b.y1 - b.y0, t, max(1, len));
            updatesector(bx, by, &nSector);
            if (nSector < 0)
                break;
            int const nBolt = insertsprite(nSector, 0);
            if (nBolt < 0)
                break;
            sprite[nBolt].x = bx; sprite[nBolt].y = by; sprite[nBolt].z = b.z;
            sprite[nBolt].cstat = 0;
            sprite[nBolt].picnum = (int16_t)s_beamTile;
            sprite[nBolt].shade = -127;
            sprite[nBolt].pal = 0;
            sprite[nBolt].xrepeat = sprite[nBolt].yrepeat = (uint8_t)s_beamRepeat;
            sprite[nBolt].xoffset = sprite[nBolt].yoffset = 0;
            sprite[nBolt].owner = -1;
            sprite[nBolt].lotag = sprite[nBolt].hitag = 0;
            s_bolt[s_bolts].nSprite = (int16_t)nBolt;
            s_bolt[s_bolts].x = bx; s_bolt[s_bolts].y = by; s_bolt[s_bolts].z = b.z;
            s_bolts++;
            b.nBolts++;
        }
        b.on = 1;
        // A channel's state starts out as its tag number (runlist_AllocChannel
        // matches channels by that value), so "off" is any change from it.
        b.c0 = b.chan >= 0 ? sRunChannels[b.chan].c : 0;
        if (getenv("VB_KEXBEAM_DEBUG"))
            OSD_Printf("Kex beam %d: sector %d ang %d len %d bolts %d chan %d (c=%d) z %d\n",
                       i, sprite[nSprite].sectnum, sprite[nSprite].ang, len, b.nBolts, b.chan, b.c0, b.z);
    }
    OSD_Printf("Kex beams: %d beams, %d bolts on this level (step %d, bolt tile %d, %dx%d, repeat %d)\n",
               s_beams, s_bolts, step, s_beamTile, tilesiz[s_beamTile].x, tilesiz[s_beamTile].y, s_beamRepeat);
}

static inline int KexBeam_Rand(int span)
{
    s_beamSeed = s_beamSeed * 1664525u + 1013904223u;
    return (int)((s_beamSeed >> 16) % (unsigned)(2 * span + 1)) - span;
}

// VB_KEX_LOOKAT=<statnum>: a moment in, stand the local player a little way
// from the first sprite of that statnum, facing it -- for looking at a
// creature without a hand on the keys.
static int KexChan(int tag);   // below

static void Kex_LookAtDebug(void)
{
    static int s_ticks = -1, s_stat = -1, s_pic0 = -1, s_pic1 = -1;
    if (s_ticks < 0)
    {
        char const *e = getenv("VB_KEX_LOOKAT");
        s_stat = e ? Batoi(e) : -1;
        s_ticks = e ? 90 : 0;
        // VB_KEX_LOOKAT_TILE=<lo>-<hi>: only a sprite whose picnum is in
        // that range -- the creatures share statnum 99, and a skinned one
        // (scorpion 7700+) is told apart by its tiles.
        char const *t = getenv("VB_KEX_LOOKAT_TILE");
        if (t && sscanf(t, "%d-%d", &s_pic0, &s_pic1) != 2)
            s_pic0 = s_pic1 = Batoi(t);
    }
    static int s_target = -1;
    int const nSprite = PlayerList[nLocalPlayer].nSprite;
    // VB_KEX_LOOKAT_TAG=<kex tag>: instead of a sprite, the first sector
    // whose hitag is that tag's channel (a door or lift) -- the player stands
    // in a neighbouring sector, facing it, to watch it fire.
    static int s_tag = -2, s_tagTicks = 0, s_tagSector = -1;
    // VB_KEX_LOOKAT_OVERPIC=<tile>: stand before the first masked wall with
    // that overpicnum (a grate, a force field), 640 units back in its sector.
    static int s_over = -2, s_overTicks = 0;
    if (s_over == -2)
    { char const *e = getenv("VB_KEX_LOOKAT_OVERPIC"); s_over = e ? Batoi(e) : -1; }
    if (s_over >= 0 && ++s_overTicks == 90)
    {
        for (int w = 0; w < numwalls; w++)
        {
            if (wall[w].overpicnum != s_over || !(wall[w].cstat & 16))
                continue;
            int const w2 = wall[w].point2;
            int const mx = (wall[w].x + wall[w2].x) / 2, my = (wall[w].y + wall[w2].y) / 2;
            int const dx = wall[w2].x - wall[w].x, dy = wall[w2].y - wall[w].y;
            int const ang = getangle(dx, dy);
            // the wall's inside is to the right of its direction in Build's y-down space
            int const px = mx + mulscale14(Cos((ang + 512) & kAngleMask), 640), py = my + mulscale14(Sin((ang + 512) & kAngleMask), 640);
            int16_t ns = (int16_t)sectorofwall(w);
            updatesector(px, py, &ns);
            if (ns < 0) continue;
            sprite[nSprite].x = px; sprite[nSprite].y = py; sprite[nSprite].z = sector[ns].floorz - 2048;
            mychangespritesect(nSprite, ns);
            int const look = getangle(mx - px, my - py);
            PlayerList[nLocalPlayer].q16angle = fix16_from_int(look); PlayerList[nLocalPlayer].q16oangle = PlayerList[nLocalPlayer].q16angle;
            sprite[nSprite].ang = (int16_t)look; PlayerList[nLocalPlayer].opos = sprite[nSprite].xyz;
            OSD_Printf("Kex look-at: masked wall %d (overpicnum %d, cstat %d) from sector %d\n", w, s_over, wall[w].cstat, ns);
            break;
        }
        s_over = -1;
    }
    if (s_tag == -2)
    { char const *e = getenv("VB_KEX_LOOKAT_TAG"); s_tag = e ? Batoi(e) : -1; }
    if (s_tag >= 0 && s_target < 0 && ++s_tagTicks == 90)
    {
        int const chan = KexChan(s_tag);
        // runlist_ProcessSectorTag() clears a sector's tags once its mover is
        // built, so the tags are read back from the map file.
        static int16_t s_fileTags[kMaxSectors][2];
        int nFile = 0;
        buildvfs_kfd hMap = kopen4loadfrommod(gUserMapFilename, 0);
        if (hMap != buildvfs_kfd_invalid)
        {
            uint8_t hdr[22];
            if (kread(hMap, hdr, 22) == 22)
            {
                nFile = min<int>(B_LITTLE16(*(uint16_t *)&hdr[20]), kMaxSectors);
                for (int si = 0; si < nFile; si++)
                {
                    uint8_t rec[40];
                    if (kread(hMap, rec, 40) != 40) { nFile = si; break; }
                    s_fileTags[si][0] = (int16_t)B_LITTLE16(*(uint16_t *)&rec[34]);
                    s_fileTags[si][1] = (int16_t)B_LITTLE16(*(uint16_t *)&rec[36]);
                }
            }
            kclose(hMap);
        }
        OSD_Printf("Kex look-at: tag %d is channel %d, %d sectors (%d tagged in the file)\n", s_tag, chan, numsectors, nFile);
        for (int si = 0; si < numsectors && si < nFile && chan >= 0; si++)
        {
            if (s_fileTags[si][1] % 1000 != chan || !s_fileTags[si][0])
                continue;
            int nb = -1, cx = 0, cy = 0, n = 0;
            for (int w = sector[si].wallptr; w < sector[si].wallptr + sector[si].wallnum; w++)
            {
                cx += wall[w].x; cy += wall[w].y; n++;
                if (nb < 0 && wall[w].nextsector >= 0 && sector[wall[w].nextsector].floorz > sector[wall[w].nextsector].ceilingz + 2048)
                    nb = wall[w].nextsector;
            }
            if (nb < 0 || !n)
            {
                OSD_Printf("Kex look-at: tag %d sector %d (lotag %d) has no tall neighbour (%d walls)\n", s_tag, si, s_fileTags[si][0], n);
                continue;
            }
            cx /= n; cy /= n;
            int px = 0, py = 0, m = 0;
            for (int w = sector[nb].wallptr; w < sector[nb].wallptr + sector[nb].wallnum; w++)
            { px += wall[w].x; py += wall[w].y; m++; }
            px /= m; py /= m;
            if (getenv("VB_KEX_LOOKAT_STAND"))
            {
                // ... or stand ON it (a pad, a lift): its centre, its floor.
                sprite[nSprite].x = cx; sprite[nSprite].y = cy;
                sprite[nSprite].z = sector[si].floorz - 2048;
                mychangespritesect(nSprite, (int16_t)si);
                PlayerList[nLocalPlayer].opos = sprite[nSprite].xyz;
                // Trace a door on the same channel if there is one, else the
                // sector stood on: a plate is stood on to watch its door.
                s_tagSector = si;
                for (int sj = 0; sj < numsectors && sj < nFile; sj++)
                    if (sj != si && s_fileTags[sj][1] % 1000 == chan && s_fileTags[sj][0] == 24)
                    { s_tagSector = sj; break; }
                OSD_Printf("Kex look-at: tag %d -> standing on sector %d, tracing sector %d\n", s_tag, si, s_tagSector);
                break;
            }
            // Back off along the line from the door, as far as the sectors
            // allow (up to 1536 units), so the whole door is in view.
            {
                int const ang = getangle(px - cx, py - cy);
                int16_t ns = (int16_t)nb;
                for (int d = 256; d <= 1536; d += 256)
                {
                    int const tx = cx + mulscale14(Cos(ang), d), ty = cy + mulscale14(Sin(ang), d);
                    int16_t ts = (int16_t)nb;
                    updatesector(tx, ty, &ts);
                    if (ts < 0 || sector[ts].floorz - sector[ts].ceilingz < 4096)
                        break;
                    px = tx; py = ty; ns = ts;
                }
                nb = ns;
            }
            sprite[nSprite].x = px; sprite[nSprite].y = py;
            sprite[nSprite].z = sector[nb].floorz - 2048;
            mychangespritesect(nSprite, (int16_t)nb);
            int const look = getangle(cx - px, cy - py);
            PlayerList[nLocalPlayer].q16angle = fix16_from_int(look);
            PlayerList[nLocalPlayer].q16oangle = PlayerList[nLocalPlayer].q16angle;
            sprite[nSprite].ang = (int16_t)look;
            PlayerList[nLocalPlayer].opos = sprite[nSprite].xyz;
            OSD_Printf("Kex look-at: tag %d -> sector %d (lotag %d) from sector %d\n", s_tag, si, s_fileTags[si][0], nb);
            s_tagSector = si;
            break;
        }
        s_tag = -1;
    }
    // VB_KEX_LOOKAT_PRESS=1: 40 ticks after the look-at, press the first
    // wall switch (file lotag 7) on the traced tag's channel, the way the
    // player's use key does, so the door it drives is seen to answer.
    static int s_pressTicks = -1, s_pressWall = -1;
    if (s_tagSector >= 0 && s_pressTicks < 0 && getenv("VB_KEX_LOOKAT_PRESS"))
    {
        s_pressTicks = 40;
        buildvfs_kfd hMap = kopen4loadfrommod(gUserMapFilename, 0);
        if (hMap != buildvfs_kfd_invalid)
        {
            uint8_t hdr[22];
            if (kread(hMap, hdr, 22) == 22)
            {
                int const ns = B_LITTLE16(*(uint16_t *)&hdr[20]);
                klseek(hMap, 40 * ns, SEEK_CUR);
                uint8_t cnt[2];
                if (kread(hMap, cnt, 2) == 2)
                {
                    int const nw = B_LITTLE16(*(uint16_t *)cnt);
                    int const chan = KexChan(Batoi(getenv("VB_KEX_LOOKAT_TAG")));
                    for (int w = 0; w < nw && w < numwalls; w++)
                    {
                        uint8_t rec[32];
                        if (kread(hMap, rec, 32) != 32) break;
                        if ((int16_t)B_LITTLE16(*(uint16_t *)&rec[26]) == 7 && (int16_t)B_LITTLE16(*(uint16_t *)&rec[28]) % 1000 == chan)
                        { s_pressWall = w; break; }
                    }
                }
            }
            kclose(hMap);
        }
        OSD_Printf("Kex look-at: will press wall %d\n", s_pressWall);
    }
    if (s_pressTicks > 0 && --s_pressTicks == 0 && s_pressWall >= 0)
    {
        runlist_SignalRun(wall[s_pressWall].lotag - 1, nLocalPlayer | 0x40000);
        OSD_Printf("Kex look-at: pressed wall %d\n", s_pressWall);
    }
    // ... and report the sector's heights every two seconds for a while, so
    // a door that opens is seen to open even off camera.
    if (s_tagSector >= 0 && ++s_tagTicks <= 90 + 10 * 40 && (s_tagTicks - 90) % 10 == 0)
        OSD_Printf("Kex look-at: sector %d ceiling %d floor %d\n", s_tagSector, sector[s_tagSector].ceilingz, sector[s_tagSector].floorz);
    if (s_target >= 0)
    {
        // Keep facing it for a while: a creature moves.
        int const look = getangle(sprite[s_target].x - sprite[nSprite].x, sprite[s_target].y - sprite[nSprite].y);
        PlayerList[nLocalPlayer].q16angle = fix16_from_int(look);
        PlayerList[nLocalPlayer].q16oangle = PlayerList[nLocalPlayer].q16angle;
        sprite[nSprite].ang = (int16_t)look;
        if (++s_ticks > 400)
            s_target = -1;
        return;
    }
    if (s_ticks <= 0 || --s_ticks != 0)
        return;
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum != s_stat || i == nSprite)
            continue;
        if (s_pic0 >= 0 && (sprite[i].picnum < s_pic0 || sprite[i].picnum > s_pic1))
            continue;
        int const ang = (sprite[i].ang + 1024) & kAngleMask;   // face it from its front
        int const px = sprite[i].x + mulscale14(Cos(ang), 600), py = sprite[i].y + mulscale14(Sin(ang), 600);
        int16_t nSector = sprite[i].sectnum;
        updatesector(px, py, &nSector);
        if (nSector < 0)
            continue;
        sprite[nSprite].x = px; sprite[nSprite].y = py;
        sprite[nSprite].z = sprite[i].z;
        mychangespritesect(nSprite, nSector);
        int const look = getangle(sprite[i].x - px, sprite[i].y - py);
        PlayerList[nLocalPlayer].q16angle = fix16_from_int(look);
        PlayerList[nLocalPlayer].q16oangle = PlayerList[nLocalPlayer].q16angle;
        sprite[nSprite].ang = (int16_t)look;
        PlayerList[nLocalPlayer].opos = sprite[nSprite].xyz;
        OSD_Printf("Kex look-at: sprite %d (statnum %d, picnum %d) from sector %d\n", i, s_stat, sprite[i].picnum, nSector);
        s_target = i; s_ticks = 0;
        return;
    }
}

static void KexToggle_SaveTestMutate(void);  // defined with the Toggle state below

// VB_KEXSAVE_TEST=1: save at tick 100 and load the save back at 160, then
// compare the player's place and health -- the per-sector tables the save
// carries are kMaxSectors wide, and the levels past 1024 sectors are where
// that has to hold.
static void Kex_SaveTest(void)
{
    static int s_ticks = -1, sx, sy, sect, hp, queenPrepared;
    if (s_ticks < 0)
        s_ticks = getenv("VB_KEXSAVE_TEST") ? 0 : 1000000;
    if (s_ticks >= 1000000)
        return;
    int const nSprite = PlayerList[nLocalPlayer].nSprite;
    if (++s_ticks == 100)
    {
        queenPrepared = !Bgetenv("VB_KEX_QUEEN_SAVE_TEST")
            || KexQueen_SaveTestPrepare();
        sx = sprite[nSprite].x; sy = sprite[nSprite].y; sect = sprite[nSprite].sectnum; hp = PlayerList[nLocalPlayer].nHealth;
        void testsave(); testsave();
        OSD_Printf("Kex save test: saved at (%d,%d) sector %d health %d, %d sectors\n", sx, sy, sect, hp, numsectors);
    }
    else if (s_ticks == 120 && Bgetenv("VB_KEX_TOGGLE_SAVE_TEST"))
        KexToggle_SaveTestMutate();
    else if (s_ticks == 160)
    {
        void testload(); testload();
        int const n2 = PlayerList[nLocalPlayer].nSprite;
        bool const playerOkay = sprite[n2].sectnum == sect
            && PlayerList[nLocalPlayer].nHealth == hp;
        bool const queenOkay = !Bgetenv("VB_KEX_QUEEN_SAVE_TEST")
            || (queenPrepared && KexQueen_SaveTestValidate());
        OSD_Printf("Kex save test: loaded at (%d,%d) sector %d health %d, %d sectors -> %s\n",
                   sprite[n2].x, sprite[n2].y, sprite[n2].sectnum, PlayerList[nLocalPlayer].nHealth, numsectors,
                   playerOkay && queenOkay ? "OK" : "MISMATCH");
    }
}

void KexBeam_Tick(void)
{
    Kex_LookAtDebug();
    KexSwitch_TestTick();
    KexSealedDoor_Tick();
    KexScript_BossTick();
    Kex_SaveTest();
    // VB_KEXBEAM_DEBUG=<n>: a moment in, stand the local player beside beam
    // n, looking along it, for a visual check without a hand on the keys.
    static int s_dbgTicks = -1, s_dbgBeam = -1;
    if (s_dbgTicks < 0)
    {
        char const *e = getenv("VB_KEXBEAM_DEBUG");
        s_dbgBeam = e ? Batoi(e) : -1;
        s_dbgTicks = e ? 60 : 0;
    }
    if (s_dbgTicks > 0 && --s_dbgTicks == 0 && (unsigned)s_dbgBeam < (unsigned)s_beams)
    {
        KexBeam const &b = s_beam[s_dbgBeam];
        int const nSprite = PlayerList[nLocalPlayer].nSprite;
        int const dx = b.x1 - b.x0, dy = b.y1 - b.y0;
        int const len = max(1, ksqrt(dx * dx + dy * dy));
        int const mx = (b.x0 + b.x1) / 2, my = (b.y0 + b.y1) / 2;
        int px = 0, py = 0;
        int16_t nSector = -1;
        for (int side = 1; side >= -1 && nSector < 0; side -= 2)   // whichever side is walkable
        {
            px = mx - dy * 480 * side / len; py = my + dx * 480 * side / len;
            nSector = sprite[b.nSprite].sectnum;
            updatesector(px, py, &nSector);
            if (nSector >= 0 && SectDamage[nSector])
                nSector = -1;
        }
        if (nSector >= 0)
        {
            sprite[nSprite].x = px; sprite[nSprite].y = py;
            sprite[nSprite].z = b.z + 6144;
            mychangespritesect(nSprite, nSector);
            int const ang = getangle(mx - px, my - py);
            PlayerList[nLocalPlayer].q16angle = fix16_from_int(ang);
            PlayerList[nLocalPlayer].q16oangle = PlayerList[nLocalPlayer].q16angle;
            sprite[nSprite].ang = (int16_t)ang;
            PlayerList[nLocalPlayer].opos = sprite[nSprite].xyz;
            OSD_Printf("Kex beam debug: standing beside beam %d in sector %d\n", s_dbgBeam, nSector);
        }
        else
            OSD_Printf("Kex beam debug: no sector beside beam %d\n", s_dbgBeam);
    }

    for (int i = 0; i < s_beams; i++)
    {
        KexBeam &b = s_beam[i];
        if (!b.nBolts)
            continue;
        int const on = b.chan < 0 || sRunChannels[b.chan].c == b.c0;
        if (on != b.on)
        {
            b.on = (uint8_t)on;
            for (int k = 0; k < b.nBolts; k++)
            {
                int const nBolt = s_bolt[b.first + k].nSprite;
                if (on) sprite[nBolt].cstat &= ~0x8000;
                else    sprite[nBolt].cstat |= 0x8000;
            }
        }
        if (!on)
            continue;

        // The jitter: a few units either way, along the perpendicular and up
        // and down, re-rolled every tick like A_BoltJitter.
        int const dx = b.x1 - b.x0, dy = b.y1 - b.y0;
        int const len = max(1, ksqrt(dx * dx + dy * dy));
        int const px = -dy * 24 / len, py = dx * 24 / len;
        for (int k = 0; k < b.nBolts; k++)
        {
            KexBolt const &bt = s_bolt[b.first + k];
            int const j = KexBeam_Rand(1);
            sprite[bt.nSprite].x = bt.x + px * j;
            sprite[bt.nSprite].y = bt.y + py * j;
            sprite[bt.nSprite].z = bt.z + KexBeam_Rand(256);
        }

        // Touching it: the player's distance from the segment, within its
        // height. The scepter walks straight through.
        for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
        {
            if (b.cooldown[nPlayer] > 0) { b.cooldown[nPlayer]--; continue; }
            int const nSprite = PlayerList[nPlayer].nSprite;
            if (nSprite < 0 || PlayerList[nPlayer].nHealth <= 0)
                continue;
            if (KexInv_HasArtifact(nPlayer, kKexArtiScepter))
                continue;
            int const sx = sprite[nSprite].x, sy = sprite[nSprite].y;
            // Feet are at sprite z; the beam has to cross the body.
            if (b.z < sprite[nSprite].z - 12288 || b.z > sprite[nSprite].z + 1024)
                continue;
            int64_t const t64 = (int64_t)(sx - b.x0) * dx + (int64_t)(sy - b.y0) * dy;
            int64_t const l2 = (int64_t)dx * dx + (int64_t)dy * dy;
            if (t64 < 0 || t64 > l2)
                continue;
            int const cx = b.x0 + (int)(t64 * dx / l2), cy = b.y0 + (int)(t64 * dy / l2);
            if (klabs(sx - cx) > KEX_BEAM_REACH || klabs(sy - cy) > KEX_BEAM_REACH)
                continue;
            b.cooldown[nPlayer] = KEX_BEAM_COOLDOWN;
            runlist_DamageEnemy(nSprite, -1, KEX_BEAM_DAMAGE);
        }
    }
}

// ---------------------------------------------------------------------------
// Channel allotments. A Build channel is hitag % 1000 and its state starts as
// that number; Kex tags run past 1000 and the converter allots those a free
// number under 1000 per level, writing the allotments out as kexchan.txt so
// the scripts below can fire a tag by the number the level script uses.
// ---------------------------------------------------------------------------
// map[16] with %15s was one short for the 16-character names (KEX_KILENTRY.MAP,
// KEX_SETARENA.MAP ...): the rest of the name fell into the tag field, the
// row was rejected, and those levels never resolved a tag past 1000.
struct KexChanRow { char map[32]; int32_t tag; int16_t chan; };   // tag 100000 is the converter's shared tag 0
static KexChanRow s_chanAll[256];
static int s_chanAllCount = -1;

static bool KexChan_ParseLine(char *line, int nRec)
{
    KexChanRow &r = s_chanAll[nRec];
    int tag, chan;
    if (Bsscanf(line, "%31s %d %d", r.map, &tag, &chan) < 3)
        return false;
    r.tag = tag; r.chan = (int16_t)chan;
    return true;
}

static int KexChan(int tag)
{
    // -1 is the no-tag sentinel. Values below -1 are genuine signed tags in
    // retail data (MAGMA links a trigger to a fireball with -5449), so those
    // use the converter's allotment table just like tags above 999.
    if (tag >= 0 && tag < 1000)
        return tag;
    if (s_chanAllCount < 0)
        s_chanAllCount = KexTable_Load("kexchan.txt", (int)ARRAY_SIZE(s_chanAll), KexChan_ParseLine);
    char base[32];
    Kex_LevelName(base, sizeof(base));
    for (int i = 0; i < s_chanAllCount; i++)
    {
        char const *m = s_chanAll[i].map;
        if (!Bstrncmp(m, "KEX_", 4)) m += 4;
        char mapUp[32];
        Bstrncpyz(mapUp, m, sizeof(mapUp));
        if (char *dot = Bstrrchr(mapUp, '.')) *dot = 0;
        if (!Bstrcmp(mapUp, base) && s_chanAll[i].tag == tag)
            return s_chanAll[i].chan;
    }
    // A non-direct tag the converter never allotted here has nothing on this
    // level listening on it. Nothing to fire.
    return -1;
}

// Fire a channel the way a touchplate does: a nonzero state, which the doors
// read as "open", and the change flag that gets it signalled next tick.
static void Kex_FireChannel(int nChannel)
{
    if (nChannel >= 0)
        runlist_ChangeChannel(nChannel, 1);
}

// ---------------------------------------------------------------------------
// ACTOR_TRIGGER. MAGMA's three: a pen holding one Mantis, and a door on the
// same tag that opens when it is dead. The creatures standing in the sector
// at load are the ones watched; runlist_DamageEnemy reports each death.
// ---------------------------------------------------------------------------
#define kMaxKexTriggers 8
#define kMaxKexTriggerSprites 16
struct KexTrigger
{
    int16_t sector, chan;
    int16_t nSprite[kMaxKexTriggerSprites];
    int16_t count, left;
    uint8_t fired;
};
static KexTrigger s_trigger[kMaxKexTriggers];
static int s_triggers;

void KexTrigger_Reset(void) { s_triggers = 0; }

// ---------------------------------------------------------------------------
// Force fields. The remaster does not open all force fields when the Scepter
// is collected. kexPuppet::OnCollide opens the contacted face and the paired
// force-field faces in the linked sector. kexforce.txt identifies the normal
// Build masked walls; height-selected portal rows retain their source flag.
// ---------------------------------------------------------------------------
struct KexForceRow { char map[32]; int32_t tile; };
static KexForceRow s_forceAll[64];
static int s_forceAllCount = -1;

static bool KexForce_ParseLine(char *line, int nRec)
{
    KexForceRow &r = s_forceAll[nRec];
    int tile;
    if (Bsscanf(line, "%31s %d", r.map, &tile) < 2)
        return false;
    r.tile = tile;
    return true;
}

static int KexForce_IsTile(int tile)
{
    if (s_forceAllCount < 0)
        s_forceAllCount = KexTable_Load("kexforce.txt", (int)ARRAY_SIZE(s_forceAll), KexForce_ParseLine);
    char base[32];
    Kex_LevelName(base, sizeof(base));
    for (int i = 0; i < s_forceAllCount; i++)
    {
        char const *m = s_forceAll[i].map;
        if (!Bstrncmp(m, "KEX_", 4)) m += 4;
        char mapUp[32];
        Bstrncpyz(mapUp, m, sizeof(mapUp));
        if (char *dot = Bstrrchr(mapUp, '.')) *dot = 0;
        if (!Bstrcmp(mapUp, base) && s_forceAll[i].tile == tile)
            return 1;
    }
    return 0;
}

void KexForce_Update(void)
{
    if (!g_kexMode)
        return;
    int n = 0;
    for (int w = 0; w < numwalls; w++)
    {
        // Invisible force fields intentionally retain only overpicnum as
        // identity; cstat 16 would draw a face the retail map hides. Their
        // collision callback, rather than inventory acquisition, changes
        // their geometry.
        if (wall[w].overpicnum <= 0 || !KexForce_IsTile(wall[w].overpicnum))
            continue;
        n++;
    }
    if (n)
        OSD_Printf("Kex force fields: %d contact-controlled walls\n", n);
}

// Kex wall topology and directed collision.
//
// Kex can put several vertically stacked portal faces on one 2D edge, each
// opening into a different sector. Build has one nextsector per wall. The
// converter writes only the competing/lost cases to kexportal.txt, including
// each face's sloped top/bottom at both wall ends. For one clipmove or render
// pass we select the face containing the actor/camera Z, install its links and
// visual/collision flags, then restore the exact ordinary Build wall state.
// This avoids both the old sealed passages and the "same coordinates, wrong
// dimension" effect without changing non-KEX maps.
//
// The separate negative wall.extra markers cover ordinary one-neighbour faces
// labelled "Block AI fallers" and "Block non-players". InitSectFlag() records
// them before LoadObjects() clears extras.
enum
{
    kKexWallExtraBlockAIFallers = -32001,
    kKexWallExtraBlockNonplayers = -32002,
    kKexWallExtraBlockBoth = -32003,
    kKexWallClipAIFallers = 1,
    kKexWallClipNonplayers = 2,
    kKexFaceSolid = 0x0001,
    kKexFaceBlockAIFallers = 0x0002,
    kKexFaceInvisible = 0x0004,
    kKexFaceBlockNonplayers = 0x0008,
    kKexFaceToggle = 0x0100,
    kKexFaceForceField = 0x0200,
};

struct KexWallClip
{
    int16_t wall, sector;
    uint8_t flags;
};

static KexWallClip s_kexWallClip[kMaxWalls];
static int s_kexWallClips;

struct KexPortalRow
{
    int16_t wall, nextwall, nextsector;
    int32_t top[2], bottom[2];
    uint32_t flags;
    int16_t picnum;
    int16_t toggleFace;
    uint8_t topBind[2], bottomBind[2];
};

#define kMaxKexPortalRows (kMaxWalls * 2)
static KexPortalRow s_kexPortal[kMaxKexPortalRows];
static int s_kexPortals;
static char s_kexPortalMap[32];
// A Kex force field's FF_FORCEFIELD bit is cleared permanently after a
// Scepter holder contacts it. Dynamic portal rows restore their ordinary
// Build wall every clip/view pass, so this state belongs to the row, not to
// the temporary wall cstat.
static uint8_t s_kexForceOpen[kMaxKexPortalRows];
// kexPuppet::OnDamage turns PF_ELECTROCUTE into LockTime=60 and ShakeTime=30.
// The native loop is 30 Hz, so thirty distinct totalclock updates reproduce
// the one-second movement lock. Keep the clock so an auxiliary movesprite()
// in the same game tick cannot shorten the stun.
static uint8_t s_kexForceStun[kMaxPlayers];
static int32_t s_kexForceStunClock[kMaxPlayers];
// PF_ELECTROCUTE is consumed by OnDamage synchronously; it is distinct from
// the remaining LockTime used to stop the player's movement afterwards.
static uint8_t s_kexForceDamage[kMaxPlayers];
// kexHud::ElectrocuteFlash starts at alpha 192 and subtracts four per 60 Hz
// update. This port runs the gameplay loop at 30 Hz, hence -8 per tick.
static int s_kexForceFlash;
// kexHud::DamageFlash and PickupFlash both begin at 192 and subtract eight
// per source update, so they lose sixteen per native game tick.
static int s_kexDamageFlash, s_kexPickupFlash;

struct KexWallSaved
{
    int16_t nextwall, nextsector, overpicnum;
    uint16_t cstat;
};

static KexWallSaved s_kexWallSaved[kMaxWalls];
static uint8_t s_kexWallWasSaved[kMaxWalls];
static int16_t s_kexWallChanged[kMaxWalls];
static int s_kexWallChanges;
static int16_t s_kexWallOwner[kMaxWalls];
static uint8_t s_kexDynamicWall[kMaxWalls];
static int16_t s_kexSelectedPortal[kMaxWalls];
static int16_t s_kexPortalFirst[kMaxWalls];
static int16_t s_kexPortalEnd[kMaxWalls];
static int s_kexWallClipActive;
static int s_kexPortalTestPending;

// Authored Toggle faces.  These are the remaster's prebuilt destructible
// passages: a blast clears the face and exposes its existing sector link.  No
// sprite, procedural room or duplicate geometry is created at impact time.
enum
{
    kMaxKexToggleFaces = 256,
    kKexToggleWords = kMaxKexToggleFaces / 32,
    kMaxKexToggleWalls = kMaxWalls * 2,
    kMaxKexToggleSides = kMaxSectors * 2,
    kMaxKexToggleSeams = 128,
};

struct KexToggleFace
{
    int32_t x, y, z;
    uint32_t closure[kKexToggleWords];
    uint8_t kind, present;
};

struct KexToggleWall
{
    int16_t face, wall, nextwall, nextsector;
    int32_t top[2], bottom[2];
    uint8_t dynamic, clearReverse;
};

struct KexToggleSide
{
    int16_t face, seam, sector;
    uint8_t cf;
};

static KexToggleFace s_kexToggleFace[kMaxKexToggleFaces];
static KexToggleWall s_kexToggleWall[kMaxKexToggleWalls];
static KexToggleSide s_kexToggleSide[kMaxKexToggleSides];
static uint32_t s_kexToggleOpen[kKexToggleWords];
static uint8_t s_kexToggleSeamActive[kMaxKexToggleSeams];
static int s_kexToggleFaces, s_kexToggleWalls, s_kexToggleSides, s_kexToggleSeams;
static int s_kexTogglePortalBindings;
static int s_kexToggleTestPending;

static bool KexPortal_ParseLine(char *line, int nRec)
{
    char map[32];
    int nWall, nNextWall, nNextSector, top0, bottom0, top1, bottom1, flags, picnum;
    if (Bsscanf(line, "%31s %d %d %d %d %d %d %d %d %d", map,
                &nWall, &nNextWall, &nNextSector, &top0, &bottom0,
                &top1, &bottom1, &flags, &picnum) != 10
        || Bstrcmp(map, s_kexPortalMap)
        || (unsigned)nWall >= (unsigned)numwalls
        || (unsigned)nNextWall >= (unsigned)numwalls
        || (unsigned)nNextSector >= (unsigned)numsectors)
        return false;

    KexPortalRow &r = s_kexPortal[nRec];
    r.wall = nWall; r.nextwall = nNextWall; r.nextsector = nNextSector;
    r.top[0] = top0; r.bottom[0] = bottom0;
    r.top[1] = top1; r.bottom[1] = bottom1;
    r.flags = flags; r.picnum = picnum;
    r.toggleFace = -1;
    r.topBind[0] = r.topBind[1] = 0;
    r.bottomBind[0] = r.bottomBind[1] = 0;
    return true;
}

static bool KexToggle_ParseLine(char *line, int nRec)
{
    (void)nRec;
    char map[32], op;
    if (Bsscanf(line, "%31s %c", map, &op) != 2 || Bstrcmp(map, s_kexPortalMap))
        return false;

    if (op == 'F')
    {
        int face, kind, x, y, z;
        unsigned word[kKexToggleWords];
        if (Bsscanf(line, "%31s %c %d %d %d %d %d %x %x %x %x %x %x %x %x",
                    map, &op, &face, &kind, &x, &y, &z,
                    &word[0], &word[1], &word[2], &word[3],
                    &word[4], &word[5], &word[6], &word[7]) != 15
            || (unsigned)face >= (unsigned)kMaxKexToggleFaces
            || (unsigned)kind > 2u)
            return false;
        KexToggleFace &f = s_kexToggleFace[face];
        f.x = x; f.y = y; f.z = z; f.kind = (uint8_t)kind; f.present = 1;
        for (int i = 0; i < kKexToggleWords; i++)
            f.closure[i] = word[i];
        s_kexToggleFaces = max(s_kexToggleFaces, face + 1);
        return true;
    }

    if (op == 'W')
    {
        int face, nWall, nNextWall, nNextSector;
        int top0, bottom0, top1, bottom1, dynamic, clearReverse;
        if (s_kexToggleWalls >= kMaxKexToggleWalls
            || Bsscanf(line, "%31s %c %d %d %d %d %d %d %d %d %d %d",
                       map, &op, &face, &nWall, &nNextWall, &nNextSector,
                       &top0, &bottom0, &top1, &bottom1,
                       &dynamic, &clearReverse) != 12
            || (unsigned)face >= (unsigned)kMaxKexToggleFaces
            || (unsigned)nWall >= (unsigned)numwalls
            || (unsigned)nNextWall >= (unsigned)numwalls
            || (unsigned)nNextSector >= (unsigned)numsectors)
            return false;
        KexToggleWall &w = s_kexToggleWall[s_kexToggleWalls++];
        w.face = face; w.wall = nWall; w.nextwall = nNextWall; w.nextsector = nNextSector;
        w.top[0] = top0; w.bottom[0] = bottom0;
        w.top[1] = top1; w.bottom[1] = bottom1;
        w.dynamic = !!dynamic; w.clearReverse = !!clearReverse;
        return true;
    }

    if (op == 'S')
    {
        int face, seam, nSector, cf;
        if (s_kexToggleSides >= kMaxKexToggleSides
            || Bsscanf(line, "%31s %c %d %d %d %d",
                       map, &op, &face, &seam, &nSector, &cf) != 6
            || (unsigned)face >= (unsigned)kMaxKexToggleFaces
            || (unsigned)seam >= (unsigned)kMaxKexToggleSeams
            || (unsigned)nSector >= (unsigned)numsectors
            || (unsigned)cf > 1u)
            return false;
        KexToggleSide &s = s_kexToggleSide[s_kexToggleSides++];
        s.face = face; s.seam = seam; s.sector = nSector; s.cf = (uint8_t)cf;
        s_kexToggleSeams = max(s_kexToggleSeams, seam + 1);
        return true;
    }
    return false;
}

static void KexWall_Save(int nWall)
{
    if ((unsigned)nWall >= (unsigned)numwalls || s_kexWallWasSaved[nWall])
        return;
    KexWallSaved &s = s_kexWallSaved[nWall];
    s.nextwall = wall[nWall].nextwall;
    s.nextsector = wall[nWall].nextsector;
    s.overpicnum = wall[nWall].overpicnum;
    s.cstat = wall[nWall].cstat;
    s_kexWallWasSaved[nWall] = 1;
    s_kexWallChanged[s_kexWallChanges++] = nWall;
}

void KexWall_ClipEnd(void)
{
    if (!s_kexWallClipActive)
        return;
    for (int i = 0; i < s_kexWallChanges; i++)
    {
        int const nWall = s_kexWallChanged[i];
        if ((unsigned)nWall >= (unsigned)numwalls)
            continue;
        KexWallSaved const &s = s_kexWallSaved[nWall];
        wall[nWall].nextwall = s.nextwall;
        wall[nWall].nextsector = s.nextsector;
        wall[nWall].overpicnum = s.overpicnum;
        wall[nWall].cstat = s.cstat;
        s_kexWallWasSaved[nWall] = 0;
    }
    s_kexWallChanges = 0;
    s_kexWallClipActive = 0;
}

static void KexWall_Point(int nWall, int x, int y, int *pX, int *pY, int64_t *pFrac, int64_t *pDen)
{
    walltype const &w = wall[nWall];
    walltype const &w2 = wall[w.point2];
    int64_t const dx = (int64_t)w2.x - w.x;
    int64_t const dy = (int64_t)w2.y - w.y;
    int64_t const den = dx * dx + dy * dy;
    int64_t frac = den ? ((int64_t)x - w.x) * dx + ((int64_t)y - w.y) * dy : 0;
    frac = clamp<int64_t>(frac, 0, den);
    *pX = den ? w.x + (int)(dx * frac / den) : w.x;
    *pY = den ? w.y + (int)(dy * frac / den) : w.y;
    *pFrac = frac;
    *pDen = den;
}

static int32_t KexPortal_Interpolate(int32_t a, int32_t b, int64_t frac, int64_t den)
{
    return den ? a + (int32_t)(((int64_t)b - a) * frac / den) : a;
}

enum
{
    kKexPortalBindSource = 1,
    kKexPortalBindTarget = 2,
};

static int KexPortal_Near(int32_t a, int32_t b)
{
    int64_t d = (int64_t)a - b;
    if (d < 0)
        d = -d;
    return d <= 8;
}

// A linked face normally ends on one or both sectors' ceiling/floor. Record
// that attachment while the level is still at its authored height, before
// lifts, touchplates and floating platforms begin moving. A face endpoint
// then follows that live surface instead of leaving a stale opening behind.
static void KexPortal_BindSurfaces(void)
{
    for (int i = 0; i < s_kexPortals; i++)
    {
        KexPortalRow &r = s_kexPortal[i];
        int const owner = s_kexWallOwner[r.wall];
        if ((unsigned)owner >= (unsigned)numsectors)
            continue;

        for (int endpoint = 0; endpoint < 2; endpoint++)
        {
            int const nPoint = endpoint ? wall[r.wall].point2 : r.wall;
            if ((unsigned)nPoint >= (unsigned)numwalls)
                continue;
            int const x = wall[nPoint].x;
            int const y = wall[nPoint].y;
            int const sourceCeil = getceilzofslope(owner, x, y);
            int const targetCeil = getceilzofslope(r.nextsector, x, y);
            int const sourceFloor = getflorzofslope(owner, x, y);
            int const targetFloor = getflorzofslope(r.nextsector, x, y);

            if (KexPortal_Near(r.top[endpoint], sourceCeil))
                r.topBind[endpoint] |= kKexPortalBindSource;
            if (KexPortal_Near(r.top[endpoint], targetCeil))
                r.topBind[endpoint] |= kKexPortalBindTarget;
            if (KexPortal_Near(r.bottom[endpoint], sourceFloor))
                r.bottomBind[endpoint] |= kKexPortalBindSource;
            if (KexPortal_Near(r.bottom[endpoint], targetFloor))
                r.bottomBind[endpoint] |= kKexPortalBindTarget;
        }
    }
}

static int32_t KexPortal_BoundAt(KexPortalRow const &r, int endpoint, int top)
{
    int const nPoint = endpoint ? wall[r.wall].point2 : r.wall;
    if ((unsigned)nPoint >= (unsigned)numwalls)
        return top ? r.top[endpoint] : r.bottom[endpoint];

    uint8_t const bind = top ? r.topBind[endpoint] : r.bottomBind[endpoint];
    int32_t value = top ? r.top[endpoint] : r.bottom[endpoint];
    int const owner = s_kexWallOwner[r.wall];
    int const x = wall[nPoint].x;
    int const y = wall[nPoint].y;
    int have = 0;

    if ((bind & kKexPortalBindSource) && (unsigned)owner < (unsigned)numsectors)
    {
        value = top ? getceilzofslope(owner, x, y) : getflorzofslope(owner, x, y);
        have = 1;
    }
    if (bind & kKexPortalBindTarget)
    {
        int32_t const target = top ? getceilzofslope(r.nextsector, x, y)
                                   : getflorzofslope(r.nextsector, x, y);
        if (!have || (top ? target > value : target < value))
            value = target;
    }
    return value;
}

static int KexPortal_Select(int first, int end, int x, int y, int z, int view)
{
    int px, py;
    int64_t frac, den;
    KexWall_Point(s_kexPortal[first].wall, x, y, &px, &py, &frac, &den);
    int best = -1;
    int bestOutside = INT_MAX;
    int64_t bestPrimary = INT64_MAX;
    int32_t bestSpan = INT32_MAX;
    for (int i = first; i < end; i++)
    {
        KexPortalRow const &r = s_kexPortal[i];
        int32_t top = KexPortal_Interpolate(KexPortal_BoundAt(r, 0, 1),
                                            KexPortal_BoundAt(r, 1, 1), frac, den);
        int32_t bottom = KexPortal_Interpolate(KexPortal_BoundAt(r, 0, 0),
                                               KexPortal_BoundAt(r, 1, 0), frac, den);
        if (top > bottom)
            continue;
        int const outside = z < top ? top - z : z > bottom ? z - bottom : 0;
        if (outside > 8)
            continue;
        // The face span is authoritative, while this rejects a malformed row
        // whose converted destination no longer occupies that vertical band.
        int32_t const cz = getceilzofslope(r.nextsector, px, py);
        int32_t const fz = getflorzofslope(r.nextsector, px, py);
        if (z < cz - 8 || z > fz + 8)
            continue;
        int64_t primary = view ? (int64_t)z * 2 - top - bottom
                               : (int64_t)z - bottom;
        if (primary < 0) primary = -primary;
        int32_t const span = bottom - top;
        if (outside < bestOutside
            || (outside == bestOutside && (primary < bestPrimary
                || (primary == bestPrimary && span < bestSpan))))
        {
            bestOutside = outside;
            bestPrimary = primary;
            bestSpan = span;
            best = i;
        }
    }
    return best;
}

static int KexForce_WallOwner(int nWall)
{
    if ((unsigned)nWall >= (unsigned)numwalls)
        return -1;
    int const saved = s_kexWallOwner[nWall];
    if ((unsigned)saved < (unsigned)numsectors)
        return saved;
    for (int nSector = 0; nSector < numsectors; nSector++)
        if (nWall >= sector[nSector].wallptr
            && nWall < sector[nSector].wallptr + sector[nSector].wallnum)
            return nSector;
    return -1;
}

static int KexForce_IsStaticWall(int nWall)
{
    return (unsigned)nWall < (unsigned)numwalls
        && wall[nWall].overpicnum > 0 && KexForce_IsTile(wall[nWall].overpicnum);
}

static int KexForce_ClearStaticWall(int nWall)
{
    if (!KexForce_IsStaticWall(nWall))
        return 0;
    // FF_SOLID|FF_FORCEFIELD becomes Invisible|Hidden in the remaster. The
    // retained overpicnum is private identity only; without MASKED it is not
    // rendered or clipped and MyLoadSave retains the opened wall state.
    wall[nWall].cstat &= ~(CSTAT_WALL_BLOCK | CSTAT_WALL_BLOCK_HITSCAN
                            | CSTAT_WALL_MASKED);
    return 1;
}

static int KexForce_OpenPortal(int nPortal)
{
    if ((unsigned)nPortal >= (unsigned)s_kexPortals
        || !(s_kexPortal[nPortal].flags & kKexFaceForceField))
        return 0;
    int const wasOpen = s_kexForceOpen[nPortal];
    s_kexForceOpen[nPortal] = 1;
    return !wasOpen;
}

int KexForce_StunMove(int nSprite)
{
    int const nPlayer = GetPlayerFromSprite(nSprite);
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers
        || !s_kexForceStun[nPlayer])
        return 0;

    if (s_kexForceStunClock[nPlayer] != (int32_t)totalclock)
    {
        s_kexForceStunClock[nPlayer] = (int32_t)totalclock;
        s_kexForceStun[nPlayer]--;
    }
    if (!s_kexForceStun[nPlayer])
        return 0;

    // Source still lets a stunned player fall (its Tick only returns early
    // while grounded), but clears all movement immediately on the shock.
    int const floor = getflorzofslope(sprite[nSprite].sectnum,
                                      sprite[nSprite].x, sprite[nSprite].y);
    if (sprite[nSprite].z < floor)
        return 0;
    sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
    return 1;
}

void KexForce_Tick(void)
{
    if (g_kexMode && s_kexForceFlash > 0)
        s_kexForceFlash = max(0, s_kexForceFlash - 8);
}

int KexForce_ElectrocuteAlpha(void)
{
    return g_kexMode ? s_kexForceFlash : 0;
}

int KexForce_IsElectrocuted(int nSprite)
{
    int const nPlayer = GetPlayerFromSprite(nSprite);
    return g_kexMode && (unsigned)nPlayer < (unsigned)kMaxPlayers
        && s_kexForceDamage[nPlayer] != 0;
}

void KexHud_DamageFlash(void)
{
    if (g_kexMode)
    {
        s_kexDamageFlash = 192;
#ifdef USE_OPENGL
        if (videoGetRenderMode() < REND_POLYMOST)
#endif
            TintPalette(255, 32, 32);
    }
}

void KexHud_PickupFlash(void)
{
    if (g_kexMode)
    {
        s_kexPickupFlash = 192;
#ifdef USE_OPENGL
        if (videoGetRenderMode() < REND_POLYMOST)
#endif
            TintPalette(32, 32, 255);
    }
}

int KexHud_DamageFlashAlpha(void) { return g_kexMode ? s_kexDamageFlash : 0; }
int KexHud_PickupFlashAlpha(void) { return g_kexMode ? s_kexPickupFlash : 0; }

void KexHudFlash_Tick(void)
{
    if (g_kexMode)
    {
        s_kexDamageFlash = max(0, s_kexDamageFlash - 16);
        s_kexPickupFlash = max(0, s_kexPickupFlash - 16);
    }
}

int KexForce_OnWallHit(int nSprite, int nWall)
{
    if (!g_kexMode || (unsigned)nSprite >= (unsigned)kMaxSprites
        || (unsigned)nWall >= (unsigned)numwalls)
        return 0;
    int const nPlayer = GetPlayerFromSprite(nSprite);
    if ((unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return 0;

    int nPortal = -1;
    int const first = s_kexPortalFirst[nWall];
    int const end = s_kexPortalEnd[nWall];
    if (first >= 0 && end > first)
        nPortal = KexPortal_Select(first, end, sprite[nSprite].x, sprite[nSprite].y,
                                   sprite[nSprite].z, 0);

    int const dynamic = nPortal >= 0
        && (s_kexPortal[nPortal].flags & kKexFaceForceField);
    int const ordinary = KexForce_IsStaticWall(nWall);
    if (!dynamic && !ordinary)
        return 0;

    if (!KexInv_HasArtifact(nPlayer, kKexArtiScepter))
    {
        // OnDamage consumes PF_ELECTROCUTE immediately: 50 source health,
        // zero velocity, and LockTime=60. It therefore cannot keep applying
        // damage every source update while the player remains against a
        // field. At 30 Hz the lock is thirty native game ticks.
        s_kexForceStun[nPlayer] = 30;
        s_kexForceStunClock[nPlayer] = (int32_t)totalclock;
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        if (nPlayer == nLocalPlayer)
        {
            s_kexForceFlash = 192;
#ifdef USE_OPENGL
            if (videoGetRenderMode() < REND_POLYMOST)
#endif
                TintPalette(32, 255, 32);
        }
        s_kexForceDamage[nPlayer] = 1;
        runlist_DamageEnemy(nSprite, -1, 50);
        s_kexForceDamage[nPlayer] = 0;
        return 1;
    }

    int changed = dynamic ? KexForce_OpenPortal(nPortal)
                          : KexForce_ClearStaticWall(nWall);
    int const owner = KexForce_WallOwner(nWall);
    int const dest = dynamic ? s_kexPortal[nPortal].nextsector : wall[nWall].nextsector;

    // The original opens every FF_FORCEFIELD face in the contacted face's
    // destination sector that points back to its source sector, then opens
    // the contacted face. This preserves paired/multi-band barriers without
    // making unrelated fields globally passable.
    if ((unsigned)owner < (unsigned)numsectors && (unsigned)dest < (unsigned)numsectors)
    {
        int const limit = sector[dest].wallptr + sector[dest].wallnum;
        for (int w = sector[dest].wallptr; w < limit; w++)
        {
            int const pfirst = s_kexPortalFirst[w];
            int const pend = s_kexPortalEnd[w];
            if (pfirst >= 0 && pend > pfirst)
            {
                for (int p = pfirst; p < pend; p++)
                    if (s_kexPortal[p].nextsector == owner)
                        changed |= KexForce_OpenPortal(p);
            }
            else if (wall[w].nextsector == owner)
                changed |= KexForce_ClearStaticWall(w);
        }
    }

    if (changed)
    {
        int const sound = KexSound_Get("k_forceoff");
        if (sound >= 0)
            D3PlayFX(sound, (short)nSprite);
    }
    return 1;
}

static int KexToggle_IsOpen(int nFace)
{
    return (unsigned)nFace < (unsigned)s_kexToggleFaces
        && (s_kexToggleOpen[nFace >> 5] & (1u << (nFace & 31)));
}

static void KexWall_ApplyPortals(int x, int y, int z, int wallMask,
                                 int nonplayer, int aiFaller, int view)
{
    int selected = 0;
    for (int first = 0; first < s_kexPortals; )
    {
        int end = first + 1;
        while (end < s_kexPortals && s_kexPortal[end].wall == s_kexPortal[first].wall)
            end++;
        int const nWall = s_kexPortal[first].wall;
        KexWall_Save(nWall);
        int const which = KexPortal_Select(first, end, x, y, z, view);
        if (which < 0)
        {
            wall[nWall].nextwall = wall[nWall].nextsector = -1;
            wall[nWall].cstat &= ~CSTAT_WALL_MASKED;
            wall[nWall].overpicnum = 0;
            if (!view)
                wall[nWall].cstat |= wallMask;
        }
        else
        {
            KexPortalRow const &r = s_kexPortal[which];
            int const faceOpen = KexToggle_IsOpen(r.toggleFace) || s_kexForceOpen[which];
            wall[nWall].nextwall = r.nextwall;
            wall[nWall].nextsector = r.nextsector;
            if (view)
            {
                wall[nWall].cstat &= ~CSTAT_WALL_MASKED;
                wall[nWall].overpicnum = faceOpen ? 0 : r.picnum;
                if (!faceOpen && r.picnum > 0 && !(r.flags & kKexFaceInvisible))
                    wall[nWall].cstat |= CSTAT_WALL_MASKED;
            }
            else
            {
                int block = (r.flags & kKexFaceSolid) && !faceOpen;
                if (!block && nonplayer && (r.flags & kKexFaceBlockNonplayers))
                    block = 1;
                if (!block && aiFaller && (r.flags & kKexFaceBlockAIFallers))
                {
                    int px, py;
                    int64_t frac, den;
                    KexWall_Point(nWall, x, y, &px, &py, &frac, &den);
                    int const owner = s_kexWallOwner[nWall];
                    block = (unsigned)owner < (unsigned)numsectors
                         && getflorzofslope(owner, px, py) != getflorzofslope(r.nextsector, px, py);
                }
                if (block) wall[nWall].cstat |= wallMask;
                else       wall[nWall].cstat &= ~wallMask;
            }
            s_kexSelectedPortal[selected++] = which;
        }
        first = end;
    }

    // Rendering and clipmove need a way back to the source when the authored
    // destination is a native Build wall. A dynamic destination owns another
    // directed Kex face, however; overwriting it with a synthetic reverse link
    // would replace that face's selected destination (a common room-over-room
    // chain) and effectively create a second copy of the room.
    for (int i = 0; i < selected; i++)
    {
        KexPortalRow const &r = s_kexPortal[s_kexSelectedPortal[i]];
        int const owner = s_kexWallOwner[r.wall];
        if ((unsigned)owner >= (unsigned)numsectors || s_kexDynamicWall[r.nextwall])
            continue;
        KexWall_Save(r.nextwall);
        wall[r.nextwall].nextwall = r.wall;
        wall[r.nextwall].nextsector = owner;
    }
}

static int KexWall_Matches(int nWall, KexWallSaved const &s)
{
    return wall[nWall].nextwall == s.nextwall
        && wall[nWall].nextsector == s.nextsector
        && wall[nWall].overpicnum == s.overpicnum
        && wall[nWall].cstat == s.cstat;
}

// VB_KEX_PORTAL_TEST=1: select every face at its centre, install its temporary
// topology/render state, verify a static target's reciprocal link, then prove
// every touched wall was restored byte-for-byte. Rows in a currently closed
// lift/bomb sector are counted as dormant rather than failed; its live sector
// bounds activate them when that geometry opens.
static void KexPortal_SelfTest(void)
{
    if (!Bgetenv("VB_KEX_PORTAL_TEST"))
        return;

    int tested = 0, dormant = 0, bad = 0, boundEndpoints = 0;
    for (int first = 0; first < s_kexPortals; )
    {
        int end = first + 1;
        while (end < s_kexPortals && s_kexPortal[end].wall == s_kexPortal[first].wall)
            end++;
        int const nWall = s_kexPortal[first].wall;
        int const nPoint = wall[nWall].point2;
        int const x = (int)(((int64_t)wall[nWall].x + wall[nPoint].x) / 2);
        int const y = (int)(((int64_t)wall[nWall].y + wall[nPoint].y) / 2);

        for (int i = first; i < end; i++)
        {
            KexPortalRow const &r = s_kexPortal[i];
            for (int endpoint = 0; endpoint < 2; endpoint++)
            {
                boundEndpoints += r.topBind[endpoint] != 0;
                boundEndpoints += r.bottomBind[endpoint] != 0;
            }
            int32_t top = (int32_t)(((int64_t)KexPortal_BoundAt(r, 0, 1)
                                   + KexPortal_BoundAt(r, 1, 1)) / 2);
            int32_t bottom = (int32_t)(((int64_t)KexPortal_BoundAt(r, 0, 0)
                                      + KexPortal_BoundAt(r, 1, 0)) / 2);
            if (top > bottom)
            {
                dormant++;
                continue;
            }
            if (bottom - top <= 16)
            {
                dormant++;
                continue;
            }
            int const z = top + (bottom - top) / 2;
            int const selected = KexPortal_Select(first, end, x, y, z, 1);
            if (selected < 0)
            {
                dormant++;
                continue;
            }
            if (selected != i)
            {
                if (bad < 16)
                    OSD_Printf(OSD_ERROR "Kex portal test: row %d on wall %d selected %d at (%d,%d,%d)\n",
                               i, nWall, selected, x, y, z);
                bad++;
                continue;
            }

            KexWallSaved const source = { wall[nWall].nextwall, wall[nWall].nextsector,
                                          wall[nWall].overpicnum, wall[nWall].cstat };
            KexWallSaved const target = { wall[r.nextwall].nextwall, wall[r.nextwall].nextsector,
                                          wall[r.nextwall].overpicnum, wall[r.nextwall].cstat };
            s_kexWallClipActive = 1;
            KexWall_ApplyPortals(x, y, z, 0, 0, 0, 1);
            int const owner = s_kexWallOwner[nWall];
            int const masked = r.picnum > 0 && !(r.flags & kKexFaceInvisible);
            int const targetIsDynamic = s_kexDynamicWall[r.nextwall];
            int const topologyBad = wall[nWall].nextwall != r.nextwall
                || wall[nWall].nextsector != r.nextsector
                || wall[nWall].overpicnum != r.picnum
                || !!(wall[nWall].cstat & CSTAT_WALL_MASKED) != masked
                || (!targetIsDynamic && (wall[r.nextwall].nextwall != nWall
                                         || wall[r.nextwall].nextsector != owner));
            if (topologyBad && bad < 16)
                OSD_Printf(OSD_ERROR "Kex portal test: row %d wall %d -> %d/%d applied %d/%d, reciprocal %d/%d expected %d/%d\n",
                           i, nWall, r.nextwall, r.nextsector,
                           (int)wall[nWall].nextwall, (int)wall[nWall].nextsector,
                           (int)wall[r.nextwall].nextwall, (int)wall[r.nextwall].nextsector,
                           nWall, owner);
            bad += topologyBad;
            KexWall_ClipEnd();
            int const restoreBad = !KexWall_Matches(nWall, source)
                || !KexWall_Matches(r.nextwall, target);
            if (restoreBad && bad < 16)
                OSD_Printf(OSD_ERROR "Kex portal test: row %d wall %d or reciprocal %d did not restore\n",
                           i, nWall, r.nextwall);
            bad += restoreBad;
            tested++;
        }
        first = end;
    }

    OSD_Printf("Kex portal test: %s (%d active, %d dormant faces, %d bound endpoints)\n",
               bad ? "FAIL" : "PASS", tested, dormant, boundEndpoints);
}

static void KexToggle_ClearVisual(int nWall)
{
    if ((unsigned)nWall >= (unsigned)numwalls)
        return;
    wall[nWall].cstat &= ~CSTAT_WALL_MASKED;
    wall[nWall].overpicnum = 0;
}

static int KexToggle_SameDirectedEdge(int a, int b)
{
    if ((unsigned)a >= (unsigned)numwalls || (unsigned)b >= (unsigned)numwalls)
        return 0;
    int const ap = wall[a].point2;
    int const bp = wall[b].point2;
    return (unsigned)ap < (unsigned)numwalls && (unsigned)bp < (unsigned)numwalls
        && wall[a].x == wall[b].x && wall[a].y == wall[b].y
        && wall[ap].x == wall[bp].x && wall[ap].y == wall[bp].y;
}

// Add all converted sectors on one opened horizontal seam to a new YAX bunch.
// The source surfaces remain ordinary textured, collidable floors/ceilings
// until this one-shot operation, so a closed bomb floor never renders as a
// hole.  Several seams may open in one retail closure; their list rebuild is
// deferred and performed once by KexToggle_OpenBits().
static int KexToggle_ActivateSeam(int nSeam)
{
    if ((unsigned)nSeam >= (unsigned)s_kexToggleSeams
        || s_kexToggleSeamActive[nSeam]
        || numyaxbunches >= YAX_MAXBUNCHES)
        return 0;

    int const bunch = numyaxbunches++;
    int sides = 0;
    for (int i = 0; i < s_kexToggleSides; i++)
    {
        KexToggleSide const &s = s_kexToggleSide[i];
        if (s.seam != nSeam)
            continue;
        yax_setbunch(s.sector, s.cf, bunch);
        sides++;
    }
    if (sides < 2)
    {
        numyaxbunches--;
        for (int i = 0; i < s_kexToggleSides; i++)
            if (s_kexToggleSide[i].seam == nSeam)
                yax_setbunch(s_kexToggleSide[i].sector, s_kexToggleSide[i].cf, -3);
        return 0;
    }

    // Pair complete directed edges just like the converter's static TROR
    // path.  A split ring can have two walls beginning at the same vertex;
    // choosing by that vertex alone can join two geographically unrelated
    // spaces. Polygon membership supplies the vertical neighbour, while these
    // unique reciprocal links keep clipping and hitscan continuous at edges.
    for (int a = 0; a < s_kexToggleSides; a++)
    {
        KexToggleSide const &up = s_kexToggleSide[a];
        if (up.seam != nSeam || up.cf != YAX_FLOOR)
            continue;
        int const upEnd = sector[up.sector].wallptr + sector[up.sector].wallnum;
        for (int uw = sector[up.sector].wallptr; uw < upEnd; uw++)
        {
            int match = -1;
            int matches = 0;
            for (int b = 0; b < s_kexToggleSides; b++)
            {
                KexToggleSide const &down = s_kexToggleSide[b];
                if (down.seam != nSeam || down.cf != YAX_CEILING)
                    continue;
                int const downEnd = sector[down.sector].wallptr + sector[down.sector].wallnum;
                for (int dw = sector[down.sector].wallptr; dw < downEnd; dw++)
                {
                    if (dw == uw || !KexToggle_SameDirectedEdge(uw, dw))
                        continue;
                    match = dw;
                    matches++;
                }
            }
            if (matches == 1)
            {
                yax_setnextwall(uw, YAX_FLOOR, match);
                yax_setnextwall(match, YAX_CEILING, uw);
            }
        }
    }
    s_kexToggleSeamActive[nSeam] = 1;
    return 1;
}

static void KexToggle_OpenBits(uint32_t const opened[kKexToggleWords])
{
    uint32_t newly[kKexToggleWords];
    int any = 0;
    for (int i = 0; i < kKexToggleWords; i++)
    {
        newly[i] = opened[i] & ~s_kexToggleOpen[i];
        any |= newly[i] != 0;
    }
    if (!any)
        return;

    // A blast can be delivered from a runlist callback immediately after a
    // move.  Restore any temporary Z-selected topology before changing the
    // permanent authored state underneath it.
    KexWall_ClipEnd();
    for (int i = 0; i < kKexToggleWords; i++)
        s_kexToggleOpen[i] |= newly[i];

    for (int i = 0; i < s_kexToggleWalls; i++)
    {
        KexToggleWall const &t = s_kexToggleWall[i];
        if (!(newly[t.face >> 5] & (1u << (t.face & 31))))
            continue;

        if (!t.dynamic)
        {
            wall[t.wall].cstat &= ~(CSTAT_WALL_BLOCK | CSTAT_WALL_BLOCK_HITSCAN);
            KexToggle_ClearVisual(t.wall);
        }
        // The converter may copy a one-sided mask to an otherwise untextured
        // reverse portal solely so Build can see it from both directions.
        // It is not another authored face, and must disappear with its source.
        if (t.clearReverse)
            KexToggle_ClearVisual(t.nextwall);
    }

    int changedYax = 0;
    for (int seam = 0; seam < s_kexToggleSeams; seam++)
    {
        if (s_kexToggleSeamActive[seam])
            continue;
        int shouldOpen = 0;
        for (int i = 0; i < s_kexToggleSides; i++)
        {
            KexToggleSide const &s = s_kexToggleSide[i];
            if (s.seam == seam && KexToggle_IsOpen(s.face))
            {
                shouldOpen = 1;
                break;
            }
        }
        if (shouldOpen)
            changedYax |= KexToggle_ActivateSeam(seam);
    }
    if (changedYax)
        yax_update(2);
}

static void KexToggle_SaveTestMutate(void)
{
    uint32_t all[kKexToggleWords] = {};
    for (int i = 0; i < s_kexToggleFaces; i++)
        if (s_kexToggleFace[i].present)
            all[i >> 5] |= 1u << (i & 31);
    KexToggle_OpenBits(all);
    OSD_Printf("Kex Toggle save test: opened all faces after the snapshot\n");
}

static void KexToggle_Reachable(int x, int y, int z, int nSector,
                                uint8_t reachable[kMaxSectors])
{
    Bmemset(reachable, 0, kMaxSectors);
    if ((unsigned)nSector >= (unsigned)numsectors)
        return;

    int16_t pending[kMaxSectors];
    int head = 0, tail = 0;
    pending[tail++] = (int16_t)nSector;
    reachable[nSector] = 1;
    while (head < tail)
    {
        int const s = pending[head++];
        int const end = sector[s].wallptr + sector[s].wallnum;
        for (int w = sector[s].wallptr; w < end; w++)
        {
            int ns = wall[w].nextsector;
            int block = wall[w].cstat & (CSTAT_WALL_BLOCK | CSTAT_WALL_BLOCK_HITSCAN);
            int const first = s_kexPortalFirst[w];
            int const pend = s_kexPortalEnd[w];
            if (first >= 0)
            {
                int const which = KexPortal_Select(first, pend, x, y, z, 0);
                if (which < 0)
                    continue;
                KexPortalRow const &r = s_kexPortal[which];
                ns = r.nextsector;
                block = (r.flags & kKexFaceSolid) && !KexToggle_IsOpen(r.toggleFace)
                     && !s_kexForceOpen[which];
            }
            if (block || (unsigned)ns >= (unsigned)numsectors || reachable[ns])
                continue;
            reachable[ns] = 1;
            pending[tail++] = (int16_t)ns;
        }

        // Already-open vertical seams are real topology too.  At the blast's
        // XY only the sector whose polygon contains that point is reachable.
        for (int cf = 0; cf < 2; cf++)
        {
            int const ns = yax_getneighborsect(x, y, s, cf);
            if ((unsigned)ns >= (unsigned)numsectors || reachable[ns])
                continue;
            reachable[ns] = 1;
            pending[tail++] = (int16_t)ns;
        }
    }
}

static int KexToggle_FaceVisible(int nFace, int x, int y, int z, int nSector,
                                 uint8_t const reachable[kMaxSectors])
{
    KexToggleFace const &f = s_kexToggleFace[nFace];
    for (int i = 0; i < s_kexToggleWalls; i++)
    {
        KexToggleWall const &t = s_kexToggleWall[i];
        if (t.face == nFace && (unsigned)t.wall < (unsigned)numwalls)
        {
            int const owner = s_kexWallOwner[t.wall];
            if ((unsigned)owner < (unsigned)numsectors && reachable[owner]
                && cansee(x, y, z, (int16_t)nSector,
                          f.x, f.y, f.z, (int16_t)owner))
                return 1;
        }
    }
    for (int i = 0; i < s_kexToggleSides; i++)
    {
        KexToggleSide const &s = s_kexToggleSide[i];
        if (s.face == nFace && reachable[s.sector]
            && cansee(x, y, z, (int16_t)nSector,
                      f.x, f.y, f.z, s.sector))
            return 1;
    }
    return 0;
}

void KexToggle_Blast(int x, int y, int z, int nSector, int nRadius)
{
    if (!g_kexMode || nRadius <= 0 || !s_kexToggleFaces
        || (unsigned)nSector >= (unsigned)numsectors)
        return;

    uint8_t reachable[kMaxSectors];
    KexToggle_Reachable(x, y, z, nSector, reachable);
    // cansee() needs the same height-selected topology the blast occupies.
    // Install it once for all face traces and restore it before committing
    // any permanent wall state.
    if (s_kexPortals)
    {
        s_kexWallClipActive = 1;
        KexWall_ApplyPortals(x, y, z, 0, 0, 0, 0);
    }
    uint32_t opened[kKexToggleWords] = {};
    int hits = 0;
    int64_t const xyRadius = (int64_t)nRadius * 6;
    // Retail multiplies the vertical range by float 0.575 (23/40) after its
    // separate XY circle test. Build Z is 16 times KEX Z and WORLD is 6.
    int64_t const zRadius = (int64_t)nRadius * 23 * 16 * 6 / 40;
    for (int i = 0; i < s_kexToggleFaces; i++)
    {
        KexToggleFace const &f = s_kexToggleFace[i];
        if (!f.present || KexToggle_IsOpen(i)
            || !KexToggle_FaceVisible(i, x, y, z, nSector, reachable))
            continue;
        int64_t const dx = (int64_t)f.x - x;
        int64_t const dy = (int64_t)f.y - y;
        int64_t const dz = (int64_t)f.z - z;
        if (dx * dx + dy * dy > xyRadius * xyRadius
            || dz < -zRadius || dz > zRadius)
            continue;
        for (int w = 0; w < kKexToggleWords; w++)
            opened[w] |= f.closure[w];
        hits++;
    }
    KexWall_ClipEnd();
    if (hits)
        KexToggle_OpenBits(opened);
}

static int KexToggle_FaceOwner(int nFace)
{
    for (int i = 0; i < s_kexToggleWalls; i++)
        if (s_kexToggleWall[i].face == nFace)
        {
            int const owner = s_kexWallOwner[s_kexToggleWall[i].wall];
            if ((unsigned)owner < (unsigned)numsectors)
                return owner;
        }
    for (int i = 0; i < s_kexToggleSides; i++)
        if (s_kexToggleSide[i].face == nFace)
            return s_kexToggleSide[i].sector;
    return -1;
}

// VB_KEX_TOGGLE_TEST=1: open one wall closure and one vertical closure (when
// present), then verify every logical bit and every ordinary wall transition.
// This intentionally changes only the isolated test process's map.
static void KexToggle_SelfTest(void)
{
    int targets[2] = { -1, -1 };
    for (int i = 0; i < s_kexToggleFaces; i++)
    {
        if (!s_kexToggleFace[i].present || KexToggle_FaceOwner(i) < 0)
            continue;
        int const slot = s_kexToggleFace[i].kind == 0 ? 0 : 1;
        if (targets[slot] < 0)
            targets[slot] = i;
    }

    uint32_t expected[kKexToggleWords] = {};
    char const *const testValue = Bgetenv("VB_KEX_TOGGLE_TEST");
    int const testAll = testValue && Batoi(testValue) > 1;
    for (int i = 0; i < (testAll ? s_kexToggleFaces : 2); i++)
    {
        int const t = testAll ? i : targets[i];
        if (t < 0)
            continue;
        if (!s_kexToggleFace[t].present || KexToggle_FaceOwner(t) < 0)
            continue;
        for (int w = 0; w < kKexToggleWords; w++)
            expected[w] |= s_kexToggleFace[t].closure[w];
        KexToggleFace const &f = s_kexToggleFace[t];
        KexToggle_Blast(f.x, f.y, f.z, KexToggle_FaceOwner(t), 192);
    }

    int bad = 0, ordinary = 0, dynamic = 0, vertical = 0;
    for (int w = 0; w < kKexToggleWords; w++)
        bad += (s_kexToggleOpen[w] & expected[w]) != expected[w];
    for (int i = 0; i < s_kexToggleWalls; i++)
    {
        KexToggleWall const &t = s_kexToggleWall[i];
        if (!(expected[t.face >> 5] & (1u << (t.face & 31))))
            continue;
        if (t.dynamic)
        {
            dynamic++;
            continue;
        }
        ordinary++;
        bad += !!(wall[t.wall].cstat
                   & (CSTAT_WALL_BLOCK | CSTAT_WALL_BLOCK_HITSCAN | CSTAT_WALL_MASKED));
        bad += wall[t.wall].overpicnum != 0;
        if (t.clearReverse)
            bad += !!(wall[t.nextwall].cstat & CSTAT_WALL_MASKED)
                || wall[t.nextwall].overpicnum != 0;
    }
    int allDynamic = 0;
    for (int i = 0; i < s_kexToggleWalls; i++)
        allDynamic += s_kexToggleWall[i].dynamic != 0;
    bad += s_kexTogglePortalBindings != allDynamic;
    for (int seam = 0; seam < s_kexToggleSeams; seam++)
    {
        int expectedOpen = 0;
        for (int i = 0; i < s_kexToggleSides; i++)
        {
            KexToggleSide const &s = s_kexToggleSide[i];
            if (s.seam == seam
                && (expected[s.face >> 5] & (1u << (s.face & 31))))
            {
                expectedOpen = 1;
                break;
            }
        }
        bad += !!s_kexToggleSeamActive[seam] != expectedOpen;
        if (s_kexToggleSeamActive[seam])
        {
            int bunch = -1, sides = 0, cfmask = 0;
            vertical++;
            for (int i = 0; i < s_kexToggleSides; i++)
            {
                KexToggleSide const &s = s_kexToggleSide[i];
                if (s.seam != seam)
                    continue;
                int const sb = yax_getbunch(s.sector, s.cf);
                if (bunch < 0) bunch = sb;
                bad += sb < 0 || sb != bunch;
                cfmask |= 1 << s.cf;
                sides++;
            }
            bad += sides < 2 || cfmask != 3;

            // Every edge with one unambiguous counterpart must carry the
            // exact reciprocal YAX link.  This catches accidental joins at a
            // shared vertex before they can look like overlapping dimensions.
            for (int a = 0; a < s_kexToggleSides; a++)
            {
                KexToggleSide const &up = s_kexToggleSide[a];
                if (up.seam != seam || up.cf != YAX_FLOOR)
                    continue;
                int const upEnd = sector[up.sector].wallptr + sector[up.sector].wallnum;
                for (int uw = sector[up.sector].wallptr; uw < upEnd; uw++)
                {
                    int match = -1;
                    int matches = 0;
                    for (int b = 0; b < s_kexToggleSides; b++)
                    {
                        KexToggleSide const &down = s_kexToggleSide[b];
                        if (down.seam != seam || down.cf != YAX_CEILING)
                            continue;
                        int const downEnd = sector[down.sector].wallptr
                                          + sector[down.sector].wallnum;
                        for (int dw = sector[down.sector].wallptr; dw < downEnd; dw++)
                            if (dw != uw && KexToggle_SameDirectedEdge(uw, dw))
                            {
                                match = dw;
                                matches++;
                            }
                    }
                    bad += matches > 1;
                    if (matches == 1)
                        bad += yax_getnextwall(uw, YAX_FLOOR) != match
                            || yax_getnextwall(match, YAX_CEILING) != uw;
                }
            }
        }
    }
    OSD_Printf("Kex Toggle test: %s (%d ordinary bands, %d height-selected bands, %d vertical seams)\n",
               bad ? "FAIL" : "PASS", ordinary, dynamic, vertical);
}

// Save the logical open bits as well as the engine's runtime YAX arrays. The
// ordinary wall state is already in MyLoadSave's wall[] snapshot, but stacked
// wall bands consult these bits and dynamically opened floor/ceiling seams
// live in YAX's game-time arrays rather than in the legacy sector fields.
#ifdef YAX_ENABLE
static int16_t s_kexSaveBunch[kMaxSectors][2];
static int16_t s_kexSaveNextWall[kMaxWalls][2];
#endif
static uint32_t s_kexToggleTestSnapshot[kKexToggleWords];
static uint8_t s_kexToggleTestSeams[kMaxKexToggleSeams];
static int s_kexToggleTestSnapshotValid;

class KexToggleLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void KexToggleLoadSave::Save()
{
    Write(s_kexToggleOpen, sizeof(s_kexToggleOpen));
    Write(s_kexToggleSeamActive, sizeof(s_kexToggleSeamActive));
    Write(s_kexForceOpen, sizeof(s_kexForceOpen));
    Write(s_kexForceStun, sizeof(s_kexForceStun));
    Write(s_kexForceStunClock, sizeof(s_kexForceStunClock));

#ifdef YAX_ENABLE
    for (int i = 0; i < numsectors; i++)
        for (int cf = 0; cf < 2; cf++)
            s_kexSaveBunch[i][cf] = yax_getbunch(i, cf);
    for (int i = 0; i < numwalls; i++)
        for (int cf = 0; cf < 2; cf++)
            s_kexSaveNextWall[i][cf] = yax_getnextwall(i, cf);
    Write(s_kexSaveBunch, numsectors * 2 * sizeof(int16_t));
    Write(s_kexSaveNextWall, numwalls * 2 * sizeof(int16_t));
#endif

    if (Bgetenv("VB_KEX_TOGGLE_SAVE_TEST"))
    {
        Bmemcpy(s_kexToggleTestSnapshot, s_kexToggleOpen,
                sizeof(s_kexToggleTestSnapshot));
        Bmemcpy(s_kexToggleTestSeams, s_kexToggleSeamActive,
                sizeof(s_kexToggleTestSeams));
        s_kexToggleTestSnapshotValid = 1;
    }
}

void KexToggleLoadSave::Load()
{
    Read(s_kexToggleOpen, sizeof(s_kexToggleOpen));
    Read(s_kexToggleSeamActive, sizeof(s_kexToggleSeamActive));
    Read(s_kexForceOpen, sizeof(s_kexForceOpen));
    Read(s_kexForceStun, sizeof(s_kexForceStun));
    Read(s_kexForceStunClock, sizeof(s_kexForceStunClock));

    int badYax = 0;
#ifdef YAX_ENABLE
    Read(s_kexSaveBunch, numsectors * 2 * sizeof(int16_t));
    Read(s_kexSaveNextWall, numwalls * 2 * sizeof(int16_t));
    for (int i = 0; i < numsectors; i++)
        for (int cf = 0; cf < 2; cf++)
            yax_setbunch(i, cf, s_kexSaveBunch[i][cf]);
    for (int i = 0; i < numwalls; i++)
        for (int cf = 0; cf < 2; cf++)
            yax_setnextwall(i, cf, s_kexSaveNextWall[i][cf]);
    yax_update(2);
    for (int i = 0; i < numsectors; i++)
        for (int cf = 0; cf < 2; cf++)
            badYax += yax_getbunch(i, cf) != s_kexSaveBunch[i][cf];
    for (int i = 0; i < numwalls; i++)
        for (int cf = 0; cf < 2; cf++)
            badYax += yax_getnextwall(i, cf) != s_kexSaveNextWall[i][cf];
#endif

    // MyLoadSave has replaced wall[] already, so discard any bookkeeping
    // left by a temporary render/clip pass rather than restoring over it.
    s_kexWallChanges = 0;
    s_kexWallClipActive = 0;
    Bmemset(s_kexWallWasSaved, 0, sizeof(s_kexWallWasSaved));

    if (Bgetenv("VB_KEX_TOGGLE_SAVE_TEST") && s_kexToggleTestSnapshotValid)
    {
        int const bad = Bmemcmp(s_kexToggleTestSnapshot, s_kexToggleOpen,
                                sizeof(s_kexToggleOpen))
                     || Bmemcmp(s_kexToggleTestSeams, s_kexToggleSeamActive,
                                sizeof(s_kexToggleSeamActive))
                     || badYax;
        OSD_Printf("Kex Toggle save test: %s (logical faces and vertical topology restored)\n",
                   bad ? "FAIL" : "PASS");
    }
}

static KexToggleLoadSave *s_kexToggleLoadSave;

void KexToggleLoadSaveConstruct(void)
{
    s_kexToggleLoadSave = new KexToggleLoadSave();
}

void KexWall_Reset(void)
{
    KexWall_ClipEnd();
    s_kexWallClips = 0;
    s_kexPortals = 0;
    s_kexPortalTestPending = 0;
    s_kexToggleFaces = s_kexToggleWalls = s_kexToggleSides = s_kexToggleSeams = 0;
    s_kexTogglePortalBindings = 0;
    s_kexToggleTestPending = 0;
    Bmemset(s_kexToggleFace, 0, sizeof(s_kexToggleFace));
    Bmemset(s_kexToggleOpen, 0, sizeof(s_kexToggleOpen));
    Bmemset(s_kexToggleSeamActive, 0, sizeof(s_kexToggleSeamActive));
    Bmemset(s_kexDynamicWall, 0, sizeof(s_kexDynamicWall));
    Bmemset(s_kexForceOpen, 0, sizeof(s_kexForceOpen));
    Bmemset(s_kexForceStun, 0, sizeof(s_kexForceStun));
    Bmemset(s_kexForceStunClock, 0, sizeof(s_kexForceStunClock));
    Bmemset(s_kexForceDamage, 0, sizeof(s_kexForceDamage));
    s_kexForceFlash = 0;
    s_kexDamageFlash = s_kexPickupFlash = 0;
    Bmemset(s_kexWallOwner, 0xff, sizeof(s_kexWallOwner));
    Bmemset(s_kexPortalFirst, 0xff, sizeof(s_kexPortalFirst));
    Bmemset(s_kexPortalEnd, 0xff, sizeof(s_kexPortalEnd));
    if (!g_kexMode)
        return;

    Kex_LevelName(s_kexPortalMap, sizeof(s_kexPortalMap));
    s_kexPortals = KexTable_Load("kexportal.txt", kMaxKexPortalRows, KexPortal_ParseLine);
    for (int i = 0; i < s_kexPortals; i++)
    {
        s_kexDynamicWall[s_kexPortal[i].wall] = 1;
        int const nWall = s_kexPortal[i].wall;
        if (s_kexPortalFirst[nWall] < 0)
            s_kexPortalFirst[nWall] = i;
        s_kexPortalEnd[nWall] = i + 1;
    }
    KexTable_Load("kextoggle.txt",
                  kMaxKexToggleFaces + kMaxKexToggleWalls + kMaxKexToggleSides,
                  KexToggle_ParseLine);

    for (int nSector = 0; nSector < numsectors; nSector++)
    {
        int const end = sector[nSector].wallptr + sector[nSector].wallnum;
        for (int nWall = sector[nSector].wallptr; nWall < end; nWall++)
        {
            if ((unsigned)nWall < (unsigned)kMaxWalls)
                s_kexWallOwner[nWall] = nSector;
            int flags = 0;
            switch (wall[nWall].extra)
            {
                case kKexWallExtraBlockAIFallers: flags = kKexWallClipAIFallers; break;
                case kKexWallExtraBlockNonplayers: flags = kKexWallClipNonplayers; break;
                case kKexWallExtraBlockBoth: flags = kKexWallClipAIFallers | kKexWallClipNonplayers; break;
                default: continue;
            }
            if (s_kexDynamicWall[nWall])
                continue;
            if (s_kexWallClips >= kMaxWalls)
                continue;
            KexWallClip &c = s_kexWallClip[s_kexWallClips++];
            c.wall = nWall;
            c.sector = nSector;
            c.flags = flags;
        }
    }
    // Attach each height-selected portal row to its logical Toggle face.  The
    // sloped endpoint bounds disambiguate two bands on one wall that happen
    // to lead to the same sector.
    for (int i = 0; i < s_kexToggleWalls; i++)
    {
        KexToggleWall const &t = s_kexToggleWall[i];
        if (!t.dynamic)
            continue;
        int const first = s_kexPortalFirst[t.wall];
        int const end = s_kexPortalEnd[t.wall];
        for (int p = first; p >= 0 && p < end; p++)
        {
            KexPortalRow &r = s_kexPortal[p];
            if (r.nextwall == t.nextwall && r.nextsector == t.nextsector
                && r.top[0] == t.top[0] && r.bottom[0] == t.bottom[0]
                && r.top[1] == t.top[1] && r.bottom[1] == t.bottom[1])
            {
                r.toggleFace = t.face;
                s_kexTogglePortalBindings++;
                break;
            }
        }
    }
    KexPortal_BindSurfaces();
    s_kexPortalTestPending = s_kexPortals && Bgetenv("VB_KEX_PORTAL_TEST");
    s_kexToggleTestPending = s_kexToggleFaces && Bgetenv("VB_KEX_TOGGLE_TEST");
    if (s_kexPortals)
        OSD_Printf("Kex height-selected portals: %d faces on this level\n", s_kexPortals);
    if (s_kexWallClips)
        OSD_Printf("Kex directed actor clipping: %d walls\n", s_kexWallClips);
    if (s_kexToggleFaces)
        OSD_Printf("Kex authored destructible faces: %d faces, %d wall bands, %d vertical sides\n",
                   s_kexToggleFaces, s_kexToggleWalls, s_kexToggleSides);
    int dynamicToggleWalls = 0;
    for (int i = 0; i < s_kexToggleWalls; i++)
        dynamicToggleWalls += s_kexToggleWall[i].dynamic != 0;
    if (s_kexTogglePortalBindings != dynamicToggleWalls)
        OSD_Printf(OSD_ERROR "Kex authored destructible faces: only %d/%d height-selected bands bound\n",
                   s_kexTogglePortalBindings, dynamicToggleWalls);
}

void KexWall_ClipBegin(int nSprite, unsigned int clipmask)
{
    KexWall_ClipEnd();
    if (!g_kexMode || (!s_kexWallClips && !s_kexPortals)
        || (unsigned)nSprite >= (unsigned)kMaxSprites)
        return;

    int const wallMask = clipmask & 0xffff;
    int const stat = sprite[nSprite].statnum;
    int const nonplayer = stat != 100;
    int const aiFaller = stat == 99 || (stat >= 101 && stat <= 122);
    if (!wallMask)
        return;

    s_kexWallClipActive = 1;
    if (s_kexPortals)
        KexWall_ApplyPortals(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z,
                             wallMask, nonplayer, aiFaller, 0);
    if (!nonplayer && !aiFaller)
        return;
    for (int i = 0; i < s_kexWallClips; i++)
    {
        KexWallClip &c = s_kexWallClip[i];
        int block = nonplayer && (c.flags & kKexWallClipNonplayers);
        if (!block && aiFaller && (c.flags & kKexWallClipAIFallers)
            && (unsigned)c.wall < (unsigned)numwalls)
        {
            walltype const &w = wall[c.wall];
            if ((unsigned)w.point2 < (unsigned)numwalls
                && (unsigned)w.nextsector < (unsigned)numsectors)
            {
                int const x = (int)(((int64_t)w.x + wall[w.point2].x) / 2);
                int const y = (int)(((int64_t)w.y + wall[w.point2].y) / 2);
                block = getflorzofslope(c.sector, x, y)
                      != getflorzofslope(w.nextsector, x, y);
            }
        }
        if (!block || (unsigned)c.wall >= (unsigned)numwalls)
            continue;
        KexWall_Save(c.wall);
        wall[c.wall].cstat |= wallMask;
    }
}

void KexWall_ViewBegin(int x, int y, int z)
{
    KexWall_ClipEnd();
    if (!g_kexMode)
        return;
    if (s_kexPortalTestPending)
    {
        s_kexPortalTestPending = 0;
        KexPortal_SelfTest();
    }
    if (s_kexToggleTestPending)
    {
        s_kexToggleTestPending = 0;
        KexToggle_SelfTest();
    }
    if (!s_kexPortals)
        return;
    s_kexWallClipActive = 1;
    KexWall_ApplyPortals(x, y, z, 0, 0, 0, 1);
}

void KexWall_ViewEnd(void)
{
    KexWall_ClipEnd();
}

// Kex sector flag 0x20, labelled "No standing on edges" by the retail
// editor. Its collision model normally lets an actor's radius take support
// from a neighbouring sector even while the actor origin is outside that
// sector. The flag suppresses that sector's floor and ceiling candidates.
// The converter emits one lotag-83 marker for every surviving split piece.
static uint8_t s_noStandingEdges[kMaxSectors];
static int s_noStandingCount;

void KexNoStanding_Reset(void)
{
    Bmemset(s_noStandingEdges, 0, sizeof(s_noStandingEdges));
    s_noStandingCount = 0;
    if (!g_kexMode)
        return;

    int count = 0;
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum >= MAXSTATUS || sprite[i].lotag != 83
            || (unsigned)sprite[i].sectnum >= (unsigned)kMaxSectors)
            continue;
        if (!s_noStandingEdges[sprite[i].sectnum])
        {
            s_noStandingEdges[sprite[i].sectnum] = 1;
            count++;
            s_noStandingCount++;
        }
    }
    if (count)
        OSD_Printf("Kex no-edge standing: %d sectors\n", count);
}

void KexNoStanding_Add(int nSprite)
{
    // InitSectFlag() scanned all markers before ExamineSprites() reaches
    // them. Keep this assignment as a safe path for dynamically built maps.
    if (g_kexMode && (unsigned)sprite[nSprite].sectnum < (unsigned)kMaxSectors
        && !s_noStandingEdges[sprite[nSprite].sectnum])
    {
        s_noStandingEdges[sprite[nSprite].sectnum] = 1;
        s_noStandingCount++;
    }
    mydeletesprite(nSprite);
}

int KexNoStanding_Sector(int nSector)
{
    return g_kexMode && (unsigned)nSector < (unsigned)kMaxSectors
        && s_noStandingEdges[nSector];
}

int KexNoStanding_Active(void)
{
    return g_kexMode && s_noStandingCount != 0;
}

// Movers that hold where their tag put them. Build's channel-only ceiling
// door (24) and lift down (9) come with a pause switch that sends them back
// 60 / 150 ticks later, which is right for a plate-driven door and wrong for
// the remaster's DOOR_OPEN_ONLY_MESSAGE and FLOOR_LOWER_ACTIVE: opened or
// lowered once by a script, a boss or an artifact, those stay. The converter
// plants a lotag 73 sprite in such a sector. LoadObjects() processes the
// sector tags BEFORE ExamineSprites() gets to the sprites, so the table is
// filled by scanning the loaded sprites from InitSectFlag(), which runs
// first; ProcessSpriteTag() then only removes the marker.
static uint8_t s_hold[kMaxSectors];
// ... and the bomb walls' floors once blown: the converter raises an EXPLODE
// sector's floor to its ceiling (solid until blown) and leaves a lotag 74
// sprite at the floor it was authored with, which is where the energy block
// drops it (object.cpp) -- the block's own guess, the first neighbour's
// floor, lands on a sibling wall or nowhere when they stand in a cluster.
static int32_t s_bombFloor[kMaxSectors];
static uint8_t s_bombKnown[kMaxSectors];
// ... plus compatibility for old generated KEX folders. Before kexpad.txt,
// conversion emitted a lotag 81 sprite carrying an approximate drop-pad top.
// Fresh conversion uses KexPad's paired-surface controller and emits none.
static int32_t s_padTop[kMaxSectors];
static uint8_t s_padKnown[kMaxSectors];
void KexHold_Reset(void)
{
    Bmemset(s_hold, 0, sizeof(s_hold));
    Bmemset(s_bombKnown, 0, sizeof(s_bombKnown));
    Bmemset(s_padKnown, 0, sizeof(s_padKnown));
    if (!g_kexMode)
        return;
    int n = 0, nb = 0, np = 0;
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum >= MAXSTATUS || (unsigned)sprite[i].sectnum >= (unsigned)kMaxSectors)
            continue;
        if (sprite[i].lotag == 73)
        { s_hold[sprite[i].sectnum] = 1; n++; }
        else if (sprite[i].lotag == 74)
        { s_bombFloor[sprite[i].sectnum] = sprite[i].z; s_bombKnown[sprite[i].sectnum] = 1; nb++; }
        else if (sprite[i].lotag == 81) // legacy pre-kexpad conversion marker
        { s_padTop[sprite[i].sectnum] = sprite[i].z; s_padKnown[sprite[i].sectnum] = 1; np++; }
    }
    if (n || nb || np)
        OSD_Printf("Kex hold: %d movers hold where their tag puts them, %d bomb walls, %d drop pads\n", n, nb, np);
}
int KexHold_PadTop(int nSector, int *pZ)
{
    if (!g_kexMode || (unsigned)nSector >= (unsigned)kMaxSectors || !s_padKnown[nSector])
        return 0;
    *pZ = s_padTop[nSector];
    return 1;
}
int KexHold_BombFloor(int nSector, int *pZ)
{
    if (!g_kexMode || (unsigned)nSector >= (unsigned)kMaxSectors || !s_bombKnown[nSector])
        return 0;
    *pZ = s_bombFloor[nSector];
    return 1;
}
void KexHold_Add(int nSprite)
{
    mydeletesprite(nSprite);
}
int KexHold_Sector(int nSector)
{
    return g_kexMode && (unsigned)nSector < (unsigned)kMaxSectors && s_hold[nSector];
}

// kexWorld::UseWallSpecial only dispatches types 1, 3..9, 21 and 200..203.
// Build's legacy player loop instead signals every tagged wall/sector found by
// neartag.  Converted KEX maps deliberately retain other tagged movers so
// their contact, script, boss and projectile paths can operate; let Use reach
// only the source's direct-use subset.
int KexUse_WallAllowed(int nWall)
{
    if (!g_kexMode || (unsigned)nWall >= (unsigned)numwalls)
        return 0;
    for (int i = 0; i < s_kexSwitches; i++)
        if (s_kexSwitch[i].wall == nWall)
            return 1; // event 200..203
    return 0;
}

int KexUse_SectorAllowed(int nSector)
{
    if (!g_kexMode || (unsigned)nSector >= (unsigned)numsectors)
        return 0;

    // Type 7 is the special sealed-door message.  It shares Build's 2024
    // construction with type 2 (trigger-only), so the explicit marker is the
    // authoritative distinction.
    if (s_kexSealedDoor[nSector])
        return 1;

    switch (sector[nSector].lotag)
    {
        case 2001: // type 1 and types 3..6: normal/key doors
        case 1075: // type 9: slow close door
        case 2075: // type 8: close door
            return 1;
        case 2009: // type 21: use lift; type 25 is script-only and holds
            return !KexHold_Sector(nSector);
        default:
            return 0;
    }
}

void KexTrigger_Add(int nSprite, int nChannel)
{
    if (g_kexMode && s_triggers < kMaxKexTriggers)
    {
        KexTrigger &t = s_trigger[s_triggers++];
        Bmemset(&t, 0, sizeof(t));
        t.sector = sprite[nSprite].sectnum;
        t.chan = (int16_t)nChannel;
    }
    mydeletesprite(nSprite);
}

void KexTrigger_Init(void)
{
    for (int i = 0; i < s_triggers; i++)
    {
        KexTrigger &t = s_trigger[i];
        for (int j = 0; j < kMaxSprites && t.count < kMaxKexTriggerSprites; j++)
        {
            // The creatures' own statnums run 100..125 (anubis.cpp .. wasp.cpp).
            if (sprite[j].statnum >= 100 && sprite[j].statnum <= 125 && sprite[j].sectnum == t.sector)
                t.nSprite[t.count++] = (int16_t)j;
        }
        t.left = t.count;
        OSD_Printf("Kex trigger: sector %d watches %d creatures for channel %d\n", t.sector, t.count, t.chan);
    }
}

// VB_KEXTRIGGER_TEST=<ticks>: that far in, kill what each trigger watches,
// through the same damage path a bullet takes, and see the channel fire.
void KexTrigger_Test(void)
{
    static int s_test = -1;
    if (s_test < 0) { char const *e = getenv("VB_KEXTRIGGER_TEST"); s_test = e ? Batoi(e) : 0; }
    if (s_test <= 0 || --s_test != 0)
        return;
    for (int i = 0; i < s_triggers; i++)
        for (int j = 0; j < s_trigger[i].count; j++)
            if (s_trigger[i].nSprite[j] >= 0)
            {
                int const n = s_trigger[i].nSprite[j];
                OSD_Printf("Kex trigger test: hitting sprite %d (statnum %d cstat %x)\n", n, sprite[n].statnum, sprite[n].cstat);
                runlist_DamageEnemy(n, -1, 20000);
            }
}

void KexScript_BossDied(int nSprite);   // below

void KexTrigger_Died(int nSprite)
{
    KexScript_BossDied(nSprite);
    for (int i = 0; i < s_triggers; i++)
    {
        KexTrigger &t = s_trigger[i];
        if (t.fired)
            continue;
        for (int j = 0; j < t.count; j++)
        {
            if (t.nSprite[j] != nSprite)
                continue;
            t.nSprite[j] = -1;
            if (--t.left <= 0)
            {
                t.fired = 1;
                Kex_FireChannel(t.chan);
                OSD_Printf("Kex trigger: sector %d cleared, channel %d fired\n", t.sector, t.chan);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SCRIPT sectors. The game has three, and the pads of Ramses (handled in
// ramses.cpp). Transcribed from scripts/map/*_script.txt; a sector with tag N
// runs $script N+1 when entered, and $script 0 runs at load:
//
//   KILENTRY $script 8: ChangeMap(KILARENA)          -- the arena's entrance
//   KILARENA $script 1: GetArtifacts() = PA_ALL; ChangeMap(KILENTRY)
//   SUNKEN   $script 0: FireRemoteEventFromTag(1001), (1002)
//   SUNKEN   $script 1: wait until the player holds key 1, then fire 2 and 1007
//              (the script restarts after a 0.1 second delay)
// ---------------------------------------------------------------------------
enum { kKexScriptChangeMap = 0, kKexScriptKeyFires };
struct KexScriptRule
{
    char const *map;
    int16_t tag;
    int8_t  kind, allArtifacts, keyBit;
    char const *nextMap;
    int16_t fire[2];
};
static KexScriptRule const s_scriptRule[] = {
    { "KILENTRY", 7, kKexScriptChangeMap, 0, -1, "KILARENA", { 0, 0 } },
    { "KILARENA", 0, kKexScriptChangeMap, 1, -1, "KILENTRY", { 0, 0 } },
    { "SUNKEN",   0, kKexScriptKeyFires,  0,  0, NULL,       { 2, 1007 } },
};
struct KexLoadRule { char const *map; int16_t fire[2]; };
static KexLoadRule const s_loadRule[] = {
    { "SUNKEN", { 1001, 1002 } },
};

// The boss levels' scripts (SELBUROW_script.txt, SETARENA_script.txt), the
// same in both: $script 0 at load -- if the player already holds the level's
// artifact (Selkis' scepter, Set's shawl), remove the boss (map actor 41 /
// 67) and fire tag 1010, the exit; else poll every two seconds ($script 1)
// and fire 1010 once the boss actor is gone. The persistent Selkis and Set
// corpses cannot satisfy that live-map poll, so their authored terminal
// callbacks fire the exit directly (as does the Queen's first snake-death
// explosion); the poll remains for an actor removed before a reload. Bosses
// are told apart by statnum. At artifact-held load, the native script's
// actor.Remove() bridge tears them down without a damage/death path.
struct KexBossRule { char const *map; int16_t statnum; int8_t artifact; int16_t fire; };
static KexBossRule const s_bossRule[] = {
    { "SELBUROW", 122, kKexArtiScepter, 1010 },   // Selkis
    { "SETARENA", 120, kKexArtiShawl,   1010 },   // Set
    { "KILARENA", 121, -1,              1010 },   // the Kilmaat Queen: no artifact, the exit alone
};

// runlist_AllocChannel() finds a channel by its STATE, which starts as the
// channel number -- and a door's elevator rewrites that state on its first
// cycle, so a lookup made later allocates a fresh channel nobody listens on.
// The plates hold their channel INDEX from build time; so must the rules.
// Every tag a rule can fire is resolved here at level start, before any
// mover has run, and fired by index from then on.
#define kMaxKexChanCache 64
static struct { int tag; int16_t index; } s_chanCache[kMaxKexChanCache];
static int s_chanCached;

static int Kex_ChannelIndex(int tag)
{
    for (int i = 0; i < s_chanCached; i++)
        if (s_chanCache[i].tag == tag)
            return s_chanCache[i].index;
    int const chan = KexChan(tag);
    int const idx = chan < 0 ? -1 : runlist_AllocChannel(chan);
    if (s_chanCached < kMaxKexChanCache)
    { s_chanCache[s_chanCached].tag = tag; s_chanCache[s_chanCached++].index = (int16_t)idx; }
    return idx;
}

// A Kex tag, fired through the channel the converter allotted it.
void KexScript_FireTag(int tag, char const *why)
{
    int const idx = Kex_ChannelIndex(tag);
    if (idx < 0)
    {
        OSD_Printf("Kex script: tag %d not allotted on this level, nothing to fire (%s)\n", tag, why);
        return;
    }
    Kex_FireChannel(idx);
    OSD_Printf("Kex script: tag %d fired on channel %d (%s)\n", tag, idx, why);
}

// Taking an artifact fires 1000 + its index: the tag the converter's audit
// found listened on, and nothing else fired, exactly where the artifacts are
// -- SANCTUAR's message door 1000 with the sandals (0), SETARENA's 1002 with
// the shawl (2), KILENTRY's six lifts 1000..1005 -- so it is the remaster's
// own rule, hardcoded there. At load it fires for every artifact held, the
// way the boss levels' scripts treat an artifact brought in.
void KexScript_ArtifactTaken(int nArtifact)
{
    if (g_kexMode && (unsigned)nArtifact < (unsigned)kKexArtiCount)
        KexScript_FireTag(1000 + nArtifact, "artifact taken");
}

// TOMB's message door 1020 stands beside Ramses' crypt and its script
// sector; it opens when his speech is over (ramses.cpp).
void KexScript_RamsesEnd(void)
{
    char base[32];
    Kex_LevelName(base, sizeof(base));
    if (!Bstrcmp(base, "TOMB"))
        KexScript_FireTag(1020, "Ramses has spoken");
}
static KexBossRule const *s_boss;     // this level's, or NULL
static int s_bossTest;                // VB_KEXBOSS_TEST: 1 kill the boss at tick 60, 2 also hold its artifact first
static int s_bossTestActive;
static int s_bossPoll;                // the remaster's two-second script poll
static uint8_t s_bossExitFired;

static void KexScript_BossFire(char const *why)
{
    if (s_bossExitFired)
        return;
    s_bossExitFired = 1;
    Kex_FireChannel(Kex_ChannelIndex(s_boss->fire));
    OSD_Printf("Kex script: %s, tag %d fired (%s)\n", why, s_boss->fire, s_boss->map);
    if (s_bossTestActive)
        OSD_Printf("Kex boss test: PASS (boss rule %s fired exit tag %d)\n",
                   s_boss->map, s_boss->fire);
}

void KexScript_BossEvent(int nSprite, char const *why)
{
    if (s_boss && (unsigned)nSprite < (unsigned)kMaxSprites
        && sprite[nSprite].statnum == s_boss->statnum)
        KexScript_BossFire(why);
}

static void KexScript_BossKill(void)
{
    int list[16], n = 0;
    for (int i = headspritestat[s_boss->statnum]; i != -1 && n < 16; i = nextspritestat[i])
        list[n++] = i;
    for (int k = 0; k < n; k++)
        // Selkis has the largest single boss pool: 4000 KEX health, or
        // 16000 native units. Ten 1600-unit test hits reach it exactly.
        for (int hit = 0; hit < 10 && sprite[list[k]].statnum == s_boss->statnum; hit++)
            runlist_DamageEnemy(list[k], -1, 1600);
}

static int KexScript_BossRemove(void)
{
    int list[16], n = 0, removed = 0;
    for (int i = headspritestat[s_boss->statnum]; i != -1 && n < 16; i = nextspritestat[i])
        list[n++] = i;
    for (int k = 0; k < n; k++)
    {
        // The artifact-level scripts call actor.Remove(), which suppresses
        // all kill-side effects.  Only Selkis and Set have that script path.
        if (s_boss->statnum == 122)
            removed += KexSelkis_RemoveSprite(list[k]);
        else if (s_boss->statnum == 120)
            removed += KexSet_RemoveSprite(list[k]);
    }
    return removed;
}

void KexScript_BossDied(int nSprite)
{
    if (s_boss && sprite[nSprite].statnum == s_boss->statnum)
        s_bossPoll = kTimerTicks;
}

static void KexScript_BossLoad(void)
{
    if (!s_boss || s_boss->artifact < 0)
        return;
    int held = 0;
    for (int i = 0; i < nTotalPlayers; i++)
        held |= KexInv_HasArtifact(i, s_boss->artifact);
    if (!held)
        return;
    int const removed = KexScript_BossRemove();
    if (removed)
        OSD_Printf("Kex script: the artifact is held, the boss removed (%s)\n", s_boss->map);
    KexScript_BossFire("the artifact is held");
}

void KexScript_BossTick(void)
{
    if (s_boss && s_bossPoll > 0 && --s_bossPoll == 0)
    {
        if (headspritestat[s_boss->statnum] == -1)
            KexScript_BossFire("the boss is gone");
        else
            s_bossPoll = kTimerTicks;
    }
    // Queen's body turns into a second, shootable snake form. Keep the
    // fixture applying its deterministic fatal hit packet until the normal
    // two-second disappearance poll sees the complete boss hierarchy gone.
    if (s_bossTestActive && !s_bossExitFired)
        KexScript_BossKill();
    // VB_KEXRAMSES_TEST=1: at tick 100, act as if Ramses had just finished
    // speaking (TOMB's crypt door), without the speech.
    static int s_ramsesTest = -1;
    if (s_ramsesTest < 0)
        s_ramsesTest = getenv("VB_KEXRAMSES_TEST") ? 100 : 0;
    if (s_ramsesTest > 0 && --s_ramsesTest == 0)
        KexScript_RamsesEnd();
    // VB_KEXEXPLODE_TEST=1: at tick 100, blow the first bomb wall (statnum
    // 406, the energy block); its siblings on the tag should follow.
    static int s_explodeTest = -1;
    if (s_explodeTest < 0)
        s_explodeTest = getenv("VB_KEXEXPLODE_TEST") ? 100 : 0;
    if (s_explodeTest > 0 && --s_explodeTest == 0)
    {
        void ExplodeEnergyBlock(int nSprite);
        int const i = headspritestat[406];
        if (i >= 0)
            ExplodeEnergyBlock(i);
    }
    if (s_bossTest <= 0 || --s_bossTest != 0)
        return;
    if (!s_boss || headspritestat[s_boss->statnum] == -1)
    {
        OSD_Printf("Kex boss test: FAIL (rule %s, live actor stat %d)\n",
                   s_boss ? s_boss->map : "missing", s_boss ? s_boss->statnum : -1);
        return;
    }
    s_bossTestActive = 1;
    OSD_Printf("Kex boss test: armed %s actor stat %d at native tick 60\n",
               s_boss->map, s_boss->statnum);
    if (getenv("VB_KEXBOSS_TEST")[0] == '2')
    {
        KexInv_GiveArtifact(nLocalPlayer, s_boss->artifact);
        KexScript_BossLoad();
    }
    else
        KexScript_BossKill();
}

#define kMaxKexScripts 8
struct KexScript
{
    int16_t sector, tag;
    int16_t chan[2];
    int16_t wait;
    KexScriptRule const *rule;
    uint8_t armed, done;
};
static KexScript s_script[kMaxKexScripts];
static int s_scripts;

void KexScript_Reset(void) { s_scripts = 0; }

void KexScript_Add(int nSprite, int nTag)
{
    if (g_kexMode && s_scripts < kMaxKexScripts)
    {
        char base[32];
        Kex_LevelName(base, sizeof(base));
        KexScript &sc = s_script[s_scripts++];
        Bmemset(&sc, 0, sizeof(sc));
        sc.sector = sprite[nSprite].sectnum;
        sc.tag = (int16_t)nTag;
        sc.chan[0] = sc.chan[1] = -1;
        for (auto const &r : s_scriptRule)
        {
            if (Bstrcmp(base, r.map) || r.tag != nTag)
                continue;
            sc.rule = &r;
            // Channels are matched by value while the level loads, so the
            // ones this script fires are claimed now, with the level's own.
            for (int k = 0; k < 2; k++)
                if (r.fire[k] > 0)
                    { int const c = KexChan(r.fire[k]); sc.chan[k] = c >= 0 ? runlist_AllocChannel(c) : -1; }
        }
        if (!sc.rule)
            s_scripts--;         // a script this port does not know: no-op
    }
    mydeletesprite(nSprite);
}

void KexScript_Init(void)
{
    if (!g_kexMode)
        return;
    char base[32];
    Kex_LevelName(base, sizeof(base));
    // Resolve every tag a rule on this level can fire while the channels'
    // states are still their numbers (see Kex_ChannelIndex).
    s_chanCached = 0;
    for (auto const &r : s_scriptRule) if (!Bstrcmp(base, r.map)) for (int k = 0; k < 2; k++) if (r.fire[k] > 0) Kex_ChannelIndex(r.fire[k]);
    for (auto const &r : s_bossRule)   if (!Bstrcmp(base, r.map)) Kex_ChannelIndex(r.fire);
    for (int a = 0; a < kKexArtiCount; a++) Kex_ChannelIndex(1000 + a);
    Kex_ChannelIndex(1020);
    for (auto const &r : s_loadRule)
    {
        if (Bstrcmp(base, r.map))
            continue;
        for (int k = 0; k < 2; k++)
            if (r.fire[k] > 0)
            {
                Kex_FireChannel(Kex_ChannelIndex(r.fire[k]));
                OSD_Printf("Kex script: %s $script 0 fires tag %d\n", base, r.fire[k]);
            }
    }
    if (s_scripts)
        OSD_Printf("Kex script: %d script sectors on this level\n", s_scripts);
    KexForce_Update();
    {
        int nUnder = 0, nWade = 0;
        for (int i = 0; i < numsectors; i++)
        { nUnder += (SectFlag[i] & kSectUnderwater) != 0; nWade += SectDepth[i] > 0; }
        if (nUnder || nWade)
            OSD_Printf("Kex water: %d sectors under water, %d to wade\n", nUnder, nWade);
    }
    s_boss = NULL;
    s_bossPoll = 0;
    s_bossExitFired = 0;
    for (auto const &r : s_bossRule)
        if (!Bstrcmp(base, r.map))
            s_boss = &r;
    { char const *e = getenv("VB_KEXBOSS_TEST"); s_bossTest = e ? 60 : 0; }
    s_bossTestActive = 0;
    KexScript_BossLoad();
    for (int a = 0; a < kKexArtiCount; a++)
        for (int i = 0; i < nTotalPlayers; i++)
            if (KexInv_HasArtifact(i, a))
            { KexScript_FireTag(1000 + a, "artifact held at load"); break; }
}

// Leave for another of the remaster's levels, keeping the campaign flow
// (no overworld in between): the map name goes where the user map's was and
// the game loop reloads.
static void Kex_ChangeMap(char const *name)
{
    // Map scripts and travel objects both ultimately call this helper.  Stage
    // the source persistent data here so a scripted transition cannot lose it
    // to RestartPlayer() while the map reload is being scheduled.
    KexCampaign_StagePlayer(nLocalPlayer);

    char path[BMAX_PATH];
    Bstrncpyz(path, gUserMapFilename, sizeof(path));
    char *base = path;
    for (char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    Bsnprintf(base, sizeof(path) - (base - path), "KEX_%s.MAP", name);
    Bstrncpyz(gUserMapFilename, path, BMAX_PATH);
    bUserMap = true;
    levelnew = levelnum;
    OSD_Printf("Kex script: ChangeMap -> %s\n", path);
}

// kexTravelObject schedules the player at destinationPosition after the next
// map creates its puppet.  Build constructs that puppet in RestartPlayer(),
// so retain the one-shot source-space destination until the level loop reaches
// its existing pre-spawn save-point hook.
struct KexTravelWarp
{
    char map[BMAX_PATH];
    int32_t x, y, z;
    uint8_t pending;
};
static KexTravelWarp s_kexTravelWarp;

static void KexTravel_ScheduleWarp(int x, int y, int z)
{
    Bstrncpyz(s_kexTravelWarp.map, gUserMapFilename, sizeof(s_kexTravelWarp.map));
    s_kexTravelWarp.x = x;
    s_kexTravelWarp.y = y;
    s_kexTravelWarp.z = z;
    s_kexTravelWarp.pending = 1;
}

int KexTravel_PrepareSpawn(int nPlayer, int nAngle)
{
    if (!g_kexMode || !s_kexTravelWarp.pending || nPlayer != nLocalPlayer)
        return 0;

    // A queued scripted map change supersedes this transient travel warp.
    // It is deliberately not a save-game field: native only retains it for
    // the immediate map transition that follows the travel-object touch.
    if (Bstrcasecmp(s_kexTravelWarp.map, gUserMapFilename))
    {
        s_kexTravelWarp.pending = 0;
        return 0;
    }

    int16_t nSector = -1;
    updatesector(s_kexTravelWarp.x, s_kexTravelWarp.y, &nSector);
    s_kexTravelWarp.pending = 0;
    if ((unsigned)nSector >= (unsigned)numsectors)
    {
        OSD_Printf(OSD_ERROR "Kex travel warp (%d,%d,%d) is outside %s; using map start\n",
                   s_kexTravelWarp.x, s_kexTravelWarp.y, s_kexTravelWarp.z,
                   gUserMapFilename);
        return 0;
    }

    // kexPuppet moves only Origin and then FindSector(), preserving the new
    // map player's authored yaw. Build's save point is that puppet's input.
    SetSavePoint(nPlayer, s_kexTravelWarp.x, s_kexTravelWarp.y,
                 s_kexTravelWarp.z, nSector, (short)nAngle);
    OSD_Printf("Kex travel: spawned at source destination (%d,%d,%d), sector %d\n",
               s_kexTravelWarp.x, s_kexTravelWarp.y, s_kexTravelWarp.z, nSector);
    return 1;
}

void KexTravel_ChangeMap(int nMap)
{
    if (!g_kexMode)
        return;

    // Older generated maps encoded the Ramses body as 24.  The retail actor
    // definition says Map_Tomb2 = 22; normalize that old bridge tag here.
    if (nMap == 24)
        nMap = 22;

    // res/defs/mapInfo.txt: Map_Tomb1 = 0 and Map_Tomb2 = 22.
    if (nMap == 0 || nMap == 22)
        KexCampaign_UnlockMap(nMap);
    if (nMap == 0)
    {
        Kex_ChangeMap("TOMB");
        // Obj_WarpPryamid destinationPosition "0 2816 -864", converted
        // with x*6, -y*6, -z*96. This is sector 5 of KEX_TOMB.MAP; its
        // former unrelated position landed in no converted TOMB sector.
        KexTravel_ScheduleWarp(0, -16896, 82944);
    }
    else if (nMap == 22)
    {
        Kex_ChangeMap("TOMBEND");
        // Obj_Ramses_Body destinationPosition "0 2816 -224".
        KexTravel_ScheduleWarp(0, -16896, 21504);
    }
}

void KexScript_Tick(void)
{
    KexTrigger_Test();
    // VB_KEXKEY_TEST=1: at tick 100, hand the local player key 1 and arm the
    // key rules as if the script sector had been entered (SUNKEN).
    static int s_keyTest = -1;
    if (s_keyTest < 0)
        s_keyTest = getenv("VB_KEXKEY_TEST") ? 100 : 0;
    if (s_keyTest > 0 && --s_keyTest == 0)
    {
        PlayerList[nLocalPlayer].keys |= 0x1000;
        for (int i = 0; i < s_scripts; i++)
            if (s_script[i].rule && s_script[i].rule->kind == kKexScriptKeyFires)
                s_script[i].armed = 1;
        OSD_Printf("Kex script test: key 1 granted, key rules armed\n");
    }
    for (int i = 0; i < s_scripts; i++)
    {
        KexScript &sc = s_script[i];
        if (sc.done || !sc.rule)
            continue;
        if (!sc.armed)
        {
            for (int nPlayer = 0; nPlayer < nTotalPlayers && !sc.armed; nPlayer++)
            {
                if (KexPlayer_OnSectorFloor(nPlayer, sc.sector))
                    sc.armed = 1;
            }
            if (!sc.armed)
                continue;
        }
        switch (sc.rule->kind)
        {
            case kKexScriptChangeMap:
                if (sc.rule->allArtifacts)
                    for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
                        for (int a = 0; a < kKexArtiCount; a++)
                            KexInv_GiveArtifact(nPlayer, a);
                sc.done = 1;
                Kex_ChangeMap(sc.rule->nextMap);
                break;

            case kKexScriptKeyFires:
            {
                if (sc.wait > 0)
                {
                    sc.wait--;
                    continue;
                }
                int have = 0;
                for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
                    if (PlayerList[nPlayer].keys & (0x1000 << sc.rule->keyBit))
                        have = 1;
                if (!have)
                {
                    sc.wait = kTimerTicks / 10;
                    break;
                }
                for (int k = 0; k < 2; k++)
                    Kex_FireChannel(sc.chan[k]);
                sc.done = 1;
                OSD_Printf("Kex script: key %d in hand, tags %d and %d fired\n",
                           sc.rule->keyBit + 1, sc.rule->fire[0], sc.rule->fire[1]);
                break;
            }
        }
    }
}
