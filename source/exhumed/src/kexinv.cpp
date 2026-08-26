//-------------------------------------------------------------------------
//
// The remaster's collectibles for VibeBuild32. See kexinv.h.
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
#include "kexinv.h"
#include "kexbreak.h"   // VibeBuild32: KexSound_Get

KexInv KexInvList[kMaxPlayers];

void KexInv_Reset(int nPlayer)
{
    if ((unsigned)nPlayer < (unsigned)kMaxPlayers)
        Bmemset(&KexInvList[nPlayer], 0, sizeof(KexInv));
}

// VB_KEXINV_TEST=<ticks>: that many ticks into a level, take every collectible
// it holds as if walked over, log the tally, then page through the status
// screen unattended, screenshotting each page (see KexMenu_Status).
static int s_selftest;
static int s_wantScreen;

void DestroyItemAnim(short nSprite);     // items.cpp
void ProcessSpriteTag(short nSprite, short nLotag, short nHitag);   // init.cpp

// KILENTRY's shrines. Each has a pad (ARTIFACT_TRIGGER) and a spawn spot
// (ARTIFACT_SPAWN) on the same tag: the artifact appears on the spot when
// the pad is stepped on, for a player who does not have it. The pickup the
// converter planted is taken off the map at load and put back on the pad.
struct KexShrine
{
    int16_t padSector, nArtifact;
    int16_t sector, lotag, hitag, picnum;
    int32_t x, y, z;
    uint8_t held, done;
};
static KexShrine s_shrine[kKexArtiCount];
static int s_shrines;

void KexInv_AddShrinePad(int nSprite, int nArtifact)
{
    if (g_kexMode && s_shrines < kKexArtiCount && (unsigned)nArtifact < (unsigned)kKexArtiCount)
    {
        KexShrine &sh = s_shrine[s_shrines++];
        Bmemset(&sh, 0, sizeof(sh));
        sh.padSector = sprite[nSprite].sectnum;
        sh.nArtifact = (int16_t)nArtifact;
    }
    mydeletesprite(nSprite);
}

static void KexInv_ShrineHold(int nSprite, int nArtifact)
{
    for (int i = 0; i < s_shrines; i++)
    {
        KexShrine &sh = s_shrine[i];
        if (sh.nArtifact != nArtifact || sh.held)
            continue;
        sh.sector = sprite[nSprite].sectnum;
        sh.lotag = sprite[nSprite].statnum - 900;
        sh.hitag = sprite[nSprite].hitag;
        sh.picnum = sprite[nSprite].picnum;
        sh.x = sprite[nSprite].x; sh.y = sprite[nSprite].y; sh.z = sprite[nSprite].z;
        sh.held = 1;
        DestroyItemAnim(nSprite);
        mydeletesprite(nSprite);
        return;
    }
}

static void KexInv_ShrineReveal(KexShrine &sh)
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
    ProcessSpriteTag((short)nSprite, sh.lotag, sh.hitag);
    AddFlash(sh.sector, sh.x, sh.y, sh.z, 80);
    PlayFX2(StaticSound[44] | 0x2000, (short)nSprite);   // tele_1: the remaster's appear sound
    sh.done = 1;
    OSD_Printf("Kex shrine: artifact %d revealed in sector %d\n", sh.nArtifact, sh.sector);
}

static void KexInv_ShrineTick(void)
{
    for (int i = 0; i < s_shrines; i++)
    {
        KexShrine &sh = s_shrine[i];
        if (!sh.held || sh.done)
            continue;
        for (int nPlayer = 0; nPlayer < nTotalPlayers; nPlayer++)
        {
            int const nSprite = PlayerList[nPlayer].nSprite;
            if (nSprite >= 0 && sprite[nSprite].sectnum == sh.padSector)
            {
                KexInv_ShrineReveal(sh);
                break;
            }
        }
    }
}

void KexInv_LevelStart(void)
{
    for (int i = 0; i < kMaxPlayers; i++)
        KexInvList[i].map = 0;

    // KILENTRY's shrines: an artifact planted with the "only if not owned"
    // mark (hitag 16+n; the mask, Build's own lotag 23 pickup, 17) is for a
    // player who arrived without it. Owned already: it is not there.
    if (g_kexMode)
    {
        for (int i = 0; i < kMaxSprites; i++)
        {
            int const st = sprite[i].statnum, h = sprite[i].hitag;
            int idx = -1;
            if (st == 900 + kKexLotagArtifact && h >= 16 && h < 16 + kKexArtiCount) idx = h - 16;
            else if (st == 923 && h == 17) idx = kKexArtiMask;
            if (idx < 0)
                continue;
            if (KexInv_HasArtifact(0, idx))
            {
                DestroyItemAnim(i);
                mydeletesprite(i);
            }
            else
            {
                sprite[i].hitag = (st == 923) ? 0 : (int16_t)idx;
                KexInv_ShrineHold(i, idx);
            }
        }
        if (s_shrines)
            OSD_Printf("Kex shrines: %d pads on this level\n", s_shrines);
    }
    { char const *e = getenv("VB_KEXINV_TEST"); s_selftest = e ? Batoi(e) : 0; }
    s_wantScreen = 0;
}

