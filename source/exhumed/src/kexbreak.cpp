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
// kexmap.py writes that out as kexbreak.txt: one line of
// "<tile> <health> <first shard tile> <shard count>" per prop type.
//
// Build already has the pieces this needs. A prop registers a runlist run the
// way BuildObject() does, so a bullet that hits the sprite routes a 0x80000
// damage message straight to it, and the shards are BuildCreatureChunk() --
// the same physics chunks the exploding fire cauldron throws.
//
//-------------------------------------------------------------------------
#include "build.h"
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
#include "anims.h"
#include "random.h"
#include "lighting.h"
#include "kextable.h"
#include "kexbreak.h"

#define kMaxKexBreakTypes 64
#define kMaxKexBreaks     512

struct KexBreakType
{
    int16_t tile, health, shard0, nshards, clipdist;
    int16_t blastradius, blastdamage;   // A_RadialBlast, in the remaster's units
};

struct KexBreakProp
{
    int16_t nSprite, nHealth, nType, nRunFree;
};

static KexBreakType s_type[kMaxKexBreakTypes];
static int          s_types = -1;          // -1 = not read yet
static KexBreakProp s_prop[kMaxKexBreaks];
static int          s_props;
static int          s_selftest;   // VB_KEXBREAK_TEST: shatter everything after N ticks

static bool KexBreak_ParseLine(char *line, int nRec)
{
    int tile, health, shard0, nshards, clipdist = 32, br = 0, bd = 0;
    if (Bsscanf(line, "%d %d %d %d %d %d %d", &tile, &health, &shard0, &nshards,
                &clipdist, &br, &bd) < 4)
        return false;

    KexBreakType &t = s_type[nRec];
    t.tile = tile; t.health = health;
    t.shard0 = shard0; t.nshards = nshards;
    t.clipdist = clamp(clipdist, 1, 255);
    t.blastradius = br; t.blastdamage = bd;
    return true;
}

