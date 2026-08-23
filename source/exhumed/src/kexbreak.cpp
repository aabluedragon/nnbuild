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
#include "scorp.h"
#include "random.h"
#include "kexbreak.h"

#define kMaxKexBreakTypes 64
#define kMaxKexBreaks     512

struct KexBreakType
{
    int16_t tile, health, shard0, nshards, clipdist;
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

static void KexBreak_LoadTable(void)
{
    s_types = 0;
    int const fil = kopen4loadfrommod("kexbreak.txt", 0);
    if (fil == -1)
        return;

    int const len = kfilelength(fil);
    char *buf = (char *)Xmalloc(len + 1);
    kread(fil, buf, len);
    buf[len] = 0;
    kclose(fil);

    char const *p = buf;
    while (*p && s_types < kMaxKexBreakTypes)
    {
        char const *eol = Bstrchr(p, '\n');
        if (*p != '#')
        {
            int tile, health, shard0, nshards, clipdist = 32;
            if (Bsscanf(p, "%d %d %d %d %d", &tile, &health, &shard0, &nshards,
                        &clipdist) >= 4)
            {
                KexBreakType &t = s_type[s_types++];
                t.tile = tile; t.health = health;
                t.shard0 = shard0; t.nshards = nshards;
                t.clipdist = clamp(clipdist, 1, 255);
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    Xfree(buf);
}

int KexBreak_Available(void)
{
    if (s_types < 0)
        KexBreak_LoadTable();
    return g_kexMode && s_types > 0;
}

static int KexBreak_TypeOfTile(int picnum)
{
    for (int i = 0; i < s_types; i++)
        if (s_type[i].tile == picnum)
            return i;
    return -1;
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
        int const nType = KexBreak_TypeOfTile(sprite[nSprite].picnum);
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
            BuildScorp(-1, x, y, z, nSector, RandomSize(11), 0);
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
