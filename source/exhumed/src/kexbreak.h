//-------------------------------------------------------------------------
//
// The remaster's breakable props for VibeBuild32.
//
// Vases, barrels and the like are shootable actors in the Kex engine: the
// actor def gives them health and a deathAnim, and that anim tosses one shard
// actor per shard, plays a break sound and destroys the prop. None of that
// survives conversion on its own -- the converter emits them as plain decor
// sprites, which stand there and ignore bullets.
//
//-------------------------------------------------------------------------
#ifndef __kexbreak_h__
#define __kexbreak_h__

int  KexBreak_Available(void);   // the table loaded and this is a Kex map
void KexBreak_Init(void);        // per level: arm every breakable prop sprite
void FuncKexBreak(int a, int b, int nRun);

void KexLight_Init(void);        // per level: collect the remaster's light sources
void KexLight_Tick(void);        // per tick: cast their glow
#define kKexFlashPal 30
void KexFlash_Hit(int nSprite);     // the remaster's white blink on a hit creature
int  KexFlash_Active(int nSprite);
void KexFlash_Tick(void);
int  KexBreak_IsProp(int nSprite);  // a breakable prop's sprite
void    KexVL_Load(char const *mapfile);   // the level's baked light field (KEX_<NAME>.LIT)
void    KexVL_Free(void);
int     KexVL_Ready(void);
int32_t KexVL_ShadeAt(float x, float y, float z, int32_t fallback);   // Build shade at a world point

// The remaster wav that is this Build sound, or NULL; *pLoop tells whether it
// loops (the flag stands in for the VOC loop block a WAV does not have).
char const *KexSound_Alias(char const *name, int *pLoop);
int KexSound_Get(char const *name);      // a Kex-only sound (kexsound.txt name with no VOC), -1 if none

// A_RandomItem: the remaster's enemy-death drop roll; no-op outside Kex mode.
void KexDrop_RandomItem(int nSprite, int nKiller);

void KexTele_Init(void);         // per level: this map's teleport pads
void KexTele_Tick(void);         // per tick: step-on pads move the player

// The sectors the level scripts move (the casket lids). The converter marks
// them with a lotag 64 sprite; the moves are the scripts', kept here.
void KexScripted_Reset(void);           // before the level's sprites are read
void KexScripted_Add(int nSector, int nTag);
void KexScripted_Init(void);            // after: the scripts' load-time moves
void KexScripted_Tick(void);
void KexScripted_Trigger(int when);     // 0 = Ramses begins, 1 = Ramses done

// The remaster's laser beams (kexSlaveLaserBeam): a standing force field from
// a lotag 65 origin sprite to whatever it hits, drawn as the remaster's own
// chain of jittering bolts, hurting on touch unless the Kilmaat Scepter is
// carried. nChannel -1 = always on; otherwise off while the channel is active.
void KexBeam_Reset(void);
void KexBeam_Add(int nSprite, int nChannel);
void KexBeam_Init(void);
void KexBeam_Tick(void);

// ACTOR_TRIGGER: a lotag 66 sprite; fires its channel when every creature that
// stood in its sector at load has died (MAGMA's three Mantis pens).
void KexTrigger_Reset(void);
void KexTrigger_Add(int nSprite, int nChannel);
void KexTrigger_Init(void);
void KexTrigger_Died(int nSprite);       // from runlist_DamageEnemy
void KexHold_Reset(void);                // movers that hold where their tag put them (lotag 73)
void KexHold_Add(int nSprite);
int  KexHold_Sector(int nSector);
int  KexHold_BombFloor(int nSector, int *pZ);   // the EXPLODE sector's authored floor (lotag 74)
int  KexHold_PadTop(int nSector, int *pZ);      // the DROP_PAD's raised height (lotag 81)
void KexForce_Update(void);              // force fields open to a Scepter holder
void KexScript_BossTick(void);          // the VB_KEXBOSS_TEST hook
void KexScript_ArtifactTaken(int nArtifact);   // fires tag 1000 + index
void KexScript_RamsesEnd(void);         // TOMB's crypt door
void KexScript_FireTag(int tag, char const *why);

// SCRIPT sectors: a lotag 67 sprite, hitag = script tag. The remaster's three
// level scripts (KILENTRY, KILARENA, SUNKEN) are transcribed here.
void KexScript_Reset(void);
void KexScript_Add(int nSprite, int nTag);
void KexScript_Init(void);               // the load-time script, where one exists
void KexScript_Tick(void);

#endif