static void KexBreak_LoadTable(void)
{
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
void KexLight_Tick(void)
{
    for (int i = 0; i < s_lights; i++)
    {
        spritetype const &s = sprite[s_light[i].nSprite];
        if (s.sectnum < 0 || (s.cstat & 0x8000))    // deleted or invisible
            continue;

        KexLightType const &t = s_lightType[s_light[i].nType];
        Kex_SpriteGlow(s.sectnum, s.x, s.y, t.peak, t.shift, 0);
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

struct KexSoundAlias { char name[12]; char path[32]; uint8_t loop; };

static KexSoundAlias s_soundAlias[kMaxKexSounds];
static int           s_soundAliases = -1;    // -1 = not read yet

static bool KexSound_ParseLine(char *line, int nRec)
{
    KexSoundAlias &a = s_soundAlias[nRec];
    int loop = 0;
    if (Bsscanf(line, "%11s %31s %d", a.name, a.path, &loop) < 2)
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

// ---------------------------------------------------------------------------
// Teleports. Kex event 72 is a pad the player steps on, 71 the arrival point
// with its facing; Build PowerSlave has no sector teleporter, so kexmap.py
// writes kextele.txt -- "<map> <padsector> <destsector> <x> <y> <z> <ang>" --
// and stepping into the pad's sector moves the player the way RestartPlayer()
// places one. One-shot per visit: leaving the pad re-arms it. There is exactly
// one pair in the retail maps, in KILARENA.
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

static bool KexTele_ParseLine(char *line, int nRec)
{
    KexTele &t = s_teleAll[nRec];
    int pad, dest, x, y, z, ang;
    if (Bsscanf(line, "%15s %d %d %d %d %d %d", t.map, &pad, &dest,
                &x, &y, &z, &ang) < 7)
        return false;
    t.pad = (int16_t)pad; t.dest = (int16_t)dest;
    t.x = x; t.y = y; t.z = z; t.ang = (int16_t)(ang & kAngleMask);
    return true;
}

void KexTele_Init(void)
{
    s_teles = 0;
    Bmemset(s_teleOnPad, 0, sizeof(s_teleOnPad));
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
}

void KexTele_Tick(void)
{
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
            onPad = 1;
            if (s_teleOnPad[nPlayer])
                break;

            sprite[nSprite].x = s_tele[i].x;
            sprite[nSprite].y = s_tele[i].y;
            sprite[nSprite].z = s_tele[i].z;
            sprite[nSprite].xvel = 0;
            sprite[nSprite].yvel = 0;
            sprite[nSprite].zvel = 0;
            mychangespritesect(nSprite, s_tele[i].dest);
            PlayerList[nPlayer].q16angle = fix16_from_int(s_tele[i].ang);
            PlayerList[nPlayer].q16oangle = PlayerList[nPlayer].q16angle;
            sprite[nSprite].ang = s_tele[i].ang;
            PlayerList[nPlayer].opos = sprite[nSprite].xyz;

            OSD_Printf("Kex teleport: player %d -> sector %d\n",
                       nPlayer, s_tele[i].dest);
            PlayFX2(StaticSound[44] | 0x2000, nSprite);   // tele_1
            AddFlash(s_tele[i].dest, s_tele[i].x, s_tele[i].y, s_tele[i].z, 80);
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
    s_prop[nProp].nRunFree = runlist_AddRunRec(NewRun, nProp, kRunKexBreak);
}

void KexBreak_Init(void)
{
    s_props = 0;
    { char const *e = getenv("VB_KEXBREAK_TEST"); s_selftest = e ? Batoi(e) : 0; }
    if (!KexBreak_Available())
        return;

    // The converter emits these as ordinary decor sprites, so they arrive in
    // statnum 0 with the prop's tile as their picnum.
    for (int nSprite = headspritestat[0]; nSprite != -1; )
    {
        int const nNext = nextspritestat[nSprite];
        int const nType = KexTable_FindTile(s_type, s_types, sprite[nSprite].picnum);
        if (nType >= 0)
            KexBreak_Arm(nSprite, nType);
        nSprite = nNext;
    }
    if (s_props)
        OSD_Printf("Kex breakables: %d props armed from %d types\n", s_props, s_types);
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
// The scepter, sandals and shawl branches cannot apply here because Exhumed has
// no such artifacts, which is the same path the remaster takes before the
// player owns any of them -- so the fall-throughs land exactly where they do
// there. The rare Item_Map and Item_Power rolls are left out for the same
// reason. Item_Ammo is the speed loader and Item_Health the berry twig, the
// two items those names correspond to in Build PowerSlave.
static void KexBreak_DropItem(int nSprite)
{
    int const nSector = sprite[nSprite].sectnum;
    int const x = sprite[nSprite].x, y = sprite[nSprite].y, z = sprite[nSprite].z;
    int const r = RandomSize(3);        // kexSlaveRandomizer::Max(8)

    short lotag = 0, hitag = 0, picnum = 0;

    switch (r)
    {
        case 0:
            // The remaster spawns AI_Scorp here -- a 20-HP critter. Build has
            // no small scorpion; the spider is the nearest pest. An earlier
            // version called BuildScorp, which is the 20000-HP Selkis BOSS.
            BuildSpider(-1, x, y, z, nSector, RandomSize(11));
            return;

        case 1:
        case 2:
        case 3:
            lotag = 6; hitag = 10; picnum = 877;    // speed loader
            break;

        case 4:
            // "health > 100" of the remaster's 200, so half of full here.
            if (PlayerList[nLocalPlayer].nHealth > (kMaxHealth / 2) && RandomBit())
                return;
            fallthrough__;

        case 5:
            lotag = 12; picnum = 718;               // berry twig
            break;

        default:
            return;
    }

    int const nItem = insertsprite(nSector, 0);
    if (nItem < 0)
        return;

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

    // The item machinery does the rest: statnum, repeats and, where the item
    // has one, its animation.
    ProcessSpriteTag(nItem, lotag, hitag);
}

// The death anim: a break sound, one physics chunk per shard, the random item
// the remaster drops, and the prop is gone.
static void KexBreak_Shatter(int nProp)
{
    int const nSprite = s_prop[nProp].nSprite;
    KexBreakType const &t = s_type[s_prop[nProp].nType];

    PlayFX2(StaticSound[kSoundPotPc1], nSprite);

    // 0x4000 is the fire cauldron's own flag for a livelier throw, which is the
    // closest thing here to the remaster's A_TossActor velocities.
    int nChunks = 0;
    for (int i = 0; i < t.nshards; i++)
    {
        if (BuildCreatureChunk(nSprite | 0x4000, t.shard0 + i) < 0)
            break;
        nChunks++;
    }
    if (s_selftest)
        OSD_Printf("Kex breakables: prop %d (tile %d) -> %d/%d shards\n",
                   nProp, t.tile, nChunks, t.nshards);

    if (t.blastdamage > 0)
    {
        // The exploding pots. Same recipe as the fire cauldron's explosion in
        // FuncObject: flash, the explosion anim on the floor (its SEQ carries
        // the boom), then the radial. The remaster's A_RadialBlast radius is
        // in its own units; runlist compares (|dx|+|dy|)>>8, so radius 512
        // becomes 512 * WORLD(6) / 256 = 12 against the cauldron's 20.
        AddFlash(sprite[nSprite].sectnum, sprite[nSprite].x, sprite[nSprite].y,
                 sprite[nSprite].z, 128);
        BuildAnim(-1, 36, 0, sprite[nSprite].x, sprite[nSprite].y,
                  sector[sprite[nSprite].sectnum].floorz,
                  sprite[nSprite].sectnum, 240, 4);
        runlist_RadialDamageEnemy(nSprite, t.blastdamage,
                                  (int16_t)(t.blastradius * 6 / 256));
    }
    else
        KexBreak_DropItem(nSprite);

    // Unregister and remove the prop, the way the exploding cauldron does.
    runlist_SubRunRec(sprite[nSprite].owner);
    runlist_SubRunRec(s_prop[nProp].nRunFree);
    s_prop[nProp].nSprite = -1;
    mydeletesprite(nSprite);
}

void FuncKexBreak(int a, int b, int nRun)
{
    int const nProp = RunData[nRun].nVal;
    if (nProp < 0 || nProp >= s_props || s_prop[nProp].nSprite < 0)
        return;

    switch (a & 0x7F0000)
    {
        case 0x20000:   // per tick: nothing to do, the prop just stands there
            if (s_selftest && --s_selftest == 1)
                for (int i = 0; i < s_props; i++)
                    if (s_prop[i].nSprite >= 0)
                    { s_prop[i].nHealth = 0; KexBreak_Shatter(i); }
            return;
        case 0x30000:
        case 0x90000:   // drawn as an ordinary sprite, so no sequence to plot
            return;

        case 0x80000:   // shot
        {
            if (s_prop[nProp].nHealth <= 0)
                return;
            s_prop[nProp].nHealth -= (short)b;
            if (s_prop[nProp].nHealth <= 0)
                KexBreak_Shatter(nProp);
            return;
        }

        case 0xA0000:   // caught in a blast
        {
            if (s_prop[nProp].nHealth <= 0)
                return;
            int const nDamage = runlist_CheckRadialDamage(s_prop[nProp].nSprite);
            if (nDamage <= 0)
                return;
            s_prop[nProp].nHealth -= nDamage;
            if (s_prop[nProp].nHealth <= 0)
                KexBreak_Shatter(nProp);
            return;
        }

        default:
            return;
    }
}