void PlayerCheckItemPickup(int nPlayer, int nPickupSprite, int valueFlag);   // player.cpp

extern int KexManacle_LastShot;    // gun.cpp
static int s_manacleTest;

int KexInv_TestFire(void) { return s_manacleTest > 20; }
int KexInv_TestAir(void)  { return s_manacleTest > 0; }

void KexInv_Tick(void)
{
    KexInv_ShrineTick();
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
    if (!s_selftest || --s_selftest != 1)
        return;
    // The test stands on every pad first, so the shrines' artifacts are there
    // to take.
    for (int i = 0; i < s_shrines; i++)
        if (s_shrine[i].held && !s_shrine[i].done)
            KexInv_ShrineReveal(s_shrine[i]);
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
            if (nArtifact == kKexArtiScepter)
                KexForce_Update();                // and the force fields, to the Scepter
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

int KexInv_DollCount(int nPlayer)  { return KexInv_Bits(KexInvList[nPlayer].dolls); }
int KexInv_TransCount(int nPlayer) { return KexInv_Bits(KexInvList[nPlayer].transmitter); }

// Each of the five ankhs raises the ceiling by a fifth of the base: all five
// double it. Build's kMaxHealth is 800 for a bar that reads 100.
#define KEX_ANKH_HEALTH (kMaxHealth / 5)
int KexInv_MaxHealth(int nPlayer)
{
    if (!g_kexMode || (unsigned)nPlayer >= (unsigned)kMaxPlayers)
        return kMaxHealth;
    return kMaxHealth + KEX_ANKH_HEALTH * KexInv_Bits(KexInvList[nPlayer].ankhs);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static char *s_text;

static void KexInv_LoadText(void)
{
    if (s_text)
        return;
    int const fil = kopen4loadfrommod("kexinv.txt", 0);
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
    static char s_result[256];
    KexInv_LoadText();

    int const klen = (int)Bstrlen(key);
    for (char const *p = s_text; *p; )
    {
        char const *eol = Bstrchr(p, '\n');
        int const linelen = eol ? (int)(eol - p) : (int)Bstrlen(p);
        if (linelen > klen && p[klen] == ' ' && !Bstrncmp(p, key, klen))
        {
            int n = min(linelen - klen - 1, (int)sizeof(s_result) - 1);
            Bmemcpy(s_result, p + klen + 1, n);
            while (n > 0 && (s_result[n-1] == '\r' || s_result[n-1] == ' '))
                n--;
            s_result[n] = 0;
            return s_result;
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
static void KexInv_Caption(int nPlayer, char const *key, int num)
{
    if (nPlayer != nLocalPlayer)
        return;
    char buf[256];
    KexInv_Format(buf, sizeof(buf), key, num);
    // The message line is one line: the descriptions' second line has to wait
    // for the status screen.
    for (char *q = buf; *q; q++)
        if (*q == '\n') { *q = 0; break; }
    if (buf[0])
        StatusMessage(400, "%s", buf);
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

// ---------------------------------------------------------------------------
// Save slots: a file beside savgamea.sav, one record per slot, so the classic
// save layout (which the loader seeks into by fixed size) is untouched.
// ---------------------------------------------------------------------------
#define KEX_INV_SAVE_FILE   "kexinv.sav"
#define KEX_INV_SAVE_SLOTS  5
static char const s_saveMagic[8] = "KEXINV1";

void KexInv_Save(int nSlot)
{
    if (!g_kexMode || (unsigned)nSlot >= KEX_INV_SAVE_SLOTS)
        return;
    KexInv slots[KEX_INV_SAVE_SLOTS];
    Bmemset(slots, 0, sizeof(slots));

    FILE *fp = fopen(KEX_INV_SAVE_FILE, "rb");
    if (fp)
    {
        char magic[8];
        if (fread(magic, sizeof(magic), 1, fp) != 1 || Bmemcmp(magic, s_saveMagic, sizeof(magic))
            || fread(slots, sizeof(slots), 1, fp) != 1)
            Bmemset(slots, 0, sizeof(slots));
        fclose(fp);
    }
    slots[nSlot] = KexInvList[nLocalPlayer];

    fp = fopen(KEX_INV_SAVE_FILE, "wb");
    if (!fp)
        return;
    fwrite(s_saveMagic, sizeof(s_saveMagic), 1, fp);
    fwrite(slots, sizeof(slots), 1, fp);
    fclose(fp);
}

void KexInv_Load(int nSlot)
{
    KexInv_Reset(nLocalPlayer);
    if (!g_kexMode || (unsigned)nSlot >= KEX_INV_SAVE_SLOTS)
        return;
    FILE *fp = fopen(KEX_INV_SAVE_FILE, "rb");
    if (!fp)
        return;
    char magic[8];
    KexInv slots[KEX_INV_SAVE_SLOTS];
    if (fread(magic, sizeof(magic), 1, fp) == 1 && !Bmemcmp(magic, s_saveMagic, sizeof(magic))
        && fread(slots, sizeof(slots), 1, fp) == 1)
        KexInvList[nLocalPlayer] = slots[nSlot];
    fclose(fp);
}
