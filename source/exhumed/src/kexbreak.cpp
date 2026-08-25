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
#include "object.h"
#include "random.h"
#include "lighting.h"
#include "kextable.h"
#include "kexbreak.h"
#include "kexinv.h"
#include "init.h"

void KillCreatures();   // object.cpp, undeclared there

#define kMaxKexBreakTypes 64
#define kMaxKexBreaks     512

struct KexBreakType
{
    int16_t tile, health, shard0, nshards, clipdist;
    int16_t blastradius, blastdamage;   // A_RadialBlast, in the remaster's units
    int16_t sight, fuse;                // the mine: arms within sight (Build units), blasts fuse ticks later
    int16_t wood;                       // a barrel: breaks to pot_pc2 (woodbreak.wav) rather than pot_pc1 (glassbreak.wav)
};

struct KexBreakProp
{
    int16_t nSprite, nHealth, nType, nRunFree;
    int16_t nFuse;                      // -1 idle, else ticks to the blast
};

static KexBreakType s_type[kMaxKexBreakTypes];
static int          s_types = -1;          // -1 = not read yet
static KexBreakProp s_prop[kMaxKexBreaks];
static int          s_props;
static int          s_selftest;   // VB_KEXBREAK_TEST: shatter everything after N ticks

static bool KexBreak_ParseLine(char *line, int nRec)
{
    int tile, health, shard0, nshards, clipdist = 32, br = 0, bd = 0, sight = 0, fuse = 0, wood = 0;
    if (Bsscanf(line, "%d %d %d %d %d %d %d %d %d %d", &tile, &health, &shard0, &nshards,
                &clipdist, &br, &bd, &sight, &fuse, &wood) < 4)
        return false;

    KexBreakType &t = s_type[nRec];
    t.tile = tile; t.health = health;
    t.shard0 = shard0; t.nshards = nshards;
    t.clipdist = clamp(clipdist, 1, 255);
    t.blastradius = br; t.blastdamage = bd;
    t.sight = sight; t.fuse = fuse; t.wood = wood;
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

// A sound the remaster has and Build never did -- the transmitter piece, the
// mine, the boulder, the Anubis and Set melee -- named in kexsound.txt under
// a name no VOC carries (k_*). LoadSound() resolves any name through the
// aliases, so it only needs asking; the index is cached, and -1 (no sound
// device, no alias) plays nothing.
#define kMaxKexOwnSounds 16
static struct { char name[12]; int nSound; } s_ownSound[kMaxKexOwnSounds];
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
    s_prop[nProp].nFuse    = -1;
    s_prop[nProp].nRunFree = runlist_AddRunRec(NewRun, nProp, kRunKexBreak);
}

static void KexBreak_Shatter(int nProp);

// The mine (AI_Magic8Ball): arms when a player it can see comes within its
// sight radius, or when it is destroyed -- its death anim is the countdown --
// and blasts when the fuse runs out; under water it also drops a random item,
// the remaster's magic8ball_explode_water. Blinks while it counts.
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
            if (klabs(sprite[nPlayer].x - sprite[nSprite].x) + klabs(sprite[nPlayer].y - sprite[nSprite].y) > t.sight)
                continue;
            if (!cansee(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z - 512, sprite[nSprite].sectnum,
                        sprite[nPlayer].x, sprite[nPlayer].y, sprite[nPlayer].z - 2048, sprite[nPlayer].sectnum))
                continue;
            s_prop[nProp].nFuse = t.fuse;
            { int const s = KexSound_Get("k_mine"); PlayFX2(s >= 0 ? s : StaticSound[kSoundItemSpecial], nSprite); }
            if (s_selftest)
                OSD_Printf("Kex mine: prop %d armed by player %d\n", nProp, i);
            break;
        }
        return;
    }
    sprite[nSprite].shade = (s_prop[nProp].nFuse & 4) ? -64 : 0;
    if (--s_prop[nProp].nFuse > 0)
        return;
    if (SectFlag[sprite[nSprite].sectnum] & kSectUnderwater)
        KexDrop_RandomItem(nSprite, -1);
    s_prop[nProp].nHealth = 0;
    if (s_selftest)
        OSD_Printf("Kex mine: prop %d blasts\n", nProp);
    KexBreak_Shatter(nProp);
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
// Stand an item up at a point and let the item machinery finish it.
static void KexDrop_Spawn(int nSector, int x, int y, int z,
                          short lotag, short hitag, short picnum)
{
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

    ProcessSpriteTag(nItem, lotag, hitag);
}

