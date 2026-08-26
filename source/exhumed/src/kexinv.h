//-------------------------------------------------------------------------
//
// The remaster's collectibles for VibeBuild32.
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
    kKexArtiMask,           // breathe under water (Build's mask, made permanent)
    kKexArtiShawl,          // hold jump while falling to float
    kKexArtiAnklets,        // half damage from lava and slime
    kKexArtiScepter,        // walks through force fields (masked walls listed in kexforce.txt) and laser beams
    kKexArtiFeather,        // hold jump while falling to hover
    kKexArtiCount
};

// The lotags the converter uses (kexmap.py KEX_INV_LOTAG_*).
enum
{
    kKexLotagArtifact    = 34,   // hitag: artifact index
    kKexLotagTransmitter = 36,   // hitag: piece 0..7
    kKexLotagDoll        = 39,   // hitag: doll 0..30
    kKexLotagAnkh        = 40,   // hitag: ankh 1..5
};

#define KEX_DOLL_COUNT      31
#define KEX_DOLLS_DOLPHIN   16   // the team-doll rewards: swim three times as fast
#define KEX_DOLLS_VULTURE   28   // ... and hold jump in the air to fly
#define KEX_TRANS_PIECES    8

struct KexInv
{
    uint8_t  artifacts;      // bit per kKexArti*
    uint8_t  transmitter;    // bit per piece
    uint8_t  ankhs;          // bit per ankh (bit 1..5)
    uint8_t  map;            // this level's map scroll picked up
    uint32_t dolls;          // bit per doll
};

extern KexInv KexInvList[];

void KexInv_Reset(int nPlayer);          // new game
void KexInv_LevelStart(void);            // per level: the map scroll is per level
void KexInv_AddShrinePad(int nSprite, int nArtifact);   // KILENTRY's ARTIFACT_TRIGGER pads
int  KexInv_Pickup(int nPlayer, int nItem, int nHitag);   // 1 = taken
void KexInv_GiveArtifact(int nPlayer, int nArtifact);
int  KexInv_HasArtifact(int nPlayer, int nArtifact);
int  KexInv_DollCount(int nPlayer);
int  KexInv_TransCount(int nPlayer);
int  KexInv_MaxHealth(int nPlayer);      // kMaxHealth, raised by the ankhs in Kex mode

// kexinv.txt: the remaster's own wording, keyed by name (kexmenu.py writes
// it). KexInv_Format expands "${0}" to num and "\n" to a newline.
char const *KexInv_Text(char const *key);
void KexInv_Format(char *out, int outSize, char const *key, int num);

void KexInv_Save(int nSlot);
void KexInv_Load(int nSlot);

void KexInv_Tick(void);                  // per tick: the VB_KEXINV_TEST sweep only
int  KexInv_TestWantsScreen(void);       // that test asking for the status screen
int  KexInv_TestActive(void);
int  KexInv_TestFire(void);              // the Manacle self-test holding fire
int  KexInv_TestAir(void);               // that test asking for the air gauge

#endif
