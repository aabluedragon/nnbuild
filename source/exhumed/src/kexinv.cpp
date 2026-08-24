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
#include "kexinv.h"

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
                sprite[i].hitag = (st == 923) ? 0 : (int16_t)idx;
        }
    }
    { char const *e = getenv("VB_KEXINV_TEST"); s_selftest = e ? Batoi(e) : 0; }
    s_wantScreen = 0;
}

void PlayerCheckItemPickup(int nPlayer, int nPickupSprite, int valueFlag);   // player.cpp

void KexInv_Tick(void)
{
    if (!s_selftest || --s_selftest != 1)
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
    KexInv const &inv = KexInvList[nLocalPlayer];
    OSD_Printf("Kex inventory: %d pickups taken -> artifacts %02x transmitter %02x dolls %d ankhs %02x\n",
               taken, inv.artifacts, inv.transmitter, KexInv_DollCount(nLocalPlayer), inv.ankhs);
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
        KexInvList[nPlayer].artifacts |= (uint8_t)(1 << nArtifact);
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
            inv.artifacts |= (uint8_t)(1 << nHitag);
            Bsprintf(key, "artiname%d", nHitag);
            KexInv_Caption(nPlayer, key, 0);
            return 1;

        case kKexLotagTransmitter:
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
            // The remaster raises the health ceiling; Build's is a constant
            // that the bar, the healing caps and the venom bowl all assume,
            // so the ankh fills the player up instead.
            inv.ankhs |= (uint8_t)(1 << nHitag);
            PlayerList[nPlayer].nHealth = kMaxHealth;
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