// A_RandomItem, transcribed from scripts/p_misc.txt: rolled at the end of
// nearly every monster death (the gib anim calls it), and with the maps
// placing almost no health or ammo at all, these drops and the vases are the
// campaign's whole supply line. r = Max(8): 0-2 drop -- an even coin between
// ammo and health, doubled half the time -- and 3-7 drop nothing unless the
// KILLING player is under a quarter health, when mercy gives a berry three
// times in four. The killer's health, not the local player's: everyone in a
// lockstep game must roll the same numbers.
void KexDrop_RandomItem(int nSprite, int nKiller)
{
    if (!g_kexMode)
        return;

    int const nSector = sprite[nSprite].sectnum;
    if (nSector < 0)
        return;
    int const x = sprite[nSprite].x, y = sprite[nSprite].y;
    int const z = sector[nSector].floorz;

    int const r = RandomSize(3);
    if (r > 2)
    {
        if (nKiller >= 0 && sprite[nKiller].statnum == 100)
        {
            int const nPlayer = GetPlayerFromSprite(nKiller);
            if (PlayerList[nPlayer].nHealth < kMaxHealth / 4 && RandomSize(2) != 0)
                KexDrop_Spawn(nSector, x, y, z, 12, 0, 718);   // mercy berry
        }
        return;
    }

    int const big = RandomBit();
    if (RandomBit())
        KexDrop_Spawn(nSector, x, y, z, 6, big ? 20 : 10, 877);  // speed loader
    else
        KexDrop_Spawn(nSector, x, y, z, 12, 0, 718);             // berry twig
}

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

    KexDrop_Spawn(nSector, x, y, z, lotag, hitag, picnum);
}

// The death anim: a break sound, one physics chunk per shard, the random item
// the remaster drops, and the prop is gone.
static void KexBreak_Shatter(int nProp)
{
    int const nSprite = s_prop[nProp].nSprite;
    KexBreakType const &t = s_type[s_prop[nProp].nType];

    PlayFX2(StaticSound[kSoundPotPc1 + (t.wood ? 1 : 0)], nSprite);   // pot_pc2 follows pot_pc1

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
        case 0x20000:   // per tick: nothing to do, unless it is a mine
            KexBreak_MineTick(nProp);
            if (s_prop[nProp].nSprite < 0)
                return;
            if (s_selftest && --s_selftest == 1)
            {
                for (int i = 0; i < s_props; i++)
                    if (s_prop[i].nSprite >= 0)
                    { s_prop[i].nHealth = 0; KexBreak_Shatter(i); }

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
            if (s_prop[nProp].nHealth <= 0)
                return;
            s_prop[nProp].nHealth -= (short)b;
            if (s_prop[nProp].nHealth <= 0)
            {
                // A mine's death is its countdown.
                if (s_type[s_prop[nProp].nType].fuse > 0)
                { s_prop[nProp].nHealth = 1; if (s_prop[nProp].nFuse < 0) s_prop[nProp].nFuse = s_type[s_prop[nProp].nType].fuse; }
                else
                    KexBreak_Shatter(nProp);
            }
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
            {
                if (s_type[s_prop[nProp].nType].fuse > 0)
                { s_prop[nProp].nHealth = 1; if (s_prop[nProp].nFuse < 0) s_prop[nProp].nFuse = s_type[s_prop[nProp].nType].fuse; }
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
// Build's z (SCALE 16 x WORLD 6), down positive. The lid is an island sector
// 320 tall, so the whole of it moves -- floor and ceiling together.
// ---------------------------------------------------------------------------
#define KEX_Z_PER_UNIT 96

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
    int16_t sector, tag;
    int32_t target;       // floor z it is moving to
    int32_t step;         // Build z per tick, signed toward target; 0 = still
    KexScriptedRule const *rule;
};
static KexScripted s_scripted[8];
static int s_scripteds;

void KexScripted_Reset(void) { s_scripteds = 0; }

void KexScripted_Add(int nSector, int nTag)
{
    if (s_scripteds >= (int)ARRAY_SIZE(s_scripted) || (unsigned)nSector >= (unsigned)numsectors)
        return;
    KexScripted &m = s_scripted[s_scripteds++];
    m.sector = (int16_t)nSector; m.tag = (int16_t)nTag;
    m.target = sector[nSector].floorz; m.step = 0; m.rule = NULL;
}

static void KexScripted_Shift(KexScripted &m, int32_t dz)
{
    sector[m.sector].floorz += dz;
    sector[m.sector].ceilingz += dz;
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
            m.target = sector[m.sector].floorz;
            OSD_Printf("Kex scripted: sector %d tag %d moved %d at load\n", m.sector, m.tag, r.atLoad);
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
        m.target = sector[m.sector].floorz - m.rule->amount * KEX_Z_PER_UNIT;
        m.step = (m.rule->amount > 0 ? -1 : 1) * m.rule->speed * KEX_Z_PER_UNIT;
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

    for (int i = 0; i < s_scripteds; i++)
    {
        KexScripted &m = s_scripted[i];
        if (!m.step)
            continue;
        int32_t const left = m.target - sector[m.sector].floorz;
        int32_t dz = m.step;
        if ((dz > 0 && dz > left) || (dz < 0 && dz < left))
            dz = left;
        KexScripted_Shift(m, dz);
        if (sector[m.sector].floorz == m.target)
        {
            m.step = 0;
            OSD_Printf("Kex scripted: sector %d tag %d arrived\n", m.sector, m.tag);
        }
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

// VB_KEXSAVE_TEST=1: save at tick 100 and load the save back at 160, then
// compare the player's place and health -- the per-sector tables the save
// carries are kMaxSectors wide, and the levels past 1024 sectors are where
// that has to hold.
static void Kex_SaveTest(void)
{
    static int s_ticks = -1, sx, sy, sect, hp;
    if (s_ticks < 0)
        s_ticks = getenv("VB_KEXSAVE_TEST") ? 0 : 1000000;
    if (s_ticks >= 1000000)
        return;
    int const nSprite = PlayerList[nLocalPlayer].nSprite;
    if (++s_ticks == 100)
    {
        sx = sprite[nSprite].x; sy = sprite[nSprite].y; sect = sprite[nSprite].sectnum; hp = PlayerList[nLocalPlayer].nHealth;
        void testsave(); testsave();
        OSD_Printf("Kex save test: saved at (%d,%d) sector %d health %d, %d sectors\n", sx, sy, sect, hp, numsectors);
    }
    else if (s_ticks == 160)
    {
        void testload(); testload();
        int const n2 = PlayerList[nLocalPlayer].nSprite;
        OSD_Printf("Kex save test: loaded at (%d,%d) sector %d health %d, %d sectors -> %s\n",
                   sprite[n2].x, sprite[n2].y, sprite[n2].sectnum, PlayerList[nLocalPlayer].nHealth, numsectors,
                   (sprite[n2].sectnum == sect && PlayerList[nLocalPlayer].nHealth == hp) ? "OK" : "MISMATCH");
    }
}

void KexBeam_Tick(void)
{
    Kex_LookAtDebug();
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
struct KexChanRow { char map[32]; int16_t tag, chan; };
static KexChanRow s_chanAll[256];
static int s_chanAllCount = -1;

static bool KexChan_ParseLine(char *line, int nRec)
{
    KexChanRow &r = s_chanAll[nRec];
    int tag, chan;
    if (Bsscanf(line, "%31s %d %d", r.map, &tag, &chan) < 3)
        return false;
    r.tag = (int16_t)tag; r.chan = (int16_t)chan;
    return true;
}

static int KexChan(int tag)
{
    if (tag < 1000)
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
    // A tag of 1000 or more the converter never allotted here: nothing on
    // this level listens on it, and tag % 1000 would land on channel 0,
    // which every tag-0 effect shares. Nothing to fire.
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
// and fire 1010 once the boss actor is gone. So the boss's death fires the
// exit, and a returning player who beat it before finds it gone and the way
// open. The boss is told apart by its statnum (scorp.cpp 122, set.cpp 120)
// and, at load, killed the way KillCreatures() kills: by damage.
struct KexBossRule { char const *map; int16_t statnum; int8_t artifact; int16_t fire; };
static KexBossRule const s_bossRule[] = {
    { "SELBUROW", 122, kKexArtiScepter, 1010 },   // Selkis
    { "SETARENA", 120, kKexArtiShawl,   1010 },   // Set
    { "KILARENA", 121, -1,              1010 },   // the Kilmaat Queen: no artifact, the exit alone
};

// A Kex tag, fired through the channel the converter allotted it.
void KexScript_FireTag(int tag, char const *why)
{
    int const chan = KexChan(tag);
    if (chan < 0)
    {
        OSD_Printf("Kex script: tag %d not allotted on this level, nothing to fire (%s)\n", tag, why);
        return;
    }
    Kex_FireChannel(runlist_AllocChannel(chan));
    OSD_Printf("Kex script: tag %d fired (%s)\n", tag, why);
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

static void KexScript_BossFire(char const *why)
{
    { int const c = KexChan(s_boss->fire); if (c >= 0) Kex_FireChannel(runlist_AllocChannel(c)); }
    OSD_Printf("Kex script: %s, tag %d fired (%s)\n", why, s_boss->fire, s_boss->map);
}

static void KexScript_BossKill(void)
{
    int list[16], n = 0;
    for (int i = headspritestat[s_boss->statnum]; i != -1 && n < 16; i = nextspritestat[i])
        list[n++] = i;
    for (int k = 0; k < n; k++)
        for (int hit = 0; hit < 8 && sprite[list[k]].statnum == s_boss->statnum; hit++)
            runlist_DamageEnemy(list[k], -1, 1600);
}

void KexScript_BossDied(int nSprite)
{
    if (s_boss && sprite[nSprite].statnum == s_boss->statnum)
        KexScript_BossFire("the boss is down");
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
    int const before = headspritestat[s_boss->statnum] != -1;
    KexScript_BossKill();
    if (!before)
        KexScript_BossFire("the artifact is held and the boss is gone");
    else
        OSD_Printf("Kex script: the artifact is held, the boss removed (%s)\n", s_boss->map);
}

void KexScript_BossTick(void)
{
    if (s_bossTest <= 0 || --s_bossTest != 1 || !s_boss)
        return;
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
    for (auto const &r : s_loadRule)
    {
        if (Bstrcmp(base, r.map))
            continue;
        for (int k = 0; k < 2; k++)
            if (r.fire[k] > 0)
            {
                { int const c = KexChan(r.fire[k]); if (c >= 0) Kex_FireChannel(runlist_AllocChannel(c)); }
                OSD_Printf("Kex script: %s $script 0 fires tag %d\n", base, r.fire[k]);
            }
    }
    if (s_scripts)
        OSD_Printf("Kex script: %d script sectors on this level\n", s_scripts);
    s_boss = NULL;
    for (auto const &r : s_bossRule)
        if (!Bstrcmp(base, r.map))
            s_boss = &r;
    { char const *e = getenv("VB_KEXBOSS_TEST"); s_bossTest = e ? 60 : 0; }
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

void KexScript_Tick(void)
{
    KexTrigger_Test();
    for (int i = 0; i < s_scripts; i++)
    {
        KexScript &sc = s_script[i];
        if (sc.done || !sc.rule)
            continue;
        if (!sc.armed)
        {
            for (int nPlayer = 0; nPlayer < nTotalPlayers && !sc.armed; nPlayer++)
            {
                int const nSprite = PlayerList[nPlayer].nSprite;
                if (nSprite >= 0 && sprite[nSprite].sectnum == sc.sector)
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
                int have = 0;
                for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
                    if (PlayerList[nPlayer].keys & (0x1000 << sc.rule->keyBit))
                        have = 1;
                if (!have)
                    break;
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
